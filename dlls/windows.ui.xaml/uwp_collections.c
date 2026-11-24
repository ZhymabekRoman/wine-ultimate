/* WinRT Windows.UI.Xaml UWP Simple Vectors: UIElementCollection, Row/ColumnDefinitionCollection
 *
 * Copyright (C) 2026 Wine project
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

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <excpt.h>
#include "wine/exception.h"
#include "uwp_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(xaml);

/* --- Simple UWP Vector (UIElementCollection / RowDefinitionCollection / ColumnDefinitionCollection) --- */
static const GUID IID_IIterable_PageStackEntry_UWP =
    { 0xdd2278a5, 0x4c14, 0x5967, { 0x8a, 0x41, 0x9a, 0x47, 0xc3, 0xc5, 0xc4, 0xa7 } };
static const GUID IID_IIterator_PageStackEntry_UWP =
    { 0xb89ca96c, 0xded1, 0x53bd, { 0x87, 0x44, 0xca, 0xa8, 0x4e, 0x87, 0xba, 0x87 } };
static const GUID IID_IIterator_IInspectable_UWP =
    { 0x44a94f2d, 0x04f8, 0x5091, { 0xb3, 0x36, 0xbe, 0x78, 0x92, 0xdd, 0x10, 0xbe } };
static const GUID IID_IIterable_UIElement_UWP =
    { 0x1d1f9d60, 0xd53b, 0x57f7, { 0xb1, 0x44, 0x8f, 0x7c, 0x48, 0x78, 0x46, 0xe8 } };
static const GUID IID_IIterator_UIElement_UWP =
    { 0x42614e61, 0xb0aa, 0x5e72, { 0x93, 0x54, 0x27, 0x71, 0xf4, 0x32, 0x90, 0x78 } };
static const GUID IID_IIterable_Block_UWP =
    { 0xf7023b9a, 0xe6d1, 0x5e2d, { 0x8f, 0x41, 0xb2, 0x8c, 0x33, 0x32, 0x3e, 0x04 } };
static const GUID IID_IIterator_Block_UWP =
    { 0x798d518e, 0xa9f8, 0x5fc7, { 0x8c, 0xcc, 0x2a, 0x49, 0x06, 0x9a, 0xba, 0x05 } };
static const GUID IID_IIterable_Inline_UWP =
    { 0x3017abfa, 0xa321, 0x5045, { 0x97, 0x44, 0x60, 0xaf, 0x42, 0xed, 0x71, 0x59 } };
static const GUID IID_IIterator_Inline_UWP =
    { 0x18bb3ba7, 0x67d6, 0x5d0d, { 0x90, 0x97, 0x24, 0xfc, 0x66, 0xbf, 0xb9, 0x01 } };
static const GUID IID_IIterable_RowDef_UWP =
    { 0x11fdab6b, 0x6769, 0x5c78, { 0x93, 0x21, 0x1c, 0x29, 0x30, 0x25, 0x38, 0x7d } };
static const GUID IID_IIterator_RowDef_UWP =
    { 0xd3d9ead4, 0x5e46, 0x54f7, { 0xa5, 0x27, 0x60, 0xde, 0xdd, 0x9f, 0x25, 0xc8 } };
static const GUID IID_IIterable_ColDef_UWP =
    { 0xb629d3fc, 0x513c, 0x5857, { 0x9b, 0xd5, 0xf9, 0xf7, 0x8f, 0xdc, 0xb1, 0xb9 } };
static const GUID IID_IIterator_ColDef_UWP =
    { 0x4159246b, 0x41be, 0x5bc2, { 0xa0, 0x7e, 0x9f, 0x26, 0xf1, 0xb9, 0x28, 0x71 } };
static const GUID IID_IIterable_DO_UWP =
    { 0x42e26ae1, 0xd357, 0x57e8, { 0xbb, 0x48, 0xf7, 0x5c, 0x9f, 0xf6, 0x9d, 0x91 } };

