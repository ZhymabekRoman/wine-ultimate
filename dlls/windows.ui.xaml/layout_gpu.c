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
#include <excpt.h>
#include "wine/exception.h"
#include "private.h"
#include "weakreference.h"
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

static void uwp_debug_dump_tree( struct uwp_element *el, int depth, int *rem )
{
    int kids = 0;
    if (!el || depth > 35 || *rem <= 0) return;
    if (wcsstr( el->class_name, L"HomepageSkeleton" ))
    {
        WARN( "%*s%s (skipped)\n", depth * 2, "", debugstr_w(el->class_name) );
        return;
    }
    if (el->children_coll)
        kids = (int)CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface )->count;
    else if (el->inlines_coll)
        kids = (int)CONTAINING_RECORD( el->inlines_coll, struct uwp_vec_view, IInspectable_iface )->count;
    (*rem)--;
    WARN( "%*s%s (%s) rc=(%ld,%ld,%ld,%ld) hb=%d vis=%d op=%.2f kids=%d w=%.0f h=%.0f txt=%s img=%s\n",
          depth * 2, "", debugstr_w(el->class_name), debugstr_w(el->name),
          el->bounds.left, el->bounds.top, el->bounds.right, el->bounds.bottom,
          el->has_bounds, el->visibility, el->opacity, kids,
          el->width, el->height,
          el->text[0] ? debugstr_w(el->text) : "\"\"",
          el->image_src[0] ? debugstr_w(el->image_src) : "\"\"" );
    if (el->content)
    {
        struct uwp_element *ch = uwp_elem_from_any( el->content );
        if (ch && ch != el) uwp_debug_dump_tree( ch, depth + 1, rem );
    }
    if (el->children_coll)
    {
        struct uwp_vec_view *v = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        unsigned int i;
        for (i = 0; i < v->count && *rem > 0; i++)
        {
            struct uwp_element *ch = uwp_elem_from_any( v->items[i] );
            if (ch && ch != el) uwp_debug_dump_tree( ch, depth + 1, rem );
        }
    }
}

void uwp_debug_dump_root( IInspectable *root )
{
    int rem = 500;
    if (root) uwp_debug_dump_tree( uwp_elem_from_any( root ), 0, &rem );
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
    if ((msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) && wp == VK_F12 && impl && impl->content)
    {
        int rem = 500;
        uwp_debug_dump_tree( uwp_elem_from_any( impl->content ), 0, &rem );
        return 0;
    }
    if ((msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) && wp == VK_ESCAPE && uwp_popup_handle_escape())
        return 0;
    if (msg == WM_CHAR && g_focused_textbox && !(GetKeyState( VK_CONTROL ) & 0x8000) && !(GetKeyState( VK_MENU ) & 0x8000))
    {
        uwp_keyboard_press_char( (WCHAR)wp );
        if (hwnd) InvalidateRect( hwnd, NULL, FALSE );
        return 0;
    }
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

#define MAX_ACTIVE_DISPATCHER_TIMERS 64
static struct uwp_element *g_active_timers[MAX_ACTIVE_DISPATCHER_TIMERS];

static void CALLBACK uwp_dispatcher_timer_proc( HWND hwnd, UINT msg, UINT_PTR id, DWORD time )
{
    struct uwp_element *impl = NULL;
    unsigned int i, count;
    void **handlers = NULL;

    for (i = 0; i < MAX_ACTIVE_DISPATCHER_TIMERS; i++)
    {
        if (g_active_timers[i] && g_active_timers[i]->timer_win32_id == id)
        {
            impl = g_active_timers[i];
            break;
        }
    }
    if (!impl || !impl->timer_enabled) return;

    uwp_sub_AddRef( &impl->IInspectable_iface );
    count = xaml_event_registry_snapshot_alloc( &impl->timer_tick_reg, &handlers );
    TRACE( "uwp_dispatcher_timer_proc id=%Iu impl=%p handlers=%u\n", id, impl, count );
    for (i = 0; i < count; i++)
    {
        struct xaml_event_handler *h = (struct xaml_event_handler *)handlers[i];
        IInspectable *sender = impl->outer ? impl->outer : &impl->IInspectable_iface;
        __TRY
        {
            h->lpVtbl->Invoke( h, sender, NULL );
        }
        __EXCEPT_ALL {}
        __ENDTRY;
        IUnknown_Release( (IUnknown *)handlers[i] );
    }
    free( handlers );
    uwp_sub_Release( &impl->IInspectable_iface );
}

static HRESULT WINAPI uwp_timer_get_Interval( void *iface, INT64 *value )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (value) *value = impl ? impl->timer_interval_100ns : 0;
    return S_OK;
}
static HRESULT WINAPI uwp_timer_put_Interval( void *iface, INT64 value )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl) impl->timer_interval_100ns = value;
    TRACE( "uwp_timer_put_Interval impl=%p interval_100ns=%s\n", impl, wine_dbgstr_longlong( value ) );
    return S_OK;
}
static HRESULT WINAPI uwp_timer_get_IsEnabled( void *iface, boolean *value )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (value) *value = (impl && impl->timer_enabled) ? TRUE : FALSE;
    return S_OK;
}
static HRESULT WINAPI uwp_timer_add_Tick( void *iface, void *handler, EventRegistrationToken *tok )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return E_NOINTERFACE;
    TRACE( "uwp_timer_add_Tick impl=%p handler=%p\n", impl, handler );
    return xaml_event_registry_add( &impl->timer_tick_reg, handler, tok );
}
static HRESULT WINAPI uwp_timer_remove_Tick( void *iface, EventRegistrationToken tok )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return E_NOINTERFACE;
    return xaml_event_registry_remove( &impl->timer_tick_reg, tok );
}
static HRESULT WINAPI uwp_timer_Start( void *iface )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    UINT ms;
    int i, slot = -1;
    if (!impl) return S_OK;
    if (impl->timer_enabled && impl->timer_win32_id) return S_OK;
    ms = (impl->timer_interval_100ns > 150000) ? (UINT)(impl->timer_interval_100ns / 10000) : 15;
    if (ms > 60000) ms = 60000;
    for (i = 0; i < MAX_ACTIVE_DISPATCHER_TIMERS; i++)
    {
        if (g_active_timers[i] == impl) { slot = i; break; }
        if (!g_active_timers[i] && slot < 0) slot = i;
    }
    if (slot < 0) return S_OK;
    impl->timer_win32_id = SetTimer( NULL, 0, ms, uwp_dispatcher_timer_proc );
    if (impl->timer_win32_id)
    {
        if (g_active_timers[slot] != impl)
        {
            g_active_timers[slot] = impl;
            uwp_sub_AddRef( &impl->IInspectable_iface );
        }
        impl->timer_enabled = TRUE;
    }
    TRACE( "uwp_timer_Start impl=%p ms=%u win32_id=%Iu\n", impl, ms, impl->timer_win32_id );
    return S_OK;
}
static HRESULT WINAPI uwp_timer_Stop( void *iface )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    int i;
    if (!impl) return S_OK;
    if (impl->timer_win32_id)
    {
        KillTimer( NULL, impl->timer_win32_id );
        impl->timer_win32_id = 0;
    }
    impl->timer_enabled = FALSE;
    for (i = 0; i < MAX_ACTIVE_DISPATCHER_TIMERS; i++)
    {
        if (g_active_timers[i] == impl)
        {
            g_active_timers[i] = NULL;
            uwp_sub_Release( &impl->IInspectable_iface );
            break;
        }
    }
    TRACE( "uwp_timer_Stop impl=%p\n", impl );
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
    [6] = uwp_timer_get_Interval,
    [7] = uwp_timer_put_Interval,
    [8] = uwp_timer_get_IsEnabled,
    [9] = uwp_timer_add_Tick,
    [10] = uwp_timer_remove_Tick,
    [11] = uwp_timer_Start,
    [12] = uwp_timer_Stop,
    [13 ... 15] = uwp_generic_noop,
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
    static SRWLOCK doc_lock = SRWLOCK_INIT;
    static WCHAR cached_cls[128];
    static OLECHAR cached_bstr[128];
    struct uwp_element *impl = uwp_elem_from_any( (IInspectable *)iface );
    const WCHAR *cls = (impl && impl->class_name[0]) ? impl->class_name : L"Windows.UI.Xaml.DependencyObject";
    AcquireSRWLockExclusive( &doc_lock );
    if (wcscmp( cached_cls, cls ))
    {
        lstrcpynW( cached_cls, cls, ARRAY_SIZE(cached_cls) );
        lstrcpynW( cached_bstr, cls, ARRAY_SIZE(cached_bstr) );
    }
    if (name) *name = cached_bstr;
    ReleaseSRWLockExclusive( &doc_lock );
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

static void uwp_vm_extract_nested_url( IInspectable *pval, WCHAR *out, size_t out_len, int depth );

static BOOL uwp_vm_query_string_prop_depth( IInspectable *vm, const WCHAR *const *props, unsigned int count,
                                            WCHAR *out, size_t out_len, int depth )
{
    static const GUID IID_ICustomPropertyProvider_gpu =
        { 0x7c925755, 0x3e48, 0x42b4, { 0x86, 0x77, 0x76, 0x37, 0x22, 0x67, 0x03, 0x3f } };
    HSTRING hcls = NULL;
    void *cpp = NULL;
    unsigned int pi;

    if (!vm || !out || !out_len) return FALSE;
    out[0] = 0;

    if (SUCCEEDED(IInspectable_GetRuntimeClassName( vm, &hcls )) && hcls)
    {
        const WCHAR *cls = WindowsGetStringRawBuffer( hcls, NULL );
        if (cls && cls[0])
        {
            for (pi = 0; pi < count && !out[0]; pi++)
            {
                IInspectable *pval = NULL;
                if (uwp_get_custom_xaml_member( uwp_get_app_outer(), cls, props[pi], vm, &pval ) && pval)
                {
                    if (!uwp_unbox_string( pval, out, out_len ))
                        uwp_vm_extract_nested_url( pval, out, out_len, depth );
                    IInspectable_Release( pval );
                }
            }
        }
        WindowsDeleteString( hcls );
    }
    if (!out[0] && SUCCEEDED(IInspectable_QueryInterface( vm, &IID_ICustomPropertyProvider_gpu, &cpp )) && cpp)
    {
        HRESULT (WINAPI *get_prop)(void *, HSTRING, void **) = (void *)(*(void ***)cpp)[6];
        HRESULT (WINAPI *get_type)(void *, TypeName *) = (void *)(*(void ***)cpp)[9];
        if (get_type)
        {
            TypeName tn = {0};
            if (SUCCEEDED(get_type( cpp, &tn )) && tn.Name)
            {
                WCHAR tcls[256] = {0};
                WCHAR *comma;
                const WCHAR *raw = WindowsGetStringRawBuffer( tn.Name, NULL );
                if (raw) lstrcpynW( tcls, raw, ARRAY_SIZE(tcls) );
                comma = wcschr( tcls, ',' );
                if (comma) *comma = 0;
                if (tcls[0])
                {
                    for (pi = 0; pi < count && !out[0]; pi++)
                    {
                        IInspectable *pval = NULL;
                        if (uwp_get_custom_xaml_member( uwp_get_app_outer(), tcls, props[pi], vm, &pval ) && pval)
                        {
                            if (!uwp_unbox_string( pval, out, out_len ))
                                uwp_vm_extract_nested_url( pval, out, out_len, depth );
                            IInspectable_Release( pval );
                        }
                    }
                }
                WindowsDeleteString( tn.Name );
            }
        }
        for (pi = 0; pi < count && !out[0]; pi++)
        {
            HSTRING hp = NULL;
            void *cp = NULL;
            if (get_prop && SUCCEEDED(WindowsCreateString( props[pi], wcslen(props[pi]), &hp )) && hp)
            {
                if (SUCCEEDED(get_prop( cpp, hp, &cp )) && cp)
                {
                    HRESULT (WINAPI *get_val)(void *, IInspectable *, IInspectable **) = (void *)(*(void ***)cp)[8];
                    IInspectable *pval = NULL;
                    if (get_val && SUCCEEDED(get_val( cp, vm, &pval )) && pval)
                    {
                        if (!uwp_unbox_string( pval, out, out_len ))
                            uwp_vm_extract_nested_url( pval, out, out_len, depth );
                        IInspectable_Release( pval );
                    }
                    IUnknown_Release( (IUnknown *)cp );
                }
                WindowsDeleteString( hp );
            }
        }
        IUnknown_Release( (IUnknown *)cpp );
    }
    return out[0] != 0;
}

