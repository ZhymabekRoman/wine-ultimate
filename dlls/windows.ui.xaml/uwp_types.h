/* WinRT Windows.UI.Xaml core UWP/WinUI types, element structs, and lifecycle APIs
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

#ifndef __WINE_WINDOWS_UI_XAML_UWP_TYPES_H
#define __WINE_WINDOWS_UI_XAML_UWP_TYPES_H

#include "private.h"
#include "weakreference.h"

typedef struct IWeakReference IWeakReference;
typedef struct IWeakReferenceVtbl IWeakReferenceVtbl;

struct uwp_thickness { double Left, Top, Right, Bottom; };
struct uwp_grid_length { double Value; int GridUnitType; };

typedef LONG (WINAPI *p_RegQueryValueExW_t)( HKEY, LPCWSTR, LPDWORD, LPDWORD, LPBYTE, LPDWORD );
typedef LONG (WINAPI *p_RegEnumKeyExW_t)( HKEY, DWORD, LPWSTR, LPDWORD, LPDWORD, LPWSTR, LPDWORD, PFILETIME );
typedef LONG (WINAPI *p_RegOpenKeyExW_t)( HKEY, LPCWSTR, DWORD, REGSAM, PHKEY );
typedef LSTATUS (WINAPI *p_RegCloseKey_t)( HKEY );

/* Forward declarations for types defined in sibling domain headers. */
struct uwp_style_setter;
struct uwp_rd_bucket;
struct uwp_dp_entry;
struct uwp_dp_cb_entry;
struct uwp_defer_entry;
struct uwp_vsm_group;
struct xaml_weak_ref;
struct cwv2_pending_op;

struct xaml_type;
struct xaml_type_vtbl;
struct xaml_member;
struct xaml_member_vtbl;
struct xaml_metadata_provider;
struct xaml_metadata_provider_vtbl;
struct xaml_event_handler;
struct xaml_event_handler_vtbl;
struct notify_property_changed;
struct notify_property_changed_vtbl;
struct property_changed_args;
struct property_changed_args_vtbl;
struct reference_int32;
struct reference_int32_vtbl;
struct command_iface;
struct command_iface_vtbl;

enum cwv2_pending_kind
{
    CWV2_OP_SETTING_BOOL,
    CWV2_OP_SETTING_UA,
    CWV2_OP_ADD_HOST_OBJECT,
    CWV2_OP_ADD_SCRIPT,
    CWV2_OP_ADD_WRS_FILTER,
    CWV2_OP_ADD_EVENT,
    CWV2_OP_ADD_EVENT2,
    CWV2_OP_ADD_EVENT11,
    CWV2_OP_DEVTOOLS,
};

enum uwp_elem_kind
{
    UWP_KIND_GENERIC = 0,
    UWP_KIND_FRAME,
    UWP_KIND_PAGE,
    UWP_KIND_USER_CONTROL,
    UWP_KIND_GRID,
    UWP_KIND_STACK_PANEL,
    UWP_KIND_BORDER,
    UWP_KIND_CONTENT_CONTROL,
    UWP_KIND_BUTTON,
    UWP_KIND_TOGGLE_BUTTON,
    UWP_KIND_TEXT_BLOCK,
    UWP_KIND_ROW_DEF,
    UWP_KIND_COL_DEF,
    UWP_KIND_SCROLL_VIEWER,
    UWP_KIND_STYLE,
    UWP_KIND_BRUSH,
    UWP_KIND_FLYOUT,
    UWP_KIND_POPUP,
    UWP_KIND_DEFERRED,
    UWP_KIND_IMAGE,
    UWP_KIND_PROGRESS_RING,
    UWP_KIND_RESOURCE_DICT,
    UWP_KIND_COLOR,
    UWP_KIND_ACCELERATOR,
    UWP_KIND_XAML_COMMAND,
    UWP_KIND_WRAP_PANEL,
};

/* Bitmask flags cached on struct uwp_element::class_flags via uwp_elem_sync_class_flags(). */
#define UWP_CF_AUTO_SUGGEST_BOX_EXACT   (1ULL << 0)
#define UWP_CF_AUTO_SUGGEST_BOX         (1ULL << 1)
#define UWP_CF_TEXT_BOX                 (1ULL << 2)
#define UWP_CF_ITEMS_REPEATER           (1ULL << 3)
#define UWP_CF_RESP_ITEMS_REPEATER_LIST (1ULL << 4)
#define UWP_CF_SCROLL_VIEWER            (1ULL << 5)
#define UWP_CF_COMBOBOX                 (1ULL << 6)
#define UWP_CF_NAV_VIEW                 (1ULL << 7)
#define UWP_CF_NAV_VIEW_ANY             (1ULL << 8)
#define UWP_CF_NAV_ITEM                 (1ULL << 9)
#define UWP_CF_NAV_HEADER               (1ULL << 10)
#define UWP_CF_NAV_SEPARATOR            (1ULL << 11)
#define UWP_CF_CARD                     (1ULL << 12)
#define UWP_CF_SHIMMER                  (1ULL << 13)
#define UWP_CF_SKELETON                 (1ULL << 14)
#define UWP_CF_LISTVIEW                 (1ULL << 17)
#define UWP_CF_GRIDVIEW                 (1ULL << 18)
#define UWP_CF_HAS_ITEM                 (1ULL << 19)
#define UWP_CF_TEACHING_TIP             (1ULL << 20)
#define UWP_CF_MENUBAR                  (1ULL << 21)
#define UWP_CF_MENUBAR_ITEM             (1ULL << 22)
#define UWP_CF_FONT_ICON                (1ULL << 23)
#define UWP_CF_SYMBOL_ICON              (1ULL << 24)
#define UWP_CF_PIVOT                    (1ULL << 25)
#define UWP_CF_PIVOT_ITEM               (1ULL << 26)
#define UWP_CF_RADIO_BUTTON             (1ULL << 27)
#define UWP_CF_TOGGLE_BUTTON            (1ULL << 28)
#define UWP_CF_RADIO_MENU_ITEM          (1ULL << 29)
#define UWP_CF_TOGGLE_MENU_ITEM         (1ULL << 30)
#define UWP_CF_MENU_FLYOUT_ITEM         (1ULL << 31)
#define UWP_CF_MENU_FLYOUT_SUB_ITEM     (1ULL << 32)
#define UWP_CF_DROP_DOWN_BUTTON         (1ULL << 33)
#define UWP_CF_INFO_BADGE               (1ULL << 34)
#define UWP_CF_FLYOUT                   (1ULL << 35)
#define UWP_CF_TABVIEW                  (1ULL << 36)
#define UWP_CF_TOOLTIP                  (1ULL << 37)
#define UWP_CF_SEPARATOR                (1ULL << 38)
#define UWP_CF_ACCELERATOR              (1ULL << 39)
#define UWP_CF_TEMPLATE                 (1ULL << 40)
#define UWP_CF_RICH_TEXT_BLOCK          (1ULL << 41)
#define UWP_CF_CONSTRAINED_BOX          (1ULL << 42)
#define UWP_CF_LISTBOX                  (1ULL << 43)
#define UWP_CF_ITEMS_CONTROL            (1ULL << 44)
#define UWP_CF_PARAGRAPH                (1ULL << 45)
#define UWP_CF_LINE_BREAK               (1ULL << 46)
#define UWP_CF_RUN                      (1ULL << 47)
#define UWP_CF_SPAN                     (1ULL << 48)
#define UWP_CF_INLINE                   (1ULL << 49)
#define UWP_CF_APP_TITLE_BAR            (1ULL << 50)
#define UWP_CF_MEDIA_PLAYER             (1ULL << 51)
#define UWP_CF_PROGRESS_BAR             (1ULL << 52)
#define UWP_CF_RECTANGLE                (1ULL << 53)
#define UWP_CF_ELLIPSE                  (1ULL << 54)
#define UWP_CF_BRUSH                    (1ULL << 55)
#define UWP_CF_FLIPVIEW                 (1ULL << 56)
#define UWP_CF_CONTENT_PRESENTER        (1ULL << 59)
#define UWP_CF_STORYBOARD               (1ULL << 60)
#define UWP_CF_WINUI_PREFIX             (1ULL << 61)
#define UWP_CF_UWP_PREFIX               (1ULL << 62)
#define UWP_CF_DATA_TEMPLATE            (1ULL << 63)

