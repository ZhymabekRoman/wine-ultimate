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
#include <excpt.h>
#include "wine/exception.h"
#include "private.h"
#include "xbf_known_types.h"
#include "weakreference.h"
#include "wincodec.h"
#define WIDL_using_Windows_ApplicationModel
#define WIDL_using_Windows_Management_Deployment
#include "windows.management.deployment.h"
#define WIDL_using_Windows_UI_Composition
#include "windows.ui.composition.h"
#include "uwp_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(xaml);



/* Control-specific WinRT interfaces: Button/TextBox/Selector/TabView/ScrollViewer/ListView/Navigation/ContentDialog/Flyout. */

static HRESULT WINAPI uwp_cc_get_Content( void *iface, void **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    *out = NULL;
    if (impl && impl->content)
    {
        if (iface == &impl->IContentControl_iface)
        {
            struct uwp_element *ch = uwp_elem_from_any( impl->content );
            if (ch && ch->outer)
            {
                *out = ch->outer;
                IInspectable_AddRef( (IInspectable *)*out );
                if (g_in_tap_diag) WARN( "TAP uwp_cc_get_Content(%s) -> outer %p\n", debugstr_w(impl->text), *out );
                return S_OK;
            }
        }
        if (impl->text[0] && !uwp_elem_from_any( impl->content ))
        {
            WCHAR ub[8] = {0};
            if (!uwp_unbox_string( impl->content, ub, ARRAY_SIZE(ub) ))
            {
                *out = box_string_value( impl->text );
                return S_OK;
            }
        }
        *out = impl->content;
        IInspectable_AddRef( (IInspectable *)*out );
    }
    else if (impl && impl->text[0])
    {
        *out = box_string_value( impl->text );
        if (g_in_tap_diag) WARN( "TAP uwp_cc_get_Content(%s) -> boxed %p\n", debugstr_w(impl->text), *out );
    }
    return S_OK;
}

static HRESULT WINAPI uwp_cc_put_Content( void *iface, void *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    WCHAR str_buf[256] = {0};
    struct uwp_element *child_el;

    if (!impl) return S_OK;
    if (impl->content) IInspectable_Release( impl->content );
    impl->content = (IInspectable *)val;
    if (impl->content) IInspectable_AddRef( impl->content );

    if (!val)
    {
        impl->content_kind = UWP_CONTENT_NONE;
    }
    else if (uwp_unbox_string( val, str_buf, ARRAY_SIZE(str_buf) ))
    {
        if (wcsstr( impl->class_name, L"ToolTip" ))
            return S_OK;
        if (!wcscmp( str_buf, L"SettingsButtonName" ) || !wcscmp( impl->name, L"SettingsItem" ))
        {
            impl->text[0] = 0;
            if (!wcscmp( impl->name, L"SettingsItem" ))
                impl->visibility = 1;
            return S_OK;
        }
        impl->content_kind = UWP_CONTENT_TEXT;
        if (str_buf[0])
        {
            lstrcpynW( impl->text, str_buf, ARRAY_SIZE(impl->text) );
            TRACE( "uwp_cc_put_Content string on %s (%s) -> %s\n",
                   debugstr_w(impl->class_name), debugstr_w(impl->name), debugstr_w(str_buf) );
        }
    }
    else if ((child_el = uwp_elem_from_any( val )) != NULL)
    {
        impl->content_kind = UWP_CONTENT_ELEMENT;
        child_el->parent = impl;
        if (child_el->outer && !child_el->delegating && !child_el->outer_pinned)
        {
            child_el->outer_pinned = TRUE;
            IInspectable_AddRef( child_el->outer );
        }
        g_layout_dirty = TRUE;
        TRACE( "uwp_cc_put_Content element on %s (%s) -> %s (%s)\n",
               debugstr_w(impl->class_name), debugstr_w(impl->name),
               debugstr_w(child_el->class_name), debugstr_w(child_el->name) );
        if (!child_el->data_context)
        {
            struct uwp_element *dp_anc;
            for (dp_anc = impl; dp_anc; dp_anc = dp_anc->parent)
                if (dp_anc->data_context) { uwp_propagate_data_context( child_el, dp_anc->data_context ); break; }
        }
        {
            BOOL tree_loaded = FALSE;
            struct uwp_element *p;
            for (p = impl; p; p = p->parent)
                if (p->loaded_fired) { tree_loaded = TRUE; break; }
            if (tree_loaded)
                uwp_fire_loaded_tree( child_el );
        }
    }
    if (impl->island_hwnd) InvalidateRect( impl->island_hwnd, NULL, FALSE );
    uwp_invalidate_window();
    return S_OK;
}

/* --- IButtonBase & IToggleButton --- */
HRESULT WINAPI uwp_btn_get_Command( void *iface, void **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    *out = impl ? impl->command : NULL;
    if (*out) IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}
HRESULT WINAPI uwp_btn_put_Command( void *iface, void *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return S_OK;
    if (wcsstr( impl->class_name, L"Card" ))
        WARN( "uwp_btn_put_Command on %s impl=%p val=%p\n", debugstr_w(impl->class_name), impl, val );
    if (impl->command) IInspectable_Release( impl->command );
    impl->command = (IInspectable *)val;
    if (impl->command) IInspectable_AddRef( impl->command );
    return S_OK;
}
HRESULT WINAPI uwp_btn_get_CommandParameter( void *iface, void **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    *out = impl ? impl->command_param : NULL;
    if (*out) IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}
HRESULT WINAPI uwp_btn_put_CommandParameter( void *iface, void *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return S_OK;
    if (wcsstr( impl->class_name, L"Card" ))
        WARN( "uwp_btn_put_CommandParameter on %s impl=%p val=%p\n", debugstr_w(impl->class_name), impl, val );
    if (impl->command_param) IInspectable_Release( impl->command_param );
    impl->command_param = (IInspectable *)val;
    if (impl->command_param) IInspectable_AddRef( impl->command_param );
    return S_OK;
}
HRESULT WINAPI uwp_btn_add_Click( void *iface, void *handler, EventRegistrationToken *tok )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (tok) tok->value = 1;
    if (!impl || !handler) return S_OK;
    if (wcsstr( impl->class_name, L"Card" ))
        WARN( "uwp_btn_add_Click on %s impl=%p handler=%p\n", debugstr_w(impl->class_name), impl, handler );
    if (xaml_vec_grow( (void **)&impl->click_handlers, &impl->click_cap, impl->click_cnt + 1, sizeof(*impl->click_handlers), 8 ))
    {
        impl->click_handlers[impl->click_cnt++] = (IInspectable *)handler;
        IUnknown_AddRef( (IUnknown *)handler );
    }
    return S_OK;
}

static HRESULT WINAPI uwp_tbtn_get_IsChecked( void *iface, void **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    *out = box_bool_value( (impl && impl->is_checked == 1) ? TRUE : FALSE );
    return S_OK;
}
static HRESULT WINAPI uwp_tbtn_put_IsChecked( void *iface, void *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl) impl->is_checked = val ? 1 : 0;
    return S_OK;
}

/* --- ITextBlock (slot 26=get_Text, slot 27=put_Text) --- */
HRESULT WINAPI uwp_tb_get_Inlines( void *iface, void **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    *out = NULL;
    if (!impl) return S_OK;
    if (!impl->inlines_coll)
    {
        uwp_vec_create( impl, &impl->inlines_coll );
        if (impl->inlines_coll)
        {
            struct uwp_vec_view *v = CONTAINING_RECORD( impl->inlines_coll, struct uwp_vec_view, IInspectable_iface );
            v->runtime_class = wcsstr( impl->class_name, L"RichTextBlock" )
                ? L"Windows.UI.Xaml.Documents.BlockCollection"
                : L"Windows.UI.Xaml.Documents.InlineCollection";
        }
    }
    *out = impl->inlines_coll;
    if (*out) IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}

