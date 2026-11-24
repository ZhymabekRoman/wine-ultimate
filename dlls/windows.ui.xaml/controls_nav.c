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
#include "weakreference.h"
#include "wincodec.h"
#define WIDL_using_Windows_ApplicationModel
#define WIDL_using_Windows_Management_Deployment
#include "windows.management.deployment.h"
#define WIDL_using_Windows_UI_Composition
#include "windows.ui.composition.h"
#include "uwp_private.h"
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

void uwp_nav_set_pane_open( struct uwp_element *nav, BOOL open )
{
    void *is_open_dp = NULL;
    int i;
    if (!nav) return;
    if (nav->nav_is_pane_open == open) return;
    nav->nav_is_pane_open = open;
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

static HRESULT WINAPI uwp_cd_get_NullOut( void *iface, void **out );

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
    [24 ... 43] = uwp_generic_noop,
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
        lstrcpynW( impl->text, raw, ARRAY_SIZE(impl->text) );
    return S_OK;
}

static HRESULT WINAPI uwp_flyout_slot6( void *iface, void **out )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IFlyout_iface );
    if (!impl) return E_NOINTERFACE;
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
    if (wcsstr( impl->class_name, L"MenuBarItem" ) == NULL &&
        wcsstr( impl->class_name, L"MenuFlyoutSubItem" ) == NULL &&
        wcsstr( impl->class_name, L"MenuFlyoutItem" ) == NULL)
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
    if (wcsstr( impl->class_name, L"MenuBarItem" ) != NULL ||
        wcsstr( impl->class_name, L"MenuFlyoutSubItem" ) != NULL)
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
    static const GUID IID_IVector_Insp_local =
        { 0xb32bdca4, 0x5e52, 0x5b27, { 0xbc, 0x5d, 0xd6, 0x6a, 0x1a, 0x26, 0x8c, 0x2a } };
    static const GUID IID_IVectorView_Insp_local =
        { 0xe6f2fef0, 0x0712, 0x587f, { 0x8b, 0x13, 0x48, 0x21, 0x24, 0x9a, 0x20, 0xb5 } };
    static const GUID IID_IBindableVector_local =
        { 0x393de7de, 0x6fd0, 0x4c0d, { 0xbb, 0x71, 0x47, 0x24, 0x4a, 0x11, 0x38, 0x43 } };
    static const GUID IID_IIterable_Insp_local =
        { 0x092b849b, 0x60b1, 0x52be, { 0xa4, 0x4a, 0x6f, 0xe8, 0xe9, 0x33, 0xcb, 0xe4 } };
    int count = 0;

    if (!tv || !out_items || max_items <= 0) return 0;

    if (tv->tab_items_source && (ULONG_PTR)tv->tab_items_source >= 0x10000)
    {
        void *vec = NULL;
        if (SUCCEEDED(IInspectable_QueryInterface( tv->tab_items_source, &IID_IVector_Insp_local, &vec )) ||
            SUCCEEDED(IInspectable_QueryInterface( tv->tab_items_source, &IID_IVectorView_Insp_local, &vec )) ||
            SUCCEEDED(IInspectable_QueryInterface( tv->tab_items_source, &IID_IBindableVector_local, &vec )))
        {
            HRESULT (WINAPI *get_at)(void *, unsigned int, void **) = (void *)(*(void ***)vec)[6];
            HRESULT (WINAPI *get_sz)(void *, unsigned int *) = (void *)(*(void ***)vec)[7];
            unsigned int sz = 0, i;
            if (get_sz && SUCCEEDED(get_sz( vec, &sz )))
            {
                for (i = 0; i < sz && count < max_items; i++)
                {
                    void *it = NULL;
                    if (get_at && SUCCEEDED(get_at( vec, i, &it )) && it)
                        out_items[count++] = (IInspectable *)it;
                }
            }
            IUnknown_Release( (IUnknown *)vec );
        }
        else if (SUCCEEDED(IInspectable_QueryInterface( tv->tab_items_source, &IID_IIterable_Insp_local, &vec )) && vec)
        {
            HRESULT (WINAPI *first)(void *, void **) = (void *)(*(void ***)vec)[6];
            void *iter = NULL;
            if (first && SUCCEEDED(first( vec, &iter )) && iter)
            {
                HRESULT (WINAPI *get_cur)(void *, void **) = (void *)(*(void ***)iter)[6];
                HRESULT (WINAPI *has_cur)(void *, boolean *) = (void *)(*(void ***)iter)[7];
                HRESULT (WINAPI *move_next)(void *, boolean *) = (void *)(*(void ***)iter)[8];
                boolean has = FALSE;
                while (count < max_items && has_cur && SUCCEEDED(has_cur( iter, &has )) && has)
                {
                    void *it = NULL;
                    if (get_cur && SUCCEEDED(get_cur( iter, &it )) && it)
                        out_items[count++] = (IInspectable *)it;
                    has = FALSE;
                    if (!move_next || FAILED(move_next( iter, &has )) || !has) break;
                }
                IUnknown_Release( (IUnknown *)iter );
            }
            IUnknown_Release( (IUnknown *)vec );
        }
    }

    if (count == 0 && tv->children_coll)
    {
        struct uwp_vec_view *v = CONTAINING_RECORD( tv->children_coll, struct uwp_vec_view, IInspectable_iface );
        unsigned int i;
        for (i = 0; i < v->count && count < max_items; i++)
        {
            if (v->items[i])
            {
                out_items[count] = v->items[i];
                IInspectable_AddRef( out_items[count] );
                count++;
            }
        }
    }

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
    static const GUID IID_ITabItemViewModel_local =
        { 0x609b13b7, 0x05f6, 0x5a02, { 0xb8, 0xe7, 0x5b, 0xbd, 0x2c, 0x33, 0x51, 0x8d } };
    if (!buf || buf_len <= 0) return;
    buf[0] = 0;
    if (item && (ULONG_PTR)item >= 0x10000)
    {
        void *vm = NULL;
        if (SUCCEEDED(IInspectable_QueryInterface( item, &IID_ITabItemViewModel_local, &vm )) && vm)
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
            if (el && el->text[0])
                lstrcpynW( buf, el->text, buf_len );
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

struct uwp_sel_changed_args
{
    IInspectable IInspectable_iface;
    LONG ref;
    IInspectable *removed_items;
    IInspectable *added_items;
};

static HRESULT WINAPI uwp_sel_args_QI( void *iface, REFIID iid, void **out )
{
    static const GUID IID_IRoutedEventArgs_local =
        { 0x5c985ac6, 0xd802, 0x4b38, { 0xa2, 0x23, 0xbf, 0x07, 0x0c, 0x43, 0xfe, 0xdf } };
    static const GUID IID_ISelectionChangedEventArgs_local =
        { 0xc972d2dc, 0xb609, 0x4758, { 0x85, 0x1e, 0xa7, 0x99, 0xc2, 0x1d, 0xe9, 0x7d } };
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IRoutedEventArgs_local ) ||
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
static ULONG WINAPI uwp_sel_args_Release( void *iface )
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
    return uwp_vec_create( NULL, (IInspectable **)out );
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
    return uwp_vec_create( NULL, (IInspectable **)out );
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

static struct uwp_sel_changed_args *uwp_sel_args_create( IInspectable *old_item, IInspectable *new_item )
{
    struct uwp_sel_changed_args *a = calloc( 1, sizeof(*a) );
    if (!a) return NULL;
    a->IInspectable_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_sel_args_vtbl;
    a->ref = 1;
    uwp_vec_create( NULL, &a->removed_items );
    uwp_vec_create( NULL, &a->added_items );
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

static HRESULT WINAPI uwp_flyout_add_Closed( void *iface, void *handler, EventRegistrationToken *tok )
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
    static const GUID IID_IVectorView_Obj =
        { 0xa6487363, 0xb074, 0x5c60, { 0xab, 0x16, 0x86, 0x6d, 0xce, 0x4e, 0xe5, 0x4d } };
    static const GUID IID_IVectorView_Insp =
        { 0xe6f2fef0, 0x0712, 0x587f, { 0x8b, 0x13, 0x48, 0x21, 0x24, 0x9a, 0x20, 0xb5 } };
    static const GUID IID_IVector_Obj =
        { 0xb32bdca4, 0x5e52, 0x5b27, { 0xbc, 0x5d, 0xd6, 0x6a, 0x1a, 0x26, 0x8c, 0x2a } };
    static const GUID IID_IBindableVector_sel =
        { 0x393de7de, 0x6fd0, 0x4c0d, { 0xbb, 0x71, 0x47, 0x24, 0x4a, 0x11, 0x38, 0x43 } };
    static const GUID IID_IBindableIterable_sel =
        { 0x036d2c08, 0xdf29, 0x41af, { 0x8a, 0xa2, 0xd7, 0x74, 0xbe, 0x62, 0xba, 0x6f } };
    static const GUID IID_IIterable_Insp_sel =
        { 0x092b849b, 0x60b1, 0x52be, { 0xa4, 0x4a, 0x6f, 0xe8, 0xe9, 0x33, 0xcb, 0xe4 } };
    int count = 0;

    if (!el || !out_items || max_items <= 0) return 0;

    if (el->items_source && (ULONG_PTR)el->items_source >= 0x10000)
    {
        void *vec = NULL;
        if ((SUCCEEDED(IInspectable_QueryInterface( el->items_source, &IID_IVector_Obj, &vec )) ||
             SUCCEEDED(IInspectable_QueryInterface( el->items_source, &IID_IVectorView_Insp, &vec )) ||
             SUCCEEDED(IInspectable_QueryInterface( el->items_source, &IID_IBindableVector_sel, &vec )) ||
             SUCCEEDED(IInspectable_QueryInterface( el->items_source, &IID_IVectorView_Obj, &vec ))) && vec)
        {
            HRESULT (WINAPI *get_at)(void *, UINT32, void **) = (void *)(*(void ***)vec)[6];
            HRESULT (WINAPI *get_sz)(void *, UINT32 *) = (void *)(*(void ***)vec)[7];
            UINT32 sz = 0, i;
            if (get_sz && get_at && SUCCEEDED(get_sz( vec, &sz )))
            {
                for (i = 0; i < sz && count < max_items; i++)
                {
                    void *it = NULL;
                    if (SUCCEEDED(get_at( vec, i, &it )) && it)
                        out_items[count++] = (IInspectable *)it;
                }
            }
            IUnknown_Release( (IUnknown *)vec );
        }
        else if ((SUCCEEDED(IInspectable_QueryInterface( el->items_source, &IID_IIterable_Insp_sel, &vec )) ||
                  SUCCEEDED(IInspectable_QueryInterface( el->items_source, &IID_IBindableIterable_sel, &vec ))) && vec)
        {
            HRESULT (WINAPI *first)(void *, void **) = (void *)(*(void ***)vec)[6];
            void *iter = NULL;
            if (first && SUCCEEDED(first( vec, &iter )) && iter)
            {
                HRESULT (WINAPI *get_cur)(void *, void **) = (void *)(*(void ***)iter)[6];
                HRESULT (WINAPI *has_cur)(void *, boolean *) = (void *)(*(void ***)iter)[7];
                HRESULT (WINAPI *move_next)(void *, boolean *) = (void *)(*(void ***)iter)[8];
                boolean has = FALSE;
                while (count < max_items && has_cur && SUCCEEDED(has_cur( iter, &has )) && has)
                {
                    void *it = NULL;
                    if (get_cur && SUCCEEDED(get_cur( iter, &it )) && it)
                        out_items[count++] = (IInspectable *)it;
                    has = FALSE;
                    if (!move_next || FAILED(move_next( iter, &has )) || !has) break;
                }
                IUnknown_Release( (IUnknown *)iter );
            }
            IUnknown_Release( (IUnknown *)vec );
        }
    }

    if (count == 0 && el->children_coll)
    {
        struct uwp_vec_view *v = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        unsigned int i;
        for (i = 0; i < v->count && count < max_items; i++)
        {
            if (v->items[i])
            {
                out_items[count] = v->items[i];
                IInspectable_AddRef( out_items[count] );
                count++;
            }
        }
    }
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
            if (iel->text[0])
            {
                lstrcpynW( buf, iel->text, buf_len );
                return;
            }
            if (iel->content)
            {
                struct uwp_element *cel = uwp_elem_from_any( iel->content );
                if (cel && cel->text[0])
                {
                    lstrcpynW( buf, cel->text, buf_len );
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
    {
        static const GUID IID_IStringable_cb =
            { 0x96369f54, 0x8eb6, 0x48f0, { 0xab, 0xce, 0xc1, 0xb2, 0x11, 0xe6, 0x27, 0xc3 } };
        void *strable = NULL;
        if (SUCCEEDED(IInspectable_QueryInterface( item, &IID_IStringable_cb, &strable )) && strable)
        {
            HSTRING hs = NULL;
            HRESULT (WINAPI *to_str)(void *, HSTRING *) = (void *)(*(void ***)strable)[6];
            if (to_str && SUCCEEDED(to_str( strable, &hs )) && hs)
            {
                const WCHAR *s = WindowsGetStringRawBuffer( hs, NULL );
                if (s && s[0]) lstrcpynW( buf, s, buf_len );
                WindowsDeleteString( hs );
            }
            IUnknown_Release( (IUnknown *)strable );
        }
    }
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
    static const GUID IID_IStringable_ic =
        { 0x96369f54, 0x8eb6, 0x48f0, { 0xab, 0xce, 0xc1, 0xb2, 0x11, 0xe6, 0x27, 0xc3 } };
    static const GUID IID_IDataTemplateSelector_ic =
        { 0xa907d496, 0x46a0, 0x4cd7, { 0x8d, 0xbe, 0xf9, 0xa5, 0x81, 0xdf, 0x60, 0xb1 } };
    static const GUID IID_IDataTemplateSelector2_ic =
        { 0x3792bc47, 0x8c4b, 0x4257, { 0xa5, 0xae, 0xcc, 0x3f, 0x8e, 0xd7, 0x86, 0xeb } };
    static const GUID IID_IDataTemplate_ic =
        { 0x9910aec7, 0x8ab5, 0x4118, { 0x9b, 0xc6, 0x09, 0xf4, 0x5a, 0x35, 0x07, 0x3d } };
    struct uwp_element *item_el = uwp_elem_from_any( item );
    if (item_el)
    {
        uwp_vec_Append( impl->children_coll, item );
    }
    else
    {
        IInspectable *btn_inner = NULL;
        IInspectable *tmpl = NULL;
        IInspectable *sel = (!wcsstr( impl->class_name, L"TileList" ) && !wcsstr( impl->class_name, L"ItemsRepeater" ))
                                ? impl->item_template_selector : NULL;
        if (sel)
        {
            void *dts = NULL;
            if (SUCCEEDED(IInspectable_QueryInterface( sel, &IID_IDataTemplateSelector_ic, &dts )) && dts)
            {
                HRESULT (WINAPI *sel_fn)(void *, void *, void *, IInspectable **) = (void *)(*(void ***)dts)[6];
                if (sel_fn) sel_fn( dts, item, &impl->IDependencyObject_iface, &tmpl );
                IUnknown_Release( (IUnknown *)dts );
            }
            if (!tmpl && SUCCEEDED(IInspectable_QueryInterface( sel, &IID_IDataTemplateSelector2_ic, &dts )) && dts)
            {
                HRESULT (WINAPI *sel2_fn)(void *, void *, IInspectable **) = (void *)(*(void ***)dts)[6];
                if (sel2_fn) sel2_fn( dts, item, &tmpl );
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
            if (SUCCEEDED(IInspectable_QueryInterface( tmpl, &IID_IDataTemplate_ic, &dt )) && dt)
            {
                HRESULT (WINAPI *load_fn)(void *, void **) = (void *)(*(void ***)dt)[6];
                void *content = NULL;
                if (load_fn && SUCCEEDED(load_fn( dt, &content )) && content)
                {
                    struct uwp_element *cel = uwp_elem_from_any( content );
                    if (cel)
                    {
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
        if (SUCCEEDED(uwp_element_create( L"Windows.UI.Xaml.Controls.Button", NULL, NULL, (void **)&btn_inner )) && btn_inner)
        {
            struct uwp_element *bel = uwp_elem_from_any( btn_inner );
            if (bel)
            {
                bel->data_context = (IInspectable *)item;
                IInspectable_AddRef( bel->data_context );
                if (!wcsstr( impl->class_name, L"ListView" ) && !wcsstr( impl->class_name, L"GridView" ))
                {
                    bel->width = 44.0;
                    bel->height = 36.0;
                }
                bel->has_bg = TRUE;
                bel->bg_argb = 0xE02A2A2A;
                bel->bg_alpha = 224;
                uwp_unbox_string( (IInspectable *)item, bel->text, ARRAY_SIZE(bel->text) );
                if (!bel->text[0])
                {
                    void *strable = NULL;
                    if (SUCCEEDED(IInspectable_QueryInterface( (IInspectable *)item, &IID_IStringable_ic, &strable )) && strable)
                    {
                        HRESULT (WINAPI *to_str)(void *, HSTRING *) = (void *)(*(void ***)strable)[6];
                        HSTRING hs = NULL;
                        if (to_str && SUCCEEDED(to_str( strable, &hs )) && hs)
                        {
                            const WCHAR *raw = WindowsGetStringRawBuffer( hs, NULL );
                            if (raw && raw[0]) lstrcpynW( bel->text, raw, ARRAY_SIZE(bel->text) );
                            WindowsDeleteString( hs );
                        }
                        IUnknown_Release( (IUnknown *)strable );
                    }
                }
                if (!bel->text[0]) lstrcpynW( bel->text, L"\x25A1", ARRAY_SIZE(bel->text) );
                uwp_vec_Append( impl->children_coll, &bel->IInspectable_iface );
            }
            IInspectable_Release( btn_inner );
        }
    }
}

void uwp_items_ctrl_materialize_source( struct uwp_element *impl, IInspectable *src )
{
    static const GUID IID_IVectorView_Obj =
        { 0xa6487363, 0xb074, 0x5c60, { 0xab, 0x16, 0x86, 0x6d, 0xce, 0x4e, 0xe5, 0x4d } };
    static const GUID IID_IVectorView_Insp =
        { 0xe6f2fef0, 0x0712, 0x587f, { 0x8b, 0x13, 0x48, 0x21, 0x24, 0x9a, 0x20, 0xb5 } };
    static const GUID IID_IVector_Obj =
        { 0xb32bdca4, 0x5e52, 0x5b27, { 0xbc, 0x5d, 0xd6, 0x6a, 0x1a, 0x26, 0x8c, 0x2a } };
    static const GUID IID_IBindableVector_ic =
        { 0x393de7de, 0x6fd0, 0x4c0d, { 0xbb, 0x71, 0x47, 0x24, 0x4a, 0x11, 0x38, 0x43 } };
    static const GUID IID_IBindableIterable_ic =
        { 0x036d2c08, 0xdf29, 0x41af, { 0x8a, 0xa2, 0xd7, 0x74, 0xbe, 0x62, 0xba, 0x6f } };
    static const GUID IID_IIterable_Insp_ic =
        { 0x092b849b, 0x60b1, 0x52be, { 0xa4, 0x4a, 0x6f, 0xe8, 0xe9, 0x33, 0xcb, 0xe4 } };
    void *vec = NULL;
    UINT32 count = 0, i;

    if (!impl || !src) return;
    if ((SUCCEEDED(IInspectable_QueryInterface( src, &IID_IVector_Obj, &vec )) ||
         SUCCEEDED(IInspectable_QueryInterface( src, &IID_IVectorView_Insp, &vec )) ||
         SUCCEEDED(IInspectable_QueryInterface( src, &IID_IBindableVector_ic, &vec )) ||
         SUCCEEDED(IInspectable_QueryInterface( src, &IID_IVectorView_Obj, &vec ))) && vec)
    {
        HRESULT (WINAPI *get_size)(void *, UINT32 *) = (void *)(*(void ***)vec)[7];
        HRESULT (WINAPI *get_at)(void *, UINT32, void **) = (void *)(*(void ***)vec)[6];
        if (get_size && get_at && SUCCEEDED(get_size( vec, &count )) && count > 0)
        {
            if (count > 64) count = 64;
            if (!impl->children_coll) uwp_vec_create( impl, &impl->children_coll );
            for (i = 0; i < count; i++)
            {
                void *item = NULL;
                if (SUCCEEDED(get_at( vec, i, &item )) && item)
                {
                    uwp_items_ctrl_append_item( impl, item );
                    IUnknown_Release( (IUnknown *)item );
                }
            }
        }
        IUnknown_Release( (IUnknown *)vec );
    }
    else if ((SUCCEEDED(IInspectable_QueryInterface( src, &IID_IIterable_Insp_ic, &vec )) ||
              SUCCEEDED(IInspectable_QueryInterface( src, &IID_IBindableIterable_ic, &vec ))) && vec)
    {
        HRESULT (WINAPI *first)(void *, void **) = (void *)(*(void ***)vec)[6];
        void *iter = NULL;
        if (first && SUCCEEDED(first( vec, &iter )) && iter)
        {
            HRESULT (WINAPI *get_cur)(void *, void **) = (void *)(*(void ***)iter)[6];
            HRESULT (WINAPI *has_cur)(void *, boolean *) = (void *)(*(void ***)iter)[7];
            HRESULT (WINAPI *move_next)(void *, boolean *) = (void *)(*(void ***)iter)[8];
            boolean has = FALSE;
            int n = 0;
            if (has_cur && SUCCEEDED(has_cur( iter, &has )) && has)
            {
                if (!impl->children_coll) uwp_vec_create( impl, &impl->children_coll );
                while (has && n < 64)
                {
                    void *item = NULL;
                    if (get_cur && SUCCEEDED(get_cur( iter, &item )) && item)
                    {
                        uwp_items_ctrl_append_item( impl, item );
                        IUnknown_Release( (IUnknown *)item );
                    }
                    n++;
                    has = FALSE;
                    if (!move_next || FAILED(move_next( iter, &has ))) break;
                }
            }
            IUnknown_Release( (IUnknown *)iter );
        }
        IUnknown_Release( (IUnknown *)vec );
    }
}

static HRESULT WINAPI uwp_items_ctrl_put_ItemsSource( void *iface, void *src )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IItemsControl_iface );
    if (!impl) return S_OK;
    if (impl->items_source == (IInspectable *)src) return S_OK;
    if (impl->items_source) IInspectable_Release( impl->items_source );
    impl->items_source = (IInspectable *)src;
    if (impl->items_source) IInspectable_AddRef( impl->items_source );
    TRACE( "IItemsControl::put_ItemsSource on %s (%s) -> %p\n",
           debugstr_w(impl->class_name), debugstr_w(impl->name), src );
    if (impl->items_source && !uwp_is_combobox( impl ))
        uwp_items_ctrl_materialize_source( impl, impl->items_source );
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
    if (impl->items_source && !impl->children_coll)
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
    if (impl->items_source && !impl->children_coll)
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
const void *uwp_key_args_vtbl[16] =
{
    [0] = uwp_key_args_QI,
    [1] = uwp_key_args_AddRef,
    [2] = uwp_key_args_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_key_args_get_Key,
    [7] = uwp_key_args_get_KeyStatus,
    [8] = uwp_key_args_get_Handled,
    [9] = uwp_key_args_put_Handled,
    [10 ... 15] = uwp_generic_noop,
};

static HRESULT WINAPI uwp_ka_args_QI( void *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IKeyboardAcceleratorInvokedEventArgs_UWP ))
    {
        *out = iface;
        InterlockedIncrement( &((struct uwp_ka_invoked_args *)iface)->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI uwp_ka_args_AddRef( void *iface )
{
    return InterlockedIncrement( &((struct uwp_ka_invoked_args *)iface)->ref );
}
ULONG WINAPI uwp_ka_args_Release( void *iface )
{
    struct uwp_ka_invoked_args *a = (struct uwp_ka_invoked_args *)iface;
    ULONG r = InterlockedDecrement( &a->ref );
    if (!r)
    {
        if (a->element) IInspectable_Release( a->element );
        free( a );
    }
    return r;
}
static HRESULT WINAPI uwp_ka_args_get_Handled( void *iface, boolean *out )
{
    if (out) *out = ((struct uwp_ka_invoked_args *)iface)->handled;
    return S_OK;
}
static HRESULT WINAPI uwp_ka_args_put_Handled( void *iface, boolean val )
{
    ((struct uwp_ka_invoked_args *)iface)->handled = val ? TRUE : FALSE;
    return S_OK;
}
static HRESULT WINAPI uwp_ka_args_get_Element( void *iface, void **out )
{
    struct uwp_ka_invoked_args *a = (struct uwp_ka_invoked_args *)iface;
    if (!out) return E_POINTER;
    *out = a->element;
    if (*out) IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}
const void *uwp_ka_args_vtbl[16] =
{
    [0] = uwp_ka_args_QI,
    [1] = uwp_ka_args_AddRef,
    [2] = uwp_ka_args_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_ka_args_get_Handled,
    [7] = uwp_ka_args_put_Handled,
    [8] = uwp_ka_args_get_Element,
    [9 ... 15] = uwp_generic_noop,
};

static HRESULT WINAPI uwp_ka_get_Key( void *iface, UINT *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (out) *out = impl ? impl->accel_key : 0;
    return S_OK;
}
static HRESULT WINAPI uwp_ka_put_Key( void *iface, UINT val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl)
    {
        impl->accel_key = val;
        uwp_accel_register( impl, impl->accel_target, impl->accel_key, impl->accel_mods, impl->accel_enabled );
    }
    return S_OK;
}
static HRESULT WINAPI uwp_ka_get_Modifiers( void *iface, UINT *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (out) *out = impl ? impl->accel_mods : 0;
    return S_OK;
}
static HRESULT WINAPI uwp_ka_put_Modifiers( void *iface, UINT val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl)
    {
        impl->accel_mods = val;
        uwp_accel_register( impl, impl->accel_target, impl->accel_key, impl->accel_mods, impl->accel_enabled );
    }
    return S_OK;
}
static HRESULT WINAPI uwp_ka_get_IsEnabled( void *iface, boolean *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (out) *out = impl ? (impl->accel_enabled ? TRUE : FALSE) : TRUE;
    return S_OK;
}
static HRESULT WINAPI uwp_ka_put_IsEnabled( void *iface, boolean val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl)
    {
        impl->accel_enabled = val ? TRUE : FALSE;
        uwp_accel_register( impl, impl->accel_target, impl->accel_key, impl->accel_mods, impl->accel_enabled );
    }
    return S_OK;
}
static HRESULT WINAPI uwp_ka_get_ScopeOwner( void *iface, void **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    *out = impl ? impl->accel_scope_owner : NULL;
    if (*out) IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}
static HRESULT WINAPI uwp_ka_put_ScopeOwner( void *iface, void *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return S_OK;
    if (impl->accel_scope_owner) IInspectable_Release( impl->accel_scope_owner );
    impl->accel_scope_owner = (IInspectable *)val;
    if (impl->accel_scope_owner) IInspectable_AddRef( impl->accel_scope_owner );
    return S_OK;
}
static HRESULT WINAPI uwp_ka_add_Invoked( void *iface, void *handler, EventRegistrationToken *tok )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (tok) tok->value = 1;
    if (!impl || !handler) return S_OK;
    if (xaml_vec_grow( (void **)&impl->accel_invoked_handlers, &impl->accel_invoked_cap,
                       impl->accel_invoked_cnt + 1, sizeof(*impl->accel_invoked_handlers), 4 ))
    {
        impl->accel_invoked_handlers[impl->accel_invoked_cnt++] = (IInspectable *)handler;
        IUnknown_AddRef( (IUnknown *)handler );
        if (tok) tok->value = impl->accel_invoked_cnt;
    }
    return S_OK;
}
const void *uwp_ka_vtbl[32] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_ka_get_Key,
    [7] = uwp_ka_put_Key,
    [8] = uwp_ka_get_Modifiers,
    [9] = uwp_ka_put_Modifiers,
    [10] = uwp_ka_get_IsEnabled,
    [11] = uwp_ka_put_IsEnabled,
    [12] = uwp_ka_get_ScopeOwner,
    [13] = uwp_ka_put_ScopeOwner,
    [14] = uwp_ka_add_Invoked,
    [15 ... 31] = uwp_generic_noop,
};

struct uwp_exec_req_args
{
    IInspectable IInspectable_iface;
    LONG ref;
    IInspectable *param;
};
static HRESULT WINAPI uwp_exec_args_QI( void *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IExecuteRequestedEventArgs_UWP ))
    {
        *out = iface;
        InterlockedIncrement( &((struct uwp_exec_req_args *)iface)->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI uwp_exec_args_AddRef( void *iface )
{
    return InterlockedIncrement( &((struct uwp_exec_req_args *)iface)->ref );
}
static ULONG WINAPI uwp_exec_args_Release( void *iface )
{
    struct uwp_exec_req_args *a = (struct uwp_exec_req_args *)iface;
    ULONG r = InterlockedDecrement( &a->ref );
    if (!r)
    {
        if (a->param) IInspectable_Release( a->param );
        free( a );
    }
    return r;
}
static HRESULT WINAPI uwp_exec_args_get_Parameter( void *iface, void **out )
{
    struct uwp_exec_req_args *a = (struct uwp_exec_req_args *)iface;
    if (!out) return E_POINTER;
    *out = a->param;
    if (*out) IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}
static const void *uwp_exec_args_vtbl[16] =
{
    [0] = uwp_exec_args_QI,
    [1] = uwp_exec_args_AddRef,
    [2] = uwp_exec_args_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_exec_args_get_Parameter,
    [7 ... 15] = uwp_generic_noop,
};

static HRESULT WINAPI uwp_xaml_cmd_add_ExecuteRequested( void *iface, void *handler, EventRegistrationToken *tok )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (tok) tok->value = 1;
    if (!impl || !handler) return S_OK;
    if (xaml_vec_grow( (void **)&impl->exec_req_handlers, &impl->exec_req_cap,
                       impl->exec_req_cnt + 1, sizeof(*impl->exec_req_handlers), 4 ))
    {
        impl->exec_req_handlers[impl->exec_req_cnt++] = (IInspectable *)handler;
        IUnknown_AddRef( (IUnknown *)handler );
        if (tok) tok->value = impl->exec_req_cnt;
    }
    return S_OK;
}
const void *uwp_xaml_cmd_vtbl[32] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6 ... 9] = uwp_generic_noop,
    [10] = uwp_ui7_get_KeyboardAccelerators,
    [11 ... 14] = uwp_generic_noop,
    [15] = uwp_btn_get_Command,
    [16] = uwp_btn_put_Command,
    [17] = uwp_xaml_cmd_add_ExecuteRequested,
    [18 ... 31] = uwp_generic_noop,
};

static HRESULT WINAPI uwp_icmd_CanExecute( void *iface, void *param, boolean *out )
{
    if (out) *out = TRUE;
    return S_OK;
}
static HRESULT WINAPI uwp_icmd_Execute( void *iface, void *param )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    struct uwp_exec_req_args *args;
    IInspectable *sender;
    int i;
    if (!impl) return S_OK;
    if (impl->command)
    {
        void *sub_cmd = NULL;
        if ((SUCCEEDED(IInspectable_QueryInterface( impl->command, &IID_ICommand_UWP, &sub_cmd )) ||
             SUCCEEDED(IInspectable_QueryInterface( impl->command, &IID_ICommand_UWP2, &sub_cmd ))) && sub_cmd)
        {
            struct command_iface *cmd = (struct command_iface *)sub_cmd;
            if (cmd->lpVtbl->Execute) cmd->lpVtbl->Execute( cmd, param );
            IUnknown_Release( (IUnknown *)sub_cmd );
        }
    }
    args = calloc( 1, sizeof(*args) );
    if (!args) return E_OUTOFMEMORY;
    args->IInspectable_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_exec_args_vtbl;
    args->ref = 1;
    args->param = (IInspectable *)param;
    if (args->param) IInspectable_AddRef( args->param );
    sender = impl->outer ? impl->outer : &impl->IXamlUICommand_iface;
    for (i = 0; i < impl->exec_req_cnt; i++)
    {
        if (impl->exec_req_handlers[i])
        {
            struct xaml_event_handler *h = (struct xaml_event_handler *)impl->exec_req_handlers[i];
            h->lpVtbl->Invoke( h, sender, &args->IInspectable_iface );
        }
    }
    uwp_exec_args_Release( &args->IInspectable_iface );
    return S_OK;
}
const void *uwp_icmd_vtbl[16] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6 ... 7] = uwp_generic_noop,
    [8] = uwp_icmd_CanExecute,
    [9] = uwp_icmd_Execute,
    [10 ... 15] = uwp_generic_noop,
};

