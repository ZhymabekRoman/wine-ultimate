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


#include "private.h"
#include "wincred.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(creds);

/*
 * The vault is the WinRT projection of the Win32 Credential Manager: each
 * PasswordVault entry is one CRED_TYPE_GENERIC credential whose TargetName
 * is "<resource>:<userName>" (the same shape on real Windows, where the
 * vault stores resource and user in the target key).  Credential Manager
 * keys are case-insensitive, matching the vault's Retrieve() contract.
 *
 * The password is stored as the credential blob, as a NUL-terminated UTF-16
 * string including the terminator, which is what Windows' vault entries
 * carry for generic credentials.
 */

static WCHAR *build_target( const WCHAR *resource, const WCHAR *username )
{
    SIZE_T res_len = lstrlenW( resource );
    SIZE_T user_len = lstrlenW( username );
    WCHAR *target = malloc( (res_len + 1 + user_len + 1) * sizeof(WCHAR) );

    if (!target) return NULL;
    memcpy( target, resource, res_len * sizeof(WCHAR) );
    target[res_len] = L':';
    memcpy( target + res_len + 1, username, (user_len + 1) * sizeof(WCHAR) );
    return target;
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

static HRESULT store_credential( const WCHAR *resource, const WCHAR *username, const WCHAR *password )
{
    CREDENTIALW cred;
    WCHAR *target;
    BOOL ret;

    if (!(target = build_target( resource, username ))) return E_OUTOFMEMORY;

    memset( &cred, 0, sizeof(cred) );
    cred.Type = CRED_TYPE_GENERIC;
    cred.TargetName = target;
    cred.CredentialBlobSize = (DWORD)(lstrlenW( password ) + 1) * sizeof(WCHAR);
    cred.CredentialBlob = (LPVOID)password;
    cred.Persist = CRED_PERSIST_LOCAL_MACHINE;
    cred.UserName = (WCHAR *)username;

    ret = CredWriteW( &cred, 0 );
    free( target );
    return ret ? S_OK : HRESULT_FROM_WIN32( GetLastError() );
}

/* Copy the credential blob into a freshly allocated NUL-terminated string. */
static HRESULT blob_to_string( const CREDENTIALW *cred, WCHAR **out )
{
    DWORD chars = cred->CredentialBlobSize / sizeof(WCHAR);
    WCHAR *str;

    if (!cred->CredentialBlobSize)
    {
        if (!(str = malloc( sizeof(WCHAR) ))) return E_OUTOFMEMORY;
        str[0] = 0;
        *out = str;
        return S_OK;
    }
    if (!(str = malloc( (chars + 1) * sizeof(WCHAR) ))) return E_OUTOFMEMORY;
    memcpy( str, cred->CredentialBlob, cred->CredentialBlobSize );
    if (chars && str[chars - 1] == 0) chars--; /* blob already NUL-terminated */
    else str[chars] = 0;
    *out = str;
    return S_OK;
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
 * Enumerate the vault.  resource_filter/user_name_filter are matched
 * case-insensitively against the target name split at its first ':' (the
 * same first-separator rule build_target produces); NULL means "all".
 */
static HRESULT vault_enumerate( const WCHAR *resource_filter, const WCHAR *user_filter,
                                __FIVectorView_1_Windows__CSecurity__CCredentials__CPasswordCredential **out )
{
    PCREDENTIALW *creds = NULL;
    DWORD count = 0, i;
    IPasswordCredential **items = NULL;
    ULONG size = 0, n = 0;
    HRESULT hr = S_OK;

    if (!CredEnumerateW( NULL, 0, &count, &creds ))
    {
        TRACE( "CredEnumerateW failed, err %lu.\n", GetLastError() );
        /* An empty vault surfaces as not-found errors; a credential store
         * without the MountMgr device (driver not loaded) fails enumeration
         * outright.  Both mean "no credentials". */
        count = 0;
    }
    else TRACE( "CredEnumerateW count %lu.\n", count );

    for (i = 0; i < count; ++i)
    {
        WCHAR *target = creds[i]->TargetName;
        WCHAR *sep, *resource, *username;
        IPasswordCredential *credential = NULL;

        if (creds[i]->Type != CRED_TYPE_GENERIC || !target) continue;
        if (!(sep = wcschr( target, L':' ))) continue;
        *sep = 0;
        resource = target;
        username = sep + 1;

        if (resource_filter && lstrcmpiW( resource, resource_filter )) continue;
        if (user_filter && lstrcmpiW( username, user_filter )) continue;

        {
            WCHAR *password = NULL;
            if (FAILED(hr = blob_to_string( creds[i], &password ))) goto done;
            hr = create_password_credential( resource, username, password, &credential );
            free( password );
            if (FAILED(hr)) goto done;
        }

        if (n == size)
        {
            IPasswordCredential **grown = realloc( items, (size + 8) * sizeof(*items) );
            if (!grown)
            {
                IPasswordCredential_Release( credential );
                hr = E_OUTOFMEMORY;
                goto done;
            }
            items = grown;
            size += 8;
        }
        items[n++] = credential;
    }

    hr = create_vector( items, n, out );
    items = NULL;

done:
    CredFree( creds );
    if (FAILED(hr))
    {
        for (i = 0; i < n; ++i) IPasswordCredential_Release( items[i] );
        free( items );
    }
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
    if (SUCCEEDED(hr)) hr = store_credential( resource, username, password );

    free( resource );
    free( username );
    free( password );
    return hr;
}

static HRESULT WINAPI vault_Remove( IPasswordVault *iface, IPasswordCredential *credential )
{
    IPasswordCredential *cred = credential;
    WCHAR *resource = NULL, *username = NULL;
    WCHAR *target;
    BOOL ret;
    HRESULT hr;

    TRACE( "iface %p, credential %p.\n", iface, credential );

    if (!credential) return E_INVALIDARG;
    if (FAILED(hr = get_credential_string( cred, IPasswordCredential_get_Resource, &resource ))) goto done;
    hr = get_credential_string( cred, IPasswordCredential_get_UserName, &username );
    if (FAILED(hr)) goto done;

    if (!(target = build_target( resource, username )))
    {
        hr = E_OUTOFMEMORY;
        goto done;
    }
    ret = CredDeleteW( target, CRED_TYPE_GENERIC, 0 );
    free( target );
    if (!ret)
    {
        DWORD err = GetLastError();
        hr = (err == ERROR_NOT_FOUND) ? S_OK : HRESULT_FROM_WIN32( err );
        goto done;
    }
    hr = S_OK;

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
    CREDENTIALW *cred;
    WCHAR *target, *password = NULL;
    BOOL ret;
    HRESULT hr;

    TRACE( "iface %p, resource %s, username %s, credential %p.\n", iface, debugstr_hstring( resource ),
           debugstr_hstring( username ), credential );

    if (!resource || !username || !credential) return E_INVALIDARG;
    if (!(target = build_target( resource_buf, username_buf ))) return E_OUTOFMEMORY;

    ret = CredReadW( target, CRED_TYPE_GENERIC, 0, &cred );
    free( target );
    if (!ret) return HRESULT_FROM_WIN32( GetLastError() );

    if (SUCCEEDED(hr = blob_to_string( cred, &password )))
    {
        hr = create_password_credential( resource_buf, username_buf, password, credential );
        free( password );
    }
    CredFree( cred );
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
