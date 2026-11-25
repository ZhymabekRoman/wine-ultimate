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
#include "uwp_private.h"
#include "xbf_private.h"
WINE_DEFAULT_DEBUG_CHANNEL(xaml);

static void xbf_compile_emit_rd_store( struct xbf_aot_builder *aot, UINT32 parent_node_idx,
                                       UINT32 child_node_idx, const WCHAR *key,
                                       const struct xbf_stack_val *sv )
{
    struct xbf_aot_op aop = {0};

    if (!aot || aot->aborted || !key || !key[0] || !sv) return;
    aop.kind = XBF_AOT_OP_RD_STORE;
    aop.sv_kind = (UINT8)sv->kind;
    aop.has_argb = sv->has_argb ? 1 : 0;
    aop.tid = sv->tid;
    aop.node_idx = parent_node_idx;
    aop.child_idx = child_node_idx;
    aop.str_off = xbf_aot_intern_str( aot, key );
    aop.str2_off = xbf_aot_intern_str( aot, (sv->kind == UWP_RD_STYLE) ? sv->based_on : sv->str_val );
    aop.argb = sv->argb;
    aop.cid = (sv->template_xbf_data && sv->template_xbf_size) ? (INT32)sv->template_sub_idx : -1;
    aop.num = (sv->kind == UWP_RD_BRUSH || sv->kind == UWP_RD_COLOR) ? sv->opacity : sv->dval;
    memcpy( aop.vec4, sv->vec4, sizeof(aop.vec4) );
    if (sv->setter_cnt > 0 && sv->setters)
    {
        aop.setter_start = xbf_aot_append_setters( aot, sv->setters, sv->setter_cnt );
        aop.setter_cnt = (UINT32)sv->setter_cnt;
    }
    xbf_aot_emit_op( aot, &aop );
}

static void xbf_vsm_holder_free( struct uwp_element *holder )
{
    int g, s;
    if (!holder) return;
    for (g = 0; g < holder->vsm_group_cnt; g++)
    {
        for (s = 0; s < holder->vsm_groups[g].state_cnt; s++)
            free( holder->vsm_groups[g].states[s].setters );
        free( holder->vsm_groups[g].states );
    }
    free( holder->vsm_groups );
    free( holder );
}

static void xbf_compile_substream( struct xbf_ctx *ctx, int sub_idx, BOOL has_initial_root,
                                   const struct xbf_rd_table *rd_tbl, UINT32 active_rd_node_idx,
                                   struct uwp_element *vsm_holder )
{
    DWORD sub_start = 0, end_p = 0, sub_sz = 0, p;
    struct xbf_stack_val val_stack[64];
    DWORD start_off_stack[64];
    UINT32 node_idx_stack[64];
    BOOL is_rd_stack[64];
    int obj_sp = 0, base_sp = 0;
    WORD prop_stack[64];
    int prop_sp = 0;
    struct xbf_stack_val last_popped;
    DWORD last_popped_off = 0;
    UINT32 last_popped_node_idx = (~0u);
    BOOL last_popped_is_rd = FALSE;
    BOOL has_last_popped = FALSE;

    memset( val_stack, 0, sizeof(val_stack) );
    memset( &last_popped, 0, sizeof(last_popped) );
    memset( start_off_stack, 0, sizeof(start_off_stack) );
    memset( node_idx_stack, 0xff, sizeof(node_idx_stack) );
    memset( is_rd_stack, 0, sizeof(is_rd_stack) );

    if (!ctx->aot || ctx->aot->aborted) return;
    if (sub_idx < 0 || !xbf_get_substream_bounds( ctx, (DWORD)sub_idx, &sub_start, &end_p, &sub_sz ))
        return;
    if (ctx->sub_done)
    {
        if (ctx->sub_done[sub_idx]) return;
        ctx->sub_done[sub_idx] = TRUE;
    }

    p = sub_start;