static HRESULT WINAPI uwp_mfi_get_Text( void *iface, HSTRING *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    return WindowsCreateString( impl ? impl->text : L"", impl ? wcslen(impl->text) : 0, out );
}
static HRESULT WINAPI uwp_mfi_put_Text( void *iface, HSTRING val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    const WCHAR *raw;
    if ((ULONG_PTR)val < 0x10000) return S_OK;
    raw = WindowsGetStringRawBuffer( val, NULL );
    if (impl && raw) lstrcpynW( impl->text, raw, ARRAY_SIZE(impl->text) );
    return S_OK;
}
const void *uwp_mfi_vtbl[32] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_mfi_get_Text,
    [7] = uwp_mfi_put_Text,
    [8] = uwp_btn_get_Command,
    [9] = uwp_btn_put_Command,
    [10] = uwp_btn_get_CommandParameter,
    [11] = uwp_btn_put_CommandParameter,
    [12] = uwp_btn_add_Click,
    [13 ... 31] = uwp_generic_noop,
};

static HRESULT WINAPI uwp_mfi3_get_AccelText( void *iface, HSTRING *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    return WindowsCreateString( impl ? impl->accel_text : L"", impl ? wcslen(impl->accel_text) : 0, out );
}
static HRESULT WINAPI uwp_mfi3_put_AccelText( void *iface, HSTRING val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    const WCHAR *raw;
    if ((ULONG_PTR)val < 0x10000) return S_OK;
    raw = WindowsGetStringRawBuffer( val, NULL );
    if (impl && raw) lstrcpynW( impl->accel_text, raw, ARRAY_SIZE(impl->accel_text) );
    return S_OK;
}
const void *uwp_mfi3_vtbl[16] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_mfi3_get_AccelText,
    [7] = uwp_mfi3_put_AccelText,
    [8 ... 15] = uwp_generic_noop,
};

