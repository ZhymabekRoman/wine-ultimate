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
#include "wincodec.h"
#include "roapi.h"

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
    IDisplayInformationInterop IDisplayInformationInterop_iface;
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
    else if (IsEqualGUID( iid, &IID_IDisplayInformationInterop ))
    {
        *out = &impl->IDisplayInformationInterop_iface;
        impl->IDisplayInformationInterop_iface.lpVtbl->AddRef( *out );
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
        IID_IDisplayInformationInterop,
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
    HMONITOR hmon;
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

static UINT get_monitor_dpi_fallback( HMONITOR hmon )
{
    typedef HRESULT (WINAPI *pGetDpiForMonitor)( HMONITOR, int, UINT *, UINT * );
    static pGetDpiForMonitor get_dpi_for_monitor;
    static LONG init_done;
    UINT dpi_x = 0, dpi_y = 0;

    if (!InterlockedCompareExchange( &init_done, 1, 0 ))
    {
        HMODULE shcore = LoadLibraryW( L"shcore.dll" );
        if (shcore)
            get_dpi_for_monitor = (pGetDpiForMonitor)GetProcAddress( shcore, "GetDpiForMonitor" );
    }

    if (hmon && get_dpi_for_monitor && SUCCEEDED( get_dpi_for_monitor( hmon, 0 /* MDT_EFFECTIVE_DPI */, &dpi_x, &dpi_y ) ) && dpi_x)
        return dpi_x;
    return 0;
}

static void get_display_metrics( struct display_information *impl, UINT *dpi_out,
                                 UINT32 *width_out, UINT32 *height_out,
                                 DisplayOrientations *native_out, DisplayOrientations *current_out )
{
    HWND hwnd = resolve_view_hwnd( impl );
    HMONITOR hmon = impl->hmon;
    MONITORINFOEXW mi;
    DEVMODEW dm;
    UINT dpi = 0;
    LONG w = 0, h = 0, nw, nh;
    DWORD dm_orient = DMDO_DEFAULT;
    BOOL have_dm_orient = FALSE;
    DisplayOrientations native_orient, cur_orient;

    if (!hmon && hwnd) hmon = MonitorFromWindow( hwnd, MONITOR_DEFAULTTOPRIMARY );
    if (!hmon)
    {
        POINT pt = {0, 0};
        hmon = MonitorFromPoint( pt, MONITOR_DEFAULTTOPRIMARY );
    }

    if (hwnd && !impl->hmon) dpi = GetDpiForWindow( hwnd );
    if (!dpi && hmon) dpi = get_monitor_dpi_fallback( hmon );
    if (!dpi && hwnd) dpi = GetDpiForWindow( hwnd );
    if (!dpi) dpi = GetDpiForSystem();
    if (!dpi)
    {
        typedef int (WINAPI *pGetDeviceCaps)( HDC, int );
        HMODULE gdi32 = GetModuleHandleW( L"gdi32.dll" );
        pGetDeviceCaps pget_caps = gdi32 ? (pGetDeviceCaps)GetProcAddress( gdi32, "GetDeviceCaps" ) : NULL;
        HDC hdc = pget_caps ? GetDC( NULL ) : NULL;
        if (hdc)
        {
            dpi = pget_caps( hdc, LOGPIXELSX );
            ReleaseDC( NULL, hdc );
        }
    }
    if (!dpi) dpi = 96;

    memset( &mi, 0, sizeof(mi) );
    mi.cbSize = sizeof(mi);
    if (hmon && GetMonitorInfoW( hmon, (MONITORINFO *)&mi ))
    {
        w = mi.rcMonitor.right - mi.rcMonitor.left;
        h = mi.rcMonitor.bottom - mi.rcMonitor.top;
    }

    if (mi.szDevice[0])
    {
        memset( &dm, 0, sizeof(dm) );
        dm.dmSize = sizeof(dm);
        if (EnumDisplaySettingsW( mi.szDevice, ENUM_CURRENT_SETTINGS, &dm ))
        {
            if ((dm.dmFields & (DM_PELSWIDTH | DM_PELSHEIGHT)) == (DM_PELSWIDTH | DM_PELSHEIGHT)
                && dm.dmPelsWidth > 0 && dm.dmPelsHeight > 0)
            {
                w = dm.dmPelsWidth;
                h = dm.dmPelsHeight;
            }
            if (dm.dmFields & DM_DISPLAYORIENTATION)
            {
                dm_orient = dm.dmDisplayOrientation;
                have_dm_orient = TRUE;
            }
        }
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
    static const ResolutionScale scales[] =
    {
        ResolutionScale_Scale100Percent,
        ResolutionScale_Scale120Percent,
        ResolutionScale_Scale125Percent,
        ResolutionScale_Scale140Percent,
        ResolutionScale_Scale150Percent,
        ResolutionScale_Scale160Percent,
        ResolutionScale_Scale175Percent,
        ResolutionScale_Scale180Percent,
        ResolutionScale_Scale200Percent,
        ResolutionScale_Scale225Percent,
        ResolutionScale_Scale250Percent,
        ResolutionScale_Scale300Percent,
        ResolutionScale_Scale350Percent,
        ResolutionScale_Scale400Percent,
        ResolutionScale_Scale450Percent,
        ResolutionScale_Scale500Percent,
    };
    int pct = MulDiv( dpi, 100, 96 );
    ResolutionScale best = scales[0];
    int best_diff = abs( pct - (int)scales[0] );
    unsigned int i;

    for (i = 1; i < ARRAY_SIZE(scales); i++)
    {
        int diff = abs( pct - (int)scales[i] );
        if (diff < best_diff)
        {
            best_diff = diff;
            best = scales[i];
        }
    }
    return best;
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

static HRESULT create_display_information( HWND hwnd, HMONITOR hmon, REFIID riid, void **out )
{
    struct display_information *impl;
    HRESULT hr;

    if (!out) return E_POINTER;
    *out = NULL;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;

    impl->IDisplayInformation_iface.lpVtbl = &display_info_vtbl;
    impl->IDisplayInformation2_iface.lpVtbl = &display_info2_vtbl;
    impl->IDisplayInformation3_iface.lpVtbl = &display_info3_vtbl;
    impl->IDisplayInformation4_iface.lpVtbl = &display_info4_vtbl;
    impl->IDisplayInformation5_iface.lpVtbl = &display_info5_vtbl;
    impl->ref = 1;
    impl->owner_tid = GetCurrentThreadId();
    impl->hwnd = hwnd ? hwnd : (hmon ? NULL : find_thread_hwnd( impl->owner_tid ));
    impl->hmon = hmon;
    InitializeCriticalSection( &impl->cs );

    hr = IDisplayInformation_QueryInterface( &impl->IDisplayInformation_iface, riid, out );
    IDisplayInformation_Release( &impl->IDisplayInformation_iface );
    return hr;
}

static HRESULT WINAPI display_info_statics_GetForCurrentView( IDisplayInformationStatics *iface,
        IDisplayInformation **current )
{
    TRACE( "iface %p, current %p.\n", iface, current );
    return create_display_information( NULL, NULL, &IID_IDisplayInformation, (void **)current );
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

DEFINE_IINSPECTABLE( display_info_interop, IDisplayInformationInterop, struct display_info_statics,
                     IActivationFactory_iface )

static HRESULT WINAPI display_info_interop_GetForWindow( IDisplayInformationInterop *iface,
        HWND window, REFIID riid, void **display_info )
{
    TRACE( "iface %p, window %p, riid %s, display_info %p.\n", iface, window, debugstr_guid( riid ), display_info );
    if (!display_info) return E_POINTER;
    if (!window || !IsWindow( window ))
    {
        *display_info = NULL;
        return E_INVALIDARG;
    }
    return create_display_information( window, NULL, riid, display_info );
}

static HRESULT WINAPI display_info_interop_GetForMonitor( IDisplayInformationInterop *iface,
        HMONITOR monitor, REFIID riid, void **display_info )
{
    TRACE( "iface %p, monitor %p, riid %s, display_info %p.\n", iface, monitor, debugstr_guid( riid ), display_info );
    if (!display_info) return E_POINTER;
    if (!monitor)
    {
        *display_info = NULL;
        return E_INVALIDARG;
    }
    return create_display_information( NULL, monitor, riid, display_info );
}

static const struct IDisplayInformationInteropVtbl display_info_interop_vtbl =
{
    display_info_interop_QueryInterface,
    display_info_interop_AddRef,
    display_info_interop_Release,
    display_info_interop_GetIids,
    display_info_interop_GetRuntimeClassName,
    display_info_interop_GetTrustLevel,
    display_info_interop_GetForWindow,
    display_info_interop_GetForMonitor,
};

static struct display_info_statics display_info_statics =
{
    {&factory_vtbl},
    {&display_info_statics_vtbl},
    {&display_info_interop_vtbl},
    1,
};

static IActivationFactory *display_info_factory = &display_info_statics.IActivationFactory_iface;

/* ============================================================================
 * Windows.Graphics.Imaging.BitmapTransform
 * ============================================================================ */

static const GUID IID_IBitmapTransform_local =
    { 0xae755344, 0xe268, 0x4d35, {0xad, 0xcf, 0xe9, 0x95, 0xd3, 0x1a, 0x8d, 0x34} };

typedef struct
{
    UINT32 X;
    UINT32 Y;
    UINT32 Width;
    UINT32 Height;
} BitmapBounds_local;

struct bitmap_transform
{
    const void *lpVtbl;
    LONG ref;
    UINT32 scaled_width;
    UINT32 scaled_height;
    UINT32 interpolation_mode;
    UINT32 flip;
    UINT32 rotation;
    BitmapBounds_local bounds;
};

static HRESULT WINAPI bt_QI( void *iface, REFIID iid, void **out )
{
    struct bitmap_transform *impl = (struct bitmap_transform *)iface;
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IBitmapTransform_local ))
    {
        *out = iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI bt_AddRef( void *iface ) { return InterlockedIncrement( &((struct bitmap_transform *)iface)->ref ); }
static ULONG WINAPI bt_Release( void *iface )
{
    struct bitmap_transform *impl = (struct bitmap_transform *)iface;
    ULONG r = InterlockedDecrement( &impl->ref );
    if (!r) free( impl );
    return r;
}
static HRESULT WINAPI bt_GetIids( void *iface, ULONG *c, IID **i )
{
    if (!c || !i) return E_POINTER;
    *i = CoTaskMemAlloc( sizeof(IID) );
    if (!*i) { *c = 0; return E_OUTOFMEMORY; }
    (*i)[0] = IID_IBitmapTransform_local;
    *c = 1;
    return S_OK;
}
static HRESULT WINAPI bt_GetRuntimeClassName( void *iface, HSTRING *n )
{
    static const WCHAR cls[] = L"Windows.Graphics.Imaging.BitmapTransform";
    return WindowsCreateString( cls, ARRAY_SIZE(cls) - 1, n );
}
static HRESULT WINAPI bt_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI bt_get_ScaledWidth( void *iface, UINT32 *v ) { if (!v) return E_POINTER; *v = ((struct bitmap_transform *)iface)->scaled_width; return S_OK; }
static HRESULT WINAPI bt_put_ScaledWidth( void *iface, UINT32 v ) { ((struct bitmap_transform *)iface)->scaled_width = v; return S_OK; }
static HRESULT WINAPI bt_get_ScaledHeight( void *iface, UINT32 *v ) { if (!v) return E_POINTER; *v = ((struct bitmap_transform *)iface)->scaled_height; return S_OK; }
static HRESULT WINAPI bt_put_ScaledHeight( void *iface, UINT32 v ) { ((struct bitmap_transform *)iface)->scaled_height = v; return S_OK; }
static HRESULT WINAPI bt_get_InterpolationMode( void *iface, UINT32 *v ) { if (!v) return E_POINTER; *v = ((struct bitmap_transform *)iface)->interpolation_mode; return S_OK; }
static HRESULT WINAPI bt_put_InterpolationMode( void *iface, UINT32 v ) { ((struct bitmap_transform *)iface)->interpolation_mode = v; return S_OK; }
static HRESULT WINAPI bt_get_Flip( void *iface, UINT32 *v ) { if (!v) return E_POINTER; *v = ((struct bitmap_transform *)iface)->flip; return S_OK; }
static HRESULT WINAPI bt_put_Flip( void *iface, UINT32 v ) { ((struct bitmap_transform *)iface)->flip = v; return S_OK; }
static HRESULT WINAPI bt_get_Rotation( void *iface, UINT32 *v ) { if (!v) return E_POINTER; *v = ((struct bitmap_transform *)iface)->rotation; return S_OK; }
static HRESULT WINAPI bt_put_Rotation( void *iface, UINT32 v ) { ((struct bitmap_transform *)iface)->rotation = v; return S_OK; }
static HRESULT WINAPI bt_get_Bounds( void *iface, BitmapBounds_local *v ) { if (!v) return E_POINTER; *v = ((struct bitmap_transform *)iface)->bounds; return S_OK; }
static HRESULT WINAPI bt_put_Bounds( void *iface, BitmapBounds_local v ) { ((struct bitmap_transform *)iface)->bounds = v; return S_OK; }

static const void * const bt_vtbl[] =
{
    bt_QI, bt_AddRef, bt_Release, bt_GetIids, bt_GetRuntimeClassName, bt_GetTrustLevel,
    bt_get_ScaledWidth, bt_put_ScaledWidth,
    bt_get_ScaledHeight, bt_put_ScaledHeight,
    bt_get_InterpolationMode, bt_put_InterpolationMode,
    bt_get_Flip, bt_put_Flip,
    bt_get_Rotation, bt_put_Rotation,
    bt_get_Bounds, bt_put_Bounds,
};

static HRESULT WINAPI bt_fac_QI( IActivationFactory *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI bt_fac_AddRef( IActivationFactory *iface ) { return 2; }
static ULONG WINAPI bt_fac_Release( IActivationFactory *iface ) { return 1; }
static HRESULT WINAPI bt_fac_GetIids( IActivationFactory *iface, ULONG *c, IID **i ) { if (c) *c = 0; if (i) *i = NULL; return S_OK; }
static HRESULT WINAPI bt_fac_GetRuntimeClassName( IActivationFactory *iface, HSTRING *n )
{
    static const WCHAR cls[] = L"Windows.Graphics.Imaging.BitmapTransform";
    return WindowsCreateString( cls, ARRAY_SIZE(cls) - 1, n );
}
static HRESULT WINAPI bt_fac_GetTrustLevel( IActivationFactory *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI bt_fac_ActivateInstance( IActivationFactory *iface, IInspectable **out )
{
    struct bitmap_transform *impl;
    if (!out) return E_POINTER;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->lpVtbl = bt_vtbl;
    impl->ref = 1;
    impl->interpolation_mode = 1; /* Fant/Linear default */
    *out = (IInspectable *)impl;
    return S_OK;
}
static const IActivationFactoryVtbl bt_fac_vtbl =
{
    bt_fac_QI, bt_fac_AddRef, bt_fac_Release,
    bt_fac_GetIids, bt_fac_GetRuntimeClassName, bt_fac_GetTrustLevel,
    bt_fac_ActivateInstance,
};
static IActivationFactory bitmap_transform_factory = { &bt_fac_vtbl };

/* ============================================================================
 * Windows.Graphics.Imaging.BitmapDecoder / PixelDataProvider / SoftwareBitmap
 * ============================================================================ */

static const GUID IID_IBitmapDecoderStatics_local =
    { 0x438ccb26, 0xbcef, 0x4e95, {0xba, 0xd6, 0x23, 0xa8, 0x22, 0xe5, 0x8d, 0x01} };
static const GUID IID_IBitmapDecoderStatics2_local =
    { 0x50ba68ea, 0x99a1, 0x40c4, {0x80, 0xd9, 0xae, 0xf0, 0xda, 0xfa, 0x6c, 0x3f} };
static const GUID IID_IBitmapDecoder_local =
    { 0xacef22ba, 0x1d74, 0x4c91, {0x9d, 0xfc, 0x96, 0x20, 0x74, 0x52, 0x33, 0xe6} };
static const GUID IID_IBitmapFrame_local =
    { 0x72a49a1c, 0x8081, 0x438d, {0x91, 0xbc, 0x94, 0xec, 0xfc, 0x81, 0x85, 0xc6} };
static const GUID IID_IBitmapFrameWithSoftwareBitmap_local =
    { 0xfe287c9a, 0x420c, 0x4963, {0x87, 0xad, 0x69, 0x14, 0x36, 0xe0, 0x83, 0x83} };
static const GUID IID_IPixelDataProvider_local =
    { 0xdd831f25, 0x185c, 0x4595, {0x9f, 0xb9, 0xcc, 0xbe, 0x6e, 0xc1, 0x8a, 0x6f} };
static const GUID IID_ISoftwareBitmap_local =
    { 0x689e0708, 0x7eef, 0x483f, {0x96, 0x3f, 0xda, 0x93, 0x88, 0x18, 0xe0, 0x73} };
static const GUID IID_IBitmapBuffer_local =
    { 0xa53e04c4, 0x399c, 0x438c, {0xb2, 0x8f, 0xa6, 0x3a, 0x6b, 0x83, 0xd1, 0xa1} };
static const GUID IID_IMemoryBuffer_local =
    { 0xfbc4dd2a, 0x245b, 0x11e4, {0xaf, 0x98, 0x68, 0x94, 0x23, 0x26, 0x0c, 0xf8} };
static const GUID IID_IMemoryBufferReference_local =
    { 0xfbc4dd29, 0x245b, 0x11e4, {0xaf, 0x98, 0x68, 0x94, 0x23, 0x26, 0x0c, 0xf8} };
static const GUID IID_IMemoryBufferByteAccess_local =
    { 0x5b0d3235, 0x4dba, 0x4d44, {0x86, 0x5e, 0x8f, 0x1d, 0x0e, 0x4f, 0xd0, 0x4d} };
static const GUID IID_IClosable_local =
    { 0x30d5a829, 0x7fa4, 0x4026, {0x83, 0xbb, 0xd7, 0x5b, 0xae, 0x4e, 0xa9, 0x9e} };
static const GUID IID_IAsyncInfo_local =
    { 0x00000036, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46} };
static const GUID IID_IAsyncOperation_BitmapDecoder =
    { 0xaa94d8e9, 0xcaef, 0x53f6, {0x82, 0x3d, 0x91, 0xb6, 0xe8, 0x34, 0x05, 0x10} };
static const GUID IID_IAsyncOperation_PixelDataProvider =
    { 0x8c2dfeb0, 0x6c22, 0x5863, {0x88, 0xd8, 0x85, 0xc1, 0xfb, 0xc7, 0x56, 0x97} };
static const GUID IID_IAsyncOperation_SoftwareBitmap =
    { 0xc4a10980, 0x714b, 0x5501, {0x8d, 0xa2, 0xdb, 0xda, 0xcc, 0xe7, 0x0f, 0x73} };
static const GUID IID_IRandomAccessStream_local =
    { 0x905a0fe1, 0xbc53, 0x11df, {0x8c, 0x49, 0x00, 0x1e, 0x4f, 0xc6, 0x86, 0xda} };
static const GUID IID_IBufferFactory_local =
    { 0x71af914d, 0xc10f, 0x484b, {0xbc, 0x50, 0x14, 0xbc, 0x62, 0x3b, 0x3a, 0x27} };
static const GUID IID_IBufferByteAccess_local =
    { 0x905a0fef, 0xbc53, 0x11df, {0x8c, 0x49, 0x00, 0x1e, 0x4f, 0xc6, 0x86, 0xda} };

/* Generic completed IAsyncOperation<T*> for Imaging */
struct img_async_op
{
    const void *lpVtbl_op;
    const void *lpVtbl_info;
    LONG ref;
    GUID op_iid;
    IUnknown *result;
    HRESULT error_code;
    void *completed_handler;
};

static HRESULT WINAPI iao_QI( void *iface, REFIID iid, void **out )
{
    struct img_async_op *impl = CONTAINING_RECORD( iface, struct img_async_op, lpVtbl_op );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &impl->op_iid ))
    {
        *out = &impl->lpVtbl_op;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IAsyncInfo_local ))
    {
        *out = &impl->lpVtbl_info;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI iao_AddRef( void *iface )
{
    struct img_async_op *impl = CONTAINING_RECORD( iface, struct img_async_op, lpVtbl_op );
    return InterlockedIncrement( &impl->ref );
}
static ULONG WINAPI iao_Release( void *iface )
{
    struct img_async_op *impl = CONTAINING_RECORD( iface, struct img_async_op, lpVtbl_op );
    ULONG r = InterlockedDecrement( &impl->ref );
    if (!r)
    {
        if (impl->result) IUnknown_Release( impl->result );
        if (impl->completed_handler) IUnknown_Release( (IUnknown *)impl->completed_handler );
        free( impl );
    }
    return r;
}
static HRESULT WINAPI iao_GetIids( void *iface, ULONG *c, IID **i ) { if (c) *c = 0; if (i) *i = NULL; return S_OK; }
static HRESULT WINAPI iao_GetRuntimeClassName( void *iface, HSTRING *n )
{
    static const WCHAR cls[] = L"Windows.Foundation.IAsyncOperation`1";
    return WindowsCreateString( cls, ARRAY_SIZE(cls) - 1, n );
}
static HRESULT WINAPI iao_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI iao_put_Completed( void *iface, void *handler )
{
    struct img_async_op *impl = CONTAINING_RECORD( iface, struct img_async_op, lpVtbl_op );
    if (impl->completed_handler) IUnknown_Release( (IUnknown *)impl->completed_handler );
    impl->completed_handler = handler;
    if (handler)
    {
        HRESULT (WINAPI *invoke)(void *, void *, UINT32);
        UINT32 status = SUCCEEDED(impl->error_code) ? 1 : 3;
        IUnknown_AddRef( (IUnknown *)handler );
        invoke = (void *)(*(void ***)handler)[3];
        if (invoke) invoke( handler, &impl->lpVtbl_op, status );
    }
    return S_OK;
}
static HRESULT WINAPI iao_get_Completed( void *iface, void **handler )
{
    struct img_async_op *impl = CONTAINING_RECORD( iface, struct img_async_op, lpVtbl_op );
    if (!handler) return E_POINTER;
    *handler = impl->completed_handler;
    if (*handler) IUnknown_AddRef( (IUnknown *)*handler );
    return S_OK;
}
static HRESULT WINAPI iao_GetResults( void *iface, void **out )
{
    struct img_async_op *impl = CONTAINING_RECORD( iface, struct img_async_op, lpVtbl_op );
    if (!out) return E_POINTER;
    if (FAILED(impl->error_code)) { *out = NULL; return impl->error_code; }
    *out = impl->result;
    if (*out) IUnknown_AddRef( (IUnknown *)*out );
    return S_OK;
}
static HRESULT WINAPI iao_info_QI( void *iface, REFIID iid, void **out )
{
    return iao_QI( &CONTAINING_RECORD( iface, struct img_async_op, lpVtbl_info )->lpVtbl_op, iid, out );
}
static ULONG WINAPI iao_info_AddRef( void *iface )
{
    return iao_AddRef( &CONTAINING_RECORD( iface, struct img_async_op, lpVtbl_info )->lpVtbl_op );
}
static ULONG WINAPI iao_info_Release( void *iface )
{
    return iao_Release( &CONTAINING_RECORD( iface, struct img_async_op, lpVtbl_info )->lpVtbl_op );
}
static HRESULT WINAPI iao_info_get_Id( void *iface, UINT32 *id ) { if (id) *id = 1; return S_OK; }
static HRESULT WINAPI iao_info_get_Status( void *iface, UINT32 *st )
{
    struct img_async_op *impl = CONTAINING_RECORD( iface, struct img_async_op, lpVtbl_info );
    if (st) *st = SUCCEEDED(impl->error_code) ? 1 : 3;
    return S_OK;
}
static HRESULT WINAPI iao_info_get_ErrorCode( void *iface, HRESULT *hr )
{
    struct img_async_op *impl = CONTAINING_RECORD( iface, struct img_async_op, lpVtbl_info );
    if (hr) *hr = impl->error_code;
    return S_OK;
}
static HRESULT WINAPI iao_info_Cancel( void *iface ) { return S_OK; }
static HRESULT WINAPI iao_info_Close( void *iface ) { return S_OK; }

static const void * const iao_op_vtbl[] =
{
    iao_QI, iao_AddRef, iao_Release, iao_GetIids, iao_GetRuntimeClassName, iao_GetTrustLevel,
    iao_put_Completed, iao_get_Completed, iao_GetResults,
};
static const void * const iao_info_vtbl[] =
{
    iao_info_QI, iao_info_AddRef, iao_info_Release, iao_GetIids, iao_GetRuntimeClassName, iao_GetTrustLevel,
    iao_info_get_Id, iao_info_get_Status, iao_info_get_ErrorCode, iao_info_Cancel, iao_info_Close,
};

static HRESULT img_async_op_create( REFIID op_iid, IUnknown *result, HRESULT error_code, void **out )
{
    struct img_async_op *impl;
    if (!out) return E_POINTER;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->lpVtbl_op = iao_op_vtbl;
    impl->lpVtbl_info = iao_info_vtbl;
    impl->ref = 1;
    impl->op_iid = *op_iid;
    impl->result = result;
    if (result) IUnknown_AddRef( result );
    impl->error_code = error_code;
    *out = &impl->lpVtbl_op;
    return S_OK;
}

/* --- PixelDataProvider --- */
struct pixel_data_provider
{
    const void *lpVtbl;
    LONG ref;
    BYTE *pixels;
    UINT32 size;
};
static HRESULT WINAPI pdp_QI( void *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IPixelDataProvider_local ))
    {
        *out = iface;
        InterlockedIncrement( &((struct pixel_data_provider *)iface)->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI pdp_AddRef( void *iface ) { return InterlockedIncrement( &((struct pixel_data_provider *)iface)->ref ); }
static ULONG WINAPI pdp_Release( void *iface )
{
    struct pixel_data_provider *impl = (struct pixel_data_provider *)iface;
    ULONG r = InterlockedDecrement( &impl->ref );
    if (!r)
    {
        if (impl->pixels) CoTaskMemFree( impl->pixels );
        free( impl );
    }
    return r;
}
static HRESULT WINAPI pdp_GetIids( void *iface, ULONG *c, IID **i ) { if (c) *c = 0; if (i) *i = NULL; return S_OK; }
static HRESULT WINAPI pdp_GetRuntimeClassName( void *iface, HSTRING *n )
{
    static const WCHAR cls[] = L"Windows.Graphics.Imaging.PixelDataProvider";
    return WindowsCreateString( cls, ARRAY_SIZE(cls) - 1, n );
}
static HRESULT WINAPI pdp_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI pdp_DetachPixelData( void *iface, UINT32 *size, BYTE **data )
{
    struct pixel_data_provider *impl = (struct pixel_data_provider *)iface;
    if (!size || !data) return E_POINTER;
    *size = impl->size;
    *data = impl->pixels;
    impl->pixels = NULL;
    impl->size = 0;
    return S_OK;
}
static const void * const pdp_vtbl[] =
{
    pdp_QI, pdp_AddRef, pdp_Release, pdp_GetIids, pdp_GetRuntimeClassName, pdp_GetTrustLevel,
    pdp_DetachPixelData,
};

/* --- BitmapBuffer / SoftwareBitmap --- */
typedef struct { INT32 StartIndex; INT32 Width; INT32 Height; INT32 Stride; } BitmapPlaneDescription_local;

struct software_bitmap;
struct bitmap_buffer
{
    const void *lpVtbl_buf;
    const void *lpVtbl_mb;
    const void *lpVtbl_mbr;
    const void *lpVtbl_mbba;
    const void *lpVtbl_close;
    LONG ref;
    struct software_bitmap *owner;
};
struct software_bitmap
{
    const void *lpVtbl_sb;
    const void *lpVtbl_close;
    LONG ref;
    UINT32 pixel_format;
    UINT32 alpha_mode;
    INT32 width;
    INT32 height;
    double dpi_x;
    double dpi_y;
    BYTE *pixels;
    UINT32 size;
    INT32 stride;
};

static ULONG WINAPI sb_AddRef( void *iface );
static ULONG WINAPI sb_Release( void *iface );

static HRESULT WINAPI bb_QI( void *iface, REFIID iid, void **out )
{
    struct bitmap_buffer *impl = CONTAINING_RECORD( iface, struct bitmap_buffer, lpVtbl_buf );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IBitmapBuffer_local ))
        *out = &impl->lpVtbl_buf;
    else if (IsEqualGUID( iid, &IID_IMemoryBuffer_local ))
        *out = &impl->lpVtbl_mb;
    else if (IsEqualGUID( iid, &IID_IMemoryBufferReference_local ))
        *out = &impl->lpVtbl_mbr;
    else if (IsEqualGUID( iid, &IID_IMemoryBufferByteAccess_local ))
        *out = &impl->lpVtbl_mbba;
    else if (IsEqualGUID( iid, &IID_IClosable_local ))
        *out = &impl->lpVtbl_close;
    else
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    InterlockedIncrement( &impl->ref );
    return S_OK;
}
static ULONG WINAPI bb_AddRef( void *iface )
{
    return InterlockedIncrement( &CONTAINING_RECORD( iface, struct bitmap_buffer, lpVtbl_buf )->ref );
}
static ULONG WINAPI bb_Release( void *iface )
{
    struct bitmap_buffer *impl = CONTAINING_RECORD( iface, struct bitmap_buffer, lpVtbl_buf );
    ULONG r = InterlockedDecrement( &impl->ref );
    if (!r)
    {
        if (impl->owner) sb_Release( &impl->owner->lpVtbl_sb );
        free( impl );
    }
    return r;
}
static HRESULT WINAPI bb_GetIids( void *iface, ULONG *c, IID **i ) { if (c) *c = 0; if (i) *i = NULL; return S_OK; }
static HRESULT WINAPI bb_GetRuntimeClassName( void *iface, HSTRING *n )
{
    static const WCHAR cls[] = L"Windows.Graphics.Imaging.BitmapBuffer";
    return WindowsCreateString( cls, ARRAY_SIZE(cls) - 1, n );
}
static HRESULT WINAPI bb_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI bb_GetPlaneCount( void *iface, INT32 *cnt ) { if (cnt) *cnt = 1; return S_OK; }
static HRESULT WINAPI bb_GetPlaneDescription( void *iface, INT32 idx, BitmapPlaneDescription_local *desc )
{
    struct bitmap_buffer *impl = CONTAINING_RECORD( iface, struct bitmap_buffer, lpVtbl_buf );
    if (!desc) return E_POINTER;
    desc->StartIndex = 0;
    desc->Width = impl->owner ? impl->owner->width : 0;
    desc->Height = impl->owner ? impl->owner->height : 0;
    desc->Stride = impl->owner ? impl->owner->stride : 0;
    return S_OK;
}

static HRESULT WINAPI bb_mb_QI( void *iface, REFIID iid, void **out ) { return bb_QI( &CONTAINING_RECORD( iface, struct bitmap_buffer, lpVtbl_mb )->lpVtbl_buf, iid, out ); }
static ULONG WINAPI bb_mb_AddRef( void *iface ) { return bb_AddRef( &CONTAINING_RECORD( iface, struct bitmap_buffer, lpVtbl_mb )->lpVtbl_buf ); }
static ULONG WINAPI bb_mb_Release( void *iface ) { return bb_Release( &CONTAINING_RECORD( iface, struct bitmap_buffer, lpVtbl_mb )->lpVtbl_buf ); }
static HRESULT WINAPI bb_mb_CreateReference( void *iface, void **out )
{
    struct bitmap_buffer *impl = CONTAINING_RECORD( iface, struct bitmap_buffer, lpVtbl_mb );
    if (!out) return E_POINTER;
    *out = &impl->lpVtbl_mbr;
    InterlockedIncrement( &impl->ref );
    return S_OK;
}

static HRESULT WINAPI bb_mbr_QI( void *iface, REFIID iid, void **out ) { return bb_QI( &CONTAINING_RECORD( iface, struct bitmap_buffer, lpVtbl_mbr )->lpVtbl_buf, iid, out ); }
static ULONG WINAPI bb_mbr_AddRef( void *iface ) { return bb_AddRef( &CONTAINING_RECORD( iface, struct bitmap_buffer, lpVtbl_mbr )->lpVtbl_buf ); }
static ULONG WINAPI bb_mbr_Release( void *iface ) { return bb_Release( &CONTAINING_RECORD( iface, struct bitmap_buffer, lpVtbl_mbr )->lpVtbl_buf ); }
static HRESULT WINAPI bb_mbr_get_Capacity( void *iface, UINT32 *cap )
{
    struct bitmap_buffer *impl = CONTAINING_RECORD( iface, struct bitmap_buffer, lpVtbl_mbr );
    if (cap) *cap = impl->owner ? impl->owner->size : 0;
    return S_OK;
}
static HRESULT WINAPI bb_mbr_add_Closed( void *iface, void *h, EventRegistrationToken *t ) { if (t) t->value = 1; return S_OK; }
static HRESULT WINAPI bb_mbr_remove_Closed( void *iface, EventRegistrationToken t ) { return S_OK; }

static HRESULT WINAPI bb_mbba_QI( void *iface, REFIID iid, void **out ) { return bb_QI( &CONTAINING_RECORD( iface, struct bitmap_buffer, lpVtbl_mbba )->lpVtbl_buf, iid, out ); }
static ULONG WINAPI bb_mbba_AddRef( void *iface ) { return bb_AddRef( &CONTAINING_RECORD( iface, struct bitmap_buffer, lpVtbl_mbba )->lpVtbl_buf ); }
static ULONG WINAPI bb_mbba_Release( void *iface ) { return bb_Release( &CONTAINING_RECORD( iface, struct bitmap_buffer, lpVtbl_mbba )->lpVtbl_buf ); }
static HRESULT WINAPI bb_mbba_GetBuffer( void *iface, BYTE **val, UINT32 *cap )
{
    struct bitmap_buffer *impl = CONTAINING_RECORD( iface, struct bitmap_buffer, lpVtbl_mbba );
    if (val) *val = impl->owner ? impl->owner->pixels : NULL;
    if (cap) *cap = impl->owner ? impl->owner->size : 0;
    return S_OK;
}

static HRESULT WINAPI bb_cl_QI( void *iface, REFIID iid, void **out ) { return bb_QI( &CONTAINING_RECORD( iface, struct bitmap_buffer, lpVtbl_close )->lpVtbl_buf, iid, out ); }
static ULONG WINAPI bb_cl_AddRef( void *iface ) { return bb_AddRef( &CONTAINING_RECORD( iface, struct bitmap_buffer, lpVtbl_close )->lpVtbl_buf ); }
static ULONG WINAPI bb_cl_Release( void *iface ) { return bb_Release( &CONTAINING_RECORD( iface, struct bitmap_buffer, lpVtbl_close )->lpVtbl_buf ); }
static HRESULT WINAPI bb_cl_Close( void *iface ) { return S_OK; }

static const void * const bb_buf_vtbl[] = { bb_QI, bb_AddRef, bb_Release, bb_GetIids, bb_GetRuntimeClassName, bb_GetTrustLevel, bb_GetPlaneCount, bb_GetPlaneDescription };
static const void * const bb_mb_vtbl[] = { bb_mb_QI, bb_mb_AddRef, bb_mb_Release, bb_GetIids, bb_GetRuntimeClassName, bb_GetTrustLevel, bb_mb_CreateReference };
static const void * const bb_mbr_vtbl[] = { bb_mbr_QI, bb_mbr_AddRef, bb_mbr_Release, bb_GetIids, bb_GetRuntimeClassName, bb_GetTrustLevel, bb_mbr_get_Capacity, bb_mbr_add_Closed, bb_mbr_remove_Closed };
static const void * const bb_mbba_vtbl[] = { bb_mbba_QI, bb_mbba_AddRef, bb_mbba_Release, bb_mbba_GetBuffer };
static const void * const bb_cl_vtbl[] = { bb_cl_QI, bb_cl_AddRef, bb_cl_Release, bb_GetIids, bb_GetRuntimeClassName, bb_GetTrustLevel, bb_cl_Close };

static HRESULT WINAPI sb_QI( void *iface, REFIID iid, void **out )
{
    struct software_bitmap *impl = CONTAINING_RECORD( iface, struct software_bitmap, lpVtbl_sb );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_ISoftwareBitmap_local ))
        *out = &impl->lpVtbl_sb;
    else if (IsEqualGUID( iid, &IID_IClosable_local ))
        *out = &impl->lpVtbl_close;
    else
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    InterlockedIncrement( &impl->ref );
    return S_OK;
}
static ULONG WINAPI sb_AddRef( void *iface )
{
    return InterlockedIncrement( &CONTAINING_RECORD( iface, struct software_bitmap, lpVtbl_sb )->ref );
}
static ULONG WINAPI sb_Release( void *iface )
{
    struct software_bitmap *impl = CONTAINING_RECORD( iface, struct software_bitmap, lpVtbl_sb );
    ULONG r = InterlockedDecrement( &impl->ref );
    if (!r)
    {
        free( impl->pixels );
        free( impl );
    }
    return r;
}
static HRESULT WINAPI sb_GetIids( void *iface, ULONG *c, IID **i ) { if (c) *c = 0; if (i) *i = NULL; return S_OK; }
static HRESULT WINAPI sb_GetRuntimeClassName( void *iface, HSTRING *n )
{
    static const WCHAR cls[] = L"Windows.Graphics.Imaging.SoftwareBitmap";
    return WindowsCreateString( cls, ARRAY_SIZE(cls) - 1, n );
}
static HRESULT WINAPI sb_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI sb_get_PixelFormat( void *iface, UINT32 *v ) { if (v) *v = CONTAINING_RECORD( iface, struct software_bitmap, lpVtbl_sb )->pixel_format; return S_OK; }
static HRESULT WINAPI sb_get_AlphaMode( void *iface, UINT32 *v ) { if (v) *v = CONTAINING_RECORD( iface, struct software_bitmap, lpVtbl_sb )->alpha_mode; return S_OK; }
static HRESULT WINAPI sb_get_PixelWidth( void *iface, INT32 *v ) { if (v) *v = CONTAINING_RECORD( iface, struct software_bitmap, lpVtbl_sb )->width; return S_OK; }
static HRESULT WINAPI sb_get_PixelHeight( void *iface, INT32 *v ) { if (v) *v = CONTAINING_RECORD( iface, struct software_bitmap, lpVtbl_sb )->height; return S_OK; }
static HRESULT WINAPI sb_get_IsReadOnly( void *iface, boolean *v ) { if (v) *v = FALSE; return S_OK; }
static HRESULT WINAPI sb_put_DpiX( void *iface, double v ) { CONTAINING_RECORD( iface, struct software_bitmap, lpVtbl_sb )->dpi_x = v; return S_OK; }
static HRESULT WINAPI sb_get_DpiX( void *iface, double *v ) { if (v) *v = CONTAINING_RECORD( iface, struct software_bitmap, lpVtbl_sb )->dpi_x; return S_OK; }
static HRESULT WINAPI sb_put_DpiY( void *iface, double v ) { CONTAINING_RECORD( iface, struct software_bitmap, lpVtbl_sb )->dpi_y = v; return S_OK; }
static HRESULT WINAPI sb_get_DpiY( void *iface, double *v ) { if (v) *v = CONTAINING_RECORD( iface, struct software_bitmap, lpVtbl_sb )->dpi_y; return S_OK; }
static HRESULT WINAPI sb_LockBuffer( void *iface, UINT32 mode, void **out )
{
    struct software_bitmap *impl = CONTAINING_RECORD( iface, struct software_bitmap, lpVtbl_sb );
    struct bitmap_buffer *bb;
    if (!out) return E_POINTER;
    if (!(bb = calloc( 1, sizeof(*bb) ))) return E_OUTOFMEMORY;
    bb->lpVtbl_buf = bb_buf_vtbl;
    bb->lpVtbl_mb = bb_mb_vtbl;
    bb->lpVtbl_mbr = bb_mbr_vtbl;
    bb->lpVtbl_mbba = bb_mbba_vtbl;
    bb->lpVtbl_close = bb_cl_vtbl;
    bb->ref = 1;
    bb->owner = impl;
    sb_AddRef( &impl->lpVtbl_sb );
    *out = &bb->lpVtbl_buf;
    return S_OK;
}
static HRESULT WINAPI sb_CopyTo( void *iface, void *dst ) { return S_OK; }
static HRESULT WINAPI sb_CopyFromBuffer( void *iface, void *buf ) { return S_OK; }
static HRESULT WINAPI sb_CopyToBuffer( void *iface, void *buf ) { return S_OK; }
static HRESULT WINAPI sb_GetReadOnlyView( void *iface, void **out )
{
    if (!out) return E_POINTER;
    *out = iface;
    sb_AddRef( iface );
    return S_OK;
}
static HRESULT WINAPI sb_cl_QI( void *iface, REFIID iid, void **out ) { return sb_QI( &CONTAINING_RECORD( iface, struct software_bitmap, lpVtbl_close )->lpVtbl_sb, iid, out ); }
static ULONG WINAPI sb_cl_AddRef( void *iface ) { return sb_AddRef( &CONTAINING_RECORD( iface, struct software_bitmap, lpVtbl_close )->lpVtbl_sb ); }
static ULONG WINAPI sb_cl_Release( void *iface ) { return sb_Release( &CONTAINING_RECORD( iface, struct software_bitmap, lpVtbl_close )->lpVtbl_sb ); }
static HRESULT WINAPI sb_cl_Close( void *iface ) { return S_OK; }

static const void * const sb_vtbl[] =
{
    sb_QI, sb_AddRef, sb_Release, sb_GetIids, sb_GetRuntimeClassName, sb_GetTrustLevel,
    sb_get_PixelFormat, sb_get_AlphaMode, sb_get_PixelWidth, sb_get_PixelHeight,
    sb_get_IsReadOnly, sb_put_DpiX, sb_get_DpiX, sb_put_DpiY, sb_get_DpiY,
    sb_LockBuffer, sb_CopyTo, sb_CopyFromBuffer, sb_CopyToBuffer, sb_GetReadOnlyView,
};
static const void * const sb_cl_vtbl[] = { sb_cl_QI, sb_cl_AddRef, sb_cl_Release, sb_GetIids, sb_GetRuntimeClassName, sb_GetTrustLevel, sb_cl_Close };

/* --- BitmapDecoder (implements IBitmapDecoder + IBitmapFrame + IBitmapFrameWithSoftwareBitmap) --- */
struct bitmap_decoder
{
    const void *lpVtbl_dec;
    const void *lpVtbl_frame;
    const void *lpVtbl_fwsb;
    LONG ref;
    IWICImagingFactory *wic;
    IWICBitmapFrameDecode *wic_frame;
    UINT32 width;
    UINT32 height;
    double dpi_x;
    double dpi_y;
    UINT32 pixel_format;
    UINT32 alpha_mode;
};

static HRESULT bd_decode_pixels( struct bitmap_decoder *impl, UINT32 pixel_format, UINT32 alpha_mode,
                                 void *transform, BYTE **out_pixels, UINT32 *out_size,
                                 UINT32 *out_w, UINT32 *out_h, UINT32 *out_stride, BOOL use_cotask )
{
    UINT32 w = impl->width ? impl->width : 1;
    UINT32 h = impl->height ? impl->height : 1;
    UINT32 bpp = (pixel_format == 62) ? 1 : (pixel_format == 12) ? 8 : 4;
    UINT32 stride, total;
    BYTE *buf;
    WICPixelFormatGUID dst_fmt = GUID_WICPixelFormat32bppRGBA;
    IWICBitmapSource *src = NULL;
    IWICBitmapScaler *scaler = NULL;
    IWICFormatConverter *conv = NULL;

    if (transform)
    {
        UINT32 sw = 0, sh = 0;
        HRESULT (WINAPI *get_sw)(void *, UINT32 *) = (void *)(*(void ***)transform)[6];
        HRESULT (WINAPI *get_sh)(void *, UINT32 *) = (void *)(*(void ***)transform)[8];
        if (get_sw) get_sw( transform, &sw );
        if (get_sh) get_sh( transform, &sh );
        if (sw) w = sw;
        if (sh) h = sh;
    }

    if (pixel_format == 87)
        dst_fmt = (alpha_mode == 0) ? GUID_WICPixelFormat32bppPBGRA : GUID_WICPixelFormat32bppBGRA;
    else if (pixel_format == 30)
        dst_fmt = (alpha_mode == 0) ? GUID_WICPixelFormat32bppPRGBA : GUID_WICPixelFormat32bppRGBA;
    else if (pixel_format == 62)
        dst_fmt = GUID_WICPixelFormat8bppGray;
    else if (pixel_format == 12)
        dst_fmt = GUID_WICPixelFormat64bppRGBA;

    stride = w * bpp;
    total = stride * h;
    buf = use_cotask ? CoTaskMemAlloc( total ) : malloc( total );
    if (!buf) return E_OUTOFMEMORY;
    memset( buf, 0, total );

    if (impl->wic && impl->wic_frame)
    {
        src = (IWICBitmapSource *)impl->wic_frame;
        IWICBitmapSource_AddRef( src );

        if (w != impl->width || h != impl->height)
        {
            if (SUCCEEDED(IWICImagingFactory_CreateBitmapScaler( impl->wic, &scaler )) && scaler)
            {
                if (SUCCEEDED(IWICBitmapScaler_Initialize( scaler, src, w, h, WICBitmapInterpolationModeFant )))
                {
                    IWICBitmapSource_Release( src );
                    src = (IWICBitmapSource *)scaler;
                    scaler = NULL;
                }
                else
                {
                    IWICBitmapScaler_Release( scaler );
                    scaler = NULL;
                }
            }
        }
        if (SUCCEEDED(IWICImagingFactory_CreateFormatConverter( impl->wic, &conv )) && conv)
        {
            if (SUCCEEDED(IWICFormatConverter_Initialize( conv, src, &dst_fmt, WICBitmapDitherTypeNone, NULL, 0.0, WICBitmapPaletteTypeCustom )))
            {
                IWICFormatConverter_CopyPixels( conv, NULL, stride, total, buf );
            }
            IWICFormatConverter_Release( conv );
        }
        IWICBitmapSource_Release( src );
    }

    *out_pixels = buf;
    *out_size = total;
    if (out_w) *out_w = w;
    if (out_h) *out_h = h;
    if (out_stride) *out_stride = stride;
    return S_OK;
}

static HRESULT WINAPI bd_QI( void *iface, REFIID iid, void **out )
{
    struct bitmap_decoder *impl = CONTAINING_RECORD( iface, struct bitmap_decoder, lpVtbl_dec );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IBitmapDecoder_local ))
        *out = &impl->lpVtbl_dec;
    else if (IsEqualGUID( iid, &IID_IBitmapFrame_local ))
        *out = &impl->lpVtbl_frame;
    else if (IsEqualGUID( iid, &IID_IBitmapFrameWithSoftwareBitmap_local ))
        *out = &impl->lpVtbl_fwsb;
    else
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    InterlockedIncrement( &impl->ref );
    return S_OK;
}
static ULONG WINAPI bd_AddRef( void *iface )
{
    return InterlockedIncrement( &CONTAINING_RECORD( iface, struct bitmap_decoder, lpVtbl_dec )->ref );
}
static ULONG WINAPI bd_Release( void *iface )
{
    struct bitmap_decoder *impl = CONTAINING_RECORD( iface, struct bitmap_decoder, lpVtbl_dec );
    ULONG r = InterlockedDecrement( &impl->ref );
    if (!r)
    {
        if (impl->wic_frame) IWICBitmapFrameDecode_Release( impl->wic_frame );
        if (impl->wic) IWICImagingFactory_Release( impl->wic );
        free( impl );
    }
    return r;
}
static HRESULT WINAPI bd_GetIids( void *iface, ULONG *c, IID **i )
{
    if (!c || !i) return E_POINTER;
    *i = CoTaskMemAlloc( 3 * sizeof(IID) );
    if (!*i) { *c = 0; return E_OUTOFMEMORY; }
    (*i)[0] = IID_IBitmapDecoder_local;
    (*i)[1] = IID_IBitmapFrame_local;
    (*i)[2] = IID_IBitmapFrameWithSoftwareBitmap_local;
    *c = 3;
    return S_OK;
}
static HRESULT WINAPI bd_GetRuntimeClassName( void *iface, HSTRING *n )
{
    static const WCHAR cls[] = L"Windows.Graphics.Imaging.BitmapDecoder";
    return WindowsCreateString( cls, ARRAY_SIZE(cls) - 1, n );
}
static HRESULT WINAPI bd_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI bd_get_BitmapContainerProperties( void *iface, void **out ) { if (out) *out = NULL; return E_NOTIMPL; }
static HRESULT WINAPI bd_get_DecoderInformation( void *iface, void **out ) { if (out) *out = NULL; return E_NOTIMPL; }
static HRESULT WINAPI bd_get_FrameCount( void *iface, UINT32 *cnt ) { if (cnt) *cnt = 1; return S_OK; }
static HRESULT WINAPI bd_GetPreviewAsync( void *iface, void **op ) { if (op) *op = NULL; return E_NOTIMPL; }
static HRESULT WINAPI bd_GetFrameAsync( void *iface, UINT32 idx, void **op ) { if (op) *op = NULL; return E_NOTIMPL; }

