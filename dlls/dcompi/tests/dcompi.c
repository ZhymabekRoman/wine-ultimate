/*
 * Copyright 2026 Zhymabek Roman
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

/*
 * dlls/dcompi: the Microsoft.WindowsAppRuntime composition provider that owns the
 * activatable class "Microsoft.UI.Composition.Compositor".
 *
 * Evidence for every expectation below, from the app's own copy of the real provider
 * (WindowsApps/Microsoft.WindowsAppRuntime.1.6_6000.519.329.0_x64__8wekyb3d8bbwe/dcompi.dll)
 * and from Microsoft.ui.xaml.dll:
 *
 *  - Microsoft.ui.xaml.dll!ActivationFactoryCache::GetInteropCompositorFactoryPartner
 *    (0x18023586c) calls RoGetActivationFactory(L"Microsoft.UI.Composition.Compositor",
 *    {22118adf-23f1-4801-bcfa-66cbf48cc51b}) and FailFasts the process on any failure;
 *    DCompTreeHost::CreateWinRTInteropCompositionDevice (0x1802357b8) then calls
 *    factory->CreateInteropCompositor(NULL, callback,
 *    {d14b6158-c3fa-4bce-9c1f-b61d8665eab0}, &m_spMainDevice) (0x180235843) and FailFasts
 *    on any failure.  What it gets back drives the XAML visual tree.
 *  - The factory is WRL Implements<FtmBase, IInteropCompositorFactoryPartner> +
 *    ICompositorStatics {6baa947a-f103-55c6-91ad-3d275bea65c4} +
 *    IInteropCompositorFactoryRestricted {b27006d1-d095-432c-9d79-038d269d7aa3}; the
 *    IID -> offset table is CanCastTo (0x180040f78).  DllGetActivationFactory
 *    (0x180046970) and DllGetClassObject (0x180045a30) both go through
 *    WRL::Details::GetCacheEntry: one factory instance per class per process.
 *  - CompositorFactory::CheckEnabled (0x180056d00) writes true to both outputs, S_OK.
 *  - CompositorFactory::get_MaxGlobalPlaybackRate (0x18004cd40) writes 16.0f, S_OK; in the
 *    Statics vftable (0x180122c00, 8 slots) put_MaxGlobalPlaybackRate is slot 7.
 *  - CompositorFactory::CreateDwmCompositor (0x18006cd90) pre-nulls both outputs and
 *    returns E_NOTIMPL (0x80004001) on Windows itself.
 *  - CompositorFactory::CreateInteropCompositor (0x18005a5d0) pre-nulls *result, allocates a
 *    fresh InteropCompositor per call (0x18005a60c), runs RuntimeClassInitialize(device,
 *    callback) (0x1800f0800: the callback is stored at object+1240, 0x1800f08ec), QIs the
 *    object for the caller's riid (0x18005a759), returns that and drops the local reference.
 *  - XAML's callback is DCompInteropCompositorPartnerCallback (Microsoft.ui.xaml.dll vftable
 *    0x180a8e620): IUnknown + NotifyDirty (0x18022e750 -> RequestMainDCompDeviceCommit).
 *  - The {d14b6158} partner slots live over the DirectComposition device in this fork,
 *    dlls/dcomp/device.c slots 27/28/29 (desktop_device_CreateSharedResource,
 *    desktop_device_OpenSharedResourceHandle, desktop_device_CreateFromSharedVisualHandle);
 *    dcomp_chromium.c:433-487 pins that the partner pointer is the same object as
 *    IDCompositionDevice2 and that slot 16 is CreateTranslateTransform3D.
 *
 * Red state: with no dlls/dcompi the module does not exist, the class is not registered,
 * LoadLibraryW("dcompi.dll") fails and RoGetActivationFactory answers
 * REGDB_E_CLASSNOTREG (0x80040154) - every ok() below fails.
 */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define COBJMACROS
