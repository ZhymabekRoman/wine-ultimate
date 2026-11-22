/* WinRT Windows.ApplicationModel.Core.CoreApplication implementation
 *
 * Copyright 2025 Zhiyi Zhang for CodeWeavers
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

#include "private.h"
#include "roapi.h"
#include "wine/exception.h"
#include "shellapi.h"
#define WIDL_using_Windows_UI_Core
#include "windows.ui.core.h"
#define WIDL_using_Windows_ApplicationModel_Activation
#include "windows.applicationmodel.activation.h"

WINE_DEFAULT_DEBUG_CHANNEL(twinapi);

/* C++/WinRT static-lifetime helper: get_activation_factory<IStaticLifetime>(class)
 * then GetCollection() returns a process-wide IMap<HSTRING,IInspectable> used to
 * cache static instances tied to the CoreApplication lifetime. WhatsApp/FrameworkUdk
 * hard-requires this on the CoreApplication factory; without it XAML fail-fasts. */
static const GUID IID_IStaticLifetime = {0x17b0e613,0x942a,0x422d,{0x90,0x4c,0xf9,0x0d,0xc7,0x1a,0x7d,0xae}};

typedef struct IStaticLifetime IStaticLifetime;
/* IStaticLifetime (17b0e613) is an IInspectable-derived interface with several
 * methods. FrameworkUdk's FinalUnhandledErrorDetected_RegisterHandler_Impl calls
 * vtable SLOT 11 (offset 0x58) directly: HRESULT(this, ptr, ptr). The other
 * method slots (6..10, 12+) are unknown; stub them all to succeed so the slot-11
 * call lands on a valid no-op instead of reading past the vtable. */
typedef struct IStaticLifetimeVtbl
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)( IStaticLifetime *, REFIID, void ** );
    ULONG   (STDMETHODCALLTYPE *AddRef)( IStaticLifetime * );
    ULONG   (STDMETHODCALLTYPE *Release)( IStaticLifetime * );
    HRESULT (STDMETHODCALLTYPE *GetIids)( IStaticLifetime *, ULONG *, IID ** );
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)( IStaticLifetime *, HSTRING * );
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)( IStaticLifetime *, TrustLevel * );
    HRESULT (STDMETHODCALLTYPE *GetCollection)( IStaticLifetime *, __FIMap_2_HSTRING_IInspectable ** ); /* slot 6 */
    HRESULT (STDMETHODCALLTYPE *method7)( IStaticLifetime *, void *, void *, void * );
    HRESULT (STDMETHODCALLTYPE *method8)( IStaticLifetime *, void *, void *, void * );
    HRESULT (STDMETHODCALLTYPE *method9)( IStaticLifetime *, void *, void *, void * );
    HRESULT (STDMETHODCALLTYPE *method10)( IStaticLifetime *, void *, void *, void * );
    HRESULT (STDMETHODCALLTYPE *method11)( IStaticLifetime *, void *, void *, void * ); /* slot 11 — called by FrameworkUdk */
    HRESULT (STDMETHODCALLTYPE *method12)( IStaticLifetime *, void *, void *, void * );
} IStaticLifetimeVtbl;
struct IStaticLifetime { const IStaticLifetimeVtbl *lpVtbl; };

struct factory
{
    IActivationFactory IActivationFactory_iface;
    ICoreApplication ICoreApplication_iface;
    ICoreApplication2 ICoreApplication2_iface;
    ICoreApplicationUnhandledError ICoreApplicationUnhandledError_iface;
    IStaticLifetime IStaticLifetime_iface;
    IWineSessionProperties IWineSessionProperties_iface;
    ICoreImmersiveApplication ICoreImmersiveApplication_iface;
    __FIMap_2_HSTRING_IInspectable *static_map;
    LONG ref;
};

static inline struct factory *impl_from_IActivationFactory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct factory, IActivationFactory_iface );
}

static inline struct factory *impl_from_ICoreApplication( ICoreApplication *iface )
{
    return CONTAINING_RECORD( iface, struct factory, ICoreApplication_iface );
}

static HRESULT WINAPI activation_factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct factory *impl = impl_from_IActivationFactory( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        IActivationFactory_AddRef( &impl->IActivationFactory_iface );
        *out = &impl->IActivationFactory_iface;
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_ICoreApplication ))
    {
        IActivationFactory_AddRef( &impl->IActivationFactory_iface );
        *out = &impl->ICoreApplication_iface;
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_ICoreApplication2 ))
    {
        IActivationFactory_AddRef( &impl->IActivationFactory_iface );
        *out = &impl->ICoreApplication2_iface;
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IStaticLifetime ))
    {
        IActivationFactory_AddRef( &impl->IActivationFactory_iface );
        *out = &impl->IStaticLifetime_iface;
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_ICoreApplicationUnhandledError ))
    {
        IActivationFactory_AddRef( &impl->IActivationFactory_iface );
        *out = &impl->ICoreApplicationUnhandledError_iface;
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_ICoreImmersiveApplication ))
    {
        IActivationFactory_AddRef( &impl->IActivationFactory_iface );
        *out = &impl->ICoreImmersiveApplication_iface;
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IWineSessionProperties ))
    {
        IActivationFactory_AddRef( &impl->IActivationFactory_iface );
        *out = &impl->IWineSessionProperties_iface;
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI activation_factory_AddRef( IActivationFactory *iface )
{
    struct factory *impl = impl_from_IActivationFactory( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p, ref %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI activation_factory_Release( IActivationFactory *iface )
{
    struct factory *impl = impl_from_IActivationFactory( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    TRACE( "iface %p, ref %lu.\n", iface, ref );
    return ref;
}

static HRESULT WINAPI activation_factory_GetIids( IActivationFactory *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI activation_factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *class_name )
{
    FIXME( "iface %p, class_name %p stub!\n", iface, class_name );
    return E_NOTIMPL;
}

static HRESULT WINAPI activation_factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI activation_factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    FIXME( "iface %p, instance %p stub!\n", iface, instance );
    return E_NOTIMPL;
}

static const struct IActivationFactoryVtbl activation_factory_vtbl =
{
    activation_factory_QueryInterface,
    activation_factory_AddRef,
    activation_factory_Release,
    /* IInspectable methods */
    activation_factory_GetIids,
    activation_factory_GetRuntimeClassName,
    activation_factory_GetTrustLevel,
    /* IActivationFactory methods */
    activation_factory_ActivateInstance,
};

/* ICoreApplicationView — a per-process application view (minimal). */

static const GUID IID_ICoreApplicationView2_custom = {0x68eb7adf, 0x917f, 0x4885, {0x9e, 0x78, 0x81, 0xb3, 0x7b, 0x37, 0xf4, 0x60}};
static const GUID IID_ICoreApplicationView3_custom = {0x07ebe1b3, 0xa4cf, 0x4550, {0xab, 0x70, 0xb0, 0x7e, 0x85, 0x33, 0x0b, 0xc8}};
static const GUID IID_ICoreApplicationViewTitleBar_custom = {0x08d17364, 0x4e43, 0x40e2, {0xa4, 0x70, 0x36, 0x64, 0xf0, 0xe0, 0xb9, 0x6c}};

#define TB_EVENT __FITypedEventHandler_2_Windows__CApplicationModel__CCore__CCoreApplicationViewTitleBar_IInspectable

struct tb_handler_entry
{
    EventRegistrationToken token;
    TB_EVENT *handler;
};

struct tb_event_list
{
    CRITICAL_SECTION cs;
    struct tb_handler_entry *entries;
    ULONG count;
    ULONG size;
};

struct core_application_view_title_bar
{
    ICoreApplicationViewTitleBar ICoreApplicationViewTitleBar_iface;
    const void *IWeakReferenceSource_iface;
    LONG ref;
    boolean extend_view_into_title_bar;
    boolean is_visible;
    DOUBLE height;
    HWND hwnd;
    struct tb_event_list layout_metrics_changed;
    struct tb_event_list is_visible_changed;
};

static HRESULT core_application_view_title_bar_create( ICoreApplicationViewTitleBar **value );

static const GUID IID_ICoreApplicationView6_custom =
    { 0x2bc095a8, 0x8ef0, 0x446d, { 0x9e, 0x60, 0x3a, 0x3e, 0x04, 0x28, 0xc6, 0x71 } };

struct core_application_view
{
    ICoreApplicationView ICoreApplicationView_iface;
    ICoreApplicationView2 ICoreApplicationView2_iface;
    ICoreApplicationView3 ICoreApplicationView3_iface;
    IInspectable ICoreApplicationView6_iface;
    LONG ref;
    ICoreApplicationViewTitleBar *title_bar;
    __FIMap_2_HSTRING_IInspectable *properties;
};

static inline struct core_application_view *impl_from_ICoreApplicationView( ICoreApplicationView *iface )
{
    return CONTAINING_RECORD( iface, struct core_application_view, ICoreApplicationView_iface );
}

static inline struct core_application_view *impl_from_ICoreApplicationView2( ICoreApplicationView2 *iface )
{
    return CONTAINING_RECORD( iface, struct core_application_view, ICoreApplicationView2_iface );
}

static inline struct core_application_view *impl_from_ICoreApplicationView3( ICoreApplicationView3 *iface )
{
    return CONTAINING_RECORD( iface, struct core_application_view, ICoreApplicationView3_iface );
}

static inline struct core_application_view *impl_from_ICoreApplicationView6( IInspectable *iface )
{
    return CONTAINING_RECORD( iface, struct core_application_view, ICoreApplicationView6_iface );
}

static HRESULT WINAPI core_application_view_QueryInterface( ICoreApplicationView *iface, REFIID iid, void **out )
{
    struct core_application_view *impl = impl_from_ICoreApplicationView( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_ICoreApplicationView ))
    {
        ICoreApplicationView_AddRef( &impl->ICoreApplicationView_iface );
        *out = &impl->ICoreApplicationView_iface;
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_ICoreApplicationView2 ) ||
        IsEqualGUID( iid, &IID_ICoreApplicationView2_custom ))
    {
        ICoreApplicationView_AddRef( &impl->ICoreApplicationView_iface );
        *out = &impl->ICoreApplicationView2_iface;
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_ICoreApplicationView3 ) ||
        IsEqualGUID( iid, &IID_ICoreApplicationView3_custom ))
    {
        ICoreApplicationView_AddRef( &impl->ICoreApplicationView_iface );
        *out = &impl->ICoreApplicationView3_iface;
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_ICoreApplicationView6_custom ))
    {
        ICoreApplicationView_AddRef( &impl->ICoreApplicationView_iface );
        *out = &impl->ICoreApplicationView6_iface;
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI core_application_view_AddRef( ICoreApplicationView *iface )
{
    struct core_application_view *impl = impl_from_ICoreApplicationView( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI core_application_view_Release( ICoreApplicationView *iface )
{
    struct core_application_view *impl = impl_from_ICoreApplicationView( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    TRACE( "iface %p, ref %lu.\n", iface, ref );
    if (!ref)
    {
        if (impl->title_bar)
            ICoreApplicationViewTitleBar_Release( impl->title_bar );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI core_application_view_GetIids( ICoreApplicationView *iface, ULONG *count, IID **iids )
{
    FIXME( "iface %p stub!\n", iface );
    return E_NOTIMPL;
}
static HRESULT WINAPI core_application_view_GetRuntimeClassName( ICoreApplicationView *iface, HSTRING *name )
{
    if (!name) return E_POINTER;
    return WindowsCreateString( RuntimeClass_Windows_ApplicationModel_Core_CoreApplicationView,
                                wcslen( RuntimeClass_Windows_ApplicationModel_Core_CoreApplicationView ), name );
}
static HRESULT WINAPI core_application_view_GetTrustLevel( ICoreApplicationView *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}
static __x_ABI_CWindows_CUI_CCore_CICoreWindow *cached_main_core_window;

static HRESULT WINAPI core_application_view_get_CoreWindow( ICoreApplicationView *iface, __x_ABI_CWindows_CUI_CCore_CICoreWindow **value )
{
    IActivationFactory *cwf = NULL;
    __x_ABI_CWindows_CUI_CCore_CICoreWindowStatic *cws = NULL;
    HSTRING class_id = NULL;
    HRESULT hr = S_OK;

    if (!value) return E_POINTER;
    *value = NULL;

    WindowsCreateString( RuntimeClass_Windows_UI_Core_CoreWindow,
                         wcslen( RuntimeClass_Windows_UI_Core_CoreWindow ), &class_id );
    if (SUCCEEDED(RoGetActivationFactory( class_id, &IID_IActivationFactory, (void **)&cwf )) && cwf)
    {
        IActivationFactory_QueryInterface( cwf, &IID___x_ABI_CWindows_CUI_CCore_CICoreWindowStatic,
                                           (void **)&cws );
        if (cws)
        {
            hr = __x_ABI_CWindows_CUI_CCore_CICoreWindowStatic_GetForCurrentThread( cws, value );
            __x_ABI_CWindows_CUI_CCore_CICoreWindowStatic_Release( cws );
        }
        IActivationFactory_Release( cwf );
    }
    WindowsDeleteString( class_id );
    if (*value)
    {
        if (!cached_main_core_window)
        {
            __x_ABI_CWindows_CUI_CCore_CICoreWindow_AddRef( *value );
            if (InterlockedCompareExchangePointer( (void *volatile *)&cached_main_core_window, *value, NULL ))
                __x_ABI_CWindows_CUI_CCore_CICoreWindow_Release( *value );
        }
    }
    else if (cached_main_core_window)
    {
        *value = cached_main_core_window;
        __x_ABI_CWindows_CUI_CCore_CICoreWindow_AddRef( *value );
        hr = S_OK;
    }
    return hr;
}
static const GUID IID_ILaunchActivatedEventArgs = {0xfbc93e26, 0xa14a, 0x4b4f, {0x82, 0xb0, 0x33, 0xbe, 0xd9, 0x20, 0xaf, 0x52}};
static const GUID IID_ILaunchActivatedEventArgs_WinUI = {0xd505cea9, 0x1bcb, 0x5b29, {0xa8, 0xbe, 0x94, 0x4e, 0x00, 0xf0, 0x6f, 0x78}};
static const GUID IID_IActivatedEventArgsWithUser = {0x1cf09b9e, 0x9962, 0x4936, {0x80, 0xff, 0xaf, 0xc8, 0xe8, 0xae, 0x5c, 0x8c}};
static const GUID IID_ILaunchActivatedEventArgs2 = {0x0fd37ebc, 0x9dc9, 0x46b5, {0x9a, 0xce, 0xbd, 0x95, 0xd4, 0x56, 0x53, 0x45}};
static const GUID IID_IPrelaunchActivatedEventArgs = {0x0c44717b, 0x19f7, 0x48d6, {0xb0, 0x46, 0xcf, 0x22, 0x82, 0x6e, 0xaa, 0x74}};
static const GUID IID_IApplicationViewActivatedEventArgs = {0x930cef4b, 0xb829, 0x40fc, {0x88, 0xf4, 0x85, 0x13, 0xe8, 0xa6, 0x47, 0x38}};
static const GUID IID_IViewSwitcherProvider = {0x33f288a6, 0x5c2c, 0x4d27, {0xba, 0xc7, 0x75, 0x36, 0x08, 0x8f, 0x12, 0x19}};
static const GUID IID_IUserStandIn = {0xdf9a26c6, 0xe746, 0x4bcd, {0xb5, 0xd4, 0x12, 0x01, 0x03, 0xc4, 0x20, 0x9b}};
static const GUID IID_ISplashScreen = {0xca4d975c, 0xd4d6, 0x43f0, {0x97, 0xc0, 0x08, 0x33, 0xc6, 0x39, 0x1c, 0x24}};
/* Windows.ApplicationModel.Activation.IProtocolActivatedEventArgs */
static const GUID IID_IProtocolActivatedEventArgs = {0x6095f4dd, 0xb7c0, 0x46ab, {0x81, 0xfe, 0xd9, 0x0f, 0x36, 0xd0, 0x0d, 0x24}};

struct activated_event_args
{
    __x_ABI_CWindows_CApplicationModel_CActivation_CIActivatedEventArgs IActivatedEventArgs_iface;
    const void *ILaunchActivatedEventArgs_vtbl;
    const void *ILaunchActivatedEventArgs2_vtbl;
    const void *IPrelaunchActivatedEventArgs_vtbl;
    const void *IActivatedEventArgsWithUser_vtbl;
    const void *IApplicationViewActivatedEventArgs_vtbl;
    const void *IViewSwitcherProvider_vtbl;
    const void *IProtocolActivatedEventArgs_vtbl;
    void *splash;
    LONG ref;
    HSTRING uri; /* set for ActivationKind_Protocol; source for IProtocolActivatedEventArgs::Uri */
    __x_ABI_CWindows_CApplicationModel_CActivation_CActivationKind kind;
    __x_ABI_CWindows_CApplicationModel_CActivation_CApplicationExecutionState prev_state;
};

static inline struct activated_event_args *impl_from_IActivatedEventArgs( __x_ABI_CWindows_CApplicationModel_CActivation_CIActivatedEventArgs *iface )
{
    return CONTAINING_RECORD( iface, struct activated_event_args, IActivatedEventArgs_iface );
}

static inline struct activated_event_args *impl_from_ILaunchActivatedEventArgs( void *iface )
{
    return CONTAINING_RECORD( iface, struct activated_event_args, ILaunchActivatedEventArgs_vtbl );
}

static inline struct activated_event_args *impl_from_ILaunchActivatedEventArgs2( void *iface )
{
    return CONTAINING_RECORD( iface, struct activated_event_args, ILaunchActivatedEventArgs2_vtbl );
}

static inline struct activated_event_args *impl_from_IPrelaunchActivatedEventArgs( void *iface )
{
    return CONTAINING_RECORD( iface, struct activated_event_args, IPrelaunchActivatedEventArgs_vtbl );
}

static inline struct activated_event_args *impl_from_IActivatedEventArgsWithUser( void *iface )
{
    return CONTAINING_RECORD( iface, struct activated_event_args, IActivatedEventArgsWithUser_vtbl );
}

static inline struct activated_event_args *impl_from_IApplicationViewActivatedEventArgs( void *iface )
{
    return CONTAINING_RECORD( iface, struct activated_event_args, IApplicationViewActivatedEventArgs_vtbl );
}

static inline struct activated_event_args *impl_from_IViewSwitcherProvider( void *iface )
{
    return CONTAINING_RECORD( iface, struct activated_event_args, IViewSwitcherProvider_vtbl );
}

static inline struct activated_event_args *impl_from_IProtocolActivatedEventArgs( void *iface )
{
    return CONTAINING_RECORD( iface, struct activated_event_args, IProtocolActivatedEventArgs_vtbl );
}

static ULONG WINAPI splash_AddRef( void *iface );
static ULONG WINAPI splash_Release( void *iface );

static HRESULT WINAPI activated_event_args_QueryInterface( __x_ABI_CWindows_CApplicationModel_CActivation_CIActivatedEventArgs *iface, REFIID iid, void **out )
{
    struct activated_event_args *impl = impl_from_IActivatedEventArgs( iface );
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID___x_ABI_CWindows_CApplicationModel_CActivation_CIActivatedEventArgs ))
    {
        InterlockedIncrement( &impl->ref );
        *out = &impl->IActivatedEventArgs_iface;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_ILaunchActivatedEventArgs ) ||
        IsEqualGUID( iid, &IID_ILaunchActivatedEventArgs_WinUI ))
    {
        InterlockedIncrement( &impl->ref );
        *out = &impl->ILaunchActivatedEventArgs_vtbl;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IActivatedEventArgsWithUser ))
    {
        InterlockedIncrement( &impl->ref );
        *out = &impl->IActivatedEventArgsWithUser_vtbl;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IPrelaunchActivatedEventArgs ))
    {
        InterlockedIncrement( &impl->ref );
        *out = &impl->IPrelaunchActivatedEventArgs_vtbl;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_ILaunchActivatedEventArgs2 ))
    {
        InterlockedIncrement( &impl->ref );
        *out = &impl->ILaunchActivatedEventArgs2_vtbl;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IApplicationViewActivatedEventArgs ))
    {
        InterlockedIncrement( &impl->ref );
        *out = &impl->IApplicationViewActivatedEventArgs_vtbl;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IViewSwitcherProvider ))
    {
        InterlockedIncrement( &impl->ref );
        *out = &impl->IViewSwitcherProvider_vtbl;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IProtocolActivatedEventArgs ))
    {
        InterlockedIncrement( &impl->ref );
        *out = &impl->IProtocolActivatedEventArgs_vtbl;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_ISplashScreen ))
    {
        if (impl->splash)
        {
            splash_AddRef( impl->splash );
            *out = impl->splash;
            return S_OK;
        }
    }
    ERR( "activated_event_args: %s not implemented.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI activated_event_args_AddRef( __x_ABI_CWindows_CApplicationModel_CActivation_CIActivatedEventArgs *iface )
{
    struct activated_event_args *impl = impl_from_IActivatedEventArgs( iface );
    return InterlockedIncrement( &impl->ref );
}
static ULONG WINAPI activated_event_args_Release( __x_ABI_CWindows_CApplicationModel_CActivation_CIActivatedEventArgs *iface )
{
    struct activated_event_args *impl = impl_from_IActivatedEventArgs( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        if (impl->splash) splash_Release( impl->splash );
        WindowsDeleteString( impl->uri );
        free( impl );
    }
    return ref;
}
static HRESULT WINAPI activated_event_args_GetIids( __x_ABI_CWindows_CApplicationModel_CActivation_CIActivatedEventArgs *iface, ULONG *count, IID **iids )
{ FIXME( "stub\n" ); return E_NOTIMPL; }
static HRESULT WINAPI activated_event_args_GetRuntimeClassName( __x_ABI_CWindows_CApplicationModel_CActivation_CIActivatedEventArgs *iface, HSTRING *name )
{
    static const WCHAR launch_class[] = L"Windows.ApplicationModel.Activation.LaunchActivatedEventArgs";
    static const WCHAR protocol_class[] = L"Windows.ApplicationModel.Activation.ProtocolActivatedEventArgs";
    const WCHAR *str;
    UINT32 len;

    if (!name) return E_POINTER;
    if (impl_from_IActivatedEventArgs( iface )->uri)
    {
        str = protocol_class;
        len = ARRAY_SIZE( protocol_class ) - 1;
    }
    else
    {
        str = launch_class;
        len = 59;
    }
    return WindowsCreateString( str, len, name );
}
static HRESULT WINAPI activated_event_args_GetTrustLevel( __x_ABI_CWindows_CApplicationModel_CActivation_CIActivatedEventArgs *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}
static HRESULT WINAPI activated_event_args_get_Kind( __x_ABI_CWindows_CApplicationModel_CActivation_CIActivatedEventArgs *iface,
        __x_ABI_CWindows_CApplicationModel_CActivation_CActivationKind *value )
{
    struct activated_event_args *impl = impl_from_IActivatedEventArgs( iface );
    if (!value) return E_POINTER;
    *value = impl->kind;
    return S_OK;
}
static HRESULT WINAPI activated_event_args_get_PreviousExecutionState( __x_ABI_CWindows_CApplicationModel_CActivation_CIActivatedEventArgs *iface,
        __x_ABI_CWindows_CApplicationModel_CActivation_CApplicationExecutionState *value )
{
    struct activated_event_args *impl = impl_from_IActivatedEventArgs( iface );
    if (!value) return E_POINTER;
    *value = impl->prev_state;
    return S_OK;
}
struct splash_handler_vtbl
{
    HRESULT (WINAPI *QueryInterface)( void *, REFIID, void ** );
    ULONG (WINAPI *AddRef)( void * );
    ULONG (WINAPI *Release)( void * );
    HRESULT (WINAPI *Invoke)( void *, void *sender, void *args );
};
struct splash_handler { const struct splash_handler_vtbl *lpVtbl; };

struct splash_screen
{
    const void **vtbl;
    LONG refcount;
    struct splash_handler *dismissed;
    LONG fired;
};

static ULONG WINAPI splash_AddRef( void *iface );
static ULONG WINAPI splash_Release( void *iface );

static HRESULT WINAPI splash_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct splash_screen *splash = CONTAINING_RECORD( iface, struct splash_screen, vtbl );
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_ISplashScreen ))
    {
        *out = &splash->vtbl;
        InterlockedIncrement( &splash->refcount );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI splash_AddRef( void *iface )
{
    struct splash_screen *splash = CONTAINING_RECORD( iface, struct splash_screen, vtbl );
    return InterlockedIncrement( &splash->refcount );
}
static ULONG WINAPI splash_Release( void *iface )
{
    struct splash_screen *splash = CONTAINING_RECORD( iface, struct splash_screen, vtbl );
    ULONG ref = InterlockedDecrement( &splash->refcount );
    if (!ref)
    {
        if (splash->dismissed) splash->dismissed->lpVtbl->Release( splash->dismissed );
        free( splash );
    }
    return ref;
}
static HRESULT WINAPI splash_GetIids( void *iface, ULONG *count, IID **iids ) { *count = 0; *iids = NULL; return S_OK; }
static HRESULT WINAPI splash_GetRuntimeClassName( void *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.ApplicationModel.Activation.SplashScreen", 47, name );
}
static HRESULT WINAPI splash_GetTrustLevel( void *iface, TrustLevel *level ) { *level = BaseTrust; return S_OK; }
/* Primary-monitor work area (device pixels), cached. Replaces the hardcoded
 * 1024x768 splash rect; mirrors windows.ui's corewindow.c work-area helper. */
static void twinapi_get_work_area( SIZE *size )
{
    static SIZE cached;
    MONITORINFO mi = { sizeof(mi) };
    HMONITOR monitor;
    RECT rect;
    UINT dpi;

    if (cached.cx && cached.cy)
    {
        *size = cached;
        return;
    }
    if (SystemParametersInfoW( SPI_GETWORKAREA, 0, &rect, 0 )
        && rect.right > rect.left && rect.bottom > rect.top)
    {
        cached.cx = rect.right - rect.left;
        cached.cy = rect.bottom - rect.top;
    }
    else if ((monitor = MonitorFromWindow( GetDesktopWindow(), MONITOR_DEFAULTTOPRIMARY ))
             && GetMonitorInfoW( monitor, &mi )
             && mi.rcWork.right > mi.rcWork.left && mi.rcWork.bottom > mi.rcWork.top)
    {
        cached.cx = mi.rcWork.right - mi.rcWork.left;
        cached.cy = mi.rcWork.bottom - mi.rcWork.top;
    }
    else
    {
        cached.cx = 1024;
        cached.cy = 768;
        if ((dpi = GetDpiForSystem()))
        {
            cached.cx = MulDiv( cached.cx, dpi, 96 );
            cached.cy = MulDiv( cached.cy, dpi, 96 );
        }
    }
    TRACE( "work area %lux%lu\n", cached.cx, cached.cy );
    *size = cached;
}

static HRESULT WINAPI splash_get_ImageLocation( void *iface, Rect *rect )
{
    SIZE work_area;

    if (!rect) return E_POINTER;
    twinapi_get_work_area( &work_area );
    rect->X = rect->Y = 0.0f;
    rect->Width = (float)work_area.cx;
    rect->Height = (float)work_area.cy;
    return S_OK;
}
static DWORD WINAPI splash_dismiss_thread( void *arg )
{
    struct splash_screen *splash = arg;
    struct splash_handler *handler;

    Sleep( 100 );
    handler = splash->dismissed;
    if (handler && !InterlockedExchange( &splash->fired, 1 ))
    {
        HRESULT hr;
        TRACE( "raising SplashScreen.Dismissed\n" );
        hr = handler->lpVtbl->Invoke( handler, &splash->vtbl, NULL );
        if (FAILED( hr )) WARN( "Dismissed handler returned %#lx\n", hr );
    }
    splash_Release( &splash->vtbl );
    return 0;
}
static HRESULT WINAPI splash_add_Dismissed( void *iface, void *handler, UINT64 *token )
{
    struct splash_screen *splash = CONTAINING_RECORD( iface, struct splash_screen, vtbl );
    HANDLE thread;

    TRACE( "splash %p, handler %p\n", splash, handler );

    if (splash->dismissed) splash->dismissed->lpVtbl->Release( splash->dismissed );
    if ((splash->dismissed = handler)) splash->dismissed->lpVtbl->AddRef( splash->dismissed );

    if (splash->dismissed)
    {
        InterlockedIncrement( &splash->refcount );
        if ((thread = CreateThread( NULL, 0, splash_dismiss_thread, splash, 0, NULL )))
            CloseHandle( thread );
        else
            splash_Release( &splash->vtbl );
    }
    if (token) *token = 1;
    return S_OK;
}
static HRESULT WINAPI splash_remove_Dismissed( void *iface, UINT64 token )
{
    struct splash_screen *splash = CONTAINING_RECORD( iface, struct splash_screen, vtbl );
    if (splash->dismissed) splash->dismissed->lpVtbl->Release( splash->dismissed );
    splash->dismissed = NULL;
    return S_OK;
}
static const void *splash_vtbl[] =
{
    splash_QueryInterface, splash_AddRef, splash_Release,
    splash_GetIids, splash_GetRuntimeClassName, splash_GetTrustLevel,
    splash_get_ImageLocation, splash_add_Dismissed, splash_remove_Dismissed,
};
static void *create_splash_screen( void )
{
    struct splash_screen *splash;
    if (!(splash = calloc( 1, sizeof(*splash) ))) return NULL;
    splash->vtbl = splash_vtbl;
    splash->refcount = 1;
    return &splash->vtbl;
}

static HRESULT WINAPI activated_event_args_get_SplashScreen( __x_ABI_CWindows_CApplicationModel_CActivation_CIActivatedEventArgs *iface,
        __x_ABI_CWindows_CApplicationModel_CActivation_CISplashScreen **value )
{
    struct activated_event_args *impl = impl_from_IActivatedEventArgs( iface );
    if (!value) return E_POINTER;
    if (impl->splash)
    {
        splash_AddRef( impl->splash );
        *value = (__x_ABI_CWindows_CApplicationModel_CActivation_CISplashScreen *)impl->splash;
    }
    else
        *value = NULL;
    return S_OK;
}

static HRESULT WINAPI launch_activated_event_args_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct activated_event_args *impl = impl_from_ILaunchActivatedEventArgs( iface );
    return activated_event_args_QueryInterface( &impl->IActivatedEventArgs_iface, iid, out );
}
static ULONG WINAPI launch_activated_event_args_AddRef( void *iface )
{
    struct activated_event_args *impl = impl_from_ILaunchActivatedEventArgs( iface );
    return InterlockedIncrement( &impl->ref );
}
static ULONG WINAPI launch_activated_event_args_Release( void *iface )
{
    struct activated_event_args *impl = impl_from_ILaunchActivatedEventArgs( iface );
    return activated_event_args_Release( &impl->IActivatedEventArgs_iface );
}
static HRESULT WINAPI launch_activated_event_args_get_Arguments( void *iface, HSTRING *value )
{
    if (!value) return E_POINTER;
    return WindowsCreateString( L"", 0, value );
}
static HRESULT WINAPI launch_activated_event_args_get_TileId( void *iface, HSTRING *value )
{
    if (!value) return E_POINTER;
    return WindowsCreateString( L"App", 3, value );
}

