/*
 * Advanced Local Procedure Call
 *
 * Copyright 2026 Zhiyi Zhang for CodeWeavers
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

#if 0
#pragma makedep unix
#endif

#include <stdarg.h>
#include <string.h>

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "winternl.h"
#include "wine/debug.h"
#include "unix_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(alpc);

/* ALPC message types.  The user-visible PORT_MESSAGE u2.s2.Type carries the
 * classic LPC message-type values (LPCRequest 1, LPCReply 2, LPCDatagram 3,
 * LPCConnectionRequest 10, LPCNewMessage 0xb): ALPC ports that serve the LPC
 * protocol (every LPC server, the CoreMessaging registrar and conversation
 * engine, and the rpcrt4 ncalpc helpers) dispatch on these bytes and treat
 * unknown values as fatal contract violations.  The wine-internal wire header
 * carried between ntdll and wineserver is a separate field and keeps its own
 * stamps (server/alpc.c uses 1 for request/reply and 10 for connect
 * notifications, translated here). */
#define LPC_REQUEST            1
#define LPC_DATAGRAM           3
#define LPC_REPLY              2
#define LPC_NEW_CONNECTION     10 /* user-visible type of a connection request */
#define LPC_NEW_MESSAGE        0xb
#define LPC_CONNECTION_REQUEST 10 /* internal wire hdr, matches the value server/alpc.c stamps */

/* The kernel fills the RESERVE attribute of the connect reply with a reverse
 * port handle: server-originated messages of the connection are delivered on
 * it.  The app reads { port, context } at +8/+16 of the attribute. */
#define ALPC_MESSAGE_RESERVE_ATTRIBUTE 0x80000000
struct alpc_reserve_attr
{
    ULONGLONG reserved;
    HANDLE    port;
    PVOID     context;
};

/* NtAlpcSendWaitReceivePort send flags come from the documented
 * ALPC_MSGFLG_* set (include/winternl.h).  Note that 0x00040000 is
 * ALPC_MSGFLG_TERMINATION (documented ALPC_MSGFLG_TERMINATE), not a datagram
 * flag: a datagram send is marked by PORT_MESSAGE.Type == LPC_DATAGRAM, the
 * flag bits carry no datagram value. */

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
    client_ptr_t     msg_context;
    unsigned int     sequence;
    unsigned int     callback_id;
    /* data-view section: a send that carries a VIEW attribute with a section
     * handle has that section duplicated into the receiving process at
     * delivery (the kernel duplicates it into the receiver's handle table);
     * this is the duplicated handle id in the receiver, 0 = no view section */
    client_ptr_t     view_section;
};

/* HANDLE attribute as the kernel presents it on delivery. It rides as an
 * optional block between the wire header and the user payload, present only
 * when attr_flags carries ALPC_MESSAGE_HANDLE_ATTRIBUTE (connect handshake
 * replies, whose caller buffers are sized for the plain header only, are not
 * enlarged by it): the handle duplicated into the receiving process (a server
 * handle id, translated by ntdll), the notification action (1 = created,
 * 2 = closed, 3 = modified, 4 = closed in a closing process) and the handle
 * count. */
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
    /* sender-side VIEW attribute: the section handle to duplicate into the
     * receiver on delivery (the kernel duplicates the section backing the
     * view; the receiver maps it through NtAlpcCreateSectionView) */
    client_ptr_t     send_section;
};

static void *get_message_attribute( ALPC_MESSAGE_ATTRIBUTES *attributes, ULONG attribute_flag )
{
    static const struct
    {
        ULONG flag;
        SIZE_T size;
    } attribute_sizes[] =
    {
        { ALPC_MESSAGE_SECURITY_ATTRIBUTE,       sizeof(ALPC_SECURITY_ATTR) },
        { ALPC_MESSAGE_VIEW_ATTRIBUTE,           sizeof(ALPC_DATA_VIEW_ATTR) },
        { ALPC_MESSAGE_CONTEXT_ATTRIBUTE,        sizeof(ALPC_CONTEXT_ATTR) },
        { ALPC_MESSAGE_HANDLE_ATTRIBUTE,         sizeof(ALPC_HANDLE_ATTR) },
        { ALPC_MESSAGE_TOKEN_ATTRIBUTE,          sizeof(ALPC_TOKEN_ATTR) },
        { ALPC_MESSAGE_DIRECT_ATTRIBUTE,         sizeof(ALPC_DIRECT_ATTR) },
        { ALPC_MESSAGE_WORK_ON_BEHALF_ATTRIBUTE, sizeof(ALPC_WORK_ON_BEHALF_ATTR) },
        { ALPC_MESSAGE_EVENT_ATTRIBUTE,          sizeof(ALPC_EVENT) },
        { ALPC_MESSAGE_RESERVE_ATTRIBUTE,        sizeof(struct alpc_reserve_attr) },
        /* ALPC_MESSAGE_MULTI_MESSAGE_SEQ_ATTRIBUTE (0x2000) allocates no
         * payload: Windows measures AlpcGetHeaderSize(0x2000) == header only.
         * The kernel delivers the multi-message sequence in the CONTEXT
         * attribute (Sequence = index in the chain, CallbackId = chain
         * length), which the receive path echoes from the wire header. */
    };
    unsigned int i;
    SIZE_T off = sizeof(ALPC_MESSAGE_ATTRIBUTES);

    if (!attributes || !attribute_flag || (attribute_flag & (attribute_flag - 1)))
        return NULL;
    if ((attributes->AllocatedAttributes & attribute_flag) != attribute_flag)
        return NULL;

    for (i = 0; i < ARRAY_SIZE(attribute_sizes); i++)
    {
        if (attribute_sizes[i].flag == attribute_flag)
            return (char *)attributes + off;
        if (attributes->AllocatedAttributes & attribute_sizes[i].flag)
            off += attribute_sizes[i].size;
    }
    return NULL;
}

/* Process-global counter for the MessageId of data requests the sender
 * composes itself (the ALPC_SEND_REQUEST path).  A connection has a single
 * client, so a process-local counter is unique in the connection's
 * correlation domain.  Connect-request ids are *not* taken from here: the
 * kernel assigns them on the target listening port, and the server allocates
 * them per port (two processes connecting the same named port must not
 * collide). */
static LONG alpc_msg_id_counter;

static unsigned int next_alpc_msg_id(void)
{
    return InterlockedIncrement( &alpc_msg_id_counter );
}

/* Async I/O bookkeeping for ALPC server requests, mirroring file.c's
 * async_irp / irp_completion pattern so out_data is delivered correctly. */
struct alpc_async
{
    struct async_fileio io;
    void               *buffer;   /* destination buffer for received data */
    ULONG               size;     /* size of destination buffer */
};

static BOOL alpc_async_completion( void *user, ULONG_PTR *info, unsigned int *status )
{
    struct alpc_async *async = user;

    if (*status == STATUS_ALERTED)
    {
        SERVER_START_REQ( get_async_result )
        {
            req->user_arg = wine_server_client_ptr( async );
            if (async->buffer) wine_server_set_reply( req, async->buffer, async->size );
            *status = virtual_locked_server_call( req );
        }
        SERVER_END_REQ;
    }
    release_fileio( &async->io );
    return TRUE;
}

/******************************************************************************
 *           NtAlpcCreatePort  (NTDLL.@)
 */