static HRESULT WINAPI uwp_tmfi_get_IsChecked( void *iface, boolean *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    *out = (impl && impl->is_checked == 1) ? TRUE : FALSE;
    return S_OK;
}
static HRESULT WINAPI uwp_tmfi_put_IsChecked( void *iface, boolean val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl) impl->is_checked = val ? 1 : 0;
    return S_OK;
}
const void *uwp_tmfi_vtbl[16] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_tmfi_get_IsChecked,
    [7] = uwp_tmfi_put_IsChecked,
    [8 ... 15] = uwp_generic_noop,
};

const void *uwp_mfsi_vtbl[16] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_panel_get_Children,
    [7] = uwp_mfi_get_Text,
    [8] = uwp_mfi_put_Text,
    [9 ... 15] = uwp_generic_noop,
};

static HRESULT WINAPI uwp_mf2_ShowAt( void *iface, void *target, UINT64 pt_raw )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    struct uwp_element *target_el = uwp_elem_from_any( target );
    float fx = 0.0f, fy = 0.0f;
    HWND owner = NULL;
    POINT spt;
    UINT dpi;
    int i;

    if (!impl) return S_OK;
    memcpy( &fx, &pt_raw, sizeof(float) );
    memcpy( &fy, (const char *)&pt_raw + sizeof(float), sizeof(float) );

    if (target_el && target_el->island_hwnd)
        owner = GetAncestor( target_el->island_hwnd, GA_ROOT );
    if (!owner) owner = GetActiveWindow();
    if (!owner) owner = GetForegroundWindow();
    if (!owner && g_island_hwnd_cnt > 0 && g_island_hwnds[0])
        owner = GetAncestor( g_island_hwnds[0], GA_ROOT );

    dpi = owner ? GetDpiForWindow( owner ) : 96;
    if (!dpi) dpi = 96;
    spt.x = MulDiv( (int)(fx + 0.5f), dpi, 96 );
    spt.y = MulDiv( (int)(fy + 0.5f), dpi, 96 );
    if (owner && IsWindow( owner ))
        ClientToScreen( owner, &spt );
    else
        GetCursorPos( &spt );

    TRACE( "IMenuFlyout2::ShowAt(%s, pt=%.1f,%.1f -> screen=%ld,%ld, owner=%p)\n",
           debugstr_w(impl->class_name), fx, fy, spt.x, spt.y, owner );

    for (i = 0; i < impl->flyout_opening_cnt; i++)
    {
        if (impl->flyout_opening_handlers[i])
        {
            IInspectable *sender = impl->outer ? impl->outer : &impl->IFlyout_iface;
            struct xaml_event_handler *h = (struct xaml_event_handler *)impl->flyout_opening_handlers[i];
            __TRY { h->lpVtbl->Invoke( h, sender, NULL ); }
            __EXCEPT_ALL {}
            __ENDTRY;
        }
    }

    uwp_show_flyout_popup( impl, owner, spt.x, spt.y );
    return S_OK;
}

static HRESULT WINAPI uwp_mf2_ShowAtWithOptions( void *iface, void *target, void *opts )
{
    POINT pt;
    UINT64 raw = 0;
    float fx, fy;
    HWND owner = GetActiveWindow();
    (void)opts;
    GetCursorPos( &pt );
    if (owner && IsWindow( owner )) ScreenToClient( owner, &pt );
    fx = (float)pt.x;
    fy = (float)pt.y;
    memcpy( &raw, &fx, sizeof(float) );
    memcpy( (char *)&raw + sizeof(float), &fy, sizeof(float) );
    return uwp_mf2_ShowAt( iface, target, raw );
}

const void *uwp_mf2_vtbl[16] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_mf2_ShowAt,
    [7] = uwp_mf2_ShowAtWithOptions,
    [8 ... 15] = uwp_generic_noop,
};

/* --- IContentDialog, IContentDialog2, IContentDialogClosedEventArgs --- */
struct uwp_cd_closed_args
{
    const void *vtbl;
    LONG ref;
    int result;
};

