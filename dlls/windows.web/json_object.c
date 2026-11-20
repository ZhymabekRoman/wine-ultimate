#include "private.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(web);

static const GUID IID_IJsonObjectWithDefaultValues_local =
    {0xd960d2a2, 0xb7f0, 0x4f00, {0x8e, 0x44, 0xd8, 0x2c, 0xf4, 0x15, 0xea, 0x13}};

typedef struct IJsonObjectWithDefaultValues_local IJsonObjectWithDefaultValues_local;
typedef struct IJsonObjectWithDefaultValues_localVtbl
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IJsonObjectWithDefaultValues_local *iface, REFIID iid, void **out);
    ULONG (STDMETHODCALLTYPE *AddRef)(IJsonObjectWithDefaultValues_local *iface);
    ULONG (STDMETHODCALLTYPE *Release)(IJsonObjectWithDefaultValues_local *iface);
    HRESULT (STDMETHODCALLTYPE *GetIids)(IJsonObjectWithDefaultValues_local *iface, ULONG *count, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(IJsonObjectWithDefaultValues_local *iface, HSTRING *name);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(IJsonObjectWithDefaultValues_local *iface, TrustLevel *level);
    HRESULT (STDMETHODCALLTYPE *GetNamedValue)(IJsonObjectWithDefaultValues_local *iface, HSTRING name, IJsonValue *def, IJsonValue **out);
    HRESULT (STDMETHODCALLTYPE *GetNamedObject)(IJsonObjectWithDefaultValues_local *iface, HSTRING name, IJsonObject *def, IJsonObject **out);
    HRESULT (STDMETHODCALLTYPE *GetNamedString)(IJsonObjectWithDefaultValues_local *iface, HSTRING name, HSTRING def, HSTRING *out);
    HRESULT (STDMETHODCALLTYPE *GetNamedArray)(IJsonObjectWithDefaultValues_local *iface, HSTRING name, IJsonArray *def, IJsonArray **out);
    HRESULT (STDMETHODCALLTYPE *GetNamedNumber)(IJsonObjectWithDefaultValues_local *iface, HSTRING name, DOUBLE def, DOUBLE *out);
    HRESULT (STDMETHODCALLTYPE *GetNamedBoolean)(IJsonObjectWithDefaultValues_local *iface, HSTRING name, boolean def, boolean *out);
} IJsonObjectWithDefaultValues_localVtbl;

struct IJsonObjectWithDefaultValues_local
{
    const IJsonObjectWithDefaultValues_localVtbl *lpVtbl;
};

struct json_object
{
    IJsonObject IJsonObject_iface;
    IJsonValueInternal IJsonValueInternal_iface;
    IJsonObjectWithDefaultValues_local IJsonObjectWithDefaultValues_iface;
    __FIMap_2_HSTRING_Windows__CData__CJson__CIJsonValue IMap_iface;
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

