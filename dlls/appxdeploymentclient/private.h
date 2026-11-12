/* WinRT Windows.Management.Deployment Implementation
 *
 * Copyright (C) 2023 Mohamad Al-Jaf
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

#ifndef __WINE_WINDOWS_MANAGEMENT_DEPLOYMENT_PRIVATE_H
#define __WINE_WINDOWS_MANAGEMENT_DEPLOYMENT_PRIVATE_H

#include <stdarg.h>

#define COBJMACROS
#include "windef.h"
#include "winbase.h"
#include "winstring.h"

#include "activation.h"

#define WIDL_using_Windows_Foundation
#define WIDL_using_Windows_Foundation_Collections
#include "windows.foundation.h"
#include "asyncinfo.h"
#define WIDL_using_Windows_System
#define WIDL_using_Windows_Storage
#define WIDL_using_Windows_Management_Deployment
#define WIDL_using_Windows_ApplicationModel
#include "windows.management.deployment.h"

/* winapigen emits the IAsyncOperation<IVectorView<User>> alias macros only in
 * the C++ tail of windows.system.h; this file's C consumers alias them by hand. */
#ifndef IAsyncOperation_IVectorView_User
#define IAsyncOperation_IVectorView_User __FIAsyncOperation_1___FIVectorView_1_Windows__CSystem__CUser
#define IAsyncOperation_IVectorView_UserVtbl __FIAsyncOperation_1___FIVectorView_1_Windows__CSystem__CUserVtbl
#define IID_IAsyncOperation_IVectorView_User IID___FIAsyncOperation_1___FIVectorView_1_Windows__CSystem__CUser
#define IAsyncOperationCompletedHandler_IVectorView_User __FIAsyncOperationCompletedHandler_1___FIVectorView_1_Windows__CSystem__CUser
#define IAsyncOperationCompletedHandler_IVectorView_User_Invoke(h,s,st) ((h)->lpVtbl->Invoke(h,s,st))
#define IID_IAsyncOperationCompletedHandler_IVectorView_User IID___FIAsyncOperationCompletedHandler_1___FIVectorView_1_Windows__CSystem__CUser
#define IVectorView_User __FIVectorView_1_Windows__CSystem__CUser
#define IVectorView_UserVtbl __FIVectorView_1_Windows__CSystem__CUserVtbl
#define IID_IVectorView_User IID___FIVectorView_1_Windows__CSystem__CUser
#endif

extern IActivationFactory *package_manager_factory;
extern IActivationFactory *package_catalog_factory;

#ifndef RuntimeClass_Windows_ApplicationModel_PackageCatalog
#define RuntimeClass_Windows_ApplicationModel_PackageCatalog L"Windows.ApplicationModel.PackageCatalog"
#endif

typedef interface IPackageCatalog IPackageCatalog;
typedef interface IPackageCatalog2 IPackageCatalog2;
typedef interface IPackageCatalog3 IPackageCatalog3;
typedef interface IPackageCatalogStatics IPackageCatalogStatics;
typedef interface IPackageCatalogStatics2 IPackageCatalogStatics2;

static const GUID IID_IPackageCatalog =
    { 0x230a3751, 0x9de3, 0x4445, { 0xbe, 0x74, 0x91, 0xfb, 0x32, 0x5a, 0xbe, 0xfe } };
static const GUID IID_IPackageCatalog2 =
    { 0x96a60c36, 0x8ff7, 0x4344, { 0xb6, 0xbf, 0xee, 0x64, 0xc2, 0x20, 0x7e, 0xd2 } };
static const GUID IID_IPackageCatalog3 =
    { 0x96dd5c88, 0x8837, 0x43f9, { 0x90, 0x15, 0x03, 0x34, 0x34, 0xba, 0x14, 0xf3 } };
static const GUID IID_IPackageCatalogStatics =
    { 0xa18c9696, 0xe65b, 0x4634, { 0xba, 0x21, 0x5e, 0x63, 0xeb, 0x72, 0x44, 0xa7 } };
static const GUID IID_IPackageCatalogStatics2 =
    { 0x4c11b159, 0x9a28, 0x598c, { 0xb1, 0x85, 0x55, 0xe1, 0x89, 0x9b, 0x2b, 0xe4 } };