NTSTATUS WINAPI NtAlpcCreatePort( HANDLE *port_handle, OBJECT_ATTRIBUTES *obj_attr,
                                   ALPC_PORT_ATTRIBUTES *port_attr )
{
    unsigned int ret;
    data_size_t len;
    struct object_attributes *objattr;

    TRACE( "(%p, %p, %p)\n", port_handle, obj_attr, port_attr );

    if (!port_handle) return STATUS_ACCESS_VIOLATION;
    *port_handle = NULL;

    if ((ret = alloc_object_attributes( obj_attr, &objattr, &len ))) return ret;

    SERVER_START_REQ( create_alpc_port )
    {
        req->access      = GENERIC_ALL;
        req->flags       = port_attr ? port_attr->Flags : 0;
        req->max_msg_len = port_attr ? (unsigned int)port_attr->MaxMessageLength : 0x1000;
        wine_server_add_data( req, objattr, len );
        ret = wine_server_call( req );
        if (!ret) *port_handle = wine_server_ptr_handle( reply->handle );
    }
    SERVER_END_REQ;

    free( objattr );
    return ret;
}


/******************************************************************************
 *           NtAlpcCreatePortSection  (NTDLL.@)
 */
NTSTATUS WINAPI NtAlpcCreatePortSection( HANDLE port_handle, ULONG flags,
                                         HANDLE section_handle, SIZE_T section_size,
                                         HANDLE *alpc_section_handle, SIZE_T *actual_section_size )
{
    NTSTATUS ret;
    HANDLE sec = NULL;

    TRACE( "(%p, %#x, %p, %Ix, %p, %p)\n",
           port_handle, (unsigned int)flags, section_handle, section_size,
           alpc_section_handle, actual_section_size );

    if (!port_handle || !alpc_section_handle)
        return STATUS_INVALID_PARAMETER;

    if (section_handle)
    {
        ret = NtDuplicateObject( NtCurrentProcess(), section_handle,
                                 NtCurrentProcess(), &sec,
                                 0, 0, DUPLICATE_SAME_ACCESS );
        if (ret) return ret;
    }
    else
    {
        LARGE_INTEGER size;
        OBJECT_ATTRIBUTES attr;

        if (!section_size) return STATUS_INVALID_PARAMETER;
        size.QuadPart = section_size;
        InitializeObjectAttributes( &attr, NULL, 0, NULL, NULL );
        ret = NtCreateSection( &sec, SECTION_ALL_ACCESS, &attr, &size,
                               PAGE_READWRITE, SEC_COMMIT, 0 );
        if (ret) return ret;
    }

    if (actual_section_size)
    {
        SECTION_BASIC_INFORMATION info;
        if (!NtQuerySection( sec, SectionBasicInformation, &info, sizeof(info), NULL ))
            *actual_section_size = (SIZE_T)info.Size.QuadPart;
        else
            *actual_section_size = section_size;
    }

    *alpc_section_handle = sec;
    return STATUS_SUCCESS;
}


/******************************************************************************
 *           NtAlpcDeletePortSection  (NTDLL.@)
 */
NTSTATUS WINAPI NtAlpcDeletePortSection( HANDLE port_handle, ULONG flags, HANDLE section_handle )
{
    TRACE( "(%p, %#x, %p)\n", port_handle, (unsigned int)flags, section_handle );

    if (!port_handle || !section_handle)
        return STATUS_INVALID_PARAMETER;

    return NtClose( section_handle );
}


/******************************************************************************
 *           NtAlpcCreateSectionView  (NTDLL.@)
 */
NTSTATUS WINAPI NtAlpcCreateSectionView( HANDLE port_handle, ULONG flags,
                                         ALPC_DATA_VIEW_ATTR *view_attr )
{
    NTSTATUS ret;
    void *base = NULL;
    SIZE_T size;

    TRACE( "(%p, %#x, %p)\n", port_handle, (unsigned int)flags, view_attr );

    if (!port_handle || !view_attr || !view_attr->SectionHandle)
        return STATUS_INVALID_PARAMETER;

    base = view_attr->ViewBase;
    size = view_attr->ViewSize;
    ret = NtMapViewOfSection( view_attr->SectionHandle, NtCurrentProcess(),
                              &base, 0, 0, NULL, &size, ViewUnmap, 0, PAGE_READWRITE );
    if (ret) return ret;

    view_attr->ViewBase = base;
    view_attr->ViewSize = size;
    return STATUS_SUCCESS;
}


/******************************************************************************
 *           NtAlpcDeleteSectionView  (NTDLL.@)
 */
NTSTATUS WINAPI NtAlpcDeleteSectionView( HANDLE port_handle, ULONG flags, PVOID view_base )
{
    TRACE( "(%p, %#x, %p)\n", port_handle, (unsigned int)flags, view_base );

    if (!port_handle || !view_base)
        return STATUS_INVALID_PARAMETER;

    return NtUnmapViewOfSection( NtCurrentProcess(), view_base );
}


/******************************************************************************
 *           NtAlpcConnectPort  (NTDLL.@)
 */
