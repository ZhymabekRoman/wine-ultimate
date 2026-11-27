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
    [7] = uwp_nav_args_get_IsSettingsSelected,
    [8] = uwp_nav_args_get_Item,
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
struct uwp_element *g_open_cdp = NULL;

void uwp_nav_track_open_state( struct uwp_element *nav )
{
    if (!nav) return;
    if (uwp_elem_nav_is_pane_open( nav ) && uwp_is_nav_view( nav ))
        g_open_overlay_nav = nav;
    else if (g_open_overlay_nav == nav)
        g_open_overlay_nav = NULL;
}

void uwp_nav_set_pane_open( struct uwp_element *nav, BOOL open )
{
    void *is_open_dp = NULL;
    struct uwp_nav_ext *ne;
    int i;
    if (!nav) return;
    ne = uwp_elem_ensure_nav( nav );
    if (!ne) return;
    if (ne->nav_is_pane_open == open) return;
    ne->nav_is_pane_open = open;
    uwp_nav_track_open_state( nav );
    if (!open) ne->nav_scroll_y = 0;
    TRACE( "uwp_nav_set_pane_open(%s) -> %d\n", debugstr_w(nav->name), (int)open );

    /* Sync IsPaneOpen DP if registered on this NavigationView so WinUI stays aligned */
    AcquireSRWLockShared( &nav->dp_lock );
    for (i = 0; i < nav->dp_cnt; i++)
    {
        if (!wcscmp( xaml_dp_get_name( nav->dp_store[i].dp ), L"IsPaneOpen" ))
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
    if (open) *open = (boolean)uwp_elem_nav_is_pane_open( impl );
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
    [6] = uwp_nav_probe6,[11] = uwp_nav_probe11,[12] = uwp_nav_probe12,[19] = uwp_nav_probe19,[20] = uwp_nav_probe20,[21] = uwp_nav_probe21,[22] = uwp_nav_probe22,[23] = uwp_nav_probe23,[24] = uwp_nav_probe24,[25] = uwp_nav_probe25,[26] = uwp_nav_probe26,[27] = uwp_nav_probe27,[28] = uwp_nav_probe28,[29] = uwp_nav_probe29,[30] = uwp_nav_probe30,[35] = uwp_nav_probe35,[38] = uwp_nav_probe38,[39] = uwp_nav_probe39,[41] = uwp_nav_probe41,[42] = uwp_nav_probe42,[45] = uwp_nav_probe45,[46] = uwp_nav_probe46,[47] = uwp_nav_probe47,[48] = uwp_nav_probe48,[49] = uwp_nav_probe49,[50] = uwp_nav_probe50,[52] = uwp_nav_probe52,[53] = uwp_nav_probe53,[54] = uwp_nav_probe54,[55] = uwp_nav_probe55,[58] = uwp_nav_probe58,[59] = uwp_nav_probe59,[60] = uwp_nav_probe60,[61] = uwp_nav_probe61,

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
    [44] = uwp_fe_remove_Loaded,
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
    [9] = uwp_fe3_remove_DataContextChanged, /* remove_DataContextChanged */
    [10 ... 15] = uwp_generic_noop,
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
    [7] = uwp_fe3_remove_Loading,
    [8 ... 15] = uwp_generic_noop,
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
    const WCHAR *raw = val ? WindowsGetStringRawBuffer( val, NULL ) : L"";
    if (impl && raw)
        uwp_elem_set_text( impl, raw );
    return S_OK;
}

static HRESULT WINAPI uwp_flyout_slot6( void *iface, void **out )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IFlyout_iface );
    if (!impl) return E_NOINTERFACE;
    if (out && (impl->class_flags2 & (UWP_CF2_FLYOUT_EXACT | UWP_CF2_CMDBAR_FLYOUT_EXACT | UWP_CF2_MENU_FLYOUT_EXACT)))
    {
        struct uwp_element *target_el = uwp_elem_lookup_no_deref( out );
        if (target_el)
        {
            if (!target_el->flyout)
            {
                target_el->flyout = &impl->IInspectable_iface;
                IInspectable_AddRef( target_el->flyout );
            }
            if (!impl->parent) impl->parent = target_el;
            if (UWP_EV_CNT( impl, flyout_opening_reg ) > 0)
            {
                IInspectable *sender = impl->outer ? impl->outer : &impl->IFlyout_iface;
                uwp_fire_event_registry( &impl->events->flyout_opening_reg, sender, NULL, "Flyout::Opening" );
            }
            impl->is_open = TRUE;
            impl->is_dropdown_open = TRUE;
            g_popup_owner_el = target_el;
            g_layout_dirty = TRUE;
            uwp_invalidate_window();
            return S_OK;
        }
    }
    /* Only MenuFlyout / MenuBar / TabView use slot 6 for get_Items; IFlyoutBase::ShowAt passes a UIElement* */
    if (impl->class_flags2 & (UWP_CF2_FLYOUT_EXACT | UWP_CF2_CMDBAR_FLYOUT_EXACT))
        return S_OK;
    return uwp_flyout_get_Items( iface, out );
}

static HRESULT WINAPI uwp_flyout_slot7( void *iface, HSTRING val )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IFlyout_iface );
    if (!impl) return S_OK;
    /* IFlyoutBase::Hide() takes no args; only MenuBarItem / MenuFlyoutSubItem use slot 7 for put_Title/put_Text */
    if (!(impl->class_flags & (UWP_CF_MENUBAR_ITEM | UWP_CF_MENU_FLYOUT_SUB_ITEM | UWP_CF_MENU_FLYOUT_ITEM)))
    {
        if (g_popup_owner_el &&
            (g_popup_owner_el == impl ||
             (g_popup_owner_el->flyout && uwp_elem_from_any( g_popup_owner_el->flyout ) == impl)))
            uwp_close_flyout_popup( TRUE );
        impl->is_open = FALSE;
        impl->is_dropdown_open = FALSE;
        return S_OK;
    }
    return uwp_flyout_put_Text( iface, val );
}

static HRESULT WINAPI uwp_flyout_slot8( void *iface, void *arg1, EventRegistrationToken *tok )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IFlyout_iface );
    if (!impl) return S_OK;
    /* IFlyoutBase::add_Opened(handler, token*) on Flyout / MenuFlyout / CommandBarFlyout */
    if (impl->class_flags2 & (UWP_CF2_FLYOUT_EXACT | UWP_CF2_MENU_FLYOUT_EXACT |
                              UWP_CF2_CMDBAR_FLYOUT_EXACT | UWP_CF2_HYPERLINK_EXACT))
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
    if (impl && uwp_elem_selected_item( impl ))
    {
        IInspectable *sel = uwp_elem_selected_item( impl );
        n = uwp_tabview_get_items( impl, items, ARRAY_SIZE(items) );
        for (i = 0; i < n; i++)
        {
            if (items[i] == sel) idx = i;
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
    *out = (impl && impl->nav) ? impl->nav->tab_add_cmd : NULL;
    if (*out) IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}

static HRESULT WINAPI uwp_tabview_put_AddTabButtonCommand( void *iface, void *cmd )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IFlyout_iface );
    struct uwp_nav_ext *nav = uwp_elem_ensure_nav( impl );
    if (!nav) return S_OK;
    if (nav->tab_add_cmd) IInspectable_Release( nav->tab_add_cmd );
    nav->tab_add_cmd = (IInspectable *)cmd;
    if (nav->tab_add_cmd) IInspectable_AddRef( nav->tab_add_cmd );
    TRACE( "ITabView::put_AddTabButtonCommand -> %p\n", cmd );
    return S_OK;
}

static HRESULT WINAPI uwp_tabview_get_AddTabButtonCommandParameter( void *iface, void **out )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IFlyout_iface );
    if (!out) return E_POINTER;
    *out = (impl && impl->nav) ? impl->nav->tab_add_cmd_param : NULL;
    if (*out) IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}

static HRESULT WINAPI uwp_tabview_put_AddTabButtonCommandParameter( void *iface, void *param )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IFlyout_iface );
    struct uwp_nav_ext *nav = uwp_elem_ensure_nav( impl );
    if (!nav) return S_OK;
    if (nav->tab_add_cmd_param) IInspectable_Release( nav->tab_add_cmd_param );
    nav->tab_add_cmd_param = (IInspectable *)param;
    if (nav->tab_add_cmd_param) IInspectable_AddRef( nav->tab_add_cmd_param );
    return S_OK;
}

static HRESULT WINAPI uwp_tabview_add_TabCloseRequested( void *iface, void *handler, EventRegistrationToken *tok )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IFlyout_iface );
    struct uwp_event_bag *ev;
    HRESULT hr;
    if (tok) tok->value = 0;
    if (!impl || !handler) return S_OK;
    ev = uwp_elem_ensure_events( impl );
    if (!ev) return E_OUTOFMEMORY;
    hr = xaml_event_registry_add( &ev->tab_close_reg, handler, tok );
    TRACE( "ITabView::add_TabCloseRequested -> %p (cnt=%u)\n", handler, ev->tab_close_reg.count );
    return hr;
}

static HRESULT WINAPI uwp_tabview_remove_TabCloseRequested( void *iface, EventRegistrationToken tok )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IFlyout_iface );
    if (!impl || !impl->events) return S_OK;
    return xaml_event_registry_remove( &impl->events->tab_close_reg, tok );
}

static HRESULT WINAPI uwp_tabview_add_AddTabButtonClick( void *iface, void *handler, EventRegistrationToken *tok )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IFlyout_iface );
    struct uwp_event_bag *ev;
    HRESULT hr;
    if (tok) tok->value = 0;
    if (!impl || !handler) return S_OK;
    ev = uwp_elem_ensure_events( impl );
    if (!ev) return E_OUTOFMEMORY;
    hr = xaml_event_registry_add( &ev->tab_add_reg, handler, tok );
    TRACE( "ITabView::add_AddTabButtonClick -> %p (cnt=%u)\n", handler, ev->tab_add_reg.count );
    return hr;
}

static HRESULT WINAPI uwp_tabview_remove_AddTabButtonClick( void *iface, EventRegistrationToken tok )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IFlyout_iface );
    if (!impl || !impl->events) return S_OK;
    return xaml_event_registry_remove( &impl->events->tab_add_reg, tok );
}

static HRESULT WINAPI uwp_tabview_get_TabItemsSource( void *iface, void **out )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IFlyout_iface );
    if (!out) return E_POINTER;
    *out = (impl && impl->nav) ? impl->nav->tab_items_source : NULL;
    if (*out) IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}

