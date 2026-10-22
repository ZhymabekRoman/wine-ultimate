/*
 * Windows.Security.Cryptography.Core implementation
 *
 * Copyright 2026 Zhymabek Roman
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
#include "bcrypt.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(crypto);

/* -------------------------------------------------------------------------
 * CryptographicKey
 * ------------------------------------------------------------------------- */
struct crypto_key
{
    ICryptographicKey ICryptographicKey_iface;
    LONG ref;
    BYTE *key_data;
    UINT32 key_len;
    WCHAR *alg_name;
};

static inline struct crypto_key *impl_from_ICryptographicKey( ICryptographicKey *iface )
{
    return CONTAINING_RECORD( iface, struct crypto_key, ICryptographicKey_iface );
}

static HRESULT WINAPI crypto_key_QueryInterface( ICryptographicKey *iface, REFIID iid, void **out )
{
    struct crypto_key *impl = impl_from_ICryptographicKey( iface );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_ICryptographicKey ))
    {
        InterlockedIncrement( &impl->ref );
        *out = &impl->ICryptographicKey_iface;
        return S_OK;
    }

    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI crypto_key_AddRef( ICryptographicKey *iface )
{
    struct crypto_key *impl = impl_from_ICryptographicKey( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI crypto_key_Release( ICryptographicKey *iface )
{
    struct crypto_key *impl = impl_from_ICryptographicKey( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        free( impl->key_data );
        free( impl->alg_name );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI crypto_key_GetIids( ICryptographicKey *iface, ULONG *count, IID **iids ) { return E_NOTIMPL; }
static HRESULT WINAPI crypto_key_GetRuntimeClassName( ICryptographicKey *iface, HSTRING *name )
{
    return WindowsCreateString( RuntimeClass_Windows_Security_Cryptography_Core_CryptographicKey,
                                wcslen( RuntimeClass_Windows_Security_Cryptography_Core_CryptographicKey ), name );
}
static HRESULT WINAPI crypto_key_GetTrustLevel( ICryptographicKey *iface, TrustLevel *level ) { if (level) *level = BaseTrust; return S_OK; }

static HRESULT WINAPI crypto_key_get_KeySize( ICryptographicKey *iface, UINT32 *value )
{
    struct crypto_key *impl = impl_from_ICryptographicKey( iface );
    if (!value) return E_POINTER;
    *value = impl->key_len * 8;
    return S_OK;
}

static HRESULT WINAPI crypto_key_ExportDefaultPrivateKeyBlobType( ICryptographicKey *iface, IBuffer **value )
{
    struct crypto_key *impl = impl_from_ICryptographicKey( iface );
    if (!value) return E_POINTER;
    return create_crypt_buffer( impl->key_len, impl->key_len, impl->key_data, value );
}

static HRESULT WINAPI crypto_key_ExportPrivateKeyWithBlobType( ICryptographicKey *iface,
        CryptographicPrivateKeyBlobType BlobType, IBuffer **value )
{
    struct crypto_key *impl = impl_from_ICryptographicKey( iface );
    if (!value) return E_POINTER;
    return create_crypt_buffer( impl->key_len, impl->key_len, impl->key_data, value );
}

static HRESULT WINAPI crypto_key_ExportDefaultPublicKeyBlobType( ICryptographicKey *iface, IBuffer **value )
{
    struct crypto_key *impl = impl_from_ICryptographicKey( iface );
    if (!value) return E_POINTER;
    return create_crypt_buffer( impl->key_len, impl->key_len, impl->key_data, value );
}

static HRESULT WINAPI crypto_key_ExportPublicKeyWithBlobType( ICryptographicKey *iface,
        CryptographicPublicKeyBlobType BlobType, IBuffer **value )
{
    struct crypto_key *impl = impl_from_ICryptographicKey( iface );
    if (!value) return E_POINTER;
    return create_crypt_buffer( impl->key_len, impl->key_len, impl->key_data, value );
}

static const struct ICryptographicKeyVtbl crypto_key_vtbl =
{
    crypto_key_QueryInterface,
    crypto_key_AddRef,
    crypto_key_Release,
    crypto_key_GetIids,
    crypto_key_GetRuntimeClassName,
    crypto_key_GetTrustLevel,
    crypto_key_get_KeySize,
    crypto_key_ExportDefaultPrivateKeyBlobType,
    crypto_key_ExportPrivateKeyWithBlobType,
    crypto_key_ExportDefaultPublicKeyBlobType,
    crypto_key_ExportPublicKeyWithBlobType,
};

static HRESULT create_cryptographic_key( const BYTE *data, UINT32 len, const WCHAR *alg_name, ICryptographicKey **out )
{
    struct crypto_key *impl;
    if (!out) return E_POINTER;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->ICryptographicKey_iface.lpVtbl = &crypto_key_vtbl;
    impl->ref = 1;
    if (len && data)
    {
        if (!(impl->key_data = malloc( len )))
        {
            free( impl );
            return E_OUTOFMEMORY;
        }
        memcpy( impl->key_data, data, len );
        impl->key_len = len;
    }
    if (alg_name) impl->alg_name = wcsdup( alg_name );
    *out = &impl->ICryptographicKey_iface;
    return S_OK;
}

/* -------------------------------------------------------------------------
 * EncryptedAndAuthenticatedData
 * ------------------------------------------------------------------------- */
struct encrypted_data
{
    IEncryptedAndAuthenticatedData IEncryptedAndAuthenticatedData_iface;
    LONG ref;
    IBuffer *encrypted_data;
    IBuffer *authentication_tag;
};

static inline struct encrypted_data *impl_from_IEncryptedAndAuthenticatedData( IEncryptedAndAuthenticatedData *iface )
{
    return CONTAINING_RECORD( iface, struct encrypted_data, IEncryptedAndAuthenticatedData_iface );
}

static HRESULT WINAPI encrypted_data_QueryInterface( IEncryptedAndAuthenticatedData *iface, REFIID iid, void **out )
{
    struct encrypted_data *impl = impl_from_IEncryptedAndAuthenticatedData( iface );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IEncryptedAndAuthenticatedData ))
    {
        InterlockedIncrement( &impl->ref );
        *out = &impl->IEncryptedAndAuthenticatedData_iface;
        return S_OK;
    }

    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI encrypted_data_AddRef( IEncryptedAndAuthenticatedData *iface )
{
    struct encrypted_data *impl = impl_from_IEncryptedAndAuthenticatedData( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI encrypted_data_Release( IEncryptedAndAuthenticatedData *iface )
{
    struct encrypted_data *impl = impl_from_IEncryptedAndAuthenticatedData( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        if (impl->encrypted_data) IBuffer_Release( impl->encrypted_data );
        if (impl->authentication_tag) IBuffer_Release( impl->authentication_tag );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI encrypted_data_GetIids( IEncryptedAndAuthenticatedData *iface, ULONG *count, IID **iids ) { return E_NOTIMPL; }
static HRESULT WINAPI encrypted_data_GetRuntimeClassName( IEncryptedAndAuthenticatedData *iface, HSTRING *name )
{
    return WindowsCreateString( RuntimeClass_Windows_Security_Cryptography_Core_EncryptedAndAuthenticatedData,
                                wcslen( RuntimeClass_Windows_Security_Cryptography_Core_EncryptedAndAuthenticatedData ), name );
}
static HRESULT WINAPI encrypted_data_GetTrustLevel( IEncryptedAndAuthenticatedData *iface, TrustLevel *level ) { if (level) *level = BaseTrust; return S_OK; }

static HRESULT WINAPI encrypted_data_get_EncryptedData( IEncryptedAndAuthenticatedData *iface, IBuffer **value )
{
    struct encrypted_data *impl = impl_from_IEncryptedAndAuthenticatedData( iface );
    if (!value) return E_POINTER;
    if (impl->encrypted_data) IBuffer_AddRef( impl->encrypted_data );
    *value = impl->encrypted_data;
    return S_OK;
}

static HRESULT WINAPI encrypted_data_get_AuthenticationTag( IEncryptedAndAuthenticatedData *iface, IBuffer **value )
{
    struct encrypted_data *impl = impl_from_IEncryptedAndAuthenticatedData( iface );
    if (!value) return E_POINTER;
    if (impl->authentication_tag) IBuffer_AddRef( impl->authentication_tag );
    *value = impl->authentication_tag;
    return S_OK;
}

static const struct IEncryptedAndAuthenticatedDataVtbl encrypted_data_vtbl =
{
    encrypted_data_QueryInterface,
    encrypted_data_AddRef,
    encrypted_data_Release,
    encrypted_data_GetIids,
    encrypted_data_GetRuntimeClassName,
    encrypted_data_GetTrustLevel,
    encrypted_data_get_EncryptedData,
    encrypted_data_get_AuthenticationTag,
};

static HRESULT create_encrypted_and_authenticated_data( IBuffer *enc, IBuffer *tag, IEncryptedAndAuthenticatedData **out )
{
    struct encrypted_data *impl;
    if (!out) return E_POINTER;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->IEncryptedAndAuthenticatedData_iface.lpVtbl = &encrypted_data_vtbl;
    impl->ref = 1;
    if (enc) { IBuffer_AddRef( enc ); impl->encrypted_data = enc; }
    if (tag) { IBuffer_AddRef( tag ); impl->authentication_tag = tag; }
    *out = &impl->IEncryptedAndAuthenticatedData_iface;
    return S_OK;
}

/* -------------------------------------------------------------------------
 * KeyDerivationParameters
 * ------------------------------------------------------------------------- */
struct keyderivation_params
{
    IKeyDerivationParameters IKeyDerivationParameters_iface;
    LONG ref;
    IBuffer *kdf_generic_binary;
    UINT32 iteration_count;
};

static inline struct keyderivation_params *impl_from_IKeyDerivationParameters( IKeyDerivationParameters *iface )
{
    return CONTAINING_RECORD( iface, struct keyderivation_params, IKeyDerivationParameters_iface );
}

static HRESULT WINAPI kd_params_QueryInterface( IKeyDerivationParameters *iface, REFIID iid, void **out )
{
    struct keyderivation_params *impl = impl_from_IKeyDerivationParameters( iface );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IKeyDerivationParameters ))
    {
        InterlockedIncrement( &impl->ref );
        *out = &impl->IKeyDerivationParameters_iface;
        return S_OK;
    }

    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI kd_params_AddRef( IKeyDerivationParameters *iface )
{
    struct keyderivation_params *impl = impl_from_IKeyDerivationParameters( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI kd_params_Release( IKeyDerivationParameters *iface )
{
    struct keyderivation_params *impl = impl_from_IKeyDerivationParameters( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        if (impl->kdf_generic_binary) IBuffer_Release( impl->kdf_generic_binary );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI kd_params_GetIids( IKeyDerivationParameters *iface, ULONG *count, IID **iids ) { return E_NOTIMPL; }
static HRESULT WINAPI kd_params_GetRuntimeClassName( IKeyDerivationParameters *iface, HSTRING *name )
{
    return WindowsCreateString( RuntimeClass_Windows_Security_Cryptography_Core_KeyDerivationParameters,
                                wcslen( RuntimeClass_Windows_Security_Cryptography_Core_KeyDerivationParameters ), name );
}
static HRESULT WINAPI kd_params_GetTrustLevel( IKeyDerivationParameters *iface, TrustLevel *level ) { if (level) *level = BaseTrust; return S_OK; }

static HRESULT WINAPI kd_params_get_KdfGenericBinary( IKeyDerivationParameters *iface, IBuffer **value )
{
    struct keyderivation_params *impl = impl_from_IKeyDerivationParameters( iface );
    if (!value) return E_POINTER;
    if (impl->kdf_generic_binary) IBuffer_AddRef( impl->kdf_generic_binary );
    *value = impl->kdf_generic_binary;
    return S_OK;
}

static HRESULT WINAPI kd_params_put_KdfGenericBinary( IKeyDerivationParameters *iface, IBuffer *value )
{
    struct keyderivation_params *impl = impl_from_IKeyDerivationParameters( iface );
    if (impl->kdf_generic_binary) IBuffer_Release( impl->kdf_generic_binary );
    if (value) IBuffer_AddRef( value );
    impl->kdf_generic_binary = value;
    return S_OK;
}

static HRESULT WINAPI kd_params_get_IterationCount( IKeyDerivationParameters *iface, UINT32 *value )
{
    struct keyderivation_params *impl = impl_from_IKeyDerivationParameters( iface );
    if (!value) return E_POINTER;
    *value = impl->iteration_count;
    return S_OK;
}

static const struct IKeyDerivationParametersVtbl kd_params_vtbl =
{
    kd_params_QueryInterface,
    kd_params_AddRef,
    kd_params_Release,
    kd_params_GetIids,
    kd_params_GetRuntimeClassName,
    kd_params_GetTrustLevel,
    kd_params_get_KdfGenericBinary,
    kd_params_put_KdfGenericBinary,
    kd_params_get_IterationCount,
};

static HRESULT create_keyderivation_params( IBuffer *salt, UINT32 iterations, IKeyDerivationParameters **out )
{
    struct keyderivation_params *impl;
    if (!out) return E_POINTER;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->IKeyDerivationParameters_iface.lpVtbl = &kd_params_vtbl;
    impl->ref = 1;
    if (salt) { IBuffer_AddRef( salt ); impl->kdf_generic_binary = salt; }
    impl->iteration_count = iterations;
    *out = &impl->IKeyDerivationParameters_iface;
    return S_OK;
}

struct keyderivationparams_factory
{
    IActivationFactory IActivationFactory_iface;
    IKeyDerivationParametersStatics IKeyDerivationParametersStatics_iface;
    LONG ref;
};

static inline struct keyderivationparams_factory *impl_from_IActivationFactory_kdp( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct keyderivationparams_factory, IActivationFactory_iface );
}

static HRESULT WINAPI kdp_factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct keyderivationparams_factory *factory = impl_from_IActivationFactory_kdp( iface );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        IActivationFactory_AddRef( iface );
        *out = &factory->IActivationFactory_iface;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IKeyDerivationParametersStatics ))
    {
        IActivationFactory_AddRef( iface );
        *out = &factory->IKeyDerivationParametersStatics_iface;
        return S_OK;
    }

    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI kdp_factory_AddRef( IActivationFactory *iface ) { return 2; }
static ULONG WINAPI kdp_factory_Release( IActivationFactory *iface ) { return 1; }
static HRESULT WINAPI kdp_factory_GetIids( IActivationFactory *iface, ULONG *count, IID **iids ) { return E_NOTIMPL; }
static HRESULT WINAPI kdp_factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *name )
{
    return WindowsCreateString( RuntimeClass_Windows_Security_Cryptography_Core_KeyDerivationParameters,
                                wcslen( RuntimeClass_Windows_Security_Cryptography_Core_KeyDerivationParameters ), name );
}
static HRESULT WINAPI kdp_factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *level ) { if (level) *level = BaseTrust; return S_OK; }
static HRESULT WINAPI kdp_factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance ) { return E_NOTIMPL; }

static const struct IActivationFactoryVtbl kdp_factory_vtbl =
{
    kdp_factory_QueryInterface,
    kdp_factory_AddRef,
    kdp_factory_Release,
    kdp_factory_GetIids,
    kdp_factory_GetRuntimeClassName,
    kdp_factory_GetTrustLevel,
    kdp_factory_ActivateInstance,
};

DEFINE_IINSPECTABLE( kdp_statics, IKeyDerivationParametersStatics, struct keyderivationparams_factory, IActivationFactory_iface )

static HRESULT WINAPI kdp_statics_BuildForPbkdf2( IKeyDerivationParametersStatics *iface,
        IBuffer *pbkdf2Salt, UINT32 iterationCount, IKeyDerivationParameters **value )
{
    TRACE( "iface %p, salt %p, iterations %u, value %p\n", iface, pbkdf2Salt, iterationCount, value );
    return create_keyderivation_params( pbkdf2Salt, iterationCount, value );
}

static HRESULT WINAPI kdp_statics_BuildForSP800108( IKeyDerivationParametersStatics *iface,
        IBuffer *label, IBuffer *context, IKeyDerivationParameters **value )
{
    FIXME( "stub!\n" );
    return create_keyderivation_params( label, 1, value );
}

static HRESULT WINAPI kdp_statics_BuildForSP80056a( IKeyDerivationParametersStatics *iface,
        IBuffer *algorithmId, IBuffer *partyUInfo, IBuffer *partyVInfo,
        IBuffer *suppPubInfo, IBuffer *suppPrivInfo, IKeyDerivationParameters **value )
{
    FIXME( "stub!\n" );
    return create_keyderivation_params( algorithmId, 1, value );
}

static const struct IKeyDerivationParametersStaticsVtbl kdp_statics_vtbl =
{
    kdp_statics_QueryInterface,
    kdp_statics_AddRef,
    kdp_statics_Release,
    kdp_statics_GetIids,
    kdp_statics_GetRuntimeClassName,
    kdp_statics_GetTrustLevel,
    kdp_statics_BuildForPbkdf2,
    kdp_statics_BuildForSP800108,
    kdp_statics_BuildForSP80056a,
};

static struct keyderivationparams_factory kdp_factory_obj =
{
    {&kdp_factory_vtbl},
    {&kdp_statics_vtbl},
    1
};

IActivationFactory *keyderivationparams_activation_factory = &kdp_factory_obj.IActivationFactory_iface;

/* -------------------------------------------------------------------------
 * SymmetricKeyAlgorithmProvider
 * ------------------------------------------------------------------------- */
struct symmetric_provider
{
    ISymmetricKeyAlgorithmProvider ISymmetricKeyAlgorithmProvider_iface;
    LONG ref;
    WCHAR *alg_name;
};

static inline struct symmetric_provider *impl_from_ISymmetricKeyAlgorithmProvider( ISymmetricKeyAlgorithmProvider *iface )
{
    return CONTAINING_RECORD( iface, struct symmetric_provider, ISymmetricKeyAlgorithmProvider_iface );
}

static HRESULT WINAPI symm_prov_QueryInterface( ISymmetricKeyAlgorithmProvider *iface, REFIID iid, void **out )
{
    struct symmetric_provider *impl = impl_from_ISymmetricKeyAlgorithmProvider( iface );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_ISymmetricKeyAlgorithmProvider ))
    {
        InterlockedIncrement( &impl->ref );
        *out = &impl->ISymmetricKeyAlgorithmProvider_iface;
        return S_OK;
    }

    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI symm_prov_AddRef( ISymmetricKeyAlgorithmProvider *iface )
{
    struct symmetric_provider *impl = impl_from_ISymmetricKeyAlgorithmProvider( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI symm_prov_Release( ISymmetricKeyAlgorithmProvider *iface )
{
    struct symmetric_provider *impl = impl_from_ISymmetricKeyAlgorithmProvider( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        free( impl->alg_name );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI symm_prov_GetIids( ISymmetricKeyAlgorithmProvider *iface, ULONG *count, IID **iids ) { return E_NOTIMPL; }
static HRESULT WINAPI symm_prov_GetRuntimeClassName( ISymmetricKeyAlgorithmProvider *iface, HSTRING *name )
{
    return WindowsCreateString( RuntimeClass_Windows_Security_Cryptography_Core_SymmetricKeyAlgorithmProvider,
                                wcslen( RuntimeClass_Windows_Security_Cryptography_Core_SymmetricKeyAlgorithmProvider ), name );
}
static HRESULT WINAPI symm_prov_GetTrustLevel( ISymmetricKeyAlgorithmProvider *iface, TrustLevel *level ) { if (level) *level = BaseTrust; return S_OK; }

static HRESULT WINAPI symm_prov_get_AlgorithmName( ISymmetricKeyAlgorithmProvider *iface, HSTRING *value )
{
    struct symmetric_provider *impl = impl_from_ISymmetricKeyAlgorithmProvider( iface );
    if (!value) return E_POINTER;
    return WindowsCreateString( impl->alg_name, wcslen( impl->alg_name ), value );
}

static HRESULT WINAPI symm_prov_get_BlockLength( ISymmetricKeyAlgorithmProvider *iface, UINT32 *value )
{
    struct symmetric_provider *impl = impl_from_ISymmetricKeyAlgorithmProvider( iface );
    if (!value) return E_POINTER;
    if (impl->alg_name && !wcsicmp( impl->alg_name, L"RC4" )) *value = 1;
    else if (impl->alg_name && (wcsstr( impl->alg_name, L"DES" ) || wcsstr( impl->alg_name, L"3DES" ))) *value = 8;
    else *value = 16; /* AES default block length in bytes */
    return S_OK;
}

static HRESULT WINAPI symm_prov_CreateSymmetricKey( ISymmetricKeyAlgorithmProvider *iface,
        IBuffer *keyMaterial, ICryptographicKey **key )
{
    struct symmetric_provider *impl = impl_from_ISymmetricKeyAlgorithmProvider( iface );
    BYTE *data = NULL;
    UINT32 len = 0;
    HRESULT hr;

    TRACE( "iface %p, keyMaterial %p, key %p\n", iface, keyMaterial, key );
    if (FAILED(hr = get_buffer_data( keyMaterial, &data, &len ))) return hr;
    return create_cryptographic_key( data, len, impl->alg_name, key );
}

static const struct ISymmetricKeyAlgorithmProviderVtbl symm_prov_vtbl =
{
    symm_prov_QueryInterface,
    symm_prov_AddRef,
    symm_prov_Release,
    symm_prov_GetIids,
    symm_prov_GetRuntimeClassName,
    symm_prov_GetTrustLevel,
    symm_prov_get_AlgorithmName,
    symm_prov_get_BlockLength,
    symm_prov_CreateSymmetricKey,
};

static HRESULT create_symmetric_provider( const WCHAR *alg_name, ISymmetricKeyAlgorithmProvider **out )
{
    struct symmetric_provider *impl;
    if (!out) return E_POINTER;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->ISymmetricKeyAlgorithmProvider_iface.lpVtbl = &symm_prov_vtbl;
    impl->ref = 1;
    if (alg_name) impl->alg_name = wcsdup( alg_name );
    *out = &impl->ISymmetricKeyAlgorithmProvider_iface;
    return S_OK;
}

struct symmetrickey_factory
{
    IActivationFactory IActivationFactory_iface;
    ISymmetricKeyAlgorithmProviderStatics ISymmetricKeyAlgorithmProviderStatics_iface;
    LONG ref;
};

static inline struct symmetrickey_factory *impl_from_IActivationFactory_sym( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct symmetrickey_factory, IActivationFactory_iface );
}

static HRESULT WINAPI sym_factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct symmetrickey_factory *factory = impl_from_IActivationFactory_sym( iface );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        IActivationFactory_AddRef( iface );
        *out = &factory->IActivationFactory_iface;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_ISymmetricKeyAlgorithmProviderStatics ))
    {
        IActivationFactory_AddRef( iface );
        *out = &factory->ISymmetricKeyAlgorithmProviderStatics_iface;
        return S_OK;
    }

    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI sym_factory_AddRef( IActivationFactory *iface ) { return 2; }
static ULONG WINAPI sym_factory_Release( IActivationFactory *iface ) { return 1; }
static HRESULT WINAPI sym_factory_GetIids( IActivationFactory *iface, ULONG *count, IID **iids ) { return E_NOTIMPL; }
static HRESULT WINAPI sym_factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *name )
{
    return WindowsCreateString( RuntimeClass_Windows_Security_Cryptography_Core_SymmetricKeyAlgorithmProvider,
                                wcslen( RuntimeClass_Windows_Security_Cryptography_Core_SymmetricKeyAlgorithmProvider ), name );
}
static HRESULT WINAPI sym_factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *level ) { if (level) *level = BaseTrust; return S_OK; }
static HRESULT WINAPI sym_factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance ) { return E_NOTIMPL; }

static const struct IActivationFactoryVtbl sym_factory_vtbl =
{
    sym_factory_QueryInterface,
    sym_factory_AddRef,
    sym_factory_Release,
    sym_factory_GetIids,
    sym_factory_GetRuntimeClassName,
    sym_factory_GetTrustLevel,
    sym_factory_ActivateInstance,
};

DEFINE_IINSPECTABLE( sym_statics, ISymmetricKeyAlgorithmProviderStatics, struct symmetrickey_factory, IActivationFactory_iface )

static HRESULT WINAPI sym_statics_OpenAlgorithm( ISymmetricKeyAlgorithmProviderStatics *iface,
        HSTRING algorithm, ISymmetricKeyAlgorithmProvider **provider )
{
    const WCHAR *str = WindowsGetStringRawBuffer( algorithm, NULL );
    TRACE( "iface %p, algorithm %s, provider %p\n", iface, debugstr_w(str), provider );
    return create_symmetric_provider( str, provider );
}

static const struct ISymmetricKeyAlgorithmProviderStaticsVtbl sym_statics_vtbl =
{
    sym_statics_QueryInterface,
    sym_statics_AddRef,
    sym_statics_Release,
    sym_statics_GetIids,
    sym_statics_GetRuntimeClassName,
    sym_statics_GetTrustLevel,
    sym_statics_OpenAlgorithm,
};

static struct symmetrickey_factory sym_factory_obj =
{
    {&sym_factory_vtbl},
    {&sym_statics_vtbl},
    1
};

IActivationFactory *symmetrickey_activation_factory = &sym_factory_obj.IActivationFactory_iface;

/* -------------------------------------------------------------------------
 * CryptographicHash (IHashComputation)
 * ------------------------------------------------------------------------- */
struct crypto_hash
{
    IHashComputation IHashComputation_iface;
    LONG ref;
    BCRYPT_ALG_HANDLE hAlg;
    BCRYPT_HASH_HANDLE hHash;
    UINT32 hash_len;
};

static inline struct crypto_hash *impl_from_IHashComputation( IHashComputation *iface )
{
    return CONTAINING_RECORD( iface, struct crypto_hash, IHashComputation_iface );
}

static HRESULT WINAPI crypto_hash_QueryInterface( IHashComputation *iface, REFIID iid, void **out )
{
    struct crypto_hash *impl = impl_from_IHashComputation( iface );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IHashComputation ))
    {
        InterlockedIncrement( &impl->ref );
        *out = &impl->IHashComputation_iface;
        return S_OK;
    }

    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI crypto_hash_AddRef( IHashComputation *iface )
{
    struct crypto_hash *impl = impl_from_IHashComputation( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI crypto_hash_Release( IHashComputation *iface )
{
    struct crypto_hash *impl = impl_from_IHashComputation( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        if (impl->hHash) BCryptDestroyHash( impl->hHash );
        if (impl->hAlg) BCryptCloseAlgorithmProvider( impl->hAlg, 0 );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI crypto_hash_GetIids( IHashComputation *iface, ULONG *count, IID **iids ) { return E_NOTIMPL; }
static HRESULT WINAPI crypto_hash_GetRuntimeClassName( IHashComputation *iface, HSTRING *name )
{
    return WindowsCreateString( RuntimeClass_Windows_Security_Cryptography_Core_CryptographicHash,
                                wcslen( RuntimeClass_Windows_Security_Cryptography_Core_CryptographicHash ), name );
}
static HRESULT WINAPI crypto_hash_GetTrustLevel( IHashComputation *iface, TrustLevel *level ) { if (level) *level = BaseTrust; return S_OK; }

static HRESULT WINAPI crypto_hash_Append( IHashComputation *iface, IBuffer *data )
{
    struct crypto_hash *impl = impl_from_IHashComputation( iface );
    BYTE *data_bytes = NULL;
    UINT32 data_len = 0;
    HRESULT hr;

    if (FAILED(hr = get_buffer_data( data, &data_bytes, &data_len ))) return hr;
    if (data_bytes && data_len)
    {
        NTSTATUS status = BCryptHashData( impl->hHash, data_bytes, data_len, 0 );
        if (status) return E_FAIL;
    }
    return S_OK;
}

static HRESULT WINAPI crypto_hash_GetValueAndReset( IHashComputation *iface, IBuffer **value )
{
    struct crypto_hash *impl = impl_from_IHashComputation( iface );
    BYTE hash_buf[64] = {0};
    NTSTATUS status;
    HRESULT hr;

    if (!value) return E_POINTER;
    status = BCryptFinishHash( impl->hHash, hash_buf, impl->hash_len, 0 );
    if (status) return E_FAIL;

    BCryptDestroyHash( impl->hHash );
    impl->hHash = NULL;
    status = BCryptCreateHash( impl->hAlg, &impl->hHash, NULL, 0, NULL, 0, 0 );
    if (status) return E_FAIL;

    hr = create_crypt_buffer( impl->hash_len, impl->hash_len, hash_buf, value );
    return hr;
}

static const struct IHashComputationVtbl crypto_hash_vtbl =
{
    crypto_hash_QueryInterface,
    crypto_hash_AddRef,
    crypto_hash_Release,
    crypto_hash_GetIids,
    crypto_hash_GetRuntimeClassName,
    crypto_hash_GetTrustLevel,
    crypto_hash_Append,
    crypto_hash_GetValueAndReset,
};

static HRESULT create_crypto_hash( const WCHAR *bcrypt_alg, UINT32 hash_len, IHashComputation **out )
{
    struct crypto_hash *impl;
    NTSTATUS status;

    if (!out) return E_POINTER;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->IHashComputation_iface.lpVtbl = &crypto_hash_vtbl;
    impl->ref = 1;
    impl->hash_len = hash_len;

    status = BCryptOpenAlgorithmProvider( &impl->hAlg, bcrypt_alg, NULL, 0 );
    if (status)
    {
        free( impl );
        return E_FAIL;
    }
    status = BCryptCreateHash( impl->hAlg, &impl->hHash, NULL, 0, NULL, 0, 0 );
    if (status)
    {
        BCryptCloseAlgorithmProvider( impl->hAlg, 0 );
        free( impl );
        return E_FAIL;
    }

    *out = &impl->IHashComputation_iface;
    return S_OK;
}

/* -------------------------------------------------------------------------
 * HashAlgorithmProvider
 * ------------------------------------------------------------------------- */
struct hash_provider
{
    IHashAlgorithmProvider IHashAlgorithmProvider_iface;
    LONG ref;
    WCHAR *alg_name;
    const WCHAR *bcrypt_alg;
    UINT32 hash_len;
};

static inline struct hash_provider *impl_from_IHashAlgorithmProvider( IHashAlgorithmProvider *iface )
{
    return CONTAINING_RECORD( iface, struct hash_provider, IHashAlgorithmProvider_iface );
}

static HRESULT WINAPI hash_prov_QueryInterface( IHashAlgorithmProvider *iface, REFIID iid, void **out )
{
    struct hash_provider *impl = impl_from_IHashAlgorithmProvider( iface );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IHashAlgorithmProvider ))
    {
        InterlockedIncrement( &impl->ref );
        *out = &impl->IHashAlgorithmProvider_iface;
        return S_OK;
    }

    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI hash_prov_AddRef( IHashAlgorithmProvider *iface )
{
    struct hash_provider *impl = impl_from_IHashAlgorithmProvider( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI hash_prov_Release( IHashAlgorithmProvider *iface )
{
    struct hash_provider *impl = impl_from_IHashAlgorithmProvider( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        free( impl->alg_name );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI hash_prov_GetIids( IHashAlgorithmProvider *iface, ULONG *count, IID **iids ) { return E_NOTIMPL; }
static HRESULT WINAPI hash_prov_GetRuntimeClassName( IHashAlgorithmProvider *iface, HSTRING *name )
{
    return WindowsCreateString( RuntimeClass_Windows_Security_Cryptography_Core_HashAlgorithmProvider,
                                wcslen( RuntimeClass_Windows_Security_Cryptography_Core_HashAlgorithmProvider ), name );
}
static HRESULT WINAPI hash_prov_GetTrustLevel( IHashAlgorithmProvider *iface, TrustLevel *level ) { if (level) *level = BaseTrust; return S_OK; }

static HRESULT WINAPI hash_prov_get_AlgorithmName( IHashAlgorithmProvider *iface, HSTRING *value )
{
    struct hash_provider *impl = impl_from_IHashAlgorithmProvider( iface );
    if (!value) return E_POINTER;
    return WindowsCreateString( impl->alg_name, wcslen( impl->alg_name ), value );
}

static HRESULT WINAPI hash_prov_get_HashLength( IHashAlgorithmProvider *iface, UINT32 *value )
{
    struct hash_provider *impl = impl_from_IHashAlgorithmProvider( iface );
    if (!value) return E_POINTER;
    *value = impl->hash_len;
    return S_OK;
}

static HRESULT WINAPI hash_prov_HashData( IHashAlgorithmProvider *iface, IBuffer *data, IBuffer **value )
{
    struct hash_provider *impl = impl_from_IHashAlgorithmProvider( iface );
    BYTE *data_bytes = NULL;
    UINT32 data_len = 0;
    BCRYPT_ALG_HANDLE hAlg = NULL;
    BCRYPT_HASH_HANDLE hHash = NULL;
    BYTE hash_buf[64] = {0};
    NTSTATUS status;
    HRESULT hr;

    if (!value) return E_POINTER;
    if (FAILED(hr = get_buffer_data( data, &data_bytes, &data_len ))) return hr;

    status = BCryptOpenAlgorithmProvider( &hAlg, impl->bcrypt_alg, NULL, 0 );
    if (status) return E_FAIL;
    status = BCryptCreateHash( hAlg, &hHash, NULL, 0, NULL, 0, 0 );
    if (!status)
    {
        if (data_bytes && data_len) BCryptHashData( hHash, data_bytes, data_len, 0 );
        BCryptFinishHash( hHash, hash_buf, impl->hash_len, 0 );
        BCryptDestroyHash( hHash );
    }
    BCryptCloseAlgorithmProvider( hAlg, 0 );
    if (status) return E_FAIL;

    return create_crypt_buffer( impl->hash_len, impl->hash_len, hash_buf, value );
}

static HRESULT WINAPI hash_prov_CreateHash( IHashAlgorithmProvider *iface, IHashComputation **value )
{
    struct hash_provider *impl = impl_from_IHashAlgorithmProvider( iface );
    return create_crypto_hash( impl->bcrypt_alg, impl->hash_len, value );
}

static const struct IHashAlgorithmProviderVtbl hash_prov_vtbl =
{
    hash_prov_QueryInterface,
    hash_prov_AddRef,
    hash_prov_Release,
    hash_prov_GetIids,
    hash_prov_GetRuntimeClassName,
    hash_prov_GetTrustLevel,
    hash_prov_get_AlgorithmName,
    hash_prov_get_HashLength,
    hash_prov_HashData,
    hash_prov_CreateHash,
};

static HRESULT create_hash_provider( const WCHAR *alg_name, IHashAlgorithmProvider **out )
{
    struct hash_provider *impl;
    if (!out) return E_POINTER;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->IHashAlgorithmProvider_iface.lpVtbl = &hash_prov_vtbl;
    impl->ref = 1;
    if (alg_name) impl->alg_name = wcsdup( alg_name );

    if (!alg_name || !wcsicmp( alg_name, L"SHA256" ))
    {
        impl->bcrypt_alg = BCRYPT_SHA256_ALGORITHM;
        impl->hash_len = 32;
    }
    else if (!wcsicmp( alg_name, L"SHA1" ))
    {
        impl->bcrypt_alg = BCRYPT_SHA1_ALGORITHM;
        impl->hash_len = 20;
    }
    else if (!wcsicmp( alg_name, L"MD5" ))
    {
        impl->bcrypt_alg = BCRYPT_MD5_ALGORITHM;
        impl->hash_len = 16;
    }
    else if (!wcsicmp( alg_name, L"SHA384" ))
    {
        impl->bcrypt_alg = BCRYPT_SHA384_ALGORITHM;
        impl->hash_len = 48;
    }
    else if (!wcsicmp( alg_name, L"SHA512" ))
    {
        impl->bcrypt_alg = BCRYPT_SHA512_ALGORITHM;
        impl->hash_len = 64;
    }
    else
    {
        FIXME( "unsupported hash algorithm %s, defaulting to SHA256\n", debugstr_w(alg_name) );
        impl->bcrypt_alg = BCRYPT_SHA256_ALGORITHM;
        impl->hash_len = 32;
    }

    *out = &impl->IHashAlgorithmProvider_iface;
    return S_OK;
}

struct hashalgorithm_factory
{
    IActivationFactory IActivationFactory_iface;
    IHashAlgorithmProviderStatics IHashAlgorithmProviderStatics_iface;
    LONG ref;
};

static inline struct hashalgorithm_factory *impl_from_IActivationFactory_hash( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct hashalgorithm_factory, IActivationFactory_iface );
}

static HRESULT WINAPI hash_factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct hashalgorithm_factory *factory = impl_from_IActivationFactory_hash( iface );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        IActivationFactory_AddRef( iface );
        *out = &factory->IActivationFactory_iface;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IHashAlgorithmProviderStatics ))
    {
        IActivationFactory_AddRef( iface );
        *out = &factory->IHashAlgorithmProviderStatics_iface;
        return S_OK;
    }

    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI hash_factory_AddRef( IActivationFactory *iface ) { return 2; }
static ULONG WINAPI hash_factory_Release( IActivationFactory *iface ) { return 1; }
static HRESULT WINAPI hash_factory_GetIids( IActivationFactory *iface, ULONG *count, IID **iids ) { return E_NOTIMPL; }
static HRESULT WINAPI hash_factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *name )
{
    return WindowsCreateString( RuntimeClass_Windows_Security_Cryptography_Core_HashAlgorithmProvider,
                                wcslen( RuntimeClass_Windows_Security_Cryptography_Core_HashAlgorithmProvider ), name );
}
static HRESULT WINAPI hash_factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *level ) { if (level) *level = BaseTrust; return S_OK; }
static HRESULT WINAPI hash_factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance ) { return E_NOTIMPL; }

static const struct IActivationFactoryVtbl hash_factory_vtbl =
{
    hash_factory_QueryInterface,
    hash_factory_AddRef,
    hash_factory_Release,
    hash_factory_GetIids,
    hash_factory_GetRuntimeClassName,
    hash_factory_GetTrustLevel,
    hash_factory_ActivateInstance,
};

DEFINE_IINSPECTABLE( hash_statics, IHashAlgorithmProviderStatics, struct hashalgorithm_factory, IActivationFactory_iface )

static HRESULT WINAPI hash_statics_OpenAlgorithm( IHashAlgorithmProviderStatics *iface,
        HSTRING algorithm, IHashAlgorithmProvider **provider )
{
    const WCHAR *str = WindowsGetStringRawBuffer( algorithm, NULL );
    TRACE( "iface %p, algorithm %s, provider %p\n", iface, debugstr_w(str), provider );
    return create_hash_provider( str, provider );
}

static const struct IHashAlgorithmProviderStaticsVtbl hash_statics_vtbl =
{
    hash_statics_QueryInterface,
    hash_statics_AddRef,
    hash_statics_Release,
    hash_statics_GetIids,
    hash_statics_GetRuntimeClassName,
    hash_statics_GetTrustLevel,
    hash_statics_OpenAlgorithm,
};

static struct hashalgorithm_factory hash_factory_obj =
{
    {&hash_factory_vtbl},
    {&hash_statics_vtbl},
    1
};

IActivationFactory *hashalgorithm_activation_factory = &hash_factory_obj.IActivationFactory_iface;

/* -------------------------------------------------------------------------
 * KeyDerivationAlgorithmProvider
 * ------------------------------------------------------------------------- */
struct keyderivation_provider
{
    IKeyDerivationAlgorithmProvider IKeyDerivationAlgorithmProvider_iface;
    LONG ref;
    WCHAR *alg_name;
};

static inline struct keyderivation_provider *impl_from_IKeyDerivationAlgorithmProvider( IKeyDerivationAlgorithmProvider *iface )
{
    return CONTAINING_RECORD( iface, struct keyderivation_provider, IKeyDerivationAlgorithmProvider_iface );
}

static HRESULT WINAPI kd_prov_QueryInterface( IKeyDerivationAlgorithmProvider *iface, REFIID iid, void **out )
{
    struct keyderivation_provider *impl = impl_from_IKeyDerivationAlgorithmProvider( iface );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IKeyDerivationAlgorithmProvider ))
    {
        InterlockedIncrement( &impl->ref );
        *out = &impl->IKeyDerivationAlgorithmProvider_iface;
        return S_OK;
    }

    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI kd_prov_AddRef( IKeyDerivationAlgorithmProvider *iface )
{
    struct keyderivation_provider *impl = impl_from_IKeyDerivationAlgorithmProvider( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI kd_prov_Release( IKeyDerivationAlgorithmProvider *iface )
{
    struct keyderivation_provider *impl = impl_from_IKeyDerivationAlgorithmProvider( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        free( impl->alg_name );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI kd_prov_GetIids( IKeyDerivationAlgorithmProvider *iface, ULONG *count, IID **iids ) { return E_NOTIMPL; }
static HRESULT WINAPI kd_prov_GetRuntimeClassName( IKeyDerivationAlgorithmProvider *iface, HSTRING *name )
{
    return WindowsCreateString( RuntimeClass_Windows_Security_Cryptography_Core_KeyDerivationAlgorithmProvider,
                                wcslen( RuntimeClass_Windows_Security_Cryptography_Core_KeyDerivationAlgorithmProvider ), name );
}
static HRESULT WINAPI kd_prov_GetTrustLevel( IKeyDerivationAlgorithmProvider *iface, TrustLevel *level ) { if (level) *level = BaseTrust; return S_OK; }

static HRESULT WINAPI kd_prov_get_AlgorithmName( IKeyDerivationAlgorithmProvider *iface, HSTRING *value )
{
    struct keyderivation_provider *impl = impl_from_IKeyDerivationAlgorithmProvider( iface );
    if (!value) return E_POINTER;
    return WindowsCreateString( impl->alg_name, wcslen( impl->alg_name ), value );
}

static HRESULT WINAPI kd_prov_CreateKey( IKeyDerivationAlgorithmProvider *iface,
        IBuffer *keyMaterial, ICryptographicKey **key )
{
    struct keyderivation_provider *impl = impl_from_IKeyDerivationAlgorithmProvider( iface );
    BYTE *data = NULL;
    UINT32 len = 0;
    HRESULT hr;

    TRACE( "iface %p, keyMaterial %p, key %p\n", iface, keyMaterial, key );
    if (FAILED(hr = get_buffer_data( keyMaterial, &data, &len ))) return hr;
    return create_cryptographic_key( data, len, impl->alg_name, key );
}

static const struct IKeyDerivationAlgorithmProviderVtbl kd_prov_vtbl =
{
    kd_prov_QueryInterface,
    kd_prov_AddRef,
    kd_prov_Release,
    kd_prov_GetIids,
    kd_prov_GetRuntimeClassName,
    kd_prov_GetTrustLevel,
    kd_prov_get_AlgorithmName,
    kd_prov_CreateKey,
};

static HRESULT create_keyderivation_provider( const WCHAR *alg_name, IKeyDerivationAlgorithmProvider **out )
{
    struct keyderivation_provider *impl;
    if (!out) return E_POINTER;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->IKeyDerivationAlgorithmProvider_iface.lpVtbl = &kd_prov_vtbl;
    impl->ref = 1;
    if (alg_name) impl->alg_name = wcsdup( alg_name );
    *out = &impl->IKeyDerivationAlgorithmProvider_iface;
    return S_OK;
}

struct keyderivation_factory
{
    IActivationFactory IActivationFactory_iface;
    IKeyDerivationAlgorithmProviderStatics IKeyDerivationAlgorithmProviderStatics_iface;
    LONG ref;
};

static inline struct keyderivation_factory *impl_from_IActivationFactory_kd( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct keyderivation_factory, IActivationFactory_iface );
}

static HRESULT WINAPI kd_factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct keyderivation_factory *factory = impl_from_IActivationFactory_kd( iface );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        IActivationFactory_AddRef( iface );
        *out = &factory->IActivationFactory_iface;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IKeyDerivationAlgorithmProviderStatics ))
    {
        IActivationFactory_AddRef( iface );
        *out = &factory->IKeyDerivationAlgorithmProviderStatics_iface;
        return S_OK;
    }

    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI kd_factory_AddRef( IActivationFactory *iface ) { return 2; }
static ULONG WINAPI kd_factory_Release( IActivationFactory *iface ) { return 1; }
static HRESULT WINAPI kd_factory_GetIids( IActivationFactory *iface, ULONG *count, IID **iids ) { return E_NOTIMPL; }
static HRESULT WINAPI kd_factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *name )
{
    return WindowsCreateString( RuntimeClass_Windows_Security_Cryptography_Core_KeyDerivationAlgorithmProvider,
                                wcslen( RuntimeClass_Windows_Security_Cryptography_Core_KeyDerivationAlgorithmProvider ), name );
}
static HRESULT WINAPI kd_factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *level ) { if (level) *level = BaseTrust; return S_OK; }
static HRESULT WINAPI kd_factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance ) { return E_NOTIMPL; }

