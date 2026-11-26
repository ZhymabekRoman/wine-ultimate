/* WinRT Windows.UI.Xaml UWP Visual Tree, PRI/XBF Loader, Layout & Input Host
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
#include <math.h>
#include <excpt.h>
#include "wine/exception.h"
#include "private.h"
#include "xbf_known_types.h"
#include "weakreference.h"
#define WIDL_using_Windows_ApplicationModel
#define WIDL_using_Windows_Management_Deployment
#include "windows.management.deployment.h"
#define WIDL_using_Windows_UI_Composition
#include "windows.ui.composition.h"
#include "uwp_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(xaml);


/* Faces we QI and call raw-indexed: the metadata provider, XAML type/member, property-change
 * notification, boxed IReference<T>, ICommand and generic event delegates. The objects behind
 * them are served by our own combase-side code and Calculator's .NET Native RCW projection,
 * whose vtables follow the WinRT ABI. Naming each slot we call lets callers use typed calls;
 * earlier, uncalled slots stay as void * placeholders. */
struct xaml_metadata_provider;



struct notify_property_changed;

struct property_changed_args;

struct reference_int32;

struct command_iface;

/* Generic event delegate: { QueryInterface, AddRef, Release, Invoke(sender, args) }. */
struct xaml_event_handler;

void uwp_elem_sync_class_flags( struct uwp_element *el )
{
    const WCHAR *cls, *dsk;
    UINT64 flags = 0;

    if (!el) return;
    cls = el->class_name;
    dsk = el->default_style_key_name;

    if (wcsstr( cls, L"AutoSuggestBox" ))
    {
        flags |= UWP_CF_AUTO_SUGGEST_BOX;
        if (!wcscmp( cls, L"Windows.UI.Xaml.Controls.AutoSuggestBox" ) ||
            !wcscmp( cls, L"Microsoft.UI.Xaml.Controls.AutoSuggestBox" ))
            flags |= UWP_CF_AUTO_SUGGEST_BOX_EXACT;
    }
    if (wcsstr( cls, L"TextBox" )) flags |= UWP_CF_TEXT_BOX;
    if (wcsstr( cls, L"ItemsRepeater" )) flags |= UWP_CF_ITEMS_REPEATER;
    if (wcsstr( cls, L"ResponsiveItemsRepeaterList" )) flags |= UWP_CF_RESP_ITEMS_REPEATER_LIST;
    if (wcsstr( cls, L"ScrollViewer" )) flags |= UWP_CF_SCROLL_VIEWER;
    if (wcsstr( cls, L"ComboBox" ) && !wcsstr( cls, L"ComboBoxItem" )) flags |= UWP_CF_COMBOBOX;
    if (!wcscmp( cls, L"Microsoft.UI.Xaml.Controls.NavigationView" ) ||
        !wcscmp( cls, L"Windows.UI.Xaml.Controls.NavigationView" ) ||
        !wcscmp( dsk, L"Microsoft.UI.Xaml.Controls.NavigationView" ) ||
        !wcscmp( dsk, L"Windows.UI.Xaml.Controls.NavigationView" ))
        flags |= UWP_CF_NAV_VIEW;
    if (wcsstr( cls, L"NavigationView" )) flags |= UWP_CF_NAV_VIEW_ANY;
    if (wcsstr( cls, L"NavigationViewItem" ) || wcsstr( dsk, L"NavigationViewItem" ))
        flags |= UWP_CF_NAV_ITEM;
    if (wcsstr( cls, L"NavigationViewItemHeader" ) || wcsstr( dsk, L"NavigationViewItemHeader" ))
        flags |= UWP_CF_NAV_HEADER;
    if (wcsstr( cls, L"NavigationViewItemSeparator" ) || wcsstr( dsk, L"NavigationViewItemSeparator" ))
        flags |= UWP_CF_NAV_SEPARATOR;
    if (wcsstr( cls, L"Card" )) flags |= UWP_CF_CARD;
    if (wcsstr( cls, L"Shimmer" )) flags |= UWP_CF_SHIMMER;
    if (wcsstr( cls, L"Skeleton" )) flags |= UWP_CF_SKELETON;
    if (wcsstr( cls, L"DynamicSpotlightControl" ))
        flags |= UWP_CF_SPOTLIGHT | UWP_CF_SPOTLIGHT_DECOR;
    else if (wcsstr( cls, L"ScrollingSpotlightControl" ) ||
             wcsstr( cls, L"CarrouselSpotlightControl" ) ||
             wcsstr( cls, L"ScrollingPreviewListView" ) ||
             wcsstr( cls, L"SpotlightHeroImage" ))
        flags |= UWP_CF_SPOTLIGHT_DECOR;
    if (wcsstr( cls, L"ListView" )) flags |= UWP_CF_LISTVIEW;
    if (wcsstr( cls, L"GridView" )) flags |= UWP_CF_GRIDVIEW;
    if (wcsstr( cls, L"Item" )) flags |= UWP_CF_HAS_ITEM;
    if (wcsstr( cls, L"TeachingTip" )) flags |= UWP_CF_TEACHING_TIP;
    if (wcsstr( cls, L"MenuBarItem" ))
        flags |= UWP_CF_MENUBAR_ITEM;
    else if (wcsstr( cls, L"MenuBar" ))
        flags |= UWP_CF_MENUBAR;
    if (wcsstr( cls, L"FontIcon" )) flags |= UWP_CF_FONT_ICON;
    if (wcsstr( cls, L"SymbolIcon" )) flags |= UWP_CF_SYMBOL_ICON;
    if (wcsstr( cls, L"PivotItem" ))
        flags |= UWP_CF_PIVOT_ITEM;
    else if (wcsstr( cls, L"Pivot" ))
        flags |= UWP_CF_PIVOT;
    if (wcsstr( cls, L"RadioButtons" )) flags |= UWP_CF_RADIO_BUTTON;
    if (wcsstr( cls, L"ToggleButton" )) flags |= UWP_CF_TOGGLE_BUTTON;
    if (wcsstr( cls, L"RadioMenuFlyoutItem" )) flags |= UWP_CF_RADIO_MENU_ITEM;
    if (wcsstr( cls, L"ToggleMenuFlyoutItem" )) flags |= UWP_CF_TOGGLE_MENU_ITEM;
    if (wcsstr( cls, L"MenuFlyoutItem" )) flags |= UWP_CF_MENU_FLYOUT_ITEM;
    if (wcsstr( cls, L"MenuFlyoutSubItem" )) flags |= UWP_CF_MENU_FLYOUT_SUB_ITEM;
    if (wcsstr( cls, L"DropDownButton" )) flags |= UWP_CF_DROP_DOWN_BUTTON;
    if (wcsstr( cls, L"InfoBadge" )) flags |= UWP_CF_INFO_BADGE;
    if (wcsstr( cls, L"Flyout" )) flags |= UWP_CF_FLYOUT;
    if (wcsstr( cls, L"TabView" ) && !wcsstr( cls, L"TabViewItem" )) flags |= UWP_CF_TABVIEW;
    if (wcsstr( cls, L"ToolTip" )) flags |= UWP_CF_TOOLTIP;
    if (wcsstr( cls, L"Separator" )) flags |= UWP_CF_SEPARATOR;
    if (wcsstr( cls, L"Accelerator" )) flags |= UWP_CF_ACCELERATOR;
    if (wcsstr( cls, L"Template" )) flags |= UWP_CF_TEMPLATE;
    if (wcsstr( cls, L"RichTextBlock" )) flags |= UWP_CF_RICH_TEXT_BLOCK;
    if (wcsstr( cls, L"ConstrainedBox" )) flags |= UWP_CF_CONSTRAINED_BOX;
    if (wcsstr( cls, L"ListBox" )) flags |= UWP_CF_LISTBOX;
    if (wcsstr( cls, L"ItemsControl" )) flags |= UWP_CF_ITEMS_CONTROL;
    if (wcsstr( cls, L"Paragraph" )) flags |= UWP_CF_PARAGRAPH;
    if (wcsstr( cls, L"LineBreak" )) flags |= UWP_CF_LINE_BREAK;
    if (wcsstr( cls, L"Run" )) flags |= UWP_CF_RUN;
    if (wcsstr( cls, L"Span" )) flags |= UWP_CF_SPAN;
    if (wcsstr( cls, L"Inline" )) flags |= UWP_CF_INLINE;
    if (wcsstr( cls, L"AppTitleBar" )) flags |= UWP_CF_APP_TITLE_BAR;
    if (wcsstr( cls, L"MediaPlayerElement" )) flags |= UWP_CF_MEDIA_PLAYER;
    if (wcsstr( cls, L"ProgressBar" )) flags |= UWP_CF_PROGRESS_BAR;
    if (wcsstr( cls, L"Rectangle" )) flags |= UWP_CF_RECTANGLE;
    if (wcsstr( cls, L"Ellipse" )) flags |= UWP_CF_ELLIPSE;
    if (wcsstr( cls, L"Brush" )) flags |= UWP_CF_BRUSH;
    if (wcsstr( cls, L"FlipView" )) flags |= UWP_CF_FLIPVIEW;
    if (wcsstr( cls, L"TileList" )) flags |= UWP_CF_TILE_LIST;
    if (wcsstr( cls, L"CalculationResult" )) flags |= UWP_CF_CALC_RESULT;
    if (wcsstr( cls, L"ContentPresenter" )) flags |= UWP_CF_CONTENT_PRESENTER;
    if (wcsstr( cls, L"Storyboard" )) flags |= UWP_CF_STORYBOARD;
    if (!wcsncmp( cls, L"Microsoft.UI.Xaml.", 18 )) flags |= UWP_CF_WINUI_PREFIX;
    if (!wcsncmp( cls, L"Windows.UI.Xaml.", 16 )) flags |= UWP_CF_UWP_PREFIX;
    if (!wcscmp( cls, L"Windows.UI.Xaml.DataTemplate" )) flags |= UWP_CF_DATA_TEMPLATE;

    el->class_flags = flags;
}

void uwp_elem_clear_text( struct uwp_element *el )
{
    if (!el) return;
    if (el->text)
    {
        free( el->text );
        el->text = NULL;
    }
    el->text_len = 0;
    el->text_cap = 0;
}

void uwp_elem_set_text_n( struct uwp_element *el, const WCHAR *s, size_t n )
{
    size_t actual_n = 0;
    size_t need;
    WCHAR *buf;

    if (!el) return;
    if (!s || n == 0)
    {
        uwp_elem_clear_text( el );
        return;
    }
    while (actual_n < n && s[actual_n]) actual_n++;
    if (actual_n == 0)
    {
        uwp_elem_clear_text( el );
        return;
    }
    if (el->text && s >= el->text && s < el->text + el->text_cap)
    {
        if (s != el->text) memmove( el->text, s, actual_n * sizeof(WCHAR) );
        el->text[actual_n] = 0;
        el->text_len = (UINT32)actual_n;
        return;
    }
    need = actual_n + 1;
    if (need > el->text_cap)
    {
        size_t new_cap = el->text_cap ? el->text_cap : 32;
        while (new_cap < need) new_cap *= 2;
        buf = realloc( el->text, new_cap * sizeof(WCHAR) );
        if (!buf) return;
        el->text = buf;
        el->text_cap = (UINT32)new_cap;
    }
    memcpy( el->text, s, actual_n * sizeof(WCHAR) );
    el->text[actual_n] = 0;
    el->text_len = (UINT32)actual_n;
}

void uwp_elem_set_text( struct uwp_element *el, const WCHAR *s )
{
    if (!el) return;
    if (!s || !s[0])
    {
        uwp_elem_clear_text( el );
        return;
    }
    uwp_elem_set_text_n( el, s, wcslen( s ) );
}

void uwp_elem_append_text( struct uwp_element *el, const WCHAR *s )
{
    size_t cur_len, add_len, need;
    size_t alias_off = 0;
    BOOL is_alias = FALSE;

    if (!el || !s || !s[0]) return;
    cur_len = el->text ? wcslen( el->text ) : 0;
    add_len = wcslen( s );
    if (el->text && s >= el->text && s < el->text + el->text_cap)
    {
        is_alias = TRUE;
        alias_off = (size_t)(s - el->text);
    }
    need = cur_len + add_len + 1;
    if (need > el->text_cap)
    {
        size_t new_cap = el->text_cap ? el->text_cap : 32;
        WCHAR *buf;
        while (new_cap < need) new_cap *= 2;
        buf = realloc( el->text, new_cap * sizeof(WCHAR) );
        if (!buf) return;
        el->text = buf;
        el->text_cap = (UINT32)new_cap;
        if (is_alias) s = el->text + alias_off;
    }
    if (is_alias)
        memmove( el->text + cur_len, s, add_len * sizeof(WCHAR) );
    else
        memcpy( el->text + cur_len, s, add_len * sizeof(WCHAR) );
    el->text[cur_len + add_len] = 0;
    el->text_len = (UINT32)(cur_len + add_len);
}

static void register_uwp_elem( struct uwp_element *el )
{
    AcquireSRWLockExclusive( &g_all_uwp_elems_lock );
    el->next_reg = g_all_uwp_elems_head;
    g_all_uwp_elems_head = el;
    g_all_uwp_elem_cnt++;
    ReleaseSRWLockExclusive( &g_all_uwp_elems_lock );
}

static const GUID IID_UWP_ELEM_SELF =
    { 0x7a1b2c3d, 0x4e5f, 0x4678, { 0x9a, 0xbc, 0xde, 0xf0, 0x12, 0x34, 0x56, 0x79 } };

struct uwp_element *uwp_elem_from_any( void *ptr )
{
    struct uwp_element *el;
    void *self_iface = NULL;
    void *inner = NULL;
    if (!ptr || (ULONG_PTR)ptr < 0x10000) return NULL;
    AcquireSRWLockExclusive( &g_all_uwp_elems_lock );
    for (el = g_all_uwp_elems_head; el; el = el->next_reg)
    {
        if ((void *)ptr == (void *)el->outer ||
            (void *)ptr == (void *)el->provider_inst ||
            ((char *)ptr >= (char *)el && (char *)ptr < (char *)(el + 1)))
        {
            ReleaseSRWLockExclusive( &g_all_uwp_elems_lock );
            return el;
        }
    }
    ReleaseSRWLockExclusive( &g_all_uwp_elems_lock );
    /* .NET Native hands us __ComObject RCWs that answer QI from their own
     * projection table (only IUnknown): unwrap get_Inner (slot 6) /
     * get_InnerIUnknown (slot 7) to reach our CCW face, then resolve that.
     * Only when the SELF identity QI failed (i.e. ptr is a foreign RCW). */
    if (SUCCEEDED(IUnknown_QueryInterface( (IUnknown *)ptr, &IID_UWP_ELEM_SELF, &self_iface )) && self_iface)
    {
        void *probe_ptr = self_iface;
        AcquireSRWLockExclusive( &g_all_uwp_elems_lock );
        for (el = g_all_uwp_elems_head; el; el = el->next_reg)
        {
            if (probe_ptr == (void *)&el->nondelegating_iface ||
                ((char *)probe_ptr >= (char *)el && (char *)probe_ptr < (char *)(el + 1)))
            {
                ReleaseSRWLockExclusive( &g_all_uwp_elems_lock );
                IUnknown_Release( (IUnknown *)self_iface );
                return el;
            }
        }
        ReleaseSRWLockExclusive( &g_all_uwp_elems_lock );
        IUnknown_Release( (IUnknown *)self_iface );
    }
    if (ptr != uwp_get_app_outer())
    {
        void *pv_iface = NULL;
        void *ui_iface = NULL;
        void *cand = NULL;
        HSTRING hcls = NULL;
        HRESULT cls_hr;
        /* Boxed primitive PropertyValues and ResourceDictionaries are never UIElements; do not probe vtbl[6]/[7] on them */
        if ((SUCCEEDED(IUnknown_QueryInterface( (IUnknown *)ptr, &IID_IPropertyValue_g, &pv_iface )) && pv_iface) ||
            (SUCCEEDED(IUnknown_QueryInterface( (IUnknown *)ptr, &IID_IResourceDictionary_UWP, &pv_iface )) && pv_iface) ||
            (SUCCEEDED(IUnknown_QueryInterface( (IUnknown *)ptr, &IID_IResourceDictionary_WinUI, &pv_iface )) && pv_iface))
        {
            IUnknown_Release( (IUnknown *)pv_iface );
            return NULL;
        }
        /* If ptr answers IUIElement, resolve directly via IID_UWP_ELEM_SELF or table without probing vtbl[6] */
        if (SUCCEEDED(IUnknown_QueryInterface( (IUnknown *)ptr, &IID_IUIElement_UWP, &ui_iface )) && ui_iface)
        {
            if (SUCCEEDED(IUnknown_QueryInterface( (IUnknown *)ui_iface, &IID_UWP_ELEM_SELF, &self_iface )) && self_iface)
            {
                AcquireSRWLockExclusive( &g_all_uwp_elems_lock );
                for (el = g_all_uwp_elems_head; el; el = el->next_reg)
                {
                    if (self_iface == (void *)&el->nondelegating_iface ||
                        ((char *)self_iface >= (char *)el && (char *)self_iface < (char *)(el + 1)))
                    {
                        ReleaseSRWLockExclusive( &g_all_uwp_elems_lock );
                        IUnknown_Release( (IUnknown *)self_iface );
                        IUnknown_Release( (IUnknown *)ui_iface );
                        return el;
                    }
                }
                ReleaseSRWLockExclusive( &g_all_uwp_elems_lock );
                IUnknown_Release( (IUnknown *)self_iface );
            }
            IUnknown_Release( (IUnknown *)ui_iface );
        }
        /* Only probe MCG __ComObject get_Inner (vtbl[6]/[7]) if GetRuntimeClassName is not implemented */
        cls_hr = E_FAIL;
        __TRY
        {
            cls_hr = IInspectable_GetRuntimeClassName( (IInspectable *)ptr, &hcls );
        }
        __EXCEPT_ALL
        {
            cls_hr = E_FAIL;
        }
        __ENDTRY;
        if (SUCCEEDED(cls_hr))
        {
            if (hcls) WindowsDeleteString( hcls );
            return NULL;
        }
        __TRY
        {
            void **vtbl = *(void ***)ptr;
            HRESULT (WINAPI *get_inner)(void *, void **) = (void *)vtbl[6];
            HRESULT (WINAPI *get_inner_iu)(void *, void **) = (void *)vtbl[7];
            if (get_inner && SUCCEEDED(get_inner( ptr, &cand )) && cand)
                inner = cand;
            else if (get_inner_iu && SUCCEEDED(get_inner_iu( ptr, &cand )) && cand)
                inner = cand;
            else inner = NULL;
        }
        __EXCEPT_ALL
        {
            inner = NULL;
        }
        __ENDTRY;
        if (inner)
        {
            /* Validate: trust inner only when it answers our identity IID
             * (slot 6 on native objects is an arbitrary property getter). */
            void *id = NULL;
            if (FAILED(IUnknown_QueryInterface( (IUnknown *)inner, &IID_UWP_ELEM_SELF, &id )) || !id)
            {
                IUnknown_Release( (IUnknown *)inner );
                return NULL;
            }
            IUnknown_Release( (IUnknown *)id );
            AcquireSRWLockExclusive( &g_all_uwp_elems_lock );
            for (el = g_all_uwp_elems_head; el; el = el->next_reg)
            {
                if ((void *)inner == (void *)el->outer ||
                    (void *)inner == (void *)&el->nondelegating_iface ||
                    ((char *)inner >= (char *)el && (char *)inner < (char *)(el + 1)))
                {
                    ReleaseSRWLockExclusive( &g_all_uwp_elems_lock );
                    IUnknown_Release( (IUnknown *)inner );
                    return el;
                }
            }
            TRACE( "uwp_elem_from_any: INNER-MISS ptr=%p inner=%p cnt=%d\n", ptr, inner, g_all_uwp_elem_cnt );
            ReleaseSRWLockExclusive( &g_all_uwp_elems_lock );
            IUnknown_Release( (IUnknown *)inner );
            return NULL;
        }
    }
    /* Fallback: Try QI for IID_IDependencyObject_UWP to unwrap MCG RCW/CCW */
    {
        void *do_iface = NULL;
        if (SUCCEEDED(IUnknown_QueryInterface( (IUnknown *)ptr, &IID_IDependencyObject_UWP, &do_iface )) && do_iface)
        {
            struct uwp_element *e2;
            int k;
            el = NULL;
            /* RCW may return its own projected face; probe all our faces on it */
            for (k = 0; k < 8; k++)
            {
                void *cand = NULL;
                static const GUID *iids[8] = { &IID_IUIElement_UWP, &IID_IFrameworkElement_UWP,
                    &IID_IControl_UWP, &IID_IColumnDefinition_UWP };
                if (k >= 4) break;
                if (SUCCEEDED(IUnknown_QueryInterface( (IUnknown *)do_iface, iids[k], &cand )) && cand)
                {
                    AcquireSRWLockExclusive( &g_all_uwp_elems_lock );
                    for (e2 = g_all_uwp_elems_head; e2; e2 = e2->next_reg)
                    {
                        if ((void *)cand == (void *)e2->outer ||
                            ((char *)cand >= (char *)e2 && (char *)cand < (char *)(e2 + 1)))
                        { el = e2; break; }
                    }
                    ReleaseSRWLockExclusive( &g_all_uwp_elems_lock );
                    IUnknown_Release( (IUnknown *)cand );
                    if (el) break;
                }
            }
            if (el)
            {
                IUnknown_Release( (IUnknown *)do_iface );
                return el;
            }
            AcquireSRWLockExclusive( &g_all_uwp_elems_lock );
            TRACE( "uwp_elem_from_any: MISS ptr=%p do_iface=%p cnt=%d\n", ptr, do_iface, g_all_uwp_elem_cnt );
            ReleaseSRWLockExclusive( &g_all_uwp_elems_lock );
            IUnknown_Release( (IUnknown *)do_iface );
        }
    }
    return NULL;
}

