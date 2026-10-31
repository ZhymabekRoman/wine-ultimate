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

#include "private.h"

WINE_DEFAULT_DEBUG_CHANNEL(xaml);

/* CoreApplicationViewSource and View implementation */
struct framework_view
{
    IFrameworkView IFrameworkView_iface;
    LONG ref;
    ICoreWindow *window;
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
        if (impl->window) ICoreWindow_Release( impl->window );
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
    TRACE( "iface %p, application_view %p.\n", iface, application_view );
    return S_OK;
}

static HRESULT WINAPI view_SetWindow( IFrameworkView *iface, ICoreWindow *window )
{
    struct framework_view *impl = impl_from_IFrameworkView( iface );

    TRACE( "iface %p, window %p.\n", iface, window );

    if (impl->window) ICoreWindow_Release( impl->window );
    impl->window = window;
    if (window) ICoreWindow_AddRef( window );
    return S_OK;
}

static HRESULT WINAPI view_Load( IFrameworkView *iface, HSTRING entry_point )
{
    TRACE( "iface %p, entry_point %s.\n", iface, debugstr_hstring( entry_point ) );
    return S_OK;
}

static HRESULT WINAPI view_Run( IFrameworkView *iface )
{
    struct framework_view *impl = impl_from_IFrameworkView( iface );
    ICoreDispatcher *dispatcher = NULL;
    HRESULT hr;
    MSG msg;

    TRACE( "iface %p.\n", iface );

    if (impl->window)
    {
        hr = ICoreWindow_get_Dispatcher( impl->window, &dispatcher );
        if (SUCCEEDED(hr) && dispatcher)
        {
            hr = ICoreDispatcher_ProcessEvents( dispatcher, CoreProcessEventsOption_ProcessUntilQuit );
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
        ICoreWindow_Release( impl->window );
        impl->window = NULL;
    }
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
struct application
{
    IApplication IApplication_iface;
    IInspectable inner_iface;
    LONG ref;
    IInspectable *outer;
};

static inline struct application *impl_from_IApplication( IApplication *iface )
{
    return CONTAINING_RECORD( iface, struct application, IApplication_iface );
}

static inline struct application *impl_from_inner_IInspectable( IInspectable *iface )
{
    return CONTAINING_RECORD( iface, struct application, inner_iface );
}

static HRESULT WINAPI application_QueryInterface( IApplication *iface, REFIID iid, void **out )
{
    struct application *impl = impl_from_IApplication( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (impl->outer)
        return IInspectable_QueryInterface( impl->outer, iid, out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IApplication ))
    {
        *out = &impl->IApplication_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }

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

static const struct IApplicationVtbl application_vtbl =
{
    application_QueryInterface,
    application_AddRef,
    application_Release,
    application_GetIids,
    application_GetRuntimeClassName,
    application_GetTrustLevel,
};

static HRESULT WINAPI application_inner_QueryInterface( IInspectable *iface, REFIID iid, void **out )
{
    struct application *impl = impl_from_inner_IInspectable( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IApplication ))
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
    return WindowsCreateString( L"Windows.UI.Xaml.Application",
                                wcslen( L"Windows.UI.Xaml.Application" ),
                                class_name );
}

static HRESULT WINAPI application_inner_GetTrustLevel( IInspectable *iface, TrustLevel *trust_level )
{
    if (!trust_level) return E_POINTER;
    *trust_level = BaseTrust;
    return S_OK;
}

static const struct IInspectableVtbl application_inner_vtbl =
{
    application_inner_QueryInterface,
    application_inner_AddRef,
    application_inner_Release,
    application_inner_GetIids,
    application_inner_GetRuntimeClassName,
    application_inner_GetTrustLevel,
};

/* Application factory and statics */
struct application_factory
{
    IActivationFactory IActivationFactory_iface;
    IApplicationStatics IApplicationStatics_iface;
    IApplicationFactory IApplicationFactory_iface;
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

static inline struct application_factory *impl_from_IApplicationFactory( IApplicationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct application_factory, IApplicationFactory_iface );
}

static HRESULT WINAPI factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct application_factory *impl = impl_from_IActivationFactory( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = &impl->IActivationFactory_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IApplicationStatics ))
    {
        *out = &impl->IApplicationStatics_iface;
        IInspectable_AddRef( (IInspectable *)&impl->IActivationFactory_iface );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IApplicationFactory ))
    {
        *out = &impl->IApplicationFactory_iface;
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

static HRESULT WINAPI factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    struct application_factory *impl = impl_from_IActivationFactory( iface );
    return application_factory_CreateInstance( &impl->IApplicationFactory_iface, NULL, NULL, (IApplication **)instance );
}

static const struct IActivationFactoryVtbl factory_vtbl =
{
    factory_QueryInterface,
    factory_AddRef,
    factory_Release,
    factory_GetIids,
    factory_GetRuntimeClassName,
    factory_GetTrustLevel,
    factory_ActivateInstance,
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
        hr = ICoreApplication_Run( core_app, &source->IFrameworkViewSource_iface );
        ICoreApplication_Release( core_app );
    }
    else
    {
        WARN( "Failed to get CoreApplication factory, hr %#lx\n", hr );
    }

    IFrameworkViewSource_Release( &source->IFrameworkViewSource_iface );
    return hr;
}

static const struct IApplicationStaticsVtbl application_statics_vtbl =
{
    statics_QueryInterface,
    statics_AddRef,
    statics_Release,
    statics_GetIids,
    statics_GetRuntimeClassName,
    statics_GetTrustLevel,
    application_statics_Start,
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

    TRACE( "iface %p, outer %p, inner %p, instance %p.\n", iface, outer, inner, instance );

    if (!instance) return E_POINTER;

    impl = calloc( 1, sizeof(*impl) );
    if (!impl) return E_OUTOFMEMORY;

    impl->IApplication_iface.lpVtbl = &application_vtbl;
    impl->inner_iface.lpVtbl = &application_inner_vtbl;
    impl->ref = 1;
    impl->outer = outer;

    if (outer)
    {
        if (inner)
        {
            *inner = &impl->inner_iface;
            IInspectable_AddRef( *inner );
        }
        *instance = &impl->IApplication_iface;
    }
    else
    {
        if (inner) *inner = NULL;
        *instance = &impl->IApplication_iface;
    }

    return S_OK;
}

static const struct IApplicationFactoryVtbl application_factory_vtbl =
{
    app_factory_QueryInterface,
    app_factory_AddRef,
    app_factory_Release,
    app_factory_GetIids,
    app_factory_GetRuntimeClassName,
    app_factory_GetTrustLevel,
    application_factory_CreateInstance,
};

static struct application_factory application_factory_obj =
{
    {&factory_vtbl},
    {&application_statics_vtbl},
    {&application_factory_vtbl},
    1,
};

IActivationFactory *application_factory = &application_factory_obj.IActivationFactory_iface;
