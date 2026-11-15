#include "private.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(web);

struct json_object
{
    IJsonObject IJsonObject_iface;
    IJsonValueInternal IJsonValueInternal_iface;
    LONG ref;

    cJSON *node;
    struct json_root *root;
    HSTRING str_cache;
};

static inline struct json_object *impl_from_IJsonObject( IJsonObject *iface )
{
    return CONTAINING_RECORD( iface, struct json_object, IJsonObject_iface );
}

static inline struct json_object *impl_from_IJsonValueInternal( IJsonValueInternal *iface )
{
    return CONTAINING_RECORD( iface, struct json_object, IJsonValueInternal_iface );
}

static HRESULT WINAPI json_object_internal_QueryInterface( IJsonValueInternal *iface, REFIID iid, void **out )
{
    struct json_object *impl = impl_from_IJsonValueInternal( iface );
    return IJsonObject_QueryInterface( &impl->IJsonObject_iface, iid, out );
}

static ULONG WINAPI json_object_internal_AddRef( IJsonValueInternal *iface )
{
    struct json_object *impl = impl_from_IJsonValueInternal( iface );
    return IJsonObject_AddRef( &impl->IJsonObject_iface );
}

static ULONG WINAPI json_object_internal_Release( IJsonValueInternal *iface )
{
    struct json_object *impl = impl_from_IJsonValueInternal( iface );
    return IJsonObject_Release( &impl->IJsonObject_iface );
}

static cJSON * WINAPI json_object_internal_GetNode( IJsonValueInternal *iface )
{
    struct json_object *impl = impl_from_IJsonValueInternal( iface );
    return impl->node;
}

static const struct IJsonValueInternalVtbl json_object_internal_vtbl =
{
    json_object_internal_QueryInterface,
    json_object_internal_AddRef,
    json_object_internal_Release,
    json_object_internal_GetNode,
};

