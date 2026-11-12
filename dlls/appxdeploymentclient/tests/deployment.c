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

typedef interface IPackageCatalog IPackageCatalog;
typedef interface IPackageCatalog2 IPackageCatalog2;
typedef interface IPackageCatalog3 IPackageCatalog3;
typedef interface IPackageCatalogStatics IPackageCatalogStatics;
typedef interface IPackageCatalogStatics2 IPackageCatalogStatics2;

static const IID IID_ipackagecatalog = {0x230a3751, 0x9de3, 0x4445, {0xbe, 0x74, 0x91, 0xfb, 0x32, 0x5a, 0xbe, 0xfe}};
static const IID IID_ipackagecatalog2 = {0x96a60c36, 0x8ff7, 0x4344, {0xb6, 0xbf, 0xee, 0x64, 0xc2, 0x20, 0x7e, 0xd2}};
static const IID IID_ipackagecatalog3 = {0x96dd5c88, 0x8837, 0x43f9, {0x90, 0x15, 0x03, 0x34, 0x34, 0xba, 0x14, 0xf3}};
static const IID IID_ipackagecatalogstatics = {0xa18c9696, 0xe65b, 0x4634, {0xba, 0x21, 0x5e, 0x63, 0xeb, 0x72, 0x44, 0xa7}};
static const IID IID_ipackagecatalogstatics2 = {0x4c11b159, 0x9a28, 0x598c, {0xb1, 0x85, 0x55, 0xe1, 0x89, 0x9b, 0x2b, 0xe4}};

struct IPackageCatalogStaticsVtbl
{
    HRESULT (WINAPI *QueryInterface)(IPackageCatalogStatics *This, REFIID riid, void **ppvObject);
    ULONG (WINAPI *AddRef)(IPackageCatalogStatics *This);
    ULONG (WINAPI *Release)(IPackageCatalogStatics *This);
    HRESULT (WINAPI *GetIids)(IPackageCatalogStatics *This, ULONG *iidCount, IID **iids);
    HRESULT (WINAPI *GetRuntimeClassName)(IPackageCatalogStatics *This, HSTRING *className);
    HRESULT (WINAPI *GetTrustLevel)(IPackageCatalogStatics *This, TrustLevel *trustLevel);
    HRESULT (WINAPI *OpenForCurrentPackage)(IPackageCatalogStatics *This, IPackageCatalog **catalog);
    HRESULT (WINAPI *OpenForCurrentUser)(IPackageCatalogStatics *This, IPackageCatalog **catalog);
    HRESULT (WINAPI *OpenForPackage)(IPackageCatalogStatics *This, IPackage *package, IPackageCatalog **catalog);
};
struct IPackageCatalogStatics
{
    const struct IPackageCatalogStaticsVtbl *lpVtbl;
};

struct IPackageCatalogStatics2Vtbl
{
    HRESULT (WINAPI *QueryInterface)(IPackageCatalogStatics2 *This, REFIID riid, void **ppvObject);
    ULONG (WINAPI *AddRef)(IPackageCatalogStatics2 *This);
    ULONG (WINAPI *Release)(IPackageCatalogStatics2 *This);
    HRESULT (WINAPI *GetIids)(IPackageCatalogStatics2 *This, ULONG *iidCount, IID **iids);
    HRESULT (WINAPI *GetRuntimeClassName)(IPackageCatalogStatics2 *This, HSTRING *className);
    HRESULT (WINAPI *GetTrustLevel)(IPackageCatalogStatics2 *This, TrustLevel *trustLevel);
    HRESULT (WINAPI *OpenForPackage)(IPackageCatalogStatics2 *This, IPackage *package, IPackageCatalog **catalog);
};
struct IPackageCatalogStatics2
{
    const struct IPackageCatalogStatics2Vtbl *lpVtbl;
};

