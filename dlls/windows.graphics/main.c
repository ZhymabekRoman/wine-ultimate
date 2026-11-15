/* WinRT Windows.Graphics Implementation
 *
 * Copyright 2026 Zhiyi Zhang for CodeWeavers
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

#include "initguid.h"
#include "private.h"

WINE_DEFAULT_DEBUG_CHANNEL(display);

struct event_entry
{
    INT64 token;
    ITypedEventHandler_DisplayInformation_IInspectable *handler;
};

struct event_list
{
    struct event_entry *entries;
    SIZE_T count;
    SIZE_T capacity;
};

static LONG64 next_event_token;

static CRITICAL_SECTION statics_cs;
static CRITICAL_SECTION_DEBUG statics_cs_debug =
{
    0, 0, &statics_cs,
    { &statics_cs_debug.ProcessLocksList, &statics_cs_debug.ProcessLocksList },
      0, 0, { (DWORD_PTR)(__FILE__ ": statics_cs") }
};
static CRITICAL_SECTION statics_cs = { &statics_cs_debug, -1, 0, 0, 0, 0 };

static DisplayOrientations auto_rotation_preferences = DisplayOrientations_None;
static struct event_list contents_invalidated_events;

static HRESULT event_list_add( CRITICAL_SECTION *cs, struct event_list *list,
                               ITypedEventHandler_DisplayInformation_IInspectable *handler,
                               EventRegistrationToken *token )
{
    struct event_entry *new_entries;
    SIZE_T new_cap;
    INT64 val;

    if (!handler) return E_INVALIDARG;
    if (!token) return E_POINTER;

    EnterCriticalSection( cs );
    if (list->count == list->capacity)
    {
        new_cap = list->capacity ? list->capacity * 2 : 4;
        if (!(new_entries = realloc( list->entries, new_cap * sizeof(*new_entries) )))
        {
            LeaveCriticalSection( cs );
            return E_OUTOFMEMORY;
        }
        list->entries = new_entries;
        list->capacity = new_cap;
    }

    val = InterlockedIncrement64( &next_event_token );
    ITypedEventHandler_DisplayInformation_IInspectable_AddRef( handler );
    list->entries[list->count].token = val;
    list->entries[list->count].handler = handler;
    list->count++;
    token->value = val;
    LeaveCriticalSection( cs );
    return S_OK;
}

static HRESULT event_list_remove( CRITICAL_SECTION *cs, struct event_list *list, EventRegistrationToken token )
{
    SIZE_T i;

    if (!token.value) return S_OK;

    EnterCriticalSection( cs );
    for (i = 0; i < list->count; i++)
    {
        if (list->entries[i].token != token.value) continue;
        ITypedEventHandler_DisplayInformation_IInspectable_Release( list->entries[i].handler );
        list->entries[i] = list->entries[list->count - 1];
        list->count--;
        break;
    }
    LeaveCriticalSection( cs );
    return S_OK;
}

static void event_list_clear( struct event_list *list )
{
    SIZE_T i;

    for (i = 0; i < list->count; i++)
        ITypedEventHandler_DisplayInformation_IInspectable_Release( list->entries[i].handler );
    free( list->entries );
    memset( list, 0, sizeof(*list) );
}

static void event_list_fire( CRITICAL_SECTION *cs, struct event_list *list, IDisplayInformation *sender )
{
    ITypedEventHandler_DisplayInformation_IInspectable **snapshot = NULL;
    SIZE_T count = 0, i;

    EnterCriticalSection( cs );
    if (list->count)
    {
        count = list->count;
        if ((snapshot = malloc( count * sizeof(*snapshot) )))
        {
            for (i = 0; i < count; i++)
            {
                snapshot[i] = list->entries[i].handler;
                ITypedEventHandler_DisplayInformation_IInspectable_AddRef( snapshot[i] );
            }
        }
        else count = 0;
    }
    LeaveCriticalSection( cs );

    for (i = 0; i < count; i++)
    {
        ITypedEventHandler_DisplayInformation_IInspectable_Invoke( snapshot[i], sender, NULL );
        ITypedEventHandler_DisplayInformation_IInspectable_Release( snapshot[i] );
    }
    free( snapshot );
}

/* ---- AdvancedColorInfo ---- */

struct advanced_color_info
{
    IAdvancedColorInfo IAdvancedColorInfo_iface;
    LONG ref;
};

static inline struct advanced_color_info *impl_from_IAdvancedColorInfo( IAdvancedColorInfo *iface )
{
    return CONTAINING_RECORD( iface, struct advanced_color_info, IAdvancedColorInfo_iface );
}

