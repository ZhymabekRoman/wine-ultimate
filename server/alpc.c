/*
 * Server-side ALPC port implementation
 *
 * Copyright 2024 Zhymabek Roman
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include "config.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "winternl.h"

#include "object.h"
#include "handle.h"
#include "request.h"
#include "process.h"
#include "thread.h"
#include "file.h"

/* ---- Structures ---- */

/* A queued message (used for both server-port connect notifications and
 * per-connection data messages). */
struct alpc_message
{
    struct list      entry;
    unsigned int     msg_id;     /* for connect msgs: the pending-conn id */
    int              is_connect; /* 1 = connect notification */
    data_size_t      data_size;  /* size of user data that follows */
    /* user data bytes follow */
};

/* A pending (not yet accepted) client connection. */
struct alpc_pending_conn
{
    struct list      entry;          /* in alpc_port::pending_conns */
    unsigned int     msg_id;         /* same as the connect message id */
    struct alpc_port *client_port;   /* client-side connection port object */
    struct async     *client_async;  /* async blocking NtAlpcConnectPort */
};

struct alpc_port;

struct alpc_port
{
    struct object       obj;
    struct fd          *fd;
    int                 is_server;   /* 1 = server listening port, 0 = connection */

    /* Message queue – both server ports and connections use this */
    struct list         msg_queue;
    struct async_queue  read_q;

    /* Server port only */
    struct list         pending_conns; /* struct alpc_pending_conn */

    /* Connection only */
    struct alpc_port   *peer;        /* peer connection endpoint, or NULL */
};

/* ---- Forward declarations ---- */

static void alpc_port_dump( struct object *obj, int verbose );
static struct fd *alpc_port_get_fd( struct object *obj );
static void alpc_port_destroy( struct object *obj );
static unsigned int alpc_port_map_access( struct object *obj, unsigned int access );

static const struct object_ops alpc_port_ops =
{
    sizeof(struct alpc_port),  /* size */
    &no_type,                  /* type */
    alpc_port_dump,            /* dump */
    NULL,                      /* add_queue */
    NULL,                      /* remove_queue */
    NULL,                      /* signaled */
    NULL,                      /* satisfied */
    no_signal,                 /* signal */
    alpc_port_get_fd,          /* get_fd */
    default_fd_get_sync,       /* get_sync */
    alpc_port_map_access,      /* map_access */
    default_get_sd,            /* get_sd */
    default_set_sd,            /* set_sd */
    default_get_full_name,     /* get_full_name */
    no_lookup_name,            /* lookup_name */
    directory_link_name,       /* link_name */
    default_unlink_name,       /* unlink_name */
    no_open_file,              /* open_file */
    no_kernel_obj_list,        /* get_kernel_obj_list */
    no_close_handle,           /* close_handle */
    alpc_port_destroy,         /* destroy */
};

static void alpc_fd_read( struct fd *fd, struct async *async, file_pos_t pos );
static void alpc_fd_write( struct fd *fd, struct async *async, file_pos_t pos );
static enum server_fd_type alpc_fd_get_type( struct fd *fd );

static const struct fd_ops alpc_fd_ops =
{
    default_fd_get_poll_events,
    default_poll_event,
    alpc_fd_get_type,
    alpc_fd_read,
    alpc_fd_write,
    no_fd_flush,
    default_fd_get_file_info,
    no_fd_get_volume_info,
    no_fd_ioctl,
    default_fd_cancel_async,
    no_fd_queue_async,
    default_fd_reselect_async,
};

/* ---- Object ops ---- */

static void alpc_port_dump( struct object *obj, int verbose )
{
    struct alpc_port *port = (struct alpc_port *)obj;
    fprintf( stderr, "ALPC %s port\n", port->is_server ? "server" : "connection" );
}

static struct fd *alpc_port_get_fd( struct object *obj )
{
    struct alpc_port *port = (struct alpc_port *)obj;
    return (struct fd *)grab_object( port->fd );
}

static unsigned int alpc_port_map_access( struct object *obj, unsigned int access )
{
    if (access & GENERIC_READ)    access |= FILE_GENERIC_READ;
    if (access & GENERIC_WRITE)   access |= FILE_GENERIC_WRITE;
    if (access & GENERIC_EXECUTE) access |= STANDARD_RIGHTS_EXECUTE;
    if (access & GENERIC_ALL)     access |= FILE_ALL_ACCESS;
    return access & ~(GENERIC_READ | GENERIC_WRITE | GENERIC_EXECUTE | GENERIC_ALL);
}

