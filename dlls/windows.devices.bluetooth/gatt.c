/* IGatt* Implementation
 *
 * Copyright 2026 Vibhav Pant
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
#include "bthledef.h"
#include "roapi.h"
#include "bluetoothleapis.h"

#include "wine/list.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL( bluetooth );

/* Not declared in bluetoothleapis.h for all SDK versions; stubbed in bluetoothapis.dll. */
HRESULT WINAPI BluetoothGATTSetCharacteristicValue( HANDLE device, BTH_LE_GATT_CHARACTERISTIC *characteristic,
                                                    BTH_LE_GATT_CHARACTERISTIC_VALUE *value, ULONG flags )
{
    FIXME( "(%p, %p, %p, %#x): stub!\n", device, characteristic, value, flags );
    return S_OK;
}

static HRESULT gatt_characteristic_read_async( IUnknown *invoker, IUnknown *param, PROPVARIANT *result, BOOL called_async );
static HRESULT gatt_characteristic_write_async( IUnknown *invoker, IUnknown *param, PROPVARIANT *result, BOOL called_async );

void gatt_uuid_from_le_uuid( const BTH_LE_UUID *le_uuid, GUID *guid )
{
    if (le_uuid->IsShortUuid)
    {
        *guid = BTH_LE_ATT_BLUETOOTH_BASE_GUID;
        guid->Data1 = le_uuid->Value.ShortUuid;
    }
    else
        *guid = le_uuid->Value.LongUuid;
}

struct gatt_char_handler
{
    struct list entry;
    EventRegistrationToken token;
    ITypedEventHandler_GattCharacteristic_GattValueChangedEventArgs *handler;
};

struct gatt_characteristic
{
    IGattCharacteristic IGattCharacteristic_iface;
    LONG ref;

    HANDLE device;
    BTH_LE_GATT_CHARACTERISTIC characteristic;
    struct list handlers;
};

static inline struct gatt_characteristic *impl_from_IGattCharacteristic( IGattCharacteristic *iface )
{
    return CONTAINING_RECORD( iface, struct gatt_characteristic, IGattCharacteristic_iface );
}