static HRESULT WINAPI json_object_QueryInterface( IJsonObject *iface, REFIID iid, void **out )
{
    struct json_object *impl = impl_from_IJsonObject( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IJsonObject ))
    {
        *out = &impl->IJsonObject_iface;
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

static ULONG WINAPI json_object_AddRef( IJsonObject *iface )
{
    struct json_object *impl = impl_from_IJsonObject( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p, ref %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI json_object_Release( IJsonObject *iface )
{
    struct json_object *impl = impl_from_IJsonObject( iface );
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

static HRESULT WINAPI json_object_GetIids( IJsonObject *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI json_object_GetRuntimeClassName( IJsonObject *iface, HSTRING *class_name )
{
    FIXME( "iface %p, class_name %p stub!\n", iface, class_name );
    return E_NOTIMPL;
}

static HRESULT WINAPI json_object_GetTrustLevel( IJsonObject *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI json_object_GetNamedValue( IJsonObject *iface, HSTRING name, IJsonValue **value )
{
    struct json_object *impl = impl_from_IJsonObject( iface );
    char *name_utf8;
    cJSON *child;

    TRACE( "iface %p, name %s, value %p.\n", iface, debugstr_hstring( name ), value );

    if (!value) return E_POINTER;
    *value = NULL;
    if (!name) return WEB_E_JSON_VALUE_NOT_FOUND;

    name_utf8 = utf8_from_hstring( name, NULL );
    child = cJSON_GetObjectItemCaseSensitive( impl->node, name_utf8 );
    free( name_utf8 );

    if (!child) return WEB_E_JSON_VALUE_NOT_FOUND;

    return create_json_value( child, impl->root, value );
}

static HRESULT WINAPI json_object_SetNamedValue( IJsonObject *iface, HSTRING name, IJsonValue *value )
{
    struct json_object *impl = impl_from_IJsonObject( iface );
    char *name_utf8;
    cJSON *new_node;

    TRACE( "iface %p, name %s, value %p.\n", iface, debugstr_hstring( name ), value );

    if (!name) return E_INVALIDARG;

    name_utf8 = utf8_from_hstring( name, NULL );
    if (!name_utf8) return E_OUTOFMEMORY;

    new_node = cjson_node_duplicate_from_IJsonValue( value );
    if (!new_node)
    {
        free( name_utf8 );
        return E_OUTOFMEMORY;
    }

    if (cJSON_GetObjectItemCaseSensitive( impl->node, name_utf8 ))
        cJSON_ReplaceItemInObjectCaseSensitive( impl->node, name_utf8, new_node );
    else
        cJSON_AddItemToObject( impl->node, name_utf8, new_node );

    free( name_utf8 );
    if (impl->str_cache)
    {
        WindowsDeleteString( impl->str_cache );
        impl->str_cache = NULL;
    }
    return S_OK;
}

static HRESULT WINAPI json_object_GetNamedObject( IJsonObject *iface, HSTRING name, IJsonObject **value )
{
    struct json_object *impl = impl_from_IJsonObject( iface );
    char *name_utf8;
    cJSON *child;

    TRACE( "iface %p, name %s, value %p.\n", iface, debugstr_hstring( name ), value );

    if (!value) return E_POINTER;
    *value = NULL;
    if (!name) return WEB_E_JSON_VALUE_NOT_FOUND;

    name_utf8 = utf8_from_hstring( name, NULL );
    child = cJSON_GetObjectItemCaseSensitive( impl->node, name_utf8 );
    free( name_utf8 );

    if (!child) return WEB_E_JSON_VALUE_NOT_FOUND;
    if (!(child->type & cJSON_Object)) return E_ILLEGAL_METHOD_CALL;

    return create_json_object( child, impl->root, value );
}

static HRESULT WINAPI json_object_GetNamedArray( IJsonObject *iface, HSTRING name, IJsonArray **value )
{
    struct json_object *impl = impl_from_IJsonObject( iface );
    char *name_utf8;
    cJSON *child;

    TRACE( "iface %p, name %s, value %p.\n", iface, debugstr_hstring( name ), value );

    if (!value) return E_POINTER;
    *value = NULL;
    if (!name) return WEB_E_JSON_VALUE_NOT_FOUND;

    name_utf8 = utf8_from_hstring( name, NULL );
    child = cJSON_GetObjectItemCaseSensitive( impl->node, name_utf8 );
    free( name_utf8 );

    if (!child) return WEB_E_JSON_VALUE_NOT_FOUND;
    if (!(child->type & cJSON_Array)) return E_ILLEGAL_METHOD_CALL;

    return create_json_array( child, impl->root, value );
}

static HRESULT WINAPI json_object_GetNamedString( IJsonObject *iface, HSTRING name, HSTRING *value )
{
    struct json_object *impl = impl_from_IJsonObject( iface );
    char *name_utf8;
    cJSON *child;
    int len;

    TRACE( "iface %p, name %s, value %p.\n", iface, debugstr_hstring( name ), value );

    if (!value) return E_POINTER;
    *value = NULL;
    if (!name) return WEB_E_JSON_VALUE_NOT_FOUND;

    name_utf8 = utf8_from_hstring( name, NULL );
    child = cJSON_GetObjectItemCaseSensitive( impl->node, name_utf8 );
    free( name_utf8 );

    if (!child) return WEB_E_JSON_VALUE_NOT_FOUND;
    if (!(child->type & cJSON_String)) return E_ILLEGAL_METHOD_CALL;

    len = child->valueint > 0 ? child->valueint : strlen( child->valuestring );
    return hstring_from_utf8_len( child->valuestring, len, value );
}

static HRESULT WINAPI json_object_GetNamedNumber( IJsonObject *iface, HSTRING name, DOUBLE *value )
{
    struct json_object *impl = impl_from_IJsonObject( iface );
    char *name_utf8;
    cJSON *child;

    TRACE( "iface %p, name %s, value %p.\n", iface, debugstr_hstring( name ), value );

    if (!value) return E_POINTER;
    if (!name) return WEB_E_JSON_VALUE_NOT_FOUND;

    name_utf8 = utf8_from_hstring( name, NULL );
    child = cJSON_GetObjectItemCaseSensitive( impl->node, name_utf8 );
    free( name_utf8 );

    if (!child) return WEB_E_JSON_VALUE_NOT_FOUND;
    if (!(child->type & cJSON_Number)) return E_ILLEGAL_METHOD_CALL;

    *value = child->valuedouble;
    return S_OK;
}

static HRESULT WINAPI json_object_GetNamedBoolean( IJsonObject *iface, HSTRING name, boolean *value )
{
    struct json_object *impl = impl_from_IJsonObject( iface );
    char *name_utf8;
    cJSON *child;

    TRACE( "iface %p, name %s, value %p.\n", iface, debugstr_hstring( name ), value );

    if (!value) return E_POINTER;
    if (!name) return WEB_E_JSON_VALUE_NOT_FOUND;

    name_utf8 = utf8_from_hstring( name, NULL );
    child = cJSON_GetObjectItemCaseSensitive( impl->node, name_utf8 );
    free( name_utf8 );

    if (!child) return WEB_E_JSON_VALUE_NOT_FOUND;
    if (!(child->type & (cJSON_False | cJSON_True))) return E_ILLEGAL_METHOD_CALL;

    *value = (child->type & cJSON_True) ? TRUE : FALSE;
    return S_OK;
}

static const struct IJsonObjectVtbl json_object_vtbl =
{
    json_object_QueryInterface,
    json_object_AddRef,
    json_object_Release,
    json_object_GetIids,
    json_object_GetRuntimeClassName,
    json_object_GetTrustLevel,
    json_object_GetNamedValue,
    json_object_SetNamedValue,
    json_object_GetNamedObject,
    json_object_GetNamedArray,
    json_object_GetNamedString,
    json_object_GetNamedNumber,
    json_object_GetNamedBoolean,
};

HRESULT create_json_object( cJSON *node, struct json_root *root, IJsonObject **out )
{
    struct json_object *impl;

    if (!out) return E_POINTER;
    *out = NULL;
    if (!node) return E_FAIL;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->IJsonObject_iface.lpVtbl = &json_object_vtbl;
    impl->IJsonValueInternal_iface.lpVtbl = &json_object_internal_vtbl;
    impl->ref = 1;
    impl->node = node;
    impl->root = root;
    json_root_addref( root );

    *out = &impl->IJsonObject_iface;
    return S_OK;
}

struct json_object_statics
{
    IActivationFactory IActivationFactory_iface;
    LONG ref;
};

static inline struct json_object_statics *impl_from_IActivationFactory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct json_object_statics, IActivationFactory_iface );
}

static HRESULT WINAPI factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct json_object_statics *impl = impl_from_IActivationFactory( iface );

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
    struct json_object_statics *impl = impl_from_IActivationFactory( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p increasing refcount to %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI factory_Release( IActivationFactory *iface )
{
    struct json_object_statics *impl = impl_from_IActivationFactory( iface );
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
    cJSON *node;
    struct json_root *root;
    IJsonObject *obj;
    HRESULT hr;

    TRACE( "iface %p, instance %p.\n", iface, instance );

    if (!instance) return E_POINTER;
    *instance = NULL;

    node = cJSON_CreateObject();
    if (!node) return E_OUTOFMEMORY;

    root = json_root_create( node );
    if (!root)
    {
        cJSON_Delete( node );
        return E_OUTOFMEMORY;
    }

    hr = create_json_object( node, root, &obj );
    json_root_release( root );
    if (FAILED(hr)) return hr;

    *instance = (IInspectable *)obj;
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

static struct json_object_statics json_object_statics =
{
    {&factory_vtbl},
    1,
};

IActivationFactory *json_object_factory = &json_object_statics.IActivationFactory_iface;
