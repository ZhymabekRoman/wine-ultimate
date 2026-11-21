/* Internal XBF v2 parser/AOT/instantiation declarations
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

#ifndef __WINE_XBF_PRIVATE_H
#define __WINE_XBF_PRIVATE_H

#include "uwp_private.h"

/* ============================================================================
 * 6b. Ahead-of-Time (AOT) XBF v2 -> Native Binary Scene-Graph Compiler & Cache
 * ============================================================================ */

#define XBF_AOT_MAGIC       0x58424641u /* 'XBFA' */
#define XBF_AOT_VERSION     9u
#define XBF_AOT_CACHE_SLOTS 64

enum xbf_aot_node_flags
{
    XBF_AOT_NODE_ROOT_REUSE  = 0x01,
    XBF_AOT_NODE_LIGHTWEIGHT = 0x02,
    XBF_AOT_NODE_BUILTIN     = 0x04,
    XBF_AOT_NODE_APP_TYPE    = 0x08,
    XBF_AOT_NODE_NO_PROVIDER = 0x10,
    XBF_AOT_NODE_USING_NS    = 0x20,
    XBF_AOT_NODE_IS_RD       = 0x40,
    XBF_AOT_NODE_DEFERRED    = 0x80,
};

enum xbf_aot_op_kind
{
    XBF_AOT_OP_CREATE_NODE = 1,
    XBF_AOT_OP_SET_XCLASS,
    XBF_AOT_OP_SET_XNAME,
    XBF_AOT_OP_CONNECT,
    XBF_AOT_OP_SET_PROP,
    XBF_AOT_OP_ADD_CHILD,
    XBF_AOT_OP_SET_OBJ_PROP,
    XBF_AOT_OP_PROVIDE_VALUE,
    XBF_AOT_OP_RD_LINK_THEME,
    XBF_AOT_OP_RD_STORE,
};

struct xbf_aot_header
{
    UINT32 magic;
    UINT32 version;
    UINT64 hash;
    UINT32 xbf_sz;
    INT32  only_sub_idx;
    UINT32 total_bytes;

    UINT32 type_cnt;
    UINT32 prop_cnt;
    UINT32 ref_cnt;
    UINT32 res_uri_cnt;
    UINT32 node_cnt;
    UINT32 op_cnt;
    UINT32 style_setter_cnt;
    UINT32 vsm_group_cnt;
    UINT32 vsm_state_cnt;
    UINT32 vsm_setter_cnt;
    UINT32 defer_cnt;
    UINT32 str_pool_bytes;

    UINT32 off_types;
    UINT32 off_props;
    UINT32 off_refs;
    UINT32 off_res_uris;
    UINT32 off_nodes;
    UINT32 off_ops;
    UINT32 off_style_setters;
    UINT32 off_vsm_groups;
    UINT32 off_vsm_states;
    UINT32 off_vsm_setters;
    UINT32 off_defers;
    UINT32 off_str_pool;
};

struct xbf_aot_ref
{
    UINT32 full_name_off;
    UINT32 short_name_off;
    UINT32 is_resource_path;
};

struct xbf_aot_node
{
    UINT32 class_name_off;
    UINT32 short_name_off;
    UINT16 tid;
    UINT8  flags;
    UINT8  has_init_argb;
    UINT32 init_argb;
    UINT32 template_sub_idx;
};

struct xbf_aot_op
{
    UINT8  kind;
    UINT8  sv_kind;
    UINT8  is_res_ref;
    UINT8  has_argb;
    UINT16 pid;
    UINT16 tid;
    UINT32 node_idx;
    UINT32 child_idx;
    INT32  vt;
    INT32  cid;
    UINT32 str_off;
    UINT32 str2_off;
    UINT32 argb;
    UINT32 setter_start;
    UINT32 setter_cnt;
    double num;
    double vec4[4];
};

struct xbf_aot_vsm_group
{
    UINT32 name_off;
    UINT32 state_start;
    UINT32 state_cnt;
};

struct xbf_aot_vsm_state
{
    UINT32 name_off;
    UINT32 has_trigger;
    double min_window_width;
    double min_window_height;
    UINT32 setter_start;
    UINT32 setter_cnt;
};

struct xbf_aot_vsm_setter
{
    UINT32 target_off;
    UINT32 property_off;
    UINT32 val_str_off;
    INT32  vt;
    INT32  is_res_ref;
    double num;
};

struct xbf_aot_defer
{
    UINT32 name_off;
    INT32  sub_idx;
    UINT32 parent_panel_idx;
};

struct xbf_aot_builder
{
    UINT64 hash;
    UINT32 xbf_sz;
    INT32  only_sub_idx;
    BOOL   aborted;

    struct xbf_aot_ref *refs;
    UINT32 ref_cnt;
    int    ref_cap;

    UINT32 *res_uris;
    UINT32 res_uri_cnt;
    int    res_uri_cap;

    struct xbf_aot_node *nodes;
    UINT32 node_cnt;
    int    node_cap;

    struct xbf_aot_op *ops;
    UINT32 op_cnt;
    int    op_cap;

    struct uwp_style_setter *style_setters;
    UINT32 style_setter_cnt;
    int    style_setter_cap;

    struct xbf_aot_vsm_group *vsm_groups;
    UINT32 vsm_group_cnt;
    int    vsm_group_cap;