static const struct IActivationFactoryVtbl kd_factory_vtbl =
{
    kd_factory_QueryInterface,
    kd_factory_AddRef,
    kd_factory_Release,
    kd_factory_GetIids,
    kd_factory_GetRuntimeClassName,
    kd_factory_GetTrustLevel,
    kd_factory_ActivateInstance,
};

DEFINE_IINSPECTABLE( kd_statics, IKeyDerivationAlgorithmProviderStatics, struct keyderivation_factory, IActivationFactory_iface )

static HRESULT WINAPI kd_statics_OpenAlgorithm( IKeyDerivationAlgorithmProviderStatics *iface,
        HSTRING algorithm, IKeyDerivationAlgorithmProvider **provider )
{
    const WCHAR *str = WindowsGetStringRawBuffer( algorithm, NULL );
    TRACE( "iface %p, algorithm %s, provider %p\n", iface, debugstr_w(str), provider );
    return create_keyderivation_provider( str, provider );
}

static const struct IKeyDerivationAlgorithmProviderStaticsVtbl kd_statics_vtbl =
{
    kd_statics_QueryInterface,
    kd_statics_AddRef,
    kd_statics_Release,
    kd_statics_GetIids,
    kd_statics_GetRuntimeClassName,
    kd_statics_GetTrustLevel,
    kd_statics_OpenAlgorithm,
};

