/*
 * Cross-process handle duplication tests
 *
 * These tests cover the primitive that the Chromium/Edge WebView2 mojo
 * "named pipe" transport relies on: a process opens its peer with
 * PROCESS_DUP_HANDLE, duplicates a handle (event / file mapping / pipe)
 * into the peer, tells the peer the *numeric value* of the duplicated
 * handle over a byte channel (the named pipe), and the peer uses that
 * value directly, without any object-name lookup, while it is blocked in
 * a non-alertable wait.
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

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winbase.h"
#include "winnt.h"
#include "winnls.h"
#include "winstring.h"
#include "winternl.h"

#include "wine/test.h"

#define MAP_SIZE      0x1000
#define MAP_PATTERN   0xdeadbeef
#define BLOCKED_EVENT_NAME L"Local\\k32_handle_blocked_child"

static char selfname[MAX_PATH];
static char **myargv;
static int   myargc;

/* ---------- byte-channel helpers (two anonymous pipes between parent and child) ---------- */

static void channel_write( HANDLE h, const char *str )
{
    DWORD written;

    if (!WriteFile( h, str, lstrlenA( str ), &written, NULL ))
        trace( "channel write failed: %lu\n", GetLastError());
}

/* blocking line read, used by the child; returns FALSE on pipe close */
static BOOL channel_read_line_blocking( HANDLE h, char *buf, size_t size )
{
    size_t total = 0;
    DWORD  read;

    while (total + 1 < size)
    {
        if (!ReadFile( h, buf + total, 1, &read, NULL ) || !read) return FALSE;
        if (buf[total] == '\n') { buf[total] = 0; return TRUE; }
        total++;
    }
    return FALSE;
}

/* bounded line read, used by the parent, so a stuck child cannot hang the suite */
static BOOL channel_read_line_timeout( HANDLE h, char *buf, size_t size, DWORD timeout_ms )
{
    DWORD avail = 0, total = 0, read;
    ULONGLONG start = GetTickCount64();

    buf[0] = 0;
    while (GetTickCount64() - start < timeout_ms)
    {
        if (!PeekNamedPipe( h, NULL, 0, NULL, &avail, NULL )) return FALSE; /* includes broken pipe */
        if (avail)
        {
            if (total + 1 >= size) return FALSE;
            if (!ReadFile( h, buf + total, 1, &read, NULL ) || !read) return FALSE;
            if (buf[total] == '\n') { buf[total] = 0; return TRUE; }
            total++;
        }
        else Sleep( 10 );
    }
    return FALSE;
}

static void send_handle( HANDLE h, HANDLE value )
{
    char buf[32];

    snprintf( buf, sizeof(buf), "%p\n", (void *)value );
    channel_write( h, buf );
}

static HANDLE get_handle_from_line( const char *line )
{
    /* %p in wine prints a zero-padded hex value with no 0x prefix: base 0 would read it as octal */
    return (HANDLE)(ULONG_PTR)strtoull( line, NULL, 16 );
}

/* the child reads its commands on the standard input handle and answers on the standard
 * output handle; CreateProcess with STARTF_USESTDHANDLES places *duplicated* copies of
 * the parent's pipe ends there, so the child does not have to guess inherited values */
static HANDLE child_p2c, child_c2p;

static void create_child_with_channel( const char *mode, PROCESS_INFORMATION *pi,
                                       HANDLE *p2c_write, HANDLE *c2p_read )
{
    STARTUPINFOA si = { 0 };
    HANDLE p2c_read, c2p_write;
    char buf[MAX_PATH + 64];
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };

    *p2c_write = *c2p_read = NULL;
    if (!CreatePipe( &p2c_read, p2c_write, &sa, 0 ) || !CreatePipe( c2p_read, &c2p_write, &sa, 0 ))
    {
        ok( 0, "CreatePipe failed: %lu\n", GetLastError());
        return;
    }
    snprintf( buf, sizeof(buf), "\"%s\" handle %s", selfname, mode );
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = p2c_read;
    si.hStdOutput = GetStdHandle( STD_OUTPUT_HANDLE );  /* keep the test framework's stdout on the console */
    si.hStdError = c2p_write;                           /* the answer channel is the child's stderr */
    ok( CreateProcessA( NULL, buf, NULL, NULL, TRUE, 0, NULL, NULL, &si, pi ),
        "CreateProcess(%s) failed: %lu\n", buf, GetLastError());
    CloseHandle( p2c_read );
    CloseHandle( c2p_write );
}

/* ------------------------------- child sides ------------------------------- */

