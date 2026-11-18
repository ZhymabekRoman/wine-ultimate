/* WinRT Windows.UI.Xaml UWP Visual Tree, PRI/XBF Loader, Layout & Input Host
 *
 * Copyright (C) 2026 Wine project
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <wctype.h>
#include <excpt.h>
#include "wine/exception.h"
#include "private.h"
#include "xbf_known_types.h"
#include "appmodel.h"
#include "weakreference.h"
#include "winreg.h"
#include "wincodec.h"
#define WIDL_using_Windows_ApplicationModel
#define WIDL_using_Windows_Management_Deployment
#include "windows.management.deployment.h"
#define WIDL_using_Windows_UI_Composition
#include "windows.ui.composition.h"
#include "uwp_private.h"
WINE_DEFAULT_DEBUG_CHANNEL(xaml);

HRESULT WINAPI uwp_brush_get_Opacity( void *iface, double *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (val) *val = impl ? impl->brush_opacity : 1.0;
    return S_OK;
}
static HRESULT WINAPI uwp_brush_put_Opacity( void *iface, double val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl) impl->brush_opacity = val;
    return S_OK;
}
const void *uwp_brush_vtbl[16] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_brush_get_Opacity,
    [7] = uwp_brush_put_Opacity,
    [8 ... 15] = uwp_generic_noop,
};

/* Ensure the w x h 32bpp top-down BGRA DIB backing st->dc exists, (re)allocating
 * it on size change.  Shared by the CPU renderer and the GPU readback blit. */
static BOOL uwp_island_ensure_dib( struct uwp_island_state *st, HDC hdc, int w, int h )
{
    BITMAPINFO bmi;
    void *bits = NULL;

    if (st->dc && st->bmp && st->w == w && st->h == h) return TRUE;

    if (st->dc)
    {
        if (st->old_bmp) SelectObject( st->dc, st->old_bmp );
        if (st->bmp) DeleteObject( st->bmp );
        DeleteDC( st->dc );
        st->dc = NULL;
        st->bmp = NULL;
        st->old_bmp = NULL;
        st->bits = NULL;
    }
    st->dc = CreateCompatibleDC( hdc );
    memset( &bmi, 0, sizeof(bmi) );
    bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    st->bmp = CreateDIBSection( st->dc, &bmi, DIB_RGB_COLORS, &bits, NULL, 0 );
    st->bits = bits;
    st->old_bmp = st->bmp ? (HBITMAP)SelectObject( st->dc, st->bmp ) : NULL;
    st->w = w;
    st->h = h;
    return st->dc && st->bmp;
}

/* WS_CHILD islands live under the X11 dummy parent, so their HWND swap chain
 * presents offscreen.  Render them into a w x h render-target texture and keep a
 * staging copy around for the CPU readback that feeds the child window's DC. */
static BOOL uwp_island_ensure_offscreen( struct uwp_island_state *st, int w, int h )
{
    D3D11_TEXTURE2D_DESC desc = {0};
    HRESULT hr;

    if (st->rt_tex && st->rtv && st->staging_tex && st->gpu_w == w && st->gpu_h == h)
        return TRUE;

    xaml_gpu_release_rtv();
    if (st->rtv) { ID3D11RenderTargetView_Release( st->rtv ); st->rtv = NULL; }
    if (st->rt_tex) { ID3D11Texture2D_Release( st->rt_tex ); st->rt_tex = NULL; }
    if (st->staging_tex) { ID3D11Texture2D_Release( st->staging_tex ); st->staging_tex = NULL; }

    desc.Width = (UINT)w;
    desc.Height = (UINT)h;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    hr = ID3D11Device_CreateTexture2D( g_island_d3d11_dev, &desc, NULL, &st->rt_tex );
    if (FAILED(hr) || !st->rt_tex)
    {
        WARN( "uwp_island_ensure_offscreen: RT texture %dx%d failed hr=%#lx\n", w, h, hr );
        st->gpu_failed = TRUE;
        return FALSE;
    }
    hr = ID3D11Device_CreateRenderTargetView( g_island_d3d11_dev, (ID3D11Resource *)st->rt_tex, NULL, &st->rtv );
    if (FAILED(hr) || !st->rtv)
    {
        WARN( "uwp_island_ensure_offscreen: RTV failed hr=%#lx\n", hr );
        st->gpu_failed = TRUE;
        return FALSE;
    }
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    hr = ID3D11Device_CreateTexture2D( g_island_d3d11_dev, &desc, NULL, &st->staging_tex );
    if (FAILED(hr) || !st->staging_tex)
    {
        WARN( "uwp_island_ensure_offscreen: staging texture failed hr=%#lx\n", hr );
        st->gpu_failed = TRUE;
        return FALSE;
    }
    st->gpu_w = w;
    st->gpu_h = h;
    return TRUE;
}

/* RT -> staging -> st->bits, ready for the caller's BitBlt onto the child DC. */
static BOOL uwp_island_download_gpu( struct uwp_island_state *st, HDC hdc, int w, int h )
{
    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr;

    if (!st->staging_tex || !st->rt_tex || !g_island_d3d11_ctx) return FALSE;
    if (!uwp_island_ensure_dib( st, hdc, w, h )) return FALSE;

    ID3D11DeviceContext_CopyResource( g_island_d3d11_ctx, (ID3D11Resource *)st->staging_tex,
                                      (ID3D11Resource *)st->rt_tex );
    hr = ID3D11DeviceContext_Map( g_island_d3d11_ctx, (ID3D11Resource *)st->staging_tex,
                                  0, D3D11_MAP_READ, 0, &mapped );
    if (FAILED(hr))
    {
        WARN( "uwp_island_download_gpu: Map failed hr=%#lx\n", hr );
        return FALSE;
    }
    if (mapped.RowPitch >= (UINT)(w * 4))
    {
        const BYTE *src = (const BYTE *)mapped.pData;
        BYTE *dst = (BYTE *)st->bits;
        int y;
        for (y = 0; y < h; y++)
            memcpy( dst + (size_t)y * w * 4, src + (size_t)y * mapped.RowPitch, (size_t)w * 4 );
    }
    ID3D11DeviceContext_Unmap( g_island_d3d11_ctx, (ID3D11Resource *)st->staging_tex, 0 );
    return TRUE;
}

static BOOL uwp_island_ensure_gpu( HWND hwnd, struct uwp_island_state *st, int w, int h )
{
    static const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
    IDXGIDevice *dxgi_dev = NULL;
    IDXGIAdapter *dxgi_adapter = NULL;
    IDXGIFactory2 *dxgi_factory = NULL;
    HRESULT hr;

    if (st->gpu_failed || g_island_d3d11_failed || w <= 0 || h <= 0) return FALSE;
    st->is_child = (GetWindowLongW( hwnd, GWL_STYLE ) & WS_CHILD) != 0;
    if (!g_island_d3d11_dev)
    {
        D3D_FEATURE_LEVEL fl;
        hr = D3D11CreateDevice( NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                levels, ARRAY_SIZE(levels), D3D11_SDK_VERSION,
                                &g_island_d3d11_dev, &fl, &g_island_d3d11_ctx );
        if (FAILED(hr))
        {
            hr = D3D11CreateDevice( NULL, D3D_DRIVER_TYPE_WARP, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                    levels, ARRAY_SIZE(levels), D3D11_SDK_VERSION,
                                    &g_island_d3d11_dev, &fl, &g_island_d3d11_ctx );
        }
        if (FAILED(hr) || !g_island_d3d11_dev || !g_island_d3d11_ctx)
        {
            g_island_d3d11_failed = TRUE;
            return FALSE;
        }
    }
    if (st->is_child) return uwp_island_ensure_offscreen( st, w, h );
    if (!st->swapchain)
    {
        DXGI_SWAP_CHAIN_DESC1 desc = {0};
        desc.Width = (UINT)w;
        desc.Height = (UINT)h;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 1;
        desc.Scaling = DXGI_SCALING_STRETCH;
        desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        desc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;

        hr = ID3D11Device_QueryInterface( g_island_d3d11_dev, &IID_IDXGIDevice, (void **)&dxgi_dev );
        if (SUCCEEDED(hr) && dxgi_dev)
        {
            hr = IDXGIDevice_GetAdapter( dxgi_dev, &dxgi_adapter );
            if (SUCCEEDED(hr) && dxgi_adapter)
            {
                hr = IDXGIAdapter_GetParent( dxgi_adapter, &IID_IDXGIFactory2, (void **)&dxgi_factory );
                if (SUCCEEDED(hr) && dxgi_factory)
                {
                    hr = IDXGIFactory2_CreateSwapChainForHwnd( dxgi_factory, (IUnknown *)g_island_d3d11_dev,
                                                               hwnd, &desc, NULL, NULL, &st->swapchain );
                    IDXGIFactory2_Release( dxgi_factory );
                }
                IDXGIAdapter_Release( dxgi_adapter );
            }
            IDXGIDevice_Release( dxgi_dev );
        }
        if (!st->swapchain)
        {
            st->gpu_failed = TRUE;
            return FALSE;
        }
        st->gpu_w = w;
        st->gpu_h = h;
    }
    else if (st->gpu_w != w || st->gpu_h != h)
    {
        xaml_gpu_release_rtv();
        if (SUCCEEDED(IDXGISwapChain1_ResizeBuffers( st->swapchain, 0, (UINT)w, (UINT)h, DXGI_FORMAT_UNKNOWN, 0 )))
        {
            st->gpu_w = w;
            st->gpu_h = h;
        }
    }
    return st->swapchain != NULL;
}

