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
#include <excpt.h>
#include "wine/exception.h"
#include "private.h"
#include "uwp_private.h"
#include "controls_nav_private.h"
WINE_DEFAULT_DEBUG_CHANNEL(xaml);

ULONG WINAPI uwp_nav_args_AddRef( void *iface ) { return 2; }
static ULONG WINAPI uwp_nav_args_Release( void *iface ) { return 1; }
static HRESULT WINAPI uwp_nav_args_get_IsSelected( void *iface, boolean *val )
{
    if (val) *val = TRUE;
    return S_OK;
}
static HRESULT WINAPI uwp_nav_args_get_IsSettingsSelected( void *iface, boolean *val )
{
    if (val) *val = FALSE;
    return S_OK;
}
static HRESULT WINAPI uwp_nav_args_get_Item( void *iface, IInspectable **val )
{
    if (val)
    {
        *val = g_nav_args_item;
        if (*val) IInspectable_AddRef( *val );
    }
    return S_OK;
}
static const void *uwp_nav_args_vtbl[16] =
{
    [0] = uwp_nav_args_QI,
    [1] = uwp_nav_args_AddRef,
    [2] = uwp_nav_args_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_nav_args_get_Item,
    [7] = uwp_nav_args_get_IsSelected,
    [8] = uwp_nav_args_get_IsSettingsSelected,
    [9] = uwp_nav_args_get_Item,
    [10 ... 15] = uwp_generic_noop,
};
static struct { const void *vtbl; } g_nav_sel_args = { uwp_nav_args_vtbl };

struct uwp_element *uwp_nav_find_item( struct uwp_element *el, int depth );

void uwp_nav_fire( struct uwp_element *el, struct xaml_event_registry *reg,
                          IInspectable *item )
{
    void **handlers = NULL;
    unsigned int n, i;
    IInspectable *sender = el->outer ? el->outer : &el->IInspectable_iface;

    n = xaml_event_registry_snapshot_alloc( reg, &handlers );
    if (!n) return;
    g_nav_args_item = item;
    for (i = 0; i < n; i++)
    {
        struct xaml_event_handler *h = (struct xaml_event_handler *)handlers[i];
        TRACE( "Firing NavigationView event on %s (%s) handler=%p item=%p\n",
               debugstr_w( el->class_name ), debugstr_w( el->name ), handlers[i], item );
        __TRY
        {
            h->lpVtbl->Invoke( h, sender, (IInspectable *)&g_nav_sel_args );
        }
        __EXCEPT_ALL
        {
            WARN( "NavigationView handler on %s (%s) threw\n",
                  debugstr_w( el->class_name ), debugstr_w( el->name ) );
        }
        __ENDTRY;
    }
    free( handlers );
    g_nav_args_item = NULL;
}

struct uwp_element *g_open_combobox = NULL;

void uwp_nav_track_open_state( struct uwp_element *nav )
{
    if (!nav) return;
    if (nav->nav_is_pane_open && uwp_is_nav_view( nav ))
        g_open_overlay_nav = nav;
    else if (g_open_overlay_nav == nav)
        g_open_overlay_nav = NULL;
}

void uwp_nav_set_pane_open( struct uwp_element *nav, BOOL open )
{
    void *is_open_dp = NULL;
    int i;
    if (!nav) return;
    if (nav->nav_is_pane_open == open) return;
    nav->nav_is_pane_open = open;
    uwp_nav_track_open_state( nav );
    if (!open) nav->nav_scroll_y = 0;
    TRACE( "uwp_nav_set_pane_open(%s) -> %d\n", debugstr_w(nav->name), (int)open );

    /* Sync IsPaneOpen DP if registered on this NavigationView so WinUI stays aligned */
    AcquireSRWLockShared( &nav->dp_lock );
    for (i = 0; i < nav->dp_cnt; i++)
    {
        struct { IDependencyProperty iface; LONG ref; void *meta; WCHAR name[64]; } *dpl = (void *)nav->dp_store[i].dp;
        if (dpl && (ULONG_PTR)dpl >= 0x10000 && !wcscmp( dpl->name, L"IsPaneOpen" ))
        {
            is_open_dp = nav->dp_store[i].dp;
            IUnknown_AddRef( (IUnknown *)is_open_dp );
            break;
        }
    }
    ReleaseSRWLockShared( &nav->dp_lock );
    if (is_open_dp)
    {
        IInspectable *boxed = box_bool_value( open ? TRUE : FALSE );
        if (boxed)
        {
            uwp_do_SetValue( &nav->IDependencyObject_iface, is_open_dp, boxed );
            IInspectable_Release( boxed );
        }
        IUnknown_Release( (IUnknown *)is_open_dp );
    }
    g_layout_dirty = TRUE;
    uwp_invalidate_window();
}

HRESULT WINAPI uwp_nav_get_IsPaneOpen( void *iface, boolean *open )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (open) *open = impl ? (boolean)impl->nav_is_pane_open : FALSE;
    return S_OK;
}
HRESULT WINAPI uwp_nav_put_IsPaneOpen( void *iface, boolean open )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl) uwp_nav_set_pane_open( impl, open != 0 );
    return S_OK;
}
HRESULT WINAPI uwp_nav_get_FooterMenuItems( void *iface, void **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl || !out) return E_INVALIDARG;
    if (!impl->children_coll) uwp_vec_create( impl, &impl->children_coll );
    *out = impl->children_coll;
    if (*out) IInspectable_AddRef( *out );
    return S_OK;
}

static HRESULT WINAPI uwp_nav_probe6( void *iface, ... ) { TRACE( "navview-vtbl slot 6 hit\n" ); return S_OK; }
static HRESULT WINAPI uwp_nav_probe11( void *iface, ... ) { TRACE( "navview-vtbl slot 11 hit\n" ); return S_OK; }
static HRESULT WINAPI uwp_nav_probe12( void *iface, ... ) { TRACE( "navview-vtbl slot 12 hit\n" ); return S_OK; }
static HRESULT WINAPI uwp_nav_probe19( void *iface, ... ) { TRACE( "navview-vtbl slot 19 hit\n" ); return S_OK; }
static HRESULT WINAPI uwp_nav_probe20( void *iface, ... ) { TRACE( "navview-vtbl slot 20 hit\n" ); return S_OK; }
static HRESULT WINAPI uwp_nav_probe21( void *iface, ... ) { TRACE( "navview-vtbl slot 21 hit\n" ); return S_OK; }
static HRESULT WINAPI uwp_nav_probe22( void *iface, ... ) { TRACE( "navview-vtbl slot 22 hit\n" ); return S_OK; }
static HRESULT WINAPI uwp_nav_probe23( void *iface, ... ) { TRACE( "navview-vtbl slot 23 hit\n" ); return S_OK; }
static HRESULT WINAPI uwp_nav_probe24( void *iface, ... ) { TRACE( "navview-vtbl slot 24 hit\n" ); return S_OK; }
static HRESULT WINAPI uwp_nav_probe25( void *iface, ... ) { TRACE( "navview-vtbl slot 25 hit\n" ); return S_OK; }
static HRESULT WINAPI uwp_nav_probe26( void *iface, ... ) { TRACE( "navview-vtbl slot 26 hit\n" ); return S_OK; }
static HRESULT WINAPI uwp_nav_probe27( void *iface, ... ) { TRACE( "navview-vtbl slot 27 hit\n" ); return S_OK; }
static HRESULT WINAPI uwp_nav_probe28( void *iface, ... ) { TRACE( "navview-vtbl slot 28 hit\n" ); return S_OK; }
static HRESULT WINAPI uwp_nav_probe29( void *iface, ... ) { TRACE( "navview-vtbl slot 29 hit\n" ); return S_OK; }
static HRESULT WINAPI uwp_nav_probe30( void *iface, ... ) { TRACE( "navview-vtbl slot 30 hit\n" ); return S_OK; }
static HRESULT WINAPI uwp_nav_probe35( void *iface, ... ) { TRACE( "navview-vtbl slot 35 hit\n" ); return S_OK; }
static HRESULT WINAPI uwp_nav_probe38( void *iface, ... ) { TRACE( "navview-vtbl slot 38 hit\n" ); return S_OK; }
static HRESULT WINAPI uwp_nav_probe39( void *iface, ... ) { TRACE( "navview-vtbl slot 39 hit\n" ); return S_OK; }
static HRESULT WINAPI uwp_nav_probe41( void *iface, ... ) { TRACE( "navview-vtbl slot 41 hit\n" ); return S_OK; }
static HRESULT WINAPI uwp_nav_probe42( void *iface, ... ) { TRACE( "navview-vtbl slot 42 hit\n" ); return S_OK; }
static HRESULT WINAPI uwp_nav_probe44( void *iface, ... ) { TRACE( "navview-vtbl slot 44 hit\n" ); return S_OK; }
static HRESULT WINAPI uwp_nav_probe45( void *iface, ... ) { TRACE( "navview-vtbl slot 45 hit\n" ); return S_OK; }
static HRESULT WINAPI uwp_nav_probe46( void *iface, ... ) { TRACE( "navview-vtbl slot 46 hit\n" ); return S_OK; }
static HRESULT WINAPI uwp_nav_probe47( void *iface, ... ) { TRACE( "navview-vtbl slot 47 hit\n" ); return S_OK; }
static HRESULT WINAPI uwp_nav_probe48( void *iface, ... ) { TRACE( "navview-vtbl slot 48 hit\n" ); return S_OK; }
static HRESULT WINAPI uwp_nav_probe49( void *iface, ... ) { TRACE( "navview-vtbl slot 49 hit\n" ); return S_OK; }
static HRESULT WINAPI uwp_nav_probe50( void *iface, ... ) { TRACE( "navview-vtbl slot 50 hit\n" ); return S_OK; }
static HRESULT WINAPI uwp_nav_probe52( void *iface, ... ) { TRACE( "navview-vtbl slot 52 hit\n" ); return S_OK; }
static HRESULT WINAPI uwp_nav_probe53( void *iface, ... ) { TRACE( "navview-vtbl slot 53 hit\n" ); return S_OK; }
static HRESULT WINAPI uwp_nav_probe54( void *iface, ... ) { TRACE( "navview-vtbl slot 54 hit\n" ); return S_OK; }
static HRESULT WINAPI uwp_nav_probe55( void *iface, ... ) { TRACE( "navview-vtbl slot 55 hit\n" ); return S_OK; }
static HRESULT WINAPI uwp_nav_probe58( void *iface, ... ) { TRACE( "navview-vtbl slot 58 hit\n" ); return S_OK; }
static HRESULT WINAPI uwp_nav_probe59( void *iface, ... ) { TRACE( "navview-vtbl slot 59 hit\n" ); return S_OK; }
static HRESULT WINAPI uwp_nav_probe60( void *iface, ... ) { TRACE( "navview-vtbl slot 60 hit\n" ); return S_OK; }
static HRESULT WINAPI uwp_nav_probe61( void *iface, ... ) { TRACE( "navview-vtbl slot 61 hit\n" ); return S_OK; }