static HRESULT WINAPI bf_QI( void *iface, REFIID iid, void **out ) { return bd_QI( &CONTAINING_RECORD( iface, struct bitmap_decoder, lpVtbl_frame )->lpVtbl_dec, iid, out ); }
static ULONG WINAPI bf_AddRef( void *iface ) { return bd_AddRef( &CONTAINING_RECORD( iface, struct bitmap_decoder, lpVtbl_frame )->lpVtbl_dec ); }
static ULONG WINAPI bf_Release( void *iface ) { return bd_Release( &CONTAINING_RECORD( iface, struct bitmap_decoder, lpVtbl_frame )->lpVtbl_dec ); }
static HRESULT WINAPI bf_GetThumbnailAsync( void *iface, void **op ) { if (op) *op = NULL; return E_NOTIMPL; }
static HRESULT WINAPI bf_get_BitmapProperties( void *iface, void **out ) { if (out) *out = NULL; return E_NOTIMPL; }
static HRESULT WINAPI bf_get_BitmapPixelFormat( void *iface, UINT32 *v ) { if (v) *v = CONTAINING_RECORD( iface, struct bitmap_decoder, lpVtbl_frame )->pixel_format; return S_OK; }
static HRESULT WINAPI bf_get_BitmapAlphaMode( void *iface, UINT32 *v ) { if (v) *v = CONTAINING_RECORD( iface, struct bitmap_decoder, lpVtbl_frame )->alpha_mode; return S_OK; }
static HRESULT WINAPI bf_get_DpiX( void *iface, double *v ) { if (v) *v = CONTAINING_RECORD( iface, struct bitmap_decoder, lpVtbl_frame )->dpi_x; return S_OK; }
static HRESULT WINAPI bf_get_DpiY( void *iface, double *v ) { if (v) *v = CONTAINING_RECORD( iface, struct bitmap_decoder, lpVtbl_frame )->dpi_y; return S_OK; }
static HRESULT WINAPI bf_get_PixelWidth( void *iface, UINT32 *v ) { if (v) *v = CONTAINING_RECORD( iface, struct bitmap_decoder, lpVtbl_frame )->width; return S_OK; }
static HRESULT WINAPI bf_get_PixelHeight( void *iface, UINT32 *v ) { if (v) *v = CONTAINING_RECORD( iface, struct bitmap_decoder, lpVtbl_frame )->height; return S_OK; }
static HRESULT WINAPI bf_get_OrientedPixelWidth( void *iface, UINT32 *v ) { if (v) *v = CONTAINING_RECORD( iface, struct bitmap_decoder, lpVtbl_frame )->width; return S_OK; }
static HRESULT WINAPI bf_get_OrientedPixelHeight( void *iface, UINT32 *v ) { if (v) *v = CONTAINING_RECORD( iface, struct bitmap_decoder, lpVtbl_frame )->height; return S_OK; }
static HRESULT WINAPI bf_GetPixelDataTransformedAsync( void *iface, UINT32 pix_fmt, UINT32 alpha_mode,
        void *transform, UINT32 exif_mode, UINT32 color_mode, void **op )
{
    struct bitmap_decoder *impl = CONTAINING_RECORD( iface, struct bitmap_decoder, lpVtbl_frame );
    struct pixel_data_provider *pdp;
    HRESULT hr;
    if (!op) return E_POINTER;
    if (!(pdp = calloc( 1, sizeof(*pdp) ))) return E_OUTOFMEMORY;
    pdp->lpVtbl = pdp_vtbl;
    pdp->ref = 1;
    hr = bd_decode_pixels( impl, pix_fmt, alpha_mode, transform, &pdp->pixels, &pdp->size, NULL, NULL, NULL, TRUE );
    if (FAILED(hr))
    {
        pdp_Release( pdp );
        return img_async_op_create( &IID_IAsyncOperation_PixelDataProvider, NULL, hr, op );
    }
    hr = img_async_op_create( &IID_IAsyncOperation_PixelDataProvider, (IUnknown *)pdp, S_OK, op );
    pdp_Release( pdp );
    return hr;
}
static HRESULT WINAPI bf_GetPixelDataAsync( void *iface, void **op )
{
    struct bitmap_decoder *impl = CONTAINING_RECORD( iface, struct bitmap_decoder, lpVtbl_frame );
    return bf_GetPixelDataTransformedAsync( iface, impl->pixel_format, impl->alpha_mode, NULL, 0, 0, op );
}

