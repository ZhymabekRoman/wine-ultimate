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
#include "xbf_known_types.h"
#include "weakreference.h"
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
                if (g_in_tap_diag) WARN( "TAP uwp_cc_get_Content(%s) -> outer %p\n", debugstr_w(uwp_elem_text( impl )), *out );
                return S_OK;
            }
        }
        *out = impl->content;
        IInspectable_AddRef( (IInspectable *)*out );
    }
    else if (uwp_has_text( impl ))
    {
        *out = box_string_value( uwp_elem_text( impl ) );
        if (g_in_tap_diag) WARN( "TAP uwp_cc_get_Content(%s) -> boxed %p\n", debugstr_w(uwp_elem_text( impl )), *out );
    }
    return S_OK;
}

static HRESULT WINAPI uwp_cc_put_Content( void *iface, void *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    WCHAR str_buf[256] = {0};
    struct uwp_element *child_el;

    if (!impl) return S_OK;
    if (impl->content)
    {
        struct uwp_element *old_ch = uwp_elem_from_any( impl->content );
        if (old_ch && old_ch->parent == impl) old_ch->parent = NULL;
        IInspectable_Release( impl->content );
    }
    impl->content = (IInspectable *)val;
    if (impl->content) IInspectable_AddRef( impl->content );
    impl->probe_flags = 0;
    if (!uwp_is_text_input( impl )) uwp_elem_set_placeholder( impl, NULL );

    if (!val)
    {
        impl->content_kind = UWP_CONTENT_NONE;
    }
    else if (uwp_unbox_string( val, str_buf, ARRAY_SIZE(str_buf) ))
    {
        if (uwp_is_tooltip( impl ))
            return S_OK;
        if (uwp_is_nav_settings_item( impl ))
        {
            uwp_elem_clear_text( impl );
            if (!wcscmp( impl->name, L"SettingsItem" ))
                impl->visibility = 1;
            return S_OK;
        }
        impl->content_kind = UWP_CONTENT_TEXT;
        if (str_buf[0])
        {
            uwp_elem_set_text( impl, str_buf );
            TRACE( "uwp_cc_put_Content string on %s (%s) -> %s\n",
                   debugstr_w(impl->class_name), debugstr_w(impl->name), debugstr_w(str_buf) );
        }
    }
    else if ((child_el = uwp_elem_from_any( val )) != NULL)
    {
        impl->content_kind = UWP_CONTENT_ELEMENT;
        impl->in_visual_tree = TRUE;
        child_el->parent = impl;
        child_el->in_visual_tree = TRUE;
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
        if (impl->has_bounds && impl->bounds.right > impl->bounds.left && impl->bounds.bottom > impl->bounds.top)
            uwp_arrange_element( child_el, impl->bounds );
        {
            BOOL tree_loaded = FALSE;
            struct uwp_element *p;
            for (p = impl; p; p = p->parent)
                if (p->loaded_fired) { tree_loaded = TRUE; break; }
            if (tree_loaded)
                uwp_fire_loaded_tree( child_el );
        }
    }
    else
    {
        static const WCHAR *const cc_props[] =
            { L"DisplayName", L"Label", L"Title", L"Text", L"Content", L"Header", L"Name" };
        if (uwp_vm_query_string_prop_depth( (IInspectable *)val, cc_props, ARRAY_SIZE(cc_props),
                                            str_buf, ARRAY_SIZE(str_buf), 2 ) && str_buf[0])
        {
            impl->content_kind = UWP_CONTENT_TEXT;
            uwp_elem_set_text( impl, str_buf );
            TRACE( "uwp_cc_put_Content vm-string on %s (%s) -> %s\n",
                   debugstr_w(impl->class_name), debugstr_w(impl->name), debugstr_w(str_buf) );
        }
        else if (val)
        {
            HSTRING hcls = NULL;
            IInspectable_GetRuntimeClassName( (IInspectable *)val, &hcls );
            TRACE( "uwp_cc_put_Content unhandled val=%p cls=%s on %s (%s)\n",
                   val, hcls ? debugstr_w(WindowsGetStringRawBuffer( hcls, NULL )) : "(null)",
                   debugstr_w(impl->class_name), debugstr_w(impl->name) );
            if (hcls) WindowsDeleteString( hcls );
        }
    }
    if (impl->template_root)
    {
        if (impl->data_context && !impl->template_root->data_context)
            uwp_propagate_data_context( impl->template_root, impl->data_context );
        uwp_sync_template_presenter( impl, L"Content", (IInspectable *)val );
    }
    uwp_vsm_invalidate_eval_chain( impl );
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
    if (uwp_is_card( impl ))
        WARN( "uwp_btn_put_Command on %s impl=%p val=%p\n", debugstr_w(impl->class_name), impl, val );
    if (impl->command) IInspectable_Release( impl->command );
    impl->command = (IInspectable *)val;
    if (impl->command) IInspectable_AddRef( impl->command );
    uwp_sync_command_can_execute( impl );
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
    if (uwp_is_card( impl ))
        WARN( "uwp_btn_put_CommandParameter on %s impl=%p val=%p\n", debugstr_w(impl->class_name), impl, val );
    if (impl->command_param) IInspectable_Release( impl->command_param );
    impl->command_param = (IInspectable *)val;
    if (impl->command_param) IInspectable_AddRef( impl->command_param );
    uwp_sync_command_can_execute( impl );
    return S_OK;
}
HRESULT WINAPI uwp_btn_add_Click( void *iface, void *handler, EventRegistrationToken *tok )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    struct uwp_event_bag *ev;
    if (tok) tok->value = 0;
    if (!impl || !handler) return S_OK;
    ev = uwp_elem_ensure_events( impl );
    if (!ev) return E_OUTOFMEMORY;
    if (uwp_is_card( impl ))
        WARN( "uwp_btn_add_Click on %s impl=%p handler=%p\n", debugstr_w(impl->class_name), impl, handler );
    return xaml_event_registry_add( &ev->click_reg, handler, tok );
}
HRESULT WINAPI uwp_btn_remove_Click( void *iface, EventRegistrationToken tok )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl || !impl->events) return S_OK;
    return xaml_event_registry_remove( &impl->events->click_reg, tok );
}