const void *uwp_navview_vtbl[112] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_nav_probe6,[11] = uwp_nav_probe11,[12] = uwp_nav_probe12,[19] = uwp_nav_probe19,[20] = uwp_nav_probe20,[21] = uwp_nav_probe21,[22] = uwp_nav_probe22,[23] = uwp_nav_probe23,[24] = uwp_nav_probe24,[25] = uwp_nav_probe25,[26] = uwp_nav_probe26,[27] = uwp_nav_probe27,[28] = uwp_nav_probe28,[29] = uwp_nav_probe29,[30] = uwp_nav_probe30,[35] = uwp_nav_probe35,[38] = uwp_nav_probe38,[39] = uwp_nav_probe39,[41] = uwp_nav_probe41,[42] = uwp_nav_probe42,[44] = uwp_nav_probe44,[45] = uwp_nav_probe45,[46] = uwp_nav_probe46,[47] = uwp_nav_probe47,[48] = uwp_nav_probe48,[49] = uwp_nav_probe49,[50] = uwp_nav_probe50,[52] = uwp_nav_probe52,[53] = uwp_nav_probe53,[54] = uwp_nav_probe54,[55] = uwp_nav_probe55,[58] = uwp_nav_probe58,[59] = uwp_nav_probe59,[60] = uwp_nav_probe60,[61] = uwp_nav_probe61,

    [7] = uwp_fe_get_Resources,
    [8] = uwp_fe_put_Resources,
    [9] = uwp_fe_get_Tag,
    [10] = uwp_fe_put_Tag,
    [13] = uwp_fe_get_ActualWidth,
    [14] = uwp_fe_get_ActualHeight,
    [15] = uwp_fe_get_Width,
    [16] = uwp_fe_put_Width,
    [17] = uwp_fe_get_Height,
    [18] = uwp_fe_put_Height,
    [31] = uwp_fe_get_Margin,
    [32] = uwp_fe_put_Margin,
    [33] = uwp_fe_get_Name,
    [34] = uwp_fe_put_Name,
    [36] = uwp_fe_get_DataContext,
    [37] = uwp_fe_put_DataContext,
    [40] = uwp_fe_get_Parent,
    [43] = uwp_fe_add_Loaded,
    [51] = uwp_fe_FindName,
    /* INavigationView ABI (winmd 2.8 metadata order, base = 3 + 53 IFE):
     * own[0]=IsPaneOpen(56), own[6]=FooterMenuItems(62),
     * own[46]=SelectionChanged(102), own[48]=ItemInvoked(104). */
    [56] = uwp_nav_get_IsPaneOpen,
    [57] = uwp_nav_put_IsPaneOpen,
    [62] = uwp_nav_get_FooterMenuItems,
    [63 ... 101] = uwp_generic_noop,
    [102] = uwp_ui_add_SelectionChanged,
    [103] = uwp_ui_remove_SelectionChanged,
    [104] = uwp_ui_add_ItemInvoked,
    [105] = uwp_ui_remove_ItemInvoked,
    [106 ... 111] = uwp_generic_noop,
};

const void *uwp_fe2_vtbl[16] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_fe2_get_RequestedTheme,      /* get_RequestedTheme */
    [7] = uwp_fe2_put_RequestedTheme,      /* put_RequestedTheme */
    [8] = uwp_fe3_add_DataContextChanged,  /* add_DataContextChanged */
    [9 ... 15] = uwp_generic_noop,
};

const void *uwp_fe3_vtbl[16] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_fe3_add_Loading,
    [7 ... 15] = uwp_generic_noop,
};

static HRESULT WINAPI uwp_ctrl_get_HorizontalContentAlignment( void *iface, int *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (out) *out = (impl && impl->has_h_content_align) ? impl->h_content_align : 1; /* Center */
    return S_OK;
}
static HRESULT WINAPI uwp_ctrl_put_HorizontalContentAlignment( void *iface, int val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl) { impl->h_content_align = val; impl->has_h_content_align = TRUE; uwp_invalidate_window(); }
    return S_OK;
}
static HRESULT WINAPI uwp_ctrl_get_VerticalContentAlignment( void *iface, int *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (out) *out = (impl && impl->has_v_content_align) ? impl->v_content_align : 1; /* Center */
    return S_OK;
}
static HRESULT WINAPI uwp_ctrl_put_VerticalContentAlignment( void *iface, int val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl) { impl->v_content_align = val; impl->has_v_content_align = TRUE; uwp_invalidate_window(); }
    return S_OK;
}

const void *uwp_ctrl_vtbl[64] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_ctrl_get_FontSize,     /* get_FontSize */
    [7] = uwp_ctrl_put_FontSize,     /* put_FontSize */
    [8] = uwp_ctrl_get_FontFamily,   /* get_FontFamily */
    [9] = uwp_ctrl_put_FontFamily,   /* put_FontFamily */
    [10 ... 21] = uwp_generic_noop,
    [22] = uwp_ctrl_get_IsEnabled,
    [23] = uwp_ctrl_put_IsEnabled,
    [24 ... 31] = uwp_generic_noop,
    [32] = uwp_ctrl_get_HorizontalContentAlignment,
    [33] = uwp_ctrl_put_HorizontalContentAlignment,
    [34] = uwp_ctrl_get_VerticalContentAlignment,
    [35] = uwp_ctrl_put_VerticalContentAlignment,
    [36 ... 43] = uwp_generic_noop,
    [44] = uwp_ctrl_ApplyTemplate,
    [45] = uwp_generic_noop,
    [46] = uwp_ctrl_Focus,
    [47 ... 63] = uwp_generic_noop,
};

const void *uwp_ctrl_prot_vtbl[16] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_ctrl_prot_get_DefaultStyleKey,
    [7] = uwp_ctrl_prot_put_DefaultStyleKey,
    [8] = uwp_ctrl_prot_GetTemplateChild,
    [9 ... 15] = uwp_generic_noop,
};

static HRESULT WINAPI uwp_flyout_get_Items( void *iface, void **out )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IFlyout_iface );
    if (!out) return E_POINTER;
    if (!impl) { *out = NULL; return E_NOINTERFACE; }
    *out = &impl->IVector_iface;
    uwp_sub_AddRef( &impl->IVector_iface );
    return S_OK;
}

static HRESULT WINAPI uwp_flyout_put_Text( void *iface, HSTRING val )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IFlyout_iface );
    const WCHAR *raw;
    if ((ULONG_PTR)val < 0x10000) return S_OK;
    raw = WindowsGetStringRawBuffer( val, NULL );
    if (impl && raw)
        uwp_elem_set_text( impl, raw );
    return S_OK;
}

static HRESULT WINAPI uwp_flyout_slot6( void *iface, void **out )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IFlyout_iface );
    if (!impl) return E_NOINTERFACE;
    if (out)
    {
        struct uwp_element *el, *target_el = NULL;
        AcquireSRWLockExclusive( &g_all_uwp_elems_lock );
        for (el = g_all_uwp_elems_head; el; el = el->next_reg)
        {
            if ((void *)out == (void *)el->outer ||
                (void *)out == (void *)el->provider_inst ||
                ((char *)out >= (char *)el && (char *)out < (char *)(el + 1)))
            {
                target_el = el;
                break;
            }
        }
        ReleaseSRWLockExclusive( &g_all_uwp_elems_lock );
        if (target_el)
        {
            if (!target_el->flyout)
            {
                target_el->flyout = &impl->IInspectable_iface;
                IInspectable_AddRef( target_el->flyout );
            }
            if (!impl->parent) impl->parent = target_el;
            g_popup_owner_el = target_el;
            uwp_invalidate_window();
            return S_OK;
        }
    }
    /* Only MenuFlyout / MenuBar / TabView use slot 6 for get_Items; IFlyoutBase::ShowAt passes a UIElement* */
    if (!wcscmp( impl->class_name, L"Windows.UI.Xaml.Controls.Flyout" ) ||
        !wcscmp( impl->class_name, L"Microsoft.UI.Xaml.Controls.CommandBarFlyout" ))
        return S_OK;
    return uwp_flyout_get_Items( iface, out );
}