static void alpc_port_destroy( struct object *obj )
{
    struct alpc_port *port = (struct alpc_port *)obj;
    struct alpc_message *msg, *next_msg;

    if (!port->fd) return; /* creation was aborted before full init */

    async_wake_up( &port->read_q, STATUS_HANDLES_CLOSED );
    free_async_queue( &port->read_q );

    LIST_FOR_EACH_ENTRY_SAFE( msg, next_msg, &port->msg_queue, struct alpc_message, entry )
    {
        list_remove( &msg->entry );
        free( msg );
    }

    if (port->is_server)
    {
        struct alpc_pending_conn *pc, *next_pc;
        LIST_FOR_EACH_ENTRY_SAFE( pc, next_pc, &port->pending_conns,
                                   struct alpc_pending_conn, entry )
        {
            list_remove( &pc->entry );
            if (pc->client_async)
            {
                async_terminate( pc->client_async, STATUS_OBJECT_NAME_NOT_FOUND );
                release_object( pc->client_async );
            }
            if (pc->client_port) release_object( pc->client_port );
            free( pc );
        }
    }
    else if (port->peer)
    {
        async_wake_up( &port->peer->read_q, STATUS_PIPE_DISCONNECTED );
        port->peer->peer = NULL;
        port->peer = NULL;
    }

    if (port->fd) release_object( port->fd );
}

/* ---- Fd ops ---- */

static enum server_fd_type alpc_fd_get_type( struct fd *fd )
{
    return FD_TYPE_DEVICE;
}

/* Deliver queued messages to pending async readers. */
static void reselect_alpc_port( struct alpc_port *port )
{
    struct async *async;

    while (!list_empty( &port->msg_queue )
           && (async = find_pending_async( &port->read_q )))
    {
        struct alpc_message *msg = LIST_ENTRY( list_head(&port->msg_queue),
                                               struct alpc_message, entry );
        struct iosb *iosb = async_get_iosb( async );

        if (iosb->out_size < msg->data_size)
        {
            async_request_complete( async, STATUS_BUFFER_TOO_SMALL, 0, 0, NULL );
        }
        else
        {
            void *data = (char *)msg + sizeof(*msg);
            async_request_complete_alloc( async, STATUS_SUCCESS,
                                           msg->data_size, msg->data_size, data );
            list_remove( &msg->entry );
            free( msg );
        }

        release_object( iosb );
        release_object( async );
    }
}

static void alpc_fd_read( struct fd *fd, struct async *async, file_pos_t pos )
{
    struct alpc_port *port = get_fd_user( fd );

    queue_async( &port->read_q, async );
    reselect_alpc_port( port );
    set_error( STATUS_PENDING );
}

/* Write op: sends a message to the peer connection's queue. */
static void alpc_fd_write( struct fd *fd, struct async *async, file_pos_t pos )
{
    struct alpc_port *port = get_fd_user( fd );
    struct alpc_port *peer;
    struct alpc_message *msg;
    struct iosb *iosb;

    if (port->is_server || !port->peer)
    {
        set_error( STATUS_PIPE_DISCONNECTED );
        return;
    }
    peer = port->peer;

    iosb = async_get_iosb( async );
    if (!iosb->in_data || !iosb->in_size)
    {
        release_object( iosb );
        async_request_complete( async, STATUS_SUCCESS, 0, 0, NULL ); /* zero-length send */
        return;
    }

    msg = malloc( sizeof(*msg) + iosb->in_size );
    if (!msg)
    {
        release_object( iosb );
        set_error( STATUS_NO_MEMORY );
        return;
    }

    msg->is_connect = 0;
    msg->msg_id     = 0;
    msg->data_size  = iosb->in_size;
    memcpy( (char *)msg + sizeof(*msg), iosb->in_data, iosb->in_size );

    list_add_tail( &peer->msg_queue, &msg->entry );
    wake_up( &peer->obj, 0 );
    reselect_alpc_port( peer );

    async_request_complete( async, STATUS_SUCCESS, iosb->in_size, 0, NULL );
    release_object( iosb );
}

/* ---- Helpers ---- */

