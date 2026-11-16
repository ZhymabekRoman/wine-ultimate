/* WinRT Windows.UI.Xaml.Application Implementation
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

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "private.h"
#include "wine/exception.h"
#include "windows.applicationmodel.activation.h"
#include "appmodel.h"
#include "pri.h"
#include "wincodec.h"

static const GUID CLSID_WICImagingFactory_xaml =
    { 0xcacaf262, 0x9370, 0x4615, { 0xa1, 0x3b, 0x9f, 0x55, 0x39, 0xda, 0x4c, 0x0a } };
static const GUID IID_IWICImagingFactory_xaml =
    { 0xec5ec8a9, 0xc395, 0x4314, { 0x9c, 0x77, 0x54, 0xd7, 0xa9, 0x35, 0xff, 0x70 } };
static const GUID GUID_WICPixelFormat32bppBGRA_xaml =
    { 0x6fddc324, 0x4e03, 0x4bfe, { 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x0f } };

WINE_DEFAULT_DEBUG_CHANNEL(xaml);

void uwp_sync_calc_display_from_vm( void );
void uwp_keyboard_press_char( WCHAR ch );
BOOL uwp_dispatch_keydown( HWND hwnd, UINT vk, LPARAM lparam, void *island_root_ptr );

struct application
{
    IApplication IApplication_iface;
    IInspectable inner_iface;
    IInspectable ms_IApplication_iface;
    IInspectable overrides_uwp_iface;
    IInspectable metadata_provider_iface;
    LONG ref;
    IInspectable *outer;
    IInspectable *resources;   /* lazy Windows.Foundation.Collections.PropertySet for the Resources QI set */
};
static struct application *current_application;

static HRESULT WINAPI app_view_activated_QueryInterface(
    __FITypedEventHandler_2_Windows__CApplicationModel__CCore__CCoreApplicationView_Windows__CApplicationModel__CActivation__CIActivatedEventArgs *iface,
    REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID___FITypedEventHandler_2_Windows__CApplicationModel__CCore__CCoreApplicationView_Windows__CApplicationModel__CActivation__CIActivatedEventArgs ))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI app_view_activated_AddRef(
    __FITypedEventHandler_2_Windows__CApplicationModel__CCore__CCoreApplicationView_Windows__CApplicationModel__CActivation__CIActivatedEventArgs *iface )
{ return 2; }
static ULONG WINAPI app_view_activated_Release(
    __FITypedEventHandler_2_Windows__CApplicationModel__CCore__CCoreApplicationView_Windows__CApplicationModel__CActivation__CIActivatedEventArgs *iface )
{ return 1; }

/* WinUI 3 (Microsoft.UI.Xaml) interface GUIDs */
static const GUID IID_IApplicationStatics_WinUI =
    { 0x4e0d09f5, 0x4358, 0x512c, { 0xa9, 0x87, 0x50, 0x3b, 0x52, 0x84, 0x8e, 0x95 } };
static const GUID IID_IApplicationFactory_WinUI3 =
    { 0x9fd96657, 0x5294, 0x5a65, { 0xa1, 0xdb, 0x4f, 0xea, 0x14, 0x35, 0x97, 0xda } };
static const GUID IID_IApplication_WinUI3 =
    { 0x06a8f4e7, 0x1146, 0x55af, { 0x82, 0x0d, 0xeb, 0xd5, 0x56, 0x43, 0xb0, 0x21 } };
static const GUID IID_IApplicationOverrides_WinUI =
    { 0xa33e81ef, 0xc665, 0x503b, { 0x88, 0x27, 0xd2, 0x7e, 0xf1, 0x72, 0x0a, 0x06 } };
static const GUID IID_IApplicationOverrides_UWP =
    { 0x25f99ff7, 0x9347, 0x459a, { 0x9f, 0xac, 0xb2, 0xd0, 0xe1, 0x1c, 0x1a, 0x0f } };
static const GUID IID_IWindowStatics_WinUI =
    { 0x8cc985e3, 0xa41a, 0x5df4, { 0xb5, 0x31, 0xd3, 0xa1, 0x78, 0x8d, 0x86, 0xc5 } };
static const GUID IID_IWindowFactory_WinUI =
    { 0xf0441536, 0xafef, 0x5222, { 0x91, 0x8f, 0x32, 0x4a, 0x9b, 0x2d, 0xec, 0x75 } };
static const GUID IID_IWindow_WinUI =
    { 0x61f0ec79, 0x5d52, 0x56b5, { 0x86, 0xfb, 0x40, 0xfa, 0x4a, 0xf2, 0x88, 0xb0 } };

typedef struct IWindowNative IWindowNative;
typedef struct IWindowNativeVtbl
{
    HRESULT (WINAPI *QueryInterface)( IWindowNative *This, REFIID riid, void **ppvObject );
    ULONG (WINAPI *AddRef)( IWindowNative *This );
    ULONG (WINAPI *Release)( IWindowNative *This );
    HRESULT (WINAPI *GetWindowHandle)( IWindowNative *This, HWND *hwnd );
} IWindowNativeVtbl;
struct IWindowNative { const IWindowNativeVtbl *lpVtbl; };

struct xaml_window
{
    IInspectable IInspectable_iface;
    IInspectable IWindow2_iface;
    IInspectable IWindowPrivate_iface;
    IInspectable ms_IWindow_iface;
    IInspectable ms_IWindow2_iface;
    IWindowNative IWindowNative_iface;
    LONG ref;
    IInspectable *content;
    ICoreWindow *core_window;
    IDCompositionDesktopDevice *dcomp_device;
    IDCompositionTarget *dcomp_target;
    IDCompositionVisual2 *dcomp_root_visual;
    IDXGISwapChain1 *swapchain;
    ID3D11Device *d3d11_dev;
    ID3D11DeviceContext *d3d11_ctx;
    HWND target_hwnd;
    UINT width, height;
    BOOL core_window_subscribed;
    void *activated_handlers[8];
    int activated_handler_count;
    BOOL is_active;
};
static struct xaml_window global_xaml_window;
static IInspectable *global_window_outer = NULL;
static WCHAR xaml_explicit_title[128];
static BOOL lifecycle_started = FALSE;

static HRESULT WINAPI app_view_activated_Invoke(
    __FITypedEventHandler_2_Windows__CApplicationModel__CCore__CCoreApplicationView_Windows__CApplicationModel__CActivation__CIActivatedEventArgs *iface,
    ICoreApplicationView *sender,
    __x_ABI_CWindows_CApplicationModel_CActivation_CIActivatedEventArgs *args )
{
    WARN( "app_view_activated_Invoke: sender %p, args %p, current_application %p\n", sender, args, current_application );
    if (current_application && current_application->outer)
    {
        IInspectable *overrides = NULL;
        HRESULT hr_uwp, hr_winui;

        hr_winui = IInspectable_QueryInterface( current_application->outer, &IID_IApplicationOverrides_WinUI, (void **)&overrides );
        WARN( "QI IApplicationOverrides_WinUI returned hr %#lx, overrides %p\n", hr_winui, overrides );
        if (SUCCEEDED(hr_winui) && overrides)
        {
            void **ov_vtbl = *(void ***)overrides;
            HRESULT (WINAPI *on_launched)( void *, void * ) = (void *)ov_vtbl[6];
            WARN( "invoking WinUI3 App OnLaunched %p (overrides %p, args %p)\n",
                   on_launched, overrides, args );
            if (on_launched) on_launched( overrides, args );
            IInspectable_Release( overrides );
        }
        else
        {
            hr_uwp = IInspectable_QueryInterface( current_application->outer, &IID_IApplicationOverrides_UWP, (void **)&overrides );
            WARN( "QI IApplicationOverrides_UWP returned hr %#lx, overrides %p\n", hr_uwp, overrides );
            if (SUCCEEDED(hr_uwp) && overrides)
            {
                void **ov_vtbl = *(void ***)overrides;
                HRESULT (WINAPI *on_launched)( void *, void * ) = (void *)ov_vtbl[7];
                WARN( "invoking UWP App OnLaunched via QI %p (overrides %p, args %p)\n",
                       on_launched, overrides, args );
                if (is_store_app())
                {
                    IInspectable *store_page = NULL;
                    if (SUCCEEDED(uwp_store_create_initial_page( &store_page )) && store_page)
                        global_xaml_window.content = store_page;
                }
                else if (on_launched) on_launched( overrides, args );
                IInspectable_Release( overrides );
            }
            else
            {
                void **vtbl = *(void ***)current_application->outer;
                HRESULT (WINAPI *on_launched)( void *, void * ) = (void *)vtbl[6];
                WARN( "invoking App OnLaunched fallback vtbl[6] %p (outer %p, args %p)\n",
                       on_launched, current_application->outer, args );
                if (on_launched) on_launched( current_application->outer, args );
            }
        }
    }
    xaml_fire_all_loaded();
    return S_OK;
}

static const struct __FITypedEventHandler_2_Windows__CApplicationModel__CCore__CCoreApplicationView_Windows__CApplicationModel__CActivation__CIActivatedEventArgsVtbl
app_view_activated_vtbl =
{
    app_view_activated_QueryInterface,
    app_view_activated_AddRef,
    app_view_activated_Release,
    app_view_activated_Invoke,
};

static __FITypedEventHandler_2_Windows__CApplicationModel__CCore__CCoreApplicationView_Windows__CApplicationModel__CActivation__CIActivatedEventArgs
app_view_activated_handler = { &app_view_activated_vtbl };

static const GUID IID_IWindowNative =
    { 0xeecdbf0e, 0xbae9, 0x4cb6, { 0xa6, 0x8e, 0x95, 0x98, 0xe1, 0xcb, 0x57, 0xbb } };
static const GUID IID_ICoreWindowInterop =
    { 0x45d64a29, 0xa63e, 0x4cb6, { 0xb4, 0x98, 0x57, 0x81, 0xd2, 0x98, 0xcb, 0x4f } };

typedef struct ICoreWindowInterop ICoreWindowInterop;
typedef struct ICoreWindowInteropVtbl
{
    HRESULT (WINAPI *QueryInterface)( ICoreWindowInterop *This, REFIID riid, void **ppvObject );
    ULONG (WINAPI *AddRef)( ICoreWindowInterop *This );
    ULONG (WINAPI *Release)( ICoreWindowInterop *This );
    HRESULT (WINAPI *GetWindowHandle)( ICoreWindowInterop *This, HWND *hwnd );
    HRESULT (WINAPI *SetMessageHandled)( ICoreWindowInterop *This, boolean value );
} ICoreWindowInteropVtbl;
struct ICoreWindowInterop { const ICoreWindowInteropVtbl *lpVtbl; };

/* ---- Shared WinRT Event Registry (token-based, twinapi bg_events pattern) ---- */
static CRITICAL_SECTION xaml_event_cs;
static CRITICAL_SECTION_DEBUG xaml_event_cs_debug =
{
    0, 0, &xaml_event_cs,
    { &xaml_event_cs_debug.ProcessLocksList, &xaml_event_cs_debug.ProcessLocksList },
    0, 0, { (DWORD_PTR)(__FILE__ ": xaml_event_cs") }
};
static CRITICAL_SECTION xaml_event_cs = { &xaml_event_cs_debug, -1, 0, 0, 0, 0 };

HRESULT xaml_event_registry_add( struct xaml_event_registry *reg, void *handler, EventRegistrationToken *token )
{
    unsigned int i;

    if (!reg) return E_INVALIDARG;
    if (!handler)
    {
        if (token) token->value = 0;
        return S_OK;
    }

    EnterCriticalSection( &xaml_event_cs );
    for (i = 0; i < ARRAY_SIZE( reg->entries ); i++)
    {
        if (!reg->entries[i].handler)
        {
            INT64 tok = InterlockedIncrement64( &reg->next_token );
            IUnknown_AddRef( (IUnknown *)handler );
            reg->entries[i].handler = handler;
            reg->entries[i].token = tok;
            if (token) token->value = tok;
            LeaveCriticalSection( &xaml_event_cs );
            return S_OK;
        }
    }
    LeaveCriticalSection( &xaml_event_cs );
    return E_OUTOFMEMORY;
}

HRESULT xaml_event_registry_remove( struct xaml_event_registry *reg, EventRegistrationToken token )
{
    unsigned int i;

    if (!reg) return E_INVALIDARG;
    EnterCriticalSection( &xaml_event_cs );
    for (i = 0; i < ARRAY_SIZE( reg->entries ); i++)
    {
        if (reg->entries[i].handler && reg->entries[i].token == token.value)
        {
            void *h = reg->entries[i].handler;
            reg->entries[i].handler = NULL;
            reg->entries[i].token = 0;
            LeaveCriticalSection( &xaml_event_cs );
            IUnknown_Release( (IUnknown *)h );
            return S_OK;
        }
    }
    LeaveCriticalSection( &xaml_event_cs );
    return S_OK;
}

unsigned int xaml_event_registry_snapshot( struct xaml_event_registry *reg, void **out, unsigned int max_out )
{
    unsigned int i, count = 0;

    if (!reg || !out || !max_out) return 0;
    EnterCriticalSection( &xaml_event_cs );
    for (i = 0; i < ARRAY_SIZE( reg->entries ) && count < max_out; i++)
    {
        if (reg->entries[i].handler)
        {
            IUnknown_AddRef( (IUnknown *)reg->entries[i].handler );
            out[count++] = reg->entries[i].handler;
        }
    }
    LeaveCriticalSection( &xaml_event_cs );
    return count;
}

void xaml_event_registry_clear( struct xaml_event_registry *reg )
{
    void *to_release[64];
    unsigned int i, count = 0;

    if (!reg) return;
    EnterCriticalSection( &xaml_event_cs );
    for (i = 0; i < ARRAY_SIZE( reg->entries ); i++)
    {
        if (reg->entries[i].handler)
        {
            to_release[count++] = reg->entries[i].handler;
            reg->entries[i].handler = NULL;
            reg->entries[i].token = 0;
        }
    }
    LeaveCriticalSection( &xaml_event_cs );
    for (i = 0; i < count; i++)
        IUnknown_Release( (IUnknown *)to_release[i] );
}

/* ---- Mouse PointerDevice + PointerPoint + PointerPointProperties + Core PointerEventArgs ---- */
static HRESULT WINAPI mouse_dev_QueryInterface( IPointerDevice *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID___x_ABI_CWindows_CDevices_CInput_CIPointerDevice ))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI mouse_dev_AddRef( IPointerDevice *iface ) { return 2; }
static ULONG WINAPI mouse_dev_Release( IPointerDevice *iface ) { return 1; }
static HRESULT WINAPI mouse_dev_GetIids( IPointerDevice *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}
static HRESULT WINAPI mouse_dev_GetRuntimeClassName( IPointerDevice *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.Devices.Input.PointerDevice", 35, name );
}
static HRESULT WINAPI mouse_dev_GetTrustLevel( IPointerDevice *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}
static HRESULT WINAPI mouse_dev_get_PointerDeviceType( IPointerDevice *iface, PointerDeviceType *value )
{
    if (!value) return E_POINTER;
    *value = PointerDeviceType_Mouse;
    return S_OK;
}
static HRESULT WINAPI mouse_dev_get_IsIntegrated( IPointerDevice *iface, boolean *value )
{
    if (!value) return E_POINTER;
    *value = FALSE;
    return S_OK;
}
static HRESULT WINAPI mouse_dev_get_MaxContacts( IPointerDevice *iface, UINT32 *value )
{
    if (!value) return E_POINTER;
    *value = 1;
    return S_OK;
}
static HRESULT WINAPI mouse_dev_get_PhysicalDeviceRect( IPointerDevice *iface, Rect *value )
{
    if (!value) return E_POINTER;
    value->X = 0.0f; value->Y = 0.0f;
    value->Width = (float)GetSystemMetrics( SM_CXSCREEN );
    value->Height = (float)GetSystemMetrics( SM_CYSCREEN );
    return S_OK;
}
static HRESULT WINAPI mouse_dev_get_ScreenRect( IPointerDevice *iface, Rect *value )
{
    return mouse_dev_get_PhysicalDeviceRect( iface, value );
}
static HRESULT WINAPI mouse_dev_get_SupportedUsages( IPointerDevice *iface, __FIVectorView_1_PointerDeviceUsage **value )
{
    if (!value) return E_POINTER;
    *value = NULL;
    return S_OK;
}

static const IPointerDeviceVtbl mouse_dev_vtbl =
{
    mouse_dev_QueryInterface,
    mouse_dev_AddRef,
    mouse_dev_Release,
    mouse_dev_GetIids,
    mouse_dev_GetRuntimeClassName,
    mouse_dev_GetTrustLevel,
    mouse_dev_get_PointerDeviceType,
    mouse_dev_get_IsIntegrated,
    mouse_dev_get_MaxContacts,
    mouse_dev_get_PhysicalDeviceRect,
    mouse_dev_get_ScreenRect,
    mouse_dev_get_SupportedUsages,
};
static IPointerDevice mouse_pointer_device = { &mouse_dev_vtbl };

struct xaml_pointer_point
{
    IPointerPoint IPointerPoint_iface;
    IPointerPointProperties IPointerPointProperties_iface;
    LONG ref;
    float x;
    float y;
    UINT32 pointer_id;
    UINT32 frame_id;
    UINT64 timestamp;
    BOOL is_in_contact;
    BOOL is_in_window;
    BOOL is_left;
    BOOL is_right;
    BOOL is_middle;
    INT32 wheel_delta;
    PointerUpdateKind update_kind;
};

static LONG xaml_pointer_frame_seq;

static inline struct xaml_pointer_point *impl_from_IPointerPoint( IPointerPoint *iface )
{
    return CONTAINING_RECORD( iface, struct xaml_pointer_point, IPointerPoint_iface );
}
static inline struct xaml_pointer_point *impl_from_IPointerPointProperties( IPointerPointProperties *iface )
{
    return CONTAINING_RECORD( iface, struct xaml_pointer_point, IPointerPointProperties_iface );
}