static HRESULT WINAPI uwp_vec_QI( IInspectable *iface, REFIID iid, void **out )
{
    struct uwp_vec_view *v = CONTAINING_RECORD( iface, struct uwp_vec_view, IInspectable_iface );
    static const GUID IID_IReferenceTracker_vec =
        { 0x11d3b13a, 0x180e, 0x4789, { 0xa8, 0xbe, 0x77, 0x12, 0x88, 0x28, 0x93, 0xe6 } };
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IIterable_PageStackEntry_UWP ) ||
        IsEqualGUID( iid, &IID_IIterable_IInspectable_UWP ) ||
        IsEqualGUID( iid, &IID_IIterable_UIElement_UWP ) ||
        IsEqualGUID( iid, &IID_IIterable_Block_UWP ) ||
        IsEqualGUID( iid, &IID_IIterable_Inline_UWP ) ||
        IsEqualGUID( iid, &IID_IBindableIterable_UWP ) ||
        IsEqualGUID( iid, &IID_IIterable_RowDef_UWP ) ||
        IsEqualGUID( iid, &IID_IIterable_ColDef_UWP ) ||
        IsEqualGUID( iid, &IID_IIterable_DO_UWP ) ||
        IsEqualGUID( iid, &IID_IIterable_GradientStop_UWP ))
    {
        *out = &v->IIterable_iface;
        IInspectable_AddRef( iface );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IMarshal ) ||
        IsEqualGUID( iid, &IID_IPropertyValue_g ) ||
        IsEqualGUID( iid, &IID_IReferenceTracker_vec ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget_UWP ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget_UWP2 ) ||
        IsEqualGUID( iid, &IID_IWeakReferenceSource_local ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    *out = iface;
    IInspectable_AddRef( iface );
    return S_OK;
}
static HRESULT WINAPI uwp_vec_GetRuntimeClassName( IInspectable *iface, HSTRING *name )
{
    struct uwp_vec_view *v = CONTAINING_RECORD( iface, struct uwp_vec_view, IInspectable_iface );
    const WCHAR *cls = (v && v->runtime_class) ? v->runtime_class : L"Windows.UI.Xaml.Controls.UIElementCollection";
    return WindowsCreateString( cls, lstrlenW( cls ), name );
}
static ULONG WINAPI uwp_vec_AddRef( IInspectable *iface )
{
    struct uwp_vec_view *v = CONTAINING_RECORD( iface, struct uwp_vec_view, IInspectable_iface );
    return InterlockedIncrement( &v->ref );
}
static ULONG WINAPI uwp_vec_Release( IInspectable *iface )
{
    struct uwp_vec_view *v = CONTAINING_RECORD( iface, struct uwp_vec_view, IInspectable_iface );
    ULONG r = InterlockedDecrement( &v->ref );
    if (!r) v->ref = 1;
    return r;
}
HRESULT WINAPI uwp_vec_GetAt( void *iface, unsigned int idx, void **item )
{
    struct uwp_vec_view *v = CONTAINING_RECORD( iface, struct uwp_vec_view, IInspectable_iface );
    if (!item) return E_POINTER;
    if (idx >= v->count) { *item = NULL; return 0x8000000b; }
    *item = v->items[idx];
    if (*item) IInspectable_AddRef( (IInspectable *)*item );
    return S_OK;
}
HRESULT WINAPI uwp_vec_get_Size( void *iface, unsigned int *sz )
{
    struct uwp_vec_view *v = CONTAINING_RECORD( iface, struct uwp_vec_view, IInspectable_iface );
    if (sz) *sz = v->count;
    return S_OK;
}
static HRESULT WINAPI uwp_vec_GetView( void *iface, void **view )
{
    if (view) { *view = iface; IInspectable_AddRef( (IInspectable *)iface ); }
    return S_OK;
}
static HRESULT WINAPI uwp_vec_IndexOf( void *iface, void *item, unsigned int *idx, boolean *found )
{
    struct uwp_vec_view *v = CONTAINING_RECORD( iface, struct uwp_vec_view, IInspectable_iface );
    unsigned int i;
    struct uwp_element *want = uwp_elem_from_any( item );
    for (i = 0; i < v->count; i++)
    {
        if (v->items[i] == item || (want && uwp_elem_from_any( v->items[i] ) == want))
        {
            if (idx) *idx = i;
            if (found) *found = TRUE;
            return S_OK;
        }
    }
    if (idx) *idx = 0;
    if (found) *found = FALSE;
    return S_OK;
}
static HRESULT WINAPI uwp_vec_SetAt( void *iface, unsigned int idx, void *item )
{
    struct uwp_vec_view *v = CONTAINING_RECORD( iface, struct uwp_vec_view, IInspectable_iface );
    if (idx >= v->count) return 0x8000000b;
    if (v->items[idx]) IInspectable_Release( v->items[idx] );
    v->items[idx] = (IInspectable *)item;
    if (item) IInspectable_AddRef( (IInspectable *)item );
    return S_OK;
}
static HRESULT WINAPI uwp_vec_InsertAt( void *iface, unsigned int idx, void *item )
{
    struct uwp_vec_view *v = CONTAINING_RECORD( iface, struct uwp_vec_view, IInspectable_iface );
    struct uwp_element *child_el;
    if (v->owner && wcsstr( v->owner->class_name, L"ItemsRepeater" ) && idx == 0)
        idx = v->count;
    if (idx > v->count) return S_OK;
    if (!xaml_vec_grow( (void **)&v->items, &v->cap, (int)v->count + 1, sizeof(*v->items), 16 ))
        return E_OUTOFMEMORY;
    memmove( &v->items[idx + 1], &v->items[idx], (v->count - idx) * sizeof(void *) );
    v->items[idx] = (IInspectable *)item;
    if (item) IInspectable_AddRef( (IInspectable *)item );
    v->count++;
    if ((child_el = uwp_elem_from_any( item )) != NULL && v->owner)
    {
        if (child_el->kind == UWP_KIND_ACCELERATOR || wcsstr( child_el->class_name, L"Accelerator" ))
        {
            child_el->accel_target = v->owner;
            uwp_accel_register( child_el, v->owner, child_el->accel_key, child_el->accel_mods, child_el->accel_enabled );
        }
        else
        {
            child_el->parent = v->owner;
            if (child_el->outer && !child_el->delegating && !child_el->outer_pinned)
            {
                child_el->outer_pinned = TRUE;
                IInspectable_AddRef( child_el->outer );
            }
            if (!child_el->data_context)
            {
                struct uwp_element *dp_anc;
                for (dp_anc = v->owner; dp_anc; dp_anc = dp_anc->parent)
                    if (dp_anc->data_context) { uwp_propagate_data_context( child_el, dp_anc->data_context ); break; }
            }
            if (!child_el->loaded_fired)
            {
                BOOL tree_loaded = FALSE;
                struct uwp_element *p;
                for (p = v->owner; p; p = p->parent)
                    if (p->loaded_fired) { tree_loaded = TRUE; break; }
                if (tree_loaded)
                    uwp_fire_loaded_tree( child_el );
            }
        }
    }
    g_layout_dirty = TRUE;
    uwp_invalidate_window();
    return S_OK;
}
static HRESULT WINAPI uwp_vec_RemoveAt( void *iface, unsigned int idx )
{
    struct uwp_vec_view *v = CONTAINING_RECORD( iface, struct uwp_vec_view, IInspectable_iface );
    if (idx >= v->count) return S_OK;
    if (v->items[idx])
    {
        struct uwp_element *ch = uwp_elem_from_any( v->items[idx] );
        if (ch && v->owner && ch->parent == v->owner) ch->parent = NULL;
        IInspectable_Release( v->items[idx] );
    }
    memmove( &v->items[idx], &v->items[idx + 1], (v->count - idx - 1) * sizeof(void *) );
    v->count--;
    g_layout_dirty = TRUE;
    uwp_invalidate_window();
    return S_OK;
}
HRESULT WINAPI uwp_vec_Append( void *iface, void *item )
{
    struct uwp_vec_view *v = CONTAINING_RECORD( iface, struct uwp_vec_view, IInspectable_iface );
    return uwp_vec_InsertAt( iface, v->count, item );
}
static HRESULT WINAPI uwp_vec_RemoveAtEnd( void *iface )
{
    struct uwp_vec_view *v = CONTAINING_RECORD( iface, struct uwp_vec_view, IInspectable_iface );
    if (v->count > 0) return uwp_vec_RemoveAt( iface, v->count - 1 );
    return S_OK;
}
HRESULT WINAPI uwp_vec_Clear( void *iface )
{
    struct uwp_vec_view *v = CONTAINING_RECORD( iface, struct uwp_vec_view, IInspectable_iface );
    unsigned int i;
    for (i = 0; i < v->count; i++)
    {
        if (v->items[i])
        {
            struct uwp_element *ch = uwp_elem_from_any( v->items[i] );
            if (ch && v->owner && ch->parent == v->owner) ch->parent = NULL;
            IInspectable_Release( v->items[i] );
        }
    }
    v->count = 0;
    g_layout_dirty = TRUE;
    uwp_invalidate_window();
    return S_OK;
}

