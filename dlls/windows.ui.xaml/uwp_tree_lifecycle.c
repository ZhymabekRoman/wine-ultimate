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
#define WIDL_using_Windows_ApplicationModel
#define WIDL_using_Windows_Management_Deployment
#include "windows.management.deployment.h"
#define WIDL_using_Windows_UI_Composition
#include "windows.ui.composition.h"
#include "uwp_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(xaml);



/* Split from uwp_xaml.c: data-context propagation, template apply, Size/Layout/Loaded tree fires, PRI/XBF realize. */

static const GUID IID_IDTE = { 0x595e9547, 0xcdff, 0x4b92, { 0xb7, 0x73, 0xab, 0x39, 0x68, 0x78, 0xf3, 0x53 } };

void uwp_propagate_data_context( struct uwp_element *impl, IInspectable *dc )
{
    int i;
    IInspectable *old_dc;
    if (!impl) return;
    old_dc = impl->data_context;
    if (old_dc) IInspectable_AddRef( old_dc );
    if (impl->data_context != dc)
    {
        if (impl->data_context) IInspectable_Release( impl->data_context );
        impl->data_context = dc;
        if (dc) IInspectable_AddRef( dc );
        impl->probe_flags = 0;
        if (!uwp_is_text_input( impl )) uwp_elem_set_placeholder( impl, NULL );
    }
    if (uwp_is_card( impl ))
    {
        void *dte = NULL;
        if (impl->binding_connector &&
            SUCCEEDED(IUnknown_QueryInterface( (IUnknown *)impl->binding_connector, &IID_IDTE, &dte )) && dte)
            IUnknown_Release( (IUnknown *)dte );
        WARN( "Card DC(%p) dc=%p dc_cnt=%d bc=%p dte=%p clicks=%d cmd=%p\n",
              impl, dc, UWP_EV_CNT( impl, dc_reg ), impl->binding_connector, dte,
              UWP_EV_CNT( impl, click_reg ), impl->command );
    }

    if (dc && dc != old_dc && impl->dt_binding_connector)
    {
        void *bc_src = impl->dt_binding_connector;
        void *dte = NULL;
        if (SUCCEEDED(IUnknown_QueryInterface( (IUnknown *)bc_src, &IID_IDTE, &dte )) && dte)
        {
            struct uwp_ccc_args *ccc = calloc( 1, sizeof(*ccc) );
            if (ccc)
            {
                const struct abi_data_template_extension *adte = (const struct abi_data_template_extension *)dte;
                INT32 next_phase = -1;
                ccc->vtbl = uwp_ccc_vtbl;
                ccc->ref = 1;
                ccc->item = dc;
                IInspectable_AddRef( dc );
                ccc->item_container = &impl->IInspectable_iface;
                IInspectable_AddRef( ccc->item_container );
                ccc->phase = 0;
                ccc->in_recycle = FALSE;
                if (adte->lpVtbl->ProcessBindings)
                {
                    __TRY
                    {
                        int guard = 0;
                        do
                        {
                            UINT32 cur_phase = ccc->phase;
                            next_phase = -1;
                            if (FAILED(adte->lpVtbl->ProcessBindings( dte, ccc, &next_phase ))) break;
                            if (next_phase <= (INT32)cur_phase) break;
                            ccc->phase = (UINT32)next_phase;
                        } while (++guard < 8);
                    }
                    __EXCEPT_ALL { WARN( "%s: exception 0x%08lx\n", __func__, (unsigned long)GetExceptionCode() ); }
                    __ENDTRY;
                }
                uwp_ccc_Release( ccc );
            }
            IUnknown_Release( (IUnknown *)dte );
        }
    }

    for (i = 0; i < impl->dc_bound_dp_cnt; i++)
    {
        if (impl->dc_bound_dps[i].dp && dc && dc != old_dc)
            uwp_apply_binding( impl, impl->dc_bound_dps[i].dp, impl->dc_bound_dps[i].binding );
    }
    for (i = 0; i < impl->xaml_binding_cnt; i++)
    {
        if (!impl->xaml_bindings[i].source && dc && dc != old_dc)
            uwp_eval_xaml_binding( impl, impl->xaml_bindings[i].owner_cls,
                                   impl->xaml_bindings[i].prop_name,
                                   impl->xaml_bindings[i].path, dc );
    }

    if (dc != old_dc && UWP_EV_CNT( impl, dc_reg ) > 0)
    {
        struct uwp_dc_args *args = calloc( 1, sizeof(*args) );
        if (args)
        {
            IInspectable *sender = impl->outer ? impl->outer : &impl->IInspectable_iface;
            args->vtbl = dc_args_vtbl;
            args->ref = 1;
            args->new_val = dc;
            if (dc) IInspectable_AddRef( dc );
            uwp_fire_event_registry( &impl->events->dc_reg, sender, (IInspectable *)args, "DataContextChanged" );
            dc_args_Release( args );
        }
    }

    if (impl->content)
    {
        struct uwp_element *ch = uwp_elem_from_any( impl->content );
        if (ch && (!ch->data_context || (old_dc && ch->data_context == old_dc)) && ch->data_context != dc)
            uwp_propagate_data_context( ch, dc );
    }
    if (impl->children_coll)
    {
        struct uwp_vec_view *v = CONTAINING_RECORD( impl->children_coll, struct uwp_vec_view, IInspectable_iface );
        unsigned int k;
        for (k = 0; k < v->count; k++)
        {
            struct uwp_element *ch = uwp_elem_from_any( v->items[k] );
            if (ch && (!ch->data_context || (old_dc && ch->data_context == old_dc)) && ch->data_context != dc)
                uwp_propagate_data_context( ch, dc );
        }
    }
    if (old_dc) IInspectable_Release( old_dc );
}

