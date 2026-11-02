/*
 * SysMain offerable heap (ext-ms-win-sysmain-pfsapi-l1-1-0 / wineuwp.dll) and shell activation
 * broker conformance tests
 *
 * Copyright 2026 Zealot
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#define COBJMACROS

#include <windows.h>

#include "initguid.h"
#include "objbase.h"
#include "objidl.h"
#include "inspectable.h"
#include "winstring.h"

#include "wine/test.h"

static const char apiset_name[] = "ext-ms-win-sysmain-pfsapi-l1-1-0.dll";

static const char *const pfsapi_exports[] =
{
    "PfsOfferableHeapInitialize",
    "PfsOfferableHeapStart",
    "PfsOfferableHeapAlloc",
    "PfsOfferableHeapFree",
    "PfsOfferableHeapOffer",
    "PfsOfferableHeapReclaim",
    "PfsOfferableHeapCleanup",
};

typedef void *(WINAPI *pfs_heap_alloc_t)( void *heap, SIZE_T size, void *arg3, void *arg4 );
typedef DWORD (WINAPI *pfs_heap_free_t)( void *heap, void *block, void *arg3, void *arg4 );

static void test_pfsapi_apiset( void )
{
    HMODULE module;
    SIZE_T i;

    winetest_push_context( "apiset" );

    module = LoadLibraryA( apiset_name );
    ok( module != NULL, "Failed to load %s, err %lu.\n", apiset_name, GetLastError() );
    if (!module)
    {
        winetest_pop_context();
        return;
    }

    for (i = 0; i < ARRAY_SIZE( pfsapi_exports ); i++)
        ok( GetProcAddress( module, pfsapi_exports[i] ) != NULL, "Failed to get %s, err %lu.\n",
            pfsapi_exports[i], GetLastError() );

    FreeLibrary( module );
    winetest_pop_context();
}

static void test_pfsapi_heap( void )
{
    /* XAML's OfferableHeap passes its own heap object; our implementation ignores it and uses
     * the process heap, so the value must not matter as long as alloc and free agree. */
    static void *const fake_heap = (void *) 0xdeadbeef;
    static const SIZE_T size = 0x100;
    pfs_heap_alloc_t p_alloc;
    pfs_heap_free_t p_free;
    HMODULE module;
    char *block;
    DWORD ret;

    winetest_push_context( "heap" );

    module = LoadLibraryA( apiset_name );
    ok( module != NULL, "Failed to load %s, err %lu.\n", apiset_name, GetLastError() );
    if (!module)
    {
        winetest_pop_context();
        return;
    }

    p_alloc = (pfs_heap_alloc_t) GetProcAddress( module, "PfsOfferableHeapAlloc" );
    p_free = (pfs_heap_free_t) GetProcAddress( module, "PfsOfferableHeapFree" );
    ok( p_alloc != NULL, "PfsOfferableHeapAlloc is missing.\n" );
    ok( p_free != NULL, "PfsOfferableHeapFree is missing.\n" );

    if (p_alloc && p_free)
    {
        block = p_alloc( fake_heap, size, NULL, NULL );
        ok( block != NULL, "PfsOfferableHeapAlloc(%Iu) returned NULL.\n", size );
        if (block)
        {
            memset( block, 0x5a, size ); /* the block must be real writable memory */
            ok( block[0] == 0x5a && block[size - 1] == 0x5a, "Block is not writable.\n" );
        }
        ret = p_free( fake_heap, block, NULL, NULL );
        ok( ret == ERROR_SUCCESS, "PfsOfferableHeapFree returned %lu.\n", ret );
    }

    FreeLibrary( module );
    winetest_pop_context();
}

/* ---------------------------------------------------------------------------------------------
 * The shell activation brokers wineuwp provides in-process
 */
