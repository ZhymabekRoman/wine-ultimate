/* WinRT Microsoft.Windows.AppNotifications (Windows App SDK) implementation
 *
 * The WinUI 3 notification path activates
 * Microsoft.Windows.AppNotifications.AppNotificationManager; combase routes
 * the namespace to twinapi.appcore.dll.  The manager shows AppNotification
 * payloads through the FreeDesktop Notifications bridge (unixlib.c), keeping
 * the tag/group/priority state the app sets, and replays the daemon's
 * ActionInvoked signal onto the NotificationInvoked handlers with the launch
 * arguments carried in the payload's toast/@launch attribute.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include "private.h"

#include "wine/debug.h"
#include "wine/list.h"

#include "unixlib.h"

WINE_DEFAULT_DEBUG_CHANNEL(twinapi);

#define WIDL_using_Microsoft_Windows_AppNotifications
#include "appnotifications.h"

/* This widl emits DEFINE_GUID declarations only; the tree convention is to
 * define the IIDs of a module's own WinRT interfaces locally. */
const IID IID___x_ABI_CMicrosoft_CWindows_CAppNotifications_CIAppNotificationActivatedEventArgs =
        {0x8f12add2, 0xdf58, 0xab70, {0x6a, 0x84, 0x1c, 0x4d, 0x61, 0x94, 0xef, 0xf8}};
const IID IID___x_ABI_CMicrosoft_CWindows_CAppNotifications_CIAppNotificationActivatedEventArgs2 =
        {0x804824ff, 0xe589, 0x6349, {0x25, 0x9c, 0x78, 0x6e, 0x02, 0x34, 0xca, 0xab}};
const IID IID___x_ABI_CMicrosoft_CWindows_CAppNotifications_CIAppNotificationFactory =
        {0x4e29a725, 0xa269, 0x5b43, {0xef, 0x9c, 0x3b, 0xa7, 0x60, 0xbe, 0x24, 0xd0}};
const IID IID___x_ABI_CMicrosoft_CWindows_CAppNotifications_CIAppNotification =
        {0xec5e57bd, 0x8bac, 0x3acb, {0x8f, 0x41, 0x1b, 0x21, 0x3f, 0xbb, 0xa9, 0xa7}};
const IID IID___x_ABI_CMicrosoft_CWindows_CAppNotifications_CIAppNotificationManager =
        {0x8491c06f, 0xb880, 0xc211, {0x7b, 0xfc, 0x40, 0x34, 0x85, 0x73, 0x06, 0x5a}};
const IID IID___x_ABI_CMicrosoft_CWindows_CAppNotifications_CIAppNotificationManagerStatics =
        {0xf90c357c, 0x1266, 0x0d53, {0xe0, 0x3d, 0xf1, 0x1c, 0x72, 0x03, 0xa4, 0xd4}};
const IID IID___x_ABI_CMicrosoft_CWindows_CAppNotifications_CIAppNotificationManagerStatics2 =
        {0xfca74de0, 0xebf0, 0x71a8, {0x92, 0xda, 0xf4, 0x4a, 0xea, 0xc3, 0x18, 0xcd}};

/* IInspectable forwarder for secondary interfaces (shared with
 * toast_notification_manager.c, duplicated for locality). */
#define FORWARD_IINSPECTABLE( pfx, iface_type, impl_type, impl_from, base_expr )                     \
    static HRESULT WINAPI pfx##_QueryInterface( iface_type *iface, REFIID iid, void **out )          \
    {                                                                                                \
        impl_type *impl = impl_from( iface );                                                        \
        return IInspectable_QueryInterface( (IInspectable *)(base_expr), iid, out );                 \
    }                                                                                                \
    static ULONG WINAPI pfx##_AddRef( iface_type *iface )                                            \
    {                                                                                                \
        impl_type *impl = impl_from( iface );                                                        \
        return IInspectable_AddRef( (IInspectable *)(base_expr) );                                   \
    }                                                                                                \
    static ULONG WINAPI pfx##_Release( iface_type *iface )                                           \
    {                                                                                                \
        impl_type *impl = impl_from( iface );                                                        \
        return IInspectable_Release( (IInspectable *)(base_expr) );                                  \
    }                                                                                                \
    static HRESULT WINAPI pfx##_GetIids( iface_type *iface, ULONG *iid_count, IID **iids )           \
    {                                                                                                \
        impl_type *impl = impl_from( iface );                                                        \
        return IInspectable_GetIids( (IInspectable *)(base_expr), iid_count, iids );                  \
    }                                                                                                \
    static HRESULT WINAPI pfx##_GetRuntimeClassName( iface_type *iface, HSTRING *class_name )        \
    {                                                                                                \
        impl_type *impl = impl_from( iface );                                                        \
        return IInspectable_GetRuntimeClassName( (IInspectable *)(base_expr), class_name );           \
    }                                                                                                \
    static HRESULT WINAPI pfx##_GetTrustLevel( iface_type *iface, TrustLevel *trust_level )          \
    {                                                                                                \
        impl_type *impl = impl_from( iface );                                                        \
        return IInspectable_GetTrustLevel( (IInspectable *)(base_expr), trust_level );                \
    }

/*
 * Empty IMapView stand-ins for AppNotificationActivatedEventArgs::UserInput
 * and GetArguments(): the bridge collects no user input, so both answer an
 * empty (keyed) map.
 */
struct empty_mapview
{
    __FIMapView_2_HSTRING_IInspectable __FIMapView_2_HSTRING_IInspectable_iface;
    __FIMapView_2_HSTRING_HSTRING __FIMapView_2_HSTRING_HSTRING_iface;
    LONG ref;
};

static inline struct empty_mapview *impl_from_FIMapView_1_HSTRING_IInspectable(
        __FIMapView_2_HSTRING_IInspectable *iface )
{
    return CONTAINING_RECORD( iface, struct empty_mapview, __FIMapView_2_HSTRING_IInspectable_iface );
}

static inline struct empty_mapview *impl_from_FIMapView_1_HSTRING_HSTRING(
        __FIMapView_2_HSTRING_HSTRING *iface )
{
    return CONTAINING_RECORD( iface, struct empty_mapview, __FIMapView_2_HSTRING_HSTRING_iface );
}