static HRESULT WINAPI ptr_point_QueryInterface( IPointerPoint *iface, REFIID iid, void **out )
{
    struct xaml_pointer_point *impl = impl_from_IPointerPoint( iface );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID___x_ABI_CWindows_CUI_CInput_CIPointerPoint ))
    {
        InterlockedIncrement( &impl->ref );
        *out = &impl->IPointerPoint_iface;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID___x_ABI_CWindows_CUI_CInput_CIPointerPointProperties ))
    {
        InterlockedIncrement( &impl->ref );
        *out = &impl->IPointerPointProperties_iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI ptr_point_AddRef( IPointerPoint *iface )
{
    return InterlockedIncrement( &impl_from_IPointerPoint( iface )->ref );
}
static ULONG WINAPI ptr_point_Release( IPointerPoint *iface )
{
    struct xaml_pointer_point *impl = impl_from_IPointerPoint( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref) free( impl );
    return ref;
}
static HRESULT WINAPI ptr_point_GetIids( IPointerPoint *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}
static HRESULT WINAPI ptr_point_GetRuntimeClassName( IPointerPoint *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.UI.Input.PointerPoint", 29, name );
}
static HRESULT WINAPI ptr_point_GetTrustLevel( IPointerPoint *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}
static HRESULT WINAPI ptr_point_get_PointerDevice( IPointerPoint *iface, IPointerDevice **value )
{
    if (!value) return E_POINTER;
    *value = &mouse_pointer_device;
    return S_OK;
}
static HRESULT WINAPI ptr_point_get_Position( IPointerPoint *iface, Point *value )
{
    struct xaml_pointer_point *impl = impl_from_IPointerPoint( iface );
    if (!value) return E_POINTER;
    value->X = impl->x;
    value->Y = impl->y;
    return S_OK;
}
static HRESULT WINAPI ptr_point_get_RawPosition( IPointerPoint *iface, Point *value )
{
    struct xaml_pointer_point *impl = impl_from_IPointerPoint( iface );
    if (!value) return E_POINTER;
    value->X = impl->x;
    value->Y = impl->y;
    return S_OK;
}
static HRESULT WINAPI ptr_point_get_pointer_id( IPointerPoint *iface, UINT32 *value )
{
    struct xaml_pointer_point *impl = impl_from_IPointerPoint( iface );
    if (!value) return E_POINTER;
    *value = impl->pointer_id;
    return S_OK;
}
static HRESULT WINAPI ptr_point_get_FrameId( IPointerPoint *iface, UINT32 *value )
{
    struct xaml_pointer_point *impl = impl_from_IPointerPoint( iface );
    if (!value) return E_POINTER;
    *value = impl->frame_id;
    return S_OK;
}
static HRESULT WINAPI ptr_point_get_Timestamp( IPointerPoint *iface, UINT64 *value )
{
    struct xaml_pointer_point *impl = impl_from_IPointerPoint( iface );
    if (!value) return E_POINTER;
    *value = impl->timestamp;
    return S_OK;
}
static HRESULT WINAPI ptr_point_get_IsInContact( IPointerPoint *iface, boolean *value )
{
    struct xaml_pointer_point *impl = impl_from_IPointerPoint( iface );
    if (!value) return E_POINTER;
    *value = impl->is_in_contact ? TRUE : FALSE;
    return S_OK;
}
static HRESULT WINAPI ptr_point_get_Properties( IPointerPoint *iface, IPointerPointProperties **value )
{
    struct xaml_pointer_point *impl = impl_from_IPointerPoint( iface );
    if (!value) return E_POINTER;
    InterlockedIncrement( &impl->ref );
    *value = &impl->IPointerPointProperties_iface;
    return S_OK;
}

static const IPointerPointVtbl ptr_point_vtbl =
{
    ptr_point_QueryInterface,
    ptr_point_AddRef,
    ptr_point_Release,
    ptr_point_GetIids,
    ptr_point_GetRuntimeClassName,
    ptr_point_GetTrustLevel,
    ptr_point_get_PointerDevice,
    ptr_point_get_Position,
    ptr_point_get_RawPosition,
    ptr_point_get_pointer_id,
    ptr_point_get_FrameId,
    ptr_point_get_Timestamp,
    ptr_point_get_IsInContact,
    ptr_point_get_Properties,
};

static HRESULT WINAPI ptr_props_QueryInterface( IPointerPointProperties *iface, REFIID iid, void **out )
{
    struct xaml_pointer_point *impl = impl_from_IPointerPointProperties( iface );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID___x_ABI_CWindows_CUI_CInput_CIPointerPointProperties ))
    {
        InterlockedIncrement( &impl->ref );
        *out = &impl->IPointerPointProperties_iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI ptr_props_AddRef( IPointerPointProperties *iface )
{
    return InterlockedIncrement( &impl_from_IPointerPointProperties( iface )->ref );
}
static ULONG WINAPI ptr_props_Release( IPointerPointProperties *iface )
{
    return ptr_point_Release( &impl_from_IPointerPointProperties( iface )->IPointerPoint_iface );
}
static HRESULT WINAPI ptr_props_GetIids( IPointerPointProperties *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}
static HRESULT WINAPI ptr_props_GetRuntimeClassName( IPointerPointProperties *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.UI.Input.PointerPointProperties", 39, name );
}
static HRESULT WINAPI ptr_props_GetTrustLevel( IPointerPointProperties *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}
static HRESULT WINAPI ptr_props_get_Pressure( IPointerPointProperties *iface, FLOAT *value )
{
    if (!value) return E_POINTER;
    *value = 0.5f;
    return S_OK;
}
static HRESULT WINAPI ptr_props_get_IsInverted( IPointerPointProperties *iface, boolean *value )
{
    if (!value) return E_POINTER;
    *value = FALSE;
    return S_OK;
}
static HRESULT WINAPI ptr_props_get_IsEraser( IPointerPointProperties *iface, boolean *value )
{
    if (!value) return E_POINTER;
    *value = FALSE;
    return S_OK;
}
static HRESULT WINAPI ptr_props_get_Orientation( IPointerPointProperties *iface, FLOAT *value )
{
    if (!value) return E_POINTER;
    *value = 0.0f;
    return S_OK;
}
static HRESULT WINAPI ptr_props_get_XTilt( IPointerPointProperties *iface, FLOAT *value )
{
    if (!value) return E_POINTER;
    *value = 0.0f;
    return S_OK;
}
static HRESULT WINAPI ptr_props_get_YTilt( IPointerPointProperties *iface, FLOAT *value )
{
    if (!value) return E_POINTER;
    *value = 0.0f;
    return S_OK;
}
static HRESULT WINAPI ptr_props_get_Twist( IPointerPointProperties *iface, FLOAT *value )
{
    if (!value) return E_POINTER;
    *value = 0.0f;
    return S_OK;
}
static HRESULT WINAPI ptr_props_get_ContactRect( IPointerPointProperties *iface, Rect *value )
{
    struct xaml_pointer_point *impl = impl_from_IPointerPointProperties( iface );
    if (!value) return E_POINTER;
    value->X = impl->x;
    value->Y = impl->y;
    value->Width = 1.0f;
    value->Height = 1.0f;
    return S_OK;
}
static HRESULT WINAPI ptr_props_get_ContactRectRaw( IPointerPointProperties *iface, Rect *value )
{
    return ptr_props_get_ContactRect( iface, value );
}
static HRESULT WINAPI ptr_props_get_TouchConfidence( IPointerPointProperties *iface, boolean *value )
{
    if (!value) return E_POINTER;
    *value = FALSE;
    return S_OK;
}
static HRESULT WINAPI ptr_props_get_IsLeftButtonPressed( IPointerPointProperties *iface, boolean *value )
{
    struct xaml_pointer_point *impl = impl_from_IPointerPointProperties( iface );
    if (!value) return E_POINTER;
    *value = impl->is_left ? TRUE : FALSE;
    return S_OK;
}
static HRESULT WINAPI ptr_props_get_IsRightButtonPressed( IPointerPointProperties *iface, boolean *value )
{
    struct xaml_pointer_point *impl = impl_from_IPointerPointProperties( iface );
    if (!value) return E_POINTER;
    *value = impl->is_right ? TRUE : FALSE;
    return S_OK;
}
static HRESULT WINAPI ptr_props_get_IsMiddleButtonPressed( IPointerPointProperties *iface, boolean *value )
{
    struct xaml_pointer_point *impl = impl_from_IPointerPointProperties( iface );
    if (!value) return E_POINTER;
    *value = impl->is_middle ? TRUE : FALSE;
    return S_OK;
}
static HRESULT WINAPI ptr_props_get_MouseWheelDelta( IPointerPointProperties *iface, INT32 *value )
{
    struct xaml_pointer_point *impl = impl_from_IPointerPointProperties( iface );
    if (!value) return E_POINTER;
    *value = impl->wheel_delta;
    return S_OK;
}
static HRESULT WINAPI ptr_props_get_IsHorizontalMouseWheel( IPointerPointProperties *iface, boolean *value )
{
    if (!value) return E_POINTER;
    *value = FALSE;
    return S_OK;
}
static HRESULT WINAPI ptr_props_get_IsPrimary( IPointerPointProperties *iface, boolean *value )
{
    if (!value) return E_POINTER;
    *value = TRUE;
    return S_OK;
}
static HRESULT WINAPI ptr_props_get_IsInRange( IPointerPointProperties *iface, boolean *value )
{
    struct xaml_pointer_point *impl = impl_from_IPointerPointProperties( iface );
    if (!value) return E_POINTER;
    *value = impl->is_in_window ? TRUE : FALSE;
    return S_OK;
}
static HRESULT WINAPI ptr_props_get_IsCanceled( IPointerPointProperties *iface, boolean *value )
{
    if (!value) return E_POINTER;
    *value = FALSE;
    return S_OK;
}
static HRESULT WINAPI ptr_props_get_IsBarrelButtonPressed( IPointerPointProperties *iface, boolean *value )
{
    if (!value) return E_POINTER;
    *value = FALSE;
    return S_OK;
}
static HRESULT WINAPI ptr_props_get_IsXButton1Pressed( IPointerPointProperties *iface, boolean *value )
{
    if (!value) return E_POINTER;
    *value = FALSE;
    return S_OK;
}
static HRESULT WINAPI ptr_props_get_IsXButton2Pressed( IPointerPointProperties *iface, boolean *value )
{
    if (!value) return E_POINTER;
    *value = FALSE;
    return S_OK;
}
static HRESULT WINAPI ptr_props_get_PointerUpdateKind( IPointerPointProperties *iface, PointerUpdateKind *value )
{
    struct xaml_pointer_point *impl = impl_from_IPointerPointProperties( iface );
    if (!value) return E_POINTER;
    *value = impl->update_kind;
    return S_OK;
}
static HRESULT WINAPI ptr_props_HasUsage( IPointerPointProperties *iface, UINT32 page, UINT32 id, boolean *value )
{
    if (!value) return E_POINTER;
    *value = FALSE;
    return S_OK;
}
static HRESULT WINAPI ptr_props_GetUsageValue( IPointerPointProperties *iface, UINT32 page, UINT32 id, INT32 *value )
{
    if (!value) return E_POINTER;
    *value = 0;
    return S_OK;
}

static const IPointerPointPropertiesVtbl ptr_props_vtbl =
{
    ptr_props_QueryInterface,
    ptr_props_AddRef,
    ptr_props_Release,
    ptr_props_GetIids,
    ptr_props_GetRuntimeClassName,
    ptr_props_GetTrustLevel,
    ptr_props_get_Pressure,
    ptr_props_get_IsInverted,
    ptr_props_get_IsEraser,
    ptr_props_get_Orientation,
    ptr_props_get_XTilt,
    ptr_props_get_YTilt,
    ptr_props_get_Twist,
    ptr_props_get_ContactRect,
    ptr_props_get_ContactRectRaw,
    ptr_props_get_TouchConfidence,
    ptr_props_get_IsLeftButtonPressed,
    ptr_props_get_IsRightButtonPressed,
    ptr_props_get_IsMiddleButtonPressed,
    ptr_props_get_MouseWheelDelta,
    ptr_props_get_IsHorizontalMouseWheel,
    ptr_props_get_IsPrimary,
    ptr_props_get_IsInRange,
    ptr_props_get_IsCanceled,
    ptr_props_get_IsBarrelButtonPressed,
    ptr_props_get_IsXButton1Pressed,
    ptr_props_get_IsXButton2Pressed,
    ptr_props_get_PointerUpdateKind,
    ptr_props_HasUsage,
    ptr_props_GetUsageValue,
};

IPointerPoint *xaml_pointer_point_create( const struct xaml_pointer_info *info, float rel_x, float rel_y )
{
    struct xaml_pointer_point *impl = calloc( 1, sizeof(*impl) );
    if (!impl) return NULL;
    impl->IPointerPoint_iface.lpVtbl = &ptr_point_vtbl;
    impl->IPointerPointProperties_iface.lpVtbl = &ptr_props_vtbl;
    impl->ref = 1;
    impl->x = rel_x;
    impl->y = rel_y;
    impl->pointer_id = info && info->pointer_id ? info->pointer_id : 1;
    impl->frame_id = (UINT32)InterlockedIncrement( &xaml_pointer_frame_seq );
    impl->timestamp = GetTickCount64() * 1000;
    if (info)
    {
        impl->is_in_contact = info->is_in_contact;
        impl->is_in_window = info->is_in_window;
        impl->is_left = info->is_left;
        impl->is_right = info->is_right;
        impl->is_middle = info->is_middle;
        impl->wheel_delta = info->wheel_delta;
        impl->update_kind = info->update_kind;
    }
    return &impl->IPointerPoint_iface;
}

struct xaml_core_pointer_args
{
    __x_ABI_CWindows_CUI_CCore_CIPointerEventArgs IPointerEventArgs_iface;
    __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs ICoreWindowEventArgs_iface;
    LONG ref;
    IPointerPoint *point;
    UINT32 key_modifiers;
    boolean handled;
};

static inline struct xaml_core_pointer_args *impl_from_cw_ptr_args( __x_ABI_CWindows_CUI_CCore_CIPointerEventArgs *iface )
{
    return CONTAINING_RECORD( iface, struct xaml_core_pointer_args, IPointerEventArgs_iface );
}
static inline struct xaml_core_pointer_args *impl_from_cw_core_args( __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs *iface )
{
    return CONTAINING_RECORD( iface, struct xaml_core_pointer_args, ICoreWindowEventArgs_iface );
}

static HRESULT WINAPI cw_ptr_args_QueryInterface( __x_ABI_CWindows_CUI_CCore_CIPointerEventArgs *iface, REFIID iid, void **out )
{
    struct xaml_core_pointer_args *impl = impl_from_cw_ptr_args( iface );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID___x_ABI_CWindows_CUI_CCore_CIPointerEventArgs ))
    {
        InterlockedIncrement( &impl->ref );
        *out = &impl->IPointerEventArgs_iface;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID___x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs ))
    {
        InterlockedIncrement( &impl->ref );
        *out = &impl->ICoreWindowEventArgs_iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI cw_ptr_args_AddRef( __x_ABI_CWindows_CUI_CCore_CIPointerEventArgs *iface )
{
    return InterlockedIncrement( &impl_from_cw_ptr_args( iface )->ref );
}
static ULONG WINAPI cw_ptr_args_Release( __x_ABI_CWindows_CUI_CCore_CIPointerEventArgs *iface )
{
    struct xaml_core_pointer_args *impl = impl_from_cw_ptr_args( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        if (impl->point) IPointerPoint_Release( impl->point );
        free( impl );
    }
    return ref;
}
static HRESULT WINAPI cw_ptr_args_GetIids( __x_ABI_CWindows_CUI_CCore_CIPointerEventArgs *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}
static HRESULT WINAPI cw_ptr_args_GetRuntimeClassName( __x_ABI_CWindows_CUI_CCore_CIPointerEventArgs *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.UI.Core.PointerEventArgs", 32, name );
}
static HRESULT WINAPI cw_ptr_args_GetTrustLevel( __x_ABI_CWindows_CUI_CCore_CIPointerEventArgs *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}
static HRESULT WINAPI cw_ptr_args_get_CurrentPoint( __x_ABI_CWindows_CUI_CCore_CIPointerEventArgs *iface, IPointerPoint **value )
{
    struct xaml_core_pointer_args *impl = impl_from_cw_ptr_args( iface );
    if (!value) return E_POINTER;
    *value = impl->point;
    if (*value) IPointerPoint_AddRef( *value );
    return S_OK;
}
static HRESULT WINAPI cw_ptr_args_get_KeyModifiers( __x_ABI_CWindows_CUI_CCore_CIPointerEventArgs *iface, UINT32 *value )
{
    struct xaml_core_pointer_args *impl = impl_from_cw_ptr_args( iface );
    if (!value) return E_POINTER;
    *value = impl->key_modifiers;
    return S_OK;
}
static HRESULT WINAPI cw_ptr_args_GetIntermediatePoints( __x_ABI_CWindows_CUI_CCore_CIPointerEventArgs *iface,
    __FIVector_1_Windows__CUI__CInput__CPointerPoint **value )
{
    if (!value) return E_POINTER;
    *value = NULL;
    return S_OK;
}

static const struct __x_ABI_CWindows_CUI_CCore_CIPointerEventArgsVtbl cw_ptr_args_vtbl =
{
    cw_ptr_args_QueryInterface,
    cw_ptr_args_AddRef,
    cw_ptr_args_Release,
    cw_ptr_args_GetIids,
    cw_ptr_args_GetRuntimeClassName,
    cw_ptr_args_GetTrustLevel,
    cw_ptr_args_get_CurrentPoint,
    cw_ptr_args_get_KeyModifiers,
    cw_ptr_args_GetIntermediatePoints,
};

static HRESULT WINAPI cw_core_args_QueryInterface( __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs *iface, REFIID iid, void **out )
{
    return cw_ptr_args_QueryInterface( &impl_from_cw_core_args( iface )->IPointerEventArgs_iface, iid, out );
}
static ULONG WINAPI cw_core_args_AddRef( __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs *iface )
{
    return cw_ptr_args_AddRef( &impl_from_cw_core_args( iface )->IPointerEventArgs_iface );
}
static ULONG WINAPI cw_core_args_Release( __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs *iface )
{
    return cw_ptr_args_Release( &impl_from_cw_core_args( iface )->IPointerEventArgs_iface );
}
static HRESULT WINAPI cw_core_args_GetIids( __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}
static HRESULT WINAPI cw_core_args_GetRuntimeClassName( __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.UI.Core.PointerEventArgs", 32, name );
}
static HRESULT WINAPI cw_core_args_GetTrustLevel( __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}
static HRESULT WINAPI cw_core_args_get_Handled( __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs *iface, boolean *value )
{
    if (!value) return E_POINTER;
    *value = impl_from_cw_core_args( iface )->handled;
    return S_OK;
}
static HRESULT WINAPI cw_core_args_put_Handled( __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs *iface, boolean value )
{
    impl_from_cw_core_args( iface )->handled = value;
    return S_OK;
}

static const struct __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgsVtbl cw_core_args_vtbl =
{
    cw_core_args_QueryInterface,
    cw_core_args_AddRef,
    cw_core_args_Release,
    cw_core_args_GetIids,
    cw_core_args_GetRuntimeClassName,
    cw_core_args_GetTrustLevel,
    cw_core_args_get_Handled,
    cw_core_args_put_Handled,
};

__x_ABI_CWindows_CUI_CCore_CIPointerEventArgs *xaml_core_pointer_args_create( const struct xaml_pointer_info *info )
{
    struct xaml_core_pointer_args *impl = calloc( 1, sizeof(*impl) );
    if (!impl) return NULL;
    impl->IPointerEventArgs_iface.lpVtbl = &cw_ptr_args_vtbl;
    impl->ICoreWindowEventArgs_iface.lpVtbl = &cw_core_args_vtbl;
    impl->ref = 1;
    impl->key_modifiers = info ? info->key_modifiers : 0;
    impl->point = xaml_pointer_point_create( info, info ? info->x : 0.0f, info ? info->y : 0.0f );
    return &impl->IPointerEventArgs_iface;
}

/* ---- CoreWindow Wrapper with Multi-Handler Pointer Event Registries ---- */
struct xaml_core_window
{
    ICoreWindow ICoreWindow_iface;
    ICoreWindowInterop ICoreWindowInterop_iface;
    LONG ref;
    ICoreWindow *inner;
    HWND hwnd;
    POINT last_pos;
    BOOL has_pos;
    BOOL pointer_inside;
    BOOL has_capture;
    struct xaml_event_registry pointer_pressed;
    struct xaml_event_registry pointer_moved;
    struct xaml_event_registry pointer_released;
    struct xaml_event_registry pointer_wheel_changed;
    struct xaml_event_registry pointer_entered;
    struct xaml_event_registry pointer_exited;
    struct xaml_event_registry pointer_capture_lost;
    struct xaml_event_registry key_down;
    struct xaml_event_registry key_up;
    struct xaml_event_registry character_received;
    EventRegistrationToken key_down_inner_tokens[64];
    EventRegistrationToken key_up_inner_tokens[64];
    EventRegistrationToken character_received_inner_tokens[64];
};

static const ICoreWindowVtbl xaml_cw_vtbl;

static inline struct xaml_core_window *impl_from_cw_ICoreWindow( ICoreWindow *iface )
{
    return CONTAINING_RECORD( iface, struct xaml_core_window, ICoreWindow_iface );
}
static inline struct xaml_core_window *impl_from_cw_Interop( ICoreWindowInterop *iface )
{
    return CONTAINING_RECORD( iface, struct xaml_core_window, ICoreWindowInterop_iface );
}

static struct xaml_core_window *as_xaml_core_window( ICoreWindow *cw )
{
    if (!cw || cw->lpVtbl != &xaml_cw_vtbl) return NULL;
    return impl_from_cw_ICoreWindow( cw );
}

static HWND xaml_cw_resolve_hwnd( struct xaml_core_window *impl )
{
    ICoreWindowInterop *interop = NULL;
    HWND hw = NULL;
    if (!impl) return NULL;
    if (impl->hwnd && IsWindow( impl->hwnd )) return impl->hwnd;
    if (impl->inner && SUCCEEDED(ICoreWindow_QueryInterface( impl->inner, &IID_ICoreWindowInterop, (void **)&interop )) && interop)
    {
        interop->lpVtbl->GetWindowHandle( interop, &hw );
        interop->lpVtbl->Release( interop );
        if (hw && IsWindow( hw )) impl->hwnd = hw;
    }
    return hw;
}

static HRESULT WINAPI xaml_cw_QueryInterface( ICoreWindow *iface, REFIID iid, void **out )
{
    struct xaml_core_window *impl = impl_from_cw_ICoreWindow( iface );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_ICoreWindow ))
    {
        InterlockedIncrement( &impl->ref );
        *out = &impl->ICoreWindow_iface;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_ICoreWindowInterop ))
    {
        InterlockedIncrement( &impl->ref );
        *out = &impl->ICoreWindowInterop_iface;
        return S_OK;
    }
    if (impl->inner)
        return ICoreWindow_QueryInterface( impl->inner, iid, out );
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI xaml_cw_AddRef( ICoreWindow *iface )
{
    return InterlockedIncrement( &impl_from_cw_ICoreWindow( iface )->ref );
}
static ULONG WINAPI xaml_cw_Release( ICoreWindow *iface )
{
    struct xaml_core_window *impl = impl_from_cw_ICoreWindow( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        xaml_event_registry_clear( &impl->pointer_pressed );
        xaml_event_registry_clear( &impl->pointer_moved );
        xaml_event_registry_clear( &impl->pointer_released );
        xaml_event_registry_clear( &impl->pointer_wheel_changed );
        xaml_event_registry_clear( &impl->pointer_entered );
        xaml_event_registry_clear( &impl->pointer_exited );
        xaml_event_registry_clear( &impl->pointer_capture_lost );
        xaml_event_registry_clear( &impl->key_down );
        xaml_event_registry_clear( &impl->key_up );
        xaml_event_registry_clear( &impl->character_received );
        if (impl->inner) ICoreWindow_Release( impl->inner );
        free( impl );
    }
    return ref;
}
static HRESULT WINAPI xaml_cw_GetIids( ICoreWindow *iface, ULONG *count, IID **iids )
{
    struct xaml_core_window *impl = impl_from_cw_ICoreWindow( iface );
    if (impl->inner) return ICoreWindow_GetIids( impl->inner, count, iids );
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}
static HRESULT WINAPI xaml_cw_GetRuntimeClassName( ICoreWindow *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.UI.Core.CoreWindow", 26, name );
}
static HRESULT WINAPI xaml_cw_GetTrustLevel( ICoreWindow *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}
static HRESULT WINAPI xaml_cw_get_AutomationHostProvider( ICoreWindow *iface, IInspectable **value )
{
    struct xaml_core_window *impl = impl_from_cw_ICoreWindow( iface );
    return impl->inner ? ICoreWindow_get_AutomationHostProvider( impl->inner, value ) : E_NOTIMPL;
}
static HRESULT WINAPI xaml_cw_get_Bounds( ICoreWindow *iface, Rect *value )
{
    struct xaml_core_window *impl = impl_from_cw_ICoreWindow( iface );
    if (impl->inner) return ICoreWindow_get_Bounds( impl->inner, value );
    if (!value) return E_POINTER;
    value->X = 0; value->Y = 0; value->Width = 0; value->Height = 0;
    return S_OK;
}
static HRESULT WINAPI xaml_cw_get_CustomProperties( ICoreWindow *iface, __x_ABI_CWindows_CFoundation_CCollections_CIPropertySet **value )
{
    struct xaml_core_window *impl = impl_from_cw_ICoreWindow( iface );
    return impl->inner ? ICoreWindow_get_CustomProperties( impl->inner, value ) : E_NOTIMPL;
}
static HRESULT WINAPI xaml_cw_get_Dispatcher( ICoreWindow *iface, ICoreDispatcher **value )
{
    struct xaml_core_window *impl = impl_from_cw_ICoreWindow( iface );
    return impl->inner ? ICoreWindow_get_Dispatcher( impl->inner, value ) : E_NOTIMPL;
}
static HRESULT WINAPI xaml_cw_get_InputProperties( ICoreWindow *iface,
    __x_ABI_CWindows_CUI_CCore_CICoreWindowInputProperties **value )
{
    struct xaml_core_window *impl = impl_from_cw_ICoreWindow( iface );
    return impl->inner ? ICoreWindow_get_InputProperties( impl->inner, value ) : E_NOTIMPL;
}
static HRESULT WINAPI xaml_cw_get_FlowDirection( ICoreWindow *iface, CoreWindowFlowDirection *value )
{
    struct xaml_core_window *impl = impl_from_cw_ICoreWindow( iface );
    return impl->inner ? ICoreWindow_get_FlowDirection( impl->inner, value ) : E_NOTIMPL;
}
static HRESULT WINAPI xaml_cw_put_FlowDirection( ICoreWindow *iface, CoreWindowFlowDirection value )
{
    struct xaml_core_window *impl = impl_from_cw_ICoreWindow( iface );
    return impl->inner ? ICoreWindow_put_FlowDirection( impl->inner, value ) : E_NOTIMPL;
}
static HRESULT WINAPI xaml_cw_get_IsInputEnabled( ICoreWindow *iface, boolean *value )
{
    struct xaml_core_window *impl = impl_from_cw_ICoreWindow( iface );
    return impl->inner ? ICoreWindow_get_IsInputEnabled( impl->inner, value ) : E_NOTIMPL;
}
static HRESULT WINAPI xaml_cw_put_IsInputEnabled( ICoreWindow *iface, boolean value )
{
    struct xaml_core_window *impl = impl_from_cw_ICoreWindow( iface );
    return impl->inner ? ICoreWindow_put_IsInputEnabled( impl->inner, value ) : E_NOTIMPL;
}
static HRESULT WINAPI xaml_cw_get_PointerCursor( ICoreWindow *iface, ICoreCursor **value )
{
    struct xaml_core_window *impl = impl_from_cw_ICoreWindow( iface );
    return impl->inner ? ICoreWindow_get_PointerCursor( impl->inner, value ) : E_NOTIMPL;
}
static HRESULT WINAPI xaml_cw_put_PointerCursor( ICoreWindow *iface, ICoreCursor *value )
{
    struct xaml_core_window *impl = impl_from_cw_ICoreWindow( iface );
    return impl->inner ? ICoreWindow_put_PointerCursor( impl->inner, value ) : E_NOTIMPL;
}
static HRESULT WINAPI xaml_cw_get_PointerPosition( ICoreWindow *iface, Point *value )
{
    struct xaml_core_window *impl = impl_from_cw_ICoreWindow( iface );
    HWND hw;
    if (!value) return E_POINTER;
    if (impl->has_pos)
    {
        value->X = (float)impl->last_pos.x;
        value->Y = (float)impl->last_pos.y;
        return S_OK;
    }
    hw = xaml_cw_resolve_hwnd( impl );
    if (hw && IsWindow( hw ))
    {
        POINT pt;
        if (GetCursorPos( &pt ) && ScreenToClient( hw, &pt ))
        {
            value->X = (float)pt.x;
            value->Y = (float)pt.y;
            return S_OK;
        }
    }
    if (impl->inner) return ICoreWindow_get_PointerPosition( impl->inner, value );
    value->X = 0.0f; value->Y = 0.0f;
    return S_OK;
}
static HRESULT WINAPI xaml_cw_get_Visible( ICoreWindow *iface, boolean *value )
{
    struct xaml_core_window *impl = impl_from_cw_ICoreWindow( iface );
    return impl->inner ? ICoreWindow_get_Visible( impl->inner, value ) : E_NOTIMPL;
}
static HRESULT WINAPI xaml_cw_Activate( ICoreWindow *iface )
{
    struct xaml_core_window *impl = impl_from_cw_ICoreWindow( iface );
    return impl->inner ? ICoreWindow_Activate( impl->inner ) : S_OK;
}
static HRESULT WINAPI xaml_cw_Close( ICoreWindow *iface )
{
    struct xaml_core_window *impl = impl_from_cw_ICoreWindow( iface );
    return impl->inner ? ICoreWindow_Close( impl->inner ) : S_OK;
}
static HRESULT WINAPI xaml_cw_GetAsyncKeyState( ICoreWindow *iface, __x_ABI_CWindows_CSystem_CVirtualKey key, CoreVirtualKeyStates *state )
{
    struct xaml_core_window *impl = impl_from_cw_ICoreWindow( iface );
    return impl->inner ? ICoreWindow_GetAsyncKeyState( impl->inner, key, state ) : E_NOTIMPL;
}
static HRESULT WINAPI xaml_cw_GetKeyState( ICoreWindow *iface, __x_ABI_CWindows_CSystem_CVirtualKey key, CoreVirtualKeyStates *state )
{
    struct xaml_core_window *impl = impl_from_cw_ICoreWindow( iface );
    return impl->inner ? ICoreWindow_GetKeyState( impl->inner, key, state ) : E_NOTIMPL;
}
static HRESULT WINAPI xaml_cw_ReleasePointerCapture( ICoreWindow *iface )
{
    struct xaml_core_window *impl = impl_from_cw_ICoreWindow( iface );
    if (impl->has_capture)
    {
        ReleaseCapture();
        impl->has_capture = FALSE;
    }
    return S_OK;
}
static HRESULT WINAPI xaml_cw_SetPointerCapture( ICoreWindow *iface )
{
    struct xaml_core_window *impl = impl_from_cw_ICoreWindow( iface );
    HWND hw = xaml_cw_resolve_hwnd( impl );
    if (hw && IsWindow( hw ))
    {
        SetCapture( hw );
        impl->has_capture = TRUE;
    }
    return S_OK;
}
static HRESULT WINAPI xaml_cw_add_Activated( ICoreWindow *iface,
    __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CWindowActivatedEventArgs *h, EventRegistrationToken *c )
{
    struct xaml_core_window *impl = impl_from_cw_ICoreWindow( iface );
    return impl->inner ? ICoreWindow_add_Activated( impl->inner, h, c ) : E_NOTIMPL;
}
static HRESULT WINAPI xaml_cw_remove_Activated( ICoreWindow *iface, EventRegistrationToken c )
{
    struct xaml_core_window *impl = impl_from_cw_ICoreWindow( iface );
    return impl->inner ? ICoreWindow_remove_Activated( impl->inner, c ) : E_NOTIMPL;
}
static HRESULT WINAPI xaml_cw_add_AutomationProviderRequested( ICoreWindow *iface,
    __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CAutomationProviderRequestedEventArgs *h, EventRegistrationToken *c )
{
    struct xaml_core_window *impl = impl_from_cw_ICoreWindow( iface );
    return impl->inner ? ICoreWindow_add_AutomationProviderRequested( impl->inner, h, c ) : E_NOTIMPL;
}
static HRESULT WINAPI xaml_cw_remove_AutomationProviderRequested( ICoreWindow *iface, EventRegistrationToken c )
{
    struct xaml_core_window *impl = impl_from_cw_ICoreWindow( iface );
    return impl->inner ? ICoreWindow_remove_AutomationProviderRequested( impl->inner, c ) : E_NOTIMPL;
}
static void xaml_event_registry_set_inner_token( struct xaml_event_registry *reg,
    EventRegistrationToken *inner_tokens, EventRegistrationToken wrapper_token, EventRegistrationToken inner_token )
{
    unsigned int i;

    if (!reg || !inner_tokens || !wrapper_token.value) return;
    EnterCriticalSection( &xaml_event_cs );
    for (i = 0; i < ARRAY_SIZE( reg->entries ); i++)
    {
        if (reg->entries[i].handler && reg->entries[i].token == wrapper_token.value)
        {
            inner_tokens[i] = inner_token;
            break;
        }
    }
    LeaveCriticalSection( &xaml_event_cs );
}

static HRESULT xaml_event_registry_remove_with_inner( struct xaml_event_registry *reg,
    EventRegistrationToken *inner_tokens, EventRegistrationToken token,
    EventRegistrationToken *out_inner_token, BOOL *out_found )
{
    unsigned int i;

    if (out_inner_token) out_inner_token->value = 0;
    if (out_found) *out_found = FALSE;
    if (!reg) return E_INVALIDARG;
    EnterCriticalSection( &xaml_event_cs );
    for (i = 0; i < ARRAY_SIZE( reg->entries ); i++)
    {
        if (reg->entries[i].handler && reg->entries[i].token == token.value)
        {
            void *h = reg->entries[i].handler;
            reg->entries[i].handler = NULL;
            reg->entries[i].token = 0;
            if (inner_tokens)
            {
                if (out_inner_token) *out_inner_token = inner_tokens[i];
                inner_tokens[i].value = 0;
            }
            if (out_found) *out_found = TRUE;
            LeaveCriticalSection( &xaml_event_cs );
            IUnknown_Release( (IUnknown *)h );
            return S_OK;
        }
    }
    LeaveCriticalSection( &xaml_event_cs );
    return S_OK;
}