static const void *uwp_vec_vtbl[32] =
{
    uwp_vec_QI,
    uwp_vec_AddRef,
    uwp_vec_Release,
    uwp_sub_GetIids,
    uwp_vec_GetRuntimeClassName,
    uwp_sub_GetTrustLevel,
    uwp_vec_GetAt,
    uwp_vec_get_Size,
    uwp_vec_GetView,
    uwp_vec_IndexOf,
    uwp_vec_SetAt,
    uwp_vec_InsertAt,
    uwp_vec_RemoveAt,
    uwp_vec_Append,
    uwp_vec_RemoveAtEnd,
    uwp_vec_Clear,
    uwp_generic_noop,
    uwp_generic_noop,
};

struct uwp_vec_iter
{
    IInspectable IIterator_iface;
    LONG ref;
    struct uwp_vec_view *vec;
    unsigned int idx;
};

static HRESULT WINAPI uwp_viter_QI( IInspectable *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IIterator_PageStackEntry_UWP ) ||
        IsEqualGUID( iid, &IID_IIterator_IInspectable_UWP ) ||
        IsEqualGUID( iid, &IID_IIterator_UIElement_UWP ) ||
        IsEqualGUID( iid, &IID_IIterator_Block_UWP ) ||
        IsEqualGUID( iid, &IID_IIterator_Inline_UWP ) ||
        IsEqualGUID( iid, &IID_IIterator_RowDef_UWP ) ||
        IsEqualGUID( iid, &IID_IIterator_ColDef_UWP ) ||
        IsEqualGUID( iid, &IID_IIterator_GradientStop_UWP ))
    {
        *out = iface;
        IInspectable_AddRef( iface );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI uwp_viter_AddRef( IInspectable *iface )
{
    struct uwp_vec_iter *it = CONTAINING_RECORD( iface, struct uwp_vec_iter, IIterator_iface );
    return InterlockedIncrement( &it->ref );
}
static ULONG WINAPI uwp_viter_Release( IInspectable *iface )
{
    struct uwp_vec_iter *it = CONTAINING_RECORD( iface, struct uwp_vec_iter, IIterator_iface );
    ULONG r = InterlockedDecrement( &it->ref );
    if (!r)
    {
        if (it->vec) IInspectable_Release( &it->vec->IInspectable_iface );
        free( it );
    }
    return r;
}
static HRESULT WINAPI uwp_viter_GetRuntimeClassName( IInspectable *iface, HSTRING *name )
{
    static const WCHAR cls[] = L"Windows.Foundation.Collections.IIterator";
    return WindowsCreateString( cls, ARRAY_SIZE(cls) - 1, name );
}
static HRESULT WINAPI uwp_viter_get_Current( IInspectable *iface, IInspectable **out )
{
    struct uwp_vec_iter *it = CONTAINING_RECORD( iface, struct uwp_vec_iter, IIterator_iface );
    if (!out) return E_POINTER;
    if (!it->vec || it->idx >= it->vec->count) { *out = NULL; return E_BOUNDS; }
    *out = it->vec->items[it->idx];
    if (*out) IInspectable_AddRef( *out );
    return S_OK;
}
static HRESULT WINAPI uwp_viter_get_HasCurrent( IInspectable *iface, boolean *out )
{
    struct uwp_vec_iter *it = CONTAINING_RECORD( iface, struct uwp_vec_iter, IIterator_iface );
    if (!out) return E_POINTER;
    *out = (it->vec && it->idx < it->vec->count) ? TRUE : FALSE;
    return S_OK;
}
static HRESULT WINAPI uwp_viter_MoveNext( IInspectable *iface, boolean *out )
{
    struct uwp_vec_iter *it = CONTAINING_RECORD( iface, struct uwp_vec_iter, IIterator_iface );
    if (it->vec && it->idx < it->vec->count) it->idx++;
    if (out) *out = (it->vec && it->idx < it->vec->count) ? TRUE : FALSE;
    return S_OK;
}
static HRESULT WINAPI uwp_viter_GetMany( IInspectable *iface, UINT32 cap, IInspectable **items, UINT32 *actual )
{
    struct uwp_vec_iter *it = CONTAINING_RECORD( iface, struct uwp_vec_iter, IIterator_iface );
    UINT32 n = 0;
    if (actual) *actual = 0;
    if (!it->vec || !items) return S_OK;
    while (n < cap && it->idx < it->vec->count)
    {
        items[n] = it->vec->items[it->idx++];
        if (items[n]) IInspectable_AddRef( items[n] );
        n++;
    }
    if (actual) *actual = n;
    return S_OK;
}
static const void *uwp_viter_vtbl[] =
{
    uwp_viter_QI, uwp_viter_AddRef, uwp_viter_Release,
    uwp_sub_GetIids, uwp_viter_GetRuntimeClassName, uwp_sub_GetTrustLevel,
    uwp_viter_get_Current, uwp_viter_get_HasCurrent, uwp_viter_MoveNext, uwp_viter_GetMany,
};

static HRESULT WINAPI uwp_vec_iter_QI( IInspectable *iface, REFIID iid, void **out )
{
    struct uwp_vec_view *v = CONTAINING_RECORD( iface, struct uwp_vec_view, IIterable_iface );
    return uwp_vec_QI( &v->IInspectable_iface, iid, out );
}
static ULONG WINAPI uwp_vec_iter_AddRef( IInspectable *iface )
{
    struct uwp_vec_view *v = CONTAINING_RECORD( iface, struct uwp_vec_view, IIterable_iface );
    return uwp_vec_AddRef( &v->IInspectable_iface );
}
static ULONG WINAPI uwp_vec_iter_Release( IInspectable *iface )
{
    struct uwp_vec_view *v = CONTAINING_RECORD( iface, struct uwp_vec_view, IIterable_iface );
    return uwp_vec_Release( &v->IInspectable_iface );
}
static HRESULT WINAPI uwp_vec_iter_First( IInspectable *iface, IInspectable **out )
{
    struct uwp_vec_view *v = CONTAINING_RECORD( iface, struct uwp_vec_view, IIterable_iface );
    struct uwp_vec_iter *it;
    if (!out) return E_POINTER;
    it = calloc( 1, sizeof(*it) );
    if (!it) return E_OUTOFMEMORY;
    it->IIterator_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_viter_vtbl;
    it->ref = 1;
    it->vec = v;
    IInspectable_AddRef( &v->IInspectable_iface );
    *out = &it->IIterator_iface;
    return S_OK;
}
static const void *uwp_vec_iterable_vtbl[] =
{
    uwp_vec_iter_QI, uwp_vec_iter_AddRef, uwp_vec_iter_Release,
    uwp_sub_GetIids, uwp_vec_GetRuntimeClassName, uwp_sub_GetTrustLevel,
    uwp_vec_iter_First,
};

HRESULT uwp_vec_create( struct uwp_element *owner, IInspectable **out )
{
    struct uwp_vec_view *v = calloc( 1, sizeof(*v) );
    if (!v) return E_OUTOFMEMORY;
    v->IInspectable_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_vec_vtbl;
    v->IIterable_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_vec_iterable_vtbl;
    v->ref = 1;
    v->owner = owner;
    *out = &v->IInspectable_iface;
    return S_OK;
}

struct uwp_vec_changed_args
{
    IInspectable IVectorChangedEventArgs_iface;
    LONG ref;
    int change;
    unsigned int index;
};
static const GUID IID_IVectorChangedEventArgs_UWP =
    { 0x575933df, 0x34fe, 0x4480, { 0xaf, 0x15, 0x07, 0x69, 0x1f, 0x3d, 0x5d, 0x9b } };
static HRESULT WINAPI uwp_vca_QI( IInspectable *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IVectorChangedEventArgs_UWP ))
    {
        *out = iface;
        IInspectable_AddRef( iface );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI uwp_vca_AddRef( IInspectable *iface )
{
    struct uwp_vec_changed_args *a = CONTAINING_RECORD( iface, struct uwp_vec_changed_args, IVectorChangedEventArgs_iface );
    return InterlockedIncrement( &a->ref );
}
static ULONG WINAPI uwp_vca_Release( IInspectable *iface )
{
    struct uwp_vec_changed_args *a = CONTAINING_RECORD( iface, struct uwp_vec_changed_args, IVectorChangedEventArgs_iface );
    ULONG r = InterlockedDecrement( &a->ref );
    if (!r) free( a );
    return r;
}
static HRESULT WINAPI uwp_vca_GetIids( IInspectable *iface, ULONG *cnt, IID **iids )
{
    if (cnt) *cnt = 0;
    if (iids) *iids = NULL;
    return S_OK;
}
static HRESULT WINAPI uwp_vca_GetRuntimeClassName( IInspectable *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.Foundation.Collections.VectorChangedEventArgs", 53, name );
}
static HRESULT WINAPI uwp_vca_GetTrustLevel( IInspectable *iface, TrustLevel *lvl )
{
    if (lvl) *lvl = BaseTrust;
    return S_OK;
}
static HRESULT WINAPI uwp_vca_get_CollectionChange( void *iface, int *out )
{
    struct uwp_vec_changed_args *a = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_vec_changed_args, IVectorChangedEventArgs_iface );
    if (out) *out = a->change;
    return S_OK;
}
static HRESULT WINAPI uwp_vca_get_Index( void *iface, unsigned int *out )
{
    struct uwp_vec_changed_args *a = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_vec_changed_args, IVectorChangedEventArgs_iface );
    if (out) *out = a->index;
    return S_OK;
}
static const void *uwp_vca_vtbl[] =
{
    uwp_vca_QI,
    uwp_vca_AddRef,
    uwp_vca_Release,
    uwp_vca_GetIids,
    uwp_vca_GetRuntimeClassName,
    uwp_vca_GetTrustLevel,
    uwp_vca_get_CollectionChange,
    uwp_vca_get_Index,
};

