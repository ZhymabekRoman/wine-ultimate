/* WinRT Windows.UI.Xaml layout, rendering, island hosting, input, controls & WebView2 declarations
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

#ifndef __WINE_WINDOWS_UI_XAML_UWP_LAYOUT_RENDER_H
#define __WINE_WINDOWS_UI_XAML_UWP_LAYOUT_RENDER_H

#include "uwp_types.h"

struct uwp_pointer;

struct uwp_popup_item
{
    struct uwp_element *el;
    WCHAR text[128];
    WCHAR accel[64];
    BOOL is_sep;
    BOOL enabled;
    BOOL checked;
    BOOL has_sub;
    int y;
    int h;
};

struct uwp_popup_state
{
    HWND hwnd;
    HWND owner_hwnd;
    HWND parent_popup;
    HWND child_popup;
    struct uwp_element *flyout;
    struct uwp_element *root_flyout;
    int hover_idx;
    int item_cnt;
    struct uwp_popup_item items[64];
};

struct uwp_island_state
{
    HDC dc;
    HBITMAP bmp;
    HBITMAP old_bmp;
    int w;
    int h;
    BOOL valid;
    IDXGISwapChain1 *swapchain;
    int gpu_w;
    int gpu_h;
    BOOL gpu_failed;
    /* WS_CHILD islands have no X11 whole_window, so an HWND swap chain lands on an
     * offscreen dummy parent.  Render into an offscreen render target instead and
     * blit the CPU-readable staging copy onto the child window's DC. */
    ID3D11Texture2D *rt_tex;
    ID3D11RenderTargetView *rtv;
    ID3D11Texture2D *staging_tex;
    void *bits;                 /* DIB pixel block backing dc/bmp */
    BOOL is_child;
};

struct uwp_accel_entry
{
    struct uwp_element *accel_el;
    struct uwp_element *target_el;
    struct uwp_element *page_el;
    UINT vk;
    UINT mods;
    BOOL enabled;
};

struct uwp_key_routed_args
{
    IInspectable IInspectable_iface;
    LONG ref;
    UINT vk;
    boolean handled;
    struct { UINT32 RepeatCount; UINT32 ScanCode; boolean IsExtendedKey; boolean IsMenuKeyDown; boolean WasKeyDown; boolean IsKeyReleased; } status;
};

struct uwp_ka_invoked_args
{
    IInspectable IInspectable_iface;
    LONG ref;
    boolean handled;
    IInspectable *element;
};

struct uwp_vec_view
{
    IInspectable IInspectable_iface;
    IInspectable IIterable_iface;
    LONG ref;
    IInspectable **items;
    unsigned int count;
    int cap;
    struct uwp_element *owner;
    const WCHAR *runtime_class;
};

struct uwp_rtt_target_vtbl
{
    IInspectableVtbl base;
    HRESULT (WINAPI *AddRefToTracker)( IInspectable * );
    HRESULT (WINAPI *ReleaseToTracker)( IInspectable * );
    HRESULT (WINAPI *GetStrongValue)( IInspectable *, IInspectable ** );
};

struct cwv2_pending_op
{
    struct cwv2_pending_op *next;
    enum cwv2_pending_kind kind;
    int arg_int;
    BOOL arg_bool;
    WCHAR *str1;
    WCHAR *str2;
    void *obj;
    const void **avtbl;
    const GUID *aiid;
};

struct uwp_ccc_args
{
    const void *vtbl;
    LONG ref;
    IInspectable *item;
    IInspectable *item_container;
    UINT32 phase;
    boolean in_recycle;
    boolean handled;
};

struct uwp_tapped_args
{
    IInspectable ITappedRoutedEventArgs_iface;
    IInspectable IRoutedEventArgs_iface;
    LONG ref;
    IInspectable *orig_source;
    boolean handled;
    float x, y;
};

struct uwp_pointer_routed_args
{
    const void **IPointerRoutedEventArgs_vtbl;
    const void **IRoutedEventArgs_vtbl;
    LONG ref;
    struct xaml_pointer_info info;
    IInspectable *orig_source;
    struct uwp_pointer *pointer;
    boolean handled;
};

struct uwp_sc_args_obj { const void *const *vtbl; };