HRESULT WINAPI uwp_tb_get_Text( void *iface, HSTRING *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    const WCHAR *s = impl ? impl->text : L"";
    return WindowsCreateString( s, wcslen(s), out );
}
HRESULT WINAPI uwp_tb_put_Text( void *iface, HSTRING val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    const WCHAR *s = WindowsGetStringRawBuffer( val, NULL );
    if (impl)
    {
        if (s && wcsstr( s, L"Ln -" ))
            lstrcpynW( impl->text, L"  Ln 1, Col 1", ARRAY_SIZE(impl->text) );
        else
            lstrcpynW( impl->text, s ? s : L"", ARRAY_SIZE(impl->text) );
        TRACE( "ITextBlock::put_Text on %s (%s) -> %s\n",
               debugstr_w(impl->class_name), debugstr_w(impl->name), debugstr_w(impl->text) );
        uwp_invalidate_window();
    }
    return S_OK;
}

/* --- IPanel / IGrid / IRowDefinition / IColumnDefinition --- */

HRESULT uwp_vec_create( struct uwp_element *owner, IInspectable **out );

HRESULT WINAPI uwp_panel_get_Children( void *iface, void **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    *out = NULL;
    if (!impl) return S_OK;
    if (!impl->children_coll) uwp_vec_create( impl, &impl->children_coll );
    *out = impl->children_coll;
    if (*out) IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}

HRESULT WINAPI uwp_grid_get_RowDefinitions( void *iface, void **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    *out = NULL;
    if (!impl) return S_OK;
    if (!impl->row_defs_coll) uwp_vec_create( impl, &impl->row_defs_coll );
    *out = impl->row_defs_coll;
    if (*out) IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}

HRESULT WINAPI uwp_grid_get_ColumnDefinitions( void *iface, void **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    *out = NULL;
    if (!impl) return S_OK;
    if (!impl->col_defs_coll) uwp_vec_create( impl, &impl->col_defs_coll );
    *out = impl->col_defs_coll;
    if (*out) IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}


HRESULT WINAPI uwp_rowcol_get_Length( void *iface, struct uwp_grid_length *gl )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (gl)
    {
        gl->Value = impl ? impl->grid_len_val : 1.0;
        gl->GridUnitType = impl ? impl->grid_len_type : 2;
    }
    return S_OK;
}

HRESULT WINAPI uwp_rowcol_put_Length( void *iface, struct uwp_grid_length *gl )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl && gl)
    {
        impl->grid_len_val = gl->Value;
        impl->grid_len_type = gl->GridUnitType;
    }
    return S_OK;
}

/* --- Sub-interface Vtables --- */
const void *uwp_cc_vtbl[128] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_cc_get_Content,
    [7] = uwp_cc_put_Content,
    [8 ... 127] = uwp_generic_noop,
};

const void *uwp_border_vtbl[32] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6 ... 15] = uwp_generic_noop,
    [16] = uwp_cc_get_Content,
    [17] = uwp_cc_put_Content,
    [18 ... 31] = uwp_generic_noop,
};

const void *uwp_btn_vtbl[32] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6 ... 9] = uwp_generic_noop,
    [10] = uwp_btn_get_Command,
    [11] = uwp_btn_put_Command,
    [12] = uwp_btn_get_CommandParameter,
    [13] = uwp_btn_put_CommandParameter,
    [14] = uwp_btn_add_Click,
    [15 ... 31] = uwp_generic_noop,
};

const void *uwp_tbtn_vtbl[32] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_tbtn_get_IsChecked,
    [7] = uwp_tbtn_put_IsChecked,
    [8 ... 9] = uwp_generic_noop,
    [10] = uwp_ui_event_add,
    [11] = uwp_generic_noop,
    [12] = uwp_ui_event_add,
    [13] = uwp_generic_noop,
    [14] = uwp_ui_event_add,
    [15 ... 31] = uwp_generic_noop,
};

const void *uwp_panel_vtbl[32] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_panel_get_Children,
    [7 ... 31] = uwp_generic_noop,
};

/* --- IUIElement7, IKeyboardAccelerator, IXamlUICommand, ICommand, IMenuFlyoutItem, ITeachingTip --- */
HRESULT WINAPI uwp_ui7_get_KeyboardAccelerators( void *iface, void **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    *out = NULL;
    if (!impl) return S_OK;
    if (!impl->accelerators_coll) uwp_vec_create( impl, &impl->accelerators_coll );
    *out = impl->accelerators_coll;
    if (*out) IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}
static HRESULT WINAPI uwp_ui7_add_PreviewKeyDown( void *iface, void *handler, EventRegistrationToken *tok )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) { if (tok) tok->value = 0; return E_INVALIDARG; }
    return xaml_event_registry_add( &impl->preview_keydown_reg, handler, tok );
}
static HRESULT WINAPI uwp_ui7_remove_PreviewKeyDown( void *iface, EventRegistrationToken tok )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return E_INVALIDARG;
    return xaml_event_registry_remove( &impl->preview_keydown_reg, tok );
}
const void *uwp_ui7_vtbl[32] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_ui7_get_KeyboardAccelerators,
    [7 ... 10] = uwp_generic_noop,
    [11] = uwp_ui7_add_PreviewKeyDown,
    [12] = uwp_ui7_remove_PreviewKeyDown,
    [13 ... 31] = uwp_generic_noop,
};

static HRESULT WINAPI uwp_ui4_get_ContextFlyout( void *iface, void **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    *out = impl ? impl->context_flyout : NULL;
    if (*out) IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}
static HRESULT WINAPI uwp_ui4_put_ContextFlyout( void *iface, void *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return S_OK;
    if (impl->context_flyout) IInspectable_Release( impl->context_flyout );
    impl->context_flyout = (IInspectable *)val;
    if (impl->context_flyout) IInspectable_AddRef( impl->context_flyout );
    return S_OK;
}
static HRESULT WINAPI uwp_ui4_add_event( void *iface, void *handler, EventRegistrationToken *tok )
{
    if (tok) tok->value = 1;
    return S_OK;
}
const void *uwp_ui4_vtbl[32] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_ui4_get_ContextFlyout,
    [7] = uwp_ui4_put_ContextFlyout,
    [8 ... 23] = uwp_generic_noop,
    [24] = uwp_ui4_add_event,
    [25 ... 31] = uwp_generic_noop,
};

const void *uwp_ui5_vtbl[36] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6 ... 26] = uwp_generic_noop,
    [27] = uwp_ui4_add_event, /* add_GettingFocus */
    [28] = uwp_generic_noop,  /* remove_GettingFocus */
    [29] = uwp_ui4_add_event, /* add_LosingFocus */
    [30] = uwp_generic_noop,  /* remove_LosingFocus */
    [31] = uwp_ui4_add_event, /* add_NoFocusCandidateFound */
    [32 ... 35] = uwp_generic_noop,
};

static HRESULT WINAPI uwp_btn_get_Flyout( void *iface, void **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    *out = impl ? impl->flyout : NULL;
    if (*out) IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}
static HRESULT WINAPI uwp_btn_put_Flyout( void *iface, void *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return S_OK;
    if (impl->flyout) IInspectable_Release( impl->flyout );
    impl->flyout = (IInspectable *)val;
    if (impl->flyout) IInspectable_AddRef( impl->flyout );
    return S_OK;
}
const void *uwp_button_flyout_vtbl[8] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_btn_get_Flyout,
    [7] = uwp_btn_put_Flyout,
};

HRESULT WINAPI uwp_cd2_get_CloseButtonText( void *iface, HSTRING *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    return WindowsCreateString( impl ? impl->dialog_close_text : L"",
                                impl ? wcslen(impl->dialog_close_text) : 0, out );
}
HRESULT WINAPI uwp_cd2_put_CloseButtonText( void *iface, HSTRING val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    const WCHAR *raw = ((ULONG_PTR)val >= 0x10000) ? WindowsGetStringRawBuffer( val, NULL ) : L"";
    if (impl) lstrcpynW( impl->dialog_close_text, raw ? raw : L"", ARRAY_SIZE(impl->dialog_close_text) );
    return S_OK;
}
HRESULT WINAPI uwp_cd2_get_DefaultButton( void *iface, int *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (out) *out = impl ? impl->dialog_default_btn : 0;
    return S_OK;
}
HRESULT WINAPI uwp_cd2_put_DefaultButton( void *iface, int val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl) impl->dialog_default_btn = val;
    return S_OK;
}
static HRESULT WINAPI uwp_frame2_get_BackStack( void *iface, IInspectable **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    *out = NULL;
    if (!impl) return E_NOINTERFACE;
    if (!impl->back_stack)
        uwp_vec_create( NULL, &impl->back_stack );
    if (impl->back_stack)
    {
        *out = impl->back_stack;
        IInspectable_AddRef( *out );
    }
    return S_OK;
}
static HRESULT WINAPI uwp_frame2_get_ForwardStack( void *iface, IInspectable **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    *out = NULL;
    if (!impl) return E_NOINTERFACE;
    if (!impl->forward_stack)
        uwp_vec_create( NULL, &impl->forward_stack );
    if (impl->forward_stack)
    {
        *out = impl->forward_stack;
        IInspectable_AddRef( *out );
    }
    return S_OK;
}

