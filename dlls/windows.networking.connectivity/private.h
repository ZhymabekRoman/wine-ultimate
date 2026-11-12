/* WinRT Windows.Networking.Connectivity Implementation
 *
 * Copyright (C) 2024 Mohamad Al-Jaf
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

#ifndef __WINE_WINDOWS_NETWORKING_CONNECTIVITY_PRIVATE_H
#define __WINE_WINDOWS_NETWORKING_CONNECTIVITY_PRIVATE_H

#include <stdarg.h>

#define COBJMACROS
#include "windef.h"
#include "winbase.h"
#include "winstring.h"

#include "activation.h"

#include "wine/debug.h"

#define WIDL_using_Windows_Foundation
#define WIDL_using_Windows_Foundation_Collections
#include "windows.foundation.h"
#define WIDL_using_Windows_Networking
#define WIDL_using_Windows_Networking_Connectivity
#include "windows.networking.connectivity.h"
#include "windows.networking.h"

extern IActivationFactory *network_information_factory;

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

extern const GUID IID_IConnectionProfile3;
extern const GUID IID_IConnectionProfile4;

#ifndef ____x_ABI_CWindows_CNetworking_CConnectivity_CIConnectionProfile3_INTERFACE_DEFINED__
#define ____x_ABI_CWindows_CNetworking_CConnectivity_CIConnectionProfile3_INTERFACE_DEFINED__

typedef interface IConnectionProfile3 IConnectionProfile3;

typedef struct IConnectionProfile3Vtbl
{
    BEGIN_INTERFACE

    /*** IUnknown methods ***/
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(
        IConnectionProfile3 *This,
        REFIID riid,
        void **ppvObject);

    ULONG (STDMETHODCALLTYPE *AddRef)(
        IConnectionProfile3 *This);

    ULONG (STDMETHODCALLTYPE *Release)(
        IConnectionProfile3 *This);

    /*** IInspectable methods ***/
    HRESULT (STDMETHODCALLTYPE *GetIids)(
        IConnectionProfile3 *This,
        ULONG *iidCount,
        IID **iids);

    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(
        IConnectionProfile3 *This,
        HSTRING *className);

    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(
        IConnectionProfile3 *This,
        TrustLevel *trustLevel);

    /*** IConnectionProfile3 methods ***/
    HRESULT (STDMETHODCALLTYPE *GetSignalBars)(
        IConnectionProfile3 *This,
        BYTE *value);

    END_INTERFACE
} IConnectionProfile3Vtbl;

interface IConnectionProfile3
{
    CONST_VTBL struct IConnectionProfile3Vtbl *lpVtbl;
};

#ifdef COBJMACROS
#define IConnectionProfile3_QueryInterface(This,riid,ppvObject) (This)->lpVtbl->QueryInterface(This,riid,ppvObject)
#define IConnectionProfile3_AddRef(This) (This)->lpVtbl->AddRef(This)
#define IConnectionProfile3_Release(This) (This)->lpVtbl->Release(This)
#define IConnectionProfile3_GetIids(This,iidCount,iids) (This)->lpVtbl->GetIids(This,iidCount,iids)
#define IConnectionProfile3_GetRuntimeClassName(This,className) (This)->lpVtbl->GetRuntimeClassName(This,className)
#define IConnectionProfile3_GetTrustLevel(This,trustLevel) (This)->lpVtbl->GetTrustLevel(This,trustLevel)
#define IConnectionProfile3_GetSignalBars(This,value) (This)->lpVtbl->GetSignalBars(This,value)
#endif

#endif /* ____x_ABI_CWindows_CNetworking_CConnectivity_CIConnectionProfile3_INTERFACE_DEFINED__ */

#ifndef ____x_ABI_CWindows_CNetworking_CConnectivity_CIConnectionProfile4_INTERFACE_DEFINED__
#define ____x_ABI_CWindows_CNetworking_CConnectivity_CIConnectionProfile4_INTERFACE_DEFINED__

typedef interface IConnectionProfile4 IConnectionProfile4;

typedef struct IConnectionProfile4Vtbl
{
    BEGIN_INTERFACE

    /*** IUnknown methods ***/
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(
        IConnectionProfile4 *This,
        REFIID riid,
        void **ppvObject);

    ULONG (STDMETHODCALLTYPE *AddRef)(
        IConnectionProfile4 *This);

    ULONG (STDMETHODCALLTYPE *Release)(
        IConnectionProfile4 *This);

    /*** IInspectable methods ***/
    HRESULT (STDMETHODCALLTYPE *GetIids)(
        IConnectionProfile4 *This,
        ULONG *iidCount,
        IID **iids);

    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(
        IConnectionProfile4 *This,
        HSTRING *className);

    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(
        IConnectionProfile4 *This,
        TrustLevel *trustLevel);

    /*** IConnectionProfile4 methods ***/
    HRESULT (STDMETHODCALLTYPE *get_NetworkSecuritySettings)(
        IConnectionProfile4 *This,
        INetworkSecuritySettings **value);

    END_INTERFACE
} IConnectionProfile4Vtbl;

interface IConnectionProfile4
{
    CONST_VTBL struct IConnectionProfile4Vtbl *lpVtbl;
};

#ifdef COBJMACROS
#define IConnectionProfile4_QueryInterface(This,riid,ppvObject) (This)->lpVtbl->QueryInterface(This,riid,ppvObject)
#define IConnectionProfile4_AddRef(This) (This)->lpVtbl->AddRef(This)
#define IConnectionProfile4_Release(This) (This)->lpVtbl->Release(This)
#define IConnectionProfile4_GetIids(This,iidCount,iids) (This)->lpVtbl->GetIids(This,iidCount,iids)
#define IConnectionProfile4_GetRuntimeClassName(This,className) (This)->lpVtbl->GetRuntimeClassName(This,className)
#define IConnectionProfile4_GetTrustLevel(This,trustLevel) (This)->lpVtbl->GetTrustLevel(This,trustLevel)
#define IConnectionProfile4_get_NetworkSecuritySettings(This,value) (This)->lpVtbl->get_NetworkSecuritySettings(This,value)
#endif

#endif /* ____x_ABI_CWindows_CNetworking_CConnectivity_CIConnectionProfile4_INTERFACE_DEFINED__ */

#endif