static struct keyderivation_factory kd_factory_obj =
{
    {&kd_factory_vtbl},
    {&kd_statics_vtbl},
    1
};

IActivationFactory *keyderivation_activation_factory = &kd_factory_obj.IActivationFactory_iface;

/* -------------------------------------------------------------------------
 * CryptographicEngine
 * ------------------------------------------------------------------------- */
struct cryptoengine_factory
{
    IActivationFactory IActivationFactory_iface;
    ICryptographicEngineStatics ICryptographicEngineStatics_iface;
    ICryptographicEngineStatics2 ICryptographicEngineStatics2_iface;
    LONG ref;
};

static inline struct cryptoengine_factory *impl_from_IActivationFactory_ce( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct cryptoengine_factory, IActivationFactory_iface );
}

static HRESULT WINAPI ce_factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct cryptoengine_factory *factory = impl_from_IActivationFactory_ce( iface );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        IActivationFactory_AddRef( iface );
        *out = &factory->IActivationFactory_iface;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_ICryptographicEngineStatics ))
    {
        IActivationFactory_AddRef( iface );
        *out = &factory->ICryptographicEngineStatics_iface;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_ICryptographicEngineStatics2 ))
    {
        IActivationFactory_AddRef( iface );
        *out = &factory->ICryptographicEngineStatics2_iface;
        return S_OK;
    }

    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI ce_factory_AddRef( IActivationFactory *iface ) { return 2; }
