/*
 * Server-side pipe management
 *
 * Copyright (C) 1998 Alexandre Julliard
 * Copyright (C) 2001 Mike McCormack
 * Copyright 2016 Jacek Caban for CodeWeavers
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
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

#include "ntstatus.h"
#include "windef.h"
#include "winternl.h"
#include "winioctl.h"
#include "wine/debug.h"

#include "file.h"
#include "handle.h"
#include "thread.h"
#include "request.h"
#include "unicode.h"
#include "security.h"
#include "process.h"

struct named_pipe;

struct pipe_message
{
    struct list          entry;      /* entry in message queue */
    data_size_t          read_pos;   /* already read bytes */
    struct iosb         *iosb;       /* message iosb */
    struct async        *async;      /* async of pending write */
    struct list         *relay;      /* WA_MOJO_RELAY handle boxes, NULL when none */
};

struct relay_box
{
    struct list          entry;      /* entry in the message relay list */
    data_size_t          offset;     /* position of the in-band u64 handle slot in the message */
    unsigned long long   src;        /* handle value as written, in the writer process (full u64:
                                      * Windows handles are 64-bit, and the ipcz slots of the types
                                      * carrying ProxyIds/RouterIds (22/34/36) are 64-bit keys) */
    unsigned int         access;     /* access on the source handle (DUPLICATE_SAME_ACCESS) */
    struct process      *writer;     /* writer process (grabbed) */
    struct object       *obj;        /* source object (grabbed), resolved at write time */
    obj_handle_t         dst;        /* handle duplicated into the reader, 0 = not yet */
    struct process      *reader;     /* process dst belongs to, NULL while not duplicated */
    unsigned short       type;       /* relay_ops message type (relay_free keeps the source open
                                      * for the buffer types both brokers keep using) */
    struct pipe_end     *end;        /* writer end this box was scanned on (gate95: ring census
                                      * keys the S/T/D snapshots by pipe-end identity) */
};

/* An instance caches, for both of the pipe ends it connected, the peer ids that
 * FSCTL_PIPE_GET_CONNECTION_ATTRIBUTE and FSCTL_PIPE_IMPERSONATE report: the client pid
 * and session, the server pid and session, and the client process (for the token).
 * Windows keeps that cache queryable when the connection dies from a handle close on
 * either end, and drops it only when the server explicitly calls DisconnectNamedPipe().
 */
enum conn_info_state
{
    CONN_INFO_NONE,     /* never connected */
    CONN_INFO_VALID,    /* connected, or torn down by a handle close */
    CONN_INFO_CLEARED   /* dropped by an explicit DisconnectNamedPipe() */
};

struct pipe_end
{
    struct object        obj;        /* object header */
    struct fd           *fd;         /* pipe file descriptor */
    unsigned int         flags;      /* pipe flags */
    unsigned int         state;      /* pipe state */
    struct named_pipe   *pipe;
    struct pipe_end     *connection; /* the other end of the pipe */
    process_id_t         client_pid; /* process that created the client */
    process_id_t         server_pid; /* process that created the server */
    unsigned int         client_session_id; /* session of the client process */
    unsigned int         server_session_id; /* session of the server process */
    struct process      *client_proc; /* client process, used for impersonation */
    enum conn_info_state conn_info;  /* state of the cached peer ids above */
    data_size_t          buffer_size;/* size of buffered data that doesn't block caller */
    struct list          message_queue;
    struct async_queue   read_q;     /* read queue */
    struct async_queue   write_q;    /* write queue */
    int                  cont_armed; /* duplex: a message-mode read completed short,
                                      * the head message is not fully consumed and the
                                      * continuation read (next REQ_read from this
                                      * client) is pending; keeps the mirror fd's POLLIN
                                      * armed across the header/body split so relayed
                                      * edges drain the remainder without a new peer
                                      * write edge */
    int                  blk_fd;     /* WA_MOJO_RELAY: dup'd unix fd of the latest AddBlockBuffer (ipcz id 14) block mapping, -1 = none */
    int                  ring_fd;    /* gate95: dup'd unix fd of *this end's* NodeLinkMemory buffer relayed in the
                                      * type-7 ConnectFromBrokerToBroker (the writer's ring block; the peer maps the
                                      * same memory through the relayed dup).  Snapshotted on every relayed frame
                                      * as WA_RELAY: R to census ring head/tail growth. -1 = none */
    struct timeout_user *hb_timeout; /* gate104 lever (b): duplex mirror heartbeat (see mirror_heartbeat) */
    struct object       *ev_obj;     /* gate102: the DriverTransport notification Event this end adopted in its own
                                      * type-7 ConnectFromBrokerToBroker (slot 1 = the event the *peer* signals after
                                      * pushing fragments into its ring; slot 2 = the NodeLinkMemory Section).  The
                                      * relay keeps a ref here so that a W-plane delivery into this end can pulse it
                                      * (WA_RELAY: N) — the "ring has data" edge.  NULL = no type-7 adopted here yet */
};

struct pipe_server
{
    struct pipe_end      pipe_end;   /* common header for both pipe ends */
    struct list          entry;      /* entry in named pipe listeners list */
    unsigned int         options;    /* pipe options */
    struct async_queue   listen_q;   /* listen queue */
};

struct named_pipe
{
    struct object       obj;         /* object header */
    int                 message_mode;
    int                 message_read_mode; /* default read mode of connected clients */
    unsigned int        sharing;
    unsigned int        maxinstances;
    unsigned int        outsize;
    unsigned int        insize;
    unsigned int        instances;
    timeout_t           timeout;
    struct list         listeners;   /* list of servers listening on this pipe */
    struct async_queue  waiters;     /* list of clients waiting to connect */
};

struct named_pipe_device
{
    struct object       obj;         /* object header */
    struct namespace   *pipes;       /* named pipe namespace */
};

struct named_pipe_device_file
{
    struct object             obj;         /* object header */
    struct fd                *fd;          /* pseudo-fd for ioctls */
    struct named_pipe_device *device;      /* named pipe device */
};

/* WA_PIPEX: opt-in message-mode payload capture for wire-format analysis.
 * Enabled with WA_PIPEX_DUMP=1 in the wineserver environment; only pipes whose
 * name contains WA_PIPEX_FILTER (default "mojo.") are dumped, so production
 * gates stay untouched.  Each captured event is one line on stderr:
 *   WA_PIPEX: W|R|P pid=<winpid> seq=<n> end=<ptr> len=<n> name=<ascii> hex=<bytes>
 * W = bytes written into the peer's queue, R = bytes delivered to a reader,
 * P = bytes peeked.  Hex is capped at ~7.9 KB per event. */
static int wapipex_on = -1;
static const char *wapipex_filter = "mojo.";
static WCHAR wapipex_wfilter[64];
static size_t wapipex_wfilter_len;
static unsigned int wapipex_seq;

static int wapipex_wanted( struct pipe_end *pipe_end )
{
    const WCHAR *hay, *p;
    unsigned int nchars, i;

    if (wapipex_on < 0)
    {
        const char *e = getenv( "WA_PIPEX_FILTER" );
        wapipex_on = !!getenv( "WA_PIPEX_DUMP" );
        if (e) wapipex_filter = e;
        for (i = 0; wapipex_filter[i] && i < sizeof(wapipex_wfilter) / sizeof(WCHAR) - 1; i++)
            wapipex_wfilter[i] = (WCHAR)(unsigned char)wapipex_filter[i];
        wapipex_wfilter[i] = 0;
        wapipex_wfilter_len = i;
    }
    if (!wapipex_on || !wapipex_wfilter_len) return 0;
    if (!pipe_end || !pipe_end->pipe || !pipe_end->pipe->obj.name) return 0;
    hay = pipe_end->pipe->obj.name->name;
    nchars = pipe_end->pipe->obj.name->len / sizeof(WCHAR);
    if (nchars < wapipex_wfilter_len) return 0;
    for (p = hay; p + wapipex_wfilter_len <= hay + nchars; p++)
        if (!memcmp( p, wapipex_wfilter, wapipex_wfilter_len * sizeof(WCHAR) )) return 1;
    return 0;
}

static void wapipex_dump( const char *kind, struct pipe_end *end, process_id_t pid,
                          const void *data, data_size_t len )
{
    static const char hexd[] = "0123456789abcdef";
    static char line[16384];
    const WCHAR *nname;
    unsigned int nchars;
    char *p, *endp;
    const unsigned char *b = data;
    unsigned int i;

    if (!wapipex_wanted( end )) return;
    nname = end->pipe->obj.name->name;
    nchars = end->pipe->obj.name->len / sizeof(WCHAR);
    p = line + snprintf( line, sizeof(line), "WA_PIPEX: %s pid=%04x seq=%u end=%p len=%u name=",
                         kind, pid, ++wapipex_seq, end, len );
    endp = line + sizeof(line) - 4;
    for (i = 0; i < nchars && p < line + 600; i++)
        *p++ = nname[i] < 128 ? (char)nname[i] : '?';
    *p++ = ' ';
    memcpy( p, "hex=", 4 ); p += 4;
    for (i = 0; i < len && p < endp; i++)
    {
        *p++ = hexd[b[i] >> 4];
        *p++ = hexd[b[i] & 0x0f];
    }
    *p++ = '\n';
    *p = 0;
    fputs( line, stderr );
}

/* WA_MOJO_RELAY: delivery-time relay of in-band handles on mojo invitation pipes.
 *
 * The app<->browser bootstrap of WebView2 runs over message-mode named pipes
 * \\.\pipe\LOCAL\mojo.<pid>.<tid>.<rand>.  ipcz/mojo frames ride the pipe as a
 * 16-byte prologue [u32 magic 0x10, u32 frame_len, u64 ts] + a 24-byte mojo
 * header [u16 header_size 24, u16 type, u16 version, u16 pad, u64 seq, u64
 * version_info] + body; the in-band handle fields serialize the raw process-local
 * HANDLE of the writer (Chromium SerializeObject), and the reader's
 * DeserializeObject expects to use it in its own process.  On Windows the
 * writer's SDK performs DuplicateHandle(writer -> peer, opts=3
 * SAME_ACCESS|CLOSE_SOURCE) before writing; Wine has no such leg on this path,
 * so the peer receives a writer-local value it cannot resolve: the
 * GetBufferResponse shared-buffer handle never maps, the SharedBufferFeature
 * consumer stalls silently and CreateCoreWebView2CompositionController never
 * answers.
 *
 * With WA_MOJO_RELAY=1 (kill-switch WINE_MOJO_RELAY=0) the server supplies the
 * missing leg on these pipes only: on write it walks the frames of the queued
 * payload and resolves the handle slots of the op table below in the writer
 * process; at READ delivery it allocates a handle for the object in the reader
 * process and patches the delivered payload copy (queue iosb data, read buffer,
 * peek buffer - server-side copies only) with that value; once the message is
 * fully consumed it closes the writer's source handle, mirroring the observed
 * opts=3 CLOSE_SOURCE semantics (WA_MOJO_RELAY_NOCLOSE=1 keeps the source open).
 * Env-off runs are byte-identical to before.
 */
static int relay_on = -1;
static int relay_no_close;

/* WA_DUPX_DUMP=1 (gate env): instance-lifecycle census for the self-apartment
 * LRPC duplex pipes (name contains "OLE_").  Logs who creates an instance, who
 * listens, and which listener a client binds to at open, so the pairing of the
 * client handle against the instance the server services is observable. */
static int dupx_on = -1;

static int dupx_wanted( struct named_pipe *pipe )
{
    const WCHAR *hay, *p;
    unsigned int nchars;
    static const WCHAR oW[] = {'O','L','E','_'};
    static const WCHAR lW[] = {'l','r','p','c'};

    if (dupx_on < 0) dupx_on = !!getenv( "WA_DUPX_DUMP" );
    if (!dupx_on || !pipe || !pipe->obj.name) return 0;
    hay = pipe->obj.name->name;
    nchars = pipe->obj.name->len / sizeof(WCHAR);
    if (nchars < 4) return 0;
    for (p = hay; p + 4 <= hay + nchars; p++)
    {
        if (!memcmp( p, oW, 4 * sizeof(WCHAR) )) return 1;
        if (!memcmp( p, lW, 4 * sizeof(WCHAR) )) return 1;  /* pipe\lrpc\* connections */
    }
    return 0;
}

static void dupx_dump( const char *ev, struct named_pipe *pipe, struct pipe_end *server,
                       struct pipe_end *client )
{
    struct list *e;
    char tailname[24] = "";
    const WCHAR *nm;
    unsigned int nchars;

    if (!dupx_wanted( pipe )) return;
    nm = pipe->obj.name->name;
    nchars = pipe->obj.name->len / sizeof(WCHAR);
    for (unsigned int i = 0; i < 12 && nchars > i; i++)
        tailname[i] = (char)(nm[nchars - 1 - i] < 128 ? nm[nchars - 1 - i] : '?');
    fprintf( stderr, "WA_DUPX: %s pipe=%p name..%s\n", ev, pipe, tailname );
    for (e = pipe->listeners.next; e != &pipe->listeners; e = e->next)
    {
        struct pipe_server *s = LIST_ENTRY( e, struct pipe_server, entry );
        fprintf( stderr, "WA_DUPX:   listener pe=%p fd=%p state=%u conn=%p\n",
                 &s->pipe_end, s->pipe_end.fd, s->pipe_end.state, s->pipe_end.connection );
    }
    if (server)
        fprintf( stderr, "WA_DUPX:   %s srv_pe=%p srv_fd=%p srv_conn=%p cli_pe=%p cli_fd=%p cli_conn=%p\n",
                 ev, server, server->fd, server->connection, client,
                 client ? client->fd : NULL, client ? client->connection : NULL );
}

static void dupx_io( const char *dir, struct pipe_end *end, data_size_t len )
{
    if (!dupx_wanted( end->pipe )) return;
    fprintf( stderr, "WA_DUPX: %s fd=%p peer=%p len=%u\n", dir, end->fd,
             end->connection ? end->connection->fd : NULL, len );
}

/* wv2io: one WINEDEBUG channel for the WebView2 transceive wall (pre-instrument for
 * the BORN epoch).  Arm with WINEDEBUG=+wv2io in the wineserver environment; every
 * traced leg prints one compact line, tag-first:
 *   WA_WV2: t=<sec>.<ms> S pid=<winpid> ep=<ptr> st=<L|I|C|G|D>      pipe state edge
 *   WA_WV2: t=... X srv=<pid> cli=<pid> ep=<ptr>                     accept pairing (AcceptEx)
 *   WA_WV2: t=... T pid=<pid> ep=<ptr> len=<n>                       Transceive armed
 *   WA_WV2: t=... G pid=<pid> ckey=<hex> ov=<ptr> st=<ntst> inf=<n>  GetQueuedCompletionStatus
 *   WA_WV2: t=... P pid=<pid> ->=<pid> seq=<n> len=<n> r=<0|1>       ALPC send queued (PostSend)
 *   WA_WV2: t=... A pid=<pid> conn=<pid>                             ALPC accept
 * Pipe legs fire for LOCAL\mojo.* pipes only (the ipcz legs Root<->msedgewebview2),
 * matching the relay_wanted filter.  Disabled cost: one cached getenv per process. */
static int wv2io_on( void )
{
    static int on = -1;
    if (on < 0)
    {
        const char *dbg = getenv( "WINEDEBUG" );
        on = (dbg && strstr( dbg, "wv2io" )) ? 1 : 0;
    }
    return on;
}

static void wv2io_print( const char *fmt, ... )
{
    struct timespec ts;
    va_list args;

    clock_gettime( CLOCK_MONOTONIC, &ts );
    fprintf( stderr, "WA_WV2: t=%u.%03u ", (unsigned int)ts.tv_sec,
             (unsigned int)(ts.tv_nsec / 1000000) );
    va_start( args, fmt );
    vfprintf( stderr, fmt, args );
    va_end( args );
}

static void wv2io_tail( struct named_pipe *pipe, char *buf, unsigned int size )
{
    const WCHAR *nm;
    unsigned int nchars, i;

    buf[0] = '\0';
    if (!pipe || !pipe->obj.name) return;
    nm = pipe->obj.name->name;
    nchars = pipe->obj.name->len / sizeof(WCHAR);
    for (i = 0; i < size - 1 && i < nchars; i++)
        buf[i] = (char)(nm[nchars - 1 - i] < 128 ? nm[nchars - 1 - i] : '?');
    buf[i] = '\0';
}

#define WV2IO(...) do { if (wv2io_on()) wv2io_print( __VA_ARGS__ ); } while (0)

static int wv2io_wanted( struct pipe_end *pipe_end )
{
    static const WCHAR prefixW[] = {'L','O','C','A','L','\\','m','o','j','o','.'};

    if (!wv2io_on()) return 0;
    if (!pipe_end || !pipe_end->pipe || !pipe_end->pipe->obj.name) return 0;
    if (pipe_end->pipe->obj.name->len < sizeof(prefixW)) return 0;
    return !memicmp_strW( pipe_end->pipe->obj.name->name, prefixW, sizeof(prefixW) );
}