static HRESULT WINAPI uwp_flyout_slot7( void *iface, HSTRING val )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IFlyout_iface );
    if (!impl) return S_OK;
    /* IFlyoutBase::Hide() takes no args; only MenuBarItem / MenuFlyoutSubItem use slot 7 for put_Title/put_Text */
    if (!(impl->class_flags & (UWP_CF_MENUBAR_ITEM | UWP_CF_MENU_FLYOUT_SUB_ITEM | UWP_CF_MENU_FLYOUT_ITEM)))
        return S_OK;
    return uwp_flyout_put_Text( iface, val );
}

static HRESULT WINAPI uwp_flyout_slot8( void *iface, void *arg1, EventRegistrationToken *tok )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IFlyout_iface );
    if (!impl) return S_OK;
    /* IFlyoutBase::add_Opened(handler, token*) on Flyout / MenuFlyout / CommandBarFlyout */
    if (!wcscmp( impl->class_name, L"Windows.UI.Xaml.Controls.Flyout" ) ||
        !wcscmp( impl->class_name, L"Windows.UI.Xaml.Controls.MenuFlyout" ) ||
        !wcscmp( impl->class_name, L"Microsoft.UI.Xaml.Controls.CommandBarFlyout" ) ||
        !wcscmp( impl->class_name, L"Windows.UI.Xaml.Documents.Hyperlink" ))
    {
        if (tok) tok->value = 1;
        return S_OK;
    }
    if (impl->class_flags & (UWP_CF_MENUBAR_ITEM | UWP_CF_MENU_FLYOUT_SUB_ITEM))
        return uwp_flyout_get_Items( iface, (void **)arg1 );
    return S_OK;
}

static HRESULT WINAPI uwp_tabview_put_SelectedItem( void *iface, void *item );

static HRESULT WINAPI uwp_tabview_get_SelectedIndex( void *iface, INT32 *out )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IFlyout_iface );
    IInspectable *items[64];
    int n, i, idx = 0;
    if (!out) return E_POINTER;
    if (impl && impl->selected_item)
    {
        n = uwp_tabview_get_items( impl, items, ARRAY_SIZE(items) );
        for (i = 0; i < n; i++)
        {
            if (items[i] == impl->selected_item) idx = i;
            IInspectable_Release( items[i] );
        }
    }
    *out = idx;
    return S_OK;
}

static HRESULT WINAPI uwp_tabview_put_SelectedIndex( void *iface, INT32 idx )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IFlyout_iface );
    IInspectable *items[64];
    int n, i;
    if (!impl) return S_OK;
    n = uwp_tabview_get_items( impl, items, ARRAY_SIZE(items) );
    if (idx >= 0 && idx < n)
        uwp_tabview_select_item( impl, items[idx] );
    for (i = 0; i < n; i++)
        IInspectable_Release( items[i] );
    return S_OK;
}

static HRESULT WINAPI uwp_tabview_get_AddTabButtonCommand( void *iface, void **out )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IFlyout_iface );
    if (!out) return E_POINTER;
    *out = impl ? impl->tab_add_cmd : NULL;
    if (*out) IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}

static HRESULT WINAPI uwp_tabview_put_AddTabButtonCommand( void *iface, void *cmd )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IFlyout_iface );
    if (!impl) return S_OK;
    if (impl->tab_add_cmd) IInspectable_Release( impl->tab_add_cmd );
    impl->tab_add_cmd = (IInspectable *)cmd;
    if (impl->tab_add_cmd) IInspectable_AddRef( impl->tab_add_cmd );
    TRACE( "ITabView::put_AddTabButtonCommand -> %p\n", cmd );
    return S_OK;
}

static HRESULT WINAPI uwp_tabview_get_AddTabButtonCommandParameter( void *iface, void **out )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IFlyout_iface );
    if (!out) return E_POINTER;
    *out = impl ? impl->tab_add_cmd_param : NULL;
    if (*out) IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}

static HRESULT WINAPI uwp_tabview_put_AddTabButtonCommandParameter( void *iface, void *param )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IFlyout_iface );
    if (!impl) return S_OK;
    if (impl->tab_add_cmd_param) IInspectable_Release( impl->tab_add_cmd_param );
    impl->tab_add_cmd_param = (IInspectable *)param;
    if (impl->tab_add_cmd_param) IInspectable_AddRef( impl->tab_add_cmd_param );
    return S_OK;
}

static HRESULT WINAPI uwp_tabview_add_TabCloseRequested( void *iface, void *handler, EventRegistrationToken *tok )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IFlyout_iface );
    if (tok) tok->value = 1;
    if (!impl || !handler) return S_OK;
    if (xaml_vec_grow( (void **)&impl->tab_close_handlers, &impl->tab_close_cap,
                       impl->tab_close_cnt + 1, sizeof(*impl->tab_close_handlers), 4 ))
    {
        impl->tab_close_handlers[impl->tab_close_cnt++] = (IInspectable *)handler;
        IUnknown_AddRef( (IUnknown *)handler );
        if (tok) tok->value = impl->tab_close_cnt;
    }
    TRACE( "ITabView::add_TabCloseRequested -> %p (cnt=%d)\n", handler, impl->tab_close_cnt );
    return S_OK;
}

static HRESULT WINAPI uwp_tabview_remove_TabCloseRequested( void *iface, EventRegistrationToken tok )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IFlyout_iface );
    int idx = (int)tok.value - 1;
    if (impl && idx >= 0 && idx < impl->tab_close_cnt && impl->tab_close_handlers[idx])
    {
        IUnknown_Release( (IUnknown *)impl->tab_close_handlers[idx] );
        impl->tab_close_handlers[idx] = NULL;
    }
    return S_OK;
}

static HRESULT WINAPI uwp_tabview_add_AddTabButtonClick( void *iface, void *handler, EventRegistrationToken *tok )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IFlyout_iface );
    if (tok) tok->value = 1;
    if (!impl || !handler) return S_OK;
    if (xaml_vec_grow( (void **)&impl->tab_add_handlers, &impl->tab_add_cap,
                       impl->tab_add_cnt + 1, sizeof(*impl->tab_add_handlers), 4 ))
    {
        impl->tab_add_handlers[impl->tab_add_cnt++] = (IInspectable *)handler;
        IUnknown_AddRef( (IUnknown *)handler );
        if (tok) tok->value = impl->tab_add_cnt;
    }
    TRACE( "ITabView::add_AddTabButtonClick -> %p (cnt=%d)\n", handler, impl->tab_add_cnt );
    return S_OK;
}

static HRESULT WINAPI uwp_tabview_remove_AddTabButtonClick( void *iface, EventRegistrationToken tok )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IFlyout_iface );
    int idx = (int)tok.value - 1;
    if (impl && idx >= 0 && idx < impl->tab_add_cnt && impl->tab_add_handlers[idx])
    {
        IUnknown_Release( (IUnknown *)impl->tab_add_handlers[idx] );
        impl->tab_add_handlers[idx] = NULL;
    }
    return S_OK;
}

static HRESULT WINAPI uwp_tabview_get_TabItemsSource( void *iface, void **out )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IFlyout_iface );
    if (!out) return E_POINTER;
    *out = impl ? impl->tab_items_source : NULL;
    if (*out) IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}

static HRESULT WINAPI uwp_tabview_put_TabItemsSource( void *iface, void *src )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IFlyout_iface );
    if (!impl) return S_OK;
    if (impl->tab_items_source) IInspectable_Release( impl->tab_items_source );
    impl->tab_items_source = (IInspectable *)src;
    if (impl->tab_items_source) IInspectable_AddRef( impl->tab_items_source );
    TRACE( "ITabView::put_TabItemsSource -> %p\n", src );
    uwp_invalidate_window();
    return S_OK;
}

struct uwp_tab_close_args
{
    IInspectable IInspectable_iface;
    LONG ref;
    IInspectable *item;
    IInspectable *tab;
};

