/*
 * WinRT Windows.Storage.Pickers Implementation
 * (XDG Desktop Portal FileChooser opt-in via WINE_USE_PORTAL_PICKER=1,
 *  with Win32 GetOpenFileNameW / GetSaveFileNameW / SHBrowseForFolderW fallback)
 *
 * Copyright 2026 Wine Project
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

#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "ntstatus.h"
#include "private.h"
#include "unixlib.h"
#include "wine/unixlib.h"

#include "commdlg.h"
#include "shlobj.h"

WINE_DEFAULT_DEBUG_CHANNEL(storage);

static const GUID kfid_Documents = { 0xfdd39ad0, 0x238f, 0x46af, { 0xad,0xb4, 0x6c,0x85,0x48,0x03,0x69,0xc7 } };
static const GUID kfid_Desktop   = { 0xb4bfcc3a, 0xdb2c, 0x424c, { 0xb0,0x29, 0x7f,0xe9,0x9a,0x87,0xc6,0x41 } };
static const GUID kfid_Downloads = { 0x374de290, 0x123f, 0x4565, { 0x91,0x64, 0x39,0xc4,0x92,0x5e,0x46,0x7b } };
static const GUID kfid_Music     = { 0x4bd8d571, 0x6d19, 0x48d3, { 0xbe,0x97, 0x42,0x22,0x20,0x08,0x0e,0x43 } };
static const GUID kfid_Pictures  = { 0x33e28130, 0x4e1e, 0x4676, { 0x83,0x5a, 0x98,0x39,0x5c,0x3b,0xc3,0xbb } };
static const GUID kfid_Videos    = { 0x18989b1d, 0x99b5, 0x455b, { 0x84,0x1c, 0xab,0x7c,0x74,0xe4,0xdd,0xfc } };
static const GUID kfid_Objects3D = { 0x31c0dd25, 0x9439, 0x4f12, { 0xbf,0x41, 0x7f,0xf4,0xee,0xd3,0x87,0xab } };

/* {3e68d4bd-7135-4d10-8018-9fb6d9f33fa1} */
static const GUID IID_IInitializeWithWindow_local =
{
    0x3e68d4bd, 0x7135, 0x4d10, { 0x80, 0x18, 0x9f, 0xb6, 0xd9, 0xf3, 0x3f, 0xa1 }
};

typedef struct IInitializeWithWindowLocal IInitializeWithWindowLocal;
typedef struct IInitializeWithWindowLocalVtbl
{
    HRESULT (WINAPI *QueryInterface)( IInitializeWithWindowLocal *This, REFIID riid, void **ppv );
    ULONG (WINAPI *AddRef)( IInitializeWithWindowLocal *This );
    ULONG (WINAPI *Release)( IInitializeWithWindowLocal *This );
    HRESULT (WINAPI *Initialize)( IInitializeWithWindowLocal *This, HWND hwnd );
} IInitializeWithWindowLocalVtbl;

struct IInitializeWithWindowLocal
{
    const IInitializeWithWindowLocalVtbl *lpVtbl;
};

static char *wide_to_utf8( const WCHAR *str )
{
    int len;
    char *ret;

    if (!str || !*str) return NULL;
    len = WideCharToMultiByte( CP_UTF8, 0, str, -1, NULL, 0, NULL, NULL );
    if (len <= 0) return NULL;
    if (!(ret = malloc( len ))) return NULL;
    WideCharToMultiByte( CP_UTF8, 0, str, -1, ret, len, NULL, NULL );
    return ret;
}

static char *hstring_to_utf8( HSTRING str )
{
    if (!str) return NULL;
    return wide_to_utf8( WindowsGetStringRawBuffer( str, NULL ) );
}

static HRESULT duplicate_hstring_out( HSTRING src, HSTRING *out )
{
    if (!out) return E_POINTER;
    if (!src)
    {
        *out = NULL;
        return S_OK;
    }
    return WindowsDuplicateString( src, out );
}

static HRESULT replace_hstring( HSTRING *dst, HSTRING src )
{
    HSTRING copy = NULL;
    HRESULT hr;

    if (src && FAILED(hr = WindowsDuplicateString( src, &copy ))) return hr;
    if (*dst) WindowsDeleteString( *dst );
    *dst = copy;
    return S_OK;
}

/* =========================================================================
 * IVector<HSTRING> / IVectorView<HSTRING> / IIterable<HSTRING>
 * ========================================================================= */

struct hstring_vector
{
    __FIVector_1_HSTRING __FIVector_1_HSTRING_iface;
    __FIVectorView_1_HSTRING __FIVectorView_1_HSTRING_iface;
    __FIIterable_1_HSTRING __FIIterable_1_HSTRING_iface;
    LONG ref;
    CRITICAL_SECTION cs;
    HSTRING *items;
    UINT32 count;
    UINT32 capacity;
};

struct hstring_iterator
{
    __FIIterator_1_HSTRING __FIIterator_1_HSTRING_iface;
    LONG ref;
    struct hstring_vector *vec;
    UINT32 index;
};

static inline struct hstring_vector *impl_from_IVector_HSTRING( __FIVector_1_HSTRING *iface )
{
    return CONTAINING_RECORD( iface, struct hstring_vector, __FIVector_1_HSTRING_iface );
}

static inline struct hstring_iterator *impl_from_IIterator_HSTRING( __FIIterator_1_HSTRING *iface )
{
    return CONTAINING_RECORD( iface, struct hstring_iterator, __FIIterator_1_HSTRING_iface );
}

static HRESULT WINAPI hstring_iterator_QueryInterface( __FIIterator_1_HSTRING *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID___FIIterator_1_HSTRING ))
    {
        *out = iface;
        IInspectable_AddRef( (IInspectable *)iface );
        return S_OK;
    }
    return E_NOINTERFACE;
}

