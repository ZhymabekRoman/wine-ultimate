/*
 * Lost-wake stress test for the message-pump wait (MsgWaitForMultipleObjectsEx).
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
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

/* Opt-in stress test: a single thread parks in MsgWaitForMultipleObjectsEx while a
 * second thread fires one wake source per iteration, in two orders (source fired
 * strictly before the wait is armed, and source fired while the wait is being
 * armed).  A watchdog reports a lost wake as a wedge: the waiter stops making
 * progress for WATCHDOG_MS while a wake source is outstanding.
 *
 * Enabled by setting WINE_MSGLOOP_ITER (iterations per combination, e.g. 100000).
 * Optional filters: WINE_MSGLOOP_SRC, WINE_MSGLOOP_ORDER, WINE_MSGLOOP_IA select a
 * single wake source / order / MWMO_INPUTAVAILABLE flag (see the tables below).
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "windef.h"
#include "winbase.h"
#include "winuser.h"
#include "winver.h"

#include "wine/test.h"


#define WATCHDOG_MS 5000
#define DEFAULT_ITER 20000

enum src { SRC_POSTMSG, SRC_POSTTHREAD, SRC_TIMER, SRC_EVENT, SRC_IOC, SRC_COUNT };
static const char *const src_names[SRC_COUNT] = { "postmsg", "postthread", "timer", "event", "ioc" };
enum order { ORD_BEFORE, ORD_RACE, ORD_COUNT };
static const char *const ord_names[ORD_COUNT] = { "before", "race" };

static int src, order, use_input_avail;
static volatile LONG done_iter;
static volatile LONG wedged;
static HANDLE req_ev, ack_ev, wake_ev, iocp;
static HWND hwnd;
static DWORD a_tid;
static int next_delay;
static volatile LONG post_ok, post_fail, post_err;

static void spin_us( unsigned int us )
{
    LARGE_INTEGER freq, start, now;
    QueryPerformanceFrequency( &freq );
    QueryPerformanceCounter( &start );
    for (;;)
    {
        QueryPerformanceCounter( &now );
        if ((now.QuadPart - start.QuadPart) * 1000000 / (ULONGLONG)freq.QuadPart >= us) break;
    }
}

static LRESULT CALLBACK wndproc( HWND h, UINT m, WPARAM w, LPARAM l )
{
    if (m == WM_TIMER) return 0;
    return DefWindowProcW( h, m, w, l );
}

static void drain_queue( void )
{
    MSG msg;
    while (PeekMessageW( &msg, 0, 0, 0, PM_REMOVE )) DispatchMessageW( &msg );
    KillTimer( hwnd, 1 );
}

static DWORD WINAPI selfwaker( void *arg )
{
    BOOL sent;
    Sleep( 20 );
    sent = PostMessageW( hwnd, WM_USER, 0, 0 );
    fprintf( stderr, "selfcheck2: posted ok=%d err=%lu\n", sent, GetLastError() );
    return 0;
}

/* Timeout-bounded one-shot check of the wake path: separates "the wake source never
 * reached the queue" from "the wake was lost by the wait". */
static void selfcheck( void )
{
    DWORD r;
    BOOL sent;

    MSG m;
    sent = PostMessageW( hwnd, WM_USER, 0, 0 );
    fprintf( stderr, "sc0: sent=%d qs=%lx\n", sent, GetQueueStatus( QS_ALLINPUT ) );
    r = MsgWaitForMultipleObjectsEx( 0, NULL, 100, QS_ALLINPUT, MWMO_INPUTAVAILABLE );
    fprintf( stderr, "sc1: r=%lu qs=%lx tid=%lu owner=%lu\n", r, GetQueueStatus( QS_ALLINPUT ),
             GetCurrentThreadId(), GetWindowThreadProcessId( hwnd, NULL ) );
    fprintf( stderr, "sc2: peek=%d msg=%x\n", PeekMessageW( &m, 0, 0, 0, PM_NOREMOVE ), 0 );
    drain_queue();

    CreateThread( NULL, 0, selfwaker, NULL, 0, NULL );
    r = MsgWaitForMultipleObjectsEx( 0, NULL, 200, QS_ALLINPUT, MWMO_INPUTAVAILABLE );
    fprintf( stderr, "selfcheck2: wait r=%lu\n", r );
    drain_queue();
}