static HRESULT WINAPI uwp_tab_close_args_QI( void *iface, REFIID iid, void **out )
{
    static const GUID IID_ITabViewTabCloseRequestedEventArgs =
        { 0xba8ef6c1, 0x2084, 0x5c36, { 0x89, 0x2d, 0x3b, 0x05, 0x2e, 0x26, 0xeb, 0x38 } };
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_ITabViewTabCloseRequestedEventArgs ))
    {
        *out = iface;
        InterlockedIncrement( &((struct uwp_tab_close_args *)iface)->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI uwp_tab_close_args_AddRef( void *iface )
{
    return InterlockedIncrement( &((struct uwp_tab_close_args *)iface)->ref );
}
static ULONG WINAPI uwp_tab_close_args_Release( void *iface )
{
    struct uwp_tab_close_args *a = iface;
    ULONG r = InterlockedDecrement( &a->ref );
    if (!r)
    {
        if (a->item) IInspectable_Release( a->item );
        if (a->tab) IInspectable_Release( a->tab );
        free( a );
    }
    return r;
}
static HRESULT WINAPI uwp_tab_close_args_get_Item( void *iface, void **out )
{
    struct uwp_tab_close_args *a = iface;
    if (!out) return E_POINTER;
    *out = a->item;
    if (*out) IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}
static HRESULT WINAPI uwp_tab_close_args_get_Tab( void *iface, void **out )
{
    struct uwp_tab_close_args *a = iface;
    if (!out) return E_POINTER;
    *out = a->tab ? a->tab : a->item;
    if (*out) IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}
static const void *uwp_tab_close_args_vtbl[12] =
{
    [0] = uwp_tab_close_args_QI,
    [1] = uwp_tab_close_args_AddRef,
    [2] = uwp_tab_close_args_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_tab_close_args_get_Item,
    [7] = uwp_tab_close_args_get_Tab,
    [8 ... 11] = uwp_generic_noop,
};

int uwp_tabview_get_items( struct uwp_element *tv, IInspectable **out_items, int max_items )
{
    int count = 0;

    if (!tv || !out_items || max_items <= 0) return 0;

    if (tv->tab_items_source)
        count = (int)uwp_winrt_vec_snapshot( tv->tab_items_source, out_items, (unsigned int)max_items );

    if (count == 0 && tv->children_coll)
        count = (int)uwp_winrt_vec_snapshot( tv->children_coll, out_items, (unsigned int)max_items );

    if (count == 0 && tv->selected_item)
    {
        out_items[0] = tv->selected_item;
        IInspectable_AddRef( out_items[0] );
        count = 1;
    }

    return count;
}

void uwp_tabview_get_item_title( IInspectable *item, WCHAR *buf, int buf_len )
{
    if (!buf || buf_len <= 0) return;
    buf[0] = 0;
    if (item && (ULONG_PTR)item >= 0x10000)
    {
        void *vm = NULL;
        if (SUCCEEDED(IInspectable_QueryInterface( item, &IID_ITabItemViewModel, &vm )) && vm)
        {
            HRESULT (WINAPI *get_name)(void *, HSTRING *) = (void *)(*(void ***)vm)[6];
            HSTRING hname = NULL;
            if (get_name && SUCCEEDED(get_name( vm, &hname )) && hname)
            {
                const WCHAR *s = WindowsGetStringRawBuffer( hname, NULL );
                if (s && s[0]) lstrcpynW( buf, s, buf_len );
                WindowsDeleteString( hname );
            }
            IUnknown_Release( (IUnknown *)vm );
        }
        if (!buf[0])
        {
            struct uwp_element *el = uwp_elem_from_any( item );
            if (uwp_has_text( el ))
                lstrcpynW( buf, uwp_elem_text( el ), buf_len );
        }
    }
    if (!buf[0])
        lstrcpynW( buf, L"Untitled", buf_len );
}

void uwp_tabview_compute_rects( const RECT *trc, int count, int idx, RECT *pill_rc, RECT *close_rc, RECT *add_rc )
{
    int avail_w = max( 80, (int)(trc->right - trc->left) - 42 );
    int n = max( 1, count );
    int tw = min( 186, max( 72, (avail_w - (n - 1) * 2) / n ) );
    int x0 = trc->left + 4 + idx * (tw + 2);
    if (pill_rc) SetRect( pill_rc, x0, trc->top + 3, min( trc->right - 34, x0 + tw ), trc->bottom );
    if (close_rc && pill_rc) SetRect( close_rc, pill_rc->right - 22, pill_rc->top + 2, pill_rc->right - 4, pill_rc->bottom - 2 );
    if (add_rc)
    {
        int ax = trc->left + 4 + n * (tw + 2) + 4;
        SetRect( add_rc, ax, trc->top + 4, min( trc->right - 2, ax + 28 ), trc->bottom - 2 );
    }
}

void uwp_tabview_fire_add_tab( struct uwp_element *tv )
{
    int i;
    if (!tv) return;
    TRACE( "uwp_tabview_fire_add_tab: cnt=%d cmd=%p\n", tv->tab_add_cnt, tv->tab_add_cmd );
    for (i = 0; i < tv->tab_add_cnt; i++)
    {
        if (tv->tab_add_handlers[i])
        {
            __TRY
            {
                IInspectable *sender = tv->outer ? tv->outer : &tv->IInspectable_iface;
                struct xaml_event_handler *h = (struct xaml_event_handler *)tv->tab_add_handlers[i];
                h->lpVtbl->Invoke( h, sender, NULL );
            }
            __EXCEPT_ALL
            {
                TRACE( "uwp_tabview_fire_add_tab: handler %d faulted\n", i );
            }
            __ENDTRY;
        }
    }
    if (tv->tab_add_cmd)
    {
        __TRY
        {
            void *cmd_iface = NULL;
            if ((SUCCEEDED(IInspectable_QueryInterface( tv->tab_add_cmd, &IID_ICommand_UWP, &cmd_iface )) ||
                 SUCCEEDED(IInspectable_QueryInterface( tv->tab_add_cmd, &IID_ICommand_UWP2, &cmd_iface ))) && cmd_iface)
            {
                struct command_iface *cmd = (struct command_iface *)cmd_iface;
                if (cmd->lpVtbl->Execute) cmd->lpVtbl->Execute( cmd, tv->tab_add_cmd_param );
                IUnknown_Release( (IUnknown *)cmd_iface );
            }
        }
        __EXCEPT_ALL
        {
            TRACE( "uwp_tabview_fire_add_tab: ICommand::Execute faulted\n" );
        }
        __ENDTRY;
    }
    uwp_invalidate_window();
}

void uwp_tabview_fire_close_tab( struct uwp_element *tv, IInspectable *item )
{
    struct uwp_tab_close_args *args;
    int i;
    if (!tv || !item) return;
    TRACE( "uwp_tabview_fire_close_tab: cnt=%d item=%p\n", tv->tab_close_cnt, item );
    args = calloc( 1, sizeof(*args) );
    if (!args) return;
    args->IInspectable_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_tab_close_args_vtbl;
    args->ref = 1;
    args->item = item;
    IInspectable_AddRef( item );
    for (i = 0; i < tv->tab_close_cnt; i++)
    {
        if (tv->tab_close_handlers[i])
        {
            __TRY
            {
                IInspectable *sender = tv->outer ? tv->outer : &tv->IInspectable_iface;
                struct xaml_event_handler *h = (struct xaml_event_handler *)tv->tab_close_handlers[i];
                h->lpVtbl->Invoke( h, sender, &args->IInspectable_iface );
            }
            __EXCEPT_ALL
            {
                TRACE( "uwp_tabview_fire_close_tab: handler %d faulted\n", i );
            }
            __ENDTRY;
        }
    }
    IInspectable_Release( &args->IInspectable_iface );
    uwp_invalidate_window();
}

void uwp_tabview_select_item( struct uwp_element *tv, IInspectable *item )
{
    if (!tv || !item) return;
    uwp_tabview_put_SelectedItem( &tv->IFlyout_iface, item );
}

static HRESULT WINAPI uwp_sel_args_QI( void *iface, REFIID iid, void **out )
{
    static const GUID IID_ISelectionChangedEventArgs_local =
        { 0xc972d2dc, 0xb609, 0x4758, { 0x85, 0x1e, 0xa7, 0x99, 0xc2, 0x1d, 0xe9, 0x7d } };
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IRoutedEventArgs_QI_UWP ) ||
        IsEqualGUID( iid, &IID_ISelectionChangedEventArgs_local ))
    {
        struct uwp_sel_changed_args *a = iface;
        *out = iface;
        InterlockedIncrement( &a->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI uwp_sel_args_AddRef( void *iface )
{
    return InterlockedIncrement( &((struct uwp_sel_changed_args *)iface)->ref );
}
ULONG WINAPI uwp_sel_args_Release( void *iface )
{
    struct uwp_sel_changed_args *a = iface;
    ULONG r = InterlockedDecrement( &a->ref );
    if (!r)
    {
        if (a->removed_items) IInspectable_Release( a->removed_items );
        if (a->added_items) IInspectable_Release( a->added_items );
        free( a );
    }
    return r;
}
static HRESULT WINAPI uwp_sel_args_GetRuntimeClassName( void *iface, HSTRING *name )
{
    static const WCHAR cls[] = L"Windows.UI.Xaml.Controls.SelectionChangedEventArgs";
    return WindowsCreateString( cls, ARRAY_SIZE(cls) - 1, name );
}
static HRESULT WINAPI uwp_sel_args_get_RemovedItems( void *iface, void **out )
{
    struct uwp_sel_changed_args *a = iface;
    if (!out) return E_POINTER;
    if (a && a->removed_items)
    {
        *out = a->removed_items;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    if (SUCCEEDED(uwp_vec_create( NULL, (IInspectable **)out )) && *out)
        CONTAINING_RECORD( (IInspectable *)*out, struct uwp_vec_view, IInspectable_iface )->runtime_class =
            L"Windows.Foundation.Collections.IVector`1<Object>";
    return S_OK;
}
static HRESULT WINAPI uwp_sel_args_get_AddedItems( void *iface, void **out )
{
    struct uwp_sel_changed_args *a = iface;
    if (!out) return E_POINTER;
    if (a && a->added_items)
    {
        *out = a->added_items;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    if (SUCCEEDED(uwp_vec_create( NULL, (IInspectable **)out )) && *out)
        CONTAINING_RECORD( (IInspectable *)*out, struct uwp_vec_view, IInspectable_iface )->runtime_class =
            L"Windows.Foundation.Collections.IVector`1<Object>";
    return S_OK;
}
static const void *uwp_sel_args_vtbl[16] =
{
    [0] = uwp_sel_args_QI,
    [1] = uwp_sel_args_AddRef,
    [2] = uwp_sel_args_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sel_args_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_sel_args_get_RemovedItems,
    [7] = uwp_sel_args_get_AddedItems,
    [8 ... 15] = uwp_generic_noop,
};

struct uwp_sel_changed_args *uwp_sel_args_create( IInspectable *old_item, IInspectable *new_item )
{
    struct uwp_sel_changed_args *a = calloc( 1, sizeof(*a) );
    if (!a) return NULL;
    a->IInspectable_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_sel_args_vtbl;
    a->ref = 1;
    uwp_vec_create( NULL, &a->removed_items );
    uwp_vec_create( NULL, &a->added_items );
    if (a->removed_items)
        CONTAINING_RECORD( a->removed_items, struct uwp_vec_view, IInspectable_iface )->runtime_class =
            L"Windows.Foundation.Collections.IVector`1<Object>";
    if (a->added_items)
        CONTAINING_RECORD( a->added_items, struct uwp_vec_view, IInspectable_iface )->runtime_class =
            L"Windows.Foundation.Collections.IVector`1<Object>";
    if (old_item && a->removed_items) uwp_vec_Append( a->removed_items, old_item );
    if (new_item && a->added_items) uwp_vec_Append( a->added_items, new_item );
    return a;
}

static HRESULT WINAPI uwp_tabview_get_SelectedItem( void *iface, void **out )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IFlyout_iface );
    if (!out) return E_POINTER;
    *out = impl ? impl->selected_item : NULL;
    if (*out) IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}

static HRESULT WINAPI uwp_tabview_put_SelectedItem( void *iface, void *item )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IFlyout_iface );
    if (!impl) return S_OK;
    if (impl->selected_item) IInspectable_Release( impl->selected_item );
    impl->selected_item = (IInspectable *)item;
    if (impl->selected_item) IInspectable_AddRef( impl->selected_item );
    TRACE( "ITabView::put_SelectedItem on %s (%s) -> %p (handler=%p)\n",
           debugstr_w(impl->class_name), debugstr_w(impl->name), item, impl->sel_changed_handler );
    if (impl->sel_changed_handler)
    {
        struct uwp_sel_changed_args *args = uwp_sel_args_create( NULL, impl->selected_item );
        IInspectable *sender = impl->outer ? impl->outer : &impl->IInspectable_iface;
        struct xaml_event_handler *h = (struct xaml_event_handler *)impl->sel_changed_handler;
        if (args)
        {
            __TRY { h->lpVtbl->Invoke( h, sender, &args->IInspectable_iface ); }
            __EXCEPT_ALL {}
            __ENDTRY;
            uwp_sel_args_Release( &args->IInspectable_iface );
        }
    }
    uwp_invalidate_window();
    return S_OK;
}

static HRESULT WINAPI uwp_tabview_add_SelectionChanged( void *iface, void *handler, EventRegistrationToken *tok )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IFlyout_iface );
    if (tok) tok->value = 1;
    if (!impl || !handler) return S_OK;
    if (impl->sel_changed_handler) IInspectable_Release( impl->sel_changed_handler );
    impl->sel_changed_handler = (IInspectable *)handler;
    IInspectable_AddRef( impl->sel_changed_handler );
    TRACE( "ITabView::add_SelectionChanged on %s (%s) -> %p\n",
           debugstr_w(impl->class_name), debugstr_w(impl->name), handler );
    return S_OK;
}

