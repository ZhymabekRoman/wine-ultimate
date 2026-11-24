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



/* Split from uwp_xaml.c: data-context propagation, template apply, Size/Layout/Loaded tree fires, PRI/XBF realize. */

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
    }
    if (wcsstr( impl->class_name, L"Card" ))
    {
        static const GUID IID_IDTE = { 0x595e9547, 0xcdff, 0x4b92, { 0xb7, 0x73, 0xab, 0x39, 0x68, 0x78, 0xf3, 0x53 } };
        void *dte = NULL;
        if (impl->binding_connector &&
            SUCCEEDED(IUnknown_QueryInterface( (IUnknown *)impl->binding_connector, &IID_IDTE, &dte )) && dte)
            IUnknown_Release( (IUnknown *)dte );
        WARN( "Card DC(%p) dc=%p dc_cnt=%d bc=%p dte=%p clicks=%d cmd=%p\n",
              impl, dc, impl->dc_cnt, impl->binding_connector, dte, impl->click_cnt, impl->command );
    }

    if (dc)
    {
        if (uwp_is_display_result_element( impl ))
        {
            uwp_calc_vm_set_instance( dc, NULL );
        }
        else
        {
            void *npc = NULL;
            if (SUCCEEDED(IInspectable_QueryInterface( dc, &IID_INotifyPropertyChanged_UWP, &npc )) && npc)
            {
                IUnknown_Release( (IUnknown *)npc );
                if (!uwp_get_app_outer() || uwp_vm_supports_display_or_cmd( dc ))
                    uwp_calc_vm_set_instance( dc, NULL );
            }
        }
    }
    else if (!xaml_get_app_caps()->teardown_vm && g_calc_vm.vm)
    {
        uwp_calc_vm_teardown( FALSE );
    }

    if (dc && dc != old_dc && impl->dt_binding_connector)
    {
        static const GUID IID_IDTE_g = { 0x595e9547, 0xcdff, 0x4b92, { 0xb7, 0x73, 0xab, 0x39, 0x68, 0x78, 0xf3, 0x53 } };
        void *bc_src = impl->dt_binding_connector;
        void *dte = NULL;
        if (SUCCEEDED(IUnknown_QueryInterface( (IUnknown *)bc_src, &IID_IDTE_g, &dte )) && dte)
        {
            struct uwp_ccc_args *ccc = calloc( 1, sizeof(*ccc) );
            if (ccc)
            {
                INT32 next_phase = -1;
                HRESULT (WINAPI *proc_bindings)(void *, void *, INT32 *) = (void *)(*(void ***)dte)[8];
                ccc->vtbl = uwp_ccc_vtbl;
                ccc->ref = 1;
                ccc->item = dc;
                IInspectable_AddRef( dc );
                ccc->item_container = &impl->IInspectable_iface;
                IInspectable_AddRef( ccc->item_container );
                ccc->phase = 0;
                ccc->in_recycle = FALSE;
                if (proc_bindings)
                {
                    __TRY
                    {
                        proc_bindings( dte, ccc, &next_phase );
                    }
                    __EXCEPT_ALL {}
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

    for (i = 0; i < impl->dc_cnt; i++)
    {
        if (impl->dc_handlers[i])
        {
            struct uwp_dc_args *args = calloc( 1, sizeof(*args) );
            if (args)
            {
                IInspectable *sender = impl->outer ? impl->outer : &impl->IInspectable_iface;
                struct xaml_event_handler *h = (struct xaml_event_handler *)impl->dc_handlers[i];
                args->vtbl = dc_args_vtbl;
                args->ref = 1;
                args->new_val = dc;
                if (dc) IInspectable_AddRef( dc );
                h->lpVtbl->Invoke( h, sender, args );
                dc_args_Release( args );
            }
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
    if (el->parent && !wcsncmp( el->parent->class_name, L"Microsoft.UI.Xaml.", 18 ))
        return;
    if (!el->template_inflated)
        uwp_inflate_control_template( el );
    if (el->named_cnt > 0 || el->size_changed_cnt > 0 || el->template_root != NULL)
        has_children = TRUE;
    /* On Windows XAML, OnApplyTemplate() is only called after a ControlTemplate
     * has actually been inflated into the control's visual tree (except WinUI
     * builtin controls like NavigationView/ItemsRepeater that synthesize parts). */
    if (!has_children && wcsncmp( el->class_name, L"Microsoft.UI.Xaml.", 18 ) != 0 &&
        !uwp_is_display_result_element( el ))
        return;
    el->template_applied = TRUE;
    if (SUCCEEDED(IInspectable_QueryInterface( el->outer, &IID_IFrameworkElementOverrides_UWP, &fe_ovr )) && fe_ovr)
    {
        /* Slot 8 on IFrameworkElementOverrides is OnApplyTemplate() */
        HRESULT (WINAPI *on_apply)(void *) = (void *)(*(void ***)fe_ovr)[8];
        if (on_apply)
        {
            WARN( "Calling IFrameworkElementOverrides::OnApplyTemplate on %s (%s) named_cnt=%d\n",
                   debugstr_w(el->class_name), debugstr_w(el->name), el->named_cnt );
            __TRY
            {
                on_apply( fe_ovr );
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
    if (xaml_get_app_caps()->display_sync && uwp_is_display_result_element( el ))
    {
        uwp_calc_vm_ensure();
        uwp_sync_calc_display_from_vm();
    }
}

void uwp_fire_size_and_layout_changed( struct uwp_element *el )
{
    int i;
    float prev_w = 0.0f, prev_h = 0.0f;
    if (!el) return;
    if (el->size_changed_cnt <= 0 && el->layout_updated_cnt <= 0) return;
    if (el->actual_w <= 0.0 || el->actual_h <= 0.0) return;
    uwp_apply_template_if_needed( el );
    if (el->size_changed_cnt > 0 && (el->actual_w != el->last_sc_w || el->actual_h != el->last_sc_h))
    {
        IInspectable * volatile sender = el->outer ? el->outer : &el->IInspectable_iface;
        volatile int cnt = el->size_changed_cnt;
        prev_w = g_sc_new_w;
        prev_h = g_sc_new_h;
        g_sc_new_w = (float)el->actual_w;
        g_sc_new_h = (float)el->actual_h;
        el->last_sc_w = el->actual_w;
        el->last_sc_h = el->actual_h;
        for (i = 0; i < cnt && i < el->size_changed_cnt; i++)
        {
            struct xaml_event_handler * volatile h = (struct xaml_event_handler *)el->size_changed_handlers[i];
            if (h)
            {
                __TRY
                {
                    h->lpVtbl->Invoke( h, sender, (IInspectable *)&uwp_sc_args_obj );
                }
                __EXCEPT_ALL
                {
                }
                __ENDTRY;
            }
        }
        g_sc_new_w = prev_w;
        g_sc_new_h = prev_h;
    }
    if (el->layout_updated_cnt > 0)
    {
        int cnt = el->layout_updated_cnt;
        for (i = 0; i < cnt && i < el->layout_updated_cnt; i++)
        {
            if (el->layout_updated_handlers[i])
            {
                struct xaml_event_handler *h = (struct xaml_event_handler *)el->layout_updated_handlers[i];
                __TRY
                {
                    h->lpVtbl->Invoke( h, NULL, NULL );
                }
                __EXCEPT_ALL
                {
                }
                __ENDTRY;
            }
        }
    }
    g_sc_new_w = prev_w;
    g_sc_new_h = prev_h;
}

void uwp_fire_loaded_tree( struct uwp_element *el )
{
    int i;
    if (!el) return;

    uwp_apply_template_if_needed( el );

    if (!el->loaded_fired && el->loading_cnt > 0)
    {
        IInspectable *sender = el->outer ? el->outer : &el->IInspectable_iface;
        for (i = 0; i < el->loading_cnt; i++)
        {
            if (el->loading_handlers[i])
            {
                struct xaml_event_handler *h = (struct xaml_event_handler *)el->loading_handlers[i];
                TRACE( "Firing Loading handler[%d] on %s (%s)\n",
                       i, debugstr_w(el->class_name), debugstr_w(el->name) );
                __TRY
                {
                    h->lpVtbl->Invoke( h, sender, NULL );
                }
                __EXCEPT_ALL
                {
                    WARN( "Loading handler[%d] on %s (%s) threw\n",
                          i, debugstr_w(el->class_name), debugstr_w(el->name) );
                }
                __ENDTRY;
            }
        }
    }

    el->loaded_fired = TRUE;
    if (el->loaded_fired_cnt < el->loaded_cnt)
    {
        IInspectable *sender = el->outer ? el->outer : &el->IInspectable_iface;
        int start = el->loaded_fired_cnt;
        el->loaded_fired_cnt = el->loaded_cnt;
        for (i = start; i < el->loaded_cnt; i++)
        {
            if (el->loaded_handlers[i])
            {
                struct xaml_event_handler *h = (struct xaml_event_handler *)el->loaded_handlers[i];
                TRACE( "Firing Loaded handler[%d] on %s (%s)\n",
                       i, debugstr_w(el->class_name), debugstr_w(el->name) );
                __TRY
                {
                    HRESULT lhr = h->lpVtbl->Invoke( h, sender, g_routed_args() );
                    if (FAILED(lhr))
                        WARN( "Loaded handler[%d] on %s (%s) returned hr %#lx\n",
                              i, debugstr_w(el->class_name), debugstr_w(el->name), lhr );
                }
                __EXCEPT_ALL
                {
                    struct uwp_element *se = uwp_elem_from_any( sender );
                    WARN( "Loaded handler[%d] on %s (%s) threw exception (sender=%s args=%s)\n",
                          i, debugstr_w(el->class_name), debugstr_w(el->name),
                          se ? debugstr_w( se->class_name ) : "\"?\"",
                          debugstr_w( g_routed_args_el ? g_routed_args_el->class_name : L"?" ) );
                }
                __ENDTRY;
            }
        }
        /* EVC is fired during Arrange (uwp_fire_evc_if_needed) once Measure has run. */
    }
    if (el->size_changed_cnt > 0 && el->actual_w > 0.0 && el->actual_h > 0.0 &&
        (el->actual_w != el->last_sc_w || el->actual_h != el->last_sc_h))
    {
        IInspectable *sender = el->outer ? el->outer : &el->IInspectable_iface;
        float prev_w = g_sc_new_w, prev_h = g_sc_new_h;
        g_sc_new_w = (float)el->actual_w;
        g_sc_new_h = (float)el->actual_h;
        el->last_sc_w = el->actual_w;
        el->last_sc_h = el->actual_h;
        for (i = 0; i < el->size_changed_cnt; i++)
        {
            if (el->size_changed_handlers[i])
            {
                struct xaml_event_handler *h = (struct xaml_event_handler *)el->size_changed_handlers[i];
                TRACE( "Firing SizeChanged handler[%d] on %s (%s) w=%.0f h=%.0f\n",
                       i, debugstr_w(el->class_name), debugstr_w(el->name), el->actual_w, el->actual_h );
                __TRY
                {
                    h->lpVtbl->Invoke( h, sender, (IInspectable *)&uwp_sc_args_obj );
                }
                __EXCEPT_ALL
                {
                    WARN( "SizeChanged handler[%d] on %s threw exception\n", i, debugstr_w(el->class_name) );
                }
                __ENDTRY;
            }
        }
        g_sc_new_w = prev_w;
        g_sc_new_h = prev_h;
    }
    if (el->layout_updated_cnt > 0)
    {
        int cnt = el->layout_updated_cnt;
        for (i = 0; i < cnt && i < el->layout_updated_cnt; i++)
        {
            if (el->layout_updated_handlers[i])
            {
                struct xaml_event_handler *h = (struct xaml_event_handler *)el->layout_updated_handlers[i];
                TRACE( "Firing LayoutUpdated handler[%d] on %s (%s) w=%.0f h=%.0f\n",
                       i, debugstr_w(el->class_name), debugstr_w(el->name), el->actual_w, el->actual_h );
                IUnknown_AddRef( (IUnknown *)h );
                __TRY
                {
                    h->lpVtbl->Invoke( h, NULL, NULL );
                }
                __EXCEPT_ALL
                {
                    WARN( "LayoutUpdated handler[%d] on %s threw exception\n", i, debugstr_w(el->class_name) );
                }
                __ENDTRY;
                IUnknown_Release( (IUnknown *)h );
            }
        }
    }

    /* NavigationView raises SelectionChanged/ItemInvoked once after its own
     * Loaded handlers so the app's navigation handler populates the content
     * Frame (ContentFrame1.Navigate in Clock's MainPage) before we recurse. */
    if (!el->nav_fired && el->class_name[0] && wcsstr( el->class_name, L".Controls.NavigationView" ))
    {
        struct uwp_element *item = uwp_nav_find_item( el, 0 );
        IInspectable *item_obj = item ? (item->outer ? item->outer : &item->IInspectable_iface) : NULL;
        el->nav_fired = TRUE;
        uwp_nav_fire( el, &el->nav_sel_changed_reg, item_obj );
        uwp_nav_fire( el, &el->nav_item_invoked_reg, item_obj );
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

