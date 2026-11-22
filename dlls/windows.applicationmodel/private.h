/* WinRT Windows.ApplicationModel Implementation
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

#ifndef __WINE_WINDOWS_APPLICATIONMODEL_PRIVATE_H
#define __WINE_WINDOWS_APPLICATIONMODEL_PRIVATE_H

#include <stdarg.h>

#define COBJMACROS
#include "windef.h"
#include "winbase.h"
#include "winstring.h"

#include "activation.h"
#include "roapi.h"

#define WIDL_using_Windows_Foundation
#define WIDL_using_Windows_Foundation_Collections
#include "windows.foundation.h"
#define WIDL_using_Windows_ApplicationModel
#define WIDL_using_Windows_Storage
#define WIDL_using_Windows_Storage_Streams
#define WIDL_using_Windows_System
#include "windows.applicationmodel.h"

/* appextensions.c */
extern IActivationFactory *app_extension_catalog_factory;

extern IActivationFactory *package_factory;
extern IActivationFactory *design_mode_factory;
extern IActivationFactory *appinfo_factory;

HRESULT create_package_instance( IPackage **out );
BOOL package_get_reg_str( const WCHAR *prop_name, WCHAR *buf, DWORD cch );
extern const WCHAR appmodel_reg_key[];

/* ---- main.c: shared WindowsAppSDK static-class surface ------------------- *
 * Every WinAppSDK/Store class below is a singleton: its activation factory
 * hands out the one object, which reports its own class name and answers every
 * method slot past IInspectable with a zeroed out-param. main.c owns the two
 * shared vtbls and the IInspectable method bodies; winappsdk_dd.c and
 * store_install.c instantiate classes from them with the macros here.        */
struct winappsdk_class
{
    const void *vtbl;
    const WCHAR *name;
    LONG ref;
};

struct winappsdk_factory
{
    const void *vtbl;
    struct winappsdk_class *obj;
    LONG ref;
};

extern const void *winappsdk_class_vtbl[];
extern const void *winappsdk_factory_vtbl[];

HRESULT WINAPI winappsdk_class_QueryInterface( void *iface, REFIID iid, void **out );
HRESULT WINAPI winappsdk_QueryInterface( void *iface, REFIID iid, void **out );
ULONG WINAPI winappsdk_AddRef( void *iface );
ULONG WINAPI winappsdk_Release( void *iface );
HRESULT WINAPI winappsdk_GetIids( void *iface, ULONG *count, IID **iids );
HRESULT WINAPI winappsdk_GetRuntimeClassName( void *iface, HSTRING *name );
HRESULT WINAPI winappsdk_GetTrustLevel( void *iface, TrustLevel *level );
HRESULT WINAPI winappsdk_zero_out( void *iface, void *out );
HRESULT WINAPI winappsdk_factory_QueryInterface( void *iface, REFIID iid, void **out );
HRESULT WINAPI winappsdk_factory_ActivateInstance( void *iface, IInspectable **instance );
HRESULT WINAPI winappsdk_factory_GetRuntimeClassName( void *iface, HSTRING *name );

#define WINAPPSDK_CLASS( ident, classname ) \
    struct winappsdk_class ident##_obj = { winappsdk_class_vtbl, L##classname, 1 }; \
    struct winappsdk_factory ident##_factory = { winappsdk_factory_vtbl, &ident##_obj, 1 }

#define WINAPPSDK_CLASS_V( ident, vtbl, classname ) \
    struct winappsdk_class ident##_obj = { vtbl, L##classname, 1 }; \
    struct winappsdk_factory ident##_factory = { winappsdk_factory_vtbl, &ident##_obj, 1 }

/* ---- winappsdk_dd.c: WinAppSDK Dynamic Dependencies ---------------------- */
extern struct winappsdk_factory deployment_manager_factory;
extern struct winappsdk_factory deployment_init_options_factory;
extern struct winappsdk_factory deployment_result_factory;
extern struct winappsdk_factory package_dependency_factory;
extern struct winappsdk_factory package_dependency_context_factory;
extern struct winappsdk_factory add_package_dependency_options_factory;
extern struct winappsdk_factory create_package_dependency_options_factory;
extern struct winappsdk_factory package_dependency_rank_factory;
extern struct winappsdk_factory environment_manager_factory;

/* ---- store_install.c: Store, AppInstallManager, StoreContext -------------- */
struct store_configuration_factory_impl
{
    const void *vtbl1;
    const void *vtbl2;
    const void *vtbl3;
    const void *vtbl4;
    const WCHAR *name;
    LONG ref;
};

extern struct store_configuration_factory_impl store_cfg_factory_instance;
extern struct winappsdk_factory app_install_manager_custom_factory;
extern struct winappsdk_factory app_install_options_factory;
extern struct winappsdk_factory app_update_options_factory;
extern struct winappsdk_factory store_system_feature_factory;
extern struct winappsdk_factory web_auth_core_helper_factory;
extern struct winappsdk_factory license_manager_factory;
extern struct winappsdk_factory store_context_custom_factory;

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