/* Secondary bitmask flags cached on struct uwp_element::class_flags2 via uwp_elem_sync_class_flags(). */
#define UWP_CF2_TEMPLATE_SELECTOR       (1ULL << 0)
#define UWP_CF2_FRAME                   (1ULL << 1)
#define UWP_CF2_NAV_TRANSITION          (1ULL << 2)
#define UWP_CF2_IMAGE                   (1ULL << 3)
#define UWP_CF2_EASE                    (1ULL << 4)
#define UWP_CF2_NAVVIEW_WINUI           (1ULL << 5)
#define UWP_CF2_NAVVIEW_ITEM_WINUI      (1ULL << 6)
#define UWP_CF2_GRID                    (1ULL << 7)
#define UWP_CF2_VISUAL_STATE            (1ULL << 8)
#define UWP_CF2_BINDING                 (1ULL << 9)
#define UWP_CF2_TEMPLATE_BINDING        (1ULL << 10)
#define UWP_CF2_NULL_EXT                (1ULL << 11)
#define UWP_CF2_GRADIENT_STOP           (1ULL << 12)
#define UWP_CF2_LINEAR_GRADIENT         (1ULL << 13)
#define UWP_CF2_FLYOUT_EXACT            (1ULL << 14)
#define UWP_CF2_MENU_FLYOUT_EXACT       (1ULL << 15)
#define UWP_CF2_CMDBAR_FLYOUT_EXACT     (1ULL << 16)
#define UWP_CF2_HYPERLINK_EXACT         (1ULL << 17)
#define UWP_CF2_NAV_VIEW_ITEM_CTRL      (1ULL << 18)
#define UWP_CF2_NAV_VIEW_CTRL           (1ULL << 19)
#define UWP_CF2_DWS_HOST                (1ULL << 20)
#define UWP_CF2_LOADED_IMAGE_SURF       (1ULL << 21)
#define UWP_CF2_COLLECTION              (1ULL << 22)
#define UWP_CF2_ANIMATION_SET           (1ULL << 23)
#define UWP_CF2_HOMEPAGE_SKEL           (1ULL << 24)
#define UWP_CF2_SEARCHPAGE_SKEL         (1ULL << 25)
#define UWP_CF2_SHIMMER_PRODUCT         (1ULL << 26)
#define UWP_CF2_DOT_CARD                (1ULL << 27)
#define UWP_CF2_XAML_UICOMMAND          (1ULL << 28)
#define UWP_CF2_FLYOUT_ITEM             (1ULL << 29)
#define UWP_CF2_CONTENT_DIALOG          (1ULL << 30)
#define UWP_CF2_DISPATCHER_TIMER        (1ULL << 31)
#define UWP_CF2_REL_SOURCE              (1ULL << 32)
#define UWP_CF2_SYMBOL_ICON_EXACT       (1ULL << 33)
#define UWP_CF2_CIRCLE_EASE             (1ULL << 34)
#define UWP_CF2_EXP_EASE                (1ULL << 35)
#define UWP_CF2_POWER_EASE              (1ULL << 36)
#define UWP_CF2_SINE_EASE               (1ULL << 37)
#define UWP_CF2_QUAD_EASE               (1ULL << 38)
#define UWP_CF2_CUBIC_EASE              (1ULL << 39)
#define UWP_CF2_QUARTIC_EASE            (1ULL << 40)
#define UWP_CF2_QUINTIC_EASE            (1ULL << 41)
#define UWP_CF2_CALENDAR_DATE_PICKER    (1ULL << 42)
#define UWP_CF2_EXPANDER                (1ULL << 43)
#define UWP_CF2_TOGGLE_SWITCH           (1ULL << 44)
#define UWP_CF2_HYPERLINK_BTN           (1ULL << 45)

enum uwp_rd_kind
{
    UWP_RD_OBJ = 0,
    UWP_RD_BRUSH,
    UWP_RD_COLOR,
    UWP_RD_DOUBLE,
    UWP_RD_THICKNESS,
    UWP_RD_STRING,
    UWP_RD_ALIAS,
    UWP_RD_STYLE,
    UWP_RD_DATA_TEMPLATE,
    UWP_RD_FONT_FAMILY,
};

struct xaml_metadata_provider
{
    const struct xaml_metadata_provider_vtbl *lpVtbl;
};

struct xaml_metadata_provider_vtbl
{
    HRESULT (WINAPI *QueryInterface)( struct xaml_metadata_provider *This, REFIID riid, void **ppv );
    ULONG (WINAPI *AddRef)( struct xaml_metadata_provider *This );
    ULONG (WINAPI *Release)( struct xaml_metadata_provider *This );
    void *pad3, *pad4, *pad5, *pad6;
    HRESULT (WINAPI *GetXamlTypeByFullName)( struct xaml_metadata_provider *This, HSTRING name,
                                             struct xaml_type **type );   /* slot 7 */
};

struct xaml_type
{
    const struct xaml_type_vtbl *lpVtbl;
};

struct xaml_type_vtbl
{
    HRESULT (WINAPI *QueryInterface)( struct xaml_type *This, REFIID riid, void **ppv );
    ULONG (WINAPI *AddRef)( struct xaml_type *This );
    ULONG (WINAPI *Release)( struct xaml_type *This );
    void *pad3, *pad4, *pad5, *pad6, *pad7;
    HRESULT (WINAPI *get_FullName)( struct xaml_type *This, HSTRING *name );                /* slot 8 */
    void *pad9, *pad10, *pad11, *pad12, *pad13, *pad14, *pad15, *pad16, *pad17;
    HRESULT (WINAPI *ActivateInstance)( struct xaml_type *This, IInspectable **instance );  /* slot 18 */
    HRESULT (WINAPI *CreateFromString)( struct xaml_type *This, HSTRING input, void **value ); /* slot 19 */
    HRESULT (WINAPI *GetMember)( struct xaml_type *This, HSTRING name, struct xaml_member **member ); /* slot 20 */
    HRESULT (WINAPI *AddToVector)( struct xaml_type *This, void *instance, void *value );   /* slot 21 */
    HRESULT (WINAPI *AddToMap)( struct xaml_type *This, void *instance, void *key, void *value ); /* slot 22 */
};

struct xaml_member
{
    const struct xaml_member_vtbl *lpVtbl;
};

struct xaml_member_vtbl
{
    HRESULT (WINAPI *QueryInterface)( struct xaml_member *This, REFIID riid, void **ppv );
    ULONG (WINAPI *AddRef)( struct xaml_member *This );
    ULONG (WINAPI *Release)( struct xaml_member *This );
    void *pad3, *pad4, *pad5;
    HRESULT (WINAPI *get_IsAttachable)( struct xaml_member *This, boolean *value );          /* slot 6 */
    void *pad7, *pad8, *pad9, *pad10;
    HRESULT (WINAPI *get_Type)( struct xaml_member *This, struct xaml_type **type );        /* slot 11 */
    HRESULT (WINAPI *GetValue)( struct xaml_member *This, void *instance, IInspectable **value ); /* slot 12 */
    HRESULT (WINAPI *SetValue)( struct xaml_member *This, void *instance, void *value );    /* slot 13 */
};

struct notify_property_changed
{
    const struct notify_property_changed_vtbl *lpVtbl;
};

struct notify_property_changed_vtbl
{
    HRESULT (WINAPI *QueryInterface)( struct notify_property_changed *This, REFIID riid, void **ppv );
    ULONG (WINAPI *AddRef)( struct notify_property_changed *This );
    ULONG (WINAPI *Release)( struct notify_property_changed *This );
    void *pad3, *pad4, *pad5;
    HRESULT (WINAPI *add_PropertyChanged)( struct notify_property_changed *This, void *handler,
                                           EventRegistrationToken *token );                 /* slot 6 */
    HRESULT (WINAPI *remove_PropertyChanged)( struct notify_property_changed *This,
                                              EventRegistrationToken token );               /* slot 7 */
};

struct property_changed_args
{
    const struct property_changed_args_vtbl *lpVtbl;
};

struct property_changed_args_vtbl
{
    HRESULT (WINAPI *QueryInterface)( struct property_changed_args *This, REFIID riid, void **ppv );
    ULONG (WINAPI *AddRef)( struct property_changed_args *This );
    ULONG (WINAPI *Release)( struct property_changed_args *This );
    void *pad3, *pad4, *pad5;
    HRESULT (WINAPI *get_PropertyName)( struct property_changed_args *This, HSTRING *name ); /* slot 6 */
};

struct reference_int32
{
    const struct reference_int32_vtbl *lpVtbl;
};

struct reference_int32_vtbl
{
    HRESULT (WINAPI *QueryInterface)( struct reference_int32 *This, REFIID riid, void **ppv );
    ULONG (WINAPI *AddRef)( struct reference_int32 *This );
    ULONG (WINAPI *Release)( struct reference_int32 *This );
    void *pad3, *pad4, *pad5;
    HRESULT (WINAPI *get_Value)( struct reference_int32 *This, INT32 *value );               /* slot 6 */
};

struct command_iface
{
    const struct command_iface_vtbl *lpVtbl;
};

struct command_iface_vtbl
{
    HRESULT (WINAPI *QueryInterface)( struct command_iface *This, REFIID riid, void **ppv );
    ULONG (WINAPI *AddRef)( struct command_iface *This );
    ULONG (WINAPI *Release)( struct command_iface *This );
    void *pad3, *pad4, *pad5, *pad6, *pad7, *pad8;
    HRESULT (WINAPI *Execute)( struct command_iface *This, void *parameter );                /* slot 9 */
};