HWND g_island_hwnds[16];
int g_island_hwnd_cnt = 0;

void uwp_invalidate_window( void )
{
    g_layout_dirty = TRUE;
    if (g_island_hwnd_cnt > 0)
    {
        int i;
        for (i = 0; i < g_island_hwnd_cnt; i++)
        {
            if (g_island_hwnds[i] && IsWindow( g_island_hwnds[i] ) && IsWindowVisible( g_island_hwnds[i] ))
                InvalidateRect( g_island_hwnds[i], NULL, FALSE );
        }
        return;
    }
    {
        HWND top = xaml_find_core_window();
        xaml_mark_window_dirty();
        if (top)
        {
            HWND root = GetAncestor( top, GA_ROOT );
            if (root && root != top) InvalidateRect( root, NULL, FALSE );
            InvalidateRect( top, NULL, FALSE );
        }
    }
}

static HRESULT WINAPI uwp_nondelegating_QueryInterface( IInspectable *iface, REFIID iid, void **out );
static ULONG WINAPI uwp_nondelegating_AddRef( IInspectable *iface );
static ULONG WINAPI uwp_nondelegating_Release( IInspectable *iface );

struct uwp_weak_ref
{
    IWeakReference IWeakReference_iface;
    LONG ref;
    struct uwp_element *elem;
};

static HRESULT WINAPI uwp_wr_QI( IWeakReference *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IWeakReference_local ))
    {
        *out = iface;
        InterlockedIncrement( &((struct uwp_weak_ref *)iface)->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI uwp_wr_AddRef( IWeakReference *iface )
{
    return InterlockedIncrement( &((struct uwp_weak_ref *)iface)->ref );
}
static ULONG WINAPI uwp_wr_Release( IWeakReference *iface )
{
    struct uwp_weak_ref *wr = (struct uwp_weak_ref *)iface;
    ULONG r = InterlockedDecrement( &wr->ref );
    if (!r) free( wr );
    return r;
}
static HRESULT WINAPI uwp_wr_Resolve( IWeakReference *iface, REFIID iid, IInspectable **out )
{
    struct uwp_weak_ref *wr = (struct uwp_weak_ref *)iface;
    if (!out) return E_POINTER;
    *out = NULL;
    if (!wr->elem) return S_OK;
    if (wr->elem->outer)
        return IInspectable_QueryInterface( wr->elem->outer, iid, (void **)out );
    return IInspectable_QueryInterface( &wr->elem->IInspectable_iface, iid, (void **)out );
}
static const struct IWeakReferenceVtbl uwp_weak_ref_vtbl =
{
    uwp_wr_QI,
    uwp_wr_AddRef,
    uwp_wr_Release,
    uwp_wr_Resolve,
};

static HRESULT WINAPI uwp_wrs_QI( IWeakReferenceSource *iface, REFIID iid, void **out )
{
    struct uwp_element *impl = CONTAINING_RECORD( iface, struct uwp_element, IWeakReferenceSource_iface );
    return uwp_nondelegating_QueryInterface( &impl->nondelegating_iface, iid, out );
}
static ULONG WINAPI uwp_wrs_AddRef( IWeakReferenceSource *iface )
{
    struct uwp_element *impl = CONTAINING_RECORD( iface, struct uwp_element, IWeakReferenceSource_iface );
    return uwp_nondelegating_AddRef( &impl->nondelegating_iface );
}
static ULONG WINAPI uwp_wrs_Release( IWeakReferenceSource *iface )
{
    struct uwp_element *impl = CONTAINING_RECORD( iface, struct uwp_element, IWeakReferenceSource_iface );
    return uwp_nondelegating_Release( &impl->nondelegating_iface );
}
static HRESULT WINAPI uwp_wrs_GetWeakReference( IWeakReferenceSource *iface, IWeakReference **out )
{
    struct uwp_element *impl = CONTAINING_RECORD( iface, struct uwp_element, IWeakReferenceSource_iface );
    struct uwp_weak_ref *wr;
    if (!out) return E_POINTER;
    wr = calloc( 1, sizeof(*wr) );
    if (!wr) return E_OUTOFMEMORY;
    wr->IWeakReference_iface.lpVtbl = &uwp_weak_ref_vtbl;
    wr->ref = 1;
    wr->elem = impl;
    *out = &wr->IWeakReference_iface;
    return S_OK;
}
static const struct IWeakReferenceSourceVtbl uwp_wrs_vtbl =
{
    uwp_wrs_QI,
    uwp_wrs_AddRef,
    uwp_wrs_Release,
    uwp_wrs_GetWeakReference,
};

HRESULT WINAPI uwp_sub_QueryInterface( IInspectable *iface, REFIID iid, void **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return E_NOINTERFACE;
    /* Private identity IID: answer locally so app-side RCW QIs can be
     * resolved back to our element (delegation to outer loses this). */
    if (IsEqualGUID( iid, &IID_UWP_ELEM_SELF ))
    {
        if (!out) return E_POINTER;
        *out = &impl->nondelegating_iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IWeakReferenceSource_local ))
    {
        if (!out) return E_POINTER;
        *out = &impl->IWeakReferenceSource_iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    if (impl->outer && *(void **)impl->outer)
    {
        HRESULT hr = E_NOINTERFACE;
        BOOL faulted = FALSE;
        __TRY
        {
            hr = IInspectable_QueryInterface( impl->outer, iid, out );
        }
        __EXCEPT_ALL
        {
            faulted = TRUE;
        }
        __ENDTRY;
        if (!faulted) return hr;
        ERR( "uwp_sub_QI: outer=%p FAULTED on %s (%s), falling back to inner\n",
             impl->outer, debugstr_w(impl->class_name), debugstr_w(impl->name) );
        impl->outer = NULL;
        impl->delegating = FALSE;
    }
    return uwp_nondelegating_QueryInterface( &impl->nondelegating_iface, iid, out );
}

ULONG WINAPI uwp_sub_AddRef( IInspectable *iface )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return 1;
    if (impl->delegating && impl->outer)
        return IInspectable_AddRef( impl->outer );
    return uwp_nondelegating_AddRef( &impl->nondelegating_iface );
}

ULONG WINAPI uwp_sub_Release( IInspectable *iface )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return 1;
    if (impl->delegating && impl->outer)
        return IInspectable_Release( impl->outer );
    return uwp_nondelegating_Release( &impl->nondelegating_iface );
}

HRESULT WINAPI uwp_sub_GetIids( IInspectable *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI uwp_nondelegating_GetRuntimeClassName( IInspectable *iface, HSTRING *name )
{
    struct uwp_element *impl = CONTAINING_RECORD( iface, struct uwp_element, nondelegating_iface );
    const WCHAR *cls = (impl && impl->class_name[0]) ? impl->class_name : L"Windows.UI.Xaml.FrameworkElement";
    return WindowsCreateString( cls, wcslen(cls), name );
}

HRESULT WINAPI uwp_sub_GetRuntimeClassName( IInspectable *iface, HSTRING *name )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    const WCHAR *cls;
    if (impl && impl->outer && iface != &impl->nondelegating_iface)
        return IInspectable_GetRuntimeClassName( impl->outer, name );
    cls = (impl && impl->class_name[0]) ? impl->class_name : L"Windows.UI.Xaml.FrameworkElement";
    return WindowsCreateString( cls, wcslen(cls), name );
}

HRESULT WINAPI uwp_sub_GetTrustLevel( IInspectable *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}

HRESULT WINAPI uwp_generic_noop( void *iface, ... )
{
    return S_OK;
}