/* map FILE_PIPE_*_STATE to one char: L istening, I nconnecting, C onnected,
 * G (closin)G, D isconnected */
static char wv2io_state_ch( unsigned int state )
{
    switch (state)
    {
    case FILE_PIPE_LISTENING_STATE:    return 'L';
    case 1 /* FILE_PIPE_CONNECTING_STATE */: return 'I';
    case FILE_PIPE_CONNECTED_STATE:    return 'C';
    case FILE_PIPE_CLOSING_STATE:      return 'G';
    default:                           return 'D';
    }
}

static int relay_wanted( struct pipe_end *pipe_end )
{
    static const WCHAR prefixW[] = {'L','O','C','A','L','\\','m','o','j','o','.'};

    if (relay_on < 0)
    {
        const char *kill = getenv( "WINE_MOJO_RELAY" );
        relay_no_close = !!getenv( "WA_MOJO_RELAY_NOCLOSE" );
        relay_on = (kill && !strcmp( kill, "0" )) ? 0 : !!getenv( "WA_MOJO_RELAY" );
    }
    if (!relay_on) return 0;
    if (!pipe_end || !pipe_end->pipe || !pipe_end->pipe->obj.name) return 0;
    if (pipe_end->pipe->obj.name->len < sizeof(prefixW)) return 0;
    return !memicmp_strW( pipe_end->pipe->obj.name->name, prefixW, sizeof(prefixW) );
}

/* In-band handle slots per ipcz message type (body-relative offsets, -1 = none);
 * canonical names/ids from the pinned Chromium 136.0.7103.0 reference
 * (third_party/ipcz/src/ipcz/node_messages_generator.h, flat copies in /tmp/webres):
 * ids 13/14/20/21/22/32/33/34/35/36 are NodeMessages; the slot offsets were verified
 * against the g35/g38 WA_PIPEX captures of the app<->browser bootstrap and the
 * gate69i capture (type 7 v0 params = StructHeader+NodeName+proto+portals + an
 * embedded serialized DriverTransport::Message at body+40: u32 num_bytes (=0x48,
 * coincidentally a live handle in the writer table — gate71 relayed *this* word and
 * clobbered {num_bytes,version} with the dup'd dst, so the reader deserialized a
 * 1380-byte driver message out of a 184-byte frame and the NodeLink tore down) +
 * u32 version + payload + the handle u64 at body+72 (frame 112 = 0x68 = the
 * writer's NodeLinkMemory primary block-buffer FILE_MAP, identical word in both
 * directions in the 184-byte gate69i/gate71 frames; type 14 driver object at body+16; AcceptParcel driver_objects arrays
 * relayed at frame off=48/56).  m136 NodeMessages has no id 38; the legacy name is
 * kept there for the observed wire traffic. */
static const struct relay_op
{
    unsigned short type;
    short slot1, slot2;
} relay_ops[] =
{
    {  7, 72, 104 },  /* ConnectFromBrokerToBroker { buffer: driver_object } — the bidirectional
                      * NodeLink handshake on every mojo pipe (184/200-byte frames, gate69i
                      * seq=3/4 both directions).  The driver_object is an embedded serialized
                      * DriverTransport::Message: {u32 num_bytes=0x48, u32 version} at body+40,
                      * its payload, then the HANDLE at body+72 (frame 112, value 0x68 in every
                      * gate69i/gate71 capture = the writer's NodeLinkMemory primary block
                      * buffer FILE_MAP).  Relay the handle word; leave the header word alone.
                      * gate73: relaying the header word too ({7,72,40}) changes nothing (same
                      * 4 handshake lines / 105 op=20) — one handle per type-7 v0, proven by
                      * node_messages_generator.h (IPCZ_MSG_PARAM_DRIVER_OBJECT(buffer) x1). */
    { 13,  8, -1 },  /* RequestIndirectIntroduction (legacy name OpenChannelRequest) */
    { 14, 16, -1 },  /* AddBlockBuffer (m136 v0 params, node_messages.h AddBlockBuffer_Params::V0 +
                      * AddBlockBuffer_Versions::V0 kParams): StructHeader {u32 num_bytes,u32 version}
                      * at body+0, then {BufferId id:u32 @body+8, u32 block_size @body+12,
                      * DriverObject buffer @body+16}.  The Windows DriverObject serializes in place
                      * as its u64 HANDLE (same encoding as the type-7 handle at body+72), so slot1=16
                      * is the Section.  id=0 is the NodeLinkMemory primary block, id>0 the sidecar
                      * block insurance for >128 KiB bursts.  Op 14 is COLD (0 occurrences in the
                      * gate69i/71/95/102/108b pipex captures), so the slot comes from the sources;
                      * relay_scan logs a WA_RELAY: B14 id/size census per frame and falls back to
                      * the last 8-aligned resolvable u64 in the body region.  (Legacy slot2=8 read
                      * the {id,block_size} u32 pair as a handle word — inert, id < 0x10.) */
    { 20, 24, -1 },  /* AcceptParcel (legacy name DuplicateBufferResponse) { ... driver_objects[] } */
    { 21,  8, -1 },  /* AcceptParcelDriverObjects (legacy name GetBufferRequest) { driver_objects[] } */
    { 22,  8, -1 },  /* RouteClosed { sublink, sequence_length } (legacy name GetBufferResponse) — u64 id slot, see relay_slot_is_id() */
    { 32,  8, -1 },  /* StopProxying (legacy name EstablishBrokerChannelRequest) */
    { 33,  8, -1 },  /* ProxyWillStop (legacy name EstablishBrokerChannelResponse) */
    { 34,  8, 16 },  /* BypassPeerWithLink { referral_header: link name u64, sublink, link: driver_object } (legacy name EstablishGroupChannelRequest) */
    { 35,  8, -1 },  /* StopProxyingToLocalPeer (legacy name EstablishGroupChannelResponse) */
    { 36,  8, -1 },  /* FlushRouter { router: RouterId (u64 ProxyId, e.g. 0x1_00000000) } (legacy name PassProxyOnChannelRequest) — u64 id slot */
    { 38,  8, -1 },  /* (no m136 NodeMessages id 38) legacy name PassInterfaceEndpointRequest { interface_endpoint } */
};

/* Types whose in-band slot is a 64-bit ipcz id (RouterId/ProxyId on 36, the
 * ReferralHeader link name + sublink on 34, sublink+sequence_length on 22) rather
 * than a driver-object handle.  Their keys are stored as full u64: a value with the
 * high 32 bits set (0x1_00000000) is a valid slot, not "not a 32-bit handle".  When
 * the low 32 bits do resolve to a handle in the writer the object is relayed as
 * usual (the old 32-bit-slot convention); when they don't, the full u64 id is
 * logged so the decode pass sees the handshake token. */
static int relay_slot_is_id( unsigned short type )
{
    return type == 22 || type == 34 || type == 36;
}

/* WA_MOJO_RELAY: on AcceptParcel (ipcz message_id 20, the relay's "op=20") the
 * payload rides in the NodeLink shared-memory block and the in-band params carry
 * only a fragment reference: the 16-byte message tail is [u32 StructHeader]
 * [u32 ArrayHeader] [u32 block_off] [u32 size].  Append blk=<off>/<size> to the
 * S line so the decode pass knows which fragment offsets matter, and, when this
 * pipe has a block mapping (the AddBlockBuffer/ipcz-id-14 driver object grabbed
 * on writer_end->blk_fd, relayed at off=48/56 in the captures; 'obj' of this very
 * frame is preferred when it is itself a mapping), map that region server-side
 * and dump the first 64 bytes there as WD<hex>.  Read-only, instrumentation
 * only; runs once per scan (write side) under WA_MOJO_RELAY. */
static void relay_dump_blk( struct pipe_end *end, struct object *obj,
                            const unsigned char *b, data_size_t off,
                            data_size_t fnum, data_size_t len )
{
    static const char hexd[] = "0123456789abcdef";
    char hex[129];
    data_size_t tail = off + fnum - 16;
    unsigned int blk_off, blk_size, i, delta;
    const unsigned char *p;
    struct fd *fd = NULL;
    void *base;
    int ufd;

    if (fnum < 56 || off + fnum > len || tail < off + 40) return;  /* tail16 not in this write */
    blk_off  = b[tail+8]  | (b[tail+9] << 8)  | (b[tail+10] << 16)  | ((unsigned int)b[tail+11] << 24);
    blk_size = b[tail+12] | (b[tail+13] << 8) | (b[tail+14] << 16) | ((unsigned int)b[tail+15] << 24);
    if (!blk_size || blk_size >= (1u << 24) || blk_off >= (1u << 28)) return;
    fprintf( stderr, " blk=%08x/%d", blk_off, blk_size );

    if (obj && obj->ops->type == &mapping_type)
    {
        if (!(fd = obj->ops->get_fd( obj ))) return;
        if ((ufd = get_unix_fd( fd )) == -1)
        {
            release_object( fd );
            return;
        }
    }
    else if (end && end->blk_fd != -1) ufd = end->blk_fd;  /* dup owned by us, no object lifetime */
    else return;
    delta = blk_off & 0xfff;
    if ((base = mmap( NULL, delta + 64, PROT_READ, MAP_PRIVATE, ufd,
                      (off_t)blk_off & ~(off_t)0xfff )) == MAP_FAILED)
    {
        if (fd) release_object( fd );
        return;
    }
    p = (const unsigned char *)base + delta;
    for (i = 0; i < 64; i++)
    {
        hex[2*i]   = hexd[p[i] >> 4];
        hex[2*i+1] = hexd[p[i] & 0x0f];
    }
    hex[128] = 0;
    munmap( base, delta + 64 );
    if (fd) release_object( fd );
    fprintf( stderr, " WD%s", hex );
}

/* gate95: snapshot the first 64 bytes of a relayed NodeLinkMemory buffer.
 * The m136 ipcz NodeLinkMemory::Global starts with two fixed-size rings:
 * Ring primary_to_sidecar {U32 size; Atomic<U32> head; Atomic<U32> tail;} at +0 and
 * Ring sidecar_to_primary {size; head; tail;} at +12, then 4 bytes of padding and the
 * box counters (node_link_memory.h; kInitializationBufferSize ring size 0x1a00).  The
 * writer of a direction pushes fragments into the ring of *its* block, so watching
 * (size, head, tail) per buffer per direction over time shows which side's buffer the
 * parcels actually ride on: adoption (client buffer words advance as the browser
 * pushes) vs. re-creation (client buffer static, browser buffer advancing).
 * tag=S: writer-side snapshot at scan (handshake).  tag=D: at delivery into the
 * reader (relay_patch).  tag=T: census on every relayed frame on this pipe end.
 * sha = first 8 bytes in hex (ring size+head fingerprint identifies a buffer). */
static void relay_ring_dump( int ufd, const char *tag, const void *ep,
                             unsigned int wpid, unsigned int rpid,
                             unsigned int op, unsigned long long h )
{
    static const char hexd[] = "0123456789abcdef";
    unsigned char b[64];
    const unsigned int *w = (const unsigned int *)b;
    struct timespec ts;
    unsigned int i;
    void *base;

    if (ufd == -1) return;
    clock_gettime( CLOCK_MONOTONIC, &ts );
    if ((base = mmap( NULL, 64, PROT_READ, MAP_PRIVATE, ufd, 0 )) == MAP_FAILED)
    {
        fprintf( stderr, "WA_RELAY: R tag=%s t=%u.%03u ep=%p mmap_fail\n",
                 tag, (unsigned int)ts.tv_sec, (unsigned int)(ts.tv_nsec / 1000000), ep );
        return;
    }
    memcpy( b, base, 64 );
    munmap( base, 64 );
    fprintf( stderr, "WA_RELAY: R tag=%s t=%u.%03u ep=%p pid=%04x", tag,
             (unsigned int)ts.tv_sec, (unsigned int)(ts.tv_nsec / 1000000), ep, wpid );
    if (rpid) fprintf( stderr, " -> pid=%04x", rpid );
    fprintf( stderr, " op=%u h=%016llx sha=", op, h );
    for (i = 0; i < 8; i++) fputc( hexd[b[i] & 0x0f], stderr ), fputc( hexd[b[i] >> 4], stderr );
    /* w0..w2 = primary_to_sidecar {size, head, tail}, w3..w5 = sidecar_to_primary,
     * w6..w7 = the box/sequence counters past the padding */
    fprintf( stderr, " ring=%u,%u,%u,%u,%u,%u w6=%u w7=%u\n",
             w[0], w[1], w[2], w[3], w[4], w[5], w[6], w[7] );
}

/* gate112: flush-on-arrival doorbell.  gate111 census: S=151 D=9 C=5 K=4 — all 9 boxes
 * (the S lines with obj=) get their D at delivery; S≫D is the census counting the
 * 105 box-less AcceptParcel scans and 37 id-slot lines.  The stall: the pulses ride
 * relay_patch only, so they fire for boxed messages alone (N=2, X=6, all in the
 * handshake window t=1142572.94..1142573.177).  The browser's 105 AcceptParcels and
 * the closing FlushRouter (op=36, id slot 0x5b -> no box) queue into the app's mojo
 * end with no wake: the app's channel thread sits in the duplex mirror read(fd,16),
 * the ring head never advances (ring=1,0,12,0,0,0 all run: 12 pushed, 0 popped, w4=0),
 * and the composition-controller reply rides the ring forever.  Ring data has no W-plane
 * bytes of its own: the AcceptParcel frame is the doorbell, and the box-less frames
 * never ring it.  Pulse the reader end's adopted event (type-7 ev_obj, the ipcz
 * DriverTransport doorbell) plus the duplex mirror on every relayed write that queues a
 * frame, and on read arrival for messages already in the pipe (reader-arrival flush). */
static void relay_doorbell( struct pipe_end *reader, const char *tag )
{
    if (!reader) return;
    if (reader->ev_obj)
    {
        struct object *sync = reader->ev_obj->ops->get_sync( reader->ev_obj );
        struct timespec ts;
        clock_gettime( CLOCK_MONOTONIC, &ts );
        if (sync)
        {
            signal_sync( sync );  /* manual: wake all, auto: wake one (relay_patch N pattern) */
            release_object( sync );
        }
        fprintf( stderr, "WA_RELAY: N t=%u.%03u ep=%p (doorbell %s: reader event pulsed)\n",
                 (unsigned int)ts.tv_sec, (unsigned int)(ts.tv_nsec / 1000000), reader, tag );
    }
    if (reader->fd && reader->pipe &&
        reader->pipe->sharing == (FILE_SHARE_READ | FILE_SHARE_WRITE))
        set_fd_signaled( reader->fd, 1 );  /* duplex inproc-sync mirror: wake the parked read(fd,16) */
}

/* gate112: queued-box depth census.  Counts the boxes sitting on messages already
 * queued on 'reader' (delivered = D'd at the next relay_patch, freed at relay_free)
 * and prints the per-op breakdown: WA_RELAY: Q tag=W (after a relayed write queued a
 * frame) / tag=R (on read arrival, queue non-empty).  depth>0 surviving to the next
 * event of the same plane proves queued-box backlog; depth 0 with S lines streaming
 * proves the S lines are the box-less AcceptParcel/id-slot census. */
static void relay_queue_census( struct pipe_end *reader, const char *tag )
{
    struct pipe_message *message;
    struct relay_box *box;
    unsigned short ops[16];
    unsigned int cnts[16];
    int nops = 0, depth = 0;
    struct timespec ts;

    LIST_FOR_EACH_ENTRY( message, &reader->message_queue, struct pipe_message, entry )
    {
        if (!message->relay) continue;
        LIST_FOR_EACH_ENTRY( box, message->relay, struct relay_box, entry )
        {
            int i;
            for (i = 0; i < nops; i++)
                if (ops[i] == box->type) { cnts[i]++; break; }
            if (i == nops && nops < (int)ARRAY_SIZE(ops))
            {
                ops[nops] = box->type;
                cnts[nops] = 1;
                nops++;
            }
            depth++;
        }
    }
    if (!depth) return;
    clock_gettime( CLOCK_MONOTONIC, &ts );
    fprintf( stderr, "WA_RELAY: Q tag=%s t=%u.%03u ep=%p depth=%d", tag,
             (unsigned int)ts.tv_sec, (unsigned int)(ts.tv_nsec / 1000000), reader, depth );
    for (nops = 0; nops < (int)ARRAY_SIZE(ops) && cnts[nops]; nops++)
        fprintf( stderr, " op%u:%u", ops[nops], cnts[nops] );
    fputc( '\n', stderr );
}

