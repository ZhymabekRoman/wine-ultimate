/*
 * Copyright 2022-2024 Zhiyi Zhang for CodeWeavers
 * Copyright 2025 Jactry Zeng for CodeWeavers
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

WINE_DEFAULT_DEBUG_CHANNEL(wintypes);

struct data_writer
{
    IActivationFactory IActivationFactory_iface;
    IDataWriter IDataWriter_iface;
    LONG ref;
};

static inline struct data_writer *impl_data_writer_from_IActivationFactory(IActivationFactory *iface)
{
    return CONTAINING_RECORD(iface, struct data_writer, IActivationFactory_iface);
}

static HRESULT STDMETHODCALLTYPE data_writer_activation_factory_QueryInterface(IActivationFactory *iface, REFIID iid,
        void **out)
{
    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_IUnknown)
            || IsEqualGUID(iid, &IID_IInspectable)
            || IsEqualGUID(iid, &IID_IAgileObject)
            || IsEqualGUID(iid, &IID_IActivationFactory))
    {
        IUnknown_AddRef(iface);
        *out = iface;
        return S_OK;
    }

    /* Windows aggregates the class default interface (IDataWriter) on the factory object. */
    if (IsEqualGUID(iid, &IID_IDataWriter))
    {
        struct data_writer *impl = impl_data_writer_from_IActivationFactory(iface);
        IUnknown_AddRef(iface);
        *out = &impl->IDataWriter_iface;
        return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid));
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE data_writer_activation_factory_AddRef(IActivationFactory *iface)
{
    struct data_writer *impl = impl_data_writer_from_IActivationFactory(iface);
    ULONG ref = InterlockedIncrement(&impl->ref);
    TRACE("iface %p, ref %lu.\n", iface, ref);
    return ref;
}

static ULONG STDMETHODCALLTYPE data_writer_activation_factory_Release(IActivationFactory *iface)
{
    struct data_writer *impl = impl_data_writer_from_IActivationFactory(iface);
    ULONG ref = InterlockedDecrement(&impl->ref);
    TRACE("iface %p, ref %lu.\n", iface, ref);
    return ref;
}

