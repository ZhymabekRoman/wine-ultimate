/* WinRT Windows.UI.Xaml XamlIsland Host, DispatcherTimer & IDispatch Bridge
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
#include "uwp_private.h"
WINE_DEFAULT_DEBUG_CHANNEL(xaml);

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
    if (!el || depth > 48 || *rem <= 0) return;
    if (el->class_flags2 & (UWP_CF2_HOMEPAGE_SKEL | UWP_CF2_SEARCHPAGE_SKEL | UWP_CF2_SHIMMER_PRODUCT))
    {
        TRACE( "%*s%s (skipped)\n", depth * 2, "", debugstr_w(el->class_name) );
        return;
    }
    if (el->children_coll)
        kids = (int)CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface )->count;
    else if (el->inlines_coll)
        kids = (int)CONTAINING_RECORD( el->inlines_coll, struct uwp_vec_view, IInspectable_iface )->count;
    (*rem)--;
    TRACE( "%*s%s (%s) rc=(%ld,%ld,%ld,%ld) hb=%d vis=%d op=%.2f kids=%d w=%.0f h=%.0f txt=%s img=%s\n",
         depth * 2, "", debugstr_w(el->class_name), debugstr_w(el->name),
         el->bounds.left, el->bounds.top, el->bounds.right, el->bounds.bottom,
         el->has_bounds, el->visibility, el->opacity, kids,
         el->width, el->height,
         uwp_has_text( el ) ? debugstr_w(uwp_elem_text( el )) : "\"\"",
         uwp_elem_image_src( el )[0] ? debugstr_w(uwp_elem_image_src( el )) : "\"\"" );
    if (el->class_flags2 & UWP_CF2_DOT_CARD)
        return;
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
    int rem = 2500;
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
                        if (g_layout_dirty || g_paint_dirty || !st->valid || w != st->w || h != st->h)
                        {
                            gpu_ok = xaml_layout_render_gpu( hwnd, w, h, impl->content,
                                                             g_island_d3d11_dev, g_island_d3d11_ctx, st->swapchain );
                            if (gpu_ok) { st->w = w; st->h = h; }
                        }
                        else
                        {
                            /* Swapchain front buffer is unchanged: skip redundant GPU render. */
                            gpu_ok = TRUE;
                        }
                    }
                    else if (g_layout_dirty || g_paint_dirty || !st->valid || w != st->w || h != st->h)
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
                    if (st && (g_layout_dirty || g_paint_dirty || !st->valid || w != st->w || h != st->h))
                    {
                        if (uwp_island_ensure_dib( st, hdc, w, h ))
                        {
                            g_layout_dirty = FALSE;
                            g_paint_dirty = FALSE;
                            xaml_layout_render( hwnd, st->dc, w, h, impl->content );
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
            g_paint_dirty = TRUE;
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
        struct uwp_element *root_el = uwp_elem_from_any( impl->content );
        RECT rc;
        if (root_el && GetClientRect( hwnd, &rc ) && rc.right > rc.left && rc.bottom > rc.top &&
            (g_layout_dirty || !root_el->has_bounds))
        {
            g_layout_dirty = FALSE;
            uwp_arrange_element( root_el, rc );
            g_paint_dirty = TRUE;
            InvalidateRect( hwnd, NULL, FALSE );
        }
        uwp_debug_dump_tree( root_el, 0, &rem );
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
    impl->in_visual_tree = TRUE;
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
        if (impl->island_hwnd &&
            (msg->hwnd == impl->island_hwnd || IsChild( impl->island_hwnd, msg->hwnd )))
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
    count = impl->events ? xaml_event_registry_snapshot_alloc( &impl->events->timer_tick_reg, &handlers ) : 0;
    TRACE( "uwp_dispatcher_timer_proc id=%Iu impl=%p handlers=%u\n", id, impl, count );
    for (i = 0; i < count; i++)
    {
        struct xaml_event_handler *h = (struct xaml_event_handler *)handlers[i];
        IInspectable *sender = impl->outer ? impl->outer : &impl->IInspectable_iface;
        __TRY
        {
            h->lpVtbl->Invoke( h, sender, NULL );
        }
        __EXCEPT_ALL { WARN( "%s: exception 0x%08lx\n", __func__, (unsigned long)GetExceptionCode() ); }
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
    struct uwp_event_bag *ev;
    if (!impl) return E_NOINTERFACE;
    ev = uwp_elem_ensure_events( impl );
    if (!ev) return E_OUTOFMEMORY;
    TRACE( "uwp_timer_add_Tick impl=%p handler=%p\n", impl, handler );
    return xaml_event_registry_add( &ev->timer_tick_reg, handler, tok );
}
static HRESULT WINAPI uwp_timer_remove_Tick( void *iface, EventRegistrationToken tok )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return E_NOINTERFACE;
    if (!impl->events) return S_OK;
    return xaml_event_registry_remove( &impl->events->timer_tick_reg, tok );
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
static ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface ) { return uwp_sub_AddRef( (IInspectable *)iface ); }
static ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface ) { return uwp_sub_Release( (IInspectable *)iface ); }
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

BOOL uwp_dispatch_keydown( HWND hwnd, UINT vk, LPARAM lparam, void *island_root_ptr )
{
    struct uwp_element *island_root = island_root_ptr ? uwp_elem_from_any( island_root_ptr ) : NULL;
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
    if (g_focused_textbox && !(mods & 3))
        return FALSE;
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