static int relay_scan( struct pipe_end *writer_end, struct pipe_message *message,
                       const void *data, data_size_t len )
{
    static const unsigned int relay_max_frame = 1 << 20;
    const unsigned char *b = data;
    data_size_t off = 0;
    int scanned = 0;

    if (!relay_wanted( writer_end ) || len < 48) return 0;

    while (off + 48 <= len)
    {
        unsigned int magic = b[off] | (b[off+1] << 8) | (b[off+2] << 16) | ((unsigned int)b[off+3] << 24);
        data_size_t fnum, body;
        unsigned short type, hdr;
        int i, s, k, s_line = 0, t14_box = 0;

        if (magic != 0x10) break;  /* frames must start at a message boundary */
        fnum = b[off+4] | (b[off+5] << 8) | (b[off+6] << 16) | ((unsigned int)b[off+7] << 24);
        if (fnum < 48 || fnum > relay_max_frame) break;
        hdr = b[off+16] | (b[off+17] << 8);
        if (hdr != 24) break;
        type = b[off+18] | (b[off+19] << 8);
        body = off + 40;

        for (i = 0; i < (int)ARRAY_SIZE(relay_ops); i++)
            if (relay_ops[i].type == type) break;

        if (i < ARRAY_SIZE(relay_ops))
        {
            const short slots[2] = { relay_ops[i].slot1, relay_ops[i].slot2 };

            scanned++;  /* gate112: a relayed frame rode this write — doorbell the reader */

            if (type == 14 && body + 16 <= off + fnum && body + 16 <= len)
            {
                /* WA_RELAY: B14 — AddBlockBuffer census: record the block id/size of every
                 * sidecar section the NodeLink shares (BufferId 0 = primary ring block,
                 * id>0 = block insurance for >128 KiB parcel bursts), so a future sidecar
                 * burst is visible in the trace even when the driver-object slot does not
                 * resolve and the frame is forwarded unpatched.  Wire: {id:u32 @body+8,
                 * block_size:u32 @body+12} (node_messages.h V0); size widened to u64 for
                 * the log. */
                unsigned int id = b[body+8] | (b[body+9] << 8) | (b[body+10] << 16) | ((unsigned int)b[body+11] << 24);
                unsigned long long size = b[body+12] | (b[body+13] << 8) | (b[body+14] << 16) |
                                          ((unsigned long long)b[body+15] << 24);
                fprintf( stderr, "WA_RELAY: B14 pid=%04x size=%llu id=%08x\n", current->process->id, size, id );
            }

            for (s = 0; s < 2; s++)
            {
                struct relay_box *box;
                data_size_t so;
                unsigned long long v = 0;
                obj_handle_t h;
                struct object *obj;

                if (slots[s] < 0) continue;
                so = body + slots[s];
                if (so + 8 > off + fnum || so + 8 > len) continue;  /* slot lands in a later fragment */
                for (k = 7; k >= 0; k--) v = (v << 8) | b[so+k];
                if (v == ~0ull) continue;     /* -1 = serialized empty slot */
                h = (obj_handle_t)v;
                obj = (h >= 0x10) ? get_handle_obj( current->process, h, 0, NULL ) : NULL;
                if (!obj)
                {
                    clear_error();            /* low-32 is not a handle in the writer */
                    if (!relay_slot_is_id( type )) continue;  /* leave the bytes untouched */
                    /* 64-bit ipcz id slot (RouterId/ProxyId/ReferralHeader/sublink): the key
                     * is the full u64 (0x1_00000000-style ProxyIds included); ids are
                     * link-global, so the reader consumes the writer's value unmodified */
                    fprintf( stderr, "WA_RELAY: S pid=%04x op=%u off=%u id=%016llx",
                             current->process->id, type, so, v );
                    fputc( '\n', stderr );
                    s_line = 1;
                    continue;
                }
                if (!(box = mem_alloc( sizeof(*box) )))
                {
                    release_object( obj );
                    return scanned;
                }
                if (!message->relay)
                {
                    if (!(message->relay = malloc( sizeof(*message->relay) )))
                    {
                        free( box );
                        release_object( obj );
                        return scanned;
                    }
                    list_init( message->relay );
                }
                box->offset = so;
                box->src    = v;
                box->access = get_handle_access( current->process, h );
                box->writer = (struct process *)grab_object( current->process );
                box->obj    = obj;
                box->dst    = 0;
                box->reader = NULL;
                box->type   = type;
                box->end    = writer_end;
                list_add_tail( message->relay, &box->entry );
                if (type == 14) t14_box = 1;
                if ((type == 14 || type == 7) &&
                    (box->obj->ops->type == &mapping_type || box->obj->ops->get_fd))
                {
                    /* latest block buffer wins (AddBlockBuffer id>0, or the ConnectFromBrokerToBroker
                     * primary BufferId-0 mapping relayed on the handshake): keep our own dup of its
                     * unix file (a struct fd is not refcounted: close_fd frees it under a grab,
                     * gate69h SIGSEGV) so the block bytes behind every later AcceptParcel blk= can
                     * be mapped here */
                    struct fd *bfd = box->obj->ops->get_fd( box->obj );
                    if (bfd)
                    {
                        int ufd = get_unix_fd( bfd ), d = -1;
                        if (ufd != -1) d = dup( ufd );
                        release_object( bfd );
                        if (d != -1)
                        {
                            if (writer_end->blk_fd != -1) close( writer_end->blk_fd );
                            writer_end->blk_fd = d;
                            fprintf( stderr, "WA_RELAY: M pid=%04x op=%u blkfd=%d\n",
                                     current->process->id, type, d );
                            if (type == 7)  /* gate95: the handshake buffer is the NodeLinkMemory
                                             * ring block (BufferId 0); keep a dedicated fd for it —
                                             * blk_fd is later replaced by AddBlockBuffer blocks */
                            {
                                int rd = dup( d );
                                if (rd != -1)
                                {
                                    if (writer_end->ring_fd != -1) close( writer_end->ring_fd );
                                    writer_end->ring_fd = rd;
                                }
                                relay_ring_dump( writer_end->ring_fd, "S", writer_end,
                                                 current->process->id, 0, type, box->src );
                            }
                        }
                    }
                }
                if (type == 7 && box->obj->ops->type == &event_type)
                {
                    /* gate102: type-7 (ConnectFromBrokerToBroker) v0 carries TWO driver objects:
                     * slot1 @body+72 = the DriverTransport notification Event, slot2 @body+104 = the
                     * NodeLinkMemory Section (gate100 S-lines: obj=Event + obj=Section/Event both
                     * ways).  ipcz/mojo ChannelWin semantics: the writer pushes fragments into the
                     * ring in *its* Section and then SetEvent()s the event the *reader* passed in
                     * its own handshake; the reader's channel wakes, drains the ring, and (Windows)
                     * the pipe frame is the header plane.  Keep our own ref on the event this end
                     * adopted so W-plane deliveries into the peer of this end can pulse it: the
                     * writer's SetEvent edge only exists if the writer writes, and the bootstrap
                     * type-7 itself never signals anything (the 12 initial fragments predate the
                     * peer event handle adoption => the circularity gate102 probes).  relay_free
                     * keeps the type-7 source handle (K line); this is an extra ref, not a leak. */
                    if (writer_end->ev_obj) release_object( writer_end->ev_obj );
                    writer_end->ev_obj = grab_object( box->obj );
                    fprintf( stderr, "WA_RELAY: E pid=%04x op=7 ev=%016llx\n",
                             current->process->id, box->src );
                }
                s_line = 1;
                {
                    /* gate95: name of the resolved object type (the server has no
                     * __wine_dbg_strdup; the type names are ascii) */
                    char tname[16];
                    unsigned int nl = obj->ops->type->name.len / sizeof(WCHAR);
                    if (nl > 12) nl = 12;
                    for (k = 0; k < (int)nl; k++) tname[k] = (char)obj->ops->type->name.str[k];
                    tname[nl] = 0;
                    fprintf( stderr, "WA_RELAY: S pid=%04x op=%u off=%u src=%016llx access=%08x obj=%s",
                             current->process->id, type, so, box->src, box->access, tname );
                }
                if (type == 20) relay_dump_blk( writer_end, box->obj, b, off, fnum, len );
                fputc( '\n', stderr );
            }
            if (type == 14 && !t14_box)
            {
                /* Defensive generic slot resolution for the COLD op-14 (0 occurrences in all
                 * pipex captures, so slot1=16 is source-derived, never capture-verified): if the
                 * source-derived slot did not resolve to a handle in the writer, scan the body
                 * region from the tail and relay the LAST 8-aligned u64 that does — the serialized
                 * DriverObject rides at the tail of the v0 params (in-place u64 HANDLE, same
                 * encoding as the type-7 handle at body+72) — so the Section still reaches the
                 * reader (relay_patch dups it with the gate108c SD-exempt mapping dup, like type 7)
                 * and its block bytes stay dumpable via the adopted blk_fd.  If nothing resolves,
                 * forward unpatched (the historical behavior). */
                data_size_t so;

                for (so = off + fnum - 8; so >= body + 8 && so + 8 <= len; so -= 8)
                {
                    unsigned long long v = 0;
                    obj_handle_t h;
                    struct object *obj;
                    struct relay_box *box;

                    for (k = 7; k >= 0; k--) v = (v << 8) | b[so+k];
                    if (v == ~0ull) continue;
                    h = (obj_handle_t)v;
                    obj = (h >= 0x10) ? get_handle_obj( current->process, h, 0, NULL ) : NULL;
                    if (!obj)
                    {
                        clear_error();
                        continue;
                    }
                    if (!(box = mem_alloc( sizeof(*box) )))
                    {
                        release_object( obj );
                        return scanned;
                    }
                    if (!message->relay)
                    {
                        if (!(message->relay = malloc( sizeof(*message->relay) )))
                        {
                            free( box );
                            release_object( obj );
                            return scanned;
                        }
                        list_init( message->relay );
                    }
                    box->offset = so;
                    box->src    = v;
                    box->access = get_handle_access( current->process, h );
                    box->writer = (struct process *)grab_object( current->process );
                    box->obj    = obj;
                    box->dst    = 0;
                    box->reader = NULL;
                    box->type   = type;
                    box->end    = writer_end;
                    list_add_tail( message->relay, &box->entry );
                    if (box->obj->ops->type == &mapping_type || box->obj->ops->get_fd)
                    {
                        struct fd *bfd = box->obj->ops->get_fd( box->obj );
                        if (bfd)
                        {
                            int ufd = get_unix_fd( bfd ), d = -1;
                            if (ufd != -1) d = dup( ufd );
                            release_object( bfd );
                            if (d != -1)
                            {
                                if (writer_end->blk_fd != -1) close( writer_end->blk_fd );
                                writer_end->blk_fd = d;
                                fprintf( stderr, "WA_RELAY: M pid=%04x op=14 blkfd=%d\n",
                                         current->process->id, d );
                            }
                        }
                    }
                    {
                        char tname[16];
                        unsigned int nl = obj->ops->type->name.len / sizeof(WCHAR);
                        if (nl > 12) nl = 12;
                        for (k = 0; k < (int)nl; k++) tname[k] = (char)obj->ops->type->name.str[k];
                        tname[nl] = 0;
                        fprintf( stderr, "WA_RELAY: S pid=%04x op=14 off=%u src=%016llx access=%08x obj=%s slot=tailscan",
                                 current->process->id, so, box->src, box->access, tname );
                    }
                    fputc( '\n', stderr );
                    break;
                }
            }
        }
        if (type == 20 && !s_line)  /* AcceptParcel with no resolved in-band handle: log the fragment ref alone */
        {
            fprintf( stderr, "WA_RELAY: S pid=%04x op=20 off=%u", current->process->id, off );
            relay_dump_blk( writer_end, NULL, b, off, fnum, len );
            fputc( '\n', stderr );
        }
        if (writer_end->ring_fd != -1)  /* gate95: ring census on every relayed frame of this pipe */
            relay_ring_dump( writer_end->ring_fd, "T", writer_end, current->process->id, 0, type, 0 );
        if (writer_end->connection && writer_end->connection->ring_fd != -1)
            /* the peer end's buffer is the one this writer's NodeLink pushes *into* on
             * adoption; sample it on every frame the writer sends (peer-side census) */
            relay_ring_dump( writer_end->connection->ring_fd, "P", writer_end->connection,
                             current->process->id, 0, type, 0 );
        off += fnum;
    }
    return scanned;
}

/* duplicate the in-band objects into 'reader' and patch the delivered copy of
 * the message bytes [msg_start, msg_start+len) which land at 'out' */
static void relay_patch( struct pipe_message *message, struct process *reader,
                         data_size_t msg_start, data_size_t len, void *out )
{
    struct relay_box *box;
    data_size_t msg_end = msg_start + len;

    if (!message->relay) return;

    /* gate95: time-series census — the writer end's NodeLink block (type-7 buffer),
     * sampled on every relayed delivery of this message; renderer-pipe traffic spreads
     * these over the run window, giving the handshake -> +5s -> gate-end trajectory */
    {
        struct relay_box *first = LIST_ENTRY( list_head( message->relay ), struct relay_box, entry );
        if (first->end && first->end->ring_fd != -1)
            relay_ring_dump( first->end->ring_fd, "Q", first->end, first->writer->id, reader->id, 0, 0 );
    }

    /* gate102 probe (the prescription): a W-plane delivery into a reader that has adopted a
     * type-7 link is the "ring has data" edge for the *reader's* notification Event — the event
     * this reader passed in its own ConnectFromBrokerToBroker (stored on its pipe_end as ev_obj
     * at relay_scan, WA_RELAY: E line).  On Windows the writer signals it after pushing fragments;
     * the bootstrap fragments (the browser's 12 initial pushes into its ring, ring=...,tail=12,
     * head=0 static) predate the peer event adoption, and the client's MojoThread sits in
     * read(fd,16) on the pipe plane => nobody ever wakes the reader.  Pulse it once per delivery. */
    {
        struct relay_box *first = LIST_ENTRY( list_head( message->relay ), struct relay_box, entry );
        struct pipe_end *rd = first->end ? first->end->connection : NULL;
        if (rd && rd->ev_obj)
        {
            struct object *sync = rd->ev_obj->ops->get_sync( rd->ev_obj );
            struct timespec ts;
            clock_gettime( CLOCK_MONOTONIC, &ts );
            if (sync)
            {
                signal_sync( sync );  /* manual: wake all, auto: wake one (completion.c pattern) */
                release_object( sync );
            }
            fprintf( stderr, "WA_RELAY: N t=%u.%03u pid=%04x -> pid=%04x (reader event pulsed)\n",
                     (unsigned int)ts.tv_sec, (unsigned int)(ts.tv_nsec / 1000000),
                     first->writer->id, reader->id );
        }
    }

    LIST_FOR_EACH_ENTRY( box, message->relay, struct relay_box, entry )
    {
        unsigned char bytes[8];
        data_size_t s, e;
        int k;

        if (box->offset >= msg_end || box->offset + 8 <= msg_start) continue;

        if (!box->dst || box->reader != reader)
        {
            /* gate108b: the browser's NodeLinkMemory Section (op=7 slot2, wire access
             * 0x7 = SECTION_QUERY|MAP_WRITE|MAP_READ from CreateFileMappingW
             * PAGE_READWRITE) failed the reader dup with STATUS_ACCESS_DENIED: the
             * reader maps the relayed ring with SECTION_MAP_READ|WRITE bits the
             * writer-side handle mask (and the type mapping) does not guarantee;
             * dup with the reader-safe union mask (mapping prot follows the section,
             * PAGE_READWRITE both ends).
             * gate108c: the DT line localizes the failure past map_access
             * (mapped=00000007) to the SD check — the browser's section object carries
             * an explicit SD whose ACE mask (generic-read class) intersects 0x7 in
             * QUERY|READ only, so MAP_WRITE denies (granted=0).  Windows
             * DuplicateHandle(DUPLICATE_SAME_ACCESS) never rechecks the SD beyond the
             * source handle's own granted mask; alloc_handle_no_access_check stores
             * box->access verbatim = the true relay dup. */
            obj_handle_t dst;
            if (box->obj->ops->type == &mapping_type)
                dst = alloc_handle_no_access_check( reader, box->obj, box->access, 0 );
            else dst = alloc_handle( reader, box->obj, box->access, 0 );
            if (!dst)
            {
                /* gate102: the reader keeps the writer's raw handle word.  The gate100 run showed the
                 * browser's Section (src=056c access=00000007) vanishing silently here: the client's
                 * read of the type-7 got the raw 0x56c (no D line, K line 'source kept'), so its
                 * ChannelWin shm plane maps the peer's ring with a foreign handle.  Log the NTSTATUS
                 * (alloc_handle fails in map_access->0 => STATUS_ACCESS_DENIED, or the SD check). */
                unsigned int err = get_error();
                clear_error();
                fprintf( stderr, "WA_RELAY: DF pid=%04x src=%016llx -> pid=%04x off=%u op=%u access=%08x err=%08x\n",
                         box->writer->id, box->src, reader->id, box->offset, box->type, box->access, err );
                /* gate108 localize: alloc_handle (server/handle.c:303) fails in exactly two
                 * places — map_access( type generic mapping ) => 0 (handle.c:307,
                 * STATUS_ACCESS_DENIED) or check_object_access => token_access_check over
                 * the object's SD (handle.c:312).  Print the mapped mask, the SD pointer and
                 * the SD verdict to pick the reader-side dup fix (mask union vs SD-check skip);
                 * requested mask = access= above (writer handle access, CreateFileMappingW
                 * PAGE_READWRITE => SECTION_QUERY|MAP_WRITE|MAP_READ = 0x7). */
                {
                    unsigned int mapped = box->obj->ops->map_access( box->obj, box->access );
                    unsigned int granted = mapped;
                    int pass = mapped ? check_object_access( NULL, box->obj, &granted ) : 0;
                    fprintf( stderr, "WA_RELAY: DT pid=%04x src=%016llx -> pid=%04x off=%u op=%u access=%08x mapped=%08x sd=%p granted=%08x pass=%d\n",
                             box->writer->id, box->src, reader->id, box->offset, box->type,
                             box->access, mapped, box->obj->sd, granted, pass );
                    if (!pass) clear_error();
                }
                continue;  /* keep the raw value; the reader sees what Windows would report */
            }
            fprintf( stderr, "WA_RELAY: D pid=%04x src=%016llx -> pid=%04x dst=%04x off=%u op=%u\n",
                     box->writer->id, box->src, reader->id, dst, box->offset, box->type );
            if (box->type == 7 && box->obj->ops->type == &mapping_type)
            {
                /* gate95: the reader adopts this very mapping — snapshot it at delivery
                 * (and at every later read that re-patches the handshake message) */
                struct fd *fd = box->obj->ops->get_fd( box->obj );
                if (fd)
                {
                    relay_ring_dump( get_unix_fd( fd ), "D", box->end, box->writer->id,
                                     reader->id, box->type, box->src );
                    release_object( fd );
                }
            }
            if (box->reader) release_object( box->reader );  /* old value leaks in the old reader */
            box->dst    = dst;
            box->reader = (struct process *)grab_object( reader );
        }
        for (k = 0; k < 8; k++) bytes[k] = (unsigned char)(box->dst >> (8*k));
        s = max( box->offset, msg_start ) - msg_start;
        e = min( box->offset + 8, msg_end ) - msg_start;
        memcpy( (char *)out + s, bytes + (s - (box->offset - msg_start)), e - s );
    }

    /* gate104 lever (a): relayed-reply mirror pulse.  A relayed reply (AcceptParcel
     * with in-band handles, op=20 — the op=7 handshake rides the same path) consumed
     * by the reader on a duplex (self-apartment LRPC, "OLE_") pipe completes the
     * message: message_queue_read pops it, the read queue empties, and the reader's
     * inproc-sync mirror falls disarmed (reselect_read_queue clears cont_armed on the
     * empty queue).  The UI thread that consumed request 6's reply then parks in the
     * LRPC idle receive with no queued message and no edge — OLE_pipe_reads froze at 0
     * since gate103 (ShowWindow 12, cadence wave) — so rpcrt4 never re-arms its read
     * and request 7 (composition-input adapter) is never issued.  Windows pairs the
     * parcel reply with a fresh "you are the server now" pump beat for the consuming
     * thread.  Pulse this end's mirror once per relayed reply, unconditionally (a
     * spurious wake is legal: pipe_end_poll_event drains the byte, runs the drain
     * against the read queue, and the client's read( fd, 16 ) returns empty → rpcrt4
     * re-arms, app pump gets its beat).  gate102's N pulse covers only the type-7
     * ev_obj (mojo plane, browser peer); the duplex mirror plane never pulsed.  */
    {
        struct relay_box *first = LIST_ENTRY( list_head( message->relay ), struct relay_box, entry );
        struct pipe_end *rd = first->end ? first->end->connection : NULL;
        if (rd && rd->fd && first->end->pipe &&
            first->end->pipe->sharing == (FILE_SHARE_READ | FILE_SHARE_WRITE))
        {
            struct timespec ts;
            clock_gettime( CLOCK_MONOTONIC, &ts );
            set_fd_signaled( rd->fd, 1 );
            fprintf( stderr, "WA_RELAY: X t=%u.%03u pid=%04x -> pid=%04x (reply mirror pulsed)\n",
                     (unsigned int)ts.tv_sec, (unsigned int)(ts.tv_nsec / 1000000),
                     first->writer->id, reader->id );
        }
    }
}

