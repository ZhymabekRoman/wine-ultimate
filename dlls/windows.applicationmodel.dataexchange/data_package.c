/* WinRT DataPackage object
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

#include "private.h"

WINE_DEFAULT_DEBUG_CHANNEL(dataexchange);

/*
 * The DataPackage the app fills on its copy/share path.  The format table is
 * plain in-memory storage keyed by the canonical format strings the app
 * passes (the StandardDataFormats names, plus any custom key SetData gets);
 * the values live in three flavours - an HSTRING (text, html, rtf), a COM
 * pointer (Uri, bitmap reference, arbitrary SetData payload) and a storage
 * item vector (the copied SetStorageItems iterable, materialized into a real
 * IVector).  GetView hands out the DataPackageView mirror, Properties the
 * property set; every event registration point accepts and forgets, so no
 * registration throws and the copy completes.
 */

struct fmt_entry
{
    HSTRING format;
    void *value;
    enum fmt_kind kind;
};

struct data_package
{
    const void *iface_vtbl;     /* IDataPackage at offset 0 */
    const void *iface2_vtbl;
    const void *iface3_vtbl;
    const void *iface4_vtbl;
    LONG ref;
    struct fmt_entry *fmts;
    ULONG count, alloc;
    struct property_set *props;
    DataPackageOperation op;
};

static const void *data_package_vtbl[];
static const void *data_package2_vtbl[];
static const void *data_package3_vtbl[];
static const void *data_package4_vtbl[];

/* Every interface group of the object shares the IUnknown/entry-point
 * implementations; resolve the object from the group's vtable pointer so one
 * set of method bodies serves every group at its own field offset. */
static struct data_package *impl_from_any( const void *iface )
{
    static const void *groups[] = { data_package_vtbl, data_package2_vtbl, data_package3_vtbl, data_package4_vtbl };
    static const int offsets[] = { 0, 1, 2, 3 };
    const void *vtbl = *(const void *const *)iface;
    int i;

    for (i = 0; i < (int)ARRAY_SIZE( groups ); i++)
        if (vtbl == groups[i])
            return (struct data_package *)((const BYTE *)iface - offsets[i] * sizeof(void *));
    return (struct data_package *)iface;
}

/* ------------------------------------------------------- format table -- */

static void fmt_release( struct fmt_entry *entry )
{
    WindowsDeleteString( entry->format );
    if (entry->kind == FMT_STRING && entry->value) WindowsDeleteString( (HSTRING)entry->value );
    if (entry->kind == FMT_COM && entry->value) IUnknown_Release( (IUnknown *)entry->value );
    if (entry->kind == FMT_ITEMS && entry->value) vector_release( (struct winrt_vector *)entry->value );
    entry->value = NULL;
}

static LONG fmt_find( struct data_package *impl, const WCHAR *format )
{
    ULONG i;

    /* The format table keys on the canonical strings; format arrives as a
     * plain wide string (the raw buffer of a caller HSTRING), so compare it
     * against the stored key's raw buffer, case-sensitively. */
    for (i = 0; i < impl->count; i++)
    {
        const WCHAR *key = WindowsGetStringRawBuffer( impl->fmts[i].format, NULL );
        if (key && !wcscmp( key, format )) return i;
    }
    return -1;
}

static HRESULT package_set( struct data_package *impl, const WCHAR *format, void *value, enum fmt_kind kind )
{
    LONG idx = fmt_find( impl, format );
    HRESULT hr;

    if (idx >= 0)
    {
        void *stored = value;

        fmt_release( &impl->fmts[idx] );
        /* The ABI passes the string by value; the entry owns its own copy. */
        if (kind == FMT_STRING && value)
        {
            HRESULT hr2 = WindowsDuplicateString( (HSTRING)value, (HSTRING *)&stored );
            if (FAILED( hr2 )) return hr2;
        }
        impl->fmts[idx].value = stored;
        impl->fmts[idx].kind = kind;
        if (kind == FMT_COM && value) IUnknown_AddRef( (IUnknown *)value );
        return S_OK;
    }

    if (impl->count == impl->alloc)
    {
        ULONG alloc = max( impl->alloc * 2, 8 );
        struct fmt_entry *fmts = realloc( impl->fmts, alloc * sizeof(*fmts) );
        if (!fmts) return E_OUTOFMEMORY;
        impl->fmts = fmts;
        impl->alloc = alloc;
    }

    if (FAILED( (hr = WindowsCreateString( format, lstrlenW( format ), &impl->fmts[impl->count].format )) ))
        return hr;
    impl->fmts[impl->count].value = value;
    if (kind == FMT_STRING && value)
    {
        if (FAILED( (hr = WindowsDuplicateString( (HSTRING)value, (HSTRING *)&impl->fmts[impl->count].value )) ))
            return hr;
    }
    impl->fmts[impl->count].kind = kind;
    if (kind == FMT_COM && value) IUnknown_AddRef( (IUnknown *)value );
    impl->count++;
    return S_OK;
}