static ULONG WINAPI ce_factory_Release( IActivationFactory *iface ) { return 1; }
static HRESULT WINAPI ce_factory_GetIids( IActivationFactory *iface, ULONG *count, IID **iids ) { return E_NOTIMPL; }
static HRESULT WINAPI ce_factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *name )
{
    return WindowsCreateString( RuntimeClass_Windows_Security_Cryptography_Core_CryptographicEngine,
                                wcslen( RuntimeClass_Windows_Security_Cryptography_Core_CryptographicEngine ), name );
}
static HRESULT WINAPI ce_factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *level ) { if (level) *level = BaseTrust; return S_OK; }
static HRESULT WINAPI ce_factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance ) { return E_NOTIMPL; }

static const struct IActivationFactoryVtbl ce_factory_vtbl =
{
    ce_factory_QueryInterface,
    ce_factory_AddRef,
    ce_factory_Release,
    ce_factory_GetIids,
    ce_factory_GetRuntimeClassName,
    ce_factory_GetTrustLevel,
    ce_factory_ActivateInstance,
};

DEFINE_IINSPECTABLE( ce_statics, ICryptographicEngineStatics, struct cryptoengine_factory, IActivationFactory_iface )

static HRESULT WINAPI ce_statics_Encrypt( ICryptographicEngineStatics *iface,
        ICryptographicKey *key, IBuffer *data, IBuffer *iv, IBuffer **value )
{
    struct crypto_key *impl_key;
    BYTE *data_bytes = NULL, *iv_bytes = NULL;
    UINT32 data_len = 0, iv_len = 0;
    BCRYPT_ALG_HANDLE hAlg = NULL;
    BCRYPT_KEY_HANDLE hKey = NULL;
    DWORD flags = 0;
    DWORD ret_len = 0;
    BYTE *iv_buf = NULL;
    BYTE *out_buf = NULL;
    const WCHAR *bcrypt_alg = BCRYPT_AES_ALGORITHM;
    const WCHAR *chain_mode = BCRYPT_CHAIN_MODE_CBC;
    NTSTATUS status;
    HRESULT hr;

    TRACE( "iface %p, key %p, data %p, iv %p, value %p\n", iface, key, data, iv, value );
    if (!key || !value) return E_POINTER;
    *value = NULL;
    impl_key = impl_from_ICryptographicKey( key );

    if (FAILED(hr = get_buffer_data( data, &data_bytes, &data_len ))) return hr;
    if (FAILED(hr = get_buffer_data( iv, &iv_bytes, &iv_len ))) return hr;

    if (impl_key->alg_name)
    {
        if (!wcsicmp( impl_key->alg_name, L"AES_CBC_PKCS7" ))
        {
            flags = BCRYPT_BLOCK_PADDING;
            chain_mode = BCRYPT_CHAIN_MODE_CBC;
        }
        else if (!wcsicmp( impl_key->alg_name, L"AES_CBC" ))
        {
            flags = 0;
            chain_mode = BCRYPT_CHAIN_MODE_CBC;
        }
        else if (!wcsicmp( impl_key->alg_name, L"AES_ECB_PKCS7" ))
        {
            flags = BCRYPT_BLOCK_PADDING;
            chain_mode = BCRYPT_CHAIN_MODE_ECB;
        }
        else if (!wcsicmp( impl_key->alg_name, L"AES_ECB" ))
        {
            flags = 0;
            chain_mode = BCRYPT_CHAIN_MODE_ECB;
        }
        else if (!wcsicmp( impl_key->alg_name, L"RC4" ))
        {
            bcrypt_alg = BCRYPT_RC4_ALGORITHM;
            chain_mode = NULL;
        }
    }
    else
    {
        flags = BCRYPT_BLOCK_PADDING;
    }

    status = BCryptOpenAlgorithmProvider( &hAlg, bcrypt_alg, NULL, 0 );
    if (status) return E_FAIL;
    if (chain_mode)
        BCryptSetProperty( hAlg, BCRYPT_CHAINING_MODE, (UCHAR *)chain_mode, (wcslen(chain_mode) + 1) * sizeof(WCHAR), 0 );
    status = BCryptGenerateSymmetricKey( hAlg, &hKey, NULL, 0, impl_key->key_data, impl_key->key_len, 0 );
    if (status)
    {
        BCryptCloseAlgorithmProvider( hAlg, 0 );
        return E_FAIL;
    }

    if (iv_bytes && iv_len)
    {
        iv_buf = malloc( iv_len );
        memcpy( iv_buf, iv_bytes, iv_len );
    }

    status = BCryptEncrypt( hKey, data_bytes, data_len, NULL, iv_buf, iv_len, NULL, 0, &ret_len, flags );
    if (!status && ret_len)
    {
        out_buf = malloc( ret_len );
        if (iv_buf && iv_bytes) memcpy( iv_buf, iv_bytes, iv_len );
        status = BCryptEncrypt( hKey, data_bytes, data_len, NULL, iv_buf, iv_len, out_buf, ret_len, &ret_len, flags );
    }

    BCryptDestroyKey( hKey );
    BCryptCloseAlgorithmProvider( hAlg, 0 );
    free( iv_buf );

    if (status)
    {
        free( out_buf );
        return E_FAIL;
    }

    hr = create_crypt_buffer( ret_len, ret_len, out_buf, value );
    free( out_buf );
    return hr;
}