static HRESULT WINAPI uwp_tabview_put_TabItemsSource( void *iface, void *src )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IFlyout_iface );
    struct uwp_nav_ext *nav = uwp_elem_ensure_nav( impl );
    if (!nav) return S_OK;
    if (nav->tab_items_source) IInspectable_Release( nav->tab_items_source );
    nav->tab_items_source = (IInspectable *)src;
    if (nav->tab_items_source) IInspectable_AddRef( nav->tab_items_source );
    uwp_tabview_sync_containers( impl );
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

    if (tv->nav && tv->nav->tab_items_source)
        count = (int)uwp_winrt_vec_snapshot( tv->nav->tab_items_source, out_items, (unsigned int)max_items );

    if (count == 0 && tv->children_coll)
        count = (int)uwp_winrt_vec_snapshot( tv->children_coll, out_items, (unsigned int)max_items );

    if (count == 0 && tv->nav && tv->nav->selected_item)
    {
        out_items[0] = tv->nav->selected_item;
        IInspectable_AddRef( out_items[0] );
        count = 1;
    }

    return count;
}

void uwp_tabview_get_item_title( IInspectable *item, WCHAR *buf, int buf_len )
{
    if (!buf || buf_len <= 0) return;
    buf[0] = 0;
    if (item)
    {
        void *vm = NULL;
        if (SUCCEEDED(IInspectable_QueryInterface( item, &IID_ITabItemViewModel, &vm )) && vm)
        {
            const struct abi_tab_item_vm *tvm = (const struct abi_tab_item_vm *)vm;
            HSTRING hname = NULL;
            if (tvm->lpVtbl->get_Name && SUCCEEDED(tvm->lpVtbl->get_Name( vm, &hname )) && hname)
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
    int avail_w = max( 80, (int)(trc->right - trc->left) - 44 );
    int n = max( 1, count );
    int tw = min( 220, max( 72, avail_w / n ) );
    int x0 = trc->left + 4 + idx * tw;
    int cy;
    if (pill_rc) SetRect( pill_rc, x0, trc->top + 4, min( trc->right - 36, x0 + tw ), trc->bottom );
    if (close_rc && pill_rc)
    {
        cy = (pill_rc->top + pill_rc->bottom) / 2;
        SetRect( close_rc, pill_rc->right - 26, cy - 11, pill_rc->right - 4, cy + 11 );
    }
    if (add_rc)
    {
        int ax = trc->left + 4 + n * tw + 6;
        int scy = (trc->top + trc->bottom) / 2;
        SetRect( add_rc, ax, max( trc->top + 3, scy - 13 ), min( trc->right - 2, ax + 30 ), min( trc->bottom - 2, scy + 13 ) );
    }
}

static struct uwp_element *uwp_find_named_child( struct uwp_element *owner, const WCHAR *name )
{
    int i;
    if (!owner || !name) return NULL;
    for (i = 0; i < owner->named_cnt; i++)
    {
        if (owner->named_elems[i] && !wcscmp( owner->named_elems[i]->name, name ))
            return owner->named_elems[i];
    }
    return NULL;
}

void uwp_tabview_sync_containers( struct uwp_element *tv )
{
    struct uwp_element *tlv;
    IInspectable *items[64];
    int count, i;
    BOOL match = FALSE;

    if (!tv) return;
    tlv = uwp_find_named_child( tv, L"TabListView" );
    if (!tlv) return;

    if (tv->nav && tlv->items_source != tv->nav->tab_items_source)
    {
        if (tlv->items_source) IInspectable_Release( tlv->items_source );
        tlv->items_source = tv->nav->tab_items_source;
        if (tlv->items_source) IInspectable_AddRef( tlv->items_source );
    }
    if (tv->item_template && tlv->item_template != tv->item_template)
    {
        if (tlv->item_template) IInspectable_Release( tlv->item_template );
        tlv->item_template = tv->item_template;
        IInspectable_AddRef( tlv->item_template );
    }
    if (!tlv->items_source) return;

    count = (int)uwp_winrt_vec_snapshot( tlv->items_source, items, ARRAY_SIZE(items) );
    if (tlv->children_coll)
    {
        struct uwp_vec_view *cv = CONTAINING_RECORD( tlv->children_coll, struct uwp_vec_view, IInspectable_iface );
        if ((int)cv->count == count)
        {
            match = TRUE;
            for (i = 0; i < count; i++)
            {
                struct uwp_element *cel = uwp_elem_from_any( cv->items[i] );
                if (!cel || (cel->data_context != items[i] && &cel->IInspectable_iface != items[i] &&
                             (!cel->outer || cel->outer != items[i])))
                {
                    match = FALSE;
                    break;
                }
            }
        }
    }
    if (!match)
    {
        uwp_items_ctrl_materialize_source( tlv, tlv->items_source );
    }
    if (tlv->children_coll)
    {
        struct uwp_vec_view *cv = CONTAINING_RECORD( tlv->children_coll, struct uwp_vec_view, IInspectable_iface );
        for (i = 0; (unsigned int)i < cv->count; i++)
        {
            struct uwp_element *cel = uwp_elem_from_any( cv->items[i] );
            if (cel)
            {
                cel->parent = tlv;
                uwp_apply_template_force( cel );
            }
        }
    }
    for (i = 0; i < count; i++)
        IInspectable_Release( items[i] );
}

void uwp_tabview_fire_add_tab( struct uwp_element *tv )
{
    struct uwp_element *add_btn;
    if (!tv) return;
    add_btn = uwp_find_named_child( tv, L"AddButton" );
    TRACE( "uwp_tabview_fire_add_tab: cnt=%u cmd=%p add_btn=%p(clicks=%u)\n",
           UWP_EV_CNT( tv, tab_add_reg ), tv->nav ? tv->nav->tab_add_cmd : NULL,
           add_btn, add_btn ? UWP_EV_CNT( add_btn, click_reg ) : 0 );
    if (add_btn && (UWP_EV_CNT( add_btn, click_reg ) > 0 || add_btn->command))
    {
        uwp_invoke_button( add_btn );
        return;
    }
    if (UWP_EV_CNT( tv, tab_add_reg ) > 0)
    {
        IInspectable *sender = tv->outer ? tv->outer : &tv->IInspectable_iface;
        uwp_fire_event_registry( &tv->events->tab_add_reg, sender, NULL, "TabView::AddTabButtonClick" );
    }
    if (tv->nav && tv->nav->tab_add_cmd)
    {
        __TRY
        {
            void *cmd_iface = NULL;
            if ((SUCCEEDED(IInspectable_QueryInterface( tv->nav->tab_add_cmd, &IID_ICommand_UWP, &cmd_iface )) ||
                 SUCCEEDED(IInspectable_QueryInterface( tv->nav->tab_add_cmd, &IID_ICommand_UWP2, &cmd_iface ))) && cmd_iface)
            {
                struct command_iface *cmd = (struct command_iface *)cmd_iface;
                if (cmd->lpVtbl->Execute) cmd->lpVtbl->Execute( cmd, tv->nav->tab_add_cmd_param );
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
    struct uwp_element *item_el, *tlv;
    if (!tv || !item) return;
    item_el = uwp_elem_from_any( item );
    if (item_el)
    {
        struct uwp_element *close_btn;
        uwp_apply_template_force( item_el );
        close_btn = uwp_find_named_child( item_el, L"CloseButton" );
        if (close_btn && (UWP_EV_CNT( close_btn, click_reg ) > 0 || close_btn->command))
        {
            uwp_invoke_button( close_btn );
            return;
        }
    }
    uwp_tabview_sync_containers( tv );
    tlv = uwp_find_named_child( tv, L"TabListView" );
    if (tlv && tlv->children_coll)
    {
        struct uwp_vec_view *cv = CONTAINING_RECORD( tlv->children_coll, struct uwp_vec_view, IInspectable_iface );
        unsigned int k;
        for (k = 0; k < cv->count; k++)
        {
            struct uwp_element *cel = uwp_elem_from_any( cv->items[k] );
            if (cel && (cel->data_context == item || &cel->IInspectable_iface == item ||
                        (cel->outer && cel->outer == item)))
            {
                struct uwp_element *close_btn;
                uwp_apply_template_force( cel );
                close_btn = uwp_find_named_child( cel, L"CloseButton" );
                TRACE( "uwp_tabview_fire_close_tab: container=%p close_btn=%p clicks=%u\n",
                       cel, close_btn, close_btn ? UWP_EV_CNT( close_btn, click_reg ) : 0 );
                if (close_btn && (UWP_EV_CNT( close_btn, click_reg ) > 0 || close_btn->command))
                {
                    uwp_invoke_button( close_btn );
                    return;
                }
                item_el = cel;
                break;
            }
        }
    }
    TRACE( "uwp_tabview_fire_close_tab: cnt=%u item=%p\n", UWP_EV_CNT( tv, tab_close_reg ), item );
    if (UWP_EV_CNT( tv, tab_close_reg ) > 0)
    {
        args = calloc( 1, sizeof(*args) );
        if (!args) return;
        args->IInspectable_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_tab_close_args_vtbl;
        args->ref = 1;
        args->item = item;
        IInspectable_AddRef( item );
        if (item_el)
        {
            args->tab = item_el->outer ? item_el->outer : &item_el->IInspectable_iface;
            IInspectable_AddRef( args->tab );
        }
        uwp_fire_event_registry( &tv->events->tab_close_reg,
                                 tv->outer ? tv->outer : &tv->IInspectable_iface,
                                 &args->IInspectable_iface, "TabView::TabCloseRequested" );
        IInspectable_Release( &args->IInspectable_iface );
    }
    uwp_invalidate_window();
}

void uwp_tabview_select_item( struct uwp_element *tv, IInspectable *item )
{
    struct uwp_element *tlv;
    if (!tv || !item) return;
    uwp_tabview_put_SelectedItem( &tv->IFlyout_iface, item );
    tlv = uwp_find_named_child( tv, L"TabListView" );
    if (tlv)
        uwp_selector_set_selection( tlv, -1, item, FALSE, TRUE );
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
    *out = uwp_elem_selected_item( impl );
    if (*out) IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}

static HRESULT WINAPI uwp_tabview_put_SelectedItem( void *iface, void *item )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IFlyout_iface );
    struct uwp_nav_ext *nav = uwp_elem_ensure_nav( impl );
    if (!nav) return S_OK;
    if (nav->selected_item) IInspectable_Release( nav->selected_item );
    nav->selected_item = (IInspectable *)item;
    if (nav->selected_item) IInspectable_AddRef( nav->selected_item );
    TRACE( "ITabView::put_SelectedItem on %s (%s) -> %p (handler=%p)\n",
           debugstr_w(impl->class_name), debugstr_w(impl->name), item, nav->sel_changed_handler );
    if (nav->sel_changed_handler)
    {
        struct uwp_sel_changed_args *args = uwp_sel_args_create( NULL, nav->selected_item );
        IInspectable *sender = impl->outer ? impl->outer : &impl->IInspectable_iface;
        struct xaml_event_handler *h = (struct xaml_event_handler *)nav->sel_changed_handler;
        if (args)
        {
            __TRY { h->lpVtbl->Invoke( h, sender, &args->IInspectable_iface ); }
            __EXCEPT_ALL { WARN( "%s: exception 0x%08lx\n", __func__, (unsigned long)GetExceptionCode() ); }
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
    struct uwp_nav_ext *nav;
    if (tok) tok->value = 1;
    if (!impl || !handler) return S_OK;
    nav = uwp_elem_ensure_nav( impl );
    if (!nav) return E_OUTOFMEMORY;
    if (nav->sel_changed_handler) IInspectable_Release( nav->sel_changed_handler );
    nav->sel_changed_handler = (IInspectable *)handler;
    IInspectable_AddRef( nav->sel_changed_handler );
    TRACE( "ITabView::add_SelectionChanged on %s (%s) -> %p\n",
           debugstr_w(impl->class_name), debugstr_w(impl->name), handler );
    return S_OK;
}

static HRESULT WINAPI uwp_tabview_remove_SelectionChanged( void *iface, EventRegistrationToken tok )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IFlyout_iface );
    if (impl && impl->nav && impl->nav->sel_changed_handler)
    {
        IInspectable_Release( impl->nav->sel_changed_handler );
        impl->nav->sel_changed_handler = NULL;
    }
    return S_OK;
}

HRESULT WINAPI uwp_flyout_add_Closed( void *iface, void *handler, EventRegistrationToken *tok )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    struct uwp_event_bag *ev;
    if (tok) tok->value = 0;
    if (!impl || !handler) return S_OK;
    ev = uwp_elem_ensure_events( impl );
    if (!ev) return E_OUTOFMEMORY;
    return xaml_event_registry_add( &ev->flyout_closed_reg, handler, tok );
}

HRESULT WINAPI uwp_flyout_remove_Closed( void *iface, EventRegistrationToken tok )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl || !impl->events) return S_OK;
    return xaml_event_registry_remove( &impl->events->flyout_closed_reg, tok );
}

