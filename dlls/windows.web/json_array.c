#include "private.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(web);

struct json_array
{
    IJsonArray IJsonArray_iface;
    IJsonValueInternal IJsonValueInternal_iface;
    LONG ref;

    cJSON *node;
    struct json_root *root;
    HSTRING str_cache;
};

static inline struct json_array *impl_from_IJsonArray( IJsonArray *iface )
{
    return CONTAINING_RECORD( iface, struct json_array, IJsonArray_iface );
}

static inline struct json_array *impl_from_IJsonValueInternal( IJsonValueInternal *iface )
{
    return CONTAINING_RECORD( iface, struct json_array, IJsonValueInternal_iface );
}

static HRESULT WINAPI json_array_internal_QueryInterface( IJsonValueInternal *iface, REFIID iid, void **out )
{
    struct json_array *impl = impl_from_IJsonValueInternal( iface );
    return IJsonArray_QueryInterface( &impl->IJsonArray_iface, iid, out );
}

static ULONG WINAPI json_array_internal_AddRef( IJsonValueInternal *iface )
{
    struct json_array *impl = impl_from_IJsonValueInternal( iface );
    return IJsonArray_AddRef( &impl->IJsonArray_iface );
}

static ULONG WINAPI json_array_internal_Release( IJsonValueInternal *iface )
{
    struct json_array *impl = impl_from_IJsonValueInternal( iface );
    return IJsonArray_Release( &impl->IJsonArray_iface );
}

static cJSON * WINAPI json_array_internal_GetNode( IJsonValueInternal *iface )
{
    struct json_array *impl = impl_from_IJsonValueInternal( iface );
    return impl->node;
}

static const struct IJsonValueInternalVtbl json_array_internal_vtbl =
{
    json_array_internal_QueryInterface,
    json_array_internal_AddRef,
    json_array_internal_Release,
    json_array_internal_GetNode,
};

HRESULT json_array_push( IJsonArray *iface, IJsonValue *value )
{
    struct json_array *impl = impl_from_IJsonArray( iface );
    cJSON *new_node = cjson_node_duplicate_from_IJsonValue( value );

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!new_node) return E_OUTOFMEMORY;
    cJSON_AddItemToArray( impl->node, new_node );

    if (impl->str_cache)
    {
        WindowsDeleteString( impl->str_cache );
        impl->str_cache = NULL;
    }
    return S_OK;
}

