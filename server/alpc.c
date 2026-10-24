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
    /* Handle-attribute bookkeeping: a message carrying ALPC_MESSAGE_HANDLE_
     * ATTRIBUTE has its handle duplicated into the receiving process at
     * queue time, exactly as the kernel duplicates the object into the
     * receiver's handle table on delivery.  While the message sits queued the
     * duplicate is not yet owned by the receiver: dropping the message (port
     * teardown) closes it, which is the close-notification path; delivering
     * it transfers ownership to the receiver's process. */
    struct process  *handle_proc;  /* receiver the duplicate lives in, NULL = none */
    obj_handle_t     handle_id;    /* duplicate handle in handle_proc */
    struct object   *handle_obj;   /* grabbed object the duplicate refers to */
    /* user data bytes follow */
};

/* wine-internal header prefixed to every message delivered to a receiver so
 * ntdll can reconstruct the kernel-filled PORT_MESSAGE fields (ClientId,
 * MessageId) and the CONTEXT message attribute (PortContext). For a connection
 * request the user bytes are the raw connect payload; for a data message they
 * are the sender's full PORT_MESSAGE. */
#define ALPC_WIRE_MAGIC 0x434e4e41 /* 'ANNC' */
struct alpc_wire_hdr
{
    unsigned int     magic;
    unsigned int     type;        /* LPC message type (10 = connection request) */
    unsigned int     msg_id;
    unsigned int     client_pid;
    unsigned int     client_tid;
    unsigned int     user_size;   /* bytes of user message following */
    client_ptr_t     port_context;
    unsigned int     attr_flags;
    unsigned int     view_flags;
    client_ptr_t     view_base;
    client_ptr_t     view_size;
    /* CONTEXT attribute fields as the kernel fills them on delivery: the
     * MessageContext a sender attached to a request is echoed back to it on
     * the reply answering that request, and carries the connection port
     * context on every message of the connection. */
    client_ptr_t     msg_context;
    unsigned int     sequence;
    unsigned int     callback_id;
};

/* HANDLE attribute as the kernel presents it on delivery. It rides as an
 * optional block between the wire header and the user payload, present only
 * when attr_flags carries ALPC_MESSAGE_HANDLE_ATTRIBUTE (the connect handshake
 * replies, whose caller buffers are sized for the plain header only, must not
 * be enlarged by it): the handle duplicated into the receiving process (a
 * server handle id, translated by ntdll), the notification action (1 =
 * created, 2 = closed, 3 = modified, 4 = closed in a closing process) and the
 * handle count. */
struct alpc_handle_blk
{
    client_ptr_t     handle_id;
    unsigned int     handle_action;
    unsigned int     handle_count;
    unsigned int     pad;
};

#define ALPC_ATTR_HDR_MAGIC 0x41545452 /* 'ATTR' */
struct alpc_msg_attr_hdr
{
    unsigned int     magic;
    unsigned int     attr_flags;
    unsigned int     view_flags;
    unsigned int     pad;
    client_ptr_t     view_base;
    client_ptr_t     view_size;
    client_ptr_t     msg_context;
    unsigned int     sequence;
    unsigned int     callback_id;
    /* sender-side HANDLE attribute: the handle to duplicate on delivery,
     * with its desired access and count (the kernel duplicates exactly one
     * handle per message) */
    client_ptr_t     send_handle;
    unsigned int     desired_access;
    unsigned int     handle_count;
    unsigned int     pad2;
    unsigned int     pad3;
};

/* A pending (not yet accepted) client connection. */
struct alpc_pending_conn
{
    struct list      entry;          /* in alpc_port::pending_conns */
    unsigned int     msg_id;         /* same as the connect message id */
    struct alpc_port *client_port;   /* client-side connection port object */
    struct async     *client_async;  /* async blocking NtAlpcConnectPort */
    char            *conn_data;      /* client's connection message payload */
    data_size_t      conn_data_size;
    char            *conn_reply;     /* server's connection reply message, delivered on accept */
    data_size_t      conn_reply_size;
    unsigned int     client_pid;
    unsigned int     client_tid;
};

struct alpc_port;

struct alpc_port
{
    struct object       obj;
    struct fd          *fd;
    struct process     *process;     /* owning process: the target of handle-attribute
                                      * duplication when a message queues on this port */
    int                 is_server;   /* 1 = server listening port, 0 = connection */

    /* Message queue – both server ports and connections use this */
    struct list         msg_queue;
    struct async_queue  read_q;

    /* Server port only */
    struct list         pending_conns; /* struct alpc_pending_conn */
    struct list         connections;   /* accepted client-side ports (conn_entry) */

    /* Connection only */
    struct alpc_port   *peer;        /* peer connection endpoint, or NULL */
    struct alpc_port   *listen_port; /* client side: the server's listening port */
    struct alpc_port   *reverse;     /* client side: reserve (reverse) port for
                                      * server-originated messages, or NULL */
    struct alpc_port   *owner;       /* reverse port only: the connection it is
                                      * the receive side of; messages queued on
                                      * the reserve port are readiness of that
                                      * connection, so wake notifications of the
                                      * owner must fire too, or NULL */
    client_ptr_t        port_context;/* server side: context passed to accept */
    /* request id of the last request slurped on this connection (tracked on
     * the client-side connection port, shared with its reserve handle): the
     * kernel answers a REPLY-flag send that carries no message id by the
     * connection's outstanding request, stamping the reply's PORT_MESSAGE
     * with that id so the sender's armed receive takes it. */
    unsigned int        outstanding_id;
    unsigned int        client_pid;  /* thread that created the port: a wake packet
                                      * associated with no receive in flight binds to
                                      * the newest port of its own thread, which is
                                      * the connection the kernel auto-arms a callback
                                      * receive on */
    unsigned int        client_tid;
    struct list         conn_entry;  /* client side: entry in listen_port->connections */
    struct list         list_entry;  /* entry in alpc_port_list */

};

