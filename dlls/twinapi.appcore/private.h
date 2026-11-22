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
#include "ntstatus.h"

#define COBJMACROS
#include "winstring.h"
#include "appnotify.h"
#include "activation.h"
#include "roapi.h"
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
#define WIDL_using_Windows_Data_Xml_Dom

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
#include "windows.data.xml.dom.h"
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
extern IActivationFactory *toast_notification_factory;
void *get_dummy_async_bool( void );

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

/* Shared toast/badge payload helpers, implemented in toast_notification_manager.c
 * on top of the FreeDesktop Notifications unix bridge. */
struct toast_payload
{
    HSTRING summary;   /* first <text> node (title), empty string when absent */
    HSTRING body;      /* remaining <text> nodes joined, empty string when absent */
    HSTRING launch;    /* toast/@launch activation arguments, empty string when absent */
    HSTRING image;     /* first image/@src, empty string when absent */
};

void toast_payload_free( struct toast_payload *payload );
HRESULT toast_create_xml_document( const WCHAR *xml, __x_ABI_CWindows_CData_CXml_CDom_CIXmlDocument **out );
HRESULT toast_xml_first_attribute( __x_ABI_CWindows_CData_CXml_CDom_CIXmlDocument *doc,
        const WCHAR *tag, const WCHAR *name, HSTRING *value );
HRESULT toast_extract_payload( __x_ABI_CWindows_CData_CXml_CDom_CIXmlDocument *doc,
        struct toast_payload *payload );

extern IActivationFactory *app_notification_manager_statics_factory;
extern IActivationFactory *app_notification_factory;
extern IActivationFactory *app_notification_activated_args_factory;

HRESULT core_app_add_leaving_background(
        __FIEventHandler_1_Windows__CApplicationModel__CLeavingBackgroundEventArgs *handler,
        EventRegistrationToken *token );
HRESULT core_app_remove_leaving_background( EventRegistrationToken token );
HRESULT core_app_add_entered_background(
        __FIEventHandler_1_Windows__CApplicationModel__CEnteredBackgroundEventArgs *handler,
        EventRegistrationToken *token );
HRESULT core_app_remove_entered_background( EventRegistrationToken token );
HRESULT core_app_add_resuming( __FIEventHandler_1_IInspectable *handler,
                               EventRegistrationToken *token );
HRESULT core_app_remove_resuming( EventRegistrationToken token );
void ensure_focus_watcher_started( void );
void core_app_flush_properties( void );
BOOL core_app_try_single_instance_redirect( void );
const WCHAR *app_activation_current_uri( void );
void *activated_event_args_create_for_uri( const WCHAR *uri );
void core_app_fire_view_activated( const WCHAR *uri );

/* Wine extension interfaces on the ToastNotification face, answered by
 * toast_notification_manager.c (NotificationSuppressed event sink registry,
 * ToastContent/ToastTemplate readback of the parsed payload). */
DEFINE_GUID( IID_IToastNotificationSuppressed, 0x4b3e2a91, 0x77d5, 0x4f1a, 0x9d,0x8e, 0x6c,0x3b, 0x5a,0x2f, 0x18,0xc4 );
DEFINE_GUID( IID_IToastNotificationTemplate,  0x0d2f6b83, 0x62e1, 0x4c9f, 0xb0,0xa7, 0x54,0x1e, 0x9d,0xc6, 0x38,0x72 );

/* Wine extension interface on CoreApplication / CoreApplication.Properties
 * for flushing and reloading session state in tests. */
DEFINE_GUID( IID_IWineSessionProperties, 0x7c4e9b21, 0x3a14, 0x4d88, 0x91,0x52, 0xe4,0x7a, 0x2b,0x91, 0x6c,0x03 );

typedef struct IWineSessionProperties IWineSessionProperties;
typedef struct IWineSessionPropertiesVtbl
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)( IWineSessionProperties *, REFIID, void ** );
    ULONG   (STDMETHODCALLTYPE *AddRef)( IWineSessionProperties * );
    ULONG   (STDMETHODCALLTYPE *Release)( IWineSessionProperties * );
    HRESULT (STDMETHODCALLTYPE *Flush)( IWineSessionProperties * );
    HRESULT (STDMETHODCALLTYPE *Reload)( IWineSessionProperties * );
} IWineSessionPropertiesVtbl;
struct IWineSessionProperties { const IWineSessionPropertiesVtbl *lpVtbl; };