void uwp_toggle_button_set_checked( struct uwp_element *btn, int checked, BOOL fire_events )
{
    int prev, i;
    IInspectable *boxed = NULL;

    if (!btn) return;
    prev = btn->is_checked;
    btn->is_checked = checked ? 1 : 0;
    btn->is_selected = (BYTE)btn->is_checked;

    if (fire_events)
    {
        boxed = box_bool_value( btn->is_checked == 1 ? TRUE : FALSE );
        /* 1. Update any TwoWay binding on IsChecked / IsSelected */
        for (i = 0; i < btn->dc_bound_dp_cnt; i++)
        {
            const WCHAR *dpn = xaml_dp_get_name( btn->dc_bound_dps[i].dp );
            struct uwp_element *bel = btn->dc_bound_dps[i].binding;
            if (!wcscmp( dpn, L"IsChecked" ) || !wcscmp( dpn, L"IsSelected" ))
            {
                IInspectable *src = (bel && bel->data_context) ? bel->data_context : btn->data_context;
                HSTRING h_path = NULL;
                const WCHAR *path_str = NULL;
                if (!src)
                {
                    struct uwp_element *anc;
                    for (anc = btn->parent; anc; anc = anc->parent)
                        if (anc->data_context) { src = anc->data_context; break; }
                }
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
                if (src && path_str && path_str[0] && boxed)
                {
                    IInspectable *conv_back = (bel && bel->style_anim && bel->style_anim->converter)
                        ? uwp_invoke_value_converter( bel, boxed, dpn, TRUE ) : NULL;
                    uwp_update_binding_source( src, path_str, conv_back ? conv_back : boxed );
                    if (conv_back) IInspectable_Release( conv_back );
                }
                if (h_path) WindowsDeleteString( h_path );
            }
        }
        /* 2. Fire any DependencyPropertyChangedCallbacks registered for IsChecked / IsSelected */
        {
            struct xaml_event_handler *cbs[16];
            void *cb_dps[16];
            int cb_cnt = 0;
            AcquireSRWLockShared( &btn->dp_lock );
            for (i = 0; i < btn->dp_cb_cnt && cb_cnt < (int)ARRAY_SIZE(cbs); i++)
            {
                const WCHAR *dpn = xaml_dp_get_name( btn->dp_cbs[i].dp );
                if (btn->dp_cbs[i].callback &&
                    (!wcscmp( dpn, L"IsChecked" ) || !wcscmp( dpn, L"IsSelected" )))
                {
                    cbs[cb_cnt] = (struct xaml_event_handler *)btn->dp_cbs[i].callback;
                    cb_dps[cb_cnt] = btn->dp_cbs[i].dp;
                    IUnknown_AddRef( (IUnknown *)cbs[cb_cnt] );
                    cb_cnt++;
                }
            }
            ReleaseSRWLockShared( &btn->dp_lock );
            for (i = 0; i < cb_cnt; i++)
            {
                void *sender = btn->outer ? (void *)btn->outer : (void *)&btn->IDependencyObject_iface;
                __TRY { cbs[i]->lpVtbl->Invoke( cbs[i], sender, cb_dps[i] ); }
                __EXCEPT_ALL { WARN( "%s: exception 0x%08lx\n", __func__, (unsigned long)GetExceptionCode() ); }
                __ENDTRY;
                IUnknown_Release( (IUnknown *)cbs[i] );
            }
        }
        /* 3. Fire Checked / Unchecked routed events if state changed */
        if (prev != btn->is_checked && btn->events)
        {
            struct xaml_event_registry *reg = (btn->is_checked == 1) ? &btn->events->tbtn_checked_reg : &btn->events->tbtn_unchecked_reg;
            void **handlers = NULL;
            unsigned int count = xaml_event_registry_snapshot_alloc( reg, &handlers ), k;
            IInspectable *sender = btn->outer ? btn->outer : &btn->IInspectable_iface;
            for (k = 0; k < count; k++)
            {
                struct xaml_event_handler *h = (struct xaml_event_handler *)handlers[k];
                IInspectable *args = (IInspectable *)g_routed_args_for( btn );
                __TRY { h->lpVtbl->Invoke( h, sender, args ); }
                __EXCEPT_ALL { WARN( "%s: exception 0x%08lx\n", __func__, (unsigned long)GetExceptionCode() ); }
                __ENDTRY;
                if (args) IInspectable_Release( args );
                IUnknown_Release( (IUnknown *)handlers[k] );
            }
            free( handlers );
        }
        if (boxed) IInspectable_Release( boxed );
    }
    g_layout_dirty = TRUE;
    uwp_invalidate_window();
}

static HRESULT WINAPI uwp_tbtn_get_IsChecked( void *iface, void **out )
{
    static const GUID IID_IReference_Boolean =
        { 0x3c00fd60, 0x2950, 0x5939, { 0xa2, 0x1a, 0x2d, 0x12, 0xc5, 0xa0, 0x1b, 0x8a } };
    struct uwp_element *impl = uwp_elem_from_any( iface );
    IInspectable *boxed;
    if (!out) return E_POINTER;
    *out = NULL;
    boxed = box_bool_value( (impl && impl->is_checked == 1) ? TRUE : FALSE );
    if (boxed)
    {
        if (FAILED(IInspectable_QueryInterface( boxed, &IID_IReference_Boolean, out )) || !*out)
        {
            *out = boxed;
            return S_OK;
        }
        IInspectable_Release( boxed );
    }
    return S_OK;
}
static HRESULT WINAPI uwp_tbtn_put_IsChecked( void *iface, void *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl)
    {
        boolean bv = FALSE;
        int new_chk = 0;
        if (!val) new_chk = 0;
        else if (uwp_unbox_bool( (IInspectable *)val, &bv )) new_chk = bv ? 1 : 0;
        else new_chk = 1;
        uwp_toggle_button_set_checked( impl, new_chk, FALSE );
    }
    return S_OK;
}
static HRESULT WINAPI uwp_tbtn_add_Checked( void *iface, void *handler, EventRegistrationToken *tok )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    struct uwp_event_bag *ev;
    if (!impl) return E_NOINTERFACE;
    ev = uwp_elem_ensure_events( impl );
    if (!ev) return E_OUTOFMEMORY;
    return xaml_event_registry_add( &ev->tbtn_checked_reg, handler, tok );
}
static HRESULT WINAPI uwp_tbtn_remove_Checked( void *iface, EventRegistrationToken tok )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return E_NOINTERFACE;
    if (!impl->events) return S_OK;
    return xaml_event_registry_remove( &impl->events->tbtn_checked_reg, tok );
}
static HRESULT WINAPI uwp_tbtn_add_Unchecked( void *iface, void *handler, EventRegistrationToken *tok )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    struct uwp_event_bag *ev;
    if (!impl) return E_NOINTERFACE;
    ev = uwp_elem_ensure_events( impl );
    if (!ev) return E_OUTOFMEMORY;
    return xaml_event_registry_add( &ev->tbtn_unchecked_reg, handler, tok );
}
static HRESULT WINAPI uwp_tbtn_remove_Unchecked( void *iface, EventRegistrationToken tok )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return E_NOINTERFACE;
    if (!impl->events) return S_OK;
    return xaml_event_registry_remove( &impl->events->tbtn_unchecked_reg, tok );
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
            v->runtime_class = uwp_is_rich_text_block( impl )
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
    const WCHAR *s = uwp_elem_text( impl );
    return WindowsCreateString( s, wcslen(s), out );
}
HRESULT WINAPI uwp_tb_put_Text( void *iface, HSTRING val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    const WCHAR *s = WindowsGetStringRawBuffer( val, NULL );
    if (impl)
    {
        if (s && wcsstr( s, L"Ln -" ))
            uwp_elem_set_text( impl, L"  Ln 1, Col 1" );
        else
            uwp_elem_set_text( impl, s );
        TRACE( "ITextBlock::put_Text on %s (%s) -> %s\n",
               debugstr_w(impl->class_name), debugstr_w(impl->name), debugstr_w(uwp_elem_text( impl )) );
        {
            struct uwp_element *p;
            int hop;
            for (p = impl, hop = 0; p && hop < 4; p = p->parent, hop++)
            {
                p->last_sc_w = 0.0f;
                p->last_sc_h = 0.0f;
            }
        }
        g_layout_dirty = TRUE;
        uwp_invalidate_window();
    }
    return S_OK;
}

