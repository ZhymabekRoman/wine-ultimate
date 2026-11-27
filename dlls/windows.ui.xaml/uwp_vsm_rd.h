/* WinRT Windows.UI.Xaml ResourceDictionary, DependencyProperty, VSM, Binding & Factory declarations
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

#ifndef __WINE_WINDOWS_UI_XAML_UWP_VSM_RD_H
#define __WINE_WINDOWS_UI_XAML_UWP_VSM_RD_H

#include "uwp_types.h"

#define XAML_FACTORY_HASH_SIZE 32   /* power of two, > 2x the entry count */

struct xbf_ctx;
struct uwp_vsm_snapshot_entry;

struct uwp_style_setter
{
    WORD pid;
    int vt;
    BOOL is_res_ref;
    double num;
    double vec4[4];
    WCHAR str[128];
    WCHAR prop_name[64];
    WCHAR owner_cls[128];
};

struct uwp_dp_entry
{
    void *dp;
    IInspectable *value;
};

struct uwp_dp_cb_entry
{
    void *dp;
    IInspectable *callback;
    INT64 token;
};

struct uwp_dp_changed_args
{
    const void *vtbl;
    LONG ref;
    IInspectable *old_val;
    IInspectable *new_val;
    void *dp;
};

struct uwp_dc_args
{
    const void *vtbl;
    LONG ref;
    IInspectable *new_val;
    boolean handled;
};

struct uwp_rd_entry
{
    WCHAR key[96];
    enum uwp_rd_kind kind;
    WORD tid;
    IInspectable *value;
    UINT32 argb;
    BOOL has_argb;
    double opacity;
    double dval;
    double vec4[4];
    WCHAR str_val[128];
    struct uwp_style_setter *setters;
    int setter_cnt;
    int setter_cap;
    const BYTE *template_xbf_data;
    DWORD template_xbf_size;
    DWORD template_sub_idx;
    IInspectable *template_owner_comp;
};

struct uwp_rd_bucket
{
    void *rd_key;
    struct uwp_element *owner_el;
    struct uwp_rd_entry *entries;
    int count;
    int cap;
    int *hash_slots;
    int hash_mask;
    struct uwp_rd_bucket **merged;
    int merged_cnt;
    int merged_cap;
    struct uwp_rd_bucket *theme_dark;
    struct uwp_rd_bucket *theme_default;
    struct uwp_rd_bucket *theme_light;
    struct uwp_rd_bucket *theme_hc;
    BOOL app_seeded;
    struct uwp_rd_bucket *next;
};

struct uwp_vsm_setter
{
    WCHAR target[64];
    WCHAR property[64];
    WCHAR val_str[128];
    int vt;
    double num;
    BOOL is_res_ref;
};

struct uwp_vsm_state
{
    WCHAR name[64];
    BOOL has_trigger;
    double min_window_width;
    double min_window_height;
    struct uwp_vsm_setter *setters;
    int setter_cnt;
    int setter_cap;
};

struct uwp_vsm_group
{
    WCHAR name[64];
    WCHAR current_state[64];
    int active_state_idx;
    struct uwp_vsm_state *states;
    int state_cnt;
    int state_cap;
    struct uwp_vsm_snapshot_entry *snapshots;
    int snapshot_cnt;
    int snapshot_cap;
};

#define UWP_FF_COMP_TARGET          (1ULL << 0)
#define UWP_FF_COMPOSABLE_FALLBACK  (1ULL << 1)
#define UWP_FF_PROP_PATH            (1ULL << 2)
#define UWP_FF_SYMBOL_ICON          (1ULL << 3)
#define UWP_FF_UIELEMENT            (1ULL << 4)
#define UWP_FF_BUTTON               (1ULL << 5)
#define UWP_FF_TOGGLE_BTN_OR_MENU   (1ULL << 6)

