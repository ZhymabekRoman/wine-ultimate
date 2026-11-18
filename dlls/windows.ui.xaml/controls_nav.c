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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <wctype.h>
#include <excpt.h>
#include "wine/exception.h"
#include "private.h"
#include "xbf_known_types.h"
#include "appmodel.h"
#include "weakreference.h"
#include "winreg.h"
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
    void *handlers[16];
    unsigned int n, i;
    IInspectable *sender = el->outer ? el->outer : &el->IInspectable_iface;

    n = xaml_event_registry_snapshot( reg, handlers, ARRAY_SIZE( handlers ) );
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
    g_nav_args_item = NULL;
}

HRESULT WINAPI uwp_nav_get_IsPaneOpen( void *iface, BOOL *open )
{
    if (open) *open = TRUE;
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
    [57] = uwp_generic_noop,
    [62] = uwp_nav_get_FooterMenuItems,
    [63 ... 101] = uwp_generic_noop,
    [102] = uwp_ui_add_SelectionChanged,
    [103] = uwp_ui_remove_SelectionChanged,
    [104] = uwp_ui_add_ItemInvoked,
    [105] = uwp_ui_remove_ItemInvoked,
    [106 ... 111] = uwp_generic_noop,
};

const void *uwp_fe3_vtbl[16] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_fe3_add_DataContextChanged,
    [7 ... 15] = uwp_generic_noop,
};

const void *uwp_ctrl_vtbl[64] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_ctrl_get_FontSize,
    [7] = uwp_ctrl_put_FontSize,
    [8 ... 21] = uwp_generic_noop,
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
    [6 ... 7] = uwp_generic_noop,
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
        !wcscmp( impl->class_name, L"Microsoft.UI.Xaml.Controls.CommandBarFlyout" ))
    {
        if (tok) tok->value = 1;
        return S_OK;
    }
    if (wcsstr( impl->class_name, L"MenuBarItem" ) != NULL ||
        wcsstr( impl->class_name, L"MenuFlyoutSubItem" ) != NULL)
        return uwp_flyout_get_Items( iface, (void **)arg1 );
    return S_OK;
}

static HRESULT WINAPI uwp_flyout_get_SelectedIndex( void *iface, int *out )
{
    if (out) *out = 0;
    return S_OK;
}

static HRESULT WINAPI uwp_sel_args_QI( void *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    *out = iface;
    return S_OK;
}
static ULONG WINAPI uwp_sel_args_AddRef( void *iface ) { return 2; }
static ULONG WINAPI uwp_sel_args_Release( void *iface ) { return 1; }
static HRESULT WINAPI uwp_sel_args_get_Items( void *iface, void **out )
{
    return uwp_vec_create( NULL, (IInspectable **)out );
}
static const void *uwp_sel_args_vtbl[16] =
{
    [0] = uwp_sel_args_QI,
    [1] = uwp_sel_args_AddRef,
    [2] = uwp_sel_args_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_sel_args_get_Items,
    [7] = uwp_sel_args_get_Items,
    [8 ... 15] = uwp_generic_noop,
};
static struct { const void *vtbl; } g_sel_changed_args = { uwp_sel_args_vtbl };

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
        IInspectable *sender = impl->outer ? impl->outer : &impl->IInspectable_iface;
        struct xaml_event_handler *h = (struct xaml_event_handler *)impl->sel_changed_handler;
        h->lpVtbl->Invoke( h, sender, &g_sel_changed_args );
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
    [13 ... 33] = uwp_generic_noop,
    [34] = uwp_flyout_get_Items,
    [35 ... 44] = uwp_generic_noop,
    [45] = uwp_flyout_get_SelectedIndex,
    [46] = uwp_generic_noop,
    [47] = uwp_tabview_get_SelectedItem,
    [48] = uwp_tabview_put_SelectedItem,
    [49 ... 50] = uwp_generic_noop,
    [51] = uwp_tabview_add_SelectionChanged,
    [52 ... 127] = uwp_generic_noop,
};

