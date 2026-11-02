/*
 * Copyright 2026 test harness for the WA_MOJO_RELAY wineserver extension
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Delivery-time relay of in-band handles on LOCAL\mojo.* invitation pipes.
 * Mirrors the WebView2 app<->browser inviter topology: the writer frames a
 * GetBufferResponse (op 22) / EstablishGroupChannelResponse (op 35) carrying a
 * raw process-local event handle in the body's handle slot (frame offset +48);
 * the peer process must be able to use the relayed value (SetEvent crossing
 * the process boundary signals the writer's event), the delivered bytes must
 * not contain the writer-local value, and the writer's source handle must be
 * closed at consumption (DUPLICATE_SAME_ACCESS|DUPLICATE_CLOSE_SOURCE, opts=3
 * as observed on the wire).
 *
 * This test is the gate for server/named_pipe.c's relay: it must run against
 * a wineserver started with WA_MOJO_RELAY=1; with the env unset the test
 * skips, with the env set against a relay-less server it fails (TDD RED).
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "windows.h"
#include "wine/test.h"

#define RELAY_ENV "WA_MOJO_RELAY"
static const char magic_tag[] = "mojo_relay_child";

/* frame layout on LOCAL\mojo.* pipes: 16B prologue + 24B mojo header + body */
#define FRAME_SIZE   64
#define SLOT_OFF     48   /* u64 in-band handle slot: body(+40) + 8 */

#define OP_GET_BUFFER_RESPONSE        22
#define OP_ESTABLISH_GROUP_RESP       35

/* status bits reported by the child */
#define B_CHILD_SET_PARENT_EVENT  0x01  /* child's SetEvent(relayed parent handle) succeeded */
#define B_PARENT_SET_CHILD_EV1    0x02  /* parent's SetEvent(relayed h1) signaled child's E1 */
#define B_PARENT_SET_CHILD_EV2    0x04  /* coalesced frame 2: E2 signaled (relay #2 in one write) */
#define B_PARENT_SET_CHILD_EV3    0x08  /* E3 signaled (single-frame write) */
#define B_CHILD_DUP_ALIVE         0x10  /* child's duplicate of the source event is usable */
#define B_CHILD_SRC_CLOSED        0x20  /* child's source handle closed at consumption (opts=3) */

#pragma pack(push, 1)
struct relay_status
{
    DWORD child_bits;
    DWORD child_seen_parent_value;  /* u32 the child extracted from the parent's frame */
    DWORD child_ev1_raw;            /* the child's E1 handle value as written */
};
#pragma pack(pop)

static void build_frame( unsigned char *f, unsigned int op, HANDLE handle )
{
    DWORD h = (DWORD)(DWORD_PTR)handle;

    memset( f, 0, FRAME_SIZE );
    f[0] = 0x10;                    /* prologue magic */
    f[4] = FRAME_SIZE;              /* frame length */
    f[16] = 24;                     /* mojo header size (u16) */
    f[18] = op;                     /* message type (u16) */
    f[40] = 0x18;                   /* body struct_size */
    f[SLOT_OFF + 0] = (unsigned char)(h);
    f[SLOT_OFF + 1] = (unsigned char)(h >> 8);
    f[SLOT_OFF + 2] = (unsigned char)(h >> 16);
    f[SLOT_OFF + 3] = (unsigned char)(h >> 24);
    f[56] = 1;                      /* trailing field: success / is_primary */
}

static DWORD frame_get_handle( const unsigned char *f )
{
    return f[SLOT_OFF] | (f[SLOT_OFF+1] << 8) | (f[SLOT_OFF+2] << 16) | ((DWORD)f[SLOT_OFF+3] << 24);
}

static void set_bit( DWORD *bits, DWORD bit, int condition )
{
    if (condition) *bits |= bit;
}