static void uwp_apply_template_if_needed( struct uwp_element *el )
{
    void *fe_ovr = NULL;
    BOOL has_children = FALSE;
    if (!el || el->template_applied || !el->outer) return;
    if (el->parent && (el->parent->class_flags & UWP_CF_WINUI_PREFIX))
        return;
    if (!el->template_inflated)
        uwp_inflate_control_template( el );
    if (el->named_cnt > 0 || UWP_EV_CNT( el, size_changed_reg ) > 0 || el->template_root != NULL)
        has_children = TRUE;
    /* On Windows XAML, OnApplyTemplate() is only called after a ControlTemplate
     * has actually been inflated into the control's visual tree (except WinUI
     * builtin controls like NavigationView/ItemsRepeater that synthesize parts). */
    if (!has_children && !(el->class_flags & UWP_CF_WINUI_PREFIX))
        return;
    el->template_applied = TRUE;
    if (SUCCEEDED(IInspectable_QueryInterface( el->outer, &IID_IFrameworkElementOverrides_UWP, &fe_ovr )) && fe_ovr)
    {
        /* Slot 8 on IFrameworkElementOverrides is OnApplyTemplate() */
        const struct abi_fe_overrides *afo = (const struct abi_fe_overrides *)fe_ovr;
        if (afo->lpVtbl->OnApplyTemplate)
        {
            WARN( "Calling IFrameworkElementOverrides::OnApplyTemplate on %s (%s) named_cnt=%d\n",
                   debugstr_w(el->class_name), debugstr_w(el->name), el->named_cnt );
            __TRY
            {
                afo->lpVtbl->OnApplyTemplate( fe_ovr );
            }
            __EXCEPT_ALL
            {
                WARN( "OnApplyTemplate on %s (%s) threw exception\n",
                      debugstr_w(el->class_name), debugstr_w(el->name) );
            }
            __ENDTRY;
        }
        IUnknown_Release( (IUnknown *)fe_ovr );
    }
    uwp_sync_all_template_presenters( el );
}