static void relay_free( struct pipe_message *message, int close_src )
{
    struct relay_box *box, *next;

    if (!message->relay) return;
    LIST_FOR_EACH_ENTRY_SAFE( box, next, message->relay, struct relay_box, entry )
    {
        /* gate49: ipcz SerializeObject() writes the writer's *own* handle value in-band and the
         * writer keeps it (Windows does DuplicateHandle(writer -> peer) without CLOSE_SOURCE for
         * process objects: the peer needs a process handle for GetProcessId/TerminateProcess/
         * GetExitCodeProcess and the writer keeps its own).  Closing it here freed the browser's
         * handle out from under it (trace: 'D pid=042c src=003c -> pid=053c' then 'C pid=042c
         * src=003c'), so the browser's next GetProcessId(003c) hit a dead table slot:
         * server get_handle_obj -> STATUS_INVALID_HANDLE (0xc0000001) -> ERROR_INVALID_HANDLE (6)
         * -> GetProcessId()==0 -> Chromium CHECK int3 in msedge.dll+0x2d2e44c.
         * Never close a process object on this path; other in-band objects (shared buffers,
         * endpoints, events) keep the opts=3 close to avoid leaking table slots.
         */
        /* gate71: the ConnectFromBrokerToBroker (ipcz id 7) buffer is the NodeLinkMemory
         * primary block buffer (BufferId 0): both brokers keep allocating in it after the
         * handshake, so its writer-side source handle survives delivery (with opts=3 close
         * the relayed run regressed to S=153/D=9, browser dead at t100, 0 msedge windows;
         * the un-relayed gate69i baseline was S=628/D=34, 9 windows). */
        if (close_src && box->obj->ops->type != &process_type && box->type != 7)
        {
            close_handle( box->writer, (obj_handle_t)box->src );  /* mojo consumed the in-band handle (opts=3) */
            fprintf( stderr, "WA_RELAY: C pid=%04x src=%016llx off=%u\n", box->writer->id, box->src, box->offset );
        }
        else if (close_src)
            fprintf( stderr, "WA_RELAY: K pid=%04x src=%016llx off=%u op=%u (source kept)\n",
                     box->writer->id, box->src, box->offset, box->type );
        release_object( box->writer );
        if (box->reader) release_object( box->reader );
        release_object( box->obj );
        free( box );
    }
    free( message->relay );
    message->relay = NULL;
}

static void named_pipe_dump( struct object *obj, int verbose );
static unsigned int named_pipe_map_access( struct object *obj, unsigned int access );
static WCHAR *named_pipe_get_full_name( struct object *obj, data_size_t max, data_size_t *ret_len );
static int named_pipe_link_name( struct object *obj, struct object_name *name, struct object *parent );
static struct object *named_pipe_open_file( struct object *obj, unsigned int access,
                                            unsigned int sharing, unsigned int options );
static void named_pipe_destroy( struct object *obj );

static const struct object_ops named_pipe_ops =
{
    sizeof(struct named_pipe),    /* size */
    &no_type,                     /* type */
    named_pipe_dump,              /* dump */
    no_add_queue,                 /* add_queue */
    NULL,                         /* remove_queue */
    NULL,                         /* signaled */
    NULL,                         /* satisfied */
    no_signal,                    /* signal */
    no_get_fd,                    /* get_fd */
    default_get_sync,             /* get_sync */
    named_pipe_map_access,        /* map_access */
    default_get_sd,               /* get_sd */
    default_set_sd,               /* set_sd */
    named_pipe_get_full_name,     /* get_full_name */
    no_lookup_name,               /* lookup_name */
    named_pipe_link_name,         /* link_name */
    default_unlink_name,          /* unlink_name */
    named_pipe_open_file,         /* open_file */
    no_kernel_obj_list,           /* get_kernel_obj_list */
    no_close_handle,              /* close_handle */
    named_pipe_destroy            /* destroy */
};

/* common server and client pipe end functions */
static void pipe_end_destroy( struct object *obj );
static enum server_fd_type pipe_end_get_fd_type( struct fd *fd );
static struct fd *pipe_end_get_fd( struct object *obj );
static struct security_descriptor *pipe_end_get_sd( struct object *obj );
static int pipe_end_set_sd( struct object *obj, const struct security_descriptor *sd,
                            unsigned int set_info );
static WCHAR *pipe_end_get_full_name( struct object *obj, data_size_t max, data_size_t *len );
static void pipe_end_read( struct fd *fd, struct async *async, file_pos_t pos );
static void pipe_end_poll_event( struct fd *fd, int event );
static int pipe_end_get_poll_events( struct fd *fd );
static void pipe_end_write( struct fd *fd, struct async *async_data, file_pos_t pos );
static void pipe_end_flush( struct fd *fd, struct async *async );
static void pipe_end_get_volume_info( struct fd *fd, struct async *async, unsigned int info_class );
static void pipe_end_reselect_async( struct fd *fd, struct async_queue *queue );
static void pipe_end_get_file_info( struct fd *fd, obj_handle_t handle, unsigned int info_class );

/* server end functions */
static void pipe_server_dump( struct object *obj, int verbose );
static struct object *pipe_server_lookup_name( struct object *obj, struct unicode_str *name,
                                               unsigned int attr, struct object *root );
static struct object *pipe_server_open_file( struct object *obj, unsigned int access,
                                             unsigned int sharing, unsigned int options );
static void pipe_server_destroy( struct object *obj);
static void pipe_server_ioctl( struct fd *fd, ioctl_code_t code, struct async *async );

static const struct object_ops pipe_server_ops =
{
    sizeof(struct pipe_server),   /* size */
    &file_type,                   /* type */
    pipe_server_dump,             /* dump */
    NULL,                         /* add_queue */
    NULL,                         /* remove_queue */
    NULL,                         /* signaled */
    NULL,                         /* satisfied */
    no_signal,                    /* signal */
    pipe_end_get_fd,              /* get_fd */
    default_fd_get_sync,          /* get_sync */
    default_map_access,           /* map_access */
    pipe_end_get_sd,              /* get_sd */
    pipe_end_set_sd,              /* set_sd */
    pipe_end_get_full_name,       /* get_full_name */
    pipe_server_lookup_name,      /* lookup_name */
    no_link_name,                 /* link_name */
    NULL,                         /* unlink_name */
    pipe_server_open_file,        /* open_file */
    no_kernel_obj_list,           /* get_kernel_obj_list */
    async_close_obj_handle,       /* close_handle */
    pipe_server_destroy           /* destroy */
};

static const struct fd_ops pipe_server_fd_ops =
{
    pipe_end_get_poll_events,     /* get_poll_events */
    pipe_end_poll_event,          /* poll_event */
    pipe_end_get_fd_type,         /* get_fd_type */
    pipe_end_read,                /* read */
    pipe_end_write,               /* write */
    pipe_end_flush,               /* flush */
    pipe_end_get_file_info,       /* get_file_info */
    pipe_end_get_volume_info,     /* get_volume_info */
    pipe_server_ioctl,            /* ioctl */
    default_fd_cancel_async,      /* cancel_async */
    no_fd_queue_async,            /* queue_async */
    pipe_end_reselect_async       /* reselect_async */
};

/* client end functions */
static void pipe_client_dump( struct object *obj, int verbose );
static void pipe_client_ioctl( struct fd *fd, ioctl_code_t code, struct async *async );

static const struct object_ops pipe_client_ops =
{
    sizeof(struct pipe_end),      /* size */
    &file_type,                   /* type */
    pipe_client_dump,             /* dump */
    NULL,                         /* add_queue */
    NULL,                         /* remove_queue */
    NULL,                         /* signaled */
    NULL,                         /* satisfied */
    no_signal,                    /* signal */
    pipe_end_get_fd,              /* get_fd */
    default_fd_get_sync,          /* get_sync */
    default_map_access,           /* map_access */
    pipe_end_get_sd,              /* get_sd */
    pipe_end_set_sd,              /* set_sd */
    pipe_end_get_full_name,       /* get_full_name */
    no_lookup_name,               /* lookup_name */
    no_link_name,                 /* link_name */
    NULL,                         /* unlink_name */
    no_open_file,                 /* open_file */
    no_kernel_obj_list,           /* get_kernel_obj_list */
    async_close_obj_handle,       /* close_handle */
    pipe_end_destroy              /* destroy */
};

static const struct fd_ops pipe_client_fd_ops =
{
    pipe_end_get_poll_events,     /* get_poll_events */
    pipe_end_poll_event,          /* poll_event */
    pipe_end_get_fd_type,         /* get_fd_type */
    pipe_end_read,                /* read */
    pipe_end_write,               /* write */
    pipe_end_flush,               /* flush */
    pipe_end_get_file_info,       /* get_file_info */
    pipe_end_get_volume_info,     /* get_volume_info */
    pipe_client_ioctl,            /* ioctl */
    default_fd_cancel_async,      /* cancel_async */
    no_fd_queue_async,            /* queue_async */
    pipe_end_reselect_async       /* reselect_async */
};

static void named_pipe_device_dump( struct object *obj, int verbose );
static WCHAR *named_pipe_device_get_full_name( struct object *obj, data_size_t max, data_size_t *len );
static struct object *named_pipe_device_lookup_name( struct object *obj,
    struct unicode_str *name, unsigned int attr, struct object *root );
static struct object *named_pipe_device_open_file( struct object *obj, unsigned int access,
                                                   unsigned int sharing, unsigned int options );
static void named_pipe_device_destroy( struct object *obj );

static const struct object_ops named_pipe_device_ops =
{
    sizeof(struct named_pipe_device), /* size */
    &device_type,                     /* type */
    named_pipe_device_dump,           /* dump */
    no_add_queue,                     /* add_queue */
    NULL,                             /* remove_queue */
    NULL,                             /* signaled */
    no_satisfied,                     /* satisfied */
    no_signal,                        /* signal */
    no_get_fd,                        /* get_fd */
    default_get_sync,                 /* get_sync */
    default_map_access,               /* map_access */
    default_get_sd,                   /* get_sd */
    default_set_sd,                   /* set_sd */
    named_pipe_device_get_full_name,  /* get_full_name */
    named_pipe_device_lookup_name,    /* lookup_name */
    directory_link_name,              /* link_name */
    default_unlink_name,              /* unlink_name */
    named_pipe_device_open_file,      /* open_file */
    no_kernel_obj_list,               /* get_kernel_obj_list */
    no_close_handle,                  /* close_handle */
    named_pipe_device_destroy         /* destroy */
};

static void named_pipe_device_file_dump( struct object *obj, int verbose );
static struct fd *named_pipe_device_file_get_fd( struct object *obj );
static WCHAR *named_pipe_device_file_get_full_name( struct object *obj, data_size_t max, data_size_t *len );
static void named_pipe_device_ioctl( struct fd *fd, ioctl_code_t code, struct async *async );
static enum server_fd_type named_pipe_device_file_get_fd_type( struct fd *fd );
static void named_pipe_device_file_destroy( struct object *obj );

static const struct object_ops named_pipe_device_file_ops =
{
    sizeof(struct named_pipe_device_file),   /* size */
    &file_type,                              /* type */
    named_pipe_device_file_dump,             /* dump */
    NULL,                                    /* add_queue */
    NULL,                                    /* remove_queue */
    NULL,                                    /* signaled */
    NULL,                                    /* satisfied */
    no_signal,                               /* signal */
    named_pipe_device_file_get_fd,           /* get_fd */
    default_fd_get_sync,                     /* get_sync */
    default_map_access,                      /* map_access */
    default_get_sd,                          /* get_sd */
    default_set_sd,                          /* set_sd */
    named_pipe_device_file_get_full_name,    /* get_full_name */
    no_lookup_name,                          /* lookup_name */
    no_link_name,                            /* link_name */
    NULL,                                    /* unlink_name */
    no_open_file,                            /* open_file */
    no_kernel_obj_list,                      /* get_kernel_obj_list */
    no_close_handle,                         /* close_handle */
    named_pipe_device_file_destroy           /* destroy */
};

static const struct fd_ops named_pipe_device_fd_ops =
{
    default_fd_get_poll_events,              /* get_poll_events */
    default_poll_event,                      /* poll_event */
    named_pipe_device_file_get_fd_type,      /* get_fd_type */
    no_fd_read,                              /* read */
    no_fd_write,                             /* write */
    no_fd_flush,                             /* flush */
    default_fd_get_file_info,                /* get_file_info */
    no_fd_get_volume_info,                   /* get_volume_info */
    named_pipe_device_ioctl,                 /* ioctl */
    default_fd_cancel_async,                 /* cancel_async */
    default_fd_queue_async,                  /* queue_async */
    default_fd_reselect_async                /* reselect_async */
};

static void named_pipe_dir_dump( struct object *obj, int verbose );
static struct fd *named_pipe_dir_get_fd( struct object *obj );
static WCHAR *named_pipe_dir_get_full_name( struct object *obj, data_size_t max, data_size_t *ret_len );
static void named_pipe_dir_ioctl( struct fd *fd, ioctl_code_t code, struct async *async );
static struct object *named_pipe_dir_lookup_name( struct object *obj, struct unicode_str *name,
                                                  unsigned int attr, struct object *root );
static struct object *named_pipe_dir_open_file( struct object *obj, unsigned int access,
                                                unsigned int sharing, unsigned int options );
