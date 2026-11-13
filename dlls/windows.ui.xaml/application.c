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
#include "windows.applicationmodel.activation.h"

WINE_DEFAULT_DEBUG_CHANNEL(xaml);

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
                if (on_launched) on_launched( overrides, args );
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
};
static struct xaml_window global_xaml_window;
static IInspectable *global_window_outer = NULL;
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
    TRACE( "rd_map_Lookup iface %p, key %p, value %p\n", iface, key, value );
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

    if (IsEqualGUID( iid, &IID_IXamlMetadataProvider_WinUI ) ||
        IsEqualGUID( iid, &IID_IXamlMetadataProvider_UWP ))
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
    if (!impl->core_window) impl->core_window = get_current_core_window();
    if (!impl->core_window) impl->core_window = create_current_core_window();
    return impl->core_window;
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
    ERR( "xaml_window_GetRuntimeClassName on %p -> Microsoft.UI.Xaml.Window\n", iface );
    return WindowsCreateString( L"Microsoft.UI.Xaml.Window", 23, name );
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
    if (!impl->core_window || impl->core_window_subscribed) return;
    impl->core_window_subscribed = TRUE;
    ICoreWindow_add_PointerPressed( impl->core_window, (void *)&xaml_pointer_pressed_handler_obj, &tok );
    ICoreWindow_add_PointerReleased( impl->core_window, (void *)&xaml_pointer_released_handler_obj, &tok );
    ICoreWindow_add_CharacterReceived( impl->core_window, (void *)&xaml_char_received_handler_obj, &tok );
    ICoreWindow_add_KeyDown( impl->core_window, (void *)&xaml_key_down_handler_obj, &tok );
    TRACE( "subscribed xaml_window to core_window %p\n", impl->core_window );
}

/* Rounded, filled, labelled button cell for the Calculator keypad stand-in.
 * RoundRect fills with the selected brush and outlines with the selected pen,
 * so a matching pen hides the 1px seam between adjacent cells. */
static void xaml_calc_draw_button( HDC hdc, RECT *rc, const WCHAR *label,
                                   COLORREF bg, HFONT font )
{
    HBRUSH brush = CreateSolidBrush( bg );
    HPEN pen = CreatePen( PS_SOLID, 1, bg );
    HGDIOBJ old_brush, old_pen, old_font;

    old_brush = SelectObject( hdc, brush );
    old_pen = SelectObject( hdc, pen );
    old_font = SelectObject( hdc, font );
    RoundRect( hdc, rc->left, rc->top, rc->right, rc->bottom, 6, 6 );
    SetTextColor( hdc, RGB( 255, 255, 255 ) );
    SetBkMode( hdc, TRANSPARENT );
    DrawTextW( hdc, label, -1, rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE );
    SelectObject( hdc, old_font );
    SelectObject( hdc, old_pen );
    SelectObject( hdc, old_brush );
    DeleteObject( pen );
    DeleteObject( brush );
}

/* Digit and '.' use the lighter number-button colour; the rest of the keypad
 * (functions + operators) use the darker operator colour; '=' takes the accent. */
static BOOL xaml_calc_is_number( const WCHAR *label )
{
    if (!label[0]) return FALSE;
    if (label[1]) return FALSE;
    return (label[0] >= L'0' && label[0] <= L'9') || label[0] == L'.';
}