static HRESULT STDMETHODCALLTYPE data_writer_activation_factory_GetIids(IActivationFactory *iface, ULONG *iid_count,
        IID **iids)
{
    IID factory_iids[] =
    {
        IID_IActivationFactory,
        IID_IDataWriter,
    };
    IID *array = CoTaskMemAlloc( sizeof(factory_iids) );

    TRACE("iface %p, iid_count %p, iids %p.\n", iface, iid_count, iids);

    if (!array) return E_OUTOFMEMORY;
    memcpy(array, factory_iids, sizeof(factory_iids));
    *iid_count = ARRAY_SIZE(factory_iids);
    *iids = array;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE data_writer_activation_factory_GetRuntimeClassName(IActivationFactory *iface,
        HSTRING *class_name)
{
    static const WCHAR name[] = L"Windows.Storage.Streams.DataWriter";

    return WindowsCreateString(name, ARRAY_SIZE(name) - 1, class_name);
}

static HRESULT STDMETHODCALLTYPE data_writer_activation_factory_GetTrustLevel(IActivationFactory *iface,
        TrustLevel *trust_level)
{
    *trust_level = FullTrust;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE data_writer_activation_factory_ActivateInstance(IActivationFactory *iface,
        IInspectable **instance)
{
    FIXME("iface %p, instance %p stub!\n", iface, instance);
    *instance = NULL;
    return E_NOTIMPL;
}

static const struct IActivationFactoryVtbl data_writer_activation_factory_vtbl =
{
    data_writer_activation_factory_QueryInterface,
    data_writer_activation_factory_AddRef,
    data_writer_activation_factory_Release,
    /* IInspectable methods */
    data_writer_activation_factory_GetIids,
    data_writer_activation_factory_GetRuntimeClassName,
    data_writer_activation_factory_GetTrustLevel,
    /* IActivationFactory methods */
    data_writer_activation_factory_ActivateInstance,
};

/*
 * Windows aggregates the class default interface (IDataWriter) on the factory
 * object: QI for IID_IDataWriter on the factory succeeds (CsWinRT probes the
 * factory for the class interfaces). The factory is not an instance; instance
 * method calls fail.
 */
DEFINE_IINSPECTABLE_( data_writer_def, IDataWriter, struct data_writer, impl_from_IDataWriter_data_writer,
                      IDataWriter_iface, &impl->IActivationFactory_iface )

#define DATA_WRITER_FACTORY_STUB( name, proto ) \
    static HRESULT WINAPI data_writer_def_##name proto { return E_FAIL; }

DATA_WRITER_FACTORY_STUB( get_UnstoredBufferLength, ( IDataWriter *iface, UINT32 *value ) )
DATA_WRITER_FACTORY_STUB( get_UnicodeEncoding, ( IDataWriter *iface, UnicodeEncoding *value ) )
DATA_WRITER_FACTORY_STUB( put_UnicodeEncoding, ( IDataWriter *iface, UnicodeEncoding value ) )
DATA_WRITER_FACTORY_STUB( get_ByteOrder, ( IDataWriter *iface, ByteOrder *value ) )
DATA_WRITER_FACTORY_STUB( put_ByteOrder, ( IDataWriter *iface, ByteOrder value ) )
DATA_WRITER_FACTORY_STUB( WriteByte, ( IDataWriter *iface, BYTE value ) )
DATA_WRITER_FACTORY_STUB( WriteBytes, ( IDataWriter *iface, UINT32 value_size, BYTE *value ) )
DATA_WRITER_FACTORY_STUB( WriteBuffer, ( IDataWriter *iface, IBuffer *buffer ) )
DATA_WRITER_FACTORY_STUB( WriteBufferRange, ( IDataWriter *iface, IBuffer *buffer, UINT32 start, UINT32 count ) )
DATA_WRITER_FACTORY_STUB( WriteBoolean, ( IDataWriter *iface, boolean value ) )
DATA_WRITER_FACTORY_STUB( WriteGuid, ( IDataWriter *iface, GUID value ) )
DATA_WRITER_FACTORY_STUB( WriteInt16, ( IDataWriter *iface, INT16 value ) )
DATA_WRITER_FACTORY_STUB( WriteInt32, ( IDataWriter *iface, INT32 value ) )
DATA_WRITER_FACTORY_STUB( WriteInt64, ( IDataWriter *iface, INT64 value ) )
DATA_WRITER_FACTORY_STUB( WriteUInt16, ( IDataWriter *iface, UINT16 value ) )
DATA_WRITER_FACTORY_STUB( WriteUInt32, ( IDataWriter *iface, UINT32 value ) )
DATA_WRITER_FACTORY_STUB( WriteUInt64, ( IDataWriter *iface, UINT64 value ) )
DATA_WRITER_FACTORY_STUB( WriteSingle, ( IDataWriter *iface, FLOAT value ) )
DATA_WRITER_FACTORY_STUB( WriteDouble, ( IDataWriter *iface, DOUBLE value ) )
DATA_WRITER_FACTORY_STUB( WriteDateTime, ( IDataWriter *iface, DateTime value ) )
DATA_WRITER_FACTORY_STUB( WriteTimeSpan, ( IDataWriter *iface, TimeSpan value ) )
DATA_WRITER_FACTORY_STUB( WriteString, ( IDataWriter *iface, HSTRING value, UINT32 *code_unit_count ) )
DATA_WRITER_FACTORY_STUB( MeasureString, ( IDataWriter *iface, HSTRING value, UINT32 *code_unit_count ) )
DATA_WRITER_FACTORY_STUB( StoreAsync, ( IDataWriter *iface, __FIAsyncOperation_1_UINT32 **operation ) )
DATA_WRITER_FACTORY_STUB( FlushAsync, ( IDataWriter *iface, __FIAsyncOperation_1_boolean **operation ) )
DATA_WRITER_FACTORY_STUB( DetachBuffer, ( IDataWriter *iface, IBuffer **buffer ) )
DATA_WRITER_FACTORY_STUB( DetachStream, ( IDataWriter *iface, IOutputStream **output_stream ) )

static const struct IDataWriterVtbl data_writer_def_vtbl =
{
    data_writer_def_QueryInterface,
    data_writer_def_AddRef,
    data_writer_def_Release,
    data_writer_def_GetIids,
    data_writer_def_GetRuntimeClassName,
    data_writer_def_GetTrustLevel,
    data_writer_def_get_UnstoredBufferLength,
    data_writer_def_get_UnicodeEncoding,
    data_writer_def_put_UnicodeEncoding,
    data_writer_def_get_ByteOrder,
    data_writer_def_put_ByteOrder,
    data_writer_def_WriteByte,
    data_writer_def_WriteBytes,
    data_writer_def_WriteBuffer,
    data_writer_def_WriteBufferRange,
    data_writer_def_WriteBoolean,
    data_writer_def_WriteGuid,
    data_writer_def_WriteInt16,
    data_writer_def_WriteInt32,
    data_writer_def_WriteInt64,
    data_writer_def_WriteUInt16,
    data_writer_def_WriteUInt32,
    data_writer_def_WriteUInt64,
    data_writer_def_WriteSingle,
    data_writer_def_WriteDouble,
    data_writer_def_WriteDateTime,
    data_writer_def_WriteTimeSpan,
    data_writer_def_WriteString,
    data_writer_def_MeasureString,
    data_writer_def_StoreAsync,
    data_writer_def_FlushAsync,
    data_writer_def_DetachBuffer,
    data_writer_def_DetachStream,
};

struct data_writer data_writer =
{
    {&data_writer_activation_factory_vtbl},
    {&data_writer_def_vtbl},
    1
};

IActivationFactory *data_writer_activation_factory = &data_writer.IActivationFactory_iface;