/* every live port; a wake completion packet binds, at association time, to
 * the port on which the associating thread is queued receiving, so the
 * association handler must be able to find it */
static struct list alpc_port_list = LIST_INIT(alpc_port_list);

/* LPC message types stamped into the delivered PORT_MESSAGE header. The kernel
 * owns this field: a reply to a pending request is LPC_REPLY, a message the
 * server originates in the reverse direction is LPC_NEW_MESSAGE (delivered to
 * the client's reverse/receive port, never to a request's reply slot). */
#define ALPC_LPC_REQUEST      0
#define ALPC_LPC_REPLY        1
#define ALPC_LPC_NEW_MESSAGE  0x10

/* Field offsets inside the sender's PORT_MESSAGE carried as message user data.
 * The client's ALPC_PORT_MESSAGE is { DataLength/TotalLength u32, Type/
 * DataInfoOffset u32, ClientId (two client pointers), MessageId u32,
 * ClientViewSize union }, so Type sits at +4, the ClientId begins at
 * 2 * pointer (its natural alignment), the MessageId follows the ClientId at
 * 4 * pointer and the header ends at 6 * pointer.  Reading the MessageId at
 * the 32-bit position on x64 picks up the low half of the (sender-zeroed)
 * ClientId and every reply correlates to message id 0, which strands it: a
 * receive armed for its request's id never takes the reply and the
 * conversation deadlocks. */
#define ALPC_USER_TYPE_OFF   4
#define ALPC_USER_MSGID_OFF  (4 * sizeof(client_ptr_t))
#define ALPC_USER_HDR_SIZE   (6 * sizeof(client_ptr_t))

/* ---- Forward declarations ---- */

static int relay_trace_on;
static int relay_trace_enabled( void );
struct alpc_pending_conn;
static struct alpc_port *complete_alpc_connection( struct alpc_port *server_port,
                                                   struct alpc_pending_conn *found,
                                                   client_ptr_t port_context );

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

/* Free a message that never reached its receiver.  A queued handle duplicate
 * was created for a receiver that will now never pick it up: close it in the
 * receiver's table and drop the grabbed references.  This is the kernel's
 * close-notification path for handles parked on a destroyed port. */
static void free_alpc_message( struct alpc_message *msg )
{
    if (msg->handle_id)
    {
        close_handle( msg->handle_proc, msg->handle_id );
        release_object( msg->handle_obj );
        release_object( msg->handle_proc );
    }
    free( msg );
}

/* The message was delivered: the receiver owns the duplicated handle now, the
 * server just drops its bookkeeping references and must not close it. */
static void release_alpc_message( struct alpc_message *msg )
{
    if (msg->handle_id)
    {
        release_object( msg->handle_obj );
        release_object( msg->handle_proc );
    }
    free( msg );
}

static void alpc_port_destroy( struct object *obj )
{
    struct alpc_port *port = (struct alpc_port *)obj;
    struct alpc_message *msg, *next_msg;

    if (!port->fd) return; /* creation was aborted before full init */

    list_remove( &port->list_entry );
    async_wake_up( &port->read_q, STATUS_HANDLES_CLOSED );
    free_async_queue( &port->read_q );

    LIST_FOR_EACH_ENTRY_SAFE( msg, next_msg, &port->msg_queue, struct alpc_message, entry )
    {
        list_remove( &msg->entry );
        free_alpc_message( msg );
    }

    if (port->reverse)
    {
        release_object( port->reverse );
        port->reverse = NULL;
    }
    if (port->owner)
    {
        if (port->owner->reverse == port) port->owner->reverse = NULL;
        release_object( port->owner );
        port->owner = NULL;
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
            if (pc->client_port) release_object( pc->client_port );            free( pc->conn_data );            free( pc );
        }
    }
    else
    {
        if (port->peer)
        {
            async_wake_up( &port->peer->read_q, STATUS_PIPE_DISCONNECTED );
            if (port->peer->peer == port) port->peer->peer = NULL;
            port->peer = NULL;
        }
        /* client side: unlink from the listening port's connection list
         * (safe even when not linked: conn_entry is list_init'd) */
        list_remove( &port->conn_entry );
    }

    if (port->listen_port) release_object( port->listen_port );
    if (port->fd) release_object( port->fd );
    if (port->process) release_object( port->process );
}

/* ---- Fd ops ---- */

static enum server_fd_type alpc_fd_get_type( struct fd *fd )
{
    return FD_TYPE_DEVICE;
}

/* A connection has two client-side handles: the communication port itself, and
 * the reserve (reverse) handle the kernel hands back in the connect reply.  They
 * are two handles of one message queue: a receive armed on either of them takes
 * messages queued on either, and readiness of either handle means a message
 * waiting on either queue.  A client arms the receive for server-initiated
 * traffic on one handle and keys its wake completion packets to that handle, so
 * a message parked on the other handle must still be readable and must still
 * read as readiness. */
static struct alpc_port *alpc_port_pair( struct alpc_port *port )
{
    return port->reverse ? port->reverse : port->owner;
}

/* The client-side connection port of the connection this port belongs to:
 * the connection port itself (it is registered on a listening port), its
 * reserve handle (owned by the connection port), and, reached from the
 * server side, the peer connection port.  The connection's outstanding
 * request id is tracked here so the connection port and its reserve handle
 * share one reply correlation, as they share one message queue. */
