/*
 * CoreUICore registrar conversation provider
 *
 * The CoreUI engine (CoreMessagingXP.dll, loaded by WinUI apps) self-hosts
 * a registrar on the ALPC port
 * "\BaseNamedObjects\CoreMessagingRegistrar-<pid>:<partition-guid>"
 * (Guid.Empty for the in-process self-host) when a CoreUI session is
 * initialised.  XAML dispatcher queues join the "CoreUICore" conversation
 * through that registrar; on Windows the conversation's server is the CoreUI
 * core thread hosted by the system CoreMessaging.dll.  Without a provider
 * the registrar lookup of the conversation name misses, the join is answered
 * with a failure result and the dispatcher thread fail-fasts.
 *
 * This file implements the builtin provider side:
 *   - a provider thread connects to the self-hosted registrar with the
 *     engine port context (24-byte connect message: engine GUID + context),
 *   - registers the thread (registrar client id) and the conversations the
 *     builtin serves,
 *   - runs the receive loop answering the requests the registrar routes to
 *     the provider (conversation joins, peer management, CoreUICall verb
 *     traffic) with synchronous replies.
 *
 * The wire format of the registrar channel (recovered from the Windows
 * binary and confirmed by runtime traces): every ALPC message data section
 * carries one call record
 *      +0 u32 total size
 *      +4 u16 verb   (2 = request, 0 = reply, 3 = callback)
 *      +6 u16 subid  (interface method)
 *      +8 parameters: [u32 size][payload padded to 4] per parameter.
 *
 * Copyright 2026 Zhymabek Roman
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
 */

#include <stdarg.h>
#include <string.h>

#include "windef.h"
#include "winbase.h"
#include "winerror.h"
#include "winnt.h"
#include "winternl.h"
#include "ntstatus.h"
#include "objbase.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(messaging);

#define REGISTRAR_MESSAGE_SIZE  0x1000
#define FRAME_HEADER_SIZE       8
#define ALPC_SYNC_REQUEST       0x00020000  /* ALPC_MSGFLG_SYNC_REQUEST */
#define ALPC_SEND_REPLY         0x00010000  /* ALPC_MSGFLG_REPLY_MESSAGE */

/* Cn engine identifier: the registrar validates the 24-byte connect port
 * context against it (the engine GUID of the CoreMessaging implementation). */
static const GUID cn_engine_guid =
    { 0x29324ee4, 0xdc59, 0x49dc, { 0xa2, 0x3f, 0x8c, 0xa1, 0x1b, 0xef, 0x42, 0x27 } };

struct registrar_provider
{
    HANDLE connection;                      /* ALPC connection to the self-hosted registrar */
    CRITICAL_SECTION send_cs;               /* serialises synchronous request/response */
    ULONG client_id;                        /* RegistrarClientId handed out by RegisterThread */
    ULONG conversation_id;                  /* conversation id assigned by RegisterConversation */
    volatile LONG running;
    HANDLE thread;
};

static struct registrar_provider provider;
static INIT_ONCE provider_init_once = INIT_ONCE_STATIC_INIT;

/*
 * Message frame helpers.  Buffers are pre-zeroed; the writer appends TLVs
 * and keeps the total size in the header.
 */
static void frame_init( void *buffer, ULONG total, USHORT verb, USHORT subid )
{
    memset( buffer, 0, FRAME_HEADER_SIZE );
    *(ULONG *)buffer = total;
    *(USHORT *)((char *)buffer + 4) = verb;
    *(USHORT *)((char *)buffer + 6) = subid;
}

static ULONG frame_append_tlv( void *buffer, ULONG offset, const void *data, ULONG size )
{
    ULONG padded = (size + 3) & ~3u;

    *(ULONG *)((char *)buffer + offset) = size;
    if (size && data) memcpy( (char *)buffer + offset + 4, data, size );
    return offset + 4 + padded;
}

static ULONG frame_append_string( void *buffer, ULONG offset, const WCHAR *str )
{
    return frame_append_tlv( buffer, offset, str, (lstrlenW( str ) + 1) * sizeof(WCHAR) );
}

struct tlv_reader
{
    const char *base;
    ULONG offset;
};

static BOOL tlv_read( struct tlv_reader *r, const void **data, ULONG *size )
{
    ULONG size_val, padded;

    if (r->offset + 4 > REGISTRAR_MESSAGE_SIZE) return FALSE;
    size_val = *(const ULONG *)(r->base + r->offset);
    padded = (size_val + 3) & ~3u;
    if (r->offset + 4 + padded > REGISTRAR_MESSAGE_SIZE) return FALSE;
    *data = r->base + r->offset + 4;
    *size = size_val;
    r->offset += 4 + padded;
    return TRUE;
}