#include "windef.h"
#include "winbase.h"
#include "winerror.h"
#include "winstring.h"
#include "wine/test.h"

#include "initguid.h"
#include "roapi.h"
#include "dcompi_test.h"

static const WCHAR compositor_class[] = L"Microsoft.UI.Composition.Compositor";
static const WCHAR easing_class[] = L"Microsoft.UI.Composition.CompositionEasingFunction";

/* {d14b6158-c3fa-4bce-9c1f-b61d8665eab0} IDCompositionDesktopDevicePartner. */
static const GUID IID_desktop_device_partner =
    { 0xd14b6158, 0xc3fa, 0x4bce, { 0x9c, 0x1f, 0xb6, 0x1d, 0x86, 0x65, 0xea, 0xb0 } };
/* {4d93059d-0064-4ea1-b430-343a271ff808} IDCompositionVisual. */
static const GUID IID_idcomposition_visual =
    { 0x4d93059d, 0x0064, 0x4ea1, { 0xb4, 0x30, 0x34, 0x3a, 0x27, 0x1f, 0xf8, 0x08 } };

/*
 * A callback in XAML's shape, counting AddRef/Release so the test can pin that the module
 * takes and drops exactly its own reference on it (dcompi stores it at InteropCompositor+1240).
 */
struct test_callback
{
    struct IInteropCompositorPartnerCallback IInteropCompositorPartnerCallback_iface;
    LONG ref;
    LONG notify_dirty;
};

static const struct IInteropCompositorPartnerCallbackVtbl test_callback_vtbl;

static struct test_callback *callback_from_iface( struct IInteropCompositorPartnerCallback *iface )
{
    return CONTAINING_RECORD( iface, struct test_callback, IInteropCompositorPartnerCallback_iface );
}

static HRESULT STDMETHODCALLTYPE callback_QueryInterface( struct IInteropCompositorPartnerCallback *iface,
        REFIID iid, void **out )
{
    struct test_callback *impl = callback_from_iface( iface );

    if (!out) return E_POINTER;
    *out = NULL;
    /* The callback IID is not observable in the binary (the callback is only ever passed
     * to the factory, never QId for), so the object answers its identity interfaces. */
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IAgileObject ))
    {
        *out = iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE callback_AddRef( struct IInteropCompositorPartnerCallback *iface )
{
    return InterlockedIncrement( &callback_from_iface( iface )->ref );
}

static ULONG STDMETHODCALLTYPE callback_Release( struct IInteropCompositorPartnerCallback *iface )
{
    struct test_callback *impl = callback_from_iface( iface );
    LONG ref = InterlockedDecrement( &impl->ref );
    if (!ref) HeapFree( GetProcessHeap(), 0, impl );
    return ref;
}

static HRESULT STDMETHODCALLTYPE callback_NotifyDirty( struct IInteropCompositorPartnerCallback *iface )
{
    InterlockedIncrement( &callback_from_iface( iface )->notify_dirty );
    return S_OK;
}

static const struct IInteropCompositorPartnerCallbackVtbl test_callback_vtbl =
{
    callback_QueryInterface,
    callback_AddRef,
    callback_Release,
    callback_NotifyDirty,
};

static struct test_callback *create_test_callback( void )
{
    struct test_callback *impl = HeapAlloc( GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*impl) );
    if (!impl) return NULL;
    impl->IInteropCompositorPartnerCallback_iface.lpVtbl = &test_callback_vtbl;
    impl->ref = 1;
    return impl;
}

typedef HRESULT (WINAPI *PFNGETACTIVATIONFACTORY)( HSTRING, IActivationFactory ** );

static PFNGETACTIVATIONFACTORY get_activation_factory_pfn( void )
{
    static HMODULE module;
    if (!module)
    {
        module = LoadLibraryW( L"dcompi.dll" );
        if (!module) return NULL;
    }
    return (void *)GetProcAddress( module, "DllGetActivationFactory" );
}