static void uwp_vm_extract_nested_url( IInspectable *pval, WCHAR *out, size_t out_len, int depth )
{
    static const WCHAR *const nested_url_props[] =
        { L"Url", L"Uri", L"ImageUrl", L"ImageUri", L"Source", L"AbsoluteUri", L"RawUri", L"Location", L"Path",
          L"UriString", L"UrlString", L"ImageSource", L"BitmapImage", L"ImageLinkUri", L"PrimaryImageUrl",
          L"TileImage", L"RequestedImage", L"CurrentImage", L"DefaultImage", L"BackgroundImage", L"ForegroundImage",
          L"SmallLogoImage", L"Icon", L"Image", L"Images" };
    static const GUID IID_IVector_Insp_g =
        { 0xb32bdca4, 0x5e52, 0x5b27, { 0xbc, 0x5d, 0xd6, 0x6a, 0x1a, 0x26, 0x8c, 0x2a } };
    static const GUID IID_IVectorView_Insp_g =
        { 0xe6f2fef0, 0x0712, 0x587f, { 0x8b, 0x13, 0x48, 0x21, 0x24, 0x9a, 0x20, 0xb5 } };
    static const GUID IID_IBindableVector_g =
        { 0x393de7de, 0x6fd0, 0x4c0d, { 0xbb, 0x71, 0x47, 0x24, 0x4a, 0x11, 0x38, 0x43 } };
    static const GUID IID_IBindableIterable_g =
        { 0x036d2c08, 0xdf29, 0x41af, { 0x8a, 0xa2, 0xd7, 0x74, 0xbe, 0x62, 0xba, 0x6f } };
    static const GUID IID_IIterable_Insp_g =
        { 0x092b849b, 0x60b1, 0x52be, { 0xa4, 0x4a, 0x6f, 0xe8, 0xe9, 0x33, 0xcb, 0xe4 } };
    struct uwp_element *pel = uwp_elem_from_any( pval );
    void *vec = NULL;

    if (pel && pel->image_src[0])
    {
        lstrcpynW( out, pel->image_src, out_len );
        return;
    }
    uwp_extract_uri_string( pval, out, out_len );
    if (out[0] || depth <= 0) return;

    if (SUCCEEDED(IInspectable_QueryInterface( pval, &IID_IVector_Insp_g, &vec )) ||
        SUCCEEDED(IInspectable_QueryInterface( pval, &IID_IVectorView_Insp_g, &vec )) ||
        SUCCEEDED(IInspectable_QueryInterface( pval, &IID_IBindableVector_g, &vec )))
    {
        HRESULT (WINAPI *get_at)(void *, unsigned int, void **) = (void *)(*(void ***)vec)[6];
        HRESULT (WINAPI *get_sz)(void *, unsigned int *) = (void *)(*(void ***)vec)[7];
        unsigned int sz = 0, i;
        if (get_sz && SUCCEEDED(get_sz( vec, &sz )))
        {
            for (i = 0; i < sz && i < 6 && !out[0]; i++)
            {
                void *it = NULL;
                if (get_at && SUCCEEDED(get_at( vec, i, &it )) && it)
                {
                    if (!uwp_unbox_string( (IInspectable *)it, out, out_len ))
                        uwp_vm_query_string_prop_depth( (IInspectable *)it, nested_url_props, ARRAY_SIZE(nested_url_props),
                                                        out, out_len, depth - 1 );
                    IUnknown_Release( (IUnknown *)it );
                }
            }
        }
        IUnknown_Release( (IUnknown *)vec );
    }
    else if ((SUCCEEDED(IInspectable_QueryInterface( pval, &IID_IIterable_Insp_g, &vec )) ||
              SUCCEEDED(IInspectable_QueryInterface( pval, &IID_IBindableIterable_g, &vec ))) && vec)
    {
        HRESULT (WINAPI *first)(void *, void **) = (void *)(*(void ***)vec)[6];
        void *iter = NULL;
        if (first && SUCCEEDED(first( vec, &iter )) && iter)
        {
            HRESULT (WINAPI *get_cur)(void *, void **) = (void *)(*(void ***)iter)[6];
            HRESULT (WINAPI *has_cur)(void *, boolean *) = (void *)(*(void ***)iter)[7];
            HRESULT (WINAPI *move_next)(void *, boolean *) = (void *)(*(void ***)iter)[8];
            boolean has = FALSE;
            int cnt = 0;
            if (has_cur && SUCCEEDED(has_cur( iter, &has )))
            {
                while (has && cnt < 6 && !out[0])
                {
                    void *it = NULL;
                    if (get_cur && SUCCEEDED(get_cur( iter, &it )) && it)
                    {
                        if (!uwp_unbox_string( (IInspectable *)it, out, out_len ))
                            uwp_vm_query_string_prop_depth( (IInspectable *)it, nested_url_props, ARRAY_SIZE(nested_url_props),
                                                            out, out_len, depth - 1 );
                        IUnknown_Release( (IUnknown *)it );
                    }
                    has = FALSE;
                    cnt++;
                    if (move_next) move_next( iter, &has );
                }
            }
            IUnknown_Release( (IUnknown *)iter );
        }
        IUnknown_Release( (IUnknown *)vec );
    }
    if (!out[0])
        uwp_vm_query_string_prop_depth( pval, nested_url_props, ARRAY_SIZE(nested_url_props), out, out_len, depth - 1 );
}

static BOOL uwp_vm_query_string_prop( IInspectable *vm, const WCHAR *const *props, unsigned int count,
                                      WCHAR *out, size_t out_len )
{
    return uwp_vm_query_string_prop_depth( vm, props, count, out, out_len, 0 );
}

static struct uwp_element *g_paint_ancestors[48];
static int g_paint_depth = 0;

static void uwp_gpu_paint_element_inner( ID3D11DeviceContext *ctx, struct uwp_element *el );

static void uwp_gpu_paint_element( ID3D11DeviceContext *ctx, struct uwp_element *el )
{
    if (!el || g_paint_depth >= 48) return;
    g_paint_ancestors[g_paint_depth++] = el;
    uwp_gpu_paint_element_inner( ctx, el );
    g_paint_depth--;
}