/* --- IPanel / IGrid / IRowDefinition / IColumnDefinition --- */

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

static HRESULT WINAPI uwp_bg_get_Background( void *iface, IInspectable **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    *out = impl ? impl->bg_brush : NULL;
    if (*out) IInspectable_AddRef( *out );
    return S_OK;
}
static HRESULT WINAPI uwp_bg_put_Background( void *iface, IInspectable *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return S_OK;
    if (impl->bg_brush != val)
    {
        if (impl->bg_brush) IInspectable_Release( impl->bg_brush );
        impl->bg_brush = val;
        if (val) IInspectable_AddRef( val );
    }
    if (val)
    {
        struct uwp_element *b_el = uwp_elem_from_any( val );
        if (b_el)
        {
            const WCHAR *b_src = uwp_elem_image_src( b_el );
            if (uwp_elem_has_brush_argb( b_el ))
                uwp_el_set_bg_argb( impl, apply_opacity_to_argb( uwp_elem_brush_argb( b_el ), uwp_elem_brush_opacity( b_el ) ) );
            if (b_src[0])
            {
                struct uwp_media_ext *m = uwp_elem_ensure_media( impl );
                if (m)
                {
                    lstrcpynW( m->image_src, b_src, ARRAY_SIZE(m->image_src) );
                    m->img_loaded = FALSE;
                    InterlockedExchange( &m->img_loading, 0 );
                }
            }
        }
    }
    else
    {
        impl->has_bg = FALSE;
    }
    uwp_invalidate_window();
    return S_OK;
}
static HRESULT WINAPI uwp_border_put_BorderBrush( void *iface, IInspectable *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return S_OK;
    if (val)
    {
        struct uwp_element *b_el = uwp_elem_from_any( val );
        if (b_el && uwp_elem_has_brush_argb( b_el ))
            uwp_el_set_border_argb( impl, apply_opacity_to_argb( uwp_elem_brush_argb( b_el ), uwp_elem_brush_opacity( b_el ) ) );
    }
    else
    {
        impl->has_border = FALSE;
    }
    uwp_invalidate_window();
    return S_OK;
}
static HRESULT WINAPI uwp_border_get_BorderThickness( void *iface, struct uwp_thickness *t )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (t && impl)
    {
        t->Left = impl->border_thick_l; t->Top = impl->border_thick_t;
        t->Right = impl->border_thick_r; t->Bottom = impl->border_thick_b;
    }
    return S_OK;
}
static HRESULT WINAPI uwp_border_put_BorderThickness( void *iface, struct uwp_thickness *t )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl && t)
    {
        impl->border_thick_l = t->Left; impl->border_thick_t = t->Top;
        impl->border_thick_r = t->Right; impl->border_thick_b = t->Bottom;
        uwp_invalidate_window();
    }
    return S_OK;
}

const void *uwp_border_vtbl[32] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_generic_noop,
    [7] = uwp_border_put_BorderBrush,
    [8] = uwp_border_get_BorderThickness,
    [9] = uwp_border_put_BorderThickness,
    [10] = uwp_bg_get_Background,
    [11] = uwp_bg_put_Background,
    [12 ... 15] = uwp_generic_noop,
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
    [15] = uwp_btn_remove_Click,
    [16 ... 31] = uwp_generic_noop,
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
    [10] = uwp_tbtn_add_Checked,
    [11] = uwp_tbtn_remove_Checked,
    [12] = uwp_tbtn_add_Unchecked,
    [13] = uwp_tbtn_remove_Unchecked,
    [14] = uwp_ui_event_add,
    [15 ... 31] = uwp_generic_noop,
};

static HRESULT WINAPI uwp_ts_get_IsOn( void *iface, boolean *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (val) *val = (impl && impl->is_checked == 1) ? TRUE : FALSE;
    return S_OK;
}

static HRESULT WINAPI uwp_ts_put_IsOn( void *iface, boolean val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    int nv = val ? 1 : 0;
    if (!impl) return S_OK;
    if (impl->is_checked != nv)
    {
        impl->is_checked = nv;
        if (UWP_EV_CNT( impl, click_reg ) > 0)
            uwp_fire_event_registry( &impl->events->click_reg,
                                     impl->outer ? impl->outer : &impl->IInspectable_iface,
                                     NULL, "ToggleSwitch::Toggled" );
        uwp_invalidate_window();
    }
    return S_OK;
}

const void *uwp_toggle_switch_vtbl[24] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_ts_get_IsOn,
    [7] = uwp_ts_put_IsOn,
    [8] = uwp_btn_add_Click,
    [9] = uwp_btn_remove_Click,
    [10 ... 23] = uwp_generic_noop,
};

static SRWLOCK g_open_popups_lock = SRWLOCK_INIT;
static struct uwp_element **g_open_popups = NULL;
static int g_open_popup_cap = 0;
static int g_open_popup_cnt = 0;

static void uwp_open_popups_track( struct uwp_element *impl, BOOL open )
{
    int i;
    AcquireSRWLockExclusive( &g_open_popups_lock );
    if (open)
    {
        for (i = 0; i < g_open_popup_cnt; i++)
            if (g_open_popups[i] == impl) { ReleaseSRWLockExclusive( &g_open_popups_lock ); return; }
        if (xaml_vec_grow( (void **)&g_open_popups, &g_open_popup_cap, g_open_popup_cnt + 1, sizeof(*g_open_popups), 16 ))
        {
            g_open_popups[g_open_popup_cnt] = impl;
            __atomic_store_n( &g_open_popup_cnt, g_open_popup_cnt + 1, __ATOMIC_RELEASE );
        }
    }
    else
    {
        for (i = 0; i < g_open_popup_cnt; i++)
        {
            if (g_open_popups[i] == impl)
            {
                if (i + 1 < g_open_popup_cnt)
                    memmove( &g_open_popups[i], &g_open_popups[i + 1],
                             (size_t)(g_open_popup_cnt - i - 1) * sizeof(g_open_popups[0]) );
                g_open_popups[g_open_popup_cnt - 1] = NULL;
                __atomic_store_n( &g_open_popup_cnt, g_open_popup_cnt - 1, __ATOMIC_RELEASE );
                break;
            }
        }
    }
    ReleaseSRWLockExclusive( &g_open_popups_lock );
}

void uwp_open_popups_remove( struct uwp_element *impl )
{
    if (impl) uwp_open_popups_track( impl, FALSE );
}