static HRESULT WINAPI cd_closed_args_QI( void *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IContentDialogClosedEventArgs_UWP ))
    {
        *out = iface;
        InterlockedIncrement( &((struct uwp_cd_closed_args *)iface)->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI cd_closed_args_AddRef( void *iface )
{
    return InterlockedIncrement( &((struct uwp_cd_closed_args *)iface)->ref );
}
static ULONG WINAPI cd_closed_args_Release( void *iface )
{
    struct uwp_cd_closed_args *a = iface;
    ULONG r = InterlockedDecrement( &a->ref );
    if (!r) free( a );
    return r;
}
static HRESULT WINAPI cd_closed_args_get_Result( void *iface, int *out )
{
    struct uwp_cd_closed_args *a = iface;
    if (out) *out = a ? a->result : 0;
    return S_OK;
}
static const void *uwp_cd_closed_args_vtbl[8] =
{
    cd_closed_args_QI,
    cd_closed_args_AddRef,
    cd_closed_args_Release,
    uwp_sub_GetIids,
    uwp_sub_GetRuntimeClassName,
    uwp_sub_GetTrustLevel,
    cd_closed_args_get_Result,
    uwp_generic_noop,
};

static void uwp_cd_fire_closed( struct uwp_element *impl, int result )
{
    struct uwp_cd_closed_args *args;
    IInspectable *sender;
    int i;
    if (!impl) return;
    if (impl->dialog_hwnd && IsWindow( impl->dialog_hwnd ))
    {
        HWND dh = impl->dialog_hwnd;
        impl->dialog_hwnd = NULL;
        DestroyWindow( dh );
    }
    args = calloc( 1, sizeof(*args) );
    if (!args) return;
    args->vtbl = uwp_cd_closed_args_vtbl;
    args->ref = 1;
    args->result = result;
    sender = impl->outer ? impl->outer : &impl->IContentDialog_iface;
    for (i = 0; i < impl->flyout_closed_cnt; i++)
    {
        if (impl->flyout_closed_handlers[i])
        {
            struct xaml_event_handler *h = (struct xaml_event_handler *)impl->flyout_closed_handlers[i];
            __TRY { h->lpVtbl->Invoke( h, sender, args ); }
            __EXCEPT_ALL {}
            __ENDTRY;
        }
    }
    cd_closed_args_Release( args );
    if (g_island_hwnd_cnt > 0 && g_island_hwnds[0])
    {
        HWND root = GetAncestor( g_island_hwnds[0], GA_ROOT );
        if (root && IsWindow( root ))
        {
            RedrawWindow( root, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN );
            PostMessageW( root, WM_NULL, 0, 0 );
        }
    }
    uwp_invalidate_window();
}

static LRESULT CALLBACK uwp_cd_popup_wndproc( HWND hwnd, UINT msg, WPARAM wp, LPARAM lp )
{
    struct uwp_element *impl = (struct uwp_element *)GetWindowLongPtrW( hwnd, GWLP_USERDATA );
    if (msg == WM_NCCREATE)
    {
        CREATESTRUCTW *cs = (CREATESTRUCTW *)lp;
        SetWindowLongPtrW( hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams );
        return DefWindowProcW( hwnd, msg, wp, lp );
    }
    if (msg == WM_ERASEBKGND)
        return 1;
    if (msg == WM_APP + 1)
    {
        if (IsWindow( hwnd ) && IsWindowVisible( hwnd ))
            SetFocus( hwnd );
        return 0;
    }
    if ((msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) && impl)
    {
        if (wp == VK_ESCAPE)
        {
            uwp_cd_fire_closed( impl, 0 );
            return 0;
        }
        if (wp == VK_RETURN)
        {
            int res = (impl->dialog_default_btn == 1) ? 1 : ((impl->dialog_default_btn == 2) ? 2 : 0);
            uwp_cd_fire_closed( impl, res );
            return 0;
        }
    }
    if (msg == WM_LBUTTONUP && impl)
    {
        RECT rc;
        int mx = (short)LOWORD( lp ), my = (short)HIWORD( lp );
        GetClientRect( hwnd, &rc );
        if (my >= rc.bottom - 52 && my <= rc.bottom - 14)
        {
            int btn_w = (rc.right - rc.left - 36) / 3;
            if (mx >= rc.left + 12 && mx < rc.left + 12 + btn_w && impl->dialog_primary_text[0])
                uwp_cd_fire_closed( impl, 1 );
            else if (mx >= rc.left + 18 + btn_w && mx < rc.left + 18 + 2 * btn_w && impl->dialog_secondary_text[0])
                uwp_cd_fire_closed( impl, 2 );
            else if (mx >= rc.left + 24 + 2 * btn_w)
                uwp_cd_fire_closed( impl, 0 );
        }
        return 0;
    }
    if (msg == WM_PAINT && impl)
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint( hwnd, &ps );
        if (hdc)
        {
            RECT rc, trc, brc;
            HBRUSH bg_br = CreateSolidBrush( RGB( 43, 43, 43 ) );
            HBRUSH bd_br = CreateSolidBrush( RGB( 80, 80, 80 ) );
            HBRUSH p_br  = CreateSolidBrush( RGB( 0, 120, 212 ) );
            HBRUSH s_br  = CreateSolidBrush( RGB( 60, 60, 60 ) );
            HFONT f_title = CreateFontW( -18, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                         DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                         CLEARTYPE_QUALITY, FF_SWISS, L"Segoe UI" );
            HFONT f_body  = CreateFontW( -14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                         DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                         CLEARTYPE_QUALITY, FF_SWISS, L"Segoe UI" );
            HGDIOBJ old_f = SelectObject( hdc, f_title );
            int btn_w;

            GetClientRect( hwnd, &rc );
            FillRect( hdc, &rc, bg_br );
            FrameRect( hdc, &rc, bd_br );
            SetBkMode( hdc, TRANSPARENT );
            SetTextColor( hdc, RGB( 255, 255, 255 ) );

            SetRect( &trc, 20, 16, rc.right - 20, 46 );
            DrawTextW( hdc, impl->dialog_title[0] ? impl->dialog_title : L"Notepad", -1, &trc,
                       DT_LEFT | DT_VCENTER | DT_SINGLELINE );

            SelectObject( hdc, f_body );
            SetRect( &brc, 20, 52, rc.right - 20, rc.bottom - 62 );
            DrawTextW( hdc, impl->text, -1, &brc, DT_LEFT | DT_WORDBREAK );

            btn_w = (rc.right - rc.left - 36) / 3;
            if (impl->dialog_primary_text[0])
            {
                RECT r1 = { 12, rc.bottom - 46, 12 + btn_w, rc.bottom - 14 };
                FillRect( hdc, &r1, p_br );
                DrawTextW( hdc, impl->dialog_primary_text, -1, &r1, DT_CENTER | DT_VCENTER | DT_SINGLELINE );
            }
            if (impl->dialog_secondary_text[0])
            {
                RECT r2 = { 18 + btn_w, rc.bottom - 46, 18 + 2 * btn_w, rc.bottom - 14 };
                FillRect( hdc, &r2, s_br );
                DrawTextW( hdc, impl->dialog_secondary_text, -1, &r2, DT_CENTER | DT_VCENTER | DT_SINGLELINE );
            }
            {
                RECT r3 = { 24 + 2 * btn_w, rc.bottom - 46, rc.right - 12, rc.bottom - 14 };
                FillRect( hdc, &r3, s_br );
                DrawTextW( hdc, impl->dialog_close_text[0] ? impl->dialog_close_text : L"Cancel", -1, &r3,
                           DT_CENTER | DT_VCENTER | DT_SINGLELINE );
            }

            SelectObject( hdc, old_f );
            DeleteObject( f_body );
            DeleteObject( f_title );
            DeleteObject( s_br );
            DeleteObject( p_br );
            DeleteObject( bd_br );
            DeleteObject( bg_br );
        }
        EndPaint( hwnd, &ps );
        return 0;
    }
    return DefWindowProcW( hwnd, msg, wp, lp );
}

static HRESULT WINAPI uwp_cd_get_Title( void *iface, IInspectable **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    *out = impl ? impl->dialog_title_obj : NULL;
    if (*out) IInspectable_AddRef( *out );
    return S_OK;
}
static HRESULT WINAPI uwp_cd_put_Title( void *iface, IInspectable *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return S_OK;
    if (impl->dialog_title_obj) IInspectable_Release( impl->dialog_title_obj );
    impl->dialog_title_obj = val;
    if (val)
    {
        IInspectable_AddRef( val );
        uwp_unbox_string( val, impl->dialog_title, ARRAY_SIZE(impl->dialog_title) );
    }
    return S_OK;
}
static HRESULT WINAPI uwp_cd_get_NullOut( void *iface, void **out )
{
    if (out) *out = NULL;
    return S_OK;
}
static HRESULT WINAPI uwp_cd_get_BoolFalse( void *iface, boolean *out )
{
    if (out) *out = FALSE;
    return S_OK;
}
static HRESULT WINAPI uwp_cd_get_BoolTrue( void *iface, boolean *out )
{
    if (out) *out = TRUE;
    return S_OK;
}
static HRESULT WINAPI uwp_cd_get_PrimaryButtonText( void *iface, HSTRING *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    return WindowsCreateString( impl ? impl->dialog_primary_text : L"",
                                impl ? wcslen(impl->dialog_primary_text) : 0, out );
}
static HRESULT WINAPI uwp_cd_put_PrimaryButtonText( void *iface, HSTRING val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    const WCHAR *raw = ((ULONG_PTR)val >= 0x10000) ? WindowsGetStringRawBuffer( val, NULL ) : L"";
    if (impl) lstrcpynW( impl->dialog_primary_text, raw ? raw : L"", ARRAY_SIZE(impl->dialog_primary_text) );
    return S_OK;
}
static HRESULT WINAPI uwp_cd_get_SecondaryButtonText( void *iface, HSTRING *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    return WindowsCreateString( impl ? impl->dialog_secondary_text : L"",
                                impl ? wcslen(impl->dialog_secondary_text) : 0, out );
}
static HRESULT WINAPI uwp_cd_put_SecondaryButtonText( void *iface, HSTRING val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    const WCHAR *raw = ((ULONG_PTR)val >= 0x10000) ? WindowsGetStringRawBuffer( val, NULL ) : L"";
    if (impl) lstrcpynW( impl->dialog_secondary_text, raw ? raw : L"", ARRAY_SIZE(impl->dialog_secondary_text) );
    return S_OK;
}
static HRESULT WINAPI uwp_cd_Hide( void *iface )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl) uwp_cd_fire_closed( impl, 0 );
    return S_OK;
}
static const GUID IID_IAsyncOperation_ContentDialogResult_UWP =
    { 0x1f23bdd1, 0x06dc, 0x5be9, { 0x9a, 0x60, 0x0b, 0x4d, 0x94, 0xd4, 0xd7, 0x2c } };
static const GUID IID_IAsyncInfo_cd =
    { 0x00000036, 0x0000, 0x0000, { 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };

struct cd_async_op
{
    const void *lpVtbl_op;
    const void *lpVtbl_info;
    LONG ref;
    void *handler;
};

static HRESULT WINAPI cd_op_QI( void *iface, REFIID iid, void **out )
{
    struct cd_async_op *impl = CONTAINING_RECORD( iface, struct cd_async_op, lpVtbl_op );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IAsyncOperation_ContentDialogResult_UWP ))
    {
        *out = &impl->lpVtbl_op;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IAsyncInfo_cd ))
    {
        *out = &impl->lpVtbl_info;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI cd_op_AddRef( void *iface )
{
    return InterlockedIncrement( &CONTAINING_RECORD( iface, struct cd_async_op, lpVtbl_op )->ref );
}
static ULONG WINAPI cd_op_Release( void *iface )
{
    struct cd_async_op *impl = CONTAINING_RECORD( iface, struct cd_async_op, lpVtbl_op );
    ULONG r = InterlockedDecrement( &impl->ref );
    if (!r)
    {
        if (impl->handler) IUnknown_Release( (IUnknown *)impl->handler );
        free( impl );
    }
    return r;
}
static HRESULT WINAPI cd_op_GetIids( void *iface, ULONG *c, IID **i ) { if (c) *c = 0; if (i) *i = NULL; return S_OK; }
static HRESULT WINAPI cd_op_GetRuntimeClassName( void *iface, HSTRING *n )
{
    static const WCHAR cls[] = L"Windows.Foundation.IAsyncOperation`1<Windows.UI.Xaml.Controls.ContentDialogResult>";
    return WindowsCreateString( cls, ARRAY_SIZE(cls) - 1, n );
}
static HRESULT WINAPI cd_op_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI cd_op_put_Completed( void *iface, void *handler )
{
    struct cd_async_op *impl = CONTAINING_RECORD( iface, struct cd_async_op, lpVtbl_op );
    if (impl->handler) IUnknown_Release( (IUnknown *)impl->handler );
    impl->handler = handler;
    if (handler)
    {
        typedef HRESULT (WINAPI *invoke_fn)(void *, void *, UINT32);
        void **vtbl = *(void ***)handler;
        IUnknown_AddRef( (IUnknown *)handler );
        ((invoke_fn)vtbl[3])( handler, &impl->lpVtbl_op, 1 /* Completed */ );
    }
    return S_OK;
}
static HRESULT WINAPI cd_op_get_Completed( void *iface, void **out )
{
    struct cd_async_op *impl = CONTAINING_RECORD( iface, struct cd_async_op, lpVtbl_op );
    if (!out) return E_POINTER;
    *out = impl->handler;
    if (*out) IUnknown_AddRef( (IUnknown *)*out );
    return S_OK;
}
static HRESULT WINAPI cd_op_GetResults( void *iface, INT32 *res )
{
    if (res) *res = 0; /* ContentDialogResult_None */
    return S_OK;
}
static HRESULT WINAPI cd_info_QI( void *iface, REFIID iid, void **out )
{
    return cd_op_QI( &CONTAINING_RECORD( iface, struct cd_async_op, lpVtbl_info )->lpVtbl_op, iid, out );
}
static ULONG WINAPI cd_info_AddRef( void *iface )
{
    return cd_op_AddRef( &CONTAINING_RECORD( iface, struct cd_async_op, lpVtbl_info )->lpVtbl_op );
}
static ULONG WINAPI cd_info_Release( void *iface )
{
    return cd_op_Release( &CONTAINING_RECORD( iface, struct cd_async_op, lpVtbl_info )->lpVtbl_op );
}
static HRESULT WINAPI cd_info_get_Id( void *iface, UINT32 *id ) { if (id) *id = 1; return S_OK; }
static HRESULT WINAPI cd_info_get_Status( void *iface, UINT32 *st ) { if (st) *st = 1; return S_OK; }
static HRESULT WINAPI cd_info_get_ErrorCode( void *iface, HRESULT *hr ) { if (hr) *hr = S_OK; return S_OK; }
static HRESULT WINAPI cd_info_Cancel( void *iface ) { return S_OK; }
static HRESULT WINAPI cd_info_Close( void *iface ) { return S_OK; }

static const void * const cd_op_vtbl[] =
{
    cd_op_QI, cd_op_AddRef, cd_op_Release, cd_op_GetIids, cd_op_GetRuntimeClassName, cd_op_GetTrustLevel,
    cd_op_put_Completed, cd_op_get_Completed, cd_op_GetResults,
};
static const void * const cd_info_vtbl[] =
{
    cd_info_QI, cd_info_AddRef, cd_info_Release, cd_op_GetIids, cd_op_GetRuntimeClassName, cd_op_GetTrustLevel,
    cd_info_get_Id, cd_info_get_Status, cd_info_get_ErrorCode, cd_info_Cancel, cd_info_Close,
};

static HRESULT WINAPI uwp_cd_ShowAsync( void *iface, void **out )
{
    static BOOL cls_reg = FALSE;
    struct uwp_element *impl = uwp_elem_from_any( iface );
    struct cd_async_op *op;
    HWND owner = NULL;
    RECT orc = { 100, 100, 900, 700 };
    int dw = 380, dh = 180, dx, dy;

    if (out)
    {
        if ((op = calloc( 1, sizeof(*op) )))
        {
            op->lpVtbl_op = cd_op_vtbl;
            op->lpVtbl_info = cd_info_vtbl;
            op->ref = 1;
            *out = &op->lpVtbl_op;
        }
        else *out = NULL;
    }
    if (!impl) return S_OK;

    uwp_fire_loaded_tree( impl );

    if (g_island_hwnd_cnt > 0 && g_island_hwnds[0])
        owner = GetAncestor( g_island_hwnds[0], GA_ROOT );
    if (!owner) owner = GetActiveWindow();
    if (owner && IsWindow( owner ))
        GetWindowRect( owner, &orc );
    dx = (orc.left + orc.right - dw) / 2;
    dy = (orc.top + orc.bottom - dh) / 2;

    if (!cls_reg)
    {
        WNDCLASSEXW wc = { .cbSize = sizeof(wc) };
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = uwp_cd_popup_wndproc;
        wc.hInstance = GetModuleHandleW( NULL );
        wc.hCursor = LoadCursorW( NULL, (LPCWSTR)IDC_ARROW );
        wc.lpszClassName = L"WineUwpContentDialogPopup";
        RegisterClassExW( &wc );
        cls_reg = TRUE;
    }
    if (impl->dialog_hwnd && IsWindow( impl->dialog_hwnd ))
        DestroyWindow( impl->dialog_hwnd );
    impl->dialog_hwnd = CreateWindowExW( WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
                                         L"WineUwpContentDialogPopup", L"",
                                         WS_POPUP | WS_VISIBLE,
                                         dx, dy, dw, dh,
                                         owner, NULL, GetModuleHandleW( NULL ), impl );
    if (impl->dialog_hwnd)
    {
        InvalidateRect( impl->dialog_hwnd, NULL, FALSE );
        UpdateWindow( impl->dialog_hwnd );
        PostMessageW( impl->dialog_hwnd, WM_APP + 1, 0, 0 );
    }
    return S_OK;
}

const void *uwp_cd_vtbl[48] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_cd_get_Title,
    [7] = uwp_cd_put_Title,
    [8] = uwp_cd_get_NullOut,
    [9] = uwp_generic_noop,
    [10] = uwp_cd_get_BoolFalse,
    [11] = uwp_generic_noop,
    [12] = uwp_cd_get_PrimaryButtonText,
    [13] = uwp_cd_put_PrimaryButtonText,
    [14] = uwp_cd_get_SecondaryButtonText,
    [15] = uwp_cd_put_SecondaryButtonText,
    [16] = uwp_cd_get_NullOut,
    [17] = uwp_generic_noop,
    [18] = uwp_cd_get_NullOut,
    [19] = uwp_generic_noop,
    [20] = uwp_cd_get_NullOut,
    [21] = uwp_generic_noop,
    [22] = uwp_cd_get_NullOut,
    [23] = uwp_generic_noop,
    [24] = uwp_cd_get_BoolTrue,
    [25] = uwp_generic_noop,
    [26] = uwp_cd_get_BoolTrue,
    [27] = uwp_generic_noop,
    [28] = uwp_ui_event_add,
    [29] = uwp_generic_noop,
    [30] = uwp_flyout_add_Closed,
    [31] = uwp_generic_noop,
    [32] = uwp_ui_event_add,
    [33] = uwp_generic_noop,
    [34] = uwp_ui_event_add,
    [35] = uwp_generic_noop,
    [36] = uwp_ui_event_add,
    [37] = uwp_generic_noop,
    [38] = uwp_cd_Hide,
    [39] = uwp_cd_ShowAsync,
    [40 ... 47] = uwp_generic_noop,
};