struct uwp_nav_event_args
{
    IInspectable INavigationEventArgs_iface;
    IInspectable INavigationEventArgs2_iface;
    LONG ref;
    IInspectable *content;
    IInspectable *parameter;
    IInspectable *transition_info;
    IInspectable *uri;
    TypeName source_page_type;
    INT32 nav_mode;
};

/* Layout, paint, island, input, popup & WebView2 globals */
extern int g_accel_cnt;
extern struct uwp_accel_entry *g_accel_entries;
extern struct xaml_element *g_active_wv2_impl;
extern struct uwp_element *g_captured_elem;
extern struct uwp_element *g_focused_textbox;
extern struct uwp_element *g_hovered_elem;
extern ID3D11DeviceContext *g_island_d3d11_ctx;
extern ID3D11Device *g_island_d3d11_dev;
extern BOOL g_island_d3d11_failed;
extern int g_island_hwnd_cnt;
extern HWND g_island_hwnds[16];
extern struct uwp_element *g_last_closed_flyout;
extern DWORD g_last_closed_tick;
extern BOOL g_layout_dirty;
extern float g_layout_scale;
extern IInspectable *g_nav_args_item;
extern struct uwp_element *g_navigated_page_el;
extern struct uwp_element *g_open_combobox;
extern COLORREF g_paint_page_bg;
extern HWND g_popup_hwnd;
extern struct uwp_element *g_popup_owner_el;
extern IDependencyProperty *g_routed_event_ptr_pressed;
extern IDependencyProperty *g_routed_event_ptr_released;
extern IDependencyProperty *g_routed_event_tapped;
extern IDependencyProperty *g_routed_event_other;
extern HWND g_sub_popup_hwnd;
extern HWND g_wv2_core_hwnd;
extern HWND g_wv2_host_hwnd;
extern IDependencyProperty *wv2_source_dp;
extern struct uwp_sc_args_obj uwp_sc_args_obj;
extern struct uwp_element *g_routed_args_el;
extern struct uwp_element *g_pressed_btn;
extern struct uwp_element *g_pressed_tabview;
extern float g_sc_new_w;
extern float g_sc_new_h;
extern BOOL g_in_tap_diag;

/* Color, font, brush, image & media helpers */
COLORREF argb_to_colorref( UINT32 argb );
UINT32 apply_opacity_to_argb( UINT32 argb, double opacity );
COLORREF blend_argb_over( UINT32 fg_argb, COLORREF bg_cr );
void uwp_el_set_bg_argb( struct uwp_element *el, UINT32 argb );
void uwp_el_set_border_argb( struct uwp_element *el, UINT32 argb );
void uwp_el_set_fg_argb( struct uwp_element *el, UINT32 argb );
COLORREF uwp_paint_resolve_color( struct uwp_element *el, const WCHAR *key,
                                  COLORREF fallback, COLORREF surface_bg );
void uwp_resolve_font_family( const WCHAR *raw_spec, WCHAR *out_face, size_t cap );
BOOL uwp_is_bundled_font_face( const WCHAR *face );
const WCHAR *uwp_resolve_elem_font_family( const struct uwp_element *el, WCHAR *buf, size_t cap );
void uwp_load_image_asset( struct uwp_element *el );
void uwp_on_image_loaded( struct uwp_element *el );
void uwp_resolve_and_load_image( struct uwp_element *el );
void uwp_media_ensure_started( struct uwp_element *el );
BOOL uwp_extract_comp_brush_image_src( IInspectable *brush, WCHAR *out_uri, size_t cap );
BOOL uwp_extract_visual_image_src( IInspectable *visual, WCHAR *out_uri, size_t cap );
void uwp_xcbb_ensure_connected( IInspectable *brush );

/* Layout, GPU/offscreen render-target & SwapChainPanel hosting */
void uwp_arrange_element( struct uwp_element *el, RECT rc );
/* Offscreen render-target frame pair + layout pass for WS_CHILD islands. */
BOOL xaml_gpu_begin_frame_rtv( ID3D11Device *dev, ID3D11DeviceContext *ctx, ID3D11RenderTargetView *rtv,
                               UINT width, UINT height, COLORREF clear_color );
BOOL xaml_gpu_end_frame_rtv( ID3D11DeviceContext *ctx, UINT *out_instances, UINT *out_hits, UINT *out_misses );
BOOL xaml_layout_render_gpu_rtv( HWND hwnd, int width, int height, IInspectable *content,
                                 ID3D11Device *dev, ID3D11DeviceContext *ctx, ID3D11RenderTargetView *rtv );