static HRESULT WINAPI uwp_selector_get_SelectedIndex( void *iface, INT32 *out )
{
    if (out) *out = 0;
    TRACE( "ISelector::get_SelectedIndex -> 0\n" );
    return S_OK;
}
static HRESULT WINAPI uwp_selector_put_SelectedIndex( void *iface, INT32 idx )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, ISelector_iface );
    TRACE( "ISelector::put_SelectedIndex(%d) handler=%p\n", idx, impl ? impl->sel_changed_handler : NULL );
    if (impl && impl->sel_changed_handler)
    {
        IInspectable *sender = impl->outer ? impl->outer : &impl->IInspectable_iface;
        struct xaml_event_handler *h = (struct xaml_event_handler *)impl->sel_changed_handler;
        h->lpVtbl->Invoke( h, sender, &g_sel_changed_args );
    }
    return S_OK;
}
static HRESULT WINAPI uwp_selector_get_SelectedItem( void *iface, void **out )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, ISelector_iface );
    if (!out) return E_POINTER;
    *out = impl ? impl->selected_item : NULL;
    if (*out) IInspectable_AddRef( (IInspectable *)*out );
    TRACE( "ISelector::get_SelectedItem -> %p\n", *out );
    return S_OK;
}
static HRESULT WINAPI uwp_selector_put_SelectedItem( void *iface, void *item )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, ISelector_iface );
    if (!impl) return S_OK;
    if (impl->selected_item == (IInspectable *)item) return S_OK;
    if (impl->selected_item) IInspectable_Release( impl->selected_item );
    impl->selected_item = (IInspectable *)item;
    if (impl->selected_item) IInspectable_AddRef( impl->selected_item );
    TRACE( "ISelector::put_SelectedItem(%p) handler=%p\n", item, impl->sel_changed_handler );
    if (impl->sel_changed_handler)
    {
        IInspectable *sender = impl->outer ? impl->outer : &impl->IInspectable_iface;
        struct xaml_event_handler *h = (struct xaml_event_handler *)impl->sel_changed_handler;
        h->lpVtbl->Invoke( h, sender, &g_sel_changed_args );
    }
    return S_OK;
}
static HRESULT WINAPI uwp_selector_add_SelectionChanged( void *iface, void *handler, EventRegistrationToken *tok )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, ISelector_iface );
    if (tok) tok->value = 1;
    if (!impl || !handler) return S_OK;
    if (impl->sel_changed_handler) IInspectable_Release( impl->sel_changed_handler );
    impl->sel_changed_handler = (IInspectable *)handler;
    IInspectable_AddRef( impl->sel_changed_handler );
    TRACE( "ISelector::add_SelectionChanged(%p)\n", handler );
    return S_OK;
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
    [17 ... 19] = uwp_generic_noop,
};

static HRESULT WINAPI uwp_items_ctrl_get_Items( void *iface, void **out )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IItemsControl_iface );
    if (!out) return E_POINTER;
    if (!impl) { *out = NULL; return E_NOINTERFACE; }
    *out = &impl->IVector_iface;
    uwp_sub_AddRef( &impl->IVector_iface );
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
    [6 ... 7] = uwp_generic_noop,
    [8] = uwp_items_ctrl_get_Items,
    [9 ... 31] = uwp_generic_noop,
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
static HRESULT WINAPI uwp_cd_ShowAsync( void *iface, void **out )
{
    static BOOL cls_reg = FALSE;
    struct uwp_element *impl = uwp_elem_from_any( iface );
    HWND owner = NULL;
    RECT orc = { 100, 100, 900, 700 };
    int dw = 380, dh = 180, dx, dy;

    if (out) *out = NULL;
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
    [24] = uwp_ui_event_add,
    [25] = uwp_generic_noop,
    [26] = uwp_ui_event_add,
    [27] = uwp_generic_noop,
    [28] = uwp_ui_event_add,
    [29 ... 31] = uwp_generic_noop,
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
        int i;
        for (i = 0; i < impl->tip_closing_cnt; i++)
        {
            if (impl->tip_closing_handlers[i])
            {
                struct xaml_event_handler *h = (struct xaml_event_handler *)impl->tip_closing_handlers[i];
                h->lpVtbl->Invoke( h, sender, &g_sel_changed_args );
            }
        }
        for (i = 0; i < impl->tip_closed_cnt; i++)
        {
            if (impl->tip_closed_handlers[i])
            {
                struct xaml_event_handler *h = (struct xaml_event_handler *)impl->tip_closed_handlers[i];
                h->lpVtbl->Invoke( h, sender, &g_sel_changed_args );
            }
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

const void *uwp_tb_vtbl[64] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_ctrl_get_FontSize,
    [7] = uwp_ctrl_put_FontSize,
    [8 ... 25] = uwp_generic_noop,
    [26] = uwp_tb_get_Text,
    [27] = uwp_tb_put_Text,
    [28 ... 63] = uwp_generic_noop,
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

