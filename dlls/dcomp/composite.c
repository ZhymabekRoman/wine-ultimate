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
#include "objidl.h"
#include "dcomp_private.h"
#include "dxgi.h"
#include "dwmapi.h"
#include "d3d11_4.h"
#include "dcomp_shaders.h"
#include <ddk/d3dkmthk.h>
#include "ntgdi.h"
#include "wine/server.h"
#include "wine/wined3d.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(dcomp);

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

static CRITICAL_SECTION composite_render_cs;
static CRITICAL_SECTION_DEBUG composite_render_debug =
{
    0, 0, &composite_render_cs,
    { &composite_render_debug.ProcessLocksList, &composite_render_debug.ProcessLocksList },
      0, 0, { (DWORD_PTR)(__FILE__ ": composite_render_cs") }
};
static CRITICAL_SECTION composite_render_cs = { &composite_render_debug, -1, 0, 0, 0, 0 };

void dcomp_render_lock(void)
{
    EnterCriticalSection(&composite_render_cs);
}

void dcomp_render_unlock(void)
{
    LeaveCriticalSection(&composite_render_cs);
}

BOOL dcomp_render_trylock(void)
{
    return TryEnterCriticalSection(&composite_render_cs);
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

HWND find_root_top_level_window(void)
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
    FLOAT backdrop_radius;
    FLOAT pad;
    FLOAT clip_rect[4];         /* left, top, right, bottom */
    FLOAT backdrop_rect[4];     /* left, top, right, bottom in device pixels (0 if unused) */
    FLOAT popup_bg_color[4];    /* theme-aware popup backdrop color (RGB + 1.0) */
};

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
                SetWindowPos(target->composite_hwnd, target->topmost ? HWND_TOP : HWND_BOTTOM, 0, 0, (int)width, (int)height,
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
        SetWindowPos(target->composite_hwnd, target->topmost ? HWND_TOP : HWND_BOTTOM, 0, 0, (int)width, (int)height,
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

    if (FAILED(hr = ID3D11Device1_CreateVertexShader(d3d11_device, dcomp_composite_vs_dxbc,
            sizeof(dcomp_composite_vs_dxbc), NULL, &target->composite_vs)))
        return hr;
    if (FAILED(hr = ID3D11Device1_CreatePixelShader(d3d11_device, dcomp_composite_ps_dxbc,
            sizeof(dcomp_composite_ps_dxbc), NULL, &target->composite_ps)))
        return hr;
    if (FAILED(hr = ID3D11Device1_CreateInputLayout(d3d11_device, layout_desc, ARRAY_SIZE(layout_desc),
            dcomp_composite_vs_dxbc, sizeof(dcomp_composite_vs_dxbc),
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
BOOL apps_use_light_theme(void)
{
    static LONG cached = -1;
    LONG val = InterlockedCompareExchange(&cached, -1, -1);
    HANDLE hkcu, hkey;
    OBJECT_ATTRIBUTES attr;
    UNICODE_STRING subkey, name;
    char buf[sizeof(KEY_VALUE_PARTIAL_INFORMATION) + sizeof(DWORD)];
    KEY_VALUE_PARTIAL_INFORMATION *info = (KEY_VALUE_PARTIAL_INFORMATION *)buf;
    ULONG len = 0;
    BOOL light = TRUE;

    if (val != -1)
        return val != 0;

    if (!RtlOpenCurrentUser(KEY_READ, &hkcu))
    {
        RtlInitUnicodeString(&subkey, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize");
        InitializeObjectAttributes(&attr, &subkey, OBJ_CASE_INSENSITIVE, hkcu, NULL);
        if (!NtOpenKey(&hkey, KEY_READ, &attr))
        {
            RtlInitUnicodeString(&name, L"AppsUseLightTheme");
            if (!NtQueryValueKey(hkey, &name, KeyValuePartialInformation, buf, sizeof(buf), &len)
                    && info->Type == REG_DWORD && info->DataLength == sizeof(DWORD))
            {
                DWORD dword_val = *(const DWORD *)info->Data;
                light = (dword_val != 0);
            }
            NtClose(hkey);
        }
        NtClose(hkcu);
    }

    InterlockedExchange(&cached, light ? 1 : 0);
    return light;
}

static HRESULT composite_surface_to_backbuffer(struct composition_target *target,
        struct composition_visual *visual,
        ID3D11Device1 *d3d11_device, ID3D11DeviceContext *d3d11_device_context,
        ID3D11Texture2D *src_texture, UINT src_width, UINT src_height,
        UINT dst_width, UINT dst_height, const D3DMATRIX *proj_matrix,
        float opacity, const D2D_RECT_F *clip_rect,
        BOOL has_backdrop_clip, const D2D_RECT_F *backdrop_clip, float backdrop_radius,
        BOOL clear, POINT copy_off)
{
    static const float transparent_clear[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    static const float dark_clear[4] = {0.125f, 0.125f, 0.125f, 1.0f};
    static const float light_clear[4] = {0.953f, 0.953f, 0.953f, 1.0f};
    DWORD ex_style = target->hwnd ? GetWindowLongW(target->hwnd, GWL_EXSTYLE) : 0;
    DWORD style = target->hwnd ? GetWindowLongW(target->hwnd, GWL_STYLE) : 0;
    const float *clear_color = ((ex_style & WS_EX_NOREDIRECTIONBITMAP) && (style & WS_POPUP))
            ? transparent_clear : (apps_use_light_theme() ? light_clear : dark_clear);
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
        blit_data.backdrop_radius = has_backdrop_clip ? backdrop_radius : 0.0f;
        blit_data.pad = 0.0f;
        blit_data.clip_rect[0] = clip_rect ? clip_rect->left : 0.0f;
        blit_data.clip_rect[1] = clip_rect ? clip_rect->top : 0.0f;
        blit_data.clip_rect[2] = clip_rect ? clip_rect->right : (FLOAT)dst_width;
        blit_data.clip_rect[3] = clip_rect ? clip_rect->bottom : (FLOAT)dst_height;
        if ((ex_style & WS_EX_NOREDIRECTIONBITMAP) && (style & WS_POPUP) && has_backdrop_clip && backdrop_clip)
        {
            blit_data.backdrop_rect[0] = backdrop_clip->left;
            blit_data.backdrop_rect[1] = backdrop_clip->top;
            blit_data.backdrop_rect[2] = backdrop_clip->right;
            blit_data.backdrop_rect[3] = backdrop_clip->bottom;
        }
        else
        {
            blit_data.backdrop_rect[0] = blit_data.backdrop_rect[1] = 0.0f;
            blit_data.backdrop_rect[2] = blit_data.backdrop_rect[3] = 0.0f;
        }
        if ((ex_style & WS_EX_NOREDIRECTIONBITMAP) && (style & WS_POPUP))
            TRACE("DCOMP: popup blit hwnd %p style %#lx ex %#lx has_bd %u bd=(%.1f,%.1f)-(%.1f,%.1f) r=%.1f\n",
                    target->hwnd, style, ex_style, has_backdrop_clip,
                    blit_data.backdrop_rect[0], blit_data.backdrop_rect[1],
                    blit_data.backdrop_rect[2], blit_data.backdrop_rect[3],
                    blit_data.backdrop_radius);
        if (apps_use_light_theme())
        {
            blit_data.popup_bg_color[0] = 0.976f;
            blit_data.popup_bg_color[1] = 0.976f;
            blit_data.popup_bg_color[2] = 0.976f;
            blit_data.popup_bg_color[3] = 1.0f;
        }
        else
        {
            blit_data.popup_bg_color[0] = 0.173f;
            blit_data.popup_bg_color[1] = 0.173f;
            blit_data.popup_bg_color[2] = 0.173f;
            blit_data.popup_bg_color[3] = 1.0f;
        }

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
                                      BOOL has_backdrop_clip, const D2D_RECT_F *backdrop_clip,
                                      float backdrop_radius,
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
                                &proj_matrix, opacity, effective_clip,
                                has_backdrop_clip, backdrop_clip, backdrop_radius,
                                FALSE, (POINT){dst_x, dst_y});
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
            has_backdrop_clip, backdrop_clip, backdrop_radius,
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

void dcomp_reclaim_drain(void)
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
    BOOL has_backdrop_clip;
    D2D_RECT_F backdrop_clip;
    float backdrop_radius;
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
static FLOAT visual_get_clip_radius(const struct composition_visual *visual);
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
    frame->has_backdrop_clip = FALSE;
    memset(&frame->backdrop_clip, 0, sizeof(frame->backdrop_clip));
    frame->backdrop_radius = 0.0f;
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
        if (!visual_tree_has_content(visual))
        {
            FLOAT radius = visual_get_clip_radius(visual);
            if (radius > 0.0f && cur_clip.right > cur_clip.left && cur_clip.bottom > cur_clip.top)
            {
                target->has_backdrop_clip = TRUE;
                target->backdrop_clip = cur_clip;
                target->backdrop_radius = radius;
            }
        }
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
                if (surf->source_swapchain)
                {
                    UINT present_count = 0;
                    device->last_has_swapchain = TRUE;
                    if (SUCCEEDED(IDXGISwapChain_GetLastPresentCount(surf->source_swapchain, &present_count)))
                    {
                        if (!present_count)
                            has_drawn_content = FALSE;
                        else
                            content_ver += present_count + 1;
                    }
                }
                else if (surf->factory && !surf->content_version)
                    has_drawn_content = FALSE;
                content_ver += surf->content_version;
            }
            else if (visual->wined3d_swapchain)
            {
                UINT present_count = 0;
                device->last_has_swapchain = TRUE;
                if (SUCCEEDED(IDXGISwapChain_GetLastPresentCount((IDXGISwapChain *)visual->content, &present_count)))
                {
                    if (!present_count)
                        has_drawn_content = FALSE;
                    else
                        content_ver += present_count + 1;
                }
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
        struct composition_visual *root_impl = root ? impl_from_IDCompositionVisual(root) : NULL;

        dcomp_lock();
        shared_visual_cache_set(h, root);
        if (!root_impl || root_impl->is_shared_proxy || !shared_visual_has_in_process_target(h, root_impl))
            has_cross_process = TRUE;
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
        if (host_target && !host_target->hwnd)
        {
            struct composition_target *cur = host_target, *up_host = NULL;
            unsigned int hops = 0;

            while (cur && !cur->hwnd && hops++ < 8)
            {
                if (!find_root_visual_container(cur, &up_host) || !up_host || up_host == cur)
                    break;
                cur = up_host;
            }
            if (cur && cur->hwnd)
                host_target = cur;
        }
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
    unsigned int i;

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

        if (target_delegates_to_in_process_target(target, root))
        {
            target->needs_redraw = FALSE;
            continue;
        }

        {
            unsigned int target_start = device->frame_buf_count;
            unsigned int target_queued = 0;
            BOOL force = target->needs_redraw;
            target->needs_redraw = FALSE;
            target->has_backdrop_clip = FALSE;
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
                target->has_backdrop_clip = FALSE;
                collect_visual_frames(device, target, root, target->hwnd, 0.0f, 0.0f,
                        &identity, FALSE, 0, NULL, 0, TRUE, &target_queued);
            }
            if (target->has_backdrop_clip)
            {
                unsigned int j;
                for (j = target_start; j < device->frame_buf_count; j++)
                {
                    device->frame_buf[j].has_backdrop_clip = TRUE;
                    device->frame_buf[j].backdrop_clip = target->backdrop_clip;
                    device->frame_buf[j].backdrop_radius = target->backdrop_radius;
                }
            }
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

static FLOAT visual_get_clip_radius(const struct composition_visual *visual)
{
    IDCompositionRectangleClip *rectangle_clip;
    struct dcomp_transform *transform;
    FLOAT radius = 0.0f;

    if (visual->clip && SUCCEEDED(IDCompositionClip_QueryInterface(visual->clip,
            &IID_IDCompositionRectangleClip, (void **)&rectangle_clip)))
    {
        transform = impl_from_rectangle_clip(rectangle_clip);
        radius = transform->clip_tl_radius_x;
        IDCompositionRectangleClip_Release(rectangle_clip);
    }
    return radius;
}

static IDXGISurface *frame_get_surface(struct composite_frame *frame, BOOL *skip_present)
{
    ID3D11Texture2D *texture = NULL;
    IDXGISwapChain1 *swapchain = NULL;
    IDXGISwapChain *swapchain_v1 = NULL;
    IDCompositionSurface *surface = NULL;
    IDXGIResource *dxgi_res = NULL;
    ID3D11Resource *d3d_res = NULL;
    IDXGISurface *dxgi_surface = NULL;
    HRESULT hr;

    if (skip_present)
        *skip_present = FALSE;

    if (SUCCEEDED(IUnknown_QueryInterface(frame->frame_content, &IID_IDCompositionSurface, (void **)&surface)))
    {
        struct composition_surface *surface_impl = unsafe_impl_from_IDCompositionSurface(surface);

        if (surface_impl && surface_impl->source_swapchain)
        {
            UINT present_count = 0;
            if (FAILED(IDXGISwapChain_GetLastPresentCount(surface_impl->source_swapchain, &present_count))
                    || !present_count)
            {
                if (skip_present)
                    *skip_present = TRUE;
                IDCompositionSurface_Release(surface);
                return NULL;
            }
        }

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
         * rendered frame stays in buffer 0 rather than rotating to BufferCount - 1.
         * Sample the render-thread snapshot ring from dxgi: d2d1 clears buffer 0
         * when it begins the next frame, and reading it live shows transparent blocks. */
        hr = dxgi_swapchain_get_presented_surface((IDXGISwapChain *)swapchain, &dxgi_surface);
        if (hr == S_FALSE)
        {
            if (skip_present)
                *skip_present = TRUE;
        }
        else if (hr != S_OK)
        {
            hr = IDXGISwapChain1_GetBuffer(swapchain, 0, &IID_IDXGISurface, (void **)&dxgi_surface);
            if (FAILED(hr))
                ERR("Failed to get the swapchain buffer 0, hr %#lx.\n", hr);
        }
        IDXGISwapChain1_Release(swapchain);
    }
    else if (SUCCEEDED(IUnknown_QueryInterface(frame->frame_content, &IID_IDXGISwapChain, (void **)&swapchain_v1)))
    {
        hr = dxgi_swapchain_get_presented_surface(swapchain_v1, &dxgi_surface);
        if (hr == S_FALSE)
        {
            if (skip_present)
                *skip_present = TRUE;
        }
        else if (hr != S_OK)
        {
            hr = IDXGISwapChain_GetBuffer(swapchain_v1, 0, &IID_IDXGISurface, (void **)&dxgi_surface);
            if (FAILED(hr))
                ERR("Failed to get the swapchain front buffer, hr %#lx.\n", hr);
        }
        IDXGISwapChain_Release(swapchain_v1);
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
    BOOL culled = FALSE, skip_present = FALSE;

    /* The frame holds a reference to the visual, so reading its effect-group
     * opacity here is pure memory; the accumulated target-space clip was
     * resolved during collect_visual_frames. */
    visual = frame->visual;
    opacity = visual ? visual_get_opacity(visual) : 1.0f;
    clip = frame->has_clip ? &frame->target_clip : NULL;

    if (frame->is_fallback)
    {
        if (frame->target_hwnd
                && (GetWindowLongW(frame->target_hwnd, GWL_EXSTYLE) & WS_EX_NOREDIRECTIONBITMAP)
                && (GetWindowLongW(frame->target_hwnd, GWL_STYLE) & WS_POPUP))
        {
            frame->culled = TRUE;
            return;
        }
        TRACE("DCOMP: fallback blitting surface %p to hwnd %p.\n", frame->surface, frame->target_hwnd);
        if (SUCCEEDED(IUnknown_QueryInterface(frame->surface->physical_surface,
                &IID_IDXGISurface, (void **)&dxgi_surface)))
        {
            if (do_composite_dxgi_surface(frame->target, visual, frame->target_hwnd, frame->pt,
                    dxgi_surface, opacity, clip,
                    frame->has_backdrop_clip, &frame->backdrop_clip, frame->backdrop_radius,
                    frame->has_transform, &frame->world_transform, &culled))
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
                    dxgi_surface, opacity, clip,
                    frame->has_backdrop_clip, &frame->backdrop_clip, frame->backdrop_radius,
                    frame->has_transform, &frame->world_transform, &culled))
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
    if ((dxgi_surface = frame_get_surface(frame, &skip_present)))
    {
        if (do_composite_dxgi_surface(frame->target, visual, frame->target_hwnd, frame->pt,
                dxgi_surface, opacity, clip,
                frame->has_backdrop_clip, &frame->backdrop_clip, frame->backdrop_radius,
                frame->has_transform, &frame->world_transform, &culled))
            frame->rendered = TRUE;
        else if (culled)
            frame->culled = TRUE;
        IDXGISurface_Release(dxgi_surface);
        if (frame->rendered || frame->culled)
            goto done;
    }
    else if (skip_present)
    {
        frame->culled = TRUE;
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

ULONGLONG dcomp_qpc_now(void)
{
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return (ULONGLONG)now.QuadPart;
}

unsigned int dcomp_qpc_elapsed_ms(ULONGLONG since, LARGE_INTEGER freq)
{
    ULONGLONG now;
    if (!since || !freq.QuadPart)
        return ~0u;
    now = dcomp_qpc_now();
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

    if (!refresh_period)
        return;

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

    start = dcomp_qpc_now();
    if (device->vblank_device && pfn)
    {
        D3DKMT_WAITFORVERTICALBLANKEVENT evt;

        evt.hDevice = device->vblank_device;
        pfn(&evt);
    }

    elapsed_us = device->qpc_freq.QuadPart ? (LONGLONG)((dcomp_qpc_now() - start) * 1000000ULL
            / (ULONGLONG)device->qpc_freq.QuadPart) : 0;
    if (elapsed_us < (LONGLONG)refresh_period * 500) /* stub: emulate the vblank interval */
    {
        unsigned int spent_ms = (unsigned int)(elapsed_us / 1000);
        if (refresh_period > spent_ms)
            Sleep(refresh_period - spent_ms);
    }
}

static BOOL visual_tree_check_swapchain(const struct composition_visual *visual, unsigned int depth,
        BOOL *has_swapchain)
{
    const struct composition_visual *child;
    BOOL updated = FALSE;

    if (!visual || depth > 256)
        return FALSE;

    if (visual->content)
    {
        struct composition_surface *surf = unsafe_impl_from_IDCompositionSurface((IDCompositionSurface *)visual->content);
        IDXGISwapChain *sc = NULL;

        if (surf && surf->source_swapchain)
            sc = surf->source_swapchain;
        else if (!surf && visual->wined3d_swapchain)
            sc = (IDXGISwapChain *)visual->content;

        if (sc)
        {
            UINT present_count = 0;
            unsigned int content_ver = visual->content_version;
            if (surf)
                content_ver += surf->content_version;
            *has_swapchain = TRUE;
            if (SUCCEEDED(IDXGISwapChain_GetLastPresentCount(sc, &present_count)) && present_count)
            {
                content_ver += present_count + 1;
                if (content_ver != visual->composited_version)
                    updated = TRUE;
            }
        }
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
            target->needs_redraw = TRUE;
            InterlockedIncrement(&dcomp_tree_version);
            updated = TRUE;
        }
        if (target->needs_redraw)
            updated = TRUE;
        if (target->root && visual_tree_check_swapchain(impl_from_IDCompositionVisual(target->root), 0, has_swapchain))
            updated = TRUE;
    }
    dcomp_unlock();
    return updated;
}

BOOL composite_device(struct composition_device *device)
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
        unsigned int elapsed = dcomp_qpc_elapsed_ms(device->last_composite_qpc, device->qpc_freq);
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
            {
                HANDLE handles[2];
                DWORD n = 0, ret;

                handles[n++] = device->wake_event;
                if (g_swapchain_present_event)
                    handles[n++] = g_swapchain_present_event;
                ret = WaitForMultipleObjects(n, handles, FALSE, wait_ms);
                /* Consume a Present wake that coalesced with this idle window, so the
                 * re-check below runs against the freshest present count. */
                if (ret == WAIT_OBJECT_0 + 1 && g_swapchain_present_event)
                    ResetEvent(g_swapchain_present_event);
            }
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
        }

        /* Present/vblank cadence. Reset the manual-reset compositor clock, do the
         * frame, then broadcast it, so a caller that arrived for this frame is
         * released when the frame lands (a caller arriving between frames returns
         * immediately, at most one frame early). A real compositor raises the
         * commit-completion alongside each presented frame, so pulse it from the
         * same tick: the CoreMessaging lifted-input workers that parked on the
         * commit-completion event then advance on the present cadence instead of
         * only when the app calls into device_Commit / WaitForCommitCompletion. */
        if (device->commit_pending || device->last_has_shared || sc_updated)
        {
            ULONGLONG prev_qpc = device->last_composite_qpc;
            unsigned int cur_elapsed = dcomp_qpc_elapsed_ms(prev_qpc, device->qpc_freq);

            if (cur_elapsed < refresh_period)
                composite_wait_vblank(device, refresh_period - cur_elapsed);
            if (device->exit_thread)
                break;

            if (TryEnterCriticalSection(&composite_render_cs))
            {
                if (device->last_composite_qpc == prev_qpc
                        || dcomp_qpc_elapsed_ms(device->last_composite_qpc, device->qpc_freq) >= refresh_period)
                {
                    BOOL had_pending;

                    had_pending = InterlockedExchange(&device->commit_pending, FALSE);
                    if (composite_device(device) || had_pending || device->last_has_shared)
                    {
                        device->last_composite_qpc = dcomp_qpc_now();
                        idle_ticks = 0;
                    }
                    dcomp_signal_compositor_clock();
                }
                LeaveCriticalSection(&composite_render_cs);
            }
            else
            {
                dcomp_signal_compositor_clock();
            }
        }
        else
        {
            dcomp_signal_compositor_clock();
        }
        if (device->self_release)
            break;
        /* Throttle the completion-side pulses to ~15Hz (every 4th 60Hz tick, ~64ms).
         * A faster commit-completion pulse drives the composition churn so hard
         * the WebView2 browser tears down early (ShowWindow climbs, LRPC drain is
         * truncated). Every 4th tick keeps the lifted-input workers advancing on the
         * present cadence while leaving the browser enough lifetime to issue further
         * LRPC requests on the composition plane. */
        if (!(++tick % 4))
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
        /* Auto-reset: one Present since the composite thread last waited releases one
         * idle-wake; successive Presents coalesce. Created before the thread starts so
         * a wine_dcomp_swapchain_presented() from a Present always finds it. */
        if (!g_swapchain_present_event)
            g_swapchain_present_event = CreateEventW( NULL, FALSE, FALSE, NULL );

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

