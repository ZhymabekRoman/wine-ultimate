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
 * Measured state (WhatsApp 2.2620, self-hosted registrar): the engine
 * self-hosts the registrar and the XAML side registers the "CoreUICore"
 * conversation itself (wire RegisterConversation, method 14) when its
 * dispatcher infrastructure initialises; it serves dispatcher-queue joins
 * for that conversation in-process.  A builtin registration of the same
 * name collides with the engine's own (the registrar answers it result 4,
 * already-registered), so the provider announces its thread only and
 * serves requests the registrar routes to it.  The join reply contract
 * implemented here mirrors the native reply sender
 * (RegistrarServerCaller::RegisterConversationClient -> CoreUICallSend):
 * [result, clientId, ConversationInfo(16), {serverId, u32}, peers(56 each),
 * peerCount] with the ExportEndpoint notification handle riding the
 * reply's ALPC handle attribute (kernel-duplicated into the requester).
 * Remaining gap after this point: XAML's own type activation
 * (Microsoft.UI.Xaml.Media.AcrylicBrush et al) answers E_NOINTERFACE,
 * which the XAML object writer turns into CErrorService::ReportParserError
 * notifications that OriginateError in Microsoft.ui.xaml.dll+0x7E4D83;
 * the window stays headless until that activation path resolves.
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
    volatile LONG next_client_id;           /* client ids handed out on joins */
    HANDLE endpoint_event;                  /* ExportEndpoint notification event */
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

static const char *debugstr_frame( const void *data, ULONG size )
{
    static char buffer[0x101];
    static const char hex[] = "0123456789abcdef";
    const unsigned char *p = data;
    ULONG i, n = size < 0x80 ? size : 0x80, o = 0;

    for (i = 0; i < n && o + 2 < sizeof(buffer); i++)
    {
        buffer[o++] = hex[p[i] >> 4];
        buffer[o++] = hex[p[i] & 15];
    }
    buffer[o] = 0;
    return buffer;
}

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
    if (reply_size >= sizeof(*reply_msg) + CN_ITEM_OFFSET + 8)
    {
        const char *p = (const char *)reply_msg + sizeof(*reply_msg);
        struct tlv_reader rr;
        const void *data;
        ULONG size;

        /* reply item at +40 is a bare TLV stream (no call-frame header):
         * RegisterThreadInfo {result, client-id pool base 0x40000, size} (12),
         * the client-id slot (4) and the queue max size (8). */
        rr.base = p + CN_ITEM_OFFSET;
        rr.offset = 0;
        if (tlv_read( &rr, &data, &size ) && size >= 12)   /* info block */
        {
            if (tlv_read( &rr, &data, &size ) && size >= 4)
            {
                provider.client_id = *(const ULONG *)data;
                TRACE( "registered with registrar, client id %u.\n", provider.client_id );
                return TRUE;
            }
        }
        WARN( "RegisterThread reply not in TLV form.\n" );
        return FALSE;
    }
    WARN( "RegisterThread short reply %u bytes.\n", reply_size );
    return FALSE;
}

/*
 * RegisterConversation: declare the "CoreUICore" conversation so the
 * registrar name lookup finds it when XAML dispatcher queues join.
 *
 * The self-hosted registrar server marshals the call frame strictly: the
 * receive host validates the TLV stream against the method signature
 * (RemoteRegistrarServerReceiveThunk::Thunk_RegisterConversation_164) and
 * answers any mismatch with an OnProtocolException notification carrying
 * protocol error 99 (MessageCallReceiveHost::NotifyProtocolViolation).  The
 * accepted signature is, in wire order:
 *      conversation name (UTF-16 string)
 *      server name       (UTF-16 string)
 *      ScopeID           16 bytes {kind, id, u64}   (kind 0 = local, id 0)
 *      ConversationInfo  16 bytes {0, itemOwner, serverStyle, connectionMode}
 *      RoutingInfo       24 bytes (must not be all-zero: RoutingInfo::Invalid)
 *      HIDENTITY         40 bytes (all-zero = local identity)
 * Validation (level 2, RegisteredObjectValidator): scope kind must be 0/1/2/3/5
 * (kind 0 with a non-zero id must equal the session id), the info fields must
 * be in range, and the routing must not be the all-zero Invalid value.
 */
