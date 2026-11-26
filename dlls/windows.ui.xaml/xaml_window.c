/* WinRT Windows.UI.Xaml.Window Implementation
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

#include <stdlib.h>
#include <string.h>

#include "private.h"
#include "app_private.h"
#include "uwp_abi_vtbls.h"
#include "weakreference.h"
#include "wine/exception.h"

WINE_DEFAULT_DEBUG_CHANNEL(xaml);

void uwp_sync_calc_display_from_vm( void );
void uwp_keyboard_press_char( WCHAR ch );
BOOL uwp_dispatch_keydown( HWND hwnd, UINT vk, LPARAM lparam, void *island_root_ptr );

/* WinUI 3 / UWP Window interface GUIDs */
const GUID IID_IWindowStatics_WinUI =
    { 0x8cc985e3, 0xa41a, 0x5df4, { 0xb5, 0x31, 0xd3, 0xa1, 0x78, 0x8d, 0x86, 0xc5 } };
const GUID IID_IWindowFactory_WinUI =
    { 0xf0441536, 0xafef, 0x5222, { 0x91, 0x8f, 0x32, 0x4a, 0x9b, 0x2d, 0xec, 0x75 } };
static const GUID IID_IWindow_WinUI =
    { 0x61f0ec79, 0x5d52, 0x56b5, { 0x86, 0xfb, 0x40, 0xfa, 0x4a, 0xf2, 0x88, 0xb0 } };
static const GUID IID_IWindowNative =
    { 0xeecdbf0e, 0xbae9, 0x4cb6, { 0xa6, 0x8e, 0x95, 0x98, 0xe1, 0xcb, 0x57, 0xbb } };
static const GUID IID_IWindow2 =
    { 0x74b861a1, 0x7487, 0x46a9, { 0x9a, 0x6e, 0xc7, 0x8b, 0x51, 0x27, 0x26, 0xc5 } };
static const GUID IID_IWindow2_WinUI =
    { 0x42febaa5, 0x1c32, 0x522a, { 0xa5, 0x91, 0x57, 0x61, 0x8c, 0x6f, 0x66, 0x5d } };
const GUID IID_IWindow_Mcg =
    { 0xb775ad7c, 0x18b8, 0x45ca, { 0xa1, 0xb0, 0xdc, 0x48, 0x3e, 0x4b, 0x10, 0x28 } };

static IInspectable *global_window_outer = NULL;
static WCHAR xaml_explicit_title[128];

/* Windows.UI.Xaml.Window implementation */
const GUID IID_IWindowStatics =
    { 0x93bbe361, 0xbe5a, 0x4ee3, { 0xb4, 0xa3, 0x95, 0x11, 0x8d, 0xc9, 0x7a, 0x89 } };
const GUID IID_IWindowStatics2 =
    { 0x93328409, 0x4ea1, 0x4afa, { 0x83, 0xdc, 0x0c, 0x4e, 0x73, 0xe8, 0x8b, 0xb1 } };
static const GUID IID_IWindow =
    { 0x3276167d, 0xc9f6, 0x462d, { 0x9d, 0xe2, 0xae, 0x4c, 0x1f, 0xd8, 0xc2, 0xe5 } };
static const GUID IID_IWindow_Alt =
    { 0x3962666a, 0x3605, 0x49be, { 0x88, 0x54, 0x44, 0x2a, 0x8b, 0x8c, 0xcd, 0xec } };

static inline struct xaml_window *impl_from_IWindow2( void *iface )
{
    return CONTAINING_RECORD( iface, struct xaml_window, IWindow2_iface );
}

ICoreWindow *get_current_core_window(void)
{
    IActivationFactory *cwf = NULL;
    __x_ABI_CWindows_CUI_CCore_CICoreWindowStatic *cws = NULL;
    __x_ABI_CWindows_CUI_CCore_CICoreWindow *cw = NULL;
    HSTRING class_id = NULL;

    WindowsCreateString( RuntimeClass_Windows_UI_Core_CoreWindow,
                         wcslen( RuntimeClass_Windows_UI_Core_CoreWindow ), &class_id );
    if (SUCCEEDED(RoGetActivationFactory( class_id, &IID_IActivationFactory, (void **)&cwf )) && cwf)
    {
        IActivationFactory_QueryInterface( cwf, &IID___x_ABI_CWindows_CUI_CCore_CICoreWindowStatic,
                                           (void **)&cws );
        if (cws)
        {
            __x_ABI_CWindows_CUI_CCore_CICoreWindowStatic_GetForCurrentThread( cws, &cw );
            __x_ABI_CWindows_CUI_CCore_CICoreWindowStatic_Release( cws );
        }
        IActivationFactory_Release( cwf );
    }
    WindowsDeleteString( class_id );
    return cw;
}

/* Window.Current.Activate() needs a CoreWindow on the UI thread to show. The WinRT
 * CoreWindow factory hands out a window that already exists and answers NULL when the
 * thread has none (CoreWindow.GetForCurrentThread); on-demand creation is env-gated in
 * windows.ui. When the app never made one, create it through windows.ui's CoreWindow_Create
 * export: it makes the top-level WS_OVERLAPPEDWINDOW | WS_VISIBLE window whose HWND the X11
 * driver maps. */
static ICoreWindow *create_current_core_window( void )
{
    ICoreWindow *(WINAPI *pCoreWindow_Create)( void );
    HMODULE module;

    module = GetModuleHandleW( L"windows.ui.dll" );
    if (!module) module = LoadLibraryW( L"windows.ui.dll" );
    if (!module) return NULL;
    if (!(pCoreWindow_Create = (void *)GetProcAddress( module, "CoreWindow_Create" ))) return NULL;
    return pCoreWindow_Create();
}

/* Resolve the thread CoreWindow, creating a top-level one when the thread has none. */
ICoreWindow *ensure_core_window( struct xaml_window *impl )
{
    impl = &global_xaml_window;
    if (!impl->core_window)
    {
        ICoreWindow *cw = get_current_core_window();
        if (!cw) cw = create_current_core_window();
        if (cw)
        {
            impl->core_window = wrap_core_window( cw );
            if (impl->core_window != cw) ICoreWindow_Release( cw );
        }
    }
    return impl->core_window;
}