static LRESULT CALLBACK uwp_island_wndproc( HWND hwnd, UINT msg, WPARAM wp, LPARAM lp )
{
    struct uwp_element *impl = (struct uwp_element *)GetWindowLongPtrW( hwnd, GWLP_USERDATA );
    struct uwp_island_state *st = (struct uwp_island_state *)GetPropW( hwnd, L"UWP_ISLAND_STATE" );
    if (msg == WM_NCCREATE)
    {
        CREATESTRUCTW *cs = (CREATESTRUCTW *)lp;
        SetWindowLongPtrW( hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams );
        st = calloc( 1, sizeof(*st) );
        if (st) SetPropW( hwnd, L"UWP_ISLAND_STATE", (HANDLE)st );
        return DefWindowProcW( hwnd, msg, wp, lp );
    }
    if (msg == WM_NCDESTROY)
    {
        if (st)
        {
            RemovePropW( hwnd, L"UWP_ISLAND_STATE" );
            if (st->swapchain)
            {
                xaml_gpu_release_rtv();
                IDXGISwapChain1_Release( st->swapchain );
            }
            if (st->rtv)
            {
                if (st->is_child) xaml_gpu_release_rtv();
                ID3D11RenderTargetView_Release( st->rtv );
                st->rtv = NULL;
            }
            if (st->rt_tex) { ID3D11Texture2D_Release( st->rt_tex ); st->rt_tex = NULL; }
            if (st->staging_tex) { ID3D11Texture2D_Release( st->staging_tex ); st->staging_tex = NULL; }
            if (st->dc)
            {
                if (st->old_bmp) SelectObject( st->dc, st->old_bmp );
                if (st->bmp) DeleteObject( st->bmp );
                DeleteDC( st->dc );
            }
            free( st );
        }
        return DefWindowProcW( hwnd, msg, wp, lp );
    }
    if (msg == WM_ERASEBKGND)
        return 1;
    if (msg == WM_WINDOWPOSCHANGING && lp && impl && impl->content)
    {
        WINDOWPOS *pos = (WINDOWPOS *)lp;
        if (!(pos->flags & SWP_NOZORDER) && pos->hwndInsertAfter == HWND_BOTTOM &&
            uwp_has_open_teachingtip( uwp_elem_from_any( impl->content ) ))
            pos->hwndInsertAfter = HWND_TOP;
    }
    if (msg == WM_SIZE)
    {
        if (st) st->valid = FALSE;
        InvalidateRect( hwnd, NULL, FALSE );
        return 0;
    }
    if (msg == WM_PAINT)
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint( hwnd, &ps );
        if (hdc && impl && impl->content)
        {
            RECT rc;
            int w, h;
            GetClientRect( hwnd, &rc );
            w = rc.right - rc.left;
            h = rc.bottom - rc.top;
            if (w > 0 && h > 0)
            {
                BOOL gpu_ok = FALSE;
                BOOL is_child = FALSE;

                if (st && uwp_island_ensure_gpu( hwnd, st, w, h ))
                {
                    is_child = st->is_child;
                    if (!is_child)
                    {
                        gpu_ok = xaml_layout_render_gpu( hwnd, w, h, impl->content,
                                                         g_island_d3d11_dev, g_island_d3d11_ctx, st->swapchain );
                    }
                    else if (g_layout_dirty || !st->valid || w != st->w || h != st->h)
                    {
                        /* Offscreen GPU frame -> staging -> DIB -> BitBlt below. */
                        gpu_ok = xaml_layout_render_gpu_rtv( hwnd, w, h, impl->content,
                                                             g_island_d3d11_dev, g_island_d3d11_ctx, st->rtv )
                                 && uwp_island_download_gpu( st, hdc, w, h );
                    }
                    else
                    {
                        /* Static frame: reuse the DIB, skip D3D11 entirely. */
                        gpu_ok = TRUE;
                    }
                    if (gpu_ok)
                    {
                        g_layout_dirty = FALSE;
                        st->valid = TRUE;
                        if (is_child && st->dc)
                            BitBlt( hdc, 0, 0, w, h, st->dc, 0, 0, SRCCOPY );
                    }
                    else
                    {
                        st->gpu_failed = TRUE;
                    }
                }
                if (!gpu_ok)
                {
                    if (st && (g_layout_dirty || !st->valid || w != st->w || h != st->h))
                    {
                        if (uwp_island_ensure_dib( st, hdc, w, h ))
                        {
                            xaml_layout_render( hwnd, st->dc, w, h, impl->content );
                            g_layout_dirty = FALSE;
                            st->valid = TRUE;
                        }
                    }
                    if (st && st->valid && st->dc)
                        BitBlt( hdc, 0, 0, w, h, st->dc, 0, 0, SRCCOPY );
                    else
                        xaml_layout_render( hwnd, hdc, w, h, impl->content );
                }
            }
        }
        EndPaint( hwnd, &ps );
        return 0;
    }
    if (msg == WM_MOUSELEAVE)
    {
        if (g_hovered_elem)
        {
            g_hovered_elem->is_hovered = FALSE;
            g_hovered_elem = NULL;
            InvalidateRect( hwnd, NULL, FALSE );
        }
        return 0;
    }
    if ((msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP || msg == WM_MOUSEMOVE) && impl && impl->content)
    {
        RECT rc;
        GetClientRect( hwnd, &rc );
        if (msg == WM_MOUSEMOVE)
        {
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent( &tme );
        }
        if (msg == WM_LBUTTONDOWN)
            TRACE( "uwp_island_wndproc: LBUTTONDOWN hwnd=%p (%d,%d) size=%dx%d\n",
                   hwnd, (short)LOWORD(lp), (short)HIWORD(lp),
                   (int)(rc.right - rc.left), (int)(rc.bottom - rc.top) );
        uwp_handle_mouse_input( hwnd, msg, (short)LOWORD(lp), (short)HIWORD(lp),
                                rc.right - rc.left, rc.bottom - rc.top, impl->content );
        return 0;
    }
    if ((msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) && wp == VK_ESCAPE && uwp_popup_handle_escape())
        return 0;
    if ((msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) && impl && impl->content)
    {
        struct uwp_element *root_el = uwp_elem_from_any( impl->content );
        if (uwp_dispatch_keydown( hwnd, (UINT)wp, lp, root_el ))
        {
            InvalidateRect( hwnd, NULL, FALSE );
            uwp_invalidate_window();
            return 0;
        }
    }
    return DefWindowProcW( hwnd, msg, wp, lp );
}

static HRESULT WINAPI uwp_dwxs_AttachToWindow( void *iface, HWND parent_wnd )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    static BOOL cls_reg = FALSE;
    if (!impl) return E_NOINTERFACE;
    if (!cls_reg)
    {
        WNDCLASSEXW wc = { .cbSize = sizeof(wc) };
        wc.lpfnWndProc = uwp_island_wndproc;
        wc.hInstance = GetModuleHandleW( NULL );
        wc.hCursor = LoadCursorW( NULL, (LPCWSTR)IDC_ARROW );
        wc.lpszClassName = L"DesktopWindowXamlSourceWindow";
        RegisterClassExW( &wc );
        cls_reg = TRUE;
    }
    if (!impl->island_hwnd && parent_wnd)
    {
        impl->island_hwnd = CreateWindowExW( 0, L"DesktopWindowXamlSourceWindow", L"",
                                             WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                                             0, 0, 800, 40, parent_wnd, NULL, GetModuleHandleW( NULL ), impl );
        if (impl->island_hwnd && g_island_hwnd_cnt < (int)ARRAY_SIZE(g_island_hwnds))
            g_island_hwnds[g_island_hwnd_cnt++] = impl->island_hwnd;
        TRACE( "uwp_dwxs_AttachToWindow: parent=%p -> island_hwnd=%p\n", parent_wnd, impl->island_hwnd );
    }
    return S_OK;
}

static HRESULT WINAPI uwp_dwxs_get_WindowHandle( void *iface, HWND *out_hwnd )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out_hwnd) return E_POINTER;
    *out_hwnd = impl ? impl->island_hwnd : NULL;
    return S_OK;
}

static BOOL uwp_is_any_island_hwnd( HWND hwnd )
{
    int i;
    if (!hwnd) return FALSE;
    for (i = 0; i < g_island_hwnd_cnt; i++)
    {
        if (g_island_hwnds[i] == hwnd || (g_island_hwnds[i] && IsChild( g_island_hwnds[i], hwnd )))
            return TRUE;
    }
    return FALSE;
}

static HRESULT WINAPI uwp_dwxs_PreTranslateMessage( void *iface, const MSG *msg, BOOL *result )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    BOOL handled = FALSE;

    if (msg && (msg->message == WM_KEYDOWN || msg->message == WM_SYSKEYDOWN) &&
        msg->wParam == VK_ESCAPE && uwp_popup_handle_escape())
    {
        if (result) *result = TRUE;
        return S_OK;
    }

    if (msg && (msg->message == WM_KEYDOWN || msg->message == WM_SYSKEYDOWN) && impl && impl->content)
    {
        if (!msg->hwnd || msg->hwnd == impl->island_hwnd ||
            (impl->island_hwnd && IsChild( impl->island_hwnd, msg->hwnd )) ||
            !uwp_is_any_island_hwnd( msg->hwnd ))
        {
            struct uwp_element *root_el = uwp_elem_from_any( impl->content );
            handled = uwp_dispatch_keydown( msg->hwnd ? msg->hwnd : impl->island_hwnd,
                                            (UINT)msg->wParam, msg->lParam, root_el );
            if (handled)
            {
                if (impl->island_hwnd) InvalidateRect( impl->island_hwnd, NULL, FALSE );
                uwp_invalidate_window();
            }
        }
    }
    if (result) *result = handled;
    return S_OK;
}

const void *uwp_dwxs_native_vtbl[16] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_dwxs_AttachToWindow,
    [4] = uwp_dwxs_get_WindowHandle,
    [5] = uwp_dwxs_PreTranslateMessage,
    [6 ... 15] = uwp_generic_noop,
};

static HRESULT WINAPI uwp_timer_add_Tick( void *iface, void *handler, EventRegistrationToken *tok )
{
    if (tok) tok->value = 1;
    return S_OK;
}

const void *uwp_timer_vtbl[16] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6 ... 8] = uwp_generic_noop,
    [9] = uwp_timer_add_Tick,
    [10 ... 15] = uwp_generic_noop,
};

/* CoreRT's McgMarshal.ComInterfaceToObject wraps a projected IInspectable in a
 * __ComObject, QI-ing IDispatch in the process.  WinUI XAML types are never
 * late-bound, so an empty dispatch surface is enough to answer the cast. */
/* CoreRT's ComInterfaceToObject() recognises a projected object as "dual"
 * (interface + IDispatch) and only wraps it when GetTypeInfo() hands back a
 * usable ITypeInfo.  A single self-describing type entry is enough. */