static struct alpc_port *alpc_conn_base( struct alpc_port *port )
{
    if (port->owner) return port->owner;         /* reserve handle: its connection */
    if (port->is_server) return NULL;            /* listening port: no single connection */
    if (port->listen_port) return port;          /* client-side connection port */
    if (port->peer) return alpc_conn_base( port->peer ); /* server side: -> client side */
    return NULL;
}

/* Fire the wake completion packets of every handle of the connection this port
 * belongs to: the connection port, the reserve (reverse) handle of the same
 * connection, and, reached from a reserve handle, its owning connection. */
static void wake_connection_waits( struct alpc_port *port )
{
    struct alpc_port *ports[4], *p;
    unsigned int i, n = 0;

    ports[n++] = port;
    if (port->owner) ports[n++] = port->owner;
    if (port->reverse) ports[n++] = port->reverse;
    if (port->owner && port->owner->reverse && port->owner->reverse != port)
        ports[n++] = port->owner->reverse;

    for (i = 0; i < n; i++)
    {
        p = ports[i];
        signal_wait_completion_packets( &p->obj );
        fire_wait_completion_packets( &p->obj );
    }
}

static void update_alpc_readiness( struct alpc_port *port )
{
    struct alpc_port *pair = alpc_port_pair( port );
    int ready = !list_empty( &port->msg_queue );

    if (pair)
    {
        int pair_ready = !list_empty( &pair->msg_queue );
        set_fd_signaled( pair->fd, pair_ready );
        ready |= pair_ready;
    }
    set_fd_signaled( port->fd, ready );
}

/* Deliver queued messages to pending async readers. Messages parked on this
 * port's own queue and messages parked on the queue of its pair (the other
 * handle of the same connection) are both taken by the receivers armed here. */