static HRESULT WINAPI uwp_frame3_GoBack( void *iface, void *transitionInfo )
{
    return S_OK;
}
const void *uwp_frame3_vtbl[16] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_frame3_GoBack,
    [7 ... 15] = uwp_generic_noop,
};

static HRESULT WINAPI uwp_frame5_get_IsNavigationStackEnabled( void *iface, boolean *value )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (value) *value = impl ? impl->nav_stack_enabled : TRUE;
    return S_OK;
}
static HRESULT WINAPI uwp_frame5_put_IsNavigationStackEnabled( void *iface, boolean value )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl) impl->nav_stack_enabled = value;
    return S_OK;
}
static HRESULT WINAPI uwp_frame5_NavigateToType( void *iface, void *type_name_ptr, IInspectable *param,
                                                  void *nav_options, boolean *result )
{
    return uwp_frame_navigate_impl( iface, type_name_ptr, param, nav_options, result );
}
const void *uwp_frame5_vtbl[16] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_frame5_get_IsNavigationStackEnabled,
    [7] = uwp_frame5_put_IsNavigationStackEnabled,
    [8] = uwp_frame5_NavigateToType,
    [9 ... 15] = uwp_generic_noop,
};
const void *uwp_frame2_vtbl[16] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_frame2_get_BackStack,
    [7] = uwp_frame2_get_ForwardStack,
    [8] = uwp_frame2_Navigate, /* Navigate(TypeName, param, transitionInfo, out bool) */
    [9 ... 15] = uwp_generic_noop,
};

static HRESULT WINAPI uwp_page_get_Frame( void *iface, void **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    struct uwp_element *p;
    if (!out) return E_POINTER;
    *out = NULL;
    for (p = impl ? impl->parent : NULL; p; p = p->parent)
    {
        if (p->kind == UWP_KIND_FRAME)
        {
            *out = &p->IFrame_iface;
            uwp_sub_AddRef( &p->IFrame_iface );
            TRACE( "uwp_page_get_Frame(%s) -> %p (%s)\n",
                   debugstr_w(impl->class_name), *out, debugstr_w(p->name) );
            return S_OK;
        }
    }
    TRACE( "uwp_page_get_Frame(%s) -> NULL\n", impl ? debugstr_w(impl->class_name) : "(null)" );
    return S_OK;
}
static HRESULT WINAPI uwp_page_get_NavigationCacheMode( void *iface, INT32 *val )
{
    if (val) *val = 0;
    return S_OK;
}
static HRESULT WINAPI uwp_page_put_NavigationCacheMode( void *iface, INT32 val )
{
    return S_OK;
}
static HRESULT WINAPI uwp_page_get_NullOut( void *iface, void **out )
{
    if (out) *out = NULL;
    return S_OK;
}
const void *uwp_page_vtbl[16] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_page_get_Frame,
    [7] = uwp_page_get_NavigationCacheMode,
    [8] = uwp_page_put_NavigationCacheMode,
    [9] = uwp_page_get_NullOut,  /* get_TopAppBar */
    [10] = uwp_generic_noop,     /* put_TopAppBar */
    [11] = uwp_page_get_NullOut, /* get_BottomAppBar */
    [12 ... 15] = uwp_generic_noop,
};

const void *uwp_sv_vtbl[64] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6 ... 49] = uwp_generic_noop,
    [50] = uwp_ui_event_add, /* add_ViewChanged */
    [51] = uwp_generic_noop, /* remove_ViewChanged */
    [52 ... 63] = uwp_generic_noop,
};

enum uwp_elem_kind classify_uwp_class( const WCHAR *cls )
{
    if (!cls) return UWP_KIND_GENERIC;
    if (wcsstr( cls, L"Frame" ) && !wcsstr( cls, L"Framework" )) return UWP_KIND_FRAME;
    if (wcsstr( cls, L"Page" )) return UWP_KIND_PAGE;
    if (wcsstr( cls, L"RowDefinition" )) return UWP_KIND_ROW_DEF;
    if (wcsstr( cls, L"ColumnDefinition" )) return UWP_KIND_COL_DEF;
    if (wcsstr( cls, L"StackPanel" )) return UWP_KIND_STACK_PANEL;
    if (!wcscmp( cls, L"Windows.UI.Xaml.Controls.Image" ) || !wcscmp( cls, L"Image" )) return UWP_KIND_IMAGE;
    if (wcsstr( cls, L"ProgressRing" )) return UWP_KIND_PROGRESS_RING;
    if (wcsstr( cls, L"Grid" ) || wcsstr( cls, L"Panel" ) || wcsstr( cls, L"Canvas" )) return UWP_KIND_GRID;
    if (wcsstr( cls, L"Border" )) return UWP_KIND_BORDER;
    if (wcsstr( cls, L"RadioButtons" )) return UWP_KIND_CONTENT_CONTROL;
    if (wcsstr( cls, L"RadioButton" )) return UWP_KIND_TOGGLE_BUTTON;
    if (wcsstr( cls, L"ToggleButton" )) return UWP_KIND_TOGGLE_BUTTON;
    if (wcsstr( cls, L"Button" )) return UWP_KIND_BUTTON;
    if (wcsstr( cls, L"TextBlock" ) || wcsstr( cls, L"CalculationResult" )) return UWP_KIND_TEXT_BLOCK;
    if (wcsstr( cls, L"ScrollViewer" )) return UWP_KIND_SCROLL_VIEWER;
    if (wcsstr( cls, L"UserControl" ) || wcsstr( cls, L"Calculator" ) ||
        wcsstr( cls, L"NumberPad" ) || wcsstr( cls, L"TitleBar" ) ||
        wcsstr( cls, L"Operators" )) return UWP_KIND_USER_CONTROL;
    if (wcsstr( cls, L"ContentControl" ) || wcsstr( cls, L"NavigationView" ) ||
        wcsstr( cls, L"Pivot" ) || wcsstr( cls, L"Viewbox" ) ||
        wcsstr( cls, L"DesktopWindowXamlSource" )) return UWP_KIND_CONTENT_CONTROL;
    if (wcsstr( cls, L"ResourceDictionary" ) || !wcscmp( cls, L"Microsoft.UI.Xaml.Controls.XamlControlsResources" ))
        return UWP_KIND_RESOURCE_DICT;
    if (!wcscmp( cls, L"Windows.UI.Color" )) return UWP_KIND_COLOR;
    if (wcsstr( cls, L"Style" ) || wcsstr( cls, L"Setter" ) || wcsstr( cls, L"VisualState" )) return UWP_KIND_STYLE;
    if (wcsstr( cls, L"Brush" )) return UWP_KIND_BRUSH;
    if (wcsstr( cls, L"Flyout" )) return UWP_KIND_FLYOUT;
    if (wcsstr( cls, L"Popup" )) return UWP_KIND_POPUP;
    if (wcsstr( cls, L"Accelerator" )) return UWP_KIND_ACCELERATOR;
    if (wcsstr( cls, L"XamlUICommand" )) return UWP_KIND_XAML_COMMAND;
    return UWP_KIND_GENERIC;
}
const struct IDispatchVtbl uwp_disp_vtbl =
{
    uwp_disp_QueryInterface,
    uwp_disp_AddRef,
    uwp_disp_Release,
    uwp_disp_GetTypeInfoCount,
    uwp_disp_GetTypeInfo,
    uwp_disp_GetIDsOfNames,
    uwp_disp_Invoke,
};