static void uwp_el_fire_vector_changed( struct uwp_element *impl, int change, unsigned int idx )
{
    void **handlers = NULL;
    unsigned int cnt, i;
    struct uwp_vec_changed_args *args;
    IInspectable *sender;
    if (!impl) return;
    cnt = xaml_event_registry_snapshot_alloc( &impl->obsvec_changed_reg, &handlers );
    if (!cnt) return;
    args = calloc( 1, sizeof(*args) );
    if (!args)
    {
        for (i = 0; i < cnt; i++) IUnknown_Release( (IUnknown *)handlers[i] );
        free( handlers );
        return;
    }
    args->IVectorChangedEventArgs_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_vca_vtbl;
    args->ref = 1;
    args->change = change;
    args->index = idx;
    sender = impl->outer ? impl->outer : &impl->IObservableVector_iface;
    TRACE( "uwp_el_fire_vector_changed on %s change=%d idx=%u handlers=%u\n",
           debugstr_w(impl->class_name), change, idx, cnt );
    for (i = 0; i < cnt; i++)
    {
        struct xaml_event_handler *h = (struct xaml_event_handler *)handlers[i];
        __TRY
        {
            h->lpVtbl->Invoke( h, sender, &args->IVectorChangedEventArgs_iface );
        }
        __EXCEPT_ALL
        {
            WARN( "uwp_el_fire_vector_changed handler[%u] threw exception\n", i );
        }
        __ENDTRY;
        IUnknown_Release( (IUnknown *)h );
    }
    free( handlers );
    IInspectable_Release( &args->IVectorChangedEventArgs_iface );
}