static HRESULT WINAPI xaml_cw_add_CharacterReceived( ICoreWindow *iface,
    __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CCharacterReceivedEventArgs *h, EventRegistrationToken *c )
{
    struct xaml_core_window *impl = impl_from_cw_ICoreWindow( iface );
    EventRegistrationToken tok = { 0 }, inner_tok = { 0 };
    HRESULT hr = xaml_event_registry_add( &impl->character_received, h, &tok );
    if (c) *c = tok;
    if (SUCCEEDED( hr ) && impl->inner && impl->inner->lpVtbl->add_CharacterReceived)
    {
        if (SUCCEEDED( impl->inner->lpVtbl->add_CharacterReceived( impl->inner, h, &inner_tok ) ))
            xaml_event_registry_set_inner_token( &impl->character_received, impl->character_received_inner_tokens, tok, inner_tok );
    }
    return hr;
}
static HRESULT WINAPI xaml_cw_remove_CharacterReceived( ICoreWindow *iface, EventRegistrationToken c )
{
    struct xaml_core_window *impl = impl_from_cw_ICoreWindow( iface );
    EventRegistrationToken inner_tok = { 0 };
    BOOL found = FALSE;
    HRESULT hr = xaml_event_registry_remove_with_inner( &impl->character_received, impl->character_received_inner_tokens, c, &inner_tok, &found );
    if (found && impl->inner && impl->inner->lpVtbl->remove_CharacterReceived)
        impl->inner->lpVtbl->remove_CharacterReceived( impl->inner, inner_tok );
    return hr;
}
static HRESULT WINAPI xaml_cw_add_Closed( ICoreWindow *iface,
    __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CCoreWindowEventArgs *h, EventRegistrationToken *c )
{
    struct xaml_core_window *impl = impl_from_cw_ICoreWindow( iface );
    return impl->inner ? ICoreWindow_add_Closed( impl->inner, h, c ) : E_NOTIMPL;
}
static HRESULT WINAPI xaml_cw_remove_Closed( ICoreWindow *iface, EventRegistrationToken c )
{
    struct xaml_core_window *impl = impl_from_cw_ICoreWindow( iface );
    return impl->inner ? ICoreWindow_remove_Closed( impl->inner, c ) : E_NOTIMPL;
}
static HRESULT WINAPI xaml_cw_add_InputEnabled( ICoreWindow *iface,
    __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CInputEnabledEventArgs *h, EventRegistrationToken *c )
{
    struct xaml_core_window *impl = impl_from_cw_ICoreWindow( iface );
    return impl->inner ? ICoreWindow_add_InputEnabled( impl->inner, h, c ) : E_NOTIMPL;
}
static HRESULT WINAPI xaml_cw_remove_InputEnabled( ICoreWindow *iface, EventRegistrationToken c )
{
    struct xaml_core_window *impl = impl_from_cw_ICoreWindow( iface );
    return impl->inner ? ICoreWindow_remove_InputEnabled( impl->inner, c ) : E_NOTIMPL;
}
static HRESULT WINAPI xaml_cw_add_KeyDown( ICoreWindow *iface,
    __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CKeyEventArgs *h, EventRegistrationToken *c )
{
    struct xaml_core_window *impl = impl_from_cw_ICoreWindow( iface );
    EventRegistrationToken tok = { 0 }, inner_tok = { 0 };
    HRESULT hr = xaml_event_registry_add( &impl->key_down, h, &tok );
    if (c) *c = tok;
    if (SUCCEEDED( hr ) && impl->inner && impl->inner->lpVtbl->add_KeyDown)
    {
        if (SUCCEEDED( impl->inner->lpVtbl->add_KeyDown( impl->inner, h, &inner_tok ) ))
            xaml_event_registry_set_inner_token( &impl->key_down, impl->key_down_inner_tokens, tok, inner_tok );
    }
    return hr;
}
static HRESULT WINAPI xaml_cw_remove_KeyDown( ICoreWindow *iface, EventRegistrationToken c )
{
    struct xaml_core_window *impl = impl_from_cw_ICoreWindow( iface );
    EventRegistrationToken inner_tok = { 0 };
    BOOL found = FALSE;
    HRESULT hr = xaml_event_registry_remove_with_inner( &impl->key_down, impl->key_down_inner_tokens, c, &inner_tok, &found );
    if (found && impl->inner && impl->inner->lpVtbl->remove_KeyDown)
        impl->inner->lpVtbl->remove_KeyDown( impl->inner, inner_tok );
    return hr;
}
static HRESULT WINAPI xaml_cw_add_KeyUp( ICoreWindow *iface,
    __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CKeyEventArgs *h, EventRegistrationToken *c )
{
    struct xaml_core_window *impl = impl_from_cw_ICoreWindow( iface );
    EventRegistrationToken tok = { 0 }, inner_tok = { 0 };
    HRESULT hr = xaml_event_registry_add( &impl->key_up, h, &tok );
    if (c) *c = tok;
    if (SUCCEEDED( hr ) && impl->inner && impl->inner->lpVtbl->add_KeyUp)
    {
        if (SUCCEEDED( impl->inner->lpVtbl->add_KeyUp( impl->inner, h, &inner_tok ) ))
            xaml_event_registry_set_inner_token( &impl->key_up, impl->key_up_inner_tokens, tok, inner_tok );
    }
    return hr;
}
static HRESULT WINAPI xaml_cw_remove_KeyUp( ICoreWindow *iface, EventRegistrationToken c )
{
    struct xaml_core_window *impl = impl_from_cw_ICoreWindow( iface );
    EventRegistrationToken inner_tok = { 0 };
    BOOL found = FALSE;
    HRESULT hr = xaml_event_registry_remove_with_inner( &impl->key_up, impl->key_up_inner_tokens, c, &inner_tok, &found );
    if (found && impl->inner && impl->inner->lpVtbl->remove_KeyUp)
        impl->inner->lpVtbl->remove_KeyUp( impl->inner, inner_tok );
    return hr;
}
static HRESULT WINAPI xaml_cw_add_PointerCaptureLost( ICoreWindow *iface,
    __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CPointerEventArgs *h, EventRegistrationToken *c )
{
    return xaml_event_registry_add( &impl_from_cw_ICoreWindow( iface )->pointer_capture_lost, h, c );
}
static HRESULT WINAPI xaml_cw_remove_PointerCaptureLost( ICoreWindow *iface, EventRegistrationToken c )
{
    return xaml_event_registry_remove( &impl_from_cw_ICoreWindow( iface )->pointer_capture_lost, c );
}
static HRESULT WINAPI xaml_cw_add_PointerEntered( ICoreWindow *iface,
    __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CPointerEventArgs *h, EventRegistrationToken *c )
{
    return xaml_event_registry_add( &impl_from_cw_ICoreWindow( iface )->pointer_entered, h, c );
}
static HRESULT WINAPI xaml_cw_remove_PointerEntered( ICoreWindow *iface, EventRegistrationToken c )
{
    return xaml_event_registry_remove( &impl_from_cw_ICoreWindow( iface )->pointer_entered, c );
}
static HRESULT WINAPI xaml_cw_add_PointerExited( ICoreWindow *iface,
    __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CPointerEventArgs *h, EventRegistrationToken *c )
{
    return xaml_event_registry_add( &impl_from_cw_ICoreWindow( iface )->pointer_exited, h, c );
}
static HRESULT WINAPI xaml_cw_remove_PointerExited( ICoreWindow *iface, EventRegistrationToken c )
{
    return xaml_event_registry_remove( &impl_from_cw_ICoreWindow( iface )->pointer_exited, c );
}
static HRESULT WINAPI xaml_cw_add_PointerMoved( ICoreWindow *iface,
    __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CPointerEventArgs *h, EventRegistrationToken *c )
{
    return xaml_event_registry_add( &impl_from_cw_ICoreWindow( iface )->pointer_moved, h, c );
}
static HRESULT WINAPI xaml_cw_remove_PointerMoved( ICoreWindow *iface, EventRegistrationToken c )
{
    return xaml_event_registry_remove( &impl_from_cw_ICoreWindow( iface )->pointer_moved, c );
}
static HRESULT WINAPI xaml_cw_add_PointerPressed( ICoreWindow *iface,
    __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CPointerEventArgs *h, EventRegistrationToken *c )
{
    return xaml_event_registry_add( &impl_from_cw_ICoreWindow( iface )->pointer_pressed, h, c );
}
static HRESULT WINAPI xaml_cw_remove_PointerPressed( ICoreWindow *iface, EventRegistrationToken c )
{
    return xaml_event_registry_remove( &impl_from_cw_ICoreWindow( iface )->pointer_pressed, c );
}
static HRESULT WINAPI xaml_cw_add_PointerReleased( ICoreWindow *iface,
    __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CPointerEventArgs *h, EventRegistrationToken *c )
{
    return xaml_event_registry_add( &impl_from_cw_ICoreWindow( iface )->pointer_released, h, c );
}
static HRESULT WINAPI xaml_cw_remove_PointerReleased( ICoreWindow *iface, EventRegistrationToken c )
{
    return xaml_event_registry_remove( &impl_from_cw_ICoreWindow( iface )->pointer_released, c );
}
static HRESULT WINAPI xaml_cw_add_TouchHitTesting( ICoreWindow *iface,
    __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CTouchHitTestingEventArgs *h, EventRegistrationToken *c )
{
    struct xaml_core_window *impl = impl_from_cw_ICoreWindow( iface );
    return impl->inner ? ICoreWindow_add_TouchHitTesting( impl->inner, h, c ) : E_NOTIMPL;
}
static HRESULT WINAPI xaml_cw_remove_TouchHitTesting( ICoreWindow *iface, EventRegistrationToken c )
{
    struct xaml_core_window *impl = impl_from_cw_ICoreWindow( iface );
    return impl->inner ? ICoreWindow_remove_TouchHitTesting( impl->inner, c ) : E_NOTIMPL;
}
static HRESULT WINAPI xaml_cw_add_PointerWheelChanged( ICoreWindow *iface,
    __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CPointerEventArgs *h, EventRegistrationToken *c )
{
    return xaml_event_registry_add( &impl_from_cw_ICoreWindow( iface )->pointer_wheel_changed, h, c );
}
static HRESULT WINAPI xaml_cw_remove_PointerWheelChanged( ICoreWindow *iface, EventRegistrationToken c )
{
    return xaml_event_registry_remove( &impl_from_cw_ICoreWindow( iface )->pointer_wheel_changed, c );
}
static HRESULT WINAPI xaml_cw_add_SizeChanged( ICoreWindow *iface,
    __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CWindowSizeChangedEventArgs *h, EventRegistrationToken *c )
{
    struct xaml_core_window *impl = impl_from_cw_ICoreWindow( iface );
    return impl->inner ? ICoreWindow_add_SizeChanged( impl->inner, h, c ) : E_NOTIMPL;
}
static HRESULT WINAPI xaml_cw_remove_SizeChanged( ICoreWindow *iface, EventRegistrationToken c )
{
    struct xaml_core_window *impl = impl_from_cw_ICoreWindow( iface );
    return impl->inner ? ICoreWindow_remove_SizeChanged( impl->inner, c ) : E_NOTIMPL;
}
static HRESULT WINAPI xaml_cw_add_VisibilityChanged( ICoreWindow *iface,
    __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CVisibilityChangedEventArgs *h, EventRegistrationToken *c )
{
    struct xaml_core_window *impl = impl_from_cw_ICoreWindow( iface );
    return impl->inner ? ICoreWindow_add_VisibilityChanged( impl->inner, h, c ) : E_NOTIMPL;
}
static HRESULT WINAPI xaml_cw_remove_VisibilityChanged( ICoreWindow *iface, EventRegistrationToken c )
{
    struct xaml_core_window *impl = impl_from_cw_ICoreWindow( iface );
    return impl->inner ? ICoreWindow_remove_VisibilityChanged( impl->inner, c ) : E_NOTIMPL;
}

static const ICoreWindowVtbl xaml_cw_vtbl =
{
    xaml_cw_QueryInterface,
    xaml_cw_AddRef,
    xaml_cw_Release,
    xaml_cw_GetIids,
    xaml_cw_GetRuntimeClassName,
    xaml_cw_GetTrustLevel,
    xaml_cw_get_AutomationHostProvider,
    xaml_cw_get_Bounds,
    xaml_cw_get_CustomProperties,
    xaml_cw_get_Dispatcher,
    xaml_cw_get_InputProperties,    xaml_cw_get_PointerCursor,
    xaml_cw_put_PointerCursor,
    xaml_cw_get_PointerPosition,
    xaml_cw_get_Visible,
    xaml_cw_Activate,
    xaml_cw_Close,
    xaml_cw_GetAsyncKeyState,
    xaml_cw_GetKeyState,
    xaml_cw_ReleasePointerCapture,
    xaml_cw_SetPointerCapture,
    xaml_cw_add_Activated,
    xaml_cw_remove_Activated,
    xaml_cw_add_AutomationProviderRequested,
    xaml_cw_remove_AutomationProviderRequested,
    xaml_cw_add_CharacterReceived,
    xaml_cw_remove_CharacterReceived,
    xaml_cw_add_Closed,
    xaml_cw_remove_Closed,
    xaml_cw_add_InputEnabled,
    xaml_cw_remove_InputEnabled,
    xaml_cw_add_KeyDown,
    xaml_cw_remove_KeyDown,
    xaml_cw_add_KeyUp,
    xaml_cw_remove_KeyUp,
    xaml_cw_add_PointerCaptureLost,
    xaml_cw_remove_PointerCaptureLost,
    xaml_cw_add_PointerEntered,
    xaml_cw_remove_PointerEntered,
    xaml_cw_add_PointerExited,
    xaml_cw_remove_PointerExited,
    xaml_cw_add_PointerMoved,
    xaml_cw_remove_PointerMoved,
    xaml_cw_add_PointerPressed,
    xaml_cw_remove_PointerPressed,
    xaml_cw_add_PointerReleased,
    xaml_cw_remove_PointerReleased,    xaml_cw_add_PointerWheelChanged,
    xaml_cw_remove_PointerWheelChanged,
    xaml_cw_add_SizeChanged,
    xaml_cw_remove_SizeChanged,
    xaml_cw_add_VisibilityChanged,
    xaml_cw_remove_VisibilityChanged,
    xaml_cw_add_TouchHitTesting,
    xaml_cw_remove_TouchHitTesting,
    xaml_cw_get_FlowDirection,
    xaml_cw_put_FlowDirection,
    xaml_cw_get_IsInputEnabled,
    xaml_cw_put_IsInputEnabled,
};

static HRESULT WINAPI xaml_cw_interop_QueryInterface( ICoreWindowInterop *iface, REFIID iid, void **out )
{
    return xaml_cw_QueryInterface( &impl_from_cw_Interop( iface )->ICoreWindow_iface, iid, out );
}
static ULONG WINAPI xaml_cw_interop_AddRef( ICoreWindowInterop *iface )
{
    return xaml_cw_AddRef( &impl_from_cw_Interop( iface )->ICoreWindow_iface );
}
static ULONG WINAPI xaml_cw_interop_Release( ICoreWindowInterop *iface )
{
    return xaml_cw_Release( &impl_from_cw_Interop( iface )->ICoreWindow_iface );
}
static HRESULT WINAPI xaml_cw_interop_GetWindowHandle( ICoreWindowInterop *iface, HWND *hwnd )
{
    struct xaml_core_window *impl = impl_from_cw_Interop( iface );
    ICoreWindowInterop *interop = NULL;
    HRESULT hr = E_NOINTERFACE;
    if (!hwnd) return E_POINTER;
    if (impl->hwnd && IsWindow( impl->hwnd ))
    {
        *hwnd = impl->hwnd;
        return S_OK;
    }
    if (impl->inner && SUCCEEDED( hr = ICoreWindow_QueryInterface( impl->inner, &IID_ICoreWindowInterop, (void **)&interop )) && interop)
    {
        hr = interop->lpVtbl->GetWindowHandle( interop, hwnd );
        interop->lpVtbl->Release( interop );
        if (SUCCEEDED(hr) && *hwnd && IsWindow( *hwnd ))
            impl->hwnd = *hwnd;
        return hr;
    }
    *hwnd = NULL;
    return hr;
}
static HRESULT WINAPI xaml_cw_interop_SetMessageHandled( ICoreWindowInterop *iface, boolean value )
{
    struct xaml_core_window *impl = impl_from_cw_Interop( iface );
    ICoreWindowInterop *interop = NULL;
    HRESULT hr = S_OK;
    if (impl->inner && SUCCEEDED( ICoreWindow_QueryInterface( impl->inner, &IID_ICoreWindowInterop, (void **)&interop )) && interop)
    {
        hr = interop->lpVtbl->SetMessageHandled( interop, value );
        interop->lpVtbl->Release( interop );
    }
    return hr;
}

static const ICoreWindowInteropVtbl xaml_cw_interop_vtbl =
{
    xaml_cw_interop_QueryInterface,
    xaml_cw_interop_AddRef,
    xaml_cw_interop_Release,
    xaml_cw_interop_GetWindowHandle,
    xaml_cw_interop_SetMessageHandled,
};

static ICoreWindow *wrap_core_window( ICoreWindow *inner )
{
    struct xaml_core_window *wrap;
    if (!inner) return NULL;
    if (is_store_app())
    {
        ICoreWindow_AddRef( inner );
        return inner;
    }
    if (inner->lpVtbl == &xaml_cw_vtbl)
    {
        ICoreWindow_AddRef( inner );
        return inner;
    }
    if (!(wrap = calloc( 1, sizeof(*wrap) ))) return inner;
    wrap->ICoreWindow_iface.lpVtbl = &xaml_cw_vtbl;
    wrap->ICoreWindowInterop_iface.lpVtbl = &xaml_cw_interop_vtbl;
    wrap->ref = 1;
    wrap->inner = inner;
    ICoreWindow_AddRef( inner );
    xaml_cw_resolve_hwnd( wrap );
    return &wrap->ICoreWindow_iface;
}


/* ---- CoreWindow KeyEventArgs / CharacterReceivedEventArgs ---- */
struct xaml_core_key_args
{
    __x_ABI_CWindows_CUI_CCore_CIKeyEventArgs IKeyEventArgs_iface;
    __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs ICoreWindowEventArgs_iface;
    LONG ref;
    __x_ABI_CWindows_CSystem_CVirtualKey key;
    __x_ABI_CWindows_CUI_CCore_CCorePhysicalKeyStatus status;
    boolean handled;
};

static inline struct xaml_core_key_args *impl_from_key_args( __x_ABI_CWindows_CUI_CCore_CIKeyEventArgs *iface )
{
    return CONTAINING_RECORD( iface, struct xaml_core_key_args, IKeyEventArgs_iface );
}
static inline struct xaml_core_key_args *impl_from_key_core_args( __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs *iface )
{
    return CONTAINING_RECORD( iface, struct xaml_core_key_args, ICoreWindowEventArgs_iface );
}

