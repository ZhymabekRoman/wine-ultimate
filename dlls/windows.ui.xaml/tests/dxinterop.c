/*
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

#define COBJMACROS
#include <stdarg.h>

#include "windef.h"
#include "winbase.h"
#include "winstring.h"
#include "roapi.h"

#define WIDL_using_Windows_Foundation
#define WIDL_using_Windows_Foundation_Collections
#include "windows.foundation.h"
#define WIDL_using_Windows_UI
#include "windows.ui.h"

#include "wine/test.h"

static const GUID IID_ISwapChainPanelNative = {0xf92f19d2, 0x3ade, 0x45a6, {0xa2, 0x0c, 0xf6, 0xf1, 0xea, 0x90, 0x55, 0x4b}};
static const GUID IID_ISwapChainPanelNative2 = {0xd2521730, 0x6d90, 0x4ae7, {0xaf, 0x63, 0x40, 0x09, 0x49, 0xbf, 0x6b, 0x9d}};
static const GUID IID_IApplicationStatics = {0x25f99625, 0x93d3, 0x4a16, {0x90, 0x92, 0x74, 0x70, 0xf3, 0xf6, 0xc8, 0xd7}};
static const GUID IID_IApplicationFactory = {0x92dc89b9, 0xd224, 0x4347, {0x97, 0x55, 0xe8, 0x89, 0x41, 0x9b, 0x4e, 0x33}};

typedef struct ISwapChainPanelNative ISwapChainPanelNative;
typedef struct ISwapChainPanelNativeVtbl
{
    HRESULT (WINAPI *QueryInterface)(ISwapChainPanelNative *iface, REFIID iid, void **out);
    ULONG (WINAPI *AddRef)(ISwapChainPanelNative *iface);
    ULONG (WINAPI *Release)(ISwapChainPanelNative *iface);
    HRESULT (WINAPI *SetSwapChain)(ISwapChainPanelNative *iface, void *swapChain);
} ISwapChainPanelNativeVtbl;
struct ISwapChainPanelNative
{
    const ISwapChainPanelNativeVtbl *lpVtbl;
};

#define ISwapChainPanelNative_QueryInterface(This,riid,ppvObject) (This)->lpVtbl->QueryInterface(This,riid,ppvObject)
#define ISwapChainPanelNative_AddRef(This) (This)->lpVtbl->AddRef(This)
#define ISwapChainPanelNative_Release(This) (This)->lpVtbl->Release(This)
#define ISwapChainPanelNative_SetSwapChain(This,swapChain) (This)->lpVtbl->SetSwapChain(This,swapChain)

typedef struct ISwapChainPanelNative2 ISwapChainPanelNative2;
typedef struct ISwapChainPanelNative2Vtbl
{
    HRESULT (WINAPI *QueryInterface)(ISwapChainPanelNative2 *iface, REFIID iid, void **out);
    ULONG (WINAPI *AddRef)(ISwapChainPanelNative2 *iface);
    ULONG (WINAPI *Release)(ISwapChainPanelNative2 *iface);
    HRESULT (WINAPI *SetSwapChain)(ISwapChainPanelNative2 *iface, void *swapChain);
    HRESULT (WINAPI *SetSwapChainHandle)(ISwapChainPanelNative2 *iface, HANDLE swapChainHandle);
} ISwapChainPanelNative2Vtbl;
struct ISwapChainPanelNative2
{
    const ISwapChainPanelNative2Vtbl *lpVtbl;
};

#define ISwapChainPanelNative2_QueryInterface(This,riid,ppvObject) (This)->lpVtbl->QueryInterface(This,riid,ppvObject)
#define ISwapChainPanelNative2_AddRef(This) (This)->lpVtbl->AddRef(This)
#define ISwapChainPanelNative2_Release(This) (This)->lpVtbl->Release(This)
#define ISwapChainPanelNative2_SetSwapChain(This,swapChain) (This)->lpVtbl->SetSwapChain(This,swapChain)
#define ISwapChainPanelNative2_SetSwapChainHandle(This,swapChainHandle) (This)->lpVtbl->SetSwapChainHandle(This,swapChainHandle)

typedef struct IApplicationStatics IApplicationStatics;
typedef struct IApplicationStaticsVtbl
{
    HRESULT (WINAPI *QueryInterface)(IApplicationStatics *iface, REFIID iid, void **out);
    ULONG (WINAPI *AddRef)(IApplicationStatics *iface);
    ULONG (WINAPI *Release)(IApplicationStatics *iface);
    HRESULT (WINAPI *GetIids)(IApplicationStatics *iface, ULONG *iid_count, IID **iids);
    HRESULT (WINAPI *GetRuntimeClassName)(IApplicationStatics *iface, HSTRING *class_name);
    HRESULT (WINAPI *GetTrustLevel)(IApplicationStatics *iface, TrustLevel *trust_level);
    HRESULT (WINAPI *Start)(IApplicationStatics *iface, void *callback);
} IApplicationStaticsVtbl;
struct IApplicationStatics
{
    const IApplicationStaticsVtbl *lpVtbl;
};

#define IApplicationStatics_QueryInterface(This,riid,ppvObject) (This)->lpVtbl->QueryInterface(This,riid,ppvObject)
#define IApplicationStatics_AddRef(This) (This)->lpVtbl->AddRef(This)
#define IApplicationStatics_Release(This) (This)->lpVtbl->Release(This)
#define IApplicationStatics_Start(This,callback) (This)->lpVtbl->Start(This,callback)

typedef struct IApplicationFactory IApplicationFactory;
typedef struct IApplicationFactoryVtbl
{
    HRESULT (WINAPI *QueryInterface)(IApplicationFactory *iface, REFIID iid, void **out);
    ULONG (WINAPI *AddRef)(IApplicationFactory *iface);
    ULONG (WINAPI *Release)(IApplicationFactory *iface);
    HRESULT (WINAPI *GetIids)(IApplicationFactory *iface, ULONG *iid_count, IID **iids);
    HRESULT (WINAPI *GetRuntimeClassName)(IApplicationFactory *iface, HSTRING *class_name);
    HRESULT (WINAPI *GetTrustLevel)(IApplicationFactory *iface, TrustLevel *trust_level);
    HRESULT (WINAPI *CreateInstance)(IApplicationFactory *iface, IInspectable *outer, IInspectable **inner, IInspectable **instance);
} IApplicationFactoryVtbl;
struct IApplicationFactory
{
    const IApplicationFactoryVtbl *lpVtbl;
};

#define IApplicationFactory_QueryInterface(This,riid,ppvObject) (This)->lpVtbl->QueryInterface(This,riid,ppvObject)
#define IApplicationFactory_AddRef(This) (This)->lpVtbl->AddRef(This)
#define IApplicationFactory_Release(This) (This)->lpVtbl->Release(This)
#define IApplicationFactory_CreateInstance(This,outer,inner,instance) (This)->lpVtbl->CreateInstance(This,outer,inner,instance)

#define check_interface( obj, iid, is_broken ) check_interface_( __LINE__, obj, iid, is_broken )
static void check_interface_( unsigned int line, void *obj, const IID *iid, BOOL is_broken )
{
    IUnknown *iface = obj;
    IUnknown *unk = NULL;
    HRESULT hr;

    hr = IUnknown_QueryInterface( iface, iid, (void **)&unk );
    ok_(__FILE__, line)( hr == S_OK || broken( is_broken && hr == E_NOINTERFACE ), "got hr %#lx.\n", hr );
    if (SUCCEEDED(hr) && unk)
        IUnknown_Release( unk );
}

static void test_Application(void)
{
    static const WCHAR *class_name = L"Windows.UI.Xaml.Application";
    IActivationFactory *factory = NULL;
    IApplicationStatics *statics = NULL;
    IApplicationFactory *app_factory = NULL;
    IInspectable *instance = NULL;
    HSTRING str;
    HRESULT hr;

    hr = WindowsCreateString( class_name, wcslen( class_name ), &str );
    ok( hr == S_OK, "WindowsCreateString failed, hr %#lx\n", hr );

    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&factory );
    ok( hr == S_OK, "RoGetActivationFactory failed, hr %#lx\n", hr );

    if (SUCCEEDED(hr))
    {
        check_interface( factory, &IID_IUnknown, FALSE );
        check_interface( factory, &IID_IInspectable, FALSE );
        check_interface( factory, &IID_IActivationFactory, FALSE );
        check_interface( factory, &IID_IApplicationStatics, FALSE );
        check_interface( factory, &IID_IApplicationFactory, FALSE );

        hr = IActivationFactory_QueryInterface( factory, &IID_IApplicationStatics, (void **)&statics );
        ok( hr == S_OK, "QueryInterface IApplicationStatics failed, hr %#lx\n", hr );
        if (SUCCEEDED(hr))
            IApplicationStatics_Release( statics );

        hr = IActivationFactory_QueryInterface( factory, &IID_IApplicationFactory, (void **)&app_factory );
        ok( hr == S_OK, "QueryInterface IApplicationFactory failed, hr %#lx\n", hr );
        if (SUCCEEDED(hr))
        {
            hr = IApplicationFactory_CreateInstance( app_factory, NULL, NULL, &instance );
            ok( hr == S_OK, "CreateInstance failed, hr %#lx\n", hr );
            if (SUCCEEDED(hr) && instance)
            {
                check_interface( instance, &IID_IInspectable, FALSE );
                IInspectable_Release( instance );
            }
            IApplicationFactory_Release( app_factory );
        }

        hr = IActivationFactory_ActivateInstance( factory, &instance );
        ok( hr == S_OK, "ActivateInstance failed, hr %#lx\n", hr );
        if (SUCCEEDED(hr) && instance)
            IInspectable_Release( instance );

        IActivationFactory_Release( factory );
    }

    WindowsDeleteString( str );
}

static void test_SwapChainPanel(void)
{
    static const WCHAR *class_name = L"Windows.UI.Xaml.Controls.SwapChainPanel";
    IActivationFactory *factory = NULL;
    ISwapChainPanelNative *native_panel = NULL;
    ISwapChainPanelNative2 *native_panel2 = NULL;
    ISwapChainPanelNative *direct_native = NULL;
    IInspectable *instance = NULL;
    HSTRING str;
    HRESULT hr;

    hr = WindowsCreateString( class_name, wcslen( class_name ), &str );
    ok( hr == S_OK, "WindowsCreateString failed, hr %#lx\n", hr );

    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&factory );
    ok( hr == S_OK, "RoGetActivationFactory failed, hr %#lx\n", hr );

    if (SUCCEEDED(hr))
    {
        check_interface( factory, &IID_IUnknown, FALSE );
        check_interface( factory, &IID_IInspectable, FALSE );
        check_interface( factory, &IID_IActivationFactory, FALSE );
        check_interface( factory, &IID_ISwapChainPanelNative, FALSE );
        check_interface( factory, &IID_ISwapChainPanelNative2, FALSE );

        /* Query ISwapChainPanelNative from factory */
        hr = IActivationFactory_QueryInterface( factory, &IID_ISwapChainPanelNative, (void **)&native_panel );
        ok( hr == S_OK, "QueryInterface ISwapChainPanelNative failed, hr %#lx\n", hr );
        if (SUCCEEDED(hr))
        {
            hr = ISwapChainPanelNative_SetSwapChain( native_panel, NULL );
            ok( hr == S_OK, "SetSwapChain(NULL) failed, hr %#lx\n", hr );

            hr = ISwapChainPanelNative_QueryInterface( native_panel, &IID_ISwapChainPanelNative2, (void **)&native_panel2 );
            ok( hr == S_OK, "QueryInterface ISwapChainPanelNative2 failed, hr %#lx\n", hr );
            if (SUCCEEDED(hr))
            {
                hr = ISwapChainPanelNative2_SetSwapChainHandle( native_panel2, (HANDLE)(ULONG_PTR)0x1234 );
                ok( hr == S_OK, "SetSwapChainHandle failed, hr %#lx\n", hr );
                ISwapChainPanelNative2_Release( native_panel2 );
            }

            ISwapChainPanelNative_Release( native_panel );
        }

        /* Activate instance and verify ISwapChainPanelNative on instance */
        hr = IActivationFactory_ActivateInstance( factory, &instance );
        ok( hr == S_OK, "ActivateInstance failed, hr %#lx\n", hr );
        if (SUCCEEDED(hr) && instance)
        {
            check_interface( instance, &IID_IUnknown, FALSE );
            check_interface( instance, &IID_IInspectable, FALSE );
            check_interface( instance, &IID_ISwapChainPanelNative, FALSE );
            check_interface( instance, &IID_ISwapChainPanelNative2, FALSE );

            hr = IInspectable_QueryInterface( instance, &IID_ISwapChainPanelNative, (void **)&native_panel );
            ok( hr == S_OK, "instance QI ISwapChainPanelNative failed, hr %#lx\n", hr );
            if (SUCCEEDED(hr))
            {
                hr = ISwapChainPanelNative_SetSwapChain( native_panel, NULL );
                ok( hr == S_OK, "instance SetSwapChain(NULL) failed, hr %#lx\n", hr );
                ISwapChainPanelNative_Release( native_panel );
            }

            hr = IInspectable_QueryInterface( instance, &IID_ISwapChainPanelNative2, (void **)&native_panel2 );
            ok( hr == S_OK, "instance QI ISwapChainPanelNative2 failed, hr %#lx\n", hr );
            if (SUCCEEDED(hr))
            {
                hr = ISwapChainPanelNative2_SetSwapChainHandle( native_panel2, (HANDLE)(ULONG_PTR)0x5678 );
                ok( hr == S_OK, "instance SetSwapChainHandle failed, hr %#lx\n", hr );
                ISwapChainPanelNative2_Release( native_panel2 );
            }

            IInspectable_Release( instance );
        }

        IActivationFactory_Release( factory );
    }

    /* Test direct query via RoGetActivationFactory */
    hr = RoGetActivationFactory( str, &IID_ISwapChainPanelNative, (void **)&direct_native );
    ok( hr == S_OK, "RoGetActivationFactory direct ISwapChainPanelNative failed, hr %#lx\n", hr );
    if (SUCCEEDED(hr) && direct_native)
    {
        hr = ISwapChainPanelNative_SetSwapChain( direct_native, NULL );
        ok( hr == S_OK, "direct SetSwapChain failed, hr %#lx\n", hr );
        ISwapChainPanelNative_Release( direct_native );
    }

    WindowsDeleteString( str );
}

START_TEST(dxinterop)
{
    HRESULT hr;

    hr = RoInitialize( RO_INIT_MULTITHREADED );
    ok( hr == S_OK, "RoInitialize failed, hr %#lx\n", hr );

    test_Application();
    test_SwapChainPanel();

    RoUninitialize();
}