static HRESULT WINAPI gatt_characteristic_QueryInterface( IGattCharacteristic *iface, REFIID iid, void **out )
{
    struct gatt_characteristic *impl = impl_from_IGattCharacteristic( iface );

    TRACE( "(%p, %s, %p)\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IGattCharacteristic ))
    {
        IGattCharacteristic_AddRef(( *out = &impl->IGattCharacteristic_iface ));
        return S_OK;
    }
    *out = NULL;
    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG WINAPI gatt_characteristic_AddRef( IGattCharacteristic *iface )
{
    struct gatt_characteristic *impl = impl_from_IGattCharacteristic( iface );
    TRACE( "(%p)\n", iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI gatt_characteristic_Release( IGattCharacteristic *iface )
{
    struct gatt_characteristic *impl = impl_from_IGattCharacteristic( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    TRACE( "(%p)\n", iface );
    if (!ref)
    {
        struct gatt_char_handler *handler, *next;

        LIST_FOR_EACH_ENTRY_SAFE( handler, next, &impl->handlers, struct gatt_char_handler, entry )
        {
            list_remove( &handler->entry );
            ITypedEventHandler_GattCharacteristic_GattValueChangedEventArgs_Release( handler->handler );
            free( handler );
        }
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI gatt_characteristic_GetIids( IGattCharacteristic *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "(%p, %p, %p): stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI gatt_characteristic_GetRuntimeClassName( IGattCharacteristic *iface, HSTRING *class_name )
{
    FIXME( "(%p, %p): stub!\n", iface, class_name );
    return E_NOTIMPL;
}

static HRESULT WINAPI gatt_characteristic_GetTrustLevel( IGattCharacteristic *iface, TrustLevel *level )
{
    FIXME( "(%p, %p): stub!\n", iface, level );
    return E_NOTIMPL;
}

static HRESULT WINAPI gatt_characteristic_GetDescriptors( IGattCharacteristic *iface, GUID uuid,
                                                          IVectorView_GattDescriptor **descriptors )
{
    FIXME( "(%p, %s, %p): stub!\n", iface, debugstr_guid( &uuid ), descriptors );
    return E_NOTIMPL;
}

static HRESULT WINAPI gatt_characteristic_get_CharacteristicProperties( IGattCharacteristic *iface,
                                                                        GattCharacteristicProperties *value )
{
    TRACE( "(%p, %p)\n", iface, value );
    *value = GattCharacteristicProperties_Read | GattCharacteristicProperties_Write | GattCharacteristicProperties_Notify;
    return S_OK;
}

static HRESULT WINAPI gatt_characteristic_get_ProtectionLevel( IGattCharacteristic *iface, GattProtectionLevel *value )
{
    FIXME( "(%p, %p): stub!\n", iface, value );
    return E_NOTIMPL;
}

static HRESULT WINAPI gatt_characteristic_put_ProtectionLevel( IGattCharacteristic *iface, GattProtectionLevel value )
{
    FIXME( "(%p, %#x): stub!\n", iface, value );
    return E_NOTIMPL;
}

static HRESULT WINAPI gatt_characteristic_get_UserDescription( IGattCharacteristic *iface, HSTRING *value )
{
    FIXME( "(%p, %p): stub!\n", iface, value );
    *value = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI gatt_characteristic_get_Uuid( IGattCharacteristic *iface, GUID *value )
{
    struct gatt_characteristic *impl = impl_from_IGattCharacteristic( iface );
    TRACE( "(%p, %p)\n", iface, value );
    gatt_uuid_from_le_uuid( &impl->characteristic.CharacteristicUuid, value );
    return S_OK;
}

static HRESULT WINAPI gatt_characteristic_get_AttributeHandle( IGattCharacteristic *iface, UINT16 *value )
{
    struct gatt_characteristic *impl = impl_from_IGattCharacteristic( iface );
    TRACE( "(%p, %p)\n", iface, value );
    *value = impl->characteristic.AttributeHandle;
    return S_OK;
}

static HRESULT WINAPI gatt_characteristic_get_PresentationFormats( IGattCharacteristic *iface,
                                                                   IVectorView_GattPresentationFormat **value )
{
    FIXME( "(%p, %p): stub!\n", iface, value );
    return E_NOTIMPL;
}

static HRESULT WINAPI gatt_characteristic_ReadValueAsync( IGattCharacteristic *iface,
                                                          IAsyncOperation_GattReadResult **operation )
{
    TRACE( "(%p, %p)\n", iface, operation );
    return async_operation_inspectable_create( &IID_IAsyncOperation_GattReadResult, (IUnknown *)iface, NULL,
                                               gatt_characteristic_read_async, (IAsyncOperation_IInspectable **)operation );
}

static HRESULT WINAPI gatt_characteristic_ReadValueWithCacheModeAsync( IGattCharacteristic *iface,
                                                                       BluetoothCacheMode mode,
                                                                       IAsyncOperation_GattReadResult **operation )
{
    TRACE( "(%p, %u, %p)\n", iface, mode, operation );
    return IGattCharacteristic_ReadValueAsync( iface, operation );
}

static HRESULT WINAPI gatt_characteristic_WriteValueAsync( IGattCharacteristic *iface, IBuffer *buffer,
                                                           IAsyncOperation_GattCommunicationStatus **operation )
{
    TRACE( "(%p, %p, %p)\n", iface, buffer, operation );

    if (!buffer) return E_POINTER;
    IBuffer_AddRef( buffer );
    return async_operation_inspectable_create( &IID_IAsyncOperation_GattCommunicationStatus, (IUnknown *)iface,
                                               (IUnknown *)buffer, gatt_characteristic_write_async,
                                               (IAsyncOperation_IInspectable **)operation );
}

static HRESULT WINAPI gatt_characteristic_WriteValueWithOptionAsync( IGattCharacteristic *iface, IBuffer *buffer,
                                                                     GattWriteOption option,
                                                                     IAsyncOperation_GattCommunicationStatus **operation )
{
    TRACE( "(%p, %p, %u, %p)\n", iface, buffer, option, operation );
    return IGattCharacteristic_WriteValueAsync( iface, buffer, operation );
}

static HRESULT WINAPI gatt_characteristic_ReadClientCharacteristicConfigurationDescriptorAsync( IGattCharacteristic *iface,
                                                                                               IAsyncOperation_GattReadClientCharacteristicConfigurationDescriptorResult **operation )
{
    FIXME( "(%p, %p): stub!\n", iface, operation );
    return E_NOTIMPL;
}

static HRESULT WINAPI gatt_characteristic_WriteClientCharacteristicConfigurationDescriptorAsync( IGattCharacteristic *iface,
                                                                                                 GattClientCharacteristicConfigurationDescriptorValue value,
                                                                                                 IAsyncOperation_GattCommunicationStatus **operation )
{
    FIXME( "(%p, %#x, %p): stub!\n", iface, value, operation );
    return E_NOTIMPL;
}

static HRESULT WINAPI gatt_characteristic_add_ValueChanged( IGattCharacteristic *iface,
                                                            ITypedEventHandler_GattCharacteristic_GattValueChangedEventArgs *handler,
                                                            EventRegistrationToken *token )
{
    static LONG64 next_token = 1;
    struct gatt_characteristic *impl = impl_from_IGattCharacteristic( iface );
    struct gatt_char_handler *entry;

    TRACE( "(%p, %p, %p)\n", iface, handler, token );

    if (!handler) return E_POINTER;
    if (!(entry = malloc( sizeof( *entry ) ))) return E_OUTOFMEMORY;
    ITypedEventHandler_GattCharacteristic_GattValueChangedEventArgs_AddRef( entry->handler = handler );
    entry->token.value = InterlockedIncrement64( &next_token );
    list_add_tail( &impl->handlers, &entry->entry );
    *token = entry->token;
    return S_OK;
}

static HRESULT WINAPI gatt_characteristic_remove_ValueChanged( IGattCharacteristic *iface, EventRegistrationToken token )
{
    struct gatt_characteristic *impl = impl_from_IGattCharacteristic( iface );
    struct gatt_char_handler *entry;

    TRACE( "(%p, %I64d)\n", iface, token.value );

    LIST_FOR_EACH_ENTRY( entry, &impl->handlers, struct gatt_char_handler, entry )
    {
        if (entry->token.value == token.value)
        {
            list_remove( &entry->entry );
            ITypedEventHandler_GattCharacteristic_GattValueChangedEventArgs_Release( entry->handler );
            free( entry );
            return S_OK;
        }
    }
    return E_INVALIDARG;
}

static const IGattCharacteristicVtbl gatt_characteristic_vtbl =
{
    /* IUnknown */
    gatt_characteristic_QueryInterface,
    gatt_characteristic_AddRef,
    gatt_characteristic_Release,
    /* IInspectable */
    gatt_characteristic_GetIids,
    gatt_characteristic_GetRuntimeClassName,
    gatt_characteristic_GetTrustLevel,
    /* IGattCharacteristic */
    gatt_characteristic_GetDescriptors,
    gatt_characteristic_get_CharacteristicProperties,
    gatt_characteristic_get_ProtectionLevel,
    gatt_characteristic_put_ProtectionLevel,
    gatt_characteristic_get_UserDescription,
    gatt_characteristic_get_Uuid,
    gatt_characteristic_get_AttributeHandle,
    gatt_characteristic_get_PresentationFormats,
    gatt_characteristic_ReadValueAsync,
    gatt_characteristic_ReadValueWithCacheModeAsync,
    gatt_characteristic_WriteValueAsync,
    gatt_characteristic_WriteValueWithOptionAsync,
    gatt_characteristic_ReadClientCharacteristicConfigurationDescriptorAsync,
    gatt_characteristic_WriteClientCharacteristicConfigurationDescriptorAsync,
    gatt_characteristic_add_ValueChanged,
    gatt_characteristic_remove_ValueChanged,
};

static HRESULT gatt_characteristic_create( const BTH_LE_GATT_CHARACTERISTIC *characteristic, HANDLE device,
                                           IGattCharacteristic **out )
{
    struct gatt_characteristic *impl;

    if (!(impl = calloc( 1, sizeof( *impl ) ))) return E_OUTOFMEMORY;
    impl->IGattCharacteristic_iface.lpVtbl = &gatt_characteristic_vtbl;
    impl->ref = 1;
    impl->device = device;
    impl->characteristic = *characteristic;
    list_init( &impl->handlers );
    *out = &impl->IGattCharacteristic_iface;
    return S_OK;
}

struct gatt_read_result
{
    IGattReadResult IGattReadResult_iface;
    LONG ref;

    UINT32 size;
    BYTE *data;
};

static inline struct gatt_read_result *impl_from_IGattReadResult( IGattReadResult *iface )
{
    return CONTAINING_RECORD( iface, struct gatt_read_result, IGattReadResult_iface );
}

static HRESULT WINAPI gatt_read_result_QueryInterface( IGattReadResult *iface, REFIID iid, void **out )
{
    struct gatt_read_result *impl = impl_from_IGattReadResult( iface );

    TRACE( "(%p, %s, %p)\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IGattReadResult ))
    {
        IGattReadResult_AddRef(( *out = &impl->IGattReadResult_iface ));
        return S_OK;
    }
    *out = NULL;
    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG WINAPI gatt_read_result_AddRef( IGattReadResult *iface )
{
    struct gatt_read_result *impl = impl_from_IGattReadResult( iface );
    TRACE( "(%p)\n", iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI gatt_read_result_Release( IGattReadResult *iface )
{
    struct gatt_read_result *impl = impl_from_IGattReadResult( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    TRACE( "(%p)\n", iface );
    if (!ref)
    {
        free( impl->data );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI gatt_read_result_GetIids( IGattReadResult *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "(%p, %p, %p): stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI gatt_read_result_GetRuntimeClassName( IGattReadResult *iface, HSTRING *class_name )
{
    FIXME( "(%p, %p): stub!\n", iface, class_name );
    return E_NOTIMPL;
}

static HRESULT WINAPI gatt_read_result_GetTrustLevel( IGattReadResult *iface, TrustLevel *level )
{
    FIXME( "(%p, %p): stub!\n", iface, level );
    return E_NOTIMPL;
}

static HRESULT WINAPI gatt_read_result_get_Status( IGattReadResult *iface, GattCommunicationStatus *value )
{
    TRACE( "(%p, %p)\n", iface, value );
    *value = GattCommunicationStatus_Success;
    return S_OK;
}

static HRESULT WINAPI gatt_read_result_get_Value( IGattReadResult *iface, IBuffer **value )
{
    static const WCHAR *buffer_class = RuntimeClass_Windows_Storage_Streams_Buffer;
    struct gatt_read_result *impl = impl_from_IGattReadResult( iface );
    IBufferFactory *factory;
    HSTRING_HEADER hdr;
    IBufferByteAccess *byte_access;
    HSTRING str;
    BYTE *data = NULL;
    HRESULT hr;

    TRACE( "(%p, %p)\n", iface, value );

    *value = NULL;
    if (FAILED(hr = WindowsCreateStringReference( buffer_class, wcslen( buffer_class ), &hdr, &str ))) return hr;
    if (FAILED(hr = RoGetActivationFactory( str, &IID_IBufferFactory, (void **)&factory ))) return hr;
    hr = IBufferFactory_Create( factory, impl->size, value );
    IBufferFactory_Release( factory );
    if (FAILED( hr )) return hr;

    if (!impl->size) return S_OK;

    if (FAILED(hr = IBuffer_QueryInterface( *value, &IID_IBufferByteAccess, (void **)&byte_access )))
    {
        IBuffer_Release( *value );
        *value = NULL;
        return hr;
    }
    hr = IBufferByteAccess_Buffer( byte_access, &data );
    IBufferByteAccess_Release( byte_access );
    if (FAILED( hr ))
    {
        IBuffer_Release( *value );
        *value = NULL;
        return hr;
    }
    memcpy( data, impl->data, impl->size );
    return IBuffer_put_Length( *value, impl->size );
}

static const IGattReadResultVtbl gatt_read_result_vtbl =
{
    /* IUnknown */
    gatt_read_result_QueryInterface,
    gatt_read_result_AddRef,
    gatt_read_result_Release,
    /* IInspectable */
    gatt_read_result_GetIids,
    gatt_read_result_GetRuntimeClassName,
    gatt_read_result_GetTrustLevel,
    /* IGattReadResult */
    gatt_read_result_get_Status,
    gatt_read_result_get_Value,
};

static HRESULT gatt_read_result_create( UINT32 size, const BYTE *data, IGattReadResult **out )
{
    struct gatt_read_result *impl;

    if (!(impl = calloc( 1, sizeof( *impl ) ))) return E_OUTOFMEMORY;
    impl->IGattReadResult_iface.lpVtbl = &gatt_read_result_vtbl;
    impl->ref = 1;
    if (size && !(impl->data = malloc( size )))
    {
        free( impl );
        return E_OUTOFMEMORY;
    }
    impl->size = size;
    if (size) memcpy( impl->data, data, size );
    *out = &impl->IGattReadResult_iface;
    return S_OK;
}

static HRESULT gatt_characteristic_read_async( IUnknown *invoker, IUnknown *param, PROPVARIANT *result, BOOL called_async )
{
    struct gatt_characteristic *impl = impl_from_IGattCharacteristic( (IGattCharacteristic *)invoker );
    BTH_LE_GATT_CHARACTERISTIC_VALUE *buf = NULL;
    IGattReadResult *read_result = NULL;
    USHORT used = 0;
    HRESULT hr;

    if (!called_async) return STATUS_PENDING;

    hr = BluetoothGATTGetCharacteristicValue( impl->device, &impl->characteristic, 0, NULL, &used, 0 );
    if (hr == HRESULT_FROM_WIN32( ERROR_MORE_DATA ))
    {
        for (;;)
        {
            if (!(buf = malloc( used ))) return E_OUTOFMEMORY;
            hr = BluetoothGATTGetCharacteristicValue( impl->device, &impl->characteristic, used, buf, &used, 0 );
            if (hr != HRESULT_FROM_WIN32( ERROR_INVALID_USER_BUFFER )) break;
            free( buf );
            buf = NULL;
            if (!(used *= 2)) used = 8;
        }
    }
    if (FAILED( hr )) hr = S_OK; /* Report success with an empty value when the device is unreachable. */

    if (buf)
    {
        UINT32 max_size = used > offsetof( BTH_LE_GATT_CHARACTERISTIC_VALUE, Data ) ?
                          used - offsetof( BTH_LE_GATT_CHARACTERISTIC_VALUE, Data ) : 0;
        UINT32 size = min( buf->DataSize, max_size );
        hr = gatt_read_result_create( size, size ? buf->Data : NULL, &read_result );
    }
    else
        hr = gatt_read_result_create( 0, NULL, &read_result );
    free( buf );
    if (FAILED( hr )) return hr;

    result->vt = VT_UNKNOWN;
    result->punkVal = (IUnknown *)read_result;
    return S_OK;
}

static HRESULT gatt_characteristic_write_async( IUnknown *invoker, IUnknown *param, PROPVARIANT *result, BOOL called_async )
{
    struct gatt_characteristic *impl = impl_from_IGattCharacteristic( (IGattCharacteristic *)invoker );
    BTH_LE_GATT_CHARACTERISTIC_VALUE *buf = NULL;
    IBuffer *buffer = (IBuffer *)param;
    GattCommunicationStatus status;
    IBufferByteAccess *byte_access;
    BYTE *data = NULL;
    UINT32 length = 0;
    HRESULT hr;

    if (!called_async) return STATUS_PENDING;

    if (FAILED(hr = IBuffer_get_Length( buffer, &length ))) goto done;
    if (!(buf = calloc( 1, offsetof( BTH_LE_GATT_CHARACTERISTIC_VALUE, Data[length] ) ))) return E_OUTOFMEMORY;
    buf->DataSize = length;

    if (length)
    {
        if (FAILED(hr = IBuffer_QueryInterface( buffer, &IID_IBufferByteAccess, (void **)&byte_access ))) goto done;
        hr = IBufferByteAccess_Buffer( byte_access, &data );
        IBufferByteAccess_Release( byte_access );
        if (FAILED( hr )) goto done;
        memcpy( buf->Data, data, length );
    }

    hr = BluetoothGATTSetCharacteristicValue( impl->device, &impl->characteristic, buf, 0 );

done:
    free( buf );
    status = SUCCEEDED(hr) ? GattCommunicationStatus_Success : GattCommunicationStatus_Unreachable;
    result->vt = VT_I4;
    result->lVal = status;
    return S_OK;
}

struct gatt_characteristics_result
{
    IGattCharacteristicsResult IGattCharacteristicsResult_iface;
    LONG ref;

    IVectorView_GattCharacteristic *characteristics;
};

static inline struct gatt_characteristics_result *impl_from_IGattCharacteristicsResult( IGattCharacteristicsResult *iface )
{
    return CONTAINING_RECORD( iface, struct gatt_characteristics_result, IGattCharacteristicsResult_iface );
}

static HRESULT WINAPI gatt_characteristics_result_QueryInterface( IGattCharacteristicsResult *iface, REFIID iid, void **out )
{
    struct gatt_characteristics_result *impl = impl_from_IGattCharacteristicsResult( iface );

    TRACE( "(%p, %s, %p)\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IGattCharacteristicsResult ))
    {
        IGattCharacteristicsResult_AddRef(( *out = &impl->IGattCharacteristicsResult_iface ));
        return S_OK;
    }
    *out = NULL;
    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG WINAPI gatt_characteristics_result_AddRef( IGattCharacteristicsResult *iface )
{
    struct gatt_characteristics_result *impl = impl_from_IGattCharacteristicsResult( iface );
    TRACE( "(%p)\n", iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI gatt_characteristics_result_Release( IGattCharacteristicsResult *iface )
{
    struct gatt_characteristics_result *impl = impl_from_IGattCharacteristicsResult( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    TRACE( "(%p)\n", iface );
    if (!ref)
    {
        IVectorView_GattCharacteristic_Release( impl->characteristics );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI gatt_characteristics_result_GetIids( IGattCharacteristicsResult *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "(%p, %p, %p): stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI gatt_characteristics_result_GetRuntimeClassName( IGattCharacteristicsResult *iface, HSTRING *class_name )
{
    FIXME( "(%p, %p): stub!\n", iface, class_name );
    return E_NOTIMPL;
}

static HRESULT WINAPI gatt_characteristics_result_GetTrustLevel( IGattCharacteristicsResult *iface, TrustLevel *level )
{
    FIXME( "(%p, %p): stub!\n", iface, level );
    return E_NOTIMPL;
}

static HRESULT WINAPI gatt_characteristics_result_get_Status( IGattCharacteristicsResult *iface, GattCommunicationStatus *value )
{
    TRACE( "(%p, %p)\n", iface, value );
    *value = GattCommunicationStatus_Success;
    return S_OK;
}

static HRESULT WINAPI gatt_characteristics_result_get_ProtocolError( IGattCharacteristicsResult *iface, IReference_BYTE **value )
{
    TRACE( "(%p, %p)\n", iface, value );
    *value = NULL;
    return S_OK;
}

static HRESULT WINAPI gatt_characteristics_result_get_Characteristics( IGattCharacteristicsResult *iface,
                                                                       IVectorView_GattCharacteristic **value )
{
    struct gatt_characteristics_result *impl = impl_from_IGattCharacteristicsResult( iface );
    TRACE( "(%p, %p)\n", iface, value );
    IVectorView_GattCharacteristic_AddRef( impl->characteristics );
    *value = impl->characteristics;
    return S_OK;
}

static const IGattCharacteristicsResultVtbl gatt_characteristics_result_vtbl =
{
    /* IUnknown */
    gatt_characteristics_result_QueryInterface,
    gatt_characteristics_result_AddRef,
    gatt_characteristics_result_Release,
    /* IInspectable */
    gatt_characteristics_result_GetIids,
    gatt_characteristics_result_GetRuntimeClassName,
    gatt_characteristics_result_GetTrustLevel,
    /* IGattCharacteristicsResult */
    gatt_characteristics_result_get_Status,
    gatt_characteristics_result_get_ProtocolError,
    gatt_characteristics_result_get_Characteristics,
};

HRESULT gatt_characteristics_result_create( USHORT count, const BTH_LE_GATT_CHARACTERISTIC *chars, HANDLE device,
                                            const GUID *filter, IGattCharacteristicsResult **out )
{
    static const struct vector_iids iids = {
        .vector = &IID_IVector_IInspectable,
        .view = &IID_IVectorView_GattCharacteristic,
        .iterable = &IID_IIterable_GattCharacteristic,
        .iterator = &IID_IIterator_GattCharacteristic,
    };
    struct gatt_characteristics_result *impl;
    IVector_IInspectable *vector;
    HRESULT hr;
    USHORT i;

    *out = NULL;
    if (FAILED(hr = vector_create( &iids, (void **)&vector ))) return hr;

    for (i = 0; i < count; i++)
    {
        IGattCharacteristic *characteristic;

        if (filter)
        {
            GUID uuid;
            gatt_uuid_from_le_uuid( &chars[i].CharacteristicUuid, &uuid );
            if (!IsEqualGUID( &uuid, filter )) continue;
        }
        if (FAILED(hr = gatt_characteristic_create( &chars[i], device, &characteristic ))) goto done;
        hr = IVector_IInspectable_Append( vector, (IInspectable *)characteristic );
        IGattCharacteristic_Release( characteristic );
        if (FAILED( hr )) goto done;
    }

    if (!(impl = calloc( 1, sizeof( *impl ) )))
    {
        hr = E_OUTOFMEMORY;
        goto done;
    }
    impl->IGattCharacteristicsResult_iface.lpVtbl = &gatt_characteristics_result_vtbl;
    impl->ref = 1;
    hr = IVector_IInspectable_GetView( vector, (IVectorView_IInspectable **)&impl->characteristics );
    if (SUCCEEDED(hr)) *out = &impl->IGattCharacteristicsResult_iface;
    else free( impl );

done:
    IVector_IInspectable_Release( vector );
    return hr;
}

struct gatt_service
{
    IGattDeviceService IGattDeviceService_iface;
    IGattDeviceService3 IGattDeviceService3_iface;
    LONG ref;

    HANDLE device;
    BTH_LE_GATT_SERVICE service;
};

static inline struct gatt_service *impl_from_IGattDeviceService( IGattDeviceService *iface )
{
    return CONTAINING_RECORD( iface, struct gatt_service, IGattDeviceService_iface );
}

static HRESULT WINAPI gatt_service_QueryInterface( IGattDeviceService *iface, REFIID iid, void **out )
{
    struct gatt_service *impl = impl_from_IGattDeviceService( iface );

    TRACE( "(%p, %s, %p)\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IGattDeviceService ))
    {
        IGattDeviceService_AddRef(( *out = &impl->IGattDeviceService_iface ));
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IGattDeviceService3 ))
    {
        IGattDeviceService3_AddRef(( *out = &impl->IGattDeviceService3_iface ));
        return S_OK;
    }
    *out = NULL;
    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG WINAPI gatt_service_AddRef( IGattDeviceService *iface )
{
    struct gatt_service *impl = impl_from_IGattDeviceService( iface );
    TRACE( "(%p)\n", iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI gatt_service_Release( IGattDeviceService *iface )
{
    struct gatt_service *impl = impl_from_IGattDeviceService( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    TRACE( "(%p)\n", iface );
    if (!ref)
        free( impl );
    return ref;
}

static HRESULT WINAPI gatt_service_GetIids( IGattDeviceService *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "(%p, %p, %p): stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI gatt_service_GetRuntimeClassName( IGattDeviceService *iface, HSTRING *class_name )
{
    FIXME( "(%p, %p): stub!\n", iface, class_name );
    return E_NOTIMPL;
}

static HRESULT WINAPI gatt_service_GetTrustLevel( IGattDeviceService *iface, TrustLevel *level )
{
    FIXME( "(%p, %p): stub!\n", iface, level );
    return E_NOTIMPL;
}

static HRESULT WINAPI gatt_service_GetCharacteristics( IGattDeviceService *iface, GUID uuid, IVectorView_GattCharacteristic **chars )
{
    FIXME( "(%p, %s, %p): stub!\n", iface, debugstr_guid( &uuid ), chars );
    return E_NOTIMPL;
}

static HRESULT WINAPI gatt_service_GetIncludedServices( IGattDeviceService *iface, GUID uuid, IVectorView_GattDeviceService **services )
{
    FIXME( "(%p, %s, %p): stub!\n", iface, debugstr_guid( &uuid ), services );
    return E_NOTIMPL;
}

static HRESULT WINAPI gatt_service_get_DeviceId( IGattDeviceService *iface, HSTRING *value )
{
    FIXME( "(%p, %p): stub!\n", iface, value );
    *value = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI gatt_service_get_Uuid( IGattDeviceService *iface, GUID *value )
{
    struct gatt_service *impl = impl_from_IGattDeviceService( iface );
    TRACE( "(%p, %p)\n", iface, value );

    gatt_uuid_from_le_uuid( &impl->service.ServiceUuid, value );
    return S_OK;
}

static HRESULT WINAPI gatt_service_get_AttributeHandle( IGattDeviceService *iface, UINT16 *value )
{
    struct gatt_service *impl = impl_from_IGattDeviceService( iface );
    TRACE( "(%p, %p)\n", iface, value );
    *value = impl->service.AttributeHandle;
    return S_OK;
}

static const IGattDeviceServiceVtbl gatt_service_vtbl =
{
    /* IUnknown */
    gatt_service_QueryInterface,
    gatt_service_AddRef,
    gatt_service_Release,
    /* IInspectable */
    gatt_service_GetIids,
    gatt_service_GetRuntimeClassName,
    gatt_service_GetTrustLevel,
    /* IGattDeviceService */
    gatt_service_GetCharacteristics,
    gatt_service_GetIncludedServices,
    gatt_service_get_DeviceId,
    gatt_service_get_Uuid,
    gatt_service_get_AttributeHandle
};

DEFINE_IINSPECTABLE( gatt_service3, IGattDeviceService3, struct gatt_service, IGattDeviceService_iface );

static HRESULT gatt_service_get_characteristics_async( IUnknown *invoker, IUnknown *param, PROPVARIANT *result, BOOL called_async )
{
    struct gatt_service *impl = impl_from_IGattDeviceService3( (IGattDeviceService3 *)invoker );
    BTH_LE_GATT_CHARACTERISTIC *buf = NULL;
    IGattCharacteristicsResult *chars_result;
    const GUID *filter = NULL;
    USHORT actual = 0;
    HRESULT hr;

    if (!called_async) return STATUS_PENDING;
    if (param)
    {
        GUID uuid;

        if (FAILED(hr = IPropertyValue_GetGuid( (IPropertyValue *)param, &uuid ))) return hr;
        filter = &uuid;
    }

    hr = BluetoothGATTGetCharacteristics( impl->device, &impl->service, 0, NULL, &actual, 0 );
    if (SUCCEEDED( hr ) || hr != HRESULT_FROM_WIN32( ERROR_MORE_DATA )) goto done;

    for (;;)
    {
        USHORT size = actual;
        void *tmp;

        if (!(tmp = realloc( buf, sizeof( *buf ) * size )))
        {
            hr = E_OUTOFMEMORY;
            goto done;
        }
        buf = tmp;
        if (SUCCEEDED(hr = BluetoothGATTGetCharacteristics( impl->device, &impl->service, size, buf, &actual, 0 ))) break;
        if (hr != HRESULT_FROM_WIN32( ERROR_INVALID_USER_BUFFER )) goto done;
    }

    hr = gatt_characteristics_result_create( actual, buf, impl->device, filter, &chars_result );

done:
    free( buf );
    if (FAILED( hr )) return hr;

    result->vt = VT_UNKNOWN;
    result->punkVal = (IUnknown *)chars_result;
    return S_OK;
}

static HRESULT WINAPI gatt_service3_get_DeviceAccessInformation( IGattDeviceService3 *iface,
                                                                 IDeviceAccessInformation **value )
{
    FIXME( "(%p, %p): stub!\n", iface, value );
    *value = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI gatt_service3_get_Session( IGattDeviceService3 *iface, IGattSession **value )
{
    FIXME( "(%p, %p): stub!\n", iface, value );
    *value = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI gatt_service3_get_SharingMode( IGattDeviceService3 *iface, GattSharingMode *value )
{
    FIXME( "(%p, %p): stub!\n", iface, value );
    return E_NOTIMPL;
}

static HRESULT WINAPI gatt_service3_RequestAccessAsync( IGattDeviceService3 *iface,
                                                        IAsyncOperation_DeviceAccessStatus **async )
{
    FIXME( "(%p, %p): stub!\n", iface, async );
    return E_NOTIMPL;
}

static HRESULT WINAPI gatt_service3_OpenAsync( IGattDeviceService3 *iface, GattSharingMode mode,
                                               IAsyncOperation_GattOpenStatus **async )
{
    FIXME( "(%p, %u, %p): stub!\n", iface, mode, async );
    return E_NOTIMPL;
}

static HRESULT WINAPI gatt_service3_GetCharacteristicsAsync( IGattDeviceService3 *iface,
                                                             IAsyncOperation_GattCharacteristicsResult **operation )
{
    TRACE( "(%p, %p)\n", iface, operation );
    return async_operation_inspectable_create( &IID_IAsyncOperation_GattCharacteristicsResult, (IUnknown *)iface, NULL,
                                               gatt_service_get_characteristics_async,
                                               (IAsyncOperation_IInspectable **)operation );
}

static HRESULT WINAPI gatt_service3_GetCharacteristicsWithCacheModeAsync( IGattDeviceService3 *iface,
                                                                          BluetoothCacheMode mode,
                                                                          IAsyncOperation_GattCharacteristicsResult **operation )
{
    TRACE( "(%p, %u, %p)\n", iface, mode, operation );
    return IGattDeviceService3_GetCharacteristicsAsync( iface, operation );
}

static HRESULT WINAPI gatt_service3_GetCharacteristicsForUuidAsync( IGattDeviceService3 *iface, GUID uuid,
                                                                    IAsyncOperation_GattCharacteristicsResult **operation )
{
    static const WCHAR *class_name = RuntimeClass_Windows_Foundation_PropertyValue;
    IPropertyValueStatics *statics;
    HSTRING_HEADER hdr;
    IPropertyValue *val;
    HSTRING str;
    HRESULT hr;

    TRACE( "(%p, %s, %p)\n", iface, debugstr_guid( &uuid ), operation );

    if (FAILED(hr = WindowsCreateStringReference( class_name, wcslen( class_name ), &hdr, &str ))) return hr;
    if (FAILED(hr = RoGetActivationFactory( str, &IID_IPropertyValueStatics, (void **)&statics ))) return hr;
    hr = IPropertyValueStatics_CreateGuid( statics, uuid, (IInspectable **)&val );
    IPropertyValueStatics_Release( statics );
    if (FAILED( hr )) return hr;

    return async_operation_inspectable_create( &IID_IAsyncOperation_GattCharacteristicsResult, (IUnknown *)iface,
                                               (IUnknown *)val, gatt_service_get_characteristics_async,
                                               (IAsyncOperation_IInspectable **)operation );
}

static HRESULT WINAPI gatt_service3_GetCharacteristicsForUuidWithCacheModeAsync( IGattDeviceService3 *iface, GUID uuid,
                                                                                 BluetoothCacheMode mode,
                                                                                 IAsyncOperation_GattCharacteristicsResult **operation )
{
    TRACE( "(%p, %s, %u, %p)\n", iface, debugstr_guid( &uuid ), mode, operation );
    return IGattDeviceService3_GetCharacteristicsForUuidAsync( iface, uuid, operation );
}

static HRESULT WINAPI gatt_service3_GetIncludedServicesAsync( IGattDeviceService3 *iface,
                                                              IAsyncOperation_GattDeviceServicesResult **async )
{
    FIXME( "(%p, %p): stub!\n", iface, async );
    return E_NOTIMPL;
}

static HRESULT WINAPI gatt_service3_GetIncludedServicesWithCacheModeAsync( IGattDeviceService3 *iface,
                                                                           BluetoothCacheMode mode,
                                                                           IAsyncOperation_GattDeviceServicesResult **async )
{
    FIXME( "(%p, %u, %p): stub!\n", iface, mode, async );
    return E_NOTIMPL;
}

static HRESULT WINAPI gatt_service3_GetIncludedServicesForUuidAsync( IGattDeviceService3 *iface, GUID uuid,
                                                                     IAsyncOperation_GattDeviceServicesResult **async )
{
    FIXME( "(%p, %s, %p): stub!\n", iface, debugstr_guid( &uuid ), async );
    return E_NOTIMPL;
}

static HRESULT WINAPI gatt_service3_GetIncludedServicesForUuidWithCacheModeAsync( IGattDeviceService3 *iface, GUID uuid,
                                                                                  BluetoothCacheMode mode,
                                                                                  IAsyncOperation_GattDeviceServicesResult **async )
{
    FIXME( "(%p, %s, %u, %p): stub!\n", iface, debugstr_guid( &uuid ), mode, async );
    return E_NOTIMPL;
}

static const IGattDeviceService3Vtbl gatt_service3_vtbl =
{
    /* IUnknown */
    gatt_service3_QueryInterface,
    gatt_service3_AddRef,
    gatt_service3_Release,
    /* IInspectable */
    gatt_service3_GetIids,
    gatt_service3_GetRuntimeClassName,
    gatt_service3_GetTrustLevel,
    /* IGattDeviceService3 */
    gatt_service3_get_DeviceAccessInformation,
    gatt_service3_get_Session,
    gatt_service3_get_SharingMode,
    gatt_service3_RequestAccessAsync,
    gatt_service3_OpenAsync,
    gatt_service3_GetCharacteristicsAsync,
    gatt_service3_GetCharacteristicsWithCacheModeAsync,
    gatt_service3_GetCharacteristicsForUuidAsync,
    gatt_service3_GetCharacteristicsForUuidWithCacheModeAsync,
    gatt_service3_GetIncludedServicesAsync,
    gatt_service3_GetIncludedServicesWithCacheModeAsync,
    gatt_service3_GetIncludedServicesForUuidAsync,
    gatt_service3_GetIncludedServicesForUuidWithCacheModeAsync,
};

HRESULT gatt_service_create( const BTH_LE_GATT_SERVICE *svc, HANDLE device, IGattDeviceService **service )
{
    struct gatt_service *impl;

    if (!(impl = calloc( 1, sizeof( *impl ) )))
        return E_OUTOFMEMORY;
    impl->IGattDeviceService_iface.lpVtbl = &gatt_service_vtbl;
    impl->IGattDeviceService3_iface.lpVtbl = &gatt_service3_vtbl;
    impl->ref = 1;
    impl->device = device;
    impl->service = *svc;
    *service = &impl->IGattDeviceService_iface;
    return S_OK;
}

struct gatt_services_result
{
    IGattDeviceServicesResult IGattDeviceServicesResult_iface;
    LONG ref;

    IVectorView_GattDeviceService *services;
};

static inline struct gatt_services_result *impl_from_IGattDeviceServicesResult( IGattDeviceServicesResult *iface )
{
    return CONTAINING_RECORD( iface, struct gatt_services_result, IGattDeviceServicesResult_iface );
}

static HRESULT WINAPI gatt_services_result_QueryInterface( IGattDeviceServicesResult *iface, REFIID iid, void **out )
{
    struct gatt_services_result *impl = impl_from_IGattDeviceServicesResult( iface );

    TRACE( "(%p, %s, %p)\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IGattDeviceServicesResult ))
    {
        IGattDeviceServicesResult_AddRef(( *out = &impl->IGattDeviceServicesResult_iface ));
        return S_OK;
    }
    *out = NULL;
    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG WINAPI gatt_services_result_AddRef( IGattDeviceServicesResult *iface )
{
    struct gatt_services_result *impl = impl_from_IGattDeviceServicesResult( iface );
    TRACE( "(%p)\n", iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI gatt_services_result_Release( IGattDeviceServicesResult *iface )
{
    struct gatt_services_result *impl = impl_from_IGattDeviceServicesResult( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    TRACE( "(%p)\n", iface );
    if (!ref)
    {
        IVectorView_GattDeviceService_Release( impl->services );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI gatt_services_result_GetIids( IGattDeviceServicesResult *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "(%p, %p, %p): stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI gatt_services_result_GetRuntimeClassName( IGattDeviceServicesResult *iface, HSTRING *class_name )
{
    FIXME( "(%p, %p): stub!\n", iface, class_name );
    return E_NOTIMPL;
}

static HRESULT WINAPI gatt_services_result_GetTrustLevel( IGattDeviceServicesResult *iface, TrustLevel *level )
{
    FIXME( "(%p, %p): stub!\n", iface, level );
    return E_NOTIMPL;
}

static HRESULT WINAPI gatt_services_result_get_Status( IGattDeviceServicesResult *iface, GattCommunicationStatus *value )
{
    TRACE( "(%p, %p)\n", iface, value );
    *value = GattCommunicationStatus_Success;
    return S_OK;
}

static HRESULT WINAPI gatt_services_result_get_ProtocolError( IGattDeviceServicesResult *iface, IReference_BYTE **value )
{
    TRACE( "(%p, %p)\n", iface, value );
    *value = NULL;
    return S_OK;
}

static HRESULT WINAPI gatt_services_result_get_Services( IGattDeviceServicesResult *iface, IVectorView_GattDeviceService **value )
{
    struct gatt_services_result *impl = impl_from_IGattDeviceServicesResult( iface );
    TRACE( "(%p, %p)\n", iface, value );
    IVectorView_GattDeviceService_AddRef( impl->services );
    *value = impl->services;
    return S_OK;
}

static const IGattDeviceServicesResultVtbl gatt_services_result_vtbl =
{
    /* IUnknown */
    gatt_services_result_QueryInterface,
    gatt_services_result_AddRef,
    gatt_services_result_Release,
    /* IInspectable */
    gatt_services_result_GetIids,
    gatt_services_result_GetRuntimeClassName,
    gatt_services_result_GetTrustLevel,
    /* IGattDeviceServicesResult */
    gatt_services_result_get_Status,
    gatt_services_result_get_ProtocolError,
    gatt_services_result_get_Services,
};

HRESULT gatt_services_result_create( USHORT count, const BTH_LE_GATT_SERVICE *svcs, HANDLE device,
                                     const GUID *filter, IGattDeviceServicesResult **out )
{
    static const struct vector_iids iids = {
        .vector = &IID_IVector_IInspectable,
        .view = &IID_IVectorView_GattDeviceService,
        .iterable = &IID_IIterable_GattDeviceService,
        .iterator = &IID_IIterator_GattDeviceService,
    };
    struct gatt_services_result *impl;
    IVector_IInspectable *vector;
    HRESULT hr;
    USHORT i;

    *out = NULL;
    if (FAILED(hr = vector_create( &iids, (void **)&vector ))) return hr;

    for (i = 0; i < count; i++)
    {
        IGattDeviceService *service;

        if (filter)
        {
            GUID uuid;
            gatt_uuid_from_le_uuid( &svcs[i].ServiceUuid, &uuid );
            if (!IsEqualGUID( &uuid, filter )) continue;
        }
        if (FAILED(hr = gatt_service_create( &svcs[i], device, &service ))) goto done;
        hr = IVector_IInspectable_Append( vector, (IInspectable *)service );
        IGattDeviceService_Release( service );
        if (FAILED( hr )) goto done;
    }

    if (!(impl = calloc( 1, sizeof( *impl ) )))
    {
        hr = E_OUTOFMEMORY;
        goto done;
    }
    impl->IGattDeviceServicesResult_iface.lpVtbl = &gatt_services_result_vtbl;
    impl->ref = 1;
    hr = IVector_IInspectable_GetView( vector, (IVectorView_IInspectable **)&impl->services );
    if (SUCCEEDED(hr)) *out = &impl->IGattDeviceServicesResult_iface;
    else free( impl );

done:
    IVector_IInspectable_Release( vector );
    return hr;
}