void uwp_popup_set_open( struct uwp_element *impl, BOOL open )
{
    BOOL prev;
    if (!impl) return;
    prev = impl->is_dropdown_open;
    impl->is_dropdown_open = (open != 0);
    if (impl->kind == UWP_KIND_POPUP)
        uwp_open_popups_track( impl, impl->is_dropdown_open );
    if (impl->is_dropdown_open && impl->content)
    {
        struct uwp_element *ch = uwp_elem_from_any( impl->content );
        if (ch)
        {
            ch->parent = impl;
            if (!ch->data_context)
            {
                struct uwp_element *anc;
                for (anc = impl; anc; anc = anc->parent)
                    if (anc->data_context) { uwp_propagate_data_context( ch, anc->data_context ); break; }
            }
            uwp_fire_loaded_tree( ch );
        }
    }
    if (prev != impl->is_dropdown_open && impl->events)
    {
        struct xaml_event_registry *reg = impl->is_dropdown_open ? &impl->events->popup_opened_reg : &impl->events->popup_closed_reg;
        void **handlers = NULL;
        unsigned int count = xaml_event_registry_snapshot_alloc( reg, &handlers ), i;
        IInspectable *sender = impl->outer ? impl->outer : &impl->IInspectable_iface;
        for (i = 0; i < count; i++)
        {
            struct xaml_event_handler *h = (struct xaml_event_handler *)handlers[i];
            IInspectable *args = (IInspectable *)g_routed_args();
            __TRY { h->lpVtbl->Invoke( h, sender, args ); }
            __EXCEPT_ALL { WARN( "%s: exception 0x%08lx\n", __func__, (unsigned long)GetExceptionCode() ); }
            __ENDTRY;
            if (args) IInspectable_Release( args );
            IUnknown_Release( (IUnknown *)handlers[i] );
        }
        free( handlers );
    }
    g_layout_dirty = TRUE;
    uwp_invalidate_window();
}

int uwp_get_open_popups( struct uwp_element **out_list, int max_cnt )
{
    int i, cnt = 0;
    if (__atomic_load_n( &g_open_popup_cnt, __ATOMIC_ACQUIRE ) == 0)
        return 0;
    AcquireSRWLockShared( &g_open_popups_lock );
    for (i = 0; i < g_open_popup_cnt; i++)
    {
        struct uwp_element *el = g_open_popups[i];
        if (el && el->kind == UWP_KIND_POPUP && el->is_dropdown_open && el->content && el->visibility == 0)
        {
            if (out_list && cnt < max_cnt)
                out_list[cnt] = el;
            cnt++;
        }
    }
    ReleaseSRWLockShared( &g_open_popups_lock );
    return out_list ? min( cnt, max_cnt ) : cnt;
}

static HRESULT WINAPI uwp_popup_get_Child( void *iface, void **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    *out = NULL;
    if (impl && impl->content)
    {
        struct uwp_element *ch = uwp_elem_from_any( impl->content );
        *out = (ch && ch->outer) ? (void *)ch->outer : (void *)impl->content;
        IInspectable_AddRef( (IInspectable *)*out );
    }
    return S_OK;
}
static HRESULT WINAPI uwp_popup_put_Child( void *iface, void *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    struct uwp_element *ch;
    if (!impl) return S_OK;
    if (impl->content)
    {
        struct uwp_element *old_ch = uwp_elem_from_any( impl->content );
        if (old_ch && old_ch->parent == impl) old_ch->parent = NULL;
        IInspectable_Release( impl->content );
    }
    impl->content = (IInspectable *)val;
    if (impl->content) IInspectable_AddRef( impl->content );
    impl->probe_flags = 0;
    if (!uwp_is_text_input( impl )) uwp_elem_set_placeholder( impl, NULL );
    ch = uwp_elem_from_any( val );
    if (ch)
    {
        ch->parent = impl;
        impl->content_kind = UWP_CONTENT_ELEMENT;
        if (impl->is_dropdown_open)
            uwp_fire_loaded_tree( ch );
    }
    g_layout_dirty = TRUE;
    uwp_invalidate_window();
    return S_OK;
}
static HRESULT WINAPI uwp_popup_get_IsOpen( void *iface, boolean *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    *out = (impl && impl->is_dropdown_open) ? TRUE : FALSE;
    return S_OK;
}
static HRESULT WINAPI uwp_popup_put_IsOpen( void *iface, boolean val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl) uwp_popup_set_open( impl, val != 0 );
    return S_OK;
}
static HRESULT WINAPI uwp_popup_get_HorizontalOffset( void *iface, double *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (out) *out = impl ? impl->popup_h_offset : 0.0;
    return S_OK;
}
static HRESULT WINAPI uwp_popup_put_HorizontalOffset( void *iface, double val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl) { impl->popup_h_offset = val; g_layout_dirty = TRUE; uwp_invalidate_window(); }
    return S_OK;
}
static HRESULT WINAPI uwp_popup_get_VerticalOffset( void *iface, double *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (out) *out = impl ? impl->popup_v_offset : 0.0;
    return S_OK;
}
static HRESULT WINAPI uwp_popup_put_VerticalOffset( void *iface, double val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl) { impl->popup_v_offset = val; g_layout_dirty = TRUE; uwp_invalidate_window(); }
    return S_OK;
}
static HRESULT WINAPI uwp_popup_get_IsLightDismissEnabled( void *iface, boolean *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (out) *out = (impl && impl->popup_light_dismiss) ? TRUE : FALSE;
    return S_OK;
}
static HRESULT WINAPI uwp_popup_put_IsLightDismissEnabled( void *iface, boolean val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl) impl->popup_light_dismiss = (val != 0);
    return S_OK;
}
static HRESULT WINAPI uwp_popup_add_Opened( void *iface, void *handler, EventRegistrationToken *tok )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    struct uwp_event_bag *ev;
    if (!impl) return E_NOINTERFACE;
    ev = uwp_elem_ensure_events( impl );
    if (!ev) return E_OUTOFMEMORY;
    return xaml_event_registry_add( &ev->popup_opened_reg, handler, tok );
}
static HRESULT WINAPI uwp_popup_remove_Opened( void *iface, EventRegistrationToken tok )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return E_NOINTERFACE;
    if (!impl->events) return S_OK;
    return xaml_event_registry_remove( &impl->events->popup_opened_reg, tok );
}
static HRESULT WINAPI uwp_popup_add_Closed( void *iface, void *handler, EventRegistrationToken *tok )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    struct uwp_event_bag *ev;
    if (!impl) return E_NOINTERFACE;
    ev = uwp_elem_ensure_events( impl );
    if (!ev) return E_OUTOFMEMORY;
    return xaml_event_registry_add( &ev->popup_closed_reg, handler, tok );
}
static HRESULT WINAPI uwp_popup_remove_Closed( void *iface, EventRegistrationToken tok )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return E_NOINTERFACE;
    if (!impl->events) return S_OK;
    return xaml_event_registry_remove( &impl->events->popup_closed_reg, tok );
}

const void *uwp_popup_vtbl[32] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_popup_get_Child,
    [7] = uwp_popup_put_Child,
    [8] = uwp_popup_get_IsOpen,
    [9] = uwp_popup_put_IsOpen,
    [10] = uwp_popup_get_HorizontalOffset,
    [11] = uwp_popup_put_HorizontalOffset,
    [12] = uwp_popup_get_VerticalOffset,
    [13] = uwp_popup_put_VerticalOffset,
    [14 ... 15] = uwp_generic_noop,
    [16] = uwp_popup_get_IsLightDismissEnabled,
    [17] = uwp_popup_put_IsLightDismissEnabled,
    [18] = uwp_popup_add_Opened,
    [19] = uwp_popup_remove_Opened,
    [20] = uwp_popup_add_Closed,
    [21] = uwp_popup_remove_Closed,
    [22 ... 31] = uwp_generic_noop,
};

const void *uwp_popup2_vtbl[16] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6 ... 15] = uwp_generic_noop,
};