static HRESULT WINAPI uwp_flyout_add_Opening( void *iface, void *handler, EventRegistrationToken *tok )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    struct uwp_event_bag *ev;
    if (tok) tok->value = 0;
    if (!impl || !handler) return S_OK;
    ev = uwp_elem_ensure_events( impl );
    if (!ev) return E_OUTOFMEMORY;
    return xaml_event_registry_add( &ev->flyout_opening_reg, handler, tok );
}

static HRESULT WINAPI uwp_flyout_remove_Opening( void *iface, EventRegistrationToken tok )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl || !impl->events) return S_OK;
    return xaml_event_registry_remove( &impl->events->flyout_opening_reg, tok );
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
    [11] = uwp_flyout_remove_Closed,
    [12] = uwp_flyout_add_Opening,
    [13] = uwp_flyout_remove_Opening,
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
    INT32 ia = 0, ib = 0;
    double da = 0.0, db = 0.0;
    boolean ba = FALSE, bb = FALSE;
    if (a == b) return TRUE;
    if (!a || !b) return FALSE;
    {
        struct uwp_element *ea = uwp_elem_from_any( a );
        struct uwp_element *eb = uwp_elem_from_any( b );
        if (ea && eb && ea == eb) return TRUE;
    }
    {
        IUnknown *ua = NULL, *ub = NULL;
        BOOL same_unk = FALSE;
        if (SUCCEEDED(IInspectable_QueryInterface( a, &IID_IUnknown, (void **)&ua )) && ua &&
            SUCCEEDED(IInspectable_QueryInterface( b, &IID_IUnknown, (void **)&ub )) && ub)
            same_unk = (ua == ub);
        if (ua) IUnknown_Release( ua );
        if (ub) IUnknown_Release( ub );
        if (same_unk) return TRUE;
    }
    if (uwp_unbox_string( a, sa, ARRAY_SIZE(sa) ) && uwp_unbox_string( b, sb, ARRAY_SIZE(sb) ))
        return !wcscmp( sa, sb );
    if (uwp_unbox_int32( a, &ia ) && uwp_unbox_int32( b, &ib ))
        return ia == ib;
    if (uwp_unbox_bool( a, &ba ) && uwp_unbox_bool( b, &bb ))
        return (ba != 0) == (bb != 0);
    if (uwp_unbox_double( a, &da ) && uwp_unbox_double( b, &db ))
        return da == db;
    return FALSE;
}

void uwp_combobox_get_item_label( struct uwp_element *cb, IInspectable *item, WCHAR *buf, int buf_len )
{
    if (!buf || buf_len <= 0) return;
    buf[0] = 0;
    if (!item) return;

    if (uwp_elem_display_member_path( cb )[0])
    {
        const WCHAR *dmp = uwp_elem_display_member_path( cb );
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
        static const WCHAR *const cb_props[] = { L"DisplayName", L"Name", L"Title", L"Header", L"Label", L"Text", L"Content" };
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

    if (out_rc) SetRect( out_rc, left, top, right, bottom );
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
        __EXCEPT_ALL { WARN( "%s: exception 0x%08lx\n", __func__, (unsigned long)GetExceptionCode() ); }
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
        uwp_fire_cb_event( prev, UWP_EV_REG( prev, cb_closed_reg ) );
    }
    if (cb->is_dropdown_open == open) return;
    cb->is_dropdown_open = open;
    cb->hovered_dropdown_idx = -1;
    if (open)
    {
        g_open_combobox = cb;
        cb->dropdown_scroll_offset = 0;
        uwp_fire_cb_event( cb, UWP_EV_REG( cb, cb_opened_reg ) );
    }
    else
    {
        if (g_open_combobox == cb) g_open_combobox = NULL;
        uwp_fire_cb_event( cb, UWP_EV_REG( cb, cb_closed_reg ) );
    }
    TRACE( "uwp_combobox_set_open(%s) -> %d\n", debugstr_w(cb->name), (int)open );
    uwp_invalidate_window();
}