static struct alpc_port *create_alpc_port_obj( int is_server )
{
    struct alpc_port *port;
    unsigned int opts = FILE_SYNCHRONOUS_IO_NONALERT;

    if (!(port = alloc_object( &alpc_port_ops ))) return NULL;

    port->is_server = is_server;
    port->peer      = NULL;
    list_init( &port->msg_queue );
    init_async_queue( &port->read_q );
    list_init( &port->pending_conns );

    port->fd = alloc_pseudo_fd( &alpc_fd_ops, &port->obj, opts );
    if (!port->fd)
    {
        release_object( port );
        return NULL;
    }
    return port;
}

/* Queue a connect-notification message to the server port's msg_queue. */
static int queue_connect_notification( struct alpc_port *server_port, unsigned int msg_id )
{
    struct alpc_message *msg;

    /* The payload is { unsigned int is_connect=1, unsigned int msg_id }
     * so ntdll can reliably detect this as a connect notification. */
    {
        unsigned int hdr[2];
        msg = malloc( sizeof(*msg) + sizeof(hdr) );
        if (!msg)
        {
            set_error( STATUS_NO_MEMORY );
            return 0;
        }
        hdr[0] = 1; /* is_connect flag */
        hdr[1] = msg_id;
        msg->is_connect = 1;
        msg->msg_id     = msg_id;
        msg->data_size  = sizeof(hdr);
        memcpy( (char *)msg + sizeof(*msg), hdr, sizeof(hdr) );
    }

    list_add_tail( &server_port->msg_queue, &msg->entry );
    wake_up( &server_port->obj, 0 );
    reselect_alpc_port( server_port );
    return 1;
}

/* ---- Request handlers ---- */

DECL_HANDLER(create_alpc_port)
{
    struct alpc_port *port;
    struct unicode_str name;
    struct object *root;
    const struct security_descriptor *sd;
    const struct object_attributes *objattr = get_req_object_attributes( &sd, &name, &root );

    if (!objattr) return;

    port = create_named_object( root, &alpc_port_ops, &name, objattr->attributes, sd );
    if (root) release_object( root );
    if (!port) return;

    if (get_error() != STATUS_OBJECT_NAME_EXISTS)
    {
        /* Newly created: initialize all fields */
        port->is_server = 1;
        port->peer      = NULL;
        list_init( &port->msg_queue );
        init_async_queue( &port->read_q );
        list_init( &port->pending_conns );
        port->fd = alloc_pseudo_fd( &alpc_fd_ops, &port->obj, FILE_SYNCHRONOUS_IO_NONALERT );
        if (!port->fd)
        {
            release_object( port );
            return;
        }
    }

    reply->handle = alloc_handle( current->process, port, req->access, objattr->attributes );
    release_object( port );
}

DECL_HANDLER(connect_alpc_port)
{
    struct alpc_port *server_port;
    struct alpc_port *client_port;
    struct alpc_pending_conn *pc;
    struct unicode_str name;
    struct object *root;
    const struct security_descriptor *sd;
    const struct object_attributes *objattr = get_req_object_attributes( &sd, &name, &root );

    if (!objattr) return;

    /* Look up the named server port */
    server_port = (struct alpc_port *)open_named_object(
                        root, &alpc_port_ops, &name, objattr->attributes );
    if (root) release_object( root );

    if (!server_port) return; /* set_error already set by open_named_object */

    if (!server_port->is_server)
    {
        release_object( server_port );
        set_error( STATUS_OBJECT_TYPE_MISMATCH );
        return;
    }

    /* Create client-side connection endpoint */
    if (!(client_port = create_alpc_port_obj( 0 )))
    {
        release_object( server_port );
        return;
    }

    /* Allocate pending-connection record */
    pc = malloc( sizeof(*pc) );
    if (!pc)
    {
        release_object( client_port );
        release_object( server_port );
        set_error( STATUS_NO_MEMORY );
        return;
    }
    pc->msg_id       = req->msg_id;
    pc->client_port  = (struct alpc_port *)grab_object( client_port );
    pc->client_async = NULL;

    /* Create handle for client now; peer will be wired up after accept */
    reply->handle = alloc_handle( current->process, client_port, GENERIC_ALL, 0 );
    if (!reply->handle)
    {
        release_object( client_port );
        free( pc );
        release_object( server_port );
        return;
    }

    /* Queue connect notification to server port */
    list_add_tail( &server_port->pending_conns, &pc->entry );
    if (!queue_connect_notification( server_port, req->msg_id ))
    {
        list_remove( &pc->entry );
        close_handle( current->process, reply->handle );
        reply->handle = 0;
        release_object( client_port );
        free( pc );
        release_object( server_port );
        return;
    }

    /* Block client until server calls accept_alpc_connection */
    {
        struct async *async = create_request_async( client_port->fd,
                                                     get_fd_comp_flags(client_port->fd),
                                                     &req->async, 0 );
        if (async)
        {
            pc->client_async = (struct async *)grab_object( async );
            /* leave async pending; it is completed later by accept_alpc_connection.
             * async_handoff returns the proper wait handle for the client. */
            set_error( STATUS_PENDING );
            reply->wait = async_handoff( async, NULL, 1 );
            release_object( async );
        }
        else
        {
            /* Allocation failure: clean up */
            list_remove( &pc->entry );
            /* connect notification already queued; remove from server msg queue */
            {
                struct alpc_message *m, *next;
                LIST_FOR_EACH_ENTRY_SAFE( m, next, &server_port->msg_queue,
                                           struct alpc_message, entry )
                {
                    if (m->is_connect && m->msg_id == req->msg_id)
                    {
                        list_remove( &m->entry );
                        free( m );
                        break;
                    }
                }
            }
            close_handle( current->process, reply->handle );
            reply->handle = 0;
            release_object( client_port );
            free( pc );
        }
    }

    release_object( client_port );
    release_object( server_port );
}

