/* WinRT Windows.Security.Authentication.Onlineid Implementation
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

#include "private.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(onlineid);

struct authenticator_statics
{
    IActivationFactory IActivationFactory_iface;
    IOnlineIdSystemAuthenticatorStatics IOnlineIdSystemAuthenticatorStatics_iface;
    LONG ref;
};

static inline struct authenticator_statics *impl_from_IActivationFactory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct authenticator_statics, IActivationFactory_iface );
}

static HRESULT WINAPI factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct authenticator_statics *impl = impl_from_IActivationFactory( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = &impl->IActivationFactory_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IOnlineIdSystemAuthenticatorStatics ))
    {
        *out = &impl->IOnlineIdSystemAuthenticatorStatics_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI factory_AddRef( IActivationFactory *iface )
{
    struct authenticator_statics *impl = impl_from_IActivationFactory( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p, ref %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI factory_Release( IActivationFactory *iface )
{
    struct authenticator_statics *impl = impl_from_IActivationFactory( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    TRACE( "iface %p, ref %lu.\n", iface, ref );
    return ref;
}

static HRESULT WINAPI factory_GetIids( IActivationFactory *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *class_name )
{
    FIXME( "iface %p, class_name %p stub!\n", iface, class_name );
    return E_NOTIMPL;
}

static HRESULT WINAPI factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    FIXME( "iface %p, instance %p stub!\n", iface, instance );
    return E_NOTIMPL;
}

static const struct IActivationFactoryVtbl factory_vtbl =
{
    factory_QueryInterface,
    factory_AddRef,
    factory_Release,
    /* IInspectable methods */
    factory_GetIids,
    factory_GetRuntimeClassName,
    factory_GetTrustLevel,
    /* IActivationFactory methods */
    factory_ActivateInstance,
};

struct ticket_result
{
    IOnlineIdSystemTicketResult IOnlineIdSystemTicketResult_iface;
    LONG ref;
};