static HRESULT WINAPI ce_statics_Decrypt( ICryptographicEngineStatics *iface,
        ICryptographicKey *key, IBuffer *data, IBuffer *iv, IBuffer **value )
{
    struct crypto_key *impl_key;
    BYTE *data_bytes = NULL, *iv_bytes = NULL;
    UINT32 data_len = 0, iv_len = 0;
    BCRYPT_ALG_HANDLE hAlg = NULL;
    BCRYPT_KEY_HANDLE hKey = NULL;
    DWORD flags = 0;
    DWORD ret_len = 0;
    BYTE *iv_buf = NULL;
    BYTE *out_buf = NULL;
    const WCHAR *bcrypt_alg = BCRYPT_AES_ALGORITHM;
    const WCHAR *chain_mode = BCRYPT_CHAIN_MODE_CBC;
    NTSTATUS status;
    HRESULT hr;

    TRACE( "iface %p, key %p, data %p, iv %p, value %p\n", iface, key, data, iv, value );
    if (!key || !value) return E_POINTER;
    *value = NULL;
    impl_key = impl_from_ICryptographicKey( key );

    if (FAILED(hr = get_buffer_data( data, &data_bytes, &data_len ))) return hr;
    if (FAILED(hr = get_buffer_data( iv, &iv_bytes, &iv_len ))) return hr;

    if (impl_key->alg_name)
    {
        if (!wcsicmp( impl_key->alg_name, L"AES_CBC_PKCS7" ))
        {
            flags = BCRYPT_BLOCK_PADDING;
            chain_mode = BCRYPT_CHAIN_MODE_CBC;
        }
        else if (!wcsicmp( impl_key->alg_name, L"AES_CBC" ))
        {
            flags = 0;
            chain_mode = BCRYPT_CHAIN_MODE_CBC;
        }
        else if (!wcsicmp( impl_key->alg_name, L"AES_ECB_PKCS7" ))
        {
            flags = BCRYPT_BLOCK_PADDING;
            chain_mode = BCRYPT_CHAIN_MODE_ECB;
        }
        else if (!wcsicmp( impl_key->alg_name, L"AES_ECB" ))
        {
            flags = 0;
            chain_mode = BCRYPT_CHAIN_MODE_ECB;
        }
        else if (!wcsicmp( impl_key->alg_name, L"RC4" ))
        {
            bcrypt_alg = BCRYPT_RC4_ALGORITHM;
            chain_mode = NULL;
        }
    }
    else
    {
        flags = BCRYPT_BLOCK_PADDING;
    }

    status = BCryptOpenAlgorithmProvider( &hAlg, bcrypt_alg, NULL, 0 );
    if (status) return E_FAIL;
    if (chain_mode)
        BCryptSetProperty( hAlg, BCRYPT_CHAINING_MODE, (UCHAR *)chain_mode, (wcslen(chain_mode) + 1) * sizeof(WCHAR), 0 );
    status = BCryptGenerateSymmetricKey( hAlg, &hKey, NULL, 0, impl_key->key_data, impl_key->key_len, 0 );
    if (status)
    {
        BCryptCloseAlgorithmProvider( hAlg, 0 );
        return E_FAIL;
    }

    if (iv_bytes && iv_len)
    {
        iv_buf = malloc( iv_len );
        memcpy( iv_buf, iv_bytes, iv_len );
    }

    status = BCryptDecrypt( hKey, data_bytes, data_len, NULL, iv_buf, iv_len, NULL, 0, &ret_len, flags );
    if (!status && ret_len)
    {
        out_buf = malloc( ret_len );
        if (iv_buf && iv_bytes) memcpy( iv_buf, iv_bytes, iv_len );
        status = BCryptDecrypt( hKey, data_bytes, data_len, NULL, iv_buf, iv_len, out_buf, ret_len, &ret_len, flags );
    }

    BCryptDestroyKey( hKey );
    BCryptCloseAlgorithmProvider( hAlg, 0 );
    free( iv_buf );

    if (status)
    {
        free( out_buf );
        return E_FAIL;
    }

    hr = create_crypt_buffer( ret_len, ret_len, out_buf, value );
    free( out_buf );
    return hr;
}