#undef INTERFACE
#define INTERFACE IPackageCatalogStatics
DECLARE_INTERFACE_IID_(IPackageCatalogStatics, IInspectable, "a18c9696-e65b-4634-ba21-5e63eb7244a7")
{
    /*** IUnknown methods ***/
    STDMETHOD_(HRESULT, QueryInterface)(THIS_ REFIID riid, void **ppvObject) PURE;
    STDMETHOD_(ULONG, AddRef)(THIS) PURE;
    STDMETHOD_(ULONG, Release)(THIS) PURE;
    /*** IInspectable methods ***/
    STDMETHOD_(HRESULT, GetIids)(THIS_ ULONG *iidCount, IID **iids) PURE;
    STDMETHOD_(HRESULT, GetRuntimeClassName)(THIS_ HSTRING *className) PURE;
    STDMETHOD_(HRESULT, GetTrustLevel)(THIS_ TrustLevel *trustLevel) PURE;
    /*** IPackageCatalogStatics methods ***/
    STDMETHOD_(HRESULT, OpenForCurrentPackage)(THIS_ IPackageCatalog **catalog) PURE;
    STDMETHOD_(HRESULT, OpenForCurrentUser)(THIS_ IPackageCatalog **catalog) PURE;
    STDMETHOD_(HRESULT, OpenForPackage)(THIS_ IPackage *package, IPackageCatalog **catalog) PURE;
};
#undef INTERFACE

#undef INTERFACE
#define INTERFACE IPackageCatalogStatics2
DECLARE_INTERFACE_IID_(IPackageCatalogStatics2, IInspectable, "4c11b159-9a28-598c-b185-55e1899b2be4")
{
    /*** IUnknown methods ***/
    STDMETHOD_(HRESULT, QueryInterface)(THIS_ REFIID riid, void **ppvObject) PURE;
    STDMETHOD_(ULONG, AddRef)(THIS) PURE;
    STDMETHOD_(ULONG, Release)(THIS) PURE;
    /*** IInspectable methods ***/
    STDMETHOD_(HRESULT, GetIids)(THIS_ ULONG *iidCount, IID **iids) PURE;
    STDMETHOD_(HRESULT, GetRuntimeClassName)(THIS_ HSTRING *className) PURE;
    STDMETHOD_(HRESULT, GetTrustLevel)(THIS_ TrustLevel *trustLevel) PURE;
    /*** IPackageCatalogStatics2 methods ***/
    STDMETHOD_(HRESULT, OpenForPackage)(THIS_ IPackage *package, IPackageCatalog **catalog) PURE;
};
#undef INTERFACE

#undef INTERFACE
#define INTERFACE IPackageCatalog
DECLARE_INTERFACE_IID_(IPackageCatalog, IInspectable, "230a3751-9de3-4445-be74-91fb325abefe")
{
    /*** IUnknown methods ***/
    STDMETHOD_(HRESULT, QueryInterface)(THIS_ REFIID riid, void **ppvObject) PURE;
    STDMETHOD_(ULONG, AddRef)(THIS) PURE;
    STDMETHOD_(ULONG, Release)(THIS) PURE;
    /*** IInspectable methods ***/
    STDMETHOD_(HRESULT, GetIids)(THIS_ ULONG *iidCount, IID **iids) PURE;
    STDMETHOD_(HRESULT, GetRuntimeClassName)(THIS_ HSTRING *className) PURE;
    STDMETHOD_(HRESULT, GetTrustLevel)(THIS_ TrustLevel *trustLevel) PURE;
    /*** IPackageCatalog methods ***/
    STDMETHOD_(HRESULT, add_PackageStaging)(THIS_ void *handler, EventRegistrationToken *token) PURE;
    STDMETHOD_(HRESULT, remove_PackageStaging)(THIS_ EventRegistrationToken token) PURE;
    STDMETHOD_(HRESULT, add_PackageInstalling)(THIS_ void *handler, EventRegistrationToken *token) PURE;
    STDMETHOD_(HRESULT, remove_PackageInstalling)(THIS_ EventRegistrationToken token) PURE;
    STDMETHOD_(HRESULT, add_PackageUpdating)(THIS_ void *handler, EventRegistrationToken *token) PURE;
    STDMETHOD_(HRESULT, remove_PackageUpdating)(THIS_ EventRegistrationToken token) PURE;
    STDMETHOD_(HRESULT, add_PackageUninstalling)(THIS_ void *handler, EventRegistrationToken *token) PURE;
    STDMETHOD_(HRESULT, remove_PackageUninstalling)(THIS_ EventRegistrationToken token) PURE;
    STDMETHOD_(HRESULT, add_PackageStatusChanged)(THIS_ void *handler, EventRegistrationToken *token) PURE;
    STDMETHOD_(HRESULT, remove_PackageStatusChanged)(THIS_ EventRegistrationToken token) PURE;
};
#undef INTERFACE