DEFINE_GUID(CLSID_ActivatableApplicationRegistrar, 0xdea794e0, 0x1c1d, 0x4363, 0xb1, 0x71, 0x98, 0xd0, 0xb1, 0x70, 0x35, 0x86);
DEFINE_GUID(IID_IActivatableApplicationRegistrar,  0x036c57fc, 0xe73d, 0x4f3a, 0xb0, 0x95, 0x8b, 0x81, 0x3f, 0xa2, 0xf7, 0x64);
DEFINE_GUID(IID_IActivatableApplication,           0x92696c00, 0x7578, 0x48e1, 0xac, 0x1a, 0x2c, 0xa9, 0x09, 0xe2, 0xc8, 0xcf);
DEFINE_GUID(IID_IActivatedEventArgs,               0xcf651713, 0xcd08, 0x4fd8, 0xb6, 0x97, 0xa2, 0x81, 0xb6, 0x54, 0x4e, 0x2e);
DEFINE_GUID(IID_ILaunchActivatedEventArgs,         0xfbc93e26, 0xa14a, 0x4b4f, 0x82, 0xb0, 0x33, 0xbe, 0xd9, 0x20, 0xaf, 0x52);
DEFINE_GUID(IID_ISplashScreen,                     0xca4d975c, 0xd4d6, 0x43f0, 0x97, 0xc0, 0x08, 0x33, 0xc6, 0x39, 0x1c, 0x24);
DEFINE_GUID(CLSID_CoreWindowBroker,                0x3480a401, 0xbde9, 0x4407, 0xbc, 0x02, 0x79, 0x8a, 0x86, 0x6a, 0xc0, 0x51);
DEFINE_GUID(IID_ICoreWindowBroker,                 0x0f4accb1, 0xd8f9, 0x4011, 0xba, 0x37, 0x25, 0x57, 0x92, 0x5a, 0x78, 0xcf);
DEFINE_GUID(IID_ICoreWindowFactory,                0xcd292360, 0x2763, 0x4085, 0x8a, 0x9f, 0x74, 0xb2, 0x24, 0xa2, 0x91, 0x75);
DEFINE_GUID(IID_IConfigureWindowFactory,           0x601d51e3, 0x801e, 0x49c9, 0xbb, 0xfa, 0xfe, 0x29, 0xa6, 0x62, 0xae, 0xad);
DEFINE_GUID(IID_ICoreWindow,                       0x79b9d5f2, 0x879e, 0x4b89, 0xb7, 0x98, 0x79, 0xe4, 0x75, 0x98, 0x03, 0x0c);
DEFINE_GUID(IID_ICoreWindowNavigation,             0xbb4cbbe9, 0x65af, 0x4730, 0x9e, 0xd2, 0xab, 0xbd, 0xb5, 0xcc, 0x15, 0xb9);
DEFINE_GUID(CLSID_PsmBroker,                       0x02844640, 0xe37c, 0x4322, 0xa3, 0xb8, 0x4c, 0x61, 0xa2, 0xe5, 0x88, 0x79);
DEFINE_GUID(IID_IPsmBroker,                        0x33c255c3, 0x1a86, 0x45fc, 0x94, 0x98, 0xf6, 0x0d, 0x8f, 0x56, 0xb9, 0xd3);
/* the navigation client's PrepareToActivate contract twinapi's brokered Activate drives (slot 6) */
DEFINE_GUID(IID_INavigationClientPrepareToActivate, 0x075be329, 0x825a, 0x4f2a, 0xa8, 0xaf, 0xcd, 0x2f, 0x9a, 0x81, 0x02, 0xec);

/* CoreApplication::GetWindowFactory asks the broker for this private service, and slot 5 on. */
static const GUID SID_window_factory = { 0xd98fd14a, 0x522a, 0x4d59, { 0xb8, 0x75, 0x81, 0x1e, 0x83, 0x91, 0x9a, 0x9e } };
/* The shell's IApplicationInvocableFlow, the service the share UI asks the broker for. Only its
 * first dword is pinned; the broker answers every service that is not the window factory, so the
 * tail does not matter to the assertion. */
static const GUID SID_invocable_flow = { 0xf230b678, 0x1234, 0x5678, { 0x9a, 0xbc, 0xde, 0xf0, 0x12, 0x34, 0x56, 0x78 } };

