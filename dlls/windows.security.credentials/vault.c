/* WinRT Windows.Security.Credentials.PasswordVault Implementation
 *
 * Copyright 2026 Wine Developers
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

#include <assert.h>
#include <wctype.h>

#include "private.h"
#include "wincrypt.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(creds);

/*
 * The vault persists entries in the prefix registry under
 * HKCU\Software\Wine\PasswordVault\<key>.  <key> is built from the
 * (resource, userName) pair: the UTF-16 code units of each string are
 * hex encoded and joined with '-', so arbitrary characters in resource
 * URLs and user names (':', '"', '\\', accents, surrogate pairs) are all
 * legal in a registry key name.  Registry key lookup is case-insensitive,
 * matching the vault's Retrieve() contract, and one key per
 * (resource, userName) pair gives the upsert semantics of Add().
 *
 * Each key carries the plain-text Resource and UserName REG_SZ values
 * plus the Password as a REG_BINARY blob protected with DPAPI
 * (CryptProtectData/CryptUnprotectData, the same protection real Windows
 * vault entries use).  The Protected REG_DWORD marker records whether the
 * blob is DPAPI-protected; when CryptProtectData fails the raw NUL
 * terminated UTF-16 password is stored as the fallback blob.
 */

#define VAULT_ROOT_KEY L"Software\\Wine\\PasswordVault"
#define VAULT_VALUE_RESOURCE L"Resource"
#define VAULT_VALUE_USERNAME L"UserName"
#define VAULT_VALUE_PASSWORD L"Password"
#define VAULT_VALUE_PROTECTED L"Protected"

static CRITICAL_SECTION vault_cs;
static INIT_ONCE vault_init_once = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK vault_init_once_cb( PINIT_ONCE once, PVOID arg, PVOID *context )
{
    InitializeCriticalSection( &vault_cs );
    return TRUE;
}

static void vault_lock( void )
{
    InitOnceExecuteOnce( &vault_init_once, vault_init_once_cb, NULL, NULL );
    EnterCriticalSection( &vault_cs );
}

static void vault_unlock( void )
{
    LeaveCriticalSection( &vault_cs );
}

/* Append the 4 hex digits of one UTF-16 code unit, return the new end. */
static WCHAR *append_hex( WCHAR *p, WCHAR c )
{
    static const WCHAR hex[] = L"0123456789abcdef";

    *p++ = hex[(c >> 12) & 0xf];
    *p++ = hex[(c >> 8) & 0xf];
    *p++ = hex[(c >> 4) & 0xf];
    *p++ = hex[c & 0xf];
    return p;
}

/*
 * Deterministic registry key name for the (resource, username) pair.
 * Hex digits do not case-fold, so the code units are upper-cased before
 * encoding: the vault's Retrieve()/key matching is case-insensitive, and
 * the registry compares key names case-insensitively only for the hex
 * letters, not for case pairs hidden inside the encoded code units.
 */
static WCHAR *build_key( const WCHAR *resource, const WCHAR *username )
{
    SIZE_T res_len = lstrlenW( resource );
    SIZE_T user_len = lstrlenW( username );
    WCHAR *key, *p;
    SIZE_T i;

    if (!(key = malloc( (res_len + user_len) * 4 * sizeof(WCHAR) + 2 * sizeof(WCHAR) ))) return NULL;
    p = key;
    for (i = 0; i < res_len; ++i) p = append_hex( p, towupper( resource[i] ) );
    *p++ = L'-';
    for (i = 0; i < user_len; ++i) p = append_hex( p, towupper( username[i] ) );
    *p = 0;
    return key;
}

/* "Software\Wine\PasswordVault\<key>" in a freshly allocated string. */
static WCHAR *build_path( const WCHAR *key )
{
    SIZE_T root_len = lstrlenW( VAULT_ROOT_KEY );
    SIZE_T key_len = lstrlenW( key );
    WCHAR *path = malloc( (root_len + 1 + key_len + 1) * sizeof(WCHAR) );

    if (!path) return NULL;
    memcpy( path, VAULT_ROOT_KEY, root_len * sizeof(WCHAR) );
    path[root_len] = L'\\';
    memcpy( path + root_len + 1, key, (key_len + 1) * sizeof(WCHAR) );
    return path;
}

static HRESULT get_credential_string( IPasswordCredential *credential,
                                      HRESULT (STDMETHODCALLTYPE *getter)( IPasswordCredential *, HSTRING * ),
                                      WCHAR **out )
{
    HSTRING str;
    const WCHAR *buffer;
    HRESULT hr;

    if (FAILED(hr = getter( credential, &str ))) return hr;
    buffer = WindowsGetStringRawBuffer( str, NULL );
    if (buffer)
    {
        SIZE_T len = lstrlenW( buffer );
        WCHAR *copy = malloc( (len + 1) * sizeof(WCHAR) );
        if (!copy)
        {
            WindowsDeleteString( str );
            return E_OUTOFMEMORY;
        }
        memcpy( copy, buffer, (len + 1) * sizeof(WCHAR) );
        *out = copy;
    }
    else *out = NULL;
    WindowsDeleteString( str );
    if (!*out)
    {
        if (!(*out = malloc( sizeof(WCHAR) ))) return E_OUTOFMEMORY;
        **out = 0;
    }
    return S_OK;
}