NTSTATUS WINAPI NtAlpcConnectPort( HANDLE *port_handle, UNICODE_STRING *port_name,
                                    OBJECT_ATTRIBUTES *obj_attr, ALPC_PORT_ATTRIBUTES *port_attr,
                                    DWORD flags, PSID required_server_sid,
                                    ALPC_PORT_MESSAGE *connect_msg, SIZE_T *connect_msg_size,
                                    ALPC_MESSAGE_ATTRIBUTES *out_msg_attr,
                                    ALPC_MESSAGE_ATTRIBUTES *in_msg_attr, LARGE_INTEGER *timeout )
{
    unsigned int ret;
    HANDLE wait_handle = NULL;
    OBJECT_ATTRIBUTES port_obj_attr;
    data_size_t len;
    struct object_attributes *objattr;
    struct alpc_async *async;
    IO_STATUS_BLOCK io = { { STATUS_PENDING } };

    TRACE( "(%p, %s, %p, %p, %#x, %p, %p, %p, %p, %p, %p)\n",
           port_handle, debugstr_us(port_name), obj_attr, port_attr, (unsigned int)flags,
           required_server_sid, connect_msg, connect_msg_size,
           out_msg_attr, in_msg_attr, timeout );

    if (!port_handle) return STATUS_ACCESS_VIOLATION;
    *port_handle = NULL;

    /* Build object_attributes referencing the port name for the server lookup. */
    InitializeObjectAttributes( &port_obj_attr, port_name, OBJ_CASE_INSENSITIVE, NULL, NULL );
    if (obj_attr && obj_attr->RootDirectory)
        port_obj_attr.RootDirectory = obj_attr->RootDirectory;

    if ((ret = alloc_object_attributes( &port_obj_attr, &objattr, &len ))) return ret;

    if (!(async = (struct alpc_async *)alloc_fileio( sizeof(*async), alpc_async_completion, NULL )))
    {
        free( objattr );
        return STATUS_NO_MEMORY;
    }
    /* the server's connection reply is delivered as the async out data when the
     * handshake completes on the server side, so register the caller's buffer */
    async->buffer = connect_msg;
    async->size   = connect_msg ? (connect_msg_size ? *connect_msg_size : connect_msg->TotalLength) : 0;

    SERVER_START_REQ( connect_alpc_port )
    {
        req->async   = server_async( NULL, &async->io, NULL, NULL, NULL, iosb_client_ptr( &io ) );
        /* the kernel assigns the connect-request id on the listening port;
         * the server allocates it from the port's counter and echoes it on
         * the connect notification and the connect reply.  req->msg_id is
         * kept on the wire (append-only protocol) but carries no meaning. */
        req->msg_id  = 0;
        wine_server_add_data( req, objattr, len );
        /* forward the caller's connection-message payload so the server can
         * validate it before accepting */
        if (connect_msg && connect_msg->DataLength)
            wine_server_add_data( req, connect_msg + 1, connect_msg->DataLength );
        /* the server's connection reply lands in the caller's connection message */
        if (connect_msg)
            wine_server_set_reply( req, connect_msg,
                                   connect_msg_size ? *connect_msg_size : connect_msg->TotalLength );
        ret = wine_server_call( req );
        if (!ret || ret == STATUS_PENDING)
        {
            *port_handle = wine_server_ptr_handle( reply->handle );
            wait_handle  = wine_server_ptr_handle( reply->wait );
        }
    }
    SERVER_END_REQ;

    free( objattr );
    if (ret != STATUS_PENDING) free( async );

    if (wait_handle)
    {
        unsigned int wait_ret = server_wait_for_object( wait_handle, FALSE, timeout );
        if (!wait_ret) ret = io.Status;
        else
        {
            NtClose( wait_handle );
            ret = wait_ret;
        }
    }

    /* The kernel completes a connect with the server's reply message plus the
     * attributes of the new connection: the CONTEXT attribute carrying the
     * connection port context the server gave to NtAlpcAcceptConnectPort and
     * the connect message id, and the RESERVE attribute carrying the reverse
     * port and its context.  The server prefixes a wine-internal wire header
     * with that information; strip it from the caller's buffer and fill in the
     * attributes as the kernel would. */
    struct alpc_wire_hdr chdr = {0};
    if (!ret && connect_msg && io.Information >= sizeof(struct alpc_wire_hdr))
    {
        memcpy( &chdr, connect_msg, sizeof(chdr) );
        if (chdr.magic == ALPC_WIRE_MAGIC)
        {
            if (chdr.user_size) memmove( connect_msg, (char *)connect_msg + sizeof(chdr), chdr.user_size );
            io.Information = chdr.user_size;

            if (out_msg_attr && (out_msg_attr->AllocatedAttributes & ALPC_MESSAGE_CONTEXT_ATTRIBUTE))
            {
                ALPC_CONTEXT_ATTR *ctx = get_message_attribute( out_msg_attr, ALPC_MESSAGE_CONTEXT_ATTRIBUTE );
                if (ctx)
                {
                    ctx->PortContext    = (void *)(ULONG_PTR)chdr.port_context;
                    ctx->MessageContext = NULL;
                    ctx->Sequence       = chdr.sequence;
                    ctx->MessageId      = chdr.msg_id;
                    ctx->CallbackId     = chdr.callback_id;
                    out_msg_attr->ValidAttributes |= ALPC_MESSAGE_CONTEXT_ATTRIBUTE;
                }
            }
        }
    }

    if (!ret && connect_msg_size) *connect_msg_size = io.Information;

    if (ret && ret != STATUS_PENDING && *port_handle)
    {
        NtClose( *port_handle );
        *port_handle = NULL;
    }

    /* The kernel hands the client a reserve (reverse) port in the RESERVE
     * attribute of the connect reply: messages the server originates in the
     * reverse direction are delivered on it, and replies to them are written
     * from it.  Create it and wire it onto the connection. */
    if (!ret && *port_handle && out_msg_attr &&
        (out_msg_attr->AllocatedAttributes & ALPC_MESSAGE_RESERVE_ATTRIBUTE))
    {
        struct alpc_reserve_attr *res = get_message_attribute( out_msg_attr, ALPC_MESSAGE_RESERVE_ATTRIBUTE );
        if (res)
        {
            HANDLE rev = NULL;
            if (!NtAlpcCreatePort( &rev, NULL, NULL ))
            {
                unsigned int wire_ret;
                SERVER_START_REQ( wire_alpc_reverse_port )
                {
                    req->conn    = wine_server_obj_handle( *port_handle );
                    req->reverse = wine_server_obj_handle( rev );
                    wire_ret = wine_server_call( req );
                }
                SERVER_END_REQ;
                if (!wire_ret)
                {
                    res->port = rev;
                    res->context = (PVOID)(ULONG_PTR)chdr.port_context;
                    out_msg_attr->ValidAttributes |= ALPC_MESSAGE_RESERVE_ATTRIBUTE;
                }
                else NtClose( rev );
            }
        }
    }

    return ret;
}


/******************************************************************************
 *           NtAlpcAcceptConnectPort  (NTDLL.@)
 */
NTSTATUS WINAPI NtAlpcAcceptConnectPort( HANDLE *communication_port, HANDLE connection_port,
                                          DWORD flags, OBJECT_ATTRIBUTES *obj_attr,
                                          ALPC_PORT_ATTRIBUTES *port_attr, void *port_context,
                                          ALPC_PORT_MESSAGE *connection_request,
                                          ALPC_MESSAGE_ATTRIBUTES *connection_msg_attr,
                                          BOOLEAN accept_connection )
{
    unsigned int ret;
    unsigned int msg_id;

    TRACE( "(%p, %p, %#x, %p, %p, %p, %p, %p, %d)\n",
           communication_port, connection_port, (unsigned int)flags, obj_attr, port_attr,
           port_context, connection_request, connection_msg_attr, accept_connection );

    if (!communication_port) return STATUS_ACCESS_VIOLATION;
    *communication_port = NULL;
    if (!connection_request) return STATUS_INVALID_PARAMETER;

    msg_id = connection_request->MessageId;

    SERVER_START_REQ( accept_alpc_connection )
    {
        req->server_port  = wine_server_obj_handle( connection_port );
        req->msg_id       = msg_id;
        req->accept       = accept_connection;
        req->port_context = (client_ptr_t)(ULONG_PTR)port_context;
        /* The server writes its connection reply into the buffer that carried the
         * connect request; hand it to the server so it can complete the handshake
         * with it.  The buffer is the caller's, so read it as the sender sized it. */
        if (accept_connection && connection_request->TotalLength)
            wine_server_add_data( req, connection_request, connection_request->TotalLength );
        ret = wine_server_call( req );
        if (!ret && accept_connection)
            *communication_port = wine_server_ptr_handle( reply->handle );
    }
    SERVER_END_REQ;

    return ret;
}


/******************************************************************************
 *           NtAlpcDisconnectPort  (NTDLL.@)
 */
NTSTATUS WINAPI NtAlpcDisconnectPort( HANDLE port_handle, ULONG flags )
{
    unsigned int ret;

    TRACE( "(%p, %#x)\n", port_handle, (unsigned int)flags );

    SERVER_START_REQ( disconnect_alpc_port )
    {
        req->handle = wine_server_obj_handle( port_handle );
        ret = wine_server_call( req );
    }
    SERVER_END_REQ;

    return ret;
}


/******************************************************************************
 *           NtAlpcCancelMessage  (NTDLL.@)
 */
NTSTATUS WINAPI NtAlpcCancelMessage( HANDLE port_handle, ULONG flags, ALPC_CONTEXT_ATTR *msg_context )
{
    unsigned int ret;

    TRACE( "(%p, %#x, %p)\n", port_handle, (unsigned int)flags, msg_context );

    if (!msg_context) return STATUS_INVALID_PARAMETER;

    SERVER_START_REQ( alpc_cancel_message )
    {
        req->handle = wine_server_obj_handle( port_handle );
        req->msg_id = msg_context->MessageId;
        ret = wine_server_call( req );
        if (!ret && !reply->cancelled) ret = STATUS_NOT_FOUND;
    }
    SERVER_END_REQ;

    return ret;
}