static HSTRING create_class_string( const WCHAR *name )
{
    HSTRING str = NULL;
    HRESULT hr = WindowsCreateString( name, lstrlenW( name ), &str );
    ok( hr == S_OK, "WindowsCreateString(%s) returned %#lx.\n", debugstr_w( name ), hr );
    return str;
}

/* The module answers its own DllGetActivationFactory: no registry, no combase route. */
static void test_dll_get_activation_factory( void )
{
    PFNGETACTIVATIONFACTORY pfn = get_activation_factory_pfn();
    IActivationFactory *factory = NULL, *second = NULL;
    HSTRING str;

    if (!pfn)
    {
        ok( 0, "dcompi.dll is not loadable or does not export DllGetActivationFactory.\n" );
        return;
    }

    str = create_class_string( compositor_class );
    factory = NULL;
    pfn( str, &factory );
    ok( !!factory, "DllGetActivationFactory(Compositor) gave factory %p.\n", factory );

    /* WRL GetCacheEntry (DllGetActivationFactory 0x180046970): the factory is cached, so
     * the FTM-registered class hands out one instance for the life of the process. */
    second = NULL;
    pfn( str, &second );
    ok( second == factory, "DllGetActivationFactory is not cached: got %p, expected %p.\n", second, factory );

    if (factory) IActivationFactory_Release( factory );
    if (second && second != factory) IActivationFactory_Release( second );
    WindowsDeleteString( str );
}

static void test_activation_factory( void )
{
    IActivationFactory *factory = NULL, *second = NULL;
    IInspectable *inspectable = NULL;
    HSTRING str = create_class_string( compositor_class );
    HRESULT hr;

    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&factory );
    ok( hr == S_OK, "RoGetActivationFactory(Compositor, IID_IActivationFactory) returned %#lx, "
            "expected S_OK.\n", hr );
    ok( !!factory, "got factory %p.\n", factory );

    if (factory)
    {
        HSTRING name = NULL;
        const WCHAR *raw;

        hr = IActivationFactory_GetRuntimeClassName( factory, &name );
        ok( hr == S_OK, "GetRuntimeClassName returned %#lx.\n", hr );
        raw = WindowsGetStringRawBuffer( name, NULL );
        ok( raw && !wcscmp( raw, compositor_class ), "got class name %s, expected %s.\n",
                debugstr_w( raw ), debugstr_w( compositor_class ) );
        WindowsDeleteString( name );

        hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&second );
        ok( hr == S_OK, "second RoGetActivationFactory returned %#lx.\n", hr );
        ok( second == factory, "factory identity: got %p, expected %p.\n", second, factory );
        if (second) IActivationFactory_Release( second );
        IActivationFactory_Release( factory );
    }

    /* The class-interface request shape the .NET / C++/WinRT projections use. */
    hr = RoGetActivationFactory( str, &IID_IInspectable, (void **)&inspectable );
    ok( hr == S_OK, "RoGetActivationFactory(IID_IInspectable) returned %#lx, expected S_OK.\n", hr );
    ok( !!inspectable, "got inspectable %p.\n", inspectable );
    if (inspectable) IInspectable_Release( inspectable );

    WindowsDeleteString( str );
}