/* Generic event delegate: { QueryInterface, AddRef, Release, Invoke(sender, args) }. */
struct xaml_event_handler
{
    const struct xaml_event_handler_vtbl *lpVtbl;
};

struct xaml_event_handler_vtbl
{
    HRESULT (WINAPI *QueryInterface)( struct xaml_event_handler *This, REFIID riid, void **ppv );
    ULONG (WINAPI *AddRef)( struct xaml_event_handler *This );
    ULONG (WINAPI *Release)( struct xaml_event_handler *This );
    HRESULT (WINAPI *Invoke)( struct xaml_event_handler *This, void *sender, void *args );   /* slot 3 */
};

struct uwp_weak_ctrl
{
    LONG ref;
    struct uwp_element *elem;
};

/* Lazy event-registry sidecar allocated on first add_* subscription. */
struct uwp_event_bag
{
    struct xaml_event_registry xaml_root_changed_reg;
    struct xaml_event_registry tbtn_checked_reg;
    struct xaml_event_registry tbtn_unchecked_reg;
    struct xaml_event_registry popup_opened_reg;
    struct xaml_event_registry popup_closed_reg;
    struct xaml_event_registry sel_changed_reg;
    struct xaml_event_registry cb_opened_reg;
    struct xaml_event_registry cb_closed_reg;
    struct xaml_event_registry ptr_pressed_reg;
    struct xaml_event_registry ptr_moved_reg;
    struct xaml_event_registry ptr_released_reg;
    struct xaml_event_registry ptr_entered_reg;
    struct xaml_event_registry ptr_exited_reg;
    struct xaml_event_registry ptr_capture_lost_reg;
    struct xaml_event_registry tapped_reg;
    struct xaml_event_registry ptr_canceled_reg;
    struct xaml_event_registry ptr_wheel_reg;
    struct xaml_event_registry got_focus_reg;
    struct xaml_event_registry lost_focus_reg;
    struct xaml_event_registry nav_sel_changed_reg;
    struct xaml_event_registry nav_item_invoked_reg;
    struct xaml_event_registry frame_navigated_reg;
    struct xaml_event_registry preview_keydown_reg;
    struct xaml_event_registry keydown_reg;
    struct xaml_event_registry keyup_reg;
    struct xaml_event_registry asb_text_changed_reg;
    struct xaml_event_registry asb_query_submitted_reg;
    struct xaml_event_registry asb_suggestion_chosen_reg;
    struct xaml_event_registry obsvec_changed_reg;
    struct xaml_event_registry timer_tick_reg;
    struct xaml_event_registry img_opened_reg;
    struct xaml_event_registry loaded_reg;
    INT64 loaded_fired_tok;
    struct xaml_event_registry evc_reg;
    struct xaml_event_registry size_changed_reg;
    struct xaml_event_registry layout_updated_reg;
    struct xaml_event_registry click_reg;
    struct xaml_event_registry dc_reg;
    struct xaml_event_registry loading_reg;
    struct xaml_event_registry tab_add_reg;
    struct xaml_event_registry tab_close_reg;
    struct xaml_event_registry accel_invoked_reg;
    struct xaml_event_registry exec_req_reg;
    struct xaml_event_registry tip_closing_reg;
    struct xaml_event_registry tip_closed_reg;
    struct xaml_event_registry flyout_opening_reg;
    struct xaml_event_registry flyout_closed_reg;
    struct xaml_event_registry cdp_date_changed_reg;
    struct xaml_event_registry pivot_item_loading_reg;
    struct xaml_event_registry pivot_item_loaded_reg;
    struct xaml_event_registry sv_pane_closing_reg;
    struct xaml_event_registry sv_pane_closed_reg;
    struct xaml_event_registry sv_pane_opening_reg;
    struct xaml_event_registry sv_pane_opened_reg;
    struct xaml_event_registry actual_theme_reg;
};

/* Lazy sidecar for Image / BitmapImage / MediaPlayerElement state. */
struct uwp_media_ext
{
    WCHAR image_src[512];
    IInspectable *img_source;
    BYTE *img_pixels;
    UINT img_w, img_h;
    BOOL img_loaded;
    volatile LONG img_loading;
    IInspectable *media_source;
    IInspectable *media_transport_controls;
    IInspectable *media_poster_source;
    IInspectable *media_player;
    boolean media_transport_enabled;
    int media_stretch;
    boolean media_auto_play;
    boolean media_is_full_window;
    SRWLOCK video_lock;
    BYTE *video_pixels;
    UINT video_w, video_h;
    UINT video_frame_seq;
    WCHAR video_uri[512];
    HANDLE video_thread;
    HANDLE video_wake_event;
    volatile LONG video_started;
    volatile LONG video_stop;
};

/* Lazy sidecar for ContentDialog state. */
struct uwp_dialog_ext
{
    HWND dialog_hwnd;
    void *dialog_op;
    IInspectable *dialog_title_obj;
    WCHAR dialog_title[128];
    WCHAR dialog_primary_text[64];
    WCHAR dialog_secondary_text[64];
    WCHAR dialog_close_text[64];
    int dialog_default_btn;
};

/* Lazy sidecar for NavigationView / Frame / TabView / ComboBox extras. */
struct uwp_nav_ext
{
    WCHAR display_member_path[64];
    IInspectable *selected_item;
    INT32 selected_index;
    IInspectable *sel_changed_handler;
    IInspectable *tab_items_source;
    IInspectable *tab_add_cmd;
    IInspectable *tab_add_cmd_param;
    IInspectable *back_stack;
    IInspectable *forward_stack;
    IInspectable *nav_args_item;
    BOOL nav_fired;
    boolean nav_stack_enabled;
    int nav_pane_display_mode;       /* 0=Auto, 1=Left, 2=Top, 3=LeftCompact, 4=LeftMinimal */
    BOOL nav_pane_display_mode_set;
    int nav_display_mode;            /* 0=Minimal, 1=Compact, 2=Expanded */
    BOOL nav_display_mode_set;
    BOOL nav_is_pane_open;
    double nav_open_pane_length;
    double nav_compact_pane_length;
    double nav_compact_threshold;
    double nav_expanded_threshold;
    BOOL nav_is_settings_visible;
    BOOL nav_is_pane_toggle_visible;
    int nav_scroll_y;
    RECT nav_pane_bounds;
    IInspectable *nav_icon;
    INT64 cdp_date_ticks;
    BOOL cdp_has_date;
    INT64 cdp_min_ticks;
    INT64 cdp_max_ticks;
    int cdp_view_year;
    int cdp_view_month;
    WCHAR cdp_placeholder[64];
    WCHAR cdp_header[128];
    WCHAR cdp_date_format[64];
    int tab_hovered_idx;
    BOOL tab_hovered_close;
    int tab_close_overlay_mode;
};

/* Lazy sidecar for Style / Brush / Gradient / Storyboard / Animation / TemplateBinding state. */
struct uwp_style_anim_ext
{
    int easing_mode;
    UINT32 brush_argb;
    BOOL has_brush_argb;
    double brush_opacity;
    WCHAR brush_color_ref[96];
    WCHAR style_based_on[96];
    struct uwp_style_setter *style_setters;
    int style_setter_cnt;
    int style_setter_cap;
    IInspectable *style_obj;
    IInspectable *gradient_stops_coll;
    IInspectable *figures_coll;
    float grad_start_x, grad_start_y;
    float grad_end_x, grad_end_y;
    int grad_spread_method;
    int grad_mapping_mode;
    int grad_color_interp;
    double grad_stop_offset;
    int tile_align_x, tile_align_y;
    int fill_rule;
    BOOL xcbb_connected;
    struct uwp_tpl_binding_entry
    {
        WORD target_pid;
        WCHAR src_prop[64];
        WCHAR target_prop[64];
    } *tpl_bindings;
    int tpl_bind_cnt;
    int tpl_bind_cap;
    double grid_col_spacing;
    double grid_row_spacing;
    IInspectable *template_root_obj;
    WCHAR anim_target_name[96];
    WCHAR anim_target_prop[96];
    struct uwp_element *anim_target_el;
    double anim_to;
    BOOL anim_has_to;
    WCHAR bg_theme_key[96];
    WCHAR fg_theme_key[96];
    WCHAR border_theme_key[96];
    double scale_x;
    double scale_y;
    double center_x;
    double center_y;
    IInspectable *converter;
    IInspectable *converter_param;
    WCHAR converter_lang[32];
    IInspectable *cmd_can_exec_handler;
    EventRegistrationToken cmd_can_exec_tok;
    IInspectable *obsvec_handler_obj;
    IInspectable *obsvec_source_obj;
    EventRegistrationToken obsvec_tok;
};