struct uwp_class_factory
{
    IActivationFactory IActivationFactory_iface;
    IInspectable IComposableFactory_iface;
    IInspectable IStatics_iface;
    const WCHAR *class_name;
    IInspectable IDpStatics_iface;
    IInspectable IBitmapImageFactory_iface;
    IInspectable IFontFamilyFactory_iface;
    UINT64 factory_flags;
};

static inline UINT64 uwp_cf_compute_flags( const WCHAR *cls )
{
    UINT64 f = 0;
    if (!cls || !cls[0]) return 0;
    if (!wcscmp( cls, L"Windows.UI.Xaml.Media.CompositionTarget" )) f |= UWP_FF_COMP_TARGET;
    if (wcsstr( cls, L"Control" ) || wcsstr( cls, L"Source" ) || wcsstr( cls, L"Box" ) ||
        wcsstr( cls, L"Item" ) || wcsstr( cls, L"View" ) || wcsstr( cls, L"Panel" ) ||
        wcsstr( cls, L"Button" ) || wcsstr( cls, L"Selector" ) || wcsstr( cls, L"Flyout" ) ||
        wcsstr( cls, L"Separator" ) || wcsstr( cls, L"Trigger" ) || wcsstr( cls, L"Transition" ) ||
        wcsstr( cls, L"Extension" ) || wcsstr( cls, L"BitmapSource" ) || wcsstr( cls, L"Accelerator" ) ||
        wcsstr( cls, L"Command" ) || wcsstr( cls, L"Timer" ) || wcsstr( cls, L"Configuration" ) ||
        wcsstr( cls, L"Collection" ) || wcsstr( cls, L"Shadow" ))
        f |= UWP_FF_COMPOSABLE_FALLBACK;
    if (!wcscmp( cls, L"Windows.UI.Xaml.PropertyPath" )) f |= UWP_FF_PROP_PATH;
    else if (!wcscmp( cls, L"Windows.UI.Xaml.Controls.SymbolIcon" )) f |= UWP_FF_SYMBOL_ICON;
    if (!wcscmp( cls, L"Windows.UI.Xaml.UIElement" )) f |= UWP_FF_UIELEMENT;
    else if (!wcscmp( cls, L"Windows.UI.Xaml.Controls.Button" )) f |= UWP_FF_BUTTON;
    else if (!wcscmp( cls, L"Windows.UI.Xaml.Controls.ToggleMenuFlyoutItem" ) ||
             !wcscmp( cls, L"Windows.UI.Xaml.Controls.Primitives.ToggleButton" ))
        f |= UWP_FF_TOGGLE_BTN_OR_MENU;
    return f;
}

struct xaml_static_factory
{
    const WCHAR *name;
    IActivationFactory *factory;
};

struct uwp_calc_vm
{
    IInspectable *vm;             /* Active ViewModel RCW/instance */
    IInspectable *npc;            /* Held INotifyPropertyChanged face for remove_PropertyChanged on teardown */
    EventRegistrationToken npc_token;
    BOOL npc_hooked;
    IInspectable *cmd;            /* Optional ICommand face resolved via IXamlMember / QI */
    IDependencyProperty *display_dp;
    WCHAR vm_class_name[128];
    BOOL pumped;
    int last_code;                /* dedupe repeated clicks on init-pending VM */
};

static inline BOOL uwp_style_is_accent_name( const WCHAR *name )
{
    if (!name || !name[0]) return FALSE;
    return wcsstr( name, L"AccentButton" ) != NULL ||
           wcsstr( name, L"PrimaryButtonStyle" ) != NULL ||
           wcsstr( name, L"AccentStyle" ) != NULL;
}

static inline BOOL uwp_brush_key_is_accent( const WCHAR *key )
{
    if (!key || !key[0]) return FALSE;
    return wcsstr( key, L"AccentButtonBackground" ) != NULL ||
           wcsstr( key, L"AccentFillColorDefault" ) != NULL ||
           wcsstr( key, L"SystemControlBackgroundAccentBrush" ) != NULL ||
           wcsstr( key, L"SystemControlHighlightAccentBrush" ) != NULL ||
           wcsstr( key, L"SystemAccentColor" ) != NULL ||
           !wcscmp( key, L"AccentBrush" );
}

