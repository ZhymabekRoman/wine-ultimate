/*
 * Copyright 2022 Nikolay Sivov for CodeWeavers
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

#include <assert.h>

#include "wine/debug.h"
#include "objbase.h"

#include "bcrypt.h"
#include "wincrypt.h"

#define WIDL_using_Windows_Security_Cryptography
#include "windows.security.cryptography.h"
#include "robuffer.h"

WINE_DEFAULT_DEBUG_CHANNEL(crypto);

struct cryptobuffer_factory
{
    IActivationFactory IActivationFactory_iface;
    ICryptographicBufferStatics ICryptographicBufferStatics_iface;
    LONG refcount;
};

static inline struct cryptobuffer_factory *impl_from_IActivationFactory(IActivationFactory *iface)
{
    return CONTAINING_RECORD(iface, struct cryptobuffer_factory, IActivationFactory_iface);
}

static HRESULT STDMETHODCALLTYPE cryptobuffer_factory_QueryInterface(
        IActivationFactory *iface, REFIID iid, void **out)
{
    struct cryptobuffer_factory *factory = impl_from_IActivationFactory(iface);

    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_IUnknown) ||
        IsEqualGUID(iid, &IID_IInspectable) ||
        IsEqualGUID(iid, &IID_IAgileObject) ||
        IsEqualGUID(iid, &IID_IActivationFactory))
    {
        IUnknown_AddRef(iface);
        *out = &factory->IActivationFactory_iface;
        return S_OK;
    }

    if (IsEqualGUID(iid, &IID_ICryptographicBufferStatics))
    {
        IUnknown_AddRef(iface);
        *out = &factory->ICryptographicBufferStatics_iface;
        return S_OK;
    }

    FIXME("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid));
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE cryptobuffer_factory_AddRef(IActivationFactory *iface)
{
    struct cryptobuffer_factory *factory = impl_from_IActivationFactory(iface);
    ULONG refcount = InterlockedIncrement(&factory->refcount);

    TRACE("iface %p, refcount %lu.\n", iface, refcount);

    return refcount;
}

static ULONG STDMETHODCALLTYPE cryptobuffer_factory_Release(IActivationFactory *iface)
{
    struct cryptobuffer_factory *factory = impl_from_IActivationFactory(iface);
    ULONG refcount = InterlockedDecrement(&factory->refcount);

    TRACE("iface %p, refcount %lu.\n", iface, refcount);

    return refcount;
}

static HRESULT STDMETHODCALLTYPE cryptobuffer_factory_GetIids(
        IActivationFactory *iface, ULONG *iid_count, IID **iids)
{
    FIXME("iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE cryptobuffer_factory_GetRuntimeClassName(
        IActivationFactory *iface, HSTRING *class_name)
{
    FIXME("iface %p, class_name %p stub!\n", iface, class_name);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE cryptobuffer_factory_GetTrustLevel(
        IActivationFactory *iface, TrustLevel *trust_level)
{
    FIXME("iface %p, trust_level %p stub!\n", iface, trust_level);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE cryptobuffer_factory_ActivateInstance(
        IActivationFactory *iface, IInspectable **instance)
{
    FIXME("iface %p, instance %p stub!\n", iface, instance);
    return E_NOTIMPL;
}

static const struct IActivationFactoryVtbl cryptobuffer_factory_vtbl =
{
    cryptobuffer_factory_QueryInterface,
    cryptobuffer_factory_AddRef,
    cryptobuffer_factory_Release,
    /* IInspectable methods */
    cryptobuffer_factory_GetIids,
    cryptobuffer_factory_GetRuntimeClassName,
    cryptobuffer_factory_GetTrustLevel,
    /* IActivationFactory methods */
    cryptobuffer_factory_ActivateInstance,
};

DEFINE_IINSPECTABLE(cryptobuffer_statics, ICryptographicBufferStatics, struct cryptobuffer_factory, IActivationFactory_iface);