static void uwp_gpu_paint_element_inner( ID3D11DeviceContext *ctx, struct uwp_element *el )
{
    COLORREF surface_bg = g_paint_page_bg;
    TRACE( "paint-enter %s (%s) bounds=%d vis=%u kids=%p content=%p\n",
           debugstr_w(el ? el->class_name : L"?"), debugstr_w(el ? el->name : L"?"),
           el ? el->has_bounds : 0, el ? el->visibility : 0xffff,
           (void *)(el ? el->children_coll : NULL), (void *)(el ? el->content : NULL) );
    if (!el || !el->has_bounds || el->visibility != 0) return;
    if (el->evc_cnt > 0 && !el->evc_fired)
        uwp_fire_evc_if_needed( el );
    if (el->items_source && !el->children_coll)
    {
        uwp_items_ctrl_materialize_source( el, el->items_source );
        if (el->children_coll)
        {
            g_layout_dirty = TRUE;
            uwp_invalidate_window();
        }
    }

    if (el->kind == UWP_KIND_IMAGE)
    {
        if (!el->image_src[0] && el->img_source)
        {
            struct uwp_element *src_el = uwp_elem_from_any( el->img_source );
            if (src_el && src_el->image_src[0])
            {
                lstrcpynW( el->image_src, src_el->image_src, ARRAY_SIZE(el->image_src) );
                el->img_loaded = FALSE;
                InterlockedExchange( &el->img_loading, 0 );
            }
        }
        if (!el->image_src[0])
        {
            static const WCHAR *const img_props[] = { L"Uri", L"Url", L"ImageUrl", L"ImageUri", L"Source", L"IconUrl", L"IconUri", L"TileImageUrl", L"BoxArtUrl", L"PosterUrl", L"ThumbnailUrl", L"Images", L"Image", L"Icon" };
            int ai;
            int min_ai = max( 0, g_paint_depth - 7 );
            for (ai = g_paint_depth - 2; ai >= min_ai && !el->image_src[0]; ai--)
            {
                struct uwp_element *p = g_paint_ancestors[ai];
                int di;
                if (!p) continue;
                for (di = 0; di < p->dp_cnt && p->dp_store && !el->image_src[0]; di++)
                {
                    IInspectable *dv = p->dp_store[di].value;
                    struct uwp_element *bel;
                    WCHAR tmp[512] = {0};
                    if (!dv) continue;
                    bel = uwp_elem_from_any( dv );
                    if (bel && bel->image_src[0])
                        lstrcpynW( el->image_src, bel->image_src, ARRAY_SIZE(el->image_src) );
                    else
                    {
                        if (!uwp_unbox_string( dv, tmp, ARRAY_SIZE(tmp) ))
                            uwp_extract_uri_string( dv, tmp, ARRAY_SIZE(tmp) );
                        if (tmp[0] && (wcsstr( tmp, L"http://" ) || wcsstr( tmp, L"https://" ) || wcsstr( tmp, L"ms-appx://" )))
                            lstrcpynW( el->image_src, tmp, ARRAY_SIZE(el->image_src) );
                    }
                }
                if (!el->image_src[0] && p->data_context)
                {
                    WCHAR img_buf[512] = {0};
                    if (uwp_vm_query_string_prop_depth( p->data_context, img_props, ARRAY_SIZE(img_props), img_buf, ARRAY_SIZE(img_buf), 2 ) &&
                        img_buf[0])
                        lstrcpynW( el->image_src, img_buf, ARRAY_SIZE(el->image_src) );
                }
            }
        }
        uwp_load_image_asset( el );
        if (el->img_pixels && el->img_w > 0 && el->img_h > 0)
        {
            RECT rc = el->bounds, dst;
            int bw = rc.right - rc.left, bh = rc.bottom - rc.top;
            int dw = (int)el->img_w, dh = (int)el->img_h;
            int dx, dy;
            if (bw > 0 && bh > 0 && dw > 0 && dh > 0)
            {
                if (dw * bh > dh * bw) { dh = MulDiv( dh, bw, dw ); dw = bw; }
                else { dw = MulDiv( dw, bh, dh ); dh = bh; }
            }
            dx = rc.left + (bw - dw) / 2;
            dy = rc.top + (bh - dh) / 2;
            SetRect( &dst, dx, dy, dx + dw, dy + dh );
            xaml_gpu_push_image( ctx, (UINT64)(ULONG_PTR)el, el->img_pixels, el->img_w, el->img_h, &dst, NULL );
        }
        return;
    }

    if (el->fill_brush || el->ecp_visual || wcsstr( el->class_name, L"Rectangle" ))
    {
        WCHAR resolved_uri[512] = {0};
        if (el->fill_brush)
        {
            struct uwp_element *b_el;
            uwp_xcbb_ensure_connected( el->fill_brush );
            b_el = uwp_elem_from_any( el->fill_brush );
            if (b_el)
            {
                if (b_el->image_src[0])
                    lstrcpynW( resolved_uri, b_el->image_src, ARRAY_SIZE(resolved_uri) );
                else if (b_el->comp_brush)
                    uwp_extract_comp_brush_image_src( b_el->comp_brush, resolved_uri, ARRAY_SIZE(resolved_uri) );
            }
        }
        if (!resolved_uri[0] && el->dp_cnt > 0 && el->dp_store)
        {
            int di;
            for (di = 0; di < el->dp_cnt; di++)
            {
                struct uwp_element *b_el = el->dp_store[di].value ? uwp_elem_from_any( el->dp_store[di].value ) : NULL;
                if (!b_el) continue;
                if (wcsstr( b_el->class_name, L"Brush" ))
                    uwp_xcbb_ensure_connected( el->dp_store[di].value );
                if (b_el->image_src[0])
                {
                    lstrcpynW( resolved_uri, b_el->image_src, ARRAY_SIZE(resolved_uri) );
                    break;
                }
                if (b_el->comp_brush && uwp_extract_comp_brush_image_src( b_el->comp_brush, resolved_uri, ARRAY_SIZE(resolved_uri) ))
                    break;
            }
        }
        if (!resolved_uri[0] && el->ecp_visual)
            uwp_extract_visual_image_src( el->ecp_visual, resolved_uri, ARRAY_SIZE(resolved_uri) );
        if (resolved_uri[0] && wcscmp( el->image_src, resolved_uri ))
        {
            lstrcpynW( el->image_src, resolved_uri, ARRAY_SIZE(el->image_src) );
            el->img_loaded = FALSE;
            InterlockedExchange( &el->img_loading, 0 );
        }
        if (el->image_src[0])
        {
            uwp_load_image_asset( el );
            if (el->img_pixels && el->img_w > 0 && el->img_h > 0)
            {
                RECT rc = el->bounds;
                xaml_gpu_push_image( ctx, (UINT64)(ULONG_PTR)el, el->img_pixels, el->img_w, el->img_h, &rc, NULL );
            }
        }
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

    if (wcsstr( el->class_name, L"AppTitleBar" ))
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
    else if (el->kind != UWP_KIND_BUTTON && el->kind != UWP_KIND_TOGGLE_BUTTON && el->kind != UWP_KIND_TEXT_BLOCK)
    {
        BOOL skip_fullscreen_bg = xaml_has_active_swapchain_panel() &&
            (el->bounds.right - el->bounds.left) >= 1000 &&
            (el->bounds.bottom - el->bounds.top) >= 600;
        if ((!skip_fullscreen_bg && el->has_bg && el->bg_alpha > 0) || (el->has_border && el->border_alpha > 0))
        {
            UINT32 fill_c = (!skip_fullscreen_bg && el->has_bg && el->bg_alpha > 0) ? cr_to_argb( blend_argb_over( el->bg_argb, surface_bg ) ) : 0;
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

        if (!label[0] && (!el->content || !uwp_elem_from_any( el->content )))
        {
            IInspectable *vm = (el->content && !uwp_elem_from_any( el->content )) ? el->content : el->data_context;
            if (vm)
            {
                static const WCHAR *const title_props[] = { L"Title", L"ProductTitle", L"Name", L"Label", L"DisplayName", L"Header", L"Text", L"ShortTitle" };
                static const WCHAR *const sub_props[] = { L"DisplayPrice", L"FormattedPrice", L"Price", L"Category", L"PublisherName", L"Subtitle", L"AverageRating" };
                static const WCHAR *const nested_props[] = { L"Product", L"ViewModel", L"Model", L"Data", L"Item", L"ProductSummary" };
                WCHAR title_buf[128] = {0}, sub_buf[128] = {0};
                IInspectable *target_vm = vm;
                IInspectable *nested_vm = NULL;

                if (!uwp_vm_query_string_prop( vm, title_props, ARRAY_SIZE(title_props), title_buf, ARRAY_SIZE(title_buf) ))
                {
                    HSTRING hcls = NULL;
                    if (SUCCEEDED(IInspectable_GetRuntimeClassName( vm, &hcls )) && hcls)
                    {
                        const WCHAR *cls = WindowsGetStringRawBuffer( hcls, NULL );
                        unsigned int ni;
                        for (ni = 0; ni < ARRAY_SIZE(nested_props) && !nested_vm; ni++)
                        {
                            IInspectable *nv = NULL;
                            if (cls && cls[0] && uwp_get_custom_xaml_member( uwp_get_app_outer(), cls, nested_props[ni], vm, &nv ) && nv)
                                nested_vm = nv;
                        }
                        WindowsDeleteString( hcls );
                    }
                    if (nested_vm)
                    {
                        target_vm = nested_vm;
                        uwp_vm_query_string_prop( target_vm, title_props, ARRAY_SIZE(title_props), title_buf, ARRAY_SIZE(title_buf) );
                    }
                }
                if (title_buf[0])
                {
                    if (uwp_vm_query_string_prop( target_vm, sub_props, ARRAY_SIZE(sub_props), sub_buf, ARRAY_SIZE(sub_buf) ))
                        swprintf( el->text, ARRAY_SIZE(el->text), L"%.120s\n%.120s", title_buf, sub_buf );
                    else
                        lstrcpynW( el->text, title_buf, ARRAY_SIZE(el->text) );
                    label = el->text;
                }
                if (nested_vm) IInspectable_Release( nested_vm );
            }
        }

        if (el->data_context && el->parent &&
            (wcsstr( el->parent->class_name, L"ListView" ) || wcsstr( el->parent->class_name, L"GridView" )) &&
            (brc.right - brc.left) >= 120 && (brc.bottom - brc.top) >= 48)
        {
            if (!el->image_src[0])
            {
                static const WCHAR *const item_img_props[] = { L"Uri", L"Url", L"ImageUrl", L"ImageUri", L"ThumbnailUrl", L"PreviewImageUrl", L"Image", L"Images", L"Source", L"IconUrl", L"BoxArtUrl", L"PosterUrl" };
                WCHAR img_buf[512] = {0};
                if (uwp_vm_query_string_prop_depth( el->data_context, item_img_props, ARRAY_SIZE(item_img_props), img_buf, ARRAY_SIZE(img_buf), 2 ) &&
                    img_buf[0])
                    lstrcpynW( el->image_src, img_buf, ARRAY_SIZE(el->image_src) );
            }
            if (el->image_src[0])
                uwp_load_image_asset( el );
            if (el->img_pixels && el->img_w > 0 && el->img_h > 0)
            {
                RECT irc = brc;
                InflateRect( &irc, -2, -2 );
                xaml_gpu_push_rect( (float)brc.left, (float)brc.top, (float)brc.right, (float)brc.bottom, NULL,
                                    6.0f, cr_to_argb( RGB( 32, 32, 32 ) ), cr_to_argb( RGB( 68, 68, 68 ) ), 1.0f, 1.0f );
                xaml_gpu_push_image( ctx, (UINT64)(ULONG_PTR)el, el->img_pixels, el->img_w, el->img_h, &irc, NULL );
                return;
            }
        }

        if (label[0] && (wcschr( label, '\n' ) || wcsstr( el->class_name, L"Card" )) &&
            (brc.right - brc.left) >= 160 && (brc.bottom - brc.top) >= 60)
        {
            const WCHAR *nl = wcschr( label, '\n' );
            WCHAR tline[128] = {0};
            RECT trc, src;
            size_t tlen = nl ? (size_t)(nl - label) : wcslen( label );
            if (tlen >= ARRAY_SIZE(tline)) tlen = ARRAY_SIZE(tline) - 1;
            memcpy( tline, label, tlen * sizeof(WCHAR) );
            tline[tlen] = 0;
            InflateRect( &brc, -2, -2 );
            xaml_gpu_push_rect( (float)brc.left, (float)brc.top, (float)brc.right, (float)brc.bottom, NULL,
                                6.0f, cr_to_argb( el->is_hovered ? RGB( 52, 52, 52 ) : RGB( 42, 42, 42 ) ),
                                cr_to_argb( RGB( 68, 68, 68 ) ), 1.0f, 1.0f );
            SetRect( &trc, brc.left + 14, brc.top + 14, brc.right - 14, brc.top + 48 );
            xaml_gpu_push_text( ctx, tline, 14, FW_SEMIBOLD, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS,
                                &trc, NULL, RGB( 255, 255, 255 ), 1.0f );
            if (nl)
            {
                SetRect( &src, brc.left + 14, brc.top + 52, brc.right - 14, brc.bottom - 12 );
                xaml_gpu_push_text( ctx, nl + 1, 12, FW_NORMAL, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS,
                                    &src, NULL, RGB( 180, 180, 180 ), 1.0f );
            }
            return;
        }

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

        if (!label[0] && !el->has_bg && !el->is_accent_style && !el->is_hovered && !el->is_pressed)
        {
            draw_bg = FALSE;
            draw_bd = FALSE;
            bg = surface_bg;
        }
        else if (el->has_bg)
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
        else if (el->is_accent_style)
        {
            bg = uwp_paint_resolve_color( el, L"AccentFillColorDefaultBrush",
                     uwp_paint_resolve_color( el, L"AccentBrush", RGB( 0, 120, 215 ), surface_bg ), surface_bg );
            if (el->is_pressed)
                bg = RGB( max( 0, (int)GetRValue(bg) - 25 ), max( 0, (int)GetGValue(bg) - 25 ), max( 0, (int)GetBValue(bg) - 25 ) );
        }
        else
        {
            bg = uwp_paint_resolve_color( el, L"ControlFillColorDefaultBrush", RGB( 50, 50, 50 ), surface_bg );
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
        if (el->content_kind == UWP_CONTENT_ELEMENT || (el->content && uwp_elem_from_any( el->content )))
        {
            struct uwp_element *ce = uwp_elem_from_any( el->content );
            if (ce && ce != el) uwp_gpu_paint_element( ctx, ce );
        }
        else if (label[0])
        {
            xaml_gpu_push_text( ctx, label, fh, FW_SEMIBOLD, DT_CENTER | DT_VCENTER | DT_SINGLELINE, &brc, NULL, fg, 1.0f );
        }
        return;
    }

    if (el->kind == UWP_KIND_TEXT_BLOCK)
    {
        if (el->tpl_normal_output && el->content == &el->tpl_normal_output->IInspectable_iface &&
            el->tpl_normal_output->has_bounds && el->tpl_normal_output->visibility == 0)
        {
            if (el->text[0] && wcscmp( el->tpl_normal_output->text, el->text ) != 0)
                lstrcpynW( el->tpl_normal_output->text, el->text, ARRAY_SIZE(el->tpl_normal_output->text) );
            uwp_gpu_paint_element( ctx, el->tpl_normal_output );
            return;
        }
        {
            WCHAR rich_buf[1024];
            const WCHAR *draw_txt = el->text;
            BOOL is_rich = FALSE;
            if (!draw_txt[0] && el->inlines_coll)
            {
                struct uwp_vec_view *v = CONTAINING_RECORD( el->inlines_coll, struct uwp_vec_view, IInspectable_iface );
                unsigned int i, j;
                size_t pos = 0;
                rich_buf[0] = 0;
                for (i = 0; v && i < v->count && pos + 2 < ARRAY_SIZE(rich_buf); i++)
                {
                    struct uwp_element *blk = uwp_elem_from_any( v->items[i] );
                    if (!blk) continue;
                    if (blk->text[0])
                    {
                        if (pos > 0 && rich_buf[pos - 1] != '\n') rich_buf[pos++] = '\n';
                        lstrcpynW( rich_buf + pos, blk->text, (int)(ARRAY_SIZE(rich_buf) - pos) );
                        pos += wcslen( rich_buf + pos );
                    }
                    else if (blk->inlines_coll)
                    {
                        struct uwp_vec_view *iv = CONTAINING_RECORD( blk->inlines_coll, struct uwp_vec_view, IInspectable_iface );
                        if (pos > 0 && rich_buf[pos - 1] != '\n') rich_buf[pos++] = '\n';
                        for (j = 0; iv && j < iv->count && pos + 2 < ARRAY_SIZE(rich_buf); j++)
                        {
                            struct uwp_element *inl = uwp_elem_from_any( iv->items[j] );
                            if (!inl && iv->items[j])
                            {
                                static const GUID IID_IRun_g =
                                    { 0x59553c83, 0x0e14, 0x49bd, { 0xb8, 0x4b, 0xc5, 0x26, 0xf3, 0x03, 0x43, 0x49 } };
                                void *run_iface = NULL;
                                if (SUCCEEDED(IUnknown_QueryInterface( (IUnknown *)iv->items[j], &IID_IRun_g, &run_iface )) && run_iface)
                                {
                                    HSTRING htxt = NULL;
                                    HRESULT (WINAPI *get_txt)(void *, HSTRING *) = (void *)(*(void ***)run_iface)[6];
                                    if (get_txt && SUCCEEDED(get_txt( run_iface, &htxt )) && htxt)
                                    {
                                        const WCHAR *s = WindowsGetStringRawBuffer( htxt, NULL );
                                        if (s && s[0])
                                        {
                                            lstrcpynW( rich_buf + pos, s, (int)(ARRAY_SIZE(rich_buf) - pos) );
                                            pos += wcslen( rich_buf + pos );
                                        }
                                        WindowsDeleteString( htxt );
                                    }
                                    IUnknown_Release( (IUnknown *)run_iface );
                                }
                                else
                                {
                                    static const GUID IID_ISpan_g =
                                        { 0x54f03373, 0xc273, 0x4f3b, { 0xb3, 0x71, 0x6e, 0x2b, 0x4b, 0x2a, 0x96, 0x91 } };
                                    void *span_iface = NULL;
                                    if (SUCCEEDED(IUnknown_QueryInterface( (IUnknown *)iv->items[j], &IID_ISpan_g, &span_iface )) && span_iface)
                                    {
                                        void *sub_inlines = NULL;
                                        HRESULT (WINAPI *get_inlines)(void *, void **) = (void *)(*(void ***)span_iface)[6];
                                        if (get_inlines && SUCCEEDED(get_inlines( span_iface, &sub_inlines )) && sub_inlines)
                                        {
                                            struct uwp_vec_view *sv = CONTAINING_RECORD( sub_inlines, struct uwp_vec_view, IInspectable_iface );
                                            unsigned int k;
                                            for (k = 0; sv && k < sv->count && pos + 2 < ARRAY_SIZE(rich_buf); k++)
                                            {
                                                struct uwp_element *sub = uwp_elem_from_any( sv->items[k] );
                                                if (sub && sub->text[0])
                                                {
                                                    lstrcpynW( rich_buf + pos, sub->text, (int)(ARRAY_SIZE(rich_buf) - pos) );
                                                    pos += wcslen( rich_buf + pos );
                                                }
                                            }
                                            IInspectable_Release( (IInspectable *)sub_inlines );
                                        }
                                        IUnknown_Release( (IUnknown *)span_iface );
                                    }
                                }
                            }
                            else if (inl && inl->text[0])
                            {
                                lstrcpynW( rich_buf + pos, inl->text, (int)(ARRAY_SIZE(rich_buf) - pos) );
                                pos += wcslen( rich_buf + pos );
                            }
                            else if (inl && inl->inlines_coll)
                            {
                                struct uwp_vec_view *sv = CONTAINING_RECORD( inl->inlines_coll, struct uwp_vec_view, IInspectable_iface );
                                unsigned int k;
                                for (k = 0; sv && k < sv->count && pos + 2 < ARRAY_SIZE(rich_buf); k++)
                                {
                                    struct uwp_element *sub = uwp_elem_from_any( sv->items[k] );
                                    if (sub && sub->text[0])
                                    {
                                        lstrcpynW( rich_buf + pos, sub->text, (int)(ARRAY_SIZE(rich_buf) - pos) );
                                        pos += wcslen( rich_buf + pos );
                                    }
                                }
                            }
                        }
                    }
                }
                if (rich_buf[0]) { draw_txt = rich_buf; is_rich = TRUE; }
            }
            if (draw_txt[0])
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
                xaml_gpu_push_text( ctx, draw_txt, fh, is_result ? FW_SEMIBOLD : FW_NORMAL,
                                    is_rich ? (DT_LEFT | DT_TOP | DT_WORDBREAK)
                                            : ((is_result ? DT_RIGHT : DT_LEFT) | DT_VCENTER | DT_SINGLELINE),
                                    &trc, NULL, tfg, 1.0f );
            }
        }
    }

    if (el->content)
    {
        struct uwp_element *ch = uwp_elem_from_any( el->content );
        if (ch && ch != el)
        {
            uwp_gpu_paint_element( ctx, ch );
        }
        else if (el->kind == UWP_KIND_CONTENT_CONTROL)
        {
            WCHAR label[128] = {0};
            if (el->text[0])
            {
                lstrcpynW( label, el->text, ARRAY_SIZE(label) );
            }
            else if (!uwp_unbox_string( el->content, label, ARRAY_SIZE(label) ))
            {
                /* Resolve ViewModel label via IXamlMember (Title/Label/DisplayName/Text) or IStringable */
                HSTRING hcls = NULL;
                if (SUCCEEDED(IInspectable_GetRuntimeClassName( el->content, &hcls )) && hcls)
                {
                    const WCHAR *cls = WindowsGetStringRawBuffer( hcls, NULL );
                    if (cls && cls[0])
                    {
                        static const WCHAR *const prop_names[] = { L"Title", L"Label", L"DisplayName", L"Header", L"Text", L"Name" };
                        unsigned int pi;
                        for (pi = 0; pi < ARRAY_SIZE(prop_names) && !label[0]; pi++)
                        {
                            IInspectable *pval = NULL;
                            if (uwp_get_custom_xaml_member( uwp_get_app_outer(), cls, prop_names[pi], el->content, &pval ) && pval)
                            {
                                uwp_unbox_string( pval, label, ARRAY_SIZE(label) );
                                IInspectable_Release( pval );
                            }
                        }
                    }
                    WindowsDeleteString( hcls );
                }
                if (!label[0])
                {
                    /* Try ICustomPropertyProvider ({7c925755-3e48-42b4-8677-76372267033f}) for managed ViewModels */
                    static const GUID IID_ICustomPropertyProvider_gpu =
                        { 0x7c925755, 0x3e48, 0x42b4, { 0x86, 0x77, 0x76, 0x37, 0x22, 0x67, 0x03, 0x3f } };
                    void *cpp = NULL;
                    if (SUCCEEDED(IInspectable_QueryInterface( el->content, &IID_ICustomPropertyProvider_gpu, &cpp )) && cpp)
                    {
                        static const WCHAR *const prop_names[] = { L"Title", L"Label", L"DisplayName", L"Header", L"Text", L"Content", L"Name" };
                        HRESULT (WINAPI *get_prop)(void *, HSTRING, void **) = (void *)(*(void ***)cpp)[6];
                        unsigned int pi;
                        for (pi = 0; pi < ARRAY_SIZE(prop_names) && !label[0]; pi++)
                        {
                            HSTRING hp = NULL;
                            void *cp = NULL;
                            if (get_prop && SUCCEEDED(WindowsCreateString( prop_names[pi], wcslen(prop_names[pi]), &hp )) && hp)
                            {
                                if (SUCCEEDED(get_prop( cpp, hp, &cp )) && cp)
                                {
                                    HRESULT (WINAPI *get_val)(void *, IInspectable *, IInspectable **) = (void *)(*(void ***)cp)[8];
                                    IInspectable *pval = NULL;
                                    if (get_val && SUCCEEDED(get_val( cp, el->content, &pval )) && pval)
                                    {
                                        uwp_unbox_string( pval, label, ARRAY_SIZE(label) );
                                        IInspectable_Release( pval );
                                    }
                                    IUnknown_Release( (IUnknown *)cp );
                                }
                                WindowsDeleteString( hp );
                            }
                        }
                        IUnknown_Release( (IUnknown *)cpp );
                    }
                }
                if (!label[0])
                {
                    static const GUID IID_IStringable_gpu =
                        { 0x96369f54, 0x8eb6, 0x48f0, { 0xab, 0xce, 0xc1, 0xb2, 0x11, 0xe6, 0x27, 0xc3 } };
                    void *strable = NULL;
                    if (SUCCEEDED(IInspectable_QueryInterface( el->content, &IID_IStringable_gpu, &strable )) && strable)
                    {
                        HSTRING hs = NULL;
                        HRESULT (WINAPI *to_str)(void *, HSTRING *) = (void *)(*(void ***)strable)[6];
                        if (to_str && SUCCEEDED(to_str( strable, &hs )) && hs)
                        {
                            const WCHAR *s = WindowsGetStringRawBuffer( hs, NULL );
                            if (s && s[0] && wcsncmp( s, L"WinStore.", 9 ) && wcsncmp( s, L"Windows.", 8 ))
                                lstrcpynW( label, s, ARRAY_SIZE(label) );
                            WindowsDeleteString( hs );
                        }
                        IUnknown_Release( (IUnknown *)strable );
                    }
                }
                if (label[0])
                    lstrcpynW( el->text, label, ARRAY_SIZE(el->text) );
            }
            if (label[0])
            {
                RECT brc = el->bounds;
                COLORREF bg = (el->is_pressed || el->is_hovered) ? RGB( 60, 60, 60 ) : RGB( 42, 42, 42 );
                COLORREF fg = uwp_paint_resolve_color( el, L"DefaultTextForegroundThemeBrush", RGB( 240, 240, 240 ), surface_bg );
                InflateRect( &brc, -2, -4 );
                xaml_gpu_push_rect( (float)brc.left, (float)brc.top, (float)brc.right, (float)brc.bottom, NULL,
                                    6.0f, cr_to_argb( bg ), 0, 0.0f, 1.0f );
                xaml_gpu_push_text( ctx, label, 11, FW_NORMAL, DT_CENTER | DT_VCENTER | DT_SINGLELINE,
                                    &brc, NULL, fg, 1.0f );
            }
        }
    }
    if (!el->children_coll && (el->data_context || (el->content && !uwp_elem_from_any( el->content ))) &&
        !(el->content && uwp_elem_from_any( el->content )) &&
        el->kind != UWP_KIND_CONTENT_CONTROL &&
        (el->bounds.right - el->bounds.left) >= 140 && (el->bounds.bottom - el->bounds.top) >= 56)
    {
        if (!el->text[0])
        {
            static const WCHAR *const title_props[] = { L"Title", L"ProductTitle", L"Name", L"Label", L"DisplayName", L"Header", L"Text", L"ShortTitle" };
            static const WCHAR *const sub_props[] = { L"DisplayPrice", L"PriceText", L"FormattedPrice", L"Price", L"Category", L"PublisherName", L"Publisher", L"Subtitle", L"Description" };
            static const WCHAR *const img_props[] = { L"Images", L"TileImages", L"ProductImages", L"Icons", L"ImageUrl", L"IconUrl", L"IconUri", L"ImageUri", L"ProductImageUrl", L"HeroImageUrl", L"TileImageUrl", L"LogoUrl", L"ThumbnailUrl", L"BoxArtUrl", L"PosterUrl", L"ImageLinkUri", L"TileImage", L"ProductImage", L"IconImage", L"LogoImage", L"BackgroundImage", L"Icon", L"Image", L"ImageSource", L"Uri" };
            static const WCHAR *const nested_props[] = { L"Content", L"Card", L"Product", L"ViewModel", L"Model", L"Data", L"Item", L"ProductSummary" };
            WCHAR title_buf[128] = {0}, sub_buf[128] = {0}, img_buf[512] = {0};
            IInspectable *vm = (el->content && !uwp_elem_from_any( el->content )) ? el->content : el->data_context;
            IInspectable *target_vm = vm;
            IInspectable *nested_vm = NULL;
            HSTRING hcls = NULL;

            if (SUCCEEDED(IInspectable_GetRuntimeClassName( vm, &hcls )) && hcls)
            {
                const WCHAR *cls = WindowsGetStringRawBuffer( hcls, NULL );
                unsigned int ni;
                for (ni = 0; ni < ARRAY_SIZE(nested_props) && !nested_vm; ni++)
                {
                    IInspectable *nv = NULL;
                    if (cls && cls[0] && uwp_get_custom_xaml_member( uwp_get_app_outer(), cls, nested_props[ni], vm, &nv ) && nv)
                        nested_vm = nv;
                }
                WindowsDeleteString( hcls );
            }
            if (!uwp_vm_query_string_prop( vm, title_props, ARRAY_SIZE(title_props), title_buf, ARRAY_SIZE(title_buf) ) && nested_vm)
            {
                target_vm = nested_vm;
                uwp_vm_query_string_prop( target_vm, title_props, ARRAY_SIZE(title_props), title_buf, ARRAY_SIZE(title_buf) );
            }
            if (title_buf[0])
            {
                if (!uwp_vm_query_string_prop( target_vm, sub_props, ARRAY_SIZE(sub_props), sub_buf, ARRAY_SIZE(sub_buf) ) &&
                    nested_vm && target_vm != nested_vm)
                    uwp_vm_query_string_prop( nested_vm, sub_props, ARRAY_SIZE(sub_props), sub_buf, ARRAY_SIZE(sub_buf) );
                if (sub_buf[0])
                    swprintf( el->text, ARRAY_SIZE(el->text), L"%.120s\n%.120s", title_buf, sub_buf );
                else
                    lstrcpynW( el->text, title_buf, ARRAY_SIZE(el->text) );
                if (!el->image_src[0])
                {
                    if (!uwp_vm_query_string_prop_depth( target_vm, img_props, ARRAY_SIZE(img_props), img_buf, ARRAY_SIZE(img_buf), 2 ) &&
                        nested_vm && target_vm != nested_vm)
                        uwp_vm_query_string_prop_depth( nested_vm, img_props, ARRAY_SIZE(img_props), img_buf, ARRAY_SIZE(img_buf), 2 );
                    if (img_buf[0])
                        lstrcpynW( el->image_src, img_buf, ARRAY_SIZE(el->image_src) );
                }
            }
            if (nested_vm) IInspectable_Release( nested_vm );
        }
        if (el->text[0])
        {
            RECT brc = el->bounds, trc, src;
            int text_left;
            const WCHAR *nl = wcschr( el->text, '\n' );
            WCHAR tline[128] = {0};
            size_t tlen = nl ? (size_t)(nl - el->text) : wcslen( el->text );
            if (tlen >= ARRAY_SIZE(tline)) tlen = ARRAY_SIZE(tline) - 1;
            memcpy( tline, el->text, tlen * sizeof(WCHAR) );
            tline[tlen] = 0;
            if (!el->image_src[0] && el->data_context)
            {
                static const WCHAR *const item_img_props[] = { L"Uri", L"Url", L"ImageUrl", L"ImageUri", L"ThumbnailUrl", L"PreviewImageUrl", L"Image", L"Images", L"Source" };
                WCHAR img_buf[512] = {0};
                if (uwp_vm_query_string_prop_depth( el->data_context, item_img_props, ARRAY_SIZE(item_img_props), img_buf, ARRAY_SIZE(img_buf), 2 ) &&
                    img_buf[0])
                    lstrcpynW( el->image_src, img_buf, ARRAY_SIZE(el->image_src) );
            }
            InflateRect( &brc, -2, -2 );
            xaml_gpu_push_rect( (float)brc.left, (float)brc.top, (float)brc.right, (float)brc.bottom, NULL,
                                6.0f, cr_to_argb( el->is_hovered ? RGB( 52, 52, 52 ) : RGB( 42, 42, 42 ) ),
                                cr_to_argb( RGB( 68, 68, 68 ) ), 1.0f, 1.0f );
            text_left = brc.left + 14;
            if (el->image_src[0])
                uwp_load_image_asset( el );
            if (el->img_pixels && el->img_w > 0 && el->img_h > 0)
            {
                if (!nl && el->parent && (wcsstr( el->parent->class_name, L"ListView" ) || wcsstr( el->parent->class_name, L"GridView" )))
                {
                    RECT irc = brc;
                    InflateRect( &irc, -2, -2 );
                    xaml_gpu_push_image( ctx, (UINT64)(ULONG_PTR)el, el->img_pixels, el->img_w, el->img_h, &irc, NULL );
                    tline[0] = 0;
                }
                else
                {
                    int bh = brc.bottom - brc.top;
                    int isz = min( 56, max( 32, bh - 20 ) );
                    int iy = brc.top + (bh - isz) / 2;
                    RECT irc = { brc.left + 12, iy, brc.left + 12 + isz, iy + isz };
                    xaml_gpu_push_image( ctx, (UINT64)(ULONG_PTR)el, el->img_pixels, el->img_w, el->img_h, &irc, NULL );
                    text_left = irc.right + 12;
                }
            }
            if (tline[0])
            {
                SetRect( &trc, text_left, brc.top + 14, brc.right - 14, brc.top + 48 );
                xaml_gpu_push_text( ctx, tline, 14, FW_SEMIBOLD, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS,
                                    &trc, NULL, RGB( 255, 255, 255 ), 1.0f );
            }
            if (nl)
            {
                SetRect( &src, text_left, brc.top + 52, brc.right - 14, brc.bottom - 12 );
                xaml_gpu_push_text( ctx, nl + 1, 12, FW_NORMAL, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS,
                                    &src, NULL, RGB( 180, 180, 180 ), 1.0f );
            }
        }
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
    AcquireSRWLockShared( &g_dyn_factories_lock );
    for (i = 0; i < g_dyn_factory_cnt; i++)
    {
        if (g_dyn_factories[i] &&
            (char *)iface >= (char *)g_dyn_factories[i] &&
            (char *)iface < (char *)(g_dyn_factories[i] + 1))
        {
            struct uwp_class_factory *cf = g_dyn_factories[i];
            ReleaseSRWLockShared( &g_dyn_factories_lock );
            return cf;
        }
    }
    ReleaseSRWLockShared( &g_dyn_factories_lock );
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
static const GUID IID_IElementCompositionPreviewStatics_WinUI3 =
    { 0x585a3a99, 0x9d25, 0x5673, { 0xa1, 0x0d, 0xd1, 0xf2, 0x92, 0xb8, 0x50, 0x53 } };
static const GUID IID_IElementCompositionPreviewStatics_WinUI3_v2 =
    { 0x84da5a6c, 0x0cfa, 0x532b, { 0x9b, 0x15, 0xcc, 0xd9, 0x86, 0x37, 0x43, 0x42 } };

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
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = iface;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IElementCompositionPreviewStatics_UWP ) ||
        IsEqualGUID( iid, &IID_IElementCompositionPreviewStatics_WinUI3 ) ||
        IsEqualGUID( iid, &IID_IElementCompositionPreviewStatics_WinUI3_v2 ))
    {
        *out = (void *)&ecp_statics_obj;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IElementCompositionPreviewStatics2_UWP ))
    {
        *out = (void *)&ecp_statics2_obj;
        return S_OK;
    }
    ERR( "ecp_QueryInterface unknown iid %s\n", debugstr_guid( iid ) );
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
static IDCompositionVisual *g_ecp_child_visual = NULL;

static HRESULT WINAPI ecp_SetElementChildVisual( void *iface, void *elem, void *visual )
{
    IDCompositionVisual *root = xaml_get_dcomp_root_visual();
    IDCompositionVisual *dcomp_vis = NULL;
    struct uwp_element *el = uwp_elem_from_any( elem );
    if (el)
    {
        if (el->ecp_visual != (IInspectable *)visual)
        {
            if (el->ecp_visual) IInspectable_Release( el->ecp_visual );
            el->ecp_visual = (IInspectable *)visual;
            if (el->ecp_visual) IInspectable_AddRef( el->ecp_visual );
        }
        if (visual && uwp_extract_visual_image_src( (IInspectable *)visual, el->image_src, ARRAY_SIZE(el->image_src) ))
        {
            TRACE( "ecp_SetElementChildVisual(%s): extracted uri=%s\n",
                   debugstr_w(el->name), debugstr_w(el->image_src) );
            el->img_loaded = FALSE;
            InterlockedExchange( &el->img_loading, 0 );
            uwp_load_image_asset( el );
            uwp_invalidate_window();
        }
    }
    if (visual)
        IUnknown_QueryInterface( (IUnknown *)visual, &IID_IDCompositionVisual, (void **)&dcomp_vis );
    if (root)
    {
        if (g_ecp_child_visual && g_ecp_child_visual != dcomp_vis)
        {
            IDCompositionVisual_RemoveVisual( root, g_ecp_child_visual );
            IDCompositionVisual_Release( g_ecp_child_visual );
            g_ecp_child_visual = NULL;
        }
        if (dcomp_vis && dcomp_vis != root && g_ecp_child_visual != dcomp_vis)
        {
            HRESULT hr_add = IDCompositionVisual_AddVisual( root, dcomp_vis, TRUE, NULL );
            ERR( "ecp_SetElementChildVisual elem=%p vis=%p dcomp=%p root=%p hr=%#lx\n",
                 elem, visual, dcomp_vis, root, hr_add );
            if (SUCCEEDED(hr_add))
            {
                g_ecp_child_visual = dcomp_vis;
                IDCompositionVisual_AddRef( g_ecp_child_visual );
            }
        }
        xaml_dcomp_commit();
    }
    if (dcomp_vis) IDCompositionVisual_Release( dcomp_vis );
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

IActivationFactory *xaml_get_ecp_factory( void )
{
    return (IActivationFactory *)&ecp_statics_obj;
}

struct uwp_size_f_gpu { float width; float height; };
struct uwp_rect_f_gpu { float x; float y; float width; float height; };

static HRESULT WINAPI layout_info_GetAvailableSize( void *iface, void *el_ptr, struct uwp_size_f_gpu *out )
{
    struct uwp_element *el = uwp_elem_from_any( el_ptr );
    if (!out) return E_POINTER;
    out->width = el ? (float)el->actual_w : 0.0f;
    out->height = el ? (float)el->actual_h : 0.0f;
    return S_OK;
}
static HRESULT WINAPI layout_info_GetLayoutExceptionElement( void *iface, void *dispatcher, void **out )
{
    if (out) *out = NULL;
    return S_OK;
}
static HRESULT WINAPI layout_info_GetLayoutSlot( void *iface, void *el_ptr, struct uwp_rect_f_gpu *out )
{
    struct uwp_element *el = uwp_elem_from_any( el_ptr );
    if (!out) return E_POINTER;
    if (el && el->has_bounds)
    {
        out->x = (float)el->bounds.left;
        out->y = (float)el->bounds.top;
        out->width = (float)(el->bounds.right - el->bounds.left);
        out->height = (float)(el->bounds.bottom - el->bounds.top);
    }
    else
    {
        out->x = out->y = out->width = out->height = 0.0f;
    }
    return S_OK;
}
static const void *layout_info_statics_vtbl[] =
{
    ecp_QueryInterface,
    ecp_AddRef,
    ecp_Release,
    ecp_GetIids,
    ecp_GetRuntimeClassName,
    ecp_GetTrustLevel,
    layout_info_GetAvailableSize,
    layout_info_GetLayoutExceptionElement,
    layout_info_GetLayoutSlot,
};
static const void *layout_info_statics_obj = layout_info_statics_vtbl;

static const GUID IID_IConnectedAnimationService_UWP =
    { 0x1c6875c9, 0x19bb, 0x4d47, { 0xb9, 0xaa, 0x66, 0xc8, 0x02, 0xdc, 0xb9, 0xff } };
static const GUID IID_IConnectedAnimationServiceStatics_UWP =
    { 0xc7078ea5, 0xd688, 0x40e8, { 0x8f, 0x90, 0x96, 0xa6, 0x27, 0x92, 0x73, 0xd2 } };

static HRESULT WINAPI cas_QI( void *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IConnectedAnimationService_UWP ))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static HRESULT WINAPI cas_GetRuntimeClassName( void *iface, HSTRING *name )
{
    static const WCHAR cls[] = L"Windows.UI.Xaml.Media.Animation.ConnectedAnimationService";
    return WindowsCreateString( cls, ARRAY_SIZE(cls) - 1, name );
}
static HRESULT WINAPI cas_get_DefaultDuration( void *iface, INT64 *val )
{
    if (val) *val = 3000000; /* 300 ms */
    return S_OK;
}
static HRESULT WINAPI cas_put_DefaultDuration( void *iface, INT64 val ) { return S_OK; }
static HRESULT WINAPI cas_get_DefaultEasingFunction( void *iface, void **out )
{
    if (out) *out = NULL;
    return S_OK;
}
static HRESULT WINAPI cas_put_DefaultEasingFunction( void *iface, void *val ) { return S_OK; }
static HRESULT WINAPI cas_PrepareToAnimate( void *iface, HSTRING key, void *src, void **out )
{
    if (out) *out = NULL;
    return S_OK;
}
static HRESULT WINAPI cas_GetAnimation( void *iface, HSTRING key, void **out )
{
    if (out) *out = NULL;
    return S_OK;
}
static const void *cas_vtbl[] =
{
    cas_QI, ecp_AddRef, ecp_Release, ecp_GetIids, cas_GetRuntimeClassName, ecp_GetTrustLevel,
    cas_get_DefaultDuration, cas_put_DefaultDuration,
    cas_get_DefaultEasingFunction, cas_put_DefaultEasingFunction,
    cas_PrepareToAnimate, cas_GetAnimation,
};
static const void *cas_obj = cas_vtbl;

static HRESULT WINAPI cas_statics_QI( void *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IConnectedAnimationServiceStatics_UWP ))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static HRESULT WINAPI cas_statics_GetForCurrentView( void *iface, void **out )
{
    if (!out) return E_POINTER;
    *out = (void *)&cas_obj;
    return S_OK;
}
static const void *cas_statics_vtbl[] =
{
    cas_statics_QI, ecp_AddRef, ecp_Release, ecp_GetIids, cas_GetRuntimeClassName, ecp_GetTrustLevel,
    cas_statics_GetForCurrentView,
};
static const void *cas_statics_obj = cas_statics_vtbl;

