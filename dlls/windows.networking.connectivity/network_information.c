/* WinRT Windows.Networking.Connectivity.NetworkInformation Implementation
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

#include <string.h>

#include "private.h"
#include "initguid.h"
#include "netlistmgr.h"
#include "objbase.h"

WINE_DEFAULT_DEBUG_CHANNEL(connectivity);

static EventRegistrationToken dummy_cookie = {.value = 0xdeadbeef};
static const GUID dummy_adapter_id = {0x11111111, 0x2222, 0x3333, {0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb}};

struct network_adapter
{
    INetworkAdapter INetworkAdapter_iface;
    LONG ref;
};

static inline struct network_adapter *impl_from_INetworkAdapter( INetworkAdapter *iface )
{
    return CONTAINING_RECORD( iface, struct network_adapter, INetworkAdapter_iface );
}

static HRESULT WINAPI network_adapter_QueryInterface( INetworkAdapter *iface, REFIID iid, void **out )
{
    struct network_adapter *impl = impl_from_INetworkAdapter( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_INetworkAdapter ))
    {
        *out = &impl->INetworkAdapter_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI network_adapter_AddRef( INetworkAdapter *iface )
{
    struct network_adapter *impl = impl_from_INetworkAdapter( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p increasing refcount to %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI network_adapter_Release( INetworkAdapter *iface )
{
    struct network_adapter *impl = impl_from_INetworkAdapter( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    TRACE( "iface %p decreasing refcount to %lu.\n", iface, ref );
    if (!ref) free( impl );
    return ref;
}

static HRESULT WINAPI network_adapter_GetIids( INetworkAdapter *iface, ULONG *iid_count, IID **iids )
{
    static const IID iid_array[] =
    {
        IID_INetworkAdapter,
    };
    IID *array = CoTaskMemAlloc( sizeof(iid_array) );

    if (!array) return E_OUTOFMEMORY;
    memcpy( array, iid_array, sizeof(iid_array) );
    *iid_count = ARRAY_SIZE(iid_array);
    *iids = array;
    return S_OK;
}

static HRESULT WINAPI network_adapter_GetRuntimeClassName( INetworkAdapter *iface, HSTRING *class_name )
{
    return WindowsCreateString( RuntimeClass_Windows_Networking_Connectivity_NetworkAdapter,
                                wcslen( RuntimeClass_Windows_Networking_Connectivity_NetworkAdapter ),
                                class_name );
}

static HRESULT WINAPI network_adapter_GetTrustLevel( INetworkAdapter *iface, TrustLevel *trust_level )
{
    if (trust_level) *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI network_adapter_get_OutboundMaxBitsPerSecond( INetworkAdapter *iface, UINT64 *value )
{
    TRACE( "iface %p, value %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = 1000000000ULL;
    return S_OK;
}

static HRESULT WINAPI network_adapter_get_InboundMaxBitsPerSecond( INetworkAdapter *iface, UINT64 *value )
{
    TRACE( "iface %p, value %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = 1000000000ULL;
    return S_OK;
}

static HRESULT WINAPI network_adapter_get_IanaInterfaceType( INetworkAdapter *iface, UINT32 *value )
{
    TRACE( "iface %p, value %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = 6; /* MIB_IF_TYPE_ETHERNET */
    return S_OK;
}

