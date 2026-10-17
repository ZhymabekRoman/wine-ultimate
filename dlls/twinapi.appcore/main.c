/*
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

#include "initguid.h"
#include "private.h"

WINE_DEFAULT_DEBUG_CHANNEL(twinapi);

/***********************************************************************
 *           RegisterAppConstrainedChangeNotification (twinapi.appcore.@)
 */
ULONG WINAPI RegisterAppConstrainedChangeNotification( PAPPCONSTRAIN_CHANGE_ROUTINE routine, void *context, PAPPCONSTRAIN_REGISTRATION *reg )
{
    FIXME( "routine %p, context %p, reg %p - stub.\n", routine, context, reg );
    return ERROR_CALL_NOT_IMPLEMENTED;
}

/***********************************************************************
 *           RegisterAppStateChangeNotification (twinapi.appcore.@)
 */
ULONG WINAPI RegisterAppStateChangeNotification( PAPPSTATE_CHANGE_ROUTINE routine, void *context, PAPPSTATE_REGISTRATION *reg )
{
    FIXME( "routine %p, context %p, reg %p - stub.\n", routine, context, reg );
    return ERROR_CALL_NOT_IMPLEMENTED;
}

/***********************************************************************
 *           UnregisterAppConstrainedChangeNotification (twinapi.appcore.@)
 */
void WINAPI UnregisterAppConstrainedChangeNotification( PAPPCONSTRAIN_REGISTRATION reg )
{
    FIXME( "reg %p - stub.\n", reg );
}

/***********************************************************************
 *           UnregisterAppStateChangeNotification (twinapi.appcore.@)
 */
void WINAPI UnregisterAppStateChangeNotification( PAPPSTATE_REGISTRATION reg )
{
    FIXME( "reg %p - stub.\n", reg );
}