static const GUID IID_IAutoSuggestBoxTextChangedEventArgs_UWP =
    { 0x38c5e3a3, 0x5f44, 0x47f4, { 0xa4, 0x50, 0x1c, 0x5e, 0x1f, 0x4d, 0x4b, 0x9f } };
static const GUID IID_IAutoSuggestBoxQuerySubmittedEventArgs_UWP =
    { 0xd5f5b0d0, 0x7d72, 0x4c76, { 0x9e, 0x77, 0x28, 0xa3, 0xd5, 0xa2, 0xe7, 0x11 } };

struct asb_tc_args
{
    const void **lpVtbl;
    LONG ref;
    INT32 reason;
};

static HRESULT WINAPI asb_tc_QI( void *iface, REFIID iid, void **out )
{
    struct asb_tc_args *a = iface;
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IAutoSuggestBoxTextChangedEventArgs_UWP ))
    {
        InterlockedIncrement( &a->ref );
        *out = iface;
        return S_OK;
    }
    FIXME( "asb_tc_args unsupported iid %s\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI asb_tc_AddRef( void *iface ) { return InterlockedIncrement( &((struct asb_tc_args *)iface)->ref ); }
static ULONG WINAPI asb_tc_Release( void *iface )
{
    struct asb_tc_args *a = iface;
    ULONG r = InterlockedDecrement( &a->ref );
    if (!r) free( a );
    return r;
}
static HRESULT WINAPI asb_tc_GetIids( void *iface, ULONG *cnt, IID **iids )
{
    IID *out;
    if (!cnt || !iids) return E_POINTER;
    out = CoTaskMemAlloc( sizeof(IID) );
    if (!out) return E_OUTOFMEMORY;
    out[0] = IID_IAutoSuggestBoxTextChangedEventArgs_UWP;
    *cnt = 1;
    *iids = out;
    return S_OK;
}
static HRESULT WINAPI asb_tc_GetRuntimeClassName( void *iface, HSTRING *name )
{
    static const WCHAR cls[] = L"Windows.UI.Xaml.Controls.AutoSuggestBoxTextChangedEventArgs";
    return WindowsCreateString( cls, ARRAY_SIZE(cls) - 1, name );
}
static HRESULT WINAPI asb_tc_GetTrustLevel( void *iface, TrustLevel *lvl )
{
    if (lvl) *lvl = BaseTrust;
    return S_OK;
}
static HRESULT WINAPI asb_tc_get_Reason( void *iface, INT32 *val )
{
    struct asb_tc_args *a = iface;
    if (val) *val = a->reason;
    return S_OK;
}
static HRESULT WINAPI asb_tc_put_Reason( void *iface, INT32 val )
{
    struct asb_tc_args *a = iface;
    a->reason = val;
    return S_OK;
}
static HRESULT WINAPI asb_tc_CheckCurrent( void *iface, boolean *val )
{
    if (val) *val = TRUE;
    return S_OK;
}
static const void *asb_tc_vtbl[] =
{
    asb_tc_QI, asb_tc_AddRef, asb_tc_Release,
    asb_tc_GetIids, asb_tc_GetRuntimeClassName, asb_tc_GetTrustLevel,
    asb_tc_get_Reason, asb_tc_put_Reason, asb_tc_CheckCurrent,
};

struct asb_qs_args
{
    const void **lpVtbl;
    LONG ref;
    HSTRING query_text;
};

static HRESULT WINAPI asb_qs_QI( void *iface, REFIID iid, void **out )
{
    struct asb_qs_args *a = iface;
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IAutoSuggestBoxQuerySubmittedEventArgs_UWP ))
    {
        InterlockedIncrement( &a->ref );
        *out = iface;
        return S_OK;
    }
    FIXME( "asb_qs_args unsupported iid %s\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI asb_qs_AddRef( void *iface ) { return InterlockedIncrement( &((struct asb_qs_args *)iface)->ref ); }
static ULONG WINAPI asb_qs_Release( void *iface )
{
    struct asb_qs_args *a = iface;
    ULONG r = InterlockedDecrement( &a->ref );
    if (!r)
    {
        if (a->query_text) WindowsDeleteString( a->query_text );
        free( a );
    }
    return r;
}
static HRESULT WINAPI asb_qs_GetIids( void *iface, ULONG *cnt, IID **iids )
{
    IID *out;
    if (!cnt || !iids) return E_POINTER;
    out = CoTaskMemAlloc( sizeof(IID) );
    if (!out) return E_OUTOFMEMORY;
    out[0] = IID_IAutoSuggestBoxQuerySubmittedEventArgs_UWP;
    *cnt = 1;
    *iids = out;
    return S_OK;
}
static HRESULT WINAPI asb_qs_GetRuntimeClassName( void *iface, HSTRING *name )
{
    static const WCHAR cls[] = L"Windows.UI.Xaml.Controls.AutoSuggestBoxQuerySubmittedEventArgs";
    return WindowsCreateString( cls, ARRAY_SIZE(cls) - 1, name );
}
static HRESULT WINAPI asb_qs_GetTrustLevel( void *iface, TrustLevel *lvl )
{
    if (lvl) *lvl = BaseTrust;
    return S_OK;
}
static HRESULT WINAPI asb_qs_get_QueryText( void *iface, HSTRING *val )
{
    struct asb_qs_args *a = iface;
    if (!val) return E_POINTER;
    return WindowsDuplicateString( a->query_text, val );
}
static HRESULT WINAPI asb_qs_get_ChosenSuggestion( void *iface, IInspectable **val )
{
    if (val) *val = NULL;
    return S_OK;
}
static const void *asb_qs_vtbl[] =
{
    asb_qs_QI, asb_qs_AddRef, asb_qs_Release,
    asb_qs_GetIids, asb_qs_GetRuntimeClassName, asb_qs_GetTrustLevel,
    asb_qs_get_QueryText, asb_qs_get_ChosenSuggestion,
};

static HRESULT WINAPI uwp_asb_add_SuggestionChosen( void *iface, void *handler, EventRegistrationToken *tok )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return E_NOINTERFACE;
    return xaml_event_registry_add( &impl->asb_suggestion_chosen_reg, handler, tok );
}
static HRESULT WINAPI uwp_asb_remove_SuggestionChosen( void *iface, EventRegistrationToken tok )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return E_NOINTERFACE;
    return xaml_event_registry_remove( &impl->asb_suggestion_chosen_reg, tok );
}
static HRESULT WINAPI uwp_asb_add_TextChanged( void *iface, void *handler, EventRegistrationToken *tok )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return E_NOINTERFACE;
    TRACE( "uwp_asb_add_TextChanged on %s (%s) handler=%p\n", debugstr_w(impl->class_name), debugstr_w(impl->name), handler );
    return xaml_event_registry_add( &impl->asb_text_changed_reg, handler, tok );
}
static HRESULT WINAPI uwp_asb_remove_TextChanged( void *iface, EventRegistrationToken tok )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return E_NOINTERFACE;
    return xaml_event_registry_remove( &impl->asb_text_changed_reg, tok );
}
static HRESULT WINAPI uwp_asb_add_QuerySubmitted( void *iface, void *handler, EventRegistrationToken *tok )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return E_NOINTERFACE;
    TRACE( "uwp_asb_add_QuerySubmitted on %s (%s) handler=%p\n", debugstr_w(impl->class_name), debugstr_w(impl->name), handler );
    return xaml_event_registry_add( &impl->asb_query_submitted_reg, handler, tok );
}
static HRESULT WINAPI uwp_asb_remove_QuerySubmitted( void *iface, EventRegistrationToken tok )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return E_NOINTERFACE;
    return xaml_event_registry_remove( &impl->asb_query_submitted_reg, tok );
}

void uwp_asb_fire_text_changed( struct uwp_element *el )
{
    void **handlers = NULL;
    unsigned int cnt, i;
    struct asb_tc_args *args;
    IInspectable *sender;

    if (!el) return;
    cnt = xaml_event_registry_snapshot_alloc( &el->asb_text_changed_reg, &handlers );
    if (!cnt) return;
    args = calloc( 1, sizeof(*args) );
    if (!args)
    {
        for (i = 0; i < cnt; i++) IUnknown_Release( (IUnknown *)handlers[i] );
        free( handlers );
        return;
    }
    args->lpVtbl = asb_tc_vtbl;
    args->ref = 1;
    args->reason = 0; /* UserInput */
    sender = el->outer ? el->outer : &el->IInspectable_iface;
    for (i = 0; i < cnt; i++)
    {
        struct xaml_event_handler *h = (struct xaml_event_handler *)handlers[i];
        __TRY { h->lpVtbl->Invoke( h, sender, (IInspectable *)args ); }
        __EXCEPT_ALL { TRACE( "uwp_asb_fire_text_changed handler %u faulted\n", i ); }
        __ENDTRY;
        IUnknown_Release( (IUnknown *)h );
    }
    free( handlers );
    asb_tc_Release( args );
}

void uwp_asb_fire_query_submitted( struct uwp_element *el )
{
    void **handlers = NULL;
    unsigned int cnt, i;
    struct asb_qs_args *args;
    IInspectable *sender;

    if (!el) return;
    cnt = xaml_event_registry_snapshot_alloc( &el->asb_query_submitted_reg, &handlers );
    if (!cnt) return;
    args = calloc( 1, sizeof(*args) );
    if (!args)
    {
        for (i = 0; i < cnt; i++) IUnknown_Release( (IUnknown *)handlers[i] );
        free( handlers );
        return;
    }
    args->lpVtbl = asb_qs_vtbl;
    args->ref = 1;
    WindowsCreateString( el->text, (UINT32)wcslen( el->text ), &args->query_text );
    sender = el->outer ? el->outer : &el->IInspectable_iface;
    TRACE( "uwp_asb_fire_query_submitted on %s (%s) query=%s handlers=%u\n",
           debugstr_w(el->class_name), debugstr_w(el->name), debugstr_w(el->text), cnt );
    for (i = 0; i < cnt; i++)
    {
        struct xaml_event_handler *h = (struct xaml_event_handler *)handlers[i];
        __TRY { h->lpVtbl->Invoke( h, sender, (IInspectable *)args ); }
        __EXCEPT_ALL { TRACE( "uwp_asb_fire_query_submitted handler %u faulted\n", i ); }
        __ENDTRY;
        IUnknown_Release( (IUnknown *)h );
    }
    free( handlers );
    asb_qs_Release( args );
}

const void *uwp_asb_vtbl[32] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6 ... 7] = uwp_generic_noop,
    [8] = uwp_cd_get_BoolFalse,
    [9] = uwp_generic_noop,
    [10] = uwp_tb_get_Text,
    [11] = uwp_generic_noop,
    [12] = uwp_tb_get_Text,
    [13] = uwp_tb_put_Text,
    [14] = uwp_cd_get_BoolTrue,
    [15] = uwp_generic_noop,
    [16] = uwp_tb_get_Text,
    [17] = uwp_generic_noop,
    [18] = uwp_cd_get_NullOut,
    [19] = uwp_generic_noop,
    [20] = uwp_cd_get_BoolTrue,
    [21] = uwp_generic_noop,
    [22] = uwp_cd_get_NullOut,
    [23] = uwp_generic_noop,
    [24] = uwp_asb_add_SuggestionChosen,
    [25] = uwp_asb_remove_SuggestionChosen,
    [26] = uwp_asb_add_TextChanged,
    [27] = uwp_asb_remove_TextChanged,
    [28] = uwp_asb_add_QuerySubmitted,
    [29] = uwp_asb_remove_QuerySubmitted,
    [30 ... 31] = uwp_generic_noop,
};

