/* Internal XBF v2 parser / direct single-pass interpreter declarations
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

/* pri.c */
BOOL uwp_pri_lookup_string( const WCHAR *res_name, WCHAR *out_buf, int out_chars );
void uwp_pri_enum_uid_props( const WCHAR *uid,
                             void (*cb)( const WCHAR *prop_rel, const WCHAR *val, void *user ),
                             void *user );

/* xbf_decode.c */
BOOL xbf_read_uleb( const BYTE *buf, DWORD *io_p, DWORD end_p, DWORD *out_val );
BOOL xbf_get_substream_bounds( const struct xbf_ctx *ctx, DWORD sub_idx,
                               DWORD *out_start, DWORD *out_end, DWORD *out_size );
void uwp_rd_store_stack_val( struct uwp_rd_bucket *b, const WCHAR *key, struct xbf_stack_val *sv );
BOOL xbf_read_value( struct xbf_ctx *ctx, const BYTE *nd, DWORD *io_p, DWORD end_p,
                     int *out_vt, double *out_num, WCHAR *out_str, int out_str_cap, double out_vec4[4] );
void xbf_apply_property( struct xbf_ctx *ctx, struct uwp_element *el, struct xbf_stack_val *sv,
                         WORD pid, int vt, double num, const WCHAR *sval,
                         const double vec4[4], BOOL is_res_ref );
BOOL xbf_read_vsm_defer_header( const BYTE *nodes, DWORD *io_p, DWORD end_p,
                                DWORD def_sub_sz, int *io_prop_sp );
BOOL xbf_read_style_defer_table( struct xbf_ctx *ctx, DWORD *io_p, DWORD end_p,
                                 DWORD def_sub, DWORD def_sub_sz,
                                 struct xbf_stack_val *style_sv, int *io_prop_sp );
BOOL xbf_read_rd_defer_header( const BYTE *nodes, DWORD *io_p, DWORD end_p,
                               DWORD def_sub_sz, struct xbf_rd_table *tbl, int *io_prop_sp );

#endif /* __WINE_XBF_PRIVATE_H */