static HRESULT WINAPI ce_statics_EncryptAndAuthenticate( ICryptographicEngineStatics *iface,
        ICryptographicKey *key, IBuffer *data, IBuffer *nonce, IBuffer *authenticatedData,
        IEncryptedAndAuthenticatedData **value )
{
    struct crypto_key *impl_key;
    BYTE *data_bytes = NULL, *nonce_bytes = NULL, *aad_bytes = NULL;
    UINT32 data_len = 0, nonce_len = 0, aad_len = 0;
    BCRYPT_ALG_HANDLE hAlg = NULL;
    BCRYPT_KEY_HANDLE hKey = NULL;
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO auth_info;
    BYTE tag[16] = {0};
    BYTE *out_buf = NULL;
    DWORD ret_len = 0;
    NTSTATUS status;
    IBuffer *enc_buf = NULL, *tag_buf = NULL;
    HRESULT hr;

    TRACE( "iface %p, key %p, data %p, nonce %p, aad %p, value %p\n",
           iface, key, data, nonce, authenticatedData, value );
    if (!key || !value) return E_POINTER;
    *value = NULL;
    impl_key = impl_from_ICryptographicKey( key );

    if (FAILED(hr = get_buffer_data( data, &data_bytes, &data_len ))) return hr;
    if (FAILED(hr = get_buffer_data( nonce, &nonce_bytes, &nonce_len ))) return hr;
    if (FAILED(hr = get_buffer_data( authenticatedData, &aad_bytes, &aad_len ))) return hr;

    status = BCryptOpenAlgorithmProvider( &hAlg, BCRYPT_AES_ALGORITHM, NULL, 0 );
    if (status) return E_FAIL;
    BCryptSetProperty( hAlg, BCRYPT_CHAINING_MODE, (UCHAR *)BCRYPT_CHAIN_MODE_GCM, sizeof(BCRYPT_CHAIN_MODE_GCM), 0 );
    status = BCryptGenerateSymmetricKey( hAlg, &hKey, NULL, 0, impl_key->key_data, impl_key->key_len, 0 );
    if (status)
    {
        BCryptCloseAlgorithmProvider( hAlg, 0 );
        return E_FAIL;
    }

    memset( &auth_info, 0, sizeof(auth_info) );
    auth_info.cbSize = sizeof(auth_info);
    auth_info.dwInfoVersion = BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO_VERSION;
    auth_info.pbNonce = nonce_bytes;
    auth_info.cbNonce = nonce_len;
    auth_info.pbAuthData = aad_bytes;
    auth_info.cbAuthData = aad_len;
    auth_info.pbTag = tag;
    auth_info.cbTag = sizeof(tag);

    if (data_len)
    {
        out_buf = malloc( data_len );
        status = BCryptEncrypt( hKey, data_bytes, data_len, &auth_info, NULL, 0, out_buf, data_len, &ret_len, 0 );
    }
    else
    {
        status = BCryptEncrypt( hKey, NULL, 0, &auth_info, NULL, 0, NULL, 0, &ret_len, 0 );
    }

    BCryptDestroyKey( hKey );
    BCryptCloseAlgorithmProvider( hAlg, 0 );

    if (status)
    {
        free( out_buf );
        return E_FAIL;
    }

    create_crypt_buffer( data_len, data_len, out_buf, &enc_buf );
    create_crypt_buffer( sizeof(tag), sizeof(tag), tag, &tag_buf );
    free( out_buf );

    hr = create_encrypted_and_authenticated_data( enc_buf, tag_buf, value );
    if (enc_buf) IBuffer_Release( enc_buf );
    if (tag_buf) IBuffer_Release( tag_buf );
    return hr;
}

static HRESULT WINAPI ce_statics_DecryptAndAuthenticate( ICryptographicEngineStatics *iface,
        ICryptographicKey *key, IBuffer *data, IBuffer *nonce, IBuffer *authenticationTag,
        IBuffer *authenticatedData, IBuffer **value )
{
    struct crypto_key *impl_key;
    BYTE *data_bytes = NULL, *nonce_bytes = NULL, *tag_bytes = NULL, *aad_bytes = NULL;
    UINT32 data_len = 0, nonce_len = 0, tag_len = 0, aad_len = 0;
    BCRYPT_ALG_HANDLE hAlg = NULL;
    BCRYPT_KEY_HANDLE hKey = NULL;
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO auth_info;
    BYTE *out_buf = NULL;
    DWORD ret_len = 0;
    NTSTATUS status;
    HRESULT hr;

    TRACE( "iface %p, key %p, data %p, nonce %p, tag %p, aad %p, value %p\n",
           iface, key, data, nonce, authenticationTag, authenticatedData, value );
    if (!key || !value) return E_POINTER;
    *value = NULL;
    impl_key = impl_from_ICryptographicKey( key );

    if (FAILED(hr = get_buffer_data( data, &data_bytes, &data_len ))) return hr;
    if (FAILED(hr = get_buffer_data( nonce, &nonce_bytes, &nonce_len ))) return hr;
    if (FAILED(hr = get_buffer_data( authenticationTag, &tag_bytes, &tag_len ))) return hr;
    if (FAILED(hr = get_buffer_data( authenticatedData, &aad_bytes, &aad_len ))) return hr;

    status = BCryptOpenAlgorithmProvider( &hAlg, BCRYPT_AES_ALGORITHM, NULL, 0 );
    if (status) return E_FAIL;
    BCryptSetProperty( hAlg, BCRYPT_CHAINING_MODE, (UCHAR *)BCRYPT_CHAIN_MODE_GCM, sizeof(BCRYPT_CHAIN_MODE_GCM), 0 );
    status = BCryptGenerateSymmetricKey( hAlg, &hKey, NULL, 0, impl_key->key_data, impl_key->key_len, 0 );
    if (status)
    {
        BCryptCloseAlgorithmProvider( hAlg, 0 );
        return E_FAIL;
    }

    memset( &auth_info, 0, sizeof(auth_info) );
    auth_info.cbSize = sizeof(auth_info);
    auth_info.dwInfoVersion = BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO_VERSION;
    auth_info.pbNonce = nonce_bytes;
    auth_info.cbNonce = nonce_len;
    auth_info.pbAuthData = aad_bytes;
    auth_info.cbAuthData = aad_len;
    auth_info.pbTag = tag_bytes;
    auth_info.cbTag = tag_len;

    if (data_len)
    {
        out_buf = malloc( data_len );
        status = BCryptDecrypt( hKey, data_bytes, data_len, &auth_info, NULL, 0, out_buf, data_len, &ret_len, 0 );
    }
    else
    {
        status = BCryptDecrypt( hKey, NULL, 0, &auth_info, NULL, 0, NULL, 0, &ret_len, 0 );
    }

    BCryptDestroyKey( hKey );
    BCryptCloseAlgorithmProvider( hAlg, 0 );

    if (status)
    {
        free( out_buf );
        return E_FAIL;
    }

    hr = create_crypt_buffer( data_len, data_len, out_buf, value );
    free( out_buf );
    return hr;
}

static HRESULT WINAPI ce_statics_Sign( ICryptographicEngineStatics *iface,
        ICryptographicKey *key, IBuffer *data, IBuffer **value )
{
    struct crypto_key *impl_key;
    BYTE *data_bytes = NULL;
    UINT32 data_len = 0;
    BCRYPT_ALG_HANDLE hAlg = NULL;
    BCRYPT_HASH_HANDLE hHash = NULL;
    BYTE hash_buf[64] = {0};
    DWORD hash_len = 32;
    NTSTATUS status;
    HRESULT hr;

    TRACE( "iface %p, key %p, data %p, value %p\n", iface, key, data, value );
    if (!key || !value) return E_POINTER;
    *value = NULL;
    impl_key = impl_from_ICryptographicKey( key );

    if (FAILED(hr = get_buffer_data( data, &data_bytes, &data_len ))) return hr;

    status = BCryptOpenAlgorithmProvider( &hAlg, BCRYPT_SHA256_ALGORITHM, NULL, BCRYPT_ALG_HANDLE_HMAC_FLAG );
    if (status) return E_FAIL;
    status = BCryptCreateHash( hAlg, &hHash, NULL, 0, impl_key->key_data, impl_key->key_len, 0 );
    if (!status)
    {
        if (data_bytes && data_len) BCryptHashData( hHash, data_bytes, data_len, 0 );
        BCryptFinishHash( hHash, hash_buf, hash_len, 0 );
        BCryptDestroyHash( hHash );
    }
    BCryptCloseAlgorithmProvider( hAlg, 0 );
    if (status) return E_FAIL;

    return create_crypt_buffer( hash_len, hash_len, hash_buf, value );
}

static HRESULT WINAPI ce_statics_VerifySignature( ICryptographicEngineStatics *iface,
        ICryptographicKey *key, IBuffer *data, IBuffer *signature, boolean *isAuthenticated )
{
    IBuffer *computed_sig = NULL;
    HRESULT hr;

    TRACE( "iface %p, key %p, data %p, signature %p, isAuthenticated %p\n",
           iface, key, data, signature, isAuthenticated );
    if (!isAuthenticated) return E_POINTER;
    *isAuthenticated = FALSE;

    if (FAILED(hr = ce_statics_Sign( iface, key, data, &computed_sig ))) return hr;
    hr = cryptobuffer_statics_Compare( NULL, computed_sig, signature, isAuthenticated );
    IBuffer_Release( computed_sig );
    return hr;
}

static HRESULT WINAPI ce_statics_DeriveKeyMaterial( ICryptographicEngineStatics *iface,
        ICryptographicKey *key, IKeyDerivationParameters *parameters, UINT32 desiredKeySize,
        IBuffer **keyMaterial )
{
    struct crypto_key *impl_key;
    IBuffer *salt_buf = NULL;
    BYTE *salt_bytes = NULL;
    UINT32 salt_len = 0, iterations = 10000;
    BYTE *derived = NULL;
    NTSTATUS status;
    BCRYPT_ALG_HANDLE hAlg = NULL;
    const WCHAR *bcrypt_hash = BCRYPT_SHA256_ALGORITHM;
    HRESULT hr;

    TRACE( "iface %p, key %p, parameters %p, desiredKeySize %u, keyMaterial %p\n",
           iface, key, parameters, desiredKeySize, keyMaterial );
    if (!key || !keyMaterial) return E_POINTER;
    *keyMaterial = NULL;
    impl_key = impl_from_ICryptographicKey( key );

    if (parameters)
    {
        IKeyDerivationParameters_get_KdfGenericBinary( parameters, &salt_buf );
        IKeyDerivationParameters_get_IterationCount( parameters, &iterations );
    }
    if (salt_buf)
    {
        get_buffer_data( salt_buf, &salt_bytes, &salt_len );
    }

    if (impl_key->alg_name)
    {
        if (wcsstr( impl_key->alg_name, L"SHA1" ) || wcsstr( impl_key->alg_name, L"Sha1" ))
            bcrypt_hash = BCRYPT_SHA1_ALGORITHM;
        else if (wcsstr( impl_key->alg_name, L"MD5" ) || wcsstr( impl_key->alg_name, L"Md5" ))
            bcrypt_hash = BCRYPT_MD5_ALGORITHM;
        else if (wcsstr( impl_key->alg_name, L"SHA384" ) || wcsstr( impl_key->alg_name, L"Sha384" ))
            bcrypt_hash = BCRYPT_SHA384_ALGORITHM;
        else if (wcsstr( impl_key->alg_name, L"SHA512" ) || wcsstr( impl_key->alg_name, L"Sha512" ))
            bcrypt_hash = BCRYPT_SHA512_ALGORITHM;
    }

    derived = malloc( desiredKeySize ? desiredKeySize : 1 );
    if (!derived)
    {
        if (salt_buf) IBuffer_Release( salt_buf );
        return E_OUTOFMEMORY;
    }

    status = BCryptOpenAlgorithmProvider( &hAlg, bcrypt_hash, NULL, BCRYPT_ALG_HANDLE_HMAC_FLAG );
    if (!status)
    {
        status = BCryptDeriveKeyPBKDF2( hAlg, impl_key->key_data, impl_key->key_len,
                                        salt_bytes, salt_len, iterations, derived, desiredKeySize, 0 );
        BCryptCloseAlgorithmProvider( hAlg, 0 );
    }
    if (salt_buf) IBuffer_Release( salt_buf );

    if (status)
    {
        free( derived );
        return E_FAIL;
    }

    hr = create_crypt_buffer( desiredKeySize, desiredKeySize, derived, keyMaterial );
    free( derived );
    return hr;
}