/* Async Operation Stub */
static HRESULT WINAPI dummy_async_QueryInterface( void *iface, REFIID iid, void **out )
{
    *out = iface;
    return S_OK;
}
static ULONG WINAPI dummy_async_AddRef( void *iface ) { return 2; }
static ULONG WINAPI dummy_async_Release( void *iface ) { return 1; }
static HRESULT WINAPI dummy_async_GetIids( void *iface, ULONG *c, IID **i ) { return E_NOTIMPL; }
static HRESULT WINAPI dummy_async_GetRuntimeClassName( void *iface, HSTRING *n )
{
    return WindowsCreateString( L"Windows.Foundation.IAsyncOperation", 32, n );
}
static HRESULT WINAPI dummy_async_GetTrustLevel( void *iface, TrustLevel *l )
{
    if (l) *l = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI dummy_async_put_Completed( void *iface, void *handler )
{
    if (handler)
    {
        void **vtbl = *(void ***)handler;
        HRESULT (STDMETHODCALLTYPE *invoke)(void *, void *, UINT32) = (void *)vtbl[3];
        if (invoke) invoke( handler, iface, 1 /* Completed */ );
    }
    return S_OK;
}
static HRESULT WINAPI dummy_async_get_Completed( void *iface, void **handler ) { if (handler) *handler = NULL; return S_OK; }
static HRESULT WINAPI dummy_async_GetResults( void *iface, DWORD *results ) { if (results) *results = 0 /* Denied */; return S_OK; }

static HRESULT WINAPI dummy_async_get_Id( void *iface, UINT32 *id ) { if (id) *id = 1; return S_OK; }
static HRESULT WINAPI dummy_async_get_Status( void *iface, UINT32 *status ) { if (status) *status = 1 /* Completed */; return S_OK; }
static HRESULT WINAPI dummy_async_get_ErrorCode( void *iface, HRESULT *err ) { if (err) *err = S_OK; return S_OK; }
static HRESULT WINAPI dummy_async_Cancel( void *iface ) { return S_OK; }
static HRESULT WINAPI dummy_async_Close( void *iface ) { return S_OK; }

static const void *dummy_async_vtbl[] =
{
    dummy_async_QueryInterface,
    dummy_async_AddRef,
    dummy_async_Release,
    dummy_async_GetIids,
    dummy_async_GetRuntimeClassName,
    dummy_async_GetTrustLevel,
    dummy_async_put_Completed,
    dummy_async_get_Completed,
    dummy_async_GetResults,
    dummy_async_get_Id,
    dummy_async_get_Status,
    dummy_async_get_ErrorCode,
    dummy_async_Cancel,
    dummy_async_Close,
};

static struct { const void *vtbl; } dummy_async = { dummy_async_vtbl };

/* Push Notification Channel Manager */
static HRESULT WINAPI push_notification_stub( void *iface, void *a, void *b, void *c )
{
    return E_NOTIMPL;
}

static HRESULT WINAPI push_notification_add_ChannelsRevoked( void *iface, void *handler, EventRegistrationToken *token )
{
    TRACE( "iface %p, handler %p, token %p\n", iface, handler, token );
    if (token) token->value = 1;
    return S_OK;
}

static HRESULT WINAPI push_notification_remove_ChannelsRevoked( void *iface, EventRegistrationToken token )
{
    return S_OK;
}

static const void *push_notification_factory_vtbl[];
static const void *push_notification_statics4_vtbl[];
static struct
{
    const void *vtbl_factory;
    const void *vtbl_statics4;
} push_notification_obj = { push_notification_factory_vtbl, push_notification_statics4_vtbl };

static HRESULT WINAPI push_notification_factory_QueryInterface( void *iface, REFIID iid, void **out )
{
    static const GUID IID_IPushNotificationChannelManagerStatics4 = {0xbc540efb, 0x7820, 0x5a5b, {0x9c, 0x01, 0xb4, 0x75, 0x7f, 0x77, 0x40, 0x25}};

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ) ||
        !memcmp( iid, "\x65\x9b\xaf\x8b\xa1\x77\x88\x45\xbd\x19\x86\x15\x29\xa9\xdc\xf0", 16 ))
    {
        *out = &push_notification_obj.vtbl_factory;
        return S_OK;
    }
    else if (IsEqualGUID( iid, &IID_IPushNotificationChannelManagerStatics4 ))
    {
        *out = &push_notification_obj.vtbl_statics4;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static HRESULT WINAPI push_notification_statics4_QueryInterface( void *iface, REFIID iid, void **out )
{
    return push_notification_factory_QueryInterface( iface, iid, out );
}

static ULONG WINAPI push_notification_factory_AddRef( void *iface ) { return 2; }
static ULONG WINAPI push_notification_factory_Release( void *iface ) { return 1; }
static HRESULT WINAPI push_notification_factory_GetIids( void *iface, ULONG *count, IID **iids ) { return E_NOTIMPL; }
static HRESULT WINAPI push_notification_factory_GetRuntimeClassName( void *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.Networking.PushNotifications.PushNotificationChannelManager", 59, name );
}
static HRESULT WINAPI push_notification_factory_GetTrustLevel( void *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}

static const void *push_notification_factory_vtbl[] =
{
    push_notification_factory_QueryInterface,
    push_notification_factory_AddRef,
    push_notification_factory_Release,
    push_notification_factory_GetIids,
    push_notification_factory_GetRuntimeClassName,
    push_notification_factory_GetTrustLevel,
    push_notification_stub, /* ActivateInstance */
    push_notification_stub, /* CreatePushNotificationChannelForApplicationAsync */
    push_notification_stub,
    push_notification_stub,
    push_notification_stub,
    push_notification_stub,
};

static const void *push_notification_statics4_vtbl[] =
{
    push_notification_statics4_QueryInterface,
    push_notification_factory_AddRef,
    push_notification_factory_Release,
    push_notification_factory_GetIids,
    push_notification_factory_GetRuntimeClassName,
    push_notification_factory_GetTrustLevel,
    push_notification_add_ChannelsRevoked,
    push_notification_remove_ChannelsRevoked,
};

/* Background Execution Manager */
static HRESULT WINAPI bg_manager_RequestAccessAsync( void *iface, void **operation )
{
    TRACE( "iface %p, operation %p\n", iface, operation );
    if (!operation) return E_POINTER;
    *operation = &dummy_async;
    return S_OK;
}

static const void *bg_manager_factory_vtbl[];
static struct { const void *vtbl; } bg_manager_obj = { bg_manager_factory_vtbl };

static HRESULT WINAPI bg_manager_factory_QueryInterface( void *iface, REFIID iid, void **out )
{
    static const GUID IID_IBackgroundExecutionManagerStatics = {0xe826ea58, 0x66a9, 0x4d41, {0x83, 0xd4, 0xb4, 0xc1, 0x8c, 0x87, 0xb8, 0x46}};

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ) ||
        IsEqualGUID( iid, &IID_IBackgroundExecutionManagerStatics ))
    {
        *out = &bg_manager_obj;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static const void *bg_manager_factory_vtbl[] =
{
    bg_manager_factory_QueryInterface,
    push_notification_factory_AddRef,
    push_notification_factory_Release,
    push_notification_factory_GetIids,
    push_notification_factory_GetRuntimeClassName,
    push_notification_factory_GetTrustLevel,
    bg_manager_RequestAccessAsync, /* slot 6: RequestAccessAsync */
    push_notification_stub,
    push_notification_stub,
    push_notification_stub,
    push_notification_stub,
};

HRESULT WINAPI DllGetClassObject( REFCLSID clsid, REFIID riid, void **out )
{
    FIXME( "clsid %s, riid %s, out %p stub!\n", debugstr_guid(clsid), debugstr_guid(riid), out );
    return CLASS_E_CLASSNOTAVAILABLE;
}

HRESULT WINAPI DllGetActivationFactory( HSTRING classid, IActivationFactory **factory )
{
    const WCHAR *buffer = WindowsGetStringRawBuffer( classid, NULL );

    TRACE( "class %s, factory %p.\n", debugstr_hstring(classid), factory );

    *factory = NULL;

    if (!wcscmp( buffer, RuntimeClass_Windows_Security_ExchangeActiveSyncProvisioning_EasClientDeviceInformation ))
        IActivationFactory_QueryInterface( client_device_information_factory, &IID_IActivationFactory, (void **)factory );
    else if (!wcscmp( buffer, RuntimeClass_Windows_System_Profile_AnalyticsInfo ))
        IActivationFactory_QueryInterface( analytics_info_factory, &IID_IActivationFactory, (void **)factory );
    else if (!wcscmp( buffer, RuntimeClass_Windows_System_UserProfile_AdvertisingManager ))
        IActivationFactory_QueryInterface( advertising_manager_factory, &IID_IActivationFactory, (void **)factory );
    else if (!wcscmp( buffer, RuntimeClass_Windows_UI_ViewManagement_ApplicationView ))
        IActivationFactory_QueryInterface( application_view_factory, &IID_IActivationFactory, (void **)factory );
    else if (!wcscmp( buffer, RuntimeClass_Windows_ApplicationModel_Core_CoreApplication ))
        IActivationFactory_QueryInterface( core_application_factory, &IID_IActivationFactory, (void **)factory );
    else if (!wcscmp( buffer, L"Windows.ApplicationModel.AppInstance" ))
        IActivationFactory_QueryInterface( app_instance_factory, &IID_IActivationFactory, (void **)factory );
    else if (!wcscmp( buffer, RuntimeClass_Windows_ApplicationModel_DataTransfer_DataTransferManager ))
        IActivationFactory_QueryInterface( data_transfer_manager_statics_factory, &IID_IActivationFactory, (void **)factory );
    else if (!wcscmp( buffer, L"Windows.Networking.PushNotifications.PushNotificationChannelManager" ))
        *factory = (IActivationFactory *)&push_notification_obj;
    else if (!wcscmp( buffer, L"Windows.ApplicationModel.Background.BackgroundExecutionManager" ))
        *factory = (IActivationFactory *)&bg_manager_obj;

    if (*factory) return S_OK;
    return CLASS_E_CLASSNOTAVAILABLE;
}