/* Read a REG_SZ value into a freshly allocated NUL-terminated string. */
static HRESULT read_reg_sz( HKEY hkey, const WCHAR *name, WCHAR **out )
{
    DWORD type = 0, size = 0, chars;
    WCHAR *str;
    LSTATUS status;

    status = RegQueryValueExW( hkey, name, NULL, &type, NULL, &size );
    if (status || type != REG_SZ || size < sizeof(WCHAR))
    {
        if (!(str = malloc( sizeof(WCHAR) ))) return E_OUTOFMEMORY;
        str[0] = 0;
        *out = str;
        return S_OK;
    }
    chars = size / sizeof(WCHAR);
    if (!(str = malloc( (chars + 1) * sizeof(WCHAR) ))) return E_OUTOFMEMORY;
    size = chars * sizeof(WCHAR);
    status = RegQueryValueExW( hkey, name, NULL, NULL, (BYTE *)str, &size );
    if (status)
    {
        free( str );
        return HRESULT_FROM_WIN32( status );
    }
    str[size / sizeof(WCHAR)] = 0; /* tolerate a value stored without terminator */
    *out = str;
    return S_OK;
}

/*
 * Read the password value.  A Protected == 1 (the default when the marker
 * is absent) blob is run through CryptUnprotectData; on unprotect failure
 * the blob is decoded as the raw UTF-16 fallback form.  Result is a
 * freshly allocated NUL-terminated string, empty when the value is absent.
 */
static HRESULT read_reg_password( HKEY hkey, WCHAR **out )
{
    DWORD type = 0, size = 0, protected_flag = 1, ptype = 0, psize = sizeof(protected_flag);
    BYTE *blob;
    WCHAR *str;
    LSTATUS status;

    status = RegQueryValueExW( hkey, VAULT_VALUE_PASSWORD, NULL, &type, NULL, &size );
    if (status || !size)
    {
        if (!(str = malloc( sizeof(WCHAR) ))) return E_OUTOFMEMORY;
        str[0] = 0;
        *out = str;
        return S_OK;
    }
    RegQueryValueExW( hkey, VAULT_VALUE_PROTECTED, NULL, &ptype, (BYTE *)&protected_flag, &psize );

    if (!(blob = malloc( size ))) return E_OUTOFMEMORY;
    status = RegQueryValueExW( hkey, VAULT_VALUE_PASSWORD, NULL, NULL, blob, &size );
    if (status)
    {
        free( blob );
        return HRESULT_FROM_WIN32( status );
    }

    if (protected_flag == 1 && type == REG_BINARY)
    {
        DATA_BLOB in = { size, blob }, out_blob = { 0, NULL };

        if (CryptUnprotectData( &in, NULL, NULL, NULL, NULL, 0, &out_blob ))
        {
            DWORD chars = out_blob.cbData / sizeof(WCHAR);
            if (!(str = malloc( (chars + 1) * sizeof(WCHAR) )))
            {
                GlobalFree( out_blob.pbData );
                free( blob );
                return E_OUTOFMEMORY;
            }
            memcpy( str, out_blob.pbData, out_blob.cbData );
            str[chars] = 0; /* the stored form carries the terminator; this is belt-and-braces */
            GlobalFree( out_blob.pbData );
            free( blob );
            *out = str;
            return S_OK;
        }
        /* fall through: decode the blob as the raw UTF-16 fallback form */
    }

    {
        DWORD chars = size / sizeof(WCHAR);
        if (!(str = malloc( (chars + 1) * sizeof(WCHAR) )))
        {
            free( blob );
            return E_OUTOFMEMORY;
        }
        memcpy( str, blob, size );
        str[chars] = 0;
        free( blob );
        *out = str;
    }
    return S_OK;
}