/* vtable access for the private interfaces, whose shapes no header describes */
static HRESULT call_slot( void *iface, unsigned int slot, void *a, void *b, void *c, void *d )
{
    HRESULT (WINAPI *func)( void *, void *, void *, void *, void * );

    func = (HRESULT (WINAPI *)( void *, void *, void *, void *, void * ))(*(void ***)iface)[slot];
    return func( iface, a, b, c, d );
}

/* ICoreWindowBroker::QueryService is slot 3 and takes exactly three arguments; the out pointer must
 * land in the fourth register, so it needs its own typed caller. */
static HRESULT call_query_service( void *broker, const GUID *service, const GUID *iid, void **out )
{
    HRESULT (WINAPI *func)( void *, const GUID *, const GUID *, void ** ) = (void *)(*(void ***)broker)[3];

    return func( broker, service, iid, out );
}

static void test_class_objects( void )
{
    static const struct
    {
        const GUID *clsid;
        const char *name;
    } classes[] =
    {
        { &CLSID_ActivatableApplicationRegistrar, "ActivatableApplicationRegistrar" },
        { &CLSID_CoreWindowBroker, "CoreWindowBroker" },
        { &CLSID_PsmBroker, "PsmBroker" },
    };
    SIZE_T i;

    winetest_push_context( "class objects" );

    for (i = 0; i < ARRAY_SIZE( classes ); i++)
    {
        IUnknown *object = NULL;
        HRESULT hr;

        hr = CoCreateInstance( classes[i].clsid, NULL, CLSCTX_INPROC_SERVER, &IID_IUnknown, (void **)&object );
        ok( hr == S_OK, "%s: CoCreateInstance returned %#lx, expected S_OK.\n", classes[i].name, hr );
        if (object) IUnknown_Release( object );
    }

    winetest_pop_context();
}

