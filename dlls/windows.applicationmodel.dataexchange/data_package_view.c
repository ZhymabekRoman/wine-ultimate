/* WinRT DataPackageView object
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
 * The DataPackageView the clipboard hands out and GetView produces: every
 * data getter mirrors the package's format table (an empty view over no
 * package reports no formats, false Contains and empty results), and every
 * Get*Async point hands out a born-completed operation whose GetResults
 * carries the value read at creation time, so a C++/WinRT co_await and a
 * C++/CX .Get() both finish synchronously with the package's data and
 * nothing throws across a noexcept boundary.
 */

struct data_package_view
{
    const void *view_vtbl;      /* IDataPackageView at offset 0 */
    const void *view2_vtbl;
    const void *view3_vtbl;
    const void *view4_vtbl;
    LONG ref;
    struct data_package *package; /* owned, may be NULL for the empty view */
    HSTRING accepted_format;
};

static const void *data_package_view_vtbl[];
static const void *data_package_view2_vtbl[];
static const void *data_package_view3_vtbl[];
static const void *data_package_view4_vtbl[];

/* Every interface group of the object shares the IUnknown/entry-point
 * implementations; resolve the object from the group's vtable pointer so one
 * set of method bodies serves every group at its own field offset. */
static struct data_package_view *impl_from_any( const void *iface )
{
    static const void *groups[] = { data_package_view_vtbl, data_package_view2_vtbl, data_package_view3_vtbl, data_package_view4_vtbl };
    static const int offsets[] = { 0, 1, 2, 3 };
    const void *vtbl = *(const void *const *)iface;
    int i;

    for (i = 0; i < (int)ARRAY_SIZE( groups ); i++)
        if (vtbl == groups[i])
            return (struct data_package_view *)((const BYTE *)iface - offsets[i] * sizeof(void *));
    return (struct data_package_view *)iface;
}

/* -------------------------------------------------- op helpers --------- */

static HRESULT view_string_op( struct data_package_view *impl, const WCHAR *format,
                               IAsyncOperation_HSTRING **operation )
{
    struct async_op *op;
    HSTRING value = NULL;

    if (impl->package)
    {
        enum fmt_kind kind;
        void *raw = package_get_raw( impl->package, format, &kind );

        if (kind == FMT_STRING && raw) WindowsDuplicateString( (HSTRING)raw, &value );
    }
    if (!value && FAILED( WindowsCreateString( L"", 0, &value ) )) return E_OUTOFMEMORY;

    op = create_async_op( &IID___FIAsyncOperation_1_HSTRING, L"Windows.Foundation.IAsyncOperation`1<HSTRING>", value, OP_RESULT_STRING );
    WindowsDeleteString( value );
    if (!op) return E_OUTOFMEMORY;
    *operation = (IAsyncOperation_HSTRING *)async_op_iface( op );
    return S_OK;
}

static HRESULT view_com_op( const IID *iid, const WCHAR *class_name, void *result, void *operation )
{
    struct async_op *op;

    op = create_async_op( &IID___FIAsyncOperation_1_HSTRING, class_name, result, OP_RESULT_COM );
    if (result) IUnknown_Release( (IUnknown *)result );
    if (!op) return E_OUTOFMEMORY;
    *(void **)operation = async_op_iface( op );
    return S_OK;
}

/* ------------------------------------------------ IDataPackageView ----- */