static BOOL registrar_register_conversation( void )
{
    static const WCHAR conversation_name[] = {'C','o','r','e','U','I','C','o','r','e',0};
    static const WCHAR server_name[] = {'W','i','n','e','C','o','r','e','M','e','s','s','a','g','i','n','g',0};
    ULONG reply_size = REGISTRAR_MESSAGE_SIZE;
    char buffer[sizeof(ALPC_PORT_MESSAGE) + 512], reply[REGISTRAR_MESSAGE_SIZE];
    char *frame = buffer + sizeof(ALPC_PORT_MESSAGE);
    ALPC_PORT_MESSAGE *reply_msg = (void *)reply;
    ULONG off;
    ULONG scope[4] = {0};               /* ScopeID: kind 0 = local process, id 0 */
    ULONG info[4] = {0};                /* ConversationInfo: core-owned, standard style, lazy */
    ULONG routing[6] = {0};             /* RoutingInfo: client id of this registration */
    BYTE identity[40] = {0};            /* HIDENTITY: local, empty */
    NTSTATUS status;
    char *item;

    routing[1] = provider.client_id;

    /* item: CoreUICall call frame verb 2 subid 14 */
    item = frame + CN_ITEM_OFFSET;
    frame_init( item, 0, 2, 14 );
    off = FRAME_HEADER_SIZE;
    off = frame_append_string( item, off, conversation_name );
    off = frame_append_string( item, off, server_name );
    off = frame_append_tlv( item, off, scope, sizeof(scope) );
    off = frame_append_tlv( item, off, info, sizeof(info) );
    off = frame_append_tlv( item, off, routing, sizeof(routing) );
    off = frame_append_tlv( item, off, identity, sizeof(identity) );
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
    if (reply_size >= sizeof(*reply_msg) + CN_ITEM_OFFSET + 8)
    {
        const char *p = (const char *)reply_msg + sizeof(*reply_msg);
        struct tlv_reader rr;
        const void *data;
        ULONG size;
        ULONG result;

        /* reply item at +40 is a bare TLV stream: [result, conversation id,
         * server id (u64)] (Reply_RegisterConversation). */
        rr.base = p + CN_ITEM_OFFSET;
        rr.offset = 0;
        if (tlv_read( &rr, &data, &size ) && size >= 4)
        {
            result = *(const ULONG *)data;
            if (result)
            {
                WARN( "RegisterConversation rejected, result %#x.\n", result );
                return FALSE;
            }
            if (tlv_read( &rr, &data, &size ) && size >= 4)
                provider.conversation_id = *(const ULONG *)data;
            TRACE( "registered conversation, id %u.\n", provider.conversation_id );
            return TRUE;
        }
        WARN( "RegisterConversation reply not in TLV form.\n" );
        return FALSE;
    }
    WARN( "RegisterConversation short reply %u bytes.\n", reply_size );
    return FALSE;
}

/*
 * Reply to a routed request.  The context attributes echo the receive
 * message context so the kernel correlates the reply.  When 'handle' is
 * non-zero a handle attribute carrying it rides on the reply: the ALPC
 * layer duplicates the conversation endpoint port into the requester,
 * which is how the CoreUICall endpoint (ExportEndpoint) is handed to
 * dispatcher queues joining the conversation.
 */
static NTSTATUS registrar_reply( const void *frame, ULONG frame_size,
                                 const ALPC_CONTEXT_ATTR *recv_ctx, HANDLE handle )
{
    struct
    {
        ALPC_MESSAGE_ATTRIBUTES attr;
        ALPC_CONTEXT_ATTR ctx;
        ALPC_HANDLE_ATTR handle_attr;
    } attrs;
    char buffer[REGISTRAR_MESSAGE_SIZE];
    ALPC_PORT_MESSAGE *msg = (void *)buffer;

    memset( msg, 0, sizeof(*msg) );
    msg->DataLength = frame_size;
    msg->TotalLength = sizeof(*msg) + frame_size;
    memcpy( (char *)msg + sizeof(*msg), frame, frame_size );

    attrs.attr.AllocatedAttributes = ALPC_MESSAGE_CONTEXT_ATTRIBUTE
                                     | (handle ? ALPC_MESSAGE_HANDLE_ATTRIBUTE : 0);
    attrs.attr.ValidAttributes = attrs.attr.AllocatedAttributes;
    attrs.ctx.PortContext = NULL;
    attrs.ctx.MessageContext = recv_ctx ? recv_ctx->MessageContext : NULL;
    attrs.ctx.Sequence = recv_ctx ? recv_ctx->Sequence : 0;
    attrs.ctx.MessageId = recv_ctx ? recv_ctx->MessageId : 0;
    attrs.ctx.CallbackId = recv_ctx ? recv_ctx->CallbackId : 0;
    attrs.handle_attr.Flags = 0;
    attrs.handle_attr.Handle = handle;
    attrs.handle_attr.ObjectType = 0;
    attrs.handle_attr.DesiredAccess = EVENT_ALL_ACCESS;

    return NtAlpcSendWaitReceivePort( provider.connection, ALPC_SEND_REPLY, msg,
                                      &attrs.attr, NULL, NULL, NULL, NULL );
}