static HRESULT WINAPI xaml_window_QueryInterface( IInspectable *iface, REFIID iid, void **out )
{
    struct xaml_window *impl = &global_xaml_window;

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IWindow ) ||
        IsEqualGUID( iid, &IID_IWindow_Alt ) ||
        IsEqualGUID( iid, &IID_IWindow_Mcg ))
    {
        *out = &impl->IInspectable_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IWindow2 ))
    {
        *out = &impl->IWindow2_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }

    {
        static const GUID IID_IWindow3_UWP =
            { 0xb70bdc9d, 0x1c35, 0x462a, { 0x9b, 0x97, 0x80, 0x8d, 0x5a, 0xf9, 0xf2, 0x8e } };
        if (IsEqualGUID( iid, &IID_IWindow3_UWP ))
        {
            *out = &impl->IWindow3_iface;
            IInspectable_AddRef( (IInspectable *)*out );
            return S_OK;
        }
    }

    {
        static const GUID IID_IWindowPrivate =
            { 0x06636c29, 0x5a17, 0x458d, { 0x8e, 0xa2, 0x24, 0x22, 0xd9, 0x97, 0xa9, 0x22 } };
        if (IsEqualGUID( iid, &IID_IWindowPrivate ))
        {
            *out = &impl->IWindowPrivate_iface;
            IInspectable_AddRef( (IInspectable *)*out );
            return S_OK;
        }
    }

    /* Windows.UI.Xaml.Window v2 (IWindow2: {d384759f-...}, slot 6 = SetTitleBar) */
    {
        static const GUID IID_IWindow_v2 =
            { 0xd384759f, 0x34f6, 0x4482, { 0x84, 0x35, 0xf5, 0x52, 0xf9, 0xb2, 0x4c, 0xc8 } };
        if (IsEqualGUID( iid, &IID_IWindow_v2 ))
        {
            *out = &impl->uwp_IWindow2_iface;
            IInspectable_AddRef( (IInspectable *)*out );
            return S_OK;
        }
    }

    if (IsEqualGUID( iid, &IID_IWeakReferenceSource_local ))
    {
        *out = &impl->IWeakReferenceSource_iface;
        IInspectable_AddRef( &impl->IInspectable_iface );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IWindow_WinUI ))
    {
        *out = &impl->ms_IWindow_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IWindow2_WinUI ))
    {
        *out = &impl->ms_IWindow2_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IWindowNative ))
    {
        *out = &impl->IWindowNative_iface;
        IUnknown_AddRef( (IUnknown *)*out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI xaml_window_AddRef( IInspectable *iface )
{
    struct xaml_window *impl = &global_xaml_window;
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p, ref %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI xaml_window_Release( IInspectable *iface )
{
    struct xaml_window *impl = &global_xaml_window;
    ULONG ref = InterlockedDecrement( &impl->ref );
    TRACE( "iface %p, ref %lu.\n", iface, ref );
    return ref;
}

static HRESULT WINAPI xaml_window_GetIids( IInspectable *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI xaml_window_GetRuntimeClassName( IInspectable *iface, HSTRING *name )
{
    const WCHAR *cls = xaml_get_app_caps()->winui3 ? L"Microsoft.UI.Xaml.Window" : L"Windows.UI.Xaml.Window";
    TRACE( "xaml_window_GetRuntimeClassName on %p -> %s\n", iface, debugstr_w(cls) );
    return WindowsCreateString( cls, wcslen( cls ), name );
}

static HRESULT WINAPI xaml_window_GetTrustLevel( IInspectable *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI xaml_window_get_Bounds( IInspectable *iface, void *a2, void *a3 )
{
    struct xaml_window *impl = &global_xaml_window;
    Rect *value = (Rect *)a2;
    ICoreWindow *cw = NULL;

    if (!value) value = (Rect *)a3;
    TRACE( "iface %p, value %p\n", iface, value );
    if (!impl->core_window) impl->core_window = get_current_core_window();
    cw = impl->core_window;
    if (cw && value) return ICoreWindow_get_Bounds( cw, value );
    if (value)
    {
        SIZE sz;
        xaml_get_layout_metrics( impl->target_hwnd, &sz, NULL );
        value->X = 0; value->Y = 0;
        value->Width = (float)sz.cx; value->Height = (float)sz.cy;
    }
    return S_OK;
}

static HRESULT WINAPI xaml_window_get_Visible( IInspectable *iface, void *a2, void *a3 )
{
    struct xaml_window *impl = &global_xaml_window;
    boolean *value = (boolean *)a2;
    ICoreWindow *cw = NULL;

    if (!value) value = (boolean *)a3;
    TRACE( "iface %p, value %p\n", iface, value );
    if (!impl->core_window) impl->core_window = get_current_core_window();
    cw = impl->core_window;
    if (cw && value) return ICoreWindow_get_Visible( cw, value );
    if (value) *value = TRUE;
    return S_OK;
}

static HRESULT WINAPI xaml_window_get_Content( IInspectable *iface, void *a2, void *a3 )
{
    struct xaml_window *impl = &global_xaml_window;
    IInspectable **value = (IInspectable **)a2;

    if (!value) value = (IInspectable **)a3;
    TRACE( "iface %p, value %p\n", iface, value );
    if (value)
    {
        *value = impl->content;
        if (*value) IInspectable_AddRef( *value );
    }
    return S_OK;
}

static IDCompositionDesktopDevice *global_dcomp_device;

static HRESULT xaml_get_or_create_dcomp_device( IDCompositionDesktopDevice **out_device )
{
    HRESULT hr = S_OK;
    if (!global_dcomp_device)
        hr = DCompositionCreateDevice2( NULL, &IID_IDCompositionDesktopDevice, (void **)&global_dcomp_device );
    if (SUCCEEDED(hr) && out_device)
    {
        *out_device = global_dcomp_device;
        IDCompositionDesktopDevice_AddRef( global_dcomp_device );
    }
    return hr;
}

HRESULT xaml_create_dcomp_visual( IDCompositionVisual **out_visual )
{
    IDCompositionDesktopDevice *dev = NULL;
    HRESULT hr;

    if (!out_visual) return E_POINTER;
    *out_visual = NULL;

    hr = xaml_get_or_create_dcomp_device( &dev );
    if (FAILED(hr)) return hr;

    hr = IDCompositionDesktopDevice_CreateVisual( dev, (IDCompositionVisual2 **)out_visual );
    IDCompositionDesktopDevice_Release( dev );
    return hr;
}

static HRESULT xaml_window_present( struct xaml_window *impl );

static HRESULT WINAPI xaml_handler_QI( void *iface, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IAgileObject ))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI xaml_handler_AddRef( void *iface ) { return 2; }
static ULONG WINAPI xaml_handler_Release( void *iface ) { return 1; }

static HRESULT WINAPI xaml_pointer_pressed_Invoke(
    __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CPointerEventArgs *iface,
    ICoreWindow *sender, __x_ABI_CWindows_CUI_CCore_CIPointerEventArgs *args )
{
    struct xaml_window *impl = &global_xaml_window;
    TRACE( "xaml pointer pressed on hwnd %p\n", impl->target_hwnd );
    xaml_window_present( impl );
    return S_OK;
}

static const void *xaml_pointer_pressed_vtbl[] =
{
    xaml_handler_QI,
    xaml_handler_AddRef,
    xaml_handler_Release,
    xaml_pointer_pressed_Invoke,
};
static struct { const void **vtbl; } xaml_pointer_pressed_handler_obj = { xaml_pointer_pressed_vtbl };

static HRESULT WINAPI xaml_pointer_released_Invoke(
    __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CPointerEventArgs *iface,
    ICoreWindow *sender, __x_ABI_CWindows_CUI_CCore_CIPointerEventArgs *args )
{
    struct xaml_window *impl = &global_xaml_window;
    TRACE( "xaml pointer released on hwnd %p\n", impl->target_hwnd );
    xaml_window_present( impl );
    return S_OK;
}

static const void *xaml_pointer_released_vtbl[] =
{
    xaml_handler_QI,
    xaml_handler_AddRef,
    xaml_handler_Release,
    xaml_pointer_released_Invoke,
};
static struct { const void **vtbl; } xaml_pointer_released_handler_obj = { xaml_pointer_released_vtbl };

static HRESULT WINAPI xaml_char_received_Invoke(
    __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CCharacterReceivedEventArgs *iface,
    ICoreWindow *sender, __x_ABI_CWindows_CUI_CCore_CICharacterReceivedEventArgs *args )
{
    struct xaml_window *impl = &global_xaml_window;
    UINT32 key_code = 0;
    if (args) args->lpVtbl->get_KeyCode( args, &key_code );
    TRACE( "xaml char received: %u (%c)\n", key_code, (char)key_code );
    xaml_window_present( impl );
    return S_OK;
}

static const void *xaml_char_received_vtbl[] =
{
    xaml_handler_QI,
    xaml_handler_AddRef,
    xaml_handler_Release,
    xaml_char_received_Invoke,
};
static struct { const void **vtbl; } xaml_char_received_handler_obj = { xaml_char_received_vtbl };

static HRESULT WINAPI xaml_key_down_Invoke(
    __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CKeyEventArgs *iface,
    ICoreWindow *sender, __x_ABI_CWindows_CUI_CCore_CIKeyEventArgs *args )
{
    struct xaml_window *impl = &global_xaml_window;
    __x_ABI_CWindows_CSystem_CVirtualKey vk = 0;
    if (args) args->lpVtbl->get_VirtualKey( args, &vk );
    TRACE( "xaml key down: %u\n", vk );
    xaml_window_present( impl );
    return S_OK;
}

static const void *xaml_key_down_vtbl[] =
{
    xaml_handler_QI,
    xaml_handler_AddRef,
    xaml_handler_Release,
    xaml_key_down_Invoke,
};
static struct { const void **vtbl; } xaml_key_down_handler_obj = { xaml_key_down_vtbl };

static void xaml_window_subscribe_core_window( struct xaml_window *impl )
{
    EventRegistrationToken tok;
    impl = &global_xaml_window;
    if (!impl->core_window || impl->core_window_subscribed) return;
    impl->core_window_subscribed = TRUE;
    ICoreWindow_add_PointerPressed( impl->core_window, (void *)&xaml_pointer_pressed_handler_obj, &tok );
    ICoreWindow_add_PointerReleased( impl->core_window, (void *)&xaml_pointer_released_handler_obj, &tok );
    ICoreWindow_add_CharacterReceived( impl->core_window, (void *)&xaml_char_received_handler_obj, &tok );
    ICoreWindow_add_KeyDown( impl->core_window, (void *)&xaml_key_down_handler_obj, &tok );
    TRACE( "subscribed xaml_window to core_window %p\n", impl->core_window );
}


static WNDPROC xaml_corewindow_prev_proc;
static HWND xaml_corewindow_subclassed_hwnd;

static void xaml_fill_pointer_info( HWND hwnd, HWND top, UINT msg, WPARAM wparam, LPARAM lparam,
                                    struct xaml_pointer_info *info, POINT *out_pt )
{
    WORD ks = LOWORD( wparam );
    POINT pt;
    RECT rc;

    memset( info, 0, sizeof(*info) );
    if (msg == WM_MOUSEWHEEL)
    {
        pt.x = (short)LOWORD( lparam );
        pt.y = (short)HIWORD( lparam );
        ScreenToClient( top, &pt );
    }
    else
    {
        pt.x = (short)LOWORD( lparam );
        pt.y = (short)HIWORD( lparam );
        if (top && top != hwnd)
            MapWindowPoints( hwnd, top, &pt, 1 );
    }

    if (out_pt) *out_pt = pt;
    info->x = (float)pt.x;
    info->y = (float)pt.y;
    info->pointer_id = 1;
    if (ks & MK_CONTROL) info->key_modifiers |= 0x1;
    if (GetKeyState( VK_MENU ) < 0) info->key_modifiers |= 0x2;
    if (ks & MK_SHIFT) info->key_modifiers |= 0x4;
    info->is_left = (ks & MK_LBUTTON) != 0;
    info->is_right = (ks & MK_RBUTTON) != 0;
    info->is_middle = (ks & MK_MBUTTON) != 0;
    info->update_kind = PointerUpdateKind_Other;

    if (msg == WM_LBUTTONDOWN)
    {
        info->is_left = TRUE;
        info->update_kind = PointerUpdateKind_LeftButtonPressed;
    }
    else if (msg == WM_LBUTTONUP)
    {
        info->is_left = FALSE;
        info->update_kind = PointerUpdateKind_LeftButtonReleased;
    }
    else if (msg == WM_RBUTTONDOWN)
    {
        info->is_right = TRUE;
        info->update_kind = PointerUpdateKind_RightButtonPressed;
    }
    else if (msg == WM_RBUTTONUP)
    {
        info->is_right = FALSE;
        info->update_kind = PointerUpdateKind_RightButtonReleased;
    }
    else if (msg == WM_MOUSEWHEEL)
    {
        info->wheel_delta = (INT32)(short)HIWORD( wparam );
    }

    info->is_in_contact = (info->is_left || info->is_right || info->is_middle);
    GetClientRect( top, &rc );
    info->is_in_window = (pt.x >= 0 && pt.y >= 0 && pt.x < rc.right && pt.y < rc.bottom);
}

static volatile LONG g_xaml_window_dirty = 1;
static BOOL g_in_xaml_present;
static HRESULT xaml_window_present( struct xaml_window *impl );

static BOOL xaml_ensure_retained_surface( struct xaml_window *impl, UINT width, UINT height )
{
    BITMAPINFO bmi;
    HDC screen_dc;

    if (width == 0 || height == 0)
        return FALSE;
    if (impl->retained_dc && impl->retained_bmp &&
        impl->retained_width == width && impl->retained_height == height)
        return TRUE;

    if (impl->retained_dc)
    {
        if (impl->retained_old_bmp)
            SelectObject( impl->retained_dc, impl->retained_old_bmp );
        if (impl->retained_bmp)
            DeleteObject( impl->retained_bmp );
        DeleteDC( impl->retained_dc );
    }
    impl->retained_dc = NULL;
    impl->retained_bmp = NULL;
    impl->retained_old_bmp = NULL;
    impl->retained_bits = NULL;
    impl->retained_width = 0;
    impl->retained_height = 0;
    impl->retained_valid = FALSE;

    screen_dc = GetDC( NULL );
    impl->retained_dc = CreateCompatibleDC( screen_dc );
    if (screen_dc) ReleaseDC( NULL, screen_dc );
    if (!impl->retained_dc)
        return FALSE;

    memset( &bmi, 0, sizeof(bmi) );
    bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
    bmi.bmiHeader.biWidth = (LONG)width;
    bmi.bmiHeader.biHeight = -(LONG)height; /* top-down 32-bpp BGRA */
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    impl->retained_bmp = CreateDIBSection( impl->retained_dc, &bmi, DIB_RGB_COLORS, &impl->retained_bits, NULL, 0 );
    if (!impl->retained_bmp || !impl->retained_bits)
    {
        if (impl->retained_bmp) DeleteObject( impl->retained_bmp );
        DeleteDC( impl->retained_dc );
        impl->retained_dc = NULL;
        impl->retained_bmp = NULL;
        impl->retained_bits = NULL;
        return FALSE;
    }
    impl->retained_old_bmp = (HBITMAP)SelectObject( impl->retained_dc, impl->retained_bmp );
    impl->retained_width = width;
    impl->retained_height = height;
    SetRect( &impl->dirty_rect, 0, 0, (int)width, (int)height );
    impl->has_dirty_rect = TRUE;
    return TRUE;
}

static void xaml_upload_retained_to_swapchain( struct xaml_window *impl, const RECT *dirty )
{
    ID3D11Texture2D *backbuf = NULL;
    D3D11_BOX box;
    UINT row_pitch;
    const BYTE *src;
    HRESULT hr;

    if (!impl->swapchain || !impl->d3d11_ctx || !impl->retained_bits || !impl->retained_valid)
        return;

    box.left = dirty && dirty->left >= 0 ? (UINT)dirty->left : 0;
    box.top = dirty && dirty->top >= 0 ? (UINT)dirty->top : 0;
    box.right = dirty && dirty->right > 0 && (UINT)dirty->right <= impl->retained_width ? (UINT)dirty->right : impl->retained_width;
    box.bottom = dirty && dirty->bottom > 0 && (UINT)dirty->bottom <= impl->retained_height ? (UINT)dirty->bottom : impl->retained_height;
    box.front = 0;
    box.back = 1;
    if (box.left >= box.right || box.top >= box.bottom)
        return;

    hr = IDXGISwapChain1_GetBuffer( impl->swapchain, 0, &IID_ID3D11Texture2D, (void **)&backbuf );
    if (SUCCEEDED(hr) && backbuf)
    {
        row_pitch = impl->retained_width * 4;
        src = (const BYTE *)impl->retained_bits + box.top * row_pitch + box.left * 4;
        ID3D11DeviceContext_UpdateSubresource( impl->d3d11_ctx, (ID3D11Resource *)backbuf, 0, &box, src, row_pitch, 0 );
        ID3D11Texture2D_Release( backbuf );
        IDXGISwapChain1_Present( impl->swapchain, 0, 0 );
    }
}

static BOOL xaml_render_retained_if_dirty( struct xaml_window *impl, HWND hwnd, UINT width, UINT height, RECT *out_dirty )
{
    BOOL dirty;
    BOOL size_changed;
    RECT upload_rc;

    if (width == 0 || height == 0)
        return FALSE;

    dirty = InterlockedExchange( &g_xaml_window_dirty, 0 ) != 0;
    size_changed = (!impl->retained_valid || impl->retained_width != width || impl->retained_height != height);
    if (!dirty && !size_changed)
    {
        if (out_dirty) SetRectEmpty( out_dirty );
        return FALSE;
    }

    if (!xaml_ensure_retained_surface( impl, width, height ))
        return FALSE;

    xaml_layout_render( hwnd, impl->retained_dc, (int)width, (int)height, impl->content );
    impl->retained_valid = TRUE;

    if (!size_changed && impl->has_dirty_rect && !IsRectEmpty( &impl->dirty_rect ))
    {
        upload_rc = impl->dirty_rect;
        if (upload_rc.left < 0) upload_rc.left = 0;
        if (upload_rc.top < 0) upload_rc.top = 0;
        if (upload_rc.right > (LONG)width) upload_rc.right = (LONG)width;
        if (upload_rc.bottom > (LONG)height) upload_rc.bottom = (LONG)height;
    }
    else
    {
        SetRect( &upload_rc, 0, 0, (int)width, (int)height );
    }
    impl->has_dirty_rect = FALSE;
    SetRectEmpty( &impl->dirty_rect );
    if (out_dirty) *out_dirty = upload_rc;
    return TRUE;
}

static void *g_active_scp_elem = NULL;
static void *g_active_scp_swapchain = NULL;

void xaml_get_host_window_size( UINT *out_w, UINT *out_h )
{
    struct xaml_window *impl = &global_xaml_window;
    UINT w = impl->width, h = impl->height;
    if (impl->target_hwnd && IsWindow( impl->target_hwnd ))
    {
        RECT rc;
        if (GetClientRect( impl->target_hwnd, &rc ) && rc.right > 0 && rc.bottom > 0)
        {
            w = (UINT)rc.right;
            h = (UINT)rc.bottom;
        }
    }
    if (w == 0) w = 1280;
    if (h == 0) h = 720;
    if (out_w) *out_w = w;
    if (out_h) *out_h = h;
}

BOOL xaml_has_active_swapchain_panel( void )
{
    return g_active_scp_swapchain != NULL;
}

void xaml_set_active_swapchain_panel( void *el, void *sc )
{
    struct xaml_window *impl = &global_xaml_window;
    g_active_scp_elem = sc ? el : NULL;
    g_active_scp_swapchain = sc;
    if (sc && impl->swapchain && !impl->swapchain_is_composition)
    {
        xaml_gpu_release_rtv();
        if (impl->d3d11_ctx)
            ID3D11DeviceContext_Flush( impl->d3d11_ctx );
        IDXGISwapChain1_Release( impl->swapchain );
        impl->swapchain = NULL;
        impl->gpu_active = FALSE;
        impl->gpu_rendered_once = FALSE;
        InterlockedExchange( &g_xaml_window_dirty, 1 );
        TRACE( "xaml_set_active_swapchain_panel: switched XAML swapchain to DComp overlay mode for sc=%p\n", sc );
    }
}

HRESULT xaml_attach_swapchain_panel_content( IUnknown *content, IDCompositionVisual2 **slot )
{
    struct xaml_window *impl = &global_xaml_window;
    HRESULT hr;

    if (!slot) return E_POINTER;
    if (!impl->dcomp_device || !impl->dcomp_root_visual)
    {
        TRACE( "xaml_attach_swapchain_panel_content: dcomp not ready (dev=%p root=%p)\n",
               impl->dcomp_device, impl->dcomp_root_visual );
        return S_OK;
    }
    if (!impl->dcomp_target && impl->target_hwnd)
    {
        if (SUCCEEDED(IDCompositionDesktopDevice_CreateTargetForHwnd( impl->dcomp_device, impl->target_hwnd, TRUE, &impl->dcomp_target )) &&
            impl->dcomp_target)
        {
            IDCompositionTarget_SetRoot( impl->dcomp_target, (IDCompositionVisual *)impl->dcomp_root_visual );
        }
    }
    if (!*slot)
    {
        hr = IDCompositionDesktopDevice_CreateVisual( impl->dcomp_device, slot );
        if (FAILED(hr) || !*slot) return hr;
        /* Insert below all existing visuals (including XAML overlay visual) */
        IDCompositionVisual_AddVisual( (IDCompositionVisual *)impl->dcomp_root_visual,
                                       (IDCompositionVisual *)*slot, FALSE, NULL );
    }
    hr = IDCompositionVisual_SetContent( (IDCompositionVisual *)*slot, content );
    IDCompositionDesktopDevice_Commit( impl->dcomp_device );
    TRACE( "xaml_attach_swapchain_panel_content content=%p vis=%p hr=%#lx\n", content, *slot, hr );
    return hr;
}

HRESULT xaml_attach_swapchain_panel_handle( HANDLE h, IDCompositionVisual2 **slot )
{
    struct xaml_window *impl = &global_xaml_window;
    IUnknown *surface = NULL;
    HRESULT hr;

    if (!slot) return E_POINTER;
    if (!impl->dcomp_device || !impl->dcomp_root_visual)
        return S_OK;
    if (h)
    {
        hr = IDCompositionDesktopDevice_CreateSurfaceFromHandle( impl->dcomp_device, h, &surface );
        if (FAILED(hr)) return hr;
    }
    hr = xaml_attach_swapchain_panel_content( surface, slot );
    if (surface) IUnknown_Release( surface );
    return hr;
}

static void xaml_window_refresh_present( HWND hwnd )
{
    static BOOL in_refresh;
    struct xaml_window *impl = &global_xaml_window;
    UINT width, height;
    RECT rc;

    if (in_refresh || g_in_xaml_present || !impl->dcomp_device)
        return;

    in_refresh = TRUE;
    if (impl->swapchain && !xaml_get_app_caps()->winui3)
    {
        HWND target = impl->target_hwnd && IsWindow( impl->target_hwnd ) ? impl->target_hwnd : hwnd;
        GetClientRect( target, &rc );
        width = rc.right > 0 ? (UINT)rc.right : impl->width;
        height = rc.bottom > 0 ? (UINT)rc.bottom : impl->height;
        if (width > 0 && height > 0)
        {
            if (impl->d3d11_dev && impl->d3d11_ctx &&
                xaml_layout_render_gpu( target, (int)width, (int)height, impl->content,
                                        impl->d3d11_dev, impl->d3d11_ctx, impl->swapchain ))
            {
                impl->gpu_active = TRUE;
                impl->gpu_rendered_once = TRUE;
            }
            else
            {
                RECT dirty_rc;
                if (xaml_render_retained_if_dirty( impl, target, width, height, &dirty_rc ))
                    xaml_upload_retained_to_swapchain( impl, &dirty_rc );
            }
        }
    }
    IDCompositionDesktopDevice_Commit( impl->dcomp_device );
    in_refresh = FALSE;
}

#define XAML_SCP_DIRTY_TIMER_ID 0x58534350

void xaml_mark_window_dirty( void )
{
    if (g_in_xaml_present) return;
    InterlockedExchange( &g_xaml_window_dirty, 1 );
    if (global_xaml_window.target_hwnd && IsWindow( global_xaml_window.target_hwnd ))
        InvalidateRect( global_xaml_window.target_hwnd, NULL, FALSE );
}

static void xaml_corewindow_sync_size( HWND hwnd )
{
    HWND top = GetAncestor( hwnd, GA_ROOT );
    RECT rc;
    if (!top) top = hwnd;
    GetClientRect( top, &rc );
    if (rc.right > 0 && rc.bottom > 0)
    {
        if (top != hwnd)
            SetWindowPos( hwnd, 0, 0, 0, rc.right, rc.bottom,
                          SWP_NOZORDER | SWP_NOMOVE | SWP_NOACTIVATE );
        xaml_wv2_on_window_resize( top, rc.right, rc.bottom );
        if ((UINT)rc.right != global_xaml_window.width ||
            (UINT)rc.bottom != global_xaml_window.height)
        {
            float old_w = (float)global_xaml_window.width;
            float old_h = (float)global_xaml_window.height;
            xaml_mark_window_dirty();
            xaml_window_present( &global_xaml_window );
            xaml_winui_fire_size_changed( old_w, old_h, (float)rc.right, (float)rc.bottom );
        }
    }
}

static LRESULT WINAPI xaml_corewindow_wndproc( HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam )
{
    if (msg == WM_ERASEBKGND)
        return TRUE;
    if (msg == WM_NCHITTEST)
    {
        HWND top = GetAncestor( hwnd, GA_ROOT );
        RECT rc;
        if (!top) top = hwnd;
        if (GetClientRect( top, &rc ) && rc.right > 0 && rc.bottom > 0 &&
            ((UINT)rc.right != global_xaml_window.width ||
             (UINT)rc.bottom != global_xaml_window.height))
        {
            xaml_corewindow_sync_size( hwnd );
        }
        if (global_xaml_window.content)
        {
            POINT pt = { (short)LOWORD( lparam ), (short)HIWORD( lparam ) };
            if (ScreenToClient( top, &pt ) &&
                uwp_hit_test_interactive_client( global_xaml_window.content, pt.x, pt.y ))
                return HTCLIENT;
        }
        goto forward_msg;
    }
    if (msg == WM_PAINT)
    {
        {
            HWND top = GetAncestor( hwnd, GA_ROOT );
            RECT rc;
            if (!top) top = hwnd;
            if (GetClientRect( top, &rc ) && rc.right > 0 && rc.bottom > 0 &&
                ((UINT)rc.right != global_xaml_window.width ||
                 (UINT)rc.bottom != global_xaml_window.height))
            {
                xaml_corewindow_sync_size( hwnd );
            }
        }
        if (!xaml_get_app_caps()->winui3)
        {
            RECT rc, dirty_rc;
            HWND top = GetAncestor( hwnd, GA_ROOT );
            if (!top) top = hwnd;
            GetClientRect( top, &rc );
            TRACE( "xaml corewindow WM_PAINT top %p rc %ldx%ld\n", top, rc.right, rc.bottom );
            uwp_fire_composition_target_rendering();
            if (rc.right > 0 && rc.bottom > 0)
            {
                if (global_xaml_window.gpu_active && global_xaml_window.swapchain &&
                    global_xaml_window.d3d11_dev && global_xaml_window.d3d11_ctx)
                {
                    if (!g_in_xaml_present && InterlockedExchange( &g_xaml_window_dirty, 0 ) != 0)
                    {
                        g_in_xaml_present = TRUE;
                        if (xaml_layout_render_gpu( top, (int)rc.right, (int)rc.bottom, global_xaml_window.content,
                                                    global_xaml_window.d3d11_dev, global_xaml_window.d3d11_ctx,
                                                    global_xaml_window.swapchain ) &&
                            global_xaml_window.dcomp_device && global_xaml_window.dcomp_target)
                        {
                            IDCompositionDesktopDevice_Commit( global_xaml_window.dcomp_device );
                        }
                        InterlockedExchange( &g_xaml_window_dirty, 0 );
                        g_in_xaml_present = FALSE;
                    }
                }
                else
                {
                    if (xaml_render_retained_if_dirty( &global_xaml_window, top, (UINT)rc.right, (UINT)rc.bottom, &dirty_rc ))
                    {
                        if (!g_in_xaml_present && global_xaml_window.swapchain && global_xaml_window.d3d11_ctx)
                        {
                            xaml_upload_retained_to_swapchain( &global_xaml_window, &dirty_rc );
                            if (global_xaml_window.dcomp_device)
                                IDCompositionDesktopDevice_Commit( global_xaml_window.dcomp_device );
                        }
                    }
                    if (global_xaml_window.retained_valid && global_xaml_window.retained_dc)
                    {
                        HDC hdc = GetDC( top );
                        if (hdc)
                        {
                            BitBlt( hdc, 0, 0, rc.right, rc.bottom, global_xaml_window.retained_dc, 0, 0, SRCCOPY );
                            ReleaseDC( top, hdc );
                        }
                    }
                }
            }
        }
        ValidateRect( hwnd, NULL );
        return 0;
    }
    if (msg == WM_MOVE || msg == WM_EXITSIZEMOVE)
    {
        LRESULT ret;
        if (xaml_corewindow_prev_proc && xaml_corewindow_prev_proc != xaml_corewindow_wndproc)
            ret = CallWindowProcW( xaml_corewindow_prev_proc, hwnd, msg, wparam, lparam );
        else
            ret = DefWindowProcW( hwnd, msg, wparam, lparam );
        if (msg == WM_EXITSIZEMOVE)
            xaml_corewindow_sync_size( hwnd );
        xaml_window_refresh_present( hwnd );
        return ret;
    }
    if (msg == WM_WINDOWPOSCHANGED)
    {
        const WINDOWPOS *wp = (const WINDOWPOS *)lparam;
        LRESULT ret;
        if (xaml_corewindow_prev_proc && xaml_corewindow_prev_proc != xaml_corewindow_wndproc)
            ret = CallWindowProcW( xaml_corewindow_prev_proc, hwnd, msg, wparam, lparam );
        else
            ret = DefWindowProcW( hwnd, msg, wparam, lparam );
        if (!wp || !(wp->flags & SWP_NOSIZE))
            xaml_corewindow_sync_size( hwnd );
        if (!wp || !(wp->flags & SWP_NOMOVE) || !(wp->flags & SWP_NOSIZE) || (wp->flags & SWP_SHOWWINDOW))
            xaml_window_refresh_present( hwnd );
        return ret;
    }
    if (msg == WM_TIMER && wparam == XAML_SCP_DIRTY_TIMER_ID)
    {
        if (!g_in_xaml_present && InterlockedCompareExchange( &g_xaml_window_dirty, 0, 0 ))
            xaml_window_present( &global_xaml_window );
        return 0;
    }
    if (msg == WM_APP_WV2_START)
    {
        TRACE( "xaml_corewindow_wndproc: WM_APP_WV2_START hwnd %p\n", hwnd );
        xaml_wv2_handle_start_msg( lparam );
        return 0;
    }
    if (msg == WM_SIZE)
    {
        LRESULT ret;
        if (xaml_corewindow_prev_proc && xaml_corewindow_prev_proc != xaml_corewindow_wndproc)
            ret = CallWindowProcW( xaml_corewindow_prev_proc, hwnd, msg, wparam, lparam );
        else
            ret = DefWindowProcW( hwnd, msg, wparam, lparam );
        xaml_corewindow_sync_size( hwnd );
        return ret;
    }
    if (msg == WM_CLOSE)
    {
        /* Close button / Alt+F4: tear the window down and quit the message loop. */
        DestroyWindow( hwnd );
        PostQuitMessage( 0 );
        return 0;
    }
    if (msg == WM_SETFOCUS || msg == WM_KILLFOCUS)
    {
        xaml_winui_fire_focus_event( msg == WM_SETFOCUS );
        goto forward_msg;
    }
    if (msg == WM_DEADCHAR || msg == WM_SYSCHAR)
        goto forward_msg;
    if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN || msg == WM_KEYUP || msg == WM_CHAR)
    {
        struct xaml_core_window *cw = as_xaml_core_window( global_xaml_window.core_window );
        HRESULT hr;
        BOOL accel_handled = FALSE;
        if (msg == WM_KEYDOWN && wparam == VK_F12)
        {
            xaml_mark_window_dirty();
            xaml_window_present( &global_xaml_window );
            uwp_debug_dump_root( global_xaml_window.content );
            return 0;
        }
        if (uwp_has_focused_textbox())
        {
            if (msg == WM_CHAR && !(GetKeyState( VK_CONTROL ) & 0x8000) && !(GetKeyState( VK_MENU ) & 0x8000))
            {
                uwp_keyboard_press_char( (WCHAR)wparam );
                xaml_mark_window_dirty();
                xaml_window_present( &global_xaml_window );
            }
            return 0;
        }
        if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN)
            accel_handled = uwp_dispatch_keydown( hwnd, (UINT)wparam, lparam, NULL );
        if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN || msg == WM_KEYUP)
            xaml_winui_fire_key_event( msg != WM_KEYUP, (UINT32)wparam, lparam );
        xaml_cw_fire_key_event( cw, msg, wparam, lparam );
        hr = notify_inner_core_window_key( hwnd, msg, wparam );
        if (msg == WM_CHAR && !(GetKeyState( VK_CONTROL ) & 0x8000) && !(GetKeyState( VK_MENU ) & 0x8000))
            uwp_keyboard_press_char( (WCHAR)wparam );
        uwp_sync_calc_display_from_vm();
        xaml_mark_window_dirty();
        if (msg != WM_KEYUP)
            xaml_window_present( &global_xaml_window );
        if ((accel_handled || (msg != WM_SYSKEYDOWN && SUCCEEDED( hr ))) &&
            xaml_corewindow_prev_proc == (WNDPROC)GetClassLongPtrW( hwnd, GCLP_WNDPROC ))
            return 0;
        goto forward_msg;
    }
    if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP || msg == WM_RBUTTONDOWN || msg == WM_RBUTTONUP ||
        msg == WM_MOUSEMOVE || msg == WM_MOUSEWHEEL || msg == WM_MOUSELEAVE || msg == WM_CAPTURECHANGED)
    {
        HWND top;
        struct xaml_core_window *cw;
        struct xaml_pointer_info info;
        enum xaml_pointer_event_kind kind;
        RECT rc;
        POINT pt = { 0, 0 };
        BOOL btn_handled = FALSE;
        BOOL tree_routed = FALSE;

        top = GetAncestor( hwnd, GA_ROOT );
        cw = as_xaml_core_window( global_xaml_window.core_window );

        if (!top) top = hwnd;
        GetClientRect( top, &rc );

        if (msg == WM_CAPTURECHANGED)
        {
            if (cw && cw->has_capture && (HWND)lparam != hwnd)
            {
                cw->has_capture = FALSE;
                memset( &info, 0, sizeof(info) );
                info.x = (float)cw->last_pos.x;
                info.y = (float)cw->last_pos.y;
                info.pointer_id = 1;
                info.is_in_window = cw->pointer_inside;
                xaml_cw_fire_pointer_event( cw, XAML_PTR_CAPTURE_LOST, &info );
                xaml_winui_fire_pointer_event( XAML_PTR_CAPTURE_LOST, &info );
                if (global_xaml_window.content && rc.right > 0 && rc.bottom > 0)
                    uwp_route_pointer_event( top, XAML_PTR_CAPTURE_LOST, &info, rc.right, rc.bottom, global_xaml_window.content );
            }
            goto forward_msg;
        }

        if (msg == WM_MOUSELEAVE)
        {
            memset( &info, 0, sizeof(info) );
            info.pointer_id = 1;
            info.is_in_window = FALSE;
            if (cw)
            {
                info.x = (float)cw->last_pos.x;
                info.y = (float)cw->last_pos.y;
                if (cw->pointer_inside)
                {
                    cw->pointer_inside = FALSE;
                    xaml_cw_fire_pointer_event( cw, XAML_PTR_EXITED, &info );
                    xaml_winui_fire_pointer_event( XAML_PTR_EXITED, &info );
                }
            }
            if (global_xaml_window.content && rc.right > 0 && rc.bottom > 0)
                uwp_route_pointer_event( top, XAML_PTR_EXITED, &info, rc.right, rc.bottom, global_xaml_window.content );
            return 0;
        }

        xaml_fill_pointer_info( hwnd, top, msg, wparam, lparam, &info, &pt );
        if (cw)
        {
            cw->last_pos = pt;
            cw->has_pos = TRUE;
            if (msg == WM_MOUSEMOVE && !cw->pointer_inside)
            {
                TRACKMOUSEEVENT tme;
                cw->pointer_inside = TRUE;
                tme.cbSize = sizeof(tme);
                tme.dwFlags = TME_LEAVE;
                tme.hwndTrack = hwnd;
                tme.dwHoverTime = 0;
                TrackMouseEvent( &tme );
                xaml_cw_fire_pointer_event( cw, XAML_PTR_ENTERED, &info );
                xaml_winui_fire_pointer_event( XAML_PTR_ENTERED, &info );
            }
        }

        switch (msg)
        {
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
            kind = XAML_PTR_PRESSED;
            if (cw) { SetCapture( hwnd ); cw->has_capture = TRUE; }
            break;
        case WM_LBUTTONUP:
        case WM_RBUTTONUP:
            kind = XAML_PTR_RELEASED;
            break;
        case WM_MOUSEWHEEL:
            kind = XAML_PTR_WHEEL;
            break;
        default:
            kind = XAML_PTR_MOVED;
            break;
        }

        xaml_cw_fire_pointer_event( cw, kind, &info );
        xaml_winui_fire_pointer_event( kind, &info );
        if ((msg == WM_LBUTTONUP || msg == WM_RBUTTONUP) && cw && cw->has_capture)
        {
            cw->has_capture = FALSE;
            ReleaseCapture();
        }

        if (global_xaml_window.content && rc.right > 0 && rc.bottom > 0)
        {
            if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP || msg == WM_MOUSEMOVE || msg == WM_MOUSEWHEEL)
                btn_handled = uwp_handle_overlay_mouse_input( top, msg, pt.x, pt.y,
                                                              (msg == WM_MOUSEWHEEL) ? GET_WHEEL_DELTA_WPARAM( wparam ) : 0,
                                                              rc.right, rc.bottom, global_xaml_window.content );
            if (!btn_handled)
            {
                tree_routed = uwp_route_pointer_event( top, kind, &info, rc.right, rc.bottom, global_xaml_window.content );
                if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP || msg == WM_MOUSEMOVE)
                    btn_handled = uwp_handle_mouse_input( top, msg, pt.x, pt.y, rc.right, rc.bottom, global_xaml_window.content );
            }
        }
        if (!btn_handled && g_active_scp_swapchain)
            uwp_ciis_dispatch_pointer_event( kind, &info );

        if (btn_handled || (tree_routed && (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP)))
        {
            if (msg == WM_MOUSEMOVE)
                uwp_invalidate_paint_only();
            else
            {
                xaml_mark_window_dirty();
                xaml_window_present( &global_xaml_window );
            }
            return 0;
        }
        return 0;
    }