static const TYPEATTR uwp_disp_typeattr =
{
    .guid = { 0xc82e1a4c, 0x91a4, 0x4d16, { 0xa7, 0x9f, 0x3a, 0xf4, 0x1d, 0x6c, 0x9b, 0x21 } },
    .lcid = 0x0409,
    .memidConstructor = DISPID_CONSTRUCTOR,
    .memidDestructor = DISPID_DESTRUCTOR,
    .cbSizeInstance = 0,
    .typekind = TKIND_INTERFACE,
    .cFuncs = 1,
    .cImplTypes = 1,
    .cbSizeVft = sizeof(void *),
};
static HRESULT WINAPI uwp_disp_ti_GetTypeAttr( ITypeInfo *iface, TYPEATTR **attr )
{
    if (attr) *attr = (TYPEATTR *)&uwp_disp_typeattr;
    return S_OK;
}
static HRESULT WINAPI uwp_disp_ti_Noop( ITypeInfo *iface, ... ) { return S_OK; }
const ITypeInfoVtbl uwp_disp_ti_vtbl;
static ITypeInfo uwp_disp_typeinfo = { &uwp_disp_ti_vtbl };
static HRESULT WINAPI uwp_disp_ti_GetRefTypeOfImplType( ITypeInfo *iface, UINT idx, HREFTYPE *href )
{
    WARN( "TI-GetRefTypeOfImplType idx=%u\n", idx );
    if (href) *href = 1;
    return idx ? DISP_E_BADINDEX : S_OK;
}
static HRESULT WINAPI uwp_disp_ti_GetRefTypeInfo( ITypeInfo *iface, HREFTYPE href, ITypeInfo **info )
{
    struct uwp_element *impl = uwp_elem_from_any( (IInspectable *)iface );
    if (info) *info = impl ? &impl->ITypeInfo_iface : (ITypeInfo *)&uwp_disp_typeinfo;
    return S_OK;
}
static HRESULT WINAPI uwp_disp_ti_GetDocumentation( ITypeInfo *iface, MEMBERID mid,
        BSTR *name, BSTR *docstr, DWORD *hcontext, BSTR *helpfile )
{
    static WCHAR cached_cls[128];
    static OLECHAR cached_bstr[128];
    static BSTR cached_name = NULL;
    struct uwp_element *impl = uwp_elem_from_any( (IInspectable *)iface );
    const WCHAR *cls = (impl && impl->class_name[0]) ? impl->class_name : L"Windows.UI.Xaml.DependencyObject";
    if (wcscmp( cached_cls, cls ))
    {
        lstrcpyW( cached_cls, cls );
        memcpy( cached_bstr, cls, (wcslen( cls ) + 1) * sizeof(WCHAR) );
    }
    if (!cached_name) cached_name = cached_bstr;
    if (name) *name = cached_name;
    if (docstr) *docstr = NULL;
    if (hcontext) *hcontext = 0;
    if (helpfile) *helpfile = NULL;
    return S_OK;
}
static HRESULT WINAPI uwp_disp_ti_QI( ITypeInfo *iface, REFIID iid, void **out )
{
    return uwp_sub_QueryInterface( (IInspectable *)iface, iid, out );
}
ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface ) { return uwp_sub_AddRef( (IInspectable *)iface ); }
ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface ) { return uwp_sub_Release( (IInspectable *)iface ); }
#define TI_NOOP(...) ((HRESULT (WINAPI *)(ITypeInfo *, __VA_ARGS__))uwp_disp_ti_Noop)
const ITypeInfoVtbl uwp_disp_ti_vtbl =
{
    .QueryInterface = uwp_disp_ti_QI,
    .AddRef = uwp_disp_ti_AddRef,
    .Release = uwp_disp_ti_Release,
    .GetTypeAttr = uwp_disp_ti_GetTypeAttr,
    .GetTypeComp = TI_NOOP(ITypeComp **),
    .GetFuncDesc = TI_NOOP(UINT, FUNCDESC **),
    .GetVarDesc = TI_NOOP(UINT, VARDESC **),
    .GetRefTypeOfImplType = uwp_disp_ti_GetRefTypeOfImplType,
    .GetImplTypeFlags = TI_NOOP(UINT, INT *),
    .GetIDsOfNames = TI_NOOP(OLECHAR **, UINT, MEMBERID *),
    .Invoke = TI_NOOP(void *, MEMBERID, WORD, DISPPARAMS *, VARIANT *, EXCEPINFO *, UINT *),
    .GetDocumentation = uwp_disp_ti_GetDocumentation,
    .GetNames = TI_NOOP(MEMBERID, BSTR *, UINT, UINT *),
    .GetContainingTypeLib = TI_NOOP(ITypeLib **, UINT *),
    .GetRefTypeInfo = uwp_disp_ti_GetRefTypeInfo,
};

HRESULT WINAPI uwp_disp_GetTypeInfoCount( IDispatch *iface, UINT *count )
{
    if (count) *count = 1;
    return S_OK;
}
HRESULT WINAPI uwp_disp_GetTypeInfo( IDispatch *iface, UINT idx, LCID lcid, ITypeInfo **info )
{
    struct uwp_element *impl;
    if (!info) return E_POINTER;
    if (idx) return DISP_E_BADINDEX;
    impl = uwp_elem_from_any( (IInspectable *)iface );
    *info = impl ? &impl->ITypeInfo_iface : (ITypeInfo *)&uwp_disp_typeinfo;
    return S_OK;
}
HRESULT WINAPI uwp_disp_GetIDsOfNames( IDispatch *iface, REFIID riid, LPOLESTR *names,
        UINT count, LCID lcid, DISPID *ids )
{
    UINT i;
    for (i = 0; i < count; i++) if (ids) ids[i] = DISPID_UNKNOWN;
    return DISP_E_UNKNOWNNAME;
}
HRESULT WINAPI uwp_disp_Invoke( IDispatch *iface, DISPID id, REFIID riid, LCID lcid,
        WORD flags, DISPPARAMS *params, VARIANT *result, EXCEPINFO *excep, UINT *argerr )
{
    if (result) V_VT( result ) = VT_EMPTY;
    return S_OK;
}
HRESULT WINAPI uwp_disp_QueryInterface( IDispatch *iface, REFIID iid, void **out )
{
    return uwp_sub_QueryInterface( (IInspectable *)iface, iid, out );
}
ULONG WINAPI uwp_disp_AddRef( IDispatch *iface )
{
    return uwp_sub_AddRef( (IInspectable *)iface );
}
ULONG WINAPI uwp_disp_Release( IDispatch *iface )
{
    return uwp_sub_Release( (IInspectable *)iface );
}
/* IReferenceTrackerTarget: CoreRT registers a reference-tracked RCW peer
 * (answered for QI(IReferenceTracker)) with the GC tracker; the tracker calls
 * AddRefToTracker/ReleaseToTracker/GetStrongValue at vtable slots 5..7. */
struct uwp_rtt_target
{
    IInspectable self;
};
static inline UINT32 cr_to_argb( COLORREF cr )
{
    return 0xff000000u | ((UINT32)GetRValue( cr ) << 16) | ((UINT32)GetGValue( cr ) << 8) | (UINT32)GetBValue( cr );
}