static HRESULT WINAPI uwp_lis_statics_QI( void *iface, REFIID iid, void **out )
{
    static const GUID IID_ILoadedImageSurfaceStatics_UWP =
        { 0x22b8edf6, 0x84ad, 0x40ab, { 0x93, 0x7d, 0x48, 0x71, 0x61, 0x3e, 0x76, 0x5d } };
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_ILoadedImageSurfaceStatics_UWP ))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static HRESULT WINAPI uwp_lis_statics_GetRuntimeClassName( void *iface, HSTRING *n )
{
    return WindowsCreateString( L"Windows.UI.Xaml.Media.LoadedImageSurface", 40, n );
}
static HRESULT WINAPI uwp_lis_StartLoadFromUriWithSize( void *iface, IInspectable *uri,
                                                        struct __x_ABI_CWindows_CFoundation_CSize desired_max_size, IInspectable **out )
{
    void *inst = NULL;
    struct uwp_element *el;
    HRESULT hr;
    if (!out) return E_POINTER;
    *out = NULL;
    hr = uwp_element_create( L"Windows.UI.Xaml.Media.LoadedImageSurface", NULL, NULL, &inst );
    if (FAILED(hr) || !inst) return FAILED(hr) ? hr : E_OUTOFMEMORY;
    el = uwp_elem_from_any( inst );
    if (el)
    {
        if (uri) uwp_extract_uri_string( uri, el->image_src, ARRAY_SIZE(el->image_src) );
        TRACE( "LoadedImageSurface.StartLoadFromUriWithSize uri=%s\n", debugstr_w(el->image_src) );
        if (el->image_src[0]) uwp_load_image_asset( el );
        *out = &el->ILoadedImageSurface_iface;
    }
    else
        *out = (IInspectable *)inst;
    return S_OK;
}
static HRESULT WINAPI uwp_lis_StartLoadFromUri( void *iface, IInspectable *uri, IInspectable **out )
{
    struct __x_ABI_CWindows_CFoundation_CSize sz = { 0.0f, 0.0f };
    return uwp_lis_StartLoadFromUriWithSize( iface, uri, sz, out );
}
static HRESULT WINAPI uwp_lis_StartLoadFromStreamWithSize( void *iface, IInspectable *stream,
                                                           struct __x_ABI_CWindows_CFoundation_CSize desired_max_size, IInspectable **out )
{
    void *inst = NULL;
    struct uwp_element *el;
    HRESULT hr;
    if (!out) return E_POINTER;
    *out = NULL;
    hr = uwp_element_create( L"Windows.UI.Xaml.Media.LoadedImageSurface", NULL, NULL, &inst );
    if (FAILED(hr) || !inst) return FAILED(hr) ? hr : E_OUTOFMEMORY;
    el = uwp_elem_from_any( inst );
    *out = el ? &el->ILoadedImageSurface_iface : (IInspectable *)inst;
    return S_OK;
}
static HRESULT WINAPI uwp_lis_StartLoadFromStream( void *iface, IInspectable *stream, IInspectable **out )
{
    struct __x_ABI_CWindows_CFoundation_CSize sz = { 0.0f, 0.0f };
    return uwp_lis_StartLoadFromStreamWithSize( iface, stream, sz, out );
}
static const void *uwp_lis_statics_vtbl[] =
{
    uwp_lis_statics_QI, ecp_AddRef, ecp_Release, ecp_GetIids, uwp_lis_statics_GetRuntimeClassName, ecp_GetTrustLevel,
    uwp_lis_StartLoadFromUriWithSize,
    uwp_lis_StartLoadFromUri,
    uwp_lis_StartLoadFromStreamWithSize,
    uwp_lis_StartLoadFromStream,
};
static const void *uwp_lis_statics_obj = uwp_lis_statics_vtbl;