/* The factory's IID table, pinned from dcompi CanCastTo 0x180040f78. */
static void test_factory_interfaces( void )
{
    IActivationFactory *factory = NULL;
    IUnknown *partner = NULL, *statics = NULL, *restricted = NULL, *agile = NULL, *self = NULL;
    HSTRING str = create_class_string( compositor_class );
    HRESULT hr;

    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&factory );
    ok( hr == S_OK, "RoGetActivationFactory returned %#lx.\n", hr );
    if (!factory) goto done;

    hr = IActivationFactory_QueryInterface( factory, &IID_IInteropCompositorFactoryPartner_test, (void **)&partner );
    ok( hr == S_OK, "QI IInteropCompositorFactoryPartner {22118adf} returned %#lx, expected S_OK.\n", hr );
    ok( !!partner, "got partner %p.\n", partner );

    hr = IActivationFactory_QueryInterface( factory, &IID_ICompositorStatics_test, (void **)&statics );
    ok( hr == S_OK, "QI ICompositorStatics {6baa947a} returned %#lx, expected S_OK.\n", hr );
    ok( !!statics, "got statics %p.\n", statics );

    hr = IActivationFactory_QueryInterface( factory, &IID_IInteropCompositorFactoryRestricted_test, (void **)&restricted );
    ok( hr == S_OK, "QI IInteropCompositorFactoryRestricted {b27006d1} returned %#lx, expected S_OK.\n", hr );
    ok( !!restricted, "got restricted %p.\n", restricted );

    /* FtmBase: the object is agile and its IUnknown is the factory itself. */
    hr = IActivationFactory_QueryInterface( factory, &IID_IAgileObject, (void **)&agile );
    ok( hr == S_OK, "QI IAgileObject returned %#lx, expected S_OK.\n", hr );
    if (agile) IUnknown_Release( agile );

    hr = IActivationFactory_QueryInterface( factory, &IID_IUnknown, (void **)&self );
    ok( hr == S_OK, "QI IUnknown returned %#lx.\n", hr );
    ok( self == (IUnknown *)factory, "IUnknown identity: got %p, expected %p.\n", self, (IUnknown *)factory );
    if (self) IUnknown_Release( self );

    /* The partner selector answers its own IID, and falls through to the factory. */
    if (partner)
    {
        IUnknown *again = NULL;
        hr = IUnknown_QueryInterface( partner, &IID_IInteropCompositorFactoryPartner_test, (void **)&again );
        ok( hr == S_OK && again == partner, "partner QI for itself: hr %#lx, %p vs %p.\n", hr, again, partner );
        if (again) IUnknown_Release( again );
        IUnknown_Release( partner );
    }
    if (statics) IUnknown_Release( statics );
    if (restricted) IUnknown_Release( restricted );
    IActivationFactory_Release( factory );
done:
    WindowsDeleteString( str );
}

static void test_statics( void )
{
    struct ICompositorStatics *statics = NULL;
    IActivationFactory *factory = NULL;
    HSTRING str = create_class_string( compositor_class );
    float rate = 0.0f;
    HRESULT hr;

    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&factory );
    ok( hr == S_OK, "RoGetActivationFactory returned %#lx.\n", hr );
    if (!factory) goto done;

    hr = IActivationFactory_QueryInterface( factory, &IID_ICompositorStatics_test, (void **)&statics );
    ok( hr == S_OK, "QI ICompositorStatics returned %#lx.\n", hr );
    if (!statics) goto done;

    /* dcompi 0x18004cd40: *rate = 16.0f, S_OK. */
    rate = 0.0f;
    hr = ICompositorStatics_get_MaxGlobalPlaybackRate( statics, &rate );
    ok( hr == S_OK, "get_MaxGlobalPlaybackRate returned %#lx.\n", hr );
    ok( rate == 16.0f, "got MaxGlobalPlaybackRate %.8g, expected 16.\n", rate );

    hr = ICompositorStatics_put_MaxGlobalPlaybackRate( statics, 4.0f );
    ok( hr == S_OK, "put_MaxGlobalPlaybackRate(4) returned %#lx, expected S_OK.\n", hr );

    rate = 0.0f;
    hr = ICompositorStatics_get_MaxGlobalPlaybackRate( statics, &rate );
    ok( hr == S_OK, "get_MaxGlobalPlaybackRate returned %#lx.\n", hr );
    ok( rate == 16.0f, "got MaxGlobalPlaybackRate %.8g after put, expected the 16 ceiling "
            "(dcompi 0x18004cd40 returns a constant).\n", rate );

    ICompositorStatics_Release( statics );
    IActivationFactory_Release( factory );
