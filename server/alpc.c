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

/* Compile-time switch for the per-message dumps of relayed ALPC traffic.  Off
 * by default: the dumps ride wineserver's message path, so a stderr write per
 * ALPC message dominates the timing of the exchange being traced.  Define to
 * enable them. */
/* #define ALPC_DEBUG_TRACE */

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

/* A request received on a connection and not yet answered.  The kernel keeps
 * the outstanding requests of a connection as a list: a server may receive
 * several requests (posted buffers) and answer them in order, a REPLY-flag
 * send that carries no message id answers the oldest outstanding one, and a
 * new synchronous request is not delivered before the exchange in flight is
 * answered.  Per-buffer message ids of a multi-buffer send are stamped by the
 * kernel as the chain is split, and every id rides this list until its reply. */
struct alpc_outstanding
{
    struct list  entry;
    unsigned int id;
};

/* A synchronous send parked by the serialisation gate: the kernel queues the
 * second synchronous request of a connection on the port's send queue and
 * starts it when the reply of the first drains the outstanding exchange. */
struct alpc_gate_entry
{
    struct list      entry;
    struct async    *async;
    struct alpc_port *port;
};

struct alpc_port
{
    struct object       obj;
    struct fd          *fd;
    struct process     *process;     /* owning process: the target of handle-attribute
                                      * duplication when a message queues on this port */
    int                 is_server;   /* 1 = server listening port, 0 = connection */

    /* Message sequence number: the kernel assigns every message a sequence
     * number from the target port's counter as the message is queued, and
     * reports the last assigned value in AlpcBasicInformation.  CoreMessaging
     * reads it (through the port context carried in the CONTEXT attribute) to
     * confirm that a multi-message ("slurp") exchange arrived as a contiguous
     * pair: the value seen when the exchange starts must equal the number of
     * messages the receiver has taken from the port.  A constant can never
     * match a running receive count, so the client connection state machine
     * stalls and its next NtAlpcSendWaitReceivePort parks on an empty queue. */
    unsigned int        sequence_no;

