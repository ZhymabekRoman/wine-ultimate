/* WinRT Windows.Globalization HSTRING vector/iterator implementation
 *
 * Copyright 2021 Rémi Bernon for CodeWeavers
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

WINE_DEFAULT_DEBUG_CHANNEL(locale);

/*
 *
 * IIterator<HSTRING>
 *
 */

struct iterator_hstring
{
   IIterator_HSTRING IIterator_HSTRING_iface;
   LONG ref;

   IVectorView_HSTRING *view;
   UINT32 index;
   UINT32 size;
};

static inline struct iterator_hstring *impl_from_IIterator_HSTRING( IIterator_HSTRING *iface )
{
   return CONTAINING_RECORD(iface, struct iterator_hstring, IIterator_HSTRING_iface);
}

static HRESULT WINAPI iterator_hstring_QueryInterface( IIterator_HSTRING *iface, REFIID iid, void **out )
{
    struct iterator_hstring *impl = impl_from_IIterator_HSTRING(iface);

   TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

   if (IsEqualGUID(iid, &IID_IUnknown) ||
       IsEqualGUID(iid, &IID_IInspectable) ||
       IsEqualGUID(iid, &IID_IAgileObject) ||
       IsEqualGUID(iid, &IID_IIterator_HSTRING))
    {
       IInspectable_AddRef((*out = &impl->IIterator_HSTRING_iface));
       return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid));
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI iterator_hstring_AddRef( IIterator_HSTRING *iface )
{
    struct iterator_hstring *impl = impl_from_IIterator_HSTRING(iface);
    ULONG ref = InterlockedIncrement(&impl->ref);
    TRACE("iface %p increasing refcount to %lu.\n", iface, ref);
    return ref;
}

static ULONG WINAPI iterator_hstring_Release( IIterator_HSTRING *iface )
{
    struct iterator_hstring *impl = impl_from_IIterator_HSTRING(iface);
    ULONG ref = InterlockedDecrement(&impl->ref);

    TRACE("iface %p decreasing refcount to %lu.\n", iface, ref);

    if (!ref)
    {
        IVectorView_HSTRING_Release(impl->view);
        free(impl);
    }

    return ref;
}

