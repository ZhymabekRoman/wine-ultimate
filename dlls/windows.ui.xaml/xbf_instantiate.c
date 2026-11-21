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
#include "xbf_private.h"
WINE_DEFAULT_DEBUG_CHANNEL(xaml);

void xbf_exec_substream_rd( struct xbf_ctx *ctx, int sub_idx, struct uwp_element *initial_root,
                                   struct uwp_element **out_created, const struct xbf_rd_table *rd_tbl,
                                   struct uwp_rd_bucket *active_rd )
{
    DWORD sub_start = 0, end_p = 0, sub_sz = 0, p;
    struct uwp_element *obj_stack[64];
    IInspectable *raw_stack[64];
    struct xbf_stack_val val_stack[64];
    DWORD start_off_stack[64];
    UINT32 node_idx_stack[64];
    int obj_sp = 0, base_sp = 0;
    WORD prop_stack[64];
    int prop_sp = 0;
    struct xbf_stack_val last_popped;
    DWORD last_popped_off = 0;
    UINT32 last_popped_node_idx = (~0u);
    BOOL has_last_popped = FALSE;
    void *dt_binding = NULL;

    memset( val_stack, 0, sizeof(val_stack) );
    memset( &last_popped, 0, sizeof(last_popped) );
    memset( start_off_stack, 0, sizeof(start_off_stack) );
    memset( node_idx_stack, 0xff, sizeof(node_idx_stack) );

    if (out_created) *out_created = NULL;
    if (sub_idx < 0 || !xbf_get_substream_bounds( ctx, (DWORD)sub_idx, &sub_start, &end_p, &sub_sz ))
        return;
    if (ctx->sub_done)
    {
        if (ctx->sub_done[sub_idx]) return;
        ctx->sub_done[sub_idx] = TRUE;
    }

    p = sub_start;

    if (initial_root)
    {
        obj_stack[0] = initial_root;
        raw_stack[0] = initial_root->outer ? initial_root->outer : &initial_root->IInspectable_iface;
        node_idx_stack[0] = (ctx->aot && initial_root == ctx->root_el) ? 0u : (~0u);
        obj_sp = 1;
        base_sp = 1;
    }