static void child_mode_event( void )
{
    char line[64];
    HANDLE h;
    DWORD ret;

    if (!channel_read_line_blocking( child_p2c, line, sizeof(line) ))
    {
        ok( 0, "child: no handle value from parent\n" );
        return;
    }
    h = get_handle_from_line( line );
    /* the parent has not signaled yet: the handle must be valid and not signaled */
    ret = WaitForSingleObject( h, 200 );
    ok( ret == WAIT_TIMEOUT, "child: first wait on duplicated event returned %lu (expected WAIT_TIMEOUT)\n", ret );
    channel_write( child_c2p, "t\n" );
    ret = WaitForSingleObject( h, 5000 );
    ok( ret == WAIT_OBJECT_0, "child: second wait on duplicated event returned %lu (expected OBJECT_0)\n", ret );
    channel_write( child_c2p, "s\n" );
}

static void child_mode_mapping( void )
{
    char line[64];
    HANDLE mapping;
    PVOID view;
    DWORD *data;

    if (!channel_read_line_blocking( child_p2c, line, sizeof(line) ))
    {
        ok( 0, "child: no mapping value from parent\n" );
        return;
    }
    mapping = get_handle_from_line( line );
    view = MapViewOfFile( mapping, FILE_MAP_READ, 0, 0, MAP_SIZE );
    ok( view != NULL, "child: MapViewOfFile(%p) failed: %lu\n", mapping, GetLastError());
    if (view)
    {
        data = (DWORD *)view;
        ok( *data == MAP_PATTERN, "child: mapped content is %08lx, expected %08lx\n", *data, (DWORD)MAP_PATTERN );
        UnmapViewOfFile( view );
    }
    channel_write( child_c2p, "m\n" );
}

static void child_mode_pipe( void )
{
    char line[64], data[8] = {0};
    HANDLE pipe;
    DWORD read;

    if (!channel_read_line_blocking( child_p2c, line, sizeof(line) ))
    {
        ok( 0, "child: no pipe value from parent\n" );
        return;
    }
    pipe = get_handle_from_line( line );
    data[0] = 0;
    ok( ReadFile( pipe, data, 4, &read, NULL ) && read == 4,
        "child: ReadFile on duplicated pipe handle %p failed: %lu\n", pipe, GetLastError());
    ok( !memcmp( data, "ping", 4 ), "child: duplicated pipe handle returned '%c%c%c%c', expected 'ping'\n",
        data[0], data[1], data[2], data[3] );
    channel_write( child_c2p, "p\n" );
}

static void child_mode_close_source( void )
{
    char line[64];
    HANDLE event;

    if (!channel_read_line_blocking( child_p2c, line, sizeof(line) ))
    {
        ok( 0, "child: no event value from parent\n" );
        return;
    }
    event = get_handle_from_line( line );
    /* DUPLICATE_CLOSE_SOURCE on the parent side: the child's copy must work */
    ok( SetEvent( event ), "child: SetEvent on handle %p failed: %lu\n", event, GetLastError());
    channel_write( child_c2p, "e\n" );
}

static void child_mode_blocked( void )
{
    char line[64];
    HANDLE blocked, value;
    PVOID view;
    DWORD ret;

    /* create a named event the parent can reach, signal that we are ready, then
     * block in a non-alertable wait: the parent duplicates handles into us while
     * we are blocked, exactly like the mojo reader does */
    blocked = CreateEventW( NULL, TRUE, FALSE, BLOCKED_EVENT_NAME );
    ok( blocked != NULL, "child: CreateEventW(named) failed: %lu\n", GetLastError());
    channel_write( child_c2p, "r\n" );
    if (blocked)
    {
        ret = WaitForSingleObject( blocked, 20000 );
        ok( ret == WAIT_OBJECT_0, "child: wake-up wait returned %lu\n", ret );
    }
    /* event duplicated into this process while it was blocked */
    if (channel_read_line_blocking( child_p2c, line, sizeof(line) ))
    {
        value = get_handle_from_line( line );
        ret = WaitForSingleObject( value, 1000 );
        ok( ret == WAIT_OBJECT_0, "child: wait on event duplicated into blocked process returned %lu (expected OBJECT_0)\n", ret );
    }
    else ok( 0, "child: no event value from parent\n" );
    /* file mapping duplicated into this process while it was blocked */
    if (channel_read_line_blocking( child_p2c, line, sizeof(line) ))
    {
        value = get_handle_from_line( line );
        view = MapViewOfFile( value, FILE_MAP_READ, 0, 0, MAP_SIZE );
        ok( view != NULL, "child: MapViewOfFile on mapping duplicated into blocked process failed: %lu\n",
            GetLastError());
        if (view)
        {
            ok( *(DWORD *)view == MAP_PATTERN, "child: content through blocked-duplicated mapping is %08lx\n",
                *(DWORD *)view );
            UnmapViewOfFile( view );
        }
    }
    else ok( 0, "child: no mapping value from parent\n" );
    channel_write( child_c2p, "b\n" );
    if (blocked) CloseHandle( blocked );
}