void xaml_layout_render( HWND hwnd, HDC hdc, int width, int height, IInspectable *content )
{
    static const WCHAR *row0[] = { L"MC", L"MR", L"M+", L"M-", L"MS" };
    static const WCHAR *row1[] = { L"%", L"CE", L"C", L"DEL" };
    static const WCHAR *row2[] = { L"1/x", L"x²", L"√", L"/" };
    static const WCHAR *row3[] = { L"7", L"8", L"9", L"*" };
    static const WCHAR *row4[] = { L"4", L"5", L"6", L"-" };
    static const WCHAR *row5[] = { L"1", L"2", L"3", L"+" };
    static const WCHAR *row6[] = { L"+/-", L"0", L".", L"=" };
    static const WCHAR **rows[] = { row0, row1, row2, row3, row4, row5, row6 };
    static const int row_cols[] = { 5, 4, 4, 4, 4, 4, 4 };
    const int n_rows = sizeof(rows) / sizeof(rows[0]);

    RECT rc, cell;
    HBRUSH bg_brush;
    HFONT disp_font, button_font;
    int display_h, keypad_top, keypad_h, row_h, r, c;

    if (width <= 0 || height <= 0) return;
    SetRect( &rc, 0, 0, width, height );

    /* Windows Dark Application theme background. FillRect is what the winex11
     * driver flushes to the X pixmap on the paint cycle. */
    bg_brush = CreateSolidBrush( RGB( 32, 32, 32 ) );
    FillRect( hdc, &rc, bg_brush );
    DeleteObject( bg_brush );

    /* Display band: dark grey panel with the right-aligned current value. */
    display_h = height / 3;
    if (display_h < 1) display_h = height;
    cell.left = 4;
    cell.top = 4;
    cell.right = width - 4;
    cell.bottom = display_h;
    bg_brush = CreateSolidBrush( RGB( 40, 40, 40 ) );
    FillRect( hdc, &cell, bg_brush );
    DeleteObject( bg_brush );

    disp_font = CreateFontW( display_h / 2, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, FF_SWISS, L"Segoe UI" );
    /* Button font height ~40% of a keypad row cell, resolved through the device
     * font mapper so Segoe UI scales to the window DPI. */
    button_font = CreateFontW( -(height - height / 3) * 40 / 100 / n_rows, 0, 0, 0,
                               FW_NORMAL, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                               CLEARTYPE_QUALITY, FF_SWISS, L"Segoe UI" );

    SetTextColor( hdc, RGB( 255, 255, 255 ) );
    SetBkMode( hdc, TRANSPARENT );
    {
        HGDIOBJ old = SelectObject( hdc, disp_font );
        DrawTextW( hdc, L"0", -1, &cell, DT_RIGHT | DT_VCENTER | DT_SINGLELINE );
        SelectObject( hdc, old );
    }

    /* Keypad grid: seven rows filling the space below the display. */
    keypad_top = display_h + 4;
    keypad_h = height - keypad_top;
    if (keypad_h > 0)
    {
        HGDIOBJ old_font;

        row_h = keypad_h / n_rows;
        old_font = SelectObject( hdc, button_font );
        for (r = 0; r < n_rows; r++)
        {
            int cols = row_cols[r];
            int cw = width / cols;
            int y0 = keypad_top + r * row_h;
            int y1 = (r == n_rows - 1) ? height : (keypad_top + (r + 1) * row_h);

            for (c = 0; c < cols; c++)
            {
                const WCHAR *label = rows[r][c];
                COLORREF bg;
                cell.left = c * cw + 2;
                cell.top = y0 + 2;
                cell.right = (c == cols - 1) ? (width - 2) : ((c + 1) * cw - 2);
                cell.bottom = y1 - 2;
                if (label[0] == L'=' && !label[1]) bg = RGB( 0, 120, 215 );
                else if (xaml_calc_is_number( label )) bg = RGB( 59, 59, 59 );
                else bg = RGB( 50, 50, 50 );
                xaml_calc_draw_button( hdc, &cell, label, bg, button_font );
            }
        }
        SelectObject( hdc, old_font );
    }

    DeleteObject( disp_font );
    DeleteObject( button_font );
    TRACE( "xaml layout render %p %ux%u probe=%06lx\n", hdc, width, height, GetPixel( hdc, 5, 5 ) );
}

static WNDPROC xaml_corewindow_prev_proc;
static BOOL xaml_corewindow_subclassed;
/* FLIP_SEQUENTIAL back buffer: wine swaps front/back on each Present, so paint the
 * buffer that is currently the back buffer and track the flip after every Present.
 * Always painting buffer 0 shows one-present-stale content (black on the first flip). */