static HRESULT WINAPI json_array_QueryInterface( IJsonArray *iface, REFIID iid, void **out )
{
    struct json_array *impl = impl_from_IJsonArray( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IJsonArray ))
    {
        *out = &impl->IJsonArray_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IJsonValueInternal ))
    {
        *out = &impl->IJsonValueInternal_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI json_array_AddRef( IJsonArray *iface )
{
    struct json_array *impl = impl_from_IJsonArray( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p, ref %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI json_array_Release( IJsonArray *iface )
{
    struct json_array *impl = impl_from_IJsonArray( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    TRACE( "iface %p, ref %lu.\n", iface, ref );

    if (!ref)
    {
        if (impl->str_cache)
            WindowsDeleteString( impl->str_cache );
        json_root_release( impl->root );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI json_array_GetIids( IJsonArray *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI json_array_GetRuntimeClassName( IJsonArray *iface, HSTRING *class_name )
{
    FIXME( "iface %p, class_name %p stub!\n", iface, class_name );
    return E_NOTIMPL;
}

static HRESULT WINAPI json_array_GetTrustLevel( IJsonArray *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI json_array_GetObjectAt( IJsonArray *iface, UINT32 index, IJsonObject **value )
{
    struct json_array *impl = impl_from_IJsonArray( iface );
    cJSON *child;

    TRACE( "iface %p, index %u, value %p\n", iface, index, value );

    if (!value) return E_INVALIDARG;
    *value = NULL;

    child = cJSON_GetArrayItem( impl->node, (int)index );
    if (!child) return E_BOUNDS;
    if (!(child->type & cJSON_Object)) return E_ILLEGAL_METHOD_CALL;

    return create_json_object( child, impl->root, value );
}

static HRESULT WINAPI json_array_GetArrayAt( IJsonArray *iface, UINT32 index, IJsonArray **value )
{
    struct json_array *impl = impl_from_IJsonArray( iface );
    cJSON *child;

    TRACE( "iface %p, index %u, value %p\n", iface, index, value );

    if (!value) return E_INVALIDARG;
    *value = NULL;

    child = cJSON_GetArrayItem( impl->node, (int)index );
    if (!child) return E_BOUNDS;
    if (!(child->type & cJSON_Array)) return E_ILLEGAL_METHOD_CALL;

    return create_json_array( child, impl->root, value );
}

static HRESULT WINAPI json_array_GetStringAt( IJsonArray *iface, UINT32 index, HSTRING *value )
{
    struct json_array *impl = impl_from_IJsonArray( iface );
    cJSON *child;
    int len;

    TRACE( "iface %p, index %u, value %p\n", iface, index, value );

    if (!value) return E_INVALIDARG;
    *value = NULL;

    child = cJSON_GetArrayItem( impl->node, (int)index );
    if (!child) return E_BOUNDS;
    if (!(child->type & cJSON_String)) return E_ILLEGAL_METHOD_CALL;

    len = child->valueint > 0 ? child->valueint : strlen( child->valuestring );
    return hstring_from_utf8_len( child->valuestring, len, value );
}

static HRESULT WINAPI json_array_GetNumberAt( IJsonArray *iface, UINT32 index, DOUBLE *value )
{
    struct json_array *impl = impl_from_IJsonArray( iface );
    cJSON *child;

    TRACE( "iface %p, index %u, value %p\n", iface, index, value );

    if (!value) return E_INVALIDARG;

    child = cJSON_GetArrayItem( impl->node, (int)index );
    if (!child) return E_BOUNDS;
    if (!(child->type & cJSON_Number)) return E_ILLEGAL_METHOD_CALL;

    *value = child->valuedouble;
    return S_OK;
}

static HRESULT WINAPI json_array_GetBooleanAt( IJsonArray *iface, UINT32 index, boolean *value )
{
    struct json_array *impl = impl_from_IJsonArray( iface );
    cJSON *child;

    TRACE( "iface %p, index %u, value %p\n", iface, index, value );

    if (!value) return E_INVALIDARG;

    child = cJSON_GetArrayItem( impl->node, (int)index );
    if (!child) return E_BOUNDS;
    if (!(child->type & (cJSON_False | cJSON_True))) return E_ILLEGAL_METHOD_CALL;

    *value = (child->type & cJSON_True) ? TRUE : FALSE;
    return S_OK;
}

static const struct IJsonArrayVtbl json_array_vtbl =
{
    json_array_QueryInterface,
    json_array_AddRef,
    json_array_Release,
    json_array_GetIids,
    json_array_GetRuntimeClassName,
    json_array_GetTrustLevel,
    json_array_GetObjectAt,
    json_array_GetArrayAt,
    json_array_GetStringAt,
    json_array_GetNumberAt,
    json_array_GetBooleanAt,
};

HRESULT create_json_array( cJSON *node, struct json_root *root, IJsonArray **out )
{
    struct json_array *impl;

    if (!out) return E_POINTER;
    *out = NULL;
    if (!node) return E_FAIL;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->IJsonArray_iface.lpVtbl = &json_array_vtbl;
    impl->IJsonValueInternal_iface.lpVtbl = &json_array_internal_vtbl;
    impl->ref = 1;
    impl->node = node;
    impl->root = root;
    json_root_addref( root );

    *out = &impl->IJsonArray_iface;
    return S_OK;
}

struct json_array_statics
{
    IActivationFactory IActivationFactory_iface;
    LONG ref;
};

static inline struct json_array_statics *impl_from_IActivationFactory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct json_array_statics, IActivationFactory_iface );
}

static HRESULT WINAPI factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct json_array_statics *impl = impl_from_IActivationFactory( iface );

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

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI factory_AddRef( IActivationFactory *iface )
{
    struct json_array_statics *impl = impl_from_IActivationFactory( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p, ref %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI factory_Release( IActivationFactory *iface )
{
    struct json_array_statics *impl = impl_from_IActivationFactory( iface );
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
    cJSON *node;
    struct json_root *root;
    IJsonArray *arr;
    HRESULT hr;

    TRACE( "iface %p, instance %p.\n", iface, instance );

    if (!instance) return E_POINTER;
    *instance = NULL;

    node = cJSON_CreateArray();
    if (!node) return E_OUTOFMEMORY;

    root = json_root_create( node );
    if (!root)
    {
        cJSON_Delete( node );
        return E_OUTOFMEMORY;
    }

    hr = create_json_array( node, root, &arr );
    json_root_release( root );
    if (FAILED(hr)) return hr;

    *instance = (IInspectable *)arr;
    return S_OK;
}

static const struct IActivationFactoryVtbl factory_vtbl =
{
    factory_QueryInterface,
    factory_AddRef,
    factory_Release,
    factory_GetIids,
    factory_GetRuntimeClassName,
    factory_GetTrustLevel,
    factory_ActivateInstance,
};

static struct json_array_statics json_array_statics =
{
    {&factory_vtbl},
    1,
};

IActivationFactory *json_array_factory = &json_array_statics.IActivationFactory_iface;