static void named_pipe_dir_destroy( struct object *obj );

static const struct object_ops named_pipe_dir_ops =
{
    sizeof(struct named_pipe_device_file),   /* size */
    &file_type,                              /* type */
    named_pipe_dir_dump,                     /* dump */
    NULL,                                    /* add_queue */
    NULL,                                    /* remove_queue */
    NULL,                                    /* signaled */
    NULL,                                    /* satisfied */
    no_signal,                               /* signal */
    named_pipe_dir_get_fd,                   /* get_fd */
    default_fd_get_sync,                     /* get_sync */
    default_map_access,                      /* map_access */
    default_get_sd,                          /* get_sd */
    default_set_sd,                          /* set_sd */
    named_pipe_dir_get_full_name,            /* get_full_name */
    named_pipe_dir_lookup_name,              /* lookup_name */
    no_link_name,                            /* link_name */
    NULL,                                    /* unlink_name */
    named_pipe_dir_open_file,                /* open_file */
    no_kernel_obj_list,                      /* get_kernel_obj_list */
    no_close_handle,                         /* close_handle */
    named_pipe_dir_destroy                   /* destroy */
};

static const struct fd_ops named_pipe_dir_fd_ops =
{
    default_fd_get_poll_events,              /* get_poll_events */
    default_poll_event,                      /* poll_event */
    NULL,                                    /* get_fd_type */
    no_fd_read,                              /* read */
    no_fd_write,                             /* write */
    no_fd_flush,                             /* flush */
    default_fd_get_file_info,                /* get_file_info */
    no_fd_get_volume_info,                   /* get_volume_info */
    named_pipe_dir_ioctl,                    /* ioctl */
    default_fd_cancel_async,                 /* cancel_async */
    default_fd_queue_async,                  /* queue_async */
    default_fd_reselect_async                /* reselect_async */
};

static void named_pipe_dump( struct object *obj, int verbose )
{
    fputs( "Named pipe\n", stderr );
}

static unsigned int named_pipe_map_access( struct object *obj, unsigned int access )
{
    if (access & GENERIC_READ)    access |= STANDARD_RIGHTS_READ;
    if (access & GENERIC_WRITE)   access |= STANDARD_RIGHTS_WRITE | FILE_CREATE_PIPE_INSTANCE;
    if (access & GENERIC_EXECUTE) access |= STANDARD_RIGHTS_EXECUTE;
    if (access & GENERIC_ALL)     access |= STANDARD_RIGHTS_ALL;
    return access & ~(GENERIC_READ | GENERIC_WRITE | GENERIC_EXECUTE | GENERIC_ALL);
}

static WCHAR *named_pipe_get_full_name( struct object *obj, data_size_t max, data_size_t *ret_len )
{
    WCHAR *ret;

    if (!(ret = default_get_full_name( obj, max, ret_len )))
        set_error( STATUS_OBJECT_PATH_INVALID );
    else if (*ret_len > max)
        set_error( STATUS_BUFFER_OVERFLOW );
    return ret;
}

static void pipe_server_dump( struct object *obj, int verbose )
{
    struct pipe_server *server = (struct pipe_server *) obj;
    assert( obj->ops == &pipe_server_ops );
    fprintf( stderr, "Named pipe server pipe=%p state=%d\n", server->pipe_end.pipe,
             server->pipe_end.state );
}

static void pipe_client_dump( struct object *obj, int verbose )
{
    struct pipe_end *client = (struct pipe_end *) obj;
    assert( obj->ops == &pipe_client_ops );
    fprintf( stderr, "Named pipe client server=%p\n", client->connection );
}

static void named_pipe_destroy( struct object *obj)
{
    struct named_pipe *pipe = (struct named_pipe *) obj;

    assert( list_empty( &pipe->listeners ) );
    assert( !pipe->instances );
    free_async_queue( &pipe->waiters );
}

static struct fd *pipe_end_get_fd( struct object *obj )
{
    struct pipe_end *pipe_end = (struct pipe_end *) obj;
    return (struct fd *) grab_object( pipe_end->fd );
}

/* in-process sync fd for a named pipe handle: its sync object (the fd's internal
 * event) is polled by the client's NtWaitForSingleObject / MsgWait fast path; when
 * that event has no ntsync fd the request used to answer NOT_IMPLEMENTED and every
 * wait cost a wineserver round trip.  Hand back the fd's pollable sync pipe (read
 * side), reported as an event so the client treats it as a signaled/waitable fd. */
int named_pipe_get_inproc_sync_fd( struct object *obj, int *type )
{
    struct fd *fd = get_obj_fd( obj );
    int ret = -1;

    if (fd)
    {
        if ((ret = fd_get_inproc_sync_fd( fd )) != -1) *type = INPROC_SYNC_EVENT;
        release_object( fd );
    }
    return ret;
}

static struct pipe_message *queue_message( struct pipe_end *pipe_end, struct iosb *iosb )
{
    struct pipe_message *message;

    if (!(message = mem_alloc( sizeof(*message) ))) return NULL;
    message->iosb = (struct iosb *)grab_object( iosb );
    message->async = NULL;
    message->read_pos = 0;
    message->relay = NULL;
    list_add_tail( &pipe_end->message_queue, &message->entry );
    return message;
}

static void wake_message( struct pipe_message *message, data_size_t result )
{
    struct async *async = message->async;

    message->async = NULL;
    if (!async) return;

    async_request_complete( async, STATUS_SUCCESS, result, 0, NULL );
    release_object( async );
}

static void free_message( struct pipe_message *message )
{
    relay_free( message, 0 );  /* dropped, not consumed: leave the source handles alone */
    list_remove( &message->entry );
    if (message->iosb) release_object( message->iosb );
    free( message );
}

static void pipe_end_disconnect( struct pipe_end *pipe_end, unsigned int status )
{
    struct pipe_end *connection = pipe_end->connection;
    struct pipe_message *message, *next;
    struct async *async;

    pipe_end->connection = NULL;

    /* Only an explicit DisconnectNamedPipe() invalidates the cached peer ids of a server
     * end; a teardown (handle close, status != STATUS_PIPE_DISCONNECTED) leaves them
     * queryable.  Releasing them in both cases made the peer lookups of a pipe whose
     * client handle had already been closed answer ERROR_PIPE_BUSY (231), and
     * ImpersonateNamedPipeClient() on the same instance ERROR_BAD_PIPE (230). */
    if (pipe_end->obj.ops == &pipe_server_ops && status == STATUS_PIPE_DISCONNECTED)
    {
        if (pipe_end->client_proc) release_object( pipe_end->client_proc );
        pipe_end->client_proc = NULL;
        pipe_end->conn_info = CONN_INFO_CLEARED;
    }

    pipe_end->state = status == STATUS_PIPE_DISCONNECTED
        ? FILE_PIPE_DISCONNECTED_STATE : FILE_PIPE_CLOSING_STATE;
    if (wv2io_wanted( pipe_end ))
        WV2IO( "S pid=%04x ep=%p st=%c (teardown %x)\n",
               current ? current->process->id : 0, pipe_end,
               wv2io_state_ch( pipe_end->state ), status );
    fd_async_wake_up( pipe_end->fd, ASYNC_TYPE_WAIT, status );
    async_wake_up( &pipe_end->read_q, status );
    LIST_FOR_EACH_ENTRY_SAFE( message, next, &pipe_end->message_queue, struct pipe_message, entry )
    {
        async = message->async;
        if (async || status == STATUS_PIPE_DISCONNECTED) free_message( message );
        if (!async) continue;
        async_terminate( async, status );
        release_object( async );
    }
    if (status == STATUS_PIPE_DISCONNECTED) set_fd_signaled( pipe_end->fd, 0 );

    if (connection)
    {
        connection->connection = NULL;
        /* the client end loses its copy of the peer ids only on an explicit disconnect;
         * it then reports the connection as not connected, not as an unavailable pipe */
        if (status == STATUS_PIPE_DISCONNECTED) connection->conn_info = CONN_INFO_CLEARED;
        pipe_end_disconnect( connection, status );
    }
}

static void pipe_end_destroy( struct object *obj )
{
    struct pipe_end *pipe_end = (struct pipe_end *)obj;
    struct pipe_message *message;

    pipe_end_disconnect( pipe_end, STATUS_PIPE_BROKEN );

    while (!list_empty( &pipe_end->message_queue ))
    {
        message = LIST_ENTRY( list_head(&pipe_end->message_queue), struct pipe_message, entry );
        assert( !message->async );
        free_message( message );
    }

    free_async_queue( &pipe_end->read_q );
    free_async_queue( &pipe_end->write_q );
    if (pipe_end->fd) release_object( pipe_end->fd );
    if (pipe_end->blk_fd != -1) close( pipe_end->blk_fd );
    if (pipe_end->ring_fd != -1) close( pipe_end->ring_fd );
    if (pipe_end->hb_timeout) remove_timeout_user( pipe_end->hb_timeout );  /* gate104: mirror heartbeat */
    if (pipe_end->ev_obj) release_object( pipe_end->ev_obj );  /* gate102: our ref on the type-7 event */
    if (pipe_end->pipe) release_object( pipe_end->pipe );
    if (pipe_end->client_proc) release_object( pipe_end->client_proc );
}

static struct object *pipe_server_lookup_name( struct object *obj, struct unicode_str *name,
                                               unsigned int attr, struct object *root )
{
    if (name && name->len)
        set_error( STATUS_OBJECT_NAME_INVALID );

    return NULL;
}

static struct object *pipe_server_open_file( struct object *obj, unsigned int access,
                                             unsigned int sharing, unsigned int options )
{
    struct pipe_server *server = (struct pipe_server *)obj;

    return server->pipe_end.pipe->obj.ops->open_file( &server->pipe_end.pipe->obj, access, sharing, options );
}

static void pipe_server_destroy( struct object *obj )
{
    struct pipe_server *server = (struct pipe_server *)obj;
    struct named_pipe *pipe = server->pipe_end.pipe;

    assert( obj->ops == &pipe_server_ops );

    assert( pipe->instances );
    if (!--pipe->instances) unlink_named_object( &pipe->obj );
    if (server->pipe_end.state == FILE_PIPE_LISTENING_STATE)
        list_remove( &server->entry );

    free_async_queue( &server->listen_q );
    pipe_end_destroy( obj );
}

static void named_pipe_device_dump( struct object *obj, int verbose )
{
    fputs( "Named pipe device\n", stderr );
}

static WCHAR *named_pipe_device_get_full_name( struct object *obj, data_size_t max, data_size_t *len )
{
    WCHAR *ret = default_get_full_name( obj, max, len );
    if (*len > max) set_error( STATUS_BUFFER_OVERFLOW );
    return ret;
}

static struct object *named_pipe_device_lookup_name( struct object *obj, struct unicode_str *name,
                                                     unsigned int attr, struct object *root )
{
    struct named_pipe_device *device = (struct named_pipe_device*)obj;
    struct object *found;

    assert( obj->ops == &named_pipe_device_ops );
    assert( device->pipes );

    if (!name) return NULL;  /* open the device itself */
    if (!name->len && name->str)
    {
        /* open the root directory */
        struct named_pipe_device_file *dir = alloc_object( &named_pipe_dir_ops );

        if (!dir) return NULL;

        dir->fd = NULL;  /* defer alloc_pseudo_fd() until after we have options */
        dir->device = (struct named_pipe_device *)grab_object( obj );

        return &dir->obj;
    }

    if ((found = find_object( device->pipes, name, attr | OBJ_CASE_INSENSITIVE )))
        name->len = 0;
    else
    {
        static const WCHAR localW[] = {'L','O','C','A','L','\\'};
        struct unicode_str alt_name;

        /* Windows NPFS aliases \Device\NamedPipe\LOCAL\<name> to \Device\NamedPipe\<name>. */
        if (name->len >= sizeof(localW) && !memicmp_strW( name->str, localW, sizeof(localW) ))
        {
            alt_name.str = name->str + 6;
            alt_name.len = name->len - sizeof(localW);
            if ((found = find_object( device->pipes, &alt_name, attr | OBJ_CASE_INSENSITIVE )))
                name->len = 0;
        }
        else
        {
            WCHAR buf[256];
            if (name->len / sizeof(WCHAR) + 6 < ARRAY_SIZE(buf))
            {
                memcpy( buf, localW, sizeof(localW) );
                memcpy( buf + 6, name->str, name->len );
                alt_name.str = buf;
                alt_name.len = name->len + sizeof(localW);
                if ((found = find_object( device->pipes, &alt_name, attr | OBJ_CASE_INSENSITIVE )))
                    name->len = 0;
            }
        }
    }

    return found;
}

static struct object *named_pipe_device_open_file( struct object *obj, unsigned int access,
                                                   unsigned int sharing, unsigned int options )
{
    struct named_pipe_device_file *file;

    if (!(file = alloc_object( &named_pipe_device_file_ops ))) return NULL;
    file->device = (struct named_pipe_device *)grab_object( obj );
    if (!(file->fd = alloc_pseudo_fd( &named_pipe_device_fd_ops, obj, options )))
    {
        release_object( file );
        return NULL;
    }
    allow_fd_caching( file->fd );
    return &file->obj;
}

static void named_pipe_device_destroy( struct object *obj )
{
    struct named_pipe_device *device = (struct named_pipe_device*)obj;
    assert( obj->ops == &named_pipe_device_ops );
    free( device->pipes );
}

struct object *create_named_pipe_device( struct object *root, const struct unicode_str *name,
                                         unsigned int attr, const struct security_descriptor *sd )
{
    struct named_pipe_device *dev;

    if ((dev = create_named_object( root, &named_pipe_device_ops, name, attr, sd )) &&
        get_error() != STATUS_OBJECT_NAME_EXISTS)
    {
        dev->pipes = NULL;
        if (!(dev->pipes = create_namespace( 7 )))
        {
            release_object( dev );
            dev = NULL;
        }
    }
    return &dev->obj;
}

static void named_pipe_device_file_dump( struct object *obj, int verbose )
{
    struct named_pipe_device_file *file = (struct named_pipe_device_file *)obj;

    fprintf( stderr, "File on named pipe device %p\n", file->device );
}

static struct fd *named_pipe_device_file_get_fd( struct object *obj )
{
    struct named_pipe_device_file *file = (struct named_pipe_device_file *)obj;
    return (struct fd *)grab_object( file->fd );
}

static WCHAR *named_pipe_device_file_get_full_name( struct object *obj, data_size_t max, data_size_t *len )
{
    struct named_pipe_device_file *file = (struct named_pipe_device_file *)obj;
    return file->device->obj.ops->get_full_name( &file->device->obj, max, len );
}

static enum server_fd_type named_pipe_device_file_get_fd_type( struct fd *fd )
{
    return FD_TYPE_DEVICE;
}

static void named_pipe_device_file_destroy( struct object *obj )
{
    struct named_pipe_device_file *file = (struct named_pipe_device_file*)obj;
    assert( obj->ops == &named_pipe_device_file_ops );
    if (file->fd) release_object( file->fd );
    release_object( file->device );
}

static void named_pipe_dir_dump( struct object *obj, int verbose )
{
    struct named_pipe_device_file *dir = (struct named_pipe_device_file *)obj;

    fprintf( stderr, "Root directory of named pipe device %p\n", dir->device );
}

static struct fd *named_pipe_dir_get_fd( struct object *obj )
{
    struct named_pipe_device_file *dir = (struct named_pipe_device_file *)obj;
    return (struct fd *)grab_object( dir->fd );
}

static WCHAR *named_pipe_dir_get_full_name( struct object *obj, data_size_t max, data_size_t *ret_len )
{
    struct named_pipe_device_file *dir = (struct named_pipe_device_file *)obj;
    data_size_t len;
    char *device_name, *ret;

    device_name = (char *)dir->device->obj.ops->get_full_name( &dir->device->obj, max, &len );
    if (!device_name) return NULL;

    len += sizeof(WCHAR);
    ret = realloc(device_name, len);
    if (!ret)
    {
        free(device_name);
        return NULL;
    }
    *(WCHAR *)(ret + len - sizeof(WCHAR)) = '\\';

    if (len > max) set_error( STATUS_BUFFER_OVERFLOW );
    *ret_len = len;
    return (WCHAR *)ret;
}

static struct object *named_pipe_dir_lookup_name( struct object *obj, struct unicode_str *name,
                                                  unsigned int attr, struct object *root )
{
    struct named_pipe_device_file *dir = (struct named_pipe_device_file *)obj;
    if (!name || !name->len) return NULL;  /* open the directory itself */
    return dir->device->obj.ops->lookup_name( &dir->device->obj, name, attr, root );
}

static struct object *named_pipe_dir_open_file( struct object *obj, unsigned int access,
                                                unsigned int sharing, unsigned int options )
{
    struct named_pipe_device_file *dir = (struct named_pipe_device_file *)obj;

    if (dir->fd)
    {
        /* Trying to open by (already opened) file object */
        return no_open_file( obj, access, sharing, options );
    }

    /* Turn this "proto-object" into an actual file object */

    if (!(dir->fd = alloc_pseudo_fd( &named_pipe_dir_fd_ops, obj, options )))
        return NULL;

    allow_fd_caching( dir->fd );
    return grab_object( obj );
}