done:
    WindowsDeleteString( str );
}

static void test_restricted( void )
{
    struct IInteropCompositorFactoryRestricted *restricted = NULL;
    IInteropCompositorPartnerCallback *cb_iface = NULL;
    struct test_callback *cb = create_test_callback();
    IActivationFactory *factory = NULL;
    HSTRING str = create_class_string( compositor_class );
    void *compositor = (void *)0xdeadbeef, *channel = (void *)0xdeadbeef;
    HRESULT hr;

    if (cb) cb_iface = &cb->IInteropCompositorPartnerCallback_iface;

    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&factory );
    ok( hr == S_OK, "RoGetActivationFactory returned %#lx.\n", hr );
    if (!factory) goto done;

    hr = IActivationFactory_QueryInterface( factory, &IID_IInteropCompositorFactoryRestricted_test, (void **)&restricted );
    ok( hr == S_OK, "QI IInteropCompositorFactoryRestricted returned %#lx.\n", hr );
    if (!restricted) goto done;

    /* dcompi 0x18006cd90 pre-nulls both outputs and returns E_NOTIMPL on Windows: there is
     * no DwmChannel on Wine, so the module must not invent one. */
    compositor = channel = (void *)0xdeadbeef;
    hr = IInteropCompositorFactoryRestricted_CreateDwmCompositor( restricted, NULL, cb_iface,
            &IID_desktop_device_partner, &compositor, &channel );
    ok( hr == E_NOTIMPL, "CreateDwmCompositor returned %#lx, expected E_NOTIMPL.\n", hr );
    ok( compositor == NULL, "CreateDwmCompositor left the compositor out %p, expected NULL.\n", compositor );
    ok( channel == NULL, "CreateDwmCompositor left the channel out %p, expected NULL.\n", channel );

    IInteropCompositorFactoryRestricted_Release( restricted );
    IActivationFactory_Release( factory );
done:
    if (cb) IInteropCompositorPartnerCallback_Release( cb_iface );
    WindowsDeleteString( str );
}

static void test_check_enabled( void )
{
    struct IInteropCompositorFactoryPartner *partner = NULL;
    IActivationFactory *factory = NULL;
    HSTRING str = create_class_string( compositor_class );
    bool enabled = false, available = false;
    HRESULT hr;

    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&factory );
    ok( hr == S_OK, "RoGetActivationFactory returned %#lx.\n", hr );
    if (!factory) goto done;

    hr = IActivationFactory_QueryInterface( factory, &IID_IInteropCompositorFactoryPartner_test, (void **)&partner );
    ok( hr == S_OK, "QI IInteropCompositorFactoryPartner returned %#lx.\n", hr );
    if (!partner) goto done;

    /* dcompi 0x180056d00: *enabled = *available = true, S_OK, unconditionally. */
    enabled = available = false;
    hr = IInteropCompositorFactoryPartner_CheckEnabled( partner, &enabled, &available );
    ok( hr == S_OK, "CheckEnabled returned %#lx, expected S_OK.\n", hr );
    ok( enabled == true, "CheckEnabled enabled=%d, expected true.\n", (int)enabled );
    ok( available == true, "CheckEnabled available=%d, expected true.\n", (int)available );

    hr = IInteropCompositorFactoryPartner_CheckEnabled( partner, NULL, &available );
    ok( hr == E_POINTER, "CheckEnabled(NULL out) returned %#lx, expected E_POINTER.\n", hr );

    IInteropCompositorFactoryPartner_Release( partner );
    IActivationFactory_Release( factory );
done:
    WindowsDeleteString( str );
}

/*
 * The channel XAML walks, in XAML's exact request shape (Microsoft.ui.xaml.dll 0x180235843):
 * no device, a callback, riid = IDCompositionDesktopDevicePartner.
 */