HRESULT xaml_attach_swapchain_panel_content( IUnknown *content, IDCompositionVisual2 **slot );
HRESULT xaml_attach_swapchain_panel_handle( HANDLE h, IDCompositionVisual2 **slot );
void xaml_get_host_window_size( UINT *out_w, UINT *out_h );
void xaml_set_active_swapchain_panel( void *el, void *sc );

/* Controls, navigation, selectors, TabView, ComboBox, TextBlock, Grid, Panel, Popups & Flyouts */
size_t uwp_flatten_inlines_text( const struct uwp_element *el, WCHAR *out_buf, size_t out_cap );
const WCHAR *uwp_format_display_label( const struct uwp_element *el, WCHAR *tmp, size_t tmp_len );
void uwp_asb_fire_text_changed( struct uwp_element *el );
void uwp_asb_fire_query_submitted( struct uwp_element *el );
void uwp_asb_fire_query_submitted_ex( struct uwp_element *el, IInspectable *chosen );
void uwp_asb_fire_suggestion_chosen( struct uwp_element *el, IInspectable *chosen );
int  uwp_asb_get_suggestions( struct uwp_element *asb, IInspectable **out_items, int max_items );
BOOL uwp_asb_compute_popup_rect( const struct uwp_element *asb, int count, int win_w, int win_h,
                                 RECT *out_rc, int *out_item_h, int *out_vis_cnt, int *out_pad_y );
void uwp_asb_get_item_label( struct uwp_element *asb, int idx, IInspectable *item,
                             WCHAR *out_primary, size_t max_primary,
                             WCHAR *out_secondary, size_t max_secondary );
void uwp_fire_asb_recursive( struct uwp_element *el, const WCHAR *text, BOOL is_submit );
int uwp_tabview_get_items( struct uwp_element *tv, IInspectable **out_items, int max_items );
void uwp_tabview_get_item_title( IInspectable *item, WCHAR *buf, int buf_len );
void uwp_tabview_compute_rects( const RECT *trc, int count, int idx, RECT *pill_rc, RECT *close_rc, RECT *add_rc );
void uwp_tabview_fire_add_tab( struct uwp_element *tv );
void uwp_tabview_fire_close_tab( struct uwp_element *tv, IInspectable *item );
void uwp_tabview_select_item( struct uwp_element *tv, IInspectable *item );
HRESULT WINAPI uwp_btn_add_Click( void *iface, void *handler, EventRegistrationToken *tok );
HRESULT WINAPI uwp_btn_get_Command( void *iface, void **out );
HRESULT WINAPI uwp_btn_get_CommandParameter( void *iface, void **out );
HRESULT WINAPI uwp_btn_put_Command( void *iface, void *val );
HRESULT WINAPI uwp_btn_put_CommandParameter( void *iface, void *val );
void uwp_invoke_button( struct uwp_element *btn );
HRESULT WINAPI uwp_cd2_get_CloseButtonText( void *iface, HSTRING *out );
HRESULT WINAPI uwp_cd2_get_DefaultButton( void *iface, int *out );
HRESULT WINAPI uwp_cd2_put_CloseButtonText( void *iface, HSTRING val );
HRESULT WINAPI uwp_cd2_put_DefaultButton( void *iface, int val );
HRESULT WINAPI uwp_ctrl_ApplyTemplate( void *iface, boolean *ret );
HRESULT WINAPI uwp_ctrl_Focus( void *iface, int state, boolean *ret );
HRESULT WINAPI uwp_ctrl_get_FontFamily( void *iface, void **out );
HRESULT WINAPI uwp_ctrl_put_FontFamily( void *iface, IInspectable *val );
HRESULT WINAPI uwp_ctrl_get_FontSize( void *iface, double *val );
HRESULT WINAPI uwp_ctrl_get_IsEnabled( void *iface, boolean *val );
HRESULT WINAPI uwp_ctrl_prot_get_DefaultStyleKey( void *iface, IInspectable **out );
HRESULT WINAPI uwp_ctrl_prot_put_DefaultStyleKey( void *iface, IInspectable *val );
HRESULT WINAPI uwp_ctrl_prot_GetTemplateChild( void *iface, HSTRING child_name, void **out );
BOOL uwp_inflate_control_template( struct uwp_element *el );
HRESULT WINAPI uwp_ctrl_put_FontSize( void *iface, double val );
HRESULT WINAPI uwp_ctrl_put_IsEnabled( void *iface, boolean val );
HRESULT WINAPI uwp_grid_get_ColumnDefinitions( void *iface, void **out );
HRESULT WINAPI uwp_grid_get_RowDefinitions( void *iface, void **out );
HRESULT WINAPI uwp_panel_get_Children( void *iface, void **out );
HRESULT WINAPI uwp_rowcol_get_Length( void *iface, struct uwp_grid_length *gl );
HRESULT WINAPI uwp_rowcol_put_Length( void *iface, struct uwp_grid_length *gl );
HRESULT WINAPI uwp_tb_get_Text( void *iface, HSTRING *out );
HRESULT WINAPI uwp_tb_get_Inlines( void *iface, void **out );
HRESULT WINAPI uwp_tb_put_Text( void *iface, HSTRING val );
BOOL uwp_has_open_teachingtip( struct uwp_element *el );
void uwp_items_ctrl_materialize_source( struct uwp_element *impl, IInspectable *src );
ULONG WINAPI uwp_ccc_Release( void *iface );
HRESULT WINAPI uwp_nav_args_QI( void *iface, REFIID iid, void **out );
struct uwp_element *uwp_nav_find_item( struct uwp_element *el, int depth );
void uwp_nav_fire( struct uwp_element *el, struct xaml_event_registry *reg,
                   IInspectable *item );