static HRESULT WINAPI uwp_tabview_remove_SelectionChanged( void *iface, EventRegistrationToken tok )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IFlyout_iface );
    if (impl && impl->sel_changed_handler)
    {
        IInspectable_Release( impl->sel_changed_handler );
        impl->sel_changed_handler = NULL;
    }
    return S_OK;
}

HRESULT WINAPI uwp_flyout_add_Closed( void *iface, void *handler, EventRegistrationToken *tok )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (tok) tok->value = 1;
    if (!impl || !handler) return S_OK;
    if (xaml_vec_grow( (void **)&impl->flyout_closed_handlers, &impl->flyout_closed_cap,
                       impl->flyout_closed_cnt + 1, sizeof(*impl->flyout_closed_handlers), 4 ))
    {
        impl->flyout_closed_handlers[impl->flyout_closed_cnt++] = (IInspectable *)handler;
        IUnknown_AddRef( (IUnknown *)handler );
        if (tok) tok->value = impl->flyout_closed_cnt;
    }
    return S_OK;
}

static HRESULT WINAPI uwp_flyout_add_Opening( void *iface, void *handler, EventRegistrationToken *tok )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (tok) tok->value = 1;
    if (!impl || !handler) return S_OK;
    if (xaml_vec_grow( (void **)&impl->flyout_opening_handlers, &impl->flyout_opening_cap,
                       impl->flyout_opening_cnt + 1, sizeof(*impl->flyout_opening_handlers), 4 ))
    {
        impl->flyout_opening_handlers[impl->flyout_opening_cnt++] = (IInspectable *)handler;
        IUnknown_AddRef( (IUnknown *)handler );
        if (tok) tok->value = impl->flyout_opening_cnt;
    }
    return S_OK;
}

const void *uwp_flyout_vtbl[128] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_flyout_slot6,
    [7] = uwp_flyout_slot7,
    [8] = uwp_flyout_slot8,
    [9] = uwp_generic_noop,
    [10] = uwp_flyout_add_Closed,
    [11] = uwp_generic_noop,
    [12] = uwp_flyout_add_Opening,
    [13 ... 19] = uwp_generic_noop,
    [20] = uwp_tabview_get_AddTabButtonCommand,
    [21] = uwp_tabview_put_AddTabButtonCommand,
    [22] = uwp_tabview_get_AddTabButtonCommandParameter,
    [23] = uwp_tabview_put_AddTabButtonCommandParameter,
    [24] = uwp_tabview_add_TabCloseRequested,
    [25] = uwp_tabview_remove_TabCloseRequested,
    [26 ... 27] = uwp_generic_noop,
    [28] = uwp_tabview_add_AddTabButtonClick,
    [29] = uwp_tabview_remove_AddTabButtonClick,
    [30 ... 31] = uwp_generic_noop,
    [32] = uwp_tabview_get_TabItemsSource,
    [33] = uwp_tabview_put_TabItemsSource,
    [34] = uwp_flyout_get_Items,
    [35 ... 44] = uwp_generic_noop,
    [45] = uwp_tabview_get_SelectedIndex,
    [46] = uwp_tabview_put_SelectedIndex,
    [47] = uwp_tabview_get_SelectedItem,
    [48] = uwp_tabview_put_SelectedItem,
    [49 ... 50] = uwp_generic_noop,
    [51] = uwp_tabview_add_SelectionChanged,
    [52] = uwp_tabview_remove_SelectionChanged,
    [53 ... 127] = uwp_generic_noop,
};

int uwp_selector_get_items( struct uwp_element *el, IInspectable **out_items, int max_items )
{
    int count = 0;

    if (!el || !out_items || max_items <= 0) return 0;

    if (el->items_source)
        count = (int)uwp_winrt_vec_snapshot( el->items_source, out_items, (unsigned int)max_items );

    if (count == 0 && el->children_coll)
        count = (int)uwp_winrt_vec_snapshot( el->children_coll, out_items, (unsigned int)max_items );
    return count;
}

static BOOL uwp_items_equal( IInspectable *a, IInspectable *b )
{
    WCHAR sa[128] = {0}, sb[128] = {0};
    if (a == b) return TRUE;
    if (!a || !b) return FALSE;
    {
        struct uwp_element *ea = uwp_elem_from_any( a );
        struct uwp_element *eb = uwp_elem_from_any( b );
        if (ea && eb && ea == eb) return TRUE;
    }
    if (uwp_unbox_string( a, sa, ARRAY_SIZE(sa) ) && uwp_unbox_string( b, sb, ARRAY_SIZE(sb) ))
        return !wcscmp( sa, sb );
    return FALSE;
}

void uwp_combobox_get_item_label( struct uwp_element *cb, IInspectable *item, WCHAR *buf, int buf_len )
{
    if (!buf || buf_len <= 0) return;
    buf[0] = 0;
    if (!item || (ULONG_PTR)item < 0x10000) return;

    if (cb && cb->display_member_path[0])
    {
        const WCHAR *dmp = cb->display_member_path;
        if (uwp_vm_query_string_prop( item, &dmp, 1, buf, buf_len ) && buf[0])
            return;
    }
    {
        struct uwp_element *iel = uwp_elem_from_any( item );
        if (iel)
        {
            if (uwp_has_text( iel ))
            {
                lstrcpynW( buf, uwp_elem_text( iel ), buf_len );
                return;
            }
            if (iel->content)
            {
                struct uwp_element *cel = uwp_elem_from_any( iel->content );
                if (uwp_has_text( cel ))
                {
                    lstrcpynW( buf, uwp_elem_text( cel ), buf_len );
                    return;
                }
                if (uwp_unbox_string( iel->content, buf, buf_len ) && buf[0])
                    return;
            }
        }
    }
    if (uwp_unbox_string( item, buf, buf_len ) && buf[0])
        return;
    {
        static const WCHAR *const cb_props[] = { L"DisplayName", L"Name", L"Title", L"Label", L"Text", L"Content" };
        if (uwp_vm_query_string_prop( item, cb_props, ARRAY_SIZE(cb_props), buf, buf_len ) && buf[0])
            return;
    }
    uwp_inspectable_to_string( item, buf, buf_len );
}