void uwp_fire_size_and_layout_changed( struct uwp_element *el )
{
    float prev_w = 0.0f, prev_h = 0.0f;
    if (!el) return;
    if (UWP_EV_CNT( el, size_changed_reg ) == 0 && UWP_EV_CNT( el, layout_updated_reg ) == 0) return;
    if (el->actual_w <= 0.0 || el->actual_h <= 0.0) return;
    uwp_apply_template_if_needed( el );
    if (UWP_EV_CNT( el, size_changed_reg ) > 0 && (el->actual_w != el->last_sc_w || el->actual_h != el->last_sc_h))
    {
        IInspectable *sender = el->outer ? el->outer : &el->IInspectable_iface;
        prev_w = g_sc_new_w;
        prev_h = g_sc_new_h;
        g_sc_new_w = (float)el->actual_w;
        g_sc_new_h = (float)el->actual_h;
        el->last_sc_w = el->actual_w;
        el->last_sc_h = el->actual_h;
        uwp_fire_event_registry( &el->events->size_changed_reg, sender, (IInspectable *)&uwp_sc_args_obj, "SizeChanged" );
        g_sc_new_w = prev_w;
        g_sc_new_h = prev_h;
    }
    if (UWP_EV_CNT( el, layout_updated_reg ) > 0)
        uwp_fire_event_registry( &el->events->layout_updated_reg, NULL, NULL, "LayoutUpdated" );
}

void uwp_fire_loaded_tree( struct uwp_element *el )
{
    if (!el) return;

    uwp_apply_template_if_needed( el );

    if (!el->loaded_fired && UWP_EV_CNT( el, loading_reg ) > 0)
    {
        IInspectable *sender = el->outer ? el->outer : &el->IInspectable_iface;
        uwp_fire_event_registry( &el->events->loading_reg, sender, NULL, "Loading" );
    }

    el->loaded_fired = TRUE;
    if (el->events && el->events->loaded_reg.count > 0 &&
        el->events->loaded_reg.next_token > el->events->loaded_fired_tok)
    {
        void **handlers = NULL;
        INT64 max_tok = el->events->loaded_fired_tok;
        unsigned int hi, cnt = xaml_event_registry_snapshot_after(
            &el->events->loaded_reg, el->events->loaded_fired_tok, &handlers, &max_tok );
        el->events->loaded_fired_tok = max_tok;
        if (cnt > 0)
        {
            IInspectable *sender = el->outer ? el->outer : &el->IInspectable_iface;
            for (hi = 0; hi < cnt; hi++)
            {
                struct xaml_event_handler *h = (struct xaml_event_handler *)handlers[hi];
                IInspectable *args = (IInspectable *)g_routed_args();
                TRACE( "Firing Loaded handler[%u] on %s (%s)\n",
                       hi, debugstr_w(el->class_name), debugstr_w(el->name) );
                __TRY
                {
                    HRESULT lhr = h->lpVtbl->Invoke( h, sender, args );
                    if (FAILED(lhr))
                        WARN( "Loaded handler[%u] on %s (%s) returned hr %#lx\n",
                              hi, debugstr_w(el->class_name), debugstr_w(el->name), lhr );
                }
                __EXCEPT_ALL
                {
                    struct uwp_element *se = uwp_elem_from_any( sender );
                    WARN( "Loaded handler[%u] on %s (%s) threw exception (sender=%s args=%s)\n",
                          hi, debugstr_w(el->class_name), debugstr_w(el->name),
                          se ? debugstr_w( se->class_name ) : "\"?\"",
                          debugstr_w( g_routed_args_el ? g_routed_args_el->class_name : L"?" ) );
                }
                __ENDTRY;
                if (args) IInspectable_Release( args );
                IUnknown_Release( (IUnknown *)h );
            }
            free( handlers );
        }
        /* EVC is fired during Arrange (uwp_fire_evc_if_needed) once Measure has run. */
    }
    if (UWP_EV_CNT( el, size_changed_reg ) > 0 && el->actual_w > 0.0 && el->actual_h > 0.0 &&
        (el->actual_w != el->last_sc_w || el->actual_h != el->last_sc_h))
    {
        IInspectable *sender = el->outer ? el->outer : &el->IInspectable_iface;
        float prev_w = g_sc_new_w, prev_h = g_sc_new_h;
        g_sc_new_w = (float)el->actual_w;
        g_sc_new_h = (float)el->actual_h;
        el->last_sc_w = el->actual_w;
        el->last_sc_h = el->actual_h;
        uwp_fire_event_registry( &el->events->size_changed_reg, sender, (IInspectable *)&uwp_sc_args_obj, "SizeChanged" );
        g_sc_new_w = prev_w;
        g_sc_new_h = prev_h;
    }
    if (UWP_EV_CNT( el, layout_updated_reg ) > 0)
        uwp_fire_event_registry( &el->events->layout_updated_reg, NULL, NULL, "LayoutUpdated" );

    /* NavigationView raises SelectionChanged/ItemInvoked once after its own
     * Loaded handlers so the app's navigation handler populates the content
     * Frame (ContentFrame1.Navigate in Clock's MainPage) before we recurse. */
    if ((el->class_flags2 & UWP_CF2_NAV_VIEW_CTRL) && (!el->nav || !el->nav->nav_fired))
    {
        struct uwp_nav_ext *nav = uwp_elem_ensure_nav( el );
        struct uwp_element *item = uwp_nav_find_item( el, 0 );
        IInspectable *item_obj = item ? (item->outer ? item->outer : &item->IInspectable_iface) : NULL;
        if (nav) nav->nav_fired = TRUE;
        uwp_nav_fire( el, UWP_EV_REG( el, nav_sel_changed_reg ), item_obj );
        uwp_nav_fire( el, UWP_EV_REG( el, nav_item_invoked_reg ), item_obj );
    }

    if (el->content)
    {
        struct uwp_element *ch = uwp_elem_from_any( el->content );
        if (ch && ch != el) uwp_fire_loaded_tree( ch );
    }
    if (el->pane)
    {
        struct uwp_element *ch = uwp_elem_from_any( el->pane );
        if (ch && ch != el) uwp_fire_loaded_tree( ch );
    }
    if (el->children_coll)
    {
        struct uwp_vec_view *v = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        unsigned int k;
        for (k = 0; k < v->count; k++)
        {
            struct uwp_element *ch = uwp_elem_from_any( v->items[k] );
            if (ch && ch != el) uwp_fire_loaded_tree( ch );
        }
    }
    if (el->named_cnt > 0)
    {
        int ni;
        for (ni = 0; ni < el->named_cnt; ni++)
        {
            struct uwp_element *ch = el->named_elems[ni];
            if (ch && ch != el && ch->parent == el && !ch->loaded_fired &&
                (UWP_EV_CNT( ch, loaded_reg ) > 0 || UWP_EV_CNT( ch, loading_reg ) > 0))
                uwp_fire_loaded_tree( ch );
        }
    }
}

