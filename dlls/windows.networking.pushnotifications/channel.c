/* WinRT Windows.Networking.PushNotifications channel objects
 *
 * Copyright 2026 Wine Developers
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

#include "private.h"

WINE_DEFAULT_DEBUG_CHANNEL(pushnotif);

/*
 * A prefix has no Windows Push Notification Services back end, so the channel
 * this module hands out is a stand-in: a fixed, syntactically valid WNS style
 * URI, an expiry 24 hours out, Close and the event registration points
 * accepting and forgetting everything.  Callers that store the URI and hand it
 * to their back end, and callers that await the creation operation, both get a
 * real object back instead of a failure the projection would throw on.
 */

static const WCHAR channel_uri[] = L"https://db5.notify.windows.com/?token=animvwZ...fake";

/* WNS channels expire daily; the FILETIME epoch is the DateTime epoch. */
static const INT64 channel_lifetime = 864000000000ll; /* 24 hours in 100ns units */

static HRESULT create_class_name( const WCHAR *name, HSTRING *class_name )
{
    if (!class_name) return E_POINTER;
    return WindowsCreateString( name, lstrlenW( name ), class_name );
}

static LONG next_token;

struct push_notification_channel
{
    const void *channel_vtbl;
    LONG ref;
};

static inline struct push_notification_channel *impl_from_channel( const void *iface )
{
    return (struct push_notification_channel *)iface;
}

