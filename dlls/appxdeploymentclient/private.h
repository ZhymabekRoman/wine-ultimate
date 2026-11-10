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