static void uwp_gpu_paint_element( ID3D11DeviceContext *ctx, struct uwp_element *el )
{
    COLORREF surface_bg = g_paint_page_bg;
    TRACE( "paint-enter %s (%s) bounds=%d vis=%u kids=%p content=%p\n",
           debugstr_w(el ? el->class_name : L"?"), debugstr_w(el ? el->name : L"?"),
           el ? el->has_bounds : 0, el ? el->visibility : 0xffff,
           (void *)(el ? el->children_coll : NULL), (void *)(el ? el->content : NULL) );
    if (!el || !el->has_bounds || el->visibility != 0) return;

    if (el->kind == UWP_KIND_IMAGE)
    {
        uwp_load_image_asset( el );
        if (el->img_pixels && el->img_w > 0 && el->img_h > 0)
        {
            RECT rc = el->bounds, dst;
            int bw = rc.right - rc.left, bh = rc.bottom - rc.top;
            int dw = (int)el->img_w, dh = (int)el->img_h;
            int dx, dy;
            if (dw > bw && bw > 0) { dh = MulDiv( dh, bw, dw ); dw = bw; }
            if (dh > bh && bh > 0) { dw = MulDiv( dw, bh, dh ); dh = bh; }
            dx = rc.left + (bw - dw) / 2;
            dy = rc.top + (bh - dh) / 2;
            SetRect( &dst, dx, dy, dx + dw, dy + dh );
            xaml_gpu_push_image( ctx, (UINT64)(ULONG_PTR)el, el->img_pixels, el->img_w, el->img_h, &dst, NULL );
        }
        return;
    }

    if (el->kind == UWP_KIND_PROGRESS_RING)
    {
        RECT rc = el->bounds;
        int cx = (rc.left + rc.right) / 2;
        int cy = min( rc.bottom - 36, (rc.top + rc.bottom) / 2 + 120 );
        int r = 18;
        COLORREF track_cr = uwp_paint_resolve_color( el, L"ControlStrokeColorDefaultBrush", RGB( 68, 68, 68 ), surface_bg );
        COLORREF arc_cr = (el->has_fg && el->fg_alpha > 0)
            ? blend_argb_over( el->fg_argb, surface_bg )
            : uwp_paint_resolve_color( el, L"AccentBrush", uwp_paint_resolve_color( el, L"SystemAccentColor", RGB( 0, 120, 215 ), surface_bg ), surface_bg );
        xaml_gpu_push_ring( (float)(cx - r), (float)(cy - r), (float)(cx + r), (float)(cy + r), NULL, cr_to_argb( track_cr ), 3.0f, FALSE );
        xaml_gpu_push_ring( (float)(cx - r), (float)(cy - r), (float)(cx + r), (float)(cy + r), NULL, cr_to_argb( arc_cr ), 3.0f, TRUE );
        return;
    }

    if (wcsstr( el->name, L"NavRail" ))
    {
        RECT rc = el->bounds;
        COLORREF rail_bg = (el->has_bg && el->bg_alpha > 0)
            ? blend_argb_over( el->bg_argb, surface_bg )
            : uwp_paint_resolve_color( el, L"BodyBackgroundBrush", RGB( 22, 22, 22 ), surface_bg );
        COLORREF rail_bd = (el->has_border && el->border_alpha > 0)
            ? blend_argb_over( el->border_argb, surface_bg )
            : uwp_paint_resolve_color( el, L"BodyBackgroundStroke", RGB( 38, 38, 38 ), surface_bg );
        xaml_gpu_push_rect( (float)rc.left, (float)rc.top, (float)rc.right, (float)rc.bottom, NULL,
                            0.0f, cr_to_argb( rail_bg ), cr_to_argb( rail_bd ), 1.0f, 1.0f );
    }
    else if (wcsstr( el->class_name, L"AppTitleBar" ))
    {
        if (el->text[0])
        {
            RECT rc = el->bounds;
            COLORREF tfg = uwp_paint_resolve_color( el, L"TextFillColorPrimaryBrush", RGB( 255, 255, 255 ), surface_bg );
            RECT trc = { rc.left + 12, rc.top, rc.left + 200, rc.bottom };
            xaml_gpu_push_text( ctx, el->text, 13, FW_SEMIBOLD, DT_LEFT | DT_VCENTER | DT_SINGLELINE,
                                &trc, NULL, tfg, 1.0f );
        }
    }
    else if (wcsstr( el->name, L"ContentFrame1" ))
    {
        RECT rc = el->bounds;
        COLORREF cbg = uwp_paint_resolve_color( el, L"CardBackgroundFillColorDefaultBrush", RGB( 40, 40, 40 ), surface_bg );
        xaml_gpu_push_rect( (float)rc.left, (float)rc.top, (float)rc.right, (float)rc.bottom, NULL,
                            0.0f, cr_to_argb( cbg ), 0, 0.0f, 1.0f );
    }
    else if (wcsstr( el->name, L"SearchBox" ) || wcsstr( el->class_name, L"AutoSuggestBox" ) ||
             wcsstr( el->name, L"SuggestBox" ))
    {
        RECT rc = el->bounds;
        BOOL focused = (g_focused_textbox == el);
        COLORREF sbg = (el->has_bg && el->bg_alpha > 0)
            ? blend_argb_over( el->bg_argb, surface_bg )
            : uwp_paint_resolve_color( el, L"ControlFillColorDefaultBrush", RGB( 42, 42, 42 ), surface_bg );
        COLORREF sbd = focused
            ? uwp_paint_resolve_color( el, L"AccentBrush", RGB( 0, 120, 215 ), surface_bg )
            : uwp_paint_resolve_color( el, L"ControlStrokeColorDefaultBrush", RGB( 90, 90, 90 ), surface_bg );
        COLORREF sfg = el->text[0]
            ? uwp_paint_resolve_color( el, L"PrimaryForegroundBrush", RGB( 255, 255, 255 ), sbg )
            : ((el->has_fg && el->fg_alpha > 0)
                ? blend_argb_over( el->fg_argb, sbg )
                : uwp_paint_resolve_color( el, L"SecondaryForegroundBrush", RGB( 185, 185, 185 ), sbg ));
        WCHAR disp[264];
        const WCHAR *fallback = wcsstr( el->name, L"SearchBox" )
            ? L"\x26b2  Search apps, games, movies and more"
            : L"Find";
        if (el->text[0])
            swprintf( disp, ARRAY_SIZE(disp), focused ? L"\x26b2  %s|" : L"\x26b2  %s", el->text );
        else if (focused)
            lstrcpynW( disp, L"\x26b2  |", ARRAY_SIZE(disp) );
        else
            lstrcpynW( disp, fallback, ARRAY_SIZE(disp) );
        xaml_gpu_push_rect( (float)rc.left, (float)rc.top, (float)rc.right, (float)rc.bottom, NULL,
                            3.0f, cr_to_argb( sbg ), cr_to_argb( sbd ), focused ? 2.0f : 1.0f, 1.0f );
        rc.left += 10;
        xaml_gpu_push_text( ctx, disp, 13, FW_NORMAL, DT_LEFT | DT_VCENTER | DT_SINGLELINE, &rc, NULL, sfg, 1.0f );
        return;
    }
    else if (wcsstr( el->class_name, L"TeachingTip" ) && el->is_open)
    {
        RECT rc = el->bounds;
        COLORREF tbg = (el->has_bg && el->bg_alpha > 0)
            ? blend_argb_over( el->bg_argb, surface_bg )
            : uwp_paint_resolve_color( el, L"SolidBackgroundFillColorTertiaryBrush", RGB( 44, 44, 44 ), surface_bg );
        COLORREF tbd = uwp_paint_resolve_color( el, L"SurfaceStrokeColorFlyoutBrush", RGB( 82, 82, 82 ), surface_bg );
        xaml_gpu_push_rect( (float)rc.left, (float)rc.top, (float)rc.right, (float)rc.bottom, NULL,
                            4.0f, cr_to_argb( tbg ), cr_to_argb( tbd ), 1.0f, 1.0f );
    }
    else if (wcsstr( el->name, L"HeroBanner" ))
    {
        RECT rc = el->bounds, trc;
        COLORREF hbg = uwp_paint_resolve_color( el, L"SystemAccentColorDark2", RGB( 20, 36, 56 ), surface_bg );
        COLORREF hbd = uwp_paint_resolve_color( el, L"AccentBrush", RGB( 0, 120, 215 ), surface_bg );
        COLORREF hfg = uwp_paint_resolve_color( el, L"PrimaryForegroundBrush", RGB( 255, 255, 255 ), hbg );
        COLORREF hsfg = uwp_paint_resolve_color( el, L"AccentTextFillColorPrimaryBrush", RGB( 200, 220, 240 ), hbg );
        xaml_gpu_push_rect( (float)rc.left, (float)rc.top, (float)rc.right, (float)rc.bottom, NULL,
                            6.0f, cr_to_argb( hbg ), cr_to_argb( hbd ), 1.0f, 1.0f );
        trc = rc;
        trc.left += 28; trc.top += 24; trc.right -= 28;
        xaml_gpu_push_text( ctx, L"Featured: Essential Windows Apps", 22, FW_BOLD, DT_LEFT | DT_TOP | DT_SINGLELINE, &trc, NULL, hfg, 1.0f );
        trc.top += 34;
        xaml_gpu_push_text( ctx, L"Discover top-rated tools for communication, productivity, and entertainment.", 13, FW_NORMAL, DT_LEFT | DT_TOP | DT_SINGLELINE, &trc, NULL, hsfg, 1.0f );
        return;
    }
    else if (wcsstr( el->name, L"TileCard" ))
    {
        RECT rc = el->bounds;
        COLORREF cbg = (el->has_bg && el->bg_alpha > 0)
            ? blend_argb_over( el->bg_argb, surface_bg )
            : uwp_paint_resolve_color( el, L"ProductCardBackground", uwp_paint_resolve_color( el, L"CardBackgroundFillColorDefaultBrush", RGB( 38, 38, 38 ), surface_bg ), surface_bg );
        COLORREF cbd = (el->has_border && el->border_alpha > 0)
            ? blend_argb_over( el->border_argb, surface_bg )
            : uwp_paint_resolve_color( el, L"DefaultCardBorderBrush", RGB( 58, 58, 58 ), surface_bg );
        xaml_gpu_push_rect( (float)rc.left, (float)rc.top, (float)rc.right, (float)rc.bottom, NULL,
                            5.0f, cr_to_argb( cbg ), cr_to_argb( cbd ), 1.0f, 1.0f );
    }
    else if (wcsstr( el->name, L"AppIcon" ))
    {
        static const COLORREF colors[] = {
            RGB(37, 211, 102), RGB(30, 215, 96), RGB(229, 9, 20), RGB(0, 122, 204),
            RGB(16, 124, 65), RGB(88, 101, 242), RGB(85, 170, 85), RGB(255, 136, 0)
        };
        RECT rc = el->bounds;
        int idx = (int)(el->bounds.left / 100 + el->bounds.top / 80) % ARRAY_SIZE(colors);
        xaml_gpu_push_rect( (float)rc.left, (float)rc.top, (float)rc.right, (float)rc.bottom, NULL,
                            4.0f, cr_to_argb( colors[idx] ), cr_to_argb( RGB( 70, 70, 70 ) ), 1.0f, 1.0f );
        if (el->text[0])
            xaml_gpu_push_text( ctx, el->text, 16, FW_BOLD, DT_CENTER | DT_VCENTER | DT_SINGLELINE, &rc, NULL, RGB( 255, 255, 255 ), 1.0f );
        return;
    }
    else if (el->kind != UWP_KIND_BUTTON && el->kind != UWP_KIND_TOGGLE_BUTTON && el->kind != UWP_KIND_TEXT_BLOCK)
    {
        if ((el->has_bg && el->bg_alpha > 0) || (el->has_border && el->border_alpha > 0))
        {
            UINT32 fill_c = (el->has_bg && el->bg_alpha > 0) ? cr_to_argb( blend_argb_over( el->bg_argb, surface_bg ) ) : 0;
            UINT32 bord_c = (el->has_border && el->border_alpha > 0) ? cr_to_argb( blend_argb_over( el->border_argb, surface_bg ) ) : 0;
            float bw = (el->has_border && el->border_alpha > 0) ? 1.0f : 0.0f;
            xaml_gpu_push_rect( (float)el->bounds.left, (float)el->bounds.top, (float)el->bounds.right, (float)el->bounds.bottom,
                                NULL, 0.0f, fill_c, bord_c, bw, 1.0f );
        }
        if (wcsstr( el->class_name, L"TabView" ) && !wcsstr( el->class_name, L"TabViewItem" ))
        {
            RECT trc = el->bounds;
            IInspectable *items[64];
            int count = uwp_tabview_get_items( el, items, ARRAY_SIZE(items) );
            int i;
            if (count == 0)
            {
                RECT pill, close_rc, add_rc;
                uwp_tabview_compute_rects( &trc, 1, 0, &pill, &close_rc, &add_rc );
                if (el->text[0])
                {
                    xaml_gpu_push_rect( (float)pill.left, (float)pill.top, (float)pill.right, (float)pill.bottom, NULL,
                                        0.0f, cr_to_argb( RGB( 44, 44, 44 ) ), 0, 0.0f, 1.0f );
                    pill.left += 10;
                    pill.right = max( pill.left, close_rc.left - 2 );
                    xaml_gpu_push_text( ctx, el->text, 13, FW_SEMIBOLD, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, &pill, NULL, RGB( 255, 255, 255 ), 1.0f );
                    xaml_gpu_push_text( ctx, L"\x00d7", 13, FW_SEMIBOLD, DT_CENTER | DT_VCENTER | DT_SINGLELINE, &close_rc, NULL, RGB( 185, 185, 185 ), 1.0f );
                }
                if (add_rc.right > add_rc.left + 12)
                    xaml_gpu_push_text( ctx, L"+", 13, FW_SEMIBOLD, DT_CENTER | DT_VCENTER | DT_SINGLELINE, &add_rc, NULL, RGB( 210, 210, 210 ), 1.0f );
                return;
            }
            for (i = 0; i < count; i++)
            {
                WCHAR tab_title[128] = {0};
                RECT pill, close_rc, add_rc;
                BOOL is_sel = el->selected_item ? (items[i] == el->selected_item) : (i == 0);
                COLORREF bg = is_sel ? RGB( 44, 44, 44 ) : RGB( 28, 28, 28 );
                COLORREF fg = is_sel ? RGB( 255, 255, 255 ) : RGB( 195, 195, 195 );
                uwp_tabview_get_item_title( items[i], tab_title, ARRAY_SIZE(tab_title) );
                uwp_tabview_compute_rects( &trc, count, i, &pill, &close_rc, &add_rc );
                xaml_gpu_push_rect( (float)pill.left, (float)pill.top, (float)pill.right, (float)pill.bottom, NULL,
                                    0.0f, cr_to_argb( bg ), is_sel ? 0 : cr_to_argb( RGB( 54, 54, 54 ) ), is_sel ? 0.0f : 1.0f, 1.0f );
                pill.left += 10;
                pill.right = max( pill.left, close_rc.left - 2 );
                xaml_gpu_push_text( ctx, tab_title, 13, is_sel ? FW_SEMIBOLD : FW_NORMAL, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, &pill, NULL, fg, 1.0f );
                xaml_gpu_push_text( ctx, L"\x00d7", 13, FW_SEMIBOLD, DT_CENTER | DT_VCENTER | DT_SINGLELINE, &close_rc, NULL, RGB( 185, 185, 185 ), 1.0f );
                if (i == count - 1 && add_rc.right > add_rc.left + 12)
                    xaml_gpu_push_text( ctx, L"+", 13, FW_SEMIBOLD, DT_CENTER | DT_VCENTER | DT_SINGLELINE, &add_rc, NULL, RGB( 210, 210, 210 ), 1.0f );
                IInspectable_Release( items[i] );
            }
            return;
        }
        if (wcsstr( el->class_name, L"PivotItem" ) && el->text[0])
        {
            RECT trc = el->bounds;
            BOOL is_first = (wcsstr( el->name, L"History" ) != NULL);
            int fh = max( 13, (int)(15 * g_layout_scale + 0.5f) );
            COLORREF tfg = is_first
                ? uwp_paint_resolve_color( el, L"TextFillColorPrimaryBrush", RGB( 255, 255, 255 ), surface_bg )
                : uwp_paint_resolve_color( el, L"TextFillColorSecondaryBrush", RGB( 175, 175, 175 ), surface_bg );
            xaml_gpu_push_text( ctx, el->text, fh, is_first ? FW_SEMIBOLD : FW_NORMAL, DT_LEFT | DT_VCENTER | DT_SINGLELINE, &trc, NULL, tfg, 1.0f );
            if (is_first)
            {
                COLORREF acc = uwp_paint_resolve_color( el, L"AccentFillColorDefaultBrush",
                                   uwp_paint_resolve_color( el, L"SystemAccentColor", RGB( 0, 120, 215 ), surface_bg ), surface_bg );
                xaml_gpu_push_rect( (float)trc.left, (float)(trc.bottom - 3), (float)min( trc.right, trc.left + 52 ), (float)trc.bottom,
                                    NULL, 0.0f, cr_to_argb( acc ), 0, 0.0f, 1.0f );
            }
            return;
        }
    }

    if (el->kind == UWP_KIND_BUTTON || el->kind == UWP_KIND_TOGGLE_BUTTON)
    {
        RECT brc = el->bounds;
        WCHAR tmp[64];
        const WCHAR *label = uwp_format_display_label( el, tmp, ARRAY_SIZE(tmp) );
        COLORREF bg, fg, bd;
        BOOL draw_bg = TRUE, draw_bd = TRUE;
        int fh;

        if (wcsstr( el->name, L"CopyButton" ) || wcsstr( el->name, L"PasteButton" ))
            return;

        if (wcsstr( el->name, L"NavItem" ))
        {
            RECT rc = el->bounds;
            BOOL selected = (el->is_checked == 1);
            if (selected || el->is_hovered || el->is_pressed)
            {
                COLORREF nav_bg = el->is_pressed ? RGB( 52, 52, 52 ) : (selected ? RGB( 40, 40, 40 ) : RGB( 34, 34, 34 ));
                xaml_gpu_push_rect( (float)(rc.left + 2), (float)(rc.top + 2), (float)(rc.right - 2), (float)(rc.bottom - 2),
                                    NULL, 3.0f, cr_to_argb( nav_bg ), 0, 0.0f, 1.0f );
            }
            if (selected)
            {
                COLORREF accent_cr = uwp_paint_resolve_color( el, L"AccentBrush", uwp_paint_resolve_color( el, L"SystemAccentColor", RGB( 0, 120, 215 ), surface_bg ), surface_bg );
                xaml_gpu_push_rect( (float)rc.left, (float)(rc.top + 4), (float)(rc.left + 3), (float)(rc.bottom - 4),
                                    NULL, 0.0f, cr_to_argb( accent_cr ), 0, 0.0f, 1.0f );
            }
            if (label[0])
            {
                COLORREF nfg = selected
                    ? uwp_paint_resolve_color( el, L"PrimaryForegroundBrush", RGB( 255, 255, 255 ), surface_bg )
                    : uwp_paint_resolve_color( el, L"SecondaryForegroundBrush", RGB( 180, 180, 180 ), surface_bg );
                xaml_gpu_push_text( ctx, label, 12, selected ? FW_SEMIBOLD : FW_NORMAL, DT_CENTER | DT_VCENTER | DT_SINGLELINE, &rc, NULL, nfg, 1.0f );
            }
            return;
        }

        InflateRect( &brc, -2, -2 );
        if (brc.right <= brc.left || brc.bottom <= brc.top) return;

        if (el->has_bg)
        {
            if (el->bg_alpha == 0)
            {
                draw_bg = el->is_pressed || el->is_hovered;
                bg = el->is_pressed ? RGB( 55, 55, 55 ) : (el->is_hovered ? RGB( 45, 45, 45 ) : surface_bg);
            }
            else
            {
                bg = blend_argb_over( el->bg_argb, surface_bg );
                if (el->is_pressed)
                    bg = RGB( max( 0, (int)GetRValue(bg) - 20 ), max( 0, (int)GetGValue(bg) - 20 ), max( 0, (int)GetBValue(bg) - 20 ) );
            }
        }
        else if (wcsstr( el->name, L"GetButton" ) || !wcscmp( label, L"Get" ) || !wcscmp( label, L"Free" ) ||
                 !wcscmp( el->name, L"EqualButton" ) || (!wcscmp( label, L"=" ) && label[1] == 0))
        {
            bg = uwp_paint_resolve_color( el, L"AccentFillColorDefaultBrush",
                     uwp_paint_resolve_color( el, L"AccentBrush", RGB( 0, 120, 215 ), surface_bg ), surface_bg );
            if (el->is_pressed)
                bg = RGB( max( 0, (int)GetRValue(bg) - 25 ), max( 0, (int)GetGValue(bg) - 25 ), max( 0, (int)GetBValue(bg) - 25 ) );
        }
        else if (wcsstr( el->name, L"Num" ) || !wcscmp( el->name, L"DecimalSeparatorButton" ) ||
                 !wcscmp( el->name, L"NegateButton" ))
        {
            bg = uwp_paint_resolve_color( el, L"CalcButtonFillColorDefaultBrush",
                     uwp_paint_resolve_color( el, L"ControlFillColorDefaultBrush", RGB( 59, 59, 59 ), surface_bg ), surface_bg );
            if (el->is_pressed)
                bg = RGB( max( 0, (int)GetRValue(bg) - 14 ), max( 0, (int)GetGValue(bg) - 14 ), max( 0, (int)GetBValue(bg) - 14 ) );
        }
        else
        {
            bg = uwp_paint_resolve_color( el, L"CalcButtonAltFillColorDefaultBrush",
                     uwp_paint_resolve_color( el, L"ControlFillColorDefaultBrush", RGB( 50, 50, 50 ), surface_bg ), surface_bg );
            if (el->is_pressed)
                bg = RGB( max( 0, (int)GetRValue(bg) - 12 ), max( 0, (int)GetGValue(bg) - 12 ), max( 0, (int)GetBValue(bg) - 12 ) );
        }

        if (el->has_border)
        {
            draw_bd = (el->border_alpha > 0);
            bd = blend_argb_over( el->border_argb, surface_bg );
        }
        else if (!draw_bg)
        {
            draw_bd = FALSE;
            bd = surface_bg;
        }
        else
        {
            bd = uwp_paint_resolve_color( el, L"ControlStrokeColorDefaultBrush", RGB( 68, 68, 68 ), surface_bg );
        }

        if (!el->is_enabled)
            fg = uwp_paint_resolve_color( el, L"TextFillColorDisabledBrush", RGB( 120, 120, 120 ), bg );
        else if (el->has_fg && el->fg_alpha > 0)
            fg = blend_argb_over( el->fg_argb, bg );
        else
            fg = uwp_paint_resolve_color( el, L"TextFillColorPrimaryBrush", RGB( 255, 255, 255 ), bg );

        fh = max( (int)(12 * g_layout_scale + 0.5f), min( (int)(24 * g_layout_scale + 0.5f), (int)((brc.bottom - brc.top) * 2 / 5) ) );
        if (draw_bg || draw_bd)
        {
            xaml_gpu_push_rect( (float)brc.left, (float)brc.top, (float)brc.right, (float)brc.bottom, NULL,
                                4.0f, draw_bg ? cr_to_argb( bg ) : 0, draw_bd ? cr_to_argb( bd ) : 0,
                                draw_bd ? 1.0f : 0.0f, 1.0f );
        }
        if (label[0])
            xaml_gpu_push_text( ctx, label, fh, FW_SEMIBOLD, DT_CENTER | DT_VCENTER | DT_SINGLELINE, &brc, NULL, fg, 1.0f );
        return;
    }

    if (el->kind == UWP_KIND_TEXT_BLOCK)
    {
        if (wcsstr( el->name, L"RatingText" ))
        {
            RECT rc = el->bounds;
            COLORREF rfg = (el->has_fg && el->fg_alpha > 0) ? blend_argb_over( el->fg_argb, surface_bg ) : RGB( 255, 215, 0 );
            xaml_gpu_push_text( ctx, el->text[0] ? el->text : L"\x2605 4.8", 11, FW_NORMAL, DT_LEFT | DT_VCENTER | DT_SINGLELINE, &rc, NULL, rfg, 1.0f );
            return;
        }
        if (wcsstr( el->name, L"CategoryText" ))
        {
            RECT rc = el->bounds;
            COLORREF cfg = (el->has_fg && el->fg_alpha > 0)
                ? blend_argb_over( el->fg_argb, surface_bg )
                : uwp_paint_resolve_color( el, L"SecondaryForegroundBrush", RGB( 150, 150, 150 ), surface_bg );
            xaml_gpu_push_text( ctx, el->text, 11, FW_NORMAL, DT_LEFT | DT_VCENTER | DT_SINGLELINE, &rc, NULL, cfg, 1.0f );
            return;
        }
        if (el->tpl_normal_output && el->content == &el->tpl_normal_output->IInspectable_iface &&
            el->tpl_normal_output->has_bounds && el->tpl_normal_output->visibility == 0)
        {
            if (el->text[0] && wcscmp( el->tpl_normal_output->text, el->text ) != 0)
                lstrcpynW( el->tpl_normal_output->text, el->text, ARRAY_SIZE(el->tpl_normal_output->text) );
            uwp_gpu_paint_element( ctx, el->tpl_normal_output );
            return;
        }
        if (el->text[0])
        {
            RECT trc = el->bounds;
            BOOL is_result = uwp_is_display_result_element( el );
            int fh = is_result
                ? max( (int)(20 * g_layout_scale + 0.5f), min( (int)(56 * g_layout_scale + 0.5f), (int)((trc.bottom - trc.top) * 3 / 5) ) )
                : max( (int)(12 * g_layout_scale + 0.5f), min( (int)(22 * g_layout_scale + 0.5f), (int)(el->font_size * g_layout_scale + 0.5f) ) );
            COLORREF tfg = (el->has_fg && el->fg_alpha > 0)
                ? blend_argb_over( el->fg_argb, surface_bg )
                : uwp_paint_resolve_color( el, L"DefaultTextForegroundThemeBrush", RGB( 255, 255, 255 ), surface_bg );
            if (wcsstr( el->class_name, L"MenuBarItem" ) &&
                (el->is_pressed || el->is_hovered || uwp_popup_is_open_for( el )))
            {
                RECT hrc = trc;
                InflateRect( &hrc, -2, -3 );
                xaml_gpu_push_rect( (float)hrc.left, (float)hrc.top, (float)hrc.right, (float)hrc.bottom, NULL,
                                    3.0f, cr_to_argb( RGB( 54, 54, 54 ) ), 0, 0.0f, 1.0f );
            }
            InflateRect( &trc, -8, 0 );
            xaml_gpu_push_text( ctx, el->text, fh, is_result ? FW_SEMIBOLD : FW_NORMAL,
                                (is_result ? DT_RIGHT : DT_LEFT) | DT_VCENTER | DT_SINGLELINE, &trc, NULL, tfg, 1.0f );
        }
    }

    if (el->content)
    {
        struct uwp_element *ch = uwp_elem_from_any( el->content );
        if (ch && ch != el) uwp_gpu_paint_element( ctx, ch );
    }
    if (el->children_coll)
    {
        struct uwp_vec_view *children = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        unsigned int k;
        for (k = 0; k < children->count; k++)
        {
            struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
            if (ch && ch != el) uwp_gpu_paint_element( ctx, ch );
        }
    }
}