struct uwp_element
{
    IInspectable nondelegating_iface;
    IInspectable IInspectable_iface;
    IInspectable IDependencyObject_iface;
    IInspectable IDependencyObject2_iface;
    IInspectable IUIElement_iface;
    IInspectable IFrameworkElement_iface;
    IInspectable IFrameworkElement2_iface;
    IInspectable IFrameworkElement3_iface;
    IInspectable IFrameworkElement7_iface;
    IInspectable IControl_iface;
    IInspectable IControlProtected_iface;
    IInspectable IContentControl_iface;
    IInspectable IUserControl_iface;
    IInspectable IPage_iface;
    IInspectable IFrame_iface;
    IInspectable IFrame2_iface;
    IInspectable IFrame3_iface;
    IInspectable IFrame5_iface;
    IInspectable IButtonBase_iface;
    IInspectable IToggleButton_iface;
    IInspectable IPanel_iface;
    IInspectable IGrid_iface;
    IInspectable IGrid3_iface;
    IInspectable IBorder_iface;
    IInspectable ITextBlock_iface;
    IInspectable IParagraph_iface;
    IInspectable IRun_iface;
    IInspectable IBinding_iface;
    IInspectable IPropertyPath_iface;
    IInspectable IRowDefinition_iface;
    IInspectable IColumnDefinition_iface;
    IInspectable IStyle_iface;
    IInspectable IDataTemplate_iface;
    IInspectable IDataTemplateSelector_iface;
    IInspectable IDataTemplateSelector2_iface;
    IInspectable IBrush_iface;
    IInspectable ISolidColorBrush_iface;
    IInspectable IFlyout_iface;
    IInspectable ISelector_iface;
    IInspectable IComboBox_iface;
    IInspectable IItemsControl_iface;
    IInspectable IStateTrigger_iface;
    IInspectable IStateTriggerPub_iface;
    boolean state_trigger_active;
    UINT32 local_prop_mask; /* UWP_LP_* bits: local XBF/property values take precedence over Style setters */
    IInspectable IObservableVector_iface;
    IInspectable IVector_iface;
    IInspectable IIterable_iface;
    IInspectable IVectorView_iface;
    IInspectable IScrollViewer_iface;
    IInspectable IDesktopWindowXamlSourceNative_iface;
    IInspectable IUIElement7_iface;
    IInspectable IKeyboardAccelerator_iface;
    IInspectable IXamlUICommand_iface;
    IInspectable ICommand_iface;
    IInspectable IMenuFlyoutItem_iface;
    IInspectable IMenuFlyoutItem3_iface;
    IInspectable IToggleMenuFlyoutItem_iface;
    IInspectable IMenuFlyoutSubItem_iface;
    IInspectable IMenuFlyout2_iface;
    IInspectable IProgressRing_iface;
    IInspectable IUIElement4_iface;
    IInspectable IUIElement5_iface;
    IInspectable IUIElement10_iface;
    IInspectable IXamlRoot_iface;
    IInspectable *xaml_root_shadow;
    IInspectable *xaml_root_override;
    IInspectable IButtonWithFlyout_iface;
    IInspectable IContentDialog_iface;
    IInspectable IContentDialog2_iface;
    IInspectable ISwapChainPanel_iface;
    IInspectable ISwapChainPanelNative2_iface;
    IInspectable ITeachingTip_iface;
    IInspectable IAutoSuggestBox_iface;
    IInspectable IAutoSuggestBox2_iface;
    IInspectable ISplitView_iface;
    IInspectable ISplitView2_iface;
    IInspectable IEasingFunctionBase_iface;
    IInspectable IMediaPlayerElement_iface;
    IInspectable IBitmapImage_iface;
    IInspectable IBitmapSource_iface;
    IInspectable IImage_iface;
    IInspectable IShape_iface;
    IInspectable IXamlCompositionBrushBaseProtected_iface;
    IInspectable ILoadedImageSurface_iface;
    IInspectable IFontFamily_iface;
    IInspectable IFontIcon_iface;
    IInspectable IImageBrush_iface;
    IInspectable ITileBrush_iface;
    IInspectable IGradientBrush_iface;
    IInspectable ILinearGradientBrush_iface;
    IInspectable IGradientStop_iface;
    IInspectable IPathGeometry_iface;
    IInspectable IPathFigure_iface;
    IInspectable IRichTextBlock_iface;
    IInspectable ITextBlock2_iface;
    IInspectable ITextBlock6_iface;
    IInspectable IPopup_iface;
    IInspectable IPopup2_iface;
    IInspectable IPopup3_iface;
    IInspectable IStoryboard_iface;
    IWeakReferenceSource IWeakReferenceSource_iface;
    IDispatch IDispatch_iface;
    IInspectable IReferenceTrackerTarget_iface;
    ITypeInfo ITypeInfo_iface;
    UINT32 magic;
    LONG ref;
    struct uwp_weak_ctrl *weak_ctrl;
    IDCompositionVisual2 *scp_visual;
    HWND island_hwnd;
    IInspectable *outer;
    BOOL delegating;             /* outer is a real app-side element (answers IUIElement): refcounts delegate to it */
    BOOL outer_pinned;           /* visual tree holds an explicit AddRef on outer to prevent GC while live */
    BOOL in_visual_tree;         /* attached to a live visual tree / window / island / named table */
    enum uwp_elem_kind kind;
    UINT64 class_flags;
    UINT64 class_flags2;
    WCHAR class_name[128];
    WCHAR name[64];
    WCHAR *text;
    UINT32 text_len;
    UINT32 text_cap;
    /* Cold heap-allocated strings (NULL when empty; access via inline getters/setters) */
    WCHAR *x_class;
    WCHAR *uid;
    WCHAR *accel_text;
    WCHAR *font_family;
    WCHAR *automation_name;
    WCHAR *default_style_key_name;
    WCHAR *placeholder;
    /* Lazy on-demand heap sidecars */
    struct uwp_event_bag *events;
    struct uwp_media_ext *media;
    struct uwp_dialog_ext *dialog;
    struct uwp_nav_ext *nav;
    struct uwp_style_anim_ext *style_anim;
    int conn_id;
    UINT16 probe_flags;          /* UWP_PF_* negative-probe cache bits (cleared on DataContext/Content change) */
    struct uwp_element *parent;
    struct uwp_element *owner_component;
    IInspectable *content;       /* ContentControl/UserControl/Page/Border/ScrollViewer child */
    IInspectable *pane;          /* SplitView.Pane */
    IInspectable *context_flyout; /* UIElement.ContextFlyout */
    IInspectable *flyout;         /* Button.Flyout */
    IInspectable *children_coll;
    IInspectable *inlines_coll; /* Panel.Children */
    IInspectable *accelerators_coll; /* UIElement.KeyboardAccelerators */
    IInspectable *row_defs_coll; /* Grid.RowDefinitions */
    IInspectable *col_defs_coll; /* Grid.ColumnDefinitions */
    IInspectable *resources_rd;  /* FrameworkElement.Resources */
    IInspectable *data_context;
    struct {
        void *dp;
        struct uwp_element *binding;
    } *dc_bound_dps;
    int dc_bound_dp_cnt;
    int dc_bound_dp_cap;
    struct {
        WCHAR owner_cls[128];
        WCHAR prop_name[64];
        WCHAR path[128];
        IInspectable *source;
    } *xaml_bindings;
    int xaml_binding_cnt;
    int xaml_binding_cap;
    IInspectable *command;
    IInspectable *command_param;
    IInspectable *tag;
    IDCompositionVisual *visual;
    /* Layout properties */
    int grid_row;
    int grid_col;
    int grid_row_span;
    int grid_col_span;
    int grid_len_type; /* 0=Auto, 1=Pixel, 2=Star */
    double grid_len_val;
    int orientation;   /* 0=Vertical, 1=Horizontal */
    int visibility;    /* 0=Visible, 1=Collapsed */
    boolean is_enabled;
    int is_checked;    /* 0=Unchecked, 1=Checked, 2=Indeterminate */
    double popup_h_offset;
    double popup_v_offset;
    BOOL popup_light_dismiss;
    BOOL is_pressed;
    BOOL is_hovered;
    BOOL is_hit_test_visible;
    double opacity;
    double width, height;
    double min_width, max_width;
    double min_height, max_height;
    double actual_w, actual_h;
    double last_sc_w, last_sc_h;
    double margin_l, margin_t, margin_r, margin_b;
    double padding_l, padding_t, padding_r, padding_b;
    int h_align;       /* 0=Left, 1=Center, 2=Right, 3=Stretch */
    int v_align;       /* 0=Top, 1=Center, 2=Bottom, 3=Stretch */
    int h_content_align; /* 0=Left, 1=Center, 2=Right, 3=Stretch */
    int v_content_align; /* 0=Top, 1=Center, 2=Bottom, 3=Stretch */
    BOOL has_h_content_align;
    BOOL has_v_content_align;
    double range_val, range_min, range_max;
    double spacing;    /* StackPanel.Spacing */
    double font_size;
    BOOL has_font_size;
    int font_weight;   /* 0=default, else Win32 FW_* (100..900) */
    int requested_theme; /* 0=Default, 1=Light, 2=Dark */
    IInspectable *default_style_key;
    int text_align;    /* 0=Center, 1=Left, 2=Right, 3=Justify */
    BOOL has_text_align;
    int text_wrapping; /* 1=NoWrap, 2=Wrap, 3=WrapWholeWords */
    int max_lines;     /* 0=unlimited */
    COLORREF bg_color;
    UINT32 bg_argb;
    BYTE bg_alpha;
    BYTE flow_direction; /* 0=unset, 1=LeftToRight, 2=RightToLeft */
    BYTE is_active;
    BYTE binding_mode;   /* 0=unset, 1=OneWay, 2=OneTime, 3=TwoWay */
    BYTE has_bg;
    BYTE has_fg;
    BYTE has_border;
    BYTE obsvec_hooked;
    COLORREF fg_color;
    UINT32 fg_argb;
    BYTE fg_alpha;
    BYTE is_selected;
    BYTE loaded_fired;
    BYTE evc_fired;
    COLORREF border_color;
    UINT32 border_argb;
    BYTE border_alpha;
    BYTE has_bounds;
    BYTE template_applied;
    BYTE is_dropdown_open;
    double border_thick_l, border_thick_t, border_thick_r, border_thick_b;
    double corner_radius;
    BOOL is_accent_style;
    enum {
        UWP_CONTENT_NONE = 0,
        UWP_CONTENT_TEXT,
        UWP_CONTENT_ELEMENT,
    } content_kind;
    struct uwp_rd_bucket *rd_bucket;
    /* Computed screen rectangle */
    RECT bounds;
    /* Lifecycle & interaction flags */
    void *binding_connector;
    void *dt_binding_connector;
    int hovered_dropdown_idx;
    int dropdown_scroll_offset;
    IInspectable *items_source;
    IInspectable *item_template;
    IInspectable *item_template_selector;
    SRWLOCK dp_lock;
    struct uwp_dp_entry *dp_store;
    int dp_cnt;
    int dp_cap;
    struct uwp_dp_cb_entry *dp_cbs;
    int dp_cb_cnt;
    int dp_cb_cap;
    INT64 dp_cb_next_token;
    /* Named children & deferred substreams for FindName */
    struct uwp_element **named_elems;
    int named_cnt;
    int named_cap;
    struct uwp_defer_entry *defer_entries;
    int defer_cnt;
    int defer_cap;
    struct uwp_xbf_ref
    {
        WCHAR full_name[128];
        WCHAR short_name[128];
        BOOL is_resource_path;
        BOOL instantiated;
    } *xbf_refs;
    int xbf_ref_cnt;
    int xbf_ref_cap;
    BOOL xbf_parsed;
    struct uwp_vsm_group *vsm_groups;
    int vsm_group_cnt;
    int vsm_group_cap;
    double vsm_eval_width;
    double vsm_eval_height;
    BOOL vsm_eval_valid;
    IInspectable *bg_brush;
    IInspectable *fill_brush;
    IInspectable *comp_brush;
    IInspectable *ecp_visual;
    struct uwp_element *next_reg;
    IInspectable *provider_inst;
    void *outer_unk;
    UINT accel_key;
    UINT accel_mods;
    BOOL accel_enabled;
    IInspectable *accel_scope_owner;
    struct uwp_element *accel_target;
    BOOL is_open;
    const BYTE *template_xbf_data;
    DWORD template_xbf_size;
    DWORD template_sub_idx;
    IInspectable *template_owner_comp;
    struct uwp_rd_bucket *template_rd_bucket;
    struct uwp_element *template_root;
    struct uwp_element *templated_parent;
    BOOL template_inflated;
    INT64 timer_interval_100ns;
    UINT_PTR timer_win32_id;
    BOOL timer_enabled;
    float last_measured_w;
    float last_measured_h;
    BOOL in_measure_arrange;
    int scroll_y;
    int scroll_extent_h;
    /* Layout memoization (keyed on g_layout_gen) */
    UINT32 nat_w_gen;
    double cached_nat_w;
    UINT32 nat_h_gen;
    int cached_nat_h_avail_w;
    double cached_nat_h;
    UINT32 auto_h_gen;
    double cached_auto_h;
    UINT32 content_flags_gen;
    BYTE cached_content_flags;
    /* Tri-state cache for QI(outer, IID_IControlOverrides_UWP): 0=unknown, 1=yes, -1=no */
    signed char has_ctrl_overrides;
};