const void *uwp_asb2_vtbl[16] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_cd_get_NullOut,
    [7] = uwp_generic_noop,
    [8] = uwp_asb_add_QuerySubmitted,
    [9] = uwp_asb_remove_QuerySubmitted,
    [10 ... 15] = uwp_generic_noop,
};

static HRESULT WINAPI uwp_sv_get_IsPaneOpen( void *iface, boolean *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (val) *val = impl ? (boolean)impl->is_open : FALSE;
    return S_OK;
}
static HRESULT WINAPI uwp_sv_put_IsPaneOpen( void *iface, boolean val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl) impl->is_open = (val != 0);
    return S_OK;
}
static HRESULT WINAPI uwp_sv_get_OpenPaneLength( void *iface, double *val )
{
    if (val) *val = 320.0;
    return S_OK;
}
static HRESULT WINAPI uwp_sv_get_CompactPaneLength( void *iface, double *val )
{
    if (val) *val = 48.0;
    return S_OK;
}
static HRESULT WINAPI uwp_sv_get_IntZero( void *iface, INT32 *val )
{
    if (val) *val = 0;
    return S_OK;
}
static HRESULT WINAPI uwp_sv_get_DisplayMode( void *iface, INT32 *val )
{
    if (val) *val = 2; /* SplitViewDisplayMode_CompactInline */
    return S_OK;
}

static BOOL uwp_sv_would_cycle( struct uwp_element *parent, struct uwp_element *child )
{
    int guard = 0;
    struct uwp_element *cur = parent;
    if (!child || !parent) return FALSE;
    while (cur && guard++ < 64)
    {
        if (cur == child) return TRUE;
        cur = cur->parent;
    }
    return guard >= 64;
}

static HRESULT WINAPI uwp_sv_get_Content( void *iface, void **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    *out = impl ? impl->content : NULL;
    if (*out) IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}
static HRESULT WINAPI uwp_sv_put_Content( void *iface, void *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    struct uwp_element *ch;
    if (!impl) return S_OK;
    ch = uwp_elem_from_any( val );
    ERR( "uwp_sv_put_Content impl=%p(%s) val=%p(%s)\n",
         impl, debugstr_w(impl->class_name), val, ch ? debugstr_w(ch->class_name) : "?" );
    if (ch && uwp_sv_would_cycle( impl, ch ))
    {
        WARN( "uwp_sv_put_Content: cycle detected, skipping\n" );
        return S_OK;
    }
    if (impl->content) IInspectable_Release( impl->content );
    impl->content = (IInspectable *)val;
    if (impl->content) IInspectable_AddRef( impl->content );
    if (ch) ch->parent = impl;
    g_layout_dirty = TRUE;
    uwp_invalidate_window();
    return S_OK;
}
static HRESULT WINAPI uwp_sv_get_Pane( void *iface, void **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    *out = impl ? impl->pane : NULL;
    if (*out) IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}
static HRESULT WINAPI uwp_sv_put_Pane( void *iface, void *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    struct uwp_element *ch;
    if (!impl) return S_OK;
    if (impl->pane) IInspectable_Release( impl->pane );
    impl->pane = (IInspectable *)val;
    if (impl->pane) IInspectable_AddRef( impl->pane );
    if ((ch = uwp_elem_from_any( val )) != NULL) ch->parent = impl;
    g_layout_dirty = TRUE;
    uwp_invalidate_window();
    return S_OK;
}

const void *uwp_splitview_vtbl[32] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_sv_get_Content,        /* get_Content */
    [7] = uwp_sv_put_Content,        /* put_Content */
    [8] = uwp_sv_get_Pane,           /* get_Pane */
    [9] = uwp_sv_put_Pane,           /* put_Pane */
    [10] = uwp_sv_get_IsPaneOpen,    /* get_IsPaneOpen */
    [11] = uwp_sv_put_IsPaneOpen,    /* put_IsPaneOpen */
    [12] = uwp_sv_get_OpenPaneLength,/* get_OpenPaneLength */
    [13] = uwp_generic_noop,         /* put_OpenPaneLength */
    [14] = uwp_sv_get_CompactPaneLength, /* get_CompactPaneLength */
    [15] = uwp_generic_noop,         /* put_CompactPaneLength */
    [16] = uwp_sv_get_IntZero,       /* get_PanePlacement */
    [17] = uwp_generic_noop,         /* put_PanePlacement */
    [18] = uwp_sv_get_DisplayMode,   /* get_DisplayMode */
    [19] = uwp_generic_noop,         /* put_DisplayMode */
    [20] = uwp_cd_get_NullOut,       /* get_TemplateSettings */
    [21] = uwp_cd_get_NullOut,       /* get_PaneBackground */
    [22] = uwp_generic_noop,         /* put_PaneBackground */
    [23] = uwp_ui_event_add,         /* add_PaneClosing */
    [24] = uwp_generic_noop,         /* remove_PaneClosing */
    [25] = uwp_ui_event_add,         /* add_PaneClosed */
    [26] = uwp_generic_noop,         /* remove_PaneClosed */
    [27 ... 31] = uwp_generic_noop,
};

const void *uwp_splitview2_vtbl[16] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_ui_event_add,          /* ISplitView3::add_PaneOpening */
    [7] = uwp_generic_noop,          /* ISplitView3::remove_PaneOpening */
    [8] = uwp_ui_event_add,          /* ISplitView3::add_PaneOpened */
    [9] = uwp_generic_noop,          /* ISplitView3::remove_PaneOpened */
    [10 ... 15] = uwp_generic_noop,
};

const void *uwp_cd2_vtbl[24] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_cd2_get_CloseButtonText,
    [7] = uwp_cd2_put_CloseButtonText,
    [8] = uwp_cd_get_NullOut,
    [9] = uwp_generic_noop,
    [10] = uwp_cd_get_NullOut,
    [11] = uwp_generic_noop,
    [12] = uwp_cd_get_BoolTrue,
    [13] = uwp_generic_noop,
    [14] = uwp_ui_event_add,
    [15] = uwp_generic_noop,
    [16] = uwp_cd_get_NullOut,
    [17] = uwp_generic_noop,
    [18] = uwp_cd2_get_DefaultButton,
    [19] = uwp_cd2_put_DefaultButton,
    [20 ... 23] = uwp_generic_noop,
};

static HRESULT WINAPI uwp_tt_get_IsOpen( void *iface, boolean *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (out) *out = (impl && impl->is_open) ? TRUE : FALSE;
    return S_OK;
}
BOOL uwp_has_open_teachingtip( struct uwp_element *el )
{
    if (!el) return FALSE;
    if (wcsstr( el->class_name, L"TeachingTip" ) && el->is_open) return TRUE;
    if (el->content && uwp_has_open_teachingtip( uwp_elem_from_any( el->content ) )) return TRUE;
    if (el->children_coll)
    {
        struct uwp_vec_view *children = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        unsigned int k;
        for (k = 0; k < children->count; k++)
            if (uwp_has_open_teachingtip( uwp_elem_from_any( children->items[k] ) )) return TRUE;
    }
    return FALSE;
}

static HRESULT WINAPI uwp_tt_put_IsOpen( void *iface, boolean val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    BOOL prev;
    if (!impl) return S_OK;
    prev = impl->is_open;
    impl->is_open = val ? TRUE : FALSE;
    g_layout_dirty = TRUE;
    TRACE( "ITeachingTip::put_IsOpen on %s (%s) -> %d (prev %d)\n",
           debugstr_w(impl->class_name), debugstr_w(impl->name), impl->is_open, prev );
    if (impl->is_open)
    {
        int ii;
        for (ii = 0; ii < g_island_hwnd_cnt; ii++)
        {
            HWND ih = g_island_hwnds[ii];
            struct uwp_element *isl = ih ? (struct uwp_element *)GetWindowLongPtrW( ih, GWLP_USERDATA ) : NULL;
            struct uwp_element *root_el = (isl && isl->content) ? uwp_elem_from_any( isl->content ) : NULL;
            if (root_el && (root_el == impl || uwp_elem_is_descendant( root_el, impl )))
                SetWindowPos( ih, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW );
        }
    }
    if (prev && !impl->is_open)
    {
        IInspectable *sender = impl->outer ? impl->outer : &impl->ITeachingTip_iface;
        struct uwp_sel_changed_args *args = uwp_sel_args_create( NULL, NULL );
        int i;
        if (args)
        {
            for (i = 0; i < impl->tip_closing_cnt; i++)
            {
                if (impl->tip_closing_handlers[i])
                {
                    struct xaml_event_handler *h = (struct xaml_event_handler *)impl->tip_closing_handlers[i];
                    h->lpVtbl->Invoke( h, sender, &args->IInspectable_iface );
                }
            }
            for (i = 0; i < impl->tip_closed_cnt; i++)
            {
                if (impl->tip_closed_handlers[i])
                {
                    struct xaml_event_handler *h = (struct xaml_event_handler *)impl->tip_closed_handlers[i];
                    h->lpVtbl->Invoke( h, sender, &args->IInspectable_iface );
                }
            }
            uwp_sel_args_Release( &args->IInspectable_iface );
        }
    }
    uwp_invalidate_window();
    return S_OK;
}
static HRESULT WINAPI uwp_tt_add_Closing( void *iface, void *handler, EventRegistrationToken *tok )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (tok) tok->value = 1;
    if (!impl || !handler) return S_OK;
    if (xaml_vec_grow( (void **)&impl->tip_closing_handlers, &impl->tip_closing_cap,
                       impl->tip_closing_cnt + 1, sizeof(*impl->tip_closing_handlers), 4 ))
    {
        impl->tip_closing_handlers[impl->tip_closing_cnt++] = (IInspectable *)handler;
        IUnknown_AddRef( (IUnknown *)handler );
        if (tok) tok->value = impl->tip_closing_cnt;
    }
    return S_OK;
}
static HRESULT WINAPI uwp_tt_add_Closed( void *iface, void *handler, EventRegistrationToken *tok )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (tok) tok->value = 1;
    if (!impl || !handler) return S_OK;
    if (xaml_vec_grow( (void **)&impl->tip_closed_handlers, &impl->tip_closed_cap,
                       impl->tip_closed_cnt + 1, sizeof(*impl->tip_closed_handlers), 4 ))
    {
        impl->tip_closed_handlers[impl->tip_closed_cnt++] = (IInspectable *)handler;
        IUnknown_AddRef( (IUnknown *)handler );
        if (tok) tok->value = impl->tip_closed_cnt;
    }
    return S_OK;
}
const void *uwp_teachingtip_vtbl[64] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6 ... 9] = uwp_generic_noop,
    [10] = uwp_tt_get_IsOpen,
    [11] = uwp_tt_put_IsOpen,
    [12 ... 50] = uwp_generic_noop,
    [51] = uwp_tt_add_Closing,
    [52] = uwp_generic_noop,
    [53] = uwp_tt_add_Closed,
    [54 ... 63] = uwp_generic_noop,
};

const void *uwp_grid_vtbl[32] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_grid_get_RowDefinitions,
    [7] = uwp_grid_get_ColumnDefinitions,
    [8 ... 31] = uwp_generic_noop,
};

static HRESULT WINAPI uwp_grid3_get_double( void *iface, double *val ) { if (val) *val = 0.0; return S_OK; }
static HRESULT WINAPI uwp_grid3_put_double( void *iface, double val ) { return S_OK; }
const void *uwp_grid3_vtbl[16] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_grid3_get_double,
    [7] = uwp_grid3_put_double,
    [8] = uwp_grid3_get_double,
    [9] = uwp_grid3_put_double,
    [10 ... 15] = uwp_generic_noop,
};

static HRESULT WINAPI uwp_tb_get_TextWrapping( void *iface, INT32 *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (out) *out = impl ? impl->text_wrapping : 1;
    return S_OK;
}
static HRESULT WINAPI uwp_tb_put_TextWrapping( void *iface, INT32 val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl)
    {
        impl->text_wrapping = val;
        g_layout_dirty = TRUE;
        uwp_invalidate_window();
    }
    return S_OK;
}
static HRESULT WINAPI uwp_tb_get_MaxLines( void *iface, INT32 *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (out) *out = impl ? impl->max_lines : 0;
    return S_OK;
}
static HRESULT WINAPI uwp_tb_put_MaxLines( void *iface, INT32 val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl)
    {
        impl->max_lines = max( 0, val );
        g_layout_dirty = TRUE;
        uwp_invalidate_window();
    }
    return S_OK;
}
static BOOL uwp_tb_is_trimmed_or_overflow( const struct uwp_element *impl )
{
    WCHAR rich_buf[2048];
    const WCHAR *txt;
    double fs, max_w, est_w;
    int lines = 1, nl_lines = 1;
    const WCHAR *p;

    if (!impl || impl->max_lines <= 0) return FALSE;
    txt = impl->text;
    if (!txt[0] && impl->inlines_coll && uwp_flatten_inlines_text( impl, rich_buf, ARRAY_SIZE(rich_buf) ) > 0)
        txt = rich_buf;
    if (!txt[0]) return FALSE;
    for (p = txt; *p; p++) if (*p == L'\n') nl_lines++;
    lines = nl_lines;
    fs = (impl->font_size > 0.0 ? impl->font_size : 14.0);
    max_w = (impl->actual_w > 32.0) ? (impl->actual_w - 16.0) : 600.0;
    if (impl->max_width > 0.0 && impl->max_width < max_w) max_w = impl->max_width;
    est_w = (double)wcslen( txt ) * fs * 0.56;
    if (max_w > 32.0 && est_w > max_w)
    {
        int wrap_lines = (int)((est_w + max_w - 1.0) / max_w);
        if (wrap_lines > lines) lines = wrap_lines;
    }
    return lines > impl->max_lines;
}
static HRESULT WINAPI uwp_tb_get_IsTextTrimmed( void *iface, boolean *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (out) *out = uwp_tb_is_trimmed_or_overflow( impl ) ? TRUE : FALSE;
    return S_OK;
}
static HRESULT WINAPI uwp_rtb_get_HasOverflowContent( void *iface, boolean *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (out) *out = uwp_tb_is_trimmed_or_overflow( impl ) ? TRUE : FALSE;
    return S_OK;
}