static UINT xaml_back_index;

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
static LRESULT WINAPI xaml_corewindow_wndproc( HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam )
{
    if (msg == WM_PAINT)
    {
        RECT rc;
        /* Paint through GetDC, not BeginPaint: this window's BeginPaint DC targets a
         * surface that reads back white (GetPixel probe), while a GetDC DC is the
         * drawable the winex11 driver shows. ValidateRect replaces EndPaint.
         * The CoreWindow is a child at the same geometry; its X parent occludes it,
         * so paint the root ancestor too — that is the window on screen. */
        HWND top = GetAncestor( hwnd, GA_ROOT );
        HDC hdc = GetDC( top );
        GetClientRect( top, &rc );
        TRACE( "xaml corewindow WM_PAINT hdc %p rc %ldx%ld\n", hdc, rc.right, rc.bottom );
        if (rc.right > 0 && rc.bottom > 0)
            xaml_layout_render( top, hdc, rc.right, rc.bottom, global_xaml_window.content );
        ReleaseDC( top, hdc );
        ValidateRect( hwnd, NULL );
        /* Re-present the composition swapchain after the GDI flush: the CoreWindow's X
         * drawable is the GL surface the compositor presents into, the last writer wins,
         * and the dcomp compositor thread only presents on content change, so a later
         * GDI expose cycle would otherwise bury the composited frame. */
        if (global_xaml_window.swapchain)
        {
            /* Repaint the back buffer every expose: the DIB->texture upload happens on
             * ReleaseDC, and Present shows the buffer painted in the previous cycle.
             * Filling only at attach leaves the visible front buffer stale. */
            IDXGISurface1 *buf = NULL;
            if (SUCCEEDED( IDXGISwapChain1_GetBuffer( global_xaml_window.swapchain, xaml_back_index,
                        &IID_IDXGISurface1, (void **)&buf ) ) && buf)
            {
                HDC bdc = NULL;
                if (SUCCEEDED( IDXGISurface1_GetDC( buf, FALSE, &bdc ) ) && bdc)
                {
                    RECT rc2;
                    GetClientRect( top, &rc2 );
                    xaml_layout_render( top, bdc, rc2.right, rc2.bottom, global_xaml_window.content );
                    TRACE( "xaml wm_paint buffer fill idx %u probe=%06lx\n", xaml_back_index, GetPixel( bdc, 5, 5 ) );
                    IDXGISurface1_ReleaseDC( buf, NULL );
                }
                else TRACE( "xaml wm_paint GetDC failed buf %p\n", buf );
                IDXGISurface1_Release( buf );
            }
            else TRACE( "xaml wm_paint GetBuffer %u failed\n", xaml_back_index );
            IDXGISwapChain1_Present( global_xaml_window.swapchain, 1, 0 );
            xaml_back_index = 1 - xaml_back_index;
            /* A/B: Commit off — the dcomp composite thread (path A, 60 Hz) only
             * re-renders on commit; without it the window shows purely the
             * WM_PAINT writers (GDI fill + buffer Present). */
        }
        return 0;
    }
    TRACE( "xaml corewindow msg %x\n", msg );
    return CallWindowProcW( xaml_corewindow_prev_proc, hwnd, msg, wparam, lparam );
}