/* Upsert one (resource, username) entry into the registry-backed vault. */
static HRESULT store_credential( const WCHAR *resource, const WCHAR *username, const WCHAR *password )
{
    WCHAR *key, *path;
    HKEY hkey;
    LSTATUS status;
    DWORD len;

    if (!(key = build_key( resource, username ))) return E_OUTOFMEMORY;
    if (!(path = build_path( key )))
    {
        free( key );
        return E_OUTOFMEMORY;
    }
    free( key );

    status = RegCreateKeyExW( HKEY_CURRENT_USER, path, 0, NULL, 0, KEY_WRITE, NULL, &hkey, NULL );
    free( path );
    if (status) return HRESULT_FROM_WIN32( status );

    len = (lstrlenW( resource ) + 1) * sizeof(WCHAR);
    status = RegSetValueExW( hkey, VAULT_VALUE_RESOURCE, 0, REG_SZ, (const BYTE *)resource, len );
    if (!status)
    {
        len = (lstrlenW( username ) + 1) * sizeof(WCHAR);
        status = RegSetValueExW( hkey, VAULT_VALUE_USERNAME, 0, REG_SZ, (const BYTE *)username, len );
    }
    if (!status)
    {
        DATA_BLOB in, out_blob;
        BYTE *data;
        DWORD data_size, protect = 1;

        len = (lstrlenW( password ) + 1) * sizeof(WCHAR);
        in.cbData = len;
        in.pbData = (BYTE *)password;
        data = (BYTE *)password;
        data_size = len;
        if (CryptProtectData( &in, NULL, NULL, NULL, NULL, 0, &out_blob ))
        {
            data = out_blob.pbData; /* GlobalAlloc'ed DPAPI blob */
            data_size = out_blob.cbData;
            protect = 1;
        }
        else protect = 0;
        status = RegSetValueExW( hkey, VAULT_VALUE_PASSWORD, 0, REG_BINARY, data, data_size );
        if (!status)
            status = RegSetValueExW( hkey, VAULT_VALUE_PROTECTED, 0, REG_DWORD, (const BYTE *)&protect, sizeof(protect) );
        if (protect == 1) GlobalFree( data );
    }
    RegCloseKey( hkey );
    return status ? HRESULT_FROM_WIN32( status ) : S_OK;
}

/*
 * IVectorView<PasswordCredential> + IIterable<PasswordCredential> over a
 * snapshot array, with shared identity (the C# projection enumerates the
 * read-only list through IIterable, same as the twinapi.appcore.dll
 * collections).  Elements carry one reference each, owned by the vector.
 */
struct credential_vector
{
    __FIVectorView_1_Windows__CSecurity__CCredentials__CPasswordCredential
        __FIVectorView_1_Windows__CSecurity__CCredentials__CPasswordCredential_iface;
    __FIIterable_1_Windows__CSecurity__CCredentials__CPasswordCredential
        __FIIterable_1_Windows__CSecurity__CCredentials__CPasswordCredential_iface;
    LONG ref;
    ULONG count;
    IPasswordCredential **items;
};

struct credential_iterator
{
    __FIIterator_1_Windows__CSecurity__CCredentials__CPasswordCredential
        __FIIterator_1_Windows__CSecurity__CCredentials__CPasswordCredential_iface;
    LONG ref;
    struct credential_vector *vector;
    ULONG index;
};

static inline struct credential_vector *impl_from_FIVectorView_PasswordCredential(
        __FIVectorView_1_Windows__CSecurity__CCredentials__CPasswordCredential *iface )
{
    return CONTAINING_RECORD( iface, struct credential_vector,
            __FIVectorView_1_Windows__CSecurity__CCredentials__CPasswordCredential_iface );
}

static inline struct credential_vector *impl_from_FIIterable_PasswordCredential(
        __FIIterable_1_Windows__CSecurity__CCredentials__CPasswordCredential *iface )
{
    return CONTAINING_RECORD( iface, struct credential_vector,
            __FIIterable_1_Windows__CSecurity__CCredentials__CPasswordCredential_iface );
}

static inline struct credential_iterator *impl_from_FIIterator_PasswordCredential(
        __FIIterator_1_Windows__CSecurity__CCredentials__CPasswordCredential *iface )
{
    return CONTAINING_RECORD( iface, struct credential_iterator,
            __FIIterator_1_Windows__CSecurity__CCredentials__CPasswordCredential_iface );
}

static HRESULT WINAPI vector_QueryInterface(
        __FIVectorView_1_Windows__CSecurity__CCredentials__CPasswordCredential *iface, REFIID iid, void **out )
{
    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (!out) return E_POINTER;
    *out = NULL;

    if (IsEqualGUID( iid, &IID_IUnknown )
            || IsEqualGUID( iid, &IID_IInspectable )
            || IsEqualGUID( iid, &IID_IAgileObject )
            || IsEqualGUID( iid, &IID___FIVectorView_1_Windows__CSecurity__CCredentials__CPasswordCredential ))
    {
        IUnknown_AddRef( (IUnknown *)iface );
        *out = iface;
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID___FIIterable_1_Windows__CSecurity__CCredentials__CPasswordCredential ))
    {
        struct credential_vector *impl = impl_from_FIVectorView_PasswordCredential( iface );

        IUnknown_AddRef( (IUnknown *)iface );
        *out = &impl->__FIIterable_1_Windows__CSecurity__CCredentials__CPasswordCredential_iface;
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG WINAPI vector_AddRef(
        __FIVectorView_1_Windows__CSecurity__CCredentials__CPasswordCredential *iface )
{
    struct credential_vector *impl = impl_from_FIVectorView_PasswordCredential( iface );
    return InterlockedIncrement( &impl->ref );
}

static void vector_destroy( struct credential_vector *impl )
{
    ULONG i;

    for (i = 0; i < impl->count; ++i) IPasswordCredential_Release( impl->items[i] );
    free( impl->items );
    free( impl );
}

static ULONG WINAPI vector_Release(
        __FIVectorView_1_Windows__CSecurity__CCredentials__CPasswordCredential *iface )
{
    struct credential_vector *impl = impl_from_FIVectorView_PasswordCredential( iface );
    LONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        TRACE( "destroying %p\n", iface );
        vector_destroy( impl );
    }
    return ref;
}