HRESULT WINAPI uwp_nav_get_FooterMenuItems( void *iface, void **out );
HRESULT WINAPI uwp_nav_get_IsPaneOpen( void *iface, boolean *open );
HRESULT WINAPI uwp_nav_put_IsPaneOpen( void *iface, boolean open );
void uwp_nav_set_pane_open( struct uwp_element *nav, BOOL open );
void uwp_nav_select_item( struct uwp_element *nav, struct uwp_element *nvi );
BOOL uwp_is_combobox( const struct uwp_element *el );
BOOL uwp_is_nav_view( const struct uwp_element *el );
BOOL uwp_is_nav_header( const struct uwp_element *el );
BOOL uwp_is_nav_separator( const struct uwp_element *el );
int uwp_nav_effective_display_mode( const struct uwp_element *nav, int avail_w );
BOOL uwp_nav_is_overlay_open( const struct uwp_element *nav );
int uwp_selector_get_items( struct uwp_element *el, IInspectable **out_items, int max_items );
void uwp_selector_set_selection( struct uwp_element *impl, INT32 new_idx, IInspectable *new_item, BOOL by_index, BOOL fire_event );
void uwp_combobox_set_open( struct uwp_element *cb, BOOL open );
void uwp_combobox_get_item_label( struct uwp_element *cb, IInspectable *item, WCHAR *buf, int buf_len );
BOOL uwp_combobox_compute_popup_rect( struct uwp_element *cb, int item_cnt, int win_w, int win_h,
                                      RECT *out_rc, int *out_item_h, int *out_vis_cnt, int *out_pad_y );
HRESULT WINAPI uwp_ui_add_SelectionChanged( void *iface, void *handler, EventRegistrationToken *tok );
HRESULT WINAPI uwp_ui_remove_SelectionChanged( void *iface, EventRegistrationToken tok );
HRESULT WINAPI uwp_ui_add_ItemInvoked( void *iface, void *handler, EventRegistrationToken *tok );
HRESULT WINAPI uwp_ui_remove_ItemInvoked( void *iface, EventRegistrationToken tok );
HRESULT WINAPI uwp_frame2_Navigate( void *iface, void *type_name_ptr, void *param, void *transition_info, boolean *result );
HRESULT uwp_frame_navigate_impl( void *iface, void *type_name_ptr, void *param, void *transition_info, boolean *result );
struct uwp_nav_event_args *uwp_nav_event_args_create( IInspectable *content, IInspectable *param,
                                                      const TypeName *type, IInspectable *transition_info );
struct uwp_nav_event_args *uwp_page_stack_entry_create( IInspectable *content, IInspectable *param,
                                                        const TypeName *type, IInspectable *transition_info );