static void child_mode_access( void )
{
    char line[64];
    HANDLE event;

    if (!channel_read_line_blocking( child_p2c, line, sizeof(line) )) return;
    event = get_handle_from_line( line );
    ok( WaitForSingleObject( event, 200 ) == WAIT_TIMEOUT, "child: unexpected waitable handle\n" );
    channel_write( child_c2p, "a\n" );
}

static void child_mode_connectpipe( void );
static void child_mode_event2( void );
static void child_mode_give_handle( void );
static void child_mode_give_handle2( void );

static void child_main( const char *mode )
{
    char line[64];

    snprintf( line, sizeof(line), "%s", mode );
    child_p2c = GetStdHandle( STD_INPUT_HANDLE );
    child_c2p = GetStdHandle( STD_ERROR_HANDLE );

    if (!strcmp( line, "event" )) child_mode_event();
    else if (!strcmp( line, "mapping" )) child_mode_mapping();
    else if (!strcmp( line, "pipe" )) child_mode_pipe();
    else if (!strcmp( line, "closesource" )) child_mode_close_source();
    else if (!strcmp( line, "blocked" )) child_mode_blocked();
    else if (!strcmp( line, "access" )) child_mode_access();
    else if (!strcmp( line, "connectpipe" )) child_mode_connectpipe();
    else if (!strcmp( line, "event2" )) child_mode_event2();
    else if (!strcmp( line, "give_handle" )) child_mode_give_handle();
    else if (!strcmp( line, "give_handle2" )) child_mode_give_handle2();
    else ok( 0, "unknown child mode '%s'\n", line );
}


/* --- peer-pid discovery: the invitation path's GetNamedPipe{Client,Server}ProcessId --- */
static const char peer_pipe_name[] = "\\\\.\\pipe\\k32_handle_peer_pids";

static void child_mode_connectpipe( void )
{
    char line[64], reply[64];
    HANDLE pipe;
    ULONG pid = 0, parent_pid;

    pipe = CreateFileA( peer_pipe_name, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL );
    ok( pipe != INVALID_HANDLE_VALUE, "child: CreateFile(%s) failed: %lu\n", peer_pipe_name, GetLastError());
    channel_write( child_c2p, "c\n" );
    if (pipe == INVALID_HANDLE_VALUE) return;

    if (!channel_read_line_blocking( child_p2c, line, sizeof(line) )) { ok( 0, "child: no parent pid line\n" ); return; }
    parent_pid = (ULONG)strtoul( line + 5, NULL, 10 );
    SetLastError( 0xdeadbeef );
    ok( GetNamedPipeServerProcessId( pipe, &pid ),
        "child: GetNamedPipeServerProcessId on the client end failed: %lu\n", GetLastError());
    ok( pid == parent_pid, "child: GetNamedPipeServerProcessId returned %lu, expected %lu\n", pid, parent_pid );
    snprintf( reply, sizeof(reply), "%lu\n", pid );
    channel_write( child_c2p, reply );
    CloseHandle( pipe );
}

/* --- the ipcz box writer flags: access 0, inherit FALSE, options 3 (CLOSE_SOURCE|SAME_ACCESS) --- */
static void child_mode_event2( void )
{
    char line[64];
    HANDLE event;
    DWORD ret;

    if (!channel_read_line_blocking( child_p2c, line, sizeof(line) )) { ok( 0, "child: no handle value\n" ); return; }
    event = get_handle_from_line( line );
    ret = WaitForSingleObject( event, 200 );
    ok( ret == WAIT_TIMEOUT, "child: wait on options=3 duplicated event returned %lu (expected WAIT_TIMEOUT)\n", ret );
    channel_write( child_c2p, "t\n" );
    ret = WaitForSingleObject( event, 5000 );
    ok( ret == WAIT_OBJECT_0, "child: second wait on options=3 duplicated event returned %lu (expected WAIT_OBJECT_0)\n", ret );
    channel_write( child_c2p, "s\n" );
}

/* --- the ~PlatformHandleInTransit idiom: DuplicateHandle(peer, h, NULL, NULL, 0, FALSE, CLOSE_SOURCE) --- */
static void child_mode_give_handle2( void )
{
    char line[64], buf[64];
    HANDLE event;

    event = CreateEventA( NULL, TRUE, FALSE, NULL );
    ok( event != NULL, "child: CreateEventA failed: %lu\n", GetLastError());
    snprintf( buf, sizeof(buf), "%p\n", (void *)event );
    channel_write( child_c2p, buf );
    if (!channel_read_line_blocking( child_p2c, line, sizeof(line) )) { ok( 0, "child: no check request\n" ); return; }
    SetLastError( 0 );
    ok( !SetEvent( event ), "child: the handle the peer took with DUPLICATE_CLOSE_SOURCE still works here\n" );
    ok( GetLastError() == ERROR_INVALID_HANDLE, "child: expected ERROR_INVALID_HANDLE, got %lu\n", GetLastError());
    channel_write( child_c2p, "y\n" );
}