static BOOL xaml_layout_render_gpu_target( HWND hwnd, int width, int height, IInspectable *content,
                                           ID3D11Device *dev, ID3D11DeviceContext *ctx,
                                           IDXGISwapChain1 *swapchain, ID3D11RenderTargetView *rtv )
{
    RECT rc;
    struct uwp_element *root_el, *bg_scope;
    COLORREF page_bg;
    double eval_w, eval_h;
    UINT inst_cnt = 0, hits = 0, misses = 0;

    if (width <= 0 || height <= 0 || !dev || !ctx || (!swapchain && !rtv)) return FALSE;
    uwp_ensure_app_resources_loaded();
    xaml_get_layout_metrics( hwnd, NULL, &g_layout_scale );
    SetRect( &rc, 0, 0, width, height );
    eval_w = (double)width / g_layout_scale;
    eval_h = (double)height / g_layout_scale;
    if (hwnd && IsWindow( hwnd ))
    {
        HWND top = GetAncestor( hwnd, GA_ROOT );
        RECT top_rc;
        if (top && GetClientRect( top, &top_rc ) && top_rc.right > 0 && top_rc.bottom > 0)
        {
            eval_w = (double)(top_rc.right - top_rc.left) / g_layout_scale;
            eval_h = (double)(top_rc.bottom - top_rc.top) / g_layout_scale;
        }
    }

    root_el = uwp_elem_from_any( content );
    bg_scope = (root_el && root_el->kind == UWP_KIND_FRAME && root_el->content &&
                !uwp_elem_from_any( root_el->content ) && g_navigated_page_el)
        ? g_navigated_page_el : root_el;

    if (bg_scope && bg_scope->has_bg && bg_scope->bg_alpha > 0)
        page_bg = blend_argb_over( bg_scope->bg_argb, RGB( 31, 31, 31 ) );
    else
        page_bg = uwp_paint_resolve_color( bg_scope, L"ApplicationPageBackgroundThemeBrush",
                                           RGB( 31, 31, 31 ), RGB( 31, 31, 31 ) );
    g_paint_page_bg = page_bg;

    if (rtv ? !xaml_gpu_begin_frame_rtv( dev, ctx, rtv, (UINT)width, (UINT)height, page_bg )
            : !xaml_gpu_begin_frame( dev, ctx, swapchain, (UINT)width, (UINT)height, page_bg ))
        return FALSE;

    if (root_el && root_el->kind == UWP_KIND_FRAME && root_el->content &&
        !uwp_elem_from_any( root_el->content ) && g_navigated_page_el)
    {
        uwp_vsm_evaluate_tree( g_navigated_page_el, eval_w, eval_h );
        uwp_arrange_element( g_navigated_page_el, rc );
        uwp_gpu_paint_element( ctx, g_navigated_page_el );
    }
    else if (root_el && (root_el->content || root_el->children_coll || root_el->xbf_parsed))
    {
        uwp_vsm_evaluate_tree( root_el, eval_w, eval_h );
        uwp_arrange_element( root_el, rc );
        uwp_gpu_paint_element( ctx, root_el );
    }

    if (rtv ? !xaml_gpu_end_frame_rtv( ctx, &inst_cnt, &hits, &misses )
            : !xaml_gpu_end_frame( ctx, swapchain, &inst_cnt, &hits, &misses ))
        return FALSE;

    TRACE( "xaml_layout_render_gpu: %ux%u instances=%u atlas_hits=%u atlas_misses=%u\n",
           width, height, inst_cnt, hits, misses );
    {
        HDC probe = GetDC( hwnd );
        if (probe)
        {
            TRACE( "renderpost px(0,0)=%06lx px(200,200)=%06lx px(50,500)=%06lx page_bg=%06lx\n",
                   GetPixel( probe, 0, 0 ), GetPixel( probe, 200, 200 ), GetPixel( probe, 50, 500 ),
                   page_bg );
            ReleaseDC( hwnd, probe );
        }
    }
    return TRUE;
}