static HRESULT WINAPI bfwsb_QI( void *iface, REFIID iid, void **out ) { return bd_QI( &CONTAINING_RECORD( iface, struct bitmap_decoder, lpVtbl_fwsb )->lpVtbl_dec, iid, out ); }
static ULONG WINAPI bfwsb_AddRef( void *iface ) { return bd_AddRef( &CONTAINING_RECORD( iface, struct bitmap_decoder, lpVtbl_fwsb )->lpVtbl_dec ); }
static ULONG WINAPI bfwsb_Release( void *iface ) { return bd_Release( &CONTAINING_RECORD( iface, struct bitmap_decoder, lpVtbl_fwsb )->lpVtbl_dec ); }
static HRESULT WINAPI bfwsb_GetSoftwareBitmapTransformedAsync( void *iface, UINT32 pix_fmt, UINT32 alpha_mode,
        void *transform, UINT32 exif_mode, UINT32 color_mode, void **op )
{
    struct bitmap_decoder *impl = CONTAINING_RECORD( iface, struct bitmap_decoder, lpVtbl_fwsb );
    struct software_bitmap *sb;
    UINT32 w = 0, h = 0, stride = 0;
    HRESULT hr;
    if (!op) return E_POINTER;
    if (!(sb = calloc( 1, sizeof(*sb) ))) return E_OUTOFMEMORY;
    sb->lpVtbl_sb = sb_vtbl;
    sb->lpVtbl_close = sb_cl_vtbl;
    sb->ref = 1;
    sb->pixel_format = pix_fmt;
    sb->alpha_mode = alpha_mode;
    sb->dpi_x = impl->dpi_x;
    sb->dpi_y = impl->dpi_y;
    hr = bd_decode_pixels( impl, pix_fmt, alpha_mode, transform, &sb->pixels, &sb->size, &w, &h, &stride, FALSE );
    if (FAILED(hr))
    {
        sb_Release( &sb->lpVtbl_sb );
        return img_async_op_create( &IID_IAsyncOperation_SoftwareBitmap, NULL, hr, op );
    }
    sb->width = (INT32)w;
    sb->height = (INT32)h;
    sb->stride = (INT32)stride;
    hr = img_async_op_create( &IID_IAsyncOperation_SoftwareBitmap, (IUnknown *)&sb->lpVtbl_sb, S_OK, op );
    sb_Release( &sb->lpVtbl_sb );
    return hr;
}
static HRESULT WINAPI bfwsb_GetSoftwareBitmapAsync( void *iface, void **op )
{
    struct bitmap_decoder *impl = CONTAINING_RECORD( iface, struct bitmap_decoder, lpVtbl_fwsb );
    return bfwsb_GetSoftwareBitmapTransformedAsync( iface, impl->pixel_format, impl->alpha_mode, NULL, 0, 0, op );
}
static HRESULT WINAPI bfwsb_GetSoftwareBitmapConvertedAsync( void *iface, UINT32 pix_fmt, UINT32 alpha_mode, void **op )
{
    return bfwsb_GetSoftwareBitmapTransformedAsync( iface, pix_fmt, alpha_mode, NULL, 0, 0, op );
}