    if (IsEqualGUID( iid, &IID_IJsonObjectWithDefaultValues_local ))
    {
        *out = &impl->IJsonObjectWithDefaultValues_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID___FIMap_2_HSTRING_Windows__CData__CJson__CIJsonValue ))
    {
        *out = &impl->IMap_iface;
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

static inline struct json_object *impl_from_IJsonObjectWithDefaultValues( IJsonObjectWithDefaultValues_local *iface )
{
    return CONTAINING_RECORD( iface, struct json_object, IJsonObjectWithDefaultValues_iface );
}

static HRESULT WINAPI json_obj_def_QueryInterface( IJsonObjectWithDefaultValues_local *iface, REFIID iid, void **out )
{
    return IJsonObject_QueryInterface( &impl_from_IJsonObjectWithDefaultValues( iface )->IJsonObject_iface, iid, out );
}

static ULONG WINAPI json_obj_def_AddRef( IJsonObjectWithDefaultValues_local *iface )
{
    return IJsonObject_AddRef( &impl_from_IJsonObjectWithDefaultValues( iface )->IJsonObject_iface );
}

static ULONG WINAPI json_obj_def_Release( IJsonObjectWithDefaultValues_local *iface )
{
    return IJsonObject_Release( &impl_from_IJsonObjectWithDefaultValues( iface )->IJsonObject_iface );
}

static HRESULT WINAPI json_obj_def_GetIids( IJsonObjectWithDefaultValues_local *iface, ULONG *count, IID **iids )
{
    return IJsonObject_GetIids( &impl_from_IJsonObjectWithDefaultValues( iface )->IJsonObject_iface, count, iids );
}

static HRESULT WINAPI json_obj_def_GetRuntimeClassName( IJsonObjectWithDefaultValues_local *iface, HSTRING *name )
{
    return IJsonObject_GetRuntimeClassName( &impl_from_IJsonObjectWithDefaultValues( iface )->IJsonObject_iface, name );
}

static HRESULT WINAPI json_obj_def_GetTrustLevel( IJsonObjectWithDefaultValues_local *iface, TrustLevel *level )
{
    return IJsonObject_GetTrustLevel( &impl_from_IJsonObjectWithDefaultValues( iface )->IJsonObject_iface, level );
}

static HRESULT WINAPI json_obj_def_GetNamedValue( IJsonObjectWithDefaultValues_local *iface, HSTRING name, IJsonValue *def, IJsonValue **out )
{
    struct json_object *impl = impl_from_IJsonObjectWithDefaultValues( iface );
    if (SUCCEEDED(json_object_GetNamedValue( &impl->IJsonObject_iface, name, out )))
        return S_OK;
    *out = def;
    if (def) IJsonValue_AddRef( def );
    return S_OK;
}

static HRESULT WINAPI json_obj_def_GetNamedObject( IJsonObjectWithDefaultValues_local *iface, HSTRING name, IJsonObject *def, IJsonObject **out )
{
    struct json_object *impl = impl_from_IJsonObjectWithDefaultValues( iface );
    if (SUCCEEDED(json_object_GetNamedObject( &impl->IJsonObject_iface, name, out )))
        return S_OK;
    *out = def;
    if (def) IJsonObject_AddRef( def );
    return S_OK;
}

static HRESULT WINAPI json_obj_def_GetNamedString( IJsonObjectWithDefaultValues_local *iface, HSTRING name, HSTRING def, HSTRING *out )
{
    struct json_object *impl = impl_from_IJsonObjectWithDefaultValues( iface );
    if (SUCCEEDED(json_object_GetNamedString( &impl->IJsonObject_iface, name, out )))
        return S_OK;
    return WindowsDuplicateString( def, out );
}

static HRESULT WINAPI json_obj_def_GetNamedArray( IJsonObjectWithDefaultValues_local *iface, HSTRING name, IJsonArray *def, IJsonArray **out )
{
    struct json_object *impl = impl_from_IJsonObjectWithDefaultValues( iface );
    if (SUCCEEDED(json_object_GetNamedArray( &impl->IJsonObject_iface, name, out )))
        return S_OK;
    *out = def;
    if (def) IJsonArray_AddRef( def );
    return S_OK;
}

static HRESULT WINAPI json_obj_def_GetNamedNumber( IJsonObjectWithDefaultValues_local *iface, HSTRING name, DOUBLE def, DOUBLE *out )
{
    struct json_object *impl = impl_from_IJsonObjectWithDefaultValues( iface );
    if (SUCCEEDED(json_object_GetNamedNumber( &impl->IJsonObject_iface, name, out )))
        return S_OK;
    *out = def;
    return S_OK;
}

static HRESULT WINAPI json_obj_def_GetNamedBoolean( IJsonObjectWithDefaultValues_local *iface, HSTRING name, boolean def, boolean *out )
{
    struct json_object *impl = impl_from_IJsonObjectWithDefaultValues( iface );
    if (SUCCEEDED(json_object_GetNamedBoolean( &impl->IJsonObject_iface, name, out )))
        return S_OK;
    *out = def;
    return S_OK;
}

static const IJsonObjectWithDefaultValues_localVtbl json_object_def_vtbl =
{
    json_obj_def_QueryInterface,
    json_obj_def_AddRef,
    json_obj_def_Release,
    json_obj_def_GetIids,
    json_obj_def_GetRuntimeClassName,
    json_obj_def_GetTrustLevel,
    json_obj_def_GetNamedValue,
    json_obj_def_GetNamedObject,
    json_obj_def_GetNamedString,
    json_obj_def_GetNamedArray,
    json_obj_def_GetNamedNumber,
    json_obj_def_GetNamedBoolean,
};

static inline struct json_object *impl_from_IMap( __FIMap_2_HSTRING_Windows__CData__CJson__CIJsonValue *iface )
{
    return CONTAINING_RECORD( iface, struct json_object, IMap_iface );
}

static HRESULT WINAPI json_obj_map_QueryInterface( __FIMap_2_HSTRING_Windows__CData__CJson__CIJsonValue *iface, REFIID iid, void **out )
{
    return IJsonObject_QueryInterface( &impl_from_IMap( iface )->IJsonObject_iface, iid, out );
}

static ULONG WINAPI json_obj_map_AddRef( __FIMap_2_HSTRING_Windows__CData__CJson__CIJsonValue *iface )
{
    return IJsonObject_AddRef( &impl_from_IMap( iface )->IJsonObject_iface );
}

static ULONG WINAPI json_obj_map_Release( __FIMap_2_HSTRING_Windows__CData__CJson__CIJsonValue *iface )
{
    return IJsonObject_Release( &impl_from_IMap( iface )->IJsonObject_iface );
}

static HRESULT WINAPI json_obj_map_GetIids( __FIMap_2_HSTRING_Windows__CData__CJson__CIJsonValue *iface, ULONG *count, IID **iids )
{
    return IJsonObject_GetIids( &impl_from_IMap( iface )->IJsonObject_iface, count, iids );
}

static HRESULT WINAPI json_obj_map_GetRuntimeClassName( __FIMap_2_HSTRING_Windows__CData__CJson__CIJsonValue *iface, HSTRING *name )
{
    return IJsonObject_GetRuntimeClassName( &impl_from_IMap( iface )->IJsonObject_iface, name );
}

static HRESULT WINAPI json_obj_map_GetTrustLevel( __FIMap_2_HSTRING_Windows__CData__CJson__CIJsonValue *iface, TrustLevel *level )
{
    return IJsonObject_GetTrustLevel( &impl_from_IMap( iface )->IJsonObject_iface, level );
}

static HRESULT WINAPI json_obj_map_Lookup( __FIMap_2_HSTRING_Windows__CData__CJson__CIJsonValue *iface, HSTRING key, IJsonValue **value )
{
    return json_object_GetNamedValue( &impl_from_IMap( iface )->IJsonObject_iface, key, value );
}

static HRESULT WINAPI json_obj_map_get_Size( __FIMap_2_HSTRING_Windows__CData__CJson__CIJsonValue *iface, unsigned int *size )
{
    struct json_object *impl = impl_from_IMap( iface );
    if (!size) return E_POINTER;
    *size = (unsigned int)cJSON_GetArraySize( impl->node );
    return S_OK;
}

static HRESULT WINAPI json_obj_map_HasKey( __FIMap_2_HSTRING_Windows__CData__CJson__CIJsonValue *iface, HSTRING key, boolean *found )
{
    IJsonValue *val = NULL;
    if (!found) return E_POINTER;
    if (SUCCEEDED(json_object_GetNamedValue( &impl_from_IMap( iface )->IJsonObject_iface, key, &val )))
    {
        if (val) IJsonValue_Release( val );
        *found = TRUE;
    }
    else
        *found = FALSE;
    return S_OK;
}

static HRESULT WINAPI json_obj_map_GetView( __FIMap_2_HSTRING_Windows__CData__CJson__CIJsonValue *iface,
        __FIMapView_2_HSTRING_Windows__CData__CJson__CIJsonValue **view )
{
    if (view) *view = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI json_obj_map_Insert( __FIMap_2_HSTRING_Windows__CData__CJson__CIJsonValue *iface,
        HSTRING key, IJsonValue *value, boolean *replaced )
{
    boolean had = FALSE;
    HRESULT hr;
    json_obj_map_HasKey( iface, key, &had );
    hr = json_object_SetNamedValue( &impl_from_IMap( iface )->IJsonObject_iface, key, value );
    if (replaced) *replaced = had;
    return hr;
}

static HRESULT WINAPI json_obj_map_Remove( __FIMap_2_HSTRING_Windows__CData__CJson__CIJsonValue *iface, HSTRING key )
{
    return S_OK;
}

static HRESULT WINAPI json_obj_map_Clear( __FIMap_2_HSTRING_Windows__CData__CJson__CIJsonValue *iface )
{
    return S_OK;
}

static const __FIMap_2_HSTRING_Windows__CData__CJson__CIJsonValueVtbl json_object_map_vtbl =
{
    json_obj_map_QueryInterface,
    json_obj_map_AddRef,
    json_obj_map_Release,
    json_obj_map_GetIids,
    json_obj_map_GetRuntimeClassName,
    json_obj_map_GetTrustLevel,
    json_obj_map_Lookup,
    json_obj_map_get_Size,
    json_obj_map_HasKey,
    json_obj_map_GetView,
    json_obj_map_Insert,
    json_obj_map_Remove,
    json_obj_map_Clear,
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
    impl->IJsonObjectWithDefaultValues_iface.lpVtbl = &json_object_def_vtbl;
    impl->IMap_iface.lpVtbl = &json_object_map_vtbl;
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
    IJsonObjectStatics IJsonObjectStatics_iface;
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

    if (IsEqualGUID( iid, &IID_IJsonObjectStatics ))
    {
        *out = &impl->IJsonObjectStatics_iface;
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

DEFINE_IINSPECTABLE( json_object_statics, IJsonObjectStatics, struct json_object_statics, IActivationFactory_iface )

static HRESULT WINAPI json_object_statics_Parse( IJsonObjectStatics *iface, HSTRING input, IJsonObject **value )
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

    if (!cJSON_IsObject( tree ))
    {
        cJSON_Delete( tree );
        return WEB_E_INVALID_JSON_STRING;
    }

    root = json_root_create( tree );
    if (!root)
    {
        cJSON_Delete( tree );
        return E_OUTOFMEMORY;
    }

    hr = create_json_object( tree, root, value );
    json_root_release( root );
    return hr;
}

static HRESULT WINAPI json_object_statics_TryParse( IJsonObjectStatics *iface, HSTRING input, IJsonObject **result, boolean *succeeded )
{
    HRESULT hr;

    TRACE( "iface %p, input %s, result %p, succeeded %p\n", iface, debugstr_hstring( input ), result, succeeded );

    if (!succeeded || !result) return E_POINTER;

    *succeeded = FALSE;
    *result = NULL;

    hr = json_object_statics_Parse( iface, input, result );
    if (SUCCEEDED(hr)) *succeeded = TRUE;

    return S_OK;
}

static const struct IJsonObjectStaticsVtbl json_object_statics_vtbl =
{
    json_object_statics_QueryInterface,
    json_object_statics_AddRef,
    json_object_statics_Release,
    /* IInspectable methods */
    json_object_statics_GetIids,
    json_object_statics_GetRuntimeClassName,
    json_object_statics_GetTrustLevel,
    /* IJsonObjectStatics methods */
    json_object_statics_Parse,
    json_object_statics_TryParse,
};

static struct json_object_statics json_object_statics =
{
    {&factory_vtbl},
    {&json_object_statics_vtbl},
    1,
};

IActivationFactory *json_object_factory = &json_object_statics.IActivationFactory_iface;
