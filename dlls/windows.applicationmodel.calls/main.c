/* WinRT Windows.ApplicationModel.Calls Implementation
 *
 * Copyright (C) 2026 Wine Developers
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
#define COBJMACROS
#include "windef.h"
#include "winbase.h"
#include "winstring.h"
#include "initguid.h"
#include "activation.h"
#include "asyncinfo.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(calls);

#ifndef AsyncStatus_Completed
#define AsyncStatus_Completed Completed
#endif

typedef struct { INT64 Duration; } TimeSpan;
typedef struct { INT64 value; } EventRegistrationToken;

static const GUID IID_IVoipCallCoordinatorStatics = {0x7f5d1f2b, 0xe04a, 0x4d10, {0xb3, 0x1a, 0xa5, 0x5c, 0x92, 0x2c, 0xc2, 0xfb}};
static const GUID IID_IVoipCallCoordinator        = {0x4f118bcf, 0xe8ef, 0x4434, {0x9c, 0x5f, 0xa8, 0xd8, 0x93, 0xfa, 0xfe, 0x79}};
static const GUID IID_IVoipCallCoordinator2       = {0xbeb4a9f3, 0xc704, 0x4234, {0x89, 0xce, 0xe8, 0x8c, 0xc0, 0xd2, 0x8f, 0xbe}};
static const GUID IID_IVoipCallCoordinator3       = {0x338d0cbf, 0x9b55, 0x4021, {0x87, 0xca, 0xe6, 0x4b, 0x9b, 0xd6, 0x66, 0xc7}};
static const GUID IID_IVoipCallCoordinator4       = {0x83737239, 0x9311, 0x468f, {0xbb, 0x49, 0x47, 0xe0, 0xdf, 0xb5, 0xd9, 0x3e}};
static const GUID IID_IVoipPhoneCall              = {0x6cf1f19a, 0x7794, 0x4a5a, {0x8c, 0x68, 0xae, 0x87, 0x94, 0x7a, 0x69, 0x90}};
static const GUID IID_IVoipPhoneCall2             = {0x741b46e1, 0x245f, 0x41f3, {0x93, 0x99, 0x31, 0x41, 0xd2, 0x5b, 0x52, 0xe3}};
static const GUID IID_IVoipPhoneCall3             = {0x0d891522, 0xe258, 0x4aa9, {0x90, 0x7a, 0x1a, 0xa4, 0x13, 0xc2, 0x55, 0x23}};
static const GUID IID_IAsyncOperation_VoipStatus  = {0x5fdc7154, 0xfb70, 0x3c6a, {0x84, 0x7b, 0xa1, 0x71, 0x77, 0xdd, 0x40, 0x9f}};

static LONG next_token = 0;

/* --- VoipPhoneCall --- */

struct voip_phone_call
{
    const void *vtbl_call;
    const void *vtbl_call2;
    const void *vtbl_call3;
    LONG ref;
    UINT32 media;
    HSTRING contact_name;
    INT64 start_time;
};

static inline struct voip_phone_call *impl_from_IVoipPhoneCall2( void *iface )
{
    return (struct voip_phone_call *)((BYTE *)iface - sizeof(void *));
}

static inline struct voip_phone_call *impl_from_IVoipPhoneCall3( void *iface )
{
    return (struct voip_phone_call *)((BYTE *)iface - 2 * sizeof(void *));
}