static const void * const bd_dec_vtbl[] =
{
    bd_QI, bd_AddRef, bd_Release, bd_GetIids, bd_GetRuntimeClassName, bd_GetTrustLevel,
    bd_get_BitmapContainerProperties, bd_get_DecoderInformation, bd_get_FrameCount,
    bd_GetPreviewAsync, bd_GetFrameAsync,
};
static const void * const bd_frame_vtbl[] =
{
    bf_QI, bf_AddRef, bf_Release, bd_GetIids, bd_GetRuntimeClassName, bd_GetTrustLevel,
    bf_GetThumbnailAsync, bf_get_BitmapProperties, bf_get_BitmapPixelFormat, bf_get_BitmapAlphaMode,
    bf_get_DpiX, bf_get_DpiY, bf_get_PixelWidth, bf_get_PixelHeight,
    bf_get_OrientedPixelWidth, bf_get_OrientedPixelHeight,
    bf_GetPixelDataAsync, bf_GetPixelDataTransformedAsync,
};
static const void * const bd_fwsb_vtbl[] =
{
    bfwsb_QI, bfwsb_AddRef, bfwsb_Release, bd_GetIids, bd_GetRuntimeClassName, bd_GetTrustLevel,
    bfwsb_GetSoftwareBitmapAsync, bfwsb_GetSoftwareBitmapConvertedAsync, bfwsb_GetSoftwareBitmapTransformedAsync,
};