forward_msg:
    TRACE( "xaml corewindow msg %x\n", msg );
    if (xaml_corewindow_prev_proc == xaml_corewindow_wndproc || !xaml_corewindow_prev_proc)
        return DefWindowProcW( hwnd, msg, wparam, lparam );
    return CallWindowProcW( xaml_corewindow_prev_proc, hwnd, msg, wparam, lparam );
}

static HRESULT xaml_window_present( struct xaml_window *impl )
{
    static BOOL d3d11_create_failed;
    HWND hwnd = NULL;
    SIZE sz;
    float scale = 1.0f;
    UINT width, height, prev_width, prev_height;
    HRESULT hr;

    impl = &global_xaml_window;
    if (g_in_xaml_present) return S_OK;
    g_in_xaml_present = TRUE;

    if (!impl->core_window) ensure_core_window( impl );
    if (impl->core_window)
    {
        ICoreWindowInterop *interop = NULL;
        if (SUCCEEDED(ICoreWindow_QueryInterface( impl->core_window, &IID_ICoreWindowInterop, (void **)&interop )))
        {
            interop->lpVtbl->GetWindowHandle( interop, &hwnd );
            interop->lpVtbl->Release( interop );
        }
    }
    if (!hwnd) hwnd = xaml_find_core_window();
    if (!hwnd) hwnd = GetActiveWindow();
    if (!hwnd) { g_in_xaml_present = FALSE; return S_FALSE; }

    TRACE( "xaml_window_present: hwnd=%p swapchain=%p dev=%p is_uwp=%d\n",
           hwnd, impl->swapchain, impl->d3d11_dev, !xaml_get_app_caps()->winui3 );

    /* Re-install the window-slot subclass on every attach: twinapi sets the CoreWindow
     * wndproc on activation, which pushes a previous window-slot subclass aside. The
     * class proc is the wrong layer (twinapi's proc never chains to it), so stay on the
     * window slot and re-assert it whenever we present. */
    {
        struct xaml_core_window *cw = as_xaml_core_window( impl->core_window );
        if (cw) cw->hwnd = hwnd;
        if (xaml_corewindow_subclassed_hwnd != hwnd)
        {
            WNDPROC cur = (WNDPROC)(void *)GetWindowLongPtrW( hwnd, GWLP_WNDPROC );
            if (cur != xaml_corewindow_wndproc)
            {
                xaml_corewindow_prev_proc = cur;
                SetWindowLongPtrW( hwnd, GWLP_WNDPROC, (LONG_PTR)xaml_corewindow_wndproc );
            }
            xaml_corewindow_subclassed_hwnd = hwnd;
        }
        /* Drive the class background brush too: WM_ERASEBKGND from the DefWindowProc
         * paint path is the one GDI operation the winex11 driver reliably flushes to
         * the X pixmap of a fresh top-level. Match the Dark theme keypad background
         * so an erase between layout paints shows the same dark grey, not a flash. */
        SetClassLongPtrW( hwnd, GCLP_HBRBACKGROUND, (LONG_PTR)(void *)CreateSolidBrush( RGB( 32, 32, 32 ) ) );
    }

    /* The DComp target and swapchain attach to this HWND; it must be visible so the
     * X11 driver maps the top-level window and the composited frames show on screen.
     * Invalidate + update right after ShowWindow: a window mapped before the swapchain
     * existed has a stale X11 backing store, and only a fresh paint cycle flushes the
     * first composited frame. */
    if (!impl->target_hwnd)
    {
        HWND top = GetAncestor( hwnd, GA_ROOT );
        RECT wrc, drc;
        SIZE target_sz, def_sz = XAML_DEFAULT_CLIENT_SIZE;

        if (!top) top = hwnd;
        if (GetWindowRect( top, &wrc ) &&
            (wrc.right - wrc.left) == def_sz.cx &&
            (wrc.bottom - wrc.top) == def_sz.cy)
        {
            xaml_get_layout_metrics( NULL, &target_sz, NULL );
            SetWindowPos( top, 0, 0, 0, target_sz.cx, target_sz.cy,
                          SWP_NOZORDER | SWP_NOMOVE | SWP_NOACTIVATE );
        }
        if (GetClientRect( GetDesktopWindow(), &drc ) && drc.right > 0 && drc.bottom > 0 &&
            GetWindowRect( top, &wrc ) &&
            ((wrc.right - wrc.left) > drc.right || (wrc.bottom - wrc.top) > drc.bottom))
        {
            SetWindowPos( top, 0, 0, 0, drc.right, drc.bottom,
                          SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED );
        }
        ShowWindow( hwnd, SW_SHOW );
        SetWindowPos( hwnd, 0, 0, 0, 0, 0, SWP_SHOWWINDOW | SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE );
    }
    else if (!IsWindowVisible( hwnd ))
    {
        ShowWindow( hwnd, SW_SHOW );
    }
    /* The X11 window manager shows the root ancestor's title and icon; the
     * CoreWindow child has no frame. Set both on the top-level once, on show. */
    {
        HWND top = GetAncestor( hwnd, GA_ROOT );
        const WCHAR *title = xaml_explicit_title[0] ? xaml_explicit_title : xaml_package_display_name();
        if (!top) top = hwnd;
        if (title) SetWindowTextW( top, title );
        xaml_apply_window_icon( top );
    }
    TRACE( "xaml show: visible %u style %08lx exstyle %08lx\n", IsWindowVisible( hwnd ),
           GetWindowLongW( hwnd, GWL_STYLE ), GetWindowLongW( hwnd, GWL_EXSTYLE ) );
    {
        LONG style = GetWindowLongW( hwnd, GWL_STYLE );
        HWND top = GetAncestor( hwnd, GA_ROOT );
        if (!(style & WS_CLIPCHILDREN))
            SetWindowLongW( hwnd, GWL_STYLE, style | WS_CLIPCHILDREN );
        if (top && top != hwnd)
        {
            LONG top_style = GetWindowLongW( top, GWL_STYLE );
            if (!(top_style & WS_CLIPCHILDREN))
                SetWindowLongW( top, GWL_STYLE, top_style | WS_CLIPCHILDREN );
        }
    }

    if (impl->core_window) xaml_window_subscribe_core_window( impl );

    xaml_get_layout_metrics( hwnd, &sz, &scale );
    width = (UINT)sz.cx;
    height = (UINT)sz.cy;

    prev_width = impl->width;
    prev_height = impl->height;
    impl->target_hwnd = hwnd;
    impl->width = width;
    impl->height = height;

    /* 1. Ensure DComp desktop device */
    if (!impl->dcomp_device)
    {
        hr = xaml_get_or_create_dcomp_device( &impl->dcomp_device );
        if (FAILED(hr))
        {
            WARN( "xaml_get_or_create_dcomp_device failed hr %#lx\n", hr );
            g_in_xaml_present = FALSE;
            return hr;
        }
    }

    /* 2. Ensure DComp target for CoreWindow HWND (only when WinUI 3 or SwapChainPanel needs DComp target;
     * creating the 1x1 DComp child HWND on direct-swapchain UWP windows triggers winex11 offscreen SYSRGN clipping) */
    if (!impl->dcomp_target && (xaml_get_app_caps()->winui3 || g_active_scp_swapchain))
    {
        hr = IDCompositionDesktopDevice_CreateTargetForHwnd( impl->dcomp_device, hwnd, TRUE, &impl->dcomp_target );
        if (FAILED(hr))
        {
            WARN( "CreateTargetForHwnd failed hr %#lx\n", hr );
            g_in_xaml_present = FALSE;
            return hr;
        }
        if (impl->dcomp_root_visual)
            IDCompositionTarget_SetRoot( impl->dcomp_target, (IDCompositionVisual *)impl->dcomp_root_visual );
    }

    /* Proactively cache the UI thread's DispatcherQueue so worker threads
     * querying DependencyObject.DispatcherQueue (e.g. WebView2.DispatcherQueue)
     * receive the UI thread's queue instead of NULL. */
    {
        static BOOL dq_warmed;
        if (!dq_warmed)
        {
            void *dq = NULL;
            dq_warmed = TRUE;
            if (SUCCEEDED(uwp_do_get_DispatcherQueue( NULL, &dq )) && dq)
                IInspectable_Release( (IInspectable *)dq );
        }
    }

    /* 3. Ensure DComp root visual */
    if (!impl->dcomp_root_visual)
    {
        hr = IDCompositionDesktopDevice_CreateVisual( impl->dcomp_device, &impl->dcomp_root_visual );
        if (FAILED(hr))
        {
            WARN( "CreateVisual failed hr %#lx\n", hr );
            g_in_xaml_present = FALSE;
            return hr;
        }
        if (impl->dcomp_target)
            IDCompositionTarget_SetRoot( impl->dcomp_target, (IDCompositionVisual *)impl->dcomp_root_visual );
    }
    if (impl->dcomp_root_visual)
    {
        RECT client_rc;
        if (hwnd && IsWindow( hwnd ) && !IsIconic( hwnd ) &&
            GetClientRect( hwnd, &client_rc ) && client_rc.right > 0 && client_rc.bottom > 0)
        {
            D2D_RECT_F clip = { 0.0f, 0.0f, (float)client_rc.right, (float)client_rc.bottom };
            IDCompositionVisual_SetClip( (IDCompositionVisual *)impl->dcomp_root_visual, &clip );
        }
    }

    /* 4. Connect Window.Current.Content visual to root visual */
    if (impl->content)
    {
        IDCompositionVisual *content_visual = NULL;
        if (SUCCEEDED(IInspectable_QueryInterface( impl->content, &IID_IDCompositionVisual, (void **)&content_visual )))
        {
            IDCompositionVisual_AddVisual( (IDCompositionVisual *)impl->dcomp_root_visual, content_visual, TRUE, NULL );
            IDCompositionVisual_Release( content_visual );
        }
    }

    /* 5. Ensure composition swapchain created via CreateSwapChainForComposition */
    if (impl->swapchain && (width != prev_width || height != prev_height))
    {
        xaml_gpu_release_rtv();
        hr = IDXGISwapChain1_ResizeBuffers( impl->swapchain, 0, width, height,
                                            DXGI_FORMAT_B8G8R8A8_UNORM, 0 );
        TRACE( "XAML: resized composition swapchain %p to %ux%u (hr %#lx)\n",
               impl->swapchain, width, height, hr );
        if (g_active_scp_elem && width > 1 && height > 1)
            uwp_notify_swapchain_panel_resize( g_active_scp_elem, width, height );
    }
    if (!impl->swapchain && !xaml_get_app_caps()->winui3 && !d3d11_create_failed)
    {
        static const D3D_FEATURE_LEVEL feature_levels[] =
        {
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_1,
            D3D_FEATURE_LEVEL_10_0,
        };
        D3D_FEATURE_LEVEL feature_level;
        IDXGIDevice *dxgi_dev = NULL;
        IDXGIAdapter *dxgi_adapter = NULL;
        IDXGIFactory2 *dxgi_factory = NULL;
        DXGI_SWAP_CHAIN_DESC1 desc = {0};

        desc.Width = width;
        desc.Height = height;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.Stereo = FALSE;
        desc.SampleDesc.Count = 1;
        desc.SampleDesc.Quality = 0;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 1;
        desc.Scaling = DXGI_SCALING_STRETCH;
        desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        desc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
        desc.Flags = 0;

        hr = D3D11CreateDevice( NULL, D3D_DRIVER_TYPE_HARDWARE, NULL,
                                D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                feature_levels, ARRAY_SIZE(feature_levels),
                                D3D11_SDK_VERSION, &impl->d3d11_dev, &feature_level, &impl->d3d11_ctx );
        if (FAILED(hr))
        {
            hr = D3D11CreateDevice( NULL, D3D_DRIVER_TYPE_WARP, NULL,
                                    D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                    feature_levels, ARRAY_SIZE(feature_levels),
                                    D3D11_SDK_VERSION, &impl->d3d11_dev, &feature_level, &impl->d3d11_ctx );
        }
        if (FAILED(hr) || !impl->d3d11_dev)
            d3d11_create_failed = TRUE;
        if (SUCCEEDED(hr) && impl->d3d11_dev)
        {
            hr = ID3D11Device_QueryInterface( impl->d3d11_dev, &IID_IDXGIDevice, (void **)&dxgi_dev );
            if (SUCCEEDED(hr))
            {
                hr = IDXGIDevice_GetAdapter( dxgi_dev, &dxgi_adapter );
                if (SUCCEEDED(hr))
                {
                    hr = IDXGIAdapter_GetParent( dxgi_adapter, &IID_IDXGIFactory2, (void **)&dxgi_factory );
                    if (SUCCEEDED(hr))
                    {
                        if (g_active_scp_swapchain)
                        {
                            desc.BufferCount = 2;
                            desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT | DXGI_USAGE_SHADER_INPUT;
                            desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
                            desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
                            hr = IDXGIFactory2_CreateSwapChainForComposition( dxgi_factory, (IUnknown *)impl->d3d11_dev,
                                                                              &desc, NULL, &impl->swapchain );
                            if (SUCCEEDED(hr) && impl->swapchain)
                            {
                                impl->swapchain_is_composition = TRUE;
                                if (impl->dcomp_device && impl->dcomp_root_visual)
                                {
                                    if (!impl->dcomp_overlay_visual)
                                    {
                                        if (SUCCEEDED(IDCompositionDesktopDevice_CreateVisual( impl->dcomp_device, &impl->dcomp_overlay_visual ))
                                            && impl->dcomp_overlay_visual)
                                        {
                                            IDCompositionVisual_AddVisual( (IDCompositionVisual *)impl->dcomp_root_visual,
                                                                           (IDCompositionVisual *)impl->dcomp_overlay_visual, TRUE, NULL );
                                        }
                                    }
                                    if (impl->dcomp_overlay_visual)
                                    {
                                        IDCompositionVisual_SetContent( (IDCompositionVisual *)impl->dcomp_overlay_visual,
                                                                        (IUnknown *)impl->swapchain );
                                        IDCompositionDesktopDevice_Commit( impl->dcomp_device );
                                    }
                                }
                            }
                        }
                        else
                        {
                            /* Present directly to the top-level XAML CoreWindow HWND so Present()
                             * blits the rendered UWP tree onscreen without relying on DComp target. */
                            hr = IDXGIFactory2_CreateSwapChainForHwnd( dxgi_factory, (IUnknown *)impl->d3d11_dev,
                                                                       hwnd, &desc, NULL, NULL, &impl->swapchain );
                            impl->swapchain_is_composition = FALSE;
                        }
                        IDXGIFactory2_Release( dxgi_factory );
                    }
                    IDXGIAdapter_Release( dxgi_adapter );
                }
                IDXGIDevice_Release( dxgi_dev );
            }
        }
    }

    /* 6. Render the XAML layout directly on GPU (Instanced SDF + Atlas) or fall back to retained DIB */
    {
        LARGE_INTEGER t0, t1, t2, freq;
        RECT dirty_rc;
        BOOL rendered = FALSE;
        BOOL gpu_ok = FALSE;

        QueryPerformanceFrequency( &freq );
        QueryPerformanceCounter( &t0 );
        if (impl->swapchain && impl->d3d11_dev && impl->d3d11_ctx && !xaml_get_app_caps()->winui3)
        {
            BOOL dirty = InterlockedExchange( &g_xaml_window_dirty, 0 ) != 0;
            BOOL size_changed = (width != prev_width || height != prev_height || !impl->gpu_rendered_once);
            if (dirty || size_changed)
            {
                gpu_ok = xaml_layout_render_gpu( hwnd, (int)width, (int)height, impl->content,
                                                 impl->d3d11_dev, impl->d3d11_ctx, impl->swapchain );
                if (gpu_ok)
                {
                    impl->gpu_active = TRUE;
                    impl->gpu_rendered_once = TRUE;
                    rendered = TRUE;
                    QueryPerformanceCounter( &t1 );
                    TRACE( "XAML perf (GPU): total_us=%lld (%ux%u)\n",
                           (long long)((t1.QuadPart - t0.QuadPart) * 1000000 / freq.QuadPart),
                           width, height );
                }
                else
                {
                    InterlockedExchange( &g_xaml_window_dirty, 1 );
                }
            }
            else if (impl->gpu_active)
            {
                gpu_ok = TRUE;
            }
        }
        if (!gpu_ok)
        {
            impl->gpu_active = FALSE;
            rendered = xaml_render_retained_if_dirty( impl, hwnd, width, height, &dirty_rc );
            QueryPerformanceCounter( &t1 );
            if (rendered && impl->swapchain && impl->d3d11_ctx && !xaml_get_app_caps()->winui3)
                xaml_upload_retained_to_swapchain( impl, &dirty_rc );
            QueryPerformanceCounter( &t2 );
            TRACE( "XAML perf (CPU): rendered=%d render_us=%lld upload_us=%lld dirty=(%ld,%ld-%ld,%ld)\n",
                   rendered,
                   (long long)((t1.QuadPart - t0.QuadPart) * 1000000 / freq.QuadPart),
                   (long long)((t2.QuadPart - t1.QuadPart) * 1000000 / freq.QuadPart),
                   dirty_rc.left, dirty_rc.top, dirty_rc.right, dirty_rc.bottom );
        }
    }

    /* 8. Commit the DirectComposition tree (skip when GPU swapchain presents directly to hwnd) */
    if (!impl->gpu_active || g_active_scp_swapchain)
        IDCompositionDesktopDevice_Commit( impl->dcomp_device );
    if (!impl->scp_timer_armed && IsWindow( hwnd ))
    {
        SetTimer( hwnd, XAML_SCP_DIRTY_TIMER_ID, 50, NULL );
        impl->scp_timer_armed = TRUE;
    }

    /* 9. Blit the retained DIB into the top-level window DC for UWP / non-DComp backing store.
     * On WinUI 3 with an active DComp tree, DComp composites child visuals (e.g.
     * ElementCompositionPreview) above the XAML visual; a direct GDI BitBlt would overwrite them. */
    if (impl->retained_valid && impl->retained_dc && !g_active_scp_swapchain && (!xaml_get_app_caps()->winui3 || !impl->dcomp_device))
    {
        HWND root = GetAncestor( hwnd, GA_ROOT );
        HDC hdc = root ? GetDC( root ) : NULL;
        if (hdc)
        {
            RECT rc;
            UINT blit_w = width, blit_h = height;
            if (root != hwnd && GetClientRect( root, &rc ))
            {
                blit_w = rc.right > 0 ? (UINT)rc.right : width;
                blit_h = rc.bottom > 0 ? (UINT)rc.bottom : height;
            }
            BitBlt( hdc, 0, 0, blit_w, blit_h, impl->retained_dc, 0, 0, SRCCOPY );
            ReleaseDC( root, hdc );
            InvalidateRect( root, NULL, FALSE );
            UpdateWindow( root );
        }
    }

    TRACE( "XAML: composited and presented visual tree for hwnd %p (%ux%u scale %.2f)\n", hwnd, width, height, scale );
    TRACE( "xaml_window_present done: hwnd %p (%ux%u)\n", hwnd, width, height );
    InterlockedExchange( &g_xaml_window_dirty, 0 );
    {
        RECT client_rc;
        UINT wv2_w = width, wv2_h = height;
        if (hwnd && IsWindow( hwnd ) && !IsIconic( hwnd ) &&
            GetClientRect( hwnd, &client_rc ) && client_rc.right > 0 && client_rc.bottom > 0)
        {
            wv2_w = (UINT)client_rc.right;
            wv2_h = (UINT)client_rc.bottom;
        }
        xaml_wv2_on_window_ready( hwnd, wv2_w, wv2_h );
    }
    g_in_xaml_present = FALSE;
    return S_OK;
}