/*
 * Send a request on the registrar connection and wait for the reply
 * (ALPC synchronous request).  The reply payload is returned in 'reply'.
 */
/*
 * Send a request frame on the registrar connection and wait for the reply
 * (ALPC synchronous request).  'frame' is the call record (header + TLVs);
 * it is wrapped in a PORT_MESSAGE in place: the caller's buffer must have
 * room for the message header before the frame.
 */
static NTSTATUS registrar_send_request( void *buffer, ULONG frame_size,
                                        void *reply, ULONG *reply_size )
{
    NTSTATUS status;
    LARGE_INTEGER timeout;
    SIZE_T size = *reply_size;
    ALPC_PORT_MESSAGE *msg = (ALPC_PORT_MESSAGE *)((char *)buffer - sizeof(*msg));

    memset( msg, 0, sizeof(*msg) );
    msg->DataLength = frame_size;
    msg->TotalLength = sizeof(*msg) + frame_size;

    EnterCriticalSection( &provider.send_cs );

    timeout.QuadPart = -10 * 10000000;  /* 10 s */
    status = NtAlpcSendWaitReceivePort( provider.connection, ALPC_SYNC_REQUEST,
                                        msg, NULL, reply, &size, NULL, &timeout );
    LeaveCriticalSection( &provider.send_cs );

    if (!status) *reply_size = size;
    return status;
}

/*
 * RegisterThread: the provider announces itself to the registrar and
 * receives its RegistrarClientId.  Mirrors the request the CoreUI engine
 * sends on its own connection (verb 2 subid 0).
 */
/*
 * Cn InterconnectQueue datagram layout on the registrar connection
 * (confirmed byte-for-byte from the engine's RegisterThread exchange):
 *   +0  16 zero bytes (queue preamble)
 *   +16 u32 command (2 = request, 0 = reply)
 *   +20 u32 reserved
 *   +24 u64 max message size
 *   +32 u32 item size
 *   +36 u32 reserved
 *   +40 item bytes (for requests, a CoreUICall call frame:
 *       u32 total, u16 verb, u16 subid, TLV parameters)
 * The registrar's reply datagram carries {u64 frame total, u64 max size,
 * u32 client id, u32, u32, u32, u32, u32, u32, u64, u64} before its item.
 */
#define CN_ITEM_OFFSET      40
#define CN_REPLY_ID_OFFSET  32
#define CN_MAX_MSG          0x10000

static BOOL registrar_register_thread( void )
{
    ULONG reply_size = REGISTRAR_MESSAGE_SIZE;
    char buffer[sizeof(ALPC_PORT_MESSAGE) + 128], reply[REGISTRAR_MESSAGE_SIZE];
    char *frame = buffer + sizeof(ALPC_PORT_MESSAGE);
    ALPC_PORT_MESSAGE *reply_msg = (void *)reply;
    NTSTATUS status;

    /* request datagram carrying the 8-byte item {2, 0x10001}: the
     * registrar control conversation registration, byte-identical to the
     * engine's own RegisterThread. */
    memset( frame, 0, CN_ITEM_OFFSET + 8 );
    *(ULONG *)(frame + 16) = 2;                       /* command */
    *(ULONGLONG *)(frame + 24) = CN_MAX_MSG;           /* max message size */
    *(ULONG *)(frame + 32) = 8;                        /* item size */
    *(ULONG *)(frame + 40) = 2;                        /* item: flags */
    *(ULONG *)(frame + 44) = 0x10001;                  /* item: control conversation */
    ULONG off = CN_ITEM_OFFSET + 8;

    status = registrar_send_request( frame, off, reply_msg, &reply_size );
    if (status)
    {
        WARN( "RegisterThread failed, status %#lx.\n", status );
        return FALSE;
    }
    if (reply_size >= sizeof(*reply_msg) + 88)
    {
        const char *p = (const char *)reply_msg + sizeof(*reply_msg);

        provider.client_id = *(const ULONG *)(p + CN_REPLY_ID_OFFSET);
        TRACE( "registered with registrar, client id %u.\n", provider.client_id );
        return TRUE;
    }
    WARN( "RegisterThread short reply %u bytes.\n", reply_size );
    return FALSE;
}