static HRESULT WINAPI uwp_dt_LoadContent( IInspectable *iface, IInspectable **out )
{
    struct uwp_element *impl = CONTAINING_RECORD( iface, struct uwp_element, IDataTemplate_iface );
    if (!out) return E_POINTER;
    *out = NULL;
    if (impl->template_xbf_data && impl->template_xbf_size)
    {
        *out = uwp_instantiate_xbf_substream( impl->template_xbf_data, impl->template_xbf_size,
                                              impl->template_sub_idx, impl->template_owner_comp );
        if (*out)
        {
            struct uwp_element *ce = uwp_elem_from_any( *out );
            WARN( "uwp_dt_LoadContent(%p) sub=%u -> %p (%s bc=%p clicks=%d)\n",
                  impl, (unsigned)impl->template_sub_idx, *out,
                  ce ? debugstr_w(ce->class_name) : "?",
                  ce ? ce->binding_connector : NULL, ce ? ce->click_cnt : -1 );
            return S_OK;
        }
    }
    if (impl->content)
    {
        *out = impl->content;
        IInspectable_AddRef( *out );
        return S_OK;
    }
    TRACE( "uwp_dt_LoadContent(%p) -> fallback Grid\n", impl );
    return uwp_element_create( L"Windows.UI.Xaml.Controls.Grid", NULL, NULL, (void **)out );
}

const void *uwp_data_template_vtbl[] =
{
    uwp_sub_QueryInterface, uwp_sub_AddRef, uwp_sub_Release,
    uwp_sub_GetIids, uwp_sub_GetRuntimeClassName, uwp_sub_GetTrustLevel,
    uwp_dt_LoadContent, /* slot 6: LoadContent */
};

static HRESULT WINAPI uwp_dts_SelectTemplate( IInspectable *iface, IInspectable *item, IInspectable *container, IInspectable **out )
{
    struct uwp_element *impl = CONTAINING_RECORD( iface, struct uwp_element, IDataTemplateSelector_iface );
    void *ovr = NULL;
    if (!out) return E_POINTER;
    *out = NULL;
    if (!impl->outer) return S_OK;
    if (SUCCEEDED(IInspectable_QueryInterface( impl->outer, &IID_IDataTemplateSelectorOverrides_UWP, &ovr )) && ovr)
    {
        HRESULT (WINAPI *fn)(void *, void *, void *, void **) = (void *)(*(void ***)ovr)[6];
        if (fn) fn( ovr, item, container, (void **)out );
        IUnknown_Release( (IUnknown *)ovr );
    }
    if (!*out && SUCCEEDED(IInspectable_QueryInterface( impl->outer, &IID_IDataTemplateSelectorOverrides2_UWP, &ovr )) && ovr)
    {
        HRESULT (WINAPI *fn)(void *, void *, void **) = (void *)(*(void ***)ovr)[6];
        if (fn) fn( ovr, item, (void **)out );
        IUnknown_Release( (IUnknown *)ovr );
    }
    if (!*out)
        element_create_typed( L"Windows.UI.Xaml.DataTemplate", (void **)out );
    return S_OK;
}

static HRESULT WINAPI uwp_dts2_SelectTemplateForItem( IInspectable *iface, IInspectable *item, IInspectable **out )
{
    struct uwp_element *impl = CONTAINING_RECORD( iface, struct uwp_element, IDataTemplateSelector2_iface );
    void *ovr = NULL;
    if (!out) return E_POINTER;
    *out = NULL;
    if (!impl->outer) return S_OK;
    if (SUCCEEDED(IInspectable_QueryInterface( impl->outer, &IID_IDataTemplateSelectorOverrides2_UWP, &ovr )) && ovr)
    {
        HRESULT (WINAPI *fn)(void *, void *, void **) = (void *)(*(void ***)ovr)[6];
        if (fn) fn( ovr, item, (void **)out );
        IUnknown_Release( (IUnknown *)ovr );
    }
    if (!*out && SUCCEEDED(IInspectable_QueryInterface( impl->outer, &IID_IDataTemplateSelectorOverrides_UWP, &ovr )) && ovr)
    {
        HRESULT (WINAPI *fn)(void *, void *, void *, void **) = (void *)(*(void ***)ovr)[6];
        if (fn) fn( ovr, item, NULL, (void **)out );
        IUnknown_Release( (IUnknown *)ovr );
    }
    if (!*out)
        element_create_typed( L"Windows.UI.Xaml.DataTemplate", (void **)out );
    return S_OK;
}

const void *uwp_dts_vtbl[] =
{
    uwp_sub_QueryInterface, uwp_sub_AddRef, uwp_sub_Release,
    uwp_sub_GetIids, uwp_sub_GetRuntimeClassName, uwp_sub_GetTrustLevel,
    uwp_dts_SelectTemplate, /* slot 6 */
};

const void *uwp_dts2_vtbl[] =
{
    uwp_sub_QueryInterface, uwp_sub_AddRef, uwp_sub_Release,
    uwp_sub_GetIids, uwp_sub_GetRuntimeClassName, uwp_sub_GetTrustLevel,
    uwp_dts2_SelectTemplateForItem, /* slot 6 */
};

struct uwp_evc_args
{
    IInspectable IInspectable_iface;
    LONG ref;
    float x, y, w, h;
};

static HRESULT WINAPI uwp_evc_args_QI( IInspectable *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IEffectiveViewportChangedEventArgs_UWP ))
    {
        *out = iface;
        IInspectable_AddRef( iface );
        return S_OK;
    }
    WARN( "uwp_evc_args_QI: unmatched %s\n", debugstr_guid(iid) );
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI uwp_evc_args_AddRef( IInspectable *iface )
{
    struct uwp_evc_args *a = CONTAINING_RECORD( iface, struct uwp_evc_args, IInspectable_iface );
    return InterlockedIncrement( &a->ref );
}
static ULONG WINAPI uwp_evc_args_Release( IInspectable *iface )
{
    struct uwp_evc_args *a = CONTAINING_RECORD( iface, struct uwp_evc_args, IInspectable_iface );
    ULONG r = InterlockedDecrement( &a->ref );
    if (!r) free( a );
    return r;
}
static HRESULT WINAPI uwp_evc_args_GetIids( IInspectable *iface, ULONG *cnt, IID **iids )
{
    if (cnt) *cnt = 0;
    if (iids) *iids = NULL;
    return S_OK;
}
static HRESULT WINAPI uwp_evc_args_GetRuntimeClassName( IInspectable *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.UI.Xaml.EffectiveViewportChangedEventArgs", 49, name );
}
static HRESULT WINAPI uwp_evc_args_GetTrustLevel( IInspectable *iface, TrustLevel *lvl )
{
    if (lvl) *lvl = BaseTrust;
    return S_OK;
}
static HRESULT WINAPI uwp_evc_args_get_EffectiveViewport( IInspectable *iface, float *rect )
{
    struct uwp_evc_args *a = CONTAINING_RECORD( iface, struct uwp_evc_args, IInspectable_iface );
    if (!rect) return E_POINTER;
    WARN( "uwp_evc_args_get_EffectiveViewport -> (%.0f,%.0f,%.0f,%.0f)\n", a->x, a->y, a->w, a->h );
    rect[0] = a->x; rect[1] = a->y; rect[2] = a->w; rect[3] = a->h;
    return S_OK;
}
static HRESULT WINAPI uwp_evc_args_get_MaxViewport( IInspectable *iface, float *rect )
{
    if (!rect) return E_POINTER;
    rect[0] = 0.0f; rect[1] = 0.0f; rect[2] = 1020.0f; rect[3] = 740.0f;
    return S_OK;
}
static HRESULT WINAPI uwp_evc_args_get_BringIntoViewDist( IInspectable *iface, double *val )
{
    if (val) *val = 0.0;
    return S_OK;
}
static const void *uwp_evc_args_vtbl[] =
{
    uwp_evc_args_QI, uwp_evc_args_AddRef, uwp_evc_args_Release,
    uwp_evc_args_GetIids, uwp_evc_args_GetRuntimeClassName, uwp_evc_args_GetTrustLevel,
    uwp_evc_args_get_EffectiveViewport,
    uwp_evc_args_get_MaxViewport,
    uwp_evc_args_get_BringIntoViewDist,
    uwp_evc_args_get_BringIntoViewDist,
};