static HRESULT WINAPI uwp_el_obsvec_add( void *iface, void *handler, EventRegistrationToken *tok )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return E_NOINTERFACE;
    TRACE( "uwp_el_obsvec_add on %s handler=%p\n", debugstr_w(impl->class_name), handler );
    return xaml_event_registry_add( &impl->obsvec_changed_reg, handler, tok );
}
static HRESULT WINAPI uwp_el_obsvec_rem( void *iface, EventRegistrationToken tok )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return E_NOINTERFACE;
    return xaml_event_registry_remove( &impl->obsvec_changed_reg, tok );
}
const void *uwp_el_obsvec_vtbl[16] =
{
    uwp_sub_QueryInterface,
    uwp_sub_AddRef,
    uwp_sub_Release,
    uwp_sub_GetIids,
    uwp_sub_GetRuntimeClassName,
    uwp_sub_GetTrustLevel,
    uwp_el_obsvec_add,
    uwp_el_obsvec_rem,
    [8 ... 15] = uwp_generic_noop,
};

static struct uwp_vec_view *uwp_el_ensure_vec( void *iface )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return NULL;
    if (!impl->children_coll) uwp_vec_create( impl, &impl->children_coll );
    return impl->children_coll ? CONTAINING_RECORD( impl->children_coll, struct uwp_vec_view, IInspectable_iface ) : NULL;
}
static HRESULT WINAPI uwp_el_vec_GetAt( void *iface, unsigned int idx, void **item )
{
    struct uwp_vec_view *v = uwp_el_ensure_vec( iface );
    if (!v) { if (item) *item = NULL; return E_FAIL; }
    return uwp_vec_GetAt( &v->IInspectable_iface, idx, item );
}
static HRESULT WINAPI uwp_el_vec_get_Size( void *iface, unsigned int *sz )
{
    struct uwp_vec_view *v = uwp_el_ensure_vec( iface );
    if (sz) *sz = v ? v->count : 0;
    return S_OK;
}
static HRESULT WINAPI uwp_el_vec_GetView( void *iface, void **view )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!view) return E_POINTER;
    if (!impl) { *view = NULL; return E_FAIL; }
    *view = &impl->IVectorView_iface;
    IInspectable_AddRef( (IInspectable *)*view );
    return S_OK;
}
static HRESULT WINAPI uwp_el_vec_IndexOf( void *iface, void *item, unsigned int *idx, boolean *found )
{
    struct uwp_vec_view *v = uwp_el_ensure_vec( iface );
    if (!v) { if (idx) *idx = 0; if (found) *found = FALSE; return S_OK; }
    return uwp_vec_IndexOf( &v->IInspectable_iface, item, idx, found );
}
static HRESULT WINAPI uwp_el_vec_SetAt( void *iface, unsigned int idx, void *item )
{
    struct uwp_vec_view *v = uwp_el_ensure_vec( iface );
    HRESULT hr = v ? uwp_vec_SetAt( &v->IInspectable_iface, idx, item ) : S_OK;
    if (SUCCEEDED(hr) && v && v->owner) uwp_el_fire_vector_changed( v->owner, 3 /* ItemChanged */, idx );
    return hr;
}
static HRESULT WINAPI uwp_el_vec_InsertAt( void *iface, unsigned int idx, void *item )
{
    struct uwp_vec_view *v = uwp_el_ensure_vec( iface );
    HRESULT hr = v ? uwp_vec_InsertAt( &v->IInspectable_iface, idx, item ) : S_OK;
    if (SUCCEEDED(hr) && v && v->owner) uwp_el_fire_vector_changed( v->owner, 1 /* ItemInserted */, idx );
    return hr;
}
static HRESULT WINAPI uwp_el_vec_RemoveAt( void *iface, unsigned int idx )
{
    struct uwp_vec_view *v = uwp_el_ensure_vec( iface );
    HRESULT hr = v ? uwp_vec_RemoveAt( &v->IInspectable_iface, idx ) : S_OK;
    if (SUCCEEDED(hr) && v && v->owner) uwp_el_fire_vector_changed( v->owner, 2 /* ItemRemoved */, idx );
    return hr;
}
static HRESULT WINAPI uwp_el_vec_Append( void *iface, void *item )
{
    struct uwp_vec_view *v = uwp_el_ensure_vec( iface );
    unsigned int idx = v ? v->count : 0;
    HRESULT hr = v ? uwp_vec_Append( &v->IInspectable_iface, item ) : S_OK;
    if (SUCCEEDED(hr) && v && v->owner) uwp_el_fire_vector_changed( v->owner, 1 /* ItemInserted */, idx );
    return hr;
}
static HRESULT WINAPI uwp_el_vec_RemoveAtEnd( void *iface )
{
    struct uwp_vec_view *v = uwp_el_ensure_vec( iface );
    unsigned int idx = (v && v->count > 0) ? v->count - 1 : 0;
    HRESULT hr = v ? uwp_vec_RemoveAtEnd( &v->IInspectable_iface ) : S_OK;
    if (SUCCEEDED(hr) && v && v->owner) uwp_el_fire_vector_changed( v->owner, 2 /* ItemRemoved */, idx );
    return hr;
}
static HRESULT WINAPI uwp_el_vec_Clear( void *iface )
{
    struct uwp_vec_view *v = uwp_el_ensure_vec( iface );
    HRESULT hr = v ? uwp_vec_Clear( &v->IInspectable_iface ) : S_OK;
    if (SUCCEEDED(hr) && v && v->owner) uwp_el_fire_vector_changed( v->owner, 0 /* Reset */, 0 );
    return hr;
}
static HRESULT WINAPI uwp_el_vec_GetMany( void *iface, unsigned int start, unsigned int cap, void **items, unsigned int *actual )
{
    struct uwp_vec_view *v = uwp_el_ensure_vec( iface );
    unsigned int i = 0;
    if (v && start < v->count)
    {
        for (i = 0; i < cap && start + i < v->count; i++)
        {
            items[i] = v->items[start + i];
            if (items[i]) IInspectable_AddRef( (IInspectable *)items[i] );
        }
    }
    if (actual) *actual = i;
    return S_OK;
}
static HRESULT WINAPI uwp_el_vec_ReplaceAll( void *iface, unsigned int cnt, void **items )
{
    struct uwp_vec_view *v = uwp_el_ensure_vec( iface );
    unsigned int i;
    if (!v) return S_OK;
    uwp_vec_Clear( &v->IInspectable_iface );
    for (i = 0; i < cnt; i++)
        uwp_vec_Append( &v->IInspectable_iface, items[i] );
    return S_OK;
}
const void *uwp_el_vec_vtbl[24] =
{
    uwp_sub_QueryInterface,
    uwp_sub_AddRef,
    uwp_sub_Release,
    uwp_sub_GetIids,
    uwp_sub_GetRuntimeClassName,
    uwp_sub_GetTrustLevel,
    uwp_el_vec_GetAt,
    uwp_el_vec_get_Size,
    uwp_el_vec_GetView,
    uwp_el_vec_IndexOf,
    uwp_el_vec_SetAt,
    uwp_el_vec_InsertAt,
    uwp_el_vec_RemoveAt,
    uwp_el_vec_Append,
    uwp_el_vec_RemoveAtEnd,
    uwp_el_vec_Clear,
    uwp_el_vec_GetMany,
    uwp_el_vec_ReplaceAll,
    [18 ... 23] = uwp_generic_noop,
};
const void *uwp_el_vecview_vtbl[16] =
{
    uwp_sub_QueryInterface,
    uwp_sub_AddRef,
    uwp_sub_Release,
    uwp_sub_GetIids,
    uwp_sub_GetRuntimeClassName,
    uwp_sub_GetTrustLevel,
    uwp_el_vec_GetAt,
    uwp_el_vec_get_Size,
    uwp_el_vec_IndexOf,
    uwp_el_vec_GetMany,
    [10 ... 15] = uwp_generic_noop,
};
struct uwp_el_iter
{
    const void *vtbl;
    LONG ref;
    struct uwp_vec_view *v;
    unsigned int idx;
};
static HRESULT WINAPI uwp_el_iter_QI( void *iface, REFIID iid, void **out )
{
    static const GUID IID_IIterator_DO =
        { 0x9247b89e, 0xd40f, 0x54c3, { 0xa7, 0xfe, 0x14, 0xfe, 0xe9, 0x37, 0x61, 0xc5 } };
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IIterator_DO ) ||
        IsEqualGUID( iid, &IID_IIterator_GradientStop_UWP ) ||
        IsEqualGUID( iid, &IID_IIterator_IInspectable_UWP ) ||
        IsEqualGUID( iid, &IID_IIterator_UIElement_UWP ))
    {
        *out = iface;
        InterlockedIncrement( &((struct uwp_el_iter *)iface)->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static HRESULT WINAPI uwp_el_iter_GetRuntimeClassName( void *iface, HSTRING *name )
{
    static const WCHAR cls[] = L"Windows.Foundation.Collections.IIterator`1<Windows.UI.Xaml.DependencyObject>";
    return WindowsCreateString( cls, ARRAY_SIZE(cls) - 1, name );
}
static ULONG WINAPI uwp_el_iter_AddRef( void *iface ) { return InterlockedIncrement( &((struct uwp_el_iter *)iface)->ref ); }
static ULONG WINAPI uwp_el_iter_Release( void *iface )
{
    struct uwp_el_iter *it = iface;
    ULONG r = InterlockedDecrement( &it->ref );
    if (!r) { if (it->v) IInspectable_Release( &it->v->IInspectable_iface ); free( it ); }
    return r;
}
static HRESULT WINAPI uwp_el_iter_get_Current( void *iface, void **out )
{
    struct uwp_el_iter *it = iface;
    if (!out) return E_POINTER;
    if (!it->v || it->idx >= it->v->count) { *out = NULL; return 0x8000000b; }
    *out = it->v->items[it->idx];
    if (*out) IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}
static HRESULT WINAPI uwp_el_iter_get_HasCurrent( void *iface, boolean *has )
{
    struct uwp_el_iter *it = iface;
    if (has) *has = (it->v && it->idx < it->v->count) ? TRUE : FALSE;
    return S_OK;
}
static HRESULT WINAPI uwp_el_iter_MoveNext( void *iface, boolean *has )
{
    struct uwp_el_iter *it = iface;
    if (it->v && it->idx < it->v->count) it->idx++;
    if (has) *has = (it->v && it->idx < it->v->count) ? TRUE : FALSE;
    return S_OK;
}
static HRESULT WINAPI uwp_el_iter_GetMany( void *iface, unsigned int cap, void **items, unsigned int *actual )
{
    struct uwp_el_iter *it = iface;
    unsigned int i = 0;
    if (it->v)
    {
        while (i < cap && it->idx < it->v->count)
        {
            items[i] = it->v->items[it->idx++];
            if (items[i]) IInspectable_AddRef( (IInspectable *)items[i] );
            i++;
        }
    }
    if (actual) *actual = i;
    return S_OK;
}
static const void *uwp_el_iter_vtbl[16] =
{
    uwp_el_iter_QI,
    uwp_el_iter_AddRef,
    uwp_el_iter_Release,
    uwp_sub_GetIids,
    uwp_el_iter_GetRuntimeClassName,
    uwp_sub_GetTrustLevel,
    uwp_el_iter_get_Current,
    uwp_el_iter_get_HasCurrent,
    uwp_el_iter_MoveNext,
    uwp_el_iter_GetMany,
    [10 ... 15] = uwp_generic_noop,
};
static HRESULT WINAPI uwp_el_iterable_First( void *iface, void **out )
{
    struct uwp_vec_view *v = uwp_el_ensure_vec( iface );
    struct uwp_el_iter *it;
    if (!out) return E_POINTER;
    it = calloc( 1, sizeof(*it) );
    if (!it) return E_OUTOFMEMORY;
    it->vtbl = uwp_el_iter_vtbl;
    it->ref = 1;
    it->v = v;
    if (v) IInspectable_AddRef( &v->IInspectable_iface );
    *out = it;
    return S_OK;
}
const void *uwp_el_iterable_vtbl[16] =
{
    uwp_sub_QueryInterface,
    uwp_sub_AddRef,
    uwp_sub_Release,
    uwp_sub_GetIids,
    uwp_sub_GetRuntimeClassName,
    uwp_sub_GetTrustLevel,
    uwp_el_iterable_First,
    [7 ... 15] = uwp_generic_noop,
};