/* Globals */
extern struct uwp_calc_vm g_calc_vm;
extern int g_app_requested_theme;
extern int g_app_rd_sentinel;
extern BOOL g_app_res_loaded;
extern BOOL g_app_res_loading;
extern struct uwp_rd_bucket *g_rd_buckets_head;
extern int g_theme_rd_sentinel;
extern struct uwp_class_factory **g_dyn_factories;
extern SRWLOCK g_dyn_factories_lock;
extern int g_dyn_factory_cnt;
extern WCHAR **g_dyn_factory_names;
extern int g_dyn_factory_cap;
extern const size_t xaml_static_factories_count;
extern const struct xaml_static_factory *xaml_factory_hash[32];
extern INIT_ONCE xaml_factory_hash_once;
extern const struct xaml_static_factory xaml_static_factories[];

/* DependencyObject / DependencyProperty / DP-changed-args */
HRESULT WINAPI uwp_do_ClearValue( void *iface, void *dp );
HRESULT WINAPI uwp_do_GetValue( void *iface, void *dp, IInspectable **out );
HRESULT WINAPI uwp_do_SetValue( void *iface, void *dp, IInspectable *val );
HRESULT WINAPI uwp_do_get_Dispatcher( void *iface, void **value );
HRESULT WINAPI uwp_do_get_DispatcherQueue( void *iface, void **value );
HRESULT WINAPI uwp_do2_RegisterPropertyChangedCallback( void *iface, void *dp, void *cb, INT64 *token );
void uwp_sync_all_template_presenters( struct uwp_element *impl );
HRESULT WINAPI dp_args_QI( void *iface, REFIID iid, void **out );
ULONG WINAPI dp_args_AddRef( void *iface );
ULONG WINAPI dp_args_Release( void *iface );
ULONG WINAPI dc_args_Release( void *iface );
HRESULT WINAPI dp_args_GetIids( void *iface, ULONG *count, IID **iids );
HRESULT WINAPI dp_args_GetRuntimeClassName( void *iface, HSTRING *name );
HRESULT WINAPI dp_args_GetTrustLevel( void *iface, TrustLevel *level );
HRESULT WINAPI dp_args_get_Property( void *iface, void **out );
HRESULT WINAPI dp_args_get_OldValue( void *iface, IInspectable **out );
HRESULT WINAPI dp_args_get_NewValue( void *iface, IInspectable **out );

/* ResourceDictionary / Style / Color-resource lookup */
struct uwp_rd_bucket *get_rd_bucket( void *rd_key, BOOL create );
struct uwp_rd_entry *rd_bucket_upsert( struct uwp_rd_bucket *b, const WCHAR *key );
struct uwp_rd_bucket *uwp_rd_bucket_for_el( struct uwp_element *el, BOOL create );
BOOL uwp_rd_find_entry( struct uwp_rd_bucket *start_bucket, struct uwp_element *scope_el,
                        const WCHAR *raw_key, int depth,
                        struct uwp_rd_entry **out_entry, struct uwp_rd_bucket **out_bucket );
IInspectable *uwp_rd_materialize_entry( struct uwp_rd_bucket *b, struct uwp_rd_entry *e );
void uwp_rd_link_merged( struct uwp_rd_bucket *parent, struct uwp_rd_bucket *child );
void uwp_rd_link_theme( struct uwp_rd_bucket *parent, const WCHAR *theme_name, struct uwp_rd_bucket *child );
void uwp_ensure_app_resources_loaded( void );
BOOL uwp_extract_resource_key( const WCHAR *in, WCHAR *out, size_t out_chars );
BOOL uwp_resolve_brush_or_color( struct uwp_rd_bucket *start_bucket, struct uwp_element *scope_el,
                                 const WCHAR *key_or_expr, int depth, UINT32 *out_argb );