static HRESULT WINAPI network_adapter_get_NetworkItem( INetworkAdapter *iface, INetworkItem **value )
{
    TRACE( "iface %p, value %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = NULL;
    return S_OK;
}

static HRESULT WINAPI network_adapter_get_NetworkAdapterId( INetworkAdapter *iface, GUID *value )
{
    TRACE( "iface %p, value %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = dummy_adapter_id;
    return S_OK;
}

static HRESULT WINAPI network_adapter_GetConnectedProfileAsync( INetworkAdapter *iface,
    IAsyncOperation_ConnectionProfile **value )
{
    FIXME( "iface %p, value %p stub!\n", iface, value );
    if (!value) return E_POINTER;
    *value = NULL;
    return E_NOTIMPL;
}

static const struct INetworkAdapterVtbl network_adapter_vtbl =
{
    network_adapter_QueryInterface,
    network_adapter_AddRef,
    network_adapter_Release,
    network_adapter_GetIids,
    network_adapter_GetRuntimeClassName,
    network_adapter_GetTrustLevel,
    network_adapter_get_OutboundMaxBitsPerSecond,
    network_adapter_get_InboundMaxBitsPerSecond,
    network_adapter_get_IanaInterfaceType,
    network_adapter_get_NetworkItem,
    network_adapter_get_NetworkAdapterId,
    network_adapter_GetConnectedProfileAsync,
};

static HRESULT network_adapter_create( INetworkAdapter **adapter )
{
    struct network_adapter *impl;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->INetworkAdapter_iface.lpVtbl = &network_adapter_vtbl;
    impl->ref = 1;
    *adapter = &impl->INetworkAdapter_iface;
    return S_OK;
}

struct ip_information
{
    IIPInformation IIPInformation_iface;
    LONG ref;
};

static inline struct ip_information *impl_from_IIPInformation( IIPInformation *iface )
{
    return CONTAINING_RECORD( iface, struct ip_information, IIPInformation_iface );
}

static HRESULT WINAPI ip_information_QueryInterface( IIPInformation *iface, REFIID iid, void **out )
{
    struct ip_information *impl = impl_from_IIPInformation( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IIPInformation ))
    {
        *out = &impl->IIPInformation_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI ip_information_AddRef( IIPInformation *iface )
{
    struct ip_information *impl = impl_from_IIPInformation( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p increasing refcount to %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI ip_information_Release( IIPInformation *iface )
{
    struct ip_information *impl = impl_from_IIPInformation( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    TRACE( "iface %p decreasing refcount to %lu.\n", iface, ref );
    if (!ref) free( impl );
    return ref;
}

static HRESULT WINAPI ip_information_GetIids( IIPInformation *iface, ULONG *iid_count, IID **iids )
{
    static const IID iid_array[] =
    {
        IID_IIPInformation,
    };
    IID *array = CoTaskMemAlloc( sizeof(iid_array) );

    if (!array) return E_OUTOFMEMORY;
    memcpy( array, iid_array, sizeof(iid_array) );
    *iid_count = ARRAY_SIZE(iid_array);
    *iids = array;
    return S_OK;
}

static HRESULT WINAPI ip_information_GetRuntimeClassName( IIPInformation *iface, HSTRING *class_name )
{
    return WindowsCreateString( RuntimeClass_Windows_Networking_Connectivity_IPInformation,
                                wcslen( RuntimeClass_Windows_Networking_Connectivity_IPInformation ),
                                class_name );
}

static HRESULT WINAPI ip_information_GetTrustLevel( IIPInformation *iface, TrustLevel *trust_level )
{
    if (trust_level) *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI ip_information_get_NetworkAdapter( IIPInformation *iface, INetworkAdapter **value )
{
    TRACE( "iface %p, value %p\n", iface, value );
    if (!value) return E_POINTER;
    return network_adapter_create( value );
}

static HRESULT WINAPI ip_information_get_PrefixLength( IIPInformation *iface, IReference_BYTE **value )
{
    TRACE( "iface %p, value %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = NULL;
    return S_OK;
}

static const struct IIPInformationVtbl ip_information_vtbl =
{
    ip_information_QueryInterface,
    ip_information_AddRef,
    ip_information_Release,
    ip_information_GetIids,
    ip_information_GetRuntimeClassName,
    ip_information_GetTrustLevel,
    ip_information_get_NetworkAdapter,
    ip_information_get_PrefixLength,
};

static HRESULT ip_information_create( IIPInformation **ip_info )
{
    struct ip_information *impl;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->IIPInformation_iface.lpVtbl = &ip_information_vtbl;
    impl->ref = 1;
    *ip_info = &impl->IIPInformation_iface;
    return S_OK;
}

struct dummy_hostname
{
    IHostName IHostName_iface;
    IStringable IStringable_iface;
    LONG ref;
    const WCHAR *name;
    HostNameType type;
};

static inline struct dummy_hostname *impl_from_IHostName( IHostName *iface )
{
    return CONTAINING_RECORD( iface, struct dummy_hostname, IHostName_iface );
}

static inline struct dummy_hostname *impl_from_IStringable( IStringable *iface )
{
    return CONTAINING_RECORD( iface, struct dummy_hostname, IStringable_iface );
}

static HRESULT WINAPI dummy_hostname_QueryInterface( IHostName *iface, REFIID iid, void **out )
{
    struct dummy_hostname *impl = impl_from_IHostName( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IHostName ))
    {
        *out = &impl->IHostName_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IStringable ))
    {
        *out = &impl->IStringable_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI dummy_hostname_AddRef( IHostName *iface )
{
    struct dummy_hostname *impl = impl_from_IHostName( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p increasing refcount to %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI dummy_hostname_Release( IHostName *iface )
{
    struct dummy_hostname *impl = impl_from_IHostName( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    TRACE( "iface %p decreasing refcount to %lu.\n", iface, ref );
    if (!ref) free( impl );
    return ref;
}

static HRESULT WINAPI dummy_hostname_GetIids( IHostName *iface, ULONG *iid_count, IID **iids )
{
    static const IID iid_array[] =
    {
        IID_IHostName,
        IID_IStringable,
    };
    IID *array = CoTaskMemAlloc( sizeof(iid_array) );

    if (!array) return E_OUTOFMEMORY;
    memcpy( array, iid_array, sizeof(iid_array) );
    *iid_count = ARRAY_SIZE(iid_array);
    *iids = array;
    return S_OK;
}

static HRESULT WINAPI dummy_hostname_GetRuntimeClassName( IHostName *iface, HSTRING *class_name )
{
    return WindowsCreateString( RuntimeClass_Windows_Networking_HostName,
                                wcslen( RuntimeClass_Windows_Networking_HostName ),
                                class_name );
}

static HRESULT WINAPI dummy_hostname_GetTrustLevel( IHostName *iface, TrustLevel *trust_level )
{
    if (trust_level) *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI dummy_hostname_get_IPInformation( IHostName *iface, IIPInformation **value )
{
    TRACE( "iface %p, value %p\n", iface, value );
    if (!value) return E_POINTER;
    return ip_information_create( value );
}

static HRESULT WINAPI dummy_hostname_get_RawName( IHostName *iface, HSTRING *value )
{
    struct dummy_hostname *impl = impl_from_IHostName( iface );
    TRACE( "iface %p, value %p\n", iface, value );
    if (!value) return E_POINTER;
    return WindowsCreateString( impl->name, wcslen( impl->name ), value );
}

static HRESULT WINAPI dummy_hostname_get_DisplayName( IHostName *iface, HSTRING *value )
{
    struct dummy_hostname *impl = impl_from_IHostName( iface );
    TRACE( "iface %p, value %p\n", iface, value );
    if (!value) return E_POINTER;
    return WindowsCreateString( impl->name, wcslen( impl->name ), value );
}

static HRESULT WINAPI dummy_hostname_get_CanonicalName( IHostName *iface, HSTRING *value )
{
    struct dummy_hostname *impl = impl_from_IHostName( iface );
    TRACE( "iface %p, value %p\n", iface, value );
    if (!value) return E_POINTER;
    return WindowsCreateString( impl->name, wcslen( impl->name ), value );
}

static HRESULT WINAPI dummy_hostname_get_Type( IHostName *iface, HostNameType *value )
{
    struct dummy_hostname *impl = impl_from_IHostName( iface );
    TRACE( "iface %p, value %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = impl->type;
    return S_OK;
}

static HRESULT WINAPI dummy_hostname_IsEqual( IHostName *iface, IHostName *name, boolean *equal )
{
    FIXME( "iface %p, name %p, equal %p stub!\n", iface, name, equal );
    if (equal) *equal = FALSE;
    return S_OK;
}

static const struct IHostNameVtbl dummy_hostname_vtbl =
{
    dummy_hostname_QueryInterface,
    dummy_hostname_AddRef,
    dummy_hostname_Release,
    dummy_hostname_GetIids,
    dummy_hostname_GetRuntimeClassName,
    dummy_hostname_GetTrustLevel,
    dummy_hostname_get_IPInformation,
    dummy_hostname_get_RawName,
    dummy_hostname_get_DisplayName,
    dummy_hostname_get_CanonicalName,
    dummy_hostname_get_Type,
    dummy_hostname_IsEqual,
};

static HRESULT WINAPI dummy_hostname_stringable_QueryInterface( IStringable *iface, REFIID iid, void **out )
{
    struct dummy_hostname *impl = impl_from_IStringable( iface );
    return IHostName_QueryInterface( &impl->IHostName_iface, iid, out );
}

static ULONG WINAPI dummy_hostname_stringable_AddRef( IStringable *iface )
{
    struct dummy_hostname *impl = impl_from_IStringable( iface );
    return IHostName_AddRef( &impl->IHostName_iface );
}

static ULONG WINAPI dummy_hostname_stringable_Release( IStringable *iface )
{
    struct dummy_hostname *impl = impl_from_IStringable( iface );
    return IHostName_Release( &impl->IHostName_iface );
}

static HRESULT WINAPI dummy_hostname_stringable_GetIids( IStringable *iface, ULONG *iid_count, IID **iids )
{
    static const IID iid_array[] =
    {
        IID_IHostName,
        IID_IStringable,
    };
    IID *array = CoTaskMemAlloc( sizeof(iid_array) );

    if (!array) return E_OUTOFMEMORY;
    memcpy( array, iid_array, sizeof(iid_array) );
    *iid_count = ARRAY_SIZE(iid_array);
    *iids = array;
    return S_OK;
}

static HRESULT WINAPI dummy_hostname_stringable_GetRuntimeClassName( IStringable *iface, HSTRING *class_name )
{
    return WindowsCreateString( RuntimeClass_Windows_Networking_HostName,
                                wcslen( RuntimeClass_Windows_Networking_HostName ),
                                class_name );
}

static HRESULT WINAPI dummy_hostname_stringable_GetTrustLevel( IStringable *iface, TrustLevel *trust_level )
{
    if (trust_level) *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI dummy_hostname_stringable_ToString( IStringable *iface, HSTRING *value )
{
    struct dummy_hostname *impl = impl_from_IStringable( iface );
    if (!value) return E_POINTER;
    return WindowsCreateString( impl->name, wcslen( impl->name ), value );
}

static const struct IStringableVtbl dummy_hostname_stringable_vtbl =
{
    dummy_hostname_stringable_QueryInterface,
    dummy_hostname_stringable_AddRef,
    dummy_hostname_stringable_Release,
    dummy_hostname_stringable_GetIids,
    dummy_hostname_stringable_GetRuntimeClassName,
    dummy_hostname_stringable_GetTrustLevel,
    dummy_hostname_stringable_ToString,
};

static HRESULT dummy_hostname_create( const WCHAR *name, HostNameType type, IHostName **hostname )
{
    struct dummy_hostname *impl;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->IHostName_iface.lpVtbl = &dummy_hostname_vtbl;
    impl->IStringable_iface.lpVtbl = &dummy_hostname_stringable_vtbl;
    impl->ref = 1;
    impl->name = name;
    impl->type = type;
    *hostname = &impl->IHostName_iface;
    return S_OK;
}

struct hostnames_view
{
    IVectorView_HostName IVectorView_HostName_iface;
    LONG ref;
};

static inline struct hostnames_view *impl_from_IVectorView_HostName( IVectorView_HostName *iface )
{
    return CONTAINING_RECORD( iface, struct hostnames_view, IVectorView_HostName_iface );
}

static HRESULT WINAPI hostnames_view_QueryInterface( IVectorView_HostName *iface, REFIID iid, void **out )
{
    struct hostnames_view *impl = impl_from_IVectorView_HostName( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IVectorView_HostName ))
    {
        *out = &impl->IVectorView_HostName_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI hostnames_view_AddRef( IVectorView_HostName *iface )
{
    struct hostnames_view *impl = impl_from_IVectorView_HostName( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p increasing refcount to %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI hostnames_view_Release( IVectorView_HostName *iface )
{
    struct hostnames_view *impl = impl_from_IVectorView_HostName( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    TRACE( "iface %p decreasing refcount to %lu.\n", iface, ref );
    if (!ref) free( impl );
    return ref;
}

static HRESULT WINAPI hostnames_view_GetIids( IVectorView_HostName *iface, ULONG *iid_count, IID **iids )
{
    static const IID iid_array[] =
    {
        IID_IVectorView_HostName,
    };
    IID *array = CoTaskMemAlloc( sizeof(iid_array) );

    if (!array) return E_OUTOFMEMORY;
    memcpy( array, iid_array, sizeof(iid_array) );
    *iid_count = ARRAY_SIZE(iid_array);
    *iids = array;
    return S_OK;
}

static HRESULT WINAPI hostnames_view_GetRuntimeClassName( IVectorView_HostName *iface, HSTRING *class_name )
{
    return WindowsCreateString( L"Windows.Foundation.Collections.IVectorView`1<Windows.Networking.HostName>",
                                67, class_name );
}

static HRESULT WINAPI hostnames_view_GetTrustLevel( IVectorView_HostName *iface, TrustLevel *trust_level )
{
    if (trust_level) *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI hostnames_view_GetAt( IVectorView_HostName *iface, unsigned int index, IHostName **item )
{
    TRACE( "iface %p, index %u, item %p\n", iface, index, item );
    if (!item) return E_POINTER;
    if (index == 0)
        return dummy_hostname_create( L"127.0.0.1", HostNameType_Ipv4, item );
    if (index == 1)
        return dummy_hostname_create( L"::1", HostNameType_Ipv6, item );
    *item = NULL;
    return 0x8000000b; /* E_BOUNDS */
}

static HRESULT WINAPI hostnames_view_get_Size( IVectorView_HostName *iface, unsigned int *size )
{
    TRACE( "iface %p, size %p\n", iface, size );
    if (!size) return E_POINTER;
    *size = 2;
    return S_OK;
}

static HRESULT WINAPI hostnames_view_IndexOf( IVectorView_HostName *iface, IHostName *item, unsigned int *index, boolean *found )
{
    TRACE( "iface %p, item %p, index %p, found %p\n", iface, item, index, found );
    if (found) *found = FALSE;
    if (index) *index = 0;
    return S_OK;
}

static HRESULT WINAPI hostnames_view_GetMany( IVectorView_HostName *iface, unsigned int start,
                                              unsigned int count, IHostName **items, unsigned int *actual )
{
    unsigned int fetched = 0;
    TRACE( "iface %p, start %u, count %u, items %p, actual %p\n", iface, start, count, items, actual );
    if (!actual) return E_POINTER;
    while (start + fetched < 2 && fetched < count)
    {
        if (FAILED(hostnames_view_GetAt( iface, start + fetched, &items[fetched] )))
            break;
        fetched++;
    }
    *actual = fetched;
    return S_OK;
}

static const struct IVectorView_HostNameVtbl hostnames_view_vtbl =
{
    hostnames_view_QueryInterface,
    hostnames_view_AddRef,
    hostnames_view_Release,
    hostnames_view_GetIids,
    hostnames_view_GetRuntimeClassName,
    hostnames_view_GetTrustLevel,
    hostnames_view_GetAt,
    hostnames_view_get_Size,
    hostnames_view_IndexOf,
    hostnames_view_GetMany,
};

static HRESULT hostnames_view_create( IVectorView_HostName **view )
{
    struct hostnames_view *impl;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->IVectorView_HostName_iface.lpVtbl = &hostnames_view_vtbl;
    impl->ref = 1;
    *view = &impl->IVectorView_HostName_iface;
    return S_OK;
}

struct connection_cost
{
    IConnectionCost IConnectionCost_iface;
    LONG ref;
};

static inline struct connection_cost *impl_from_IConnectionCost( IConnectionCost *iface )
{
    return CONTAINING_RECORD( iface, struct connection_cost, IConnectionCost_iface );
}

static HRESULT WINAPI connection_cost_QueryInterface( IConnectionCost *iface, REFIID iid, void **out )
{
    struct connection_cost *impl = impl_from_IConnectionCost( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IConnectionCost ))
    {
        *out = &impl->IConnectionCost_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI connection_cost_AddRef( IConnectionCost *iface )
{
    struct connection_cost *impl = impl_from_IConnectionCost( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p increasing refcount to %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI connection_cost_Release( IConnectionCost *iface )
{
    struct connection_cost *impl = impl_from_IConnectionCost( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    TRACE( "iface %p decreasing refcount to %lu.\n", iface, ref );
    if (!ref) free( impl );
    return ref;
}

static HRESULT WINAPI connection_cost_GetIids( IConnectionCost *iface, ULONG *iid_count, IID **iids )
{
    static const IID iid_array[] =
    {
        IID_IConnectionCost,
    };
    IID *array = CoTaskMemAlloc( sizeof(iid_array) );

    if (!array) return E_OUTOFMEMORY;
    memcpy( array, iid_array, sizeof(iid_array) );
    *iid_count = ARRAY_SIZE(iid_array);
    *iids = array;
    return S_OK;
}

static HRESULT WINAPI connection_cost_GetRuntimeClassName( IConnectionCost *iface, HSTRING *class_name )
{
    return WindowsCreateString( RuntimeClass_Windows_Networking_Connectivity_ConnectionCost,
                                wcslen( RuntimeClass_Windows_Networking_Connectivity_ConnectionCost ),
                                class_name );
}

static HRESULT WINAPI connection_cost_GetTrustLevel( IConnectionCost *iface, TrustLevel *trust_level )
{
    if (trust_level) *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI connection_cost_get_NetworkCostType( IConnectionCost *iface, NetworkCostType *value )
{
    TRACE( "iface %p, value %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = NetworkCostType_Unrestricted;
    return S_OK;
}

static HRESULT WINAPI connection_cost_get_Roaming( IConnectionCost *iface, boolean *value )
{
    TRACE( "iface %p, value %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = FALSE;
    return S_OK;
}

static HRESULT WINAPI connection_cost_get_OverDataLimit( IConnectionCost *iface, boolean *value )
{
    TRACE( "iface %p, value %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = FALSE;
    return S_OK;
}

static HRESULT WINAPI connection_cost_get_ApproachingDataLimit( IConnectionCost *iface, boolean *value )
{
    TRACE( "iface %p, value %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = FALSE;
    return S_OK;
}

static const struct IConnectionCostVtbl connection_cost_vtbl =
{
    connection_cost_QueryInterface,
    connection_cost_AddRef,
    connection_cost_Release,
    connection_cost_GetIids,
    connection_cost_GetRuntimeClassName,
    connection_cost_GetTrustLevel,
    connection_cost_get_NetworkCostType,
    connection_cost_get_Roaming,
    connection_cost_get_OverDataLimit,
    connection_cost_get_ApproachingDataLimit,
};

static HRESULT connection_cost_create( IConnectionCost **cost )
{
    struct connection_cost *impl;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->IConnectionCost_iface.lpVtbl = &connection_cost_vtbl;
    impl->ref = 1;
    *cost = &impl->IConnectionCost_iface;
    return S_OK;
}

struct wlan_details
{
    IWlanConnectionProfileDetails IWlanConnectionProfileDetails_iface;
    LONG ref;
};

static inline struct wlan_details *impl_from_IWlanConnectionProfileDetails( IWlanConnectionProfileDetails *iface )
{
    return CONTAINING_RECORD( iface, struct wlan_details, IWlanConnectionProfileDetails_iface );
}

static HRESULT WINAPI wlan_details_QueryInterface( IWlanConnectionProfileDetails *iface, REFIID iid, void **out )
{
    struct wlan_details *impl = impl_from_IWlanConnectionProfileDetails( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IWlanConnectionProfileDetails ))
    {
        *out = &impl->IWlanConnectionProfileDetails_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI wlan_details_AddRef( IWlanConnectionProfileDetails *iface )
{
    struct wlan_details *impl = impl_from_IWlanConnectionProfileDetails( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p increasing refcount to %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI wlan_details_Release( IWlanConnectionProfileDetails *iface )
{
    struct wlan_details *impl = impl_from_IWlanConnectionProfileDetails( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    TRACE( "iface %p decreasing refcount to %lu.\n", iface, ref );
    if (!ref) free( impl );
    return ref;
}

static HRESULT WINAPI wlan_details_GetIids( IWlanConnectionProfileDetails *iface, ULONG *iid_count, IID **iids )
{
    static const IID iid_array[] =
    {
        IID_IWlanConnectionProfileDetails,
    };
    IID *array = CoTaskMemAlloc( sizeof(iid_array) );

    if (!array) return E_OUTOFMEMORY;
    memcpy( array, iid_array, sizeof(iid_array) );
    *iid_count = ARRAY_SIZE(iid_array);
    *iids = array;
    return S_OK;
}

static HRESULT WINAPI wlan_details_GetRuntimeClassName( IWlanConnectionProfileDetails *iface, HSTRING *class_name )
{
    return WindowsCreateString( RuntimeClass_Windows_Networking_Connectivity_WlanConnectionProfileDetails,
                                wcslen( RuntimeClass_Windows_Networking_Connectivity_WlanConnectionProfileDetails ),
                                class_name );
}

static HRESULT WINAPI wlan_details_GetTrustLevel( IWlanConnectionProfileDetails *iface, TrustLevel *trust_level )
{
    if (trust_level) *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI wlan_details_GetConnectedSsid( IWlanConnectionProfileDetails *iface, HSTRING *value )
{
    TRACE( "iface %p, value %p\n", iface, value );
    if (!value) return E_POINTER;
    return WindowsCreateString( L"Wine Network", 12, value );
}

static const struct IWlanConnectionProfileDetailsVtbl wlan_details_vtbl =
{
    wlan_details_QueryInterface,
    wlan_details_AddRef,
    wlan_details_Release,
    wlan_details_GetIids,
    wlan_details_GetRuntimeClassName,
    wlan_details_GetTrustLevel,
    wlan_details_GetConnectedSsid,
};

static HRESULT wlan_details_create( IWlanConnectionProfileDetails **details )
{
    struct wlan_details *impl;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->IWlanConnectionProfileDetails_iface.lpVtbl = &wlan_details_vtbl;
    impl->ref = 1;
    *details = &impl->IWlanConnectionProfileDetails_iface;
    return S_OK;
}

struct empty_hstrings_view
{
    IVectorView_HSTRING IVectorView_HSTRING_iface;
};

static HRESULT WINAPI empty_hstrings_QueryInterface( IVectorView_HSTRING *iface, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IVectorView_HSTRING ))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI empty_hstrings_AddRef( IVectorView_HSTRING *iface ) { return 2; }
static ULONG WINAPI empty_hstrings_Release( IVectorView_HSTRING *iface ) { return 1; }
static HRESULT WINAPI empty_hstrings_GetIids( IVectorView_HSTRING *iface, ULONG *c, IID **i )
{
    static const IID iid_array[] =
    {
        IID_IVectorView_HSTRING,
    };
    IID *array = CoTaskMemAlloc( sizeof(iid_array) );

    if (!array) return E_OUTOFMEMORY;
    memcpy( array, iid_array, sizeof(iid_array) );
    *iid_count = ARRAY_SIZE(iid_array);
    *iids = array;
    return S_OK;
}
static HRESULT WINAPI empty_hstrings_GetTrustLevel( IVectorView_HSTRING *iface, TrustLevel *l )
{
    if (l) *l = BaseTrust; return S_OK;
}
static HRESULT WINAPI empty_hstrings_GetAt( IVectorView_HSTRING *iface, unsigned int index, HSTRING *item )
{
    if (item) *item = NULL;
    return 0x8000000b; /* E_BOUNDS */
}
static HRESULT WINAPI empty_hstrings_get_Size( IVectorView_HSTRING *iface, unsigned int *size )
{
    if (size) *size = 0;
    return S_OK;
}
static HRESULT WINAPI empty_hstrings_IndexOf( IVectorView_HSTRING *iface, HSTRING item, unsigned int *index, boolean *found )
{
    if (found) *found = FALSE;
    if (index) *index = 0;
    return S_OK;
}
static HRESULT WINAPI empty_hstrings_GetMany( IVectorView_HSTRING *iface, unsigned int start, unsigned int count, HSTRING *items, unsigned int *actual )
{
    if (actual) *actual = 0;
    return S_OK;
}

static const struct IVectorView_HSTRINGVtbl empty_hstrings_vtbl =
{
    empty_hstrings_QueryInterface,
    empty_hstrings_AddRef,
    empty_hstrings_Release,
    empty_hstrings_GetIids,
    empty_hstrings_GetRuntimeClassName,
    empty_hstrings_GetTrustLevel,
    empty_hstrings_GetAt,
    empty_hstrings_get_Size,
    empty_hstrings_IndexOf,
    empty_hstrings_GetMany,
};
static struct empty_hstrings_view empty_hstrings_obj = { { &empty_hstrings_vtbl } };

struct empty_lan_identifiers_view
{
    IVectorView_LanIdentifier IVectorView_LanIdentifier_iface;
};

static HRESULT WINAPI empty_lan_identifiers_QueryInterface( IVectorView_LanIdentifier *iface, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IVectorView_LanIdentifier ))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI empty_lan_identifiers_AddRef( IVectorView_LanIdentifier *iface ) { return 2; }
static ULONG WINAPI empty_lan_identifiers_Release( IVectorView_LanIdentifier *iface ) { return 1; }
static HRESULT WINAPI empty_lan_identifiers_GetIids( IVectorView_LanIdentifier *iface, ULONG *c, IID **i )
{
    static const IID iid_array[] =
    {
        IID_IVectorView_LanIdentifier,
    };
    IID *array = CoTaskMemAlloc( sizeof(iid_array) );

    if (!array) return E_OUTOFMEMORY;
    memcpy( array, iid_array, sizeof(iid_array) );
    *iid_count = ARRAY_SIZE(iid_array);
    *iids = array;
    return S_OK;
}
static HRESULT WINAPI empty_lan_identifiers_GetTrustLevel( IVectorView_LanIdentifier *iface, TrustLevel *l )
{
    if (l) *l = BaseTrust; return S_OK;
}
static HRESULT WINAPI empty_lan_identifiers_GetAt( IVectorView_LanIdentifier *iface, unsigned int index, ILanIdentifier **item )
{
    if (item) *item = NULL;
    return 0x8000000b; /* E_BOUNDS */
}
static HRESULT WINAPI empty_lan_identifiers_get_Size( IVectorView_LanIdentifier *iface, unsigned int *size )
{
    if (size) *size = 0;
    return S_OK;
}
static HRESULT WINAPI empty_lan_identifiers_IndexOf( IVectorView_LanIdentifier *iface, ILanIdentifier *item, unsigned int *index, boolean *found )
{
    if (found) *found = FALSE;
    if (index) *index = 0;
    return S_OK;
}
static HRESULT WINAPI empty_lan_identifiers_GetMany( IVectorView_LanIdentifier *iface, unsigned int start, unsigned int count, ILanIdentifier **items, unsigned int *actual )
{
    if (actual) *actual = 0;
    return S_OK;
}

static const struct IVectorView_LanIdentifierVtbl empty_lan_identifiers_vtbl =
{
    empty_lan_identifiers_QueryInterface,
    empty_lan_identifiers_AddRef,
    empty_lan_identifiers_Release,
    empty_lan_identifiers_GetIids,
    empty_lan_identifiers_GetRuntimeClassName,
    empty_lan_identifiers_GetTrustLevel,
    empty_lan_identifiers_GetAt,
    empty_lan_identifiers_get_Size,
    empty_lan_identifiers_IndexOf,
    empty_lan_identifiers_GetMany,
};
static struct empty_lan_identifiers_view empty_lan_identifiers_obj = { { &empty_lan_identifiers_vtbl } };

struct connection_profile
{
    IConnectionProfile IConnectionProfile_iface;
    IConnectionProfile2 IConnectionProfile2_iface;
    LONG ref;

    INetworkListManager *network_list_manager;
};

static inline struct connection_profile *impl_from_IConnectionProfile( IConnectionProfile *iface )
{
    return CONTAINING_RECORD( iface, struct connection_profile, IConnectionProfile_iface );
}

static HRESULT WINAPI connection_profile_QueryInterface( IConnectionProfile *iface, REFIID iid, void **out )
{
    struct connection_profile *impl = impl_from_IConnectionProfile( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IConnectionProfile ))
    {
        *out = &impl->IConnectionProfile_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IConnectionProfile2 ))
    {
        *out = &impl->IConnectionProfile2_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }
    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI connection_profile_AddRef( IConnectionProfile *iface )
{
    struct connection_profile *impl = impl_from_IConnectionProfile( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p increasing refcount to %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI connection_profile_Release( IConnectionProfile *iface )
{
    struct connection_profile *impl = impl_from_IConnectionProfile( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    TRACE( "iface %p decreasing refcount to %lu.\n", iface, ref );

    if (!ref)
    {
        if (impl->network_list_manager)
            INetworkListManager_Release( impl->network_list_manager );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI connection_profile_GetIids( IConnectionProfile *iface, ULONG *iid_count, IID **iids )
{
    static const IID iid_array[] =
    {
        IID_IConnectionProfile,
        IID_IConnectionProfile2,
    };
    IID *array = CoTaskMemAlloc( sizeof(iid_array) );

    if (!array) return E_OUTOFMEMORY;
    memcpy( array, iid_array, sizeof(iid_array) );
    *iid_count = ARRAY_SIZE(iid_array);
    *iids = array;
    return S_OK;
}

static HRESULT WINAPI connection_profile_GetRuntimeClassName( IConnectionProfile *iface, HSTRING *class_name )
{
    return WindowsCreateString( RuntimeClass_Windows_Networking_Connectivity_ConnectionProfile,
                                wcslen( RuntimeClass_Windows_Networking_Connectivity_ConnectionProfile ),
                                class_name );
}

static HRESULT WINAPI connection_profile_GetTrustLevel( IConnectionProfile *iface, TrustLevel *trust_level )
{
    if (trust_level) *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI connection_profile_get_ProfileName( IConnectionProfile *iface, HSTRING *value )
{
    TRACE( "iface %p, value %p\n", iface, value );
    if (!value) return E_POINTER;
    return WindowsCreateString( L"Wine Network", 12, value );
}

static HRESULT WINAPI connection_profile_GetNetworkConnectivityLevel( IConnectionProfile *iface, NetworkConnectivityLevel *value )
{
    struct connection_profile *impl = impl_from_IConnectionProfile( iface );
    NetworkConnectivityLevel network_connectivity_level = NetworkConnectivityLevel_InternetAccess;
    NLM_CONNECTIVITY connectivity = 0xdeadbeef;

    TRACE( "iface %p, value %p\n", iface, value );

    if (!value) return E_POINTER;
    if (impl->network_list_manager && SUCCEEDED(INetworkListManager_GetConnectivity( impl->network_list_manager, &connectivity )))
    {
        if (connectivity == NLM_CONNECTIVITY_DISCONNECTED)
            network_connectivity_level = NetworkConnectivityLevel_InternetAccess;
        else if (connectivity & ( NLM_CONNECTIVITY_IPV4_INTERNET | NLM_CONNECTIVITY_IPV6_INTERNET ))
            network_connectivity_level = NetworkConnectivityLevel_InternetAccess;
        else if (connectivity & ( NLM_CONNECTIVITY_IPV4_LOCALNETWORK | NLM_CONNECTIVITY_IPV6_LOCALNETWORK | NLM_CONNECTIVITY_IPV4_NOTRAFFIC | NLM_CONNECTIVITY_IPV6_NOTRAFFIC ))
            network_connectivity_level = NetworkConnectivityLevel_LocalAccess;
        else
            network_connectivity_level = NetworkConnectivityLevel_ConstrainedInternetAccess;
    }

    *value = network_connectivity_level;
    return S_OK;
}

static HRESULT WINAPI connection_profile_GetNetworkNames( IConnectionProfile *iface, IVectorView_HSTRING **value )
{
    TRACE( "iface %p, value %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = &empty_hstrings_obj.IVectorView_HSTRING_iface;
    return S_OK;
}

static HRESULT WINAPI connection_profile_GetConnectionCost( IConnectionProfile *iface, IConnectionCost **value )
{
    TRACE( "iface %p, value %p\n", iface, value );
    if (!value) return E_POINTER;
    return connection_cost_create( value );
}

static HRESULT WINAPI connection_profile_GetDataPlanStatus( IConnectionProfile *iface, IDataPlanStatus **value )
{
    TRACE( "iface %p, value %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = NULL;
    return S_OK;
}

static HRESULT WINAPI connection_profile_get_NetworkAdapter( IConnectionProfile *iface, INetworkAdapter **value )
{
    TRACE( "iface %p, value %p\n", iface, value );
    if (!value) return E_POINTER;
    return network_adapter_create( value );
}

static HRESULT WINAPI connection_profile_GetLocalUsage( IConnectionProfile *iface, DateTime start, DateTime end, IDataUsage **value )
{
    TRACE( "iface %p, start %I64d, end %I64d, value %p\n", iface, start.UniversalTime, end.UniversalTime, value );
    if (!value) return E_POINTER;
    *value = NULL;
    return S_OK;
}

static HRESULT WINAPI connection_profile_GetLocalUsagePerRoamingStates( IConnectionProfile *iface, DateTime start, DateTime end, RoamingStates states, IDataUsage **value )
{
    TRACE( "iface %p, start %I64d, end %I64d, states %u, value %p\n", iface, start.UniversalTime, end.UniversalTime, states, value );
    if (!value) return E_POINTER;
    *value = NULL;
    return S_OK;
}

static HRESULT WINAPI connection_profile_get_NetworkSecuritySettings( IConnectionProfile *iface, INetworkSecuritySettings **value )
{
    TRACE( "iface %p, value %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = NULL;
    return S_OK;
}

static const struct IConnectionProfileVtbl connection_profile_vtbl =
{
    connection_profile_QueryInterface,
    connection_profile_AddRef,
    connection_profile_Release,
    /* IInspectable methods */
    connection_profile_GetIids,
    connection_profile_GetRuntimeClassName,
    connection_profile_GetTrustLevel,
    /* IConnectionProfile methods */
    connection_profile_get_ProfileName,
    connection_profile_GetNetworkConnectivityLevel,
    connection_profile_GetNetworkNames,
    connection_profile_GetConnectionCost,
    connection_profile_GetDataPlanStatus,
    connection_profile_get_NetworkAdapter,
    connection_profile_GetLocalUsage,
    connection_profile_GetLocalUsagePerRoamingStates,
    connection_profile_get_NetworkSecuritySettings,
};

static inline struct connection_profile *impl_from_IConnectionProfile2( IConnectionProfile2 *iface )
{
    return CONTAINING_RECORD( iface, struct connection_profile, IConnectionProfile2_iface );
}

static HRESULT WINAPI connection_profile2_QueryInterface( IConnectionProfile2 *iface, REFIID iid, void **out )
{
    struct connection_profile *impl = impl_from_IConnectionProfile2( iface );
    return IConnectionProfile_QueryInterface(&impl->IConnectionProfile_iface, iid, out);
}

static ULONG WINAPI connection_profile2_AddRef( IConnectionProfile2 *iface )
{
    struct connection_profile *impl = impl_from_IConnectionProfile2( iface );
    return IConnectionProfile_AddRef(&impl->IConnectionProfile_iface);
}

static ULONG WINAPI connection_profile2_Release( IConnectionProfile2 *iface )
{
    struct connection_profile *impl = impl_from_IConnectionProfile2( iface );
    return IConnectionProfile_Release(&impl->IConnectionProfile_iface);
}

static HRESULT WINAPI connection_profile2_GetIids( IConnectionProfile2 *iface, ULONG *iid_count, IID **iids )
{
    static const IID iid_array[] =
    {
        IID_IConnectionProfile,
        IID_IConnectionProfile2,
    };
    IID *array = CoTaskMemAlloc( sizeof(iid_array) );

    if (!array) return E_OUTOFMEMORY;
    memcpy( array, iid_array, sizeof(iid_array) );
    *iid_count = ARRAY_SIZE(iid_array);
    *iids = array;
    return S_OK;
}

static HRESULT WINAPI connection_profile2_GetRuntimeClassName( IConnectionProfile2 *iface, HSTRING *class_name )
{
    return WindowsCreateString( RuntimeClass_Windows_Networking_Connectivity_ConnectionProfile,
                                wcslen( RuntimeClass_Windows_Networking_Connectivity_ConnectionProfile ),
                                class_name );
}

static HRESULT WINAPI connection_profile2_GetTrustLevel( IConnectionProfile2 *iface, TrustLevel *trust_level )
{
    if (trust_level) *trust_level = BaseTrust;
    return S_OK;
}

HRESULT WINAPI connection_profile2_get_IsWwanConnectionProfile( IConnectionProfile2 *iface, boolean *value )
{
    TRACE( "iface %p, value %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = 0;
    return S_OK;
}

HRESULT WINAPI connection_profile2_get_IsWlanConnectionProfile( IConnectionProfile2 *iface, boolean *value )
{
    TRACE( "iface %p, value %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = 0;
    return S_OK;
}

HRESULT WINAPI connection_profile2_get_WwanConnectionProfileDetails( IConnectionProfile2 *iface, IWwanConnectionProfileDetails **value )
{
    TRACE( "iface %p, value %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = NULL;
    return S_OK;
}

HRESULT WINAPI connection_profile2_get_WlanConnectionProfileDetails( IConnectionProfile2 *iface, IWlanConnectionProfileDetails **value )
{
    TRACE( "iface %p, value %p\n", iface, value );
    if (!value) return E_POINTER;
    return wlan_details_create( value );
}

HRESULT WINAPI connection_profile2_get_ServiceProviderGuid( IConnectionProfile2 *iface, IReference_GUID **value )
{
    TRACE( "iface %p, value %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = NULL;
    return S_OK;
}

HRESULT WINAPI connection_profile2_GetSignalBars( IConnectionProfile2 *iface, IReference_BYTE **value )
{
    TRACE( "iface %p, value %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = NULL;
    return S_OK;
}

HRESULT WINAPI connection_profile2_GetDomainConnectivityLevel( IConnectionProfile2 *iface, DomainConnectivityLevel *value )
{
    TRACE( "iface %p, value %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = DomainConnectivityLevel_None;
    return S_OK;
}

HRESULT WINAPI connection_profile2_GetNetworkUsageAsync( IConnectionProfile2 *iface, DateTime time_start, DateTime time_end,
        DataUsageGranularity granularity, NetworkUsageStates states, IAsyncOperation_IVectorView_NetworkUsage **value )
{
    FIXME( "iface %p, time_start %I64d, time_end %I64d, granularity %d, states %d-%d, value %p stub!\n",
            iface, time_start.UniversalTime, time_end.UniversalTime, granularity, states.Roaming, states.Shared, value );
    if (!value) return E_POINTER;
    *value = NULL;
    return E_NOTIMPL;
}

HRESULT WINAPI connection_profile2_GetConnectivityIntervalsAsync( IConnectionProfile2 *iface, DateTime time_start, DateTime time_end,
        NetworkUsageStates states, IAsyncOperation_IVectorView_ConnectivityInterval **value )
{
    FIXME( "iface %p, time_start %I64d, time_end %I64d, states %d-%d, value %p stub!\n",
            iface, time_start.UniversalTime, time_end.UniversalTime, states.Roaming, states.Shared, value );
    if (!value) return E_POINTER;
    *value = NULL;
    return E_NOTIMPL;
}

static const struct IConnectionProfile2Vtbl connection_profile2_vtbl =
{
    connection_profile2_QueryInterface,
    connection_profile2_AddRef,
    connection_profile2_Release,
    /* IInspectable methods */
    connection_profile2_GetIids,
    connection_profile2_GetRuntimeClassName,
    connection_profile2_GetTrustLevel,
    /* IConnectionProfile2 methods */
    connection_profile2_get_IsWwanConnectionProfile,
    connection_profile2_get_IsWlanConnectionProfile,
    connection_profile2_get_WwanConnectionProfileDetails,
    connection_profile2_get_WlanConnectionProfileDetails,
    connection_profile2_get_ServiceProviderGuid,
    connection_profile2_GetSignalBars,
    connection_profile2_GetDomainConnectivityLevel,
    connection_profile2_GetNetworkUsageAsync,
    connection_profile2_GetConnectivityIntervalsAsync,
};

static HRESULT connection_profile_create( INetworkListManager *network_list_manager, IConnectionProfile **profile )
{
    struct connection_profile *impl;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->IConnectionProfile_iface.lpVtbl = &connection_profile_vtbl;
    impl->IConnectionProfile2_iface.lpVtbl = &connection_profile2_vtbl;
    impl->ref = 1;
    impl->network_list_manager = network_list_manager;
    if (impl->network_list_manager)
        INetworkListManager_AddRef( impl->network_list_manager );

    *profile = &impl->IConnectionProfile_iface;
    return S_OK;
}

struct connection_profiles_view
{
    IVectorView_ConnectionProfile IVectorView_ConnectionProfile_iface;
    LONG ref;
};

static inline struct connection_profiles_view *impl_from_IVectorView_ConnectionProfile( IVectorView_ConnectionProfile *iface )
{
    return CONTAINING_RECORD( iface, struct connection_profiles_view, IVectorView_ConnectionProfile_iface );
}

static HRESULT WINAPI connection_profiles_view_QueryInterface( IVectorView_ConnectionProfile *iface, REFIID iid, void **out )
{
    struct connection_profiles_view *impl = impl_from_IVectorView_ConnectionProfile( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IVectorView_ConnectionProfile ))
    {
        *out = &impl->IVectorView_ConnectionProfile_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI connection_profiles_view_AddRef( IVectorView_ConnectionProfile *iface )
{
    struct connection_profiles_view *impl = impl_from_IVectorView_ConnectionProfile( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p increasing refcount to %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI connection_profiles_view_Release( IVectorView_ConnectionProfile *iface )
{
    struct connection_profiles_view *impl = impl_from_IVectorView_ConnectionProfile( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    TRACE( "iface %p decreasing refcount to %lu.\n", iface, ref );
    if (!ref) free( impl );
    return ref;
}

static HRESULT WINAPI connection_profiles_view_GetIids( IVectorView_ConnectionProfile *iface, ULONG *iid_count, IID **iids )
{
    static const IID iid_array[] =
    {
        IID_IVectorView_ConnectionProfile,
    };
    IID *array = CoTaskMemAlloc( sizeof(iid_array) );

    if (!array) return E_OUTOFMEMORY;
    memcpy( array, iid_array, sizeof(iid_array) );
    *iid_count = ARRAY_SIZE(iid_array);
    *iids = array;
    return S_OK;
}

static HRESULT WINAPI connection_profiles_view_GetRuntimeClassName( IVectorView_ConnectionProfile *iface, HSTRING *class_name )
{
    return WindowsCreateString( L"Windows.Foundation.Collections.IVectorView`1<Windows.Networking.Connectivity.ConnectionProfile>",
                                80, class_name );
}

static HRESULT WINAPI connection_profiles_view_GetTrustLevel( IVectorView_ConnectionProfile *iface, TrustLevel *trust_level )
{
    if (trust_level) *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI connection_profiles_view_GetAt( IVectorView_ConnectionProfile *iface, unsigned int index, IConnectionProfile **item )
{
    TRACE( "iface %p, index %u, item %p\n", iface, index, item );
    if (!item) return E_POINTER;
    if (index == 0)
        return connection_profile_create( NULL, item );
    *item = NULL;
    return 0x8000000b; /* E_BOUNDS */
}

static HRESULT WINAPI connection_profiles_view_get_Size( IVectorView_ConnectionProfile *iface, unsigned int *size )
{
    TRACE( "iface %p, size %p\n", iface, size );
    if (!size) return E_POINTER;
    *size = 1;
    return S_OK;
}

static HRESULT WINAPI connection_profiles_view_IndexOf( IVectorView_ConnectionProfile *iface, IConnectionProfile *item,
                                                       unsigned int *index, boolean *found )
{
    TRACE( "iface %p, item %p, index %p, found %p\n", iface, item, index, found );
    if (found) *found = FALSE;
    if (index) *index = 0;
    return S_OK;
}

static HRESULT WINAPI connection_profiles_view_GetMany( IVectorView_ConnectionProfile *iface, unsigned int start,
                                                       unsigned int count, IConnectionProfile **items, unsigned int *actual )
{
    TRACE( "iface %p, start %u, count %u, items %p, actual %p\n", iface, start, count, items, actual );
    if (!actual) return E_POINTER;
    if (start == 0 && count > 0)
    {
        if (SUCCEEDED(connection_profile_create( NULL, &items[0] )))
        {
            *actual = 1;
            return S_OK;
        }
    }
    *actual = 0;
    return S_OK;
}

static const struct IVectorView_ConnectionProfileVtbl connection_profiles_view_vtbl =
{
    connection_profiles_view_QueryInterface,
    connection_profiles_view_AddRef,
    connection_profiles_view_Release,
    connection_profiles_view_GetIids,
    connection_profiles_view_GetRuntimeClassName,
    connection_profiles_view_GetTrustLevel,
    connection_profiles_view_GetAt,
    connection_profiles_view_get_Size,
    connection_profiles_view_IndexOf,
    connection_profiles_view_GetMany,
};

static HRESULT connection_profiles_view_create( IVectorView_ConnectionProfile **view )
{
    struct connection_profiles_view *impl;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->IVectorView_ConnectionProfile_iface.lpVtbl = &connection_profiles_view_vtbl;
    impl->ref = 1;
    *view = &impl->IVectorView_ConnectionProfile_iface;
    return S_OK;
}

struct network_information_statics
{
    IActivationFactory IActivationFactory_iface;
    INetworkInformationStatics INetworkInformationStatics_iface;
    LONG ref;
};

static inline struct network_information_statics *impl_from_IActivationFactory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct network_information_statics, IActivationFactory_iface );
}

static HRESULT WINAPI factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct network_information_statics *impl = impl_from_IActivationFactory( iface );

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

    if (IsEqualGUID( iid, &IID_INetworkInformationStatics ))
    {
        *out = &impl->INetworkInformationStatics_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI factory_AddRef( IActivationFactory *iface )
{
    struct network_information_statics *impl = impl_from_IActivationFactory( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p increasing refcount to %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI factory_Release( IActivationFactory *iface )
{
    struct network_information_statics *impl = impl_from_IActivationFactory( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    TRACE( "iface %p decreasing refcount to %lu.\n", iface, ref );
    return ref;
}

static HRESULT WINAPI factory_GetIids( IActivationFactory *iface, ULONG *iid_count, IID **iids )
{
    static const IID iid_array[] =
    {
        IID_IActivationFactory,
        IID_INetworkInformationStatics,
    };
    IID *array = CoTaskMemAlloc( sizeof(iid_array) );

    if (!array) return E_OUTOFMEMORY;
    memcpy( array, iid_array, sizeof(iid_array) );
    *iid_count = ARRAY_SIZE(iid_array);
    *iids = array;
    return S_OK;
}

static HRESULT WINAPI factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *class_name )
{
    return WindowsCreateString( RuntimeClass_Windows_Networking_Connectivity_NetworkInformation,
                                wcslen( RuntimeClass_Windows_Networking_Connectivity_NetworkInformation ),
                                class_name );
}

static HRESULT WINAPI factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *trust_level )
{
    if (trust_level) *trust_level = BaseTrust;
    return S_OK;
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

DEFINE_IINSPECTABLE( network_information_statics, INetworkInformationStatics, struct network_information_statics, IActivationFactory_iface )

static HRESULT WINAPI network_information_statics_GetConnectionProfiles( INetworkInformationStatics *iface, IVectorView_ConnectionProfile **value )
{
    TRACE( "iface %p, value %p\n", iface, value );
    if (!value) return E_POINTER;
    return connection_profiles_view_create( value );
}

static HRESULT WINAPI network_information_statics_GetInternetConnectionProfile( INetworkInformationStatics *iface, IConnectionProfile **value )
{
    INetworkListManager *network_list_manager = NULL;
    HRESULT hr;

    TRACE( "iface %p, value %p\n", iface, value );

    if (!value) return E_POINTER;
    *value = NULL;

    CoCreateInstance( &CLSID_NetworkListManager, NULL, CLSCTX_INPROC_SERVER, &IID_INetworkListManager, (void **)&network_list_manager );

    hr = connection_profile_create( network_list_manager, value );
    if (network_list_manager) INetworkListManager_Release( network_list_manager );

    TRACE( "created IConnectionProfile %p, hr %#lx.\n", *value, hr );
    return hr;
}

static HRESULT WINAPI network_information_statics_GetLanIdentifiers( INetworkInformationStatics *iface, IVectorView_LanIdentifier **value )
{
    TRACE( "iface %p, value %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = &empty_lan_identifiers_obj.IVectorView_LanIdentifier_iface;
    return S_OK;
}

static HRESULT WINAPI network_information_statics_GetHostNames( INetworkInformationStatics *iface, IVectorView_HostName **value )
{
    TRACE( "iface %p, value %p\n", iface, value );
    if (!value) return E_POINTER;
    return hostnames_view_create( value );
}

static HRESULT WINAPI network_information_statics_GetProxyConfigurationAsync( INetworkInformationStatics *iface, IUriRuntimeClass *uri, IAsyncOperation_ProxyConfiguration **value )
{
    FIXME( "iface %p, uri %p, value %p stub!\n", iface, uri, value );
    if (!value) return E_POINTER;
    *value = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI network_information_statics_GetSortedEndpointPairs( INetworkInformationStatics *iface, IIterable_EndpointPair *endpoint,
                                                                          HostNameSortOptions options, IVectorView_EndpointPair **value )
{
    FIXME( "iface %p, endpoint %p, options %d, value %p stub!\n", iface, endpoint, options, value );
    if (!value) return E_POINTER;
    *value = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI network_information_statics_add_NetworkStatusChanged( INetworkInformationStatics *iface, INetworkStatusChangedEventHandler *handler,
                                                                            EventRegistrationToken *cookie )
{
    TRACE( "iface %p, handler %p, cookie %p\n", iface, handler, cookie );
    if (!cookie) return E_POINTER;
    *cookie = dummy_cookie;
    return S_OK;
}

static HRESULT WINAPI network_information_statics_remove_NetworkStatusChanged( INetworkInformationStatics *iface, EventRegistrationToken cookie )
{
    TRACE( "iface %p, cookie %#I64x\n", iface, cookie.value );
    return S_OK;
}

static const struct INetworkInformationStaticsVtbl network_information_statics_vtbl =
{
    network_information_statics_QueryInterface,
    network_information_statics_AddRef,
    network_information_statics_Release,
    /* IInspectable methods */
    network_information_statics_GetIids,
    network_information_statics_GetRuntimeClassName,
    network_information_statics_GetTrustLevel,
    /* INetworkInformationStatics methods */
    network_information_statics_GetConnectionProfiles,
    network_information_statics_GetInternetConnectionProfile,
    network_information_statics_GetLanIdentifiers,
    network_information_statics_GetHostNames,
    network_information_statics_GetProxyConfigurationAsync,
    network_information_statics_GetSortedEndpointPairs,
    network_information_statics_add_NetworkStatusChanged,
    network_information_statics_remove_NetworkStatusChanged,
};

static struct network_information_statics network_information_statics =
{
    {&factory_vtbl},
    {&network_information_statics_vtbl},
    1,
};

IActivationFactory *network_information_factory = &network_information_statics.IActivationFactory_iface;