static inline struct ticket_result *impl_from_ticket_result( IOnlineIdSystemTicketResult *iface )
{
    return CONTAINING_RECORD( iface, struct ticket_result, IOnlineIdSystemTicketResult_iface );
}
static HRESULT WINAPI ticket_result_QueryInterface( IOnlineIdSystemTicketResult *iface, REFIID iid, void **out )
{
    struct ticket_result *impl = impl_from_ticket_result( iface );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IOnlineIdSystemTicketResult ))
    {
        *out = &impl->IOnlineIdSystemTicketResult_iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    FIXME( "unsupported iid %s\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI ticket_result_AddRef( IOnlineIdSystemTicketResult *iface )
{ return InterlockedIncrement( &impl_from_ticket_result( iface )->ref ); }
static ULONG WINAPI ticket_result_Release( IOnlineIdSystemTicketResult *iface )
{
    struct ticket_result *impl = impl_from_ticket_result( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref) free( impl );
    return ref;
}
static HRESULT WINAPI ticket_result_GetIids( IOnlineIdSystemTicketResult *iface, ULONG *c, IID **i )
{ if (c) *c = 0; if (i) *i = NULL; return S_OK; }
static HRESULT WINAPI ticket_result_GetRuntimeClassName( IOnlineIdSystemTicketResult *iface, HSTRING *n )
{
    static const WCHAR cls[] = L"Windows.Security.Authentication.OnlineId.OnlineIdSystemTicketResult";
    return WindowsCreateString( cls, ARRAY_SIZE(cls) - 1, n );
}
static HRESULT WINAPI ticket_result_GetTrustLevel( IOnlineIdSystemTicketResult *iface, TrustLevel *l )
{ if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI ticket_result_get_Identity( IOnlineIdSystemTicketResult *iface,
        __x_ABI_CWindows_CSecurity_CAuthentication_COnlineId_CIOnlineIdSystemIdentity **value )
{
    if (!value) return E_POINTER;
    *value = NULL;
    return S_OK;
}
static HRESULT WINAPI ticket_result_get_Status( IOnlineIdSystemTicketResult *iface,
        OnlineIdSystemTicketStatus *value )
{
    if (!value) return E_POINTER;
    *value = OnlineIdSystemTicketStatus_Error;
    return S_OK;
}
static HRESULT WINAPI ticket_result_get_ExtendedError( IOnlineIdSystemTicketResult *iface, HRESULT *value )
{
    if (!value) return E_POINTER;
    *value = E_FAIL;
    return S_OK;
}
static const struct IOnlineIdSystemTicketResultVtbl ticket_result_vtbl =
{
    ticket_result_QueryInterface, ticket_result_AddRef, ticket_result_Release,
    ticket_result_GetIids, ticket_result_GetRuntimeClassName, ticket_result_GetTrustLevel,
    ticket_result_get_Identity, ticket_result_get_Status, ticket_result_get_ExtendedError,
};

struct ticket_op
{
    IAsyncOperation_OnlineIdSystemTicketResult IAsyncOperation_iface;
    IAsyncInfo IAsyncInfo_iface;
    LONG ref;
    IOnlineIdSystemTicketResult *result;
};
static inline struct ticket_op *impl_from_ticket_op( IAsyncOperation_OnlineIdSystemTicketResult *iface )
{ return CONTAINING_RECORD( iface, struct ticket_op, IAsyncOperation_iface ); }
static inline struct ticket_op *impl_from_ticket_info( IAsyncInfo *iface )
{ return CONTAINING_RECORD( iface, struct ticket_op, IAsyncInfo_iface ); }

static HRESULT WINAPI ticket_op_QueryInterface( IAsyncOperation_OnlineIdSystemTicketResult *iface, REFIID iid, void **out )
{
    struct ticket_op *impl = impl_from_ticket_op( iface );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IAsyncOperation_OnlineIdSystemTicketResult ))
    {
        *out = &impl->IAsyncOperation_iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IAsyncInfo ))
    {
        *out = &impl->IAsyncInfo_iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    FIXME( "unsupported iid %s\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI ticket_op_AddRef( IAsyncOperation_OnlineIdSystemTicketResult *iface )
{ return InterlockedIncrement( &impl_from_ticket_op( iface )->ref ); }
static ULONG WINAPI ticket_op_Release( IAsyncOperation_OnlineIdSystemTicketResult *iface )
{
    struct ticket_op *impl = impl_from_ticket_op( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref) { if (impl->result) IOnlineIdSystemTicketResult_Release( impl->result ); free( impl ); }
    return ref;
}
static HRESULT WINAPI ticket_op_GetIids( IAsyncOperation_OnlineIdSystemTicketResult *iface, ULONG *c, IID **i )
{ if (c) *c = 0; if (i) *i = NULL; return S_OK; }
static HRESULT WINAPI ticket_op_GetRuntimeClassName( IAsyncOperation_OnlineIdSystemTicketResult *iface, HSTRING *n )
{
    static const WCHAR cls[] = L"Windows.Foundation.IAsyncOperation`1<Windows.Security.Authentication.OnlineId.OnlineIdSystemTicketResult>";
    return WindowsCreateString( cls, ARRAY_SIZE(cls) - 1, n );
}
static HRESULT WINAPI ticket_op_GetTrustLevel( IAsyncOperation_OnlineIdSystemTicketResult *iface, TrustLevel *l )
{ if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI ticket_op_put_Completed( IAsyncOperation_OnlineIdSystemTicketResult *iface,
        IAsyncOperationCompletedHandler_OnlineIdSystemTicketResult *handler )
{ if (handler) handler->lpVtbl->Invoke( handler, iface, Completed ); return S_OK; }
static HRESULT WINAPI ticket_op_get_Completed( IAsyncOperation_OnlineIdSystemTicketResult *iface,
        IAsyncOperationCompletedHandler_OnlineIdSystemTicketResult **handler )
{ if (handler) *handler = NULL; return S_OK; }
static HRESULT WINAPI ticket_op_GetResults( IAsyncOperation_OnlineIdSystemTicketResult *iface,
        IOnlineIdSystemTicketResult **results )
{
    struct ticket_op *impl = impl_from_ticket_op( iface );
    if (!results) return E_POINTER;
    *results = impl->result;
    if (*results) IOnlineIdSystemTicketResult_AddRef( *results );
    return S_OK;
}
static const struct __FIAsyncOperation_1_Windows__CSecurity__CAuthentication__COnlineId__COnlineIdSystemTicketResultVtbl ticket_op_vtbl =
{
    ticket_op_QueryInterface, ticket_op_AddRef, ticket_op_Release,
    ticket_op_GetIids, ticket_op_GetRuntimeClassName, ticket_op_GetTrustLevel,
    ticket_op_put_Completed, ticket_op_get_Completed, ticket_op_GetResults,
};

static HRESULT WINAPI ticket_info_QI( IAsyncInfo *iface, REFIID iid, void **out )
{ return ticket_op_QueryInterface( &impl_from_ticket_info( iface )->IAsyncOperation_iface, iid, out ); }
static ULONG WINAPI ticket_info_AddRef( IAsyncInfo *iface ) { return ticket_op_AddRef( &impl_from_ticket_info( iface )->IAsyncOperation_iface ); }
static ULONG WINAPI ticket_info_Release( IAsyncInfo *iface ) { return ticket_op_Release( &impl_from_ticket_info( iface )->IAsyncOperation_iface ); }
static HRESULT WINAPI ticket_info_GetIids( IAsyncInfo *iface, ULONG *c, IID **i ) { if (c) *c = 0; if (i) *i = NULL; return S_OK; }
static HRESULT WINAPI ticket_info_GetRuntimeClassName( IAsyncInfo *iface, HSTRING *n )
{ static const WCHAR cls[] = L"Windows.Foundation.IAsyncInfo"; return WindowsCreateString( cls, ARRAY_SIZE(cls) - 1, n ); }
static HRESULT WINAPI ticket_info_GetTrustLevel( IAsyncInfo *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI ticket_info_get_Id( IAsyncInfo *iface, UINT32 *id ) { if (id) *id = 1; return S_OK; }
static HRESULT WINAPI ticket_info_get_Status( IAsyncInfo *iface, AsyncStatus *s ) { if (s) *s = Completed; return S_OK; }
static HRESULT WINAPI ticket_info_get_ErrorCode( IAsyncInfo *iface, HRESULT *e ) { if (e) *e = S_OK; return S_OK; }
static HRESULT WINAPI ticket_info_Cancel( IAsyncInfo *iface ) { return S_OK; }
static HRESULT WINAPI ticket_info_Close( IAsyncInfo *iface ) { return S_OK; }
static const struct IAsyncInfoVtbl ticket_info_vtbl =
{
    ticket_info_QI, ticket_info_AddRef, ticket_info_Release,
    ticket_info_GetIids, ticket_info_GetRuntimeClassName, ticket_info_GetTrustLevel,
    ticket_info_get_Id, ticket_info_get_Status, ticket_info_get_ErrorCode, ticket_info_Cancel, ticket_info_Close,
};

struct authenticator
{
    IOnlineIdSystemAuthenticatorForUser IOnlineIdSystemAuthenticatorForUser_iface;
    LONG ref;
    GUID app_id;
    __x_ABI_CWindows_CSystem_CIUser *user;
};

static inline struct authenticator *impl_from_IOnlineIdSystemAuthenticatorForUser( IOnlineIdSystemAuthenticatorForUser *iface )
{
    return CONTAINING_RECORD( iface, struct authenticator, IOnlineIdSystemAuthenticatorForUser_iface );
}

static HRESULT WINAPI authenticator_QueryInterface( IOnlineIdSystemAuthenticatorForUser *iface, REFIID iid, void **out )
{
    struct authenticator *impl = impl_from_IOnlineIdSystemAuthenticatorForUser( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IOnlineIdSystemAuthenticatorForUser ))
    {
        *out = &impl->IOnlineIdSystemAuthenticatorForUser_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI authenticator_AddRef( IOnlineIdSystemAuthenticatorForUser *iface )
{
    struct authenticator *impl = impl_from_IOnlineIdSystemAuthenticatorForUser( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p increasing refcount to %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI authenticator_Release( IOnlineIdSystemAuthenticatorForUser *iface )
{
    struct authenticator *impl = impl_from_IOnlineIdSystemAuthenticatorForUser( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    TRACE( "iface %p decreasing refcount to %lu.\n", iface, ref );

    if (!ref)
    {
        if (impl->user) IUnknown_Release( (IUnknown *)impl->user );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI authenticator_GetIids( IOnlineIdSystemAuthenticatorForUser *iface, ULONG *iid_count, IID **iids )
{
    if (iid_count) *iid_count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI authenticator_GetRuntimeClassName( IOnlineIdSystemAuthenticatorForUser *iface, HSTRING *class_name )
{
    static const WCHAR cls[] = L"Windows.Security.Authentication.OnlineId.OnlineIdSystemAuthenticatorForUser";
    return WindowsCreateString( cls, ARRAY_SIZE(cls) - 1, class_name );
}

static HRESULT WINAPI authenticator_GetTrustLevel( IOnlineIdSystemAuthenticatorForUser *iface, TrustLevel *trust_level )
{
    if (trust_level) *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI authenticator_GetTicketAsync( IOnlineIdSystemAuthenticatorForUser *iface, IOnlineIdServiceTicketRequest *request,
                                                    IAsyncOperation_OnlineIdSystemTicketResult **operation )
{
    struct ticket_result *res;
    struct ticket_op *op;

    TRACE( "iface %p, request %p, operation %p\n", iface, request, operation );
    if (!operation) return E_POINTER;
    if (!(res = calloc( 1, sizeof(*res) ))) return E_OUTOFMEMORY;
    res->IOnlineIdSystemTicketResult_iface.lpVtbl = &ticket_result_vtbl;
    res->ref = 1;
    if (!(op = calloc( 1, sizeof(*op) ))) { free( res ); return E_OUTOFMEMORY; }
    op->IAsyncOperation_iface.lpVtbl = &ticket_op_vtbl;
    op->IAsyncInfo_iface.lpVtbl = &ticket_info_vtbl;
    op->ref = 1;
    op->result = &res->IOnlineIdSystemTicketResult_iface;
    *operation = &op->IAsyncOperation_iface;
    return S_OK;
}

static HRESULT WINAPI authenticator_put_ApplicationId( IOnlineIdSystemAuthenticatorForUser *iface, GUID value )
{
    struct authenticator *impl = impl_from_IOnlineIdSystemAuthenticatorForUser( iface );
    impl->app_id = value;
    return S_OK;
}

static HRESULT WINAPI authenticator_get_ApplicationId( IOnlineIdSystemAuthenticatorForUser *iface, GUID *value )
{
    struct authenticator *impl = impl_from_IOnlineIdSystemAuthenticatorForUser( iface );
    if (!value) return E_POINTER;
    *value = impl->app_id;
    return S_OK;
}

static HRESULT WINAPI authenticator_get_User( IOnlineIdSystemAuthenticatorForUser *iface, __x_ABI_CWindows_CSystem_CIUser **user )
{
    struct authenticator *impl = impl_from_IOnlineIdSystemAuthenticatorForUser( iface );
    if (!user) return E_POINTER;
    *user = impl->user;
    if (*user) IUnknown_AddRef( (IUnknown *)*user );
    return S_OK;
}

static const struct IOnlineIdSystemAuthenticatorForUserVtbl authenticator_vtbl =
{
    authenticator_QueryInterface,
    authenticator_AddRef,
    authenticator_Release,
    /* IInspectable methods */
    authenticator_GetIids,
    authenticator_GetRuntimeClassName,
    authenticator_GetTrustLevel,
    /* IOnlineIdSystemAuthenticatorForUser methods */
    authenticator_GetTicketAsync,
    authenticator_put_ApplicationId,
    authenticator_get_ApplicationId,
    authenticator_get_User,
};

DEFINE_IINSPECTABLE( authenticator_statics, IOnlineIdSystemAuthenticatorStatics, struct authenticator_statics, IActivationFactory_iface )

static HRESULT WINAPI authenticator_statics_get_Default( IOnlineIdSystemAuthenticatorStatics *iface,
                                                         IOnlineIdSystemAuthenticatorForUser **value )
{
    struct authenticator *impl;

    TRACE( "iface %p, value %p\n", iface, value );

    if (!value) return E_POINTER;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;

    impl->IOnlineIdSystemAuthenticatorForUser_iface.lpVtbl = &authenticator_vtbl;
    impl->ref = 1;

    *value = &impl->IOnlineIdSystemAuthenticatorForUser_iface;
    TRACE( "created IOnlineIdSystemAuthenticatorForUser %p.\n", *value );
    return S_OK;
}

static HRESULT WINAPI authenticator_statics_GetForUser( IOnlineIdSystemAuthenticatorStatics *iface, __x_ABI_CWindows_CSystem_CIUser *user,
                                                        IOnlineIdSystemAuthenticatorForUser **value )
{
    struct authenticator *impl;

    TRACE( "iface %p, user %p, value %p\n", iface, user, value );
    if (!value) return E_POINTER;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->IOnlineIdSystemAuthenticatorForUser_iface.lpVtbl = &authenticator_vtbl;
    impl->ref = 1;
    impl->user = user;
    if (user) IUnknown_AddRef( (IUnknown *)user );
    *value = &impl->IOnlineIdSystemAuthenticatorForUser_iface;
    return S_OK;
}

static const struct IOnlineIdSystemAuthenticatorStaticsVtbl authenticator_statics_vtbl =
{
    authenticator_statics_QueryInterface,
    authenticator_statics_AddRef,
    authenticator_statics_Release,
    /* IInspectable methods */
    authenticator_statics_GetIids,
    authenticator_statics_GetRuntimeClassName,
    authenticator_statics_GetTrustLevel,
    /* IOnlineIdSystemAuthenticatorStatics methods */
    authenticator_statics_get_Default,
    authenticator_statics_GetForUser,
};

static struct authenticator_statics authenticator_statics =
{
    {&factory_vtbl},
    {&authenticator_statics_vtbl},
    1,
};

IActivationFactory *authenticator_factory = &authenticator_statics.IActivationFactory_iface;
