#include "private.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(crypto);

static const GUID IID_IDataProtectionProvider = {0x09639948, 0xed22, 0x4270, {0xbd, 0x1c, 0x6d, 0x72, 0xc0, 0x0f, 0x87, 0x87}};
static const GUID IID_IDataProtectionProviderFactory = {0xadf33dac, 0x4932, 0x4cdf, {0xac, 0x41, 0x72, 0x14, 0x33, 0x35, 0x14, 0xca}};
static const GUID IID_IReferenceTracker = {0x11d3b13a, 0x180e, 0x4789, {0xa8, 0xbe, 0x77, 0x12, 0x88, 0x28, 0x93, 0xe6}};
static const GUID IID_IReferenceTrackerTarget = {0x5c13e51c, 0x4f32, 0x4726, {0xa3, 0xfd, 0xf3, 0xed, 0xd6, 0x3d, 0xa3, 0xa0}};

struct async_buffer
{
    IInspectable IInspectable_iface;
    IAsyncInfo IAsyncInfo_iface;
    LONG ref;
    IInspectable *buffer;
};

static inline struct async_buffer *impl_from_IInspectable( IInspectable *iface )
{
    return CONTAINING_RECORD( iface, struct async_buffer, IInspectable_iface );
}

static inline struct async_buffer *impl_from_IAsyncInfo( IAsyncInfo *iface )
{
    return CONTAINING_RECORD( iface, struct async_buffer, IAsyncInfo_iface );
}