static HRESULT WINAPI uwp_nondelegating_QueryInterface( IInspectable *iface, REFIID iid, void **out )
{
    struct uwp_element *impl = CONTAINING_RECORD( iface, struct uwp_element, nondelegating_iface );
    if (!out) return E_POINTER;
    *out = NULL;

    if (IsEqualGUID( iid, &IID_UWP_ELEM_SELF ))
    {
        *out = &impl->nondelegating_iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ))
    {
        *out = &impl->nondelegating_iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IDispatch ))
    {
        *out = &impl->IDispatch_iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IAgileObject ))
    {
        if (impl->delegating && impl->outer)
        {
            *out = &impl->IInspectable_iface;
            IInspectable_AddRef( impl->outer );
            return S_OK;
        }
        *out = &impl->nondelegating_iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IWeakReferenceSource_local ))
    {
        *out = &impl->IWeakReferenceSource_iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IDependencyObject_UWP ))
        *out = &impl->IDependencyObject_iface;
    else if (IsEqualGUID( iid, &IID_IDependencyObject2_UWP ))
        *out = &impl->IDependencyObject2_iface;
    else if (IsEqualGUID( iid, &IID_IUIElement_UWP ))
        *out = &impl->IUIElement_iface;
    else if (IsEqualGUID( iid, &IID_IFrameworkElement_UWP ))
        *out = &impl->IFrameworkElement_iface;
    else if (IsEqualGUID( iid, &IID_IFrameworkElement2_UWP ))
        *out = &impl->IFrameworkElement2_iface;
    else if (IsEqualGUID( iid, &IID_IFrameworkElement3_UWP ))
        *out = &impl->IFrameworkElement3_iface;
    else if (IsEqualGUID( iid, &IID_IControl_UWP ))
        *out = &impl->IControl_iface;
    else if (IsEqualGUID( iid, &IID_IControlProtected_UWP ))
        *out = &impl->IControlProtected_iface;
    else if (IsEqualGUID( iid, &IID_IContentControl_UWP ) ||
             IsEqualGUID( iid, &IID_INavigationView_UWP ) ||
             IsEqualGUID( iid, &IID_INavigationView2_UWP ) ||
             IsEqualGUID( iid, &IID_IDesktopWindowXamlSource_UWP ))
        *out = &impl->IContentControl_iface;
    else if (IsEqualGUID( iid, &IID_IDesktopWindowXamlSourceNative_UWP ) ||
             IsEqualGUID( iid, &IID_IDesktopWindowXamlSourceNative2_UWP ))
        *out = &impl->IDesktopWindowXamlSourceNative_iface;
    else if (IsEqualGUID( iid, &IID_IUserControl_UWP ))
        *out = &impl->IUserControl_iface;
    else if (IsEqualGUID( iid, &IID_IPage_UWP ))
        *out = &impl->IPage_iface;
    else if (IsEqualGUID( iid, &IID_IFrame_UWP ) ||
             IsEqualGUID( iid, &IID_INavigate_UWP ))
        *out = &impl->IFrame_iface;
    else if (IsEqualGUID( iid, &IID_IButtonBase_UWP ) ||
             IsEqualGUID( iid, &IID_IHyperlinkButton_UWP ))
        *out = &impl->IButtonBase_iface;
    else if (IsEqualGUID( iid, &IID_IToggleButton_UWP ) ||
             IsEqualGUID( iid, &IID_IRadioButton_UWP ))
        *out = &impl->IToggleButton_iface;
    else if (IsEqualGUID( iid, &IID_IToggleMenuFlyoutItem_UWP ))
    {
        if (impl->class_flags & (UWP_CF_TOGGLE_MENU_ITEM | UWP_CF_RADIO_MENU_ITEM))
            *out = &impl->IToggleMenuFlyoutItem_iface;
    }
    else if (IsEqualGUID( iid, &IID_IRadioButtons_WinUI ))
        *out = &impl->IStyle_iface;
    else if (IsEqualGUID( iid, &IID_IDataTemplate_UWP ) &&
             !wcsstr( impl->class_name, L"TemplateSelector" ))
        *out = &impl->IDataTemplate_iface;
    else if (IsEqualGUID( iid, &IID_IDataTemplateSelector_UWP ))
        *out = &impl->IDataTemplateSelector_iface;
    else if (IsEqualGUID( iid, &IID_IDataTemplateSelector2_UWP ))
        *out = &impl->IDataTemplateSelector2_iface;
    else if (IsEqualGUID( iid, &IID_IPanel_UWP ) ||
             IsEqualGUID( iid, &IID_IStackPanel_UWP ) ||
             IsEqualGUID( iid, &IID_IStackPanel_UWP2 ) ||
             IsEqualGUID( iid, &IID_IStackPanel2_Real_UWP ) ||
             IsEqualGUID( iid, &IID_IMenuBar_WinUI ))
        *out = &impl->IPanel_iface;
    else if (IsEqualGUID( iid, &IID_IMenuFlyoutSubItem_UWP ))
    {
        if (impl->class_flags & UWP_CF_MENU_FLYOUT_SUB_ITEM)
            *out = &impl->IMenuFlyoutSubItem_iface;
    }
    else if (IsEqualGUID( iid, &IID_IUIElement4_Real_UWP ) ||
             IsEqualGUID( iid, &IID_IUIElement4_UWP ))
        *out = &impl->IUIElement4_iface;
    else if (IsEqualGUID( iid, &IID_IUIElement5_Real_UWP ) ||
             IsEqualGUID( iid, &IID_IUIElement5_UWP ))
        *out = &impl->IUIElement5_iface;
    else if (IsEqualGUID( iid, &IID_IButtonWithFlyout_Real_UWP ) ||
             IsEqualGUID( iid, &IID_IButtonWithFlyout_UWP ))
        *out = &impl->IButtonWithFlyout_iface;
    else if (IsEqualGUID( iid, &IID_IUIElement7_UWP ))
        *out = &impl->IUIElement7_iface;
    else if (IsEqualGUID( iid, &IID_IKeyboardAccelerator_UWP ))
        *out = &impl->IKeyboardAccelerator_iface;
    else if (IsEqualGUID( iid, &IID_IXamlUICommand_UWP ))
        *out = &impl->IXamlUICommand_iface;
    else if (IsEqualGUID( iid, &IID_ICommand_UWP ) ||
             IsEqualGUID( iid, &IID_ICommand_UWP2 ))
        *out = &impl->ICommand_iface;
    else if (IsEqualGUID( iid, &IID_IMenuFlyoutItem_UWP ))
    {
        if (impl->class_flags & UWP_CF_MENU_FLYOUT_ITEM)
            *out = &impl->IMenuFlyoutItem_iface;
    }
    else if (IsEqualGUID( iid, &IID_IMenuFlyoutItem3_UWP ))
    {
        if (impl->class_flags & UWP_CF_MENU_FLYOUT_ITEM)
            *out = &impl->IMenuFlyoutItem3_iface;
    }
    else if (IsEqualGUID( iid, &IID_IMenuFlyout2_Real_UWP ))
        *out = &impl->IMenuFlyout2_iface;
    else if (IsEqualGUID( iid, &IID_IAutoSuggestBox_Real_UWP ))
    {
        *out = &impl->IItemsControl_iface;
    }
    else if (IsEqualGUID( iid, &IID_IAutoSuggestBox2_Real_UWP ))
    {
        if (uwp_is_autosuggest( impl ))
            *out = &impl->IAutoSuggestBox2_iface;
        else
            *out = &impl->ITextBlock_iface;
    }
    else if (IsEqualGUID( iid, &IID_IContentDialog_UWP ) ||
             IsEqualGUID( iid, &IID_IContentDialog_UWP_Alt ))
    {
        if (uwp_is_autosuggest( impl ))
            *out = &impl->IAutoSuggestBox_iface;
        else
            *out = &impl->IContentDialog_iface;
    }
    else if (IsEqualGUID( iid, &IID_IContentDialog2_UWP ))
        *out = &impl->IContentDialog2_iface;
    else if (IsEqualGUID( iid, &IID_ISwapChainPanel_UWP ))
        *out = &impl->ISwapChainPanel_iface;
    else if (IsEqualGUID( iid, &IID_ISwapChainPanelNative_UWP ) ||
             IsEqualGUID( iid, &IID_ISwapChainPanelNative2_UWP ))
        *out = &impl->ISwapChainPanelNative2_iface;
    else if (IsEqualGUID( iid, &IID_ITeachingTip_WinUI ))
        *out = &impl->ITeachingTip_iface;
    else if (IsEqualGUID( iid, &IID_IGrid_UWP ) ||
             IsEqualGUID( iid, &IID_IGrid_UWP2 ))
        *out = &impl->IGrid_iface;
    else if (IsEqualGUID( iid, &IID_IGrid3_Calc ) ||
             IsEqualGUID( iid, &IID_IGrid3_UWP ))
        *out = &impl->IGrid3_iface;
    else if (IsEqualGUID( iid, &IID_IBorder_UWP ) ||
             IsEqualGUID( iid, &IID_IBorder2_Calc ))
        *out = &impl->IBorder_iface;
    else if (IsEqualGUID( iid, &IID_ITextBlock_UWP ))
        *out = &impl->ITextBlock_iface;
    else if (iid->Data1 == 0xe5fff9e2 && iid->Data2 == 0xb968 && iid->Data3 == 0x49e7) /* IRichTextBlock */
        *out = &impl->IRichTextBlock_iface;
    else if (IsEqualGUID( iid, &IID_ITextBlock2_UWP ) ||
             (iid->Data1 == 0x3f209013 && iid->Data2 == 0x03e7 && iid->Data3 == 0x4508)) /* IRichTextBlock2 */
        *out = &impl->ITextBlock2_iface;
    else if (IsEqualGUID( iid, &IID_ITextBlock6_UWP ) ||
             (iid->Data1 == 0xce5eb4c8 && iid->Data2 == 0x2f10 && iid->Data3 == 0x4b88) || /* ITextBlock6 */
             (iid->Data1 == 0x394b0030 && iid->Data2 == 0x9f4e && iid->Data3 == 0x41e6))   /* IRichTextBlock6 */
        *out = &impl->ITextBlock6_iface;
    else if ((iid->Data1 == 0x0bc465dc && iid->Data2 == 0xbe4d && iid->Data3 == 0x4d0d) || /* IStoryboard */
             (iid->Data1 == 0x04d41bb3 && iid->Data2 == 0x8721 && iid->Data3 == 0x519e))   /* IStoryboard (WinUI3) */
        *out = &impl->IStoryboard_iface;
    else if ((iid->Data1 == 0xf83ef59a && iid->Data2 == 0xfa61 && iid->Data3 == 0x4bef) || /* IParagraph */
             (iid->Data1 == 0x54f03373 && iid->Data2 == 0xc273 && iid->Data3 == 0x4f3b) || /* ISpan */
             (iid->Data1 == 0x9839d4a9 && iid->Data2 == 0x02af && iid->Data3 == 0x4811))   /* ISpan (UWP) */
        *out = &impl->IParagraph_iface;
    else if (iid->Data1 == 0x59553c83 && iid->Data2 == 0x0e14 && iid->Data3 == 0x49bd)     /* IRun */
        *out = &impl->IRun_iface;
    else if (iid->Data1 == 0x3f7a0c6b && iid->Data2 == 0xd00f && iid->Data3 == 0x4730)     /* IBinding */
        *out = &impl->IBinding_iface;
    else if (iid->Data1 == 0x300e5d8a && iid->Data2 == 0x1ff3 && iid->Data3 == 0x4d2c)     /* IPropertyPath */
        *out = &impl->IPropertyPath_iface;
    else if (IsEqualGUID( iid, &IID_IRowDefinition_UWP ))
        *out = &impl->IRowDefinition_iface;
    else if (IsEqualGUID( iid, &IID_IColumnDefinition_UWP ))
        *out = &impl->IColumnDefinition_iface;
    else if (IsEqualGUID( iid, &IID_IScrollViewer_UWP ) ||
             IsEqualGUID( iid, &IID_IScrollViewer2_UWP ) ||
             IsEqualGUID( iid, &IID_IScrollViewer3_UWP ))
        *out = &impl->IScrollViewer_iface;
    else if (IsEqualGUID( iid, &IID_IFrameworkElement7_UWP ) ||
             IsEqualGUID( iid, &IID_IFrameworkElement7_Proj ))
        *out = &impl->IFrameworkElement7_iface;
    else if (IsEqualGUID( iid, &IID_IUIElement2_UWP ) ||
             IsEqualGUID( iid, &IID_IUIElement3_UWP ) ||
             IsEqualGUID( iid, &IID_IUIElement5_UWP ) ||
             IsEqualGUID( iid, &IID_IUIElementOverrides_UWP ) ||
             IsEqualGUID( iid, &IID_IUIElement_Calc2 ) ||
             IsEqualGUID( iid, &IID_IFrameworkElement4_UWP ) ||
             IsEqualGUID( iid, &IID_IFrameworkElement6_UWP ) ||
             IsEqualGUID( iid, &IID_IFrameworkElementOverrides_UWP ) ||
             IsEqualGUID( iid, &IID_IFrameworkElementOverrides2_UWP ) ||
             IsEqualGUID( iid, &IID_IControl2_UWP ) ||
             IsEqualGUID( iid, &IID_IControl3_UWP ) ||
             IsEqualGUID( iid, &IID_IControl4_UWP ) ||
             IsEqualGUID( iid, &IID_IControl5_UWP ) ||
             IsEqualGUID( iid, &IID_IControlOverrides_UWP ) ||
             IsEqualGUID( iid, &IID_IContentControl2_UWP ) ||
             IsEqualGUID( iid, &IID_IContentControlOverrides_UWP ) ||
             IsEqualGUID( iid, &IID_IContentControlOvr_Calc ) ||
             IsEqualGUID( iid, &IID_IPageOverrides_UWP ) ||
             IsEqualGUID( iid, &IID_IButton_UWP ) ||
             IsEqualGUID( iid, &IID_IToggleButtonOverrides_UWP ) ||
             IsEqualGUID( iid, &IID_ITextBlock3_UWP ) ||
             IsEqualGUID( iid, &IID_ITextBlock_Calc2 ) ||
             IsEqualGUID( iid, &IID_ITextBlock_Calc3 ) ||
             IsEqualGUID( iid, &IID_ITextBlock_Calc4 ) ||
             IsEqualGUID( iid, &IID_IStyle_UWP ) ||
             IsEqualGUID( iid, &IID_ISetter_UWP ) ||
             IsEqualGUID( iid, &IID_ISetterBase_UWP ) ||
             IsEqualGUID( iid, &IID_IVisualStateGroup_UWP ) ||
             IsEqualGUID( iid, &IID_IVisualState_UWP ))
        *out = &impl->IStyle_iface;
    else if (IsEqualGUID( iid, &IID_IPopup_UWP ) ||
             IsEqualGUID( iid, &IID_IPopup_WinUI ))
        *out = &impl->IPopup_iface;
    else if (IsEqualGUID( iid, &IID_IPopup2_UWP ) ||
             IsEqualGUID( iid, &IID_IPopup2_WinUI ))
        *out = &impl->IPopup2_iface;
    else if (IsEqualGUID( iid, &IID_IPopup3_UWP ) ||
             IsEqualGUID( iid, &IID_IPopup3_WinUI ))
        *out = &impl->IPopup3_iface;
    else if (IsEqualGUID( iid, &IID_IBrush_UWP ))
        *out = &impl->IBrush_iface;
    else if (IsEqualGUID( iid, &IID_ISolidColorBrush_UWP ))
        *out = &impl->ISolidColorBrush_iface;
    else if (IsEqualGUID( iid, &IID_IFontFamily_UWP ))
        *out = &impl->IFontFamily_iface;
    else if (IsEqualGUID( iid, &IID_IFontIcon_UWP ))
        *out = &impl->IFontIcon_iface;
    else if (IsEqualGUID( iid, &IID_IImageBrush_UWP ))
        *out = &impl->IImageBrush_iface;
    else if (IsEqualGUID( iid, &IID_ITileBrush_UWP ) ||
             IsEqualGUID( iid, &IID_ITileBrush_UWP_Alt ))
        *out = &impl->ITileBrush_iface;
    else if (IsEqualGUID( iid, &IID_IGradientBrush_UWP ))
        *out = &impl->IGradientBrush_iface;
    else if (IsEqualGUID( iid, &IID_ILinearGradientBrush_UWP ))
        *out = &impl->ILinearGradientBrush_iface;
    else if (IsEqualGUID( iid, &IID_IGradientStop_UWP ))
        *out = &impl->IGradientStop_iface;
    else if (IsEqualGUID( iid, &IID_IPathGeometry_UWP ))
        *out = &impl->IPathGeometry_iface;
    else if (IsEqualGUID( iid, &IID_IPathGeometry_UWP_Alt ))
        *out = &impl->IPathFigure_iface;
    else
    {
        static const GUID IID_IXamlCompositionBrushBaseProtected_UWP =
            { 0x1513f3d8, 0x0457, 0x4e1c, { 0xad, 0x77, 0x11, 0xc1, 0xd9, 0x87, 0x97, 0x43 } };
        static const GUID IID_IXamlCompositionBrushBase_UWP =
            { 0x410b99e7, 0x9d63, 0x4449, { 0xa3, 0x58, 0x49, 0x62, 0xfd, 0x93, 0x5f, 0x39 } };
        static const GUID IID_ILoadedImageSurface_UWP =
            { 0x091e237c, 0x5217, 0x424b, { 0xb6, 0xea, 0x19, 0x34, 0x18, 0xc2, 0x6d, 0x0b } };
        static const GUID IID_ICompositionSurface_UWP =
            { 0x1527540d, 0x42c7, 0x47a6, { 0xa4, 0x08, 0x66, 0x8f, 0x79, 0xa9, 0x0d, 0xfb } };
        if (IsEqualGUID( iid, &IID_IXamlCompositionBrushBaseProtected_UWP ))
            *out = &impl->IXamlCompositionBrushBaseProtected_iface;
        else if (IsEqualGUID( iid, &IID_IXamlCompositionBrushBase_UWP ))
            *out = &impl->IStyle_iface;
        else if (IsEqualGUID( iid, &IID_ILoadedImageSurface_UWP ))
            *out = &impl->ILoadedImageSurface_iface;
        else if (IsEqualGUID( iid, &IID_ICompositionSurface_UWP ))
            *out = &impl->IInspectable_iface;
    }
    if (!*out && (IsEqualGUID( iid, &IID_IRoutedEventArgs_QI_UWP ) ||
              IsEqualGUID( iid, &IID_IRoutedEventArgs_Clone_UWP ) ||
              IsEqualGUID( iid, &IID_IRoutedEventArgs2_UWP ) ||
              IsEqualGUID( iid, &IID_IRoutedEventArgs_WinUI3 )))
        *out = &impl->IInspectable_iface;
    else if (IsEqualGUID( iid, &IID_ISplitView_UWP ))
        *out = &impl->ISplitView_iface;
    else if (IsEqualGUID( iid, &IID_ISplitView2_UWP ))
        *out = &impl->ISplitView2_iface;
    else if (IsEqualGUID( iid, &IID_IItemsRepeater_UWP ) ||
             IsEqualGUID( iid, &IID_IItemsRepeaterSizingTarget_UWP ))
        *out = &impl->IPanel_iface;
    else if (IsEqualGUID( iid, &IID_IContentPresenter_UWP ))
        *out = &impl->IContentControl_iface;
    else if ((IsEqualGUID( iid, &IID_IFrameProj_UWP ) &&
             (wcsstr( impl->class_name, L".Controls.Frame" ) ||
              wcsstr( impl->class_name, L".Controls.ContentFrame" ))))
        *out = &impl->IFrame2_iface; /* IFrame2: Navigate(TypeName, param, transitionInfo) */
    else if (IsEqualGUID( iid, &IID_IFrame3_UWP ) &&
             (wcsstr( impl->class_name, L".Controls.Frame" ) ||
              wcsstr( impl->class_name, L".Controls.ContentFrame" )))
        *out = &impl->IFrame3_iface;
    else if (IsEqualGUID( iid, &IID_IFrame5_UWP ) &&
             (wcsstr( impl->class_name, L".Controls.Frame" ) ||
              wcsstr( impl->class_name, L".Controls.ContentFrame" )))
        *out = &impl->IFrame5_iface;
    else if (IsEqualGUID( iid, &IID_IMediaPlayerElement_UWP ) &&
             (impl->class_flags & UWP_CF_MEDIA_PLAYER))
        *out = &impl->IMediaPlayerElement_iface;
    else if ((IsEqualGUID( iid, &IID_INavigationTransitionInfo_UWP ) ||
              IsEqualGUID( iid, &IID_IEntranceNavigationTransitionInfo_UWP )) &&
             wcsstr( impl->class_name, L"NavigationTransitionInfo" ))
        *out = &impl->IStyle_iface; /* INavigationTransitionInfo / IEntranceNavigationTransitionInfo */
    else if (IsEqualGUID( iid, &IID_IFlyoutBase5_UWP ) &&
             uwp_is_flyout( impl ))
        *out = &impl->IStyle_iface; /* IFlyoutBase5 */
    else if (IsEqualGUID( iid, &IID_IImage2_UWP ) &&
             wcsstr( impl->class_name, L"Image" ))
        *out = &impl->IImage_iface; /* Windows.UI.Xaml.Controls.IImage */
    else if (IsEqualGUID( iid, &IID_IEasingFunctionBase_UWP ) &&
             (wcsstr( impl->class_name, L"Ease" ) || wcsstr( impl->class_name, L"EasingFunction" )))
        *out = &impl->IEasingFunctionBase_iface;
    else if (IsEqualGUID( iid, &IID_INavigationView_UWP ) &&
             !wcscmp( impl->class_name, L"Microsoft.UI.Xaml.Controls.NavigationView" ))
        *out = &impl->IStyle_iface;
    else if (IsEqualGUID( iid, &IID_INavigationViewItem_UWP ) &&
             !wcscmp( impl->class_name, L"Microsoft.UI.Xaml.Controls.NavigationViewItem" ))
        *out = &impl->IStyle_iface;
    else if (IsEqualGUID( iid, &IID_IFlyoutBase_UWP ) ||
             IsEqualGUID( iid, &IID_IFlyoutBase3_UWP ) ||
             IsEqualGUID( iid, &IID_IFlyoutBase5_UWP ) ||
             IsEqualGUID( iid, &IID_IMenuFlyout_UWP ) ||
             IsEqualGUID( iid, &IID_IMenuFlyout_UWP2 ) ||
             IsEqualGUID( iid, &IID_IMenuFlyoutItemBase_UWP ) ||
             IsEqualGUID( iid, &IID_IMenuFlyoutItemBase_UWP2 ) ||
             IsEqualGUID( iid, &IID_IMenuBarItem_WinUI ) ||
             IsEqualGUID( iid, &IID_ITabView_WinUI ))
        *out = &impl->IFlyout_iface;
    if (!*out)
    {
        static const GUID IID_IListViewBase_UWP =
            { 0xf6ce8c6d, 0xfe96, 0x41ad, { 0xa6, 0x4a, 0xc2, 0xb8, 0x1c, 0x4a, 0xf7, 0xf8 } };
        static const GUID IID_ISelector_UWP =
            { 0xe30eb3a5, 0xb36b, 0x42dc, { 0x85, 0x27, 0xcd, 0x25, 0x13, 0x6c, 0x08, 0x3c } };
        static const GUID IID_IComboBox_UWP =
            { 0xb9a8d05c, 0xac97, 0x47f1, { 0xa5, 0xf4, 0x3f, 0x9f, 0x4d, 0x4b, 0x11, 0x6c } };
        static const GUID IID_IItemsControl_UWP =
            { 0x3d0813ba, 0x6890, 0x4537, { 0xbf, 0xe5, 0x79, 0x6d, 0x94, 0x58, 0xed, 0xd6 } };
        static const GUID IID_IItemsControl_UWP2 =
            { 0xf4a91dd8, 0xd979, 0x4381, { 0x86, 0x52, 0xbd, 0xa0, 0x34, 0x2a, 0x76, 0x5e } };
        static const GUID IID_IListViewBase2_UWP =
            { 0xd1c2a4f7, 0x78a3, 0x4491, { 0x86, 0xe0, 0x2d, 0xde, 0xbc, 0x00, 0x7a, 0xc5 } };
        static const GUID IID_IListViewBase4_UWP =
            { 0x18b7792b, 0x11dc, 0x4eb6, { 0xa7, 0xbc, 0x74, 0x1a, 0xdb, 0x3e, 0xeb, 0x79 } };
        static const GUID IID_IListView_UWP =
            { 0x8eed9bc2, 0xa58c, 0x4453, { 0xaf, 0x0f, 0xa9, 0x2e, 0xe0, 0x6d, 0x03, 0x17 } };
        static const GUID IID_IShape_UWP =
            { 0x786f2b75, 0x9aa0, 0x454d, { 0xae, 0x06, 0xa2, 0x46, 0x6e, 0x37, 0xc8, 0x32 } };
        static const GUID IID_IScrollViewer3_Real_UWP =
            { 0x74e9be21, 0x5de2, 0x595e, { 0xbc, 0x08, 0xdb, 0xd3, 0xc5, 0x77, 0x49, 0x2c } };
        static const GUID IID_IScrollAnchorProvider_UWP =
            { 0x6ae99a0f, 0x478c, 0x5440, { 0xb2, 0xc6, 0xea, 0xd4, 0x37, 0x8e, 0xf8, 0xe4 } };
        static const GUID IID_IStateTrigger_UWP =
            { 0x67adef2e, 0xd8d9, 0x49f7, { 0xa1, 0xfd, 0x2e, 0x35, 0xee, 0xdd, 0x23, 0xcd } };
        static const GUID IID_IBitmapImage_UWP =
            { 0x31af3271, 0xe3b4, 0x442d, { 0xa3, 0x41, 0x4c, 0x02, 0x26, 0xb2, 0x72, 0x5b } };
        static const GUID IID_IBitmapImage2_UWP =
            { 0x1abaee23, 0x74ee, 0x4cc7, { 0x99, 0xba, 0xb1, 0x71, 0xe3, 0xcd, 0xa6, 0x1e } };
        static const GUID IID_IBitmapImage2_Real_UWP =
            { 0x1069c1b6, 0x8c9b, 0x4762, { 0xbe, 0x3d, 0x75, 0x9f, 0x56, 0x98, 0xf2, 0xb3 } };
        static const GUID IID_IBitmapImage3_UWP =
            { 0x7311e0d4, 0xfe94, 0x4e70, { 0x9b, 0x90, 0xcd, 0xd4, 0x7a, 0xc2, 0x3a, 0xfb } };
        static const GUID IID_IBitmapSource_UWP =
            { 0x23d86411, 0x202f, 0x41b2, { 0x8c, 0x5b, 0xa8, 0xa3, 0xb3, 0x33, 0x80, 0x0b } };
        static const GUID IID_IAnimatedVisualPlayer_WinUI =
            { 0xbf7f8865, 0x7b79, 0x5354, { 0x97, 0x88, 0xc2, 0x1e, 0x52, 0x35, 0x91, 0xea } };
        if (IsEqualGUID( iid, &IID_IListViewBase_UWP ))
            *out = &impl->IFlyout_iface;
        else if (IsEqualGUID( iid, &IID_IBitmapImage_UWP ))
            *out = &impl->IBitmapImage_iface;
        else if (IsEqualGUID( iid, &IID_IBitmapSource_UWP ))
            *out = &impl->IBitmapSource_iface;
        else if (IsEqualGUID( iid, &IID_IStateTrigger_UWP ))
            *out = &impl->IStateTriggerPub_iface;
        else if (IsEqualGUID( iid, &IID_IShape_UWP ))
            *out = &impl->IShape_iface;
        else if (IsEqualGUID( iid, &IID_IListViewBase2_UWP ) ||
                 IsEqualGUID( iid, &IID_IListViewBase4_UWP ) ||
                 IsEqualGUID( iid, &IID_IListView_UWP ) ||
                 IsEqualGUID( iid, &IID_IScrollViewer3_Real_UWP ) ||
                 (IsEqualGUID( iid, &IID_IScrollAnchorProvider_UWP ) &&
                  (impl->kind == UWP_KIND_SCROLL_VIEWER || uwp_is_scroll_viewer( impl ))) ||
                 IsEqualGUID( iid, &IID_IBitmapImage2_UWP ) ||
                 IsEqualGUID( iid, &IID_IBitmapImage2_Real_UWP ) ||
                 IsEqualGUID( iid, &IID_IBitmapImage3_UWP ) ||
                 IsEqualGUID( iid, &IID_IAnimatedVisualPlayer_WinUI ))
            *out = &impl->IStyle_iface;
        else if (IsEqualGUID( iid, &IID_ISelector_UWP ))
            *out = &impl->ISelector_iface;
        else if (IsEqualGUID( iid, &IID_IComboBox_UWP ))
            *out = &impl->IComboBox_iface;
        else if (IsEqualGUID( iid, &IID_IItemsControl_UWP ) ||
                 IsEqualGUID( iid, &IID_IItemsControl_UWP2 ))
            *out = &impl->IItemsControl_iface;
    }
    if (!*out)
    {
        if (IsEqualGUID( iid, &IID_IDispatcherTimer_UWP ))
            *out = &impl->IInspectable_iface;
        else if (IsEqualGUID( iid, &IID_IStateTriggerBase_UWP ) ||
                 IsEqualGUID( iid, &IID_IMarkupExtension_UWP ) ||
                 IsEqualGUID( iid, &IID_IMarkupExtensionOverrides_UWP ))
            *out = &impl->IStyle_iface;
        else if (IsEqualGUID( iid, &IID_IStateTriggerBaseProtected_UWP ))
            *out = &impl->IStateTrigger_iface;
        else if (IsEqualGUID( iid, &IID_IUIElement10_UWP ))
            *out = &impl->IUIElement10_iface;
        else if (IsEqualGUID( iid, &IID_IXamlRoot_UWP ))
            *out = &impl->IXamlRoot_iface;
        else
    {
        static const GUID IID_IObservableVector_DO =
            { 0xfac6f4d2, 0x96fd, 0x5ceb, { 0x80, 0xbf, 0x46, 0xac, 0x65, 0x29, 0xfc, 0x65 } };
        static const GUID IID_IObservableVector_ICBE =
            { 0xd5644006, 0x717d, 0x512d, { 0x87, 0x85, 0xb4, 0x8c, 0x17, 0xe7, 0x34, 0x68 } };
        static const GUID IID_IVector_DO =
            { 0x771b857e, 0xab5c, 0x5db8, { 0xa0, 0x21, 0x39, 0x7c, 0x92, 0xcd, 0xc4, 0x4c } };
        static const GUID IID_IVector_MBI =
            { 0xef83f4ba, 0x0b73, 0x51d5, { 0xb1, 0x3b, 0x89, 0xa2, 0xb1, 0x56, 0xad, 0x9a } };
        static const GUID IID_IVector_MFIB =
            { 0xf7f1c37b, 0xb4df, 0x5128, { 0xa4, 0x76, 0xfc, 0x83, 0x99, 0xbc, 0x10, 0xaf } };
        static const GUID IID_IIterable_DO =
            { 0xf66c6bd3, 0x55b4, 0x5bbb, { 0xb8, 0x2a, 0x6d, 0x9c, 0xe3, 0x83, 0x09, 0x1a } };
        static const GUID IID_IVectorView_DO =
            { 0xfe750d77, 0x1307, 0x5df2, { 0xa0, 0x21, 0x1c, 0x7a, 0x8d, 0x6b, 0x80, 0xad } };
        if (IsEqualGUID( iid, &IID_IObservableVector_DO ) ||
            IsEqualGUID( iid, &IID_IObservableVector_ICBE ))
            *out = &impl->IObservableVector_iface;
        else if (IsEqualGUID( iid, &IID_IVector_DO ) ||
                 IsEqualGUID( iid, &IID_IVector_IInspectable_UWP ) ||
                 IsEqualGUID( iid, &IID_IVector_MBI ) ||
                 IsEqualGUID( iid, &IID_IVector_MFIB ) ||
                 IsEqualGUID( iid, &IID_IVector_GradientStop_UWP ))
            *out = &impl->IVector_iface;
        else if (IsEqualGUID( iid, &IID_IIterable_DO ) ||
                 IsEqualGUID( iid, &IID_IIterable_GradientStop_UWP ))
            *out = &impl->IIterable_iface;
        else if (IsEqualGUID( iid, &IID_IVectorView_DO ) ||
                 IsEqualGUID( iid, &IID_IVectorView_GradientStop_UWP ))
            *out = &impl->IVectorView_iface;
        else
        {
            size_t k;
            for (k = 0; k < ARRAY_SIZE(IID_IUIE_Extra); k++)
            {
                if (IsEqualGUID( iid, &IID_IUIE_Extra[k] ))
                {
                    *out = &impl->IStyle_iface;
                    break;
                }
            }
        }
    }
    }
    if (!*out && (IsEqualGUID( iid, &IID_IDCompositionVisual ) ||
                  IsEqualGUID( iid, &IID_IDCompositionVisual2 )))
    {
        if (impl->visual)
        {
            *out = impl->visual;
            IDCompositionVisual_AddRef( impl->visual );
            return S_OK;
        }
    }

    if (*out)
    {
        TRACE( "uwp_nondelegating_QI: matched %s -> %p on %s\n", debugstr_guid(iid), *out, debugstr_w(impl->class_name) );
        uwp_sub_AddRef( (IInspectable *)*out );
        return S_OK;
    }

    if (iid->Data1 != 0x11d3b13a && wcsstr( impl->class_name, L"Grid" ))
        WARN( "uwp_nondelegating_QI: %s not matched on %s\n", debugstr_guid(iid), debugstr_w(impl->class_name) );
    else
        TRACE( "uwp_nondelegating_QI: %s not matched on %s\n", debugstr_guid(iid), debugstr_w(impl->class_name) );
    return E_NOINTERFACE;
}

