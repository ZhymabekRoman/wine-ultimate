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

#include "unixlib.h"
#include "pri.h"

WINE_DEFAULT_DEBUG_CHANNEL(twinapi);

BOOL WINAPI DllMain( HINSTANCE inst, DWORD reason, LPVOID reserved )
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        __wine_init_unix_call();
    }
    return TRUE;
}

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
static const GUID IID_IReferenceTracker = {0x11d3b13a, 0x180e, 0x4789, {0xa8, 0xbe, 0x77, 0x12, 0x88, 0x28, 0x93, 0xe6}};
static const GUID IID_IReferenceTrackerTarget = {0x5c13e51c, 0x4f32, 0x4726, {0xa3, 0xfd, 0xf3, 0xed, 0xd6, 0x3d, 0xa3, 0xa0}};

enum dummy_async_results_kind
{
    DUMMY_ASYNC_RESULTS_CHANNEL = 0,
    DUMMY_ASYNC_RESULTS_STATUS32 = 1,
};

struct dummy_async_impl
{
    const void *vtbl_op;
    const void *vtbl_info;
    enum dummy_async_results_kind results_kind;
};

static const void *dummy_async_op_vtbl[];
static const void *dummy_async_info_vtbl[];
static struct dummy_async_impl dummy_async;

static HRESULT WINAPI dummy_async_QueryInterface( void *iface, REFIID iid, void **out )
{
    static const GUID IID_IAsyncOperation_boolean_local =
        { 0xcdb5efb3, 0x5788, 0x509d, { 0x9b, 0xe1, 0x71, 0xcc, 0xb8, 0xa3, 0x36, 0x2a } };
    static const GUID IID_IAsyncOperation_PushChannel_local =
        { 0x70945a09, 0x331a, 0x5e40, { 0xb8, 0x54, 0x66, 0xb7, 0xa3, 0x23, 0x3b, 0xab } };
    static const GUID IID_IAsyncOperation_StartupTaskState_local =
        { 0x55733555, 0x3474, 0x5d19, { 0x9c, 0xc3, 0x9b, 0x75, 0xbe, 0x39, 0x1b, 0x7f } };
    static const GUID IID_IAsyncOperation_BackgroundAccessStatus_local =
        { 0x7b44e581, 0xcfa9, 0x5763, { 0xbe, 0xd7, 0x6a, 0x65, 0x73, 0x9f, 0x0d, 0xbf } };
    struct dummy_async_impl *impl = iface;

    TRACE( "iface %p, iid %s, out %p\n", iface, debugstr_guid( iid ), out );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IAsyncInfo ))
    {
        *out = &impl->vtbl_info;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IAsyncAction ) ||
        IsEqualGUID( iid, &IID_IAsyncOperation_boolean_local ) ||
        IsEqualGUID( iid, &IID_IAsyncOperation_PushChannel_local ) ||
        IsEqualGUID( iid, &IID_IAsyncOperation_StartupTaskState_local ) ||
        IsEqualGUID( iid, &IID_IAsyncOperation_BackgroundAccessStatus_local ))
    {
        *out = &impl->vtbl_op;
        return S_OK;
    }
    FIXME( "unsupported iid %s\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}
static HRESULT WINAPI dummy_async_info_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct dummy_async_impl *impl = CONTAINING_RECORD( iface, struct dummy_async_impl, vtbl_info );
    return dummy_async_QueryInterface( &impl->vtbl_op, iid, out );
}
static ULONG WINAPI dummy_async_AddRef( void *iface ) { return 2; }
static ULONG WINAPI dummy_async_Release( void *iface ) { return 1; }
static HRESULT WINAPI dummy_async_GetIids( void *iface, ULONG *c, IID **i )
{
    if (c) *c = 0;
    if (i) *i = NULL;
    return S_OK;
}
static HRESULT WINAPI dummy_async_GetRuntimeClassName( void *iface, HSTRING *n )
{
    static const WCHAR cls[] = L"Windows.Foundation.IAsyncOperation";
    return WindowsCreateString( cls, ARRAY_SIZE(cls) - 1, n );
}
static HRESULT WINAPI dummy_async_GetTrustLevel( void *iface, TrustLevel *l )
{
    if (l) *l = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI dummy_async_put_Completed( void *iface, void *handler )
{
    TRACE( "iface %p, handler %p\n", iface, handler );
    if (handler)
    {
        /* Every completed handler is { QueryInterface, AddRef, Release, Invoke }; the concrete
         * generic IID varies with the operation's T, so the handler is called through the
         * action-handler layout. */
        IAsyncActionCompletedHandler *h = (IAsyncActionCompletedHandler *)handler;
        h->lpVtbl->Invoke( h, (IAsyncAction *)iface, 1 /* Completed */ );
    }
    return S_OK;
}
static HRESULT WINAPI dummy_async_get_Completed( void *iface, void **handler ) { if (handler) *handler = NULL; return S_OK; }
static HRESULT WINAPI push_notification_stub( void *iface, void *a, void *b, void *c )
{
    return E_NOTIMPL;
}

static const GUID IID_IWeakReference = {0x00000037, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
static const GUID IID_IWeakReferenceSource = {0x00000038, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
static const GUID IID_IBackgroundTaskRegistrationStatics = {0x4c542f69, 0xb000, 0x42ba, {0xa0, 0x93, 0x6a, 0x56, 0x3c, 0x65, 0xe3, 0xf8}};

struct dummy_channel_impl { const void *vtbl; };
static struct dummy_channel_impl dummy_channel;

static HRESULT WINAPI dummy_channel_QueryInterface( void *iface, REFIID iid, void **out );

static HRESULT WINAPI dummy_weakref_QueryInterface( void *iface, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IWeakReference ))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI dummy_weakref_AddRef( void *iface ) { return 2; }
static ULONG WINAPI dummy_weakref_Release( void *iface ) { return 1; }
static HRESULT WINAPI dummy_weakref_Resolve( void *iface, REFIID iid, void **out )
{
    return dummy_channel_QueryInterface( &dummy_channel, iid, out );
}
static const void *dummy_weakref_vtbl[] =
{
    dummy_weakref_QueryInterface,
    dummy_weakref_AddRef,
    dummy_weakref_Release,
    dummy_weakref_Resolve,
};
static struct { const void *vtbl; } dummy_weakref = { dummy_weakref_vtbl };

static HRESULT WINAPI dummy_channel_GetWeakReference( void *iface, void **ref )
{
    if (!ref) return E_POINTER;
    *ref = &dummy_weakref;
    return S_OK;
}
static const void *dummy_weakref_source_vtbl[] =
{
    dummy_channel_QueryInterface,
    dummy_async_AddRef,
    dummy_async_Release,
    dummy_channel_GetWeakReference,
};
static struct { const void *vtbl; } dummy_weakref_source = { dummy_weakref_source_vtbl };

static HRESULT WINAPI dummy_channel_QueryInterface( void *iface, REFIID iid, void **out )
{
    TRACE( "iface %p, iid %s, out %p\n", iface, debugstr_guid( iid ), out );

    if (!out) return E_POINTER;

    if (IsEqualGUID( iid, &IID_IWeakReferenceSource ))
    {
        *out = &dummy_weakref_source;
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IReferenceTracker ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget ) ||
        IsEqualGUID( iid, &IID_IStringable ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }

    /*
     * This QueryInterface is shared by the vtables of the push notification channel,
     * the startup task, the background task builder and the weak reference source
     * stubs, so it has to answer for every interface those objects are asked for.
     */
    *out = iface;
    return S_OK;
}

/* Default offline/local WNS endpoint URI returned by PushNotificationChannel::get_Uri
 * when no cloud push service daemon is connected. */
#define WNS_DEFAULT_CHANNEL_URI L"https://wns2-sn1.notify.windows.com/dummy"

static HRESULT WINAPI dummy_channel_get_Uri( void *iface, HSTRING *uri )
{
    return WindowsCreateString( WNS_DEFAULT_CHANNEL_URI, 39, uri );
}

static HRESULT WINAPI dummy_channel_get_ExpirationTime( void *iface, void *expiration_time )
{
    if (expiration_time) *(ULONGLONG *)expiration_time = 135000000000000000ULL;
    return S_OK;
}

static HRESULT WINAPI dummy_channel_Close( void *iface )
{
    return S_OK;
}

static HRESULT WINAPI dummy_channel_add_PushNotificationReceived( void *iface, void *handler, EventRegistrationToken *token )
{
    if (token) token->value = 1;
    return S_OK;
}

static HRESULT WINAPI dummy_channel_remove_PushNotificationReceived( void *iface, EventRegistrationToken token )
{
    return S_OK;
}

static const void *dummy_channel_vtbl[] =
{
    dummy_channel_QueryInterface,
    dummy_async_AddRef,
    dummy_async_Release,
    dummy_async_GetIids,
    dummy_async_GetRuntimeClassName,
    dummy_async_GetTrustLevel,
    dummy_channel_get_Uri,
    dummy_channel_get_ExpirationTime,
    dummy_channel_Close,
    dummy_channel_add_PushNotificationReceived,
    dummy_channel_remove_PushNotificationReceived,
};

static struct dummy_channel_impl dummy_channel = { dummy_channel_vtbl };

static HRESULT WINAPI dummy_async_GetResults( void *iface, void **results )
{
    struct dummy_async_impl *impl = iface;
    if (!results) return S_OK;
    if (impl->results_kind == DUMMY_ASYNC_RESULTS_STATUS32)
        *(UINT32 *)results = (UINT32)1;
    else
        *results = &dummy_channel;
    return S_OK;
}

static HRESULT WINAPI dummy_async_get_Id( void *iface, UINT32 *id ) { if (id) *id = 1; return S_OK; }
static HRESULT WINAPI dummy_async_get_Status( void *iface, UINT32 *status ) { if (status) *status = 1 /* Completed */; return S_OK; }
static HRESULT WINAPI dummy_async_get_ErrorCode( void *iface, HRESULT *err ) { if (err) *err = S_OK; return S_OK; }
static HRESULT WINAPI dummy_async_Cancel( void *iface ) { return S_OK; }
static HRESULT WINAPI dummy_async_Close( void *iface ) { return S_OK; }

static const void *dummy_async_op_vtbl[] =
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
};

static const void *dummy_async_info_vtbl[] =
{
    dummy_async_info_QueryInterface,
    dummy_async_AddRef,
    dummy_async_Release,
    dummy_async_GetIids,
    dummy_async_GetRuntimeClassName,
    dummy_async_GetTrustLevel,
    dummy_async_get_Id,
    dummy_async_get_Status,
    dummy_async_get_ErrorCode,
    dummy_async_Cancel,
    dummy_async_Close,
};

static struct dummy_async_impl dummy_async = { dummy_async_op_vtbl, dummy_async_info_vtbl };

static HRESULT WINAPI dummy_async_bool_GetResults( void *iface, boolean *results )
{
    if (results) *results = TRUE;
    return S_OK;
}
static const void *dummy_async_bool_op_vtbl[] =
{
    dummy_async_QueryInterface,
    dummy_async_AddRef,
    dummy_async_Release,
    dummy_async_GetIids,
    dummy_async_GetRuntimeClassName,
    dummy_async_GetTrustLevel,
    dummy_async_put_Completed,
    dummy_async_get_Completed,
    dummy_async_bool_GetResults,
};
static struct dummy_async_impl dummy_async_bool = { dummy_async_bool_op_vtbl, dummy_async_info_vtbl };

void *get_dummy_async_bool( void )
{
    return (void *)&dummy_async_bool.vtbl_op;
}

/* Push Notification Channel Manager */
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
    /* {8baf9b65-77a1-4588-bd19-861529a9dcf0} */
    static const GUID IID_IPushNotificationChannelManagerStatics = {0x8baf9b65, 0x77a1, 0x4588, {0xbd, 0x19, 0x86, 0x15, 0x29, 0xa9, 0xdc, 0xf0}};
    /* {bc540efb-7820-5a5b-9c01-b4757f774025} */
    static const GUID IID_IPushNotificationChannelManagerStatics4 = {0xbc540efb, 0x7820, 0x5a5b, {0x9c, 0x01, 0xb4, 0x75, 0x7f, 0x77, 0x40, 0x25}};

    TRACE( "iface %p, iid %s, out %p\n", iface, debugstr_guid( iid ), out );

    if (!out) return E_POINTER;

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ) ||
        IsEqualGUID( iid, &IID_IPushNotificationChannelManagerStatics ))
    {
        *out = &push_notification_obj.vtbl_factory;
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IPushNotificationChannelManagerStatics4 ))
    {
        *out = &push_notification_obj.vtbl_statics4;
        return S_OK;
    }

    /*
     * The .NET projection asks the factory for its weak reference when it registers the
     * ChannelsRevoked event handler, so hand out the shared weak reference source stub that
     * the rest of this file's objects use.  The factory is agile, so it needs no marshaler.
     */
    if (IsEqualGUID( iid, &IID_IWeakReferenceSource ))
    {
        *out = &dummy_weakref_source;
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static HRESULT WINAPI push_notification_statics4_QueryInterface( void *iface, REFIID iid, void **out )
{
    return push_notification_factory_QueryInterface( iface, iid, out );
}

static ULONG WINAPI push_notification_factory_AddRef( void *iface ) { return 2; }
static ULONG WINAPI push_notification_factory_Release( void *iface ) { return 1; }
static HRESULT WINAPI push_notification_factory_GetIids( void *iface, ULONG *count, IID **iids )
{
    /* {8baf9b65-77a1-4588-bd19-861529a9dcf0} */
    static const IID IID_IPushNotificationChannelManagerStatics = {0x8baf9b65, 0x77a1, 0x4588, {0xbd, 0x19, 0x86, 0x15, 0x29, 0xa9, 0xdc, 0xf0}};
    /* the statics and the statics4 surface the factory answers for */
    static const IID IID_IPushNotificationChannelManagerStatics4 = {0xbc540efb, 0x7820, 0x5a5b, {0x9c, 0x01, 0xb4, 0x75, 0x7f, 0x77, 0x40, 0x25}};
    static const IID *const iids_arr[] =
    {
        &IID_IPushNotificationChannelManagerStatics,
        &IID_IPushNotificationChannelManagerStatics4,
    };
    IID *arr;
    ULONG i;

    if (!count) return E_POINTER;
    *count = ARRAY_SIZE( iids_arr );
    if (iids)
    {
        if (!(arr = CoTaskMemAlloc( ARRAY_SIZE( iids_arr ) * sizeof(*arr) ))) return E_OUTOFMEMORY;
        for (i = 0; i < ARRAY_SIZE( iids_arr ); ++i) arr[i] = *iids_arr[i];
        *iids = arr;
    }
    return S_OK;
}
static HRESULT WINAPI push_notification_factory_GetRuntimeClassName( void *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.Networking.PushNotifications.PushNotificationChannelManager", 59, name );
}
static HRESULT WINAPI push_notification_factory_GetTrustLevel( void *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI push_notification_CreateChannelAsync( void *iface, void **operation )
{
    TRACE( "iface %p, operation %p\n", iface, operation );
    if (!operation) return E_POINTER;
    *operation = &dummy_async;
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
    push_notification_CreateChannelAsync, /* CreatePushNotificationChannelForApplicationAsync */
    push_notification_CreateChannelAsync,
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

static HRESULT WINAPI bg_async_GetResults( void *iface, DWORD *results )
{
    if (results) *results = (UINT32)1;
    return S_OK;
}

static const void *bg_async_op_vtbl[] =
{
    dummy_async_QueryInterface,
    dummy_async_AddRef,
    dummy_async_Release,
    dummy_async_GetIids,
    dummy_async_GetRuntimeClassName,
    dummy_async_GetTrustLevel,
    dummy_async_put_Completed,
    dummy_async_get_Completed,
    bg_async_GetResults,
};

static struct dummy_async_impl bg_async = { bg_async_op_vtbl, dummy_async_info_vtbl, DUMMY_ASYNC_RESULTS_STATUS32 };

/* Background Execution Manager */
static HRESULT WINAPI bg_manager_RequestAccessAsync( void *iface, void **operation )
{
    TRACE( "iface %p, operation %p\n", iface, operation );
    if (!operation) return E_POINTER;
    *operation = &bg_async;
    return S_OK;
}

static HRESULT WINAPI bg_manager_RequestAccessForApplicationAsync( void *iface, HSTRING app_id, void **operation )
{
    TRACE( "iface %p, app_id %s, operation %p\n", iface, debugstr_hstring( app_id ), operation );
    if (!operation) return E_POINTER;
    *operation = &bg_async;
    return S_OK;
}

static const void *bg_manager_factory_vtbl[];
static struct { const void *vtbl; } bg_manager_obj = { bg_manager_factory_vtbl };

static HRESULT WINAPI bg_manager_factory_QueryInterface( void *iface, REFIID iid, void **out )
{
    static const GUID IID_IBackgroundExecutionManagerStatics = {0xe826ea58, 0x66a9, 0x4d41, {0x83, 0xd4, 0xb4, 0xc1, 0x8c, 0x87, 0xb8, 0x46}};

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
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
    bg_manager_RequestAccessForApplicationAsync, /* slot 7: RequestAccessForApplicationAsync */
    push_notification_stub,
    push_notification_stub,
    push_notification_stub,
};

/* Background Task Registration */
/* IMapView<Guid, IBackgroundTaskRegistration> */
static const GUID my_IID_IMapView_GUID_IBackgroundTaskRegistration = {0xfbb695e5, 0x7c7b, 0x5b5c, {0xa5, 0x7b, 0x06, 0xb0, 0xad, 0xd4, 0x93, 0xac}};
/* IMapView<Guid, BackgroundTaskRegistration> */
static const GUID my_IID_IMapView_GUID_BackgroundTaskRegistration = {0xba3d387f, 0xf6b4, 0x566b, {0xbf, 0xd4, 0xc5, 0xa3, 0x8b, 0x6a, 0xf2, 0xc3}};
/* IIterable<IKeyValuePair<Guid, IBackgroundTaskRegistration>> */
static const GUID IID_IIterable_BackgroundTask = {0xcedf3c13, 0xab1e, 0x5aa1, {0xbc, 0xd6, 0x7f, 0xf0, 0xfe, 0xab, 0xfe, 0x27}};
/* .NET projection parameterized interface IIDs for the AllTasks map enumerable view */
static const GUID IID_IIterable_BackgroundTask_alt2 = {0xaf86e2e0, 0xb12d, 0x4c6a, {0x9c, 0x5a, 0xd7, 0xaa, 0x65, 0x10, 0x1e, 0x90}};
static const GUID IID_IIterable_BackgroundTask_alt3 = {0x80fb0327, 0x5a00, 0x55cc, {0x85, 0xdb, 0xa8, 0x52, 0x71, 0x99, 0x81, 0xb9}};
/* IIterator<IKeyValuePair<Guid, BackgroundTaskRegistration>> .NET projection IID */
static const GUID IID_IIterator_BackgroundTask_alt3 = {0x2001aea5, 0x1a86, 0x517e, {0x8b, 0xe5, 0x11, 0xd7, 0xfb, 0x59, 0x35, 0xb2}};
/* IIterable<IKeyValuePair<Guid, BackgroundTaskRegistration>> */
static const GUID IID_IIterable_BackgroundTask_alt1 = {0x201c5249, 0x8c9d, 0x53c2, {0x82, 0x59, 0x31, 0x33, 0xfc, 0x0a, 0xb9, 0x5f}};
/* IIterator<IKeyValuePair<Guid, IBackgroundTaskRegistration>> */
static const GUID IID_IIterator_BackgroundTask_alt1 = {0xe38d5c4b, 0xcc8b, 0x52d5, {0xb4, 0xb0, 0xca, 0xce, 0x05, 0x80, 0xf3, 0x34}};
/* IIterator<IKeyValuePair<Guid, BackgroundTaskRegistration>> */
static const GUID IID_IIterator_BackgroundTask_alt2 = {0xf54ec1fe, 0x70d8, 0x5c1e, {0x8c, 0x58, 0x75, 0x8a, 0x0e, 0x6f, 0x60, 0xb9}};

struct twinapi_dummy_obj { const void *vtbl; };
static const void *empty_iterator_vtbl[];
static struct twinapi_dummy_obj empty_iterator_obj = { empty_iterator_vtbl };

static const void *empty_map_view_vtbl[];
static struct twinapi_dummy_obj empty_map_view_obj;

static const void *empty_iterable_vtbl[];
static struct twinapi_dummy_obj empty_iterable_obj = { empty_iterable_vtbl };

static HRESULT WINAPI empty_iterator_QueryInterface( void *iface, REFIID iid, void **out )
{
    TRACE( "iface %p, iid %s, out %p\n", iface, debugstr_guid( iid ), out );

    if (!out) return E_POINTER;

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IIterator_BackgroundTask_alt1 ) ||
        IsEqualGUID( iid, &IID_IIterator_BackgroundTask_alt2 ) ||
        IsEqualGUID( iid, &IID_IIterator_BackgroundTask_alt3 ))
    {
        *out = &empty_iterator_obj;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IIterable_BackgroundTask_alt2 ))
    {
        *out = &empty_iterator_obj;
        return S_OK;
    }

    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI empty_iterator_AddRef( void *iface ) { return 2; }
static ULONG WINAPI empty_iterator_Release( void *iface ) { return 1; }
static HRESULT WINAPI empty_iterator_GetIids( void *iface, ULONG *c, IID **i )
{
    if (c) *c = 0;
    if (i) *i = NULL;
    return S_OK;
}
static HRESULT WINAPI empty_iterator_GetRuntimeClassName( void *iface, HSTRING *n )
{
    return WindowsCreateString( L"Windows.Foundation.Collections.IIterator`1", 42, n );
}
static HRESULT WINAPI empty_iterator_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI empty_iterator_get_Current( void *iface, void **current )
{
    if (current) *current = NULL;
    return 0x8000000b /* E_BOUNDS */;
}
static HRESULT WINAPI empty_iterator_get_HasCurrent( void *iface, boolean *has_current ) { if (has_current) *has_current = FALSE; return S_OK; }
static HRESULT WINAPI empty_iterator_MoveNext( void *iface, boolean *has_current ) { if (has_current) *has_current = FALSE; return S_OK; }
static HRESULT WINAPI empty_iterator_GetMany( void *iface, unsigned int count, void **items, unsigned int *actual ) { if (actual) *actual = 0; return S_OK; }

static const void *empty_iterator_vtbl[] =
{
    empty_iterator_QueryInterface,
    empty_iterator_AddRef,
    empty_iterator_Release,
    empty_iterator_GetIids,
    empty_iterator_GetRuntimeClassName,
    empty_iterator_GetTrustLevel,
    empty_iterator_get_Current,
    empty_iterator_get_HasCurrent,
    empty_iterator_MoveNext,
    empty_iterator_GetMany,
};

static HRESULT WINAPI empty_iterable_QueryInterface( void *iface, REFIID iid, void **out )
{
    TRACE( "iface %p, iid %s, out %p\n", iface, debugstr_guid( iid ), out );

    if (!out) return E_POINTER;

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IIterable_BackgroundTask ) ||
        IsEqualGUID( iid, &IID_IIterable_BackgroundTask_alt1 ) ||
        IsEqualGUID( iid, &IID_IIterable_BackgroundTask_alt2 ) ||
        IsEqualGUID( iid, &IID_IIterable_BackgroundTask_alt3 ))
    {
        *out = &empty_iterable_obj;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IIterator_BackgroundTask_alt1 ) ||
        IsEqualGUID( iid, &IID_IIterator_BackgroundTask_alt2 ) ||
        IsEqualGUID( iid, &IID_IIterator_BackgroundTask_alt3 ))
    {
        *out = &empty_iterator_obj;
        return S_OK;
    }
    if (IsEqualGUID( iid, &my_IID_IMapView_GUID_IBackgroundTaskRegistration ) ||
        IsEqualGUID( iid, &my_IID_IMapView_GUID_BackgroundTaskRegistration ))
    {
        *out = &empty_map_view_obj;
        return S_OK;
    }

    *out = NULL;
    return E_NOINTERFACE;
}