static HRESULT WINAPI async_buffer_QueryInterface( IInspectable *iface, REFIID iid, void **out )
{
    struct async_buffer *impl = impl_from_IInspectable( iface );

    if (IsEqualGUID( iid, &IID_IReferenceTracker ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IAsyncOperation_IBuffer ))
    {
        InterlockedIncrement( &impl->ref );
        *out = &impl->IInspectable_iface;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IAsyncInfo ))
    {
        InterlockedIncrement( &impl->ref );
        *out = &impl->IAsyncInfo_iface;
        return S_OK;
    }

    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI async_buffer_AddRef( IInspectable *iface )
{
    struct async_buffer *impl = impl_from_IInspectable( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI async_buffer_Release( IInspectable *iface )
{
    struct async_buffer *impl = impl_from_IInspectable( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        if (impl->buffer) IInspectable_Release( impl->buffer );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI async_buffer_GetIids( IInspectable *iface, ULONG *count, IID **iids )
{
    if (!count || !iids) return E_POINTER;
    *count = 0;
    *iids = NULL;
    return S_OK;
}
static HRESULT WINAPI async_buffer_GetRuntimeClassName( IInspectable *iface, HSTRING *name )
{
    static const WCHAR class_name[] = L"Windows.Foundation.IAsyncOperation`1<Windows.Storage.Streams.IBuffer>";
    if (!name) return E_POINTER;
    return WindowsCreateString( class_name, ARRAY_SIZE(class_name) - 1, name );
}
static HRESULT WINAPI async_buffer_GetTrustLevel( IInspectable *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI async_buffer_put_Completed( IInspectable *iface, void *handler )
{
    if (handler)
    {
        typedef HRESULT (WINAPI *invoke_fn)( void *handler, void *async, AsyncStatus status );
        void **vtbl = *(void ***)handler;
        invoke_fn invoke = (invoke_fn)vtbl[3];
        invoke( handler, iface, Completed );
    }
    return S_OK;
}

static HRESULT WINAPI async_buffer_get_Completed( IInspectable *iface, void **handler )
{
    if (handler) *handler = NULL;
    return S_OK;
}

static HRESULT WINAPI async_buffer_GetResults( IInspectable *iface, void **results )
{
    struct async_buffer *impl = impl_from_IInspectable( iface );
    if (!results) return E_POINTER;
    if (impl->buffer) IInspectable_AddRef( impl->buffer );
    *results = impl->buffer;
    return S_OK;
}

static const void *async_buffer_vtbl[] =
{
    async_buffer_QueryInterface,
    async_buffer_AddRef,
    async_buffer_Release,
    async_buffer_GetIids,
    async_buffer_GetRuntimeClassName,
    async_buffer_GetTrustLevel,
    async_buffer_put_Completed,
    async_buffer_get_Completed,
    async_buffer_GetResults,
};

static HRESULT WINAPI async_info_QueryInterface( IAsyncInfo *iface, REFIID iid, void **out )
{
    struct async_buffer *impl = impl_from_IAsyncInfo( iface );
    return IInspectable_QueryInterface( &impl->IInspectable_iface, iid, out );
}
static ULONG WINAPI async_info_AddRef( IAsyncInfo *iface )
{
    struct async_buffer *impl = impl_from_IAsyncInfo( iface );
    return IInspectable_AddRef( &impl->IInspectable_iface );
}
static ULONG WINAPI async_info_Release( IAsyncInfo *iface )
{
    struct async_buffer *impl = impl_from_IAsyncInfo( iface );
    return IInspectable_Release( &impl->IInspectable_iface );
}
static HRESULT WINAPI async_info_GetIids( IAsyncInfo *iface, ULONG *count, IID **iids )
{
    if (!count || !iids) return E_POINTER;
    *count = 0;
    *iids = NULL;
    return S_OK;
}
static HRESULT WINAPI async_info_GetRuntimeClassName( IAsyncInfo *iface, HSTRING *name )
{
    static const WCHAR class_name[] = L"Windows.Foundation.IAsyncInfo";
    if (!name) return E_POINTER;
    return WindowsCreateString( class_name, ARRAY_SIZE(class_name) - 1, name );
}
static HRESULT WINAPI async_info_GetTrustLevel( IAsyncInfo *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}
static HRESULT WINAPI async_info_get_Id( IAsyncInfo *iface, UINT32 *id )
{
    if (!id) return E_POINTER;
    *id = 1;
    return S_OK;
}
static HRESULT WINAPI async_info_get_Status( IAsyncInfo *iface, AsyncStatus *status )
{
    if (!status) return E_POINTER;
    *status = Completed;
    return S_OK;
}
static HRESULT WINAPI async_info_get_ErrorCode( IAsyncInfo *iface, HRESULT *error )
{
    if (!error) return E_POINTER;
    *error = S_OK;
    return S_OK;
}
static HRESULT WINAPI async_info_Cancel( IAsyncInfo *iface ) { return S_OK; }
static HRESULT WINAPI async_info_Close( IAsyncInfo *iface ) { return S_OK; }

static const struct IAsyncInfoVtbl async_info_vtbl =
{
    async_info_QueryInterface,
    async_info_AddRef,
    async_info_Release,
    async_info_GetIids,
    async_info_GetRuntimeClassName,
    async_info_GetTrustLevel,
    async_info_get_Id,
    async_info_get_Status,
    async_info_get_ErrorCode,
    async_info_Cancel,
    async_info_Close,
};

HRESULT create_async_buffer( IInspectable *buffer, void **out )
{
    struct async_buffer *impl;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->IInspectable_iface.lpVtbl = (void *)async_buffer_vtbl;
    impl->IAsyncInfo_iface.lpVtbl = &async_info_vtbl;
    impl->ref = 1;
    if (buffer) IInspectable_AddRef( buffer );
    impl->buffer = buffer;
    *out = &impl->IInspectable_iface;
    return S_OK;
}

/* --- Async Action Implementation --- */

struct async_action
{
    IInspectable IInspectable_iface;
    IAsyncInfo IAsyncInfo_iface;
    LONG ref;
};

static inline struct async_action *impl_from_async_action_IInspectable( IInspectable *iface )
{
    return CONTAINING_RECORD( iface, struct async_action, IInspectable_iface );
}

static inline struct async_action *impl_from_async_action_IAsyncInfo( IAsyncInfo *iface )
{
    return CONTAINING_RECORD( iface, struct async_action, IAsyncInfo_iface );
}

static HRESULT WINAPI async_action_QueryInterface( IInspectable *iface, REFIID iid, void **out )
{
    struct async_action *impl = impl_from_async_action_IInspectable( iface );

    if (IsEqualGUID( iid, &IID_IReferenceTracker ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IAsyncAction ))
    {
        InterlockedIncrement( &impl->ref );
        *out = &impl->IInspectable_iface;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IAsyncInfo ))
    {
        InterlockedIncrement( &impl->ref );
        *out = &impl->IAsyncInfo_iface;
        return S_OK;
    }

    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI async_action_AddRef( IInspectable *iface )
{
    struct async_action *impl = impl_from_async_action_IInspectable( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI async_action_Release( IInspectable *iface )
{
    struct async_action *impl = impl_from_async_action_IInspectable( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref) free( impl );
    return ref;
}

static HRESULT WINAPI async_action_GetIids( IInspectable *iface, ULONG *count, IID **iids )
{
    if (!count || !iids) return E_POINTER;
    *count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI async_action_GetRuntimeClassName( IInspectable *iface, HSTRING *name )
{
    static const WCHAR class_name[] = L"Windows.Foundation.IAsyncAction";
    if (!name) return E_POINTER;
    return WindowsCreateString( class_name, ARRAY_SIZE(class_name) - 1, name );
}

static HRESULT WINAPI async_action_GetTrustLevel( IInspectable *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI async_action_put_Completed( IInspectable *iface, void *handler )
{
    if (handler)
    {
        typedef HRESULT (WINAPI *invoke_fn)( void *handler, void *async, AsyncStatus status );
        void **vtbl = *(void ***)handler;
        invoke_fn invoke = (invoke_fn)vtbl[3];
        invoke( handler, iface, Completed );
    }
    return S_OK;
}

static HRESULT WINAPI async_action_get_Completed( IInspectable *iface, void **handler )
{
    if (handler) *handler = NULL;
    return S_OK;
}

static HRESULT WINAPI async_action_GetResults( IInspectable *iface )
{
    return S_OK;
}

static const void *async_action_vtbl[] =
{
    async_action_QueryInterface,
    async_action_AddRef,
    async_action_Release,
    async_action_GetIids,
    async_action_GetRuntimeClassName,
    async_action_GetTrustLevel,
    async_action_put_Completed,
    async_action_get_Completed,
    async_action_GetResults,
};

static HRESULT WINAPI async_action_info_QueryInterface( IAsyncInfo *iface, REFIID iid, void **out )
{
    struct async_action *impl = impl_from_async_action_IAsyncInfo( iface );
    return IInspectable_QueryInterface( &impl->IInspectable_iface, iid, out );
}
static ULONG WINAPI async_action_info_AddRef( IAsyncInfo *iface )
{
    struct async_action *impl = impl_from_async_action_IAsyncInfo( iface );
    return IInspectable_AddRef( &impl->IInspectable_iface );
}
static ULONG WINAPI async_action_info_Release( IAsyncInfo *iface )
{
    struct async_action *impl = impl_from_async_action_IAsyncInfo( iface );
    return IInspectable_Release( &impl->IInspectable_iface );
}
static HRESULT WINAPI async_action_info_GetIids( IAsyncInfo *iface, ULONG *count, IID **iids )
{
    if (!count || !iids) return E_POINTER;
    *count = 0;
    *iids = NULL;
    return S_OK;
}
static HRESULT WINAPI async_action_info_GetRuntimeClassName( IAsyncInfo *iface, HSTRING *name )
{
    if (!name) return E_POINTER;
    return WindowsCreateString( L"Windows.Foundation.IAsyncInfo", 27, name );
}
static HRESULT WINAPI async_action_info_GetTrustLevel( IAsyncInfo *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}
static HRESULT WINAPI async_action_info_get_Id( IAsyncInfo *iface, UINT32 *id )
{
    if (!id) return E_POINTER;
    *id = 1;
    return S_OK;
}
static HRESULT WINAPI async_action_info_get_Status( IAsyncInfo *iface, AsyncStatus *status )
{
    if (!status) return E_POINTER;
    *status = Completed;
    return S_OK;
}
static HRESULT WINAPI async_action_info_get_ErrorCode( IAsyncInfo *iface, HRESULT *error )
{
    if (!error) return E_POINTER;
    *error = S_OK;
    return S_OK;
}
static HRESULT WINAPI async_action_info_Cancel( IAsyncInfo *iface ) { return S_OK; }
static HRESULT WINAPI async_action_info_Close( IAsyncInfo *iface ) { return S_OK; }

static const struct IAsyncInfoVtbl async_action_info_vtbl =
{
    async_action_info_QueryInterface,
    async_action_info_AddRef,
    async_action_info_Release,
    async_action_info_GetIids,
    async_action_info_GetRuntimeClassName,
    async_action_info_GetTrustLevel,
    async_action_info_get_Id,
    async_action_info_get_Status,
    async_action_info_get_ErrorCode,
    async_action_info_Cancel,
    async_action_info_Close,
};

HRESULT create_async_action( void **out )
{
    struct async_action *impl;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->IInspectable_iface.lpVtbl = (void *)async_action_vtbl;
    impl->IAsyncInfo_iface.lpVtbl = &async_action_info_vtbl;
    impl->ref = 1;
    *out = &impl->IInspectable_iface;
    return S_OK;
}

struct data_protection_provider
{
    IInspectable IInspectable_iface;
    LONG ref;
};

static inline struct data_protection_provider *impl_from_provider( void *iface )
{
    return CONTAINING_RECORD( iface, struct data_protection_provider, IInspectable_iface );
}

static HRESULT WINAPI provider_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct data_protection_provider *impl = impl_from_provider( iface );

    if (IsEqualGUID( iid, &IID_IReferenceTracker ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IDataProtectionProvider ))
    {
        InterlockedIncrement( &impl->ref );
        *out = &impl->IInspectable_iface;
        return S_OK;
    }

    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI provider_AddRef( void *iface )
{
    struct data_protection_provider *impl = impl_from_provider( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI provider_Release( void *iface )
{
    struct data_protection_provider *impl = impl_from_provider( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref) free( impl );
    return ref;
}

static HRESULT WINAPI provider_GetIids( void *iface, ULONG *count, IID **iids )
{
    if (!count || !iids) return E_POINTER;
    *count = 0;
    *iids = NULL;
    return S_OK;
}
static HRESULT WINAPI provider_GetRuntimeClassName( void *iface, HSTRING *name )
{
    const WCHAR class_name[] = L"Windows.Security.Cryptography.DataProtection.DataProtectionProvider";
    if (!name) return E_POINTER;
    return WindowsCreateString( class_name, ARRAY_SIZE(class_name) - 1, name );
}
static HRESULT WINAPI provider_GetTrustLevel( void *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI provider_ProtectAsync( void *iface, void *data, void **value )
{
    TRACE( "iface %p, data %p, value %p\n", iface, data, value );
    if (!value) return E_POINTER;
    if (!data) return E_INVALIDARG;
    return create_async_buffer( (IInspectable *)data, value );
}

static HRESULT WINAPI provider_UnprotectAsync( void *iface, void *data, void **value )
{
    TRACE( "iface %p, data %p, value %p\n", iface, data, value );
    if (!value) return E_POINTER;
    if (!data) return E_INVALIDARG;
    return create_async_buffer( (IInspectable *)data, value );
}

static HRESULT WINAPI provider_ProtectStreamAsync( void *iface, void *src, void *dest, void **value )
{
    TRACE( "iface %p, src %p, dest %p, value %p\n", iface, src, dest, value );
    if (!value) return E_POINTER;
    return create_async_action( value );
}

static HRESULT WINAPI provider_UnprotectStreamAsync( void *iface, void *src, void *dest, void **value )
{
    TRACE( "iface %p, src %p, dest %p, value %p\n", iface, src, dest, value );
    if (!value) return E_POINTER;
    return create_async_action( value );
}

static const void *provider_vtbl[] =
{
    provider_QueryInterface,
    provider_AddRef,
    provider_Release,
    provider_GetIids,
    provider_GetRuntimeClassName,
    provider_GetTrustLevel,
    provider_ProtectAsync,
    provider_UnprotectAsync,
    provider_ProtectStreamAsync,
    provider_UnprotectStreamAsync,
};

static HRESULT create_provider( void **out )
{
    struct data_protection_provider *impl;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->IInspectable_iface.lpVtbl = (void *)provider_vtbl;
    impl->ref = 1;
    *out = &impl->IInspectable_iface;
    return S_OK;
}

struct dataprotection_factory
{
    IActivationFactory IActivationFactory_iface;
    IInspectable IDataProtectionProviderFactory_iface;
    LONG ref;
};

static inline struct dataprotection_factory *impl_from_IActivationFactory_dp( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct dataprotection_factory, IActivationFactory_iface );
}

static inline struct dataprotection_factory *impl_from_IDataProtectionProviderFactory( void *iface )
{
    return CONTAINING_RECORD( iface, struct dataprotection_factory, IDataProtectionProviderFactory_iface );
}

static HRESULT WINAPI factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct dataprotection_factory *impl = impl_from_IActivationFactory_dp( iface );

    TRACE( "iid %s\n", debugstr_guid( iid ) );
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        IActivationFactory_AddRef( iface );
        *out = &impl->IActivationFactory_iface;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IDataProtectionProviderFactory ))
    {
        IActivationFactory_AddRef( iface );
        *out = &impl->IDataProtectionProviderFactory_iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI factory_AddRef( IActivationFactory *iface ) { return 2; }
static ULONG WINAPI factory_Release( IActivationFactory *iface ) { return 1; }
static HRESULT WINAPI factory_GetIids( IActivationFactory *iface, ULONG *count, IID **iids )
{
    if (!count || !iids) return E_POINTER;
    *count = 0;
    *iids = NULL;
    return S_OK;
}
static HRESULT WINAPI factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *name )
{
    const WCHAR class_name[] = L"Windows.Security.Cryptography.DataProtection.DataProtectionProvider";
    return WindowsCreateString( class_name, ARRAY_SIZE(class_name) - 1, name );
}
static HRESULT WINAPI factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *level ) { if (level) *level = BaseTrust; return S_OK; }

static HRESULT WINAPI factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    HRESULT hr;
    if (!instance) return E_POINTER;
    hr = create_provider( (void **)instance );
    return hr;
}

static HRESULT WINAPI factory_CreateOverloadExplicit( void *iface, HSTRING descriptor, void **value )
{
    HRESULT hr;
    if (!value) return E_POINTER;
    hr = create_provider( value );
    return hr;
}

static HRESULT WINAPI dp_factory_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct dataprotection_factory *impl = impl_from_IDataProtectionProviderFactory( iface );
    return factory_QueryInterface( &impl->IActivationFactory_iface, iid, out );
}
static ULONG WINAPI dp_factory_AddRef( void *iface )
{
    struct dataprotection_factory *impl = impl_from_IDataProtectionProviderFactory( iface );
    return factory_AddRef( &impl->IActivationFactory_iface );
}
static ULONG WINAPI dp_factory_Release( void *iface )
{
    struct dataprotection_factory *impl = impl_from_IDataProtectionProviderFactory( iface );
    return factory_Release( &impl->IActivationFactory_iface );
}

static const void *dataprotection_factory_vtbl[] =
{
    factory_QueryInterface,
    factory_AddRef,
    factory_Release,
    factory_GetIids,
    factory_GetRuntimeClassName,
    factory_GetTrustLevel,
    factory_ActivateInstance,
};

static const void *dataprotection_provider_factory_vtbl[] =
{
    dp_factory_QueryInterface,
    dp_factory_AddRef,
    dp_factory_Release,
    factory_GetIids,
    factory_GetRuntimeClassName,
    factory_GetTrustLevel,
    factory_CreateOverloadExplicit,
};

static struct dataprotection_factory dataprotection_factory_obj =
{
    {(void *)dataprotection_factory_vtbl},
    {(void *)dataprotection_provider_factory_vtbl},
    1
};

IActivationFactory *dataprotection_activation_factory = &dataprotection_factory_obj.IActivationFactory_iface;
