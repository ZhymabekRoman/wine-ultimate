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

struct ticket_request_factory_statics
{
    IActivationFactory IActivationFactory_iface;
    IOnlineIdServiceTicketRequestFactory IOnlineIdServiceTicketRequestFactory_iface;
    LONG ref;
};

static inline struct ticket_request_factory_statics *impl_from_IActivationFactory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct ticket_request_factory_statics, IActivationFactory_iface );
}

static HRESULT WINAPI factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct ticket_request_factory_statics *impl = impl_from_IActivationFactory( iface );

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

    if (IsEqualGUID( iid, &IID_IOnlineIdServiceTicketRequestFactory ))
    {
        *out = &impl->IOnlineIdServiceTicketRequestFactory_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI factory_AddRef( IActivationFactory *iface )
{
    struct ticket_request_factory_statics *impl = impl_from_IActivationFactory( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p, ref %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI factory_Release( IActivationFactory *iface )
{
    struct ticket_request_factory_statics *impl = impl_from_IActivationFactory( iface );
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

DEFINE_IINSPECTABLE( ticket_request_factory_statics, IOnlineIdServiceTicketRequestFactory, struct ticket_request_factory_statics, IActivationFactory_iface )

struct ticket_request
{
    IOnlineIdServiceTicketRequest IOnlineIdServiceTicketRequest_iface;
    LONG ref;
    HSTRING service;
    HSTRING policy;
};

static inline struct ticket_request *impl_from_ticket_request( IOnlineIdServiceTicketRequest *iface )
{
    return CONTAINING_RECORD( iface, struct ticket_request, IOnlineIdServiceTicketRequest_iface );
}

static HRESULT WINAPI ticket_request_QueryInterface( IOnlineIdServiceTicketRequest *iface, REFIID iid, void **out )
{
    struct ticket_request *impl = impl_from_ticket_request( iface );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IOnlineIdServiceTicketRequest ))
    {
        *out = &impl->IOnlineIdServiceTicketRequest_iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    FIXME( "unsupported iid %s\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI ticket_request_AddRef( IOnlineIdServiceTicketRequest *iface )
{
    return InterlockedIncrement( &impl_from_ticket_request( iface )->ref );
}
static ULONG WINAPI ticket_request_Release( IOnlineIdServiceTicketRequest *iface )
{
    struct ticket_request *impl = impl_from_ticket_request( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        WindowsDeleteString( impl->service );
        WindowsDeleteString( impl->policy );
        free( impl );
    }
    return ref;
}
static HRESULT WINAPI ticket_request_GetIids( IOnlineIdServiceTicketRequest *iface, ULONG *c, IID **i )
{
    if (c) *c = 0;
    if (i) *i = NULL;
    return S_OK;
}
static HRESULT WINAPI ticket_request_GetRuntimeClassName( IOnlineIdServiceTicketRequest *iface, HSTRING *n )
{
    static const WCHAR cls[] = L"Windows.Security.Authentication.OnlineId.OnlineIdServiceTicketRequest";
    return WindowsCreateString( cls, ARRAY_SIZE(cls) - 1, n );
}
static HRESULT WINAPI ticket_request_GetTrustLevel( IOnlineIdServiceTicketRequest *iface, TrustLevel *l )
{
    if (l) *l = BaseTrust;
    return S_OK;
}
static HRESULT WINAPI ticket_request_get_Service( IOnlineIdServiceTicketRequest *iface, HSTRING *value )
{
    struct ticket_request *impl = impl_from_ticket_request( iface );
    if (!value) return E_POINTER;
    return WindowsDuplicateString( impl->service, value );
}
static HRESULT WINAPI ticket_request_get_Policy( IOnlineIdServiceTicketRequest *iface, HSTRING *value )
{
    struct ticket_request *impl = impl_from_ticket_request( iface );
    if (!value) return E_POINTER;
    return WindowsDuplicateString( impl->policy, value );
}

static const struct IOnlineIdServiceTicketRequestVtbl ticket_request_vtbl =
{
    ticket_request_QueryInterface,
    ticket_request_AddRef,
    ticket_request_Release,
    ticket_request_GetIids,
    ticket_request_GetRuntimeClassName,
    ticket_request_GetTrustLevel,
    ticket_request_get_Service,
    ticket_request_get_Policy,
};

static HRESULT create_ticket_request( HSTRING service, HSTRING policy, IOnlineIdServiceTicketRequest **out )
{
    struct ticket_request *impl;
    if (!out) return E_POINTER;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->IOnlineIdServiceTicketRequest_iface.lpVtbl = &ticket_request_vtbl;
    impl->ref = 1;
    if (service) WindowsDuplicateString( service, &impl->service );
    if (policy) WindowsDuplicateString( policy, &impl->policy );
    *out = &impl->IOnlineIdServiceTicketRequest_iface;
    return S_OK;
}

static HRESULT WINAPI ticket_request_factory_statics_CreateOnlineIdServiceTicketRequest( IOnlineIdServiceTicketRequestFactory *iface, HSTRING service,
                                                                                         HSTRING policy, IOnlineIdServiceTicketRequest **request )
{
    TRACE( "iface %p, service %s, policy %s, request %p\n", iface, debugstr_hstring( service ), debugstr_hstring( policy ), request );
    return create_ticket_request( service, policy, request );
}

static HRESULT WINAPI ticket_request_factory_statics_CreateOnlineIdServiceTicketRequestAdvanced( IOnlineIdServiceTicketRequestFactory *iface, HSTRING service,
                                                                                                 IOnlineIdServiceTicketRequest **request )
{
    TRACE( "iface %p, service %s, request %p\n", iface, debugstr_hstring( service ), request );
    return create_ticket_request( service, NULL, request );
}

static const struct IOnlineIdServiceTicketRequestFactoryVtbl ticket_request_factory_statics_vtbl =
{
    ticket_request_factory_statics_QueryInterface,
    ticket_request_factory_statics_AddRef,
    ticket_request_factory_statics_Release,
    /* IInspectable methods */
    ticket_request_factory_statics_GetIids,
    ticket_request_factory_statics_GetRuntimeClassName,
    ticket_request_factory_statics_GetTrustLevel,
    /* IOnlineIdServiceTicketRequestFactory methods */
    ticket_request_factory_statics_CreateOnlineIdServiceTicketRequest,
    ticket_request_factory_statics_CreateOnlineIdServiceTicketRequestAdvanced,
};

static struct ticket_request_factory_statics ticket_request_factory_statics =
{
    {&factory_vtbl},
    {&ticket_request_factory_statics_vtbl},
    1,
};

IActivationFactory *ticket_factory = &ticket_request_factory_statics.IActivationFactory_iface;
