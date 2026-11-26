/*
 * Copyright 2020 Nikolay Sivov for CodeWeavers
 * Copyright 2023 Zhiyi Zhang for CodeWeavers
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
#include <assert.h>
#include <math.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "ntstatus.h"
#define WIN32_NO_STATUS
#define COBJMACROS
#include "windef.h"
#include "winbase.h"
#include "wingdi.h"
#include "winternl.h"
#include "initguid.h"
#include "objidl.h"
#include "dcomp_private.h"
#include "dxgi.h"
#include "dwmapi.h"
#include "d3d11_4.h"
#include <ddk/d3dkmthk.h>
#include "ntgdi.h"
#include "wine/server.h"
#include "wine/wined3d.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(dcomp);

/* Guards device->rendering_device and device->surface_factory, both of which
 * are created by calls into Direct3D. Locking order is dcomp_device_cs before
 * dcomp_cs, never the reverse, and dcomp_cs is never held across the creation
 * itself. */
static CRITICAL_SECTION dcomp_device_cs;
static CRITICAL_SECTION_DEBUG dcomp_device_debug =
{
    0, 0, &dcomp_device_cs,
    { &dcomp_device_debug.ProcessLocksList, &dcomp_device_debug.ProcessLocksList },
      0, 0, { (DWORD_PTR)(__FILE__ ": dcomp_device_cs") }
};
static CRITICAL_SECTION dcomp_device_cs = { &dcomp_device_debug, -1, 0, 0, 0, 0 };

static CRITICAL_SECTION dcomp_cs;
static CRITICAL_SECTION_DEBUG dcomp_debug =
{
    0, 0, &dcomp_cs,
    { &dcomp_debug.ProcessLocksList, &dcomp_debug.ProcessLocksList },
      0, 0, { (DWORD_PTR)(__FILE__ ": dcomp_cs") }
};
static CRITICAL_SECTION dcomp_cs = { &dcomp_debug, -1, 0, 0, 0, 0 };

void dcomp_lock(void)
{
    EnterCriticalSection(&dcomp_cs);
}

void dcomp_unlock(void)
{
    LeaveCriticalSection(&dcomp_cs);
}

struct list dcomp_devices = LIST_INIT(dcomp_devices);
volatile LONG dcomp_tree_version = 1;
volatile LONG dcomp_transform_version = 1;

/* A kernel call, not a window server round trip and not a lock: it is the only
 * call permitted while dcomp_cs is held, because it cannot be delayed by
 * another thread of this process. */
BOOL dcomp_compare_handles(HANDLE h1, HANDLE h2)
{
    if (h1 == h2) return TRUE;
    if (!h1 || !h2) return FALSE;
    return NtCompareObjects(h1, h2) == STATUS_SUCCESS;
}

HANDLE g_commit_completion_event = NULL;

/*
 * Emulated compositor present clock. The composite thread (composite_thread_proc)
 * is the process's vertical-blank cadence: under the X11 backend a Direct3D present
 * produces no D3DKMT vertical-blank event (winex11 has no vblank signalling), so the
 * per-frame wait in that loop is the frame tick. Each tick advances the sequence
 * counter and wakes all DCompositionWaitForCompositorClock waiters on the condition
 * variable so callers block until the next tick (never spinning on a stuck-signalled
 * manual-reset event).
 */
static CRITICAL_SECTION g_compositor_clock_cs;
static CRITICAL_SECTION_DEBUG g_compositor_clock_debug =
{
    0, 0, &g_compositor_clock_cs,
    { &g_compositor_clock_debug.ProcessLocksList, &g_compositor_clock_debug.ProcessLocksList },
      0, 0, { (DWORD_PTR)(__FILE__ ": g_compositor_clock_cs") }
};
static CRITICAL_SECTION g_compositor_clock_cs = { &g_compositor_clock_debug, -1, 0, 0, 0, 0 };
static CONDITION_VARIABLE g_compositor_clock_cv = CONDITION_VARIABLE_INIT;
static ULONG g_compositor_clock_seq;

void dcomp_signal_compositor_clock(void)
{
    EnterCriticalSection(&g_compositor_clock_cs);
    g_compositor_clock_seq++;
    WakeAllConditionVariable(&g_compositor_clock_cv);
    LeaveCriticalSection(&g_compositor_clock_cs);
}

/*
 * Composition-swapchain present signal. dxgi calls wine_dcomp_swapchain_presented()
 * from IDXGISwapChain1::Present() whenever a composition swapchain (one created by
 * CreateSwapChainForComposition, e.g. a XAML SwapChainPanel) pushes a new frame. It
 * raises this auto-reset event; the composite thread (composite_thread_proc) waits on
 * it in its idle path, so a Present-driven update is picked up on the next tick instead
 * of waiting for the idle poll throttle. The thread re-runs device_check_swapchains(),
 * which reads IDXGISwapChain::GetLastPresentCount() per content visual and re-composites
 * the target when a present count advanced. Raising the event is idempotent (auto-reset,
 * coalesces), so the event is not re-created per device: it is process-lifetime once the
 * first composite thread starts. The composite thread's own present of
 * target->composite_swapchain also routes through this hook; that swapchain is not a
 * visual's content, so the follow-up check finds no content-version change and the pass
 * is a cheap no-op: no present<->composite recursion.
 */
HANDLE g_swapchain_present_event = NULL;

/*
 * Compositor-clock waiter registry. A caller of DCompositionWaitForCompositorClock
 * passes the handles it parks on (the CoreMessaging lifted-input workers register the
 * handle the present clock is expected to signal per frame). Each caller handle is
 * duplicated into the 16-slot registry under dcomp_cs so the composite thread never
 * touches a raw caller handle after the caller closes it. If SetEvent fails with
 * ERROR_INVALID_HANDLE the slot's duplicated handle is closed and evicted; when the
 * 16-slot cap is full, the oldest slot (index 0) is closed and dropped to make room.
 */
HANDLE g_compositor_clock_handles[COMPOSITOR_CLOCK_MAX_HANDLES];
volatile LONG g_compositor_clock_handle_count;

static const D2D_RECT_F g_default_dcomp_rect = { 0.0f, 0.0f, 1016.0f, 734.0f };

static HRESULT WINAPI dcomp_6b556968_QueryInterface( void *iface, REFIID iid, void **out )
{
    *out = iface;
    return S_OK;
}
static ULONG WINAPI dcomp_6b556968_AddRef( void *iface ) { return 2; }
static ULONG WINAPI dcomp_6b556968_Release( void *iface ) { return 1; }
static HRESULT WINAPI dcomp_6b556968_slot3( void *iface ) { return S_OK; }
static const D2D_RECT_F * WINAPI dcomp_6b556968_GetRect( void *iface )
{
    return &g_default_dcomp_rect;
}
static HRESULT WINAPI dcomp_6b556968_SetCommitCompletionEvent( void *iface, HANDLE event )
{
    TRACE("DCOMP: dcomp_6b556968_SetCommitCompletionEvent iface %p event %p\n", iface, event);
    g_commit_completion_event = event;
    if (event) SetEvent(event);
    return S_OK;
}
static void * WINAPI dcomp_6b556968_GetTarget( void *iface )
{
    return iface;
}

struct dcomp_6b556968_vtbl
{
    HRESULT (WINAPI *QueryInterface)( void *iface, REFIID iid, void **out );
    ULONG (WINAPI *AddRef)( void *iface );
    ULONG (WINAPI *Release)( void *iface );
    HRESULT (WINAPI *slot3)( void *iface );
    const D2D_RECT_F * (WINAPI *GetRect)( void *iface );
    HRESULT (WINAPI *SetCommitCompletionEvent)( void *iface, HANDLE event );
    HRESULT (WINAPI *slot6)( void *iface );
    HRESULT (WINAPI *slot7)( void *iface );
    void * (WINAPI *GetTarget)( void *iface );
    HRESULT (WINAPI *slot9)( void *iface );
    HRESULT (WINAPI *slot10)( void *iface );
    HRESULT (WINAPI *slot11)( void *iface );
    HRESULT (WINAPI *slot12)( void *iface );
    HRESULT (WINAPI *slot13)( void *iface );
    HRESULT (WINAPI *slot14)( void *iface );
    HRESULT (WINAPI *slot15)( void *iface );
    HRESULT (WINAPI *slot16)( void *iface );
};

static const struct dcomp_6b556968_vtbl dcomp_6b556968_vtbl =
{
    dcomp_6b556968_QueryInterface, /* 0 (0x00) */
    dcomp_6b556968_AddRef,         /* 1 (0x08) */
    dcomp_6b556968_Release,        /* 2 (0x10) */
    dcomp_6b556968_slot3,          /* 3 (0x18) */
    dcomp_6b556968_GetRect,        /* 4 (0x20) */
    dcomp_6b556968_SetCommitCompletionEvent, /* 5 (0x28) */
    dcomp_6b556968_slot3,          /* 6 (0x30) */
    dcomp_6b556968_slot3,          /* 7 (0x38) */
    dcomp_6b556968_GetTarget,      /* 8 (0x40) */
    dcomp_6b556968_slot3,          /* 9 (0x48) */
    dcomp_6b556968_slot3,          /* 10 (0x50) */
    dcomp_6b556968_slot3,          /* 11 (0x58) */
    dcomp_6b556968_slot3,          /* 12 (0x60) */
    dcomp_6b556968_slot3,          /* 13 (0x68) */
    dcomp_6b556968_slot3,          /* 14 (0x70) */
    dcomp_6b556968_slot3,          /* 15 (0x78) */
    dcomp_6b556968_slot3,          /* 16 (0x80) */
};

static struct { const struct dcomp_6b556968_vtbl *vtbl; } dcomp_6b556968_obj = { &dcomp_6b556968_vtbl };

/*
 * IDCompositionDevice4 (composition textures, Windows 11 22H2) and
 * IDCompositionDevice5 (dynamic textures) are not implemented.  A composition device
 * created through DCompositionCreateDevice[23] answers them with the same object it
 * uses for IDCompositionDevice2/3/IDCompositionDesktopDevice, whose extra slots are
 * stubs, so that a client requesting the newest device version can use the
 * implemented functionality instead of failing device creation.
 */