struct crypt_buffer
{
    IBuffer IBuffer_iface;
    IBufferByteAccess IBufferByteAccess_iface;
    LONG ref;
    UINT32 capacity;
    UINT32 length;
    BYTE data[1];
};

static inline struct crypt_buffer *impl_from_IBuffer( IBuffer *iface )
{
    return CONTAINING_RECORD( iface, struct crypt_buffer, IBuffer_iface );
}

static inline struct crypt_buffer *impl_from_IBufferByteAccess( IBufferByteAccess *iface )
{
    return CONTAINING_RECORD( iface, struct crypt_buffer, IBufferByteAccess_iface );
}

static HRESULT WINAPI crypt_buffer_QueryInterface( IBuffer *iface, REFIID iid, void **out )
{
    struct crypt_buffer *impl = impl_from_IBuffer( iface );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IBuffer ))
    {
        InterlockedIncrement( &impl->ref );
        *out = &impl->IBuffer_iface;
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IBufferByteAccess ))
    {
        InterlockedIncrement( &impl->ref );
        *out = &impl->IBufferByteAccess_iface;
        return S_OK;
    }

    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI crypt_buffer_AddRef( IBuffer *iface )
{
    struct crypt_buffer *impl = impl_from_IBuffer( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI crypt_buffer_Release( IBuffer *iface )
{
    struct crypt_buffer *impl = impl_from_IBuffer( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref) free( impl );
    return ref;
}

static HRESULT WINAPI crypt_buffer_GetIids( IBuffer *iface, ULONG *count, IID **iids ) { return E_NOTIMPL; }
static HRESULT WINAPI crypt_buffer_GetRuntimeClassName( IBuffer *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.Storage.Streams.Buffer", 29, name );
}
static HRESULT WINAPI crypt_buffer_GetTrustLevel( IBuffer *iface, TrustLevel *level ) { if (level) *level = BaseTrust; return S_OK; }
static HRESULT WINAPI crypt_buffer_get_Capacity( IBuffer *iface, UINT32 *value )
{
    struct crypt_buffer *impl = impl_from_IBuffer( iface );
    if (!value) return E_POINTER;
    *value = impl->capacity;
    return S_OK;
}
static HRESULT WINAPI crypt_buffer_get_Length( IBuffer *iface, UINT32 *value )
{
    struct crypt_buffer *impl = impl_from_IBuffer( iface );
    if (!value) return E_POINTER;
    *value = impl->length;
    return S_OK;
}
static HRESULT WINAPI crypt_buffer_put_Length( IBuffer *iface, UINT32 value )
{
    struct crypt_buffer *impl = impl_from_IBuffer( iface );
    if (value > impl->capacity) return E_INVALIDARG;
    impl->length = value;
    return S_OK;
}

static const struct IBufferVtbl crypt_buffer_vtbl =
{
    crypt_buffer_QueryInterface,
    crypt_buffer_AddRef,
    crypt_buffer_Release,
    crypt_buffer_GetIids,
    crypt_buffer_GetRuntimeClassName,
    crypt_buffer_GetTrustLevel,
    crypt_buffer_get_Capacity,
    crypt_buffer_get_Length,
    crypt_buffer_put_Length,
};

static HRESULT WINAPI crypt_buffer_byte_access_QueryInterface( IBufferByteAccess *iface, REFIID iid, void **out )
{
    struct crypt_buffer *impl = impl_from_IBufferByteAccess( iface );
    return IBuffer_QueryInterface( &impl->IBuffer_iface, iid, out );
}
static ULONG WINAPI crypt_buffer_byte_access_AddRef( IBufferByteAccess *iface )
{
    struct crypt_buffer *impl = impl_from_IBufferByteAccess( iface );
    return IBuffer_AddRef( &impl->IBuffer_iface );
}
static ULONG WINAPI crypt_buffer_byte_access_Release( IBufferByteAccess *iface )
{
    struct crypt_buffer *impl = impl_from_IBufferByteAccess( iface );
    return IBuffer_Release( &impl->IBuffer_iface );
}
static HRESULT WINAPI crypt_buffer_byte_access_Buffer( IBufferByteAccess *iface, byte **value )
{
    struct crypt_buffer *impl = impl_from_IBufferByteAccess( iface );
    if (!value) return E_POINTER;
    *value = impl->data;
    return S_OK;
}

static const struct IBufferByteAccessVtbl crypt_buffer_byte_access_vtbl =
{
    crypt_buffer_byte_access_QueryInterface,
    crypt_buffer_byte_access_AddRef,
    crypt_buffer_byte_access_Release,
    crypt_buffer_byte_access_Buffer,
};

HRESULT create_crypt_buffer( UINT32 capacity, UINT32 length, const BYTE *data, IBuffer **out )
{
    struct crypt_buffer *impl;
    if (!out) return E_POINTER;
    if (!(impl = calloc( 1, offsetof( struct crypt_buffer, data[capacity ? capacity : 1] ) )))
        return E_OUTOFMEMORY;
    impl->IBuffer_iface.lpVtbl = &crypt_buffer_vtbl;
    impl->IBufferByteAccess_iface.lpVtbl = &crypt_buffer_byte_access_vtbl;
    impl->ref = 1;
    impl->capacity = capacity;
    impl->length = length;
    if (data && length) memcpy( impl->data, data, length );
    *out = &impl->IBuffer_iface;
    return S_OK;
}

HRESULT get_buffer_data( IBuffer *buffer, BYTE **data, UINT32 *len )
{
    IBufferByteAccess *access;
    HRESULT hr;

    if (!buffer)
    {
        if (data) *data = NULL;
        if (len) *len = 0;
        return S_OK;
    }
    if (len)
    {
        if (FAILED(hr = IBuffer_get_Length( buffer, len ))) return hr;
    }
    if (data)
    {
        if (FAILED(hr = IBuffer_QueryInterface( buffer, &IID_IBufferByteAccess, (void **)&access )))
            return hr;
        hr = IBufferByteAccess_Buffer( access, data );
        IBufferByteAccess_Release( access );
        if (FAILED(hr)) return hr;
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE cryptobuffer_statics_Compare(
        ICryptographicBufferStatics *iface, IBuffer *object1, IBuffer *object2, boolean *is_equal)
{
    BYTE *d1 = NULL, *d2 = NULL;
    UINT32 l1 = 0, l2 = 0;

    TRACE("iface %p, object1 %p, object2 %p, is_equal %p.\n", iface, object1, object2, is_equal);
    if (!is_equal) return E_POINTER;
    *is_equal = FALSE;

    if (object1 == object2) { *is_equal = TRUE; return S_OK; }
    if (!object1 || !object2) return S_OK;

    IBuffer_get_Length( object1, &l1 );
    IBuffer_get_Length( object2, &l2 );
    if (l1 != l2) return S_OK;
    if (l1 == 0) { *is_equal = TRUE; return S_OK; }

    get_buffer_data( object1, &d1, NULL );
    get_buffer_data( object2, &d2, NULL );
    if (d1 && d2 && !memcmp( d1, d2, l1 ))
        *is_equal = TRUE;

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE cryptobuffer_statics_GenerateRandom(
        ICryptographicBufferStatics *iface, UINT32 length, IBuffer **buffer)
{
    IBuffer *buf;
    IBufferByteAccess *access;
    BYTE *data;
    HRESULT hr;

    TRACE("iface %p, length %u, buffer %p.\n", iface, length, buffer);
    if (!buffer) return E_POINTER;
    if (FAILED(hr = create_crypt_buffer( length, length, NULL, &buf ))) return hr;
    if (SUCCEEDED(IBuffer_QueryInterface( buf, &IID_IBufferByteAccess, (void **)&access )))
    {
        if (SUCCEEDED(IBufferByteAccess_Buffer( access, &data )) && data && length)
            BCryptGenRandom( NULL, data, length, BCRYPT_USE_SYSTEM_PREFERRED_RNG );
        IBufferByteAccess_Release( access );
    }
    *buffer = buf;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE cryptobuffer_statics_GenerateRandomNumber(
        ICryptographicBufferStatics *iface, UINT32 *value)
{
    TRACE("iface %p, value %p.\n", iface, value);

    BCryptGenRandom(NULL, (UCHAR *)value, sizeof(*value), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE cryptobuffer_statics_CreateFromByteArray(
        ICryptographicBufferStatics *iface, UINT32 value_size, BYTE *value, IBuffer **buffer)
{
    TRACE("iface %p, value_size %u, value %p, buffer %p.\n", iface, value_size, value, buffer);
    return create_crypt_buffer( value_size, value_size, value, buffer );
}

static HRESULT STDMETHODCALLTYPE cryptobuffer_statics_CopyToByteArray(
        ICryptographicBufferStatics *iface, IBuffer *buffer, UINT32 *value_size, BYTE **value)
{
    IBufferByteAccess *access;
    BYTE *data = NULL;
    UINT32 length = 0;
    BYTE *copy;
    HRESULT hr;

    TRACE("iface %p, buffer %p, value_size %p, value %p.\n", iface, buffer, value_size, value);
    if (!value_size || !value) return E_POINTER;
    *value_size = 0;
    *value = NULL;
    if (!buffer) return S_OK;

    if (FAILED(hr = IBuffer_get_Length( buffer, &length ))) return hr;
    if (FAILED(hr = IBuffer_QueryInterface( buffer, &IID_IBufferByteAccess, (void **)&access ))) return hr;
    hr = IBufferByteAccess_Buffer( access, &data );
    IBufferByteAccess_Release( access );
    if (FAILED(hr)) return hr;

    if (length)
    {
        if (!(copy = CoTaskMemAlloc( length ))) return E_OUTOFMEMORY;
        memcpy( copy, data, length );
        *value = copy;
    }
    *value_size = length;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE cryptobuffer_statics_DecodeFromHexString(
        ICryptographicBufferStatics *iface, HSTRING value, IBuffer **buffer)
{
    const WCHAR *str;
    UINT32 len = 0, i;
    IBufferByteAccess *access;
    BYTE *data = NULL;
    IBuffer *buf;
    HRESULT hr;

    TRACE("iface %p, value %p, buffer %p.\n", iface, value, buffer);
    if (!buffer) return E_POINTER;
    *buffer = NULL;

    str = WindowsGetStringRawBuffer( value, &len );
    if (!str || !len)
        return create_crypt_buffer( 0, 0, NULL, buffer );
    if (len % 2) return E_INVALIDARG;

    if (FAILED(hr = create_crypt_buffer( len / 2, len / 2, NULL, &buf )))
        return hr;

    if (SUCCEEDED(IBuffer_QueryInterface( buf, &IID_IBufferByteAccess, (void **)&access )))
    {
        IBufferByteAccess_Buffer( access, &data );
        for (i = 0; i < len / 2; i++)
        {
            WCHAR c1 = str[i * 2], c2 = str[i * 2 + 1];
            int v1 = (c1 >= '0' && c1 <= '9') ? (c1 - '0') : ((c1 >= 'a' && c1 <= 'f') ? (c1 - 'a' + 10) : ((c1 >= 'A' && c1 <= 'F') ? (c1 - 'A' + 10) : -1));
            int v2 = (c2 >= '0' && c2 <= '9') ? (c2 - '0') : ((c2 >= 'a' && c2 <= 'f') ? (c2 - 'a' + 10) : ((c2 >= 'A' && c2 <= 'F') ? (c2 - 'A' + 10) : -1));
            if (v1 < 0 || v2 < 0)
            {
                IBufferByteAccess_Release( access );
                IBuffer_Release( buf );
                return E_INVALIDARG;
            }
            data[i] = (v1 << 4) | v2;
        }
        IBufferByteAccess_Release( access );
    }

    *buffer = buf;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE cryptobuffer_statics_EncodeToHexString(
        ICryptographicBufferStatics *iface, IBuffer *buffer, HSTRING *value)
{
    static const WCHAR hex_digits[] = L"0123456789ABCDEF";
    BYTE *data = NULL;
    UINT32 length = 0, i;
    HSTRING_BUFFER str_buf;
    WCHAR *str;
    HRESULT hr;

    TRACE("iface %p, buffer %p, value %p.\n", iface, buffer, value);
    if (!value) return E_POINTER;
    *value = NULL;
    if (buffer) IBuffer_get_Length( buffer, &length );
    if (!length) return WindowsCreateString( NULL, 0, value );

    if (FAILED(hr = get_buffer_data( buffer, &data, &length ))) return hr;
    if (!data) return E_FAIL;

    if (FAILED(hr = WindowsPreallocateStringBuffer( length * 2, &str, &str_buf )))
        return hr;
    for (i = 0; i < length; i++)
    {
        str[i * 2]     = hex_digits[(data[i] >> 4) & 0xf];
        str[i * 2 + 1] = hex_digits[data[i] & 0xf];
    }
    return WindowsPromoteStringBuffer( str_buf, value );
}

static HRESULT STDMETHODCALLTYPE cryptobuffer_statics_DecodeFromBase64String(
        ICryptographicBufferStatics *iface, HSTRING value, IBuffer **buffer)
{
    const WCHAR *str;
    UINT32 len = 0;
    DWORD size = 0;
    IBufferByteAccess *access;
    BYTE *data = NULL;
    IBuffer *buf;
    HRESULT hr;

    TRACE("iface %p, value %p, buffer %p.\n", iface, value, buffer);
    if (!buffer) return E_POINTER;
    *buffer = NULL;

    str = WindowsGetStringRawBuffer( value, &len );
    if (!str || !len)
        return create_crypt_buffer( 0, 0, NULL, buffer );

    if (!CryptStringToBinaryW( str, len, CRYPT_STRING_BASE64, NULL, &size, NULL, NULL ))
        return E_INVALIDARG;

    if (FAILED(hr = create_crypt_buffer( size, size, NULL, &buf )))
        return hr;

    if (SUCCEEDED(IBuffer_QueryInterface( buf, &IID_IBufferByteAccess, (void **)&access )))
    {
        IBufferByteAccess_Buffer( access, &data );
        CryptStringToBinaryW( str, len, CRYPT_STRING_BASE64, data, &size, NULL, NULL );
        IBufferByteAccess_Release( access );
    }

    *buffer = buf;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE cryptobuffer_statics_EncodeToBase64String(
        ICryptographicBufferStatics *iface, IBuffer *buffer, HSTRING *value)
{
    IBufferByteAccess *buffer_access;
    HSTRING_BUFFER str_buffer;
    void *data = NULL;
    UINT32 length = 0;
    DWORD ret_length;
    WCHAR *str;
    HRESULT hr;

    TRACE("iface %p, buffer %p, value %p.\n", iface, buffer, value);

    if (buffer)
    {
        IBuffer_get_Length(buffer, &length);
        if (length)
        {
            if (SUCCEEDED(IBuffer_QueryInterface(buffer, &IID_IBufferByteAccess, (void **)&buffer_access)))
            {
                IBufferByteAccess_Buffer(buffer_access, (byte **)&data);
                IBufferByteAccess_Release(buffer_access);
            }
        }
    }

    if (!length)
        return WindowsCreateString(NULL, 0, value);

    if (!data)
        return E_FAIL;

    if (!CryptBinaryToStringW(data, length, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, NULL, &ret_length))
        return E_FAIL;

    if (FAILED(hr = WindowsPreallocateStringBuffer(ret_length, &str, &str_buffer)))
        return hr;

    if (!CryptBinaryToStringW(data, length, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, str, &ret_length))
    {
        WindowsDeleteStringBuffer(str_buffer);
        return E_FAIL;
    }

    return WindowsPromoteStringBuffer(str_buffer, value);
}

static HRESULT STDMETHODCALLTYPE cryptobuffer_statics_ConvertStringToBinary(
        ICryptographicBufferStatics *iface, HSTRING value, BinaryStringEncoding encoding,
        IBuffer **buffer)
{
    const WCHAR *str;
    UINT32 len = 0;
    int utf8_len;
    char *utf8_data;
    HRESULT hr;

    TRACE("iface %p, value %p, encoding %d, buffer %p.\n", iface, value, encoding, buffer);
    if (!buffer) return E_POINTER;
    *buffer = NULL;

    str = WindowsGetStringRawBuffer( value, &len );
    if (!str || !len) return create_crypt_buffer( 0, 0, NULL, buffer );

    if (encoding == BinaryStringEncoding_Utf8)
    {
        utf8_len = WideCharToMultiByte( CP_UTF8, 0, str, len, NULL, 0, NULL, NULL );
        if (!utf8_len) return E_FAIL;
        if (!(utf8_data = malloc( utf8_len ))) return E_OUTOFMEMORY;
        WideCharToMultiByte( CP_UTF8, 0, str, len, utf8_data, utf8_len, NULL, NULL );
        hr = create_crypt_buffer( utf8_len, utf8_len, (BYTE *)utf8_data, buffer );
        free( utf8_data );
        return hr;
    }
    else if (encoding == BinaryStringEncoding_Utf16LE)
    {
        return create_crypt_buffer( len * sizeof(WCHAR), len * sizeof(WCHAR), (const BYTE *)str, buffer );
    }
    else
    {
        FIXME("encoding %d not fully supported\n", encoding);
        return E_NOTIMPL;
    }
}

static HRESULT STDMETHODCALLTYPE cryptobuffer_statics_ConvertBinaryToString(
        ICryptographicBufferStatics *iface, BinaryStringEncoding encoding, IBuffer *buffer, HSTRING *value)
{
    BYTE *data = NULL;
    UINT32 len = 0;
    int wlen;
    WCHAR *wstr;
    HRESULT hr;

    TRACE("iface %p, encoding %d, buffer %p, value %p.\n", iface, encoding, buffer, value);
    if (!value) return E_POINTER;
    *value = NULL;
    if (buffer) IBuffer_get_Length( buffer, &len );
    if (!len) return WindowsCreateString( NULL, 0, value );

    if (FAILED(hr = get_buffer_data( buffer, &data, &len ))) return hr;
    if (!data) return E_FAIL;

    if (encoding == BinaryStringEncoding_Utf8)
    {
        wlen = MultiByteToWideChar( CP_UTF8, 0, (const char *)data, len, NULL, 0 );
        if (!wlen) return E_FAIL;
        if (!(wstr = malloc( wlen * sizeof(WCHAR) ))) return E_OUTOFMEMORY;
        MultiByteToWideChar( CP_UTF8, 0, (const char *)data, len, wstr, wlen );
        hr = WindowsCreateString( wstr, wlen, value );
        free( wstr );
        return hr;
    }
    else if (encoding == BinaryStringEncoding_Utf16LE)
    {
        return WindowsCreateString( (const WCHAR *)data, len / sizeof(WCHAR), value );
    }
    return E_NOTIMPL;
}

static const struct ICryptographicBufferStaticsVtbl cryptobuffer_statics_vtbl =
{
    cryptobuffer_statics_QueryInterface,
    cryptobuffer_statics_AddRef,
    cryptobuffer_statics_Release,
    /* IInspectable methods */
    cryptobuffer_statics_GetIids,
    cryptobuffer_statics_GetRuntimeClassName,
    cryptobuffer_statics_GetTrustLevel,
    /* ICryptographicBufferStatics methods */
    cryptobuffer_statics_Compare,
    cryptobuffer_statics_GenerateRandom,
    cryptobuffer_statics_GenerateRandomNumber,
    cryptobuffer_statics_CreateFromByteArray,
    cryptobuffer_statics_CopyToByteArray,
    cryptobuffer_statics_DecodeFromHexString,
    cryptobuffer_statics_EncodeToHexString,
    cryptobuffer_statics_DecodeFromBase64String,
    cryptobuffer_statics_EncodeToBase64String,
    cryptobuffer_statics_ConvertStringToBinary,
    cryptobuffer_statics_ConvertBinaryToString,
};

static struct cryptobuffer_factory cryptobuffer_factory =
{
    .IActivationFactory_iface.lpVtbl = &cryptobuffer_factory_vtbl,
    .ICryptographicBufferStatics_iface.lpVtbl = &cryptobuffer_statics_vtbl,
    .refcount = 1,
};

IActivationFactory *cryptobuffer_activation_factory = &cryptobuffer_factory.IActivationFactory_iface;

HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void **out)
{
    FIXME("clsid %s, riid %s, out %p stub!\n", debugstr_guid(clsid), debugstr_guid(riid), out);
    return CLASS_E_CLASSNOTAVAILABLE;
}

HRESULT WINAPI DllGetActivationFactory(HSTRING classid, IActivationFactory **factory)
{
    const WCHAR *name = WindowsGetStringRawBuffer(classid, NULL);

    TRACE("classid %s, factory %p.\n", debugstr_hstring(classid), factory);

    *factory = NULL;

    if (!wcscmp(name, RuntimeClass_Windows_Security_Cryptography_CryptographicBuffer))
        IActivationFactory_QueryInterface(cryptobuffer_activation_factory, &IID_IActivationFactory, (void **)factory);
    if (!wcscmp(name, RuntimeClass_Windows_Security_Credentials_KeyCredentialManager))
        IActivationFactory_QueryInterface(credentials_activation_factory, &IID_IActivationFactory, (void **)factory);
    if (!wcscmp(name, L"Windows.Security.Cryptography.DataProtection.DataProtectionProvider"))
        IActivationFactory_QueryInterface(dataprotection_activation_factory, &IID_IActivationFactory, (void **)factory);

    if (!wcscmp(name, RuntimeClass_Windows_Security_Cryptography_Core_CryptographicEngine))
        IActivationFactory_QueryInterface(cryptoengine_activation_factory, &IID_IActivationFactory, (void **)factory);
    if (!wcscmp(name, RuntimeClass_Windows_Security_Cryptography_Core_SymmetricKeyAlgorithmProvider))
        IActivationFactory_QueryInterface(symmetrickey_activation_factory, &IID_IActivationFactory, (void **)factory);
    if (!wcscmp(name, RuntimeClass_Windows_Security_Cryptography_Core_HashAlgorithmProvider))
        IActivationFactory_QueryInterface(hashalgorithm_activation_factory, &IID_IActivationFactory, (void **)factory);
    if (!wcscmp(name, RuntimeClass_Windows_Security_Cryptography_Core_KeyDerivationAlgorithmProvider))
        IActivationFactory_QueryInterface(keyderivation_activation_factory, &IID_IActivationFactory, (void **)factory);
    if (!wcscmp(name, RuntimeClass_Windows_Security_Cryptography_Core_KeyDerivationParameters))
        IActivationFactory_QueryInterface(keyderivationparams_activation_factory, &IID_IActivationFactory, (void **)factory);

    if (!wcscmp(name, L"Windows.Security.Cryptography.Core.SymmetricAlgorithmNames"))
        IActivationFactory_QueryInterface(symmetricalgnames_activation_factory, &IID_IActivationFactory, (void **)factory);
    if (!wcscmp(name, L"Windows.Security.Cryptography.Core.HashAlgorithmNames"))
        IActivationFactory_QueryInterface(hashalgnames_activation_factory, &IID_IActivationFactory, (void **)factory);
    if (!wcscmp(name, L"Windows.Security.Cryptography.Core.KeyDerivationAlgorithmNames"))
        IActivationFactory_QueryInterface(keyderivationalgnames_activation_factory, &IID_IActivationFactory, (void **)factory);

    if (*factory) return S_OK;
    return E_NOINTERFACE;
}