static HRESULT WINAPI channel_QueryInterface( const void *iface, REFIID iid, void **out )
{
    struct push_notification_channel *impl = impl_from_channel( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (!out) return E_POINTER;
    *out = NULL;

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IPushNotificationChannel ))
    {
        *out = (void *)impl;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG WINAPI channel_AddRef( const void *iface )
{
    struct push_notification_channel *impl = impl_from_channel( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI channel_Release( const void *iface )
{
    struct push_notification_channel *impl = impl_from_channel( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref) free( impl );
    return ref;
}

static HRESULT WINAPI channel_GetIids( const void *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI channel_GetRuntimeClassName( const void *iface, HSTRING *class_name )
{
    return create_class_name( L"Windows.Networking.PushNotifications.PushNotificationChannel", class_name );
}

static HRESULT WINAPI channel_GetTrustLevel( const void *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI channel_get_Uri( const void *iface, HSTRING *value )
{
    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    return WindowsCreateString( channel_uri, lstrlenW( channel_uri ), value );
}

static HRESULT WINAPI channel_get_ExpirationTime( const void *iface,
                                                  __x_ABI_CWindows_CFoundation_CDateTime *value )
{
    FILETIME ft;

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    GetSystemTimeAsFileTime( &ft );
    value->UniversalTime = ((INT64)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    value->UniversalTime += channel_lifetime;
    return S_OK;
}

static HRESULT WINAPI channel_Close( const void *iface )
{
    TRACE( "iface %p.\n", iface );
    return S_OK;
}

static HRESULT WINAPI channel_add_PushNotificationReceived( const void *iface,
        __FIEventHandler_1_Windows__CNetworking__CPushNotifications__CPushNotificationReceivedEventArgs *handler,
        EventRegistrationToken *token )
{
    TRACE( "iface %p, handler %p, token %p.\n", iface, handler, token );

    if (!token) return E_POINTER;
    token->value = InterlockedIncrement( &next_token );
    return S_OK;
}

static HRESULT WINAPI channel_remove_PushNotificationReceived( const void *iface,
                                                               EventRegistrationToken token )
{
    TRACE( "iface %p, token %s.\n", iface, wine_dbgstr_longlong( token.value ) );
    return S_OK;
}

static const void *push_notification_channel_vtbl[] =
{
    channel_QueryInterface,
    channel_AddRef,
    channel_Release,
    channel_GetIids,
    channel_GetRuntimeClassName,
    channel_GetTrustLevel,
    channel_get_Uri,
    channel_get_ExpirationTime,
    channel_Close,
    channel_add_PushNotificationReceived,
    channel_remove_PushNotificationReceived,
};

IInspectable *create_push_notification_channel( void )
{
    struct push_notification_channel *impl;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return NULL;
    impl->channel_vtbl = push_notification_channel_vtbl;
    impl->ref = 1;

    TRACE( "created PushNotificationChannel %p.\n", impl );
    return (IInspectable *)impl;
}

/*
 * PushNotificationChannelManagerForUser stand-in (GetDefault / GetForUser
 * result).  Its channel creation points hand out the same stand-in channel the
 * statics do, and the User property stays null as there is no user manager.
 */

struct channel_manager_for_user
{
    const void *for_user_vtbl;
    LONG ref;
};

static inline struct channel_manager_for_user *impl_from_for_user( const void *iface )
{
    return (struct channel_manager_for_user *)iface;
}

static HRESULT create_channel_op( IAsyncOperation_PushNotificationChannel **operation )
{
    struct async_op *op;
    IInspectable *channel;

    if (!(channel = create_push_notification_channel())) return E_OUTOFMEMORY;
    if (!(op = create_async_op( &IID_IAsyncOperation_PushNotificationChannel,
                                L"Windows.Foundation.IAsyncOperation`1<Windows.Networking.PushNotifications.PushNotificationChannel>",
                                channel )))
    {
        IInspectable_Release( channel );
        return E_OUTOFMEMORY;
    }
    IInspectable_Release( channel );

    *operation = (IAsyncOperation_PushNotificationChannel *)async_op_iface( op );
    TRACE( "created completed channel op %p.\n", *operation );
    return S_OK;
}

static HRESULT WINAPI for_user_QueryInterface( const void *iface, REFIID iid, void **out )
{
    struct channel_manager_for_user *impl = impl_from_for_user( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (!out) return E_POINTER;
    *out = NULL;

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IPushNotificationChannelManagerForUser ))
    {
        *out = (void *)impl;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG WINAPI for_user_AddRef( const void *iface )
{
    struct channel_manager_for_user *impl = impl_from_for_user( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI for_user_Release( const void *iface )
{
    struct channel_manager_for_user *impl = impl_from_for_user( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref) free( impl );
    return ref;
}

static HRESULT WINAPI for_user_GetIids( const void *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI for_user_GetRuntimeClassName( const void *iface, HSTRING *class_name )
{
    return create_class_name( L"Windows.Networking.PushNotifications.PushNotificationChannelManagerForUser",
                              class_name );
}

static HRESULT WINAPI for_user_GetTrustLevel( const void *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI for_user_CreatePushNotificationChannelForApplicationAsync( const void *iface,
        IAsyncOperation_PushNotificationChannel **operation )
{
    TRACE( "iface %p, operation %p.\n", iface, operation );

    if (!operation) return E_POINTER;
    return create_channel_op( operation );
}

static HRESULT WINAPI for_user_CreatePushNotificationChannelForApplicationAsyncWithId( const void *iface,
        HSTRING channel_uri_, IAsyncOperation_PushNotificationChannel **operation )
{
    TRACE( "iface %p, channel_uri %s, operation %p.\n", iface, debugstr_hstring( channel_uri_ ), operation );

    if (!operation) return E_POINTER;
    return create_channel_op( operation );
}

static HRESULT WINAPI for_user_CreatePushNotificationChannelForSecondaryTileAsync( const void *iface,
        HSTRING tile_id, IAsyncOperation_PushNotificationChannel **operation )
{
    TRACE( "iface %p, tile_id %s, operation %p.\n", iface, debugstr_hstring( tile_id ), operation );

    if (!operation) return E_POINTER;
    return create_channel_op( operation );
}

static HRESULT WINAPI for_user_get_User( const void *iface, __x_ABI_CWindows_CSystem_CUser **value )
{
    TRACE( "iface %p, value %p.\n", iface, value );
    if (!value) return E_POINTER;
    *value = NULL;
    return S_OK;
}

static const void *channel_manager_for_user_vtbl[] =
{
    for_user_QueryInterface,
    for_user_AddRef,
    for_user_Release,
    for_user_GetIids,
    for_user_GetRuntimeClassName,
    for_user_GetTrustLevel,
    for_user_CreatePushNotificationChannelForApplicationAsync,
    for_user_CreatePushNotificationChannelForApplicationAsyncWithId,
    for_user_CreatePushNotificationChannelForSecondaryTileAsync,
    for_user_get_User,
};

IInspectable *create_push_notification_channel_manager_for_user( void )
{
    struct channel_manager_for_user *impl;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return NULL;
    impl->for_user_vtbl = channel_manager_for_user_vtbl;
    impl->ref = 1;

    TRACE( "created PushNotificationChannelManagerForUser %p.\n", impl );
    return (IInspectable *)impl;
}