static const GUID dcomp_device4_guid =
{ 0x85fc5cca, 0x2da6, 0x494c, { 0x86, 0xb6, 0x4a, 0x77, 0x5c, 0x04, 0x9b, 0x8a } };
static const GUID dcomp_device5_guid =
{ 0x2c6bebfe, 0xa603, 0x472f, { 0xaf, 0x34, 0xd2, 0x44, 0x33, 0x56, 0xe6, 0x1b } };

static BOOL is_device_version_guid(REFIID iid)
{
    return dcomp_is_equal_guid(iid, &dcomp_device4_guid) || dcomp_is_equal_guid(iid, &dcomp_device5_guid);
}

static BOOL is_partner_guid(REFIID iid)
{
    return dcomp_is_equal_guid(iid, &IID_IDCompositionDesktopDevicePartner)
        || dcomp_is_equal_guid(iid, &IID_IDCompositionDeviceUnknown)
        || dcomp_is_equal_guid(iid, (const GUID *)"\xfe\x33\x46\x5f\x08\x1e\xb8\x4c\x8c\x75\xce\x24\x33\x3f\x56\x02")
        || dcomp_is_equal_guid(iid, (const GUID *)"\x8d\x46\xf6\x75\x8e\x1b\x7c\x44\x9b\xc6\x75\xfe\xa8\x0b\x5b\x25")
        || dcomp_is_equal_guid(iid, (const GUID *)"\xc4\x2e\x68\x25\xa4\x73\x22\x40\xa0\x4f\x1b\xbe\x6d\xc3\xe1\xf0")
        || dcomp_is_equal_guid(iid, (const GUID *)"\x5f\x27\x1a\x3c\x75\xf1\x76\x46\x92\xb4\x06\x15\x30\xb2\xf2\xf7")
        || dcomp_is_equal_guid(iid, (const GUID *)"\xac\x1f\xb3\xdf\x91\xba\x57\x4b\xac\xed\xe7\x34\x7f\x50\x67\xe0")
        || dcomp_is_equal_guid(iid, (const GUID *)"\x58\x61\x4b\xd1\xfa\xc3\xce\x4b\x9c\x1f\xb6\x1d\x86\x65\xea\xb0");
}

static HRESULT STDMETHODCALLTYPE device_QueryInterface(IDCompositionDevice *iface,
        REFIID iid, void **out)
{
    struct composition_device *device = impl_from_IDCompositionDevice(iface);

    TRACE("iface %p, iid %s, out %p\n", iface, debugstr_guid(iid), out);

    if (dcomp_is_equal_guid(iid, &IID_IUnknown)
            || dcomp_is_equal_guid(iid, &IID_IDCompositionDevice))
    {
        IUnknown_AddRef(&device->IDCompositionDevice_iface);
        *out = &device->IDCompositionDevice_iface;
        return S_OK;
    }
    else if (dcomp_is_equal_guid(iid, (const GUID *)"\x68\x69\x55\x6b\x25\x2d\xe2\x11\x99\xf6\x8b\xc3\x60\x88\x70\x9b"))
    {
        *out = &dcomp_6b556968_obj;
        return S_OK;
    }
    else if ((device->version >= 2
              && (dcomp_is_equal_guid(iid, &IID_IDCompositionDevice2)
                  || dcomp_is_equal_guid(iid, &IID_IDCompositionDevice3)
                  || dcomp_is_equal_guid(iid, &IID_IDCompositionDesktopDevice)
                  || is_device_version_guid(iid)))
              || is_partner_guid(iid))
    {
        IUnknown_AddRef(&device->IDCompositionDeviceUnknown_iface);
        *out = &device->IDCompositionDeviceUnknown_iface;

        if (is_partner_guid(iid))
            FIXME("Returning undocumented interface %s %p.\n", wine_dbgstr_guid(iid), *out);
        return S_OK;
    }

    FIXME("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid));
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE device_AddRef(IDCompositionDevice *iface)
{
    struct composition_device *device = impl_from_IDCompositionDevice(iface);
    ULONG ref = InterlockedIncrement(&device->ref);

    TRACE("iface %p, ref %lu.\n", iface, ref);
    return ref;
}

static ULONG STDMETHODCALLTYPE device_Release(IDCompositionDevice *iface)
{
    struct composition_device *device = impl_from_IDCompositionDevice(iface);
    ULONG ref = InterlockedDecrement(&device->ref);

    TRACE("iface %p, ref %lu.\n", iface, ref);

    if (ref == 1 && device->surface_factory)
    {
        IDCompositionSurfaceFactory *factory = device->surface_factory;
        device->surface_factory = NULL;
        IDCompositionSurfaceFactory_Release(factory);
        return 0;
    }

    if (!ref)
    {
        dcomp_lock();
        list_remove(&device->dev_entry);
        dcomp_unlock();

        if (device->thread)
        {
            InterlockedExchange(&device->exit_thread, TRUE);
            if (device->wake_event)
                SetEvent(device->wake_event);
            if (GetThreadId(device->thread) == GetCurrentThreadId())
            {
                device->self_release = TRUE;
                return 0;
            }
            WaitForSingleObject(device->thread, INFINITE);
            CloseHandle(device->thread);
            device->thread = NULL;
        }
        dcomp_reclaim_drain();
        if (device->wake_event)
        {
            CloseHandle(device->wake_event);
            device->wake_event = NULL;
        }
        if (device->surface_factory)
        {
            IDCompositionSurfaceFactory *factory = device->surface_factory;
            device->surface_factory = NULL;
            IDCompositionSurfaceFactory_Release(factory);
        }
        if (device->rendering_device)
            IUnknown_Release(device->rendering_device);
        free(device->frame_buf);
        free(device->snap_buf);
        free(device->handle_buf);
        free(device);
    }

    return ref;
}

static HRESULT STDMETHODCALLTYPE device_Commit(IDCompositionDevice *iface)
{
    struct composition_device *device = impl_from_IDCompositionDevice(iface);
    struct composition_target *t;
    struct { HANDLE handle; IDCompositionVisual *root; } to_republish[16];
    struct { HWND hwnd; IDCompositionVisual *root; } to_publish_hwnd[16];
    int n_republish = 0, n_publish_hwnd = 0, i;
    HRESULT hr = S_OK;

    TRACE("DCOMP: device_Commit iface %p\n", iface);

    dcomp_lock();
    LIST_FOR_EACH_ENTRY(t, &device->targets, struct composition_target, entry)
    {
        TRACE("DCOMP: device_Commit device %p has target %p hwnd %p root %p\n",
            device, t, t->hwnd, t->root);
        if (t->shared_visual_handle && t->root && n_republish < (int)ARRAY_SIZE(to_republish))
        {
            to_republish[n_republish].handle = t->shared_visual_handle;
            to_republish[n_republish].root = t->root;
            IDCompositionVisual_AddRef(t->root);
            n_republish++;
        }
        if (t->hwnd && t->root && n_publish_hwnd < (int)ARRAY_SIZE(to_publish_hwnd))
        {
            to_publish_hwnd[n_publish_hwnd].hwnd = t->hwnd;
            to_publish_hwnd[n_publish_hwnd].root = t->root;
            IDCompositionVisual_AddRef(t->root);
            n_publish_hwnd++;
        }
    }
    dcomp_unlock();

    for (i = 0; i < n_publish_hwnd; i++)
    {
        HANDLE surf_h = NULL;
        UINT w = 0, h = 0;
        visual_publish_shared_content(impl_from_IDCompositionVisual(to_publish_hwnd[i].root), &surf_h, &w, &h);
        if (surf_h)
        {
            static LONG hwnd_frame_seq;
            ULONG_PTR seq = (ULONG_PTR)InterlockedIncrement(&hwnd_frame_seq);
            HWND hw = to_publish_hwnd[i].hwnd;
            while (hw)
            {
                SetPropW(hw, L"__wine_dcomp_hwnd_surface", surf_h);
                SetPropW(hw, L"__wine_dcomp_hwnd_frame", (HANDLE)seq);
                if (w && h)
                    SetPropW(hw, L"__wine_dcomp_hwnd_size",
                            (HANDLE)(ULONG_PTR)(((ULONG_PTR)w << 16) | (h & 0xffff)));
                if (GetPropW(hw, L"__wine_dcomp_captured_hwnd"))
                {
                    DWORD wpid = 0;
                    GetWindowThreadProcessId(hw, &wpid);
                    if (wpid == GetCurrentProcessId())
                    {
                        LONG_PTR ex = GetWindowLongPtrW(hw, GWL_EXSTYLE);
                        if ((ex & WS_EX_APPWINDOW) || !(ex & WS_EX_TOOLWINDOW) || !(ex & WS_EX_NOACTIVATE))
                        {
                            SetWindowLongPtrW(hw, GWL_EXSTYLE,
                                    (ex & ~WS_EX_APPWINDOW) | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE);
                            SetWindowPos(hw, 0, 0, 0, 0, 0,
                                    SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_ASYNCWINDOWPOS);
                        }
                    }
                }
                hw = GetParent(hw);
            }
        }
        IDCompositionVisual_Release(to_publish_hwnd[i].root);
    }

    for (i = 0; i < n_republish; i++)
    {
        shared_visual_target_set_root(to_republish[i].handle, to_republish[i].root);
        dcomp_lock();
        shared_visual_cache_set(to_republish[i].handle, to_republish[i].root);
        dcomp_unlock();
        IDCompositionVisual_Release(to_republish[i].root);
    }

    device->committed = TRUE;
    ensure_composite_thread(device);

    dcomp_render_lock();
    if (dcomp_qpc_elapsed_ms(device->last_composite_qpc, device->qpc_freq) >= device->refresh_period_ms)
    {
        InterlockedExchange(&device->commit_pending, FALSE);
        if (composite_device(device))
            device->last_composite_qpc = dcomp_qpc_now();
        dcomp_signal_compositor_clock();
        if (g_commit_completion_event)
            SetEvent(g_commit_completion_event);
    }
    else
    {
        InterlockedExchange(&device->commit_pending, TRUE);
    }
    dcomp_render_unlock();
    if (device->wake_event)
        SetEvent(device->wake_event);

    /* Cross-device visual trees (e.g. WebView2 / ContentExternalOutputLink):
     * wake any other committed device that owns targets so its composite thread
     * picks up visuals grafted into its tree by this device. */
    if (n_publish_hwnd == 0)
    {
        struct composition_device *other;
        dcomp_lock();
        LIST_FOR_EACH_ENTRY(other, &dcomp_devices, struct composition_device, dev_entry)
        {
            if (other != device && other->committed && !list_empty(&other->targets))
            {
                InterlockedExchange(&other->commit_pending, TRUE);
                if (other->wake_event)
                    SetEvent(other->wake_event);
            }
        }
        dcomp_unlock();
    }

    return hr;
}

static HRESULT STDMETHODCALLTYPE device_WaitForCommitCompletion(IDCompositionDevice *iface)
{
    TRACE("iface %p\n", iface);
    /* gate100 force-present-wake test: pulse the commit-completion event on every
     * wait so the CoreMessagingXP composition-input threads (Composition Token /
     * Lifted DMIT / DWM Master Input) that parked on the DirectComposition commit
     * completion advance and issue LRPC request 7 on the composition plane. */
    if (g_commit_completion_event) SetEvent( g_commit_completion_event );
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE device_GetFrameStatistics(IDCompositionDevice *iface,
        DCOMPOSITION_FRAME_STATISTICS *statistics)
{
    struct composition_device *device = impl_from_IDCompositionDevice(iface);
    LARGE_INTEGER now, freq;

    TRACE("iface %p, statistics %p\n", iface, statistics);
    if (!statistics) return E_POINTER;

    memset(statistics, 0, sizeof(*statistics));
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&freq);
    statistics->lastFrameTime.QuadPart = (LONGLONG)device->last_composite_qpc;
    statistics->currentCompositionRate.Numerator = 60;
    statistics->currentCompositionRate.Denominator = 1;
    statistics->currentTime = now;
    statistics->timeFrequency = freq;
    if (freq.QuadPart)
        statistics->nextEstimatedFrameTime.QuadPart = (LONGLONG)(device->last_composite_qpc
                + ((ULONGLONG)freq.QuadPart * device->refresh_period_ms) / 1000ULL);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE device_CreateTargetForHwnd(IDCompositionDevice *iface,
        HWND hwnd, BOOL topmost, IDCompositionTarget **target)
{
    struct composition_device *device = impl_from_IDCompositionDevice(iface);

    TRACE("DCOMP: device_CreateTargetForHwnd iface %p, hwnd %p, topmost %d, target %p\n", iface, hwnd, topmost, target);

    if (!target)
        return E_INVALIDARG;
    *target = NULL;
    if (!hwnd || hwnd == GetDesktopWindow())
        return E_INVALIDARG;

    return create_target(device, hwnd, topmost, target);
}

static HRESULT STDMETHODCALLTYPE device_CreateVisual(IDCompositionDevice *iface,
        IDCompositionVisual **visual)
{
    HRESULT hr;

    TRACE("iface %p, visual %p\n", iface, visual);

    if (!visual)
        return E_INVALIDARG;
    *visual = NULL;

    hr = create_visual(1, &IID_IDCompositionVisual, (void **)visual);
    TRACE("DCOMP: device_CreateVisual iface %p, out_visual %p, hr %#lx\n", iface, *visual, hr);
    return hr;
}

/* Resolved during process attach. LoadLibrary() and GetProcAddress() take the
 * loader lock, which must never be entered while a critical section is held: a
 * thread inside DllMain owns the loader lock and can reach the same Direct3D code
 * from its own initialisation, so the rendering device a client call creates has
 * to be built without any loader traffic. The reference is kept for the lifetime
 * of the process; on Windows the composition engine's Direct3D dependency is a
 * static one, so the module is loaded in every case as well. */
static HMODULE dcomp_d3d11_module;
static HRESULT (WINAPI *pD3D11CreateDevice)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT,
        const D3D_FEATURE_LEVEL *, UINT, UINT, ID3D11Device **, D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);

static void dcomp_init_rendering_module(void)
{
    if (dcomp_d3d11_module) return;

    if (!(dcomp_d3d11_module = LoadLibraryA("d3d11.dll")))
        return;

    if (!(pD3D11CreateDevice = (void *)GetProcAddress(dcomp_d3d11_module, "D3D11CreateDevice")))
    {
        FreeLibrary(dcomp_d3d11_module);
        dcomp_d3d11_module = NULL;
    }
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, void *reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(inst);
        dcomp_init_rendering_module();
    }
    return TRUE;
}

HRESULT get_default_rendering_device(IUnknown **out_device)
{
    ID3D11Device *d3d11_device = NULL;
    HRESULT hr = E_FAIL;

    if (pD3D11CreateDevice)
    {
        static const D3D_FEATURE_LEVEL feature_levels[] =
        {
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_1,
            D3D_FEATURE_LEVEL_10_0,
        };

        hr = pD3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL,
                D3D11_CREATE_DEVICE_BGRA_SUPPORT, feature_levels, ARRAY_SIZE(feature_levels),
                D3D11_SDK_VERSION, &d3d11_device, NULL, NULL);
        if (FAILED(hr))
        {
            hr = pD3D11CreateDevice(NULL, D3D_DRIVER_TYPE_WARP, NULL,
                    D3D11_CREATE_DEVICE_BGRA_SUPPORT, feature_levels, ARRAY_SIZE(feature_levels),
                    D3D11_SDK_VERSION, &d3d11_device, NULL, NULL);
        }
        if (FAILED(hr))
        {
            hr = pD3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL,
                    0, feature_levels, ARRAY_SIZE(feature_levels),
                    D3D11_SDK_VERSION, &d3d11_device, NULL, NULL);
        }
        if (FAILED(hr))
        {
            hr = pD3D11CreateDevice(NULL, D3D_DRIVER_TYPE_WARP, NULL,
                    0, feature_levels, ARRAY_SIZE(feature_levels),
                    D3D11_SDK_VERSION, &d3d11_device, NULL, NULL);
        }
    }

    if (SUCCEEDED(hr) && d3d11_device)
    {
        *out_device = (IUnknown *)d3d11_device;
        return S_OK;
    }

    ERR("Failed to create default D3D11 rendering device, hr %#lx.\n", hr);
    return hr;
}