BOOL uwp_combobox_compute_popup_rect( struct uwp_element *cb, int item_cnt, int win_w, int win_h,
                                      RECT *out_rc, int *out_item_h, int *out_vis_cnt, int *out_pad_y )
{
    int item_h, pad_y, max_vis, popup_h, popup_w;
    int space_below, space_above, left, top, right, bottom;
    if (!cb || !cb->has_bounds || item_cnt <= 0 || win_w <= 0 || win_h <= 0) return FALSE;

    item_h = max( 28, (int)(32 * g_layout_scale + 0.5f) );
    pad_y = max( 4, (int)(4 * g_layout_scale + 0.5f) );
    max_vis = min( item_cnt, 9 );
    popup_h = max_vis * item_h + pad_y * 2;
    popup_w = max( (int)(cb->bounds.right - cb->bounds.left), (int)(160 * g_layout_scale + 0.5f) );
    if (popup_w > win_w - 8) popup_w = max( 80, win_w - 8 );

    space_below = win_h - cb->bounds.bottom - 6;
    space_above = cb->bounds.top - 6;
    if (space_below >= popup_h || space_below >= space_above)
    {
        top = cb->bounds.bottom + 2;
        if (space_below < popup_h)
        {
            max_vis = max( 1, (space_below - pad_y * 2) / item_h );
            popup_h = max_vis * item_h + pad_y * 2;
        }
        bottom = min( win_h - 4, top + popup_h );
    }
    else
    {
        bottom = cb->bounds.top - 2;
        if (space_above < popup_h)
        {
            max_vis = max( 1, (space_above - pad_y * 2) / item_h );
            popup_h = max_vis * item_h + pad_y * 2;
        }
        top = max( 4, bottom - popup_h );
    }
    left = max( 4, min( (int)cb->bounds.left, win_w - popup_w - 4 ) );
    right = min( win_w - 4, left + popup_w );

    if (cb->dropdown_scroll_offset > max( 0, item_cnt - max_vis ))
        cb->dropdown_scroll_offset = max( 0, item_cnt - max_vis );
    if (cb->dropdown_scroll_offset < 0)
        cb->dropdown_scroll_offset = 0;

    SetRect( &cb->dropdown_bounds, left, top, right, bottom );
    cb->dropdown_item_h = item_h;
    cb->dropdown_visible_cnt = max_vis;

    if (out_rc) *out_rc = cb->dropdown_bounds;
    if (out_item_h) *out_item_h = item_h;
    if (out_vis_cnt) *out_vis_cnt = max_vis;
    if (out_pad_y) *out_pad_y = pad_y;
    return TRUE;
}

static void uwp_fire_cb_event( struct uwp_element *cb, struct xaml_event_registry *reg )
{
    void **handlers = NULL;
    unsigned int n, i;
    IInspectable *sender;
    if (!cb || !reg) return;
    n = xaml_event_registry_snapshot_alloc( reg, &handlers );
    if (!n) return;
    sender = cb->outer ? cb->outer : &cb->IInspectable_iface;
    for (i = 0; i < n; i++)
    {
        struct xaml_event_handler *h = (struct xaml_event_handler *)handlers[i];
        __TRY { h->lpVtbl->Invoke( h, sender, NULL ); }
        __EXCEPT_ALL {}
        __ENDTRY;
        IUnknown_Release( (IUnknown *)handlers[i] );
    }
    free( handlers );
}

void uwp_combobox_set_open( struct uwp_element *cb, BOOL open )
{
    if (!cb) return;
    if (open && g_open_combobox && g_open_combobox != cb)
    {
        struct uwp_element *prev = g_open_combobox;
        prev->is_dropdown_open = FALSE;
        prev->hovered_dropdown_idx = -1;
        g_open_combobox = NULL;
        uwp_fire_cb_event( prev, &prev->cb_closed_reg );
    }
    if (cb->is_dropdown_open == open) return;
    cb->is_dropdown_open = open;
    cb->hovered_dropdown_idx = -1;
    if (open)
    {
        g_open_combobox = cb;
        cb->dropdown_scroll_offset = 0;
        uwp_fire_cb_event( cb, &cb->cb_opened_reg );
    }
    else
    {
        if (g_open_combobox == cb) g_open_combobox = NULL;
        uwp_fire_cb_event( cb, &cb->cb_closed_reg );
    }
    TRACE( "uwp_combobox_set_open(%s) -> %d\n", debugstr_w(cb->name), (int)open );
    uwp_invalidate_window();
}

void uwp_selector_set_selection( struct uwp_element *impl, INT32 new_idx, IInspectable *new_item, BOOL by_index, BOOL fire_event )
{
    IInspectable *items[256];
    int count, i;
    INT32 resolved_idx = -1;
    IInspectable *resolved_item = NULL;
    IInspectable *old_item = NULL;
    BOOL changed;

    if (!impl) return;
    count = uwp_selector_get_items( impl, items, ARRAY_SIZE(items) );

    if (by_index)
    {
        if (new_idx >= 0 && new_idx < count)
        {
            resolved_idx = new_idx;
            resolved_item = items[new_idx];
        }
        else
        {
            resolved_idx = -1;
            resolved_item = NULL;
        }
    }
    else
    {
        resolved_item = new_item;
        if (new_item)
        {
            for (i = 0; i < count; i++)
            {
                if (uwp_items_equal( items[i], new_item ))
                {
                    resolved_idx = i;
                    break;
                }
            }
        }
    }

    changed = (resolved_idx != impl->selected_index) || !uwp_items_equal( resolved_item, impl->selected_item );
    old_item = impl->selected_item;
    if (old_item) IInspectable_AddRef( old_item );

    impl->selected_index = resolved_idx;
    if (impl->selected_item != resolved_item)
    {
        if (impl->selected_item) IInspectable_Release( impl->selected_item );
        impl->selected_item = resolved_item;
        if (impl->selected_item) IInspectable_AddRef( impl->selected_item );
    }

    /* Keep any stored SelectedIndex / SelectedItem DP entries synchronized */
    if (impl->dp_cnt > 0 && impl->dp_store)
    {
        AcquireSRWLockExclusive( &impl->dp_lock );
        for (i = 0; i < impl->dp_cnt; i++)
        {
            struct { IDependencyProperty iface; LONG ref; void *meta; WCHAR name[64]; } *dpl = (void *)impl->dp_store[i].dp;
            if (!dpl || (ULONG_PTR)dpl < 0x10000) continue;
            if (!wcscmp( dpl->name, L"SelectedItem" ))
            {
                if (impl->dp_store[i].value != impl->selected_item)
                {
                    if (impl->dp_store[i].value) IInspectable_Release( impl->dp_store[i].value );
                    impl->dp_store[i].value = impl->selected_item;
                    if (impl->dp_store[i].value) IInspectable_AddRef( impl->dp_store[i].value );
                }
            }
            else if (!wcscmp( dpl->name, L"SelectedIndex" ))
            {
                IInspectable *bi = box_int32_value( impl->selected_index );
                if (bi)
                {
                    if (impl->dp_store[i].value) IInspectable_Release( impl->dp_store[i].value );
                    impl->dp_store[i].value = bi;
                }
            }
        }
        ReleaseSRWLockExclusive( &impl->dp_lock );
    }

    TRACE( "uwp_selector_set_selection(%s): idx=%d item=%p changed=%d fire=%d count=%d\n",
           debugstr_w(impl->name), resolved_idx, resolved_item, (int)changed, (int)fire_event, count );

    if (fire_event && changed)
    {
        void **handlers = NULL;
        unsigned int n = xaml_event_registry_snapshot_alloc( &impl->sel_changed_reg, &handlers );
        if (n > 0)
        {
            struct uwp_sel_changed_args *args = uwp_sel_args_create( old_item, impl->selected_item );
            IInspectable *sender = impl->outer ? impl->outer : &impl->IInspectable_iface;
            unsigned int hi;
            for (hi = 0; hi < n; hi++)
            {
                struct xaml_event_handler *h = (struct xaml_event_handler *)handlers[hi];
                if (h && args)
                {
                    __TRY { h->lpVtbl->Invoke( h, sender, &args->IInspectable_iface ); }
                    __EXCEPT_ALL { WARN( "SelectionChanged handler[%u] on %s threw\n", hi, debugstr_w(impl->name) ); }
                    __ENDTRY;
                }
                IUnknown_Release( (IUnknown *)handlers[hi] );
            }
            if (args) uwp_sel_args_Release( &args->IInspectable_iface );
            free( handlers );
        }
    }

    if (old_item) IInspectable_Release( old_item );
    for (i = 0; i < count; i++) IInspectable_Release( items[i] );
    uwp_invalidate_window();
}