static void child_mode_give_handle( void )
{
    static const WCHAR name[] = L"Local\\k32_handle_close_in_remote";
    char line[64], buf[64];
    HANDLE event;

    event = CreateEventW( NULL, TRUE, FALSE, name );
    ok( event != NULL, "child: CreateEventW failed: %lu\n", GetLastError());
    snprintf( buf, sizeof(buf), "%p\n", (void *)event );
    channel_write( child_c2p, buf );            /* the value, in *this* process */
    if (!channel_read_line_blocking( child_p2c, line, sizeof(line) )) { ok( 0, "child: no check request\n" ); return; }
    SetLastError( 0 );
    ok( !SetEvent( event ), "child: SetEvent on a handle the peer closed remotely succeeded\n" );
    ok( GetLastError() == ERROR_INVALID_HANDLE, "child: expected ERROR_INVALID_HANDLE after remote close, got %lu\n",
        GetLastError());
    channel_write( child_c2p, "x\n" );
}

/* ------------------------------- parent sides ------------------------------- */

static HANDLE open_child( PROCESS_INFORMATION *pi )
{
    HANDLE process = OpenProcess( PROCESS_DUP_HANDLE, FALSE, GetProcessId( pi->hProcess ));
    ok( process != NULL, "OpenProcess(PROCESS_DUP_HANDLE) on child failed: %lu\n", GetLastError());
    return process;
}

static void reply( HANDLE h )
{
    char line[64];

    ok( channel_read_line_timeout( h, line, sizeof(line), 15000 ), "timed out waiting for child reply\n" );
}

static void test_dup_event_cross_process( void )
{
    PROCESS_INFORMATION pi = { 0 };
    HANDLE process, event, p2c_write, c2p_read, remote = NULL;
    char line[64];

    event = CreateEventA( NULL, TRUE, FALSE, NULL );
    ok( event != NULL, "CreateEvent failed: %lu\n", GetLastError());
    create_child_with_channel( "event", &pi, &p2c_write, &c2p_read );
    if (!pi.hProcess) { CloseHandle( event ); return; }

    process = open_child( &pi );
    if (process)
    {
        remote = NULL;
        SetLastError( 0xdeadbeef );
        ok( DuplicateHandle( GetCurrentProcess(), event, process, &remote, 0, FALSE, DUPLICATE_SAME_ACCESS ),
            "DuplicateHandle(event -> child) failed: %lu\n", GetLastError());
        ok( remote != NULL, "DuplicateHandle returned a NULL handle\n" );
        printf( "DIAG parent: self=%lu child_pid=%lu event=%p remote=%p\n",
                GetCurrentProcessId(), GetProcessId( pi.hProcess ), event, remote );
        if (remote)
        {
            send_handle( p2c_write, remote );
            reply( c2p_read );                 /* child saw WAIT_TIMEOUT */
            SetEvent( event );
            ok( channel_read_line_timeout( c2p_read, line, sizeof(line), 10000 ),
                "child never confirmed the signaled duplicated event\n" );
            ok( !strcmp( line, "s" ), "child reply '%s', expected 's'\n", line );
            CloseHandle( remote );
        }
        CloseHandle( process );
    }
    CloseHandle( p2c_write );
    CloseHandle( c2p_read );
    CloseHandle( event );
    if (pi.hThread) CloseHandle( pi.hThread );
    if (pi.hProcess)
    {
        ok( WaitForSingleObject( pi.hProcess, 15000 ) == WAIT_OBJECT_0, "child did not exit\n" );
        CloseHandle( pi.hProcess );
    }
}

static void test_dup_mapping_cross_process( void )
{
    PROCESS_INFORMATION pi = { 0 };
    HANDLE process, mapping, view, p2c_write, c2p_read, remote = NULL;
    char line[64];

    mapping = CreateFileMappingA( INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, MAP_SIZE, NULL );
    ok( mapping != NULL, "CreateFileMapping failed: %lu\n", GetLastError());
    if (!mapping) return;
    view = MapViewOfFile( mapping, FILE_MAP_WRITE, 0, 0, MAP_SIZE );
    ok( view != NULL, "MapViewOfFile failed: %lu\n", GetLastError());
    if (view) *(DWORD *)view = MAP_PATTERN;

    create_child_with_channel( "mapping", &pi, &p2c_write, &c2p_read );
    if (!pi.hProcess) goto done;

    process = open_child( &pi );
    if (process)
    {
        SetLastError( 0xdeadbeef );
        ok( DuplicateHandle( GetCurrentProcess(), mapping, process, &remote, 0, FALSE, DUPLICATE_SAME_ACCESS ),
            "DuplicateHandle(file mapping -> child) failed: %lu\n", GetLastError());
        if (remote)
        {
            send_handle( p2c_write, remote );
            ok( channel_read_line_timeout( c2p_read, line, sizeof(line), 10000 ),
                "child never reported on the duplicated mapping\n" );
            ok( !strcmp( line, "m" ), "child reply '%s', expected 'm'\n", line );
            CloseHandle( remote );
        }
        CloseHandle( process );
    }
    CloseHandle( p2c_write );
    CloseHandle( c2p_read );
    if (pi.hThread) CloseHandle( pi.hThread );
    if (pi.hProcess)
    {
        ok( WaitForSingleObject( pi.hProcess, 15000 ) == WAIT_OBJECT_0, "child did not exit\n" );
        CloseHandle( pi.hProcess );
    }
done:
    if (view) UnmapViewOfFile( view );
    CloseHandle( mapping );
}