#define UWP_ELEM_MAGIC 0x55575045u

struct xaml_frame_prop
{
    void *dp;
    IInspectable *value;
};

struct xaml_frame
{
    IInspectable IInspectable_iface;
    const void **IDependencyObject_iface;
    LONG ref;
    IInspectable *outer;
    IInspectable *content;
    IDCompositionVisual *visual;
    struct xaml_frame_prop props[16];
    int prop_count;
};

struct xaml_element
{
    IInspectable IInspectable_iface;
    IInspectable IGrid_iface;
    IInspectable IPanel_iface;
    IInspectable IContentControl_iface;
    IInspectable IRowDefinition_iface;
    IInspectable IColumnDefinition_iface;
    IInspectable IWebView2_iface;
    IInspectable IWebView22_iface;
    IInspectable IUIElement_iface;
    IInspectable IFrameworkElement_iface;
    IInspectable ITextBlock_iface;
    IInspectable IDependencyObject_iface;
    IUnknown IWeakReferenceSource_iface;
    struct xaml_weak_ref *weak_ref;
    struct { void *dp; IInspectable *val; } *dp_store;
    int dp_count;
    int dp_cap;
    LONG ref;
    int visibility;
    IInspectable *outer;
    IDCompositionVisual *visual;
    IInspectable *row_defs;
    IInspectable *col_defs;
    IInspectable *children;
    IInspectable *content;
    IInspectable *core_webview2;
    IInspectable *init_handler;
    IInspectable *wv2_source;
    HSTRING wv2_source_uri;
    void *loaded_fn;    /* ABI RoutedEventHandler Invoke fn (vtbl[3]) */
    BOOL init_triggered;
    BOOL browser_queued;
    BOOL browser_started;
    BOOL navigated;
    BOOL real_loader;
    BOOL visual_attached;
    BOOL has_comp_ctrl;
    void *loader_env;
    void *loader_controller;
    void *loader_comp_ctrl;
    void *loader_core;
    BOOL is_webview;
    HWND parent_hwnd;
    unsigned int qi_count;
    const WCHAR *class_name;
};

struct xaml_async_action
{
    IAsyncAction IAsyncAction_iface;
    IAsyncInfo IAsyncInfo_iface;
    LONG ref;
    BOOL completed;
    HRESULT hr;
    IAsyncActionCompletedHandler *handler;
};

struct core_webview2
{
    IInspectable IInspectable_iface;
    IInspectable ICoreWebView2_2_iface;
    IInspectable ICoreWebView2_3_iface;
    IInspectable ICoreWebView2_11_iface;
    IInspectable ICoreWebView2_13_iface;
    IInspectable ICoreWebView2_19_iface;
    IInspectable ICoreWebView2_stub_iface;
    LONG ref;
    INT32 memory_usage_target_level;
    HSTRING source_uri;
    BOOL real_loader;
    void *loader_core;         /* COM ICoreWebView2* */
    void *loader_core2;        /* COM ICoreWebView2_2* */
    void *loader_core11;       /* COM ICoreWebView2_11* */
    void *loader_core13;       /* COM ICoreWebView2_13* */
    void *tracker_source;      /* webview2loader webview object */
    struct xaml_element *owner;
    struct cwv2_pending_op *pending_head;
    struct cwv2_pending_op *pending_tail;
};