static DWORD WINAPI watchdog( void *arg )
{
    LONG last = -1;
    DWORD last_tick = GetTickCount();
    while (!InterlockedCompareExchange( &wedged, 0, 0 ))
    {
        LONG d = InterlockedCompareExchange( &done_iter, 0, 0 );
        Sleep( 200 );
        if (d != last) { last = d; last_tick = GetTickCount(); continue; }
        if (GetTickCount() - last_tick < WATCHDOG_MS) continue;
        fprintf( stderr, "WEDGE src=%s order=%s inputavail=%d iter=%ld posts_ok=%ld posts_fail=%ld lasterr=%lu iswindow=%d owner_tid=%lu a_tid=%lu self_tid=%lu\n",
                 src_names[src], ord_names[order], use_input_avail, (long)d,
                 (long)InterlockedCompareExchange( &post_ok, 0, 0 ),
                 (long)InterlockedCompareExchange( &post_fail, 0, 0 ), (ULONG)post_err,
                 IsWindow( hwnd ), GetWindowThreadProcessId( hwnd, NULL ), a_tid, GetCurrentThreadId() );
        InterlockedExchange( &wedged, 1 );
        for (;;) Sleep( 1000 ); /* stay attachable for gdb */
    }
    return 0;
}

static DWORD WINAPI waker( void *arg )
{
    for (;;)
    {
        WaitForSingleObject( req_ev, INFINITE );
        if (order == ORD_RACE && next_delay) spin_us( next_delay * 40 );
        switch (src)
        {
        case SRC_POSTMSG:    if (PostMessageW( hwnd, WM_USER, 0, 0 )) InterlockedIncrement( &post_ok );
                             else { InterlockedIncrement( &post_fail ); post_err = GetLastError(); } break;
        case SRC_POSTTHREAD: if (PostThreadMessageW( a_tid, WM_USER, 0, 0 )) InterlockedIncrement( &post_ok );
                             else { InterlockedIncrement( &post_fail ); post_err = GetLastError(); } break;
        case SRC_TIMER:      SetTimer( hwnd, 1, 1, NULL ); break;
        case SRC_EVENT:      SetEvent( wake_ev ); break;
        case SRC_IOC:        PostQueuedCompletionStatus( iocp, 1, 0, NULL ); break;
        }
        if (order == ORD_BEFORE) SetEvent( ack_ev );
    }
}

static void run_combination( LONG iterations )
{
    HANDLE handles[1], *handle_array = NULL;
    LONG i;

    if (src == SRC_EVENT || src == SRC_IOC) handles[0] = (src == SRC_EVENT) ? wake_ev : (HANDLE)iocp,
                                             handle_array = handles;
    InterlockedExchange( &done_iter, 0 );
    InterlockedExchange( &wedged, 0 );

    for (i = 0; i < iterations; i++)
    {
        DWORD r, count;
        drain_queue();
        next_delay = (int)(i % 4);
        ResetEvent( ack_ev );
        SetEvent( req_ev );
        if (order == ORD_BEFORE) WaitForSingleObject( ack_ev, INFINITE ); /* source already fired */

        count = handle_array ? 1 : 0;
        /* use_input_avail=0: conforming call, monitor every queue state and ask for
         * the pending-state check.  use_input_avail=1: the shape used by Chromium's
         * message pump - MWMO_INPUTAVAILABLE passed in the event mask slot, flags 0,
         * which relies on a newly posted message always waking the wait. */
        r = MsgWaitForMultipleObjectsEx( count, handle_array, INFINITE,
                                         use_input_avail ? MWMO_INPUTAVAILABLE : QS_ALLINPUT,
                                         use_input_avail ? 0 : MWMO_INPUTAVAILABLE );
        if (r == WAIT_TIMEOUT)
        {
            fprintf( stderr, "TIMEOUT src=%s order=%s inputavail=%d iter=%ld\n",
                   src_names[src], ord_names[order], use_input_avail, (long)i );
            break;
        }
        drain_queue();
        InterlockedExchange( &done_iter, i + 1 );
        if (!(i & 0x3fff)) fprintf( stderr, "msgloop: progress src=%s order=%s ia=%d iter=%ld\n", src_names[src], ord_names[order], use_input_avail, (long)i + 1 );
    }
    ok( !InterlockedCompareExchange( &wedged, 0, 0 ),
        "lost wake: MsgWaitForMultipleObjectsEx never returned (src=%s order=%s inputavail=%d iter=%ld)\n",
        src_names[src], ord_names[order], use_input_avail, (long)InterlockedCompareExchange( &done_iter, 0, 0 ) );
    InterlockedExchange( &wedged, 1 );
}