const void *uwp_tb_vtbl[64] =
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
    [10 ... 19] = uwp_generic_noop,
    [20] = uwp_tb_get_TextWrapping,  /* get_TextWrapping */
    [21] = uwp_tb_put_TextWrapping,  /* put_TextWrapping */
    [22 ... 23] = uwp_generic_noop,
    [24] = uwp_tb_get_Inlines,
    [25] = uwp_generic_noop,
    [26] = uwp_tb_get_Text,
    [27] = uwp_tb_put_Text,
    [28] = uwp_tb_get_Inlines,       /* get_Inlines */
    [29 ... 33] = uwp_generic_noop,
    [34] = uwp_tb_get_Inlines,
    [35 ... 63] = uwp_generic_noop,
};

const void *uwp_rtb_vtbl[64] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_ctrl_get_FontSize,          /* get_FontSize */
    [7] = uwp_ctrl_put_FontSize,          /* put_FontSize */
    [8] = uwp_ctrl_get_FontFamily,        /* get_FontFamily */
    [9] = uwp_ctrl_put_FontFamily,        /* put_FontFamily */
    [10 ... 19] = uwp_generic_noop,
    [20] = uwp_tb_get_TextWrapping,       /* get_TextWrapping */
    [21] = uwp_tb_put_TextWrapping,       /* put_TextWrapping */
    [22 ... 23] = uwp_generic_noop,
    [24] = uwp_tb_get_Inlines,
    [25 ... 33] = uwp_generic_noop,
    [34] = uwp_tb_get_Inlines,            /* get_Blocks */
    [35] = uwp_rtb_get_HasOverflowContent,/* get_HasOverflowContent */
    [36 ... 63] = uwp_generic_noop,
};

const void *uwp_tb2_vtbl[24] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6 ... 7] = uwp_generic_noop,
    [8] = uwp_tb_get_MaxLines,            /* get_MaxLines */
    [9] = uwp_tb_put_MaxLines,            /* put_MaxLines */
    [10 ... 23] = uwp_generic_noop,
};

const void *uwp_tb6_vtbl[16] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_tb_get_IsTextTrimmed,       /* get_IsTextTrimmed */
    [7 ... 15] = uwp_generic_noop,
};

const void *uwp_paragraph_vtbl[64] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_tb_get_Inlines,
    [7 ... 23] = uwp_generic_noop,
    [24] = uwp_tb_get_Inlines,
    [25 ... 33] = uwp_generic_noop,
    [34] = uwp_tb_get_Inlines,
    [35 ... 63] = uwp_generic_noop,
};

const void *uwp_run_vtbl[16] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_tb_get_Text,
    [7] = uwp_tb_put_Text,
    [8 ... 15] = uwp_generic_noop,
};

static HRESULT WINAPI uwp_binding_get_Path( void *iface, IInspectable **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    *out = impl ? impl->tag : NULL;
    if (*out) IInspectable_AddRef( *out );
    return S_OK;
}

static HRESULT WINAPI uwp_binding_put_Path( void *iface, IInspectable *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return S_OK;
    if (impl->tag != val)
    {
        if (impl->tag) IInspectable_Release( impl->tag );
        impl->tag = val;
        if (val) IInspectable_AddRef( val );
    }
    return S_OK;
}

static HRESULT WINAPI uwp_binding_get_Source( void *iface, IInspectable **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    *out = impl ? impl->data_context : NULL;
    if (*out) IInspectable_AddRef( *out );
    return S_OK;
}

static HRESULT WINAPI uwp_binding_put_Source( void *iface, IInspectable *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return S_OK;
    if (impl->data_context != val)
    {
        if (impl->data_context) IInspectable_Release( impl->data_context );
        impl->data_context = val;
        if (val) IInspectable_AddRef( val );
    }
    return S_OK;
}

const struct uwp_binding_vtbl uwp_binding_vtbl =
{
    uwp_sub_QueryInterface,
    uwp_sub_AddRef,
    uwp_sub_Release,
    uwp_sub_GetIids,
    uwp_sub_GetRuntimeClassName,
    uwp_sub_GetTrustLevel,
    uwp_binding_get_Path,
    uwp_binding_put_Path,
    { [0 ... 5] = uwp_generic_noop },
    uwp_binding_get_Source,
    uwp_binding_put_Source,
    { [0 ... 15] = uwp_generic_noop },
};

static HRESULT WINAPI uwp_pp_get_Path( void *iface, HSTRING *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    if (impl && impl->text[0])
        return WindowsCreateString( impl->text, wcslen( impl->text ), out );
    *out = NULL;
    return S_OK;
}

const struct uwp_pp_vtbl uwp_pp_vtbl =
{
    uwp_sub_QueryInterface,
    uwp_sub_AddRef,
    uwp_sub_Release,
    uwp_sub_GetIids,
    uwp_sub_GetRuntimeClassName,
    uwp_sub_GetTrustLevel,
    uwp_pp_get_Path,
    { [0 ... 8] = uwp_generic_noop },
};

const void *uwp_rowcol_vtbl[32] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_rowcol_get_Length,
    [7] = uwp_rowcol_put_Length,
    [8 ... 31] = uwp_generic_noop,
};

HRESULT WINAPI uwp_frame_NavigateWithParam( void *iface, void *type_name_ptr, void *param, boolean *result );
struct uwp_element *uwp_nav_find_item( struct uwp_element *el, int depth )
{
    if (!el || depth > 6) return NULL;
    if (el->class_name[0] && wcsstr( el->class_name, L".Controls.NavigationViewItem" ) &&
        !wcsstr( el->class_name, L"ViewItemInvoked" ))
        return el;
    if (!wcscmp( el->name, L"MenuItemsHost" ) && el->children_coll)
    {
        struct uwp_vec_view *v = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        if (v->count > 0)
        {
            struct uwp_element *ch = uwp_elem_from_any( v->items[0] );
            if (ch) return ch;
        }
    }
    if (el->content)
    {
        struct uwp_element *r = uwp_nav_find_item( uwp_elem_from_any( el->content ), depth + 1 );
        if (r) return r;
    }
    if (el->children_coll)
    {
        struct uwp_vec_view *v = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        unsigned int k;
        for (k = 0; k < v->count; k++)
        {
            struct uwp_element *r = uwp_nav_find_item( uwp_elem_from_any( v->items[k] ), depth + 1 );
            if (r) return r;
        }
    }
    return NULL;
}

static void uwp_fire_flyout_closed( struct uwp_element *flyout )
{
    int i;
    if (!flyout) return;
    for (i = 0; i < flyout->flyout_closed_cnt; i++)
    {
        if (flyout->flyout_closed_handlers[i])
        {
            IInspectable *sender = flyout->outer ? flyout->outer : &flyout->IFlyout_iface;
            struct xaml_event_handler *h = (struct xaml_event_handler *)flyout->flyout_closed_handlers[i];
            __TRY { h->lpVtbl->Invoke( h, sender, NULL ); }
            __EXCEPT_ALL {}
            __ENDTRY;
        }
    }
}

BOOL uwp_popup_is_open_for( struct uwp_element *flyout )
{
    return flyout && g_popup_hwnd && IsWindow( g_popup_hwnd ) && g_popup_owner_el == flyout;
}

void uwp_close_flyout_popup( BOOL fire_closed )
{
    HWND sub = g_sub_popup_hwnd;
    HWND main_p = g_popup_hwnd;
    struct uwp_element *owner_el = g_popup_owner_el;

    if (owner_el)
    {
        g_last_closed_flyout = owner_el;
        g_last_closed_tick = GetTickCount();
    }
    g_sub_popup_hwnd = NULL;
    g_popup_hwnd = NULL;
    g_popup_owner_el = NULL;

    if (sub && IsWindow( sub )) DestroyWindow( sub );
    if (main_p && IsWindow( main_p )) DestroyWindow( main_p );
    if (fire_closed && owner_el)
        uwp_fire_flyout_closed( owner_el );
    uwp_invalidate_window();
}

BOOL uwp_popup_handle_escape( void )
{
    if (g_sub_popup_hwnd && IsWindow( g_sub_popup_hwnd ))
    {
        HWND sub = g_sub_popup_hwnd;
        g_sub_popup_hwnd = NULL;
        DestroyWindow( sub );
        if (g_popup_hwnd && IsWindow( g_popup_hwnd ))
            SetFocus( g_popup_hwnd );
        return TRUE;
    }
    if (g_popup_hwnd && IsWindow( g_popup_hwnd ))
    {
        struct uwp_popup_state *st = (struct uwp_popup_state *)GetWindowLongPtrW( g_popup_hwnd, GWLP_USERDATA );
        HWND owner = st ? st->owner_hwnd : NULL;
        uwp_close_flyout_popup( TRUE );
        if (owner && IsWindow( owner ))
            SetFocus( owner );
        return TRUE;
    }
    return FALSE;
}

static HWND uwp_create_flyout_popup_internal( struct uwp_element *flyout, struct uwp_element *root_flyout,
                                              HWND owner_hwnd, HWND parent_popup, int screen_x, int screen_y );