/*
 * Answer the requests the registrar routes to the provider conversation
 * server (frame verb 2): joins (RegisterConversationClient 17,
 * RegisterPendingConversationClientForServer 18), peer management
 * (ClientConversationAttached 0, FindCrossProcConnection 13,
 * CompleteCrossProcConnection 4, CompleteLocalConnection 5, CloseConnection
 * 6, ReleaseConversationClient 16) and CoreUICall verb traffic.
 *
 * The reply is a queue datagram with the same 40-byte header as the request
 * and a call frame of verb 0 (reply) dispatched by ALPC message-id
 * correlation.  The TLV stream must match the requester's unmarshalling
 * signature exactly (extra or missing parameters => protocol violation 99):
 *   join 17/18 -> Reply_RegisterConversationClient:
 *                  [result, conversationId, serverId(u64), peer count,
 *                   peer array, ConversationInfo(16)]
 *   all others -> [result]
 * Notifications (frame verb 0: OnProtocolException, conversation change
 * broadcasts; also fire-and-forget AttachPeer-family verbs 11/31) carry no
 * pending request and must not be answered.
 */
/*
 * Peer entry (ConversationPeerInfo, 56 bytes) the joining client routes
 * conversation traffic by: {RoutingInfo (24), endpoint id (u32), pad,
 * server id (u64), scope (u64), registrar client id (u32), pad}.
 */
struct peer_info
{
    ULONG routing[6];
    ULONG endpoint;
    ULONG pad0;
    ULONGLONG server_id;
    ULONGLONG scope;
    ULONG client_id;
    ULONG pad1;
};

/*
 * Answer the requests the registrar routes to the provider conversation
 * server (frame verb 2): joins (RegisterConversationClient 17,
 * RegisterPendingConversationClientForServer 18), peer management
 * (ClientConversationAttached 0, CompleteCrossProcConnection 4,
 * CompleteLocalConnection 5, CloseConnection 6, ReleaseConversationClient
 * 16, FindCrossProcConnection 13) and the dispatcher-queue connection
 * verbs (ConnectionAccepted 11, NotifyLocalConnectionCreated 31).
 *
 * The reply is a queue datagram with the same 40-byte header as the
 * request and a bare TLV stream dispatched by ALPC message-id
 * correlation.  The TLV stream must match the requester's unmarshalling
 * signature exactly (extra or missing parameters => protocol violation
 * 99).  For the join, the native reply sender
 * (RegistrarServerCaller::RegisterConversationClient -> CoreUICallSend)
 * emits, in wire order:
 *      result (u32)
 *      assigned client id (u32)
 *      ConversationInfo (16)
 *      server block (12: {serverId u64, u32})
 *      peers array (count * 56, ConversationPeerInfo)
 *      peer count (u32)
 * and the ExportEndpoint handle rides the reply message's ALPC handle
 * attribute (kernel-duplicated into the requester).
 * Every other verb answers with the bare [result].
 */