/* child role: server end of the mojo pipe, writes the handle-carrying frames */
static void relay_child( const char *pipe_name )
{
    struct relay_status status = { 0, 0, 0 };
    HANDLE pipe, ev1, ev2, ev3, ev1dup, ev2dup, ev3dup;
    unsigned char wbuf[2 * FRAME_SIZE], rbuf[512];
    DWORD got, mode;

    pipe = CreateNamedPipeA( pipe_name, PIPE_ACCESS_DUPLEX,
                             PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
                             1, 4096, 4096, 0, NULL );
    if (pipe == INVALID_HANDLE_VALUE)
    {
        fprintf( stderr, "relay_child: CreateNamedPipe(%s) err=%lu\n", pipe_name, GetLastError() );
        ExitProcess(2);
    }

    ev1 = CreateEventA( NULL, FALSE, FALSE, NULL );
    ev2 = CreateEventA( NULL, FALSE, FALSE, NULL );
    ev3 = CreateEventA( NULL, FALSE, FALSE, NULL );
    ev1dup = ev2dup = ev3dup = NULL;
    if (!DuplicateHandle( GetCurrentProcess(), ev1, GetCurrentProcess(), &ev1dup,
                          0, FALSE, DUPLICATE_SAME_ACCESS ) ||
        !DuplicateHandle( GetCurrentProcess(), ev2, GetCurrentProcess(), &ev2dup,
                          0, FALSE, DUPLICATE_SAME_ACCESS ) ||
        !DuplicateHandle( GetCurrentProcess(), ev3, GetCurrentProcess(), &ev3dup,
                          0, FALSE, DUPLICATE_SAME_ACCESS )) ExitProcess(3);

    if (!ConnectNamedPipe( pipe, NULL ) && GetLastError() != ERROR_PIPE_CONNECTED) ExitProcess(4);

    mode = PIPE_READMODE_MESSAGE;
    SetNamedPipeHandleState( pipe, &mode, NULL, NULL );

    fprintf( stderr, "child: connected\n" ); fflush( stderr );
    if (!ReadFile( pipe, rbuf, 2, &got, NULL ) || got != 2) ExitProcess(5);  /* 'GO' */

    /* one message with two coalesced frames, then one message with one frame */
    build_frame( wbuf + 0, OP_GET_BUFFER_RESPONSE, ev1 );
    build_frame( wbuf + FRAME_SIZE, OP_ESTABLISH_GROUP_RESP, ev2 );
    if (!WriteFile( pipe, wbuf, 2 * FRAME_SIZE, &got, NULL ) || got != 2 * FRAME_SIZE) ExitProcess(6);
    build_frame( wbuf, OP_GET_BUFFER_RESPONSE, ev3 );
    if (!WriteFile( pipe, wbuf, FRAME_SIZE, &got, NULL ) || got != FRAME_SIZE) ExitProcess(7);

    fprintf( stderr, "child: frames written\n" ); fflush( stderr );
    /* read the parent's frame: the value in the slot must be usable here */
    if (!ReadFile( pipe, rbuf, sizeof(rbuf), &got, NULL ) || got < SLOT_OFF + 8) ExitProcess(8);
    fprintf( stderr, "child: parent frame read got=%lu\n", got ); fflush( stderr );

    {
        HANDLE hp = (HANDLE)(DWORD_PTR)frame_get_handle( rbuf );
        int se = SetEvent( hp );
        fprintf( stderr, "child: hP=%p ev1=%p ev2=%p ev3=%p dup=%p SetEvent=%d err=%lu\n",
                 hp, ev1, ev2, ev3, ev1dup, se, GetLastError() );
        set_bit( &status.child_bits, B_CHILD_SET_PARENT_EVENT, se );
    }
    status.child_seen_parent_value = frame_get_handle( rbuf );
    status.child_ev1_raw = (DWORD)(DWORD_PTR)ev1;

    /* the source handles are closed by the relay at consumption (opts=3): observe
     * the objects through the duplicates made before the write */
    set_bit( &status.child_bits, B_PARENT_SET_CHILD_EV1,
             WaitForSingleObject( ev1dup, 3000 ) == WAIT_OBJECT_0 );
    set_bit( &status.child_bits, B_PARENT_SET_CHILD_EV2,
             WaitForSingleObject( ev2dup, 1000 ) == WAIT_OBJECT_0 );
    set_bit( &status.child_bits, B_PARENT_SET_CHILD_EV3,
             WaitForSingleObject( ev3dup, 1000 ) == WAIT_OBJECT_0 );
    set_bit( &status.child_bits, B_CHILD_DUP_ALIVE, SetEvent( ev1dup ) );
    set_bit( &status.child_bits, B_CHILD_SRC_CLOSED,
             WaitForSingleObject( ev1, 0 ) == WAIT_FAILED && GetLastError() == ERROR_INVALID_HANDLE );

    fprintf( stderr, "child: status bits=%08lx\n", status.child_bits ); fflush( stderr );
    WriteFile( pipe, &status, sizeof(status), &got, NULL );
    ExitProcess( 0 );
}