struct IPackageCatalogVtbl
{
    HRESULT (WINAPI *QueryInterface)(IPackageCatalog *This, REFIID riid, void **ppvObject);
    ULONG (WINAPI *AddRef)(IPackageCatalog *This);
    ULONG (WINAPI *Release)(IPackageCatalog *This);
    HRESULT (WINAPI *GetIids)(IPackageCatalog *This, ULONG *iidCount, IID **iids);
    HRESULT (WINAPI *GetRuntimeClassName)(IPackageCatalog *This, HSTRING *className);
    HRESULT (WINAPI *GetTrustLevel)(IPackageCatalog *This, TrustLevel *trustLevel);
    HRESULT (WINAPI *add_PackageStaging)(IPackageCatalog *This, void *handler, EventRegistrationToken *token);
    HRESULT (WINAPI *remove_PackageStaging)(IPackageCatalog *This, EventRegistrationToken token);
    HRESULT (WINAPI *add_PackageInstalling)(IPackageCatalog *This, void *handler, EventRegistrationToken *token);
    HRESULT (WINAPI *remove_PackageInstalling)(IPackageCatalog *This, EventRegistrationToken token);
    HRESULT (WINAPI *add_PackageUpdating)(IPackageCatalog *This, void *handler, EventRegistrationToken *token);
    HRESULT (WINAPI *remove_PackageUpdating)(IPackageCatalog *This, EventRegistrationToken token);
    HRESULT (WINAPI *add_PackageUninstalling)(IPackageCatalog *This, void *handler, EventRegistrationToken *token);
    HRESULT (WINAPI *remove_PackageUninstalling)(IPackageCatalog *This, EventRegistrationToken token);
    HRESULT (WINAPI *add_PackageStatusChanged)(IPackageCatalog *This, void *handler, EventRegistrationToken *token);
    HRESULT (WINAPI *remove_PackageStatusChanged)(IPackageCatalog *This, EventRegistrationToken token);
};
struct IPackageCatalog
{
    const struct IPackageCatalogVtbl *lpVtbl;
};

struct IPackageCatalog2Vtbl
{
    HRESULT (WINAPI *QueryInterface)(IPackageCatalog2 *This, REFIID riid, void **ppvObject);
    ULONG (WINAPI *AddRef)(IPackageCatalog2 *This);
    ULONG (WINAPI *Release)(IPackageCatalog2 *This);
    HRESULT (WINAPI *GetIids)(IPackageCatalog2 *This, ULONG *iidCount, IID **iids);
    HRESULT (WINAPI *GetRuntimeClassName)(IPackageCatalog2 *This, HSTRING *className);
    HRESULT (WINAPI *GetTrustLevel)(IPackageCatalog2 *This, TrustLevel *trustLevel);
    HRESULT (WINAPI *add_PackageContentGroupStaging)(IPackageCatalog2 *This, void *handler, EventRegistrationToken *token);
    HRESULT (WINAPI *remove_PackageContentGroupStaging)(IPackageCatalog2 *This, EventRegistrationToken token);
    HRESULT (WINAPI *AddOptionalPackageAsync)(IPackageCatalog2 *This, HSTRING name, void **operation);
};
struct IPackageCatalog2
{
    const struct IPackageCatalog2Vtbl *lpVtbl;
};

struct IPackageCatalog3Vtbl
{
    HRESULT (WINAPI *QueryInterface)(IPackageCatalog3 *This, REFIID riid, void **ppvObject);
    ULONG (WINAPI *AddRef)(IPackageCatalog3 *This);
    ULONG (WINAPI *Release)(IPackageCatalog3 *This);
    HRESULT (WINAPI *GetIids)(IPackageCatalog3 *This, ULONG *iidCount, IID **iids);
    HRESULT (WINAPI *GetRuntimeClassName)(IPackageCatalog3 *This, HSTRING *className);
    HRESULT (WINAPI *GetTrustLevel)(IPackageCatalog3 *This, TrustLevel *trustLevel);
    HRESULT (WINAPI *RemoveOptionalPackagesAsync)(IPackageCatalog3 *This, void *names, void **operation);
};
struct IPackageCatalog3
{
    const struct IPackageCatalog3Vtbl *lpVtbl;
};

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