typedef struct ICoreWebView2Controller ICoreWebView2Controller;
typedef struct ICoreWebView2CompositionController ICoreWebView2CompositionController;
typedef struct ICoreWebView2ControllerVtbl
{
    HRESULT (WINAPI *QueryInterface)( ICoreWebView2Controller *This, REFIID riid, void **ppvObject );
    ULONG (WINAPI *AddRef)( ICoreWebView2Controller *This );
    ULONG (WINAPI *Release)( ICoreWebView2Controller *This );
    HRESULT (WINAPI *get_IsVisible)( ICoreWebView2Controller *This, BOOL *isVisible );
    HRESULT (WINAPI *put_IsVisible)( ICoreWebView2Controller *This, BOOL isVisible );
    HRESULT (WINAPI *get_Bounds)( ICoreWebView2Controller *This, RECT *bounds );
    HRESULT (WINAPI *put_Bounds)( ICoreWebView2Controller *This, RECT bounds );
    HRESULT (WINAPI *get_ZoomFactor)( ICoreWebView2Controller *This, double *zoomFactor );
    HRESULT (WINAPI *put_ZoomFactor)( ICoreWebView2Controller *This, double zoomFactor );
    void *add_ZoomFactorChanged;
    void *remove_ZoomFactorChanged;
    HRESULT (WINAPI *SetBoundsAndZoomFactor)( ICoreWebView2Controller *This, RECT bounds, double zoomFactor );
    HRESULT (WINAPI *MoveFocus)( ICoreWebView2Controller *This, int reason );
    void *add_MoveFocusRequested;
    void *remove_MoveFocusRequested;
    void *add_GotFocus;
    void *remove_GotFocus;
    void *add_LostFocus;
    void *remove_LostFocus;
    void *add_AcceleratorKeyPressed;
    void *remove_AcceleratorKeyPressed;
    HRESULT (WINAPI *get_ParentWindow)( ICoreWebView2Controller *This, HWND *parentWindow );
    HRESULT (WINAPI *put_ParentWindow)( ICoreWebView2Controller *This, HWND parentWindow );
    HRESULT (WINAPI *NotifyParentWindowPositionChanged)( ICoreWebView2Controller *This );
    HRESULT (WINAPI *Close)( ICoreWebView2Controller *This );
    HRESULT (WINAPI *get_CoreWebView2)( ICoreWebView2Controller *This, void **coreWebView2 );

} ICoreWebView2ControllerVtbl;

typedef struct ICoreWebView2CompositionControllerVtbl
{
    HRESULT (WINAPI *QueryInterface)( ICoreWebView2CompositionController *This, REFIID riid, void **ppvObject );
    ULONG (WINAPI *AddRef)( ICoreWebView2CompositionController *This );
    ULONG (WINAPI *Release)( ICoreWebView2CompositionController *This );
    HRESULT (WINAPI *get_RootVisualTarget)( ICoreWebView2CompositionController *This, IUnknown **target );
    HRESULT (WINAPI *put_RootVisualTarget)( ICoreWebView2CompositionController *This, IUnknown *target );
    HRESULT (WINAPI *SendMouseInput)( ICoreWebView2CompositionController *This, UINT32 eventKind,
                                      UINT32 virtualKeys, UINT32 mouseData, POINT point );
    HRESULT (WINAPI *SendPointerInput)( ICoreWebView2CompositionController *This, UINT32 eventKind, void *pointerInfo );
    HRESULT (WINAPI *get_Cursor)( ICoreWebView2CompositionController *This, HCURSOR *cursor );
    HRESULT (WINAPI *get_SystemCursorId)( ICoreWebView2CompositionController *This, UINT32 *systemCursorId );
    void *add_CursorChanged;
    void *remove_CursorChanged;
} ICoreWebView2CompositionControllerVtbl;

struct ICoreWebView2Controller
{
    const ICoreWebView2ControllerVtbl *lpVtbl;
};

struct ICoreWebView2CompositionController
{
    const ICoreWebView2CompositionControllerVtbl *lpVtbl;
};

static inline void uwp_set_template_owner( IInspectable **dst, IInspectable *src )
{
    if (*dst == src) return;
    if (src) IInspectable_AddRef( src );
    if (*dst) IInspectable_Release( *dst );
    *dst = src;
}

/* Global element registry */
extern int g_all_uwp_elem_cnt;
extern struct uwp_element *g_all_uwp_elems_head;
extern SRWLOCK g_all_uwp_elems_lock;
extern struct uwp_element *g_last_created_page;

/* Boxing / unboxing / reflection helpers */
IPropertyValueStatics *uwp_get_property_value_statics( void );
IInspectable *box_bool_value( boolean b );
IInspectable *box_double_value( double d );
IInspectable *box_int32_value( INT32 v );
IInspectable *box_string_value( const WCHAR *str );
BOOL uwp_unbox_int32( void *obj, INT32 *out_val );
BOOL uwp_unbox_bool( void *obj, boolean *out_val );
BOOL uwp_unbox_double( void *obj, double *out_val );
BOOL uwp_unbox_enum_int32( IInspectable *obj, INT32 *out_val, BOOL allow_plain_int );
BOOL uwp_replace_iface( IInspectable **slot, IInspectable *val );
BOOL uwp_unbox_string( void *obj, WCHAR *buf, int buf_chars );
BOOL uwp_inspectable_to_string( IInspectable *obj, WCHAR *buf, int buf_cch );
void uwp_extract_uri_string( IInspectable *uri_obj, WCHAR *out, size_t cap );
BOOL uwp_get_custom_xaml_member( void *app_outer, const WCHAR *owner_type_name,
                                 const WCHAR *prop_name, void *target_obj, IInspectable **out_val );
void uwp_set_custom_xaml_member( void *app_outer, const WCHAR *owner_type_name,
                                 const WCHAR *prop_name, void *target_obj, const WCHAR *str_val );
BOOL uwp_set_custom_xaml_member_obj( void *app_outer, const WCHAR *owner_type_name,
                                     const WCHAR *prop_name, void *target_obj, void *val_obj );
BOOL uwp_add_custom_xaml_collection_item( void *app_outer, const WCHAR *owner_type_name,
                                          const WCHAR *prop_name, void *target_obj, void *item_obj );

/* Core element creation, classification, sub-interface & tree lifecycle */
enum uwp_elem_kind classify_uwp_class( const WCHAR *cls );
void uwp_elem_sync_class_flags( struct uwp_element *el );
void uwp_elem_set_text( struct uwp_element *el, const WCHAR *s );
void uwp_elem_set_text_n( struct uwp_element *el, const WCHAR *s, size_t n );
void uwp_elem_append_text( struct uwp_element *el, const WCHAR *s );
void uwp_elem_clear_text( struct uwp_element *el );
void uwp_elem_set_cold_str( WCHAR **dst, const WCHAR *src, size_t max_chars );

/* Lazy sidecar allocators (thread-safe via InterlockedCompareExchangePointer) */
struct uwp_event_bag *uwp_elem_ensure_events( struct uwp_element *el );
struct uwp_media_ext *uwp_elem_ensure_media( struct uwp_element *el );
struct uwp_dialog_ext *uwp_elem_ensure_dialog( struct uwp_element *el );
struct uwp_nav_ext *uwp_elem_ensure_nav( struct uwp_element *el );
struct uwp_style_anim_ext *uwp_elem_ensure_style_anim( struct uwp_element *el );

/* Shared event-registry firing helper */
void uwp_fire_event_registry( struct xaml_event_registry *reg, IInspectable *sender,
                              IInspectable *args, const char *tag );
void uwp_clear_global_elem_refs( struct uwp_element *el );

#define UWP_EV_REG(el, field) (((el) && (el)->events) ? &(el)->events->field : NULL)
#define UWP_EV_CNT(el, field) (((el) && (el)->events) ? (el)->events->field.count : 0)
#define UWP_EV_ANY(el, field) (((el) && (el)->events) ? ((el)->events->field.next_token > 0) : FALSE)

static inline BOOL uwp_loaded_needs_fire( const struct uwp_element *el )
{
    if (!el) return FALSE;
    if (!el->loaded_fired) return TRUE;
    return el->events && el->events->loaded_reg.count > 0 &&
           el->events->loaded_reg.next_token > el->events->loaded_fired_tok;
}

static inline const WCHAR *uwp_elem_text( const struct uwp_element *el )
{
    return (el && el->text) ? el->text : L"";
}

static inline BOOL uwp_has_text( const struct uwp_element *el )
{
    return el && el->text && el->text[0] != 0;
}

static inline const WCHAR *uwp_elem_x_class( const struct uwp_element *el )
{
    return (el && el->x_class) ? el->x_class : L"";
}
static inline void uwp_elem_set_x_class( struct uwp_element *el, const WCHAR *s )
{
    if (el) uwp_elem_set_cold_str( &el->x_class, s, 128 );
}

static inline const WCHAR *uwp_elem_uid( const struct uwp_element *el )
{
    return (el && el->uid) ? el->uid : L"";
}
static inline void uwp_elem_set_uid( struct uwp_element *el, const WCHAR *s )
{
    if (el) uwp_elem_set_cold_str( &el->uid, s, 64 );
}

static inline const WCHAR *uwp_elem_accel_text( const struct uwp_element *el )
{
    return (el && el->accel_text) ? el->accel_text : L"";
}
static inline void uwp_elem_set_accel_text( struct uwp_element *el, const WCHAR *s )
{
    if (el) uwp_elem_set_cold_str( &el->accel_text, s, 64 );
}

static inline const WCHAR *uwp_elem_font_family( const struct uwp_element *el )
{
    return (el && el->font_family) ? el->font_family : L"";
}
static inline void uwp_elem_set_font_family( struct uwp_element *el, const WCHAR *s )
{
    if (el) uwp_elem_set_cold_str( &el->font_family, s, 128 );
}