static void handle_routed_request( const void *payload, ULONG size, const ALPC_CONTEXT_ATTR *ctx )
{
    char datagram[REGISTRAR_MESSAGE_SIZE];
    const char *item = (const char *)payload + CN_ITEM_OFFSET;
    ULONG itemsize = *(const ULONG *)((const char *)payload + 32);
    ULONG verb, subid, off;
    ULONG result = 0;
    ULONG client_id = 0, peer_count = 0;
    ULONG info16[4] = {0};
    struct
    {
        ULONGLONG server_id;
        ULONG extra;
    } server_block;
    struct peer_info peer;
    HANDLE reply_handle = NULL;

    if (size < CN_ITEM_OFFSET + FRAME_HEADER_SIZE) return;
    verb = *(const USHORT *)(item + 4);
    subid = *(const USHORT *)(item + 6);
    TRACE( "registrar routed request verb %u subid %u (item %u bytes): %s\n",
           verb, subid, itemsize, debugstr_frame( item, itemsize ) );

    /* verb 2 = request (correlated reply required); verb 0 = notification;
     * verb 3 = reply to our own outstanding request (handled by the sender) */
    if (verb == 0)
    {
        if (subid == 0)
        {
            struct tlv_reader rr = { item + FRAME_HEADER_SIZE, 0 };
            const void *data;
            ULONG sz;

            if (tlv_read( &rr, &data, &sz ) && sz >= 4)
                WARN( "registrar protocol exception %#x.\n", *(const ULONG *)data );
        }
        return;
    }
    if (verb != 2) return;

    /* reply datagram: 40-byte queue header, then the bare TLV stream */
    memset( datagram, 0, CN_ITEM_OFFSET );
    off = 0;
    off = frame_append_tlv( datagram + CN_ITEM_OFFSET, off, &result, sizeof(result) );

    switch (subid)
    {
        case 17:   /* RegisterConversationClient (join) */
        case 18:   /* RegisterPendingConversationClientForServer */
            client_id = InterlockedIncrement( &provider.next_client_id );
            server_block.server_id = provider.conversation_id;
            server_block.extra = 0;
            memset( &peer, 0, sizeof(peer) );
            peer.routing[0] = 0;                  /* scope kind: local */
            peer.routing[1] = provider.client_id; /* server routing = this registration */
            peer.endpoint = 1;                    /* conversation server endpoint */
            peer.server_id = provider.conversation_id;
            peer.client_id = provider.client_id;
            peer_count = 1;
            off = frame_append_tlv( datagram + CN_ITEM_OFFSET, off, &client_id, sizeof(client_id) );
            off = frame_append_tlv( datagram + CN_ITEM_OFFSET, off, info16, sizeof(info16) );
            off = frame_append_tlv( datagram + CN_ITEM_OFFSET, off, &server_block, sizeof(server_block) );
            off = frame_append_tlv( datagram + CN_ITEM_OFFSET, off, &peer, sizeof(peer) );
            off = frame_append_tlv( datagram + CN_ITEM_OFFSET, off, &peer_count, sizeof(peer_count) );
            reply_handle = provider.endpoint_event;
            break;
        default:
            break;
    }
    *(ULONG *)(datagram + 16) = 0;                     /* command: reply */
    *(ULONGLONG *)(datagram + 24) = CN_MAX_MSG;
    *(ULONG *)(datagram + 32) = off;                   /* item size */

    registrar_reply( datagram, CN_ITEM_OFFSET + off, ctx, reply_handle );
}

static DWORD WINAPI provider_thread( void *param )
{
    static const WCHAR prefix[] = {'\\','B','a','s','e','N','a','m','e','d','O','b','j','e','c','t','s',
                                   '\\','C','o','r','e','M','e','s','s','a','g','i','n','g','R','e','g','i','s','t','r','a','r','-',0};
    WCHAR name[128];
    UNICODE_STRING string;
    ALPC_PORT_ATTRIBUTES port_attr;
    char connect_buf[sizeof(ALPC_PORT_MESSAGE) + 160];
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

    /* Self-host: the CoreUI engine of the app registers the "CoreUICore"
     * conversation itself when its dispatcher infrastructure initialises, and
     * serves joins for it in-process.  A builtin registration of the same
     * name collides (RegisterConversation answers result 4 - already
     * registered - to the engine's own registration) and XAML fail-fasts, so
     * the provider only announces its thread and serves what the registrar
     * routes to it. */
    if (registrar_register_thread())
    {
        struct
        {
            ALPC_MESSAGE_ATTRIBUTES attr;
            ALPC_CONTEXT_ATTR ctx;
            ALPC_HANDLE_ATTR handle;
        } attrs;
        SIZE_T buffer_size;
        char buffer[REGISTRAR_MESSAGE_SIZE];

        /* ExportEndpoint: the notification event exported (kernel-duplicated)
         * to joining dispatcher queues so provider traffic can wake them */
        provider.endpoint_event = CreateEventW( NULL, FALSE, FALSE, NULL );

        while (provider.running)
        {
            ALPC_PORT_MESSAGE *msg = (void *)buffer;

            attrs.attr.AllocatedAttributes = ALPC_MESSAGE_CONTEXT_ATTRIBUTE
                                             | ALPC_MESSAGE_HANDLE_ATTRIBUTE;
            attrs.attr.ValidAttributes = 0;
            memset( &attrs.ctx, 0, sizeof(attrs.ctx) );
            memset( &attrs.handle, 0, sizeof(attrs.handle) );
            buffer_size = sizeof(buffer);

            status = NtAlpcSendWaitReceivePort( provider.connection, 0, NULL, NULL,
                                                msg, &buffer_size, &attrs.attr, NULL );
            if (status)
            {
                TRACE( "provider receive loop done, status %#lx.\n", status );
                break;
            }
            if (attrs.attr.ValidAttributes & ALPC_MESSAGE_HANDLE_ATTRIBUTE)
                TRACE( "provider received handle attribute: handle %p action %u.\n",
                       attrs.handle.Handle, attrs.handle.Flags );
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