static ULONG WINAPI uwp_nondelegating_AddRef( IInspectable *iface )
{
    struct uwp_element *impl = CONTAINING_RECORD( iface, struct uwp_element, nondelegating_iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI uwp_nondelegating_Release( IInspectable *iface )
{
    struct uwp_element *impl = CONTAINING_RECORD( iface, struct uwp_element, nondelegating_iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        /* Keep registered elements alive for visual tree lifetime */
        impl->outer = NULL;
        impl->ref = 1;
    }
    return ref;
}

/* --- IContentControl / IUserControl / IBorder --- */
BOOL g_in_tap_diag = FALSE;

static const void *uwp_nondelegating_vtbl[16] =
{
    uwp_nondelegating_QueryInterface,
    uwp_nondelegating_AddRef,
    uwp_nondelegating_Release,
    uwp_sub_GetIids,
    uwp_nondelegating_GetRuntimeClassName,
    uwp_sub_GetTrustLevel,
    [6 ... 15] = uwp_generic_noop,
};

HRESULT uwp_frame_navigate_impl( void *iface, void *type_name_ptr, void *param, void *transition_info, boolean *result );
static HRESULT WINAPI uwp_frame_Navigate( void *iface, void *type_name_ptr, boolean *result )
{
    return uwp_frame_navigate_impl( iface, type_name_ptr, NULL, NULL, result );
}
HRESULT WINAPI uwp_frame2_Navigate( void *iface, void *type_name_ptr, void *param, void *transition_info, boolean *result )
{
    return uwp_frame_navigate_impl( iface, type_name_ptr, param, transition_info, result );
}

static HRESULT WINAPI uwp_frame_add_Navigated( void *iface, void *handler, EventRegistrationToken *token )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return E_NOINTERFACE;
    TRACE( "uwp_frame_add_Navigated on %s (%s) handler=%p\n",
           debugstr_w(impl->class_name), debugstr_w(impl->name), handler );
    return xaml_event_registry_add( &impl->frame_navigated_reg, handler, token );
}
static HRESULT WINAPI uwp_frame_remove_Navigated( void *iface, EventRegistrationToken token )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return E_NOINTERFACE;
    return xaml_event_registry_remove( &impl->frame_navigated_reg, token );
}

static HRESULT WINAPI uwp_frame_get_CacheSize( void *iface, INT32 *out )
{
    if (out) *out = 10;
    return S_OK;
}
static HRESULT WINAPI uwp_frame_get_CanGoBack( void *iface, boolean *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    unsigned int sz = 0;
    if (impl && impl->back_stack) uwp_vec_get_Size( impl->back_stack, &sz );
    if (out) *out = sz > 0 ? TRUE : FALSE;
    return S_OK;
}
static HRESULT WINAPI uwp_frame_get_CanGoForward( void *iface, boolean *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    unsigned int sz = 0;
    if (impl && impl->forward_stack) uwp_vec_get_Size( impl->forward_stack, &sz );
    if (out) *out = sz > 0 ? TRUE : FALSE;
    return S_OK;
}
static HRESULT WINAPI uwp_frame_get_SourcePageType( void *iface, TypeName *out )
{
    if (out) { out->Name = NULL; out->Kind = 0; }
    return S_OK;
}
static HRESULT WINAPI uwp_frame_get_BackStackDepth( void *iface, INT32 *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    unsigned int sz = 0;
    if (impl && impl->back_stack) uwp_vec_get_Size( impl->back_stack, &sz );
    if (out) *out = (INT32)sz;
    return S_OK;
}

HRESULT WINAPI uwp_frame_GoBack( void *iface )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    struct uwp_vec_view *v;
    IInspectable *prev_entry;
    struct uwp_nav_event_args *prev_nea;
    IInspectable *prev_page = NULL;
    struct uwp_element *page_el;

    if (!impl || !impl->back_stack) return S_OK;
    v = CONTAINING_RECORD( impl->back_stack, struct uwp_vec_view, IInspectable_iface );
    TRACE( "uwp_frame_GoBack: impl=%p (%s) count=%u\n", impl, debugstr_w(impl->name), v->count );
    if (v->count == 0) return S_OK;

    prev_entry = v->items[v->count - 1];
    v->items[--v->count] = NULL;
    if (!prev_entry) return S_OK;

    prev_nea = CONTAINING_RECORD( prev_entry, struct uwp_nav_event_args, INavigationEventArgs_iface );
    prev_page = prev_nea->content;
    if (prev_page)
    {
        struct uwp_nav_event_args *back_nea = uwp_nav_event_args_create(
            prev_nea->content, prev_nea->parameter, &prev_nea->source_page_type, prev_nea->transition_info );
        if (back_nea) back_nea->nav_mode = 1; /* NavigationMode_Back */
        if (impl->nav_args_item) uwp_nea_Release( impl->nav_args_item );
        impl->nav_args_item = back_nea ? &back_nea->INavigationEventArgs_iface : NULL;
        if (impl->nav_args_item) IInspectable_AddRef( impl->nav_args_item );
        if (impl->content) IInspectable_Release( impl->content );
        impl->content = prev_page;
        IInspectable_AddRef( prev_page );
        page_el = uwp_elem_from_any( prev_page );
        if (page_el)
        {
            page_el->parent = impl;
            if (!impl->parent || !wcsstr( page_el->class_name, L"Skeleton" ))
                g_navigated_page_el = page_el;
        }
        if (page_el && page_el->outer && back_nea)
        {
            void *page_ovr = NULL;
            if (SUCCEEDED(IInspectable_QueryInterface( page_el->outer, &IID_IPageOverrides_UWP, &page_ovr )) && page_ovr)
            {
                HRESULT (WINAPI *on_nav)(void *, void *) = (void *)(*(void ***)page_ovr)[7];
                __TRY { if (on_nav) on_nav( page_ovr, &back_nea->INavigationEventArgs_iface ); }
                __EXCEPT_ALL { }
                __ENDTRY;
                IUnknown_Release( (IUnknown *)page_ovr );
            }
        }
        if (impl->frame_navigated_reg.next_token > 0 && back_nea)
        {
            void **handlers = NULL;
            unsigned int cnt = xaml_event_registry_snapshot_alloc( &impl->frame_navigated_reg, &handlers );
            IInspectable *sender = impl->outer ? impl->outer : &impl->IInspectable_iface;
            unsigned int hi;
            for (hi = 0; hi < cnt; hi++)
            {
                struct xaml_event_handler *h = (struct xaml_event_handler *)handlers[hi];
                __TRY { h->lpVtbl->Invoke( h, sender, &back_nea->INavigationEventArgs_iface ); }
                __EXCEPT_ALL { }
                __ENDTRY;
                IUnknown_Release( (IUnknown *)handlers[hi] );
            }
            free( handlers );
        }
        if (back_nea) uwp_nea_Release( &back_nea->INavigationEventArgs_iface );
        g_layout_dirty = TRUE;
        xaml_window_present_current();
    }
    uwp_nea_Release( prev_entry );
    return S_OK;
}

static HRESULT WINAPI uwp_frame_NavigateWithParam( void *iface, void *type_name_ptr, void *param, boolean *result );

static const void *uwp_frame_vtbl[48] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_frame_get_CacheSize,
    [7] = uwp_generic_noop,        /* put_CacheSize */
    [8] = uwp_frame_get_CanGoBack,
    [9] = uwp_frame_get_CanGoForward,
    [10] = uwp_frame_get_SourcePageType, /* get_CurrentSourcePageType */
    [11] = uwp_frame_get_SourcePageType, /* get_SourcePageType */
    [12] = uwp_generic_noop,       /* put_SourcePageType */
    [13] = uwp_frame_get_BackStackDepth,
    [14] = uwp_frame_add_Navigated,
    [15] = uwp_frame_remove_Navigated,
    [16] = uwp_ui_event_add,       /* add_Navigating */
    [17] = uwp_generic_noop,       /* remove_Navigating */
    [18] = uwp_ui_event_add,       /* add_NavigationFailed */
    [19] = uwp_generic_noop,       /* remove_NavigationFailed */
    [20] = uwp_ui_event_add,       /* add_NavigationStopped */
    [21] = uwp_generic_noop,       /* remove_NavigationStopped */
    [22] = uwp_frame_GoBack,       /* GoBack */
    [23] = uwp_frame_Navigate,
    [24] = uwp_frame_NavigateWithParam,
    [25 ... 47] = uwp_generic_noop,
};





