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

struct core_application_view_title_bar
{
    ICoreApplicationViewTitleBar ICoreApplicationViewTitleBar_iface;
    LONG ref;
    boolean extend_view_into_title_bar;
    __FITypedEventHandler_2_Windows__CApplicationModel__CCore__CCoreApplicationViewTitleBar_IInspectable *is_visible_changed_handler;
    __FITypedEventHandler_2_Windows__CApplicationModel__CCore__CCoreApplicationViewTitleBar_IInspectable *layout_metrics_changed_handler;
};

static HRESULT core_application_view_title_bar_create( ICoreApplicationViewTitleBar **value );

struct core_application_view
{
    ICoreApplicationView ICoreApplicationView_iface;
    ICoreApplicationView2 ICoreApplicationView2_iface;
    ICoreApplicationView3 ICoreApplicationView3_iface;
    LONG ref;
    ICoreApplicationViewTitleBar *title_bar;
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

struct activated_event_args
{
    __x_ABI_CWindows_CApplicationModel_CActivation_CIActivatedEventArgs IActivatedEventArgs_iface;
    const void *ILaunchActivatedEventArgs_vtbl;
    const void *ILaunchActivatedEventArgs2_vtbl;
    const void *IPrelaunchActivatedEventArgs_vtbl;
    const void *IActivatedEventArgsWithUser_vtbl;
    const void *IApplicationViewActivatedEventArgs_vtbl;
    const void *IViewSwitcherProvider_vtbl;
    void *splash;
    LONG ref;
    __x_ABI_CWindows_CApplicationModel_CActivation_CActivationKind kind;
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
        free( impl );
    }
    return ref;
}
static HRESULT WINAPI activated_event_args_GetIids( __x_ABI_CWindows_CApplicationModel_CActivation_CIActivatedEventArgs *iface, ULONG *count, IID **iids )
{ FIXME( "stub\n" ); return E_NOTIMPL; }
static HRESULT WINAPI activated_event_args_GetRuntimeClassName( __x_ABI_CWindows_CApplicationModel_CActivation_CIActivatedEventArgs *iface, HSTRING *name )
{
    if (!name) return E_POINTER;
    return WindowsCreateString( L"Windows.ApplicationModel.Activation.LaunchActivatedEventArgs", 59, name );
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
    if (!value) return E_POINTER;
    *value = 0; /* NotRunning */
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
static HRESULT WINAPI splash_get_ImageLocation( void *iface, Rect *rect )
{
    if (!rect) return E_POINTER;
    rect->X = rect->Y = 0.0f;
    rect->Width = 1024.0f;
    rect->Height = 768.0f;
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
    return WindowsCreateString( L"none", 4, value );
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
    impl->splash = create_splash_screen();
    impl->ref = 1;
    impl->kind = kind;
    return &impl->IActivatedEventArgs_iface;
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

static HRESULT WINAPI core_application_view_title_bar_QueryInterface( ICoreApplicationViewTitleBar *iface,
        REFIID iid, void **out )
{
    struct core_application_view_title_bar *impl = impl_from_ICoreApplicationViewTitleBar( iface );

    TRACE( "iface %p, iid %s, out %p\n", iface, debugstr_guid( iid ), out );

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
        if (impl->is_visible_changed_handler)
            __FITypedEventHandler_2_Windows__CApplicationModel__CCore__CCoreApplicationViewTitleBar_IInspectable_Release( impl->is_visible_changed_handler );
        if (impl->layout_metrics_changed_handler)
            __FITypedEventHandler_2_Windows__CApplicationModel__CCore__CCoreApplicationViewTitleBar_IInspectable_Release( impl->layout_metrics_changed_handler );
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
    impl->extend_view_into_title_bar = value;
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
    TRACE( "iface %p, value %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = 0.0;
    return S_OK;
}

static HRESULT WINAPI core_application_view_title_bar_get_Height( ICoreApplicationViewTitleBar *iface,
        DOUBLE *value )
{
    TRACE( "iface %p, value %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = 32.0;
    return S_OK;
}

static HRESULT WINAPI core_application_view_title_bar_add_LayoutMetricsChanged( ICoreApplicationViewTitleBar *iface,
        __FITypedEventHandler_2_Windows__CApplicationModel__CCore__CCoreApplicationViewTitleBar_IInspectable *handler,
        EventRegistrationToken *token )
{
    struct core_application_view_title_bar *impl = impl_from_ICoreApplicationViewTitleBar( iface );
    TRACE( "iface %p, handler %p, token %p\n", iface, handler, token );
    if (token) token->value = 1;
    if (handler)
    {
        __FITypedEventHandler_2_Windows__CApplicationModel__CCore__CCoreApplicationViewTitleBar_IInspectable_AddRef( handler );
        if (impl->layout_metrics_changed_handler)
            __FITypedEventHandler_2_Windows__CApplicationModel__CCore__CCoreApplicationViewTitleBar_IInspectable_Release( impl->layout_metrics_changed_handler );
        impl->layout_metrics_changed_handler = handler;
    }
    return S_OK;
}

static HRESULT WINAPI core_application_view_title_bar_remove_LayoutMetricsChanged( ICoreApplicationViewTitleBar *iface,
        EventRegistrationToken token )
{
    struct core_application_view_title_bar *impl = impl_from_ICoreApplicationViewTitleBar( iface );
    TRACE( "iface %p, token %#I64x\n", iface, token.value );
    if (impl->layout_metrics_changed_handler)
    {
        __FITypedEventHandler_2_Windows__CApplicationModel__CCore__CCoreApplicationViewTitleBar_IInspectable_Release( impl->layout_metrics_changed_handler );
        impl->layout_metrics_changed_handler = NULL;
    }
    return S_OK;
}

static HRESULT WINAPI core_application_view_title_bar_get_IsVisible( ICoreApplicationViewTitleBar *iface,
        boolean *value )
{
    TRACE( "iface %p, value %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = TRUE;
    return S_OK;
}

static HRESULT WINAPI core_application_view_title_bar_add_IsVisibleChanged( ICoreApplicationViewTitleBar *iface,
        __FITypedEventHandler_2_Windows__CApplicationModel__CCore__CCoreApplicationViewTitleBar_IInspectable *handler,
        EventRegistrationToken *token )
{
    struct core_application_view_title_bar *impl = impl_from_ICoreApplicationViewTitleBar( iface );
    TRACE( "iface %p, handler %p, token %p\n", iface, handler, token );
    if (token) token->value = 1;
    if (handler)
    {
        __FITypedEventHandler_2_Windows__CApplicationModel__CCore__CCoreApplicationViewTitleBar_IInspectable_AddRef( handler );
        if (impl->is_visible_changed_handler)
            __FITypedEventHandler_2_Windows__CApplicationModel__CCore__CCoreApplicationViewTitleBar_IInspectable_Release( impl->is_visible_changed_handler );
        impl->is_visible_changed_handler = handler;
    }
    return S_OK;
}

static HRESULT WINAPI core_application_view_title_bar_remove_IsVisibleChanged( ICoreApplicationViewTitleBar *iface,
        EventRegistrationToken token )
{
    struct core_application_view_title_bar *impl = impl_from_ICoreApplicationViewTitleBar( iface );
    TRACE( "iface %p, token %#I64x\n", iface, token.value );
    if (impl->is_visible_changed_handler)
    {
        __FITypedEventHandler_2_Windows__CApplicationModel__CCore__CCoreApplicationViewTitleBar_IInspectable_Release( impl->is_visible_changed_handler );
        impl->is_visible_changed_handler = NULL;
    }
    return S_OK;
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
    impl->ref = 1;
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

static ICoreApplicationView *core_application_view_get(void)
{
    if (!the_application_view)
    {
        struct core_application_view *impl;
        if (!(impl = calloc( 1, sizeof(*impl) ))) return NULL;
        impl->ICoreApplicationView_iface.lpVtbl = &core_application_view_vtbl;
        impl->ICoreApplicationView2_iface.lpVtbl = &core_application_view2_vtbl;
        impl->ICoreApplicationView3_iface.lpVtbl = &core_application_view3_vtbl;
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
    FIXME( "iface %p, value %p semi-stub.\n", iface, value );
    if (!value) return E_POINTER;
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
    FIXME( "iface %p, handler %p, token %p semi-stub.\n", iface, handler, token );
    if (token) token->value = 0;
    return S_OK;
}

static HRESULT WINAPI core_application_remove_Resuming( ICoreApplication *iface, EventRegistrationToken token )
{
    FIXME( "iface %p, token %#I64x semi-stub.\n", iface, token.value );
    return S_OK;
}

static HRESULT WINAPI core_application_get_Properties( ICoreApplication *iface, IPropertySet **value )
{
    static const WCHAR name[] = L"Windows.Foundation.Collections.PropertySet";
    static IPropertySet *properties;
    HSTRING class_id;
    HRESULT hr;

    TRACE( "iface %p, value %p\n", iface, value );
    if (!value) return E_POINTER;
    if (properties)
    {
        *value = properties;
        IPropertySet_AddRef( properties );
        return S_OK;
    }

    WindowsCreateString( name, wcslen(name), &class_id );
    hr = RoActivateInstance( class_id, (IInspectable **)&properties );
    WindowsDeleteString( class_id );
    if (SUCCEEDED(hr))
    {
        *value = properties;
        IPropertySet_AddRef( properties );
    }
    return hr;
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

static BOOL use_shell_brokers( void )
{
    static int cached = -1;

    if (cached < 0) cached = GetEnvironmentVariableW( L"WINE_UWP_BROKERS", NULL, 0 ) ? 1 : 0;
    return cached == 1;
}

/* The shell's brokers speak private interfaces no header describes, so their vtables are indexed. */
static HRESULT broker_QueryService( IUnknown *broker, const GUID *service, const GUID *iid, void **out )
{
    HRESULT (WINAPI *func)( void *, const GUID *, const GUID *, void ** ) = (void *)(*(void ***)broker)[3];

    return func( broker, service, iid, out );
}
static HRESULT factory_CreateCoreWindow( IUnknown *factory, HSTRING title, IUnknown **window )
{
    HRESULT (WINAPI *func)( void *, HSTRING, void ** ) = (void *)(*(void ***)factory)[6];

    return func( factory, title, (void **)window );
}
static HRESULT registrar_RegisterActivatableApplication( IUnknown *registrar, IUnknown *app )
{
    HRESULT (WINAPI *func)( void *, IUnknown * ) = (void *)(*(void ***)registrar)[3];

    return func( registrar, app );
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
    HRESULT (WINAPI *get_NavigationClient)( void *, IUnknown ** );
    HRESULT (WINAPI *PrepareToActivate)( void *, UINT64, void ** );
    HRESULT hr;

    if (FAILED( hr = IUnknown_QueryInterface( window, &IID_ICoreWindowNavigation_, (void **)&host ) ))
    {
        WARN( "CoreWindow has no navigation client property, hr %#lx.\n", hr );
        return hr;
    }
    get_NavigationClient = (void *)(*(void ***)host)[6];
    hr = get_NavigationClient( host, &client );
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
    PrepareToActivate = (void *)(*(void ***)prepare)[6];
    hr = PrepareToActivate( prepare, 0, (void **)&action );
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

        hr = IFrameworkView_SetWindow( view, cw );
        if (cw) __x_ABI_CWindows_CUI_CCore_CICoreWindow_Release( cw );
    }
    if (!brokered && SUCCEEDED(hr)) hr = IFrameworkView_Load( view, NULL );
    if (!brokered && view_activated_handler)
    {
        __x_ABI_CWindows_CApplicationModel_CActivation_CIActivatedEventArgs *args =
            activated_event_args_create( 0 /* ActivationKind_Launch */ );
        if (args)
        {
            TRACE( "invoking Activated handler %p\n", view_activated_handler );
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
{ FIXME( "iface %p semi-stub.\n", iface ); if (token) token->value = 0; return S_OK; }
static HRESULT WINAPI core_application2_remove_LeavingBackground( ICoreApplication2 *iface, EventRegistrationToken token )
{ FIXME( "iface %p semi-stub.\n", iface ); return S_OK; }
static HRESULT WINAPI core_application2_add_EnteredBackground( ICoreApplication2 *iface,
        __FIEventHandler_1_Windows__CApplicationModel__CEnteredBackgroundEventArgs *handler, EventRegistrationToken *token )
{ FIXME( "iface %p semi-stub.\n", iface ); if (token) token->value = 0; return S_OK; }
static HRESULT WINAPI core_application2_remove_EnteredBackground( ICoreApplication2 *iface, EventRegistrationToken token )
{ FIXME( "iface %p semi-stub.\n", iface ); return S_OK; }
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

static struct factory factory =
{
    {&activation_factory_vtbl},
    {&core_application_vtbl},
    {&core_application2_vtbl},
    {&unhandled_error_vtbl},
    {&static_lifetime_vtbl},
    NULL,
    1,
};

IActivationFactory *core_application_factory = &factory.IActivationFactory_iface;

/* ---- Windows.ApplicationModel.AppInstance ---- */

static const GUID IID_IAppInstanceStatics = {0x9d11e77f, 0x9ea6, 0x47af, {0xa6, 0xec, 0x46, 0x78, 0x4c, 0x5b, 0xa2, 0x54}};
static const GUID IID_IMicrosoftAppInstanceStatics = {0x4f414b25, 0x8330, 0x5a9b, {0xbb, 0xc1, 0x82, 0x29, 0xd4, 0x79, 0x64, 0x9d}};
static const GUID IID_IMicrosoftAppInstanceStatics2 = {0xfe9f1885, 0x7160, 0x5397, {0xba, 0x9b, 0x58, 0x90, 0xb2, 0x4f, 0xdc, 0x04}};
static const GUID IID_IMicrosoftAppInstance = {0x75766ae4, 0x0239, 0x5a26, {0xb9, 0xda, 0xd5, 0xbf, 0xc7, 0x5a, 0x48, 0x66}};

static const GUID IID_IWeakReference = {0x00000037, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
static const GUID IID_IWeakReferenceSource = {0x00000038, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};

typedef HRESULT (STDMETHODCALLTYPE *qi_fn)( const void *iface, REFIID iid, void **out );

/*
 * IWeakReferenceSource for the AppLifecycle objects.  Every WinRT server object
 * answers IWeakReferenceSource: the .NET/WinRT callable wrapper, the C++/WinRT
 * weak_ref helper and the event registration code all QI for it in order to
 * keep a non-owning reference, and callers QI it on the AppInstance object
 * handed out by IMicrosoftAppInstanceStatics::RecommendedInstance.  The objects
 * below are static singletons with a fixed reference count, so their weak
 * reference always resolves back to the owner it was created from.
 */
struct static_weakref
{
    const void *vtbl;
    const void *owner;
    qi_fn qi;
};

struct static_weakref_source
{
    const void *vtbl;
    const struct static_weakref *weakref;
};

static struct static_weakref app_instance_weakref;
static struct static_weakref app_instance_statics_weakref;
static struct static_weakref app_activation_args_weakref;
static struct static_weakref_source app_instance_weakref_source;
static struct static_weakref_source app_instance_statics_weakref_source;
static struct static_weakref_source app_activation_args_weakref_source;

static HRESULT WINAPI static_weakref_QueryInterface( void *iface, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IWeakReference ))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI static_weakref_AddRef( void *iface ) { return 2; }
static ULONG WINAPI static_weakref_Release( void *iface ) { return 1; }
static HRESULT WINAPI static_weakref_Resolve( void *iface, REFIID iid, void **out )
{
    struct static_weakref *ref = (struct static_weakref *)iface;

    return ref->qi( ref->owner, iid, out );
}
static const void *static_weakref_vtbl[] =
{
    static_weakref_QueryInterface,
    static_weakref_AddRef,
    static_weakref_Release,
    static_weakref_Resolve,
};

static HRESULT WINAPI static_weakref_source_GetWeakReference( void *iface, void **ref )
{
    struct static_weakref_source *source = (struct static_weakref_source *)iface;

    if (!ref) return E_POINTER;
    *ref = (void *)source->weakref;
    return S_OK;
}

struct app_instance_statics
{
    IActivationFactory IActivationFactory_iface;
    IInspectable IAppInstanceStatics_iface;
    LONG ref;
};

static inline struct app_instance_statics *impl_from_IActivationFactory_app_instance( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct app_instance_statics, IActivationFactory_iface );
}

static inline struct app_instance_statics *impl_from_IAppInstanceStatics( IInspectable *iface )
{
    return CONTAINING_RECORD( iface, struct app_instance_statics, IAppInstanceStatics_iface );
}

static HRESULT WINAPI app_instance_statics_QueryInterface( IInspectable *iface, REFIID iid, void **out )
{
    struct app_instance_statics *impl = impl_from_IAppInstanceStatics( iface );
    TRACE( "app_instance_statics QI: %s\n", debugstr_guid( iid ) );
    if (IsEqualGUID( iid, &IID_IWeakReferenceSource ))
    {
        *out = &app_instance_statics_weakref_source;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IAppInstanceStatics ) ||
        IsEqualGUID( iid, &IID_IMicrosoftAppInstanceStatics ) ||
        IsEqualGUID( iid, &IID_IMicrosoftAppInstanceStatics2 ))
    {
        *out = &impl->IAppInstanceStatics_iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    TRACE( "app_instance_statics QI NOT IMPLEMENTED: %s\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI app_instance_statics_AddRef( IInspectable *iface )
{
    struct app_instance_statics *impl = impl_from_IAppInstanceStatics( iface );
    return InterlockedIncrement( &impl->ref );
}
static ULONG WINAPI app_instance_statics_Release( IInspectable *iface )
{
    struct app_instance_statics *impl = impl_from_IAppInstanceStatics( iface );
    return InterlockedDecrement( &impl->ref );
}
static HRESULT WINAPI app_instance_statics_GetIids( IInspectable *iface, ULONG *count, IID **iids )
{ FIXME( "stub\n" ); return E_NOTIMPL; }
static HRESULT WINAPI app_instance_statics_GetRuntimeClassName( IInspectable *iface, HSTRING *name )
{ FIXME( "stub\n" ); return E_NOTIMPL; }
static HRESULT WINAPI app_instance_statics_GetTrustLevel( IInspectable *iface, TrustLevel *level )
{ if (level) *level = BaseTrust; return S_OK; }

static const GUID IID_IReferenceTracker = {0x11d3b13a, 0x180e, 0x4789, {0xa8, 0xbe, 0x77, 0x12, 0x88, 0x28, 0x93, 0xe6}};
static const GUID IID_IReferenceTrackerTarget = {0x5c13e51c, 0x4f32, 0x4726, {0xa3, 0xfd, 0xf3, 0xed, 0xd6, 0x3d, 0xa3, 0xa0}};
static const GUID IID_IUwpAppInstance = {0x6756488b, 0xdd4d, 0x4c26, {0xa5, 0x6e, 0xd3, 0xe4, 0xce, 0x07, 0xd8, 0x26}};

static HRESULT WINAPI app_instance_QueryInterface( IInspectable *iface, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IReferenceTracker ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }

    TRACE( "app_instance QI: %s\n", debugstr_guid( iid ) );
    if (IsEqualGUID( iid, &IID_IWeakReferenceSource ))
    {
        *out = &app_instance_weakref_source;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IUwpAppInstance ) ||
        IsEqualGUID( iid, &IID_IMicrosoftAppInstance ))
    {
        *out = iface;
        return S_OK;
    }
    TRACE( "app_instance QI NOT IMPLEMENTED: %s\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI app_instance_AddRef( IInspectable *iface ) { return 2; }
static ULONG WINAPI app_instance_Release( IInspectable *iface ) { return 1; }
static HRESULT WINAPI app_instance_GetIids( IInspectable *iface, ULONG *c, IID **i ) { return E_NOTIMPL; }
static HRESULT WINAPI app_instance_GetRuntimeClassName( IInspectable *iface, HSTRING *n )
{
    return WindowsCreateString( L"Microsoft.Windows.AppLifecycle.AppInstance", 42, n );
}
static HRESULT WINAPI app_instance_GetTrustLevel( IInspectable *iface, TrustLevel *l )
{
    if (l) *l = BaseTrust; return S_OK;
}
static const GUID IID_IAppActivationArguments = {0x14f99eaf, 0x1580, 0x5062, {0xbd, 0xc8, 0xd5, 0xd1, 0xc3, 0x11, 0x38, 0xfb}};

static HRESULT WINAPI app_activation_args_QueryInterface( void *iface, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IReferenceTracker ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }

    if (IsEqualGUID( iid, &IID_IWeakReferenceSource ))
    {
        *out = &app_activation_args_weakref_source;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IAppActivationArguments ))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI app_activation_args_AddRef( void *iface ) { return 2; }
static ULONG WINAPI app_activation_args_Release( void *iface ) { return 1; }
static HRESULT WINAPI app_activation_args_GetIids( void *iface, ULONG *c, IID **i ) { return E_NOTIMPL; }
static HRESULT WINAPI app_activation_args_GetRuntimeClassName( void *iface, HSTRING *n )
{
    return WindowsCreateString( L"Microsoft.Windows.AppLifecycle.AppActivationArguments", 53, n );
}
static HRESULT WINAPI app_activation_args_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI app_activation_args_get_Kind( void *iface, UINT32 *kind )
{
    TRACE( "app_activation_args_get_Kind called\n" );
    if (kind) *kind = 0 /* ExtendedActivationKind_Launch */;
    return S_OK;
}
static HRESULT WINAPI app_activation_args_get_Data( void *iface, void **data )
{
    TRACE( "app_activation_args_get_Data called\n" );
    if (!data) return E_POINTER;
    *data = (void *)activated_event_args_create( 0 );
    return S_OK;
}
static const void *app_activation_args_vtbl[] =
{
    app_activation_args_QueryInterface,
    app_activation_args_AddRef,
    app_activation_args_Release,
    app_activation_args_GetIids,
    app_activation_args_GetRuntimeClassName,
    app_activation_args_GetTrustLevel,
    app_activation_args_get_Data,
    app_activation_args_get_Kind,
};
static struct { const void *vtbl; } app_activation_args_obj = { app_activation_args_vtbl };

static const void *app_instance_vtbl[];
struct current_app_instance_impl
{
    const void *vtbl;
};
static struct current_app_instance_impl current_app_instance = { app_instance_vtbl };

static HRESULT WINAPI app_instance_UnregisterKey( IInspectable *iface ) { return S_OK; }
static HRESULT WINAPI app_instance_RedirectActivationToAsync( IInspectable *iface, void *args, void **action ) { return E_NOTIMPL; }
static HRESULT WINAPI app_instance_GetActivatedEventArgs( IInspectable *iface, void **result )
{
    TRACE( "app_instance_GetActivatedEventArgs called\n" );
    if (!result) return E_POINTER;
    *result = (void *)&app_activation_args_obj;
    return S_OK;
}
static HRESULT WINAPI app_instance_get_IsCurrent( IInspectable *iface, boolean *value )
{
    if (value) *value = TRUE;
    return S_OK;
}
static HRESULT WINAPI app_instance_get_Key( IInspectable *iface, HSTRING *value )
{
    return WindowsCreateString( L"61E9B470-92E6-482B-AFD8-38C1FB464FB5", 36, value );
}
static HRESULT WINAPI app_instance_get_ProcessId( IInspectable *iface, UINT32 *value )
{
    if (value) *value = GetCurrentProcessId();
    return S_OK;
}

/* AppInstance.Activated registration.  The AppLifecycle contract: the handler
 * is invoked for activations redirected to this instance; the launch itself is
 * delivered through GetActivatedEventArgs, and the app's own framework
 * (Microsoft.ui.xaml driving Application.OnLaunched over the CoreUI window's
 * conversation) drives the startup activation.  Registration stores the
 * handler; nothing is triggered from here. */
static void *g_activated_handler;

static HRESULT WINAPI app_instance_add_Activated( IInspectable *iface, void *handler, EventRegistrationToken *token )
{
    TRACE( "iface %p, handler %p, token %p\n", iface, handler, token );
    if (token) token->value = 1;
    if (handler)
    {
        IUnknown_AddRef( (IUnknown *)handler );
        g_activated_handler = handler;
    }
    return S_OK;
}
static HRESULT WINAPI app_instance_remove_Activated( IInspectable *iface, EventRegistrationToken token )
{
    if (g_activated_handler)
    {
        IUnknown_Release( (IUnknown *)g_activated_handler );
        g_activated_handler = NULL;
    }
    return S_OK;
}

static const void *app_instance_vtbl[] =
{
    /* IInspectable */
    app_instance_QueryInterface,
    app_instance_AddRef,
    app_instance_Release,
    app_instance_GetIids,
    app_instance_GetRuntimeClassName,
    app_instance_GetTrustLevel,
    /* IAppInstance (Microsoft.Windows.AppLifecycle.IAppInstance) */
    app_instance_UnregisterKey,             /* slot 6 */
    app_instance_RedirectActivationToAsync, /* slot 7 */
    app_instance_GetActivatedEventArgs,     /* slot 8 */
    app_instance_add_Activated,             /* slot 9: add_Activated */
    app_instance_remove_Activated,          /* slot 10: remove_Activated */
    app_instance_get_Key,                   /* slot 11: get_Key */
    app_instance_get_IsCurrent,             /* slot 12: get_IsCurrent */
    app_instance_get_ProcessId,             /* slot 13: get_ProcessId */
};


static HRESULT WINAPI app_instance_statics_get_RecommendedInstance( IInspectable *iface, IInspectable **value )
{
    TRACE( "app_instance_statics_get_RecommendedInstance called\n" );
    if (!value) return E_POINTER;
    *value = (IInspectable *)&current_app_instance;
    return S_OK;
}
static HRESULT WINAPI app_instance_statics_GetInstances( IInspectable *iface, IInspectable **value )
{
    TRACE( "app_instance_statics_GetInstances called\n" );
    if (value) *value = NULL;
    return S_OK;
}
static HRESULT WINAPI app_instance_statics_FindOrRegisterInstanceForKey( IInspectable *iface, HSTRING key, IInspectable **result )
{
    TRACE( "app_instance_statics_FindOrRegisterInstanceForKey called: key %s\n", debugstr_hstring( key ) );
    if (!result) return E_POINTER;
    *result = (IInspectable *)&current_app_instance;
    return S_OK;
}
static HRESULT WINAPI app_instance_statics_Unregister( IInspectable *iface )
{
    TRACE( "app_instance_statics_Unregister called\n" );
    return S_OK;
}
static HRESULT WINAPI app_instance_statics_GetActivatedEventArgs( IInspectable *iface, IInspectable **result )
{
    TRACE( "app_instance_statics_GetActivatedEventArgs called\n" );
    if (!result) return E_POINTER;
    *result = (IInspectable *)&app_activation_args_obj;
    return S_OK;
}

static const void *app_instance_statics_vtbl[] =
{
    app_instance_statics_QueryInterface,
    app_instance_statics_AddRef,
    app_instance_statics_Release,
    app_instance_statics_GetIids,
    app_instance_statics_GetRuntimeClassName,
    app_instance_statics_GetTrustLevel,
    app_instance_statics_get_RecommendedInstance,
    app_instance_statics_GetInstances,
    app_instance_statics_FindOrRegisterInstanceForKey,
    app_instance_statics_Unregister,
    app_instance_statics_GetActivatedEventArgs,
};

static HRESULT WINAPI app_instance_factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct app_instance_statics *impl = impl_from_IActivationFactory_app_instance( iface );
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = &impl->IActivationFactory_iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IAppInstanceStatics ) ||
        IsEqualGUID( iid, &IID_IMicrosoftAppInstanceStatics ) ||
        IsEqualGUID( iid, &IID_IMicrosoftAppInstanceStatics2 ))
    {
        *out = &impl->IAppInstanceStatics_iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    TRACE( "app_instance_factory QI NOT IMPLEMENTED: %s\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI app_instance_factory_AddRef( IActivationFactory *iface )
{
    struct app_instance_statics *impl = impl_from_IActivationFactory_app_instance( iface );
    return InterlockedIncrement( &impl->ref );
}
static ULONG WINAPI app_instance_factory_Release( IActivationFactory *iface )
{
    struct app_instance_statics *impl = impl_from_IActivationFactory_app_instance( iface );
    return InterlockedDecrement( &impl->ref );
}
static HRESULT WINAPI app_instance_factory_GetIids( IActivationFactory *iface, ULONG *count, IID **iids )
{ FIXME( "stub\n" ); return E_NOTIMPL; }
static HRESULT WINAPI app_instance_factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *name )
{ FIXME( "stub\n" ); return E_NOTIMPL; }
static HRESULT WINAPI app_instance_factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *level )
{ if (level) *level = BaseTrust; return S_OK; }
static HRESULT WINAPI app_instance_factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    FIXME( "iface %p, instance %p stub!\n", iface, instance );
    return E_NOTIMPL;
}

static const struct IActivationFactoryVtbl app_instance_factory_vtbl =
{
    app_instance_factory_QueryInterface,
    app_instance_factory_AddRef,
    app_instance_factory_Release,
    app_instance_factory_GetIids,
    app_instance_factory_GetRuntimeClassName,
    app_instance_factory_GetTrustLevel,
    app_instance_factory_ActivateInstance,
};

static struct app_instance_statics app_instance_statics_obj =
{
    {&app_instance_factory_vtbl},
    {(IInspectableVtbl *)app_instance_statics_vtbl},
    1,
};

IActivationFactory *app_instance_factory = &app_instance_statics_obj.IActivationFactory_iface;

/*
 * Weak reference sources: the vtable of a source is the owner's IInspectable
 * vtable with GetWeakReference in the seventh slot, so a caller that QIs the
 * owner for IWeakReferenceSource gets an interface whose QI/AddRef/Release
 * behave exactly like the owner's.
 */
static const void *app_instance_weakref_source_vtbl[] =
{
    app_instance_QueryInterface,
    app_instance_AddRef,
    app_instance_Release,
    static_weakref_source_GetWeakReference,
};

static const void *app_instance_statics_weakref_source_vtbl[] =
{
    app_instance_statics_QueryInterface,
    app_instance_statics_AddRef,
    app_instance_statics_Release,
    static_weakref_source_GetWeakReference,
};

static const void *app_activation_args_weakref_source_vtbl[] =
{
    app_activation_args_QueryInterface,
    app_activation_args_AddRef,
    app_activation_args_Release,
    static_weakref_source_GetWeakReference,
};

static struct static_weakref app_instance_weakref =
{
    static_weakref_vtbl,
    &current_app_instance,
    (qi_fn)app_instance_QueryInterface,
};

static struct static_weakref_source app_instance_weakref_source =
{
    app_instance_weakref_source_vtbl,
    &app_instance_weakref,
};

static struct static_weakref app_instance_statics_weakref =
{
    static_weakref_vtbl,
    &app_instance_statics_obj.IAppInstanceStatics_iface,
    (qi_fn)app_instance_statics_QueryInterface,
};

static struct static_weakref_source app_instance_statics_weakref_source =
{
    app_instance_statics_weakref_source_vtbl,
    &app_instance_statics_weakref,
};

static struct static_weakref app_activation_args_weakref =
{
    static_weakref_vtbl,
    &app_activation_args_obj,
    (qi_fn)app_activation_args_QueryInterface,
};

static struct static_weakref_source app_activation_args_weakref_source =
{
    app_activation_args_weakref_source_vtbl,
    &app_activation_args_weakref,
};