/******************************************************************************
 *           NtAlpcQueryInformationMessage  (NTDLL.@)
 *
 * The information the kernel stamps into a delivered message is carried in
 * the PORT_MESSAGE itself and on the port: the message context (port context,
 * message context, sequence, the kernel-assigned message id and callback id)
 * is answerable from that state.  The SID / token-modified-id / handle
 * classes describe kernel bookkeeping of the sending security context that
 * this relay does not carry.
 */
NTSTATUS WINAPI NtAlpcQueryInformationMessage( HANDLE port_handle, ALPC_PORT_MESSAGE *msg,
                                               ULONG class, void *buffer, ULONG length, ULONG *ret_length )
{
    TRACE( "(%p, %p, %u, %p, %u, %p)\n", port_handle, msg, (int)class, buffer, (int)length, ret_length );

    if (!msg) return STATUS_INVALID_PARAMETER;

    switch (class)
    {
    case 0: /* message context information (ALPC_CONTEXT_ATTR) */
    {
        ALPC_CONTEXT_ATTR ctx;
        unsigned int ret;

        if (length < sizeof(ctx)) return STATUS_INFO_LENGTH_MISMATCH;
        memset( &ctx, 0, sizeof(ctx) );
        ctx.MessageId  = msg->MessageId;

        SERVER_START_REQ( alpc_query_port )
        {
            req->handle = wine_server_obj_handle( port_handle );
            ret = wine_server_call( req );
            if (!ret) ctx.PortContext = (void *)(ULONG_PTR)reply->port_context;
        }
        SERVER_END_REQ;
        if (ret) return ret;

        memcpy( buffer, &ctx, sizeof(ctx) );
        if (ret_length) *ret_length = sizeof(ctx);
        return STATUS_SUCCESS;
    }
    case 3: /* handle information: { HANDLE handle, ULONG object_type_info } */
        FIXME( "AlpcMessageHandleInformation unimplemented: the relay duplicates the sender's\n" );
        FIXME( "handle at delivery only and keeps no per-message handle record to answer from.\n" );
        return STATUS_NOT_IMPLEMENTED;
    default:
        FIXME( "unsupported message info class %u\n", (int)class );
        return STATUS_INVALID_INFO_CLASS;
    }
}


/******************************************************************************
 *           NtAlpcOpenSenderProcess  (NTDLL.@)
 *
 * The delivered PORT_MESSAGE carries the sending thread's client id (the
 * server stamps it), so the sender is opened by that id.
 */
NTSTATUS WINAPI NtAlpcOpenSenderProcess( HANDLE *process_handle, HANDLE port_handle,
                                         ALPC_PORT_MESSAGE *msg )
{
    OBJECT_ATTRIBUTES attr;
    CLIENT_ID cid;

    TRACE( "(%p, %p, %p)\n", process_handle, port_handle, msg );

    if (!process_handle) return STATUS_ACCESS_VIOLATION;
    if (!msg) return STATUS_INVALID_PARAMETER;
    *process_handle = NULL;

    /* FIXME: the kernel opens the sender in the client's session and checks
     * the connection's impersonation level; the relay authenticates the
     * client id from the stamped PORT_MESSAGE only. */
    cid = msg->ClientId;
    InitializeObjectAttributes( &attr, NULL, 0, NULL, NULL );
    return NtOpenProcess( process_handle, PROCESS_ALL_ACCESS, &attr, &cid );
}


/******************************************************************************
 *           NtAlpcOpenSenderThread  (NTDLL.@)
 */
NTSTATUS WINAPI NtAlpcOpenSenderThread( HANDLE *thread_handle, HANDLE port_handle,
                                        ALPC_PORT_MESSAGE *msg )
{
    OBJECT_ATTRIBUTES attr;
    CLIENT_ID cid;

    TRACE( "(%p, %p, %p)\n", thread_handle, port_handle, msg );

    if (!thread_handle) return STATUS_ACCESS_VIOLATION;
    if (!msg) return STATUS_INVALID_PARAMETER;
    *thread_handle = NULL;

    cid = msg->ClientId;
    InitializeObjectAttributes( &attr, NULL, 0, NULL, NULL );
    return NtOpenThread( thread_handle, THREAD_ALL_ACCESS, &attr, &cid );
}


/******************************************************************************
 *           NtAlpcConnectPortEx  (NTDLL.@)
 *
 * The OBJECT_ATTRIBUTES-based variant of NtAlpcConnectPort: the target port
 * is named by the connection port object attributes, the rest of the
 * handshake is the same connect path.
 */
NTSTATUS WINAPI NtAlpcConnectPortEx( HANDLE *port_handle, OBJECT_ATTRIBUTES *conn_port_attr,
                                     OBJECT_ATTRIBUTES *obj_attr, ALPC_PORT_ATTRIBUTES *port_attr,
                                     DWORD flags, PSECURITY_DESCRIPTOR server_sd,
                                     ALPC_PORT_MESSAGE *connect_msg, SIZE_T *connect_msg_size,
                                     ALPC_MESSAGE_ATTRIBUTES *out_msg_attr,
                                     ALPC_MESSAGE_ATTRIBUTES *in_msg_attr, LARGE_INTEGER *timeout )
{
    TRACE( "(%p, %p, %p, %p, %#x, %p, %p, %p, %p, %p, %p)\n",
           port_handle, conn_port_attr, obj_attr, port_attr, (unsigned int)flags,
           server_sd, connect_msg, connect_msg_size, out_msg_attr, in_msg_attr, timeout );

    if (!conn_port_attr || !conn_port_attr->ObjectName) return STATUS_INVALID_PARAMETER;
    if (server_sd) FIXME( "server security descriptor requirements are not enforced\n" );

    return NtAlpcConnectPort( port_handle, conn_port_attr->ObjectName, obj_attr, port_attr,
                              flags, NULL, connect_msg, connect_msg_size,
                              out_msg_attr, in_msg_attr, timeout );
}


/******************************************************************************
 *           NtAlpcSendWaitReceivePortEx  (NTDLL.@)
 */
NTSTATUS WINAPI NtAlpcSendWaitReceivePortEx( HANDLE port_handle, DWORD flags,
                                             ALPC_PORT_MESSAGE *send_msg,
                                             ALPC_MESSAGE_ATTRIBUTES *send_msg_attr,
                                             ALPC_PORT_MESSAGE *recv_msg,
                                             SIZE_T *recv_buffer_size,
                                             ALPC_MESSAGE_ATTRIBUTES *recv_msg_attr,
                                             LARGE_INTEGER *timeout )
{
    FIXME( "(%p, %#x, %p, %p, %p, %p, %p, %p) stub: the Ex variant adds the extended "
           "completion context; the plain path does not model it\n",
           port_handle, (unsigned int)flags, send_msg, send_msg_attr,
           recv_msg, recv_buffer_size, recv_msg_attr, timeout );
    return STATUS_NOT_IMPLEMENTED;
}


/******************************************************************************
 *           NtAlpcCreateResource  (NTDLL.@)
 */
NTSTATUS WINAPI NtAlpcCreateResource( HANDLE port_handle, PVOID view_base, SIZE_T view_size,
                                      void *resource )
{
    FIXME( "(%p, %p, %Iu, %p) stub: section-resource tracking (the kernel registers a\n"
           " view as a port resource for cross-process view reserving) is not implemented\n",
           port_handle, view_base, view_size, resource );
    return STATUS_NOT_IMPLEMENTED;
}


