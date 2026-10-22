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
#include <stdarg.h>
#include <stdlib.h>
#include <assert.h>

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
#include "wine/server.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(dcomp);

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

static inline BOOL compare_handles(HANDLE h1, HANDLE h2)
{
    if (h1 == h2) return TRUE;
    if (!h1 || !h2) return FALSE;
    return NtCompareObjects(h1, h2) == STATUS_SUCCESS;
}

HRESULT WINAPI DCompositionCreateSharedVisualHandle(HANDLE *handle);

static HANDLE g_commit_completion_event = NULL;

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

static BOOL is_partner_guid(REFIID iid)
{
    return IsEqualGUID(iid, &IID_IDCompositionDesktopDevicePartner)
        || IsEqualGUID(iid, &IID_IDCompositionDeviceUnknown)
        || !memcmp(iid, "\xfe\x33\x46\x5f\x08\x1e\xb8\x4c\x8c\x75\xce\x24\x33\x3f\x56\x02", 16)
        || !memcmp(iid, "\x8d\x46\xf6\x75\x8e\x1b\x7c\x44\x9b\xc6\x75\xfe\xa8\x0b\x5b\x25", 16)
        || !memcmp(iid, "\xc4\x2e\x68\x25\xa4\x73\x22\x40\xa0\x4f\x1b\xbe\x6d\xc3\xe1\xf0", 16)
        || !memcmp(iid, "\x5f\x27\x1a\x3c\x75\xf1\x76\x46\x92\xb4\x06\x15\x30\xb2\xf2\xf7", 16)
        || !memcmp(iid, "\xac\x1f\xb3\xdf\x91\xba\x57\x4b\xac\xed\xe7\x34\x7f\x50\x67\xe0", 16)
        || !memcmp(iid, "\x58\x61\x4b\xd1\xfa\xc3\xce\x4b\x9c\x1f\xb6\x1d\x86\x65\xea\xb0", 16);
}

static HRESULT STDMETHODCALLTYPE device_QueryInterface(IDCompositionDevice *iface,
        REFIID iid, void **out)
{
    struct composition_device *device = impl_from_IDCompositionDevice(iface);

    TRACE("iface %p, iid %s, out %p\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_IUnknown)
            || IsEqualGUID(iid, &IID_IDCompositionDevice))
    {
        IUnknown_AddRef(&device->IDCompositionDevice_iface);
        *out = &device->IDCompositionDevice_iface;
        return S_OK;
    }
    else if (!memcmp(iid, "\x68\x69\x55\x6b\x25\x2d\xe2\x11\x99\xf6\x8b\xc3\x60\x88\x70\x9b", 16))
    {
        *out = &dcomp_6b556968_obj;
        return S_OK;
    }
    else if ((device->version >= 2
              && (IsEqualGUID(iid, &IID_IDCompositionDevice2)
                  || IsEqualGUID(iid, &IID_IDCompositionDevice3)
                  || IsEqualGUID(iid, &IID_IDCompositionDesktopDevice)))
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
            device->exit_thread = TRUE;
            WaitForSingleObject(device->thread, INFINITE);
            CloseHandle(device->thread);
            device->thread = NULL;
        }
        if (device->surface_factory)
        {
            IDCompositionSurfaceFactory *factory = device->surface_factory;
            device->surface_factory = NULL;
            IDCompositionSurfaceFactory_Release(factory);
        }
        if (device->rendering_device)
            IUnknown_Release(device->rendering_device);
        free(device);
    }

    return ref;
}