static const struct ICryptographicEngineStaticsVtbl ce_statics_vtbl =
{
    ce_statics_QueryInterface,
    ce_statics_AddRef,
    ce_statics_Release,
    ce_statics_GetIids,
    ce_statics_GetRuntimeClassName,
    ce_statics_GetTrustLevel,
    ce_statics_Encrypt,
    ce_statics_Decrypt,
    ce_statics_EncryptAndAuthenticate,
    ce_statics_DecryptAndAuthenticate,
    ce_statics_Sign,
    ce_statics_VerifySignature,
    ce_statics_DeriveKeyMaterial,
};

DEFINE_IINSPECTABLE( ce_statics2, ICryptographicEngineStatics2, struct cryptoengine_factory, IActivationFactory_iface )

static struct cryptoengine_factory ce_factory_obj;

static HRESULT WINAPI ce_statics2_SignHashedData( ICryptographicEngineStatics2 *iface,
        ICryptographicKey *key, IBuffer *data, IBuffer **value )
{
    return ce_statics_Sign( &ce_factory_obj.ICryptographicEngineStatics_iface, key, data, value );
}

static HRESULT WINAPI ce_statics2_VerifySignatureWithHashInput( ICryptographicEngineStatics2 *iface,
        ICryptographicKey *key, IBuffer *data, IBuffer *signature, boolean *isAuthenticated )
{
    return ce_statics_VerifySignature( &ce_factory_obj.ICryptographicEngineStatics_iface,
                                       key, data, signature, isAuthenticated );
}

static HRESULT WINAPI ce_statics2_DecryptAsync( ICryptographicEngineStatics2 *iface,
        ICryptographicKey *key, IBuffer *data, IBuffer *iv, IAsyncOperation_IBuffer **value )
{
    IBuffer *buf = NULL;
    HRESULT hr;

    TRACE( "iface %p, key %p, data %p, iv %p, value %p\n", iface, key, data, iv, value );
    if (!value) return E_POINTER;
    if (!key || !data) return E_INVALIDARG;

    hr = ce_statics_Decrypt( &ce_factory_obj.ICryptographicEngineStatics_iface, key, data, iv, &buf );
    if (FAILED(hr)) return hr;
    hr = create_async_buffer( (IInspectable *)buf, (void **)value );
    if (buf) IBuffer_Release( buf );
    return hr;
}

static HRESULT WINAPI ce_statics2_SignAsync( ICryptographicEngineStatics2 *iface,
        ICryptographicKey *key, IBuffer *data, IAsyncOperation_IBuffer **value )
{
    IBuffer *buf = NULL;
    HRESULT hr;

    TRACE( "iface %p, key %p, data %p, value %p\n", iface, key, data, value );
    if (!value) return E_POINTER;
    if (!key || !data) return E_INVALIDARG;

    hr = ce_statics_Sign( &ce_factory_obj.ICryptographicEngineStatics_iface, key, data, &buf );
    if (FAILED(hr)) return hr;
    hr = create_async_buffer( (IInspectable *)buf, (void **)value );
    if (buf) IBuffer_Release( buf );
    return hr;
}

static HRESULT WINAPI ce_statics2_SignHashedDataAsync( ICryptographicEngineStatics2 *iface,
        ICryptographicKey *key, IBuffer *data, IAsyncOperation_IBuffer **value )
{
    IBuffer *buf = NULL;
    HRESULT hr;

    TRACE( "iface %p, key %p, data %p, value %p\n", iface, key, data, value );
    if (!value) return E_POINTER;
    if (!key || !data) return E_INVALIDARG;

    hr = ce_statics2_SignHashedData( iface, key, data, &buf );
    if (FAILED(hr)) return hr;
    hr = create_async_buffer( (IInspectable *)buf, (void **)value );
    if (buf) IBuffer_Release( buf );
    return hr;
}

static const struct ICryptographicEngineStatics2Vtbl ce_statics2_vtbl =
{
    ce_statics2_QueryInterface,
    ce_statics2_AddRef,
    ce_statics2_Release,
    ce_statics2_GetIids,
    ce_statics2_GetRuntimeClassName,
    ce_statics2_GetTrustLevel,
    ce_statics2_SignHashedData,
    ce_statics2_VerifySignatureWithHashInput,
    ce_statics2_DecryptAsync,
    ce_statics2_SignAsync,
    ce_statics2_SignHashedDataAsync,
};

static struct cryptoengine_factory ce_factory_obj =
{
    {&ce_factory_vtbl},
    {&ce_statics_vtbl},
    {&ce_statics2_vtbl},
    1
};

IActivationFactory *cryptoengine_activation_factory = &ce_factory_obj.IActivationFactory_iface;

/* -------------------------------------------------------------------------
 * SymmetricAlgorithmNames
 * ------------------------------------------------------------------------- */
struct symmetricalgnames_factory
{
    IActivationFactory IActivationFactory_iface;
    ISymmetricAlgorithmNamesStatics ISymmetricAlgorithmNamesStatics_iface;
    LONG ref;
};

static inline struct symmetricalgnames_factory *impl_from_IActivationFactory_san( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct symmetricalgnames_factory, IActivationFactory_iface );
}

static HRESULT WINAPI san_factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct symmetricalgnames_factory *factory = impl_from_IActivationFactory_san( iface );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        IActivationFactory_AddRef( iface );
        *out = &factory->IActivationFactory_iface;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_ISymmetricAlgorithmNamesStatics ))
    {
        IActivationFactory_AddRef( iface );
        *out = &factory->ISymmetricAlgorithmNamesStatics_iface;
        return S_OK;
    }

    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI san_factory_AddRef( IActivationFactory *iface ) { return 2; }
static ULONG WINAPI san_factory_Release( IActivationFactory *iface ) { return 1; }
static HRESULT WINAPI san_factory_GetIids( IActivationFactory *iface, ULONG *count, IID **iids ) { return E_NOTIMPL; }
static HRESULT WINAPI san_factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.Security.Cryptography.Core.SymmetricAlgorithmNames", 55, name );
}
static HRESULT WINAPI san_factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *level ) { if (level) *level = BaseTrust; return S_OK; }
static HRESULT WINAPI san_factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance ) { return E_NOTIMPL; }

static const struct IActivationFactoryVtbl san_factory_vtbl =
{
    san_factory_QueryInterface,
    san_factory_AddRef,
    san_factory_Release,
    san_factory_GetIids,
    san_factory_GetRuntimeClassName,
    san_factory_GetTrustLevel,
    san_factory_ActivateInstance,
};

DEFINE_IINSPECTABLE( san_statics, ISymmetricAlgorithmNamesStatics, struct symmetricalgnames_factory, IActivationFactory_iface )

static HRESULT WINAPI san_statics_get_DesCbc( ISymmetricAlgorithmNamesStatics *iface, HSTRING *value ) { return WindowsCreateString( L"DES_CBC", 7, value ); }
static HRESULT WINAPI san_statics_get_DesEcb( ISymmetricAlgorithmNamesStatics *iface, HSTRING *value ) { return WindowsCreateString( L"DES_ECB", 7, value ); }
static HRESULT WINAPI san_statics_get_TripleDesCbc( ISymmetricAlgorithmNamesStatics *iface, HSTRING *value ) { return WindowsCreateString( L"3DES_CBC", 8, value ); }
static HRESULT WINAPI san_statics_get_TripleDesEcb( ISymmetricAlgorithmNamesStatics *iface, HSTRING *value ) { return WindowsCreateString( L"3DES_ECB", 8, value ); }
static HRESULT WINAPI san_statics_get_Rc2Cbc( ISymmetricAlgorithmNamesStatics *iface, HSTRING *value ) { return WindowsCreateString( L"RC2_CBC", 7, value ); }
static HRESULT WINAPI san_statics_get_Rc2Ecb( ISymmetricAlgorithmNamesStatics *iface, HSTRING *value ) { return WindowsCreateString( L"RC2_ECB", 7, value ); }
static HRESULT WINAPI san_statics_get_AesCbc( ISymmetricAlgorithmNamesStatics *iface, HSTRING *value ) { return WindowsCreateString( L"AES_CBC", 7, value ); }
static HRESULT WINAPI san_statics_get_AesEcb( ISymmetricAlgorithmNamesStatics *iface, HSTRING *value ) { return WindowsCreateString( L"AES_ECB", 7, value ); }
static HRESULT WINAPI san_statics_get_AesGcm( ISymmetricAlgorithmNamesStatics *iface, HSTRING *value ) { return WindowsCreateString( L"AES_GCM", 7, value ); }
static HRESULT WINAPI san_statics_get_AesCcm( ISymmetricAlgorithmNamesStatics *iface, HSTRING *value ) { return WindowsCreateString( L"AES_CCM", 7, value ); }
static HRESULT WINAPI san_statics_get_AesCbcPkcs7( ISymmetricAlgorithmNamesStatics *iface, HSTRING *value ) { return WindowsCreateString( L"AES_CBC_PKCS7", 13, value ); }
static HRESULT WINAPI san_statics_get_AesEcbPkcs7( ISymmetricAlgorithmNamesStatics *iface, HSTRING *value ) { return WindowsCreateString( L"AES_ECB_PKCS7", 13, value ); }
static HRESULT WINAPI san_statics_get_DesCbcPkcs7( ISymmetricAlgorithmNamesStatics *iface, HSTRING *value ) { return WindowsCreateString( L"DES_CBC_PKCS7", 13, value ); }
static HRESULT WINAPI san_statics_get_DesEcbPkcs7( ISymmetricAlgorithmNamesStatics *iface, HSTRING *value ) { return WindowsCreateString( L"DES_ECB_PKCS7", 13, value ); }
static HRESULT WINAPI san_statics_get_TripleDesCbcPkcs7( ISymmetricAlgorithmNamesStatics *iface, HSTRING *value ) { return WindowsCreateString( L"3DES_CBC_PKCS7", 14, value ); }
static HRESULT WINAPI san_statics_get_TripleDesEcbPkcs7( ISymmetricAlgorithmNamesStatics *iface, HSTRING *value ) { return WindowsCreateString( L"3DES_ECB_PKCS7", 14, value ); }
static HRESULT WINAPI san_statics_get_Rc2CbcPkcs7( ISymmetricAlgorithmNamesStatics *iface, HSTRING *value ) { return WindowsCreateString( L"RC2_CBC_PKCS7", 13, value ); }
static HRESULT WINAPI san_statics_get_Rc2EcbPkcs7( ISymmetricAlgorithmNamesStatics *iface, HSTRING *value ) { return WindowsCreateString( L"RC2_ECB_PKCS7", 13, value ); }
static HRESULT WINAPI san_statics_get_Rc4( ISymmetricAlgorithmNamesStatics *iface, HSTRING *value ) { return WindowsCreateString( L"RC4", 3, value ); }

static const struct ISymmetricAlgorithmNamesStaticsVtbl san_statics_vtbl =
{
    san_statics_QueryInterface,
    san_statics_AddRef,
    san_statics_Release,
    san_statics_GetIids,
    san_statics_GetRuntimeClassName,
    san_statics_GetTrustLevel,
    san_statics_get_DesCbc,
    san_statics_get_DesEcb,
    san_statics_get_TripleDesCbc,
    san_statics_get_TripleDesEcb,
    san_statics_get_Rc2Cbc,
    san_statics_get_Rc2Ecb,
    san_statics_get_AesCbc,
    san_statics_get_AesEcb,
    san_statics_get_AesGcm,
    san_statics_get_AesCcm,
    san_statics_get_AesCbcPkcs7,
    san_statics_get_AesEcbPkcs7,
    san_statics_get_DesCbcPkcs7,
    san_statics_get_DesEcbPkcs7,
    san_statics_get_TripleDesCbcPkcs7,
    san_statics_get_TripleDesEcbPkcs7,
    san_statics_get_Rc2CbcPkcs7,
    san_statics_get_Rc2EcbPkcs7,
    san_statics_get_Rc4,
};