void uwp_selector_set_selection( struct uwp_element *impl, INT32 new_idx, IInspectable *new_item, BOOL by_index, BOOL fire_event )
{
    static struct uwp_element *sel_sync_stack[16];
    static int sel_sync_depth = 0;
    IInspectable *items[256];
    int count, i, si;
    INT32 resolved_idx = -1;
    IInspectable *resolved_item = NULL;
    IInspectable *old_item = NULL;
    BOOL changed;
    struct uwp_nav_ext *nav;

    if (!impl) return;
    for (si = 0; si < sel_sync_depth; si++)
        if (sel_sync_stack[si] == impl) return;
    nav = uwp_elem_ensure_nav( impl );
    if (!nav) return;
    if (sel_sync_depth < (int)ARRAY_SIZE(sel_sync_stack))
        sel_sync_stack[sel_sync_depth++] = impl;
    count = uwp_selector_get_items( impl, items, ARRAY_SIZE(items) );

    if (by_index)
    {
        if (new_idx >= 0 && new_idx < count)
        {
            struct uwp_element *iel = uwp_elem_from_any( items[new_idx] );
            resolved_idx = new_idx;
            resolved_item = (new_item && iel && iel->data_context == new_item) ? new_item : items[new_idx];
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
                struct uwp_element *iel = uwp_elem_from_any( items[i] );
                if (uwp_items_equal( items[i], new_item ) ||
                    (iel && iel->data_context && uwp_items_equal( iel->data_context, new_item )))
                {
                    resolved_idx = i;
                    break;
                }
            }
        }
    }

    changed = (resolved_idx != nav->selected_index) || !uwp_items_equal( resolved_item, nav->selected_item );
    old_item = nav->selected_item;
    if (old_item) IInspectable_AddRef( old_item );

    nav->selected_index = resolved_idx;
    if (nav->selected_item != resolved_item)
    {
        if (nav->selected_item) IInspectable_Release( nav->selected_item );
        nav->selected_item = resolved_item;
        if (nav->selected_item) IInspectable_AddRef( nav->selected_item );
    }
    if (uwp_is_combobox( impl ))
    {
        struct uwp_vec_view *cv = impl->children_coll
            ? CONTAINING_RECORD( impl->children_coll, struct uwp_vec_view, IInspectable_iface ) : NULL;
        struct uwp_element *ce = (cv && cv->count > 0) ? uwp_elem_from_any( cv->items[0] ) : NULL;
        if (changed && ce && ce->dt_binding_connector && resolved_item)
            uwp_fe_put_DataContext( &ce->IInspectable_iface, resolved_item );
    }

    /* Synchronize SelectedItem / SelectedIndex DPs through SetValue when changed
     * so PropertyChangedCallback (e.g. WinUI RadioButtons) and x:Bind TwoWay
     * RegisterPropertyChangedCallback fire; also push TwoWay classic bindings. */
    if (changed)
    {
        void *item_dps[8], *idx_dps[8];
        int item_dp_cnt = 0, idx_dp_cnt = 0, k;

        AcquireSRWLockShared( &impl->dp_lock );
        for (i = 0; i < impl->dp_cnt; i++)
        {
            const WCHAR *dpn = xaml_dp_get_name( impl->dp_store[i].dp );
            if (!dpn[0]) continue;
            if (!wcscmp( dpn, L"SelectedItem" ) && item_dp_cnt < (int)ARRAY_SIZE(item_dps))
            {
                for (k = 0; k < item_dp_cnt; k++) if (item_dps[k] == impl->dp_store[i].dp) break;
                if (k == item_dp_cnt) item_dps[item_dp_cnt++] = impl->dp_store[i].dp;
            }
            else if (!wcscmp( dpn, L"SelectedIndex" ) && idx_dp_cnt < (int)ARRAY_SIZE(idx_dps))
            {
                for (k = 0; k < idx_dp_cnt; k++) if (idx_dps[k] == impl->dp_store[i].dp) break;
                if (k == idx_dp_cnt) idx_dps[idx_dp_cnt++] = impl->dp_store[i].dp;
            }
        }
        for (i = 0; i < impl->dp_cb_cnt; i++)
        {
            const WCHAR *dpn = xaml_dp_get_name( impl->dp_cbs[i].dp );
            if (!dpn[0]) continue;
            if (!wcscmp( dpn, L"SelectedItem" ) && item_dp_cnt < (int)ARRAY_SIZE(item_dps))
            {
                for (k = 0; k < item_dp_cnt; k++) if (item_dps[k] == impl->dp_cbs[i].dp) break;
                if (k == item_dp_cnt) item_dps[item_dp_cnt++] = impl->dp_cbs[i].dp;
            }
            else if (!wcscmp( dpn, L"SelectedIndex" ) && idx_dp_cnt < (int)ARRAY_SIZE(idx_dps))
            {
                for (k = 0; k < idx_dp_cnt; k++) if (idx_dps[k] == impl->dp_cbs[i].dp) break;
                if (k == idx_dp_cnt) idx_dps[idx_dp_cnt++] = impl->dp_cbs[i].dp;
            }
        }
        for (i = 0; i < item_dp_cnt; i++)
            if ((ULONG_PTR)item_dps[i] >= 0x10000) IUnknown_AddRef( (IUnknown *)item_dps[i] );
        for (i = 0; i < idx_dp_cnt; i++)
            if ((ULONG_PTR)idx_dps[i] >= 0x10000) IUnknown_AddRef( (IUnknown *)idx_dps[i] );
        ReleaseSRWLockShared( &impl->dp_lock );

        if (fire_event)
        {
            for (i = 0; i < idx_dp_cnt; i++)
            {
                IInspectable *bi = box_int32_value( nav->selected_index );
                if (bi)
                {
                    uwp_do_SetValue( &impl->IDependencyObject_iface, idx_dps[i], bi );
                    IInspectable_Release( bi );
                }
            }
            for (i = 0; i < item_dp_cnt; i++)
                uwp_do_SetValue( &impl->IDependencyObject_iface, item_dps[i], nav->selected_item );

            /* Update any TwoWay classic binding on SelectedItem / SelectedIndex */
            for (i = 0; i < impl->dc_bound_dp_cnt; i++)
            {
                const WCHAR *dpn = xaml_dp_get_name( impl->dc_bound_dps[i].dp );
                struct uwp_element *bel = impl->dc_bound_dps[i].binding;
                if (!wcscmp( dpn, L"SelectedItem" ) || !wcscmp( dpn, L"SelectedIndex" ))
                {
                    IInspectable *src = (bel && bel->data_context) ? bel->data_context : impl->data_context;
                    HSTRING h_path = NULL;
                    const WCHAR *path_str = NULL;
                    if (bel && bel->tag)
                    {
                        void *pp = NULL;
                        if (SUCCEEDED(IInspectable_QueryInterface( bel->tag, &IID_IPropertyPath_UWP, &pp )) && pp)
                        {
                            const struct abi_property_path *app = (const struct abi_property_path *)pp;
                            if (app->lpVtbl->get_Path && SUCCEEDED(app->lpVtbl->get_Path( pp, &h_path )) && h_path)
                                path_str = WindowsGetStringRawBuffer( h_path, NULL );
                            IUnknown_Release( (IUnknown *)pp );
                        }
                    }
                    if ((!path_str || !path_str[0]) && uwp_has_text( bel ))
                        path_str = uwp_elem_text( bel );
                    if (src && path_str && path_str[0])
                    {
                        if (!wcscmp( dpn, L"SelectedItem" ))
                            uwp_update_binding_source( src, path_str, nav->selected_item );
                        else
                        {
                            IInspectable *bi = box_int32_value( nav->selected_index );
                            if (bi)
                            {
                                uwp_update_binding_source( src, path_str, bi );
                                IInspectable_Release( bi );
                            }
                        }
                    }
                    if (h_path) WindowsDeleteString( h_path );
                }
            }
        }
        else if (impl->dp_cnt > 0 && impl->dp_store)
        {
            AcquireSRWLockExclusive( &impl->dp_lock );
            for (i = 0; i < impl->dp_cnt; i++)
            {
                const WCHAR *dpn = xaml_dp_get_name( impl->dp_store[i].dp );
                if (!dpn[0]) continue;
                if (!wcscmp( dpn, L"SelectedItem" ))
                {
                    if (impl->dp_store[i].value != nav->selected_item)
                    {
                        if (impl->dp_store[i].value) IInspectable_Release( impl->dp_store[i].value );
                        impl->dp_store[i].value = nav->selected_item;
                        if (impl->dp_store[i].value) IInspectable_AddRef( impl->dp_store[i].value );
                    }
                }
                else if (!wcscmp( dpn, L"SelectedIndex" ))
                {
                    IInspectable *bi = box_int32_value( nav->selected_index );
                    if (bi)
                    {
                        if (impl->dp_store[i].value) IInspectable_Release( impl->dp_store[i].value );
                        impl->dp_store[i].value = bi;
                    }
                }
            }
            ReleaseSRWLockExclusive( &impl->dp_lock );
        }

        for (i = 0; i < item_dp_cnt; i++)
            if ((ULONG_PTR)item_dps[i] >= 0x10000) IUnknown_Release( (IUnknown *)item_dps[i] );
        for (i = 0; i < idx_dp_cnt; i++)
            if ((ULONG_PTR)idx_dps[i] >= 0x10000) IUnknown_Release( (IUnknown *)idx_dps[i] );
    }

    TRACE( "uwp_selector_set_selection(%s): idx=%d item=%p changed=%d fire=%d count=%d\n",
           debugstr_w(impl->name), resolved_idx, resolved_item, (int)changed, (int)fire_event, count );

    if (fire_event && changed && impl->events)
    {
        void **handlers = NULL;
        unsigned int n = xaml_event_registry_snapshot_alloc( &impl->events->sel_changed_reg, &handlers );
        if (n > 0)
        {
            struct uwp_sel_changed_args *args = uwp_sel_args_create( old_item, nav->selected_item );
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

    for (si = sel_sync_depth - 1; si >= 0; si--)
    {
        if (sel_sync_stack[si] == impl)
        {
            sel_sync_stack[si] = sel_sync_stack[--sel_sync_depth];
            break;
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
    if (impl && impl->nav && impl->nav->selected_index < 0 && impl->nav->selected_item)
        uwp_selector_set_selection( impl, -1, impl->nav->selected_item, FALSE, FALSE );
    *out = uwp_elem_selected_index( impl );
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
    if (impl && impl->nav && !impl->nav->selected_item && impl->nav->selected_index >= 0)
        uwp_selector_set_selection( impl, impl->nav->selected_index, NULL, TRUE, FALSE );
    *out = uwp_elem_selected_item( impl );
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
    struct uwp_event_bag *ev;
    if (!impl || !handler) { if (tok) tok->value = 1; return S_OK; }
    ev = uwp_elem_ensure_events( impl );
    if (!ev) return E_OUTOFMEMORY;
    TRACE( "ISelector::add_SelectionChanged(%p) on %s\n", handler, debugstr_w(impl->name) );
    return xaml_event_registry_add( &ev->sel_changed_reg, handler, tok );
}
static HRESULT WINAPI uwp_selector_remove_SelectionChanged( void *iface, EventRegistrationToken tok )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, ISelector_iface );
    if (!impl || !impl->events) return S_OK;
    return xaml_event_registry_remove( &impl->events->sel_changed_reg, tok );
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
    *out = uwp_elem_selected_item( impl );
    if (*out) IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}
static HRESULT WINAPI uwp_cb_add_DropDownClosed( void *iface, void *handler, EventRegistrationToken *tok )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IComboBox_iface );
    struct uwp_event_bag *ev;
    if (!impl || !handler) { if (tok) tok->value = 1; return S_OK; }
    ev = uwp_elem_ensure_events( impl );
    if (!ev) return E_OUTOFMEMORY;
    return xaml_event_registry_add( &ev->cb_closed_reg, handler, tok );
}
static HRESULT WINAPI uwp_cb_remove_DropDownClosed( void *iface, EventRegistrationToken tok )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IComboBox_iface );
    if (!impl || !impl->events) return S_OK;
    return xaml_event_registry_remove( &impl->events->cb_closed_reg, tok );
}
static HRESULT WINAPI uwp_cb_add_DropDownOpened( void *iface, void *handler, EventRegistrationToken *tok )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IComboBox_iface );
    struct uwp_event_bag *ev;
    if (!impl || !handler) { if (tok) tok->value = 1; return S_OK; }
    ev = uwp_elem_ensure_events( impl );
    if (!ev) return E_OUTOFMEMORY;
    return xaml_event_registry_add( &ev->cb_opened_reg, handler, tok );
}
static HRESULT WINAPI uwp_cb_remove_DropDownOpened( void *iface, EventRegistrationToken tok )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IComboBox_iface );
    if (!impl || !impl->events) return S_OK;
    return xaml_event_registry_remove( &impl->events->cb_opened_reg, tok );
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

/* Windows.UI.Xaml.Controls.IComboBox6 ({61aad6a5-81fb-5f87-bae3-369fbe2ea1f3}) */
static HRESULT WINAPI uwp_cb6_get_bool_false( void *iface, boolean *val )
{
    if (val) *val = FALSE;
    return S_OK;
}
static HRESULT WINAPI uwp_cb6_get_Text( void *iface, HSTRING *out )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, ISplitView2_iface );
    WCHAR buf[256] = {0};
    const WCHAR *txt = L"";
    if (!out) return E_POINTER;
    if (impl)
    {
        if (uwp_has_text( impl ))
            txt = uwp_elem_text( impl );
        else if (uwp_elem_selected_item( impl ))
        {
            uwp_combobox_get_item_label( impl, uwp_elem_selected_item( impl ), buf, ARRAY_SIZE(buf) );
            txt = buf;
        }
    }
    return WindowsCreateString( txt, wcslen(txt), out );
}
static HRESULT WINAPI uwp_cb6_put_Text( void *iface, HSTRING val )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, ISplitView2_iface );
    const WCHAR *s = val ? WindowsGetStringRawBuffer( val, NULL ) : L"";
    if (impl && s)
    {
        uwp_elem_set_text( impl, s );
        uwp_invalidate_window();
    }
    return S_OK;
}
static HRESULT WINAPI uwp_cb6_get_NullOut( void *iface, void **out )
{
    if (out) *out = NULL;
    return S_OK;
}
static HRESULT WINAPI uwp_cb6_add_event( void *iface, void *handler, EventRegistrationToken *tok )
{
    if (tok) tok->value = 1;
    return S_OK;
}
const void *uwp_combobox6_vtbl[24] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_cb6_get_bool_false,
    [7] = uwp_cb6_get_Text,
    [8] = uwp_cb6_put_Text,
    [9] = uwp_cb6_get_NullOut,
    [10] = uwp_generic_noop,
    [11] = uwp_cb6_get_NullOut,
    [12] = uwp_generic_noop,
    [13] = uwp_cb6_add_event,
    [14] = uwp_generic_noop,
    [15 ... 23] = uwp_generic_noop,
};

/* Windows.UI.Xaml.Controls.IPivot ({103e9b13-3400-4a16-90b9-6912bf06974e}) */
static HRESULT WINAPI uwp_pivot_get_NullOut( void *iface, void **out )
{
    if (out) *out = NULL;
    return S_OK;
}
static HRESULT WINAPI uwp_pivot_get_SelectedIndex( void *iface, INT32 *out )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IComboBox_iface );
    INT32 idx = uwp_elem_selected_index( impl );
    if (idx < 0 && impl && impl->children_coll)
    {
        struct uwp_vec_view *v = CONTAINING_RECORD( impl->children_coll, struct uwp_vec_view, IInspectable_iface );
        if (v && v->count > 0) idx = 0;
    }
    if (out) *out = idx;
    return S_OK;
}
static HRESULT WINAPI uwp_pivot_put_SelectedIndex( void *iface, INT32 idx )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IComboBox_iface );
    if (impl)
    {
        uwp_selector_set_selection( impl, idx, NULL, TRUE, TRUE );
        g_layout_dirty = TRUE;
        uwp_invalidate_window();
    }
    return S_OK;
}
static HRESULT WINAPI uwp_pivot_get_SelectedItem( void *iface, void **out )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IComboBox_iface );
    IInspectable *item;
    if (!out) return E_POINTER;
    item = uwp_elem_selected_item( impl );
    if (!item && impl && impl->children_coll)
    {
        struct uwp_vec_view *v = CONTAINING_RECORD( impl->children_coll, struct uwp_vec_view, IInspectable_iface );
        INT32 idx = uwp_elem_selected_index( impl );
        if (idx < 0) idx = 0;
        if (v && (unsigned int)idx < v->count) item = v->items[idx];
    }
    *out = item;
    if (*out) IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}