static HRESULT WINAPI uwp_selector_get_SelectedIndex( void *iface, INT32 *out )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, ISelector_iface );
    if (!out) return E_POINTER;
    if (impl && impl->selected_index < 0 && impl->selected_item)
        uwp_selector_set_selection( impl, -1, impl->selected_item, FALSE, FALSE );
    *out = impl ? impl->selected_index : -1;
    TRACE( "ISelector::get_SelectedIndex -> %d\n", *out );
    return S_OK;
}
static HRESULT WINAPI uwp_selector_put_SelectedIndex( void *iface, INT32 idx )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, ISelector_iface );
    TRACE( "ISelector::put_SelectedIndex(%d)\n", idx );
    if (impl) uwp_selector_set_selection( impl, idx, NULL, TRUE, TRUE );
    return S_OK;
}
static HRESULT WINAPI uwp_selector_get_SelectedItem( void *iface, void **out )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, ISelector_iface );
    if (!out) return E_POINTER;
    if (impl && !impl->selected_item && impl->selected_index >= 0)
        uwp_selector_set_selection( impl, impl->selected_index, NULL, TRUE, FALSE );
    *out = impl ? impl->selected_item : NULL;
    if (*out) IInspectable_AddRef( (IInspectable *)*out );
    TRACE( "ISelector::get_SelectedItem -> %p\n", *out );
    return S_OK;
}
static HRESULT WINAPI uwp_selector_put_SelectedItem( void *iface, void *item )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, ISelector_iface );
    TRACE( "ISelector::put_SelectedItem(%p)\n", item );
    if (impl) uwp_selector_set_selection( impl, -1, (IInspectable *)item, FALSE, TRUE );
    return S_OK;
}
static HRESULT WINAPI uwp_selector_add_SelectionChanged( void *iface, void *handler, EventRegistrationToken *tok )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, ISelector_iface );
    if (!impl || !handler) { if (tok) tok->value = 1; return S_OK; }
    TRACE( "ISelector::add_SelectionChanged(%p) on %s\n", handler, debugstr_w(impl->name) );
    return xaml_event_registry_add( &impl->sel_changed_reg, handler, tok );
}
static HRESULT WINAPI uwp_selector_remove_SelectionChanged( void *iface, EventRegistrationToken tok )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, ISelector_iface );
    if (!impl) return S_OK;
    return xaml_event_registry_remove( &impl->sel_changed_reg, tok );
}
const void *uwp_selector_vtbl[20] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_selector_get_SelectedIndex,
    [7] = uwp_selector_put_SelectedIndex,
    [8] = uwp_selector_get_SelectedItem,
    [9] = uwp_selector_put_SelectedItem,
    [10 ... 15] = uwp_generic_noop,
    [16] = uwp_selector_add_SelectionChanged,
    [17] = uwp_selector_remove_SelectionChanged,
    [18 ... 19] = uwp_generic_noop,
};

static HRESULT WINAPI uwp_cb_get_IsDropDownOpen( void *iface, boolean *val )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IComboBox_iface );
    if (val) *val = impl ? (boolean)impl->is_dropdown_open : FALSE;
    return S_OK;
}
static HRESULT WINAPI uwp_cb_put_IsDropDownOpen( void *iface, boolean val )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IComboBox_iface );
    if (impl) uwp_combobox_set_open( impl, val != 0 );
    return S_OK;
}
static HRESULT WINAPI uwp_cb_get_IsSelectionBoxHighlighted( void *iface, boolean *val )
{
    if (val) *val = FALSE;
    return S_OK;
}
static HRESULT WINAPI uwp_cb_get_MaxDropDownHeight( void *iface, double *val )
{
    if (val) *val = 300.0;
    return S_OK;
}
static HRESULT WINAPI uwp_cb_get_SelectionBoxItem( void *iface, void **out )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IComboBox_iface );
    if (!out) return E_POINTER;
    *out = impl ? impl->selected_item : NULL;
    if (*out) IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}
static HRESULT WINAPI uwp_cb_add_DropDownClosed( void *iface, void *handler, EventRegistrationToken *tok )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IComboBox_iface );
    if (!impl || !handler) { if (tok) tok->value = 1; return S_OK; }
    return xaml_event_registry_add( &impl->cb_closed_reg, handler, tok );
}
static HRESULT WINAPI uwp_cb_remove_DropDownClosed( void *iface, EventRegistrationToken tok )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IComboBox_iface );
    if (!impl) return S_OK;
    return xaml_event_registry_remove( &impl->cb_closed_reg, tok );
}
static HRESULT WINAPI uwp_cb_add_DropDownOpened( void *iface, void *handler, EventRegistrationToken *tok )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IComboBox_iface );
    if (!impl || !handler) { if (tok) tok->value = 1; return S_OK; }
    return xaml_event_registry_add( &impl->cb_opened_reg, handler, tok );
}
static HRESULT WINAPI uwp_cb_remove_DropDownOpened( void *iface, EventRegistrationToken tok )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IComboBox_iface );
    if (!impl) return S_OK;
    return xaml_event_registry_remove( &impl->cb_opened_reg, tok );
}
const void *uwp_combobox_vtbl[24] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_cb_get_IsDropDownOpen,
    [7] = uwp_cb_put_IsDropDownOpen,
    [8] = uwp_cb_get_IsSelectionBoxHighlighted,
    [9] = uwp_cb_get_MaxDropDownHeight,
    [10] = uwp_generic_noop,
    [11] = uwp_cb_get_SelectionBoxItem,
    [12 ... 13] = uwp_generic_noop,
    [14] = uwp_cb_add_DropDownClosed,
    [15] = uwp_cb_remove_DropDownClosed,
    [16] = uwp_cb_add_DropDownOpened,
    [17] = uwp_cb_remove_DropDownOpened,
    [18 ... 23] = uwp_generic_noop,
};

static HRESULT WINAPI uwp_items_ctrl_get_ItemsSource( void *iface, void **out )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IItemsControl_iface );
    if (!out) return E_POINTER;
    *out = impl ? impl->items_source : NULL;
    if (*out) IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}

static void uwp_items_ctrl_append_item( struct uwp_element *impl, void *item )
{
    struct uwp_element *item_el = uwp_elem_from_any( item );
    if (item_el)
    {
        uwp_vec_Append( impl->children_coll, item );
    }
    else
    {
        IInspectable *btn_inner = NULL;
        IInspectable *tmpl = NULL;
        IInspectable *sel = (!(impl->class_flags & (UWP_CF_TILE_LIST | UWP_CF_ITEMS_REPEATER)))
                                ? (impl->item_template_selector ? impl->item_template_selector : impl->item_template) : NULL;
        if (sel)
        {
            void *dts = NULL;
            if (SUCCEEDED(IInspectable_QueryInterface( sel, &IID_IDataTemplateSelector_UWP, &dts )) && dts)
            {
                HRESULT (WINAPI *sel_fn)(void *, void *, void *, IInspectable **) = (void *)(*(void ***)dts)[6];
                __TRY { if (sel_fn) sel_fn( dts, item, &impl->IDependencyObject_iface, &tmpl ); }
                __EXCEPT_ALL { tmpl = NULL; }
                __ENDTRY;
                IUnknown_Release( (IUnknown *)dts );
            }
            if (!tmpl && SUCCEEDED(IInspectable_QueryInterface( sel, &IID_IDataTemplateSelector2_UWP, &dts )) && dts)
            {
                HRESULT (WINAPI *sel2_fn)(void *, void *, IInspectable **) = (void *)(*(void ***)dts)[6];
                __TRY { if (sel2_fn) sel2_fn( dts, item, &tmpl ); }
                __EXCEPT_ALL { tmpl = NULL; }
                __ENDTRY;
                IUnknown_Release( (IUnknown *)dts );
            }
        }
        if (!tmpl && impl->item_template)
        {
            tmpl = impl->item_template;
            IInspectable_AddRef( tmpl );
        }
        if (tmpl)
        {
            void *dt = NULL;
            if (SUCCEEDED(IInspectable_QueryInterface( tmpl, &IID_IDataTemplate_UWP, &dt )) && dt)
            {
                HRESULT (WINAPI *load_fn)(void *, void **) = (void *)(*(void ***)dt)[6];
                void *content = NULL;
                if (load_fn && SUCCEEDED(load_fn( dt, &content )) && content)
                {
                    struct uwp_element *cel = uwp_elem_from_any( content );
                    if (cel)
                    {
                        cel->parent = impl;
                        uwp_vec_Append( impl->children_coll, &cel->IInspectable_iface );
                        uwp_fe_put_DataContext( &cel->IInspectable_iface, item );
                        IUnknown_Release( (IUnknown *)content );
                        IUnknown_Release( (IUnknown *)dt );
                        IInspectable_Release( tmpl );
                        return;
                    }
                    IUnknown_Release( (IUnknown *)content );
                }
                IUnknown_Release( (IUnknown *)dt );
            }
            IInspectable_Release( tmpl );
        }
        if (SUCCEEDED(uwp_element_create( (impl->class_flags & UWP_CF_RADIO_BUTTON)
                                              ? L"Windows.UI.Xaml.Controls.ToggleButton"
                                              : L"Windows.UI.Xaml.Controls.Button",
                                          NULL, NULL, (void **)&btn_inner )) && btn_inner)
        {
            struct uwp_element *bel = uwp_elem_from_any( btn_inner );
            if (bel)
            {
                static const WCHAR *const act_props[] =
                    { L"BuyBoxProductActionText", L"ActionText", L"ButtonText", L"DisplayText",
                      L"Label", L"Title", L"Text", L"Content", L"Header", L"Name" };
                bel->parent = impl;
                bel->data_context = (IInspectable *)item;
                IInspectable_AddRef( bel->data_context );
                if (!(impl->class_flags & (UWP_CF_LISTVIEW | UWP_CF_GRIDVIEW)))
                {
                    bel->width = (impl->min_width > 44.0) ? impl->min_width : 44.0;
                    bel->height = (impl->min_width > 44.0) ? 40.0 : 36.0;
                }
                if (impl->min_width >= 120.0)
                {
                    bel->is_accent_style = TRUE;
                }
                else if (!(impl->class_flags & UWP_CF_RADIO_BUTTON))
                {
                    bel->has_bg = TRUE;
                    bel->bg_argb = 0xE02A2A2A;
                    bel->bg_alpha = 224;
                }
                {
                    WCHAR tmp_txt[512] = {0};
                    if (!uwp_vm_query_string_prop_depth( (IInspectable *)item, act_props, ARRAY_SIZE(act_props),
                                                         tmp_txt, ARRAY_SIZE(tmp_txt), 2 ))
                    {
                        uwp_inspectable_to_string( (IInspectable *)item, tmp_txt, ARRAY_SIZE(tmp_txt) );
                    }
                    if ((!tmp_txt[0] || wcschr( tmp_txt, L'.' )) && impl->min_width >= 120.0)
                        lstrcpynW( tmp_txt, L"Get", ARRAY_SIZE(tmp_txt) );
                    uwp_elem_set_text( bel, tmp_txt );
                }
                if (uwp_has_text( bel ) && impl->min_width <= 44.0 &&
                    !(impl->class_flags & (UWP_CF_LISTVIEW | UWP_CF_GRIDVIEW)))
                {
                    double tw = (double)wcslen( uwp_elem_text( bel ) ) * 8.5 + 28.0;
                    if (tw > bel->width) bel->width = tw;
                }
                uwp_vec_Append( impl->children_coll, &bel->IInspectable_iface );
            }
            IInspectable_Release( btn_inner );
        }
    }
}