/******************************************************************************
 *           NtAlpcDeleteResource  (NTDLL.@)
 */
NTSTATUS WINAPI NtAlpcDeleteResource( HANDLE port_handle, HANDLE resource_handle, ULONG flags )
{
    FIXME( "(%p, %p, %#x) stub: section-resource tracking is not implemented\n",
           port_handle, resource_handle, (unsigned int)flags );
    return STATUS_NOT_IMPLEMENTED;
}


/******************************************************************************
 *           NtAlpcSendWaitReceivePort  (NTDLL.@)
 *
 * Combined send + wait-for-receive on an ALPC port or connection.
 *
 * On a SERVER port handle the received data is a wine-internal 8-byte blob
 * { ULONG is_connect=1, ULONG msg_id }.  We synthesize a proper PORT_MESSAGE
 * with Type = LPC_CONNECTION_REQUEST so the caller can pass it straight to
 * NtAlpcAcceptConnectPort.
 */
static const char *debugstr_alpc_frame( const ALPC_PORT_MESSAGE *msg, SIZE_T total )
{
    static char out[1024];
    const unsigned char *p = (const unsigned char *)msg;
    SIZE_T n = total, i;
    WORD verb = 0xffff, subid = 0xffff;

    if (n >= sizeof(*msg) + 8)
    {
        verb  = p[sizeof(*msg) + 4] | (p[sizeof(*msg) + 5] << 8);
        subid = p[sizeof(*msg) + 6] | (p[sizeof(*msg) + 7] << 8);
    }
    if (n > 400) n = 400;
    for (i = 0; i < n; i++) sprintf( out + i * 2, "%02x", p[i] );
    out[n * 2] = 0;
    return wine_dbg_sprintf( "%s(len %u verb %u subid %u)", out, (unsigned int)total, verb, subid );
}