static void reselect_alpc_port( struct alpc_port *port )
{
    struct alpc_port *queues[2], *queue;
    struct alpc_message *msg, *next;
    unsigned int i;

    queues[0] = port;
    queues[1] = alpc_port_pair( port );

    for (i = 0; i < 2; i++)
    {
        if (!(queue = queues[i])) continue;
        if (i && queue == queues[0]) continue;

        LIST_FOR_EACH_ENTRY_SAFE( msg, next, &queue->msg_queue, struct alpc_message, entry )
        {
            struct async *async = find_pending_alpc_async( &port->read_q, msg->msg_id );
            struct iosb *iosb;

            if (!async) continue;

            iosb = async_get_iosb( async );
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
                release_alpc_message( msg ); /* delivered: receiver owns the handle duplicate */
            }
            release_object( iosb );
            release_object( async );
        }
    }

    /* a port handle is signaled while a message awaits a receiver on either of
     * the connection's queues; once they are drained it stops being signaled */
    update_alpc_readiness( port );
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
    unsigned int reply_id = 0;
    unsigned int msg_type = 0;  /* LPC type to stamp into the delivered PORT_MESSAGE, 0 = keep */
    int reply_stamped = 0;      /* the kernel filled the reply's message id */

    iosb = async_get_iosb( async );
    if (!iosb->in_data || !iosb->in_size)
    {
        release_object( iosb );
        async_request_complete( async, STATUS_SUCCESS, 0, 0, NULL ); /* zero-length send */
        return;
    }

    if (port->is_server)
    {
        /* A reply sent on a listening port carries no connection handle, so the
         * kernel routes it by the context of the request it answers: the message
         * must echo back the message id of a request that a client connection is
         * still waiting for.  A reply with no such pending request is rejected
         * with STATUS_INVALID_INFO_CLASS; the kernel never delivers it to some
         * other client's pending receive, and neither may we: an arbitrary
         * message would then be handed to a client as the reply to its own
         * request, which the sender's protocol reads as a valid (empty) payload. */
        struct alpc_port *conn;
        const void *data = iosb->in_data;
        data_size_t size = iosb->in_size;
        struct alpc_msg_attr_hdr attr;
        unsigned short utype = 0;

        memset( &attr, 0, sizeof(attr) );
        if (size >= sizeof(struct alpc_msg_attr_hdr) && *(const unsigned int *)data == ALPC_ATTR_HDR_MAGIC)
        {
            memcpy( &attr, data, sizeof(attr) );
            data = (const char *)data + sizeof(struct alpc_msg_attr_hdr);
            size -= sizeof(struct alpc_msg_attr_hdr);
        }
        if (size >= ALPC_USER_MSGID_OFF + 4)
            reply_id = *(const unsigned int *)((const char *)data + ALPC_USER_MSGID_OFF);
        if (size >= ALPC_USER_TYPE_OFF + 2)
            utype = *(const unsigned short *)((const char *)data + ALPC_USER_TYPE_OFF);

        peer = NULL;
        if (utype == ALPC_LPC_REPLY)
        {
            /* The LPC connect dance: a REPLY-flag message on the listening
             * port answers a connection request the server slurped there.
             * The kernel creates the connection, hands the reply's payload
             * to the connecting client, and the server keeps no connection
             * handle: its traffic rides the listening port. */
            struct alpc_pending_conn *pc, *target = NULL;

            LIST_FOR_EACH_ENTRY( pc, &port->pending_conns, struct alpc_pending_conn, entry )
            {
                if (reply_id && pc->msg_id != reply_id) continue;
                target = pc;
                break;  /* the oldest pending connect: the slurped request */
            }
            if (target)
            {
                if (size > ALPC_USER_HDR_SIZE
                    && (target->conn_reply = malloc( size - ALPC_USER_HDR_SIZE )))
                {
                    memcpy( target->conn_reply, (const char *)data + ALPC_USER_HDR_SIZE,
                            size - ALPC_USER_HDR_SIZE );
                    target->conn_reply_size = size - ALPC_USER_HDR_SIZE;
                }
                list_remove( &target->entry );
                if (complete_alpc_connection( port, target, attr.msg_context ))
                {
                    async_request_complete( async, STATUS_SUCCESS, iosb->in_size, 0, NULL );
                    release_object( iosb );
                    return;
                }
                set_error( STATUS_NO_MEMORY );
                async_request_complete( async, STATUS_NO_MEMORY, 0, 0, NULL );
                release_object( iosb );
                return;
            }
            /* a reply with no echoed id answers the connection's outstanding
             * (slurped) request: find the armed receiver for that id */
            if (!reply_id)
            {
                LIST_FOR_EACH_ENTRY( conn, &port->connections, struct alpc_port, conn_entry )
                {
                    struct async *matching;

                    if (!conn->outstanding_id) continue;
                    matching = find_reply_alpc_async( &conn->read_q, conn->outstanding_id );
                    if (matching)
                    {
                        release_object( matching );
                        reply_id = conn->outstanding_id;
                        conn->outstanding_id = 0;
                        peer = conn;
                        msg_type = ALPC_LPC_REPLY;
                        reply_stamped = 1;
                        break;
                    }
                }
            }
        }
        if (!peer && reply_id)
        {
            LIST_FOR_EACH_ENTRY( conn, &port->connections, struct alpc_port, conn_entry )
            {
                struct async *matching = find_reply_alpc_async( &conn->read_q, reply_id );
                if (matching)
                {
                    release_object( matching );
                    peer = conn;
                    msg_type = ALPC_LPC_REPLY;
                    break;
                }
            }
        }
        if (!peer)
        {
            /* Nothing is waiting for this message id, so the message is not the
             * answer to a queued request: it is a message that the server side
             * originates in the reverse direction while the client itself waits
             * for its reply.  The kernel queues it on the connection the server
             * accepted, and marks that connection handle ready: the client arms
             * its receives on the connection handle (the reserve handle it got
             * back in the connect reply is a spare it never reads), and the wake
             * completion packets it associated are keyed to the connection.  A
             * receive armed as the receive of a send-receive pair is armed with
             * the id it is waiting for, so it cannot take this id-less message;
             * only a bare receive, which is the receive armed for
             * server-initiated traffic, does. */
            LIST_FOR_EACH_ENTRY( conn, &port->connections, struct alpc_port, conn_entry )
                if (async_waiting( &conn->read_q ))
                {
                    peer = conn;
                    msg_type = ALPC_LPC_NEW_MESSAGE;
                    break;
                }
            if (!peer)
            {
                struct list *head = list_head( &port->connections );
                if (!head)
                {
                    release_object( iosb );
                    set_error( STATUS_PIPE_DISCONNECTED );
                    return;
                }
                peer = LIST_ENTRY( head, struct alpc_port, conn_entry );
                msg_type = ALPC_LPC_NEW_MESSAGE;
            }
            reply_id = 0;
        }
    }
    else
    {
        /* Deliver to peer communication port if a receiver is waiting there,
         * or to the listening port if the server is receiving all connections there. */
        if (port->peer && async_waiting( &port->peer->read_q ))
            peer = port->peer;
        else if (port->listen_port && async_waiting( &port->listen_port->read_q ))
            peer = port->listen_port;
        else if (port->listen_port)
            peer = port->listen_port;
        else
            peer = port->peer;

        if (!peer)
        {
            release_object( iosb );
            set_error( STATUS_PIPE_DISCONNECTED );
            return;
        }
    }

    {
        struct alpc_wire_hdr *hdr;
        struct alpc_msg_attr_hdr attr_hdr;
        obj_handle_t dup = 0;
        struct object *grabbed_obj = NULL;
        const void *user_data = iosb->in_data;
        data_size_t user_size = iosb->in_size;
        data_size_t blob_size;
        /* context the receiver sees = the per-connection context.
         * client request: our connection (peer = server side). server reply:
         * the destination client's connection context. */
        client_ptr_t ctx;
        if (port->is_server)        ctx = peer->port_context;
        else if (port->peer)        ctx = port->peer->port_context;
        else                        ctx = port->port_context;

        memset( &attr_hdr, 0, sizeof(attr_hdr) );
        if (user_size >= sizeof(attr_hdr) && *(const unsigned int *)user_data == ALPC_ATTR_HDR_MAGIC)
        {
            memcpy( &attr_hdr, user_data, sizeof(attr_hdr) );
            user_data = (const char *)user_data + sizeof(attr_hdr);
            user_size -= sizeof(attr_hdr);
        }

        /* Handle attribute: the kernel duplicates the sender's handle into the
         * receiving process's handle table as the message is delivered. Perform
         * the duplication now, at queue time (the receiver arms its receive
         * before the sender writes, and a receive that is already in flight
         * takes the message straight away), and bookkeep it on the message:
         * delivery transfers ownership to the receiver, teardown closes the
         * duplicate (the close-notification path for a handle whose receiver
         * vanished). */
        if (attr_hdr.attr_flags & ALPC_MESSAGE_HANDLE_ATTRIBUTE)
        {
            struct object *hobj = get_handle_obj( current->process,
                        (obj_handle_t)(ULONG_PTR)attr_hdr.send_handle,
                        attr_hdr.desired_access, NULL );
            if (!hobj)
            {
                release_object( iosb );
                return; /* get_handle_obj set the error */
            }
            if (!(dup = alloc_handle( peer->process, hobj, attr_hdr.desired_access, 0 )))
            {
                release_object( hobj );
                release_object( iosb );
                set_error( STATUS_NO_MEMORY );
                return;
            }
            grabbed_obj = grab_object( hobj ); /* keep a reference for the queue record */
            release_object( hobj );            /* the handle and the record own the rest */
        }

        blob_size = sizeof(*hdr) + (dup ? sizeof(struct alpc_handle_blk) : 0) + user_size;
        if (!(msg = malloc( sizeof(*msg) + blob_size )))
        {
            if (dup)
            {
                close_handle( peer->process, dup );
                release_object( grabbed_obj );
            }
            release_object( iosb );
            set_error( STATUS_NO_MEMORY );
            return;
        }
        memset( msg, 0, sizeof(*msg) );
        msg->is_connect = 0;
        if (dup)
        {
            msg->handle_proc = (struct process *)grab_object( peer->process );
            msg->handle_id   = dup;
            msg->handle_obj  = grabbed_obj;
        }
        msg->msg_id = reply_id;
        if (!msg->msg_id && user_size >= ALPC_USER_MSGID_OFF + 4)
        {
            unsigned int mid = *(const unsigned int *)((const char *)user_data + ALPC_USER_MSGID_OFF);
            unsigned short mtype = *(const unsigned short *)((const char *)user_data + ALPC_USER_TYPE_OFF);
            struct alpc_port *base = alpc_conn_base( port );

            if (mtype == ALPC_LPC_REQUEST && mid)
            {
                /* a request slurped on this connection becomes its outstanding
                 * request: the kernel serialises one per connection */
                if (base) base->outstanding_id = mid;
                msg->msg_id = mid;
            }
            else if (mtype == ALPC_LPC_REPLY && !mid && base && base->outstanding_id)
            {
                /* a REPLY-flag send with no id answers the outstanding request
                 * (LPC serialisation: slurp, process, commit): the kernel
                 * stamps the reply with the request's id */
                msg->msg_id = base->outstanding_id;
                base->outstanding_id = 0;
                reply_stamped = 1;  /* the delivered PORT_MESSAGE gets the id below */
            }
            else msg->msg_id = mid;
        }
        msg->data_size  = blob_size;

        hdr = (struct alpc_wire_hdr *)((char *)msg + sizeof(*msg));
        hdr->magic        = ALPC_WIRE_MAGIC;
        hdr->type         = 1; /* LPC_REQUEST/REPLY – distinguished by the user PORT_MESSAGE */
        hdr->msg_id       = msg->msg_id;
        hdr->client_pid   = get_process_id( current->process );
        hdr->client_tid   = get_thread_id( current );
        hdr->user_size    = user_size;
        hdr->port_context = ctx;
        hdr->attr_flags   = attr_hdr.attr_flags;
        hdr->view_flags   = attr_hdr.view_flags;
        hdr->view_base    = attr_hdr.view_base;
        hdr->view_size    = attr_hdr.view_size;
        hdr->msg_context  = attr_hdr.msg_context;
        hdr->sequence     = attr_hdr.sequence;
        hdr->callback_id  = attr_hdr.callback_id;
        /* the receiver sees the duplicated handle with action "created" */
        if (dup)
        {
            struct alpc_handle_blk *blk = (struct alpc_handle_blk *)(hdr + 1);
            hdr->attr_flags |= ALPC_MESSAGE_HANDLE_ATTRIBUTE;
            blk->handle_id     = (client_ptr_t)dup;
            blk->handle_action = 1; /* LPC handle action: created */
            blk->handle_count  = 1;
            blk->pad           = 0;
            if (user_size) memcpy( (char *)hdr + sizeof(*hdr) + sizeof(*blk), user_data, user_size );
        }
        else if (user_size) memcpy( hdr + 1, user_data, user_size );
        /* the kernel fills the reply's MessageId with the id of the request it
         * answers: stamp the delivered copy so the receiver sees it too */
        if (reply_stamped) *(unsigned int *)((char *)(hdr + 1) + ALPC_USER_MSGID_OFF) = msg->msg_id;
        if (!port->is_server && user_size >= ALPC_USER_MSGID_OFF + 4)
        {
            /* a client write echoing a message id a server receive is armed for
             * is a reply: the kernel stamps it LPC_REPLY */
            unsigned int mid = msg->msg_id;
            struct async *m = mid ? find_reply_alpc_async( &peer->read_q, mid ) : NULL;
            if (m)
            {
                release_object( m );
                msg_type = ALPC_LPC_REPLY;
            }
        }
        /* the stamped type lands in the delivered PORT_MESSAGE's Type field
         * (u16 at +4 in both the 32- and 64-bit layouts) */
        if (msg_type && user_size >= ALPC_USER_TYPE_OFF + 2)
            *(unsigned short *)((char *)(hdr + 1) + ALPC_USER_TYPE_OFF) = msg_type;
        if (relay_trace_enabled())
            fprintf( stderr, "ALPC relay from %x:%x port %p(%s) -> %p: msg_id %u lpc %u size %u dup %u\n",
                     hdr->client_pid, hdr->client_tid, (void *)port, port->is_server ? "srv" : "conn",
                     (void *)peer, msg->msg_id, msg_type, user_size, dup );
    }

    list_add_tail( &peer->msg_queue, &msg->entry );
    wake_up( &peer->obj, 0 );
    reselect_alpc_port( peer );
    /* readiness of any handle of a connection is readiness of all of them: the
     * client keys its wake completion packets to the handle it arms the receive
     * on, which for the server side is the listening port and for the client
     * side may be the connection handle or its reserve handle */
    wake_connection_waits( peer );
    /* the port handle becomes signaled while the message stays queued
     * (delivered messages cleared it in reselect_alpc_port); a message on a
     * reserve port is readiness of its connection handle too */
    update_alpc_readiness( peer );

    async_request_complete( async, STATUS_SUCCESS, iosb->in_size, 0, NULL );
    release_object( iosb );
}