static HRESULT WINAPI call_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct voip_phone_call *impl = iface;

    TRACE( "iface %p, iid %s, out %p\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IVoipPhoneCall ))
    {
        *out = &impl->vtbl_call;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IVoipPhoneCall2 ))
    {
        *out = &impl->vtbl_call2;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IVoipPhoneCall3 ))
    {
        *out = &impl->vtbl_call3;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    FIXME( "call %s not implemented\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI call_AddRef( void *iface )
{
    struct voip_phone_call *impl = iface;
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI call_Release( void *iface )
{
    struct voip_phone_call *impl = iface;
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        if (impl->contact_name) WindowsDeleteString( impl->contact_name );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI call_GetIids( void *iface, ULONG *c, IID **i ) { return E_NOTIMPL; }
static HRESULT WINAPI call_GetRuntimeClassName( void *iface, HSTRING *n )
{
    return WindowsCreateString( L"Windows.ApplicationModel.Calls.VoipPhoneCall", 42, n );
}
static HRESULT WINAPI call_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }

static HRESULT WINAPI call_NotifyCallHeld( void *iface ) { TRACE( "iface %p\n", iface ); return S_OK; }
static HRESULT WINAPI call_NotifyCallActive( void *iface ) { TRACE( "iface %p\n", iface ); return S_OK; }
static HRESULT WINAPI call_NotifyCallEnded( void *iface ) { TRACE( "iface %p\n", iface ); return S_OK; }
static HRESULT WINAPI call_NotifyCallReady( void *iface ) { TRACE( "iface %p\n", iface ); return S_OK; }

static HRESULT WINAPI call_get_CallMedia( void *iface, UINT32 *value )
{
    struct voip_phone_call *impl = iface;
    if (value) *value = impl->media;
    return S_OK;
}

static HRESULT WINAPI call_put_CallMedia( void *iface, UINT32 value )
{
    struct voip_phone_call *impl = iface;
    impl->media = value;
    return S_OK;
}

static HRESULT WINAPI call_get_ContactName( void *iface, HSTRING *value )
{
    struct voip_phone_call *impl = iface;
    if (value) return WindowsDuplicateString( impl->contact_name, value );
    return S_OK;
}

static HRESULT WINAPI call_put_ContactName( void *iface, HSTRING value )
{
    struct voip_phone_call *impl = iface;
    if (impl->contact_name) WindowsDeleteString( impl->contact_name );
    return WindowsDuplicateString( value, &impl->contact_name );
}

static HRESULT WINAPI call_get_StartTime( void *iface, INT64 *value )
{
    struct voip_phone_call *impl = iface;
    if (value) *value = impl->start_time;
    return S_OK;
}

static HRESULT WINAPI call_put_StartTime( void *iface, INT64 value )
{
    struct voip_phone_call *impl = iface;
    impl->start_time = value;
    return S_OK;
}

static HRESULT WINAPI call_add_AnswerRequested( void *iface, void *handler, EventRegistrationToken *token )
{
    TRACE( "iface %p, handler %p, token %p\n", iface, handler, token );
    if (token) token->value = InterlockedIncrement( &next_token );
    return S_OK;
}

static HRESULT WINAPI call_remove_AnswerRequested( void *iface, EventRegistrationToken token )
{
    TRACE( "iface %p, token %I64d\n", iface, token.value );
    return S_OK;
}

static HRESULT WINAPI call_add_EndRequested( void *iface, void *handler, EventRegistrationToken *token )
{
    if (token) token->value = InterlockedIncrement( &next_token );
    return S_OK;
}
static HRESULT WINAPI call_remove_EndRequested( void *iface, EventRegistrationToken token ) { return S_OK; }

static HRESULT WINAPI call_add_HoldRequested( void *iface, void *handler, EventRegistrationToken *token )
{
    if (token) token->value = InterlockedIncrement( &next_token );
    return S_OK;
}
static HRESULT WINAPI call_remove_HoldRequested( void *iface, EventRegistrationToken token ) { return S_OK; }

static HRESULT WINAPI call_add_RejectRequested( void *iface, void *handler, EventRegistrationToken *token )
{
    if (token) token->value = InterlockedIncrement( &next_token );
    return S_OK;
}
static HRESULT WINAPI call_remove_RejectRequested( void *iface, EventRegistrationToken token ) { return S_OK; }

static HRESULT WINAPI call_add_ResumeRequested( void *iface, void *handler, EventRegistrationToken *token )
{
    if (token) token->value = InterlockedIncrement( &next_token );
    return S_OK;
}
static HRESULT WINAPI call_remove_ResumeRequested( void *iface, EventRegistrationToken token ) { return S_OK; }

static const void *voip_phone_call_vtbl[] =
{
    call_QueryInterface,
    call_AddRef,
    call_Release,
    call_GetIids,
    call_GetRuntimeClassName,
    call_GetTrustLevel,
    call_NotifyCallHeld,
    call_NotifyCallActive,
    call_NotifyCallEnded,
    call_NotifyCallReady,
    call_get_CallMedia,
    call_put_CallMedia,
    call_get_ContactName,
    call_put_ContactName,
    call_get_StartTime,
    call_put_StartTime,
    call_add_AnswerRequested,
    call_remove_AnswerRequested,
    call_add_EndRequested,
    call_remove_EndRequested,
    call_add_HoldRequested,
    call_remove_HoldRequested,
    call_add_RejectRequested,
    call_remove_RejectRequested,
    call_add_ResumeRequested,
    call_remove_ResumeRequested,
};

static HRESULT WINAPI call2_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct voip_phone_call *impl = impl_from_IVoipPhoneCall2( iface );
    return call_QueryInterface( impl, iid, out );
}
static ULONG WINAPI call2_AddRef( void *iface )
{
    struct voip_phone_call *impl = impl_from_IVoipPhoneCall2( iface );
    return call_AddRef( impl );
}
static ULONG WINAPI call2_Release( void *iface )
{
    struct voip_phone_call *impl = impl_from_IVoipPhoneCall2( iface );
    return call_Release( impl );
}
static HRESULT WINAPI call2_GetIids( void *iface, ULONG *c, IID **i ) { return E_NOTIMPL; }
static HRESULT WINAPI call2_GetRuntimeClassName( void *iface, HSTRING *n )
{
    return WindowsCreateString( L"Windows.ApplicationModel.Calls.VoipPhoneCall", 42, n );
}
static HRESULT WINAPI call2_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }

static HRESULT WINAPI call2_TryShowAppUI( void *iface )
{
    TRACE( "iface %p\n", iface );
    return S_OK;
}

static const void *voip_phone_call2_vtbl[] =
{
    call2_QueryInterface,
    call2_AddRef,
    call2_Release,
    call2_GetIids,
    call2_GetRuntimeClassName,
    call2_GetTrustLevel,
    call2_TryShowAppUI,
};

static HRESULT WINAPI call3_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct voip_phone_call *impl = impl_from_IVoipPhoneCall3( iface );
    return call_QueryInterface( impl, iid, out );
}
static ULONG WINAPI call3_AddRef( void *iface )
{
    struct voip_phone_call *impl = impl_from_IVoipPhoneCall3( iface );
    return call_AddRef( impl );
}
static ULONG WINAPI call3_Release( void *iface )
{
    struct voip_phone_call *impl = impl_from_IVoipPhoneCall3( iface );
    return call_Release( impl );
}
static HRESULT WINAPI call3_GetIids( void *iface, ULONG *c, IID **i ) { return E_NOTIMPL; }
static HRESULT WINAPI call3_GetRuntimeClassName( void *iface, HSTRING *n )
{
    return WindowsCreateString( L"Windows.ApplicationModel.Calls.VoipPhoneCall", 42, n );
}
static HRESULT WINAPI call3_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }

static HRESULT WINAPI call3_NotifyCallAccepted( void *iface, UINT32 media )
{
    struct voip_phone_call *impl = impl_from_IVoipPhoneCall3( iface );
    TRACE( "iface %p, media %u\n", iface, media );
    impl->media = media;
    return S_OK;
}

static const void *voip_phone_call3_vtbl[] =
{
    call3_QueryInterface,
    call3_AddRef,
    call3_Release,
    call3_GetIids,
    call3_GetRuntimeClassName,
    call3_GetTrustLevel,
    call3_NotifyCallAccepted,
};

static HRESULT create_voip_phone_call( HSTRING contactName, UINT32 media, void **out )
{
    struct voip_phone_call *impl = calloc( 1, sizeof(*impl) );
    if (!impl) return E_OUTOFMEMORY;
    impl->vtbl_call = voip_phone_call_vtbl;
    impl->vtbl_call2 = voip_phone_call2_vtbl;
    impl->vtbl_call3 = voip_phone_call3_vtbl;
    impl->ref = 1;
    impl->media = media;
    if (contactName) WindowsDuplicateString( contactName, &impl->contact_name );
    *out = impl;
    return S_OK;
}

/* --- ReserveCallResourcesAsync op --- */

struct reserve_resources_op
{
    const void *vtbl_op;
    const void *vtbl_info;
    LONG ref;
};