static void named_pipe_dir_destroy( struct object *obj )
{
    struct named_pipe_device_file *file = (struct named_pipe_device_file *)obj;
    assert( obj->ops == &named_pipe_dir_ops );
    if (file->fd) release_object( file->fd );
    release_object( file->device );
}

static void pipe_end_flush( struct fd *fd, struct async *async )
{
    struct pipe_end *pipe_end = get_fd_user( fd );

    if (!pipe_end->pipe)
    {
        set_error( STATUS_PIPE_DISCONNECTED );
        return;
    }

    if (pipe_end->connection && !list_empty( &pipe_end->connection->message_queue ))
    {
        fd_queue_async( pipe_end->fd, async, ASYNC_TYPE_WAIT );
        set_error( STATUS_PENDING );
    }
}

static data_size_t pipe_end_get_avail( struct pipe_end *pipe_end )
{
    struct pipe_message *message;
    data_size_t avail = 0;

    LIST_FOR_EACH_ENTRY( message, &pipe_end->message_queue, struct pipe_message, entry )
        avail += message->iosb->in_size - message->read_pos;

    return avail;
}

static void pipe_end_get_file_info( struct fd *fd, obj_handle_t handle, unsigned int info_class )
{
    struct pipe_end *pipe_end = get_fd_user( fd );
    struct named_pipe *pipe = pipe_end->pipe;

    switch (info_class)
    {
    case FileNameInformation:
        {
            FILE_NAME_INFORMATION *name_info;
            data_size_t name_len, reply_size;
            const WCHAR *name;

            if (get_reply_max_size() < sizeof(*name_info))
            {
                set_error( STATUS_INFO_LENGTH_MISMATCH );
                return;
            }

            /* FIXME: We should be able to return on unlinked pipe */
            if (!pipe || !(name = get_object_name( &pipe->obj, &name_len )))
            {
                set_error( STATUS_PIPE_DISCONNECTED );
                return;
            }

            reply_size = offsetof( FILE_NAME_INFORMATION, FileName[name_len/sizeof(WCHAR) + 1] );
            if (reply_size > get_reply_max_size())
            {
                reply_size = get_reply_max_size();
                set_error( STATUS_BUFFER_OVERFLOW );
            }

            if (!(name_info = set_reply_data_size( reply_size ))) return;
            name_info->FileNameLength = name_len + sizeof(WCHAR);
            name_info->FileName[0] = '\\';
            reply_size -= offsetof( FILE_NAME_INFORMATION, FileName[1] );
            if (reply_size) memcpy( &name_info->FileName[1], name, reply_size );
            break;
        }
    case FilePipeInformation:
    {
            FILE_PIPE_INFORMATION *pipe_info;

            if (!(get_handle_access( current->process, handle) & FILE_READ_ATTRIBUTES))
            {
                set_error( STATUS_ACCESS_DENIED );
                return;
            }

            if (get_reply_max_size() < sizeof(*pipe_info))
            {
                set_error( STATUS_INFO_LENGTH_MISMATCH );
                return;
            }

            if (!pipe)
            {
                set_error( STATUS_PIPE_DISCONNECTED );
                return;
            }

            if (!(pipe_info = set_reply_data_size( sizeof(*pipe_info) ))) return;
            pipe_info->ReadMode       = (pipe_end->flags & NAMED_PIPE_MESSAGE_STREAM_READ)
                ? FILE_PIPE_MESSAGE_MODE : FILE_PIPE_BYTE_STREAM_MODE;
            pipe_info->CompletionMode = (pipe_end->flags & NAMED_PIPE_NONBLOCKING_MODE)
                ? FILE_PIPE_COMPLETE_OPERATION : FILE_PIPE_QUEUE_OPERATION;
            break;
        }
    case FilePipeLocalInformation:
        {
            FILE_PIPE_LOCAL_INFORMATION *pipe_info;

            if (!(get_handle_access( current->process, handle) & FILE_READ_ATTRIBUTES))
            {
                set_error( STATUS_ACCESS_DENIED );
                return;
            }

            if (get_reply_max_size() < sizeof(*pipe_info))
            {
                set_error( STATUS_INFO_LENGTH_MISMATCH );
                return;
            }

            if (!pipe)
            {
                set_error( STATUS_PIPE_DISCONNECTED );
                return;
            }

            if (!(pipe_info = set_reply_data_size( sizeof(*pipe_info) ))) return;
            pipe_info->NamedPipeType = pipe->message_mode;
            switch (pipe->sharing)
            {
            case FILE_SHARE_READ:
                pipe_info->NamedPipeConfiguration = FILE_PIPE_OUTBOUND;
                break;
            case FILE_SHARE_WRITE:
                pipe_info->NamedPipeConfiguration = FILE_PIPE_INBOUND;
                break;
            case FILE_SHARE_READ | FILE_SHARE_WRITE:
                pipe_info->NamedPipeConfiguration = FILE_PIPE_FULL_DUPLEX;
                break;
            }
            pipe_info->MaximumInstances    = pipe->maxinstances;
            pipe_info->CurrentInstances    = pipe->instances;
            pipe_info->InboundQuota        = pipe->insize;

            pipe_info->ReadDataAvailable   = pipe_end_get_avail( pipe_end );

            pipe_info->OutboundQuota       = pipe->outsize;
            pipe_info->NamedPipeState      = pipe_end->state;
            pipe_info->NamedPipeEnd        = pipe_end->obj.ops == &pipe_server_ops
                ? FILE_PIPE_SERVER_END : FILE_PIPE_CLIENT_END;

            pipe_info->WriteQuotaAvailable = pipe_info->NamedPipeEnd == FILE_PIPE_CLIENT_END
                ? pipe_info->InboundQuota : pipe_info->OutboundQuota;
                /* FIXME: Needs to be reduced by ReadDataAvailable at the other end of the pipe. */
            break;
        }
    case FileStandardInformation:
        {
            FILE_STANDARD_INFORMATION *std_info;

            if (!(get_handle_access( current->process, handle) & FILE_READ_ATTRIBUTES))
            {
                set_error( STATUS_ACCESS_DENIED );
                return;
            }

            if (get_reply_max_size() < sizeof(*std_info))
            {
                set_error( STATUS_INFO_LENGTH_MISMATCH );
                return;
            }

            if (!pipe)
            {
                set_error( STATUS_PIPE_DISCONNECTED );
                return;
            }

            if (!(std_info = set_reply_data_size( sizeof(*std_info) ))) return;
            std_info->AllocationSize.QuadPart = pipe->outsize + pipe->insize;
            std_info->EndOfFile.QuadPart      = pipe_end_get_avail( pipe_end );
            std_info->NumberOfLinks           = 1; /* FIXME */
            std_info->DeletePending           = 0; /* FIXME */
            std_info->Directory               = 0;
            break;
        }
    default:
        default_fd_get_file_info( fd, handle, info_class );
    }
}

static struct security_descriptor *pipe_end_get_sd( struct object *obj )
{
    struct pipe_end *pipe_end = (struct pipe_end *) obj;
    if (pipe_end->pipe) return default_get_sd( &pipe_end->pipe->obj );
    set_error( STATUS_PIPE_DISCONNECTED );
    return NULL;
}

static int pipe_end_set_sd( struct object *obj, const struct security_descriptor *sd,
                            unsigned int set_info )
{
    struct pipe_end *pipe_end = (struct pipe_end *) obj;
    if (pipe_end->pipe) return default_set_sd( &pipe_end->pipe->obj, sd, set_info );
    set_error( STATUS_PIPE_DISCONNECTED );
    return 0;
}

static WCHAR *pipe_end_get_full_name( struct object *obj, data_size_t max, data_size_t *len )
{
    struct pipe_end *pipe_end = (struct pipe_end *) obj;
    return pipe_end->pipe->obj.ops->get_full_name( &pipe_end->pipe->obj, max, len );
}

static void pipe_end_get_volume_info( struct fd *fd, struct async *async, unsigned int info_class )
{
    switch (info_class)
    {
    case FileFsDeviceInformation:
        {
            static const FILE_FS_DEVICE_INFORMATION device_info =
            {
                FILE_DEVICE_NAMED_PIPE,
                FILE_DEVICE_ALLOW_APPCONTAINER_TRAVERSAL
            };
            if (get_reply_max_size() >= sizeof(device_info))
                set_reply_data( &device_info, sizeof(device_info) );
            else
                set_error( STATUS_BUFFER_TOO_SMALL );
            break;
        }
    default:
        set_error( STATUS_NOT_IMPLEMENTED );
    }
}

/* Duplex (self-apartment LRPC) ends ride the inproc-sync mirror (fd.c
 * fd_poll_fd): a byte written by the peer's set_fd_signaled is the data-arrived
 * edge that wakes the drain (pipe_end_poll_event → reselect_read_queue).  A
 * message-mode read that completes short (rpcrt4 transceives a received packet
 * as 16-byte header, then the body: the first read lands on
 * STATUS_BUFFER_OVERFLOW and the message stays queued with read_pos advanced)
 * leaves the fd with no waiting async, so default_fd_get_poll_events() drops
 * the mirror's POLLIN: the relayed edges queued across the header/body split
 * are never replayed, the remainder drains one chunk per fresh peer write edge,
 * and the 7th LRPC request (composition-input adapter) strands on its body.
 * Keep the mirror armed while a partially consumed message is pending. */
/* gate104 lever (b): duplex mirror heartbeat.  The self-apartment LRPC model
 * parks the UI thread in rpcrt4's idle receive — a read( mirror fd, 16 ) whose
 * wineserver async sits on pipe_end->read_q — and the pump beat that issues the
 * 7th LRPC request (composition-input adapter) needs an edge with no queued
 * message behind it: after request 6's reply is consumed the read queue empties,
 * the mirror falls disarmed, and OLE_pipe_reads froze at 0 (gate103, ShowWindow
 * 12, cadence wave) — the lifted-input continuation never runs.  Windows pairs
 * each duplex read completion with the connection's own 10 ms read-loop tick.
 * Arm a self-re-arming 10 ms timer per duplex end once a duplex read completes
 * with no queued read; every tick re-creates the "data arrived" mirror byte
 * (the same edge pipe_end_write rings at the peer), wakes default_poll_event →
 * pipe_end_poll_event → reselect_read_queue, and the parked read completes
 * empty → rpcrt4 re-arms → the app pump gets a beat.  Created lazily on the
 * first completion, lives for the pipe's lifetime (a handful of OLE_ duplex
 * instances per run; the drain work per tick is a list_empty check). */
static void mirror_heartbeat( void *private )
{
    struct pipe_end *pipe_end = private;

    set_fd_signaled( pipe_end->fd, 1 );
    pipe_end->hb_timeout = add_timeout_user( -10000000 /* 10 ms, monotonic ns */, mirror_heartbeat, pipe_end );
}

static void arm_mirror_heartbeat( struct pipe_end *pipe_end )
{
    if (pipe_end->hb_timeout) return;
    pipe_end->hb_timeout = add_timeout_user( -10000000, mirror_heartbeat, pipe_end );
}

static void arm_mirror_continuation( struct pipe_end *pipe_end )
{
    if (!pipe_end->pipe || pipe_end->pipe->sharing != (FILE_SHARE_READ | FILE_SHARE_WRITE)) return;
    if (!(pipe_end->cont_armed = !list_empty( &pipe_end->message_queue ))) return;
    fd_set_mirror_poll( pipe_end->fd );
    set_fd_events( pipe_end->fd, pipe_end_get_poll_events( pipe_end->fd ) );
}

static void message_queue_read( struct pipe_end *pipe_end, struct async *async )
{
    struct iosb *iosb = async_get_iosb( async );
    unsigned int status = STATUS_SUCCESS;
    struct pipe_message *message;
    data_size_t out_size;

    if (pipe_end->flags & NAMED_PIPE_MESSAGE_STREAM_READ)
    {
        message = LIST_ENTRY( list_head(&pipe_end->message_queue), struct pipe_message, entry );
        out_size = min( iosb->out_size, message->iosb->in_size - message->read_pos );

        if (message->read_pos + out_size < message->iosb->in_size)
            status = STATUS_BUFFER_OVERFLOW;
    }
    else
    {
        data_size_t avail = 0;
        LIST_FOR_EACH_ENTRY( message, &pipe_end->message_queue, struct pipe_message, entry )
        {
            avail += message->iosb->in_size - message->read_pos;
            if (avail >= iosb->out_size) break;
        }
        out_size = min( iosb->out_size, avail );
    }

    dupx_io( "R", pipe_end, out_size );
    message = LIST_ENTRY( list_head(&pipe_end->message_queue), struct pipe_message, entry );
    if (!message->read_pos && message->iosb->in_size == iosb->out_size) /* fast path */
    {
        relay_patch( message, async_get_thread( async )->process, 0, message->iosb->in_size,
                     message->iosb->in_data );
        wapipex_dump( "R", pipe_end, async_get_thread( async )->process->id,
                      message->iosb->in_data, message->iosb->in_size );
        async_request_complete( async, status, out_size, out_size, message->iosb->in_data );
        message->iosb->in_data = NULL;
        relay_free( message, !relay_no_close );  /* fully consumed: relay closes the source */
        wake_message( message, message->iosb->in_size );
        free_message( message );
    }
    else
    {
        data_size_t write_pos = 0, writing;
        char *buf = NULL;

        if (out_size && !(buf = malloc( out_size )))
        {
            async_terminate( async, STATUS_NO_MEMORY );
            release_object( iosb );
            return;
        }

        do
        {
            message = LIST_ENTRY( list_head(&pipe_end->message_queue), struct pipe_message, entry );
            writing = min( out_size - write_pos, message->iosb->in_size - message->read_pos );
            if (writing)
            {
                memcpy( buf + write_pos, (const char *)message->iosb->in_data + message->read_pos, writing );
                relay_patch( message, async_get_thread( async )->process, message->read_pos, writing,
                             buf + write_pos );
            }
            write_pos += writing;
            message->read_pos += writing;
            if (message->read_pos == message->iosb->in_size)
            {
                relay_free( message, !relay_no_close );  /* fully consumed: relay closes the source */
                wake_message(message, message->iosb->in_size);
                free_message(message);
            }
        } while (write_pos < out_size);

        wapipex_dump( "R", pipe_end, async_get_thread( async )->process->id, buf, out_size );

        /* A read that stops short of the message end is not the end of the
         * message: message mode is read as a stream here, with message->read_pos
         * carried across requests, and the caller keeps reading until the whole
         * message is delivered.  rpcrt4's servers read a received packet as
         * several requests (16-byte common header, rest of the header, payload),
         * so the first read of a bind packet is always short of its end.  Freeing
         * the message here destroyed the packet between those reads: the reader
         * got its leading bytes, the following read found nothing, and the RPC
         * call died with RPC_S_CALL_FAILED_DELAY (1726) - which surfaces as
         * OpenSCManagerW() failing with 1722/1726 and services.exe logging
         * "receiving command result timed out" for every service it starts.
         * Leave the remainder queued; the delivered size is reported from the
         * completion, which is what 1a5440320 needed in the first place. */
        async_request_complete( async, status, out_size, out_size, buf );
    }

    /* A duplex message-mode read that stopped mid-message (STATUS_BUFFER_OVERFLOW
     * stream read, or a byte-size buffer filled inside one message) leaves the head
     * message queued with read_pos advanced: the client's next REQ_read delivers the
     * remainder.  Keep this end's inproc-sync mirror armed for that continuation:
     * the fd has just lost its waiting async, so default_fd_get_poll_events() would
     * disarm the mirror (mask 0) and the queued remainder would only drain on the
     * next REQ_read's own reselect — one chunk per wake — stranding the 7th LRPC
     * request (composition-input adapter) on its header/body split.  With the
     * continuation armed, any relayed edge (and the queued read's arrival) drains
     * the remaining bytes immediately, without needing a new peer write edge. */
    arm_mirror_continuation( pipe_end );

    /* gate104 lever (b): duplex read completed with no queued reader behind it —
     * start the 10 ms mirror heartbeat so the parked LRPC idle receive wakes on a
     * beat and rpcrt4 re-arms (request 7 issues). */
    if (pipe_end->pipe && pipe_end->pipe->sharing == (FILE_SHARE_READ | FILE_SHARE_WRITE) &&
        list_empty( &pipe_end->read_q.queue ))
        arm_mirror_heartbeat( pipe_end );

    release_object( iosb );
}

/* We call async_terminate in our reselect implementation, which causes recursive reselect.
 * We're not interested in such reselect calls, so we ignore them. */
static int ignore_reselect;

static void reselect_write_queue( struct pipe_end *pipe_end );

