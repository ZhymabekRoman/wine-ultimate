/*
 * WinRT Windows.Management.Deployment Implementation tests
 *
 * Copyright (C) 2026 the Wine project authors
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

#include <stdarg.h>

#define COBJMACROS

#include "windef.h"
#include "winbase.h"
#include "winerror.h"
#include "winstring.h"
#include "winreg.h"
#include "asyncinfo.h"
#include "activation.h"

#include "wine/test.h"

#define COBJMACROS
#define WIDL_using_Windows_Foundation
#include "windows.foundation.h"
#define WIDL_using_Windows_Management_Deployment
#define WIDL_using_Windows_ApplicationModel
#include "windows.management.deployment.h"

static const WCHAR full_name[] = L"WineTest.Deploy_4.5.6.7_x64__t7q2w3e4r5y6";
static const WCHAR staged_path[] = L"C:\\WineTestStage\\WineTest.Deploy_4.5.6.7_x64__t7q2w3e4r5y6";
static const WCHAR appmodel_key[] =
    L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppModel\\Packages\\WineTest.Deploy_4.5.6.7_x64__t7q2w3e4r5y6";

/* the interfaces data the mingw import libraries do not carry */
static const IID IID_iinspectable = {0xaf86e2e0, 0xb12d, 0x4c6a, {0x9c, 0x5a, 0xd7, 0xaa, 0x65, 0x10, 0x1e, 0x90}};
static const IID IID_iuri = {0x9e92e132, 0x23be, 0x4f41, {0xba, 0x42, 0x32, 0xe3, 0x94, 0x6f, 0x7b, 0x4f}};
static const IID IID_iasyncinfo = {0x00000036, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
static const IID IID_ipm = {0x9a7d4b65, 0x5e8f, 0x4fc7, {0xa2, 0xe5, 0x7f, 0x69, 0x25, 0xcb, 0x8b, 0x53}};

/* the URI the deployment is asked for: only Path is read (the staged directory) */

struct uri
{
    IUriRuntimeClass IUriRuntimeClass_iface;
    LONG ref;
};

static inline struct uri *impl_from_IUriRuntimeClass( IUriRuntimeClass *iface )
{
    return CONTAINING_RECORD( iface, struct uri, IUriRuntimeClass_iface );
}

static HRESULT WINAPI uri_QueryInterface( IUriRuntimeClass *iface, REFIID iid, void **out )
{
    struct uri *impl = impl_from_IUriRuntimeClass( iface );

    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_iinspectable ) ||
        IsEqualGUID( iid, &IID_iuri ))
    {
        *out = &impl->IUriRuntimeClass_iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI uri_AddRef( IUriRuntimeClass *iface )
{
    struct uri *impl = impl_from_IUriRuntimeClass( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI uri_Release( IUriRuntimeClass *iface )
{
    struct uri *impl = impl_from_IUriRuntimeClass( iface );
    return InterlockedDecrement( &impl->ref );
}

static HRESULT WINAPI uri_GetIids( IUriRuntimeClass *iface, ULONG *count, IID **iids )
{
    *count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI uri_GetRuntimeClassName( IUriRuntimeClass *iface, HSTRING *name )
{
    static const WCHAR class_name[] = L"Windows.Foundation.Uri";
    return WindowsCreateString( class_name, ARRAY_SIZE(class_name) - 1, name );
}

static HRESULT WINAPI uri_GetTrustLevel( IUriRuntimeClass *iface, TrustLevel *level )
{
    *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI uri_string( IUriRuntimeClass *iface, HSTRING *value )
{
    *value = NULL;
    return S_OK;
}

static HRESULT WINAPI uri_get_Path( IUriRuntimeClass *iface, HSTRING *value )
{
    return WindowsCreateString( staged_path, ARRAY_SIZE(staged_path) - 1, value );
}

static HRESULT WINAPI uri_decoder( IUriRuntimeClass *iface, IWwwFormUrlDecoderRuntimeClass **decoder )
{
    *decoder = NULL;
    return S_OK;
}

static HRESULT WINAPI uri_int( IUriRuntimeClass *iface, INT32 *value )
{
    *value = 0;
    return S_OK;
}

static HRESULT WINAPI uri_bool( IUriRuntimeClass *iface, boolean *value )
{
    *value = FALSE;
    return S_OK;
}

static HRESULT WINAPI uri_equals( IUriRuntimeClass *iface, IUriRuntimeClass *uri, boolean *value )
{
    *value = FALSE;
    return S_OK;
}

static HRESULT WINAPI uri_combine( IUriRuntimeClass *iface, HSTRING relative, IUriRuntimeClass **instance )
{
    *instance = NULL;
    return S_OK;
}

static const struct IUriRuntimeClassVtbl uri_vtbl =
{
    uri_QueryInterface,
    uri_AddRef,
    uri_Release,
    uri_GetIids,
    uri_GetRuntimeClassName,
    uri_GetTrustLevel,
    uri_string, /* AbsoluteUri */
    uri_string, /* DisplayUri */
    uri_string, /* Domain */
    uri_string, /* Extension */
    uri_string, /* Fragment */
    uri_string, /* Host */
    uri_string, /* Password */
    uri_get_Path,
    uri_string, /* Query */
    uri_decoder,
    uri_string, /* RawUri */
    uri_string, /* SchemeName */
    uri_string, /* UserName */
    uri_int,    /* Port */
    uri_bool,   /* Suspicious */
    uri_equals,
    uri_combine,
};

static struct uri test_uri = { { &uri_vtbl }, 1 };

/* the completed handler: fired synchronously by a born-completed operation */

static LONG handler_invoked;

/* the delegate's pinterface IID is computed from its projection name; the test
 * only round-trips the one it passes in, so a fixed value serves */
static const IID iid_is_handler = {0x26be573a, 0x04b0, 0x5a1d, {0x9c, 0x51, 0x34, 0x46, 0xa6, 0x27, 0x9a, 0x74}};

struct completed_handler
{
    IAsyncOperationWithProgressCompletedHandler_DeploymentResult_DeploymentProgress iface;
    LONG ref;
};

static inline struct completed_handler *impl_from_handler(
        IAsyncOperationWithProgressCompletedHandler_DeploymentResult_DeploymentProgress *iface )
{
    return CONTAINING_RECORD( iface, struct completed_handler, iface );
}

static HRESULT WINAPI handler_QueryInterface(
        IAsyncOperationWithProgressCompletedHandler_DeploymentResult_DeploymentProgress *iface, REFIID iid, void **out )
{
    struct completed_handler *impl = impl_from_handler( iface );

    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_iinspectable ) ||
        IsEqualGUID( iid, &iid_is_handler ))
    {
        *out = iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI handler_AddRef(
        IAsyncOperationWithProgressCompletedHandler_DeploymentResult_DeploymentProgress *iface )
{
    struct completed_handler *impl = impl_from_handler( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI handler_Release(
        IAsyncOperationWithProgressCompletedHandler_DeploymentResult_DeploymentProgress *iface )
{
    struct completed_handler *impl = impl_from_handler( iface );
    return InterlockedDecrement( &impl->ref );
}

static HRESULT WINAPI handler_Invoke(
        IAsyncOperationWithProgressCompletedHandler_DeploymentResult_DeploymentProgress *iface,
        IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress *op, AsyncStatus status )
{
    InterlockedIncrement( &handler_invoked );
    ok( status == Completed, "completed status %d\n", status );
    ok( op != NULL, "completed op %p\n", op );
    return S_OK;
}

/* the delegate is IUnknown-based: no IInspectable slots in the table */
static const struct IAsyncOperationWithProgressCompletedHandler_DeploymentResult_DeploymentProgressVtbl handler_vtbl =
{
    handler_QueryInterface,
    handler_AddRef,
    handler_Release,
    handler_Invoke,
};

static struct completed_handler test_handler = { { &handler_vtbl }, 1 };

static void test_register_package( void )
{
    static const WCHAR class_name[] = L"Windows.Management.Deployment.PackageManager";
    HRESULT (WINAPI *p_DllGetActivationFactory)( HSTRING, IActivationFactory ** );
    IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress *op;
    IAsyncOperationWithProgressCompletedHandler_DeploymentResult_DeploymentProgress *handler;
    HMODULE mod;
    HSTRING str;
    IActivationFactory *factory;
    IPackageManager *pm;
    IDeploymentResult *result;
    IAsyncInfo *info;
    AsyncStatus status;
    IInspectable *inspectable;
    HRESULT hr;
    HKEY hkey;
    WCHAR buf[512];
    DWORD size, type, vtype;

    mod = LoadLibraryA( "appxdeploymentclient.dll" );
    ok( mod != NULL, "failed to load appxdeploymentclient.dll, err %lu\n", GetLastError() );
    if (!mod) return;

    p_DllGetActivationFactory = (void *)GetProcAddress( mod, "DllGetActivationFactory" );
    ok( p_DllGetActivationFactory != NULL, "DllGetActivationFactory not exported\n" );
    if (!p_DllGetActivationFactory)
    {
        FreeLibrary( mod );
        return;
    }

    WindowsCreateString( class_name, ARRAY_SIZE(class_name) - 1, &str );
    factory = NULL;
    hr = p_DllGetActivationFactory( str, &factory );
    WindowsDeleteString( str );
    ok( hr == S_OK && factory, "factory hr %#lx factory %p\n", hr, factory );
    if (!factory)
    {
        FreeLibrary( mod );
        return;
    }

    hr = IActivationFactory_ActivateInstance( factory, &inspectable );
    ok( hr == S_OK && inspectable, "ActivateInstance hr %#lx\n", hr );
    IActivationFactory_Release( factory );
    if (hr != S_OK)
    {
        FreeLibrary( mod );
        return;
    }
    hr = IInspectable_QueryInterface( inspectable, &IID_ipm, (void **)&pm );
    IInspectable_Release( inspectable );
    ok( hr == S_OK && pm, "PackageManager QI hr %#lx\n", hr );
    if (!pm)
    {
        FreeLibrary( mod );
        return;
    }

    RegDeleteTreeW( HKEY_CURRENT_USER, appmodel_key );

    op = NULL;
    hr = IPackageManager_RegisterPackageAsync( pm, &test_uri.IUriRuntimeClass_iface, NULL, DeploymentOptions_None, &op );
    ok( hr == S_OK && op, "RegisterPackageAsync hr %#lx op %p\n", hr, op );
    if (!op)
    {
        IPackageManager_Release( pm );
        FreeLibrary( mod );
        return;
    }

    /* born completed: results answer, status is completed */
    result = NULL;
    hr = IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress_GetResults( op, &result );
    ok( hr == S_OK && result, "GetResults hr %#lx result %p\n", hr, result );

    hr = IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress_QueryInterface( op, &IID_iasyncinfo,
            (void **)&info );
    ok( hr == S_OK && info, "IAsyncInfo QI hr %#lx\n", hr );
    if (info)
    {
        status = (AsyncStatus)0xdead;
        hr = IAsyncInfo_get_Status( info, &status );
        ok( hr == S_OK && status == Completed, "status %d hr %#lx\n", status, hr );
        IAsyncInfo_Release( info );
    }

    /* the handler of a completed operation fires at once */
    handler_invoked = 0;
    handler = &test_handler.iface;
    hr = IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress_put_Completed( op, handler );
    ok( hr == S_OK, "put_Completed hr %#lx\n", hr );
    ok( handler_invoked == 1, "handler invoked %ld\n", handler_invoked );
    handler = NULL;
    hr = IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress_get_Completed( op, &handler );
    ok( hr == S_OK && handler == &test_handler.iface, "get_Completed hr %#lx handler %p\n", hr, handler );

    /* the deployment recorded the package registration (kernelbase's plane) */
    if (!RegOpenKeyExW( HKEY_CURRENT_USER, appmodel_key, 0, KEY_READ, &hkey ))
    {
        size = sizeof(buf);
        type = 0;
        hr = RegQueryValueExW( hkey, L"PackagePath", NULL, &type, (BYTE *)buf, &size ) == ERROR_SUCCESS && type == REG_SZ
                 && !wcscmp( buf, staged_path ) ? S_OK : E_FAIL;
        ok( hr == S_OK, "registered PackagePath %s\n", debugstr_w( buf ) );
        size = sizeof(type);
        type = 0xffffffff;
        hr = RegQueryValueExW( hkey, L"Framework", NULL, &vtype, (BYTE *)&type, &size );
        ok( hr == ERROR_SUCCESS && vtype == REG_DWORD && type == 0, "Framework value %lu type %lu hr %ld\n", type, vtype, hr );
        RegCloseKey( hkey );
    }
    else ok( FALSE, "no Packages\\%s registration\n", debugstr_w( full_name ) );

    RegDeleteTreeW( HKEY_CURRENT_USER, appmodel_key );

    IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress_Release( op );
    IPackageManager_Release( pm );
    FreeLibrary( mod );
}

START_TEST( deployment )
{
    test_register_package();
}