static const void *launch_activated_event_args_vtbl_array[] =
{
    launch_activated_event_args_QueryInterface,
    launch_activated_event_args_AddRef,
    launch_activated_event_args_Release,
    activated_event_args_GetIids,
    activated_event_args_GetRuntimeClassName,
    activated_event_args_GetTrustLevel,
    launch_activated_event_args_get_Arguments,
    launch_activated_event_args_get_TileId,
};

static HRESULT WINAPI user_QueryInterface( void *iface, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IUserStandIn ))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI user_AddRef( void *iface ) { return 2; }
static ULONG WINAPI user_Release( void *iface ) { return 1; }
static HRESULT WINAPI user_GetIids( void *iface, ULONG *count, IID **iids ) { *count = 0; *iids = NULL; return S_OK; }
static HRESULT WINAPI user_GetRuntimeClassName( void *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.System.User", 19, name );
}
static HRESULT WINAPI user_GetTrustLevel( void *iface, TrustLevel *level ) { *level = BaseTrust; return S_OK; }
static HRESULT WINAPI user_get_NonRoamableId( void *iface, HSTRING *value )
{
    return WindowsCreateString( L"wine-uwp-local-user", 19, value );
}
static HRESULT WINAPI user_get_AuthenticationStatus( void *iface, INT32 *value ) { *value = 1; return S_OK; }
static HRESULT WINAPI user_get_Type( void *iface, INT32 *value ) { *value = 0; return S_OK; }
static HRESULT WINAPI user_stub( void *iface ) { return S_OK; }

static const void *user_vtbl_array[] =
{
    user_QueryInterface, user_AddRef, user_Release,
    user_GetIids, user_GetRuntimeClassName, user_GetTrustLevel,
    user_get_NonRoamableId, user_get_AuthenticationStatus, user_get_Type,
    user_stub, user_stub, user_stub,
};
static const void *user_instance = user_vtbl_array;

static HRESULT WINAPI withuser_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct activated_event_args *impl = impl_from_IActivatedEventArgsWithUser( iface );
    return activated_event_args_QueryInterface( &impl->IActivatedEventArgs_iface, iid, out );
}
static ULONG WINAPI withuser_AddRef( void *iface )
{
    struct activated_event_args *impl = impl_from_IActivatedEventArgsWithUser( iface );
    return InterlockedIncrement( &impl->ref );
}
static ULONG WINAPI withuser_Release( void *iface )
{
    struct activated_event_args *impl = impl_from_IActivatedEventArgsWithUser( iface );
    return activated_event_args_Release( &impl->IActivatedEventArgs_iface );
}
static HRESULT WINAPI withuser_get_User( void *iface, void **out )
{
    if (!out) return E_POINTER;
    *out = (void *)&user_instance;
    return S_OK;
}
static const void *withuser_vtbl_array[] =
{
    withuser_QueryInterface, withuser_AddRef, withuser_Release,
    activated_event_args_GetIids, activated_event_args_GetRuntimeClassName, activated_event_args_GetTrustLevel,
    withuser_get_User,
};

static HRESULT WINAPI prelaunch_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct activated_event_args *impl = impl_from_IPrelaunchActivatedEventArgs( iface );
    return activated_event_args_QueryInterface( &impl->IActivatedEventArgs_iface, iid, out );
}
static ULONG WINAPI prelaunch_AddRef( void *iface )
{
    struct activated_event_args *impl = impl_from_IPrelaunchActivatedEventArgs( iface );
    return InterlockedIncrement( &impl->ref );
}
static ULONG WINAPI prelaunch_Release( void *iface )
{
    struct activated_event_args *impl = impl_from_IPrelaunchActivatedEventArgs( iface );
    return activated_event_args_Release( &impl->IActivatedEventArgs_iface );
}
static HRESULT WINAPI prelaunch_get_PrelaunchActivated( void *iface, boolean *value )
{
    if (!value) return E_POINTER;
    *value = FALSE;
    return S_OK;
}
static const void *prelaunch_vtbl_array[] =
{
    prelaunch_QueryInterface, prelaunch_AddRef, prelaunch_Release,
    activated_event_args_GetIids, activated_event_args_GetRuntimeClassName, activated_event_args_GetTrustLevel,
    prelaunch_get_PrelaunchActivated,
};

static HRESULT WINAPI launch2_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct activated_event_args *impl = impl_from_ILaunchActivatedEventArgs2( iface );
    return activated_event_args_QueryInterface( &impl->IActivatedEventArgs_iface, iid, out );
}
static ULONG WINAPI launch2_AddRef( void *iface )
{
    struct activated_event_args *impl = impl_from_ILaunchActivatedEventArgs2( iface );
    return InterlockedIncrement( &impl->ref );
}
static ULONG WINAPI launch2_Release( void *iface )
{
    struct activated_event_args *impl = impl_from_ILaunchActivatedEventArgs2( iface );
    return activated_event_args_Release( &impl->IActivatedEventArgs_iface );
}
static HRESULT WINAPI launch2_get_TileActivatedInfo( void *iface, void **info )
{
    if (!info) return E_POINTER;
    *info = NULL;
    return S_OK;
}
static const void *launch2_vtbl_array[] =
{
    launch2_QueryInterface, launch2_AddRef, launch2_Release,
    activated_event_args_GetIids, activated_event_args_GetRuntimeClassName, activated_event_args_GetTrustLevel,
    launch2_get_TileActivatedInfo,
};

static HRESULT WINAPI appview_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct activated_event_args *impl = impl_from_IApplicationViewActivatedEventArgs( iface );
    return activated_event_args_QueryInterface( &impl->IActivatedEventArgs_iface, iid, out );
}
static ULONG WINAPI appview_AddRef( void *iface )
{
    struct activated_event_args *impl = impl_from_IApplicationViewActivatedEventArgs( iface );
    return InterlockedIncrement( &impl->ref );
}
static ULONG WINAPI appview_Release( void *iface )
{
    struct activated_event_args *impl = impl_from_IApplicationViewActivatedEventArgs( iface );
    return activated_event_args_Release( &impl->IActivatedEventArgs_iface );
}
static HRESULT WINAPI appview_get_CurrentlyShownApplicationViewId( void *iface, INT32 *id )
{
    if (!id) return E_POINTER;
    *id = 0;
    return S_OK;
}
static const void *appview_vtbl_array[] =
{
    appview_QueryInterface, appview_AddRef, appview_Release,
    activated_event_args_GetIids, activated_event_args_GetRuntimeClassName, activated_event_args_GetTrustLevel,
    appview_get_CurrentlyShownApplicationViewId,
};

static HRESULT WINAPI viewswitch_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct activated_event_args *impl = impl_from_IViewSwitcherProvider( iface );
    return activated_event_args_QueryInterface( &impl->IActivatedEventArgs_iface, iid, out );
}
static ULONG WINAPI viewswitch_AddRef( void *iface )
{
    struct activated_event_args *impl = impl_from_IViewSwitcherProvider( iface );
    return InterlockedIncrement( &impl->ref );
}
static ULONG WINAPI viewswitch_Release( void *iface )
{
    struct activated_event_args *impl = impl_from_IViewSwitcherProvider( iface );
    return activated_event_args_Release( &impl->IActivatedEventArgs_iface );
}
static HRESULT WINAPI viewswitch_get_ViewSwitcher( void *iface, void **switcher )
{
    if (!switcher) return E_POINTER;
    *switcher = NULL;
    return S_OK;
}
static const void *viewswitch_vtbl_array[] =
{
    viewswitch_QueryInterface, viewswitch_AddRef, viewswitch_Release,
    activated_event_args_GetIids, activated_event_args_GetRuntimeClassName, activated_event_args_GetTrustLevel,
    viewswitch_get_ViewSwitcher,
};

/* IProtocolActivatedEventArgs: a thin face over activated_event_args that hands
 * back the launch URI as a Windows.Foundation.Uri. The Uri object is built
 * lazily through the WinRT Uri activation factory (CreateUri) so callers get a
 * real IUriRuntimeClass, not a stub. */