BOOL xaml_layout_render_gpu( HWND hwnd, int width, int height, IInspectable *content,
                             ID3D11Device *dev, ID3D11DeviceContext *ctx, IDXGISwapChain1 *swapchain )
{
    return xaml_layout_render_gpu_target( hwnd, width, height, content, dev, ctx, swapchain, NULL );
}

BOOL xaml_layout_render_gpu_rtv( HWND hwnd, int width, int height, IInspectable *content,
                                 ID3D11Device *dev, ID3D11DeviceContext *ctx, ID3D11RenderTargetView *rtv )
{
    return xaml_layout_render_gpu_target( hwnd, width, height, content, dev, ctx, NULL, rtv );
}

BOOL uwp_dispatch_keydown( HWND hwnd, UINT vk, LPARAM lparam, void *island_root_ptr )
{
    struct uwp_element *island_root = (struct uwp_element *)island_root_ptr;
    struct uwp_element *active_page = NULL;
    UINT mods = 0;
    int pass, i;

    (void)hwnd;
    if (!vk || vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL ||
        vk == VK_MENU || vk == VK_LMENU || vk == VK_RMENU ||
        vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT ||
        vk == VK_LWIN || vk == VK_RWIN)
        return FALSE;

    if (GetKeyState( VK_CONTROL ) & 0x8000) mods |= 1;
    if (GetKeyState( VK_MENU ) & 0x8000)    mods |= 2;
    if (GetKeyState( VK_SHIFT ) & 0x8000)   mods |= 4;
    if ((GetKeyState( VK_LWIN ) & 0x8000) || (GetKeyState( VK_RWIN ) & 0x8000)) mods |= 8;

    /* 1. Route PreviewKeyDown on the target island or navigated page first */
    {
        struct uwp_element *pk_root = island_root ? island_root : g_navigated_page_el;
        if (pk_root)
        {
            struct uwp_key_routed_args *kargs = calloc( 1, sizeof(*kargs) );
            if (kargs)
            {
                BOOL pk_handled;
                kargs->IInspectable_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_key_args_vtbl;
                kargs->ref = 1;
                kargs->vk = vk;
                kargs->handled = FALSE;
                kargs->status.RepeatCount = (UINT32)(lparam & 0xffff) ? (UINT32)(lparam & 0xffff) : 1;
                kargs->status.ScanCode = ((lparam >> 16) & 0xff) ? (UINT32)((lparam >> 16) & 0xff) : 1;
                kargs->status.IsExtendedKey = ((lparam >> 24) & 1) ? TRUE : FALSE;
                kargs->status.IsMenuKeyDown = (mods & 2) ? TRUE : FALSE;
                pk_handled = uwp_fire_preview_keydown_tree( pk_root, kargs );
                uwp_key_args_Release( &kargs->IInspectable_iface );
                if (pk_handled) return TRUE;
            }
        }
    }

    /* 2. Walk accelerator registry (innermost / last registered page wins) */
    active_page = island_root ? uwp_find_owning_page( island_root )
                              : (g_navigated_page_el ? g_navigated_page_el : g_last_created_page);

    for (pass = 0; pass < 2; pass++)
    {
        struct uwp_accel_entry matched = {0};
        BOOL found = FALSE;

        AcquireSRWLockExclusive( &g_all_uwp_elems_lock );
        for (i = g_accel_cnt - 1; i >= 0; i--)
        {
            const struct uwp_accel_entry *e = &g_accel_entries[i];
            if (!e->enabled || e->vk != vk || e->mods != mods) continue;
            if (e->target_el && !e->target_el->is_enabled) continue;
            if (pass == 0)
            {
                if (active_page && e->page_el == active_page)
                {
                    matched = *e;
                    found = TRUE;
                    break;
                }
                if (island_root && e->target_el && uwp_elem_is_descendant( island_root, e->target_el ))
                {
                    matched = *e;
                    found = TRUE;
                    break;
                }
            }
            else
            {
                matched = *e;
                found = TRUE;
                break;
            }
        }
        ReleaseSRWLockExclusive( &g_all_uwp_elems_lock );

        if (found)
            return uwp_invoke_accel_entry( &matched );
    }

    return FALSE;
}

static inline struct uwp_class_factory *uwp_cf_from_any( void *iface )
{
    int i;
    if ((char *)iface >= (char *)&vsm_factory_obj && (char *)iface < (char *)(&vsm_factory_obj + 1))
        return &vsm_factory_obj;
    if ((char *)iface >= (char *)&auto_props_factory_obj && (char *)iface < (char *)(&auto_props_factory_obj + 1))
        return &auto_props_factory_obj;
    if ((char *)iface >= (char *)&vth_factory_obj && (char *)iface < (char *)(&vth_factory_obj + 1))
        return &vth_factory_obj;
    if ((char *)iface >= (char *)&feap_factory_obj && (char *)iface < (char *)(&feap_factory_obj + 1))
        return &feap_factory_obj;
    if ((char *)iface >= (char *)&scb_factory_obj && (char *)iface < (char *)(&scb_factory_obj + 1))
        return &scb_factory_obj;
    AcquireSRWLockExclusive( &g_dyn_factories_lock );
    for (i = 0; i < g_dyn_factory_cnt; i++)
    {
        if ((char *)iface >= (char *)&g_dyn_factories[i] &&
            (char *)iface < (char *)&g_dyn_factories[i + 1])
        {
            ReleaseSRWLockExclusive( &g_dyn_factories_lock );
            return &g_dyn_factories[i];
        }
    }
    ReleaseSRWLockExclusive( &g_dyn_factories_lock );
    return NULL;
}

static const GUID IID_ICanvasStatics_local =
    { 0x40ce5c46, 0x2962, 0x446f, { 0xaa, 0xfb, 0x4c, 0xdc, 0x48, 0x69, 0x39, 0xc9 } };
static const GUID IID_IScrollViewerStatics_local =
    { 0x33d1299d, 0x8d6e, 0x4290, { 0xbf, 0x22, 0x90, 0x5c, 0xcc, 0xd0, 0x4d, 0x31 } };
static const GUID IID_IComboBoxFactory_local =
    { 0xac0d0444, 0xa65b, 0x4abd, { 0x86, 0xdf, 0x30, 0x16, 0x04, 0x9e, 0xfe, 0xdc } };
static const GUID IID_ISolidColorBrushFactory_local =
    { 0xd935ce0c, 0x86f5, 0x4da6, { 0x8a, 0x27, 0xb1, 0x61, 0x9e, 0xf7, 0xf9, 0x2b } };
static const GUID IID_IButtonBaseStatics_Calc =
    { 0x67ef17e1, 0xfe37, 0x474f, { 0x9e, 0x97, 0x3b, 0x5e, 0x0b, 0x30, 0xf2, 0xdf } };

static const GUID IID_IElementCompositionPreviewStatics_UWP =
    { 0x08c92b38, 0xec99, 0x4c55, { 0xbc, 0x85, 0xa1, 0xc1, 0x80, 0xb2, 0x76, 0x46 } };
static const GUID IID_IElementCompositionPreviewStatics2_UWP =
    { 0x24148fbb, 0x23d6, 0x4f37, { 0xba, 0x0c, 0x07, 0x33, 0xe7, 0x99, 0x72, 0x2d } };

static __x_ABI_CWindows_CUI_CComposition_CICompositor *g_ecp_compositor = NULL;