static const void *comp_target_statics_obj;
static const void *binding_ops_statics_obj;
static const void *thickness_helper_statics_obj;
static const void *uwp_uielement_statics_obj;
static const void *uwp_uielement_routed_statics_obj;
static const void *uwp_grid_statics_obj;
static const GUID IID_IUIElementStatics4_UWP;
static const GUID IID_IUIElementStatics6_UWP;
static const GUID IID_IUIElementStatics7_UWP;
static const GUID IID_IUIElementStatics8_UWP;
static const GUID IID_IRoutedEventArgsFactory_WinUI3 =
    { 0x914b02c7, 0x076b, 0x5b89, { 0x98, 0xe7, 0x6c, 0x37, 0x33, 0x79, 0xe9, 0xaf } };

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
        static const GUID IID_ISplitViewStatics_UWP =
            { 0x859b4f6f, 0x44ab, 0x4e4b, { 0x91, 0xc1, 0x17, 0xb7, 0x05, 0x6d, 0x9b, 0x5f } };
        static const GUID IID_IContentControlStatics_UWP =
            { 0x116982a4, 0x5cf5, 0x4c05, { 0xa5, 0x3f, 0x15, 0xe4, 0x87, 0x6c, 0x85, 0xec } };
        static const GUID IID_ISymbolIconStatics_UWP =
            { 0x8624fbdf, 0x952c, 0x4f76, { 0xb0, 0x3f, 0x30, 0x83, 0x3b, 0xe9, 0x95, 0x72 } };
        static const GUID IID_IBindingOperationsStatics_UWP =
            { 0xe155ef73, 0x95a0, 0x4aab, { 0x8c, 0x7d, 0x2a, 0x47, 0xda, 0x07, 0x3c, 0x79 } };
        static const GUID IID_IThicknessHelperStatics_UWP =
            { 0xc0991a7c, 0x070c, 0x4da6, { 0x87, 0x84, 0x01, 0xca, 0x80, 0x0e, 0xb7, 0x3a } };
        static const GUID IID_IXamlBindingHelperStatics_UWP =
            { 0xf65cfb71, 0xc80c, 0x4ffa, { 0x86, 0xee, 0x55, 0x87, 0x54, 0xee, 0x33, 0x6d } };
        static const GUID IID_ILayoutInformationStatics_UWP =
            { 0xcf06cf99, 0x58e9, 0x4682, { 0x83, 0x26, 0x50, 0xca, 0xab, 0x65, 0xed, 0x7c } };
        static const GUID IID_IStoryboardStatics_UWP =
            { 0xd82f07d8, 0x73d5, 0x4379, { 0xbd, 0x48, 0x7e, 0x05, 0x18, 0x4a, 0x8b, 0xad } };
        if (IsEqualGUID( iid, &IID_IStoryboardStatics_UWP ))
        {
            *out = (void *)&sb_factory_obj.IStatics_iface;
            return S_OK;
        }
        if (IsEqualGUID( iid, &IID_ILayoutInformationStatics_UWP ))
        {
            *out = (void *)&layout_info_statics_obj;
            return S_OK;
        }
        if (IsEqualGUID( iid, &IID_IConnectedAnimationServiceStatics_UWP ))
        {
            *out = (void *)&cas_statics_obj;
            return S_OK;
        }
        if (IsEqualGUID( iid, &IID_IBindingOperationsStatics_UWP ) ||
            IsEqualGUID( iid, &IID_IXamlBindingHelperStatics_UWP ))
        {
            *out = (void *)&binding_ops_statics_obj;
            return S_OK;
        }
        if (IsEqualGUID( iid, &IID_IThicknessHelperStatics_UWP ))
        {
            *out = (void *)&thickness_helper_statics_obj;
            return S_OK;
        }
        if (IsEqualGUID( iid, &IID_IButtonBaseStatics_UWP ) ||
            IsEqualGUID( iid, &IID_IButtonBaseStatics_Calc ) ||
            IsEqualGUID( iid, &IID_IToggleButtonStatics_UWP ) ||
            IsEqualGUID( iid, &IID_IControlStatics_UWP ) ||
            IsEqualGUID( iid, &IID_IRangeBaseStatics_UWP ) ||
            IsEqualGUID( iid, &IID_IAutoSuggestBoxStatics_UWP ) ||
            IsEqualGUID( iid, &IID_ISplitViewStatics_UWP ) ||
            IsEqualGUID( iid, &IID_IContentControlStatics_UWP ) ||
            IsEqualGUID( iid, &IID_ISymbolIconStatics_UWP ) ||
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
    {
        static const GUID IID_IFrameworkElementAutomationPeerFactory_UWP =
            { 0x0db9b8bc, 0xb812, 0x48e3, { 0xaf, 0x1f, 0xdb, 0xc5, 0x76, 0x00, 0xc3, 0x25 } };
        if (IsEqualGUID( iid, &IID_IFrameworkElementAutomationPeerFactory_UWP ))
        {
            *out = f ? (void *)&f->IComposableFactory_iface : (void *)iface;
            return S_OK;
        }
    }
    if (IsEqualGUID( iid, &IID_IUIElementStatics_UWP ))
    {
        *out = (void *)&uwp_uielement_statics_obj;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IUIElementStatics4_UWP ) ||
        IsEqualGUID( iid, &IID_IUIElementStatics6_UWP ) ||
        IsEqualGUID( iid, &IID_IUIElementStatics7_UWP ) ||
        IsEqualGUID( iid, &IID_IUIElementStatics8_UWP ))
    {
        *out = (void *)&uwp_uielement_routed_statics_obj;
        return S_OK;
    }
    {
        static const GUID IID_IGridStatics_UWP3 =
            { 0x64fe2e9f, 0xf951, 0x42b6, { 0xa9, 0xce, 0xbb, 0x17, 0x9a, 0xf1, 0x15, 0x95 } };
        if (IsEqualGUID( iid, &IID_IGridStatics_UWP ) ||
            IsEqualGUID( iid, &IID_IGridStatics_UWP2 ) ||
            IsEqualGUID( iid, &IID_IGridStatics_UWP3 ))
        {
            *out = (void *)&uwp_grid_statics_obj;
            return S_OK;
        }
    }
    if (f == &auto_props_factory_obj ||
        f == &feap_factory_obj ||
        IsEqualGUID( iid, &IID_ICanvasStatics_local ) ||
        IsEqualGUID( iid, &IID_IScrollViewerStatics_local ) ||
        IsEqualGUID( iid, &IID_IVisualTreeHelperStatics_UWP ) ||
        IsEqualGUID( iid, &IID_IFrameworkElementStatics_UWP ))
    {
        *out = (void *)&f->IStatics_iface;
        return S_OK;
    }
    {
        static const GUID IID_IRangeBaseFactory_UWP =
            { 0x389b7c71, 0x5220, 0x42b2, { 0x99, 0x92, 0x26, 0x90, 0xc1, 0xa6, 0x70, 0x30 } };
        static const GUID IID_IMediaPlayerElementFactory_UWP =
            { 0x77e525c3, 0xeb17, 0x4b8d, { 0x88, 0x9d, 0x1e, 0xa8, 0xab, 0xdb, 0xd4, 0xef } };
        static const GUID IID_ISwapChainPanelFactory_UWP =
            { 0xf38f8d7f, 0x1a48, 0x49cb, { 0x86, 0xd2, 0x10, 0xea, 0xaa, 0xf6, 0xfd, 0x70 } };
        if (IsEqualGUID( iid, &IID_IRangeBaseFactory_UWP ) ||
            IsEqualGUID( iid, &IID_IMediaPlayerElementFactory_UWP ) ||
            IsEqualGUID( iid, &IID_ISwapChainPanelFactory_UWP ))
        {
            *out = f ? (void *)&f->IComposableFactory_iface : (void *)iface;
            return S_OK;
        }
    }
    {
        static const GUID IID_IBitmapImageFactory_UWP =
            { 0xc9132978, 0x4810, 0x4e5e, { 0x80, 0x87, 0x03, 0x67, 0x1e, 0xe6, 0x0d, 0x85 } };
        if (IsEqualGUID( iid, &IID_IBitmapImageFactory_UWP ) && f && f->IBitmapImageFactory_iface.lpVtbl)
        {
            *out = (void *)&f->IBitmapImageFactory_iface;
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
        IsEqualGUID( iid, &IID_IRoutedEventArgsFactory_WinUI3 ) ||
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
         wcsstr( f->class_name, L"Configuration" ) ||
         wcsstr( f->class_name, L"Collection" ) ||
         wcsstr( f->class_name, L"Shadow" )))
    {
        WARN( "uwp_cf_QI(%s) unknown iid %s -> ComposableFactory fallback\n",
              debugstr_w(f->class_name), debugstr_guid(iid) );
        *out = (void *)&f->IComposableFactory_iface;
        return S_OK;
    }
    {
        static const GUID IID_IXamlMarkupHelperStatics_UWP =
            { 0xc9bc3725, 0xf34f, 0x445c, { 0x81, 0xa2, 0x6b, 0x72, 0xa5, 0xe8, 0xf0, 0x72 } };
        static const GUID IID_ILoadedImageSurfaceStatics_UWP =
            { 0x22b8edf6, 0x84ad, 0x40ab, { 0x93, 0x7d, 0x48, 0x71, 0x61, 0x3e, 0x76, 0x5d } };
        extern const void *xaml_markup_helper_statics_obj;
        if (IsEqualGUID( iid, &IID_IXamlMarkupHelperStatics_UWP ))
        {
            *out = (void *)&xaml_markup_helper_statics_obj;
            return S_OK;
        }
        if (IsEqualGUID( iid, &IID_ILoadedImageSurfaceStatics_UWP ))
        {
            *out = (void *)&uwp_lis_statics_obj;
            return S_OK;
        }
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
     * and ISymbolIconFactory::CreateInstanceWithIcon(Symbol sym, ISymbolIcon **out)
     * take 2 args after iface, so the output pointer arrives in `inner`. */
    if (!wcscmp( cls, L"Windows.UI.Xaml.PropertyPath" ) ||
        !wcscmp( cls, L"Windows.UI.Xaml.Controls.SymbolIcon" ))
    {
        HRESULT hr;
        TRACE( "uwp_cf_CreateInstance(%s, arg1=%p, out=%p)\n", debugstr_w(cls), outer, inner );
        hr = uwp_element_create( cls, NULL, NULL, (void **)inner );
        if (SUCCEEDED(hr) && inner && *(void **)inner && !wcscmp( cls, L"Windows.UI.Xaml.PropertyPath" ) &&
            (ULONG_PTR)outer >= 0x10000)
        {
            struct uwp_element *pp_el = uwp_elem_from_any( *(void **)inner );
            UINT32 len = 0;
            const WCHAR *raw = WindowsGetStringRawBuffer( (HSTRING)outer, &len );
            if (pp_el && raw && len > 0)
            {
                UINT32 cpy = len < ARRAY_SIZE(pp_el->text) - 1 ? len : ARRAY_SIZE(pp_el->text) - 1;
                memcpy( pp_el->text, raw, cpy * sizeof(WCHAR) );
                pp_el->text[cpy] = 0;
            }
        }
        return hr;
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
    if (!out || (ULONG_PTR)out < 0x10000) return S_OK;
    return dependency_property_create( NULL, (IDependencyProperty **)out );
}

static HRESULT WINAPI uwp_cf_dp_prop_slot6( void *iface, void **out )
{
    struct uwp_class_factory *f = uwp_cf_from_any( iface );
    if (!out || (ULONG_PTR)out < 0x10000) return S_OK;
    if (f && f->class_name && !wcscmp( f->class_name, L"Windows.UI.Xaml.UIElement" ))
    {
        if (!g_routed_event_ptr_pressed)
            uwp_element_create( L"Windows.UI.Xaml.RoutedEvent", NULL, NULL, (void **)&g_routed_event_ptr_pressed );
        *out = g_routed_event_ptr_pressed;
        if (*out) IUnknown_AddRef( (IUnknown *)*out );
        return S_OK;
    }
    return dependency_property_create( NULL, (IDependencyProperty **)out );
}

static HRESULT WINAPI uwp_cf_routed_event( void *iface, void **out )
{
    if (!out || (ULONG_PTR)out < 0x10000) return S_OK;
    if (!g_routed_event_ptr_pressed)
        uwp_element_create( L"Windows.UI.Xaml.RoutedEvent", NULL, NULL, (void **)&g_routed_event_ptr_pressed );
    *out = g_routed_event_ptr_pressed;
    if (*out) IUnknown_AddRef( (IUnknown *)*out );
    return S_OK;
}

static const GUID IID_IUIElementStatics4_UWP =
    { 0x5d1157a0, 0x0588, 0x4b15, { 0x89, 0x5c, 0x76, 0x99, 0x5c, 0x45, 0x8d, 0x83 } };
static const GUID IID_IUIElementStatics6_UWP =
    { 0x62809849, 0x2689, 0x4266, { 0xba, 0x3b, 0x3c, 0x5e, 0x86, 0xc3, 0xa8, 0x26 } };
static const GUID IID_IUIElementStatics7_UWP =
    { 0x933e9348, 0xb1e5, 0x4005, { 0x8d, 0x3a, 0x29, 0xba, 0xb9, 0xe7, 0xb9, 0x73 } };
static const GUID IID_IUIElementStatics8_UWP =
    { 0xa6890878, 0x664e, 0x4679, { 0x9c, 0x6b, 0x96, 0x67, 0x25, 0x16, 0x2f, 0x59 } };

static const void *uwp_uielement_routed_statics_vtbl[32] =
{
    [0] = uwp_cf_QI,
    [1] = uwp_cf_AddRef,
    [2] = uwp_cf_Release,
    [3] = uwp_cf_GetIids,
    [4] = uwp_cf_GetRuntimeClassName,
    [5] = uwp_cf_GetTrustLevel,
    [6 ... 31] = uwp_cf_routed_event,
};
static const void *uwp_uielement_routed_statics_obj = uwp_uielement_routed_statics_vtbl;

static HRESULT WINAPI uwp_grid_statics_SetProp( void *iface, void *elem, INT32 val )
{
    return S_OK;
}
static HRESULT WINAPI uwp_grid_statics_GetProp( void *iface, void *elem, INT32 *out )
{
    if (out) *out = 0;
    return S_OK;
}
static const void *uwp_grid_statics_vtbl[24] =
{
    [0] = uwp_cf_QI,
    [1] = uwp_cf_AddRef,
    [2] = uwp_cf_Release,
    [3] = uwp_cf_GetIids,
    [4] = uwp_cf_GetRuntimeClassName,
    [5] = uwp_cf_GetTrustLevel,
    [6] = uwp_cf_dp_prop,             /* RowProperty */
    [7] = uwp_grid_statics_GetProp,   /* GetRow */
    [8] = uwp_grid_statics_SetProp,   /* SetRow */
    [9] = uwp_cf_dp_prop,             /* ColumnProperty */
    [10] = uwp_grid_statics_GetProp,  /* GetColumn */
    [11] = uwp_grid_statics_SetProp,  /* SetColumn */
    [12] = uwp_cf_dp_prop,            /* RowSpanProperty */
    [13] = uwp_grid_statics_GetProp,  /* GetRowSpan */
    [14] = uwp_grid_statics_SetProp,  /* SetRowSpan */
    [15] = uwp_cf_dp_prop,            /* ColumnSpanProperty */
    [16] = uwp_grid_statics_GetProp,  /* GetColumnSpan */
    [17] = uwp_grid_statics_SetProp,  /* SetColumnSpan */
    [18 ... 23] = uwp_generic_noop,
};
static const void *uwp_grid_statics_obj = uwp_grid_statics_vtbl;

static const void *uwp_uielement_statics_vtbl[64] =
{
    [0] = uwp_cf_QI,
    [1] = uwp_cf_AddRef,
    [2] = uwp_cf_Release,
    [3] = uwp_cf_GetIids,
    [4] = uwp_cf_GetRuntimeClassName,
    [5] = uwp_cf_GetTrustLevel,
    [6 ... 22] = uwp_cf_routed_event,
    [23 ... 24] = uwp_cf_routed_event,
    [25 ... 63] = uwp_cf_dp_prop,
};
static const void *uwp_uielement_statics_obj = uwp_uielement_statics_vtbl;

static HRESULT WINAPI uwp_cf_dp_prop_slot9( void *iface, void **out )
{
    struct uwp_class_factory *f = uwp_cf_from_any( iface );
    if (f && f->class_name && !wcscmp( f->class_name, L"Windows.UI.Xaml.UIElement" ))
    {
        if (!out) return E_POINTER;
        if (!g_routed_event_ptr_pressed)
            uwp_element_create( L"Windows.UI.Xaml.RoutedEvent", NULL, NULL, (void **)&g_routed_event_ptr_pressed );
        *out = g_routed_event_ptr_pressed;
        if (*out) IUnknown_AddRef( (IUnknown *)*out );
        return S_OK;
    }
    if (!out || (ULONG_PTR)out < 0x10000) return S_OK;
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

static HRESULT WINAPI comp_target_GetCompositorForCurrentThread( void *iface, void **out )
{
    __x_ABI_CWindows_CUI_CComposition_CICompositor *comp;
    if (!out) return E_POINTER;
    *out = NULL;
    comp = ecp_get_compositor();
    ERR( "comp_target_GetCompositorForCurrentThread -> %p\n", comp );
    if (!comp) return E_FAIL;
    __x_ABI_CWindows_CUI_CComposition_CICompositor_AddRef( comp );
    *out = comp;
    return S_OK;
}

static HRESULT WINAPI comp_target_QI( void *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IMarshal ) ||
        IsEqualGUID( iid, &IID_IReferenceTracker_UWP ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget_UWP ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget_UWP2 ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    ERR( "comp_target_QI iid=%s\n", debugstr_guid( iid ) );
    *out = (void *)&comp_target_statics_obj;
    return S_OK;
}

static const GUID IID_IRenderingEventArgs_UWP =
    { 0x58b55d6d, 0x50a6, 0x435e, { 0x96, 0x9c, 0xf0, 0xe8, 0x18, 0xa9, 0x6e, 0x35 } };

static HRESULT WINAPI rendering_args_QI( void *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IRenderingEventArgs_UWP ))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static HRESULT WINAPI rendering_args_get_RenderingTime( void *iface, INT64 *val )
{
    if (val) *val = (INT64)GetTickCount64() * 10000;
    return S_OK;
}
static const void *rendering_args_vtbl[] =
{
    rendering_args_QI, ecp_AddRef, ecp_Release, ecp_GetIids, ecp_GetRuntimeClassName, ecp_GetTrustLevel,
    rendering_args_get_RenderingTime,
};
static const void *rendering_args_obj = rendering_args_vtbl;

struct ct_rendering_entry { IInspectable *handler; INT64 token; };
static struct ct_rendering_entry *g_ct_rendering = NULL;
static int g_ct_rendering_cnt = 0;
static int g_ct_rendering_cap = 0;
static LONG g_ct_rendering_next_tok = 1;
static BOOL g_ct_in_rendering = FALSE;
static SRWLOCK g_ct_rendering_lock = SRWLOCK_INIT;

static HRESULT WINAPI comp_target_add_Rendering( void *iface, void *handler, EventRegistrationToken *tok )
{
    INT64 t = InterlockedIncrement( &g_ct_rendering_next_tok );
    if (tok && (ULONG_PTR)tok > 0x10000 && !IsBadWritePtr( tok, sizeof(*tok) ))
        tok->value = t;
    if (handler)
    {
        AcquireSRWLockExclusive( &g_ct_rendering_lock );
        if (xaml_vec_grow( (void **)&g_ct_rendering, &g_ct_rendering_cap,
                           g_ct_rendering_cnt + 1, sizeof(*g_ct_rendering), 16 ))
        {
            TRACE( "CompositionTarget::add_Rendering handler=%p cnt=%d\n", handler, g_ct_rendering_cnt + 1 );
            g_ct_rendering[g_ct_rendering_cnt].handler = (IInspectable *)handler;
            g_ct_rendering[g_ct_rendering_cnt].token = t;
            g_ct_rendering_cnt++;
            IInspectable_AddRef( (IInspectable *)handler );
            ReleaseSRWLockExclusive( &g_ct_rendering_lock );
            uwp_invalidate_window();
            return S_OK;
        }
        ReleaseSRWLockExclusive( &g_ct_rendering_lock );
    }
    return S_OK;
}

static HRESULT WINAPI comp_target_remove_Rendering( void *iface, EventRegistrationToken tok )
{
    int i;
    IInspectable *h = NULL;
    AcquireSRWLockExclusive( &g_ct_rendering_lock );
    for (i = 0; i < g_ct_rendering_cnt; i++)
    {
        if (g_ct_rendering[i].token == tok.value)
        {
            h = g_ct_rendering[i].handler;
            g_ct_rendering[i] = g_ct_rendering[--g_ct_rendering_cnt];
            break;
        }
    }
    ReleaseSRWLockExclusive( &g_ct_rendering_lock );
    if (h) IInspectable_Release( h );
    return S_OK;
}

void uwp_fire_composition_target_rendering( void )
{
    IInspectable **snap = NULL;
    int n = 0, i;
    if (g_ct_in_rendering) return;
    AcquireSRWLockShared( &g_ct_rendering_lock );
    if (g_ct_rendering_cnt <= 0)
    {
        ReleaseSRWLockShared( &g_ct_rendering_lock );
        return;
    }
    snap = malloc( (size_t)g_ct_rendering_cnt * sizeof(*snap) );
    if (!snap)
    {
        ReleaseSRWLockShared( &g_ct_rendering_lock );
        return;
    }
    for (i = 0; i < g_ct_rendering_cnt; i++)
    {
        if (g_ct_rendering[i].handler)
        {
            snap[n++] = g_ct_rendering[i].handler;
            IInspectable_AddRef( g_ct_rendering[i].handler );
        }
    }
    ReleaseSRWLockShared( &g_ct_rendering_lock );
    g_ct_in_rendering = TRUE;
    for (i = 0; i < n; i++)
    {
        struct xaml_event_handler *h = (struct xaml_event_handler *)snap[i];
        __TRY
        {
            h->lpVtbl->Invoke( h, NULL, (IInspectable *)&rendering_args_obj );
        }
        __EXCEPT_ALL
        {
        }
        __ENDTRY;
        IInspectable_Release( snap[i] );
    }
    free( snap );
    g_ct_in_rendering = FALSE;
}

static const void *comp_target_statics_vtbl[16] =
{
    [0] = comp_target_QI,
    [1] = ecp_AddRef,
    [2] = ecp_Release,
    [3] = ecp_GetIids,
    [4] = ecp_GetRuntimeClassName,
    [5] = ecp_GetTrustLevel,
    [6] = comp_target_add_Rendering,
    [7] = comp_target_remove_Rendering,
    [8] = uwp_ui_event_add,
    [9] = uwp_generic_noop,
    [10] = uwp_ui_event_add,
    [11] = uwp_generic_noop,
    [12] = comp_target_GetCompositorForCurrentThread,
    [13 ... 15] = uwp_generic_noop,
};
static const void *comp_target_statics_obj = comp_target_statics_vtbl;

IActivationFactory *xaml_get_comp_target_factory( void )
{
    return (IActivationFactory *)&comp_target_statics_obj;
}

/* Microsoft.UI.Content.ContentExternalOutputLink bridge -> PlacementVisual + IDCompositionTarget */
struct xaml_ceol
{
    const void *link_vtbl;
    const void *target_vtbl;
    LONG ref;
    __x_ABI_CWindows_CUI_CComposition_CIContainerVisual *placement_visual;
    IDCompositionVisual *placement_dcomp_vis;
};

static inline struct xaml_ceol *ceol_from_link( void *iface )
{
    return CONTAINING_RECORD( iface, struct xaml_ceol, link_vtbl );
}
static inline struct xaml_ceol *ceol_from_target( void *iface )
{
    return CONTAINING_RECORD( iface, struct xaml_ceol, target_vtbl );
}

static HRESULT WINAPI ceol_QueryInterface( void *iface, REFIID iid, void **out )
{
    static const GUID IID_IDCompositionTarget_local =
        { 0xeacdd04c, 0x117e, 0x4e17, { 0x88, 0xf4, 0xd1, 0xb1, 0x2b, 0x0e, 0x3d, 0x89 } };
    static const GUID IID_IDCompositionVisual_local =
        { 0x4d93059d, 0x097b, 0x4651, { 0x9a, 0x60, 0xf0, 0xf2, 0x51, 0x16, 0xe2, 0xf3 } };
    static const GUID IID_IDCompositionVisual2_local =
        { 0xe8de1639, 0x4331, 0x4b26, { 0xbc, 0x5f, 0x6a, 0x32, 0x1d, 0x34, 0x7a, 0x85 } };
    static const GUID IID_IContentExternalOutputLink_WinUI3 =
        { 0x1054bf83, 0xb35b, 0x5fde, { 0x8d, 0xd7, 0xac, 0x3b, 0xb3, 0xe6, 0xce, 0x27 } };
    struct xaml_ceol *impl = ceol_from_link( iface );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IContentExternalOutputLink_WinUI3 ))
    {
        InterlockedIncrement( &impl->ref );
        *out = &impl->link_vtbl;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IDCompositionTarget_local ))
    {
        InterlockedIncrement( &impl->ref );
        *out = &impl->target_vtbl;
        ERR( "ceol_QueryInterface -> IDCompositionTarget %p (dcomp_vis=%p)\n", *out, impl->placement_dcomp_vis );
        return S_OK;
    }
    if ((IsEqualGUID( iid, &IID_IDCompositionVisual_local ) ||
         IsEqualGUID( iid, &IID_IDCompositionVisual2_local )) && impl->placement_dcomp_vis)
    {
        HRESULT hr = IDCompositionVisual_QueryInterface( impl->placement_dcomp_vis, iid, out );
        ERR( "ceol_QueryInterface -> IDCompositionVisual %p hr=%#lx\n", *out, hr );
        return hr;
    }
    ERR( "ceol_QueryInterface unsupported iid %s\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI ceol_AddRef( void *iface )
{
    return InterlockedIncrement( &ceol_from_link( iface )->ref );
}
static ULONG WINAPI ceol_Release( void *iface )
{
    struct xaml_ceol *impl = ceol_from_link( iface );
    ULONG r = InterlockedDecrement( &impl->ref );
    if (!r)
    {
        if (impl->placement_dcomp_vis) IDCompositionVisual_Release( impl->placement_dcomp_vis );
        if (impl->placement_visual) __x_ABI_CWindows_CUI_CComposition_CIContainerVisual_Release( impl->placement_visual );
        free( impl );
    }
    return r;
}
static HRESULT WINAPI ceol_get_PlacementVisual( void *iface, void **out )
{
    struct xaml_ceol *impl = ceol_from_link( iface );
    if (!out) return E_POINTER;
    *out = impl->placement_visual;
    if (impl->placement_visual)
        __x_ABI_CWindows_CUI_CComposition_CIContainerVisual_AddRef( impl->placement_visual );
    ERR( "ceol_get_PlacementVisual -> %p\n", *out );
    return S_OK;
}
static HRESULT WINAPI ceol_target_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct xaml_ceol *impl = ceol_from_target( iface );
    return ceol_QueryInterface( &impl->link_vtbl, iid, out );
}
static ULONG WINAPI ceol_target_AddRef( void *iface )
{
    return InterlockedIncrement( &ceol_from_target( iface )->ref );
}
static ULONG WINAPI ceol_target_Release( void *iface )
{
    struct xaml_ceol *impl = ceol_from_target( iface );
    return ceol_Release( &impl->link_vtbl );
}
static HRESULT WINAPI ceol_target_SetRoot( void *iface, IDCompositionVisual *visual )
{
    struct xaml_ceol *impl = ceol_from_target( iface );
    HRESULT hr = S_OK;
    ERR( "ceol_target_SetRoot(impl=%p, visual=%p, placement_dcomp_vis=%p)\n", impl, visual, impl->placement_dcomp_vis );
    if (impl->placement_dcomp_vis)
    {
        IDCompositionVisual_RemoveAllVisuals( impl->placement_dcomp_vis );
        if (visual)
            hr = IDCompositionVisual_AddVisual( impl->placement_dcomp_vis, visual, TRUE, NULL );
        xaml_dcomp_commit();
    }
    return hr;
}