static HRESULT read_ras_to_hglobal( void *ras_in, HGLOBAL *out_hmem )
{
    void *ras = NULL, *istm = NULL, *buf_fac = NULL, *ibuf = NULL, *read_op = NULL, *res_buf = NULL, *bba = NULL;
    UINT64 size64 = 0;
    UINT32 size32, got = 0;
    HSTRING hcls = NULL;
    HGLOBAL hmem = NULL;
    BYTE *src_ptr = NULL, *dst_ptr = NULL;
    HRESULT hr;

    *out_hmem = NULL;
    if (!ras_in) return E_INVALIDARG;
    hr = IUnknown_QueryInterface( (IUnknown *)ras_in, &IID_IRandomAccessStream_local, &ras );
    if (FAILED(hr) || !ras) return hr;

    /* IRandomAccessStream: slot 6 = get_Size, slot 8 = GetInputStreamAt */
    hr = ((HRESULT (WINAPI *)(void *, UINT64 *))(*(void ***)ras)[6])( ras, &size64 );
    if (FAILED(hr) || !size64 || size64 > 64 * 1024 * 1024)
    {
        IUnknown_Release( (IUnknown *)ras );
        return FAILED(hr) ? hr : E_FAIL;
    }
    size32 = (UINT32)size64;

    hr = ((HRESULT (WINAPI *)(void *, UINT64, void **))(*(void ***)ras)[8])( ras, 0, &istm );
    IUnknown_Release( (IUnknown *)ras );
    if (FAILED(hr) || !istm) return FAILED(hr) ? hr : E_FAIL;

    if (SUCCEEDED(WindowsCreateString( L"Windows.Storage.Streams.Buffer", 30, &hcls )))
    {
        RoGetActivationFactory( hcls, &IID_IBufferFactory_local, &buf_fac );
        WindowsDeleteString( hcls );
    }
    if (!buf_fac) { IUnknown_Release( (IUnknown *)istm ); return E_FAIL; }

    /* IBufferFactory::Create(capacity, &ibuf) = slot 6 */
    hr = ((HRESULT (WINAPI *)(void *, UINT32, void **))(*(void ***)buf_fac)[6])( buf_fac, size32, &ibuf );
    IUnknown_Release( (IUnknown *)buf_fac );
    if (FAILED(hr) || !ibuf) { IUnknown_Release( (IUnknown *)istm ); return FAILED(hr) ? hr : E_FAIL; }

    /* IInputStream::ReadAsync(ibuf, count, InputStreamOptions_None, &op) = slot 6 */
    hr = ((HRESULT (WINAPI *)(void *, void *, UINT32, UINT32, void **))(*(void ***)istm)[6])( istm, ibuf, size32, 0, &read_op );
    IUnknown_Release( (IUnknown *)istm );
    if (SUCCEEDED(hr) && read_op)
    {
        /* IAsyncOperationWithProgress::GetResults = slot 10 */
        hr = ((HRESULT (WINAPI *)(void *, void **))(*(void ***)read_op)[10])( read_op, &res_buf );
        IUnknown_Release( (IUnknown *)read_op );
    }
    if (!res_buf) { res_buf = ibuf; IUnknown_AddRef( (IUnknown *)res_buf ); }

    /* IBuffer::get_Length = slot 7 */
    ((HRESULT (WINAPI *)(void *, UINT32 *))(*(void ***)res_buf)[7])( res_buf, &got );
    if (!got) got = size32;

    hr = IUnknown_QueryInterface( (IUnknown *)res_buf, &IID_IBufferByteAccess_local, &bba );
    if (SUCCEEDED(hr) && bba)
    {
        hr = ((HRESULT (WINAPI *)(void *, BYTE **))(*(void ***)bba)[3])( bba, &src_ptr );
        if (SUCCEEDED(hr) && src_ptr)
        {
            hmem = GlobalAlloc( GMEM_MOVEABLE, got );
            if (hmem && (dst_ptr = GlobalLock( hmem )))
            {
                memcpy( dst_ptr, src_ptr, got );
                GlobalUnlock( hmem );
                *out_hmem = hmem;
            }
            else if (hmem)
            {
                GlobalFree( hmem );
                hr = E_OUTOFMEMORY;
            }
        }
        IUnknown_Release( (IUnknown *)bba );
    }
    IUnknown_Release( (IUnknown *)res_buf );
    IUnknown_Release( (IUnknown *)ibuf );
    return *out_hmem ? S_OK : (FAILED(hr) ? hr : E_FAIL);
}