static inline const WCHAR *uwp_elem_automation_name( const struct uwp_element *el )
{
    return (el && el->automation_name) ? el->automation_name : L"";
}
static inline void uwp_elem_set_automation_name( struct uwp_element *el, const WCHAR *s )
{
    if (el) uwp_elem_set_cold_str( &el->automation_name, s, 128 );
}

static inline const WCHAR *uwp_elem_default_style_key_name( const struct uwp_element *el )
{
    return (el && el->default_style_key_name) ? el->default_style_key_name : L"";
}
static inline void uwp_elem_set_default_style_key_name( struct uwp_element *el, const WCHAR *s )
{
    if (el) uwp_elem_set_cold_str( &el->default_style_key_name, s, 128 );
}

#define UWP_PF_NO_IMG           0x0001u
#define UWP_PF_NO_BTN_LBL       0x0002u
#define UWP_PF_NO_BTN_IMG       0x0004u
#define UWP_PF_NO_CARD          0x0008u
#define UWP_PF_NO_VM            0x0010u

/* Local-value precedence bits stored in struct uwp_element::local_prop_mask */
#define UWP_LP_WIDTH            (1u << 0)
#define UWP_LP_HEIGHT           (1u << 1)
#define UWP_LP_MARGIN           (1u << 2)
#define UWP_LP_PADDING          (1u << 3)
#define UWP_LP_BACKGROUND       (1u << 4)
#define UWP_LP_FOREGROUND       (1u << 5)
#define UWP_LP_VISIBILITY       (1u << 6)
#define UWP_LP_H_ALIGN          (1u << 7)
#define UWP_LP_V_ALIGN          (1u << 8)
#define UWP_LP_FONT_SIZE        (1u << 9)
#define UWP_LP_FONT_WEIGHT      (1u << 10)
#define UWP_LP_FONT_FAMILY      (1u << 11)
#define UWP_LP_BORDER_BRUSH     (1u << 12)
#define UWP_LP_BORDER_THICKNESS (1u << 13)
#define UWP_LP_CORNER_RADIUS    (1u << 14)
#define UWP_LP_OPACITY          (1u << 15)
#define UWP_LP_IS_ACTIVE        (1u << 16)

static inline const WCHAR *uwp_elem_placeholder( const struct uwp_element *el )
{
    return (el && el->placeholder) ? el->placeholder : L"";
}
static inline void uwp_elem_set_placeholder( struct uwp_element *el, const WCHAR *s )
{
    if (el) uwp_elem_set_cold_str( &el->placeholder, s, 256 );
}

static inline const WCHAR *uwp_elem_image_src( const struct uwp_element *el )
{
    return (el && el->media) ? el->media->image_src : L"";
}

static inline int uwp_elem_media_stretch( const struct uwp_element *el )
{
    return (el && el->media) ? el->media->media_stretch : 2; /* Uniform */
}

static inline const WCHAR *uwp_elem_display_member_path( const struct uwp_element *el )
{
    return (el && el->nav) ? el->nav->display_member_path : L"";
}

static inline IInspectable *uwp_elem_selected_item( const struct uwp_element *el )
{
    return (el && el->nav) ? el->nav->selected_item : NULL;
}

static inline INT32 uwp_elem_selected_index( const struct uwp_element *el )
{
    return (el && el->nav) ? el->nav->selected_index : -1;
}

static inline BOOL uwp_elem_nav_is_pane_open( const struct uwp_element *el )
{
    return (el && el->nav) ? el->nav->nav_is_pane_open : FALSE;
}

static inline BOOL uwp_elem_nav_is_settings_visible( const struct uwp_element *el )
{
    return (el && el->nav) ? el->nav->nav_is_settings_visible : TRUE;
}

static inline BOOL uwp_elem_nav_is_pane_toggle_visible( const struct uwp_element *el )
{
    return (el && el->nav) ? el->nav->nav_is_pane_toggle_visible : TRUE;
}

static inline double uwp_elem_brush_opacity( const struct uwp_element *el )
{
    return (el && el->style_anim) ? el->style_anim->brush_opacity : 1.0;
}

static inline BOOL uwp_elem_has_brush_argb( const struct uwp_element *el )
{
    return (el && el->style_anim) ? el->style_anim->has_brush_argb : FALSE;
}

static inline UINT32 uwp_elem_brush_argb( const struct uwp_element *el )
{
    return (el && el->style_anim) ? el->style_anim->brush_argb : 0;
}

static inline BOOL uwp_is_exact_autosuggest( const struct uwp_element *el )
{
    return el && (el->class_flags & UWP_CF_AUTO_SUGGEST_BOX_EXACT) != 0;
}

static inline BOOL uwp_is_autosuggest( const struct uwp_element *el )
{
    return el && (el->class_flags & UWP_CF_AUTO_SUGGEST_BOX) != 0;
}

static inline BOOL uwp_is_textbox( const struct uwp_element *el )
{
    return el && (el->class_flags & UWP_CF_TEXT_BOX) != 0;
}

static inline BOOL uwp_is_text_input( const struct uwp_element *el )
{
    return el && ((el->class_flags & (UWP_CF_AUTO_SUGGEST_BOX | UWP_CF_TEXT_BOX)) != 0 ||
                  wcsstr( el->name, L"SearchBox" ) != NULL ||
                  wcsstr( el->name, L"SuggestBox" ) != NULL);
}

static inline BOOL uwp_is_combobox( const struct uwp_element *el )
{
    return el && (el->class_flags & UWP_CF_COMBOBOX) != 0;
}

static inline BOOL uwp_is_nav_view( const struct uwp_element *el )
{
    return el && (el->class_flags & UWP_CF_NAV_VIEW) != 0;
}

static inline BOOL uwp_is_nav_header( const struct uwp_element *el )
{
    return el && (el->class_flags & UWP_CF_NAV_HEADER) != 0;
}

static inline BOOL uwp_is_nav_separator( const struct uwp_element *el )
{
    return el && (el->class_flags & UWP_CF_NAV_SEPARATOR) != 0;
}

static inline BOOL uwp_is_nav_item( const struct uwp_element *el )
{
    return el && (el->class_flags & UWP_CF_NAV_ITEM) != 0;
}

static inline BOOL uwp_is_items_repeater( const struct uwp_element *el )
{
    return el && (el->class_flags & UWP_CF_ITEMS_REPEATER) != 0;
}

static inline BOOL uwp_is_scroll_viewer( const struct uwp_element *el )
{
    return el && (el->class_flags & UWP_CF_SCROLL_VIEWER) != 0;
}

static inline BOOL uwp_is_card( const struct uwp_element *el )
{
    return el && (el->class_flags & UWP_CF_CARD) != 0;
}

static inline BOOL uwp_is_shimmer( const struct uwp_element *el )
{
    return el && (el->class_flags & UWP_CF_SHIMMER) != 0;
}

static inline BOOL uwp_is_teaching_tip( const struct uwp_element *el )
{
    return el && (el->class_flags & UWP_CF_TEACHING_TIP) != 0;
}

static inline BOOL uwp_is_menubar( const struct uwp_element *el )
{
    return el && (el->class_flags & UWP_CF_MENUBAR) != 0;
}

static inline BOOL uwp_is_menubar_item( const struct uwp_element *el )
{
    return el && (el->class_flags & UWP_CF_MENUBAR_ITEM) != 0;
}

static inline BOOL uwp_is_font_icon( const struct uwp_element *el )
{
    return el && (el->class_flags & UWP_CF_FONT_ICON) != 0;
}

static inline BOOL uwp_is_pivot( const struct uwp_element *el )
{
    return el && (el->class_flags & UWP_CF_PIVOT) != 0;
}

static inline BOOL uwp_is_pivot_item( const struct uwp_element *el )
{
    return el && (el->class_flags & UWP_CF_PIVOT_ITEM) != 0;
}

static inline BOOL uwp_is_tabview( const struct uwp_element *el )
{
    return el && (el->class_flags & UWP_CF_TABVIEW) != 0;
}

static inline BOOL uwp_is_calendar_date_picker( const struct uwp_element *el )
{
    return el && (el->class_flags2 & UWP_CF2_CALENDAR_DATE_PICKER) != 0;
}

static inline BOOL uwp_is_expander( const struct uwp_element *el )
{
    return el && (el->class_flags2 & UWP_CF2_EXPANDER) != 0;
}

static inline BOOL uwp_is_toggle_switch( const struct uwp_element *el )
{
    return el && (el->class_flags2 & UWP_CF2_TOGGLE_SWITCH) != 0;
}

static inline BOOL uwp_is_hyperlink_button( const struct uwp_element *el )
{
    return el && (el->class_flags2 & UWP_CF2_HYPERLINK_BTN) != 0;
}

static inline BOOL uwp_is_rich_text_block( const struct uwp_element *el )
{
    return el && (el->class_flags & UWP_CF_RICH_TEXT_BLOCK) != 0;
}

