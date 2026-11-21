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
#include "d3dcompiler.h"
#include <ddk/d3dkmthk.h>
#include "ntgdi.h"
#include "wine/server.h"
#include "wine/wined3d.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(dcomp);

/* Exported by dxgi (dxgi.spec): the wined3d swapchain behind a DXGI
 * swapchain, for direct-to-display composition. */
struct wined3d_swapchain *dxgi_get_wined3d_swapchain(struct IDXGISwapChain *swapchain);
HRESULT WINAPI dxgi_bind_external_memory(IUnknown *obj, HANDLE nt_handle, UINT64 alloc_size);

/* D3DKMTWaitForVerticalBlankEvent (d3d11.spec, exported by d3d11.dll; the
 * winex11 build is a stub that returns immediately) and the win32u syscall it
 * forwards to on real hardware. Resolved at runtime: the spec stubs are not
 * present in the import libraries. The caller measures the wait to tell a
 * real vertical-blank signal from the stub's immediate return. */
typedef struct
{
    D3DKMT_HANDLE hDevice;
} D3DKMT_WAITFORVERTICALBLANKEVENT;

typedef NTSTATUS (WINAPI *pfn_wait_vblank)( const D3DKMT_WAITFORVERTICALBLANKEVENT *params );

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

static CRITICAL_SECTION composite_render_cs;
static CRITICAL_SECTION_DEBUG composite_render_debug =
{
    0, 0, &composite_render_cs,
    { &composite_render_debug.ProcessLocksList, &composite_render_debug.ProcessLocksList },
      0, 0, { (DWORD_PTR)(__FILE__ ": composite_render_cs") }
};
static CRITICAL_SECTION composite_render_cs = { &composite_render_debug, -1, 0, 0, 0, 0 };

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
static inline BOOL compare_handles(HANDLE h1, HANDLE h2)
{
    if (h1 == h2) return TRUE;
    if (!h1 || !h2) return FALSE;
    return NtCompareObjects(h1, h2) == STATUS_SUCCESS;
}

HRESULT WINAPI DCompositionCreateSharedVisualHandle(HANDLE *handle);

static HANDLE g_commit_completion_event = NULL;

/*
 * Emulated compositor present clock. The composite thread (composite_thread_proc)
 * is the process's vertical-blank cadence: under the X11 backend a Direct3D present
 * produces no D3DKMT vertical-blank event (winex11 has no vblank signalling), so the
 * per-frame Sleep() in that loop is the frame tick. It broadcasts this manual-reset
 * event once per present so the compositor-clock waiters (DCompositionWaitForCompositorClock
 * callers: the CoreMessaging Composition Token / Lifted DMIT / DWM Master Input workers)
 * advance in lockstep with the present cadence, not a blind sleep. Created lazily when
 * the composite thread is first started (first device creation), so the idle cost before
 * any device exists is zero.
 */
static HANDLE g_compositor_clock_event = NULL;

/*
 * Compositor-clock waiter registry. A caller of DCompositionWaitForCompositorClock
 * passes the handles it parks on (the CoreMessaging lifted-input workers register the
 * handle the present clock is expected to signal per frame). Each caller handle is
 * duplicated into the 16-slot registry under dcomp_cs so the composite thread never
 * touches a raw caller handle after the caller closes it. If SetEvent fails with
 * ERROR_INVALID_HANDLE the slot's duplicated handle is closed and evicted; when the
 * 16-slot cap is full, the oldest slot (index 0) is closed and dropped to make room.
 */
#define COMPOSITOR_CLOCK_MAX_HANDLES 16
static HANDLE g_compositor_clock_handles[COMPOSITOR_CLOCK_MAX_HANDLES];
static volatile LONG g_compositor_clock_handle_count;

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

static const void *dcomp_6b556968_vtbl[] =
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

static struct { const void *vtbl; } dcomp_6b556968_obj = { dcomp_6b556968_vtbl };

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

static void dcomp_reclaim_drain(void);

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

static BOOL CALLBACK find_top_level_window_proc(HWND hwnd, LPARAM lparam)
{
    HWND *out_hwnd = (HWND *)lparam;
    WCHAR class_name[256];

    if (!IsWindowVisible(hwnd) || IsIconic(hwnd))
        return TRUE;

    if (GetWindowLongW(hwnd, GWL_STYLE) & WS_CHILD)
        return TRUE;

    RealGetWindowClassW(hwnd, class_name, ARRAY_SIZE(class_name));
    if (!wcscmp(class_name, L"Progman") || !wcscmp(class_name, L"Shell_TrayWnd") ||
        !wcscmp(class_name, L"static") || !wcscmp(class_name, L"Message") ||
        !wcscmp(class_name, L"WineConsoleClass"))
        return TRUE;

    *out_hwnd = hwnd;
    return FALSE;
}

static HWND find_root_top_level_window(void)
{
    HWND hwnd = FindWindowW(L"Windows.UI.Core.CoreWindow", NULL);
    if (!hwnd) hwnd = FindWindowW(L"WinUIDesktopWin32WindowClass", NULL);
    if (!hwnd) hwnd = GetActiveWindow();
    if (!hwnd) hwnd = GetForegroundWindow();
    if (!hwnd || hwnd == GetDesktopWindow())
    {
        HWND found_hwnd = NULL;
        EnumWindows(find_top_level_window_proc, (LPARAM)&found_hwnd);
        hwnd = found_hwnd;
    }
    if (hwnd)
    {
        HWND root = GetAncestor(hwnd, GA_ROOT);
        if (root) hwnd = root;
    }
    return hwnd;
}

struct composite_vs_data
{
    D3DMATRIX transform;
};

/*
 * Constant buffer for the composite blit pixel shader: premultiplied-alpha
 * opacity applied to the sampled pixel, and the visual's rectangle clip in
 * device-pixel coordinates (pixels whose centre falls outside are discarded).
 */
struct composite_blit_data
{
    FLOAT opacity;
    FLOAT replace_popup_black;
    FLOAT pad[2];
    FLOAT clip_rect[4];         /* left, top, right, bottom */
};

static const char composite_vs_code[] =
    "cbuffer vs_data : register(b0)\n"
    "{\n"
    "    float4 m0;\n"
    "    float4 m1;\n"
    "    float4 m2;\n"
    "    float4 m3;\n"
    "};\n"
    "void main(in float4 in_position : POSITION,\n"
    "          in float2 in_texcoord : TEXCOORD,\n"
    "          out float4 position : SV_Position,\n"
    "          out float2 texcoord : TEXCOORD)\n"
    "{\n"
    "     float4 p = float4(in_position.xy, 0.0, 1.0);\n"
    "     position = p.x * m0 + p.y * m1 + p.z * m2 + p.w * m3;\n"
    "     texcoord = in_texcoord;\n"
    "}";

static const char composite_ps_code[] =
    "Texture2D src_texture : register(t0);\n"
    "SamplerState sam_linear : register(s0);\n"
    "cbuffer blit_data : register(b0)\n"
    "{\n"
    "    float opacity;\n"
    "    float replace_popup_black;\n"
    "    float2 pad;\n"
    "    float4 clip_rect; /* left, top, right, bottom in device pixels */\n"
    "};\n"
    "float4 main(float4 pos : SV_POSITION, float2 uv : TEXCOORD) : SV_TARGET\n"
    "{\n"
    "    float2 px = pos.xy - float2(0.5, 0.5);\n"
    "    if (px.x < clip_rect.x || px.y < clip_rect.y || px.x > clip_rect.z || px.y > clip_rect.w)\n"
    "        discard;\n"
    "    float4 c = src_texture.Sample(sam_linear, uv);\n"
    "    if (replace_popup_black > 0.5 && c.a > 0.99 && c.r < 0.005 && c.g < 0.005 && c.b < 0.005)\n"
    "        c = float4(0.976, 0.976, 0.976, 1.0);\n"
    "    return float4(c.rgb * opacity, c.a * opacity);\n"
    "}";

/*
 * Per-target zero-copy presentation swapchain. Its back buffer is the wined3d
 * texture that owns the target window's drawable; content is sampled into it
 * on the GPU and wined3d_swapchain_present() blits the drawable to the window
 * on the command stream thread, with no CPU staging readback. Recreated when
 * the producing device or the content size changes.
 */
static HRESULT ensure_composite_swapchain(struct composition_target *target,
        ID3D11Device *device, UINT width, UINT height)
{
    static IDXGIFactory2 *factory;
    DXGI_SWAP_CHAIN_DESC1 desc;
    HRESULT hr;

    if (width < 1)
        width = 1;
    if (height < 1)
        height = 1;

    if (target->composite_swapchain && target->composite_device == device
            && target->composite_width == width && target->composite_height == height)
        return S_OK;

    if (target->composite_context)
    {
        ID3D11DeviceContext_OMSetRenderTargets(target->composite_context, 0, NULL, NULL);
        ID3D11DeviceContext_Flush(target->composite_context);
    }
    if (device)
    {
        ID3D11DeviceContext *imm_ctx = NULL;
        ID3D11Device_GetImmediateContext(device, &imm_ctx);
        if (imm_ctx)
        {
            ID3D11DeviceContext_OMSetRenderTargets(imm_ctx, 0, NULL, NULL);
            ID3D11DeviceContext_Flush(imm_ctx);
            ID3D11DeviceContext_Release(imm_ctx);
        }
    }
    if (target->composite_rtv)
    {
        ID3D11RenderTargetView_Release(target->composite_rtv);
        target->composite_rtv = NULL;
    }
    if (target->composite_swapchain && target->composite_device == device)
    {
        hr = IDXGISwapChain1_ResizeBuffers(target->composite_swapchain, 0, width, height,
                DXGI_FORMAT_B8G8R8A8_UNORM, 0);
        if (SUCCEEDED(hr))
        {
            target->composite_width = width;
            target->composite_height = height;
            if (target->composite_hwnd && IsWindow(target->composite_hwnd))
                SetWindowPos(target->composite_hwnd, HWND_BOTTOM, 0, 0, (int)width, (int)height,
                        SWP_NOACTIVATE | SWP_SHOWWINDOW | SWP_ASYNCWINDOWPOS);
            return S_OK;
        }
    }
    if (target->composite_swapchain)
    {
        IDXGISwapChain1_Release(target->composite_swapchain);
        target->composite_swapchain = NULL;
        ID3D11Device_Release(target->composite_device);
        target->composite_device = NULL;
    }

    if (!factory && FAILED(hr = CreateDXGIFactory1(&IID_IDXGIFactory2, (void **)&factory)))
    {
        ERR("Failed to create DXGI factory, hr %#lx.\n", hr);
        return hr;
    }

    if (target->composite_hwnd && IsWindow(target->composite_hwnd))
        SetWindowPos(target->composite_hwnd, HWND_BOTTOM, 0, 0, (int)width, (int)height,
                SWP_NOACTIVATE | SWP_SHOWWINDOW | SWP_ASYNCWINDOWPOS);

    memset(&desc, 0, sizeof(desc));
    desc.Width = width;
    desc.Height = height;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferCount = 2;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    desc.Flags = 0;
    hr = IDXGIFactory2_CreateSwapChainForHwnd(factory, (IUnknown *)device,
            target->composite_hwnd ? target->composite_hwnd : target->hwnd,
            &desc, NULL, NULL, &target->composite_swapchain);
    if (FAILED(hr))
    {
        ERR("Failed to create composite swapchain, hr %#lx.\n", hr);
        return hr;
    }

    ID3D11Device_AddRef(device);
    target->composite_device = device;
    target->composite_width = width;
    target->composite_height = height;
    return S_OK;
}

static HRESULT ensure_composite_pipeline(struct composition_target *target,
        ID3D11Device1 *d3d11_device)
{
    static ID3D10Blob *vs_blob = NULL, *ps_blob = NULL;
    static const struct
    {
        struct { float x, y; } position;
        struct { float x, y; } texcoord;
    }
    quad[] =
    {
        {{-1.0f,  1.0f}, {0.0f, 0.0f}},   /* Top-Left */
        {{ 1.0f,  1.0f}, {1.0f, 0.0f}},   /* Top-Right */
        {{-1.0f, -1.0f}, {0.0f, 1.0f}},   /* Bottom-Left */
        {{ 1.0f, -1.0f}, {1.0f, 1.0f}},   /* Bottom-Right */
    };
    static const D3D11_INPUT_ELEMENT_DESC layout_desc[] =
    {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    D3D11_SUBRESOURCE_DATA resource_data;
    D3D11_BUFFER_DESC buffer_desc;
    D3D11_RASTERIZER_DESC rs_desc;
    D3D11_SAMPLER_DESC sampler_desc;
    HRESULT hr;

    if (target->composite_vs)
        return S_OK;

    if (!vs_blob)
    {
        hr = D3DCompile(composite_vs_code, sizeof(composite_vs_code) - 1, "vs", NULL, NULL, "main",
                        "vs_4_0", 0, 0, &vs_blob, NULL);
        if (FAILED(hr))
        {
            ERR("Failed to compile vertex shader, hr %#lx.\n", hr);
            return hr;
        }
        hr = D3DCompile(composite_ps_code, sizeof(composite_ps_code) - 1, "ps", NULL, NULL, "main",
                        "ps_4_0", 0, 0, &ps_blob, NULL);
        if (FAILED(hr))
        {
            ERR("Failed to compile pixel shader, hr %#lx.\n", hr);
            return hr;
        }
    }

    if (FAILED(hr = ID3D11Device1_CreateVertexShader(d3d11_device, ID3D10Blob_GetBufferPointer(vs_blob),
            ID3D10Blob_GetBufferSize(vs_blob), NULL, &target->composite_vs)))
        return hr;
    if (FAILED(hr = ID3D11Device1_CreatePixelShader(d3d11_device, ID3D10Blob_GetBufferPointer(ps_blob),
            ID3D10Blob_GetBufferSize(ps_blob), NULL, &target->composite_ps)))
        return hr;
    if (FAILED(hr = ID3D11Device1_CreateInputLayout(d3d11_device, layout_desc, ARRAY_SIZE(layout_desc),
            ID3D10Blob_GetBufferPointer(vs_blob), ID3D10Blob_GetBufferSize(vs_blob),
            &target->composite_input_layout)))
        return hr;

    memset(&buffer_desc, 0, sizeof(buffer_desc));
    buffer_desc.ByteWidth = sizeof(struct composite_vs_data);
    buffer_desc.Usage = D3D11_USAGE_DEFAULT;
    buffer_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    if (FAILED(hr = ID3D11Device1_CreateBuffer(d3d11_device, &buffer_desc, NULL, &target->composite_vs_cb)))
        return hr;

    buffer_desc.ByteWidth = sizeof(struct composite_blit_data);
    if (FAILED(hr = ID3D11Device1_CreateBuffer(d3d11_device, &buffer_desc, NULL, &target->composite_ps_cb)))
        return hr;

    buffer_desc.ByteWidth = sizeof(quad);
    buffer_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    resource_data.pSysMem = quad;
    resource_data.SysMemPitch = 0;
    resource_data.SysMemSlicePitch = 0;
    if (FAILED(hr = ID3D11Device1_CreateBuffer(d3d11_device, &buffer_desc, &resource_data, &target->composite_vb)))
        return hr;

    memset(&rs_desc, 0, sizeof(rs_desc));
    rs_desc.FillMode = D3D11_FILL_SOLID;
    rs_desc.CullMode = D3D11_CULL_NONE;
    rs_desc.DepthClipEnable = TRUE;
    if (FAILED(hr = ID3D11Device1_CreateRasterizerState(d3d11_device, &rs_desc, &target->composite_rs)))
        return hr;

    {
        D3D11_BLEND_DESC blend_desc;
        memset(&blend_desc, 0, sizeof(blend_desc));
        blend_desc.RenderTarget[0].BlendEnable = TRUE;
        blend_desc.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
        blend_desc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        blend_desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        blend_desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        blend_desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        blend_desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        blend_desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        if (FAILED(hr = ID3D11Device1_CreateBlendState(d3d11_device, &blend_desc, &target->composite_bs)))
            return hr;
    }

    memset(&sampler_desc, 0, sizeof(sampler_desc));
    sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sampler_desc.MinLOD = 0;
    sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(hr = ID3D11Device1_CreateSamplerState(d3d11_device, &sampler_desc, &target->composite_sampler)))
        return hr;

    return S_OK;
}

static ID3D11ShaderResourceView *get_or_create_visual_srv(struct composition_visual *visual,
        ID3D11Device1 *d3d11_device, ID3D11DeviceContext *d3d11_device_context,
        ID3D11Texture2D *src_texture)
{
    struct composition_surface *surf = NULL;
    ID3D11ShaderResourceView *srv = NULL;
    D3D11_TEXTURE2D_DESC src_desc;
    HRESULT hr;

    if (visual && visual->content && dcomp_is_equal_guid(&visual->content_iid, &IID_IDCompositionSurface))
    {
        surf = unsafe_impl_from_IDCompositionSurface((IDCompositionSurface *)visual->content);
        if (surf && surf->cached_srv && surf->cached_srv_device == (ID3D11Device *)d3d11_device)
        {
            ID3D11ShaderResourceView_AddRef(surf->cached_srv);
            return surf->cached_srv;
        }
    }

    ID3D11Texture2D_GetDesc(src_texture, &src_desc);
    if ((src_desc.BindFlags & D3D11_BIND_SHADER_RESOURCE) && src_desc.SampleDesc.Count <= 1)
    {
        hr = ID3D11Device1_CreateShaderResourceView(d3d11_device, (ID3D11Resource *)src_texture, NULL, &srv);
        if (SUCCEEDED(hr))
        {
            if (surf)
            {
                if (surf->cached_srv)
                    ID3D11ShaderResourceView_Release(surf->cached_srv);
                if (surf->cached_srv_device)
                    ID3D11Device_Release(surf->cached_srv_device);
                surf->cached_srv = srv;
                ID3D11ShaderResourceView_AddRef(srv);
                surf->cached_srv_device = (ID3D11Device *)d3d11_device;
                ID3D11Device_AddRef(surf->cached_srv_device);
            }
            return srv;
        }
    }

    /* Foreign swapchain/surface without D3D11_BIND_SHADER_RESOURCE (or MSAA):
     * maintain a single persistent SRV-capable texture on the visual instead of
     * allocating and destroying a staging texture or falling back to raw copy. */
    if (!visual)
        return NULL;

    if (!visual->cached_copy_tex || visual->cached_copy_device != (ID3D11Device *)d3d11_device
            || visual->cached_copy_width != src_desc.Width
            || visual->cached_copy_height != src_desc.Height
            || visual->cached_copy_format != src_desc.Format)
    {
        D3D11_TEXTURE2D_DESC copy_desc;

        if (visual->cached_copy_srv)
        {
            ID3D11ShaderResourceView_Release(visual->cached_copy_srv);
            visual->cached_copy_srv = NULL;
        }
        if (visual->cached_copy_tex)
        {
            ID3D11Texture2D_Release(visual->cached_copy_tex);
            visual->cached_copy_tex = NULL;
        }
        if (visual->cached_copy_device)
        {
            ID3D11Device_Release(visual->cached_copy_device);
            visual->cached_copy_device = NULL;
        }

        memset(&copy_desc, 0, sizeof(copy_desc));
        copy_desc.Width = src_desc.Width;
        copy_desc.Height = src_desc.Height;
        copy_desc.MipLevels = 1;
        copy_desc.ArraySize = 1;
        copy_desc.Format = src_desc.Format;
        copy_desc.SampleDesc.Count = 1;
        copy_desc.Usage = D3D11_USAGE_DEFAULT;
        copy_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        hr = ID3D11Device1_CreateTexture2D(d3d11_device, &copy_desc, NULL, &visual->cached_copy_tex);
        if (FAILED(hr))
            return NULL;

        hr = ID3D11Device1_CreateShaderResourceView(d3d11_device,
                (ID3D11Resource *)visual->cached_copy_tex, NULL, &visual->cached_copy_srv);
        if (FAILED(hr))
        {
            ID3D11Texture2D_Release(visual->cached_copy_tex);
            visual->cached_copy_tex = NULL;
            return NULL;
        }

        visual->cached_copy_device = (ID3D11Device *)d3d11_device;
        ID3D11Device_AddRef(visual->cached_copy_device);
        visual->cached_copy_width = src_desc.Width;
        visual->cached_copy_height = src_desc.Height;
        visual->cached_copy_format = src_desc.Format;
    }

    if (src_desc.SampleDesc.Count > 1)
        ID3D11DeviceContext_ResolveSubresource(d3d11_device_context,
                (ID3D11Resource *)visual->cached_copy_tex, 0,
                (ID3D11Resource *)src_texture, 0, src_desc.Format);
    else
        ID3D11DeviceContext_CopySubresourceRegion(d3d11_device_context,
                (ID3D11Resource *)visual->cached_copy_tex, 0, 0, 0, 0,
                (ID3D11Resource *)src_texture, 0, NULL);

    ID3D11ShaderResourceView_AddRef(visual->cached_copy_srv);
    return visual->cached_copy_srv;
}