struct bd_factory
{
    const void *lpVtbl_factory;
    const void *lpVtbl_statics;
    const void *lpVtbl_statics2;
};
static struct bd_factory bd_factory_obj;

static HRESULT WINAPI bd_fac_QI( void *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IActivationFactory ))
        *out = &bd_factory_obj.lpVtbl_factory;
    else if (IsEqualGUID( iid, &IID_IBitmapDecoderStatics_local ))
        *out = &bd_factory_obj.lpVtbl_statics;
    else if (IsEqualGUID( iid, &IID_IBitmapDecoderStatics2_local ))
        *out = &bd_factory_obj.lpVtbl_statics2;
    else
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    return S_OK;
}
static ULONG WINAPI bd_fac_AddRef( void *iface ) { return 2; }
static ULONG WINAPI bd_fac_Release( void *iface ) { return 1; }
static HRESULT WINAPI bd_fac_GetIids( void *iface, ULONG *c, IID **i ) { if (c) *c = 0; if (i) *i = NULL; return S_OK; }
static HRESULT WINAPI bd_fac_GetRuntimeClassName( void *iface, HSTRING *n )
{
    static const WCHAR cls[] = L"Windows.Graphics.Imaging.BitmapDecoder";
    return WindowsCreateString( cls, ARRAY_SIZE(cls) - 1, n );
}
static HRESULT WINAPI bd_fac_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI bd_fac_ActivateInstance( void *iface, IInspectable **out ) { if (out) *out = NULL; return E_NOTIMPL; }

static HRESULT WINAPI bd_st_BmpDecoderId( void *iface, GUID *id ) { if (id) *id = CLSID_WICBmpDecoder; return S_OK; }
static HRESULT WINAPI bd_st_JpegDecoderId( void *iface, GUID *id ) { if (id) *id = CLSID_WICJpegDecoder; return S_OK; }
static HRESULT WINAPI bd_st_PngDecoderId( void *iface, GUID *id ) { if (id) *id = CLSID_WICPngDecoder; return S_OK; }
static HRESULT WINAPI bd_st_TiffDecoderId( void *iface, GUID *id ) { if (id) *id = CLSID_WICTiffDecoder; return S_OK; }
static HRESULT WINAPI bd_st_GifDecoderId( void *iface, GUID *id ) { if (id) *id = CLSID_WICGifDecoder; return S_OK; }
static HRESULT WINAPI bd_st_JpegXRDecoderId( void *iface, GUID *id ) { if (id) *id = CLSID_WICWmpDecoder; return S_OK; }
static HRESULT WINAPI bd_st_IcoDecoderId( void *iface, GUID *id ) { if (id) *id = CLSID_WICIcoDecoder; return S_OK; }
static HRESULT WINAPI bd_st_GetDecoderInfoEnum( void *iface, void **out ) { if (out) *out = NULL; return E_NOTIMPL; }