static HRESULT WINAPI empty_iterable_GetRuntimeClassName( void *iface, HSTRING *n )
{
    return WindowsCreateString( L"Windows.Foundation.Collections.IIterable`1", 42, n );
}

static HRESULT WINAPI empty_iterable_First( void *iface, void **first )
{
    TRACE( "iface %p, first %p\n", iface, first );
    if (!first) return E_POINTER;
    *first = &empty_iterator_obj;
    return S_OK;
}
static const void *empty_iterable_vtbl[] =
{
    empty_iterable_QueryInterface,
    empty_iterator_AddRef,
    empty_iterator_Release,
    empty_iterator_GetIids,
    empty_iterable_GetRuntimeClassName,
    empty_iterator_GetTrustLevel,
    empty_iterable_First,
};

static HRESULT WINAPI empty_map_view_QueryInterface( void *iface, REFIID iid, void **out )
{
    TRACE( "iface %p, iid %s, out %p\n", iface, debugstr_guid( iid ), out );

    if (!out) return E_POINTER;

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &my_IID_IMapView_GUID_IBackgroundTaskRegistration ) ||
        IsEqualGUID( iid, &my_IID_IMapView_GUID_BackgroundTaskRegistration ))
    {
        *out = &empty_map_view_obj;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IIterable_BackgroundTask ) ||
        IsEqualGUID( iid, &IID_IIterable_BackgroundTask_alt1 ) ||
        IsEqualGUID( iid, &IID_IIterable_BackgroundTask_alt2 ) ||
        IsEqualGUID( iid, &IID_IIterable_BackgroundTask_alt3 ))
    {
        *out = &empty_iterable_obj;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IIterator_BackgroundTask_alt1 ) ||
        IsEqualGUID( iid, &IID_IIterator_BackgroundTask_alt2 ) ||
        IsEqualGUID( iid, &IID_IIterator_BackgroundTask_alt3 ) ||
        IsEqualGUID( iid, &IID_IIterable_BackgroundTask_alt2 ))
    {
        *out = &empty_iterator_obj;
        return S_OK;
    }

    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI empty_map_view_AddRef( void *iface ) { return 2; }
static ULONG WINAPI empty_map_view_Release( void *iface ) { return 1; }
static HRESULT WINAPI empty_map_view_GetIids( void *iface, ULONG *c, IID **i )
{
    if (c) *c = 0;
    if (i) *i = NULL;
    return S_OK;
}
static HRESULT WINAPI empty_map_view_GetRuntimeClassName( void *iface, HSTRING *n )
{
    return WindowsCreateString( L"Windows.Foundation.Collections.IMapView`2", 40, n );
}
static HRESULT WINAPI empty_map_view_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI empty_map_view_Lookup( void *iface, GUID key, void **value )
{
    TRACE( "iface %p, key %s, value %p\n", iface, debugstr_guid(&key), value );
    if (value) *value = NULL;
    return 0x8000000b /* E_BOUNDS */;
}
static HRESULT WINAPI empty_map_view_get_Size( void *iface, UINT32 *size )
{
    TRACE( "iface %p, size %p\n", iface, size );
    if (!size) return E_POINTER;
    *size = 0;
    return S_OK;
}
static HRESULT WINAPI empty_map_view_HasKey( void *iface, GUID key, boolean *found )
{
    TRACE( "iface %p, key %s, found %p\n", iface, debugstr_guid(&key), found );
    if (found) *found = FALSE;
    return S_OK;
}
static HRESULT WINAPI empty_map_view_Split( void *iface, void **first, void **second )
{
    TRACE( "iface %p, first %p, second %p\n", iface, first, second );
    if (first) *first = NULL;
    if (second) *second = NULL;
    return S_OK;
}

static const void *empty_map_view_vtbl[] =
{
    empty_map_view_QueryInterface,
    empty_map_view_AddRef,
    empty_map_view_Release,
    empty_map_view_GetIids,
    empty_map_view_GetRuntimeClassName,
    empty_map_view_GetTrustLevel,
    empty_map_view_Lookup,
    empty_map_view_get_Size,
    empty_map_view_HasKey,
    empty_map_view_Split,
};
static struct twinapi_dummy_obj empty_map_view_obj = { empty_map_view_vtbl };

