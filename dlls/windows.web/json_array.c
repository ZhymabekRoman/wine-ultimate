#include "private.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(web);

struct json_array
{
    IJsonArray IJsonArray_iface;
    IJsonValue IJsonValue_iface;
    IJsonValueInternal IJsonValueInternal_iface;
    IStringable IStringable_iface;
    __FIVector_1_Windows__CData__CJson__CIJsonValue IVector_iface;
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

static inline struct json_array *impl_from_IVector( __FIVector_1_Windows__CData__CJson__CIJsonValue *iface )
{
    return CONTAINING_RECORD( iface, struct json_array, IVector_iface );
}

static HRESULT WINAPI json_array_vec_QueryInterface( __FIVector_1_Windows__CData__CJson__CIJsonValue *iface, REFIID iid, void **out )
{
    struct json_array *impl = impl_from_IVector( iface );
    return IJsonArray_QueryInterface( &impl->IJsonArray_iface, iid, out );
}

static ULONG WINAPI json_array_vec_AddRef( __FIVector_1_Windows__CData__CJson__CIJsonValue *iface )
{
    struct json_array *impl = impl_from_IVector( iface );
    return IJsonArray_AddRef( &impl->IJsonArray_iface );
}

static ULONG WINAPI json_array_vec_Release( __FIVector_1_Windows__CData__CJson__CIJsonValue *iface )
{
    struct json_array *impl = impl_from_IVector( iface );
    return IJsonArray_Release( &impl->IJsonArray_iface );
}

static HRESULT WINAPI json_array_vec_GetIids( __FIVector_1_Windows__CData__CJson__CIJsonValue *iface, ULONG *iid_count, IID **iids )
{
    struct json_array *impl = impl_from_IVector( iface );
    return IJsonArray_GetIids( &impl->IJsonArray_iface, iid_count, iids );
}

static HRESULT WINAPI json_array_vec_GetRuntimeClassName( __FIVector_1_Windows__CData__CJson__CIJsonValue *iface, HSTRING *class_name )
{
    struct json_array *impl = impl_from_IVector( iface );
    return IJsonArray_GetRuntimeClassName( &impl->IJsonArray_iface, class_name );
}

static HRESULT WINAPI json_array_vec_GetTrustLevel( __FIVector_1_Windows__CData__CJson__CIJsonValue *iface, TrustLevel *trust_level )
{
    struct json_array *impl = impl_from_IVector( iface );
    return IJsonArray_GetTrustLevel( &impl->IJsonArray_iface, trust_level );
}

static HRESULT WINAPI json_array_vec_GetAt( __FIVector_1_Windows__CData__CJson__CIJsonValue *iface, UINT32 index, IJsonValue **value )
{
    struct json_array *impl = impl_from_IVector( iface );
    cJSON *child;

    if (!value) return E_POINTER;
    *value = NULL;
    child = cJSON_GetArrayItem( impl->node, (int)index );
    if (!child) return E_BOUNDS;
    return create_json_value( child, impl->root, value );
}

static HRESULT WINAPI json_array_vec_get_Size( __FIVector_1_Windows__CData__CJson__CIJsonValue *iface, UINT32 *value )
{
    struct json_array *impl = impl_from_IVector( iface );
    if (!value) return E_POINTER;
    *value = (UINT32)cJSON_GetArraySize( impl->node );
    return S_OK;
}

static HRESULT WINAPI json_array_vec_GetView( __FIVector_1_Windows__CData__CJson__CIJsonValue *iface,
        __FIVectorView_1_Windows__CData__CJson__CIJsonValue **value )
{
    FIXME( "iface %p, value %p stub!\n", iface, value );
    if (value) *value = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI json_array_vec_IndexOf( __FIVector_1_Windows__CData__CJson__CIJsonValue *iface,
        IJsonValue *element, UINT32 *index, BOOLEAN *value )
{
    if (index) *index = 0;
    if (value) *value = FALSE;
    return S_OK;
}

static HRESULT WINAPI json_array_vec_SetAt( __FIVector_1_Windows__CData__CJson__CIJsonValue *iface, UINT32 index, IJsonValue *value )
{
    FIXME( "iface %p, index %u, value %p stub!\n", iface, index, value );
    return E_NOTIMPL;
}

static HRESULT WINAPI json_array_vec_InsertAt( __FIVector_1_Windows__CData__CJson__CIJsonValue *iface, UINT32 index, IJsonValue *value )
{
    struct json_array *impl = impl_from_IVector( iface );
    return json_array_push( &impl->IJsonArray_iface, value );
}

static HRESULT WINAPI json_array_vec_RemoveAt( __FIVector_1_Windows__CData__CJson__CIJsonValue *iface, UINT32 index )
{
    struct json_array *impl = impl_from_IVector( iface );
    cJSON_DeleteItemFromArray( impl->node, (int)index );
    return S_OK;
}

static HRESULT WINAPI json_array_vec_Append( __FIVector_1_Windows__CData__CJson__CIJsonValue *iface, IJsonValue *value )
{
    struct json_array *impl = impl_from_IVector( iface );
    return json_array_push( &impl->IJsonArray_iface, value );
}

static HRESULT WINAPI json_array_vec_RemoveAtEnd( __FIVector_1_Windows__CData__CJson__CIJsonValue *iface )
{
    struct json_array *impl = impl_from_IVector( iface );
    int sz = cJSON_GetArraySize( impl->node );
    if (sz <= 0) return E_BOUNDS;
    cJSON_DeleteItemFromArray( impl->node, sz - 1 );
    return S_OK;
}

static HRESULT WINAPI json_array_vec_Clear( __FIVector_1_Windows__CData__CJson__CIJsonValue *iface )
{
    struct json_array *impl = impl_from_IVector( iface );
    while (cJSON_GetArraySize( impl->node ) > 0)
        cJSON_DeleteItemFromArray( impl->node, 0 );
    return S_OK;
}

static HRESULT WINAPI json_array_vec_GetMany( __FIVector_1_Windows__CData__CJson__CIJsonValue *iface,
        UINT32 start_index, UINT32 items_size, IJsonValue **items, UINT32 *value )
{
    struct json_array *impl = impl_from_IVector( iface );
    UINT32 count = 0, total = (UINT32)cJSON_GetArraySize( impl->node );

    if (!value) return E_POINTER;
    *value = 0;
    while (count < items_size && start_index + count < total)
    {
        HRESULT hr = json_array_vec_GetAt( iface, start_index + count, &items[count] );
        if (FAILED(hr)) break;
        count++;
    }
    *value = count;
    return S_OK;
}

static HRESULT WINAPI json_array_vec_ReplaceAll( __FIVector_1_Windows__CData__CJson__CIJsonValue *iface,
        UINT32 count, IJsonValue **items )
{
    UINT32 i;
    json_array_vec_Clear( iface );
    for (i = 0; i < count; i++)
        json_array_vec_Append( iface, items[i] );
    return S_OK;
}

static const __FIVector_1_Windows__CData__CJson__CIJsonValueVtbl json_array_vec_vtbl =
{
    json_array_vec_QueryInterface,
    json_array_vec_AddRef,
    json_array_vec_Release,
    json_array_vec_GetIids,
    json_array_vec_GetRuntimeClassName,
    json_array_vec_GetTrustLevel,
    json_array_vec_GetAt,
    json_array_vec_get_Size,
    json_array_vec_GetView,
    json_array_vec_IndexOf,
    json_array_vec_SetAt,
    json_array_vec_InsertAt,
    json_array_vec_RemoveAt,
    json_array_vec_Append,
    json_array_vec_RemoveAtEnd,
    json_array_vec_Clear,
    json_array_vec_GetMany,
    json_array_vec_ReplaceAll,
};

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

    if (IsEqualGUID( iid, &IID_IJsonValue ))
    {
        *out = &impl->IJsonValue_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IStringable ))
    {
        *out = &impl->IStringable_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IJsonValueInternal ))
    {
        *out = &impl->IJsonValueInternal_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID___FIVector_1_Windows__CData__CJson__CIJsonValue ))
    {
        *out = &impl->IVector_iface;
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
    static const WCHAR cls[] = L"Windows.Data.Json.JsonArray";
    if (!class_name) return E_POINTER;
    return WindowsCreateString( cls, ARRAY_SIZE(cls) - 1, class_name );
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

static inline struct json_array *impl_from_IJsonValue( IJsonValue *iface )
{
    return CONTAINING_RECORD( iface, struct json_array, IJsonValue_iface );
}

static HRESULT WINAPI json_arr_val_QueryInterface( IJsonValue *iface, REFIID iid, void **out )
{
    return IJsonArray_QueryInterface( &impl_from_IJsonValue( iface )->IJsonArray_iface, iid, out );
}

static ULONG WINAPI json_arr_val_AddRef( IJsonValue *iface )
{
    return IJsonArray_AddRef( &impl_from_IJsonValue( iface )->IJsonArray_iface );
}

static ULONG WINAPI json_arr_val_Release( IJsonValue *iface )
{
    return IJsonArray_Release( &impl_from_IJsonValue( iface )->IJsonArray_iface );
}

static HRESULT WINAPI json_arr_val_GetIids( IJsonValue *iface, ULONG *count, IID **iids )
{
    return IJsonArray_GetIids( &impl_from_IJsonValue( iface )->IJsonArray_iface, count, iids );
}

static HRESULT WINAPI json_arr_val_GetRuntimeClassName( IJsonValue *iface, HSTRING *name )
{
    return IJsonArray_GetRuntimeClassName( &impl_from_IJsonValue( iface )->IJsonArray_iface, name );
}

static HRESULT WINAPI json_arr_val_GetTrustLevel( IJsonValue *iface, TrustLevel *level )
{
    return IJsonArray_GetTrustLevel( &impl_from_IJsonValue( iface )->IJsonArray_iface, level );
}

static HRESULT WINAPI json_arr_val_get_ValueType( IJsonValue *iface, JsonValueType *value )
{
    if (!value) return E_POINTER;
    *value = JsonValueType_Array;
    return S_OK;
}

static HRESULT WINAPI json_arr_val_Stringify( IJsonValue *iface, HSTRING *value )
{
    struct json_array *impl = impl_from_IJsonValue( iface );
    char *printed;
    HRESULT hr;

    if (!value) return E_POINTER;
    *value = NULL;
    if (!impl->node) return WindowsCreateString( L"[]", 2, value );
    if (!(printed = cJSON_PrintUnformatted( impl->node ))) return E_OUTOFMEMORY;
    hr = hstring_from_utf8( printed, value );
    cJSON_free( printed );
    return hr;
}

static HRESULT WINAPI json_arr_val_GetString( IJsonValue *iface, HSTRING *value )
{
    return E_ILLEGAL_METHOD_CALL;
}

static HRESULT WINAPI json_arr_val_GetNumber( IJsonValue *iface, DOUBLE *value )
{
    return E_ILLEGAL_METHOD_CALL;
}

static HRESULT WINAPI json_arr_val_GetBoolean( IJsonValue *iface, boolean *value )
{
    return E_ILLEGAL_METHOD_CALL;
}

static HRESULT WINAPI json_arr_val_GetArray( IJsonValue *iface, IJsonArray **value )
{
    struct json_array *impl = impl_from_IJsonValue( iface );
    if (!value) return E_POINTER;
    *value = &impl->IJsonArray_iface;
    IJsonArray_AddRef( *value );
    return S_OK;
}

static HRESULT WINAPI json_arr_val_GetObject( IJsonValue *iface, IJsonObject **value )
{
    return E_ILLEGAL_METHOD_CALL;
}

static const struct IJsonValueVtbl json_array_value_vtbl =
{
    json_arr_val_QueryInterface,
    json_arr_val_AddRef,
    json_arr_val_Release,
    json_arr_val_GetIids,
    json_arr_val_GetRuntimeClassName,
    json_arr_val_GetTrustLevel,
    json_arr_val_get_ValueType,
    json_arr_val_Stringify,
    json_arr_val_GetString,
    json_arr_val_GetNumber,
    json_arr_val_GetBoolean,
    json_arr_val_GetArray,
    json_arr_val_GetObject,
};

static inline struct json_array *impl_from_IStringable( IStringable *iface )
{
    return CONTAINING_RECORD( iface, struct json_array, IStringable_iface );
}

static HRESULT WINAPI json_arr_str_QueryInterface( IStringable *iface, REFIID iid, void **out )
{
    return IJsonArray_QueryInterface( &impl_from_IStringable( iface )->IJsonArray_iface, iid, out );
}

static ULONG WINAPI json_arr_str_AddRef( IStringable *iface )
{
    return IJsonArray_AddRef( &impl_from_IStringable( iface )->IJsonArray_iface );
}

static ULONG WINAPI json_arr_str_Release( IStringable *iface )
{
    return IJsonArray_Release( &impl_from_IStringable( iface )->IJsonArray_iface );
}

static HRESULT WINAPI json_arr_str_GetIids( IStringable *iface, ULONG *count, IID **iids )
{
    return IJsonArray_GetIids( &impl_from_IStringable( iface )->IJsonArray_iface, count, iids );
}

static HRESULT WINAPI json_arr_str_GetRuntimeClassName( IStringable *iface, HSTRING *name )
{
    return IJsonArray_GetRuntimeClassName( &impl_from_IStringable( iface )->IJsonArray_iface, name );
}

static HRESULT WINAPI json_arr_str_GetTrustLevel( IStringable *iface, TrustLevel *level )
{
    return IJsonArray_GetTrustLevel( &impl_from_IStringable( iface )->IJsonArray_iface, level );
}

static HRESULT WINAPI json_arr_str_ToString( IStringable *iface, HSTRING *value )
{
    return json_arr_val_Stringify( &impl_from_IStringable( iface )->IJsonValue_iface, value );
}

static const struct IStringableVtbl json_array_stringable_vtbl =
{
    json_arr_str_QueryInterface,
    json_arr_str_AddRef,
    json_arr_str_Release,
    json_arr_str_GetIids,
    json_arr_str_GetRuntimeClassName,
    json_arr_str_GetTrustLevel,
    json_arr_str_ToString,
};

HRESULT create_json_array( cJSON *node, struct json_root *root, IJsonArray **out )
{
    struct json_array *impl;

    if (!out) return E_POINTER;
    *out = NULL;
    if (!node) return E_FAIL;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->IJsonArray_iface.lpVtbl = &json_array_vtbl;
    impl->IJsonValue_iface.lpVtbl = &json_array_value_vtbl;
    impl->IJsonValueInternal_iface.lpVtbl = &json_array_internal_vtbl;
    impl->IStringable_iface.lpVtbl = &json_array_stringable_vtbl;
    impl->IVector_iface.lpVtbl = &json_array_vec_vtbl;
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
    IJsonArrayStatics IJsonArrayStatics_iface;
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

    if (IsEqualGUID( iid, &IID_IJsonArrayStatics ))
    {
        *out = &impl->IJsonArrayStatics_iface;
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

DEFINE_IINSPECTABLE( json_array_statics, IJsonArrayStatics, struct json_array_statics, IActivationFactory_iface )

static HRESULT WINAPI json_array_statics_Parse( IJsonArrayStatics *iface, HSTRING input, IJsonArray **value )
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

    if (!cJSON_IsArray( tree ))
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

    hr = create_json_array( tree, root, value );
    json_root_release( root );
    return hr;
}

static HRESULT WINAPI json_array_statics_TryParse( IJsonArrayStatics *iface, HSTRING input, IJsonArray **result, boolean *succeeded )
{
    HRESULT hr;

    TRACE( "iface %p, input %s, result %p, succeeded %p\n", iface, debugstr_hstring( input ), result, succeeded );

    if (!succeeded || !result) return E_POINTER;

    *succeeded = FALSE;
    *result = NULL;

    hr = json_array_statics_Parse( iface, input, result );
    if (SUCCEEDED(hr)) *succeeded = TRUE;

    return S_OK;
}

static const struct IJsonArrayStaticsVtbl json_array_statics_vtbl =
{
    json_array_statics_QueryInterface,
    json_array_statics_AddRef,
    json_array_statics_Release,
    json_array_statics_GetIids,
    json_array_statics_GetRuntimeClassName,
    json_array_statics_GetTrustLevel,
    json_array_statics_Parse,
    json_array_statics_TryParse,
};

static struct json_array_statics json_array_statics =
{
    {&factory_vtbl},
    {&json_array_statics_vtbl},
    1,
};

IActivationFactory *json_array_factory = &json_array_statics.IActivationFactory_iface;