    /* Port attribute flags (the ALPC_PORTFLG_* bits) as supplied when the port
     * was created; AlpcBasicInformation reports them back unchanged. */
    unsigned int        flags;

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
    /* Outstanding requests of this connection (tracked on the client-side
     * connection port, shared with its reserve handle): the kernel answers a
     * REPLY-flag send that carries no message id by the oldest outstanding
     * request, stamping the reply's PORT_MESSAGE with that id so the sender's
     * armed receive takes it, and serialises synchronous request-reply
     * exchanges: a second synchronous send is parked on the connection's send
     * gate until the reply pops the exchange in flight.
     * On a listening port the same list carries the connect requests slurped
     * there, each outstanding until the server commits its connect exchange
     * with the zero-payload REPLY-flag send on the listening port: the connect
     * domain of reply correlation, kept apart from the data requests of the
     * connections (which ride each connection's own list). */
    struct list         outstanding; /* list of struct alpc_outstanding */
    struct list         send_gate;   /* list of struct alpc_gate_entry */
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

/* LPC message types stamped into the delivered PORT_MESSAGE header (the
 * classic LPC values: the kernel owns this field, a reply to a pending request
 * is LPC_REPLY, a message the server originates in the reverse direction is
 * LPC_NEW_MESSAGE, delivered to the client's reverse/receive port, never to a
 * request's reply slot).  These must match the values ntdll stamps into sent
 * PORT_MESSAGEs (dlls/ntdll/unix/alpc.c). */
#define ALPC_LPC_REQUEST      1
#define ALPC_LPC_REPLY        2
#define ALPC_LPC_NEW_MESSAGE  0xb

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

static void alpc_gate_clear( struct alpc_port *base, unsigned int status );
struct alpc_pending_conn;
static struct alpc_port *complete_alpc_connection( struct alpc_port *server_port,
                                                   struct alpc_pending_conn *found,
                                                   client_ptr_t port_context, int open_commit );

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
    alpc_gate_clear( port, STATUS_HANDLES_CLOSED );
    {
        struct alpc_outstanding *o, *next_o;
        LIST_FOR_EACH_ENTRY_SAFE( o, next_o, &port->outstanding, struct alpc_outstanding, entry )
        {
            list_remove( &o->entry );
            free( o );
        }
    }

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

/* Message ids stamped by the kernel for the buffers of a multi-buffer send.
 * The sender left them zero (the kernel owns them); the range starts high so
 * a stamped id cannot collide with the ids ntdll hands to Wine-side senders
 * (which count up from 1) on the same connection. */
static unsigned int alpc_stamped_id = 0x80000000;

static void alpc_add_outstanding( struct alpc_port *base, unsigned int id )
{
    struct alpc_outstanding *o;

    if (!base || !id) return;
    LIST_FOR_EACH_ENTRY( o, &base->outstanding, struct alpc_outstanding, entry )
        if (o->id == id) return;
    if ((o = malloc( sizeof(*o) )))
    {
        o->id = id;
        list_add_tail( &base->outstanding, &o->entry );
    }
}

/* Answer an outstanding request: with an id, pop that request (the reply
 * echoes the id of the request it answers); with id 0, pop the oldest (the
 * kernel serialises one exchange per connection, so a reply that carries no
 * id of its own answers the request in flight).  Returns the popped id,
 * 0 = none: the connection has nothing outstanding to answer, and the kernel
 * refuses such a reply. */
static unsigned int alpc_pop_outstanding( struct alpc_port *base, unsigned int id )
{
    struct alpc_outstanding *o;

    if (!base) return 0;
    if (id)
    {
        LIST_FOR_EACH_ENTRY( o, &base->outstanding, struct alpc_outstanding, entry )
            if (o->id == id)
            {
                list_remove( &o->entry );
                free( o );
                return id;
            }
        return 0;
    }
    if (!list_empty( &base->outstanding ))
    {
        o = LIST_ENTRY( list_head( &base->outstanding ), struct alpc_outstanding, entry );
        list_remove( &o->entry );
        id = o->id;
        free( o );
        return id;
    }
    return 0;
}

/* Run the synchronous sends parked on the connection's gate: the kernel
 * starts the next queued send the moment the connection has no outstanding
 * exchange. */
static void alpc_drain_send_gate( struct alpc_port *base );

static void alpc_gate_add( struct alpc_port *base, struct alpc_port *port, struct async *async )
{
    struct alpc_gate_entry *g;

    if ((g = malloc( sizeof(*g) )))
    {
        g->async = (struct async *)grab_object( async );
        g->port  = (struct alpc_port *)grab_object( port );
        list_add_tail( &base->send_gate, &g->entry );
        return;
    }
    set_error( STATUS_NO_MEMORY );
    async_request_complete( async, STATUS_NO_MEMORY, 0, 0, NULL );
}

static void alpc_gate_clear( struct alpc_port *base, unsigned int status )
{
    struct alpc_gate_entry *g, *next;

    LIST_FOR_EACH_ENTRY_SAFE( g, next, &base->send_gate, struct alpc_gate_entry, entry )
    {
        list_remove( &g->entry );
        async_terminate( g->async, status );
        release_object( g->async );
        release_object( g->port );
        free( g );
    }
}

static void alpc_drain_send_gate( struct alpc_port *base )
{
    while (!list_empty( &base->send_gate ) && list_empty( &base->outstanding ))
    {
        struct alpc_gate_entry *g = LIST_ENTRY( list_head( &base->send_gate ), struct alpc_gate_entry, entry );
        struct async *async;

        list_remove( &g->entry );
        async = g->async;
        alpc_fd_write( g->port->fd, async, 0 );
        release_object( g->async );
        release_object( g->port );
        free( g );
    }
}

/* The connection's oldest (id 0) outstanding request, 0 = none */
static unsigned int alpc_oldest_outstanding( struct alpc_port *base )
{
    if (base && !list_empty( &base->outstanding ))
        return LIST_ENTRY( list_head( &base->outstanding ), struct alpc_outstanding, entry )->id;
    return 0;
}

/* The messaging layer lays a framing header over the ALPC PORT_MESSAGE whose
 * slots the kernel owns: the delivered message carries a payload length that
 * must satisfy the length identity (TotalLength == payload + 80, a 40-byte
 * framing header inside the ALPC header), and the receiver validates it before
 * it dispatches a verb.  Returns the payload length of a message in that
 * framing shape, (unsigned int)-1 = the message is not in it. */
static unsigned int alpc_frame_payload_len( const void *data, data_size_t size )
{
    const unsigned char *p = data;
    unsigned short data_len, total_len;

    if (size < 80) return (unsigned int)-1;
    data_len  = *(const unsigned short *)p;
    total_len = *(const unsigned short *)(p + 2);
    if (total_len != (unsigned short)size || data_len + 40 != (unsigned short)size)
        return (unsigned int)-1;
    return *(const unsigned int *)(p + 0x48);
}

/* A server that receives every connection on the listening port has no
 * per-connection endpoint at write time (alpc_conn_base is the listening
 * port): a request slurped there is outstanding on the connection it was
 * routed to, so find that connection by the ids riding its outstanding
 * list (id 0 matches the connection with the oldest outstanding request).
 * Returns the client-side connection port, NULL = no match. */
static struct alpc_port *alpc_conn_find_outstanding( struct alpc_port *server_port, unsigned int id )
{
    struct alpc_port *conn;