#define DEFINE_EMPTY_MAPVIEW( pfx, iface_type, iid_type, impl_from, tvalue )                          \
    static HRESULT WINAPI pfx##_QueryInterface( iface_type *iface, REFIID iid, void **out )            \
    {                                                                                                  \
        TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );                      \
                                                                                                       \
        if (IsEqualGUID( iid, &IID_IUnknown )                                                          \
                || IsEqualGUID( iid, &IID_IInspectable )                                               \
                || IsEqualGUID( iid, &IID_IAgileObject )                                               \
                || IsEqualGUID( iid, &iid_type ))                                                      \
        {                                                                                              \
            IUnknown_AddRef( (IUnknown *)iface );                                                      \
            *out = iface;                                                                              \
            return S_OK;                                                                               \
        }                                                                                              \
                                                                                                       \
        *out = NULL;                                                                                   \
        return E_NOINTERFACE;                                                                          \
    }                                                                                                  \
    static ULONG WINAPI pfx##_AddRef( iface_type *iface )                                              \
    {                                                                                                  \
        struct empty_mapview *impl = impl_from( iface );                                               \
        return InterlockedIncrement( &impl->ref );                                                     \
    }                                                                                                  \
    static ULONG WINAPI pfx##_Release( iface_type *iface )                                             \
    {                                                                                                  \
        struct empty_mapview *impl = impl_from( iface );                                               \
        return InterlockedDecrement( &impl->ref );                                                     \
    }                                                                                                  \
    static HRESULT WINAPI pfx##_GetIids( iface_type *iface, ULONG *iid_count, IID **iids )             \
    {                                                                                                  \
        if (!iid_count || !iids) return E_POINTER;                                                     \
        *iid_count = 0;                                                                                \
        *iids = NULL;                                                                                  \
        return S_OK;                                                                                   \
    }                                                                                                  \
    static HRESULT WINAPI pfx##_GetRuntimeClassName( iface_type *iface, HSTRING *class_name )          \
    {                                                                                                  \
        static const WCHAR emptyW[] = L"";                                                             \
        return WindowsCreateString( emptyW, 0, class_name );                                           \
    }                                                                                                  \
    static HRESULT WINAPI pfx##_GetTrustLevel( iface_type *iface, TrustLevel *trust_level )            \
    {                                                                                                  \
        if (trust_level) *trust_level = BaseTrust;                                                     \
        return S_OK;                                                                                   \
    }                                                                                                  \
    static HRESULT WINAPI pfx##_Lookup( iface_type *iface, HSTRING key, tvalue *value )                \
    {                                                                                                  \
        if (!value) return E_POINTER;                                                                  \
        *value = NULL;                                                                                 \
        return S_OK;                                                                                   \
    }                                                                                                  \
    static HRESULT WINAPI pfx##_GetSize( iface_type *iface, UINT32 *size )                             \
    {                                                                                                  \
        if (!size) return E_POINTER;                                                                   \
        *size = 0;                                                                                     \
        return S_OK;                                                                                   \
    }                                                                                                  \
    static HRESULT WINAPI pfx##_HasKey( iface_type *iface, HSTRING key, boolean *has_key )             \
    {                                                                                                  \
        if (!has_key) return E_POINTER;                                                                \
        *has_key = FALSE;                                                                              \
        return S_OK;                                                                                   \
    }                                                                                                  \
    static HRESULT WINAPI pfx##_Split( iface_type *iface, iface_type **first, iface_type **second )    \
    {                                                                                                  \
        if (first) { *first = NULL; iface_type##_AddRef( iface ); }                                      \
        if (second) *second = NULL;                                                                    \
        return S_OK;                                                                                   \
    }                                                                                                  \
    static const struct iface_type##Vtbl pfx##_vtbl =                                                  \
    {                                                                                                  \
        pfx##_QueryInterface,                                                                          \
        pfx##_AddRef,                                                                                  \
        pfx##_Release,                                                                                 \
        pfx##_GetIids,                                                                                 \
        pfx##_GetRuntimeClassName,                                                                     \
        pfx##_GetTrustLevel,                                                                           \
        pfx##_Lookup,                                                                                  \
        pfx##_GetSize,                                                                                 \
        pfx##_HasKey,                                                                                  \
        pfx##_Split,                                                                                   \
    }

DEFINE_EMPTY_MAPVIEW( mapview_inspectable, __FIMapView_2_HSTRING_IInspectable,
        IID___FIMapView_2_HSTRING_IInspectable, impl_from_FIMapView_1_HSTRING_IInspectable, IInspectable * );
DEFINE_EMPTY_MAPVIEW( mapview_hstring, __FIMapView_2_HSTRING_HSTRING,
        IID___FIMapView_2_HSTRING_HSTRING, impl_from_FIMapView_1_HSTRING_HSTRING, HSTRING );

static struct empty_mapview empty_mapview_obj =
{
    { &mapview_inspectable_vtbl },
    { &mapview_hstring_vtbl },
    1
};

/*
 * Microsoft.Windows.AppNotifications.AppNotificationActivatedEventArgs
 */
struct app_notification_activated_args
{
    IAppNotificationActivatedEventArgs IAppNotificationActivatedEventArgs_iface;
    IAppNotificationActivatedEventArgs2 IAppNotificationActivatedEventArgs2_iface;
    HSTRING argument;
    LONG ref;
};

static inline struct app_notification_activated_args *impl_from_IAppNotificationActivatedEventArgs(
        IAppNotificationActivatedEventArgs *iface )
{
    return CONTAINING_RECORD( iface, struct app_notification_activated_args,
                              IAppNotificationActivatedEventArgs_iface );
}

static inline struct app_notification_activated_args *impl_from_IAppNotificationActivatedEventArgs2(
        IAppNotificationActivatedEventArgs2 *iface )
{
    return CONTAINING_RECORD( iface, struct app_notification_activated_args,
                              IAppNotificationActivatedEventArgs2_iface );
}

static HRESULT WINAPI app_notification_activated_args_QueryInterface(
        IAppNotificationActivatedEventArgs *iface, REFIID iid, void **out )
{
    struct app_notification_activated_args *impl = impl_from_IAppNotificationActivatedEventArgs( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown )
            || IsEqualGUID( iid, &IID_IInspectable )
            || IsEqualGUID( iid, &IID_IAgileObject )
            || IsEqualGUID( iid, &IID_IAppNotificationActivatedEventArgs ))
    {
        IUnknown_AddRef( (IUnknown *)iface );
        *out = iface;
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IAppNotificationActivatedEventArgs2 ))
    {
        IUnknown_AddRef( (IUnknown *)iface );
        *out = &impl->IAppNotificationActivatedEventArgs2_iface;
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI app_notification_activated_args_AddRef( IAppNotificationActivatedEventArgs *iface )
{
    struct app_notification_activated_args *impl = impl_from_IAppNotificationActivatedEventArgs( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI app_notification_activated_args_Release( IAppNotificationActivatedEventArgs *iface )
{
    struct app_notification_activated_args *impl = impl_from_IAppNotificationActivatedEventArgs( iface );
    LONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        WindowsDeleteString( impl->argument );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI app_notification_activated_args_GetIids( IAppNotificationActivatedEventArgs *iface,
        ULONG *iid_count, IID **iids )
{
    if (!iid_count || !iids) return E_POINTER;
    *iid_count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI app_notification_activated_args_GetRuntimeClassName(
        IAppNotificationActivatedEventArgs *iface, HSTRING *class_name )
{
    return WindowsCreateString( RuntimeClass_Microsoft_Windows_AppNotifications_AppNotificationActivatedEventArgs,
            ARRAY_SIZE( RuntimeClass_Microsoft_Windows_AppNotifications_AppNotificationActivatedEventArgs ) - 1,
            class_name );
}

static HRESULT WINAPI app_notification_activated_args_GetTrustLevel( IAppNotificationActivatedEventArgs *iface,
        TrustLevel *trust_level )
{
    if (trust_level) *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI app_notification_activated_args_get_Argument(
        IAppNotificationActivatedEventArgs *iface, HSTRING *value )
{
    struct app_notification_activated_args *impl = impl_from_IAppNotificationActivatedEventArgs( iface );

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    return WindowsDuplicateString( impl->argument, value );
}

static HRESULT WINAPI app_notification_activated_args_get_UserInput(
        IAppNotificationActivatedEventArgs *iface, __FIMapView_2_HSTRING_IInspectable **value )
{
    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    IUnknown_AddRef( (IUnknown *)&empty_mapview_obj.__FIMapView_2_HSTRING_IInspectable_iface );
    *value = &empty_mapview_obj.__FIMapView_2_HSTRING_IInspectable_iface;
    return S_OK;
}

static const struct IAppNotificationActivatedEventArgsVtbl app_notification_activated_args_vtbl =
{
    app_notification_activated_args_QueryInterface,
    app_notification_activated_args_AddRef,
    app_notification_activated_args_Release,
    /* IInspectable methods */
    app_notification_activated_args_GetIids,
    app_notification_activated_args_GetRuntimeClassName,
    app_notification_activated_args_GetTrustLevel,
    /* IAppNotificationActivatedEventArgs methods */
    app_notification_activated_args_get_Argument,
    app_notification_activated_args_get_UserInput,
};

FORWARD_IINSPECTABLE( activated_args2, IAppNotificationActivatedEventArgs2, struct app_notification_activated_args,
        impl_from_IAppNotificationActivatedEventArgs2, &impl->IAppNotificationActivatedEventArgs_iface )

static HRESULT WINAPI app_notification_activated_args2_GetArguments(
        IAppNotificationActivatedEventArgs2 *iface, __FIMapView_2_HSTRING_HSTRING **value )
{
    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    IUnknown_AddRef( (IUnknown *)&empty_mapview_obj.__FIMapView_2_HSTRING_HSTRING_iface );
    *value = &empty_mapview_obj.__FIMapView_2_HSTRING_HSTRING_iface;
    return S_OK;
}

static const struct IAppNotificationActivatedEventArgs2Vtbl app_notification_activated_args2_vtbl =
{
    activated_args2_QueryInterface,
    activated_args2_AddRef,
    activated_args2_Release,
    /* IInspectable methods */
    activated_args2_GetIids,
    activated_args2_GetRuntimeClassName,
    activated_args2_GetTrustLevel,
    /* IAppNotificationActivatedEventArgs2 methods */
    app_notification_activated_args2_GetArguments,
};

static HRESULT create_app_notification_activated_args( HSTRING argument,
        struct app_notification_activated_args **out )
{
    struct app_notification_activated_args *impl;
    HRESULT hr;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->IAppNotificationActivatedEventArgs_iface.lpVtbl = &app_notification_activated_args_vtbl;
    impl->IAppNotificationActivatedEventArgs2_iface.lpVtbl = &app_notification_activated_args2_vtbl;
    impl->ref = 1;
    if (argument && FAILED( hr = WindowsDuplicateString( argument, &impl->argument ) ))
    {
        free( impl );
        return hr;
    }
    *out = impl;
    return S_OK;
}

/*
 * Microsoft.Windows.AppNotifications.AppNotification: carries the XML
 * payload string the manager shows through the notification daemon.
 */
struct app_notification
{
    IAppNotification IAppNotification_iface;
    LONG ref;
    HSTRING tag, group, payload;
    HSTRING launch_args;           /* toast/@launch, replayed on activation */
    UINT32 id;                     /* daemon id once shown */
    UINT32 fd_id;
    struct list shown_entry;
    __x_ABI_CMicrosoft_CWindows_CAppNotifications_CAppNotificationPriority priority;
    boolean expires_on_reboot, suppress_display;
    DateTime expiration;
    IInspectable *progress;
};

static inline struct app_notification *impl_from_IAppNotification( IAppNotification *iface )
{
    return CONTAINING_RECORD( iface, struct app_notification, IAppNotification_iface );
}

static HRESULT WINAPI app_notification_QueryInterface( IAppNotification *iface, REFIID iid, void **out )
{
    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown )
            || IsEqualGUID( iid, &IID_IInspectable )
            || IsEqualGUID( iid, &IID_IAgileObject )
            || IsEqualGUID( iid, &IID_IAppNotification ))
    {
        IUnknown_AddRef( (IUnknown *)iface );
        *out = iface;
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI app_notification_AddRef( IAppNotification *iface )
{
    struct app_notification *impl = impl_from_IAppNotification( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI app_notification_Release( IAppNotification *iface )
{
    struct app_notification *impl = impl_from_IAppNotification( iface );
    LONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        TRACE( "destroying %p\n", iface );
        if (impl->progress) IInspectable_Release( impl->progress );
        if (!list_empty( &impl->shown_entry ))
        {
            list_remove( &impl->shown_entry );
            list_init( &impl->shown_entry );
        }
        WindowsDeleteString( impl->tag );
        WindowsDeleteString( impl->group );
        WindowsDeleteString( impl->payload );
        WindowsDeleteString( impl->launch_args );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI app_notification_GetIids( IAppNotification *iface, ULONG *iid_count, IID **iids )
{
    if (!iid_count || !iids) return E_POINTER;
    *iid_count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI app_notification_GetRuntimeClassName( IAppNotification *iface, HSTRING *class_name )
{
    return WindowsCreateString( RuntimeClass_Microsoft_Windows_AppNotifications_AppNotification,
            ARRAY_SIZE( RuntimeClass_Microsoft_Windows_AppNotifications_AppNotification ) - 1, class_name );
}

static HRESULT WINAPI app_notification_GetTrustLevel( IAppNotification *iface, TrustLevel *trust_level )
{
    if (trust_level) *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT app_notification_set_hstring( HSTRING *field, HSTRING value )
{
    HSTRING dup = NULL;
    HRESULT hr;

    if (value && FAILED( hr = WindowsDuplicateString( value, &dup ) )) return hr;
    WindowsDeleteString( *field );
    *field = dup;
    return S_OK;
}

static HRESULT WINAPI app_notification_get_Tag( IAppNotification *iface, HSTRING *value )
{
    struct app_notification *impl = impl_from_IAppNotification( iface );

    if (!value) return E_POINTER;
    return WindowsDuplicateString( impl->tag, value );
}

static HRESULT WINAPI app_notification_put_Tag( IAppNotification *iface, HSTRING value )
{
    struct app_notification *impl = impl_from_IAppNotification( iface );
    return app_notification_set_hstring( &impl->tag, value );
}

static HRESULT WINAPI app_notification_get_Group( IAppNotification *iface, HSTRING *value )
{
    struct app_notification *impl = impl_from_IAppNotification( iface );

    if (!value) return E_POINTER;
    return WindowsDuplicateString( impl->group, value );
}

static HRESULT WINAPI app_notification_put_Group( IAppNotification *iface, HSTRING value )
{
    struct app_notification *impl = impl_from_IAppNotification( iface );
    return app_notification_set_hstring( &impl->group, value );
}

static HRESULT WINAPI app_notification_get_Id( IAppNotification *iface, UINT32 *value )
{
    struct app_notification *impl = impl_from_IAppNotification( iface );

    if (!value) return E_POINTER;
    *value = impl->id;
    return S_OK;
}

static HRESULT WINAPI app_notification_get_Payload( IAppNotification *iface, HSTRING *value )
{
    struct app_notification *impl = impl_from_IAppNotification( iface );

    if (!value) return E_POINTER;
    return WindowsDuplicateString( impl->payload, value );
}

static HRESULT WINAPI app_notification_get_Progress( IAppNotification *iface, IInspectable **value )
{
    struct app_notification *impl = impl_from_IAppNotification( iface );

    if (!value) return E_POINTER;
    if (impl->progress) IInspectable_AddRef( impl->progress );
    *value = impl->progress;
    return S_OK;
}

static HRESULT WINAPI app_notification_put_Progress( IAppNotification *iface, IInspectable *value )
{
    struct app_notification *impl = impl_from_IAppNotification( iface );

    if (value) IInspectable_AddRef( value );
    if (impl->progress) IInspectable_Release( impl->progress );
    impl->progress = value;
    return S_OK;
}

static HRESULT WINAPI app_notification_get_Expiration( IAppNotification *iface, DateTime *value )
{
    struct app_notification *impl = impl_from_IAppNotification( iface );

    if (!value) return E_POINTER;
    *value = impl->expiration;
    return S_OK;
}

static HRESULT WINAPI app_notification_put_Expiration( IAppNotification *iface, DateTime value )
{
    struct app_notification *impl = impl_from_IAppNotification( iface );

    impl->expiration = value;
    return S_OK;
}

static HRESULT WINAPI app_notification_get_ExpiresOnReboot( IAppNotification *iface, boolean *value )
{
    struct app_notification *impl = impl_from_IAppNotification( iface );

    if (!value) return E_POINTER;
    *value = impl->expires_on_reboot;
    return S_OK;
}

static HRESULT WINAPI app_notification_put_ExpiresOnReboot( IAppNotification *iface, boolean value )
{
    struct app_notification *impl = impl_from_IAppNotification( iface );

    impl->expires_on_reboot = value;
    return S_OK;
}

static HRESULT WINAPI app_notification_get_Priority( IAppNotification *iface, __x_ABI_CMicrosoft_CWindows_CAppNotifications_CAppNotificationPriority *value )
{
    struct app_notification *impl = impl_from_IAppNotification( iface );

    if (!value) return E_POINTER;
    *value = impl->priority;
    return S_OK;
}

static HRESULT WINAPI app_notification_put_Priority( IAppNotification *iface, __x_ABI_CMicrosoft_CWindows_CAppNotifications_CAppNotificationPriority value )
{
    struct app_notification *impl = impl_from_IAppNotification( iface );

    impl->priority = value;
    return S_OK;
}

static HRESULT WINAPI app_notification_get_SuppressDisplay( IAppNotification *iface, boolean *value )
{
    struct app_notification *impl = impl_from_IAppNotification( iface );

    if (!value) return E_POINTER;
    *value = impl->suppress_display;
    return S_OK;
}

static HRESULT WINAPI app_notification_put_SuppressDisplay( IAppNotification *iface, boolean value )
{
    struct app_notification *impl = impl_from_IAppNotification( iface );

    impl->suppress_display = value;
    return S_OK;
}

static const struct IAppNotificationVtbl app_notification_vtbl =
{
    app_notification_QueryInterface,
    app_notification_AddRef,
    app_notification_Release,
    /* IInspectable methods */
    app_notification_GetIids,
    app_notification_GetRuntimeClassName,
    app_notification_GetTrustLevel,
    /* IAppNotification methods */
    app_notification_get_Tag,
    app_notification_put_Tag,
    app_notification_get_Group,
    app_notification_put_Group,
    app_notification_get_Id,
    app_notification_get_Payload,
    app_notification_get_Progress,
    app_notification_put_Progress,
    app_notification_get_Expiration,
    app_notification_put_Expiration,
    app_notification_get_ExpiresOnReboot,
    app_notification_put_ExpiresOnReboot,
    app_notification_get_Priority,
    app_notification_put_Priority,
    app_notification_get_SuppressDisplay,
    app_notification_put_SuppressDisplay,
};

static HRESULT create_app_notification( HSTRING payload, struct app_notification **out )
{
    struct app_notification *impl;
    HRESULT hr;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->IAppNotification_iface.lpVtbl = &app_notification_vtbl;
    impl->ref = 1;
    list_init( &impl->shown_entry );
    if (payload && FAILED( hr = WindowsDuplicateString( payload, &impl->payload ) ))
    {
        free( impl );
        return hr;
    }
    *out = impl;
    return S_OK;
}

/*
 * Microsoft.Windows.AppNotifications.AppNotificationManager
 */
struct app_notification_sink
{
    struct list entry;
    EventRegistrationToken token;
    __FITypedEventHandler_2_Microsoft__CWindows__CAppNotifications__CAppNotificationManager_Microsoft__CWindows__CAppNotifications__CAppNotificationActivatedEventArgs *handler;
};

struct app_notification_manager
{
    IAppNotificationManager IAppNotificationManager_iface;
    LONG ref;
    struct list sinks;
};

static inline struct app_notification_manager *impl_from_IAppNotificationManager(
        IAppNotificationManager *iface )
{
    return CONTAINING_RECORD( iface, struct app_notification_manager, IAppNotificationManager_iface );
}

/* AppNotifications currently visible in the notification daemon. */
static struct list shown_app_notifications = LIST_INIT( shown_app_notifications );
static LONG app_notification_token;

static HRESULT WINAPI app_notification_manager_QueryInterface( IAppNotificationManager *iface,
        REFIID iid, void **out )
{
    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown )
            || IsEqualGUID( iid, &IID_IInspectable )
            || IsEqualGUID( iid, &IID_IAgileObject )
            || IsEqualGUID( iid, &IID_IAppNotificationManager ))
    {
        IUnknown_AddRef( (IUnknown *)iface );
        *out = iface;
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI app_notification_manager_AddRef( IAppNotificationManager *iface )
{
    struct app_notification_manager *impl = impl_from_IAppNotificationManager( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI app_notification_manager_Release( IAppNotificationManager *iface )
{
    struct app_notification_manager *impl = impl_from_IAppNotificationManager( iface );
    LONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        struct app_notification_sink *sink, *next;

        TRACE( "destroying %p\n", iface );
        LIST_FOR_EACH_ENTRY_SAFE( sink, next, &impl->sinks, struct app_notification_sink, entry )
        {
            list_remove( &sink->entry );
            IUnknown_Release( (IUnknown *)sink->handler );
            free( sink );
        }
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI app_notification_manager_GetIids( IAppNotificationManager *iface,
        ULONG *iid_count, IID **iids )
{
    if (!iid_count || !iids) return E_POINTER;
    *iid_count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI app_notification_manager_GetRuntimeClassName( IAppNotificationManager *iface,
        HSTRING *class_name )
{
    return WindowsCreateString( RuntimeClass_Microsoft_Windows_AppNotifications_AppNotificationManager,
            ARRAY_SIZE( RuntimeClass_Microsoft_Windows_AppNotifications_AppNotificationManager ) - 1,
            class_name );
}

static HRESULT WINAPI app_notification_manager_GetTrustLevel( IAppNotificationManager *iface,
        TrustLevel *trust_level )
{
    if (trust_level) *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI app_notification_manager_Register( IAppNotificationManager *iface )
{
    TRACE( "iface %p.\n", iface );
    return S_OK;
}

static HRESULT WINAPI app_notification_manager_Unregister( IAppNotificationManager *iface )
{
    TRACE( "iface %p.\n", iface );
    return S_OK;
}

static HRESULT WINAPI app_notification_manager_UnregisterAll( IAppNotificationManager *iface )
{
    TRACE( "iface %p.\n", iface );
    return S_OK;
}

static HRESULT WINAPI app_notification_manager_add_NotificationInvoked( IAppNotificationManager *iface,
        __FITypedEventHandler_2_Microsoft__CWindows__CAppNotifications__CAppNotificationManager_Microsoft__CWindows__CAppNotifications__CAppNotificationActivatedEventArgs *handler,
        EventRegistrationToken *token )
{
    struct app_notification_manager *impl = impl_from_IAppNotificationManager( iface );
    struct app_notification_sink *sink;

    TRACE( "iface %p, handler %p, token %p.\n", iface, handler, token );

    if (!handler || !token) return E_POINTER;
    if (!(sink = malloc( sizeof(*sink) ))) return E_OUTOFMEMORY;

    sink->token.value = (ULONGLONG)InterlockedIncrement( &app_notification_token );
    sink->handler = handler;
    IUnknown_AddRef( (IUnknown *)handler );
    list_add_tail( &impl->sinks, &sink->entry );
    *token = sink->token;
    return S_OK;
}

static HRESULT WINAPI app_notification_manager_remove_NotificationInvoked( IAppNotificationManager *iface,
        EventRegistrationToken token )
{
    struct app_notification_manager *impl = impl_from_IAppNotificationManager( iface );
    struct app_notification_sink *sink, *next;

    TRACE( "iface %p, token %s.\n", iface, wine_dbgstr_longlong( token.value ) );

    LIST_FOR_EACH_ENTRY_SAFE( sink, next, &impl->sinks, struct app_notification_sink, entry )
    {
        if (sink->token.value != token.value) continue;
        list_remove( &sink->entry );
        IUnknown_Release( (IUnknown *)sink->handler );
        free( sink );
        break;
    }
    return S_OK;
}

static HRESULT show_app_notification( struct app_notification_manager *impl, __x_ABI_CMicrosoft_CWindows_CAppNotifications_CIAppNotification *notification )
{
    __x_ABI_CWindows_CData_CXml_CDom_CIXmlDocument *doc = NULL;
    IAppNotification *notif;
    struct toast_payload payload = {0};
    struct fd_notify_params params;
    HSTRING xml = NULL, class_name = NULL;
    struct app_notification *obj;
    IInspectable *instance;
    HRESULT hr;

    if (!notification) return E_POINTER;
    if (FAILED( hr = IInspectable_QueryInterface( (IInspectable *)notification, &IID_IAppNotification,
            (void **)&notif ) ))
        return hr;

    hr = IAppNotification_get_Payload( notif, &xml );
    IAppNotification_Release( notif );
    if (FAILED( hr )) return hr;

    /* The payload is a toast XML string; parse and extract it like a
     * ToastNotification content document. */
    if (SUCCEEDED( hr = WindowsCreateString( RuntimeClass_Windows_Data_Xml_Dom_XmlDocument,
            ARRAY_SIZE( RuntimeClass_Windows_Data_Xml_Dom_XmlDocument ) - 1, &class_name ) ))
    {
        hr = RoActivateInstance( class_name, &instance );
        WindowsDeleteString( class_name );
        if (SUCCEEDED( hr ))
        {
            hr = IInspectable_QueryInterface( instance, &IID_IXmlDocument, (void **)&doc );
            IInspectable_Release( instance );
        }
    }
    if (SUCCEEDED( hr ) && doc)
    {
        __x_ABI_CWindows_CData_CXml_CDom_CIXmlDocumentIO *io;

        hr = IInspectable_QueryInterface( (IInspectable *)doc, &IID_IXmlDocumentIO, (void **)&io );
        if (SUCCEEDED( hr ))
        {
            hr = IXmlDocumentIO_LoadXml( io, xml );
            IInspectable_Release( (IInspectable *)io );
        }
    }
    WindowsDeleteString( xml );
    if (SUCCEEDED( hr ) && doc)
    {
        hr = toast_extract_payload( doc, &payload );
        IInspectable_Release( (IInspectable *)doc );
        if (FAILED( hr ))
        {
            toast_payload_free( &payload );
            return hr;
        }
    }

    obj = impl_from_IAppNotification( notif );
    params.app_name = L"Wine Application";
    params.icon = payload.image ? WindowsGetStringRawBuffer( payload.image, NULL ) : NULL;
    params.summary = payload.summary ? WindowsGetStringRawBuffer( payload.summary, NULL ) : NULL;
    params.body = payload.body ? WindowsGetStringRawBuffer( payload.body, NULL ) : NULL;
    params.badge_count = -1;
    params.replaces_id = obj->fd_id;
    if (TWINAPI_CALL( fd_notify, &params ) == STATUS_SUCCESS && params.id)
    {
        obj->fd_id = params.id;
        obj->id = params.id;
        WindowsDeleteString( obj->launch_args );
        obj->launch_args = payload.launch;
        payload.launch = NULL;
        if (list_empty( &obj->shown_entry ))
        {
            IInspectable_AddRef( (IInspectable *)notification );
            list_add_tail( &shown_app_notifications, &obj->shown_entry );
        }
        TRACE( "delivered AppNotification as notification id %u\n", params.id );
    }
    toast_payload_free( &payload );
    return S_OK;
}

static void app_notification_manager_fire( struct app_notification_manager *impl, HSTRING argument )
{
    __FITypedEventHandler_2_Microsoft__CWindows__CAppNotifications__CAppNotificationManager_Microsoft__CWindows__CAppNotifications__CAppNotificationActivatedEventArgs *handler;
    struct app_notification_activated_args *args;
    struct app_notification_sink *sink;

    if (FAILED( create_app_notification_activated_args( argument, &args ) )) return;

    LIST_FOR_EACH_ENTRY( sink, &impl->sinks, struct app_notification_sink, entry )
    {
        handler = sink->handler;
        handler->lpVtbl->Invoke( handler, &impl->IAppNotificationManager_iface,
                                 &args->IAppNotificationActivatedEventArgs_iface );
    }
    IUnknown_Release( (IUnknown *)&args->IAppNotificationActivatedEventArgs_iface );
}

/* Drain queued daemon events onto the NotificationInvoked handlers of the
 * manager shown the matching id (click → argument = payload launch). */
static void app_notification_manager_drain( struct app_notification_manager *impl )
{
    struct fd_event_params params;
    struct app_notification *obj;

    while (TWINAPI_CALL( fd_event, &params ) == STATUS_SUCCESS && params.got_event)
    {
        if (!params.got_event || params.reason != 2) continue;
        LIST_FOR_EACH_ENTRY( obj, &shown_app_notifications, struct app_notification, shown_entry )
        {
            if (obj->fd_id != params.id) continue;
            app_notification_manager_fire( impl, obj->launch_args );
            break;
        }
    }
}

static HRESULT WINAPI app_notification_manager_Show( IAppNotificationManager *iface,
        __x_ABI_CMicrosoft_CWindows_CAppNotifications_CIAppNotification *notification )
{
    struct app_notification_manager *impl = impl_from_IAppNotificationManager( iface );

    TRACE( "iface %p, notification %p.\n", iface, notification );

    app_notification_manager_drain( impl );
    return show_app_notification( impl, notification );
}

static HRESULT WINAPI app_notification_manager_ShowWithId( IAppNotificationManager *iface,
        UINT32 id, __x_ABI_CMicrosoft_CWindows_CAppNotifications_CIAppNotification *notification )
{
    struct app_notification_manager *impl = impl_from_IAppNotificationManager( iface );

    TRACE( "iface %p, id %u, notification %p.\n", iface, id, notification );

    app_notification_manager_drain( impl );
    return show_app_notification( impl, notification );
}

static HRESULT WINAPI app_notification_manager_RemoveByIdAsync( IAppNotificationManager *iface,
        UINT32 notification_id, IAsyncAction **value )
{
    struct fd_close_params close_params = { notification_id };

    TRACE( "iface %p, notification_id %u, value %p.\n", iface, notification_id, value );

    if (close_params.id) TWINAPI_CALL( fd_close, &close_params );
    if (value) *value = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI app_notification_manager_get_Setting( IAppNotificationManager *iface,
        __x_ABI_CMicrosoft_CWindows_CAppNotifications_CAppNotificationSetting *value )
{
    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    *value = AppNotificationSetting_AppNotificationSetting_Enabled;
    return S_OK;
}

static HRESULT WINAPI app_notification_manager_RemoveByTagAsync( IAppNotificationManager *iface,
        HSTRING tag, IAsyncAction **value )
{
    TRACE( "iface %p, tag %s, value %p.\n", iface, debugstr_hstring( tag ), value );

    if (value) *value = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI app_notification_manager_RemoveByTagAndGroupAsync( IAppNotificationManager *iface,
        HSTRING tag, HSTRING group, IAsyncAction **value )
{
    TRACE( "iface %p, tag %s, group %s, value %p.\n", iface, debugstr_hstring( tag ),
            debugstr_hstring( group ), value );

    if (value) *value = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI app_notification_manager_RemoveAllAsync( IAppNotificationManager *iface,
        IAsyncAction **value )
{
    TRACE( "iface %p, value %p.\n", iface, value );

    if (value) *value = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI app_notification_manager_UpdateAsync( IAppNotificationManager *iface,
        __x_ABI_CMicrosoft_CWindows_CAppNotifications_CIAppNotificationProgressData *progress_data, HSTRING tag, HSTRING group,
        __FIAsyncOperation_1_AppNotificationProgressResult **value )
{
    TRACE( "iface %p, progress_data %p, tag %s, group %s, value %p.\n", iface, progress_data,
            debugstr_hstring( tag ), debugstr_hstring( group ), value );

    if (value) *value = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI app_notification_manager_Register2( IAppNotificationManager *iface,
        HSTRING display_name, IUriRuntimeClass *icon_uri )
{
    TRACE( "iface %p, display_name %s, icon_uri %p.\n", iface, debugstr_hstring( display_name ), icon_uri );
    return S_OK;
}

static HRESULT WINAPI app_notification_manager_Unregister2( IAppNotificationManager *iface, HSTRING tag )
{
    TRACE( "iface %p, tag %s.\n", iface, debugstr_hstring( tag ) );
    return S_OK;
}

static const struct IAppNotificationManagerVtbl app_notification_manager_vtbl =
{
    app_notification_manager_QueryInterface,
    app_notification_manager_AddRef,
    app_notification_manager_Release,
    /* IInspectable methods */
    app_notification_manager_GetIids,
    app_notification_manager_GetRuntimeClassName,
    app_notification_manager_GetTrustLevel,
    /* IAppNotificationManager methods */
    app_notification_manager_Register,
    app_notification_manager_Unregister,
    app_notification_manager_UnregisterAll,
    app_notification_manager_add_NotificationInvoked,
    app_notification_manager_remove_NotificationInvoked,
    app_notification_manager_Show,
    app_notification_manager_ShowWithId,
    app_notification_manager_RemoveByIdAsync,
    app_notification_manager_get_Setting,
    app_notification_manager_RemoveByTagAsync,
    app_notification_manager_RemoveByTagAndGroupAsync,
    app_notification_manager_RemoveAllAsync,
    app_notification_manager_UpdateAsync,
    app_notification_manager_Register2,
    app_notification_manager_Unregister2,
};

/* The Default manager: statics get_Default hands out this singleton. */
static struct app_notification_manager app_notification_manager =
{
    { &app_notification_manager_vtbl },
    1,
    LIST_INIT( app_notification_manager.sinks )
};

static HRESULT create_app_notification_manager( struct app_notification_manager **out )
{
    struct app_notification_manager *impl;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->IAppNotificationManager_iface.lpVtbl = &app_notification_manager_vtbl;
    impl->ref = 1;
    list_init( &impl->sinks );
    *out = impl;
    return S_OK;
}

/*
 * Class factory for AppNotificationManager: answers
 * IAppNotificationManagerStatics (get_Default) and
 * IAppNotificationManagerStatics2 (IsSupported); parameterless activation
 * creates a new manager instance.
 */
struct app_notification_manager_factory
{
    IActivationFactory IActivationFactory_iface;
    IAppNotificationManagerStatics IAppNotificationManagerStatics_iface;
    IAppNotificationManagerStatics2 IAppNotificationManagerStatics2_iface;
    LONG ref;
};

static inline struct app_notification_manager_factory *impl_from_IActivationFactory(
        IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct app_notification_manager_factory, IActivationFactory_iface );
}

static HRESULT WINAPI manager_factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct app_notification_manager_factory *impl = impl_from_IActivationFactory( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown )
            || IsEqualGUID( iid, &IID_IInspectable )
            || IsEqualGUID( iid, &IID_IAgileObject )
            || IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        IUnknown_AddRef( (IUnknown *)iface );
        *out = iface;
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IAppNotificationManagerStatics ))
    {
        IUnknown_AddRef( (IUnknown *)iface );
        *out = &impl->IAppNotificationManagerStatics_iface;
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IAppNotificationManagerStatics2 ))
    {
        IUnknown_AddRef( (IUnknown *)iface );
        *out = &impl->IAppNotificationManagerStatics2_iface;
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI manager_factory_AddRef( IActivationFactory *iface )
{
    struct app_notification_manager_factory *impl = impl_from_IActivationFactory( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI manager_factory_Release( IActivationFactory *iface )
{
    struct app_notification_manager_factory *impl = impl_from_IActivationFactory( iface );
    return InterlockedDecrement( &impl->ref );
}

static HRESULT WINAPI manager_factory_GetIids( IActivationFactory *iface, ULONG *iid_count, IID **iids )
{
    if (!iid_count || !iids) return E_POINTER;
    *iid_count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI manager_factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *class_name )
{
    return WindowsCreateString( RuntimeClass_Microsoft_Windows_AppNotifications_AppNotificationManager,
            ARRAY_SIZE( RuntimeClass_Microsoft_Windows_AppNotifications_AppNotificationManager ) - 1,
            class_name );
}

static HRESULT WINAPI manager_factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *trust_level )
{
    if (trust_level) *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI manager_factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    struct app_notification_manager *impl;
    HRESULT hr;

    TRACE( "iface %p, instance %p.\n", iface, instance );

    if (!instance) return E_POINTER;
    if (FAILED( hr = create_app_notification_manager( &impl ) )) return hr;
    *instance = (IInspectable *)&impl->IAppNotificationManager_iface;
    return S_OK;
}

static const struct IActivationFactoryVtbl manager_factory_vtbl =
{
    manager_factory_QueryInterface,
    manager_factory_AddRef,
    manager_factory_Release,
    /* IInspectable methods */
    manager_factory_GetIids,
    manager_factory_GetRuntimeClassName,
    manager_factory_GetTrustLevel,
    /* IActivationFactory methods */
    manager_factory_ActivateInstance,
};

DEFINE_IINSPECTABLE( manager_statics, IAppNotificationManagerStatics, struct app_notification_manager_factory,
        IActivationFactory_iface )
DEFINE_IINSPECTABLE( manager_statics2, IAppNotificationManagerStatics2, struct app_notification_manager_factory,
        IActivationFactory_iface )

static HRESULT WINAPI manager_statics_get_Default( IAppNotificationManagerStatics *iface,
        __x_ABI_CMicrosoft_CWindows_CAppNotifications_CIAppNotificationManager **value )
{
    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    IUnknown_AddRef( (IUnknown *)&app_notification_manager.IAppNotificationManager_iface );
    *value = (__x_ABI_CMicrosoft_CWindows_CAppNotifications_CIAppNotificationManager *)&app_notification_manager.IAppNotificationManager_iface;
    return S_OK;
}

static const struct IAppNotificationManagerStaticsVtbl manager_statics_vtbl =
{
    manager_statics_QueryInterface,
    manager_statics_AddRef,
    manager_statics_Release,
    /* IInspectable methods */
    manager_statics_GetIids,
    manager_statics_GetRuntimeClassName,
    manager_statics_GetTrustLevel,
    /* IAppNotificationManagerStatics methods */
    manager_statics_get_Default,
};

static HRESULT WINAPI manager_statics2_IsSupported( IAppNotificationManagerStatics2 *iface, boolean *value )
{
    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    *value = TRUE;
    return S_OK;
}

static const struct IAppNotificationManagerStatics2Vtbl manager_statics2_vtbl =
{
    manager_statics2_QueryInterface,
    manager_statics2_AddRef,
    manager_statics2_Release,
    /* IInspectable methods */
    manager_statics2_GetIids,
    manager_statics2_GetRuntimeClassName,
    manager_statics2_GetTrustLevel,
    /* IAppNotificationManagerStatics2 methods */
    manager_statics2_IsSupported,
};

static struct app_notification_manager_factory manager_factory_obj =
{
    { &manager_factory_vtbl },
    { &manager_statics_vtbl },
    { &manager_statics2_vtbl },
    1
};

/*
 * Class factory for AppNotification: answers IAppNotificationFactory
 * (CreateInstance with the payload string); parameterless activation creates
 * an AppNotification with an empty payload.
 */
struct app_notification_factory
{
    IActivationFactory IActivationFactory_iface;
    IAppNotificationFactory IAppNotificationFactory_iface;
    LONG ref;
};

static inline struct app_notification_factory *impl_from_app_notification_factory(
        IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct app_notification_factory, IActivationFactory_iface );
}

static inline struct app_notification_factory *impl_from_IAppNotificationFactory(
        IAppNotificationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct app_notification_factory, IAppNotificationFactory_iface );
}

static HRESULT WINAPI app_notification_factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct app_notification_factory *impl = impl_from_app_notification_factory( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown )
            || IsEqualGUID( iid, &IID_IInspectable )
            || IsEqualGUID( iid, &IID_IAgileObject )
            || IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        IUnknown_AddRef( (IUnknown *)iface );
        *out = iface;
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IAppNotificationFactory ))
    {
        IUnknown_AddRef( (IUnknown *)iface );
        *out = &impl->IAppNotificationFactory_iface;
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI app_notification_factory_AddRef( IActivationFactory *iface )
{
    struct app_notification_factory *impl = impl_from_app_notification_factory( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI app_notification_factory_Release( IActivationFactory *iface )
{
    struct app_notification_factory *impl = impl_from_app_notification_factory( iface );
    return InterlockedDecrement( &impl->ref );
}

static HRESULT WINAPI app_notification_factory_GetIids( IActivationFactory *iface, ULONG *iid_count, IID **iids )
{
    if (!iid_count || !iids) return E_POINTER;
    *iid_count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI app_notification_factory_GetRuntimeClassName( IActivationFactory *iface,
        HSTRING *class_name )
{
    return WindowsCreateString( RuntimeClass_Microsoft_Windows_AppNotifications_AppNotification,
            ARRAY_SIZE( RuntimeClass_Microsoft_Windows_AppNotifications_AppNotification ) - 1, class_name );
}

static HRESULT WINAPI app_notification_factory_GetTrustLevel( IActivationFactory *iface,
        TrustLevel *trust_level )
{
    if (trust_level) *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI app_notification_factory_ActivateInstance( IActivationFactory *iface,
        IInspectable **instance )
{
    struct app_notification *impl;
    HRESULT hr;

    TRACE( "iface %p, instance %p.\n", iface, instance );

    if (!instance) return E_POINTER;
    if (FAILED( hr = create_app_notification( NULL, &impl ) )) return hr;
    *instance = (IInspectable *)&impl->IAppNotification_iface;
    return S_OK;
}

static const struct IActivationFactoryVtbl app_notification_factory_vtbl =
{
    app_notification_factory_QueryInterface,
    app_notification_factory_AddRef,
    app_notification_factory_Release,
    /* IInspectable methods */
    app_notification_factory_GetIids,
    app_notification_factory_GetRuntimeClassName,
    app_notification_factory_GetTrustLevel,
    /* IActivationFactory methods */
    app_notification_factory_ActivateInstance,
};

FORWARD_IINSPECTABLE( app_notification_factory_statics, IAppNotificationFactory, struct app_notification_factory,
        impl_from_IAppNotificationFactory, &impl->IActivationFactory_iface )

static HRESULT WINAPI app_notification_factory_CreateInstance( IAppNotificationFactory *iface,
        HSTRING payload, __x_ABI_CMicrosoft_CWindows_CAppNotifications_CIAppNotification **value )
{
    struct app_notification *impl;
    HRESULT hr;

    TRACE( "iface %p, payload %s, value %p.\n", iface, debugstr_hstring( payload ), value );

    if (!value) return E_POINTER;
    if (FAILED( hr = create_app_notification( payload, &impl ) )) return hr;
    *value = (__x_ABI_CMicrosoft_CWindows_CAppNotifications_CIAppNotification *)&impl->IAppNotification_iface;
    return S_OK;
}

static const struct IAppNotificationFactoryVtbl app_notification_factory_vtbl_factory =
{
    app_notification_factory_statics_QueryInterface,
    app_notification_factory_statics_AddRef,
    app_notification_factory_statics_Release,
    /* IInspectable methods */
    app_notification_factory_statics_GetIids,
    app_notification_factory_statics_GetRuntimeClassName,
    app_notification_factory_statics_GetTrustLevel,
    /* IAppNotificationFactory methods */
    app_notification_factory_CreateInstance,
};

static struct app_notification_factory app_notification_factory_obj =
{
    { &app_notification_factory_vtbl },
    { &app_notification_factory_vtbl_factory },
    1
};

/*
 * Class factory for AppNotificationActivatedEventArgs (parameterless).
 */
struct app_notification_activated_args_factory
{
    IActivationFactory IActivationFactory_iface;
    LONG ref;
};

static inline struct app_notification_activated_args_factory *impl_from_args_factory(
        IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct app_notification_activated_args_factory,
                              IActivationFactory_iface );
}

static HRESULT WINAPI args_factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown )
            || IsEqualGUID( iid, &IID_IInspectable )
            || IsEqualGUID( iid, &IID_IAgileObject )
            || IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        IUnknown_AddRef( (IUnknown *)iface );
        *out = iface;
        return S_OK;
    }

    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI args_factory_AddRef( IActivationFactory *iface )
{
    struct app_notification_activated_args_factory *impl = impl_from_args_factory( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI args_factory_Release( IActivationFactory *iface )
{
    struct app_notification_activated_args_factory *impl = impl_from_args_factory( iface );
    return InterlockedDecrement( &impl->ref );
}

static HRESULT WINAPI args_factory_GetIids( IActivationFactory *iface, ULONG *iid_count, IID **iids )
{
    if (!iid_count || !iids) return E_POINTER;
    *iid_count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI args_factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *class_name )
{
    return WindowsCreateString( RuntimeClass_Microsoft_Windows_AppNotifications_AppNotificationActivatedEventArgs,
            ARRAY_SIZE( RuntimeClass_Microsoft_Windows_AppNotifications_AppNotificationActivatedEventArgs ) - 1,
            class_name );
}

static HRESULT WINAPI args_factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *trust_level )
{
    if (trust_level) *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI args_factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    struct app_notification_activated_args *impl;
    HRESULT hr;

    TRACE( "iface %p, instance %p.\n", iface, instance );

    if (!instance) return E_POINTER;
    if (FAILED( hr = create_app_notification_activated_args( NULL, &impl ) )) return hr;
    *instance = (IInspectable *)&impl->IAppNotificationActivatedEventArgs_iface;
    return S_OK;
}

static const struct IActivationFactoryVtbl args_factory_vtbl =
{
    args_factory_QueryInterface,
    args_factory_AddRef,
    args_factory_Release,
    /* IInspectable methods */
    args_factory_GetIids,
    args_factory_GetRuntimeClassName,
    args_factory_GetTrustLevel,
    /* IActivationFactory methods */
    args_factory_ActivateInstance,
};

static struct app_notification_activated_args_factory args_factory_obj =
{
    { &args_factory_vtbl },
    1
};

IActivationFactory *app_notification_manager_statics_factory =
    &manager_factory_obj.IActivationFactory_iface;

IActivationFactory *app_notification_factory =
    &app_notification_factory_obj.IActivationFactory_iface;

IActivationFactory *app_notification_activated_args_factory =
    &args_factory_obj.IActivationFactory_iface;