static void test_qi_matrix( void )
{
    IUnknown *registrar = NULL, *broker = NULL, *psm = NULL;
    IClassFactory *factory[3] = { NULL, NULL, NULL };
    SIZE_T i;

    winetest_push_context( "QueryInterface" );

    if (FAILED( CoCreateInstance( &CLSID_ActivatableApplicationRegistrar, NULL, CLSCTX_INPROC_SERVER,
                                  &IID_IUnknown, (void **)&registrar ) ))
        goto done;
    if (FAILED( CoCreateInstance( &CLSID_CoreWindowBroker, NULL, CLSCTX_INPROC_SERVER,
                                  &IID_IUnknown, (void **)&broker ) ))
        goto done;
    if (FAILED( CoCreateInstance( &CLSID_PsmBroker, NULL, CLSCTX_INPROC_SERVER,
                                  &IID_IUnknown, (void **)&psm ) ))
        goto done;

    /* the interface each class is registered for answers */
    {
        IUnknown *iface = NULL;
        HRESULT hr = IUnknown_QueryInterface( registrar, &IID_IActivatableApplicationRegistrar, (void **)&iface );
        ok( hr == S_OK, "registrar QI IActivatableApplicationRegistrar returned %#lx.\n", hr );
        if (iface) IUnknown_Release( iface );

        iface = NULL;
        hr = IUnknown_QueryInterface( broker, &IID_ICoreWindowBroker, (void **)&iface );
        ok( hr == S_OK, "broker QI ICoreWindowBroker returned %#lx.\n", hr );
        if (iface) IUnknown_Release( iface );

        /* IInspectable is the window factory's default vtable, ICoreWindowFactory its slot 7-8 */
        iface = NULL;
        hr = IUnknown_QueryInterface( broker, &IID_IInspectable, (void **)&iface );
        ok( hr == S_OK, "broker QI IInspectable returned %#lx.\n", hr );
        if (iface) IUnknown_Release( iface );

        iface = NULL;
        hr = IUnknown_QueryInterface( broker, &IID_ICoreWindowFactory, (void **)&iface );
        ok( hr == S_OK, "broker QI ICoreWindowFactory returned %#lx.\n", hr );
        if (iface) IUnknown_Release( iface );

        iface = NULL;
        hr = IUnknown_QueryInterface( broker, &IID_IConfigureWindowFactory, (void **)&iface );
        ok( hr == S_OK, "broker QI IConfigureWindowFactory returned %#lx.\n", hr );
        if (iface) IUnknown_Release( iface );

        iface = NULL;
        hr = IUnknown_QueryInterface( psm, &IID_IPsmBroker, (void **)&iface );
        ok( hr == S_OK, "psm QI IPsmBroker returned %#lx.\n", hr );
        if (iface) IUnknown_Release( iface );
    }

    /* Negative assertions: a class does not answer another broker's interface. Handing the window
     * factory back for an unrelated interface is what made twinapi.appcore treat it as one. */
    {
        IUnknown *iface = (IUnknown *)0xdeadbeef;
        HRESULT hr = IUnknown_QueryInterface( registrar, &IID_ICoreWindowBroker, (void **)&iface );
        ok( hr == E_NOINTERFACE, "registrar QI ICoreWindowBroker returned %#lx, expected E_NOINTERFACE.\n", hr );
        ok( iface == NULL, "registrar QI ICoreWindowBroker returned iface %p, expected NULL.\n", iface );

        iface = (IUnknown *)0xdeadbeef;
        hr = IUnknown_QueryInterface( broker, &IID_IActivatableApplicationRegistrar, (void **)&iface );
        ok( hr == E_NOINTERFACE, "broker QI IActivatableApplicationRegistrar returned %#lx, expected E_NOINTERFACE.\n", hr );
        ok( iface == NULL, "broker QI returned iface %p, expected NULL.\n", iface );

        iface = (IUnknown *)0xdeadbeef;
        hr = IUnknown_QueryInterface( psm, &IID_ICoreWindowFactory, (void **)&iface );
        ok( hr == E_NOINTERFACE, "psm QI ICoreWindowFactory returned %#lx, expected E_NOINTERFACE.\n", hr );
        ok( iface == NULL, "psm QI returned iface %p, expected NULL.\n", iface );
    }

    /* QueryService answers only the window factory's private service. */
    {
        IUnknown *service = (IUnknown *)0xdeadbeef;
        HRESULT hr = call_query_service( broker, &SID_window_factory, &IID_IUnknown, (void **)&service );
        ok( hr == S_OK, "QueryService SID_window_factory returned %#lx, expected S_OK.\n", hr );
        ok( service != NULL, "QueryService SID_window_factory returned no object.\n" );
        if (service) IUnknown_Release( service );

        service = (IUnknown *)0xdeadbeef;
        hr = call_query_service( broker, &SID_invocable_flow, &IID_IUnknown, (void **)&service );
        ok( hr == E_NOINTERFACE, "QueryService {f230b678} returned %#lx, expected E_NOINTERFACE.\n", hr );
        ok( service == NULL, "QueryService {f230b678} returned iface %p, expected NULL.\n", service );
    }

    /* The registrar's remaining slots are refused, so a caller that reaches one says what it wants. */
    for (i = 4; i <= 6; i++)
    {
        HRESULT hr = call_slot( registrar, i, NULL, NULL, NULL, NULL );
        ok( hr == E_NOTIMPL, "registrar slot %Iu returned %#lx, expected E_NOTIMPL.\n", i, hr );
    }

done:
    /* no aggregation: a class factory hands out its object only to an outer of NULL */
    for (i = 0; i < 3; i++)
    {
        static const GUID *clsids[3] =
        {
            &CLSID_ActivatableApplicationRegistrar, &CLSID_CoreWindowBroker, &CLSID_PsmBroker,
        };
        IUnknown *object = NULL;
        HRESULT hr;

        hr = CoGetClassObject( clsids[i], CLSCTX_INPROC_SERVER, NULL, &IID_IClassFactory, (void **)&factory[i] );
        ok( hr == S_OK, "CoGetClassObject(%s) returned %#lx.\n", debugstr_guid( clsids[i] ), hr );
        if (FAILED( hr )) continue;

        hr = IClassFactory_CreateInstance( factory[i], (IUnknown *)0x1, &IID_IUnknown, (void **)&object );
        ok( hr == CLASS_E_NOAGGREGATION, "CreateInstance(outer %p) returned %#lx, expected CLASS_E_NOAGGREGATION.\n",
            (IUnknown *)0x1, hr );
        ok( object == NULL, "CreateInstance(outer) returned object %p, expected NULL.\n", object );
        IClassFactory_Release( factory[i] );
        factory[i] = NULL;
    }

    if (registrar) IUnknown_Release( registrar );
    if (broker) IUnknown_Release( broker );
    if (psm) IUnknown_Release( psm );
    winetest_pop_context();
}