static void test_dup_pipe_cross_process( void )
{
    static const char name[] = "\\\\.\\pipe\\k32_handle_dup_test";
    PROCESS_INFORMATION pi = { 0 };
    HANDLE process, server, client, p2c_write, c2p_read, remote = NULL;
    char line[64];
    DWORD written;

    server = CreateNamedPipeA( name, PIPE_ACCESS_DUPLEX, PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
                               4, 512, 512, 0, NULL );
    ok( server != INVALID_HANDLE_VALUE, "CreateNamedPipe failed: %lu\n", GetLastError());
    if (server == INVALID_HANDLE_VALUE) return;
    client = CreateFileA( name, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL );
    ok( client != INVALID_HANDLE_VALUE, "CreateFile(pipe) failed: %lu\n", GetLastError());
    if (client == INVALID_HANDLE_VALUE) { CloseHandle( server ); return; }

    create_child_with_channel( "pipe", &pi, &p2c_write, &c2p_read );
    if (!pi.hProcess) { CloseHandle( server ); CloseHandle( client ); return; }

    process = open_child( &pi );
    if (process)
    {
        SetLastError( 0xdeadbeef );
        ok( DuplicateHandle( GetCurrentProcess(), client, process, &remote, 0, FALSE, DUPLICATE_SAME_ACCESS ),
            "DuplicateHandle(pipe client -> child) failed: %lu\n", GetLastError());
        if (remote)
        {
            send_handle( p2c_write, remote );
            ok( WriteFile( server, "ping", 4, &written, NULL ), "server WriteFile failed: %lu\n", GetLastError());
            ok( channel_read_line_timeout( c2p_read, line, sizeof(line), 10000 ),
                "child never reported on the duplicated pipe handle\n" );
            ok( !strcmp( line, "p" ), "child reply '%s', expected 'p'\n", line );
            CloseHandle( remote );
        }
        CloseHandle( process );
    }
    CloseHandle( p2c_write );
    CloseHandle( c2p_read );
    CloseHandle( server );
    CloseHandle( client );
    if (pi.hThread) CloseHandle( pi.hThread );
    if (pi.hProcess)
    {
        ok( WaitForSingleObject( pi.hProcess, 15000 ) == WAIT_OBJECT_0, "child did not exit\n" );
        CloseHandle( pi.hProcess );
    }
}

static void test_dup_close_source_cross_process( void )
{
    static const WCHAR name[] = L"Local\\k32_handle_dup_closesource";
    PROCESS_INFORMATION pi = { 0 };
    HANDLE process, event, reopened, p2c_write, c2p_read, remote = NULL;
    char line[64];

    event = CreateEventW( NULL, TRUE, FALSE, name );
    ok( event != NULL, "CreateEventW failed: %lu\n", GetLastError());
    if (!event) return;

    create_child_with_channel( "closesource", &pi, &p2c_write, &c2p_read );
    if (!pi.hProcess) { CloseHandle( event ); return; }

    process = open_child( &pi );
    if (process)
    {
        ok( DuplicateHandle( GetCurrentProcess(), event, process, &remote, 0, FALSE,
                             DUPLICATE_SAME_ACCESS | DUPLICATE_CLOSE_SOURCE ),
            "DuplicateHandle(DUPLICATE_CLOSE_SOURCE) failed: %lu\n", GetLastError());
        /* the source handle must be gone from this process now */
        SetLastError( 0 );
        ok( !SetEvent( event ), "SetEvent on the closed source handle succeeded\n" );
        ok( GetLastError() == ERROR_INVALID_HANDLE, "expected ERROR_INVALID_HANDLE, got %lu\n", GetLastError());
        if (remote)
        {
            /* re-open by name *before* the child can exit and drop the last reference */
            reopened = OpenEventW( SYNCHRONIZE, FALSE, name );
            ok( reopened != NULL, "OpenEventW on the event the parent created failed: %lu\n", GetLastError());
            send_handle( p2c_write, remote );
            ok( channel_read_line_timeout( c2p_read, line, sizeof(line), 10000 ),
                "child never reported on the DUPLICATE_CLOSE_SOURCE handle\n" );
            if (reopened)
            {
                ok( WaitForSingleObject( reopened, 5000 ) == WAIT_OBJECT_0,
                    "event not signaled by the child that received it via DUPLICATE_CLOSE_SOURCE\n" );
                CloseHandle( reopened );
            }
            CloseHandle( remote );
        }
        CloseHandle( process );
    }
    CloseHandle( p2c_write );
    CloseHandle( c2p_read );
    CloseHandle( event );
    if (pi.hThread) CloseHandle( pi.hThread );
    if (pi.hProcess)
    {
        ok( WaitForSingleObject( pi.hProcess, 15000 ) == WAIT_OBJECT_0, "child did not exit\n" );
        CloseHandle( pi.hProcess );
    }
}

