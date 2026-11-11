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
#include "windows.applicationmodel.activation.h"

WINE_DEFAULT_DEBUG_CHANNEL(xaml);

struct application
{
    IApplication IApplication_iface;
    IInspectable inner_iface;
    IInspectable ms_IApplication_iface;
    IInspectable overrides_uwp_iface;
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

static HRESULT WINAPI app_view_activated_Invoke(
    __FITypedEventHandler_2_Windows__CApplicationModel__CCore__CCoreApplicationView_Windows__CApplicationModel__CActivation__CIActivatedEventArgs *iface,
    ICoreApplicationView *sender,
    __x_ABI_CWindows_CApplicationModel_CActivation_CIActivatedEventArgs *args )
{
    TRACE( "sender %p, args %p, current_application %p\n", sender, args, current_application );
    if (current_application && current_application->outer)
    {
        IInspectable *overrides = NULL;
        HRESULT hr_uwp, hr_winui;

        hr_uwp = IInspectable_QueryInterface( current_application->outer, &IID_IApplicationOverrides_UWP, (void **)&overrides );
        TRACE( "QI IApplicationOverrides_UWP returned hr %#lx, overrides %p\n", hr_uwp, overrides );
        if (SUCCEEDED(hr_uwp) && overrides)
        {
            void **ov_vtbl = *(void ***)overrides;
            HRESULT (WINAPI *on_launched)( void *, void * ) = (void *)ov_vtbl[7];
            TRACE( "invoking UWP App OnLaunched via QI %p (overrides %p, args %p)\n",
                   on_launched, overrides, args );
            if (on_launched) on_launched( overrides, args );
            IInspectable_Release( overrides );
        }
        else
        {
            hr_winui = IInspectable_QueryInterface( current_application->outer, &IID_IApplicationOverrides_WinUI, (void **)&overrides );
            TRACE( "QI IApplicationOverrides_WinUI returned hr %#lx, overrides %p\n", hr_winui, overrides );
            if (SUCCEEDED(hr_winui) && overrides)
            {
                void **ov_vtbl = *(void ***)overrides;
                HRESULT (WINAPI *on_launched)( void *, void * ) = (void *)ov_vtbl[6];
                TRACE( "invoking WinUI3 App OnLaunched %p (overrides %p, args %p)\n",
                       on_launched, overrides, args );
                if (on_launched) on_launched( overrides, args );
                IInspectable_Release( overrides );
            }
            else
            {
                void **vtbl = *(void ***)current_application->outer;
                HRESULT (WINAPI *on_launched)( void *, void * ) = (void *)vtbl[6];
                TRACE( "invoking App OnLaunched fallback vtbl[6] %p (outer %p, args %p)\n",
                       on_launched, current_application->outer, args );
                if (on_launched) on_launched( current_application->outer, args );
            }
        }
    }
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

struct xaml_window
{
    IInspectable IInspectable_iface;
    IInspectable IWindow2_iface;
    IInspectable ms_IWindow_iface;
    LONG ref;
    IInspectable *content;
    ICoreWindow *core_window;
};
static struct xaml_window global_xaml_window;
static BOOL lifecycle_started = FALSE;

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
    if (impl->window) ICoreWindow_Release( impl->window );
    impl->window = window;
    if (window) ICoreWindow_AddRef( window );
    if (window) impl->state = VIEW_STATE_WINDOW_SET;
    if (window && !global_xaml_window.core_window)
    {
        global_xaml_window.core_window = window;
        ICoreWindow_AddRef( window );
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

    hr = RoActivateInstance( hstr, (IInspectable **)&face );
    if (FAILED( hr ))
    {
        WARN( "PropertySet instance create failed, hr %#lx.\n", hr );
        *out = NULL;
        return hr;
    }

    impl->resources = face;
    return IInspectable_QueryInterface( face, iid, out );
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
    struct application *impl = (struct application *)iface;
    if (impl->IApplication_iface.lpVtbl == (const struct IApplicationVtbl *)&application_vtbl)
        return impl;
    if (impl->ms_IApplication_iface.lpVtbl == (const struct IInspectableVtbl *)ms_application_vtbl)
        return CONTAINING_RECORD( iface, struct application, ms_IApplication_iface );
    return impl_from_inner_IInspectable( (IInspectable *)iface );
}

static HRESULT WINAPI application_get_Resources( void *iface, void **value )
{
    struct application *impl = app_from_iface( iface );
    TRACE( "iface %p, value %p\n", iface, value );
    if (!value) return E_POINTER;
    return resources_propget( impl, &IID_IPropertySet, value );
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

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

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

static HRESULT WINAPI application_inner_QueryInterface( IInspectable *iface, REFIID iid, void **out )
{
    struct application *impl = impl_from_inner_IInspectable( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

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

static const void *ms_application_vtbl[] =
{
    application_ms_QueryInterface,        /* 0: QI */
    application_ms_AddRef,                /* 1: AddRef */
    application_ms_Release,               /* 2: Release */
    application_GetIids,                  /* 3: GetIids */
    application_GetRuntimeClassName,      /* 4: GetRuntimeClassName */
    application_GetTrustLevel,            /* 5: GetTrustLevel */
    application_get_Resources,            /* 6: get_Resources */
    application_put_Resources,            /* 7: set_Resources */
    application_stub_zero,                /* 8: get_DebugSettings */
    application_stub_zero,                /* 9: get_RequestedTheme */
    application_stub_ok,                  /* 10: set_RequestedTheme */
    application_stub_zero,                /* 11: get_FocusVisualKind */
    application_stub_ok,                  /* 12: set_FocusVisualKind */
    application_stub_zero,                /* 13: get_HighContrastAdjustment */
    application_stub_ok,                  /* 14: set_HighContrastAdjustment */
    application_add_UnhandledException,   /* 15: add_UnhandledException */
    application_remove_UnhandledException,/* 16: remove_UnhandledException */
    application_Exit,                     /* 17: Exit */
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

static ICoreWindow *get_current_core_window(void)
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

static HRESULT WINAPI xaml_window_QueryInterface( IInspectable *iface, REFIID iid, void **out )
{
    struct xaml_window *impl = (struct xaml_window *)iface;

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

    if (IsEqualGUID( iid, &IID_IWindow_WinUI ))
    {
        *out = &impl->ms_IWindow_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI xaml_window_AddRef( IInspectable *iface )
{
    struct xaml_window *impl = (struct xaml_window *)iface;
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p, ref %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI xaml_window_Release( IInspectable *iface )
{
    struct xaml_window *impl = (struct xaml_window *)iface;
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
    return WindowsCreateString( L"Windows.UI.Xaml.Window", 21, name );
}

static HRESULT WINAPI xaml_window_GetTrustLevel( IInspectable *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI xaml_window_get_Bounds( IInspectable *iface, void *a2, void *a3 )
{
    struct xaml_window *impl = (struct xaml_window *)iface;
    Rect *value = (Rect *)a2;
    ICoreWindow *cw = NULL;

    if (!value) value = (Rect *)a3;
    TRACE( "iface %p, value %p\n", iface, value );
    if (!impl->core_window) impl->core_window = get_current_core_window();
    cw = impl->core_window;
    if (cw && value) return ICoreWindow_get_Bounds( cw, value );
    if (value)
    {
        value->X = 0; value->Y = 0;
        value->Width = 1024; value->Height = 768;
    }
    return S_OK;
}

static HRESULT WINAPI xaml_window_get_Visible( IInspectable *iface, void *a2, void *a3 )
{
    struct xaml_window *impl = (struct xaml_window *)iface;
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
    struct xaml_window *impl = (struct xaml_window *)iface;
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

static HRESULT WINAPI xaml_window_put_Content( IInspectable *iface, void *a2, void *a3 )
{
    struct xaml_window *impl = (struct xaml_window *)iface;
    IInspectable *content = (IInspectable *)a2;

    if (!content) content = (IInspectable *)a3;
    TRACE( "iface %p, content %p\n", iface, content );
    if (impl->content) IInspectable_Release( impl->content );
    impl->content = content;
    if (impl->content) IInspectable_AddRef( impl->content );
    return S_OK;
}

static HRESULT WINAPI xaml_window_get_CoreWindow( IInspectable *iface, void *a2, void *a3 )
{
    struct xaml_window *impl = (struct xaml_window *)iface;
    ICoreWindow **value = (ICoreWindow **)a2;

    if (!value) value = (ICoreWindow **)a3;
    TRACE( "iface %p, value %p\n", iface, value );
    if (!impl->core_window) impl->core_window = get_current_core_window();
    if (value)
    {
        *value = impl->core_window;
        if (*value) ICoreWindow_AddRef( *value );
    }
    return S_OK;
}

static HRESULT WINAPI xaml_window_get_Dispatcher( IInspectable *iface, void *a2, void *a3 )
{
    struct xaml_window *impl = (struct xaml_window *)iface;
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

static HRESULT WINAPI xaml_window_add_Activated( IInspectable *iface, void *handler, EventRegistrationToken *token )
{
    TRACE( "iface %p, handler %p\n", iface, handler );
    if (token) token->value = 1;
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
    struct xaml_window *impl = (struct xaml_window *)iface;
    ICoreWindow *cw = NULL;
    TRACE( "iface %p\n", iface );
    if (!impl->core_window) impl->core_window = get_current_core_window();
    cw = impl->core_window;
    if (cw) ICoreWindow_Activate( cw );
    return S_OK;
}

static HRESULT WINAPI xaml_window_Close( IInspectable *iface )
{
    struct xaml_window *impl = (struct xaml_window *)iface;
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
    void *pad[10];
};

static HRESULT WINAPI xaml_window_zero_out( void *iface, void *a2, void *a3 );

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
        (void *)xaml_window_zero_out, (void *)xaml_window_zero_out,
        (void *)xaml_window_zero_out, (void *)xaml_window_zero_out,
        (void *)xaml_window_zero_out, (void *)xaml_window_zero_out,
        (void *)xaml_window_zero_out, (void *)xaml_window_zero_out,
        (void *)xaml_window_zero_out, (void *)xaml_window_zero_out
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
    return WindowsCreateString( L"Windows.UI.Xaml.Window", 21, name );
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
    if (a3) *(void **)a3 = NULL;
    else if (a2) *(void **)a2 = NULL;
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
    xaml_window_zero_out,                 /* 13: get_DispatcherQueue */
    xaml_window_zero_out,                 /* 14: get_Title */
    xaml_window_zero_out,                 /* 15: set_Title */
    xaml_window_zero_out,                 /* 16: get_ExtendsContentIntoTitleBar */
    xaml_window_zero_out,                 /* 17: set_ExtendsContentIntoTitleBar */
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
    xaml_window_zero_out,                 /* 28: SetTitleBar */
    xaml_window_zero_out, xaml_window_zero_out, xaml_window_zero_out, xaml_window_zero_out,
};

static struct xaml_window global_xaml_window =
{
    {(const struct IInspectableVtbl *)&xaml_window_vtbl},
    {(const struct IInspectableVtbl *)xaml_window2_vtbl},
    {(const struct IInspectableVtbl *)ms_xaml_window_vtbl},
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
    if (inner && (void *)inner != (void *)outer) *inner = NULL;
    if (instance && (void *)instance != (void *)outer)
    {
        *instance = (void *)&global_xaml_window.ms_IWindow_iface;
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
    TRACE( "iface %p, a2 %p, a3 %p.\n", iface, a2, a3 );
    /* a2 is managed in-object/context; a3 is out-pointer on stack */
    if (a3)
    {
        *(void **)a3 = (void *)&global_xaml_window.IInspectable_iface;
        IInspectable_AddRef( (IInspectable *)&global_xaml_window.IInspectable_iface );
    }
    else if (a2)
    {
        *(void **)a2 = (void *)&global_xaml_window.IInspectable_iface;
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

/* Composable factory vtable with Mcg / .NET Native layout extensions:
 * When Mcg projects Windows.UI.Xaml.Application's class activation interface
 * {06499997-f7b4-45fe-b763-7577d1d3cb4a}:
 *   slot 6: get_Current
 *   slot 7: Start(callback)
 *   slot 8: ActivateInstance / LoadComponent
 *   slot 9: CreateInstance(outer, inner, instance) */
struct application_activation_factory_vtbl
{
    HRESULT (WINAPI *QueryInterface)(IActivationFactory *, REFIID, void **);            /* 0 */
    ULONG (WINAPI *AddRef)(IActivationFactory *);                                       /* 1 */
    ULONG (WINAPI *Release)(IActivationFactory *);                                      /* 2 */
    HRESULT (WINAPI *GetIids)(IActivationFactory *, ULONG *, IID **);                   /* 3 */
    HRESULT (WINAPI *GetRuntimeClassName)(IActivationFactory *, HSTRING *);             /* 4 */
    HRESULT (WINAPI *GetTrustLevel)(IActivationFactory *, TrustLevel *);                /* 5 */
    HRESULT (WINAPI *get_Current)(IApplicationStatics *, IApplication **);              /* 6 */
    HRESULT (WINAPI *Start)(IApplicationStatics *, IApplicationInitializationCallback *);/* 7 */
    HRESULT (WINAPI *ActivateInstance)(IActivationFactory *, IInspectable **);          /* 8 */
    HRESULT (WINAPI *CreateInstance)(IApplicationFactory *, IInspectable *, IInspectable **, IApplication **); /* 9 */
    HRESULT (WINAPI *CreateInstance2)(IApplicationFactory *, IInspectable *, IInspectable **, IApplication **); /* 10 */
};

static const struct application_activation_factory_vtbl factory_vtbl =
{
    factory_QueryInterface,
    factory_AddRef,
    factory_Release,
    factory_GetIids,
    factory_GetRuntimeClassName,
    factory_GetTrustLevel,
    application_statics_get_Current,
    application_statics_Start,
    factory_ActivateInstance,
    application_factory_CreateInstance,
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

    impl->IApplication_iface.lpVtbl = (const struct IApplicationVtbl *)&application_vtbl;
    impl->inner_iface.lpVtbl = (const struct IInspectableVtbl *)&application_inner_vtbl;
    impl->ms_IApplication_iface.lpVtbl = (const struct IInspectableVtbl *)ms_application_vtbl;
    impl->overrides_uwp_iface.lpVtbl = (const struct IInspectableVtbl *)application_overrides_uwp_vtbl;
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
         * drive the view lifecycle and message pump. In WinRT, Application.Start / base..ctor
         * blocks for the lifetime of the application view. */
        if (!lifecycle_started)
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

static const void *ms_application_statics_vtbl[] =
{
    ms_statics_QueryInterface,           /* 0: QI */
    ms_statics_AddRef,                   /* 1: AddRef */
    ms_statics_Release,                  /* 2: Release */
    statics_GetIids,                     /* 3: GetIids */
    statics_GetRuntimeClassName,         /* 4: GetRuntimeClassName */
    statics_GetTrustLevel,               /* 5: GetTrustLevel */
    application_statics_get_Current,     /* 6: get_Current */
    application_statics_Start,           /* 7: Start */
    application_stub_ok,                 /* 8: LoadComponent */
    application_stub_ok,                 /* 9: LoadComponent */
    application_stub_zero, application_stub_zero, application_stub_zero, application_stub_zero,
};

static struct application_factory application_factory_obj =
{
    {(const struct IActivationFactoryVtbl *)&factory_vtbl},
    {&application_statics_vtbl},
    {(const struct IApplicationStaticsVtbl *)ms_application_statics_vtbl},
    {(const struct IApplicationFactoryVtbl *)&application_factory_vtbl},
    {&view_source_vtbl},
    1,
};

IActivationFactory *application_factory = &application_factory_obj.IActivationFactory_iface;