/*
 * One GPU pass: sample 'src_texture' (any 32-bit format; the output merger
 * converts to the back buffer's B8G8R8A8) into the composite swapchain back
 * buffer, multiplying by the visual opacity and discarding pixels outside the
 * visual's rectangle clip. Falls back to a plain GPU CopySubresourceRegion
 * only when even a cached SRV copy texture cannot be created.
 */
static HRESULT composite_surface_to_backbuffer(struct composition_target *target,
        struct composition_visual *visual,
        ID3D11Device1 *d3d11_device, ID3D11DeviceContext *d3d11_device_context,
        ID3D11Texture2D *src_texture, UINT src_width, UINT src_height,
        UINT dst_width, UINT dst_height, const D3DMATRIX *proj_matrix,
        float opacity, const D2D_RECT_F *clip_rect, BOOL clear, POINT copy_off)
{
    static const float transparent_clear[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    static const float black_clear[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    static const float light_clear[4] = {0.953f, 0.953f, 0.953f, 1.0f};
    DWORD ex_style = target->hwnd ? GetWindowLongW(target->hwnd, GWL_EXSTYLE) : 0;
    DWORD style = target->hwnd ? GetWindowLongW(target->hwnd, GWL_STYLE) : 0;
    const float *clear_color = (ex_style & WS_EX_NOREDIRECTIONBITMAP)
            ? ((style & WS_POPUP) ? transparent_clear : light_clear) : black_clear;
    struct composite_vs_data vs_data;
    struct composite_blit_data blit_data;
    ID3D11ShaderResourceView *srv = NULL;
    ID3D11RenderTargetView *rtv = NULL;
    ID3D11Texture2D *backbuffer = NULL;
    D3D11_VIEWPORT viewport;
    unsigned int stride = sizeof(float) * 4, offset = 0;
    IDXGISwapChain1 *swapchain;
    HRESULT hr;

    swapchain = target->composite_swapchain;
    if (!swapchain)
        return E_FAIL;

    if (target->composite_rtv)
    {
        rtv = target->composite_rtv;
        ID3D11RenderTargetView_AddRef(rtv);
    }
    else
    {
        if (FAILED(hr = IDXGISwapChain1_GetBuffer(swapchain, 0, &IID_ID3D11Texture2D, (void **)&backbuffer)))
        {
            ERR("Failed to get composite back buffer, hr %#lx.\n", hr);
            return hr;
        }
        hr = ID3D11Device1_CreateRenderTargetView(d3d11_device, (ID3D11Resource *)backbuffer, NULL, &rtv);
        ID3D11Texture2D_Release(backbuffer);
        if (FAILED(hr))
        {
            ERR("Failed to create composite render target view, hr %#lx.\n", hr);
            return hr;
        }
        target->composite_rtv = rtv;
        ID3D11RenderTargetView_AddRef(rtv);
    }

    if ((srv = get_or_create_visual_srv(visual, d3d11_device, d3d11_device_context, src_texture))
            && SUCCEEDED(hr = ensure_composite_pipeline(target, d3d11_device)))
    {
        vs_data.transform = *proj_matrix;
        blit_data.opacity = opacity;
        blit_data.replace_popup_black = ((ex_style & WS_EX_NOREDIRECTIONBITMAP) && (style & WS_POPUP)) ? 1.0f : 0.0f;
        blit_data.pad[0] = blit_data.pad[1] = 0;
        blit_data.clip_rect[0] = clip_rect ? clip_rect->left : 0.0f;
        blit_data.clip_rect[1] = clip_rect ? clip_rect->top : 0.0f;
        blit_data.clip_rect[2] = clip_rect ? clip_rect->right : (FLOAT)dst_width;
        blit_data.clip_rect[3] = clip_rect ? clip_rect->bottom : (FLOAT)dst_height;

        ID3D11DeviceContext_UpdateSubresource(d3d11_device_context,
                (ID3D11Resource *)target->composite_vs_cb, 0, NULL, &vs_data, 0, 0);
        ID3D11DeviceContext_UpdateSubresource(d3d11_device_context,
                (ID3D11Resource *)target->composite_ps_cb, 0, NULL, &blit_data, 0, 0);

        ID3D11DeviceContext_OMSetRenderTargets(d3d11_device_context, 1, &rtv, NULL);
        if (clear)
            ID3D11DeviceContext_ClearRenderTargetView(d3d11_device_context, rtv, clear_color);
        if (target->composite_bs && (!visual || !visual->is_shared_proxy))
            ID3D11DeviceContext_OMSetBlendState(d3d11_device_context, target->composite_bs, NULL, 0xffffffff);
        else
            ID3D11DeviceContext_OMSetBlendState(d3d11_device_context, NULL, NULL, 0xffffffff);
        ID3D11DeviceContext_RSSetState(d3d11_device_context, target->composite_rs);
        ID3D11DeviceContext_PSSetShaderResources(d3d11_device_context, 0, 1, &srv);
        ID3D11DeviceContext_PSSetSamplers(d3d11_device_context, 0, 1, &target->composite_sampler);
        ID3D11DeviceContext_VSSetConstantBuffers(d3d11_device_context, 0, 1, &target->composite_vs_cb);
        ID3D11DeviceContext_PSSetConstantBuffers(d3d11_device_context, 0, 1, &target->composite_ps_cb);
        ID3D11DeviceContext_IASetInputLayout(d3d11_device_context, target->composite_input_layout);
        ID3D11DeviceContext_IASetPrimitiveTopology(d3d11_device_context, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        ID3D11DeviceContext_IASetVertexBuffers(d3d11_device_context, 0, 1, &target->composite_vb, &stride, &offset);
        ID3D11DeviceContext_VSSetShader(d3d11_device_context, target->composite_vs, NULL, 0);
        ID3D11DeviceContext_PSSetShader(d3d11_device_context, target->composite_ps, NULL, 0);
        viewport.TopLeftX = 0;
        viewport.TopLeftY = 0;
        viewport.Width = dst_width;
        viewport.Height = dst_height;
        viewport.MinDepth = 0;
        viewport.MaxDepth = 1;
        ID3D11DeviceContext_RSSetViewports(d3d11_device_context, 1, &viewport);
        ID3D11DeviceContext_Draw(d3d11_device_context, 4, 0);
        hr = S_OK;
        goto done;
    }

    /* Source not sampleable: one GPU copy, opacity/transform not applied. */
    hr = IDXGISwapChain1_GetBuffer(swapchain, 0, &IID_ID3D11Texture2D, (void **)&backbuffer);
    if (FAILED(hr))
    {
        ERR("Failed to get composite back buffer, hr %#lx.\n", hr);
        goto done;
    }
    {
        D3D11_BOX box = {0, 0, 0, min(src_width, dst_width), min(src_height, dst_height), 1};
        ID3D11DeviceContext_CopySubresourceRegion(d3d11_device_context, (ID3D11Resource *)backbuffer,
                0, copy_off.x, copy_off.y, 0, (ID3D11Resource *)src_texture, 0, &box);
    }
    ID3D11Texture2D_Release(backbuffer);
    hr = S_OK;

done:
    if (srv)
        ID3D11ShaderResourceView_Release(srv);
    if (rtv)
        ID3D11RenderTargetView_Release(rtv);
    return hr;
}

/*
 * Renders one frame into the window of 'target'. Must be called with dcomp_cs
 * released: it enters Direct3D and the window manager. The visual's opacity
 * (from its effect group) and rectangle clip are applied by the composite
 * pixel shader; 'pt' is the already accumulated visual offset chain, so each
 * visual of a composition tree updates only its own region of the window
 * instead of clobbering the whole drawable.
 */
static BOOL do_composite_dxgi_surface(struct composition_target *target,
                                      struct composition_visual *visual,
                                      HWND target_hwnd, POINT pt,
                                      IDXGISurface *dxgi_surface,
                                      float opacity, const D2D_RECT_F *clip_rect,
                                      BOOL has_transform, const D3DMATRIX *world_transform,
                                      BOOL *out_culled)
{
    LARGE_INTEGER qpc_start, qpc_end, qpc_freq;
    ID3DDeviceContextState *state = NULL, *old_state = NULL;
    ID3D11DeviceContext1 *d3d11_device_context1 = NULL;
    ID3D11DeviceContext *d3d11_device_context = NULL;
    ID3D11Multithread *multithread = NULL;
    IDXGIKeyedMutex *keyed_mutex = NULL;
    struct wined3d_swapchain *wined3d_swapchain;
    DXGI_SURFACE_DESC surface_desc;
    ID3D11Device1 *d3d11_device = NULL;
    ID3D11Texture2D *texture = NULL;
    HWND root_hwnd = NULL;
    POINT win_off = {0, 0};
    LONG dst_x, dst_y;
    UINT dst_w, dst_h;
    D3DMATRIX proj_matrix;
    D2D_RECT_F adjusted_clip;
    const D2D_RECT_F *effective_clip = NULL;
    BOOL use_proj_path;
    BOOL ok = FALSE;
    HRESULT hr;
    /* Single-pass mode: the back buffer covers the whole target window and
     * composite_target_end() presents it once for all visuals of this tick. */
    BOOL batch = target->composite_batch_active;
    UINT buf_w, buf_h;
    RECT root_client = {0, 0, 0, 0};

    static const D3D_FEATURE_LEVEL feature_levels[] =
    {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };

    if (out_culled)
        *out_culled = FALSE;

    QueryPerformanceCounter(&qpc_start);

    hr = IDXGISurface_GetDevice(dxgi_surface, &IID_ID3D11Device1, (void **)&d3d11_device);
    if (FAILED(hr))
    {
        ERR("Failed to get device, hr %#lx.\n", hr);
        goto done;
    }

    ID3D11Device1_GetImmediateContext(d3d11_device, &d3d11_device_context);
    if (FAILED(hr = ID3D11DeviceContext_QueryInterface(d3d11_device_context, &IID_ID3D11DeviceContext1,
                                                       (void **)&d3d11_device_context1)))
    {
        ERR("Failed to query ID3D11DeviceContext1, hr %#lx.\n", hr);
        goto done;
    }

    /* Use ID3D11Multithread to avoid conflicts with rendering in an another thread with the same
     * immediate ID3D11DeviceContext */
    hr = ID3D11DeviceContext_QueryInterface(d3d11_device_context, &IID_ID3D11Multithread, (void **)&multithread);
    if (FAILED(hr))
    {
        ERR("Failed to query ID3D11Multithread, hr %#lx.\n", hr);
        goto done;
    }

    if (SUCCEEDED(IDXGISurface_QueryInterface(dxgi_surface, &IID_IDXGIKeyedMutex, (void **)&keyed_mutex)))
    {
        hr = IDXGIKeyedMutex_AcquireSync(keyed_mutex, 0, 0);
        if (hr == (HRESULT)WAIT_TIMEOUT)
            hr = IDXGIKeyedMutex_AcquireSync(keyed_mutex, 1, 0);
        if (hr == (HRESULT)WAIT_TIMEOUT)
        {
            /* GPU producer is still rendering into the surface; skip composite tick without stalling CPU */
            IDXGIKeyedMutex_Release(keyed_mutex);
            keyed_mutex = NULL;
            goto done;
        }
    }

    ID3D11Multithread_SetMultithreadProtected(multithread, TRUE);
    ID3D11Multithread_Enter(multithread);

    /* Is this really necessary? anyway, just to be safe. The context state is
     * cached on the target: one per composite device, reused by every visual
     * of every frame. Creating it per visual allocates a full wined3d_state
     * object (and the driver state that goes with it) each time. */
    if (target->composite_state_device != d3d11_device)
    {
        if (target->composite_sampler)
            ID3D11SamplerState_Release(target->composite_sampler);
        if (target->composite_rs)
            ID3D11RasterizerState_Release(target->composite_rs);
        if (target->composite_vb)
            ID3D11Buffer_Release(target->composite_vb);
        if (target->composite_ps_cb)
            ID3D11Buffer_Release(target->composite_ps_cb);
        if (target->composite_vs_cb)
            ID3D11Buffer_Release(target->composite_vs_cb);
        if (target->composite_input_layout)
            ID3D11InputLayout_Release(target->composite_input_layout);
        if (target->composite_ps)
            ID3D11PixelShader_Release(target->composite_ps);
        if (target->composite_vs)
            ID3D11VertexShader_Release(target->composite_vs);
        target->composite_sampler = NULL;
        target->composite_rs = NULL;
        target->composite_vb = NULL;
        target->composite_ps_cb = NULL;
        target->composite_vs_cb = NULL;
        target->composite_input_layout = NULL;
        target->composite_ps = NULL;
        target->composite_vs = NULL;
        if (target->composite_state)
            ID3DDeviceContextState_Release(target->composite_state);
        if (target->composite_context1)
            ID3D11DeviceContext1_Release(target->composite_context1);
        if (target->composite_context)
            ID3D11DeviceContext_Release(target->composite_context);
        if (target->composite_state_device)
            ID3D11Device1_Release(target->composite_state_device);
        target->composite_state = NULL;
        target->composite_context1 = NULL;
        target->composite_context = NULL;
        ID3D11Device1_AddRef(d3d11_device);
        target->composite_state_device = d3d11_device;
    }
    if (!target->composite_state)
    {
        if (FAILED(hr = ID3D11Device1_CreateDeviceContextState(d3d11_device, 0, feature_levels,
                                                               ARRAY_SIZE(feature_levels), D3D11_SDK_VERSION,
                                                               &IID_ID3D11Device1, NULL, &target->composite_state)))
        {
            ERR("Failed to create device context state, hr %#lx.\n", hr);
            goto done;
        }
        ID3D11DeviceContext_AddRef(d3d11_device_context);
        target->composite_context = d3d11_device_context;
        ID3D11DeviceContext1_AddRef(d3d11_device_context1);
        target->composite_context1 = d3d11_device_context1;
    }
    state = target->composite_state;
    ID3DDeviceContextState_AddRef(state);
    ID3D11DeviceContext1_SwapDeviceContextState(d3d11_device_context1, state, &old_state);

    if (FAILED(hr = IDXGISurface_GetDesc(dxgi_surface, &surface_desc)))
    {
        ERR("Failed to get the dxgi surface description, hr %#lx.\n", hr);
        goto done;
    }

    if (!target_hwnd || !IsWindow(target_hwnd) || !IsWindowVisible(target_hwnd))
        goto done;

    {
        WCHAR class_name[64];
        RealGetWindowClassW(target_hwnd, class_name, ARRAY_SIZE(class_name));
        if (wcscmp(class_name, L"static") != 0)
        {
            root_hwnd = GetAncestor(target_hwnd, GA_ROOT);
            if (!root_hwnd)
                root_hwnd = target_hwnd;

            if (root_hwnd != target_hwnd)
            {
                MapWindowPoints(target_hwnd, root_hwnd, &win_off, 1);
                if (win_off.x <= -2000 || win_off.y <= -2000)
                    win_off.x = win_off.y = 0;
            }
        }
    }

    if (!root_hwnd || !IsWindow(root_hwnd))
        goto done;

    /* Batch mode: the back buffer is the size of the window client, the pixel
     * space the visual's projection matrix and clip rectangle are expressed
     * in. The per-visual (non-batch) path keeps the visual-sized back buffer. */
    if (batch)
    {
        GetClientRect(root_hwnd, &root_client);
        buf_w = root_client.right > 0 ? (UINT)root_client.right : surface_desc.Width;
        buf_h = root_client.bottom > 0 ? (UINT)root_client.bottom : surface_desc.Height;
    }
    else
    {
        buf_w = dst_w = surface_desc.Width;
        buf_h = dst_h = surface_desc.Height;
    }

    hr = IDXGISurface_QueryInterface(dxgi_surface, &IID_ID3D11Texture2D, (void **)&texture);
    if (FAILED(hr))
    {
        ERR("Failed to get a ID3D11Texture2D, hr %#lx.\n", hr);
        goto done;
    }

    use_proj_path = has_transform || clip_rect != NULL || pt.x < 0 || pt.y < 0
            || (world_transform && (world_transform->m[3][0] != (float)pt.x
                || world_transform->m[3][1] != (float)pt.y));

    if (use_proj_path)
    {
        D3DMATRIX win_shift, base_world, root_m;
        float corners_x[4] = {0.0f, (float)surface_desc.Width, 0.0f, (float)surface_desc.Width};
        float corners_y[4] = {0.0f, 0.0f, (float)surface_desc.Height, (float)surface_desc.Height};
        float min_x = 0.0f, min_y = 0.0f, max_x = 0.0f, max_y = 0.0f;
        BOOL valid_bounds = TRUE;
        RECT client_rect;
        int i;

        if (world_transform)
            base_world = *world_transform;
        else
        {
            dcomp_matrix_identity(&base_world);
            base_world.m[3][0] = (float)pt.x;
            base_world.m[3][1] = (float)pt.y;
        }

        dcomp_matrix_identity(&win_shift);
        win_shift.m[3][0] = (float)win_off.x;
        win_shift.m[3][1] = (float)win_off.y;
        dcomp_matrix_multiply(&root_m, &base_world, &win_shift);

        for (i = 0; i < 4; i++)
        {
            float cx = corners_x[i];
            float cy = corners_y[i];
            float x = cx * root_m.m[0][0] + cy * root_m.m[1][0] + root_m.m[3][0];
            float y = cx * root_m.m[0][1] + cy * root_m.m[1][1] + root_m.m[3][1];
            float w = cx * root_m.m[0][3] + cy * root_m.m[1][3] + root_m.m[3][3];
            if (w <= 1e-4f)
            {
                valid_bounds = FALSE;
                break;
            }
            x /= w;
            y /= w;
            if (i == 0 || x < min_x) min_x = x;
            if (i == 0 || y < min_y) min_y = y;
            if (i == 0 || x > max_x) max_x = x;
            if (i == 0 || y > max_y) max_y = y;
        }

        if (valid_bounds)
        {
            LONG left = (LONG)floorf(min_x);
            LONG top = (LONG)floorf(min_y);
            LONG right = (LONG)ceilf(max_x);
            LONG bottom = (LONG)ceilf(max_y);

            if (GetClientRect(root_hwnd, &client_rect) && client_rect.right > 0 && client_rect.bottom > 0)
            {
                if (left < 0) left = 0;
                if (top < 0) top = 0;
                if (right > client_rect.right) right = client_rect.right;
                if (bottom > client_rect.bottom) bottom = client_rect.bottom;
            }
            if (clip_rect)
            {
                LONG cl = (LONG)floorf(clip_rect->left + (float)win_off.x);
                LONG ct = (LONG)floorf(clip_rect->top + (float)win_off.y);
                LONG cr = (LONG)ceilf(clip_rect->right + (float)win_off.x);
                LONG cb = (LONG)ceilf(clip_rect->bottom + (float)win_off.y);
                if (left < cl) left = cl;
                if (top < ct) top = ct;
                if (right > cr) right = cr;
                if (bottom > cb) bottom = cb;
            }
            if (right <= left || bottom <= top)
            {
                if (out_culled)
                    *out_culled = TRUE;
                goto done;
            }

            dst_x = left;
            dst_y = top;
            dst_w = (UINT)(right - left);
            dst_h = (UINT)(bottom - top);
            if (dst_w > 4096) dst_w = 4096;
            if (dst_h > 4096) dst_h = 4096;
        }
        else
        {
            dst_x = pt.x + win_off.x;
            dst_y = pt.y + win_off.y;
            dst_w = surface_desc.Width;
            dst_h = surface_desc.Height;
        }

        if (clip_rect)
        {
            /* Batch: clip rectangle in window-client pixels. Per-visual: the
             * back buffer starts at the visual's region, so subtract it. */
            float off_x = (float)win_off.x - (batch ? 0.0f : (float)dst_x);
            float off_y = (float)win_off.y - (batch ? 0.0f : (float)dst_y);
            adjusted_clip.left = clip_rect->left + off_x;
            adjusted_clip.top = clip_rect->top + off_y;
            adjusted_clip.right = clip_rect->right + off_x;
            adjusted_clip.bottom = clip_rect->bottom + off_y;
            effective_clip = &adjusted_clip;
        }

        visual_get_transform_3d(visual, surface_desc.Width, surface_desc.Height, &root_m,
                batch ? 0.0f : (float)dst_x, batch ? 0.0f : (float)dst_y,
                (float)buf_w, (float)buf_h, &proj_matrix);
    }
    else
    {
        dst_x = pt.x + win_off.x;
        dst_y = pt.y + win_off.y;
        dst_w = surface_desc.Width;
        dst_h = surface_desc.Height;
        if (batch)
        {
            /* Pixel space is the whole window: bake the accumulated offset
             * into the world matrix (local transform, then shift to the
             * visual's window position) and map through the window-sized
             * viewport. */
            D3DMATRIX local_m, shift_m, world_m;
            visual_get_local_transform_3d(visual, &local_m);
            dcomp_matrix_identity(&shift_m);
            shift_m.m[3][0] = (float)dst_x;
            shift_m.m[3][1] = (float)dst_y;
            dcomp_matrix_multiply(&world_m, &local_m, &shift_m);
            visual_get_transform_3d(visual, surface_desc.Width, surface_desc.Height, &world_m,
                    0.0f, 0.0f, (float)buf_w, (float)buf_h, &proj_matrix);
        }
        else
            visual_get_transform_3d(visual, surface_desc.Width, surface_desc.Height, NULL,
                    0.0f, 0.0f, (float)dst_w, (float)dst_h, &proj_matrix);
    }

    if (batch && target->composite_batch_cleared && target->composite_device
            && target->composite_device != (ID3D11Device *)d3d11_device)
    {
        /* Cross-device visual in the same batch (e.g. XAML overlay on its own D3D11 device
         * composited over a SwapChainPanel 3D device): copy via staging into target->composite_device
         * so we preserve the already-drawn lower visuals in target->composite_swapchain. */
        D3D11_TEXTURE2D_DESC src_desc, stg_desc, dst_desc;
        ID3D11Texture2D *staging = NULL, *comp_tex = NULL;
        ID3D11Device1 *comp_dev1 = NULL;
        ID3D11DeviceContext *comp_ctx = NULL;
        D3D11_MAPPED_SUBRESOURCE mapped;

        ID3D11Texture2D_GetDesc(texture, &src_desc);
        stg_desc = src_desc;
        stg_desc.Usage = D3D11_USAGE_STAGING;
        stg_desc.BindFlags = 0;
        stg_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        stg_desc.MiscFlags = 0;
        stg_desc.MipLevels = 1;
        stg_desc.ArraySize = 1;
        stg_desc.SampleDesc.Count = 1;
        stg_desc.SampleDesc.Quality = 0;

        if (SUCCEEDED(ID3D11Device1_CreateTexture2D(d3d11_device, &stg_desc, NULL, &staging)))
        {
            ID3D11DeviceContext_CopySubresourceRegion(d3d11_device_context, (ID3D11Resource *)staging, 0,
                    0, 0, 0, (ID3D11Resource *)texture, 0, NULL);
            if (SUCCEEDED(ID3D11DeviceContext_Map(d3d11_device_context, (ID3D11Resource *)staging, 0,
                    D3D11_MAP_READ, 0, &mapped)))
            {
                D3D11_SUBRESOURCE_DATA init_data = { mapped.pData, mapped.RowPitch, mapped.DepthPitch };
                dst_desc = stg_desc;
                dst_desc.Usage = D3D11_USAGE_DEFAULT;
                dst_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                dst_desc.CPUAccessFlags = 0;
                if (SUCCEEDED(ID3D11Device_CreateTexture2D(target->composite_device, &dst_desc, &init_data, &comp_tex)))
                {
                    if (SUCCEEDED(ID3D11Device_QueryInterface(target->composite_device, &IID_ID3D11Device1, (void **)&comp_dev1)))
                    {
                        ID3D11Device_GetImmediateContext(target->composite_device, &comp_ctx);
                        hr = composite_surface_to_backbuffer(target, NULL, comp_dev1, comp_ctx,
                                comp_tex, surface_desc.Width, surface_desc.Height, buf_w, buf_h,
                                &proj_matrix, opacity, effective_clip, FALSE, (POINT){dst_x, dst_y});
                        if (comp_ctx) ID3D11DeviceContext_Release(comp_ctx);
                        ID3D11Device1_Release(comp_dev1);
                    }
                    ID3D11Texture2D_Release(comp_tex);
                }
                ID3D11DeviceContext_Unmap(d3d11_device_context, (ID3D11Resource *)staging, 0);
            }
            ID3D11Texture2D_Release(staging);
        }
        if (SUCCEEDED(hr))
        {
            RECT r;
            SetRect(&r, dst_x, dst_y, dst_x + (LONG)dst_w, dst_y + (LONG)dst_h);
            UnionRect(&target->composite_batch_rect, &target->composite_batch_rect, &r);
            target->composite_root_hwnd = root_hwnd;
            ok = TRUE;
        }
        goto done;
    }

    if (FAILED(hr = ensure_composite_swapchain(target, (ID3D11Device *)d3d11_device,
            batch ? buf_w : dst_w, batch ? buf_h : dst_h)))
    {
        ERR("Failed to ensure composite swapchain, hr %#lx.\n", hr);
        goto done;
    }

    if (FAILED(hr = composite_surface_to_backbuffer(target, visual, d3d11_device, d3d11_device_context,
            texture, surface_desc.Width, surface_desc.Height, batch ? buf_w : dst_w,
            batch ? buf_h : dst_h, &proj_matrix, opacity, effective_clip,
            !batch || !target->composite_batch_cleared,
            batch ? (POINT){dst_x, dst_y} : (POINT){0, 0})))
    {
        ERR("Failed to blit surface to back buffer, hr %#lx.\n", hr);
        goto done;
    }

    if (batch)
    {
        /* Queue this visual's quad into the shared back buffer; the flush and
         * the single present for the whole target happen in
         * composite_target_end(). Track the union of the drawn regions. */
        RECT r;

        SetRect(&r, dst_x, dst_y, dst_x + (LONG)dst_w, dst_y + (LONG)dst_h);
        TRACE("DCOMP: batch-draw dst %ld,%ld %ux%u buf %ux%u use_proj %u m=[%.3f %.3f %.3f %.3f]\n",
                dst_x, dst_y, dst_w, dst_h, buf_w, buf_h, use_proj_path,
                world_transform ? world_transform->m[0][0] : 1.0f,
                world_transform ? world_transform->m[1][1] : 1.0f,
                world_transform ? world_transform->m[3][0] : 0.0f,
                world_transform ? world_transform->m[3][1] : 0.0f);
        UnionRect(&target->composite_batch_rect, &target->composite_batch_rect, &r);
        target->composite_batch_cleared = TRUE;
        target->composite_root_hwnd = root_hwnd;
        ok = TRUE;
        goto done;
    }

    ID3D11DeviceContext_Flush(d3d11_device_context);

    /* Present the back buffer region onto the window's drawable: the wined3d
     * command stream thread blits it on the GPU. dst_rect is this visual's
     * accumulated region so child visuals do not clobber each other. */
    if ((wined3d_swapchain = dxgi_get_wined3d_swapchain((struct IDXGISwapChain *)target->composite_swapchain)))
    {
        RECT src_rect, dst_rect;

        SetRect(&src_rect, 0, 0, (LONG)dst_w, (LONG)dst_h);
        SetRect(&dst_rect, dst_x, dst_y, dst_x + (LONG)dst_w, dst_y + (LONG)dst_h);
        wined3d_swapchain_present(wined3d_swapchain, &src_rect, &dst_rect, root_hwnd, 0, 0);
        TRACE("DCOMP: presented frame %ux%u (src %ux%u) to root_hwnd %p (target hwnd %p at %ld,%ld)\n",
                dst_w, dst_h, surface_desc.Width, surface_desc.Height, root_hwnd, target_hwnd, dst_x, dst_y);
        ok = TRUE;
    }
    else
    {
        ERR("Failed to get wined3d swapchain for composite swapchain\n");
    }

done:
    if (texture)
        ID3D11Texture2D_Release(texture);
    if (d3d11_device_context1 && old_state)
    {
        ID3D11DeviceContext1_SwapDeviceContextState(d3d11_device_context1, old_state, NULL);
        ID3DDeviceContextState_Release(old_state);
    }
    if (state)
        ID3DDeviceContextState_Release(state);
    if (multithread)
    {
        ID3D11Multithread_Leave(multithread);
        ID3D11Multithread_Release(multithread);
    }
    if (d3d11_device_context1)
        ID3D11DeviceContext1_Release(d3d11_device_context1);
    if (d3d11_device_context)
        ID3D11DeviceContext_Release(d3d11_device_context);
    if (d3d11_device)
        ID3D11Device1_Release(d3d11_device);
    if (keyed_mutex)
    {
        IDXGIKeyedMutex_ReleaseSync(keyed_mutex, 0);
        IDXGIKeyedMutex_Release(keyed_mutex);
    }
    if (ok && TRACE_ON(dcomp))
    {
        QueryPerformanceCounter(&qpc_end);
        QueryPerformanceFrequency(&qpc_freq);
        if (qpc_freq.QuadPart)
            TRACE("DCOMP: do_composite_dxgi_surface %llu us\n",
                    (unsigned long long)((qpc_end.QuadPart - qpc_start.QuadPart) * 1000000ULL / qpc_freq.QuadPart));
    }
    return ok;
}

/*
 * Single-pass composite: one device-context state, one back-buffer clear and
 * one present per target per composite tick. composite_device() opens a pass
 * on a target before rendering its visual tree's frames and closes it after
 * the last frame of that target: every visual of one window is drawn into
 * one back buffer (in z-order, each quad placed and clipped by its own
 * projection matrix) and the window is updated with one present.
 */
static void composite_target_begin(struct composition_target *target)
{
    target->composite_batch_active = TRUE;
    target->composite_batch_cleared = FALSE;
    SetRect(&target->composite_batch_rect, 0, 0, 0, 0);
    target->composite_root_hwnd = NULL;
}

static void composite_target_end(struct composition_target *target)
{
    struct wined3d_swapchain *wined3d_swapchain;
    RECT rect;

    if (!target->composite_batch_active)
        return;
    target->composite_batch_active = FALSE;
    if (!target->composite_batch_cleared || !target->composite_context)
        return;

    ID3D11DeviceContext_Flush(target->composite_context);

    rect = target->composite_batch_rect;
    if ((wined3d_swapchain = dxgi_get_wined3d_swapchain((struct IDXGISwapChain *)target->composite_swapchain))
            && target->composite_root_hwnd && rect.right > rect.left && rect.bottom > rect.top)
    {
        HWND present_hwnd = (target->composite_hwnd && IsWindow(target->composite_hwnd))
                ? target->composite_hwnd : target->composite_root_hwnd;
        wined3d_swapchain_present(wined3d_swapchain, NULL, NULL, present_hwnd, 0, 0);
        TRACE("DCOMP: batch-presented %ldx%ld at %ld,%ld to hwnd %p\n",
                rect.right - rect.left, rect.bottom - rect.top, rect.left, rect.top,
                present_hwnd);
    }
}

/*
 * A shared visual handle is created and consumed inside one process; the
 * window server only maps it to the root pointer of the target that published
 * it. That mapping is a window server round trip, and the server is the queue
 * every thread of the process is serialised on, so it is cached in process.
 * The cache is written by the publisher and refreshed by the composite tick
 * outside dcomp_cs; code that owns dcomp_cs only reads memory.
 */
struct shared_visual_entry
{
    HANDLE handle;
    IDCompositionVisual *root;
    struct list entry;
};

static IDCompositionVisual *shared_visual_cache_lookup(HANDLE handle);
static struct composition_visual *shared_visual_cached_root(HANDLE shared_visual_handle);

static struct list shared_visual_cache = LIST_INIT(shared_visual_cache);

static IDCompositionVisual *shared_visual_cache_lookup(HANDLE handle)
{
    struct shared_visual_entry *entry;

    LIST_FOR_EACH_ENTRY(entry, &shared_visual_cache, struct shared_visual_entry, entry)
        if (entry->handle == handle)
            return entry->root;

    return NULL;
}

/* dcomp_cs held. */
void shared_visual_cache_set(HANDLE handle, IDCompositionVisual *root)
{
    struct shared_visual_entry *entry;

    LIST_FOR_EACH_ENTRY(entry, &shared_visual_cache, struct shared_visual_entry, entry)
    {
        if (entry->handle == handle)
        {
            if (entry->root != root)
                InterlockedIncrement(&dcomp_tree_version);
            entry->root = root;
            return;
        }
    }

    if ((entry = malloc(sizeof(*entry))))
    {
        entry->handle = handle;
        entry->root = root;
        list_add_tail(&shared_visual_cache, &entry->entry);
        InterlockedIncrement(&dcomp_tree_version);
    }
}

/* Root of a shared visual published by another process.
 *
 * The publisher's root is a pointer into its address space, so the consumer
 * cannot dereference it. Build a local stand-in (a 'proxy') once per handle and
 * hand that to the visual tree walk: it carries the published size, the shared
 * resource handle of the publisher's surface, and it is a struct this process
 * can read. The proxy is referenced by the cache, which lives for the lifetime
 * of the process, so the reference create_visual() hands out is kept here. */
static IDCompositionVisual *shared_visual_proxy_root(HANDLE shared_visual_handle, HANDLE surface,
        UINT width, UINT height, unsigned int dirty_count)
{
    IDCompositionVisual *root;
    struct composition_visual *impl;

    dcomp_lock();
    if ((root = shared_visual_cache_lookup(shared_visual_handle)))
    {
        impl = impl_from_IDCompositionVisual(root);
        if (impl->is_shared_proxy)
        {
            /* Republish: a resized surface, a new content handle after a
             * device lost, or a new frame presented by the producer. Bump the change
             * counter only when the server generation or geometry/surface changed. */
            if (impl->size.cx != (LONG)width || impl->size.cy != (LONG)height)
            {
                impl->size.cx = width;
                impl->size.cy = height;
                impl->content_version++;
            }
            if (surface && surface != impl->shared_content_handle)
            {
                HANDLE cap_surf = (impl->captured_hwnd && IsWindow(impl->captured_hwnd))
                        ? GetPropW(impl->captured_hwnd, L"__wine_dcomp_hwnd_surface") : NULL;
                if (!cap_surf || cap_surf == surface)
                {
                    impl->shared_content_handle = surface;
                    impl->content_version++;
                }
            }
            if (dirty_count != impl->shared_dirty_count)
            {
                impl->shared_dirty_count = dirty_count;
                impl->content_version++;
            }
            if (impl->shm_ptr)
            {
                UINT seq = ((const volatile UINT *)impl->shm_ptr)[3];
                if (seq && !(seq & 1) && seq != impl->shm_last_seq)
                    impl->content_version++;
            }
            dcomp_unlock();
            return root;
        }
    }
    dcomp_unlock();

    if (FAILED(create_visual(2, &IID_IDCompositionVisual, (void **)&root)))
        return NULL;

    impl = impl_from_IDCompositionVisual(root);
    impl->is_shared_proxy = TRUE;
    impl->shared_visual_handle = shared_visual_handle;
    impl->shared_content_handle = surface;
    impl->size.cx = width;
    impl->size.cy = height;
    impl->shared_dirty_count = dirty_count;
    impl->content_version = 1;
    InterlockedIncrement(&dcomp_tree_version);

    return root;
}

/* Window server round trip: must run with dcomp_cs released. */
IDCompositionVisual *shared_visual_query_root(HANDLE shared_visual_handle)
{
    IDCompositionVisual *root = NULL;
    IDCompositionVisual *proxy;
    NTSTATUS status;
    HANDLE surface = NULL;
    UINT width = 0, height = 0;
    unsigned int dirty_count = 0;
    DWORD owner = 0;

    SERVER_START_REQ(dcomp_get_shared_visual_info)
    {
        req->handle = wine_server_obj_handle(shared_visual_handle);
        if (!(status = wine_server_call(req)))
        {
            root = wine_server_get_ptr(reply->target_root);
            surface = wine_server_ptr_handle(reply->target_surface);
            width = reply->width;
            height = reply->height;
            dirty_count = reply->dirty_count;
            owner = reply->owner_pid;
        }
    }
    SERVER_END_REQ;

    if (status)
    {
        struct composition_visual *cached;

        ERR("dcomp_get_shared_visual_info failed, status %#lx.\n", status);
        cached = shared_visual_cached_root(shared_visual_handle);
        return cached ? (IDCompositionVisual *)&cached->IDCompositionVisualUnknown_iface : NULL;
    }

    /* Cross-process shared visual (Chromium's GPU process publishes, this
     * process consumes): target_root names a visual in the publisher's address
     * space, so use a local proxy bound to the surface it published. */
    if (owner && owner != GetCurrentProcessId() && (proxy = shared_visual_proxy_root(shared_visual_handle,
            surface, width, height, dirty_count)))
        root = proxy;

    /* Publish the mapping on every successful query, including a cleared root:
     * a target that lost its root must not keep serving the stale one. */
    dcomp_lock();
    shared_visual_cache_set(shared_visual_handle, root);
    dcomp_unlock();

    return root;
}

/* dcomp_cs held: memory only. */
static struct composition_visual *shared_visual_cached_root(HANDLE shared_visual_handle)
{
    IDCompositionVisual *root = shared_visual_cache_lookup(shared_visual_handle);

    return root ? impl_from_IDCompositionVisual(root) : NULL;
}

static struct composition_visual *find_container_visual_in_tree(struct composition_visual *visual,
                                                                 HANDLE shared_handle,
                                                                 struct composition_visual *target_root)
{
    struct composition_visual *child, *found;

    if (!visual)
        return NULL;

    if (visual->shared_visual_handle && shared_handle)
    {
        if (compare_handles(visual->shared_visual_handle, shared_handle))
            return visual;

        if (target_root && shared_visual_cached_root(visual->shared_visual_handle) == target_root)
            return visual;
    }

    LIST_FOR_EACH_ENTRY(child, &visual->child_visuals, struct composition_visual, entry)
    {
        if (target_root && child == target_root)
            return visual;
        found = find_container_visual_in_tree(child, shared_handle, target_root);
        if (found)
            return found;
    }

    return NULL;
}

static struct composition_visual *find_root_visual_container(struct composition_target *target,
                                                             struct composition_target **out_host_target)
{
    struct composition_visual *target_root = target->root ? impl_from_IDCompositionVisual(target->root) : NULL;
    struct composition_visual *container = NULL;
    struct composition_device *dev;
    struct composition_target *t;

    if (out_host_target)
        *out_host_target = NULL;

    LIST_FOR_EACH_ENTRY(dev, &dcomp_devices, struct composition_device, dev_entry)
    {
        LIST_FOR_EACH_ENTRY(t, &dev->targets, struct composition_target, entry)
        {
            if (t == target || !t->root)
                continue;

            container = find_container_visual_in_tree(impl_from_IDCompositionVisual(t->root),
                                                        target->shared_visual_handle,
                                                        target_root);
            if (container)
            {
                if (out_host_target)
                    *out_host_target = t;
                return container;
            }
        }
    }

    return NULL;
}

static struct composition_visual *resolve_shared_visual_root(HANDLE shared_handle)
{
    struct composition_visual *root = NULL;
    struct composition_device *dev;
    struct composition_target *target;

    if (!shared_handle)
        return NULL;

    root = shared_visual_cached_root(shared_handle);
    if (root && (root->content || !list_empty(&root->child_visuals)))
        return root;

    LIST_FOR_EACH_ENTRY(dev, &dcomp_devices, struct composition_device, dev_entry)
    {
        LIST_FOR_EACH_ENTRY(target, &dev->targets, struct composition_target, entry)
        {
            if (target->shared_visual_handle &&
                compare_handles(target->shared_visual_handle, shared_handle))
            {
                if (target->root)
                    return impl_from_IDCompositionVisual(target->root);
            }
        }
    }

    return root;
}

extern struct composition_surface *g_last_rendered_surface;

static BOOL visual_tree_has_content(struct composition_visual *visual)
{
    struct composition_visual *child;
    if (!visual) return FALSE;
    if (visual->content) return TRUE;
    LIST_FOR_EACH_ENTRY(child, &visual->child_visuals, struct composition_visual, entry)
    {
        if (visual_tree_has_content(child))
            return TRUE;
    }
    return FALSE;
}

/*
 * Locking: dcomp_cs protects the composition object graph - the device list,
 * the target lists, the visual trees and the per-target bookkeeping. It may
 * only be held across pure memory operations on dcomp's own objects, plus a
 * refcount bump on one. Entering Direct3D, Direct2D, GDI, the window manager
 * or the window server from inside the critical section inverts against the
 * threads that call into dcomp from those subsystems - a browser compositor
 * thread calling SetContent() already owns the Direct3D device lock, and the
 * window server is the queue every thread of the process is serialised on -
 * and that inversion parks the composite thread inside the critical section
 * for a full 60 second critical section timeout, which blocks visual
 * attachment and everything that follows it.
 *
 * A refresh tick therefore runs in five steps: collect the shared visual
 * handles and the target snapshots with the critical section held; resolve
 * those handles with the window server and resolve the window each target
 * composites into with the critical section released; flatten the visual trees
 * into a list of self-contained render jobs with the critical section held;
 * take the Direct3D references and blit with the critical section released;
 * write the bookkeeping back with the critical section held.
 *
 * Content, Direct2D contexts and interop targets are only ever referenced by
 * the visual that owns them, so a frame keeps the visual alive while it
 * records their pointers, and the objects released by a dcomp entry point that
 * owns the critical section are deferred to the reclaim list below and dropped
 * at the start of the next tick. A recorded pointer therefore cannot be freed
 * while a frame is in flight.
 */

static struct list dcomp_reclaim_list = LIST_INIT(dcomp_reclaim_list);

struct dcomp_reclaim_entry
{
    IUnknown *object;
    struct list entry;
};

BOOL dcomp_has_active_composite_thread(void)
{
    struct composition_device *dev;
    BOOL active = FALSE;

    dcomp_lock();
    LIST_FOR_EACH_ENTRY(dev, &dcomp_devices, struct composition_device, dev_entry)
    {
        if (dev->thread && !dev->thread_exited)
        {
            active = TRUE;
            break;
        }
    }
    dcomp_unlock();
    return active;
}

void dcomp_reclaim_push(IUnknown *object)
{
    struct dcomp_reclaim_entry *entry;

    if (!object)
        return;

    if (!dcomp_has_active_composite_thread())
    {
        IUnknown_Release(object);
        return;
    }

    if (TryEnterCriticalSection(&composite_render_cs))
    {
        dcomp_reclaim_drain();
        IUnknown_Release(object);
        LeaveCriticalSection(&composite_render_cs);
        return;
    }

    entry = malloc(sizeof(*entry));
    if (!entry)
    {
        /* Out of memory: releasing here is safe on the caller's side, which is
         * dropping the last dcomp reference and is not inside the composite
         * tick that recorded the pointer. */
        IUnknown_Release(object);
        return;
    }

    entry->object = object;
    dcomp_lock();
    list_add_tail(&dcomp_reclaim_list, &entry->entry);
    dcomp_unlock();
}

static void dcomp_reclaim_drain(void)
{
    struct dcomp_reclaim_entry *entry, *entry_next;
    struct list list;

    list_init(&list);
    dcomp_lock();
    LIST_FOR_EACH_ENTRY_SAFE(entry, entry_next, &dcomp_reclaim_list, struct dcomp_reclaim_entry, entry)
    {
        list_remove(&entry->entry);
        list_add_tail(&list, &entry->entry);
    }
    dcomp_unlock();

    /* Releasing enters Direct3D and GDI: run with the critical section released. */
    LIST_FOR_EACH_ENTRY_SAFE(entry, entry_next, &list, struct dcomp_reclaim_entry, entry)
    {
        list_remove(&entry->entry);
        IUnknown_Release(entry->object);
        free(entry);
    }
}

/* One queued blit: the pointers it needs, with the references that keep the
 * dcomp objects alive across the render pass. */
struct composite_frame
{
    struct composition_target *target;
    struct composition_visual *visual;        /* refcounted */
    struct composition_surface *surface;      /* refcounted, set for the fallback blit */
    IUnknown *content;                        /* raw visual->content, referenced at render time */
    ID2D1DeviceContext *device_context;       /* raw visual->device_context */
    ID2D1GdiInteropRenderTarget *interop;     /* raw visual->interop */
    IUnknown *frame_content;                  /* references taken outside the critical section */
    ID2D1DeviceContext *frame_context;
    ID2D1GdiInteropRenderTarget *frame_interop;
    unsigned int version;                     /* content_version this frame renders */
    unsigned int transform_version;           /* accumulated transform/effect version */
    HWND target_hwnd;
    POINT pt;                                 /* accumulated visual offset chain */
    D3DMATRIX world_transform;                /* accumulated 3D transform to target space */
    D2D_RECT_F target_clip;                   /* accumulated clip in target space */
    BOOL has_clip;
    BOOL has_transform;
    BOOL is_fallback;
    BOOL rendered;
    BOOL culled;
};

/* A target plus the window it composites into, for the steps that must run
 * outside the critical section. */
struct target_snapshot
{
    struct composition_target *target;        /* refcounted */
    struct composition_visual *root;          /* refcounted, may be NULL */
    struct composition_visual *container;     /* refcounted, may be NULL */
    struct composition_target *host_target;   /* refcounted, may be NULL */
    HWND hwnd;
    HWND resolved_hwnd;
    BOOL needs_hwnd;
    BOOL use_container;
};

static BOOL visual_get_clip_rect(const struct composition_visual *visual, D2D_RECT_F *rect);
static void composite_frame_cleanup(struct composite_frame *frame);

static struct composite_frame *device_alloc_frame(struct composition_device *device)
{
    if (device->frame_buf_count == device->frame_buf_cap)
    {
        unsigned int new_cap = device->frame_buf_cap ? device->frame_buf_cap * 2 : 16;
        struct composite_frame *new_buf = realloc(device->frame_buf, new_cap * sizeof(*new_buf));
        if (!new_buf)
            return NULL;
        device->frame_buf = new_buf;
        device->frame_buf_cap = new_cap;
    }
    return &device->frame_buf[device->frame_buf_count++];
}

static struct target_snapshot *device_alloc_snapshot(struct composition_device *device)
{
    if (device->snap_buf_count == device->snap_buf_cap)
    {
        unsigned int new_cap = device->snap_buf_cap ? device->snap_buf_cap * 2 : 8;
        struct target_snapshot *new_buf = realloc(device->snap_buf, new_cap * sizeof(*new_buf));
        if (!new_buf)
            return NULL;
        device->snap_buf = new_buf;
        device->snap_buf_cap = new_cap;
    }
    return &device->snap_buf[device->snap_buf_count++];
}

static void device_add_handle(struct composition_device *device, HANDLE handle)
{
    unsigned int i;

    for (i = 0; i < device->handle_buf_count; i++)
        if (device->handle_buf[i] == handle)
            return;

    if (device->handle_buf_count == device->handle_buf_cap)
    {
        unsigned int new_cap = device->handle_buf_cap ? device->handle_buf_cap * 2 : 8;
        HANDLE *new_buf = realloc(device->handle_buf, new_cap * sizeof(*new_buf));
        if (!new_buf)
            return;
        device->handle_buf = new_buf;
        device->handle_buf_cap = new_cap;
    }
    device->handle_buf[device->handle_buf_count++] = handle;
}

static void composite_frame_init(struct composite_frame *frame, struct composition_target *target,
        struct composition_visual *visual, HWND target_hwnd, POINT pt)
{
    frame->target = target;
    frame->visual = visual;
    frame->surface = NULL;
    frame->content = NULL;
    frame->device_context = NULL;
    frame->interop = NULL;
    frame->version = 0;
    frame->transform_version = 0;
    frame->frame_content = NULL;
    frame->frame_context = NULL;
    frame->frame_interop = NULL;
    frame->target_hwnd = target_hwnd;
    frame->pt = pt;
    dcomp_matrix_identity(&frame->world_transform);
    memset(&frame->target_clip, 0, sizeof(frame->target_clip));
    frame->has_clip = FALSE;
    frame->has_transform = FALSE;
    frame->is_fallback = FALSE;
    frame->rendered = FALSE;
    frame->culled = FALSE;
}

static void transform_rect_to_target(const D2D_RECT_F *local_rect, const D3DMATRIX *m, D2D_RECT_F *out)
{
    float xs[4] = { local_rect->left, local_rect->right, local_rect->left, local_rect->right };
    float ys[4] = { local_rect->top, local_rect->top, local_rect->bottom, local_rect->bottom };
    float min_x = 0.0f, min_y = 0.0f, max_x = 0.0f, max_y = 0.0f;
    int i;

    for (i = 0; i < 4; i++)
    {
        float x = xs[i] * m->m[0][0] + ys[i] * m->m[1][0] + m->m[3][0];
        float y = xs[i] * m->m[0][1] + ys[i] * m->m[1][1] + m->m[3][1];
        float w = xs[i] * m->m[0][3] + ys[i] * m->m[1][3] + m->m[3][3];
        if (w > 1e-4f)
        {
            x /= w;
            y /= w;
        }
        if (i == 0 || x < min_x) min_x = x;
        if (i == 0 || y < min_y) min_y = y;
        if (i == 0 || x > max_x) max_x = x;
        if (i == 0 || y > max_y) max_y = y;
    }
    out->left = min_x;
    out->top = min_y;
    out->right = max_x;
    out->bottom = max_y;
}

static BOOL CALLBACK dcomp_find_captured_child_cb(HWND hwnd, LPARAM lparam)
{
    HWND *out = (HWND *)lparam;

    if (GetPropW(hwnd, L"__wine_dcomp_captured_hwnd"))
    {
        *out = hwnd;
        return FALSE;
    }
    return TRUE;
}

static BOOL CALLBACK dcomp_find_captured_hwnd_cb(HWND hwnd, LPARAM lparam)
{
    HWND *out = (HWND *)lparam;

    if (GetPropW(hwnd, L"__wine_dcomp_captured_hwnd"))
    {
        *out = hwnd;
        return FALSE;
    }
    EnumChildWindows(hwnd, dcomp_find_captured_child_cb, lparam);
    return !*out;
}

static void dcomp_sync_captured_hwnd(HWND target_hwnd, struct composition_visual *visual)
{
    HANDLE hwnd_surf;

    if (!visual || !visual->is_shared_proxy)
        return;
    if (!visual->captured_hwnd || !IsWindow(visual->captured_hwnd))
    {
        HWND found = NULL;
        EnumWindows(dcomp_find_captured_hwnd_cb, (LPARAM)&found);
        visual->captured_hwnd = found;
    }
    if (!visual->captured_hwnd)
        return;
    hwnd_surf = GetPropW(visual->captured_hwnd, L"__wine_dcomp_hwnd_surface");
    if (hwnd_surf && hwnd_surf != visual->shared_content_handle)
    {
        visual->shared_content_handle = hwnd_surf;
        visual->content_version++;
    }
}

static void collect_visual_frames(struct composition_device *device, struct composition_target *target,
        struct composition_visual *visual, HWND target_hwnd, float parent_x, float parent_y,
        const D3DMATRIX *parent_transform, BOOL parent_has_transform,
        unsigned int parent_xform_ver, const D2D_RECT_F *parent_clip,
        unsigned int depth, BOOL force_redraw, unsigned int *queued)
{
    struct composition_visual *child, *child_root;
    struct composite_frame *frame;
    D3DMATRIX local_m, world_m, offset_m, parent_world, clip_m;
    D2D_RECT_F local_clip, v_clip, cur_clip;
    const D2D_RECT_F *effective_clip_ptr = parent_clip;
    BOOL local_has, has_transform;
    unsigned int xform_ver;
    float pos_x, pos_y;
    POINT pt;

    if (!visual || depth > 256)
        return;

    if (visual->visibility == DCOMPOSITION_BACKFACE_VISIBILITY_HIDDEN)
        return;

    pos_x = parent_x + visual->offset_x;
    pos_y = parent_y + visual->offset_y;
    pt.x = (LONG)floorf(pos_x + 0.5f);
    pt.y = (LONG)floorf(pos_y + 0.5f);

    visual_get_local_transform_3d(visual, &local_m);
    local_has = !dcomp_matrix_is_identity(&local_m);
    xform_ver = parent_xform_ver * 131u + visual_get_transform_version(visual);
    has_transform = parent_has_transform || local_has;

    if (has_transform)
    {
        dcomp_matrix_identity(&offset_m);
        offset_m.m[3][0] = visual->offset_x;
        offset_m.m[3][1] = visual->offset_y;
        if (parent_has_transform && parent_transform)
            parent_world = *parent_transform;
        else
        {
            dcomp_matrix_identity(&parent_world);
            parent_world.m[3][0] = parent_x;
            parent_world.m[3][1] = parent_y;
        }
        dcomp_matrix_multiply(&world_m, &local_m, &offset_m);
        dcomp_matrix_multiply(&world_m, &world_m, &parent_world);
    }
    else
    {
        dcomp_matrix_identity(&world_m);
        world_m.m[3][0] = pos_x;
        world_m.m[3][1] = pos_y;
    }

    if (visual_get_clip_rect(visual, &local_clip))
    {
        if (parent_has_transform && parent_transform)
        {
            dcomp_matrix_identity(&offset_m);
            offset_m.m[3][0] = visual->offset_x;
            offset_m.m[3][1] = visual->offset_y;
            dcomp_matrix_multiply(&clip_m, &offset_m, parent_transform);
            transform_rect_to_target(&local_clip, &clip_m, &v_clip);
        }
        else
        {
            v_clip.left = pos_x + local_clip.left;
            v_clip.top = pos_y + local_clip.top;
            v_clip.right = pos_x + local_clip.right;
            v_clip.bottom = pos_y + local_clip.bottom;
        }
        if (parent_clip)
        {
            cur_clip.left = max(parent_clip->left, v_clip.left);
            cur_clip.top = max(parent_clip->top, v_clip.top);
            cur_clip.right = min(parent_clip->right, v_clip.right);
            cur_clip.bottom = min(parent_clip->bottom, v_clip.bottom);
        }
        else
        {
            cur_clip = v_clip;
        }
        effective_clip_ptr = &cur_clip;
    }

    if (visual->shared_visual_handle)
    {
        child_root = resolve_shared_visual_root(visual->shared_visual_handle);
        if (child_root && child_root != visual)
            collect_visual_frames(device, target, child_root, target_hwnd, pos_x, pos_y, &world_m,
                    has_transform, xform_ver, effective_clip_ptr, depth + 1, force_redraw, queued);

        if (!*queued && !visual_tree_has_content(child_root) && g_last_rendered_surface
                && InterlockedCompareExchange(&g_last_rendered_surface->ref, 0, 0) > 0
                && g_last_rendered_surface->physical_surface
                && g_last_rendered_surface != target->composited_surface)
        {
            if ((frame = device_alloc_frame(device)))
            {
                composite_frame_init(frame, target, visual, target_hwnd, pt);
                frame->surface = g_last_rendered_surface;
                frame->world_transform = world_m;
                frame->has_transform = has_transform;
                frame->transform_version = xform_ver;
                if (effective_clip_ptr)
                {
                    frame->target_clip = *effective_clip_ptr;
                    frame->has_clip = TRUE;
                }
                IDCompositionSurfaceUnknown_AddRef(&frame->surface->IDCompositionSurfaceUnknown_iface);
                IDCompositionVisual_AddRef((IDCompositionVisual *)&visual->IDCompositionVisualUnknown_iface);
                frame->is_fallback = TRUE;
                (*queued)++;
            }
        }
    }

    /* A proxy has no content of its own: its pixels come from the publisher's
     * surface, which changes on every publisher frame, so queue it every tick. */
    {
        unsigned int content_ver = visual->content_version;
        BOOL has_drawn_content = TRUE;
        if (visual->content)
        {
            struct composition_surface *surf = unsafe_impl_from_IDCompositionSurface((IDCompositionSurface *)visual->content);
            if (surf)
            {
                if (surf->factory && !surf->content_version)
                    has_drawn_content = FALSE;
                content_ver += surf->content_version;
            }
            else if (visual->wined3d_swapchain)
            {
                UINT present_count = 0;
                device->last_has_swapchain = TRUE;
                if (SUCCEEDED(IDXGISwapChain_GetLastPresentCount((IDXGISwapChain *)visual->content, &present_count)))
                    content_ver += present_count + 1;
            }
        }
        if (visual->is_shared_proxy)
        {
            dcomp_sync_captured_hwnd(target_hwnd, visual);
            content_ver = visual->content_version;
        }
        if (visual->is_shared_proxy && visual->shared_content_handle)
        {
            if (visual->shm_ptr && visual->shm_kmt_handle == visual->shared_content_handle)
            {
                UINT seq = visual->shm_ptr[3];
                if (seq && !(seq & 1))
                {
                    RECT crc = {0};
                    content_ver += (seq >> 1) + 1;
                    if (target_hwnd && GetClientRect(target_hwnd, &crc))
                        content_ver ^= ((UINT)crc.right << 16) ^ (UINT)crc.bottom;
                }
                else
                    content_ver = visual->composited_version;
            }
            else
            {
                /* SHM not opened yet: retry until producer creates it on first Present. */
                content_ver = visual->composited_version + 1;
            }
        }
        if (((visual->is_shared_proxy && visual->shared_content_handle)
                    || (visual->content && has_drawn_content))
                && (force_redraw
                    || content_ver != visual->composited_version
                    || xform_ver != visual->composited_transform_version
                    || visual->composited_hwnd != target_hwnd))
        {
            if ((frame = device_alloc_frame(device)))
            {
                composite_frame_init(frame, target, visual, target_hwnd, pt);
                frame->content = visual->content;
                frame->device_context = visual->device_context;
                frame->interop = visual->interop;
                frame->version = content_ver;
                frame->transform_version = xform_ver;
                frame->world_transform = world_m;
                frame->has_transform = has_transform;
                if (effective_clip_ptr)
                {
                    frame->target_clip = *effective_clip_ptr;
                    frame->has_clip = TRUE;
                }
                IDCompositionVisual_AddRef((IDCompositionVisual *)&visual->IDCompositionVisualUnknown_iface);
                (*queued)++;
                /* A dirty parent clears/repaints the target backbuffer: force all
                 * descendant visuals (e.g. WebView2 shared proxy) to draw on top in
                 * the exact same batch pass so they are never overwritten. */
                force_redraw = TRUE;
            }
        }
    }

    LIST_FOR_EACH_ENTRY(child, &visual->child_visuals, struct composition_visual, entry)
        collect_visual_frames(device, target, child, target_hwnd, pos_x, pos_y, &world_m,
                has_transform, xform_ver, effective_clip_ptr, depth + 1, force_redraw, queued);
}

/* Pure memory: gather the shared visual handles in use, so that the window
 * server round-trips that resolve them can run outside the critical section. */
static void collect_visual_handles(struct composition_device *device, struct composition_visual *visual,
        unsigned int depth, BOOL *has_shared)
{
    struct composition_visual *child;

    if (!visual || depth > 256)
        return;

    if (visual->is_shared_proxy)
        *has_shared = TRUE;

    if (visual->shared_visual_handle)
    {
        *has_shared = TRUE;
        device_add_handle(device, visual->shared_visual_handle);
    }

    LIST_FOR_EACH_ENTRY(child, &visual->child_visuals, struct composition_visual, entry)
        collect_visual_handles(device, child, depth + 1, has_shared);
}

static void snapshot_device_targets(struct composition_device *device, BOOL *has_shared)
{
    struct composition_target *target;
    struct target_snapshot *snap;

    device->snap_buf_count = 0;
    device->handle_buf_count = 0;

    dcomp_lock();
    LIST_FOR_EACH_ENTRY(target, &device->targets, struct composition_target, entry)
    {
        if (!(snap = device_alloc_snapshot(device)))
            continue;

        snap->target = target;
        IDCompositionTarget_AddRef(&target->IDCompositionTarget_iface);
        snap->root = target->root ? impl_from_IDCompositionVisual(target->root) : NULL;
        if (snap->root)
            IDCompositionVisual_AddRef((IDCompositionVisual *)&snap->root->IDCompositionVisualUnknown_iface);
        snap->hwnd = target->hwnd;
        snap->resolved_hwnd = NULL;
        snap->needs_hwnd = FALSE;
        snap->use_container = FALSE;
        snap->container = NULL;
        snap->host_target = NULL;

        if (target->shared_visual_handle)
        {
            *has_shared = TRUE;
            device_add_handle(device, target->shared_visual_handle);
        }

        if (target->root)
            collect_visual_handles(device, impl_from_IDCompositionVisual(target->root), 0, has_shared);
    }
    dcomp_unlock();
}

static BOOL warm_shared_visual_cache(struct composition_device *device)
{
    BOOL has_cross_process = FALSE;
    unsigned int i;

    /* Window server round-trips, outside the critical section. */
    for (i = 0; i < device->handle_buf_count; i++)
    {
        HANDLE h = device->handle_buf[i];
        IDCompositionVisual *root = shared_visual_query_root(h);

        if (root && impl_from_IDCompositionVisual(root)->is_shared_proxy)
            has_cross_process = TRUE;

        dcomp_lock();
        shared_visual_cache_set(h, root);
        dcomp_unlock();
    }
    return has_cross_process;
}

static void resolve_target_windows(struct composition_device *device, BOOL *any_needs_hwnd)
{
    unsigned int i;

    for (i = 0; i < device->snap_buf_count; i++)
    {
        struct target_snapshot *snap = &device->snap_buf[i];
        if (snap->hwnd && IsWindow(snap->hwnd))
            continue;
        snap->needs_hwnd = TRUE;
        *any_needs_hwnd = TRUE;
    }
}

/* A target without a usable window is attached to the visual tree that hosts it.
 * The walk itself is memory plus cache lookups; the window calls that follow it
 * run outside the critical section. */
static void find_target_containers(struct composition_device *device)
{
    struct composition_target *host_target;
    unsigned int i;

    dcomp_lock();
    for (i = 0; i < device->snap_buf_count; i++)
    {
        struct target_snapshot *snap = &device->snap_buf[i];
        if (!snap->needs_hwnd)
            continue;

        snap->container = find_root_visual_container(snap->target, &host_target);
        if (snap->container)
            IDCompositionVisual_AddRef((IDCompositionVisual *)&snap->container->IDCompositionVisualUnknown_iface);
        if ((snap->host_target = host_target))
            IDCompositionTarget_AddRef(&host_target->IDCompositionTarget_iface);
    }
    dcomp_unlock();
}

/* Window manager calls: validate the window each snapshot will composite into,
 * with dcomp_cs released, so that the applying step below is pure memory. */
static void validate_target_windows(struct composition_device *device)
{
    unsigned int i;

    for (i = 0; i < device->snap_buf_count; i++)
    {
        struct target_snapshot *snap = &device->snap_buf[i];
        if (!snap->needs_hwnd)
            continue;

        if (snap->host_target && snap->host_target->hwnd && IsWindow(snap->host_target->hwnd))
        {
            HWND host_root = GetAncestor(snap->host_target->hwnd, GA_ROOT);
            snap->use_container = TRUE;
            snap->resolved_hwnd = (host_root && IsWindow(host_root)) ? host_root : snap->host_target->hwnd;
            continue;
        }

        snap->resolved_hwnd = find_root_top_level_window();
    }
}

static void collect_device_frames(struct composition_device *device, LONG *out_tree_ver, LONG *out_xform_ver)
{
    struct composition_visual *root;
    D3DMATRIX identity;
    unsigned int queued = 0, i;

    device->frame_buf_count = 0;
    dcomp_matrix_identity(&identity);
    dcomp_lock();
    *out_tree_ver = InterlockedCompareExchange(&dcomp_tree_version, 0, 0);
    *out_xform_ver = InterlockedCompareExchange(&dcomp_transform_version, 0, 0);
    for (i = 0; i < device->snap_buf_count; i++)
    {
        struct target_snapshot *snap = &device->snap_buf[i];
        struct composition_target *target = snap->target;

        if (!(root = snap->root))
            continue;

        if (target->hwnd)
        {
            HWND p = target->hwnd;
            BOOL captured = FALSE;
            while (p)
            {
                if (GetPropW(p, L"__wine_dcomp_captured_hwnd"))
                {
                    captured = TRUE;
                    break;
                }
                p = GetParent(p);
            }
            if (captured)
                continue;
        }

        if (snap->needs_hwnd)
        {
            if (snap->use_container && !root->parent)
            {
                target->hwnd = snap->resolved_hwnd;
                root->parent = snap->container;
            }
            else if (snap->resolved_hwnd)
            {
                target->hwnd = snap->resolved_hwnd;
            }
        }

        {
            unsigned int target_start = device->frame_buf_count;
            unsigned int target_queued = 0;
            BOOL force = target->needs_redraw;
            target->needs_redraw = FALSE;
            collect_visual_frames(device, target, root, target->hwnd, 0.0f, 0.0f,
                    &identity, FALSE, 0, NULL, 0, force, &target_queued);
            if (!force && target_queued > 0)
            {
                /* Any dirty visual clears and repaints the target backbuffer:
                 * re-collect the entire target in full z-order so sibling and
                 * earlier visuals (e.g. Paint canvas) are never erased by
                 * light_clear when only one visual was dirty. */
                unsigned int j;
                for (j = target_start; j < device->frame_buf_count; j++)
                    composite_frame_cleanup(&device->frame_buf[j]);
                device->frame_buf_count = target_start;
                target_queued = 0;
                collect_visual_frames(device, target, root, target->hwnd, 0.0f, 0.0f,
                        &identity, FALSE, 0, NULL, 0, TRUE, &target_queued);
            }
            queued += target_queued;
        }
    }
    dcomp_unlock();
}

/* Visual opacity: an effect group (IDCompositionEffectGroup, set via
 * SetEffectGroup / IDCompositionVisual2::SetOpacity) carries it; a visual
 * without one is fully opaque. Pure memory, dcomp_cs-protected fields. */
static inline struct dcomp_transform *impl_from_effect_group(IDCompositionEffectGroup *iface)
{
    return CONTAINING_RECORD(iface, struct dcomp_transform, IDCompositionEffectGroup_iface);
}

static inline struct dcomp_transform *impl_from_rectangle_clip(IDCompositionRectangleClip *iface)
{
    return CONTAINING_RECORD(iface, struct dcomp_transform, IDCompositionRectangleClip_iface);
}

static float visual_get_opacity(const struct composition_visual *visual)
{
    struct dcomp_transform *transform;

    if (visual->effect)
    {
        transform = unsafe_impl_from_dcomp_transform((IUnknown *)visual->effect);
        if (transform && transform->kind == OBJECT_EFFECT_GROUP)
            return transform->opacity;
    }

    if (visual->transform)
    {
        transform = unsafe_impl_from_dcomp_transform((IUnknown *)visual->transform);
        if (transform && transform->kind == OBJECT_EFFECT_GROUP)
            return transform->opacity;
    }
    return 1.0f;
}

/* Visual clip rectangle in content coordinates: a rectangle clip object set
 * via SetClipObject overrides the rectangle set via SetClip. Returns FALSE
 * when the visual has no effective clip. */
static BOOL visual_get_clip_rect(const struct composition_visual *visual, D2D_RECT_F *rect)
{
    IDCompositionRectangleClip *rectangle_clip;
    struct dcomp_transform *transform;

    if (visual->clip && SUCCEEDED(IDCompositionClip_QueryInterface(visual->clip,
            &IID_IDCompositionRectangleClip, (void **)&rectangle_clip)))
    {
        transform = impl_from_rectangle_clip(rectangle_clip);
        rect->left = transform->clip_left;
        rect->top = transform->clip_top;
        rect->right = transform->clip_right;
        rect->bottom = transform->clip_bottom;
        IDCompositionRectangleClip_Release(rectangle_clip);
        return rect->right > rect->left && rect->bottom > rect->top;
    }

    if (visual->has_rect_clip && visual->rect_clip.right > visual->rect_clip.left
            && visual->rect_clip.bottom > visual->rect_clip.top)
    {
        *rect = visual->rect_clip;
        return TRUE;
    }
    return FALSE;
}

static IDXGISurface *frame_get_surface(struct composite_frame *frame)
{
    ID3D11Texture2D *texture = NULL;
    IDXGISwapChain1 *swapchain = NULL;
    IDXGISwapChain *swapchain_v1 = NULL;
    IDCompositionSurface *surface = NULL;
    IDXGIResource *dxgi_res = NULL;
    ID3D11Resource *d3d_res = NULL;
    IDXGISurface *dxgi_surface = NULL;
    HRESULT hr;

    if (SUCCEEDED(IUnknown_QueryInterface(frame->frame_content, &IID_IDCompositionSurface, (void **)&surface)))
    {
        struct composition_surface *surface_impl = unsafe_impl_from_IDCompositionSurface(surface);

        if (surface_impl)
        {
            if (surface_impl->physical_surface)
                IUnknown_QueryInterface(surface_impl->physical_surface, &IID_IDXGISurface, (void **)&dxgi_surface);
            if (!dxgi_surface && surface_impl->draw_surface)
                ID3D11Texture2D_QueryInterface(surface_impl->draw_surface, &IID_IDXGISurface, (void **)&dxgi_surface);
        }
        if (!dxgi_surface)
            IUnknown_AddRef(surface); /* hand out the surface itself */
        IDCompositionSurface_Release(surface);
    }
    else if (SUCCEEDED(IUnknown_QueryInterface(frame->frame_content, &IID_IDXGISwapChain1, (void **)&swapchain)))
    {
        /* Composition swapchains skip X11 SwapBuffers in dxgi Present1, so the
         * rendered frame stays in buffer 0 rather than rotating to BufferCount - 1. */
        hr = IDXGISwapChain1_GetBuffer(swapchain, 0, &IID_IDXGISurface, (void **)&dxgi_surface);
        IDXGISwapChain1_Release(swapchain);
        if (FAILED(hr))
            ERR("Failed to get the swapchain buffer 0, hr %#lx.\n", hr);
    }
    else if (SUCCEEDED(IUnknown_QueryInterface(frame->frame_content, &IID_IDXGISwapChain, (void **)&swapchain_v1)))
    {
        hr = IDXGISwapChain_GetBuffer(swapchain_v1, 0, &IID_IDXGISurface, (void **)&dxgi_surface);
        IDXGISwapChain_Release(swapchain_v1);
        if (FAILED(hr))
            ERR("Failed to get the swapchain front buffer, hr %#lx.\n", hr);
    }
    else if (SUCCEEDED(IUnknown_QueryInterface(frame->frame_content, &IID_IDXGISurface, (void **)&dxgi_surface)))
    {
        /* direct IDXGISurface */
    }
    else if (SUCCEEDED(IUnknown_QueryInterface(frame->frame_content, &IID_ID3D11Texture2D, (void **)&texture)))
    {
        ID3D11Texture2D_QueryInterface(texture, &IID_IDXGISurface, (void **)&dxgi_surface);
        ID3D11Texture2D_Release(texture);
    }
    else if (SUCCEEDED(IUnknown_QueryInterface(frame->frame_content, &IID_ID3D11Resource, (void **)&d3d_res)))
    {
        ID3D11Resource_QueryInterface(d3d_res, &IID_IDXGISurface, (void **)&dxgi_surface);
        ID3D11Resource_Release(d3d_res);
    }
    else if (SUCCEEDED(IUnknown_QueryInterface(frame->frame_content, &IID_IDXGIResource, (void **)&dxgi_res)))
    {
        IDXGIResource_QueryInterface(dxgi_res, &IID_IDXGISurface, (void **)&dxgi_surface);
        IDXGIResource_Release(dxgi_res);
    }

    return dxgi_surface;
}

/* Region of the window that this visual occupies: its accumulated offset
 * chain position ('pt') and the size of the content being composited. */
static RECT composite_dst_rect(const struct composite_frame *frame, UINT width, UINT height)
{
    RECT rect;

    SetRect(&rect, frame->pt.x, frame->pt.y,
            frame->pt.x + (LONG)width, frame->pt.y + (LONG)height);
    return rect;
}

static void dcomp_raise_shared_proxy_host(HWND target_hwnd, struct composition_visual *visual)
{
    /* Do NOT lower captured_hwnd (Chrome_WidgetWin_1) below target_hwnd in Win32
     * Z-order: Chromium's WindowOcclusionTrackerWin walks GW_HWNDPREV and marks
     * Chrome_WidgetWin_1 OCCLUDED (freezing rendering at seq=4/6) whenever an
     * opaque window of the same size sits above it. Visual/input pass-through is
     * handled on X11 by winex11.drv apply_window_cloak() (1x1 ShapeBounding +
     * empty ShapeInput + SKIP_TASKBAR). */
    (void)target_hwnd;
    (void)visual;
}

static void render_composite_frame(struct composite_frame *frame)
{
    IDXGISwapChain *swapchain = NULL;
    struct wined3d_swapchain_desc sc_desc;
    struct composition_visual *visual;
    const D2D_RECT_F *clip;
    IDXGISurface *dxgi_surface;
    RECT dst_rect;
    float opacity;
    BOOL culled = FALSE;

    /* The frame holds a reference to the visual, so reading its effect-group
     * opacity here is pure memory; the accumulated target-space clip was
     * resolved during collect_visual_frames. */
    visual = frame->visual;
    opacity = visual ? visual_get_opacity(visual) : 1.0f;
    clip = frame->has_clip ? &frame->target_clip : NULL;

    if (frame->is_fallback)
    {
        TRACE("DCOMP: fallback blitting surface %p to hwnd %p.\n", frame->surface, frame->target_hwnd);
        if (SUCCEEDED(IUnknown_QueryInterface(frame->surface->physical_surface,
                &IID_IDXGISurface, (void **)&dxgi_surface)))
        {
            if (do_composite_dxgi_surface(frame->target, visual, frame->target_hwnd, frame->pt,
                    dxgi_surface, opacity, clip, frame->has_transform, &frame->world_transform, &culled))
                frame->rendered = TRUE;
            else if (culled)
                frame->culled = TRUE;
            IDXGISurface_Release(dxgi_surface);
        }
        return;
    }

    if (visual && visual->is_shared_proxy)
    {
        /* Cross-process shared visual: open the surface the publisher published
         * on this process's device. The window server keys a shared resource by
         * handle, so the texture view wraps the publisher's GPU resource; the
         * composite samples it on the GPU into this target's presentation
         * swapchain. No CPU staging readback on either side. */
        TRACE("DCOMP: shared proxy visual %p handle %p to hwnd %p at %ld,%ld\n", visual,
                visual->shared_content_handle, frame->target_hwnd, frame->pt.x, frame->pt.y);
        if ((dxgi_surface = shared_proxy_open_surface(
                (frame->target && frame->target->device) ? impl_from_IDCompositionDevice(frame->target->device) : NULL,
                visual)))
        {
            if (do_composite_dxgi_surface(frame->target, visual, frame->target_hwnd, frame->pt,
                    dxgi_surface, opacity, clip, frame->has_transform, &frame->world_transform, &culled))
            {
                frame->rendered = TRUE;
                dcomp_raise_shared_proxy_host(frame->target_hwnd, visual);
            }
            else if (culled)
                frame->culled = TRUE;
            IDXGISurface_Release(dxgi_surface);
        }
        return;
    }

    if (!(frame->frame_content = frame->content))
        return;

    /* Taking the references enters the Direct3D device: run outside the critical
     * section. The pointers are alive because the frame holds a reference to the
     * visual that owns them and dcomp defers the release of a replaced content. */
    IUnknown_AddRef(frame->frame_content);
    if ((frame->frame_context = frame->device_context))
        ID2D1DeviceContext_AddRef(frame->frame_context);
    if ((frame->frame_interop = frame->interop))
        ID2D1GdiInteropRenderTarget_AddRef(frame->frame_interop);

    /* Surface or swapchain content: frame_get_surface() extracts the underlying
     * ID3D11Texture2D (wined3d texture) of the IDCompositionSurface /
     * IDXGISwapChain / IDXGISurface directly; the blit samples or copies it on
     * the GPU into the target's presentation swapchain, applying the visual
     * opacity and clip, and presents the visual's region of the window. */
    if ((dxgi_surface = frame_get_surface(frame)))
    {
        if (do_composite_dxgi_surface(frame->target, visual, frame->target_hwnd, frame->pt,
                dxgi_surface, opacity, clip, frame->has_transform, &frame->world_transform, &culled))
            frame->rendered = TRUE;
        else if (culled)
            frame->culled = TRUE;
        IDXGISurface_Release(dxgi_surface);
        if (frame->rendered || frame->culled)
            goto done;
    }

    if (frame->visual && frame->visual->wined3d_swapchain && opacity == 1.0f && !clip
            && !frame->has_transform && frame->pt.x >= 0 && frame->pt.y >= 0)
    {
        struct wined3d_swapchain *wined3d_swapchain = frame->visual->wined3d_swapchain;
        RECT *dst_rect_ptr = NULL;

        if (frame->target_hwnd && IsWindow(frame->target_hwnd))
            wined3d_swapchain_set_window(wined3d_swapchain, frame->target_hwnd);
        wined3d_swapchain_get_desc(wined3d_swapchain, &sc_desc);
        if (sc_desc.backbuffer_width && sc_desc.backbuffer_height)
        {
            dst_rect = composite_dst_rect(frame, sc_desc.backbuffer_width, sc_desc.backbuffer_height);
            dst_rect_ptr = &dst_rect;
        }
        TRACE("DCOMP: present path A ws %p hwnd %p dst %ld,%ld-%ldx%ld\n", wined3d_swapchain,
                frame->target_hwnd, dst_rect.left, dst_rect.top,
                dst_rect.right - dst_rect.left, dst_rect.bottom - dst_rect.top);
        wined3d_swapchain_present(wined3d_swapchain, NULL, dst_rect_ptr, frame->target_hwnd, 0, 0);
        frame->rendered = TRUE;
        goto done;
    }
    else if (opacity == 1.0f && !clip && !frame->has_transform
            && frame->pt.x >= 0 && frame->pt.y >= 0
            && SUCCEEDED(IUnknown_QueryInterface(frame->frame_content, &IID_IDXGISwapChain, (void **)&swapchain)))
    {
        struct wined3d_swapchain *wined3d_swapchain = dxgi_get_wined3d_swapchain(swapchain);
        RECT *dst_rect_ptr = NULL;

        if (wined3d_swapchain)
        {
            if (frame->target_hwnd && IsWindow(frame->target_hwnd))
                wined3d_swapchain_set_window(wined3d_swapchain, frame->target_hwnd);
            wined3d_swapchain_get_desc(wined3d_swapchain, &sc_desc);
            if (sc_desc.backbuffer_width && sc_desc.backbuffer_height)
            {
                dst_rect = composite_dst_rect(frame, sc_desc.backbuffer_width, sc_desc.backbuffer_height);
                dst_rect_ptr = &dst_rect;
            }
            TRACE("DCOMP: present path B ws %p hwnd %p dst %ld,%ld-%ldx%ld\n", wined3d_swapchain,
                    frame->target_hwnd, dst_rect.left, dst_rect.top,
                    dst_rect.right - dst_rect.left, dst_rect.bottom - dst_rect.top);
            wined3d_swapchain_present(wined3d_swapchain, NULL, dst_rect_ptr, frame->target_hwnd, 0, 0);
            frame->rendered = TRUE;
        }
        IDXGISwapChain_Release(swapchain);
        if (frame->rendered)
            goto done;
    }

done:
    if (frame->frame_interop)
        ID2D1GdiInteropRenderTarget_Release(frame->frame_interop);
    if (frame->frame_context)
        ID2D1DeviceContext_Release(frame->frame_context);
    IUnknown_Release(frame->frame_content);
    frame->frame_content = NULL;
}

/* Runs with dcomp_cs released: dropping the visual reference can release its
 * content, which enters Direct3D and GDI. The content, context and interop
 * references taken for the render pass are already dropped by the renderer. */
static void composite_frame_cleanup(struct composite_frame *frame)
{
    if (frame->visual)
        IDCompositionVisual_Release((IDCompositionVisual *)&frame->visual->IDCompositionVisualUnknown_iface);
    if (frame->surface)
        IDCompositionSurfaceUnknown_Release(&frame->surface->IDCompositionSurfaceUnknown_iface);
    frame->visual = NULL;
    frame->surface = NULL;
}

static void target_snapshot_cleanup(struct target_snapshot *snap)
{
    if (snap->container)
        IDCompositionVisual_Release((IDCompositionVisual *)&snap->container->IDCompositionVisualUnknown_iface);
    if (snap->host_target)
        IDCompositionTarget_Release(&snap->host_target->IDCompositionTarget_iface);
    if (snap->root)
        IDCompositionVisual_Release((IDCompositionVisual *)&snap->root->IDCompositionVisualUnknown_iface);
    IDCompositionTarget_Release(&snap->target->IDCompositionTarget_iface);
    snap->container = NULL;
    snap->host_target = NULL;
    snap->root = NULL;
    snap->target = NULL;
}

static ULONGLONG qpc_now(void)
{
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return (ULONGLONG)now.QuadPart;
}

static unsigned int qpc_elapsed_ms(ULONGLONG since, LARGE_INTEGER freq)
{
    ULONGLONG now;
    if (!since || !freq.QuadPart)
        return ~0u;
    now = qpc_now();
    if (now <= since)
        return 0;
    return (unsigned int)(((now - since) * 1000ULL) / (ULONGLONG)freq.QuadPart);
}

/*
 * Vertical-blank gate for the composite tick. D3DKMTWaitForVerticalBlankEvent
 * is the real cadence source: it blocks until the display's next vblank. Under
 * the winex11 backend d3d11 exports it as a stub that returns immediately, so
 * measure the wait: when it did not gate, emulate one refresh interval with a
 * sleep. The adapter and device are opened once per composition device (a
 * kernel handle, no window server round trip).
 */
static void composite_wait_vblank(struct composition_device *device, unsigned int refresh_period)
{
    static pfn_wait_vblank pfn;
    static BOOL probed;
    ULONGLONG start;
    LONGLONG elapsed_us;

    if (!device->vblank_valid)
    {
        D3DKMT_OPENADAPTERFROMHDC open_desc;
        D3DKMT_CREATEDEVICE create_desc;
        HDC screen_dc = GetDC(0);

        device->vblank_valid = TRUE; /* probe once; failure = emulate forever */
        memset(&open_desc, 0, sizeof(open_desc));
        memset(&create_desc, 0, sizeof(create_desc));
        if (screen_dc)
        {
            open_desc.hDc = screen_dc;
            if (!D3DKMTOpenAdapterFromHdc(&open_desc))
            {
                create_desc.hAdapter = open_desc.hAdapter;
                if (!D3DKMTCreateDevice(&create_desc))
                    device->vblank_device = create_desc.hDevice;
            }
            ReleaseDC(0, screen_dc);
        }
    }

    if (!probed) /* d3d11's export, then the win32u syscall it forwards to */
    {
        static const WCHAR d3d11W[] = L"d3d11.dll", win32uW[] = L"win32u.dll";
        HMODULE mod = LoadLibraryW(d3d11W);
        probed = TRUE;
        if (mod)
            pfn = (void *)GetProcAddress(mod, "D3DKMTWaitForVerticalBlankEvent");
        if (!pfn && (mod = GetModuleHandleW(win32uW)))
            pfn = (void *)GetProcAddress(mod, "NtGdiDdDDIWaitForVerticalBlankEvent");
    }

    start = qpc_now();
    if (device->vblank_device && pfn)
    {
        D3DKMT_WAITFORVERTICALBLANKEVENT evt;

        evt.hDevice = device->vblank_device;
        pfn(&evt);
    }

    elapsed_us = device->qpc_freq.QuadPart ? (LONGLONG)((qpc_now() - start) * 1000000ULL
            / (ULONGLONG)device->qpc_freq.QuadPart) : 0;
    if (elapsed_us < (LONGLONG)refresh_period * 500) /* stub: emulate the vblank interval */
        Sleep(refresh_period - (unsigned int)(elapsed_us / 1000));
}

static BOOL visual_tree_check_swapchain(const struct composition_visual *visual, unsigned int depth,
        BOOL *has_swapchain)
{
    const struct composition_visual *child;
    BOOL updated = FALSE;

    if (!visual || depth > 256)
        return FALSE;

    if (visual->content && visual->wined3d_swapchain)
    {
        UINT present_count = 0;
        unsigned int content_ver = visual->content_version;
        *has_swapchain = TRUE;
        if (SUCCEEDED(IDXGISwapChain_GetLastPresentCount((IDXGISwapChain *)visual->content, &present_count)))
            content_ver += present_count + 1;
        if (content_ver != visual->composited_version)
            updated = TRUE;
    }

    LIST_FOR_EACH_ENTRY(child, &visual->child_visuals, const struct composition_visual, entry)
    {
        if (visual_tree_check_swapchain(child, depth + 1, has_swapchain))
            updated = TRUE;
    }
    return updated;
}

static BOOL device_check_swapchains(struct composition_device *device, BOOL *has_swapchain)
{
    struct composition_target *target;
    BOOL updated = FALSE;

    *has_swapchain = FALSE;
    dcomp_lock();
    LIST_FOR_EACH_ENTRY(target, &device->targets, struct composition_target, entry)
    {
        RECT rc;
        if (target->hwnd && IsWindow(target->hwnd))
        {
            BOOL vis = IsWindowVisible(target->hwnd) && !IsIconic(target->hwnd);
            if (vis && !target->last_visible)
            {
                target->needs_redraw = TRUE;
                InterlockedIncrement(&dcomp_tree_version);
                updated = TRUE;
            }
            target->last_visible = vis;
        }
        if (target->hwnd && IsWindow(target->hwnd) && GetClientRect(target->hwnd, &rc)
                && (rc.right != target->last_client_rect.right
                    || rc.bottom != target->last_client_rect.bottom))
        {
            target->last_client_rect = rc;
            InterlockedIncrement(&dcomp_tree_version);
            updated = TRUE;
        }
        if (target->root && visual_tree_check_swapchain(impl_from_IDCompositionVisual(target->root), 0, has_swapchain))
            updated = TRUE;
    }
    dcomp_unlock();
    return updated;
}

static BOOL composite_device(struct composition_device *device)
{
    struct composition_target *current_target;
    LONG tree_ver, xform_ver;
    BOOL has_shared = FALSE, has_cross_process_shared = FALSE, any_needs_hwnd = FALSE, has_swapchain = FALSE, sc_updated;
    unsigned int count = 0, i;

    /* Objects released by the application's dcomp calls since the previous tick
     * are safe to drop now: no frame of that tick is in flight any more. */
    dcomp_reclaim_drain();

    sc_updated = device_check_swapchains(device, &has_swapchain);
    device->last_has_swapchain = has_swapchain;

    if (!device->committed && !device->last_has_shared)
    {
        struct composition_target *t;
        BOOL any_shared = FALSE;

        dcomp_lock();
        LIST_FOR_EACH_ENTRY(t, &device->targets, struct composition_target, entry)
        {
            if (t->shared_visual_handle || (t->root && impl_from_IDCompositionVisual(t->root)->shared_visual_handle))
            {
                any_shared = TRUE;
                break;
            }
        }
        dcomp_unlock();
        if (!any_shared)
            return FALSE;
    }

    tree_ver = InterlockedCompareExchange(&dcomp_tree_version, 0, 0);
    xform_ver = InterlockedCompareExchange(&dcomp_transform_version, 0, 0);
    if (!sc_updated && !device->last_has_shared && device->last_tree_version != 0
            && device->last_tree_version == tree_ver
            && device->last_xform_version == xform_ver)
    {
        return FALSE;
    }

    snapshot_device_targets(device, &has_shared);
    if (has_shared)
        has_cross_process_shared = warm_shared_visual_cache(device);
    resolve_target_windows(device, &any_needs_hwnd);
    if (any_needs_hwnd)
    {
        find_target_containers(device);
        validate_target_windows(device);
    }
    collect_device_frames(device, &tree_ver, &xform_ver);

    /* Frames are queued target by target (collect_device_frames walks the
     * snapshots), so a pass opens on the first frame of a target and closes
     * on the last: one clear, N quad draws, one present per target. */
    current_target = NULL;
    for (i = 0; i < device->frame_buf_count; i++)
    {
        struct composite_frame *frame = &device->frame_buf[i];

        if (frame->target != current_target)
        {
            if (current_target)
                composite_target_end(current_target);
            current_target = frame->target;
            composite_target_begin(current_target);
        }
        render_composite_frame(frame);
        if (frame->rendered)
            count++;
    }
    if (current_target)
        composite_target_end(current_target);

    dcomp_lock();
    for (i = 0; i < device->frame_buf_count; i++)
    {
        struct composite_frame *frame = &device->frame_buf[i];
        if (!frame->rendered && !frame->culled)
            continue;
        if (frame->is_fallback)
            frame->target->composited_surface = frame->surface;
        else
        {
            frame->visual->composited_version = frame->version;
            frame->visual->composited_transform_version = frame->transform_version;
            frame->visual->composited_hwnd = frame->target_hwnd;
        }
    }
    dcomp_unlock();

    for (i = 0; i < device->frame_buf_count; i++)
        composite_frame_cleanup(&device->frame_buf[i]);
    device->frame_buf_count = 0;

    for (i = 0; i < device->snap_buf_count; i++)
        target_snapshot_cleanup(&device->snap_buf[i]);
    device->snap_buf_count = 0;
    device->handle_buf_count = 0;

    device->last_tree_version = tree_ver;
    device->last_xform_version = xform_ver;
    device->last_has_shared = has_cross_process_shared || any_needs_hwnd;
    dcomp_reclaim_drain();

    if (count)
    {
        LONG total = InterlockedIncrement(&device->composite_count);
        TRACE("DCOMP: composited %u frames (composite_count %ld).\n", count, total);
        return TRUE;
    }
    return FALSE;
}

static HWND g_dcomp_captured_top_hwnd;
static void dcomp_enforce_captured_toolwindow(HWND top);
static BOOL CALLBACK dcomp_enforce_own_captured_cb(HWND hwnd, LPARAM lparam);

static DWORD WINAPI composite_thread_proc(void *iface)
{
    struct composition_device *device = impl_from_IDCompositionDevice(iface);
    unsigned int frequency, refresh_period, tick = 0, idle_ticks = 0;
    HDC hdc;

    /* Refresh interval for the emulated-vblank fallback of composite_wait_vblank(). */
    hdc = GetDC(0);
    frequency = hdc ? GetDeviceCaps(hdc, VREFRESH) : 0;
    if (frequency <= 1)
        frequency = 60;
    refresh_period = 1000 / frequency;
    if (refresh_period == 0)
        refresh_period = 16;
    if (hdc)
        ReleaseDC(0, hdc);
    device->refresh_period_ms = refresh_period;

    TRACE("DCOMP: composite_thread_proc started, refresh_period %u ms\n", refresh_period);

    while (!device->exit_thread)
    {
        unsigned int elapsed = qpc_elapsed_ms(device->last_composite_qpc, device->qpc_freq);
        BOOL has_sc = FALSE;
        BOOL sc_updated = device->committed && device_check_swapchains(device, &has_sc);
        device->last_has_swapchain = has_sc;

        /* Always poll shared-visual targets for updated cross-process surfaces
         * (e.g. resized Chromium swapchains published via __wine_dcomp_hwnd_surface). */
        {
            struct composition_target *st;
            struct { HANDLE handle; IDCompositionVisual *root; struct composition_target *t; } to_pub[8];
            int n_pub = 0, pi;
            dcomp_lock();
            LIST_FOR_EACH_ENTRY(st, &device->targets, struct composition_target, entry)
            {
                if (st->shared_visual_handle && st->root && n_pub < (int)ARRAY_SIZE(to_pub))
                {
                    to_pub[n_pub].handle = st->shared_visual_handle;
                    to_pub[n_pub].root = st->root;
                    to_pub[n_pub].t = st;
                    IDCompositionTarget_AddRef(&st->IDCompositionTarget_iface);
                    IDCompositionVisual_AddRef(st->root);
                    n_pub++;
                }
            }
            dcomp_unlock();
            for (pi = 0; pi < n_pub; pi++)
            {
                HANDLE cur_surf = NULL;
                UINT cw = 0, ch = 0;
                visual_publish_shared_content(impl_from_IDCompositionVisual(to_pub[pi].root), &cur_surf, &cw, &ch);
                if (cur_surf && cur_surf != to_pub[pi].t->last_published_surface)
                {
                    to_pub[pi].t->last_published_surface = cur_surf;
                    shared_visual_target_set_root(to_pub[pi].handle, to_pub[pi].root);
                }
                IDCompositionVisual_Release(to_pub[pi].root);
                IDCompositionTarget_Release(&to_pub[pi].t->IDCompositionTarget_iface);
            }
            if (g_dcomp_captured_top_hwnd && IsWindow(g_dcomp_captured_top_hwnd))
                dcomp_enforce_captured_toolwindow(g_dcomp_captured_top_hwnd);
            else if (n_pub > 0)
                EnumWindows(dcomp_enforce_own_captured_cb, (LPARAM)GetCurrentProcessId());
        }

        if (!device->commit_pending && !device->last_has_shared && !sc_updated)
        {
            DWORD wait_ms;

            if (!has_sc)
                idle_ticks++;
            else
                idle_ticks = 0;

            if (!has_sc && idle_ticks >= 3)
                wait_ms = 250;
            else
                wait_ms = (elapsed < refresh_period && refresh_period - elapsed > 0)
                        ? refresh_period - elapsed : refresh_period;

            if (device->wake_event)
                WaitForSingleObject(device->wake_event, wait_ms);
            else
                Sleep(wait_ms);
            if (device->exit_thread)
                break;
            sc_updated = device->committed && device_check_swapchains(device, &has_sc);
            device->last_has_swapchain = has_sc;
            if (device->commit_pending || device->last_has_shared || sc_updated || has_sc)
                idle_ticks = 0;
            else if (idle_ticks >= 3 && !InterlockedCompareExchange(&g_compositor_clock_handle_count, 0, 0))
            {
                struct composition_target *st;
                struct { HANDLE handle; IDCompositionVisual *root; struct composition_target *t; } to_pub[8];
                int n_pub = 0, pi;
                dcomp_lock();
                LIST_FOR_EACH_ENTRY(st, &device->targets, struct composition_target, entry)
                {
                    if (st->shared_visual_handle && st->root && n_pub < (int)ARRAY_SIZE(to_pub))
                    {
                        to_pub[n_pub].handle = st->shared_visual_handle;
                        to_pub[n_pub].root = st->root;
                        to_pub[n_pub].t = st;
                        IDCompositionTarget_AddRef(&st->IDCompositionTarget_iface);
                        IDCompositionVisual_AddRef(st->root);
                        n_pub++;
                    }
                }
                dcomp_unlock();
                for (pi = 0; pi < n_pub; pi++)
                {
                    HANDLE cur_surf = NULL;
                    UINT cw = 0, ch = 0;
                    visual_publish_shared_content(impl_from_IDCompositionVisual(to_pub[pi].root), &cur_surf, &cw, &ch);
                    if (cur_surf && cur_surf != to_pub[pi].t->last_published_surface)
                    {
                        to_pub[pi].t->last_published_surface = cur_surf;
                        shared_visual_target_set_root(to_pub[pi].handle, to_pub[pi].root);
                    }
                    IDCompositionVisual_Release(to_pub[pi].root);
                    IDCompositionTarget_Release(&to_pub[pi].t->IDCompositionTarget_iface);
                }
                continue;
            }
        }
        else
        {
            idle_ticks = 0;
            if (elapsed < refresh_period)
            {
                DWORD rem = refresh_period - elapsed;
                Sleep(rem > 0 ? rem : 1);
                continue;
            }
        }

        /* Present/vblank cadence. Reset the manual-reset compositor clock, do the
         * frame, then broadcast it, so a caller that arrived for this frame is
         * released when the frame lands (a caller arriving between frames returns
         * immediately, at most one frame early). A real compositor raises the
         * commit-completion alongside each presented frame, so pulse it from the
         * same tick: the CoreMessaging lifted-input workers that parked on the
         * commit-completion event then advance on the present cadence instead of
         * only when the app calls into device_Commit / WaitForCommitCompletion. */
        if ((device->commit_pending || device->last_has_shared || sc_updated)
                && TryEnterCriticalSection(&composite_render_cs))
        {
            if (qpc_elapsed_ms(device->last_composite_qpc, device->qpc_freq) >= refresh_period)
            {
                BOOL had_pending;

                composite_wait_vblank(device, refresh_period);
                had_pending = InterlockedExchange(&device->commit_pending, FALSE);
                if (g_compositor_clock_event)
                    ResetEvent(g_compositor_clock_event);
                if (composite_device(device) || had_pending || device->last_has_shared)
                {
                    device->last_composite_qpc = qpc_now();
                    idle_ticks = 0;
                }
                if (g_compositor_clock_event)
                    SetEvent(g_compositor_clock_event);
            }
            LeaveCriticalSection(&composite_render_cs);
        }
        else if (g_compositor_clock_event)
        {
            SetEvent(g_compositor_clock_event);
        }
        if (device->self_release)
            break;
        /* Throttle the completion-side pulses to ~30Hz (every 2nd present tick).
         * A full-60Hz commit-completion pulse drives the composition churn so hard
         * the WebView2 browser tears down early (ShowWindow climbs, LRPC drain is
         * truncated). Every 2nd tick keeps the lifted-input workers advancing on the
         * present cadence while leaving the browser enough lifetime to issue further
         * LRPC requests on the composition plane. */
        if (!(++tick % 2))
        {
            LONG k;
            struct composition_target *st;
            struct { HANDLE handle; IDCompositionVisual *root; struct composition_target *t; } to_pub[8];
            int n_pub = 0, pi;

            dcomp_lock();
            LIST_FOR_EACH_ENTRY(st, &device->targets, struct composition_target, entry)
            {
                if (st->shared_visual_handle && st->root && n_pub < (int)ARRAY_SIZE(to_pub))
                {
                    to_pub[n_pub].handle = st->shared_visual_handle;
                    to_pub[n_pub].root = st->root;
                    to_pub[n_pub].t = st;
                    IDCompositionTarget_AddRef(&st->IDCompositionTarget_iface);
                    IDCompositionVisual_AddRef(st->root);
                    n_pub++;
                }
            }
            for (k = 0; k < g_compositor_clock_handle_count; )
            {
                if (!g_compositor_clock_handles[k])
                {
                    memmove(&g_compositor_clock_handles[k], &g_compositor_clock_handles[k + 1],
                            (g_compositor_clock_handle_count - k - 1) * sizeof(g_compositor_clock_handles[0]));
                    g_compositor_clock_handles[--g_compositor_clock_handle_count] = NULL;
                    continue;
                }
                if (!SetEvent(g_compositor_clock_handles[k]) && GetLastError() == ERROR_INVALID_HANDLE)
                {
                    CloseHandle(g_compositor_clock_handles[k]);
                    memmove(&g_compositor_clock_handles[k], &g_compositor_clock_handles[k + 1],
                            (g_compositor_clock_handle_count - k - 1) * sizeof(g_compositor_clock_handles[0]));
                    g_compositor_clock_handles[--g_compositor_clock_handle_count] = NULL;
                    continue;
                }
                k++;
            }
            dcomp_unlock();
            for (pi = 0; pi < n_pub; pi++)
            {
                HANDLE cur_surf = NULL;
                UINT cw = 0, ch = 0;
                visual_publish_shared_content(impl_from_IDCompositionVisual(to_pub[pi].root), &cur_surf, &cw, &ch);
                if (cur_surf && cur_surf != to_pub[pi].t->last_published_surface)
                {
                    to_pub[pi].t->last_published_surface = cur_surf;
                    shared_visual_target_set_root(to_pub[pi].handle, to_pub[pi].root);
                }
                IDCompositionVisual_Release(to_pub[pi].root);
                IDCompositionTarget_Release(&to_pub[pi].t->IDCompositionTarget_iface);
            }
            if (g_commit_completion_event)
                SetEvent(g_commit_completion_event);
            /* gate104 lever (c): hand the UI thread one pump beat per
             * commit-completion pulse.  With the cadence wave the app reaches the
             * window-setup states (ShowWindow 12) and stops issuing LRPC reads:
             * the lifted-input continuation wants a message-loop beat to re-enter
             * the idle receive (request 7 = composition-input adapter).  The XAML
             * host window class in WinUI3 is L"Window"; posting WM_NULL is the
             * minimal real message: it wakes GetMessage and lets rpcrt4's
             * completion queue drain on the app's own pump. */
            {
                static HWND bridge;
                if (!bridge) bridge = FindWindowW( L"Window", NULL );
                if (!bridge) bridge = FindWindowW( L"Windows.UI.Core.CoreWindow", NULL );
                if (bridge) PostMessageW( bridge, WM_NULL, 0, 0 );
            }
        }
    }

    TRACE("DCOMP: composite_thread_proc exiting.\n");
    InterlockedExchange(&device->thread_exited, TRUE);
    if (device->self_release)
    {
        if (device->thread)
        {
            CloseHandle(device->thread);
            device->thread = NULL;
        }
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
    return 0;
}

void ensure_composite_thread(struct composition_device *device)
{
    HANDLE thread = NULL;
    BOOL start, published = FALSE;

    dcomp_lock();
    if (device->thread && !device->thread_exited)
    {
        dcomp_unlock();
        return;
    }

    /* Hand over a running thread and ask it to stop. The join runs with the
     * critical section released: the composite thread takes the critical section
     * on every tick, so joining while holding it waits for a thread that is
     * itself waiting for the lock. */
    if (device->thread)
    {
        InterlockedExchange(&device->exit_thread, TRUE);
        if (device->wake_event)
            SetEvent(device->wake_event);
        thread = device->thread;
        device->thread = NULL;
    }
    InterlockedExchange(&device->thread_exited, FALSE);
    dcomp_unlock();

    if (thread)
    {
        WaitForSingleObject(thread, INFINITE);
        CloseHandle(thread);
    }

    dcomp_lock();
    start = !device->thread;
    dcomp_unlock();

    if (start)
    {
        /* The present clock exists for the lifetime of the process once the first
         * device starts compositing; DCompositionWaitForCompositorClock paces on it
         * when it is present. Manual-reset so a frame tick is broadcast to every
         * compositor-clock waiter, not just one. */
        if (!g_compositor_clock_event)
            g_compositor_clock_event = CreateEventW( NULL, TRUE, FALSE, NULL );

        /* Starting a thread reaches the PE loader: ntdll logs the thread creation
         * event under the loader lock. A thread that is inside DllMain owns that
         * lock and reaches this code (the DirectComposition device of the
         * composition client is created from a DllMain), so starting the
         * composite thread with the device critical section held is the AB-BA
         * cycle that freezes the process. Start it with the lock released and
         * publish the handle under the lock; a thread that lost the race is asked
         * to exit and closed. */
        thread = CreateThread(NULL, 0, composite_thread_proc,
                &device->IDCompositionDevice_iface, CREATE_SUSPENDED, NULL);

        dcomp_lock();
        if (!device->thread)
        {
            device->thread = thread;
            published = TRUE;
        }
        TRACE("DCOMP: started composite_thread_proc %p\n", device->thread);
        dcomp_unlock();

        /* The thread is created suspended so that the publish above happens
         * before it runs; a starter that lost the race cancels its own thread,
         * which has not executed a single instruction. */
        if (published) ResumeThread( thread );
        else if (thread)
        {
            TerminateThread( thread, 0 );
            CloseHandle( thread );
        }
    }
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

    EnterCriticalSection(&composite_render_cs);
    if (qpc_elapsed_ms(device->last_composite_qpc, device->qpc_freq) >= device->refresh_period_ms)
    {
        InterlockedExchange(&device->commit_pending, FALSE);
        if (composite_device(device))
            device->last_composite_qpc = qpc_now();
        if (g_compositor_clock_event)
            SetEvent(g_compositor_clock_event);
        if (g_commit_completion_event)
            SetEvent(g_commit_completion_event);
    }
    else
    {
        InterlockedExchange(&device->commit_pending, TRUE);
    }
    LeaveCriticalSection(&composite_render_cs);
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

static HRESULT get_default_rendering_device(IUnknown **out_device)
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

/* Surface of a shared visual published by another process.
 *
 * The publisher's content is a texture in the GPU's memory; a shared resource
 * is named by a handle that the window server resolves for any process, so
 * opening it on a device of this process yields a view of the same resource.
 * The device is created once per process and kept: shared-resource views are
 * not device specific, and creating one per frame would cost a device. */
static HRESULT device_get_or_create_rendering_device(struct composition_device *device,
        IUnknown **out_rendering_device);

IDXGISurface *shared_proxy_open_surface(struct composition_device *comp_dev, struct composition_visual *visual)
{
    static ID3D11Device *fallback_device;
    ID3D11Device *device = NULL;
    IUnknown *rendering_device = NULL;
    IDXGISurface *surface = NULL;
    HRESULT hr;

    if (!visual->shared_content_handle)
        return NULL;

    if (comp_dev && SUCCEEDED(device_get_or_create_rendering_device(comp_dev, &rendering_device)) && rendering_device)
    {
        IUnknown_QueryInterface(rendering_device, &IID_ID3D11Device, (void **)&device);
        IUnknown_Release(rendering_device);
    }
    if (!device)
    {
        if (!fallback_device)
        {
            if (FAILED(get_default_rendering_device(&rendering_device)) || !rendering_device)
                return NULL;
            fallback_device = (ID3D11Device *)rendering_device;
        }
        device = fallback_device;
        ID3D11Device_AddRef(device);
    }

    if (visual->shm_kmt_handle != visual->shared_content_handle || !visual->shm_ptr)
    {
        WCHAR shm_name[64];
        if (visual->shm_ptr) { UnmapViewOfFile(visual->shm_ptr); visual->shm_ptr = NULL; }
        if (visual->shm_map) { CloseHandle(visual->shm_map); visual->shm_map = NULL; }
        visual->shm_kmt_handle = visual->shared_content_handle;
        visual->shm_last_seq = 0;
        swprintf(shm_name, ARRAY_SIZE(shm_name), L"Local\\wine_dcomp_shm_%08x",
                (UINT)(ULONG_PTR)visual->shared_content_handle);
        if ((visual->shm_map = OpenFileMappingW(FILE_MAP_READ, FALSE, shm_name)))
            visual->shm_ptr = MapViewOfFile(visual->shm_map, FILE_MAP_READ, 0, 0, 0);
    }

    if (visual->shm_ptr)
    {
        const UINT *hdr = visual->shm_ptr;
        UINT s1 = hdr[3];
        MemoryBarrier();
        if (s1 && !(s1 & 1))
        {
            UINT w = hdr[0], h = hdr[1], stride = hdr[2];
            UINT magic = hdr[4];
            if (magic == 0x5A434750u && w > 0 && h > 0 && w <= 4096 && h <= 2160)
            {
                DWORD pid = hdr[5];
                HANDLE remote_h = (HANDLE)(ULONG_PTR)hdr[6];
                UINT alloc_sz = hdr[7];

                if (pid && remote_h && alloc_sz)
                {
                    if (!visual->zc_tex || visual->zc_pid != pid || visual->zc_remote_handle != remote_h
                            || visual->zc_w != w || visual->zc_h != h || visual->zc_alloc_size != alloc_sz)
                    {
                        HANDLE src_proc = NULL, local_h = NULL;
                        ID3D11Texture2D *ztex = NULL;

                        if (visual->zc_tex)
                        {
                            ID3D11Texture2D_Release(visual->zc_tex);
                            visual->zc_tex = NULL;
                        }
                        if (pid == GetCurrentProcessId())
                        {
                            DuplicateHandle(GetCurrentProcess(), remote_h, GetCurrentProcess(), &local_h,
                                    0, FALSE, DUPLICATE_SAME_ACCESS);
                        }
                        else if ((src_proc = OpenProcess(PROCESS_DUP_HANDLE, FALSE, pid)))
                        {
                            DuplicateHandle(src_proc, remote_h, GetCurrentProcess(), &local_h,
                                    0, FALSE, DUPLICATE_SAME_ACCESS);
                            CloseHandle(src_proc);
                        }

                        if (local_h)
                        {
                            D3D11_TEXTURE2D_DESC tdesc = {0};
                            tdesc.Width = w;
                            tdesc.Height = h;
                            tdesc.MipLevels = 1;
                            tdesc.ArraySize = 1;
                            tdesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
                            tdesc.SampleDesc.Count = 1;
                            tdesc.Usage = D3D11_USAGE_DEFAULT;
                            tdesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                            if (SUCCEEDED(ID3D11Device_CreateTexture2D(device, &tdesc, NULL, &ztex)))
                            {
                                if (SUCCEEDED(dxgi_bind_external_memory((IUnknown *)ztex, local_h, alloc_sz)))
                                {
                                    visual->zc_tex = ztex;
                                    visual->zc_pid = pid;
                                    visual->zc_remote_handle = remote_h;
                                    visual->zc_w = w;
                                    visual->zc_h = h;
                                    visual->zc_alloc_size = alloc_sz;
                                    visual->size.cx = (LONG)w;
                                    visual->size.cy = (LONG)h;
                                    TRACE("DCOMP: Imported zero-copy GPU texture %ux%u (pid %lu, handle %p, size %u)\n",
                                            w, h, pid, remote_h, alloc_sz);
                                }
                                else
                                {
                                    ID3D11Texture2D_Release(ztex);
                                }
                            }
                            CloseHandle(local_h);
                        }
                    }

                    if (visual->zc_tex)
                    {
                        MemoryBarrier();
                        if (hdr[3] == s1)
                            visual->shm_last_seq = s1;
                        hr = ID3D11Texture2D_QueryInterface(visual->zc_tex, &IID_IDXGISurface, (void **)&surface);
                        ID3D11Device_Release(device);
                        return SUCCEEDED(hr) ? surface : NULL;
                    }
                }
            }
            if (magic != 0x5A434750u && w > 0 && h > 0 && w <= 4096 && h <= 2160 && stride >= w * 4)
            {
                if (!visual->shm_tex || visual->shm_tex_w != w || visual->shm_tex_h != h)
                {
                    D3D11_TEXTURE2D_DESC tdesc = {0};
                    if (visual->shm_tex)
                    {
                        ID3D11Texture2D_Release(visual->shm_tex);
                        visual->shm_tex = NULL;
                    }
                    tdesc.Width = w;
                    tdesc.Height = h;
                    tdesc.MipLevels = 1;
                    tdesc.ArraySize = 1;
                    tdesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
                    tdesc.SampleDesc.Count = 1;
                    tdesc.Usage = D3D11_USAGE_DEFAULT;
                    tdesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                    if (SUCCEEDED(ID3D11Device_CreateTexture2D(device, &tdesc, NULL, &visual->shm_tex)))
                    {
                        visual->shm_tex_w = w;
                        visual->shm_tex_h = h;
                        visual->size.cx = (LONG)w;
                        visual->size.cy = (LONG)h;
                    }
                }
                if (visual->shm_tex && s1 != visual->shm_last_seq)
                {
                    ID3D11DeviceContext *ctx = NULL;
                    ID3D11Device_GetImmediateContext(device, &ctx);
                    if (ctx)
                    {
                        ID3D11DeviceContext_UpdateSubresource(ctx, (ID3D11Resource *)visual->shm_tex,
                                0, NULL, (const BYTE *)hdr + 32, stride, 0);
                        ID3D11DeviceContext_Release(ctx);
                    }
                    MemoryBarrier();
                    if (hdr[3] == s1)
                        visual->shm_last_seq = s1;
                }
            }
        }
        if (visual->shm_tex)
        {
            hr = ID3D11Texture2D_QueryInterface(visual->shm_tex, &IID_IDXGISurface, (void **)&surface);
            ID3D11Device_Release(device);
            return SUCCEEDED(hr) ? surface : NULL;
        }
        ID3D11Device_Release(device);
        return NULL;
    }

    /* Producer has not created its SHM mapping yet; wait for first Present instead
     * of opening a blank unlinked cross-process D3D11 texture. */
    ID3D11Device_Release(device);
    return NULL;
}

static HRESULT device_get_or_create_rendering_device(struct composition_device *device,
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

static inline BOOL is_kmt_handle(HANDLE h)
{
    return ((ULONG_PTR)h & 0xc0000000) != 0;
}

static HRESULT STDMETHODCALLTYPE device_CreateSurfaceFromHandle(IDCompositionDevice *iface,
        HANDLE handle, IUnknown **surface)
{
    struct composition_device *device = impl_from_IDCompositionDevice(iface);
    IUnknown *rendering_device = NULL;
    ID3D11Device *d3d11_device = NULL;
    ID3D11Device1 *d3d11_device1 = NULL;
    IDXGISurface *dxgi_surface = NULL;
    HRESULT hr;

    TRACE("DCOMP: device_CreateSurfaceFromHandle iface %p, handle %p, surface %p\n", iface, handle, surface);

    if (!surface)
        return E_POINTER;
    *surface = NULL;

    if (!handle)
        return E_INVALIDARG;

    hr = device_get_or_create_rendering_device(device, &rendering_device);
    if (FAILED(hr) || !rendering_device)
        return hr;

    if (SUCCEEDED(IUnknown_QueryInterface(rendering_device, &IID_ID3D11Device1, (void **)&d3d11_device1)))
    {
        if (is_kmt_handle(handle))
            hr = ID3D11Device_OpenSharedResource((ID3D11Device *)d3d11_device1, handle, &IID_IDXGISurface, (void **)&dxgi_surface);
        else
            hr = ID3D11Device1_OpenSharedResource1(d3d11_device1, handle, &IID_IDXGISurface, (void **)&dxgi_surface);
        ID3D11Device1_Release(d3d11_device1);
    }
    else if (SUCCEEDED(IUnknown_QueryInterface(rendering_device, &IID_ID3D11Device, (void **)&d3d11_device)))
    {
        hr = ID3D11Device_OpenSharedResource(d3d11_device, handle, &IID_IDXGISurface, (void **)&dxgi_surface);
        ID3D11Device_Release(d3d11_device);
    }
    else
    {
        /* A rendering device that is not an ID3D11Device (an IDXGIDevice or ID2D1Device
         * from a client that never passed a D3D11 device) has no shared-resource
         * opening entry point at all; report that instead of succeeding with a NULL
         * surface. */
        hr = E_NOINTERFACE;
    }
    IUnknown_Release(rendering_device);

    if (FAILED(hr) || !dxgi_surface)
    {
        TRACE("DCOMP: Failed to open shared resource handle %p, hr %#lx\n", handle, hr);
        return hr;
    }

    hr = create_surface_from_dxgi_surface(device, dxgi_surface, surface);
    IDXGISurface_Release(dxgi_surface);
    return hr;
}

static BOOL CALLBACK dcomp_enforce_own_captured_cb(HWND hwnd, LPARAM lparam)
{
    DWORD wpid = 0;

    if (!GetPropW(hwnd, L"__wine_dcomp_captured_hwnd"))
        return TRUE;
    GetWindowThreadProcessId(hwnd, &wpid);
    if (wpid == (DWORD)lparam)
    {
        g_dcomp_captured_top_hwnd = hwnd;
        dcomp_enforce_captured_toolwindow(hwnd);
    }
    return TRUE;
}

static void dcomp_enforce_captured_toolwindow(HWND top)
{
    LONG_PTR ex;
    DWORD wpid = 0;

    if (!top || !IsWindow(top) || !IsWindowVisible(top))
        return;
    GetWindowThreadProcessId(top, &wpid);
    if (wpid != GetCurrentProcessId())
        return;
    ex = GetWindowLongPtrW(top, GWL_EXSTYLE);
    if ((ex & WS_EX_APPWINDOW) || !(ex & WS_EX_TOOLWINDOW) || !(ex & WS_EX_NOACTIVATE))
    {
        SetWindowLongPtrW(top, GWL_EXSTYLE,
                (ex & ~WS_EX_APPWINDOW) | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE);
        SetWindowPos(top, 0, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_ASYNCWINDOWPOS);
    }
}

static void dcomp_hide_captured_hwnd(HWND hwnd)
{
    HWND top;

    SetPropW(hwnd, L"__wine_dcomp_captured_hwnd", (HANDLE)1);
    top = GetAncestor(hwnd, GA_ROOT);
    if (!top) top = hwnd;
    if (top != hwnd)
        SetPropW(top, L"__wine_dcomp_captured_hwnd", (HANDLE)1);
    g_dcomp_captured_top_hwnd = top;
    dcomp_enforce_captured_toolwindow(top);
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

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown1(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown2(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown3(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown4(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown5(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown6(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown7(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown8(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown9(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown10(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown11(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown12(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown13(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown14(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown15(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static LONG g_batch_id_counter = 10;

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown16(IDCompositionDeviceUnknown *iface, void **parameter1)
{
    TRACE("iface %p parameter1 %p.\n", iface, parameter1);

    if (!parameter1)
        return E_INVALIDARG;

    *(UINT64 *)parameter1 = (UINT64)(DWORD)InterlockedIncrement(&g_batch_id_counter);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown17(IDCompositionDeviceUnknown *iface, void **parameter1)
{
    void *ret = __builtin_return_address(0);
    TRACE("DCOMP: desktop_device_Unknown17 iface %p parameter1 %p caller %p\n", iface, parameter1, ret);

    if (!parameter1)
        return E_INVALIDARG;

    *(UINT64 *)parameter1 = (UINT64)(DWORD)InterlockedIncrement(&g_batch_id_counter);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown18(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown19(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown20(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown21(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown22(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown23(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown24(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown25(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown26(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown27(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown28(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown29(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown30(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown31(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown32(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown33(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown34(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown35(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown36(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown37(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown38(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown39(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown40(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown41(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown42(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown43(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown44(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown45(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown46(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown47(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown48(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown49(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown50(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown51(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown52(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown53(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown54(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown55(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown56(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown57(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown58(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown59(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown60(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown61(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown62(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown63(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown64(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown65(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown66(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown67(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown68(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown69(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown70(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown71(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown72(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown73(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown74(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown75(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown76(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown77(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown78(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown79(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown80(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown81(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown82(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown83(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown84(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown85(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown86(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown87(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown88(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown89(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown90(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown91(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown92(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown93(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown94(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown95(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown96(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown97(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown98(IDCompositionDeviceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown99(IDCompositionDeviceUnknown *iface)
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
    desktop_device_Unknown1,
    desktop_device_Unknown2,
    desktop_device_Unknown3,
    desktop_device_Unknown4,
    desktop_device_Unknown5,
    desktop_device_Unknown6,
    desktop_device_Unknown7,
    desktop_device_Unknown8,
    desktop_device_Unknown9,
    desktop_device_Unknown10,
    desktop_device_Unknown11,
    desktop_device_Unknown12,
    desktop_device_Unknown13,
    desktop_device_Unknown14,
    desktop_device_Unknown15,
    desktop_device_Unknown16,
    desktop_device_Unknown17,
    desktop_device_Unknown18,
    desktop_device_Unknown19,
    desktop_device_Unknown20,
    desktop_device_Unknown21,
    desktop_device_Unknown22,
    desktop_device_Unknown23,
    desktop_device_Unknown24,
    desktop_device_Unknown25,
    desktop_device_Unknown26,
    desktop_device_Unknown27,
    desktop_device_Unknown28,
    desktop_device_Unknown29,
    desktop_device_Unknown30,
    desktop_device_Unknown31,
    desktop_device_Unknown32,
    desktop_device_Unknown33,
    desktop_device_Unknown34,
    desktop_device_Unknown35,
    desktop_device_Unknown36,
    desktop_device_Unknown37,
    desktop_device_Unknown38,
    desktop_device_Unknown39,
    desktop_device_Unknown40,
    desktop_device_Unknown41,
    desktop_device_Unknown42,
    desktop_device_Unknown43,
    desktop_device_Unknown44,
    desktop_device_Unknown45,
    desktop_device_Unknown46,
    desktop_device_Unknown47,
    desktop_device_Unknown48,
    desktop_device_Unknown49,
    desktop_device_Unknown50,
    desktop_device_Unknown51,
    desktop_device_Unknown52,
    desktop_device_Unknown53,
    desktop_device_Unknown54,
    desktop_device_Unknown55,
    desktop_device_Unknown56,
    desktop_device_Unknown57,
    desktop_device_Unknown58,
    desktop_device_Unknown59,
    desktop_device_Unknown60,
    desktop_device_Unknown61,
    desktop_device_Unknown62,
    desktop_device_Unknown63,
    desktop_device_Unknown64,
    desktop_device_Unknown65,
    desktop_device_Unknown66,
    desktop_device_Unknown67,
    desktop_device_Unknown68,
    desktop_device_Unknown69,
    desktop_device_Unknown70,
    desktop_device_Unknown71,
    desktop_device_Unknown72,
    desktop_device_Unknown73,
    desktop_device_Unknown74,
    desktop_device_Unknown75,
    desktop_device_Unknown76,
    desktop_device_Unknown77,
    desktop_device_Unknown78,
    desktop_device_Unknown79,
    desktop_device_Unknown80,
    desktop_device_Unknown81,
    desktop_device_Unknown82,
    desktop_device_Unknown83,
    desktop_device_Unknown84,
    desktop_device_Unknown85,
    desktop_device_Unknown86,
    desktop_device_Unknown87,
    desktop_device_Unknown88,
    desktop_device_Unknown89,
    desktop_device_Unknown90,
    desktop_device_Unknown91,
    desktop_device_Unknown92,
    desktop_device_Unknown93,
    desktop_device_Unknown94,
    desktop_device_Unknown95,
    desktop_device_Unknown96,
    desktop_device_Unknown97,
    desktop_device_Unknown98,
    desktop_device_Unknown99,
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

HRESULT WINAPI DCompositionCreateSharedVisualHandle(HANDLE *handle)
{
    struct composition_visual *visual = NULL;
    NTSTATUS status;

    if (!handle)
        return STATUS_INVALID_PARAMETER;

    SERVER_START_REQ( dcomp_create_shared_visual )
    {
        if (!(status = wine_server_call( req )))
            *handle = wine_server_ptr_handle( reply->handle );
    }
    SERVER_END_REQ;

    if (!status && handle && *handle)
    {
        if (SUCCEEDED(create_visual(2, &IID_IDCompositionVisual, (void **)&visual)))
        {
            SERVER_START_REQ( dcomp_set_shared_visual_info )
            {
                req->handle = wine_server_obj_handle( *handle );
                req->target_root = wine_server_client_ptr( visual );
                wine_server_call( req );
            }
            SERVER_END_REQ;
            TRACE("DCOMP: DCompositionCreateSharedVisualHandle created and bound target_root %p to handle %p\n", visual, *handle);
        }
    }

    return (HRESULT)status;
}

struct composition_batch_query
{
    IUnknown IUnknown_iface;
    LONG ref;
};

static inline struct composition_batch_query *impl_from_batch_query(IUnknown *iface)
{
    return CONTAINING_RECORD(iface, struct composition_batch_query, IUnknown_iface);
}

static HRESULT WINAPI batch_query_QueryInterface(IUnknown *iface, REFIID riid, void **out)
{
    if (IsEqualIID(riid, &IID_IUnknown))
    {
        IUnknown_AddRef(iface);
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI batch_query_AddRef(IUnknown *iface)
{
    struct composition_batch_query *impl = impl_from_batch_query(iface);
    return InterlockedIncrement(&impl->ref);
}

static ULONG WINAPI batch_query_Release(IUnknown *iface)
{
    struct composition_batch_query *impl = impl_from_batch_query(iface);
    ULONG ref = InterlockedDecrement(&impl->ref);
    if (!ref) free(impl);
    return ref;
}

static HRESULT WINAPI batch_query_slot3(IUnknown *iface, DWORD *batch_id)
{
    if (batch_id) *batch_id = (DWORD)InterlockedIncrement(&g_batch_id_counter);
    return S_OK;
}

static HRESULT WINAPI batch_query_GetBatchId(IUnknown *iface, DWORD *batch_id)
{
    if (batch_id) *batch_id = (DWORD)InterlockedIncrement(&g_batch_id_counter);
    return S_OK;
}

static void *batch_query_vtbl[] =
{
    batch_query_QueryInterface,
    batch_query_AddRef,
    batch_query_Release,
    batch_query_slot3,
    batch_query_GetBatchId,
    batch_query_slot3,
    batch_query_slot3,
    batch_query_slot3,
};

HRESULT WINAPI DCompPrivates_CreateCompositionBatchQuery(IDCompositionDevice *device, void **out_query)
{
    struct composition_batch_query *impl;

    TRACE("device %p, out_query %p.\n", device, out_query);

    if (!out_query) return E_POINTER;
    if (!(impl = calloc(1, sizeof(*impl)))) return E_OUTOFMEMORY;
    impl->IUnknown_iface.lpVtbl = (void *)batch_query_vtbl;
    impl->ref = 1;
    *out_query = &impl->IUnknown_iface;
    return S_OK;
}

HRESULT WINAPI DCompPrivates_CreateSharedVisualHandle(IDCompositionDevice *device, HANDLE *out_handle)
{
    TRACE("device %p, out_handle %p.\n", device, out_handle);

    if (!out_handle) return E_POINTER;
    return DCompositionCreateSharedVisualHandle(out_handle);
}

HRESULT WINAPI DCompPrivates_OpenAndAttachSharedTarget(IDCompositionDevice *device, HWND hwnd, void *target_in, void **target_out)
{
    IDCompositionTarget *target = NULL;
    HRESULT hr;

    TRACE("device %p, hwnd %p, target_in %p, target_out %p.\n", device, hwnd, target_in, target_out);

    if (!hwnd) hwnd = find_root_top_level_window();

    hr = IDCompositionDevice_CreateTargetForHwnd(device, hwnd, TRUE, &target);
    if (SUCCEEDED(hr) && target)
    {
        struct composition_target *target_impl = impl_from_IDCompositionTarget(target);
        if (target_in)
        {
            target_impl->root = (IDCompositionVisual *)target_in;
            IDCompositionVisual_AddRef(target_impl->root);
        }
        if (target_out) *target_out = target;
        TRACE("Attached target %p to hwnd %p.\n", target, hwnd);
    }
    return hr;
}

HRESULT WINAPI DCompPrivates_OpenSharedDCompositionObject(IUnknown *device_unk, HANDLE handle, REFIID riid, void **out)
{
    struct composition_device *device_impl = NULL;
    IDCompositionDevice *device = NULL;

    TRACE("DCOMP: DCompPrivates_OpenSharedDCompositionObject device %p, handle %p, riid %s, out %p\n",
        device_unk, handle, debugstr_guid(riid), out);

    if (!device_unk || !out)
        return E_POINTER;

    if (SUCCEEDED(IUnknown_QueryInterface(device_unk, &IID_IDCompositionDevice, (void **)&device)))
    {
        device_impl = impl_from_IDCompositionDevice(device);
        IDCompositionDevice_Release(device);
    }
    else
    {
        device_impl = (struct composition_device *)device_unk;
    }

    if (IsEqualGUID(riid, &IID_IDCompositionTarget))
    {
        return create_target_from_shared_visual_handle(device_impl, handle, out);
    }
    else if (IsEqualGUID(riid, &IID_IDCompositionVisual))
    {
        return create_visual_from_shared_visual_handle(handle, out);
    }

    FIXME("Unsupported riid %s\n", debugstr_guid(riid));
    return E_NOINTERFACE;
}

HRESULT WINAPI DCompPrivates_SetCommitCompletionEvent(IDCompositionDevice *device, HANDLE event)
{
    TRACE("device %p, event %p.\n", device, event);
    g_commit_completion_event = event;
    if (event) SetEvent(event);
    return S_OK;
}

/*
 * Composition islands entry points.
 *
 * The Windows App SDK composition-islands layer - Microsoft.UI.Input.dll, which
 * hosts the Microsoft.UI.Content.ContentIsland, ContentSite, ContentIsland-
 * Environment and DesktopChildSiteBridge classes, driven from
 * Microsoft.UI.Windowing.Core.dll - reaches the compositor through these
 * private dcomp exports: Microsoft.UI.Input.dll imports the DCompPrivates_* set
 * by name from Microsoft.Internal.FrameworkUdk.dll, which forwards it to
 * dcomp.dll.  The site bridge that AppWindowTitleBar stores at obj+0x250 is
 * assembled from the shared visual and the shared target these produce, so
 * answering S_OK with an uninitialised out-parameter leaves the application
 * holding a null bridge, and its title-bar layout pass then dereferences it
 * into a FailFast.
 */

HRESULT WINAPI DCompPrivates_CreateSharedVisual(IDCompositionDevice *device, IDCompositionVisual **visual)
{
    struct composition_device *device_impl;

    TRACE("device %p, visual %p.\n", device, visual);

    if (!visual)
        return E_POINTER;
    *visual = NULL;
    if (!device)
        return E_INVALIDARG;

    device_impl = impl_from_IDCompositionDevice(device);
    return desktop_device_CreateSharedResource(&device_impl->IDCompositionDeviceUnknown_iface,
            &IID_IDCompositionVisual, (void **)visual);
}

HRESULT WINAPI DCompPrivates_OpenSharedResourceHandle(IDCompositionDevice *device, IUnknown *unknown, HANDLE *handle)
{
    struct composition_device *device_impl;

    TRACE("device %p, unknown %p, handle %p.\n", device, unknown, handle);

    if (!handle)
        return E_POINTER;
    *handle = NULL;
    if (!device)
        return E_INVALIDARG;

    device_impl = impl_from_IDCompositionDevice(device);
    return desktop_device_OpenSharedResourceHandle(&device_impl->IDCompositionDeviceUnknown_iface, unknown, handle);
}

/*
 * The size arrives as a SIZE by value in a single 8-byte stack slot on both
 * architectures, so the specification declares it as a 64-bit scalar and the
 * components are split here.
 */
static HRESULT set_visual_size(IDCompositionVisual *visual, LONGLONG packed)
{
    struct composition_visual *visual_impl;

    if (!visual)
        return E_INVALIDARG;

    visual_impl = impl_from_IDCompositionVisual(visual);
    visual_impl->size.cx = (LONG)(ULONGLONG)packed;
    visual_impl->size.cy = (LONG)((ULONGLONG)packed >> 32);
    return S_OK;
}

HRESULT WINAPI DCompPrivates_SetVisualRelativeSize(IDCompositionVisual *visual, LONGLONG size)
{
    TRACE("visual %p, size %ldx%ld.\n", visual, (LONG)(ULONGLONG)size, (LONG)((ULONGLONG)size >> 32));
    return set_visual_size(visual, size);
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
                if (compare_handles(g_compositor_clock_handles[k], handles[i])) break;
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
    if (g_compositor_clock_event)
    {
        /* Advance on the present cadence: release as soon as the composite thread
         * lands a frame, bounded by the frame period so the caller is still paced
         * before the first device starts compositing (event not yet signalled). */
        WaitForSingleObject( g_compositor_clock_event, sleep_ms );
    }
    else
        Sleep( sleep_ms );
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