    while (p < end_p)
    {
        DWORD cur_rel_off = p - sub_start;
        BYTE op = ctx->nodes[p++];
        if (op == 0x12) /* LineInfo (6 bytes) */
        {
            if (end_p - p < 6) break;
            p += 6;
        }
        else if (op == 0x03) /* Xmlns prefix */
        {
            DWORD plen;
            if (end_p - p < 6) break;
            plen = *(const DWORD *)(ctx->nodes + p + 2);
            if (plen > (end_p - (p + 6)) / 2) break;
            p += 6 + plen * 2;
        }
        else if (op == 0x17 || op == 0x14 || op == 0x15 || op == 0x16)
        {
            WORD tid;
            const WCHAR *cls_name = L"Windows.UI.Xaml.Controls.ContentControl";
            IInspectable *inst = NULL;
            struct uwp_element *el = NULL;
            struct xbf_stack_val sv = {0};
            int vt_init = 0;
            double num_init = 0, vec4_init[4] = {0};
            WCHAR sbuf_init[256] = {0};
            BOOL is_rd_type = FALSE;

            sv.opacity = 1.0;
            if (p + 2 > end_p) break;
            tid = *(const WORD *)(ctx->nodes + p);
            p += 2;
            if (op == 0x15 || op == 0x16)
            {
                if (!xbf_read_value( ctx, ctx->nodes, &p, end_p, &vt_init, &num_init, sbuf_init, ARRAY_SIZE(sbuf_init), vec4_init ))
                    break;
            }
            TRACE( "XBF push op=%#x tid=%#x cls=%s at %lu/%lu (sub %d)\n", op, tid,
                   (tid < ctx->type_cnt) ? debugstr_w(ctx->types[tid].full_name) : "(btype)",
                   (unsigned long)cur_rel_off, (unsigned long)end_p, sub_idx );

            if (tid == XBF_TYPE_RESOURCE_DICTIONARY) is_rd_type = TRUE;
            else if (!(tid & 0x8000) && tid < ctx->type_cnt &&
                     (wcsstr( ctx->types[tid].short_name, L"ResourceDictionary" ) ||
                      !wcscmp( ctx->types[tid].short_name, L"XamlControlsResources" ) ||
                      (prop_sp > 0 && prop_stack[prop_sp - 1] == XBF_PROP_MERGED_DICTIONARIES)))
                is_rd_type = TRUE;

            if (tid == XBF_TYPE_COLOR)
            {
                sv.kind = UWP_RD_COLOR;
                if (vt_init == 0x08) { sv.argb = (UINT32)num_init; sv.has_argb = TRUE; }
                else if (sbuf_init[0] && uwp_parse_color_string( sbuf_init, &sv.argb )) sv.has_argb = TRUE;
            }
            else if (tid == XBF_TYPE_DOUBLE)
            {
                sv.kind = UWP_RD_DOUBLE;
                sv.dval = num_init;
            }
            else if (tid == XBF_TYPE_GRID_LENGTH)
            {
                sv.kind = UWP_RD_DOUBLE;
                sv.dval = (vt_init == 0x07) ? vec4_init[1] : num_init;
            }
            else if (tid == XBF_TYPE_THICKNESS)
            {
                sv.kind = UWP_RD_THICKNESS;
                memcpy( sv.vec4, vec4_init, sizeof(sv.vec4) );
            }
            else if (tid == XBF_TYPE_BRUSH_C || tid == XBF_TYPE_BRUSH_B ||
                     (!(tid & 0x8000) && tid < ctx->type_cnt && wcsstr( ctx->types[tid].short_name, L"Brush" )))
            {
                sv.kind = UWP_RD_BRUSH;
                if (op == 0x16 && sbuf_init[0] && uwp_parse_color_string( sbuf_init, &sv.argb ))
                    sv.has_argb = TRUE;
            }
            else if (tid == XBF_TYPE_DATA_TEMPLATE)
            {
                sv.kind = UWP_RD_DATA_TEMPLATE;
            }
            else if (tid == XBF_TYPE_STYLE)
            {
                sv.kind = UWP_RD_STYLE;
            }
            else if ((op == 0x15 || op == 0x16) && sbuf_init[0])
            {
                sv.kind = UWP_RD_STRING;
                lstrcpynW( sv.str_val, sbuf_init, ARRAY_SIZE(sv.str_val) );
            }

            if (op == 0x17 && ctx->root_el)
            {
                el = ctx->root_el;
                inst = ctx->root_comp;
                if (is_rd_type && active_rd)
                {
                    el->rd_bucket = active_rd;
                    active_rd->owner_el = el;
                }
            }
            else if ((ctx->rd_only || rd_tbl) && !is_rd_type &&
                     ((!ctx->connector &&
                       !(tid < ctx->type_cnt && (wcsstr( ctx->types[tid].full_name, L"Breakpoint" ) ||
                                                 wcsstr( ctx->types[tid].full_name, L"TemplateSelector" )))) ||
                      tid == XBF_TYPE_STYLE || tid == XBF_TYPE_BRUSH_C || tid == XBF_TYPE_SETTER_A || tid == XBF_TYPE_SETTER_B ||
                      tid == XBF_TYPE_BRUSH_B || tid == XBF_TYPE_DOUBLE || tid == XBF_TYPE_STRING ||
                      tid == XBF_TYPE_THICKNESS || tid == XBF_TYPE_COLUMN_DEFINITION || tid == XBF_TYPE_RESOURCE_DICTIONARY))
            {
                /* Lightweight mode inside ResourceDictionary XBFs: no COM/visual element needed */
                el = NULL;
                inst = NULL;
            }
            else
            {
                if (tid & 0x8000)
                {
                    cls_name = uwp_btype_to_class_name( tid );
                    uwp_element_create( cls_name, NULL, NULL, (void **)&inst );
                    el = uwp_elem_from_any( inst );
                    if (el && tid == XBF_TYPE_DEFERRED_ELEMENT) el->kind = UWP_KIND_DEFERRED;
                }
                else if (tid < ctx->type_cnt)
                {
                    cls_name = ctx->types[tid].full_name;
                    if (ctx->root_el) uwp_mark_xbf_ref_instantiated( ctx->root_el, cls_name );
                    /* Do not activate ResourceString or ResourceVirtualKey via provider */
                    if (is_rd_type || wcsstr( cls_name, L"ResourceString" ) ||
                        wcsstr( cls_name, L"ResourceVirtualKey" ))
                    {
                        uwp_element_create( cls_name, NULL, NULL, (void **)&inst );
                        el = uwp_elem_from_any( inst );
                    }
                    else
                    {
                        struct uwp_element *prev_head;
                        HRESULT phr;
                        AcquireSRWLockExclusive( &g_all_uwp_elems_lock );
                        prev_head = g_all_uwp_elems_head;
                        ReleaseSRWLockExclusive( &g_all_uwp_elems_lock );
                        phr = uwp_create_instance_from_provider( cls_name, (void **)&inst );
                        if (SUCCEEDED(phr) && inst && g_all_uwp_elems_head != prev_head)
                            el = uwp_elem_from_any( inst );
                        if (!el)
                        {
                            AcquireSRWLockExclusive( &g_all_uwp_elems_lock );
                            if (g_all_uwp_elems_head != prev_head)
                            {
                                struct uwp_element *e;
                                for (e = g_all_uwp_elems_head; e && e != prev_head; e = e->next_reg)
                                {
                                    if (e->kind == UWP_KIND_USER_CONTROL || e->kind == UWP_KIND_PAGE)
                                        el = e;
                                }
                                if (!el)
                                {
                                    for (e = g_all_uwp_elems_head; e && e != prev_head; e = e->next_reg)
                                        el = e;
                                }
                            }
                            ReleaseSRWLockExclusive( &g_all_uwp_elems_lock );
                        }
                        if (el)
                        {
                            if (inst && !el->provider_inst) el->provider_inst = inst;
                            else if (!inst) inst = el->outer ? el->outer : &el->IInspectable_iface;
                            lstrcpynW( el->class_name, cls_name, ARRAY_SIZE(el->class_name) );
                            el->kind = classify_uwp_class( el->class_name );
                        }
                        if (FAILED(phr) || !inst)
                        {
                            if (!inst) uwp_element_create( cls_name, NULL, NULL, (void **)&inst );
                            if (!el) el = uwp_elem_from_any( inst );
                            if (el && !el->xbf_parsed && !el->children_coll && !el->content &&
                                !wcsncmp( ctx->types[tid].xmlns, L"using:", 6 ))
                            {
                                uwp_populate_element_from_pri_xbf( el, ctx->types[tid].short_name );
                            }
                        }
                    }
                }
                else
                {
                    uwp_element_create( cls_name, NULL, NULL, (void **)&inst );
                    el = uwp_elem_from_any( inst );
                }
            }

            if (el)
            {
                if (ctx->root_el) el->owner_component = ctx->root_el;
                if (sv.kind == UWP_RD_BRUSH && sv.has_argb)
                {
                    el->brush_argb = sv.argb;
                    el->has_brush_argb = TRUE;
                }
                if (is_rd_type)
                {
                    el->kind = UWP_KIND_RESOURCE_DICT;
                    if (op == 0x17 && active_rd)
                    {
                        el->rd_bucket = active_rd;
                        active_rd->owner_el = el;
                    }
                    else
                    {
                        struct uwp_rd_bucket *sub_rd = uwp_rd_bucket_for_el( el, TRUE );
                        if (sub_rd && !(tid & 0x8000) && tid < ctx->type_cnt &&
                            !wcsncmp( ctx->types[tid].xmlns, L"using:", 6 ) &&
                            ctx->types[tid].short_name[0] &&
                            wcscmp( ctx->types[tid].short_name, L"XamlControlsResources" ))
                        {
                            const WCHAR *ns = ctx->types[tid].xmlns + 6;
                            const WCHAR *dot = wcschr( ns, L'.' );
                            WCHAR xaml_name[256];
                            BOOL loaded = FALSE;

                            /* Try namespace-qualified path first (e.g. using:App.Resources -> Resources/Foo.xaml
                             * or App/Resources/Foo.xaml) to avoid colliding with same-named XBFs in other assemblies. */
                            if (dot && dot[1])
                            {
                                WCHAR rel_ns[128];
                                int ni;
                                lstrcpynW( rel_ns, dot + 1, ARRAY_SIZE(rel_ns) );
                                for (ni = 0; rel_ns[ni]; ni++)
                                    if (rel_ns[ni] == L'.') rel_ns[ni] = L'/';
                                swprintf( xaml_name, ARRAY_SIZE(xaml_name), L"%s/%s.xaml", rel_ns, ctx->types[tid].short_name );
                                uwp_load_res_xbf_into_bucket( ctx->base_xbf_dir, xaml_name, sub_rd );
                                loaded = (sub_rd->count > 0 || sub_rd->merged_cnt > 0 || sub_rd->theme_dark || sub_rd->theme_default);
                            }
                            if (!loaded && ns[0])
                            {
                                WCHAR full_ns[128];
                                int ni;
                                lstrcpynW( full_ns, ns, ARRAY_SIZE(full_ns) );
                                for (ni = 0; full_ns[ni]; ni++)
                                    if (full_ns[ni] == L'.') full_ns[ni] = L'/';
                                swprintf( xaml_name, ARRAY_SIZE(xaml_name), L"%s/%s.xaml", full_ns, ctx->types[tid].short_name );
                                uwp_load_res_xbf_into_bucket( ctx->base_xbf_dir, xaml_name, sub_rd );
                                loaded = (sub_rd->count > 0 || sub_rd->merged_cnt > 0 || sub_rd->theme_dark || sub_rd->theme_default);
                            }
                            if (!loaded)
                            {
                                swprintf( xaml_name, ARRAY_SIZE(xaml_name), L"%s.xaml", ctx->types[tid].short_name );
                                uwp_load_res_xbf_into_bucket( ctx->base_xbf_dir, xaml_name, sub_rd );
                            }
                        }
                    }
                }
            }
            sv.tid = tid;
            sv.el = el;
            sv.raw = inst;
            if (out_created && !*out_created && el) *out_created = el;

            if ((sub_idx == 0 || ctx->vsm_collect) && !ctx->rd_only && ctx->root_el)
            {
                if (tid == XBF_TYPE_VISUAL_STATE_GROUP)
                    uwp_vsm_add_group( ctx->root_el, NULL );
                else if (tid == XBF_TYPE_VISUAL_STATE)
                {
                    struct uwp_vsm_group *grp = (ctx->root_el->vsm_group_cnt > 0)
                        ? &ctx->root_el->vsm_groups[ctx->root_el->vsm_group_cnt - 1]
                        : uwp_vsm_add_group( ctx->root_el, L"DefaultGroup" );
                    uwp_vsm_add_state( grp, NULL );
                }
            }

            if (obj_sp < (int)ARRAY_SIZE(obj_stack))
            {
                UINT32 cur_node_idx = (~0u);
                if (ctx->aot && !ctx->aot->aborted)
                {
                    UINT8 nflags = 0;
                    const WCHAR *short_n = L"";
                    if (is_rd_type) nflags |= XBF_AOT_NODE_IS_RD;
                    if (op == 0x17 && ctx->root_el)
                    {
                        nflags |= XBF_AOT_NODE_ROOT_REUSE;
                        cur_node_idx = 0;
                        if (ctx->aot->node_cnt > 0)
                            ctx->aot->nodes[0].flags |= nflags;
                    }
                    else if ((ctx->rd_only || rd_tbl) && !is_rd_type &&
                             (!ctx->connector ||
                              tid == XBF_TYPE_STYLE || tid == XBF_TYPE_BRUSH_C || tid == XBF_TYPE_SETTER_A || tid == XBF_TYPE_SETTER_B ||
                              tid == XBF_TYPE_BRUSH_B || tid == XBF_TYPE_DOUBLE || tid == XBF_TYPE_STRING ||
                              tid == XBF_TYPE_THICKNESS || tid == XBF_TYPE_COLUMN_DEFINITION || tid == XBF_TYPE_RESOURCE_DICTIONARY))
                    {
                        nflags |= XBF_AOT_NODE_LIGHTWEIGHT;
                        cur_node_idx = xbf_aot_add_node( ctx->aot, L"", L"", tid, nflags, FALSE, 0 );
                    }
                    else if (tid & 0x8000)
                    {
                        nflags |= XBF_AOT_NODE_BUILTIN;
                        if (tid == XBF_TYPE_DEFERRED_ELEMENT) nflags |= XBF_AOT_NODE_DEFERRED;
                        cur_node_idx = xbf_aot_add_node( ctx->aot, cls_name, L"", tid, nflags,
                                                         (sv.kind == UWP_RD_BRUSH && sv.has_argb), sv.argb );
                    }
                    else if (tid < ctx->type_cnt)
                    {
                        nflags |= XBF_AOT_NODE_APP_TYPE;
                        short_n = ctx->types[tid].short_name;
                        if (!wcsncmp( ctx->types[tid].xmlns, L"using:", 6 )) nflags |= XBF_AOT_NODE_USING_NS;
                        if (is_rd_type || wcsstr( cls_name, L"ResourceString" ) || wcsstr( cls_name, L"ResourceVirtualKey" ))
                            nflags |= XBF_AOT_NODE_NO_PROVIDER;
                        cur_node_idx = xbf_aot_add_node( ctx->aot, cls_name, short_n, tid, nflags,
                                                         (sv.kind == UWP_RD_BRUSH && sv.has_argb), sv.argb );
                    }
                    else
                    {
                        cur_node_idx = xbf_aot_add_node( ctx->aot, cls_name, L"", tid, nflags,
                                                         (sv.kind == UWP_RD_BRUSH && sv.has_argb), sv.argb );
                    }
                    if (cur_node_idx != (~0u))
                    {
                        struct xbf_aot_op aop = {0};
                        aop.kind = XBF_AOT_OP_CREATE_NODE;
                        aop.node_idx = cur_node_idx;
                        aop.child_idx = (~0u);
                        xbf_aot_emit_op( ctx->aot, &aop );
                    }
                }
                obj_stack[obj_sp] = el;
                raw_stack[obj_sp] = inst;
                val_stack[obj_sp] = sv;
                start_off_stack[obj_sp] = cur_rel_off;
                node_idx_stack[obj_sp] = cur_node_idx;
                obj_sp++;
            }
            else
            {
                free( sv.setters );
            }
        }
        else if (op == 0x0b) /* x:Class */
        {
            DWORD slen;
            if (end_p - p < 4) break;
            slen = *(const DWORD *)(ctx->nodes + p);
            p += 4;
            if (slen > (end_p - p) / 2) break;
            if (obj_sp > 0 && obj_stack[obj_sp - 1] && slen > 0)
            {
                struct uwp_element *top_el = obj_stack[obj_sp - 1];
                DWORD cp = min( slen, (DWORD)(ARRAY_SIZE(top_el->x_class) - 1) );
                memcpy( top_el->x_class, ctx->nodes + p, cp * sizeof(WCHAR) );
                top_el->x_class[cp] = 0;
                if (ctx->aot && node_idx_stack[obj_sp - 1] != (~0u))
                {
                    struct xbf_aot_op aop = {0};
                    aop.kind = XBF_AOT_OP_SET_XCLASS;
                    aop.node_idx = node_idx_stack[obj_sp - 1];
                    aop.child_idx = (~0u);
                    aop.str_off = xbf_aot_intern_str( ctx->aot, top_el->x_class );
                    xbf_aot_emit_op( ctx->aot, &aop );
                }
            }
            p += slen * 2;
        }
        else if (op == 0x0c) /* ConnectionId */
        {
            int vt = 0;
            double num = 0;
            if (!xbf_read_value( ctx, ctx->nodes, &p, end_p, &vt, &num, NULL, 0, NULL )) break;
            if (obj_sp > 0)
            {
                int cid = (int)num;
                struct uwp_element *top_el = obj_stack[obj_sp - 1];
                IInspectable *top_raw = raw_stack[obj_sp - 1];
                if (top_el) top_el->conn_id = cid;
                if (ctx->aot && node_idx_stack[obj_sp - 1] != (~0u))
                {
                    struct xbf_aot_op aop = {0};
                    aop.kind = XBF_AOT_OP_CONNECT;
                    aop.node_idx = node_idx_stack[obj_sp - 1];
                    aop.child_idx = (~0u);
                    aop.cid = cid;
                    xbf_aot_emit_op( ctx->aot, &aop );
                }
                if (ctx->connector && top_raw && cid != 1)
                {
                    HRESULT (WINAPI *conn_fn)(void *, int, void *) = (void *)(*(void ***)ctx->connector)[6];
                    TRACE( "XBF Connect(cid=%d, target=%p [%s %s])\n",
                           cid, top_raw, top_el ? debugstr_w(top_el->class_name) : "?",
                           top_el ? debugstr_w(top_el->name) : "" );
                    conn_fn( ctx->connector, cid, top_raw );
                    if (ctx->root_el && ctx->root_el->binding_connector && (!out_created || initial_root))
                    {
                        HRESULT (WINAPI *b_conn)(void *, int, void *) = (void *)(*(void ***)ctx->root_el->binding_connector)[6];
                        if (b_conn) b_conn( ctx->root_el->binding_connector, cid, top_raw );
                    }
                    else if (out_created && ctx->root_comp)
                    {
                        if (!dt_binding)
                        {
                            void *conn2 = NULL, *bc = NULL;
                            if (SUCCEEDED(IInspectable_QueryInterface( ctx->root_comp, &IID_IComponentConnector2_UWP_g, &conn2 )) && conn2)
                            {
                                HRESULT (WINAPI *get_bc)(void *, int, void *, void **) = (void *)(*(void ***)conn2)[6];
                                if (get_bc && SUCCEEDED(get_bc( conn2, cid, top_raw, &bc )) && bc)
                                {
                                    dt_binding = bc;
                                    if (top_el && !top_el->binding_connector)
                                    {
                                        top_el->binding_connector = bc;
                                        IUnknown_AddRef( (IUnknown *)bc );
                                    }
                                }
                                IUnknown_Release( (IUnknown *)conn2 );
                            }
                        }
                        if (dt_binding)
                        {
                            HRESULT (WINAPI *b_conn)(void *, int, void *) = (void *)(*(void ***)dt_binding)[6];
                            if (b_conn) b_conn( dt_binding, cid, top_raw );
                        }
                    }
                }
            }
        }
        else if (op == 0x0d) /* x:Name */
        {
            int vt = 0;
            double num = 0;
            WCHAR sbuf[128] = {0};
            if (!xbf_read_value( ctx, ctx->nodes, &p, end_p, &vt, &num, sbuf, ARRAY_SIZE(sbuf), NULL )) break;
            if (obj_sp > 0 && sbuf[0])
            {
                struct uwp_element *top_el = obj_stack[obj_sp - 1];
                WORD top_tid = val_stack[obj_sp - 1].tid;
                if (top_el)
                {
                    lstrcpynW( top_el->name, sbuf, ARRAY_SIZE(top_el->name) );
                    if (ctx->root_el &&
                        xaml_vec_grow( (void **)&ctx->root_el->named_elems, &ctx->root_el->named_cap,
                                       ctx->root_el->named_cnt + 1, sizeof(*ctx->root_el->named_elems), 32 ))
                    {
                        ctx->root_el->named_elems[ctx->root_el->named_cnt++] = top_el;
                    }
                    if (ctx->aot && node_idx_stack[obj_sp - 1] != (~0u))
                    {
                        struct xbf_aot_op aop = {0};
                        aop.kind = XBF_AOT_OP_SET_XNAME;
                        aop.node_idx = node_idx_stack[obj_sp - 1];
                        aop.child_idx = (~0u);
                        aop.str_off = xbf_aot_intern_str( ctx->aot, sbuf );
                        xbf_aot_emit_op( ctx->aot, &aop );
                    }
                }
                if ((sub_idx == 0 || ctx->vsm_collect) && !ctx->rd_only && ctx->root_el && ctx->root_el->vsm_group_cnt > 0)
                {
                    struct uwp_vsm_group *grp = &ctx->root_el->vsm_groups[ctx->root_el->vsm_group_cnt - 1];
                    if (top_tid == XBF_TYPE_VISUAL_STATE_GROUP)
                        lstrcpynW( grp->name, sbuf, ARRAY_SIZE(grp->name) );
                    else if (top_tid == XBF_TYPE_VISUAL_STATE && grp->state_cnt > 0)
                        lstrcpynW( grp->states[grp->state_cnt - 1].name, sbuf, ARRAY_SIZE(grp->states[grp->state_cnt - 1].name) );
                }
            }
        }
        else if (op == 0x0e) /* x:Key */
        {
            int vt = 0;
            double num = 0;
            WCHAR sbuf[128] = {0};
            if (!xbf_read_value( ctx, ctx->nodes, &p, end_p, &vt, &num, sbuf, ARRAY_SIZE(sbuf), NULL )) break;
        }
        else if (op == 0x13) /* PushProperty */
        {
            WORD pid;
            if (p + 2 > end_p) break;
            pid = *(const WORD *)(ctx->nodes + p);
            p += 2;
            if (prop_sp < (int)ARRAY_SIZE(prop_stack))
                prop_stack[prop_sp++] = pid;
        }
        else if (op == 0x01 || op == 0x27) /* EndOfAttributes / EndConditionalScope */
        {
        }
        else if (op == 0x26) /* BeginConditionalScope <pred_tid:u16> <arg_sid:u16> */
        {
            if (p + 4 > end_p) break;
            p += 4;
        }
        else if (op == 0x04) /* PushConstant <xbf_value> */
        {
            int vt = 0;
            double num = 0, vec4[4] = {0};
            WCHAR sbuf[256] = {0};
            if (!xbf_read_value( ctx, ctx->nodes, &p, end_p, &vt, &num, sbuf, ARRAY_SIZE(sbuf), vec4 )) break;
        }
        else if (op == 0x11) /* SetDeferredProperty <pid:u16> <sub_idx:uleb> <c1:uleb> <c2:uleb> <(c1+c2)*u16> */
        {
            DWORD sub_i = 0, def_sz = 0, c1 = 0, c2 = 0;
            if (end_p - p < 2) break;
            p += 2;
            if (!xbf_read_uleb( ctx->nodes, &p, end_p, &sub_i )) break;
            if (!xbf_get_substream_bounds( ctx, sub_i, NULL, NULL, &def_sz )) break;
            if (!xbf_read_uleb( ctx->nodes, &p, end_p, &c1 )) break;
            if (!xbf_read_uleb( ctx->nodes, &p, end_p, &c2 )) break;
            (void)def_sz;
            if (c1 > (end_p - p) / 2 || c2 > (end_p - p) / 2 - c1) break;
            p += (c1 + c2) * 2;
            if (obj_sp > 0 && val_stack[obj_sp - 1].tid == XBF_TYPE_DATA_TEMPLATE)
            {
                val_stack[obj_sp - 1].template_xbf_data = ctx->xbf;
                val_stack[obj_sp - 1].template_xbf_size = ctx->xbf_sz;
                val_stack[obj_sp - 1].template_sub_idx = sub_i;
                val_stack[obj_sp - 1].template_owner_comp = ctx->root_comp;
                if (obj_stack[obj_sp - 1])
                {
                    obj_stack[obj_sp - 1]->template_xbf_data = ctx->xbf;
                    obj_stack[obj_sp - 1]->template_xbf_size = ctx->xbf_sz;
                    obj_stack[obj_sp - 1]->template_sub_idx = sub_i;
                    uwp_set_template_owner( &obj_stack[obj_sp - 1]->template_owner_comp, ctx->root_comp );
                }
                if (ctx->aot && node_idx_stack[obj_sp - 1] < ctx->aot->node_cnt)
                    ctx->aot->nodes[node_idx_stack[obj_sp - 1]].template_sub_idx = sub_i;
                TRACE( "XBF DataTemplate SetDeferredProperty captured substream %u\n", (unsigned)sub_i );
            }
        }
        else if (op == 0x02) /* PopProperty */
        {
            if (prop_sp > 0) prop_sp--;
        }
        else if (op == 0x1d) /* SetValueWithType: <pid:u16> <tid:u16> */
        {
            WORD pid, tid;
            if (p + 4 > end_p) break;
            pid = *(const WORD *)(ctx->nodes + p);
            tid = *(const WORD *)(ctx->nodes + p + 2);
            (void)pid; (void)tid;
            p += 4;
        }
        else if ((op >= 0x1a && op <= 0x1f) || op == 0x24) /* SetValue / SetStaticResource / SetThemeResource */
        {
            WORD pid;
            int vt = 0;
            double num = 0, vec4[4] = {0};
            WCHAR sbuf[256] = {0};
            BOOL is_res_ref = (op == 0x1e || op == 0x24);
            if (p + 2 > end_p) break;
            pid = *(const WORD *)(ctx->nodes + p);
            p += 2;
            if (!xbf_read_value( ctx, ctx->nodes, &p, end_p, &vt, &num, sbuf, ARRAY_SIZE(sbuf), vec4 )) break;
            if (obj_sp > 0)
            {
                struct xbf_stack_val *top_sv = &val_stack[obj_sp - 1];
                xbf_apply_property( ctx, obj_stack[obj_sp - 1], top_sv,
                                    pid, vt, num, sbuf, vec4, is_res_ref );
                if (ctx->aot && obj_stack[obj_sp - 1] && node_idx_stack[obj_sp - 1] != (~0u))
                {
                    struct xbf_aot_op aop = {0};
                    aop.kind = XBF_AOT_OP_SET_PROP;
                    aop.node_idx = node_idx_stack[obj_sp - 1];
                    aop.child_idx = (~0u);
                    aop.pid = pid;
                    aop.vt = vt;
                    aop.num = num;
                    memcpy( aop.vec4, vec4, sizeof(aop.vec4) );
                    aop.str_off = xbf_aot_intern_str( ctx->aot, sbuf );
                    aop.is_res_ref = is_res_ref ? 1 : 0;
                    xbf_aot_emit_op( ctx->aot, &aop );
                }
                if ((sub_idx == 0 || ctx->vsm_collect) && !ctx->rd_only && ctx->root_el && ctx->root_el->vsm_group_cnt > 0)
                {
                    struct uwp_vsm_group *grp = &ctx->root_el->vsm_groups[ctx->root_el->vsm_group_cnt - 1];
                    struct uwp_vsm_state *st = (grp->state_cnt > 0) ? &grp->states[grp->state_cnt - 1] : NULL;
                    if (top_sv->tid == XBF_TYPE_ADAPTIVE_TRIGGER && st && (pid == XBF_PROP_MIN_WINDOW_HEIGHT || pid == XBF_PROP_MIN_WINDOW_WIDTH))
                    {
                        double trig_val = num;
                        if ((is_res_ref || vt == 0x05 || vt == 0x09) && sbuf[0])
                        {
                            struct uwp_rd_entry *de = NULL;
                            if (uwp_rd_find_entry( active_rd ? active_rd : ctx->target_rd, ctx->root_el, sbuf, 0, &de, NULL ) &&
                                de && de->kind == UWP_RD_DOUBLE)
                                trig_val = de->dval;
                            else
                                trig_val = wcstod( sbuf, NULL );
                        }
                        if (pid == XBF_PROP_MIN_WINDOW_HEIGHT) st->min_window_height = trig_val;
                        else st->min_window_width = trig_val;
                        st->has_trigger = TRUE;
                    }
                    else if ((top_sv->tid == XBF_TYPE_SETTER_A || top_sv->tid == XBF_TYPE_SETTER_B) && st)
                    {
                        if (pid == XBF_PROP_SETTER_TARGET && sbuf[0])
                            vsm_split_setter_target( sbuf, top_sv->vsm_target, ARRAY_SIZE(top_sv->vsm_target),
                                                     top_sv->vsm_prop, ARRAY_SIZE(top_sv->vsm_prop) );
                        else if (pid == XBF_PROP_SETTER_VALUE)
                        {
                            lstrcpynW( top_sv->vsm_val_str, sbuf, ARRAY_SIZE(top_sv->vsm_val_str) );
                            top_sv->vsm_vt = vt;
                            top_sv->vsm_num = num;
                            top_sv->vsm_is_res_ref = is_res_ref;
                            top_sv->vsm_has_val = TRUE;
                        }
                    }
                    else if (top_sv->tid == XBF_TYPE_OBJECT_ANIMATION_USING_KEY_FRAMES && st)
                    {
                        if (pid == XBF_PROP_STORYBOARD_TARGET_NAME && sbuf[0])
                            lstrcpynW( top_sv->vsm_target, sbuf, ARRAY_SIZE(top_sv->vsm_target) );
                        else if (pid == XBF_PROP_STORYBOARD_TARGET_PROPERTY && sbuf[0])
                            vsm_clean_prop_name( sbuf, top_sv->vsm_prop, ARRAY_SIZE(top_sv->vsm_prop) );
                    }
                    else if (top_sv->tid == XBF_TYPE_DISCRETE_OBJECT_KEY_FRAME && st && pid == XBF_PROP_KEYFRAME_VALUE)
                    {
                        lstrcpynW( top_sv->vsm_val_str, sbuf, ARRAY_SIZE(top_sv->vsm_val_str) );
                        top_sv->vsm_vt = vt;
                        top_sv->vsm_num = num;
                        top_sv->vsm_is_res_ref = is_res_ref;
                        top_sv->vsm_has_val = TRUE;
                    }
                }
            }
        }
        else if (op == 0x22) /* AddStaticResourceValue <xbf_value> */
        {
            int vt = 0;
            double num = 0;
            WCHAR sbuf[128] = {0};
            if (!xbf_read_value( ctx, ctx->nodes, &p, end_p, &vt, &num, sbuf, ARRAY_SIZE(sbuf), NULL )) break;
            free( last_popped.setters );
            memset( &last_popped, 0, sizeof(last_popped) );
            last_popped.kind = UWP_RD_ALIAS;
            last_popped.opacity = 1.0;
            lstrcpynW( last_popped.str_val, sbuf, ARRAY_SIZE(last_popped.str_val) );
            last_popped_off = cur_rel_off;
            last_popped_node_idx = (~0u);
            has_last_popped = TRUE;
        }
        else if (op == 0x21) /* EndOfObject / PopObject */
        {
            if (obj_sp > base_sp)
            {
                free( last_popped.setters );
                obj_sp--;
                last_popped = val_stack[obj_sp];
                last_popped_off = start_off_stack[obj_sp];
                last_popped_node_idx = node_idx_stack[obj_sp];
                memset( &val_stack[obj_sp], 0, sizeof(val_stack[obj_sp]) );
                has_last_popped = TRUE;

                if ((sub_idx == 0 || ctx->vsm_collect) && !ctx->rd_only && ctx->root_el && ctx->root_el->vsm_group_cnt > 0)
                {
                    struct uwp_vsm_group *grp = &ctx->root_el->vsm_groups[ctx->root_el->vsm_group_cnt - 1];
                    struct uwp_vsm_state *st = (grp->state_cnt > 0) ? &grp->states[grp->state_cnt - 1] : NULL;
                    if ((last_popped.tid == XBF_TYPE_SETTER_A || last_popped.tid == XBF_TYPE_SETTER_B) && st &&
                        last_popped.vsm_target[0] && last_popped.vsm_prop[0] && last_popped.vsm_has_val)
                    {
                        uwp_vsm_state_add_setter( st, last_popped.vsm_target, last_popped.vsm_prop,
                                                  last_popped.vsm_val_str, last_popped.vsm_vt,
                                                  last_popped.vsm_num, last_popped.vsm_is_res_ref );
                    }
                    else if (last_popped.tid == XBF_TYPE_DISCRETE_OBJECT_KEY_FRAME && st && last_popped.vsm_has_val &&
                             obj_sp > 0 && val_stack[obj_sp - 1].tid == XBF_TYPE_OBJECT_ANIMATION_USING_KEY_FRAMES &&
                             val_stack[obj_sp - 1].vsm_target[0] && val_stack[obj_sp - 1].vsm_prop[0])
                    {
                        uwp_vsm_state_add_setter( st, val_stack[obj_sp - 1].vsm_target,
                                                  val_stack[obj_sp - 1].vsm_prop,
                                                  last_popped.vsm_val_str, last_popped.vsm_vt,
                                                  last_popped.vsm_num, last_popped.vsm_is_res_ref );
                    }
                }

                /* Indexed ResourceDictionary entry without trailing 0x0a AddToDictionary (e.g. Dark.xbf, TileDataTemplates.xbf) */
                if (active_rd && rd_tbl &&
                    (obj_sp == base_sp || (obj_sp == base_sp + 1 && obj_stack[obj_sp - 1] &&
                                           obj_stack[obj_sp - 1]->kind == UWP_KIND_RESOURCE_DICT)))
                {
                    BYTE next_op = (p < end_p) ? ctx->nodes[p] : 0xff;
                    if (next_op != 0x0a && next_op != 0x07 && next_op != 0x08 && next_op != 0x09 && next_op != 0x20)
                    {
                        int k;
                        for (k = 0; k < rd_tbl->count; k++)
                        {
                            if (rd_tbl->items[k].sub_off == last_popped_off &&
                                rd_tbl->items[k].key_sid < ctx->str_cnt && ctx->strs[rd_tbl->items[k].key_sid])
                            {
                                uwp_rd_store_stack_val( active_rd, ctx->strs[rd_tbl->items[k].key_sid], &last_popped );
                            }
                        }
                        free( last_popped.setters );
                        memset( &last_popped, 0, sizeof(last_popped) );
                        has_last_popped = FALSE;
                    }
                }
            }
            else
            {
                free( last_popped.setters );
                memset( &last_popped, 0, sizeof(last_popped) );
                last_popped_node_idx = (~0u);
                has_last_popped = FALSE;
            }
        }
        else if (op == 0x0a) /* AddToDictionary <xbf_value> */
        {
            int vt = 0;
            double num = 0;
            WCHAR kbuf[128] = {0};
            if (!xbf_read_value( ctx, ctx->nodes, &p, end_p, &vt, &num, kbuf, ARRAY_SIZE(kbuf), NULL )) break;
            if (has_last_popped && kbuf[0])
            {
                struct uwp_element *parent_el = (obj_sp > 0) ? obj_stack[obj_sp - 1] : NULL;
                WORD pid = (prop_sp > 0) ? prop_stack[prop_sp - 1] : 0;
                if (pid == XBF_PROP_THEME_DICTIONARIES && last_popped.el && last_popped.el->kind == UWP_KIND_RESOURCE_DICT)
                {
                    struct uwp_rd_bucket *parent_rd = (parent_el && parent_el->kind == UWP_KIND_RESOURCE_DICT)
                        ? uwp_rd_bucket_for_el( parent_el, TRUE )
                        : (active_rd ? active_rd : uwp_rd_bucket_for_el( parent_el, TRUE ));
                    struct uwp_rd_bucket *child_rd = uwp_rd_bucket_for_el( last_popped.el, TRUE );
                    uwp_rd_link_theme( parent_rd, kbuf, child_rd );
                    if (ctx->aot && last_popped_node_idx != (~0u))
                    {
                        struct xbf_aot_op aop = {0};
                        aop.kind = XBF_AOT_OP_RD_LINK_THEME;
                        aop.node_idx = (obj_sp > 0) ? node_idx_stack[obj_sp - 1] : (~0u);
                        aop.child_idx = last_popped_node_idx;
                        aop.str_off = xbf_aot_intern_str( ctx->aot, kbuf );
                        xbf_aot_emit_op( ctx->aot, &aop );
                    }
                }
                else
                {
                    struct uwp_rd_bucket *dest_rd = NULL;
                    if (parent_el && parent_el->kind == UWP_KIND_RESOURCE_DICT)
                        dest_rd = uwp_rd_bucket_for_el( parent_el, TRUE );
                    else
                        dest_rd = active_rd ? active_rd : (parent_el ? uwp_rd_bucket_for_el( parent_el, TRUE ) : NULL);
                    if (dest_rd)
                    {
                        uwp_rd_store_stack_val( dest_rd, kbuf, &last_popped );
                        if (active_rd && active_rd != dest_rd)
                            uwp_rd_store_stack_val( active_rd, kbuf, &last_popped );
                        if (ctx->aot)
                        {
                            struct xbf_aot_op aop = {0};
                            aop.kind = XBF_AOT_OP_RD_STORE;
                            aop.sv_kind = (UINT8)last_popped.kind;
                            aop.has_argb = last_popped.has_argb ? 1 : 0;
                            aop.tid = last_popped.tid;
                            aop.node_idx = (obj_sp > 0) ? node_idx_stack[obj_sp - 1] : (~0u);
                            aop.child_idx = last_popped_node_idx;
                            aop.str_off = xbf_aot_intern_str( ctx->aot, kbuf );
                            aop.str2_off = xbf_aot_intern_str( ctx->aot,
                                (last_popped.kind == UWP_RD_STYLE) ? last_popped.based_on : last_popped.str_val );
                            aop.argb = last_popped.argb;
                            aop.num = (last_popped.kind == UWP_RD_BRUSH || last_popped.kind == UWP_RD_COLOR)
                                ? last_popped.opacity : last_popped.dval;
                            memcpy( aop.vec4, last_popped.vec4, sizeof(aop.vec4) );
                            if (last_popped.setter_cnt > 0 && last_popped.setters)
                            {
                                aop.setter_start = xbf_aot_append_setters( ctx->aot, last_popped.setters, last_popped.setter_cnt );
                                aop.setter_cnt = (UINT32)last_popped.setter_cnt;
                            }
                            xbf_aot_emit_op( ctx->aot, &aop );
                        }
                    }
                }
            }
            free( last_popped.setters );
            memset( &last_popped, 0, sizeof(last_popped) );
            last_popped_node_idx = (~0u);
            has_last_popped = FALSE;
        }
        else if (op == 0x08) /* AddToCollection */
        {
            if (has_last_popped && obj_sp > 0)
            {
                struct uwp_element *parent_el = obj_stack[obj_sp - 1];
                IInspectable *parent_raw = raw_stack[obj_sp - 1];
                WORD pid = (prop_sp > 0) ? prop_stack[prop_sp - 1] : XBF_PROP_PANEL_CHILDREN;
                if (parent_el && pid == XBF_PROP_MERGED_DICTIONARIES && last_popped.el && last_popped.el->kind == UWP_KIND_RESOURCE_DICT)
                {
                    struct uwp_rd_bucket *parent_rd = (parent_el && parent_el->kind == UWP_KIND_RESOURCE_DICT)
                        ? uwp_rd_bucket_for_el( parent_el, TRUE )
                        : (active_rd ? active_rd : uwp_rd_bucket_for_el( parent_el, TRUE ));
                    struct uwp_rd_bucket *child_rd = uwp_rd_bucket_for_el( last_popped.el, TRUE );
                    uwp_rd_link_merged( parent_rd, child_rd );
                }
                else if ((parent_el || parent_raw) && last_popped.raw)
                {
                    BOOL handled_custom = FALSE;
                    void *app_outer = ctx->app_outer ? ctx->app_outer : uwp_get_app_outer();
                    if (parent_el && (pid == XBF_PROP_KEYBOARD_ACCELERATORS || (last_popped.el && (last_popped.el->kind == UWP_KIND_ACCELERATOR ||
                                                             wcsstr( last_popped.el->class_name, L"Accelerator" )))))
                    {
                        if (!parent_el->accelerators_coll) uwp_vec_create( parent_el, &parent_el->accelerators_coll );
                        uwp_vec_Append( parent_el->accelerators_coll, last_popped.raw );
                        if (last_popped.el)
                        {
                            last_popped.el->accel_target = parent_el;
                            uwp_accel_register( last_popped.el, parent_el,
                                                last_popped.el->accel_key, last_popped.el->accel_mods,
                                                last_popped.el->accel_enabled );
                        }
                        handled_custom = TRUE;
                    }
                    else if (parent_el && pid == XBF_PROP_GRID_ROW_DEFINITIONS) /* RowDefinitions */
                    {
                        if (!parent_el->row_defs_coll) uwp_vec_create( parent_el, &parent_el->row_defs_coll );
                        uwp_vec_Append( parent_el->row_defs_coll, last_popped.raw );
                        handled_custom = TRUE;
                    }
                    else if (parent_el && pid == XBF_PROP_GRID_COLUMN_DEFINITIONS) /* ColumnDefinitions */
                    {
                        if (!parent_el->col_defs_coll) uwp_vec_create( parent_el, &parent_el->col_defs_coll );
                        uwp_vec_Append( parent_el->col_defs_coll, last_popped.raw );
                        handled_custom = TRUE;
                    }
                    else if (parent_raw && app_outer &&
                             val_stack[obj_sp - 1].tid < ctx->type_cnt &&
                             ctx->types[val_stack[obj_sp - 1].tid].full_name[0] &&
                             wcsstr( ctx->types[val_stack[obj_sp - 1].tid].full_name, L"BreakpointCollection" ))
                    {
                        void *provider = NULL;
                        const WCHAR *coll_cls = ctx->types[val_stack[obj_sp - 1].tid].full_name;
                        if (SUCCEEDED(IInspectable_QueryInterface( (IInspectable *)app_outer, &IID_IXamlMetadataProvider_UWP_g, &provider )) && provider)
                        {
                            struct xaml_type *ct = (struct xaml_type *)uwp_provider_get_xaml_type_cached( (struct xaml_metadata_provider *)provider, coll_cls );
                            if (ct && ct->lpVtbl->AddToVector)
                            {
                                HRESULT ahr = ct->lpVtbl->AddToVector( ct, parent_raw, last_popped.raw );
                                handled_custom = SUCCEEDED(ahr);
                                TRACE( "AddToVector(%s coll=%p item=%p) -> hr %#lx\n",
                                       debugstr_w(coll_cls), parent_raw, last_popped.raw, ahr );
                            }
                            if (ct) IInspectable_Release( (IInspectable *)ct );
                            IInspectable_Release( (IInspectable *)provider );
                        }
                    }
                    else if (!(pid & 0x8000) && pid < ctx->prop_cnt &&
                             ctx->props[pid].owner_tid < ctx->type_cnt &&
                             ctx->props[pid].name[0] &&
                             wcscmp( ctx->props[pid].name, L"Children" ) &&
                             wcscmp( ctx->props[pid].name, L"Items" ) &&
                             wcscmp( ctx->props[pid].name, L"MergedDictionaries" ) &&
                             wcscmp( ctx->props[pid].name, L"RowDefinitions" ) &&
                             wcscmp( ctx->props[pid].name, L"ColumnDefinitions" ) &&
                             wcscmp( ctx->props[pid].name, L"KeyboardAccelerators" ))
                    {
                        const struct xbf_prop_info *pi = &ctx->props[pid];
                        const WCHAR *owner_cls = ctx->types[pi->owner_tid].full_name;
                        IInspectable *target = parent_raw ? parent_raw : (parent_el ? (parent_el->outer ? parent_el->outer : &parent_el->IInspectable_iface) : NULL);
                        if (owner_cls[0] && target)
                            handled_custom = uwp_add_custom_xaml_collection_item( app_outer, owner_cls, pi->name, target, last_popped.raw );
                    }
                    if (!handled_custom && parent_el && parent_el->kind == UWP_KIND_TEXT_BLOCK &&
                        last_popped.el && last_popped.el->text[0])
                    {
                        size_t cur = wcslen( parent_el->text );
                        if (cur + 1 < ARRAY_SIZE(parent_el->text))
                            lstrcpynW( parent_el->text + cur, last_popped.el->text, (int)(ARRAY_SIZE(parent_el->text) - cur) );
                        handled_custom = TRUE;
                    }
                    if (!handled_custom && parent_el &&
                        (pid == XBF_PROP_PANEL_CHILDREN || pid == XBF_PROP_ITEMS || parent_el->kind == UWP_KIND_GRID || parent_el->kind == UWP_KIND_STACK_PANEL))
                    {
                        if (!parent_el->children_coll) uwp_vec_create( parent_el, &parent_el->children_coll );
                        uwp_vec_Append( parent_el->children_coll, last_popped.raw );
                        if (last_popped.el) last_popped.el->parent = parent_el;
                    }
                }
                if (ctx->aot && node_idx_stack[obj_sp - 1] != (~0u) && last_popped_node_idx != (~0u))
                {
                    struct xbf_aot_op aop = {0};
                    aop.kind = XBF_AOT_OP_ADD_CHILD;
                    aop.node_idx = node_idx_stack[obj_sp - 1];
                    aop.child_idx = last_popped_node_idx;
                    aop.pid = pid;
                    xbf_aot_emit_op( ctx->aot, &aop );
                }
            }
            free( last_popped.setters );
            memset( &last_popped, 0, sizeof(last_popped) );
            last_popped_node_idx = (~0u);
            has_last_popped = FALSE;
        }
        else if (op == 0x07) /* SetObjectProperty <pid:u16> */
        {
            WORD pid = 0;
            if (end_p - p < 2) break;
            pid = *(const WORD *)(ctx->nodes + p);
            p += 2;
            TRACE( "xbf-trace: op=0x07 pid=%#x has_last=%d last_el=%p\n",
                   pid, has_last_popped, last_popped.el );
            if (has_last_popped)
            {
                struct uwp_element *parent_el = (obj_sp > 0) ? obj_stack[obj_sp - 1] : NULL;
                IInspectable *parent_raw = (obj_sp > 0) ? raw_stack[obj_sp - 1] : NULL;
                BOOL is_dict = (last_popped.el && last_popped.el->kind == UWP_KIND_RESOURCE_DICT);
                struct uwp_rd_bucket *child_rd = last_popped.el ? uwp_rd_bucket_for_el( last_popped.el, FALSE ) : NULL;

                if (is_dict || (child_rd && (pid == XBF_PROP_FRAMEWORKELEMENT_RESOURCES || pid == XBF_PROP_MERGED_DICTIONARIES || pid == XBF_PROP_THEME_DICTIONARIES)))
                {
                    struct uwp_rd_bucket *dest_b = (parent_el && parent_el->kind == UWP_KIND_RESOURCE_DICT)
                        ? uwp_rd_bucket_for_el( parent_el, TRUE )
                        : (active_rd ? active_rd : ctx->target_rd);
                    if (!child_rd && last_popped.el)
                        child_rd = uwp_rd_bucket_for_el( last_popped.el, TRUE );
                    if (dest_b && child_rd && dest_b != child_rd)
                    {
                        if (pid == XBF_PROP_THEME_DICTIONARIES) /* ThemeDictionaries */
                            uwp_rd_link_theme( dest_b, (last_popped.el && last_popped.el->name[0]) ? last_popped.el->name : L"Default", child_rd );
                        else
                            uwp_rd_link_merged( dest_b, child_rd );
                        TRACE( "xbf-res: linked child_rd %p into dest_b %p (parent_el %p pid %#x)\n",
                               child_rd, dest_b, parent_el, pid );
                    }
                }
                else if (parent_el || parent_raw)
                {
                    BOOL handled_custom_obj = FALSE;
                    if (last_popped.raw && !(pid & 0x8000) && pid < ctx->prop_cnt &&
                        ctx->props[pid].owner_tid < ctx->type_cnt &&
                        ctx->props[pid].name[0])
                    {
                        const struct xbf_prop_info *pi = &ctx->props[pid];
                        const WCHAR *owner_cls = ctx->types[pi->owner_tid].full_name;
                        IInspectable *target = parent_raw ? parent_raw : (parent_el ? (parent_el->outer ? parent_el->outer : &parent_el->IInspectable_iface) : NULL);
                        if (owner_cls[0] && target &&
                            wcscmp( pi->name, L"Style" ) && wcscmp( pi->name, L"Resources" ) &&
                            wcscmp( pi->name, L"MergedDictionaries" ) && wcscmp( pi->name, L"ThemeDictionaries" ) &&
                            wcscmp( pi->name, L"ContextFlyout" ) && wcscmp( pi->name, L"Background" ) &&
                            wcscmp( pi->name, L"Foreground" ) && wcscmp( pi->name, L"BorderBrush" ))
                        {
                            if (uwp_set_custom_xaml_member_obj( ctx->app_outer, owner_cls, pi->name, target, last_popped.raw ))
                            {
                                if (wcscmp( pi->name, L"Content" ) && wcscmp( pi->name, L"Child" ))
                                    handled_custom_obj = TRUE;
                            }
                        }
                    }
                    if (handled_custom_obj || !parent_el)
                    {
                        /* already applied via IXamlMember::SetValue */
                    }
                    else if (last_popped.kind == UWP_RD_BRUSH || last_popped.kind == UWP_RD_COLOR)
                    {
                        UINT32 argb = 0;
                        BOOL got = FALSE;
                        if (last_popped.has_argb)
                        {
                            argb = apply_opacity_to_argb( last_popped.argb, last_popped.opacity );
                            got = TRUE;
                        }
                        else if (last_popped.str_val[0] &&
                                 uwp_resolve_brush_or_color( active_rd ? active_rd : ctx->target_rd, parent_el,
                                                             last_popped.str_val, 0, &argb ))
                        {
                            argb = apply_opacity_to_argb( argb, last_popped.opacity );
                            got = TRUE;
                        }
                        else if (last_popped.el && last_popped.el->has_brush_argb)
                        {
                            argb = apply_opacity_to_argb( last_popped.el->brush_argb, last_popped.el->brush_opacity );
                            got = TRUE;
                        }
                        if (got)
                        {
                            if (uwp_is_bg_pid( pid )) uwp_el_set_bg_argb( parent_el, argb );
                            else if (uwp_is_fg_pid( pid )) uwp_el_set_fg_argb( parent_el, argb );
                            else if (uwp_is_border_pid( pid )) uwp_el_set_border_argb( parent_el, argb );
                        }
                    }
                    else if (last_popped.kind == UWP_RD_STYLE && pid == XBF_PROP_STYLE)
                    {
                        int si;
                        if (last_popped.based_on[0])
                            uwp_apply_style_by_name( ctx, parent_el, last_popped.based_on );
                        for (si = 0; si < last_popped.setter_cnt; si++)
                        {
                            const struct uwp_style_setter *st = &last_popped.setters[si];
                            xbf_apply_property( ctx, parent_el, NULL, st->pid, st->vt, st->num, st->str, st->vec4, st->is_res_ref );
                        }
                    }
                    else if (pid == 0x000c && last_popped.el && last_popped.el->text[0])
                    {
                        if (!parent_el->text[0])
                            lstrcpynW( parent_el->text, last_popped.el->text, ARRAY_SIZE(parent_el->text) );
                    }
                    else if (last_popped.raw &&
                             (pid == XBF_PROP_DATACONTEXT ||
                              (!(pid & 0x8000) && ctx && pid < ctx->prop_cnt && !wcscmp( ctx->props[pid].name, L"DataContext" ))))
                    {
                        uwp_fe_put_DataContext( &parent_el->IInspectable_iface, last_popped.raw );
                    }
                    else if (last_popped.raw &&
                             (pid == XBF_PROP_CONTEXT_FLYOUT ||
                              (ctx && pid < ctx->prop_cnt && !wcscmp( ctx->props[pid].name, L"ContextFlyout" ))))
                    {
                        if (parent_el->context_flyout) IInspectable_Release( parent_el->context_flyout );
                        parent_el->context_flyout = last_popped.raw;
                        IInspectable_AddRef( last_popped.raw );
                        if (last_popped.el) last_popped.el->parent = parent_el;
                    }
                    else if (last_popped.raw &&
                             last_popped.kind == UWP_RD_OBJ &&
                             ctx && pid < ctx->prop_cnt &&
                             !wcscmp( ctx->props[pid].name, L"Pane" ))
                    {
                        if (parent_el->pane) IInspectable_Release( parent_el->pane );
                        parent_el->pane = last_popped.raw;
                        IInspectable_AddRef( last_popped.raw );
                        if (last_popped.el) last_popped.el->parent = parent_el;
                    }
                    else if (last_popped.raw &&
                             last_popped.kind == UWP_RD_OBJ &&
                             (!last_popped.el ||
                              (last_popped.el->kind != UWP_KIND_STYLE &&
                               last_popped.el->kind != UWP_KIND_BRUSH &&
                               last_popped.el->kind != UWP_KIND_COLOR)) &&
                             (pid == XBF_PROP_CONTENT || pid == XBF_PROP_USERCONTROL_CONTENT || pid == XBF_PROP_BORDER_CHILD || pid == XBF_PROP_RENDER_TRANSFORM_ORIGIN || pid == XBF_PROP_POPUP_CHILD ||
                              (!parent_el->content && pid != XBF_PROP_UIELEMENT_RENDER_TRANSFORM && pid != XBF_PROP_FRAMEWORKELEMENT_RESOURCES && pid != XBF_PROP_AUTOMATION_NAME && pid != 0x000c)))
                    {
                        if (parent_el->content) IInspectable_Release( parent_el->content );
                        parent_el->content = last_popped.raw;
                        IInspectable_AddRef( last_popped.raw );
                        if (last_popped.el) last_popped.el->parent = parent_el;
                    }
                }
                if (ctx->aot && (obj_sp == 0 || node_idx_stack[obj_sp - 1] != (~0u)))
                {
                    struct xbf_aot_op aop = {0};
                    aop.kind = XBF_AOT_OP_SET_OBJ_PROP;
                    aop.sv_kind = (UINT8)last_popped.kind;
                    aop.has_argb = last_popped.has_argb ? 1 : 0;
                    aop.pid = pid;
                    aop.node_idx = (obj_sp > 0) ? node_idx_stack[obj_sp - 1] : (~0u);
                    aop.child_idx = last_popped_node_idx;
                    aop.argb = last_popped.argb;
                    aop.num = last_popped.opacity;
                    if (last_popped.kind == UWP_RD_STYLE)
                    {
                        aop.str_off = xbf_aot_intern_str( ctx->aot, last_popped.based_on );
                        if (last_popped.setter_cnt > 0 && last_popped.setters)
                        {
                            aop.setter_start = xbf_aot_append_setters( ctx->aot, last_popped.setters, last_popped.setter_cnt );
                            aop.setter_cnt = (UINT32)last_popped.setter_cnt;
                        }
                    }
                    else
                    {
                        aop.str_off = xbf_aot_intern_str( ctx->aot, last_popped.str_val );
                    }
                    xbf_aot_emit_op( ctx->aot, &aop );
                }
            }
            free( last_popped.setters );
            memset( &last_popped, 0, sizeof(last_popped) );
            last_popped_node_idx = (~0u);
            has_last_popped = FALSE;
        }
        else if (op == 0x20) /* ProvideValue and SetProperty (u16 pid) */
        {
            WORD pid = 0;
            if (end_p - p < 2) break;
            pid = *(const WORD *)(ctx->nodes + p);
            p += 2;
            if (has_last_popped && last_popped.el && obj_sp > 0 && obj_stack[obj_sp - 1] && last_popped.el->text[0])
            {
                struct uwp_element *target_el = obj_stack[obj_sp - 1];
                WCHAR resolved[256] = {0};
                if (uwp_pri_lookup_string( last_popped.el->text, resolved, ARRAY_SIZE(resolved) ) && resolved[0])
                {
                    const WCHAR *pname = (ctx && pid < ctx->prop_cnt) ? ctx->props[pid].name : L"";
                    BOOL is_chord = (!wcscmp( pname, L"VirtualKeyControlChord" ) ||
                                     !wcscmp( pname, L"VirtualKeyAltChord" ) ||
                                     !wcscmp( pname, L"VirtualKeyShiftChord" ) ||
                                     !wcscmp( pname, L"VirtualKeyControlShiftChord" ));
                    BOOL is_vk = (!wcscmp( pname, L"VirtualKey" ) || is_chord);
                    if (is_chord)
                    {
                        UINT vk = uwp_parse_vk_string( resolved );
                        UINT mods = !wcscmp( pname, L"VirtualKeyControlChord" ) ? 1 :
                                    (!wcscmp( pname, L"VirtualKeyAltChord" ) ? 2 :
                                     (!wcscmp( pname, L"VirtualKeyShiftChord" ) ? 4 : 5));
                        if (vk)
                            uwp_accel_register( NULL, target_el, vk, mods, TRUE );
                    }
                    if (pid == XBF_PROP_CONTENT || pid == XBF_PROP_UIELEMENT_XY_FOCUS_KEYBOARD_NAV || pid == XBF_PROP_TEXTBLOCK_TEXT || (!is_vk && !target_el->text[0]))
                        lstrcpynW( target_el->text, resolved, ARRAY_SIZE(target_el->text) );
                }
                if (ctx->aot && node_idx_stack[obj_sp - 1] != (~0u) && last_popped_node_idx != (~0u))
                {
                    struct xbf_aot_op aop = {0};
                    aop.kind = XBF_AOT_OP_PROVIDE_VALUE;
                    aop.pid = pid;
                    aop.node_idx = node_idx_stack[obj_sp - 1];
                    aop.child_idx = last_popped_node_idx;
                    xbf_aot_emit_op( ctx->aot, &aop );
                }
            }
            free( last_popped.setters );
            memset( &last_popped, 0, sizeof(last_popped) );
            last_popped_node_idx = (~0u);
            has_last_popped = FALSE;
        }
        else if (op == 0x09)
        {
            free( last_popped.setters );
            memset( &last_popped, 0, sizeof(last_popped) );
            last_popped_node_idx = (~0u);
            has_last_popped = FALSE;
        }
        else if (op == 0x0f) /* DeferSubstream */
        {
            DWORD def_sub = 0, def_sub_sz = 0;
            BYTE def_kind;
            if (!xbf_read_uleb( ctx->nodes, &p, end_p, &def_sub ) || end_p - p < 3) break;
            if (!xbf_get_substream_bounds( ctx, def_sub, NULL, NULL, &def_sub_sz )) break;
            def_kind = ctx->nodes[p + 2];
            p += 3;

            if (def_kind == 0x09) /* DeferredElement: <name_sid:u16> <00> <01> */
            {
                if (end_p - p < 4) break;
                {
                    WORD nsid = *(const WORD *)(ctx->nodes + p);
                    const WCHAR *dname = (nsid < ctx->str_cnt && ctx->strs[nsid]) ? ctx->strs[nsid] : L"";
                    p += 4;
                    if (obj_sp > 0 && obj_stack[obj_sp - 1])
                        lstrcpynW( obj_stack[obj_sp - 1]->name, dname, ARRAY_SIZE(obj_stack[obj_sp - 1]->name) );
                    if (ctx->root_el &&
                        xaml_vec_grow( (void **)&ctx->root_el->defer_entries, &ctx->root_el->defer_cap,
                                       ctx->root_el->defer_cnt + 1, sizeof(*ctx->root_el->defer_entries), 16 ))
                    {
                        struct uwp_defer_entry *de = &ctx->root_el->defer_entries[ctx->root_el->defer_cnt++];
                        lstrcpynW( de->name, dname, ARRAY_SIZE(de->name) );
                        de->sub_idx = def_sub;
                        de->parent_panel = (obj_sp >= 2) ? obj_stack[obj_sp - 2] : ctx->root_el;
                        de->xbf_data = ctx->xbf;
                        de->xbf_size = ctx->xbf_sz;
                        de->root_component = ctx->root_comp;
                        de->realized = FALSE;
                        if (ctx->aot)
                        {
                            UINT32 ppanel_idx = (obj_sp >= 2 && node_idx_stack[obj_sp - 2] != (~0u))
                                ? node_idx_stack[obj_sp - 2] : 0u;
                            xbf_aot_add_defer( ctx->aot, dname, def_sub, ppanel_idx );
                        }
                    }
                }
            }
            else if (def_kind == 0x05) /* VisualStateGroupCollection */
            {
                if (!xbf_read_vsm_defer_header( ctx->nodes, &p, end_p, def_sub_sz, &prop_sp ))
                    break;
                if (!ctx->rd_only)
                {
                    BOOL prev_vsm = ctx->vsm_collect;
                    ctx->vsm_collect = (sub_idx == 0);
                    xbf_exec_substream_rd( ctx, (int)def_sub, ctx->root_el, NULL, NULL, active_rd );
                    ctx->vsm_collect = prev_vsm;
                }
                else if (ctx->sub_done && def_sub < ctx->sub_cnt)
                    ctx->sub_done[def_sub] = TRUE;
            }
            else if (def_kind == 0x08) /* Style Setters table */
            {
                struct xbf_stack_val *style_sv = (obj_sp > 0) ? &val_stack[obj_sp - 1] : NULL;
                if (ctx->sub_done && def_sub < ctx->sub_cnt)
                    ctx->sub_done[def_sub] = TRUE;
                if (!xbf_read_style_defer_table( ctx, &p, end_p, def_sub, def_sub_sz, style_sv, &prop_sp ))
                    break;
            }
            else if (def_kind == 0x0a) /* ResourceDictionary table */
            {
                struct xbf_rd_table tbl = {0};
                struct uwp_rd_bucket *sub_rd = NULL;
                if (!xbf_read_rd_defer_header( ctx->nodes, &p, end_p, def_sub_sz, &tbl, &prop_sp ))
                {
                    free( tbl.items );
                    break;
                }
                if (obj_sp > 0 && obj_stack[obj_sp - 1])
                    sub_rd = uwp_rd_bucket_for_el( obj_stack[obj_sp - 1], TRUE );
                else
                    sub_rd = active_rd ? active_rd : ctx->target_rd;
                {
                    BOOL prev_rd_only = ctx->rd_only;
                    ctx->rd_only = TRUE;
                    xbf_exec_substream_rd( ctx, (int)def_sub, NULL, NULL, &tbl, sub_rd );
                    ctx->rd_only = prev_rd_only;
                }
                free( tbl.items );
            }
            else
            {
                if (obj_sp > 0 && val_stack[obj_sp - 1].tid == XBF_TYPE_DATA_TEMPLATE)
                {
                    val_stack[obj_sp - 1].template_xbf_data = ctx->xbf;
                    val_stack[obj_sp - 1].template_xbf_size = ctx->xbf_sz;
                    val_stack[obj_sp - 1].template_sub_idx = def_sub;
                    val_stack[obj_sp - 1].template_owner_comp = ctx->root_comp;
                    if (obj_stack[obj_sp - 1])
                    {
                        obj_stack[obj_sp - 1]->template_xbf_data = ctx->xbf;
                        obj_stack[obj_sp - 1]->template_xbf_size = ctx->xbf_sz;
                        obj_stack[obj_sp - 1]->template_sub_idx = def_sub;
                        uwp_set_template_owner( &obj_stack[obj_sp - 1]->template_owner_comp, ctx->root_comp );
                    }
                    if (ctx->aot && node_idx_stack[obj_sp - 1] < ctx->aot->node_cnt)
                        ctx->aot->nodes[node_idx_stack[obj_sp - 1]].template_sub_idx = def_sub;
                    TRACE( "XBF DataTemplate captured deferred substream %u (kind=%#x)\n",
                           (unsigned)def_sub, def_kind );
                }
                if (rd_tbl)
                {
                    DWORD next_off = 0xffffffffu;
                    int k;
                    for (k = 0; k < rd_tbl->count; k++)
                    {
                        if (rd_tbl->items[k].sub_off > cur_rel_off && rd_tbl->items[k].sub_off < next_off)
                            next_off = rd_tbl->items[k].sub_off;
                    }
                    if (next_off != 0xffffffffu && next_off < sub_sz)
                    {
                        p = sub_start + next_off;
                        while (obj_sp > base_sp) { obj_sp--; free( val_stack[obj_sp].setters ); memset( &val_stack[obj_sp], 0, sizeof(val_stack[obj_sp]) ); }
                        prop_sp = 0;
                        free( last_popped.setters );
                        memset( &last_popped, 0, sizeof(last_popped) );
                        has_last_popped = FALSE;
                        continue;
                    }
                }
                TRACE( "XBF unknown DeferSubstream kind=%#x at %lu/%lu\n", def_kind, (unsigned long)p, (unsigned long)end_p );
                break;
            }
        }
        else
        {
            TRACE( "XBF unknown op=%#x at %lu/%lu (sub %d) top_tid=%#x next=[%02x %02x %02x %02x]\n",
                   op, (unsigned long)cur_rel_off, (unsigned long)sub_sz, sub_idx,
                   obj_sp > 0 ? val_stack[obj_sp - 1].tid : 0,
                   p < end_p ? ctx->nodes[p] : 0, p + 1 < end_p ? ctx->nodes[p + 1] : 0,
                   p + 2 < end_p ? ctx->nodes[p + 2] : 0, p + 3 < end_p ? ctx->nodes[p + 3] : 0 );
            if (rd_tbl)
            {
                DWORD next_off = 0xffffffffu;
                int k;
                for (k = 0; k < rd_tbl->count; k++)
                {
                    if (rd_tbl->items[k].sub_off > cur_rel_off && rd_tbl->items[k].sub_off < next_off)
                        next_off = rd_tbl->items[k].sub_off;
                }
                if (next_off != 0xffffffffu && next_off < sub_sz)
                {
                    p = sub_start + next_off;
                    while (obj_sp > base_sp) { obj_sp--; free( val_stack[obj_sp].setters ); memset( &val_stack[obj_sp], 0, sizeof(val_stack[obj_sp]) ); }
                    prop_sp = 0;
                    free( last_popped.setters );
                    memset( &last_popped, 0, sizeof(last_popped) );
                    has_last_popped = FALSE;
                    continue;
                }
            }
            TRACE( "XBF unrecognized op=%#x at %lu/%lu (sub %d), scanning forward\n",
                   op, (unsigned long)cur_rel_off, (unsigned long)end_p, sub_idx );
            while (p < end_p)
            {
                if (ctx->nodes[p] == 0x21 || ctx->nodes[p] == 0x07 || ctx->nodes[p] == 0x08 ||
                    ctx->nodes[p] == 0x14 || ctx->nodes[p] == 0x15)
                    break;
                p++;
            }
            if (p >= end_p) break;
        }
    }