static HRESULT WINAPI reserve_op_QueryInterface( void *iface, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IAsyncOperation_VoipStatus ))
    {
        *out = iface;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IAsyncInfo ))
    {
        *out = (void *)((BYTE *)iface + sizeof(void *));
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI reserve_op_AddRef( void *iface ) { return 2; }
static ULONG WINAPI reserve_op_Release( void *iface ) { return 1; }
static HRESULT WINAPI reserve_op_GetIids( void *iface, ULONG *c, IID **i ) { return E_NOTIMPL; }
static HRESULT WINAPI reserve_op_GetRuntimeClassName( void *iface, HSTRING *n )
{
    return WindowsCreateString( L"Windows.Foundation.IAsyncOperation`1<Windows.ApplicationModel.Calls.VoipPhoneCallResourceReservationStatus>", 101, n );
}
static HRESULT WINAPI reserve_op_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI reserve_op_put_Completed( void *iface, void *handler )
{
    if (handler)
    {
        void **vtbl = *(void ***)handler;
        HRESULT (STDMETHODCALLTYPE *invoke)(void *, void *, UINT32) = (void *)vtbl[3];
        if (invoke) invoke( handler, iface, AsyncStatus_Completed );
    }
    return S_OK;
}
static HRESULT WINAPI reserve_op_get_Completed( void *iface, void **handler ) { if (handler) *handler = NULL; return S_OK; }
static HRESULT WINAPI reserve_op_GetResults( void *iface, UINT32 *results )
{
    if (results) *results = 0; /* VoipPhoneCallResourceReservationStatus_Success */
    return S_OK;
}

static const void *reserve_op_vtbl[] =
{
    reserve_op_QueryInterface,
    reserve_op_AddRef,
    reserve_op_Release,
    reserve_op_GetIids,
    reserve_op_GetRuntimeClassName,
    reserve_op_GetTrustLevel,
    reserve_op_put_Completed,
    reserve_op_get_Completed,
    reserve_op_GetResults,
};

static HRESULT WINAPI reserve_info_QueryInterface( void *iface, REFIID iid, void **out )
{
    void *op = (void *)((BYTE *)iface - sizeof(void *));
    return reserve_op_QueryInterface( op, iid, out );
}
static ULONG WINAPI reserve_info_AddRef( void *iface ) { return 2; }
static ULONG WINAPI reserve_info_Release( void *iface ) { return 1; }
static HRESULT WINAPI reserve_info_GetIids( void *iface, ULONG *c, IID **i ) { return E_NOTIMPL; }
static HRESULT WINAPI reserve_info_GetRuntimeClassName( void *iface, HSTRING *n )
{
    return WindowsCreateString( L"Windows.Foundation.IAsyncInfo", 29, n );
}
static HRESULT WINAPI reserve_info_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI reserve_info_get_Id( void *iface, UINT32 *id ) { if (id) *id = 1; return S_OK; }
static HRESULT WINAPI reserve_info_get_Status( void *iface, UINT32 *st ) { if (st) *st = AsyncStatus_Completed; return S_OK; }
static HRESULT WINAPI reserve_info_get_ErrorCode( void *iface, HRESULT *ec ) { if (ec) *ec = S_OK; return S_OK; }
static HRESULT WINAPI reserve_info_Cancel( void *iface ) { return S_OK; }
static HRESULT WINAPI reserve_info_Close( void *iface ) { return S_OK; }

static const void *reserve_info_vtbl[] =
{
    reserve_info_QueryInterface,
    reserve_info_AddRef,
    reserve_info_Release,
    reserve_info_GetIids,
    reserve_info_GetRuntimeClassName,
    reserve_info_GetTrustLevel,
    reserve_info_get_Id,
    reserve_info_get_Status,
    reserve_info_get_ErrorCode,
    reserve_info_Cancel,
    reserve_info_Close,
};

static struct reserve_resources_op reserve_resources_op_inst = { reserve_op_vtbl, reserve_info_vtbl, 1 };

/* --- VoipCallCoordinator --- */

struct voip_call_coordinator
{
    const void *vtbl_coord;
    const void *vtbl_coord2;
    const void *vtbl_coord3;
    const void *vtbl_coord4;
    LONG ref;
};

static inline struct voip_call_coordinator *impl_from_IVoipCallCoordinator2( void *iface )
{
    return (struct voip_call_coordinator *)((BYTE *)iface - sizeof(void *));
}
static inline struct voip_call_coordinator *impl_from_IVoipCallCoordinator3( void *iface )
{
    return (struct voip_call_coordinator *)((BYTE *)iface - 2 * sizeof(void *));
}
static inline struct voip_call_coordinator *impl_from_IVoipCallCoordinator4( void *iface )
{
    return (struct voip_call_coordinator *)((BYTE *)iface - 3 * sizeof(void *));
}

static HRESULT WINAPI coord_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct voip_call_coordinator *impl = iface;

    TRACE( "iface %p, iid %s, out %p\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IVoipCallCoordinator ))
    {
        *out = &impl->vtbl_coord;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IVoipCallCoordinator2 ))
    {
        *out = &impl->vtbl_coord2;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IVoipCallCoordinator3 ))
    {
        *out = &impl->vtbl_coord3;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IVoipCallCoordinator4 ))
    {
        *out = &impl->vtbl_coord4;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    FIXME( "coordinator %s not implemented\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI coord_AddRef( void *iface )
{
    struct voip_call_coordinator *impl = iface;
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI coord_Release( void *iface )
{
    struct voip_call_coordinator *impl = iface;
    return InterlockedDecrement( &impl->ref );
}

static HRESULT WINAPI coord_GetIids( void *iface, ULONG *c, IID **i ) { return E_NOTIMPL; }
static HRESULT WINAPI coord_GetRuntimeClassName( void *iface, HSTRING *n )
{
    return WindowsCreateString( L"Windows.ApplicationModel.Calls.VoipCallCoordinator", 48, n );
}
static HRESULT WINAPI coord_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }

static HRESULT WINAPI coord_ReserveCallResourcesAsync( void *iface, HSTRING taskEntryPoint, void **op )
{
    TRACE( "iface %p, taskEntryPoint %s, op %p\n", iface, debugstr_hstring( taskEntryPoint ), op );
    if (op) *op = &reserve_resources_op_inst;
    return S_OK;
}

static HRESULT WINAPI coord_RequestNewIncomingCall( void *iface, HSTRING context, HSTRING contactName,
                                                   HSTRING contactNumber, void *contactImage,
                                                   HSTRING serviceName, void *brandingImage,
                                                   HSTRING callDetails, void *ringtone,
                                                   UINT32 media, TimeSpan ringTimeout, void **result )
{
    TRACE( "iface %p, contactName %s, media %u, result %p\n", iface, debugstr_hstring( contactName ), media, result );
    return create_voip_phone_call( contactName, media, result );
}

static HRESULT WINAPI coord_RequestNewOutgoingCall( void *iface, HSTRING context, HSTRING contactName,
                                                   HSTRING serviceName, UINT32 media, void **result )
{
    TRACE( "iface %p, contactName %s, media %u, result %p\n", iface, debugstr_hstring( contactName ), media, result );
    return create_voip_phone_call( contactName, media, result );
}

static HRESULT WINAPI coord_NotifyMuted( void *iface ) { TRACE( "iface %p\n", iface ); return S_OK; }
static HRESULT WINAPI coord_NotifyUnmuted( void *iface ) { TRACE( "iface %p\n", iface ); return S_OK; }

static HRESULT WINAPI coord_RequestOutgoingUpgradeToVideoCall( void *iface, GUID callUpgradeGuid, HSTRING context,
                                                              HSTRING contactName, HSTRING serviceName, void **result )
{
    return create_voip_phone_call( contactName, 2 /* Video */, result );
}

static HRESULT WINAPI coord_RequestIncomingUpgradeToVideoCall( void *iface, HSTRING context, HSTRING contactName,
                                                              HSTRING contactNumber, void *contactImage,
                                                              HSTRING serviceName, void *brandingImage,
                                                              HSTRING callDetails, void *ringtone,
                                                              TimeSpan ringTimeout, void **result )
{
    return create_voip_phone_call( contactName, 2 /* Video */, result );
}

static HRESULT WINAPI coord_TerminateCellularCall( void *iface, GUID callUpgradeGuid ) { return S_OK; }
static HRESULT WINAPI coord_CancelUpgrade( void *iface, GUID callUpgradeGuid ) { return S_OK; }

static HRESULT WINAPI coord_add_MuteStateChanged( void *iface, void *handler, EventRegistrationToken *token )
{
    TRACE( "iface %p, handler %p, token %p\n", iface, handler, token );
    if (token) token->value = InterlockedIncrement( &next_token );
    return S_OK;
}

static HRESULT WINAPI coord_remove_MuteStateChanged( void *iface, EventRegistrationToken token )
{
    TRACE( "iface %p, token %I64d\n", iface, token.value );
    return S_OK;
}

static const void *voip_call_coordinator_vtbl[] =
{
    coord_QueryInterface,
    coord_AddRef,
    coord_Release,
    coord_GetIids,
    coord_GetRuntimeClassName,
    coord_GetTrustLevel,
    coord_ReserveCallResourcesAsync,
    coord_RequestNewIncomingCall,
    coord_RequestNewOutgoingCall,
    coord_NotifyMuted,
    coord_NotifyUnmuted,
    coord_RequestOutgoingUpgradeToVideoCall,
    coord_RequestIncomingUpgradeToVideoCall,
    coord_TerminateCellularCall,
    coord_CancelUpgrade,
    coord_add_MuteStateChanged,
    coord_remove_MuteStateChanged,
};

static HRESULT WINAPI coord2_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct voip_call_coordinator *impl = impl_from_IVoipCallCoordinator2( iface );
    return coord_QueryInterface( impl, iid, out );
}
static ULONG WINAPI coord2_AddRef( void *iface )
{
    struct voip_call_coordinator *impl = impl_from_IVoipCallCoordinator2( iface );
    return coord_AddRef( impl );
}
static ULONG WINAPI coord2_Release( void *iface )
{
    struct voip_call_coordinator *impl = impl_from_IVoipCallCoordinator2( iface );
    return coord_Release( impl );
}
static HRESULT WINAPI coord2_GetIids( void *iface, ULONG *c, IID **i ) { return E_NOTIMPL; }
static HRESULT WINAPI coord2_GetRuntimeClassName( void *iface, HSTRING *n )
{
    return WindowsCreateString( L"Windows.ApplicationModel.Calls.VoipCallCoordinator", 48, n );
}
static HRESULT WINAPI coord2_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }

static HRESULT WINAPI coord2_SetupNewAcceptedCall( void *iface, HSTRING context, HSTRING contactName,
                                                  HSTRING contactNumber, HSTRING serviceName,
                                                  UINT32 media, void **result )
{
    TRACE( "iface %p, contactName %s, media %u, result %p\n", iface, debugstr_hstring( contactName ), media, result );
    return create_voip_phone_call( contactName, media, result );
}

static const void *voip_call_coordinator2_vtbl[] =
{
    coord2_QueryInterface,
    coord2_AddRef,
    coord2_Release,
    coord2_GetIids,
    coord2_GetRuntimeClassName,
    coord2_GetTrustLevel,
    coord2_SetupNewAcceptedCall,
};

static HRESULT WINAPI coord3_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct voip_call_coordinator *impl = impl_from_IVoipCallCoordinator3( iface );
    return coord_QueryInterface( impl, iid, out );
}
static ULONG WINAPI coord3_AddRef( void *iface )
{
    struct voip_call_coordinator *impl = impl_from_IVoipCallCoordinator3( iface );
    return coord_AddRef( impl );
}
static ULONG WINAPI coord3_Release( void *iface )
{
    struct voip_call_coordinator *impl = impl_from_IVoipCallCoordinator3( iface );
    return coord_Release( impl );
}
static HRESULT WINAPI coord3_GetIids( void *iface, ULONG *c, IID **i ) { return E_NOTIMPL; }
static HRESULT WINAPI coord3_GetRuntimeClassName( void *iface, HSTRING *n )
{
    return WindowsCreateString( L"Windows.ApplicationModel.Calls.VoipCallCoordinator", 48, n );
}
static HRESULT WINAPI coord3_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }

static HRESULT WINAPI coord3_RequestNewAppInitiatedCall( void *iface, HSTRING context, HSTRING contactName,
                                                       HSTRING contactNumber, HSTRING serviceName,
                                                       UINT32 media, void **result )
{
    TRACE( "iface %p, contactName %s, media %u, result %p\n", iface, debugstr_hstring( contactName ), media, result );
    return create_voip_phone_call( contactName, media, result );
}

static HRESULT WINAPI coord3_RequestNewIncomingCall( void *iface, HSTRING context, HSTRING contactName,
                                                   HSTRING contactNumber, void *contactImage,
                                                   HSTRING serviceName, void *brandingImage,
                                                   HSTRING callDetails, void *ringtone,
                                                   UINT32 media, TimeSpan ringTimeout,
                                                   HSTRING contactRemoteId, void **result )
{
    TRACE( "iface %p, contactName %s, media %u, result %p\n", iface, debugstr_hstring( contactName ), media, result );
    return create_voip_phone_call( contactName, media, result );
}

static const void *voip_call_coordinator3_vtbl[] =
{
    coord3_QueryInterface,
    coord3_AddRef,
    coord3_Release,
    coord3_GetIids,
    coord3_GetRuntimeClassName,
    coord3_GetTrustLevel,
    coord3_RequestNewAppInitiatedCall,
    coord3_RequestNewIncomingCall,
};