static void package_remove( struct data_package *impl, const WCHAR *format )
{
    LONG idx = fmt_find( impl, format );
    ULONG i;

    if (idx < 0) return;
    fmt_release( &impl->fmts[idx] );
    for (i = idx; i + 1 < impl->count; i++) impl->fmts[i] = impl->fmts[i + 1];
    impl->count--;
}

static void *package_get( struct data_package *impl, const WCHAR *format, enum fmt_kind *kind )
{
    LONG idx = fmt_find( impl, format );

    if (kind) *kind = idx < 0 ? FMT_NONE : impl->fmts[idx].kind;
    return idx < 0 ? NULL : impl->fmts[idx].value;
}

/* Cross-file accessors for the DataPackageView mirror (data_package_view.c). */

void *package_get_raw( struct data_package *pkg, const WCHAR *format, enum fmt_kind *kind )
{
    return package_get( pkg, format, kind );
}

HSTRING package_get_string( struct data_package *pkg, const WCHAR *format )
{
    enum fmt_kind kind;
    void *value = pkg ? package_get( pkg, format, &kind ) : NULL;

    if (kind == FMT_STRING && value)
    {
        HSTRING dup = NULL;
        WindowsDuplicateString( (HSTRING)value, &dup );
        return dup;
    }
    {
        HSTRING empty = NULL;
        WindowsCreateString( L"", 0, &empty );
        return empty;
    }
}

void *package_get_com( struct data_package *pkg, const WCHAR *format )
{
    enum fmt_kind kind;
    void *value = package_get( pkg, format, &kind );

    if (kind == FMT_COM && value) IUnknown_AddRef( (IUnknown *)value );
    return value;
}

struct winrt_vector *package_get_items( struct data_package *pkg )
{
    enum fmt_kind kind;
    void *value = package_get( pkg, format_storage_items, &kind );

    if (kind == FMT_ITEMS && value)
    {
        vector_add_ref( (struct winrt_vector *)value );
        return (struct winrt_vector *)value;
    }
    return NULL;
}

ULONG package_format_count( struct data_package *pkg )
{
    return pkg ? pkg->count : 0;
}

const WCHAR *package_format_key( struct data_package *pkg, ULONG index )
{
    if (!pkg || index >= pkg->count) return NULL;
    return WindowsGetStringRawBuffer( pkg->fmts[index].format, NULL );
}

DataPackageOperation package_get_operation( struct data_package *pkg )
{
    return pkg ? pkg->op : DataPackageOperation_None;
}

struct property_set *package_get_props( struct data_package *pkg )
{
    return pkg ? pkg->props : NULL;
}

struct winrt_map *package_get_resource_map( struct data_package *pkg )
{
    (void)pkg;
    return create_map( &IID___FIMap_2_HSTRING_Windows__CStorage__CStreams__CRandomAccessStreamReference,
                       &IID___FIMapView_2_HSTRING_Windows__CStorage__CStreams__CRandomAccessStreamReference,
                       NULL, NULL, NULL, ELEM_COM,
                       L"Windows.Foundation.Collections.IMap`2<HSTRING, Windows.Storage.Streams.RandomAccessStreamReference>" );
}

/* ------------------------------------------------------- IDataPackage -- */

