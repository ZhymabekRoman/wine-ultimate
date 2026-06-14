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

WINE_DEFAULT_DEBUG_CHANNEL(twinapi);

/* C++/WinRT static-lifetime helper: get_activation_factory<IStaticLifetime>(class)
 * then GetCollection() returns a process-wide IMap<HSTRING,IInspectable> used to
 * cache static instances tied to the CoreApplication lifetime. WhatsApp/FrameworkUdk
 * hard-requires this on the CoreApplication factory; without it XAML fail-fasts. */
static const GUID IID_IStaticLifetime = {0x17b0e613,0x942a,0x422d,{0x90,0x4c,0xf9,0x0d,0xc7,0x1a,0x7d,0xae}};

typedef struct IStaticLifetime IStaticLifetime;
typedef struct IStaticLifetimeVtbl
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)( IStaticLifetime *, REFIID, void ** );
    ULONG   (STDMETHODCALLTYPE *AddRef)( IStaticLifetime * );
    ULONG   (STDMETHODCALLTYPE *Release)( IStaticLifetime * );
    HRESULT (STDMETHODCALLTYPE *GetIids)( IStaticLifetime *, ULONG *, IID ** );
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)( IStaticLifetime *, HSTRING * );
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)( IStaticLifetime *, TrustLevel * );
    HRESULT (STDMETHODCALLTYPE *GetCollection)( IStaticLifetime *, __FIMap_2_HSTRING_IInspectable ** );
} IStaticLifetimeVtbl;
struct IStaticLifetime { const IStaticLifetimeVtbl *lpVtbl; };

struct factory
{
    IActivationFactory IActivationFactory_iface;
    ICoreApplication ICoreApplication_iface;
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

    if (IsEqualGUID( iid, &IID_IStaticLifetime ))
    {
        IActivationFactory_AddRef( &impl->IActivationFactory_iface );
        *out = &impl->IStaticLifetime_iface;
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
    FIXME( "iface %p, value %p semi-stub.\n", iface, value );
    if (value) *value = NULL;
    return S_OK;
}
static HRESULT WINAPI core_application_view_add_Activated( ICoreApplicationView *iface,
        __FITypedEventHandler_2_Windows__CApplicationModel__CCore__CCoreApplicationView_Windows__CApplicationModel__CActivation__CIActivatedEventArgs *handler, EventRegistrationToken *token )
{
    FIXME( "iface %p, handler %p, token %p semi-stub.\n", iface, handler, token );
    if (token) token->value = 0;
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

static ICoreApplicationView *core_application_view_create(void)
{
    struct core_application_view *impl;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return NULL;
    impl->ICoreApplicationView_iface.lpVtbl = &core_application_view_vtbl;
    impl->ref = 1;
    return &impl->ICoreApplicationView_iface;
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
    FIXME( "iface %p, value %p stub!\n", iface, value );
    if (value) *value = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI core_application_GetCurrentView( ICoreApplication *iface, ICoreApplicationView **value )
{
    FIXME( "iface %p, value %p semi-stub.\n", iface, value );
    if (!value) return E_POINTER;
    *value = core_application_view_create();
    return *value ? S_OK : E_OUTOFMEMORY;
}

static HRESULT WINAPI core_application_Run( ICoreApplication *iface, IFrameworkViewSource *view_source )
{
    ICoreApplicationView *app_view;
    IFrameworkView *view;
    HRESULT hr;

    FIXME( "iface %p, view_source %p semi-stub.\n", iface, view_source );

    if (!view_source) return E_INVALIDARG;

    if (FAILED(hr = IFrameworkViewSource_CreateView( view_source, &view )))
    {
        ERR( "CreateView failed %#lx.\n", hr );
        return hr;
    }

    if (!(app_view = core_application_view_create()))
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

static const IStaticLifetimeVtbl static_lifetime_vtbl =
{
    static_lifetime_QueryInterface,
    static_lifetime_AddRef,
    static_lifetime_Release,
    static_lifetime_GetIids,
    static_lifetime_GetRuntimeClassName,
    static_lifetime_GetTrustLevel,
    static_lifetime_GetCollection,
};

static struct factory factory =
{
    {&activation_factory_vtbl},
    {&core_application_vtbl},
    {&static_lifetime_vtbl},
    NULL,
    1,
};

IActivationFactory *core_application_factory = &factory.IActivationFactory_iface;