ULONG WINAPI uwp_nea_Release( void *iface );
void uwp_close_flyout_popup( BOOL fire_closed );
BOOL uwp_popup_handle_escape( void );
BOOL uwp_popup_is_open_for( struct uwp_element *flyout );
void uwp_show_flyout_popup( struct uwp_element *flyout, HWND owner_hwnd, int screen_x, int screen_y );

/* Vector / collection helpers */
HRESULT WINAPI uwp_vec_Append( void *iface, void *item );
HRESULT WINAPI uwp_vec_Clear( void *iface );
HRESULT WINAPI uwp_vec_GetAt( void *iface, unsigned int idx, void **item );
HRESULT WINAPI uwp_vec_get_Size( void *iface, unsigned int *sz );
unsigned int uwp_winrt_vec_snapshot( IInspectable *coll, IInspectable **out_items, unsigned int max_items );
unsigned int uwp_winrt_vec_count( IInspectable *coll );

/* Input, keyboard accelerators, routed/pointer args & hit-testing */
void uwp_accel_register( struct uwp_element *accel_el, struct uwp_element *target_el,
                         UINT vk, UINT mods, BOOL enabled );
BOOL uwp_invoke_accel_entry( const struct uwp_accel_entry *e );
ULONG WINAPI uwp_ka_args_Release( void *iface );
ULONG WINAPI uwp_key_args_Release( void *iface );
HRESULT WINAPI uwp_ui7_get_KeyboardAccelerators( void *iface, void **out );
HRESULT WINAPI uwp_ui7_add_PreviewKeyDown( void *iface, void *handler, EventRegistrationToken *tok );
HRESULT WINAPI uwp_ui7_remove_PreviewKeyDown( void *iface, EventRegistrationToken tok );
UINT uwp_parse_vk_string( const WCHAR *s );
void uwp_keyboard_press_char( WCHAR ch );
BOOL uwp_dispatch_keydown( HWND hwnd, UINT vk, LPARAM lparam, void *island_root_ptr );
BOOL uwp_fire_preview_keydown_tree( struct uwp_element *el, struct uwp_key_routed_args *args );
BOOL uwp_handle_overlay_mouse_input( HWND hwnd, UINT msg, int x, int y, int wheel_delta, int width, int height, IInspectable *root_content );
/* Routed/pointer args helpers (uwp_xaml.c). */
void *g_routed_args( void );
struct uwp_pointer_routed_args *uwp_pointer_routed_args_create( const struct xaml_pointer_info *info,
                                                                IInspectable *orig_source );
ULONG WINAPI pra_Release( void *iface );
ULONG WINAPI uwp_tap_Release( void *iface );
struct xaml_event_registry *uwp_elem_ptr_registry( struct uwp_element *el, enum xaml_pointer_event_kind kind );
/* Hit-testing helpers shared between uwp_xaml.c and uwp_input.c. */
struct uwp_element *uwp_hit_test_tappable( struct uwp_element *el, int x, int y );
struct uwp_element *uwp_hit_test_element( struct uwp_element *el, int x, int y );
struct uwp_element *uwp_hit_test_tabview( struct uwp_element *el, int x, int y );
struct uwp_element *uwp_hit_test_textbox( struct uwp_element *el, int x, int y );
struct uwp_element *uwp_find_button_by_text( struct uwp_element *el, const WCHAR *text );
/* Pointer/tapped dispatch (uwp_input.c, called back from uwp_xaml.c). */
BOOL uwp_fire_tapped_bubble( struct uwp_element *target, int x, int y );
BOOL uwp_fire_element_pointer( struct uwp_element *target, enum xaml_pointer_event_kind kind,
                               const struct xaml_pointer_info *info, BOOL bubble );