static const void *ceol_link_vtbl[24] =
{
    [0] = ceol_QueryInterface,
    [1] = ceol_AddRef,
    [2] = ceol_Release,
    [3] = ecp_GetIids,
    [4] = ecp_GetRuntimeClassName,
    [5] = ecp_GetTrustLevel,
    [6 ... 12] = uwp_generic_noop,
    [13] = ceol_get_PlacementVisual,
    [14 ... 23] = uwp_generic_noop,
};
static const void *ceol_target_vtbl[4] =
{
    [0] = ceol_target_QueryInterface,
    [1] = ceol_target_AddRef,
    [2] = ceol_target_Release,
    [3] = ceol_target_SetRoot,
};

static HRESULT WINAPI ceol_statics_Create( void *iface, void *compositor_arg, void **out )
{
    static const GUID IID_IDCompositionVisual_local =
        { 0x4d93059d, 0x097b, 0x4651, { 0x9a, 0x60, 0xf0, 0xf2, 0x51, 0x16, 0xe2, 0xf3 } };
    __x_ABI_CWindows_CUI_CComposition_CICompositor *comp = ecp_get_compositor();
    struct xaml_ceol *impl;
    HRESULT hr;
    if (!out) return E_POINTER;
    *out = NULL;
    if (!comp) return E_FAIL;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->link_vtbl = ceol_link_vtbl;
    impl->target_vtbl = ceol_target_vtbl;
    impl->ref = 1;
    hr = __x_ABI_CWindows_CUI_CComposition_CICompositor_CreateContainerVisual( comp, &impl->placement_visual );
    if (FAILED(hr) || !impl->placement_visual)
    {
        free( impl );
        return FAILED(hr) ? hr : E_FAIL;
    }
    __x_ABI_CWindows_CUI_CComposition_CIContainerVisual_QueryInterface(
        impl->placement_visual, &IID_IDCompositionVisual_local, (void **)&impl->placement_dcomp_vis );
    ERR( "ceol_statics_Create -> %p (placement_visual=%p, dcomp_vis=%p)\n",
         impl, impl->placement_visual, impl->placement_dcomp_vis );
    *out = &impl->link_vtbl;
    return S_OK;
}