static ULONG WINAPI hstring_iterator_AddRef( __FIIterator_1_HSTRING *iface )
{
    struct hstring_iterator *impl = impl_from_IIterator_HSTRING( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI hstring_iterator_Release( __FIIterator_1_HSTRING *iface )
{
    struct hstring_iterator *impl = impl_from_IIterator_HSTRING( iface );
    LONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        __FIVector_1_HSTRING_Release( &impl->vec->__FIVector_1_HSTRING_iface );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI hstring_iterator_GetIids( __FIIterator_1_HSTRING *iface, ULONG *count, IID **iids )
{
    return E_NOTIMPL;
}

static HRESULT WINAPI hstring_iterator_GetRuntimeClassName( __FIIterator_1_HSTRING *iface, HSTRING *name )
{
    return E_NOTIMPL;
}

static HRESULT WINAPI hstring_iterator_GetTrustLevel( __FIIterator_1_HSTRING *iface, TrustLevel *level )
{
    if (!level) return E_POINTER;
    *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI hstring_iterator_get_Current( __FIIterator_1_HSTRING *iface, HSTRING *value )
{
    struct hstring_iterator *impl = impl_from_IIterator_HSTRING( iface );
    HRESULT hr = E_BOUNDS;

    if (!value) return E_POINTER;
    *value = NULL;
    EnterCriticalSection( &impl->vec->cs );
    if (impl->index < impl->vec->count)
        hr = WindowsDuplicateString( impl->vec->items[impl->index], value );
    LeaveCriticalSection( &impl->vec->cs );
    return hr;
}

static HRESULT WINAPI hstring_iterator_get_HasCurrent( __FIIterator_1_HSTRING *iface, boolean *value )
{
    struct hstring_iterator *impl = impl_from_IIterator_HSTRING( iface );

    if (!value) return E_POINTER;
    EnterCriticalSection( &impl->vec->cs );
    *value = impl->index < impl->vec->count;
    LeaveCriticalSection( &impl->vec->cs );
    return S_OK;
}

static HRESULT WINAPI hstring_iterator_MoveNext( __FIIterator_1_HSTRING *iface, boolean *value )
{
    struct hstring_iterator *impl = impl_from_IIterator_HSTRING( iface );

    if (!value) return E_POINTER;
    EnterCriticalSection( &impl->vec->cs );
    if (impl->index < impl->vec->count) ++impl->index;
    *value = impl->index < impl->vec->count;
    LeaveCriticalSection( &impl->vec->cs );
    return S_OK;
}

static HRESULT WINAPI hstring_iterator_GetMany( __FIIterator_1_HSTRING *iface, UINT32 size, HSTRING *items, UINT32 *count )
{
    struct hstring_iterator *impl = impl_from_IIterator_HSTRING( iface );
    UINT32 n = 0;

    if (!count) return E_POINTER;
    EnterCriticalSection( &impl->vec->cs );
    while (n < size && impl->index + n < impl->vec->count)
    {
        if (!items) { LeaveCriticalSection( &impl->vec->cs ); return E_POINTER; }
        WindowsDuplicateString( impl->vec->items[impl->index + n], &items[n] );
        ++n;
    }
    impl->index += n;
    LeaveCriticalSection( &impl->vec->cs );
    *count = n;
    return S_OK;
}

static const struct __FIIterator_1_HSTRINGVtbl hstring_iterator_vtbl =
{
    hstring_iterator_QueryInterface,
    hstring_iterator_AddRef,
    hstring_iterator_Release,
    hstring_iterator_GetIids,
    hstring_iterator_GetRuntimeClassName,
    hstring_iterator_GetTrustLevel,
    hstring_iterator_get_Current,
    hstring_iterator_get_HasCurrent,
    hstring_iterator_MoveNext,
    hstring_iterator_GetMany,
};

static HRESULT WINAPI hstring_vector_QueryInterface( __FIVector_1_HSTRING *iface, REFIID iid, void **out )
{
    struct hstring_vector *impl = impl_from_IVector_HSTRING( iface );
    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID___FIVector_1_HSTRING ))
    {
        *out = &impl->__FIVector_1_HSTRING_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID___FIVectorView_1_HSTRING ))
    {
        *out = &impl->__FIVectorView_1_HSTRING_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID___FIIterable_1_HSTRING ))
    {
        *out = &impl->__FIIterable_1_HSTRING_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    return E_NOINTERFACE;
}

static ULONG WINAPI hstring_vector_AddRef( __FIVector_1_HSTRING *iface )
{
    struct hstring_vector *impl = impl_from_IVector_HSTRING( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI hstring_vector_Release( __FIVector_1_HSTRING *iface )
{
    struct hstring_vector *impl = impl_from_IVector_HSTRING( iface );
    LONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        UINT32 i;
        for (i = 0; i < impl->count; ++i) WindowsDeleteString( impl->items[i] );
        free( impl->items );
        DeleteCriticalSection( &impl->cs );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI hstring_vector_GetIids( __FIVector_1_HSTRING *iface, ULONG *count, IID **iids )
{
    return E_NOTIMPL;
}

static HRESULT WINAPI hstring_vector_GetRuntimeClassName( __FIVector_1_HSTRING *iface, HSTRING *name )
{
    return E_NOTIMPL;
}

static HRESULT WINAPI hstring_vector_GetTrustLevel( __FIVector_1_HSTRING *iface, TrustLevel *level )
{
    if (!level) return E_POINTER;
    *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI hstring_vector_GetAt( __FIVector_1_HSTRING *iface, UINT32 index, HSTRING *value )
{
    struct hstring_vector *impl = impl_from_IVector_HSTRING( iface );
    HRESULT hr = E_BOUNDS;

    if (!value) return E_POINTER;
    *value = NULL;
    EnterCriticalSection( &impl->cs );
    if (index < impl->count)
        hr = WindowsDuplicateString( impl->items[index], value );
    LeaveCriticalSection( &impl->cs );
    return hr;
}

static HRESULT WINAPI hstring_vector_get_Size( __FIVector_1_HSTRING *iface, UINT32 *value )
{
    struct hstring_vector *impl = impl_from_IVector_HSTRING( iface );
    if (!value) return E_POINTER;
    EnterCriticalSection( &impl->cs );
    *value = impl->count;
    LeaveCriticalSection( &impl->cs );
    return S_OK;
}

static HRESULT WINAPI hstring_vector_GetView( __FIVector_1_HSTRING *iface, __FIVectorView_1_HSTRING **value )
{
    struct hstring_vector *impl = impl_from_IVector_HSTRING( iface );
    if (!value) return E_POINTER;
    *value = &impl->__FIVectorView_1_HSTRING_iface;
    IInspectable_AddRef( (IInspectable *)*value );
    return S_OK;
}

static HRESULT WINAPI hstring_vector_IndexOf( __FIVector_1_HSTRING *iface, HSTRING element, UINT32 *index, boolean *value )
{
    struct hstring_vector *impl = impl_from_IVector_HSTRING( iface );
    UINT32 i;
    INT32 cmp;

    if (!index || !value) return E_POINTER;
    *index = 0;
    *value = FALSE;
    EnterCriticalSection( &impl->cs );
    for (i = 0; i < impl->count; ++i)
    {
        if (SUCCEEDED(WindowsCompareStringOrdinal( impl->items[i], element, &cmp )) && cmp == 0)
        {
            *index = i;
            *value = TRUE;
            break;
        }
    }
    LeaveCriticalSection( &impl->cs );
    return S_OK;
}

static HRESULT WINAPI hstring_vector_SetAt( __FIVector_1_HSTRING *iface, UINT32 index, HSTRING value )
{
    struct hstring_vector *impl = impl_from_IVector_HSTRING( iface );
    HSTRING copy = NULL;
    HRESULT hr;

    if (value && FAILED(hr = WindowsDuplicateString( value, &copy ))) return hr;
    EnterCriticalSection( &impl->cs );
    if (index >= impl->count)
    {
        LeaveCriticalSection( &impl->cs );
        if (copy) WindowsDeleteString( copy );
        return E_BOUNDS;
    }
    WindowsDeleteString( impl->items[index] );
    impl->items[index] = copy;
    LeaveCriticalSection( &impl->cs );
    return S_OK;
}

static HRESULT WINAPI hstring_vector_InsertAt( __FIVector_1_HSTRING *iface, UINT32 index, HSTRING value )
{
    struct hstring_vector *impl = impl_from_IVector_HSTRING( iface );
    HSTRING copy = NULL;
    HRESULT hr;

    if (value && FAILED(hr = WindowsDuplicateString( value, &copy ))) return hr;
    EnterCriticalSection( &impl->cs );
    if (index > impl->count)
    {
        LeaveCriticalSection( &impl->cs );
        if (copy) WindowsDeleteString( copy );
        return E_BOUNDS;
    }
    if (impl->count == impl->capacity)
    {
        UINT32 new_cap = impl->capacity ? impl->capacity * 2 : 8;
        HSTRING *grown = realloc( impl->items, new_cap * sizeof(*grown) );
        if (!grown)
        {
            LeaveCriticalSection( &impl->cs );
            if (copy) WindowsDeleteString( copy );
            return E_OUTOFMEMORY;
        }
        impl->items = grown;
        impl->capacity = new_cap;
    }
    if (index < impl->count)
        memmove( &impl->items[index + 1], &impl->items[index], (impl->count - index) * sizeof(HSTRING) );
    impl->items[index] = copy;
    impl->count++;
    LeaveCriticalSection( &impl->cs );
    return S_OK;
}

static HRESULT WINAPI hstring_vector_RemoveAt( __FIVector_1_HSTRING *iface, UINT32 index )
{
    struct hstring_vector *impl = impl_from_IVector_HSTRING( iface );

    EnterCriticalSection( &impl->cs );
    if (index >= impl->count)
    {
        LeaveCriticalSection( &impl->cs );
        return E_BOUNDS;
    }
    WindowsDeleteString( impl->items[index] );
    if (index + 1 < impl->count)
        memmove( &impl->items[index], &impl->items[index + 1], (impl->count - index - 1) * sizeof(HSTRING) );
    impl->count--;
    LeaveCriticalSection( &impl->cs );
    return S_OK;
}

static HRESULT WINAPI hstring_vector_Append( __FIVector_1_HSTRING *iface, HSTRING value )
{
    struct hstring_vector *impl = impl_from_IVector_HSTRING( iface );
    UINT32 count;
    hstring_vector_get_Size( iface, &count );
    return hstring_vector_InsertAt( iface, impl->count, value );
}

static HRESULT WINAPI hstring_vector_RemoveAtEnd( __FIVector_1_HSTRING *iface )
{
    struct hstring_vector *impl = impl_from_IVector_HSTRING( iface );
    EnterCriticalSection( &impl->cs );
    if (!impl->count)
    {
        LeaveCriticalSection( &impl->cs );
        return E_BOUNDS;
    }
    impl->count--;
    WindowsDeleteString( impl->items[impl->count] );
    LeaveCriticalSection( &impl->cs );
    return S_OK;
}

static HRESULT WINAPI hstring_vector_Clear( __FIVector_1_HSTRING *iface )
{
    struct hstring_vector *impl = impl_from_IVector_HSTRING( iface );
    UINT32 i;
    EnterCriticalSection( &impl->cs );
    for (i = 0; i < impl->count; ++i) WindowsDeleteString( impl->items[i] );
    impl->count = 0;
    LeaveCriticalSection( &impl->cs );
    return S_OK;
}

static HRESULT WINAPI hstring_vector_GetMany( __FIVector_1_HSTRING *iface, UINT32 start, UINT32 size, HSTRING *items, UINT32 *count )
{
    struct hstring_vector *impl = impl_from_IVector_HSTRING( iface );
    UINT32 n = 0;
    if (!count) return E_POINTER;
    EnterCriticalSection( &impl->cs );
    while (n < size && start + n < impl->count)
    {
        if (!items) { LeaveCriticalSection( &impl->cs ); return E_POINTER; }
        WindowsDuplicateString( impl->items[start + n], &items[n] );
        ++n;
    }
    LeaveCriticalSection( &impl->cs );
    *count = n;
    return S_OK;
}

static HRESULT WINAPI hstring_vector_ReplaceAll( __FIVector_1_HSTRING *iface, UINT32 count, HSTRING *items )
{
    UINT32 i;
    HRESULT hr;
    if (count && !items) return E_POINTER;
    hstring_vector_Clear( iface );
    for (i = 0; i < count; ++i)
    {
        if (FAILED(hr = hstring_vector_Append( iface, items[i] ))) return hr;
    }
    return S_OK;
}

static const struct __FIVector_1_HSTRINGVtbl hstring_vector_vtbl =
{
    hstring_vector_QueryInterface,
    hstring_vector_AddRef,
    hstring_vector_Release,
    hstring_vector_GetIids,
    hstring_vector_GetRuntimeClassName,
    hstring_vector_GetTrustLevel,
    hstring_vector_GetAt,
    hstring_vector_get_Size,
    hstring_vector_GetView,
    hstring_vector_IndexOf,
    hstring_vector_SetAt,
    hstring_vector_InsertAt,
    hstring_vector_RemoveAt,
    hstring_vector_Append,
    hstring_vector_RemoveAtEnd,
    hstring_vector_Clear,
    hstring_vector_GetMany,
    hstring_vector_ReplaceAll,
};

DEFINE_IINSPECTABLE_( hstring_vector_view, __FIVectorView_1_HSTRING, struct hstring_vector, impl_from_IVectorView_HSTRING, __FIVectorView_1_HSTRING_iface, &impl->__FIVector_1_HSTRING_iface )

static HRESULT WINAPI hstring_vector_view_GetAt( __FIVectorView_1_HSTRING *iface, UINT32 index, HSTRING *value )
{
    return hstring_vector_GetAt( &impl_from_IVectorView_HSTRING( iface )->__FIVector_1_HSTRING_iface, index, value );
}

static HRESULT WINAPI hstring_vector_view_get_Size( __FIVectorView_1_HSTRING *iface, UINT32 *value )
{
    return hstring_vector_get_Size( &impl_from_IVectorView_HSTRING( iface )->__FIVector_1_HSTRING_iface, value );
}

static HRESULT WINAPI hstring_vector_view_IndexOf( __FIVectorView_1_HSTRING *iface, HSTRING element, UINT32 *index, boolean *value )
{
    return hstring_vector_IndexOf( &impl_from_IVectorView_HSTRING( iface )->__FIVector_1_HSTRING_iface, element, index, value );
}

static HRESULT WINAPI hstring_vector_view_GetMany( __FIVectorView_1_HSTRING *iface, UINT32 start, UINT32 size, HSTRING *items, UINT32 *count )
{
    return hstring_vector_GetMany( &impl_from_IVectorView_HSTRING( iface )->__FIVector_1_HSTRING_iface, start, size, items, count );
}

static const struct __FIVectorView_1_HSTRINGVtbl hstring_vector_view_vtbl =
{
    hstring_vector_view_QueryInterface,
    hstring_vector_view_AddRef,
    hstring_vector_view_Release,
    hstring_vector_view_GetIids,
    hstring_vector_view_GetRuntimeClassName,
    hstring_vector_view_GetTrustLevel,
    hstring_vector_view_GetAt,
    hstring_vector_view_get_Size,
    hstring_vector_view_IndexOf,
    hstring_vector_view_GetMany,
};

DEFINE_IINSPECTABLE_( hstring_iterable, __FIIterable_1_HSTRING, struct hstring_vector, impl_from_IIterable_HSTRING, __FIIterable_1_HSTRING_iface, &impl->__FIVector_1_HSTRING_iface )

static HRESULT WINAPI hstring_iterable_First( __FIIterable_1_HSTRING *iface, __FIIterator_1_HSTRING **value )
{
    struct hstring_vector *impl = impl_from_IIterable_HSTRING( iface );
    struct hstring_iterator *iter;

    if (!value) return E_POINTER;
    if (!(iter = calloc( 1, sizeof(*iter) ))) return E_OUTOFMEMORY;
    iter->__FIIterator_1_HSTRING_iface.lpVtbl = &hstring_iterator_vtbl;
    iter->ref = 1;
    iter->vec = impl;
    __FIVector_1_HSTRING_AddRef( &impl->__FIVector_1_HSTRING_iface );
    *value = &iter->__FIIterator_1_HSTRING_iface;
    return S_OK;
}

static const struct __FIIterable_1_HSTRINGVtbl hstring_iterable_vtbl =
{
    hstring_iterable_QueryInterface,
    hstring_iterable_AddRef,
    hstring_iterable_Release,
    hstring_iterable_GetIids,
    hstring_iterable_GetRuntimeClassName,
    hstring_iterable_GetTrustLevel,
    hstring_iterable_First,
};

static struct hstring_vector *hstring_vector_create( void )
{
    struct hstring_vector *impl = calloc( 1, sizeof(*impl) );
    if (!impl) return NULL;
    impl->__FIVector_1_HSTRING_iface.lpVtbl = &hstring_vector_vtbl;
    impl->__FIVectorView_1_HSTRING_iface.lpVtbl = &hstring_vector_view_vtbl;
    impl->__FIIterable_1_HSTRING_iface.lpVtbl = &hstring_iterable_vtbl;
    impl->ref = 1;
    InitializeCriticalSection( &impl->cs );
    return impl;
}

/* =========================================================================
 * IMap<HSTRING, IVector<HSTRING> *> for FileSavePicker.FileTypeChoices
 * ========================================================================= */

struct choices_entry
{
    HSTRING key;
    __FIVector_1_HSTRING *value;
};

struct choices_map
{
    __FIMap_2_HSTRING___FIVector_1_HSTRING __FIMap_2_HSTRING___FIVector_1_HSTRING_iface;
    __FIMapView_2_HSTRING___FIVector_1_HSTRING __FIMapView_2_HSTRING___FIVector_1_HSTRING_iface;
    __FIIterable_1___FIKeyValuePair_2_HSTRING___FIVector_1_HSTRING __FIIterable_iface;
    LONG ref;
    CRITICAL_SECTION cs;
    struct choices_entry *entries;
    UINT32 count;
    UINT32 capacity;
};

struct choices_kvp
{
    __FIKeyValuePair_2_HSTRING___FIVector_1_HSTRING __FIKeyValuePair_iface;
    LONG ref;
    HSTRING key;
    __FIVector_1_HSTRING *value;
};

struct choices_iterator
{
    __FIIterator_1___FIKeyValuePair_2_HSTRING___FIVector_1_HSTRING __FIIterator_iface;
    LONG ref;
    struct choices_map *map;
    UINT32 index;
};

static inline struct choices_map *impl_from_choices_map( __FIMap_2_HSTRING___FIVector_1_HSTRING *iface )
{
    return CONTAINING_RECORD( iface, struct choices_map, __FIMap_2_HSTRING___FIVector_1_HSTRING_iface );
}

static inline struct choices_kvp *impl_from_choices_kvp( __FIKeyValuePair_2_HSTRING___FIVector_1_HSTRING *iface )
{
    return CONTAINING_RECORD( iface, struct choices_kvp, __FIKeyValuePair_iface );
}

static inline struct choices_iterator *impl_from_choices_iterator( __FIIterator_1___FIKeyValuePair_2_HSTRING___FIVector_1_HSTRING *iface )
{
    return CONTAINING_RECORD( iface, struct choices_iterator, __FIIterator_iface );
}

static HRESULT WINAPI choices_kvp_QueryInterface( __FIKeyValuePair_2_HSTRING___FIVector_1_HSTRING *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID___FIKeyValuePair_2_HSTRING___FIVector_1_HSTRING ))
    {
        *out = iface;
        IInspectable_AddRef( (IInspectable *)iface );
        return S_OK;
    }
    return E_NOINTERFACE;
}

static ULONG WINAPI choices_kvp_AddRef( __FIKeyValuePair_2_HSTRING___FIVector_1_HSTRING *iface )
{
    return InterlockedIncrement( &impl_from_choices_kvp( iface )->ref );
}

static ULONG WINAPI choices_kvp_Release( __FIKeyValuePair_2_HSTRING___FIVector_1_HSTRING *iface )
{
    struct choices_kvp *impl = impl_from_choices_kvp( iface );
    LONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        WindowsDeleteString( impl->key );
        if (impl->value) __FIVector_1_HSTRING_Release( impl->value );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI choices_kvp_GetIids( __FIKeyValuePair_2_HSTRING___FIVector_1_HSTRING *iface, ULONG *c, IID **iids ) { return E_NOTIMPL; }
static HRESULT WINAPI choices_kvp_GetRuntimeClassName( __FIKeyValuePair_2_HSTRING___FIVector_1_HSTRING *iface, HSTRING *n ) { return E_NOTIMPL; }
static HRESULT WINAPI choices_kvp_GetTrustLevel( __FIKeyValuePair_2_HSTRING___FIVector_1_HSTRING *iface, TrustLevel *l )
{
    if (!l) return E_POINTER;
    *l = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI choices_kvp_get_Key( __FIKeyValuePair_2_HSTRING___FIVector_1_HSTRING *iface, HSTRING *value )
{
    return WindowsDuplicateString( impl_from_choices_kvp( iface )->key, value );
}

static HRESULT WINAPI choices_kvp_get_Value( __FIKeyValuePair_2_HSTRING___FIVector_1_HSTRING *iface, __FIVector_1_HSTRING **value )
{
    struct choices_kvp *impl = impl_from_choices_kvp( iface );
    if (!value) return E_POINTER;
    *value = impl->value;
    if (*value) __FIVector_1_HSTRING_AddRef( *value );
    return S_OK;
}

static const struct __FIKeyValuePair_2_HSTRING___FIVector_1_HSTRINGVtbl choices_kvp_vtbl =
{
    choices_kvp_QueryInterface,
    choices_kvp_AddRef,
    choices_kvp_Release,
    choices_kvp_GetIids,
    choices_kvp_GetRuntimeClassName,
    choices_kvp_GetTrustLevel,
    choices_kvp_get_Key,
    choices_kvp_get_Value,
};

static HRESULT choices_kvp_create( HSTRING key, __FIVector_1_HSTRING *val, __FIKeyValuePair_2_HSTRING___FIVector_1_HSTRING **out )
{
    struct choices_kvp *impl = calloc( 1, sizeof(*impl) );
    if (!impl) return E_OUTOFMEMORY;
    impl->__FIKeyValuePair_iface.lpVtbl = &choices_kvp_vtbl;
    impl->ref = 1;
    WindowsDuplicateString( key, &impl->key );
    impl->value = val;
    if (val) __FIVector_1_HSTRING_AddRef( val );
    *out = &impl->__FIKeyValuePair_iface;
    return S_OK;
}

static HRESULT WINAPI choices_iterator_QueryInterface( __FIIterator_1___FIKeyValuePair_2_HSTRING___FIVector_1_HSTRING *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID___FIIterator_1___FIKeyValuePair_2_HSTRING___FIVector_1_HSTRING ))
    {
        *out = iface;
        IInspectable_AddRef( (IInspectable *)iface );
        return S_OK;
    }
    return E_NOINTERFACE;
}

static ULONG WINAPI choices_iterator_AddRef( __FIIterator_1___FIKeyValuePair_2_HSTRING___FIVector_1_HSTRING *iface )
{
    return InterlockedIncrement( &impl_from_choices_iterator( iface )->ref );
}

static ULONG WINAPI choices_iterator_Release( __FIIterator_1___FIKeyValuePair_2_HSTRING___FIVector_1_HSTRING *iface )
{
    struct choices_iterator *impl = impl_from_choices_iterator( iface );
    LONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        __FIMap_2_HSTRING___FIVector_1_HSTRING_Release( &impl->map->__FIMap_2_HSTRING___FIVector_1_HSTRING_iface );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI choices_iterator_GetIids( __FIIterator_1___FIKeyValuePair_2_HSTRING___FIVector_1_HSTRING *iface, ULONG *c, IID **iids ) { return E_NOTIMPL; }
static HRESULT WINAPI choices_iterator_GetRuntimeClassName( __FIIterator_1___FIKeyValuePair_2_HSTRING___FIVector_1_HSTRING *iface, HSTRING *n ) { return E_NOTIMPL; }
static HRESULT WINAPI choices_iterator_GetTrustLevel( __FIIterator_1___FIKeyValuePair_2_HSTRING___FIVector_1_HSTRING *iface, TrustLevel *l )
{
    if (!l) return E_POINTER;
    *l = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI choices_iterator_get_Current( __FIIterator_1___FIKeyValuePair_2_HSTRING___FIVector_1_HSTRING *iface,
                                                   __FIKeyValuePair_2_HSTRING___FIVector_1_HSTRING **value )
{
    struct choices_iterator *impl = impl_from_choices_iterator( iface );
    HRESULT hr = E_BOUNDS;
    if (!value) return E_POINTER;
    *value = NULL;
    EnterCriticalSection( &impl->map->cs );
    if (impl->index < impl->map->count)
        hr = choices_kvp_create( impl->map->entries[impl->index].key, impl->map->entries[impl->index].value, value );
    LeaveCriticalSection( &impl->map->cs );
    return hr;
}

static HRESULT WINAPI choices_iterator_get_HasCurrent( __FIIterator_1___FIKeyValuePair_2_HSTRING___FIVector_1_HSTRING *iface, boolean *value )
{
    struct choices_iterator *impl = impl_from_choices_iterator( iface );
    if (!value) return E_POINTER;
    EnterCriticalSection( &impl->map->cs );
    *value = impl->index < impl->map->count;
    LeaveCriticalSection( &impl->map->cs );
    return S_OK;
}

static HRESULT WINAPI choices_iterator_MoveNext( __FIIterator_1___FIKeyValuePair_2_HSTRING___FIVector_1_HSTRING *iface, boolean *value )
{
    struct choices_iterator *impl = impl_from_choices_iterator( iface );
    if (!value) return E_POINTER;
    EnterCriticalSection( &impl->map->cs );
    if (impl->index < impl->map->count) ++impl->index;
    *value = impl->index < impl->map->count;
    LeaveCriticalSection( &impl->map->cs );
    return S_OK;
}

static HRESULT WINAPI choices_iterator_GetMany( __FIIterator_1___FIKeyValuePair_2_HSTRING___FIVector_1_HSTRING *iface, UINT32 size,
                                                __FIKeyValuePair_2_HSTRING___FIVector_1_HSTRING **items, UINT32 *count )
{
    struct choices_iterator *impl = impl_from_choices_iterator( iface );
    UINT32 n = 0;
    if (!count) return E_POINTER;
    EnterCriticalSection( &impl->map->cs );
    while (n < size && impl->index + n < impl->map->count)
    {
        if (!items) { LeaveCriticalSection( &impl->map->cs ); return E_POINTER; }
        choices_kvp_create( impl->map->entries[impl->index + n].key, impl->map->entries[impl->index + n].value, &items[n] );
        ++n;
    }
    impl->index += n;
    LeaveCriticalSection( &impl->map->cs );
    *count = n;
    return S_OK;
}

static const struct __FIIterator_1___FIKeyValuePair_2_HSTRING___FIVector_1_HSTRINGVtbl choices_iterator_vtbl =
{
    choices_iterator_QueryInterface,
    choices_iterator_AddRef,
    choices_iterator_Release,
    choices_iterator_GetIids,
    choices_iterator_GetRuntimeClassName,
    choices_iterator_GetTrustLevel,
    choices_iterator_get_Current,
    choices_iterator_get_HasCurrent,
    choices_iterator_MoveNext,
    choices_iterator_GetMany,
};

static int choices_map_find( struct choices_map *impl, HSTRING key )
{
    UINT32 i;
    INT32 cmp;
    for (i = 0; i < impl->count; ++i)
    {
        if (SUCCEEDED(WindowsCompareStringOrdinal( impl->entries[i].key, key, &cmp )) && cmp == 0)
            return (int)i;
    }
    return -1;
}

static HRESULT WINAPI choices_map_QueryInterface( __FIMap_2_HSTRING___FIVector_1_HSTRING *iface, REFIID iid, void **out )
{
    struct choices_map *impl = impl_from_choices_map( iface );
    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID___FIMap_2_HSTRING___FIVector_1_HSTRING ))
    {
        *out = &impl->__FIMap_2_HSTRING___FIVector_1_HSTRING_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID___FIMapView_2_HSTRING___FIVector_1_HSTRING ))
    {
        *out = &impl->__FIMapView_2_HSTRING___FIVector_1_HSTRING_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID___FIIterable_1___FIKeyValuePair_2_HSTRING___FIVector_1_HSTRING ))
    {
        *out = &impl->__FIIterable_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    return E_NOINTERFACE;
}

static ULONG WINAPI choices_map_AddRef( __FIMap_2_HSTRING___FIVector_1_HSTRING *iface )
{
    return InterlockedIncrement( &impl_from_choices_map( iface )->ref );
}

static ULONG WINAPI choices_map_Release( __FIMap_2_HSTRING___FIVector_1_HSTRING *iface )
{
    struct choices_map *impl = impl_from_choices_map( iface );
    LONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        UINT32 i;
        for (i = 0; i < impl->count; ++i)
        {
            WindowsDeleteString( impl->entries[i].key );
            if (impl->entries[i].value) __FIVector_1_HSTRING_Release( impl->entries[i].value );
        }
        free( impl->entries );
        DeleteCriticalSection( &impl->cs );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI choices_map_GetIids( __FIMap_2_HSTRING___FIVector_1_HSTRING *iface, ULONG *c, IID **iids ) { return E_NOTIMPL; }
static HRESULT WINAPI choices_map_GetRuntimeClassName( __FIMap_2_HSTRING___FIVector_1_HSTRING *iface, HSTRING *n ) { return E_NOTIMPL; }
static HRESULT WINAPI choices_map_GetTrustLevel( __FIMap_2_HSTRING___FIVector_1_HSTRING *iface, TrustLevel *l )
{
    if (!l) return E_POINTER;
    *l = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI choices_map_Lookup( __FIMap_2_HSTRING___FIVector_1_HSTRING *iface, HSTRING key, __FIVector_1_HSTRING **value )
{
    struct choices_map *impl = impl_from_choices_map( iface );
    int idx;
    if (!value) return E_POINTER;
    *value = NULL;
    EnterCriticalSection( &impl->cs );
    idx = choices_map_find( impl, key );
    if (idx >= 0)
    {
        *value = impl->entries[idx].value;
        if (*value) __FIVector_1_HSTRING_AddRef( *value );
    }
    LeaveCriticalSection( &impl->cs );
    return idx >= 0 ? S_OK : E_BOUNDS;
}

static HRESULT WINAPI choices_map_get_Size( __FIMap_2_HSTRING___FIVector_1_HSTRING *iface, UINT32 *size )
{
    struct choices_map *impl = impl_from_choices_map( iface );
    if (!size) return E_POINTER;
    EnterCriticalSection( &impl->cs );
    *size = impl->count;
    LeaveCriticalSection( &impl->cs );
    return S_OK;
}

static HRESULT WINAPI choices_map_HasKey( __FIMap_2_HSTRING___FIVector_1_HSTRING *iface, HSTRING key, boolean *found )
{
    struct choices_map *impl = impl_from_choices_map( iface );
    if (!found) return E_POINTER;
    EnterCriticalSection( &impl->cs );
    *found = choices_map_find( impl, key ) >= 0;
    LeaveCriticalSection( &impl->cs );
    return S_OK;
}

static HRESULT WINAPI choices_map_GetView( __FIMap_2_HSTRING___FIVector_1_HSTRING *iface, __FIMapView_2_HSTRING___FIVector_1_HSTRING **view )
{
    struct choices_map *impl = impl_from_choices_map( iface );
    if (!view) return E_POINTER;
    *view = &impl->__FIMapView_2_HSTRING___FIVector_1_HSTRING_iface;
    IInspectable_AddRef( (IInspectable *)*view );
    return S_OK;
}

static HRESULT WINAPI choices_map_Insert( __FIMap_2_HSTRING___FIVector_1_HSTRING *iface, HSTRING key, __FIVector_1_HSTRING *value, boolean *replaced )
{
    struct choices_map *impl = impl_from_choices_map( iface );
    int idx;
    HSTRING key_copy = NULL;
    HRESULT hr;

    if (!replaced) return E_POINTER;
    if (key && FAILED(hr = WindowsDuplicateString( key, &key_copy ))) return hr;
    EnterCriticalSection( &impl->cs );
    idx = choices_map_find( impl, key );
    if (idx >= 0)
    {
        if (key_copy) WindowsDeleteString( key_copy );
        if (impl->entries[idx].value) __FIVector_1_HSTRING_Release( impl->entries[idx].value );
        impl->entries[idx].value = value;
        if (value) __FIVector_1_HSTRING_AddRef( value );
        *replaced = TRUE;
    }
    else
    {
        if (impl->count == impl->capacity)
        {
            UINT32 new_cap = impl->capacity ? impl->capacity * 2 : 8;
            struct choices_entry *grown = realloc( impl->entries, new_cap * sizeof(*grown) );
            if (!grown)
            {
                LeaveCriticalSection( &impl->cs );
                if (key_copy) WindowsDeleteString( key_copy );
                return E_OUTOFMEMORY;
            }
            impl->entries = grown;
            impl->capacity = new_cap;
        }
        impl->entries[impl->count].key = key_copy;
        impl->entries[impl->count].value = value;
        if (value) __FIVector_1_HSTRING_AddRef( value );
        impl->count++;
        *replaced = FALSE;
    }
    LeaveCriticalSection( &impl->cs );
    return S_OK;
}

static HRESULT WINAPI choices_map_Remove( __FIMap_2_HSTRING___FIVector_1_HSTRING *iface, HSTRING key )
{
    struct choices_map *impl = impl_from_choices_map( iface );
    int idx;
    EnterCriticalSection( &impl->cs );
    idx = choices_map_find( impl, key );
    if (idx >= 0)
    {
        WindowsDeleteString( impl->entries[idx].key );
        if (impl->entries[idx].value) __FIVector_1_HSTRING_Release( impl->entries[idx].value );
        if ((UINT32)idx + 1 < impl->count)
            memmove( &impl->entries[idx], &impl->entries[idx + 1], (impl->count - idx - 1) * sizeof(*impl->entries) );
        impl->count--;
    }
    LeaveCriticalSection( &impl->cs );
    return idx >= 0 ? S_OK : E_BOUNDS;
}

static HRESULT WINAPI choices_map_Clear( __FIMap_2_HSTRING___FIVector_1_HSTRING *iface )
{
    struct choices_map *impl = impl_from_choices_map( iface );
    UINT32 i;
    EnterCriticalSection( &impl->cs );
    for (i = 0; i < impl->count; ++i)
    {
        WindowsDeleteString( impl->entries[i].key );
        if (impl->entries[i].value) __FIVector_1_HSTRING_Release( impl->entries[i].value );
    }
    impl->count = 0;
    LeaveCriticalSection( &impl->cs );
    return S_OK;
}

static const struct __FIMap_2_HSTRING___FIVector_1_HSTRINGVtbl choices_map_vtbl =
{
    choices_map_QueryInterface,
    choices_map_AddRef,
    choices_map_Release,
    choices_map_GetIids,
    choices_map_GetRuntimeClassName,
    choices_map_GetTrustLevel,
    choices_map_Lookup,
    choices_map_get_Size,
    choices_map_HasKey,
    choices_map_GetView,
    choices_map_Insert,
    choices_map_Remove,
    choices_map_Clear,
};

DEFINE_IINSPECTABLE_( choices_map_view, __FIMapView_2_HSTRING___FIVector_1_HSTRING, struct choices_map, impl_from_choices_map_view, __FIMapView_2_HSTRING___FIVector_1_HSTRING_iface, &impl->__FIMap_2_HSTRING___FIVector_1_HSTRING_iface )

static HRESULT WINAPI choices_map_view_Lookup( __FIMapView_2_HSTRING___FIVector_1_HSTRING *iface, HSTRING key, __FIVector_1_HSTRING **value )
{
    return choices_map_Lookup( &impl_from_choices_map_view( iface )->__FIMap_2_HSTRING___FIVector_1_HSTRING_iface, key, value );
}

static HRESULT WINAPI choices_map_view_get_Size( __FIMapView_2_HSTRING___FIVector_1_HSTRING *iface, UINT32 *size )
{
    return choices_map_get_Size( &impl_from_choices_map_view( iface )->__FIMap_2_HSTRING___FIVector_1_HSTRING_iface, size );
}

static HRESULT WINAPI choices_map_view_HasKey( __FIMapView_2_HSTRING___FIVector_1_HSTRING *iface, HSTRING key, boolean *found )
{
    return choices_map_HasKey( &impl_from_choices_map_view( iface )->__FIMap_2_HSTRING___FIVector_1_HSTRING_iface, key, found );
}

static HRESULT WINAPI choices_map_view_Split( __FIMapView_2_HSTRING___FIVector_1_HSTRING *iface,
                                            __FIMapView_2_HSTRING___FIVector_1_HSTRING **first,
                                            __FIMapView_2_HSTRING___FIVector_1_HSTRING **second )
{
    if (!first || !second) return E_POINTER;
    *first = NULL;
    *second = NULL;
    return S_OK;
}

static const struct __FIMapView_2_HSTRING___FIVector_1_HSTRINGVtbl choices_map_view_vtbl =
{
    choices_map_view_QueryInterface,
    choices_map_view_AddRef,
    choices_map_view_Release,
    choices_map_view_GetIids,
    choices_map_view_GetRuntimeClassName,
    choices_map_view_GetTrustLevel,
    choices_map_view_Lookup,
    choices_map_view_get_Size,
    choices_map_view_HasKey,
    choices_map_view_Split,
};

DEFINE_IINSPECTABLE_( choices_iterable, __FIIterable_1___FIKeyValuePair_2_HSTRING___FIVector_1_HSTRING, struct choices_map, impl_from_choices_iterable, __FIIterable_iface, &impl->__FIMap_2_HSTRING___FIVector_1_HSTRING_iface )

static HRESULT WINAPI choices_iterable_First( __FIIterable_1___FIKeyValuePair_2_HSTRING___FIVector_1_HSTRING *iface,
                                              __FIIterator_1___FIKeyValuePair_2_HSTRING___FIVector_1_HSTRING **value )
{
    struct choices_map *impl = impl_from_choices_iterable( iface );
    struct choices_iterator *iter;

    if (!value) return E_POINTER;
    if (!(iter = calloc( 1, sizeof(*iter) ))) return E_OUTOFMEMORY;
    iter->__FIIterator_iface.lpVtbl = &choices_iterator_vtbl;
    iter->ref = 1;
    iter->map = impl;
    __FIMap_2_HSTRING___FIVector_1_HSTRING_AddRef( &impl->__FIMap_2_HSTRING___FIVector_1_HSTRING_iface );
    *value = &iter->__FIIterator_iface;
    return S_OK;
}

static const struct __FIIterable_1___FIKeyValuePair_2_HSTRING___FIVector_1_HSTRINGVtbl choices_iterable_vtbl =
{
    choices_iterable_QueryInterface,
    choices_iterable_AddRef,
    choices_iterable_Release,
    choices_iterable_GetIids,
    choices_iterable_GetRuntimeClassName,
    choices_iterable_GetTrustLevel,
    choices_iterable_First,
};

static struct choices_map *choices_map_create( void )
{
    struct choices_map *impl = calloc( 1, sizeof(*impl) );
    if (!impl) return NULL;
    impl->__FIMap_2_HSTRING___FIVector_1_HSTRING_iface.lpVtbl = &choices_map_vtbl;
    impl->__FIMapView_2_HSTRING___FIVector_1_HSTRING_iface.lpVtbl = &choices_map_view_vtbl;
    impl->__FIIterable_iface.lpVtbl = &choices_iterable_vtbl;
    impl->ref = 1;
    InitializeCriticalSection( &impl->cs );
    return impl;
}

/* =========================================================================
 * IVectorView<StorageFile *> + IIterable<StorageFile *> for PickMultipleFilesAsync
 * ========================================================================= */

struct file_vector_view
{
    IVectorView_StorageFile IVectorView_StorageFile_iface;
    __FIIterable_1_Windows__CStorage__CStorageFile __FIIterable_iface;
    LONG ref;
    IStorageFile **items;
    UINT32 count;
};

struct file_iterator
{
    __FIIterator_1_Windows__CStorage__CStorageFile __FIIterator_iface;
    LONG ref;
    struct file_vector_view *view;
    UINT32 index;
};

static inline struct file_vector_view *impl_from_file_vector_view( IVectorView_StorageFile *iface )
{
    return CONTAINING_RECORD( iface, struct file_vector_view, IVectorView_StorageFile_iface );
}

static inline struct file_iterator *impl_from_file_iterator( __FIIterator_1_Windows__CStorage__CStorageFile *iface )
{
    return CONTAINING_RECORD( iface, struct file_iterator, __FIIterator_iface );
}

static HRESULT WINAPI file_iterator_QueryInterface( __FIIterator_1_Windows__CStorage__CStorageFile *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID___FIIterator_1_Windows__CStorage__CStorageFile ))
    {
        *out = iface;
        IInspectable_AddRef( (IInspectable *)iface );
        return S_OK;
    }
    return E_NOINTERFACE;
}

static ULONG WINAPI file_iterator_AddRef( __FIIterator_1_Windows__CStorage__CStorageFile *iface )
{
    return InterlockedIncrement( &impl_from_file_iterator( iface )->ref );
}

static ULONG WINAPI file_iterator_Release( __FIIterator_1_Windows__CStorage__CStorageFile *iface )
{
    struct file_iterator *impl = impl_from_file_iterator( iface );
    LONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        IVectorView_StorageFile_Release( &impl->view->IVectorView_StorageFile_iface );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI file_iterator_GetIids( __FIIterator_1_Windows__CStorage__CStorageFile *iface, ULONG *c, IID **iids ) { return E_NOTIMPL; }
static HRESULT WINAPI file_iterator_GetRuntimeClassName( __FIIterator_1_Windows__CStorage__CStorageFile *iface, HSTRING *n ) { return E_NOTIMPL; }
static HRESULT WINAPI file_iterator_GetTrustLevel( __FIIterator_1_Windows__CStorage__CStorageFile *iface, TrustLevel *l )
{
    if (!l) return E_POINTER;
    *l = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI file_iterator_get_Current( __FIIterator_1_Windows__CStorage__CStorageFile *iface, IStorageFile **value )
{
    struct file_iterator *impl = impl_from_file_iterator( iface );
    if (!value) return E_POINTER;
    *value = NULL;
    if (impl->index >= impl->view->count) return E_BOUNDS;
    *value = impl->view->items[impl->index];
    IStorageFile_AddRef( *value );
    return S_OK;
}

static HRESULT WINAPI file_iterator_get_HasCurrent( __FIIterator_1_Windows__CStorage__CStorageFile *iface, boolean *value )
{
    struct file_iterator *impl = impl_from_file_iterator( iface );
    if (!value) return E_POINTER;
    *value = impl->index < impl->view->count;
    return S_OK;
}

static HRESULT WINAPI file_iterator_MoveNext( __FIIterator_1_Windows__CStorage__CStorageFile *iface, boolean *value )
{
    struct file_iterator *impl = impl_from_file_iterator( iface );
    if (!value) return E_POINTER;
    if (impl->index < impl->view->count) ++impl->index;
    *value = impl->index < impl->view->count;
    return S_OK;
}

static HRESULT WINAPI file_iterator_GetMany( __FIIterator_1_Windows__CStorage__CStorageFile *iface, UINT32 size, IStorageFile **items, UINT32 *count )
{
    struct file_iterator *impl = impl_from_file_iterator( iface );
    UINT32 n = 0;
    if (!count) return E_POINTER;
    while (n < size && impl->index + n < impl->view->count)
    {
        if (!items) return E_POINTER;
        items[n] = impl->view->items[impl->index + n];
        IStorageFile_AddRef( items[n] );
        ++n;
    }
    impl->index += n;
    *count = n;
    return S_OK;
}

static const struct __FIIterator_1_Windows__CStorage__CStorageFileVtbl file_iterator_vtbl =
{
    file_iterator_QueryInterface,
    file_iterator_AddRef,
    file_iterator_Release,
    file_iterator_GetIids,
    file_iterator_GetRuntimeClassName,
    file_iterator_GetTrustLevel,
    file_iterator_get_Current,
    file_iterator_get_HasCurrent,
    file_iterator_MoveNext,
    file_iterator_GetMany,
};

static HRESULT WINAPI file_vector_view_QueryInterface( IVectorView_StorageFile *iface, REFIID iid, void **out )
{
    struct file_vector_view *impl = impl_from_file_vector_view( iface );
    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IVectorView_StorageFile ))
    {
        *out = &impl->IVectorView_StorageFile_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID___FIIterable_1_Windows__CStorage__CStorageFile ))
    {
        *out = &impl->__FIIterable_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    return E_NOINTERFACE;
}

static ULONG WINAPI file_vector_view_AddRef( IVectorView_StorageFile *iface )
{
    return InterlockedIncrement( &impl_from_file_vector_view( iface )->ref );
}

static ULONG WINAPI file_vector_view_Release( IVectorView_StorageFile *iface )
{
    struct file_vector_view *impl = impl_from_file_vector_view( iface );
    LONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        UINT32 i;
        for (i = 0; i < impl->count; ++i) IStorageFile_Release( impl->items[i] );
        free( impl->items );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI file_vector_view_GetIids( IVectorView_StorageFile *iface, ULONG *c, IID **iids ) { return E_NOTIMPL; }
static HRESULT WINAPI file_vector_view_GetRuntimeClassName( IVectorView_StorageFile *iface, HSTRING *n ) { return E_NOTIMPL; }
static HRESULT WINAPI file_vector_view_GetTrustLevel( IVectorView_StorageFile *iface, TrustLevel *l )
{
    if (!l) return E_POINTER;
    *l = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI file_vector_view_GetAt( IVectorView_StorageFile *iface, UINT32 index, IStorageFile **value )
{
    struct file_vector_view *impl = impl_from_file_vector_view( iface );
    if (!value) return E_POINTER;
    *value = NULL;
    if (index >= impl->count) return E_BOUNDS;
    *value = impl->items[index];
    IStorageFile_AddRef( *value );
    return S_OK;
}

static HRESULT WINAPI file_vector_view_get_Size( IVectorView_StorageFile *iface, UINT32 *size )
{
    if (!size) return E_POINTER;
    *size = impl_from_file_vector_view( iface )->count;
    return S_OK;
}

static HRESULT WINAPI file_vector_view_IndexOf( IVectorView_StorageFile *iface, IStorageFile *element, UINT32 *index, boolean *value )
{
    struct file_vector_view *impl = impl_from_file_vector_view( iface );
    UINT32 i;
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

static HRESULT WINAPI file_vector_view_GetMany( IVectorView_StorageFile *iface, UINT32 start, UINT32 size, IStorageFile **items, UINT32 *count )
{
    struct file_vector_view *impl = impl_from_file_vector_view( iface );
    UINT32 n = 0;
    if (!count) return E_POINTER;
    while (n < size && start + n < impl->count)
    {
        if (!items) return E_POINTER;
        items[n] = impl->items[start + n];
        IStorageFile_AddRef( items[n] );
        ++n;
    }
    *count = n;
    return S_OK;
}

static const struct IVectorView_StorageFileVtbl file_vector_view_vtbl =
{
    file_vector_view_QueryInterface,
    file_vector_view_AddRef,
    file_vector_view_Release,
    file_vector_view_GetIids,
    file_vector_view_GetRuntimeClassName,
    file_vector_view_GetTrustLevel,
    file_vector_view_GetAt,
    file_vector_view_get_Size,
    file_vector_view_IndexOf,
    file_vector_view_GetMany,
};

DEFINE_IINSPECTABLE_( file_iterable, __FIIterable_1_Windows__CStorage__CStorageFile, struct file_vector_view, impl_from_file_iterable, __FIIterable_iface, &impl->IVectorView_StorageFile_iface )

static HRESULT WINAPI file_iterable_First( __FIIterable_1_Windows__CStorage__CStorageFile *iface, __FIIterator_1_Windows__CStorage__CStorageFile **value )
{
    struct file_vector_view *impl = impl_from_file_iterable( iface );
    struct file_iterator *iter;
    if (!value) return E_POINTER;
    if (!(iter = calloc( 1, sizeof(*iter) ))) return E_OUTOFMEMORY;
    iter->__FIIterator_iface.lpVtbl = &file_iterator_vtbl;
    iter->ref = 1;
    iter->view = impl;
    IVectorView_StorageFile_AddRef( &impl->IVectorView_StorageFile_iface );
    *value = &iter->__FIIterator_iface;
    return S_OK;
}

static const struct __FIIterable_1_Windows__CStorage__CStorageFileVtbl file_iterable_vtbl =
{
    file_iterable_QueryInterface,
    file_iterable_AddRef,
    file_iterable_Release,
    file_iterable_GetIids,
    file_iterable_GetRuntimeClassName,
    file_iterable_GetTrustLevel,
    file_iterable_First,
};

static HRESULT file_vector_view_create( IStorageFile **items, UINT32 count, IVectorView_StorageFile **out )
{
    struct file_vector_view *impl = calloc( 1, sizeof(*impl) );
    if (!impl)
    {
        UINT32 i;
        for (i = 0; i < count; ++i) IStorageFile_Release( items[i] );
        free( items );
        return E_OUTOFMEMORY;
    }
    impl->IVectorView_StorageFile_iface.lpVtbl = &file_vector_view_vtbl;
    impl->__FIIterable_iface.lpVtbl = &file_iterable_vtbl;
    impl->ref = 1;
    impl->items = items;
    impl->count = count;
    *out = &impl->IVectorView_StorageFile_iface;
    return S_OK;
}

/* =========================================================================
 * Picker Location + Filter Builders + Portal/Win32 Invocation
 * ========================================================================= */

static WCHAR *resolve_start_location_w( PickerLocationId loc )
{
    const KNOWNFOLDERID *fid = NULL;
    PWSTR path = NULL;
    WCHAR *copy = NULL;

    switch (loc)
    {
    case PickerLocationId_DocumentsLibrary: fid = &kfid_Documents; break;
    case PickerLocationId_Desktop:          fid = &kfid_Desktop; break;
    case PickerLocationId_Downloads:        fid = &kfid_Downloads; break;
    case PickerLocationId_MusicLibrary:     fid = &kfid_Music; break;
    case PickerLocationId_PicturesLibrary:  fid = &kfid_Pictures; break;
    case PickerLocationId_VideosLibrary:    fid = &kfid_Videos; break;
    case PickerLocationId_Objects3D:        fid = &kfid_Objects3D; break;
    default: return NULL;
    }

    if (SUCCEEDED(SHGetKnownFolderPath( fid, 0, NULL, &path )) && path)
    {
        copy = wcsdup( path );
        CoTaskMemFree( path );
    }
    return copy;
}

static char *build_portal_filters_from_vector( struct hstring_vector *vec )
{
    size_t cap = 256, pos = 0;
    char *buf;
    UINT32 i;
    static const char label[] = "Files";

    if (!vec || !vec->count) return NULL;
    if (!(buf = malloc( cap ))) return NULL;

    memcpy( buf, label, sizeof(label) );
    pos = sizeof(label);

    EnterCriticalSection( &vec->cs );
    for (i = 0; i < vec->count; ++i)
    {
        char *ext = hstring_to_utf8( vec->items[i] );
        size_t need;
        if (!ext || !*ext) { free( ext ); continue; }

        /* ".png" -> "*.png", "*" -> "*" */
        need = strlen( ext ) + 4;
        if (pos + need + 4 >= cap)
        {
            char *grown;
            cap = (pos + need + 64) * 2;
            grown = realloc( buf, cap );
            if (!grown) { free( ext ); LeaveCriticalSection( &vec->cs ); free( buf ); return NULL; }
            buf = grown;
        }
        if (i > 0 && pos > sizeof(label)) buf[pos++] = ';';
        if (ext[0] == '.') buf[pos++] = '*';
        memcpy( buf + pos, ext, strlen( ext ) );
        pos += strlen( ext );
        free( ext );
    }
    LeaveCriticalSection( &vec->cs );

    if (pos == sizeof(label)) { free( buf ); return NULL; }
    buf[pos++] = '\0';
    buf[pos++] = '\0';
    return buf;
}

static char *build_portal_filters_from_map( struct choices_map *map )
{
    size_t cap = 512, pos = 0;
    char *buf;
    UINT32 i;

    if (!map || !map->count) return NULL;
    if (!(buf = malloc( cap ))) return NULL;

    EnterCriticalSection( &map->cs );
    for (i = 0; i < map->count; ++i)
    {
        char *label = hstring_to_utf8( map->entries[i].key );
        __FIVector_1_HSTRING *exts = map->entries[i].value;
        UINT32 count = 0, j;
        size_t l_len;

        if (!label || !*label || !exts) { free( label ); continue; }
        __FIVector_1_HSTRING_get_Size( exts, &count );
        if (!count) { free( label ); continue; }

        l_len = strlen( label ) + 1;
        if (pos + l_len + 64 >= cap)
        {
            char *grown;
            cap = (pos + l_len + 128) * 2;
            grown = realloc( buf, cap );
            if (!grown) { free( label ); LeaveCriticalSection( &map->cs ); free( buf ); return NULL; }
            buf = grown;
        }
        memcpy( buf + pos, label, l_len );
        pos += l_len;
        free( label );

        for (j = 0; j < count; ++j)
        {
            HSTRING h_ext = NULL;
            char *ext;
            size_t need;
            if (FAILED(__FIVector_1_HSTRING_GetAt( exts, j, &h_ext ))) continue;
            ext = hstring_to_utf8( h_ext );
            WindowsDeleteString( h_ext );
            if (!ext || !*ext) { free( ext ); continue; }
            need = strlen( ext ) + 4;
            if (pos + need + 4 >= cap)
            {
                char *grown;
                cap = (pos + need + 64) * 2;
                grown = realloc( buf, cap );
                if (!grown) { free( ext ); LeaveCriticalSection( &map->cs ); free( buf ); return NULL; }
                buf = grown;
            }
            if (j > 0) buf[pos++] = ';';
            if (ext[0] == '.') buf[pos++] = '*';
            memcpy( buf + pos, ext, strlen( ext ) );
            pos += strlen( ext );
            free( ext );
        }
        buf[pos++] = '\0';
    }
    LeaveCriticalSection( &map->cs );

    if (!pos) { free( buf ); return NULL; }
    buf[pos++] = '\0';
    return buf;
}

static BOOL try_portal_pick( int mode, const WCHAR *title_w, HSTRING accept_label_h,
                             HSTRING current_name_h, PickerLocationId start_loc,
                             const char *filters, WCHAR ***out_paths, UINT32 *out_count )
{
    struct portal_pick_params params;
    char out_buf[16384];
    char *accept_utf8 = NULL, *name_utf8 = NULL, *folder_utf8 = NULL, *title_utf8 = NULL;
    WCHAR *start_w = NULL;
    NTSTATUS status;

    *out_paths = NULL;
    *out_count = 0;

    if (__wine_init_unix_call()) return FALSE;
    if (WINE_UNIX_CALL( unix_portal_is_enabled, NULL ) != STATUS_SUCCESS) return FALSE;

    title_utf8 = wide_to_utf8( title_w );
    accept_utf8 = hstring_to_utf8( accept_label_h );
    name_utf8 = hstring_to_utf8( current_name_h );
    if ((start_w = resolve_start_location_w( start_loc )))
    {
        folder_utf8 = wine_get_unix_file_name( start_w );
        free( start_w );
    }

    memset( &params, 0, sizeof(params) );
    params.mode = mode;
    params.title = title_utf8;
    params.accept_label = accept_utf8;
    params.current_name = name_utf8;
    params.current_folder = folder_utf8;
    params.filters = filters;
    params.out_paths = out_buf;
    params.out_size = sizeof(out_buf);

    status = WINE_UNIX_CALL( unix_portal_pick, &params );

    free( title_utf8 );
    free( accept_utf8 );
    free( name_utf8 );
    if (folder_utf8) HeapFree( GetProcessHeap(), 0, folder_utf8 );

    if (status == STATUS_CANCELLED)
        return TRUE; /* handled by portal, user cancelled */

    if (status != STATUS_SUCCESS || !params.out_count)
        return FALSE; /* fall back to Win32 dialog if portal failed/unavailable */

    {
        WCHAR **dos_paths = calloc( params.out_count, sizeof(*dos_paths) );
        UINT32 n = 0;
        const char *p = out_buf;

        if (!dos_paths) return TRUE;
        while (*p && n < params.out_count)
        {
            WCHAR *dos = wine_get_dos_file_name( p );
            if (dos)
            {
                dos_paths[n++] = wcsdup( dos );
                HeapFree( GetProcessHeap(), 0, dos );
            }
            p += strlen( p ) + 1;
        }
        *out_paths = dos_paths;
        *out_count = n;
    }
    return TRUE;
}

static void free_dos_paths( WCHAR **paths, UINT32 count )
{
    UINT32 i;
    if (!paths) return;
    for (i = 0; i < count; ++i) free( paths[i] );
    free( paths );
}

static void win32_pick_files( HWND owner, BOOL multi, BOOL save, PickerLocationId start_loc,
                              HSTRING suggested_name, WCHAR ***out_paths, UINT32 *out_count )
{
    WCHAR buf[4096];
    OPENFILENAMEW ofn;
    WCHAR *init_dir = resolve_start_location_w( start_loc );
    BOOL ok;

    *out_paths = NULL;
    *out_count = 0;
    buf[0] = 0;

    if (save && suggested_name)
    {
        const WCHAR *raw = WindowsGetStringRawBuffer( suggested_name, NULL );
        if (raw) lstrcpynW( buf, raw, ARRAY_SIZE(buf) );
    }

    memset( &ofn, 0, sizeof(ofn) );
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"All Files (*.*)\0*.*\0";
    ofn.lpstrFile = buf;
    ofn.nMaxFile = ARRAY_SIZE(buf);
    ofn.lpstrInitialDir = init_dir;
    ofn.Flags = OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST;
    if (save) ofn.Flags |= OFN_OVERWRITEPROMPT;
    else ofn.Flags |= OFN_FILEMUSTEXIST | (multi ? (OFN_ALLOWMULTISELECT | OFN_EXPLORER) : 0);

    ok = save ? GetSaveFileNameW( &ofn ) : GetOpenFileNameW( &ofn );
    free( init_dir );
    if (!ok) return;

    if (!multi)
    {
        WCHAR **arr = calloc( 1, sizeof(*arr) );
        if (arr) { arr[0] = wcsdup( buf ); *out_paths = arr; *out_count = 1; }
    }
    else
    {
        size_t dir_len = wcslen( buf );
        const WCHAR *p = buf + dir_len + 1;
        if (!*p)
        {
            WCHAR **arr = calloc( 1, sizeof(*arr) );
            if (arr) { arr[0] = wcsdup( buf ); *out_paths = arr; *out_count = 1; }
        }
        else
        {
            UINT32 count = 0, cap = 8;
            WCHAR **arr = calloc( cap, sizeof(*arr) );
            while (*p && arr)
            {
                WCHAR *full = malloc( (dir_len + 1 + wcslen( p ) + 1) * sizeof(WCHAR) );
                if (full)
                {
                    swprintf( full, dir_len + 1 + wcslen( p ) + 1, L"%s\\%s", buf, p );
                    if (count == cap)
                    {
                        cap *= 2;
                        arr = realloc( arr, cap * sizeof(*arr) );
                    }
                    if (arr) arr[count++] = full;
                    else free( full );
                }
                p += wcslen( p ) + 1;
            }
            *out_paths = arr;
            *out_count = count;
        }
    }
}

static WCHAR *win32_pick_folder( HWND owner )
{
    BROWSEINFOW bi;
    PIDLIST_ABSOLUTE pidl;
    WCHAR path[MAX_PATH];

    memset( &bi, 0, sizeof(bi) );
    bi.hwndOwner = owner;
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    pidl = SHBrowseForFolderW( &bi );
    if (!pidl) return NULL;
    if (SHGetPathFromIDListW( pidl, path ))
    {
        CoTaskMemFree( pidl );
        return wcsdup( path );
    }
    CoTaskMemFree( pidl );
    return NULL;
}

/* =========================================================================
 * FileOpenPicker
 * ========================================================================= */

struct file_open_picker
{
    IFileOpenPicker IFileOpenPicker_iface;
    IInitializeWithWindowLocal IInitializeWithWindow_iface;
    LONG ref;
    HWND owner;
    PickerViewMode view_mode;
    PickerLocationId start_location;
    HSTRING settings_id;
    HSTRING commit_button_text;
    struct hstring_vector *filters;
};

static inline struct file_open_picker *impl_from_IFileOpenPicker( IFileOpenPicker *iface )
{
    return CONTAINING_RECORD( iface, struct file_open_picker, IFileOpenPicker_iface );
}

static inline struct file_open_picker *impl_from_open_init_window( IInitializeWithWindowLocal *iface )
{
    return CONTAINING_RECORD( iface, struct file_open_picker, IInitializeWithWindow_iface );
}

static HRESULT WINAPI file_open_picker_QueryInterface( IFileOpenPicker *iface, REFIID iid, void **out )
{
    struct file_open_picker *impl = impl_from_IFileOpenPicker( iface );
    if (!out) return E_POINTER;
    *out = NULL;

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IFileOpenPicker ))
    {
        *out = &impl->IFileOpenPicker_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IInitializeWithWindow_local ))
    {
        *out = &impl->IInitializeWithWindow_iface;
        IUnknown_AddRef( (IUnknown *)*out );
        return S_OK;
    }
    FIXME( "iid %s not implemented\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG WINAPI file_open_picker_AddRef( IFileOpenPicker *iface )
{
    return InterlockedIncrement( &impl_from_IFileOpenPicker( iface )->ref );
}

static ULONG WINAPI file_open_picker_Release( IFileOpenPicker *iface )
{
    struct file_open_picker *impl = impl_from_IFileOpenPicker( iface );
    LONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        WindowsDeleteString( impl->settings_id );
        WindowsDeleteString( impl->commit_button_text );
        if (impl->filters) __FIVector_1_HSTRING_Release( &impl->filters->__FIVector_1_HSTRING_iface );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI file_open_picker_GetIids( IFileOpenPicker *iface, ULONG *count, IID **iids )
{
    static const IID iids_array[] =
    {
        { 0x2ca8278a, 0x12c5, 0x4c5f, { 0x89,0x77, 0x94,0x54,0x77,0x93,0xc2,0x41 } },
    };
    if (!count || !iids) return E_POINTER;
    if (!(*iids = CoTaskMemAlloc( sizeof(iids_array) ))) return E_OUTOFMEMORY;
    memcpy( *iids, iids_array, sizeof(iids_array) );
    *count = ARRAY_SIZE(iids_array);
    return S_OK;
}

static HRESULT WINAPI file_open_picker_GetRuntimeClassName( IFileOpenPicker *iface, HSTRING *name )
{
    if (!name) return E_POINTER;
    return WindowsCreateString( RuntimeClass_Windows_Storage_Pickers_FileOpenPicker,
                                ARRAY_SIZE(RuntimeClass_Windows_Storage_Pickers_FileOpenPicker) - 1, name );
}

static HRESULT WINAPI file_open_picker_GetTrustLevel( IFileOpenPicker *iface, TrustLevel *level )
{
    if (!level) return E_POINTER;
    *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI file_open_picker_get_ViewMode( IFileOpenPicker *iface, PickerViewMode *value )
{
    if (!value) return E_POINTER;
    *value = impl_from_IFileOpenPicker( iface )->view_mode;
    return S_OK;
}

static HRESULT WINAPI file_open_picker_put_ViewMode( IFileOpenPicker *iface, PickerViewMode value )
{
    impl_from_IFileOpenPicker( iface )->view_mode = value;
    return S_OK;
}

static HRESULT WINAPI file_open_picker_get_SettingsIdentifier( IFileOpenPicker *iface, HSTRING *value )
{
    return duplicate_hstring_out( impl_from_IFileOpenPicker( iface )->settings_id, value );
}

static HRESULT WINAPI file_open_picker_put_SettingsIdentifier( IFileOpenPicker *iface, HSTRING value )
{
    return replace_hstring( &impl_from_IFileOpenPicker( iface )->settings_id, value );
}

static HRESULT WINAPI file_open_picker_get_SuggestedStartLocation( IFileOpenPicker *iface, PickerLocationId *value )
{
    if (!value) return E_POINTER;
    *value = impl_from_IFileOpenPicker( iface )->start_location;
    return S_OK;
}

static HRESULT WINAPI file_open_picker_put_SuggestedStartLocation( IFileOpenPicker *iface, PickerLocationId value )
{
    impl_from_IFileOpenPicker( iface )->start_location = value;
    return S_OK;
}

static HRESULT WINAPI file_open_picker_get_CommitButtonText( IFileOpenPicker *iface, HSTRING *value )
{
    return duplicate_hstring_out( impl_from_IFileOpenPicker( iface )->commit_button_text, value );
}

static HRESULT WINAPI file_open_picker_put_CommitButtonText( IFileOpenPicker *iface, HSTRING value )
{
    return replace_hstring( &impl_from_IFileOpenPicker( iface )->commit_button_text, value );
}

static HRESULT WINAPI file_open_picker_get_FileTypeFilter( IFileOpenPicker *iface, __FIVector_1_HSTRING **value )
{
    struct file_open_picker *impl = impl_from_IFileOpenPicker( iface );
    if (!value) return E_POINTER;
    *value = &impl->filters->__FIVector_1_HSTRING_iface;
    __FIVector_1_HSTRING_AddRef( *value );
    return S_OK;
}

static HRESULT WINAPI file_open_picker_PickSingleFileAsync( IFileOpenPicker *iface, IAsyncOperation_StorageFile **operation )
{
    struct file_open_picker *impl = impl_from_IFileOpenPicker( iface );
    WCHAR **paths = NULL;
    UINT32 count = 0;
    char *filters;
    IStorageFile *file = NULL;
    HRESULT hr;

    TRACE( "iface %p, operation %p.\n", iface, operation );
    if (!operation) return E_POINTER;

    filters = build_portal_filters_from_vector( impl->filters );
    if (!try_portal_pick( PORTAL_PICKER_OPEN_SINGLE, L"Open File", impl->commit_button_text,
                          NULL, impl->start_location, filters, &paths, &count ))
    {
        win32_pick_files( impl->owner, FALSE, FALSE, impl->start_location, NULL, &paths, &count );
    }
    free( filters );

    if (count && paths && paths[0])
        storage_file_create( paths[0], &file );
    free_dos_paths( paths, count );

    hr = async_operation_inspectable_create( &IID_IAsyncOperation_StorageFile,
                                             L"Windows.Foundation.IAsyncOperation`1<Windows.Storage.StorageFile>",
                                             (IInspectable *)file, (void **)operation );
    if (file) IStorageFile_Release( file );
    return hr;
}

static HRESULT WINAPI file_open_picker_PickMultipleFilesAsync( IFileOpenPicker *iface,
                                                               IAsyncOperation_IVectorView_StorageFile **operation )
{
    struct file_open_picker *impl = impl_from_IFileOpenPicker( iface );
    WCHAR **paths = NULL;
    UINT32 count = 0, i, valid = 0;
    char *filters;
    IStorageFile **files = NULL;
    IVectorView_StorageFile *view = NULL;
    HRESULT hr;

    TRACE( "iface %p, operation %p.\n", iface, operation );
    if (!operation) return E_POINTER;

    filters = build_portal_filters_from_vector( impl->filters );
    if (!try_portal_pick( PORTAL_PICKER_OPEN_MULTIPLE, L"Open Files", impl->commit_button_text,
                          NULL, impl->start_location, filters, &paths, &count ))
    {
        win32_pick_files( impl->owner, TRUE, FALSE, impl->start_location, NULL, &paths, &count );
    }
    free( filters );

    if (count && paths)
    {
        files = calloc( count, sizeof(*files) );
        if (files)
        {
            for (i = 0; i < count; ++i)
            {
                if (paths[i] && SUCCEEDED(storage_file_create( paths[i], &files[valid] )))
                    ++valid;
            }
        }
    }
    free_dos_paths( paths, count );

    if (FAILED(hr = file_vector_view_create( files, valid, &view )))
        return hr;

    hr = async_operation_inspectable_create( &IID_IAsyncOperation_IVectorView_StorageFile,
                                             L"Windows.Foundation.IAsyncOperation`1<Windows.Foundation.Collections.IVectorView`1<Windows.Storage.StorageFile>>",
                                             (IInspectable *)view, (void **)operation );
    IVectorView_StorageFile_Release( view );
    return hr;
}

static const struct IFileOpenPickerVtbl file_open_picker_vtbl =
{
    file_open_picker_QueryInterface,
    file_open_picker_AddRef,
    file_open_picker_Release,
    file_open_picker_GetIids,
    file_open_picker_GetRuntimeClassName,
    file_open_picker_GetTrustLevel,
    file_open_picker_get_ViewMode,
    file_open_picker_put_ViewMode,
    file_open_picker_get_SettingsIdentifier,
    file_open_picker_put_SettingsIdentifier,
    file_open_picker_get_SuggestedStartLocation,
    file_open_picker_put_SuggestedStartLocation,
    file_open_picker_get_CommitButtonText,
    file_open_picker_put_CommitButtonText,
    file_open_picker_get_FileTypeFilter,
    file_open_picker_PickSingleFileAsync,
    file_open_picker_PickMultipleFilesAsync,
};

static HRESULT WINAPI open_init_window_QueryInterface( IInitializeWithWindowLocal *iface, REFIID iid, void **out )
{
    return file_open_picker_QueryInterface( &impl_from_open_init_window( iface )->IFileOpenPicker_iface, iid, out );
}
static ULONG WINAPI open_init_window_AddRef( IInitializeWithWindowLocal *iface )
{
    return file_open_picker_AddRef( &impl_from_open_init_window( iface )->IFileOpenPicker_iface );
}
static ULONG WINAPI open_init_window_Release( IInitializeWithWindowLocal *iface )
{
    return file_open_picker_Release( &impl_from_open_init_window( iface )->IFileOpenPicker_iface );
}
static HRESULT WINAPI open_init_window_Initialize( IInitializeWithWindowLocal *iface, HWND hwnd )
{
    impl_from_open_init_window( iface )->owner = hwnd;
    return S_OK;
}

static const IInitializeWithWindowLocalVtbl open_init_window_vtbl =
{
    open_init_window_QueryInterface,
    open_init_window_AddRef,
    open_init_window_Release,
    open_init_window_Initialize,
};

/* =========================================================================
 * FileSavePicker
 * ========================================================================= */

struct file_save_picker
{
    IFileSavePicker IFileSavePicker_iface;
    IInitializeWithWindowLocal IInitializeWithWindow_iface;
    LONG ref;
    HWND owner;
    PickerLocationId start_location;
    HSTRING settings_id;
    HSTRING commit_button_text;
    HSTRING default_ext;
    HSTRING suggested_name;
    IStorageFile *suggested_file;
    struct choices_map *choices;
};

static inline struct file_save_picker *impl_from_IFileSavePicker( IFileSavePicker *iface )
{
    return CONTAINING_RECORD( iface, struct file_save_picker, IFileSavePicker_iface );
}

static inline struct file_save_picker *impl_from_save_init_window( IInitializeWithWindowLocal *iface )
{
    return CONTAINING_RECORD( iface, struct file_save_picker, IInitializeWithWindow_iface );
}

static HRESULT WINAPI file_save_picker_QueryInterface( IFileSavePicker *iface, REFIID iid, void **out )
{
    struct file_save_picker *impl = impl_from_IFileSavePicker( iface );
    if (!out) return E_POINTER;
    *out = NULL;

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IFileSavePicker ))
    {
        *out = &impl->IFileSavePicker_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IInitializeWithWindow_local ))
    {
        *out = &impl->IInitializeWithWindow_iface;
        IUnknown_AddRef( (IUnknown *)*out );
        return S_OK;
    }
    FIXME( "iid %s not implemented\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG WINAPI file_save_picker_AddRef( IFileSavePicker *iface )
{
    return InterlockedIncrement( &impl_from_IFileSavePicker( iface )->ref );
}

static ULONG WINAPI file_save_picker_Release( IFileSavePicker *iface )
{
    struct file_save_picker *impl = impl_from_IFileSavePicker( iface );
    LONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        WindowsDeleteString( impl->settings_id );
        WindowsDeleteString( impl->commit_button_text );
        WindowsDeleteString( impl->default_ext );
        WindowsDeleteString( impl->suggested_name );
        if (impl->suggested_file) IStorageFile_Release( impl->suggested_file );
        if (impl->choices) __FIMap_2_HSTRING___FIVector_1_HSTRING_Release( &impl->choices->__FIMap_2_HSTRING___FIVector_1_HSTRING_iface );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI file_save_picker_GetIids( IFileSavePicker *iface, ULONG *count, IID **iids )
{
    static const IID iids_array[] =
    {
        { 0x3286ffcb, 0x617f, 0x4cc5, { 0xaf,0x6a, 0xb3,0xfd,0xf2,0x9a,0xd1,0x45 } },
    };
    if (!count || !iids) return E_POINTER;
    if (!(*iids = CoTaskMemAlloc( sizeof(iids_array) ))) return E_OUTOFMEMORY;
    memcpy( *iids, iids_array, sizeof(iids_array) );
    *count = ARRAY_SIZE(iids_array);
    return S_OK;
}

static HRESULT WINAPI file_save_picker_GetRuntimeClassName( IFileSavePicker *iface, HSTRING *name )
{
    if (!name) return E_POINTER;
    return WindowsCreateString( RuntimeClass_Windows_Storage_Pickers_FileSavePicker,
                                ARRAY_SIZE(RuntimeClass_Windows_Storage_Pickers_FileSavePicker) - 1, name );
}

static HRESULT WINAPI file_save_picker_GetTrustLevel( IFileSavePicker *iface, TrustLevel *level )
{
    if (!level) return E_POINTER;
    *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI file_save_picker_get_SettingsIdentifier( IFileSavePicker *iface, HSTRING *value )
{
    return duplicate_hstring_out( impl_from_IFileSavePicker( iface )->settings_id, value );
}

static HRESULT WINAPI file_save_picker_put_SettingsIdentifier( IFileSavePicker *iface, HSTRING value )
{
    return replace_hstring( &impl_from_IFileSavePicker( iface )->settings_id, value );
}

static HRESULT WINAPI file_save_picker_get_SuggestedStartLocation( IFileSavePicker *iface, PickerLocationId *value )
{
    if (!value) return E_POINTER;
    *value = impl_from_IFileSavePicker( iface )->start_location;
    return S_OK;
}

static HRESULT WINAPI file_save_picker_put_SuggestedStartLocation( IFileSavePicker *iface, PickerLocationId value )
{
    impl_from_IFileSavePicker( iface )->start_location = value;
    return S_OK;
}

static HRESULT WINAPI file_save_picker_get_CommitButtonText( IFileSavePicker *iface, HSTRING *value )
{
    return duplicate_hstring_out( impl_from_IFileSavePicker( iface )->commit_button_text, value );
}

static HRESULT WINAPI file_save_picker_put_CommitButtonText( IFileSavePicker *iface, HSTRING value )
{
    return replace_hstring( &impl_from_IFileSavePicker( iface )->commit_button_text, value );
}

static HRESULT WINAPI file_save_picker_get_FileTypeChoices( IFileSavePicker *iface,
                                                            __FIMap_2_HSTRING___FIVector_1_HSTRING **value )
{
    struct file_save_picker *impl = impl_from_IFileSavePicker( iface );
    if (!value) return E_POINTER;
    *value = &impl->choices->__FIMap_2_HSTRING___FIVector_1_HSTRING_iface;
    __FIMap_2_HSTRING___FIVector_1_HSTRING_AddRef( *value );
    return S_OK;
}

static HRESULT WINAPI file_save_picker_get_DefaultFileExtension( IFileSavePicker *iface, HSTRING *value )
{
    return duplicate_hstring_out( impl_from_IFileSavePicker( iface )->default_ext, value );
}

static HRESULT WINAPI file_save_picker_put_DefaultFileExtension( IFileSavePicker *iface, HSTRING value )
{
    return replace_hstring( &impl_from_IFileSavePicker( iface )->default_ext, value );
}

static HRESULT WINAPI file_save_picker_get_SuggestedSaveFile( IFileSavePicker *iface, IStorageFile **value )
{
    struct file_save_picker *impl = impl_from_IFileSavePicker( iface );
    if (!value) return E_POINTER;
    *value = impl->suggested_file;
    if (*value) IStorageFile_AddRef( *value );
    return S_OK;
}

static HRESULT WINAPI file_save_picker_put_SuggestedSaveFile( IFileSavePicker *iface, IStorageFile *value )
{
    struct file_save_picker *impl = impl_from_IFileSavePicker( iface );
    if (value) IStorageFile_AddRef( value );
    if (impl->suggested_file) IStorageFile_Release( impl->suggested_file );
    impl->suggested_file = value;
    return S_OK;
}

static HRESULT WINAPI file_save_picker_get_SuggestedFileName( IFileSavePicker *iface, HSTRING *value )
{
    return duplicate_hstring_out( impl_from_IFileSavePicker( iface )->suggested_name, value );
}

static HRESULT WINAPI file_save_picker_put_SuggestedFileName( IFileSavePicker *iface, HSTRING value )
{
    return replace_hstring( &impl_from_IFileSavePicker( iface )->suggested_name, value );
}

static HRESULT WINAPI file_save_picker_PickSaveFileAsync( IFileSavePicker *iface, IAsyncOperation_StorageFile **operation )
{
    struct file_save_picker *impl = impl_from_IFileSavePicker( iface );
    WCHAR **paths = NULL;
    UINT32 count = 0;
    char *filters;
    IStorageFile *file = NULL;
    HRESULT hr;

    TRACE( "iface %p, operation %p.\n", iface, operation );
    if (!operation) return E_POINTER;

    filters = build_portal_filters_from_map( impl->choices );
    if (!try_portal_pick( PORTAL_PICKER_SAVE, L"Save File", impl->commit_button_text,
                          impl->suggested_name, impl->start_location, filters, &paths, &count ))
    {
        win32_pick_files( impl->owner, FALSE, TRUE, impl->start_location, impl->suggested_name, &paths, &count );
    }
    free( filters );

    if (count && paths && paths[0])
        storage_file_create( paths[0], &file );
    free_dos_paths( paths, count );

    hr = async_operation_inspectable_create( &IID_IAsyncOperation_StorageFile,
                                             L"Windows.Foundation.IAsyncOperation`1<Windows.Storage.StorageFile>",
                                             (IInspectable *)file, (void **)operation );
    if (file) IStorageFile_Release( file );
    return hr;
}

static const struct IFileSavePickerVtbl file_save_picker_vtbl =
{
    file_save_picker_QueryInterface,
    file_save_picker_AddRef,
    file_save_picker_Release,
    file_save_picker_GetIids,
    file_save_picker_GetRuntimeClassName,
    file_save_picker_GetTrustLevel,
    file_save_picker_get_SettingsIdentifier,
    file_save_picker_put_SettingsIdentifier,
    file_save_picker_get_SuggestedStartLocation,
    file_save_picker_put_SuggestedStartLocation,
    file_save_picker_get_CommitButtonText,
    file_save_picker_put_CommitButtonText,
    file_save_picker_get_FileTypeChoices,
    file_save_picker_get_DefaultFileExtension,
    file_save_picker_put_DefaultFileExtension,
    file_save_picker_get_SuggestedSaveFile,
    file_save_picker_put_SuggestedSaveFile,
    file_save_picker_get_SuggestedFileName,
    file_save_picker_put_SuggestedFileName,
    file_save_picker_PickSaveFileAsync,
};

static HRESULT WINAPI save_init_window_QueryInterface( IInitializeWithWindowLocal *iface, REFIID iid, void **out )
{
    return file_save_picker_QueryInterface( &impl_from_save_init_window( iface )->IFileSavePicker_iface, iid, out );
}
static ULONG WINAPI save_init_window_AddRef( IInitializeWithWindowLocal *iface )
{
    return file_save_picker_AddRef( &impl_from_save_init_window( iface )->IFileSavePicker_iface );
}
static ULONG WINAPI save_init_window_Release( IInitializeWithWindowLocal *iface )
{
    return file_save_picker_Release( &impl_from_save_init_window( iface )->IFileSavePicker_iface );
}
static HRESULT WINAPI save_init_window_Initialize( IInitializeWithWindowLocal *iface, HWND hwnd )
{
    impl_from_save_init_window( iface )->owner = hwnd;
    return S_OK;
}

static const IInitializeWithWindowLocalVtbl save_init_window_vtbl =
{
    save_init_window_QueryInterface,
    save_init_window_AddRef,
    save_init_window_Release,
    save_init_window_Initialize,
};

/* =========================================================================
 * FolderPicker
 * ========================================================================= */

struct folder_picker
{
    IFolderPicker IFolderPicker_iface;
    IInitializeWithWindowLocal IInitializeWithWindow_iface;
    LONG ref;
    HWND owner;
    PickerViewMode view_mode;
    PickerLocationId start_location;
    HSTRING settings_id;
    HSTRING commit_button_text;
    struct hstring_vector *filters;
};

static inline struct folder_picker *impl_from_IFolderPicker( IFolderPicker *iface )
{
    return CONTAINING_RECORD( iface, struct folder_picker, IFolderPicker_iface );
}

static inline struct folder_picker *impl_from_folder_init_window( IInitializeWithWindowLocal *iface )
{
    return CONTAINING_RECORD( iface, struct folder_picker, IInitializeWithWindow_iface );
}

static HRESULT WINAPI folder_picker_QueryInterface( IFolderPicker *iface, REFIID iid, void **out )
{
    struct folder_picker *impl = impl_from_IFolderPicker( iface );
    if (!out) return E_POINTER;
    *out = NULL;

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IFolderPicker ))
    {
        *out = &impl->IFolderPicker_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IInitializeWithWindow_local ))
    {
        *out = &impl->IInitializeWithWindow_iface;
        IUnknown_AddRef( (IUnknown *)*out );
        return S_OK;
    }
    FIXME( "iid %s not implemented\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG WINAPI folder_picker_AddRef( IFolderPicker *iface )
{
    return InterlockedIncrement( &impl_from_IFolderPicker( iface )->ref );
}

static ULONG WINAPI folder_picker_Release( IFolderPicker *iface )
{
    struct folder_picker *impl = impl_from_IFolderPicker( iface );
    LONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        WindowsDeleteString( impl->settings_id );
        WindowsDeleteString( impl->commit_button_text );
        if (impl->filters) __FIVector_1_HSTRING_Release( &impl->filters->__FIVector_1_HSTRING_iface );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI folder_picker_GetIids( IFolderPicker *iface, ULONG *count, IID **iids )
{
    static const IID iids_array[] =
    {
        { 0x084f7799, 0xf3fb, 0x400a, { 0x99,0xb1, 0x7b,0x4a,0x77,0x2f,0xd6,0x0d } },
    };
    if (!count || !iids) return E_POINTER;
    if (!(*iids = CoTaskMemAlloc( sizeof(iids_array) ))) return E_OUTOFMEMORY;
    memcpy( *iids, iids_array, sizeof(iids_array) );
    *count = ARRAY_SIZE(iids_array);
    return S_OK;
}

static HRESULT WINAPI folder_picker_GetRuntimeClassName( IFolderPicker *iface, HSTRING *name )
{
    if (!name) return E_POINTER;
    return WindowsCreateString( RuntimeClass_Windows_Storage_Pickers_FolderPicker,
                                ARRAY_SIZE(RuntimeClass_Windows_Storage_Pickers_FolderPicker) - 1, name );
}

static HRESULT WINAPI folder_picker_GetTrustLevel( IFolderPicker *iface, TrustLevel *level )
{
    if (!level) return E_POINTER;
    *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI folder_picker_get_ViewMode( IFolderPicker *iface, PickerViewMode *value )
{
    if (!value) return E_POINTER;
    *value = impl_from_IFolderPicker( iface )->view_mode;
    return S_OK;
}

static HRESULT WINAPI folder_picker_put_ViewMode( IFolderPicker *iface, PickerViewMode value )
{
    impl_from_IFolderPicker( iface )->view_mode = value;
    return S_OK;
}

static HRESULT WINAPI folder_picker_get_SettingsIdentifier( IFolderPicker *iface, HSTRING *value )
{
    return duplicate_hstring_out( impl_from_IFolderPicker( iface )->settings_id, value );
}

static HRESULT WINAPI folder_picker_put_SettingsIdentifier( IFolderPicker *iface, HSTRING value )
{
    return replace_hstring( &impl_from_IFolderPicker( iface )->settings_id, value );
}

static HRESULT WINAPI folder_picker_get_SuggestedStartLocation( IFolderPicker *iface, PickerLocationId *value )
{
    if (!value) return E_POINTER;
    *value = impl_from_IFolderPicker( iface )->start_location;
    return S_OK;
}

static HRESULT WINAPI folder_picker_put_SuggestedStartLocation( IFolderPicker *iface, PickerLocationId value )
{
    impl_from_IFolderPicker( iface )->start_location = value;
    return S_OK;
}

static HRESULT WINAPI folder_picker_get_CommitButtonText( IFolderPicker *iface, HSTRING *value )
{
    return duplicate_hstring_out( impl_from_IFolderPicker( iface )->commit_button_text, value );
}

static HRESULT WINAPI folder_picker_put_CommitButtonText( IFolderPicker *iface, HSTRING value )
{
    return replace_hstring( &impl_from_IFolderPicker( iface )->commit_button_text, value );
}

static HRESULT WINAPI folder_picker_get_FileTypeFilter( IFolderPicker *iface, __FIVector_1_HSTRING **value )
{
    struct folder_picker *impl = impl_from_IFolderPicker( iface );
    if (!value) return E_POINTER;
    *value = &impl->filters->__FIVector_1_HSTRING_iface;
    __FIVector_1_HSTRING_AddRef( *value );
    return S_OK;
}

static HRESULT WINAPI folder_picker_PickSingleFolderAsync( IFolderPicker *iface, IAsyncOperation_StorageFolder **operation )
{
    struct folder_picker *impl = impl_from_IFolderPicker( iface );
    WCHAR **paths = NULL;
    UINT32 count = 0;
    IStorageFolder *folder = NULL;
    HRESULT hr;

    TRACE( "iface %p, operation %p.\n", iface, operation );
    if (!operation) return E_POINTER;

    if (try_portal_pick( PORTAL_PICKER_FOLDER, L"Select Folder", impl->commit_button_text,
                         NULL, impl->start_location, NULL, &paths, &count ))
    {
        if (count && paths && paths[0])
            storage_folder_create( paths[0], &folder );
        free_dos_paths( paths, count );
    }
    else
    {
        WCHAR *dir = win32_pick_folder( impl->owner );
        if (dir)
        {
            storage_folder_create( dir, &folder );
            free( dir );
        }
    }

    hr = async_operation_inspectable_create( &IID_IAsyncOperation_StorageFolder,
                                             L"Windows.Foundation.IAsyncOperation`1<Windows.Storage.StorageFolder>",
                                             (IInspectable *)folder, (void **)operation );
    if (folder) IStorageFolder_Release( folder );
    return hr;
}

static const struct IFolderPickerVtbl folder_picker_vtbl =
{
    folder_picker_QueryInterface,
    folder_picker_AddRef,
    folder_picker_Release,
    folder_picker_GetIids,
    folder_picker_GetRuntimeClassName,
    folder_picker_GetTrustLevel,
    folder_picker_get_ViewMode,
    folder_picker_put_ViewMode,
    folder_picker_get_SettingsIdentifier,
    folder_picker_put_SettingsIdentifier,
    folder_picker_get_SuggestedStartLocation,
    folder_picker_put_SuggestedStartLocation,
    folder_picker_get_CommitButtonText,
    folder_picker_put_CommitButtonText,
    folder_picker_get_FileTypeFilter,
    folder_picker_PickSingleFolderAsync,
};

static HRESULT WINAPI folder_init_window_QueryInterface( IInitializeWithWindowLocal *iface, REFIID iid, void **out )
{
    return folder_picker_QueryInterface( &impl_from_folder_init_window( iface )->IFolderPicker_iface, iid, out );
}
static ULONG WINAPI folder_init_window_AddRef( IInitializeWithWindowLocal *iface )
{
    return folder_picker_AddRef( &impl_from_folder_init_window( iface )->IFolderPicker_iface );
}
static ULONG WINAPI folder_init_window_Release( IInitializeWithWindowLocal *iface )
{
    return folder_picker_Release( &impl_from_folder_init_window( iface )->IFolderPicker_iface );
}
static HRESULT WINAPI folder_init_window_Initialize( IInitializeWithWindowLocal *iface, HWND hwnd )
{
    impl_from_folder_init_window( iface )->owner = hwnd;
    return S_OK;
}

static const IInitializeWithWindowLocalVtbl folder_init_window_vtbl =
{
    folder_init_window_QueryInterface,
    folder_init_window_AddRef,
    folder_init_window_Release,
    folder_init_window_Initialize,
};

/* =========================================================================
 * Activation Factories
 * ========================================================================= */

static HRESULT WINAPI picker_factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = iface;
        IInspectable_AddRef( (IInspectable *)iface );
        return S_OK;
    }
    return E_NOINTERFACE;
}

static ULONG WINAPI picker_factory_AddRef( IActivationFactory *iface ) { return 2; }
static ULONG WINAPI picker_factory_Release( IActivationFactory *iface ) { return 1; }
static HRESULT WINAPI picker_factory_GetIids( IActivationFactory *iface, ULONG *c, IID **iids ) { return E_NOTIMPL; }
static HRESULT WINAPI picker_factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *n ) { return E_NOTIMPL; }
static HRESULT WINAPI picker_factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *l )
{
    if (!l) return E_POINTER;
    *l = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI open_picker_factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    struct file_open_picker *impl;
    if (!instance) return E_POINTER;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    if (!(impl->filters = hstring_vector_create())) { free( impl ); return E_OUTOFMEMORY; }
    impl->IFileOpenPicker_iface.lpVtbl = &file_open_picker_vtbl;
    impl->IInitializeWithWindow_iface.lpVtbl = &open_init_window_vtbl;
    impl->ref = 1;
    impl->view_mode = PickerViewMode_List;
    impl->start_location = PickerLocationId_DocumentsLibrary;
    *instance = (IInspectable *)&impl->IFileOpenPicker_iface;
    return S_OK;
}

static HRESULT WINAPI save_picker_factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    struct file_save_picker *impl;
    if (!instance) return E_POINTER;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    if (!(impl->choices = choices_map_create())) { free( impl ); return E_OUTOFMEMORY; }
    impl->IFileSavePicker_iface.lpVtbl = &file_save_picker_vtbl;
    impl->IInitializeWithWindow_iface.lpVtbl = &save_init_window_vtbl;
    impl->ref = 1;
    impl->start_location = PickerLocationId_DocumentsLibrary;
    *instance = (IInspectable *)&impl->IFileSavePicker_iface;
    return S_OK;
}

static HRESULT WINAPI folder_picker_factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    struct folder_picker *impl;
    if (!instance) return E_POINTER;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    if (!(impl->filters = hstring_vector_create())) { free( impl ); return E_OUTOFMEMORY; }
    impl->IFolderPicker_iface.lpVtbl = &folder_picker_vtbl;
    impl->IInitializeWithWindow_iface.lpVtbl = &folder_init_window_vtbl;
    impl->ref = 1;
    impl->view_mode = PickerViewMode_List;
    impl->start_location = PickerLocationId_DocumentsLibrary;
    *instance = (IInspectable *)&impl->IFolderPicker_iface;
    return S_OK;
}

static const struct IActivationFactoryVtbl open_picker_factory_vtbl =
{
    picker_factory_QueryInterface,
    picker_factory_AddRef,
    picker_factory_Release,
    picker_factory_GetIids,
    picker_factory_GetRuntimeClassName,
    picker_factory_GetTrustLevel,
    open_picker_factory_ActivateInstance,
};

static const struct IActivationFactoryVtbl save_picker_factory_vtbl =
{
    picker_factory_QueryInterface,
    picker_factory_AddRef,
    picker_factory_Release,
    picker_factory_GetIids,
    picker_factory_GetRuntimeClassName,
    picker_factory_GetTrustLevel,
    save_picker_factory_ActivateInstance,
};

static const struct IActivationFactoryVtbl folder_picker_factory_vtbl =
{
    picker_factory_QueryInterface,
    picker_factory_AddRef,
    picker_factory_Release,
    picker_factory_GetIids,
    picker_factory_GetRuntimeClassName,
    picker_factory_GetTrustLevel,
    folder_picker_factory_ActivateInstance,
};

static IActivationFactory open_picker_factory_impl = { &open_picker_factory_vtbl };
static IActivationFactory save_picker_factory_impl = { &save_picker_factory_vtbl };
static IActivationFactory folder_picker_factory_impl = { &folder_picker_factory_vtbl };

IActivationFactory *file_open_picker_factory = &open_picker_factory_impl;
IActivationFactory *file_save_picker_factory = &save_picker_factory_impl;
IActivationFactory *folder_picker_factory = &folder_picker_factory_impl;