static HRESULT WINAPI uwp_pivot_put_SelectedItem( void *iface, void *item )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IComboBox_iface );
    if (impl)
    {
        uwp_selector_set_selection( impl, -1, (IInspectable *)item, FALSE, TRUE );
        g_layout_dirty = TRUE;
        uwp_invalidate_window();
    }
    return S_OK;
}
static HRESULT WINAPI uwp_pivot_get_IsLocked( void *iface, boolean *out )
{
    if (out) *out = FALSE;
    return S_OK;
}
static HRESULT WINAPI uwp_pivot_add_SelectionChanged( void *iface, void *handler, EventRegistrationToken *tok )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IComboBox_iface );
    struct uwp_event_bag *ev;
    if (!impl || !handler) { if (tok) tok->value = 1; return S_OK; }
    ev = uwp_elem_ensure_events( impl );
    if (!ev) return E_OUTOFMEMORY;
    TRACE( "IPivot::add_SelectionChanged(%p) on %s\n", handler, debugstr_w(impl->name) );
    return xaml_event_registry_add( &ev->sel_changed_reg, handler, tok );
}
static HRESULT WINAPI uwp_pivot_remove_SelectionChanged( void *iface, EventRegistrationToken tok )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IComboBox_iface );
    if (!impl || !impl->events) return S_OK;
    return xaml_event_registry_remove( &impl->events->sel_changed_reg, tok );
}
static HRESULT WINAPI uwp_pivot_add_PivotItemLoading( void *iface, void *handler, EventRegistrationToken *tok )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IComboBox_iface );
    struct uwp_event_bag *ev;
    if (!impl || !handler) { if (tok) tok->value = 1; return S_OK; }
    ev = uwp_elem_ensure_events( impl );
    if (!ev) return E_OUTOFMEMORY;
    return xaml_event_registry_add( &ev->pivot_item_loading_reg, handler, tok );
}
static HRESULT WINAPI uwp_pivot_remove_PivotItemLoading( void *iface, EventRegistrationToken tok )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IComboBox_iface );
    if (!impl || !impl->events) return S_OK;
    return xaml_event_registry_remove( &impl->events->pivot_item_loading_reg, tok );
}
static HRESULT WINAPI uwp_pivot_add_PivotItemLoaded( void *iface, void *handler, EventRegistrationToken *tok )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IComboBox_iface );
    struct uwp_event_bag *ev;
    if (!impl || !handler) { if (tok) tok->value = 1; return S_OK; }
    ev = uwp_elem_ensure_events( impl );
    if (!ev) return E_OUTOFMEMORY;
    return xaml_event_registry_add( &ev->pivot_item_loaded_reg, handler, tok );
}
static HRESULT WINAPI uwp_pivot_remove_PivotItemLoaded( void *iface, EventRegistrationToken tok )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IComboBox_iface );
    if (!impl || !impl->events) return S_OK;
    return xaml_event_registry_remove( &impl->events->pivot_item_loaded_reg, tok );
}
const void *uwp_pivot_vtbl[32] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_pivot_get_NullOut,             /* get_Title */
    [7] = uwp_generic_noop,                  /* put_Title */
    [8] = uwp_pivot_get_NullOut,             /* get_TitleTemplate */
    [9] = uwp_generic_noop,                  /* put_TitleTemplate */
    [10] = uwp_pivot_get_NullOut,            /* get_HeaderTemplate */
    [11] = uwp_generic_noop,                 /* put_HeaderTemplate */
    [12] = uwp_pivot_get_SelectedIndex,      /* get_SelectedIndex */
    [13] = uwp_pivot_put_SelectedIndex,      /* put_SelectedIndex */
    [14] = uwp_pivot_get_SelectedItem,       /* get_SelectedItem */
    [15] = uwp_pivot_put_SelectedItem,       /* put_SelectedItem */
    [16] = uwp_pivot_get_IsLocked,           /* get_IsLocked */
    [17] = uwp_generic_noop,                 /* put_IsLocked */
    [18] = uwp_pivot_add_SelectionChanged,   /* add_SelectionChanged */
    [19] = uwp_pivot_remove_SelectionChanged,/* remove_SelectionChanged */
    [20] = uwp_pivot_add_PivotItemLoading,   /* add_PivotItemLoading */
    [21] = uwp_pivot_remove_PivotItemLoading,/* remove_PivotItemLoading */
    [22] = uwp_pivot_add_PivotItemLoaded,    /* add_PivotItemLoaded */
    [23] = uwp_pivot_remove_PivotItemLoaded, /* remove_PivotItemLoaded */
    [24] = uwp_cb6_add_event,                /* add_PivotItemUnloading */
    [25] = uwp_generic_noop,                 /* remove_PivotItemUnloading */
    [26] = uwp_cb6_add_event,                /* add_PivotItemUnloaded */
    [27] = uwp_generic_noop,                 /* remove_PivotItemUnloaded */
    [28 ... 31] = uwp_generic_noop,
};

/* Windows.UI.Xaml.Controls.IPivotItem ({2543d0bc-2620-4361-8272-d06443d89362}) */
static HRESULT WINAPI uwp_pivot_item_get_Header( void *iface, void **out )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IComboBox_iface );
    if (!out) return E_POINTER;
    if (impl && uwp_has_text( impl ))
    {
        *out = box_string_value( uwp_elem_text( impl ) );
        return S_OK;
    }
    *out = NULL;
    return S_OK;
}
static HRESULT WINAPI uwp_pivot_item_put_Header( void *iface, void *val )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IComboBox_iface );
    WCHAR buf[256] = {0};
    if (impl && val && uwp_unbox_string( (IInspectable *)val, buf, ARRAY_SIZE(buf) ))
    {
        uwp_elem_set_text( impl, buf );
        uwp_invalidate_window();
    }
    return S_OK;
}
const void *uwp_pivot_item_vtbl[12] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_pivot_item_get_Header,
    [7] = uwp_pivot_item_put_Header,
    [8 ... 11] = uwp_generic_noop,
};

/* ============================================================================
 * Windows.UI.Xaml.Controls.CalendarDatePicker ({63c9c16f-668c-4491-9444-d45d8bf4fa29})
 * ============================================================================ */

static IInspectable *uwp_box_datetime_ref( INT64 ticks )
{
    static const GUID IID_IPropertyValueStatics_local =
        { 0x629bdbc8, 0xd932, 0x4ff4, { 0x96, 0xb9, 0x8d, 0x96, 0xc5, 0xc1, 0xe8, 0x58 } };
    HSTRING h_cls = NULL;
    void *pvs = NULL;
    IInspectable *res = NULL;

    if (SUCCEEDED(WindowsCreateString( L"Windows.Foundation.PropertyValue", 32, &h_cls )) && h_cls)
    {
        if (SUCCEEDED(RoGetActivationFactory( h_cls, &IID_IPropertyValueStatics_local, &pvs )) && pvs)
        {
            const struct abi_property_value_statics *pv_statics = (const struct abi_property_value_statics *)pvs;
            if (pv_statics->lpVtbl->CreateDateTime)
                pv_statics->lpVtbl->CreateDateTime( pvs, ticks, &res );
            IUnknown_Release( (IUnknown *)pvs );
        }
        WindowsDeleteString( h_cls );
    }
    return res;
}

static BOOL uwp_unbox_datetime_ref( IInspectable *val, INT64 *out_ticks )
{
    void *iref = NULL;
    if (!val || !out_ticks) return FALSE;
    if (SUCCEEDED(IInspectable_QueryInterface( val, &IID_IReference_DateTime_UWP, &iref )) && iref)
    {
        const struct abi_iref_int64 *ir = (const struct abi_iref_int64 *)iref;
        HRESULT hr = ir->lpVtbl->get_Value ? ir->lpVtbl->get_Value( iref, out_ticks ) : E_FAIL;
        IUnknown_Release( (IUnknown *)iref );
        return SUCCEEDED(hr);
    }
    {
        static const GUID IID_IPropertyValue_local =
            { 0x4bd682dd, 0x7554, 0x40e9, { 0x9a, 0x9b, 0x82, 0x65, 0x4e, 0xde, 0x7e, 0x62 } };
        void *pv = NULL;
        if (SUCCEEDED(IInspectable_QueryInterface( val, &IID_IPropertyValue_local, &pv )) && pv)
        {
            const struct abi_property_value *apv = (const struct abi_property_value *)pv;
            HRESULT hr = apv->lpVtbl->GetDateTime ? apv->lpVtbl->GetDateTime( pv, out_ticks ) : E_FAIL;
            IUnknown_Release( (IUnknown *)pv );
            return SUCCEEDED(hr);
        }
    }
    return FALSE;
}

static INT64 uwp_cdp_today_ticks( void )
{
    SYSTEMTIME st;
    FILETIME ft;
    GetLocalTime( &st );
    st.wHour = 12; st.wMinute = 0; st.wSecond = 0; st.wMilliseconds = 0;
    if (SystemTimeToFileTime( &st, &ft ))
        return ((INT64)ft.dwHighDateTime << 32) | (INT64)ft.dwLowDateTime;
    return 134139168000000000LL; /* 2026-11-26 12:00 UTC fallback */
}

void uwp_cdp_ticks_to_ymd( INT64 ticks, int *y, int *m, int *d )
{
    FILETIME ft;
    SYSTEMTIME st;
    if (ticks <= 0) ticks = uwp_cdp_today_ticks();
    ft.dwLowDateTime = (DWORD)(ticks & 0xffffffff);
    ft.dwHighDateTime = (DWORD)((UINT64)ticks >> 32);
    if (FileTimeToSystemTime( &ft, &st ))
    {
        if (y) *y = st.wYear;
        if (m) *m = st.wMonth;
        if (d) *d = st.wDay;
        return;
    }
    if (y) *y = 2026;
    if (m) *m = 11;
    if (d) *d = 26;
}

INT64 uwp_cdp_ymd_to_ticks( int y, int m, int d )
{
    SYSTEMTIME st = {0};
    FILETIME ft = {0};
    if (y < 1601) y = 1601;
    if (y > 9999) y = 9999;
    if (m < 1) m = 1;
    if (m > 12) m = 12;
    if (d < 1) d = 1;
    if (d > uwp_cdp_days_in_month( y, m )) d = uwp_cdp_days_in_month( y, m );
    st.wYear = (WORD)y;
    st.wMonth = (WORD)m;
    st.wDay = (WORD)d;
    st.wHour = 12;
    if (SystemTimeToFileTime( &st, &ft ))
        return ((INT64)ft.dwHighDateTime << 32) | (INT64)ft.dwLowDateTime;
    return uwp_cdp_today_ticks();
}