BOOL uwp_parse_color_string( const WCHAR *s, UINT32 *out_argb );
BOOL uwp_is_bg_pid( WORD pid );
BOOL uwp_is_border_pid( WORD pid );
BOOL uwp_is_fg_pid( WORD pid );
BOOL uwp_is_display_result_element( const struct uwp_element *el );
void uwp_apply_style_by_name( struct xbf_ctx *ctx, struct uwp_element *el, const WCHAR *style_key );
void uwp_apply_style_entry( struct xbf_ctx *ctx, struct uwp_element *el,
                            struct uwp_rd_bucket *b, struct uwp_rd_entry *se, int depth );

/* VisualStateManager */
struct uwp_vsm_group *uwp_vsm_add_group( struct uwp_element *el, const WCHAR *name );
struct uwp_vsm_state *uwp_vsm_add_state( struct uwp_vsm_group *g, const WCHAR *name );
BOOL uwp_vsm_goto_state_on_tree( struct uwp_element *el, const WCHAR *state_name );
BOOL uwp_vsm_leave_state_on_tree( struct uwp_element *el, const WCHAR *state_name );
void uwp_vsm_evaluate_tree( struct uwp_element *el, double win_w, double win_h );
void uwp_vsm_invalidate_eval_chain( struct uwp_element *el );
void uwp_vsm_free_groups( struct uwp_element *el );
void uwp_vsm_state_add_setter( struct uwp_vsm_state *st, const WCHAR *target, const WCHAR *prop,
                               const WCHAR *val_str, int vt, double num, BOOL is_res_ref );
void vsm_clean_prop_name( const WCHAR *raw, WCHAR *out_prop, size_t max_prop );
void vsm_split_setter_target( const WCHAR *spec, WCHAR *out_target, size_t max_target,
                              WCHAR *out_prop, size_t max_prop );

/* Binding & DataContext engine */
void uwp_propagate_data_context( struct uwp_element *impl, IInspectable *dc );
void uwp_apply_binding( struct uwp_element *tel, void *dp, struct uwp_element *bel );
void uwp_binding_unsubscribe_element( struct uwp_element *el );
void uwp_add_xaml_binding( struct uwp_element *tel, const WCHAR *owner_cls, const WCHAR *prop_name, struct uwp_element *bel );
void uwp_eval_xaml_binding( struct uwp_element *tel, const WCHAR *owner_cls,
                            const WCHAR *prop_name, const WCHAR *path, IInspectable *src );

/* ViewModel bridge (uwp_vm_bridge.c) */
BOOL uwp_vm_supports_display_or_cmd( IInspectable *vm );
BOOL uwp_vm_query_string_prop_depth( IInspectable *vm, const WCHAR *const *props, unsigned int count,
                                     WCHAR *out, size_t out_len, int depth );
BOOL uwp_vm_query_string_prop( IInspectable *vm, const WCHAR *const *props, unsigned int count,
                               WCHAR *out, size_t out_len );
int  uwp_resolve_button_calc_code( struct uwp_element *btn );
void uwp_calc_vm_ensure( void );
void uwp_calc_vm_send( int code, IInspectable *cmd_param );
void uwp_calc_vm_set_instance( IInspectable *vm, const WCHAR *class_name );
void uwp_calc_vm_teardown( BOOL final_cleanup );
void uwp_sync_calc_display_from_vm( void );

/* Class factory helpers */
ULONG WINAPI uwp_cf_AddRef( IActivationFactory *iface );
HRESULT WINAPI uwp_cf_GetIids( IActivationFactory *iface, ULONG *cnt, IID **iids );
HRESULT WINAPI uwp_cf_GetRuntimeClassName( IActivationFactory *iface, HSTRING *name );
HRESULT WINAPI uwp_cf_GetTrustLevel( IActivationFactory *iface, TrustLevel *lvl );
HRESULT WINAPI uwp_cf_QI( IActivationFactory *iface, REFIID iid, void **out );
ULONG WINAPI uwp_cf_Release( IActivationFactory *iface );

#endif /* __WINE_WINDOWS_UI_XAML_UWP_VSM_RD_H */