    LIST_FOR_EACH_ENTRY( conn, &server_port->connections, struct alpc_port, conn_entry )
    {
        struct alpc_outstanding *o;

        if (!id)
        {
            if (!list_empty( &conn->outstanding )) return conn;
            continue;
        }
        LIST_FOR_EACH_ENTRY( o, &conn->outstanding, struct alpc_outstanding, entry )
            if (o->id == id) return conn;
    }
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
            }
            /* The kernel consumes the message on both statuses.  A message left
             * parked on an undersized receive keeps the port signaled, so every
             * later receive of that connection fails the same way and the handle
             * the message's attributes duplicated into the receiver's table is
             * pinned for the lifetime of the connection: one oversized message
             * poisons the port.  Once consumed the receiver owns the handle
             * duplicate. */
            list_remove( &msg->entry );
            release_alpc_message( msg );
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

    if (!port->is_server && !async_get_apc_context( async ) && list_empty( &port->msg_queue ) &&
        (!alpc_port_pair( port ) || list_empty( &alpc_port_pair( port )->msg_queue )))
    {
        update_alpc_readiness( port );
        set_error( STATUS_UNSUCCESSFUL );
        return;
    }

    queue_async( &port->read_q, async );
    reselect_alpc_port( port );
    set_error( STATUS_PENDING );
}

/* Build one queued message from a send and queue it on the peer.  The
 * handle-attribute duplication (attr dup) rides the first message of a
 * multi-buffer send only; seq/count tag the buffers of a multi-buffer send
 * with their position in the chain (the kernel stamps them as it splits). */