DECL_HANDLER(accept_alpc_connection)
{
    struct alpc_port *server_port;
    struct alpc_pending_conn *pc, *found = NULL;
    struct alpc_port *server_conn_port;

    server_port = (struct alpc_port *)get_handle_obj( current->process,
                                                       req->server_port,
                                                       0, &alpc_port_ops );
    if (!server_port) return;

    if (!server_port->is_server)
    {
        release_object( server_port );
        set_error( STATUS_INVALID_PARAMETER );
        return;
    }

    /* Find pending connection by msg_id */
    LIST_FOR_EACH_ENTRY( pc, &server_port->pending_conns, struct alpc_pending_conn, entry )
    {
        if (pc->msg_id == req->msg_id)
        {
            found = pc;
            break;
        }
    }

    if (!found)
    {
        release_object( server_port );
        set_error( STATUS_NOT_FOUND );
        return;
    }

    list_remove( &found->entry );

    if (!req->accept)
    {
        /* Reject */
        if (found->client_async)
        {
            async_terminate( found->client_async, STATUS_CONNECTION_REFUSED );
            release_object( found->client_async );
        }
        release_object( found->client_port );
        free( found );
        release_object( server_port );
        return;
    }

    /* Create server-side connection endpoint and wire up the peer links */
    if (!(server_conn_port = create_alpc_port_obj( 0 )))
    {
        if (found->client_async)
        {
            async_terminate( found->client_async, STATUS_NO_MEMORY );
            release_object( found->client_async );
        }
        release_object( found->client_port );
        free( found );
        release_object( server_port );
        return;
    }

    server_conn_port->peer  = (struct alpc_port *)grab_object( found->client_port );
    found->client_port->peer = (struct alpc_port *)grab_object( server_conn_port );

    reply->handle = alloc_handle( current->process, server_conn_port,
                                   GENERIC_READ | GENERIC_WRITE | SYNCHRONIZE, 0 );
    release_object( server_conn_port );

    /* Wake blocked client */
    if (found->client_async)
    {
        async_request_complete( found->client_async, STATUS_SUCCESS, 0, 0, NULL );
        release_object( found->client_async );
        found->client_async = NULL;
    }

    release_object( found->client_port );
    free( found );
    release_object( server_port );
}

DECL_HANDLER(disconnect_alpc_port)
{
    struct alpc_port *port = (struct alpc_port *)get_handle_obj( current->process,
                                                                   req->handle,
                                                                   0, &alpc_port_ops );
    if (!port) return;

    if (!port->is_server && port->peer)
    {
        async_wake_up( &port->peer->read_q, STATUS_PIPE_DISCONNECTED );
        release_object( port->peer->peer ); /* release client's ref to server */
        port->peer->peer = NULL;
        release_object( port->peer );
        port->peer = NULL;
    }

    release_object( port );
}