static HRESULT WINAPI bg_task_get_AllTasks( void *iface, void **tasks )
{
    TRACE( "iface %p, tasks %p\n", iface, tasks );
    if (!tasks) return E_POINTER;
    *tasks = &empty_map_view_obj;
    return S_OK;
}
static const void *bg_task_factory_vtbl[];
static struct { const void *vtbl; } bg_task_obj = { bg_task_factory_vtbl };
static HRESULT WINAPI bg_task_factory_QueryInterface( void *iface, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ) ||
        IsEqualGUID( iid, &IID_IBackgroundTaskRegistrationStatics ))
    {
        *out = &bg_task_obj;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static const void *bg_task_factory_vtbl[] =
{
    bg_task_factory_QueryInterface,
    push_notification_factory_AddRef,
    push_notification_factory_Release,
    push_notification_factory_GetIids,
    push_notification_factory_GetRuntimeClassName,
    push_notification_factory_GetTrustLevel,
    bg_task_get_AllTasks,
};

/* Windows.System.Profile.HardwareIdentification */
static const GUID IID_IHardwareIdentificationStatics = {0x971260e0, 0xf170, 0x4a42, {0xbd, 0x55, 0xa9, 0x00, 0xb2, 0x12, 0xda, 0xe2}};
static const GUID IID_IHardwareToken = {0x28f6d4c0, 0xfb12, 0x40a4, {0x81, 0x67, 0x7f, 0x4e, 0x03, 0xd2, 0x72, 0x4c}};

struct dummy_hw_buffer_impl
{
    IBuffer IBuffer_iface;
    IBufferByteAccess IBufferByteAccess_iface;
    UINT32 capacity;
    UINT32 length;
    BYTE data[32];
};

static const void *hw_buffer_vtbl[];
static const void *hw_buffer_byte_access_vtbl[];

static struct dummy_hw_buffer_impl dummy_hw_buffer_id =
{
    { (const struct IBufferVtbl *)hw_buffer_vtbl },
    { (const struct IBufferByteAccessVtbl *)hw_buffer_byte_access_vtbl },
    32,
    32,
    {
        0x3a, 0x7f, 0x9b, 0x1c, 0x4e, 0x82, 0xd0, 0x55,
        0x12, 0xa4, 0xb8, 0x67, 0xef, 0x90, 0x23, 0x45,
        0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45,
        0x67, 0x89, 0xab, 0xcd, 0xef, 0xaa, 0xbb, 0xcc
    }
};

static struct dummy_hw_buffer_impl dummy_hw_buffer_sig =
{
    { (const struct IBufferVtbl *)hw_buffer_vtbl },
    { (const struct IBufferByteAccessVtbl *)hw_buffer_byte_access_vtbl },
    32,
    32,
    {
        0x5c, 0x21, 0x88, 0xfe, 0x90, 0x11, 0x34, 0x7a,
        0xbb, 0xcc, 0xdd, 0xee, 0x11, 0x22, 0x33, 0x44,
        0x55, 0x66, 0x77, 0x88, 0x99, 0x00, 0xaa, 0xbb,
        0xcc, 0xdd, 0xee, 0xff, 0x12, 0x34, 0x56, 0x78
    }
};

static struct dummy_hw_buffer_impl dummy_hw_buffer_cert =
{
    { (const struct IBufferVtbl *)hw_buffer_vtbl },
    { (const struct IBufferByteAccessVtbl *)hw_buffer_byte_access_vtbl },
    32,
    32,
    {
        0x30, 0x82, 0x01, 0x22, 0x30, 0x81, 0xca, 0xa0,
        0x03, 0x02, 0x01, 0x02, 0x02, 0x10, 0x12, 0x34,
        0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0, 0x12, 0x34,
        0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0, 0xaa, 0xbb
    }
};

static HRESULT WINAPI hw_buffer_QueryInterface( IBuffer *iface, REFIID iid, void **out )
{
    struct dummy_hw_buffer_impl *impl = CONTAINING_RECORD( iface, struct dummy_hw_buffer_impl, IBuffer_iface );

    TRACE( "iface %p, iid %s, out %p\n", iface, debugstr_guid( iid ), out );

    if (!out) return E_POINTER;

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IBuffer ))
    {
        *out = &impl->IBuffer_iface;
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IBufferByteAccess ))
    {
        *out = &impl->IBufferByteAccess_iface;
        return S_OK;
    }

    *out = NULL;
    return E_NOINTERFACE;
}

static HRESULT WINAPI hw_buffer_GetRuntimeClassName( IBuffer *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.Storage.Streams.Buffer", 29, name );
}

static HRESULT WINAPI hw_buffer_get_Capacity( IBuffer *iface, UINT32 *value )
{
    struct dummy_hw_buffer_impl *impl = CONTAINING_RECORD( iface, struct dummy_hw_buffer_impl, IBuffer_iface );
    if (!value) return E_POINTER;
    *value = impl->capacity;
    return S_OK;
}

static HRESULT WINAPI hw_buffer_get_Length( IBuffer *iface, UINT32 *value )
{
    struct dummy_hw_buffer_impl *impl = CONTAINING_RECORD( iface, struct dummy_hw_buffer_impl, IBuffer_iface );
    if (!value) return E_POINTER;
    *value = impl->length;
    return S_OK;
}

static HRESULT WINAPI hw_buffer_put_Length( IBuffer *iface, UINT32 value )
{
    struct dummy_hw_buffer_impl *impl = CONTAINING_RECORD( iface, struct dummy_hw_buffer_impl, IBuffer_iface );
    if (value > impl->capacity) return E_INVALIDARG;
    impl->length = value;
    return S_OK;
}

static const void *hw_buffer_vtbl[] =
{
    hw_buffer_QueryInterface,
    dummy_async_AddRef,
    dummy_async_Release,
    dummy_async_GetIids,
    hw_buffer_GetRuntimeClassName,
    dummy_async_GetTrustLevel,
    hw_buffer_get_Capacity,
    hw_buffer_get_Length,
    hw_buffer_put_Length,
};

static HRESULT WINAPI hw_buffer_byte_access_QueryInterface( IBufferByteAccess *iface, REFIID iid, void **out )
{
    struct dummy_hw_buffer_impl *impl = CONTAINING_RECORD( iface, struct dummy_hw_buffer_impl, IBufferByteAccess_iface );
    return hw_buffer_QueryInterface( &impl->IBuffer_iface, iid, out );
}

static HRESULT WINAPI hw_buffer_byte_access_Buffer( IBufferByteAccess *iface, byte **value )
{
    struct dummy_hw_buffer_impl *impl = CONTAINING_RECORD( iface, struct dummy_hw_buffer_impl, IBufferByteAccess_iface );
    if (!value) return E_POINTER;
    *value = impl->data;
    return S_OK;
}

static const void *hw_buffer_byte_access_vtbl[] =
{
    hw_buffer_byte_access_QueryInterface,
    dummy_async_AddRef,
    dummy_async_Release,
    hw_buffer_byte_access_Buffer,
};

static HRESULT WINAPI hw_token_QueryInterface( void *iface, REFIID iid, void **out )
{
    TRACE( "iface %p, iid %s, out %p\n", iface, debugstr_guid( iid ), out );

    if (!out) return E_POINTER;

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IHardwareToken ))
    {
        *out = iface;
        return S_OK;
    }

    *out = NULL;
    return E_NOINTERFACE;
}

static HRESULT WINAPI hw_token_GetRuntimeClassName( void *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.System.Profile.HardwareToken", 36, name );
}