static void reselect_read_queue( struct pipe_end *pipe_end, int reselect_write )
{
    struct async *async;

    ignore_reselect = 1;
    while (!list_empty( &pipe_end->message_queue ) && (async = find_pending_async( &pipe_end->read_q )))
    {
        message_queue_read( pipe_end, async );
        release_object( async );
        reselect_write = 1;
    }
    ignore_reselect = 0;

    if (!list_empty( &pipe_end->message_queue ))
        arm_mirror_continuation( pipe_end );  /* continuation still pending: keep waking */

    if (pipe_end->connection)
    {
        if (list_empty( &pipe_end->message_queue ))
        {
            pipe_end->cont_armed = 0;
            fd_async_wake_up( pipe_end->connection->fd, ASYNC_TYPE_WAIT, STATUS_SUCCESS );
        }
        else if (reselect_write)
            reselect_write_queue( pipe_end->connection );
    }
}

/* poll event for a pipe end's (pseudo) fd: the pollable fd is the inproc-sync
 * mirror (fd.c fd_poll_fd), whose byte is the peer's "data arrived" edge.  The
 * default wake (ALERTED) makes the client re-read the pseudo fd's unix fd (-1):
 * that read returns garbage on duplex ends and cost the pipe suite 614 failures.
 * Relay the edge in-server instead: drain the mirror byte, then run the
 * message-queue drain against the queued reads. */
static void pipe_end_poll_event( struct fd *fd, int event )
{
    struct pipe_end *pipe_end = get_fd_user( fd );

    if (event & (POLLIN | POLLERR | POLLHUP))
    {
        fd_mirror_reset( fd );
        ignore_reselect = 1;
        reselect_read_queue( pipe_end, 1 );
        ignore_reselect = 0;
    }

    set_fd_events( fd, pipe_end_get_poll_events( fd ) );
}

static int pipe_end_get_poll_events( struct fd *fd )
{
    struct pipe_end *pipe_end = get_fd_user( fd );
    int events = default_fd_get_poll_events( fd );

    /* a message-mode read parked mid-message (see arm_mirror_continuation) must
     * keep the mirror's POLLIN armed across the header/body split */
    if (pipe_end->cont_armed) events |= POLLIN;
    return events;
}

static void reselect_write_queue( struct pipe_end *pipe_end )
{
    struct pipe_message *message, *next;
    struct pipe_end *reader = pipe_end->connection;
    data_size_t avail = 0;

    if (!reader) return;

    ignore_reselect = 1;

    LIST_FOR_EACH_ENTRY_SAFE( message, next, &reader->message_queue, struct pipe_message, entry )
    {
        if (message->async && message->iosb->status != STATUS_PENDING)
        {
            release_object( message->async );
            message->async = NULL;
            free_message( message );
        }
        else
        {
            avail += message->iosb->in_size - message->read_pos;
            if (message->async && (avail <= reader->buffer_size || !message->iosb->in_size))
            {
                wake_message( message, message->iosb->in_size );
            }
            else if (message->async && (pipe_end->flags & NAMED_PIPE_NONBLOCKING_MODE))
            {
                wake_message( message, message->read_pos );
                free_message( message );
            }
        }
    }

    ignore_reselect = 0;
    reselect_read_queue( reader, 0 );
}

static void pipe_end_read( struct fd *fd, struct async *async, file_pos_t pos )
{
    struct pipe_end *pipe_end = get_fd_user( fd );
    switch (pipe_end->state)
    {
    case FILE_PIPE_CONNECTED_STATE:
        if ((pipe_end->flags & NAMED_PIPE_NONBLOCKING_MODE) && list_empty( &pipe_end->message_queue ))
        {
            set_error( STATUS_PIPE_EMPTY );
            return;
        }
        break;
    case FILE_PIPE_DISCONNECTED_STATE:
        set_error( STATUS_PIPE_DISCONNECTED );
        return;
    case FILE_PIPE_LISTENING_STATE:
        set_error( STATUS_PIPE_LISTENING );
        return;
    case FILE_PIPE_CLOSING_STATE:
        if (!list_empty( &pipe_end->message_queue )) break;
        set_error( STATUS_PIPE_BROKEN );
        return;
    }

    queue_async( &pipe_end->read_q, async );
    reselect_read_queue( pipe_end, 0 );
    /* gate112: reader-arrival census.  reselect_read_queue above already drained the
     * messages queued on this end (message_queue_read → relay_patch flushes every box
     * whose slot lands in the delivered window), so the depth printed here is the
     * backlog that arrived *after* the last delivery — the proof for the S≫D census:
     * if it stays 0, the S lines are the box-less AcceptParcel/id-slot scans, not
     * un-flushed boxes. */
    if (relay_wanted( pipe_end )) relay_queue_census( pipe_end, "R" );
    /* A duplex pipe whose peer wrote before this read was queued, with the
     * queued async list drained at write time, has its data in the kernel
     * pipe already; re-check the fd poll state so the async completes on the
     * readability edge the kernel pipe has been holding.  A duplex instance
     * shares one kernel pipe pair between its two ends: a read queued on this
     * end can also be waiting for data that the peer end's queued read has
     * already made readable, so re-arm the paired end's read queue too. */
    if (pipe_end->pipe && pipe_end->pipe->sharing == (FILE_SHARE_READ | FILE_SHARE_WRITE))
    {
        fd_reselect_async( pipe_end->fd, &pipe_end->read_q );
        if (pipe_end->connection)
            fd_reselect_async( pipe_end->connection->fd, &pipe_end->connection->read_q );
        /* With a read queued and no message to drain into it, arm this end's
         * poll entry: the pipe end rides a pseudo fd (unix_fd == -1), so the
         * poll fd is the inproc-sync mirror's read side (fd.c fd_poll_fd) —
         * the same kernel pipe the peer's set_fd_signaled writes a byte into.
         * That byte is the data-arrived edge for the queued read: the poll
         * wakes default_poll_event, which ALERTS the read queue and the
         * drained message rides out on the async's reply.  Without this the
         * read queue was only ever re-checked when the next REQ_write arrived
         * on the *writing* side, and the 7th LRPC request stranded both ends
         * PENDING with the client's request socketpair full. */
        fd_set_mirror_poll( pipe_end->fd );
        set_fd_events( pipe_end->fd, default_fd_get_poll_events( pipe_end->fd ) );
    }
    set_error( STATUS_PENDING );
}

static void pipe_end_write( struct fd *fd, struct async *async, file_pos_t pos )
{
    struct pipe_end *pipe_end = get_fd_user( fd );
    struct pipe_message *message;
    struct iosb *iosb;
    switch (pipe_end->state)
    {
    case FILE_PIPE_CONNECTED_STATE:
        break;
    case FILE_PIPE_DISCONNECTED_STATE:
        set_error( STATUS_PIPE_DISCONNECTED );
        return;
    case FILE_PIPE_LISTENING_STATE:
        set_error( STATUS_PIPE_LISTENING );
        return;
    case FILE_PIPE_CLOSING_STATE:
        set_error( STATUS_PIPE_CLOSING );
        return;
    }

    if (!pipe_end->pipe->message_mode && !get_req_data_size()) return;

    wapipex_dump( "W", pipe_end, current->process->id, get_req_data(), get_req_data_size() );
    dupx_io( "W", pipe_end, get_req_data_size() );

    iosb = async_get_iosb( async );
    message = queue_message( pipe_end->connection, iosb );
    release_object( iosb );
    if (!message) return;

    /* gate112: flush-on-write doorbell.  A relayed frame (AcceptParcel included — the
     * box-less op=20 is the ring data doorbell itself) riding this write must ring the
     * reader's wake edge now; gate111 deferred every pulse to the next *boxed* message
     * delivery (N=2/X=6, all in the handshake window), leaving the browser's 105
     * AcceptParcels + the closing op=36 FlushRouter queued with nobody woken
     * (ring head static at 0 of 12, w4=0). */
    if (relay_scan( pipe_end, message, get_req_data(), get_req_data_size() ))
    {
        relay_queue_census( pipe_end->connection, "W" );
        relay_doorbell( pipe_end->connection, "W" );
    }

    message->async = (struct async *)grab_object( async );
    queue_async( &pipe_end->write_q, async );
    reselect_read_queue( pipe_end->connection, 1 );
    /* Duplex (self-apartment LRPC) pipes: the reader parks on the inproc-sync
     * mirror fd (read( fd, 16 )) with no wineserver-side event; a queued
     * message that no queued read consumed leaves the reader asleep and the
     * writer's next write blocks the kernel pair.  Nudge the reader's sync
     * mirror so it wakes and picks the message up. */
    if (pipe_end->pipe->sharing == (FILE_SHARE_READ | FILE_SHARE_WRITE) &&
        !list_empty( &pipe_end->connection->message_queue ))
        set_fd_signaled( pipe_end->connection->fd, 1 );
    set_error( STATUS_PENDING );
}

static void pipe_end_reselect_async( struct fd *fd, struct async_queue *queue )
{
    struct pipe_end *pipe_end = get_fd_user( fd );

    if (ignore_reselect) return;

    if (&pipe_end->write_q == queue)
        reselect_write_queue( pipe_end );
    else if (&pipe_end->read_q == queue)
        reselect_read_queue( pipe_end, 0 );
}

static enum server_fd_type pipe_end_get_fd_type( struct fd *fd )
{
    return FD_TYPE_DEVICE;
}

static void pipe_end_peek( struct pipe_end *pipe_end )
{
    unsigned reply_size = get_reply_max_size();
    FILE_PIPE_PEEK_BUFFER *buffer;
    struct pipe_message *message;
    data_size_t avail = 0;
    data_size_t message_length = 0;

    if (reply_size < offsetof( FILE_PIPE_PEEK_BUFFER, Data ))
    {
        set_error( STATUS_INFO_LENGTH_MISMATCH );
        return;
    }
    reply_size -= offsetof( FILE_PIPE_PEEK_BUFFER, Data );

    switch (pipe_end->state)
    {
    case FILE_PIPE_CONNECTED_STATE:
        break;
    case FILE_PIPE_CLOSING_STATE:
        if (!list_empty( &pipe_end->message_queue )) break;
        set_error( STATUS_PIPE_BROKEN );
        return;
    default:
        set_error( pipe_end->pipe ? STATUS_INVALID_PIPE_STATE : STATUS_PIPE_DISCONNECTED );
        return;
    }

    LIST_FOR_EACH_ENTRY( message, &pipe_end->message_queue, struct pipe_message, entry )
        avail += message->iosb->in_size - message->read_pos;
    reply_size = min( reply_size, avail );

    if (avail && pipe_end->pipe->message_mode)
    {
        message = LIST_ENTRY( list_head(&pipe_end->message_queue), struct pipe_message, entry );
        message_length = message->iosb->in_size - message->read_pos;
        reply_size = min( reply_size, message_length );
    }

    if (!(buffer = set_reply_data_size( offsetof( FILE_PIPE_PEEK_BUFFER, Data[reply_size] )))) return;
    buffer->NamedPipeState    = pipe_end->state;
    buffer->ReadDataAvailable = avail;
    buffer->NumberOfMessages  = 0;  /* FIXME */
    buffer->MessageLength     = message_length;

    if (reply_size)
    {
        data_size_t write_pos = 0, writing;
        LIST_FOR_EACH_ENTRY( message, &pipe_end->message_queue, struct pipe_message, entry )
        {
            writing = min( reply_size - write_pos, message->iosb->in_size - message->read_pos );
            memcpy( buffer->Data + write_pos, (const char *)message->iosb->in_data + message->read_pos,
                    writing );
            relay_patch( message, current->process, message->read_pos, writing, buffer->Data + write_pos );
            write_pos += writing;
            if (write_pos == reply_size) break;
        }
    }
    if (reply_size)
        wapipex_dump( "P", pipe_end, current->process->id, buffer->Data, reply_size );
    if (message_length > reply_size) set_error( STATUS_BUFFER_OVERFLOW );
}

static void pipe_end_transceive( struct pipe_end *pipe_end, struct async *async )
{
    struct pipe_message *message;
    struct iosb *iosb;
    const WCHAR *pname;
    data_size_t plen;

    if (wv2io_wanted( pipe_end ))
        WV2IO( "T pid=%04x ep=%p (transceive armed)\n", current->process->id, pipe_end );

    if (!pipe_end->connection)
    {
        set_error( pipe_end->pipe ? STATUS_INVALID_PIPE_STATE : STATUS_PIPE_DISCONNECTED );
        if (wv2io_wanted( pipe_end ))
            WV2IO( "T pid=%04x ep=%p st=%x (no connection)\n", current->process->id, pipe_end,
                   pipe_end->pipe ? STATUS_INVALID_PIPE_STATE : STATUS_PIPE_DISCONNECTED );
        return;
    }

    if (!(pipe_end->flags & NAMED_PIPE_MESSAGE_STREAM_READ))
    {
        set_error( STATUS_INVALID_READ_MODE );
        if (wv2io_wanted( pipe_end ))
            WV2IO( "T pid=%04x ep=%p st=%x (read mode)\n", current->process->id, pipe_end,
                   (unsigned int)STATUS_INVALID_READ_MODE );
        return;
    }

    /* not allowed if we already have read data buffered */
    if (!list_empty( &pipe_end->message_queue ))
    {
        set_error( STATUS_PIPE_BUSY );
        if (wv2io_wanted( pipe_end ))
            WV2IO( "T pid=%04x ep=%p st=%x (busy)\n", current->process->id, pipe_end,
                   (unsigned int)STATUS_PIPE_BUSY );
        return;
    }

    iosb = async_get_iosb( async );
    /* ignore output buffer copy transferred because of METHOD_NEITHER */
    iosb->in_size -= iosb->out_size;
    wapipex_dump( "W", pipe_end, current->process->id, iosb->in_data, iosb->in_size );
    /* transaction never blocks on write, so just queue a message without async */
    message = queue_message( pipe_end->connection, iosb );
    release_object( iosb );
    if (!message)
    {
        return;
    }
    if (relay_scan( pipe_end, message, message->iosb->in_data, message->iosb->in_size ))
    {
        relay_queue_census( pipe_end->connection, "W" );  /* gate112: transceive write plane too */
        relay_doorbell( pipe_end->connection, "W" );
    }
    reselect_read_queue( pipe_end->connection, 0 );

    queue_async( &pipe_end->read_q, async );
    reselect_read_queue( pipe_end, 0 );
    set_error( STATUS_PENDING );
}

/* Peer ids are served from the instance cache, not from a live connection: an end keeps
 * them queryable while its connection was torn down by a handle close, and stops serving
 * them once the server explicitly disconnected the instance.  A never-connected instance
 * keeps reporting the pipe as unavailable. */
static int pipe_end_connection_info_available( struct pipe_end *pipe_end )
{
    if (pipe_end->conn_info == CONN_INFO_VALID) return 1;
    if (pipe_end->conn_info == CONN_INFO_CLEARED)
        set_error( pipe_end->obj.ops == &pipe_server_ops ? STATUS_NOT_FOUND : STATUS_PIPE_DISCONNECTED );
    else set_error( STATUS_PIPE_NOT_AVAILABLE );
    return 0;
}

static void pipe_end_get_connection_attribute( struct pipe_end *pipe_end )
{
    const char *attr = get_req_data();
    data_size_t value_size, attr_size = get_req_data_size();
    void *value;

    if (attr_size == sizeof("ClientProcessId") && !memcmp( attr, "ClientProcessId", attr_size ))
    {
        if (!pipe_end_connection_info_available( pipe_end )) return;
        value = &pipe_end->client_pid;
        value_size = sizeof(pipe_end->client_pid);
    }
    else if (attr_size == sizeof("ServerProcessId") && !memcmp( attr, "ServerProcessId", attr_size ))
    {
        /* the server pid of a server end is its own creator, always known */
        if (pipe_end->obj.ops == &pipe_client_ops && !pipe_end_connection_info_available( pipe_end )) return;
        value = &pipe_end->server_pid;
        value_size = sizeof(pipe_end->server_pid);
    }
    else if (attr_size == sizeof("ClientSessionId") && !memcmp( attr, "ClientSessionId", attr_size ))
    {
        if (!pipe_end_connection_info_available( pipe_end )) return;
        value = &pipe_end->client_session_id;
        value_size = sizeof(pipe_end->client_session_id);
    }
    else if (attr_size == sizeof("ServerSessionId") && !memcmp( attr, "ServerSessionId", attr_size ))
    {
        if (pipe_end->obj.ops == &pipe_client_ops && !pipe_end_connection_info_available( pipe_end )) return;
        value = &pipe_end->server_session_id;
        value_size = sizeof(pipe_end->server_session_id);
    }
    else
    {
        set_error( STATUS_ILLEGAL_FUNCTION );
        return;
    }

    if (get_reply_max_size() < value_size)
    {
        set_error( STATUS_INFO_LENGTH_MISMATCH );
        return;
    }

    set_reply_data( value, value_size );
}

static void pipe_end_ioctl( struct pipe_end *pipe_end, ioctl_code_t code, struct async *async )
{
    switch(code)
    {
    case FSCTL_PIPE_GET_CONNECTION_ATTRIBUTE:
        pipe_end_get_connection_attribute( pipe_end );
        break;

    case FSCTL_PIPE_PEEK:
        pipe_end_peek( pipe_end );
        break;

    case FSCTL_PIPE_TRANSCEIVE:
        pipe_end_transceive( pipe_end, async );
        break;

    default:
        default_fd_ioctl( pipe_end->fd, code, async );
    }
}