/* ---- Helpers ---- */

/* the port this thread is queued receiving on, if any: the wake completion
 * packet's completion source is the receive in flight when it is associated
 * (the kernel binds the packet to that IRP), so the associate handler
 * resolves the binding here */
/* is a message already queued on the port?  a wake packet bound to a port
 * whose queue is not empty has its completion IRP finished at binding time,
 * so the kernel fires it the moment the association is made */
int alpc_port_ready( struct object *obj )
{
    struct alpc_port *port, *pair;

    LIST_FOR_EACH_ENTRY( port, &alpc_port_list, struct alpc_port, list_entry )
    {
        if (&port->obj != obj) continue;
        if (!list_empty( &port->msg_queue )) return 1;
        /* a message waiting on the other handle of the same connection is a
         * completed receive of this port too */
        if ((pair = alpc_port_pair( port )) && !list_empty( &pair->msg_queue )) return 1;
        return 0;
    }
    return 0;
}

struct object *find_thread_alpc_receive_port( struct thread *thread )
{
    struct alpc_port *port, *candidate = NULL;

    LIST_FOR_EACH_ENTRY( port, &alpc_port_list, struct alpc_port, list_entry )
    {
        if (async_queue_has_thread( &port->read_q, thread ))
            return grab_object( &port->obj );
    }