/* TODO: Optimize this. There is plenty of room */
static HRESULT create_bgra_surface_from_rgba(IDXGISurface *rgba_surface, IDXGISurface **bgra_surface)
{
    static const D3D11_INPUT_ELEMENT_DESC layout_desc[] =
    {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };

    static const char vs_code[] =
        "void main(in float4 in_position : POSITION,\n"
        "          in float2 in_texcoord : TEXCOORD,\n"
        "          out float4 position : SV_Position,\n"
        "          out float2 texcoord : TEXCOORD)\n"
        "{\n"
        "     position = in_position;\n"
        "     texcoord = in_texcoord;\n"
        "}";

    static const char ps_code[] =
        "Texture2D src_texture : register(t0);\n"
        "SamplerState sam_linear : register(s0);\n"
        "float4 main(float4 pos : SV_POSITION, float2 uv : TEXCOORD) : SV_TARGET"
        "{\n"
        "    return src_texture.Sample(sam_linear, uv);\n"
        "}";

    struct vec2
    {
        float x, y;
    };

    static const struct
    {
        struct vec2 position;
        struct vec2 texcoord;
    } quad[] =
    {
        {{-1.0f, 1.0f}, {0.0f, 0.0f}},   /* Top-Left */
        {{1.0f, 1.0f}, {1.0f, 0.0f}},    /* Top-Right */
        {{-1.0f, -1.0f}, {0.0f, 1.0f}},  /* Bottom-Left */
        {{1.0f, -1.0f}, {1.0f, 1.0f}},   /* Bottom-Right */
    };

    ID3D11Texture2D *src_texture = NULL, *dst_texture = NULL;
    ID3DDeviceContextState *state = NULL, *old_state = NULL;
    ID3D11DeviceContext1 *d3d11_device_context1 = NULL;
    ID3D11DeviceContext *d3d11_device_context = NULL;
    static ID3D10Blob *vs_blob = NULL, *ps_blob = NULL;
    ID3D11InputLayout *input_layout = NULL;
    D3D11_SAMPLER_DESC sampler_desc = {0};
    D3D11_SUBRESOURCE_DATA resource_data;
    ID3D11ShaderResourceView *srv = NULL;
    ID3D11Device1 *d3d11_device = NULL;
    ID3D11RenderTargetView *rtv = NULL;
    ID3D11SamplerState *sampler = NULL;
    D3D11_TEXTURE2D_DESC texture_desc;
    DXGI_SURFACE_DESC surface_desc;
    D3D11_BUFFER_DESC buffer_desc;
    ID3D11VertexShader *vs = NULL;
    ID3D11PixelShader *ps = NULL;
    unsigned int stride, offset;
    ID3D11Buffer *vb = NULL;
    D3D11_VIEWPORT vp;
    HRESULT hr;

    static const D3D_FEATURE_LEVEL feature_levels[] =
    {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };

    hr = IDXGISurface_QueryInterface(rgba_surface, &IID_ID3D11Texture2D, (void **)&src_texture);
    if (FAILED(hr))
    {
        ERR("Failed to get a ID3D11Texture2D, hr %#lx.\n", hr);
        goto done;
    }

    hr = IDXGISurface_GetDevice(rgba_surface, &IID_ID3D11Device1, (void **)&d3d11_device);
    if (FAILED(hr))
    {
        ERR("Failed to get device, hr %#lx.\n", hr);
        goto done;
    }

    hr = IDXGISurface_GetDesc(rgba_surface, &surface_desc);
    if (FAILED(hr))
    {
        ERR("Failed to get the dxgi surface description, hr %#lx.\n", hr);
        goto done;
    }

    texture_desc.Width = surface_desc.Width;
    texture_desc.Height = surface_desc.Height;
    texture_desc.MipLevels = 1;
    texture_desc.ArraySize = 1;
    texture_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    texture_desc.SampleDesc.Count = 1;
    texture_desc.SampleDesc.Quality = 0;
    texture_desc.Usage = D3D11_USAGE_DEFAULT;
    texture_desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    texture_desc.CPUAccessFlags = 0;
    texture_desc.MiscFlags = D3D11_RESOURCE_MISC_GUARDED | D3D11_RESOURCE_MISC_GDI_COMPATIBLE;
    hr = ID3D11Device1_CreateTexture2D(d3d11_device, &texture_desc, NULL, &dst_texture);
    if (FAILED(hr))
    {
        ERR("Failed to create a ID3D11Texture2D, hr %#lx.\n", hr);
        goto done;
    }

    hr = ID3D11Device1_CreateShaderResourceView(d3d11_device, (ID3D11Resource *)src_texture, NULL, &srv);
    if (FAILED(hr))
    {
        ERR("Failed to create a SRV, hr %#lx.\n", hr);
        goto done;
    }

    hr = ID3D11Device1_CreateRenderTargetView(d3d11_device, (ID3D11Resource *)dst_texture, NULL, &rtv);
    if (FAILED(hr))
    {
        ERR("Failed to create a RTV, hr %#lx.\n", hr);
        goto done;
    }

    ID3D11Device1_GetImmediateContext(d3d11_device, &d3d11_device_context);
    if (FAILED(hr = ID3D11DeviceContext_QueryInterface(d3d11_device_context, &IID_ID3D11DeviceContext1,
                                                       (void **)&d3d11_device_context1)))
    {
        ERR("Failed to query ID3D11DeviceContext1, hr %#lx.\n", hr);
        goto done;
    }

    if (FAILED(hr = ID3D11Device1_CreateDeviceContextState(d3d11_device, 0, feature_levels,
                                                           ARRAY_SIZE(feature_levels), D3D11_SDK_VERSION,
                                                           &IID_ID3D11Device1, NULL, &state)))
    {
        ERR("Failed to create device context state, hr %#lx.\n", hr);
        goto done;
    }
    ID3D11DeviceContext1_SwapDeviceContextState(d3d11_device_context1, state, &old_state);

    ID3D11DeviceContext1_OMSetRenderTargets(d3d11_device_context1, 1, &rtv, NULL);
    ID3D11DeviceContext1_PSSetShaderResources(d3d11_device_context1, 0, 1, &srv);

    /* Move this to dcomp device instance ? */
    if (vs_blob == NULL)
    {
        hr = D3DCompile(vs_code, sizeof(vs_code) - 1, "vs", NULL, NULL, "main", "vs_4_0", 0, 0, &vs_blob, NULL);
        if (FAILED(hr))
        {
            ERR("Failed to compile vertex shader, hr %#lx.\n", hr);
            goto done;
        }

        hr = D3DCompile(ps_code, sizeof(ps_code) - 1, "ps", NULL, NULL, "main", "ps_4_0", 0, 0, &ps_blob, NULL);
        if (FAILED(hr))
        {
            ERR("Failed to compile pixel shader, hr %#lx.\n", hr);
            goto done;
        }
    }

    hr = ID3D11Device1_CreateVertexShader(d3d11_device, ID3D10Blob_GetBufferPointer(vs_blob),
                                          ID3D10Blob_GetBufferSize(vs_blob), NULL, &vs);
    if (FAILED(hr))
    {
        ERR("Failed to create vertex shader, hr %#lx.\n", hr);
        goto done;
    }

    hr = ID3D11Device1_CreatePixelShader(d3d11_device, ID3D10Blob_GetBufferPointer(ps_blob),
                                         ID3D10Blob_GetBufferSize(ps_blob), NULL, &ps);
    if (FAILED(hr))
    {
        ERR("Failed to create pixel shader, hr %#lx.\n", hr);
        goto done;
    }

    hr = ID3D11Device1_CreateInputLayout(d3d11_device, layout_desc, ARRAY_SIZE(layout_desc),
                                         ID3D10Blob_GetBufferPointer(vs_blob),
                                         ID3D10Blob_GetBufferSize(vs_blob), &input_layout);
    if (FAILED(hr))
    {
        ERR("Failed to create input layout, hr %#lx.\n", hr);
        goto done;
    }

    buffer_desc.ByteWidth = sizeof(quad);
    buffer_desc.Usage = D3D11_USAGE_DEFAULT;
    buffer_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    buffer_desc.CPUAccessFlags = 0;
    buffer_desc.MiscFlags = 0;
    buffer_desc.StructureByteStride = 0;
    resource_data.pSysMem = quad;
    resource_data.SysMemPitch = 0;
    resource_data.SysMemSlicePitch = 0;
    hr = ID3D11Device1_CreateBuffer(d3d11_device, &buffer_desc, &resource_data, &vb);
    if (FAILED(hr))
    {
        ERR("Failed to create vertex buffer, hr %#lx.\n", hr);
        goto done;
    }

    sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sampler_desc.MinLOD = 0;
    sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;

    hr = ID3D11Device1_CreateSamplerState(d3d11_device, &sampler_desc, &sampler);
    if (FAILED(hr))
    {
        ERR("Failed to create sampler state, hr %#lx.\n", hr);
        goto done;
    }
    ID3D11DeviceContext1_PSSetSamplers(d3d11_device_context1, 0, 1, &sampler);

    ID3D11DeviceContext1_IASetInputLayout(d3d11_device_context1, input_layout);
    ID3D11DeviceContext1_IASetPrimitiveTopology(d3d11_device_context1, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

    stride = sizeof(quad[0]);
    offset = 0;
    ID3D11DeviceContext1_IASetVertexBuffers(d3d11_device_context1, 0, 1, &vb, &stride, &offset);
    ID3D11DeviceContext1_VSSetShader(d3d11_device_context1, vs, NULL, 0);
    ID3D11DeviceContext1_PSSetShader(d3d11_device_context1, ps, NULL, 0);

    vp.TopLeftX = 0;
    vp.TopLeftY = 0;
    vp.Width = surface_desc.Width;
    vp.Height = surface_desc.Height;
    vp.MinDepth = 0;
    vp.MaxDepth = 1;
    ID3D11DeviceContext1_RSSetViewports(d3d11_device_context1, 1, &vp);
    ID3D11DeviceContext1_Draw(d3d11_device_context1, 4, 0);

    hr = ID3D11Texture2D_QueryInterface(dst_texture, &IID_IDXGISurface, (void **)bgra_surface);
    if (FAILED(hr))
    {
        ERR("Failed to get bgra surface, hr %#lx.\n", hr);
        goto done;
    }

done:
    if (old_state)
    {
        ID3D11DeviceContext1_SwapDeviceContextState(d3d11_device_context1, old_state, NULL);
        ID3DDeviceContextState_Release(old_state);
    }
    if (state)
        ID3DDeviceContextState_Release(state);
    if (vb)
        ID3D11Buffer_Release(vb);
    if (sampler)
        ID3D11SamplerState_Release(sampler);
    if (input_layout)
        ID3D11InputLayout_Release(input_layout);
    if (ps)
        ID3D11PixelShader_Release(ps);
    if (vs)
        ID3D11VertexShader_Release(vs);
    if (d3d11_device_context1)
        ID3D11DeviceContext1_Release(d3d11_device_context1);
    if (d3d11_device_context)
        ID3D11DeviceContext_Release(d3d11_device_context);
    if (rtv)
        ID3D11RenderTargetView_Release(rtv);
    if (srv)
        ID3D11ShaderResourceView_Release(srv);
    if (dst_texture)
        ID3D11Texture2D_Release(dst_texture);
    if (d3d11_device)
        ID3D11Device1_Release(d3d11_device);
    if (src_texture)
        ID3D11Texture2D_Release(src_texture);
    return hr;
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
        !wcscmp(class_name, L"static") || !wcscmp(class_name, L"Message"))
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

static void do_composite_dxgi_surface(const struct composition_target *target,
                                      const struct composition_visual *visual,
                                      IDXGISurface *dxgi_surface)
{
    ID3DDeviceContextState *state = NULL, *old_state = NULL;
    ID3D11DeviceContext1 *d3d11_device_context1 = NULL;
    ID3D11DeviceContext *d3d11_device_context = NULL;
    ID3D11Multithread *multithread = NULL;
    D2D1_BITMAP_PROPERTIES1 bitmap_desc;
    ID2D1Bitmap1 *target_bitmap = NULL;
    ID3D11Device1 *d3d11_device = NULL;
    IDXGISurface *bgra_surface = NULL;
    DXGI_SURFACE_DESC surface_desc;
    ID2D1Bitmap *src_bitmap = NULL;
    ID2D1Image *old_target = NULL;
    ID2D1DeviceContext *d2d_context = NULL, *temp_context = NULL;
    ID2D1GdiInteropRenderTarget *interop = NULL, *temp_interop = NULL;
    ID2D1Device *d2d_device = NULL;
    IDXGIDevice *dxgi_dev = NULL;
    HWND root_hwnd = NULL;
    POINT pt = {0, 0};
    const struct composition_visual *v;
    HDC dst_dc, src_dc;
    D2D1_SIZE_U size;
    HRESULT hr;

    static const D3D_FEATURE_LEVEL feature_levels[] =
    {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };

    if (!target->root)
    {
        ERR("Target has no root.\n");
        goto done;
    }

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

    ID3D11Multithread_SetMultithreadProtected(multithread, TRUE);
    ID3D11Multithread_Enter(multithread);

    /* Is this really necessary? anyway, just to be safe */
    if (FAILED(hr = ID3D11Device1_CreateDeviceContextState(d3d11_device, 0, feature_levels,
                                                           ARRAY_SIZE(feature_levels), D3D11_SDK_VERSION,
                                                           &IID_ID3D11Device1, NULL, &state)))
    {
        ERR("Failed to create device context state, hr %#lx.\n", hr);
        goto done;
    }
    ID3D11DeviceContext1_SwapDeviceContextState(d3d11_device_context1, state, &old_state);

    if (FAILED(hr = IDXGISurface_GetDesc(dxgi_surface, &surface_desc)))
    {
        ERR("Failed to get the dxgi surface description, hr %#lx.\n", hr);
        goto done;
    }

    /* Convert DXGI_FORMAT_R8G8B8A8_UNORM to DXGI_FORMAT_B8G8R8A8_UNORM */
    if (surface_desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM)
    {
        hr = create_bgra_surface_from_rgba(dxgi_surface, &bgra_surface);
        if (FAILED(hr))
        {
            ERR("Failed to convert DXGI_FORMAT_R8G8B8A8_UNORM surface to DXGI_FORMAT_B8G8R8A8_UNORM, hr %#lx.\n", hr);
            goto done;
        }
        dxgi_surface = bgra_surface;
    }
    else if (surface_desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT)
    {
        ERR("DXGI_FORMAT_R16G16B16A16_FLOAT surface is currently unsupported.\n");
        goto done;
    }

    size.width = surface_desc.Width;
    size.height = surface_desc.Height;
    bitmap_desc.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
    bitmap_desc.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
    bitmap_desc.dpiX = 0;
    bitmap_desc.dpiY = 0;
    bitmap_desc.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_GDI_COMPATIBLE;
    bitmap_desc.colorContext = NULL;

    d2d_context = visual->device_context;
    interop = visual->interop;

    if (!d2d_context || !interop)
    {
        if (SUCCEEDED(IDXGISurface_GetDevice(dxgi_surface, &IID_IDXGIDevice, (void **)&dxgi_dev)))
        {
            if (SUCCEEDED(D2D1CreateDevice(dxgi_dev, NULL, &d2d_device)))
            {
                if (SUCCEEDED(ID2D1Device_CreateDeviceContext(d2d_device, D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &temp_context)))
                {
                    if (SUCCEEDED(ID2D1DeviceContext_QueryInterface(temp_context, &IID_ID2D1GdiInteropRenderTarget, (void **)&temp_interop)))
                    {
                        d2d_context = temp_context;
                        interop = temp_interop;
                    }
                }
                ID2D1Device_Release(d2d_device);
            }
            IDXGIDevice_Release(dxgi_dev);
        }
    }

    if (!d2d_context || !interop)
    {
        ERR("Failed to get D2D device context or interop for composite\n");
        goto done;
    }

    if (FAILED(hr = ID2D1DeviceContext_CreateBitmap(d2d_context, size, NULL, 0,
            &bitmap_desc, &target_bitmap)))
    {
        ERR("Failed to create a bitmap, hr %#lx.\n", hr);
        goto done;
    }

    if (FAILED(hr = ID2D1DeviceContext_CreateSharedBitmap(d2d_context, &IID_IDXGISurface,
            dxgi_surface, NULL, &src_bitmap)))
    {
        ERR("Failed to create a bitmap from dxgi surface, hr %#lx.\n", hr);
        goto done;
    }

    if (FAILED(hr = ID2D1Bitmap1_CopyFromBitmap(target_bitmap, NULL, src_bitmap, NULL)))
    {
        ERR("Failed to copy bitmap, hr %#lx.\n", hr);
        goto done;
    }

    ID2D1DeviceContext_GetTarget(d2d_context, &old_target);
    ID2D1DeviceContext_SetTarget(d2d_context, (ID2D1Image *)target_bitmap);
    if (FAILED(hr = ID2D1GdiInteropRenderTarget_GetDC(interop, D2D1_DC_INITIALIZE_MODE_COPY,
            &src_dc)))
    {
        ERR("GetDC failed, hr %#lx.\n", hr);
        ID2D1DeviceContext_SetTarget(d2d_context, old_target);
        if (old_target)
            ID2D1Image_Release(old_target);
        goto done;
    }

    if (target->hwnd && IsWindow(target->hwnd) && IsWindowVisible(target->hwnd))
    {
        WCHAR class_name[64];
        RealGetWindowClassW(target->hwnd, class_name, ARRAY_SIZE(class_name));
        if (wcscmp(class_name, L"static") != 0)
        {
            root_hwnd = GetAncestor(target->hwnd, GA_ROOT);
            if (!root_hwnd)
                root_hwnd = target->hwnd;

            if (root_hwnd != target->hwnd)
                MapWindowPoints(target->hwnd, root_hwnd, &pt, 1);
        }
    }

    if (!root_hwnd || !IsWindow(root_hwnd))
        root_hwnd = find_root_top_level_window();

    {
        unsigned int depth = 0;
        for (v = visual; v && depth < 256; v = v->parent, depth++)
        {
            pt.x += (LONG)v->offset_x;
            pt.y += (LONG)v->offset_y;
        }
    }

    if (root_hwnd)
    {
        dst_dc = GetDC(root_hwnd);
        if (dst_dc)
        {
            BitBlt(dst_dc, pt.x, pt.y, size.width, size.height, src_dc, 0, 0, SRCCOPY);
            TRACE("DCOMP: blitted frame %ux%u to root_hwnd %p (target hwnd %p at %ld,%ld)\n",
                (unsigned int)size.width, (unsigned int)size.height, root_hwnd, target->hwnd, pt.x, pt.y);
            ReleaseDC(root_hwnd, dst_dc);
        }
        else
        {
            ERR("Failed to get DC for root_hwnd %p\n", root_hwnd);
        }
    }

    ID2D1GdiInteropRenderTarget_ReleaseDC(interop, NULL);
    ID2D1DeviceContext_SetTarget(d2d_context, old_target);
    if (old_target)
        ID2D1Image_Release(old_target);

done:
    if (temp_interop)
        ID2D1GdiInteropRenderTarget_Release(temp_interop);
    if (temp_context)
        ID2D1DeviceContext_Release(temp_context);
    if (bgra_surface)
        IDXGISurface_Release(bgra_surface);
    if (src_bitmap)
        ID2D1Bitmap_Release(src_bitmap);
    if (target_bitmap)
        ID2D1Bitmap1_Release(target_bitmap);
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
}

static struct composition_visual * shared_visual_target_get_root(HANDLE shared_visual_handle)
{
    struct composition_visual *visual = NULL;
    NTSTATUS status;

    SERVER_START_REQ(dcomp_get_shared_visual_info)
    {
        req->handle = wine_server_obj_handle(shared_visual_handle);
        if (!(status = wine_server_call(req)))
            visual = impl_from_IDCompositionVisual(wine_server_get_ptr(reply->target_root));
    }
    SERVER_END_REQ;

    if (status)
        ERR("dcomp_get_shared_visual_info failed, not in the same process? status %#lx.\n", status);

    return visual;
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

        if (target_root && shared_visual_target_get_root(visual->shared_visual_handle) == target_root)
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

    root = shared_visual_target_get_root(shared_handle);
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

static HRESULT do_composite(const struct composition_target *target, struct composition_visual *visual)
{
    struct composition_visual *child_visual;
    IDXGISurface *dxgi_surface = NULL;
    BOOL rendered_any = FALSE;
    HRESULT hr;

    if (!visual)
        return E_POINTER;

    TRACE("DCOMP: do_composite visual %p, shared_handle %p, content %p, has_children %d\n",
        visual, visual->shared_visual_handle, visual->content, !list_empty(&visual->child_visuals));

    if (visual->shared_visual_handle)
    {
        struct composition_visual *child_root = resolve_shared_visual_root(visual->shared_visual_handle);
        TRACE("DCOMP: do_composite visual %p shared_handle %p -> child_root %p\n",
            visual, visual->shared_visual_handle, child_root);
        if (child_root && child_root != visual)
        {
            struct composition_visual *old_parent = child_root->parent;
            child_root->parent = visual;
            hr = do_composite(target, child_root);
            child_root->parent = old_parent;
            if (SUCCEEDED(hr))
                rendered_any = TRUE;
        }

        if (!rendered_any && !visual_tree_has_content(child_root) &&
            g_last_rendered_surface && g_last_rendered_surface->physical_surface)
        {
            IDXGISurface *surface = NULL;
            if (SUCCEEDED(IUnknown_QueryInterface(g_last_rendered_surface->physical_surface,
                    &IID_IDXGISurface, (void **)&surface)))
            {
                TRACE("DCOMP: fallback blitting g_last_rendered_surface %p to hwnd %p\n",
                    g_last_rendered_surface, target->hwnd);
                do_composite_dxgi_surface(target, visual, surface);
                IDXGISurface_Release(surface);
                rendered_any = TRUE;
            }
        }
    }

    /* Render content */
    if (visual->content)
    {
        IDCompositionSurface *surface = NULL;
        IDXGISwapChain1 *swapchain = NULL;
        IDXGISwapChain *swapchain_v1 = NULL;
        ID3D11Texture2D *texture = NULL;
        ID3D11Resource *d3d_res = NULL;
        IDXGIResource *dxgi_res = NULL;
        dxgi_surface = NULL;

        TRACE("DCOMP: do_composite visual %p has content %p iid %s\n",
            visual, visual->content, debugstr_guid(&visual->content_iid));

        if (SUCCEEDED(IUnknown_QueryInterface(visual->content, &IID_IDCompositionSurface, (void **)&surface)))
        {
            struct composition_surface *surface_impl = unsafe_impl_from_IDCompositionSurface(surface);
            if (surface_impl)
            {
                if (surface_impl->physical_surface)
                    IUnknown_QueryInterface(surface_impl->physical_surface, &IID_IDXGISurface, (void **)&dxgi_surface);
                if (!dxgi_surface && surface_impl->draw_surface)
                    ID3D11Texture2D_QueryInterface(surface_impl->draw_surface, &IID_IDXGISurface, (void **)&dxgi_surface);
            }
            IDCompositionSurface_Release(surface);
        }
        else if (SUCCEEDED(IUnknown_QueryInterface(visual->content, &IID_IDXGISwapChain1, (void **)&swapchain)))
        {
            /* The front buffer is the last back buffer because the composition swapchain must
                     * be created with DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL */
            hr = IDXGISwapChain1_GetBuffer(swapchain, 0, &IID_IDXGISurface,
                                           (void **)&dxgi_surface);
            IDXGISwapChain1_Release(swapchain);
            if (FAILED(hr))
            {
                ERR("Failed to get the swapchain front buffer, hr %#lx.\n", hr);
            }
        }
        else if (SUCCEEDED(IUnknown_QueryInterface(visual->content, &IID_IDXGISwapChain, (void **)&swapchain_v1)))
        {
            hr = IDXGISwapChain_GetBuffer(swapchain_v1, 0, &IID_IDXGISurface,
                                          (void **)&dxgi_surface);
            IDXGISwapChain_Release(swapchain_v1);
            if (FAILED(hr))
            {
                ERR("Failed to get the swapchain buffer, hr %#lx.\n", hr);
            }
        }
        else if (SUCCEEDED(IUnknown_QueryInterface(visual->content, &IID_IDXGISurface, (void **)&dxgi_surface)))
        {
            /* direct IDXGISurface */
        }
        else if (SUCCEEDED(IUnknown_QueryInterface(visual->content, &IID_ID3D11Texture2D, (void **)&texture)))
        {
            ID3D11Texture2D_QueryInterface(texture, &IID_IDXGISurface, (void **)&dxgi_surface);
            ID3D11Texture2D_Release(texture);
        }
        else if (SUCCEEDED(IUnknown_QueryInterface(visual->content, &IID_ID3D11Resource, (void **)&d3d_res)))
        {
            ID3D11Resource_QueryInterface(d3d_res, &IID_IDXGISurface, (void **)&dxgi_surface);
            ID3D11Resource_Release(d3d_res);
        }
        else if (SUCCEEDED(IUnknown_QueryInterface(visual->content, &IID_IDXGIResource, (void **)&dxgi_res)))
        {
            IDXGIResource_QueryInterface(dxgi_res, &IID_IDXGISurface, (void **)&dxgi_surface);
            IDXGIResource_Release(dxgi_res);
        }

        if (dxgi_surface)
        {
            do_composite_dxgi_surface(target, visual, dxgi_surface);
            IDXGISurface_Release(dxgi_surface);
            rendered_any = TRUE;
        }
    }

    LIST_FOR_EACH_ENTRY(child_visual, &visual->child_visuals, struct composition_visual, entry)
    {
        if (SUCCEEDED(do_composite(target, child_visual)))
            rendered_any = TRUE;
    }

    return rendered_any ? S_OK : S_FALSE;
}

static DWORD WINAPI composite_thread_proc(void *iface)
{
    struct composition_device *device = impl_from_IDCompositionDevice(iface);
    unsigned int count, frequency, refresh_period;
    struct composition_target *target;
    struct composition_visual *visual;
    HDC hdc;
    static DWORD last_log;

    /* TODO: Implement and use D3DKMTWaitForVerticalBlankEvent() */
    hdc = GetDC(0);
    frequency = hdc ? GetDeviceCaps(hdc, VREFRESH) : 0;
    if (frequency <= 1) frequency = 60;
    refresh_period = 1000 / frequency;
    if (refresh_period == 0) refresh_period = 16;
    if (hdc) ReleaseDC(0, hdc);

    TRACE("DCOMP: composite_thread_proc started, refresh_period %u ms\n", refresh_period);

    while (TRUE)
    {
        DWORD now = GetTickCount();
        BOOL should_log = (now - last_log > 3000);
        if (should_log) last_log = now;

        dcomp_lock();

        count = 0;
        LIST_FOR_EACH_ENTRY(target, &device->targets, struct composition_target, entry)
        {
            if (!target->root && target->shared_visual_handle)
            {
                struct composition_visual *resolved_root = resolve_shared_visual_root(target->shared_visual_handle);
                if (resolved_root)
                {
                    target->root = (IDCompositionVisual *)&resolved_root->IDCompositionVisualUnknown_iface;
                    IDCompositionVisual_AddRef(target->root);
                }
            }

            if (!target->root)
            {
                if (should_log) TRACE("DCOMP: target %p hwnd %p has NO root\n", target, target->hwnd);
                continue;
            }

            if (!target->hwnd || !IsWindow(target->hwnd))
            {
                struct composition_target *host_target = NULL;
                struct composition_visual *container = find_root_visual_container(target, &host_target);
                if (container && host_target && host_target->hwnd && IsWindow(host_target->hwnd))
                {
                    target->hwnd = host_target->hwnd;
                    if (target->root && !impl_from_IDCompositionVisual(target->root)->parent)
                        impl_from_IDCompositionVisual(target->root)->parent = container;
                    if (should_log)
                        TRACE("DCOMP: resolved target %p container visual %p host_target %p hwnd %p\n",
                            target, container, host_target, target->hwnd);
                }
                else
                {
                    target->hwnd = find_root_top_level_window();
                }
            }

            visual = impl_from_IDCompositionVisual(target->root);
            if (should_log) TRACE("DCOMP: tick compositing target %p hwnd %p root %p\n", target, target->hwnd, visual);
            if (SUCCEEDED(do_composite(target, visual)))
                count++;
        }

        if (should_log && count)
            TRACE("DCOMP: composited %u targets\n", count);
        else
            (void)count;

        dcomp_unlock();

        if (device->exit_thread)
            break;

        Sleep(refresh_period);
    }

    device->thread_exited = TRUE;
    return 0;
}

void ensure_composite_thread(struct composition_device *device)
{
    dcomp_lock();
    if (!device->thread || device->thread_exited)
    {
        if (device->thread)
        {
            device->exit_thread = TRUE;
            WaitForSingleObject(device->thread, INFINITE);
            CloseHandle(device->thread);
        }
        device->thread_exited = FALSE;
        device->exit_thread = FALSE;
        device->thread = CreateThread(NULL, 0, composite_thread_proc, &device->IDCompositionDevice_iface, 0, NULL);
        TRACE("DCOMP: started composite_thread_proc %p\n", device->thread);
    }
    dcomp_unlock();
}

static HRESULT STDMETHODCALLTYPE device_Commit(IDCompositionDevice *iface)
{
    struct composition_device *device = impl_from_IDCompositionDevice(iface);
    struct composition_target *t;
    HRESULT hr = S_OK;

    TRACE("DCOMP: device_Commit iface %p\n", iface);

    dcomp_lock();
    LIST_FOR_EACH_ENTRY(t, &device->targets, struct composition_target, entry)
    {
        TRACE("DCOMP: device_Commit device %p has target %p hwnd %p root %p\n",
            device, t, t->hwnd, t->root);
    }
    dcomp_unlock();

    ensure_composite_thread(device);

    return hr;
}

static HRESULT STDMETHODCALLTYPE device_WaitForCommitCompletion(IDCompositionDevice *iface)
{
    TRACE("iface %p\n", iface);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE device_GetFrameStatistics(IDCompositionDevice *iface,
        DCOMPOSITION_FRAME_STATISTICS *statistics)
{
    TRACE("iface %p, statistics %p\n", iface, statistics);
    if (!statistics) return E_POINTER;
    memset(statistics, 0, sizeof(*statistics));
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE device_CreateTargetForHwnd(IDCompositionDevice *iface,
        HWND hwnd, BOOL topmost, IDCompositionTarget **target)
{
    struct composition_device *device = impl_from_IDCompositionDevice(iface);

    TRACE("DCOMP: device_CreateTargetForHwnd iface %p, hwnd %p, topmost %d, target %p\n", iface, hwnd, topmost, target);

    return create_target(device, hwnd, topmost, target);
}

static HRESULT STDMETHODCALLTYPE device_CreateVisual(IDCompositionDevice *iface,
        IDCompositionVisual **visual)
{
    HRESULT hr = create_visual(1, &IID_IDCompositionVisual, (void **)visual);
    TRACE("DCOMP: device_CreateVisual iface %p, out_visual %p, hr %#lx\n", iface, *visual, hr);
    return hr;
}

static HRESULT get_default_rendering_device(IUnknown **out_device)
{
    static HMODULE d3d11_mod = NULL;
    static HRESULT (WINAPI *pD3D11CreateDevice)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT,
            const D3D_FEATURE_LEVEL *, UINT, UINT, ID3D11Device **, D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);
    ID3D11Device *d3d11_device = NULL;
    HRESULT hr = E_FAIL;

    if (!d3d11_mod)
    {
        d3d11_mod = LoadLibraryA("d3d11.dll");
        if (d3d11_mod)
            pD3D11CreateDevice = (void *)GetProcAddress(d3d11_mod, "D3D11CreateDevice");
    }

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

static HRESULT composition_device_create_surface(struct composition_device *device,
        UINT width, UINT height, DXGI_FORMAT pixel_format, DXGI_ALPHA_MODE alpha_mode,
        IDCompositionSurface **surface)
{
    HRESULT hr;

    TRACE("DCOMP: CreateSurface device %p, width %u, height %u, format %#x, alpha_mode %#x, surface %p\n",
        device, width, height, pixel_format, alpha_mode, surface);

    if (!surface)
        return E_POINTER;

    *surface = NULL;

    dcomp_lock();

    if (!device->surface_factory)
    {
        IUnknown *rendering_device = NULL;

        hr = device_get_or_create_rendering_device(device, &rendering_device);
        if (FAILED(hr) || !rendering_device)
        {
            dcomp_unlock();
            ERR("Failed to get rendering device for composition device %p, hr %#lx.\n", device, hr);
            return hr;
        }

        hr = create_surface_factory(device, rendering_device, &device->surface_factory);
        IUnknown_Release(rendering_device);
        if (FAILED(hr))
        {
            dcomp_unlock();
            ERR("Failed to create surface factory for composition device %p, hr %#lx.\n", device, hr);
            return hr;
        }
    }

    dcomp_unlock();

    return create_surface(impl_from_IDCompositionSurfaceFactory(device->surface_factory),
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
    FIXME("iface %p, width %u, height %u, format %#x, alpha_mode %#x, surface %p stub!\n", iface,
            width, height, pixel_format, alpha_mode, surface);
    return E_NOTIMPL;
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
        *surface = (IUnknown *)dcomp_surface;
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
    FIXME("iface %p, transforms %p, elements %u, transform_group %p stub!\n", iface, transforms,
            elements, transform_group);
    return E_NOTIMPL;
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
    FIXME("iface %p, transforms_3d %p, elements %u, transform_3d_group %p stub!\n", iface,
            transforms_3d, elements, transform_3d_group);
    return E_NOTIMPL;
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
    TRACE("iface %p, statistics %p\n", iface, statistics);
    if (!statistics) return E_POINTER;
    memset(statistics, 0, sizeof(*statistics));
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE desktop_device_CreateVisual(IDCompositionDeviceUnknown *iface,
        IDCompositionVisual2 **visual)
{
    HRESULT hr = create_visual(2, &IID_IDCompositionVisual2, (void **)visual);
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
    FIXME("iface %p, width %u, height %u, format %#x, alpha_mode %#x, surface %p stub!\n", iface,
            width, height, pixel_format, alpha_mode, surface);
    return E_NOTIMPL;
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
    FIXME("iface %p, transforms %p, elements %u, transform_group %p stub!\n", iface, transforms,
            elements, transform_group);
    return E_NOTIMPL;
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
    FIXME("iface %p, transforms_3d %p, elements %u, transform_3d_group %p stub!\n", iface,
            transforms_3d, elements, transform_3d_group);
    return E_NOTIMPL;
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
        *surface = (IUnknown *)dcomp_surface;
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
            HWND hwnd = find_root_top_level_window();
            if (hwnd)
            {
                struct composition_target *existing_target = NULL, *t;
                dcomp_lock();
                LIST_FOR_EACH_ENTRY(t, &device->targets, struct composition_target, entry)
                {
                    if (t->hwnd == hwnd)
                    {
                        existing_target = t;
                        break;
                    }
                }
                dcomp_unlock();

                if (!existing_target)
                {
                    IDCompositionTarget *target = NULL;
                    create_target(device, hwnd, TRUE, &target);
                    if (target)
                    {
                        struct composition_target *target_impl = impl_from_IDCompositionTarget(target);
                        target_impl->root = (IDCompositionVisual *)*out;
                        IDCompositionVisual_AddRef(target_impl->root);
                        TRACE("Attached shared visual %p to target %p hwnd %p\n", *out, target, hwnd);
                    }
                }
            }
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
            struct composition_target *target_impl = impl_from_IDCompositionTarget((IDCompositionTarget *)*out);
            HWND hwnd = find_root_top_level_window();
            if (hwnd)
            {
                target_impl->hwnd = hwnd;
                TRACE("Bound shared target %p to root_hwnd %p\n", *out, hwnd);
            }
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

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown16(IDCompositionDeviceUnknown *iface, void **parameter1)
{
    FIXME("iface %p parameter1 %p stub!\n", iface, parameter1);

    if (!parameter1)
        return E_INVALIDARG;

    *(DWORD *)parameter1 = 0x7fffffff;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE desktop_device_Unknown17(IDCompositionDeviceUnknown *iface, void **parameter1)
{
    void *ret = __builtin_return_address(0);
    TRACE("DCOMP: desktop_device_Unknown17 iface %p parameter1 %p caller %p\n", iface, parameter1, ret);

    if (!parameter1)
        return E_INVALIDARG;

    *(DWORD *)parameter1 = 0x7fffffff;
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

HRESULT WINAPI DCompositionCreateDevice(IDXGIDevice *dxgi_device, REFIID iid, void **device)
{
    TRACE("%p, %s, %p\n", dxgi_device, debugstr_guid(iid), device);

    if (!IsEqualIID(iid, &IID_IDCompositionDevice))
        return E_NOINTERFACE;

    return create_device(1, (IUnknown *)dxgi_device, iid, device);
}

HRESULT WINAPI DCompositionCreateDevice2(IUnknown *rendering_device, REFIID iid, void **device)
{
    TRACE("DCOMP: DCompositionCreateDevice2 rendering_device %p, iid %s, device %p\n", rendering_device, debugstr_guid(iid), device);

    if (!IsEqualIID(iid, &IID_IUnknown)
            && !IsEqualIID(iid, &IID_IDCompositionDevice)
            && !IsEqualIID(iid, &IID_IDCompositionDevice2)
            && !IsEqualIID(iid, &IID_IDCompositionDevice3)
            && !IsEqualIID(iid, &IID_IDCompositionDesktopDevice)
            && !is_partner_guid(iid))
        return E_NOINTERFACE;

    return create_device(2, rendering_device, iid, device);
}

HRESULT WINAPI DCompositionCreateDevice3(IUnknown *rendering_device, REFIID iid, void **device)
{
    FIXME("%p, %s, %p.\n", rendering_device, debugstr_guid(iid), device);

    if (!IsEqualIID(iid, &IID_IUnknown)
            && !IsEqualIID(iid, &IID_IDCompositionDevice)
            && !IsEqualIID(iid, &IID_IDCompositionDevice2)
            && !IsEqualIID(iid, &IID_IDCompositionDevice3)
            && !IsEqualIID(iid, &IID_IDCompositionDesktopDevice)
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

static LONG g_batch_id_counter = 10;

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
    TRACE("(%u, %p, %lu)\n", count, handles, timeout);
    if (count && handles)
    {
        DWORD ret = WaitForMultipleObjects(count, handles, FALSE, timeout < 16 ? timeout : 16);
        if (ret != WAIT_TIMEOUT) return ret;
    }
    if (g_commit_completion_event)
        SetEvent(g_commit_completion_event);
    Sleep(timeout < 16 ? timeout : 16);
    return WAIT_OBJECT_0;
}