static HRESULT WINAPI iterator_hstring_GetIids( IIterator_HSTRING *iface, ULONG *iid_count, IID **iids )
{
    if (!iid_count || !iids) return E_POINTER;
    *iid_count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI iterator_hstring_GetRuntimeClassName( IIterator_HSTRING *iface, HSTRING *class_name )
{
    if (!class_name) return E_POINTER;
    return WindowsCreateString( L"Windows.Foundation.Collections.IIterator`1<String>", 50, class_name );
}

static HRESULT WINAPI iterator_hstring_GetTrustLevel( IIterator_HSTRING *iface, TrustLevel *trust_level )
{
    if (trust_level) *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI iterator_hstring_get_Current( IIterator_HSTRING *iface, HSTRING *value )
{
    struct iterator_hstring *impl = impl_from_IIterator_HSTRING(iface);
    TRACE("iface %p, value %p.\n", iface, value);
    return IVectorView_HSTRING_GetAt(impl->view, impl->index, value);
}

static HRESULT WINAPI iterator_hstring_get_HasCurrent( IIterator_HSTRING *iface, boolean *value )
{
    struct iterator_hstring *impl = impl_from_IIterator_HSTRING(iface);

    TRACE("iface %p, value %p.\n", iface, value);

    *value = impl->index < impl->size;
    return S_OK;
}

static HRESULT WINAPI iterator_hstring_MoveNext( IIterator_HSTRING *iface, boolean *value )
{
    struct iterator_hstring *impl = impl_from_IIterator_HSTRING(iface);

    TRACE("iface %p, value %p.\n", iface, value);

    if (impl->index < impl->size) impl->index++;
    return IIterator_HSTRING_get_HasCurrent(iface, value);
}

static HRESULT WINAPI iterator_hstring_GetMany( IIterator_HSTRING *iface, UINT32 items_size,
                                                HSTRING *items, UINT *count )
{
    struct iterator_hstring *impl = impl_from_IIterator_HSTRING(iface);
    HRESULT hr;

    TRACE("iface %p, items_size %u, items %p, count %p.\n", iface, items_size, items, count);

    if (!count) return E_POINTER;
    hr = IVectorView_HSTRING_GetMany(impl->view, impl->index, items_size, items, count);
    if (SUCCEEDED(hr)) impl->index += *count;
    return hr;
}

static const IIterator_HSTRINGVtbl iterator_hstring_vtbl =
{
    /* IUnknown methods */
    iterator_hstring_QueryInterface,
    iterator_hstring_AddRef,
    iterator_hstring_Release,
    /* IInspectable methods */
    iterator_hstring_GetIids,
    iterator_hstring_GetRuntimeClassName,
    iterator_hstring_GetTrustLevel,
    /* IIterator<HSTRING> methods */
    iterator_hstring_get_Current,
    iterator_hstring_get_HasCurrent,
    iterator_hstring_MoveNext,
    iterator_hstring_GetMany,
};

struct hstring_vector
{
    IVectorView_HSTRING IVectorView_HSTRING_iface;
    IIterable_HSTRING IIterable_HSTRING_iface;
    IVector_HSTRING IVector_HSTRING_iface;
    LONG ref;

    ULONG count;
    ULONG capacity;
    HSTRING *values;
};

static inline struct hstring_vector *impl_from_IVectorView_HSTRING(IVectorView_HSTRING *iface)
{
    return CONTAINING_RECORD(iface, struct hstring_vector, IVectorView_HSTRING_iface);
}

static HRESULT STDMETHODCALLTYPE hstring_vector_QueryInterface(IVectorView_HSTRING *iface,
        REFIID iid, void **out)
{
    struct hstring_vector *impl = impl_from_IVectorView_HSTRING(iface);
    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_IMarshal) ||
        IsEqualGUID(iid, &IID_IReferenceTrackerTarget_glob) ||
        IsEqualGUID(iid, &IID_IReferenceTracker_glob))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }

    if (IsEqualGUID(iid, &IID_IUnknown) ||
        IsEqualGUID(iid, &IID_IInspectable) ||
        IsEqualGUID(iid, &IID_IAgileObject) ||
        IsEqualGUID(iid, &IID_IVectorView_HSTRING))
    {
        IUnknown_AddRef(iface);
        *out = &impl->IVectorView_HSTRING_iface;
        return S_OK;
    }

    if (IsEqualGUID(iid, &IID_IIterable_HSTRING))
    {
        IUnknown_AddRef(iface);
        *out = &impl->IIterable_HSTRING_iface;
        return S_OK;
    }

    if (IsEqualGUID(iid, &IID_IVector_HSTRING))
    {
        IUnknown_AddRef(iface);
        *out = &impl->IVector_HSTRING_iface;
        return S_OK;
    }

    FIXME("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid));
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE hstring_vector_AddRef(IVectorView_HSTRING *iface)
{
    struct hstring_vector *impl = impl_from_IVectorView_HSTRING(iface);
    ULONG ref = InterlockedIncrement(&impl->ref);
    TRACE("iface %p, ref %lu.\n", iface, ref);
    return ref;
}

static ULONG STDMETHODCALLTYPE hstring_vector_Release(IVectorView_HSTRING *iface)
{
    struct hstring_vector *impl = impl_from_IVectorView_HSTRING(iface);
    ULONG ref = InterlockedDecrement(&impl->ref);
    TRACE("iface %p, ref %lu.\n", iface, ref);
    if (ref == 0)
    {
        while (impl->count--) WindowsDeleteString(impl->values[impl->count]);
        free(impl->values);
        free(impl);
    }
    return ref;
}

static HRESULT STDMETHODCALLTYPE hstring_vector_GetIids(IVectorView_HSTRING *iface,
        ULONG *iid_count, IID **iids)
{
    if (!iid_count || !iids) return E_POINTER;
    *iid_count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE hstring_vector_GetRuntimeClassName(IVectorView_HSTRING *iface,
        HSTRING *class_name)
{
    static const WCHAR name[] = L"Windows.Foundation.Collections.IVectorView`1<String>";
    if (!class_name) return E_POINTER;
    return WindowsCreateString(name, ARRAY_SIZE(name) - 1, class_name);
}

static HRESULT STDMETHODCALLTYPE hstring_vector_GetTrustLevel(IVectorView_HSTRING *iface,
        TrustLevel *trust_level)
{
    if (trust_level) *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE hstring_vector_GetAt(IVectorView_HSTRING *iface,
        UINT32 index, HSTRING *value)
{
    struct hstring_vector *impl = impl_from_IVectorView_HSTRING(iface);

    TRACE("iface %p, index %#x, value %p.\n", iface, index, value);

    if (!value) return E_POINTER;
    *value = NULL;
    if (index >= impl->count) return E_BOUNDS;
    return WindowsDuplicateString(impl->values[index], value);
}

static HRESULT STDMETHODCALLTYPE hstring_vector_get_Size(IVectorView_HSTRING *iface,
        UINT32 *value)
{
    struct hstring_vector *impl = impl_from_IVectorView_HSTRING(iface);

    TRACE("iface %p, value %p, returning count %lu.\n", iface, value, impl->count);

    if (!value) return E_POINTER;
    *value = impl->count;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE hstring_vector_IndexOf(IVectorView_HSTRING *iface,
        HSTRING element, UINT32 *index, BOOLEAN *found)
{
    struct hstring_vector *impl = impl_from_IVectorView_HSTRING(iface);
    INT32 i, order;

    TRACE("iface %p, element %p, index %p, found %p.\n", iface, element, index, found);

    for (i = 0; i < impl->count; ++i)
        if (SUCCEEDED(WindowsCompareStringOrdinal(impl->values[i], element, &order)) && order == 0)
            break;

    if (i < impl->count)
    {
        *found = TRUE;
        *index = i;
    }
    else
    {
        *found = FALSE;
        *index = 0;
    }

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE hstring_vector_GetMany(IVectorView_HSTRING *iface,
        UINT32 start_index, UINT32 items_size, HSTRING *items, UINT *count)
{
    struct hstring_vector *impl = impl_from_IVectorView_HSTRING(iface);
    HRESULT hr = S_OK;
    ULONG i;

    TRACE("iface %p, start_index %#x, items %p, count %p.\n", iface, start_index, items, count);

    if (!count || (items_size && !items)) return E_POINTER;
    if (items_size) memset(items, 0, items_size * sizeof(*items));

    for (i = start_index; i < impl->count && i < start_index + items_size; ++i)
        if (FAILED(hr = WindowsDuplicateString(impl->values[i], items + i - start_index)))
            break;

    if (FAILED(hr)) while (i-- > start_index) WindowsDeleteString(items[i - start_index]);
    *count = i - start_index;
    return hr;
}

static const struct IVectorView_HSTRINGVtbl hstring_vector_vtbl =
{
    hstring_vector_QueryInterface,
    hstring_vector_AddRef,
    hstring_vector_Release,
    /* IInspectable methods */
    hstring_vector_GetIids,
    hstring_vector_GetRuntimeClassName,
    hstring_vector_GetTrustLevel,
    /* IVectorView<HSTRING> methods */
    hstring_vector_GetAt,
    hstring_vector_get_Size,
    hstring_vector_IndexOf,
    hstring_vector_GetMany,
};

static const struct IIterable_HSTRINGVtbl iterable_view_hstring_vtbl;
static const struct IVector_HSTRINGVtbl hstring_ivector_vtbl;

HRESULT hstring_vector_create(HSTRING *values, SIZE_T count, IVectorView_HSTRING **out)
{
    struct hstring_vector *impl;
    HSTRING *buf = NULL;

    if (!out) return E_POINTER;
    if (!(impl = calloc(1, sizeof(*impl)))) return E_OUTOFMEMORY;
    if (count)
    {
        if (!(buf = malloc(count * sizeof(HSTRING))))
        {
            free(impl);
            return E_OUTOFMEMORY;
        }
        memcpy(buf, values, count * sizeof(HSTRING));
    }

    impl->ref = 1;
    impl->IVectorView_HSTRING_iface.lpVtbl = &hstring_vector_vtbl;
    impl->IIterable_HSTRING_iface.lpVtbl = &iterable_view_hstring_vtbl;
    impl->IVector_HSTRING_iface.lpVtbl = &hstring_ivector_vtbl;
    impl->count = count;
    impl->capacity = count;
    impl->values = buf;

    *out = &impl->IVectorView_HSTRING_iface;
    return S_OK;
}

/*
 *
 * IIterable<HSTRING>
 *
 */

DEFINE_IINSPECTABLE_(iterable_view_hstring, IIterable_HSTRING, struct hstring_vector, view_impl_from_IIterable_HSTRING,
                     IIterable_HSTRING_iface, &impl->IVectorView_HSTRING_iface)

static HRESULT WINAPI iterable_view_hstring_First( IIterable_HSTRING *iface, IIterator_HSTRING **value )
{
    struct hstring_vector *impl = view_impl_from_IIterable_HSTRING(iface);
    struct iterator_hstring *iter;

    TRACE("iface %p, value %p.\n", iface, value);

    if (!(iter = calloc(1, sizeof(*iter)))) return E_OUTOFMEMORY;
    iter->IIterator_HSTRING_iface.lpVtbl = &iterator_hstring_vtbl;
    iter->ref = 1;

    IVectorView_HSTRING_AddRef((iter->view = &impl->IVectorView_HSTRING_iface));
    iter->size = impl->count;

    *value = &iter->IIterator_HSTRING_iface;
    return S_OK;
}

static const struct IIterable_HSTRINGVtbl iterable_view_hstring_vtbl =
{
    /* IUnknown methods */
    iterable_view_hstring_QueryInterface,
    iterable_view_hstring_AddRef,
    iterable_view_hstring_Release,
    /* IInspectable methods */
    iterable_view_hstring_GetIids,
    iterable_view_hstring_GetRuntimeClassName,
    iterable_view_hstring_GetTrustLevel,
    /* IIterable<HSTRING> methods */
    iterable_view_hstring_First,
};

/*
 *
 * IVector<HSTRING> (Windows.Foundation.Collections.IVector`1<String>,
 * IID {98b9acc1-4b56-532e-ac73-03d5291cca90}). Shares the hstring_vector
 * buffer; mutation methods resize it with realloc().
 *
 */

DEFINE_IINSPECTABLE_(hstring_ivector, IVector_HSTRING, struct hstring_vector,
                     impl_from_IVector_HSTRING, IVector_HSTRING_iface, &impl->IVectorView_HSTRING_iface)

static HRESULT WINAPI hstring_ivector_GetAt( IVector_HSTRING *iface, UINT32 index, HSTRING *value )
{
    return IVectorView_HSTRING_GetAt( &impl_from_IVector_HSTRING(iface)->IVectorView_HSTRING_iface, index, value );
}

static HRESULT WINAPI hstring_ivector_get_Size( IVector_HSTRING *iface, UINT32 *value )
{
    return IVectorView_HSTRING_get_Size( &impl_from_IVector_HSTRING(iface)->IVectorView_HSTRING_iface, value );
}

static HRESULT WINAPI hstring_ivector_GetView( IVector_HSTRING *iface, IVectorView_HSTRING **value )
{
    struct hstring_vector *impl = impl_from_IVector_HSTRING(iface);
    TRACE("iface %p, value %p.\n", iface, value);
    if (!value) return E_POINTER;
    *value = &impl->IVectorView_HSTRING_iface;
    IUnknown_AddRef(*value);
    return S_OK;
}

static HRESULT WINAPI hstring_ivector_IndexOf( IVector_HSTRING *iface, HSTRING element,
                                               UINT32 *index, boolean *found )
{
    return IVectorView_HSTRING_IndexOf( &impl_from_IVector_HSTRING(iface)->IVectorView_HSTRING_iface,
                                        element, index, found );
}

static HRESULT WINAPI hstring_ivector_SetAt( IVector_HSTRING *iface, UINT32 index, HSTRING value )
{
    struct hstring_vector *impl = impl_from_IVector_HSTRING(iface);
    HSTRING dup = NULL;
    HRESULT hr;

    TRACE("iface %p, index %u, value %p.\n", iface, index, value);
    if (index >= impl->count) return E_BOUNDS;
    if (FAILED(hr = WindowsDuplicateString(value, &dup))) return hr;
    WindowsDeleteString(impl->values[index]);
    impl->values[index] = dup;
    return S_OK;
}

static HRESULT hstring_vector_insert( struct hstring_vector *impl, UINT32 index, HSTRING value )
{
    HSTRING dup = NULL;
    HRESULT hr;

    if (index > impl->count) return E_BOUNDS;
    if (impl->count >= impl->capacity)
    {
        ULONG new_cap = impl->capacity ? impl->capacity * 2 : 4;
        HSTRING *grown = realloc(impl->values, new_cap * sizeof(*grown));
        if (!grown) return E_OUTOFMEMORY;
        impl->values = grown;
        impl->capacity = new_cap;
    }
    if (FAILED(hr = WindowsDuplicateString(value, &dup))) return hr;
    memmove(impl->values + index + 1, impl->values + index,
            (impl->count - index) * sizeof(*impl->values));
    impl->values[index] = dup;
    impl->count++;
    return S_OK;
}

static HRESULT WINAPI hstring_ivector_InsertAt( IVector_HSTRING *iface, UINT32 index, HSTRING value )
{
    return hstring_vector_insert(impl_from_IVector_HSTRING(iface), index, value);
}

static HRESULT WINAPI hstring_ivector_RemoveAt( IVector_HSTRING *iface, UINT32 index )
{
    struct hstring_vector *impl = impl_from_IVector_HSTRING(iface);

    TRACE("iface %p, index %u.\n", iface, index);
    if (index >= impl->count) return E_BOUNDS;
    WindowsDeleteString(impl->values[index]);
    impl->count--;
    memmove(impl->values + index, impl->values + index + 1,
            (impl->count - index) * sizeof(*impl->values));
    return S_OK;
}

static HRESULT WINAPI hstring_ivector_Append( IVector_HSTRING *iface, HSTRING value )
{
    struct hstring_vector *impl = impl_from_IVector_HSTRING(iface);
    return hstring_vector_insert(impl, impl->count, value);
}

static HRESULT WINAPI hstring_ivector_RemoveAtEnd( IVector_HSTRING *iface )
{
    struct hstring_vector *impl = impl_from_IVector_HSTRING(iface);

    TRACE("iface %p.\n", iface);
    if (!impl->count) return E_BOUNDS;
    WindowsDeleteString(impl->values[--impl->count]);
    return S_OK;
}

static HRESULT WINAPI hstring_ivector_Clear( IVector_HSTRING *iface )
{
    struct hstring_vector *impl = impl_from_IVector_HSTRING(iface);

    TRACE("iface %p.\n", iface);
    while (impl->count) WindowsDeleteString(impl->values[--impl->count]);
    return S_OK;
}

static const struct IVector_HSTRINGVtbl hstring_ivector_vtbl =
{
    /* IUnknown methods */
    hstring_ivector_QueryInterface,
    hstring_ivector_AddRef,
    hstring_ivector_Release,
    /* IInspectable methods */
    hstring_ivector_GetIids,
    hstring_ivector_GetRuntimeClassName,
    hstring_ivector_GetTrustLevel,
    /* IVector<HSTRING> methods */
    hstring_ivector_GetAt,
    hstring_ivector_get_Size,
    hstring_ivector_GetView,
    hstring_ivector_IndexOf,
    hstring_ivector_SetAt,
    hstring_ivector_InsertAt,
    hstring_ivector_RemoveAt,
    hstring_ivector_Append,
    hstring_ivector_RemoveAtEnd,
    hstring_ivector_Clear,
};