void func_mojo_relay( int argc, char **argv )
{
    char **args = NULL;
    struct relay_status status;
    int nargs = winetest_get_mainargs( &args );

    /* the harness runs 'kernel32_test.exe mojo_relay [magic <pipe>]' - the child
     * variant carries the marker past the test name and exits in its role */
    if (nargs >= 4 && !strcmp( args[1], "mojo_relay" ) && !strcmp( args[2], magic_tag ))
        relay_child( args[3] );
    PROCESS_INFORMATION pi;
    STARTUPINFOW si = { sizeof(si) };
    WCHAR self[MAX_PATH], cmdl[4 * MAX_PATH];
    char pipe_name[MAX_PATH], msg[2 * FRAME_SIZE];
    HANDLE pipe = INVALID_HANDLE_VALUE, pe1, pe1dup;
    DWORD got, mode, i, h1, h2, h3;
    int child_pipe_ok = 0;

    if (!GetEnvironmentVariableA( RELAY_ENV, NULL, 0 ))
    {
        win_skip( "%s not set: relay is inert outside the app gate\n", RELAY_ENV );
        return;
    }

    sprintf( pipe_name, "\\\\.\\pipe\\LOCAL\\mojo.relaytest.%u", (unsigned int)GetCurrentProcessId() );

    GetModuleFileNameW( NULL, self, MAX_PATH );
    wsprintfW( cmdl, L"\"%s\" mojo_relay mojo_relay_child %hs", self, pipe_name );
    memset( &pi, 0, sizeof(pi) );
    if (!CreateProcessW( NULL, cmdl, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi ))
    {
        win_skip( "could not spawn relay child, err %lu\n", GetLastError() );
        return;
    }

    fprintf( stderr, "parent: child spawned\n" ); fflush( stderr );
    for (i = 0; i < 100; i++)
    {
        pipe = CreateFileA( pipe_name, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL );
        if (pipe != INVALID_HANDLE_VALUE) { child_pipe_ok = 1; break; }
        Sleep( 50 );
    }
    ok( child_pipe_ok, "child pipe never became available\n" );
    if (!child_pipe_ok)
    {
        CloseHandle( pi.hProcess );
        CloseHandle( pi.hThread );
        return;
    }

    mode = PIPE_READMODE_MESSAGE;
    SetNamedPipeHandleState( pipe, &mode, NULL, NULL );
    pe1 = CreateEventA( NULL, FALSE, FALSE, NULL );
    pe1dup = NULL;
    DuplicateHandle( GetCurrentProcess(), pe1, GetCurrentProcess(), &pe1dup, 0, FALSE, DUPLICATE_SAME_ACCESS );

    fprintf( stderr, "parent: pipe open, writing GO\n" ); fflush( stderr );
    WriteFile( pipe, "GO", 2, &got, NULL );

    memset( msg, 0, sizeof(msg) );
    build_frame( (unsigned char *)msg + 0, OP_GET_BUFFER_RESPONSE, pe1 );
    WriteFile( pipe, msg, 2 * FRAME_SIZE, &got, NULL );

    fprintf( stderr, "parent: frames written, reading\n" ); fflush( stderr );
    /* read the coalesced pair and the single frame */
    if (!ReadFile( pipe, msg, sizeof(msg), &got, NULL )) got = 0;
    ok( got == 2 * FRAME_SIZE, "coalesced read got %lu bytes\n", got );
    if (got == 2 * FRAME_SIZE)
    {
        h1 = frame_get_handle( (const unsigned char *)msg );
        h2 = frame_get_handle( (const unsigned char *)msg + FRAME_SIZE );
    }
    else h1 = h2 = 0;

    if (!ReadFile( pipe, msg, sizeof(msg), &got, NULL )) got = 0;
    ok( got == FRAME_SIZE, "single frame read got %lu bytes\n", got );
    h3 = (got == FRAME_SIZE) ? frame_get_handle( (const unsigned char *)msg ) : 0;

    /* relayed handles must drive the child's events across the process boundary */
    {
        int s1 = SetEvent( (HANDLE)(DWORD_PTR)h1 );
        int s2 = SetEvent( (HANDLE)(DWORD_PTR)h2 );
        int s3 = SetEvent( (HANDLE)(DWORD_PTR)h3 );
        fprintf( stderr, "parent: pe1=%p h1=%lx h2=%lx h3=%lx SetEvent=%d/%d/%d err=%lu\n",
                 pe1, h1, h2, h3, s1, s2, s3, GetLastError() );
    }

    fprintf( stderr, "parent: P frame sent, reading status\n" ); fflush( stderr );
    memset( &status, 0, sizeof(status) );
    if (!ReadFile( pipe, &status, sizeof(status), &got, NULL ) || got != sizeof(status))
    {
        ok( 0, "no status frame from child (got %lu)\n", got );
        CloseHandle( pipe );
        CloseHandle( pi.hProcess );
        CloseHandle( pi.hThread );
        return;
    }

    ok( status.child_bits & B_CHILD_SET_PARENT_EVENT, "child could not SetEvent the relayed parent handle\n" );
    ok( status.child_bits & B_PARENT_SET_CHILD_EV1, "parent SetEvent(h1) did not signal the child's source event (no relay?)\n" );
    ok( status.child_bits & B_PARENT_SET_CHILD_EV2, "coalesced frame 2 handle did not relay (SetEvent(h2) lost)\n" );
    ok( status.child_bits & B_PARENT_SET_CHILD_EV3, "single-frame handle did not relay (SetEvent(h3) lost)\n" );
    ok( status.child_bits & B_CHILD_DUP_ALIVE, "child's duplicate of the source event is dead (object closed, not handle?)\n" );
    ok( status.child_bits & B_CHILD_SRC_CLOSED, "source handle was not closed in the writer at delivery (opts=3 semantics)\n" );
    ok( WaitForSingleObject( pe1dup, 0 ) == WAIT_OBJECT_0, "parent event not signaled by the child via the relayed handle\n" );
    ok( status.child_seen_parent_value != (DWORD)(DWORD_PTR)pe1, "delivered payload kept the writer-local value %08lx (unpatched)\n",
        status.child_seen_parent_value );
    ok( h1 != status.child_ev1_raw, "delivered coalesced frame 1 kept the writer-local value %08lx (unpatched)\n", h1 );

    CloseHandle( pipe );
    CloseHandle( pe1 );
    CloseHandle( pe1dup );
    CloseHandle( pi.hThread );
    CloseHandle( pi.hProcess );
}
