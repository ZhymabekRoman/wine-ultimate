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
#define LPC_CONNECTION_REQUEST 10

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
    async->buffer = NULL;
    async->size   = 0;

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
        NtWaitForSingleObject( wait_handle, FALSE, timeout );
        NtClose( wait_handle );
        ret = io.Status;
    }

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
        req->server_port = wine_server_obj_handle( connection_port );
        req->msg_id      = msg_id;
        req->accept      = accept_connection;
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
    IO_STATUS_BLOCK io = { { STATUS_PENDING } };
    data_size_t recv_size = recv_buffer_size ? (data_size_t)*recv_buffer_size : 0;

    TRACE( "(%p, %#x, %p, %p, %p, %p, %p, %p)\n",
           port_handle, (unsigned int)flags, send_msg, send_msg_attr,
           recv_msg, recv_buffer_size, recv_msg_attr, timeout );

    /* --- Send phase --- */
    if (send_msg)
    {
        data_size_t send_size = send_msg->TotalLength;

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
            wine_server_add_data( req, send_msg, send_size );
            ret = wine_server_call( req );
            wait_handle = wine_server_ptr_handle( reply->wait );
        }
        SERVER_END_REQ;

        if (ret != STATUS_PENDING) free( async );
        if (wait_handle) ret = wait_async( wait_handle, FALSE );

        if (ret) return ret;
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

    {
    data_size_t reply_size = 0;
    SERVER_START_REQ( read )
    {
        req->async = server_async( port_handle, &async->io, NULL, NULL, NULL, iosb_client_ptr( &io ) );
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
        NtWaitForSingleObject( wait_handle, FALSE, timeout );
        NtClose( wait_handle );
        ret = io.Status;
        wait_handle = NULL;
    }
    if (wait_handle) NtClose( wait_handle );
    }

    if (!ret && recv_buffer_size) *recv_buffer_size = io.Information;

    /* Detect the wine-internal connect-notification blob
     * { magic, msg_id, client_pid, client_tid, payload_size, payload... }
     * and turn it into a proper LPC_CONNECTION_REQUEST PORT_MESSAGE in-place. */
    if (!ret && recv_msg && io.Information >= 5 * sizeof(ULONG))
    {
        ULONG hdr[5];
        memcpy( hdr, recv_msg, sizeof(hdr) );
        if (hdr[0] == 0x434e4e41) /* 'ANNC' */
        {
            ULONG msg_id = hdr[1], pid = hdr[2], tid = hdr[3], payload = hdr[4];
            char *src = (char *)recv_msg + 5 * sizeof(ULONG);

            if (payload) memmove( (char *)recv_msg + sizeof(*recv_msg), src, payload );
            memset( recv_msg, 0, sizeof(*recv_msg) );
            recv_msg->DataLength  = payload;
            recv_msg->TotalLength = sizeof(*recv_msg) + payload;
            recv_msg->Type        = LPC_CONNECTION_REQUEST;
            recv_msg->MessageId   = msg_id;
            recv_msg->ClientId.UniqueProcess = (HANDLE)(ULONG_PTR)pid;
            recv_msg->ClientId.UniqueThread  = (HANDLE)(ULONG_PTR)tid;
            if (recv_buffer_size) *recv_buffer_size = sizeof(*recv_msg) + payload;
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
 *           NtAlpcImpersonateClientOfPort  (NTDLL.@)
 */
NTSTATUS WINAPI NtAlpcImpersonateClientOfPort( HANDLE port_handle,
                                                ALPC_PORT_MESSAGE *msg, void *reserved )
{
    FIXME( "(%p, %p, %p): stub\n", port_handle, msg, reserved );
    return STATUS_NOT_IMPLEMENTED;
}
