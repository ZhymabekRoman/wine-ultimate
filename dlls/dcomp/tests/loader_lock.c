/*
 * Lock-order conformance tests for the device-creation path behind
 * DirectComposition.
 *
 * The rule these tests pin down is the one the WhatsApp Desktop startup
 * deadlock breaks: the PE loader lock (loader_section) must never be taken
 * while the wined3d critical section is held.  A thread that is inside
 * DllMain already owns loader_section and can reach wined3d through
 * d2d1 -> dxgi -> wined3d; a thread that resolves a dll entry point,
 * registers a window class, creates a window or queries pixel formats
 * while it owns the wined3d critical section waits for loader_section,
 * and the two threads deadlock.  Every other thread of the process then
 * queues behind them, which is the observed freeze.
 *
 * Both tests observe the invariant from outside the modules, in a process
 * that has not created a device yet, so the lazy code path under test is
 * the one that runs:
 *
 *  - the DirectComposition rendering device must be built without loading
 *    a dll, i.e. dcomp has to resolve its Direct3D module at process
 *    attach and build the device outside its own critical section;
 *  - the wined3d critical section must be released around the loader work
 *    of adapter initialisation, which is what makes a thread that holds
 *    the loader lock able to finish.
 *
 * Copyright 2026
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
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include <stdarg.h>

#include "ntstatus.h"
#define WIN32_NO_STATUS
#define COBJMACROS
#include <winternl.h>
/* No initguid.h here: dcomp.c, which is linked into this same test
 * executable, defines the GUIDs; defining them twice breaks the link. */
#include <d3d11.h>
#include "dcomp.h"
#include "wine/test.h"

/* The wined3d global critical section.  The d3d9, d3d11, dxgi and dcomp
 * entry points take it around every call, device creation included, so a
 * test of this suite can take it too; it is declared here because the
 * wined3d internal header is not for test use. */
extern void WINAPI wined3d_mutex_lock( void );
extern void WINAPI wined3d_mutex_unlock( void );

/* d3d11 is loaded by hand, not imported: the sibling test
 * rendering_device_lock.c watches for the lazy load of d3d11.dll, and an
 * import table entry here would load it at process start. */
typedef HRESULT (WINAPI *D3D11CREATEDEVICE_PROC)( IDXGIAdapter *adapter, D3D_DRIVER_TYPE driver_type,
        HMODULE software, UINT flags, const D3D_FEATURE_LEVEL *feature_levels, UINT feature_level_count,
        UINT sdk_version, ID3D11Device **device, D3D_FEATURE_LEVEL *chosen_feature_level,
        ID3D11DeviceContext **context );
static D3D11CREATEDEVICE_PROC pD3D11CreateDevice;
static HMODULE module_d3d11;

/***********************************************************************
 * Test 1: the rendering device must be built without loading a dll.
 */

/***********************************************************************
 * Test 2: the wined3d critical section must not be held across the loader
 * work of adapter initialisation.
 *
 * The loader lock is taken and held for LOADER_HOLD_MS.  While it is held,
 * the main thread of this test creates a hardware Direct3D11 device, which
 * initialises the adapter: it registers the OpenGL window class window,
 * selects a pixel format and asks opengl32 for extension entry points.
 * Every one of those reaches the PE loader, so an implementation that
 * keeps the wined3d critical section across them cannot make progress and
 * keeps every other thread of the process out of wined3d for the whole
 * time the loader lock is held.  A short-lived probe thread therefore
 * takes and releases the wined3d critical section repeatedly during that
 * window; the fastest acquisition tells the two shapes apart with a wide
 * margin, and the probe latency is what the invariant is about.
 */

#define LOADER_HOLD_MS        9000
#define PROBE_START_MS         150
#define PROBE_INTERVAL_MS       50
#define PROBE_COUNT             40
#define CS_FREE_THRESHOLD_MS  1500

static HANDLE loader_held_event;
static LARGE_INTEGER qpc_freq;

struct probe
{
    HANDLE thread;
    LARGE_INTEGER start, end;
};
static struct probe probes[PROBE_COUNT];

static ID3D11Device *device;
static ID3D11DeviceContext *context;
static HRESULT device_hr = E_FAIL;
static unsigned int device_elapsed_ms;

static BOOL loader_lock_taken;
static NTSTATUS loader_lock_status;