void xaml_get_layout_metrics( HWND hwnd, SIZE *client, float *scale )
{
    RECT rc;
    UINT dpi = 0;

    if (client)
    {
        if (hwnd && IsWindow( hwnd ) && GetClientRect( hwnd, &rc ) && rc.right > 0 && rc.bottom > 0)
        {
            UINT wdpi = GetDpiForWindow( hwnd );
            if (wdpi > 96)
            {
                client->cx = MulDiv( rc.right, 96, (int)wdpi );
                client->cy = MulDiv( rc.bottom, 96, (int)wdpi );
            }
            else
            {
                client->cx = rc.right;
                client->cy = rc.bottom;
            }
        }
        else
        {
            HMONITOR mon = (hwnd && IsWindow( hwnd ))
                ? MonitorFromWindow( hwnd, MONITOR_DEFAULTTOPRIMARY )
                : MonitorFromPoint( (POINT){ 0, 0 }, MONITOR_DEFAULTTOPRIMARY );
            MONITORINFO mi = { .cbSize = sizeof(mi) };
            LONG w = 0, h = 0;

            if (mon && GetMonitorInfoW( mon, &mi ))
            {
                w = mi.rcWork.right - mi.rcWork.left;
                h = mi.rcWork.bottom - mi.rcWork.top;
            }
            if (w <= 0 || h <= 0)
            {
                w = GetSystemMetrics( SM_CXSCREEN );
                h = GetSystemMetrics( SM_CYSCREEN );
            }
            if (w > 0 && h > 0)
            {
                LONG cw = (LONG)(w * 0.8f + 0.5f);
                LONG ch = (LONG)(h * 0.8f + 0.5f);

                if (cw < 800) cw = 800;
                if (cw > 1920) cw = 1920;
                if (ch < 600) ch = 600;
                if (ch > 1920) ch = 1920;
                client->cx = cw;
                client->cy = ch;
            }
            else
            {
                *client = XAML_DEFAULT_CLIENT_SIZE;
            }
        }
    }

    if (scale)
    {
        HWND target = (hwnd && IsWindow( hwnd )) ? hwnd : xaml_get_window_hwnd();
        if (target && IsWindow( target )) dpi = GetDpiForWindow( target );
        if (!dpi) dpi = GetDpiForSystem();
        *scale = dpi ? (float)dpi / 96.0f : 1.0f;
    }
}