static int alpc_queue_send_message( struct alpc_port *port, struct alpc_port *peer,
                                    struct alpc_msg_attr_hdr *attr_hdr,
                                    obj_handle_t dup, struct object *grabbed_obj,
                                    const void *user_data, data_size_t user_size,
                                    unsigned int msg_id, unsigned int msg_type,
                                    int reply_stamped, unsigned int seq, unsigned int count,
                                    process_id_t sender_pid, thread_id_t sender_tid )
{
    struct alpc_message *msg;
    struct alpc_wire_hdr *hdr;
    data_size_t blob_size;
    client_ptr_t ctx;
    int stamped = 0;

    /* context the receiver sees = the per-connection context.
     * client request: our connection (peer = server side). server reply:
     * the destination client's connection context. */
    if (port->is_server)        ctx = peer->port_context;
    else if (port->peer)        ctx = port->peer->port_context;
    else                        ctx = port->port_context;

    blob_size = sizeof(*hdr) + (dup ? sizeof(struct alpc_handle_blk) : 0) + user_size;
    if (!(msg = malloc( sizeof(*msg) + blob_size )))
    {
        if (dup)
        {
            close_handle( peer->process, dup );
            release_object( grabbed_obj );
        }
        set_error( STATUS_NO_MEMORY );
        return 0;
    }
    memset( msg, 0, sizeof(*msg) );
    msg->is_connect = 0;
    if (dup)
    {
        msg->handle_proc = (struct process *)grab_object( peer->process );
        msg->handle_id   = dup;
        msg->handle_obj  = grabbed_obj;
    }
    msg->msg_id    = msg_id;
    msg->data_size = blob_size;

    hdr = (struct alpc_wire_hdr *)((char *)msg + sizeof(*msg));
    hdr->magic        = ALPC_WIRE_MAGIC;
    hdr->type         = 1; /* LPC_REQUEST/REPLY – distinguished by the user PORT_MESSAGE */
    hdr->msg_id       = msg_id;
    hdr->client_pid   = sender_pid;
    hdr->client_tid   = sender_tid;
    hdr->user_size    = user_size;
    hdr->port_context = ctx;
    hdr->attr_flags   = attr_hdr->attr_flags;
    hdr->view_flags   = attr_hdr->view_flags;
    hdr->view_base    = attr_hdr->view_base;
    hdr->view_size    = attr_hdr->view_size;
    hdr->msg_context  = attr_hdr->msg_context;
    hdr->sequence     = seq ? seq - 1 : attr_hdr->sequence; /* 0-based chain index */
    hdr->callback_id  = seq ? count : attr_hdr->callback_id;
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

    if ((reply_stamped || seq) && user_size >= ALPC_USER_MSGID_OFF + 4)
    {
        /* the kernel fills the MessageId with the id of the request a reply
         * answers, and stamps the per-buffer ids as it splits a multi-buffer
         * send: stamp the delivered copy so the receiver sees them too */
        *(unsigned int *)((char *)(hdr + 1) + ALPC_USER_MSGID_OFF) = msg_id;
        stamped = 1;
    }
    if (!stamped && !port->is_server && !msg_type && user_size >= ALPC_USER_MSGID_OFF + 4)
    {
        /* a client write echoing a message id a server receive is armed for
         * is a reply: the kernel stamps it LPC_REPLY */
        struct async *m = msg_id ? find_reply_alpc_async( &peer->read_q, msg_id ) : NULL;
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

    /* The kernel stamps the per-message header slots every delivered request
     * and reply carries; the messaging framing laid over them encodes a
     * payload-length identity (TotalLength == length + 80) and a chunk pair
     * (index < count) that both endpoints validate before any verb is
     * dispatched.  A relayed message never passes the kernel, so the
     * delivered copy carries the sender's zeroed slots: every exchange is
     * refused as a protocol exception, and a refusal reply with a zero
     * length field makes the receiver dispatch an uninitialized verb slot
     * (guarded call to address 0).  Stamp the chunk pair and the payload
     * length as the kernel would. */
    {
        unsigned char *msg_data = (unsigned char *)(hdr + 1) + (dup ? sizeof(struct alpc_handle_blk) : 0);

        if (alpc_frame_payload_len( msg_data, user_size ) != (unsigned int)-1)
        {
            *(unsigned short *)(msg_data + 0x40) = seq ? seq - 1 : 0; /* chunk index */
            *(unsigned short *)(msg_data + 0x42) = seq ? count : 1;   /* chunk count */
            *(unsigned int *)(msg_data + 0x48) = user_size - 80;      /* payload length */
        }
    }

#ifdef ALPC_DEBUG_TRACE
    {
        char hex[600];
        int k, len = min( user_size, 260 );
        for (k = 0; k < len; k++) sprintf( hex + k * 2, "%02x", ((const unsigned char *)user_data)[k] );
        fprintf( stderr, "ALPC_QUEUE_SEND: sender=%04x:%04x port=%p peer=%p type=%d size=%u is_reply=%d data=%s\n",
                 current ? current->process->id : 0, current ? current->id : 0,
                 port, peer, msg_type, user_size,
                 (msg_type == ALPC_LPC_REPLY || reply_stamped), hex );
    }
#endif
    peer->sequence_no++;
    list_add_tail( &peer->msg_queue, &msg->entry );
    wake_up( &peer->obj, 0 );
    reselect_alpc_port( peer );
    /* readiness of any handle of a connection is readiness of all of them: the
     * client keys its wake completion packets to the handle it arms the receive
     * on, which for the server side is the listening port and for the client
     * side may be the connection handle or its reserve handle.
     * Synchronous replies do not fire wait completion packets because they are
     * consumed directly by the calling thread in NtAlpcSendWaitReceivePort. */
    {
        unsigned short delivered_type = 0;
        int is_reply;
        if (user_size >= ALPC_USER_TYPE_OFF + 2)
            delivered_type = *(const unsigned short *)((char *)(hdr + 1) + ALPC_USER_TYPE_OFF);
        is_reply = (msg_type == ALPC_LPC_REPLY || delivered_type == ALPC_LPC_REPLY || reply_stamped);

        if (!is_reply && (!list_empty( &peer->msg_queue ) || (alpc_port_pair( peer ) && !list_empty( &alpc_port_pair( peer )->msg_queue ))))
            wake_connection_waits( peer );
    }
    /* the port handle becomes signaled while the message stays queued
     * (delivered messages cleared it in reselect_alpc_port); a message on a
     * reserve port is readiness of its connection handle too */
    update_alpc_readiness( peer );

    return 1;
}

/* Write op: sends a message to the peer connection's queue. */
static void alpc_fd_write( struct fd *fd, struct async *async, file_pos_t pos )
{
    struct alpc_port *port = get_fd_user( fd );
    struct alpc_port *peer, *base, *conn;
    struct thread *sender;
    process_id_t sender_pid;
    thread_id_t sender_tid;
    struct iosb *iosb;
    struct alpc_msg_attr_hdr attr;
    const void *data;
    data_size_t size;
    unsigned int reply_id = 0;
    unsigned int send_flags = 0;
    unsigned int utype = 0;
    unsigned int msg_type = 0;  /* LPC type to stamp into the delivered PORT_MESSAGE, 0 = keep */
    int reply_stamped = 0;      /* the kernel filled the reply's message id */
    int reply_resolved = 0;     /* the reply's correlation was settled above */
    int is_reply_send, is_multi;

    iosb = async_get_iosb( async );
    if (!iosb->in_data || !iosb->in_size)
    {
        release_object( iosb );
        async_request_complete( async, STATUS_SUCCESS, 0, 0, NULL ); /* zero-length send */
        return;
    }

    /* the delivered ClientId is that of the sending thread: the send may run
     * from the serialisation gate long after its caller queued it, so take
     * the identity from the async, not from the request thread */
    sender = async_get_thread( async );
    sender_pid = get_process_id( sender->process );
    sender_tid = get_thread_id( sender );

    /* Parse the wire header and the sender's PORT_MESSAGE header up front:
     * the send flags and the message type drive routing. */
    memset( &attr, 0, sizeof(attr) );
    data = iosb->in_data;
    size = iosb->in_size;
    if (size >= sizeof(attr) && *(const unsigned int *)data == ALPC_ATTR_HDR_MAGIC)
    {
        memcpy( &attr, data, sizeof(attr) );
        data = (const char *)data + sizeof(attr);
        size -= sizeof(attr);
    }
    send_flags = attr.pad2;
    if (size >= ALPC_USER_MSGID_OFF + 4)
        reply_id = *(const unsigned int *)((const char *)data + ALPC_USER_MSGID_OFF);
    if (size >= ALPC_USER_TYPE_OFF + 2)
        utype = *(const unsigned short *)((const char *)data + ALPC_USER_TYPE_OFF);
    is_reply_send = (utype == ALPC_LPC_REPLY);
    is_multi = (send_flags & ALPC_MSGFLG_LPC_MULTI_MESSAGE) && !port->is_server;
    /* a request's message id identifies the request (it joins the
     * connection's outstanding list); only a REPLY-flag send's id is a reply
     * echo for correlation */
    if (!is_reply_send) reply_id = 0;

    base = alpc_conn_base( port );

    /* Synchronous serialisation: the kernel does not start a synchronous
     * request on a connection that already has one outstanding; the send
     * stays pending on the connection's gate until the reply of the request
     * in flight drains it.  (Replies are never gated: the reply that pops an
     * exchange must flow for the gate to open.) */
    if (base && !port->is_server && !is_reply_send && !is_multi
        && (send_flags & ALPC_MSGFLG_SYNC_REQUEST) && !list_empty( &base->outstanding ))
    {
        alpc_gate_add( base, port, async );
        set_error( STATUS_PENDING );
        release_object( iosb );
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
        peer = NULL;
        if (is_reply_send)
        {
            /* The LPC connect dance: a REPLY-flag message on the listening
             * port answers a connection request the server slurped there.
             * The kernel creates the connection, hands the reply's payload
             * to the connecting client, and the server keeps no connection
             * handle: its traffic rides the listening port.  The connect
             * request carries a kernel-assigned message id, so only a reply
             * that echoes an id can complete a connect: a data reply with no
             * correlating id must never be taken for the connect's answer. */
            struct alpc_pending_conn *pc, *target = NULL;

            if (reply_id)
            {
                LIST_FOR_EACH_ENTRY( pc, &port->pending_conns, struct alpc_pending_conn, entry )
                {
                    if (pc->msg_id != reply_id) continue;
                    target = pc;
                    break;  /* the connect whose id this reply echoes */
                }
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
                if (complete_alpc_connection( port, target, attr.msg_context, 0 ))
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
            /* The connect exchange's own completion.  A REPLY-flag send on a
             * listening port is correlated first by the outstanding exchange of
             * *that listening port* — the connect request the server slurped
             * there — and only then by the data requests of the connections it
             * carries.  The server's commit of a slurped connect (the LPC
             * datagram commit, CoreMessaging's AlpcServerConnection::CommitToSlurp)
             * is a REPLY-flag send on the listening port that carries no payload
             * at all (the framing shape's payload word is zero: an 80-byte
             * frame), and it is a second commit the kernel accepts next to the
             * verb reply of the data request — the engine commits its connect and
             * its verb reply, and both must be taken.  The kernel routes the send
             * by the listening port and stamps the connect request's id, so the
             * message id on the wire is the sender's own composition (zero here):
             * match the listening port's outstanding connect slot by id when it
             * echoes one, else its oldest open exchange.  The commit completes
             * the connect, so it is consumed — never queued: a delivered
             * zero-length frame takes the receiver's unmarshal early-out (S_OK)
             * without writing the verb record, and the guarded dispatch then
             * calls the unwritten slot.  With no connect slot open the frame
             * answers nothing in its own domain and the kernel refuses it. */
            if (!alpc_frame_payload_len( data, size ))
            {
                unsigned int cid = alpc_pop_outstanding( port, reply_id );

                if (!cid) cid = alpc_pop_outstanding( port, 0 );
                if (cid)
                {
                    async_request_complete( async, STATUS_SUCCESS, iosb->in_size, 0, NULL );
                    release_object( iosb );
                    return;
                }
                release_object( iosb );
                set_error( STATUS_INVALID_DEVICE_REQUEST );
                async_request_complete( async, STATUS_INVALID_DEVICE_REQUEST, 0, 0, NULL );
                return;
            }

            /* a reply with no echoed id answers the connection's oldest
             * outstanding (slurped) request: find the armed receiver for it.
             * The pop *is* the correlation — the request in flight is answered
             * here and stamped with its id — so the correlation pass below must
             * not pop the same slot a second time and refuse the reply. */
            if (!reply_id)
            {
                LIST_FOR_EACH_ENTRY( conn, &port->connections, struct alpc_port, conn_entry )
                {
                    unsigned int oid = alpc_oldest_outstanding( conn );
                    struct async *matching;

                    if (!oid) continue;
                    matching = find_reply_alpc_async( &conn->read_q, oid );
                    if (matching)
                    {
                        release_object( matching );
                        reply_id = alpc_pop_outstanding( conn, 0 );
                        peer = conn;
                        msg_type = ALPC_LPC_REPLY;
                        reply_stamped = 1;
                        reply_resolved = 1;
                        alpc_drain_send_gate( conn );
                        break;
                    }
                }
            }
        }
        if (is_reply_send && !reply_resolved)
        {
            /* Kernel reply correlation.  A connection carries one outstanding
             * request at a time (a server that posts several buffers, or splits
             * a multi-buffer send, has one entry per request id).  A reply
             * echoes the id of the request it answers, is stamped with that id,
             * and goes to the receive armed with the id — the receive of the
             * sender's send-receive pair; a reply that finds no receiver armed
             * rides the connection's queue as the kernel's pending reply, for
             * the receive armed with the id next.  The cases:
             *
             *  - the id answers an outstanding request of a connection: pop it.
             *  - the id answers nothing (the request was already answered): the
             *    kernel refuses the reply with STATUS_INVALID_DEVICE_REQUEST and
             *    never queues nor delivers it.  A delivered duplicate reaches the
             *    receiver as a second answer to a request whose reply it already
             *    consumed: the messaging layer reads the stale frame, its
             *    unmarshal of a zero-length payload takes an S_OK early-out
             *    without writing the verb record, and the guarded dispatch reads
             *    the unwritten slot.
             *  - a reply-shaped frame carrying no id of its own answers the
             *    request in flight (the kernel serialises one exchange per
             *    connection), and is stamped with that request's id.
             *
             * The registrar exchange measured on the relay is of that shape, and
             * the engine commits it twice: the PrepareConnection request (id N,
             * 88 bytes) rides the connection and stays outstanding until the
             * verb reply answers it (128 bytes, framing payload length non-zero,
             * no id of its own — the payload the client unmarshals, so it takes
             * the request's reply slot), and the connect exchange is committed by
             * the zero-payload REPLY-flag send on the listening port (80 bytes,
             * framing payload length 0, and no message id of its own either: the
             * kernel stamps the connect request's id) — that commit is correlated
             * in the block above, on the listening port's outstanding connect
             * slot, before any connection's data request is touched, in either
             * order.  Two outstanding slots, one commit each: the connect slot on
             * the listening port and the data request on the connection.  A reply
             * that answers nothing in its own correlation domain is the refused
             * duplicate. */
            struct alpc_port *oc;
            unsigned int popped;

            oc = alpc_conn_find_outstanding( port, reply_id );
            popped = oc ? alpc_pop_outstanding( oc, reply_id ) : 0;
            if (popped)
            {
                struct async *armed;

                reply_id = popped;
                peer = oc;
                msg_type = ALPC_LPC_REPLY;
                reply_stamped = 1;
                armed = reply_id ? find_reply_alpc_async( &oc->read_q, reply_id ) : NULL;
                if (armed) release_object( armed ); /* the armed receive takes it */
                alpc_drain_send_gate( oc );
            }
            else
            {
                /* the kernel drops it: no request is outstanding to answer, so
                 * the reply is never queued and never delivered, and the send
                 * completes with the kernel's refusal */
                release_object( iosb );
                set_error( STATUS_INVALID_DEVICE_REQUEST );
                async_request_complete( async, STATUS_INVALID_DEVICE_REQUEST, 0, 0, NULL );
                return;
            }
            reply_resolved = 1;
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
        if (is_reply_send && port->peer)
            peer = port->peer; /* a reply belongs to the connection's client side */
        else if (port->peer && async_waiting( &port->peer->read_q ))
            peer = port->peer;
        else if (port->listen_port && async_waiting( &port->listen_port->read_q ))
            peer = port->listen_port;
        else if (port->listen_port)
            peer = port->listen_port; /* the server receives the traffic of every
                                       * connection on the listening port (LPC
                                       * connect-dance servers keep no connection
                                       * handle); park the message there when no
                                       * receive is armed yet */
        else
            peer = port->peer;

        if (!peer)
        {
            release_object( iosb );
            set_error( STATUS_PIPE_DISCONNECTED );
            return;
        }

        if (is_reply_send && base)
        {
            /* The same correlation on a client-side connection: a reply answers
             * the connection's outstanding request (the request the server
             * originated there, or the exchange the client itself opened), is
             * stamped with that request's id, and goes to the receive armed
             * with the id.  A reply for a request the connection does not have
             * outstanding is refused by the kernel with
             * STATUS_INVALID_DEVICE_REQUEST: it is never queued and never
             * delivered, so the receiver never sees a second answer to a
             * request it already consumed. */
            unsigned int popped = alpc_pop_outstanding( base, reply_id );
            unsigned int payload = alpc_frame_payload_len( data, size );
            struct async *armed;

            if (!popped)
            {
                /* the kernel drops it: never queued, never delivered, and the
                 * send completes with the kernel's refusal */
                release_object( iosb );
                set_error( STATUS_INVALID_DEVICE_REQUEST );
                async_request_complete( async, STATUS_INVALID_DEVICE_REQUEST, 0, 0, NULL );
                return;
            }
            reply_id = popped;
            msg_type = ALPC_LPC_REPLY;
            reply_stamped = 1;
            armed = reply_id ? find_reply_alpc_async( &peer->read_q, reply_id ) : NULL;
            if (armed) release_object( armed ); /* the armed receive takes it */
            else if (!payload)
            {
                /* the completion ack with no receive armed for the id: the
                 * correlation of the exchange, not a data reply */
                alpc_drain_send_gate( base );
                async_request_complete( async, STATUS_SUCCESS, iosb->in_size, 0, NULL );
                release_object( iosb );
                return;
            }
            alpc_drain_send_gate( base );
            reply_resolved = 1;
        }
    }

    {
        obj_handle_t dup = 0;
        struct object *grabbed_obj = NULL;

        /* Handle attribute: the kernel duplicates the sender's handle into the
         * receiving process's handle table as the message is delivered. Perform
         * the duplication now, at queue time (the receiver arms its receive
         * before the sender writes, and a receive that is already in flight
         * takes the message straight away), and bookkeep it on the message:
         * delivery transfers ownership to the receiver, teardown closes the
         * duplicate (the close-notification path for a handle whose receiver
         * vanished). */
        if (attr.attr_flags & ALPC_MESSAGE_HANDLE_ATTRIBUTE)
        {
            struct object *hobj = get_handle_obj( current->process,
                        (obj_handle_t)(ULONG_PTR)attr.send_handle,
                        attr.desired_access, NULL );
            if (!hobj)
            {
                release_object( iosb );
                return; /* get_handle_obj set the error */
            }
            if (!(dup = alloc_handle( peer->process, hobj, attr.desired_access, 0 )))
            {
                release_object( hobj );
                release_object( iosb );
                set_error( STATUS_NO_MEMORY );
                return;
            }
            grabbed_obj = grab_object( hobj ); /* keep a reference for the queue record */
            release_object( hobj );            /* the handle and the record own the rest */
        }

        if (is_multi && size >= ALPC_USER_HDR_SIZE)
        {
            /* Multi-buffer send: the send buffer carries a chain of
             * PORT_MESSAGEs, each sized by its own header + payload
             * (8-aligned); the head's TotalLength covers the whole chain.
             * The kernel splits the chain, stamps a distinct message id into
             * every buffer (the sender left them zero), tags each delivered
             * message with its position in the chain (index in Sequence,
             * chain length in CallbackId, delivered in the CONTEXT
             * attribute), and puts every request on the connection's
             * outstanding list so the server can answer them all. */
            const char *p = (const char *)data;
            data_size_t left = size;
            unsigned int chain = 0, idx = 0;

            /* count the chain first: the sequence context carries its length */
            while (left >= ALPC_USER_HDR_SIZE)
            {
                unsigned int tl = ALPC_USER_HDR_SIZE + ((*(const unsigned short *)p + 7) & ~7u);
                if (tl > left) break;
                chain++;
                p += tl;
                left -= tl;
            }
            p = (const char *)data;
            left = size;
            while (chain && left >= ALPC_USER_HDR_SIZE)
            {
                unsigned int tl = ALPC_USER_HDR_SIZE + ((*(const unsigned short *)p + 7) & ~7u);
                unsigned int mid = *(const unsigned int *)(p + ALPC_USER_MSGID_OFF);
                int first = (idx == 0);

                if (tl > left) break;
                if (!mid) mid = ++alpc_stamped_id;
                if (base) alpc_add_outstanding( base, mid );
                if (!alpc_queue_send_message( port, peer, &attr, first ? dup : 0,
                                              first ? grabbed_obj : NULL,
                                              p, tl, mid, 0, 0, idx + 1, chain, sender_pid, sender_tid ))
                {
                    release_object( iosb );
                    return;
                }
                p += tl;
                left -= tl;
                idx++;
            }
            async_request_complete( async, STATUS_SUCCESS, iosb->in_size, 0, NULL );
            release_object( iosb );
            return;
        }

        if (!reply_resolved && !reply_id && !msg_type)
        {
            unsigned int mid = 0;

            if (size >= ALPC_USER_MSGID_OFF + 4)
                mid = *(const unsigned int *)((const char *)data + ALPC_USER_MSGID_OFF);

            if (mid)
            {
                /* the request joins the outstanding list of the connection it
                 * is delivered on (the kernel serialises synchronous
                 * exchanges): on the client side the connection base carries
                 * the list, on the listening-port route the request rides the
                 * routed peer connection, and a request the server originates
                 * in the reverse direction rides the client's connection.
                 *
                 * A non-reply send that carries a message id of its own *is*
                 * the request leg of an exchange: the kernel owns the Type slot
                 * and stamps it on delivery, so a sender that composes its
                 * message id itself leaves Type zero (the messaging layer's
                 * PrepareConnection request is of that shape — a synchronous
                 * send, id 2, Type 0) and the registration must not be keyed on
                 * the sender's Type.  A datagram carries no id, and a reply
                 * never reaches this point, so the id alone identifies it. */
                if (base) alpc_add_outstanding( base, mid );
                else if (port->is_server && peer) alpc_add_outstanding( peer, mid );
                reply_id = mid;
            }
        }

        if (!alpc_queue_send_message( port, peer, &attr, dup, grabbed_obj,
                                      data, size, reply_id, msg_type, reply_stamped, 0, 0, sender_pid, sender_tid ))
        {
            release_object( iosb );
            return;
        }
    }

    async_request_complete( async, STATUS_SUCCESS, iosb->in_size, 0, NULL );
    release_object( iosb );
}

/* ---- Helpers ---- */

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
    port->sequence_no = 0;
    port->flags       = 0;
    port->client_pid  = get_process_id( current->process );
    port->client_tid  = get_thread_id( current );
    list_init( &port->msg_queue );
    list_init( &port->outstanding );
    list_init( &port->send_gate );
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
    set_fd_signaled( port->fd, 0 );
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
    server_port->sequence_no++;
    list_add_tail( &server_port->msg_queue, &msg->entry );
    wake_up( &server_port->obj, 0 );
    reselect_alpc_port( server_port );
    /* notify any wait completion packet associated with this port (the standard
     * NtAssociateWaitCompletionPacket-based server wait used by CoreMessaging). */
    if (!list_empty( &server_port->msg_queue ))
    {
        signal_wait_completion_packets( &server_port->obj );
        fire_wait_completion_packets( &server_port->obj );
        set_fd_signaled( server_port->fd, 1 );
    }
    return 1;
}

/* ---- Request handlers ---- */

/* dump the (null-terminated) object name carried by a port request.
 * The server stores names as UTF-16; the C library's %ls expects
 * wchar_t, so transcode the (ASCII) object names by hand. */
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
        port->sequence_no = 0;
        port->flags       = req->flags;
        port->client_pid  = get_process_id( current->process );
        port->client_tid  = get_thread_id( current );
        list_init( &port->msg_queue );
        list_init( &port->outstanding );
        list_init( &port->send_gate );
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
        set_fd_signaled( port->fd, 0 );
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

    fprintf( stderr, "CONNECT_ALPC_PORT: pid=%04x tid=%04x handle=%x client=%p server=%p\n",
             current->process->id, current->id, reply->handle, client_port, server_port );
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
 * NULL on allocation failure.
 *
 * `open_commit` says which doorway completed the connect.  A server that
 * accepted the connection explicitly (NtAlpcAcceptConnectPort) has created the
 * connection object, but the connect request it slurped on the listening port
 * is *still outstanding there*: the connect exchange owns a completion of its
 * own — the server's LPC datagram commit, a zero-payload REPLY-flag send on the
 * listening port (CoreMessaging's AlpcServerConnection::CommitToSlurp) — and
 * the kernel keeps the slurped request on the listening port's outstanding list
 * until that send arrives.  So the accept route registers the connect id on the
 * listening port (a slot of its own, separate from the data requests of the
 * connection, which ride the connection's outstanding list).  When the
 * completion send itself is the connect dance's reply on the listening port,
 * the commit has already arrived and no slot is opened. */
static struct alpc_port *complete_alpc_connection( struct alpc_port *server_port,
                                                   struct alpc_pending_conn *found,
                                                   client_ptr_t port_context, int open_commit )
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

    /* the slurped connect request stays outstanding on the listening port
     * until the server commits the connect exchange with its zero-payload
     * REPLY-flag send there (see the write path's connect-commit rule) */
    if (open_commit) alpc_add_outstanding( server_port, found->msg_id );

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
     * blocked client with the connection reply message.  The connect exchange
     * keeps its own outstanding slot on the listening port: the server commits
     * it with a zero-payload REPLY-flag send there. */
    if ((server_conn_port = complete_alpc_connection( server_port, found, req->port_context, 1 )))
    {
        reply->handle = alloc_handle( current->process, server_conn_port,
                                      GENERIC_READ | GENERIC_WRITE | SYNCHRONIZE, 0 );
        fprintf( stderr, "ACCEPT_ALPC_CONN: pid=%04x tid=%04x handle=%x server_conn=%p peer=%p context=%lx\n",
                 current->process->id, current->id, reply->handle, server_conn_port, server_conn_port->peer, (unsigned long)req->port_context );
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

    update_alpc_readiness( conn );
    update_alpc_readiness( rev );

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

DECL_HANDLER(alpc_query_port)
{
    struct alpc_port *port = (struct alpc_port *)get_handle_obj( current->process, req->handle,
                                                                 0, &alpc_port_ops );
    if (!port) return;

    reply->flags       = port->flags;
    reply->sequence_no = port->sequence_no;
    reply->port_context = port->port_context;
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
        alpc_gate_clear( port->peer, STATUS_PIPE_DISCONNECTED );
        alpc_gate_clear( port, STATUS_PIPE_DISCONNECTED );
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