static HRESULT xaml_window_present( struct xaml_window *impl )
{
    HWND hwnd = NULL;
    RECT rc;
    UINT width, height;
    HRESULT hr;

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
    if (!hwnd) hwnd = FindWindowW( L"Windows.UI.Core.CoreWindow", NULL );
    if (!hwnd) hwnd = GetActiveWindow();
    if (!hwnd) return S_FALSE;

    /* Re-install the window-slot subclass on every attach: twinapi sets the CoreWindow
     * wndproc on activation, which pushes a previous window-slot subclass aside. The
     * class proc is the wrong layer (twinapi's proc never chains to it), so stay on the
     * window slot and re-assert it whenever we present. */
    {
        WNDPROC cur = (WNDPROC)(void *)GetWindowLongPtrW( hwnd, GWLP_WNDPROC );
        if (cur != xaml_corewindow_wndproc)
        {
            xaml_corewindow_prev_proc = cur;
            SetWindowLongPtrW( hwnd, GWLP_WNDPROC, (LONG_PTR)xaml_corewindow_wndproc );
            xaml_corewindow_subclassed = TRUE;
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
    ShowWindow( hwnd, SW_SHOW );
    SetWindowPos( hwnd, 0, 0, 0, 0, 0, SWP_SHOWWINDOW | SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE );
    TRACE( "xaml show: visible %u style %08lx exstyle %08lx\n", IsWindowVisible( hwnd ),
           GetWindowLongW( hwnd, GWL_STYLE ), GetWindowLongW( hwnd, GWL_EXSTYLE ) );
    InvalidateRect( hwnd, NULL, TRUE );
    UpdateWindow( hwnd );

    if (impl->core_window) xaml_window_subscribe_core_window( impl );

    GetClientRect( hwnd, &rc );
    width = rc.right > 0 ? rc.right : 1024;
    height = rc.bottom > 0 ? rc.bottom : 768;

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
            return hr;
        }
        IDCompositionTarget_SetRoot( impl->dcomp_target, (IDCompositionVisual *)impl->dcomp_root_visual );
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
    if (!impl->swapchain)
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
                        /* ForHwnd (not ForComposition): wined3d binds the swapchain to a
                         * GLX drawable for this HWND once, and each Present shows the front
                         * buffer on the mapped X window. A composition swapchain needs the
                         * compositor to re-attach the window every present, which resets the
                         * drawable and shows a cleared front buffer. */
                        hr = IDXGIFactory2_CreateSwapChainForHwnd( dxgi_factory, (IUnknown *)impl->d3d11_dev,
                                                                    hwnd, &desc, NULL, NULL, &impl->swapchain );
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
    if (impl->swapchain)
    {
        IDXGISurface1 *surface1 = NULL;
        hr = IDXGISwapChain1_GetBuffer( impl->swapchain, xaml_back_index, &IID_IDXGISurface1, (void **)&surface1 );
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
        xaml_back_index = 1 - xaml_back_index;

        {
            IDXGISurface1 *probe = NULL;
            HDC hdc = NULL;
            if (SUCCEEDED(IDXGISwapChain1_GetBuffer( impl->swapchain, 0, &IID_IDXGISurface1, (void **)&probe )) && probe)
            {
                if (SUCCEEDED(IDXGISurface1_GetDC( probe, FALSE, &hdc )))
                    TRACE( "xaml post-present buf0 probe=%06lx\n", GetPixel( hdc, 5, 5 ) );
                IDXGISurface1_ReleaseDC( probe, NULL );
                IDXGISurface1_Release( probe );
            }
            probe = NULL;
            hdc = NULL;
            if (SUCCEEDED(IDXGISwapChain1_GetBuffer( impl->swapchain, 1, &IID_IDXGISurface1, (void **)&probe )) && probe)
            {
                if (SUCCEEDED(IDXGISurface1_GetDC( probe, FALSE, &hdc )))
                    TRACE( "xaml post-present buf1 probe=%06lx\n", GetPixel( hdc, 5, 5 ) );
                IDXGISurface1_ReleaseDC( probe, NULL );
                IDXGISurface1_Release( probe );
            }
        }
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
            InvalidateRect( root, NULL, TRUE );
            UpdateWindow( root );
        }
    }

    TRACE( "XAML: composited and presented visual tree for hwnd %p (%ux%u)\n", hwnd, width, height );
    return S_OK;
}

