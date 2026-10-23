/*
 * Copyright 2023 Rémi Bernon for CodeWeavers
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
#include <stddef.h>

#include "windef.h"
#include "winbase.h"

#define COBJMACROS
#include "winstring.h"
#include "appnotify.h"
#include "activation.h"
#include "shlobj.h"

#define WIDL_using_Windows_Foundation
#define WIDL_using_Windows_Foundation_Collections
#define WIDL_using_Windows_Security_ExchangeActiveSyncProvisioning
#define WIDL_using_Windows_System_Profile
#define WIDL_using_Windows_System_Power
#define WIDL_using_Windows_System_UserProfile
#define WIDL_using_Windows_UI_ViewManagement
#define WIDL_using_Windows_UI_Notifications
#define WIDL_using_Windows_ApplicationModel_Core
#define WIDL_using_Windows_ApplicationModel_DataTransfer
#define WIDL_using_Windows_Storage
#define WIDL_using_Windows_Storage_Streams

#include "windows.foundation.h"
#include "windows.security.exchangeactivesyncprovisioning.h"
#include "windows.system.profile.h"
#include "windows.system.power.h"
#include "windows.system.userprofile.h"
#include "windows.ui.viewmanagement.h"
#include "windows.ui.notifications.h"
#include "windows.applicationmodel.core.h"
#include "windows.applicationmodel.datatransfer.h"
#include "windows.storage.h"
#include "windows.storage.streams.h"
#include "robuffer.h"

#include "wine/debug.h"

extern IActivationFactory *application_view_factory;
extern IActivationFactory *client_device_information_factory;
extern IActivationFactory *analytics_info_factory;
extern IActivationFactory *advertising_manager_factory;
extern IActivationFactory *power_manager_statics_factory;
extern IActivationFactory *core_application_factory;
extern IActivationFactory *app_instance_factory;
extern IActivationFactory *data_transfer_manager_statics_factory;
extern IActivationFactory *clipboard_statics_factory;
extern IActivationFactory *data_package_factory;
extern IActivationFactory *html_format_helper_statics_factory;
extern IActivationFactory *standard_data_formats_statics_factory;
extern IActivationFactory *badge_update_manager_statics_factory;
extern IActivationFactory *toast_notification_manager_statics_factory;

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

extern HRESULT static_lifetime_map_create( __FIMap_2_HSTRING_IInspectable **out );