static HRESULT WINAPI view_QueryInterface( const void *iface, REFIID iid, void **out )
{
    struct data_package_view *impl = impl_from_any( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (!out) return E_POINTER;
    *out = NULL;

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IDataPackageView ))
    {
        *out = (void *)impl;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IDataPackageView2 ))
    {
        *out = (void *)&impl->view2_vtbl;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IDataPackageView3 ))
    {
        *out = (void *)&impl->view3_vtbl;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IDataPackageView4 ))
    {
        *out = (void *)&impl->view4_vtbl;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG WINAPI view_AddRef( const void *iface )
{
    struct data_package_view *impl = impl_from_any( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI view_Release( const void *iface )
{
    struct data_package_view *impl = impl_from_any( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        if (impl->package) IInspectable_Release( (IInspectable *)impl->package );
        WindowsDeleteString( impl->accepted_format );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI view_GetIids( const void *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI view_GetRuntimeClassName( const void *iface, HSTRING *class_name )
{
    if (!class_name) return E_POINTER;
    return WindowsCreateString( RuntimeClass_Windows_ApplicationModel_DataTransfer_DataPackageView,
                                lstrlenW( RuntimeClass_Windows_ApplicationModel_DataTransfer_DataPackageView ),
                                class_name );
}

static HRESULT WINAPI view_GetTrustLevel( const void *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI view_get_Properties( const void *iface, void **value )
{
    struct data_package_view *impl = impl_from_any( iface );
    IInspectable *props;

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    if (!(props = create_property_set_view( package_get_props( impl->package ) ))) return E_OUTOFMEMORY;
    *value = props;
    return S_OK;
}

static HRESULT WINAPI view_get_RequestedOperation( const void *iface, DataPackageOperation *value )
{
    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    *value = package_get_operation( impl_from_any( iface )->package );
    return S_OK;
}

static HRESULT WINAPI view_ReportOperationCompleted( const void *iface, DataPackageOperation value )
{
    TRACE( "iface %p, value %#x.\n", iface, value );
    return S_OK;
}

static HRESULT WINAPI view_get_AvailableFormats( const void *iface, void **formats )
{
    struct data_package_view *impl = impl_from_any( iface );
    struct winrt_vector *vector;
    ULONG i;

    TRACE( "iface %p, formats %p.\n", iface, formats );

    if (!formats) return E_POINTER;

    vector = create_vector( NULL, &IID___FIVectorView_1_HSTRING, &IID___FIIterable_1_HSTRING,
                            &IID___FIIterator_1_HSTRING, ELEM_STRING,
                            L"Windows.Foundation.Collections.IVectorView`1<HSTRING>" );
    if (!vector) return E_OUTOFMEMORY;

    for (i = 0; i < package_format_count( impl->package ); i++)
    {
        const WCHAR *key = package_format_key( impl->package, i );
        HSTRING str = NULL;

        if (!key) continue;
        if (FAILED( WindowsCreateString( key, lstrlenW( key ), &str ) )) continue;
        vector_append( vector, str );
        WindowsDeleteString( str );
    }

    *formats = vector_view_iface( vector );
    return S_OK;
}

static HRESULT WINAPI view_Contains( const void *iface, HSTRING format_id, BOOLEAN *value )
{
    struct data_package_view *impl = impl_from_any( iface );
    const WCHAR *key = WindowsGetStringRawBuffer( format_id, NULL );
    enum fmt_kind kind;

    TRACE( "iface %p, format_id %s, value %p.\n", iface, debugstr_hstring( format_id ), value );

    if (!value) return E_POINTER;
    *value = FALSE;
    if (!key || !impl->package) return S_OK;

    package_get_raw( impl->package, key, &kind );
    *value = kind != FMT_NONE;
    return S_OK;
}

static HRESULT WINAPI view_GetDataAsync( const void *iface, HSTRING format_id, void **operation )
{
    struct data_package_view *impl = impl_from_any( iface );
    const WCHAR *key = WindowsGetStringRawBuffer( format_id, NULL );
    void *value;

    TRACE( "iface %p, format_id %s, operation %p.\n", iface, debugstr_hstring( format_id ), operation );

    if (!operation) return E_POINTER;

    /* Only a stored COM payload has an IInspectable result to hand out; a
     * string payload's box is not expressible without the metadata boxing
     * helper, and Windows hands null for an unset format, so both the string
     * payload and the miss answer a completed operation with a null result. */
    value = key && impl->package ? package_get_com( impl->package, key ) : NULL;
    return view_com_op( &IID___FIAsyncOperation_1_IInspectable,
                        L"Windows.Foundation.IAsyncOperation`1<IInspectable>", value, operation );
}

static HRESULT WINAPI view_GetTextAsync( const void *iface, IAsyncOperation_HSTRING **operation )
{
    TRACE( "iface %p, operation %p.\n", iface, operation );

    if (!operation) return E_POINTER;
    return view_string_op( impl_from_any( iface ), format_text, operation );
}

static HRESULT WINAPI view_GetCustomTextAsync( const void *iface, HSTRING format_id,
                                               IAsyncOperation_HSTRING **operation )
{
    struct data_package_view *impl = impl_from_any( iface );
    const WCHAR *key = WindowsGetStringRawBuffer( format_id, NULL );
    enum fmt_kind kind;
    struct async_op *op;
    HSTRING value = NULL;

    TRACE( "iface %p, format_id %s, operation %p.\n", iface, debugstr_hstring( format_id ), operation );

    if (!operation) return E_POINTER;

    if (key && impl->package)
    {
        void *raw = package_get_raw( impl->package, key, &kind );
        if (kind == FMT_STRING && raw) WindowsDuplicateString( (HSTRING)raw, &value );
    }
    if (!value && FAILED( WindowsCreateString( L"", 0, &value ) )) return E_OUTOFMEMORY;

    op = create_async_op( &IID___FIAsyncOperation_1_HSTRING, L"Windows.Foundation.IAsyncOperation`1<HSTRING>", value, OP_RESULT_STRING );
    WindowsDeleteString( value );
    if (!op) return E_OUTOFMEMORY;
    *operation = (IAsyncOperation_HSTRING *)async_op_iface( op );
    return S_OK;
}

static HRESULT WINAPI view_GetUriAsync( const void *iface, void **operation )
{
    struct data_package_view *impl = impl_from_any( iface );
    void *uri;

    TRACE( "iface %p, operation %p.\n", iface, operation );

    if (!operation) return E_POINTER;

    uri = impl->package ? package_get_com( impl->package, format_uri ) : NULL;
    return view_com_op( &IID___FIAsyncOperation_1_Windows__CFoundation__CUri,
                        L"Windows.Foundation.IAsyncOperation`1<Windows.Foundation.Uri>", uri, operation );
}

static HRESULT WINAPI view_GetHtmlFormatAsync( const void *iface, IAsyncOperation_HSTRING **operation )
{
    TRACE( "iface %p, operation %p.\n", iface, operation );

    if (!operation) return E_POINTER;
    return view_string_op( impl_from_any( iface ), format_html, operation );
}

static HRESULT WINAPI view_GetResourceMapAsync( const void *iface, void **operation )
{
    struct winrt_map *map;
    void *result;

    TRACE( "iface %p, operation %p.\n", iface, operation );

    if (!operation) return E_POINTER;

    map = create_map( NULL,
                      &IID___FIMapView_2_HSTRING_Windows__CStorage__CStreams__CRandomAccessStreamReference,
                      NULL, NULL, NULL, ELEM_COM,
                      L"Windows.Foundation.Collections.IMapView`2<HSTRING, Windows.Storage.Streams.RandomAccessStreamReference>" );
    if (!map) return E_OUTOFMEMORY;
    result = map_view_iface( map );
    return view_com_op( &IID___FIAsyncOperation_1___FIMapView_2_HSTRING_Windows__CStorage__CStreams__CRandomAccessStreamReference,
                        L"Windows.Foundation.IAsyncOperation`1<Windows.Foundation.Collections.IMapView`2<HSTRING, Windows.Storage.Streams.RandomAccessStreamReference>>",
                        result, operation );
}

static HRESULT WINAPI view_GetRtfAsync( const void *iface, IAsyncOperation_HSTRING **operation )
{
    TRACE( "iface %p, operation %p.\n", iface, operation );

    if (!operation) return E_POINTER;
    return view_string_op( impl_from_any( iface ), format_rtf, operation );
}

static HRESULT WINAPI view_GetBitmapAsync( const void *iface, void **operation )
{
    struct data_package_view *impl = impl_from_any( iface );
    void *bitmap;

    TRACE( "iface %p, operation %p.\n", iface, operation );

    if (!operation) return E_POINTER;

    bitmap = impl->package ? package_get_com( impl->package, format_bitmap ) : NULL;
    return view_com_op( &IID___FIAsyncOperation_1_Windows__CStorage__CStreams__CRandomAccessStreamReference,
                        L"Windows.Foundation.IAsyncOperation`1<Windows.Storage.Streams.RandomAccessStreamReference>",
                        bitmap, operation );
}

static HRESULT WINAPI view_GetStorageItemsAsync( const void *iface, void **operation )
{
    struct data_package_view *impl = impl_from_any( iface );
    struct winrt_vector *items;

    TRACE( "iface %p, operation %p.\n", iface, operation );

    if (!operation) return E_POINTER;

    items = package_get_items( impl->package );
    if (!items)
    {
        items = create_vector( NULL, &IID___FIVectorView_1_Windows__CStorage__CIStorageItem,
                               &IID___FIIterable_1_Windows__CStorage__CIStorageItem,
                               &IID___FIIterator_1_Windows__CStorage__CIStorageItem, ELEM_COM,
                               L"Windows.Foundation.Collections.IVectorView`1<Windows.Storage.IStorageItem>" );
        if (!items) return E_OUTOFMEMORY;
    }

    return view_com_op( &IID___FIAsyncOperation_1___FIVectorView_1_Windows__CStorage__CIStorageItem,
                        L"Windows.Foundation.IAsyncOperation`1<Windows.Foundation.Collections.IVectorView`1<Windows.Storage.IStorageItem>>",
                        vector_view_iface( items ), operation );
}

static const void *data_package_view_vtbl[] =
{
    view_QueryInterface,
    view_AddRef,
    view_Release,
    view_GetIids,
    view_GetRuntimeClassName,
    view_GetTrustLevel,
    view_get_Properties,
    view_get_RequestedOperation,
    view_ReportOperationCompleted,
    view_get_AvailableFormats,
    view_Contains,
    view_GetDataAsync,
    view_GetTextAsync,
    view_GetCustomTextAsync,
    view_GetUriAsync,
    view_GetHtmlFormatAsync,
    view_GetResourceMapAsync,
    view_GetRtfAsync,
    view_GetBitmapAsync,
    view_GetStorageItemsAsync,
};

/* ------------------------------------------------ IDataPackageView2 ---- */

static HRESULT WINAPI view2_QueryInterface( const void *iface, REFIID iid, void **out )
{
    return view_QueryInterface( (void *)impl_from_any( iface ), iid, out );
}

static ULONG WINAPI view2_AddRef( const void *iface )
{
    return view_AddRef( (void *)impl_from_any( iface ) );
}

static ULONG WINAPI view2_Release( const void *iface )
{
    return view_Release( (void *)impl_from_any( iface ) );
}

static HRESULT WINAPI view2_GetApplicationLinkAsync( const void *iface, void **operation )
{
    struct data_package_view *impl = impl_from_any( iface );
    void *uri;

    TRACE( "iface %p, operation %p.\n", iface, operation );

    if (!operation) return E_POINTER;

    uri = impl->package ? package_get_com( impl->package, format_application_link ) : NULL;
    return view_com_op( &IID___FIAsyncOperation_1_Windows__CFoundation__CUri,
                        L"Windows.Foundation.IAsyncOperation`1<Windows.Foundation.Uri>", uri, operation );
}

static HRESULT WINAPI view2_GetWebLinkAsync( const void *iface, void **operation )
{
    struct data_package_view *impl = impl_from_any( iface );
    void *uri;

    TRACE( "iface %p, operation %p.\n", iface, operation );

    if (!operation) return E_POINTER;

    uri = impl->package ? package_get_com( impl->package, format_web_link ) : NULL;
    return view_com_op( &IID___FIAsyncOperation_1_Windows__CFoundation__CUri,
                        L"Windows.Foundation.IAsyncOperation`1<Windows.Foundation.Uri>", uri, operation );
}

static const void *data_package_view2_vtbl[] =
{
    view2_QueryInterface,
    view2_AddRef,
    view2_Release,
    view_GetIids,
    view_GetRuntimeClassName,
    view_GetTrustLevel,
    view2_GetApplicationLinkAsync,
    view2_GetWebLinkAsync,
};

/* ------------------------------------------------ IDataPackageView3 ---- */

/* The enterprise-data evaluation has no DRM back end; the completed
 * operation answers Allowed, the same result a plain unlocked file
 * evaluates to on Windows.  The instantiation IID of this operation is not
 * computable from the tree metadata, so the operation runs in wildcard QI
 * mode (see async.c). */
static HRESULT WINAPI view3_QueryInterface( const void *iface, REFIID iid, void **out )
{
    return view_QueryInterface( (void *)impl_from_any( iface ), iid, out );
}

static ULONG WINAPI view3_AddRef( const void *iface )
{
    return view_AddRef( (void *)impl_from_any( iface ) );
}

static ULONG WINAPI view3_Release( const void *iface )
{
    return view_Release( (void *)impl_from_any( iface ) );
}

static HRESULT view_access_op( void **operation )
{
    struct async_op *op;

    op = create_async_op( NULL,
                          L"Windows.Foundation.IAsyncOperation`1<Windows.Security.EnterpriseData.ProtectionPolicyEvaluationResult>",
                          (void *)(UINT_PTR)ProtectionPolicyEvaluationResult_Allowed, OP_RESULT_VALUE32 );
    if (!op) return E_OUTOFMEMORY;
    *operation = async_op_iface( op );
    return S_OK;
}

static HRESULT WINAPI view3_RequestAccessAsync( const void *iface, void **operation )
{
    TRACE( "iface %p, operation %p.\n", iface, operation );

    if (!operation) return E_POINTER;
    return view_access_op( operation );
}

static HRESULT WINAPI view3_RequestAccessWithEnterpriseIdAsync( const void *iface, HSTRING enterprise_id,
                                                                void **operation )
{
    TRACE( "iface %p, enterprise_id %s, operation %p.\n", iface, debugstr_hstring( enterprise_id ), operation );

    if (!operation) return E_POINTER;
    return view_access_op( operation );
}

static HRESULT WINAPI view3_UnlockAndAssumeEnterpriseIdentity( const void *iface, void *result )
{
    TRACE( "iface %p, result %p.\n", iface, result );

    if (!result) return E_POINTER;
    *(int *)result = ProtectionPolicyEvaluationResult_Allowed;
    return S_OK;
}

static const void *data_package_view3_vtbl[] =
{
    view3_QueryInterface,
    view3_AddRef,
    view3_Release,
    view_GetIids,
    view_GetRuntimeClassName,
    view_GetTrustLevel,
    view3_RequestAccessAsync,
    view3_RequestAccessWithEnterpriseIdAsync,
    view3_UnlockAndAssumeEnterpriseIdentity,
};

/* ------------------------------------------------ IDataPackageView4 ---- */

static HRESULT WINAPI view4_QueryInterface( const void *iface, REFIID iid, void **out )
{
    return view_QueryInterface( (void *)impl_from_any( iface ), iid, out );
}

static ULONG WINAPI view4_AddRef( const void *iface )
{
    return view_AddRef( (void *)impl_from_any( iface ) );
}

static ULONG WINAPI view4_Release( const void *iface )
{
    return view_Release( (void *)impl_from_any( iface ) );
}

static HRESULT WINAPI view4_SetAcceptedFormatId( const void *iface, HSTRING format_id )
{
    struct data_package_view *impl = impl_from_any( iface );
    HSTRING dup = NULL;

    TRACE( "iface %p, format_id %s.\n", iface, debugstr_hstring( format_id ) );

    if (format_id) WindowsDuplicateString( format_id, &dup );
    WindowsDeleteString( impl->accepted_format );
    impl->accepted_format = dup;
    return S_OK;
}

static const void *data_package_view4_vtbl[] =
{
    view4_QueryInterface,
    view4_AddRef,
    view4_Release,
    view_GetIids,
    view_GetRuntimeClassName,
    view_GetTrustLevel,
    view4_SetAcceptedFormatId,
};

/* ------------------------------------------------------------ create -- */

struct data_package_view *create_package_view_object( struct data_package *package )
{
    struct data_package_view *impl;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return NULL;

    impl->view_vtbl = data_package_view_vtbl;
    impl->view2_vtbl = data_package_view2_vtbl;
    impl->view3_vtbl = data_package_view3_vtbl;
    impl->view4_vtbl = data_package_view4_vtbl;
    impl->ref = 1;
    impl->package = package;
    if (package) IInspectable_AddRef( (IInspectable *)package );

    TRACE( "created package view %p over package %p.\n", impl, package );
    return impl;
}

IInspectable *create_data_package_view( struct data_package *package )
{
    return (IInspectable *)create_package_view_object( package );
}