void uwp_items_ctrl_materialize_source( struct uwp_element *impl, IInspectable *src )
{
    IInspectable *items[64];
    unsigned int count, i;

    if (!impl || !src) return;
    count = uwp_winrt_vec_snapshot( src, items, ARRAY_SIZE(items) );
    if (!count)
    {
        if (impl->children_coll) uwp_vec_Clear( impl->children_coll );
        return;
    }
    if (!impl->children_coll) uwp_vec_create( impl, &impl->children_coll );
    else uwp_vec_Clear( impl->children_coll );
    for (i = 0; i < count; i++)
    {
        uwp_items_ctrl_append_item( impl, items[i] );
        IInspectable_Release( items[i] );
    }
}

static HRESULT WINAPI uwp_items_ctrl_put_ItemsSource( void *iface, void *src )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IItemsControl_iface );
    if (!impl) return S_OK;
    if (impl->items_source != (IInspectable *)src)
    {
        if (impl->items_source) IInspectable_Release( impl->items_source );
        impl->items_source = (IInspectable *)src;
        if (impl->items_source) IInspectable_AddRef( impl->items_source );
    }
    TRACE( "IItemsControl::put_ItemsSource on %s (%s) -> %p\n",
           debugstr_w(impl->class_name), debugstr_w(impl->name), src );
    if (impl->items_source && !uwp_is_combobox( impl ))
        uwp_items_ctrl_materialize_source( impl, impl->items_source );
    if (uwp_is_autosuggest( impl ))
    {
        unsigned int cnt = impl->items_source ? uwp_winrt_vec_count( impl->items_source ) : 0;
        impl->is_open = (cnt > 0 && impl == g_focused_textbox && uwp_has_text( impl ));
        impl->is_dropdown_open = impl->is_open;
        impl->hovered_dropdown_idx = -1;
        impl->dropdown_scroll_offset = 0;
    }
    if (uwp_is_combobox( impl ) && impl->selected_item)
        uwp_selector_set_selection( impl, -1, impl->selected_item, FALSE, FALSE );
    g_layout_dirty = TRUE;
    uwp_invalidate_window();
    return S_OK;
}

static HRESULT WINAPI uwp_items_ctrl_get_Items( void *iface, void **out )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IItemsControl_iface );
    if (!out) return E_POINTER;
    if (!impl) { *out = NULL; return E_NOINTERFACE; }
    *out = &impl->IVector_iface;
    uwp_sub_AddRef( &impl->IVector_iface );
    return S_OK;
}

static HRESULT WINAPI uwp_items_ctrl_get_ItemTemplate( void *iface, void **out )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IItemsControl_iface );
    if (!out) return E_POINTER;
    *out = impl ? impl->item_template : NULL;
    if (*out) IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}

static HRESULT WINAPI uwp_items_ctrl_put_ItemTemplate( void *iface, void *val )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IItemsControl_iface );
    if (!impl) return S_OK;
    if (impl->item_template == (IInspectable *)val) return S_OK;
    if (impl->item_template) IInspectable_Release( impl->item_template );
    impl->item_template = (IInspectable *)val;
    if (impl->item_template) IInspectable_AddRef( impl->item_template );
    if (impl->items_source && !uwp_is_combobox( impl ))
        uwp_items_ctrl_materialize_source( impl, impl->items_source );
    g_layout_dirty = TRUE;
    uwp_invalidate_window();
    return S_OK;
}

static HRESULT WINAPI uwp_items_ctrl_get_ItemTemplateSelector( void *iface, void **out )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IItemsControl_iface );
    if (!out) return E_POINTER;
    *out = impl ? impl->item_template_selector : NULL;
    if (*out) IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}

static HRESULT WINAPI uwp_items_ctrl_put_ItemTemplateSelector( void *iface, void *val )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IItemsControl_iface );
    if (!impl) return S_OK;
    if (impl->item_template_selector == (IInspectable *)val) return S_OK;
    if (impl->item_template_selector) IInspectable_Release( impl->item_template_selector );
    impl->item_template_selector = (IInspectable *)val;
    if (impl->item_template_selector) IInspectable_AddRef( impl->item_template_selector );
    TRACE( "IItemsControl::put_ItemTemplateSelector on %s (%s) -> %p\n",
           debugstr_w(impl->class_name), debugstr_w(impl->name), val );
    if (impl->items_source && !uwp_is_combobox( impl ))
        uwp_items_ctrl_materialize_source( impl, impl->items_source );
    g_layout_dirty = TRUE;
    uwp_invalidate_window();
    return S_OK;
}

static HRESULT WINAPI uwp_items_ctrl_get_DisplayMemberPath( void *iface, HSTRING *out )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IItemsControl_iface );
    if (!out) return E_POINTER;
    if (impl && impl->display_member_path[0])
        return WindowsCreateString( impl->display_member_path, wcslen( impl->display_member_path ), out );
    *out = NULL;
    return S_OK;
}

static HRESULT WINAPI uwp_items_ctrl_put_DisplayMemberPath( void *iface, HSTRING val )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IItemsControl_iface );
    const WCHAR *raw = val ? WindowsGetStringRawBuffer( val, NULL ) : NULL;
    if (impl)
        lstrcpynW( impl->display_member_path, raw ? raw : L"", ARRAY_SIZE(impl->display_member_path) );
    return S_OK;
}

const void *uwp_items_ctrl_vtbl[32] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_items_ctrl_get_ItemsSource,
    [7] = uwp_items_ctrl_put_ItemsSource,
    [8] = uwp_items_ctrl_get_Items,
    [9] = uwp_items_ctrl_get_ItemTemplate,
    [10] = uwp_items_ctrl_put_ItemTemplate,
    [11] = uwp_items_ctrl_get_ItemTemplateSelector,
    [12] = uwp_items_ctrl_put_ItemTemplateSelector,
    [13 ... 14] = uwp_generic_noop,
    [15] = uwp_items_ctrl_get_DisplayMemberPath,
    [16] = uwp_items_ctrl_put_DisplayMemberPath,
    [17 ... 31] = uwp_generic_noop,
};

static HRESULT WINAPI uwp_key_args_QI( void *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IKeyRoutedEventArgs_UWP ))
    {
        *out = iface;
        InterlockedIncrement( &((struct uwp_key_routed_args *)iface)->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI uwp_key_args_AddRef( void *iface )
{
    return InterlockedIncrement( &((struct uwp_key_routed_args *)iface)->ref );
}
ULONG WINAPI uwp_key_args_Release( void *iface )
{
    struct uwp_key_routed_args *a = (struct uwp_key_routed_args *)iface;
    ULONG r = InterlockedDecrement( &a->ref );
    if (!r) free( a );
    return r;
}
static HRESULT WINAPI uwp_key_args_get_Key( void *iface, UINT *out )
{
    struct uwp_key_routed_args *a = (struct uwp_key_routed_args *)iface;
    if (out) *out = a->vk;
    return S_OK;
}
static HRESULT WINAPI uwp_key_args_get_KeyStatus( void *iface, void *out )
{
    struct uwp_key_routed_args *a = (struct uwp_key_routed_args *)iface;
    if (out) memcpy( out, &a->status, sizeof(a->status) );
    return S_OK;
}
static HRESULT WINAPI uwp_key_args_get_Handled( void *iface, boolean *out )
{
    struct uwp_key_routed_args *a = (struct uwp_key_routed_args *)iface;
    if (out) *out = a->handled;
    return S_OK;
}
static HRESULT WINAPI uwp_key_args_put_Handled( void *iface, boolean val )
{
    struct uwp_key_routed_args *a = (struct uwp_key_routed_args *)iface;
    a->handled = val ? TRUE : FALSE;
    return S_OK;
}
static HRESULT WINAPI uwp_key_args_GetIids( void *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}
static HRESULT WINAPI uwp_key_args_GetRuntimeClassName( void *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.UI.Xaml.Input.KeyRoutedEventArgs", 40, name );
}
static HRESULT WINAPI uwp_key_args_GetTrustLevel( void *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}
const void *uwp_key_args_vtbl[16] =
{
    [0] = uwp_key_args_QI,
    [1] = uwp_key_args_AddRef,
    [2] = uwp_key_args_Release,
    [3] = uwp_key_args_GetIids,
    [4] = uwp_key_args_GetRuntimeClassName,
    [5] = uwp_key_args_GetTrustLevel,
    [6] = uwp_key_args_get_Key,
    [7] = uwp_key_args_get_KeyStatus,
    [8] = uwp_key_args_get_Handled,
    [9] = uwp_key_args_put_Handled,
    [10 ... 15] = uwp_generic_noop,
};