static LRESULT CALLBACK uwp_popup_wndproc( HWND hwnd, UINT msg, WPARAM wp, LPARAM lp )
{
    struct uwp_popup_state *st = (struct uwp_popup_state *)GetWindowLongPtrW( hwnd, GWLP_USERDATA );

    if (msg == WM_NCCREATE)
    {
        CREATESTRUCTW *cs = (CREATESTRUCTW *)lp;
        SetWindowLongPtrW( hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams );
        return DefWindowProcW( hwnd, msg, wp, lp );
    }
    if (msg == WM_NCDESTROY)
    {
        if (st)
        {
            SetWindowLongPtrW( hwnd, GWLP_USERDATA, 0 );
            free( st );
        }
        return DefWindowProcW( hwnd, msg, wp, lp );
    }
    if (msg == WM_ERASEBKGND)
        return 1;
    if (msg == WM_APP + 1)
    {
        if (IsWindow( hwnd ) && IsWindowVisible( hwnd ))
            SetFocus( hwnd );
        return 0;
    }
    if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN)
    {
        if (wp == VK_ESCAPE)
        {
            uwp_popup_handle_escape();
            return 0;
        }
        if (wp == VK_DOWN && st && st->item_cnt > 0)
        {
            int k, next = st->hover_idx;
            for (k = 0; k < st->item_cnt; k++)
            {
                next = (next + 1) % st->item_cnt;
                if (!st->items[next].is_sep && st->items[next].enabled)
                {
                    st->hover_idx = next;
                    InvalidateRect( hwnd, NULL, FALSE );
                    break;
                }
            }
            return 0;
        }
        if (wp == VK_UP && st && st->item_cnt > 0)
        {
            int k, prev = st->hover_idx < 0 ? 0 : st->hover_idx;
            for (k = 0; k < st->item_cnt; k++)
            {
                prev = (prev - 1 + st->item_cnt) % st->item_cnt;
                if (!st->items[prev].is_sep && st->items[prev].enabled)
                {
                    st->hover_idx = prev;
                    InvalidateRect( hwnd, NULL, FALSE );
                    break;
                }
            }
            return 0;
        }
        if (wp == VK_RETURN && st && st->hover_idx >= 0 && st->hover_idx < st->item_cnt)
        {
            struct uwp_popup_item *it = &st->items[st->hover_idx];
            if (!it->is_sep && it->enabled && !it->has_sub && it->el)
            {
                struct uwp_element *clicked_el = it->el;
                HWND owner = st->owner_hwnd;
                if (wcsstr( clicked_el->class_name, L"ToggleMenuFlyoutItem" ))
                    clicked_el->is_checked = !clicked_el->is_checked;
                uwp_invoke_button( clicked_el );
                uwp_close_flyout_popup( TRUE );
                if (owner && IsWindow( owner )) SetFocus( owner );
                return 0;
            }
        }
    }
    if (msg == WM_MOUSEMOVE && st)
    {
        int my = (short)HIWORD( lp );
        int i, hit = -1;
        for (i = 0; i < st->item_cnt; i++)
        {
            if (!st->items[i].is_sep && my >= st->items[i].y && my < st->items[i].y + st->items[i].h)
            {
                hit = i;
                break;
            }
        }
        if (hit != st->hover_idx)
        {
            st->hover_idx = hit;
            InvalidateRect( hwnd, NULL, FALSE );
            if (hit >= 0 && st->items[hit].has_sub && st->items[hit].enabled && st->items[hit].el && !st->parent_popup)
            {
                RECT wrc;
                GetWindowRect( hwnd, &wrc );
                if (g_sub_popup_hwnd && IsWindow( g_sub_popup_hwnd ))
                {
                    DestroyWindow( g_sub_popup_hwnd );
                    g_sub_popup_hwnd = NULL;
                }
                g_sub_popup_hwnd = uwp_create_flyout_popup_internal( st->items[hit].el, st->root_flyout,
                                                                     st->owner_hwnd, hwnd,
                                                                     wrc.right - 2, wrc.top + st->items[hit].y );
                st->child_popup = g_sub_popup_hwnd;
            }
        }
        return 0;
    }
    if ((msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP) && st)
    {
        int my = (short)HIWORD( lp );
        int i;
        for (i = 0; i < st->item_cnt; i++)
        {
            if (!st->items[i].is_sep && my >= st->items[i].y && my < st->items[i].y + st->items[i].h)
            {
                if (!st->items[i].enabled) return 0;
                if (st->items[i].has_sub && st->items[i].el && !st->parent_popup)
                {
                    if (msg == WM_LBUTTONUP)
                    {
                        RECT wrc;
                        GetWindowRect( hwnd, &wrc );
                        if (g_sub_popup_hwnd && IsWindow( g_sub_popup_hwnd ))
                        {
                            DestroyWindow( g_sub_popup_hwnd );
                            g_sub_popup_hwnd = NULL;
                        }
                        g_sub_popup_hwnd = uwp_create_flyout_popup_internal( st->items[i].el, st->root_flyout,
                                                                             st->owner_hwnd, hwnd,
                                                                             wrc.right - 2, wrc.top + st->items[i].y );
                        st->child_popup = g_sub_popup_hwnd;
                    }
                    return 0;
                }
                if (msg == WM_LBUTTONUP && st->items[i].el)
                {
                    struct uwp_element *clicked_el = st->items[i].el;
                    HWND owner = st->owner_hwnd;
                    if (wcsstr( clicked_el->class_name, L"ToggleMenuFlyoutItem" ))
                        clicked_el->is_checked = !clicked_el->is_checked;
                    uwp_invoke_button( clicked_el );
                    uwp_close_flyout_popup( TRUE );
                    if (owner && IsWindow( owner )) SetFocus( owner );
                }
                return 0;
            }
        }
        return 0;
    }
    if (msg == WM_KILLFOCUS && st)
    {
        HWND new_focus = (HWND)wp;
        if (new_focus != hwnd &&
            (!g_popup_hwnd || new_focus != g_popup_hwnd) &&
            (!g_sub_popup_hwnd || new_focus != g_sub_popup_hwnd))
        {
            uwp_close_flyout_popup( TRUE );
        }
        return 0;
    }
    if (msg == WM_PAINT && st)
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint( hwnd, &ps );
        if (hdc)
        {
            RECT rc;
            HBRUSH bg_br = CreateSolidBrush( RGB( 44, 44, 44 ) );
            HBRUSH bd_br = CreateSolidBrush( RGB( 72, 72, 72 ) );
            HBRUSH hv_br = CreateSolidBrush( RGB( 62, 62, 62 ) );
            HBRUSH sep_br = CreateSolidBrush( RGB( 66, 66, 66 ) );
            HFONT font = CreateFontW( -13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                      CLEARTYPE_QUALITY, FF_SWISS, L"Segoe UI" );
            HGDIOBJ old_font = SelectObject( hdc, font );
            int i;

            GetClientRect( hwnd, &rc );
            FillRect( hdc, &rc, bg_br );
            FrameRect( hdc, &rc, bd_br );
            SetBkMode( hdc, TRANSPARENT );

            for (i = 0; i < st->item_cnt; i++)
            {
                const struct uwp_popup_item *it = &st->items[i];
                if (it->is_sep)
                {
                    RECT src = { rc.left + 10, it->y + it->h / 2, rc.right - 10, it->y + it->h / 2 + 1 };
                    FillRect( hdc, &src, sep_br );
                }
                else
                {
                    RECT irc = { rc.left + 4, it->y, rc.right - 4, it->y + it->h };
                    RECT trc = { rc.left + 30, it->y, rc.right - 14, it->y + it->h };
                    if (i == st->hover_idx && it->enabled)
                        FillRect( hdc, &irc, hv_br );
                    SetTextColor( hdc, it->enabled ? RGB( 245, 245, 245 ) : RGB( 125, 125, 125 ) );
                    if (it->checked)
                    {
                        RECT crc = { rc.left + 8, it->y, rc.left + 28, it->y + it->h };
                        DrawTextW( hdc, L"\x2713", -1, &crc, DT_CENTER | DT_VCENTER | DT_SINGLELINE );
                    }
                    DrawTextW( hdc, it->text, -1, &trc, DT_LEFT | DT_VCENTER | DT_SINGLELINE );
                    if (it->has_sub)
                    {
                        RECT arc = { rc.right - 26, it->y, rc.right - 10, it->y + it->h };
                        SetTextColor( hdc, RGB( 185, 185, 185 ) );
                        DrawTextW( hdc, L"\x203a", -1, &arc, DT_RIGHT | DT_VCENTER | DT_SINGLELINE );
                    }
                    else if (it->accel[0])
                    {
                        SetTextColor( hdc, it->enabled ? RGB( 170, 170, 170 ) : RGB( 110, 110, 110 ) );
                        DrawTextW( hdc, it->accel, -1, &trc, DT_RIGHT | DT_VCENTER | DT_SINGLELINE );
                    }
                }
            }

            SelectObject( hdc, old_font );
            DeleteObject( font );
            DeleteObject( sep_br );
            DeleteObject( hv_br );
            DeleteObject( bd_br );
            DeleteObject( bg_br );
        }
        EndPaint( hwnd, &ps );
        return 0;
    }
    return DefWindowProcW( hwnd, msg, wp, lp );
}

static HWND uwp_create_flyout_popup_internal( struct uwp_element *flyout, struct uwp_element *root_flyout,
                                              HWND owner_hwnd, HWND parent_popup, int screen_x, int screen_y )
{
    static BOOL cls_reg = FALSE;
    struct uwp_element *raw_items[64];
    unsigned int raw_cnt = 0, k;
    struct uwp_popup_state *st;
    int cur_y = 6, max_w = 220, pop_w, pop_h;
    HDC screen_dc;
    HFONT font = NULL;
    HGDIOBJ old_font = NULL;
    HWND hwnd;

    if (!flyout) return NULL;
    if (flyout->children_coll)
    {
        struct uwp_vec_view *children = CONTAINING_RECORD( flyout->children_coll, struct uwp_vec_view, IInspectable_iface );
        for (k = 0; children && k < children->count && raw_cnt < ARRAY_SIZE(raw_items); k++)
        {
            struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
            if (ch) raw_items[raw_cnt++] = ch;
        }
    }
    if (raw_cnt == 0 && flyout->outer)
    {
        void *mbi = NULL;
        if (SUCCEEDED(IInspectable_QueryInterface( flyout->outer, &IID_IMenuBarItem_WinUI, &mbi )) && mbi)
        {
            void *vec = NULL;
            HRESULT (WINAPI *get_items)(void *, void **) = (void *)(*(void ***)mbi)[8];
            if (get_items && SUCCEEDED(get_items( mbi, &vec )) && vec)
            {
                UINT32 cnt = 0;
                HRESULT (WINAPI *get_at)(void *, UINT32, void **) = (void *)(*(void ***)vec)[6];
                HRESULT (WINAPI *get_size)(void *, UINT32 *) = (void *)(*(void ***)vec)[7];
                if (get_size && SUCCEEDED(get_size( vec, &cnt )))
                {
                    for (k = 0; k < cnt && raw_cnt < ARRAY_SIZE(raw_items); k++)
                    {
                        void *item = NULL;
                        if (get_at && SUCCEEDED(get_at( vec, k, &item )) && item)
                        {
                            struct uwp_element *ch = uwp_elem_from_any( item );
                            if (ch) raw_items[raw_cnt++] = ch;
                            IUnknown_Release( (IUnknown *)item );
                        }
                    }
                }
                IUnknown_Release( (IUnknown *)vec );
            }
            IUnknown_Release( (IUnknown *)mbi );
        }
    }
    if (raw_cnt == 0) return NULL;

    if (!cls_reg)
    {
        WNDCLASSEXW wc = { .cbSize = sizeof(wc) };
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = uwp_popup_wndproc;
        wc.hInstance = GetModuleHandleW( NULL );
        wc.hCursor = LoadCursorW( NULL, (LPCWSTR)IDC_ARROW );
        wc.lpszClassName = L"WineUwpMenuFlyoutPopup";
        RegisterClassExW( &wc );
        cls_reg = TRUE;
    }

    st = calloc( 1, sizeof(*st) );
    if (!st) return NULL;
    st->flyout = flyout;
    st->root_flyout = root_flyout ? root_flyout : flyout;
    st->owner_hwnd = owner_hwnd;
    st->parent_popup = parent_popup;
    st->hover_idx = -1;

    screen_dc = GetDC( NULL );
    if (screen_dc)
    {
        font = CreateFontW( -13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                            CLEARTYPE_QUALITY, FF_SWISS, L"Segoe UI" );
        old_font = SelectObject( screen_dc, font );
    }

    for (k = 0; k < raw_cnt && st->item_cnt < (int)ARRAY_SIZE(st->items); k++)
    {
        struct uwp_element *ch = raw_items[k];
        struct uwp_popup_item *it;
        if (!ch || ch->visibility != 0 || wcsstr( ch->class_name, L"Accelerator" )) continue;

        it = &st->items[st->item_cnt++];
        it->el = ch;
        it->y = cur_y;
        if (wcsstr( ch->class_name, L"Separator" ))
        {
            it->is_sep = TRUE;
            it->h = 9;
            cur_y += it->h;
        }
        else
        {
            SIZE sz_t = {0}, sz_a = {0};
            int row_w;
            it->is_sep = FALSE;
            it->enabled = ch->is_enabled;
            it->checked = (ch->is_checked != 0);
            it->has_sub = (wcsstr( ch->class_name, L"MenuFlyoutSubItem" ) != NULL);
            lstrcpynW( it->text, ch->text, ARRAY_SIZE(it->text) );
            lstrcpynW( it->accel, ch->accel_text, ARRAY_SIZE(it->accel) );
            if (!it->accel[0] && ch->accelerators_coll)
            {
                struct uwp_vec_view *av = CONTAINING_RECORD( ch->accelerators_coll, struct uwp_vec_view, IInspectable_iface );
                if (av->count > 0)
                {
                    struct uwp_element *ka = uwp_elem_from_any( av->items[0] );
                    if (ka && ka->accel_key)
                    {
                        WCHAR kname[32] = {0};
                        if (ka->accel_key >= 'A' && ka->accel_key <= 'Z')
                        { kname[0] = (WCHAR)ka->accel_key; kname[1] = 0; }
                        else if (ka->accel_key >= '0' && ka->accel_key <= '9')
                        { kname[0] = (WCHAR)ka->accel_key; kname[1] = 0; }
                        else if (ka->accel_key == VK_ADD || ka->accel_key == 0xbb)
                            lstrcpyW( kname, L"Plus" );
                        else if (ka->accel_key == VK_SUBTRACT || ka->accel_key == 0xbd)
                            lstrcpyW( kname, L"Minus" );
                        else if (ka->accel_key >= VK_F1 && ka->accel_key <= VK_F12)
                            swprintf( kname, ARRAY_SIZE(kname), L"F%u", ka->accel_key - VK_F1 + 1 );
                        if (kname[0])
                        {
                            swprintf( it->accel, ARRAY_SIZE(it->accel), L"%s%s%s%s",
                                      (ka->accel_mods & 1) ? L"Ctrl+" : L"",
                                      (ka->accel_mods & 4) ? L"Shift+" : L"",
                                      (ka->accel_mods & 2) ? L"Alt+" : L"",
                                      kname );
                        }
                    }
                }
            }
            it->h = 28;
            cur_y += it->h;
            if (screen_dc)
            {
                if (it->text[0]) GetTextExtentPoint32W( screen_dc, it->text, wcslen(it->text), &sz_t );
                if (it->accel[0]) GetTextExtentPoint32W( screen_dc, it->accel, wcslen(it->accel), &sz_a );
                row_w = 36 + sz_t.cx + (sz_a.cx ? (sz_a.cx + 28) : 24) + 18;
                if (row_w > max_w) max_w = min( 360, row_w );
            }
        }
    }

    if (screen_dc)
    {
        if (old_font) SelectObject( screen_dc, old_font );
        if (font) DeleteObject( font );
        ReleaseDC( NULL, screen_dc );
    }

    if (st->item_cnt == 0)
    {
        free( st );
        return NULL;
    }

    pop_w = max_w;
    pop_h = cur_y + 6;
    {
        RECT work = { 0, 0, 1024, 768 };
        HMONITOR mon = owner_hwnd ? MonitorFromWindow( owner_hwnd, MONITOR_DEFAULTTONEAREST )
                                  : MonitorFromPoint( (POINT){ screen_x, screen_y }, MONITOR_DEFAULTTONEAREST );
        if (mon)
        {
            MONITORINFO mi = { .cbSize = sizeof(mi) };
            if (GetMonitorInfoW( mon, &mi )) work = mi.rcWork;
        }
        if (screen_x + pop_w > work.right - 4) screen_x = max( work.left + 4, work.right - pop_w - 4 );
        if (screen_y + pop_h > work.bottom - 4) screen_y = max( work.top + 4, work.bottom - pop_h - 4 );
        if (screen_x < work.left + 4) screen_x = work.left + 4;
        if (screen_y < work.top + 4) screen_y = work.top + 4;
    }

    hwnd = CreateWindowExW( WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
                            L"WineUwpMenuFlyoutPopup", L"",
                            WS_POPUP,
                            screen_x, screen_y, pop_w, pop_h,
                            owner_hwnd, NULL, GetModuleHandleW( NULL ), st );
    if (!hwnd)
    {
        free( st );
        return NULL;
    }
    st->hwnd = hwnd;
    ShowWindow( hwnd, SW_SHOWNOACTIVATE );
    InvalidateRect( hwnd, NULL, FALSE );
    UpdateWindow( hwnd );
    PostMessageW( hwnd, WM_APP + 1, 0, 0 );
    return hwnd;
}

void uwp_show_flyout_popup( struct uwp_element *flyout, HWND owner_hwnd, int screen_x, int screen_y )
{
    uwp_close_flyout_popup( FALSE );
    g_popup_owner_el = flyout;
    g_popup_hwnd = uwp_create_flyout_popup_internal( flyout, flyout, owner_hwnd, NULL, screen_x, screen_y );
    uwp_invalidate_window();
}

/* ============================================================================
 * 8. UWP Helper & Statics Factories (XamlBindingHelper, VisualStateManager, etc.)
 * ============================================================================ */

const GUID IID_IXamlReaderStatics_UWP =
    { 0x9891c6bd, 0x534f, 0x4955, { 0xb8, 0x5a, 0x8a, 0x8d, 0xc0, 0xdc, 0xa6, 0x02 } };