    while (obj_sp > base_sp)
    {
        obj_sp--;
        free( val_stack[obj_sp].setters );
    }
    free( last_popped.setters );
    if (dt_binding) IUnknown_Release( (IUnknown *)dt_binding );
}

void uwp_parse_xbf_internal( const BYTE *xbf, DWORD xbf_sz, IInspectable *component,
                                    void *connector, BOOL is_uwp_conn, void *app_outer,
                                    int only_sub_idx, struct uwp_element *attach_root,
                                    struct uwp_element **out_created,
                                    const char *base_xbf_path, struct uwp_rd_bucket *target_rd,
                                    BOOL rd_only )
{
    struct xbf_ctx ctx = {0};
    struct xbf_aot_builder aot_b = {0};
    struct uwp_element *root_el_pre;
    LARGE_INTEGER t0 = {0}, t1 = {0}, freq = {0};
    int initial_vsm_cnt = 0;
    BOOL aot_compiled_ok = FALSE;
    const BYTE *meta_base, *meta_end;
    UINT64 offs[6];
    DWORD i;

    if (!xbf || xbf_sz < 0x50 || memcmp( xbf, "XBF\0", 4 ) != 0) return;

    root_el_pre = attach_root ? attach_root : uwp_elem_from_any( component );
    if (!rd_only)
    {
        UINT64 aot_hash = xbf_aot_compute_hash( xbf, xbf_sz,
            (root_el_pre && root_el_pre->class_name[0]) ? root_el_pre->class_name : L"", only_sub_idx );
        BOOL from_disk = FALSE;
        struct xbf_aot_cache_slot *slot;

        QueryPerformanceFrequency( &freq );
        QueryPerformanceCounter( &t0 );

        slot = xbf_aot_cache_acquire( aot_hash, xbf_sz, only_sub_idx, &from_disk );
        if (slot)
        {
            const struct xbf_aot_header *tpl = slot->tpl;
            double us;
            xbf_aot_instantiate( tpl, xbf, xbf_sz, component, connector, is_uwp_conn, app_outer,
                                 root_el_pre, out_created, target_rd,
                                 (out_created != NULL && attach_root == NULL) );
            QueryPerformanceCounter( &t1 );
            us = freq.QuadPart ? ((double)(t1.QuadPart - t0.QuadPart) * 1000000.0 / (double)freq.QuadPart) : 0.0;
            TRACE( "XBF AOT %s hit (%s, sub=%d, %lu xbf -> %lu aot bytes, %u nodes, %u ops) in %.1f us\n",
                   from_disk ? "DISK" : "MEM",
                   debugstr_w(root_el_pre ? root_el_pre->class_name : L""),
                   only_sub_idx, (unsigned long)xbf_sz, (unsigned long)tpl->total_bytes,
                   tpl->node_cnt, tpl->op_cnt, us );
            InterlockedDecrement( &slot->ref );
            return;
        }

        aot_b.hash = aot_hash;
        aot_b.xbf_sz = xbf_sz;
        aot_b.only_sub_idx = only_sub_idx;
        if (root_el_pre)
            xbf_aot_add_node( &aot_b, root_el_pre->class_name, L"", 0, XBF_AOT_NODE_ROOT_REUSE, FALSE, 0 );
        initial_vsm_cnt = root_el_pre ? root_el_pre->vsm_group_cnt : 0;
        ctx.aot = &aot_b;
    }

    ctx.xbf = xbf;
    ctx.xbf_sz = xbf_sz;
    ctx.meta_sz = *(const DWORD *)(xbf + 4);
    ctx.node_sz = *(const DWORD *)(xbf + 8);
    if (ctx.meta_sz > xbf_sz - 12 || ctx.node_sz > xbf_sz - 12 - ctx.meta_sz) return;

    meta_base = xbf + 12;
    meta_end = meta_base + ctx.meta_sz;
    memcpy( offs, xbf + 20, sizeof(offs) );
    ctx.app_outer = app_outer;
    ctx.connector = connector;
    ctx.is_uwp_conn = is_uwp_conn;
    ctx.root_comp = component;
    ctx.target_rd = target_rd;
    ctx.rd_only = rd_only;
    if (base_xbf_path && base_xbf_path[0])
    {
        const char *slash = strrchr( base_xbf_path, '/' );
        if (slash)
        {
            size_t dlen = min( (size_t)(slash - base_xbf_path + 1), sizeof(ctx.base_xbf_dir) - 1 );
            memcpy( ctx.base_xbf_dir, base_xbf_path, dlen );
            ctx.base_xbf_dir[dlen] = 0;
        }
    }
    ctx.root_el = attach_root ? attach_root : uwp_elem_from_any( component );
    if (ctx.root_el) ctx.root_el->xbf_parsed = TRUE;

    /* 1. String Table */
    {
        const BYTE *p;
        if (offs[0] > ctx.meta_sz || ctx.meta_sz - (DWORD)offs[0] < 4) return;
        p = meta_base + (size_t)offs[0];
        ctx.str_cnt = *(const DWORD *)p;
        p += 4;
        if (ctx.str_cnt > 16384 || ctx.str_cnt > (DWORD)(meta_end - p) / 4) return;
        ctx.strs = calloc( ctx.str_cnt, sizeof(WCHAR *) );
        if (!ctx.strs && ctx.str_cnt > 0) return;
        for (i = 0; i < ctx.str_cnt; i++)
        {
            DWORD len;
            if ((size_t)(meta_end - p) < 4) goto cleanup;
            len = *(const DWORD *)p;
            p += 4;
            if (len > (DWORD)(meta_end - p) / 2) goto cleanup;
            ctx.strs[i] = calloc( len + 1, sizeof(WCHAR) );
            if (ctx.strs[i]) memcpy( ctx.strs[i], p, len * 2 );
            p += len * 2;
            if ((size_t)(meta_end - p) >= 2) p += 2;
        }
    }

    /* 2. XmlNamespace Table -> Type Table -> Property Table */
    {
        const BYTE *xp = (offs[5] <= ctx.meta_sz) ? meta_base + (size_t)offs[5] : meta_end;
        DWORD xcnt = ((size_t)(meta_end - xp) >= 4) ? *(const DWORD *)xp : 0;
        const DWORD *x_sids = (const DWORD *)(xp + 4);
        const BYTE *tp = (offs[3] <= ctx.meta_sz) ? meta_base + (size_t)offs[3] : meta_end;

        if (xcnt > 0 && xcnt < 4096 && (size_t)(meta_end - xp) >= 4 && xcnt <= (DWORD)(meta_end - xp - 4) / 4)
        {
            ctx.xmlns_cnt = xcnt;
            ctx.xmlns = calloc( xcnt, sizeof(WCHAR *) );
            for (i = 0; i < xcnt && ctx.xmlns; i++)
            {
                DWORD xsid = x_sids[i];
                const WCHAR *ns = (xsid < ctx.str_cnt && ctx.strs[xsid]) ? ctx.strs[xsid] : L"";
                ctx.xmlns[i] = calloc( wcslen(ns) + 1, sizeof(WCHAR) );
                if (ctx.xmlns[i]) lstrcpyW( ctx.xmlns[i], ns );
            }
        }
        else
        {
            xcnt = 0;
        }

        if ((size_t)(meta_end - tp) >= 4)
        {
            ctx.type_cnt = *(const DWORD *)tp;
            if (ctx.type_cnt > 0 && ctx.type_cnt < 4096 && ctx.type_cnt <= (DWORD)(meta_end - tp - 4) / 12)
            {
                ctx.types = calloc( ctx.type_cnt, sizeof(*ctx.types) );
                for (i = 0; i < ctx.type_cnt && ctx.types; i++)
                {
                    DWORD xid = *(const DWORD *)(tp + 4 + i * 12 + 4);
                    DWORD sid = *(const DWORD *)(tp + 4 + i * 12 + 8);
                    const WCHAR *sname = (sid < ctx.str_cnt && ctx.strs[sid]) ? ctx.strs[sid] : L"";
                    const WCHAR *ns = (xid < ctx.xmlns_cnt && ctx.xmlns && ctx.xmlns[xid])
                        ? ctx.xmlns[xid]
                        : ((xid < xcnt && x_sids[xid] < ctx.str_cnt) ? ctx.strs[x_sids[xid]] : L"");
                    lstrcpynW( ctx.types[i].xmlns, ns ? ns : L"", ARRAY_SIZE(ctx.types[i].xmlns) );
                    lstrcpynW( ctx.types[i].short_name, sname, 64 );
                    if (ns && !wcsncmp( ns, L"using:", 6 ))
                        swprintf( ctx.types[i].full_name, 128, L"%s.%s", ns + 6, sname );
                    else
                        swprintf( ctx.types[i].full_name, 128, L"Windows.UI.Xaml.Controls.%s", sname );
                }
            }
            else
            {
                ctx.type_cnt = 0;
            }
        }

        {
            const BYTE *pp = (offs[4] <= ctx.meta_sz) ? meta_base + (size_t)offs[4] : meta_end;
            if ((size_t)(meta_end - pp) >= 4)
            {
                ctx.prop_cnt = *(const DWORD *)pp;
                if (ctx.prop_cnt > 0 && ctx.prop_cnt < 4096 && ctx.prop_cnt <= (DWORD)(meta_end - pp - 4) / 12)
                {
                    ctx.props = calloc( ctx.prop_cnt, sizeof(*ctx.props) );
                    for (i = 0; i < ctx.prop_cnt && ctx.props; i++)
                    {
                        DWORD tid = *(const DWORD *)(pp + 4 + i * 12 + 4);
                        DWORD sid = *(const DWORD *)(pp + 4 + i * 12 + 8);
                        ctx.props[i].owner_tid = (WORD)tid;
                        if (sid < ctx.str_cnt && ctx.strs[sid])
                            lstrcpynW( ctx.props[i].name, ctx.strs[sid], 64 );
                    }
                }
                else
                {
                    ctx.prop_cnt = 0;
                }
            }
        }
    }

    /* 3. Record XBF-referenced resources and using: controls from metadata tables */
    if (!rd_only && ctx.root_el && only_sub_idx < 0)
    {
        const BYTE *rx = NULL;
        DWORD rsz = 0;
        for (i = 0; i < ctx.type_cnt; i++)
        {
            if (!wcsncmp( ctx.types[i].xmlns, L"using:", 6 ) && ctx.types[i].short_name[0] &&
                uwp_pri_find_xbf( ctx.types[i].short_name, &rx, &rsz ))
            {
                uwp_record_xbf_ref( ctx.root_el, ctx.types[i].full_name, ctx.types[i].short_name, FALSE );
                xbf_aot_add_ref( ctx.aot, ctx.types[i].full_name, ctx.types[i].short_name, FALSE );
            }
        }
        for (i = 0; i < ctx.str_cnt; i++)
        {
            const WCHAR *s = ctx.strs[i];
            size_t slen = s ? wcslen( s ) : 0;
            if (slen >= 5 && (!wcsicmp( s + slen - 5, L".xaml" ) || !wcsicmp( s + slen - 4, L".xbf" )) &&
                uwp_pri_find_xbf( s, &rx, &rsz ))
            {
                uwp_record_xbf_ref( ctx.root_el, s, s, TRUE );
                xbf_aot_add_ref( ctx.aot, s, s, TRUE );
                if (wcsstr( s, L"Resource" ) || wcsstr( s, L"Theme" ) || wcsstr( s, L"Generic" ) ||
                    wcsstr( s, L"Dictionary" ) || wcsstr( s, L"Style" ))
                {
                    uwp_load_res_xbf_into_bucket( ctx.base_xbf_dir, s,
                        target_rd ? target_rd : uwp_rd_bucket_for_el( ctx.root_el, TRUE ) );
                    xbf_aot_add_res_uri( ctx.aot, s );
                }
            }
        }
    }
    else if (rd_only && target_rd)
    {
        /* Also follow any explicit ResourceDictionary.xaml / Themes XAML strings referenced in this RD */
        for (i = 0; i < ctx.str_cnt; i++)
        {
            const WCHAR *s = ctx.strs[i];
            size_t slen = s ? wcslen( s ) : 0;
            if (slen >= 5 && (!wcsicmp( s + slen - 5, L".xaml" ) || !wcsicmp( s + slen - 4, L".xbf" )) &&
                (wcsstr( s, L"Resource" ) || wcsstr( s, L"Themes/" ) || wcsstr( s, L"Generic" ) ||
                 wcsstr( s, L"Dark" ) || wcsstr( s, L"Dictionary" )))
            {
                struct uwp_rd_bucket *child = get_rd_bucket( (void *)(ULONG_PTR)(0x70000000u + (unsigned int)i + (unsigned int)g_loaded_res_xbf_cnt), TRUE );
                if (child)
                {
                    uwp_load_res_xbf_into_bucket( ctx.base_xbf_dir, s, child );
                    if (wcsstr( s, L"Dark" )) uwp_rd_link_theme( target_rd, L"Dark", child );
                    else if (!wcsstr( s, L"Light" ) && !wcsstr( s, L"HighContrast" ))
                        uwp_rd_link_merged( target_rd, child );
                }
            }
        }
    }

    /* 4. Node Substreams */
    ctx.nodes = xbf + 12 + ctx.meta_sz;
    if (ctx.node_sz >= 12)
    {
        ctx.sub_cnt = *(const DWORD *)ctx.nodes;
        if (ctx.sub_cnt > 0 && ctx.sub_cnt < 2048 && ctx.sub_cnt <= (ctx.node_sz - 4) / 8)
        {
            BOOL subs_valid = TRUE;
            ctx.sub_base = 4 + ctx.sub_cnt * 8;
            for (i = 0; i < ctx.sub_cnt; i++)
            {
                if (!xbf_get_substream_bounds( &ctx, i, NULL, NULL, NULL ))
                {
                    subs_valid = FALSE;
                    break;
                }
            }
            if (subs_valid)
            {
                ctx.sub_done = calloc( ctx.sub_cnt, sizeof(BOOL) );
                if (only_sub_idx >= 0)
                {
                    xbf_exec_substream_rd( &ctx, only_sub_idx, NULL, out_created, NULL, target_rd );
                }
                else if (rd_only)
                {
                    xbf_exec_substream_rd( &ctx, 0, NULL, NULL, NULL, target_rd );
                }
                else
                {
                    xbf_exec_substream_rd( &ctx, 0, ctx.root_el, out_created, NULL, target_rd );
                    /* Also execute substream 1..N for VisualStateGroups / Styles that hold connection IDs */
                    for (i = 1; i < ctx.sub_cnt; i++)
                    {
                        BOOL is_lazy = FALSE;
                        int d;
                        if (ctx.root_el)
                        {
                            for (d = 0; d < ctx.root_el->defer_cnt; d++)
                                if (ctx.root_el->defer_entries[d].sub_idx == (int)i) { is_lazy = TRUE; break; }
                        }
                        if (!is_lazy)
                            xbf_exec_substream_rd( &ctx, (int)i, NULL, NULL, NULL, target_rd );
                    }
                }
                free( ctx.sub_done );
                aot_compiled_ok = TRUE;
            }
        }
    }

    if (ctx.aot && aot_compiled_ok && !ctx.aot->aborted)
    {
        struct xbf_aot_header *tpl;
        xbf_aot_snapshot_vsm( ctx.aot, ctx.root_el, initial_vsm_cnt );
        tpl = xbf_aot_finalize( ctx.aot, &ctx );
        if (tpl)
        {
            double us;
            xbf_aot_save_to_disk( tpl );
            QueryPerformanceCounter( &t1 );
            us = freq.QuadPart ? ((double)(t1.QuadPart - t0.QuadPart) * 1000000.0 / (double)freq.QuadPart) : 0.0;
            TRACE( "XBF AOT COMPILE (%s, sub=%d, %lu xbf -> %lu aot bytes, %u nodes, %u ops) in %.1f us\n",
                   debugstr_w(ctx.root_el ? ctx.root_el->class_name : L""),
                   only_sub_idx, (unsigned long)xbf_sz, (unsigned long)tpl->total_bytes,
                   tpl->node_cnt, tpl->op_cnt, us );
            xbf_aot_cache_insert( tpl );
        }
    }

cleanup:
    xbf_aot_builder_free( &aot_b );
    if (ctx.strs)
    {
        for (i = 0; i < ctx.str_cnt; i++) free( ctx.strs[i] );
        free( ctx.strs );
    }
    if (ctx.xmlns)
    {
        for (i = 0; i < ctx.xmlns_cnt; i++) free( ctx.xmlns[i] );
        free( ctx.xmlns );
    }
    free( ctx.types );
    free( ctx.props );
}