void uwp_realize_empty_panel_defers( struct uwp_element *root )
{
    int d;
    if (!root) return;
    for (d = 0; d < root->defer_cnt; d++)
    {
        struct uwp_defer_entry *de = &root->defer_entries[d];
        if (!de->realized && de->parent_panel &&
            (de->parent_panel->kind == UWP_KIND_GRID ||
             de->parent_panel->kind == UWP_KIND_STACK_PANEL ||
             de->parent_panel->kind == UWP_KIND_BORDER) &&
            !uwp_panel_has_concrete_child( de->parent_panel ))
        {
            uwp_realize_deferred_entry( de );
        }
    }
}

BOOL uwp_populate_element_from_pri_xbf( struct uwp_element *el, const WCHAR *short_name )
{
    const BYTE *xbf = NULL;
    DWORD xbf_sz = 0;
    const WCHAR *lookup = short_name;

    if (!el) return FALSE;
    if (!lookup || !lookup[0])
    {
        const WCHAR *dot = wcsrchr( el->class_name, '.' );
        lookup = dot ? dot + 1 : el->class_name;
    }
    if (!lookup || !lookup[0]) return FALSE;
    if (!uwp_pri_find_xbf( lookup, &xbf, &xbf_sz ) || !xbf || xbf_sz <= 0x50)
        return FALSE;

    if (!el->children_coll && !el->content)
    {
        void *conn = NULL;
        IInspectable *comp = el->outer ? el->outer : &el->IInspectable_iface;
        if (el->outer)
            IInspectable_QueryInterface( el->outer, &IID_IComponentConnector_UWP_g, &conn );
        TRACE( "uwp_populate_element_from_pri_xbf: parsing %s.xbf (%lu bytes) into %p\n",
               debugstr_w(lookup), (unsigned long)xbf_sz, el );
        uwp_parse_and_build_xbf( xbf, xbf_sz, comp, conn, TRUE, uwp_get_app_outer(), -1, el, NULL );
        if (conn) IUnknown_Release( (IUnknown *)conn );
    }
    uwp_realize_empty_panel_defers( el );
    return el->children_coll != NULL || el->content != NULL;
}