static HRESULT WINAPI bd_st_CreateAsync( void *iface, void *stream, void **op )
{
    struct bitmap_decoder *dec;
    HGLOBAL hmem = NULL;
    IStream *istm = NULL;
    IWICBitmapDecoder *wic_dec = NULL;
    HRESULT hr;

    if (!op) return E_POINTER;
    if (!(dec = calloc( 1, sizeof(*dec) ))) return E_OUTOFMEMORY;
    dec->lpVtbl_dec = bd_dec_vtbl;
    dec->lpVtbl_frame = bd_frame_vtbl;
    dec->lpVtbl_fwsb = bd_fwsb_vtbl;
    dec->ref = 1;
    dec->width = 1;
    dec->height = 1;
    dec->dpi_x = 96.0;
    dec->dpi_y = 96.0;
    dec->pixel_format = 30; /* Rgba8 */
    dec->alpha_mode = 1;    /* Straight */

    if (SUCCEEDED(read_ras_to_hglobal( stream, &hmem )) && hmem)
    {
        if (SUCCEEDED(CreateStreamOnHGlobal( hmem, TRUE, &istm )) && istm)
        {
            if (SUCCEEDED(CoCreateInstance( &CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
                                            &IID_IWICImagingFactory, (void **)&dec->wic )) && dec->wic)
            {
                if (SUCCEEDED(IWICImagingFactory_CreateDecoderFromStream( dec->wic, istm, NULL,
                                WICDecodeMetadataCacheOnLoad, &wic_dec )) && wic_dec)
                {
                    if (SUCCEEDED(IWICBitmapDecoder_GetFrame( wic_dec, 0, &dec->wic_frame )) && dec->wic_frame)
                    {
                        IWICBitmapFrameDecode_GetSize( dec->wic_frame, &dec->width, &dec->height );
                        IWICBitmapFrameDecode_GetResolution( dec->wic_frame, &dec->dpi_x, &dec->dpi_y );
                    }
                    IWICBitmapDecoder_Release( wic_dec );
                }
            }
            IStream_Release( istm );
        }
        else
            GlobalFree( hmem );
    }

    hr = img_async_op_create( &IID_IAsyncOperation_BitmapDecoder, (IUnknown *)&dec->lpVtbl_dec, S_OK, op );
    bd_Release( &dec->lpVtbl_dec );
    return hr;
}
static HRESULT WINAPI bd_st_CreateWithIdAsync( void *iface, GUID id, void *stream, void **op )
{
    return bd_st_CreateAsync( iface, stream, op );
}
static HRESULT WINAPI bd_st2_HeifDecoderId( void *iface, GUID *id ) { if (id) memset( id, 0, sizeof(*id) ); return S_OK; }
static HRESULT WINAPI bd_st2_WebpDecoderId( void *iface, GUID *id ) { if (id) memset( id, 0, sizeof(*id) ); return S_OK; }

static const void * const bd_fac_vtbl[] = { bd_fac_QI, bd_fac_AddRef, bd_fac_Release, bd_fac_GetIids, bd_fac_GetRuntimeClassName, bd_fac_GetTrustLevel, bd_fac_ActivateInstance };
static const void * const bd_st_vtbl[] =
{
    bd_fac_QI, bd_fac_AddRef, bd_fac_Release, bd_fac_GetIids, bd_fac_GetRuntimeClassName, bd_fac_GetTrustLevel,
    bd_st_BmpDecoderId, bd_st_JpegDecoderId, bd_st_PngDecoderId, bd_st_TiffDecoderId,
    bd_st_GifDecoderId, bd_st_JpegXRDecoderId, bd_st_IcoDecoderId,
    bd_st_GetDecoderInfoEnum, bd_st_CreateAsync, bd_st_CreateWithIdAsync,
};
static const void * const bd_st2_vtbl[] =
{
    bd_fac_QI, bd_fac_AddRef, bd_fac_Release, bd_fac_GetIids, bd_fac_GetRuntimeClassName, bd_fac_GetTrustLevel,
    bd_st2_HeifDecoderId, bd_st2_WebpDecoderId,
};
static struct bd_factory bd_factory_obj = { bd_fac_vtbl, bd_st_vtbl, bd_st2_vtbl };

/* ============================================================================
 * Windows.Devices.Display.DisplayMonitor (IDisplayMonitorStatics,
 *                                         IDisplayMonitor, IDisplayMonitor2)
 * ============================================================================ */

static const GUID IID_IDisplayMonitorStatics_local =
    { 0x6eae698f, 0xa228, 0x4c05, {0x82, 0x1d, 0xb6, 0x95, 0xd6, 0x67, 0xde, 0x8e} };
static const GUID IID_IDisplayMonitor_local =
    { 0x1f6b15d4, 0x1d01, 0x4c51, {0x87, 0xe2, 0x6f, 0x95, 0x4a, 0x77, 0x2b, 0x59} };
static const GUID IID_IDisplayMonitor2_local =
    { 0x023018e6, 0xcb23, 0x5830, {0x96, 0xdf, 0xa7, 0xbf, 0x6e, 0x60, 0x25, 0x77} };
static const GUID IID_IAsyncOperation_DisplayMonitor_local =
    { 0x28c0e623, 0x4e46, 0x58c3, {0xad, 0x82, 0x50, 0x2b, 0xde, 0xcc, 0x43, 0x45} };
static const GUID IID_IAsyncInfo_dm_local =
    { 0x00000036, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46} };

struct dm_size_int32 { INT32 Width; INT32 Height; };
struct dm_point { float X; float Y; };

struct display_monitor
{
    const void *lpVtbl_dm;
    const void *lpVtbl_dm2;
    LONG ref;
    HSTRING device_id;
};

static HRESULT WINAPI dm_QI_impl( struct display_monitor *impl, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IDisplayMonitor_local ))
    {
        *out = &impl->lpVtbl_dm;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IDisplayMonitor2_local ))
    {
        *out = &impl->lpVtbl_dm2;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI dm_AddRef_impl( struct display_monitor *impl ) { return InterlockedIncrement( &impl->ref ); }
static ULONG WINAPI dm_Release_impl( struct display_monitor *impl )
{
    ULONG r = InterlockedDecrement( &impl->ref );
    if (!r)
    {
        if (impl->device_id) WindowsDeleteString( impl->device_id );
        free( impl );
    }
    return r;
}
static HRESULT WINAPI dm_QI( void *iface, REFIID iid, void **out )
{
    return dm_QI_impl( CONTAINING_RECORD( iface, struct display_monitor, lpVtbl_dm ), iid, out );
}
static ULONG WINAPI dm_AddRef( void *iface )
{
    return dm_AddRef_impl( CONTAINING_RECORD( iface, struct display_monitor, lpVtbl_dm ) );
}
static ULONG WINAPI dm_Release( void *iface )
{
    return dm_Release_impl( CONTAINING_RECORD( iface, struct display_monitor, lpVtbl_dm ) );
}
static HRESULT WINAPI dm2_QI( void *iface, REFIID iid, void **out )
{
    return dm_QI_impl( CONTAINING_RECORD( iface, struct display_monitor, lpVtbl_dm2 ), iid, out );
}
static ULONG WINAPI dm2_AddRef( void *iface )
{
    return dm_AddRef_impl( CONTAINING_RECORD( iface, struct display_monitor, lpVtbl_dm2 ) );
}
static ULONG WINAPI dm2_Release( void *iface )
{
    return dm_Release_impl( CONTAINING_RECORD( iface, struct display_monitor, lpVtbl_dm2 ) );
}
static HRESULT WINAPI dm_GetIids( void *iface, ULONG *c, IID **i )
{
    if (!c || !i) return E_POINTER;
    *i = CoTaskMemAlloc( 2 * sizeof(IID) );
    if (!*i) { *c = 0; return E_OUTOFMEMORY; }
    (*i)[0] = IID_IDisplayMonitor_local;
    (*i)[1] = IID_IDisplayMonitor2_local;
    *c = 2;
    return S_OK;
}
static HRESULT WINAPI dm_GetRuntimeClassName( void *iface, HSTRING *n )
{
    static const WCHAR cls[] = L"Windows.Devices.Display.DisplayMonitor";
    return WindowsCreateString( cls, ARRAY_SIZE(cls) - 1, n );
}
static HRESULT WINAPI dm_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }

static HRESULT WINAPI dm_get_DeviceId( void *iface, HSTRING *out )
{
    struct display_monitor *impl = CONTAINING_RECORD( iface, struct display_monitor, lpVtbl_dm );
    if (!out) return E_POINTER;
    if (impl->device_id) return WindowsDuplicateString( impl->device_id, out );
    return WindowsCreateString( L"DISPLAY1", 8, out );
}
static HRESULT WINAPI dm_get_DisplayName( void *iface, HSTRING *out )
{
    static const WCHAR s[] = L"Wine Display";
    if (!out) return E_POINTER;
    return WindowsCreateString( s, ARRAY_SIZE(s) - 1, out );
}
static HRESULT WINAPI dm_get_ConnectionKind( void *iface, INT32 *out ) { if (out) *out = 1; /* Wired */ return S_OK; }
static HRESULT WINAPI dm_get_PhysicalConnector( void *iface, INT32 *out ) { if (out) *out = 5; /* DisplayPort */ return S_OK; }
static HRESULT WINAPI dm_get_DisplayAdapterDeviceId( void *iface, HSTRING *out )
{
    static const WCHAR s[] = L"PCI\\VEN_10DE&DEV_0000";
    if (!out) return E_POINTER;
    return WindowsCreateString( s, ARRAY_SIZE(s) - 1, out );
}
static HRESULT WINAPI dm_get_DisplayAdapterId( void *iface, UINT64 *out ) { if (out) *out = 1; return S_OK; }
static HRESULT WINAPI dm_get_DisplayAdapterTargetId( void *iface, UINT32 *out ) { if (out) *out = 0; return S_OK; }
static HRESULT WINAPI dm_get_UsageKind( void *iface, INT32 *out ) { if (out) *out = 0; /* Standard */ return S_OK; }
static HRESULT WINAPI dm_get_NativeResolutionInRawPixels( void *iface, struct dm_size_int32 *out )
{
    if (!out) return E_POINTER;
    out->Width = GetSystemMetrics( SM_CXSCREEN );
    out->Height = GetSystemMetrics( SM_CYSCREEN );
    if (out->Width <= 0) out->Width = 1920;
    if (out->Height <= 0) out->Height = 1080;
    return S_OK;
}
static HRESULT WINAPI dm_get_PhysicalSizeInInches( void *iface, void **out ) { if (out) *out = NULL; return S_OK; }
static HRESULT WINAPI dm_get_RawDpiX( void *iface, float *out ) { if (out) *out = 96.0f; return S_OK; }
static HRESULT WINAPI dm_get_RawDpiY( void *iface, float *out ) { if (out) *out = 96.0f; return S_OK; }
static HRESULT WINAPI dm_get_RedPrimary( void *iface, struct dm_point *out ) { if (out) { out->X = 0.64f; out->Y = 0.33f; } return S_OK; }
static HRESULT WINAPI dm_get_GreenPrimary( void *iface, struct dm_point *out ) { if (out) { out->X = 0.30f; out->Y = 0.60f; } return S_OK; }
static HRESULT WINAPI dm_get_BluePrimary( void *iface, struct dm_point *out ) { if (out) { out->X = 0.15f; out->Y = 0.06f; } return S_OK; }
static HRESULT WINAPI dm_get_WhitePoint( void *iface, struct dm_point *out ) { if (out) { out->X = 0.3127f; out->Y = 0.3290f; } return S_OK; }
static HRESULT WINAPI dm_get_MaxLuminanceInNits( void *iface, float *out ) { if (out) *out = 300.0f; return S_OK; }
static HRESULT WINAPI dm_get_MinLuminanceInNits( void *iface, float *out ) { if (out) *out = 0.1f; return S_OK; }
static HRESULT WINAPI dm_get_MaxAverageFullFrameLuminanceInNits( void *iface, float *out ) { if (out) *out = 250.0f; return S_OK; }
static HRESULT WINAPI dm_GetDescriptor( void *iface, INT32 kind, UINT32 *len, BYTE **data )
{
    if (len) *len = 0;
    if (data) *data = NULL;
    return S_OK;
}
static HRESULT WINAPI dm2_get_IsDolbyVisionSupportedInHdrMode( void *iface, boolean *out )
{
    if (out) *out = FALSE;
    return S_OK;
}