const void *uwp_popup3_vtbl[16] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6 ... 15] = uwp_generic_noop,
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
HRESULT WINAPI uwp_ui7_add_PreviewKeyDown( void *iface, void *handler, EventRegistrationToken *tok )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    struct uwp_event_bag *ev;
    if (!impl) { if (tok) tok->value = 0; return E_INVALIDARG; }
    ev = uwp_elem_ensure_events( impl );
    if (!ev) return E_OUTOFMEMORY;
    return xaml_event_registry_add( &ev->preview_keydown_reg, handler, tok );
}
HRESULT WINAPI uwp_ui7_remove_PreviewKeyDown( void *iface, EventRegistrationToken tok )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return E_INVALIDARG;
    if (!impl->events) return S_OK;
    return xaml_event_registry_remove( &impl->events->preview_keydown_reg, tok );
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
    const WCHAR *txt = (impl && impl->dialog) ? impl->dialog->dialog_close_text : L"";
    if (!out) return E_POINTER;
    return WindowsCreateString( txt, wcslen( txt ), out );
}
HRESULT WINAPI uwp_cd2_put_CloseButtonText( void *iface, HSTRING val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    const WCHAR *raw = val ? WindowsGetStringRawBuffer( val, NULL ) : L"";
    struct uwp_dialog_ext *d = uwp_elem_ensure_dialog( impl );
    if (d) lstrcpynW( d->dialog_close_text, raw ? raw : L"", ARRAY_SIZE(d->dialog_close_text) );
    return S_OK;
}
HRESULT WINAPI uwp_cd2_get_DefaultButton( void *iface, int *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (out) *out = (impl && impl->dialog) ? impl->dialog->dialog_default_btn : 0;
    return S_OK;
}
HRESULT WINAPI uwp_cd2_put_DefaultButton( void *iface, int val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    struct uwp_dialog_ext *d = uwp_elem_ensure_dialog( impl );
    if (d) d->dialog_default_btn = val;
    return S_OK;
}
static HRESULT WINAPI uwp_frame2_get_BackStack( void *iface, IInspectable **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    struct uwp_nav_ext *nav;
    if (!out) return E_POINTER;
    *out = NULL;
    if (!impl) return E_NOINTERFACE;
    nav = uwp_elem_ensure_nav( impl );
    if (!nav) return E_OUTOFMEMORY;
    if (!nav->back_stack)
    {
        uwp_vec_create( NULL, &nav->back_stack );
        if (nav->back_stack)
            CONTAINING_RECORD( nav->back_stack, struct uwp_vec_view, IInspectable_iface )->runtime_class =
                L"Windows.Foundation.Collections.IVector`1<Windows.UI.Xaml.Navigation.PageStackEntry>";
    }
    if (nav->back_stack)
    {
        *out = nav->back_stack;
        IInspectable_AddRef( *out );
    }
    return S_OK;
}
static HRESULT WINAPI uwp_frame2_get_ForwardStack( void *iface, IInspectable **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    struct uwp_nav_ext *nav;
    if (!out) return E_POINTER;
    *out = NULL;
    if (!impl) return E_NOINTERFACE;
    nav = uwp_elem_ensure_nav( impl );
    if (!nav) return E_OUTOFMEMORY;
    if (!nav->forward_stack)
    {
        uwp_vec_create( NULL, &nav->forward_stack );
        if (nav->forward_stack)
            CONTAINING_RECORD( nav->forward_stack, struct uwp_vec_view, IInspectable_iface )->runtime_class =
                L"Windows.Foundation.Collections.IVector`1<Windows.UI.Xaml.Navigation.PageStackEntry>";
    }
    if (nav->forward_stack)
    {
        *out = nav->forward_stack;
        IInspectable_AddRef( *out );
    }
    return S_OK;
}

HRESULT WINAPI uwp_frame_GoBack( void *iface );
static HRESULT WINAPI uwp_frame3_GoBack( void *iface, void *transitionInfo )
{
    (void)transitionInfo;
    return uwp_frame_GoBack( iface );
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
    if (value) *value = (impl && impl->nav) ? impl->nav->nav_stack_enabled : TRUE;
    return S_OK;
}
static HRESULT WINAPI uwp_frame5_put_IsNavigationStackEnabled( void *iface, boolean value )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    struct uwp_nav_ext *nav = uwp_elem_ensure_nav( impl );
    if (nav) nav->nav_stack_enabled = value;
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

static unsigned int uwp_sv_descendant_repeater_items_count( struct uwp_element *el, int depth )
{
    unsigned int best = 0, cnt;
    int i;
    if (!el || depth > 14) return 0;
    if (uwp_is_items_repeater( el ) &&
        !wcsstr( el->name, L"MenuItems" ) && !wcsstr( el->name, L"FooterMenuItems" ))
    {
        for (i = 0; i < el->dp_cnt; i++)
        {
            if (el->dp_store[i].value)
            {
                cnt = uwp_winrt_vec_count( el->dp_store[i].value );
                if (cnt > 0)
                {
                    if (!el->children_coll ||
                        CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface )->count == 0)
                    {
                        el->last_measured_w = 0.0f;
                        el->last_measured_h = 0.0f;
                        g_layout_dirty = TRUE;
                        uwp_invalidate_window();
                    }
                    if (cnt > best) best = cnt;
                }
            }
        }
    }
    if (el->content)
    {
        struct uwp_element *ch = uwp_elem_from_any( el->content );
        if (ch && ch != el)
        {
            cnt = uwp_sv_descendant_repeater_items_count( ch, depth + 1 );
            if (cnt > best) best = cnt;
        }
    }
    if (el->children_coll)
    {
        struct uwp_vec_view *v = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        unsigned int k;
        for (k = 0; v && k < v->count; k++)
        {
            struct uwp_element *ch = uwp_elem_from_any( v->items[k] );
            if (ch && ch != el)
            {
                cnt = uwp_sv_descendant_repeater_items_count( ch, depth + 1 );
                if (cnt > best) best = cnt;
            }
        }
    }
    return best;
}

static HRESULT WINAPI uwp_sv_get_zero_double( void *iface, double *out )
{
    if (out) *out = 0.0;
    return S_OK;
}
static HRESULT WINAPI uwp_sv_get_viewport_w( void *iface, double *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (out) *out = (impl && impl->actual_w > 0.0) ? impl->actual_w : 0.0;
    return S_OK;
}
static HRESULT WINAPI uwp_sv_get_viewport_h( void *iface, double *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (out) *out = (impl && impl->actual_h > 0.0) ? impl->actual_h : 0.0;
    return S_OK;
}
static HRESULT WINAPI uwp_sv_get_scrollable_h( void *iface, double *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    unsigned int rep_cnt = impl ? uwp_sv_descendant_repeater_items_count( impl, 0 ) : 0;
    if (out) *out = (rep_cnt >= 12) ? 500.0 : 0.0;
    return S_OK;
}
static HRESULT WINAPI uwp_sv_get_extent_h( void *iface, double *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    double vh = (impl && impl->actual_h > 0.0) ? impl->actual_h : 0.0;
    unsigned int rep_cnt = impl ? uwp_sv_descendant_repeater_items_count( impl, 0 ) : 0;
    if (out) *out = (rep_cnt >= 12) ? (vh + 500.0) : vh;
    return S_OK;
}
static HRESULT WINAPI uwp_sv_get_zoom( void *iface, float *out )
{
    if (out) *out = 1.0f;
    return S_OK;
}
static HRESULT WINAPI uwp_sv_get_collapsed( void *iface, int *out )
{
    if (out) *out = 1; /* Collapsed / Auto / Enabled */
    return S_OK;
}