static inline BOOL uwp_is_tooltip( const struct uwp_element *el )
{
    return el && (el->class_flags & UWP_CF_TOOLTIP) != 0;
}

static inline BOOL uwp_is_separator( const struct uwp_element *el )
{
    return el && (el->class_flags & UWP_CF_SEPARATOR) != 0;
}

static inline BOOL uwp_is_accelerator( const struct uwp_element *el )
{
    return el && (el->class_flags & UWP_CF_ACCELERATOR) != 0;
}

static inline BOOL uwp_is_flyout( const struct uwp_element *el )
{
    return el && (el->class_flags & UWP_CF_FLYOUT) != 0;
}

const WCHAR *uwp_btype_to_class_name( WORD tid );
HRESULT uwp_element_create( const WCHAR *class_name, IInspectable *outer, IInspectable **inner, void **instance );
struct uwp_element *uwp_elem_from_any( void *ptr );
struct uwp_element *uwp_elem_lookup_no_deref( const void *ptr );
void uwp_elem_register_alias( struct uwp_element *el, void *alias_ptr );
BOOL uwp_elem_is_descendant( struct uwp_element *ancestor, struct uwp_element *node );
struct uwp_element *uwp_find_owning_page( struct uwp_element *el );
BOOL uwp_panel_has_concrete_child( struct uwp_element *panel );
void uwp_fire_loaded_tree( struct uwp_element *el );
void uwp_fire_evc_if_needed( struct uwp_element *el );
void uwp_fire_size_and_layout_changed( struct uwp_element *el );
void uwp_ui_fire_got_focus( struct uwp_element *el );
void uwp_ui_fire_lost_focus( struct uwp_element *el );
void uwp_popup_set_open( struct uwp_element *impl, BOOL open );
void uwp_open_popups_remove( struct uwp_element *impl );
int uwp_get_open_popups( struct uwp_element **out_list, int max_cnt );
void uwp_toggle_button_set_checked( struct uwp_element *btn, int checked, BOOL fire_events );
void uwp_update_binding_source( IInspectable *src, const WCHAR *path, IInspectable *new_val );
void uwp_refresh_element_bindings( struct uwp_element *el );
HANDLE get_xaml_window_ready_event( void );
HRESULT xaml_async_action_create( IAsyncAction **out );
void **vtbl_dup( const void *src );

/* Shared COM sub-interface and generic/noop handlers */
HRESULT WINAPI uwp_sub_QueryInterface( IInspectable *iface, REFIID iid, void **out );
extern ULONG WINAPI uwp_sub_AddRef( IInspectable *iface );
extern ULONG WINAPI uwp_sub_Release( IInspectable *iface );
HRESULT WINAPI uwp_sub_GetIids( IInspectable *iface, ULONG *count, IID **iids );
HRESULT WINAPI uwp_sub_GetRuntimeClassName( IInspectable *iface, HSTRING *name );
HRESULT WINAPI uwp_sub_GetTrustLevel( IInspectable *iface, TrustLevel *level );
HRESULT WINAPI uwp_generic_noop( void *iface, ... );
HRESULT WINAPI uwp_ui_event_add( void *iface, void *handler, EventRegistrationToken *tok );

/* IDispatch implementation on uwp_element */
HRESULT WINAPI uwp_disp_QueryInterface( IDispatch *iface, REFIID iid, void **out );
ULONG WINAPI uwp_disp_AddRef( IDispatch *iface );
ULONG WINAPI uwp_disp_Release( IDispatch *iface );
HRESULT WINAPI uwp_disp_GetTypeInfoCount( IDispatch *iface, UINT *count );
HRESULT WINAPI uwp_disp_GetTypeInfo( IDispatch *iface, UINT idx, LCID lcid, ITypeInfo **info );
HRESULT WINAPI uwp_disp_GetIDsOfNames( IDispatch *iface, REFIID riid, LPOLESTR *names,
        UINT count, LCID lcid, DISPID *ids );
HRESULT WINAPI uwp_disp_Invoke( IDispatch *iface, DISPID id, REFIID riid, LCID lcid,
        WORD flags, DISPPARAMS *params, VARIANT *result, EXCEPINFO *excep, UINT *argerr );

/* FrameworkElement / FrameworkElement2 / FrameworkElement3 core methods */
HRESULT WINAPI uwp_fe_FindName( void *iface, HSTRING name, void **out );
HRESULT WINAPI uwp_fe_add_Loaded( void *iface, void *handler, EventRegistrationToken *tok );
HRESULT WINAPI uwp_fe_remove_Loaded( void *iface, EventRegistrationToken tok );
HRESULT WINAPI uwp_fe_get_ActualHeight( void *iface, double *val );
HRESULT WINAPI uwp_fe_get_ActualWidth( void *iface, double *val );
HRESULT WINAPI uwp_fe_get_DataContext( void *iface, void **out );
HRESULT WINAPI uwp_fe_get_Height( void *iface, double *val );
HRESULT WINAPI uwp_fe_get_Margin( void *iface, struct uwp_thickness *t );
HRESULT WINAPI uwp_fe_get_Name( void *iface, HSTRING *name );
HRESULT WINAPI uwp_fe_get_Parent( void *iface, void **out );
HRESULT WINAPI uwp_fe_get_Resources( void *iface, void **out );
HRESULT WINAPI uwp_fe_get_Tag( void *iface, void **out );
HRESULT WINAPI uwp_fe_get_Width( void *iface, double *val );
HRESULT WINAPI uwp_fe_put_DataContext( void *iface, void *val );
HRESULT WINAPI uwp_fe_put_Height( void *iface, double val );
HRESULT WINAPI uwp_fe_put_Margin( void *iface, struct uwp_thickness *t );
HRESULT WINAPI uwp_fe_put_Name( void *iface, HSTRING name );
HRESULT WINAPI uwp_fe_put_Resources( void *iface, void *val );
HRESULT WINAPI uwp_fe_put_Style( void *iface, IInspectable *val );
HRESULT WINAPI uwp_fe_put_Tag( void *iface, void *val );
HRESULT WINAPI uwp_fe_put_Width( void *iface, double val );
HRESULT WINAPI uwp_fe2_get_RequestedTheme( void *iface, int *out );
HRESULT WINAPI uwp_fe2_put_RequestedTheme( void *iface, int val );
HRESULT WINAPI uwp_fe3_add_DataContextChanged( void *iface, void *handler, EventRegistrationToken *tok );
HRESULT WINAPI uwp_fe3_remove_DataContextChanged( void *iface, EventRegistrationToken tok );
HRESULT WINAPI uwp_fe3_add_Loading( void *iface, void *handler, EventRegistrationToken *tok );
HRESULT WINAPI uwp_fe3_remove_Loading( void *iface, EventRegistrationToken tok );

/* Legacy xaml_element / WinUI element helpers */
struct xaml_element *impl_from_element( void *iface );
struct xaml_element *impl_from_IDependencyObject( void *iface );
struct xaml_element *impl_from_IWebView2( void *iface );
struct xaml_element *impl_from_IWebView22( void *iface );
struct xaml_async_action *impl_from_async_action( IAsyncAction *iface );
struct xaml_async_action *impl_from_async_info( IAsyncInfo *iface );
ULONG WINAPI element_AddRef( IInspectable *iface );
HRESULT WINAPI element_GetIids( IInspectable *iface, ULONG *count, IID **iids );
HRESULT WINAPI element_GetRuntimeClassName( IInspectable *iface, HSTRING *name );
HRESULT WINAPI element_GetTrustLevel( IInspectable *iface, TrustLevel *level );
HRESULT WINAPI element_QueryInterface( IInspectable *iface, REFIID iid, void **out );
ULONG WINAPI element_Release( IInspectable *iface );
HRESULT WINAPI element_add_Loaded( void *iface, void *handler, EventRegistrationToken *token );
HRESULT WINAPI element_generic_slot( void *iface, ... );
HRESULT WINAPI element_get_double( void *iface, double *val );
HRESULT WINAPI element_grid_get_RowDefinitions_abi3( void *iface, void *count, void **out );
HRESULT WINAPI element_remove_Loaded( void *iface, EventRegistrationToken token );
ULONG WINAPI element_wv22_AddRef( IInspectable *iface );
HRESULT WINAPI element_wv22_GetRuntimeClassName( IInspectable *iface, HSTRING *name );
HRESULT WINAPI element_wv22_QueryInterface( IInspectable *iface, REFIID iid, void **out );
ULONG WINAPI element_wv22_Release( IInspectable *iface );
ULONG WINAPI element_wv2_AddRef( IInspectable *iface );
HRESULT WINAPI element_wv2_GetRuntimeClassName( IInspectable *iface, HSTRING *name );
HRESULT WINAPI element_wv2_QueryInterface( IInspectable *iface, REFIID iid, void **out );
ULONG WINAPI element_wv2_Release( IInspectable *iface );

#endif /* __WINE_WINDOWS_UI_XAML_UWP_TYPES_H */