static HRESULT WINAPI cw_key_args_QI( __x_ABI_CWindows_CUI_CCore_CIKeyEventArgs *iface, REFIID iid, void **out )
{
    struct xaml_core_key_args *impl = impl_from_key_args( iface );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID___x_ABI_CWindows_CUI_CCore_CIKeyEventArgs ))
    {
        InterlockedIncrement( &impl->ref );
        *out = &impl->IKeyEventArgs_iface;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID___x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs ))
    {
        InterlockedIncrement( &impl->ref );
        *out = &impl->ICoreWindowEventArgs_iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI cw_key_args_AddRef( __x_ABI_CWindows_CUI_CCore_CIKeyEventArgs *iface )
{
    return InterlockedIncrement( &impl_from_key_args( iface )->ref );
}
static ULONG WINAPI cw_key_args_Release( __x_ABI_CWindows_CUI_CCore_CIKeyEventArgs *iface )
{
    struct xaml_core_key_args *impl = impl_from_key_args( iface );
    LONG ref = InterlockedDecrement( &impl->ref );
    if (!ref) free( impl );
    return ref;
}
static HRESULT WINAPI cw_key_args_GetIids( __x_ABI_CWindows_CUI_CCore_CIKeyEventArgs *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}
static HRESULT WINAPI cw_key_args_GetRuntimeClassName( __x_ABI_CWindows_CUI_CCore_CIKeyEventArgs *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.UI.Core.KeyEventArgs", 28, name );
}
static HRESULT WINAPI cw_key_args_GetTrustLevel( __x_ABI_CWindows_CUI_CCore_CIKeyEventArgs *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}
static HRESULT WINAPI cw_key_args_get_Key( __x_ABI_CWindows_CUI_CCore_CIKeyEventArgs *iface,
    __x_ABI_CWindows_CSystem_CVirtualKey *value )
{
    if (!value) return E_POINTER;
    *value = impl_from_key_args( iface )->key;
    return S_OK;
}
static HRESULT WINAPI cw_key_args_get_KeyStatus( __x_ABI_CWindows_CUI_CCore_CIKeyEventArgs *iface,
    __x_ABI_CWindows_CUI_CCore_CCorePhysicalKeyStatus *value )
{
    if (!value) return E_POINTER;
    *value = impl_from_key_args( iface )->status;
    return S_OK;
}
static const __x_ABI_CWindows_CUI_CCore_CIKeyEventArgsVtbl cw_key_args_vtbl =
{
    cw_key_args_QI,
    cw_key_args_AddRef,
    cw_key_args_Release,
    cw_key_args_GetIids,
    cw_key_args_GetRuntimeClassName,
    cw_key_args_GetTrustLevel,
    cw_key_args_get_Key,
    cw_key_args_get_KeyStatus,
};

static HRESULT WINAPI cw_key_core_args_QI( __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs *iface, REFIID iid, void **out )
{
    struct xaml_core_key_args *impl = impl_from_key_core_args( iface );
    return __x_ABI_CWindows_CUI_CCore_CIKeyEventArgs_QueryInterface( &impl->IKeyEventArgs_iface, iid, out );
}
static ULONG WINAPI cw_key_core_args_AddRef( __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs *iface )
{
    struct xaml_core_key_args *impl = impl_from_key_core_args( iface );
    return __x_ABI_CWindows_CUI_CCore_CIKeyEventArgs_AddRef( &impl->IKeyEventArgs_iface );
}
static ULONG WINAPI cw_key_core_args_Release( __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs *iface )
{
    struct xaml_core_key_args *impl = impl_from_key_core_args( iface );
    return __x_ABI_CWindows_CUI_CCore_CIKeyEventArgs_Release( &impl->IKeyEventArgs_iface );
}
static HRESULT WINAPI cw_key_core_args_GetIids( __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}
static HRESULT WINAPI cw_key_core_args_GetRuntimeClassName( __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.UI.Core.KeyEventArgs", 28, name );
}
static HRESULT WINAPI cw_key_core_args_GetTrustLevel( __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}
static HRESULT WINAPI cw_key_core_args_get_Handled( __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs *iface, boolean *value )
{
    if (!value) return E_POINTER;
    *value = impl_from_key_core_args( iface )->handled;
    return S_OK;
}
static HRESULT WINAPI cw_key_core_args_put_Handled( __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs *iface, boolean value )
{
    impl_from_key_core_args( iface )->handled = value;
    return S_OK;
}
static const __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgsVtbl cw_key_core_args_vtbl =
{
    cw_key_core_args_QI,
    cw_key_core_args_AddRef,
    cw_key_core_args_Release,
    cw_key_core_args_GetIids,
    cw_key_core_args_GetRuntimeClassName,
    cw_key_core_args_GetTrustLevel,
    cw_key_core_args_get_Handled,
    cw_key_core_args_put_Handled,
};

struct xaml_core_char_args
{
    __x_ABI_CWindows_CUI_CCore_CICharacterReceivedEventArgs ICharacterReceivedEventArgs_iface;
    __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs ICoreWindowEventArgs_iface;
    LONG ref;
    UINT32 code;
    __x_ABI_CWindows_CUI_CCore_CCorePhysicalKeyStatus status;
    boolean handled;
};

static inline struct xaml_core_char_args *impl_from_char_args( __x_ABI_CWindows_CUI_CCore_CICharacterReceivedEventArgs *iface )
{
    return CONTAINING_RECORD( iface, struct xaml_core_char_args, ICharacterReceivedEventArgs_iface );
}
static inline struct xaml_core_char_args *impl_from_char_core_args( __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs *iface )
{
    return CONTAINING_RECORD( iface, struct xaml_core_char_args, ICoreWindowEventArgs_iface );
}

static HRESULT WINAPI cw_char_args_QI( __x_ABI_CWindows_CUI_CCore_CICharacterReceivedEventArgs *iface, REFIID iid, void **out )
{
    struct xaml_core_char_args *impl = impl_from_char_args( iface );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID___x_ABI_CWindows_CUI_CCore_CICharacterReceivedEventArgs ))
    {
        InterlockedIncrement( &impl->ref );
        *out = &impl->ICharacterReceivedEventArgs_iface;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID___x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs ))
    {
        InterlockedIncrement( &impl->ref );
        *out = &impl->ICoreWindowEventArgs_iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI cw_char_args_AddRef( __x_ABI_CWindows_CUI_CCore_CICharacterReceivedEventArgs *iface )
{
    return InterlockedIncrement( &impl_from_char_args( iface )->ref );
}
static ULONG WINAPI cw_char_args_Release( __x_ABI_CWindows_CUI_CCore_CICharacterReceivedEventArgs *iface )
{
    struct xaml_core_char_args *impl = impl_from_char_args( iface );
    LONG ref = InterlockedDecrement( &impl->ref );
    if (!ref) free( impl );
    return ref;
}
static HRESULT WINAPI cw_char_args_GetIids( __x_ABI_CWindows_CUI_CCore_CICharacterReceivedEventArgs *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}
static HRESULT WINAPI cw_char_args_GetRuntimeClassName( __x_ABI_CWindows_CUI_CCore_CICharacterReceivedEventArgs *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.UI.Core.CharacterReceivedEventArgs", 42, name );
}
static HRESULT WINAPI cw_char_args_GetTrustLevel( __x_ABI_CWindows_CUI_CCore_CICharacterReceivedEventArgs *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}
static HRESULT WINAPI cw_char_args_get_KeyCode( __x_ABI_CWindows_CUI_CCore_CICharacterReceivedEventArgs *iface, UINT32 *value )
{
    if (!value) return E_POINTER;
    *value = impl_from_char_args( iface )->code;
    return S_OK;
}
static HRESULT WINAPI cw_char_args_get_KeyStatus( __x_ABI_CWindows_CUI_CCore_CICharacterReceivedEventArgs *iface,
    __x_ABI_CWindows_CUI_CCore_CCorePhysicalKeyStatus *value )
{
    if (!value) return E_POINTER;
    *value = impl_from_char_args( iface )->status;
    return S_OK;
}
static const __x_ABI_CWindows_CUI_CCore_CICharacterReceivedEventArgsVtbl cw_char_args_vtbl =
{
    cw_char_args_QI,
    cw_char_args_AddRef,
    cw_char_args_Release,
    cw_char_args_GetIids,
    cw_char_args_GetRuntimeClassName,
    cw_char_args_GetTrustLevel,
    cw_char_args_get_KeyCode,
    cw_char_args_get_KeyStatus,
};

static HRESULT WINAPI cw_char_core_args_QI( __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs *iface, REFIID iid, void **out )
{
    struct xaml_core_char_args *impl = impl_from_char_core_args( iface );
    return __x_ABI_CWindows_CUI_CCore_CICharacterReceivedEventArgs_QueryInterface( &impl->ICharacterReceivedEventArgs_iface, iid, out );
}
static ULONG WINAPI cw_char_core_args_AddRef( __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs *iface )
{
    struct xaml_core_char_args *impl = impl_from_char_core_args( iface );
    return __x_ABI_CWindows_CUI_CCore_CICharacterReceivedEventArgs_AddRef( &impl->ICharacterReceivedEventArgs_iface );
}
static ULONG WINAPI cw_char_core_args_Release( __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs *iface )
{
    struct xaml_core_char_args *impl = impl_from_char_core_args( iface );
    return __x_ABI_CWindows_CUI_CCore_CICharacterReceivedEventArgs_Release( &impl->ICharacterReceivedEventArgs_iface );
}
static HRESULT WINAPI cw_char_core_args_GetIids( __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}
static HRESULT WINAPI cw_char_core_args_GetRuntimeClassName( __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.UI.Core.CharacterReceivedEventArgs", 42, name );
}
static HRESULT WINAPI cw_char_core_args_GetTrustLevel( __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}
static HRESULT WINAPI cw_char_core_args_get_Handled( __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs *iface, boolean *value )
{
    if (!value) return E_POINTER;
    *value = impl_from_char_core_args( iface )->handled;
    return S_OK;
}
static HRESULT WINAPI cw_char_core_args_put_Handled( __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs *iface, boolean value )
{
    impl_from_char_core_args( iface )->handled = value;
    return S_OK;
}
static const __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgsVtbl cw_char_core_args_vtbl =
{
    cw_char_core_args_QI,
    cw_char_core_args_AddRef,
    cw_char_core_args_Release,
    cw_char_core_args_GetIids,
    cw_char_core_args_GetRuntimeClassName,
    cw_char_core_args_GetTrustLevel,
    cw_char_core_args_get_Handled,
    cw_char_core_args_put_Handled,
};

static void cw_fill_key_status( __x_ABI_CWindows_CUI_CCore_CCorePhysicalKeyStatus *status, UINT msg, LPARAM lparam )
{
    status->RepeatCount = LOWORD( lparam );
    status->ScanCode = (UINT16)((lparam >> 16) & 0xff);
    status->IsExtendedKey = !!(lparam & 0x01000000);
    status->IsMenuKeyDown = (GetKeyState( VK_MENU ) & 0x8000) != 0;
    status->WasKeyDown = (msg == WM_KEYDOWN);
    status->IsKeyReleased = (msg == WM_KEYUP);
}

static __x_ABI_CWindows_CUI_CCore_CIKeyEventArgs *xaml_core_key_args_create( WPARAM wparam, UINT msg, LPARAM lparam )
{
    struct xaml_core_key_args *impl = calloc( 1, sizeof(*impl) );
    if (!impl) return NULL;
    impl->IKeyEventArgs_iface.lpVtbl = &cw_key_args_vtbl;
    impl->ICoreWindowEventArgs_iface.lpVtbl = &cw_key_core_args_vtbl;
    impl->ref = 1;
    impl->key = (__x_ABI_CWindows_CSystem_CVirtualKey)wparam;
    cw_fill_key_status( &impl->status, msg, lparam );
    return &impl->IKeyEventArgs_iface;
}

static __x_ABI_CWindows_CUI_CCore_CICharacterReceivedEventArgs *xaml_core_char_args_create( WPARAM wparam, LPARAM lparam )
{
    struct xaml_core_char_args *impl = calloc( 1, sizeof(*impl) );
    if (!impl) return NULL;
    impl->ICharacterReceivedEventArgs_iface.lpVtbl = &cw_char_args_vtbl;
    impl->ICoreWindowEventArgs_iface.lpVtbl = &cw_char_core_args_vtbl;
    impl->ref = 1;
    impl->code = (UINT32)wparam;
    cw_fill_key_status( &impl->status, WM_KEYDOWN, lparam );
    return &impl->ICharacterReceivedEventArgs_iface;
}

static void xaml_cw_fire_key_event( struct xaml_core_window *cw, UINT msg, WPARAM wparam, LPARAM lparam )
{
    struct xaml_event_registry *reg;
    void *handlers[64];
    unsigned int i, count;

    if (!cw) return;
    reg = msg == WM_KEYDOWN ? &cw->key_down : msg == WM_KEYUP ? &cw->key_up : &cw->character_received;
    count = xaml_event_registry_snapshot( reg, handlers, ARRAY_SIZE( handlers ) );
    if (!count) return;

    if (msg == WM_CHAR)
    {
        __x_ABI_CWindows_CUI_CCore_CICharacterReceivedEventArgs *args = xaml_core_char_args_create( wparam, lparam );
        if (!args)
        {
            for (i = 0; i < count; i++) IUnknown_Release( (IUnknown *)handlers[i] );
            return;
        }
        for (i = 0; i < count; i++)
        {
            __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CCharacterReceivedEventArgs *h = handlers[i];
            __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CCharacterReceivedEventArgs_Invoke(
                h, &cw->ICoreWindow_iface, args );
            IUnknown_Release( (IUnknown *)h );
        }
        __x_ABI_CWindows_CUI_CCore_CICharacterReceivedEventArgs_Release( args );
    }
    else
    {
        __x_ABI_CWindows_CUI_CCore_CIKeyEventArgs *args = xaml_core_key_args_create( wparam, msg, lparam );
        if (!args)
        {
            for (i = 0; i < count; i++) IUnknown_Release( (IUnknown *)handlers[i] );
            return;
        }
        for (i = 0; i < count; i++)
        {
            __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CKeyEventArgs *h = handlers[i];
            __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CKeyEventArgs_Invoke(
                h, &cw->ICoreWindow_iface, args );
            IUnknown_Release( (IUnknown *)h );
        }
        __x_ABI_CWindows_CUI_CCore_CIKeyEventArgs_Release( args );
    }
}

/* Fire the handlers the app registered on the inner windows.ui CoreWindow
 * (store apps reach it through twinapi's view.CoreWindow, not the XAML
 * wrapper).  Resolved by name out of windows.ui, the same way the module
 * reaches the core-window creation entry points. */
static HRESULT notify_inner_core_window_key( HWND hwnd, UINT msg, WPARAM wparam )
{
    static HRESULT (WINAPI *pnotify)( HWND, UINT, WPARAM );
    HMODULE module;

    if (!pnotify)
    {
        module = GetModuleHandleW( L"windows.ui.dll" );
        if (!module) module = LoadLibraryW( L"windows.ui.dll" );
        if (!module) return E_FAIL;
        if (!(pnotify = (void *)GetProcAddress( module, "CoreWindow_NotifyKeyMsg" ))) return E_FAIL;
    }
    return pnotify( hwnd, msg, wparam );
}

static void xaml_cw_fire_pointer_event( struct xaml_core_window *cw, enum xaml_pointer_event_kind kind,
                                        const struct xaml_pointer_info *info )
{
    struct xaml_event_registry *reg = NULL;
    void *handlers[64];
    unsigned int i, count;
    __x_ABI_CWindows_CUI_CCore_CIPointerEventArgs *args;

    if (!cw) return;
    switch (kind)
    {
    case XAML_PTR_PRESSED:      reg = &cw->pointer_pressed; break;
    case XAML_PTR_MOVED:        reg = &cw->pointer_moved; break;
    case XAML_PTR_RELEASED:     reg = &cw->pointer_released; break;
    case XAML_PTR_WHEEL:        reg = &cw->pointer_wheel_changed; break;
    case XAML_PTR_ENTERED:      reg = &cw->pointer_entered; break;
    case XAML_PTR_EXITED:       reg = &cw->pointer_exited; break;
    case XAML_PTR_CAPTURE_LOST: reg = &cw->pointer_capture_lost; break;
    }
    if (!reg) return;
    count = xaml_event_registry_snapshot( reg, handlers, ARRAY_SIZE( handlers ) );
    if (!count) return;

    args = xaml_core_pointer_args_create( info );
    if (!args)
    {
        for (i = 0; i < count; i++) IUnknown_Release( (IUnknown *)handlers[i] );
        return;
    }
    for (i = 0; i < count; i++)
    {
        __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CPointerEventArgs *h = handlers[i];
        __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CPointerEventArgs_Invoke(
            h, &cw->ICoreWindow_iface, args );
        IUnknown_Release( (IUnknown *)h );
    }
    __x_ABI_CWindows_CUI_CCore_CIPointerEventArgs_Release( args );
}

/* The class activation interface IID of Windows.UI.Xaml.Application (the
 * Windows.UI.Xaml.Application "activatable" interface; the WinUI3 projection
 * Microsoft.UI.Xaml.Application shares it).  Not in the generated header; the
 * app's Mcg binder passes it to RoGetActivationFactory. */
static const IID IID_Windows_UI_Xaml_Application =
    { 0x06499997, 0xf7b4, 0x45fe, {0xb7, 0x63, 0x75, 0x77, 0xd1, 0xd3, 0xcb, 0x4a} };
static const GUID IID_IApplicationFactory_WinUI =
    { 0x93bbe361, 0xbe5a, 0x4ee3, { 0xb4, 0xa3, 0x95, 0x11, 0x8d, 0xc9, 0x7a, 0x89 } };
static const GUID IID_IApplication_WinUI =
    { 0x74b861a1, 0x7487, 0x46a9, { 0x9a, 0x6e, 0xc7, 0x8b, 0x51, 0x27, 0x26, 0xc5 } };
static const GUID IID_IWindow2 =
    { 0x74b861a1, 0x7487, 0x46a9, { 0x9a, 0x6e, 0xc7, 0x8b, 0x51, 0x27, 0x26, 0xc5 } };
static const GUID IID_IWindow2_WinUI =
    { 0x42febaa5, 0x1c32, 0x522a, { 0xa5, 0x91, 0x57, 0x61, 0x8c, 0x6f, 0x66, 0x5d } };
static const GUID IID_IApplication_751b =
    { 0x751b792e, 0x5772, 0x4488, { 0x8b, 0x87, 0xf5, 0x47, 0xfa, 0xa6, 0x44, 0x74 } };
static const GUID IID_IApplication_0191 =
    { 0x019104be, 0x522a, 0x5904, { 0xf5, 0x2f, 0xde, 0x72, 0x01, 0x04, 0x29, 0xe0 } };
static const GUID IID_IWindow_Mcg =
    { 0xb775ad7c, 0x18b8, 0x45ca, { 0xa1, 0xb0, 0xdc, 0x48, 0x3e, 0x4b, 0x10, 0x28 } };
static const GUID IID_IWindow_f722dcc2 =
    { 0xf722dcc2, 0x9827, 0x403d, { 0xaa, 0xed, 0xec, 0xca, 0x9a, 0xc1, 0x73, 0x98 } };
static const GUID IID_IWindow_96061c05 =
    { 0x96061c05, 0x2dba, 0x4d08, { 0xb0, 0xbd, 0x2b, 0x30, 0xa1, 0x31, 0xc6, 0xaa } };
static const GUID IID_IComponentConnector_x =
    { 0xad401812, 0xb091, 0x51d0, { 0xb9, 0x15, 0x2d, 0x68, 0x2c, 0xd2, 0xaf, 0x10 } };
static const GUID IID_IComponentConnector_UWP =
    { 0xf6790987, 0xe6e5, 0x47f2, { 0x92, 0xc6, 0xec, 0xcc, 0xe4, 0xba, 0x15, 0x9a } };
static const GUID IID_IUriRuntimeClass_x =
    { 0x9e365e57, 0x48b2, 0x4160, { 0x95, 0x6f, 0xc7, 0x38, 0x51, 0x20, 0xbb, 0xfc } };

/* CoreApplicationViewSource and View implementation
 *
 * View lifecycle invariants (WP1.11). The shell's contract, enforced by the XAML framework view on
 * Windows, is Initialize -> SetWindow -> Load -> Run -> Uninitialize, all on the thread that owns
 * the view, and exactly one window for the view's lifetime:
 *
 *   - SetWindow on a foreign thread returns RPC_E_WRONG_THREAD (0x8001010E);
 *   - SetWindow with a second, different window returns E_UNEXPECTED (0x8000FFFF) — Windows'
 *     DXamlCore::ConfigureCoreWindow returns exactly this on a double-configure and XAML
 *     fail-fasts on it, so refusing the second window is what keeps the process alive;
 *   - SetWindow with the same window is idempotent S_OK;
 *   - Load before SetWindow returns E_UNEXPECTED, Load twice is S_OK;
 *   - after Uninitialize the view is EXITING: SetWindow and Load return
 *     E_APPLICATION_VIEW_EXITING (0x8000001b), which is the value twinapi's brokered Activate
 *     hands back once CoreApplication::Run has returned.
 *
 * A NULL window in SetWindow is accepted (S_OK) and leaves the window-unset state, which is what
 * the unpackaged path has always passed when it could not create a CoreWindow; the view's Run
 * keeps its GetMessage fallback for that case.
 *
 * The builtins enforce this contract ONLY under WINE_XAML_STRICT_LIFECYCLE (set, not "0");
 * the default is the pre-WP1.11 permissive accept. Live bisect: the unpackaged WinUI3 client
 * (WhatsApp Desktop) activates and pumps the view across threads, and these refusals — our
 * invention for builtins, native XAML enforces through its own private paths, not this public
 * IFrameworkView — regressed it, blocking managed init before the WebView2 browser spawns.
 * The owner_tid and state recording stay live in both modes; permissive mode only TRACEs the
 * would-be violation and accepts. The mirror of this line is the WP1.11 contract block in
 * dlls/twinapi.appcore/core_application.c (twinapi's broker step is unaffected: bisect
 * exonerated it; its E_APPLICATION_VIEW_EXITING passthrough only fires in a brokered run).
 */

/* The mode latches on first use (one getenv per process); the env must be set in the process
 * environment before the first framework-view call, exactly like WINE_UWP_BROKERS. */
static BOOL strict_lifecycle_enabled( void )
{
    static int cached = -1;   /* latched on the first call: -1 unknown, 0 permissive, 1 strict */

    if (cached == -1)
    {
        const char *v = getenv( "WINE_XAML_STRICT_LIFECYCLE" );
        cached = v && strcmp( v, "0" ) != 0;
    }
    return cached;
}
enum view_state
{
    VIEW_STATE_CREATED = 0,
    VIEW_STATE_INITIALIZED,
    VIEW_STATE_WINDOW_SET,
    VIEW_STATE_LOADED,
    VIEW_STATE_RUNNING,
    VIEW_STATE_EXITING
};

struct framework_view
{
    IFrameworkView IFrameworkView_iface;
    LONG ref;
    ICoreWindow *window;
    DWORD owner_tid;
    enum view_state state;
};

static inline struct framework_view *impl_from_IFrameworkView( IFrameworkView *iface )
{
    return CONTAINING_RECORD( iface, struct framework_view, IFrameworkView_iface );
}

static HRESULT WINAPI view_QueryInterface( IFrameworkView *iface, REFIID iid, void **out )
{
    struct framework_view *impl = impl_from_IFrameworkView( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IFrameworkView ))
    {
        *out = &impl->IFrameworkView_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI view_AddRef( IFrameworkView *iface )
{
    struct framework_view *impl = impl_from_IFrameworkView( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p increasing refcount to %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI view_Release( IFrameworkView *iface )
{
    struct framework_view *impl = impl_from_IFrameworkView( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    TRACE( "iface %p decreasing refcount to %lu.\n", iface, ref );
    if (!ref)
    {
        if (impl->window)
        {
            struct xaml_core_window *cw = as_xaml_core_window( global_xaml_window.core_window );
            if ((global_xaml_window.core_window == impl->window || (cw && cw->inner == impl->window)) &&
                !global_xaml_window.target_hwnd)
            {
                ICoreWindow_Release( global_xaml_window.core_window );
                global_xaml_window.core_window = NULL;
            }
            ICoreWindow_Release( impl->window );
        }
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI view_GetIids( IFrameworkView *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI view_GetRuntimeClassName( IFrameworkView *iface, HSTRING *class_name )
{
    FIXME( "iface %p, class_name %p stub!\n", iface, class_name );
    return E_NOTIMPL;
}

static HRESULT WINAPI view_GetTrustLevel( IFrameworkView *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI view_Initialize( IFrameworkView *iface, ICoreApplicationView *application_view )
{
    struct framework_view *impl = impl_from_IFrameworkView( iface );
    EventRegistrationToken token;

    TRACE( "iface %p, application_view %p.\n", iface, application_view );

    impl->owner_tid = GetCurrentThreadId();
    impl->state = VIEW_STATE_INITIALIZED;

    if (application_view)
    {
        ICoreApplicationView_add_Activated( application_view, &app_view_activated_handler, &token );
    }
    return S_OK;
}

static HRESULT WINAPI view_SetWindow( IFrameworkView *iface, ICoreWindow *window )
{
    struct framework_view *impl = impl_from_IFrameworkView( iface );

    BOOL strict = strict_lifecycle_enabled();

    TRACE( "iface %p, window %p, state %u.\n", iface, window, impl->state );

    if (impl->state == VIEW_STATE_EXITING)
    {
        if (strict)
        {
            WARN( "view is exiting, returning E_APPLICATION_VIEW_EXITING.\n" );
            return E_APPLICATION_VIEW_EXITING;
        }
        TRACE( "lifecycle: SetWindow after Uninitialize (strict=off, accepted).\n" );
    }
    if (impl->owner_tid && impl->owner_tid != GetCurrentThreadId())
    {
        if (strict)
        {
            WARN( "SetWindow on tid %lu, owner tid is %lu, returning RPC_E_WRONG_THREAD.\n",
                  GetCurrentThreadId(), impl->owner_tid );
            return RPC_E_WRONG_THREAD;
        }
        TRACE( "lifecycle: SetWindow from foreign tid %lu, owner tid %lu (strict=off, accepted).\n",
               GetCurrentThreadId(), impl->owner_tid );
    }
    if (impl->window == window)
        return S_OK;   /* idempotent: configuring the same window again is fine */
    if (impl->window && window)
    {
        /* The window is configured exactly once. A second, different window is what Windows'
         * ConfigureCoreWindow answers E_UNEXPECTED for; the real XAML framework fail-fasts when
         * that value reaches it, so the app must never see a second window accepted. */
        if (strict)
        {
            WARN( "view already has window %p, refusing window %p, returning E_UNEXPECTED.\n",
                  impl->window, window );
            return E_UNEXPECTED;
        }
        TRACE( "lifecycle: SetWindow replaces window %p with %p (strict=off, accepted).\n",
               impl->window, window );
    }
    if (impl->window)
    {
        struct xaml_core_window *cw = as_xaml_core_window( global_xaml_window.core_window );
        if ((global_xaml_window.core_window == impl->window || (cw && cw->inner == impl->window)) &&
            !global_xaml_window.target_hwnd)
        {
            ICoreWindow_Release( global_xaml_window.core_window );
            global_xaml_window.core_window = NULL;
        }
        ICoreWindow_Release( impl->window );
    }
    impl->window = window;
    if (window) ICoreWindow_AddRef( window );
    if (window) impl->state = VIEW_STATE_WINDOW_SET;
    if (window && !global_xaml_window.core_window)
    {
        global_xaml_window.core_window = wrap_core_window( window );
    }
    return S_OK;
}

static HRESULT WINAPI view_Load( IFrameworkView *iface, HSTRING entry_point )
{
    struct framework_view *impl = impl_from_IFrameworkView( iface );

    BOOL strict = strict_lifecycle_enabled();

    TRACE( "iface %p, entry_point %s, state %u.\n", iface, debugstr_hstring( entry_point ), impl->state );

    if (impl->state == VIEW_STATE_EXITING)
    {
        if (strict)
        {
            WARN( "view is exiting, returning E_APPLICATION_VIEW_EXITING.\n" );
            return E_APPLICATION_VIEW_EXITING;
        }
        TRACE( "lifecycle: Load after Uninitialize (strict=off, accepted).\n" );
    }
    else if (impl->state < VIEW_STATE_WINDOW_SET)
    {
        if (strict)
        {
            WARN( "Load before SetWindow, returning E_UNEXPECTED.\n" );
            return E_UNEXPECTED;
        }
        TRACE( "lifecycle: Load before SetWindow (strict=off, accepted).\n" );
    }
    impl->state = VIEW_STATE_LOADED;   /* Load twice is S_OK; the state stays LOADED */
    return S_OK;
}

static HRESULT WINAPI view_Run( IFrameworkView *iface )
{
    struct framework_view *impl = impl_from_IFrameworkView( iface );
    ICoreDispatcher *dispatcher = NULL;
    HRESULT hr;
    MSG msg;

    TRACE( "iface %p, state %u.\n", iface, impl->state );

    impl->state = VIEW_STATE_RUNNING;

    /* Attach the swapchain visual to the CoreWindow's composition target and map the
     * top-level HWND now, before the app's Window.Current.Activate() lands. An app that
     * builds its page asynchronously (Calculator awaits its view model) leaves the window
     * unmapped and unpainted until that completes; presenting here gets a frame on screen
     * at Run() time. */
    xaml_window_present_current();

    if (impl->window)
    {
        hr = ICoreWindow_get_Dispatcher( impl->window, &dispatcher );
        if (SUCCEEDED(hr) && dispatcher)
        {
            /* Windows keeps Run alive for the lifetime of the view: each ProcessUntilQuit
             * pass drains the queue (WM_QUIT messages are consumed by nested loops), and
             * the view only exits when the CoreWindow closes. One pass returned Run early
             * and the process exited ~16 s after startup. */
            for (;;)
            {
                hr = ICoreDispatcher_ProcessEvents( dispatcher, CoreProcessEventsOption_ProcessUntilQuit );
                if (FAILED(hr)) break;
                if (!impl->window) break;
            }
            ICoreDispatcher_Release( dispatcher );
            return hr;
        }
    }

    while (GetMessageW( &msg, NULL, 0, 0 ))
    {
        TranslateMessage( &msg );
        DispatchMessageW( &msg );
    }
    return S_OK;
}

static HRESULT WINAPI view_Uninitialize( IFrameworkView *iface )
{
    struct framework_view *impl = impl_from_IFrameworkView( iface );

    TRACE( "iface %p.\n", iface );

    if (impl->window)
    {
        struct xaml_core_window *cw = as_xaml_core_window( global_xaml_window.core_window );
        if ((global_xaml_window.core_window == impl->window || (cw && cw->inner == impl->window)) &&
            !global_xaml_window.target_hwnd)
        {
            ICoreWindow_Release( global_xaml_window.core_window );
            global_xaml_window.core_window = NULL;
        }
        ICoreWindow_Release( impl->window );
        impl->window = NULL;
    }
    impl->state = VIEW_STATE_EXITING;   /* everything after Run's return reports E_APPLICATION_VIEW_EXITING */
    return S_OK;
}

static const struct IFrameworkViewVtbl view_vtbl =
{
    view_QueryInterface,
    view_AddRef,
    view_Release,
    view_GetIids,
    view_GetRuntimeClassName,
    view_GetTrustLevel,
    view_Initialize,
    view_SetWindow,
    view_Load,
    view_Run,
    view_Uninitialize,
};

struct view_source
{
    IFrameworkViewSource IFrameworkViewSource_iface;
    LONG ref;
};

static inline struct view_source *impl_from_IFrameworkViewSource( IFrameworkViewSource *iface )
{
    return CONTAINING_RECORD( iface, struct view_source, IFrameworkViewSource_iface );
}

static HRESULT WINAPI view_source_QueryInterface( IFrameworkViewSource *iface, REFIID iid, void **out )
{
    struct view_source *impl = impl_from_IFrameworkViewSource( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IFrameworkViewSource ))
    {
        *out = &impl->IFrameworkViewSource_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI view_source_AddRef( IFrameworkViewSource *iface )
{
    struct view_source *impl = impl_from_IFrameworkViewSource( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p increasing refcount to %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI view_source_Release( IFrameworkViewSource *iface )
{
    struct view_source *impl = impl_from_IFrameworkViewSource( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    TRACE( "iface %p decreasing refcount to %lu.\n", iface, ref );
    if (!ref) free( impl );
    return ref;
}

static HRESULT WINAPI view_source_GetIids( IFrameworkViewSource *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI view_source_GetRuntimeClassName( IFrameworkViewSource *iface, HSTRING *class_name )
{
    FIXME( "iface %p, class_name %p stub!\n", iface, class_name );
    return E_NOTIMPL;
}

static HRESULT WINAPI view_source_GetTrustLevel( IFrameworkViewSource *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI view_source_CreateView( IFrameworkViewSource *iface, IFrameworkView **view )
{
    struct framework_view *impl;

    TRACE( "iface %p, view %p.\n", iface, view );

    if (!view) return E_POINTER;

    impl = calloc( 1, sizeof(*impl) );
    if (!impl) return E_OUTOFMEMORY;

    impl->IFrameworkView_iface.lpVtbl = &view_vtbl;
    impl->ref = 1;

    *view = &impl->IFrameworkView_iface;
    return S_OK;
}

static const struct IFrameworkViewSourceVtbl view_source_vtbl =
{
    view_source_QueryInterface,
    view_source_AddRef,
    view_source_Release,
    view_source_GetIids,
    view_source_GetRuntimeClassName,
    view_source_GetTrustLevel,
    view_source_CreateView,
};

/* Application instance implementation */
static inline struct application *impl_from_IApplication( IApplication *iface )
{
    return CONTAINING_RECORD( iface, struct application, IApplication_iface );
}

static inline struct application *impl_from_inner_IInspectable( IInspectable *iface )
{
    return CONTAINING_RECORD( iface, struct application, inner_iface );
}

/* The Resources property shape.
 *
 * The app's WinUI3 binder probes the activated Application instance for its
 * Resources-property type set: IPropertySet {f9d8e8a9-...} and its projections
 * IMap`2<String,Object> {11d3b13a-180e-4789-a8be-7712882893e6} and
 * IMapView`2 {2f13c006-a03a-5f69-b090-75a43e33423e}. All of them answered
 * E_NOINTERFACE off the bare six-slot face and the app stalled before mapping
 * its window. Real Windows backs Application.Resources with a live
 * Windows.Foundation.Collections.PropertySet whose merged face (wintypes)
 * answers the whole set with one object, so create one lazily
 * (RoGetActivationFactory) per application instance and delegate. */
#define IID_IPropertySet IID___x_ABI_CWindows_CFoundation_CCollections_CIPropertySet
#define IID_IMap_HSTRING_IInspectable IID___FIMap_2_HSTRING_IInspectable
#define IID_IMapView_HSTRING_IInspectable IID___FIMapView_2_HSTRING_IInspectable

/* The runtime (pinterface) IIDs the app's CoreRT binder QIs are
 * IMap`2<String,Object> {11d3b13a-...} and IMapView`2<String,Object>
 * {2f13c006-...}. The MIDL projection constants in windows.foundation.h name
 * the C projection of the same shapes with different IIDs
 * (IID___FIMap_2_HSTRING_IInspectable = {1b0d3570-...}), so match both pairs. */
static const IID IID_pinterface_IMap_HSTRING_IInspectable =
    { 0x11d3b13a, 0x180e, 0x4789, {0xa8, 0xbe, 0x77, 0x12, 0x88, 0x28, 0x93, 0xe6} };
static const IID IID_pinterface_IMapView_HSTRING_IInspectable =
    { 0x2f13c006, 0xa03a, 0x5f69, {0xb0, 0x90, 0x75, 0xa4, 0x3e, 0x33, 0x42, 0x3e} };

static const GUID IID_IResourceDictionary_WinUI =
    { 0x1b690975, 0xa710, 0x5783, { 0xa6, 0xe1, 0x15, 0x83, 0x6f, 0x61, 0x86, 0xc2 } };
static const GUID IID_IResourceDictionary_UWP =
    { 0x44a9796f, 0x723e, 0x4fdf, { 0xa2, 0x18, 0x03, 0x3e, 0x75, 0xb0, 0xc0, 0x84 } };
static const GUID IID_IResourceDictionary_Mcg =
    { 0xc1ea4f24, 0xd6de, 0x4191, { 0x8e, 0x3a, 0xf4, 0x86, 0x01, 0xf7, 0x48, 0x9c } };
static const GUID IID_IResourceDictionaryFactory_UWP =
    { 0xea3639b5, 0x31b7, 0x4271, { 0x92, 0xc9, 0x7c, 0x95, 0x84, 0xa9, 0x1c, 0x22 } };
static const GUID IID_IResourceDictionaryFactory_WinUI =
    { 0xea22a48f, 0xab71, 0x56f6, { 0xa3, 0x92, 0xd8, 0x23, 0x10, 0xc8, 0xaa, 0x7b } };
static const GUID IID_IMap_Object_Object_WinUI =
    { 0x1c200999, 0x1a6d, 0x5f79, { 0xb6, 0x10, 0xf8, 0x5a, 0x2c, 0xd7, 0x77, 0xd8 } };
static const GUID IID_IMapView_Object_Object_WinUI =
    { 0x8e411a90, 0x5410, 0x5dd2, { 0x9b, 0x64, 0x25, 0xb5, 0xc5, 0xf0, 0xfc, 0x9f } };
static const GUID IID_IDictionary_Object_Object =
    { 0xf5f69427, 0x55ed, 0x5512, { 0x84, 0x29, 0xd4, 0xf6, 0x62, 0x6d, 0xfc, 0xdd } };
static const GUID IID_IMap_Object_Object_Old =
    { 0x9f03235a, 0xf3e2, 0x5cbe, { 0x81, 0x22, 0x21, 0xf7, 0xf4, 0xda, 0xd6, 0xf6 } };
static const GUID IID_IMapView_Object_Object_Old =
    { 0x28cb687b, 0xca64, 0x5c58, { 0xa0, 0xc5, 0xee, 0x73, 0x50, 0x15, 0x10, 0xc2 } };

struct xaml_resource_dictionary
{
    IInspectable IInspectable_iface; /* IResourceDictionary face */
    IInspectable IMap_iface;          /* IMap / IDictionary face */
    LONG ref;
};

static inline struct xaml_resource_dictionary *impl_from_rd( IInspectable *iface )
{
    return CONTAINING_RECORD( iface, struct xaml_resource_dictionary, IInspectable_iface );
}
static inline struct xaml_resource_dictionary *impl_from_rd_map( void *iface )
{
    return CONTAINING_RECORD( iface, struct xaml_resource_dictionary, IMap_iface );
}

static HRESULT WINAPI rd_generic_slot( void *iface, ... )
{
    return S_OK;
}

static HRESULT WINAPI rd_QueryInterface( IInspectable *iface, REFIID iid, void **out );
static ULONG WINAPI rd_AddRef( IInspectable *iface );
static ULONG WINAPI rd_Release( IInspectable *iface );
static HRESULT WINAPI rd_GetIids( IInspectable *iface, ULONG *count, IID **iids )
{ if (count) *count = 0; if (iids) *iids = NULL; return S_OK; }
static HRESULT WINAPI rd_GetRuntimeClassName( IInspectable *iface, HSTRING *name )
{ return WindowsCreateString( L"Microsoft.UI.Xaml.ResourceDictionary", 35, name ); }
static HRESULT WINAPI rd_GetTrustLevel( IInspectable *iface, TrustLevel *level )
{ if (level) *level = BaseTrust; return S_OK; }

static HRESULT WINAPI rd_map_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct xaml_resource_dictionary *impl = impl_from_rd_map( iface );
    return rd_QueryInterface( &impl->IInspectable_iface, iid, out );
}
static ULONG WINAPI rd_map_AddRef( void *iface )
{
    struct xaml_resource_dictionary *impl = impl_from_rd_map( iface );
    return rd_AddRef( &impl->IInspectable_iface );
}
static ULONG WINAPI rd_map_Release( void *iface )
{
    struct xaml_resource_dictionary *impl = impl_from_rd_map( iface );
    return rd_Release( &impl->IInspectable_iface );
}

static HRESULT WINAPI rd_QueryInterface( IInspectable *iface, REFIID iid, void **out )
{
    struct xaml_resource_dictionary *impl = impl_from_rd( iface );
    ERR( "rd_QueryInterface %p, iid %s\n", iface, debugstr_guid( iid ) );
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IPropertySet ) ||
        IsEqualGUID( iid, &IID_IResourceDictionary_WinUI ) ||
        IsEqualGUID( iid, &IID_IResourceDictionary_UWP ) ||
        IsEqualGUID( iid, &IID_IResourceDictionary_Mcg ))
    {
        *out = &impl->IInspectable_iface;
        rd_AddRef( iface );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IDictionary_Object_Object ) ||
        IsEqualGUID( iid, &IID_IMap_Object_Object_WinUI ) ||
        IsEqualGUID( iid, &IID_IMapView_Object_Object_WinUI ) ||
        IsEqualGUID( iid, &IID_IMap_Object_Object_Old ) ||
        IsEqualGUID( iid, &IID_IMapView_Object_Object_Old ))
    {
        *out = &impl->IMap_iface;
        rd_AddRef( iface );
        return S_OK;
    }
    ERR( "rd_QueryInterface iid %s not implemented, returning E_NOINTERFACE\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI rd_AddRef( IInspectable *iface )
{
    struct xaml_resource_dictionary *impl = impl_from_rd( iface );
    return InterlockedIncrement( &impl->ref );
}
static ULONG WINAPI rd_Release( IInspectable *iface )
{
    struct xaml_resource_dictionary *impl = impl_from_rd( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref) free( impl );
    return ref;
}

static HRESULT WINAPI rd_get_MergedDictionaries( void *iface, void **value )
{
    TRACE( "rd_get_MergedDictionaries iface %p, value %p\n", iface, value );
    if (value) *value = NULL;
    return S_OK;
}
static HRESULT WINAPI rd_get_Source( void *iface, void **value )
{
    if (value) *value = NULL;
    return S_OK;
}
static HRESULT WINAPI rd_set_Source( void *iface, void *value )
{
    return S_OK;
}
static HRESULT WINAPI rd_get_ThemeDictionaries( void *iface, void **value )
{
    if (value) *value = NULL;
    return S_OK;
}

static HRESULT WINAPI rd_map_Lookup( void *iface, void *key, void **value )
{
    struct xaml_resource_dictionary *impl = impl_from_rd_map( iface );
    TRACE( "rd_map_Lookup iface %p, key %p, value %p\n", iface, key, value );
    if (is_uwp_xaml_app())
        return uwp_rd_lookup( &impl->IInspectable_iface, key, value );
    if (value) *value = NULL;
    return 0x8000000b; /* E_BOUNDS */
}
static HRESULT WINAPI rd_map_get_Size( void *iface, UINT32 *size )
{
    if (size) *size = 0;
    return S_OK;
}
static HRESULT WINAPI rd_map_HasKey( void *iface, void *key, boolean *found )
{
    struct xaml_resource_dictionary *impl = impl_from_rd_map( iface );
    if (is_uwp_xaml_app())
    {
        if (found) *found = uwp_rd_haskey( &impl->IInspectable_iface, key );
        return S_OK;
    }
    if (found) *found = FALSE;
    return S_OK;
}
static HRESULT WINAPI rd_map_GetView( void *iface, void **view )
{
    if (view) *view = NULL;
    return S_OK;
}

static const void *rd_vtbl[256] =
{
    [0] = rd_QueryInterface,
    [1] = rd_AddRef,
    [2] = rd_Release,
    [3] = rd_GetIids,
    [4] = rd_GetRuntimeClassName,
    [5] = rd_GetTrustLevel,
    [10 ... 255] = rd_generic_slot,
    [6] = rd_get_MergedDictionaries,
    [7] = rd_get_Source,
    [8] = rd_set_Source,
    [9] = rd_get_ThemeDictionaries,
};

static const void *rd_map_vtbl[256] =
{
    [0] = rd_map_QueryInterface,
    [1] = rd_map_AddRef,
    [2] = rd_map_Release,
    [3] = rd_GetIids,
    [4] = rd_GetRuntimeClassName,
    [5] = rd_GetTrustLevel,
    [10 ... 255] = rd_generic_slot,
    [6] = rd_map_Lookup,
    [7] = rd_map_get_Size,
    [8] = rd_map_HasKey,
    [9] = rd_map_GetView,
};

static HRESULT xaml_resource_dictionary_create( void **out )
{
    struct xaml_resource_dictionary *impl;
    if (!out) return E_POINTER;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->IInspectable_iface.lpVtbl = (const struct IInspectableVtbl *)rd_vtbl;
    impl->IMap_iface.lpVtbl = (const struct IInspectableVtbl *)rd_map_vtbl;
    impl->ref = 1;
    *out = &impl->IInspectable_iface;
    return S_OK;
}

HRESULT xaml_resource_dictionary_create_for_uwp( void **out )
{
    return xaml_resource_dictionary_create( out );
}

/* ResourceDictionary Factory */
struct rd_factory
{
    const void **vtbl_act;
    const void **vtbl_fac;
};

static struct rd_factory rd_factory_obj;

static HRESULT WINAPI rd_factory_QueryInterface( void *iface, REFIID iid, void **out )
{
    TRACE( "iface %p, iid %s, out %p\n", iface, debugstr_guid( iid ), out );
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = &rd_factory_obj.vtbl_act;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IResourceDictionaryFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IResourceDictionaryFactory_WinUI ))
    {
        *out = &rd_factory_obj.vtbl_fac;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IResourceDictionary_UWP ) ||
        IsEqualGUID( iid, &IID_IResourceDictionary_WinUI ) ||
        IsEqualGUID( iid, &IID_IResourceDictionary_Mcg ))
    {
        return xaml_resource_dictionary_create( out );
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI rd_factory_AddRef( void *iface ) { return 2; }
static ULONG WINAPI rd_factory_Release( void *iface ) { return 1; }
static HRESULT WINAPI rd_factory_GetIids( void *iface, ULONG *count, IID **iids )
{
    IID *array;
    if (!count || !iids) return E_POINTER;
    array = CoTaskMemAlloc( sizeof(IID) * 2 );
    if (!array) return E_OUTOFMEMORY;
    array[0] = IID_IResourceDictionaryFactory_UWP;
    array[1] = IID_IResourceDictionaryFactory_WinUI;
    *count = 2;
    *iids = array;
    return S_OK;
}
static HRESULT WINAPI rd_factory_GetRuntimeClassName( void *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.UI.Xaml.ResourceDictionary", 34, name );
}
static HRESULT WINAPI rd_factory_GetTrustLevel( void *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}
static HRESULT WINAPI rd_factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    return xaml_resource_dictionary_create( (void **)instance );
}
static HRESULT WINAPI rd_factory_CreateInstance( void *iface, void *outer, void **inner, void **instance )
{
    HRESULT hr = xaml_resource_dictionary_create( instance );
    if (SUCCEEDED(hr) && inner)
    {
        *inner = *instance;
        IInspectable_AddRef( (IInspectable *)*inner );
    }
    return hr;
}

static const void *rd_act_vtbl[] =
{
    rd_factory_QueryInterface,
    rd_factory_AddRef,
    rd_factory_Release,
    rd_factory_GetIids,
    rd_factory_GetRuntimeClassName,
    rd_factory_GetTrustLevel,
    rd_factory_ActivateInstance,
};

static const void *rd_fac_vtbl[] =
{
    rd_factory_QueryInterface,
    rd_factory_AddRef,
    rd_factory_Release,
    rd_factory_GetIids,
    rd_factory_GetRuntimeClassName,
    rd_factory_GetTrustLevel,
    rd_factory_CreateInstance,
};

static struct rd_factory rd_factory_obj = { rd_act_vtbl, rd_fac_vtbl };
IActivationFactory *resource_dictionary_factory = (IActivationFactory *)&rd_factory_obj.vtbl_act;

static HRESULT resources_propget( struct application *impl, REFIID iid, void **out )
{
    ERR( "resources_propget impl %p, iid %s\n", impl, debugstr_guid( iid ) );
    if (!impl) return E_FAIL;
    if (!impl->resources)
    {
        xaml_resource_dictionary_create( (void **)&impl->resources );
    }
    if (impl->resources)
    {
        return IInspectable_QueryInterface( impl->resources, iid, out );
    }
    *out = NULL;
    return E_FAIL;
}

struct application_extended_vtbl
{
    /* IInspectable */
    HRESULT (WINAPI *QueryInterface)(void *, REFIID, void **);
    ULONG (WINAPI *AddRef)(void *);
    ULONG (WINAPI *Release)(void *);
    HRESULT (WINAPI *GetIids)(void *, ULONG *, IID **);
    HRESULT (WINAPI *GetRuntimeClassName)(void *, HSTRING *);
    HRESULT (WINAPI *GetTrustLevel)(void *, TrustLevel *);
    /* IApplication methods */
    HRESULT (WINAPI *get_Resources)(void *, void **);
    HRESULT (WINAPI *put_Resources)(void *, void *);
    HRESULT (WINAPI *add_UnhandledException)(void *, void *, EventRegistrationToken *);
    HRESULT (WINAPI *remove_UnhandledException)(void *, EventRegistrationToken);
    HRESULT (WINAPI *add_Suspending)(void *, void *, EventRegistrationToken *);
    HRESULT (WINAPI *remove_Suspending)(void *, EventRegistrationToken);
    HRESULT (WINAPI *add_Resuming)(void *, void *, EventRegistrationToken *);
    HRESULT (WINAPI *remove_Resuming)(void *, EventRegistrationToken);
    HRESULT (WINAPI *Exit)(void *);
    HRESULT (WINAPI *pad_slot15)(void *);
    void *pad[32];
};

static const struct application_extended_vtbl application_vtbl;
static const struct application_extended_vtbl application_inner_vtbl;
static const void *ms_application_vtbl[];

static HRESULT WINAPI application_stub_zero( void *iface, void *out )
{
    if (out) *(void **)out = NULL;
    return S_OK;
}
static HRESULT WINAPI application_stub_ok( void *iface )
{
    return S_OK;
}

static struct application *app_from_iface( void *iface )
{
    const void *vtbl = *(const void **)iface;
    if (vtbl == &application_vtbl)
        return CONTAINING_RECORD( iface, struct application, IApplication_iface );
    if (vtbl == ms_application_vtbl)
        return CONTAINING_RECORD( iface, struct application, ms_IApplication_iface );
    return impl_from_inner_IInspectable( (IInspectable *)iface );
}

static HRESULT WINAPI application_get_Resources( void *iface, void **value )
{
    struct application *impl = app_from_iface( iface );
    ERR( "application_get_Resources iface %p, impl %p\n", iface, impl );
    if (!value) return E_POINTER;
    return resources_propget( impl, &IID_IResourceDictionary_WinUI, value );
}

static HRESULT WINAPI application_put_Resources( void *iface, void *value )
{
    TRACE( "iface %p, value %p\n", iface, value );
    return S_OK;
}

static HRESULT WINAPI application_add_UnhandledException( void *iface, void *handler, EventRegistrationToken *token )
{
    TRACE( "iface %p, handler %p, token %p\n", iface, handler, token );
    if (token) token->value = 1;
    return S_OK;
}

static HRESULT WINAPI application_remove_UnhandledException( void *iface, EventRegistrationToken token )
{
    TRACE( "iface %p, token %llu\n", iface, (unsigned long long)token.value );
    return S_OK;
}

static HRESULT WINAPI application_add_Suspending( void *iface, void *handler, EventRegistrationToken *token )
{
    TRACE( "iface %p, handler %p, token %p\n", iface, handler, token );
    if (token) token->value = 1;
    return S_OK;
}

static HRESULT WINAPI application_remove_Suspending( void *iface, EventRegistrationToken token )
{
    TRACE( "iface %p, token %llu\n", iface, (unsigned long long)token.value );
    return S_OK;
}

static HRESULT WINAPI application_add_Resuming( void *iface, void *handler, EventRegistrationToken *token )
{
    TRACE( "iface %p, handler %p, token %p\n", iface, handler, token );
    if (token) token->value = 1;
    return S_OK;
}

static HRESULT WINAPI application_remove_Resuming( void *iface, EventRegistrationToken token )
{
    TRACE( "iface %p, token %llu\n", iface, (unsigned long long)token.value );
    return S_OK;
}

static HRESULT WINAPI application_Exit( void *iface )
{
    TRACE( "iface %p\n", iface );
    return S_OK;
}

static HRESULT WINAPI application_QueryInterface( IApplication *iface, REFIID iid, void **out )
{
    struct application *impl = impl_from_IApplication( iface );

    ERR( "application_QueryInterface iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (impl->outer &&
        !IsEqualGUID( iid, &IID_IApplication ) &&
        !IsEqualGUID( iid, &IID_IApplication_WinUI ) &&
        !IsEqualGUID( iid, &IID_IApplication_WinUI3 ) &&
        !IsEqualGUID( iid, &IID_IApplication_751b ) &&
        !IsEqualGUID( iid, &IID_IApplication_0191 ) &&
        !IsEqualGUID( iid, &IID_IWindow_Mcg ) &&
        !IsEqualGUID( iid, &IID_IWindow_f722dcc2 ) &&
        !IsEqualGUID( iid, &IID_IWindow_96061c05 ) &&
        !IsEqualGUID( iid, &IID_Windows_UI_Xaml_Application ))
        return IInspectable_QueryInterface( impl->outer, iid, out );

    if (IsEqualGUID( iid, &IID_IApplication_WinUI3 ))
    {
        *out = &impl->ms_IApplication_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IApplicationOverrides_UWP ))
    {
        *out = &impl->overrides_uwp_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IApplication ) ||
        IsEqualGUID( iid, &IID_IApplication_WinUI ) ||
        IsEqualGUID( iid, &IID_IApplication_751b ) ||
        IsEqualGUID( iid, &IID_IApplication_0191 ) ||
        IsEqualGUID( iid, &IID_IWindow_Mcg ) ||
        IsEqualGUID( iid, &IID_IWindow_f722dcc2 ) ||
        IsEqualGUID( iid, &IID_IWindow_96061c05 ) ||
        IsEqualGUID( iid, &IID_Windows_UI_Xaml_Application ))
    {
        *out = &impl->IApplication_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IApplicationFactory ) ||
        IsEqualGUID( iid, &IID_IApplicationFactory_WinUI ) ||
        IsEqualGUID( iid, &IID_IApplicationStatics ))
        return IActivationFactory_QueryInterface( application_factory, iid, out );

    if (IsEqualGUID( iid, &IID_IPropertySet ) ||
        IsEqualGUID( iid, &IID_IMap_HSTRING_IInspectable ) ||
        IsEqualGUID( iid, &IID_IMapView_HSTRING_IInspectable ) ||
        IsEqualGUID( iid, &IID_pinterface_IMap_HSTRING_IInspectable ) ||
        IsEqualGUID( iid, &IID_pinterface_IMapView_HSTRING_IInspectable ))
        return resources_propget( impl, iid, out );

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI application_AddRef( IApplication *iface )
{
    struct application *impl = impl_from_IApplication( iface );
    if (impl->outer)
        return IInspectable_AddRef( impl->outer );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI application_Release( IApplication *iface )
{
    struct application *impl = impl_from_IApplication( iface );
    ULONG ref;

    if (impl->outer)
        return IInspectable_Release( impl->outer );

    ref = InterlockedDecrement( &impl->ref );
    TRACE( "iface %p decreasing refcount to %lu.\n", iface, ref );
    if (!ref) free( impl );
    return ref;
}

static HRESULT WINAPI application_GetIids( IApplication *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI application_GetRuntimeClassName( IApplication *iface, HSTRING *class_name )
{
    return WindowsCreateString( L"Windows.UI.Xaml.Application",
                                wcslen( L"Windows.UI.Xaml.Application" ),
                                class_name );
}

static HRESULT WINAPI application_GetTrustLevel( IApplication *iface, TrustLevel *trust_level )
{
    if (!trust_level) return E_POINTER;
    *trust_level = BaseTrust;
    return S_OK;
}

static const struct application_extended_vtbl application_vtbl =
{
    (void *)application_QueryInterface,
    (void *)application_AddRef,
    (void *)application_Release,
    (void *)application_GetIids,
    (void *)application_GetRuntimeClassName,
    (void *)application_GetTrustLevel,
    application_get_Resources,
    application_put_Resources,
    application_add_UnhandledException,
    application_remove_UnhandledException,
    application_add_Suspending,
    application_remove_Suspending,
    application_add_Resuming,
    application_remove_Resuming,
    application_Exit,
    (void *)application_stub_ok,
    {
        application_stub_zero, application_stub_zero, application_stub_zero, application_stub_zero,
        application_stub_zero, application_stub_zero, application_stub_zero, application_stub_zero,
        application_stub_zero, application_stub_zero, application_stub_zero, application_stub_zero,
        application_stub_zero, application_stub_zero, application_stub_zero, application_stub_zero,
        application_stub_zero, application_stub_zero, application_stub_zero, application_stub_zero,
        application_stub_zero, application_stub_zero, application_stub_zero, application_stub_zero,
        application_stub_zero, application_stub_zero, application_stub_zero, application_stub_zero,
        application_stub_zero, application_stub_zero, application_stub_zero, application_stub_zero,
    }
};

static const GUID IID_IXamlMetadataProvider_WinUI =
    { 0xa96251f0, 0x2214, 0x5d53, { 0x87, 0x46, 0xce, 0x99, 0xa2, 0x59, 0x3c, 0xd7 } };
static const GUID IID_IXamlMetadataProvider_UWP =
    { 0xb3765d69, 0x68a5, 0x4b32, { 0x88, 0x61, 0xfd, 0xb9, 0x0c, 0x1f, 0x58, 0x36 } };

static HRESULT WINAPI application_inner_QueryInterface( IInspectable *iface, REFIID iid, void **out );
static ULONG WINAPI application_inner_AddRef( IInspectable *iface );
static ULONG WINAPI application_inner_Release( IInspectable *iface );
static HRESULT WINAPI application_inner_GetIids( IInspectable *iface, ULONG *iid_count, IID **iids );
static HRESULT WINAPI application_inner_GetRuntimeClassName( IInspectable *iface, HSTRING *class_name );
static HRESULT WINAPI application_inner_GetTrustLevel( IInspectable *iface, TrustLevel *trust_level );

static HRESULT WINAPI app_meta_GetXamlType( void *iface, void *type_name, void **out )
{ if (out) *out = NULL; return S_OK; }
static HRESULT WINAPI app_meta_GetXamlTypeByFullName( void *iface, HSTRING name, void **out )
{ if (out) *out = NULL; return S_OK; }
static HRESULT WINAPI app_meta_GetXmlnsDefinitions( void *iface, ULONG *count, void ***out )
{ if (count) *count = 0; if (out) *out = NULL; return S_OK; }

static HRESULT WINAPI app_meta_QueryInterface( void *iface, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IXamlMetadataProvider_WinUI ) ||
        IsEqualGUID( iid, &IID_IXamlMetadataProvider_UWP ))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI app_meta_AddRef( void *iface ) { return 2; }
static ULONG WINAPI app_meta_Release( void *iface ) { return 1; }
static HRESULT WINAPI app_meta_GetIids( void *iface, ULONG *count, IID **iids )
{ if (count) *count = 0; if (iids) *iids = NULL; return S_OK; }
static HRESULT WINAPI app_meta_GetRuntimeClassName( void *iface, HSTRING *name )
{ return WindowsCreateString( L"Windows.UI.Xaml.Markup.IXamlMetadataProvider", 43, name ); }
static HRESULT WINAPI app_meta_GetTrustLevel( void *iface, TrustLevel *level )
{ if (level) *level = BaseTrust; return S_OK; }

static const void *app_meta_provider_vtbl[] =
{
    app_meta_QueryInterface,
    app_meta_AddRef,
    app_meta_Release,
    app_meta_GetIids,
    app_meta_GetRuntimeClassName,
    app_meta_GetTrustLevel,
    app_meta_GetXamlType,           /* 6: GetXamlType */
    app_meta_GetXamlTypeByFullName, /* 7: GetXamlTypeByFullName */
    app_meta_GetXmlnsDefinitions,   /* 8: GetXmlnsDefinitions */
};
static struct { const void *vtbl; } app_meta_provider_obj = { app_meta_provider_vtbl };

static HRESULT WINAPI application_inner_QueryInterface( IInspectable *iface, REFIID iid, void **out )
{
    struct application *impl = impl_from_inner_IInspectable( iface );

    ERR( "application_inner_QueryInterface iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IApplication_WinUI3 ))
    {
        *out = &impl->ms_IApplication_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IApplicationOverrides_UWP ))
    {
        *out = &impl->overrides_uwp_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }

    if (!is_uwp_xaml_app() &&
        (IsEqualGUID( iid, &IID_IXamlMetadataProvider_WinUI ) ||
         IsEqualGUID( iid, &IID_IXamlMetadataProvider_UWP )))
    {
        *out = &app_meta_provider_obj;
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IApplication ) ||
        IsEqualGUID( iid, &IID_IApplication_WinUI ) ||
        IsEqualGUID( iid, &IID_IApplication_751b ) ||
        IsEqualGUID( iid, &IID_IApplication_0191 ) ||
        IsEqualGUID( iid, &IID_IWindow_Mcg ) ||
        IsEqualGUID( iid, &IID_IWindow_f722dcc2 ) ||
        IsEqualGUID( iid, &IID_IWindow_96061c05 ) ||
        IsEqualGUID( iid, &IID_Windows_UI_Xaml_Application ))
    {
        *out = &impl->inner_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI application_inner_AddRef( IInspectable *iface )
{
    struct application *impl = impl_from_inner_IInspectable( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p increasing refcount to %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI application_inner_Release( IInspectable *iface )
{
    struct application *impl = impl_from_inner_IInspectable( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    TRACE( "iface %p decreasing refcount to %lu.\n", iface, ref );
    if (!ref) free( impl );
    return ref;
}

static HRESULT WINAPI application_inner_GetIids( IInspectable *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI application_inner_GetRuntimeClassName( IInspectable *iface, HSTRING *class_name )
{
    return WindowsCreateString( L"Microsoft.UI.Xaml.Application",
                                wcslen( L"Microsoft.UI.Xaml.Application" ),
                                class_name );
}

static HRESULT WINAPI application_inner_GetTrustLevel( IInspectable *iface, TrustLevel *trust_level )
{
    if (!trust_level) return E_POINTER;
    *trust_level = BaseTrust;
    return S_OK;
}

static const struct application_extended_vtbl application_inner_vtbl =
{
    (void *)application_inner_QueryInterface,
    (void *)application_inner_AddRef,
    (void *)application_inner_Release,
    (void *)application_inner_GetIids,
    (void *)application_inner_GetRuntimeClassName,
    (void *)application_inner_GetTrustLevel,
    application_get_Resources,
    application_put_Resources,
    application_add_UnhandledException,
    application_remove_UnhandledException,
    application_add_Suspending,
    application_remove_Suspending,
    application_add_Resuming,
    application_remove_Resuming,
    application_Exit,
    (void *)application_stub_ok,
    {
        application_stub_zero, application_stub_zero, application_stub_zero, application_stub_zero,
        application_stub_zero, application_stub_zero, application_stub_zero, application_stub_zero,
        application_stub_zero, application_stub_zero, application_stub_zero, application_stub_zero,
        application_stub_zero, application_stub_zero, application_stub_zero, application_stub_zero,
        application_stub_zero, application_stub_zero, application_stub_zero, application_stub_zero,
        application_stub_zero, application_stub_zero, application_stub_zero, application_stub_zero,
        application_stub_zero, application_stub_zero, application_stub_zero, application_stub_zero,
        application_stub_zero, application_stub_zero, application_stub_zero, application_stub_zero,
    }
};

static inline struct application *impl_from_ms_IApplication( void *iface )
{
    return CONTAINING_RECORD( iface, struct application, ms_IApplication_iface );
}

static HRESULT WINAPI application_ms_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct application *impl = impl_from_ms_IApplication( iface );
    ERR( "application_ms_QueryInterface iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );
    return application_QueryInterface( &impl->IApplication_iface, iid, out );
}

static ULONG WINAPI application_ms_AddRef( void *iface )
{
    struct application *impl = impl_from_ms_IApplication( iface );
    return application_AddRef( &impl->IApplication_iface );
}

static ULONG WINAPI application_ms_Release( void *iface )
{
    struct application *impl = impl_from_ms_IApplication( iface );
    return application_Release( &impl->IApplication_iface );
}

static HRESULT WINAPI ms_application_GetRuntimeClassName( void *iface, HSTRING *class_name )
{
    return WindowsCreateString( L"Microsoft.UI.Xaml.Application",
                                wcslen( L"Microsoft.UI.Xaml.Application" ),
                                class_name );
}

static const void *ms_application_vtbl[] =
{
    application_ms_QueryInterface,        /* 0: QI */
    application_ms_AddRef,                /* 1: AddRef */
    application_ms_Release,               /* 2: Release */
    application_GetIids,                  /* 3: GetIids */
    ms_application_GetRuntimeClassName,   /* 4: GetRuntimeClassName */
    application_GetTrustLevel,            /* 5: GetTrustLevel */
    application_get_Resources,            /* 6: get_Resources */
    application_put_Resources,            /* 7: set_Resources */
    application_add_UnhandledException,   /* 8: add_UnhandledException */
    application_remove_UnhandledException,/* 9: remove_UnhandledException */
    application_stub_zero,                /* 10: get_HighContrastAdjustment */
    application_stub_ok,                  /* 11: set_HighContrastAdjustment */
    application_stub_zero,                /* 12: get_RequestedTheme */
    application_stub_ok,                  /* 13: set_RequestedTheme */
    application_get_Resources,            /* 14: get_Resources */
    application_put_Resources,            /* 15: set_Resources */
    application_Exit,                     /* 16: Exit */
    application_stub_zero,                /* 17 */
    application_stub_zero, application_stub_zero, application_stub_zero, application_stub_zero,
    application_stub_zero, application_stub_zero, application_stub_zero, application_stub_zero,
    application_stub_zero, application_stub_zero, application_stub_zero, application_stub_zero,
    application_stub_zero, application_stub_zero,
};

static inline struct application *impl_from_overrides_uwp( void *iface )
{
    return CONTAINING_RECORD( iface, struct application, overrides_uwp_iface );
}

static HRESULT WINAPI application_overrides_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct application *impl = impl_from_overrides_uwp( iface );
    return application_inner_QueryInterface( &impl->inner_iface, iid, out );
}

static ULONG WINAPI application_overrides_AddRef( void *iface )
{
    struct application *impl = impl_from_overrides_uwp( iface );
    return application_inner_AddRef( &impl->inner_iface );
}

static ULONG WINAPI application_overrides_Release( void *iface )
{
    struct application *impl = impl_from_overrides_uwp( iface );
    return application_inner_Release( &impl->inner_iface );
}

static const void *application_overrides_uwp_vtbl[] =
{
    application_overrides_QueryInterface,   /* 0: QI */
    application_overrides_AddRef,           /* 1: AddRef */
    application_overrides_Release,          /* 2: Release */
    application_GetIids,                    /* 3: GetIids */
    application_GetRuntimeClassName,        /* 4: GetRuntimeClassName */
    application_GetTrustLevel,              /* 5: GetTrustLevel */
    application_stub_ok,                    /* 6: OnActivated */
    application_stub_ok,                    /* 7: OnLaunched */
    application_stub_ok,                    /* 8: OnFileActivated */
    application_stub_ok,                    /* 9: OnSearchActivated */
    application_stub_ok,                    /* 10: OnShareTargetActivated */
    application_stub_ok,                    /* 11: OnFileOpenPickerActivated */
    application_stub_ok,                    /* 12: OnFileSavePickerActivated */
    application_stub_ok,                    /* 13: OnCachedFileUpdaterActivated */
    application_stub_ok,                    /* 14: OnWindowCreated */
    application_stub_ok, application_stub_ok, application_stub_ok, application_stub_ok,
    application_stub_ok, application_stub_ok, application_stub_ok, application_stub_ok,
    application_stub_ok, application_stub_ok, application_stub_ok, application_stub_ok,
    application_stub_ok, application_stub_ok, application_stub_ok, application_stub_ok,
};

/* Application factory and statics */
struct application_factory
{
    IActivationFactory IActivationFactory_iface;
    IApplicationStatics IApplicationStatics_iface;
    IApplicationStatics ms_IApplicationStatics_iface;
    IApplicationFactory IApplicationFactory_iface;
    IFrameworkViewSource IFrameworkViewSource_iface;
    LONG ref;
};

static inline struct application_factory *impl_from_IActivationFactory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct application_factory, IActivationFactory_iface );
}

static inline struct application_factory *impl_from_IApplicationStatics( IApplicationStatics *iface )
{
    return CONTAINING_RECORD( iface, struct application_factory, IApplicationStatics_iface );
}

static inline struct application_factory *impl_from_ms_IApplicationStatics( IApplicationStatics *iface )
{
    return CONTAINING_RECORD( iface, struct application_factory, ms_IApplicationStatics_iface );
}

static inline struct application_factory *impl_from_IApplicationFactory( IApplicationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct application_factory, IApplicationFactory_iface );
}

/* Windows.UI.Xaml.Window implementation */
static const GUID IID_IWindowStatics =
    { 0x93bbe361, 0xbe5a, 0x4ee3, { 0xb4, 0xa3, 0x95, 0x11, 0x8d, 0xc9, 0x7a, 0x89 } };
static const GUID IID_IWindowStatics2 =
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
static ICoreWindow *ensure_core_window( struct xaml_window *impl )
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
        IsEqualGUID( iid, &IID_IWindow_Mcg ) ||
        IsEqualGUID( iid, &IID_IWindow_f722dcc2 ) ||
        IsEqualGUID( iid, &IID_IWindow_96061c05 ))
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
        static const GUID IID_IWindowPrivate =
            { 0x06636c29, 0x5a17, 0x458d, { 0x8e, 0xa2, 0x24, 0x22, 0xd9, 0x97, 0xa9, 0x22 } };
        if (IsEqualGUID( iid, &IID_IWindowPrivate ))
        {
            *out = &impl->IWindowPrivate_iface;
            IInspectable_AddRef( (IInspectable *)*out );
            return S_OK;
        }
    }

    /* Windows.UI.Xaml.Window v2 (titlebar/Compositor contract, {d384759f-...}):
     * TitleBar..ctor QIs Window.Current for it and casts the result to
     * Windows.UI.Xaml.Window, so the returned face must answer that QI too. */
    {
        static const GUID IID_IWindow_v2 =
            { 0xd384759f, 0x34f6, 0x4482, { 0x84, 0x35, 0xf5, 0x52, 0xf9, 0xb2, 0x4c, 0xc8 } };
        if (IsEqualGUID( iid, &IID_IWindow_v2 ))
        {
            *out = &impl->ms_IWindow_iface;
            IInspectable_AddRef( (IInspectable *)*out );
            return S_OK;
        }
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
    const WCHAR *cls = xaml_get_app_profile()->use_winui3_window_class ? L"Microsoft.UI.Xaml.Window" : L"Windows.UI.Xaml.Window";
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

/* AppModel package identity, not process-name sniffing: the deployment record
 * (AppModel\Exes\<image>, AppModel\Packages\<FullName>, seeded per prefix and
 * read by kernelbase's GetCurrentPackage*) is the source of truth. The
 * display name comes from the package manifest's uap:VisualElements
 * DisplayName, with ms-resource: keys resolved from the package
 * resources.pri (the same mrm_pri2 reader twinapi.appcore uses). */

struct xaml_package
{
    BOOL is_packaged;
    WCHAR full_name[512];
    WCHAR family_name[512];
    WCHAR installed_location[MAX_PATH];
    WCHAR display_name[512];
};

static struct xaml_package xaml_package;
static INIT_ONCE xaml_package_once = INIT_ONCE_STATIC_INIT;
static pri_index *xaml_package_pri;

static const HKEY xaml_reg_roots[] = { HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE };
static const WCHAR xaml_appmodel_key[] = L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppModel";

static BOOL xaml_read_reg_str( HKEY root, const WCHAR *subkey, const WCHAR *value, WCHAR *buf, DWORD cch )
{
    WCHAR path[640];
    HKEY hkey;
    DWORD type = 0, size;
    LONG ret;

    lstrcpyW( path, xaml_appmodel_key );
    if (subkey && subkey[0]) lstrcatW( path, L"\\" ), lstrcatW( path, subkey );
    if (RegOpenKeyExW( root, path, 0, KEY_READ, &hkey )) return FALSE;
    size = cch * sizeof(*buf);
    ret = RegQueryValueExW( hkey, value, NULL, &type, (BYTE *)buf, &size );
    RegCloseKey( hkey );
    if (ret != ERROR_SUCCESS || type != REG_SZ) return FALSE;
    buf[min( size / sizeof(*buf), cch - 1 )] = 0;
    return buf[0] != 0;
}

/* InstalledLocation: WINE_PACKAGE_PATH env, then Packages\<FullName>\PackagePath
 * (PackageRootPath accepted as a synonym), HKCU before HKLM. May stay empty;
 * callers fall back to the exe directory, which is the package dir for UWP. */
static void xaml_package_resolve_location( struct xaml_package *pkg )
{
    WCHAR subkey[600];
    SIZE_T i;

    if (GetEnvironmentVariableW( L"WINE_PACKAGE_PATH", pkg->installed_location,
                                 ARRAY_SIZE( pkg->installed_location ) ) && pkg->installed_location[0])
        return;
    pkg->installed_location[0] = 0;
    if (!pkg->full_name[0]) return;

    lstrcpyW( subkey, L"Packages\\" );
    lstrcatW( subkey, pkg->full_name );
    for (i = 0; i < ARRAY_SIZE( xaml_reg_roots ); i++)
    {
        static const WCHAR *const values[] = { L"PackagePath", L"PackageRootPath" };
        SIZE_T j;
        for (j = 0; j < ARRAY_SIZE( values ); j++)
            if (xaml_read_reg_str( xaml_reg_roots[i], subkey, values[j],
                                   pkg->installed_location, ARRAY_SIZE( pkg->installed_location ) ))
                return;
    }
}

static void xml_unescape( WCHAR *s )
{
    static const struct { const WCHAR *ent; WCHAR ch; } ents[] =
    {
        { L"&amp;", L'&' }, { L"&lt;", L'<' }, { L"&gt;", L'>' }, { L"&quot;", L'"' }, { L"&apos;", L'\'' },
    };
    SIZE_T i;

    for (i = 0; i < ARRAY_SIZE( ents ); i++)
    {
        WCHAR *p;
        while ((p = wcsstr( s, ents[i].ent )))
        {
            *p = ents[i].ch;
            memmove( p + 1, p + lstrlenW( ents[i].ent ),
                     (lstrlenW( p + lstrlenW( ents[i].ent ) ) + 1) * sizeof(WCHAR) );
        }
    }
}

/* <uap:VisualElements DisplayName="..." ...> attribute from AppxManifest.xml
 * in dir. Returns FALSE when the file or attribute is absent. */
static BOOL xaml_manifest_display_name( const WCHAR *dir, WCHAR *out, DWORD cch )
{
    static const WCHAR manifest_name[] = L"\\AppxManifest.xml";
    WCHAR path[MAX_PATH + 32];
    char *raw;
    WCHAR *xml;
    DWORD size, read, wlen;
    HANDLE file;
    BOOL ok = FALSE;

    lstrcpyW( path, dir );
    lstrcatW( path, manifest_name );
    if ((file = CreateFileW( path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL )) == INVALID_HANDLE_VALUE)
    {
        TRACE( "manifest open %s failed, err %u\n", debugstr_w( path ), GetLastError() );
        return FALSE;
    }
    size = GetFileSize( file, NULL );
    if (!size || size == INVALID_FILE_SIZE || !(raw = malloc( size + 2 )))
    {
        CloseHandle( file );
        return FALSE;
    }
    ReadFile( file, raw, size, &read, NULL );
    CloseHandle( file );
    raw[read] = 0;
    UINT cp = CP_UTF8;
    wlen = MultiByteToWideChar( cp, 0, raw, read, NULL, 0 );
    if (!wlen
            && !(wlen = MultiByteToWideChar( cp = CP_UTF8, MB_USEGLYPHCHARS, raw, read, NULL, 0 ))
            && !(wlen = MultiByteToWideChar( cp = CP_UNIXCP, 0, raw, read, NULL, 0 )))
    {
        free( raw );
        return FALSE;
    }
    if (!(xml = malloc( (wlen + 1) * sizeof(*xml) )))
    {
        free( raw );
        return FALSE;
    }
    MultiByteToWideChar( cp, cp == CP_UTF8 ? MB_USEGLYPHCHARS : 0, raw, read, xml, wlen );
    xml[wlen] = 0;
    free( raw );

    {
        WCHAR *tag = wcsstr( xml, L"uap:VisualElements" );
        WCHAR *end, *attr;
        if (tag && (end = wcschr( tag, L'>' )) && (attr = wcsstr( tag, L"DisplayName=\"" )) && attr < end)
        {
            WCHAR *val = attr + lstrlenW( L"DisplayName=\"" );
            WCHAR *close = wcschr( val, L'"' );
            if (close)
            {
                DWORD len = min( (DWORD)(close - val), cch - 1 );
                memcpy( out, val, len * sizeof(WCHAR) );
                out[len] = 0;
                xml_unescape( out );
                ok = out[0] != 0;
            }
        }
    }
    free( xml );
    return ok;
}

static pri_index *xaml_package_open_pri( struct xaml_package *pkg )
{
    static const WCHAR pri_name[] = L"\\resources.pri";
    WCHAR dir[MAX_PATH], path[MAX_PATH + 32];
    DWORD len;

    if (pkg->installed_location[0])
    {
        lstrcpyW( path, pkg->installed_location );
        lstrcatW( path, pri_name );
        if ((xaml_package_pri = pri_load( path ))) return xaml_package_pri;
    }
    len = GetModuleFileNameW( NULL, dir, ARRAY_SIZE( dir ) );
    if (len && len < ARRAY_SIZE( dir ) && wcsrchr( dir, L'\\' ))
    {
        *wcsrchr( dir, L'\\' ) = 0;
        lstrcpyW( path, dir );
        lstrcatW( path, pri_name );
        xaml_package_pri = pri_load( path );
    }
    return xaml_package_pri;
}

/* DisplayName="ms-resource:AppName" resolves through the package resource map:
 * bare key, then Resources\ and Files\ scopes (same order as twinapi's
 * ResourceLoader). Leaves display_name as the bare key on a miss. */
static void xaml_package_resolve_ms_resource( struct xaml_package *pkg )
{
    static const WCHAR ms_resource[] = L"ms-resource:";
    static const WCHAR *const scopes[] = { L"", L"Resources\\", L"Files\\" };
    WCHAR key[512], full[600], namebuf[512];
    WCHAR *val = NULL;
    SIZE_T i;

    if (wcsncmp( pkg->display_name, ms_resource, ARRAY_SIZE( ms_resource ) - 1 )) return;

    lstrcpyW( key, pkg->display_name + ARRAY_SIZE( ms_resource ) - 1 );
    if (key[0] == L'/' && key[1] == L'/') lstrcpyW( key, key + 2 );
    if (key[0] == L'/') lstrcpyW( key, key + 1 );
    for (i = 0; key[i]; i++) if (key[i] == L'/') key[i] = L'\\';

    if (xaml_package_open_pri( pkg ))
    {
        for (i = 0; i < ARRAY_SIZE( scopes ); i++)
        {
            lstrcpyW( full, scopes[i] );
            lstrcatW( full, key );
            if (pri_lookup( xaml_package_pri, full, NULL, 0, namebuf, ARRAY_SIZE( namebuf ),
                            &val, NULL, NULL ))
            {
                lstrcpynW( pkg->display_name, val, ARRAY_SIZE( pkg->display_name ) );
                pri_free_result( val, NULL );
                return;
            }
        }
    }
    lstrcpynW( pkg->display_name, key, ARRAY_SIZE( pkg->display_name ) );
}

static BOOL WINAPI xaml_package_init( INIT_ONCE *once, void *param, void **context )
{
    struct xaml_package *pkg = &xaml_package;
    WCHAR subkey[600];
    UINT32 len;
    SIZE_T i;

    pkg->full_name[0] = pkg->family_name[0] = 0;
    len = ARRAY_SIZE( pkg->full_name );
    pkg->is_packaged = GetCurrentPackageFullName( &len, pkg->full_name ) == ERROR_SUCCESS;
    if (pkg->is_packaged)
    {
        len = ARRAY_SIZE( pkg->family_name );
        GetCurrentPackageFamilyName( &len, pkg->family_name );
    }

    xaml_package_resolve_location( pkg );

    /* manifest in the installed location, then the exe dir (the package dir
     * for a deployed UWP app): the authoritative DisplayName. */
    if (pkg->installed_location[0] && xaml_manifest_display_name( pkg->installed_location,
                                                                  pkg->display_name, ARRAY_SIZE( pkg->display_name ) ));
    else
    {
        WCHAR dir[MAX_PATH];
        DWORD len2 = GetModuleFileNameW( NULL, dir, ARRAY_SIZE( dir ) );
        if (len2 && len2 < ARRAY_SIZE( dir ) && wcsrchr( dir, L'\\' ))
        {
            *wcsrchr( dir, L'\\' ) = 0;
            xaml_manifest_display_name( dir, pkg->display_name, ARRAY_SIZE( pkg->display_name ) );
        }
    }

    /* a DisplayName seeded on the deployment record overrides, and covers
     * packages shipping no manifest attribute. */
    if (pkg->full_name[0])
    {
        lstrcpyW( subkey, L"Packages\\" );
        lstrcatW( subkey, pkg->full_name );
        for (i = 0; i < ARRAY_SIZE( xaml_reg_roots ); i++)
            if (xaml_read_reg_str( xaml_reg_roots[i], subkey, L"DisplayName",
                                   pkg->display_name, ARRAY_SIZE( pkg->display_name ) ))
                break;
    }

    TRACE( "package init: manifest/reg display_name %s, ms-resource resolving\n", debugstr_w( pkg->display_name ) );
    xaml_package_resolve_ms_resource( pkg );
    TRACE( "package init: resolved display_name %s\n", debugstr_w( pkg->display_name ) );

    /* Publish the resolved DisplayName as the package short name on the seeded
     * AppModel identity: windows.ui names the CoreWindow window class with it,
     * and winex11 publishes that class name as WM_CLASS (taskbar identity). */
    if (pkg->full_name[0] && pkg->display_name[0])
    {
        WCHAR sub[640];
        HKEY hkey;
        lstrcpyW( sub, xaml_appmodel_key );
        lstrcatW( sub, L"\\Packages\\" );
        lstrcatW( sub, pkg->full_name );
        if (RegCreateKeyExW( HKEY_CURRENT_USER, sub, 0, NULL, 0, KEY_WRITE, NULL, &hkey, NULL ) == ERROR_SUCCESS)
        {
            RegSetValueExW( hkey, L"ApplicationShortName", 0, REG_SZ, (const BYTE *)pkg->display_name,
                            (lstrlenW( pkg->display_name ) + 1) * sizeof(WCHAR) );
            RegCloseKey( hkey );
        }
    }
    return TRUE;
}

static const WCHAR *xaml_package_display_name( void )
{
    InitOnceExecuteOnce( &xaml_package_once, xaml_package_init, NULL, NULL );
    return xaml_package.display_name[0] ? xaml_package.display_name : NULL;
}

static const WCHAR *xaml_package_short_name( void );

void xaml_package_warm( void )
{
    const WCHAR *short_name = xaml_package_short_name();
    TRACE( "package warm: short name %s\n", debugstr_w( short_name ) );
    if (short_name && short_name[0])
        SetEnvironmentVariableW( L"WINE_PACKAGE_CLASS", short_name );
}

/* Short app identity for window lookups: the package DisplayName, else the
 * WINE_PACKAGE_AUMID app id (the part after '!'), else nothing. Matches what
 * windows.ui/corewindow.c picks as the CoreWindow class name. */
static const WCHAR *xaml_package_short_name( void )
{
    static WCHAR aumid_app[128];
    const WCHAR *name = xaml_package_display_name();
    WCHAR buf[256];
    DWORD len;

    if (name) return name;
    if (aumid_app[0]) return aumid_app;
    len = GetEnvironmentVariableW( L"WINE_PACKAGE_AUMID", buf, ARRAY_SIZE(buf) );
    if (len && len < ARRAY_SIZE(buf))
    {
        WCHAR *p = wcschr( buf, L'!' );
        lstrcpynW( aumid_app, p ? p + 1 : buf, ARRAY_SIZE(aumid_app) );
        if (aumid_app[0]) return aumid_app;
    }
    return NULL;
}

HWND xaml_find_core_window( void )
{
    const WCHAR *short_name = xaml_package_short_name();
    HWND hw = NULL;
    if (short_name) hw = FindWindowW( short_name, NULL );
    if (!hw) hw = FindWindowW( L"Windows.UI.Core.CoreWindow", NULL );
    if (!hw) hw = FindWindowW( L"App", NULL );
    return hw;
}

static BOOL xaml_package_logo_path( WCHAR *out_path, DWORD cch )
{
    typedef HRESULT (WINAPI *p_AppxGetCurrentPackageSquare44x44Logo)( WCHAR *, DWORD );
    p_AppxGetCurrentPackageSquare44x44Logo fn;
    HMODULE mod;
    WCHAR key[640];
    SIZE_T i;

    if (!out_path || !cch) return FALSE;
    out_path[0] = 0;

    InitOnceExecuteOnce( &xaml_package_once, xaml_package_init, NULL, NULL );
    if (xaml_package.full_name[0])
    {
        lstrcpyW( key, L"Software\\Classes\\AppModel\\Repository\\Packages\\" );
        lstrcatW( key, xaml_package.full_name );
        lstrcatW( key, L"\\VisualElements" );
        for (i = 0; i < ARRAY_SIZE( xaml_reg_roots ); i++)
        {
            HKEY hkey;
            DWORD type = 0, size = cch * sizeof(WCHAR), attr;
            if (RegOpenKeyExW( xaml_reg_roots[i], key, 0, KEY_READ, &hkey ) == ERROR_SUCCESS)
            {
                LONG ret = RegQueryValueExW( hkey, L"Square44x44LogoPath", NULL, &type, (BYTE *)out_path, &size );
                RegCloseKey( hkey );
                if (ret == ERROR_SUCCESS && type == REG_SZ && out_path[0] &&
                    (attr = GetFileAttributesW( out_path )) != INVALID_FILE_ATTRIBUTES &&
                    !(attr & FILE_ATTRIBUTE_DIRECTORY))
                    return TRUE;
                out_path[0] = 0;
            }
        }
    }

    mod = GetModuleHandleW( L"appxdeploymentclient.dll" );
    if (!mod) mod = LoadLibraryW( L"appxdeploymentclient.dll" );
    if (mod && (fn = (p_AppxGetCurrentPackageSquare44x44Logo)GetProcAddress( mod, "AppxGetCurrentPackageSquare44x44Logo" )))
    {
        if (SUCCEEDED( fn( out_path, cch ) ) && out_path[0])
            return TRUE;
        out_path[0] = 0;
    }
    return FALSE;
}

static HICON xaml_wic_create_icon_from_frame( IWICImagingFactory *wic, IWICBitmapSource *src, UINT w, UINT h )
{
    IWICBitmapScaler *scaler = NULL;
    IWICFormatConverter *conv = NULL;
    BITMAPINFO bmi;
    void *bits = NULL;
    HDC hdc;
    HBITMAP color = NULL, mask = NULL;
    HICON icon = NULL;
    HRESULT hr;

    hr = IWICImagingFactory_CreateBitmapScaler( wic, &scaler );
    if (SUCCEEDED( hr ))
        hr = IWICBitmapScaler_Initialize( scaler, src, w, h, WICBitmapInterpolationModeFant );
    if (SUCCEEDED( hr ))
        hr = IWICImagingFactory_CreateFormatConverter( wic, &conv );
    if (SUCCEEDED( hr ))
        hr = IWICFormatConverter_Initialize( conv, (IWICBitmapSource *)scaler, &GUID_WICPixelFormat32bppBGRA_xaml,
                                             WICBitmapDitherTypeNone, NULL, 0.0, WICBitmapPaletteTypeCustom );
    if (SUCCEEDED( hr ))
    {
        memset( &bmi, 0, sizeof(bmi) );
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = (LONG)w;
        bmi.bmiHeader.biHeight = -(LONG)h;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        hdc = GetDC( NULL );
        color = CreateDIBSection( hdc, &bmi, DIB_RGB_COLORS, &bits, NULL, 0 );
        ReleaseDC( NULL, hdc );
        if (color && bits &&
            SUCCEEDED( IWICFormatConverter_CopyPixels( conv, NULL, w * 4, w * h * 4, bits ) ))
        {
            mask = CreateBitmap( w, h, 1, 1, NULL );
            if (mask)
            {
                ICONINFO ii = {0};
                ii.fIcon = TRUE;
                ii.hbmMask = mask;
                ii.hbmColor = color;
                icon = CreateIconIndirect( &ii );
                DeleteObject( mask );
            }
        }
        if (color) DeleteObject( color );
    }

    if (conv) IWICFormatConverter_Release( conv );
    if (scaler) IWICBitmapScaler_Release( scaler );
    return icon;
}

static void xaml_load_package_icons( HICON *out_big, HICON *out_small )
{
    WCHAR logo_path[MAX_PATH];
    IWICImagingFactory *wic = NULL;
    IWICBitmapDecoder *dec = NULL;
    IWICBitmapFrameDecode *frame = NULL;
    BOOL com_init = FALSE;
    HRESULT hr;

    *out_big = NULL;
    *out_small = NULL;
    if (!xaml_package_logo_path( logo_path, ARRAY_SIZE( logo_path ) )) return;

    hr = CoCreateInstance( &CLSID_WICImagingFactory_xaml, NULL, CLSCTX_INPROC_SERVER,
                           &IID_IWICImagingFactory_xaml, (void **)&wic );
    if (hr == CO_E_NOTINITIALIZED && SUCCEEDED( CoInitializeEx( NULL, COINIT_APARTMENTTHREADED ) ))
    {
        com_init = TRUE;
        hr = CoCreateInstance( &CLSID_WICImagingFactory_xaml, NULL, CLSCTX_INPROC_SERVER,
                               &IID_IWICImagingFactory_xaml, (void **)&wic );
    }
    if (SUCCEEDED( hr ) && wic)
    {
        if (SUCCEEDED( IWICImagingFactory_CreateDecoderFromFilename( wic, logo_path, NULL, GENERIC_READ,
                                                                     WICDecodeMetadataCacheOnLoad, &dec ) ) && dec)
        {
            if (SUCCEEDED( IWICBitmapDecoder_GetFrame( dec, 0, &frame ) ) && frame)
            {
                *out_big = xaml_wic_create_icon_from_frame( wic, (IWICBitmapSource *)frame, 48, 48 );
                *out_small = xaml_wic_create_icon_from_frame( wic, (IWICBitmapSource *)frame, 16, 16 );
                TRACE( "loaded package icon from %s -> big=%p small=%p\n",
                       debugstr_w( logo_path ), *out_big, *out_small );
                IWICBitmapFrameDecode_Release( frame );
            }
            IWICBitmapDecoder_Release( dec );
        }
        IWICImagingFactory_Release( wic );
    }
    if (com_init) CoUninitialize();
}

static void xaml_apply_window_icon( HWND top )
{
    static HICON big_icon, small_icon;
    static BOOL icons_init;
    static HWND last_icon_hwnd;

    if (!top) return;
    if (!icons_init)
    {
        xaml_load_package_icons( &big_icon, &small_icon );
        if (!big_icon)
            big_icon = LoadImageW( 0, (LPCWSTR)IDI_APPLICATION, IMAGE_ICON, 32, 32, LR_SHARED );
        if (!small_icon)
            small_icon = LoadImageW( 0, (LPCWSTR)IDI_APPLICATION, IMAGE_ICON, 16, 16, LR_SHARED );
        icons_init = TRUE;
    }
    if (top != last_icon_hwnd)
    {
        last_icon_hwnd = top;
        if (big_icon)
        {
            SetClassLongPtrW( top, GCLP_HICON, (LONG_PTR)big_icon );
            SendMessageW( top, WM_SETICON, ICON_BIG, (LPARAM)big_icon );
        }
        if (small_icon)
        {
            SetClassLongPtrW( top, GCLP_HICONSM, (LONG_PTR)small_icon );
            SendMessageW( top, WM_SETICON, ICON_SMALL, (LPARAM)small_icon );
        }
    }
}

static const struct xaml_app_profile xaml_app_profiles[] =
{
    {
        .family_prefix           = L"Microsoft.WindowsCalculator",
        .use_winui3_window_class = FALSE,
        .webview2_template       = NULL,
        .design_component        = NULL,
        .serve_core_controls     = TRUE,
        .vm_type_name            = L"CalculatorApp.ViewModel.StandardCalculatorViewModel",
        .display_sync            = TRUE,
        .teardown_vm             = TRUE,
        .seed_xbf                = L"Calculator",
    },
    {
        .family_prefix           = L"Microsoft.WindowsStore",
        .use_winui3_window_class = FALSE,
        .webview2_template       = NULL,
        .design_component        = NULL,
        .serve_core_controls     = TRUE,
        .vm_type_name            = NULL,
        .display_sync            = FALSE,
        .teardown_vm             = FALSE,
        .seed_xbf                = L"StoreManifestPage",
    },
    {
        .family_prefix           = L"5319275A.WhatsAppDesktop",
        .use_winui3_window_class = TRUE,
        .webview2_template       = L"Microsoft.UI.Xaml.Controls.WebView2",
        .design_component        = L"WhatsApp.Design.FontScaleControlPanel",
        .serve_core_controls     = FALSE,
        .vm_type_name            = NULL,
        .display_sync            = FALSE,
        .teardown_vm             = FALSE,
        .seed_xbf                = NULL,
    },
    {
        .family_prefix           = L"Microsoft.WindowsNotepad",
        .use_winui3_window_class = FALSE,
        .webview2_template       = NULL,
        .design_component        = NULL,
        .serve_core_controls     = TRUE,
        .vm_type_name            = NULL,
        .display_sync            = FALSE,
        .teardown_vm             = FALSE,
        .seed_xbf                = NULL,
    },
    {
        .family_prefix           = NULL,
        .use_winui3_window_class = FALSE,
        .webview2_template       = NULL,
        .design_component        = NULL,
        .serve_core_controls     = TRUE,
        .vm_type_name            = NULL,
        .display_sync            = FALSE,
        .teardown_vm             = FALSE,
        .seed_xbf                = NULL,
    },
};

const struct xaml_app_profile *g_profile = &xaml_app_profiles[ARRAY_SIZE( xaml_app_profiles ) - 1];
static INIT_ONCE g_profile_once = INIT_ONCE_STATIC_INIT;

static BOOL WINAPI xaml_profile_init( INIT_ONCE *once, void *param, void **context )
{
    WCHAR family[256] = {0};
    UINT32 len = ARRAY_SIZE( family );
    SIZE_T i;

    g_profile = &xaml_app_profiles[ARRAY_SIZE( xaml_app_profiles ) - 1];
    if (GetCurrentPackageFamilyName( &len, family ) == ERROR_SUCCESS && family[0])
    {
        for (i = 0; i < ARRAY_SIZE( xaml_app_profiles ) - 1; i++)
        {
            const WCHAR *pfx = xaml_app_profiles[i].family_prefix;
            SIZE_T plen = pfx ? lstrlenW( pfx ) : 0;
            if (plen && !wcsnicmp( family, pfx, plen ) &&
                (!family[plen] || family[plen] == L'_'))
            {
                g_profile = &xaml_app_profiles[i];
                break;
            }
        }
    }
    return TRUE;
}

const struct xaml_app_profile *xaml_get_app_profile( void )
{
    InitOnceExecuteOnce( &g_profile_once, xaml_profile_init, NULL, NULL );
    return g_profile;
}

BOOL is_calculator_app( void )
{
    const WCHAR *seed = xaml_get_app_profile()->seed_xbf;
    return seed && !wcscmp( seed, L"Calculator" );
}

BOOL is_store_app( void )
{
    const WCHAR *seed = xaml_get_app_profile()->seed_xbf;
    return seed && !wcscmp( seed, L"StoreManifestPage" );
}

BOOL is_uwp_xaml_app( void )
{
    const struct xaml_app_profile *p = xaml_get_app_profile();
    return p->family_prefix != NULL && p->serve_core_controls;
}

void *uwp_get_app_outer( void )
{
    return current_application ? current_application->outer : NULL;
}

BOOL is_whatsapp_app( void )
{
    return xaml_get_app_profile()->use_winui3_window_class;
}

static WNDPROC xaml_corewindow_prev_proc;
static HWND xaml_corewindow_subclassed_hwnd;
/* FLIP_SEQUENTIAL back buffer: wine swaps front/back on each Present, so paint the
 * buffer that is currently the back buffer and track the flip after every Present.
 * Always painting buffer 0 shows one-present-stale content (black on the first flip). */
static UINT xaml_back_index;

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

/* Paint every child's DC as well: the X tree shows a child window at the same
 * geometry as the mapped top-level, and the visible surface may be that child. */
static BOOL CALLBACK xaml_paint_child( HWND hwnd, LPARAM lparam )
{
    RECT rc;
    HDC hdc = GetDC( hwnd );
    if (hdc)
    {
        GetClientRect( hwnd, &rc );
        if (rc.right > 0 && rc.bottom > 0)
            xaml_layout_render( hwnd, hdc, rc.right, rc.bottom, global_xaml_window.content );
        ReleaseDC( hwnd, hdc );
        TRACE( "xaml painted child %p %ldx%ld\n", hwnd, rc.right, rc.bottom );
    }
    return TRUE;
}

/* Paint the composed layout inside WM_PAINT. A GetDC() blit lands on the window's
 * GDI surface, but on the winex11 driver only the BeginPaint/EndPaint cycle flushes
 * a surface to the X pixmap, so the mapped window is painted from here. */
static volatile LONG g_xaml_window_dirty = 1;
static BOOL g_xaml_post_load_presented;
static HWND g_xaml_timer_armed_hwnd;
static int g_xaml_timer_idle_ticks;
static BOOL g_in_xaml_present;
static HRESULT xaml_window_present( struct xaml_window *impl );

static void xaml_window_refresh_present( HWND hwnd )
{
    static BOOL in_refresh;
    struct xaml_window *impl = &global_xaml_window;
    UINT width, height;
    RECT rc;

    if (in_refresh || g_in_xaml_present || !impl->dcomp_device)
        return;

    in_refresh = TRUE;
    if (impl->swapchain && is_uwp_xaml_app())
    {
        IDXGISurface1 *surface1 = NULL;
        HWND target = impl->target_hwnd && IsWindow( impl->target_hwnd ) ? impl->target_hwnd : hwnd;
        GetClientRect( target, &rc );
        width = rc.right > 0 ? (UINT)rc.right : impl->width;
        height = rc.bottom > 0 ? (UINT)rc.bottom : impl->height;
        if (width > 0 && height > 0)
        {
            if (SUCCEEDED(IDXGISwapChain1_GetBuffer( impl->swapchain, 0, &IID_IDXGISurface1, (void **)&surface1 )) && surface1)
            {
                HDC hdc = NULL;
                if (SUCCEEDED(IDXGISurface1_GetDC( surface1, FALSE, &hdc )) && hdc)
                {
                    xaml_layout_render( target, hdc, width, height, impl->content );
                    IDXGISurface1_ReleaseDC( surface1, NULL );
                }
                IDXGISurface1_Release( surface1 );
            }
            IDXGISwapChain1_Present( impl->swapchain, 0, 0 );
        }
    }
    IDCompositionDesktopDevice_Commit( impl->dcomp_device );
    in_refresh = FALSE;
}

void xaml_mark_window_dirty( void )
{
    InterlockedExchange( &g_xaml_window_dirty, 1 );
    g_xaml_timer_idle_ticks = 0;
    if (!g_xaml_timer_armed_hwnd && global_xaml_window.target_hwnd && IsWindow( global_xaml_window.target_hwnd ))
    {
        g_xaml_timer_armed_hwnd = global_xaml_window.target_hwnd;
        SetTimer( g_xaml_timer_armed_hwnd, 0x5756, 500, NULL );
    }
}

static LRESULT WINAPI xaml_corewindow_wndproc( HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam )
{
    if (msg == WM_ERASEBKGND)
        return TRUE;
    if (msg == WM_PAINT)
    {
        if (is_uwp_xaml_app())
        {
            RECT rc;
            HWND top = GetAncestor( hwnd, GA_ROOT );
            HDC hdc = GetDC( top );
            GetClientRect( top, &rc );
            TRACE( "xaml corewindow WM_PAINT hdc %p rc %ldx%ld\n", hdc, rc.right, rc.bottom );
            if (rc.right > 0 && rc.bottom > 0)
            {
                xaml_layout_render( top, hdc, rc.right, rc.bottom, global_xaml_window.content );
                InterlockedExchange( &g_xaml_window_dirty, 0 );
            }
            ReleaseDC( top, hdc );
        }
        ValidateRect( hwnd, NULL );
        xaml_window_refresh_present( hwnd );
        return 0;
    }
    if (msg == WM_MOVE || msg == WM_EXITSIZEMOVE)
    {
        LRESULT ret;
        if (xaml_corewindow_prev_proc && xaml_corewindow_prev_proc != xaml_corewindow_wndproc)
            ret = CallWindowProcW( xaml_corewindow_prev_proc, hwnd, msg, wparam, lparam );
        else
            ret = DefWindowProcW( hwnd, msg, wparam, lparam );
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
        if (!wp || !(wp->flags & SWP_NOMOVE) || !(wp->flags & SWP_NOSIZE) || (wp->flags & SWP_SHOWWINDOW))
            xaml_window_refresh_present( hwnd );
        return ret;
    }
    if (msg == WM_APP_WV2_START)
    {
        ERR( "xaml_corewindow_wndproc: WM_APP_WV2_START hwnd %p\n", hwnd );
        xaml_wv2_handle_start_msg( lparam );
        return 0;
    }
    if (msg == WM_SIZE)
    {
        HWND top = GetAncestor( hwnd, GA_ROOT );
        RECT rc;
        LRESULT ret;
        if (!top) top = hwnd;
        if (xaml_corewindow_prev_proc && xaml_corewindow_prev_proc != xaml_corewindow_wndproc)
            ret = CallWindowProcW( xaml_corewindow_prev_proc, hwnd, msg, wparam, lparam );
        else
            ret = DefWindowProcW( hwnd, msg, wparam, lparam );
        GetClientRect( top, &rc );
        TRACE( "xaml corewindow WM_SIZE hwnd %p top %p rc %ldx%ld (prev %ux%u)\n",
               hwnd, top, rc.right, rc.bottom, global_xaml_window.width, global_xaml_window.height );
        if (rc.right > 0 && rc.bottom > 0)
        {
            if (top != hwnd)
                SetWindowPos( hwnd, 0, 0, 0, rc.right, rc.bottom,
                              SWP_NOZORDER | SWP_NOMOVE | SWP_NOACTIVATE );
            xaml_wv2_on_window_resize( top, rc.right, rc.bottom );
            if ((UINT)rc.right != global_xaml_window.width ||
                (UINT)rc.bottom != global_xaml_window.height)
            {
                xaml_mark_window_dirty();
                InvalidateRect( top, NULL, FALSE );
                if (top != hwnd) InvalidateRect( hwnd, NULL, FALSE );
                xaml_window_present( &global_xaml_window );
            }
        }
        return ret;
    }
    if (msg == WM_CLOSE)
    {
        /* Close button / Alt+F4: tear the window down and quit the message loop. */
        DestroyWindow( hwnd );
        PostQuitMessage( 0 );
        return 0;
    }
    if (msg == WM_TIMER && wparam == 0x5756)
    {
        if (InterlockedExchange( &g_xaml_window_dirty, 0 ))
        {
            SIZE sz;
            g_xaml_timer_idle_ticks = 0;
            xaml_window_present( &global_xaml_window );
            xaml_get_layout_metrics( hwnd, &sz, NULL );
            xaml_wv2_on_window_ready( hwnd, sz.cx, sz.cy );
        }
        else if (g_xaml_post_load_presented && ++g_xaml_timer_idle_ticks >= 4)
        {
            KillTimer( hwnd, 0x5756 );
            g_xaml_timer_armed_hwnd = NULL;
        }
        return 0;
    }
    if (msg == WM_DEADCHAR || msg == WM_SYSCHAR)
        goto forward_msg;
    if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN || msg == WM_KEYUP || msg == WM_CHAR)
    {
        struct xaml_core_window *cw = as_xaml_core_window( global_xaml_window.core_window );
        HRESULT hr;
        BOOL accel_handled = FALSE;
        if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN)
            accel_handled = uwp_dispatch_keydown( hwnd, (UINT)wparam, lparam, NULL );
        xaml_cw_fire_key_event( cw, msg, wparam, lparam );
        hr = notify_inner_core_window_key( hwnd, msg, wparam );
        if (msg == WM_CHAR && !(GetKeyState( VK_CONTROL ) & 0x8000) && !(GetKeyState( VK_MENU ) & 0x8000))
            uwp_keyboard_press_char( (WCHAR)wparam );
        uwp_sync_calc_display_from_vm();
        xaml_window_present( &global_xaml_window );
        xaml_mark_window_dirty();
        InvalidateRect( GetAncestor( hwnd, GA_ROOT ), NULL, FALSE );
        UpdateWindow( GetAncestor( hwnd, GA_ROOT ) );
        if ((accel_handled || (msg != WM_SYSKEYDOWN && SUCCEEDED( hr ))) &&
            xaml_corewindow_prev_proc == (WNDPROC)GetClassLongPtrW( hwnd, GCLP_WNDPROC ))
            return 0;
        goto forward_msg;
    }
    if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP || msg == WM_RBUTTONDOWN || msg == WM_RBUTTONUP ||
        msg == WM_MOUSEMOVE || msg == WM_MOUSEWHEEL || msg == WM_MOUSELEAVE || msg == WM_CAPTURECHANGED)
    {
        HWND top = GetAncestor( hwnd, GA_ROOT );
        struct xaml_core_window *cw = as_xaml_core_window( global_xaml_window.core_window );
        struct xaml_pointer_info info;
        enum xaml_pointer_event_kind kind;
        RECT rc;
        POINT pt = { 0, 0 };
        BOOL btn_handled = FALSE;
        BOOL tree_routed = FALSE;

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
            if (cw && cw->has_capture) { ReleaseCapture(); cw->has_capture = FALSE; }
            break;
        case WM_MOUSEWHEEL:
            kind = XAML_PTR_WHEEL;
            break;
        default:
            kind = XAML_PTR_MOVED;
            break;
        }

        xaml_cw_fire_pointer_event( cw, kind, &info );

        if (global_xaml_window.content && rc.right > 0 && rc.bottom > 0)
        {
            tree_routed = uwp_route_pointer_event( top, kind, &info, rc.right, rc.bottom, global_xaml_window.content );
            if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP)
                btn_handled = uwp_handle_mouse_input( top, msg, pt.x, pt.y, rc.right, rc.bottom, global_xaml_window.content );
        }

        if (btn_handled || (tree_routed && (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP)))
        {
            xaml_mark_window_dirty();
            InvalidateRect( top, NULL, FALSE );
            InvalidateRect( hwnd, NULL, FALSE );
            UpdateWindow( top );
            UpdateWindow( hwnd );
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

    ERR( "xaml_window_present: hwnd=%p swapchain=%p dev=%p is_uwp=%d\n",
         hwnd, impl->swapchain, impl->d3d11_dev, is_uwp_xaml_app() );

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
        RECT wrc;
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
    InvalidateRect( hwnd, NULL, FALSE );
    UpdateWindow( hwnd );

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

    /* 2. Ensure DComp target for CoreWindow HWND */
    if (!impl->dcomp_target)
    {
        hr = IDCompositionDesktopDevice_CreateTargetForHwnd( impl->dcomp_device, hwnd, TRUE, &impl->dcomp_target );
        if (FAILED(hr))
        {
            WARN( "CreateTargetForHwnd failed hr %#lx\n", hr );
            g_in_xaml_present = FALSE;
            return hr;
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
        IDCompositionTarget_SetRoot( impl->dcomp_target, (IDCompositionVisual *)impl->dcomp_root_visual );
    }
    if (impl->dcomp_root_visual)
    {
        D2D_RECT_F clip = { 0.0f, 0.0f, (float)width, (float)height };
        IDCompositionVisual_SetClip( (IDCompositionVisual *)impl->dcomp_root_visual, &clip );
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
        hr = IDXGISwapChain1_ResizeBuffers( impl->swapchain, 0, width, height,
                                            DXGI_FORMAT_B8G8R8A8_UNORM,
                                            DXGI_SWAP_CHAIN_FLAG_GDI_COMPATIBLE );
        TRACE( "XAML: resized composition swapchain %p to %ux%u (hr %#lx)\n",
               impl->swapchain, width, height, hr );
    }
    if (!impl->swapchain && is_uwp_xaml_app() && !d3d11_create_failed)
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
        desc.BufferCount = 2;
        desc.Scaling = DXGI_SCALING_STRETCH;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
        /* GDI_COMPATIBLE: wined3d_texture_get_dc only serves a DC for swapchain
         * buffers created with this flag; without it the per-frame buffer fill
         * in WM_PAINT gets WINED3DERR_INVALIDCALL and present shows a black
         * never-painted texture. */
        desc.Flags = DXGI_SWAP_CHAIN_FLAG_GDI_COMPATIBLE;

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
                        /* Composition swapchain: DComp owns the single onscreen GLX swapchain
                         * on hwnd and composites impl->swapchain's front buffer on Commit. */
                        hr = IDXGIFactory2_CreateSwapChainForComposition( dxgi_factory, (IUnknown *)impl->d3d11_dev,
                                                                           &desc, NULL, &impl->swapchain );
                        IDXGIFactory2_Release( dxgi_factory );
                    }
                    IDXGIAdapter_Release( dxgi_adapter );
                }
                IDXGIDevice_Release( dxgi_dev );
            }
        }

        if (impl->swapchain)
        {
            IDCompositionVisual_SetContent( (IDCompositionVisual *)impl->dcomp_root_visual,
                                            (IUnknown *)impl->swapchain );
        }
    }

    /* 6. Render the XAML layout / visual elements onto the swapchain backbuffer */
    if (impl->swapchain && is_uwp_xaml_app())
    {
        IDXGISurface1 *surface1 = NULL;
        hr = IDXGISwapChain1_GetBuffer( impl->swapchain, 0, &IID_IDXGISurface1, (void **)&surface1 );
        if (SUCCEEDED(hr) && surface1)
        {
            HDC hdc = NULL;
            hr = IDXGISurface1_GetDC( surface1, FALSE, &hdc );
            if (SUCCEEDED(hr) && hdc)
            {
                xaml_layout_render( hwnd, hdc, width, height, impl->content );
                IDXGISurface1_ReleaseDC( surface1, NULL );
            }
            IDXGISurface1_Release( surface1 );
        }

        /* 7. Present swapchain via wined3d CS-thread path */
        IDXGISwapChain1_Present( impl->swapchain, 1, 0 );
    }

    /* 8. Commit the DirectComposition tree */
    IDCompositionDesktopDevice_Commit( impl->dcomp_device );

    /* 9. Blit the composed layout into the top-level window DC. A composition swapchain
     * has no GDI backing store, so the winex11 driver's window pixmap stays blank until
     * something paints the HWND; drawing the same layout here is what makes the mapped
     * window show content on screen. */
    {
        /* The X11 driver paints the root ancestor's pixmap; a child HWND has no backing
         * store of its own on screen, so blit through the root window. */
        HWND root = GetAncestor( hwnd, GA_ROOT );
        HDC hdc = root ? GetDC( root ) : NULL;
        if (hdc)
        {
            RECT rc;
            if (root != hwnd && GetClientRect( root, &rc ))
            {
                width = rc.right > 0 ? rc.right : width;
                height = rc.bottom > 0 ? rc.bottom : height;
            }
            xaml_layout_render( root, hdc, width, height, impl->content );
            TRACE( "xaml step9 root %p hwnd %p hdc %p probe=%06lx\n", root, hwnd, hdc, GetPixel( hdc, 5, 5 ) );
            ReleaseDC( root, hdc );
            /* Flush after the blit: the X11 driver copies the DC surface on the paint
             * cycle, so an invalidate issued before the draw shows a stale pixmap. */
            InvalidateRect( root, NULL, FALSE );
            UpdateWindow( root );
        }
    }

    TRACE( "XAML: composited and presented visual tree for hwnd %p (%ux%u scale %.2f)\n", hwnd, width, height, scale );
    ERR( "xaml_window_present done: hwnd %p (%ux%u)\n", hwnd, width, height );
    InterlockedExchange( &g_xaml_window_dirty, 0 );
    if (impl->content) g_xaml_post_load_presented = TRUE;
    if (g_xaml_timer_armed_hwnd != hwnd)
    {
        g_xaml_timer_armed_hwnd = hwnd;
        g_xaml_timer_idle_ticks = 0;
        SetTimer( hwnd, 0x5756, 500, NULL );
    }
    xaml_wv2_on_window_ready( hwnd, width, height );
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
            client->cx = rc.right;
            client->cy = rc.bottom;
        }
        else
        {
            HMONITOR mon = (hwnd && IsWindow( hwnd ))
                ? MonitorFromWindow( hwnd, MONITOR_DEFAULTTOPRIMARY )
                : MonitorFromPoint( (POINT){ 0, 0 }, MONITOR_DEFAULTTOPRIMARY );
            MONITORINFO mi = { sizeof(mi) };
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
    if (impl->content) IInspectable_AddRef( impl->content );
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

static HRESULT WINAPI dummy_wa_noop( void *iface, void *a1, void *a2 )
{
    if (a1) *(void **)a1 = NULL;
    if (a2) *(void **)a2 = NULL;
    return S_OK;
}

static ULONG WINAPI dummy_wa_AddRef( void *iface ) { return 2; }
static ULONG WINAPI dummy_wa_Release( void *iface ) { return 1; }

static HRESULT WINAPI dummy_wa_QueryInterface( void *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    TRACE( "dummy_wa_QueryInterface iid %s\n", debugstr_guid( iid ) );
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IWindowActivatedEventArgs_app ))
    {
        *out = iface;
        dummy_wa_AddRef( iface );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static HRESULT WINAPI dummy_wa_GetIids( void *iface, ULONG *count, IID **iids ) { if (count) *count = 0; if (iids) *iids = NULL; return S_OK; }
static HRESULT WINAPI dummy_wa_GetRuntimeClassName( void *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.UI.Core.WindowActivatedEventArgs", 39, name );
}
static HRESULT WINAPI dummy_wa_GetTrustLevel( void *iface, TrustLevel *level ) { if (level) *level = BaseTrust; return S_OK; }
static HRESULT WINAPI dummy_wa_get_WindowActivationState( void *iface, int *value )
{
    if (value) *value = 0; /* CodeActivated */
    return S_OK;
}
static const void *dummy_wa_vtbl[64] =
{
    [0] = dummy_wa_QueryInterface,
    [1] = dummy_wa_AddRef,
    [2] = dummy_wa_Release,
    [3] = dummy_wa_GetIids,
    [4] = dummy_wa_GetRuntimeClassName,
    [5] = dummy_wa_GetTrustLevel,
    [6] = dummy_wa_get_WindowActivationState,
    [7 ... 63] = dummy_wa_noop,
};
static struct { const void **vtbl; } dummy_wa_obj = { dummy_wa_vtbl };

static HRESULT WINAPI xaml_window_add_Activated( IInspectable *iface, void *handler, EventRegistrationToken *token )
{
    struct xaml_window *impl = &global_xaml_window;
    TRACE( "iface %p, handler %p\n", iface, handler );
    if (impl->activated_handler_count < ARRAY_SIZE(impl->activated_handlers))
    {
        impl->activated_handlers[impl->activated_handler_count++] = handler;
        if (handler) IUnknown_AddRef( (IUnknown *)handler );
    }
    if (token) token->value = impl->activated_handler_count;
    if (impl->is_active && handler)
    {
        HRESULT (STDMETHODCALLTYPE *invoke)(void *, void *, void *) = (*(void ***)handler)[3];
        TRACE( "window already active: immediately invoking Window.Activated handler %p\n", handler );
        invoke( handler, global_window_outer ? global_window_outer : &impl->IInspectable_iface, (void *)&dummy_wa_obj );
    }
    return S_OK;
}

static HRESULT WINAPI xaml_window_remove_Activated( IInspectable *iface, EventRegistrationToken token )
{
    return S_OK;
}

static HRESULT WINAPI xaml_window_add_Closed( IInspectable *iface, void *handler, EventRegistrationToken *token )
{
    if (token) token->value = 1;
    return S_OK;
}

static HRESULT WINAPI xaml_window_remove_Closed( IInspectable *iface, EventRegistrationToken token )
{
    return S_OK;
}

static HRESULT WINAPI xaml_window_add_SizeChanged( IInspectable *iface, void *handler, EventRegistrationToken *token )
{
    if (token) token->value = 1;
    return S_OK;
}

static HRESULT WINAPI xaml_window_remove_SizeChanged( IInspectable *iface, EventRegistrationToken token )
{
    return S_OK;
}

static HRESULT WINAPI xaml_window_add_VisibilityChanged( IInspectable *iface, void *handler, EventRegistrationToken *token )
{
    if (token) token->value = 1;
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
    for (i = 0; i < impl->activated_handler_count; i++)
    {
        void *h = impl->activated_handlers[i];
        if (h)
        {
            HRESULT (STDMETHODCALLTYPE *invoke)(void *, void *, void *) = (*(void ***)h)[3];
            TRACE( "invoking Window.Activated handler %p\n", h );
            __TRY
            {
                invoke( h, global_window_outer ? global_window_outer : &impl->IInspectable_iface, (void *)&dummy_wa_obj );
            }
            __EXCEPT_ALL
            {
                TRACE( "Window.Activated handler %p faulted\n", h );
            }
            __ENDTRY;
        }
    }
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
    ERR( "xaml_window_add_Loaded iface %p handler %p\n", iface, handler );
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
        [0 ... 233] = (void *)xaml_window_zero_out,
        [75 - 32] = (void *)xaml_window_add_Loaded,
        [76 - 32] = (void *)xaml_window_remove_Loaded,
        [130 - 32] = (void *)xaml_window_add_Loaded,
        [131 - 32] = (void *)xaml_window_remove_Loaded,
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
    const WCHAR *cls = xaml_get_app_profile()->use_winui3_window_class ? L"Microsoft.UI.Xaml.Window" : L"Windows.UI.Xaml.Window";
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
        void **vtbl = *(void ***)af;
        HRESULT (WINAPI *pGetForCurrentThread)(void *, void **) = (void *)vtbl[6];
        hr = pGetForCurrentThread( af, value );
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
        void **vtbl = *(void ***)statics;
        HRESULT (WINAPI *pGetFromWindowId)(void *, UINT64, void **) = (void *)vtbl[9];
        hr = pGetFromWindowId( statics, (UINT64)hwnd, value );
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

static struct xaml_window global_xaml_window =
{
    {(const struct IInspectableVtbl *)&xaml_window_vtbl},
    {(const struct IInspectableVtbl *)xaml_window2_vtbl},
    {(const struct IInspectableVtbl *)xaml_window_private_vtbl},
    {(const struct IInspectableVtbl *)ms_xaml_window_vtbl},
    {(const struct IInspectableVtbl *)ms_xaml_window2_vtbl},
    {&xaml_window_native_vtbl},
    1,
    NULL,
    NULL,
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
};

static struct window_statics window_statics_obj =
{
    {(const struct IActivationFactoryVtbl *)&window_statics_vtbl},
    1,
};

IActivationFactory *window_factory = &window_statics_obj.IActivationFactory_iface;

static HRESULT WINAPI factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct application_factory *impl = impl_from_IActivationFactory( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ) ||
        IsEqualGUID( iid, &IID_Windows_UI_Xaml_Application ))
    {
        *out = &impl->IActivationFactory_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    /* The class's activation interfaces: RoGetActivationFactory answers
     * IID_IApplication {c1b89ae4-...}, the projected interface the WinUI3
     * Mcg binder uses for the Microsoft.UI.Xaml.Application base of the
     * app's Application subclass. Windows answers both off the factory
     * object with the IApplication vtable; hand out the Application instance
     * the same ActivateInstance creates, so the caller's Resources/Run slots
     * dispatch on the right vtable. */
    if (IsEqualGUID( iid, &IID_IApplication ) ||
        IsEqualGUID( iid, &IID_IApplication_WinUI ) ||
        IsEqualGUID( iid, &IID_IApplication_751b ))
        return IActivationFactory_ActivateInstance( &impl->IActivationFactory_iface, (IInspectable **)out );

    if (IsEqualGUID( iid, &IID_IApplicationStatics ))
    {
        *out = &impl->IApplicationStatics_iface;
        IInspectable_AddRef( (IInspectable *)&impl->IActivationFactory_iface );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IApplicationStatics_WinUI ))
    {
        *out = &impl->ms_IApplicationStatics_iface;
        IInspectable_AddRef( (IInspectable *)&impl->IActivationFactory_iface );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IApplicationFactory ) ||
        IsEqualGUID( iid, &IID_IApplicationFactory_WinUI ) ||
        IsEqualGUID( iid, &IID_IApplicationFactory_WinUI3 ))
    {
        *out = &impl->IApplicationFactory_iface;
        IInspectable_AddRef( (IInspectable *)&impl->IActivationFactory_iface );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IWindowStatics ) ||
        IsEqualGUID( iid, &IID_IWindowStatics2 ) ||
        IsEqualGUID( iid, &IID_IWindowStatics_WinUI ))
    {
        *out = window_factory;
        IActivationFactory_AddRef( (IActivationFactory *)*out );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IWindowFactory_WinUI ))
    {
        *out = &win_factory_instance.IActivationFactory_iface;
        IActivationFactory_AddRef( (IActivationFactory *)*out );
        return S_OK;
    }

    /* The view source CoreApplication::Run drives, answered off the Application factory so a
     * caller holding the factory (Start's own path, and the test suite) can create and inspect
     * the framework view without going through Start's blocking Run. Real XAML answers this QI
     * E_NOINTERFACE; the conformance tests treat that as the broken-on-Windows case. */
    if (IsEqualGUID( iid, &IID_IFrameworkViewSource ))
    {
        *out = &impl->IFrameworkViewSource_iface;
        IInspectable_AddRef( (IInspectable *)&impl->IActivationFactory_iface );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI factory_AddRef( IActivationFactory *iface )
{
    struct application_factory *impl = impl_from_IActivationFactory( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p increasing refcount to %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI factory_Release( IActivationFactory *iface )
{
    struct application_factory *impl = impl_from_IActivationFactory( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    TRACE( "iface %p decreasing refcount to %lu.\n", iface, ref );
    return ref;
}

static HRESULT WINAPI factory_GetIids( IActivationFactory *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *class_name )
{
    FIXME( "iface %p, class_name %p stub!\n", iface, class_name );
    return E_NOTIMPL;
}

static HRESULT WINAPI factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI application_factory_CreateInstance( IApplicationFactory *iface,
        IInspectable *outer, IInspectable **inner, IApplication **instance );
static HRESULT WINAPI application_statics_Start( IApplicationStatics *iface,
        IApplicationInitializationCallback *callback );
static HRESULT WINAPI application_statics_get_Current( IApplicationStatics *iface,
        IApplication **current );

static HRESULT WINAPI factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    struct application_factory *impl = impl_from_IActivationFactory( iface );
    return application_factory_CreateInstance( &impl->IApplicationFactory_iface, NULL, NULL, (IApplication **)instance );
}

static HRESULT xaml_create_instance_from_provider( const WCHAR *full_name, void **out )
{
    void *provider = NULL;
    void *xaml_type = NULL;
    HSTRING hname = NULL;
    HRESULT hr = E_FAIL;
    BOOL is_uwp = FALSE;

    if (!out) return E_POINTER;
    *out = NULL;

    if (current_application && current_application->outer)
    {
        if (is_uwp_xaml_app())
        {
            hr = IInspectable_QueryInterface( current_application->outer, &IID_IXamlMetadataProvider_UWP, &provider );
            if (SUCCEEDED(hr) && provider) is_uwp = TRUE;
        }
        else
        {
            hr = IInspectable_QueryInterface( current_application->outer, &IID_IXamlMetadataProvider_WinUI, &provider );
            if (FAILED(hr) || !provider)
            {
                hr = IInspectable_QueryInterface( current_application->outer, &IID_IXamlMetadataProvider_UWP, &provider );
                if (SUCCEEDED(hr) && provider) is_uwp = TRUE;
            }
        }
        if (SUCCEEDED(hr) && provider)
        {
            void **vtbl = *(void ***)provider;
            HRESULT (WINAPI *get_by_name)(void *, HSTRING, void **) = (void *)vtbl[7];
            if (get_by_name && SUCCEEDED(WindowsCreateString( full_name, wcslen(full_name), &hname )) && hname)
            {
                hr = get_by_name( provider, hname, &xaml_type );
                ERR( "GetXamlTypeByFullName(%s) -> hr %#lx, type %p\n", debugstr_w(full_name), hr, xaml_type );
                WindowsDeleteString( hname );
                if (SUCCEEDED(hr) && xaml_type)
                {
                    void **type_vtbl = *(void ***)xaml_type;
                    /* UWP IXamlType: slot 18 is ActivateInstance; WinUI 3 IXamlType: slot 19 is ActivateInstance */
                    HRESULT (WINAPI *activate)(void *, void **) = (void *)type_vtbl[is_uwp ? 18 : 19];
                    if (activate)
                    {
                        hr = activate( xaml_type, out );
                        ERR( "ActivateInstance(%s) -> hr %#lx, instance %p\n", debugstr_w(full_name), hr, *out );
                    }
                    IInspectable_Release( (IInspectable *)xaml_type );
                }
            }
            IInspectable_Release( (IInspectable *)provider );
        }
    }
    ERR( "xaml_create_instance_from_provider(%s) -> hr %#lx, instance %p\n", debugstr_w(full_name), hr, *out );
    return hr;
}

HRESULT uwp_create_instance_from_provider( const WCHAR *full_name, void **out )
{
    return xaml_create_instance_from_provider( full_name, out );
}

struct comp_connector_vtbl
{
    HRESULT (WINAPI *QueryInterface)(void *this, REFIID riid, void **ppvObject);
    ULONG (WINAPI *AddRef)(void *this);
    ULONG (WINAPI *Release)(void *this);
    HRESULT (WINAPI *GetIids)(void *this, ULONG *iidCount, IID **iids);
    HRESULT (WINAPI *GetRuntimeClassName)(void *this, HSTRING *className);
    HRESULT (WINAPI *GetTrustLevel)(void *this, TrustLevel *trustLevel);
    HRESULT (WINAPI *Connect)(void *this, int connectionId, void *target);
    HRESULT (WINAPI *GetBindingConnector)(void *this, int connectionId, void *target, void **bindingConnector);
};

static void xaml_load_xbf_components( void *component, void *resourceLocator )
{
    static int load_depth = 0;
    struct { const struct comp_connector_vtbl *lpVtbl; } *connector = NULL;
    IInspectable *uri_insp = (IInspectable *)resourceLocator;
    void *uri_rt = NULL;
    WCHAR comp_name[MAX_PATH] = {0};
    char target_xbf[MAX_PATH] = {0};
    WCHAR pri_path[MAX_PATH];
    WCHAR *p_slash;
    WCHAR *dot;
    HANDLE hFile;
    void *binding = NULL;
    IInspectable *target_conn = (IInspectable *)component;
    BOOL is_winui_conn = TRUE;

    if (is_uwp_xaml_app())
    {
        static const GUID IID_IControl_uwp =
            { 0xa8912263, 0x2951, 0x4f58, { 0xa9, 0xc5, 0x5a, 0x13, 0x4e, 0xaa, 0x7f, 0x07 } };
        static void *uwp_xbf_controls[64];
        static unsigned int uwp_xbf_control_cnt;
        void *ctrl = NULL;
        unsigned int i;

        if (target_conn && uwp_xbf_control_cnt < ARRAY_SIZE( uwp_xbf_controls ) &&
            SUCCEEDED( IInspectable_QueryInterface( target_conn, &IID_IControl_uwp, &ctrl ) ) && ctrl)
            uwp_xbf_controls[uwp_xbf_control_cnt++] = ctrl;

        if (uwp_try_load_xbf_component( component, resourceLocator, uwp_get_app_outer() ))
        {
            for (i = 0; i < uwp_xbf_control_cnt; i++)
            {
                void *c = uwp_xbf_controls[i];
                HRESULT (WINAPI *put_enabled)( void *, boolean ) = (void *)(*(void ***)c)[23];
                if (put_enabled) put_enabled( c, TRUE );
            }
            return;
        }
    }

    if (load_depth > 4)
    {
        TRACE( "recursion depth limit exceeded (%d)\n", load_depth );
        return;
    }
    load_depth++;

    TRACE( "xaml_load_xbf_components: target_conn %p, uri_insp %p, depth %d\n", target_conn, uri_insp, load_depth );

    if (!target_conn)
    {
        load_depth--;
        return;
    }
    if (FAILED(IInspectable_QueryInterface(target_conn, &IID_IComponentConnector_x, (void **)&connector)))
    {
        is_winui_conn = FALSE;
        if (FAILED(IInspectable_QueryInterface(target_conn, &IID_IComponentConnector_UWP, (void **)&connector)))
        {
            TRACE( "failed to QI target_conn for IComponentConnector\n" );
            load_depth--;
            return;
        }
    }

    TRACE( "got connector %p\n", connector );

    if (uri_insp && SUCCEEDED(IInspectable_QueryInterface(uri_insp, &IID_IUriRuntimeClass_x, &uri_rt)))
    {
        typedef HRESULT (WINAPI *p_get_RawUri)(void *, HSTRING *);
        p_get_RawUri get_raw_uri = (p_get_RawUri)(*(void ***)uri_rt)[6]; /* slot 6: get_AbsoluteUri */
        HSTRING raw = NULL;
        if (SUCCEEDED(get_raw_uri(uri_rt, &raw)) && raw)
        {
            const WCHAR *str = WindowsGetStringRawBuffer(raw, NULL);
            const WCHAR *slash = wcsrchr(str, '/');
            TRACE( "URI raw buffer: %s\n", debugstr_w(str) );
            if (slash) slash++; else slash = str;
            lstrcpynW(comp_name, slash, MAX_PATH);
            dot = wcsrchr(comp_name, '.');
            if (dot) *dot = 0;
            WindowsDeleteString(raw);
        }
        IInspectable_Release((IInspectable *)uri_rt);
    }

    TRACE( "comp_name: %s\n", debugstr_w(comp_name) );

    if (!comp_name[0] || wcsstr(comp_name, L"ResourceDictionary") || wcsstr(comp_name, L"Dictionary"))
    {
        TRACE( "skipping element synthesis for %s\n", debugstr_w(comp_name) );
        connector->lpVtbl->Release(connector);
        load_depth--;
        return;
    }

    snprintf(target_xbf, sizeof(target_xbf), "%ls.xbf", comp_name);

    GetModuleFileNameW(NULL, pri_path, MAX_PATH);
    p_slash = wcsrchr(pri_path, '\\');
    if (p_slash) *(p_slash + 1) = 0;
    lstrcatW(pri_path, L"resources.pri");

    hFile = CreateFileW(pri_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (hFile != INVALID_HANDLE_VALUE)
    {
        DWORD size = GetFileSize(hFile, NULL);
        if (size > 0 && size < 64 * 1024 * 1024)
        {
            HANDLE hMap = CreateFileMappingW(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
            if (hMap)
            {
                const BYTE *data = MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0);
                if (data)
                {
                    int target_idx = -1;
                    int cur_idx = 0;
                    const BYTE *scan = data;
                    const BYTE *scan_limit = data + min(size, 0x100000);
                    size_t target_len = strlen(target_xbf);

                    /* 1. Find index of target_xbf in PRI name list */
                    while (scan + 5 <= scan_limit)
                    {
                        if (!memcmp(scan, ".xbf\0", 5))
                        {
                            const BYTE *b = scan;
                            while (b > data && *(b - 1) != 0 && (scan - b) < 128)
                                b--;
                            if ((size_t)(scan + 4 - b) == target_len && !strncasecmp((const char *)b, target_xbf, target_len))
                            {
                                target_idx = cur_idx;
                                TRACE( "Found XBF name '%s' at index %d\n", target_xbf, target_idx );
                                break;
                            }
                            cur_idx++;
                        }
                        scan++;
                    }

                    if (target_idx >= 0)
                    {
                        /* 2. Locate target_idx-th XBF stream */
                        int found_cnt = 0;
                        const BYTE *xbf_stream = NULL;
                        const BYTE *p = data;
                        const BYTE *end = data + size;

                        while (p + 32 < end)
                        {
                            if (p[0] == 'X' && p[1] == 'B' && p[2] == 'F' && p[3] == 0)
                            {
                                if (found_cnt == target_idx)
                                {
                                    xbf_stream = p;
                                    break;
                                }
                                found_cnt++;
                            }
                            p += 4;
                        }

                        if (xbf_stream)
                        {
                            DWORD meta_size = *(const DWORD*)(xbf_stream + 4);
                            DWORD node_size = *(const DWORD*)(xbf_stream + 8);
                            TRACE( "XBF stream for %s: meta_size %lu, node_size %lu\n",
                                   target_xbf, (unsigned long)meta_size, (unsigned long)node_size );
                            if (xbf_stream + meta_size + node_size <= end && meta_size > 0x80 && node_size > 0)
                            {
                                UINT64 str_off = *(const UINT64*)(xbf_stream + 20);
                                const BYTE *f_str = xbf_stream + (size_t)str_off + 12;
                                if (f_str + 4 <= xbf_stream + meta_size)
                                {
                                    DWORD str_cnt = *(const DWORD*)f_str;
                                    if (str_cnt > 0 && str_cnt < 2000)
                                    {
                                        const WCHAR **strings = calloc(str_cnt, sizeof(WCHAR*));
                                        const BYTE *cur = f_str + 4;
                                        const BYTE *n_cur;
                                        const BYTE *n_end;
                                        DWORD i;
                                        for (i = 0; i < str_cnt && cur + 4 <= xbf_stream + meta_size; i++)
                                        {
                                            DWORD len = *(const DWORD*)cur;
                                            cur += 4;
                                            if (cur + len * 2 + 2 <= xbf_stream + meta_size)
                                            {
                                                strings[i] = (const WCHAR*)cur;
                                                cur += len * 2 + 2;
                                            }
                                        }

                                        n_cur = xbf_stream + meta_size;
                                        n_end = n_cur + node_size;
                                        while (n_cur + 10 <= n_end)
                                        {
                                            if (n_cur[0] == 0x0c && n_cur[1] == 0x04)
                                            {
                                                DWORD conn_id = *(const DWORD*)(n_cur + 2);
                                                if (n_cur + 10 <= n_end && n_cur[6] == 0x0d && n_cur[7] == 0x05)
                                                {
                                                    WORD str_id = *(const WORD*)(n_cur + 8);
                                                    const WCHAR *elem_name = (str_id < str_cnt && strings[str_id]) ? strings[str_id] : L"";
                                                    const WCHAR *class_name = L"Microsoft.UI.Xaml.Controls.Grid";
                                                    void *target = NULL;
                                                    /* Map the XBF connection-id node to a stock control by the
                                                     * element's x:Name. Names carry the control kind by convention
                                                     * (x:Name="WebView", "TitleText", ...), which holds across apps;
                                                     * the raw conn_id is per-component and says nothing about the
                                                     * type, so it only feeds the Connect() call below. Unknown
                                                     * names fall back to Grid, which hosts any content. */
                                                    if (wcsstr(elem_name, L"ProgressRing") || wcsstr(elem_name, L"Ring"))
                                                        continue;
                                                    else if (wcsstr(elem_name, L"WebView") || wcsstr(elem_name, L"webview"))
                                                        class_name = xaml_get_app_profile()->webview2_template
                                                                     ? xaml_get_app_profile()->webview2_template
                                                                     : L"Windows.UI.Xaml.Controls.WebView2";
                                                    else if (wcsstr(elem_name, L"Image") || wcsstr(elem_name, L"image"))
                                                        class_name = L"Windows.UI.Xaml.Controls.Image";
                                                    else if (wcsstr(elem_name, L"Canvas") || wcsstr(elem_name, L"canvas"))
                                                        class_name = L"Windows.UI.Xaml.Controls.Canvas";
                                                    else if (wcsstr(elem_name, L"ErrorScreen"))
                                                        class_name = L"Windows.UI.Xaml.Controls.Grid";
                                                    else if (wcsstr(elem_name, L"Backdrop") || wcsstr(elem_name, L"Rectangle"))
                                                        class_name = L"Windows.UI.Xaml.Shapes.Rectangle";
                                                    else if (wcsstr(elem_name, L"Cactus") || wcsstr(elem_name, L"Viewbox") || wcsstr(elem_name, L"viewbox"))
                                                        class_name = L"Windows.UI.Xaml.Controls.Viewbox";
                                                    else if (wcsstr(elem_name, L"Title") || wcsstr(elem_name, L"Message") || wcsstr(elem_name, L"Text"))
                                                        class_name = L"Windows.UI.Xaml.Controls.TextBlock";
                                                    else if (wcsstr(elem_name, L"Border"))
                                                        class_name = L"Windows.UI.Xaml.Controls.Border";
                                                    else if (wcsstr(elem_name, L"Template") || wcsstr(elem_name, L"Layout"))
                                                        class_name = L"Windows.UI.Xaml.DataTemplate";
                                                    else if (wcsstr(elem_name, L"Panel") || wcsstr(elem_name, L"Stack"))
                                                        class_name = L"Windows.UI.Xaml.Controls.StackPanel";
                                                    else
                                                        class_name = L"Windows.UI.Xaml.Controls.Grid";
                                                    TRACE("XBF connect conn_id %lu, elem_name %s, class_name %s\n",
                                                          (unsigned long)conn_id, debugstr_w(elem_name), debugstr_w(class_name));
                                                    if (FAILED(xaml_create_instance_from_provider(class_name, &target)) || !target)
                                                        element_create_typed(class_name, &target);
                                                    if (target)
                                                    {
                                                        connector->lpVtbl->Connect(connector, conn_id, target);
                                                        IInspectable_Release( (IInspectable *)target );
                                                    }
                                                }
                                            }
                                            n_cur++;
                                        }
                                        free(strings);
                                    }
                                }
                            }
                        }
                    }
                    UnmapViewOfFile(data);
                }
                CloseHandle(hMap);
            }
        }
        CloseHandle(hFile);
    }

    connector->lpVtbl->Connect(connector, 1, component);
    if (is_winui_conn)
        connector->lpVtbl->GetBindingConnector(connector, 1, component, &binding);
    if (binding)
    {
        struct { const struct comp_connector_vtbl *lpVtbl; } *b_conn = binding;
        void *font_scale = NULL;

        b_conn->lpVtbl->Connect( b_conn, 1, component );
        if (!wcscmp( comp_name, L"MainWindow" ) && xaml_get_app_profile()->design_component)
        {
            if (SUCCEEDED( xaml_create_instance_from_provider( xaml_get_app_profile()->design_component, &font_scale ) ) && font_scale)
            {
                ERR( "binding font_scale %p to conn_id 6\n", font_scale );
                b_conn->lpVtbl->Connect( b_conn, 6, font_scale );
                IInspectable_Release( (IInspectable *)font_scale );
            }
        }
        b_conn->lpVtbl->Release( b_conn );
    }

    connector->lpVtbl->Release(connector);
    load_depth--;
}

static HRESULT WINAPI application_LoadComponentWithLocation( void *iface, void *component, void *resourceLocator, int location )
{
    TRACE( "iface %p, component %p, resourceLocator %p, location %d\n", iface, component, resourceLocator, location );
    xaml_load_xbf_components( component, resourceLocator );
    return S_OK;
}

static HRESULT WINAPI application_LoadComponent( void *iface, void *component, void *resourceLocator )
{
    return application_LoadComponentWithLocation( iface, component, resourceLocator, 0 );
}

static const void *factory_vtbl[] =
{
    factory_QueryInterface,
    factory_AddRef,
    factory_Release,
    factory_GetIids,
    factory_GetRuntimeClassName,
    factory_GetTrustLevel,
    application_statics_get_Current,
    application_statics_Start,
    application_LoadComponent,
    application_LoadComponentWithLocation,
    application_factory_CreateInstance,
};

/* IApplicationStatics implementation */
static HRESULT WINAPI statics_QueryInterface( IApplicationStatics *iface, REFIID iid, void **out )
{
    struct application_factory *impl = impl_from_IApplicationStatics( iface );
    return factory_QueryInterface( &impl->IActivationFactory_iface, iid, out );
}

static ULONG WINAPI statics_AddRef( IApplicationStatics *iface )
{
    struct application_factory *impl = impl_from_IApplicationStatics( iface );
    return factory_AddRef( &impl->IActivationFactory_iface );
}

static ULONG WINAPI statics_Release( IApplicationStatics *iface )
{
    struct application_factory *impl = impl_from_IApplicationStatics( iface );
    return factory_Release( &impl->IActivationFactory_iface );
}

static HRESULT WINAPI statics_GetIids( IApplicationStatics *iface, ULONG *iid_count, IID **iids )
{
    struct application_factory *impl = impl_from_IApplicationStatics( iface );
    return factory_GetIids( &impl->IActivationFactory_iface, iid_count, iids );
}

static HRESULT WINAPI statics_GetRuntimeClassName( IApplicationStatics *iface, HSTRING *class_name )
{
    struct application_factory *impl = impl_from_IApplicationStatics( iface );
    return factory_GetRuntimeClassName( &impl->IActivationFactory_iface, class_name );
}

static HRESULT WINAPI statics_GetTrustLevel( IApplicationStatics *iface, TrustLevel *trust_level )
{
    struct application_factory *impl = impl_from_IApplicationStatics( iface );
    return factory_GetTrustLevel( &impl->IActivationFactory_iface, trust_level );
}

static HRESULT WINAPI application_statics_Start( IApplicationStatics *iface,
        IApplicationInitializationCallback *callback )
{
    struct view_source *source;
    ICoreApplication *core_app = NULL;
    HSTRING class_name;
    HRESULT hr = S_OK;

    TRACE( "iface %p, callback %p\n", iface, callback );

    if (callback)
    {
        lifecycle_started = TRUE;
        hr = IApplicationInitializationCallback_Invoke( callback, NULL );
        TRACE( "callback->Invoke returned %#lx\n", hr );
    }

    source = calloc( 1, sizeof(*source) );
    if (!source) return E_OUTOFMEMORY;
    source->IFrameworkViewSource_iface.lpVtbl = &view_source_vtbl;
    source->ref = 1;

    hr = WindowsCreateString( L"Windows.ApplicationModel.Core.CoreApplication",
                              wcslen( L"Windows.ApplicationModel.Core.CoreApplication" ),
                              &class_name );
    if (SUCCEEDED(hr))
    {
        hr = RoGetActivationFactory( class_name, &IID_ICoreApplication, (void **)&core_app );
        WindowsDeleteString( class_name );
    }

    if (SUCCEEDED(hr) && core_app)
    {
        TRACE( "running CoreApplication with view source %p\n", source );
        hr = ICoreApplication_Run( core_app, &source->IFrameworkViewSource_iface );
        ICoreApplication_Release( core_app );
    }
    else
    {
        IFrameworkView *view = NULL;
        WARN( "Failed to get CoreApplication factory (hr %#lx), running view directly.\n", hr );
        if (SUCCEEDED(IFrameworkViewSource_CreateView( &source->IFrameworkViewSource_iface, &view )) && view)
        {
            hr = IFrameworkView_Run( view );
            IFrameworkView_Release( view );
        }
        else
        {
            MSG msg;
            while (GetMessageW( &msg, NULL, 0, 0 ))
            {
                TranslateMessage( &msg );
                DispatchMessageW( &msg );
            }
        }
    }

    IFrameworkViewSource_Release( &source->IFrameworkViewSource_iface );
    return hr;
}

/* Application.Current: Windows returns the activated Application (the WinUI3 app's own
 * subclass instance, bound by its constructor) and a default face when activation has
 * not run. Cache the first instance created through this factory as the singleton, the
 * way the runtime's static Current field binds it; the managed startup caches the
 * answer into a GC handle slot — a NULL here becomes a -1 sentinel the Redhawk GC card
 * scan later dereferences (mrt100_app+0x522c3 pair). */
static HRESULT WINAPI application_statics_get_Current( IApplicationStatics *iface,
        IApplication **current )
{
    struct application_factory *impl = impl_from_IApplicationStatics( iface );
    HRESULT hr = S_OK;

    TRACE( "iface %p, current %p.\n", iface, current );

    if (!current) return E_POINTER;
    if (!current_application)
    {
        IApplication *app;
        hr = application_factory_CreateInstance( &impl->IApplicationFactory_iface, NULL, NULL, &app );
        if (FAILED(hr)) return hr;
        current_application = impl_from_IApplication( app );   /* keep a strong ref */
    }
    *current = &current_application->IApplication_iface;
    IApplication_AddRef( *current );
    return hr;
}

static const void *application_statics_vtbl[] =
{
    statics_QueryInterface,
    statics_AddRef,
    statics_Release,
    statics_GetIids,
    statics_GetRuntimeClassName,
    statics_GetTrustLevel,
    application_statics_get_Current,
    application_statics_Start,
    application_LoadComponent,
    application_LoadComponentWithLocation,
};

/* IApplicationFactory implementation */
static HRESULT WINAPI app_factory_QueryInterface( IApplicationFactory *iface, REFIID iid, void **out )
{
    struct application_factory *impl = impl_from_IApplicationFactory( iface );
    return factory_QueryInterface( &impl->IActivationFactory_iface, iid, out );
}

static ULONG WINAPI app_factory_AddRef( IApplicationFactory *iface )
{
    struct application_factory *impl = impl_from_IApplicationFactory( iface );
    return factory_AddRef( &impl->IActivationFactory_iface );
}

static ULONG WINAPI app_factory_Release( IApplicationFactory *iface )
{
    struct application_factory *impl = impl_from_IApplicationFactory( iface );
    return factory_Release( &impl->IActivationFactory_iface );
}

static HRESULT WINAPI app_factory_GetIids( IApplicationFactory *iface, ULONG *iid_count, IID **iids )
{
    struct application_factory *impl = impl_from_IApplicationFactory( iface );
    return factory_GetIids( &impl->IActivationFactory_iface, iid_count, iids );
}

static HRESULT WINAPI app_factory_GetRuntimeClassName( IApplicationFactory *iface, HSTRING *class_name )
{
    struct application_factory *impl = impl_from_IApplicationFactory( iface );
    return factory_GetRuntimeClassName( &impl->IActivationFactory_iface, class_name );
}

static HRESULT WINAPI app_factory_GetTrustLevel( IApplicationFactory *iface, TrustLevel *trust_level )
{
    struct application_factory *impl = impl_from_IApplicationFactory( iface );
    return factory_GetTrustLevel( &impl->IActivationFactory_iface, trust_level );
}

static HRESULT WINAPI application_factory_CreateInstance( IApplicationFactory *iface,
        IInspectable *outer, IInspectable **inner, IApplication **instance )
{
    struct application *impl;

    ERR( "application_factory_CreateInstance outer %p, inner %p, instance %p\n", outer, inner, instance );

    impl = calloc( 1, sizeof(*impl) );
    if (!impl) return E_OUTOFMEMORY;

    impl->IApplication_iface.lpVtbl = (const struct IApplicationVtbl *)&application_vtbl;
    impl->inner_iface.lpVtbl = (const struct IInspectableVtbl *)&application_inner_vtbl;
    impl->ms_IApplication_iface.lpVtbl = (const struct IInspectableVtbl *)ms_application_vtbl;
    impl->overrides_uwp_iface.lpVtbl = (const struct IInspectableVtbl *)application_overrides_uwp_vtbl;
    impl->metadata_provider_iface.lpVtbl = (const struct IInspectableVtbl *)app_meta_provider_vtbl;
    impl->ref = 1;
    impl->outer = outer;

    if (!current_application)
    {
        current_application = impl;
        IApplication_AddRef( &impl->IApplication_iface );
    }

    if (outer)
    {
        /* In Mcg / .NET Native composable constructors, the generated caller passes only
         * (this, outer); unpassed registers hold caller-saved garbage (e.g. instance == outer).
         * Only write to inner/instance if valid distinct pointers were provided. */
        if (inner && (void *)inner != (void *)outer)
        {
            *inner = &impl->inner_iface;
            IInspectable_AddRef( *inner );
        }
        if (instance && (void *)instance != (void *)outer)
        {
            *instance = &impl->IApplication_iface;
            IApplication_AddRef( *instance );
        }

        /* If this is the main application instance registration (called from Main/App..ctor),
         * drive the view lifecycle and message pump for pure UWP apps. FullTrust / XAML Islands
         * apps (seed_xbf == NULL) construct Application and run their own Win32 window + pump. */
        if (!lifecycle_started && xaml_get_app_profile()->seed_xbf)
        {
            struct view_source *source;
            lifecycle_started = TRUE;
            source = calloc( 1, sizeof(*source) );
            if (source)
            {
                ICoreApplication *core_app = NULL;
                HSTRING class_name;
                HRESULT hr;

                source->IFrameworkViewSource_iface.lpVtbl = &view_source_vtbl;
                source->ref = 1;

                hr = WindowsCreateString( L"Windows.ApplicationModel.Core.CoreApplication",
                                          wcslen( L"Windows.ApplicationModel.Core.CoreApplication" ),
                                          &class_name );
                if (SUCCEEDED(hr))
                {
                    hr = RoGetActivationFactory( class_name, &IID_ICoreApplication, (void **)&core_app );
                    WindowsDeleteString( class_name );
                }

                if (SUCCEEDED(hr) && core_app)
                {
                    TRACE( "driving CoreApplication view lifecycle with source %p\n", source );
                    hr = ICoreApplication_Run( core_app, &source->IFrameworkViewSource_iface );
                    ICoreApplication_Release( core_app );
                }
                else
                {
                    IFrameworkView *view = NULL;
                    WARN( "CoreApplication factory unavailable (hr %#lx), driving view directly\n", hr );
                    if (SUCCEEDED(IFrameworkViewSource_CreateView( &source->IFrameworkViewSource_iface, &view )) && view)
                    {
                        hr = IFrameworkView_Run( view );
                        IFrameworkView_Release( view );
                    }
                }
                IFrameworkViewSource_Release( &source->IFrameworkViewSource_iface );
            }
        }
    }
    else
    {
        if (inner) *inner = NULL;
        if (instance)
        {
            *instance = &impl->IApplication_iface;
            IApplication_AddRef( *instance );
        }
    }

    return S_OK;
}

struct application_factory_extended_vtbl
{
    HRESULT (WINAPI *QueryInterface)(IApplicationFactory *, REFIID, void **);
    ULONG (WINAPI *AddRef)(IApplicationFactory *);
    ULONG (WINAPI *Release)(IApplicationFactory *);
    HRESULT (WINAPI *GetIids)(IApplicationFactory *, ULONG *, IID **);
    HRESULT (WINAPI *GetRuntimeClassName)(IApplicationFactory *, HSTRING *);
    HRESULT (WINAPI *GetTrustLevel)(IApplicationFactory *, TrustLevel *);
    HRESULT (WINAPI *CreateInstance)(IApplicationFactory *, IInspectable *, IInspectable **, IApplication **);
    HRESULT (WINAPI *Start)(IApplicationStatics *, IApplicationInitializationCallback *);
    HRESULT (WINAPI *get_Current)(IApplicationStatics *, IApplication **);
    void *pad[16];
};

static const struct application_factory_extended_vtbl application_factory_vtbl =
{
    app_factory_QueryInterface,
    app_factory_AddRef,
    app_factory_Release,
    app_factory_GetIids,
    app_factory_GetRuntimeClassName,
    app_factory_GetTrustLevel,
    application_factory_CreateInstance,
    application_statics_Start,
    application_statics_get_Current,
    {
        application_stub_zero, application_stub_zero, application_stub_zero, application_stub_zero,
        application_stub_zero, application_stub_zero, application_stub_zero, application_stub_zero,
        application_stub_zero, application_stub_zero, application_stub_zero, application_stub_zero,
        application_stub_zero, application_stub_zero, application_stub_zero, application_stub_zero,
    }
};

static HRESULT WINAPI ms_statics_QueryInterface( IApplicationStatics *iface, REFIID iid, void **out )
{
    struct application_factory *impl = impl_from_ms_IApplicationStatics( iface );
    return factory_QueryInterface( &impl->IActivationFactory_iface, iid, out );
}
static ULONG WINAPI ms_statics_AddRef( IApplicationStatics *iface )
{
    struct application_factory *impl = impl_from_ms_IApplicationStatics( iface );
    return factory_AddRef( &impl->IActivationFactory_iface );
}
static ULONG WINAPI ms_statics_Release( IApplicationStatics *iface )
{
    struct application_factory *impl = impl_from_ms_IApplicationStatics( iface );
    return factory_Release( &impl->IActivationFactory_iface );
}

static HRESULT WINAPI ms_application_statics_get_Current( IApplicationStatics *iface,
        IApplication **current )
{
    struct application_factory *impl = impl_from_ms_IApplicationStatics( iface );
    ERR( "ms_application_statics_get_Current iface %p, current %p, current_application %p, outer %p\n",
         iface, current, current_application, current_application ? current_application->outer : NULL );
    if (!current) return E_POINTER;
    if (!current_application)
    {
        IApplication *app;
        HRESULT hr = application_factory_CreateInstance( &impl->IApplicationFactory_iface, NULL, NULL, &app );
        if (FAILED(hr)) return hr;
        current_application = impl_from_IApplication( app );
    }
    if (current_application->outer)
    {
        *current = (IApplication *)current_application->outer;
        IInspectable_AddRef( current_application->outer );
        return S_OK;
    }
    *current = (IApplication *)&current_application->ms_IApplication_iface;
    IInspectable_AddRef( &current_application->ms_IApplication_iface );
    return S_OK;
}

static HRESULT WINAPI ms_statics_GetRuntimeClassName( IApplicationStatics *iface, HSTRING *class_name )
{
    return WindowsCreateString( L"Microsoft.UI.Xaml.Application", 28, class_name );
}

static const void *ms_application_statics_vtbl[] =
{
    ms_statics_QueryInterface,           /* 0: QI */
    ms_statics_AddRef,                   /* 1: AddRef */
    ms_statics_Release,                  /* 2: Release */
    statics_GetIids,                     /* 3: GetIids */
    ms_statics_GetRuntimeClassName,      /* 4: GetRuntimeClassName */
    statics_GetTrustLevel,               /* 5: GetTrustLevel */
    ms_application_statics_get_Current,  /* 6: get_Current */
    application_statics_Start,           /* 7: Start */
    application_LoadComponent,           /* 8: LoadComponent */
    application_LoadComponentWithLocation,/* 9: LoadComponent */
    application_stub_zero, application_stub_zero, application_stub_zero, application_stub_zero,
};

static struct application_factory application_factory_obj =
{
    {(const struct IActivationFactoryVtbl *)&factory_vtbl},
    {(const struct IApplicationStaticsVtbl *)application_statics_vtbl},
    {(const struct IApplicationStaticsVtbl *)ms_application_statics_vtbl},
    {(const struct IApplicationFactoryVtbl *)&application_factory_vtbl},
    {&view_source_vtbl},
    1,
};

IActivationFactory *application_factory = &application_factory_obj.IActivationFactory_iface;

HRESULT xaml_create_page( void *type_name_ptr, IInspectable **out )
{
    static const GUID IID_IXamlMetadataProvider =
        { 0xb3765d69, 0x68a5, 0x4b32, { 0x88, 0x61, 0xfd, 0xb9, 0x0c, 0x1f, 0x58, 0x36 } };
    TypeName *type = (TypeName *)type_name_ptr;
    void *provider = NULL;
    void *xaml_type = NULL;
    HRESULT hr;

    if (!out) return E_POINTER;
    *out = NULL;

    if (current_application && current_application->outer)
    {
        hr = IInspectable_QueryInterface( current_application->outer,
                                          &IID_IXamlMetadataProvider, &provider );
        TRACE( "QI IXamlMetadataProvider on outer %p returned hr %#lx, provider %p\n",
               current_application->outer, hr, provider );
        if (SUCCEEDED(hr) && provider)
        {
            void **vtbl = *(void ***)provider;
            /* Slot 6: GetXamlType(TypeName, IXamlType**) */
            HRESULT (WINAPI *get_xaml_type)(void *, TypeName, void **) = (void *)vtbl[6];
            /* Slot 7: GetXamlTypeByFullName(HSTRING, IXamlType**) */
            HRESULT (WINAPI *get_by_name)(void *, HSTRING, void **) = (void *)vtbl[7];

            hr = E_FAIL;
            if (get_by_name && type && type->Name)
            {
                hr = get_by_name( provider, type->Name, &xaml_type );
                TRACE( "GetXamlTypeByFullName(%s) returned hr %#lx, xaml_type %p\n",
                       debugstr_hstring( type->Name ), hr, xaml_type );
            }
            if (FAILED(hr) || !xaml_type)
            {
                if (get_xaml_type && type)
                {
                    hr = get_xaml_type( provider, *type, &xaml_type );
                    TRACE( "GetXamlType returned hr %#lx, xaml_type %p\n", hr, xaml_type );
                }
            }
            if (SUCCEEDED(hr) && xaml_type)
            {
                void **type_vtbl = *(void ***)xaml_type;
                /* UWP IXamlType has ActivateInstance at slot 18; WinUI 3 has it at slot 19 */
                int act_slot = is_uwp_xaml_app() ? 18 : 19;
                HRESULT (WINAPI *activate)(void *, void **) = (void *)type_vtbl[act_slot];
                TRACE( "calling IXamlType::ActivateInstance slot %d (%p)\n", act_slot, activate );
                if (activate)
                {
                    hr = activate( xaml_type, (void **)out );
                    TRACE( "IXamlType::ActivateInstance returned hr %#lx, instance %p\n", hr, *out );
                }
                IInspectable_Release( (IInspectable *)xaml_type );
            }
            IInspectable_Release( (IInspectable *)provider );
            if (SUCCEEDED(hr) && *out) return hr;
        }
    }

    if (type && type->Name)
        return RoActivateInstance( type->Name, out );
    return E_FAIL;
}