int uwp_cdp_days_in_month( int y, int m )
{
    static const int days[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (m < 1 || m > 12) return 30;
    if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || (y % 400 == 0))) return 29;
    return days[m - 1];
}

int uwp_cdp_first_dow( int y, int m )
{
    SYSTEMTIME st = {0};
    FILETIME ft = {0};
    st.wYear = (WORD)max( 1601, y );
    st.wMonth = (WORD)max( 1, min( 12, m ) );
    st.wDay = 1;
    st.wHour = 12;
    if (SystemTimeToFileTime( &st, &ft ) && FileTimeToSystemTime( &ft, &st ))
        return st.wDayOfWeek; /* 0=Sun..6=Sat */
    return 0;
}

void uwp_cdp_format_date( struct uwp_element *cdp, WCHAR *buf, int buf_len )
{
    static const WCHAR * const months[12] = {
        L"January", L"February", L"March", L"April", L"May", L"June",
        L"July", L"August", L"September", L"October", L"November", L"December"
    };
    struct uwp_nav_ext *ne = cdp ? cdp->nav : NULL;
    int y, m, d;
    if (!buf || buf_len <= 0) return;
    buf[0] = 0;
    if (!ne || !ne->cdp_has_date)
    {
        if (ne && ne->cdp_placeholder[0])
            lstrcpynW( buf, ne->cdp_placeholder, buf_len );
        else if (uwp_has_text( cdp ))
            lstrcpynW( buf, uwp_elem_text( cdp ), buf_len );
        else
            lstrcpynW( buf, L"Select a date", buf_len );
        return;
    }
    uwp_cdp_ticks_to_ymd( ne->cdp_date_ticks, &y, &m, &d );
    if (m >= 1 && m <= 12)
        swprintf( buf, buf_len, L"%s %d, %d", months[m - 1], d, y );
    else
        swprintf( buf, buf_len, L"%d/%d/%d", m, d, y );
}

BOOL uwp_cdp_compute_popup_rect( struct uwp_element *cdp, int win_w, int win_h, RECT *out_rc )
{
    float sc = (g_layout_scale > 0.25f) ? g_layout_scale : 1.0f;
    int pw = (int)(280 * sc + 0.5f);
    int ph = (int)(284 * sc + 0.5f);
    int left, top;
    if (!cdp || !cdp->has_bounds || !out_rc) return FALSE;
    left = cdp->bounds.left;
    if (left + pw > win_w - 8) left = max( 8, win_w - 8 - pw );
    top = cdp->bounds.bottom + 4;
    if (top + ph > win_h - 8) top = max( 8, cdp->bounds.top - ph - 4 );
    SetRect( out_rc, left, top, left + pw, top + ph );
    return TRUE;
}

struct uwp_cdp_args
{
    IInspectable IInspectable_iface;
    LONG ref;
    IInspectable *new_date;
    IInspectable *old_date;
};

static HRESULT WINAPI uwp_cdp_args_QI( void *iface, REFIID iid, void **out )
{
    struct uwp_cdp_args *a = iface;
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_ICalendarDatePickerDateChangedEventArgs_UWP ))
    {
        *out = iface;
        InterlockedIncrement( &a->ref );
        return S_OK;
    }
    TRACE( "uwp_cdp_args_QI: unmatched iid %s -> E_NOINTERFACE\n", debugstr_guid(iid) );
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI uwp_cdp_args_AddRef( void *iface )
{
    return InterlockedIncrement( &((struct uwp_cdp_args *)iface)->ref );
}
static ULONG WINAPI uwp_cdp_args_Release( void *iface )
{
    struct uwp_cdp_args *a = iface;
    ULONG r = InterlockedDecrement( &a->ref );
    if (!r)
    {
        if (a->new_date) IInspectable_Release( a->new_date );
        if (a->old_date) IInspectable_Release( a->old_date );
        free( a );
    }
    return r;
}
static HRESULT WINAPI uwp_cdp_args_GetRuntimeClassName( void *iface, HSTRING *name )
{
    static const WCHAR cls[] = L"Windows.UI.Xaml.Controls.CalendarDatePickerDateChangedEventArgs";
    return WindowsCreateString( cls, ARRAY_SIZE(cls) - 1, name );
}
static HRESULT WINAPI uwp_cdp_args_get_NewDate( void *iface, void **out )
{
    struct uwp_cdp_args *a = iface;
    if (!out) return E_POINTER;
    *out = a->new_date;
    if (*out) IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}
static HRESULT WINAPI uwp_cdp_args_get_OldDate( void *iface, void **out )
{
    struct uwp_cdp_args *a = iface;
    if (!out) return E_POINTER;
    *out = a->old_date;
    if (*out) IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}
static const void *uwp_cdp_args_vtbl[12] =
{
    [0] = uwp_cdp_args_QI,
    [1] = uwp_cdp_args_AddRef,
    [2] = uwp_cdp_args_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_cdp_args_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_cdp_args_get_NewDate,
    [7] = uwp_cdp_args_get_OldDate,
    [8 ... 11] = uwp_generic_noop,
};

void uwp_cdp_set_open( struct uwp_element *cdp, BOOL open )
{
    struct uwp_nav_ext *ne;
    if (!cdp) return;
    if (open && g_open_cdp && g_open_cdp != cdp)
    {
        struct uwp_element *prev = g_open_cdp;
        prev->is_dropdown_open = FALSE;
        g_open_cdp = NULL;
        uwp_fire_cb_event( prev, UWP_EV_REG( prev, cb_closed_reg ) );
    }
    if (cdp->is_dropdown_open == open) return;
    cdp->is_dropdown_open = open;
    ne = uwp_elem_ensure_nav( cdp );
    if (open)
    {
        if (ne)
        {
            INT64 ref_ticks = ne->cdp_has_date ? ne->cdp_date_ticks : uwp_cdp_today_ticks();
            int y, m, d;
            uwp_cdp_ticks_to_ymd( ref_ticks, &y, &m, &d );
            if (ne->cdp_view_year <= 0 || ne->cdp_view_month <= 0)
            {
                ne->cdp_view_year = y;
                ne->cdp_view_month = m;
            }
        }
        g_open_cdp = cdp;
        uwp_fire_cb_event( cdp, UWP_EV_REG( cdp, cb_opened_reg ) );
    }
    else
    {
        if (g_open_cdp == cdp) g_open_cdp = NULL;
        uwp_fire_cb_event( cdp, UWP_EV_REG( cdp, cb_closed_reg ) );
    }
    TRACE( "uwp_cdp_set_open(%s) -> %d\n", debugstr_w(cdp->name), (int)open );
    uwp_invalidate_window();
}

void uwp_cdp_set_date( struct uwp_element *cdp, INT64 ticks, BOOL has_date, BOOL fire_event )
{
    struct uwp_nav_ext *ne;
    BOOL old_has;
    INT64 old_ticks;
    WCHAR fmt[96] = {0};

    if (!cdp) return;
    ne = uwp_elem_ensure_nav( cdp );
    if (!ne) return;
    old_has = ne->cdp_has_date;
    old_ticks = ne->cdp_date_ticks;
    ne->cdp_has_date = has_date;
    ne->cdp_date_ticks = has_date ? ticks : 0;
    if (has_date)
    {
        int y, m, d;
        uwp_cdp_ticks_to_ymd( ticks, &y, &m, &d );
        ne->cdp_view_year = y;
        ne->cdp_view_month = m;
    }
    uwp_cdp_format_date( cdp, fmt, ARRAY_SIZE(fmt) );
    uwp_elem_set_text( cdp, fmt );
    TRACE( "uwp_cdp_set_date(%s): has=%d ticks=%I64d -> %s (fire=%d)\n",
           debugstr_w(cdp->name), (int)has_date, ticks, debugstr_w(fmt), (int)fire_event );

    if (fire_event && (old_has != has_date || old_ticks != ne->cdp_date_ticks) &&
        UWP_EV_CNT( cdp, cdp_date_changed_reg ) > 0)
    {
        struct uwp_cdp_args *args = calloc( 1, sizeof(*args) );
        if (args)
        {
            IInspectable *sender = cdp->outer ? cdp->outer : &cdp->IInspectable_iface;
            args->IInspectable_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_cdp_args_vtbl;
            args->ref = 1;
            args->new_date = has_date ? uwp_box_datetime_ref( ticks ) : NULL;
            args->old_date = old_has ? uwp_box_datetime_ref( old_ticks ) : NULL;
            uwp_fire_event_registry( &cdp->events->cdp_date_changed_reg, sender,
                                     &args->IInspectable_iface, "CalendarDatePicker::DateChanged" );
            IInspectable_Release( &args->IInspectable_iface );
        }
    }
    uwp_invalidate_window();
}