const void *uwp_sv_vtbl[72] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_sv_get_collapsed,     /* get_HorizontalScrollMode (1 = Enabled) */
    [7] = uwp_generic_noop,         /* put_HorizontalScrollMode */
    [8] = uwp_sv_get_collapsed,     /* get_VerticalScrollMode (1 = Enabled) */
    [9 ... 23] = uwp_generic_noop,
    [24] = uwp_sv_get_collapsed,    /* get_HorizontalScrollBarVisibility (1 = Auto) */
    [25] = uwp_generic_noop,
    [26] = uwp_sv_get_collapsed,    /* get_VerticalScrollBarVisibility (1 = Auto) */
    [27 ... 39] = uwp_generic_noop,
    [40] = uwp_sv_get_zero_double,  /* get_HorizontalOffset */
    [41] = uwp_sv_get_viewport_w,   /* get_ViewportWidth */
    [42] = uwp_sv_get_zero_double,  /* get_ScrollableWidth */
    [43] = uwp_sv_get_collapsed,    /* get_ComputedHorizontalScrollBarVisibility */
    [44] = uwp_sv_get_viewport_w,   /* get_ExtentWidth */
    [45] = uwp_sv_get_zero_double,  /* get_VerticalOffset */
    [46] = uwp_sv_get_viewport_h,   /* get_ViewportHeight */
    [47] = uwp_sv_get_scrollable_h, /* get_ScrollableHeight */
    [48] = uwp_sv_get_collapsed,    /* get_ComputedVerticalScrollBarVisibility */
    [49] = uwp_sv_get_extent_h,     /* get_ExtentHeight */
    [50] = uwp_sv_get_zoom,         /* get_MinZoomFactor */
    [51] = uwp_generic_noop,        /* put_MinZoomFactor */
    [52] = uwp_sv_get_zoom,         /* get_MaxZoomFactor */
    [53] = uwp_generic_noop,        /* put_MaxZoomFactor */
    [54] = uwp_sv_get_zoom,         /* get_ZoomFactor */
    [55] = uwp_generic_noop,        /* get_ZoomSnapPoints */
    [56] = uwp_ui_event_add,        /* add_ViewChanged */
    [57] = uwp_generic_noop,        /* remove_ViewChanged */
    [58 ... 71] = uwp_generic_noop,
};

enum uwp_elem_kind classify_uwp_class( const WCHAR *cls )
{
    if (!cls) return UWP_KIND_GENERIC;
    if (wcsstr( cls, L"Frame" ) && !wcsstr( cls, L"Framework" )) return UWP_KIND_FRAME;
    if (wcsstr( cls, L"Page" )) return UWP_KIND_PAGE;
    if (wcsstr( cls, L"RowDefinition" )) return UWP_KIND_ROW_DEF;
    if (wcsstr( cls, L"ColumnDefinition" )) return UWP_KIND_COL_DEF;
    if (wcsstr( cls, L"StackPanel" )) return UWP_KIND_STACK_PANEL;
    if (wcsstr( cls, L"WrapPanel" )) return UWP_KIND_WRAP_PANEL;
    if (!wcscmp( cls, L"Windows.UI.Xaml.Controls.Image" ) || !wcscmp( cls, L"Image" )) return UWP_KIND_IMAGE;
    if (wcsstr( cls, L"ProgressRing" )) return UWP_KIND_PROGRESS_RING;
    if (wcsstr( cls, L"Grid" ) || wcsstr( cls, L"Panel" ) || wcsstr( cls, L"Canvas" )) return UWP_KIND_GRID;
    if (wcsstr( cls, L"Border" )) return UWP_KIND_BORDER;
    if (wcsstr( cls, L"RadioButtons" )) return UWP_KIND_CONTENT_CONTROL;
    if (wcsstr( cls, L"RadioButton" )) return UWP_KIND_TOGGLE_BUTTON;
    if (wcsstr( cls, L"ToggleButton" )) return UWP_KIND_TOGGLE_BUTTON;
    if (wcsstr( cls, L"Button" )) return UWP_KIND_BUTTON;
    if (wcsstr( cls, L"TextBlock" )) return UWP_KIND_TEXT_BLOCK;
    if (wcsstr( cls, L"ScrollViewer" )) return UWP_KIND_SCROLL_VIEWER;
    if (wcsstr( cls, L"UserControl" )) return UWP_KIND_USER_CONTROL;
    if (wcsstr( cls, L"ContentControl" ) || wcsstr( cls, L"NavigationView" ) ||
        wcsstr( cls, L"Pivot" ) || wcsstr( cls, L"Viewbox" ) ||
        wcsstr( cls, L"DesktopWindowXamlSource" )) return UWP_KIND_CONTENT_CONTROL;
    if (wcsstr( cls, L"ResourceDictionary" ) || !wcscmp( cls, L"Microsoft.UI.Xaml.Controls.XamlControlsResources" ))
        return UWP_KIND_RESOURCE_DICT;
    if (!wcscmp( cls, L"Windows.UI.Color" )) return UWP_KIND_COLOR;
    if (wcsstr( cls, L"Style" ) || wcsstr( cls, L"Setter" ) || wcsstr( cls, L"VisualState" )) return UWP_KIND_STYLE;
    if (wcsstr( cls, L"Brush" )) return UWP_KIND_BRUSH;
    if (!wcscmp( cls, L"Windows.UI.Xaml.Controls.Flyout" ) ||
        !wcscmp( cls, L"Windows.UI.Xaml.Controls.MenuFlyout" ) ||
        !wcscmp( cls, L"Microsoft.UI.Xaml.Controls.CommandBarFlyout" ) ||
        !wcscmp( cls, L"Windows.UI.Xaml.Controls.Primitives.FlyoutBase" ) ||
        !wcscmp( cls, L"Flyout" ) || !wcscmp( cls, L"MenuFlyout" ))
        return UWP_KIND_FLYOUT;
    if (!wcscmp( cls, L"Windows.UI.Xaml.Controls.Primitives.Popup" ) ||
        !wcscmp( cls, L"Microsoft.UI.Xaml.Controls.Primitives.Popup" ))
        return UWP_KIND_POPUP;
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
                  ce ? ce->binding_connector : NULL, UWP_EV_CNT( ce, click_reg ) );
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
        const struct abi_dts_overrides *adts = (const struct abi_dts_overrides *)ovr;
        if (adts->lpVtbl->SelectTemplateCore) adts->lpVtbl->SelectTemplateCore( ovr, item, container, (void **)out );
        IUnknown_Release( (IUnknown *)ovr );
    }
    if (!*out && SUCCEEDED(IInspectable_QueryInterface( impl->outer, &IID_IDataTemplateSelectorOverrides2_UWP, &ovr )) && ovr)
    {
        const struct abi_dts_overrides2 *adts2 = (const struct abi_dts_overrides2 *)ovr;
        if (adts2->lpVtbl->SelectTemplateForItemCore) adts2->lpVtbl->SelectTemplateForItemCore( ovr, item, (void **)out );
        IUnknown_Release( (IUnknown *)ovr );
    }
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
        const struct abi_dts_overrides2 *adts2 = (const struct abi_dts_overrides2 *)ovr;
        if (adts2->lpVtbl->SelectTemplateForItemCore) adts2->lpVtbl->SelectTemplateForItemCore( ovr, item, (void **)out );
        IUnknown_Release( (IUnknown *)ovr );
    }
    if (!*out && SUCCEEDED(IInspectable_QueryInterface( impl->outer, &IID_IDataTemplateSelectorOverrides_UWP, &ovr )) && ovr)
    {
        const struct abi_dts_overrides *adts = (const struct abi_dts_overrides *)ovr;
        if (adts->lpVtbl->SelectTemplateCore) adts->lpVtbl->SelectTemplateCore( ovr, item, NULL, (void **)out );
        IUnknown_Release( (IUnknown *)ovr );
    }
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
    void **handlers = NULL;
    unsigned int i, count;
    if (!impl || UWP_EV_CNT( impl, evc_reg ) == 0 || impl->evc_fired) return;
    impl->evc_fired = TRUE;
    count = xaml_event_registry_snapshot_alloc( &impl->events->evc_reg, &handlers );
    for (i = 0; i < count; i++)
    {
        uwp_fire_evc_one( impl, (IInspectable *)handlers[i] );
        IUnknown_Release( (IUnknown *)handlers[i] );
    }
    free( handlers );
}