static HRESULT WINAPI protocol_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct activated_event_args *impl = impl_from_IProtocolActivatedEventArgs( iface );
    return activated_event_args_QueryInterface( &impl->IActivatedEventArgs_iface, iid, out );
}
static ULONG WINAPI protocol_AddRef( void *iface )
{
    struct activated_event_args *impl = impl_from_IProtocolActivatedEventArgs( iface );
    return InterlockedIncrement( &impl->ref );
}
static ULONG WINAPI protocol_Release( void *iface )
{
    struct activated_event_args *impl = impl_from_IProtocolActivatedEventArgs( iface );
    return activated_event_args_Release( &impl->IActivatedEventArgs_iface );
}
static HRESULT WINAPI protocol_get_Uri( void *iface, __x_ABI_CWindows_CFoundation_CIUriRuntimeClass **value )
{
    struct activated_event_args *impl = impl_from_IProtocolActivatedEventArgs( iface );
    __x_ABI_CWindows_CFoundation_CIUriRuntimeClassFactory *factory = NULL;
    HSTRING class_id = NULL;
    HRESULT hr = S_OK;

    TRACE( "iface %p, value %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = NULL;
    if (!impl->uri) return S_OK;

    WindowsCreateString( RuntimeClass_Windows_Foundation_Uri, wcslen( RuntimeClass_Windows_Foundation_Uri ), &class_id );
    hr = RoGetActivationFactory( class_id, &IID_IUriRuntimeClassFactory, (void **)&factory );
    if (SUCCEEDED( hr ) && factory)
    {
        hr = IUriRuntimeClassFactory_CreateUri( factory, impl->uri, value );
        IUriRuntimeClassFactory_Release( factory );
    }
    WindowsDeleteString( class_id );
    return hr;
}
static const void *protocol_vtbl_array[] =
{
    protocol_QueryInterface, protocol_AddRef, protocol_Release,
    activated_event_args_GetIids, activated_event_args_GetRuntimeClassName, activated_event_args_GetTrustLevel,
    protocol_get_Uri,
};

static const struct __x_ABI_CWindows_CApplicationModel_CActivation_CIActivatedEventArgsVtbl activated_event_args_vtbl =
{
    activated_event_args_QueryInterface,
    activated_event_args_AddRef,
    activated_event_args_Release,
    activated_event_args_GetIids,
    activated_event_args_GetRuntimeClassName,
    activated_event_args_GetTrustLevel,
    activated_event_args_get_Kind,
    activated_event_args_get_PreviousExecutionState,
    activated_event_args_get_SplashScreen,
};

static __x_ABI_CWindows_CApplicationModel_CActivation_CIActivatedEventArgs *activated_event_args_create( __x_ABI_CWindows_CApplicationModel_CActivation_CActivationKind kind )
{
    struct activated_event_args *impl;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return NULL;
    impl->IActivatedEventArgs_iface.lpVtbl = &activated_event_args_vtbl;
    impl->ILaunchActivatedEventArgs_vtbl = launch_activated_event_args_vtbl_array;
    impl->ILaunchActivatedEventArgs2_vtbl = launch2_vtbl_array;
    impl->IPrelaunchActivatedEventArgs_vtbl = prelaunch_vtbl_array;
    impl->IActivatedEventArgsWithUser_vtbl = withuser_vtbl_array;
    impl->IApplicationViewActivatedEventArgs_vtbl = appview_vtbl_array;
    impl->IViewSwitcherProvider_vtbl = viewswitch_vtbl_array;
    impl->IProtocolActivatedEventArgs_vtbl = protocol_vtbl_array;
    impl->splash = create_splash_screen();
    impl->ref = 1;
    impl->kind = kind;
    return &impl->IActivatedEventArgs_iface;
}

/* ProtocolActivatedEventArgs for an AppUri activation (AppInstance redirect /
 * browser protocol launch): kind 4 (ActivationKind_Protocol) with the URI kept
 * for IProtocolActivatedEventArgs::Uri. */
static __x_ABI_CWindows_CApplicationModel_CActivation_CIActivatedEventArgs *activated_event_args_create_protocol( const WCHAR *uri )
{
    __x_ABI_CWindows_CApplicationModel_CActivation_CIActivatedEventArgs *args;

    if (!(args = activated_event_args_create( 4 /* ActivationKind_Protocol */ ))) return NULL;
    if (uri) WindowsCreateString( uri, wcslen( uri ), &impl_from_IActivatedEventArgs( args )->uri );
    return args;
}

static __FITypedEventHandler_2_Windows__CApplicationModel__CCore__CCoreApplicationView_Windows__CApplicationModel__CActivation__CIActivatedEventArgs *view_activated_handler;

static HRESULT WINAPI core_application_view_add_Activated( ICoreApplicationView *iface,
        __FITypedEventHandler_2_Windows__CApplicationModel__CCore__CCoreApplicationView_Windows__CApplicationModel__CActivation__CIActivatedEventArgs *handler, EventRegistrationToken *token )
{
    TRACE( "iface %p, handler %p, token %p\n", iface, handler, token );
    if (token) token->value = 1;
    if (handler)
    {
        /* registration only: the launch activation is driven by the app's
         * own lifecycle (XAML fires OnLaunched once the CoreUI window and
         * its conversation exist), which delivers it through this handler
         * on the view's thread */
        __FITypedEventHandler_2_Windows__CApplicationModel__CCore__CCoreApplicationView_Windows__CApplicationModel__CActivation__CIActivatedEventArgs_AddRef( handler );
        view_activated_handler = handler;
    }
    return S_OK;
}
static HRESULT WINAPI core_application_view_remove_Activated( ICoreApplicationView *iface, EventRegistrationToken token )
{
    FIXME( "iface %p semi-stub.\n", iface );
    return S_OK;
}
static HRESULT WINAPI core_application_view_get_IsMain( ICoreApplicationView *iface, boolean *value )
{
    FIXME( "iface %p, value %p semi-stub.\n", iface, value );
    if (value) *value = TRUE;
    return S_OK;
}
static HRESULT WINAPI core_application_view_get_IsHosted( ICoreApplicationView *iface, boolean *value )
{
    FIXME( "iface %p, value %p semi-stub.\n", iface, value );
    if (value) *value = FALSE;
    return S_OK;
}

static const struct ICoreApplicationViewVtbl core_application_view_vtbl =
{
    core_application_view_QueryInterface,
    core_application_view_AddRef,
    core_application_view_Release,
    core_application_view_GetIids,
    core_application_view_GetRuntimeClassName,
    core_application_view_GetTrustLevel,
    core_application_view_get_CoreWindow,
    core_application_view_add_Activated,
    core_application_view_remove_Activated,
    core_application_view_get_IsMain,
    core_application_view_get_IsHosted,
};

/* ICoreApplicationViewTitleBar implementation */

static inline struct core_application_view_title_bar *impl_from_ICoreApplicationViewTitleBar( ICoreApplicationViewTitleBar *iface )
{
    return CONTAINING_RECORD( iface, struct core_application_view_title_bar, ICoreApplicationViewTitleBar_iface );
}

static INT64 tb_next_token;

static void tb_event_list_init( struct tb_event_list *list )
{
    InitializeCriticalSection( &list->cs );
    list->entries = NULL;
    list->count = 0;
    list->size = 0;
}

static void tb_event_list_clear( struct tb_event_list *list )
{
    ULONG i;

    for (i = 0; i < list->count; ++i)
        if (list->entries[i].handler) __FITypedEventHandler_2_Windows__CApplicationModel__CCore__CCoreApplicationViewTitleBar_IInspectable_Release( list->entries[i].handler );
    free( list->entries );
    list->entries = NULL;
    list->count = list->size = 0;
}

static void tb_event_list_done( struct tb_event_list *list )
{
    tb_event_list_clear( list );
    DeleteCriticalSection( &list->cs );
}

static HRESULT tb_event_list_add( struct tb_event_list *list, TB_EVENT *handler, EventRegistrationToken *token )
{
    struct tb_handler_entry *entries;
    HRESULT hr = S_OK;

    TRACE( "list %p, handler %p, token %p\n", list, handler, token );

    if (!token) return E_POINTER;

    EnterCriticalSection( &list->cs );
    if (list->count == list->size)
    {
        ULONG size = list->size ? list->size * 2 : 4;
        if (!(entries = realloc( list->entries, size * sizeof(*entries) )))
        {
            hr = E_OUTOFMEMORY;
            goto done;
        }
        list->entries = entries;
        list->size = size;
    }
    if (handler) __FITypedEventHandler_2_Windows__CApplicationModel__CCore__CCoreApplicationViewTitleBar_IInspectable_AddRef( handler );
    list->entries[list->count].handler = handler;
    list->entries[list->count].token.value = InterlockedIncrement64( &tb_next_token );
    *token = list->entries[list->count].token;
    list->count++;
done:
    LeaveCriticalSection( &list->cs );
    return hr;
}

static HRESULT tb_event_list_remove( struct tb_event_list *list, EventRegistrationToken token )
{
    TB_EVENT *handler = NULL;
    ULONG i;

    TRACE( "list %p, token %s\n", list, wine_dbgstr_longlong( token.value ) );

    EnterCriticalSection( &list->cs );
    for (i = 0; i < list->count; ++i)
    {
        if (list->entries[i].token.value == token.value)
        {
            handler = list->entries[i].handler;
            list->entries[i] = list->entries[list->count - 1];
            list->count--;
            break;
        }
    }
    LeaveCriticalSection( &list->cs );

    if (handler) __FITypedEventHandler_2_Windows__CApplicationModel__CCore__CCoreApplicationViewTitleBar_IInspectable_Release( handler );
    return S_OK;
}

static void tb_event_list_raise( struct tb_event_list *list, void *sender )
{
    struct tb_handler_entry *snapshot;
    ULONG i, count;

    EnterCriticalSection( &list->cs );
    count = list->count;
    snapshot = count ? malloc( count * sizeof(*snapshot) ) : NULL;
    if (snapshot)
    {
        memcpy( snapshot, list->entries, count * sizeof(*snapshot) );
        for (i = 0; i < count; ++i)
            if (snapshot[i].handler) __FITypedEventHandler_2_Windows__CApplicationModel__CCore__CCoreApplicationViewTitleBar_IInspectable_AddRef( snapshot[i].handler );
    }
    LeaveCriticalSection( &list->cs );

    if (!snapshot) return;
    for (i = 0; i < count; ++i)
    {
        if (snapshot[i].handler)
        {
            HRESULT hr = snapshot[i].handler->lpVtbl->Invoke( snapshot[i].handler, sender, NULL );
            if (FAILED(hr)) WARN( "handler %p invoke failed, hr %#lx\n", snapshot[i].handler, hr );
            __FITypedEventHandler_2_Windows__CApplicationModel__CCore__CCoreApplicationViewTitleBar_IInspectable_Release( snapshot[i].handler );
        }
    }
    free( snapshot );
}

/* Cache the view's top-level window, mirroring application_view.c: prefer the
 * thread's active window, fall back to the foreground window. */
static HWND title_bar_get_hwnd( struct core_application_view_title_bar *impl )
{
    HWND hwnd;

    if (impl->hwnd && IsWindow( impl->hwnd )) return impl->hwnd;

    if ((hwnd = GetActiveWindow()) && IsWindow( hwnd ))
    {
        impl->hwnd = hwnd;
        return hwnd;
    }
    if ((hwnd = GetForegroundWindow()) && IsWindow( hwnd ))
    {
        impl->hwnd = hwnd;
        return hwnd;
    }
    impl->hwnd = NULL;
    return NULL;
}

/* ExtendViewIntoTitleBar maps to a DWM top-frame margin sized to the titlebar
 * height so the client area covers the caption. Resolved at runtime: dwmapi is
 * not in the module import list. */
static void title_bar_apply_dwm( struct core_application_view_title_bar *impl )
{
    static HMODULE hmodule;
    struct tb_margins { LONG cxLeftWidth, cxRightWidth, cyTopHeight, cyBottomHeight; };
    static HRESULT (WINAPI *pDwmExtendFrameIntoClientArea)( HWND, const struct tb_margins * );
    struct tb_margins margins = {0, 0, 0, 0};
    HWND hwnd;

    if (!(hwnd = title_bar_get_hwnd( impl ))) return;

    if (!hmodule)
    {
        if (!(hmodule = GetModuleHandleW( L"dwmapi.dll" )))
            hmodule = LoadLibraryW( L"dwmapi.dll" );
        if (hmodule)
            pDwmExtendFrameIntoClientArea = (void *)GetProcAddress( hmodule, "DwmExtendFrameIntoClientArea" );
    }
    if (!pDwmExtendFrameIntoClientArea) return;

    margins.cyTopHeight = impl->extend_view_into_title_bar ? (int)impl->height : 0;
    pDwmExtendFrameIntoClientArea( hwnd, &margins );
}

static const GUID IID_IWeakReference_tb = {0x00000037, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
static const GUID IID_IWeakReferenceSource_tb = {0x00000038, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};

struct tb_weakref
{
    const void *vtbl;
    LONG ref;
    struct core_application_view_title_bar *owner;
};

static HRESULT WINAPI tb_weakref_QueryInterface( void *iface, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IWeakReference_tb ))
    {
        InterlockedIncrement( &((struct tb_weakref *)iface)->ref );
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI tb_weakref_AddRef( void *iface )
{
    return InterlockedIncrement( &((struct tb_weakref *)iface)->ref );
}
static ULONG WINAPI tb_weakref_Release( void *iface )
{
    struct tb_weakref *wr = iface;
    ULONG ref = InterlockedDecrement( &wr->ref );
    if (!ref)
    {
        if (wr->owner) ICoreApplicationViewTitleBar_Release( &wr->owner->ICoreApplicationViewTitleBar_iface );
        free( wr );
    }
    return ref;
}
static HRESULT WINAPI tb_weakref_Resolve( void *iface, REFIID iid, void **out )
{
    struct tb_weakref *wr = iface;
    if (!wr->owner) { *out = NULL; return S_OK; }
    return ICoreApplicationViewTitleBar_QueryInterface( &wr->owner->ICoreApplicationViewTitleBar_iface, iid, out );
}
static const void *tb_weakref_vtbl[] =
{
    tb_weakref_QueryInterface,
    tb_weakref_AddRef,
    tb_weakref_Release,
    tb_weakref_Resolve,
};

static HRESULT WINAPI core_application_view_title_bar_QueryInterface( ICoreApplicationViewTitleBar *iface, REFIID iid, void **out );

struct tb_weakref_source
{
    const void *vtbl;
};

static HRESULT WINAPI tb_wrs_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct core_application_view_title_bar *impl =
        CONTAINING_RECORD( iface, struct core_application_view_title_bar, IWeakReferenceSource_iface );
    return core_application_view_title_bar_QueryInterface( &impl->ICoreApplicationViewTitleBar_iface, iid, out );
}
static ULONG WINAPI tb_wrs_AddRef( void *iface )
{
    struct core_application_view_title_bar *impl =
        CONTAINING_RECORD( iface, struct core_application_view_title_bar, IWeakReferenceSource_iface );
    return ICoreApplicationViewTitleBar_AddRef( &impl->ICoreApplicationViewTitleBar_iface );
}
static ULONG WINAPI tb_wrs_Release( void *iface )
{
    struct core_application_view_title_bar *impl =
        CONTAINING_RECORD( iface, struct core_application_view_title_bar, IWeakReferenceSource_iface );
    return ICoreApplicationViewTitleBar_Release( &impl->ICoreApplicationViewTitleBar_iface );
}
static HRESULT WINAPI tb_wrs_GetWeakReference( void *iface, void **ref )
{
    struct core_application_view_title_bar *impl =
        CONTAINING_RECORD( iface, struct core_application_view_title_bar, IWeakReferenceSource_iface );
    struct tb_weakref *wr;
    if (!ref) return E_POINTER;
    if (!(wr = calloc( 1, sizeof(*wr) ))) return E_OUTOFMEMORY;
    wr->vtbl = tb_weakref_vtbl;
    wr->ref = 1;
    wr->owner = impl;
    ICoreApplicationViewTitleBar_AddRef( &impl->ICoreApplicationViewTitleBar_iface );
    *ref = wr;
    return S_OK;
}
static const void *tb_wrs_vtbl[] =
{
    tb_wrs_QueryInterface,
    tb_wrs_AddRef,
    tb_wrs_Release,
    tb_wrs_GetWeakReference,
};

static HRESULT WINAPI core_application_view_title_bar_QueryInterface( ICoreApplicationViewTitleBar *iface,
        REFIID iid, void **out )
{
    struct core_application_view_title_bar *impl = impl_from_ICoreApplicationViewTitleBar( iface );

    TRACE( "iface %p, iid %s, out %p\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IWeakReferenceSource_tb ))
    {
        ICoreApplicationViewTitleBar_AddRef( &impl->ICoreApplicationViewTitleBar_iface );
        *out = &impl->IWeakReferenceSource_iface;
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_ICoreApplicationViewTitleBar ) ||
        IsEqualGUID( iid, &IID_ICoreApplicationViewTitleBar_custom ))
    {
        ICoreApplicationViewTitleBar_AddRef( &impl->ICoreApplicationViewTitleBar_iface );
        *out = &impl->ICoreApplicationViewTitleBar_iface;
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI core_application_view_title_bar_AddRef( ICoreApplicationViewTitleBar *iface )
{
    struct core_application_view_title_bar *impl = impl_from_ICoreApplicationViewTitleBar( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p, ref %lu\n", iface, ref );
    return ref;
}

static ULONG WINAPI core_application_view_title_bar_Release( ICoreApplicationViewTitleBar *iface )
{
    struct core_application_view_title_bar *impl = impl_from_ICoreApplicationViewTitleBar( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    TRACE( "iface %p, ref %lu\n", iface, ref );
    if (!ref)
    {
        tb_event_list_done( &impl->layout_metrics_changed );
        tb_event_list_done( &impl->is_visible_changed );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI core_application_view_title_bar_GetIids( ICoreApplicationViewTitleBar *iface,
        ULONG *count, IID **iids )
{
    FIXME( "iface %p, count %p, iids %p stub!\n", iface, count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI core_application_view_title_bar_GetRuntimeClassName( ICoreApplicationViewTitleBar *iface,
        HSTRING *name )
{
    if (!name) return E_POINTER;
    return WindowsCreateString( RuntimeClass_Windows_ApplicationModel_Core_CoreApplicationViewTitleBar,
                                wcslen( RuntimeClass_Windows_ApplicationModel_Core_CoreApplicationViewTitleBar ), name );
}

static HRESULT WINAPI core_application_view_title_bar_GetTrustLevel( ICoreApplicationViewTitleBar *iface,
        TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI core_application_view_title_bar_put_ExtendViewIntoTitleBar( ICoreApplicationViewTitleBar *iface,
        boolean value )
{
    struct core_application_view_title_bar *impl = impl_from_ICoreApplicationViewTitleBar( iface );
    TRACE( "iface %p, value %d\n", iface, value );
    if (impl->extend_view_into_title_bar == value) return S_OK;
    impl->extend_view_into_title_bar = value;
    title_bar_apply_dwm( impl );
    tb_event_list_raise( &impl->layout_metrics_changed, (IInspectable *)iface );
    return S_OK;
}

static HRESULT WINAPI core_application_view_title_bar_get_ExtendViewIntoTitleBar( ICoreApplicationViewTitleBar *iface,
        boolean *value )
{
    struct core_application_view_title_bar *impl = impl_from_ICoreApplicationViewTitleBar( iface );
    TRACE( "iface %p, value %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = impl->extend_view_into_title_bar;
    return S_OK;
}

static HRESULT WINAPI core_application_view_title_bar_get_SystemOverlayLeftInset( ICoreApplicationViewTitleBar *iface,
        DOUBLE *value )
{
    TRACE( "iface %p, value %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = 0.0;
    return S_OK;
}

static HRESULT WINAPI core_application_view_title_bar_get_SystemOverlayRightInset( ICoreApplicationViewTitleBar *iface,
        DOUBLE *value )
{
    struct core_application_view_title_bar *impl = impl_from_ICoreApplicationViewTitleBar( iface );
    TRACE( "iface %p, value %p\n", iface, value );
    if (!value) return E_POINTER;
    /* Standard Windows min/max/close caption button cluster width. */
    *value = impl->extend_view_into_title_bar ? 138.0 : 0.0;
    return S_OK;
}

static HRESULT WINAPI core_application_view_title_bar_get_Height( ICoreApplicationViewTitleBar *iface,
        DOUBLE *value )
{
    struct core_application_view_title_bar *impl = impl_from_ICoreApplicationViewTitleBar( iface );
    TRACE( "iface %p, value %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = impl->is_visible ? impl->height : 0.0;
    return S_OK;
}

static HRESULT WINAPI core_application_view_title_bar_add_LayoutMetricsChanged( ICoreApplicationViewTitleBar *iface,
        __FITypedEventHandler_2_Windows__CApplicationModel__CCore__CCoreApplicationViewTitleBar_IInspectable *handler,
        EventRegistrationToken *token )
{
    struct core_application_view_title_bar *impl = impl_from_ICoreApplicationViewTitleBar( iface );
    return tb_event_list_add( &impl->layout_metrics_changed, handler, token );
}

static HRESULT WINAPI core_application_view_title_bar_remove_LayoutMetricsChanged( ICoreApplicationViewTitleBar *iface,
        EventRegistrationToken token )
{
    struct core_application_view_title_bar *impl = impl_from_ICoreApplicationViewTitleBar( iface );
    return tb_event_list_remove( &impl->layout_metrics_changed, token );
}

static HRESULT WINAPI core_application_view_title_bar_get_IsVisible( ICoreApplicationViewTitleBar *iface,
        boolean *value )
{
    struct core_application_view_title_bar *impl = impl_from_ICoreApplicationViewTitleBar( iface );
    TRACE( "iface %p, value %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = impl->is_visible;
    return S_OK;
}

static HRESULT WINAPI core_application_view_title_bar_add_IsVisibleChanged( ICoreApplicationViewTitleBar *iface,
        __FITypedEventHandler_2_Windows__CApplicationModel__CCore__CCoreApplicationViewTitleBar_IInspectable *handler,
        EventRegistrationToken *token )
{
    struct core_application_view_title_bar *impl = impl_from_ICoreApplicationViewTitleBar( iface );
    return tb_event_list_add( &impl->is_visible_changed, handler, token );
}

static HRESULT WINAPI core_application_view_title_bar_remove_IsVisibleChanged( ICoreApplicationViewTitleBar *iface,
        EventRegistrationToken token )
{
    struct core_application_view_title_bar *impl = impl_from_ICoreApplicationViewTitleBar( iface );
    return tb_event_list_remove( &impl->is_visible_changed, token );
}

static const struct ICoreApplicationViewTitleBarVtbl core_application_view_title_bar_vtbl =
{
    core_application_view_title_bar_QueryInterface,
    core_application_view_title_bar_AddRef,
    core_application_view_title_bar_Release,
    core_application_view_title_bar_GetIids,
    core_application_view_title_bar_GetRuntimeClassName,
    core_application_view_title_bar_GetTrustLevel,
    core_application_view_title_bar_put_ExtendViewIntoTitleBar,
    core_application_view_title_bar_get_ExtendViewIntoTitleBar,
    core_application_view_title_bar_get_SystemOverlayLeftInset,
    core_application_view_title_bar_get_SystemOverlayRightInset,
    core_application_view_title_bar_get_Height,
    core_application_view_title_bar_add_LayoutMetricsChanged,
    core_application_view_title_bar_remove_LayoutMetricsChanged,
    core_application_view_title_bar_get_IsVisible,
    core_application_view_title_bar_add_IsVisibleChanged,
    core_application_view_title_bar_remove_IsVisibleChanged,
};

static HRESULT core_application_view_title_bar_create( ICoreApplicationViewTitleBar **value )
{
    struct core_application_view_title_bar *impl;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->ICoreApplicationViewTitleBar_iface.lpVtbl = &core_application_view_title_bar_vtbl;
    impl->IWeakReferenceSource_iface = tb_wrs_vtbl;
    impl->ref = 1;
    impl->extend_view_into_title_bar = FALSE;
    impl->is_visible = TRUE;
    impl->height = 32.0;
    impl->hwnd = NULL;
    tb_event_list_init( &impl->layout_metrics_changed );
    tb_event_list_init( &impl->is_visible_changed );
    *value = &impl->ICoreApplicationViewTitleBar_iface;
    return S_OK;
}

/* ICoreApplicationView2 implementation */

static HRESULT WINAPI core_application_view2_QueryInterface( ICoreApplicationView2 *iface, REFIID iid, void **out )
{
    struct core_application_view *impl = impl_from_ICoreApplicationView2( iface );
    return core_application_view_QueryInterface( &impl->ICoreApplicationView_iface, iid, out );
}

static ULONG WINAPI core_application_view2_AddRef( ICoreApplicationView2 *iface )
{
    struct core_application_view *impl = impl_from_ICoreApplicationView2( iface );
    return core_application_view_AddRef( &impl->ICoreApplicationView_iface );
}

static ULONG WINAPI core_application_view2_Release( ICoreApplicationView2 *iface )
{
    struct core_application_view *impl = impl_from_ICoreApplicationView2( iface );
    return core_application_view_Release( &impl->ICoreApplicationView_iface );
}

static HRESULT WINAPI core_application_view2_GetIids( ICoreApplicationView2 *iface, ULONG *count, IID **iids )
{
    FIXME( "iface %p stub!\n", iface );
    return E_NOTIMPL;
}

static HRESULT WINAPI core_application_view2_GetRuntimeClassName( ICoreApplicationView2 *iface, HSTRING *name )
{
    if (!name) return E_POINTER;
    return WindowsCreateString( RuntimeClass_Windows_ApplicationModel_Core_CoreApplicationView,
                                wcslen( RuntimeClass_Windows_ApplicationModel_Core_CoreApplicationView ), name );
}

static HRESULT WINAPI core_application_view2_GetTrustLevel( ICoreApplicationView2 *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI core_application_view2_get_Dispatcher( ICoreApplicationView2 *iface,
        __x_ABI_CWindows_CUI_CCore_CICoreDispatcher **value )
{
    struct core_application_view *impl = impl_from_ICoreApplicationView2( iface );
    __x_ABI_CWindows_CUI_CCore_CICoreWindow *window = NULL;
    HRESULT hr;

    TRACE( "iface %p, value %p\n", iface, value );

    if (!value) return E_POINTER;
    *value = NULL;

    hr = core_application_view_get_CoreWindow( &impl->ICoreApplicationView_iface, &window );
    if (FAILED(hr)) return hr;
    if (!window) return E_FAIL;

    hr = __x_ABI_CWindows_CUI_CCore_CICoreWindow_get_Dispatcher( window, value );
    __x_ABI_CWindows_CUI_CCore_CICoreWindow_Release( window );
    return hr;
}

static const struct ICoreApplicationView2Vtbl core_application_view2_vtbl =
{
    core_application_view2_QueryInterface,
    core_application_view2_AddRef,
    core_application_view2_Release,
    core_application_view2_GetIids,
    core_application_view2_GetRuntimeClassName,
    core_application_view2_GetTrustLevel,
    core_application_view2_get_Dispatcher,
};

/* ICoreApplicationView3 implementation */

static HRESULT WINAPI core_application_view3_QueryInterface( ICoreApplicationView3 *iface, REFIID iid, void **out )
{
    struct core_application_view *impl = impl_from_ICoreApplicationView3( iface );
    return core_application_view_QueryInterface( &impl->ICoreApplicationView_iface, iid, out );
}

static ULONG WINAPI core_application_view3_AddRef( ICoreApplicationView3 *iface )
{
    struct core_application_view *impl = impl_from_ICoreApplicationView3( iface );
    return core_application_view_AddRef( &impl->ICoreApplicationView_iface );
}

static ULONG WINAPI core_application_view3_Release( ICoreApplicationView3 *iface )
{
    struct core_application_view *impl = impl_from_ICoreApplicationView3( iface );
    return core_application_view_Release( &impl->ICoreApplicationView_iface );
}

static HRESULT WINAPI core_application_view3_GetIids( ICoreApplicationView3 *iface, ULONG *count, IID **iids )
{
    FIXME( "iface %p stub!\n", iface );
    return E_NOTIMPL;
}

static HRESULT WINAPI core_application_view3_GetRuntimeClassName( ICoreApplicationView3 *iface, HSTRING *name )
{
    if (!name) return E_POINTER;
    return WindowsCreateString( RuntimeClass_Windows_ApplicationModel_Core_CoreApplicationView,
                                wcslen( RuntimeClass_Windows_ApplicationModel_Core_CoreApplicationView ), name );
}

static HRESULT WINAPI core_application_view3_GetTrustLevel( ICoreApplicationView3 *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI core_application_view3_get_IsComponent( ICoreApplicationView3 *iface, boolean *value )
{
    TRACE( "iface %p, value %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = FALSE;
    return S_OK;
}

static HRESULT WINAPI core_application_view3_get_TitleBar( ICoreApplicationView3 *iface,
        ICoreApplicationViewTitleBar **value )
{
    struct core_application_view *impl = impl_from_ICoreApplicationView3( iface );
    TRACE( "iface %p, value %p\n", iface, value );
    if (!value) return E_POINTER;
    if (!impl->title_bar)
    {
        HRESULT hr = core_application_view_title_bar_create( &impl->title_bar );
        if (FAILED(hr)) return hr;
    }
    ICoreApplicationViewTitleBar_AddRef( impl->title_bar );
    *value = impl->title_bar;
    return S_OK;
}

static HRESULT WINAPI core_application_view3_add_HostedViewClosing( ICoreApplicationView3 *iface,
        __FITypedEventHandler_2_Windows__CApplicationModel__CCore__CCoreApplicationView_Windows__CApplicationModel__CCore__CHostedViewClosingEventArgs *handler,
        EventRegistrationToken *token )
{
    FIXME( "iface %p, handler %p, token %p stub!\n", iface, handler, token );
    if (token) token->value = 0;
    return S_OK;
}

static HRESULT WINAPI core_application_view3_remove_HostedViewClosing( ICoreApplicationView3 *iface,
        EventRegistrationToken token )
{
    FIXME( "iface %p, token %#I64x stub!\n", iface, token.value );
    return S_OK;
}

static const struct ICoreApplicationView3Vtbl core_application_view3_vtbl =
{
    core_application_view3_QueryInterface,
    core_application_view3_AddRef,
    core_application_view3_Release,
    core_application_view3_GetIids,
    core_application_view3_GetRuntimeClassName,
    core_application_view3_GetTrustLevel,
    core_application_view3_get_IsComponent,
    core_application_view3_get_TitleBar,
    core_application_view3_add_HostedViewClosing,
    core_application_view3_remove_HostedViewClosing,
};

/* Single process-wide CoreApplicationView shared by Run + GetCurrentView; they
 * MUST return the same object or FrameworkUdk fail-fasts. */
static ICoreApplicationView *the_application_view;

static HRESULT WINAPI core_application_view6_QueryInterface( IInspectable *iface, REFIID iid, void **out )
{
    return core_application_view_QueryInterface( &impl_from_ICoreApplicationView6( iface )->ICoreApplicationView_iface, iid, out );
}
static ULONG WINAPI core_application_view6_AddRef( IInspectable *iface )
{
    return core_application_view_AddRef( &impl_from_ICoreApplicationView6( iface )->ICoreApplicationView_iface );
}
static ULONG WINAPI core_application_view6_Release( IInspectable *iface )
{
    return core_application_view_Release( &impl_from_ICoreApplicationView6( iface )->ICoreApplicationView_iface );
}
static HRESULT WINAPI core_application_view6_GetIids( IInspectable *iface, ULONG *count, IID **iids )
{
    return core_application_view_GetIids( &impl_from_ICoreApplicationView6( iface )->ICoreApplicationView_iface, count, iids );
}
static HRESULT WINAPI core_application_view6_GetRuntimeClassName( IInspectable *iface, HSTRING *name )
{
    return core_application_view_GetRuntimeClassName( &impl_from_ICoreApplicationView6( iface )->ICoreApplicationView_iface, name );
}
static HRESULT WINAPI core_application_view6_GetTrustLevel( IInspectable *iface, TrustLevel *level )
{
    return core_application_view_GetTrustLevel( &impl_from_ICoreApplicationView6( iface )->ICoreApplicationView_iface, level );
}
static HRESULT WINAPI core_application_view6_get_DispatcherQueue( IInspectable *iface, void **value )
{
    /* {2bc095a8-8ef0-446d-9e60-3a3e0428c671} is ICoreApplicationViewProperties::get_Properties
     * returning an IPropertySet / IMap<HSTRING, IInspectable*> ({1b0d3570-...}). */
    struct core_application_view *impl = impl_from_ICoreApplicationView6( iface );
    if (!value) return E_POINTER;
    *value = NULL;
    if (!impl->properties)
    {
        HRESULT hr = static_lifetime_map_create( &impl->properties );
        if (FAILED(hr)) return hr;
    }
    *value = impl->properties;
    if (*value) IUnknown_AddRef( (IUnknown *)*value );
    TRACE( "iface %p -> properties %p\n", iface, *value );
    return S_OK;
}
static const void *core_application_view6_vtbl[] =
{
    core_application_view6_QueryInterface,
    core_application_view6_AddRef,
    core_application_view6_Release,
    core_application_view6_GetIids,
    core_application_view6_GetRuntimeClassName,
    core_application_view6_GetTrustLevel,
    core_application_view6_get_DispatcherQueue,
};

static ICoreApplicationView *core_application_view_get(void)
{
    if (!the_application_view)
    {
        struct core_application_view *impl;
        if (!(impl = calloc( 1, sizeof(*impl) ))) return NULL;
        impl->ICoreApplicationView_iface.lpVtbl = &core_application_view_vtbl;
        impl->ICoreApplicationView2_iface.lpVtbl = &core_application_view2_vtbl;
        impl->ICoreApplicationView3_iface.lpVtbl = &core_application_view3_vtbl;
        impl->ICoreApplicationView6_iface.lpVtbl = (const IInspectableVtbl *)core_application_view6_vtbl;
        impl->ref = 1;
        the_application_view = &impl->ICoreApplicationView_iface;
    }
    ICoreApplicationView_AddRef( the_application_view );
    return the_application_view;
}

/* ICoreApplication — the static Windows.ApplicationModel.Core.CoreApplication */

static HRESULT WINAPI core_application_QueryInterface( ICoreApplication *iface, REFIID iid, void **out )
{
    struct factory *impl = impl_from_ICoreApplication( iface );
    return IActivationFactory_QueryInterface( &impl->IActivationFactory_iface, iid, out );
}

static ULONG WINAPI core_application_AddRef( ICoreApplication *iface )
{
    struct factory *impl = impl_from_ICoreApplication( iface );
    return IActivationFactory_AddRef( &impl->IActivationFactory_iface );
}

static ULONG WINAPI core_application_Release( ICoreApplication *iface )
{
    struct factory *impl = impl_from_ICoreApplication( iface );
    return IActivationFactory_Release( &impl->IActivationFactory_iface );
}

static HRESULT WINAPI core_application_GetIids( ICoreApplication *iface, ULONG *count, IID **iids )
{
    FIXME( "iface %p, count %p, iids %p stub!\n", iface, count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI core_application_GetRuntimeClassName( ICoreApplication *iface, HSTRING *name )
{
    FIXME( "iface %p, name %p stub!\n", iface, name );
    return E_NOTIMPL;
}

static HRESULT WINAPI core_application_GetTrustLevel( ICoreApplication *iface, TrustLevel *level )
{
    FIXME( "iface %p, level %p stub!\n", iface, level );
    return E_NOTIMPL;
}

static HRESULT WINAPI core_application_get_Id( ICoreApplication *iface, HSTRING *value )
{
    static const HKEY roots[] = { HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE };
    WCHAR aumid[512] = {0}, exe[MAX_PATH], subkey[600];
    const WCHAR *bang, *slash;
    SIZE_T i;

    TRACE( "iface %p, value %p.\n", iface, value );
    if (!value) return E_POINTER;

    if (!GetEnvironmentVariableW( L"WINE_PACKAGE_AUMID", aumid, ARRAY_SIZE( aumid ) ))
        aumid[0] = 0;

    if (!aumid[0] && GetModuleFileNameW( NULL, exe, ARRAY_SIZE( exe ) ) &&
        (slash = wcsrchr( exe, L'\\' )) && slash[1])
    {
        swprintf( subkey, ARRAY_SIZE( subkey ),
                  L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppModel\\Exes\\%ls", slash + 1 );
        for (i = 0; i < ARRAY_SIZE( roots ); i++)
        {
            HKEY hkey;
            DWORD type = 0, size = sizeof(aumid);
            if (RegOpenKeyExW( roots[i], subkey, 0, KEY_READ, &hkey ) == ERROR_SUCCESS)
            {
                if (RegQueryValueExW( hkey, L"ApplicationUserModelId", NULL, &type, (BYTE *)aumid, &size ) == ERROR_SUCCESS &&
                    type == REG_SZ && aumid[0])
                {
                    RegCloseKey( hkey );
                    break;
                }
                aumid[0] = 0;
                RegCloseKey( hkey );
            }
        }
    }

    if ((bang = wcschr( aumid, L'!' )) && bang[1])
        return WindowsCreateString( bang + 1, wcslen( bang + 1 ), value );
    return WindowsCreateString( NULL, 0, value );
}

static HRESULT WINAPI core_application_add_Suspending( ICoreApplication *iface,
        IEventHandler_SuspendingEventArgs *handler, EventRegistrationToken *token )
{
    FIXME( "iface %p, handler %p, token %p semi-stub.\n", iface, handler, token );
    if (token) token->value = 0;
    return S_OK;
}

static HRESULT WINAPI core_application_remove_Suspending( ICoreApplication *iface, EventRegistrationToken token )
{
    FIXME( "iface %p, token %#I64x semi-stub.\n", iface, token.value );
    return S_OK;
}

static HRESULT WINAPI core_application_add_Resuming( ICoreApplication *iface,
        IEventHandler_IInspectable *handler, EventRegistrationToken *token )
{
    TRACE( "iface %p, handler %p, token %p\n", iface, handler, token );
    return core_app_add_resuming( handler, token );
}

static HRESULT WINAPI core_application_remove_Resuming( ICoreApplication *iface, EventRegistrationToken token )
{
    TRACE( "iface %p, token %#I64x\n", iface, token.value );
    return core_app_remove_resuming( token );
}

/* ---- CoreApplication.Properties session persistence (%LOCALAPPDATA%\<FamilyName>\Session\state.ini) ---- */

static const GUID IID_IReferenceTracker_props = {0x11d3b13a, 0x180e, 0x4789, {0xa8, 0xbe, 0x77, 0x12, 0x88, 0x28, 0x93, 0xe6}};
static const GUID IID_IMapView_String_IInspectable_props = {0xe8cc1dcf, 0xaa1b, 0x51e7, {0x90, 0xa7, 0x6b, 0x7d, 0x99, 0xbd, 0x99, 0xa0}};
static const GUID IID_IMapView_StringObject_IInspectable_props = {0x5c13e51c, 0x4f32, 0x4726, {0xa3, 0xfd, 0xf3, 0xed, 0xd6, 0x3d, 0xa3, 0xa0}};
static const GUID IID_IMap_String_Object_props = {0x8114f3e9, 0xf118, 0x3c7b, {0x91, 0x94, 0x00, 0xa3, 0xa2, 0x4d, 0x96, 0x7d}};
static const GUID IID_IMapView_String_Object_props = {0x7c054694, 0x4acd, 0x3716, {0xa1, 0x7a, 0xd5, 0x83, 0xc5, 0xb3, 0xde, 0x8b}};
static const GUID IID_IObservableMap_String_Object_props = {0x65df2bf5, 0xbf39, 0x41b5, {0xae, 0xbc, 0x5a, 0x9d, 0x86, 0x5e, 0x47, 0x2b}};
static const GUID IID_IMap_Object_Object_props = {0xf5f69427, 0x55ed, 0x5512, {0x84, 0x29, 0xd4, 0xf6, 0x62, 0x6d, 0xfc, 0xdd}};
static const GUID IID_IMapView_Object_Object_props = {0x59e7ae0c, 0xc29d, 0x5ad6, {0xbe, 0xf5, 0xde, 0xdb, 0x52, 0xa1, 0x98, 0xe1}};
static const GUID IID_IIterable_KeyValuePair_Object_Object_props = {0x33ac68f0, 0x1084, 0x529a, {0x8a, 0x17, 0x4e, 0x7c, 0x8a, 0xdb, 0x7a, 0x0c}};
static const GUID IID_IResourceDictionary_props = {0xc1ea4f24, 0xd6de, 0x4191, {0x8e, 0x3a, 0xf4, 0x86, 0x01, 0xf7, 0x48, 0x9c}};
static const GUID IID_INotifyCollectionChanged_props = {0x350ffb1a, 0xdf08, 0x56fd, {0x98, 0xd1, 0x37, 0x4d, 0x77, 0x72, 0x34, 0x9f}};
static const GUID IID_INotifyPropertyChanged_props = {0xe8a31095, 0x6789, 0x587b, {0xb8, 0xe5, 0xcc, 0x7a, 0x1b, 0xdd, 0x8e, 0x37}};
static const GUID IID_IStringable_props = {0x96369f54, 0x8eb6, 0x48f0, {0xab, 0xce, 0xc1, 0xb2, 0x11, 0xe6, 0x27, 0xc3}};

struct core_com_interface_entry
{
    void **vtable;
    IID *iid;
};

struct core_properties_inner_vtbl
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)( IPropertySet *iface, REFIID iid, void **out );
    ULONG (STDMETHODCALLTYPE *AddRef)( IPropertySet *iface );
    ULONG (STDMETHODCALLTYPE *Release)( IPropertySet *iface );
    HRESULT (STDMETHODCALLTYPE *GetIids)( IPropertySet *iface, ULONG *count, IID **iids );
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)( IPropertySet *iface, HSTRING *name );
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)( IPropertySet *iface, TrustLevel *level );
    HRESULT (STDMETHODCALLTYPE *Lookup)( IPropertySet *iface, HSTRING key, IInspectable **value );
    HRESULT (STDMETHODCALLTYPE *get_Size)( IPropertySet *iface, UINT32 *size );
    HRESULT (STDMETHODCALLTYPE *HasKey)( IPropertySet *iface, HSTRING key, boolean *found );
    HRESULT (STDMETHODCALLTYPE *GetView)( IPropertySet *iface, IMapView_HSTRING_IInspectable **first, IMapView_HSTRING_IInspectable **second );
    HRESULT (STDMETHODCALLTYPE *Insert)( IPropertySet *iface, HSTRING key, IInspectable *value, boolean *replaced );
    HRESULT (STDMETHODCALLTYPE *Remove)( IPropertySet *iface, HSTRING key );
    HRESULT (STDMETHODCALLTYPE *Clear)( IPropertySet *iface );
    HRESULT (STDMETHODCALLTYPE *First)( IPropertySet *iface, IIterator_IKeyValuePair_HSTRING_IInspectable **iter );
    HRESULT (STDMETHODCALLTYPE *add_MapChanged)( IPropertySet *iface, IMapChangedEventHandler_HSTRING_IInspectable *handler, EventRegistrationToken *token );
    HRESULT (STDMETHODCALLTYPE *remove_MapChanged)( IPropertySet *iface, EventRegistrationToken token );
    HRESULT (STDMETHODCALLTYPE *get_MapChanged)( IPropertySet *iface, IMapChangedEventHandler_HSTRING_IInspectable **handler );
    HRESULT (STDMETHODCALLTYPE *add_CollectionChanged)( IPropertySet *iface, void *handler, EventRegistrationToken *token );
    HRESULT (STDMETHODCALLTYPE *remove_CollectionChanged)( IPropertySet *iface, EventRegistrationToken token );
    HRESULT (STDMETHODCALLTYPE *get_CollectionChanged)( IPropertySet *iface, void **handler );
    HRESULT (STDMETHODCALLTYPE *add_PropertyChanged)( IPropertySet *iface, void *handler, EventRegistrationToken *token );
    HRESULT (STDMETHODCALLTYPE *remove_PropertyChanged)( IPropertySet *iface, EventRegistrationToken token );
    HRESULT (STDMETHODCALLTYPE *get_PropertyChanged)( IPropertySet *iface, void **handler );
};

struct core_properties
{
    IPropertySet IPropertySet_iface;
    IMapView_HSTRING_IInspectable IMapView_iface;
    IIterable_IKeyValuePair_HSTRING_IInspectable IIterable_iface;
    IWineSessionProperties IWineSessionProperties_iface;
    LONG ref;
    IPropertySet *inner;
    CRITICAL_SECTION cs;
    HSTRING *keys;
    SIZE_T key_count, key_cap;
    struct core_com_interface_entry *iids_cache;
    ULONG iids_count;
};

static CRITICAL_SECTION core_properties_singleton_cs;
static CRITICAL_SECTION_DEBUG core_properties_singleton_cs_debug =
{
    0, 0, &core_properties_singleton_cs,
    { &core_properties_singleton_cs_debug.ProcessLocksList, &core_properties_singleton_cs_debug.ProcessLocksList },
    0, 0, { (DWORD_PTR)(__FILE__ ": core_properties_singleton_cs") }
};
static CRITICAL_SECTION core_properties_singleton_cs = { &core_properties_singleton_cs_debug, -1, 0, 0, 0, 0 };

static struct core_properties *the_core_properties;

static inline struct core_properties *impl_from_core_properties_IPropertySet( IPropertySet *iface )
{
    return CONTAINING_RECORD( iface, struct core_properties, IPropertySet_iface );
}

static inline struct core_properties *impl_from_core_properties_IMapView( IMapView_HSTRING_IInspectable *iface )
{
    return CONTAINING_RECORD( iface, struct core_properties, IMapView_iface );
}

static inline struct core_properties *impl_from_core_properties_IIterable( IIterable_IKeyValuePair_HSTRING_IInspectable *iface )
{
    return CONTAINING_RECORD( iface, struct core_properties, IIterable_iface );
}

static inline struct core_properties *impl_from_core_properties_IWineSessionProperties( IWineSessionProperties *iface )
{
    return CONTAINING_RECORD( iface, struct core_properties, IWineSessionProperties_iface );
}

static inline const struct core_properties_inner_vtbl *core_properties_inner_vt( struct core_properties *impl )
{
    return (const struct core_properties_inner_vtbl *)impl->inner->lpVtbl;
}

static void core_app_get_family_name( WCHAR *buf, DWORD cch )
{
    WCHAR exe[MAX_PATH], *p;
    DWORD len;

    len = GetEnvironmentVariableW( L"WINE_PACKAGE_FAMILY_NAME", buf, cch );
    if (len && len < cch) return;

    len = GetModuleFileNameW( NULL, exe, ARRAY_SIZE(exe) );
    if (!len || len >= ARRAY_SIZE(exe))
    {
        lstrcpynW( buf, L"Windows", cch );
        return;
    }
    if ((p = wcsrchr( exe, L'\\' ))) p++;
    else if ((p = wcsrchr( exe, L'/' ))) p++;
    else p = exe;
    lstrcpynW( buf, p, cch );
    if ((p = wcsrchr( buf, L'.' )) && p > buf) *p = 0;
}

static BOOL core_app_get_session_ini_path( WCHAR *path, DWORD cch )
{
    WCHAR base[MAX_PATH], family[MAX_PATH], dir[MAX_PATH * 2];

    if (FAILED(SHGetFolderPathW( NULL, CSIDL_LOCAL_APPDATA | CSIDL_FLAG_CREATE, NULL, SHGFP_TYPE_CURRENT, base )) || !base[0])
    {
        if (!GetEnvironmentVariableW( L"LOCALAPPDATA", base, ARRAY_SIZE(base) ) || !base[0])
        {
            if (!GetEnvironmentVariableW( L"USERPROFILE", base, ARRAY_SIZE(base) ) || !base[0])
                return FALSE;
            lstrcatW( base, L"\\AppData\\Local" );
        }
    }

    core_app_get_family_name( family, ARRAY_SIZE(family) );

    if (swprintf( dir, ARRAY_SIZE(dir), L"%ls\\%ls", base, family ) < 0) return FALSE;
    CreateDirectoryW( dir, NULL );
    if (swprintf( dir, ARRAY_SIZE(dir), L"%ls\\%ls\\Session", base, family ) < 0) return FALSE;
    CreateDirectoryW( dir, NULL );

    return swprintf( path, cch, L"%ls\\state.ini", dir ) > 0;
}

static HRESULT core_app_pv_statics( IPropertyValueStatics **out )
{
    static const WCHAR *cls = RuntimeClass_Windows_Foundation_PropertyValue;
    static IPropertyValueStatics *cache;
    HSTRING class_id;
    HRESULT hr;

    if (cache)
    {
        IPropertyValueStatics_AddRef( cache );
        *out = cache;
        return S_OK;
    }
    if (FAILED(hr = WindowsCreateString( cls, wcslen( cls ), &class_id ))) return hr;
    hr = RoGetActivationFactory( class_id, &IID_IPropertyValueStatics, (void **)&cache );
    WindowsDeleteString( class_id );
    if (FAILED(hr)) return hr;
    IPropertyValueStatics_AddRef( cache );
    *out = cache;
    return S_OK;
}

static HSTRING core_properties_resolve_key( void *key, BOOL *allocated )
{
    UINT32 len = 0;
    const WCHAR *raw;
    HSTRING str = NULL;

    *allocated = FALSE;
    if (!key) return NULL;

    raw = WindowsGetStringRawBuffer( (HSTRING)key, &len );
    if (raw && len < 0x1000) return (HSTRING)key;

    __TRY
    {
        IUnknown *unk = (IUnknown *)key;
        void *stringable = NULL;
        if (SUCCEEDED(IUnknown_QueryInterface( unk, &IID_IStringable_props, &stringable )) && stringable)
        {
            void **v = *(void ***)stringable;
            HRESULT (STDMETHODCALLTYPE *to_string)( void *, HSTRING * ) = (void *)v[6];
            if (SUCCEEDED(to_string( stringable, &str )) && str)
                *allocated = TRUE;
            IUnknown_Release( (IUnknown *)stringable );
        }
    }
    __EXCEPT_ALL
    {
    }
    __ENDTRY

    return str;
}

static void core_properties_track_key( struct core_properties *impl, HSTRING key )
{
    SIZE_T i;
    HSTRING dup = NULL;

    EnterCriticalSection( &impl->cs );
    for (i = 0; i < impl->key_count; i++)
    {
        INT32 cmp = 1;
        WindowsCompareStringOrdinal( impl->keys[i], key, &cmp );
        if (cmp == 0)
        {
            LeaveCriticalSection( &impl->cs );
            return;
        }
    }

    if (impl->key_count >= impl->key_cap)
    {
        SIZE_T cap = impl->key_cap ? impl->key_cap * 2 : 8;
        HSTRING *tmp = realloc( impl->keys, cap * sizeof(*tmp) );
        if (!tmp)
        {
            LeaveCriticalSection( &impl->cs );
            return;
        }
        impl->keys = tmp;
        impl->key_cap = cap;
    }

    if (SUCCEEDED(WindowsDuplicateString( key, &dup )))
        impl->keys[impl->key_count++] = dup;
    LeaveCriticalSection( &impl->cs );
}

static void core_properties_untrack_key( struct core_properties *impl, HSTRING key )
{
    SIZE_T i;

    EnterCriticalSection( &impl->cs );
    for (i = 0; i < impl->key_count; i++)
    {
        INT32 cmp = 1;
        WindowsCompareStringOrdinal( impl->keys[i], key, &cmp );
        if (cmp == 0)
        {
            WindowsDeleteString( impl->keys[i] );
            memmove( &impl->keys[i], &impl->keys[i + 1], (impl->key_count - i - 1) * sizeof(*impl->keys) );
            impl->key_count--;
            break;
        }
    }
    LeaveCriticalSection( &impl->cs );
}

static void core_properties_clear_keys( struct core_properties *impl )
{
    SIZE_T i;

    EnterCriticalSection( &impl->cs );
    for (i = 0; i < impl->key_count; i++)
        WindowsDeleteString( impl->keys[i] );
    impl->key_count = 0;
    LeaveCriticalSection( &impl->cs );
}

static void core_properties_ensure_empty_ini( const WCHAR *ini_path )
{
    static const char header[] = "[Session]\r\n";
    HANDLE h = CreateFileW( ini_path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL );
    if (h != INVALID_HANDLE_VALUE)
    {
        DWORD written;
        WriteFile( h, header, sizeof(header) - 1, &written, NULL );
        CloseHandle( h );
    }
}

static HRESULT core_properties_flush_impl( struct core_properties *impl )
{
    WCHAR ini_path[MAX_PATH * 2];
    HSTRING *keys = NULL;
    SIZE_T count = 0, i;
    BOOL wrote_any = FALSE;

    if (!impl) return S_OK;
    if (!core_app_get_session_ini_path( ini_path, ARRAY_SIZE(ini_path) )) return E_FAIL;

    EnterCriticalSection( &impl->cs );
    if (impl->key_count)
    {
        keys = calloc( impl->key_count, sizeof(*keys) );
        if (keys)
        {
            for (i = 0; i < impl->key_count; i++)
            {
                if (SUCCEEDED(WindowsDuplicateString( impl->keys[i], &keys[count] )))
                    count++;
            }
        }
    }
    LeaveCriticalSection( &impl->cs );

    WritePrivateProfileStringW( L"Session", NULL, NULL, ini_path );
    WritePrivateProfileStringW( L"Types", NULL, NULL, ini_path );

    for (i = 0; i < count; i++)
    {
        const WCHAR *key_w = WindowsGetStringRawBuffer( keys[i], NULL );
        IInspectable *val = NULL;
        IPropertyValue *pv = NULL;
        PropertyType ptype;

        if (!key_w || !key_w[0])
        {
            WindowsDeleteString( keys[i] );
            continue;
        }
        if (SUCCEEDED(core_properties_inner_vt( impl )->Lookup( impl->inner, keys[i], &val )) && val)
        {
            if (SUCCEEDED(IInspectable_QueryInterface( val, &IID_IPropertyValue, (void **)&pv )) && pv)
            {
                if (SUCCEEDED(IPropertyValue_get_Type( pv, &ptype )))
                {
                    WCHAR num_buf[64];
                    switch (ptype)
                    {
                    case PropertyType_String:
                    {
                        HSTRING sval = NULL;
                        if (SUCCEEDED(IPropertyValue_GetString( pv, &sval )))
                        {
                            const WCHAR *w = WindowsGetStringRawBuffer( sval, NULL );
                            WritePrivateProfileStringW( L"Session", key_w, w ? w : L"", ini_path );
                            WritePrivateProfileStringW( L"Types", key_w, L"string", ini_path );
                            wrote_any = TRUE;
                            WindowsDeleteString( sval );
                        }
                        break;
                    }
                    case PropertyType_UInt64:
                    {
                        UINT64 u = 0;
                        if (SUCCEEDED(IPropertyValue_GetUInt64( pv, &u )))
                        {
                            swprintf( num_buf, ARRAY_SIZE(num_buf), L"%I64u", u );
                            WritePrivateProfileStringW( L"Session", key_w, num_buf, ini_path );
                            WritePrivateProfileStringW( L"Types", key_w, L"uint64", ini_path );
                            wrote_any = TRUE;
                        }
                        break;
                    }
                    case PropertyType_Int64:
                    {
                        INT64 v = 0;
                        if (SUCCEEDED(IPropertyValue_GetInt64( pv, &v )))
                        {
                            swprintf( num_buf, ARRAY_SIZE(num_buf), L"%I64d", v );
                            WritePrivateProfileStringW( L"Session", key_w, num_buf, ini_path );
                            WritePrivateProfileStringW( L"Types", key_w, L"int64", ini_path );
                            wrote_any = TRUE;
                        }
                        break;
                    }
                    case PropertyType_UInt32:
                    {
                        UINT32 u = 0;
                        if (SUCCEEDED(IPropertyValue_GetUInt32( pv, &u )))
                        {
                            swprintf( num_buf, ARRAY_SIZE(num_buf), L"%u", u );
                            WritePrivateProfileStringW( L"Session", key_w, num_buf, ini_path );
                            WritePrivateProfileStringW( L"Types", key_w, L"uint32", ini_path );
                            wrote_any = TRUE;
                        }
                        break;
                    }
                    case PropertyType_Int32:
                    {
                        INT32 v = 0;
                        if (SUCCEEDED(IPropertyValue_GetInt32( pv, &v )))
                        {
                            swprintf( num_buf, ARRAY_SIZE(num_buf), L"%d", v );
                            WritePrivateProfileStringW( L"Session", key_w, num_buf, ini_path );
                            WritePrivateProfileStringW( L"Types", key_w, L"int32", ini_path );
                            wrote_any = TRUE;
                        }
                        break;
                    }
                    case PropertyType_Boolean:
                    {
                        boolean b = FALSE;
                        if (SUCCEEDED(IPropertyValue_GetBoolean( pv, &b )))
                        {
                            WritePrivateProfileStringW( L"Session", key_w, b ? L"1" : L"0", ini_path );
                            WritePrivateProfileStringW( L"Types", key_w, L"boolean", ini_path );
                            wrote_any = TRUE;
                        }
                        break;
                    }
                    case PropertyType_Double:
                    {
                        DOUBLE d = 0.0;
                        if (SUCCEEDED(IPropertyValue_GetDouble( pv, &d )))
                        {
                            swprintf( num_buf, ARRAY_SIZE(num_buf), L"%.17g", d );
                            WritePrivateProfileStringW( L"Session", key_w, num_buf, ini_path );
                            WritePrivateProfileStringW( L"Types", key_w, L"double", ini_path );
                            wrote_any = TRUE;
                        }
                        break;
                    }
                    default:
                        break;
                    }
                }
                IPropertyValue_Release( pv );
            }
            IInspectable_Release( val );
        }
        WindowsDeleteString( keys[i] );
    }
    free( keys );

    WritePrivateProfileStringW( NULL, NULL, NULL, ini_path );
    if (!wrote_any)
    {
        HANDLE h = CreateFileW( ini_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL );
        if (h != INVALID_HANDLE_VALUE)
        {
            static const char header[] = "[Session]\r\n";
            DWORD written;
            WriteFile( h, header, sizeof(header) - 1, &written, NULL );
            CloseHandle( h );
        }
    }
    TRACE( "flushed %Iu keys to %s\n", count, debugstr_w( ini_path ) );
    return S_OK;
}

static void core_properties_load_impl( struct core_properties *impl )
{
    WCHAR ini_path[MAX_PATH * 2];
    IPropertyValueStatics *statics = NULL;
    DWORD cap = 4096, len;
    WCHAR *sec_buf = NULL, *p;

    if (!core_app_get_session_ini_path( ini_path, ARRAY_SIZE(ini_path) )) return;

    if (GetFileAttributesW( ini_path ) == INVALID_FILE_ATTRIBUTES)
    {
        core_properties_ensure_empty_ini( ini_path );
        return;
    }

    for (;;)
    {
        WCHAR *tmp = realloc( sec_buf, cap * sizeof(WCHAR) );
        if (!tmp) { free( sec_buf ); return; }
        sec_buf = tmp;
        len = GetPrivateProfileSectionW( L"Session", sec_buf, cap, ini_path );
        if (len < cap - 2 || cap >= 65536) break;
        cap *= 2;
    }

    if (!len)
    {
        WritePrivateProfileStringW( NULL, NULL, NULL, ini_path );
        free( sec_buf );
        return;
    }

    if (FAILED(core_app_pv_statics( &statics )))
    {
        WritePrivateProfileStringW( NULL, NULL, NULL, ini_path );
        free( sec_buf );
        return;
    }

    for (p = sec_buf; *p; p += wcslen( p ) + 1)
    {
        WCHAR *eq = wcschr( p, L'=' );
        WCHAR type_buf[32];
        IInspectable *boxed = NULL;
        HSTRING hkey = NULL;
        const WCHAR *val;
        HRESULT hr = E_FAIL;

        if (!eq) continue;
        *eq = 0;
        val = eq + 1;
        if (!p[0]) continue;

        GetPrivateProfileStringW( L"Types", p, L"string", type_buf, ARRAY_SIZE(type_buf), ini_path );
        if (!wcscmp( type_buf, L"uint64" ))
            hr = IPropertyValueStatics_CreateUInt64( statics, wcstoull( val, NULL, 10 ), &boxed );
        else if (!wcscmp( type_buf, L"int64" ))
            hr = IPropertyValueStatics_CreateInt64( statics, wcstoll( val, NULL, 10 ), &boxed );
        else if (!wcscmp( type_buf, L"uint32" ))
            hr = IPropertyValueStatics_CreateUInt32( statics, (UINT32)wcstoul( val, NULL, 10 ), &boxed );
        else if (!wcscmp( type_buf, L"int32" ))
            hr = IPropertyValueStatics_CreateInt32( statics, (INT32)wcstol( val, NULL, 10 ), &boxed );
        else if (!wcscmp( type_buf, L"boolean" ))
            hr = IPropertyValueStatics_CreateBoolean( statics, (!wcscmp( val, L"1" ) || !_wcsicmp( val, L"true" )), &boxed );
        else if (!wcscmp( type_buf, L"double" ))
            hr = IPropertyValueStatics_CreateDouble( statics, wcstod( val, NULL ), &boxed );
        else
        {
            HSTRING hval = NULL;
            if (SUCCEEDED(WindowsCreateString( val, wcslen( val ), &hval )))
            {
                hr = IPropertyValueStatics_CreateString( statics, hval, &boxed );
                WindowsDeleteString( hval );
            }
        }

        if (SUCCEEDED(hr) && boxed)
        {
            if (SUCCEEDED(WindowsCreateString( p, wcslen( p ), &hkey )))
            {
                boolean replaced = FALSE;
                if (SUCCEEDED(core_properties_inner_vt( impl )->Insert( impl->inner, hkey, boxed, &replaced )))
                    core_properties_track_key( impl, hkey );
                WindowsDeleteString( hkey );
            }
            IInspectable_Release( boxed );
        }
    }

    WritePrivateProfileStringW( NULL, NULL, NULL, ini_path );
    IPropertyValueStatics_Release( statics );
    free( sec_buf );
    TRACE( "loaded session properties from %s\n", debugstr_w( ini_path ) );
}

static const struct core_properties_inner_vtbl core_properties_vtbl;
static const struct IMapView_HSTRING_IInspectableVtbl core_properties_map_view_vtbl;
static const struct IIterable_IKeyValuePair_HSTRING_IInspectableVtbl core_properties_iterable_vtbl;
static const struct IWineSessionPropertiesVtbl core_properties_wine_session_vtbl;

static HRESULT core_properties_first_impl( struct core_properties *impl,
                                           IIterator_IKeyValuePair_HSTRING_IInspectable **iter )
{
    IMapView_HSTRING_IInspectable *view = NULL;
    IIterable_IKeyValuePair_HSTRING_IInspectable *iterable = NULL;
    HRESULT hr;

    if (!iter) return E_POINTER;
    *iter = NULL;
    if (FAILED(hr = core_properties_inner_vt( impl )->GetView( impl->inner, &view, NULL )) || !view)
        return FAILED(hr) ? hr : E_FAIL;
    hr = IMapView_HSTRING_IInspectable_QueryInterface( view, &IID_IIterable_IKeyValuePair_HSTRING_IInspectable,
                                                       (void **)&iterable );
    IMapView_HSTRING_IInspectable_Release( view );
    if (FAILED(hr) || !iterable) return hr;
    hr = IIterable_IKeyValuePair_HSTRING_IInspectable_First( iterable, iter );
    IIterable_IKeyValuePair_HSTRING_IInspectable_Release( iterable );
    return hr;
}

static HRESULT WINAPI core_properties_QueryInterface( IPropertySet *iface, REFIID iid, void **out )
{
    struct core_properties *impl = impl_from_core_properties_IPropertySet( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (!out) return E_POINTER;
    *out = NULL;

    if (IsEqualGUID( iid, &IID_IReferenceTracker_props ))
        return E_NOINTERFACE;

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IPropertySet ) ||
        IsEqualGUID( iid, &IID_IMap_HSTRING_IInspectable ) ||
        IsEqualGUID( iid, &IID_IObservableMap_HSTRING_IInspectable ) ||
        IsEqualGUID( iid, &IID_IMap_String_Object_props ) ||
        IsEqualGUID( iid, &IID_IObservableMap_String_Object_props ) ||
        IsEqualGUID( iid, &IID_IMap_Object_Object_props ) ||
        IsEqualGUID( iid, &IID_IResourceDictionary_props ) ||
        IsEqualGUID( iid, &IID_INotifyCollectionChanged_props ) ||
        IsEqualGUID( iid, &IID_INotifyPropertyChanged_props ))
    {
        *out = &impl->IPropertySet_iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IMapView_HSTRING_IInspectable ) ||
        IsEqualGUID( iid, &IID_IMapView_String_IInspectable_props ) ||
        IsEqualGUID( iid, &IID_IMapView_StringObject_IInspectable_props ) ||
        IsEqualGUID( iid, &IID_IMapView_String_Object_props ) ||
        IsEqualGUID( iid, &IID_IMapView_Object_Object_props ))
    {
        *out = &impl->IMapView_iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IIterable_IKeyValuePair_HSTRING_IInspectable ) ||
        IsEqualGUID( iid, &IID_IIterable_KeyValuePair_Object_Object_props ))
    {
        *out = &impl->IIterable_iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IWineSessionProperties ))
    {
        *out = &impl->IWineSessionProperties_iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG WINAPI core_properties_AddRef( IPropertySet *iface )
{
    struct core_properties *impl = impl_from_core_properties_IPropertySet( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI core_properties_Release( IPropertySet *iface )
{
    struct core_properties *impl = impl_from_core_properties_IPropertySet( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        SIZE_T i;
        for (i = 0; i < impl->key_count; i++)
            WindowsDeleteString( impl->keys[i] );
        free( impl->keys );
        impl->cs.DebugInfo->Spare[0] = 0;
        DeleteCriticalSection( &impl->cs );
        IPropertySet_Release( impl->inner );
        CoTaskMemFree( impl->iids_cache );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI core_properties_GetIids( IPropertySet *iface, ULONG *count, IID **iids )
{
    struct core_properties *impl = impl_from_core_properties_IPropertySet( iface );

    if (!impl->iids_cache)
    {
        const struct core_com_interface_entry entries[] =
        {
            { (void **)&core_properties_vtbl, (IID *)&IID_IPropertySet },
            { (void **)&core_properties_map_view_vtbl, (IID *)&IID_IMapView_HSTRING_IInspectable },
            { (void **)&core_properties_iterable_vtbl, (IID *)&IID_IIterable_IKeyValuePair_HSTRING_IInspectable },
        };
        struct core_com_interface_entry *array = CoTaskMemAlloc( sizeof(entries) );
        if (!array) return E_OUTOFMEMORY;
        memcpy( array, entries, sizeof(entries) );
        impl->iids_cache = array;
        impl->iids_count = ARRAY_SIZE(entries);
    }
    if (count) *count = impl->iids_count;
    if (iids) *iids = (IID *)impl->iids_cache;
    return S_OK;
}

static HRESULT WINAPI core_properties_GetRuntimeClassName( IPropertySet *iface, HSTRING *name )
{
    static const WCHAR cls[] = L"Windows.Foundation.Collections.PropertySet";
    if (!name) return E_POINTER;
    return WindowsCreateString( cls, ARRAY_SIZE(cls) - 1, name );
}

static HRESULT WINAPI core_properties_GetTrustLevel( IPropertySet *iface, TrustLevel *level )
{
    if (level) *level = FullTrust;
    return S_OK;
}

static HRESULT WINAPI core_properties_Lookup( IPropertySet *iface, HSTRING key, IInspectable **value )
{
    struct core_properties *impl = impl_from_core_properties_IPropertySet( iface );
    return core_properties_inner_vt( impl )->Lookup( impl->inner, key, value );
}

static HRESULT WINAPI core_properties_get_Size( IPropertySet *iface, UINT32 *size )
{
    struct core_properties *impl = impl_from_core_properties_IPropertySet( iface );
    return core_properties_inner_vt( impl )->get_Size( impl->inner, size );
}

static HRESULT WINAPI core_properties_HasKey( IPropertySet *iface, HSTRING key, boolean *found )
{
    struct core_properties *impl = impl_from_core_properties_IPropertySet( iface );
    return core_properties_inner_vt( impl )->HasKey( impl->inner, key, found );
}

static HRESULT WINAPI core_properties_GetView( IPropertySet *iface, IMapView_HSTRING_IInspectable **view )
{
    struct core_properties *impl = impl_from_core_properties_IPropertySet( iface );
    return core_properties_inner_vt( impl )->GetView( impl->inner, view, NULL );
}

static HRESULT WINAPI core_properties_Insert( IPropertySet *iface, HSTRING key, IInspectable *value, boolean *replaced )
{
    struct core_properties *impl = impl_from_core_properties_IPropertySet( iface );
    boolean local_replaced = FALSE;
    BOOL allocated = FALSE;
    HSTRING hkey;
    HRESULT hr;

    hkey = core_properties_resolve_key( key, &allocated );
    hr = core_properties_inner_vt( impl )->Insert( impl->inner, hkey, value, replaced ? replaced : &local_replaced );
    if (SUCCEEDED(hr) && hkey)
        core_properties_track_key( impl, hkey );
    if (allocated) WindowsDeleteString( hkey );
    return hr;
}

static HRESULT WINAPI core_properties_Remove( IPropertySet *iface, HSTRING key )
{
    struct core_properties *impl = impl_from_core_properties_IPropertySet( iface );
    BOOL allocated = FALSE;
    HSTRING hkey;
    HRESULT hr;

    hkey = core_properties_resolve_key( key, &allocated );
    hr = core_properties_inner_vt( impl )->Remove( impl->inner, hkey );
    if (SUCCEEDED(hr) && hkey)
        core_properties_untrack_key( impl, hkey );
    if (allocated) WindowsDeleteString( hkey );
    return hr;
}

static HRESULT WINAPI core_properties_Clear( IPropertySet *iface )
{
    struct core_properties *impl = impl_from_core_properties_IPropertySet( iface );
    HRESULT hr = core_properties_inner_vt( impl )->Clear( impl->inner );
    if (SUCCEEDED(hr)) core_properties_clear_keys( impl );
    return hr;
}

static HRESULT WINAPI core_properties_First( IPropertySet *iface, IIterator_IKeyValuePair_HSTRING_IInspectable **iter )
{
    struct core_properties *impl = impl_from_core_properties_IPropertySet( iface );
    return core_properties_first_impl( impl, iter );
}

static HRESULT WINAPI core_properties_add_MapChanged( IPropertySet *iface,
        IMapChangedEventHandler_HSTRING_IInspectable *handler, EventRegistrationToken *token )
{
    struct core_properties *impl = impl_from_core_properties_IPropertySet( iface );
    return core_properties_inner_vt( impl )->add_MapChanged( impl->inner, handler, token );
}

static HRESULT WINAPI core_properties_remove_MapChanged( IPropertySet *iface, EventRegistrationToken token )
{
    struct core_properties *impl = impl_from_core_properties_IPropertySet( iface );
    return core_properties_inner_vt( impl )->remove_MapChanged( impl->inner, token );
}

static HRESULT WINAPI core_properties_get_MapChanged( IPropertySet *iface,
        IMapChangedEventHandler_HSTRING_IInspectable **handler )
{
    struct core_properties *impl = impl_from_core_properties_IPropertySet( iface );
    return core_properties_inner_vt( impl )->get_MapChanged( impl->inner, handler );
}

static HRESULT WINAPI core_properties_add_CollectionChanged( IPropertySet *iface, void *handler, EventRegistrationToken *token )
{
    struct core_properties *impl = impl_from_core_properties_IPropertySet( iface );
    return core_properties_inner_vt( impl )->add_CollectionChanged( impl->inner, handler, token );
}

static HRESULT WINAPI core_properties_remove_CollectionChanged( IPropertySet *iface, EventRegistrationToken token )
{
    struct core_properties *impl = impl_from_core_properties_IPropertySet( iface );
    return core_properties_inner_vt( impl )->remove_CollectionChanged( impl->inner, token );
}

static HRESULT WINAPI core_properties_get_CollectionChanged( IPropertySet *iface, void **handler )
{
    struct core_properties *impl = impl_from_core_properties_IPropertySet( iface );
    return core_properties_inner_vt( impl )->get_CollectionChanged( impl->inner, handler );
}

static const struct core_properties_inner_vtbl core_properties_vtbl =
{
    core_properties_QueryInterface,
    core_properties_AddRef,
    core_properties_Release,
    core_properties_GetIids,
    core_properties_GetRuntimeClassName,
    core_properties_GetTrustLevel,
    core_properties_Lookup,
    core_properties_get_Size,
    core_properties_HasKey,
    (void *)core_properties_GetView,
    core_properties_Insert,
    core_properties_Remove,
    core_properties_Clear,
    core_properties_First,
    core_properties_add_MapChanged,
    core_properties_remove_MapChanged,
    core_properties_get_MapChanged,
    core_properties_add_CollectionChanged,
    core_properties_remove_CollectionChanged,
    core_properties_get_CollectionChanged,
    core_properties_add_CollectionChanged,
    core_properties_remove_CollectionChanged,
    core_properties_get_CollectionChanged,
};

static HRESULT WINAPI core_properties_mv_QueryInterface( IMapView_HSTRING_IInspectable *iface, REFIID iid, void **out )
{
    struct core_properties *impl = impl_from_core_properties_IMapView( iface );
    return core_properties_QueryInterface( &impl->IPropertySet_iface, iid, out );
}

static ULONG WINAPI core_properties_mv_AddRef( IMapView_HSTRING_IInspectable *iface )
{
    struct core_properties *impl = impl_from_core_properties_IMapView( iface );
    return core_properties_AddRef( &impl->IPropertySet_iface );
}

static ULONG WINAPI core_properties_mv_Release( IMapView_HSTRING_IInspectable *iface )
{
    struct core_properties *impl = impl_from_core_properties_IMapView( iface );
    return core_properties_Release( &impl->IPropertySet_iface );
}

static HRESULT WINAPI core_properties_mv_GetIids( IMapView_HSTRING_IInspectable *iface, ULONG *count, IID **iids )
{
    struct core_properties *impl = impl_from_core_properties_IMapView( iface );
    return core_properties_GetIids( &impl->IPropertySet_iface, count, iids );
}

static HRESULT WINAPI core_properties_mv_GetRuntimeClassName( IMapView_HSTRING_IInspectable *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.Foundation.Collections.IMapView`2", 40, name );
}

static HRESULT WINAPI core_properties_mv_GetTrustLevel( IMapView_HSTRING_IInspectable *iface, TrustLevel *level )
{
    if (level) *level = FullTrust;
    return S_OK;
}

static HRESULT WINAPI core_properties_mv_Lookup( IMapView_HSTRING_IInspectable *iface, HSTRING key, IInspectable **value )
{
    struct core_properties *impl = impl_from_core_properties_IMapView( iface );
    return core_properties_inner_vt( impl )->Lookup( impl->inner, key, value );
}

static HRESULT WINAPI core_properties_mv_get_Size( IMapView_HSTRING_IInspectable *iface, UINT32 *size )
{
    struct core_properties *impl = impl_from_core_properties_IMapView( iface );
    return core_properties_inner_vt( impl )->get_Size( impl->inner, size );
}

static HRESULT WINAPI core_properties_mv_HasKey( IMapView_HSTRING_IInspectable *iface, HSTRING key, boolean *found )
{
    struct core_properties *impl = impl_from_core_properties_IMapView( iface );
    return core_properties_inner_vt( impl )->HasKey( impl->inner, key, found );
}

static HRESULT WINAPI core_properties_mv_Split( IMapView_HSTRING_IInspectable *iface,
        IMapView_HSTRING_IInspectable **first, IMapView_HSTRING_IInspectable **second )
{
    struct core_properties *impl = impl_from_core_properties_IMapView( iface );
    IMapView_HSTRING_IInspectable *view = NULL;
    HRESULT hr;

    if (FAILED(hr = core_properties_inner_vt( impl )->GetView( impl->inner, &view, NULL )) || !view)
        return FAILED(hr) ? hr : E_FAIL;
    hr = IMapView_HSTRING_IInspectable_Split( view, first, second );
    IMapView_HSTRING_IInspectable_Release( view );
    return hr;
}

static const struct IMapView_HSTRING_IInspectableVtbl core_properties_map_view_vtbl =
{
    core_properties_mv_QueryInterface,
    core_properties_mv_AddRef,
    core_properties_mv_Release,
    core_properties_mv_GetIids,
    core_properties_mv_GetRuntimeClassName,
    core_properties_mv_GetTrustLevel,
    core_properties_mv_Lookup,
    core_properties_mv_get_Size,
    core_properties_mv_HasKey,
    core_properties_mv_Split,
};

static HRESULT WINAPI core_properties_ii_QueryInterface( IIterable_IKeyValuePair_HSTRING_IInspectable *iface, REFIID iid, void **out )
{
    struct core_properties *impl = impl_from_core_properties_IIterable( iface );
    return core_properties_QueryInterface( &impl->IPropertySet_iface, iid, out );
}

static ULONG WINAPI core_properties_ii_AddRef( IIterable_IKeyValuePair_HSTRING_IInspectable *iface )
{
    struct core_properties *impl = impl_from_core_properties_IIterable( iface );
    return core_properties_AddRef( &impl->IPropertySet_iface );
}

static ULONG WINAPI core_properties_ii_Release( IIterable_IKeyValuePair_HSTRING_IInspectable *iface )
{
    struct core_properties *impl = impl_from_core_properties_IIterable( iface );
    return core_properties_Release( &impl->IPropertySet_iface );
}

static HRESULT WINAPI core_properties_ii_GetIids( IIterable_IKeyValuePair_HSTRING_IInspectable *iface, ULONG *count, IID **iids )
{
    struct core_properties *impl = impl_from_core_properties_IIterable( iface );
    return core_properties_GetIids( &impl->IPropertySet_iface, count, iids );
}

static HRESULT WINAPI core_properties_ii_GetRuntimeClassName( IIterable_IKeyValuePair_HSTRING_IInspectable *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.Foundation.Collections.IIterable`1", 42, name );
}

static HRESULT WINAPI core_properties_ii_GetTrustLevel( IIterable_IKeyValuePair_HSTRING_IInspectable *iface, TrustLevel *level )
{
    if (level) *level = FullTrust;
    return S_OK;
}

static HRESULT WINAPI core_properties_ii_First( IIterable_IKeyValuePair_HSTRING_IInspectable *iface,
        IIterator_IKeyValuePair_HSTRING_IInspectable **iter )
{
    struct core_properties *impl = impl_from_core_properties_IIterable( iface );
    return core_properties_first_impl( impl, iter );
}

static const struct IIterable_IKeyValuePair_HSTRING_IInspectableVtbl core_properties_iterable_vtbl =
{
    core_properties_ii_QueryInterface,
    core_properties_ii_AddRef,
    core_properties_ii_Release,
    core_properties_ii_GetIids,
    core_properties_ii_GetRuntimeClassName,
    core_properties_ii_GetTrustLevel,
    core_properties_ii_First,
};

static HRESULT WINAPI core_properties_wine_QueryInterface( IWineSessionProperties *iface, REFIID iid, void **out )
{
    struct core_properties *impl = impl_from_core_properties_IWineSessionProperties( iface );
    return core_properties_QueryInterface( &impl->IPropertySet_iface, iid, out );
}

static ULONG WINAPI core_properties_wine_AddRef( IWineSessionProperties *iface )
{
    struct core_properties *impl = impl_from_core_properties_IWineSessionProperties( iface );
    return core_properties_AddRef( &impl->IPropertySet_iface );
}

static ULONG WINAPI core_properties_wine_Release( IWineSessionProperties *iface )
{
    struct core_properties *impl = impl_from_core_properties_IWineSessionProperties( iface );
    return core_properties_Release( &impl->IPropertySet_iface );
}

static HRESULT WINAPI core_properties_wine_Flush( IWineSessionProperties *iface )
{
    core_app_flush_properties();
    return S_OK;
}

static HRESULT WINAPI core_properties_wine_Reload( IWineSessionProperties *iface )
{
    struct core_properties *old = NULL;

    EnterCriticalSection( &core_properties_singleton_cs );
    old = the_core_properties;
    the_core_properties = NULL;
    LeaveCriticalSection( &core_properties_singleton_cs );
    if (old) IPropertySet_Release( &old->IPropertySet_iface );
    return S_OK;
}

static const struct IWineSessionPropertiesVtbl core_properties_wine_session_vtbl =
{
    core_properties_wine_QueryInterface,
    core_properties_wine_AddRef,
    core_properties_wine_Release,
    core_properties_wine_Flush,
    core_properties_wine_Reload,
};

static HRESULT core_properties_create( struct core_properties **out )
{
    static const WCHAR name[] = L"Windows.Foundation.Collections.PropertySet";
    struct core_properties *impl;
    HSTRING class_id;
    HRESULT hr;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->IPropertySet_iface.lpVtbl = (const struct IPropertySetVtbl *)&core_properties_vtbl;
    impl->IMapView_iface.lpVtbl = &core_properties_map_view_vtbl;
    impl->IIterable_iface.lpVtbl = &core_properties_iterable_vtbl;
    impl->IWineSessionProperties_iface.lpVtbl = &core_properties_wine_session_vtbl;
    impl->ref = 1;
    InitializeCriticalSectionEx( &impl->cs, 0, RTL_CRITICAL_SECTION_FLAG_FORCE_DEBUG_INFO );
    impl->cs.DebugInfo->Spare[0] = (DWORD_PTR)(__FILE__ ": core_properties.cs");

    if (FAILED(hr = WindowsCreateString( name, ARRAY_SIZE(name) - 1, &class_id )))
    {
        impl->cs.DebugInfo->Spare[0] = 0;
        DeleteCriticalSection( &impl->cs );
        free( impl );
        return hr;
    }
    hr = RoActivateInstance( class_id, (IInspectable **)&impl->inner );
    WindowsDeleteString( class_id );
    if (FAILED(hr))
    {
        impl->cs.DebugInfo->Spare[0] = 0;
        DeleteCriticalSection( &impl->cs );
        free( impl );
        return hr;
    }

    core_properties_load_impl( impl );
    *out = impl;
    return S_OK;
}

static HRESULT core_properties_ensure( IPropertySet **out )
{
    struct core_properties *props;
    HRESULT hr = S_OK;

    EnterCriticalSection( &core_properties_singleton_cs );
    if (!the_core_properties)
        hr = core_properties_create( &the_core_properties );
    props = the_core_properties;
    if (props && out) IPropertySet_AddRef( &props->IPropertySet_iface );
    LeaveCriticalSection( &core_properties_singleton_cs );

    if (out) *out = props ? &props->IPropertySet_iface : NULL;
    return hr;
}

void core_app_flush_properties( void )
{
    struct core_properties *props = NULL;

    EnterCriticalSection( &core_properties_singleton_cs );
    if ((props = the_core_properties))
        IPropertySet_AddRef( &props->IPropertySet_iface );
    LeaveCriticalSection( &core_properties_singleton_cs );

    if (props)
    {
        core_properties_flush_impl( props );
        IPropertySet_Release( &props->IPropertySet_iface );
    }
}

static HRESULT WINAPI core_application_get_Properties( ICoreApplication *iface, IPropertySet **value )
{
    TRACE( "iface %p, value %p\n", iface, value );
    if (!value) return E_POINTER;
    return core_properties_ensure( value );
}

static HRESULT WINAPI core_application_GetCurrentView( ICoreApplication *iface, ICoreApplicationView **value )
{
    FIXME( "iface %p, value %p semi-stub.\n", iface, value );
    if (!value) return E_POINTER;
    *value = core_application_view_get();
    return *value ? S_OK : E_OUTOFMEMORY;
}

/* ---- the shell's activation handshake, through wineuwp's in-process brokers -------------------
 *
 * A packaged app's CoreApplication::Run registers its activation object with the shell's
 * ActivatableApplicationRegistrar and hands the process state manager to RegisterWithPSM. The shell
 * then calls Activate back on the app, and the app's activation factory asks the shell's
 * CoreWindowBroker for the window factory that makes the view's CoreWindow. No prefix has those
 * brokers, so wineuwp provides all three in-process (dlls/wineuwp); this is the app side of it.
 *
 * The order is the shell's and XAML is strict about it: CreateCoreWindow, SetWindowAndGetDispatcher,
 * Load, PrepareToActivate, Activate. Configuring a CoreWindow twice is not harmless —
 * DXamlCore::ConfigureCoreWindow returns E_UNEXPECTED, XAML stows it and fail-fasts — so a brokered
 * run configures the window exactly once and skips the direct path below.
 *
 * These invariants are enforced inline, by the builtin modules: the windows.ui.xaml framework view
 * records the thread that Initialized it and its one window, and answers a foreign-thread
 * SetWindow RPC_E_WRONG_THREAD, a second window E_UNEXPECTED, Load-before-SetWindow E_UNEXPECTED,
 * and everything after Uninitialize E_APPLICATION_VIEW_EXITING. Native Windows twinapi.appcore /
 * windows.ui.xaml (if a prefix ever loads them) carry their own enforcement, which is unobservable
 * from here; the builtin path is the one this handshake runs on. The one-window-per-thread rule
 * (windows.ui's per-thread CoreWindow) is the anti-split invariant: a view whose CoreWindow and
 * dispatcher live on one thread cannot be torn across two, which is the failure the brokered
 * handshake is built to prevent.
 *
 * All of this is behind WINE_UWP_BROKERS. The unpackaged path (WinUI 3, dcompi, appresolver) is
 * byte-for-byte the path it has always been, and a brokered run that cannot reach the brokers falls
 * back to it.
 */
static const GUID CLSID_ActivatableApplicationRegistrar_ =
    { 0xdea794e0, 0x1c1d, 0x4363, { 0xb1, 0x71, 0x98, 0xd0, 0xb1, 0x70, 0x35, 0x86 } };
static const GUID IID_IActivatableApplicationRegistrar_ =
    { 0x036c57fc, 0xe73d, 0x4f3a, { 0xb0, 0x95, 0x8b, 0x81, 0x3f, 0xa2, 0xf7, 0x64 } };
static const GUID IID_IActivatableApplication_ =
    { 0x92696c00, 0x7578, 0x48e1, { 0xac, 0x1a, 0x2c, 0xa9, 0x09, 0xe2, 0xc8, 0xcf } };
static const GUID CLSID_CoreWindowBroker_ =
    { 0x3480a401, 0xbde9, 0x4407, { 0xbc, 0x02, 0x79, 0x8a, 0x86, 0x6a, 0xc0, 0x51 } };
static const GUID IID_ICoreWindowBroker_ =
    { 0x0f4accb1, 0xd8f9, 0x4011, { 0xba, 0x37, 0x25, 0x57, 0x92, 0x5a, 0x78, 0xcf } };
static const GUID IID_ICoreWindowFactory_ =
    { 0xcd292360, 0x2763, 0x4085, { 0x8a, 0x9f, 0x74, 0xb2, 0x24, 0xa2, 0x91, 0x75 } };
static const GUID SID_window_factory_ =
    { 0xd98fd14a, 0x522a, 0x4d59, { 0xb8, 0x75, 0x81, 0x1e, 0x83, 0x91, 0x9a, 0x9e } };
static const GUID CLSID_PsmBroker_ =
    { 0x02844640, 0xe37c, 0x4322, { 0xa3, 0xb8, 0x4c, 0x61, 0xa2, 0xe5, 0x88, 0x79 } };
static const GUID IID_IPsmBroker_ =
    { 0x33c255c3, 0x1a86, 0x45fc, { 0x94, 0x98, 0xf6, 0x0d, 0x8f, 0x56, 0xb9, 0xd3 } };
static const GUID IID_ICoreWindowNavigation_ =
    { 0xbb4cbbe9, 0x65af, 0x4730, { 0x9e, 0xd2, 0xab, 0xbd, 0xb5, 0xcc, 0x15, 0xb9 } };
static const GUID IID_INavigationClientPrepareToActivate_ =
    { 0x075be329, 0x825a, 0x4f2a, { 0xa8, 0xaf, 0xcd, 0x2f, 0x9a, 0x81, 0x02, 0xec } };

/* Slot layouts for the shell's broker faces, named from CoreUIComponents' symbols. The objects
 * behind them (wineuwp's in-process broker, the shell's window factory, windows.ui's CoreWindow)
 * are QI'd for the matching IID by the callers below; the structs replace the raw vtable indexes. */
struct core_window_broker;
struct core_window_broker_vtbl
{
    HRESULT (WINAPI *QueryInterface)( struct core_window_broker *This, REFIID riid, void **ppv );
    ULONG (WINAPI *AddRef)( struct core_window_broker *This );
    ULONG (WINAPI *Release)( struct core_window_broker *This );
    HRESULT (WINAPI *QueryService)( struct core_window_broker *This, const GUID *service,
                                    const GUID *iid, void **out );
};
struct core_window_broker
{
    const struct core_window_broker_vtbl *lpVtbl;
};

struct core_window_factory;
struct core_window_factory_vtbl
{
    HRESULT (WINAPI *QueryInterface)( struct core_window_factory *This, REFIID riid, void **ppv );
    ULONG (WINAPI *AddRef)( struct core_window_factory *This );
    ULONG (WINAPI *Release)( struct core_window_factory *This );
    HRESULT (WINAPI *GetIids)( struct core_window_factory *This, ULONG *count, IID **iids );
    HRESULT (WINAPI *GetRuntimeClassName)( struct core_window_factory *This, HSTRING *name );
    HRESULT (WINAPI *GetTrustLevel)( struct core_window_factory *This, TrustLevel *level );
    HRESULT (WINAPI *CreateCoreWindow)( struct core_window_factory *This, HSTRING title,
                                        IUnknown **window );
};
struct core_window_factory
{
    const struct core_window_factory_vtbl *lpVtbl;
};

struct activatable_application_registrar;
struct activatable_application_registrar_vtbl
{
    HRESULT (WINAPI *QueryInterface)( struct activatable_application_registrar *This, REFIID riid,
                                      void **ppv );
    ULONG (WINAPI *AddRef)( struct activatable_application_registrar *This );
    ULONG (WINAPI *Release)( struct activatable_application_registrar *This );
    HRESULT (WINAPI *RegisterActivatableApplication)( struct activatable_application_registrar *This,
                                                      IUnknown *app );
};
struct activatable_application_registrar
{
    const struct activatable_application_registrar_vtbl *lpVtbl;
};

struct core_window_navigation;
struct core_window_navigation_vtbl
{
    HRESULT (WINAPI *QueryInterface)( struct core_window_navigation *This, REFIID riid, void **ppv );
    ULONG (WINAPI *AddRef)( struct core_window_navigation *This );
    ULONG (WINAPI *Release)( struct core_window_navigation *This );
    HRESULT (WINAPI *GetIids)( struct core_window_navigation *This, ULONG *count, IID **iids );
    HRESULT (WINAPI *GetRuntimeClassName)( struct core_window_navigation *This, HSTRING *name );
    HRESULT (WINAPI *GetTrustLevel)( struct core_window_navigation *This, TrustLevel *level );
    HRESULT (WINAPI *get_NavigationClient)( struct core_window_navigation *This, IUnknown **client );
    HRESULT (WINAPI *put_NavigationClient)( struct core_window_navigation *This, IUnknown *client );
};
struct core_window_navigation
{
    const struct core_window_navigation_vtbl *lpVtbl;
};

struct navigation_client_prepare;
struct navigation_client_prepare_vtbl
{
    HRESULT (WINAPI *QueryInterface)( struct navigation_client_prepare *This, REFIID riid, void **ppv );
    ULONG (WINAPI *AddRef)( struct navigation_client_prepare *This );
    ULONG (WINAPI *Release)( struct navigation_client_prepare *This );
    HRESULT (WINAPI *GetIids)( struct navigation_client_prepare *This, ULONG *count, IID **iids );
    HRESULT (WINAPI *GetRuntimeClassName)( struct navigation_client_prepare *This, HSTRING *name );
    HRESULT (WINAPI *GetTrustLevel)( struct navigation_client_prepare *This, TrustLevel *level );
    HRESULT (WINAPI *PrepareToActivate)( struct navigation_client_prepare *This, UINT64 cookie,
                                         void **action );
};
struct navigation_client_prepare
{
    const struct navigation_client_prepare_vtbl *lpVtbl;
};

static BOOL use_shell_brokers( void )
{
    static int cached = -1;

    if (cached < 0) cached = GetEnvironmentVariableW( L"WINE_UWP_BROKERS", NULL, 0 ) ? 1 : 0;
    return cached == 1;
}

/* The shell's brokers speak private interfaces no header describes, so their vtables are indexed;
 * the structs above name the slots. */
static HRESULT broker_QueryService( IUnknown *broker, const GUID *service, const GUID *iid, void **out )
{
    struct core_window_broker *b = (struct core_window_broker *)broker; /* private face: no QI needed */

    return b->lpVtbl->QueryService( b, service, iid, out );
}
static HRESULT factory_CreateCoreWindow( IUnknown *factory, HSTRING title, IUnknown **window )
{
    struct core_window_factory *f = (struct core_window_factory *)factory; /* QI'd for IID_ICoreWindowFactory_ */

    return f->lpVtbl->CreateCoreWindow( f, title, window );
}
static HRESULT registrar_RegisterActivatableApplication( IUnknown *registrar, IUnknown *app )
{
    struct activatable_application_registrar *r = (struct activatable_application_registrar *)registrar; /* private face: no QI needed */

    return r->lpVtbl->RegisterActivatableApplication( r, app );
}

struct broker_app
{
    const void *lpVtbl;
    LONG ref;
    IFrameworkView *view;
    ICoreApplicationView *app_view;
};

static inline struct broker_app *impl_from_broker_app( void *iface )
{
    return CONTAINING_RECORD( iface, struct broker_app, lpVtbl );
}

static HRESULT WINAPI broker_app_QueryInterface( void *iface, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IActivatableApplication_ ))
    {
        *out = iface;
        InterlockedIncrement( &impl_from_broker_app( iface )->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI broker_app_AddRef( void *iface ) { return InterlockedIncrement( &impl_from_broker_app( iface )->ref ); }
static ULONG WINAPI broker_app_Release( void *iface ) { return InterlockedDecrement( &impl_from_broker_app( iface )->ref ); }
static HRESULT WINAPI broker_app_GetIids( void *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}
static HRESULT WINAPI broker_app_GetRuntimeClassName( void *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.ApplicationModel.Core.ApplicationActivationFactory", 61, name );
}
static HRESULT WINAPI broker_app_GetTrustLevel( void *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}

/* The view's Activated event: what a XAML app's Application.OnLaunched hangs off. */
static void invoke_view_activated( ICoreApplicationView *app_view )
{
    __x_ABI_CWindows_CApplicationModel_CActivation_CIActivatedEventArgs *args;

    if (!view_activated_handler) return;
    if (!(args = activated_event_args_create( 0 /* ActivationKind_Launch */ ))) return;
    TRACE( "invoking Activated handler %p\n", view_activated_handler );
    __FITypedEventHandler_2_Windows__CApplicationModel__CCore__CCoreApplicationView_Windows__CApplicationModel__CActivation__CIActivatedEventArgs_Invoke(
        view_activated_handler, app_view, args );
    __x_ABI_CWindows_CApplicationModel_CActivation_CIActivatedEventArgs_Release( args );
}

/* CoreApplicationView::PrepareToActivateAsync, from the shell's side: it asks the window's
 * navigation client (the property wineuwp put there when it made the window) for a completed
 * IAsyncAction. A window with no navigation client fails this with E_NOTIMPL, which is what the
 * shell treats as "this view cannot be activated". */
static HRESULT prepare_to_activate( IUnknown *window )
{
    IUnknown *host = NULL, *client = NULL, *prepare = NULL, *action = NULL;
    struct core_window_navigation *nav;
    struct navigation_client_prepare *prepare_face;
    HRESULT hr;

    if (FAILED( hr = IUnknown_QueryInterface( window, &IID_ICoreWindowNavigation_, (void **)&host ) ))
    {
        WARN( "CoreWindow has no navigation client property, hr %#lx.\n", hr );
        return hr;
    }
    nav = (struct core_window_navigation *)host;   /* QI'd for IID_ICoreWindowNavigation_ above */
    hr = nav->lpVtbl->get_NavigationClient( nav, &client );
    if (FAILED( hr ) || !client)
    {
        WARN( "no navigation client on the window, hr %#lx.\n", hr );
        if (FAILED( hr )) goto done;
        hr = E_NOTIMPL;
        goto done;
    }
    if (FAILED( hr = IUnknown_QueryInterface( client, &IID_INavigationClientPrepareToActivate_,
                                              (void **)&prepare ) ))
        goto done;
    prepare_face = (struct navigation_client_prepare *)prepare;   /* QI'd for the IID above */
    hr = prepare_face->lpVtbl->PrepareToActivate( prepare_face, 0, (void **)&action );
    TRACE( "PrepareToActivate hr %#lx, action %p.\n", hr, action );
    if (action) IUnknown_Release( action );
done:
    if (prepare) IUnknown_Release( prepare );
    if (client) IUnknown_Release( client );
    if (host) IUnknown_Release( host );
    return hr;
}

/* ApplicationActivationFactory::Activate: the shell's callback, which gets the view its CoreWindow
 * from the broker and runs the shell's view startup on it, on this thread — the view's apartment. */
static HRESULT WINAPI broker_app_Activate( void *iface, HSTRING aumid, HSTRING contract,
        HSTRING activation_id, HSTRING extra, IInspectable *args, UINT64 cookie, UINT32 options )
{
    struct broker_app *app = impl_from_broker_app( iface );
    IUnknown *broker = NULL, *service = NULL, *factory = NULL, *window = NULL;
    HSTRING title = NULL;
    HRESULT hr;

    TRACE( "aumid %s, contract %s.\n", debugstr_hstring( aumid ), debugstr_hstring( contract ) );

    /* CoreApplication::GetWindowFactory: CoCreate the broker, ask it for the window factory
     * service, QI the factory off the object it hands back. */
    if (FAILED( hr = CoCreateInstance( &CLSID_CoreWindowBroker_, NULL, CLSCTX_INPROC_SERVER,
                                       &IID_ICoreWindowBroker_, (void **)&broker ) ))
    {
        WARN( "no CoreWindowBroker in this prefix, hr %#lx.\n", hr );
        goto done;
    }
    if (FAILED( hr = broker_QueryService( broker, &SID_window_factory_, &IID_ICoreWindowFactory_,
                                          (void **)&service ) ))
    {
        WARN( "broker has no window factory service, hr %#lx.\n", hr );
        goto done;
    }
    if (FAILED( hr = IUnknown_QueryInterface( service, &IID_ICoreWindowFactory_, (void **)&factory ) ))
        goto done;
    /* the window's text is the app model entry id, which is what the shell names it with */
    if (SUCCEEDED( WindowsDuplicateString( aumid, &title ) ) && !title)
        WindowsCreateString( L"App", 3, &title );
    if (!title) WindowsCreateString( L"App", 3, &title );
    TRACE( "step 1/5 CreateCoreWindow.\n" );
    if (FAILED( hr = factory_CreateCoreWindow( factory, title, &window ) ))
    {
        WARN( "CreateCoreWindow failed, hr %#lx.\n", hr );
        goto done;
    }
    /* SetWindowAndGetDispatcher: the CoreWindow carries its own dispatcher, which is what the view
     * asks for alongside the window. This is the only place the window is configured. */
    TRACE( "step 2/5 SetWindowAndGetDispatcher.\n" );
    hr = IFrameworkView_SetWindow( app->view, (void *)window );
    if (hr == E_APPLICATION_VIEW_EXITING)
    {
        /* The view's Run has returned and Uninitialize ran: the shell's contract says an
         * activation of an exiting view is refused, and 0x8000001b is the value Windows answers. */
        TRACE( "step 2/5: view is exiting (Run returned), activation refused with E_APPLICATION_VIEW_EXITING.\n" );
        goto done;
    }
    TRACE( "step 3/5 Load, hr %#lx.\n", hr );
    if (SUCCEEDED( hr )) hr = IFrameworkView_Load( app->view, NULL );
    TRACE( "step 4/5 PrepareToActivate.\n" );
    if (SUCCEEDED( hr )) prepare_to_activate( window );   /* best effort; the shell does not stop on it */
    TRACE( "step 5/5 Activate: invoking Activated handler.\n" );
    if (SUCCEEDED( hr )) invoke_view_activated( app->app_view );
done:
    WindowsDeleteString( title );
    if (window) IUnknown_Release( window );
    if (factory) IUnknown_Release( factory );
    if (service) IUnknown_Release( service );
    if (broker) IUnknown_Release( broker );
    return hr;
}

static const void *broker_app_vtbl[] =
{
    broker_app_QueryInterface, broker_app_AddRef, broker_app_Release,
    broker_app_GetIids, broker_app_GetRuntimeClassName, broker_app_GetTrustLevel,
    broker_app_Activate,
};

static HRESULT core_application_broker_handshake( IFrameworkView *view, ICoreApplicationView *app_view )
{
    struct broker_app app;
    IUnknown *psm = NULL, *registrar = NULL;
    HRESULT hr;

    /* RegisterWithPSM CoCreates the process state manager's broker in-process on Windows; nothing
     * here tracks process state, and the app stops waiting on the service. */
    if (FAILED( hr = CoCreateInstance( &CLSID_PsmBroker_, NULL, CLSCTX_INPROC_SERVER,
                                       &IID_IPsmBroker_, (void **)&psm ) ))
    {
        WARN( "no PsmBroker in this prefix, hr %#lx.\n", hr );
        return hr;
    }
    IUnknown_Release( psm );

    if (FAILED( hr = CoCreateInstance( &CLSID_ActivatableApplicationRegistrar_, NULL, CLSCTX_INPROC_SERVER,
                                       &IID_IActivatableApplicationRegistrar_, (void **)&registrar ) ))
    {
        WARN( "no ActivatableApplicationRegistrar in this prefix, hr %#lx.\n", hr );
        return hr;
    }

    app.lpVtbl = broker_app_vtbl;
    app.ref = 1;
    app.view = view;
    app.app_view = app_view;

    /* CoreApplication::RegisterActivatableApplication, slot 3. The broker runs the activation on
     * this thread, which is the apartment CoreApplication::Run — and so the view — belongs to. */
    hr = registrar_RegisterActivatableApplication( registrar, (IUnknown *)&app.lpVtbl );
    TRACE( "RegisterActivatableApplication hr %#lx, app refcount %ld.\n", hr, app.ref );
    IUnknown_Release( registrar );
    return hr;
}

static HRESULT WINAPI core_application_Run( ICoreApplication *iface, IFrameworkViewSource *view_source )
{
    ICoreApplicationView *app_view;
    IFrameworkView *view;
    BOOL brokered;
    HRESULT hr;
    HMODULE cm;

    FIXME( "iface %p, view_source %p semi-stub.\n", iface, view_source );

    if (!view_source) return E_INVALIDARG;

    if (core_app_try_single_instance_redirect())
    {
        TRACE( "redirected activation to primary UWP instance; exiting secondary.\n" );
        ExitProcess( 0 );
    }

    core_properties_ensure( NULL );
    ensure_focus_watcher_started();

    /* Create the DispatcherQueue BEFORE FrameworkView.Initialize — the real
     * CoreApplication.Run does this. The DispatcherQueue drives the composition
     * tick (CCoreServices::Tick → DCompTreeHost → CreateConversationHost) that
     * creates the CoreUI ConversationServer (the dispatch handler at endpoint+0x58).
     * Without it the first CoreUI message dispatches against an empty table → AV. */
    /* Do not block on DispatcherQueueController in UWP CoreApplication.Run */
    if (0 && (cm = GetModuleHandleW( L"CoreMessagingXP.dll" )))
    {
        HRESULT (WINAPI *p_create_dqc)(void *, IInspectable **) =
            (void *)GetProcAddress( cm, "WinUICreateDispatcherQueueController" );
        if (p_create_dqc)
        {
            /* DispatcherQueueOptions { dwSize=12, threadType=2(CURRENT_THREAD), apartment=2(MTA) } */
            struct { DWORD dwSize; DWORD threadType; DWORD apartment; } opts = { 12, 2, 2 };
            IInspectable *dqc = NULL;
            HRESULT dqhr = p_create_dqc( &opts, &dqc );
            FIXME( "DispatcherQueueController hr=%#lx obj=%p\n", dqhr, dqc );
        }
    }

    if (FAILED(hr = IFrameworkViewSource_CreateView( view_source, &view )))
    {
        ERR( "CreateView failed %#lx.\n", hr );
        return hr;
    }

    if (!(app_view = core_application_view_get()))
    {
        IFrameworkView_Release( view );
        return E_OUTOFMEMORY;
    }

    /* drive the UWP view lifecycle: Initialize -> SetWindow -> Load -> Run */
    hr = IFrameworkView_Initialize( view, app_view );
    /* WINE_UWP_BROKERS: let the shell's brokers, provided in-process by wineuwp, hand the view its
     * CoreWindow and drive its startup. A run that cannot reach them falls back to the path below,
     * which is what an unpackaged app has always taken. */
    brokered = use_shell_brokers() && SUCCEEDED( core_application_broker_handshake( view, app_view ) );

    if (!brokered && SUCCEEDED(hr))
    {
        /* get a CoreWindow for SetWindow via the WinRT activation factory */
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
                if (!cw)
                {
                    HMODULE wui = LoadLibraryW( L"windows.ui.dll" );
                    if (wui)
                    {
                        void *(WINAPI *p_create)(void) = (void *)GetProcAddress( wui, "CoreWindow_Create" );
                        if (p_create) cw = p_create();
                    }
                }
                __x_ABI_CWindows_CUI_CCore_CICoreWindowStatic_Release( cws );
            }
            IActivationFactory_Release( cwf );
        }
        WindowsDeleteString( class_id );

        if (cw && !cached_main_core_window)
        {
            __x_ABI_CWindows_CUI_CCore_CICoreWindow_AddRef( cw );
            if (InterlockedCompareExchangePointer( (void *volatile *)&cached_main_core_window, cw, NULL ))
                __x_ABI_CWindows_CUI_CCore_CICoreWindow_Release( cw );
        }
        hr = IFrameworkView_SetWindow( view, cw );
        if (cw) __x_ABI_CWindows_CUI_CCore_CICoreWindow_Release( cw );
    }
    if (!brokered && SUCCEEDED(hr)) hr = IFrameworkView_Load( view, NULL );
    if (!brokered && view_activated_handler)
    {
        const WCHAR *uri = app_activation_current_uri();
        __x_ABI_CWindows_CApplicationModel_CActivation_CIActivatedEventArgs *args =
            uri ? activated_event_args_create_protocol( uri )
                : activated_event_args_create( 0 /* ActivationKind_Launch */ );
        if (args)
        {
            TRACE( "invoking Activated handler %p (uri %s)\n", view_activated_handler, debugstr_w( uri ) );
            __FITypedEventHandler_2_Windows__CApplicationModel__CCore__CCoreApplicationView_Windows__CApplicationModel__CActivation__CIActivatedEventArgs_Invoke(
                view_activated_handler, app_view, args );
            __x_ABI_CWindows_CApplicationModel_CActivation_CIActivatedEventArgs_Release( args );
        }
    }
    if (SUCCEEDED(hr)) hr = IFrameworkView_Run( view );
    IFrameworkView_Uninitialize( view );

    ICoreApplicationView_Release( app_view );
    IFrameworkView_Release( view );
    return hr;
}

static HRESULT WINAPI core_application_RunWithActivationFactories( ICoreApplication *iface,
        IGetActivationFactory *factory )
{
    FIXME( "iface %p, factory %p stub!\n", iface, factory );
    return E_NOTIMPL;
}

static const struct ICoreApplicationVtbl core_application_vtbl =
{
    core_application_QueryInterface,
    core_application_AddRef,
    core_application_Release,
    /* IInspectable methods */
    core_application_GetIids,
    core_application_GetRuntimeClassName,
    core_application_GetTrustLevel,
    /* ICoreApplication methods */
    core_application_get_Id,
    core_application_add_Suspending,
    core_application_remove_Suspending,
    core_application_add_Resuming,
    core_application_remove_Resuming,
    core_application_get_Properties,
    core_application_GetCurrentView,
    core_application_Run,
    core_application_RunWithActivationFactories,
};

/* ---- IStaticLifetime: GetCollection returns a process-wide IMap<HSTRING,IInspectable> ---- */

static inline struct factory *impl_from_IStaticLifetime( IStaticLifetime *iface )
{
    return CONTAINING_RECORD( iface, struct factory, IStaticLifetime_iface );
}

static HRESULT STDMETHODCALLTYPE static_lifetime_QueryInterface( IStaticLifetime *iface, REFIID iid, void **out )
{
    struct factory *impl = impl_from_IStaticLifetime( iface );
    return IActivationFactory_QueryInterface( &impl->IActivationFactory_iface, iid, out );
}
static ULONG STDMETHODCALLTYPE static_lifetime_AddRef( IStaticLifetime *iface )
{
    struct factory *impl = impl_from_IStaticLifetime( iface );
    return IActivationFactory_AddRef( &impl->IActivationFactory_iface );
}
static ULONG STDMETHODCALLTYPE static_lifetime_Release( IStaticLifetime *iface )
{
    struct factory *impl = impl_from_IStaticLifetime( iface );
    return IActivationFactory_Release( &impl->IActivationFactory_iface );
}
static HRESULT STDMETHODCALLTYPE static_lifetime_GetIids( IStaticLifetime *iface, ULONG *count, IID **iids )
{
    FIXME( "iface %p stub!\n", iface );
    return E_NOTIMPL;
}
static HRESULT STDMETHODCALLTYPE static_lifetime_GetRuntimeClassName( IStaticLifetime *iface, HSTRING *name )
{
    FIXME( "iface %p stub!\n", iface );
    return E_NOTIMPL;
}
static HRESULT STDMETHODCALLTYPE static_lifetime_GetTrustLevel( IStaticLifetime *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE static_lifetime_GetCollection( IStaticLifetime *iface,
        __FIMap_2_HSTRING_IInspectable **value )
{
    struct factory *impl = impl_from_IStaticLifetime( iface );

    TRACE( "iface %p, value %p.\n", iface, value );
    if (!value) return E_POINTER;

    if (!impl->static_map)
    {
        HRESULT hr = static_lifetime_map_create( &impl->static_map );
        if (FAILED(hr)) { *value = NULL; return hr; }
    }
    __FIMap_2_HSTRING_IInspectable_AddRef( impl->static_map );
    *value = impl->static_map;
    return S_OK;
}

/* Unknown IStaticLifetime methods 7..12 — no-op success. FrameworkUdk's
 * FinalUnhandledErrorDetected_RegisterHandler_Impl calls slot 11; returning S_OK
 * avoids the check_hresult throw + fail-fast. */
static HRESULT STDMETHODCALLTYPE static_lifetime_method7( IStaticLifetime *iface, void *a, void *b, void *c )
{ FIXME( "iface %p (slot7) semi-stub.\n", iface ); return S_OK; }
static HRESULT STDMETHODCALLTYPE static_lifetime_method8( IStaticLifetime *iface, void *a, void *b, void *c )
{ FIXME( "iface %p (slot8) semi-stub.\n", iface ); return S_OK; }
static HRESULT STDMETHODCALLTYPE static_lifetime_method9( IStaticLifetime *iface, void *a, void *b, void *c )
{ FIXME( "iface %p (slot9) semi-stub.\n", iface ); return S_OK; }
static HRESULT STDMETHODCALLTYPE static_lifetime_method10( IStaticLifetime *iface, void *a, void *b, void *c )
{ FIXME( "iface %p (slot10) semi-stub.\n", iface ); return S_OK; }
static HRESULT STDMETHODCALLTYPE static_lifetime_method11( IStaticLifetime *iface, void *a, void *b, void *c )
{ FIXME( "iface %p (slot11) a=%p b=%p c=%p semi-stub.\n", iface, a, b, c ); return S_OK; }
static HRESULT STDMETHODCALLTYPE static_lifetime_method12( IStaticLifetime *iface, void *a, void *b, void *c )
{ FIXME( "iface %p (slot12) semi-stub.\n", iface ); return S_OK; }

static const IStaticLifetimeVtbl static_lifetime_vtbl =
{
    static_lifetime_QueryInterface,
    static_lifetime_AddRef,
    static_lifetime_Release,
    static_lifetime_GetIids,
    static_lifetime_GetRuntimeClassName,
    static_lifetime_GetTrustLevel,
    static_lifetime_GetCollection,
    static_lifetime_method7,
    static_lifetime_method8,
    static_lifetime_method9,
    static_lifetime_method10,
    static_lifetime_method11,
    static_lifetime_method12,
};

/* ---- ICoreApplicationUnhandledError: UnhandledErrorDetected event (semi-stub) ----
 * XAML's DirectUI::FinalUnhandledErrorDetectedRegistration::Register (via FrameworkUdk
 * FinalUnhandledErrorDetected_RegisterHandler) registers for this event; E_NOINTERFACE
 * here makes XAML throw + fail-fast during init. */

static inline struct factory *impl_from_ICoreApplicationUnhandledError( ICoreApplicationUnhandledError *iface )
{
    return CONTAINING_RECORD( iface, struct factory, ICoreApplicationUnhandledError_iface );
}

static HRESULT WINAPI unhandled_error_QueryInterface( ICoreApplicationUnhandledError *iface, REFIID iid, void **out )
{
    struct factory *impl = impl_from_ICoreApplicationUnhandledError( iface );
    return IActivationFactory_QueryInterface( &impl->IActivationFactory_iface, iid, out );
}
static ULONG WINAPI unhandled_error_AddRef( ICoreApplicationUnhandledError *iface )
{
    struct factory *impl = impl_from_ICoreApplicationUnhandledError( iface );
    return IActivationFactory_AddRef( &impl->IActivationFactory_iface );
}
static ULONG WINAPI unhandled_error_Release( ICoreApplicationUnhandledError *iface )
{
    struct factory *impl = impl_from_ICoreApplicationUnhandledError( iface );
    return IActivationFactory_Release( &impl->IActivationFactory_iface );
}
static HRESULT WINAPI unhandled_error_GetIids( ICoreApplicationUnhandledError *iface, ULONG *count, IID **iids )
{
    FIXME( "iface %p stub!\n", iface );
    return E_NOTIMPL;
}
static HRESULT WINAPI unhandled_error_GetRuntimeClassName( ICoreApplicationUnhandledError *iface, HSTRING *name )
{
    FIXME( "iface %p stub!\n", iface );
    return E_NOTIMPL;
}
static HRESULT WINAPI unhandled_error_GetTrustLevel( ICoreApplicationUnhandledError *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}
static HRESULT WINAPI unhandled_error_add_UnhandledErrorDetected( ICoreApplicationUnhandledError *iface,
        __FIEventHandler_1_Windows__CApplicationModel__CCore__CUnhandledErrorDetectedEventArgs *handler,
        EventRegistrationToken *token )
{
    FIXME( "iface %p, handler %p, token %p semi-stub.\n", iface, handler, token );
    if (token) token->value = 0;
    return S_OK;
}
static HRESULT WINAPI unhandled_error_remove_UnhandledErrorDetected( ICoreApplicationUnhandledError *iface,
        EventRegistrationToken token )
{
    FIXME( "iface %p, token %#I64x semi-stub.\n", iface, token.value );
    return S_OK;
}

static const struct ICoreApplicationUnhandledErrorVtbl unhandled_error_vtbl =
{
    unhandled_error_QueryInterface,
    unhandled_error_AddRef,
    unhandled_error_Release,
    unhandled_error_GetIids,
    unhandled_error_GetRuntimeClassName,
    unhandled_error_GetTrustLevel,
    unhandled_error_add_UnhandledErrorDetected,
    unhandled_error_remove_UnhandledErrorDetected,
};

/* ---- ICoreApplication2: background/prelaunch events (semi-stub) ----
 * XAML's FrameworkView::Initialize QIs CoreApplication for ICoreApplication2;
 * E_NOINTERFACE here makes it fail-fast. */
static inline struct factory *impl_from_ICoreApplication2( ICoreApplication2 *iface )
{
    return CONTAINING_RECORD( iface, struct factory, ICoreApplication2_iface );
}
static HRESULT WINAPI core_application2_QueryInterface( ICoreApplication2 *iface, REFIID iid, void **out )
{
    return IActivationFactory_QueryInterface( &impl_from_ICoreApplication2( iface )->IActivationFactory_iface, iid, out );
}
static ULONG WINAPI core_application2_AddRef( ICoreApplication2 *iface )
{
    return IActivationFactory_AddRef( &impl_from_ICoreApplication2( iface )->IActivationFactory_iface );
}
static ULONG WINAPI core_application2_Release( ICoreApplication2 *iface )
{
    return IActivationFactory_Release( &impl_from_ICoreApplication2( iface )->IActivationFactory_iface );
}
static HRESULT WINAPI core_application2_GetIids( ICoreApplication2 *iface, ULONG *count, IID **iids )
{ FIXME( "iface %p stub!\n", iface ); return E_NOTIMPL; }
static HRESULT WINAPI core_application2_GetRuntimeClassName( ICoreApplication2 *iface, HSTRING *name )
{ FIXME( "iface %p stub!\n", iface ); return E_NOTIMPL; }
static HRESULT WINAPI core_application2_GetTrustLevel( ICoreApplication2 *iface, TrustLevel *level )
{ if (level) *level = BaseTrust; return S_OK; }
static HRESULT WINAPI core_application2_add_BackgroundActivated( ICoreApplication2 *iface,
        __FIEventHandler_1_Windows__CApplicationModel__CActivation__CBackgroundActivatedEventArgs *handler, EventRegistrationToken *token )
{ FIXME( "iface %p semi-stub.\n", iface ); if (token) token->value = 0; return S_OK; }
static HRESULT WINAPI core_application2_remove_BackgroundActivated( ICoreApplication2 *iface, EventRegistrationToken token )
{ FIXME( "iface %p semi-stub.\n", iface ); return S_OK; }
static HRESULT WINAPI core_application2_add_LeavingBackground( ICoreApplication2 *iface,
        __FIEventHandler_1_Windows__CApplicationModel__CLeavingBackgroundEventArgs *handler, EventRegistrationToken *token )
{
    TRACE( "iface %p, handler %p, token %p\n", iface, handler, token );
    return core_app_add_leaving_background( handler, token );
}
static HRESULT WINAPI core_application2_remove_LeavingBackground( ICoreApplication2 *iface, EventRegistrationToken token )
{
    TRACE( "iface %p, token %#I64x\n", iface, token.value );
    return core_app_remove_leaving_background( token );
}
static HRESULT WINAPI core_application2_add_EnteredBackground( ICoreApplication2 *iface,
        __FIEventHandler_1_Windows__CApplicationModel__CEnteredBackgroundEventArgs *handler, EventRegistrationToken *token )
{
    TRACE( "iface %p, handler %p, token %p\n", iface, handler, token );
    return core_app_add_entered_background( handler, token );
}
static HRESULT WINAPI core_application2_remove_EnteredBackground( ICoreApplication2 *iface, EventRegistrationToken token )
{
    TRACE( "iface %p, token %#I64x\n", iface, token.value );
    return core_app_remove_entered_background( token );
}
static HRESULT WINAPI core_application2_EnablePrelaunch( ICoreApplication2 *iface, boolean value )
{ FIXME( "iface %p, value %d semi-stub.\n", iface, value ); return S_OK; }

static const struct ICoreApplication2Vtbl core_application2_vtbl =
{
    core_application2_QueryInterface,
    core_application2_AddRef,
    core_application2_Release,
    core_application2_GetIids,
    core_application2_GetRuntimeClassName,
    core_application2_GetTrustLevel,
    core_application2_add_BackgroundActivated,
    core_application2_remove_BackgroundActivated,
    core_application2_add_LeavingBackground,
    core_application2_remove_LeavingBackground,
    core_application2_add_EnteredBackground,
    core_application2_remove_EnteredBackground,
    core_application2_EnablePrelaunch,
};

static inline struct factory *impl_from_IWineSessionProperties_factory( IWineSessionProperties *iface )
{
    return CONTAINING_RECORD( iface, struct factory, IWineSessionProperties_iface );
}

static HRESULT WINAPI factory_session_QueryInterface( IWineSessionProperties *iface, REFIID iid, void **out )
{
    struct factory *impl = impl_from_IWineSessionProperties_factory( iface );
    return IActivationFactory_QueryInterface( &impl->IActivationFactory_iface, iid, out );
}

static ULONG WINAPI factory_session_AddRef( IWineSessionProperties *iface )
{
    struct factory *impl = impl_from_IWineSessionProperties_factory( iface );
    return IActivationFactory_AddRef( &impl->IActivationFactory_iface );
}

static ULONG WINAPI factory_session_Release( IWineSessionProperties *iface )
{
    struct factory *impl = impl_from_IWineSessionProperties_factory( iface );
    return IActivationFactory_Release( &impl->IActivationFactory_iface );
}

static HRESULT WINAPI factory_session_Flush( IWineSessionProperties *iface )
{
    return core_properties_wine_Flush( iface );
}

static HRESULT WINAPI factory_session_Reload( IWineSessionProperties *iface )
{
    return core_properties_wine_Reload( iface );
}

static const struct IWineSessionPropertiesVtbl factory_session_vtbl =
{
    factory_session_QueryInterface,
    factory_session_AddRef,
    factory_session_Release,
    factory_session_Flush,
    factory_session_Reload,
};

DEFINE_IINSPECTABLE( immersive_app, ICoreImmersiveApplication, struct factory, IActivationFactory_iface )

static HRESULT WINAPI immersive_app_get_Views( ICoreImmersiveApplication *iface,
        __FIVectorView_1_Windows__CApplicationModel__CCore__CCoreApplicationView **value )
{
    FIXME( "iface %p, value %p stub!\n", iface, value );
    if (value) *value = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI immersive_app_CreateNewView( ICoreImmersiveApplication *iface,
        HSTRING runtime_type, HSTRING entry_point, ICoreApplicationView **view )
{
    TRACE( "iface %p, runtime_type %s, entry_point %s, view %p\n",
           iface, debugstr_hstring( runtime_type ), debugstr_hstring( entry_point ), view );
    if (!view) return E_POINTER;
    *view = core_application_view_get();
    return *view ? S_OK : E_OUTOFMEMORY;
}

static HRESULT WINAPI immersive_app_get_MainView( ICoreImmersiveApplication *iface,
        ICoreApplicationView **value )
{
    TRACE( "iface %p, value %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = core_application_view_get();
    return *value ? S_OK : E_OUTOFMEMORY;
}

static const struct ICoreImmersiveApplicationVtbl immersive_app_vtbl =
{
    immersive_app_QueryInterface,
    immersive_app_AddRef,
    immersive_app_Release,
    immersive_app_GetIids,
    immersive_app_GetRuntimeClassName,
    immersive_app_GetTrustLevel,
    immersive_app_get_Views,
    immersive_app_CreateNewView,
    immersive_app_get_MainView,
};

static struct factory factory =
{
    {&activation_factory_vtbl},
    {&core_application_vtbl},
    {&core_application2_vtbl},
    {&unhandled_error_vtbl},
    {&static_lifetime_vtbl},
    {&factory_session_vtbl},
    {&immersive_app_vtbl},
    NULL,
    1,
};

IActivationFactory *core_application_factory = &factory.IActivationFactory_iface;

void *activated_event_args_create_for_uri( const WCHAR *uri )
{
    return (void *)(uri ? activated_event_args_create_protocol( uri ) : activated_event_args_create( 0 ));
}

void core_app_fire_view_activated( const WCHAR *uri )
{
    __FITypedEventHandler_2_Windows__CApplicationModel__CCore__CCoreApplicationView_Windows__CApplicationModel__CActivation__CIActivatedEventArgs *handler = view_activated_handler;
    __x_ABI_CWindows_CApplicationModel_CActivation_CIActivatedEventArgs *args;
    ICoreApplicationView *view = NULL;

    if (handler && (args = (uri && uri[0]) ? activated_event_args_create_protocol( uri )
                                           : activated_event_args_create( 0 )))
    {
        impl_from_IActivatedEventArgs( args )->prev_state = 1; /* ApplicationExecutionState_Running */
        if ((view = core_application_view_get()))
            __FITypedEventHandler_2_Windows__CApplicationModel__CCore__CCoreApplicationView_Windows__CApplicationModel__CActivation__CIActivatedEventArgs_Invoke( handler, view, args );
        ICoreApplicationView_Release( view );
        __x_ABI_CWindows_CApplicationModel_CActivation_CIActivatedEventArgs_Release( args );
    }
}
