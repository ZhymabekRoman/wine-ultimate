/* WinRT Windows.Storage.Streams.Buffer Implementation
 *
 * Copyright (C) 2025 Mohamad Al-Jaf
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
#include "robuffer.h"

WINE_DEFAULT_DEBUG_CHANNEL(wintypes);

struct buffer_factory_statics
{
    IActivationFactory IActivationFactory_iface;
    IBufferFactory IBufferFactory_iface;
    IBuffer IBuffer_iface;
    LONG ref;
};

static inline struct buffer_factory_statics *impl_from_IActivationFactory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct buffer_factory_statics, IActivationFactory_iface );
}

static HRESULT WINAPI factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct buffer_factory_statics *impl = impl_from_IActivationFactory( iface );

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

    if (IsEqualGUID( iid, &IID_IBufferFactory ))
    {
        *out = &impl->IBufferFactory_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    /* Windows aggregates the class default interface on the factory object. */
    if (IsEqualGUID( iid, &IID_IBuffer ))
    {
        *out = &impl->IBuffer_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI factory_AddRef( IActivationFactory *iface )
{
    struct buffer_factory_statics *impl = impl_from_IActivationFactory( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p increasing refcount to %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI factory_Release( IActivationFactory *iface )
{
    struct buffer_factory_statics *impl = impl_from_IActivationFactory( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    TRACE( "iface %p decreasing refcount to %lu.\n", iface, ref );
    return ref;
}

static HRESULT WINAPI factory_GetIids( IActivationFactory *iface, ULONG *iid_count, IID **iids )
{
    IID factory_iids[] =
    {
        IID_IActivationFactory,
        IID_IBufferFactory,
        IID_IBuffer,
    };
    IID *array = CoTaskMemAlloc( sizeof(factory_iids) );

    TRACE( "iface %p, iid_count %p, iids %p.\n", iface, iid_count, iids );

    if (!array) return E_OUTOFMEMORY;
    memcpy( array, factory_iids, sizeof(factory_iids) );
    *iid_count = ARRAY_SIZE(factory_iids);
    *iids = array;
    return S_OK;
}

static HRESULT WINAPI factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *class_name )
{
    static const WCHAR name[] = L"Windows.Storage.Streams.Buffer";

    return WindowsCreateString( name, ARRAY_SIZE(name) - 1, class_name );
}

static HRESULT WINAPI factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *trust_level )
{
    *trust_level = FullTrust;
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

struct buffer
{
    IBuffer IBuffer_iface;
    IBufferByteAccess IBufferByteAccess_iface;
    LONG ref;

    UINT32 capacity;
    UINT32 length;
    BYTE data[];
};

C_ASSERT( offsetof( struct buffer, data ) <= sizeof( struct buffer ) );

static inline struct buffer *impl_from_IBuffer( IBuffer *iface )
{
    return CONTAINING_RECORD( iface, struct buffer, IBuffer_iface );
}

static HRESULT WINAPI buffer_QueryInterface( IBuffer *iface, REFIID iid, void **out )
{
    struct buffer *impl = impl_from_IBuffer( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IBuffer ))
    {
        *out = &impl->IBuffer_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IBufferByteAccess ))
    {
        *out = &impl->IBufferByteAccess_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI buffer_AddRef( IBuffer *iface )
{
    struct buffer *impl = impl_from_IBuffer( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p increasing refcount to %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI buffer_Release( IBuffer *iface )
{
    struct buffer *impl = impl_from_IBuffer( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    TRACE( "iface %p decreasing refcount to %lu.\n", iface, ref );

    if (!ref) free( impl );
    return ref;
}

static HRESULT WINAPI buffer_GetIids( IBuffer *iface, ULONG *iid_count, IID **iids )
{
    IID instance_iids[] =
    {
        IID_IBuffer,
        IID_IBufferByteAccess,
    };
    IID *array = CoTaskMemAlloc( sizeof(instance_iids) );

    TRACE( "iface %p, iid_count %p, iids %p.\n", iface, iid_count, iids );

    if (!array) return E_OUTOFMEMORY;
    memcpy( array, instance_iids, sizeof(instance_iids) );
    *iid_count = ARRAY_SIZE(instance_iids);
    *iids = array;
    return S_OK;
}

static HRESULT WINAPI buffer_GetRuntimeClassName( IBuffer *iface, HSTRING *class_name )
{
    static const WCHAR name[] = L"Windows.Storage.Streams.Buffer";

    return WindowsCreateString( name, ARRAY_SIZE(name) - 1, class_name );
}

static HRESULT WINAPI buffer_GetTrustLevel( IBuffer *iface, TrustLevel *trust_level )
{
    *trust_level = FullTrust;
    return S_OK;
}

static HRESULT WINAPI buffer_get_Capacity( IBuffer *iface, UINT32 *value )
{
    struct buffer *impl = impl_from_IBuffer( iface );

    TRACE( "iface %p, value %p\n", iface, value );

    *value = impl->capacity;
    return S_OK;
}

static HRESULT WINAPI buffer_get_Length( IBuffer *iface, UINT32 *value )
{
    struct buffer *impl = impl_from_IBuffer( iface );

    TRACE( "iface %p, value %p\n", iface, value );

    *value = impl->length;
    return S_OK;
}

static HRESULT WINAPI buffer_put_Length( IBuffer *iface, UINT32 value )
{
    struct buffer *impl = impl_from_IBuffer( iface );

    TRACE( "iface %p, value %u\n", iface, value );

    if (value > impl->capacity) return E_INVALIDARG;

    impl->length = value;
    return S_OK;
}

static const struct IBufferVtbl buffer_vtbl =
{
    buffer_QueryInterface,
    buffer_AddRef,
    buffer_Release,
    /* IInspectable methods */
    buffer_GetIids,
    buffer_GetRuntimeClassName,
    buffer_GetTrustLevel,
    /* IBuffer methods */
    buffer_get_Capacity,
    buffer_get_Length,
    buffer_put_Length,
};

static inline struct buffer *impl_from_IBufferByteAccess( IBufferByteAccess *iface )
{
    return CONTAINING_RECORD( iface, struct buffer, IBufferByteAccess_iface );
}

static HRESULT WINAPI buffer_byte_access_QueryInterface( IBufferByteAccess *iface, REFIID iid, void **out )
{
    struct buffer *impl = impl_from_IBufferByteAccess( iface );
    return IBuffer_QueryInterface( &impl->IBuffer_iface, iid, out );
}

static ULONG WINAPI buffer_byte_access_AddRef( IBufferByteAccess *iface )
{
    struct buffer *impl = impl_from_IBufferByteAccess( iface );
    return IBuffer_AddRef( &impl->IBuffer_iface );
}

static ULONG WINAPI buffer_byte_access_Release( IBufferByteAccess *iface )
{
    struct buffer *impl = impl_from_IBufferByteAccess( iface );
    return IBuffer_Release( &impl->IBuffer_iface );
}

static HRESULT WINAPI buffer_byte_access_Buffer( IBufferByteAccess *iface, byte **value )
{
    struct buffer *impl = impl_from_IBufferByteAccess( iface );

    TRACE( "iface %p, value %p\n", iface, value );

    *value = impl->data;
    return S_OK;
}

static const struct IBufferByteAccessVtbl buffer_byte_access_vtbl =
{
    buffer_byte_access_QueryInterface,
    buffer_byte_access_AddRef,
    buffer_byte_access_Release,
    /* IBufferByteAccess methods */
    buffer_byte_access_Buffer,
};

DEFINE_IINSPECTABLE( buffer_factory_statics, IBufferFactory, struct buffer_factory_statics, IActivationFactory_iface )

static HRESULT WINAPI buffer_factory_statics_Create( IBufferFactory *iface, UINT32 capacity, IBuffer **value )
{
    struct buffer *impl;

    TRACE( "iface %p, capacity %u, value %p\n", iface, capacity, value );

    *value = NULL;

    if (!(impl = malloc( offsetof( struct buffer, data[capacity] ) ))) return E_OUTOFMEMORY;

    impl->IBuffer_iface.lpVtbl = &buffer_vtbl;
    impl->IBufferByteAccess_iface.lpVtbl = &buffer_byte_access_vtbl;
    impl->ref = 1;
    impl->capacity = capacity;
    impl->length = 0;

    *value = &impl->IBuffer_iface;
    TRACE( "created IBuffer %p.\n", *value );
    return S_OK;
}

static const struct IBufferFactoryVtbl buffer_factory_statics_vtbl =
{
    buffer_factory_statics_QueryInterface,
    buffer_factory_statics_AddRef,
    buffer_factory_statics_Release,
    /* IInspectable methods */
    buffer_factory_statics_GetIids,
    buffer_factory_statics_GetRuntimeClassName,
    buffer_factory_statics_GetTrustLevel,
    /* IBufferFactory methods */
    buffer_factory_statics_Create,
};

/*
 * Windows aggregates the class default interface (IBuffer) on the factory
 * object: QI for IID_IBuffer on the factory succeeds (CsWinRT probes the
 * factory for the class interfaces). The factory is not an instance; instance
 * method calls fail.
 */
DEFINE_IINSPECTABLE_( buffer_factory_def, IBuffer, struct buffer_factory_statics, impl_from_buffer_factory_def,
                      IBuffer_iface, &impl->IActivationFactory_iface )

static HRESULT WINAPI buffer_factory_def_get_Capacity( IBuffer *iface, UINT32 *value ) { return E_FAIL; }
static HRESULT WINAPI buffer_factory_def_get_Length( IBuffer *iface, UINT32 *value ) { return E_FAIL; }
static HRESULT WINAPI buffer_factory_def_put_Length( IBuffer *iface, UINT32 value ) { return E_FAIL; }

static const struct IBufferVtbl buffer_factory_def_vtbl =
{
    buffer_factory_def_QueryInterface,
    buffer_factory_def_AddRef,
    buffer_factory_def_Release,
    buffer_factory_def_GetIids,
    buffer_factory_def_GetRuntimeClassName,
    buffer_factory_def_GetTrustLevel,
    buffer_factory_def_get_Capacity,
    buffer_factory_def_get_Length,
    buffer_factory_def_put_Length,
};

static struct buffer_factory_statics buffer_factory_statics =
{
    {&factory_vtbl},
    {&buffer_factory_statics_vtbl},
    {&buffer_factory_def_vtbl},
    1,
};

IActivationFactory *buffer_activation_factory = &buffer_factory_statics.IActivationFactory_iface;
