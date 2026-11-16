#include "private.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(web);

struct json_value_statics
{
    IActivationFactory IActivationFactory_iface;
    IJsonValueStatics IJsonValueStatics_iface;
    LONG ref;
};

static inline struct json_value_statics *impl_from_IActivationFactory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct json_value_statics, IActivationFactory_iface );
}

static HRESULT WINAPI factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct json_value_statics *impl = impl_from_IActivationFactory( iface );

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

    if (IsEqualGUID( iid, &IID_IJsonValueStatics ))
    {
        *out = &impl->IJsonValueStatics_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI factory_AddRef( IActivationFactory *iface )
{
    struct json_value_statics *impl = impl_from_IActivationFactory( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p increasing refcount to %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI factory_Release( IActivationFactory *iface )
{
    struct json_value_statics *impl = impl_from_IActivationFactory( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    TRACE( "iface %p decreasing refcount to %lu.\n", iface, ref );
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
    FIXME( "iface %p, instance %p stub!\n", iface, instance );
    return E_NOTIMPL;
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

struct json_value
{
    IJsonValue IJsonValue_iface;
    IJsonValueInternal IJsonValueInternal_iface;
    LONG ref;

    cJSON *node;
    struct json_root *root;
    HSTRING str_cache;
};

static inline struct json_value *impl_from_IJsonValue( IJsonValue *iface )
{
    return CONTAINING_RECORD( iface, struct json_value, IJsonValue_iface );
}

static inline struct json_value *impl_from_IJsonValueInternal( IJsonValueInternal *iface )
{
    return CONTAINING_RECORD( iface, struct json_value, IJsonValueInternal_iface );
}

static HRESULT WINAPI json_value_internal_QueryInterface( IJsonValueInternal *iface, REFIID iid, void **out )
{
    struct json_value *impl = impl_from_IJsonValueInternal( iface );
    return IJsonValue_QueryInterface( &impl->IJsonValue_iface, iid, out );
}

static ULONG WINAPI json_value_internal_AddRef( IJsonValueInternal *iface )
{
    struct json_value *impl = impl_from_IJsonValueInternal( iface );
    return IJsonValue_AddRef( &impl->IJsonValue_iface );
}

static ULONG WINAPI json_value_internal_Release( IJsonValueInternal *iface )
{
    struct json_value *impl = impl_from_IJsonValueInternal( iface );
    return IJsonValue_Release( &impl->IJsonValue_iface );
}

static cJSON * WINAPI json_value_internal_GetNode( IJsonValueInternal *iface )
{
    struct json_value *impl = impl_from_IJsonValueInternal( iface );
    return impl->node;
}

static const struct IJsonValueInternalVtbl json_value_internal_vtbl =
{
    json_value_internal_QueryInterface,
    json_value_internal_AddRef,
    json_value_internal_Release,
    json_value_internal_GetNode,
};

static HRESULT WINAPI json_value_QueryInterface( IJsonValue *iface, REFIID iid, void **out )
{
    struct json_value *impl = impl_from_IJsonValue( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IJsonValue ))
    {
        *out = &impl->IJsonValue_iface;
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

static ULONG WINAPI json_value_AddRef( IJsonValue *iface )
{
    struct json_value *impl = impl_from_IJsonValue( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p increasing refcount to %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI json_value_Release( IJsonValue *iface )
{
    struct json_value *impl = impl_from_IJsonValue( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    TRACE( "iface %p decreasing refcount to %lu.\n", iface, ref );

    if (!ref)
    {
        if (impl->str_cache)
            WindowsDeleteString( impl->str_cache );
        json_root_release( impl->root );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI json_value_GetIids( IJsonValue *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI json_value_GetRuntimeClassName( IJsonValue *iface, HSTRING *class_name )
{
    FIXME( "iface %p, class_name %p stub!\n", iface, class_name );
    return E_NOTIMPL;
}

static HRESULT WINAPI json_value_GetTrustLevel( IJsonValue *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI json_value_get_ValueType( IJsonValue *iface, JsonValueType *value )
{
    struct json_value *impl = impl_from_IJsonValue( iface );

    TRACE( "iface %p, value %p\n", iface, value );

    if (!value) return E_POINTER;

    *value = get_json_value_type( impl->node );
    return S_OK;
}

static HRESULT WINAPI json_value_Stringify( IJsonValue *iface, HSTRING *value )
{
    struct json_value *impl = impl_from_IJsonValue( iface );
    char *printed;
    HRESULT hr;

    TRACE( "iface %p, value %p\n", iface, value );

    if (!value) return E_POINTER;
    *value = NULL;
    if (!impl->node) return E_FAIL;

    printed = cJSON_PrintUnformatted( impl->node );
    if (!printed) return E_OUTOFMEMORY;
    hr = hstring_from_utf8( printed, value );
    cJSON_free( printed );
    return hr;
}

static HRESULT WINAPI json_value_GetString( IJsonValue *iface, HSTRING *value )
{
    struct json_value *impl = impl_from_IJsonValue( iface );

    TRACE( "iface %p, value %p\n", iface, value );

    if (!impl->node) return E_FAIL;
    if (!(impl->node->type & (cJSON_String | cJSON_Object | cJSON_Array))) return E_ILLEGAL_METHOD_CALL;
    if (!value) return E_POINTER;
    *value = NULL;

    if (impl->node->type & cJSON_String)
    {
        if (!impl->str_cache)
        {
            int len = impl->node->valueint > 0 ? impl->node->valueint : strlen( impl->node->valuestring );
            HRESULT hr = hstring_from_utf8_len( impl->node->valuestring, len, &impl->str_cache );
            if (FAILED(hr)) return hr;
        }
        return WindowsDuplicateString( impl->str_cache, value );
    }
    else
    {
        if (!impl->str_cache)
        {
            char *printed = cJSON_PrintUnformatted( impl->node );
            HRESULT hr;
            if (!printed) return E_OUTOFMEMORY;
            hr = hstring_from_utf8( printed, &impl->str_cache );
            cJSON_free( printed );
            if (FAILED(hr)) return hr;
        }
        return WindowsDuplicateString( impl->str_cache, value );
    }
}

static HRESULT WINAPI json_value_GetNumber( IJsonValue *iface, DOUBLE *value )
{
    struct json_value *impl = impl_from_IJsonValue( iface );

    TRACE( "iface %p, value %p\n", iface, value );

    if (!impl->node || !(impl->node->type & cJSON_Number)) return E_ILLEGAL_METHOD_CALL;
    if (!value) return E_POINTER;

    *value = impl->node->valuedouble;
    return S_OK;
}

static HRESULT WINAPI json_value_GetBoolean( IJsonValue *iface, boolean *value )
{
    struct json_value *impl = impl_from_IJsonValue( iface );

    TRACE( "iface %p, value %p\n", iface, value );

    if (!impl->node || !(impl->node->type & (cJSON_False | cJSON_True))) return E_ILLEGAL_METHOD_CALL;
    if (!value) return E_POINTER;

    *value = (impl->node->type & cJSON_True) ? TRUE : FALSE;
    return S_OK;
}

static HRESULT WINAPI json_value_GetArray( IJsonValue *iface, IJsonArray **value )
{
    struct json_value *impl = impl_from_IJsonValue( iface );

    TRACE( "iface %p, value %p\n", iface, value );

    if (!value) return E_POINTER;
    *value = NULL;
    if (!impl->node || !(impl->node->type & cJSON_Array)) return E_ILLEGAL_METHOD_CALL;

    return create_json_array( impl->node, impl->root, value );
}

static HRESULT WINAPI json_value_GetObject( IJsonValue *iface, IJsonObject **value )
{
    struct json_value *impl = impl_from_IJsonValue( iface );

    TRACE( "iface %p, value %p\n", iface, value );

    if (!value) return E_POINTER;
    *value = NULL;
    if (!impl->node || !(impl->node->type & cJSON_Object)) return E_ILLEGAL_METHOD_CALL;

    return create_json_object( impl->node, impl->root, value );
}

static const struct IJsonValueVtbl json_value_vtbl =
{
    json_value_QueryInterface,
    json_value_AddRef,
    json_value_Release,
    json_value_GetIids,
    json_value_GetRuntimeClassName,
    json_value_GetTrustLevel,
    json_value_get_ValueType,
    json_value_Stringify,
    json_value_GetString,
    json_value_GetNumber,
    json_value_GetBoolean,
    json_value_GetArray,
    json_value_GetObject,
};

HRESULT create_json_value( cJSON *node, struct json_root *root, IJsonValue **out )
{
    struct json_value *impl;

    if (!out) return E_POINTER;
    *out = NULL;
    if (!node) return E_FAIL;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->IJsonValue_iface.lpVtbl = &json_value_vtbl;
    impl->IJsonValueInternal_iface.lpVtbl = &json_value_internal_vtbl;
    impl->ref = 1;
    impl->node = node;
    impl->root = root;
    json_root_addref( root );

    *out = &impl->IJsonValue_iface;
    return S_OK;
}

cJSON *cjson_node_duplicate_from_IJsonValue( IJsonValue *value )
{
    IJsonValueInternal *internal = NULL;
    cJSON *result = NULL;
    JsonValueType type;

    if (!value) return cJSON_CreateNull();

    if (SUCCEEDED(IJsonValue_QueryInterface( value, &IID_IJsonValueInternal, (void **)&internal )))
    {
        cJSON *node = internal->lpVtbl->GetNode( internal );
        result = cJSON_Duplicate( node, 1 );
        internal->lpVtbl->Release( internal );
        return result;
    }

    if (FAILED(IJsonValue_get_ValueType( value, &type ))) return NULL;
    switch (type)
    {
        case JsonValueType_Null:
            return cJSON_CreateNull();
        case JsonValueType_Boolean:
        {
            boolean b = FALSE;
            IJsonValue_GetBoolean( value, &b );
            return cJSON_CreateBool( b );
        }
        case JsonValueType_Number:
        {
            DOUBLE num = 0;
            IJsonValue_GetNumber( value, &num );
            return cJSON_CreateNumber( num );
        }
        case JsonValueType_String:
        {
            HSTRING str = NULL;
            char *utf8;
            int utf8_len = 0;
            IJsonValue_GetString( value, &str );
            utf8 = utf8_from_hstring( str, &utf8_len );
            WindowsDeleteString( str );
            result = cJSON_CreateString( utf8 ? utf8 : "" );
            if (result && utf8) result->valueint = utf8_len;
            free( utf8 );
            return result;
        }
        case JsonValueType_Array:
        case JsonValueType_Object:
        {
            HSTRING str = NULL;
            char *utf8;
            int utf8_len = 0;
            if (SUCCEEDED(IJsonValue_Stringify( value, &str )) || SUCCEEDED(IJsonValue_GetString( value, &str )))
            {
                utf8 = utf8_from_hstring( str, &utf8_len );
                WindowsDeleteString( str );
                if (utf8)
                {
                    result = cJSON_ParseWithLength( utf8, utf8_len );
                    free( utf8 );
                }
            }
            return result;
        }
    }
    return NULL;
}

DEFINE_IINSPECTABLE( json_value_statics, IJsonValueStatics, struct json_value_statics, IActivationFactory_iface )

static HRESULT WINAPI json_value_statics_Parse( IJsonValueStatics *iface, HSTRING input, IJsonValue **value )
{
    cJSON *tree;
    struct json_root *root;
    char *utf8;
    int utf8_len = 0;
    const char *parse_end = NULL;
    HRESULT hr;

    TRACE( "iface %p, input %s, value %p\n", iface, debugstr_hstring( input ), value );

    if (!value) return E_POINTER;
    *value = NULL;
    if (!input) return WEB_E_INVALID_JSON_STRING;

    utf8 = utf8_from_hstring( input, &utf8_len );
    if (!utf8) return E_OUTOFMEMORY;
    if (utf8_len == 0)
    {
        free( utf8 );
        return WEB_E_INVALID_JSON_STRING;
    }

    errno = 0;
    tree = cJSON_ParseWithLengthOpts( utf8, utf8_len + 1, &parse_end, 1 );
    if (!tree)
    {
        int err = errno;
        free( utf8 );
        if (err == ERANGE) return WEB_E_INVALID_JSON_NUMBER;
        return WEB_E_INVALID_JSON_STRING;
    }
    free( utf8 );

    root = json_root_create( tree );
    if (!root)
    {
        cJSON_Delete( tree );
        return E_OUTOFMEMORY;
    }

    hr = create_json_value( tree, root, value );
    json_root_release( root );
    if (SUCCEEDED(hr))
        TRACE( "created IJsonValue %p.\n", *value );

    return hr;
}

static HRESULT WINAPI json_value_statics_TryParse( IJsonValueStatics *iface, HSTRING input, IJsonValue **result, boolean *succeeded )
{
    HRESULT hr;

    TRACE( "iface %p, input %s, result %p, succeeded %p\n", iface, debugstr_hstring( input ), result, succeeded );

    if (!succeeded) return E_POINTER;
    if (!result) return E_POINTER;

    *succeeded = FALSE;
    *result = NULL;

    hr = json_value_statics_Parse( iface, input, result );
    if (SUCCEEDED(hr))
    {
        *succeeded = TRUE;
        return S_OK;
    }

    return S_OK;
}

static HRESULT WINAPI json_value_statics_CreateBooleanValue( IJsonValueStatics *iface, boolean input, IJsonValue **value )
{
    cJSON *node;
    struct json_root *root;
    HRESULT hr;

    TRACE( "iface %p, input %d, value %p\n", iface, input, value );

    if (!value) return E_POINTER;
    *value = NULL;

    node = cJSON_CreateBool( input != FALSE );
    if (!node) return E_OUTOFMEMORY;

    root = json_root_create( node );
    if (!root)
    {
        cJSON_Delete( node );
        return E_OUTOFMEMORY;
    }

    hr = create_json_value( node, root, value );
    json_root_release( root );
    return hr;
}

static HRESULT WINAPI json_value_statics_CreateNumberValue( IJsonValueStatics *iface, DOUBLE input, IJsonValue **value )
{
    cJSON *node;
    struct json_root *root;
    HRESULT hr;

    TRACE( "iface %p, input %f, value %p\n", iface, input, value );

    if (!value) return E_POINTER;
    *value = NULL;

    node = cJSON_CreateNumber( input );
    if (!node) return E_OUTOFMEMORY;

    root = json_root_create( node );
    if (!root)
    {
        cJSON_Delete( node );
        return E_OUTOFMEMORY;
    }

    hr = create_json_value( node, root, value );
    json_root_release( root );
    return hr;
}

static HRESULT WINAPI json_value_statics_CreateStringValue( IJsonValueStatics *iface, HSTRING input, IJsonValue **value )
{
    cJSON *node;
    struct json_root *root;
    char *utf8;
    int utf8_len = 0;
    HRESULT hr;

    TRACE( "iface %p, input %s, value %p\n", iface, debugstr_hstring( input ), value );

    if (!value) return E_POINTER;
    *value = NULL;

    utf8 = utf8_from_hstring( input, &utf8_len );
    node = cJSON_CreateString( utf8 ? utf8 : "" );
    if (node && utf8) node->valueint = utf8_len;
    free( utf8 );
    if (!node) return E_OUTOFMEMORY;

    root = json_root_create( node );
    if (!root)
    {
        cJSON_Delete( node );
        return E_OUTOFMEMORY;
    }

    hr = create_json_value( node, root, value );
    json_root_release( root );
    if (SUCCEEDED(hr))
    {
        struct json_value *impl = impl_from_IJsonValue( *value );
        WindowsDuplicateString( input, &impl->str_cache );
    }
    return hr;
}

static const struct IJsonValueStaticsVtbl json_value_statics_vtbl =
{
    json_value_statics_QueryInterface,
    json_value_statics_AddRef,
    json_value_statics_Release,
    json_value_statics_GetIids,
    json_value_statics_GetRuntimeClassName,
    json_value_statics_GetTrustLevel,
    json_value_statics_Parse,
    json_value_statics_TryParse,
    json_value_statics_CreateBooleanValue,
    json_value_statics_CreateNumberValue,
    json_value_statics_CreateStringValue,
};

static struct json_value_statics json_value_statics =
{
    {&factory_vtbl},
    {&json_value_statics_vtbl},
    1,
};

IActivationFactory *json_value_factory = &json_value_statics.IActivationFactory_iface;