static HRESULT WINAPI advanced_color_QueryInterface( IAdvancedColorInfo *iface, REFIID iid, void **out )
{
    struct advanced_color_info *impl = impl_from_IAdvancedColorInfo( iface );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IAdvancedColorInfo ))
    {
        *out = &impl->IAdvancedColorInfo_iface;
        IAdvancedColorInfo_AddRef( *out );
        return S_OK;
    }

    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI advanced_color_AddRef( IAdvancedColorInfo *iface )
{
    struct advanced_color_info *impl = impl_from_IAdvancedColorInfo( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI advanced_color_Release( IAdvancedColorInfo *iface )
{
    struct advanced_color_info *impl = impl_from_IAdvancedColorInfo( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref) free( impl );
    return ref;
}

static HRESULT WINAPI advanced_color_GetIids( IAdvancedColorInfo *iface, ULONG *count, IID **iids )
{
    static const IID iid_array[] = { IID_IAdvancedColorInfo };
    IID *array;

    if (!count || !iids) return E_POINTER;
    if (!(array = CoTaskMemAlloc( sizeof(iid_array) ))) return E_OUTOFMEMORY;
    memcpy( array, iid_array, sizeof(iid_array) );
    *count = ARRAY_SIZE(iid_array);
    *iids = array;
    return S_OK;
}

static HRESULT WINAPI advanced_color_GetRuntimeClassName( IAdvancedColorInfo *iface, HSTRING *name )
{
    return WindowsCreateString( RuntimeClass_Windows_Graphics_Display_AdvancedColorInfo,
                                wcslen( RuntimeClass_Windows_Graphics_Display_AdvancedColorInfo ), name );
}

static HRESULT WINAPI advanced_color_GetTrustLevel( IAdvancedColorInfo *iface, TrustLevel *level )
{
    if (!level) return E_POINTER;
    *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI advanced_color_get_CurrentAdvancedColorKind( IAdvancedColorInfo *iface, AdvancedColorKind *value )
{
    if (!value) return E_POINTER;
    *value = AdvancedColorKind_StandardDynamicRange;
    return S_OK;
}

static HRESULT WINAPI advanced_color_get_RedPrimary( IAdvancedColorInfo *iface, Point *value )
{
    if (!value) return E_POINTER;
    value->X = 0.64f;
    value->Y = 0.33f;
    return S_OK;
}

static HRESULT WINAPI advanced_color_get_GreenPrimary( IAdvancedColorInfo *iface, Point *value )
{
    if (!value) return E_POINTER;
    value->X = 0.30f;
    value->Y = 0.60f;
    return S_OK;
}

static HRESULT WINAPI advanced_color_get_BluePrimary( IAdvancedColorInfo *iface, Point *value )
{
    if (!value) return E_POINTER;
    value->X = 0.15f;
    value->Y = 0.06f;
    return S_OK;
}

static HRESULT WINAPI advanced_color_get_WhitePoint( IAdvancedColorInfo *iface, Point *value )
{
    if (!value) return E_POINTER;
    value->X = 0.3127f;
    value->Y = 0.3290f;
    return S_OK;
}

static HRESULT WINAPI advanced_color_get_MaxLuminanceInNits( IAdvancedColorInfo *iface, FLOAT *value )
{
    if (!value) return E_POINTER;
    *value = 80.0f;
    return S_OK;
}

static HRESULT WINAPI advanced_color_get_MinLuminanceInNits( IAdvancedColorInfo *iface, FLOAT *value )
{
    if (!value) return E_POINTER;
    *value = 0.0f;
    return S_OK;
}

static HRESULT WINAPI advanced_color_get_MaxAverageFullFrameLuminanceInNits( IAdvancedColorInfo *iface, FLOAT *value )
{
    if (!value) return E_POINTER;
    *value = 80.0f;
    return S_OK;
}

static HRESULT WINAPI advanced_color_get_SdrWhiteLevelInNits( IAdvancedColorInfo *iface, FLOAT *value )
{
    if (!value) return E_POINTER;
    *value = 80.0f;
    return S_OK;
}

static HRESULT WINAPI advanced_color_IsHdrMetadataFormatCurrentlySupported( IAdvancedColorInfo *iface,
        HdrMetadataFormat format, boolean *result )
{
    if (!result) return E_POINTER;
    *result = FALSE;
    return S_OK;
}

static HRESULT WINAPI advanced_color_IsAdvancedColorKindAvailable( IAdvancedColorInfo *iface,
        AdvancedColorKind kind, boolean *result )
{
    if (!result) return E_POINTER;
    *result = (kind == AdvancedColorKind_StandardDynamicRange);
    return S_OK;
}

static const struct IAdvancedColorInfoVtbl advanced_color_vtbl =
{
    advanced_color_QueryInterface,
    advanced_color_AddRef,
    advanced_color_Release,
    advanced_color_GetIids,
    advanced_color_GetRuntimeClassName,
    advanced_color_GetTrustLevel,
    advanced_color_get_CurrentAdvancedColorKind,
    advanced_color_get_RedPrimary,
    advanced_color_get_GreenPrimary,
    advanced_color_get_BluePrimary,
    advanced_color_get_WhitePoint,
    advanced_color_get_MaxLuminanceInNits,
    advanced_color_get_MinLuminanceInNits,
    advanced_color_get_MaxAverageFullFrameLuminanceInNits,
    advanced_color_get_SdrWhiteLevelInNits,
    advanced_color_IsHdrMetadataFormatCurrentlySupported,
    advanced_color_IsAdvancedColorKindAvailable,
};

/* ---- DisplayInformationStatics ---- */

struct display_info_statics
{
    IActivationFactory IActivationFactory_iface;
    IDisplayInformationStatics IDisplayInformationStatics_iface;
    LONG ref;
};

static inline struct display_info_statics *impl_from_IActivationFactory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct display_info_statics, IActivationFactory_iface );
}

static HRESULT WINAPI factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct display_info_statics *impl = impl_from_IActivationFactory( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown )
        || IsEqualGUID( iid, &IID_IInspectable )
        || IsEqualGUID( iid, &IID_IAgileObject )
        || IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = &impl->IActivationFactory_iface;
        IActivationFactory_AddRef( *out );
        return S_OK;
    }
    else if (IsEqualGUID( iid, &IID_IDisplayInformationStatics ))
    {
        *out = &impl->IDisplayInformationStatics_iface;
        IDisplayInformationStatics_AddRef( *out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI factory_AddRef( IActivationFactory *iface )
{
    struct display_info_statics *impl = impl_from_IActivationFactory( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p, ref %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI factory_Release( IActivationFactory *iface )
{
    struct display_info_statics *impl = impl_from_IActivationFactory( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    TRACE( "iface %p, ref %lu.\n", iface, ref );
    return ref;
}

static HRESULT WINAPI factory_GetIids( IActivationFactory *iface, ULONG *iid_count, IID **iids )
{
    static const IID iid_array[] =
    {
        IID_IActivationFactory,
        IID_IDisplayInformationStatics,
    };
    IID *array;

    if (!iid_count || !iids) return E_POINTER;
    if (!(array = CoTaskMemAlloc( sizeof(iid_array) ))) return E_OUTOFMEMORY;
    memcpy( array, iid_array, sizeof(iid_array) );
    *iid_count = ARRAY_SIZE(iid_array);
    *iids = array;
    return S_OK;
}

static HRESULT WINAPI factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *class_name )
{
    if (!class_name) return E_POINTER;
    return WindowsCreateString( RuntimeClass_Windows_Graphics_Display_DisplayInformation,
                                wcslen( RuntimeClass_Windows_Graphics_Display_DisplayInformation ), class_name );
}

static HRESULT WINAPI factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *trust_level )
{
    if (!trust_level) return E_POINTER;
    *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    FIXME( "iface %p, instance %p stub!\n", iface, instance );
    return E_NOTIMPL;
}

static const struct IActivationFactoryVtbl factory_vtbl =
{
    factory_QueryInterface,
    factory_AddRef,
    factory_Release,
    /* IInspectable methods */
    factory_GetIids,
    factory_GetRuntimeClassName,
    factory_GetTrustLevel,
    /* IActivationFactory methods */
    factory_ActivateInstance,
};

/* ---- DisplayInformation ---- */

struct display_information
{
    IDisplayInformation IDisplayInformation_iface;
    IDisplayInformation2 IDisplayInformation2_iface;
    IDisplayInformation3 IDisplayInformation3_iface;
    IDisplayInformation4 IDisplayInformation4_iface;
    IDisplayInformation5 IDisplayInformation5_iface;
    LONG ref;
    HWND hwnd;
    DWORD owner_tid;
    CRITICAL_SECTION cs;
    struct event_list orientation_changed;
    struct event_list dpi_changed;
    struct event_list stereo_changed;
    struct event_list color_profile_changed;
    struct event_list advanced_color_changed;
    HANDLE stop_event;
    HANDLE poll_thread;
    DWORD poll_tid;
    UINT last_dpi;
    DisplayOrientations last_orientation;
};

static inline struct display_information *impl_from_IDisplayInformation( IDisplayInformation *iface )
{
    return CONTAINING_RECORD( iface, struct display_information, IDisplayInformation_iface );
}
static inline struct display_information *impl_from_IDisplayInformation2( IDisplayInformation2 *iface )
{
    return CONTAINING_RECORD( iface, struct display_information, IDisplayInformation2_iface );
}
static inline struct display_information *impl_from_IDisplayInformation3( IDisplayInformation3 *iface )
{
    return CONTAINING_RECORD( iface, struct display_information, IDisplayInformation3_iface );
}
static inline struct display_information *impl_from_IDisplayInformation4( IDisplayInformation4 *iface )
{
    return CONTAINING_RECORD( iface, struct display_information, IDisplayInformation4_iface );
}
static inline struct display_information *impl_from_IDisplayInformation5( IDisplayInformation5 *iface )
{
    return CONTAINING_RECORD( iface, struct display_information, IDisplayInformation5_iface );
}

static BOOL CALLBACK enum_thread_top_hwnd( HWND hwnd, LPARAM lparam )
{
    HWND *out = (HWND *)lparam;
    *out = hwnd;
    return FALSE;
}

static HWND find_thread_hwnd( DWORD tid )
{
    HWND hwnd = NULL;

    if (tid == GetCurrentThreadId()) hwnd = GetActiveWindow();
    if (!hwnd && tid) EnumThreadWindows( tid, enum_thread_top_hwnd, (LPARAM)&hwnd );
    return hwnd;
}

static HWND resolve_view_hwnd( struct display_information *impl )
{
    HWND hwnd;

    EnterCriticalSection( &impl->cs );
    hwnd = impl->hwnd;
    if (hwnd && !IsWindow( hwnd ))
    {
        hwnd = NULL;
        impl->hwnd = NULL;
    }
    LeaveCriticalSection( &impl->cs );

    if (!hwnd)
    {
        hwnd = find_thread_hwnd( impl->owner_tid );
        if (hwnd)
        {
            EnterCriticalSection( &impl->cs );
            if (!impl->hwnd || !IsWindow( impl->hwnd )) impl->hwnd = hwnd;
            hwnd = impl->hwnd;
            LeaveCriticalSection( &impl->cs );
        }
    }

    if (!hwnd) hwnd = GetForegroundWindow();
    return hwnd;
}

static void get_display_metrics( struct display_information *impl, UINT *dpi_out,
                                 UINT32 *width_out, UINT32 *height_out,
                                 DisplayOrientations *native_out, DisplayOrientations *current_out )
{
    HWND hwnd = resolve_view_hwnd( impl );
    HMONITOR hmon = NULL;
    MONITORINFOEXW mi;
    DEVMODEW dm;
    UINT dpi = 0;
    LONG w = 0, h = 0, nw, nh;
    DWORD dm_orient = DMDO_DEFAULT;
    BOOL have_dm_orient = FALSE;
    DisplayOrientations native_orient, cur_orient;

    if (hwnd) dpi = GetDpiForWindow( hwnd );
    if (!dpi) dpi = GetDpiForSystem();
    if (!dpi) dpi = 96;

    if (hwnd) hmon = MonitorFromWindow( hwnd, MONITOR_DEFAULTTOPRIMARY );
    if (!hmon)
    {
        POINT pt = {0, 0};
        hmon = MonitorFromPoint( pt, MONITOR_DEFAULTTOPRIMARY );
    }

    memset( &mi, 0, sizeof(mi) );
    mi.cbSize = sizeof(mi);
    if (hmon && GetMonitorInfoW( hmon, (MONITORINFO *)&mi ))
    {
        w = mi.rcMonitor.right - mi.rcMonitor.left;
        h = mi.rcMonitor.bottom - mi.rcMonitor.top;
    }

    if (w <= 0 || h <= 0)
    {
        RECT rc;
        if (hwnd && GetClientRect( hwnd, &rc ) && rc.right > rc.left && rc.bottom > rc.top)
        {
            w = rc.right - rc.left;
            h = rc.bottom - rc.top;
        }
        else
        {
            w = GetSystemMetrics( SM_CXSCREEN );
            h = GetSystemMetrics( SM_CYSCREEN );
        }
    }
    if (w <= 0) w = 1024;
    if (h <= 0) h = 768;

    if (mi.szDevice[0])
    {
        memset( &dm, 0, sizeof(dm) );
        dm.dmSize = sizeof(dm);
        if (EnumDisplaySettingsW( mi.szDevice, ENUM_CURRENT_SETTINGS, &dm ) &&
            (dm.dmFields & DM_DISPLAYORIENTATION))
        {
            dm_orient = dm.dmDisplayOrientation;
            have_dm_orient = TRUE;
        }
    }

    if (have_dm_orient && (dm_orient == DMDO_90 || dm_orient == DMDO_270))
    {
        nw = h;
        nh = w;
    }
    else
    {
        nw = w;
        nh = h;
    }

    native_orient = (nw >= nh) ? DisplayOrientations_Landscape : DisplayOrientations_Portrait;
    cur_orient = (w >= h) ? DisplayOrientations_Landscape : DisplayOrientations_Portrait;

    if (have_dm_orient)
    {
        switch (dm_orient)
        {
        case DMDO_90:
            cur_orient = (native_orient == DisplayOrientations_Landscape)
                         ? DisplayOrientations_Portrait : DisplayOrientations_LandscapeFlipped;
            break;
        case DMDO_180:
            cur_orient = (native_orient == DisplayOrientations_Landscape)
                         ? DisplayOrientations_LandscapeFlipped : DisplayOrientations_PortraitFlipped;
            break;
        case DMDO_270:
            cur_orient = (native_orient == DisplayOrientations_Landscape)
                         ? DisplayOrientations_PortraitFlipped : DisplayOrientations_Landscape;
            break;
        default:
            cur_orient = native_orient;
            break;
        }
    }

    if (dpi_out) *dpi_out = dpi;
    if (width_out) *width_out = (UINT32)w;
    if (height_out) *height_out = (UINT32)h;
    if (native_out) *native_out = native_orient;
    if (current_out) *current_out = cur_orient;
}

static ResolutionScale scale_from_dpi( UINT dpi )
{
    UINT pct = MulDiv( dpi, 100, 96 );

    if (pct <= 112) return ResolutionScale_Scale100Percent;
    if (pct <= 137) return ResolutionScale_Scale125Percent;
    if (pct <= 162) return ResolutionScale_Scale150Percent;
    if (pct <= 187) return ResolutionScale_Scale175Percent;
    if (pct <= 212) return ResolutionScale_Scale200Percent;
    if (pct <= 237) return ResolutionScale_Scale225Percent;
    if (pct <= 275) return ResolutionScale_Scale250Percent;
    if (pct <= 325) return ResolutionScale_Scale300Percent;
    if (pct <= 375) return ResolutionScale_Scale350Percent;
    if (pct <= 425) return ResolutionScale_Scale400Percent;
    if (pct <= 475) return ResolutionScale_Scale450Percent;
    return ResolutionScale_Scale500Percent;
}

static void check_display_changes( struct display_information *impl )
{
    UINT dpi;
    DisplayOrientations orient;
    BOOL dpi_changed, orient_changed;

    get_display_metrics( impl, &dpi, NULL, NULL, NULL, &orient );

    EnterCriticalSection( &impl->cs );
    dpi_changed = (dpi != impl->last_dpi);
    orient_changed = (orient != impl->last_orientation);
    impl->last_dpi = dpi;
    impl->last_orientation = orient;
    LeaveCriticalSection( &impl->cs );

    if (dpi_changed)
        event_list_fire( &impl->cs, &impl->dpi_changed, &impl->IDisplayInformation_iface );
    if (orient_changed)
        event_list_fire( &impl->cs, &impl->orientation_changed, &impl->IDisplayInformation_iface );
}

static LRESULT CALLBACK display_notify_wndproc( HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam )
{
    if (msg == WM_DPICHANGED || msg == WM_DISPLAYCHANGE)
    {
        struct display_information *impl = (struct display_information *)GetWindowLongPtrW( hwnd, GWLP_USERDATA );
        if (impl) check_display_changes( impl );
    }
    return DefWindowProcW( hwnd, msg, wparam, lparam );
}

static DWORD WINAPI display_poll_worker( void *arg )
{
    struct display_information *impl = arg;
    HWND notify_hwnd;

    SetThreadDpiAwarenessContext( DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 );
    notify_hwnd = CreateWindowExW( 0, L"static", NULL, WS_POPUP, 0, 0, 0, 0, NULL, NULL, NULL, NULL );
    if (notify_hwnd)
    {
        SetWindowLongPtrW( notify_hwnd, GWLP_USERDATA, (LONG_PTR)impl );
        SetWindowLongPtrW( notify_hwnd, GWLP_WNDPROC, (LONG_PTR)display_notify_wndproc );
    }

    for (;;)
    {
        DWORD ret = MsgWaitForMultipleObjects( 1, &impl->stop_event, FALSE, 500, QS_ALLINPUT );

        if (ret == WAIT_OBJECT_0)
            break;
        if (ret == WAIT_OBJECT_0 + 1)
        {
            MSG msg;
            while (PeekMessageW( &msg, NULL, 0, 0, PM_REMOVE ))
            {
                TranslateMessage( &msg );
                DispatchMessageW( &msg );
            }
            check_display_changes( impl );
        }
        else if (ret == WAIT_TIMEOUT)
        {
            check_display_changes( impl );
        }
        else
            break;
    }

    if (notify_hwnd) DestroyWindow( notify_hwnd );
    return 0;
}

static HRESULT ensure_poll_thread( struct display_information *impl )
{
    UINT dpi;
    DisplayOrientations orient;

    EnterCriticalSection( &impl->cs );
    if (impl->poll_thread)
    {
        LeaveCriticalSection( &impl->cs );
        return S_OK;
    }
    LeaveCriticalSection( &impl->cs );

    get_display_metrics( impl, &dpi, NULL, NULL, NULL, &orient );

    EnterCriticalSection( &impl->cs );
    if (!impl->poll_thread)
    {
        impl->last_dpi = dpi;
        impl->last_orientation = orient;
        if (!impl->stop_event) impl->stop_event = CreateEventW( NULL, TRUE, FALSE, NULL );
        if (!impl->stop_event)
        {
            LeaveCriticalSection( &impl->cs );
            return E_OUTOFMEMORY;
        }
        impl->poll_thread = CreateThread( NULL, 0, display_poll_worker, impl, 0, &impl->poll_tid );
        if (!impl->poll_thread)
        {
            LeaveCriticalSection( &impl->cs );
            return HRESULT_FROM_WIN32( GetLastError() );
        }
    }
    LeaveCriticalSection( &impl->cs );
    return S_OK;
}

static HRESULT WINAPI display_info_QueryInterface( IDisplayInformation *iface, REFIID iid, void **out )
{
    struct display_information *impl = impl_from_IDisplayInformation( iface );
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IDisplayInformation ))
    {
        *out = &impl->IDisplayInformation_iface;
        IDisplayInformation_AddRef( *out );
        return S_OK;
    }
    else if (IsEqualGUID( iid, &IID_IDisplayInformation2 ))
    {
        *out = &impl->IDisplayInformation2_iface;
        IDisplayInformation2_AddRef( *out );
        return S_OK;
    }
    else if (IsEqualGUID( iid, &IID_IDisplayInformation3 ))
    {
        *out = &impl->IDisplayInformation3_iface;
        IDisplayInformation3_AddRef( *out );
        return S_OK;
    }
    else if (IsEqualGUID( iid, &IID_IDisplayInformation4 ))
    {
        *out = &impl->IDisplayInformation4_iface;
        IDisplayInformation4_AddRef( *out );
        return S_OK;
    }
    else if (IsEqualGUID( iid, &IID_IDisplayInformation5 ))
    {
        *out = &impl->IDisplayInformation5_iface;
        IDisplayInformation5_AddRef( *out );
        return S_OK;
    }
    FIXME( "%s not implemented.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI display_info_AddRef( IDisplayInformation *iface )
{
    struct display_information *impl = impl_from_IDisplayInformation( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI display_info_Release( IDisplayInformation *iface )
{
    struct display_information *impl = impl_from_IDisplayInformation( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        if (impl->stop_event) SetEvent( impl->stop_event );
        if (impl->poll_thread)
        {
            if (GetCurrentThreadId() != impl->poll_tid)
                WaitForSingleObject( impl->poll_thread, 2000 );
            CloseHandle( impl->poll_thread );
        }
        if (impl->stop_event) CloseHandle( impl->stop_event );
        event_list_clear( &impl->orientation_changed );
        event_list_clear( &impl->dpi_changed );
        event_list_clear( &impl->stereo_changed );
        event_list_clear( &impl->color_profile_changed );
        event_list_clear( &impl->advanced_color_changed );
        DeleteCriticalSection( &impl->cs );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI display_info_GetIids( IDisplayInformation *iface, ULONG *count, IID **iids )
{
    static const IID iid_array[] =
    {
        IID_IDisplayInformation,
        IID_IDisplayInformation2,
        IID_IDisplayInformation3,
        IID_IDisplayInformation4,
        IID_IDisplayInformation5,
    };
    IID *array;

    if (!count || !iids) return E_POINTER;
    if (!(array = CoTaskMemAlloc( sizeof(iid_array) ))) return E_OUTOFMEMORY;
    memcpy( array, iid_array, sizeof(iid_array) );
    *count = ARRAY_SIZE(iid_array);
    *iids = array;
    return S_OK;
}

static HRESULT WINAPI display_info_GetRuntimeClassName( IDisplayInformation *iface, HSTRING *name )
{
    if (!name) return E_POINTER;
    return WindowsCreateString( RuntimeClass_Windows_Graphics_Display_DisplayInformation,
                                wcslen( RuntimeClass_Windows_Graphics_Display_DisplayInformation ), name );
}

static HRESULT WINAPI display_info_GetTrustLevel( IDisplayInformation *iface, TrustLevel *level )
{
    if (!level) return E_POINTER;
    *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI display_info_get_CurrentOrientation( IDisplayInformation *iface, DisplayOrientations *value )
{
    struct display_information *impl = impl_from_IDisplayInformation( iface );

    if (!value) return E_POINTER;
    get_display_metrics( impl, NULL, NULL, NULL, NULL, value );
    return S_OK;
}

static HRESULT WINAPI display_info_get_NativeOrientation( IDisplayInformation *iface, DisplayOrientations *value )
{
    struct display_information *impl = impl_from_IDisplayInformation( iface );

    if (!value) return E_POINTER;
    get_display_metrics( impl, NULL, NULL, NULL, value, NULL );
    return S_OK;
}

static HRESULT WINAPI display_info_add_OrientationChanged( IDisplayInformation *iface,
        ITypedEventHandler_DisplayInformation_IInspectable *h, EventRegistrationToken *t )
{
    struct display_information *impl = impl_from_IDisplayInformation( iface );
    HRESULT hr = event_list_add( &impl->cs, &impl->orientation_changed, h, t );
    if (SUCCEEDED( hr )) ensure_poll_thread( impl );
    return hr;
}

static HRESULT WINAPI display_info_remove_OrientationChanged( IDisplayInformation *iface, EventRegistrationToken t )
{
    struct display_information *impl = impl_from_IDisplayInformation( iface );
    return event_list_remove( &impl->cs, &impl->orientation_changed, t );
}

static HRESULT WINAPI display_info_get_ResolutionScale( IDisplayInformation *iface, ResolutionScale *value )
{
    struct display_information *impl = impl_from_IDisplayInformation( iface );
    UINT dpi;

    if (!value) return E_POINTER;
    get_display_metrics( impl, &dpi, NULL, NULL, NULL, NULL );
    *value = scale_from_dpi( dpi );
    return S_OK;
}

static HRESULT WINAPI display_info_get_LogicalDpi( IDisplayInformation *iface, FLOAT *value )
{
    struct display_information *impl = impl_from_IDisplayInformation( iface );
    UINT dpi;

    if (!value) return E_POINTER;
    get_display_metrics( impl, &dpi, NULL, NULL, NULL, NULL );
    *value = (FLOAT)dpi;
    return S_OK;
}

static HRESULT WINAPI display_info_get_RawDpiX( IDisplayInformation *iface, FLOAT *value )
{
    struct display_information *impl = impl_from_IDisplayInformation( iface );
    UINT dpi;

    if (!value) return E_POINTER;
    get_display_metrics( impl, &dpi, NULL, NULL, NULL, NULL );
    *value = (FLOAT)dpi;
    return S_OK;
}

static HRESULT WINAPI display_info_get_RawDpiY( IDisplayInformation *iface, FLOAT *value )
{
    struct display_information *impl = impl_from_IDisplayInformation( iface );
    UINT dpi;

    if (!value) return E_POINTER;
    get_display_metrics( impl, &dpi, NULL, NULL, NULL, NULL );
    *value = (FLOAT)dpi;
    return S_OK;
}

static HRESULT WINAPI display_info_add_DpiChanged( IDisplayInformation *iface,
        ITypedEventHandler_DisplayInformation_IInspectable *h, EventRegistrationToken *t )
{
    struct display_information *impl = impl_from_IDisplayInformation( iface );
    HRESULT hr = event_list_add( &impl->cs, &impl->dpi_changed, h, t );
    if (SUCCEEDED( hr )) ensure_poll_thread( impl );
    return hr;
}

static HRESULT WINAPI display_info_remove_DpiChanged( IDisplayInformation *iface, EventRegistrationToken t )
{
    struct display_information *impl = impl_from_IDisplayInformation( iface );
    return event_list_remove( &impl->cs, &impl->dpi_changed, t );
}

static HRESULT WINAPI display_info_get_StereoEnabled( IDisplayInformation *iface, boolean *value )
{
    if (!value) return E_POINTER;
    *value = FALSE;
    return S_OK;
}

static HRESULT WINAPI display_info_add_StereoEnabledChanged( IDisplayInformation *iface,
        ITypedEventHandler_DisplayInformation_IInspectable *h, EventRegistrationToken *t )
{
    struct display_information *impl = impl_from_IDisplayInformation( iface );
    return event_list_add( &impl->cs, &impl->stereo_changed, h, t );
}

static HRESULT WINAPI display_info_remove_StereoEnabledChanged( IDisplayInformation *iface, EventRegistrationToken t )
{
    struct display_information *impl = impl_from_IDisplayInformation( iface );
    return event_list_remove( &impl->cs, &impl->stereo_changed, t );
}

static HRESULT WINAPI display_info_GetColorProfileAsync( IDisplayInformation *iface,
        IAsyncOperation_IRandomAccessStream **async_info )
{
    if (async_info) *async_info = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI display_info_add_ColorProfileChanged( IDisplayInformation *iface,
        ITypedEventHandler_DisplayInformation_IInspectable *h, EventRegistrationToken *t )
{
    struct display_information *impl = impl_from_IDisplayInformation( iface );
    return event_list_add( &impl->cs, &impl->color_profile_changed, h, t );
}

static HRESULT WINAPI display_info_remove_ColorProfileChanged( IDisplayInformation *iface, EventRegistrationToken t )
{
    struct display_information *impl = impl_from_IDisplayInformation( iface );
    return event_list_remove( &impl->cs, &impl->color_profile_changed, t );
}

static const struct IDisplayInformationVtbl display_info_vtbl =
{
    display_info_QueryInterface,
    display_info_AddRef,
    display_info_Release,
    display_info_GetIids,
    display_info_GetRuntimeClassName,
    display_info_GetTrustLevel,
    display_info_get_CurrentOrientation,
    display_info_get_NativeOrientation,
    display_info_add_OrientationChanged,
    display_info_remove_OrientationChanged,
    display_info_get_ResolutionScale,
    display_info_get_LogicalDpi,
    display_info_get_RawDpiX,
    display_info_get_RawDpiY,
    display_info_add_DpiChanged,
    display_info_remove_DpiChanged,
    display_info_get_StereoEnabled,
    display_info_add_StereoEnabledChanged,
    display_info_remove_StereoEnabledChanged,
    display_info_GetColorProfileAsync,
    display_info_add_ColorProfileChanged,
    display_info_remove_ColorProfileChanged,
};

static HRESULT WINAPI display_info2_QueryInterface( IDisplayInformation2 *iface, REFIID iid, void **out )
{
    struct display_information *impl = impl_from_IDisplayInformation2( iface );
    return IDisplayInformation_QueryInterface( &impl->IDisplayInformation_iface, iid, out );
}
static ULONG WINAPI display_info2_AddRef( IDisplayInformation2 *iface )
{
    struct display_information *impl = impl_from_IDisplayInformation2( iface );
    return IDisplayInformation_AddRef( &impl->IDisplayInformation_iface );
}
static ULONG WINAPI display_info2_Release( IDisplayInformation2 *iface )
{
    struct display_information *impl = impl_from_IDisplayInformation2( iface );
    return IDisplayInformation_Release( &impl->IDisplayInformation_iface );
}
static HRESULT WINAPI display_info2_GetIids( IDisplayInformation2 *iface, ULONG *c, IID **i )
{
    struct display_information *impl = impl_from_IDisplayInformation2( iface );
    return display_info_GetIids( &impl->IDisplayInformation_iface, c, i );
}
static HRESULT WINAPI display_info2_GetRuntimeClassName( IDisplayInformation2 *iface, HSTRING *n )
{
    struct display_information *impl = impl_from_IDisplayInformation2( iface );
    return display_info_GetRuntimeClassName( &impl->IDisplayInformation_iface, n );
}
static HRESULT WINAPI display_info2_GetTrustLevel( IDisplayInformation2 *iface, TrustLevel *l )
{
    struct display_information *impl = impl_from_IDisplayInformation2( iface );
    return display_info_GetTrustLevel( &impl->IDisplayInformation_iface, l );
}
static HRESULT WINAPI display_info2_get_RawPixelsPerViewPixel( IDisplayInformation2 *iface, DOUBLE *value )
{
    struct display_information *impl = impl_from_IDisplayInformation2( iface );
    UINT dpi;

    if (!value) return E_POINTER;
    get_display_metrics( impl, &dpi, NULL, NULL, NULL, NULL );
    *value = (DOUBLE)dpi / 96.0;
    return S_OK;
}

static const struct IDisplayInformation2Vtbl display_info2_vtbl =
{
    display_info2_QueryInterface,
    display_info2_AddRef,
    display_info2_Release,
    display_info2_GetIids,
    display_info2_GetRuntimeClassName,
    display_info2_GetTrustLevel,
    display_info2_get_RawPixelsPerViewPixel,
};

static HRESULT WINAPI display_info3_QueryInterface( IDisplayInformation3 *iface, REFIID iid, void **out )
{
    struct display_information *impl = impl_from_IDisplayInformation3( iface );
    return IDisplayInformation_QueryInterface( &impl->IDisplayInformation_iface, iid, out );
}
static ULONG WINAPI display_info3_AddRef( IDisplayInformation3 *iface )
{
    struct display_information *impl = impl_from_IDisplayInformation3( iface );
    return IDisplayInformation_AddRef( &impl->IDisplayInformation_iface );
}
static ULONG WINAPI display_info3_Release( IDisplayInformation3 *iface )
{
    struct display_information *impl = impl_from_IDisplayInformation3( iface );
    return IDisplayInformation_Release( &impl->IDisplayInformation_iface );
}
static HRESULT WINAPI display_info3_GetIids( IDisplayInformation3 *iface, ULONG *c, IID **i )
{
    struct display_information *impl = impl_from_IDisplayInformation3( iface );
    return display_info_GetIids( &impl->IDisplayInformation_iface, c, i );
}
static HRESULT WINAPI display_info3_GetRuntimeClassName( IDisplayInformation3 *iface, HSTRING *n )
{
    struct display_information *impl = impl_from_IDisplayInformation3( iface );
    return display_info_GetRuntimeClassName( &impl->IDisplayInformation_iface, n );
}
static HRESULT WINAPI display_info3_GetTrustLevel( IDisplayInformation3 *iface, TrustLevel *l )
{
    struct display_information *impl = impl_from_IDisplayInformation3( iface );
    return display_info_GetTrustLevel( &impl->IDisplayInformation_iface, l );
}
static HRESULT WINAPI display_info3_get_DiagonalSizeInInches( IDisplayInformation3 *iface, IReference_DOUBLE **value )
{
    if (!value) return E_POINTER;
    *value = NULL;
    return S_OK;
}
static const struct IDisplayInformation3Vtbl display_info3_vtbl =
{
    display_info3_QueryInterface,
    display_info3_AddRef,
    display_info3_Release,
    display_info3_GetIids,
    display_info3_GetRuntimeClassName,
    display_info3_GetTrustLevel,
    display_info3_get_DiagonalSizeInInches,
};

static HRESULT WINAPI display_info4_QueryInterface( IDisplayInformation4 *iface, REFIID iid, void **out )
{
    struct display_information *impl = impl_from_IDisplayInformation4( iface );
    return IDisplayInformation_QueryInterface( &impl->IDisplayInformation_iface, iid, out );
}
static ULONG WINAPI display_info4_AddRef( IDisplayInformation4 *iface )
{
    struct display_information *impl = impl_from_IDisplayInformation4( iface );
    return IDisplayInformation_AddRef( &impl->IDisplayInformation_iface );
}
static ULONG WINAPI display_info4_Release( IDisplayInformation4 *iface )
{
    struct display_information *impl = impl_from_IDisplayInformation4( iface );
    return IDisplayInformation_Release( &impl->IDisplayInformation_iface );
}
static HRESULT WINAPI display_info4_GetIids( IDisplayInformation4 *iface, ULONG *c, IID **i )
{
    struct display_information *impl = impl_from_IDisplayInformation4( iface );
    return display_info_GetIids( &impl->IDisplayInformation_iface, c, i );
}
static HRESULT WINAPI display_info4_GetRuntimeClassName( IDisplayInformation4 *iface, HSTRING *n )
{
    struct display_information *impl = impl_from_IDisplayInformation4( iface );
    return display_info_GetRuntimeClassName( &impl->IDisplayInformation_iface, n );
}
static HRESULT WINAPI display_info4_GetTrustLevel( IDisplayInformation4 *iface, TrustLevel *l )
{
    struct display_information *impl = impl_from_IDisplayInformation4( iface );
    return display_info_GetTrustLevel( &impl->IDisplayInformation_iface, l );
}
static HRESULT WINAPI display_info4_get_ScreenWidthInRawPixels( IDisplayInformation4 *iface, UINT32 *value )
{
    struct display_information *impl = impl_from_IDisplayInformation4( iface );

    if (!value) return E_POINTER;
    get_display_metrics( impl, NULL, value, NULL, NULL, NULL );
    return S_OK;
}
static HRESULT WINAPI display_info4_get_ScreenHeightInRawPixels( IDisplayInformation4 *iface, UINT32 *value )
{
    struct display_information *impl = impl_from_IDisplayInformation4( iface );

    if (!value) return E_POINTER;
    get_display_metrics( impl, NULL, NULL, value, NULL, NULL );
    return S_OK;
}
static const struct IDisplayInformation4Vtbl display_info4_vtbl =
{
    display_info4_QueryInterface,
    display_info4_AddRef,
    display_info4_Release,
    display_info4_GetIids,
    display_info4_GetRuntimeClassName,
    display_info4_GetTrustLevel,
    display_info4_get_ScreenWidthInRawPixels,
    display_info4_get_ScreenHeightInRawPixels,
};

static HRESULT WINAPI display_info5_QueryInterface( IDisplayInformation5 *iface, REFIID iid, void **out )
{
    struct display_information *impl = impl_from_IDisplayInformation5( iface );
    return IDisplayInformation_QueryInterface( &impl->IDisplayInformation_iface, iid, out );
}
static ULONG WINAPI display_info5_AddRef( IDisplayInformation5 *iface )
{
    struct display_information *impl = impl_from_IDisplayInformation5( iface );
    return IDisplayInformation_AddRef( &impl->IDisplayInformation_iface );
}
static ULONG WINAPI display_info5_Release( IDisplayInformation5 *iface )
{
    struct display_information *impl = impl_from_IDisplayInformation5( iface );
    return IDisplayInformation_Release( &impl->IDisplayInformation_iface );
}
static HRESULT WINAPI display_info5_GetIids( IDisplayInformation5 *iface, ULONG *c, IID **i )
{
    struct display_information *impl = impl_from_IDisplayInformation5( iface );
    return display_info_GetIids( &impl->IDisplayInformation_iface, c, i );
}
static HRESULT WINAPI display_info5_GetRuntimeClassName( IDisplayInformation5 *iface, HSTRING *n )
{
    struct display_information *impl = impl_from_IDisplayInformation5( iface );
    return display_info_GetRuntimeClassName( &impl->IDisplayInformation_iface, n );
}
static HRESULT WINAPI display_info5_GetTrustLevel( IDisplayInformation5 *iface, TrustLevel *l )
{
    struct display_information *impl = impl_from_IDisplayInformation5( iface );
    return display_info_GetTrustLevel( &impl->IDisplayInformation_iface, l );
}
static HRESULT WINAPI display_info5_GetAdvancedColorInfo( IDisplayInformation5 *iface, IAdvancedColorInfo **value )
{
    struct advanced_color_info *info;

    if (!value) return E_POINTER;
    if (!(info = calloc( 1, sizeof(*info) )))
    {
        *value = NULL;
        return E_OUTOFMEMORY;
    }
    info->IAdvancedColorInfo_iface.lpVtbl = &advanced_color_vtbl;
    info->ref = 1;
    *value = &info->IAdvancedColorInfo_iface;
    return S_OK;
}
static HRESULT WINAPI display_info5_add_AdvancedColorInfoChanged( IDisplayInformation5 *iface,
        __FITypedEventHandler_2_Windows__CGraphics__CDisplay__CDisplayInformation_IInspectable *h, EventRegistrationToken *t )
{
    struct display_information *impl = impl_from_IDisplayInformation5( iface );
    return event_list_add( &impl->cs, &impl->advanced_color_changed, h, t );
}
static HRESULT WINAPI display_info5_remove_AdvancedColorInfoChanged( IDisplayInformation5 *iface, EventRegistrationToken t )
{
    struct display_information *impl = impl_from_IDisplayInformation5( iface );
    return event_list_remove( &impl->cs, &impl->advanced_color_changed, t );
}
static const struct IDisplayInformation5Vtbl display_info5_vtbl =
{
    display_info5_QueryInterface,
    display_info5_AddRef,
    display_info5_Release,
    display_info5_GetIids,
    display_info5_GetRuntimeClassName,
    display_info5_GetTrustLevel,
    display_info5_GetAdvancedColorInfo,
    display_info5_add_AdvancedColorInfoChanged,
    display_info5_remove_AdvancedColorInfoChanged,
};

DEFINE_IINSPECTABLE( display_info_statics, IDisplayInformationStatics, struct display_info_statics,
                     IActivationFactory_iface )

static HRESULT WINAPI display_info_statics_GetForCurrentView( IDisplayInformationStatics *iface,
        IDisplayInformation **current )
{
    struct display_information *impl;

    TRACE( "iface %p, current %p.\n", iface, current );
    if (!current) return E_POINTER;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;

    impl->IDisplayInformation_iface.lpVtbl = &display_info_vtbl;
    impl->IDisplayInformation2_iface.lpVtbl = &display_info2_vtbl;
    impl->IDisplayInformation3_iface.lpVtbl = &display_info3_vtbl;
    impl->IDisplayInformation4_iface.lpVtbl = &display_info4_vtbl;
    impl->IDisplayInformation5_iface.lpVtbl = &display_info5_vtbl;
    impl->ref = 1;
    impl->owner_tid = GetCurrentThreadId();
    impl->hwnd = find_thread_hwnd( impl->owner_tid );
    InitializeCriticalSection( &impl->cs );
    *current = &impl->IDisplayInformation_iface;
    return S_OK;
}

static HRESULT WINAPI display_info_statics_get_AutoRotationPreferences( IDisplayInformationStatics *iface,
        DisplayOrientations *value )
{
    if (!value) return E_POINTER;
    EnterCriticalSection( &statics_cs );
    *value = auto_rotation_preferences;
    LeaveCriticalSection( &statics_cs );
    return S_OK;
}

static HRESULT WINAPI display_info_statics_put_AutoRotationPreferences( IDisplayInformationStatics *iface,
        DisplayOrientations value )
{
    EnterCriticalSection( &statics_cs );
    auto_rotation_preferences = value;
    LeaveCriticalSection( &statics_cs );
    return S_OK;
}

static HRESULT WINAPI display_info_statics_add_DisplayContentsInvalidated( IDisplayInformationStatics *iface,
        ITypedEventHandler_DisplayInformation_IInspectable *handler, EventRegistrationToken *token )
{
    return event_list_add( &statics_cs, &contents_invalidated_events, handler, token );
}

static HRESULT WINAPI display_info_statics_remove_DisplayContentsInvalidated( IDisplayInformationStatics *iface,
        EventRegistrationToken token )
{
    return event_list_remove( &statics_cs, &contents_invalidated_events, token );
}

static const struct IDisplayInformationStaticsVtbl display_info_statics_vtbl =
{
    display_info_statics_QueryInterface,
    display_info_statics_AddRef,
    display_info_statics_Release,
    /* IInspectable methods */
    display_info_statics_GetIids,
    display_info_statics_GetRuntimeClassName,
    display_info_statics_GetTrustLevel,
    /* IDisplayInformationStatics methods */
    display_info_statics_GetForCurrentView,
    display_info_statics_get_AutoRotationPreferences,
    display_info_statics_put_AutoRotationPreferences,
    display_info_statics_add_DisplayContentsInvalidated,
    display_info_statics_remove_DisplayContentsInvalidated
};

static struct display_info_statics display_info_statics =
{
    {&factory_vtbl},
    {&display_info_statics_vtbl},
    1,
};

static IActivationFactory *display_info_factory = &display_info_statics.IActivationFactory_iface;

HRESULT WINAPI DllGetClassObject( REFCLSID clsid, REFIID riid, void **out )
{
    FIXME( "clsid %s, riid %s, out %p stub!\n", debugstr_guid( clsid ), debugstr_guid( riid ), out );
    return CLASS_E_CLASSNOTAVAILABLE;
}

HRESULT WINAPI DllGetActivationFactory( HSTRING classid, IActivationFactory **factory )
{
    const WCHAR *name = WindowsGetStringRawBuffer( classid, NULL );

    TRACE( "classid %s, factory %p.\n", debugstr_hstring( classid ), factory );

    *factory = NULL;

    if (!wcscmp( name, RuntimeClass_Windows_Graphics_Display_DisplayInformation ))
        IActivationFactory_QueryInterface( display_info_factory, &IID_IActivationFactory, (void **)factory );

    return *factory ? S_OK : E_NOINTERFACE;
}