NTSTATUS WINAPI NtAlpcSendWaitReceivePort( HANDLE port_handle, DWORD flags,
                                            ALPC_PORT_MESSAGE *send_msg,
                                            ALPC_MESSAGE_ATTRIBUTES *send_msg_attr,
                                            ALPC_PORT_MESSAGE *recv_msg,
                                            SIZE_T *recv_buffer_size,
                                            ALPC_MESSAGE_ATTRIBUTES *recv_msg_attr,
                                            LARGE_INTEGER *timeout )
{
    unsigned int ret = STATUS_SUCCESS;
    HANDLE wait_handle = NULL;
    struct alpc_async *async;
    ALPC_EVENT *recv_event;
    IO_STATUS_BLOCK io = { { STATUS_PENDING } };
    data_size_t recv_size = recv_buffer_size ? (data_size_t)*recv_buffer_size : 0;

    TRACE( "(%p, %#x, %p, %p, %p, %p, %p, %p)\n",
           port_handle, (unsigned int)flags, send_msg, send_msg_attr,
           recv_msg, recv_buffer_size, recv_msg_attr, timeout );

    /* --- Send phase --- */
    /* ALPC_MSGFLG_REPLY_WAIT_ONLY sends nothing: it only waits for the reply
     * of the request named by the message context, so the send leg is
     * skipped and the receive arms for that request's id. */
    if (send_msg && !(flags & ALPC_MSGFLG_REPLY_WAIT_ONLY))
    {
        data_size_t send_size = send_msg->TotalLength;
        struct alpc_msg_attr_hdr attr_hdr;

        /* ALPC_MSGFLG_RELEASE_MESSAGE: Windows defers consumption of a
         * delivered message until the receiver releases it (LPC
         * request-buffer recycling).  This design consumes the queued
         * message as the receive completes it (the copy is already in the
         * receiver's buffer), so there is no deferred state to release and
         * the flag is accepted as a no-op. */
        if (flags & (ALPC_MSGFLG_PC_MESSAGE | ALPC_MSGFLG_TERMINATION | ALPC_MSGFLG_REQUEST_USER_APC |
                     ALPC_MSGFLG_SYNC_CLIENT_WAIT | ALPC_MSGFLG_SYNC_CLIENT_RETURN))
            FIXME( "send flags %#x unimplemented: PC_MESSAGE needs the protected-code payload path, "
                   "TERMINATION the kernel port-closed notification, REQUEST_USER_APC/SYNC_CLIENT_* "
                   "the kernel-internal delivery bookkeeping\n", (unsigned int)flags );

        /* ALPC_MSGFLG_EMPTY_MESSAGE: send a 0-length message, no user buffer
         * travels (the kernel delivers the bare PORT_MESSAGE header). */
        if (flags & ALPC_MSGFLG_EMPTY_MESSAGE) send_size = 0;

        /* The kernel stamps the message type from the send flags and owns the
         * MessageId: a new request gets a unique non-zero id, a reply echoes
         * back the id of the request identified by the message context, and a
         * datagram carries no id. Applications leave these fields as zero (or
         * echo the context back), relying on the kernel to fill them in. */
        if (flags & ALPC_MSGFLG_REPLY_MESSAGE)
        {
            ALPC_CONTEXT_ATTR *ctx = send_msg_attr ?
                (ALPC_CONTEXT_ATTR *)get_message_attribute( send_msg_attr, ALPC_MESSAGE_CONTEXT_ATTRIBUTE ) : NULL;
            send_msg->Type = LPC_REPLY;
            if (ctx) send_msg->MessageId = ctx->MessageId;
        }
        else if (flags & ALPC_MSGFLG_SYNC_REQUEST)
        {
            send_msg->Type = LPC_REQUEST;
            /* a multi-buffer send carries several requests: the kernel stamps
             * a distinct MessageId into every buffer of the chain, so ntdll
             * leaves the ids for the server to fill */
            if (!send_msg->MessageId && !(flags & ALPC_MSGFLG_LPC_MULTI_MESSAGE))
                send_msg->MessageId = next_alpc_msg_id();
        }
        else if (send_msg->Type == LPC_DATAGRAM)
        {
            /* a datagram is marked by the message type, not a flag bit */
            send_msg->MessageId = 0;
        }
        else if (!send_msg->MessageId)
        {
            /* notification-style send on a connection port: kernel assigns an id
             * only when the sender left the message uninitialized */
            send_msg->Type = LPC_REQUEST;
            send_msg->MessageId = next_alpc_msg_id();
        }


        memset( &attr_hdr, 0, sizeof(attr_hdr) );
        attr_hdr.magic = ALPC_ATTR_HDR_MAGIC;
        if (send_msg_attr && (send_msg_attr->ValidAttributes & ALPC_MESSAGE_VIEW_ATTRIBUTE))
        {
            ALPC_DATA_VIEW_ATTR *view = get_message_attribute( send_msg_attr, ALPC_MESSAGE_VIEW_ATTRIBUTE );
            if (view)
            {
                attr_hdr.attr_flags |= ALPC_MESSAGE_VIEW_ATTRIBUTE;
                attr_hdr.view_flags = view->Flags;
                attr_hdr.view_base  = (client_ptr_t)(ULONG_PTR)view->ViewBase;
                attr_hdr.view_size  = (client_ptr_t)view->ViewSize;
                /* the kernel transports the *section*, not the sender's view:
                 * hand the section handle to the server for duplication into
                 * the receiver */
                attr_hdr.send_section = (client_ptr_t)(ULONG_PTR)view->SectionHandle;
            }
        }
        /* the CONTEXT attribute travels with the message: a reply echoes the
         * MessageContext of the request it answers, and the kernel delivers
         * that context back to the original sender on the reply */
        if (send_msg_attr && (send_msg_attr->ValidAttributes & ALPC_MESSAGE_CONTEXT_ATTRIBUTE))
        {
            ALPC_CONTEXT_ATTR *ctx = get_message_attribute( send_msg_attr, ALPC_MESSAGE_CONTEXT_ATTRIBUTE );
            if (ctx)
            {
                attr_hdr.attr_flags |= ALPC_MESSAGE_CONTEXT_ATTRIBUTE;
                attr_hdr.msg_context = (client_ptr_t)(ULONG_PTR)ctx->MessageContext;
                attr_hdr.sequence    = ctx->Sequence;
                attr_hdr.callback_id = ctx->CallbackId;
            }
        }
        /* the HANDLE attribute carries one object handle: the kernel
         * duplicates it into the receiving process as the message is
         * delivered, and the receiver sees the new handle with the
         * "created" action */
        if (send_msg_attr && (send_msg_attr->ValidAttributes & ALPC_MESSAGE_HANDLE_ATTRIBUTE))
        {
            ALPC_HANDLE_ATTR *h = get_message_attribute( send_msg_attr, ALPC_MESSAGE_HANDLE_ATTRIBUTE );
            if (h)
            {
                attr_hdr.attr_flags |= ALPC_MESSAGE_HANDLE_ATTRIBUTE;
                attr_hdr.send_handle = (client_ptr_t)(ULONG_PTR)h->Handle;
                attr_hdr.desired_access = h->DesiredAccess;
                attr_hdr.handle_count = 1;
            }
        }
        /* the send flags travel with the message: the kernel serialises a
         * synchronous request-reply exchange on the connection (a second
         * synchronous request is not delivered before the first is answered),
         * splits a multi-buffer send into per-buffer messages, and owns the
         * per-buffer MessageIds it stamps into each; the server needs all of
         * that to reproduce the exchange */
        attr_hdr.pad2 = flags;

        TRACE( "send frame: port %p type %u id %u flags %#x: %s\n",
               port_handle, send_msg->Type, send_msg->MessageId, (unsigned int)flags,
               debugstr_alpc_frame( send_msg, send_size ) );

        if (!(async = (struct alpc_async *)alloc_fileio( sizeof(*async), alpc_async_completion, port_handle )))
            return STATUS_NO_MEMORY;
        async->buffer = NULL;
        async->size   = 0;

        io.Status = STATUS_PENDING;
        wait_handle = NULL;
        SERVER_START_REQ( write )
        {
            req->async = server_async( port_handle, &async->io, NULL, NULL, NULL, iosb_client_ptr( &io ) );
            req->pos   = 0;
            wine_server_add_data( req, &attr_hdr, sizeof(attr_hdr) );
            wine_server_add_data( req, send_msg, send_size );
            ret = wine_server_call( req );
            wait_handle = wine_server_ptr_handle( reply->wait );
        }
        SERVER_END_REQ;

        if (ret == STATUS_PENDING)
            ret = wait_async( wait_handle, FALSE );  /* frees async via the completion apc */
        else
        {
            free( async );
            if (wait_handle) NtClose( wait_handle );
        }

        if (ret) return ret;

        /* a send-side event packet notification fires when the send completes */
        if (send_msg_attr && (send_msg_attr->AllocatedAttributes & ALPC_MESSAGE_EVENT_ATTRIBUTE))
        {
            ALPC_EVENT *event = get_message_attribute( send_msg_attr, ALPC_MESSAGE_EVENT_ATTRIBUTE );
            if (event)
            {
                event->UsedEntryCount = 1;
                send_msg_attr->ValidAttributes |= ALPC_MESSAGE_EVENT_ATTRIBUTE;
                if (event->Event) NtSetEvent( event->Event, NULL );
            }
        }
    }

    if (!recv_msg) return STATUS_SUCCESS;

    /* --- Receive phase --- */
    if (!(async = (struct alpc_async *)alloc_fileio( sizeof(*async), alpc_async_completion, port_handle )))
        return STATUS_NO_MEMORY;
    async->buffer = recv_msg;
    async->size   = recv_size;

    io.Status = STATUS_PENDING;
    io.Information = 0;
    wait_handle = NULL;

    recv_event = recv_msg_attr ? get_message_attribute( recv_msg_attr, ALPC_MESSAGE_EVENT_ATTRIBUTE ) : NULL;

    {
    data_size_t reply_size = 0;
    /* the receive of a send-receive pair arms for the id of the request it
     * sends; REPLY_WAIT_ONLY arms for the id of the request it only awaits */
    void *expected_id = (send_msg && (flags & (ALPC_MSGFLG_SYNC_REQUEST | ALPC_MSGFLG_REPLY_WAIT_ONLY))) ?
                        (void *)(ULONG_PTR)send_msg->MessageId : NULL;

    if (recv_event)
    {
        /* receive armed with an event packet notification: the server signals
         * the event when the queued async completes */
        SERVER_START_REQ( recv_alpc_port )
        {
            req->async = server_async( port_handle, &async->io, NULL, NULL, expected_id, iosb_client_ptr( &io ) );
            req->port  = wine_server_obj_handle( port_handle );
            req->event = recv_event->Event ? wine_server_obj_handle( recv_event->Event ) : 0;
            wine_server_set_reply( req, recv_msg, recv_size );
            ret = wine_server_call( req );
            wait_handle = wine_server_ptr_handle( reply->wait );
            reply_size  = wine_server_reply_size( reply );
            if (ret != STATUS_PENDING)
            {
                io.Status      = ret;
                io.Information  = reply_size;
            }
        }
        SERVER_END_REQ;
    }
    else SERVER_START_REQ( read )
    {
        req->async = server_async( port_handle, &async->io, NULL, NULL, expected_id, iosb_client_ptr( &io ) );
        req->pos   = 0;
        wine_server_set_reply( req, recv_msg, recv_size );
        ret = wine_server_call( req );
        wait_handle = wine_server_ptr_handle( reply->wait );
        reply_size  = wine_server_reply_size( reply );
        /* data delivered inline (message was already queued): record it on the iosb */
        if (ret != STATUS_PENDING)
        {
            io.Status      = ret;
            io.Information  = reply_size;
        }
    }
    SERVER_END_REQ;

    if (ret != STATUS_PENDING) free( async );
    else if (wait_handle)
    {
        unsigned int wait_ret = server_wait_for_object( wait_handle, FALSE, timeout );
        if (!wait_ret) ret = io.Status;
        else
        {
            NtClose( wait_handle );
            /* Windows NtAlpcSendWaitReceivePort timeout semantics: a NULL
             * Timeout blocks until a message arrives, a real Timeout expires
             * as STATUS_TIMEOUT, and a zero Timeout polls the queue and
             * reports STATUS_UNSUCCESSFUL when nothing is queued.  The
             * server parks every empty-queue receive, so translate the poll
             * expiry here. */
            if (wait_ret == STATUS_TIMEOUT && timeout && !timeout->QuadPart)
                ret = STATUS_UNSUCCESSFUL;
            else ret = wait_ret;
        }
        wait_handle = NULL;
    }
    }

    if (!ret && recv_buffer_size) *recv_buffer_size = io.Information;
    else if (ret == STATUS_BUFFER_TOO_SMALL && recv_buffer_size)
    {
        /* The kernel reports the length the queued message needs on this status
         * (the server carries it in the io status Information field).  A client
         * that grew its buffer and retried on this status - CoreMessaging's
         * receive park site does exactly that, and only retries when the
         * returned size exceeds the buffer it passed - needs a real number
         * here; leaving the caller's size untouched makes the oversize message
         * unanswerable.  Strip the wine-internal wire header so the number is
         * the message length, in the units the caller sized its buffer in. */
        data_size_t needed = io.Information;
        if (needed >= sizeof(struct alpc_wire_hdr)) needed -= sizeof(struct alpc_wire_hdr);
        *recv_buffer_size = needed;
    }

    /* Every delivered message is prefixed by a wine-internal wire header
     * { u32 magic,type,msg_id,pid,tid,user_size; u64 port_context, ... }. Strip it and
     * reconstruct the kernel-filled PORT_MESSAGE fields + attributes. */
    if (!ret && recv_msg && io.Information >= sizeof(struct alpc_wire_hdr))
    {
        struct alpc_wire_hdr hdr;
        memcpy( &hdr, recv_msg, sizeof(hdr) );
        if (hdr.magic == ALPC_WIRE_MAGIC)
        {
            char *src = (char *)recv_msg + sizeof(struct alpc_wire_hdr);
            struct alpc_handle_blk hblk = {0};

            /* a delivered handle attribute rides between the header and the
             * user payload: strip it and remember the kernel-duplicated
             * handle for the receiver's attribute set */
            if ((hdr.attr_flags & ALPC_MESSAGE_HANDLE_ATTRIBUTE) &&
                io.Information >= sizeof(hdr) + sizeof(hblk))
            {
                memcpy( &hblk, src, sizeof(hblk) );
                src += sizeof(hblk);
            }
            SIZE_T total;

            if (hdr.type == LPC_CONNECTION_REQUEST)
            {
                /* user bytes are the raw connection payload: build a PORT_MESSAGE */
                if (hdr.user_size) memmove( (char *)recv_msg + sizeof(*recv_msg), src, hdr.user_size );
                memset( recv_msg, 0, sizeof(*recv_msg) );
                recv_msg->DataLength  = hdr.user_size;
                recv_msg->TotalLength = sizeof(*recv_msg) + hdr.user_size;
                recv_msg->Type        = LPC_NEW_CONNECTION;
                recv_msg->MessageId   = hdr.msg_id;
                total = sizeof(*recv_msg) + hdr.user_size;
                /* The kernel stamps a connection request's PORT_MESSAGE with the
                 * connecting thread's client id.  The connection request is the one
                 * message the receiver's engine parses with the Windows PORT_MESSAGE
                 * layout - ClientId.UniqueProcess at +1*sizeof(void*), UniqueThread at
                 * +2*sizeof(void*) - not Wine's shifted in-process layout the relay
                 * correlation uses for ordinary messages (MessageId at +4*sizeof).
                 * Filling the id through the struct writes it into the shifted slots,
                 * leaving the Windows process-id slot (the +1*sizeof pad Wine reserves
                 * for kernel alignment) zero; the registrar's partition gate reads that
                 * zero as the companion client id and fails the connect.  Stamp the id
                 * at the offsets the engine reads. */
                *(client_ptr_t *)((char *)recv_msg + 1 * sizeof(void *)) = hdr.client_pid;
                *(client_ptr_t *)((char *)recv_msg + 2 * sizeof(void *)) = hdr.client_tid;
            }
            else
            {
                /* user bytes are the sender's full PORT_MESSAGE: preserve its
                 * Type/MessageId/CallbackId (reply correlation), only stamp ClientId */
                memmove( recv_msg, src, hdr.user_size );
                total = hdr.user_size;
                recv_msg->ClientId.UniqueProcess = (HANDLE)(ULONG_PTR)hdr.client_pid;
                recv_msg->ClientId.UniqueThread  = (HANDLE)(ULONG_PTR)hdr.client_tid;
            }
            if (recv_buffer_size) *recv_buffer_size = total;
            TRACE( "recv frame: port %p type %u id %u: %s\n",
                   port_handle, recv_msg->Type, recv_msg->MessageId,
                   debugstr_alpc_frame( recv_msg, total ) );

            /* deliver attributes */
            if (recv_msg_attr)
            {
                if (recv_msg_attr->AllocatedAttributes & ALPC_MESSAGE_CONTEXT_ATTRIBUTE)
                {
                    ALPC_CONTEXT_ATTR *ctx = get_message_attribute( recv_msg_attr, ALPC_MESSAGE_CONTEXT_ATTRIBUTE );
                    if (ctx)
                    {
                        /* the kernel delivers the sender's context fields with
                         * the message: a server reading a request echoes them
                         * back on the reply (the CoreMessaging engine routes
                         * its replies and its chunked-message reassembly by
                         * them), and the message id is the kernel's */
                        /* The slot is the receiver's own context value, as the
                         * kernel echoes it: for an ordinary send the per-connection
                         * context the server passed to NtAlpcAcceptConnectPort, for
                         * a connect notification the context registered with the
                         * port the notification is queued on (nothing registers one,
                         * so null).  Both are the app's own bits, so pass them
                         * through untouched.  The app reads the value back as a
                         * pointer - CoreMessaging's accept path dereferences it at
                         * CoreMessagingXP+0x85b48 to take the port handle out of it
                         * - so stamping a wineserver handle in here hands the app a
                         * small integer where it expects a pointer: its null test
                         * does not catch a handle, and it faults on that value. */
                        ctx->PortContext    = (void *)(ULONG_PTR)hdr.port_context;
                        ctx->MessageContext = (void *)(ULONG_PTR)hdr.msg_context;
                        ctx->Sequence       = hdr.sequence;
                        ctx->MessageId      = hdr.msg_id;
                        ctx->CallbackId     = hdr.callback_id;
                        recv_msg_attr->ValidAttributes |= ALPC_MESSAGE_CONTEXT_ATTRIBUTE;
                    }
                }

                /* a message delivered from a multi-buffer send carries its
                 * position in the chain in the CONTEXT attribute: index in
                 * Sequence, chain length in CallbackId (the kernel fills the
                 * sequence context into the context from the split it
                 * performed; 0x2000 allocates no payload of its own) */

                if ((hdr.attr_flags & ALPC_MESSAGE_VIEW_ATTRIBUTE) &&
                    (recv_msg_attr->AllocatedAttributes & ALPC_MESSAGE_VIEW_ATTRIBUTE))
                {
                    ALPC_DATA_VIEW_ATTR *view = get_message_attribute( recv_msg_attr, ALPC_MESSAGE_VIEW_ATTRIBUTE );
                    if (view)
                    {
                        view->Flags         = hdr.view_flags;
                        if (hdr.view_section)
                        {
                            /* the kernel duplicated the sender's section into
                             * this process on delivery: report the
                             * duplicated handle; the receiver maps it with
                             * NtAlpcCreateSectionView (ViewBase/ViewSize are
                             * filled by the map) */
                            view->SectionHandle = wine_server_ptr_handle( (obj_handle_t)hdr.view_section );
                            view->ViewBase      = NULL;
                            view->ViewSize      = (SIZE_T)hdr.view_size;
                        }
                        else
                        {
                            view->SectionHandle = NULL;
                            view->ViewBase      = (void *)(ULONG_PTR)hdr.view_base;
                            view->ViewSize      = (SIZE_T)hdr.view_size;
                        }
                        recv_msg_attr->ValidAttributes |= ALPC_MESSAGE_VIEW_ATTRIBUTE;
                    }
                }

                /* the kernel duplicated the sender's handle into this process
                 * on delivery: report it in the handle attribute with the
                 * notification action and the handle count */
                if ((hdr.attr_flags & ALPC_MESSAGE_HANDLE_ATTRIBUTE) && hblk.handle_id &&
                    (recv_msg_attr->AllocatedAttributes & ALPC_MESSAGE_HANDLE_ATTRIBUTE))
                {
                    ALPC_HANDLE_ATTR *h = get_message_attribute( recv_msg_attr, ALPC_MESSAGE_HANDLE_ATTRIBUTE );
                    if (h)
                    {
                        h->Flags         = hblk.handle_action; /* 1 = created */
                        h->Handle        = wine_server_ptr_handle( (obj_handle_t)hblk.handle_id );
                        h->ObjectType    = 0;
                        h->DesiredAccess = 0;
                        recv_msg_attr->ValidAttributes |= ALPC_MESSAGE_HANDLE_ATTRIBUTE;
                    }
                }

                if (recv_event)
                {
                    recv_event->UsedEntryCount = 1;
                    recv_msg_attr->ValidAttributes |= ALPC_MESSAGE_EVENT_ATTRIBUTE;
                    /* pulse the notification event on delivery; the queued path
                     * also signals it server-side through the async event */
                    if (recv_event->Event) NtSetEvent( recv_event->Event, NULL );
                }
            }
        }
    }

    return ret;
}


