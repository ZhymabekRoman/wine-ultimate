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

/* ALPC message types (subset of Windows LPC message types) */
#define LPC_REQUEST            1
#define LPC_DATAGRAM           2
#define LPC_REPLY              0x10
#define LPC_CONNECTION_REQUEST 10 /* matches the value server/alpc.c stamps */

/* NtAlpcSendWaitReceivePort send flags */
#define ALPC_SEND_REPLY        0x10000
#define ALPC_SEND_REQUEST      0x20000
#define ALPC_SEND_DATAGRAM     0x40000

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

/* Process-global message id counter. Only needs to be unique within a single
 * server port's pending-connection list, so a global atomic is fine. */
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
    unsigned int msg_id;
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

    msg_id = next_alpc_msg_id();

    SERVER_START_REQ( connect_alpc_port )
    {
        req->async   = server_async( NULL, &async->io, NULL, NULL, NULL, iosb_client_ptr( &io ) );
        req->msg_id  = msg_id;
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

    if (!ret && connect_msg_size) *connect_msg_size = io.Information;

    if (ret && ret != STATUS_PENDING && *port_handle)
    {
        NtClose( *port_handle );
        *port_handle = NULL;
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
 *           NtAlpcSendWaitReceivePort  (NTDLL.@)
 *
 * Combined send + wait-for-receive on an ALPC port or connection.
 *
 * On a SERVER port handle the received data is a wine-internal 8-byte blob
 * { ULONG is_connect=1, ULONG msg_id }.  We synthesize a proper PORT_MESSAGE
 * with Type = LPC_CONNECTION_REQUEST so the caller can pass it straight to
 * NtAlpcAcceptConnectPort.
 */
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
    if (send_msg)
    {
        data_size_t send_size = send_msg->TotalLength;
        struct alpc_msg_attr_hdr attr_hdr;

        /* The kernel stamps the message type from the send flags and owns the
         * MessageId: a new request gets a unique non-zero id, a reply echoes
         * back the id of the request identified by the message context, and a
         * datagram carries no id. Applications leave these fields as zero (or
         * echo the context back), relying on the kernel to fill them in. */
        if (flags & ALPC_SEND_REPLY)
        {
            ALPC_CONTEXT_ATTR *ctx = send_msg_attr ?
                (ALPC_CONTEXT_ATTR *)get_message_attribute( send_msg_attr, ALPC_MESSAGE_CONTEXT_ATTRIBUTE ) : NULL;
            send_msg->Type = LPC_REPLY;
            if (ctx) send_msg->MessageId = ctx->MessageId;
        }
        else if (flags & ALPC_SEND_REQUEST)
        {
            send_msg->Type = LPC_REQUEST;
            if (!send_msg->MessageId) send_msg->MessageId = next_alpc_msg_id();
        }
        else if (flags & ALPC_SEND_DATAGRAM)
        {
            send_msg->Type = LPC_DATAGRAM;
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
                attr_hdr.attr_flags = ALPC_MESSAGE_VIEW_ATTRIBUTE;
                attr_hdr.view_flags = view->Flags;
                attr_hdr.view_base  = (client_ptr_t)(ULONG_PTR)view->ViewBase;
                attr_hdr.view_size  = (client_ptr_t)view->ViewSize;
            }
        }

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
    void *expected_id = (flags & 0x20000 && send_msg) ? (void *)(ULONG_PTR)send_msg->MessageId : NULL;

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
            ret = wait_ret;
        }
        wait_handle = NULL;
    }
    }

    if (!ret && recv_buffer_size) *recv_buffer_size = io.Information;

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
            SIZE_T total;

            if (hdr.type == LPC_CONNECTION_REQUEST)
            {
                /* user bytes are the raw connection payload: build a PORT_MESSAGE */
                if (hdr.user_size) memmove( (char *)recv_msg + sizeof(*recv_msg), src, hdr.user_size );
                memset( recv_msg, 0, sizeof(*recv_msg) );
                recv_msg->DataLength  = hdr.user_size;
                recv_msg->TotalLength = sizeof(*recv_msg) + hdr.user_size;
                recv_msg->Type        = LPC_CONNECTION_REQUEST;
                recv_msg->MessageId   = hdr.msg_id;
                total = sizeof(*recv_msg) + hdr.user_size;
            }
            else
            {
                /* user bytes are the sender's full PORT_MESSAGE: preserve its
                 * Type/MessageId/CallbackId (reply correlation), only stamp ClientId */
                memmove( recv_msg, src, hdr.user_size );
                total = hdr.user_size;
            }
            recv_msg->ClientId.UniqueProcess = (HANDLE)(ULONG_PTR)hdr.client_pid;
            recv_msg->ClientId.UniqueThread  = (HANDLE)(ULONG_PTR)hdr.client_tid;
            if (recv_buffer_size) *recv_buffer_size = total;

            /* deliver attributes */
            if (recv_msg_attr)
            {
                if (recv_msg_attr->AllocatedAttributes & ALPC_MESSAGE_CONTEXT_ATTRIBUTE)
                {
                    ALPC_CONTEXT_ATTR *ctx = get_message_attribute( recv_msg_attr, ALPC_MESSAGE_CONTEXT_ATTRIBUTE );
                    if (ctx)
                    {
                        ctx->PortContext    = (void *)(ULONG_PTR)hdr.port_context;
                        ctx->MessageContext = NULL;
                        ctx->Sequence       = 0;
                        ctx->MessageId      = hdr.msg_id;
                        ctx->CallbackId     = 0;
                        recv_msg_attr->ValidAttributes |= ALPC_MESSAGE_CONTEXT_ATTRIBUTE;
                    }
                }

                if ((hdr.attr_flags & ALPC_MESSAGE_VIEW_ATTRIBUTE) &&
                    (recv_msg_attr->AllocatedAttributes & ALPC_MESSAGE_VIEW_ATTRIBUTE))
                {
                    ALPC_DATA_VIEW_ATTR *view = get_message_attribute( recv_msg_attr, ALPC_MESSAGE_VIEW_ATTRIBUTE );
                    if (view)
                    {
                        view->Flags         = hdr.view_flags;
                        view->SectionHandle = NULL;
                        view->ViewBase      = (void *)(ULONG_PTR)hdr.view_base;
                        view->ViewSize      = (SIZE_T)hdr.view_size;
                        recv_msg_attr->ValidAttributes |= ALPC_MESSAGE_VIEW_ATTRIBUTE;
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
 * CoreMessaging queries AlpcBasicInformation (class 0) right after creating its
 * registrar port and caches the returned SequenceNo. Returning an error sends it
 * down a failure path that leaves the port object half-initialised, so provide a
 * minimal successful answer.
 */
static LONG alpc_sequence_counter;

NTSTATUS WINAPI NtAlpcQueryInformation( HANDLE port_handle, ULONG class,
                                         void *buffer, ULONG length, ULONG *ret_length )
{
    TRACE( "(%p, %u, %p, %u, %p)\n", port_handle, (int)class, buffer, (int)length, ret_length );

    switch (class)
    {
    case 0: /* AlpcBasicInformation */
    {
        ALPC_BASIC_INFORMATION info;
        if (length < sizeof(info)) return STATUS_INFO_LENGTH_MISMATCH;
        info.Flags       = 0;
        info.SequenceNo  = InterlockedIncrement( &alpc_sequence_counter );
        info.PortContext = NULL;
        memcpy( buffer, &info, sizeof(info) );
        if (ret_length) *ret_length = sizeof(info);
        return STATUS_SUCCESS;
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