static HRESULT WINAPI coord4_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct voip_call_coordinator *impl = impl_from_IVoipCallCoordinator4( iface );
    return coord_QueryInterface( impl, iid, out );
}
static ULONG WINAPI coord4_AddRef( void *iface )
{
    struct voip_call_coordinator *impl = impl_from_IVoipCallCoordinator4( iface );
    return coord_AddRef( impl );
}
static ULONG WINAPI coord4_Release( void *iface )
{
    struct voip_call_coordinator *impl = impl_from_IVoipCallCoordinator4( iface );
    return coord_Release( impl );
}
static HRESULT WINAPI coord4_GetIids( void *iface, ULONG *c, IID **i ) { return E_NOTIMPL; }
static HRESULT WINAPI coord4_GetRuntimeClassName( void *iface, HSTRING *n )
{
    return WindowsCreateString( L"Windows.ApplicationModel.Calls.VoipCallCoordinator", 48, n );
}
static HRESULT WINAPI coord4_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }

static HRESULT WINAPI coord4_ReserveCallResourcesAsync( void *iface, void **op )
{
    TRACE( "iface %p, op %p\n", iface, op );
    if (op) *op = &reserve_resources_op_inst;
    return S_OK;
}

static const void *voip_call_coordinator4_vtbl[] =
{
    coord4_QueryInterface,
    coord4_AddRef,
    coord4_Release,
    coord4_GetIids,
    coord4_GetRuntimeClassName,
    coord4_GetTrustLevel,
    coord4_ReserveCallResourcesAsync,
};