static const void *ceol_statics_obj;
static HRESULT WINAPI ceol_statics_QI( void *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IMarshal ) ||
        IsEqualGUID( iid, &IID_IReferenceTracker_UWP ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget_UWP ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget_UWP2 ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    *out = (void *)&ceol_statics_obj;
    return S_OK;
}
static const void *ceol_statics_vtbl[8] =
{
    [0] = ceol_statics_QI,
    [1] = ecp_AddRef,
    [2] = ecp_Release,
    [3] = ecp_GetIids,
    [4] = ecp_GetRuntimeClassName,
    [5] = ecp_GetTrustLevel,
    [6] = ceol_statics_Create,
    [7] = uwp_generic_noop,
};
static const void *ceol_statics_obj = ceol_statics_vtbl;

IActivationFactory *xaml_get_ceol_factory( void )
{
    return (IActivationFactory *)&ceol_statics_obj;
}

static HRESULT WINAPI binding_ops_SetBinding( void *iface, void *target, void *dp, void *binding )
{
    struct uwp_element *tel = uwp_elem_from_any( target );
    struct uwp_element *bel = uwp_elem_from_any( binding );
    TRACE( "binding_ops_SetBinding(target=%p, dp=%p, binding=%p)\n", target, dp, binding );
    uwp_apply_binding( tel, dp, bel );
    return S_OK;
}
static const void *binding_ops_statics_vtbl[8] =
{
    [0] = uwp_cf_QI,
    [1] = uwp_cf_AddRef,
    [2] = uwp_cf_Release,
    [3] = uwp_cf_GetIids,
    [4] = uwp_cf_GetRuntimeClassName,
    [5] = uwp_cf_GetTrustLevel,
    [6] = binding_ops_SetBinding,
    [7] = uwp_generic_noop,
};
static const void *binding_ops_statics_obj = binding_ops_statics_vtbl;