    /* with no receive in flight the wake is armed for the conversation this
     * thread is setting up: the kernel auto-arms the callback receive on the
     * port the thread created during the handshake and binds the packet to
     * that IRP, so the newest port this thread created is the receive the
     * wake completes */
    LIST_FOR_EACH_ENTRY( port, &alpc_port_list, struct alpc_port, list_entry )
    {
        if (port->client_tid == get_thread_id( thread ) &&
            port->client_pid == get_process_id( current->process ))
            candidate = port; /* creation order: the newest wins */
    }
    return candidate ? grab_object( &candidate->obj ) : NULL;
}

/* Wake every ALPC receive parked on a process's ports.
 *
 * CoreMessaging parks a thread in an indefinite receive (a recv_alpc_port
 * request-async sitting in the port's read queue).  Such a thread is inside
 * a blocking server call with the cooperative-suspend signal blocked, so a
 * debugger attach that suspends the process can never complete it and the
 * attach handshake stalls.  Waking the queued receives returns the receive to
 * its caller (with the wake status), which lets the cooperative suspend run;
 * the caller re-arms the receive when it resumes. */
void alpc_wake_process_receives( struct process *process )
{
    struct alpc_port *port;

    LIST_FOR_EACH_ENTRY( port, &alpc_port_list, struct alpc_port, list_entry )
    {
        if (port->process == process) async_wake_up( &port->read_q, STATUS_USER_APC );
    }
}

static struct alpc_port *create_alpc_port_obj( int is_server )
{
    struct alpc_port *port;
    unsigned int opts = FILE_SYNCHRONOUS_IO_NONALERT;

    if (!(port = alloc_object( &alpc_port_ops ))) return NULL;

    port->process     = (struct process *)grab_object( current->process );
    port->is_server   = is_server;
    port->peer        = NULL;
    port->listen_port = NULL;
    port->reverse     = NULL;
    port->owner       = NULL;
    port->port_context = 0;
    port->outstanding_id = 0;
    port->client_pid  = get_process_id( current->process );
    port->client_tid  = get_thread_id( current );
    list_init( &port->msg_queue );
    init_async_queue( &port->read_q );
    list_init( &port->pending_conns );
    list_init( &port->connections );
    list_init( &port->conn_entry );

    port->fd = alloc_pseudo_fd( &alpc_fd_ops, &port->obj, opts );
    if (!port->fd)
    {
        release_object( port );
        return NULL;
    }
    list_add_tail( &alpc_port_list, &port->list_entry );
    return port;
}

/* Queue a connect-notification message to the server port's msg_queue. */
static int queue_connect_notification( struct alpc_port *server_port, struct alpc_pending_conn *pc )
{
    struct alpc_message *msg;
    struct alpc_wire_hdr *hdr;
    data_size_t blob_size = sizeof(*hdr) + pc->conn_data_size;

    if (!(msg = malloc( sizeof(*msg) + blob_size )))
    {
        set_error( STATUS_NO_MEMORY );
        return 0;
    }
    memset( msg, 0, sizeof(*msg) );
    msg->is_connect = 1;
    msg->msg_id     = pc->msg_id;
    msg->data_size  = blob_size;

    hdr = (struct alpc_wire_hdr *)((char *)msg + sizeof(*msg));
    hdr->magic        = ALPC_WIRE_MAGIC;
    hdr->type         = 10; /* LPC_CONNECTION_REQUEST */
    hdr->msg_id       = pc->msg_id;
    hdr->client_pid   = pc->client_pid;
    hdr->client_tid   = pc->client_tid;
    hdr->user_size    = pc->conn_data_size;
    hdr->port_context = 0;
    hdr->attr_flags   = 0;
    hdr->view_flags   = 0;
    hdr->view_base    = 0;
    hdr->view_size    = 0;
    hdr->msg_context  = 0;
    hdr->sequence     = 0;
    hdr->callback_id  = 0;
    hdr->attr_flags   = 0;
    if (pc->conn_data_size)
        memcpy( hdr + 1, pc->conn_data, pc->conn_data_size );
    list_add_tail( &server_port->msg_queue, &msg->entry );
    wake_up( &server_port->obj, 0 );
    reselect_alpc_port( server_port );
    /* notify any wait completion packet associated with this port (the standard
     * NtAssociateWaitCompletionPacket-based server wait used by CoreMessaging). */
    signal_wait_completion_packets( &server_port->obj );
    fire_wait_completion_packets( &server_port->obj );
    if (!list_empty( &server_port->msg_queue )) set_fd_signaled( server_port->fd, 1 );
    return 1;
}