/* WebView2 bridge & vtable probe slots */
void wv2_ensure_host_subclass( HWND parent );
HWND wv2_get_controller_parent( void );
void wv2_start_browser( struct xaml_element *impl );
HRESULT WINAPI wv2_statics_get_SourceProperty( void *iface, void **value );
HRESULT WINAPI wv2_statics_get_prop( void *iface, void **value );
void wv2_sync_child_hwnds( HWND parent, UINT width, UINT height );
HRESULT WINAPI wv2_probe0( void *iface, ... );
HRESULT WINAPI wv2_probe1( void *iface, ... );
HRESULT WINAPI wv2_probe2( void *iface, ... );
HRESULT WINAPI wv2_probe3( void *iface, ... );
HRESULT WINAPI wv2_probe4( void *iface, ... );
HRESULT WINAPI wv2_probe5( void *iface, ... );
HRESULT WINAPI wv2_probe6( void *iface, ... );
HRESULT WINAPI wv2_probe7( void *iface, ... );
HRESULT WINAPI wv2_probe8( void *iface, ... );
HRESULT WINAPI wv2_probe9( void *iface, ... );
HRESULT WINAPI wv2_probe10( void *iface, ... );
HRESULT WINAPI wv2_probe11( void *iface, ... );
HRESULT WINAPI wv2_probe12( void *iface, ... );
HRESULT WINAPI wv2_probe13( void *iface, ... );
HRESULT WINAPI wv2_probe14( void *iface, ... );
HRESULT WINAPI wv2_probe15( void *iface, ... );
HRESULT WINAPI wv2_probe16( void *iface, ... );
HRESULT WINAPI wv2_probe17( void *iface, ... );
HRESULT WINAPI wv2_probe18( void *iface, ... );
HRESULT WINAPI wv2_probe19( void *iface, ... );
HRESULT WINAPI wv2_probe20( void *iface, ... );
HRESULT WINAPI wv2_probe21( void *iface, ... );
HRESULT WINAPI wv2_probe22( void *iface, ... );
HRESULT WINAPI wv2_probe23( void *iface, ... );
HRESULT WINAPI wv2_probe24( void *iface, ... );
HRESULT WINAPI wv2_probe25( void *iface, ... );
HRESULT WINAPI wv2_probe26( void *iface, ... );
HRESULT WINAPI wv2_probe27( void *iface, ... );
HRESULT WINAPI wv2_probe28( void *iface, ... );
HRESULT WINAPI wv2_probe29( void *iface, ... );
HRESULT WINAPI wv2_probe30( void *iface, ... );
HRESULT WINAPI wv2_probe31( void *iface, ... );
HRESULT WINAPI wv2_probe32( void *iface, ... );
HRESULT WINAPI wv2_probe33( void *iface, ... );
HRESULT WINAPI wv2_probe34( void *iface, ... );
HRESULT WINAPI wv2_probe35( void *iface, ... );
HRESULT WINAPI wv2_probe36( void *iface, ... );
HRESULT WINAPI wv2_probe37( void *iface, ... );
HRESULT WINAPI wv2_probe38( void *iface, ... );
HRESULT WINAPI wv2_probe39( void *iface, ... );
HRESULT WINAPI wv2_probe40( void *iface, ... );
HRESULT WINAPI wv2_probe41( void *iface, ... );
HRESULT WINAPI wv2_probe42( void *iface, ... );
HRESULT WINAPI wv2_probe43( void *iface, ... );
HRESULT WINAPI wv2_probe44( void *iface, ... );
HRESULT WINAPI wv2_probe45( void *iface, ... );
HRESULT WINAPI wv2_probe46( void *iface, ... );
HRESULT WINAPI wv2_probe47( void *iface, ... );
HRESULT WINAPI wv2_probe48( void *iface, ... );
HRESULT WINAPI wv2_probe49( void *iface, ... );
HRESULT WINAPI wv2_probe50( void *iface, ... );
HRESULT WINAPI wv2_probe51( void *iface, ... );
HRESULT WINAPI wv2_probe52( void *iface, ... );
HRESULT WINAPI wv2_probe53( void *iface, ... );
HRESULT WINAPI wv2_probe54( void *iface, ... );
HRESULT WINAPI wv2_probe55( void *iface, ... );
HRESULT WINAPI wv2_probe56( void *iface, ... );
HRESULT WINAPI wv2_probe57( void *iface, ... );
HRESULT WINAPI wv2_probe58( void *iface, ... );
HRESULT WINAPI wv2_probe59( void *iface, ... );
HRESULT WINAPI wv2_probe60( void *iface, ... );
HRESULT WINAPI wv2_probe61( void *iface, ... );
HRESULT WINAPI wv2_probe62( void *iface, ... );
HRESULT WINAPI wv2_probe63( void *iface, ... );

#endif /* __WINE_WINDOWS_UI_XAML_UWP_LAYOUT_RENDER_H */