BOOL uwp_handle_mouse_input( HWND hwnd, UINT msg, int x, int y, int width, int height, IInspectable *root_content );


ID3D11Device *g_island_d3d11_dev = NULL;
ID3D11DeviceContext *g_island_d3d11_ctx = NULL;
BOOL g_island_d3d11_failed = FALSE;

static HRESULT WINAPI uwp_rtt_QI( IInspectable *iface, REFIID iid, void **out )
{
    return uwp_sub_QueryInterface( iface, iid, out );
}
static ULONG WINAPI uwp_rtt_AddRef( IInspectable *iface )
{
    return uwp_sub_AddRef( iface );
}
static ULONG WINAPI uwp_rtt_Release( IInspectable *iface )
{
    return uwp_sub_Release( iface );
}
static HRESULT WINAPI uwp_rtt_GetIids_( IInspectable *iface, ULONG *count, GUID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}
static HRESULT WINAPI uwp_rtt_GetRuntimeClassName_( IInspectable *iface, HSTRING *name )
{
    return uwp_sub_GetRuntimeClassName( iface, name );
}
static HRESULT WINAPI uwp_rtt_GetTrustLevel_( IInspectable *iface, UINT *level )
{
    if (level) *level = 0 /* BaseTrust */;
    return S_OK;
}
static HRESULT WINAPI uwp_rtt_AddRefToTracker( IInspectable *iface )
{
    uwp_sub_AddRef( iface );
    return S_OK;
}
static HRESULT WINAPI uwp_rtt_ReleaseToTracker( IInspectable *iface )
{
    uwp_sub_Release( iface );
    return S_OK;
}
static HRESULT WINAPI uwp_rtt_GetStrongValue( IInspectable *iface, IInspectable **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    *out = &impl->nondelegating_iface;
    uwp_nondelegating_AddRef( &impl->nondelegating_iface );
    return S_OK;
}
const struct uwp_rtt_target_vtbl uwp_rtt_vtbl =
{
    { uwp_rtt_QI, uwp_rtt_AddRef, uwp_rtt_Release,
      uwp_rtt_GetIids_, uwp_rtt_GetRuntimeClassName_, uwp_rtt_GetTrustLevel_ },
    uwp_rtt_AddRefToTracker,
    uwp_rtt_ReleaseToTracker,
    uwp_rtt_GetStrongValue,
};
static double uwp_ease_in_core( const WCHAR *cls, double t )
{
    if (wcsstr( cls, L"SineEase" ))
        return 1.0 - sin( (1.0 - t) * 1.5707963267948966 );
    if (wcsstr( cls, L"QuadraticEase" ))
        return t * t;
    if (wcsstr( cls, L"CubicEase" ))
        return t * t * t;
    if (wcsstr( cls, L"QuarticEase" ))
        return t * t * t * t;
    if (wcsstr( cls, L"QuinticEase" ))
        return t * t * t * t * t;
    if (wcsstr( cls, L"CircleEase" ))
        return 1.0 - sqrt( fmax( 0.0, 1.0 - t * t ) );
    if (wcsstr( cls, L"ExponentialEase" ))
        return (t <= 0.0) ? 0.0 : (exp( 2.0 * t ) - 1.0) / (exp( 2.0 ) - 1.0);
    if (wcsstr( cls, L"PowerEase" ))
        return t * t;
    return t;
}

static HRESULT WINAPI uwp_ease_get_EasingMode( IInspectable *iface, INT32 *val )
{
    struct uwp_element *impl = CONTAINING_RECORD( iface, struct uwp_element, IEasingFunctionBase_iface );
    if (!val) return E_POINTER;
    *val = impl->easing_mode;
    return S_OK;
}

static HRESULT WINAPI uwp_ease_put_EasingMode( IInspectable *iface, INT32 val )
{
    struct uwp_element *impl = CONTAINING_RECORD( iface, struct uwp_element, IEasingFunctionBase_iface );
    impl->easing_mode = val;
    return S_OK;
}

static HRESULT WINAPI uwp_ease_Ease( IInspectable *iface, double t, double *out )
{
    struct uwp_element *impl = CONTAINING_RECORD( iface, struct uwp_element, IEasingFunctionBase_iface );
    if (!out) return E_POINTER;
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;
    /* EasingMode: 0=EaseOut, 1=EaseIn, 2=EaseInOut */
    if (impl->easing_mode == 1)
        *out = uwp_ease_in_core( impl->class_name, t );
    else if (impl->easing_mode == 2)
        *out = (t < 0.5) ? (uwp_ease_in_core( impl->class_name, t * 2.0 ) * 0.5)
                         : ((1.0 - uwp_ease_in_core( impl->class_name, (1.0 - t) * 2.0 )) * 0.5 + 0.5);
    else
        *out = 1.0 - uwp_ease_in_core( impl->class_name, 1.0 - t );
    return S_OK;
}

static const void *uwp_ease_vtbl[] =
{
    uwp_sub_QueryInterface, uwp_sub_AddRef, uwp_sub_Release,
    uwp_sub_GetIids, uwp_sub_GetRuntimeClassName, uwp_sub_GetTrustLevel,
    uwp_ease_get_EasingMode, /* slot 6: get_EasingMode */
    uwp_ease_put_EasingMode, /* slot 7: put_EasingMode */
    uwp_ease_Ease,           /* slot 8: Ease(double) */
};


static HRESULT WINAPI uwp_scp_get_CompositionScaleX( void *iface, float *val )
{
    if (val) *val = 1.0f;
    return S_OK;
}
static HRESULT WINAPI uwp_scp_get_CompositionScaleY( void *iface, float *val )
{
    if (val) *val = 1.0f;
    return S_OK;
}
static const GUID IID_ICoreInputSourceBase_UWP =
    { 0x9f488807, 0x4580, 0x4be8, { 0xbe, 0x68, 0x92, 0xa9, 0x31, 0x17, 0x13, 0xbb } };
static const GUID IID_ICorePointerInputSource_UWP =
    { 0xbbf1bb18, 0xe47a, 0x48eb, { 0x88, 0x07, 0xf8, 0xf8, 0xd3, 0xea, 0x45, 0x51 } };
static const GUID IID_ICorePointerInputSource2_UWP =
    { 0xd70370ba, 0x1953, 0x4271, { 0xb8, 0x65, 0x72, 0x77, 0x0c, 0x92, 0x7d, 0x03 } };

#define CIIS_MAX_HANDLERS 16
#define CIIS_MAX_SOURCES  16

struct core_ind_input_source
{
    const void **lpVtbl_base;
    const void **lpVtbl_ptr;
    const void **lpVtbl_ptr2;
    LONG ref;
    LONG next_token;
    boolean input_enabled;
    boolean has_capture;
    float last_x;
    float last_y;
    void *handlers[7][CIIS_MAX_HANDLERS];
    INT64 tokens[7][CIIS_MAX_HANDLERS];
};

static CRITICAL_SECTION g_ciis_cs = { NULL, -1, 0, 0, 0, 0 };
static struct core_ind_input_source *g_ciis_sources[CIIS_MAX_SOURCES];

static inline struct core_ind_input_source *ciis_from_base( void *iface )
{ return CONTAINING_RECORD( iface, struct core_ind_input_source, lpVtbl_base ); }
static inline struct core_ind_input_source *ciis_from_ptr( void *iface )
{ return CONTAINING_RECORD( iface, struct core_ind_input_source, lpVtbl_ptr ); }
static inline struct core_ind_input_source *ciis_from_ptr2( void *iface )
{ return CONTAINING_RECORD( iface, struct core_ind_input_source, lpVtbl_ptr2 ); }

static HRESULT WINAPI ciis_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct core_ind_input_source *impl = ciis_from_base( iface );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_ICoreInputSourceBase_UWP ))
    {
        InterlockedIncrement( &impl->ref );
        *out = &impl->lpVtbl_base;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_ICorePointerInputSource_UWP ))
    {
        InterlockedIncrement( &impl->ref );
        *out = &impl->lpVtbl_ptr;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_ICorePointerInputSource2_UWP ))
    {
        InterlockedIncrement( &impl->ref );
        *out = &impl->lpVtbl_ptr2;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI ciis_AddRef( void *iface )
{ return InterlockedIncrement( &ciis_from_base( iface )->ref ); }
static ULONG WINAPI ciis_Release( void *iface )
{
    struct core_ind_input_source *impl = ciis_from_base( iface );
    ULONG r = InterlockedDecrement( &impl->ref );
    if (!r)
    {
        int i, k;
        EnterCriticalSection( &g_ciis_cs );
        for (i = 0; i < CIIS_MAX_SOURCES; i++)
            if (g_ciis_sources[i] == impl) g_ciis_sources[i] = NULL;
        LeaveCriticalSection( &g_ciis_cs );
        for (k = 0; k < 7; k++)
            for (i = 0; i < CIIS_MAX_HANDLERS; i++)
                if (impl->handlers[k][i]) IUnknown_Release( (IUnknown *)impl->handlers[k][i] );
        free( impl );
    }
    return r;
}
static HRESULT WINAPI ciis_GetIids( void *iface, ULONG *cnt, IID **iids )
{ if (cnt) *cnt = 0; if (iids) *iids = NULL; return S_OK; }
static HRESULT WINAPI ciis_GetRuntimeClassName( void *iface, HSTRING *name )
{ return WindowsCreateString( L"Windows.UI.Core.CoreIndependentInputSource", 42, name ); }
static HRESULT WINAPI ciis_GetTrustLevel( void *iface, TrustLevel *lvl )
{ if (lvl) *lvl = BaseTrust; return S_OK; }

static HRESULT WINAPI ciis_get_Dispatcher( void *iface, ICoreDispatcher **out )
{
    ICoreWindow *cw = get_current_core_window();
    if (!out) return E_POINTER;
    *out = NULL;
    if (cw) return ICoreWindow_get_Dispatcher( cw, out );
    return S_OK;
}
static HRESULT WINAPI ciis_get_IsInputEnabled( void *iface, boolean *val )
{ if (val) *val = ciis_from_base( iface )->input_enabled; return S_OK; }
static HRESULT WINAPI ciis_put_IsInputEnabled( void *iface, boolean val )
{ ciis_from_base( iface )->input_enabled = val; return S_OK; }
static HRESULT WINAPI ciis_event_add( void *iface, void *handler, EventRegistrationToken *token )
{ if (token) token->value = 1; return S_OK; }
static HRESULT WINAPI ciis_event_remove( void *iface, EventRegistrationToken token )
{ return S_OK; }

static HRESULT ciis_ptr_add_kind( void *iface, int kind, void *handler, EventRegistrationToken *token )
{
    struct core_ind_input_source *impl = ciis_from_ptr( iface );
    int i;
    if (!handler) return E_POINTER;
    EnterCriticalSection( &g_ciis_cs );
    for (i = 0; i < CIIS_MAX_HANDLERS; i++)
    {
        if (!impl->handlers[kind][i])
        {
            INT64 tok = InterlockedIncrement( &impl->next_token );
            IUnknown_AddRef( (IUnknown *)handler );
            impl->handlers[kind][i] = handler;
            impl->tokens[kind][i] = tok;
            if (token) token->value = tok;
            LeaveCriticalSection( &g_ciis_cs );
            ERR( "ciis_ptr_add_kind kind=%d handler=%p tok=%lld\n", kind, handler, (long long)tok );
            return S_OK;
        }
    }
    LeaveCriticalSection( &g_ciis_cs );
    return E_OUTOFMEMORY;
}

static HRESULT ciis_ptr_remove_kind( void *iface, int kind, EventRegistrationToken token )
{
    struct core_ind_input_source *impl = ciis_from_ptr( iface );
    int i;
    EnterCriticalSection( &g_ciis_cs );
    for (i = 0; i < CIIS_MAX_HANDLERS; i++)
    {
        if (impl->handlers[kind][i] && impl->tokens[kind][i] == token.value)
        {
            void *h = impl->handlers[kind][i];
            impl->handlers[kind][i] = NULL;
            impl->tokens[kind][i] = 0;
            LeaveCriticalSection( &g_ciis_cs );
            IUnknown_Release( (IUnknown *)h );
            return S_OK;
        }
    }
    LeaveCriticalSection( &g_ciis_cs );
    return S_OK;
}

#define CIIS_PTR_EV( name, kind ) \
static HRESULT WINAPI ciis_add_##name( void *iface, void *h, EventRegistrationToken *t ) \
{ return ciis_ptr_add_kind( iface, kind, h, t ); } \
static HRESULT WINAPI ciis_remove_##name( void *iface, EventRegistrationToken t ) \
{ return ciis_ptr_remove_kind( iface, kind, t ); }

CIIS_PTR_EV( PointerPressed,      XAML_PTR_PRESSED )
CIIS_PTR_EV( PointerMoved,        XAML_PTR_MOVED )
CIIS_PTR_EV( PointerReleased,     XAML_PTR_RELEASED )
CIIS_PTR_EV( PointerWheelChanged, XAML_PTR_WHEEL )
CIIS_PTR_EV( PointerEntered,      XAML_PTR_ENTERED )
CIIS_PTR_EV( PointerExited,       XAML_PTR_EXITED )
CIIS_PTR_EV( PointerCaptureLost,  XAML_PTR_CAPTURE_LOST )

void uwp_ciis_dispatch_pointer_event( enum xaml_pointer_event_kind kind, const struct xaml_pointer_info *info )
{
    struct core_ind_input_source *srcs[CIIS_MAX_SOURCES];
    void *handlers[CIIS_MAX_HANDLERS];
    int n_srcs = 0, i, k;
    __x_ABI_CWindows_CUI_CCore_CIPointerEventArgs *args = NULL;

    if ((unsigned int)kind >= 7 || !info) return;

    EnterCriticalSection( &g_ciis_cs );
    for (i = 0; i < CIIS_MAX_SOURCES; i++)
    {
        if (g_ciis_sources[i] && g_ciis_sources[i]->input_enabled)
        {
            InterlockedIncrement( &g_ciis_sources[i]->ref );
            srcs[n_srcs++] = g_ciis_sources[i];
        }
    }
    LeaveCriticalSection( &g_ciis_cs );

    if (!n_srcs) return;
    if (kind == XAML_PTR_PRESSED || kind == XAML_PTR_WHEEL)
        ERR( "uwp_ciis_dispatch_pointer_event kind=%d pos=(%.1f,%.1f) R=%d L=%d wheel=%d srcs=%d\n",
             kind, info->x, info->y, info->is_right, info->is_left, info->wheel_delta, n_srcs );
    args = xaml_core_pointer_args_create( info );
    if (!args)
    {
        for (i = 0; i < n_srcs; i++) ciis_Release( &srcs[i]->lpVtbl_base );
        return;
    }

    for (i = 0; i < n_srcs; i++)
    {
        struct core_ind_input_source *src = srcs[i];
        int n_h = 0;
        src->last_x = info->x;
        src->last_y = info->y;
        EnterCriticalSection( &g_ciis_cs );
        for (k = 0; k < CIIS_MAX_HANDLERS; k++)
        {
            if (src->handlers[kind][k])
            {
                IUnknown_AddRef( (IUnknown *)src->handlers[kind][k] );
                handlers[n_h++] = src->handlers[kind][k];
            }
        }
        LeaveCriticalSection( &g_ciis_cs );
        for (k = 0; k < n_h; k++)
        {
            typedef HRESULT (WINAPI *invoke_fn)( void *iface, void *sender, void *args );
            void ***vtbl = (void ***)handlers[k];
            invoke_fn fn = (invoke_fn)((*vtbl)[3]);
            __TRY
            {
                fn( handlers[k], &src->lpVtbl_ptr, args );
            }
            __EXCEPT_ALL
            {
            }
            __ENDTRY;
            IUnknown_Release( (IUnknown *)handlers[k] );
        }
        ciis_Release( &src->lpVtbl_base );
    }
    __x_ABI_CWindows_CUI_CCore_CIPointerEventArgs_Release( args );
}

static const void *ciis_base_vtbl[] =
{
    ciis_QueryInterface,
    ciis_AddRef,
    ciis_Release,
    ciis_GetIids,
    ciis_GetRuntimeClassName,
    ciis_GetTrustLevel,
    ciis_get_Dispatcher,
    ciis_get_IsInputEnabled,
    ciis_put_IsInputEnabled,
    ciis_event_add,
    ciis_event_remove,
};

static HRESULT WINAPI ciis_ptr_QI( void *iface, REFIID iid, void **out )
{ return ciis_QueryInterface( &ciis_from_ptr( iface )->lpVtbl_base, iid, out ); }
static ULONG WINAPI ciis_ptr_AddRef( void *iface )
{ return ciis_AddRef( &ciis_from_ptr( iface )->lpVtbl_base ); }
static ULONG WINAPI ciis_ptr_Release( void *iface )
{ return ciis_Release( &ciis_from_ptr( iface )->lpVtbl_base ); }
static HRESULT WINAPI ciis_ptr_ReleaseCapture( void *iface )
{ ciis_from_ptr( iface )->has_capture = FALSE; return S_OK; }
static HRESULT WINAPI ciis_ptr_SetCapture( void *iface )
{ ciis_from_ptr( iface )->has_capture = TRUE; return S_OK; }
static HRESULT WINAPI ciis_ptr_get_HasCapture( void *iface, boolean *val )
{ if (val) *val = ciis_from_ptr( iface )->has_capture; return S_OK; }
static HRESULT WINAPI ciis_ptr_get_PointerPosition( void *iface, Point *pt )
{
    struct core_ind_input_source *impl = ciis_from_ptr( iface );
    if (pt) { pt->X = impl->last_x; pt->Y = impl->last_y; }
    return S_OK;
}
static HRESULT WINAPI ciis_ptr_get_PointerCursor( void *iface, void **out )
{ if (out) *out = NULL; return S_OK; }
static HRESULT WINAPI ciis_ptr_put_PointerCursor( void *iface, void *cursor )
{ return S_OK; }

static const void *ciis_ptr_vtbl[] =
{
    ciis_ptr_QI,
    ciis_ptr_AddRef,
    ciis_ptr_Release,
    ciis_GetIids,
    ciis_GetRuntimeClassName,
    ciis_GetTrustLevel,
    ciis_ptr_ReleaseCapture,      /* 6: ReleasePointerCapture */
    ciis_ptr_SetCapture,          /* 7: SetPointerCapture */
    ciis_ptr_get_HasCapture,      /* 8: get_HasCapture */
    ciis_ptr_get_PointerPosition, /* 9: get_PointerPosition */
    ciis_ptr_get_PointerCursor,   /* 10: get_PointerCursor */
    ciis_ptr_put_PointerCursor,   /* 11: put_PointerCursor */
    ciis_add_PointerCaptureLost,      /* 12: add_PointerCaptureLost */
    ciis_remove_PointerCaptureLost,   /* 13: remove_PointerCaptureLost */
    ciis_add_PointerEntered,          /* 14: add_PointerEntered */
    ciis_remove_PointerEntered,       /* 15: remove_PointerEntered */
    ciis_add_PointerExited,           /* 16: add_PointerExited */
    ciis_remove_PointerExited,        /* 17: remove_PointerExited */
    ciis_add_PointerMoved,            /* 18: add_PointerMoved */
    ciis_remove_PointerMoved,         /* 19: remove_PointerMoved */
    ciis_add_PointerPressed,          /* 20: add_PointerPressed */
    ciis_remove_PointerPressed,       /* 21: remove_PointerPressed */
    ciis_add_PointerReleased,         /* 22: add_PointerReleased */
    ciis_remove_PointerReleased,      /* 23: remove_PointerReleased */
    ciis_add_PointerWheelChanged,     /* 24: add_PointerWheelChanged */
    ciis_remove_PointerWheelChanged,  /* 25: remove_PointerWheelChanged */
};

static HRESULT WINAPI ciis_ptr2_QI( void *iface, REFIID iid, void **out )
{ return ciis_QueryInterface( &ciis_from_ptr2( iface )->lpVtbl_base, iid, out ); }
static ULONG WINAPI ciis_ptr2_AddRef( void *iface )
{ return ciis_AddRef( &ciis_from_ptr2( iface )->lpVtbl_base ); }
static ULONG WINAPI ciis_ptr2_Release( void *iface )
{ return ciis_Release( &ciis_from_ptr2( iface )->lpVtbl_base ); }
static HRESULT WINAPI ciis_ptr2_get_DispatcherQueue( void *iface, void **out )
{ return uwp_do_get_DispatcherQueue( NULL, out ); }

static const void *ciis_ptr2_vtbl[] =
{
    ciis_ptr2_QI,
    ciis_ptr2_AddRef,
    ciis_ptr2_Release,
    ciis_GetIids,
    ciis_GetRuntimeClassName,
    ciis_GetTrustLevel,
    ciis_ptr2_get_DispatcherQueue,
};

static HRESULT WINAPI uwp_scp_CreateCoreIndependentInputSource( void *iface, UINT32 device_types, void **out )
{
    struct core_ind_input_source *src;
    int i;
    ERR( "uwp_scp_CreateCoreIndependentInputSource dev=%#x\n", device_types );
    if (!out) return E_POINTER;
    if (!(src = calloc( 1, sizeof(*src) ))) return E_OUTOFMEMORY;
    src->lpVtbl_base = ciis_base_vtbl;
    src->lpVtbl_ptr = ciis_ptr_vtbl;
    src->lpVtbl_ptr2 = ciis_ptr2_vtbl;
    src->ref = 1;
    src->input_enabled = TRUE;
    EnterCriticalSection( &g_ciis_cs );
    for (i = 0; i < CIIS_MAX_SOURCES; i++)
    {
        if (!g_ciis_sources[i])
        {
            g_ciis_sources[i] = src;
            break;
        }
    }
    LeaveCriticalSection( &g_ciis_cs );
    *out = &src->lpVtbl_base;
    return S_OK;
}
static const void *uwp_scp_vtbl[16] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_scp_get_CompositionScaleX,
    [7] = uwp_scp_get_CompositionScaleY,
    [8] = uwp_ui_event_add,
    [9] = uwp_generic_noop,
    [10] = uwp_scp_CreateCoreIndependentInputSource,
    [11 ... 15] = uwp_generic_noop,
};
void uwp_notify_swapchain_panel_resize( void *el_ptr, UINT w, UINT h )
{
    struct uwp_element *impl = (struct uwp_element *)el_ptr;
    if (!impl || w <= 1 || h <= 1) return;
    impl->actual_w = (double)w;
    impl->actual_h = (double)h;
    uwp_fire_size_and_layout_changed( impl );
}