static __x_ABI_CWindows_CUI_CComposition_CICompositor *ecp_get_compositor( void )
{
    if (!g_ecp_compositor)
    {
        HSTRING cls = NULL;
        IActivationFactory *af = NULL;
        if (SUCCEEDED(WindowsCreateString( L"Windows.UI.Composition.Compositor", 33, &cls )) && cls)
        {
            if (SUCCEEDED(RoGetActivationFactory( cls, &IID_IActivationFactory, (void **)&af )) && af)
            {
                IInspectable *inst = NULL;
                if (SUCCEEDED(IActivationFactory_ActivateInstance( af, &inst )) && inst)
                {
                    IInspectable_QueryInterface( inst, &IID___x_ABI_CWindows_CUI_CComposition_CICompositor, (void **)&g_ecp_compositor );
                    IInspectable_Release( inst );
                }
                IActivationFactory_Release( af );
            }
            WindowsDeleteString( cls );
        }
    }
    return g_ecp_compositor;
}

static const void *ecp_statics_obj;
static const void *ecp_statics2_obj;

static HRESULT WINAPI ecp_QueryInterface( void *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ))
    {
        *out = iface;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IElementCompositionPreviewStatics_UWP ))
    {
        *out = (void *)&ecp_statics_obj;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IElementCompositionPreviewStatics2_UWP ))
    {
        *out = (void *)&ecp_statics2_obj;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI ecp_AddRef( void *iface ) { return 2; }
static ULONG WINAPI ecp_Release( void *iface ) { return 1; }
static HRESULT WINAPI ecp_GetIids( void *iface, ULONG *c, IID **i )
{
    if (c) *c = 0;
    if (i) *i = NULL;
    return S_OK;
}
static HRESULT WINAPI ecp_GetRuntimeClassName( void *iface, HSTRING *n )
{
    return WindowsCreateString( L"Windows.UI.Xaml.Hosting.ElementCompositionPreview", 49, n );
}
static HRESULT WINAPI ecp_GetTrustLevel( void *iface, TrustLevel *l )
{
    if (l) *l = BaseTrust;
    return S_OK;
}
static HRESULT WINAPI ecp_GetElementVisual( void *iface, void *elem, void **out )
{
    __x_ABI_CWindows_CUI_CComposition_CICompositor *comp;
    __x_ABI_CWindows_CUI_CComposition_CIContainerVisual *cv = NULL;
    HRESULT hr;
    if (!out) return E_POINTER;
    *out = NULL;
    comp = ecp_get_compositor();
    if (!comp) return E_FAIL;
    hr = __x_ABI_CWindows_CUI_CComposition_CICompositor_CreateContainerVisual( comp, &cv );
    if (SUCCEEDED(hr) && cv)
    {
        hr = __x_ABI_CWindows_CUI_CComposition_CIContainerVisual_QueryInterface( cv, &IID___x_ABI_CWindows_CUI_CComposition_CIVisual, out );
        __x_ABI_CWindows_CUI_CComposition_CIContainerVisual_Release( cv );
    }
    TRACE( "ecp_GetElementVisual(%p) -> hr %#lx, visual %p\n", elem, hr, *out );
    return hr;
}
static HRESULT WINAPI ecp_GetElementChildVisual( void *iface, void *elem, void **out )
{
    if (!out) return E_POINTER;
    *out = NULL;
    return S_OK;
}
static HRESULT WINAPI ecp_SetElementChildVisual( void *iface, void *elem, void *visual )
{
    return S_OK;
}
static HRESULT WINAPI ecp_GetScrollViewerManipulationPropertySet( void *iface, void *sv, void **out )
{
    __x_ABI_CWindows_CUI_CComposition_CICompositor *comp;
    __x_ABI_CWindows_CUI_CComposition_CICompositionPropertySet *ps = NULL;
    HRESULT hr;
    if (!out) return E_POINTER;
    *out = NULL;
    comp = ecp_get_compositor();
    if (!comp) return E_FAIL;
    hr = __x_ABI_CWindows_CUI_CComposition_CICompositor_CreatePropertySet( comp, &ps );
    if (SUCCEEDED(hr) && ps) *out = ps;
    return hr;
}
static HRESULT WINAPI ecp_SetImplicitShowAnimation( void *iface, void *elem, void *anim ) { return S_OK; }
static HRESULT WINAPI ecp_SetImplicitHideAnimation( void *iface, void *elem, void *anim ) { return S_OK; }
static HRESULT WINAPI ecp_SetIsTranslationEnabled( void *iface, void *elem, boolean val ) { return S_OK; }
static HRESULT WINAPI ecp_GetPointerPositionPropertySet( void *iface, void *elem, void **out )
{
    return ecp_GetScrollViewerManipulationPropertySet( iface, elem, out );
}

static const void *ecp_statics_vtbl[] =
{
    ecp_QueryInterface,
    ecp_AddRef,
    ecp_Release,
    ecp_GetIids,
    ecp_GetRuntimeClassName,
    ecp_GetTrustLevel,
    ecp_GetElementVisual,
    ecp_GetElementChildVisual,
    ecp_SetElementChildVisual,
    ecp_GetScrollViewerManipulationPropertySet,
};
static const void *ecp_statics_obj = ecp_statics_vtbl;

static const void *ecp_statics2_vtbl[] =
{
    ecp_QueryInterface,
    ecp_AddRef,
    ecp_Release,
    ecp_GetIids,
    ecp_GetRuntimeClassName,
    ecp_GetTrustLevel,
    ecp_SetImplicitShowAnimation,
    ecp_SetImplicitHideAnimation,
    ecp_SetIsTranslationEnabled,
    ecp_GetPointerPositionPropertySet,
};
static const void *ecp_statics2_obj = ecp_statics2_vtbl;

static const void *comp_target_statics_obj;
HRESULT WINAPI uwp_cf_QI( IActivationFactory *iface, REFIID iid, void **out )
{
    struct uwp_class_factory *f = uwp_cf_from_any( iface );
    TRACE( "uwp_cf_QI enter: cls=%s, iid=%s\n", f ? debugstr_w(f->class_name) : "?", debugstr_guid(iid) );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IMarshal ) ||
        IsEqualGUID( iid, &IID_IReferenceTracker_UWP ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget_UWP ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget_UWP2 ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = f ? (void *)&f->IActivationFactory_iface : (void *)iface;
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IElementCompositionPreviewStatics_UWP ))
    {
        *out = (void *)&ecp_statics_obj;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IElementCompositionPreviewStatics2_UWP ))
    {
        *out = (void *)&ecp_statics2_obj;
        return S_OK;
    }
    if (f && !wcscmp( f->class_name, L"Windows.UI.Xaml.Media.CompositionTarget" ))
    {
        *out = (void *)&comp_target_statics_obj;
        return S_OK;
    }

    {
        static const GUID IID_IControlStatics_UWP =
            { 0x473b305b, 0x877d, 0x4c35, { 0x8f, 0xca, 0x46, 0xa0, 0x5f, 0x96, 0x60, 0x7a } };
        static const GUID IID_IRangeBaseStatics_UWP =
            { 0x67ef17e1, 0xfe37, 0x474f, { 0x9e, 0x97, 0x3b, 0x5e, 0x0b, 0x30, 0xf2, 0xe0 } };
        static const GUID IID_IAutoSuggestBoxStatics_UWP =
            { 0xee22c7f4, 0xc15d, 0x4ff3, { 0x8a, 0x94, 0xf5, 0x0d, 0xfd, 0xfb, 0xe8, 0x9a } };
        if (IsEqualGUID( iid, &IID_IButtonBaseStatics_UWP ) ||
            IsEqualGUID( iid, &IID_IButtonBaseStatics_Calc ) ||
            IsEqualGUID( iid, &IID_IToggleButtonStatics_UWP ) ||
            IsEqualGUID( iid, &IID_IControlStatics_UWP ) ||
            IsEqualGUID( iid, &IID_IRangeBaseStatics_UWP ) ||
            IsEqualGUID( iid, &IID_IAutoSuggestBoxStatics_UWP ) ||
            IsEqualGUID( iid, &IID_IToggleButtonFactory2_UWP )) /* ISelectorStatics {13300b06-...} */
        {
            *out = f && f->IDpStatics_iface.lpVtbl ? (void *)&f->IDpStatics_iface : (void *)&f->IStatics_iface;
            return S_OK;
        }
    }

    {
        static const GUID IID_IPopupStatics_UWP =
            { 0xaa119f27, 0x2055, 0x483e, { 0xa4, 0xe0, 0x6f, 0x25, 0x58, 0x73, 0x2d, 0x9b } };
        if (IsEqualGUID( iid, &IID_IPopupStatics_UWP ))
        {
            *out = f ? (void *)&f->IStatics_iface : (void *)iface;
            return S_OK;
        }
    }
    if (f == &auto_props_factory_obj ||
        f == &feap_factory_obj ||
        IsEqualGUID( iid, &IID_ICanvasStatics_local ) ||
        IsEqualGUID( iid, &IID_IScrollViewerStatics_local ) ||
        IsEqualGUID( iid, &IID_IGridStatics_UWP ) ||
        IsEqualGUID( iid, &IID_IGridStatics_UWP2 ) ||
        IsEqualGUID( iid, &IID_IVisualTreeHelperStatics_UWP ) ||
        IsEqualGUID( iid, &IID_IUIElementStatics_UWP ) ||
        IsEqualGUID( iid, &IID_IFrameworkElementStatics_UWP ))
    {
        *out = (void *)&f->IStatics_iface;
        return S_OK;
    }
    {
        static const GUID IID_IRangeBaseFactory_UWP =
            { 0x389b7c71, 0x5220, 0x42b2, { 0x99, 0x92, 0x26, 0x90, 0xc1, 0xa6, 0x70, 0x30 } };
        static const GUID IID_INotifyCollectionChangedEventArgsFactory_UWP =
            { 0xb30c3e3a, 0xdf8d, 0x44a5, { 0x9a, 0x38, 0x7a, 0xc0, 0xd0, 0x8c, 0xe6, 0x3d } };
        if (IsEqualGUID( iid, &IID_IRangeBaseFactory_UWP ) ||
            IsEqualGUID( iid, &IID_INotifyCollectionChangedEventArgsFactory_UWP ))
        {
            *out = f ? (void *)&f->IComposableFactory_iface : (void *)iface;
            return S_OK;
        }
    }
    if (IsEqualGUID( iid, &IID_IFrameFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IPageFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IUserControlFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IContentControlFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IContentControlFactory_UWP2 ) ||
        IsEqualGUID( iid, &IID_IControlFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IUIElementFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IFrameworkElementFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IDependencyObjectFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IButtonFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IButtonBaseFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IToggleButtonFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IPanelFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IPanelFactory_UWP2 ) ||
        IsEqualGUID( iid, &IID_IGridFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IListViewFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IComboBoxFactory_local ) ||
        IsEqualGUID( iid, &IID_ISolidColorBrushFactory_local ) ||
        IsEqualGUID( iid, &IID_IXamlLightFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IContentPresenterFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IXamlCompositionBrushBaseFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IFlyoutBaseFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IStateTriggerBaseFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IMenuFlyoutItemBaseFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IMenuFlyoutItemFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IMenuFlyoutSeparatorFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IMarkupExtensionFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IRoutedEventArgsFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IBindingFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IRelativeSourceFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IRadioButtonFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IPropertyPathFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IDependencyObjectCollectionFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IVisualStateManagerFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IDataTemplateSelectorFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IStyleSelectorFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IStyleSelectorFactory_UWP2 ) ||
        IsEqualGUID( iid, &IID_IStateTriggerBaseFactory2_UWP ) ||
        IsEqualGUID( iid, &IID_IDesktopWindowXamlSourceFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IDispatcherTimerFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IKeyboardAcceleratorFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IContentDialogFactory_UWP ))
    {
        *out = f ? (void *)&f->IComposableFactory_iface : (void *)iface;
        return S_OK;
    }
    if (f && f->class_name &&
        (wcsstr( f->class_name, L"Control" ) ||
         wcsstr( f->class_name, L"Source" ) ||
         wcsstr( f->class_name, L"Box" ) ||
         wcsstr( f->class_name, L"Item" ) ||
         wcsstr( f->class_name, L"View" ) ||
         wcsstr( f->class_name, L"Panel" ) ||
         wcsstr( f->class_name, L"Button" ) ||
         wcsstr( f->class_name, L"Selector" ) ||
         wcsstr( f->class_name, L"Flyout" ) ||
         wcsstr( f->class_name, L"Separator" ) ||
         wcsstr( f->class_name, L"Trigger" ) ||
         wcsstr( f->class_name, L"Transition" ) ||
         wcsstr( f->class_name, L"Extension" ) ||
         wcsstr( f->class_name, L"BitmapSource" ) ||
         wcsstr( f->class_name, L"Accelerator" ) ||
         wcsstr( f->class_name, L"Command" ) ||
         wcsstr( f->class_name, L"Timer" ) ||
         wcsstr( f->class_name, L"Collection" )))
    {
        WARN( "uwp_cf_QI(%s) unknown iid %s -> ComposableFactory fallback\n",
              debugstr_w(f->class_name), debugstr_guid(iid) );
        *out = (void *)&f->IComposableFactory_iface;
        return S_OK;
    }
    /* Unknown IID on factory: log so we can see exact Statics/Factory IID, return Statics */
    WARN( "uwp_cf_QI(%s) unknown iid %s -> Statics fallback\n",
          f ? debugstr_w(f->class_name) : "?", debugstr_guid(iid) );
    *out = f ? (void *)&f->IStatics_iface : (void *)iface;
    return S_OK;
}
ULONG WINAPI uwp_cf_AddRef( IActivationFactory *iface ) { return 2; }
ULONG WINAPI uwp_cf_Release( IActivationFactory *iface ) { return 1; }
HRESULT WINAPI uwp_cf_GetIids( IActivationFactory *iface, ULONG *cnt, IID **iids )
{
    if (cnt) *cnt = 0;
    if (iids) *iids = NULL;
    return S_OK;
}
HRESULT WINAPI uwp_cf_GetRuntimeClassName( IActivationFactory *iface, HSTRING *name )
{
    struct uwp_class_factory *f = uwp_cf_from_any( iface );
    const WCHAR *cls = f ? f->class_name : L"Windows.UI.Xaml.FrameworkElement";
    return WindowsCreateString( cls, wcslen(cls), name );
}
HRESULT WINAPI uwp_cf_GetTrustLevel( IActivationFactory *iface, TrustLevel *lvl )
{
    if (lvl) *lvl = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI uwp_cf_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    struct uwp_class_factory *f = uwp_cf_from_any( iface );
    const WCHAR *cls = f ? f->class_name : L"Windows.UI.Xaml.FrameworkElement";
    TRACE( "uwp_cf_ActivateInstance(%s, %p)\n", debugstr_w(cls), instance );
    return uwp_element_create( cls, NULL, NULL, (void **)instance );
}