/*
 * RegisterConversation: declare the "CoreUICore" conversation so the
 * registrar name lookup finds it when XAML dispatcher queues join.
 * verb 2 subid 14, six parameters: conversation name, server name,
 * ConversationInfo(24), RoutingInfo(16), HIDENTITY(40), ScopeID(8).
 */
static BOOL registrar_register_conversation( void )
{
    static const WCHAR conversation_name[] = {'C','o','r','e','U','I','C','o','r','e',0};
    static const WCHAR server_name[] = {'W','i','n','e','C','o','r','e','M','e','s','s','a','g','i','n','g',0};
    ULONG reply_size = REGISTRAR_MESSAGE_SIZE;
    char buffer[sizeof(ALPC_PORT_MESSAGE) + 256], reply[REGISTRAR_MESSAGE_SIZE];
    char *frame = buffer + sizeof(ALPC_PORT_MESSAGE);
    ALPC_PORT_MESSAGE *reply_msg = (void *)reply;
    ULONG off = FRAME_HEADER_SIZE;
    ULONG info[6] = {0};            /* ConversationInfo: itemOwner 0, serverStyle 0, connectionMode 0 */
    ULONG routing[4];               /* RoutingInfo: scope kind + our client id */
    BYTE identity[40] = {0};        /* HIDENTITY: local session, empty */
    ULONG scope[2] = {0, 0};        /* ScopeID: kind 0 = local process */
    NTSTATUS status;
    char *item;

    routing[0] = 0;
    routing[1] = provider.client_id;
    routing[2] = 0;
    routing[3] = 0;

    /* item: CoreUICall call frame verb 2 subid 14 */
    item = frame + CN_ITEM_OFFSET;
    frame_init( item, 256 - CN_ITEM_OFFSET, 2, 14 );
    off = FRAME_HEADER_SIZE;
    off = frame_append_string( item, off, conversation_name );
    off = frame_append_string( item, off, server_name );
    off = frame_append_tlv( item, off, info, sizeof(info) );
    off = frame_append_tlv( item, off, routing, sizeof(routing) );
    off = frame_append_tlv( item, off, identity, sizeof(identity) );
    off = frame_append_tlv( item, off, scope, sizeof(scope) );
    *(ULONG *)item = off;

    /* datagram envelope */
    memset( frame, 0, CN_ITEM_OFFSET );
    *(ULONG *)(frame + 16) = 2;
    *(ULONGLONG *)(frame + 24) = CN_MAX_MSG;
    *(ULONG *)(frame + 32) = off;           /* item size */
    off += CN_ITEM_OFFSET;

    status = registrar_send_request( frame, off, reply_msg, &reply_size );
    if (status)
    {
        WARN( "RegisterConversation failed, status %#lx.\n", status );
        return FALSE;
    }
    if (reply_size >= sizeof(*reply_msg) + 88)
    {
        const char *p = (const char *)reply_msg + sizeof(*reply_msg);
        ULONG itemsize = *(const ULONG *)(p + 56);
        ULONG total = reply_size - sizeof(*reply_msg);

        if (itemsize >= 16 && total >= 80 + itemsize)
        {
            const char *item_reply = p + 80;
            struct tlv_reader rr;
            const void *data;
            ULONG size;

            rr.base = item_reply + FRAME_HEADER_SIZE;
            rr.offset = 0;
            if (tlv_read( &rr, &data, &size ) && size >= 4 && *(const ULONG *)data == 0)
            {
                if (tlv_read( &rr, &data, &size ) && size >= 4)
                    provider.conversation_id = *(const ULONG *)data;
                TRACE( "registered conversation, id %u.\n", provider.conversation_id );
                return TRUE;
            }
            WARN( "RegisterConversation rejected, result %#x.\n", *(const ULONG *)data );
            return FALSE;
        }
        TRACE( "RegisterConversation reply %u bytes itemsize %u (item at reply+80, need %u).\n",
               total, itemsize, 80 + itemsize );
        return TRUE;   /* accepted; conversation id left at the default */
    }
    WARN( "RegisterConversation short reply %u bytes.\n", reply_size );
    return FALSE;
}

/*
 * Reply to a routed request.  The context attributes echo the receive
 * message context so the kernel correlates the reply.
 */