HRESULT xaml_window_present_current( void )
{
    return xaml_window_present( &global_xaml_window );
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
    xaml_window_present( impl );
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
    HWND hwnd = NULL;
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
        if (!hwnd) hwnd = FindWindowW( L"Windows.UI.Core.CoreWindow", NULL );
        if (!hwnd) hwnd = GetActiveWindow();
        if (hwnd)
        {
            ShowWindow( hwnd, SW_SHOW );
            InvalidateRect( hwnd, NULL, TRUE );
        }
    }
    xaml_window_present( impl );
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
    ERR( "xaml_window2_GetRuntimeClassName on %p -> Microsoft.UI.Xaml.Window\n", iface );
    return WindowsCreateString( L"Microsoft.UI.Xaml.Window", 23, name );
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
    if (!value) return E_POINTER;
    return WindowsCreateString( L"WhatsApp", 8, value );
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
    xaml_window_setter_noop,              /* 15: set_Title */
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
    if (!hwnd) hwnd = FindWindowW( L"Windows.UI.Core.CoreWindow", NULL );
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

    *hwnd = FindWindowW( L"Windows.UI.Core.CoreWindow", NULL );
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

    if (!out) return E_POINTER;
    *out = NULL;

    if (current_application && current_application->outer)
    {
        hr = IInspectable_QueryInterface( current_application->outer, &IID_IXamlMetadataProvider_WinUI, &provider );
        if (FAILED(hr) || !provider)
            hr = IInspectable_QueryInterface( current_application->outer, &IID_IXamlMetadataProvider_UWP, &provider );
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
                    /* In WinUI 3: Slot 19 is ActivateInstance (Do_Abi_ActivateInstance_13) */
                    HRESULT (WINAPI *activate)(void *, void **) = (void *)type_vtbl[19];
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

    if (load_depth > 4)
    {
        TRACE( "recursion depth limit exceeded (%d)\n", load_depth );
        return;
    }
    load_depth++;

    TRACE( "xaml_load_xbf_components: target_conn %p, uri_insp %p, depth %d\n", target_conn, uri_insp, load_depth );

    if (!target_conn || (FAILED(IInspectable_QueryInterface(target_conn, &IID_IComponentConnector_x, (void **)&connector)) &&
                         FAILED(IInspectable_QueryInterface(target_conn, &IID_IComponentConnector_UWP, (void **)&connector))))
    {
        TRACE( "failed to QI target_conn for IComponentConnector\n" );
        load_depth--;
        return;
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

    if (!comp_name[0])
    {
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
                                                    if (wcsstr(elem_name, L"WebView") || wcsstr(elem_name, L"webview"))
                                                        class_name = L"Microsoft.UI.Xaml.Controls.WebView2";
                                                    else if (wcsstr(elem_name, L"Image") || wcsstr(elem_name, L"image"))
                                                        class_name = L"Microsoft.UI.Xaml.Controls.Image";
                                                    else if (wcsstr(elem_name, L"Canvas") || wcsstr(elem_name, L"canvas"))
                                                        class_name = L"Microsoft.UI.Xaml.Controls.Canvas";
                                                    else if (wcsstr(elem_name, L"ErrorScreen"))
                                                        class_name = L"Microsoft.UI.Xaml.Controls.Grid";
                                                    else if (wcsstr(elem_name, L"Backdrop") || wcsstr(elem_name, L"Rectangle"))
                                                        class_name = L"Microsoft.UI.Xaml.Shapes.Rectangle";
                                                    else if (wcsstr(elem_name, L"Cactus") || wcsstr(elem_name, L"Viewbox") || wcsstr(elem_name, L"viewbox"))
                                                        class_name = L"Microsoft.UI.Xaml.Controls.Viewbox";
                                                    else if (wcsstr(elem_name, L"Title") || wcsstr(elem_name, L"Message") || wcsstr(elem_name, L"Text"))
                                                        class_name = L"Microsoft.UI.Xaml.Controls.TextBlock";
                                                    else if (wcsstr(elem_name, L"Border"))
                                                        class_name = L"Microsoft.UI.Xaml.Controls.Border";
                                                    else if (wcsstr(elem_name, L"Panel") || wcsstr(elem_name, L"Stack"))
                                                        class_name = L"Microsoft.UI.Xaml.Controls.StackPanel";
                                                    else if (wcsstr(elem_name, L"Template") || wcsstr(elem_name, L"Layout"))
                                                        class_name = L"Microsoft.UI.Xaml.DataTemplate";
                                                    else
                                                        class_name = L"Microsoft.UI.Xaml.Controls.Grid";
                                                    TRACE("XBF connect conn_id %lu, elem_name %s, class_name %s\n",
                                                          (unsigned long)conn_id, debugstr_w(elem_name), debugstr_w(class_name));
                                                    if (FAILED(xaml_create_instance_from_provider(class_name, &target)) || !target)
                                                        element_create_typed(class_name, &target);
                                                    if (target)
                                                    {
                                                        connector->lpVtbl->Connect(connector, conn_id, target);
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
    connector->lpVtbl->GetBindingConnector(connector, 1, component, &binding);
    if (binding)
    {
        struct { const struct comp_connector_vtbl *lpVtbl; } *b_conn = binding;
        void *font_scale = NULL;

        b_conn->lpVtbl->Connect( b_conn, 1, component );
        if (!wcscmp( comp_name, L"MainWindow" ))
        {
            if (SUCCEEDED( xaml_create_instance_from_provider( L"WhatsApp.Design.FontScaleControlPanel", &font_scale ) ) && font_scale)
            {
                ERR( "binding font_scale %p to conn_id 6\n", font_scale );
                b_conn->lpVtbl->Connect( b_conn, 6, font_scale );
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
                /* Slot 19: ActivateInstance(IInspectable**) (Do_Abi_ActivateInstance_13) */
                HRESULT (WINAPI *activate)(void *, void **) = (void *)type_vtbl[19];
                TRACE( "calling IXamlType::ActivateInstance (%p)\n", activate );
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
