/* WinRT Windows.UI.Xaml XBF, PRI catalog, and type-metadata cache declarations
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

#ifndef __WINE_WINDOWS_UI_XAML_UWP_XBF_H
#define __WINE_WINDOWS_UI_XAML_UWP_XBF_H

#include "uwp_types.h"
#include "xbf_known_types.h"

#define XAML_META_CACHE_SIZE 1024

struct pri_xbf_entry;
struct xbf_aot_builder;

struct xbf_type_info
{
    WCHAR xmlns[128];
    WCHAR full_name[128];
    WCHAR short_name[64];
};

struct xbf_prop_info
{
    WORD owner_tid;
    WCHAR name[64];
};

struct xbf_rd_map_item
{
    WORD key_sid;
    DWORD sub_off;
};

struct xbf_rd_table
{
    struct xbf_rd_map_item *items;
    int count;
    int cap;
};

struct pri_xbf_catalog
{
    struct pri_xbf_entry *entries;
    DWORD cnt;
    DWORD cap;
};

struct xbf_ctx
{
    const BYTE *xbf;
    DWORD xbf_sz;
    DWORD meta_sz;
    DWORD node_sz;
    WCHAR **strs;
    DWORD str_cnt;
    WCHAR **xmlns;
    DWORD xmlns_cnt;
    struct xbf_type_info *types;
    DWORD type_cnt;
    struct xbf_prop_info *props;
    DWORD prop_cnt;
    const BYTE *nodes;
    DWORD sub_cnt;
    DWORD sub_base;
    BOOL *sub_done;
    void *app_outer;
    void *connector;
    BOOL is_uwp_conn;
    struct uwp_element *root_el;
    IInspectable *root_comp;
    struct uwp_rd_bucket *target_rd;
    char base_xbf_dir[256];
    BOOL rd_only;
    BOOL is_template;
    BOOL vsm_collect;
    struct xbf_aot_builder *aot;
};

struct xbf_stack_val
{
    enum uwp_rd_kind kind;
    WORD tid;
    struct uwp_element *el;
    IInspectable *raw;
    UINT32 argb;
    BOOL has_argb;
    double opacity;
    double dval;
    double vec4[4];
    WCHAR str_val[128];
    WCHAR based_on[96];
    WCHAR target_type[128];
    struct uwp_style_setter *setters;
    int setter_cnt;
    int setter_cap;
    WCHAR vsm_target[64];
    WCHAR vsm_prop[64];
    WCHAR vsm_val_str[128];
    int vsm_vt;
    double vsm_num;
    BOOL vsm_is_res_ref;
    BOOL vsm_has_val;
    const BYTE *template_xbf_data;
    DWORD template_xbf_size;
    DWORD template_sub_idx;
    IInspectable *template_owner_comp;
};

struct uwp_defer_entry
{
    WCHAR name[64];
    int sub_idx;
    struct uwp_element *parent_panel;
    int child_idx;
    const BYTE *xbf_data;
    DWORD xbf_size;
    IInspectable *root_component;
    BOOL realized;
};

struct xaml_cached_type
{
    const void **lpVtbl;
    const void **lpVtbl_winui3;
    LONG ref;
    WCHAR *full_name;
    const WCHAR *short_name;
    WORD xbf_type_id;
};

struct xaml_meta_cache_entry
{
    struct xaml_meta_cache_entry *next;
    WCHAR *full_name;
    WORD xbf_type_id;
    struct xaml_cached_type *builtin_type;
    struct xaml_type *provider_type;
    void *provider_key;
};

/* ============================================================================
 * XAML Type-Metadata Reflection Cache (IXamlMetadataProvider + xbf_known_types)
 *
 * O(1) FNV-1a hash index protected by SRWLOCK (shared read on lookup, exclusive
 * write on miss).  Pre-seeds all xbf_known_types[] entries on first use and
 * caches IXamlType instances (both builtin XAML types and app-provider RCWs).
 * ============================================================================ */
extern SRWLOCK xaml_meta_cache_lock;
extern INIT_ONCE xaml_meta_cache_once;
extern struct xaml_meta_cache_entry *xaml_meta_cache[XAML_META_CACHE_SIZE];

/* PRI / XBF catalog globals */
extern struct pri_xbf_catalog g_pri_cat;
extern const BYTE *g_pri_data;
extern INIT_ONCE g_pri_once;
extern DWORD g_pri_size;
extern struct pri_xbf_catalog g_winui_pri_cat;
extern BOOL g_winui_pri_cat_done;
extern const BYTE *g_winui_pri_data;
extern BOOL g_winui_pri_probed;
extern DWORD g_winui_pri_size;
extern int g_loaded_res_xbf_cnt;
extern char g_loaded_res_xbfs[128][256];