static HRESULT WINAPI uwp_cdp_get_Date( void *iface, void **out )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IComboBox_iface );
    if (!out) return E_POINTER;
    if (impl && impl->nav && impl->nav->cdp_has_date)
        *out = uwp_box_datetime_ref( impl->nav->cdp_date_ticks );
    else
        *out = NULL;
    return S_OK;
}
static HRESULT WINAPI uwp_cdp_put_Date( void *iface, void *val )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IComboBox_iface );
    INT64 ticks = 0;
    if (!impl) return S_OK;
    if (!val)
        uwp_cdp_set_date( impl, 0, FALSE, TRUE );
    else if (uwp_unbox_datetime_ref( (IInspectable *)val, &ticks ))
        uwp_cdp_set_date( impl, ticks, TRUE, TRUE );
    return S_OK;
}
static HRESULT WINAPI uwp_cdp_get_IsCalendarOpen( void *iface, boolean *val )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IComboBox_iface );
    if (val) *val = impl ? (boolean)impl->is_dropdown_open : FALSE;
    return S_OK;
}
static HRESULT WINAPI uwp_cdp_put_IsCalendarOpen( void *iface, boolean val )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IComboBox_iface );
    if (impl) uwp_cdp_set_open( impl, val != 0 );
    return S_OK;
}
static HRESULT WINAPI uwp_cdp_get_DateFormat( void *iface, HSTRING *out )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IComboBox_iface );
    const WCHAR *s = (impl && impl->nav) ? impl->nav->cdp_date_format : L"";
    return out ? WindowsCreateString( s, wcslen(s), out ) : E_POINTER;
}
static HRESULT WINAPI uwp_cdp_put_DateFormat( void *iface, HSTRING val )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IComboBox_iface );
    struct uwp_nav_ext *ne = uwp_elem_ensure_nav( impl );
    const WCHAR *s = val ? WindowsGetStringRawBuffer( val, NULL ) : L"";
    if (ne && s) lstrcpynW( ne->cdp_date_format, s, ARRAY_SIZE(ne->cdp_date_format) );
    return S_OK;
}
static HRESULT WINAPI uwp_cdp_get_PlaceholderText( void *iface, HSTRING *out )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IComboBox_iface );
    const WCHAR *s = (impl && impl->nav) ? impl->nav->cdp_placeholder : L"";
    return out ? WindowsCreateString( s, wcslen(s), out ) : E_POINTER;
}
static HRESULT WINAPI uwp_cdp_put_PlaceholderText( void *iface, HSTRING val )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IComboBox_iface );
    struct uwp_nav_ext *ne = uwp_elem_ensure_nav( impl );
    const WCHAR *s = val ? WindowsGetStringRawBuffer( val, NULL ) : L"";
    if (ne && s)
    {
        lstrcpynW( ne->cdp_placeholder, s, ARRAY_SIZE(ne->cdp_placeholder) );
        if (!ne->cdp_has_date && s[0]) uwp_elem_set_text( impl, s );
        uwp_invalidate_window();
    }
    return S_OK;
}
static HRESULT WINAPI uwp_cdp_get_Header( void *iface, void **out )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IComboBox_iface );
    if (!out) return E_POINTER;
    if (impl && impl->nav && impl->nav->cdp_header[0])
        *out = box_string_value( impl->nav->cdp_header );
    else
        *out = NULL;
    return S_OK;
}
static HRESULT WINAPI uwp_cdp_put_Header( void *iface, void *val )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IComboBox_iface );
    struct uwp_nav_ext *ne = uwp_elem_ensure_nav( impl );
    WCHAR buf[128] = {0};
    if (ne && val && uwp_unbox_string( (IInspectable *)val, buf, ARRAY_SIZE(buf) ))
    {
        lstrcpynW( ne->cdp_header, buf, ARRAY_SIZE(ne->cdp_header) );
        g_layout_dirty = TRUE;
        uwp_invalidate_window();
    }
    return S_OK;
}
static HRESULT WINAPI uwp_cdp_get_MinDate( void *iface, INT64 *out )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IComboBox_iface );
    if (out) *out = (impl && impl->nav && impl->nav->cdp_min_ticks) ? impl->nav->cdp_min_ticks : 0;
    return S_OK;
}
static HRESULT WINAPI uwp_cdp_put_MinDate( void *iface, INT64 val )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IComboBox_iface );
    struct uwp_nav_ext *ne = uwp_elem_ensure_nav( impl );
    if (ne) ne->cdp_min_ticks = val;
    return S_OK;
}
static HRESULT WINAPI uwp_cdp_get_MaxDate( void *iface, INT64 *out )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IComboBox_iface );
    if (out) *out = (impl && impl->nav && impl->nav->cdp_max_ticks) ? impl->nav->cdp_max_ticks : 2650467743990000000LL;
    return S_OK;
}
static HRESULT WINAPI uwp_cdp_put_MaxDate( void *iface, INT64 val )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IComboBox_iface );
    struct uwp_nav_ext *ne = uwp_elem_ensure_nav( impl );
    if (ne) ne->cdp_max_ticks = val;
    return S_OK;
}
static HRESULT WINAPI uwp_cdp_get_BoolTrue( void *iface, boolean *val )
{
    if (val) *val = TRUE;
    return S_OK;
}
static HRESULT WINAPI uwp_cdp_get_IntZero( void *iface, INT32 *val )
{
    if (val) *val = 0;
    return S_OK;
}
static HRESULT WINAPI uwp_cdp_get_CalendarId( void *iface, HSTRING *out )
{
    return out ? WindowsCreateString( L"GregorianCalendar", 17, out ) : E_POINTER;
}
static HRESULT WINAPI uwp_cdp_get_EmptyStr( void *iface, HSTRING *out )
{
    return out ? WindowsCreateString( L"", 0, out ) : E_POINTER;
}
static HRESULT WINAPI uwp_cdp_add_DateChanged( void *iface, void *handler, EventRegistrationToken *tok )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IComboBox_iface );
    struct uwp_event_bag *ev;
    if (!impl || !handler) { if (tok) tok->value = 1; return S_OK; }
    ev = uwp_elem_ensure_events( impl );
    if (!ev) return E_OUTOFMEMORY;
    TRACE( "ICalendarDatePicker::add_DateChanged(%p) on %s\n", handler, debugstr_w(impl->name) );
    return xaml_event_registry_add( &ev->cdp_date_changed_reg, handler, tok );
}
static HRESULT WINAPI uwp_cdp_remove_DateChanged( void *iface, EventRegistrationToken tok )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IComboBox_iface );
    if (!impl || !impl->events) return S_OK;
    return xaml_event_registry_remove( &impl->events->cdp_date_changed_reg, tok );
}
static HRESULT WINAPI uwp_cdp_SetDisplayDate( void *iface, INT64 ticks )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IComboBox_iface );
    struct uwp_nav_ext *ne = uwp_elem_ensure_nav( impl );
    if (ne && ticks > 0)
    {
        int y, m, d;
        uwp_cdp_ticks_to_ymd( ticks, &y, &m, &d );
        ne->cdp_view_year = y;
        ne->cdp_view_month = m;
    }
    return S_OK;
}
#define CDP_SLOT_PROBE(N) \
static HRESULT WINAPI uwp_cdp_slot_##N( void *iface, void *a1, void *a2 ) \
{ \
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IComboBox_iface ); \
    TRACE( "ICalendarDatePicker::slot[%d] on %s a1=%p a2=%p\n", N, impl ? debugstr_w(impl->name) : "?", a1, a2 ); \
    if ((N) == 48 && impl && a1) \
    { \
        struct uwp_event_bag *ev = uwp_elem_ensure_events( impl ); \
        if (ev) return xaml_event_registry_add( &ev->cdp_date_changed_reg, a1, (EventRegistrationToken *)a2 ); \
    } \
    return S_OK; \
}
CDP_SLOT_PROBE(48)
CDP_SLOT_PROBE(49)
CDP_SLOT_PROBE(50)
CDP_SLOT_PROBE(51)
CDP_SLOT_PROBE(52)
CDP_SLOT_PROBE(53)
CDP_SLOT_PROBE(54)
CDP_SLOT_PROBE(55)
CDP_SLOT_PROBE(56)
CDP_SLOT_PROBE(57)
CDP_SLOT_PROBE(58)
CDP_SLOT_PROBE(59)
CDP_SLOT_PROBE(60)
CDP_SLOT_PROBE(61)
CDP_SLOT_PROBE(62)
CDP_SLOT_PROBE(63)