void uwp_parse_xbf_into_rd( const BYTE *xbf, DWORD xbf_sz, const char *base_xbf_path,
                                   struct uwp_rd_bucket *target_rd )
{
    uwp_parse_xbf_internal( xbf, xbf_sz, NULL, NULL, FALSE, NULL, -1, NULL, NULL,
                            base_xbf_path, target_rd, TRUE );
}

void uwp_parse_and_build_xbf( const BYTE *xbf, DWORD xbf_sz, IInspectable *component,
                                     void *connector, BOOL is_uwp_conn, void *app_outer,
                                     int only_sub_idx, struct uwp_element *attach_root,
                                     struct uwp_element **out_created )
{
    uwp_ensure_app_resources_loaded();
    uwp_parse_xbf_internal( xbf, xbf_sz, component, connector, is_uwp_conn, app_outer,
                            only_sub_idx, attach_root, out_created, NULL, NULL, FALSE );
}

void uwp_realize_deferred_entry( struct uwp_defer_entry *de )
{
    void *conn = NULL;
    struct uwp_element *created = NULL;
    if (!de || de->realized || !de->xbf_data || !de->root_component) return;
    de->realized = TRUE;

    TRACE( "Realizing deferred substream %d for '%s'\n", de->sub_idx, debugstr_w(de->name) );
    IInspectable_QueryInterface( de->root_component, &IID_IComponentConnector_UWP_g, &conn );
    uwp_parse_and_build_xbf( de->xbf_data, de->xbf_size, de->root_component,
                             conn, TRUE, uwp_get_app_outer(),
                             de->sub_idx, uwp_elem_from_any( de->root_component ), &created );
    if (conn) IUnknown_Release( (IUnknown *)conn );

    if (created)
    {
        struct uwp_element *root_el = uwp_elem_from_any( de->root_component );
        if (root_el && !created->owner_component)
            created->owner_component = root_el;
        if (!created->name[0] && de->name[0])
        {
            lstrcpynW( created->name, de->name, ARRAY_SIZE(created->name) );
            if (root_el &&
                xaml_vec_grow( (void **)&root_el->named_elems, &root_el->named_cap,
                               root_el->named_cnt + 1, sizeof(*root_el->named_elems), 32 ))
            {
                root_el->named_elems[root_el->named_cnt++] = created;
            }
        }
    }
    if (created && de->parent_panel)
    {
        IInspectable *ch_iface = created->outer ? created->outer : &created->IInspectable_iface;
        created->parent = de->parent_panel;
        if (!de->parent_panel->children_coll)
            uwp_vec_create( de->parent_panel, &de->parent_panel->children_coll );
        uwp_vec_Append( de->parent_panel->children_coll, ch_iface );
        if (de->parent_panel->data_context && !created->data_context)
            uwp_propagate_data_context( created, de->parent_panel->data_context );
        uwp_vsm_invalidate_eval_chain( created );
        {
            struct uwp_element *root_el = uwp_elem_from_any( de->root_component );
            if (root_el && root_el->loaded_fired && root_el->loading_cnt > 0)
            {
                IInspectable *sender = root_el->outer ? root_el->outer : &root_el->IInspectable_iface;
                int i;
                for (i = 0; i < root_el->loading_cnt; i++)
                {
                    if (root_el->loading_handlers[i])
                    {
                        struct xaml_event_handler *h = (struct xaml_event_handler *)root_el->loading_handlers[i];
                        TRACE( "Re-firing Loading handler[%d] on %s (%s) after deferred realize\n",
                               i, debugstr_w(root_el->class_name), debugstr_w(root_el->name) );
                        __TRY
                        {
                            h->lpVtbl->Invoke( h, sender, NULL );
                        }
                        __EXCEPT_ALL
                        {
                        }
                        __ENDTRY;
                    }
                }
            }
        }
        uwp_invalidate_window();
    }
}