static HRESULT WINAPI hw_token_get_Id( void *iface, IBuffer **value )
{
    TRACE( "iface %p, value %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = &dummy_hw_buffer_id.IBuffer_iface;
    return S_OK;
}

static HRESULT WINAPI hw_token_get_Signature( void *iface, IBuffer **value )
{
    TRACE( "iface %p, value %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = &dummy_hw_buffer_sig.IBuffer_iface;
    return S_OK;
}

static HRESULT WINAPI hw_token_get_Certificate( void *iface, IBuffer **value )
{
    TRACE( "iface %p, value %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = &dummy_hw_buffer_cert.IBuffer_iface;
    return S_OK;
}

static const void *hw_token_vtbl[] =
{
    hw_token_QueryInterface,
    dummy_async_AddRef,
    dummy_async_Release,
    dummy_async_GetIids,
    hw_token_GetRuntimeClassName,
    dummy_async_GetTrustLevel,
    hw_token_get_Id,
    hw_token_get_Signature,
    hw_token_get_Certificate,
};
static struct { const void *vtbl; } dummy_hw_token = { hw_token_vtbl };

static HRESULT WINAPI hw_id_GetPackageSpecificToken( void *iface, IBuffer *nonce, void **token )
{
    TRACE( "iface %p, nonce %p, token %p\n", iface, nonce, token );
    if (!token) return E_POINTER;
    *token = &dummy_hw_token;
    return S_OK;
}

static const void *hw_id_factory_vtbl[];
static const void *hw_id_statics_vtbl[];
static struct
{
    const void *vtbl_factory;
    const void *vtbl_statics;
} hw_id_obj = { hw_id_factory_vtbl, hw_id_statics_vtbl };

static HRESULT WINAPI hw_id_factory_QueryInterface( void *iface, REFIID iid, void **out )
{
    TRACE( "iface %p, iid %s, out %p\n", iface, debugstr_guid( iid ), out );

    if (!out) return E_POINTER;

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = &hw_id_obj.vtbl_factory;
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IHardwareIdentificationStatics ))
    {
        *out = &hw_id_obj.vtbl_statics;
        return S_OK;
    }

    *out = NULL;
    return E_NOINTERFACE;
}

static HRESULT WINAPI hw_id_statics_QueryInterface( void *iface, REFIID iid, void **out )
{
    return hw_id_factory_QueryInterface( iface, iid, out );
}

static HRESULT WINAPI hw_id_factory_GetRuntimeClassName( void *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.System.Profile.HardwareIdentification", 45, name );
}

static HRESULT WINAPI hw_id_factory_ActivateInstance( void *iface, void **instance )
{
    if (instance) *instance = NULL;
    return E_NOTIMPL;
}

static const void *hw_id_factory_vtbl[] =
{
    hw_id_factory_QueryInterface,
    dummy_async_AddRef,
    dummy_async_Release,
    dummy_async_GetIids,
    hw_id_factory_GetRuntimeClassName,
    dummy_async_GetTrustLevel,
    hw_id_factory_ActivateInstance,
};

static const void *hw_id_statics_vtbl[] =
{
    hw_id_statics_QueryInterface,
    dummy_async_AddRef,
    dummy_async_Release,
    dummy_async_GetIids,
    hw_id_factory_GetRuntimeClassName,
    dummy_async_GetTrustLevel,
    hw_id_GetPackageSpecificToken,
};

/* StartupTask stub */
static HRESULT WINAPI startup_task_get_State( void *iface, UINT32 *state )
{
    if (state) *state = 0 /* StartupTaskState_Disabled */;
    return S_OK;
}
static const void *startup_task_vtbl[] =
{
    dummy_channel_QueryInterface,
    dummy_async_AddRef,
    dummy_async_Release,
    dummy_async_GetIids,
    dummy_async_GetRuntimeClassName,
    dummy_async_GetTrustLevel,
    push_notification_stub, /* RequestEnableAsync */
    push_notification_stub, /* Disable */
    startup_task_get_State, /* get_State */
    push_notification_stub, /* get_TaskId */
};
static struct { const void *vtbl; } dummy_startup_task = { startup_task_vtbl };

static HRESULT WINAPI dummy_startup_task_async_GetResults( void *iface, void **results )
{
    if (results) *results = &dummy_startup_task;
    return S_OK;
}
static const void *dummy_startup_task_async_op_vtbl[];
static const void *dummy_startup_task_async_info_vtbl[];
static struct dummy_async_impl dummy_startup_task_async;

static HRESULT WINAPI dummy_startup_task_async_QueryInterface( void *iface, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IReferenceTracker ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    if (IsEqualGUID( iid, &IID_IAsyncInfo ))
    {
        *out = &dummy_startup_task_async.vtbl_info;
        return S_OK;
    }
    *out = &dummy_startup_task_async.vtbl_op;
    return S_OK;
}
static HRESULT WINAPI dummy_startup_task_async_info_QueryInterface( void *iface, REFIID iid, void **out )
{
    return dummy_startup_task_async_QueryInterface( iface, iid, out );
}

static const void *dummy_startup_task_async_op_vtbl[] =
{
    dummy_startup_task_async_QueryInterface,
    dummy_async_AddRef,
    dummy_async_Release,
    dummy_async_GetIids,
    dummy_async_GetRuntimeClassName,
    dummy_async_GetTrustLevel,
    dummy_async_put_Completed,
    dummy_async_get_Completed,
    dummy_startup_task_async_GetResults,
};

static const void *dummy_startup_task_async_info_vtbl[] =
{
    dummy_startup_task_async_info_QueryInterface,
    dummy_async_AddRef,
    dummy_async_Release,
    dummy_async_GetIids,
    dummy_async_GetRuntimeClassName,
    dummy_async_GetTrustLevel,
    dummy_async_get_Id,
    dummy_async_get_Status,
    dummy_async_get_ErrorCode,
    dummy_async_Cancel,
    dummy_async_Close,
};

static struct dummy_async_impl dummy_startup_task_async = { dummy_startup_task_async_op_vtbl, dummy_startup_task_async_info_vtbl };

static HRESULT WINAPI startup_task_GetForCurrentPackageAsync( void *iface, void **operation )
{
    if (!operation) return E_POINTER;
    *operation = &dummy_startup_task_async;
    return S_OK;
}

static HRESULT WINAPI startup_task_GetAsync( void *iface, HSTRING taskId, void **operation )
{
    if (!operation) return E_POINTER;
    *operation = &dummy_startup_task_async;
    return S_OK;
}

static const void *startup_task_factory_vtbl[];
static struct { const void *vtbl; } startup_task_factory_obj = { startup_task_factory_vtbl };
static HRESULT WINAPI startup_task_factory_QueryInterface( void *iface, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IReferenceTracker ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    *out = &startup_task_factory_obj;
    return S_OK;
}
static const void *startup_task_factory_vtbl[] =
{
    startup_task_factory_QueryInterface,
    push_notification_factory_AddRef,
    push_notification_factory_Release,
    push_notification_factory_GetIids,
    push_notification_factory_GetRuntimeClassName,
    push_notification_factory_GetTrustLevel,
    startup_task_GetForCurrentPackageAsync,
    startup_task_GetAsync,
};

/* PushNotificationTrigger stub (shared by the trigger factory below and
 * IBackgroundTaskRegistration::Trigger). */
static const void *push_trigger_vtbl[] =
{
    dummy_channel_QueryInterface,
    dummy_async_AddRef,
    dummy_async_Release,
    dummy_async_GetIids,
    dummy_async_GetRuntimeClassName,
    dummy_async_GetTrustLevel,
};
static struct { const void *vtbl; } dummy_push_trigger = { push_trigger_vtbl };

/* BackgroundTaskBuilder & CoreApplication lifecycle event registries */
struct bg_event_entry
{
    void *handler;
    INT64 token;
};

struct bg_event_registry
{
    struct bg_event_entry entries[64];
    INT64 next_token;
};

static struct bg_event_registry bg_events;
static struct bg_event_registry leaving_bg_events;
static struct bg_event_registry entered_bg_events;
static struct bg_event_registry resuming_events;

static CRITICAL_SECTION lifecycle_cs;
static CRITICAL_SECTION_DEBUG lifecycle_cs_debug =
{
    0, 0, &lifecycle_cs,
    { &lifecycle_cs_debug.ProcessLocksList, &lifecycle_cs_debug.ProcessLocksList },
    0, 0, { (DWORD_PTR)(__FILE__ ": lifecycle_cs") }
};
static CRITICAL_SECTION lifecycle_cs = { &lifecycle_cs_debug, -1, 0, 0, 0, 0 };

static INIT_ONCE focus_watcher_once = INIT_ONCE_STATIC_INIT;

static HRESULT event_registry_add( struct bg_event_registry *reg, void *handler, INT64 *token, BOOL addref )
{
    unsigned int i;

    if (!handler)
    {
        if (token) *token = 0;
        return S_OK;
    }

    EnterCriticalSection( &lifecycle_cs );
    for (i = 0; i < ARRAY_SIZE( reg->entries ); i++)
    {
        if (!reg->entries[i].handler)
        {
            INT64 tok = InterlockedIncrement64( &reg->next_token );
            if (addref) IUnknown_AddRef( (IUnknown *)handler );
            reg->entries[i].handler = handler;
            reg->entries[i].token = tok;
            if (token) *token = tok;
            LeaveCriticalSection( &lifecycle_cs );
            return S_OK;
        }
    }
    LeaveCriticalSection( &lifecycle_cs );
    return E_OUTOFMEMORY;
}

static HRESULT event_registry_remove( struct bg_event_registry *reg, INT64 token, BOOL release, HRESULT not_found_hr )
{
    unsigned int i;

    EnterCriticalSection( &lifecycle_cs );
    for (i = 0; i < ARRAY_SIZE( reg->entries ); i++)
    {
        if (reg->entries[i].handler && reg->entries[i].token == token)
        {
            void *h = reg->entries[i].handler;
            reg->entries[i].handler = NULL;
            reg->entries[i].token = 0;
            LeaveCriticalSection( &lifecycle_cs );
            if (release) IUnknown_Release( (IUnknown *)h );
            return S_OK;
        }
    }
    LeaveCriticalSection( &lifecycle_cs );
    return not_found_hr;
}

static unsigned int event_registry_snapshot( struct bg_event_registry *reg, void **out, unsigned int max_out )
{
    unsigned int i, count = 0;

    EnterCriticalSection( &lifecycle_cs );
    for (i = 0; i < ARRAY_SIZE( reg->entries ) && count < max_out; i++)
    {
        if (reg->entries[i].handler)
        {
            IUnknown_AddRef( (IUnknown *)reg->entries[i].handler );
            out[count++] = reg->entries[i].handler;
        }
    }
    LeaveCriticalSection( &lifecycle_cs );
    return count;
}

/* Complete-able deferral implementing Windows.Foundation.IDeferral, IClosable,
 * and Windows.ApplicationModel.Background.IBackgroundTaskDeferral. */
static const GUID IID_IBackgroundTaskDeferral_local = {0x93cc156d, 0xaf27, 0x4dd3, {0x84, 0x6e, 0x24, 0xee, 0x40, 0xca, 0xdd, 0x25}};

struct bg_deferral
{
    __x_ABI_CWindows_CFoundation_CIDeferral IDeferral_iface;
    __x_ABI_CWindows_CFoundation_CIClosable IClosable_iface;
    LONG ref;
    LONG completed;
};

static inline struct bg_deferral *impl_from_IDeferral( __x_ABI_CWindows_CFoundation_CIDeferral *iface )
{
    return CONTAINING_RECORD( iface, struct bg_deferral, IDeferral_iface );
}

static inline struct bg_deferral *impl_from_IClosable_deferral( __x_ABI_CWindows_CFoundation_CIClosable *iface )
{
    return CONTAINING_RECORD( iface, struct bg_deferral, IClosable_iface );
}

static HRESULT WINAPI bg_deferral_QueryInterface( __x_ABI_CWindows_CFoundation_CIDeferral *iface, REFIID iid, void **out )
{
    struct bg_deferral *impl = impl_from_IDeferral( iface );

    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID___x_ABI_CWindows_CFoundation_CIDeferral ) ||
        IsEqualGUID( iid, &IID_IBackgroundTaskDeferral_local ))
    {
        InterlockedIncrement( &impl->ref );
        *out = &impl->IDeferral_iface;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID___x_ABI_CWindows_CFoundation_CIClosable ))
    {
        InterlockedIncrement( &impl->ref );
        *out = &impl->IClosable_iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI bg_deferral_AddRef( __x_ABI_CWindows_CFoundation_CIDeferral *iface )
{
    struct bg_deferral *impl = impl_from_IDeferral( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI bg_deferral_Release( __x_ABI_CWindows_CFoundation_CIDeferral *iface )
{
    struct bg_deferral *impl = impl_from_IDeferral( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref) free( impl );
    return ref;
}

static HRESULT WINAPI bg_deferral_GetIids( __x_ABI_CWindows_CFoundation_CIDeferral *iface, ULONG *count, IID **iids )
{
    if (!count) return E_POINTER;
    *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI bg_deferral_GetRuntimeClassName( __x_ABI_CWindows_CFoundation_CIDeferral *iface, HSTRING *name )
{
    static const WCHAR cls[] = L"Windows.Foundation.Deferral";
    if (!name) return E_POINTER;
    return WindowsCreateString( cls, ARRAY_SIZE( cls ) - 1, name );
}

static HRESULT WINAPI bg_deferral_GetTrustLevel( __x_ABI_CWindows_CFoundation_CIDeferral *iface, TrustLevel *level )
{
    if (!level) return E_POINTER;
    *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI bg_deferral_Complete( __x_ABI_CWindows_CFoundation_CIDeferral *iface )
{
    struct bg_deferral *impl = impl_from_IDeferral( iface );
    if (!InterlockedCompareExchange( &impl->completed, 1, 0 ))
        TRACE( "deferral %p completed\n", impl );
    return S_OK;
}

static const __x_ABI_CWindows_CFoundation_CIDeferralVtbl bg_deferral_vtbl =
{
    bg_deferral_QueryInterface,
    bg_deferral_AddRef,
    bg_deferral_Release,
    bg_deferral_GetIids,
    bg_deferral_GetRuntimeClassName,
    bg_deferral_GetTrustLevel,
    bg_deferral_Complete,
};

static HRESULT WINAPI bg_deferral_closable_QueryInterface( __x_ABI_CWindows_CFoundation_CIClosable *iface, REFIID iid, void **out )
{
    struct bg_deferral *impl = impl_from_IClosable_deferral( iface );
    return bg_deferral_QueryInterface( &impl->IDeferral_iface, iid, out );
}

static ULONG WINAPI bg_deferral_closable_AddRef( __x_ABI_CWindows_CFoundation_CIClosable *iface )
{
    struct bg_deferral *impl = impl_from_IClosable_deferral( iface );
    return bg_deferral_AddRef( &impl->IDeferral_iface );
}

static ULONG WINAPI bg_deferral_closable_Release( __x_ABI_CWindows_CFoundation_CIClosable *iface )
{
    struct bg_deferral *impl = impl_from_IClosable_deferral( iface );
    return bg_deferral_Release( &impl->IDeferral_iface );
}

static HRESULT WINAPI bg_deferral_closable_GetIids( __x_ABI_CWindows_CFoundation_CIClosable *iface, ULONG *count, IID **iids )
{
    struct bg_deferral *impl = impl_from_IClosable_deferral( iface );
    return bg_deferral_GetIids( &impl->IDeferral_iface, count, iids );
}

static HRESULT WINAPI bg_deferral_closable_GetRuntimeClassName( __x_ABI_CWindows_CFoundation_CIClosable *iface, HSTRING *name )
{
    struct bg_deferral *impl = impl_from_IClosable_deferral( iface );
    return bg_deferral_GetRuntimeClassName( &impl->IDeferral_iface, name );
}

static HRESULT WINAPI bg_deferral_closable_GetTrustLevel( __x_ABI_CWindows_CFoundation_CIClosable *iface, TrustLevel *level )
{
    struct bg_deferral *impl = impl_from_IClosable_deferral( iface );
    return bg_deferral_GetTrustLevel( &impl->IDeferral_iface, level );
}

static HRESULT WINAPI bg_deferral_closable_Close( __x_ABI_CWindows_CFoundation_CIClosable *iface )
{
    struct bg_deferral *impl = impl_from_IClosable_deferral( iface );
    return bg_deferral_Complete( &impl->IDeferral_iface );
}

static const __x_ABI_CWindows_CFoundation_CIClosableVtbl bg_deferral_closable_vtbl =
{
    bg_deferral_closable_QueryInterface,
    bg_deferral_closable_AddRef,
    bg_deferral_closable_Release,
    bg_deferral_closable_GetIids,
    bg_deferral_closable_GetRuntimeClassName,
    bg_deferral_closable_GetTrustLevel,
    bg_deferral_closable_Close,
};

static __x_ABI_CWindows_CFoundation_CIDeferral *bg_deferral_create( void )
{
    struct bg_deferral *impl;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return NULL;
    impl->IDeferral_iface.lpVtbl = &bg_deferral_vtbl;
    impl->IClosable_iface.lpVtbl = &bg_deferral_closable_vtbl;
    impl->ref = 1;
    return &impl->IDeferral_iface;
}

/* LeavingBackgroundEventArgs / EnteredBackgroundEventArgs */
struct bg_lifecycle_args
{
    const void *lpVtbl;
    LONG ref;
    BOOL entered;
};

static HRESULT WINAPI bg_lifecycle_args_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct bg_lifecycle_args *impl = iface;
    const IID *primary = impl->entered
        ? &IID___x_ABI_CWindows_CApplicationModel_CIEnteredBackgroundEventArgs
        : &IID___x_ABI_CWindows_CApplicationModel_CILeavingBackgroundEventArgs;

    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, primary ))
    {
        InterlockedIncrement( &impl->ref );
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI bg_lifecycle_args_AddRef( void *iface )
{
    struct bg_lifecycle_args *impl = iface;
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI bg_lifecycle_args_Release( void *iface )
{
    struct bg_lifecycle_args *impl = iface;
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref) free( impl );
    return ref;
}

static HRESULT WINAPI bg_lifecycle_args_GetIids( void *iface, ULONG *count, IID **iids )
{
    if (!count) return E_POINTER;
    *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI bg_lifecycle_args_GetRuntimeClassName( void *iface, HSTRING *name )
{
    struct bg_lifecycle_args *impl = iface;
    static const WCHAR entered_cls[] = L"Windows.ApplicationModel.EnteredBackgroundEventArgs";
    static const WCHAR leaving_cls[] = L"Windows.ApplicationModel.LeavingBackgroundEventArgs";
    if (!name) return E_POINTER;
    return impl->entered
        ? WindowsCreateString( entered_cls, ARRAY_SIZE( entered_cls ) - 1, name )
        : WindowsCreateString( leaving_cls, ARRAY_SIZE( leaving_cls ) - 1, name );
}

static HRESULT WINAPI bg_lifecycle_args_GetTrustLevel( void *iface, TrustLevel *level )
{
    if (!level) return E_POINTER;
    *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI bg_lifecycle_args_GetDeferral( void *iface, __x_ABI_CWindows_CFoundation_CIDeferral **value )
{
    struct bg_lifecycle_args *impl = iface;
    __x_ABI_CWindows_CFoundation_CIDeferral *def;

    TRACE( "%s args %p GetDeferral\n", impl->entered ? "EnteredBackground" : "LeavingBackground", impl );
    if (!value) return E_POINTER;
    if (!(def = bg_deferral_create()))
    {
        *value = NULL;
        return E_OUTOFMEMORY;
    }
    *value = def;
    return S_OK;
}

static const void *bg_lifecycle_args_vtbl[] =
{
    bg_lifecycle_args_QueryInterface,
    bg_lifecycle_args_AddRef,
    bg_lifecycle_args_Release,
    bg_lifecycle_args_GetIids,
    bg_lifecycle_args_GetRuntimeClassName,
    bg_lifecycle_args_GetTrustLevel,
    bg_lifecycle_args_GetDeferral,
};

static void *bg_lifecycle_args_create( BOOL entered )
{
    struct bg_lifecycle_args *impl;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return NULL;
    impl->lpVtbl = bg_lifecycle_args_vtbl;
    impl->ref = 1;
    impl->entered = entered;
    return impl;
}

/* Window-focus watcher: EnumWindows lookup for the process's top-level frame
 * window (xaml_get_window_hwnd-style), polling GetForegroundWindow/GetActiveWindow
 * every 250 ms and firing LeavingBackground/Resuming on gain, EnteredBackground on loss. */
struct focus_enum_ctx
{
    DWORD pid;
    HWND preferred;
    HWND fallback;
};

static BOOL CALLBACK focus_enum_windows_cb( HWND hwnd, LPARAM lparam )
{
    struct focus_enum_ctx *ctx = (struct focus_enum_ctx *)lparam;
    DWORD wpid = 0;
    WCHAR cls[128];
    RECT rc;

    GetWindowThreadProcessId( hwnd, &wpid );
    if (wpid != ctx->pid) return TRUE;
    if (!IsWindowVisible( hwnd )) return TRUE;
    if (GetWindow( hwnd, GW_OWNER )) return TRUE;
    if (!GetWindowRect( hwnd, &rc )) return TRUE;
    if (rc.right - rc.left < 64 || rc.bottom - rc.top < 64) return TRUE;

    if (GetClassNameW( hwnd, cls, ARRAY_SIZE( cls ) ))
    {
        if (!wcscmp( cls, L"WinUIDesktopWin32WindowClass" ) ||
            !wcscmp( cls, L"Windows.UI.Core.CoreWindow" ) ||
            !wcscmp( cls, L"ApplicationFrameWindow" ))
        {
            ctx->preferred = hwnd;
            return FALSE;
        }
    }
    if (!ctx->fallback) ctx->fallback = hwnd;
    return TRUE;
}

static HWND find_app_main_hwnd( void )
{
    struct focus_enum_ctx ctx = { GetCurrentProcessId(), NULL, NULL };
    EnumWindows( focus_enum_windows_cb, (LPARAM)&ctx );
    return ctx.preferred ? ctx.preferred : ctx.fallback;
}

static BOOL is_app_window_focused( HWND main_hwnd )
{
    HWND fg, active;
    DWORD fg_pid = 0, main_tid;
    GUITHREADINFO gti;

    if (!main_hwnd || !IsWindow( main_hwnd ) || !IsWindowVisible( main_hwnd ) || IsIconic( main_hwnd ))
        return FALSE;

    fg = GetForegroundWindow();
    if (fg)
    {
        if (fg == main_hwnd || GetAncestor( fg, GA_ROOT ) == main_hwnd ||
            GetWindow( fg, GW_OWNER ) == main_hwnd)
            return TRUE;
        GetWindowThreadProcessId( fg, &fg_pid );
        if (fg_pid == GetCurrentProcessId() && IsWindowVisible( fg ))
            return TRUE;
        return FALSE;
    }

    active = GetActiveWindow();
    if (active && (active == main_hwnd || GetAncestor( active, GA_ROOT ) == main_hwnd))
        return TRUE;

    main_tid = GetWindowThreadProcessId( main_hwnd, NULL );
    gti.cbSize = sizeof(gti);
    if (main_tid && GetGUIThreadInfo( main_tid, &gti ) && gti.hwndActive)
    {
        if (gti.hwndActive == main_hwnd || GetAncestor( gti.hwndActive, GA_ROOT ) == main_hwnd)
            return TRUE;
    }
    return FALSE;
}

static void fire_leaving_background( void )
{
    void *handlers[64];
    unsigned int i, count = event_registry_snapshot( &leaving_bg_events, handlers, ARRAY_SIZE( handlers ) );
    __x_ABI_CWindows_CApplicationModel_CILeavingBackgroundEventArgs *args;

    if (!count) return;
    if (!(args = bg_lifecycle_args_create( FALSE )))
    {
        for (i = 0; i < count; i++) IUnknown_Release( (IUnknown *)handlers[i] );
        return;
    }

    for (i = 0; i < count; i++)
    {
        __FIEventHandler_1_Windows__CApplicationModel__CLeavingBackgroundEventArgs *h = handlers[i];
        TRACE( "firing LeavingBackground handler %p\n", h );
        __FIEventHandler_1_Windows__CApplicationModel__CLeavingBackgroundEventArgs_Invoke(
            h, (IInspectable *)core_application_factory, args );
        IUnknown_Release( (IUnknown *)h );
    }
    IUnknown_Release( (IUnknown *)args );
}

static void fire_resuming( void )
{
    void *handlers[64];
    unsigned int i, count = event_registry_snapshot( &resuming_events, handlers, ARRAY_SIZE( handlers ) );

    for (i = 0; i < count; i++)
    {
        __FIEventHandler_1_IInspectable *h = handlers[i];
        TRACE( "firing Resuming handler %p\n", h );
        __FIEventHandler_1_IInspectable_Invoke( h, (IInspectable *)core_application_factory, NULL );
        IUnknown_Release( (IUnknown *)h );
    }
}

static void fire_entered_background( void )
{
    void *handlers[64];
    unsigned int i, count = event_registry_snapshot( &entered_bg_events, handlers, ARRAY_SIZE( handlers ) );
    __x_ABI_CWindows_CApplicationModel_CIEnteredBackgroundEventArgs *args;

    core_app_flush_properties();

    if (!count) return;
    if (!(args = bg_lifecycle_args_create( TRUE )))
    {
        for (i = 0; i < count; i++) IUnknown_Release( (IUnknown *)handlers[i] );
        return;
    }

    for (i = 0; i < count; i++)
    {
        __FIEventHandler_1_Windows__CApplicationModel__CEnteredBackgroundEventArgs *h = handlers[i];
        TRACE( "firing EnteredBackground handler %p\n", h );
        __FIEventHandler_1_Windows__CApplicationModel__CEnteredBackgroundEventArgs_Invoke(
            h, (IInspectable *)core_application_factory, args );
        IUnknown_Release( (IUnknown *)h );
    }
    IUnknown_Release( (IUnknown *)args );
}

static DWORD WINAPI focus_watcher_proc( void *param )
{
    HWND tracked_hwnd = NULL;
    BOOL was_focused = FALSE;

    CoInitializeEx( NULL, COINIT_MULTITHREADED );
    TRACE( "focus_watcher_proc started\n" );

    for (;;)
    {
        BOOL now_focused;

        Sleep( 250 );
        if (!tracked_hwnd || !IsWindow( tracked_hwnd ) || !IsWindowVisible( tracked_hwnd ))
            tracked_hwnd = find_app_main_hwnd();

        now_focused = is_app_window_focused( tracked_hwnd );
        if (now_focused && !was_focused)
        {
            TRACE( "focus gained hwnd=%p -> LeavingBackground + Resuming\n", tracked_hwnd );
            was_focused = TRUE;
            fire_leaving_background();
            fire_resuming();
        }
        else if (!now_focused && was_focused)
        {
            TRACE( "focus lost hwnd=%p -> EnteredBackground\n", tracked_hwnd );
            was_focused = FALSE;
            fire_entered_background();
        }
    }
    return 0;
}

static BOOL CALLBACK init_focus_watcher_cb( INIT_ONCE *once, void *param, void **context )
{
    HANDLE thread = CreateThread( NULL, 0, focus_watcher_proc, NULL, 0, NULL );
    if (thread) CloseHandle( thread );
    return thread != NULL;
}

void ensure_focus_watcher_started( void )
{
    InitOnceExecuteOnce( &focus_watcher_once, init_focus_watcher_cb, NULL, NULL );
}

HRESULT core_app_add_leaving_background(
        __FIEventHandler_1_Windows__CApplicationModel__CLeavingBackgroundEventArgs *handler,
        EventRegistrationToken *token )
{
    HRESULT hr = event_registry_add( &leaving_bg_events, handler, token ? &token->value : NULL, TRUE );
    TRACE( "handler %p -> token %#I64x, hr %#lx\n", handler, token ? token->value : 0, hr );
    if (SUCCEEDED( hr ) && handler) ensure_focus_watcher_started();
    return hr;
}

HRESULT core_app_remove_leaving_background( EventRegistrationToken token )
{
    TRACE( "token %#I64x\n", token.value );
    return event_registry_remove( &leaving_bg_events, token.value, TRUE, S_OK );
}

HRESULT core_app_add_entered_background(
        __FIEventHandler_1_Windows__CApplicationModel__CEnteredBackgroundEventArgs *handler,
        EventRegistrationToken *token )
{
    HRESULT hr = event_registry_add( &entered_bg_events, handler, token ? &token->value : NULL, TRUE );
    TRACE( "handler %p -> token %#I64x, hr %#lx\n", handler, token ? token->value : 0, hr );
    if (SUCCEEDED( hr ) && handler) ensure_focus_watcher_started();
    return hr;
}

HRESULT core_app_remove_entered_background( EventRegistrationToken token )
{
    TRACE( "token %#I64x\n", token.value );
    return event_registry_remove( &entered_bg_events, token.value, TRUE, S_OK );
}

HRESULT core_app_add_resuming( __FIEventHandler_1_IInspectable *handler,
                               EventRegistrationToken *token )
{
    HRESULT hr = event_registry_add( &resuming_events, handler, token ? &token->value : NULL, TRUE );
    TRACE( "handler %p -> token %#I64x, hr %#lx\n", handler, token ? token->value : 0, hr );
    if (SUCCEEDED( hr ) && handler) ensure_focus_watcher_started();
    return hr;
}

HRESULT core_app_remove_resuming( EventRegistrationToken token )
{
    TRACE( "token %#I64x\n", token.value );
    return event_registry_remove( &resuming_events, token.value, TRUE, S_OK );
}

/* One registration per app: the id is stable for the process lifetime and
 * unique across processes (time + QPC + pid entropy), like a real TaskId. */
static GUID bg_task_id;

static HRESULT WINAPI bg_task_reg_get_TaskId( void *iface, GUID *id )
{
    LARGE_INTEGER pc;

    if (!id) return E_POINTER;
    if (!bg_task_id.Data1)
    {
        QueryPerformanceCounter( &pc );
        bg_task_id.Data1 = pc.LowPart;
        bg_task_id.Data2 = (USHORT)(pc.HighPart & 0xffff);
        bg_task_id.Data3 = (USHORT)((pc.HighPart >> 16) ^ GetTickCount());
        bg_task_id.Data4[0] = (BYTE)GetCurrentProcessId();
        bg_task_id.Data4[1] = (BYTE)(GetCurrentProcessId() >> 8);
        bg_task_id.Data4[2] = (BYTE)(GetCurrentProcessId() >> 16);
        bg_task_id.Data4[3] = (BYTE)(GetCurrentProcessId() >> 24);
        bg_task_id.Data4[4] = (BYTE)GetCurrentThreadId();
        bg_task_id.Data4[5] = (BYTE)(GetTickCount() >> 8);
        bg_task_id.Data4[6] = (BYTE)(GetTickCount() >> 16);
        bg_task_id.Data4[7] = (BYTE)(GetTickCount() >> 24);
    }
    *id = bg_task_id;
    return S_OK;
}
static WCHAR bg_task_name[256];
static WCHAR bg_builder_name[256];

static HRESULT WINAPI bg_task_reg_get_Name( void *iface, HSTRING *name )
{
    const WCHAR *n = bg_task_name[0] ? bg_task_name : L"BackgroundTask";
    if (!name) return E_POINTER;
    return WindowsCreateString( n, lstrlenW( n ), name );
}
static HRESULT WINAPI bg_task_reg_Unregister( void *iface, boolean cancel ) { return S_OK; }
static HRESULT WINAPI bg_task_reg_add_event( void *iface, void *handler, INT64 *token )
{
    return event_registry_add( &bg_events, handler, token, FALSE );
}
static HRESULT WINAPI bg_task_reg_remove_event( void *iface, INT64 token )
{
    return event_registry_remove( &bg_events, token, FALSE, E_INVALIDARG );
}
static HRESULT WINAPI bg_task_reg_Cancel( void *iface ) { return S_OK; }
static HRESULT WINAPI bg_task_reg_get_Trigger( void *iface, void **trigger )
{
    if (!trigger) return E_POINTER;
    *trigger = &dummy_push_trigger;
    return S_OK;
}

static const void *bg_task_reg_vtbl[] =
{
    dummy_channel_QueryInterface,
    dummy_async_AddRef,
    dummy_async_Release,
    dummy_async_GetIids,
    dummy_async_GetRuntimeClassName,
    dummy_async_GetTrustLevel,
    bg_task_reg_get_TaskId,    /* slot 6: get_TaskId */
    bg_task_reg_get_Name,      /* slot 7: get_Name */
    bg_task_reg_add_event,     /* slot 8: add_Progress */
    bg_task_reg_remove_event,  /* slot 9: remove_Progress */
    bg_task_reg_add_event,     /* slot 10: add_Completed */
    bg_task_reg_remove_event,  /* slot 11: remove_Completed */
    bg_task_reg_Unregister,    /* slot 12: Unregister */
    bg_task_reg_get_Trigger,   /* slot 13 */
    bg_task_reg_Cancel,        /* slot 14 */
};
static struct { const void *vtbl; } dummy_bg_task_reg = { bg_task_reg_vtbl };

static HRESULT WINAPI bg_builder_put_Name( void *iface, HSTRING name )
{
    const WCHAR *raw = name ? WindowsGetStringRawBuffer( name, NULL ) : NULL;
    if (raw && raw[0])
    {
        lstrcpynW( bg_builder_name, raw, ARRAY_SIZE( bg_builder_name ) );
        lstrcpynW( bg_task_name, raw, ARRAY_SIZE( bg_task_name ) );
    }
    else
    {
        bg_builder_name[0] = 0;
        bg_task_name[0] = 0;
    }
    return S_OK;
}

static HRESULT WINAPI bg_builder_get_Name( void *iface, HSTRING *name )
{
    if (!name) return E_POINTER;
    return WindowsCreateString( bg_builder_name, lstrlenW( bg_builder_name ), name );
}

static HRESULT WINAPI bg_builder_Register( void *iface, void **reg )
{
    if (!reg) return E_POINTER;
    if (bg_builder_name[0])
        lstrcpynW( bg_task_name, bg_builder_name, ARRAY_SIZE( bg_task_name ) );
    *reg = &dummy_bg_task_reg;
    return S_OK;
}
static HRESULT WINAPI bg_builder_Register_name( void *iface, void *name, void **reg )
{
    const WCHAR *raw = name ? WindowsGetStringRawBuffer( (HSTRING)name, NULL ) : NULL;
    if (!reg) return E_POINTER;
    if (raw && raw[0])
        lstrcpynW( bg_task_name, raw, ARRAY_SIZE( bg_task_name ) );
    else if (bg_builder_name[0])
        lstrcpynW( bg_task_name, bg_builder_name, ARRAY_SIZE( bg_task_name ) );
    *reg = &dummy_bg_task_reg;
    return S_OK;
}
static HRESULT WINAPI bg_builder_Register_group( void *iface, void *name, const GUID *group, void **reg )
{
    return bg_builder_Register_name( iface, name, reg );
}

static HRESULT WINAPI bg_builder_get_TaskEntryPoint( void *iface, HSTRING *value )
{
    if (!value) return E_POINTER;
    *value = NULL;
    return S_OK;
}
static HRESULT WINAPI bg_builder_success( void *iface, void *a, void *b, void *c ) { return S_OK; }

static const void *bg_builder_vtbl[] =
{
    dummy_channel_QueryInterface,
    dummy_async_AddRef,
    dummy_async_Release,
    dummy_async_GetIids,
    dummy_async_GetRuntimeClassName,
    dummy_async_GetTrustLevel,
    bg_builder_success,     /* put_TaskEntryPoint */
    bg_builder_get_TaskEntryPoint, /* get_TaskEntryPoint */
    bg_builder_success,     /* SetTrigger */
    bg_builder_success,     /* AddCondition */
    bg_builder_put_Name,    /* put_Name */
    bg_builder_get_Name,    /* get_Name */
    bg_builder_Register,    /* Register() */
    bg_builder_Register_name,  /* Register(name) */
    bg_builder_Register_group, /* Register(name, group) */
    bg_builder_success,     /* SetTaskEntryPointClsid */
};
static struct { const void *vtbl; } dummy_bg_builder = { bg_builder_vtbl };

static HRESULT WINAPI bg_builder_factory_ActivateInstance( void *iface, void **instance )
{
    if (!instance) return E_POINTER;
    *instance = &dummy_bg_builder;
    return S_OK;
}

static const void *bg_builder_factory_vtbl[];
static struct { const void *vtbl; } bg_builder_factory_obj = { bg_builder_factory_vtbl };
static HRESULT WINAPI bg_builder_factory_QueryInterface( void *iface, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IReferenceTracker ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    *out = &bg_builder_factory_obj;
    return S_OK;
}
static const void *bg_builder_factory_vtbl[] =
{
    bg_builder_factory_QueryInterface,
    push_notification_factory_AddRef,
    push_notification_factory_Release,
    push_notification_factory_GetIids,
    push_notification_factory_GetRuntimeClassName,
    push_notification_factory_GetTrustLevel,
    bg_builder_factory_ActivateInstance,
};

static HRESULT WINAPI push_trigger_factory_ActivateInstance( void *iface, void **instance )
{
    if (!instance) return E_POINTER;
    *instance = &dummy_push_trigger;
    return S_OK;
}
static const void *push_trigger_factory_vtbl[];
static struct { const void *vtbl; } push_trigger_factory_obj = { push_trigger_factory_vtbl };
static HRESULT WINAPI push_trigger_factory_QueryInterface( void *iface, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IReferenceTracker ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    *out = &push_trigger_factory_obj;
    return S_OK;
}
static const void *push_trigger_factory_vtbl[] =
{
    push_trigger_factory_QueryInterface,
    push_notification_factory_AddRef,
    push_notification_factory_Release,
    push_notification_factory_GetIids,
    push_notification_factory_GetRuntimeClassName,
    push_notification_factory_GetTrustLevel,
    push_trigger_factory_ActivateInstance,
};

/* TimeTrigger and SystemTrigger */
static const GUID IID_ITimeTriggerFactory_local =
    { 0x38c682fe, 0x9b54, 0x45e6, { 0xb2, 0xf3, 0x26, 0x9b, 0x87, 0xa6, 0xf7, 0x34 } };
static const GUID IID_ISystemTriggerFactory_local =
    { 0xe80423d4, 0x8791, 0x4579, { 0x81, 0x26, 0x87, 0xec, 0x8a, 0xaa, 0x40, 0x7a } };

struct bg_trigger_obj
{
    const void **vtbl;
    LONG ref;
    UINT32 param;
    boolean one_shot;
};

static HRESULT WINAPI bg_trigger_QueryInterface( void *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IReferenceTracker ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget ) ||
        IsEqualGUID( iid, &IID_IStringable ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    IUnknown_AddRef( (IUnknown *)iface );
    *out = iface;
    return S_OK;
}

static ULONG WINAPI bg_trigger_AddRef( void *iface )
{
    struct bg_trigger_obj *impl = iface;
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI bg_trigger_Release( void *iface )
{
    struct bg_trigger_obj *impl = iface;
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref) free( impl );
    return ref;
}

static HRESULT WINAPI time_trigger_get_FreshnessTime( void *iface, UINT32 *value )
{
    struct bg_trigger_obj *impl = iface;
    if (!value) return E_POINTER;
    *value = impl->param;
    return S_OK;
}

static HRESULT WINAPI time_trigger_get_OneShot( void *iface, boolean *value )
{
    struct bg_trigger_obj *impl = iface;
    if (!value) return E_POINTER;
    *value = impl->one_shot;
    return S_OK;
}

static const void *time_trigger_vtbl[] =
{
    bg_trigger_QueryInterface,
    bg_trigger_AddRef,
    bg_trigger_Release,
    dummy_async_GetIids,
    dummy_async_GetRuntimeClassName,
    dummy_async_GetTrustLevel,
    time_trigger_get_FreshnessTime,
    time_trigger_get_OneShot,
};

static HRESULT WINAPI sys_trigger_get_OneShot( void *iface, boolean *value )
{
    struct bg_trigger_obj *impl = iface;
    if (!value) return E_POINTER;
    *value = impl->one_shot;
    return S_OK;
}

static HRESULT WINAPI sys_trigger_get_TriggerType( void *iface, UINT32 *value )
{
    struct bg_trigger_obj *impl = iface;
    if (!value) return E_POINTER;
    *value = impl->param;
    return S_OK;
}

static const void *sys_trigger_vtbl[] =
{
    bg_trigger_QueryInterface,
    bg_trigger_AddRef,
    bg_trigger_Release,
    dummy_async_GetIids,
    dummy_async_GetRuntimeClassName,
    dummy_async_GetTrustLevel,
    sys_trigger_get_OneShot,
    sys_trigger_get_TriggerType,
};

static HRESULT WINAPI time_trigger_factory_Create( void *iface, UINT32 freshness_time, boolean one_shot, void **trigger )
{
    struct bg_trigger_obj *obj;
    if (!trigger) return E_POINTER;
    if (!(obj = calloc( 1, sizeof(*obj) ))) return E_OUTOFMEMORY;
    obj->vtbl = time_trigger_vtbl;
    obj->ref = 1;
    obj->param = freshness_time;
    obj->one_shot = one_shot;
    *trigger = obj;
    return S_OK;
}

static const void *time_trigger_factory_vtbl[];
static struct { const void **vtbl; } time_trigger_factory_obj = { time_trigger_factory_vtbl };

static HRESULT WINAPI time_trigger_factory_QueryInterface( void *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IReferenceTracker ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    if (IsEqualGUID( iid, &IID_ITimeTriggerFactory_local ))
    {
        *out = &time_trigger_factory_obj;
        return S_OK;
    }
    *out = &time_trigger_factory_obj;
    return S_OK;
}

static const void *time_trigger_factory_vtbl[] =
{
    time_trigger_factory_QueryInterface,
    push_notification_factory_AddRef,
    push_notification_factory_Release,
    push_notification_factory_GetIids,
    push_notification_factory_GetRuntimeClassName,
    push_notification_factory_GetTrustLevel,
    time_trigger_factory_Create,
};

static HRESULT WINAPI sys_trigger_factory_Create( void *iface, UINT32 trigger_type, boolean one_shot, void **trigger )
{
    struct bg_trigger_obj *obj;
    if (!trigger) return E_POINTER;
    if (!(obj = calloc( 1, sizeof(*obj) ))) return E_OUTOFMEMORY;
    obj->vtbl = sys_trigger_vtbl;
    obj->ref = 1;
    obj->param = trigger_type;
    obj->one_shot = one_shot;
    *trigger = obj;
    return S_OK;
}

static const void *sys_trigger_factory_vtbl[];
static struct { const void **vtbl; } sys_trigger_factory_obj = { sys_trigger_factory_vtbl };

static HRESULT WINAPI sys_trigger_factory_QueryInterface( void *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IReferenceTracker ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    if (IsEqualGUID( iid, &IID_ISystemTriggerFactory_local ))
    {
        *out = &sys_trigger_factory_obj;
        return S_OK;
    }
    *out = &sys_trigger_factory_obj;
    return S_OK;
}

static const void *sys_trigger_factory_vtbl[] =
{
    sys_trigger_factory_QueryInterface,
    push_notification_factory_AddRef,
    push_notification_factory_Release,
    push_notification_factory_GetIids,
    push_notification_factory_GetRuntimeClassName,
    push_notification_factory_GetTrustLevel,
    sys_trigger_factory_Create,
};

static HRESULT WINAPI sys_condition_factory_Create( void *iface, UINT32 condition_type, void **condition )
{
    struct bg_trigger_obj *obj;
    if (!condition) return E_POINTER;
    if (!(obj = calloc( 1, sizeof(*obj) ))) return E_OUTOFMEMORY;
    obj->vtbl = sys_trigger_vtbl;
    obj->ref = 1;
    obj->param = condition_type;
    *condition = obj;
    return S_OK;
}

static const void *sys_condition_factory_vtbl[];
static struct { const void **vtbl; } sys_condition_factory_obj = { sys_condition_factory_vtbl };

static HRESULT WINAPI sys_condition_factory_QueryInterface( void *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IReferenceTracker ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    *out = &sys_condition_factory_obj;
    return S_OK;
}

static const void *sys_condition_factory_vtbl[] =
{
    sys_condition_factory_QueryInterface,
    push_notification_factory_AddRef,
    push_notification_factory_Release,
    push_notification_factory_GetIids,
    push_notification_factory_GetRuntimeClassName,
    push_notification_factory_GetTrustLevel,
    sys_condition_factory_Create,
};

/* Direct PRI (resources.pri) support for ResourceLoader, sharing the mrm_pri2
 * reader from dlls/windows.applicationmodel.resources.core/pri.c. One
 * process-wide index, loaded lazily on the first GetString/GetStringForUri
 * call from the app's resources.pri. */
static pri_index *cached_pri_index;
static INIT_ONCE pri_init_once = INIT_ONCE_STATIC_INIT;

/* Install directory of the running package (Package.Current.InstalledLocation):
 * WINE_PACKAGE_PATH env first, then the AppModel registration
 * (Exes\<exe> -> PackageFullName -> Packages\<name>\PackagePath /
 * PackageRootPath), same resolution order as windows.applicationmodel.
 * Leaves buf empty when the process carries no package identity. */
static void package_installed_location( WCHAR *buf, DWORD cch )
{
    static const HKEY roots[] = { HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE };
    static const WCHAR appmodel_key[] = L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppModel";
    WCHAR subkey[576], name[MAX_PATH], exe[MAX_PATH];
    SIZE_T i;

    buf[0] = 0;
    if (GetEnvironmentVariableW( L"WINE_PACKAGE_PATH", buf, cch ) && buf[0]) return;

    if (!GetEnvironmentVariableW( L"WINE_PACKAGE_FULL_NAME", name, ARRAY_SIZE( name ) ))
    {
        WCHAR *slash, exename[MAX_PATH];

        name[0] = 0;
        if (!GetModuleFileNameW( NULL, exe, ARRAY_SIZE( exe ) )) return;
        if (!(slash = wcsrchr( exe, '\\' ))) return;
        lstrcpyW( exename, slash + 1 );
        lstrcpyW( subkey, appmodel_key );
        lstrcatW( subkey, L"\\Exes\\" );
        lstrcatW( subkey, exename );
        for (i = 0; i < ARRAY_SIZE( roots ); i++)
        {
            HKEY key;
            DWORD type, size = sizeof(name);
            if (RegOpenKeyExW( roots[i], subkey, 0, KEY_READ, &key )) continue;
            if (!RegQueryValueExW( key, L"PackageFullName", 0, &type, (BYTE *)name, &size )
                    && type == REG_SZ && size)
            {
                RegCloseKey( key );
                break;
            }
            RegCloseKey( key );
        }
    }
    if (!name[0]) return;

    lstrcpyW( subkey, appmodel_key );
    lstrcatW( subkey, L"\\Packages\\" );
    lstrcatW( subkey, name );
    for (i = 0; i < ARRAY_SIZE( roots ); i++)
    {
        static const WCHAR *const values[] = { L"PackagePath", L"PackageRootPath" };
        HKEY key;
        SIZE_T j;

        if (RegOpenKeyExW( roots[i], subkey, 0, KEY_READ, &key )) continue;
        for (j = 0; j < ARRAY_SIZE( values ); j++)
        {
            DWORD type, size = cch * sizeof(*buf);
            if (!RegQueryValueExW( key, values[j], 0, &type, (BYTE *)buf, &size ) && type == REG_SZ && size)
            {
                RegCloseKey( key );
                return;
            }
        }
        RegCloseKey( key );
    }
    buf[0] = 0;
}

static WCHAR resource_loader_scope[256];    /* normalized loader scope, '\' separators */
static WCHAR resource_loader_primary[256];  /* part before '/': secondary .pri name */
static pri_index *scope_pri_index;          /* resources of the secondary .pri, if any */

static BOOL CALLBACK init_cached_pri( INIT_ONCE *once, void *param, void **context )
{
    static const WCHAR pri_name[] = L"\\resources.pri";
    WCHAR dir[2048], path[4096];
    DWORD len;

    /* 1) <exe dir>\resources.pri, 2) <exe dir>\..\resources.pri */
    len = GetModuleFileNameW( NULL, dir, ARRAY_SIZE( dir ) );
    if (len && len < ARRAY_SIZE( dir ) && wcsrchr( dir, '\\' ))
    {
        *wcsrchr( dir, '\\' ) = 0;
        lstrcpyW( path, dir );
        lstrcatW( path, pri_name );
        if ((cached_pri_index = pri_load( path ))) return TRUE;

        lstrcatW( dir, L"\\.." );
        lstrcpyW( path, dir );
        lstrcatW( path, pri_name );
        if ((cached_pri_index = pri_load( path ))) return TRUE;
    }

    /* 3) <Package.Current.InstalledLocation>\resources.pri */
    package_installed_location( dir, ARRAY_SIZE( dir ) );
    if (dir[0])
    {
        lstrcpyW( path, dir );
        lstrcatW( path, pri_name );
        if ((cached_pri_index = pri_load( path ))) return TRUE;
    }

    TRACE( "no resources.pri found, falling back to the built-in overrides\n" );
    return TRUE;
}

/* Load the secondary .pri file named by the primary part of a
 * "Primary/ResourceName" loader name (e.g. "Microsoft.UI.Xaml/Assets",
 * "WhatsApp.WebView2.LocalizationResources/AppResources"): <Primary>.pri
 * sits next to the package's resources.pri, in the exe dir / installed
 * location. */
static pri_index *load_named_pri( const WCHAR *primary )
{
    static const WCHAR pri_ext[] = L".pri";
    WCHAR dir[2048], path[4096];
    pri_index *idx;
    DWORD len;

    len = GetModuleFileNameW( NULL, dir, ARRAY_SIZE( dir ) );
    if (len && len < ARRAY_SIZE( dir ) && wcsrchr( dir, '\\' ))
    {
        *wcsrchr( dir, '\\' ) = 0;
        lstrcpyW( path, dir );
        lstrcatW( path, L"\\" );
        lstrcatW( path, primary );
        lstrcatW( path, pri_ext );
        if ((idx = pri_load( path ))) return idx;
    }

    package_installed_location( dir, ARRAY_SIZE( dir ) );
    if (dir[0])
    {
        lstrcpyW( path, dir );
        lstrcatW( path, L"\\" );
        lstrcatW( path, primary );
        lstrcatW( path, pri_ext );
        return pri_load( path );
    }
    return NULL;
}

/* Store the loader scope from a factory entry point name.  WinRT spells the
 * scope with '/' separators ("Primary/ResourceName"); PRI full names join
 * their tree with '\', so normalize the separators.  When the name carries a
 * primary part, load the matching secondary .pri: its resources (top scope
 * "ResourceName") are not in the package resources.pri. */
static void set_resource_loader_scope( const WCHAR *raw )
{
    WCHAR norm[256], *p;
    const WCHAR *slash;

    if (!raw || !raw[0]) return;
    lstrcpynW( norm, raw, ARRAY_SIZE( norm ) );
    for (p = norm; *p; p++) if (*p == L'/') *p = L'\\';
    if (!lstrcmpW( norm, resource_loader_scope )) return;
    lstrcpynW( resource_loader_scope, norm, ARRAY_SIZE( resource_loader_scope ) );
    pri_release( scope_pri_index );
    scope_pri_index = NULL;
    resource_loader_primary[0] = 0;
    if ((slash = wcschr( norm, L'\\' )) && slash != norm)
    {
        lstrcpynW( resource_loader_primary, norm, (INT)(slash - norm) + 1 );
        scope_pri_index = load_named_pri( resource_loader_primary );
        ERR( "TWINAPI: loader scope %s, primary %s, secondary pri %s\n", debugstr_w( resource_loader_scope ),
             debugstr_w( resource_loader_primary ), scope_pri_index ? "loaded" : "missing" );
    }
}

/* Resolve a resource key against the cached index.  A bare key is tried in
 * the main map, then under the default "Resources" scope, any named loader
 * scope, top-level PRI scopes, and the "Files" scope (ms-resource file URIs);
 * the scope prefix is skipped when the key already carries one.  A named
 * loader scope with a secondary .pri ("Primary/ResourceName") searches that
 * file first.  On success *value holds the string. */
static HRESULT pri_lookup_key( const WCHAR *key, HSTRING *value )
{
    static const WCHAR *const scopes[] = { L"", L"Resources\\", L"Files\\" };
    WCHAR full[1024], namebuf[1024], top[256];
    WCHAR *val = NULL;
    UINT32 t, top_count;
    SIZE_T i;

    if (!cached_pri_index || !key[0]) return E_FAIL;
    if (resource_loader_scope[0])
    {
        /* Split-package pri files key candidates under the resource-map name
         * alone ("AppResources\\Key"), the primary package name is only the
         * .pri file selector.  Try <map>\<key> first, then the raw scope. */
        const WCHAR *map = resource_loader_scope;
        WCHAR *slash = wcschr( map, L'\\' );
        if (slash) map = slash + 1;
        lstrcpyW( full, map );
        lstrcatW( full, L"\\" );
        lstrcatW( full, key );
        if (scope_pri_index
            && pri_lookup( scope_pri_index, full, NULL, 0, namebuf, ARRAY_SIZE( namebuf ), &val, NULL, NULL ))
        {
            HRESULT hr = WindowsCreateString( val, (UINT32)lstrlenW( val ), value );
            pri_free_result( val, NULL );
            return hr;
        }
        if (pri_lookup( cached_pri_index, full, NULL, 0, namebuf, ARRAY_SIZE( namebuf ), &val, NULL, NULL ))
        {
            HRESULT hr = WindowsCreateString( val, (UINT32)lstrlenW( val ), value );
            pri_free_result( val, NULL );
            return hr;
        }
        lstrcpyW( full, resource_loader_scope );
        lstrcatW( full, L"\\" );
        lstrcatW( full, key );
        if (scope_pri_index
            && pri_lookup( scope_pri_index, full, NULL, 0, namebuf, ARRAY_SIZE( namebuf ), &val, NULL, NULL ))
        {
            HRESULT hr = WindowsCreateString( val, (UINT32)lstrlenW( val ), value );
            pri_free_result( val, NULL );
            return hr;
        }
        if (pri_lookup( cached_pri_index, full, NULL, 0, namebuf, ARRAY_SIZE( namebuf ), &val, NULL, NULL ))
        {
            HRESULT hr = WindowsCreateString( val, (UINT32)lstrlenW( val ), value );
            pri_free_result( val, NULL );
            return hr;
        }
    }
    for (i = 0; i < ARRAY_SIZE( scopes ); i++)
    {
        lstrcpyW( full, scopes[i] );
        lstrcatW( full, key );
        if (pri_lookup( cached_pri_index, full, NULL, 0, namebuf, ARRAY_SIZE( namebuf ), &val, NULL, NULL ))
        {
            HRESULT hr = WindowsCreateString( val, (UINT32)lstrlenW( val ), value );
            pri_free_result( val, NULL );
            return hr;
        }
    }
    top_count = pri_top_count( cached_pri_index );
    for (t = 0; t < top_count; t++)
    {
        if (!pri_top_name( cached_pri_index, t, top, ARRAY_SIZE( top ) ) || !top[0]) continue;
        lstrcpyW( full, top );
        lstrcatW( full, L"\\" );
        lstrcatW( full, key );
        if (pri_lookup( cached_pri_index, full, NULL, 0, namebuf, ARRAY_SIZE( namebuf ), &val, NULL, NULL ))
        {
            HRESULT hr = WindowsCreateString( val, (UINT32)lstrlenW( val ), value );
            pri_free_result( val, NULL );
            return hr;
        }
    }
    return E_FAIL;
}

/* Windows.ApplicationModel.Resources.ResourceLoader */
static const GUID IID_IResourceLoader = {0x08524908, 0x16ef, 0x45ad, {0xa6, 0x02, 0x29, 0x36, 0x37, 0xd7, 0xe6, 0x1a}};
static const GUID IID_IResourceLoader2 = {0x10eb6ec6, 0x8138, 0x48c1, {0xbc, 0x65, 0xe1, 0xf1, 0x42, 0x07, 0x36, 0x7c}};
static const GUID IID_IResourceLoaderFactory = {0xc33a3603, 0x69dc, 0x4285, {0xa0, 0x77, 0xd5, 0xc0, 0xe4, 0x7c, 0xcb, 0xe8}};
static const GUID IID_IResourceLoaderStatics = {0xbf777ce1, 0x19c8, 0x49c2, {0x95, 0x3c, 0x47, 0xe9, 0x22, 0x7b, 0x33, 0x4e}};
static const GUID IID_IResourceLoaderStatics2 = {0x0cc04141, 0x6466, 0x4989, {0x94, 0x94, 0x0b, 0x82, 0xdf, 0xc5, 0x3f, 0x1f}};
static const GUID IID_IResourceLoaderStatics2_old = {0x0cc6354d, 0x2a52, 0x4d2c, {0xae, 0x8c, 0xe5, 0x4b, 0x67, 0x95, 0xf7, 0x0b}};
static const GUID IID_IResourceLoaderStatics3 = {0x64609dfb, 0x64ac, 0x491b, {0x81, 0x00, 0x0e, 0x55, 0x8d, 0x61, 0xc1, 0xd0}};

static HRESULT WINAPI resource_loader_GetIids( void *iface, ULONG *count, IID **iids );

/* Windows.Globalization.Language, the value type ResourceLoader.Culture
 * returns.  ILanguage ({9b708f8b-ff1c-4b74-a3f5-4c0b34813eed}) getters follow
 * the SDK declaration order: LanguageTag (get/put), Current, LayoutDirection.
 * The culture never changes here, so one agile singleton answers every
 * ResourceLoader in the process. */
static const GUID IID_ILanguage = {0x9b708f8b, 0xff1c, 0x4b74, {0xa3, 0xf5, 0x4c, 0x0b, 0x34, 0x81, 0x3e, 0xed}};

static const void *language_vtbl[];

static struct
{
    const void *vtbl;
    LONG ref;
} language_obj = { language_vtbl, 1 };

static HRESULT WINAPI language_QueryInterface( void *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_ILanguage ))
    {
        *out = &language_obj.vtbl;
        InterlockedIncrement( &language_obj.ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static HRESULT WINAPI language_GetRuntimeClassName( void *iface, HSTRING *name )
{
    if (!name) return E_POINTER;
    return WindowsCreateString( L"Windows.Globalization.Language", 31, name );
}

static HRESULT WINAPI language_GetIids( void *iface, ULONG *count, IID **iids )
{
    if (!count || !iids) return E_POINTER;
    if (!(*iids = CoTaskMemAlloc( 2 * sizeof(**iids) ))) return E_OUTOFMEMORY;
    (*iids)[0] = IID_IInspectable;
    (*iids)[1] = IID_ILanguage;
    *count = 2;
    return S_OK;
}

static HRESULT WINAPI language_get_LanguageTag( void *iface, HSTRING *value )
{
    if (!value) return E_POINTER;
    return WindowsCreateString( L"en-US", 5, value );
}

static HRESULT WINAPI language_put_LanguageTag( void *iface, HSTRING value )
{
    return S_OK;
}

static HRESULT WINAPI language_get_Current( void *iface, void *languages, HSTRING *value )
{
    return language_get_LanguageTag( iface, value );
}

static HRESULT WINAPI language_get_LayoutDirection( void *iface, INT32 *value )
{
    /* LanguageLayoutDirection_LeftToRight */
    if (!value) return E_POINTER;
    *value = 0;
    return S_OK;
}

static const void *language_vtbl[] =
{
    language_QueryInterface,
    dummy_async_AddRef,
    dummy_async_Release,
    language_GetIids,
    language_GetRuntimeClassName,
    dummy_async_GetTrustLevel,
    language_get_LanguageTag,
    language_put_LanguageTag,
    language_get_Current,
    language_get_LayoutDirection,
};

struct resource_loader_impl
{
    const void *vtbl_loader;
    const void *vtbl_loader2;
};

static const void *resource_loader_vtbl[];
static const void *resource_loader2_vtbl[];
static struct resource_loader_impl resource_loader_obj =
{
    resource_loader_vtbl,
    resource_loader2_vtbl
};

static HRESULT WINAPI resource_loader_QueryInterface( void *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IResourceLoader ))
    {
        *out = &resource_loader_obj.vtbl_loader;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IResourceLoader2 ))
    {
        *out = &resource_loader_obj.vtbl_loader2;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static HRESULT WINAPI resource_loader2_QueryInterface( void *iface, REFIID iid, void **out )
{
    return resource_loader_QueryInterface( iface, iid, out );
}

static HRESULT WINAPI resource_loader_GetIids( void *iface, ULONG *count, IID **iids )
{
    static const IID *iids_arr[] = { &IID_IInspectable, &IID_IResourceLoader, &IID_IResourceLoader2 };
    IID *iid;
    SIZE_T i;

    if (!count || !iids) return E_POINTER;
    if (!(iid = CoTaskMemAlloc( ARRAY_SIZE( iids_arr ) * sizeof(*iid) ))) return E_OUTOFMEMORY;
    for (i = 0; i < ARRAY_SIZE( iids_arr ); i++)
        iid[i] = *iids_arr[i];
    *count = ARRAY_SIZE( iids_arr );
    *iids = iid;
    return S_OK;
}

static HRESULT WINAPI resource_loader_GetRuntimeClassName( void *iface, HSTRING *name )
{
    if (!name) return E_POINTER;
    return WindowsCreateString( L"Windows.ApplicationModel.Resources.ResourceLoader", 47, name );
}

static HRESULT WINAPI resource_loader_GetString( void *iface, HSTRING resource, HSTRING *value )
{
    const WCHAR *str = WindowsGetStringRawBuffer( resource, NULL );

    TRACE( "resource_loader_GetString resource %s\n", debugstr_hstring( resource ) );
    if (!value) return E_POINTER;
    if (!str) return E_POINTER;

    InitOnceExecuteOnce( &pri_init_once, init_cached_pri, NULL, NULL );
    if (SUCCEEDED( pri_lookup_key( str, value ) )) return S_OK;

    /* built-in overrides, kept for keys the app's resources.pri does not carry */
    if (!wcscmp( str, L"LocalizedFontFamilyOverride" ))
        return WindowsCreateString( L"RESERVED_FOR_FONTLOC", 20, value );
    if (!wcscmp( str, L"LocalizedFontWeightOverride" ))
        return WindowsCreateString( L"normal", 6, value );
    if (!wcscmp( str, L"LocalizedUICaptionFontSizeFactorOverride" ))
        return WindowsCreateString( L"1.0", 3, value );
    if (!wcscmp( str, L"LocalizedUITextFontSizeFactorOverride" ))
        return WindowsCreateString( L"1.0", 3, value );

    /* documented stub tier: key as value, so lookups never fail */
    WARN( "resources.pri lookup failed for %s, using key as value\n", debugstr_w( str ) );
    return WindowsDuplicateString( resource, value );
}

static HRESULT WINAPI resource_loader_GetStringForUri( void *iface, void *uri, HSTRING *value )
{
    static const WCHAR ms_resource[] = L"ms-resource:";
    HRESULT (STDMETHODCALLTYPE *get_RawUri)( void *, HSTRING * );
    const WCHAR *raw;
    HSTRING hstr = NULL;
    WCHAR key[1024];
    SIZE_T i;

    TRACE( "resource_loader_GetStringForUri uri %p\n", uri );
    if (!value) return E_POINTER;
    if (!uri) return E_POINTER;

    InitOnceExecuteOnce( &pri_init_once, init_cached_pri, NULL, NULL );

    /* IUriRuntimeClass::get_RawUri: 15 URI properties after the 5 IInspectable
     * slots, RawUri last -> vtable slot 20.  The app hands us a
     * Windows.Foundation.Uri; its RawUri is the string it was built from. */
    get_RawUri = (void *)((void **)(*(void **)uri))[20];
    if (FAILED( get_RawUri( uri, &hstr ) ) || !hstr)
        return WindowsCreateString( L"", 0, value );

    raw = WindowsGetStringRawBuffer( hstr, NULL );
    if (!raw)
    {
        WindowsDeleteString( hstr );
        return WindowsCreateString( L"", 0, value );
    }
    /* strip "ms-resource:" plus the optional "//" authority and leading '/' */
    if (!wcsncmp( raw, ms_resource, ARRAY_SIZE( ms_resource ) - 1 ))
    {
        raw += ARRAY_SIZE( ms_resource ) - 1;
        if (raw[0] == '/' && raw[1] == '/') raw += 2;
        if (*raw == '/') raw++;
    }

    for (i = 0; *raw && i < ARRAY_SIZE( key ) - 1; raw++, i++)
        key[i] = *raw == '/' ? '\\' : *raw;
    key[i] = 0;
    WindowsDeleteString( hstr );

    if (SUCCEEDED( pri_lookup_key( key, value ) )) return S_OK;
    return WindowsCreateString( L"", 0, value );
}

/* ResourceLoader.Culture: the culture never varies per loader here, so the
 * shared Language above answers for every one of them. */
static HRESULT WINAPI resource_loader_get_Culture( void *iface, void **value )
{
    if (!value) return E_POINTER;
    *value = &language_obj.vtbl;
    InterlockedIncrement( &language_obj.ref );
    return S_OK;
}

static HRESULT WINAPI resource_loader_put_Culture( void *iface, void *value )
{
    return S_OK;
}

/* ResourceLoader.PriName: the resource scope named by the factory entry point
 * that created this loader (GetForCurrentViewWithName, the ResourceLoader
 * constructor, ...), empty when the loader is the package's default one. */
static HRESULT WINAPI resource_loader_get_PriName( void *iface, HSTRING *value )
{
    const WCHAR *name = resource_loader_primary[0] ? resource_loader_primary : resource_loader_scope;
    if (!value) return E_POINTER;
    return WindowsCreateString( name, (UINT32)lstrlenW( name ), value );
}

/* IResourceLoader: GetString, GetStringForUri, get_Culture. */
static const void *resource_loader_vtbl[] =
{
    resource_loader_QueryInterface,
    dummy_async_AddRef,
    dummy_async_Release,
    resource_loader_GetIids,
    resource_loader_GetRuntimeClassName,
    dummy_async_GetTrustLevel,
    resource_loader_GetString,
    resource_loader_GetStringForUri,
    resource_loader_get_Culture,
};

/* IResourceLoader2 derives from IInspectable, not IResourceLoader: get_Culture,
 * put_Culture, get_PriName. */
static const void *resource_loader2_vtbl[] =
{
    resource_loader2_QueryInterface,
    dummy_async_AddRef,
    dummy_async_Release,
    resource_loader_GetIids,
    resource_loader_GetRuntimeClassName,
    dummy_async_GetTrustLevel,
    resource_loader_get_Culture,
    resource_loader_put_Culture,
    resource_loader_get_PriName,
};

struct resource_loader_factory_impl
{
    const void *vtbl_activation;
    const void *vtbl_factory;
    const void *vtbl_statics;
    const void *vtbl_statics2;
    const void *vtbl_statics3;
};

static const void *resource_loader_activation_vtbl[];
static const void *resource_loader_factory_vtbl[];
static const void *resource_loader_statics_vtbl[];
static const void *resource_loader_statics2_vtbl[];
static const void *resource_loader_statics3_vtbl[];

static struct resource_loader_factory_impl resource_loader_factory_obj =
{
    resource_loader_activation_vtbl,
    resource_loader_factory_vtbl,
    resource_loader_statics_vtbl,
    resource_loader_statics2_vtbl,
    resource_loader_statics3_vtbl
};

static HRESULT WINAPI resource_loader_factory_QueryInterface( void *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = &resource_loader_factory_obj.vtbl_activation;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IResourceLoaderFactory ))
    {
        *out = &resource_loader_factory_obj.vtbl_factory;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IResourceLoaderStatics ))
    {
        *out = &resource_loader_factory_obj.vtbl_statics;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IResourceLoaderStatics2 ) ||
        IsEqualGUID( iid, &IID_IResourceLoaderStatics2_old ))
    {
        *out = &resource_loader_factory_obj.vtbl_statics2;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IResourceLoaderStatics3 ))
    {
        *out = &resource_loader_factory_obj.vtbl_statics3;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static HRESULT WINAPI resource_loader_factory_ActivateInstance( void *iface, void **instance )
{
    if (!instance) return E_POINTER;
    *instance = &resource_loader_obj.vtbl_loader;
    return S_OK;
}

static HRESULT WINAPI resource_loader_factory_CreateResourceLoaderByName( void *iface, HSTRING name, void **loader )
{
    const WCHAR *raw = name ? WindowsGetStringRawBuffer( name, NULL ) : NULL;
    ERR( "TWINAPI: CreateResourceLoaderByName name %s\n", debugstr_hstring(name) );
    if (!loader) return E_POINTER;
    set_resource_loader_scope( raw );
    *loader = &resource_loader_obj.vtbl_loader;
    return S_OK;
}

static HRESULT WINAPI resource_loader_statics_GetStringForReference( void *iface, void *uri, HSTRING *value )
{
    ERR( "TWINAPI: GetStringForReference uri %p\n", uri );
    if (!value) return E_POINTER;
    return WindowsCreateString( L"", 0, value );
}

static HRESULT WINAPI resource_loader_statics2_GetForCurrentView( void *iface, void **loader )
{
    ERR( "TWINAPI: GetForCurrentView\n" );
    if (!loader) return E_POINTER;
    *loader = &resource_loader_obj.vtbl_loader;
    return S_OK;
}
static HRESULT WINAPI resource_loader_statics2_GetForCurrentViewWithName( void *iface, HSTRING name, void **loader )
{
    const WCHAR *raw = name ? WindowsGetStringRawBuffer( name, NULL ) : NULL;
    ERR( "TWINAPI: GetForCurrentViewWithName name %s\n", debugstr_hstring(name) );
    if (!loader) return E_POINTER;
    set_resource_loader_scope( raw );
    *loader = &resource_loader_obj.vtbl_loader;
    return S_OK;
}
static HRESULT WINAPI resource_loader_statics2_GetForViewIndependentUse( void *iface, void **loader )
{
    ERR( "TWINAPI: GetForViewIndependentUse\n" );
    if (!loader) return E_POINTER;
    *loader = &resource_loader_obj.vtbl_loader;
    return S_OK;
}
static HRESULT WINAPI resource_loader_statics2_GetForViewIndependentUseWithName( void *iface, HSTRING name, void **loader )
{
    const WCHAR *raw = name ? WindowsGetStringRawBuffer( name, NULL ) : NULL;
    ERR( "TWINAPI: GetForViewIndependentUseWithName name %s\n", debugstr_hstring(name) );
    if (!loader) return E_POINTER;
    set_resource_loader_scope( raw );
    *loader = &resource_loader_obj.vtbl_loader;
    return S_OK;
}

static HRESULT WINAPI resource_loader_statics3_GetForUIContext( void *iface, void *context, void **loader )
{
    ERR( "TWINAPI: GetForUIContext context %p\n", context );
    if (!loader) return E_POINTER;
    *loader = &resource_loader_obj.vtbl_loader;
    return S_OK;
}

static const void *resource_loader_activation_vtbl[] =
{
    resource_loader_factory_QueryInterface,
    push_notification_factory_AddRef,
    push_notification_factory_Release,
    resource_loader_GetIids,
    resource_loader_GetRuntimeClassName,
    push_notification_factory_GetTrustLevel,
    resource_loader_factory_ActivateInstance,
};

static const void *resource_loader_factory_vtbl[] =
{
    resource_loader_factory_QueryInterface,
    push_notification_factory_AddRef,
    push_notification_factory_Release,
    resource_loader_GetIids,
    resource_loader_GetRuntimeClassName,
    push_notification_factory_GetTrustLevel,
    resource_loader_factory_CreateResourceLoaderByName,
};

static const void *resource_loader_statics_vtbl[] =
{
    resource_loader_factory_QueryInterface,
    push_notification_factory_AddRef,
    push_notification_factory_Release,
    resource_loader_GetIids,
    resource_loader_GetRuntimeClassName,
    push_notification_factory_GetTrustLevel,
    resource_loader_GetString,
    resource_loader_statics2_GetForCurrentView,
    resource_loader_statics_GetStringForReference,
};

static const void *resource_loader_statics2_vtbl[] =
{
    resource_loader_factory_QueryInterface,
    push_notification_factory_AddRef,
    push_notification_factory_Release,
    resource_loader_GetIids,
    resource_loader_GetRuntimeClassName,
    push_notification_factory_GetTrustLevel,
    resource_loader_statics2_GetForCurrentView,
    resource_loader_statics2_GetForCurrentViewWithName,
    resource_loader_statics2_GetForViewIndependentUse,
    resource_loader_statics2_GetForViewIndependentUseWithName,
};

static const void *resource_loader_statics3_vtbl[] =
{
    resource_loader_factory_QueryInterface,
    push_notification_factory_AddRef,
    push_notification_factory_Release,
    resource_loader_GetIids,
    resource_loader_GetRuntimeClassName,
    push_notification_factory_GetTrustLevel,
    resource_loader_statics3_GetForUIContext,
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
    else if (!wcscmp( buffer, RuntimeClass_Windows_System_Power_PowerManager ))
        IActivationFactory_QueryInterface( power_manager_statics_factory, &IID_IActivationFactory, (void **)factory );
    else if (!wcscmp( buffer, RuntimeClass_Windows_System_UserProfile_AdvertisingManager ))
        IActivationFactory_QueryInterface( advertising_manager_factory, &IID_IActivationFactory, (void **)factory );
    else if (!wcscmp( buffer, RuntimeClass_Windows_UI_ViewManagement_ApplicationView ))
        IActivationFactory_QueryInterface( application_view_factory, &IID_IActivationFactory, (void **)factory );
    else if (!wcscmp( buffer, RuntimeClass_Windows_ApplicationModel_Core_CoreApplication ))
        IActivationFactory_QueryInterface( core_application_factory, &IID_IActivationFactory, (void **)factory );
    else if (!wcscmp( buffer, L"Windows.ApplicationModel.AppInstance" ) ||
             !wcscmp( buffer, L"Microsoft.Windows.AppLifecycle.AppInstance" ))
        IActivationFactory_QueryInterface( app_instance_factory, &IID_IActivationFactory, (void **)factory );
    else if (!wcscmp( buffer, RuntimeClass_Windows_UI_Notifications_BadgeUpdateManager ))
        IActivationFactory_QueryInterface( badge_update_manager_statics_factory, &IID_IActivationFactory, (void **)factory );
    else if (!wcscmp( buffer, RuntimeClass_Windows_UI_Notifications_ToastNotificationManager ))
        IActivationFactory_QueryInterface( toast_notification_manager_statics_factory, &IID_IActivationFactory, (void **)factory );
    else if (!wcscmp( buffer, RuntimeClass_Windows_UI_Notifications_ToastNotification ))
        IActivationFactory_QueryInterface( toast_notification_factory, &IID_IActivationFactory, (void **)factory );
    else if (!wcscmp( buffer, L"Microsoft.Windows.AppNotifications.AppNotificationManager" ))
        IActivationFactory_QueryInterface( app_notification_manager_statics_factory, &IID_IActivationFactory, (void **)factory );
    else if (!wcscmp( buffer, L"Microsoft.Windows.AppNotifications.AppNotification" ))
        IActivationFactory_QueryInterface( app_notification_factory, &IID_IActivationFactory, (void **)factory );
    else if (!wcscmp( buffer, L"Microsoft.Windows.AppNotifications.AppNotificationActivatedEventArgs" ))
        IActivationFactory_QueryInterface( app_notification_activated_args_factory, &IID_IActivationFactory, (void **)factory );
    else if (!wcscmp( buffer, RuntimeClass_Windows_ApplicationModel_DataTransfer_DataTransferManager ))
        IActivationFactory_QueryInterface( data_transfer_manager_statics_factory, &IID_IActivationFactory, (void **)factory );
    else if (!wcscmp( buffer, RuntimeClass_Windows_ApplicationModel_DataTransfer_Clipboard ))
        IActivationFactory_QueryInterface( clipboard_statics_factory, &IID_IActivationFactory, (void **)factory );
    else if (!wcscmp( buffer, RuntimeClass_Windows_ApplicationModel_DataTransfer_DataPackage ))
        IActivationFactory_QueryInterface( data_package_factory, &IID_IActivationFactory, (void **)factory );
    else if (!wcscmp( buffer, RuntimeClass_Windows_ApplicationModel_DataTransfer_HtmlFormatHelper ))
        IActivationFactory_QueryInterface( html_format_helper_statics_factory, &IID_IActivationFactory, (void **)factory );
    else if (!wcscmp( buffer, RuntimeClass_Windows_ApplicationModel_DataTransfer_StandardDataFormats ))
        IActivationFactory_QueryInterface( standard_data_formats_statics_factory, &IID_IActivationFactory, (void **)factory );
    else if (!wcscmp( buffer, L"Windows.Networking.PushNotifications.PushNotificationChannelManager" ))
        *factory = (IActivationFactory *)&push_notification_obj;
    else if (!wcscmp( buffer, L"Windows.ApplicationModel.Background.BackgroundExecutionManager" ))
        *factory = (IActivationFactory *)&bg_manager_obj;
    else if (!wcscmp( buffer, L"Windows.ApplicationModel.Background.BackgroundTaskRegistration" ))
        *factory = (IActivationFactory *)&bg_task_obj;
    else if (!wcscmp( buffer, L"Windows.ApplicationModel.Background.BackgroundTaskBuilder" ))
        *factory = (IActivationFactory *)&bg_builder_factory_obj;
    else if (!wcscmp( buffer, L"Windows.ApplicationModel.Background.PushNotificationTrigger" ))
        *factory = (IActivationFactory *)&push_trigger_factory_obj;
    else if (!wcscmp( buffer, L"Windows.ApplicationModel.Background.TimeTrigger" ))
        *factory = (IActivationFactory *)&time_trigger_factory_obj;
    else if (!wcscmp( buffer, L"Windows.ApplicationModel.Background.SystemTrigger" ))
        *factory = (IActivationFactory *)&sys_trigger_factory_obj;
    else if (!wcscmp( buffer, L"Windows.ApplicationModel.Background.SystemCondition" ))
        *factory = (IActivationFactory *)&sys_condition_factory_obj;
    else if (!wcscmp( buffer, L"Windows.ApplicationModel.StartupTask" ))
        *factory = (IActivationFactory *)&startup_task_factory_obj;
    else if (!wcscmp( buffer, L"Windows.ApplicationModel.Resources.ResourceLoader" ))
        *factory = (IActivationFactory *)&resource_loader_factory_obj.vtbl_activation;
    else if (!wcscmp( buffer, L"Windows.System.Profile.HardwareIdentification" ))
        *factory = (IActivationFactory *)&hw_id_obj.vtbl_factory;

    if (*factory) return S_OK;
    return E_NOINTERFACE;
}