/* ---- Request handlers ---- */

static int relay_trace_enabled( void )
{
    static int once;
    if (!once++) relay_trace_on = getenv( "WINE_ALPC_RELAY_TRACE" ) != NULL;
    return relay_trace_on;
}

DECL_HANDLER(create_alpc_port)
{
    struct alpc_port *port;
    struct unicode_str name;
    struct object *root;
    const struct security_descriptor *sd;
    const struct object_attributes *objattr = get_req_object_attributes( &sd, &name, &root );

    if (!objattr) return;

    port = create_named_object( root, &alpc_port_ops, &name, objattr->attributes | OBJ_OPENIF, sd );
    if (root) release_object( root );
    if (!port) return;

    if (get_error() != STATUS_OBJECT_NAME_EXISTS)
    {
        /* Newly created: initialize all fields */
        port->process     = (struct process *)grab_object( current->process );
        port->is_server   = 1;
        port->peer        = NULL;
        port->listen_port = NULL;
        port->reverse     = NULL;
        port->owner       = NULL;
        port->port_context = 0;
        port->outstanding_id = 0;
        port->client_pid  = get_process_id( current->process );
        port->client_tid  = get_thread_id( current );
        list_init( &port->msg_queue );
        init_async_queue( &port->read_q );
        list_init( &port->pending_conns );
        list_init( &port->connections );
        list_init( &port->conn_entry );
        port->fd = alloc_pseudo_fd( &alpc_fd_ops, &port->obj, FILE_SYNCHRONOUS_IO_NONALERT );
        if (!port->fd)
        {
            release_object( port );
            return;
        }
        list_add_tail( &alpc_port_list, &port->list_entry );
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
    pc->conn_data    = NULL;
    pc->conn_data_size = 0;
    pc->conn_reply   = NULL;
    pc->conn_reply_size = 0;
    pc->client_pid   = get_process_id( current->process );
    pc->client_tid   = get_thread_id( current );

    /* Capture the client's connection-message payload (follows the object
     * attributes), which the server validates before accepting. */
    {
        data_size_t cdata_size;
        const void *cdata = get_req_data_after_objattr( objattr, &cdata_size );
        if (cdata && cdata_size)
        {
            if ((pc->conn_data = malloc( cdata_size )))
            {
                memcpy( pc->conn_data, cdata, cdata_size );
                pc->conn_data_size = cdata_size;
            }
        }
    }

    /* Create handle for client now; peer will be wired up after accept */
    reply->handle = alloc_handle( current->process, client_port, GENERIC_ALL, 0 );
    if (!reply->handle)
    {
        release_object( client_port );
        free( pc->conn_data );
        free( pc );
        release_object( server_port );
        return;
    }

    /* Queue connect notification to server port */
    list_add_tail( &server_port->pending_conns, &pc->entry );
    if (!queue_connect_notification( server_port, pc ))
    {
        list_remove( &pc->entry );
        close_handle( current->process, reply->handle );
        reply->handle = 0;
        release_object( client_port );        free( pc->conn_data );        free( pc );
        release_object( server_port );
        return;
    }

    /* Block client until server calls accept_alpc_connection */
    {
        struct async *async = create_request_async( client_port->fd,
                                                     get_fd_comp_flags(client_port->fd),
                                                     &req->async, 1 );
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
            release_object( client_port );            free( pc->conn_data );            free( pc );
        }
    }

    release_object( client_port );
    release_object( server_port );
}

/* Complete an accepted connection: create the server-side endpoint, wire the
 * peer links, route the client's writes to the listening port, and wake the
 * blocked client with the server's connection reply message.
 *
 * The kernel accepts a connection through two doorways: the explicit
 * NtAlpcAcceptConnectPort, and the LPC connect dance that the registrar and
 * RPC-family servers use — the server slurps the connection request on the
 * listening port and answers it with a REPLY-flag send on that port.  The
 * reply's payload is the connection reply message, and the server never gets
 * a connection handle: all its traffic rides the listening port and the
 * kernel routes its replies by message id.  Both doorways end here.
 *
 * The found record is consumed.  Returns the created server-side port (with
 * a reference for the caller) so the accept route can hand out a handle, or
 * NULL on allocation failure. */