static HRESULT WINAPI uwp_fe6_get_ActualTheme( IInspectable *iface, int *val )
{
    struct uwp_element *impl = CONTAINING_RECORD( iface, struct uwp_element, ILoadedImageSurface_iface );
    int theme = uwp_is_light_theme( impl ) ? 1 : 2;
    TRACE( "uwp_fe6_get_ActualTheme(%s, %s) -> %d\n",
           debugstr_w(impl->class_name), debugstr_w(impl->name), theme );
    if (val) *val = theme;
    return S_OK;
}
static HRESULT WINAPI uwp_fe6_add_ActualThemeChanged( IInspectable *iface, void *handler, EventRegistrationToken *tok )
{
    struct uwp_element *impl = CONTAINING_RECORD( iface, struct uwp_element, ILoadedImageSurface_iface );
    struct uwp_event_bag *ev = uwp_elem_ensure_events( impl );
    if (tok) tok->value = 0;
    if (!ev) return E_OUTOFMEMORY;
    return xaml_event_registry_add( &ev->actual_theme_reg, handler, tok );
}
static HRESULT WINAPI uwp_fe6_remove_ActualThemeChanged( IInspectable *iface, EventRegistrationToken tok )
{
    struct uwp_element *impl = CONTAINING_RECORD( iface, struct uwp_element, ILoadedImageSurface_iface );
    if (!impl || !impl->events) return S_OK;
    return xaml_event_registry_remove( &impl->events->actual_theme_reg, tok );
}
void uwp_fire_actual_theme_changed( struct uwp_element *el )
{
    if (!el || !el->events || el->events->actual_theme_reg.count == 0) return;
    uwp_fire_event_registry( &el->events->actual_theme_reg,
                             el->outer ? el->outer : &el->IInspectable_iface,
                             NULL, "FrameworkElement::ActualThemeChanged" );
}
const void *uwp_fe6_vtbl[12] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_fe6_get_ActualTheme,
    [7] = uwp_fe6_add_ActualThemeChanged,
    [8] = uwp_fe6_remove_ActualThemeChanged,
    [9 ... 11] = uwp_generic_noop,
};

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
    struct uwp_event_bag *ev = uwp_elem_ensure_events( impl );
    HRESULT hr;
    if (!ev) return E_OUTOFMEMORY;
    hr = xaml_event_registry_add( &ev->evc_reg, handler, tok );
    if (SUCCEEDED(hr) && handler)
    {
        WARN( "EVC_ADD %s (%s) h=%p tok=%I64d\n", debugstr_w(impl->class_name), debugstr_w(impl->name),
              handler, tok ? tok->value : 0 );
        impl->evc_fired = FALSE;
        uwp_invalidate_window();
    }
    return hr;
}
static HRESULT WINAPI uwp_fe7_remove_EffectiveViewportChanged( IInspectable *iface, EventRegistrationToken tok )
{
    struct uwp_element *impl = CONTAINING_RECORD( iface, struct uwp_element, IFrameworkElement7_iface );
    if (uwp_is_items_repeater( impl ))
        WARN( "EVC_REM %s (%s) tok=%lld\n", debugstr_w(impl->class_name), debugstr_w(impl->name), (long long)tok.value );
    if (!impl || !impl->events) return S_OK;
    return xaml_event_registry_remove( &impl->events->evc_reg, tok );
}
static HRESULT WINAPI uwp_fe7_InvalidateViewport( IInspectable *iface )
{
    struct uwp_element *impl = CONTAINING_RECORD( iface, struct uwp_element, IFrameworkElement7_iface );
    void **handlers = NULL;
    unsigned int i, count;
    if (!impl || !impl->events) return S_OK;
    count = xaml_event_registry_snapshot_alloc( &impl->events->evc_reg, &handlers );
    for (i = 0; i < count; i++)
    {
        uwp_fire_evc_one( impl, (IInspectable *)handlers[i] );
        IUnknown_Release( (IUnknown *)handlers[i] );
    }
    free( handlers );
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
void uwp_mark_in_visual_tree( IInspectable *content )
{
    struct uwp_element *el = uwp_elem_from_any( content );
    if (el) el->in_visual_tree = TRUE;
}

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
    const WCHAR *dot;
    if (!el || !full_name) return FALSE;
    if (el->class_name[0] && !wcscmp( el->class_name, full_name )) return TRUE;
    if (uwp_elem_default_style_key_name( el )[0] && !wcscmp( uwp_elem_default_style_key_name( el ), full_name )) return TRUE;
    dot = wcsrchr( full_name, L'.' );
    if ((el->kind == UWP_KIND_USER_CONTROL || el->kind == UWP_KIND_PAGE) &&
        el->name[0] && !wcscmp( el->name, dot ? dot + 1 : full_name ))
        return TRUE;
    if (el->outer && (el->kind == UWP_KIND_USER_CONTROL || el->kind == UWP_KIND_PAGE))
    {
        HSTRING hcls = NULL;
        if (SUCCEEDED(IInspectable_GetRuntimeClassName( el->outer, &hcls )) && hcls)
        {
            const WCHAR *rcls = WindowsGetStringRawBuffer( hcls, NULL );
            BOOL match = (rcls && !wcscmp( rcls, full_name ));
            WindowsDeleteString( hcls );
            if (match) return TRUE;
        }
    }
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
    IInspectable *hold_raw = NULL;
    struct uwp_element *el = NULL;
    HRESULT hr;

    if (!holder || !full_name || !full_name[0]) return FALSE;

    hr = uwp_create_instance_from_provider_el( full_name, &inst, &el );
    if (!el && inst && SUCCEEDED(hr))
        el = uwp_elem_from_any( inst );

    if (!el)
    {
        IInspectable *raw = NULL;
        if (SUCCEEDED(uwp_element_create( full_name, NULL, NULL, (void **)&raw )) && raw)
        {
            el = uwp_elem_from_any( raw );
            if (el) hold_raw = raw;
            else IInspectable_Release( raw );
        }
    }
    if (!el) return FALSE;

    if (inst)
    {
        el->provider_inst = (IInspectable *)inst;
        uwp_elem_register_alias( el, inst );
    }
    lstrcpynW( el->class_name, full_name, ARRAY_SIZE(el->class_name) );
    uwp_elem_sync_class_flags( el );
    if (!el->children_coll && !el->content)
        uwp_populate_element_from_pri_xbf( el, short_name );

    if (!el->children_coll && !el->content)
    {
        TRACE( "uwp_instantiate_xbf_control_into: %s yielded empty tree\n", debugstr_w(full_name) );
        if (hold_raw) IInspectable_Release( hold_raw );
        return FALSE;
    }

    if (!holder->children_coll) uwp_vec_create( holder, &holder->children_coll );
    uwp_vec_Append( holder->children_coll, el->outer ? (void *)el->outer : (void *)&el->IInspectable_iface );
    el->parent = holder;
    el->in_visual_tree = TRUE;
    holder->in_visual_tree = TRUE;
    if (hold_raw) IInspectable_Release( hold_raw );
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
        if (!ref->is_resource_path && uwp_tree_has_class( page_el, ref->full_name ))
        {
            ref->instantiated = TRUE;
            return TRUE;
        }
    }

    for (i = 0; i < page_el->xbf_ref_cnt; i++)
    {
        struct uwp_xbf_ref *ref = &page_el->xbf_refs[i];
        struct uwp_element *holder;

        if (ref->is_resource_path || ref->instantiated) continue;
        holder = uwp_find_empty_holder_border( page_el, ref->short_name, TRUE );
        if (!holder) holder = any_holder;
        if (holder && holder->parent && uwp_is_pivot_item( holder->parent ))
            continue;
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

static const GUID IID_IPageStackEntry_UWP =
    { 0xef8814a6, 0x9388, 0x4aca, { 0x85, 0x72, 0x40, 0x51, 0x94, 0x06, 0x90, 0x80 } };

static HRESULT WINAPI uwp_pse_QI( void *iface, REFIID iid, void **out )
{
    struct uwp_nav_event_args *a = impl_from_nav_args( iface );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IPageStackEntry_UWP ) ||
        IsEqualGUID( iid, &IID_IDependencyObject ))
    {
        InterlockedIncrement( &a->ref );
        *out = &a->INavigationEventArgs_iface;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_INavigationEventArgs_UWP ))
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
static HRESULT WINAPI uwp_pse_GetIids( void *iface, ULONG *cnt, IID **iids )
{
    IID *out = CoTaskMemAlloc( sizeof(IID) );
    if (!out) return E_OUTOFMEMORY;
    out[0] = IID_IPageStackEntry_UWP;
    *cnt = 1;
    *iids = out;
    return S_OK;
}
static HRESULT WINAPI uwp_pse_GetRuntimeClassName( void *iface, HSTRING *name )
{
    static const WCHAR cls[] = L"Windows.UI.Xaml.Navigation.PageStackEntry";
    return WindowsCreateString( cls, ARRAY_SIZE(cls) - 1, name );
}
static HRESULT WINAPI uwp_pse_get_NavigationTransitionInfo( void *iface, IInspectable **val )
{
    struct uwp_nav_event_args *a = impl_from_nav_args( iface );
    if (!val) return E_POINTER;
    *val = a->transition_info;
    if (*val) IInspectable_AddRef( *val );
    return S_OK;
}
static const void *uwp_pse_vtbl[] =
{
    uwp_pse_QI, uwp_nea_AddRef, uwp_nea_Release,
    uwp_pse_GetIids, uwp_pse_GetRuntimeClassName, uwp_nea_GetTrustLevel,
    uwp_nea_get_SourcePageType,
    uwp_nea_get_Parameter,
    uwp_pse_get_NavigationTransitionInfo,
};