/******************************************************************************
 *           NtAlpcQueryInformation  (NTDLL.@)
 *
 * CoreMessaging queries AlpcBasicInformation (class 0) on the connection port
 * carried in a received message's CONTEXT PortContext, at the start of every
 * multi-message ("slurp") exchange, and compares the returned SequenceNo with
 * the number of messages it has received on that port: when they match, the
 * exchange arrived as a contiguous pair and the connection's receive state
 * machine completes.  SequenceNo is the port's message sequence counter, which
 * the kernel advances as each message is queued on the port, so it must be
 * tracked per port by the server.  A constant answer never matches a running
 * receive count: the pair is never confirmed, and the client's next
 * NtAlpcSendWaitReceivePort parks on an already drained queue with no timeout,
 * deadlocking the UI thread inside AppWindow::EnsureMainWindowInitialized.
 * Returning an error is worse: it sends CoreMessaging down a failure path that
 * leaves the port object half-initialised.
 */

NTSTATUS WINAPI NtAlpcQueryInformation( HANDLE port_handle, ULONG class,
                                         void *buffer, ULONG length, ULONG *ret_length )
{
    TRACE( "(%p, %u, %p, %u, %p)\n", port_handle, (int)class, buffer, (int)length, ret_length );

    switch (class)
    {
    case 0: /* AlpcBasicInformation */
    {
        ALPC_BASIC_INFORMATION info;
        unsigned int ret = 0;
        NTSTATUS st;

        memset( &info, 0, sizeof(info) );

        /* AlpcBasicInformation is { Flags, SequenceNo, PortContext }: 16 bytes on
         * x86_64 (the context slot needs 8-byte alignment), 12 on i386.  The
         * length gate is the whole structure, which is what Windows enforces
         * (test_alpc_query_basic_length / test_NtAlpcQueryInformation pin a
         * sizeof-4 request to STATUS_INFO_LENGTH_MISMATCH).  CoreMessaging's
         * slurp probe (AlpcBuffer::CommitToSlurp, 0x180085ef0) passes
         * Length = 16, which on x86_64 is exactly the structure, so it is
         * answered here; the gate is not the source of a fail-fast on that call. */
        if (length < sizeof(info)) st = STATUS_INFO_LENGTH_MISMATCH;
        else if (!port_handle) st = STATUS_INVALID_HANDLE;
        else
        {
            SERVER_START_REQ( alpc_query_port )
            {
                req->handle = wine_server_obj_handle( port_handle );
                ret = wine_server_call( req );
                if (!ret)
                {
                    info.Flags       = reply->flags;
                    info.SequenceNo  = reply->sequence_no;
                    info.PortContext = (void *)(ULONG_PTR)reply->port_context;
                }
            }
            SERVER_END_REQ;
            if (ret) st = ret;
            else
            {
                memcpy( buffer, &info, length < sizeof(info) ? length : sizeof(info) );
                if (ret_length) *ret_length = sizeof(info);
                st = STATUS_SUCCESS;
            }
        }
        return st;
    }
    default:
        FIXME( "unsupported info class %u\n", (int)class );
        return STATUS_INVALID_INFO_CLASS;
    }
}