HRESULT device_get_or_create_rendering_device(struct composition_device *device,
        IUnknown **out_rendering_device)
{
    HRESULT hr;

    if (device->rendering_device)
    {
        *out_rendering_device = device->rendering_device;
        IUnknown_AddRef(*out_rendering_device);
        return S_OK;
    }

    hr = get_default_rendering_device(&device->rendering_device);
    if (SUCCEEDED(hr) && device->rendering_device)
    {
        *out_rendering_device = device->rendering_device;
        IUnknown_AddRef(*out_rendering_device);
        return S_OK;
    }

    return hr;
}

/* Every surface the device creates comes from its own IDCompositionSurfaceFactory;
 * a client that never called IDCompositionDevice2::CreateSurfaceFactory() gets one
 * built on the device's rendering device (a default D3D11 device when the client
 * passed no rendering device at all).  Both are created *outside* dcomp_device_cs:
 * creating them enters Direct3D, which loads dlls and takes the wined3d critical
 * section, and the loader lock must never be entered while a critical section is
 * held (a thread inside DllMain owns the loader lock and reaches the same code).
 * The reference published first wins; the returned pointer is the device's own
 * reference, not an extra one. */
static HRESULT device_get_or_create_surface_factory(struct composition_device *device,
        IDCompositionSurfaceFactory **out_factory)
{
    IDCompositionSurfaceFactory *factory, *published;
    IUnknown *rendering_device = NULL;
    HRESULT hr;

    EnterCriticalSection(&dcomp_device_cs);
    factory = device->surface_factory;
    LeaveCriticalSection(&dcomp_device_cs);

    if (factory)
    {
        *out_factory = factory;
        return S_OK;
    }

    if (FAILED(hr = device_get_or_create_rendering_device(device, &rendering_device)) || !rendering_device)
    {
        ERR("Failed to get rendering device for composition device %p, hr %#lx.\n", device, hr);
        return hr;
    }

    hr = create_surface_factory(device, rendering_device, &factory);
    IUnknown_Release(rendering_device);
    if (FAILED(hr))
    {
        ERR("Failed to create surface factory for composition device %p, hr %#lx.\n", device, hr);
        return hr;
    }

    EnterCriticalSection(&dcomp_device_cs);
    if (!device->surface_factory)
        device->surface_factory = factory;
    published = device->surface_factory;
    LeaveCriticalSection(&dcomp_device_cs);

    if (published != factory) IDCompositionSurfaceFactory_Release(factory);

    *out_factory = published;
    return S_OK;
}

static HRESULT composition_device_create_surface(struct composition_device *device,
        UINT width, UINT height, DXGI_FORMAT pixel_format, DXGI_ALPHA_MODE alpha_mode,
        IDCompositionSurface **surface)
{
    IDCompositionSurfaceFactory *factory;
    HRESULT hr;

    TRACE("DCOMP: CreateSurface device %p, width %u, height %u, format %#x, alpha_mode %#x, surface %p\n",
        device, width, height, pixel_format, alpha_mode, surface);

    if (!surface)
        return E_POINTER;

    *surface = NULL;

    if (FAILED(hr = device_get_or_create_surface_factory(device, &factory)))
        return hr;

    /* Creating the surface enters Direct3D and Direct2D. */
    return create_surface(impl_from_IDCompositionSurfaceFactory(factory),
            width, height, pixel_format, alpha_mode, surface);
}

static HRESULT composition_device_create_virtual_surface(struct composition_device *device,
        UINT width, UINT height, DXGI_FORMAT pixel_format, DXGI_ALPHA_MODE alpha_mode,
        IDCompositionVirtualSurface **surface)
{
    IDCompositionSurfaceFactory *factory;
    HRESULT hr;

    TRACE("DCOMP: CreateVirtualSurface device %p, width %u, height %u, format %#x, alpha_mode %#x, surface %p\n",
        device, width, height, pixel_format, alpha_mode, surface);

    if (!surface)
        return E_POINTER;

    *surface = NULL;

    if (FAILED(hr = device_get_or_create_surface_factory(device, &factory)))
        return hr;

    return create_virtual_surface(impl_from_IDCompositionSurfaceFactory(factory),
            width, height, pixel_format, alpha_mode, surface);
}