struct uwp_nav_event_args *uwp_page_stack_entry_create( IInspectable *content, IInspectable *param,
                                                        const TypeName *type, IInspectable *transition_info )
{
    struct uwp_nav_event_args *a = uwp_nav_event_args_create( content, param, type, transition_info );
    if (a) a->INavigationEventArgs_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_pse_vtbl;
    return a;
}

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

static BOOL uwp_btn_label_matches_key( const WCHAR *raw, const WCHAR *disp, const WCHAR *text )
{
    if (raw && raw[0] && !lstrcmpiW( raw, text )) return TRUE;
    if (disp && disp[0] && !lstrcmpiW( disp, text )) return TRUE;
    if (text && text[0] && !text[1])
    {
        WCHAR ch = text[0];
        const WCHAR *s = (raw && raw[0]) ? raw : disp;
        if (s && s[0] && !s[1])
        {
            WCHAR g = s[0];
            if (ch == L'*' && (g == 0x00d7 || g == 0xe947)) return TRUE;
            if (ch == L'/' && (g == 0x00f7 || g == 0xe94a)) return TRUE;
            if (ch == L'+' && g == 0xe948) return TRUE;
            if (ch == L'-' && g == 0xe949) return TRUE;
            if (ch == L'=' && g == 0xe94e) return TRUE;
            if (ch == L'\b' && (g == 0x232b || g == 0xe94f)) return TRUE;
        }
    }
    return FALSE;
}

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
    if (el->kind == UWP_KIND_BUTTON || el->kind == UWP_KIND_TOGGLE_BUTTON)
    {
        WCHAR tmp[64];
        const WCHAR *disp = uwp_has_text( el ) ? uwp_format_display_label( el, tmp, ARRAY_SIZE(tmp) ) : L"";
        if (uwp_btn_label_matches_key( uwp_elem_text( el ), disp, text ))
            return el;
        if (el->content)
        {
            struct uwp_element *ce = uwp_elem_from_any( el->content );
            if (ce && uwp_has_text( ce ))
            {
                const WCHAR *cdisp = uwp_format_display_label( ce, tmp, ARRAY_SIZE(tmp) );
                if (uwp_btn_label_matches_key( uwp_elem_text( ce ), cdisp, text ))
                    return el;
            }
        }
    }
    return NULL;
}

void uwp_fire_asb_recursive( struct uwp_element *el, const WCHAR *text, BOOL is_submit )
{
    if (!el) return;
    if (text && el->text != text && uwp_is_text_input( el ))
        uwp_elem_set_text( el, text );
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

struct uwp_element *g_pressed_btn = NULL;
struct uwp_element *g_pressed_tabview = NULL;
BOOL uwp_fire_element_pointer( struct uwp_element *target, enum xaml_pointer_event_kind kind,
                                      const struct xaml_pointer_info *info, BOOL bubble );

/* ============================================================================
 * 7c. Non-Blocking WinUI MenuFlyout / MenuBarItem Popup Window
 * ============================================================================ */



HRESULT WINAPI dp_args_QI( void *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IDependencyPropertyChangedEventArgs_UWP ) ||
        IsEqualGUID( iid, &IID_IRoutedEventArgs_Clone_UWP ))
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
        if ((ULONG_PTR)a->dp >= 0x10000) IUnknown_Release( (IUnknown *)a->dp );
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
