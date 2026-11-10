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

#include <stdlib.h>
#include <string.h>

#include "private.h"

WINE_DEFAULT_DEBUG_CHANNEL(xaml);

/* The class activation interface IID of Windows.UI.Xaml.Application (the
 * Windows.UI.Xaml.Application "activatable" interface; the WinUI3 projection
 * Microsoft.UI.Xaml.Application shares it).  Not in the generated header; the
 * app's Mcg binder passes it to RoGetActivationFactory. */
static const IID IID_Windows_UI_Xaml_Application =
    { 0x06499997, 0xf7b4, 0x45fe, {0xb7, 0x63, 0x75, 0x77, 0xd1, 0xd3, 0xcb, 0x4a} };

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
    struct framework_view *impl = impl_from_IFrameworkView( iface );

    TRACE( "iface %p, application_view %p.\n", iface, application_view );

    impl->owner_tid = GetCurrentThreadId();
    impl->state = VIEW_STATE_INITIALIZED;
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
    if (impl->window) ICoreWindow_Release( impl->window );
    impl->window = window;
    if (window) ICoreWindow_AddRef( window );
    if (window) impl->state = VIEW_STATE_WINDOW_SET;
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
struct application
{
    IApplication IApplication_iface;
    IInspectable inner_iface;
    LONG ref;
    IInspectable *outer;
    IInspectable *resources;   /* lazy Windows.Foundation.Collections.PropertySet for the Resources QI set */
};

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

static HRESULT resources_propget( struct application *impl, REFIID iid, void **out )
{
    static const WCHAR propertyset_name[] = L"Windows.Foundation.Collections.PropertySet";
    static HSTRING_HEADER propertyset_header;
    IInspectable *face = NULL;
    HSTRING hstr = NULL;
    HRESULT hr;

    if (impl->resources)
    {
        hr = IInspectable_QueryInterface( impl->resources, iid, out );
        if (SUCCEEDED( hr )) return S_OK;
    }

    if (FAILED( hr = WindowsCreateStringReference( propertyset_name,
            sizeof(propertyset_name) / sizeof(WCHAR) - 1, &propertyset_header, &hstr )))
    {
        WARN( "PropertySet HSTRING create failed, hr %#lx.\n", hr );
        *out = NULL;
        return hr;
    }

    hr = RoGetActivationFactory( hstr, &IID_IInspectable, (void **)&face );
    if (FAILED( hr ))
    {
        WARN( "PropertySet class create failed, hr %#lx.\n", hr );
        *out = NULL;
        return hr;
    }

    if (!impl->resources) impl->resources = face;
    hr = IInspectable_QueryInterface( face, iid, out );
    IInspectable_Release( face );
    return SUCCEEDED( hr ) ? S_OK : E_NOINTERFACE;
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
        IsEqualGUID( iid, &IID_IApplication ) ||
        IsEqualGUID( iid, &IID_Windows_UI_Xaml_Application ))
    {
        *out = &impl->IApplication_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IApplicationFactory ) ||
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
    if (IsEqualGUID( iid, &IID_IApplication ))
        return IActivationFactory_ActivateInstance( &impl->IActivationFactory_iface, (IInspectable **)out );

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

/* Composable factory vtable with Mcg / .NET Native layout extensions:
 * When Mcg projects Windows.UI.Xaml.Application's class activation interface
 * {06499997-f7b4-45fe-b763-7577d1d3cb4a}, it places CreateInstance at slot 7
 * (following ActivateInstance at slot 6). Extend the vtable so calls to
 * vtable[7] dispatch to CreateInstance instead of hitting NULL. */
struct application_activation_factory_vtbl
{
    HRESULT (WINAPI *QueryInterface)(IActivationFactory *, REFIID, void **);
    ULONG (WINAPI *AddRef)(IActivationFactory *);
    ULONG (WINAPI *Release)(IActivationFactory *);
    HRESULT (WINAPI *GetIids)(IActivationFactory *, ULONG *, IID **);
    HRESULT (WINAPI *GetRuntimeClassName)(IActivationFactory *, HSTRING *);
    HRESULT (WINAPI *GetTrustLevel)(IActivationFactory *, TrustLevel *);
    HRESULT (WINAPI *ActivateInstance)(IActivationFactory *, IInspectable **);
    HRESULT (WINAPI *CreateInstance)(IApplicationFactory *, IInspectable *, IInspectable **, IApplication **);
    HRESULT (WINAPI *Start)(IApplicationStatics *, IApplicationInitializationCallback *);
    HRESULT (WINAPI *get_Current)(IApplicationStatics *, IApplication **);
};

static const struct application_activation_factory_vtbl factory_vtbl =
{
    factory_QueryInterface,
    factory_AddRef,
    factory_Release,
    factory_GetIids,
    factory_GetRuntimeClassName,
    factory_GetTrustLevel,
    factory_ActivateInstance,
    application_factory_CreateInstance,
    application_statics_Start,
    application_statics_get_Current,
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

/* Application.Current: Windows returns the activated Application (the WinUI3 app's own
 * subclass instance, bound by its constructor) and a default face when activation has
 * not run. Cache the first instance created through this factory as the singleton, the
 * way the runtime's static Current field binds it; the managed startup caches the
 * answer into a GC handle slot — a NULL here becomes a -1 sentinel the Redhawk GC card
 * scan later dereferences (mrt100_app+0x522c3 pair). */
static struct application *current_application;   /* strong ref, process singleton */

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

static const struct IApplicationStaticsVtbl application_statics_vtbl =
{
    statics_QueryInterface,
    statics_AddRef,
    statics_Release,
    statics_GetIids,
    statics_GetRuntimeClassName,
    statics_GetTrustLevel,
    application_statics_Start,
    application_statics_get_Current,
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

    impl = calloc( 1, sizeof(*impl) );
    if (!impl) return E_OUTOFMEMORY;

    impl->IApplication_iface.lpVtbl = &application_vtbl;
    impl->inner_iface.lpVtbl = &application_inner_vtbl;
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
            *instance = &impl->IApplication_iface;
    }
    else
    {
        if (inner) *inner = NULL;
        if (instance) *instance = &impl->IApplication_iface;
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
    HRESULT (WINAPI *CreateInstance2)(IApplicationFactory *, IInspectable *, IInspectable **, IApplication **);
    HRESULT (WINAPI *Start)(IApplicationStatics *, IApplicationInitializationCallback *);
    HRESULT (WINAPI *get_Current)(IApplicationStatics *, IApplication **);
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
    application_factory_CreateInstance,
    application_statics_Start,
    application_statics_get_Current,
};

static struct application_factory application_factory_obj =
{
    {(const struct IActivationFactoryVtbl *)&factory_vtbl},
    {&application_statics_vtbl},
    {(const struct IApplicationFactoryVtbl *)&application_factory_vtbl},
    {&view_source_vtbl},
    1,
};

IActivationFactory *application_factory = &application_factory_obj.IActivationFactory_iface;