#undef INTERFACE
#define INTERFACE IPackageCatalog2
DECLARE_INTERFACE_IID_(IPackageCatalog2, IInspectable, "96a60c36-8ff7-4344-b6bf-ee64c2207ed2")
{
    /*** IUnknown methods ***/
    STDMETHOD_(HRESULT, QueryInterface)(THIS_ REFIID riid, void **ppvObject) PURE;
    STDMETHOD_(ULONG, AddRef)(THIS) PURE;
    STDMETHOD_(ULONG, Release)(THIS) PURE;
    /*** IInspectable methods ***/
    STDMETHOD_(HRESULT, GetIids)(THIS_ ULONG *iidCount, IID **iids) PURE;
    STDMETHOD_(HRESULT, GetRuntimeClassName)(THIS_ HSTRING *className) PURE;
    STDMETHOD_(HRESULT, GetTrustLevel)(THIS_ TrustLevel *trustLevel) PURE;
    /*** IPackageCatalog2 methods ***/
    STDMETHOD_(HRESULT, add_PackageContentGroupStaging)(THIS_ void *handler, EventRegistrationToken *token) PURE;
    STDMETHOD_(HRESULT, remove_PackageContentGroupStaging)(THIS_ EventRegistrationToken token) PURE;
    STDMETHOD_(HRESULT, AddOptionalPackageAsync)(THIS_ HSTRING optionalPackageFamilyName, void **operation) PURE;
};
#undef INTERFACE

#undef INTERFACE
#define INTERFACE IPackageCatalog3
DECLARE_INTERFACE_IID_(IPackageCatalog3, IInspectable, "96dd5c88-8837-43f9-9015-033434ba14f3")
{
    /*** IUnknown methods ***/
    STDMETHOD_(HRESULT, QueryInterface)(THIS_ REFIID riid, void **ppvObject) PURE;
    STDMETHOD_(ULONG, AddRef)(THIS) PURE;
    STDMETHOD_(ULONG, Release)(THIS) PURE;
    /*** IInspectable methods ***/
    STDMETHOD_(HRESULT, GetIids)(THIS_ ULONG *iidCount, IID **iids) PURE;
    STDMETHOD_(HRESULT, GetRuntimeClassName)(THIS_ HSTRING *className) PURE;
    STDMETHOD_(HRESULT, GetTrustLevel)(THIS_ TrustLevel *trustLevel) PURE;
    /*** IPackageCatalog3 methods ***/
    STDMETHOD_(HRESULT, RemoveOptionalPackagesAsync)(THIS_ void *optionalPackageFamilyNames, void **operation) PURE;
};
#undef INTERFACE

#define IPackageCatalogStatics_QueryInterface(This,riid,ppvObject) (This)->lpVtbl->QueryInterface(This,riid,ppvObject)
#define IPackageCatalogStatics_AddRef(This) (This)->lpVtbl->AddRef(This)
#define IPackageCatalogStatics_Release(This) (This)->lpVtbl->Release(This)
#define IPackageCatalogStatics_OpenForCurrentPackage(This,catalog) (This)->lpVtbl->OpenForCurrentPackage(This,catalog)
#define IPackageCatalogStatics_OpenForCurrentUser(This,catalog) (This)->lpVtbl->OpenForCurrentUser(This,catalog)
#define IPackageCatalogStatics_OpenForPackage(This,package,catalog) (This)->lpVtbl->OpenForPackage(This,package,catalog)

#define IPackageCatalogStatics2_QueryInterface(This,riid,ppvObject) (This)->lpVtbl->QueryInterface(This,riid,ppvObject)
#define IPackageCatalogStatics2_AddRef(This) (This)->lpVtbl->AddRef(This)
#define IPackageCatalogStatics2_Release(This) (This)->lpVtbl->Release(This)
#define IPackageCatalogStatics2_OpenForPackage(This,package,catalog) (This)->lpVtbl->OpenForPackage(This,package,catalog)

#define IPackageCatalog_QueryInterface(This,riid,ppvObject) (This)->lpVtbl->QueryInterface(This,riid,ppvObject)
#define IPackageCatalog_AddRef(This) (This)->lpVtbl->AddRef(This)
#define IPackageCatalog_Release(This) (This)->lpVtbl->Release(This)
#define IPackageCatalog_add_PackageStaging(This,handler,token) (This)->lpVtbl->add_PackageStaging(This,handler,token)
#define IPackageCatalog_remove_PackageStaging(This,token) (This)->lpVtbl->remove_PackageStaging(This,token)
#define IPackageCatalog_add_PackageInstalling(This,handler,token) (This)->lpVtbl->add_PackageInstalling(This,handler,token)
#define IPackageCatalog_remove_PackageInstalling(This,token) (This)->lpVtbl->remove_PackageInstalling(This,token)
#define IPackageCatalog_add_PackageUpdating(This,handler,token) (This)->lpVtbl->add_PackageUpdating(This,handler,token)
#define IPackageCatalog_remove_PackageUpdating(This,token) (This)->lpVtbl->remove_PackageUpdating(This,token)
#define IPackageCatalog_add_PackageUninstalling(This,handler,token) (This)->lpVtbl->add_PackageUninstalling(This,handler,token)
#define IPackageCatalog_remove_PackageUninstalling(This,token) (This)->lpVtbl->remove_PackageUninstalling(This,token)
#define IPackageCatalog_add_PackageStatusChanged(This,handler,token) (This)->lpVtbl->add_PackageStatusChanged(This,handler,token)
#define IPackageCatalog_remove_PackageStatusChanged(This,token) (This)->lpVtbl->remove_PackageStatusChanged(This,token)