    struct xbf_aot_vsm_state *vsm_states;
    UINT32 vsm_state_cnt;
    int    vsm_state_cap;

    struct xbf_aot_vsm_setter *vsm_setters;
    UINT32 vsm_setter_cnt;
    int    vsm_setter_cap;

    struct xbf_aot_defer *defers;
    UINT32 defer_cnt;
    int    defer_cap;

    BYTE  *str_pool;
    UINT32 str_pool_bytes;
    int    str_pool_cap;
};

struct xbf_aot_cache_slot
{
    UINT64 hash;
    struct xbf_aot_header *tpl;
    UINT64 last_used;
    LONG   ref;
};


/* xbf_decode.c */
BOOL uwp_pri_lookup_string( const WCHAR *res_name, WCHAR *out_buf, int out_chars );
BOOL xbf_read_uleb( const BYTE *buf, DWORD *io_p, DWORD end_p, DWORD *out_val );
BOOL xbf_get_substream_bounds( const struct xbf_ctx *ctx, DWORD sub_idx,
                               DWORD *out_start, DWORD *out_end, DWORD *out_size );
void xbf_stack_val_add_setter( struct xbf_stack_val *sv, WORD pid, int vt, BOOL is_res_ref,
                               double num, const WCHAR *sval, const double vec4[4] );
void uwp_rd_store_stack_val( struct uwp_rd_bucket *b, const WCHAR *key, struct xbf_stack_val *sv );
BOOL xbf_read_value( struct xbf_ctx *ctx, const BYTE *nd, DWORD *io_p, DWORD end_p,
                     int *out_vt, double *out_num, WCHAR *out_str, int out_str_cap, double out_vec4[4] );
void xbf_apply_property( struct xbf_ctx *ctx, struct uwp_element *el, struct xbf_stack_val *sv,
                         WORD pid, int vt, double num, const WCHAR *sval,
                         const double vec4[4], BOOL is_res_ref );
void xbf_parse_style_setter_sub( struct xbf_ctx *ctx, int sub_idx, DWORD off, WORD target_pid,
                                 struct xbf_stack_val *out_sv );
BOOL xbf_read_vsm_defer_header( const BYTE *nodes, DWORD *io_p, DWORD end_p,
                                DWORD def_sub_sz, int *io_prop_sp );
BOOL xbf_read_style_defer_table( struct xbf_ctx *ctx, DWORD *io_p, DWORD end_p,
                                 DWORD def_sub, DWORD def_sub_sz,
                                 struct xbf_stack_val *style_sv, int *io_prop_sp );
BOOL xbf_read_rd_defer_header( const BYTE *nodes, DWORD *io_p, DWORD end_p,
                               DWORD def_sub_sz, struct xbf_rd_table *tbl, int *io_prop_sp );

/* xbf_aot.c */
UINT64 xbf_aot_compute_hash( const BYTE *xbf, DWORD xbf_sz, const WCHAR *root_cls, int only_sub_idx );
UINT32 xbf_aot_intern_str( struct xbf_aot_builder *b, const WCHAR *s );
void xbf_aot_add_ref( struct xbf_aot_builder *b, const WCHAR *full_name, const WCHAR *short_name,
                      BOOL is_resource_path );
void xbf_aot_add_res_uri( struct xbf_aot_builder *b, const WCHAR *uri );
UINT32 xbf_aot_add_node( struct xbf_aot_builder *b, const WCHAR *cls_name, const WCHAR *short_name,
                         WORD tid, UINT8 flags, BOOL has_init_argb, UINT32 init_argb );
void xbf_aot_emit_op( struct xbf_aot_builder *b, const struct xbf_aot_op *op_in );
void xbf_aot_add_defer( struct xbf_aot_builder *b, const WCHAR *name, int sub_idx, UINT32 parent_panel_idx );
UINT32 xbf_aot_append_setters( struct xbf_aot_builder *b, const struct uwp_style_setter *setters, int cnt );
void xbf_aot_snapshot_vsm( struct xbf_aot_builder *b, struct uwp_element *root_el, int start_group_idx );
void xbf_aot_builder_free( struct xbf_aot_builder *b );
struct xbf_aot_header *xbf_aot_finalize( struct xbf_aot_builder *b, const struct xbf_ctx *ctx );
void xbf_aot_save_to_disk( const struct xbf_aot_header *hdr );
struct xbf_aot_cache_slot *xbf_aot_cache_acquire( UINT64 hash, DWORD xbf_sz, int only_sub_idx, BOOL *from_disk );
void xbf_aot_cache_insert( struct xbf_aot_header *tpl );
void xbf_aot_instantiate( const struct xbf_aot_header *hdr, const BYTE *xbf, DWORD xbf_sz,
                          IInspectable *component, void *connector, BOOL is_uwp_conn, void *app_outer,
                          struct uwp_element *root_el, struct uwp_element **out_created,
                          struct uwp_rd_bucket *target_rd, BOOL is_template );

/* xbf_instantiate.c */
void xbf_exec_substream_rd( struct xbf_ctx *ctx, int sub_idx, struct uwp_element *initial_root,
                            struct uwp_element **out_created, const struct xbf_rd_table *rd_tbl,
                            struct uwp_rd_bucket *active_rd );

#endif /* __WINE_XBF_PRIVATE_H */