static struct alpc_port *complete_alpc_connection( struct alpc_port *server_port,
                                                   struct alpc_pending_conn *found,
                                                   client_ptr_t port_context )
{
    struct alpc_port *server_conn_port;

    /* Create server-side connection endpoint and wire up the peer links */
    if (!(server_conn_port = create_alpc_port_obj( 0 )))
    {
        if (found->client_async)
        {
            async_terminate( found->client_async, STATUS_NO_MEMORY );
            release_object( found->client_async );
            found->client_async = NULL;
        }
        release_object( found->client_port );
        free( found->conn_data );
        free( found->conn_reply );
        free( found );
        return NULL;
    }

    server_conn_port->peer  = (struct alpc_port *)grab_object( found->client_port );
    found->client_port->peer = (struct alpc_port *)grab_object( server_conn_port );

    /* The server receives all client messages on its listening port, so route
     * the client side's writes there. The server side replies to the client. */
    found->client_port->listen_port = (struct alpc_port *)grab_object( server_port );
    server_conn_port->port_context  = port_context;
    found->client_port->port_context = port_context;
    /* track the connection on the listening port for reply routing */
    list_add_tail( &server_port->connections, &found->client_port->conn_entry );

    /* Wake blocked client, carrying the server's connection reply message to it.
     * The kernel completes a connect with the server's reply message plus the
     * attributes of the connection: the CONTEXT attribute (the connection port
     * context the server passed to accept, the connect message id) and the
     * RESERVE attribute (the reverse port).  Prefix a wine-internal wire header
     * carrying that information so ntdll can fill the caller's message
     * attributes exactly as the kernel would. */
    if (found->client_async)
    {
        struct alpc_wire_hdr *whdr;
        data_size_t total = sizeof(*whdr) + found->conn_reply_size;

        if ((whdr = malloc( total )))
        {
            whdr->magic        = ALPC_WIRE_MAGIC;
            whdr->type         = 10; /* LPC_CONNECTION_REQUEST reply */
            whdr->msg_id       = found->msg_id;
            whdr->client_pid   = get_process_id( current->process );
            whdr->client_tid   = get_thread_id( current );
            whdr->user_size    = found->conn_reply_size;
            whdr->port_context = port_context;
            whdr->attr_flags   = 0x20000000; /* ALPC_MESSAGE_CONTEXT_ATTRIBUTE */
            whdr->view_flags   = 0;
            whdr->view_base    = 0;
            whdr->view_size    = 0;
            whdr->msg_context  = 0;
            whdr->sequence     = 0;
            whdr->callback_id  = 0;
            if (found->conn_reply_size)
                memcpy( whdr + 1, found->conn_reply, found->conn_reply_size );
            async_request_complete_alloc( found->client_async, STATUS_SUCCESS,
                                          total, total, whdr );
            free( whdr );
        }
        else if (found->conn_reply_size)
            async_request_complete_alloc( found->client_async, STATUS_SUCCESS,
                                          found->conn_reply_size, found->conn_reply_size,
                                          found->conn_reply );
        else async_request_complete( found->client_async, STATUS_SUCCESS, 0, 0, NULL );
        release_object( found->client_async );
        found->client_async = NULL;
    }

    release_object( found->client_port );
    free( found->conn_data );
    free( found->conn_reply );
    free( found );
    return server_conn_port;
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

    /* A server that accepts completes the handshake with a connection reply message,
     * written into the buffer that carried the connect request.  The kernel hands that
     * message to the connecting client, which gets its connection context from it;
     * without it the client proceeds with an empty context. */
    if (req->accept)
    {
        data_size_t size = get_req_data_size();
        const void *data = get_req_data();
        if (data && size && (found->conn_reply = malloc( size )))
        {
            memcpy( found->conn_reply, data, size );
            found->conn_reply_size = size;
        }
    }

    if (!req->accept)
    {
        /* Reject */
        if (found->client_async)
        {
            async_terminate( found->client_async, STATUS_CONNECTION_REFUSED );
            release_object( found->client_async );
        }
        release_object( found->client_port );
        free( found->conn_data );
        free( found->conn_reply );
        free( found );
        release_object( server_port );
        return;
    }

    /* Create server-side connection endpoint, wire the peers, and wake the
     * blocked client with the connection reply message. */
    if ((server_conn_port = complete_alpc_connection( server_port, found, req->port_context )))
    {
        reply->handle = alloc_handle( current->process, server_conn_port,
                                      GENERIC_READ | GENERIC_WRITE | SYNCHRONIZE, 0 );
        release_object( server_conn_port );
    }
    release_object( server_port );
}

DECL_HANDLER(wire_alpc_reverse_port)
{
    struct alpc_port *conn, *rev;

    conn = (struct alpc_port *)get_handle_obj( current->process, req->conn, 0, &alpc_port_ops );
    if (!conn) return;

    rev = (struct alpc_port *)get_handle_obj( current->process, req->reverse, 0, &alpc_port_ops );
    if (!rev)
    {
        release_object( conn );
        return;
    }

    if (conn->is_server || !conn->peer || conn->reverse)
    {
        set_error( STATUS_INVALID_PARAMETER );
        release_object( conn );
        release_object( rev );
        return;
    }

    /* The reverse port is a second client-side endpoint on the same connection:
     * server-originated messages land on it, and replies written on it travel
     * to the server side like those of the connection port itself.  The client
     * created it as a listening port through NtAlpcCreatePort, but wired into
     * the connection it is purely a communication endpoint. */
    conn->reverse = (struct alpc_port *)grab_object( rev );
    rev->is_server = 0;
    rev->owner = (struct alpc_port *)grab_object( conn );
    rev->peer = (struct alpc_port *)grab_object( conn->peer );
    if (conn->listen_port) rev->listen_port = (struct alpc_port *)grab_object( conn->listen_port );
    rev->port_context = conn->port_context;

    release_object( conn );
    release_object( rev );
}

DECL_HANDLER(recv_alpc_port)
{
    struct alpc_port *port;
    struct async *async;
    struct event *event = NULL;

    port = (struct alpc_port *)get_handle_obj( current->process, req->port,
                                               FILE_READ_DATA, &alpc_port_ops );
    if (!port) return;

    if (req->event && !(event = get_event_obj( current->process, req->event, EVENT_MODIFY_STATE )))
    {
        release_object( port );
        return;
    }

    if ((async = create_request_async( port->fd, get_fd_comp_flags( port->fd ), &req->async, 0 )))
    {
        if (event) async_set_event( async, event );
        alpc_fd_read( port->fd, async, 0 );
        reply->wait = async_handoff( async, NULL, 1 );
        release_object( async );
    }

    if (event) release_object( event );
    release_object( port );
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
        if (port->peer->peer == port)
        {
            release_object( port->peer->peer ); /* release client's ref to server */
            port->peer->peer = NULL;
        }
        release_object( port->peer );
        port->peer = NULL;
    }

    release_object( port );
}