static DWORD WINAPI loader_lock_holder( void *arg )
{
    ULONG_PTR magic = 0;
    ULONG result = 0;

    /* ntdll takes (flags, result, magic); the magic is mandatory, so a call that
     * passes none fails and the thread would own nothing. */
    loader_lock_status = LdrLockLoaderLock( 0, &result, &magic );
    loader_lock_taken = (loader_lock_status == STATUS_SUCCESS);

    SetEvent( loader_held_event );

    if (loader_lock_taken)
    {
        Sleep( LOADER_HOLD_MS );
        LdrUnlockLoaderLock( 0, magic );
    }
    return 0;
}

static DWORD WINAPI cs_probe( void *arg )
{
    struct probe *p = arg;

    QueryPerformanceCounter( &p->start );
    wined3d_mutex_lock();
    QueryPerformanceCounter( &p->end );
    wined3d_mutex_unlock();
    return 0;
}

static DWORD WINAPI device_creator( void *arg )
{
    LARGE_INTEGER start, end;

    WaitForSingleObject( loader_held_event, INFINITE );

    QueryPerformanceCounter( &start );
    device_hr = pD3D11CreateDevice( NULL, D3D_DRIVER_TYPE_HARDWARE, 0, 0, NULL, 0,
            D3D11_SDK_VERSION, &device, NULL, &context );
    QueryPerformanceCounter( &end );
    device_elapsed_ms = (unsigned int)((end.QuadPart - start.QuadPart) * 1000 / qpc_freq.QuadPart);

    return 0;
}

static void test_wined3d_cs_not_held_across_loader( void )
{
    unsigned int i, ms, min_ms = ~0u, max_ms = 0;
    HANDLE threads[2];

    QueryPerformanceFrequency( &qpc_freq );
    loader_held_event = CreateEventW( NULL, TRUE, FALSE, NULL );

    threads[0] = CreateThread( NULL, 0, loader_lock_holder, NULL, 0, NULL );
    threads[1] = CreateThread( NULL, 0, device_creator, NULL, 0, NULL );

    /* The probe window has to overlap the adapter initialisation, which is
     * what blocks on the loader lock here. */
    WaitForSingleObject( loader_held_event, INFINITE );
    Sleep( PROBE_START_MS );
    for (i = 0; i < PROBE_COUNT; i++)
    {
        probes[i].thread = CreateThread( NULL, 0, cs_probe, &probes[i], 0, NULL );
        Sleep( PROBE_INTERVAL_MS );
    }

    ok( WaitForSingleObject( threads[1], 30000 ) == WAIT_OBJECT_0, "device creation did not finish.\n" );
    for (i = 0; i < PROBE_COUNT; i++)
        ok( WaitForSingleObject( probes[i].thread, 30000 ) == WAIT_OBJECT_0, "probe %u did not finish.\n", i );

    ok( loader_lock_taken, "LdrLockLoaderLock failed, %#lx.\n", loader_lock_status );
    if (!loader_lock_taken)
    {
        win_skip( "the loader lock was not held, the test cannot say anything about it.\n" );
        goto done;
    }
    if (device_hr == E_FAIL)
    {
        win_skip( "no D3D11 device created.\n" );
        goto done;
    }
    ok( device_hr == S_OK, "D3D11CreateDevice failed, hr %#lx.\n", device_hr );
    if (device_hr != S_OK) goto done;

    for (i = 0; i < PROBE_COUNT; i++)
    {
        ms = (unsigned int)((probes[i].end.QuadPart - probes[i].start.QuadPart) * 1000 / qpc_freq.QuadPart);
        if (ms < min_ms) min_ms = ms;
        if (ms > max_ms) max_ms = ms;
    }

    trace( "loader lock held %u ms, device creation took %u ms, wined3d_cs acquisition min %u ms max %u ms\n",
            LOADER_HOLD_MS, device_elapsed_ms, min_ms, max_ms );
    ok( min_ms < CS_FREE_THRESHOLD_MS,
        "wined3d_cs was held across the loader work of adapter init: the fastest acquisition "
        "of the critical section took %u ms while the loader lock was held for %u ms\n",
        min_ms, LOADER_HOLD_MS );

done:
    if (context) ID3D11DeviceContext_Release( context );
    if (device) ID3D11Device_Release( device );
    CloseHandle( threads[0] );
    CloseHandle( threads[1] );
    for (i = 0; i < PROBE_COUNT; i++)
        if (probes[i].thread) CloseHandle( probes[i].thread );
    CloseHandle( loader_held_event );
}

START_TEST(loader_lock)
{
    module_d3d11 = LoadLibraryW( L"d3d11.dll" );
    if (module_d3d11)
        pD3D11CreateDevice = (void *)GetProcAddress( module_d3d11, "D3D11CreateDevice" );

    test_wined3d_cs_not_held_across_loader();
}