static void test_package_catalog( void )
{
    static const WCHAR class_name[] = L"Windows.ApplicationModel.PackageCatalog";
    HRESULT (WINAPI *p_DllGetActivationFactory)( HSTRING, IActivationFactory ** );
    HMODULE mod;
    HSTRING str;
    IActivationFactory *factory = NULL;
    IInspectable *inspectable = NULL;
    IPackageCatalogStatics *statics = NULL;
    IPackageCatalogStatics2 *statics2 = NULL;
    IPackageCatalog *catalog = NULL;
    IPackageCatalog *catalog2 = NULL;
    IPackageCatalog2 *cat2 = NULL;
    IPackageCatalog3 *cat3 = NULL;
    EventRegistrationToken token;
    HRESULT hr;

    mod = LoadLibraryA( "appxdeploymentclient.dll" );
    ok( mod != NULL, "failed to load appxdeploymentclient.dll\n" );
    if (!mod) return;

    p_DllGetActivationFactory = (void *)GetProcAddress( mod, "DllGetActivationFactory" );
    ok( p_DllGetActivationFactory != NULL, "DllGetActivationFactory not exported\n" );
    if (!p_DllGetActivationFactory)
    {
        FreeLibrary( mod );
        return;
    }

    hr = WindowsCreateString( class_name, ARRAY_SIZE(class_name) - 1, &str );
    ok( hr == S_OK, "WindowsCreateString hr %#lx\n", hr );

    hr = p_DllGetActivationFactory( str, &factory );
    ok( hr == S_OK && factory != NULL, "DllGetActivationFactory hr %#lx factory %p\n", hr, factory );
    WindowsDeleteString( str );
    if (FAILED( hr ) || !factory)
    {
        FreeLibrary( mod );
        return;
    }

    /* 1. Test IActivationFactory::ActivateInstance */
    hr = IActivationFactory_ActivateInstance( factory, &inspectable );
    ok( hr == S_OK && inspectable != NULL, "ActivateInstance hr %#lx inspectable %p\n", hr, inspectable );
    if (SUCCEEDED( hr ) && inspectable)
    {
        catalog = NULL;
        hr = IInspectable_QueryInterface( inspectable, &IID_ipackagecatalog, (void **)&catalog );
        ok( hr == S_OK && catalog != NULL, "QueryInterface(IPackageCatalog) hr %#lx\n", hr );
        if (catalog) catalog->lpVtbl->Release( catalog );
        IInspectable_Release( inspectable );
    }

    /* 2. Test IPackageCatalogStatics */
    hr = IActivationFactory_QueryInterface( factory, &IID_ipackagecatalogstatics, (void **)&statics );
    ok( hr == S_OK && statics != NULL, "QI(IPackageCatalogStatics) hr %#lx\n", hr );
    if (SUCCEEDED( hr ) && statics)
    {
        /* OpenForCurrentUser */
        catalog = NULL;
        hr = statics->lpVtbl->OpenForCurrentUser( statics, &catalog );
        ok( hr == S_OK && catalog != NULL, "OpenForCurrentUser hr %#lx catalog %p\n", hr, catalog );

        /* OpenForCurrentPackage */
        catalog2 = NULL;
        hr = statics->lpVtbl->OpenForCurrentPackage( statics, &catalog2 );
        ok( hr == S_OK && catalog2 != NULL, "OpenForCurrentPackage hr %#lx catalog2 %p\n", hr, catalog2 );
        if (catalog2) catalog2->lpVtbl->Release( catalog2 );

        /* OpenForPackage on statics */
        catalog2 = NULL;
        hr = statics->lpVtbl->OpenForPackage( statics, NULL, &catalog2 );
        ok( hr == S_OK && catalog2 != NULL, "OpenForPackage hr %#lx catalog2 %p\n", hr, catalog2 );
        if (catalog2) catalog2->lpVtbl->Release( catalog2 );

        statics->lpVtbl->Release( statics );
    }

    /* 3. Test IPackageCatalogStatics2 */
    hr = IActivationFactory_QueryInterface( factory, &IID_ipackagecatalogstatics2, (void **)&statics2 );
    ok( hr == S_OK && statics2 != NULL, "QI(IPackageCatalogStatics2) hr %#lx\n", hr );
    if (SUCCEEDED( hr ) && statics2)
    {
        catalog2 = NULL;
        hr = statics2->lpVtbl->OpenForPackage( statics2, NULL, &catalog2 );
        ok( hr == S_OK && catalog2 != NULL, "statics2 OpenForPackage hr %#lx\n", hr );
        if (catalog2) catalog2->lpVtbl->Release( catalog2 );

        statics2->lpVtbl->Release( statics2 );
    }

    /* 4. Test IPackageCatalog event add/remove methods */
    ok( catalog != NULL, "catalog should be valid\n" );
    if (catalog)
    {
        /* PackageInstalling */
        token.value = 0;
        hr = catalog->lpVtbl->add_PackageInstalling( catalog, (void *)0xdead, &token );
        ok( hr == S_OK && token.value != 0, "add_PackageInstalling hr %#lx token %I64d\n", hr, token.value );
        hr = catalog->lpVtbl->remove_PackageInstalling( catalog, token );
        ok( hr == S_OK, "remove_PackageInstalling hr %#lx\n", hr );

        /* PackageUpdating */
        token.value = 0;
        hr = catalog->lpVtbl->add_PackageUpdating( catalog, (void *)0xdead, &token );
        ok( hr == S_OK && token.value != 0, "add_PackageUpdating hr %#lx token %I64d\n", hr, token.value );
        hr = catalog->lpVtbl->remove_PackageUpdating( catalog, token );
        ok( hr == S_OK, "remove_PackageUpdating hr %#lx\n", hr );

        /* PackageUninstalling */
        token.value = 0;
        hr = catalog->lpVtbl->add_PackageUninstalling( catalog, (void *)0xdead, &token );
        ok( hr == S_OK && token.value != 0, "add_PackageUninstalling hr %#lx token %I64d\n", hr, token.value );
        hr = catalog->lpVtbl->remove_PackageUninstalling( catalog, token );
        ok( hr == S_OK, "remove_PackageUninstalling hr %#lx\n", hr );

        /* PackageStatusChanged */
        token.value = 0;
        hr = catalog->lpVtbl->add_PackageStatusChanged( catalog, (void *)0xdead, &token );
        ok( hr == S_OK && token.value != 0, "add_PackageStatusChanged hr %#lx token %I64d\n", hr, token.value );
        hr = catalog->lpVtbl->remove_PackageStatusChanged( catalog, token );
        ok( hr == S_OK, "remove_PackageStatusChanged hr %#lx\n", hr );

        /* PackageStaging */
        token.value = 0;
        hr = catalog->lpVtbl->add_PackageStaging( catalog, (void *)0xdead, &token );
        ok( hr == S_OK && token.value != 0, "add_PackageStaging hr %#lx token %I64d\n", hr, token.value );
        hr = catalog->lpVtbl->remove_PackageStaging( catalog, token );
        ok( hr == S_OK, "remove_PackageStaging hr %#lx\n", hr );

        /* 5. Test IPackageCatalog2 stub */
        cat2 = NULL;
        hr = catalog->lpVtbl->QueryInterface( catalog, &IID_ipackagecatalog2, (void **)&cat2 );
        ok( hr == S_OK && cat2 != NULL, "QI(IPackageCatalog2) hr %#lx\n", hr );
        if (SUCCEEDED( hr ) && cat2)
        {
            token.value = 0;
            hr = cat2->lpVtbl->add_PackageContentGroupStaging( cat2, (void *)0xdead, &token );
            ok( hr == S_OK && token.value != 0, "add_PackageContentGroupStaging hr %#lx\n", hr );
            hr = cat2->lpVtbl->remove_PackageContentGroupStaging( cat2, token );
            ok( hr == S_OK, "remove_PackageContentGroupStaging hr %#lx\n", hr );

            hr = cat2->lpVtbl->AddOptionalPackageAsync( cat2, NULL, NULL );
            ok( hr == E_NOTIMPL, "AddOptionalPackageAsync hr %#lx\n", hr );

            cat2->lpVtbl->Release( cat2 );
        }

        /* 6. Test IPackageCatalog3 stub */
        cat3 = NULL;
        hr = catalog->lpVtbl->QueryInterface( catalog, &IID_ipackagecatalog3, (void **)&cat3 );
        ok( hr == S_OK && cat3 != NULL, "QI(IPackageCatalog3) hr %#lx\n", hr );
        if (SUCCEEDED( hr ) && cat3)
        {
            hr = cat3->lpVtbl->RemoveOptionalPackagesAsync( cat3, NULL, NULL );
            ok( hr == E_NOTIMPL, "RemoveOptionalPackagesAsync hr %#lx\n", hr );

            cat3->lpVtbl->Release( cat3 );
        }

        catalog->lpVtbl->Release( catalog );
    }

    IActivationFactory_Release( factory );
    FreeLibrary( mod );
}

START_TEST( deployment )
{
    test_register_package();
    test_class_factory();
    test_package_catalog();
}