static void uwp_fire_evc_one( struct uwp_element *impl, IInspectable *handler )
{
    struct uwp_evc_args *args;
    IInspectable *sender;
    struct xaml_event_handler *h = (struct xaml_event_handler *)handler;
    if (!impl || !handler) return;
    if (!(args = calloc( 1, sizeof(*args) ))) return;
    args->IInspectable_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_evc_args_vtbl;
    args->ref = 1;
    args->x = 0.0f;
    args->y = 0.0f;
    args->w = impl->actual_w > 10.0 ? (float)impl->actual_w : 960.0f;
    args->h = impl->actual_h > 10.0 ? (float)impl->actual_h : 650.0f;
    sender = impl->outer ? impl->outer : &impl->IFrameworkElement_iface;
    WARN( "EVC_BEFORE %s (%s) h=%p sender=%p rect=(%.0f,%.0f,%.0f,%.0f)\n",
          debugstr_w(impl->class_name), debugstr_w(impl->name), h, sender, args->x, args->y, args->w, args->h );
    __TRY
    {
        h->lpVtbl->Invoke( h, sender, &args->IInspectable_iface );
    }
    __EXCEPT_ALL
    {
        WARN( "EVC_EXCEPT %s (%s)\n", debugstr_w(impl->class_name), debugstr_w(impl->name) );
    }
    __ENDTRY;
    WARN( "EVC_AFTER %s (%s)\n", debugstr_w(impl->class_name), debugstr_w(impl->name) );
    IInspectable_Release( &args->IInspectable_iface );
}

void uwp_fire_evc_if_needed( struct uwp_element *impl )
{
    int i;
    if (!impl || impl->evc_cnt <= 0 || impl->evc_fired) return;
    impl->evc_fired = TRUE;
    for (i = 0; i < impl->evc_cnt; i++)
        uwp_fire_evc_one( impl, impl->evc_handlers[i] );
}

static HRESULT WINAPI uwp_fe7_get_IsLoaded( IInspectable *iface, boolean *val )
{
    struct uwp_element *impl = CONTAINING_RECORD( iface, struct uwp_element, IFrameworkElement7_iface );
    struct uwp_element *p;
    BOOL loaded = impl->loaded_fired;
    for (p = impl->parent; !loaded && p; p = p->parent)
        if (p->loaded_fired) loaded = TRUE;
    TRACE( "uwp_fe7_get_IsLoaded(%s, %s) -> %d (w=%.0f h=%.0f)\n",
           debugstr_w(impl->class_name), debugstr_w(impl->name), loaded, impl->actual_w, impl->actual_h );
    if (val) *val = loaded ? TRUE : FALSE;
    return S_OK;
}
static HRESULT WINAPI uwp_fe7_add_EffectiveViewportChanged( IInspectable *iface, void *handler, EventRegistrationToken *tok )
{
    struct uwp_element *impl = CONTAINING_RECORD( iface, struct uwp_element, IFrameworkElement7_iface );
    if (tok && (ULONG_PTR)tok > 0x10000 && !IsBadWritePtr( tok, sizeof(*tok) ))
        tok->value = impl->evc_cnt + 1;
    if (handler && impl->evc_cnt < (int)ARRAY_SIZE(impl->evc_handlers))
    {
        WARN( "EVC_ADD %s (%s) h=%p tok=%d\n", debugstr_w(impl->class_name), debugstr_w(impl->name), handler, impl->evc_cnt + 1 );
        impl->evc_handlers[impl->evc_cnt++] = (IInspectable *)handler;
        IInspectable_AddRef( (IInspectable *)handler );
        impl->evc_fired = FALSE;
        uwp_invalidate_window();
    }
    return S_OK;
}
static HRESULT WINAPI uwp_fe7_remove_EffectiveViewportChanged( IInspectable *iface, EventRegistrationToken tok )
{
    struct uwp_element *impl = CONTAINING_RECORD( iface, struct uwp_element, IFrameworkElement7_iface );
    if (wcsstr( impl->class_name, L"ItemsRepeater" ))
        WARN( "EVC_REM %s (%s) tok=%lld\n", debugstr_w(impl->class_name), debugstr_w(impl->name), (long long)tok.value );
    if (tok.value >= 1 && tok.value <= (INT64)impl->evc_cnt)
    {
        int idx = (int)(tok.value - 1);
        if (impl->evc_handlers[idx])
        {
            IInspectable_Release( impl->evc_handlers[idx] );
            impl->evc_handlers[idx] = NULL;
        }
    }
    return S_OK;
}
static HRESULT WINAPI uwp_fe7_InvalidateViewport( IInspectable *iface )
{
    struct uwp_element *impl = CONTAINING_RECORD( iface, struct uwp_element, IFrameworkElement7_iface );
    int i;
    for (i = 0; i < impl->evc_cnt; i++)
        uwp_fire_evc_one( impl, impl->evc_handlers[i] );
    return S_OK;
}
const void *uwp_fe7_vtbl[32] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_fe7_get_IsLoaded,                    /* slot 6: get_IsLoaded */
    [7] = uwp_fe7_add_EffectiveViewportChanged,    /* slot 7: add_EffectiveViewportChanged */
    [8] = uwp_fe7_remove_EffectiveViewportChanged, /* slot 8: remove_EffectiveViewportChanged */
    [9] = uwp_fe7_InvalidateViewport,              /* slot 9: InvalidateViewport */
    [10 ... 31] = uwp_generic_noop,
};
void uwp_fire_loaded_content( IInspectable *content )
{
    struct uwp_element *el;
    if (!content) return;
    el = uwp_elem_from_any( content );
    if (el) uwp_fire_loaded_tree( el );
}

void uwp_realize_deferred_entry( struct uwp_defer_entry *de );
void uwp_vsm_invalidate_eval_chain( struct uwp_element *el );

void uwp_record_xbf_ref( struct uwp_element *root, const WCHAR *full_name,
                                const WCHAR *short_name, BOOL is_resource_path )
{
    int i;
    if (!root || !full_name || !full_name[0] || !short_name || !short_name[0]) return;
    for (i = 0; i < root->xbf_ref_cnt; i++)
    {
        if (!wcscmp( root->xbf_refs[i].full_name, full_name )) return;
    }
    if (!xaml_vec_grow( (void **)&root->xbf_refs, &root->xbf_ref_cap, root->xbf_ref_cnt + 1, sizeof(*root->xbf_refs), 32 ))
        return;
    lstrcpynW( root->xbf_refs[root->xbf_ref_cnt].full_name, full_name,
               ARRAY_SIZE(root->xbf_refs[0].full_name) );
    lstrcpynW( root->xbf_refs[root->xbf_ref_cnt].short_name, short_name,
               ARRAY_SIZE(root->xbf_refs[0].short_name) );
    root->xbf_refs[root->xbf_ref_cnt].is_resource_path = is_resource_path;
    root->xbf_refs[root->xbf_ref_cnt].instantiated = FALSE;
    root->xbf_ref_cnt++;
}

void uwp_mark_xbf_ref_instantiated( struct uwp_element *root, const WCHAR *cls_name )
{
    int i;
    if (!root || !cls_name || !cls_name[0]) return;
    for (i = 0; i < root->xbf_ref_cnt; i++)
    {
        if (!wcscmp( root->xbf_refs[i].full_name, cls_name ))
            root->xbf_refs[i].instantiated = TRUE;
    }
}

static BOOL uwp_tree_has_class( struct uwp_element *el, const WCHAR *full_name )
{
    struct uwp_vec_view *v;
    unsigned int k;
    if (!el || !full_name) return FALSE;
    if (el->class_name[0] && !wcscmp( el->class_name, full_name )) return TRUE;
    if (el->content)
    {
        struct uwp_element *ch = uwp_elem_from_any( el->content );
        if (ch && ch != el && uwp_tree_has_class( ch, full_name )) return TRUE;
    }
    if (el->children_coll)
    {
        v = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        for (k = 0; k < v->count; k++)
        {
            struct uwp_element *ch = uwp_elem_from_any( v->items[k] );
            if (ch && ch != el && uwp_tree_has_class( ch, full_name )) return TRUE;
        }
    }
    return FALSE;
}