/* RegisterActivatableApplication is slot 3; a null app object is a caller bug it reports. */
static void test_register_null( void )
{
    IUnknown *registrar = NULL;
    HRESULT hr;

    winetest_push_context( "RegisterActivatableApplication" );

    hr = CoCreateInstance( &CLSID_ActivatableApplicationRegistrar, NULL, CLSCTX_INPROC_SERVER,
                           &IID_IActivatableApplicationRegistrar, (void **)&registrar );
    ok( hr == S_OK, "CoCreateInstance registrar returned %#lx.\n", hr );
    if (FAILED( hr )) goto done;

    hr = call_slot( registrar, 3, NULL, NULL, NULL, NULL );
    ok( hr == E_INVALIDARG, "RegisterActivatableApplication(NULL) returned %#lx, expected E_INVALIDARG.\n", hr );

done:
    if (registrar) IUnknown_Release( registrar );
    winetest_pop_context();
}

/* ICoreWindowFactory::CreateCoreWindow is slot 6 and get_WindowReuseAllowed slot 7. */
static HRESULT call_create_core_window( void *factory, HSTRING title, IUnknown **window )
{
    HRESULT (WINAPI *func)( void *, HSTRING, void ** ) = (void *)(*(void ***)factory)[6];

    return func( factory, title, (void **)window );
}
static HRESULT call_get_window_reuse_allowed( void *factory, boolean *value )
{
    HRESULT (WINAPI *func)( void *, boolean * ) = (void *)(*(void ***)factory)[7];

    return func( factory, value );
}

/* The window factory a view activates through: the broker's private service, its
 * ICoreWindowFactory, and the CoreWindow it makes through windows.ui's private
 * PrivateCreateCoreWindow (ordinal 1500). CreateCoreWindow also puts a navigation client on the
 * window: CoreApplicationView::PrepareToActivateAsync fails the activation with E_NOTIMPL when the
 * window's navigation client property is empty, so the attach is part of the contract.
 */
