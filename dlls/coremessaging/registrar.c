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
 *   - registers the thread (registrar client id),
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
 * already-registered).  The engine's reply path (AlpcConnection::DoReply)
 * enforces the kernel's synchronous serialisation: it Abandonments when it
 * replies with more than one request buffer outstanding on the connection
 * (a reply-pending exchange of a posted-buffer conversation must be
 * answered before the next request is taken).  The provider therefore keeps
 * exactly one exchange in flight (send_cs) like the engine's own self-host
 * clients; the server's sync-request serialisation gate (server/alpc.c)
 * enforces the same contract for every client, so this is protocol
 * compliance, not a workaround.  The join reply contract
 * implemented here mirrors the native reply sender
 * (RegistrarServerCaller::RegisterConversationClient -> CoreUICallSend):
 * [result, clientId, ConversationInfo(16), {serverId, u32}, peers(56 each),
 * peerCount] with the ExportEndpoint notification handle riding the
 * reply's ALPC handle attribute (kernel-duplicated into the requester).
 * The peer entry's RoutingInfo in the join reply carries the requester's
 * own partition GUID (a zero GUID classifies the join cross-partition and
 * the engine fail-fasts building the reverse connection port for it).
 * Measured state (WhatsApp 2.2620): with this provider the app survives
 * the registrar conversation — tray dispatch, VoIP/WebLifecycle/push
 * subsystems run and the process lives past the 150 s gate.  XAML's own
 * type activation (Microsoft.UI.Xaml.Media.AcrylicBrush et al,
 * E_NOINTERFACE -> ReportParserError -> OriginateError) and the
 * window/content-root build remain the next gap.
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

#include "private.h"

WINE_DEFAULT_DEBUG_CHANNEL(messaging);

#define REGISTRAR_MESSAGE_SIZE  0x1000
#define FRAME_HEADER_SIZE       8
#define ALPC_SYNC_REQUEST       0x00020000  /* ALPC_MSGFLG_SYNC_REQUEST */
#define ALPC_SEND_REPLY         0x00010000  /* ALPC_MSGFLG_REPLY_MESSAGE */

/* Cn engine identifier: the GUID of the CoreMessaging implementation, the
 * value the engine's own clients carry in the connect port context (the
 * provider carries the trailing 8 bytes of it, the part the engine's
 * 64-bit connect message carries behind its kernel header). */
static const GUID cn_engine_guid =
    { 0x29324ee4, 0xdc59, 0x49dc, { 0xa2, 0x3f, 0x8c, 0xa1, 0x1b, 0xef, 0x42, 0x27 } };

/*
 * Self-host context marker in the connect message.  The engine's registrar
 * server echoes 0x20 at +8 of every connect reply payload (measured, it is
 * the server's own self-host/connect flag value); presenting the same value
 * in the connect port context marks the client as a self-host connection of
 * this engine.  Without it the client is classified cross-partition:
 * MessageSession::CreateConnection takes the
 * CreateCrossPartitionReverseConnectionPort branch, which fail-fasts on
 * Wine (Abandonment::Fail at CoreMessagingXP RVA 0xa399) because the
 * reverse-port machinery there depends on kernel behaviour Wine does not
 * emulate.  The provider is in-process, so it presents the marker.
 */