static HRESULT WINAPI package_QueryInterface( const void *iface, REFIID iid, void **out )
{
    struct data_package *impl = impl_from_any( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (!out) return E_POINTER;
    *out = NULL;

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IDataPackage ))
    {
        *out = (void *)impl;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IDataPackage2 ))
    {
        *out = (void *)&impl->iface2_vtbl;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IDataPackage3 ))
    {
        *out = (void *)&impl->iface3_vtbl;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IDataPackage4 ))
    {
        *out = (void *)&impl->iface4_vtbl;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG WINAPI package_AddRef( const void *iface )
{
    struct data_package *impl = impl_from_any( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI package_Release( const void *iface )
{
    struct data_package *impl = impl_from_any( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        ULONG i;
        for (i = 0; i < impl->count; i++) fmt_release( &impl->fmts[i] );
        free( impl->fmts );
        if (impl->props) IInspectable_Release( (IInspectable *)impl->props );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI package_GetIids( const void *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI package_GetRuntimeClassName( const void *iface, HSTRING *class_name )
{
    if (!class_name) return E_POINTER;
    return WindowsCreateString( RuntimeClass_Windows_ApplicationModel_DataTransfer_DataPackage,
                                lstrlenW( RuntimeClass_Windows_ApplicationModel_DataTransfer_DataPackage ),
                                class_name );
}

static HRESULT WINAPI package_GetTrustLevel( const void *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI package_GetView( const void *iface, void **result )
{
    struct data_package *impl = impl_from_any( iface );
    IInspectable *view;

    TRACE( "iface %p, result %p.\n", iface, result );

    if (!result) return E_POINTER;
    if (!(view = create_data_package_view( impl ))) return E_OUTOFMEMORY;
    *result = view;
    return S_OK;
}

static HRESULT WINAPI package_get_Properties( const void *iface, void **value )
{
    struct data_package *impl = impl_from_any( iface );

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    *value = (void *)impl->props;
    IInspectable_AddRef( (IInspectable *)impl->props );
    return S_OK;
}

static HRESULT WINAPI package_get_RequestedOperation( const void *iface, DataPackageOperation *value )
{
    struct data_package *impl = impl_from_any( iface );

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    *value = impl->op;
    return S_OK;
}

static HRESULT WINAPI package_put_RequestedOperation( const void *iface, DataPackageOperation value )
{
    struct data_package *impl = impl_from_any( iface );

    TRACE( "iface %p, value %#x.\n", iface, value );

    impl->op = value;
    return S_OK;
}

/* Event registration points: accept the handler, hand out a non-zero token,
 * forget everything; removal accepts any token.  No app registration throws. */

static HRESULT package_add_event( void *token )
{
    static LONG next_token;

    if (!token) return E_POINTER;
    ((EventRegistrationToken *)token)->value = InterlockedIncrement( &next_token );
    return S_OK;
}

static HRESULT WINAPI package_add_OperationCompleted( const void *iface, void *handler, void *token )
{
    TRACE( "iface %p, handler %p, token %p.\n", iface, handler, token );
    return package_add_event( token );
}

static HRESULT WINAPI package_remove_OperationCompleted( const void *iface, EventRegistrationToken token )
{
    TRACE( "iface %p, token %s.\n", iface, wine_dbgstr_longlong( token.value ) );
    return S_OK;
}

static HRESULT WINAPI package_add_Destroyed( const void *iface, void *handler, void *token )
{
    TRACE( "iface %p, handler %p, token %p.\n", iface, handler, token );
    return package_add_event( token );
}

static HRESULT WINAPI package_remove_Destroyed( const void *iface, EventRegistrationToken token )
{
    TRACE( "iface %p, token %s.\n", iface, wine_dbgstr_longlong( token.value ) );
    return S_OK;
}

static HRESULT WINAPI package_SetData( const void *iface, HSTRING format_id, void *value )
{
    struct data_package *impl = impl_from_any( iface );
    const WCHAR *key = WindowsGetStringRawBuffer( format_id, NULL );

    TRACE( "iface %p, format_id %s, value %p.\n", iface, debugstr_hstring( format_id ), value );

    if (!key) return E_POINTER;
    if (!value)
    {
        package_remove( impl, key );
        return S_OK;
    }
    return package_set( impl, key, value, FMT_COM );
}

static HRESULT WINAPI package_SetDataProvider( const void *iface, HSTRING format_id, void *handler )
{
    struct data_package *impl = impl_from_any( iface );

    TRACE( "iface %p, format_id %s, handler %p.\n", iface, debugstr_hstring( format_id ), handler );

    /* A lazy provider has no consumer without a share target; accept and
     * drop the registration so the app's SetDataProvider does not throw. */
    (void)impl;
    return S_OK;
}

static HRESULT WINAPI package_SetText( const void *iface, HSTRING value )
{
    struct data_package *impl = impl_from_any( iface );

    TRACE( "iface %p, value %s.\n", iface, debugstr_hstring( value ) );

    if (!value) return E_POINTER;
    return package_set( impl, format_text, value, FMT_STRING );
}

static HRESULT WINAPI package_SetUri( const void *iface, void *value )
{
    struct data_package *impl = impl_from_any( iface );

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    return package_set( impl, format_uri, value, FMT_COM );
}

static HRESULT WINAPI package_SetHtmlFormat( const void *iface, HSTRING value )
{
    struct data_package *impl = impl_from_any( iface );

    TRACE( "iface %p, value %s.\n", iface, debugstr_hstring( value ) );

    if (!value) return E_POINTER;
    return package_set( impl, format_html, value, FMT_STRING );
}

static HRESULT WINAPI package_get_ResourceMap( const void *iface, void **value )
{
    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    *value = map_iface( package_get_resource_map( NULL ) );
    return S_OK;
}

static HRESULT WINAPI package_SetRtf( const void *iface, HSTRING value )
{
    struct data_package *impl = impl_from_any( iface );

    TRACE( "iface %p, value %s.\n", iface, debugstr_hstring( value ) );

    if (!value) return E_POINTER;
    return package_set( impl, format_rtf, value, FMT_STRING );
}

static HRESULT WINAPI package_SetBitmap( const void *iface, void *value )
{
    struct data_package *impl = impl_from_any( iface );

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    return package_set( impl, format_bitmap, value, FMT_COM );
}

/* Copy the iterable's items into a real vector; the view hands them out
 * through the IVector`1 view interface. */
static HRESULT package_set_storage_items( struct data_package *impl,
                                          __FIIterable_1_Windows__CStorage__CIStorageItem *value )
{
    __FIIterator_1_Windows__CStorage__CIStorageItem *iter = NULL;
    struct winrt_vector *items;
    HRESULT hr;

    items = create_vector( NULL, &IID___FIVectorView_1_Windows__CStorage__CIStorageItem,
                           &IID___FIIterable_1_Windows__CStorage__CIStorageItem,
                           &IID___FIIterator_1_Windows__CStorage__CIStorageItem,
                           ELEM_COM, L"Windows.Foundation.Collections.IVector`1<Windows.Storage.IStorageItem>" );
    if (!items) return E_OUTOFMEMORY;

    if (value && SUCCEEDED( __FIIterable_1_Windows__CStorage__CIStorageItem_First( value, &iter ) ))
    {
        BOOLEAN has = FALSE;

        __FIIterator_1_Windows__CStorage__CIStorageItem_get_HasCurrent( iter, &has );
        while (has)
        {
            IInspectable *item = NULL;

            if (FAILED( __FIIterator_1_Windows__CStorage__CIStorageItem_get_Current( iter, (__x_ABI_CWindows_CStorage_CIStorageItem **)&item ) ))
                break;
            hr = vector_append( items, item );
            if (item) IInspectable_Release( item );
            if (FAILED( hr )) break;
            if (FAILED( __FIIterator_1_Windows__CStorage__CIStorageItem_MoveNext( iter, &has ) )) break;
        }
        __FIIterator_1_Windows__CStorage__CIStorageItem_Release( iter );
    }

    return package_set( impl, format_storage_items, items, FMT_ITEMS );
}

static HRESULT WINAPI package_SetStorageItemsReadOnly( const void *iface, void *value )
{
    struct data_package *impl = impl_from_any( iface );

    TRACE( "iface %p, value %p.\n", iface, value );

    return package_set_storage_items( impl, (__FIIterable_1_Windows__CStorage__CIStorageItem *)value );
}

static HRESULT WINAPI package_SetStorageItems( const void *iface, void *value, BOOLEAN read_only )
{
    struct data_package *impl = impl_from_any( iface );

    TRACE( "iface %p, value %p, read_only %d.\n", iface, value, read_only );

    return package_set_storage_items( impl, (__FIIterable_1_Windows__CStorage__CIStorageItem *)value );
}

static const void *data_package_vtbl[] =
{
    package_QueryInterface,
    package_AddRef,
    package_Release,
    package_GetIids,
    package_GetRuntimeClassName,
    package_GetTrustLevel,
    package_GetView,
    package_get_Properties,
    package_get_RequestedOperation,
    package_put_RequestedOperation,
    package_add_OperationCompleted,
    package_remove_OperationCompleted,
    package_add_Destroyed,
    package_remove_Destroyed,
    package_SetData,
    package_SetDataProvider,
    package_SetText,
    package_SetUri,
    package_SetHtmlFormat,
    package_get_ResourceMap,
    package_SetRtf,
    package_SetBitmap,
    package_SetStorageItemsReadOnly,
    package_SetStorageItems,
};

static HRESULT WINAPI package2_QueryInterface( const void *iface, REFIID iid, void **out )
{
    return package_QueryInterface( (void *)impl_from_any( iface ), iid, out );
}

static ULONG WINAPI package2_AddRef( const void *iface )
{
    return package_AddRef( (void *)impl_from_any( iface ) );
}

static ULONG WINAPI package2_Release( const void *iface )
{
    return package_Release( (void *)impl_from_any( iface ) );
}

static HRESULT WINAPI package2_SetApplicationLink( const void *iface, void *value )
{
    struct data_package *impl = impl_from_any( iface );

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    return package_set( impl, format_application_link, value, FMT_COM );
}

static HRESULT WINAPI package2_SetWebLink( const void *iface, void *value )
{
    struct data_package *impl = impl_from_any( iface );

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    return package_set( impl, format_web_link, value, FMT_COM );
}

static const void *data_package2_vtbl[] =
{
    package2_QueryInterface,
    package2_AddRef,
    package2_Release,
    package_GetIids,
    package_GetRuntimeClassName,
    package_GetTrustLevel,
    package2_SetApplicationLink,
    package2_SetWebLink,
};

static HRESULT WINAPI package3_QueryInterface( const void *iface, REFIID iid, void **out )
{
    return package_QueryInterface( (void *)impl_from_any( iface ), iid, out );
}

static ULONG WINAPI package3_AddRef( const void *iface )
{
    return package_AddRef( (void *)impl_from_any( iface ) );
}

static ULONG WINAPI package3_Release( const void *iface )
{
    return package_Release( (void *)impl_from_any( iface ) );
}

static HRESULT WINAPI package3_add_ShareCompleted( const void *iface, void *handler, void *token )
{
    TRACE( "iface %p, handler %p, token %p.\n", iface, handler, token );
    return package_add_event( token );
}

static HRESULT WINAPI package3_remove_ShareCompleted( const void *iface, EventRegistrationToken token )
{
    TRACE( "iface %p, token %s.\n", iface, wine_dbgstr_longlong( token.value ) );
    return S_OK;
}

static const void *data_package3_vtbl[] =
{
    package3_QueryInterface,
    package3_AddRef,
    package3_Release,
    package_GetIids,
    package_GetRuntimeClassName,
    package_GetTrustLevel,
    package3_add_ShareCompleted,
    package3_remove_ShareCompleted,
};

static HRESULT WINAPI package4_QueryInterface( const void *iface, REFIID iid, void **out )
{
    return package_QueryInterface( (void *)impl_from_any( iface ), iid, out );
}

static ULONG WINAPI package4_AddRef( const void *iface )
{
    return package_AddRef( (void *)impl_from_any( iface ) );
}

static ULONG WINAPI package4_Release( const void *iface )
{
    return package_Release( (void *)impl_from_any( iface ) );
}

static HRESULT WINAPI package4_add_ShareCanceled( const void *iface, void *handler, void *token )
{
    TRACE( "iface %p, handler %p, token %p.\n", iface, handler, token );
    return package_add_event( token );
}

static HRESULT WINAPI package4_remove_ShareCanceled( const void *iface, EventRegistrationToken token )
{
    TRACE( "iface %p, token %s.\n", iface, wine_dbgstr_longlong( token.value ) );
    return S_OK;
}

static const void *data_package4_vtbl[] =
{
    package4_QueryInterface,
    package4_AddRef,
    package4_Release,
    package_GetIids,
    package_GetRuntimeClassName,
    package_GetTrustLevel,
    package4_add_ShareCanceled,
    package4_remove_ShareCanceled,
};

IInspectable *create_data_package( void )
{
    struct data_package *impl;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return NULL;

    impl->iface_vtbl = data_package_vtbl;
    impl->iface2_vtbl = data_package2_vtbl;
    impl->iface3_vtbl = data_package3_vtbl;
    impl->iface4_vtbl = data_package4_vtbl;
    impl->ref = 1;
    impl->op = DataPackageOperation_Copy;

    impl->props = (struct property_set *)create_property_set();
    if (!impl->props)
    {
        free( impl );
        return NULL;
    }

    TRACE( "created data package %p.\n", impl );
    return (IInspectable *)impl;
}