static NTSTATUS registrar_reply( const void *frame, ULONG frame_size,
                                 const ALPC_CONTEXT_ATTR *recv_ctx )
{
    struct
    {
        ALPC_MESSAGE_ATTRIBUTES attr;
        ALPC_CONTEXT_ATTR ctx;
    } attrs;
    char buffer[REGISTRAR_MESSAGE_SIZE];
    ALPC_PORT_MESSAGE *msg = (void *)buffer;

    memset( msg, 0, sizeof(*msg) );
    msg->DataLength = frame_size;
    msg->TotalLength = sizeof(*msg) + frame_size;
    memcpy( (char *)msg + sizeof(*msg), frame, frame_size );

    attrs.attr.AllocatedAttributes = ALPC_MESSAGE_CONTEXT_ATTRIBUTE;
    attrs.attr.ValidAttributes = ALPC_MESSAGE_CONTEXT_ATTRIBUTE;
    attrs.ctx.PortContext = NULL;
    attrs.ctx.MessageContext = recv_ctx ? recv_ctx->MessageContext : NULL;
    attrs.ctx.Sequence = recv_ctx ? recv_ctx->Sequence : 0;
    attrs.ctx.MessageId = recv_ctx ? recv_ctx->MessageId : 0;
    attrs.ctx.CallbackId = recv_ctx ? recv_ctx->CallbackId : 0;

    return NtAlpcSendWaitReceivePort( provider.connection, ALPC_SEND_REPLY, msg,
                                      &attrs.attr, NULL, NULL, NULL, NULL );
}

/*
 * Answer the requests the registrar routes to the provider conversation
 * server (verb 2): joins, peer management and CoreUICall verb traffic.
 * Success replies carry result 0 and the trailing words the requesters
 * unmarshal.
 */
static void handle_routed_request( const void *payload, ULONG size, const ALPC_CONTEXT_ATTR *ctx )
{
    char datagram[REGISTRAR_MESSAGE_SIZE];
    const char *item = (const char *)payload + CN_ITEM_OFFSET;
    ULONG cmd = *(const ULONG *)((const char *)payload + 16);
    ULONG itemsize = *(const ULONG *)((const char *)payload + 32);
    ULONG verb, subid, off;
    ULONG result = 0, zero32 = 0;
    ULONGLONG zero64 = 0;

    if (size < CN_ITEM_OFFSET + 8 || cmd != 2) return;
    verb = *(const USHORT *)(item + 4);
    subid = *(const USHORT *)(item + 6);
    TRACE( "registrar routed request verb %u subid %u (item %u bytes).\n", verb, subid, itemsize );

    if (verb != 2) return;

    /* reply datagram: 80-byte queue header, then the RPC reply frame */
    memset( datagram, 0, 80 );
    off = FRAME_HEADER_SIZE;
    frame_init( datagram + 80, REGISTRAR_MESSAGE_SIZE - 80, 0, (USHORT)subid );
    off = frame_append_tlv( datagram + 80, off, &result, sizeof(result) );

    switch (subid)
    {
        case 17:   /* RegisterConversationClient (join) */
        case 18:   /* RegisterPendingConversationClientForServer */
        case 13:   /* FindCrossProcConnection */
        case 4:    /* CompleteCrossProcConnection */
        case 5:    /* CompleteLocalConnection */
        case 6:    /* CloseConnection */
        case 16:   /* ReleaseConversationClient */
            off = frame_append_tlv( datagram + 80, off, &zero32, sizeof(zero32) );
            off = frame_append_tlv( datagram + 80, off, &zero32, sizeof(zero32) );
            off = frame_append_tlv( datagram + 80, off, &zero64, sizeof(zero64) );
            break;
        default:
            break;
    }
    *(ULONG *)(datagram + 80) = off;   /* item frame total */

    *(ULONG *)(datagram + 16) = 0x30;
    *(ULONGLONG *)(datagram + 24) = CN_MAX_MSG;
    *(ULONG *)(datagram + 32) = provider.client_id;
    *(ULONG *)(datagram + 40) = 4;
    *(ULONG *)(datagram + 48) = 4;
    *(ULONG *)(datagram + 56) = off;   /* item size */
    *(ULONGLONG *)(datagram + 64) = 1;
    *(ULONGLONG *)(datagram + 72) = 0x10;

    registrar_reply( datagram, 80 + off, ctx );
}