static BOOL CALLBACK xaml_find_process_window_cb( HWND hwnd, LPARAM lparam )
{
    DWORD pid = 0;
    if (!IsWindowVisible( hwnd )) return TRUE;
    if ((GetWindowLongW( hwnd, GWL_STYLE ) & WS_OVERLAPPEDWINDOW) != WS_OVERLAPPEDWINDOW) return TRUE;
    GetWindowThreadProcessId( hwnd, &pid );
    if (pid != GetCurrentProcessId()) return TRUE;
    *(HWND *)lparam = hwnd;
    return FALSE;
}

HWND xaml_get_window_hwnd( void )
{
    HWND hw = global_xaml_window.target_hwnd;
    if (hw && IsWindow( hw ))
    {
        HWND root = GetAncestor( hw, GA_ROOT );
        return root ? root : hw;
    }
    hw = FindWindowW( L"WinUIDesktopWin32WindowClass", NULL );
    if (hw) return hw;
    hw = xaml_find_core_window();
    if (hw)
    {
        HWND root = GetAncestor( hw, GA_ROOT );
        return root ? root : hw;
    }
    hw = NULL;
    EnumWindows( xaml_find_process_window_cb, (LPARAM)&hw );
    if (hw) return hw;
    return GetActiveWindow();
}

IDCompositionVisual *xaml_get_dcomp_root_visual( void )
{
    return (IDCompositionVisual *)global_xaml_window.dcomp_root_visual;
}

void xaml_dcomp_commit( void )
{
    if (global_xaml_window.dcomp_device)
        IDCompositionDesktopDevice_Commit( global_xaml_window.dcomp_device );
    else if (global_dcomp_device)
        IDCompositionDesktopDevice_Commit( global_dcomp_device );
}

HRESULT xaml_window_present_current( void )
{
    xaml_mark_window_dirty();
    return xaml_window_present( &global_xaml_window );
}

static HRESULT WINAPI xaml_window_put_Content( IInspectable *iface, void *a2, void *a3 )
{
    struct xaml_window *impl = &global_xaml_window;
    IInspectable *content = (IInspectable *)a2;

    if (!content) content = (IInspectable *)a3;
    TRACE( "iface %p, content %p\n", iface, content );
    if (impl->content) IInspectable_Release( impl->content );
    impl->content = content;
    if (impl->content)
    {
        uwp_mark_in_visual_tree( impl->content );
        IInspectable_AddRef( impl->content );
    }
    if (impl->is_active && impl->content) uwp_fire_loaded_content( impl->content );
    xaml_mark_window_dirty();
    xaml_window_present( impl );
    return S_OK;
}

static HRESULT WINAPI xaml_window_get_CoreWindow( IInspectable *iface, void *a2, void *a3 )
{
    struct xaml_window *impl = &global_xaml_window;
    ICoreWindow **value = (ICoreWindow **)a2;

    if (!value) value = (ICoreWindow **)a3;
    TRACE( "iface %p, value %p\n", iface, value );
    if (!impl->core_window)
    {
        ICoreWindow *cw = get_current_core_window();
        if (cw)
        {
            impl->core_window = wrap_core_window( cw );
            if (impl->core_window != cw) ICoreWindow_Release( cw );
        }
    }
    if (value)
    {
        *value = impl->core_window;
        if (*value) ICoreWindow_AddRef( *value );
    }
    return S_OK;
}

static HRESULT WINAPI xaml_window_get_Dispatcher( IInspectable *iface, void *a2, void *a3 )
{
    struct xaml_window *impl = &global_xaml_window;
    ICoreDispatcher **value = (ICoreDispatcher **)a2;
    ICoreWindow *cw = NULL;
    HRESULT hr = S_OK;

    if (!value) value = (ICoreDispatcher **)a3;
    TRACE( "iface %p, value %p\n", iface, value );
    if (!impl->core_window) impl->core_window = get_current_core_window();
    cw = impl->core_window;
    if (cw && value)
    {
        hr = ICoreWindow_get_Dispatcher( cw, value );
    }
    else if (value) *value = NULL;
    return hr;
}

static const GUID IID_IWindowActivatedEventArgs_app =
    { 0x179d65e7, 0x4658, 0x4cb6, { 0xaa, 0x13, 0x41, 0xd0, 0x94, 0xea, 0x25, 0x5e } };
/* Microsoft.UI.Xaml.IWindowActivatedEventArgs (WinUI 3 / Windows App SDK). */
static const GUID IID_IWindowActivatedEventArgs_winui3 =
    { 0xc723a5ea, 0x82c4, 0x5dd6, { 0x86, 0x1b, 0x70, 0xef, 0x57, 0x3b, 0x88, 0xd6 } };

static HRESULT WINAPI dummy_wa_noop( void *iface, void *a1, void *a2 )
{
    if (a1) *(void **)a1 = NULL;
    if (a2) *(void **)a2 = NULL;
    return S_OK;
}

static ULONG WINAPI dummy_wa_AddRef( void *iface ) { return 2; }
static ULONG WINAPI dummy_wa_Release( void *iface ) { return 1; }

static const void *dummy_wa_app_vtbl[64];
static const void *dummy_wa_winui3_vtbl[64];
static struct { const void **vtbl; } dummy_wa_app_obj = { dummy_wa_app_vtbl };
static struct { const void **vtbl; } dummy_wa_winui3_obj = { dummy_wa_winui3_vtbl };