static const void * const dm_vtbl[] =
{
    dm_QI, dm_AddRef, dm_Release, dm_GetIids, dm_GetRuntimeClassName, dm_GetTrustLevel,
    dm_get_DeviceId, dm_get_DisplayName, dm_get_ConnectionKind, dm_get_PhysicalConnector,
    dm_get_DisplayAdapterDeviceId, dm_get_DisplayAdapterId, dm_get_DisplayAdapterTargetId,
    dm_get_UsageKind, dm_get_NativeResolutionInRawPixels, dm_get_PhysicalSizeInInches,
    dm_get_RawDpiX, dm_get_RawDpiY,
    dm_get_RedPrimary, dm_get_GreenPrimary, dm_get_BluePrimary, dm_get_WhitePoint,
    dm_get_MaxLuminanceInNits, dm_get_MinLuminanceInNits, dm_get_MaxAverageFullFrameLuminanceInNits,
    dm_GetDescriptor,
};
static const void * const dm2_vtbl[] =
{
    dm2_QI, dm2_AddRef, dm2_Release, dm_GetIids, dm_GetRuntimeClassName, dm_GetTrustLevel,
    dm2_get_IsDolbyVisionSupportedInHdrMode,
};

struct dm_async_op
{
    const void *lpVtbl_op;
    const void *lpVtbl_info;
    LONG ref;
    void *handler;
    HSTRING device_id;
};

static HRESULT WINAPI dmao_QI( void *iface, REFIID iid, void **out )
{
    struct dm_async_op *impl = CONTAINING_RECORD( iface, struct dm_async_op, lpVtbl_op );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IAsyncOperation_DisplayMonitor_local ))
    {
        *out = &impl->lpVtbl_op;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IAsyncInfo_dm_local ))
    {
        *out = &impl->lpVtbl_info;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI dmao_AddRef( void *iface )
{
    struct dm_async_op *impl = CONTAINING_RECORD( iface, struct dm_async_op, lpVtbl_op );
    return InterlockedIncrement( &impl->ref );
}
static ULONG WINAPI dmao_Release( void *iface )
{
    struct dm_async_op *impl = CONTAINING_RECORD( iface, struct dm_async_op, lpVtbl_op );
    ULONG r = InterlockedDecrement( &impl->ref );
    if (!r)
    {
        if (impl->handler) IUnknown_Release( (IUnknown *)impl->handler );
        if (impl->device_id) WindowsDeleteString( impl->device_id );
        free( impl );
    }
    return r;
}
static HRESULT WINAPI dmao_GetIids( void *iface, ULONG *c, IID **i ) { if (c) *c = 0; if (i) *i = NULL; return S_OK; }
static HRESULT WINAPI dmao_GetRuntimeClassName( void *iface, HSTRING *n )
{
    static const WCHAR cls[] = L"Windows.Foundation.IAsyncOperation`1<Windows.Devices.Display.DisplayMonitor>";
    return WindowsCreateString( cls, ARRAY_SIZE(cls) - 1, n );
}
static HRESULT WINAPI dmao_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI dmao_put_Completed( void *iface, void *handler )
{
    struct dm_async_op *impl = CONTAINING_RECORD( iface, struct dm_async_op, lpVtbl_op );
    if (impl->handler) IUnknown_Release( (IUnknown *)impl->handler );
    impl->handler = handler;
    if (handler)
    {
        typedef HRESULT (WINAPI *invoke_fn)(void *, void *, UINT32);
        void **vtbl = *(void ***)handler;
        IUnknown_AddRef( (IUnknown *)handler );
        ((invoke_fn)vtbl[3])( handler, &impl->lpVtbl_op, 1 /* Completed */ );
    }
    return S_OK;
}
static HRESULT WINAPI dmao_get_Completed( void *iface, void **out )
{
    struct dm_async_op *impl = CONTAINING_RECORD( iface, struct dm_async_op, lpVtbl_op );
    if (!out) return E_POINTER;
    *out = impl->handler;
    if (*out) IUnknown_AddRef( (IUnknown *)*out );
    return S_OK;
}
static HRESULT WINAPI dmao_GetResults( void *iface, void **out )
{
    struct dm_async_op *impl = CONTAINING_RECORD( iface, struct dm_async_op, lpVtbl_op );
    struct display_monitor *dm;
    if (!out) return E_POINTER;
    if (!(dm = calloc( 1, sizeof(*dm) ))) return E_OUTOFMEMORY;
    dm->lpVtbl_dm = dm_vtbl;
    dm->lpVtbl_dm2 = dm2_vtbl;
    dm->ref = 1;
    if (impl->device_id) WindowsDuplicateString( impl->device_id, &dm->device_id );
    *out = &dm->lpVtbl_dm;
    return S_OK;
}
static HRESULT WINAPI dmao_info_QI( void *iface, REFIID iid, void **out )
{
    struct dm_async_op *impl = CONTAINING_RECORD( iface, struct dm_async_op, lpVtbl_info );
    return dmao_QI( &impl->lpVtbl_op, iid, out );
}
static ULONG WINAPI dmao_info_AddRef( void *iface )
{
    struct dm_async_op *impl = CONTAINING_RECORD( iface, struct dm_async_op, lpVtbl_info );
    return dmao_AddRef( &impl->lpVtbl_op );
}
static ULONG WINAPI dmao_info_Release( void *iface )
{
    struct dm_async_op *impl = CONTAINING_RECORD( iface, struct dm_async_op, lpVtbl_info );
    return dmao_Release( &impl->lpVtbl_op );
}
static HRESULT WINAPI dmao_info_get_Id( void *iface, UINT32 *id ) { if (id) *id = 1; return S_OK; }
static HRESULT WINAPI dmao_info_get_Status( void *iface, UINT32 *st ) { if (st) *st = 1; return S_OK; }
static HRESULT WINAPI dmao_info_get_ErrorCode( void *iface, HRESULT *hr ) { if (hr) *hr = S_OK; return S_OK; }
static HRESULT WINAPI dmao_info_Cancel( void *iface ) { return S_OK; }
static HRESULT WINAPI dmao_info_Close( void *iface ) { return S_OK; }

static const void * const dmao_op_vtbl[] =
{
    dmao_QI, dmao_AddRef, dmao_Release, dmao_GetIids, dmao_GetRuntimeClassName, dmao_GetTrustLevel,
    dmao_put_Completed, dmao_get_Completed, dmao_GetResults,
};
static const void * const dmao_info_vtbl[] =
{
    dmao_info_QI, dmao_info_AddRef, dmao_info_Release, dmao_GetIids, dmao_GetRuntimeClassName, dmao_GetTrustLevel,
    dmao_info_get_Id, dmao_info_get_Status, dmao_info_get_ErrorCode, dmao_info_Cancel, dmao_info_Close,
};

struct dm_factory
{
    const void *lpVtbl_factory;
    const void *lpVtbl_statics;
};
static struct dm_factory dm_factory_obj;

static HRESULT WINAPI dm_fac_QI( void *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = &dm_factory_obj.lpVtbl_factory;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IDisplayMonitorStatics_local ))
    {
        *out = &dm_factory_obj.lpVtbl_statics;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI dm_fac_AddRef( void *iface ) { return 2; }
static ULONG WINAPI dm_fac_Release( void *iface ) { return 1; }
static HRESULT WINAPI dm_fac_GetIids( void *iface, ULONG *c, IID **i ) { if (c) *c = 0; if (i) *i = NULL; return S_OK; }
static HRESULT WINAPI dm_fac_GetRuntimeClassName( void *iface, HSTRING *n )
{
    static const WCHAR cls[] = L"Windows.Devices.Display.DisplayMonitor";
    return WindowsCreateString( cls, ARRAY_SIZE(cls) - 1, n );
}
static HRESULT WINAPI dm_fac_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI dm_fac_ActivateInstance( void *iface, IInspectable **out ) { if (out) *out = NULL; return E_NOTIMPL; }

static HRESULT WINAPI dm_st_GetDeviceSelector( void *iface, HSTRING *out )
{
    static const WCHAR sel[] = L"System.Devices.InterfaceClassGuid:=\"{E6F07B5F-EE97-4a90-B076-33F57BF4EAA7}\"";
    if (!out) return E_POINTER;
    return WindowsCreateString( sel, ARRAY_SIZE(sel) - 1, out );
}
static HRESULT WINAPI dm_st_FromIdAsync( void *iface, HSTRING id, void **out )
{
    struct dm_async_op *op;
    if (!out) return E_POINTER;
    if (!(op = calloc( 1, sizeof(*op) ))) return E_OUTOFMEMORY;
    op->lpVtbl_op = dmao_op_vtbl;
    op->lpVtbl_info = dmao_info_vtbl;
    op->ref = 1;
    if (id) WindowsDuplicateString( id, &op->device_id );
    *out = &op->lpVtbl_op;
    return S_OK;
}

static const void * const dm_fac_vtbl[] =
{
    dm_fac_QI, dm_fac_AddRef, dm_fac_Release, dm_fac_GetIids, dm_fac_GetRuntimeClassName, dm_fac_GetTrustLevel,
    dm_fac_ActivateInstance,
};
static const void * const dm_st_vtbl[] =
{
    dm_fac_QI, dm_fac_AddRef, dm_fac_Release, dm_fac_GetIids, dm_fac_GetRuntimeClassName, dm_fac_GetTrustLevel,
    dm_st_GetDeviceSelector, dm_st_FromIdAsync, dm_st_FromIdAsync,
};
static struct dm_factory dm_factory_obj = { dm_fac_vtbl, dm_st_vtbl };

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
    else if (!wcscmp( name, L"Windows.Graphics.Imaging.BitmapTransform" ))
        IActivationFactory_QueryInterface( &bitmap_transform_factory, &IID_IActivationFactory, (void **)factory );
    else if (!wcscmp( name, L"Windows.Graphics.Imaging.BitmapDecoder" ))
        IActivationFactory_QueryInterface( (IActivationFactory *)&bd_factory_obj.lpVtbl_factory, &IID_IActivationFactory, (void **)factory );
    else if (!wcscmp( name, L"Windows.Devices.Display.DisplayMonitor" ))
        IActivationFactory_QueryInterface( (IActivationFactory *)&dm_factory_obj.lpVtbl_factory, &IID_IActivationFactory, (void **)factory );

    return *factory ? S_OK : E_NOINTERFACE;
}