static void test_create_core_window( void )
{
    static const WCHAR title[] = L"App";
    IUnknown *broker = NULL, *service = NULL, *factory = NULL;
    IUnknown *window = NULL, *iface = NULL, *client = NULL;
    HSTRING str = NULL;
    boolean reuse = TRUE;
    HRESULT hr;

    winetest_push_context( "CreateCoreWindow" );

    hr = CoCreateInstance( &CLSID_CoreWindowBroker, NULL, CLSCTX_INPROC_SERVER,
                           &IID_ICoreWindowBroker, (void **)&broker );
    ok( hr == S_OK, "CoCreateInstance broker returned %#lx.\n", hr );
    if (FAILED( hr )) goto done;

    service = NULL;
    hr = call_query_service( broker, &SID_window_factory, &IID_ICoreWindowFactory, (void **)&service );
    ok( hr == S_OK, "QueryService SID_window_factory returned %#lx.\n", hr );
    if (FAILED( hr )) goto done;

    factory = NULL;
    hr = IUnknown_QueryInterface( service, &IID_ICoreWindowFactory, (void **)&factory );
    ok( hr == S_OK, "QI ICoreWindowFactory returned %#lx.\n", hr );
    if (FAILED( hr )) goto done;

    /* a UAP view window is not reused */
    hr = call_get_window_reuse_allowed( factory, &reuse );
    ok( hr == S_OK && !reuse, "get_WindowReuseAllowed returned %#lx, reuse %d.\n", hr, reuse );

    WindowsCreateString( title, ARRAY_SIZE( title ) - 1, &str );
    window = NULL;
    hr = call_create_core_window( factory, str, &window );
    ok( hr == S_OK, "CreateCoreWindow returned %#lx.\n", hr );
    ok( window != NULL, "CreateCoreWindow returned window %p.\n", window );
    if (!window) goto done;

    iface = NULL;
    hr = IUnknown_QueryInterface( window, &IID_ICoreWindow, (void **)&iface );
    ok( hr == S_OK && iface, "window QI ICoreWindow returned %#lx.\n", hr );
    if (iface) IUnknown_Release( iface );

    iface = NULL;
    hr = IUnknown_QueryInterface( window, &IID_ICoreWindowNavigation, (void **)&iface );
    ok( hr == S_OK, "window QI ICoreWindowNavigation returned %#lx.\n", hr );
    if (SUCCEEDED( hr ))
    {
        client = NULL;
        hr = call_slot( iface, 6, &client, NULL, NULL, NULL );   /* get_NavigationClient */
        ok( hr == S_OK && client, "get_NavigationClient returned %#lx, client %p.\n", hr, client );
        if (client) IUnknown_Release( client );
        IUnknown_Release( iface );
    }

done:
    WindowsDeleteString( str );
    if (window) IUnknown_Release( window );
    if (factory) IUnknown_Release( factory );
    if (service) IUnknown_Release( service );
    if (broker) IUnknown_Release( broker );
    winetest_pop_context();
}

/* PrepareToActivate as the brokered Activate drives it (twinapi core_application.c
 * prepare_to_activate): the window's navigation client, QI'd for
 * INavigationClientPrepareToActivate {075be329}, slot 6, with a completed IAsyncAction out.
 * E_NOTIMPL here is the value the shell treats as "this view cannot be activated"; the broker
 * attaches a navigation client that answers, so the activation contract step has to be reached.
 */
static void test_prepare_to_activate( void )
{
    static const WCHAR title[] = L"App";
    IUnknown *broker = NULL, *service = NULL, *factory = NULL;
    IUnknown *window = NULL, *nav = NULL, *client = NULL, *prepare = NULL, *action = NULL;
    HSTRING str = NULL;
    HRESULT hr;

    winetest_push_context( "PrepareToActivate" );

    hr = CoCreateInstance( &CLSID_CoreWindowBroker, NULL, CLSCTX_INPROC_SERVER,
                           &IID_ICoreWindowBroker, (void **)&broker );
    ok( hr == S_OK, "CoCreateInstance broker returned %#lx.\n", hr );
    if (FAILED( hr )) goto done;

    hr = call_query_service( broker, &SID_window_factory, &IID_ICoreWindowFactory, (void **)&service );
    ok( hr == S_OK, "QueryService SID_window_factory returned %#lx.\n", hr );
    if (FAILED( hr )) goto done;

    hr = IUnknown_QueryInterface( service, &IID_ICoreWindowFactory, (void **)&factory );
    ok( hr == S_OK, "QI ICoreWindowFactory returned %#lx.\n", hr );
    if (FAILED( hr )) goto done;

    WindowsCreateString( title, ARRAY_SIZE( title ) - 1, &str );
    hr = call_create_core_window( factory, str, &window );
    ok( hr == S_OK && window, "CreateCoreWindow returned %#lx, window %p.\n", hr, window );
    if (!window) goto done;

    /* step 4 of the activation contract: navigation client -> prepare interface -> PrepareToActivate */
    hr = IUnknown_QueryInterface( window, &IID_ICoreWindowNavigation, (void **)&nav );
    ok( hr == S_OK && nav, "window QI ICoreWindowNavigation returned %#lx, nav %p.\n", hr, nav );
    if (!nav) goto done;

    client = NULL;
    hr = call_slot( nav, 6, &client, NULL, NULL, NULL );   /* get_NavigationClient */
    ok( hr == S_OK && client, "get_NavigationClient returned %#lx, client %p.\n", hr, client );
    if (!client) goto done;

    hr = IUnknown_QueryInterface( client, &IID_INavigationClientPrepareToActivate, (void **)&prepare );
    ok( hr == S_OK && prepare, "client QI INavigationClientPrepareToActivate returned %#lx, prepare %p.\n", hr, prepare );
    if (!prepare) goto done;

    hr = call_slot( prepare, 6, NULL /* UINT64 arg = 0 */, &action, NULL, NULL );   /* PrepareToActivate */
    ok( hr == S_OK, "PrepareToActivate returned %#lx, expected S_OK (E_NOTIMPL fails the activation).\n", hr );
    ok( action != NULL, "PrepareToActivate returned action %p, expected a completed IAsyncAction.\n", action );

done:
    WindowsDeleteString( str );
    if (action) IUnknown_Release( action );
    if (prepare) IUnknown_Release( prepare );
    if (client) IUnknown_Release( client );
    if (nav) IUnknown_Release( nav );
    if (window) IUnknown_Release( window );
    if (factory) IUnknown_Release( factory );
    if (service) IUnknown_Release( service );
    if (broker) IUnknown_Release( broker );
    winetest_pop_context();
}

