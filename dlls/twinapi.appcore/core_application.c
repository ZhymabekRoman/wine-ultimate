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

struct core_application_view
{
    ICoreApplicationView ICoreApplicationView_iface;
    LONG ref;
};

static inline struct core_application_view *impl_from_ICoreApplicationView( ICoreApplicationView *iface )
{
    return CONTAINING_RECORD( iface, struct core_application_view, ICoreApplicationView_iface );
}

static HRESULT WINAPI core_application_view_QueryInterface( ICoreApplicationView *iface, REFIID iid, void **out )
{
    struct core_application_view *impl = impl_from_ICoreApplicationView( iface );
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_ICoreApplicationView ))
    {
        ICoreApplicationView_AddRef( &impl->ICoreApplicationView_iface );
        *out = &impl->ICoreApplicationView_iface;
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
    if (!ref) free( impl );
    return ref;
}

static HRESULT WINAPI core_application_view_GetIids( ICoreApplicationView *iface, ULONG *count, IID **iids )
{
    FIXME( "iface %p stub!\n", iface );
    return E_NOTIMPL;
}
static HRESULT WINAPI core_application_view_GetRuntimeClassName( ICoreApplicationView *iface, HSTRING *name )
{
    FIXME( "iface %p stub!\n", iface );
    return E_NOTIMPL;
}
static HRESULT WINAPI core_application_view_GetTrustLevel( ICoreApplicationView *iface, TrustLevel *level )
{
    FIXME( "iface %p stub!\n", iface );
    return E_NOTIMPL;
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

struct activated_event_args
{
    __x_ABI_CWindows_CApplicationModel_CActivation_CIActivatedEventArgs IActivatedEventArgs_iface;
    const void *ILaunchActivatedEventArgs_vtbl;
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
    if (IsEqualGUID( iid, &IID_ILaunchActivatedEventArgs ))
    {
        InterlockedIncrement( &impl->ref );
        *out = &impl->ILaunchActivatedEventArgs_vtbl;
        return S_OK;
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
    if (!ref) free( impl );
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
static HRESULT WINAPI activated_event_args_get_SplashScreen( __x_ABI_CWindows_CApplicationModel_CActivation_CIActivatedEventArgs *iface,
        __x_ABI_CWindows_CApplicationModel_CActivation_CISplashScreen **value )
{
    if (!value) return E_POINTER;
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

    static const WCHAR name[] = L"Windows.Foundation.Collections.PropertySet";
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

static HRESULT WINAPI core_application_Run( ICoreApplication *iface, IFrameworkViewSource *view_source )
{
    ICoreApplicationView *app_view;
    IFrameworkView *view;
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
    if (SUCCEEDED(hr))
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
                __x_ABI_CWindows_CUI_CCore_CICoreWindowStatic_Release( cws );
            }
            IActivationFactory_Release( cwf );
        }
        WindowsDeleteString( class_id );

        hr = IFrameworkView_SetWindow( view, cw );
        if (cw) __x_ABI_CWindows_CUI_CCore_CICoreWindow_Release( cw );
    }
    if (SUCCEEDED(hr)) hr = IFrameworkView_Load( view, NULL );
    if (view_activated_handler)
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
    ERR( "app_instance_statics QI: %s\n", debugstr_guid( iid ) );
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
    ERR( "app_instance_statics QI NOT IMPLEMENTED: %s\n", debugstr_guid( iid ) );
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

static const GUID IID_IUwpAppInstance = {0x6756488b, 0xdd4d, 0x4c26, {0xa5, 0x6e, 0xd3, 0xe4, 0xce, 0x07, 0xd8, 0x26}};

static HRESULT WINAPI app_instance_QueryInterface( IInspectable *iface, REFIID iid, void **out )
{
    ERR( "app_instance QI: %s\n", debugstr_guid( iid ) );
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IUwpAppInstance ) ||
        IsEqualGUID( iid, &IID_IMicrosoftAppInstance ))
    {
        *out = iface;
        return S_OK;
    }
    ERR( "app_instance QI NOT IMPLEMENTED: %s\n", debugstr_guid( iid ) );
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
    ERR( "app_activation_args_get_Kind called\n" );
    if (kind) *kind = 0 /* ExtendedActivationKind_Launch */;
    return S_OK;
}
static HRESULT WINAPI app_activation_args_get_Data( void *iface, void **data )
{
    ERR( "app_activation_args_get_Data called\n" );
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
    ERR( "app_instance_GetActivatedEventArgs called\n" );
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
    ERR( "app_instance_statics_get_RecommendedInstance called\n" );
    if (!value) return E_POINTER;
    *value = (IInspectable *)&current_app_instance;
    return S_OK;
}
static HRESULT WINAPI app_instance_statics_GetInstances( IInspectable *iface, IInspectable **value )
{
    ERR( "app_instance_statics_GetInstances called\n" );
    if (value) *value = NULL;
    return S_OK;
}
static HRESULT WINAPI app_instance_statics_FindOrRegisterInstanceForKey( IInspectable *iface, HSTRING key, IInspectable **result )
{
    ERR( "app_instance_statics_FindOrRegisterInstanceForKey called: key %s\n", debugstr_hstring( key ) );
    if (!result) return E_POINTER;
    *result = (IInspectable *)&current_app_instance;
    return S_OK;
}
static HRESULT WINAPI app_instance_statics_Unregister( IInspectable *iface )
{
    ERR( "app_instance_statics_Unregister called\n" );
    return S_OK;
}
static HRESULT WINAPI app_instance_statics_GetActivatedEventArgs( IInspectable *iface, IInspectable **result )
{
    ERR( "app_instance_statics_GetActivatedEventArgs called\n" );
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
    ERR( "app_instance_factory QI NOT IMPLEMENTED: %s\n", debugstr_guid( iid ) );
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