START_TEST(msgloop)
{
    WNDCLASSW cls;
    const char *env;
    LONG iterations;
    int s, o, f, only_src = -1, only_order = -1, only_ia = -1;

    setvbuf( stderr, NULL, _IONBF, 0 );
    setvbuf( stdout, NULL, _IONBF, 0 );
    env = getenv( "WINE_MSGLOOP_ITER" );
    if (!env) return; /* opt-in stress test */
    iterations = atol( env );
    if (iterations < 1) iterations = DEFAULT_ITER;
    if ((env = getenv( "WINE_MSGLOOP_SRC" ))) only_src = atoi( env );
    if ((env = getenv( "WINE_MSGLOOP_ORDER" ))) only_order = atoi( env );
    if ((env = getenv( "WINE_MSGLOOP_IA" ))) only_ia = atoi( env );

    a_tid = GetCurrentThreadId();
    memset( &cls, 0, sizeof(cls) );
    cls.lpfnWndProc = wndproc;
    cls.lpszClassName = L"MsgLoopTestClass";
    RegisterClassW( &cls );
    hwnd = CreateWindowExW( 0, L"MsgLoopTestClass", L"MsgLoopTest", WS_POPUP,
                            0, 0, 64, 64, getenv( "WINE_MSGLOOP_HWMSG" ) ? HWND_MESSAGE : NULL,
                            NULL, NULL, NULL );
    ok( hwnd != NULL, "could not create test window, error %lu\n", GetLastError() );
    if (!hwnd) return;
    selfcheck();

    req_ev  = CreateEventA( NULL, FALSE, FALSE, NULL );
    ack_ev  = CreateEventA( NULL, TRUE, FALSE, NULL );
    wake_ev = CreateEventA( NULL, FALSE, FALSE, NULL );
    iocp    = CreateIoCompletionPort( INVALID_HANDLE_VALUE, NULL, 0, 1 );
    ok( req_ev && ack_ev && wake_ev && iocp, "could not create sync objects\n" );
    CreateThread( NULL, 0, waker, NULL, 0, NULL );
    CreateThread( NULL, 0, watchdog, NULL, 0, NULL );

    for (s = 0; s < SRC_COUNT; s++)
        for (o = 0; o < ORD_COUNT; o++)
            for (f = 0; f < 2; f++)
            {
                if (only_src >= 0 && s != only_src) continue;
                if (only_order >= 0 && o != only_order) continue;
                if (only_ia >= 0 && f != only_ia) continue;
                src = s; order = o; use_input_avail = f;
                fprintf( stderr, "msgloop: src=%s order=%s inputavail=%d iterations=%ld\n",
                       src_names[s], ord_names[o], f, iterations );
                run_combination( iterations );
                if (InterlockedCompareExchange( &wedged, 0, 0 )) return;
                ResetEvent( wake_ev );
                drain_queue();
            }
}