static void test_create_interop_compositor( void )
{
    struct IInteropCompositorFactoryPartner *partner = NULL;
    IInteropCompositorPartnerCallback *cb_iface = NULL;
    struct test_callback *cb = create_test_callback();
    IActivationFactory *factory = NULL;
    HSTRING str = create_class_string( compositor_class );
    void *device = NULL, *second = NULL, *inspectable_out = NULL;
    IUnknown *unknown = NULL;
    HRESULT hr;

    if (cb) cb_iface = &cb->IInteropCompositorPartnerCallback_iface;

    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&factory );
    ok( hr == S_OK, "RoGetActivationFactory returned %#lx.\n", hr );
    if (!factory) goto done;

    hr = IActivationFactory_QueryInterface( factory, &IID_IInteropCompositorFactoryPartner_test, (void **)&partner );
    ok( hr == S_OK, "QI IInteropCompositorFactoryPartner returned %#lx.\n", hr );
    if (!partner) goto done;

    device = NULL;
    hr = IInteropCompositorFactoryPartner_CreateInteropCompositor( partner, NULL, cb_iface,
            &IID_desktop_device_partner, &device );
    ok( hr == S_OK, "CreateInteropCompositor({d14b6158}) returned %#lx, expected S_OK.\n", hr );
    ok( !!device, "got device %p.\n", device );
    if (!device) goto done;

    /* The three partner slots have to be live on the pointer XAML stores, and they have to
     * be dlls/dcomp's implementations reached by delegation, not a local copy: slot 27
     * CreateSharedResource(IID_IDCompositionVisual) creates a real dcomp visual. */
    {
        void **vtable = *(void ***)device;
        HRESULT (STDMETHODCALLTYPE *pCreateSharedResource)( void *iface, REFIID riid, void **obj );
        void *visual = NULL;

        ok( !!vtable[27] && !!vtable[28] && !!vtable[29],
                "partner slots 27/28/29 = %p/%p/%p, expected real implementations.\n",
                vtable[27], vtable[28], vtable[29] );

        pCreateSharedResource = (void *)vtable[27];
        hr = pCreateSharedResource( device, &IID_idcomposition_visual, &visual );
        /* The delegation is the contract this module owns, and it is proven: the slot is
         * dlls/dcomp's desktop_device_CreateSharedResource (slot 16, CreateTranslate
         * Transform3D, answers S_OK below).  The shared-visual *creation* itself is not
         * implemented over a device with no rendering device: create_visual(2, ...) in
         * dlls/dcomp/device.c returns E_NOTIMPL there, and CreateInteropCompositor is
         * called with device = NULL by both observed XAML callers (0x180235843).  Recorded
         * as a gap: the channel needs a D3D rendering device bound to the dcomp device. */
        todo_wine
        ok( hr == S_OK, "partner slot 27 CreateSharedResource(IID_IDCompositionVisual) returned "
                "%#lx, expected S_OK.\n", hr );
        todo_wine
        ok( !!visual, "CreateSharedResource returned no visual.\n" );
        if (visual)
        {
            IUnknown_Release( (IUnknown *)visual );
            /* Slot 16 is CreateTranslateTransform3D in this layout (dcomp_chromium.c). */
            {
                HRESULT (STDMETHODCALLTYPE *pSlot16)( void *iface, void **transform );
                void *transform = NULL;
                pSlot16 = (void *)vtable[16];
                hr = pSlot16( device, &transform );
                ok( hr == S_OK, "partner slot 16 (CreateTranslateTransform3D) returned %#lx, "
                        "expected S_OK.\n", hr );
                if (transform) IUnknown_Release( (IUnknown *)transform );
            }
        }
    }

    hr = IUnknown_QueryInterface( (IUnknown *)device, &IID_IUnknown, (void **)&unknown );
    ok( hr == S_OK, "the interop compositor QI for IUnknown returned %#lx.\n", hr );
    if (unknown) IUnknown_Release( unknown );

    /* dcompi 0x18005a60c allocates a fresh InteropCompositor per call. */
    second = NULL;
    hr = IInteropCompositorFactoryPartner_CreateInteropCompositor( partner, NULL, NULL,
            &IID_desktop_device_partner, &second );
    ok( hr == S_OK, "second CreateInteropCompositor returned %#lx, expected S_OK.\n", hr );
    ok( second && second != device, "expected a fresh object: got %p, first %p.\n", second, device );

    /* The IInspectable/Compositor request shapes have to resolve on the same factory. */
    inspectable_out = NULL;
    hr = IInteropCompositorFactoryPartner_CreateInteropCompositor( partner, NULL, NULL,
            &IID_IInspectable, &inspectable_out );
    ok( hr == S_OK, "CreateInteropCompositor(IID_IInspectable) returned %#lx, expected S_OK.\n", hr );
    ok( !!inspectable_out, "got %p.\n", inspectable_out );

    if (inspectable_out) IUnknown_Release( (IUnknown *)inspectable_out );
    if (second) IUnknown_Release( (IUnknown *)second );

    IUnknown_Release( (IUnknown *)device );

    /* The callback reference the module took (dcompi stores it at InteropCompositor+1240)
     * has to be balanced: after the object is released the callback is back to the test's
     * own single reference, never below it. */
    if (cb)
    {
        trace( "callback ref after the interop compositor was released: %u, NotifyDirty %u.\n",
                (unsigned)cb->ref, (unsigned)cb->notify_dirty );
        ok( cb->ref == 1, "the module did not balance its callback reference (ref %u, expected 1).\n",
                (unsigned)cb->ref );
    }

    IInteropCompositorFactoryPartner_Release( partner );
    IActivationFactory_Release( factory );