static struct voip_call_coordinator default_coordinator =
{
    voip_call_coordinator_vtbl,
    voip_call_coordinator2_vtbl,
    voip_call_coordinator3_vtbl,
    voip_call_coordinator4_vtbl,
    1,
};

/* --- VoipCallCoordinator Statics & Factory --- */

static HRESULT WINAPI coord_factory_QueryInterface( void *iface, REFIID iid, void **out )
{
    TRACE( "iface %p, iid %s, out %p\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ) ||
        IsEqualGUID( iid, &IID_IVoipCallCoordinatorStatics ))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI coord_factory_AddRef( void *iface ) { return 2; }
static ULONG WINAPI coord_factory_Release( void *iface ) { return 1; }
static HRESULT WINAPI coord_factory_GetIids( void *iface, ULONG *c, IID **i ) { return E_NOTIMPL; }
static HRESULT WINAPI coord_factory_GetRuntimeClassName( void *iface, HSTRING *n )
{
    return WindowsCreateString( L"Windows.ApplicationModel.Calls.VoipCallCoordinator", 48, n );
}
static HRESULT WINAPI coord_factory_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }

static HRESULT WINAPI coord_factory_ActivateInstance( void *iface, IInspectable **instance )
{
    TRACE( "iface %p, instance %p\n", iface, instance );
    *instance = (IInspectable *)&default_coordinator;
    coord_AddRef( &default_coordinator );
    return S_OK;
}

static HRESULT WINAPI coord_statics_GetDefault( void *iface, void **coordinator )
{
    TRACE( "iface %p, coordinator %p\n", iface, coordinator );
    if (coordinator)
    {
        *coordinator = &default_coordinator;
        coord_AddRef( &default_coordinator );
    }
    return S_OK;
}

static const void *voip_call_coordinator_factory_vtbl[] =
{
    coord_factory_QueryInterface,
    coord_factory_AddRef,
    coord_factory_Release,
    coord_factory_GetIids,
    coord_factory_GetRuntimeClassName,
    coord_factory_GetTrustLevel,
    coord_factory_ActivateInstance,
    coord_statics_GetDefault,
};

static const void *voip_call_coordinator_factory_inst = voip_call_coordinator_factory_vtbl;

HRESULT WINAPI DllCanUnloadNow(void)
{
    return S_FALSE;
}

HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void **out)
{
    FIXME("clsid %s, riid %s, out %p stub!\n", debugstr_guid(clsid), debugstr_guid(riid), out);
    return CLASS_E_CLASSNOTAVAILABLE;
}

HRESULT WINAPI DllGetActivationFactory(HSTRING classid, void **factory)
{
    const WCHAR *buffer = WindowsGetStringRawBuffer( classid, NULL );

    TRACE( "class %s, factory %p.\n", debugstr_hstring(classid), factory );

    if (!factory) return E_POINTER;
    *factory = NULL;

    if (!wcscmp( buffer, L"Windows.ApplicationModel.Calls.VoipCallCoordinator" ))
    {
        *factory = (void *)&voip_call_coordinator_factory_inst;
        return S_OK;
    }

    FIXME( "class %s not supported\n", debugstr_hstring(classid) );
    return E_NOINTERFACE;
}