static void pipe_server_ioctl( struct fd *fd, ioctl_code_t code, struct async *async )
{
    struct pipe_server *server = get_fd_user( fd );

    switch(code)
    {
    case FSCTL_PIPE_LISTEN:
        switch(server->pipe_end.state)
        {
        case FILE_PIPE_LISTENING_STATE:
            break;
        case FILE_PIPE_DISCONNECTED_STATE:
            server->pipe_end.state = FILE_PIPE_LISTENING_STATE;
            list_add_tail( &server->pipe_end.pipe->listeners, &server->entry );
            dupx_dump( "LISTEN", server->pipe_end.pipe, &server->pipe_end, NULL );
            if (wv2io_wanted( &server->pipe_end ))
                WV2IO( "S pid=%04x ep=%p st=L (relisten)\n", current->process->id, &server->pipe_end );
            break;
        case FILE_PIPE_CONNECTED_STATE:
            set_error( STATUS_PIPE_CONNECTED );
            return;
        case FILE_PIPE_CLOSING_STATE:
            set_error( STATUS_PIPE_CLOSING );
            return;
        }

        if (server->pipe_end.flags & NAMED_PIPE_NONBLOCKING_MODE)
        {
            set_error( STATUS_PIPE_LISTENING );
            return;
        }
        queue_async( &server->listen_q, async );
        async_wake_up( &server->pipe_end.pipe->waiters, STATUS_SUCCESS );
        set_error( STATUS_PENDING );
        return;

    case FSCTL_PIPE_DISCONNECT:
        switch(server->pipe_end.state)
        {
        case FILE_PIPE_CONNECTED_STATE:
            /* dump the client connection - all data is lost */
            assert( server->pipe_end.connection );
            release_object( server->pipe_end.connection->pipe );
            server->pipe_end.connection->pipe = NULL;
            break;
        case FILE_PIPE_CLOSING_STATE:
            break;
        case FILE_PIPE_LISTENING_STATE:
            set_error( STATUS_PIPE_LISTENING );
            return;
        case FILE_PIPE_DISCONNECTED_STATE:
            set_error( STATUS_PIPE_DISCONNECTED );
            return;
        }

        pipe_end_disconnect( &server->pipe_end, STATUS_PIPE_DISCONNECTED );
        return;

    case FSCTL_PIPE_IMPERSONATE:
        {
            /* Impersonation runs from the client process cached on the instance, which
             * survives the client's handle close - Windows keeps impersonating after the
             * client has exited.  Before any client connected the request reports
             * ERROR_CANNOT_IMPERSONATE, not ERROR_BAD_PIPE. */
            struct process *client_proc = server->pipe_end.client_proc;
            struct token *token;

            if (!client_proc)
            {
                set_error( STATUS_CANNOT_IMPERSONATE );
                return;
            }
            if (client_proc->token)
            {
                if (!(token = token_duplicate( client_proc->token, 0, SecurityImpersonation,
                                               NULL, NULL, 0, NULL, 0, 0 )))
                    return;
                if (current->token) release_object( current->token );
                current->token = token;
            }
        }
        return;

    default:
        pipe_end_ioctl( &server->pipe_end, code, async );
    }
}

static void pipe_client_ioctl( struct fd *fd, ioctl_code_t code, struct async *async )
{
    struct pipe_end *client = get_fd_user( fd );

    switch(code)
    {
    case FSCTL_PIPE_LISTEN:
        set_error( STATUS_ILLEGAL_FUNCTION );
        return;

    default:
        pipe_end_ioctl( client, code, async );
    }
}

static void init_pipe_end( struct pipe_end *pipe_end, struct named_pipe *pipe,
                           unsigned int pipe_flags, data_size_t buffer_size )
{
    pipe_end->pipe = (struct named_pipe *)grab_object( pipe );
    pipe_end->fd = NULL;
    pipe_end->flags = pipe_flags;
    pipe_end->connection = NULL;
    pipe_end->client_pid = 0;
    pipe_end->server_pid = 0;
    pipe_end->client_session_id = 0;
    pipe_end->server_session_id = 0;
    pipe_end->client_proc = NULL;
    pipe_end->conn_info = CONN_INFO_NONE;
    pipe_end->buffer_size = buffer_size;
    pipe_end->blk_fd = -1;
    pipe_end->ring_fd = -1;
    pipe_end->ev_obj = NULL;
    pipe_end->hb_timeout = NULL;  /* gate108: gate104 added the heartbeat timer without
                                   * initializing it here — pipe_end_destroy's
                                   * remove_timeout_user ate a garbage pointer on every
                                   * server teardown (startup segfault, gate108) */
    init_async_queue( &pipe_end->read_q );
    init_async_queue( &pipe_end->write_q );
    list_init( &pipe_end->message_queue );
}

static struct pipe_server *create_pipe_server( struct named_pipe *pipe, unsigned int options,
                                               unsigned int pipe_flags )
{
    struct pipe_server *server;

    server = alloc_object( &pipe_server_ops );
    if (!server)
        return NULL;

    server->options = options;
    init_pipe_end( &server->pipe_end, pipe, pipe_flags, pipe->insize );
    server->pipe_end.state = FILE_PIPE_LISTENING_STATE;
    server->pipe_end.server_pid = get_process_id( current->process );
    server->pipe_end.server_session_id = current->process->session_id;
    init_async_queue( &server->listen_q );

    list_add_tail( &pipe->listeners, &server->entry );
    dupx_dump( "CREATE", pipe, &server->pipe_end, NULL );
    if (!(server->pipe_end.fd = alloc_pseudo_fd( &pipe_server_fd_ops, &server->pipe_end.obj, options )))
    {
        release_object( server );
        return NULL;
    }
    allow_fd_caching( server->pipe_end.fd );
    set_fd_signaled( server->pipe_end.fd, 1 );
    async_wake_up( &pipe->waiters, STATUS_SUCCESS );
    return server;
}

static struct pipe_end *create_pipe_client( struct named_pipe *pipe, data_size_t buffer_size, unsigned int options )
{
    struct pipe_end *client;

    client = alloc_object( &pipe_client_ops );
    if (!client)
        return NULL;

    /* The read mode of a connected client handle starts out as pipe, independent of
     * the read mode recorded in the create specification of the pipe's server end.
     * Inheriting the specification here made byte-mode clients take the message-mode
     * read path: a read smaller than the queued message returned STATUS_BUFFER_OVERFLOW
     * instead of the requested bytes, and the peer write that fed it was reported
     * pending with its event never signaled and its APC never delivered. */
    init_pipe_end( client, pipe, 0, buffer_size );
    client->state = FILE_PIPE_CONNECTED_STATE;
    client->client_pid = get_process_id( current->process );
    client->client_session_id = current->process->session_id;
    client->client_proc = (struct process *)grab_object( current->process );

    client->fd = alloc_pseudo_fd( &pipe_client_fd_ops, &client->obj, options );
    if (!client->fd)
    {
        release_object( client );
        return NULL;
    }
    allow_fd_caching( client->fd );
    set_fd_signaled( client->fd, 1 );

    return client;
}

static int named_pipe_link_name( struct object *obj, struct object_name *name, struct object *parent )
{
    if (parent->ops == &named_pipe_dir_ops) parent = &((struct named_pipe_device_file *)parent)->device->obj;
    if (parent->ops != &named_pipe_device_ops)
    {
        set_error( STATUS_OBJECT_NAME_INVALID );
        return 0;
    }
    namespace_add( ((struct named_pipe_device *)parent)->pipes, name );
    name->parent = grab_object( parent );
    return 1;
}

static struct object *named_pipe_open_file( struct object *obj, unsigned int access,
                                            unsigned int sharing, unsigned int options )
{
    struct named_pipe *pipe = (struct named_pipe *)obj;
    struct pipe_server *server;
    struct pipe_end *client;
    unsigned int pipe_sharing;
    if (list_empty( &pipe->listeners ))
    {
        set_error( STATUS_PIPE_NOT_AVAILABLE );
        return NULL;
    }
    /* Pair the client with the oldest queued listener (FIFO), matching the
     * Windows named-pipe instance order that kernel32:pipe asserts: new
     * instances are appended at the tail (create_pipe_server) and a
     * disconnect+listen reuse is re-appended at the tail, so the head is the
     * instance the next client open must connect (ConnectNamedPipe on it then
     * completes with ERROR_PIPE_CONNECTED).  Binding the tail made pairing
     * LIFO and shifted every duplex multi-instance ordering check by one. */
    server = LIST_ENTRY( list_head( &pipe->listeners ), struct pipe_server, entry );

    pipe_sharing = pipe->sharing;
    if (((access & GENERIC_READ) && !(pipe_sharing & FILE_SHARE_READ)) ||
        ((access & GENERIC_WRITE) && !(pipe_sharing & FILE_SHARE_WRITE)))
    {
        set_error( STATUS_ACCESS_DENIED );
        return NULL;
    }

    if ((client = create_pipe_client( pipe, pipe->outsize, options )))
    {
        async_wake_up( &server->listen_q, STATUS_SUCCESS );
        server->pipe_end.state = FILE_PIPE_CONNECTED_STATE;
        server->pipe_end.connection = client;
        client->connection = &server->pipe_end;
        /* a reused instance (disconnect + listen) caches the new client only */
        if (server->pipe_end.client_proc) release_object( server->pipe_end.client_proc );
        server->pipe_end.client_proc = (struct process *)grab_object( client->client_proc );
        server->pipe_end.client_pid = client->client_proc->id;
        server->pipe_end.client_session_id = client->client_proc->session_id;
        client->server_pid = server->pipe_end.server_pid;
        client->server_session_id = server->pipe_end.server_session_id;
        server->pipe_end.conn_info = CONN_INFO_VALID;
        client->conn_info = CONN_INFO_VALID;
        list_remove( &server->entry );
        dupx_dump( "OPEN", pipe, &server->pipe_end, client );
        if (wv2io_wanted( client ))
        {
            char tail[24];
            wv2io_tail( pipe, tail, sizeof(tail) );
            WV2IO( "X srv=%04x cli=%04x ep=%p tail=%s\n",
                   server->pipe_end.server_pid, client->client_pid, client, tail );
        }
    }
    return &client->obj;
}

static void named_pipe_device_ioctl( struct fd *fd, ioctl_code_t code, struct async *async )
{
    switch(code)
    {
    case FSCTL_PIPE_WAIT:
    case FSCTL_PIPE_LISTEN:
    case FSCTL_PIPE_IMPERSONATE:
        set_error( STATUS_ILLEGAL_FUNCTION );
        return;

    case FSCTL_PIPE_DISCONNECT:
    case FSCTL_PIPE_TRANSCEIVE:
        set_error( STATUS_PIPE_DISCONNECTED );
        return;

    case FSCTL_PIPE_QUERY_CLIENT_PROCESS:
        set_error( STATUS_INVALID_PARAMETER );
        return;

    default:
        default_fd_ioctl( fd, code, async );
    }
}

static void named_pipe_dir_ioctl( struct fd *fd, ioctl_code_t code, struct async *async )
{
    struct named_pipe_device *device = get_fd_user( fd );

    switch(code)
    {
    case FSCTL_PIPE_WAIT:
        {
            const FILE_PIPE_WAIT_FOR_BUFFER *buffer = get_req_data();
            data_size_t size = get_req_data_size();
            struct named_pipe *pipe;
            struct unicode_str name;
            timeout_t when;

            if (size < sizeof(*buffer) ||
                size < FIELD_OFFSET(FILE_PIPE_WAIT_FOR_BUFFER, Name[buffer->NameLength/sizeof(WCHAR)]))
            {
                set_error( STATUS_INVALID_PARAMETER );
                return;
            }
            name.str = buffer->Name;
            name.len = (buffer->NameLength / sizeof(WCHAR)) * sizeof(WCHAR);
            if (!(pipe = open_named_object( &device->obj, &named_pipe_ops, &name, 0 ))) return;

            if (list_empty( &pipe->listeners ))
            {
                queue_async( &pipe->waiters, async );
                when = buffer->TimeoutSpecified ? buffer->Timeout.QuadPart : pipe->timeout;
                async_set_timeout( async, when, STATUS_IO_TIMEOUT );
                set_error( STATUS_PENDING );
            }

            release_object( pipe );
            return;
        }

    default:
        named_pipe_device_ioctl( fd, code, async );
    }
}


DECL_HANDLER(create_named_pipe)
{
    struct named_pipe *pipe;
    struct pipe_server *server;
    struct unicode_str name;
    struct object *root;
    const struct security_descriptor *sd;
    const struct object_attributes *objattr = get_req_object_attributes( &sd, &name, &root );

    if (!objattr) return;

    if (!req->sharing || (req->sharing & ~(FILE_SHARE_READ | FILE_SHARE_WRITE)) ||
        (!(req->flags & NAMED_PIPE_MESSAGE_STREAM_WRITE) && (req->flags & NAMED_PIPE_MESSAGE_STREAM_READ)))
    {
        if (root) release_object( root );
        set_error( STATUS_INVALID_PARAMETER );
        return;
    }

    if (!req->access)
    {
        if (root) release_object( root );
        set_error( STATUS_ACCESS_DENIED );
        return;
    }

    if (!name.len)  /* pipes need a root directory even without a name */
    {
        if (!objattr->rootdir)
        {
            set_error( STATUS_OBJECT_PATH_SYNTAX_BAD );
            return;
        }
        if (!(root = get_handle_obj( current->process, objattr->rootdir, 0, NULL ))) return;
    }

    switch (req->disposition)
    {
    case FILE_OPEN:
        pipe = open_named_object( root, &named_pipe_ops, &name, objattr->attributes );
        break;
    case FILE_CREATE:
    case FILE_OPEN_IF:
        pipe = create_named_object( root, &named_pipe_ops, &name, objattr->attributes | OBJ_OPENIF, NULL );
        break;
    default:
        pipe = NULL;
        set_error( STATUS_INVALID_PARAMETER );
        break;
    }

    if (root) release_object( root );
    if (!pipe) return;

    if (get_error() != STATUS_OBJECT_NAME_EXISTS && req->disposition != FILE_OPEN)
    {
        /* initialize it if it didn't already exist */
        pipe->instances = 0;
        init_async_queue( &pipe->waiters );
        list_init( &pipe->listeners );
        pipe->insize = req->insize;
        pipe->outsize = req->outsize;
        pipe->maxinstances = req->maxinstances;
        pipe->timeout = req->timeout;
        pipe->message_mode = (req->flags & NAMED_PIPE_MESSAGE_STREAM_WRITE) != 0;
        pipe->message_read_mode = (req->flags & NAMED_PIPE_MESSAGE_STREAM_READ) != 0;
        pipe->sharing = req->sharing;
        if (sd) default_set_sd( &pipe->obj, sd, OWNER_SECURITY_INFORMATION |
                                                GROUP_SECURITY_INFORMATION |
                                                DACL_SECURITY_INFORMATION |
                                                SACL_SECURITY_INFORMATION );
        reply->created = 1;
    }
    else
    {
        if (req->flags & NAMED_PIPE_FIRST_INSTANCE)
        {
            set_error( STATUS_ACCESS_DENIED );
            release_object( pipe );
            return;
        }
        if (pipe->maxinstances <= pipe->instances)
        {
            set_error( STATUS_INSTANCE_NOT_AVAILABLE );
            release_object( pipe );
            return;
        }
        if (pipe->sharing != req->sharing || req->disposition == FILE_CREATE)
        {
            set_error( STATUS_ACCESS_DENIED );
            release_object( pipe );
            return;
        }
        clear_error(); /* clear the name collision */
    }

    server = create_pipe_server( pipe, req->options, req->flags );
    if (server)
    {
        reply->handle = alloc_handle( current->process, server, req->access, objattr->attributes );
        pipe->instances++;
        release_object( server );
    }

    release_object( pipe );
}

DECL_HANDLER(set_named_pipe_info)
{
    struct pipe_end *pipe_end;

    pipe_end = (struct pipe_end *)get_handle_obj( current->process, req->handle,
                                                  FILE_WRITE_ATTRIBUTES, &pipe_server_ops );
    if (!pipe_end)
    {
        if (get_error() != STATUS_OBJECT_TYPE_MISMATCH)
            return;

        clear_error();
        pipe_end = (struct pipe_end *)get_handle_obj( current->process, req->handle,
                                                      0, &pipe_client_ops );
        if (!pipe_end) return;
    }

    if (!pipe_end->pipe)
    {
        set_error( STATUS_PIPE_DISCONNECTED );
    }
    else if ((req->flags & ~(NAMED_PIPE_MESSAGE_STREAM_READ | NAMED_PIPE_NONBLOCKING_MODE)) ||
            ((req->flags & NAMED_PIPE_MESSAGE_STREAM_READ) && !pipe_end->pipe->message_mode))
    {
        set_error( STATUS_INVALID_PARAMETER );
    }
    else
    {
        pipe_end->flags = req->flags;
    }

    release_object( pipe_end );
}

DECL_HANDLER(get_named_pipe_shm)
{
    set_error( STATUS_NOT_IMPLEMENTED );
}