#define CN_SELFHOST_CONTEXT 0x20ull

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

    ULONG req_routing_guid[4] = {0};   /* requester's RoutingInfo partition GUID */
    struct tlv_reader rq;
    const void *pdata;
    ULONG psz;

    if (size < CN_ITEM_OFFSET + FRAME_HEADER_SIZE) return;
    if (itemsize > REGISTRAR_MESSAGE_SIZE - sizeof(ALPC_PORT_MESSAGE) - CN_ITEM_OFFSET)
        itemsize = REGISTRAR_MESSAGE_SIZE - sizeof(ALPC_PORT_MESSAGE) - CN_ITEM_OFFSET;
    verb = *(const USHORT *)(item + 4);
    subid = *(const USHORT *)(item + 6);
    TRACE( "registrar routed request verb %u subid %u (item %u bytes): %s\n",
           verb, subid, itemsize, debugstr_frame( item, itemsize ) );

    /* A join request carries the requester's RoutingInfo (24 bytes:
     * {scope kind, id, partition GUID}).  The provider serves the
     * conversation from the engine's own process, so the peers it reports
     * live in the requester's partition: echo that partition GUID into the
     * peer routing.  A zero GUID classifies the connection as
     * cross-partition and the engine fails fast building a reverse
     * connection port for it (MessageSession::CreateConnection). */
    rq.offset = FRAME_HEADER_SIZE;
    rq.base = item;
    while (rq.offset < itemsize && tlv_read( &rq, &pdata, &psz ))
    {
        if (psz == 24)
        {
            memcpy( req_routing_guid, (const char *)pdata + 8, sizeof(req_routing_guid) );
            break;
        }
    }

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
        {
            /* The conversation server is the in-process CoreUICore provider
             * object.  The requester treats the peer entry's server routing
             * and server id as the object pointer it binds to (self-host:
             * provider and requester share the process).  Stamping the
             * registrar client id there hands the requester an integer it
             * dereferences as a vtable (measured: XAML Application.Start
             * read [client-id] and died as an AccessViolationException). */
            void *object = core_ui_provider_object();
            client_id = InterlockedIncrement( &provider.next_client_id );
            server_block.server_id = (ULONGLONG)(UINT_PTR)object;
            server_block.extra = 0;
            memset( &peer, 0, sizeof(peer) );
            peer.routing[0] = 0;                  /* scope kind: local */
            peer.routing[1] = (ULONGLONG)(UINT_PTR)object; /* server routing = in-proc object */
            peer.routing[2] = req_routing_guid[0]; /* partition GUID: the requester's own */
            peer.routing[3] = req_routing_guid[1];
            peer.routing[4] = req_routing_guid[2];
            peer.routing[5] = req_routing_guid[3];
            peer.endpoint = 1;                    /* conversation server endpoint */
            peer.server_id = (ULONGLONG)(UINT_PTR)object;
            peer.client_id = provider.client_id;
            peer_count = 1;
            off = frame_append_tlv( datagram + CN_ITEM_OFFSET, off, &client_id, sizeof(client_id) );
            off = frame_append_tlv( datagram + CN_ITEM_OFFSET, off, info16, sizeof(info16) );
            off = frame_append_tlv( datagram + CN_ITEM_OFFSET, off, &server_block, sizeof(server_block) );
            off = frame_append_tlv( datagram + CN_ITEM_OFFSET, off, &peer, sizeof(peer) );
            off = frame_append_tlv( datagram + CN_ITEM_OFFSET, off, &peer_count, sizeof(peer_count) );
            reply_handle = provider.endpoint_event;
            break;
        }
        case 36:   /* CoreUICall verb relayed to the conversation server: the
                    * reply carries the CoreUICore provider object the requester
                    * binds to, as the third TLV of the bare reply stream
                    * (measured against the requester's unmarshalling walk).
                    * A bare [result] leaves the requester's object slot at its
                    * stale initial value, which it then dereferences as a
                    * vtable pointer; hand back the real provider object.
                    * Interfaces it does not implement answer E_NOINTERFACE
                    * from its own QueryInterface. */
        {
            void *object = core_ui_provider_object();
            ULONGLONG zero = 0, extra = 0;

            if (!object) { WARN( "no CoreUICore provider object.\n" ); break; }
            off = frame_append_tlv( datagram + CN_ITEM_OFFSET, off, &zero, sizeof(zero) );
            off = frame_append_tlv( datagram + CN_ITEM_OFFSET, off, &object, sizeof(object) );
            off = frame_append_tlv( datagram + CN_ITEM_OFFSET, off, &extra, sizeof(extra) );
            break;
        }
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
    /* 24-byte port context: the Cn engine GUID tail + self-host context
     * marker (see cn_engine_guid / CN_SELFHOST_CONTEXT).  This matches the
     * bytes the engine's own in-process clients' connect message carries
     * behind the kernel header. */
    memcpy( connect_buf + sizeof(*connect_msg), (const char *)&cn_engine_guid + 8, 8 );
    *(ULONGLONG *)(connect_buf + sizeof(*connect_msg) + 8) = CN_SELFHOST_CONTEXT;

    /* The registrar is created by the CoreUI engine when the app
     * initialises its dispatcher infrastructure, after this DLL is
     * loaded; poll for it to appear.  The port lookup of a not-yet-created
     * registrar is a pure miss on the server (it leaves no connection
     * record behind), so the connect cost is the polling itself: back off
     * from a short interval to a long one so the thread covers the engine's
     * variable startup window with a bounded number of attempts instead of
     * a fixed high-frequency spin. */
    {
        static const ULONG delays_ms[] = { 25, 50, 100, 250, 500 };
        ULONG attempt = 0;

        /* the ramped delays sum to ~925 ms; the trailing 500 ms polls
         * cover a further ~9 s, matching the old fixed 400x25 ms window
         * with a fraction of the connect traffic */
        while (provider.running && attempt < ARRAYSIZE(delays_ms) + 18)
        {
            connect_size = sizeof(connect_buf);
            status = NtAlpcConnectPort( &provider.connection, &string, NULL, &port_attr,
                                        ALPC_SYNC_REQUEST, NULL, connect_msg, &connect_size,
                                        NULL, NULL, NULL );
            if (!status) break;
            delay.QuadPart = -(LONGLONG)delays_ms[attempt < ARRAYSIZE(delays_ms) ? attempt
                                                                                   : ARRAYSIZE(delays_ms) - 1] * 10000;
            NtDelayExecution( FALSE, &delay );
            attempt++;
        }
    }
    if (status)
    {
        return 0;
    }

    /* Self-host: the CoreUI engine of the app registers the "CoreUICore"
     * conversation itself when its dispatcher infrastructure initialises, and
     * serves joins for it in-process.  A builtin registration of the same
     * name collides (RegisterConversation answers result 4 - already
     * registered - to the engine's own registration), so the provider only
     * announces its thread and serves what the registrar routes to it.
     * (The provider deliberately sends no second request while the engine
     * may still be replying to RegisterThread: the engine's AlpcConnection
     * reply path fail-fasts when it replies with more than one request
     * buffer outstanding, and the in-process self-host keeps exactly one
     * exchange in flight per connection.) */
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