/******************************************************************************
 *           NtAlpcSetInformation  (NTDLL.@)
 */
NTSTATUS WINAPI NtAlpcSetInformation( HANDLE port_handle, ULONG class, void *buffer, ULONG length )
{
    TRACE( "(%p, %u, %p, %u)\n", port_handle, (int)class, buffer, (int)length );

    switch (class)
    {
    case 1: /* AlpcPortSecurityDescriptorInformation: buffer is a PSECURITY_DESCRIPTOR */
        if (!buffer) return STATUS_ACCESS_VIOLATION;
        if (length < sizeof(PSECURITY_DESCRIPTOR)) return STATUS_INVALID_PARAMETER;
        return NtSetSecurityObject( port_handle, DACL_SECURITY_INFORMATION, *(PSECURITY_DESCRIPTOR *)buffer );
    default:
        FIXME( "unsupported info class %u\n", (int)class );
        return STATUS_INVALID_INFO_CLASS;
    }
}


/******************************************************************************
 *           NtAlpcImpersonateClientOfPort  (NTDLL.@)
 */
NTSTATUS WINAPI NtAlpcImpersonateClientOfPort( HANDLE port_handle,
                                                ALPC_PORT_MESSAGE *msg, void *reserved )
{
    NTSTATUS ret;
    HANDLE proc_token = NULL, imp_token = NULL;
    OBJECT_ATTRIBUTES attr;
    SECURITY_QUALITY_OF_SERVICE qos;

    TRACE( "(%p, %p, %p)\n", port_handle, msg, reserved );

    /* In-process, same-user ALPC: impersonate the client by duplicating our own
     * process token as an impersonation token and applying it to the current
     * thread. Servers (e.g. CoreMessaging) impersonate during connection
     * acceptance and reject the connection if it fails. */
    /* FIXME: real client-token impersonation.  The kernel impersonates the
     * *client's* primary/dominant token as recorded on the connection (with
     * the ALPC_PORTFLG_ALLOWIMPERSONATION gate and the client's session);
     * this fakes it with the server's own process token, so cross-user and
     * token-restricted clients are indistinguishable.  Needs the server to
     * carry the client's token on the connection and a get-token request. */
    if ((ret = NtOpenProcessToken( NtCurrentProcess(), TOKEN_DUPLICATE, &proc_token )))
        return ret;

    qos.Length              = sizeof(qos);
    qos.ImpersonationLevel  = SecurityImpersonation;
    qos.ContextTrackingMode = SECURITY_DYNAMIC_TRACKING;
    qos.EffectiveOnly       = FALSE;
    InitializeObjectAttributes( &attr, NULL, 0, NULL, NULL );
    attr.SecurityQualityOfService = &qos;

    ret = NtDuplicateToken( proc_token, TOKEN_IMPERSONATE | TOKEN_QUERY, &attr,
                            FALSE, TokenImpersonation, &imp_token );
    NtClose( proc_token );
    if (ret) return ret;

    ret = NtSetInformationThread( NtCurrentThread(), ThreadImpersonationToken,
                                  &imp_token, sizeof(imp_token) );
    NtClose( imp_token );
    return ret;
}