static HRESULT WINAPI dummy_wa_QueryInterface( void *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    TRACE( "dummy_wa_QueryInterface iid %s\n", debugstr_guid( iid ) );
    if (IsEqualGUID( iid, &IID_IWindowActivatedEventArgs_app ))
    {
        *out = &dummy_wa_app_obj;
        dummy_wa_AddRef( *out );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IWindowActivatedEventArgs_winui3 ))
    {
        *out = &dummy_wa_winui3_obj;
        dummy_wa_AddRef( *out );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ))
    {
        *out = iface;
        dummy_wa_AddRef( iface );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static HRESULT WINAPI dummy_wa_GetIids( void *iface, ULONG *count, IID **iids )
{
    IID *array;

    if (!count || !iids) return E_POINTER;
    array = CoTaskMemAlloc( sizeof(IID) * 2 );
    if (!array) return E_OUTOFMEMORY;
    array[0] = IID_IWindowActivatedEventArgs_winui3;
    array[1] = IID_IWindowActivatedEventArgs_app;
    *count = 2;
    *iids = array;
    return S_OK;
}
static HRESULT WINAPI dummy_wa_app_GetRuntimeClassName( void *iface, HSTRING *name )
{
    static const WCHAR cls[] = L"Windows.UI.Core.WindowActivatedEventArgs";
    return WindowsCreateString( cls, ARRAY_SIZE(cls) - 1, name );
}
static HRESULT WINAPI dummy_wa_winui3_GetRuntimeClassName( void *iface, HSTRING *name )
{
    static const WCHAR cls[] = L"Microsoft.UI.Xaml.WindowActivatedEventArgs";
    return WindowsCreateString( cls, ARRAY_SIZE(cls) - 1, name );
}
static HRESULT WINAPI dummy_wa_GetTrustLevel( void *iface, TrustLevel *level ) { if (level) *level = BaseTrust; return S_OK; }
static HRESULT WINAPI dummy_wa_get_Handled( void *iface, boolean *value )
{
    if (value) *value = FALSE;
    return S_OK;
}
static HRESULT WINAPI dummy_wa_put_Handled( void *iface, boolean value )
{
    return S_OK;
}
static HRESULT WINAPI dummy_wa_get_WindowActivationState( void *iface, int *value )
{
    if (value) *value = 0; /* CodeActivated */
    return S_OK;
}
/* UWP Windows.UI.Core.IWindowActivatedEventArgs layout:
 * slot 6 get_WindowActivationState, slot 7 get_Handled, slot 8 put_Handled. */
static const void *dummy_wa_app_vtbl[64] =
{
    [0] = dummy_wa_QueryInterface,
    [1] = dummy_wa_AddRef,
    [2] = dummy_wa_Release,
    [3] = dummy_wa_GetIids,
    [4] = dummy_wa_app_GetRuntimeClassName,
    [5] = dummy_wa_GetTrustLevel,
    [6] = dummy_wa_get_WindowActivationState,
    [7] = dummy_wa_get_Handled,
    [8] = dummy_wa_put_Handled,
    [9 ... 63] = dummy_wa_noop,
};
/* WinUI 3 Microsoft.UI.Xaml.IWindowActivatedEventArgs layout:
 * slot 6 get_Handled, slot 7 put_Handled, slot 8 get_WindowActivationState. */
static const void *dummy_wa_winui3_vtbl[64] =
{
    [0] = dummy_wa_QueryInterface,
    [1] = dummy_wa_AddRef,
    [2] = dummy_wa_Release,
    [3] = dummy_wa_GetIids,
    [4] = dummy_wa_winui3_GetRuntimeClassName,
    [5] = dummy_wa_GetTrustLevel,
    [6] = dummy_wa_get_Handled,
    [7] = dummy_wa_put_Handled,
    [8] = dummy_wa_get_WindowActivationState,
    [9 ... 63] = dummy_wa_noop,
};
#define dummy_wa_obj dummy_wa_app_obj

static HRESULT WINAPI xaml_window_add_Activated( IInspectable *iface, void *handler, EventRegistrationToken *token )
{
    struct xaml_window *impl = &global_xaml_window;
    TRACE( "iface %p, handler %p\n", iface, handler );
    if (impl->activated_handler_count < (int)ARRAY_SIZE(impl->activated_handlers))
    {
        impl->activated_handlers[impl->activated_handler_count++] = handler;
        if (handler) IUnknown_AddRef( (IUnknown *)handler );
    }
    if (token) token->value = impl->activated_handler_count;
    if (impl->is_active && handler)
    {
        const struct abi_typed_event_handler *eh = (const struct abi_typed_event_handler *)handler;
        TRACE( "window already active: immediately invoking Window.Activated handler %p\n", handler );
        eh->lpVtbl->Invoke( handler, global_window_outer ? global_window_outer : &impl->IInspectable_iface, (void *)&dummy_wa_obj );
    }
    return S_OK;
}

static HRESULT WINAPI xaml_window_remove_Activated( IInspectable *iface, EventRegistrationToken token )
{
    return S_OK;
}

static HRESULT WINAPI xaml_window_add_Closed( IInspectable *iface, void *handler, EventRegistrationToken *token )
{
    TRACE( "iface %p, handler %p\n", iface, handler );
    if (token) token->value = 1;
    return S_OK;
}

static HRESULT WINAPI xaml_window_remove_Closed( IInspectable *iface, EventRegistrationToken token )
{
    return S_OK;
}

static HRESULT WINAPI xaml_window_add_SizeChanged( IInspectable *iface, void *handler, EventRegistrationToken *token )
{
    TRACE( "iface %p, handler %p\n", iface, handler );
    if (token) token->value = 1;
    return S_OK;
}

static HRESULT WINAPI xaml_window_remove_SizeChanged( IInspectable *iface, EventRegistrationToken token )
{
    return S_OK;
}

static const GUID IID_IVisibilityChangedEventArgs_app =
    { 0xbf9918ea, 0xd801, 0x4564, { 0xa4, 0x95, 0xb1, 0xe8, 0x4f, 0x8a, 0xd0, 0x85 } };
/* Microsoft.UI.Xaml.IWindowVisibilityChangedEventArgs (WinUI 3 / Windows App SDK). */
static const GUID IID_IWindowVisibilityChangedEventArgs_winui3 =
    { 0x7bb24a6d, 0x070c, 0x5cb6, { 0x8e, 0x9c, 0x54, 0x79, 0x05, 0xbe, 0x82, 0x65 } };

static ULONG WINAPI dummy_vis_AddRef( void *iface ) { return 2; }
static ULONG WINAPI dummy_vis_Release( void *iface ) { return 1; }

static HRESULT WINAPI dummy_vis_QueryInterface( void *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    TRACE( "dummy_vis_QueryInterface iid %s\n", debugstr_guid( iid ) );
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IVisibilityChangedEventArgs_app ) ||
        IsEqualGUID( iid, &IID_IWindowVisibilityChangedEventArgs_winui3 ))
    {
        *out = iface;
        dummy_vis_AddRef( iface );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static HRESULT WINAPI dummy_vis_GetIids( void *iface, ULONG *c, IID **i )
{
    IID *array;

    if (!c || !i) return E_POINTER;
    array = CoTaskMemAlloc( sizeof(IID) * 2 );
    if (!array) return E_OUTOFMEMORY;
    array[0] = IID_IWindowVisibilityChangedEventArgs_winui3;
    array[1] = IID_IVisibilityChangedEventArgs_app;
    *c = 2;
    *i = array;
    return S_OK;
}
static HRESULT WINAPI dummy_vis_GetRuntimeClassName( void *iface, HSTRING *n )
{
    static const WCHAR cls[] = L"Microsoft.UI.Xaml.WindowVisibilityChangedEventArgs";
    return WindowsCreateString( cls, ARRAY_SIZE(cls) - 1, n );
}
static HRESULT WINAPI dummy_vis_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI dummy_vis_get_Handled( void *iface, boolean *v ) { if (v) *v = FALSE; return S_OK; }
static HRESULT WINAPI dummy_vis_put_Handled( void *iface, boolean v ) { return S_OK; }
static HRESULT WINAPI dummy_vis_get_Visible( void *iface, boolean *v ) { if (v) *v = TRUE; return S_OK; }
static const void *dummy_vis_vtbl[] =
{
    dummy_vis_QueryInterface,
    dummy_vis_AddRef,
    dummy_vis_Release,
    dummy_vis_GetIids,
    dummy_vis_GetRuntimeClassName,
    dummy_vis_GetTrustLevel,
    dummy_vis_get_Handled,
    dummy_vis_put_Handled,
    dummy_vis_get_Visible,
};
static struct { const void **vtbl; } dummy_vis_obj = { dummy_vis_vtbl };

static HRESULT WINAPI xaml_window_add_VisibilityChanged( IInspectable *iface, void *handler, EventRegistrationToken *token )
{
    struct xaml_window *impl = &global_xaml_window;
    TRACE( "iface %p, handler %p\n", iface, handler );
    if (impl->visibility_handler_count < (int)ARRAY_SIZE(impl->visibility_handlers))
    {
        impl->visibility_handlers[impl->visibility_handler_count++] = handler;
        if (handler) IUnknown_AddRef( (IUnknown *)handler );
    }
    if (token) token->value = impl->visibility_handler_count;
    return S_OK;
}

static HRESULT WINAPI xaml_window_remove_VisibilityChanged( IInspectable *iface, EventRegistrationToken token )
{
    return S_OK;
}

static HRESULT WINAPI xaml_window_Activate( IInspectable *iface )
{
    struct xaml_window *impl = &global_xaml_window;
    ICoreWindow *cw = NULL;
    HWND hwnd = NULL;
    int i;
    TRACE( "iface %p\n", iface );
    cw = ensure_core_window( impl );
    if (cw)
    {
        ICoreWindowInterop *interop = NULL;
        ICoreWindow_Activate( cw );
        if (SUCCEEDED( ICoreWindow_QueryInterface( cw, &IID_ICoreWindowInterop, (void **)&interop ) ))
        {
            interop->lpVtbl->GetWindowHandle( interop, &hwnd );
            interop->lpVtbl->Release( interop );
        }
        if (!hwnd) hwnd = xaml_find_core_window();
        if (!hwnd) hwnd = GetActiveWindow();
        if (hwnd)
        {
            HWND top = GetAncestor( hwnd, GA_ROOT );
            if (!top) top = hwnd;
            xaml_apply_window_icon( top );
            ShowWindow( hwnd, SW_SHOW );
            InvalidateRect( hwnd, NULL, TRUE );
        }
    }
    impl->is_active = TRUE;
    for (i = 0; i < impl->visibility_handler_count; i++)
    {
        void *h = impl->visibility_handlers[i];
        if (h)
        {
            const struct abi_typed_event_handler *eh = (const struct abi_typed_event_handler *)h;
            TRACE( "invoking Window.VisibilityChanged handler %p\n", h );
            __TRY
            {
                eh->lpVtbl->Invoke( h, global_window_outer ? global_window_outer : &impl->IInspectable_iface, (void *)&dummy_vis_obj );
            }
            __EXCEPT_ALL
            {
                TRACE( "Window.VisibilityChanged handler %p faulted\n", h );
            }
            __ENDTRY;
        }
    }
    for (i = 0; i < impl->activated_handler_count; i++)
    {
        void *h = impl->activated_handlers[i];
        if (h)
        {
            const struct abi_typed_event_handler *eh = (const struct abi_typed_event_handler *)h;
            TRACE( "invoking Window.Activated handler %p\n", h );
            __TRY
            {
                eh->lpVtbl->Invoke( h, global_window_outer ? global_window_outer : &impl->IInspectable_iface, (void *)&dummy_wa_obj );
            }
            __EXCEPT_ALL
            {
                TRACE( "Window.Activated handler %p faulted\n", h );
            }
            __ENDTRY;
        }
    }
    if (impl->content) uwp_fire_loaded_content( impl->content );
    xaml_window_present( impl );
    return S_OK;
}

static HRESULT WINAPI xaml_window_Close( IInspectable *iface )
{
    struct xaml_window *impl = &global_xaml_window;
    ICoreWindow *cw = NULL;
    TRACE( "iface %p\n", iface );
    if (!impl->core_window) impl->core_window = get_current_core_window();
    cw = impl->core_window;
    if (cw) ICoreWindow_Close( cw );
    return S_OK;
}

struct xaml_window_vtbl_type
{
    /* 0..5: IInspectable */
    HRESULT (WINAPI *QueryInterface)(IInspectable *, REFIID, void **);
    ULONG (WINAPI *AddRef)(IInspectable *);
    ULONG (WINAPI *Release)(IInspectable *);
    HRESULT (WINAPI *GetIids)(IInspectable *, ULONG *, IID **);
    HRESULT (WINAPI *GetRuntimeClassName)(IInspectable *, HSTRING *);
    HRESULT (WINAPI *GetTrustLevel)(IInspectable *, TrustLevel *);
    /* 6..21: IWindow */
    HRESULT (WINAPI *get_Bounds)(IInspectable *, void *, void *);
    HRESULT (WINAPI *get_Visible)(IInspectable *, void *, void *);
    HRESULT (WINAPI *get_Content)(IInspectable *, void *, void *);
    HRESULT (WINAPI *put_Content)(IInspectable *, void *, void *);
    HRESULT (WINAPI *get_CoreWindow)(IInspectable *, void *, void *);
    HRESULT (WINAPI *get_Dispatcher)(IInspectable *, void *, void *);
    HRESULT (WINAPI *add_Activated)(IInspectable *, void *, EventRegistrationToken *);
    HRESULT (WINAPI *remove_Activated)(IInspectable *, EventRegistrationToken);
    HRESULT (WINAPI *add_Closed)(IInspectable *, void *, EventRegistrationToken *);
    HRESULT (WINAPI *remove_Closed)(IInspectable *, EventRegistrationToken);
    HRESULT (WINAPI *add_SizeChanged)(IInspectable *, void *, EventRegistrationToken *);
    HRESULT (WINAPI *remove_SizeChanged)(IInspectable *, EventRegistrationToken);
    HRESULT (WINAPI *add_VisibilityChanged)(IInspectable *, void *, EventRegistrationToken *);
    HRESULT (WINAPI *remove_VisibilityChanged)(IInspectable *, EventRegistrationToken);
    HRESULT (WINAPI *Activate)(IInspectable *);
    HRESULT (WINAPI *Close)(IInspectable *);
    void *pad[234];    /* full WinUI Window projection: Loaded = 130/131, extended props beyond */
};

static HRESULT WINAPI xaml_window_zero_out( void *iface, void *a2, void *a3 );

static HRESULT WINAPI xaml_window_add_Loaded( void *iface, void *handler, EventRegistrationToken *token )
{
    TRACE( "xaml_window_add_Loaded iface %p handler %p\n", iface, handler );
    if (token) token->value = 1;
    xaml_register_loaded( handler, (IInspectable *)iface );
    return S_OK;
}

static HRESULT WINAPI xaml_window_remove_Loaded( void *iface, EventRegistrationToken token )
{
    return S_OK;
}

static const struct xaml_window_vtbl_type xaml_window_vtbl =
{
    xaml_window_QueryInterface,
    xaml_window_AddRef,
    xaml_window_Release,
    xaml_window_GetIids,
    xaml_window_GetRuntimeClassName,
    xaml_window_GetTrustLevel,
    xaml_window_get_Bounds,
    xaml_window_get_Visible,
    xaml_window_get_Content,
    xaml_window_put_Content,
    xaml_window_get_CoreWindow,
    xaml_window_get_Dispatcher,
    xaml_window_add_Activated,
    xaml_window_remove_Activated,
    xaml_window_add_Closed,
    xaml_window_remove_Closed,
    xaml_window_add_SizeChanged,
    xaml_window_remove_SizeChanged,
    xaml_window_add_VisibilityChanged,
    xaml_window_remove_VisibilityChanged,
    xaml_window_Activate,
    xaml_window_Close,
    {
        [0 ... 42] = (void *)xaml_window_zero_out,
        [75 - 32] = (void *)xaml_window_add_Loaded,
        [76 - 32] = (void *)xaml_window_remove_Loaded,
        [45 ... 97] = (void *)xaml_window_zero_out,
        [130 - 32] = (void *)xaml_window_add_Loaded,
        [131 - 32] = (void *)xaml_window_remove_Loaded,
        [100 ... 233] = (void *)xaml_window_zero_out,
    }
};

static HRESULT WINAPI xaml_window2_QueryInterface( IInspectable *iface, REFIID iid, void **out )
{
    struct xaml_window *impl = impl_from_IWindow2( iface );
    return xaml_window_QueryInterface( &impl->IInspectable_iface, iid, out );
}
static ULONG WINAPI xaml_window2_AddRef( IInspectable *iface )
{
    struct xaml_window *impl = impl_from_IWindow2( iface );
    return xaml_window_AddRef( &impl->IInspectable_iface );
}
static ULONG WINAPI xaml_window2_Release( IInspectable *iface )
{
    struct xaml_window *impl = impl_from_IWindow2( iface );
    return xaml_window_Release( &impl->IInspectable_iface );
}
static HRESULT WINAPI xaml_window2_GetIids( IInspectable *iface, ULONG *count, IID **iids )
{
    if (!count || !iids) return E_POINTER;
    *count = 0;
    *iids = NULL;
    return S_OK;
}
static HRESULT WINAPI xaml_window2_GetRuntimeClassName( IInspectable *iface, HSTRING *name )
{
    const WCHAR *cls = xaml_get_app_caps()->winui3 ? L"Microsoft.UI.Xaml.Window" : L"Windows.UI.Xaml.Window";
    TRACE( "xaml_window2_GetRuntimeClassName on %p -> %s\n", iface, debugstr_w(cls) );
    return WindowsCreateString( cls, wcslen( cls ), name );
}
static HRESULT WINAPI xaml_window2_GetTrustLevel( IInspectable *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}
static HRESULT WINAPI xaml_window2_get_Compositor( void *iface, void **value )
{
    static const WCHAR name[] = L"Windows.UI.Composition.Compositor";
    HSTRING hstr;
    HRESULT hr;

    TRACE( "iface %p, value %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = NULL;
    hr = WindowsCreateString( name, ARRAY_SIZE(name) - 1, &hstr );
    if (FAILED(hr)) return hr;
    hr = RoActivateInstance( hstr, (IInspectable **)value );
    WindowsDeleteString( hstr );
    return hr;
}
static HRESULT WINAPI xaml_window_zero_out( void *iface, void *a2, void *a3 )
{
    TRACE( "iface %p, a2 %p, a3 %p\n", iface, a2, a3 );
    if ((ULONG_PTR)a3 >= 0x10000 && !IsBadWritePtr( a3, sizeof(void *) )) *(void **)a3 = NULL;
    else if ((ULONG_PTR)a2 >= 0x10000 && !IsBadWritePtr( a2, sizeof(void *) )) *(void **)a2 = NULL;
    return S_OK;
}

static HRESULT WINAPI xaml_window2_get_SystemBackdrop( void *iface, void **value )
{
    TRACE( "iface %p, value %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = NULL;
    return S_OK;
}

static HRESULT WINAPI xaml_window2_set_SystemBackdrop( void *iface, void *value )
{
    TRACE( "iface %p, value %p\n", iface, value );
    return S_OK;
}

static HRESULT WINAPI xaml_window3_get_DispatcherQueue( void *iface, void **value )
{
    static const WCHAR name[] = L"Windows.System.DispatcherQueue";
    HSTRING hstr;
    IActivationFactory *af = NULL;
    HRESULT hr;

    if (!value) return E_POINTER;
    *value = NULL;
    hr = WindowsCreateString( name, ARRAY_SIZE(name) - 1, &hstr );
    if (FAILED(hr)) return hr;
    hr = RoGetActivationFactory( hstr, &IID_IActivationFactory, (void **)&af );
    WindowsDeleteString( hstr );
    if (SUCCEEDED(hr) && af)
    {
        const struct abi_dispatcher_queue_statics *dqs = (const struct abi_dispatcher_queue_statics *)af;
        hr = dqs->lpVtbl->GetForCurrentThread( af, value );
        IActivationFactory_Release( af );
    }
    TRACE( "IWindow3::get_DispatcherQueue -> hr %#lx, value %p\n", hr, *value );
    return hr;
}

static HRESULT WINAPI xaml_window3_QueryInterface( IInspectable *iface, REFIID iid, void **out )
{
    return xaml_window_QueryInterface( &global_xaml_window.IInspectable_iface, iid, out );
}
static ULONG WINAPI xaml_window3_AddRef( IInspectable *iface )
{
    return xaml_window_AddRef( &global_xaml_window.IInspectable_iface );
}
static ULONG WINAPI xaml_window3_Release( IInspectable *iface )
{
    return xaml_window_Release( &global_xaml_window.IInspectable_iface );
}

static const void *xaml_window3_vtbl[] =
{
    xaml_window3_QueryInterface,     /* 0 */
    xaml_window3_AddRef,             /* 1 */
    xaml_window3_Release,            /* 2 */
    xaml_window2_GetIids,            /* 3 */
    xaml_window2_GetRuntimeClassName,/* 4 */
    xaml_window2_GetTrustLevel,      /* 5 */
    xaml_window2_get_Compositor,     /* 6: slot 6 (0x30) is get_Compositor! */
    xaml_window3_get_DispatcherQueue,/* 7: get_DispatcherQueue */
    xaml_window_zero_out, xaml_window_zero_out, xaml_window_zero_out, xaml_window_zero_out, /* 8..11 */
    xaml_window_zero_out, xaml_window_zero_out, xaml_window_zero_out, xaml_window_zero_out, /* 12..15 */
};

static HRESULT WINAPI xaml_window_setter_noop( void *iface, void *a2 );
static const void *uwp_xaml_window2_vtbl[] =
{
    xaml_window3_QueryInterface,     /* 0 */
    xaml_window3_AddRef,             /* 1 */
    xaml_window3_Release,            /* 2 */
    xaml_window2_GetIids,            /* 3 */
    xaml_window2_GetRuntimeClassName,/* 4 */
    xaml_window2_GetTrustLevel,      /* 5 */
    xaml_window_setter_noop,         /* 6: SetTitleBar */
    xaml_window_zero_out, xaml_window_zero_out, xaml_window_zero_out, xaml_window_zero_out,
};

static const void *xaml_window2_vtbl[] =
{
    xaml_window2_QueryInterface,     /* 0 */
    xaml_window2_AddRef,             /* 1 */
    xaml_window2_Release,            /* 2 */
    xaml_window2_GetIids,            /* 3 */
    xaml_window2_GetRuntimeClassName,/* 4 */
    xaml_window2_GetTrustLevel,      /* 5 */
    xaml_window2_get_Compositor,     /* 6 */
    xaml_window_zero_out, xaml_window_zero_out, xaml_window_zero_out, xaml_window_zero_out, /* 7..10 */
    xaml_window_zero_out, xaml_window_zero_out, xaml_window_zero_out, xaml_window_zero_out, /* 11..14 */
    xaml_window_zero_out, xaml_window_zero_out, xaml_window_zero_out, xaml_window_zero_out, /* 15..18 */
    xaml_window_zero_out, xaml_window_zero_out, xaml_window_zero_out, xaml_window_zero_out, /* 19..22 */
    xaml_window_zero_out, xaml_window_zero_out, xaml_window_zero_out, xaml_window_zero_out, /* 23..26 */
    xaml_window_zero_out, xaml_window_zero_out, xaml_window_zero_out, xaml_window_zero_out, /* 27..30 */
    xaml_window_zero_out, xaml_window_zero_out, xaml_window_zero_out, xaml_window_zero_out, /* 31..34 */
    xaml_window_zero_out, xaml_window_zero_out, xaml_window_zero_out, xaml_window_zero_out, /* 35..38 */
    xaml_window_zero_out, xaml_window_zero_out, xaml_window_zero_out, xaml_window_zero_out, /* 39..42 */
    xaml_window_zero_out, xaml_window_zero_out, xaml_window_zero_out, xaml_window_zero_out, /* 43..46 */
    xaml_window_zero_out, xaml_window_zero_out, xaml_window_zero_out, xaml_window_zero_out, /* 47..50 */
    xaml_window_zero_out, xaml_window_zero_out, xaml_window_zero_out, xaml_window_zero_out, /* 51..54 */
    xaml_window_zero_out, xaml_window_zero_out, xaml_window_zero_out, xaml_window_zero_out, /* 55..58 */
    xaml_window_zero_out, xaml_window_zero_out, xaml_window_zero_out, xaml_window_zero_out, /* 59..62 */
    xaml_window_zero_out                                                                    /* 63 */
};

static inline struct xaml_window *impl_from_IWindowPrivate( void *iface )
{
    return CONTAINING_RECORD( iface, struct xaml_window, IWindowPrivate_iface );
}
static HRESULT WINAPI xaml_window_private_QueryInterface( IInspectable *iface, REFIID iid, void **out )
{
    struct xaml_window *impl = impl_from_IWindowPrivate( iface );
    return xaml_window_QueryInterface( &impl->IInspectable_iface, iid, out );
}
static ULONG WINAPI xaml_window_private_AddRef( IInspectable *iface )
{
    struct xaml_window *impl = impl_from_IWindowPrivate( iface );
    return xaml_window_AddRef( &impl->IInspectable_iface );
}
static ULONG WINAPI xaml_window_private_Release( IInspectable *iface )
{
    struct xaml_window *impl = impl_from_IWindowPrivate( iface );
    return xaml_window_Release( &impl->IInspectable_iface );
}
static HRESULT WINAPI xaml_window_private_get_TransparentBackground( void *iface, boolean *value )
{
    if (value) *value = FALSE;
    return S_OK;
}
static HRESULT WINAPI xaml_window_private_put_TransparentBackground( void *iface, boolean value )
{
    TRACE( "iface %p, value %d\n", iface, value );
    return S_OK;
}
static const void *xaml_window_private_vtbl[24] =
{
    [0] = xaml_window_private_QueryInterface,
    [1] = xaml_window_private_AddRef,
    [2] = xaml_window_private_Release,
    [3] = xaml_window2_GetIids,
    [4] = xaml_window2_GetRuntimeClassName,
    [5] = xaml_window2_GetTrustLevel,
    [6] = xaml_window_private_get_TransparentBackground,
    [7] = xaml_window_private_put_TransparentBackground,
    [8 ... 23] = xaml_window_zero_out,
};

static inline struct xaml_window *impl_from_ms_IWindow( void *iface )
{
    return CONTAINING_RECORD( iface, struct xaml_window, ms_IWindow_iface );
}
static HRESULT WINAPI ms_xaml_window_QueryInterface( IInspectable *iface, REFIID iid, void **out )
{
    struct xaml_window *impl = impl_from_ms_IWindow( iface );
    return xaml_window_QueryInterface( &impl->IInspectable_iface, iid, out );
}
static ULONG WINAPI ms_xaml_window_AddRef( IInspectable *iface )
{
    struct xaml_window *impl = impl_from_ms_IWindow( iface );
    return xaml_window_AddRef( &impl->IInspectable_iface );
}
static ULONG WINAPI ms_xaml_window_Release( IInspectable *iface )
{
    struct xaml_window *impl = impl_from_ms_IWindow( iface );
    return xaml_window_Release( &impl->IInspectable_iface );
}

static HRESULT WINAPI xaml_window_setter_noop( void *iface, void *a2 )
{
    TRACE( "iface %p, a2 %p\n", iface, a2 );
    return S_OK;
}

static HRESULT WINAPI ms_xaml_window_get_DispatcherQueue( void *iface, void **value )
{
    static const WCHAR name[] = L"Microsoft.UI.Dispatching.DispatcherQueue";
    HSTRING hstr;
    IActivationFactory *af = NULL;
    HRESULT hr;

    if (!value) return E_POINTER;
    *value = NULL;

    hr = WindowsCreateString( name, ARRAY_SIZE(name) - 1, &hstr );
    if (FAILED(hr)) return hr;
    hr = RoGetActivationFactory( hstr, &IID_IActivationFactory, (void **)&af );
    WindowsDeleteString( hstr );
    if (SUCCEEDED(hr) && af)
    {
        const struct abi_dispatcher_queue_statics *dqs = (const struct abi_dispatcher_queue_statics *)af;
        hr = dqs->lpVtbl->GetForCurrentThread( af, value );
        IActivationFactory_Release( af );
    }
    TRACE( "get_DispatcherQueue -> hr %#lx, value %p\n", hr, *value );
    return hr;
}

static HRESULT WINAPI ms_xaml_window_get_Title( void *iface, HSTRING *value )
{
    const WCHAR *name;

    if (!value) return E_POINTER;
    if (xaml_explicit_title[0])
        return WindowsCreateString( xaml_explicit_title, lstrlenW( xaml_explicit_title ), value );
    if ((name = xaml_package_display_name()))
        return WindowsCreateString( name, wcslen( name ), value );
    return WindowsCreateString( NULL, 0, value );
}

static HRESULT WINAPI ms_xaml_window_set_Title( void *iface, HSTRING value )
{
    const WCHAR *s = value ? WindowsGetStringRawBuffer( value, NULL ) : NULL;

    if (s && *s)
    {
        HWND top = xaml_get_window_hwnd();
        lstrcpynW( xaml_explicit_title, s, ARRAY_SIZE(xaml_explicit_title) );
        if (top) SetWindowTextW( top, xaml_explicit_title );
    }
    else
        xaml_explicit_title[0] = 0;
    return S_OK;
}

static HRESULT WINAPI ms_xaml_window_get_ExtendsContentIntoTitleBar( void *iface, boolean *value )
{
    if (!value) return E_POINTER;
    *value = TRUE;
    return S_OK;
}

static const void *ms_xaml_window_vtbl[] =
{
    ms_xaml_window_QueryInterface,        /* 0: QI */
    ms_xaml_window_AddRef,                /* 1: AddRef */
    ms_xaml_window_Release,               /* 2: Release */
    xaml_window2_GetIids,                 /* 3: GetIids */
    xaml_window2_GetRuntimeClassName,     /* 4: GetRuntimeClassName */
    xaml_window2_GetTrustLevel,           /* 5: GetTrustLevel */
    xaml_window_get_Bounds,               /* 6: get_Bounds */
    xaml_window_get_Visible,              /* 7: get_Visible */
    xaml_window_get_Content,              /* 8: get_Content */
    xaml_window_put_Content,              /* 9: set_Content */
    xaml_window_get_CoreWindow,           /* 10: get_CoreWindow */
    xaml_window2_get_Compositor,          /* 11: get_Compositor */
    xaml_window_get_Dispatcher,           /* 12: get_Dispatcher */
    ms_xaml_window_get_DispatcherQueue,   /* 13: get_DispatcherQueue */
    ms_xaml_window_get_Title,             /* 14: get_Title */
    ms_xaml_window_set_Title,             /* 15: set_Title */
    ms_xaml_window_get_ExtendsContentIntoTitleBar, /* 16: get_ExtendsContentIntoTitleBar */
    xaml_window_setter_noop,              /* 17: set_ExtendsContentIntoTitleBar */
    xaml_window_add_Activated,            /* 18: add_Activated */
    xaml_window_remove_Activated,         /* 19: remove_Activated */
    xaml_window_add_Closed,               /* 20: add_Closed */
    xaml_window_remove_Closed,            /* 21: remove_Closed */
    xaml_window_add_SizeChanged,          /* 22: add_SizeChanged */
    xaml_window_remove_SizeChanged,       /* 23: remove_SizeChanged */
    xaml_window_add_VisibilityChanged,    /* 24: add_VisibilityChanged */
    xaml_window_remove_VisibilityChanged, /* 25: remove_VisibilityChanged */
    xaml_window_Activate,                 /* 26: Activate */
    xaml_window_Close,                    /* 27: Close */
    xaml_window_setter_noop,              /* 28: SetTitleBar */
    xaml_window_zero_out, xaml_window_zero_out, xaml_window_zero_out, xaml_window_zero_out,
};

static inline struct xaml_window *impl_from_ms_IWindow2( void *iface )
{
    return CONTAINING_RECORD( iface, struct xaml_window, ms_IWindow2_iface );
}
static HRESULT WINAPI ms_xaml_window2_QueryInterface( IInspectable *iface, REFIID iid, void **out )
{
    struct xaml_window *impl = impl_from_ms_IWindow2( iface );
    return xaml_window_QueryInterface( &impl->IInspectable_iface, iid, out );
}
static ULONG WINAPI ms_xaml_window2_AddRef( IInspectable *iface )
{
    struct xaml_window *impl = impl_from_ms_IWindow2( iface );
    return xaml_window_AddRef( &impl->IInspectable_iface );
}
static ULONG WINAPI ms_xaml_window2_Release( IInspectable *iface )
{
    struct xaml_window *impl = impl_from_ms_IWindow2( iface );
    return xaml_window_Release( &impl->IInspectable_iface );
}

static HRESULT WINAPI ms_xaml_window2_get_AppWindow( void *iface, void **value )
{
    struct xaml_window *impl = impl_from_ms_IWindow2( iface );
    HWND hwnd = NULL;
    HSTRING class_name = NULL;
    IActivationFactory *af = NULL;
    void *statics = NULL;
    static const GUID IID_IAppWindowStatics =
        { 0x3c315c24, 0xd540, 0x5d72, { 0xb5, 0x18, 0xb2, 0x26, 0xb8, 0x36, 0x27, 0xcb } };
    HRESULT hr;

    if (!value) return E_POINTER;
    *value = NULL;

    if (!impl->core_window) impl->core_window = get_current_core_window();
    if (impl->core_window)
    {
        ICoreWindowInterop *interop = NULL;
        if (SUCCEEDED(ICoreWindow_QueryInterface( impl->core_window, &IID_ICoreWindowInterop, (void **)&interop )))
        {
            interop->lpVtbl->GetWindowHandle( interop, &hwnd );
            interop->lpVtbl->Release( interop );
        }
    }

    if (!hwnd && impl->target_hwnd && IsWindow( impl->target_hwnd )) hwnd = impl->target_hwnd;
    if (!hwnd) hwnd = xaml_find_core_window();
    if (!hwnd) hwnd = FindWindowW( L"Window", NULL );
    if (!hwnd) hwnd = GetActiveWindow();
    if (!hwnd) hwnd = GetForegroundWindow();
    if (!hwnd) return E_FAIL;

    hr = WindowsCreateString( L"Microsoft.UI.Windowing.AppWindow", 32, &class_name );
    if (FAILED(hr)) return hr;

    hr = RoGetActivationFactory( class_name, &IID_IActivationFactory, (void **)&af );
    WindowsDeleteString( class_name );
    if (FAILED(hr) || !af) return hr;

    hr = IActivationFactory_QueryInterface( af, &IID_IAppWindowStatics, (void **)&statics );
    IActivationFactory_Release( af );
    if (FAILED(hr) || !statics) return hr;

    /* GetFromWindowId is slot 9 on IAppWindowStatics */
    {
        const struct abi_app_window_statics *aws = (const struct abi_app_window_statics *)statics;
        hr = aws->lpVtbl->GetFromWindowId( statics, (UINT64)hwnd, value );
        IUnknown_Release( (IUnknown *)statics );
    }

    TRACE( "get_AppWindow hwnd %p -> hr %#lx, value %p\n", hwnd, hr, *value );
    return hr;
}

static const void *ms_xaml_window2_vtbl[] =
{
    ms_xaml_window2_QueryInterface,     /* 0 */
    ms_xaml_window2_AddRef,             /* 1 */
    ms_xaml_window2_Release,            /* 2 */
    xaml_window2_GetIids,               /* 3 */
    xaml_window2_GetRuntimeClassName,   /* 4 */
    xaml_window2_GetTrustLevel,         /* 5 */
    xaml_window2_get_SystemBackdrop,    /* 6: get_SystemBackdrop */
    xaml_window2_set_SystemBackdrop,    /* 7: put_SystemBackdrop */
    ms_xaml_window2_get_AppWindow,      /* 8: get_AppWindow */
    xaml_window_zero_out, xaml_window_zero_out, /* 9..10 */
    xaml_window_zero_out, xaml_window_zero_out, xaml_window_zero_out, xaml_window_zero_out, /* 11..14 */
    xaml_window_zero_out, xaml_window_zero_out, xaml_window_zero_out, xaml_window_zero_out, /* 15..18 */
    xaml_window_zero_out, xaml_window_zero_out, xaml_window_zero_out, xaml_window_zero_out, /* 19..22 */
    xaml_window_zero_out, xaml_window_zero_out, xaml_window_zero_out, xaml_window_zero_out, /* 23..26 */
    xaml_window_zero_out, xaml_window_zero_out, xaml_window_zero_out, xaml_window_zero_out, /* 27..30 */
    xaml_window_zero_out, xaml_window_zero_out, xaml_window_zero_out, xaml_window_zero_out, /* 31..34 */
    xaml_window_zero_out, xaml_window_zero_out, xaml_window_zero_out, xaml_window_zero_out, /* 35..38 */
    xaml_window_zero_out, xaml_window_zero_out, xaml_window_zero_out, xaml_window_zero_out, /* 39..42 */
    xaml_window_zero_out, xaml_window_zero_out, xaml_window_zero_out, xaml_window_zero_out, /* 43..46 */
    xaml_window_zero_out, xaml_window_zero_out, xaml_window_zero_out, xaml_window_zero_out, /* 47..50 */
    xaml_window_zero_out, xaml_window_zero_out, xaml_window_zero_out, xaml_window_zero_out, /* 51..54 */
    xaml_window_zero_out, xaml_window_zero_out, xaml_window_zero_out, xaml_window_zero_out, /* 55..58 */
    xaml_window_zero_out, xaml_window_zero_out, xaml_window_zero_out, xaml_window_zero_out, /* 59..62 */
    xaml_window_zero_out                                                                    /* 63 */
};

static inline struct xaml_window *impl_from_IWindowNative( IWindowNative *iface )
{
    return CONTAINING_RECORD( iface, struct xaml_window, IWindowNative_iface );
}

static HRESULT WINAPI xaml_window_native_QueryInterface( IWindowNative *iface, REFIID iid, void **out )
{
    struct xaml_window *impl = impl_from_IWindowNative( iface );
    return xaml_window_QueryInterface( &impl->IInspectable_iface, iid, out );
}

static ULONG WINAPI xaml_window_native_AddRef( IWindowNative *iface )
{
    struct xaml_window *impl = impl_from_IWindowNative( iface );
    return xaml_window_AddRef( &impl->IInspectable_iface );
}

static ULONG WINAPI xaml_window_native_Release( IWindowNative *iface )
{
    struct xaml_window *impl = impl_from_IWindowNative( iface );
    return xaml_window_Release( &impl->IInspectable_iface );
}

static HRESULT WINAPI xaml_window_native_GetWindowHandle( IWindowNative *iface, HWND *hwnd )
{
    struct xaml_window *impl = impl_from_IWindowNative( iface );
    ICoreWindowInterop *interop = NULL;
    HRESULT hr;

    TRACE( "iface %p, hwnd %p\n", iface, hwnd );

    if (!hwnd) return E_POINTER;
    *hwnd = NULL;
    ensure_core_window( impl );
    if (impl->core_window)
    {
        if (SUCCEEDED( hr = ICoreWindow_QueryInterface( impl->core_window, &IID_ICoreWindowInterop, (void **)&interop ) ))
        {
            hr = interop->lpVtbl->GetWindowHandle( interop, hwnd );
            interop->lpVtbl->Release( interop );
            if (SUCCEEDED(hr) && *hwnd) return S_OK;
        }
    }

    if (impl->target_hwnd && IsWindow( impl->target_hwnd ))
    {
        *hwnd = impl->target_hwnd;
        return S_OK;
    }

    *hwnd = xaml_find_core_window();
    if (!*hwnd) *hwnd = FindWindowW( L"Window", NULL );
    if (!*hwnd) *hwnd = GetActiveWindow();
    if (!*hwnd) *hwnd = GetForegroundWindow();

    return *hwnd ? S_OK : E_FAIL;
}

static const struct IWindowNativeVtbl xaml_window_native_vtbl =
{
    xaml_window_native_QueryInterface,
    xaml_window_native_AddRef,
    xaml_window_native_Release,
    xaml_window_native_GetWindowHandle,
};

static HRESULT WINAPI xaml_window_wr_QueryInterface( void *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IWeakReference_local ))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI xaml_window_wr_AddRef( void *iface ) { return 2; }
static ULONG WINAPI xaml_window_wr_Release( void *iface ) { return 1; }
static HRESULT WINAPI xaml_window_wr_Resolve( void *iface, REFIID iid, IInspectable **out )
{
    if (!out) return E_POINTER;
    return xaml_window_QueryInterface( &global_xaml_window.IInspectable_iface, iid, (void **)out );
}
static const void *xaml_window_wr_vtbl[] =
{
    xaml_window_wr_QueryInterface,
    xaml_window_wr_AddRef,
    xaml_window_wr_Release,
    xaml_window_wr_Resolve,
};
static struct { const void **vtbl; } xaml_window_weakref = { xaml_window_wr_vtbl };

static HRESULT WINAPI xaml_window_wrs_QueryInterface( void *iface, REFIID iid, void **out )
{
    return xaml_window_QueryInterface( &global_xaml_window.IInspectable_iface, iid, out );
}
static ULONG WINAPI xaml_window_wrs_AddRef( void *iface )
{
    return xaml_window_AddRef( &global_xaml_window.IInspectable_iface );
}
static ULONG WINAPI xaml_window_wrs_Release( void *iface )
{
    return xaml_window_Release( &global_xaml_window.IInspectable_iface );
}
static HRESULT WINAPI xaml_window_wrs_GetWeakReference( void *iface, void **out )
{
    if (!out) return E_POINTER;
    *out = &xaml_window_weakref;
    return S_OK;
}
static const void *xaml_window_wrs_vtbl[] =
{
    xaml_window_wrs_QueryInterface,
    xaml_window_wrs_AddRef,
    xaml_window_wrs_Release,
    xaml_window_wrs_GetWeakReference,
};

struct xaml_window global_xaml_window =
{
    .IInspectable_iface = {(const struct IInspectableVtbl *)&xaml_window_vtbl},
    .IWindow2_iface = {(const struct IInspectableVtbl *)xaml_window2_vtbl},
    .uwp_IWindow2_iface = {(const struct IInspectableVtbl *)uwp_xaml_window2_vtbl},
    .IWindow3_iface = {(const struct IInspectableVtbl *)xaml_window3_vtbl},
    .IWindowPrivate_iface = {(const struct IInspectableVtbl *)xaml_window_private_vtbl},
    .ms_IWindow_iface = {(const struct IInspectableVtbl *)ms_xaml_window_vtbl},
    .ms_IWindow2_iface = {(const struct IInspectableVtbl *)ms_xaml_window2_vtbl},
    .IWindowNative_iface = {&xaml_window_native_vtbl},
    .IWeakReferenceSource_iface = xaml_window_wrs_vtbl,
    .ref = 1,
};

/* IWindowStatics implementation */
struct window_statics
{
    IActivationFactory IActivationFactory_iface;
    LONG ref;
};
static struct window_statics window_statics_obj;

static HRESULT WINAPI window_statics_CreateInstance( void *iface, void *outer, void **inner, void **instance )
{
    TRACE( "iface %p, outer %p, inner %p, instance %p\n", iface, outer, inner, instance );
    global_window_outer = (IInspectable *)outer;
    TRACE( "stored global_window_outer = %p\n", global_window_outer );
    if (inner && (void *)inner != (void *)outer)
    {
        *inner = (void *)&global_xaml_window.IInspectable_iface;
        IInspectable_AddRef( (IInspectable *)*inner );
    }
    if (instance && (void *)instance != (void *)outer)
    {
        *instance = (void *)&global_xaml_window.IInspectable_iface;
        IInspectable_AddRef( (IInspectable *)*instance );
    }
    return S_OK;
}

static HRESULT WINAPI window_statics_QueryInterface( IActivationFactory *iface, REFIID iid, void **out );
static HRESULT WINAPI win_factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    return window_statics_QueryInterface( &window_statics_obj.IActivationFactory_iface, iid, out );
}
static ULONG WINAPI win_factory_AddRef( IActivationFactory *iface ) { return 2; }
static ULONG WINAPI win_factory_Release( IActivationFactory *iface ) { return 1; }

static HRESULT WINAPI window_statics_GetIids( IActivationFactory *iface, ULONG *count, IID **iids );
static HRESULT WINAPI window_statics_GetRuntimeClassName( IActivationFactory *iface, HSTRING *name );
static HRESULT WINAPI window_statics_GetTrustLevel( IActivationFactory *iface, TrustLevel *level );

static const void *win_factory_vtbl[] =
{
    win_factory_QueryInterface,
    win_factory_AddRef,
    win_factory_Release,
    window_statics_GetIids,
    window_statics_GetRuntimeClassName,
    window_statics_GetTrustLevel,
    window_statics_CreateInstance, /* slot 6: CreateInstance */
};
static struct window_statics win_factory_instance = { {(const struct IActivationFactoryVtbl *)win_factory_vtbl}, 1 };

static HRESULT WINAPI window_statics_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ) ||
        IsEqualGUID( iid, &IID_IWindowStatics ) ||
        IsEqualGUID( iid, &IID_IWindowStatics2 ) ||
        IsEqualGUID( iid, &IID_IWindowStatics_WinUI ))
    {
        *out = iface;
        IActivationFactory_AddRef( iface );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IWindowFactory_WinUI ))
    {
        *out = &win_factory_instance.IActivationFactory_iface;
        IActivationFactory_AddRef( (IActivationFactory *)*out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI window_statics_AddRef( IActivationFactory *iface )
{
    struct window_statics *impl = CONTAINING_RECORD( iface, struct window_statics, IActivationFactory_iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI window_statics_Release( IActivationFactory *iface )
{
    struct window_statics *impl = CONTAINING_RECORD( iface, struct window_statics, IActivationFactory_iface );
    return InterlockedDecrement( &impl->ref );
}

static HRESULT WINAPI window_statics_GetIids( IActivationFactory *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}
static HRESULT WINAPI window_statics_GetRuntimeClassName( IActivationFactory *iface, HSTRING *name )
{ return WindowsCreateString( L"Windows.UI.Xaml.Window", 21, name ); }
static HRESULT WINAPI window_statics_GetTrustLevel( IActivationFactory *iface, TrustLevel *level )
{ if (level) *level = BaseTrust; return S_OK; }

static HRESULT WINAPI window_statics_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    if (!instance) return E_POINTER;
    *instance = (IInspectable *)&global_xaml_window.IInspectable_iface;
    IInspectable_AddRef( *instance );
    return S_OK;
}

static HRESULT WINAPI window_statics_get_Current( void *iface, void *a2, void *a3 )
{
    void **out = a2 ? (void **)a2 : (void **)a3;
    TRACE( "iface %p, a2 %p, a3 %p -> out %p\n", iface, a2, a3, out );
    if (out)
    {
        *out = (void *)&global_xaml_window.IInspectable_iface;
        IInspectable_AddRef( (IInspectable *)&global_xaml_window.IInspectable_iface );
    }
    return S_OK;
}

struct window_statics_extended_vtbl
{
    HRESULT (WINAPI *QueryInterface)(IActivationFactory *, REFIID, void **);
    ULONG (WINAPI *AddRef)(IActivationFactory *);
    ULONG (WINAPI *Release)(IActivationFactory *);
    HRESULT (WINAPI *GetIids)(IActivationFactory *, ULONG *, IID **);
    HRESULT (WINAPI *GetRuntimeClassName)(IActivationFactory *, HSTRING *);
    HRESULT (WINAPI *GetTrustLevel)(IActivationFactory *, TrustLevel *);
    HRESULT (WINAPI *get_Current)(void *, void *, void *);
    HRESULT (WINAPI *ActivateInstance)(IActivationFactory *, IInspectable **);
    void *pad[8];
};

static const struct window_statics_extended_vtbl window_statics_vtbl =
{
    window_statics_QueryInterface,
    window_statics_AddRef,
    window_statics_Release,
    window_statics_GetIids,
    window_statics_GetRuntimeClassName,
    window_statics_GetTrustLevel,
    window_statics_get_Current,
    window_statics_ActivateInstance,
    { NULL },
};

static struct window_statics window_statics_obj =
{
    {(const struct IActivationFactoryVtbl *)&window_statics_vtbl},
    1,
};

IActivationFactory *window_factory = &window_statics_obj.IActivationFactory_iface;
IActivationFactory *win_factory = &win_factory_instance.IActivationFactory_iface;