#define IPackageCatalog2_QueryInterface(This,riid,ppvObject) (This)->lpVtbl->QueryInterface(This,riid,ppvObject)
#define IPackageCatalog2_AddRef(This) (This)->lpVtbl->AddRef(This)
#define IPackageCatalog2_Release(This) (This)->lpVtbl->Release(This)
#define IPackageCatalog2_add_PackageContentGroupStaging(This,handler,token) (This)->lpVtbl->add_PackageContentGroupStaging(This,handler,token)
#define IPackageCatalog2_remove_PackageContentGroupStaging(This,token) (This)->lpVtbl->remove_PackageContentGroupStaging(This,token)
#define IPackageCatalog2_AddOptionalPackageAsync(This,name,op) (This)->lpVtbl->AddOptionalPackageAsync(This,name,op)

#define IPackageCatalog3_QueryInterface(This,riid,ppvObject) (This)->lpVtbl->QueryInterface(This,riid,ppvObject)
#define IPackageCatalog3_AddRef(This) (This)->lpVtbl->AddRef(This)
#define IPackageCatalog3_Release(This) (This)->lpVtbl->Release(This)
#define IPackageCatalog3_RemoveOptionalPackagesAsync(This,names,op) (This)->lpVtbl->RemoveOptionalPackagesAsync(This,names,op)

HRESULT package_manager_create( IPackageManager **out );
HRESULT WINAPI AppxRegisterPackageManifest( const WCHAR *manifest_path );
HRESULT WINAPI AppxUnregisterPackageByName( const WCHAR *package_full_name );

#define DEFINE_IINSPECTABLE_( pfx, iface_type, impl_type, impl_from, iface_mem, expr )             \
    static inline impl_type *impl_from( iface_type *iface )                                        \
    {                                                                                              \
        return CONTAINING_RECORD( iface, impl_type, iface_mem );                                   \
    }                                                                                              \
    static HRESULT WINAPI pfx##_QueryInterface( iface_type *iface, REFIID iid, void **out )        \
    {                                                                                              \
        impl_type *impl = impl_from( iface );                                                      \
        return IInspectable_QueryInterface( (IInspectable *)(expr), iid, out );                    \
    }                                                                                              \
    static ULONG WINAPI pfx##_AddRef( iface_type *iface )                                          \
    {                                                                                              \
        impl_type *impl = impl_from( iface );                                                      \
        return IInspectable_AddRef( (IInspectable *)(expr) );                                      \
    }                                                                                              \
    static ULONG WINAPI pfx##_Release( iface_type *iface )                                         \
    {                                                                                              \
        impl_type *impl = impl_from( iface );                                                      \
        return IInspectable_Release( (IInspectable *)(expr) );                                     \
    }                                                                                              \
    static HRESULT WINAPI pfx##_GetIids( iface_type *iface, ULONG *iid_count, IID **iids )         \
    {                                                                                              \
        impl_type *impl = impl_from( iface );                                                      \
        return IInspectable_GetIids( (IInspectable *)(expr), iid_count, iids );                    \
    }                                                                                              \
    static HRESULT WINAPI pfx##_GetRuntimeClassName( iface_type *iface, HSTRING *class_name )      \
    {                                                                                              \
        impl_type *impl = impl_from( iface );                                                      \
        return IInspectable_GetRuntimeClassName( (IInspectable *)(expr), class_name );             \
    }                                                                                              \
    static HRESULT WINAPI pfx##_GetTrustLevel( iface_type *iface, TrustLevel *trust_level )        \
    {                                                                                              \
        impl_type *impl = impl_from( iface );                                                      \
        return IInspectable_GetTrustLevel( (IInspectable *)(expr), trust_level );                  \
    }
#define DEFINE_IINSPECTABLE( pfx, iface_type, impl_type, base_iface )                              \
    DEFINE_IINSPECTABLE_( pfx, iface_type, impl_type, impl_from_##iface_type, iface_type##_iface, &impl->base_iface )

#endif