/* PRI / XBF decode, instantiation, deferred realization & metadata cache APIs */
void ensure_winui_pri_cat( void );
const BYTE *pri_get_data( DWORD *out_size );
const BYTE *winui_pri_get_data( DWORD *out_size );
void pri_map_file_to( const WCHAR *pri_path, const BYTE **out_data, DWORD *out_size );
void scan_disk_xbfs_in_dir( const WCHAR *dir_path, const char *rel_prefix, struct pri_xbf_catalog *cat );
BOOL WINAPI init_pri_once( INIT_ONCE *once, void *param, void **ctx );
BOOL WINAPI init_pri_xbf_cat_once( INIT_ONCE *once, void *param, void **ctx );
BOOL WINAPI init_xaml_meta_cache( INIT_ONCE *once, void *param, void **ctx );
BOOL pri_xbf_collect_cb( const char *path, const BYTE *xbf, DWORD sz, void *ctx );
void uwp_pri_enum_xbfs_in_blob( const BYTE *pri, DWORD pri_sz,
                                BOOL (*cb)(const char *path, const BYTE *xbf, DWORD sz, void *ctx), void *ctx );
BOOL uwp_pri_find_xbf_rel( const char *base_xbf_dir, const WCHAR *uri_or_name,
                           const BYTE **out_ptr, DWORD *out_sz,
                           char *out_resolved, size_t out_resolved_sz );
BOOL uwp_pri_get_cat_entry( DWORD idx, const char **out_path, const BYTE **out_data, DWORD *out_sz );
IInspectable *uwp_instantiate_xbf_substream( const BYTE *xbf_data, DWORD xbf_size, DWORD sub_idx, IInspectable *owner_comp );
IInspectable *uwp_instantiate_xbf_substream_rd( const BYTE *xbf_data, DWORD xbf_size, DWORD sub_idx, IInspectable *owner_comp, struct uwp_rd_bucket *rd_bucket );
void xbf_apply_property( struct xbf_ctx *ctx, struct uwp_element *el, struct xbf_stack_val *sv,
                         WORD pid, int vt, double num, const WCHAR *sval,
                         const double vec4[4], BOOL is_res_ref );
void uwp_load_res_xbf_into_bucket( const char *base_dir, const WCHAR *uri_or_path, struct uwp_rd_bucket *target );
void uwp_mark_xbf_ref_instantiated( struct uwp_element *root, const WCHAR *cls_name );
void uwp_record_xbf_ref( struct uwp_element *root, const WCHAR *full_name,
                         const WCHAR *short_name, BOOL is_resource_path );
void uwp_parse_and_build_xbf( const BYTE *xbf, DWORD xbf_sz, IInspectable *component,
                              void *connector, BOOL is_uwp_conn, void *app_outer,
                              int only_sub_idx, struct uwp_element *attach_root,
                              struct uwp_element **out_created );
void uwp_parse_xbf_internal( const BYTE *xbf, DWORD xbf_sz, IInspectable *component,
                             void *connector, BOOL is_uwp_conn, void *app_outer,
                             int only_sub_idx, struct uwp_element *attach_root,
                             struct uwp_element **out_created,
                             const char *base_xbf_path, struct uwp_rd_bucket *target_rd,
                             BOOL rd_only );
void uwp_parse_xbf_into_rd( const BYTE *xbf, DWORD xbf_sz, const char *base_xbf_path,
                            struct uwp_rd_bucket *target_rd );
BOOL uwp_populate_element_from_pri_xbf( struct uwp_element *el, const WCHAR *short_name );
BOOL uwp_populate_page_from_xbf_refs( struct uwp_element *page_el );
void uwp_realize_empty_panel_defers( struct uwp_element *root );
void uwp_realize_deferred_entry( struct uwp_defer_entry *de );
struct xaml_type *uwp_provider_get_xaml_type_cached( struct xaml_metadata_provider *mp,
                                                     const WCHAR *owner_type_name );
struct xaml_meta_cache_entry *xaml_meta_insert_locked( const WCHAR *full_name, WORD xbf_id );

static inline const WCHAR *xbf_resolve_prop_name( WORD pid, const struct xbf_ctx *ctx )
{
    if (!(pid & 0x8000) && ctx && pid < ctx->prop_cnt)
        return ctx->props[pid].name;
    return NULL;
}

static inline BOOL xbf_prop_is_item_template( WORD pid, const struct xbf_ctx *ctx, BOOL *is_selector )
{
    if (pid == XBF_PROP_ITEMS_CONTROL_ITEM_TEMPLATE_SELECTOR)
    {
        if (is_selector) *is_selector = TRUE;
        return TRUE;
    }
    if (pid == XBF_PROP_ITEMS_CONTROL_ITEM_TEMPLATE)
    {
        if (is_selector) *is_selector = FALSE;
        return TRUE;
    }
    {
        const WCHAR *pname = xbf_resolve_prop_name( pid, ctx );
        if (pname)
        {
            if (!wcscmp( pname, L"ItemTemplateSelector" ))
            {
                if (is_selector) *is_selector = TRUE;
                return TRUE;
            }
            if (!wcscmp( pname, L"ItemTemplate" ))
            {
                if (is_selector) *is_selector = FALSE;
                return TRUE;
            }
        }
    }
    return FALSE;
}

#endif /* __WINE_WINDOWS_UI_XAML_UWP_XBF_H */