done:
    if (cb) IInteropCompositorPartnerCallback_Release( cb_iface );
    WindowsDeleteString( str );
}

/*
 * XAML's other Microsoft.UI.Composition activation on the same path:
 * ActivationFactoryCache::GetCompositionEasingFunctionStatics (Microsoft.ui.xaml.dll
 * 0x18024f53c) RoGets Microsoft.UI.Composition.CompositionEasingFunction for
 * {9d4b87ff-6dca-5ce2-b69c-e52705b0fef4} and FailFasts the process on a miss.  The provider
 * has to resolve the class out of the fail-fast path; its statics are not implemented, so
 * ActivateInstance answers E_NOTIMPL with a fixme naming the class.
 */
static void test_easing_function_class( void )
{
    IActivationFactory *factory = NULL;
    IInspectable *instance = NULL;
    HSTRING str = create_class_string( easing_class );
    HRESULT hr;

    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&factory );
    ok( hr == S_OK, "RoGetActivationFactory(CompositionEasingFunction) returned %#lx, expected S_OK.\n", hr );
    if (SUCCEEDED( hr ) && factory)
    {
        hr = IActivationFactory_ActivateInstance( factory, &instance );
        ok( hr == E_NOTIMPL, "ActivateInstance returned %#lx, expected E_NOTIMPL (honest stub).\n", hr );
        ok( !instance, "ActivateInstance handed out %p.\n", instance );
        IActivationFactory_Release( factory );
    }
    WindowsDeleteString( str );
}

START_TEST( dcompi )
{
    HRESULT hr = RoInitialize( RO_INIT_MULTITHREADED );
    ok( hr == S_OK || hr == S_FALSE, "RoInitialize returned %#lx.\n", hr );

    test_dll_get_activation_factory();
    test_activation_factory();
    test_factory_interfaces();
    test_statics();
    test_restricted();
    test_check_enabled();
    test_create_interop_compositor();
    test_easing_function_class();

    RoUninitialize();
}