/* the mojo pattern: handles are duplicated into a peer that is blocked in a
 * non-alertable wait, and the peer uses the numeric value right away */
static void test_dup_into_blocked_process( void )
{
    PROCESS_INFORMATION pi = { 0 };
    HANDLE process, event, mapping, view = NULL, blocked, p2c_write, c2p_read, remote = NULL;
    char line[64];

    event   = CreateEventA( NULL, TRUE, TRUE, NULL );
    mapping = CreateFileMappingA( INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, MAP_SIZE, NULL );
    ok( event && mapping, "setup handles failed: %lu\n", GetLastError());
    if (!event || !mapping) goto setup_failed;
    view = MapViewOfFile( mapping, FILE_MAP_WRITE, 0, 0, MAP_SIZE );
    if (view) *(DWORD *)view = MAP_PATTERN;

    create_child_with_channel( "blocked", &pi, &p2c_write, &c2p_read );
    if (!pi.hProcess) goto setup_failed;

    ok( channel_read_line_timeout( c2p_read, line, sizeof(line), 15000 ), "child never reported ready\n" );
    process = open_child( &pi );
    if (process)
    {
        ok( DuplicateHandle( GetCurrentProcess(), event, process, &remote, 0, FALSE, DUPLICATE_SAME_ACCESS ),
            "DuplicateHandle(event -> blocked child) failed: %lu\n", GetLastError());
        if (remote)
        {
            send_handle( p2c_write, remote );
            CloseHandle( remote );
            remote = NULL;
        }
        SetEvent( event );
        ok( DuplicateHandle( GetCurrentProcess(), mapping, process, &remote, 0, FALSE, DUPLICATE_SAME_ACCESS ),
            "DuplicateHandle(mapping -> blocked child) failed: %lu\n", GetLastError());
        if (remote)
        {
            send_handle( p2c_write, remote );
            CloseHandle( remote );
        }
        CloseHandle( process );
    }
    /* let the child wake up and consume what we queued */
    blocked = OpenEventW( EVENT_MODIFY_STATE, FALSE, BLOCKED_EVENT_NAME );
    ok( blocked != NULL, "OpenEventW on the blocked-child event failed: %lu\n", GetLastError());
    if (blocked)
    {
        SetEvent( blocked );
        CloseHandle( blocked );
    }
    ok( channel_read_line_timeout( c2p_read, line, sizeof(line), 25000 ), "blocked child never finished\n" );
    ok( !strcmp( line, "b" ), "blocked child reply '%s', expected 'b'\n", line );

    CloseHandle( p2c_write );
    CloseHandle( c2p_read );
    if (pi.hThread) CloseHandle( pi.hThread );
    if (pi.hProcess)
    {
        ok( WaitForSingleObject( pi.hProcess, 15000 ) == WAIT_OBJECT_0, "child did not exit\n" );
        CloseHandle( pi.hProcess );
    }
setup_failed:
    if (view) UnmapViewOfFile( view );
    if (mapping) CloseHandle( mapping );
    if (event) CloseHandle( event );
}

static void test_dup_access_denied( void )
{
    PROCESS_INFORMATION pi = { 0 };
    HANDLE process, event, p2c_write, c2p_read, remote = NULL;

    event = CreateEventA( NULL, TRUE, FALSE, NULL );
    create_child_with_channel( "access", &pi, &p2c_write, &c2p_read );
    if (!pi.hProcess) { CloseHandle( event ); return; }

    process = OpenProcess( PROCESS_VM_READ, FALSE, GetProcessId( pi.hProcess ));
    if (process)
    {
        SetLastError( 0 );
        remote = NULL;
        /* wine grants more to an OpenProcess handle than was asked for (see the
         * NtTerminateProcess probe below), so the PROCESS_DUP_HANDLE check cannot bite yet */
        todo_wine ok( !DuplicateHandle( GetCurrentProcess(), event, process, &remote, 0, FALSE, DUPLICATE_SAME_ACCESS ),
            "DuplicateHandle into a process opened without PROCESS_DUP_HANDLE succeeded\n" );
        todo_wine ok( GetLastError() == ERROR_ACCESS_DENIED, "expected ERROR_ACCESS_DENIED, got %lu\n", GetLastError());
        SetLastError( 0 );
        todo_wine ok( NtTerminateProcess( process, 1 ) == STATUS_ACCESS_DENIED,
                      "a handle opened with PROCESS_VM_READ alone was accepted by NtTerminateProcess\n" );
        CloseHandle( process );
    }
    else skip( "OpenProcess(PROCESS_VM_READ) failed: %lu\n", GetLastError());

    CloseHandle( p2c_write );
    CloseHandle( c2p_read );
    CloseHandle( event );
    if (pi.hThread) CloseHandle( pi.hThread );
    if (pi.hProcess)
    {
        TerminateProcess( pi.hProcess, 0 );
        WaitForSingleObject( pi.hProcess, 5000 );
        CloseHandle( pi.hProcess );
    }
}