    if (has_initial_root)
    {
        node_idx_stack[0] = 0u;
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
            TRACE( "XBF compile push op=%#x tid=%#x cls=%s at %lu/%lu (sub %d)\n", op, tid,
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
            else if (tid == XBF_TYPE_DOUBLE || tid == XBF_TYPE_INT32 ||
                     ((op == 0x15 || op == 0x16) && (vt_init == 0x03 || vt_init == 0x04 || vt_init == 0x0b)))
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
            else if (tid == XBF_TYPE_CORNER_RADIUS)
            {
                sv.kind = UWP_RD_THICKNESS;
                if (vt_init == 0x06)
                {
                    memcpy( sv.vec4, vec4_init, sizeof(sv.vec4) );
                }
                else if (vt_init == 0x03 || vt_init == 0x04)
                {
                    sv.vec4[0] = sv.vec4[1] = sv.vec4[2] = sv.vec4[3] = num_init;
                }
                else if (sbuf_init[0])
                {
                    WCHAR *ep = sbuf_init;
                    double v0 = wcstod( ep, &ep );
                    sv.vec4[0] = sv.vec4[1] = sv.vec4[2] = sv.vec4[3] = v0;
                    if (ep && (*ep == L',' || *ep == L' '))
                    {
                        int ci;
                        for (ci = 1; ci < 4 && *ep; ci++)
                        {
                            while (*ep == L',' || *ep == L' ') ep++;
                            if (!*ep) break;
                            sv.vec4[ci] = wcstod( ep, &ep );
                        }
                    }
                }
            }
            else if (tid == XBF_TYPE_FONT_FAMILY)
            {
                sv.kind = UWP_RD_FONT_FAMILY;
                if (sbuf_init[0])
                {
                    WCHAR face[64] = {0};
                    lstrcpynW( sv.str_val, sbuf_init, ARRAY_SIZE(sv.str_val) );
                    uwp_resolve_font_family( sbuf_init, face, ARRAY_SIZE(face) );
                }
            }
            else if (tid == XBF_TYPE_BRUSH_C || tid == XBF_TYPE_BRUSH_B ||
                     tid == XBF_TYPE_IMAGE_BRUSH || tid == XBF_TYPE_LINEAR_GRADIENT_BRUSH ||
                     (!(tid & 0x8000) && tid < ctx->type_cnt && wcsstr( ctx->types[tid].short_name, L"Brush" )))
            {
                sv.kind = UWP_RD_BRUSH;
                if (op == 0x16 && sbuf_init[0] && uwp_parse_color_string( sbuf_init, &sv.argb ))
                    sv.has_argb = TRUE;
            }
            else if (tid == XBF_TYPE_DATA_TEMPLATE || tid == XBF_TYPE_CONTROL_TEMPLATE)
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
            sv.tid = tid;

            if ((sub_idx == 0 || ctx->vsm_collect) && !ctx->rd_only && vsm_holder)
            {
                if (tid == XBF_TYPE_VISUAL_STATE_GROUP)
                    uwp_vsm_add_group( vsm_holder, NULL );
                else if (tid == XBF_TYPE_VISUAL_STATE)
                {
                    struct uwp_vsm_group *grp = (vsm_holder->vsm_group_cnt > 0)
                        ? &vsm_holder->vsm_groups[vsm_holder->vsm_group_cnt - 1]
                        : uwp_vsm_add_group( vsm_holder, L"DefaultGroup" );
                    uwp_vsm_add_state( grp, NULL );
                }
            }

            if (obj_sp < (int)ARRAY_SIZE(val_stack))
            {
                UINT32 cur_node_idx = (~0u);
                UINT8 nflags = 0;
                const WCHAR *short_n = L"";
                BOOL reuse_active_rd = (rd_tbl != NULL && obj_sp == 0 && is_rd_type && active_rd_node_idx != (~0u));
                if (is_rd_type) nflags |= XBF_AOT_NODE_IS_RD;
                if (reuse_active_rd)
                {
                    cur_node_idx = active_rd_node_idx;
                    if (cur_node_idx < ctx->aot->node_cnt)
                        ctx->aot->nodes[cur_node_idx].flags |= XBF_AOT_NODE_IS_RD;
                }
                else if (op == 0x17 && ctx->root_el)
                {
                    nflags |= XBF_AOT_NODE_ROOT_REUSE;
                    cur_node_idx = 0;
                    if (ctx->aot->node_cnt > 0)
                        ctx->aot->nodes[0].flags |= nflags;
                }
                else if ((ctx->rd_only || rd_tbl) && !is_rd_type &&
                         ((!ctx->connector &&
                           !(tid < ctx->type_cnt && (wcsstr( ctx->types[tid].full_name, L"Breakpoint" ) ||
                                                     wcsstr( ctx->types[tid].full_name, L"TemplateSelector" ) ||
                                                     wcsstr( ctx->types[tid].full_name, L"Converter" ) ||
                                                     wcsstr( ctx->types[tid].full_name, L"Layout" )))) ||
                          tid == XBF_TYPE_STYLE || tid == XBF_TYPE_BRUSH_C || tid == XBF_TYPE_SETTER_A || tid == XBF_TYPE_SETTER_B ||
                          tid == XBF_TYPE_BRUSH_B || tid == XBF_TYPE_DOUBLE || tid == XBF_TYPE_INT32 || tid == XBF_TYPE_STRING ||
                          tid == XBF_TYPE_THICKNESS || tid == XBF_TYPE_CORNER_RADIUS || tid == XBF_TYPE_FONT_FAMILY ||
                          tid == XBF_TYPE_COLUMN_DEFINITION || tid == XBF_TYPE_RESOURCE_DICTIONARY))
                {
                    nflags |= XBF_AOT_NODE_LIGHTWEIGHT;
                    cur_node_idx = xbf_aot_add_node( ctx->aot, L"", L"", tid, nflags, FALSE, 0 );
                }
                else if (tid & 0x8000)
                {
                    cls_name = uwp_btype_to_class_name( tid );
                    nflags |= XBF_AOT_NODE_BUILTIN;
                    if (tid == XBF_TYPE_DEFERRED_ELEMENT) nflags |= XBF_AOT_NODE_DEFERRED;
                    cur_node_idx = xbf_aot_add_node( ctx->aot, cls_name, L"", tid, nflags,
                                                     (sv.kind == UWP_RD_BRUSH && sv.has_argb), sv.argb );
                }
                else if (tid < ctx->type_cnt)
                {
                    cls_name = ctx->types[tid].full_name;
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
                if (cur_node_idx != (~0u) && !reuse_active_rd)
                {
                    struct xbf_aot_op aop = {0};
                    aop.kind = XBF_AOT_OP_CREATE_NODE;
                    aop.node_idx = cur_node_idx;
                    aop.child_idx = (~0u);
                    xbf_aot_emit_op( ctx->aot, &aop );
                }
                val_stack[obj_sp] = sv;
                start_off_stack[obj_sp] = cur_rel_off;
                node_idx_stack[obj_sp] = cur_node_idx;
                is_rd_stack[obj_sp] = is_rd_type;
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
            if (obj_sp > 0 && node_idx_stack[obj_sp - 1] != (~0u) && slen > 0)
            {
                WCHAR xcls[128];
                DWORD cp = min( slen, (DWORD)(ARRAY_SIZE(xcls) - 1) );
                struct xbf_aot_op aop = {0};
                memcpy( xcls, ctx->nodes + p, cp * sizeof(WCHAR) );
                xcls[cp] = 0;
                aop.kind = XBF_AOT_OP_SET_XCLASS;
                aop.node_idx = node_idx_stack[obj_sp - 1];
                aop.child_idx = (~0u);
                aop.str_off = xbf_aot_intern_str( ctx->aot, xcls );
                xbf_aot_emit_op( ctx->aot, &aop );
            }
            p += slen * 2;
        }
        else if (op == 0x0c) /* ConnectionId */
        {
            int vt = 0;
            double num = 0;
            if (!xbf_read_value( ctx, ctx->nodes, &p, end_p, &vt, &num, NULL, 0, NULL )) break;
            if (obj_sp > 0 && node_idx_stack[obj_sp - 1] != (~0u))
            {
                struct xbf_aot_op aop = {0};
                aop.kind = XBF_AOT_OP_CONNECT;
                aop.node_idx = node_idx_stack[obj_sp - 1];
                aop.child_idx = (~0u);
                aop.cid = (int)num;
                xbf_aot_emit_op( ctx->aot, &aop );
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
                WORD top_tid = val_stack[obj_sp - 1].tid;
                UINT32 top_nidx = node_idx_stack[obj_sp - 1];
                if (top_nidx != (~0u) && top_nidx < ctx->aot->node_cnt &&
                    !(ctx->aot->nodes[top_nidx].flags & XBF_AOT_NODE_LIGHTWEIGHT))
                {
                    struct xbf_aot_op aop = {0};
                    aop.kind = XBF_AOT_OP_SET_XNAME;
                    aop.node_idx = top_nidx;
                    aop.child_idx = (~0u);
                    aop.str_off = xbf_aot_intern_str( ctx->aot, sbuf );
                    xbf_aot_emit_op( ctx->aot, &aop );
                }
                if ((sub_idx == 0 || ctx->vsm_collect) && !ctx->rd_only && vsm_holder && vsm_holder->vsm_group_cnt > 0)
                {
                    struct uwp_vsm_group *grp = &vsm_holder->vsm_groups[vsm_holder->vsm_group_cnt - 1];
                    if (top_tid == XBF_TYPE_VISUAL_STATE_GROUP)
                        lstrcpynW( grp->name, sbuf, ARRAY_SIZE(grp->name) );
                    else if (top_tid == XBF_TYPE_VISUAL_STATE && grp->state_cnt > 0)
                        lstrcpynW( grp->states[grp->state_cnt - 1].name, sbuf, ARRAY_SIZE(grp->states[grp->state_cnt - 1].name) );
                }
            }
        }
        else if (op == 0x0e) /* x:Uid */
        {
            int vt = 0;
            double num = 0;
            WCHAR sbuf[128] = {0};
            if (!xbf_read_value( ctx, ctx->nodes, &p, end_p, &vt, &num, sbuf, ARRAY_SIZE(sbuf), NULL )) break;
            if (sbuf[0] && (obj_sp == 0 || node_idx_stack[obj_sp - 1] != (~0u)))
            {
                struct xbf_aot_op aop = {0};
                aop.kind = XBF_AOT_OP_SET_PROP;
                aop.vt = (UINT8)vt;
                aop.pid = XBF_PROP_AUTOMATION_ID;
                aop.node_idx = (obj_sp > 0) ? node_idx_stack[obj_sp - 1] : 0u;
                aop.child_idx = (~0u);
                aop.str_off = xbf_aot_intern_str( ctx->aot, sbuf );
                xbf_aot_emit_op( ctx->aot, &aop );
            }
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
            if (obj_sp > 0 && (val_stack[obj_sp - 1].tid == XBF_TYPE_DATA_TEMPLATE ||
                               val_stack[obj_sp - 1].tid == XBF_TYPE_CONTROL_TEMPLATE))
            {
                val_stack[obj_sp - 1].template_xbf_data = ctx->xbf;
                val_stack[obj_sp - 1].template_xbf_size = ctx->xbf_sz;
                val_stack[obj_sp - 1].template_sub_idx = sub_i;
                if (node_idx_stack[obj_sp - 1] < ctx->aot->node_cnt)
                    ctx->aot->nodes[node_idx_stack[obj_sp - 1]].template_sub_idx = sub_i;
                TRACE( "XBF Template SetDeferredProperty captured substream %u\n", (unsigned)sub_i );
            }
        }
        else if (op == 0x02) /* PopProperty */
        {
            if (prop_sp > 0) prop_sp--;
        }
        else if (op == 0x1d) /* SetValueWithType: <pid:u16> <tid:u16> */
        {
            WORD pid, t_tid;
            if (p + 4 > end_p) break;
            pid = *(const WORD *)(ctx->nodes + p);
            t_tid = *(const WORD *)(ctx->nodes + p + 2);
            p += 4;
            if (obj_sp > 0 && val_stack[obj_sp - 1].kind == UWP_RD_STYLE && pid == XBF_PROP_STYLE_TARGET_TYPE)
            {
                const WCHAR *tname = (t_tid & 0x8000)
                    ? uwp_btype_to_class_name( t_tid )
                    : ((t_tid < ctx->type_cnt) ? ctx->types[t_tid].full_name : L"");
                if (tname && tname[0])
                    lstrcpynW( val_stack[obj_sp - 1].target_type, tname, ARRAY_SIZE(val_stack[obj_sp - 1].target_type) );
            }
        }
        else if (op == 0x1f) /* SetValueFromTemplateBinding: <pid:u16> <src_pid:u16> (no vt byte) */
        {
            WORD pid, src_pid;
            if (p + 4 > end_p) break;
            pid = *(const WORD *)(ctx->nodes + p);
            src_pid = *(const WORD *)(ctx->nodes + p + 2);
            p += 4;
            if (obj_sp > 0)
            {
                UINT32 top_nidx = node_idx_stack[obj_sp - 1];
                if (top_nidx != (~0u) && top_nidx < ctx->aot->node_cnt &&
                    !(ctx->aot->nodes[top_nidx].flags & XBF_AOT_NODE_LIGHTWEIGHT))
                {
                    struct xbf_aot_op aop = {0};
                    const WCHAR *src_name = (!(src_pid & 0x8000) && src_pid < ctx->prop_cnt)
                        ? ctx->props[src_pid].name : L"";
                    aop.kind = XBF_AOT_OP_TEMPLATE_BINDING;
                    aop.node_idx = top_nidx;
                    aop.child_idx = (~0u);
                    aop.pid = pid;
                    aop.tid = src_pid;
                    aop.str_off = xbf_aot_intern_str( ctx->aot, src_name );
                    xbf_aot_emit_op( ctx->aot, &aop );
                }
            }
        }
        else if ((op >= 0x1a && op <= 0x1e) || op == 0x24) /* SetValue / SetStaticResource / SetThemeResource */
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
                UINT32 top_nidx = node_idx_stack[obj_sp - 1];

                /* Update compile-time stack value metadata (brush color/opacity, style BasedOn, etc.) */
                xbf_apply_property( ctx, NULL, top_sv, pid, vt, num, sbuf, vec4, is_res_ref );

                if (top_nidx != (~0u) && top_nidx < ctx->aot->node_cnt &&
                    !(ctx->aot->nodes[top_nidx].flags & XBF_AOT_NODE_LIGHTWEIGHT))
                {
                    struct xbf_aot_op aop = {0};
                    aop.kind = XBF_AOT_OP_SET_PROP;
                    aop.node_idx = top_nidx;
                    aop.child_idx = (~0u);
                    aop.pid = pid;
                    aop.vt = vt;
                    aop.num = num;
                    memcpy( aop.vec4, vec4, sizeof(aop.vec4) );
                    aop.str_off = xbf_aot_intern_str( ctx->aot, sbuf );
                    aop.is_res_ref = is_res_ref ? 1 : 0;
                    xbf_aot_emit_op( ctx->aot, &aop );
                }
                if ((sub_idx == 0 || ctx->vsm_collect) && !ctx->rd_only && vsm_holder && vsm_holder->vsm_group_cnt > 0)
                {
                    struct uwp_vsm_group *grp = &vsm_holder->vsm_groups[vsm_holder->vsm_group_cnt - 1];
                    struct uwp_vsm_state *st = (grp->state_cnt > 0) ? &grp->states[grp->state_cnt - 1] : NULL;
                    if (top_sv->tid == XBF_TYPE_ADAPTIVE_TRIGGER && st && (pid == XBF_PROP_MIN_WINDOW_HEIGHT || pid == XBF_PROP_MIN_WINDOW_WIDTH))
                    {
                        double trig_val = num;
                        if ((is_res_ref || vt == 0x05 || vt == 0x09) && sbuf[0])
                        {
                            struct uwp_rd_entry *de = NULL;
                            if (uwp_rd_find_entry( ctx->target_rd, ctx->root_el, sbuf, 0, &de, NULL ) &&
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
                    else if ((top_sv->tid == 0x80c8 || top_sv->tid == 0x80ca) && st) /* DoubleAnimation */
                    {
                        if (pid == XBF_PROP_STORYBOARD_TARGET_NAME && sbuf[0])
                            lstrcpynW( top_sv->vsm_target, sbuf, ARRAY_SIZE(top_sv->vsm_target) );
                        else if (pid == XBF_PROP_STORYBOARD_TARGET_PROPERTY && sbuf[0])
                            vsm_clean_prop_name( sbuf, top_sv->vsm_prop, ARRAY_SIZE(top_sv->vsm_prop) );
                        else if (pid == 0x816f) /* DoubleAnimation.To */
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
            last_popped_is_rd = FALSE;
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
                last_popped_is_rd = is_rd_stack[obj_sp];
                memset( &val_stack[obj_sp], 0, sizeof(val_stack[obj_sp]) );
                has_last_popped = TRUE;

                if ((sub_idx == 0 || ctx->vsm_collect) && !ctx->rd_only && vsm_holder && vsm_holder->vsm_group_cnt > 0)
                {
                    struct uwp_vsm_group *grp = &vsm_holder->vsm_groups[vsm_holder->vsm_group_cnt - 1];
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
                    else if ((last_popped.tid == 0x80c8 || last_popped.tid == 0x80ca) && st &&
                             last_popped.vsm_target[0] && last_popped.vsm_prop[0] && last_popped.vsm_has_val)
                    {
                        uwp_vsm_state_add_setter( st, last_popped.vsm_target, last_popped.vsm_prop,
                                                  last_popped.vsm_val_str, last_popped.vsm_vt,
                                                  last_popped.vsm_num, last_popped.vsm_is_res_ref );
                    }
                    else if (last_popped.tid == XBF_TYPE_VISUAL_STATE && st && !st->name[0])
                    {
                        WCHAR synth[64];
                        swprintf( synth, ARRAY_SIZE(synth), L"__vsm_g%d_s%d",
                                  vsm_holder->vsm_group_cnt - 1, grp->state_cnt - 1 );
                        lstrcpynW( st->name, synth, ARRAY_SIZE(st->name) );
                        if (last_popped_node_idx != (~0u))
                        {
                            struct xbf_aot_op aop = {0};
                            aop.kind = XBF_AOT_OP_SET_XNAME;
                            aop.node_idx = last_popped_node_idx;
                            aop.child_idx = (~0u);
                            aop.str_off = xbf_aot_intern_str( ctx->aot, synth );
                            xbf_aot_emit_op( ctx->aot, &aop );
                        }
                    }
                }

                /* Implicit Style (TargetType without explicit x:Key) inside a ResourceDictionary */
                if (last_popped.kind == UWP_RD_STYLE && last_popped.target_type[0] &&
                    (ctx->rd_only || (obj_sp > 0 && is_rd_stack[obj_sp - 1])))
                {
                    BYTE next_op = (p < end_p) ? ctx->nodes[p] : 0xff;
                    if (next_op != 0x0a && next_op != 0x07 && next_op != 0x08 && next_op != 0x09 && next_op != 0x20)
                    {
                        UINT32 rd_parent_idx = (obj_sp > 0 && is_rd_stack[obj_sp - 1])
                            ? node_idx_stack[obj_sp - 1] : active_rd_node_idx;
                        xbf_compile_emit_rd_store( ctx->aot, rd_parent_idx, last_popped_node_idx,
                                                   last_popped.target_type, &last_popped );
                    }
                }

                /* Indexed ResourceDictionary entry without trailing 0x0a AddToDictionary (e.g. Dark.xbf, TileDataTemplates.xbf) */
                if (rd_tbl &&
                    (obj_sp == base_sp || (obj_sp == base_sp + 1 && is_rd_stack[obj_sp - 1])))
                {
                    BYTE next_op = (p < end_p) ? ctx->nodes[p] : 0xff;
                    if (next_op != 0x0a && next_op != 0x07 && next_op != 0x08 && next_op != 0x09 && next_op != 0x20)
                    {
                        UINT32 rd_parent_idx = (obj_sp > 0 && is_rd_stack[obj_sp - 1])
                            ? node_idx_stack[obj_sp - 1] : active_rd_node_idx;
                        int k;
                        for (k = 0; k < rd_tbl->count; k++)
                        {
                            if (rd_tbl->items[k].sub_off == last_popped_off &&
                                rd_tbl->items[k].key_sid < ctx->str_cnt && ctx->strs[rd_tbl->items[k].key_sid])
                            {
                                xbf_compile_emit_rd_store( ctx->aot, rd_parent_idx, last_popped_node_idx,
                                                           ctx->strs[rd_tbl->items[k].key_sid], &last_popped );
                            }
                        }
                        free( last_popped.setters );
                        memset( &last_popped, 0, sizeof(last_popped) );
                        last_popped_node_idx = (~0u);
                        last_popped_is_rd = FALSE;
                        has_last_popped = FALSE;
                    }
                }
            }
            else
            {
                free( last_popped.setters );
                memset( &last_popped, 0, sizeof(last_popped) );
                last_popped_node_idx = (~0u);
                last_popped_is_rd = FALSE;
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
                WORD pid = (prop_sp > 0) ? prop_stack[prop_sp - 1] : 0;
                UINT32 parent_idx = (obj_sp > 0 && node_idx_stack[obj_sp - 1] != (~0u))
                    ? node_idx_stack[obj_sp - 1] : active_rd_node_idx;
                if (pid == XBF_PROP_THEME_DICTIONARIES && last_popped_is_rd && last_popped_node_idx != (~0u))
                {
                    struct xbf_aot_op aop = {0};
                    aop.kind = XBF_AOT_OP_RD_LINK_THEME;
                    aop.node_idx = parent_idx;
                    aop.child_idx = last_popped_node_idx;
                    aop.str_off = xbf_aot_intern_str( ctx->aot, kbuf );
                    xbf_aot_emit_op( ctx->aot, &aop );
                }
                else
                {
                    xbf_compile_emit_rd_store( ctx->aot, parent_idx, last_popped_node_idx, kbuf, &last_popped );
                }
            }
            free( last_popped.setters );
            memset( &last_popped, 0, sizeof(last_popped) );
            last_popped_node_idx = (~0u);
            last_popped_is_rd = FALSE;
            has_last_popped = FALSE;
        }
        else if (op == 0x08) /* AddToCollection */
        {
            if (has_last_popped && obj_sp > 0)
            {
                WORD pid = (prop_sp > 0) ? prop_stack[prop_sp - 1] : XBF_PROP_PANEL_CHILDREN;
                if (node_idx_stack[obj_sp - 1] != (~0u) && last_popped_node_idx != (~0u))
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
            last_popped_is_rd = FALSE;
            has_last_popped = FALSE;
        }
        else if (op == 0x07) /* SetObjectProperty <pid:u16> */
        {
            WORD pid = 0;
            if (end_p - p < 2) break;
            pid = *(const WORD *)(ctx->nodes + p);
            p += 2;
            if (has_last_popped)
            {
                if (obj_sp == 0 || node_idx_stack[obj_sp - 1] != (~0u))
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
            last_popped_is_rd = FALSE;
            has_last_popped = FALSE;
        }
        else if (op == 0x20) /* ProvideValue and SetProperty (u16 pid) */
        {
            WORD pid = 0;
            if (end_p - p < 2) break;
            pid = *(const WORD *)(ctx->nodes + p);
            p += 2;
            if (has_last_popped && obj_sp > 0 && node_idx_stack[obj_sp - 1] != (~0u) && last_popped_node_idx != (~0u))
            {
                struct xbf_aot_op aop = {0};
                aop.kind = XBF_AOT_OP_PROVIDE_VALUE;
                aop.pid = pid;
                aop.node_idx = node_idx_stack[obj_sp - 1];
                aop.child_idx = last_popped_node_idx;
                xbf_aot_emit_op( ctx->aot, &aop );
            }
            free( last_popped.setters );
            memset( &last_popped, 0, sizeof(last_popped) );
            last_popped_node_idx = (~0u);
            last_popped_is_rd = FALSE;
            has_last_popped = FALSE;
        }
        else if (op == 0x09)
        {
            free( last_popped.setters );
            memset( &last_popped, 0, sizeof(last_popped) );
            last_popped_node_idx = (~0u);
            last_popped_is_rd = FALSE;
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
                    UINT32 ppanel_idx = (obj_sp >= 2 && node_idx_stack[obj_sp - 2] != (~0u))
                        ? node_idx_stack[obj_sp - 2] : 0u;
                    p += 4;
                    xbf_aot_add_defer( ctx->aot, dname, def_sub, ppanel_idx );
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
                    xbf_compile_substream( ctx, (int)def_sub, ctx->root_el != NULL, NULL, active_rd_node_idx, vsm_holder );
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
                UINT32 sub_rd_node_idx = (obj_sp > 0 && node_idx_stack[obj_sp - 1] != (~0u))
                    ? node_idx_stack[obj_sp - 1] : active_rd_node_idx;
                if (!xbf_read_rd_defer_header( ctx->nodes, &p, end_p, def_sub_sz, &tbl, &prop_sp ))
                {
                    free( tbl.items );
                    break;
                }
                {
                    BOOL prev_rd_only = ctx->rd_only;
                    ctx->rd_only = TRUE;
                    xbf_compile_substream( ctx, (int)def_sub, FALSE, &tbl, sub_rd_node_idx, vsm_holder );
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
                    if (node_idx_stack[obj_sp - 1] < ctx->aot->node_cnt)
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
                        last_popped_node_idx = (~0u);
                        last_popped_is_rd = FALSE;
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
                    last_popped_node_idx = (~0u);
                    last_popped_is_rd = FALSE;
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
    struct uwp_element *vsm_holder = NULL;
    struct xbf_aot_header *tpl = NULL;
    LARGE_INTEGER t0 = {0}, t1 = {0}, freq = {0};
    BOOL is_template = (out_created != NULL && attach_root == NULL);
    BOOL aot_compiled_ok = FALSE;
    const BYTE *meta_base, *meta_end;
    UINT64 offs[6];
    DWORD i;

    if (!xbf || xbf_sz < 0x50 || memcmp( xbf, "XBF\0", 4 ) != 0) return;

    root_el_pre = attach_root ? attach_root : uwp_elem_from_any( component );
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

    if (!rd_only)
    {
        UINT64 aot_hash = xbf_aot_compute_hash( xbf, xbf_sz,
            (root_el_pre && root_el_pre->class_name[0]) ? root_el_pre->class_name : L"", only_sub_idx );
        struct xbf_aot_cache_slot *slot;

        QueryPerformanceFrequency( &freq );
        QueryPerformanceCounter( &t0 );

        slot = xbf_aot_cache_acquire( aot_hash, xbf_sz, only_sub_idx );
        if (slot)
        {
            const struct xbf_aot_header *cached_tpl = slot->tpl;
            double us;
            xbf_aot_instantiate( cached_tpl, xbf, xbf_sz, component, connector, is_uwp_conn, app_outer,
                                 root_el_pre, out_created, ctx.base_xbf_dir[0] ? ctx.base_xbf_dir : NULL,
                                 target_rd, is_template );
            QueryPerformanceCounter( &t1 );
            us = freq.QuadPart ? ((double)(t1.QuadPart - t0.QuadPart) * 1000000.0 / (double)freq.QuadPart) : 0.0;
            TRACE( "XBF AOT MEM hit (%s, sub=%d, %lu xbf -> %lu aot bytes, %u nodes, %u ops) in %.1f us\n",
                   debugstr_w(root_el_pre ? root_el_pre->class_name : L""),
                   only_sub_idx, (unsigned long)xbf_sz, (unsigned long)cached_tpl->total_bytes,
                   cached_tpl->node_cnt, cached_tpl->op_cnt, us );
            InterlockedDecrement( &slot->ref );
            return;
        }

        aot_b.hash = aot_hash;
    }

    aot_b.xbf_sz = xbf_sz;
    aot_b.only_sub_idx = only_sub_idx;
    if (root_el_pre)
        xbf_aot_add_node( &aot_b, root_el_pre->class_name, L"", 0, XBF_AOT_NODE_ROOT_REUSE, FALSE, 0 );
    ctx.aot = &aot_b;

    ctx.xbf = xbf;
    ctx.xbf_sz = xbf_sz;
    ctx.meta_sz = *(const DWORD *)(xbf + 4);
    ctx.node_sz = *(const DWORD *)(xbf + 8);
    if (ctx.meta_sz > xbf_sz - 12 || ctx.node_sz > xbf_sz - 12 - ctx.meta_sz) goto cleanup;

    meta_base = xbf + 12;
    meta_end = meta_base + ctx.meta_sz;
    memcpy( offs, xbf + 20, sizeof(offs) );
    ctx.app_outer = app_outer;
    ctx.connector = connector;
    ctx.is_uwp_conn = is_uwp_conn;
    ctx.root_comp = component;
    ctx.target_rd = target_rd;
    ctx.rd_only = rd_only;
    ctx.is_template = is_template;
    ctx.root_el = root_el_pre;

    /* 1. String Table */
    {
        const BYTE *p;
        if (offs[0] > ctx.meta_sz || ctx.meta_sz - (DWORD)offs[0] < 4) goto cleanup;
        p = meta_base + (size_t)offs[0];
        ctx.str_cnt = *(const DWORD *)p;
        p += 4;
        if (ctx.str_cnt > 16384 || ctx.str_cnt > (DWORD)(meta_end - p) / 4) goto cleanup;
        ctx.strs = calloc( ctx.str_cnt, sizeof(WCHAR *) );
        if (!ctx.strs && ctx.str_cnt > 0) goto cleanup;
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
                xbf_aot_add_ref( ctx.aot, s, s, TRUE );
                if (wcsstr( s, L"Resource" ) || wcsstr( s, L"Theme" ) || wcsstr( s, L"Generic" ) ||
                    wcsstr( s, L"Dictionary" ) || wcsstr( s, L"Style" ))
                {
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

    /* 4. Compile Node Substreams to AOT IR (no live UI/COM side effects) */
    if (!rd_only && ctx.root_el)
        vsm_holder = calloc( 1, sizeof(*vsm_holder) );

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
                    xbf_compile_substream( &ctx, only_sub_idx, FALSE, NULL, (~0u), vsm_holder );
                }
                else if (rd_only)
                {
                    xbf_compile_substream( &ctx, 0, FALSE, NULL, (~0u), NULL );
                }
                else
                {
                    xbf_compile_substream( &ctx, 0, ctx.root_el != NULL, NULL, (~0u), vsm_holder );
                    /* Also compile substream 1..N for VisualStateGroups / Styles that hold connection IDs */
                    for (i = 1; i < ctx.sub_cnt; i++)
                    {
                        BOOL is_lazy = FALSE;
                        UINT32 d;
                        for (d = 0; d < aot_b.defer_cnt; d++)
                            if (aot_b.defers[d].sub_idx == (INT32)i) { is_lazy = TRUE; break; }
                        if (!is_lazy)
                            xbf_compile_substream( &ctx, (int)i, FALSE, NULL, (~0u), vsm_holder );
                    }
                }
                free( ctx.sub_done );
                aot_compiled_ok = TRUE;
            }
        }
    }

    /* 5. Finalize IR and execute via single unified xbf_aot_instantiate() path */
    if (aot_compiled_ok && !aot_b.aborted)
    {
        if (vsm_holder)
            xbf_aot_snapshot_vsm( &aot_b, vsm_holder, 0 );
        tpl = xbf_aot_finalize( &aot_b, &ctx );
        if (tpl)
        {
            xbf_aot_instantiate( tpl, xbf, xbf_sz, component, connector, is_uwp_conn, app_outer,
                                 root_el_pre, out_created, ctx.base_xbf_dir[0] ? ctx.base_xbf_dir : NULL,
                                 target_rd, is_template );
            if (!rd_only)
            {
                double us;
                QueryPerformanceCounter( &t1 );
                us = freq.QuadPart ? ((double)(t1.QuadPart - t0.QuadPart) * 1000000.0 / (double)freq.QuadPart) : 0.0;
                TRACE( "XBF AOT COMPILE+EXEC (%s, sub=%d, %lu xbf -> %lu aot bytes, %u nodes, %u ops) in %.1f us\n",
                       debugstr_w(root_el_pre ? root_el_pre->class_name : L""),
                       only_sub_idx, (unsigned long)xbf_sz, (unsigned long)tpl->total_bytes,
                       tpl->node_cnt, tpl->op_cnt, us );
                xbf_aot_cache_insert( tpl );
            }
            else
            {
                free( tpl );
            }
        }
    }

cleanup:
    xbf_vsm_holder_free( vsm_holder );
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

IInspectable *uwp_instantiate_xbf_substream_rd( const BYTE *xbf_data, DWORD xbf_size, DWORD sub_idx,
                                                IInspectable *owner_comp, struct uwp_rd_bucket *rd_bucket )
{
    struct uwp_element *created = NULL;
    void *conn = NULL;
    if (!xbf_data || !xbf_size) return NULL;
    if (owner_comp)
        IInspectable_QueryInterface( owner_comp, &IID_IComponentConnector_UWP_g, &conn );
    uwp_ensure_app_resources_loaded();
    uwp_parse_xbf_internal( xbf_data, xbf_size, owner_comp, conn, TRUE, uwp_get_app_outer(),
                            (int)sub_idx, NULL, &created, NULL, rd_bucket, FALSE );
    if (conn) IUnknown_Release( (IUnknown *)conn );
    if (!created) return NULL;
    {
        IInspectable *ret = created->outer ? created->outer : &created->IInspectable_iface;
        IInspectable_AddRef( ret );
        return ret;
    }
}

IInspectable *uwp_instantiate_xbf_substream( const BYTE *xbf_data, DWORD xbf_size, DWORD sub_idx, IInspectable *owner_comp )
{
    return uwp_instantiate_xbf_substream_rd( xbf_data, xbf_size, sub_idx, owner_comp, NULL );
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