static HRESULT WINAPI uwp_cf_CreateInstance( void *iface, IInspectable *outer, IInspectable **inner, void **instance )
{
    struct uwp_class_factory *f = uwp_cf_from_any( iface );
    const WCHAR *cls = f ? f->class_name : L"Windows.UI.Xaml.FrameworkElement";
    /* IPropertyPathFactory::CreateInstance(HSTRING path, IPropertyPath **out)
     * takes 2 args after iface, so the output pointer arrives in `inner`. */
    if (!wcscmp( cls, L"Windows.UI.Xaml.PropertyPath" ))
    {
        TRACE( "uwp_cf_CreateInstance(PropertyPath, path=%p, out=%p)\n", outer, inner );
        return uwp_element_create( cls, NULL, NULL, (void **)inner );
    }
    /* If called as I*Statics::get_*Property(iface, IDependencyProperty **out)
     * (e.g. IPopupStatics on Controls.Primitives.Popup routed to the
     * ComposableFactory vtbl), `outer` is the out-pointer. */
    if ((ULONG_PTR)outer >= 0x10000 && *(void **)outer == NULL)
        return dependency_property_create( NULL, (IDependencyProperty **)outer );
    if ((ULONG_PTR)outer < 0x10000) outer = NULL;
    TRACE( "uwp_cf_CreateInstance(%s, outer=%p, inner=%p, inst=%p)\n", debugstr_w(cls), outer, inner, instance );
    return uwp_element_create( cls, outer, inner, instance );
}

static HRESULT WINAPI uwp_cf_dp_prop( void *iface, void **out )
{
    return dependency_property_create( NULL, (IDependencyProperty **)out );
}

static HRESULT WINAPI uwp_cf_dp_prop_slot9( void *iface, void **out )
{
    struct uwp_class_factory *f = uwp_cf_from_any( iface );
    if (f && f->class_name && !wcscmp( f->class_name, L"Windows.UI.Xaml.UIElement" ))
    {
        if (!out) return E_POINTER;
        if (!g_routed_event_ptr_pressed)
            dependency_property_create( NULL, &g_routed_event_ptr_pressed );
        *out = g_routed_event_ptr_pressed;
        if (*out) IUnknown_AddRef( (IUnknown *)*out );
        return S_OK;
    }
    return dependency_property_create( NULL, (IDependencyProperty **)out );
}

const void *uwp_cf_act_vtbl[16] =
{
    [0] = uwp_cf_QI,
    [1] = uwp_cf_AddRef,
    [2] = uwp_cf_Release,
    [3] = uwp_cf_GetIids,
    [4] = uwp_cf_GetRuntimeClassName,
    [5] = uwp_cf_GetTrustLevel,
    [6] = uwp_cf_ActivateInstance,
    [7 ... 15] = uwp_cf_dp_prop,
};

const void *uwp_cf_comp_vtbl[64] =
{
    [0] = uwp_cf_QI,
    [1] = uwp_cf_AddRef,
    [2] = uwp_cf_Release,
    [3] = uwp_cf_GetIids,
    [4] = uwp_cf_GetRuntimeClassName,
    [5] = uwp_cf_GetTrustLevel,
    [6] = uwp_cf_CreateInstance,
    [7] = uwp_cf_CreateInstance,
    [8] = uwp_cf_CreateInstance,
    [9] = uwp_cf_CreateInstance,
    [10 ... 63] = uwp_generic_noop,
};

static const void *comp_target_statics_vtbl[16] =
{
    [0] = uwp_cf_QI,
    [1] = uwp_cf_AddRef,
    [2] = uwp_cf_Release,
    [3] = uwp_cf_GetIids,
    [4] = uwp_cf_GetRuntimeClassName,
    [5] = uwp_cf_GetTrustLevel,
    [6] = uwp_ui_event_add,
    [7] = uwp_generic_noop,
    [8] = uwp_ui_event_add,
    [9] = uwp_generic_noop,
    [10 ... 15] = uwp_generic_noop,
};
static const void *comp_target_statics_obj = comp_target_statics_vtbl;

const void *uwp_cf_statics_vtbl[64] =
{
    [0] = uwp_cf_QI,
    [1] = uwp_cf_AddRef,
    [2] = uwp_cf_Release,
    [3] = uwp_cf_GetIids,
    [4] = uwp_cf_GetRuntimeClassName,
    [5] = uwp_cf_GetTrustLevel,
    [6] = uwp_cf_dp_prop,
    [7 ... 8] = uwp_generic_noop,
    [9] = uwp_cf_dp_prop_slot9,
    [10 ... 11] = uwp_generic_noop,
    [12] = uwp_cf_dp_prop,
    [13 ... 14] = uwp_generic_noop,
    [15] = uwp_cf_dp_prop,
    [16 ... 17] = uwp_generic_noop,
    [18] = uwp_cf_dp_prop,
    [19 ... 20] = uwp_generic_noop,
    [21] = uwp_cf_dp_prop,
    [22 ... 23] = uwp_generic_noop,
    [24] = uwp_cf_dp_prop,
    [25 ... 26] = uwp_generic_noop,
    [27] = uwp_cf_dp_prop,
    [28 ... 29] = uwp_generic_noop,
    [30] = uwp_cf_dp_prop,
    [31 ... 32] = uwp_generic_noop,
    [33] = uwp_cf_dp_prop,
    [34 ... 35] = uwp_generic_noop,
    [36] = uwp_cf_dp_prop,
    [37 ... 38] = uwp_generic_noop,
    [39] = uwp_cf_dp_prop,
    [40 ... 41] = uwp_generic_noop,
    [42] = uwp_cf_dp_prop,
    [43 ... 44] = uwp_generic_noop,
    [45] = uwp_cf_dp_prop,
    [46 ... 63] = uwp_generic_noop,
};

const void *uwp_dp_statics_vtbl[32] =
{
    [0] = uwp_cf_QI,
    [1] = uwp_cf_AddRef,
    [2] = uwp_cf_Release,
    [3] = uwp_cf_GetIids,
    [4] = uwp_cf_GetRuntimeClassName,
    [5] = uwp_cf_GetTrustLevel,
    [6 ... 31] = uwp_cf_dp_prop,
};

/* VisualStateManagerStatics (slot 6 = GoToState(Control, HSTRING, bool, bool*), slot 7 = GetVisualStateGroups) */
static HRESULT WINAPI scb_factory_CreateInstanceWithColor( void *iface, Color color, void **out )
{
    HRESULT hr;
    if (!out) return E_POINTER;
    hr = uwp_element_create( L"Windows.UI.Xaml.Media.SolidColorBrush", NULL, NULL, out );
    if (SUCCEEDED(hr) && *out)
    {
        struct uwp_element *el = uwp_elem_from_any( *out );
        if (el)
        {
            el->brush_argb = ((UINT32)color.A << 24) | ((UINT32)color.R << 16) | ((UINT32)color.G << 8) | color.B;
            el->has_brush_argb = TRUE;
            el->brush_opacity = 1.0;
        }
    }
    return hr;
}
const void *scb_factory_vtbl[16] =
{
    [0] = uwp_cf_QI,
    [1] = uwp_cf_AddRef,
    [2] = uwp_cf_Release,
    [3] = uwp_cf_GetIids,
    [4] = uwp_cf_GetRuntimeClassName,
    [5] = uwp_cf_GetTrustLevel,
    [6] = scb_factory_CreateInstanceWithColor,
    [7 ... 15] = uwp_generic_noop,
};
struct uwp_class_factory scb_factory_obj =
    { {(const struct IActivationFactoryVtbl *)uwp_cf_act_vtbl}, {(const struct IInspectableVtbl *)scb_factory_vtbl}, {(const struct IInspectableVtbl *)uwp_cf_statics_vtbl}, L"Windows.UI.Xaml.Media.SolidColorBrush", { NULL } };