/* A stand-in app object: what CoreApplication::Run hands the registrar, an activation factory
 * cloaking IActivatableApplication {92696c00}. Its Activate is the callback the shell makes, and
 * it is what raises the app's OnLaunched. */
static int activate_calls;
static HSTRING seen_aumid, seen_contract, seen_activation_id, seen_extra;
static IInspectable *seen_args;

static HRESULT WINAPI fake_app_QueryInterface( void *iface, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IActivatableApplication ))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI fake_app_AddRef( void *iface ) { return 1; }
static ULONG WINAPI fake_app_Release( void *iface ) { return 1; }
static HRESULT WINAPI fake_app_GetIids( void *iface, ULONG *count, IID **iids ) { *count = 0; *iids = NULL; return S_OK; }
static HRESULT WINAPI fake_app_GetRuntimeClassName( void *iface, HSTRING *name ) { *name = NULL; return E_NOTIMPL; }
static HRESULT WINAPI fake_app_GetTrustLevel( void *iface, int *level ) { *level = 0; return S_OK; }
static HRESULT WINAPI fake_app_Activate( void *iface, HSTRING aumid, HSTRING contract, HSTRING activation_id,
                                         HSTRING extra, IInspectable *args, UINT64 cookie, UINT32 options )
{
    activate_calls++;
    seen_aumid = aumid;
    seen_contract = contract;
    seen_activation_id = activation_id;
    seen_extra = extra;
    /* the app that keeps the arguments holds a reference, as a real OnLaunched does */
    if (args) IInspectable_AddRef( seen_args = args );
    return S_OK;
}

static const void *fake_app_vtbl[] =
{
    fake_app_QueryInterface, fake_app_AddRef, fake_app_Release,
    fake_app_GetIids, fake_app_GetRuntimeClassName, fake_app_GetTrustLevel,
    fake_app_Activate,
};

/* the object: its first member is the pointer to that vtable */
static const void *fake_app_obj = fake_app_vtbl;

static const char *hstring_c( HSTRING str, char *buffer, SIZE_T size )
{
    const WCHAR *raw = WindowsGetStringRawBuffer( str, NULL );
    if (!raw) return "(null)";
    WideCharToMultiByte( CP_UTF8, 0, raw, -1, buffer, size, NULL, NULL );
    return buffer;
}