static struct uwp_element *uwp_find_empty_holder_border( struct uwp_element *el, const WCHAR *match_prefix, BOOL prefix_only )
{
    struct uwp_vec_view *v;
    unsigned int k;
    if (!el) return NULL;
    if (el->kind == UWP_KIND_BORDER && el->visibility == 0 && !el->children_coll && !el->content)
    {
        if (!prefix_only) return el;
        if (match_prefix && el->name[0])
        {
            WCHAR pfx[8];
            int plen = min( 4, (int)wcslen( match_prefix ) );
            if (plen >= 3)
            {
                lstrcpynW( pfx, match_prefix, plen + 1 );
                if (wcsstr( el->name, pfx )) return el;
            }
        }
    }
    if (el->content)
    {
        struct uwp_element *ch = uwp_elem_from_any( el->content );
        struct uwp_element *r;
        if (ch && ch != el && (r = uwp_find_empty_holder_border( ch, match_prefix, prefix_only ))) return r;
    }
    if (el->children_coll)
    {
        v = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        for (k = 0; k < v->count; k++)
        {
            struct uwp_element *ch = uwp_elem_from_any( v->items[k] );
            struct uwp_element *r;
            if (ch && ch != el && (r = uwp_find_empty_holder_border( ch, match_prefix, prefix_only ))) return r;
        }
    }
    return NULL;
}


static BOOL uwp_instantiate_xbf_control_into( struct uwp_element *holder, const WCHAR *full_name,
                                              const WCHAR *short_name )
{
    void *inst = NULL;
    struct uwp_element *el = NULL;
    struct uwp_element *prev_head;
    int gen;
    HRESULT hr;

    if (!holder || !full_name || !full_name[0]) return FALSE;

    AcquireSRWLockExclusive( &g_all_uwp_elems_lock );
    prev_head = g_all_uwp_elems_head;
    gen = g_all_uwp_elem_cnt;
    ReleaseSRWLockExclusive( &g_all_uwp_elems_lock );

    __TRY
    {
        hr = uwp_create_instance_from_provider( full_name, &inst );
    }
    __EXCEPT_ALL
    {
        hr = E_FAIL;
        TRACE( "uwp_instantiate_xbf_control_into: provider activation(%s) faulted\n", debugstr_w(full_name) );
    }
    __ENDTRY;

    AcquireSRWLockExclusive( &g_all_uwp_elems_lock );
    if (g_all_uwp_elem_cnt > gen)
    {
        struct uwp_element *e;
        for (e = g_all_uwp_elems_head; e && e != prev_head; e = e->next_reg)
        {
            if (e->kind == UWP_KIND_USER_CONTROL || e->kind == UWP_KIND_PAGE)
                el = e;
        }
    }
    ReleaseSRWLockExclusive( &g_all_uwp_elems_lock );
    if (!el && inst && SUCCEEDED(hr))
        el = uwp_elem_from_any( inst );

    if (!el)
    {
        IInspectable *raw = NULL;
        if (SUCCEEDED(uwp_element_create( full_name, NULL, NULL, (void **)&raw )) && raw)
        {
            el = uwp_elem_from_any( raw );
            IInspectable_Release( raw );
        }
    }
    if (!el) return FALSE;

    if (inst) el->provider_inst = (IInspectable *)inst;
    lstrcpynW( el->class_name, full_name, ARRAY_SIZE(el->class_name) );
    if (!el->children_coll && !el->content)
        uwp_populate_element_from_pri_xbf( el, short_name );

    if (!el->children_coll && !el->content)
    {
        TRACE( "uwp_instantiate_xbf_control_into: %s yielded empty tree\n", debugstr_w(full_name) );
        return FALSE;
    }

    uwp_vec_create( holder, &holder->children_coll );
    uwp_vec_Append( holder->children_coll, el->outer ? (void *)el->outer : (void *)&el->IInspectable_iface );
    el->parent = holder;
    uwp_vsm_invalidate_eval_chain( el );
    uwp_realize_empty_panel_defers( el );
    uwp_fire_loaded_tree( el );
    TRACE( "uwp_instantiate_xbf_control_into: hosted %s in %s\n",
           debugstr_w(el->class_name), debugstr_w(holder->name) );
    return TRUE;
}

BOOL uwp_populate_page_from_xbf_refs( struct uwp_element *page_el )
{
    int i;
    struct uwp_element *any_holder;

    if (!page_el || !page_el->xbf_parsed) return FALSE;

    uwp_realize_empty_panel_defers( page_el );

    /* Check if page already has populated content and no empty placeholder holders needing XBF controls */
    any_holder = uwp_find_empty_holder_border( page_el, NULL, FALSE );
    if (!any_holder)
        return page_el->children_coll != NULL || page_el->content != NULL;

    for (i = 0; i < page_el->xbf_ref_cnt; i++)
    {
        struct uwp_xbf_ref *ref = &page_el->xbf_refs[i];
        struct uwp_element *holder;

        if (ref->is_resource_path || ref->instantiated) continue;
        if (uwp_tree_has_class( page_el, ref->full_name ))
        {
            ref->instantiated = TRUE;
            continue;
        }
        holder = uwp_find_empty_holder_border( page_el, ref->short_name, TRUE );
        if (!holder) holder = any_holder;
        if (holder && uwp_instantiate_xbf_control_into( holder, ref->full_name, ref->short_name ))
        {
            ref->instantiated = TRUE;
            return TRUE;
        }
    }

    return page_el->children_coll != NULL || page_el->content != NULL;
}


struct uwp_element *g_focused_textbox = NULL;

static const GUID IID_INavigationEventArgs_UWP =
    { 0xb6aa9834, 0x6691, 0x44d1, { 0xbd, 0xf7, 0x58, 0x82, 0x0c, 0x27, 0xb0, 0xd0 } };
static const GUID IID_INavigationEventArgs2_UWP =
    { 0xdbff71d9, 0x979a, 0x4b2e, { 0xa4, 0x9b, 0x3b, 0xb1, 0x7f, 0xde, 0xf5, 0x74 } };

static inline struct uwp_nav_event_args *impl_from_nav_args( void *iface )
{
    return CONTAINING_RECORD( iface, struct uwp_nav_event_args, INavigationEventArgs_iface );
}
static inline struct uwp_nav_event_args *impl_from_nav_args2( void *iface )
{
    return CONTAINING_RECORD( iface, struct uwp_nav_event_args, INavigationEventArgs2_iface );
}

