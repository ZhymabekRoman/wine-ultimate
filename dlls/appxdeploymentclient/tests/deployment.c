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

#ifndef E_NOT_FOUND
#define E_NOT_FOUND MAKE_HRESULT( SEVERITY_ERROR, 0x87, 0x490 ) /* 0x80070490 */
#endif

#define COBJMACROS
#define WIDL_using_Windows_Foundation
#define WIDL_using_Windows_Foundation_Collections
#include "windows.foundation.h"
#include "windows.foundation.collections.h"
#define WIDL_using_Windows_System
#define WIDL_using_Windows_Storage
#define WIDL_using_Windows_Management_Deployment
#define WIDL_using_Windows_ApplicationModel
#include "windows.management.deployment.h"
#include "windows.storage.h"

static const WCHAR full_name[] = L"WineTest.Deploy_4.5.6.7_x64__t7q2w3e4r5y6";
static const WCHAR staged_path[] = L"C:\\WineTestStage\\WineTest.Deploy_4.5.6.7_x64__t7q2w3e4r5y6";
static const WCHAR appmodel_key[] =
    L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppModel\\Packages\\WineTest.Deploy_4.5.6.7_x64__t7q2w3e4r5y6";

/* the interfaces data the mingw import libraries do not carry */
static const IID IID_iinspectable = {0xaf86e2e0, 0xb12d, 0x4c6a, {0x9c, 0x5a, 0xd7, 0xaa, 0x65, 0x10, 0x1e, 0x90}};
static const IID IID_iuri = {0x9e92e132, 0x23be, 0x4f41, {0xba, 0x42, 0x32, 0xe3, 0x94, 0x6f, 0x7b, 0x4f}};
static const IID IID_iasyncinfo = {0x00000036, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
static const IID IID_ipm = {0x9a7d4b65, 0x5e8f, 0x4fc7, {0xa2, 0xe5, 0x7f, 0x69, 0x25, 0xcb, 0x8b, 0x53}};
static const IID IID_ipm2 = {0xf7aad08d, 0x0840, 0x46f2, {0xb5, 0xd8, 0xca, 0xd4, 0x76, 0x93, 0xa0, 0x95}};
static const IID IID_istorageitem = {0x4207a996, 0xca2f, 0x42f7, {0xbd, 0xe8, 0x8b, 0x10, 0x45, 0x7a, 0x7f, 0x30}};

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

/* the registration the deployment left behind must come back through the
 * collection faces: FindPackages walks every record, the family lookup finds
 * exactly this one, the full-name lookup answers its identity and path, and
 * an unregistered name is E_NOT_FOUND */
static void test_find_packages( IPackageManager *pm )
{
    static const WCHAR family[] = L"WineTest.Deploy_t7q2w3e4r5y6";
    static const WCHAR absent[] = L"WineTest.Absent_9.9.9.9_x64__zzzzzzzzzzzz";
    IIterable_Package *iterable;
    IIterator_Package *iter;
    IPackageManager2 *pm2;
    IPackage *pkg, *found;
    IPackage *items[4];
    IPackageId *id;
    HSTRING str;
    HRESULT hr;
    boolean has, framework = TRUE;
    ULONG total = 0;

    str = NULL;
    WindowsCreateString( full_name, ARRAY_SIZE(full_name) - 1, &str );
    iterable = NULL;
    hr = IPackageManager_FindPackages( pm, &iterable );
    WindowsDeleteString( str );
    ok( hr == S_OK && iterable, "FindPackages hr %#lx iterable %p\n", hr, iterable );
    if (!iterable) return;

    /* the collection contains the registered package, and its id reads back */
    found = NULL;
    if (SUCCEEDED( (hr = IIterable_Package_First( iterable, &iter ) ) ))
    {
        has = TRUE;
        IIterator_Package_get_HasCurrent( iter, &has );
        while (has)
        {
            total++;
            pkg = NULL;
            if (SUCCEEDED( IIterator_Package_get_Current( iter, &pkg ) ) && pkg)
            {
                HSTRING name = NULL;
                if (SUCCEEDED( IPackage_get_Id( pkg, &id ) ) )
                {
                    IPackageId_get_FullName( id, &name );
                    IPackageId_Release( id );
                }
                if (name && !wcscmp( WindowsGetStringRawBuffer( name, NULL ),
                                     WindowsGetStringRawBuffer( str, NULL ) ))
                {
                    found = pkg;
                    WindowsDeleteString( name );
                    break;
                }
                WindowsDeleteString( name );
            }
            IIterator_Package_MoveNext( iter, &has );
        }
        IIterator_Package_Release( iter );
    }
    ok( !!found, "FindPackages did not enumerate %s (total %lu)\n", debugstr_w( full_name ), total );
    if (found)
    {
        HSTRING fam = NULL;
        IStorageFolder *folder;
        IStorageItem *item;
        HSTRING path = NULL;

        if (SUCCEEDED( IPackage_get_Id( found, &id ) ) )
        {
            IPackageId_get_FamilyName( id, &fam );
            IPackageId_Release( id );
        }
        WindowsCreateString( family, ARRAY_SIZE(family) - 1, &str );
        ok( fam && !wcscmp( WindowsGetStringRawBuffer( fam, NULL ), WindowsGetStringRawBuffer( str, NULL ) ),
                "FamilyName %s, expected %s\n", debugstr_hstring( fam ), debugstr_w( family ) );
        WindowsDeleteString( str );
        WindowsDeleteString( fam );

        IPackage_get_IsFramework( found, &framework );
        ok( !framework, "IsFramework %d\n", framework );

        /* InstalledLocation is the registered path */
        if (SUCCEEDED( IPackage_get_InstalledLocation( found, &folder ) ) )
        {
            if (SUCCEEDED( IStorageFolder_QueryInterface( folder, (void *)&IID_istorageitem, (void **)&item ) ) )
            {
                IStorageItem_get_Path( item, &path );
                IStorageItem_Release( item );
            }
            IStorageFolder_Release( folder );
        }
        ok( path && !wcscmp( WindowsGetStringRawBuffer( path, NULL ), staged_path ),
                "InstalledLocation path %s, expected %s\n", debugstr_hstring( path ), debugstr_w( staged_path ) );
        WindowsDeleteString( path );
    }
    IIterable_Package_Release( iterable );

    /* the family filter answers exactly this deployment */
    WindowsCreateString( family, ARRAY_SIZE(family) - 1, &str );
    iterable = NULL;
    hr = IPackageManager_FindPackagesByPackageFamilyName( pm, str, &iterable );
    WindowsDeleteString( str );
    ok( hr == S_OK && iterable, "FindPackagesByPackageFamilyName hr %#lx\n", hr );
    if (iterable)
    {
        UINT32 written = 0;
        if (SUCCEEDED( IIterable_Package_First( iterable, &iter ) ) )
        {
            hr = IIterator_Package_GetMany( iter, ARRAY_SIZE(items), items, &written );
            ok( hr == S_OK && written == 1, "family collection size %u hr %#lx\n", (unsigned int)written, hr );
            for (UINT32 i = 0; i < written; i++) IPackage_Release( items[i] );
            IIterator_Package_Release( iter );
        }
        IIterable_Package_Release( iterable );
    }

    /* full-name lookup: registered answers, absent is E_NOT_FOUND */
    pkg = NULL;
    WindowsCreateString( full_name, ARRAY_SIZE(full_name) - 1, &str );
    hr = IPackageManager_FindPackageByPackageFullName( pm, str, &pkg );
    WindowsDeleteString( str );
    ok( hr == S_OK && pkg, "FindPackageByPackageFullName hr %#lx pkg %p\n", hr, pkg );
    if (pkg) IPackage_Release( pkg );

    pkg = (void *)0xdead;
    WindowsCreateString( absent, ARRAY_SIZE(absent) - 1, &str );
    hr = IPackageManager_FindPackageByPackageFullName( pm, str, &pkg );
    WindowsDeleteString( str );
    ok( hr == E_NOT_FOUND, "absent package hr %#lx, expected E_NOT_FOUND\n", hr );

    /* the same face through the WithPackageTypes overload (App|Framework) */
    if (SUCCEEDED( (hr = IPackageManager_QueryInterface( pm, (void *)&IID_ipm2, (void **)&pm2 ) ) ) )
    {
        iterable = NULL;
        WindowsCreateString( family, ARRAY_SIZE(family) - 1, &str );
        hr = IPackageManager2_FindPackagesByPackageFamilyNameWithPackageTypes( pm2,
                str, PackageTypes_All, &iterable );
        WindowsDeleteString( str );
        ok( hr == S_OK && iterable, "WithPackageTypes hr %#lx iterable %p\n", hr, iterable );
        if (iterable) IIterable_Package_Release( iterable );
        IPackageManager2_Release( pm2 );
    }
    else skip( "IPackageManager2 QI hr %#lx\n", hr );
}

/* CoCreateInstance(CLSID_PackageManager) reaches the same object the
 * activation factory activates */
static void test_class_factory( void )
{
    static const IID clsid_pm = {0x4df1538a, 0x1819, 0x4822, {0x81, 0xa4, 0x4d, 0xf6, 0x79, 0x5e, 0xa7, 0x89}};
    static const IID clsid_other = {0x11111111, 0x2222, 0x3333, {0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb}};
    static const IID iid_icf = {0x00000001, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
    HRESULT (WINAPI *p_DllGetClassObject)( const IID *, const IID *, void ** );
    IClassFactory *cf;
    IPackageManager *pm;
    IIterable_Package *iterable;
    HMODULE mod;
    void *obj;
    HRESULT hr;

    mod = LoadLibraryA( "appxdeploymentclient.dll" );
    if (!mod) return;
    p_DllGetClassObject = (void *)GetProcAddress( mod, "DllGetClassObject" );
    ok( p_DllGetClassObject != NULL, "DllGetClassObject not exported\n" );
    if (!p_DllGetClassObject)
    {
        FreeLibrary( mod );
        return;
    }

    obj = NULL;
    hr = p_DllGetClassObject( &clsid_pm, &iid_icf, &obj );
    ok( hr == S_OK && obj, "class object hr %#lx cf %p\n", hr, obj );
    if (hr != S_OK || !obj)
    {
        FreeLibrary( mod );
        return;
    }
    cf = (IClassFactory *)obj;

    pm = NULL;
    hr = IClassFactory_CreateInstance( cf, NULL, (void *)&IID_ipm, (void **)&pm );
    ok( hr == S_OK && pm, "CreateInstance hr %#lx pm %p\n", hr, pm );
    if (pm)
    {
        iterable = NULL;
        hr = IPackageManager_FindPackages( pm, &iterable );
        ok( hr == S_OK && iterable, "FindPackages through class factory hr %#lx\n", hr );
        if (iterable) IIterable_Package_Release( iterable );
        IPackageManager_Release( pm );
    }

    /* the default (IPackageManager) interface answers too */
    obj = NULL;
    hr = IClassFactory_CreateInstance( cf, NULL, (void *)&IID_ipm, &obj );
    ok( hr == S_OK && obj, "CreateInstance(IPackageManager) hr %#lx\n", hr );
    if (obj) IPackageManager_Release( (IPackageManager *)obj );

    hr = IClassFactory_LockServer( cf, TRUE );
    ok( hr == S_OK, "LockServer hr %#lx\n", hr );
    IClassFactory_Release( cf );

    obj = (void *)0xdead;
    hr = p_DllGetClassObject( &clsid_other, &iid_icf, &obj );
    ok( hr == CLASS_E_CLASSNOTAVAILABLE && !obj, "foreign clsid hr %#lx obj %p\n", hr, obj );

    FreeLibrary( mod );
}

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

    /* the deployment is visible through the Find faces before it is cleaned up */
    test_find_packages( pm );

    RegDeleteTreeW( HKEY_CURRENT_USER, appmodel_key );

    IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress_Release( op );
    IPackageManager_Release( pm );
    FreeLibrary( mod );
}

START_TEST( deployment )
{
    test_register_package();
    test_class_factory();
}