static HRESULT WINAPI uwp_scp_native_SetSwapChain( void *iface, void *sc )
{
    struct uwp_element *impl = CONTAINING_RECORD( iface, struct uwp_element, ISwapChainPanelNative2_iface );
    HRESULT hr;
    ERR( "uwp_scp_native_SetSwapChain iface=%p sc=%p\n", iface, sc );
    xaml_set_active_swapchain_panel( impl, sc );
    hr = xaml_attach_swapchain_panel_content( (IUnknown *)sc, &impl->scp_visual );
    if (sc)
    {
        UINT w = 0, h = 0;
        xaml_get_host_window_size( &w, &h );
        if (w > 1 && h > 1)
        {
            impl->actual_w = (double)w;
            impl->actual_h = (double)h;
            ERR( "uwp_scp_native_SetSwapChain firing SizeChanged %ux%u (handlers=%d)\n",
                 w, h, impl->size_changed_cnt );
            uwp_fire_size_and_layout_changed( impl );
        }
    }
    return hr;
}
static HRESULT WINAPI uwp_scp_native_SetSwapChainHandle( void *iface, HANDLE h )
{
    struct uwp_element *impl = CONTAINING_RECORD( iface, struct uwp_element, ISwapChainPanelNative2_iface );
    ERR( "uwp_scp_native_SetSwapChainHandle iface=%p h=%p\n", iface, h );
    return xaml_attach_swapchain_panel_handle( h, &impl->scp_visual );
}
static const void *uwp_scp_native_vtbl[8] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_scp_native_SetSwapChain,
    [4] = uwp_scp_native_SetSwapChainHandle,
    [5 ... 7] = uwp_generic_noop,
};

HRESULT uwp_element_create( const WCHAR *class_name, IInspectable *outer, IInspectable **inner, void **instance )
{
    struct uwp_element *impl;
    if (!instance) return E_POINTER;
    impl = calloc( 1, sizeof(*impl) );
    if (!impl) return E_OUTOFMEMORY;

    impl->nondelegating_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_nondelegating_vtbl;
    impl->IInspectable_iface.lpVtbl = (class_name && !wcscmp( class_name, L"Windows.UI.Xaml.DispatcherTimer" ))
        ? (const struct IInspectableVtbl *)uwp_timer_vtbl
        : (class_name && !wcscmp( class_name, L"Microsoft.UI.Xaml.Controls.NavigationView" ))
        ? (const struct IInspectableVtbl *)uwp_navview_vtbl
        : (const struct IInspectableVtbl *)uwp_fe_vtbl;
    impl->IDependencyObject_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_do_vtbl;
    impl->IDependencyObject2_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_do2_vtbl;
    impl->IUIElement_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_ui_vtbl;
    impl->IFrameworkElement_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_fe_vtbl;
    impl->IFrameworkElement2_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_fe2_vtbl;
    impl->IFrameworkElement3_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_fe3_vtbl;
    impl->IFrameworkElement7_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_fe7_vtbl;
    impl->IControl_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_ctrl_vtbl;
    impl->IControlProtected_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_ctrl_prot_vtbl;
    impl->IContentControl_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_cc_vtbl;
    impl->IUserControl_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_cc_vtbl;
    impl->IPage_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_page_vtbl;
    impl->IFrame_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_frame_vtbl;
    impl->IFrame2_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_frame2_vtbl;
    impl->IFrame3_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_frame3_vtbl;
    impl->IFrame5_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_frame5_vtbl;
    impl->nav_stack_enabled = TRUE;
    impl->IButtonBase_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_btn_vtbl;
    impl->IToggleButton_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_tbtn_vtbl;
    impl->IPanel_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_panel_vtbl;
    impl->IGrid_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_grid_vtbl;
    impl->IGrid3_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_grid3_vtbl;
    impl->IBorder_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_border_vtbl;
    impl->ITextBlock_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_tb_vtbl;
    impl->IParagraph_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_paragraph_vtbl;
    impl->IRun_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_run_vtbl;
    impl->IBinding_iface.lpVtbl = (const struct IInspectableVtbl *)&uwp_binding_vtbl;
    impl->IPropertyPath_iface.lpVtbl = (const struct IInspectableVtbl *)&uwp_pp_vtbl;
    impl->IRowDefinition_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_rowcol_vtbl;
    impl->IColumnDefinition_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_rowcol_vtbl;
    impl->IStyle_iface.lpVtbl = (!wcscmp( class_name, L"Windows.UI.Xaml.VisualState" ) ||
                                 !wcscmp( class_name, L"Windows.UI.Xaml.VisualStateGroup" ))
        ? (const struct IInspectableVtbl *)uwp_visual_state_vtbl
        : (const struct IInspectableVtbl *)uwp_noop_vtbl;
    impl->IDataTemplate_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_data_template_vtbl;
    impl->IDataTemplateSelector_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_dts_vtbl;
    impl->IDataTemplateSelector2_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_dts2_vtbl;
    impl->IBrush_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_brush_vtbl;
    impl->ISolidColorBrush_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_scb_vtbl;
    impl->IFlyout_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_flyout_vtbl;
    impl->ISelector_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_selector_vtbl;
    impl->IComboBox_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_combobox_vtbl;
    impl->IItemsControl_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_items_ctrl_vtbl;
    impl->IStateTrigger_iface.lpVtbl = (const struct IInspectableVtbl *)&uwp_state_trigger_vtbl;
    impl->IStateTriggerPub_iface.lpVtbl = (const struct IInspectableVtbl *)&uwp_state_trigger_pub_vtbl;
    impl->IObservableVector_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_el_obsvec_vtbl;
    impl->IVector_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_el_vec_vtbl;
    impl->IIterable_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_el_iterable_vtbl;
    impl->IVectorView_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_el_vecview_vtbl;
    impl->IScrollViewer_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_sv_vtbl;
    impl->IDesktopWindowXamlSourceNative_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_dwxs_native_vtbl;
    impl->IUIElement7_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_ui7_vtbl;
    impl->IKeyboardAccelerator_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_ka_vtbl;
    impl->IXamlUICommand_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_xaml_cmd_vtbl;
    impl->ICommand_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_icmd_vtbl;
    impl->IMenuFlyoutItem_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_mfi_vtbl;
    impl->IMenuFlyoutItem3_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_mfi3_vtbl;
    impl->IToggleMenuFlyoutItem_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_tmfi_vtbl;
    impl->IMenuFlyoutSubItem_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_mfsi_vtbl;
    impl->IMenuFlyout2_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_mf2_vtbl;
    impl->IUIElement4_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_ui4_vtbl;
    impl->IUIElement5_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_ui5_vtbl;
    impl->IUIElement10_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_uie10_vtbl;
    impl->IXamlRoot_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_xaml_root_vtbl;
    impl->IButtonWithFlyout_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_button_flyout_vtbl;
    impl->IContentDialog_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_cd_vtbl;
    impl->IContentDialog2_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_cd2_vtbl;
    impl->ISwapChainPanel_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_scp_vtbl;
    impl->ISwapChainPanelNative2_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_scp_native_vtbl;
    impl->ITeachingTip_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_teachingtip_vtbl;
    impl->IAutoSuggestBox_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_asb_vtbl;
    impl->IAutoSuggestBox2_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_asb2_vtbl;
    impl->ISplitView_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_splitview_vtbl;
    impl->ISplitView2_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_splitview2_vtbl;
    impl->IEasingFunctionBase_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_ease_vtbl;
    impl->IMediaPlayerElement_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_mpe_vtbl;
    impl->IBitmapImage_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_bi_vtbl;
    impl->IBitmapSource_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_bs_vtbl;
    impl->IImage_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_img_vtbl;
    impl->IShape_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_shape_vtbl;
    impl->IXamlCompositionBrushBaseProtected_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_xcbb_prot_vtbl;
    impl->ILoadedImageSurface_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_lis_vtbl;
    impl->IFontFamily_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_font_family_vtbl;
    impl->IFontIcon_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_font_icon_vtbl;
    impl->IImageBrush_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_image_brush_vtbl;
    impl->ITileBrush_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_tile_brush_vtbl;
    impl->IGradientBrush_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_grad_brush_vtbl;
    impl->ILinearGradientBrush_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_lgb_vtbl;
    impl->IGradientStop_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_grad_stop_vtbl;
    impl->IPathGeometry_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_path_geom_vtbl;
    impl->IPathFigure_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_path_geom_f2_vtbl;
    impl->IRichTextBlock_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_rtb_vtbl;
    impl->ITextBlock2_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_tb2_vtbl;
    impl->ITextBlock6_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_tb6_vtbl;
    impl->IPopup_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_popup_vtbl;
    impl->IPopup2_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_popup2_vtbl;
    impl->IPopup3_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_popup3_vtbl;
    impl->IStoryboard_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_sb_vtbl;
    impl->media_stretch = 2; /* Uniform */
    impl->tile_align_x = 1; /* Center */
    impl->tile_align_y = 1; /* Center */
    impl->grad_end_x = 1.0f;
    impl->grad_end_y = 1.0f;
    impl->grad_mapping_mode = 1; /* RelativeToBoundingBox */
    impl->grad_color_interp = 1; /* SRgbLinearInterpolation */
    impl->IWeakReferenceSource_iface.lpVtbl = &uwp_wrs_vtbl;
    impl->IDispatch_iface.lpVtbl = &uwp_disp_vtbl;
    impl->IReferenceTrackerTarget_iface.lpVtbl = (const IInspectableVtbl *)&uwp_rtt_vtbl;
    impl->ITypeInfo_iface.lpVtbl = &uwp_disp_ti_vtbl;

    impl->ref = 1;
    impl->outer = outer;
    impl->accel_enabled = TRUE;
    lstrcpynW( impl->class_name, class_name ? class_name : L"Windows.UI.Xaml.FrameworkElement", ARRAY_SIZE(impl->class_name) );
    impl->kind = classify_uwp_class( impl->class_name );
    uwp_elem_sync_class_flags( impl );
    if (impl->kind == UWP_KIND_PAGE) g_last_created_page = impl;
    impl->grid_row_span = 1;
    impl->grid_col_span = 1;
    impl->grid_len_type = 2; /* Star */
    impl->grid_len_val = 1.0;
    impl->is_enabled = TRUE;
    impl->is_hit_test_visible = TRUE;
    impl->opacity = 1.0;
    impl->brush_opacity = 1.0;
    impl->h_align = 3; /* Stretch */
    impl->v_align = 3; /* Stretch */
    impl->font_size = 16.0;
    impl->text_align = 1; /* Left */
    impl->text_wrapping = uwp_is_rich_text_block( impl ) ? 2 : 1; /* RichTextBlock defaults to Wrap */
    impl->selected_index = -1;
    impl->hovered_dropdown_idx = -1;
    impl->nav_open_pane_length = 280.0;
    impl->nav_compact_pane_length = 68.0;
    impl->nav_compact_threshold = 641.0;
    impl->nav_expanded_threshold = 1008.0;
    impl->nav_is_settings_visible = TRUE;
    impl->nav_is_pane_toggle_visible = TRUE;
    impl->nav_is_pane_open = FALSE;
    InitializeSRWLock( &impl->video_lock );
    g_layout_dirty = TRUE;
    if (impl->kind != UWP_KIND_BRUSH && impl->kind != UWP_KIND_STYLE &&
        impl->kind != UWP_KIND_RESOURCE_DICT && impl->kind != UWP_KIND_COLOR &&
        impl->kind != UWP_KIND_ROW_DEF && impl->kind != UWP_KIND_COL_DEF &&
        impl->kind != UWP_KIND_DEFERRED && impl->kind != UWP_KIND_ACCELERATOR &&
        impl->kind != UWP_KIND_XAML_COMMAND)
        xaml_create_dcomp_visual( &impl->visual );

    register_uwp_elem( impl );

    if (inner)
    {
        void *ui = NULL;
        HRESULT qi_hr = E_FAIL;
        *inner = &impl->nondelegating_iface;
        /* Delegation mode is a property of the outer object, not the app: an outer
         * that answers IUIElement is a real app-side element, so the element's
         * lifetime rides on it; otherwise the element owns its own refcount. */
        if (outer &&
            !IsBadReadPtr( outer, sizeof(void *) ) &&
            !IsBadReadPtr( *(void **)outer, 2 * sizeof(void *) ))
        {
            __TRY
            {
                qi_hr = IInspectable_QueryInterface( outer, &IID_IUIElement_UWP, &ui );
            }
            __EXCEPT_ALL
            {
                qi_hr = E_FAIL;
            }
            __ENDTRY;
        }
        if (SUCCEEDED( qi_hr ) && ui)
        {
            IInspectable_Release( (IInspectable *)ui );
            impl->delegating = TRUE;
            IInspectable_AddRef( outer );
        }
        else
            InterlockedIncrement( &impl->ref );
    }
    if (impl->kind == UWP_KIND_FRAME)
        *instance = &impl->IFrame_iface;
    else if (impl->kind == UWP_KIND_GRID || impl->kind == UWP_KIND_STACK_PANEL)
        *instance = &impl->IGrid_iface;
    else if (impl->kind == UWP_KIND_BORDER)
        *instance = &impl->IBorder_iface;
    else if (impl->kind == UWP_KIND_BUTTON)
        *instance = &impl->IButtonBase_iface;
    else if (impl->kind == UWP_KIND_TOGGLE_BUTTON)
        *instance = &impl->IToggleButton_iface;
    else if (impl->kind == UWP_KIND_TEXT_BLOCK)
        *instance = &impl->ITextBlock_iface;
    else if (impl->kind == UWP_KIND_ROW_DEF)
        *instance = &impl->IRowDefinition_iface;
    else if (impl->kind == UWP_KIND_COL_DEF)
        *instance = &impl->IColumnDefinition_iface;
    else if (impl->kind == UWP_KIND_CONTENT_CONTROL || impl->kind == UWP_KIND_USER_CONTROL || impl->kind == UWP_KIND_PAGE)
        *instance = &impl->IContentControl_iface;
    else if (impl->kind == UWP_KIND_ACCELERATOR || uwp_is_accelerator( impl ))
        *instance = &impl->IKeyboardAccelerator_iface;
    else if (impl->kind == UWP_KIND_XAML_COMMAND || wcsstr( impl->class_name, L"XamlUICommand" ))
        *instance = &impl->IXamlUICommand_iface;
    else if (wcsstr( impl->class_name, L"FlyoutItem" ))
        *instance = &impl->IMenuFlyoutItem_iface;
    else if (wcsstr( impl->class_name, L"ContentDialog" ))
        *instance = &impl->IContentDialog_iface;
    else if (!wcscmp( impl->class_name, L"Windows.UI.Xaml.DispatcherTimer" ))
        *instance = &impl->IInspectable_iface;
    else if (!wcscmp( impl->class_name, L"Windows.UI.Xaml.Data.Binding" ) ||
             !wcscmp( impl->class_name, L"Windows.UI.Xaml.Data.RelativeSource" ) ||
             !wcscmp( impl->class_name, L"Windows.UI.Xaml.Controls.SymbolIcon" ))
        *instance = &impl->IStyle_iface;
    else
        *instance = &impl->IFrameworkElement_iface;

    return S_OK;
}