static void test_activate_callback( void )
{
    char a[64], b[64], c[64], d[64];
    IUnknown *registrar = NULL;
    HRESULT hr;

    winetest_push_context( "Activate callback" );

    /* the AUMID the shell names the app by; the reference host's default is the entry id "App" */
    SetEnvironmentVariableA( "WINE_UWP_AUMID", "App" );

    hr = CoCreateInstance( &CLSID_ActivatableApplicationRegistrar, NULL, CLSCTX_INPROC_SERVER,
                           &IID_IActivatableApplicationRegistrar, (void **)&registrar );
    ok( hr == S_OK, "CoCreateInstance registrar returned %#lx.\n", hr );
    if (FAILED( hr )) goto done;

    /* the object is a pointer to its vtable, as every COM object here is */
    hr = call_slot( registrar, 3, (void *)&fake_app_obj, NULL, NULL, NULL );
    ok( hr == S_OK, "RegisterActivatableApplication returned %#lx, expected S_OK.\n", hr );
    ok( activate_calls == 1, "Activate was called %d times, expected 1.\n", activate_calls );
    ok( seen_args != NULL, "Activate was given no launch arguments.\n" );

    ok( !lstrcmpA( hstring_c( seen_aumid, a, sizeof(a) ), "App" ), "aumid was \"%s\", expected \"App\".\n", a );
    ok( !lstrcmpA( hstring_c( seen_contract, b, sizeof(b) ), "Windows.Launch" ),
        "contract was \"%s\", expected \"Windows.Launch\".\n", b );
    ok( !lstrcmpA( hstring_c( seen_activation_id, c, sizeof(c) ), "" ),
        "activation id was \"%s\", expected \"\".\n", c );
    ok( !lstrcmpA( hstring_c( seen_extra, d, sizeof(d) ), "" ), "extra was \"%s\", expected \"\".\n", d );

    /* The launch arguments have to answer the interfaces a managed OnLaunched reads. */
    if (seen_args)
    {
        IUnknown *iface = NULL;

        hr = IInspectable_QueryInterface( seen_args, &IID_IActivatedEventArgs, (void **)&iface );
        ok( hr == S_OK, "args QI IActivatedEventArgs returned %#lx.\n", hr );
        if (iface)
        {
            INT32 (WINAPI *get_Kind)( void *, INT32 * ) = (void *)(*(void ***)iface)[6];
            INT32 kind = 0xdead;
            ok( get_Kind( iface, &kind ) == S_OK && kind == 0 /* ActivationKind_Launch */,
                "get_Kind returned %d.\n", kind );
            IUnknown_Release( iface );
        }

        iface = NULL;
        hr = IInspectable_QueryInterface( seen_args, &IID_ILaunchActivatedEventArgs, (void **)&iface );
        ok( hr == S_OK, "args QI ILaunchActivatedEventArgs returned %#lx.\n", hr );
        if (iface)
        {
            HRESULT (WINAPI *get_Arguments)( void *, HSTRING * ) = (void *)(*(void ***)iface)[6];
            HSTRING arguments = NULL;
            ok( get_Arguments( iface, &arguments ) == S_OK, "get_Arguments failed.\n" );
            ok( !WindowsGetStringLen( arguments ), "Arguments was \"%s\", expected \"\".\n",
                hstring_c( arguments, a, sizeof(a) ) );
            WindowsDeleteString( arguments );
            IUnknown_Release( iface );
        }

        iface = NULL;
        hr = IInspectable_QueryInterface( seen_args, &IID_ISplashScreen, (void **)&iface );
        ok( hr == S_OK && iface, "args QI ISplashScreen returned %#lx, expected a splash screen.\n", hr );
        if (iface) IUnknown_Release( iface );

        IInspectable_Release( seen_args );
        seen_args = NULL;
    }

done:
    if (registrar) IUnknown_Release( registrar );
    winetest_pop_context();
}

START_TEST( wineuwp )
{
    CoInitialize( NULL );
    test_pfsapi_apiset();
    test_pfsapi_heap();
    test_class_objects();
    test_qi_matrix();
    test_register_null();
    test_create_core_window();
    test_prepare_to_activate();
    test_activate_callback();
    CoUninitialize();
}