static struct symmetricalgnames_factory san_factory_obj =
{
    {&san_factory_vtbl},
    {&san_statics_vtbl},
    1
};

IActivationFactory *symmetricalgnames_activation_factory = &san_factory_obj.IActivationFactory_iface;

/* -------------------------------------------------------------------------
 * HashAlgorithmNames
 * ------------------------------------------------------------------------- */
struct hashalgnames_factory
{
    IActivationFactory IActivationFactory_iface;
    IHashAlgorithmNamesStatics IHashAlgorithmNamesStatics_iface;
    LONG ref;
};

static inline struct hashalgnames_factory *impl_from_IActivationFactory_han( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct hashalgnames_factory, IActivationFactory_iface );
}

static HRESULT WINAPI han_factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct hashalgnames_factory *factory = impl_from_IActivationFactory_han( iface );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        IActivationFactory_AddRef( iface );
        *out = &factory->IActivationFactory_iface;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IHashAlgorithmNamesStatics ))
    {
        IActivationFactory_AddRef( iface );
        *out = &factory->IHashAlgorithmNamesStatics_iface;
        return S_OK;
    }

    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI han_factory_AddRef( IActivationFactory *iface ) { return 2; }
static ULONG WINAPI han_factory_Release( IActivationFactory *iface ) { return 1; }
static HRESULT WINAPI han_factory_GetIids( IActivationFactory *iface, ULONG *count, IID **iids ) { return E_NOTIMPL; }
static HRESULT WINAPI han_factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.Security.Cryptography.Core.HashAlgorithmNames", 50, name );
}
static HRESULT WINAPI han_factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *level ) { if (level) *level = BaseTrust; return S_OK; }
static HRESULT WINAPI han_factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance ) { return E_NOTIMPL; }

static const struct IActivationFactoryVtbl han_factory_vtbl =
{
    han_factory_QueryInterface,
    han_factory_AddRef,
    han_factory_Release,
    han_factory_GetIids,
    han_factory_GetRuntimeClassName,
    han_factory_GetTrustLevel,
    han_factory_ActivateInstance,
};

DEFINE_IINSPECTABLE( han_statics, IHashAlgorithmNamesStatics, struct hashalgnames_factory, IActivationFactory_iface )

static HRESULT WINAPI han_statics_get_Md5( IHashAlgorithmNamesStatics *iface, HSTRING *value ) { return WindowsCreateString( L"MD5", 3, value ); }
static HRESULT WINAPI han_statics_get_Sha1( IHashAlgorithmNamesStatics *iface, HSTRING *value ) { return WindowsCreateString( L"SHA1", 4, value ); }
static HRESULT WINAPI han_statics_get_Sha256( IHashAlgorithmNamesStatics *iface, HSTRING *value ) { return WindowsCreateString( L"SHA256", 6, value ); }
static HRESULT WINAPI han_statics_get_Sha384( IHashAlgorithmNamesStatics *iface, HSTRING *value ) { return WindowsCreateString( L"SHA384", 6, value ); }
static HRESULT WINAPI han_statics_get_Sha512( IHashAlgorithmNamesStatics *iface, HSTRING *value ) { return WindowsCreateString( L"SHA512", 6, value ); }

static const struct IHashAlgorithmNamesStaticsVtbl han_statics_vtbl =
{
    han_statics_QueryInterface,
    han_statics_AddRef,
    han_statics_Release,
    han_statics_GetIids,
    han_statics_GetRuntimeClassName,
    han_statics_GetTrustLevel,
    han_statics_get_Md5,
    han_statics_get_Sha1,
    han_statics_get_Sha256,
    han_statics_get_Sha384,
    han_statics_get_Sha512,
};

static struct hashalgnames_factory han_factory_obj =
{
    {&han_factory_vtbl},
    {&han_statics_vtbl},
    1
};

IActivationFactory *hashalgnames_activation_factory = &han_factory_obj.IActivationFactory_iface;

/* -------------------------------------------------------------------------
 * KeyDerivationAlgorithmNames
 * ------------------------------------------------------------------------- */
struct keyderivationalgnames_factory
{
    IActivationFactory IActivationFactory_iface;
    IKeyDerivationAlgorithmNamesStatics IKeyDerivationAlgorithmNamesStatics_iface;
    LONG ref;
};

static inline struct keyderivationalgnames_factory *impl_from_IActivationFactory_kdan( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct keyderivationalgnames_factory, IActivationFactory_iface );
}

static HRESULT WINAPI kdan_factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct keyderivationalgnames_factory *factory = impl_from_IActivationFactory_kdan( iface );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        IActivationFactory_AddRef( iface );
        *out = &factory->IActivationFactory_iface;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IKeyDerivationAlgorithmNamesStatics ))
    {
        IActivationFactory_AddRef( iface );
        *out = &factory->IKeyDerivationAlgorithmNamesStatics_iface;
        return S_OK;
    }

    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI kdan_factory_AddRef( IActivationFactory *iface ) { return 2; }
static ULONG WINAPI kdan_factory_Release( IActivationFactory *iface ) { return 1; }
static HRESULT WINAPI kdan_factory_GetIids( IActivationFactory *iface, ULONG *count, IID **iids ) { return E_NOTIMPL; }
static HRESULT WINAPI kdan_factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.Security.Cryptography.Core.KeyDerivationAlgorithmNames", 59, name );
}
static HRESULT WINAPI kdan_factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *level ) { if (level) *level = BaseTrust; return S_OK; }
static HRESULT WINAPI kdan_factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance ) { return E_NOTIMPL; }

static const struct IActivationFactoryVtbl kdan_factory_vtbl =
{
    kdan_factory_QueryInterface,
    kdan_factory_AddRef,
    kdan_factory_Release,
    kdan_factory_GetIids,
    kdan_factory_GetRuntimeClassName,
    kdan_factory_GetTrustLevel,
    kdan_factory_ActivateInstance,
};

DEFINE_IINSPECTABLE( kdan_statics, IKeyDerivationAlgorithmNamesStatics, struct keyderivationalgnames_factory, IActivationFactory_iface )

static HRESULT WINAPI kdan_statics_get_Pbkdf2Md5( IKeyDerivationAlgorithmNamesStatics *iface, HSTRING *value ) { return WindowsCreateString( L"PBKDF2_MD5", 10, value ); }
static HRESULT WINAPI kdan_statics_get_Pbkdf2Sha1( IKeyDerivationAlgorithmNamesStatics *iface, HSTRING *value ) { return WindowsCreateString( L"PBKDF2_SHA1", 11, value ); }
static HRESULT WINAPI kdan_statics_get_Pbkdf2Sha256( IKeyDerivationAlgorithmNamesStatics *iface, HSTRING *value ) { return WindowsCreateString( L"PBKDF2_SHA256", 13, value ); }
static HRESULT WINAPI kdan_statics_get_Pbkdf2Sha384( IKeyDerivationAlgorithmNamesStatics *iface, HSTRING *value ) { return WindowsCreateString( L"PBKDF2_SHA384", 13, value ); }
static HRESULT WINAPI kdan_statics_get_Pbkdf2Sha512( IKeyDerivationAlgorithmNamesStatics *iface, HSTRING *value ) { return WindowsCreateString( L"PBKDF2_SHA512", 13, value ); }
static HRESULT WINAPI kdan_statics_get_Sp800108CtrHmacMd5( IKeyDerivationAlgorithmNamesStatics *iface, HSTRING *value ) { return WindowsCreateString( L"SP800_108_CTR_HMAC_MD5", 22, value ); }
static HRESULT WINAPI kdan_statics_get_Sp800108CtrHmacSha1( IKeyDerivationAlgorithmNamesStatics *iface, HSTRING *value ) { return WindowsCreateString( L"SP800_108_CTR_HMAC_SHA1", 23, value ); }
static HRESULT WINAPI kdan_statics_get_Sp800108CtrHmacSha256( IKeyDerivationAlgorithmNamesStatics *iface, HSTRING *value ) { return WindowsCreateString( L"SP800_108_CTR_HMAC_SHA256", 25, value ); }
static HRESULT WINAPI kdan_statics_get_Sp800108CtrHmacSha384( IKeyDerivationAlgorithmNamesStatics *iface, HSTRING *value ) { return WindowsCreateString( L"SP800_108_CTR_HMAC_SHA384", 25, value ); }
static HRESULT WINAPI kdan_statics_get_Sp800108CtrHmacSha512( IKeyDerivationAlgorithmNamesStatics *iface, HSTRING *value ) { return WindowsCreateString( L"SP800_108_CTR_HMAC_SHA512", 25, value ); }
static HRESULT WINAPI kdan_statics_get_Sp80056aConcatMd5( IKeyDerivationAlgorithmNamesStatics *iface, HSTRING *value ) { return WindowsCreateString( L"SP800_56A_CONCAT_MD5", 20, value ); }
static HRESULT WINAPI kdan_statics_get_Sp80056aConcatSha1( IKeyDerivationAlgorithmNamesStatics *iface, HSTRING *value ) { return WindowsCreateString( L"SP800_56A_CONCAT_SHA1", 21, value ); }
static HRESULT WINAPI kdan_statics_get_Sp80056aConcatSha256( IKeyDerivationAlgorithmNamesStatics *iface, HSTRING *value ) { return WindowsCreateString( L"SP800_56A_CONCAT_SHA256", 23, value ); }
static HRESULT WINAPI kdan_statics_get_Sp80056aConcatSha384( IKeyDerivationAlgorithmNamesStatics *iface, HSTRING *value ) { return WindowsCreateString( L"SP800_56A_CONCAT_SHA384", 23, value ); }
static HRESULT WINAPI kdan_statics_get_Sp80056aConcatSha512( IKeyDerivationAlgorithmNamesStatics *iface, HSTRING *value ) { return WindowsCreateString( L"SP800_56A_CONCAT_SHA512", 23, value ); }

static const struct IKeyDerivationAlgorithmNamesStaticsVtbl kdan_statics_vtbl =
{
    kdan_statics_QueryInterface,
    kdan_statics_AddRef,
    kdan_statics_Release,
    kdan_statics_GetIids,
    kdan_statics_GetRuntimeClassName,
    kdan_statics_GetTrustLevel,
    kdan_statics_get_Pbkdf2Md5,
    kdan_statics_get_Pbkdf2Sha1,
    kdan_statics_get_Pbkdf2Sha256,
    kdan_statics_get_Pbkdf2Sha384,
    kdan_statics_get_Pbkdf2Sha512,
    kdan_statics_get_Sp800108CtrHmacMd5,
    kdan_statics_get_Sp800108CtrHmacSha1,
    kdan_statics_get_Sp800108CtrHmacSha256,
    kdan_statics_get_Sp800108CtrHmacSha384,
    kdan_statics_get_Sp800108CtrHmacSha512,
    kdan_statics_get_Sp80056aConcatMd5,
    kdan_statics_get_Sp80056aConcatSha1,
    kdan_statics_get_Sp80056aConcatSha256,
    kdan_statics_get_Sp80056aConcatSha384,
    kdan_statics_get_Sp80056aConcatSha512,
};

static struct keyderivationalgnames_factory kdan_factory_obj =
{
    {&kdan_factory_vtbl},
    {&kdan_statics_vtbl},
    1
};

IActivationFactory *keyderivationalgnames_activation_factory = &kdan_factory_obj.IActivationFactory_iface;