/* ============================================================================
 * 5. DataContext Propagation, Loaded & OnApplyTemplate Dispatch, Frame Navigate
 * ============================================================================ */


static HRESULT WINAPI uwp_ccc_QueryInterface( void *iface, REFIID iid, void **out )
{
    static const GUID IID_IContainerContentChangingEventArgs_g =
        { 0x13922481, 0x41f3, 0x4e20, { 0xb6, 0x3a, 0x3f, 0x53, 0x63, 0x2e, 0x10, 0x85 } };
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IContainerContentChangingEventArgs_g ))
    {
        *out = iface;
        InterlockedIncrement( &((struct uwp_ccc_args *)iface)->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI uwp_ccc_AddRef( void *iface )
{
    return InterlockedIncrement( &((struct uwp_ccc_args *)iface)->ref );
}

ULONG WINAPI uwp_ccc_Release( void *iface )
{
    struct uwp_ccc_args *a = (struct uwp_ccc_args *)iface;
    LONG r = InterlockedDecrement( &a->ref );
    if (r == 0)
    {
        if (a->item) IInspectable_Release( a->item );
        if (a->item_container) IInspectable_Release( a->item_container );
        free( a );
    }
    return r;
}

static HRESULT WINAPI uwp_ccc_GetIids( void *iface, ULONG *cnt, IID **iids )
{
    if (cnt) *cnt = 0;
    if (iids) *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI uwp_ccc_GetRuntimeClassName( void *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.UI.Xaml.Controls.ContainerContentChangingEventArgs", 58, name );
}

static HRESULT WINAPI uwp_ccc_GetTrustLevel( void *iface, TrustLevel *tl )
{
    if (tl) *tl = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI uwp_ccc_get_ItemContainer( void *iface, void **out )
{
    struct uwp_ccc_args *a = (struct uwp_ccc_args *)iface;
    if (!out) return E_POINTER;
    *out = a->item_container;
    if (*out) IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}
static HRESULT WINAPI uwp_ccc_get_InRecycleQueue( void *iface, boolean *out ) { if (out) *out = ((struct uwp_ccc_args *)iface)->in_recycle; return S_OK; }
static HRESULT WINAPI uwp_ccc_get_ItemIndex( void *iface, INT32 *out ) { if (out) *out = 0; return S_OK; }
static HRESULT WINAPI uwp_ccc_get_Item( void *iface, IInspectable **out )
{
    struct uwp_ccc_args *a = (struct uwp_ccc_args *)iface;
    if (!out) return E_POINTER;
    *out = a->item;
    if (*out) IInspectable_AddRef( *out );
    return S_OK;
}
static HRESULT WINAPI uwp_ccc_get_Phase( void *iface, UINT32 *out ) { if (out) *out = ((struct uwp_ccc_args *)iface)->phase; return S_OK; }
static HRESULT WINAPI uwp_ccc_put_Handled( void *iface, boolean v ) { ((struct uwp_ccc_args *)iface)->handled = v; return S_OK; }
static HRESULT WINAPI uwp_ccc_get_Handled( void *iface, boolean *out ) { if (out) *out = ((struct uwp_ccc_args *)iface)->handled; return S_OK; }
static HRESULT WINAPI uwp_ccc_RegisterUpdateCallback( void *iface, void *cb ) { return S_OK; }
static HRESULT WINAPI uwp_ccc_RegisterUpdateCallbackWithPhase( void *iface, UINT32 p, void *cb ) { return S_OK; }

const void *uwp_ccc_vtbl[] =
{
    uwp_ccc_QueryInterface,
    uwp_ccc_AddRef,
    uwp_ccc_Release,
    uwp_ccc_GetIids,
    uwp_ccc_GetRuntimeClassName,
    uwp_ccc_GetTrustLevel,
    uwp_ccc_get_ItemContainer,
    uwp_ccc_get_InRecycleQueue,
    uwp_ccc_get_ItemIndex,
    uwp_ccc_get_Item,
    uwp_ccc_get_Phase,
    uwp_ccc_put_Handled,
    uwp_ccc_get_Handled,
    uwp_ccc_RegisterUpdateCallback,
    uwp_ccc_RegisterUpdateCallbackWithPhase,
};


static struct uwp_dp_changed_args g_dp_routed_args = { (const void *)dp_changed_args_vtbl, 0x100000, NULL, NULL, NULL };
/* Loaded/Click/Navigation callbacks receive this: McgMarshal ComInterfaceToObject
 * calls GetRuntimeClassName on it, so it must be an element face naming
 * RoutedEventArgs. */
struct uwp_element *g_routed_args_el = NULL;
/* McgMarshal caches the RCW (__ComObject) per IUnknown pointer and pins its
 * interop type at the first crossing.  Handlers registered on one element
 * often use distinct delegate args types (RoutedEventArgs vs RoutedEventArgs2),
 * so each fire allocates its own args object to keep cached RCW types matching. */
void *g_routed_args(void)
{
    void *inst = NULL;
    struct uwp_element *el;
    uwp_element_create( L"Windows.UI.Xaml.RoutedEventArgs", NULL, NULL, &inst );
    el = uwp_elem_from_any( inst );
    if (el && !g_routed_args_el) g_routed_args_el = el;
    if (el && el->outer) return el->outer;
    if (el) return &el->IInspectable_iface;
    return &g_dp_routed_args;
}
#define g_dummy_routed_args g_routed_args()


static const GUID IID_ISizeChangedEventArgs_UWP =
    { 0xd5312e60, 0x5cc1, 0x42a1, { 0x92, 0x0c, 0x1a, 0xf4, 0x6b, 0xe2, 0xf9, 0x86 } };

static HRESULT WINAPI uwp_sc_args_QI( void *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IRoutedEventArgs_QI_UWP ) ||
        IsEqualGUID( iid, &IID_ISizeChangedEventArgs_UWP ))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI uwp_sc_args_AddRef( void *iface ) { return 2; }
static ULONG WINAPI uwp_sc_args_Release( void *iface ) { return 1; }
static HRESULT WINAPI uwp_sc_args_GetIids( void *iface, ULONG *c, IID **i ) { if (c) *c = 0; if (i) *i = NULL; return S_OK; }
static HRESULT WINAPI uwp_sc_args_GetRuntimeClassName( void *iface, HSTRING *n )
{
    static const WCHAR cls[] = L"Windows.UI.Xaml.SizeChangedEventArgs";
    return WindowsCreateString( cls, ARRAY_SIZE(cls) - 1, n );
}
static HRESULT WINAPI uwp_sc_args_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI uwp_sc_args_get_PreviousSize( void *iface, Size *sz )
{
    if (sz) { sz->Width = 0.0f; sz->Height = 0.0f; }
    return S_OK;
}
float g_sc_new_w = 1272.0f, g_sc_new_h = 990.0f;
static HRESULT WINAPI uwp_sc_args_get_NewSize( void *iface, Size *sz )
{
    if (sz) { sz->Width = g_sc_new_w > 0.0f ? g_sc_new_w : 1272.0f; sz->Height = g_sc_new_h > 0.0f ? g_sc_new_h : 990.0f; }
    return S_OK;
}
static const void *const uwp_sc_args_vtbl[] =
{
    uwp_sc_args_QI, uwp_sc_args_AddRef, uwp_sc_args_Release,
    uwp_sc_args_GetIids, uwp_sc_args_GetRuntimeClassName, uwp_sc_args_GetTrustLevel,
    uwp_sc_args_get_PreviousSize,
    uwp_sc_args_get_NewSize,
};
struct uwp_sc_args_obj uwp_sc_args_obj = { uwp_sc_args_vtbl };




HRESULT uwp_frame_navigate_impl( void *iface, void *type_name_ptr, void *param, void *transition_info, boolean *result )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    TypeName *type = (TypeName *)type_name_ptr;
    const WCHAR *name = (type && type->Name) ? WindowsGetStringRawBuffer( type->Name, NULL ) : NULL;

    WARN( "uwp_frame_navigate_impl iface=%p (%s) type=%s param=%p trans=%p\n",
          iface, impl ? debugstr_w(impl->name) : "\"?\"", debugstr_w(name), param, transition_info );
    if (result) *result = TRUE;
    if (!impl || !name) return S_OK;

    {
        IInspectable *page = NULL;
        HRESULT hr = xaml_create_page( type_name_ptr, &page );
        TRACE( "xaml_create_page(%s) -> hr %#lx, page %p\n", debugstr_w(name), hr, page );
        if (SUCCEEDED(hr) && page)
        {
            struct uwp_element *page_el = uwp_elem_from_any( page );
            TRACE( "NAVIGATE probe: page=%p g_last_created_page=%p kind=%d\n",
                   page, g_last_created_page, g_last_created_page ? g_last_created_page->kind : -1 );
            if (!page_el && g_last_created_page)
            {
                page_el = g_last_created_page;
                TRACE( "uwp_frame_navigate_impl: RCW page unresolvable, using last PAGE element %p\n", page_el );
            }
            TRACE( "uwp_frame_navigate_impl page_el=%p outer=%p\n", page_el, page_el ? page_el->outer : NULL );
            if (page_el)
            {
                const WCHAR *comma = wcschr( name, ',' );
                size_t len = comma ? (size_t)(comma - name) : wcslen( name );
                IInspectable *new_content;
                if (len && len < ARRAY_SIZE( page_el->class_name ))
                {
                    lstrcpynW( page_el->class_name, name, (int)len + 1 );
                    page_el->class_name[len] = 0;
                    uwp_elem_sync_class_flags( page_el );
                }
                new_content = page_el->outer ? page_el->outer : &page_el->IInspectable_iface;
                IInspectable_AddRef( new_content );
                if (impl->content && impl->nav_stack_enabled)
                {
                    if (!impl->back_stack)
                    {
                        uwp_vec_create( NULL, &impl->back_stack );
                        if (impl->back_stack)
                            CONTAINING_RECORD( impl->back_stack, struct uwp_vec_view, IInspectable_iface )->runtime_class =
                                L"Windows.Foundation.Collections.IVector`1<Windows.UI.Xaml.Navigation.PageStackEntry>";
                    }
                    if (impl->back_stack && impl->nav_args_item)
                    {
                        struct uwp_nav_event_args *prev_nea =
                            CONTAINING_RECORD( impl->nav_args_item, struct uwp_nav_event_args, INavigationEventArgs_iface );
                        struct uwp_nav_event_args *pse = uwp_page_stack_entry_create(
                            prev_nea->content, prev_nea->parameter, &prev_nea->source_page_type, prev_nea->transition_info );
                        if (pse)
                        {
                            uwp_vec_Append( impl->back_stack, &pse->INavigationEventArgs_iface );
                            uwp_nea_Release( &pse->INavigationEventArgs_iface );
                        }
                    }
                }
                if (impl->content) IInspectable_Release( impl->content );
                impl->content = new_content;
                if ((!impl->parent && !wcsstr( name, L"Skeleton" )) || !g_navigated_page_el)
                    g_navigated_page_el = page_el;
                page_el->parent = impl;
                page_el->scroll_y = 0;

                {
                    struct uwp_nav_event_args *nea = uwp_nav_event_args_create(
                        new_content, (IInspectable *)param, type, (IInspectable *)transition_info );
                    if (impl->nav_args_item) uwp_nea_Release( impl->nav_args_item );
                    impl->nav_args_item = nea ? &nea->INavigationEventArgs_iface : NULL;
                    if (impl->nav_args_item) IInspectable_AddRef( impl->nav_args_item );

                    /* Invoke IPageOverrides::OnNavigatedTo (slot 7) if implemented */
                    if (page_el->outer)
                    {
                        void *page_ovr = NULL;
                        HRESULT qi_hr = IInspectable_QueryInterface( page_el->outer, &IID_IPageOverrides_UWP, &page_ovr );
                        TRACE( "uwp_frame_navigate_impl QI IPageOverrides -> hr %#lx ptr %p\n", qi_hr, page_ovr );
                        if (SUCCEEDED(qi_hr) && page_ovr)
                        {
                            HRESULT (WINAPI *on_nav)(void *, void *) = (void *)(*(void ***)page_ovr)[7];
                            HRESULT nav_hr = S_OK;
                            __TRY
                            {
                                nav_hr = on_nav ? on_nav( page_ovr, nea ? (void *)&nea->INavigationEventArgs_iface : g_routed_args() ) : S_OK;
                            }
                            __EXCEPT_ALL { nav_hr = E_FAIL; }
                            __ENDTRY;
                            TRACE( "uwp_frame_navigate_impl OnNavigatedTo -> hr %#lx\n", nav_hr );
                            IUnknown_Release( (IUnknown *)page_ovr );
                        }
                    }

                    /* Fire Frame::Navigated handlers */
                    if (impl->frame_navigated_reg.next_token > 0)
                    {
                        void **handlers = NULL;
                        unsigned int cnt = xaml_event_registry_snapshot_alloc( &impl->frame_navigated_reg, &handlers );
                        IInspectable *sender = impl->outer ? impl->outer : &impl->IInspectable_iface;
                        unsigned int hi;
                        for (hi = 0; hi < cnt; hi++)
                        {
                            struct xaml_event_handler *h = (struct xaml_event_handler *)handlers[hi];
                            TRACE( "uwp_frame_navigate_impl firing Navigated[%u] on %s (%s)\n",
                                   hi, debugstr_w(impl->class_name), debugstr_w(impl->name) );
                            __TRY
                            {
                                h->lpVtbl->Invoke( h, sender, nea ? &nea->INavigationEventArgs_iface : g_routed_args() );
                            }
                            __EXCEPT_ALL { WARN( "Frame::Navigated[%u] threw\n", hi ); }
                            __ENDTRY;
                            IUnknown_Release( (IUnknown *)handlers[hi] );
                        }
                        free( handlers );
                    }
                    if (nea) uwp_nea_Release( &nea->INavigationEventArgs_iface );
                }
                uwp_fire_loaded_tree( page_el );

                /* Populate any empty placeholder containers from the page XBF's referenced PRI resources */
                uwp_populate_page_from_xbf_refs( page_el );
            }
            else
            {
                if (impl->content) IInspectable_Release( impl->content );
                impl->content = page;
            }
            xaml_window_present_current();
        }
    }
    return S_OK;
}

static HRESULT WINAPI uwp_frame_NavigateWithParam( void *iface, void *type_name_ptr, void *param, boolean *result )
{
    return uwp_frame_navigate_impl( iface, type_name_ptr, param, NULL, result );
}

/* ============================================================================
 * 6. XBF v2 Stack Parser & Visual Tree Builder
 * ============================================================================ */


static const GUID IID_ITappedRoutedEventArgs_UWP =
    { 0xafa4bd0f, 0xa05b, 0x4665, { 0x8d, 0x2c, 0xa6, 0xf6, 0x73, 0x67, 0x23, 0x8a } };


static inline struct uwp_tapped_args *impl_from_tap( void *iface )
{
    return CONTAINING_RECORD( iface, struct uwp_tapped_args, ITappedRoutedEventArgs_iface );
}
static inline struct uwp_tapped_args *impl_from_tap_routed( void *iface )
{
    return CONTAINING_RECORD( iface, struct uwp_tapped_args, IRoutedEventArgs_iface );
}
static HRESULT WINAPI uwp_tap_QI( void *iface, REFIID iid, void **out )
{
    struct uwp_tapped_args *a = impl_from_tap( iface );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_ITappedRoutedEventArgs_UWP ))
    {
        InterlockedIncrement( &a->ref );
        *out = &a->ITappedRoutedEventArgs_iface;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IRoutedEventArgs_QI_UWP ))
    {
        InterlockedIncrement( &a->ref );
        *out = &a->IRoutedEventArgs_iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI uwp_tap_AddRef( void *iface ) { return InterlockedIncrement( &impl_from_tap( iface )->ref ); }
ULONG WINAPI uwp_tap_Release( void *iface )
{
    struct uwp_tapped_args *a = impl_from_tap( iface );
    ULONG r = InterlockedDecrement( &a->ref );
    if (!r)
    {
        if (a->orig_source) IInspectable_Release( a->orig_source );
        free( a );
    }
    return r;
}
static HRESULT WINAPI uwp_tap_GetIids( void *iface, ULONG *cnt, IID **iids ) { *cnt = 0; *iids = NULL; return S_OK; }
static HRESULT WINAPI uwp_tap_GetRuntimeClassName( void *iface, HSTRING *n )
{
    static const WCHAR cls[] = L"Windows.UI.Xaml.Input.TappedRoutedEventArgs";
    return WindowsCreateString( cls, ARRAY_SIZE(cls) - 1, n );
}
static HRESULT WINAPI uwp_tap_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI uwp_tap_get_PointerDeviceType( void *iface, INT32 *v ) { if (v) *v = 2; return S_OK; } /* Mouse */
static HRESULT WINAPI uwp_tap_get_Handled( void *iface, boolean *v ) { if (v) *v = impl_from_tap( iface )->handled; return S_OK; }
static HRESULT WINAPI uwp_tap_put_Handled( void *iface, boolean v ) { impl_from_tap( iface )->handled = v; return S_OK; }
static HRESULT WINAPI uwp_tap_GetPosition( void *iface, void *rel, Point *pt )
{
    struct uwp_tapped_args *a = impl_from_tap( iface );
    if (pt) { pt->X = a->x; pt->Y = a->y; }
    return S_OK;
}
static HRESULT WINAPI uwp_tap_routed_QI( void *iface, REFIID iid, void **out ) { return uwp_tap_QI( &impl_from_tap_routed(iface)->ITappedRoutedEventArgs_iface, iid, out ); }
static ULONG WINAPI uwp_tap_routed_AddRef( void *iface ) { return uwp_tap_AddRef( &impl_from_tap_routed(iface)->ITappedRoutedEventArgs_iface ); }
static ULONG WINAPI uwp_tap_routed_Release( void *iface ) { return uwp_tap_Release( &impl_from_tap_routed(iface)->ITappedRoutedEventArgs_iface ); }
static HRESULT WINAPI uwp_tap_routed_GetIids( void *iface, ULONG *c, IID **i ) { return uwp_tap_GetIids( &impl_from_tap_routed(iface)->ITappedRoutedEventArgs_iface, c, i ); }
static HRESULT WINAPI uwp_tap_routed_GetRuntimeClassName( void *iface, HSTRING *n ) { return uwp_tap_GetRuntimeClassName( &impl_from_tap_routed(iface)->ITappedRoutedEventArgs_iface, n ); }
static HRESULT WINAPI uwp_tap_routed_GetTrustLevel( void *iface, TrustLevel *l ) { return uwp_tap_GetTrustLevel( &impl_from_tap_routed(iface)->ITappedRoutedEventArgs_iface, l ); }
static HRESULT WINAPI uwp_tap_routed_get_OriginalSource( void *iface, IInspectable **out )
{
    struct uwp_tapped_args *a = impl_from_tap_routed( iface );
    if (!out) return E_POINTER;
    *out = a->orig_source;
    if (*out) IInspectable_AddRef( *out );
    return S_OK;
}
const void *uwp_tap_vtbl[] =
{
    uwp_tap_QI, uwp_tap_AddRef, uwp_tap_Release,
    uwp_tap_GetIids, uwp_tap_GetRuntimeClassName, uwp_tap_GetTrustLevel,
    uwp_tap_get_PointerDeviceType, uwp_tap_get_Handled, uwp_tap_put_Handled, uwp_tap_GetPosition,
};
const void *uwp_tap_routed_vtbl[] =
{
    uwp_tap_routed_QI, uwp_tap_routed_AddRef, uwp_tap_routed_Release,
    uwp_tap_routed_GetIids, uwp_tap_routed_GetRuntimeClassName, uwp_tap_routed_GetTrustLevel,
    uwp_tap_routed_get_OriginalSource,
};


/* ============================================================================
 * 7b. Generic Pointer Hit-Testing, Capture, Bubbling & PointerRoutedEventArgs
 * ============================================================================ */

static const GUID IID_IPointerRoutedEventArgs_UWP =
    { 0xda628f0a, 0x9752, 0x49e2, { 0xbd, 0xe2, 0x49, 0xec, 0xca, 0xb9, 0x19, 0x4d } };
static const GUID IID_IPointer_UWP =
    { 0x5ee8f39f, 0x747d, 0x4171, { 0x90, 0xe6, 0xcd, 0x37, 0xa9, 0xdf, 0xfb, 0x11 } };

struct uwp_pointer
{
    const void **lpVtbl;
    LONG ref;
    UINT32 pointer_id;
    BOOL is_in_contact;
    BOOL is_in_range;
};

static HRESULT WINAPI uwp_ptr_QI( void *iface, REFIID iid, void **out )
{
    struct uwp_pointer *p = iface;
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IPointer_UWP ))
    {
        InterlockedIncrement( &p->ref );
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI uwp_ptr_AddRef( void *iface )
{
    return InterlockedIncrement( &((struct uwp_pointer *)iface)->ref );
}
static ULONG WINAPI uwp_ptr_Release( void *iface )
{
    struct uwp_pointer *p = iface;
    ULONG ref = InterlockedDecrement( &p->ref );
    if (!ref) free( p );
    return ref;
}
static HRESULT WINAPI uwp_ptr_GetRuntimeClassName( void *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.UI.Xaml.Input.Pointer", 29, name );
}
static HRESULT WINAPI uwp_ptr_get_PointerId( void *iface, UINT32 *val )
{
    if (!val) return E_POINTER;
    *val = ((struct uwp_pointer *)iface)->pointer_id;
    return S_OK;
}
static HRESULT WINAPI uwp_ptr_get_PointerDeviceType( void *iface, PointerDeviceType *val )
{
    if (!val) return E_POINTER;
    *val = PointerDeviceType_Mouse;
    return S_OK;
}
static HRESULT WINAPI uwp_ptr_get_IsInContact( void *iface, boolean *val )
{
    if (!val) return E_POINTER;
    *val = ((struct uwp_pointer *)iface)->is_in_contact ? TRUE : FALSE;
    return S_OK;
}
static HRESULT WINAPI uwp_ptr_get_IsInRange( void *iface, boolean *val )
{
    if (!val) return E_POINTER;
    *val = ((struct uwp_pointer *)iface)->is_in_range ? TRUE : FALSE;
    return S_OK;
}
static const void *uwp_ptr_vtbl[] =
{
    uwp_ptr_QI,
    uwp_ptr_AddRef,
    uwp_ptr_Release,
    uwp_sub_GetIids,
    uwp_ptr_GetRuntimeClassName,
    uwp_sub_GetTrustLevel,
    uwp_ptr_get_PointerId,
    uwp_ptr_get_PointerDeviceType,
    uwp_ptr_get_IsInContact,
    uwp_ptr_get_IsInRange,
};


static inline struct uwp_pointer_routed_args *pra_from_routed( void *iface )
{
    return CONTAINING_RECORD( iface, struct uwp_pointer_routed_args, IRoutedEventArgs_vtbl );
}

static HRESULT WINAPI pra_QI( void *iface, REFIID iid, void **out )
{
    struct uwp_pointer_routed_args *a = iface;
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IPointerRoutedEventArgs_UWP ))
    {
        InterlockedIncrement( &a->ref );
        *out = &a->IPointerRoutedEventArgs_vtbl;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IRoutedEventArgs_QI_UWP ))
    {
        InterlockedIncrement( &a->ref );
        *out = &a->IRoutedEventArgs_vtbl;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI pra_AddRef( void *iface )
{
    return InterlockedIncrement( &((struct uwp_pointer_routed_args *)iface)->ref );
}
ULONG WINAPI pra_Release( void *iface )
{
    struct uwp_pointer_routed_args *a = iface;
    ULONG ref = InterlockedDecrement( &a->ref );
    if (!ref)
    {
        if (a->orig_source) IInspectable_Release( a->orig_source );
        if (a->pointer) uwp_ptr_Release( a->pointer );
        free( a );
    }
    return ref;
}
static HRESULT WINAPI pra_GetRuntimeClassName( void *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.UI.Xaml.Input.PointerRoutedEventArgs", 44, name );
}
static HRESULT WINAPI pra_get_Pointer( void *iface, void **value )
{
    struct uwp_pointer_routed_args *a = iface;
    if (!value) return E_POINTER;
    if (!a->pointer)
    {
        a->pointer = calloc( 1, sizeof(*a->pointer) );
        if (a->pointer)
        {
            a->pointer->lpVtbl = uwp_ptr_vtbl;
            a->pointer->ref = 1;
            a->pointer->pointer_id = a->info.pointer_id ? a->info.pointer_id : 1;
            a->pointer->is_in_contact = a->info.is_in_contact;
            a->pointer->is_in_range = a->info.is_in_window;
        }
    }
    *value = a->pointer;
    if (*value) uwp_ptr_AddRef( *value );
    return S_OK;
}
static HRESULT WINAPI pra_get_KeyModifiers( void *iface, UINT32 *value )
{
    struct uwp_pointer_routed_args *a = iface;
    if (!value) return E_POINTER;
    *value = a->info.key_modifiers;
    return S_OK;
}
static HRESULT WINAPI pra_get_Handled( void *iface, boolean *value )
{
    struct uwp_pointer_routed_args *a = iface;
    if (!value) return E_POINTER;
    *value = a->handled;
    return S_OK;
}
static HRESULT WINAPI pra_put_Handled( void *iface, boolean value )
{
    struct uwp_pointer_routed_args *a = iface;
    a->handled = value;
    return S_OK;
}
static HRESULT WINAPI pra_GetCurrentPoint( void *iface, void *relative_to, IPointerPoint **value )
{
    struct uwp_pointer_routed_args *a = iface;
    float rx = a->info.x, ry = a->info.y;
    struct uwp_element *rel;

    if (!value) return E_POINTER;
    if (relative_to && (rel = uwp_elem_from_any( relative_to )) != NULL && rel->has_bounds)
    {
        rx -= (float)rel->bounds.left;
        ry -= (float)rel->bounds.top;
    }
    *value = xaml_pointer_point_create( &a->info, rx, ry );
    return *value ? S_OK : E_OUTOFMEMORY;
}
static HRESULT WINAPI pra_GetIntermediatePoints( void *iface, void *relative_to, void **value )
{
    if (!value) return E_POINTER;
    *value = NULL;
    return S_OK;
}

static const void *pra_vtbl[] =
{
    pra_QI,
    pra_AddRef,
    pra_Release,
    uwp_sub_GetIids,
    pra_GetRuntimeClassName,
    uwp_sub_GetTrustLevel,
    pra_get_Pointer,
    pra_get_KeyModifiers,
    pra_get_Handled,
    pra_put_Handled,
    pra_GetCurrentPoint,
    pra_GetIntermediatePoints,
};

static HRESULT WINAPI pra_routed_QI( void *iface, REFIID iid, void **out )
{
    return pra_QI( pra_from_routed( iface ), iid, out );
}
static ULONG WINAPI pra_routed_AddRef( void *iface )
{
    return pra_AddRef( pra_from_routed( iface ) );
}
static ULONG WINAPI pra_routed_Release( void *iface )
{
    return pra_Release( pra_from_routed( iface ) );
}
static HRESULT WINAPI pra_routed_get_OriginalSource( void *iface, IInspectable **value )
{
    struct uwp_pointer_routed_args *a = pra_from_routed( iface );
    if (!value) return E_POINTER;
    *value = a->orig_source;
    if (*value) IInspectable_AddRef( *value );
    return S_OK;
}
static const void *pra_routed_vtbl[] =
{
    pra_routed_QI,
    pra_routed_AddRef,
    pra_routed_Release,
    uwp_sub_GetIids,
    pra_GetRuntimeClassName,
    uwp_sub_GetTrustLevel,
    pra_routed_get_OriginalSource,
};

struct uwp_pointer_routed_args *uwp_pointer_routed_args_create( const struct xaml_pointer_info *info,
                                                                      IInspectable *orig_source )
{
    struct uwp_pointer_routed_args *a = calloc( 1, sizeof(*a) );
    if (!a) return NULL;
    a->IPointerRoutedEventArgs_vtbl = pra_vtbl;
    a->IRoutedEventArgs_vtbl = pra_routed_vtbl;
    a->ref = 1;
    if (info) a->info = *info;
    a->orig_source = orig_source;
    if (orig_source) IInspectable_AddRef( orig_source );
    return a;
}


struct xaml_event_registry *uwp_elem_ptr_registry( struct uwp_element *el, enum xaml_pointer_event_kind kind )
{
    if (!el) return NULL;
    switch (kind)
    {
    case XAML_PTR_PRESSED:      return &el->ptr_pressed_reg;
    case XAML_PTR_MOVED:        return &el->ptr_moved_reg;
    case XAML_PTR_RELEASED:     return &el->ptr_released_reg;
    case XAML_PTR_WHEEL:        return &el->ptr_wheel_reg;
    case XAML_PTR_ENTERED:      return &el->ptr_entered_reg;
    case XAML_PTR_EXITED:       return &el->ptr_exited_reg;
    case XAML_PTR_CAPTURE_LOST: return &el->ptr_capture_lost_reg;
    }
    return NULL;
}



BOOL uwp_route_pointer_event( HWND hwnd, enum xaml_pointer_event_kind kind, const struct xaml_pointer_info *info,
                              int width, int height, IInspectable *root_content )
{
    static struct uwp_element *last_root = NULL;
    static int last_w = 0, last_h = 0;
    struct uwp_element *root_el = uwp_elem_from_any( root_content );
    struct uwp_element *target = NULL;
    BOOL fired = FALSE;

    if (!info) return FALSE;
    if (!root_el && g_navigated_page_el) root_el = g_navigated_page_el;
    if (!root_el) return FALSE;
    if (root_el->kind == UWP_KIND_FRAME && !uwp_elem_from_any( root_el->content ) && g_navigated_page_el)
        root_el = g_navigated_page_el;

    if (width > 0 && height > 0 &&
        (!root_el->has_bounds || g_layout_dirty || root_el != last_root || width != last_w || height != last_h))
    {
        RECT rc = { 0, 0, width, height };
        uwp_arrange_element( root_el, rc );
        g_layout_dirty = FALSE;
        last_root = root_el;
        last_w = width;
        last_h = height;
    }

    if (kind == XAML_PTR_EXITED)
    {
        if (g_hovered_elem)
        {
            g_hovered_elem->is_hovered = FALSE;
            uwp_fire_element_pointer( g_hovered_elem, XAML_PTR_EXITED, info, FALSE );
            g_hovered_elem = NULL;
        }
        return TRUE;
    }

    if (kind == XAML_PTR_CAPTURE_LOST)
    {
        if (g_captured_elem)
        {
            struct uwp_element *cap = g_captured_elem;
            g_captured_elem = NULL;
            uwp_fire_element_pointer( cap, XAML_PTR_CAPTURE_LOST, info, FALSE );
        }
        return TRUE;
    }

    target = g_captured_elem ? g_captured_elem : uwp_hit_test_element( root_el, (int)info->x, (int)info->y );

    if (!g_captured_elem && target != g_hovered_elem)
    {
        if (g_hovered_elem)
        {
            g_hovered_elem->is_hovered = FALSE;
            uwp_fire_element_pointer( g_hovered_elem, XAML_PTR_EXITED, info, FALSE );
        }
        g_hovered_elem = target;
        if (target)
        {
            target->is_hovered = TRUE;
            uwp_fire_element_pointer( target, XAML_PTR_ENTERED, info, FALSE );
        }
    }

    if (!target) return FALSE;

    if (kind == XAML_PTR_PRESSED)
    {
        g_captured_elem = target;
        TRACE( "pointer: pressed routed to element %p (%s / %s) at (%d,%d)\n",
               target, debugstr_w(target->class_name), debugstr_w(target->name), (int)info->x, (int)info->y );
        fired = uwp_fire_element_pointer( target, XAML_PTR_PRESSED, info, TRUE );
    }
    else if (kind == XAML_PTR_MOVED)
    {
        fired = uwp_fire_element_pointer( target, XAML_PTR_MOVED, info, TRUE );
    }
    else if (kind == XAML_PTR_RELEASED)
    {
        struct uwp_element *nvi = NULL, *cur;
        for (cur = target; cur; cur = cur->parent)
        {
            if (cur->parent &&
                (!wcscmp( cur->parent->name, L"MenuItemsHost" ) || !wcscmp( cur->parent->name, L"FooterMenuItemsHost" )) &&
                !uwp_is_nav_header( cur ) && !uwp_is_nav_separator( cur ))
            {
                nvi = cur;
                break;
            }
        }
        fired = uwp_fire_element_pointer( target, XAML_PTR_RELEASED, info, TRUE );
        if (uwp_fire_tapped_bubble( target, (int)info->x, (int)info->y ))
            fired = TRUE;
        if (nvi)
        {
            uwp_nav_select_item( NULL, nvi );
            fired = TRUE;
        }
        g_captured_elem = NULL;
    }
    else if (kind == XAML_PTR_WHEEL)
    {
        fired = uwp_fire_element_pointer( target, XAML_PTR_WHEEL, info, TRUE );
    }

    return fired || (kind == XAML_PTR_PRESSED);
}