const void *uwp_cdp_vtbl[64] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_cdp_get_Date,
    [7] = uwp_cdp_put_Date,
    [8] = uwp_cdp_get_IsCalendarOpen,
    [9] = uwp_cdp_put_IsCalendarOpen,
    [10] = uwp_cdp_get_DateFormat,
    [11] = uwp_cdp_put_DateFormat,
    [12] = uwp_cdp_get_PlaceholderText,
    [13] = uwp_cdp_put_PlaceholderText,
    [14] = uwp_cdp_get_Header,
    [15] = uwp_cdp_put_Header,
    [16] = uwp_pivot_get_NullOut,        /* get_HeaderTemplate */
    [17] = uwp_generic_noop,             /* put_HeaderTemplate */
    [18] = uwp_pivot_get_NullOut,        /* get_CalendarViewStyle */
    [19] = uwp_generic_noop,             /* put_CalendarViewStyle */
    [20] = uwp_cdp_get_MinDate,
    [21] = uwp_cdp_put_MinDate,
    [22] = uwp_cdp_get_MaxDate,
    [23] = uwp_cdp_put_MaxDate,
    [24] = uwp_cdp_get_BoolTrue,         /* get_IsTodayHighlighted */
    [25] = uwp_generic_noop,             /* put_IsTodayHighlighted */
    [26] = uwp_cdp_get_IntZero,          /* get_DisplayMode */
    [27] = uwp_generic_noop,             /* put_DisplayMode */
    [28] = uwp_cdp_get_IntZero,          /* get_FirstDayOfWeek */
    [29] = uwp_generic_noop,             /* put_FirstDayOfWeek */
    [30] = uwp_cdp_get_EmptyStr,         /* get_DayOfWeekFormat */
    [31] = uwp_generic_noop,             /* put_DayOfWeekFormat */
    [32] = uwp_cdp_get_CalendarId,       /* get_CalendarIdentifier */
    [33] = uwp_generic_noop,             /* put_CalendarIdentifier */
    [34] = uwp_cdp_get_BoolTrue,         /* get_IsOutOfScopeEnabled */
    [35] = uwp_generic_noop,             /* put_IsOutOfScopeEnabled */
    [36] = uwp_cdp_get_BoolTrue,         /* get_IsGroupLabelVisible */
    [37] = uwp_generic_noop,             /* put_IsGroupLabelVisible */
    [38] = uwp_cb6_add_event,            /* add_CalendarViewDayItemChanging */
    [39] = uwp_generic_noop,             /* remove_CalendarViewDayItemChanging */
    [40] = uwp_cdp_add_DateChanged,
    [41] = uwp_cdp_remove_DateChanged,
    [42] = uwp_cb_add_DropDownOpened,    /* add_Opened */
    [43] = uwp_cb_remove_DropDownOpened, /* remove_Opened */
    [44] = uwp_cb_add_DropDownClosed,    /* add_Closed */
    [45] = uwp_cb_remove_DropDownClosed, /* remove_Closed */
    [46] = uwp_cdp_SetDisplayDate,
    [47] = uwp_generic_noop,             /* SetYearDecadeDisplayDimensions */
    [48] = uwp_cdp_slot_48,
    [49] = uwp_cdp_slot_49,
    [50] = uwp_cdp_slot_50,
    [51] = uwp_cdp_slot_51,
    [52] = uwp_cdp_slot_52,
    [53] = uwp_cdp_slot_53,
    [54] = uwp_cdp_slot_54,
    [55] = uwp_cdp_slot_55,
    [56] = uwp_cdp_slot_56,
    [57] = uwp_cdp_slot_57,
    [58] = uwp_cdp_slot_58,
    [59] = uwp_cdp_slot_59,
    [60] = uwp_cdp_slot_60,
    [61] = uwp_cdp_slot_61,
    [62] = uwp_cdp_slot_62,
    [63] = uwp_cdp_slot_63,
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
        IInspectable *sel = (!(impl->class_flags & UWP_CF_ITEMS_REPEATER))
                                ? (impl->item_template_selector ? impl->item_template_selector : impl->item_template) : NULL;
        if (sel)
        {
            void *dts = NULL;
            if (SUCCEEDED(IInspectable_QueryInterface( sel, &IID_IDataTemplateSelector_UWP, &dts )) && dts)
            {
                const struct abi_data_template_selector *sdts = (const struct abi_data_template_selector *)dts;
                __TRY { if (sdts->lpVtbl->SelectTemplate) sdts->lpVtbl->SelectTemplate( dts, item, &impl->IDependencyObject_iface, &tmpl ); }
                __EXCEPT_ALL { tmpl = NULL; }
                __ENDTRY;
                IUnknown_Release( (IUnknown *)dts );
            }
            if (!tmpl && SUCCEEDED(IInspectable_QueryInterface( sel, &IID_IDataTemplateSelector2_UWP, &dts )) && dts)
            {
                const struct abi_data_template_selector2 *sdts2 = (const struct abi_data_template_selector2 *)dts;
                __TRY { if (sdts2->lpVtbl->SelectTemplateForItem) sdts2->lpVtbl->SelectTemplateForItem( dts, item, &tmpl ); }
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
                const struct abi_data_template *adt = (const struct abi_data_template *)dt;
                void *content = NULL;
                if (adt->lpVtbl->LoadContent && SUCCEEDED(adt->lpVtbl->LoadContent( dt, &content )) && content)
                {
                    struct uwp_element *cel = uwp_elem_from_any( content );
                    if (cel)
                    {
                        cel->parent = impl;
                        uwp_vec_Append( impl->children_coll, &cel->IInspectable_iface );
                        uwp_fe_put_DataContext( &cel->IInspectable_iface, item );
                        if (cel->outer && !cel->template_applied)
                            uwp_apply_template_force( cel );
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
                bel->probe_flags = 0;
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

struct uwp_obsvec_cb
{
    const void *vtbl;
    LONG ref;
    IWeakReference *weak_ctrl;
};

static HRESULT WINAPI obsvec_cb_QI( void *iface, REFIID iid, void **out )
{
    static const GUID IID_BindableVectorChangedHandler_UWP =
        { 0x624cd4e1, 0xd007, 0x43b1, { 0x9c, 0x03, 0xaf, 0x4d, 0x3e, 0x62, 0x58, 0xc4 } };
    static const GUID IID_NotifyCollectionChangedHandler_UWP =
        { 0xca10b37c, 0xf382, 0x4591, { 0x85, 0x57, 0x5e, 0x24, 0x96, 0x52, 0x79, 0xb0 } };
    static const GUID IID_VectorChangedHandler_Obj =
        { 0xb423a801, 0xd35e, 0x56b9, { 0x81, 0x3b, 0x00, 0x88, 0x95, 0x36, 0xcb, 0x98 } };
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_BindableVectorChangedHandler_UWP ) ||
        IsEqualGUID( iid, &IID_NotifyCollectionChangedHandler_UWP ) ||
        IsEqualGUID( iid, &IID_VectorChangedHandler_Obj ))
    {
        *out = iface;
        InterlockedIncrement( &((struct uwp_obsvec_cb *)iface)->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI obsvec_cb_AddRef( void *iface )
{
    return InterlockedIncrement( &((struct uwp_obsvec_cb *)iface)->ref );
}
static ULONG WINAPI obsvec_cb_Release( void *iface )
{
    struct uwp_obsvec_cb *c = iface;
    ULONG r = InterlockedDecrement( &c->ref );
    if (!r)
    {
        if (c->weak_ctrl) IWeakReference_Release( c->weak_ctrl );
        free( c );
    }
    return r;
}
static HRESULT WINAPI obsvec_cb_Invoke( void *iface, void *sender, void *args )
{
    struct uwp_obsvec_cb *c = iface;
    IInspectable *obj = NULL;
    if (c && c->weak_ctrl && SUCCEEDED(IWeakReference_Resolve( c->weak_ctrl, &IID_IInspectable, &obj )) && obj)
    {
        struct uwp_element *ctrl = uwp_elem_from_any( obj );
        if (ctrl && ctrl->items_source && !uwp_is_combobox( ctrl ))
        {
            uwp_items_ctrl_materialize_source( ctrl, ctrl->items_source );
            g_layout_dirty = TRUE;
            uwp_invalidate_window();
        }
        IInspectable_Release( obj );
    }
    return S_OK;
}
static const void *g_obsvec_cb_vtbl[] = { obsvec_cb_QI, obsvec_cb_AddRef, obsvec_cb_Release, obsvec_cb_Invoke };

void uwp_items_ctrl_hook_source( struct uwp_element *impl, IInspectable *src )
{
    static const GUID IID_IObservableVector_Obj =
        { 0x7fd418c2, 0xe84d, 0x5217, { 0xba, 0x05, 0xcf, 0xd8, 0xb8, 0x05, 0xc3, 0xa2 } };
    struct uwp_style_anim_ext *sa;
    void *obs = NULL;
    IWeakReference *wr = NULL;
    struct uwp_obsvec_cb *cb;
    if (!impl || !src) return;
    sa = uwp_elem_ensure_style_anim( impl );
    if (!sa) return;
    if (impl->obsvec_hooked && sa->obsvec_source_obj == src) return;

    if (FAILED(IInspectable_QueryInterface( src, &IID_IBindableObservableVector_UWP, &obs )) || !obs)
        if (FAILED(IInspectable_QueryInterface( src, &IID_INotifyCollectionChanged_UWP, &obs )) || !obs)
            IInspectable_QueryInterface( src, &IID_IObservableVector_Obj, &obs );
    if (!obs) return;

    if (SUCCEEDED(impl->IWeakReferenceSource_iface.lpVtbl->GetWeakReference( &impl->IWeakReferenceSource_iface, &wr )) && wr)
    {
        cb = calloc( 1, sizeof(*cb) );
        if (cb)
        {
            const struct abi_observable_vector *ov = (const struct abi_observable_vector *)obs;
            cb->vtbl = g_obsvec_cb_vtbl;
            cb->ref = 1;
            cb->weak_ctrl = wr;
            if (ov->lpVtbl->add_VectorChanged)
            {
                EventRegistrationToken tok = {0};
                __TRY
                {
                    if (SUCCEEDED(ov->lpVtbl->add_VectorChanged( obs, cb, &tok )))
                    {
                        uwp_replace_iface( &sa->obsvec_handler_obj, (IInspectable *)cb );
                        uwp_replace_iface( &sa->obsvec_source_obj, src );
                        sa->obsvec_tok = tok;
                        impl->obsvec_hooked = 1;
                    }
                }
                __EXCEPT_ALL {}
                __ENDTRY;
            }
            obsvec_cb_Release( cb );
        }
        else
            IWeakReference_Release( wr );
    }
    IUnknown_Release( (IUnknown *)obs );
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
    {
        uwp_items_ctrl_hook_source( impl, impl->items_source );
        uwp_items_ctrl_materialize_source( impl, impl->items_source );
    }
    if (uwp_is_autosuggest( impl ))
    {
        unsigned int cnt = impl->items_source ? uwp_winrt_vec_count( impl->items_source ) : 0;
        impl->is_open = (cnt > 0 && impl == g_focused_textbox && uwp_has_text( impl ));
        impl->is_dropdown_open = impl->is_open;
        impl->hovered_dropdown_idx = -1;
        impl->dropdown_scroll_offset = 0;
    }
    if (uwp_is_combobox( impl ) && uwp_elem_selected_item( impl ))
        uwp_selector_set_selection( impl, -1, uwp_elem_selected_item( impl ), FALSE, FALSE );
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
    const WCHAR *dmp = uwp_elem_display_member_path( impl );
    if (!out) return E_POINTER;
    if (dmp[0])
        return WindowsCreateString( dmp, wcslen( dmp ), out );
    *out = NULL;
    return S_OK;
}

static HRESULT WINAPI uwp_items_ctrl_put_DisplayMemberPath( void *iface, HSTRING val )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IItemsControl_iface );
    const WCHAR *raw = val ? WindowsGetStringRawBuffer( val, NULL ) : NULL;
    struct uwp_nav_ext *nav = uwp_elem_ensure_nav( impl );
    if (nav)
        lstrcpynW( nav->display_member_path, raw ? raw : L"", ARRAY_SIZE(nav->display_member_path) );
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

static HRESULT WINAPI uwp_icm_ItemFromContainer( void *iface, void *container, void **out )
{
    struct uwp_element *cel = uwp_elem_from_any( container );
    if (!out) return E_POINTER;
    *out = NULL;
    if (cel)
    {
        *out = cel->data_context ? cel->data_context : (cel->outer ? cel->outer : &cel->IInspectable_iface);
        if (*out) IInspectable_AddRef( (IInspectable *)*out );
    }
    return S_OK;
}
static HRESULT WINAPI uwp_icm_ContainerFromItem( void *iface, void *item, void **out )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IFrame2_iface );
    if (!out) return E_POINTER;
    *out = NULL;
    if (impl && item && impl->children_coll)
    {
        struct uwp_vec_view *cv = CONTAINING_RECORD( impl->children_coll, struct uwp_vec_view, IInspectable_iface );
        unsigned int k;
        for (k = 0; k < cv->count; k++)
        {
            struct uwp_element *cel = uwp_elem_from_any( cv->items[k] );
            if (cel && (cel->data_context == (IInspectable *)item || &cel->IInspectable_iface == item ||
                        (cel->outer && cel->outer == item)))
            {
                *out = cel->outer ? cel->outer : &cel->IInspectable_iface;
                IInspectable_AddRef( (IInspectable *)*out );
                break;
            }
        }
    }
    return S_OK;
}
static HRESULT WINAPI uwp_icm_IndexFromContainer( void *iface, void *container, INT32 *out )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IFrame2_iface );
    struct uwp_element *target = uwp_elem_from_any( container );
    if (!out) return E_POINTER;
    *out = -1;
    if (impl && target && impl->children_coll)
    {
        struct uwp_vec_view *cv = CONTAINING_RECORD( impl->children_coll, struct uwp_vec_view, IInspectable_iface );
        unsigned int k;
        for (k = 0; k < cv->count; k++)
        {
            if (uwp_elem_from_any( cv->items[k] ) == target)
            {
                *out = (INT32)k;
                break;
            }
        }
    }
    return S_OK;
}
static HRESULT WINAPI uwp_icm_ContainerFromIndex( void *iface, INT32 index, void **out )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IFrame2_iface );
    if (!out) return E_POINTER;
    *out = NULL;
    if (impl && index >= 0 && impl->children_coll)
    {
        struct uwp_vec_view *cv = CONTAINING_RECORD( impl->children_coll, struct uwp_vec_view, IInspectable_iface );
        if ((unsigned int)index < cv->count)
        {
            struct uwp_element *cel = uwp_elem_from_any( cv->items[index] );
            *out = (cel && cel->outer) ? cel->outer : cv->items[index];
            if (*out) IInspectable_AddRef( (IInspectable *)*out );
        }
    }
    return S_OK;
}
const void *uwp_icm_vtbl[12] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_icm_ItemFromContainer,
    [7] = uwp_icm_ContainerFromItem,
    [8] = uwp_icm_IndexFromContainer,
    [9] = uwp_icm_ContainerFromIndex,
    [10 ... 11] = uwp_generic_noop,
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