static HRESULT WINAPI thickness_helper_FromLengths( void *iface, double left, double top, double right, double bottom, double *out )
{
    if (!out) return E_POINTER;
    out[0] = left;
    out[1] = top;
    out[2] = right;
    out[3] = bottom;
    return S_OK;
}
static HRESULT WINAPI thickness_helper_FromUniformLength( void *iface, double uniform, double *out )
{
    if (!out) return E_POINTER;
    out[0] = out[1] = out[2] = out[3] = uniform;
    return S_OK;
}
static const void *thickness_helper_statics_vtbl[8] =
{
    [0] = uwp_cf_QI,
    [1] = uwp_cf_AddRef,
    [2] = uwp_cf_Release,
    [3] = uwp_cf_GetIids,
    [4] = uwp_cf_GetRuntimeClassName,
    [5] = uwp_cf_GetTrustLevel,
    [6] = thickness_helper_FromLengths,
    [7] = thickness_helper_FromUniformLength,
};
static const void *thickness_helper_statics_obj = thickness_helper_statics_vtbl;

static HRESULT WINAPI xaml_markup_helper_UnloadObject( void *iface, void *element )
{
    struct uwp_element *el = uwp_elem_from_any( element );
    if (el) el->visibility = 1; /* Collapsed */
    return S_OK;
}
static const void *xaml_markup_helper_statics_vtbl[16] =
{
    [0] = uwp_cf_QI,
    [1] = uwp_cf_AddRef,
    [2] = uwp_cf_Release,
    [3] = uwp_cf_GetIids,
    [4] = uwp_cf_GetRuntimeClassName,
    [5] = uwp_cf_GetTrustLevel,
    [6] = xaml_markup_helper_UnloadObject,
    [7 ... 15] = uwp_generic_noop,
};
const void *xaml_markup_helper_statics_obj = xaml_markup_helper_statics_vtbl;

const void *uwp_cf_statics_vtbl[64] =
{
    [0] = uwp_cf_QI,
    [1] = uwp_cf_AddRef,
    [2] = uwp_cf_Release,
    [3] = uwp_cf_GetIids,
    [4] = uwp_cf_GetRuntimeClassName,
    [5] = uwp_cf_GetTrustLevel,
    [6] = uwp_cf_dp_prop_slot6,
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
    [6] = uwp_cf_dp_prop_slot6,
    [7 ... 31] = uwp_cf_dp_prop,
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