static HRESULT WINAPI uwp_nea_QI( void *iface, REFIID iid, void **out )
{
    struct uwp_nav_event_args *a = impl_from_nav_args( iface );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_INavigationEventArgs_UWP ))
    {
        InterlockedIncrement( &a->ref );
        *out = &a->INavigationEventArgs_iface;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_INavigationEventArgs2_UWP ))
    {
        InterlockedIncrement( &a->ref );
        *out = &a->INavigationEventArgs2_iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI uwp_nea_AddRef( void *iface ) { return InterlockedIncrement( &impl_from_nav_args( iface )->ref ); }
ULONG WINAPI uwp_nea_Release( void *iface )
{
    struct uwp_nav_event_args *a = impl_from_nav_args( iface );
    ULONG r = InterlockedDecrement( &a->ref );
    if (!r)
    {
        if (a->content) IInspectable_Release( a->content );
        if (a->parameter) IInspectable_Release( a->parameter );
        if (a->transition_info) IInspectable_Release( a->transition_info );
        if (a->uri) IInspectable_Release( a->uri );
        if (a->source_page_type.Name) WindowsDeleteString( a->source_page_type.Name );
        free( a );
    }
    return r;
}
static HRESULT WINAPI uwp_nea_GetIids( void *iface, ULONG *cnt, IID **iids )
{
    IID *out = CoTaskMemAlloc( 2 * sizeof(IID) );
    if (!out) return E_OUTOFMEMORY;
    out[0] = IID_INavigationEventArgs_UWP;
    out[1] = IID_INavigationEventArgs2_UWP;
    *cnt = 2;
    *iids = out;
    return S_OK;
}
static HRESULT WINAPI uwp_nea_GetRuntimeClassName( void *iface, HSTRING *name )
{
    static const WCHAR cls[] = L"Windows.UI.Xaml.Navigation.NavigationEventArgs";
    return WindowsCreateString( cls, ARRAY_SIZE(cls) - 1, name );
}
static HRESULT WINAPI uwp_nea_GetTrustLevel( void *iface, TrustLevel *lvl ) { if (lvl) *lvl = BaseTrust; return S_OK; }
static HRESULT WINAPI uwp_nea_get_Content( void *iface, IInspectable **val )
{
    struct uwp_nav_event_args *a = impl_from_nav_args( iface );
    if (!val) return E_POINTER;
    *val = a->content;
    if (*val) IInspectable_AddRef( *val );
    return S_OK;
}
static HRESULT WINAPI uwp_nea_get_Parameter( void *iface, IInspectable **val )
{
    struct uwp_nav_event_args *a = impl_from_nav_args( iface );
    if (!val) return E_POINTER;
    *val = a->parameter;
    if (*val) IInspectable_AddRef( *val );
    return S_OK;
}
static HRESULT WINAPI uwp_nea_get_SourcePageType( void *iface, TypeName *val )
{
    struct uwp_nav_event_args *a = impl_from_nav_args( iface );
    if (!val) return E_POINTER;
    val->Name = NULL;
    val->Kind = a->source_page_type.Kind;
    if (a->source_page_type.Name)
        WindowsDuplicateString( a->source_page_type.Name, &val->Name );
    return S_OK;
}
static HRESULT WINAPI uwp_nea_get_NavigationMode( void *iface, INT32 *val )
{
    struct uwp_nav_event_args *a = impl_from_nav_args( iface );
    if (val) *val = a->nav_mode;
    return S_OK;
}
static HRESULT WINAPI uwp_nea_get_Uri( void *iface, IInspectable **val )
{
    struct uwp_nav_event_args *a = impl_from_nav_args( iface );
    if (!val) return E_POINTER;
    *val = a->uri;
    if (*val) IInspectable_AddRef( *val );
    return S_OK;
}
static HRESULT WINAPI uwp_nea_put_Uri( void *iface, IInspectable *val )
{
    struct uwp_nav_event_args *a = impl_from_nav_args( iface );
    if (a->uri) IInspectable_Release( a->uri );
    a->uri = val;
    if (val) IInspectable_AddRef( val );
    return S_OK;
}

static HRESULT WINAPI uwp_nea2_QI( void *iface, REFIID iid, void **out ) { return uwp_nea_QI( &impl_from_nav_args2(iface)->INavigationEventArgs_iface, iid, out ); }
static ULONG WINAPI uwp_nea2_AddRef( void *iface ) { return uwp_nea_AddRef( &impl_from_nav_args2(iface)->INavigationEventArgs_iface ); }
ULONG WINAPI uwp_nea2_Release( void *iface ) { return uwp_nea_Release( &impl_from_nav_args2(iface)->INavigationEventArgs_iface ); }
static HRESULT WINAPI uwp_nea2_GetIids( void *iface, ULONG *cnt, IID **iids ) { return uwp_nea_GetIids( &impl_from_nav_args2(iface)->INavigationEventArgs_iface, cnt, iids ); }
static HRESULT WINAPI uwp_nea2_GetRuntimeClassName( void *iface, HSTRING *name ) { return uwp_nea_GetRuntimeClassName( &impl_from_nav_args2(iface)->INavigationEventArgs_iface, name ); }
static HRESULT WINAPI uwp_nea2_GetTrustLevel( void *iface, TrustLevel *lvl ) { return uwp_nea_GetTrustLevel( &impl_from_nav_args2(iface)->INavigationEventArgs_iface, lvl ); }
static HRESULT WINAPI uwp_nea2_get_NavigationTransitionInfo( void *iface, IInspectable **val )
{
    struct uwp_nav_event_args *a = impl_from_nav_args2( iface );
    if (!val) return E_POINTER;
    *val = a->transition_info;
    if (*val) IInspectable_AddRef( *val );
    return S_OK;
}

static const void *uwp_nea_vtbl[] =
{
    uwp_nea_QI, uwp_nea_AddRef, uwp_nea_Release,
    uwp_nea_GetIids, uwp_nea_GetRuntimeClassName, uwp_nea_GetTrustLevel,
    uwp_nea_get_Content,
    uwp_nea_get_Parameter,
    uwp_nea_get_SourcePageType,
    uwp_nea_get_NavigationMode,
    uwp_nea_get_Uri,
    uwp_nea_put_Uri,
};
static const void *uwp_nea2_vtbl[] =
{
    uwp_nea2_QI, uwp_nea2_AddRef, uwp_nea2_Release,
    uwp_nea2_GetIids, uwp_nea2_GetRuntimeClassName, uwp_nea2_GetTrustLevel,
    uwp_nea2_get_NavigationTransitionInfo,
};

struct uwp_nav_event_args *uwp_nav_event_args_create( IInspectable *content, IInspectable *param,
                                                             const TypeName *type, IInspectable *transition_info )
{
    struct uwp_nav_event_args *a = calloc( 1, sizeof(*a) );
    if (!a) return NULL;
    a->INavigationEventArgs_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_nea_vtbl;
    a->INavigationEventArgs2_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_nea2_vtbl;
    a->ref = 1;
    a->content = content;
    if (content) IInspectable_AddRef( content );
    a->parameter = param;
    if (param) IInspectable_AddRef( param );
    a->transition_info = transition_info;
    if (transition_info) IInspectable_AddRef( transition_info );
    if (type)
    {
        a->source_page_type.Kind = type->Kind;
        if (type->Name) WindowsDuplicateString( type->Name, &a->source_page_type.Name );
    }
    a->nav_mode = 0; /* NavigationMode_New */
    return a;
}
const WCHAR *uwp_btype_to_class_name( WORD tid )
{
    WORD idx = tid & 0x7fff;

    if ((tid & 0x8000) && idx < ARRAY_SIZE( xbf_known_types ) && xbf_known_types[idx])
        return xbf_known_types[idx];

    FIXME( "Unknown XBF builtin type %#04x\n", tid );
    return L"Windows.UI.Xaml.Controls.ContentControl";
}

/* Set a custom XAML property (e.g. CalculatorButton.ButtonId = "Zero") via IXamlMetadataProvider */

void uwp_invoke_button( struct uwp_element *btn );

struct uwp_element *uwp_find_button_by_text( struct uwp_element *el, const WCHAR *text )
{
    if (!el || el->visibility != 0 || !el->is_enabled) return NULL;

    if (el->children_coll)
    {
        struct uwp_vec_view *children = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        int k;
        for (k = (int)children->count - 1; k >= 0; k--)
        {
            struct uwp_element *hit = uwp_find_button_by_text( uwp_elem_from_any( children->items[k] ), text );
            if (hit) return hit;
        }
    }
    if (el->content)
    {
        struct uwp_element *hit = uwp_find_button_by_text( uwp_elem_from_any( el->content ), text );
        if (hit) return hit;
    }
    if ((el->kind == UWP_KIND_BUTTON || el->kind == UWP_KIND_TOGGLE_BUTTON) && el->text[0])
    {
        WCHAR tmp[64];
        const WCHAR *disp = uwp_format_display_label( el, tmp, ARRAY_SIZE(tmp) );
        if (!lstrcmpiW( el->text, text ) || (disp && disp[0] && !lstrcmpiW( disp, text )))
            return el;
    }
    return NULL;
}

static BOOL uwp_is_textbox( const struct uwp_element *el )
{
    if (!el || el->visibility != 0 || !el->is_enabled) return FALSE;
    return wcsstr( el->class_name, L"TextBox" ) != NULL ||
           wcsstr( el->class_name, L"AutoSuggestBox" ) != NULL ||
           wcsstr( el->name, L"SearchBox" ) != NULL;
}

struct uwp_element *uwp_hit_test_textbox( struct uwp_element *el, int x, int y )
{
    if (!el || el->visibility != 0) return NULL;

    if (el->has_bounds &&
        x >= el->bounds.left && x < el->bounds.right &&
        y >= el->bounds.top && y < el->bounds.bottom &&
        uwp_is_textbox( el ))
        return el;

    if (el->children_coll)
    {
        struct uwp_vec_view *children = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        int k;
        for (k = (int)children->count - 1; k >= 0; k--)
        {
            struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
            struct uwp_element *hit = uwp_hit_test_textbox( ch, x, y );
            if (hit) return hit;
        }
    }
    if (el->content)
    {
        struct uwp_element *ch = uwp_elem_from_any( el->content );
        struct uwp_element *hit = uwp_hit_test_textbox( ch, x, y );
        if (hit) return hit;
    }
    return NULL;
}

void uwp_fire_asb_recursive( struct uwp_element *el, const WCHAR *text, BOOL is_submit )
{
    if (!el) return;
    if (text && el->text != text && (wcsstr( el->class_name, L"AutoSuggestBox" ) || wcsstr( el->class_name, L"TextBox" )))
        lstrcpynW( el->text, text, ARRAY_SIZE(el->text) );
    if (is_submit)
        uwp_asb_fire_query_submitted( el );
    else
        uwp_asb_fire_text_changed( el );
    if (el->content)
        uwp_fire_asb_recursive( uwp_elem_from_any( el->content ), text, is_submit );
    if (el->children_coll)
    {
        struct uwp_vec_view *children = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        unsigned int k;
        for (k = 0; k < children->count; k++)
            uwp_fire_asb_recursive( uwp_elem_from_any( children->items[k] ), text, is_submit );
    }
}


BOOL uwp_elem_is_descendant( struct uwp_element *ancestor, struct uwp_element *node )
{
    struct uwp_element *cur = node;
    int depth = 0;
    if (!ancestor || !node) return FALSE;
    while (cur && depth < 64)
    {
        if (cur == ancestor || cur->owner_component == ancestor) return TRUE;
        cur = cur->parent;
        depth++;
    }
    return FALSE;
}



BOOL uwp_fire_tapped_bubble( struct uwp_element *target, int x, int y );


struct uwp_element *uwp_hit_test_tabview( struct uwp_element *el, int x, int y )
{
    if (!el || !el->has_bounds || el->visibility != 0) return NULL;
    if (wcsstr( el->class_name, L"TabView" ) && !wcsstr( el->class_name, L"TabViewItem" ))
    {
        if (x >= el->bounds.left && x < el->bounds.right &&
            y >= el->bounds.top && y < el->bounds.bottom)
            return el;
    }
    if (el->content)
    {
        struct uwp_element *ch = uwp_elem_from_any( el->content );
        if (ch && ch != el)
        {
            struct uwp_element *hit = uwp_hit_test_tabview( ch, x, y );
            if (hit) return hit;
        }
    }
    if (el->children_coll)
    {
        struct uwp_vec_view *children = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        unsigned int k;
        for (k = 0; k < children->count; k++)
        {
            struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
            if (ch && ch != el)
            {
                struct uwp_element *hit = uwp_hit_test_tabview( ch, x, y );
                if (hit) return hit;
            }
        }
    }
    return NULL;
}
struct uwp_element *uwp_hit_test_tappable( struct uwp_element *el, int x, int y )
{
    if (!el || !el->has_bounds || el->visibility != 0) return NULL;
    if (x < el->bounds.left || x >= el->bounds.right || y < el->bounds.top || y >= el->bounds.bottom)
        return NULL;
    if (el->children_coll)
    {
        struct uwp_vec_view *children = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        int k;
        for (k = (int)children->count - 1; k >= 0; k--)
        {
            struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
            struct uwp_element *hit = ch && ch != el ? uwp_hit_test_tappable( ch, x, y ) : NULL;
            if (hit) return hit;
        }
    }
    if (el->content)
    {
        struct uwp_element *ch = uwp_elem_from_any( el->content );
        struct uwp_element *hit = ch && ch != el ? uwp_hit_test_tappable( ch, x, y ) : NULL;
        if (hit) return hit;
    }
    if (el->tapped_reg.next_token > 0) return el;
    return NULL;
}


struct uwp_element *g_pressed_btn = NULL;
struct uwp_element *g_pressed_tabview = NULL;
BOOL uwp_fire_element_pointer( struct uwp_element *target, enum xaml_pointer_event_kind kind,
                                      const struct xaml_pointer_info *info, BOOL bubble );

/* ============================================================================
 * 7c. Non-Blocking WinUI MenuFlyout / MenuBarItem Popup Window
 * ============================================================================ */



HRESULT WINAPI dp_args_QI( void *iface, REFIID iid, void **out )
{
    static const GUID IID_IRoutedEventArgs_UWP =
        { 0x443ecc75, 0x7e5d, 0x48a5, { 0x93, 0x1a, 0x30, 0x0e, 0x46, 0xa4, 0x91, 0x6a } };
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IDependencyPropertyChangedEventArgs_UWP ) ||
        IsEqualGUID( iid, &IID_IRoutedEventArgs_UWP ))
    {
        *out = iface;
        InterlockedIncrement( &((struct uwp_dp_changed_args *)iface)->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
ULONG WINAPI dp_args_AddRef( void *iface )
{
    return InterlockedIncrement( &((struct uwp_dp_changed_args *)iface)->ref );
}
ULONG WINAPI dp_args_Release( void *iface )
{
    struct uwp_dp_changed_args *a = iface;
    ULONG r = InterlockedDecrement( &a->ref );
    if (!r)
    {
        if (a->old_val) IInspectable_Release( a->old_val );
        if (a->new_val) IInspectable_Release( a->new_val );
        free( a );
    }
    return r;
}
HRESULT WINAPI dp_args_GetIids( void *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}
HRESULT WINAPI dp_args_GetRuntimeClassName( void *iface, HSTRING *name )
{
    const WCHAR *cls = L"Windows.UI.Xaml.DependencyPropertyChangedEventArgs";
    return WindowsCreateString( cls, wcslen(cls), name );
}
HRESULT WINAPI dp_args_GetTrustLevel( void *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}
HRESULT WINAPI dp_args_get_Property( void *iface, void **out )
{
    struct uwp_dp_changed_args *a = iface;
    if (!out) return E_POINTER;
    *out = a->dp;
    if (*out) IUnknown_AddRef( (IUnknown *)*out );
    return S_OK;
}
HRESULT WINAPI dp_args_get_OldValue( void *iface, IInspectable **out )
{
    struct uwp_dp_changed_args *a = iface;
    if (!out) return E_POINTER;
    *out = a->old_val;
    if (*out) IInspectable_AddRef( *out );
    return S_OK;
}
HRESULT WINAPI dp_args_get_NewValue( void *iface, IInspectable **out )
{
    struct uwp_dp_changed_args *a = iface;
    if (!out) return E_POINTER;
    *out = a->new_val;
    if (*out) IInspectable_AddRef( *out );
    return S_OK;
}
SRWLOCK xaml_meta_cache_lock = SRWLOCK_INIT;
INIT_ONCE xaml_meta_cache_once = INIT_ONCE_STATIC_INIT;
struct xaml_meta_cache_entry *xaml_meta_cache[XAML_META_CACHE_SIZE];
struct uwp_calc_vm g_calc_vm;
BOOL uwp_get_custom_xaml_member( void *app_outer, const WCHAR *owner_type_name,
                                        const WCHAR *prop_name, void *target_obj, IInspectable **out_val )
{
    void *provider = NULL, *xaml_type = NULL, *member = NULL;
    HSTRING h_prop = NULL;
    BOOL ok = FALSE;
    if (!out_val) return FALSE;
    *out_val = NULL;
    if (!app_outer || !owner_type_name || !owner_type_name[0] || !prop_name || !target_obj) return FALSE;
    if (FAILED(IInspectable_QueryInterface( (IInspectable *)app_outer, &IID_IXamlMetadataProvider_UWP_g, &provider )) || !provider)
        return FALSE;
    xaml_type = uwp_provider_get_xaml_type_cached( (struct xaml_metadata_provider *)provider, owner_type_name );
    if (xaml_type)
    {
        struct xaml_type *xt = (struct xaml_type *)xaml_type;
        if (xt->lpVtbl->GetMember && SUCCEEDED(WindowsCreateString( prop_name, wcslen(prop_name), &h_prop )))
        {
            if (SUCCEEDED(xt->lpVtbl->GetMember( xt, h_prop, (struct xaml_member **)&member )) && member)
            {
                struct xaml_member *xm = (struct xaml_member *)member;
                if (xm->lpVtbl->GetValue)
                {
                    HRESULT hr = E_FAIL;
                    __TRY
                    {
                        hr = xm->lpVtbl->GetValue( xm, target_obj, out_val );
                    }
                    __EXCEPT_ALL { hr = E_FAIL; *out_val = NULL; }
                    __ENDTRY;
                    if (SUCCEEDED(hr) && *out_val) ok = TRUE;
                }
                IInspectable_Release( (IInspectable *)member );
            }
            WindowsDeleteString( h_prop );
        }
        IInspectable_Release( (IInspectable *)xaml_type );
    }
    IInspectable_Release( (IInspectable *)provider );
    return ok;
}