static HRESULT WINAPI vector_GetIids(
        __FIVectorView_1_Windows__CSecurity__CCredentials__CPasswordCredential *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI vector_GetRuntimeClassName(
        __FIVectorView_1_Windows__CSecurity__CCredentials__CPasswordCredential *iface, HSTRING *class_name )
{
    static const WCHAR name[] = L"Windows.Foundation.Collections.IVectorView`1<Windows.Security.Credentials.PasswordCredential>";

    if (!class_name) return E_POINTER;
    return WindowsCreateString( name, ARRAY_SIZE( name ) - 1, class_name );
}

static HRESULT WINAPI vector_GetTrustLevel(
        __FIVectorView_1_Windows__CSecurity__CCredentials__CPasswordCredential *iface, TrustLevel *trust_level )
{
    if (!trust_level) return E_POINTER;
    *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI vector_GetAt(
        __FIVectorView_1_Windows__CSecurity__CCredentials__CPasswordCredential *iface, UINT32 index,
        IPasswordCredential **value )
{
    struct credential_vector *impl = impl_from_FIVectorView_PasswordCredential( iface );

    TRACE( "iface %p, index %u, value %p.\n", iface, index, value );

    if (!value) return E_POINTER;
    if (index >= impl->count) return E_BOUNDS;
    *value = impl->items[index];
    IPasswordCredential_AddRef( *value );
    return S_OK;
}

static HRESULT WINAPI vector_get_Size(
        __FIVectorView_1_Windows__CSecurity__CCredentials__CPasswordCredential *iface, UINT32 *value )
{
    struct credential_vector *impl = impl_from_FIVectorView_PasswordCredential( iface );

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    *value = impl->count;
    return S_OK;
}

static HRESULT WINAPI vector_IndexOf(
        __FIVectorView_1_Windows__CSecurity__CCredentials__CPasswordCredential *iface,
        IPasswordCredential *element, UINT32 *index, boolean *value )
{
    struct credential_vector *impl = impl_from_FIVectorView_PasswordCredential( iface );
    ULONG i;

    TRACE( "iface %p, element %p, index %p, value %p.\n", iface, element, index, value );

    if (!index || !value) return E_POINTER;
    for (i = 0; i < impl->count; ++i)
    {
        if (impl->items[i] == element)
        {
            *index = i;
            *value = TRUE;
            return S_OK;
        }
    }
    *index = 0;
    *value = FALSE;
    return S_OK;
}

static HRESULT WINAPI vector_GetMany(
        __FIVectorView_1_Windows__CSecurity__CCredentials__CPasswordCredential *iface, UINT32 start_index,
        UINT32 items_size, IPasswordCredential **items, UINT32 *value )
{
    struct credential_vector *impl = impl_from_FIVectorView_PasswordCredential( iface );
    ULONG written = 0;

    TRACE( "iface %p, start_index %u, items_size %u, items %p, value %p.\n", iface, start_index,
           items_size, items, value );

    if (!value) return E_POINTER;
    while (written < items_size && start_index + written < impl->count)
    {
        if (!items) return E_POINTER;
        items[written] = impl->items[start_index + written];
        IPasswordCredential_AddRef( items[written] );
        ++written;
    }
    *value = written;
    return S_OK;
}

static const struct __FIVectorView_1_Windows__CSecurity__CCredentials__CPasswordCredentialVtbl vector_vtbl =
{
    vector_QueryInterface,
    vector_AddRef,
    vector_Release,
    /* IInspectable methods */
    vector_GetIids,
    vector_GetRuntimeClassName,
    vector_GetTrustLevel,
    /* IVectorView<PasswordCredential> methods */
    vector_GetAt,
    vector_get_Size,
    vector_IndexOf,
    vector_GetMany,
};

static HRESULT iterator_create( struct credential_vector *vector, struct credential_iterator **out );

static HRESULT WINAPI iterable_QueryInterface(
        __FIIterable_1_Windows__CSecurity__CCredentials__CPasswordCredential *iface, REFIID iid, void **out )
{
    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (!out) return E_POINTER;
    *out = NULL;

    if (IsEqualGUID( iid, &IID_IUnknown )
            || IsEqualGUID( iid, &IID_IInspectable )
            || IsEqualGUID( iid, &IID_IAgileObject )
            || IsEqualGUID( iid, &IID___FIIterable_1_Windows__CSecurity__CCredentials__CPasswordCredential ))
    {
        IUnknown_AddRef( (IUnknown *)iface );
        *out = iface;
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG WINAPI iterable_AddRef(
        __FIIterable_1_Windows__CSecurity__CCredentials__CPasswordCredential *iface )
{
    struct credential_vector *impl = impl_from_FIIterable_PasswordCredential( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI iterable_Release(
        __FIIterable_1_Windows__CSecurity__CCredentials__CPasswordCredential *iface )
{
    struct credential_vector *impl = impl_from_FIIterable_PasswordCredential( iface );
    LONG ref = InterlockedDecrement( &impl->ref );

    if (!ref) vector_destroy( impl );
    return ref;
}

static HRESULT WINAPI iterable_GetIids(
        __FIIterable_1_Windows__CSecurity__CCredentials__CPasswordCredential *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI iterable_GetRuntimeClassName(
        __FIIterable_1_Windows__CSecurity__CCredentials__CPasswordCredential *iface, HSTRING *class_name )
{
    static const WCHAR name[] = L"Windows.Foundation.Collections.IIterable`1<Windows.Security.Credentials.PasswordCredential>";

    if (!class_name) return E_POINTER;
    return WindowsCreateString( name, ARRAY_SIZE( name ) - 1, class_name );
}

static HRESULT WINAPI iterable_GetTrustLevel(
        __FIIterable_1_Windows__CSecurity__CCredentials__CPasswordCredential *iface, TrustLevel *trust_level )
{
    if (!trust_level) return E_POINTER;
    *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI iterable_First(
        __FIIterable_1_Windows__CSecurity__CCredentials__CPasswordCredential *iface,
        __FIIterator_1_Windows__CSecurity__CCredentials__CPasswordCredential **value )
{
    struct credential_vector *impl = impl_from_FIIterable_PasswordCredential( iface );
    struct credential_iterator *iter;
    HRESULT hr;

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    if (FAILED(hr = iterator_create( impl, &iter ))) return hr;
    *value = (__FIIterator_1_Windows__CSecurity__CCredentials__CPasswordCredential *)&iter->__FIIterator_1_Windows__CSecurity__CCredentials__CPasswordCredential_iface;
    return S_OK;
}

static const struct __FIIterable_1_Windows__CSecurity__CCredentials__CPasswordCredentialVtbl iterable_vtbl =
{
    iterable_QueryInterface,
    iterable_AddRef,
    iterable_Release,
    /* IInspectable methods */
    iterable_GetIids,
    iterable_GetRuntimeClassName,
    iterable_GetTrustLevel,
    /* IIterable<PasswordCredential> methods */
    iterable_First,
};

static HRESULT WINAPI iterator_QueryInterface(
        __FIIterator_1_Windows__CSecurity__CCredentials__CPasswordCredential *iface, REFIID iid, void **out )
{
    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (!out) return E_POINTER;
    *out = NULL;

    if (IsEqualGUID( iid, &IID_IUnknown )
            || IsEqualGUID( iid, &IID_IInspectable )
            || IsEqualGUID( iid, &IID_IAgileObject )
            || IsEqualGUID( iid, &IID___FIIterator_1_Windows__CSecurity__CCredentials__CPasswordCredential ))
    {
        IUnknown_AddRef( (IUnknown *)iface );
        *out = iface;
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG WINAPI iterator_AddRef(
        __FIIterator_1_Windows__CSecurity__CCredentials__CPasswordCredential *iface )
{
    struct credential_iterator *impl = impl_from_FIIterator_PasswordCredential( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI iterator_Release(
        __FIIterator_1_Windows__CSecurity__CCredentials__CPasswordCredential *iface )
{
    struct credential_iterator *impl = impl_from_FIIterator_PasswordCredential( iface );
    LONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        IUnknown_Release( (IUnknown *)&impl->vector->__FIVectorView_1_Windows__CSecurity__CCredentials__CPasswordCredential_iface );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI iterator_GetIids(
        __FIIterator_1_Windows__CSecurity__CCredentials__CPasswordCredential *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI iterator_GetRuntimeClassName(
        __FIIterator_1_Windows__CSecurity__CCredentials__CPasswordCredential *iface, HSTRING *class_name )
{
    static const WCHAR name[] = L"Windows.Foundation.Collections.IIterator`1<Windows.Security.Credentials.PasswordCredential>";

    if (!class_name) return E_POINTER;
    return WindowsCreateString( name, ARRAY_SIZE( name ) - 1, class_name );
}

static HRESULT WINAPI iterator_GetTrustLevel(
        __FIIterator_1_Windows__CSecurity__CCredentials__CPasswordCredential *iface, TrustLevel *trust_level )
{
    if (!trust_level) return E_POINTER;
    *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI iterator_get_HasCurrent(
        __FIIterator_1_Windows__CSecurity__CCredentials__CPasswordCredential *iface, boolean *value )
{
    struct credential_iterator *impl = impl_from_FIIterator_PasswordCredential( iface );

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    *value = impl->index < impl->vector->count;
    return S_OK;
}

static HRESULT WINAPI iterator_get_Current(
        __FIIterator_1_Windows__CSecurity__CCredentials__CPasswordCredential *iface, IPasswordCredential **value )
{
    struct credential_iterator *impl = impl_from_FIIterator_PasswordCredential( iface );

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    if (impl->index >= impl->vector->count) return E_BOUNDS;
    *value = impl->vector->items[impl->index];
    IPasswordCredential_AddRef( *value );
    return S_OK;
}

static HRESULT WINAPI iterator_MoveNext(
        __FIIterator_1_Windows__CSecurity__CCredentials__CPasswordCredential *iface, boolean *value )
{
    struct credential_iterator *impl = impl_from_FIIterator_PasswordCredential( iface );

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    if (impl->index < impl->vector->count) ++impl->index;
    *value = impl->index < impl->vector->count;
    return S_OK;
}

static HRESULT WINAPI iterator_GetMany(
        __FIIterator_1_Windows__CSecurity__CCredentials__CPasswordCredential *iface, UINT32 items_size,
        IPasswordCredential **items, UINT32 *value )
{
    struct credential_iterator *impl = impl_from_FIIterator_PasswordCredential( iface );
    ULONG written = 0;

    TRACE( "iface %p, items_size %u, items %p, value %p.\n", iface, items_size, items, value );

    if (!value) return E_POINTER;
    while (written < items_size && impl->index + written < impl->vector->count)
    {
        if (!items) return E_POINTER;
        items[written] = impl->vector->items[impl->index + written];
        IPasswordCredential_AddRef( items[written] );
        ++written;
    }
    impl->index += written;
    *value = written;
    return S_OK;
}

static const struct __FIIterator_1_Windows__CSecurity__CCredentials__CPasswordCredentialVtbl iterator_vtbl =
{
    iterator_QueryInterface,
    iterator_AddRef,
    iterator_Release,
    /* IInspectable methods */
    iterator_GetIids,
    iterator_GetRuntimeClassName,
    iterator_GetTrustLevel,
    /* IIterator<PasswordCredential> methods */
    iterator_get_Current,
    iterator_get_HasCurrent,
    iterator_MoveNext,
    iterator_GetMany,
};

static HRESULT iterator_create( struct credential_vector *vector, struct credential_iterator **out )
{
    struct credential_iterator *iter;

    if (!(iter = calloc( 1, sizeof(*iter) ))) return E_OUTOFMEMORY;
    iter->__FIIterator_1_Windows__CSecurity__CCredentials__CPasswordCredential_iface.lpVtbl = &iterator_vtbl;
    iter->ref = 1;
    iter->vector = vector;
    IUnknown_AddRef( (IUnknown *)&vector->__FIVectorView_1_Windows__CSecurity__CCredentials__CPasswordCredential_iface );
    *out = iter;
    return S_OK;
}

static HRESULT create_vector( IPasswordCredential **items, ULONG count,
                              __FIVectorView_1_Windows__CSecurity__CCredentials__CPasswordCredential **out )
{
    struct credential_vector *impl;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->__FIVectorView_1_Windows__CSecurity__CCredentials__CPasswordCredential_iface.lpVtbl = &vector_vtbl;
    impl->__FIIterable_1_Windows__CSecurity__CCredentials__CPasswordCredential_iface.lpVtbl = &iterable_vtbl;
    impl->ref = 1;
    impl->count = count;
    impl->items = items;

    *out = &impl->__FIVectorView_1_Windows__CSecurity__CCredentials__CPasswordCredential_iface;
    return S_OK;
}

/*
 * Enumerate the vault.  resource_filter/user_filter are matched
 * case-insensitively against the stored Resource/UserName values (NULL
 * means "all").  Callers hold the vault lock.
 */
static HRESULT vault_enumerate_locked( const WCHAR *resource_filter, const WCHAR *user_filter,
                                       __FIVectorView_1_Windows__CSecurity__CCredentials__CPasswordCredential **out )
{
    HKEY root;
    IPasswordCredential **items = NULL;
    ULONG size = 0, n = 0;
    DWORD index;
    HRESULT hr = S_OK;

    if (RegOpenKeyExW( HKEY_CURRENT_USER, VAULT_ROOT_KEY, 0, KEY_READ, &root ))
    {
        /* No vault key at all is a valid empty vault. */
        TRACE( "no vault key, returning empty vector.\n" );
        return create_vector( NULL, 0, out );
    }

    for (index = 0; ; ++index)
    {
        WCHAR name[1024];
        DWORD name_size = ARRAY_SIZE( name );
        HKEY hkey;
        WCHAR *resource = NULL, *username = NULL, *password = NULL;
        IPasswordCredential *credential = NULL;

        if (RegEnumKeyExW( root, index, name, &name_size, NULL, NULL, NULL, NULL )) break;
        if (RegOpenKeyExW( root, name, 0, KEY_READ, &hkey )) continue;

        hr = read_reg_sz( hkey, VAULT_VALUE_RESOURCE, &resource );
        if (SUCCEEDED(hr)) hr = read_reg_sz( hkey, VAULT_VALUE_USERNAME, &username );
        if (SUCCEEDED(hr)) hr = read_reg_password( hkey, &password );
        if (SUCCEEDED(hr))
        {
            if (!resource_filter || !lstrcmpiW( resource, resource_filter ))
            {
                if (!user_filter || !lstrcmpiW( username, user_filter ))
                {
                    hr = create_password_credential( resource, username, password, &credential );
                    if (SUCCEEDED(hr))
                    {
                        if (n == size)
                        {
                            IPasswordCredential **grown = realloc( items, (size + 8) * sizeof(*items) );
                            if (!grown)
                            {
                                IPasswordCredential_Release( credential );
                                hr = E_OUTOFMEMORY;
                            }
                            else
                            {
                                items = grown;
                                size += 8;
                                items[n++] = credential;
                                credential = NULL;
                            }
                        }
                        else items[n++] = credential;
                    }
                }
            }
        }

        RegCloseKey( hkey );
        free( resource );
        free( username );
        free( password );
        if (FAILED(hr)) goto done;
    }

    hr = create_vector( items, n, out );
    items = NULL;

done:
    RegCloseKey( root );
    if (FAILED(hr))
    {
        ULONG i;
        for (i = 0; i < n; ++i) IPasswordCredential_Release( items[i] );
        free( items );
    }
    return hr;
}

static HRESULT vault_enumerate( const WCHAR *resource_filter, const WCHAR *user_filter,
                                __FIVectorView_1_Windows__CSecurity__CCredentials__CPasswordCredential **out )
{
    HRESULT hr;

    vault_lock();
    hr = vault_enumerate_locked( resource_filter, user_filter, out );
    vault_unlock();
    return hr;
}

struct password_vault
{
    IPasswordVault IPasswordVault_iface;
    LONG ref;
};

static inline struct password_vault *impl_from_IPasswordVault( IPasswordVault *iface )
{
    return CONTAINING_RECORD( iface, struct password_vault, IPasswordVault_iface );
}

static HRESULT WINAPI vault_QueryInterface( IPasswordVault *iface, REFIID iid, void **out )
{
    struct password_vault *impl = impl_from_IPasswordVault( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (!out) return E_POINTER;
    *out = NULL;

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IPasswordVault ))
    {
        *out = &impl->IPasswordVault_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG WINAPI vault_AddRef( IPasswordVault *iface )
{
    struct password_vault *impl = impl_from_IPasswordVault( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI vault_Release( IPasswordVault *iface )
{
    struct password_vault *impl = impl_from_IPasswordVault( iface );
    LONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        TRACE( "destroying %p\n", iface );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI vault_GetIids( IPasswordVault *iface, ULONG *iid_count, IID **iids )
{
    static const IID iids_array[] =
    {
        { 0x61fd242f, 0xceec, 0x45a6, { 0x86, 0xe5, 0x53, 0xfb, 0x66, 0x37, 0xae, 0x8c } }, /* IID_IPasswordVault */
    };
    IID *array;

    if (!iid_count || !iids) return E_POINTER;
    if (!(array = CoTaskMemAlloc( sizeof(iids_array) ))) return E_OUTOFMEMORY;
    memcpy( array, iids_array, sizeof(iids_array) );
    *iid_count = ARRAY_SIZE(iids_array);
    *iids = array;
    return S_OK;
}

static HRESULT WINAPI vault_GetRuntimeClassName( IPasswordVault *iface, HSTRING *class_name )
{
    if (!class_name) return E_POINTER;
    return WindowsCreateString( RuntimeClass_Windows_Security_Credentials_PasswordVault,
                                sizeof(RuntimeClass_Windows_Security_Credentials_PasswordVault) / sizeof(WCHAR) - 1,
                                class_name );
}

static HRESULT WINAPI vault_GetTrustLevel( IPasswordVault *iface, TrustLevel *trust_level )
{
    if (!trust_level) return E_POINTER;
    *trust_level = FullTrust;
    return S_OK;
}

static HRESULT WINAPI vault_Add( IPasswordVault *iface, IPasswordCredential *credential )
{
    IPasswordCredential *cred = credential;
    WCHAR *resource = NULL, *username = NULL, *password = NULL;
    HRESULT hr;

    TRACE( "iface %p, credential %p.\n", iface, credential );

    if (!credential) return E_INVALIDARG;
    if (FAILED(hr = get_credential_string( cred, IPasswordCredential_get_Resource, &resource ))) return hr;
    hr = get_credential_string( cred, IPasswordCredential_get_UserName, &username );
    if (SUCCEEDED(hr)) hr = get_credential_string( cred, IPasswordCredential_get_Password, &password );
    if (SUCCEEDED(hr))
    {
        vault_lock();
        hr = store_credential( resource, username, password );
        vault_unlock();
    }

    free( resource );
    free( username );
    free( password );
    return hr;
}

static HRESULT WINAPI vault_Remove( IPasswordVault *iface, IPasswordCredential *credential )
{
    IPasswordCredential *cred = credential;
    WCHAR *resource = NULL, *username = NULL, *key, *path;
    LSTATUS status;
    HRESULT hr;

    TRACE( "iface %p, credential %p.\n", iface, credential );

    if (!credential) return E_INVALIDARG;
    if (FAILED(hr = get_credential_string( cred, IPasswordCredential_get_Resource, &resource ))) goto done;
    hr = get_credential_string( cred, IPasswordCredential_get_UserName, &username );
    if (FAILED(hr)) goto done;

    if (!(key = build_key( resource, username )))
    {
        hr = E_OUTOFMEMORY;
        goto done;
    }
    if (!(path = build_path( key )))
    {
        free( key );
        hr = E_OUTOFMEMORY;
        goto done;
    }
    free( key );

    vault_lock();
    status = RegDeleteKeyW( HKEY_CURRENT_USER, path );
    vault_unlock();
    free( path );
    /* RegDeleteKeyW succeeds for missing keys; a second Remove is a no-op,
     * matching Windows. */
    hr = status ? HRESULT_FROM_WIN32( status ) : S_OK;

done:
    free( resource );
    free( username );
    return hr;
}

static HRESULT WINAPI vault_Retrieve( IPasswordVault *iface, HSTRING resource, HSTRING username,
                                      IPasswordCredential **credential )
{
    const WCHAR *resource_buf = WindowsGetStringRawBuffer( resource, NULL );
    const WCHAR *username_buf = WindowsGetStringRawBuffer( username, NULL );
    WCHAR *key, *path, *password = NULL;
    HKEY hkey;
    LSTATUS status;
    HRESULT hr;

    TRACE( "iface %p, resource %s, username %s, credential %p.\n", iface, debugstr_hstring( resource ),
           debugstr_hstring( username ), credential );

    if (!resource || !username || !credential) return E_INVALIDARG;
    if (!(key = build_key( resource_buf, username_buf ))) return E_OUTOFMEMORY;
    if (!(path = build_path( key )))
    {
        free( key );
        return E_OUTOFMEMORY;
    }
    free( key );

    vault_lock();
    status = RegOpenKeyExW( HKEY_CURRENT_USER, path, 0, KEY_READ, &hkey );
    if (!status)
    {
        hr = read_reg_password( hkey, &password );
        RegCloseKey( hkey );
    }
    else hr = (status == ERROR_NOT_FOUND) ? HRESULT_FROM_WIN32( ERROR_NOT_FOUND )
                                          : HRESULT_FROM_WIN32( status );
    vault_unlock();
    free( path );

    if (SUCCEEDED(hr))
    {
        hr = create_password_credential( resource_buf, username_buf, password, credential );
        free( password );
    }
    return hr;
}

static HRESULT WINAPI vault_RetrieveAll( IPasswordVault *iface,
        __FIVectorView_1_Windows__CSecurity__CCredentials__CPasswordCredential **credentials )
{
    TRACE( "iface %p, credentials %p.\n", iface, credentials );

    if (!credentials) return E_POINTER;
    return vault_enumerate( NULL, NULL, credentials );
}

static HRESULT WINAPI vault_FindAllByResource( IPasswordVault *iface, HSTRING resource_name,
        __FIVectorView_1_Windows__CSecurity__CCredentials__CPasswordCredential **credentials )
{
    TRACE( "iface %p, resource_name %s, credentials %p.\n", iface, debugstr_hstring( resource_name ),
           credentials );

    if (!resource_name || !credentials) return E_INVALIDARG;
    return vault_enumerate( WindowsGetStringRawBuffer( resource_name, NULL ), NULL, credentials );
}

static HRESULT WINAPI vault_FindAllByUserName( IPasswordVault *iface, HSTRING username,
        __FIVectorView_1_Windows__CSecurity__CCredentials__CPasswordCredential **credentials )
{
    TRACE( "iface %p, username %s, credentials %p.\n", iface, debugstr_hstring( username ), credentials );

    if (!username || !credentials) return E_INVALIDARG;
    return vault_enumerate( NULL, WindowsGetStringRawBuffer( username, NULL ), credentials );
}

static const struct IPasswordVaultVtbl vault_vtbl =
{
    vault_QueryInterface,
    vault_AddRef,
    vault_Release,
    vault_GetIids,
    vault_GetRuntimeClassName,
    vault_GetTrustLevel,
    vault_Add,
    vault_Remove,
    vault_Retrieve,
    vault_RetrieveAll,
    vault_FindAllByResource,
    vault_FindAllByUserName,
};

IInspectable *create_password_vault( void )
{
    struct password_vault *impl = calloc( 1, sizeof(*impl) );

    if (!impl) return NULL;
    impl->IPasswordVault_iface.lpVtbl = &vault_vtbl;
    impl->ref = 1;
    return (IInspectable *)&impl->IPasswordVault_iface;
}