static void test_named_pipe_peer_pids( void )
{
    PROCESS_INFORMATION pi = { 0 };
    HANDLE server, p2c_write, c2p_read;
    ULONG client_pid = 0, got;
    char line[64], buf[32];
    BOOL ret;

    server = CreateNamedPipeA( peer_pipe_name, PIPE_ACCESS_DUPLEX,
                               PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
                               4, 4096, 4096, 0, NULL );
    ok( server != INVALID_HANDLE_VALUE, "CreateNamedPipe(peer pids) failed: %lu\n", GetLastError());
    if (server == INVALID_HANDLE_VALUE) return;

    create_child_with_channel( "connectpipe", &pi, &p2c_write, &c2p_read );
    if (!pi.hProcess) { CloseHandle( server ); return; }

    ok( channel_read_line_timeout( c2p_read, line, sizeof(line), 15000 ), "child never reported the pipe open\n" );
    SetLastError( 0 );
    ret = ConnectNamedPipe( server, NULL );
    ok( ret || GetLastError() == ERROR_PIPE_CONNECTED, "ConnectNamedPipe failed: %lu\n", GetLastError());

    SetLastError( 0xdeadbeef );
    ok( GetNamedPipeClientProcessId( server, &client_pid ),
        "GetNamedPipeClientProcessId on the server end failed: %lu\n", GetLastError());
    ok( client_pid == GetProcessId( pi.hProcess ),
        "GetNamedPipeClientProcessId returned %lu, expected child pid %lu\n", client_pid, GetProcessId( pi.hProcess ));

    snprintf( buf, sizeof(buf), "ppid %lu\n", GetCurrentProcessId());
    channel_write( p2c_write, buf );
    ok( channel_read_line_timeout( c2p_read, line, sizeof(line), 15000 ), "child never reported its view of the peer pid\n" );
    got = (ULONG)strtoul( line, NULL, 10 );
    ok( got == GetCurrentProcessId(), "child side GetNamedPipeServerProcessId reported %lu, expected %lu\n",
        got, GetCurrentProcessId());

    CloseHandle( p2c_write );
    CloseHandle( c2p_read );
    CloseHandle( server );
    if (pi.hThread) CloseHandle( pi.hThread );
    if (pi.hProcess)
    {
        ok( WaitForSingleObject( pi.hProcess, 15000 ) == WAIT_OBJECT_0, "child did not exit\n" );
        CloseHandle( pi.hProcess );
    }
}

static void test_dup_options_3( void )
{
    static const WCHAR name[] = L"Local\\k32_handle_dup_options3";
    PROCESS_INFORMATION pi = { 0 };
    HANDLE process, event, reopened, p2c_write, c2p_read, remote = NULL;
    char line[64];

    event = CreateEventW( NULL, TRUE, FALSE, name );
    ok( event != NULL, "CreateEventW failed: %lu\n", GetLastError());
    if (!event) return;
    create_child_with_channel( "event2", &pi, &p2c_write, &c2p_read );
    if (!pi.hProcess) { CloseHandle( event ); return; }

    process = open_child( &pi );
    if (process)
    {
        SetLastError( 0xdeadbeef );
        ok( DuplicateHandle( GetCurrentProcess(), event, process, &remote, 0, FALSE,
                             DUPLICATE_CLOSE_SOURCE | DUPLICATE_SAME_ACCESS ),
            "DuplicateHandle(options=3) failed: %lu\n", GetLastError());
        SetLastError( 0 );
        ok( !SetEvent( event ), "the source handle survived a DUPLICATE_CLOSE_SOURCE duplication\n" );
        ok( GetLastError() == ERROR_INVALID_HANDLE, "expected ERROR_INVALID_HANDLE, got %lu\n", GetLastError());
        if (remote)
        {
            send_handle( p2c_write, remote );
            ok( channel_read_line_timeout( c2p_read, line, sizeof(line), 10000 ), "child: no TIMEOUT reply\n" );
            reopened = OpenEventW( EVENT_MODIFY_STATE, FALSE, name );
            ok( reopened != NULL, "reopening the event by name failed: %lu\n", GetLastError());
            if (reopened)
            {
                SetEvent( reopened );
                CloseHandle( reopened );
            }
            ok( channel_read_line_timeout( c2p_read, line, sizeof(line), 10000 ), "child: no SIGNALLED reply\n" );
            CloseHandle( remote );
        }
        CloseHandle( process );
    }
    CloseHandle( p2c_write );
    CloseHandle( c2p_read );
    CloseHandle( event );
    if (pi.hThread) CloseHandle( pi.hThread );
    if (pi.hProcess)
    {
        ok( WaitForSingleObject( pi.hProcess, 15000 ) == WAIT_OBJECT_0, "child did not exit\n" );
        CloseHandle( pi.hProcess );
    }
}