static DWORD WINAPI provider_thread( void *param )
{
    static const WCHAR prefix[] = {'\\','B','a','s','e','N','a','m','e','d','O','b','j','e','c','t','s',
                                   '\\','C','o','r','e','M','e','s','s','a','g','i','n','g','R','e','g','i','s','t','r','a','r','-',0};
    WCHAR name[128];
    UNICODE_STRING string;
    ALPC_PORT_ATTRIBUTES port_attr;
    char connect_buf[sizeof(ALPC_PORT_MESSAGE) + 24];
    ALPC_PORT_MESSAGE *connect_msg = (void *)connect_buf;
    SIZE_T connect_size = sizeof(connect_buf);
    LARGE_INTEGER delay;
    NTSTATUS status;
    int i;

    lstrcpyW( name, prefix );
    wsprintfW( name + lstrlenW( name ), L"%u:00000000-0000-0000-0000-000000000000",
               GetCurrentProcessId() );
    RtlInitUnicodeString( &string, name );

    memset( &port_attr, 0, sizeof(port_attr) );
    port_attr.MaxMessageLength = REGISTRAR_MESSAGE_SIZE;
    port_attr.MemoryBandwidth = port_attr.MaxPoolUsage = port_attr.MaxMessageLength;
    port_attr.MaxViewSize = 0x100000;
    port_attr.MaxTotalSectionSize = 0x1000000;
    port_attr.DupObjectTypes = 0x2000000;

    memset( connect_buf, 0, sizeof(connect_buf) );
    connect_msg = (void *)connect_buf;
    connect_msg->DataLength = 24;
    connect_msg->TotalLength = 24 + sizeof(*connect_msg);
    /* 24-byte port context: engine GUID + zero context value */
    memcpy( connect_buf + sizeof(*connect_msg), &cn_engine_guid, sizeof(GUID) );

    /* The registrar is created by the CoreUI engine when the app
     * initialises its dispatcher infrastructure, after this DLL is
     * loaded; retry the connect until it appears. */
    for (i = 0; i < 400 && provider.running; i++)
    {
        connect_size = sizeof(connect_buf);
        status = NtAlpcConnectPort( &provider.connection, &string, NULL, &port_attr,
                                    ALPC_SYNC_REQUEST, NULL, connect_msg, &connect_size,
                                    NULL, NULL, NULL );
        if (!status) break;
        delay.QuadPart = -25 * 10000;  /* 25 ms */
        NtDelayExecution( FALSE, &delay );
    }
    if (status)
    {
        TRACE( "registrar %s not present, status %#lx.\n", debugstr_w( name ), status );
        return 0;
    }
    TRACE( "connected to registrar %s.\n", debugstr_w( name ) );

    if (registrar_register_thread() && registrar_register_conversation())
    {
        struct
        {
            ALPC_MESSAGE_ATTRIBUTES attr;
            ALPC_CONTEXT_ATTR ctx;
        } attrs;
        SIZE_T buffer_size;
        char buffer[REGISTRAR_MESSAGE_SIZE];

        while (provider.running)
        {
            ALPC_PORT_MESSAGE *msg = (void *)buffer;

            attrs.attr.AllocatedAttributes = ALPC_MESSAGE_CONTEXT_ATTRIBUTE;
            attrs.attr.ValidAttributes = 0;
            memset( &attrs.ctx, 0, sizeof(attrs.ctx) );
            buffer_size = sizeof(buffer);

            status = NtAlpcSendWaitReceivePort( provider.connection, 0, NULL, NULL,
                                                msg, &buffer_size, &attrs.attr, NULL );
            if (status)
            {
                TRACE( "provider receive loop done, status %#lx.\n", status );
                break;
            }
            if (buffer_size >= sizeof(*msg) + FRAME_HEADER_SIZE)
            {
                const void *payload = (const char *)msg + sizeof(*msg);
                ULONG payload_size = msg->DataLength;
                ALPC_CONTEXT_ATTR *ctx = NULL;

                if (attrs.attr.ValidAttributes & ALPC_MESSAGE_CONTEXT_ATTRIBUTE)
                    ctx = &attrs.ctx;
                handle_routed_request( payload, payload_size, ctx );
            }
        }
    }
    NtClose( provider.connection );
    provider.connection = NULL;
    return 0;
}

static BOOL CALLBACK provider_init_once_cb( INIT_ONCE *once, void *param, void **context )
{
    InitializeCriticalSection( &provider.send_cs );
    provider.running = 1;
    provider.thread = CreateThread( NULL, 0, provider_thread, NULL, 0, NULL );
    return TRUE;
}

void registrar_provider_start( void )
{
    InitOnceExecuteOnce( &provider_init_once, provider_init_once_cb, NULL, NULL );
}

BOOL WINAPI DllMain( HINSTANCE inst, DWORD reason, void *reserved )
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls( inst );
        /* The app loads CoreMessaging.dll before it initialises the CoreUI
         * conversation infrastructure; start the provider thread now so it
         * registers the conversations as soon as the engine's self-hosted
         * registrar port appears. */
        registrar_provider_start();
    }
    return TRUE;
}
