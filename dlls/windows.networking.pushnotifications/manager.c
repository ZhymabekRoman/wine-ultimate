/* WinRT Windows.Networking.PushNotifications.PushNotificationChannelManager statics
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
 * PushNotificationChannelManager is the entry point apps use to obtain their
 * Windows Push Notification Services channel at startup.  There is no WNS back
 * end in a prefix, so the creation operations complete immediately with a
 * stand-in channel: the await/then path runs, the caller stores a syntactically
 * valid channel URI, and nothing is thrown through a noexcept boundary.
 */

struct pn_manager_statics
{
    IActivationFactory IActivationFactory_iface;
    IPushNotificationChannelManagerStatics IPushNotificationChannelManagerStatics_iface;
    IPushNotificationChannelManagerStatics2 IPushNotificationChannelManagerStatics2_iface;
    IPushNotificationChannelManagerStatics3 IPushNotificationChannelManagerStatics3_iface;
    IPushNotificationChannelManagerStatics4 IPushNotificationChannelManagerStatics4_iface;
    LONG ref;
};

static inline struct pn_manager_statics *impl_from_IActivationFactory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct pn_manager_statics, IActivationFactory_iface );
}

static HRESULT WINAPI factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct pn_manager_statics *impl = impl_from_IActivationFactory( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (!out) return E_POINTER;
    *out = NULL;

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = &impl->IActivationFactory_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IPushNotificationChannelManagerStatics ))
    {
        *out = &impl->IPushNotificationChannelManagerStatics_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IPushNotificationChannelManagerStatics2 ))
    {
        *out = &impl->IPushNotificationChannelManagerStatics2_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IPushNotificationChannelManagerStatics3 ))
    {
        *out = &impl->IPushNotificationChannelManagerStatics3_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IPushNotificationChannelManagerStatics4 ))
    {
        *out = &impl->IPushNotificationChannelManagerStatics4_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG WINAPI factory_AddRef( IActivationFactory *iface )
{
    struct pn_manager_statics *impl = impl_from_IActivationFactory( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI factory_Release( IActivationFactory *iface )
{
    struct pn_manager_statics *impl = impl_from_IActivationFactory( iface );
    return InterlockedDecrement( &impl->ref );
}

static HRESULT WINAPI factory_GetIids( IActivationFactory *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *class_name )
{
    static const WCHAR name[] = L"Windows.Networking.PushNotifications.PushNotificationChannelManager";

    if (!class_name) return E_POINTER;
    return WindowsCreateString( name, lstrlenW( name ), class_name );
}

static HRESULT WINAPI factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    /* The class is statics-only: Windows has no default constructor for it. */
    FIXME( "iface %p, instance %p stub!\n", iface, instance );
    return E_FAIL;
}

static const struct IActivationFactoryVtbl factory_vtbl =
{
    factory_QueryInterface,
    factory_AddRef,
    factory_Release,
    factory_GetIids,
    factory_GetRuntimeClassName,
    factory_GetTrustLevel,
    factory_ActivateInstance,
};

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
    return S_OK;
}

static HRESULT WINAPI statics_CreatePushNotificationChannelForApplicationAsync(
        IPushNotificationChannelManagerStatics *iface, IAsyncOperation_PushNotificationChannel **operation )
{
    TRACE( "iface %p, operation %p.\n", iface, operation );

    if (!operation) return E_POINTER;
    return create_channel_op( operation );
}

static HRESULT WINAPI statics_CreatePushNotificationChannelForApplicationAsyncWithId(
        IPushNotificationChannelManagerStatics *iface, HSTRING channel_uri,
        IAsyncOperation_PushNotificationChannel **operation )
{
    TRACE( "iface %p, channel_uri %s, operation %p.\n", iface, debugstr_hstring( channel_uri ), operation );

    if (!operation) return E_POINTER;
    return create_channel_op( operation );
}

static HRESULT WINAPI statics_CreatePushNotificationChannelForSecondaryTileAsync(
        IPushNotificationChannelManagerStatics *iface, HSTRING tile_id,
        IAsyncOperation_PushNotificationChannel **operation )
{
    TRACE( "iface %p, tile_id %s, operation %p.\n", iface, debugstr_hstring( tile_id ), operation );

    if (!operation) return E_POINTER;
    return create_channel_op( operation );
}

DEFINE_IINSPECTABLE( statics, IPushNotificationChannelManagerStatics, struct pn_manager_statics,
                     IActivationFactory_iface )

static const struct IPushNotificationChannelManagerStaticsVtbl statics_vtbl =
{
    statics_QueryInterface,
    statics_AddRef,
    statics_Release,
    statics_GetIids,
    statics_GetRuntimeClassName,
    statics_GetTrustLevel,
    statics_CreatePushNotificationChannelForApplicationAsync,
    statics_CreatePushNotificationChannelForApplicationAsyncWithId,
    statics_CreatePushNotificationChannelForSecondaryTileAsync,
};

static HRESULT WINAPI statics2_GetForUser( IPushNotificationChannelManagerStatics2 *iface,
                                           __x_ABI_CWindows_CSystem_CIUser *user,
                                           __x_ABI_CWindows_CNetworking_CPushNotifications_CIPushNotificationChannelManagerForUser **value )
{
    IInspectable *obj;

    TRACE( "iface %p, user %p, value %p.\n", iface, user, value );

    if (!value) return E_POINTER;
    if (!(obj = create_push_notification_channel_manager_for_user())) return E_OUTOFMEMORY;
    *value = (__x_ABI_CWindows_CNetworking_CPushNotifications_CIPushNotificationChannelManagerForUser *)obj;
    return S_OK;
}

DEFINE_IINSPECTABLE( statics2, IPushNotificationChannelManagerStatics2, struct pn_manager_statics,
                     IActivationFactory_iface )

static const struct IPushNotificationChannelManagerStatics2Vtbl statics2_vtbl =
{
    statics2_QueryInterface,
    statics2_AddRef,
    statics2_Release,
    statics2_GetIids,
    statics2_GetRuntimeClassName,
    statics2_GetTrustLevel,
    statics2_GetForUser,
};

static HRESULT WINAPI statics3_GetDefault( IPushNotificationChannelManagerStatics3 *iface,
                                           __x_ABI_CWindows_CNetworking_CPushNotifications_CIPushNotificationChannelManagerForUser **value )
{
    IInspectable *obj;

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    if (!(obj = create_push_notification_channel_manager_for_user())) return E_OUTOFMEMORY;
    *value = (__x_ABI_CWindows_CNetworking_CPushNotifications_CIPushNotificationChannelManagerForUser *)obj;
    return S_OK;
}

DEFINE_IINSPECTABLE( statics3, IPushNotificationChannelManagerStatics3, struct pn_manager_statics,
                     IActivationFactory_iface )

static const struct IPushNotificationChannelManagerStatics3Vtbl statics3_vtbl =
{
    statics3_QueryInterface,
    statics3_AddRef,
    statics3_Release,
    statics3_GetIids,
    statics3_GetRuntimeClassName,
    statics3_GetTrustLevel,
    statics3_GetDefault,
};

static HRESULT WINAPI statics4_ChannelsRevoked( IPushNotificationChannelManagerStatics4 *iface,
        __FIEventHandler_1_Windows__CNetworking__CPushNotifications__CPushNotificationChannelsRevokedEventArgs *handler,
        EventRegistrationToken *token )
{
    static LONG next_token;

    TRACE( "iface %p, handler %p, token %p.\n", iface, handler, token );

    if (!token) return E_POINTER;
    token->value = InterlockedIncrement( &next_token );
    return S_OK;
}

static HRESULT WINAPI statics4_remove_ChannelsRevoked( IPushNotificationChannelManagerStatics4 *iface,
                                                       EventRegistrationToken token )
{
    TRACE( "iface %p, token %s.\n", iface, wine_dbgstr_longlong( token.value ) );
    return S_OK;
}

DEFINE_IINSPECTABLE( statics4, IPushNotificationChannelManagerStatics4, struct pn_manager_statics,
                     IActivationFactory_iface )

static const struct IPushNotificationChannelManagerStatics4Vtbl statics4_vtbl =
{
    statics4_QueryInterface,
    statics4_AddRef,
    statics4_Release,
    statics4_GetIids,
    statics4_GetRuntimeClassName,
    statics4_GetTrustLevel,
    statics4_ChannelsRevoked,
    statics4_remove_ChannelsRevoked,
};

static struct pn_manager_statics pn_manager_statics =
{
    { &factory_vtbl },
    { &statics_vtbl },
    { &statics2_vtbl },
    { &statics3_vtbl },
    { &statics4_vtbl },
    1,
};

IActivationFactory *pushnotificationchannel_manager_factory = &pn_manager_statics.IActivationFactory_iface;