static HRESULT STDMETHODCALLTYPE device_CreateSurface(IDCompositionDevice *iface,
        UINT width, UINT height, DXGI_FORMAT pixel_format, DXGI_ALPHA_MODE alpha_mode,
        IDCompositionSurface **surface)
{
    struct composition_device *device = impl_from_IDCompositionDevice(iface);

    return composition_device_create_surface(device, width, height, pixel_format, alpha_mode, surface);
}

static HRESULT STDMETHODCALLTYPE device_CreateVirtualSurface(IDCompositionDevice *iface,
        UINT width, UINT height, DXGI_FORMAT pixel_format, DXGI_ALPHA_MODE alpha_mode,
        IDCompositionVirtualSurface **surface)
{
    struct composition_device *device = impl_from_IDCompositionDevice(iface);

    return composition_device_create_virtual_surface(device, width, height, pixel_format, alpha_mode, surface);
}

static HRESULT STDMETHODCALLTYPE device_CreateSurfaceFromHandle(IDCompositionDevice *iface,
        HANDLE handle, IUnknown **surface)
{
    struct composition_device *device = impl_from_IDCompositionDevice(iface);
    return dcomp_create_surface_from_handle(device, handle, surface);
}

static HRESULT STDMETHODCALLTYPE device_CreateSurfaceFromHwnd(IDCompositionDevice *iface,
        HWND hwnd, IUnknown **surface)
{
    struct composition_device *device = impl_from_IDCompositionDevice(iface);
    IDCompositionSurface *dcomp_surface = NULL;
    RECT rect;
    UINT width, height;
    HRESULT hr;

    FIXME("iface %p, hwnd %p, surface %p semi-stub!\n", iface, hwnd, surface);

    if (!surface)
        return E_POINTER;

    *surface = NULL;

    if (!hwnd || !IsWindow(hwnd))
        return E_INVALIDARG;

    GetClientRect(hwnd, &rect);
    width = rect.right > 0 ? rect.right : 100;
    height = rect.bottom > 0 ? rect.bottom : 100;

    hr = composition_device_create_surface(device, width, height,
            DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_ALPHA_MODE_PREMULTIPLIED, &dcomp_surface);
    if (SUCCEEDED(hr))
    {
        struct composition_surface *surf_impl = unsafe_impl_from_IDCompositionSurface(dcomp_surface);
        if (surf_impl)
            surf_impl->source_hwnd = hwnd;
        dcomp_hide_captured_hwnd(hwnd);
        *surface = (IUnknown *)dcomp_surface;
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE device_CreateTranslateTransform(IDCompositionDevice *iface,
        IDCompositionTranslateTransform **transform)
{
    struct composition_device *device = impl_from_IDCompositionDevice( iface );
    struct dcomp_transform *impl;

    TRACE("DCOMP: device_CreateTranslateTransform iface %p, transform %p\n", iface, transform);

    if (!transform)
        return E_POINTER;

    if (!(impl = dcomp_transform_create( device, TRANSFORM_TRANSLATE )))
        return E_OUTOFMEMORY;

    *transform = (IDCompositionTranslateTransform *)&impl->IDCompositionTranslateTransform_iface;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE device_CreateScaleTransform(IDCompositionDevice *iface,
        IDCompositionScaleTransform **transform)
{
    struct composition_device *device = impl_from_IDCompositionDevice( iface );
    struct dcomp_transform *impl;

    TRACE("DCOMP: device_CreateScaleTransform iface %p, transform %p\n", iface, transform);

    if (!transform)
        return E_POINTER;

    if (!(impl = dcomp_transform_create( device, TRANSFORM_SCALE )))
        return E_OUTOFMEMORY;

    *transform = (IDCompositionScaleTransform *)&impl->IDCompositionScaleTransform_iface;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE device_CreateRotateTransform(IDCompositionDevice *iface,
        IDCompositionRotateTransform **transform)
{
    struct composition_device *device = impl_from_IDCompositionDevice( iface );
    struct dcomp_transform *impl;

    TRACE("DCOMP: device_CreateRotateTransform iface %p, transform %p\n", iface, transform);

    if (!transform)
        return E_POINTER;

    if (!(impl = dcomp_transform_create( device, TRANSFORM_ROTATE )))
        return E_OUTOFMEMORY;

    *transform = (IDCompositionRotateTransform *)&impl->IDCompositionRotateTransform_iface;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE device_CreateSkewTransform(IDCompositionDevice *iface,
        IDCompositionSkewTransform **transform)
{
    struct composition_device *device = impl_from_IDCompositionDevice( iface );
    struct dcomp_transform *impl;

    TRACE("DCOMP: device_CreateSkewTransform iface %p, transform %p\n", iface, transform);

    if (!transform)
        return E_POINTER;

    if (!(impl = dcomp_transform_create( device, TRANSFORM_SKEW )))
        return E_OUTOFMEMORY;

    *transform = (IDCompositionSkewTransform *)&impl->IDCompositionSkewTransform_iface;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE device_CreateMatrixTransform(IDCompositionDevice *iface,
        IDCompositionMatrixTransform **transform)
{
    struct composition_device *device = impl_from_IDCompositionDevice( iface );
    struct dcomp_transform *impl;

    TRACE("DCOMP: device_CreateMatrixTransform iface %p, transform %p\n", iface, transform);

    if (!transform)
        return E_POINTER;

    if (!(impl = dcomp_transform_create( device, TRANSFORM_MATRIX )))
        return E_OUTOFMEMORY;

    *transform = (IDCompositionMatrixTransform *)&impl->IDCompositionMatrixTransform_iface;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE device_CreateTransformGroup(IDCompositionDevice *iface,
        IDCompositionTransform **transforms, UINT elements,
        IDCompositionTransform **transform_group)
{
    struct composition_device *device = impl_from_IDCompositionDevice( iface );
    struct dcomp_transform *impl;
    UINT i;

    TRACE("DCOMP: device_CreateTransformGroup iface %p, transforms %p, elements %u, transform_group %p\n",
            iface, transforms, elements, transform_group);

    if (!transform_group)
        return E_POINTER;
    *transform_group = NULL;

    if (!transforms || !elements)
        return E_INVALIDARG;
    for (i = 0; i < elements; i++)
        if (!transforms[i])
            return E_INVALIDARG;

    if (!(impl = dcomp_transform_group_create( device, TRANSFORM_GROUP,
            (IUnknown *const *)transforms, elements )))
        return E_OUTOFMEMORY;

    *transform_group = (IDCompositionTransform *)&impl->IDCompositionTranslateTransform_iface;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE device_CreateTranslateTransform3D(IDCompositionDevice *iface,
        IDCompositionTranslateTransform3D **transform_3d)
{
    struct composition_device *device = impl_from_IDCompositionDevice( iface );
    struct dcomp_transform *impl;

    TRACE("DCOMP: device_CreateTranslateTransform3D iface %p, transform_3d %p\n", iface, transform_3d);

    if (!transform_3d)
        return E_POINTER;

    if (!(impl = dcomp_transform_create( device, TRANSFORM_TRANSLATE_3D )))
        return E_OUTOFMEMORY;

    *transform_3d = (IDCompositionTranslateTransform3D *)&impl->IDCompositionTranslateTransform3D_iface;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE device_CreateScaleTransform3D(IDCompositionDevice *iface,
        IDCompositionScaleTransform3D **transform_3d)
{
    struct composition_device *device = impl_from_IDCompositionDevice( iface );
    struct dcomp_transform *impl;

    TRACE("DCOMP: device_CreateScaleTransform3D iface %p, transform_3d %p\n", iface, transform_3d);

    if (!transform_3d)
        return E_POINTER;

    if (!(impl = dcomp_transform_create( device, TRANSFORM_SCALE_3D )))
        return E_OUTOFMEMORY;

    *transform_3d = (IDCompositionScaleTransform3D *)&impl->IDCompositionScaleTransform3D_iface;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE device_CreateRotateTransform3D(IDCompositionDevice *iface,
        IDCompositionRotateTransform3D **transform_3d)
{
    struct composition_device *device = impl_from_IDCompositionDevice( iface );
    struct dcomp_transform *impl;

    TRACE("DCOMP: device_CreateRotateTransform3D iface %p, transform_3d %p\n", iface, transform_3d);

    if (!transform_3d)
        return E_POINTER;

    if (!(impl = dcomp_transform_create( device, TRANSFORM_ROTATE_3D )))
        return E_OUTOFMEMORY;

    *transform_3d = (IDCompositionRotateTransform3D *)&impl->IDCompositionRotateTransform3D_iface;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE device_CreateMatrixTransform3D(IDCompositionDevice *iface,
        IDCompositionMatrixTransform3D **transform_3d)
{
    struct composition_device *device = impl_from_IDCompositionDevice( iface );
    struct dcomp_transform *impl;

    TRACE("DCOMP: device_CreateMatrixTransform3D iface %p, transform_3d %p\n", iface, transform_3d);

    if (!transform_3d)
        return E_POINTER;

    if (!(impl = dcomp_transform_create( device, TRANSFORM_MATRIX_3D )))
        return E_OUTOFMEMORY;

    *transform_3d = (IDCompositionMatrixTransform3D *)&impl->IDCompositionMatrixTransform3D_iface;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE device_CreateTransform3DGroup(IDCompositionDevice *iface,
        IDCompositionTransform3D **transforms_3d, UINT elements,
        IDCompositionTransform3D **transform_3d_group)
{
    struct composition_device *device = impl_from_IDCompositionDevice( iface );
    struct dcomp_transform *impl;
    UINT i;

    TRACE("DCOMP: device_CreateTransform3DGroup iface %p, transforms_3d %p, elements %u, transform_3d_group %p\n",
            iface, transforms_3d, elements, transform_3d_group);

    if (!transform_3d_group)
        return E_POINTER;
    *transform_3d_group = NULL;

    if (!transforms_3d || !elements)
        return E_INVALIDARG;
    for (i = 0; i < elements; i++)
        if (!transforms_3d[i])
            return E_INVALIDARG;

    if (!(impl = dcomp_transform_group_create( device, TRANSFORM_3D_GROUP,
            (IUnknown *const *)transforms_3d, elements )))
        return E_OUTOFMEMORY;

    *transform_3d_group = (IDCompositionTransform3D *)&impl->IDCompositionTranslateTransform3D_iface;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE device_CreateEffectGroup(IDCompositionDevice *iface,
        IDCompositionEffectGroup **effect_group)
{
    struct composition_device *device = impl_from_IDCompositionDevice( iface );
    struct dcomp_transform *impl;

    TRACE("DCOMP: device_CreateEffectGroup iface %p, effect_group %p\n", iface, effect_group);

    if (!effect_group)
        return E_POINTER;

    if (!(impl = dcomp_transform_create( device, OBJECT_EFFECT_GROUP )))
        return E_OUTOFMEMORY;

    *effect_group = (IDCompositionEffectGroup *)&impl->IDCompositionEffectGroup_iface;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE device_CreateRectangleClip(IDCompositionDevice *iface,
        IDCompositionRectangleClip **clip)
{
    struct composition_device *device = impl_from_IDCompositionDevice( iface );
    struct dcomp_transform *impl;

    TRACE("DCOMP: device_CreateRectangleClip iface %p, clip %p\n", iface, clip);

    if (!clip)
        return E_POINTER;

    if (!(impl = dcomp_transform_create( device, OBJECT_RECTANGLE_CLIP )))
        return E_OUTOFMEMORY;

    *clip = (IDCompositionRectangleClip *)&impl->IDCompositionRectangleClip_iface;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE device_CreateAnimation(IDCompositionDevice *iface,
        IDCompositionAnimation **animation)
{
    FIXME("iface %p, animation %p stub!\n", iface, animation);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE device_CheckDeviceState(IDCompositionDevice *iface,
        BOOL *valid)
{
    TRACE("iface %p, valid %p\n", iface, valid);
    if (valid) *valid = TRUE;
    return S_OK;
}

/*
 * Method coverage of the composition device (IDCompositionDevice vtable).
 *
 *   Commit, WaitForCommitCompletion, GetFrameStatistics   implemented; the compositor
 *                                                          is emulated, see composite_thread_proc()
 *   CreateTargetForHwnd, CreateVisual                     implemented (target.c, visual.c)
 *   CreateSurface, CreateVirtualSurface                   implemented (surface.c) through the device
 *                                                          surface factory, which is created on demand
 *   CreateSurfaceFromHandle                               implemented when the rendering device is an
 *                                                          ID3D11Device(1): the handle is opened with
 *                                                          OpenSharedResource[1] as an IDXGISurface and
 *                                                          wrapped; E_NOINTERFACE for a rendering
 *                                                          device that is only IDXGIDevice/ID2D1Device
 *   CreateSurfaceFromHwnd                                 semi-stub: returns a new B8G8R8A8 surface of
 *                                                          the window client size, not the window's own
 *                                                          redirection surface
 *   Create{Translate,Scale,Rotate,Skew,Matrix}Transform   implemented (transform.c)
 *   Create{Translate,Scale,Rotate,Matrix}Transform3D      implemented (transform.c)
 *   CreateTransformGroup, CreateTransform3DGroup          E_NOTIMPL: a group is a transform whose
 *                                                          effective matrix is the concatenation of its
 *                                                          children, the object in transform.c
 *                                                          implements single transforms only and no
 *                                                          client of this fork builds groups, so a group
 *                                                          the compositor would silently ignore is not
 *                                                          faked up
 *   CreateEffectGroup, CreateRectangleClip                implemented (transform.c)
 *   CreateAnimation                                       E_NOTIMPL: this tree has no animation
 *                                                          byte-code parser in dlls/dcomp, and the
 *                                                          Set*Animation entry points reject the
 *                                                          animation pointer
 *   CheckDeviceState                                      implemented: the device is never lost,
 *                                                          *valid = TRUE
 *
 * No DirectComposition interface has a SetMaxSize() entry point: the device
 * interfaces are IDCompositionDevice{,2,3}, IDCompositionDesktopDevice{,Partner} and
 * the private IDCompositionDeviceUnknown (checked against include/dcomp.idl and
 * dlls/dcomp/dcomp_private_iface.idl), the surface, virtual surface, surface factory
 * and target interfaces are checked as well, and none declares it.  The interface
 * that changes a surface size is IDCompositionVirtualSurface::Resize(), and the size
 * is validated at creation time by create_surface().
 */
static const struct IDCompositionDeviceVtbl device_vtbl =
{
    /* IUnknown methods */
    device_QueryInterface,
    device_AddRef,
    device_Release,
    /* IDCompositionDevice methods */
    device_Commit,
    device_WaitForCommitCompletion,
    device_GetFrameStatistics,
    device_CreateTargetForHwnd,
    device_CreateVisual,
    device_CreateSurface,
    device_CreateVirtualSurface,
    device_CreateSurfaceFromHandle,
    device_CreateSurfaceFromHwnd,
    device_CreateTranslateTransform,
    device_CreateScaleTransform,
    device_CreateRotateTransform,
    device_CreateSkewTransform,
    device_CreateMatrixTransform,
    device_CreateTransformGroup,
    device_CreateTranslateTransform3D,
    device_CreateScaleTransform3D,
    device_CreateRotateTransform3D,
    device_CreateMatrixTransform3D,
    device_CreateTransform3DGroup,
    device_CreateEffectGroup,
    device_CreateRectangleClip,
    device_CreateAnimation,
    device_CheckDeviceState,
};

static HRESULT STDMETHODCALLTYPE desktop_device_QueryInterface(IDCompositionDeviceUnknown *iface,
        REFIID iid, void **out)
{
    struct composition_device *device = impl_from_IDCompositionDeviceUnknown(iface);

    TRACE("iface %p.\n", iface);

    return IDCompositionDevice_QueryInterface(&device->IDCompositionDevice_iface, iid, out);
}

static ULONG STDMETHODCALLTYPE desktop_device_AddRef(IDCompositionDeviceUnknown *iface)
{
    struct composition_device *device = impl_from_IDCompositionDeviceUnknown(iface);

    TRACE("iface %p.\n", iface);

    return IDCompositionDevice_AddRef(&device->IDCompositionDevice_iface);
}

static ULONG STDMETHODCALLTYPE desktop_device_Release(IDCompositionDeviceUnknown *iface)
{
    struct composition_device *device = impl_from_IDCompositionDeviceUnknown(iface);

    TRACE("iface %p.\n", iface);

    return IDCompositionDevice_Release(&device->IDCompositionDevice_iface);
}

static HRESULT STDMETHODCALLTYPE desktop_device_Commit(IDCompositionDeviceUnknown *iface)
{
    struct composition_device *device = impl_from_IDCompositionDeviceUnknown(iface);

    TRACE("iface %p.\n", iface);

    return IDCompositionDevice_Commit(&device->IDCompositionDevice_iface);
}

static HRESULT STDMETHODCALLTYPE desktop_device_WaitForCommitCompletion(IDCompositionDeviceUnknown *iface)
{
    TRACE("iface %p\n", iface);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE desktop_device_GetFrameStatistics(IDCompositionDeviceUnknown *iface,
        DCOMPOSITION_FRAME_STATISTICS *statistics)
{
    struct composition_device *device = impl_from_IDCompositionDeviceUnknown(iface);
    return device_GetFrameStatistics(&device->IDCompositionDevice_iface, statistics);
}

static HRESULT STDMETHODCALLTYPE desktop_device_CreateVisual(IDCompositionDeviceUnknown *iface,
        IDCompositionVisual2 **visual)
{
    HRESULT hr;

    TRACE("iface %p, visual %p\n", iface, visual);

    if (!visual)
        return E_INVALIDARG;
    *visual = NULL;

    hr = create_visual(2, &IID_IDCompositionVisual2, (void **)visual);
    TRACE("DCOMP: desktop_device_CreateVisual iface %p, out_visual %p, hr %#lx\n", iface, *visual, hr);
    return hr;
}

static HRESULT STDMETHODCALLTYPE desktop_device_CreateSurfaceFactory(IDCompositionDeviceUnknown *iface,
        IUnknown *rendering_device, IDCompositionSurfaceFactory **surface_factory)
{
    struct composition_device *device = impl_from_IDCompositionDeviceUnknown(iface);

    TRACE("iface %p, rendering_device %p, surface_factory %p.\n", iface, rendering_device, surface_factory);

    if (!device->rendering_device && rendering_device)
    {
        device->rendering_device = rendering_device;
        IUnknown_AddRef(rendering_device);
    }

    return create_surface_factory(device, rendering_device, surface_factory);
}

static HRESULT STDMETHODCALLTYPE desktop_device_CreateSurface(IDCompositionDeviceUnknown *iface,
        UINT width, UINT height, DXGI_FORMAT pixel_format, DXGI_ALPHA_MODE alpha_mode,
        IDCompositionSurface **surface)
{
    struct composition_device *device = impl_from_IDCompositionDeviceUnknown(iface);

    return composition_device_create_surface(device, width, height, pixel_format, alpha_mode, surface);
}

static HRESULT STDMETHODCALLTYPE desktop_device_CreateVirtualSurface(IDCompositionDeviceUnknown *iface,
        UINT width, UINT height, DXGI_FORMAT pixel_format, DXGI_ALPHA_MODE alpha_mode,
        IDCompositionVirtualSurface **surface)
{
    struct composition_device *device = impl_from_IDCompositionDeviceUnknown(iface);

    return device_CreateVirtualSurface(&device->IDCompositionDevice_iface, width, height,
            pixel_format, alpha_mode, surface);
}

static HRESULT STDMETHODCALLTYPE desktop_device_CreateTranslateTransform(IDCompositionDeviceUnknown *iface,
        IDCompositionTranslateTransform **transform)
{
    struct composition_device *device = impl_from_IDCompositionDeviceUnknown( iface );
    return device_CreateTranslateTransform(&device->IDCompositionDevice_iface, transform);
}

static HRESULT STDMETHODCALLTYPE desktop_device_CreateScaleTransform(IDCompositionDeviceUnknown *iface,
        IDCompositionScaleTransform **transform)
{
    struct composition_device *device = impl_from_IDCompositionDeviceUnknown( iface );
    return device_CreateScaleTransform(&device->IDCompositionDevice_iface, transform);
}

static HRESULT STDMETHODCALLTYPE desktop_device_CreateRotateTransform(IDCompositionDeviceUnknown *iface,
        IDCompositionRotateTransform **transform)
{
    struct composition_device *device = impl_from_IDCompositionDeviceUnknown( iface );
    return device_CreateRotateTransform(&device->IDCompositionDevice_iface, transform);
}

static HRESULT STDMETHODCALLTYPE desktop_device_CreateSkewTransform(IDCompositionDeviceUnknown *iface,
        IDCompositionSkewTransform **transform)
{
    struct composition_device *device = impl_from_IDCompositionDeviceUnknown( iface );
    return device_CreateSkewTransform(&device->IDCompositionDevice_iface, transform);
}

static HRESULT STDMETHODCALLTYPE desktop_device_CreateMatrixTransform(IDCompositionDeviceUnknown *iface,
        IDCompositionMatrixTransform **transform)
{
    struct composition_device *device = impl_from_IDCompositionDeviceUnknown( iface );
    return device_CreateMatrixTransform(&device->IDCompositionDevice_iface, transform);
}

static HRESULT STDMETHODCALLTYPE desktop_device_CreateTransformGroup(IDCompositionDeviceUnknown *iface,
        IDCompositionTransform **transforms, UINT elements, IDCompositionTransform **transform_group)
{
    struct composition_device *device = impl_from_IDCompositionDeviceUnknown( iface );
    return device_CreateTransformGroup(&device->IDCompositionDevice_iface, transforms, elements, transform_group);
}

static HRESULT STDMETHODCALLTYPE desktop_device_CreateTranslateTransform3D(IDCompositionDeviceUnknown *iface,
        IDCompositionTranslateTransform3D **transform_3d)
{
    struct composition_device *device = impl_from_IDCompositionDeviceUnknown( iface );
    return device_CreateTranslateTransform3D(&device->IDCompositionDevice_iface, transform_3d);
}

static HRESULT STDMETHODCALLTYPE desktop_device_CreateScaleTransform3D(IDCompositionDeviceUnknown *iface,
        IDCompositionScaleTransform3D **transform_3d)
{
    struct composition_device *device = impl_from_IDCompositionDeviceUnknown( iface );
    return device_CreateScaleTransform3D(&device->IDCompositionDevice_iface, transform_3d);
}

static HRESULT STDMETHODCALLTYPE desktop_device_CreateRotateTransform3D(IDCompositionDeviceUnknown *iface,
        IDCompositionRotateTransform3D **transform_3d)
{
    struct composition_device *device = impl_from_IDCompositionDeviceUnknown( iface );
    return device_CreateRotateTransform3D(&device->IDCompositionDevice_iface, transform_3d);
}

static HRESULT STDMETHODCALLTYPE desktop_device_CreateMatrixTransform3D(IDCompositionDeviceUnknown *iface,
        IDCompositionMatrixTransform3D **transform_3d)
{
    struct composition_device *device = impl_from_IDCompositionDeviceUnknown( iface );
    return device_CreateMatrixTransform3D(&device->IDCompositionDevice_iface, transform_3d);
}

static HRESULT STDMETHODCALLTYPE desktop_device_CreateTransform3DGroup(IDCompositionDeviceUnknown *iface,
        IDCompositionTransform3D **transforms_3d, UINT elements,
        IDCompositionTransform3D **transform_3d_group)
{
    struct composition_device *device = impl_from_IDCompositionDeviceUnknown( iface );
    return device_CreateTransform3DGroup(&device->IDCompositionDevice_iface, transforms_3d, elements, transform_3d_group);
}

static HRESULT STDMETHODCALLTYPE desktop_device_CreateEffectGroup(IDCompositionDeviceUnknown *iface,
        IDCompositionEffectGroup **effect_group)
{
    struct composition_device *device = impl_from_IDCompositionDeviceUnknown( iface );
    return device_CreateEffectGroup(&device->IDCompositionDevice_iface, effect_group);
}

static HRESULT STDMETHODCALLTYPE desktop_device_CreateRectangleClip(IDCompositionDeviceUnknown *iface,
        IDCompositionRectangleClip **clip)
{
    struct composition_device *device = impl_from_IDCompositionDeviceUnknown( iface );
    return device_CreateRectangleClip(&device->IDCompositionDevice_iface, clip);
}

static HRESULT STDMETHODCALLTYPE desktop_device_CreateAnimation(IDCompositionDeviceUnknown *iface,
        IDCompositionAnimation **animation)
{
    FIXME("iface %p, animation %p stub!\n", iface, animation);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_CreateTargetForHwnd(IDCompositionDeviceUnknown *iface,
        HWND hwnd, BOOL topmost, IDCompositionTarget **target)
{
    struct composition_device *device = impl_from_IDCompositionDeviceUnknown(iface);

    TRACE("DCOMP: desktop_device_CreateTargetForHwnd iface %p, hwnd %p, topmost %d, target %p\n", iface, hwnd, topmost, target);

    return IDCompositionDevice_CreateTargetForHwnd(&device->IDCompositionDevice_iface, hwnd,
            topmost, target);
}

static HRESULT STDMETHODCALLTYPE desktop_device_CreateSurfaceFromHandle(IDCompositionDeviceUnknown *iface,
        HANDLE handle, IUnknown **surface)
{
    struct composition_device *device = impl_from_IDCompositionDeviceUnknown(iface);
    return device_CreateSurfaceFromHandle(&device->IDCompositionDevice_iface, handle, surface);
}

static HRESULT STDMETHODCALLTYPE desktop_device_CreateSurfaceFromHwnd(IDCompositionDeviceUnknown *iface,
        HWND hwnd, IUnknown **surface)
{
    struct composition_device *device = impl_from_IDCompositionDeviceUnknown(iface);
    IDCompositionSurface *dcomp_surface = NULL;
    RECT rect;
    UINT width, height;
    HRESULT hr;

    FIXME("iface %p, hwnd %p, surface %p semi-stub!\n", iface, hwnd, surface);

    if (!surface)
        return E_POINTER;

    *surface = NULL;

    if (!hwnd || !IsWindow(hwnd))
        return E_INVALIDARG;

    GetClientRect(hwnd, &rect);
    width = rect.right > 0 ? rect.right : 100;
    height = rect.bottom > 0 ? rect.bottom : 100;

    hr = composition_device_create_surface(device, width, height,
            DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_ALPHA_MODE_PREMULTIPLIED, &dcomp_surface);
    if (SUCCEEDED(hr))
    {
        struct composition_surface *surf_impl = unsafe_impl_from_IDCompositionSurface(dcomp_surface);
        if (surf_impl)
            surf_impl->source_hwnd = hwnd;
        dcomp_hide_captured_hwnd(hwnd);
        *surface = (IUnknown *)dcomp_surface;
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE desktop_device_CreateSharedResource(IDCompositionDeviceUnknown *iface,
        REFIID riid, void **obj)
{
    TRACE("iface %p, riid %s, obj %p.\n", iface, debugstr_guid(riid), obj);
    if (!obj) return E_POINTER;
    *obj = NULL;

    if (IsEqualGUID(riid, &IID_IDCompositionVisual) || IsEqualGUID(riid, &IID_IDCompositionVisual2))
    {
        struct composition_visual *visual_impl;
        HRESULT hr = create_visual(2, riid, obj);
        if (SUCCEEDED(hr) && *obj)
        {
            visual_impl = impl_from_IDCompositionVisual(*(IDCompositionVisual **)obj);
            DCompositionCreateSharedVisualHandle(&visual_impl->shared_visual_handle);
            TRACE("DCOMP: Created shared visual %p, handle %p\n", *obj, visual_impl->shared_visual_handle);
        }
        return hr;
    }

    FIXME("Unsupported riid %s\n", debugstr_guid(riid));
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_OpenSharedResourceHandle(IDCompositionDeviceUnknown *iface,
        IUnknown *unknown, HANDLE *handle)
{
    IDCompositionVisual *visual = NULL;

    TRACE("iface %p, unknown %p, handle %p.\n", iface, unknown, handle);
    if (!handle) return E_POINTER;
    *handle = NULL;
    if (!unknown) return E_INVALIDARG;

    if (SUCCEEDED(IUnknown_QueryInterface(unknown, &IID_IDCompositionVisual, (void **)&visual)))
    {
        struct composition_visual *visual_impl = impl_from_IDCompositionVisual(visual);
        if (!visual_impl->shared_visual_handle)
            DCompositionCreateSharedVisualHandle(&visual_impl->shared_visual_handle);

        *handle = visual_impl->shared_visual_handle;
        IDCompositionVisual_Release(visual);
        TRACE("DCOMP: Opened shared resource handle %p for visual %p\n", *handle, unknown);
        return S_OK;
    }

    FIXME("Unsupported unknown %p\n", unknown);
    return E_NOTIMPL;
}

HRESULT dcomp_desktop_device_create_shared_resource(IDCompositionDeviceUnknown *iface,
        REFIID riid, void **obj)
{
    return desktop_device_CreateSharedResource(iface, riid, obj);
}

HRESULT dcomp_desktop_device_open_shared_resource_handle(IDCompositionDeviceUnknown *iface,
        IUnknown *unknown, HANDLE *handle)
{
    return desktop_device_OpenSharedResourceHandle(iface, unknown, handle);
}

static HRESULT STDMETHODCALLTYPE desktop_device_CreateFromSharedVisualHandle(IDCompositionDeviceUnknown *iface,
     HANDLE visual_handle, REFIID iid, void **out)
{
    void *caller = __builtin_return_address(0);
    HRESULT hr;

    TRACE("iface %p, visual_handle %p, iid %s, out %p, caller %p\n",
        iface, visual_handle, debugstr_guid(iid), out, caller);

    if (!out)
        return E_POINTER;
    *out = NULL;

    if (!visual_handle)
        return E_INVALIDARG;

    if (IsEqualGUID(iid, &IID_IDCompositionVisual))
    {
        hr = create_visual_from_shared_visual_handle(visual_handle, out);
        if (SUCCEEDED(hr) && *out)
        {
            struct composition_device *device = impl_from_IDCompositionDeviceUnknown(iface);
            ensure_composite_thread(device);
        }
        return hr;
    }
    else if (IsEqualGUID(iid, &IID_IDCompositionTarget))
    {
        struct composition_device *device = impl_from_IDCompositionDeviceUnknown(iface);
        hr = create_target_from_shared_visual_handle(device, visual_handle, out);
        if (SUCCEEDED(hr) && *out)
        {
            ensure_composite_thread(device);
        }
        return hr;
    }
    else
    {
        FIXME("Unsupported GUID %s, returning E_NOTIMPL.\n", debugstr_guid(iid));
        return E_NOTIMPL;
    }
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot31(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot32(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot33(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot34(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot35(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot36(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot37(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot38(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot39(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot40(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot41(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot42(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot43(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot44(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot45(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

LONG g_batch_id_counter = 10;

static HRESULT STDMETHODCALLTYPE desktop_device_AcquireBatchId(IDCompositionDeviceUnknown *iface, void **parameter1)
{
    TRACE("iface %p parameter1 %p.\n", iface, parameter1);

    if (!parameter1)
        return E_INVALIDARG;

    *(UINT64 *)parameter1 = (UINT64)(DWORD)InterlockedIncrement(&g_batch_id_counter);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE desktop_device_AcquireCommitBatchId(IDCompositionDeviceUnknown *iface, void **parameter1)
{
    void *ret = __builtin_return_address(0);
    TRACE("DCOMP: desktop_device_AcquireCommitBatchId iface %p parameter1 %p caller %p\n", iface, parameter1, ret);

    if (!parameter1)
        return E_INVALIDARG;

    *(UINT64 *)parameter1 = (UINT64)(DWORD)InterlockedIncrement(&g_batch_id_counter);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot48(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot49(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot50(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot51(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot52(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot53(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot54(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot55(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot56(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot57(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot58(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot59(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot60(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot61(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot62(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot63(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot64(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot65(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot66(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot67(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot68(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot69(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot70(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot71(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot72(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot73(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot74(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot75(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot76(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot77(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot78(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot79(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot80(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot81(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot82(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot83(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot84(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot85(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot86(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot87(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot88(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot89(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot90(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot91(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot92(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot93(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot94(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot95(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot96(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot97(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot98(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot99(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot100(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot101(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot102(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot103(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot104(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot105(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot106(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot107(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot108(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot109(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot110(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot111(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot112(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot113(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot114(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot115(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot116(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot117(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot118(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot119(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot120(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot121(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot122(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot123(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot124(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot125(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot126(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot127(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot128(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_reserved_slot129(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

/*
 * Every method of IDCompositionDevice2 (Commit, WaitForCommitCompletion,
 * GetFrameStatistics, CreateVisual, CreateSurfaceFactory, CreateSurface,
 * CreateVirtualSurface, the transforms, CreateEffectGroup, CreateRectangleClip), of
 * IDCompositionDesktopDevice (CreateTargetForHwnd, CreateSurfaceFromHandle,
 * CreateSurfaceFromHwnd) and of IDCompositionDesktopDevicePartner for visuals
 * (CreateSharedResource, OpenSharedResourceHandle, CreateFromSharedVisualHandle) is
 * implemented.  The 99 IDCompositionDeviceUnknown slots stay E_NOTIMPL: the
 * interface is unnamed, its methods are numbered, and their parameters are unknown,
 * so no target-specific behaviour can be attached to them.
 *
 * QueryInterface answers IDCompositionDevice3 with this object, so the v3 layout
 * shares this vtable.  The 13 methods IDCompositionDevice3 adds
 * (CreateGaussianBlurEffect ... CreateAffineTransform2DEffect) sit at slots 25..37,
 * which is exactly where IDCompositionDesktopDevice's three methods and the partner
 * interface's three methods sit.  One object cannot serve both layouts; the desktop
 * layout wins because it is the one clients of this fork use (Chromium queries
 * IDCompositionDevice3 and then calls only IDCompositionDevice2 slots, which are
 * slots 3..24 and are identical in both layouts), so the device effect creation
 * methods must not be called through an IDCompositionDevice3 pointer.
 */
static const struct IDCompositionDeviceUnknownVtbl desktop_device_vtbl =
{
    /* IUnknown methods */
    desktop_device_QueryInterface,
    desktop_device_AddRef,
    desktop_device_Release,
    /* IDCompositionDevice2 methods */
    desktop_device_Commit,
    desktop_device_WaitForCommitCompletion,
    desktop_device_GetFrameStatistics,
    desktop_device_CreateVisual,
    desktop_device_CreateSurfaceFactory,
    desktop_device_CreateSurface,
    desktop_device_CreateVirtualSurface,
    desktop_device_CreateTranslateTransform,
    desktop_device_CreateScaleTransform,
    desktop_device_CreateRotateTransform,
    desktop_device_CreateSkewTransform,
    desktop_device_CreateMatrixTransform,
    desktop_device_CreateTransformGroup,
    desktop_device_CreateTranslateTransform3D,
    desktop_device_CreateScaleTransform3D,
    desktop_device_CreateRotateTransform3D,
    desktop_device_CreateMatrixTransform3D,
    desktop_device_CreateTransform3DGroup,
    desktop_device_CreateEffectGroup,
    desktop_device_CreateRectangleClip,
    desktop_device_CreateAnimation,
    /* IDCompositionDesktopDevice methods */
    desktop_device_CreateTargetForHwnd,
    desktop_device_CreateSurfaceFromHandle,
    desktop_device_CreateSurfaceFromHwnd,
    /* IDCompositionDesktopDevicePartner methods */
    desktop_device_CreateSharedResource,
    desktop_device_OpenSharedResourceHandle,
    desktop_device_CreateFromSharedVisualHandle,
    /* IDCompositionDeviceUnknown methods */
    desktop_device_reserved_slot31,
    desktop_device_reserved_slot32,
    desktop_device_reserved_slot33,
    desktop_device_reserved_slot34,
    desktop_device_reserved_slot35,
    desktop_device_reserved_slot36,
    desktop_device_reserved_slot37,
    desktop_device_reserved_slot38,
    desktop_device_reserved_slot39,
    desktop_device_reserved_slot40,
    desktop_device_reserved_slot41,
    desktop_device_reserved_slot42,
    desktop_device_reserved_slot43,
    desktop_device_reserved_slot44,
    desktop_device_reserved_slot45,
    desktop_device_AcquireBatchId,
    desktop_device_AcquireCommitBatchId,
    desktop_device_reserved_slot48,
    desktop_device_reserved_slot49,
    desktop_device_reserved_slot50,
    desktop_device_reserved_slot51,
    desktop_device_reserved_slot52,
    desktop_device_reserved_slot53,
    desktop_device_reserved_slot54,
    desktop_device_reserved_slot55,
    desktop_device_reserved_slot56,
    desktop_device_reserved_slot57,
    desktop_device_reserved_slot58,
    desktop_device_reserved_slot59,
    desktop_device_reserved_slot60,
    desktop_device_reserved_slot61,
    desktop_device_reserved_slot62,
    desktop_device_reserved_slot63,
    desktop_device_reserved_slot64,
    desktop_device_reserved_slot65,
    desktop_device_reserved_slot66,
    desktop_device_reserved_slot67,
    desktop_device_reserved_slot68,
    desktop_device_reserved_slot69,
    desktop_device_reserved_slot70,
    desktop_device_reserved_slot71,
    desktop_device_reserved_slot72,
    desktop_device_reserved_slot73,
    desktop_device_reserved_slot74,
    desktop_device_reserved_slot75,
    desktop_device_reserved_slot76,
    desktop_device_reserved_slot77,
    desktop_device_reserved_slot78,
    desktop_device_reserved_slot79,
    desktop_device_reserved_slot80,
    desktop_device_reserved_slot81,
    desktop_device_reserved_slot82,
    desktop_device_reserved_slot83,
    desktop_device_reserved_slot84,
    desktop_device_reserved_slot85,
    desktop_device_reserved_slot86,
    desktop_device_reserved_slot87,
    desktop_device_reserved_slot88,
    desktop_device_reserved_slot89,
    desktop_device_reserved_slot90,
    desktop_device_reserved_slot91,
    desktop_device_reserved_slot92,
    desktop_device_reserved_slot93,
    desktop_device_reserved_slot94,
    desktop_device_reserved_slot95,
    desktop_device_reserved_slot96,
    desktop_device_reserved_slot97,
    desktop_device_reserved_slot98,
    desktop_device_reserved_slot99,
    desktop_device_reserved_slot100,
    desktop_device_reserved_slot101,
    desktop_device_reserved_slot102,
    desktop_device_reserved_slot103,
    desktop_device_reserved_slot104,
    desktop_device_reserved_slot105,
    desktop_device_reserved_slot106,
    desktop_device_reserved_slot107,
    desktop_device_reserved_slot108,
    desktop_device_reserved_slot109,
    desktop_device_reserved_slot110,
    desktop_device_reserved_slot111,
    desktop_device_reserved_slot112,
    desktop_device_reserved_slot113,
    desktop_device_reserved_slot114,
    desktop_device_reserved_slot115,
    desktop_device_reserved_slot116,
    desktop_device_reserved_slot117,
    desktop_device_reserved_slot118,
    desktop_device_reserved_slot119,
    desktop_device_reserved_slot120,
    desktop_device_reserved_slot121,
    desktop_device_reserved_slot122,
    desktop_device_reserved_slot123,
    desktop_device_reserved_slot124,
    desktop_device_reserved_slot125,
    desktop_device_reserved_slot126,
    desktop_device_reserved_slot127,
    desktop_device_reserved_slot128,
    desktop_device_reserved_slot129,
};

static HRESULT create_device(int version, IUnknown *rendering_device, REFIID iid, void **out)
{
    struct composition_device *device;
    HRESULT hr;

    if (!out)
        return E_INVALIDARG;

    device = calloc(1, sizeof(*device));
    if (!device)
        return E_OUTOFMEMORY;

    list_init(&device->targets);
    device->IDCompositionDevice_iface.lpVtbl = &device_vtbl;
    device->IDCompositionDeviceUnknown_iface.lpVtbl = &desktop_device_vtbl;
    device->version = version;
    device->ref = 1;
    QueryPerformanceFrequency(&device->qpc_freq);
    device->refresh_period_ms = 16;
    device->wake_event = CreateEventW(NULL, FALSE, FALSE, NULL);
    dcomp_lock();
    list_add_tail(&dcomp_devices, &device->dev_entry);
    dcomp_unlock();

    if (rendering_device)
    {
        device->rendering_device = rendering_device;
        IUnknown_AddRef(rendering_device);
    }

    hr = IDCompositionDevice_QueryInterface(&device->IDCompositionDevice_iface, iid, out);
    IDCompositionDevice_Release(&device->IDCompositionDevice_iface);
    return hr;
}

/*
 * The device returned here answers every device version through QueryInterface
 * (IDCompositionDevice, IDCompositionDevice2, IDCompositionDevice3,
 * IDCompositionDesktopDevice and the desktop partner interfaces) even when it was
 * created through the v1 entry point, and it is built with the version of the
 * newest device interface.  That is deliberate: Chromium's
 * ui/gl/direct_composition_support.cc InitializeDirectComposition() calls
 * DCompositionCreateDevice() and then immediately queries the returned device for
 * IDCompositionDevice3, so a v1-only device fails the GPU process' composition
 * setup.  The v1 entry point still only accepts IID_IDCompositionDevice as its
 * 'iid' argument, matching Windows.
 */
HRESULT WINAPI DCompositionCreateDevice(IDXGIDevice *dxgi_device, REFIID iid, void **device)
{
    TRACE("%p, %s, %p\n", dxgi_device, debugstr_guid(iid), device);

    if (!IsEqualIID(iid, &IID_IDCompositionDevice))
        return E_NOINTERFACE;

    return create_device(3, (IUnknown *)dxgi_device, iid, device);
}

HRESULT WINAPI DCompositionCreateDevice2(IUnknown *rendering_device, REFIID iid, void **device)
{
    TRACE("DCOMP: DCompositionCreateDevice2 rendering_device %p, iid %s, device %p\n", rendering_device, debugstr_guid(iid), device);

    if (!IsEqualIID(iid, &IID_IUnknown)
            && !IsEqualIID(iid, &IID_IDCompositionDevice)
            && !IsEqualIID(iid, &IID_IDCompositionDevice2)
            && !IsEqualIID(iid, &IID_IDCompositionDevice3)
            && !IsEqualIID(iid, &IID_IDCompositionDesktopDevice)
            && !is_device_version_guid(iid)
            && !is_partner_guid(iid))
        return E_NOINTERFACE;

    return create_device(2, rendering_device, iid, device);
}

HRESULT WINAPI DCompositionCreateDevice3(IUnknown *rendering_device, REFIID iid, void **device)
{
    TRACE("DCOMP: DCompositionCreateDevice3 rendering_device %p, iid %s, device %p\n", rendering_device, debugstr_guid(iid), device);

    if (!IsEqualIID(iid, &IID_IUnknown)
            && !IsEqualIID(iid, &IID_IDCompositionDevice)
            && !IsEqualIID(iid, &IID_IDCompositionDevice2)
            && !IsEqualIID(iid, &IID_IDCompositionDevice3)
            && !IsEqualIID(iid, &IID_IDCompositionDesktopDevice)
            && !is_device_version_guid(iid)
            && !is_partner_guid(iid))
        return E_NOINTERFACE;

    return create_device(3, rendering_device, iid, device);
}

DWORD WINAPI DCompositionWaitForCompositorClock(UINT count, const HANDLE *handles, DWORD timeout)
{
    LARGE_INTEGER counter, freq;
    DWORD sleep_ms = 16;
    UINT i;

    TRACE("(%u, %p, %lu)\n", count, handles, timeout);
    if (count && handles)
    {
        DWORD ret;
        /* Register the caller's park handles (duplicated into the 16-slot registry;
         * oldest slot is closed and dropped on overflow) so the composite thread's
         * present tick wakes them without holding raw caller handle values. */
        dcomp_lock();
        for (i = 0; i < count; i++)
        {
            LONG k;
            HANDLE dup = NULL;

            if (!handles[i]) continue;
            for (k = 0; k < g_compositor_clock_handle_count; k++)
                if (dcomp_compare_handles(g_compositor_clock_handles[k], handles[i])) break;
            if (k < g_compositor_clock_handle_count) continue;

            if (!DuplicateHandle(GetCurrentProcess(), handles[i], GetCurrentProcess(), &dup,
                    0, FALSE, DUPLICATE_SAME_ACCESS))
                continue;

            if (g_compositor_clock_handle_count == COMPOSITOR_CLOCK_MAX_HANDLES)
            {
                CloseHandle(g_compositor_clock_handles[0]);
                memmove(&g_compositor_clock_handles[0], &g_compositor_clock_handles[1],
                        (COMPOSITOR_CLOCK_MAX_HANDLES - 1) * sizeof(g_compositor_clock_handles[0]));
                g_compositor_clock_handle_count--;
            }
            g_compositor_clock_handles[g_compositor_clock_handle_count++] = dup;
        }
        dcomp_unlock();
        ret = WaitForMultipleObjects(count, handles, FALSE, timeout < 16 ? timeout : 16);
        if (ret != WAIT_TIMEOUT) return ret;
    }
    if (g_commit_completion_event)
        SetEvent(g_commit_completion_event);

    if (QueryPerformanceCounter( &counter ) && QueryPerformanceFrequency( &freq ) && freq.QuadPart)
    {
        LONGLONG ticks_per_frame = freq.QuadPart / 60;
        if (ticks_per_frame > 0)
        {
            LONGLONG next_frame = ((counter.QuadPart / ticks_per_frame) + 1) * ticks_per_frame;
            LONGLONG delta = next_frame - counter.QuadPart;
            DWORD ms = (DWORD)((delta * 1000) / freq.QuadPart);
            if (ms > 0 && ms <= 16) sleep_ms = ms;
        }
    }

    sleep_ms = timeout < sleep_ms ? timeout : sleep_ms;
    if (sleep_ms > 0)
    {
        ULONG seq;
        EnterCriticalSection( &g_compositor_clock_cs );
        seq = g_compositor_clock_seq;
        while (g_compositor_clock_seq == seq)
        {
            if (!SleepConditionVariableCS( &g_compositor_clock_cv, &g_compositor_clock_cs, sleep_ms ))
                break;
        }
        LeaveCriticalSection( &g_compositor_clock_cs );
    }
    return WAIT_OBJECT_0 + count;
}

static ULONGLONG get_emulated_compositor_frame_id(void)
{
    LARGE_INTEGER counter, freq;
    ULONGLONG seconds, rem;

    QueryPerformanceCounter( &counter );
    QueryPerformanceFrequency( &freq );
    seconds = counter.QuadPart / freq.QuadPart;
    rem = counter.QuadPart % freq.QuadPart;
    return seconds * 60 + (rem * 60) / freq.QuadPart;
}

HRESULT WINAPI DCompositionGetFrameId( ULONGLONG *frame_id )
{
    TRACE( "(%p)\n", frame_id );

    if (!frame_id)
        return E_INVALIDARG;

    *frame_id = get_emulated_compositor_frame_id();
    return S_OK;
}

HRESULT WINAPI DCompositionGetStatistics( ULONGLONG *frame_id, LARGE_INTEGER *update_time )
{
    LARGE_INTEGER now;

    TRACE( "(%p, %p)\n", frame_id, update_time );

    if (!frame_id || !update_time)
        return E_INVALIDARG;

    *frame_id = get_emulated_compositor_frame_id();
    QueryPerformanceCounter( &now );
    update_time->QuadPart = now.QuadPart;
    return S_OK;
}

/*
 * DCompositionBoostCompositorClock() asks the window compositor to raise the update cadence
 * of its clock, i.e. to drive the visual tree faster while a boosted frame is pending. It is
 * a request that reports failure only through its HRESULT, and returns no compositor state.
 * The Chromium/WebView2 compositor code resolves this name through GetProcAddress(), so the
 * export has to be present for that lookup to succeed.
 *
 * Our emulated compositor clock runs at a fixed 60Hz cadence (see
 * get_emulated_compositor_frame_id()) and keeps no boost state, so there is nothing to raise:
 * the request is acknowledged and otherwise ignored.
 */
HRESULT WINAPI DCompositionBoostCompositorClock( BOOL boost )
{
    TRACE( "(%d)\n", boost );

    return S_OK;
}

void WINAPI wine_dcomp_swapchain_presented( void *swapchain )
{
    HANDLE event = g_swapchain_present_event;

    if (event)
        SetEvent( event );
}

LONG WINAPI wine_dcomp_get_composite_count( IDCompositionDevice *iface )
{
    struct composition_device *device;
    IDCompositionDevice *dcomp_dev = NULL;
    LONG count;

    if (!iface)
        return 0;

    if (SUCCEEDED(IUnknown_QueryInterface( (IUnknown *)iface, &IID_IDCompositionDevice, (void **)&dcomp_dev )))
    {
        device = impl_from_IDCompositionDevice( dcomp_dev );
        count = InterlockedCompareExchange( &device->composite_count, 0, 0 );
        IDCompositionDevice_Release( dcomp_dev );
        return count;
    }

    device = impl_from_IDCompositionDevice( iface );
    return InterlockedCompareExchange( &device->composite_count, 0, 0 );
}