IInspectable *uwp_instantiate_xbf_substream( const BYTE *xbf_data, DWORD xbf_size, DWORD sub_idx, IInspectable *owner_comp )
{
    struct uwp_element *created = NULL;
    void *conn = NULL;
    if (!xbf_data || !xbf_size) return NULL;
    if (owner_comp)
        IInspectable_QueryInterface( owner_comp, &IID_IComponentConnector_UWP_g, &conn );
    uwp_parse_and_build_xbf( xbf_data, xbf_size, owner_comp, conn, TRUE, uwp_get_app_outer(),
                             (int)sub_idx, NULL, &created );
    if (conn) IUnknown_Release( (IUnknown *)conn );
    if (!created) return NULL;
    {
        IInspectable *ret = created->outer ? created->outer : &created->IInspectable_iface;
        IInspectable_AddRef( ret );
        return ret;
    }
}

BOOL uwp_try_load_xbf_component( void *component, void *resourceLocator, void *app_outer )
{
    static int depth = 0;
    void *conn = NULL, *conn2 = NULL, *binding = NULL;
    void *uri_rt = NULL;
    WCHAR uri_str[MAX_PATH] = {0};
    const BYTE *xbf = NULL;
    DWORD xbf_sz = 0;

    if (!component || !resourceLocator) return FALSE;
    if (FAILED(IInspectable_QueryInterface( (IInspectable *)component, &IID_IComponentConnector_UWP_g, &conn )) || !conn)
        return FALSE;

    if (SUCCEEDED(IInspectable_QueryInterface( (IInspectable *)resourceLocator, &IID_IUriRuntimeClass_g, &uri_rt )) && uri_rt)
    {
        HSTRING hraw = NULL;
        if (SUCCEEDED(IUriRuntimeClass_get_AbsoluteUri( (IUriRuntimeClass *)uri_rt, &hraw )) && hraw)
        {
            const WCHAR *s = WindowsGetStringRawBuffer( hraw, NULL );
            if (s) lstrcpynW( uri_str, s, MAX_PATH );
            WindowsDeleteString( hraw );
        }
        IInspectable_Release( (IInspectable *)uri_rt );
    }

    if (!uri_str[0] || !uwp_pri_find_xbf( uri_str, &xbf, &xbf_sz ))
    {
        IUnknown_Release( (IUnknown *)conn );
        return FALSE;
    }

    if (depth > 8)
    {
        IUnknown_Release( (IUnknown *)conn );
        return TRUE;
    }
    depth++;

    TRACE( "uwp_try_load_xbf_component: %s (%lu bytes) on %p (depth %d)\n",
           debugstr_w(uri_str), (unsigned long)xbf_sz, component, depth );

    /* 1. Connect(1, component) and GetBindingConnector(1, component) first so root & {x:Bind} are wired */
    {
        struct uwp_element *comp_el = uwp_elem_from_any( component );
        HRESULT (WINAPI *conn_fn)(void *, int, void *) = (void *)(*(void ***)conn)[6];
        HRESULT chr = conn_fn( conn, 1, component );
        TRACE( "uwp_try_load_xbf_component: Connect(1,comp) -> %#lx\n", chr );
        if (SUCCEEDED(IInspectable_QueryInterface( (IInspectable *)component, &IID_IComponentConnector2_UWP_g, &conn2 )) && conn2)
        {
            HRESULT (WINAPI *get_bc)(void *, int, void *, void **) = (void *)(*(void ***)conn2)[6];
            if (get_bc && SUCCEEDED(get_bc( conn2, 1, component, &binding )) && binding)
            {
                HRESULT (WINAPI *b_conn)(void *, int, void *) = (void *)(*(void ***)binding)[6];
                if (comp_el) comp_el->binding_connector = binding;
                if (b_conn) b_conn( binding, 1, component );
            }
            IUnknown_Release( (IUnknown *)conn2 );
        }
    }

    /* 2. Build visual tree from XBF v2 stream and Connect child elements */
    uwp_parse_and_build_xbf( xbf, xbf_sz, (IInspectable *)component, conn, TRUE, app_outer, -1, NULL, NULL );

    /* 2b. Realize default deferred elements inside empty panels */
    {
        struct uwp_element *comp_el = uwp_elem_from_any( component );
        if (comp_el)
            uwp_realize_empty_panel_defers( comp_el );
    }

    IUnknown_Release( (IUnknown *)conn );
    depth--;
    return TRUE;
}

/* ============================================================================
 * 7. Genuine Visual Tree Layout, Rendering & Input Dispatch
 * ============================================================================ */

float g_layout_scale = 1.0f;