static void test_close_handle_in_remote_process( void )
{
    PROCESS_INFORMATION pi = { 0 };
    HANDLE process, p2c_write, c2p_read;
    char line[64];

    create_child_with_channel( "give_handle", &pi, &p2c_write, &c2p_read );
    if (!pi.hProcess) return;

    ok( channel_read_line_timeout( c2p_read, line, sizeof(line), 15000 ), "child never sent its handle value\n" );
    process = open_child( &pi );
    if (process)
    {
        SetLastError( 0xdeadbeef );
        ok( DuplicateHandle( process, get_handle_from_line( line ), NULL, NULL, 0, FALSE, DUPLICATE_CLOSE_SOURCE ),
            "DuplicateHandle(process, handle, NULL, NULL, 0, FALSE, DUPLICATE_CLOSE_SOURCE) failed: %lu\n",
            GetLastError());
        channel_write( p2c_write, "check\n" );
        ok( channel_read_line_timeout( c2p_read, line, sizeof(line), 10000 ), "child never verified the remote close\n" );
        CloseHandle( process );
    }
    CloseHandle( p2c_write );
    CloseHandle( c2p_read );
    if (pi.hThread) CloseHandle( pi.hThread );
    if (pi.hProcess)
    {
        ok( WaitForSingleObject( pi.hProcess, 15000 ) == WAIT_OBJECT_0, "child did not exit\n" );
        CloseHandle( pi.hProcess );
    }
}

/* the exact call the mojo/ipcz reader makes for a handle that arrived from the peer:
 * DuplicateHandle( peer_process, wire_value, current_process, &out, 0, FALSE,
 *                  DUPLICATE_CLOSE_SOURCE | DUPLICATE_SAME_ACCESS ) */
static void test_take_handle_from_peer( void )
{
    PROCESS_INFORMATION pi = { 0 };
    HANDLE process, p2c_write, c2p_read, taken = NULL;
    char line[64];

    create_child_with_channel( "give_handle2", &pi, &p2c_write, &c2p_read );
    if (!pi.hProcess) return;

    ok( channel_read_line_timeout( c2p_read, line, sizeof(line), 15000 ), "child never sent its handle value\n" );
    process = open_child( &pi );
    if (process)
    {
        SetLastError( 0xdeadbeef );
        ok( DuplicateHandle( process, get_handle_from_line( line ), GetCurrentProcess(), &taken, 0, FALSE,
                             DUPLICATE_CLOSE_SOURCE | DUPLICATE_SAME_ACCESS ),
            "DuplicateHandle(peer, handle, self, &out, 0, FALSE, CLOSE_SOURCE|SAME_ACCESS) failed: %lu\n",
            GetLastError());
        if (taken)
        {
            ok( SetEvent( taken ), "the handle taken from the peer is not usable here: %lu\n", GetLastError());
            CloseHandle( taken );
        }
        channel_write( p2c_write, "check\n" );
        ok( channel_read_line_timeout( c2p_read, line, sizeof(line), 10000 ),
            "child never verified that its handle was taken\n" );
        ok( !strcmp( line, "y" ), "child reply '%s', expected 'y'\n", line );
        CloseHandle( process );
    }
    CloseHandle( p2c_write );
    CloseHandle( c2p_read );
    if (pi.hThread) CloseHandle( pi.hThread );
    if (pi.hProcess)
    {
        ok( WaitForSingleObject( pi.hProcess, 15000 ) == WAIT_OBJECT_0, "child did not exit\n" );
        CloseHandle( pi.hProcess );
    }
}

START_TEST(handle)
{
    myargc = winetest_get_mainargs( &myargv );
    GetModuleFileNameA( NULL, selfname, sizeof(selfname) );

    if (myargc >= 3)
    {
        child_main( myargv[2] );
        return;
    }

    test_dup_event_cross_process();
    test_dup_mapping_cross_process();
    test_dup_pipe_cross_process();
    test_dup_close_source_cross_process();
    test_dup_into_blocked_process();
    test_dup_access_denied();
    test_named_pipe_peer_pids();
    test_dup_options_3();
    test_close_handle_in_remote_process();
    test_take_handle_from_peer();
}
