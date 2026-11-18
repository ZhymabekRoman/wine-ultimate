#ifndef UWP_PRIVATE_H
#define UWP_PRIVATE_H
#include "private.h"
#include "weakreference.h"
extern const GUID IID_IPropertyValue_g;
typedef struct IWeakReference IWeakReference;
typedef struct IWeakReferenceVtbl IWeakReferenceVtbl;
extern const GUID IID_IDependencyPropertyChangedEventArgs_UWP;
extern const GUID IID_IXamlMetadataProvider_UWP_g;
struct uwp_thickness { double Left, Top, Right, Bottom; };
struct uwp_grid_length { double Value; int GridUnitType; };


typedef LONG (WINAPI *p_RegQueryValueExW_t)( HKEY, LPCWSTR, LPDWORD, LPDWORD, LPBYTE, LPDWORD );
typedef LONG (WINAPI *p_RegEnumKeyExW_t)( HKEY, DWORD, LPWSTR, LPDWORD, LPDWORD, LPWSTR, LPDWORD, PFILETIME );

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

#define XAML_META_CACHE_SIZE 1024


#define XAML_FACTORY_HASH_SIZE 32   /* power of two, > 2x the entry count */


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
};

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
    void *pad3, *pad4, *pad5, *pad6, *pad7, *pad8, *pad9, *pad10;
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


struct uwp_element
{
    IInspectable nondelegating_iface;
    IInspectable IInspectable_iface;
    IInspectable IDependencyObject_iface;
    IInspectable IDependencyObject2_iface;
    IInspectable IUIElement_iface;
    IInspectable IFrameworkElement_iface;
    IInspectable IFrameworkElement3_iface;
    IInspectable IControl_iface;
    IInspectable IControlProtected_iface;
    IInspectable IContentControl_iface;
    IInspectable IUserControl_iface;
    IInspectable IPage_iface;
    IInspectable IFrame_iface;
    IInspectable IButtonBase_iface;
    IInspectable IToggleButton_iface;
    IInspectable IPanel_iface;
    IInspectable IGrid_iface;
    IInspectable IBorder_iface;
    IInspectable ITextBlock_iface;
    IInspectable IRowDefinition_iface;
    IInspectable IColumnDefinition_iface;
    IInspectable IStyle_iface;
    IInspectable IBrush_iface;
    IInspectable ISolidColorBrush_iface;
    IInspectable IFlyout_iface;
    IInspectable ISelector_iface;
    IInspectable IItemsControl_iface;
    IInspectable IStateTrigger_iface;
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
    IInspectable IUIElement4_iface;
    IInspectable IContentDialog_iface;
    IInspectable IContentDialog2_iface;
    IInspectable ITeachingTip_iface;
    IInspectable IAutoSuggestBox_iface;
    IWeakReferenceSource IWeakReferenceSource_iface;
    IDispatch IDispatch_iface;
    IInspectable IReferenceTrackerTarget_iface;
    ITypeInfo ITypeInfo_iface;
    LONG ref;
    HWND island_hwnd;
    HWND dialog_hwnd;
    IInspectable *outer;
    BOOL delegating;             /* outer is a real app-side element (answers IUIElement): refcounts delegate to it */
    enum uwp_elem_kind kind;
    WCHAR class_name[128];
    WCHAR x_class[128];
    WCHAR name[64];
    WCHAR uid[64];
    WCHAR text[256];
    WCHAR accel_text[64];
    IInspectable *dialog_title_obj;
    WCHAR dialog_title[128];
    WCHAR dialog_primary_text[64];
    WCHAR dialog_secondary_text[64];
    WCHAR dialog_close_text[64];
    int dialog_default_btn;
    int conn_id;
    struct uwp_element *parent;
    struct uwp_element *owner_component;
    IInspectable *content;       /* ContentControl/UserControl/Page/Border/ScrollViewer child */
    IInspectable *context_flyout; /* UIElement.ContextFlyout */
    IInspectable *children_coll; /* Panel.Children */
    IInspectable *accelerators_coll; /* UIElement.KeyboardAccelerators */
    IInspectable *row_defs_coll; /* Grid.RowDefinitions */
    IInspectable *col_defs_coll; /* Grid.ColumnDefinitions */
    IInspectable *resources_rd;  /* FrameworkElement.Resources */
    IInspectable *data_context;
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
    BOOL is_pressed;
    BOOL is_hovered;
    BOOL is_hit_test_visible;
    double opacity;
    double width, height;
    double min_width, max_width;
    double min_height, max_height;
    double actual_w, actual_h;
    double margin_l, margin_t, margin_r, margin_b;
    double padding_l, padding_t, padding_r, padding_b;
    int h_align;       /* 0=Left, 1=Center, 2=Right, 3=Stretch */
    int v_align;       /* 0=Top, 1=Center, 2=Bottom, 3=Stretch */
    double font_size;
    COLORREF bg_color;
    UINT32 bg_argb;
    BYTE bg_alpha;
    BOOL has_bg;
    COLORREF fg_color;
    UINT32 fg_argb;
    BYTE fg_alpha;
    BOOL has_fg;
    COLORREF border_color;
    UINT32 border_argb;
    BYTE border_alpha;
    BOOL has_border;
    double border_thick_l, border_thick_t, border_thick_r, border_thick_b;
    double corner_radius;
    /* SolidColorBrush / Color fields */
    UINT32 brush_argb;
    BOOL has_brush_argb;
    double brush_opacity;
    WCHAR brush_color_ref[96];
    /* Style / Setter fields */
    WCHAR style_based_on[96];
    struct uwp_style_setter *style_setters;
    int style_setter_cnt;
    int style_setter_cap;
    struct uwp_rd_bucket *rd_bucket;
    /* Computed screen rectangle */
    RECT bounds;
    BOOL has_bounds;
    /* Events & DependencyProperty store */
    IInspectable **loaded_handlers;
    int loaded_cnt;
    int loaded_cap;
    BOOL loaded_fired;
    BOOL template_applied;
    IInspectable **click_handlers;
    int click_cnt;
    int click_cap;
    IInspectable **dc_handlers;
    int dc_cnt;
    int dc_cap;
    void *binding_connector;
    IInspectable *selected_item;
    IInspectable *sel_changed_handler;
    IInspectable *tab_items_source;
    IInspectable *tab_add_cmd;
    IInspectable *tab_add_cmd_param;
    IInspectable **tab_add_handlers;
    int tab_add_cnt;
    int tab_add_cap;
    IInspectable **tab_close_handlers;
    int tab_close_cnt;
    int tab_close_cap;
    struct xaml_event_registry ptr_pressed_reg;
    struct xaml_event_registry ptr_moved_reg;
    struct xaml_event_registry ptr_released_reg;
    struct xaml_event_registry ptr_entered_reg;
    struct xaml_event_registry ptr_exited_reg;
    struct xaml_event_registry ptr_capture_lost_reg;
    struct xaml_event_registry ptr_canceled_reg;
    struct xaml_event_registry ptr_wheel_reg;
    struct xaml_event_registry nav_sel_changed_reg;
    struct xaml_event_registry nav_item_invoked_reg;
    IInspectable *nav_args_item;
    BOOL nav_fired;
    struct uwp_dp_entry *dp_store;
    int dp_cnt;
    int dp_cap;
    struct uwp_dp_cb_entry *dp_cbs;
    int dp_cb_cnt;
    int dp_cb_cap;
    INT64 dp_cb_next_token;
    /* Template children cache (for CalculationResult etc.) */
    struct uwp_element *tpl_text_container;
    struct uwp_element *tpl_normal_output;
    struct uwp_element *tpl_scroll_left;
    struct uwp_element *tpl_scroll_right;
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
    WCHAR image_src[256];
    BYTE *img_pixels;
    UINT img_w, img_h;
    BOOL img_loaded;
    struct uwp_element *next_reg;
    int adopted_faulted_ctor;
    IInspectable *provider_inst;
    void *outer_unk;
    /* KeyboardAccelerator, XamlUICommand, PreviewKeyDown, TeachingTip state */
    struct xaml_event_registry preview_keydown_reg;
    UINT accel_key;
    UINT accel_mods;
    BOOL accel_enabled;
    IInspectable *accel_scope_owner;
    struct uwp_element *accel_target;
    IInspectable **accel_invoked_handlers;
    int accel_invoked_cnt;
    int accel_invoked_cap;
    IInspectable **exec_req_handlers;
    int exec_req_cnt;
    int exec_req_cap;
    IInspectable **tip_closing_handlers;
    int tip_closing_cnt;
    int tip_closing_cap;
    IInspectable **tip_closed_handlers;
    int tip_closed_cnt;
    int tip_closed_cap;
    IInspectable **flyout_opening_handlers;
    int flyout_opening_cnt;
    int flyout_opening_cap;
    IInspectable **flyout_closed_handlers;
    int flyout_closed_cnt;
    int flyout_closed_cap;
    BOOL is_open;
};

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

struct uwp_dp_entry
{
    void *dp;
    IInspectable *value;
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
    BOOL vsm_collect;
    struct xbf_aot_builder *aot;
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

struct xaml_cached_type
{
    const void **lpVtbl;
    LONG ref;
    WCHAR *full_name;
    const WCHAR *short_name;
    WORD xbf_type_id;
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

struct uwp_class_factory
{
    IActivationFactory IActivationFactory_iface;
    IInspectable IComposableFactory_iface;
    IInspectable IStatics_iface;
    const WCHAR *class_name;
    IInspectable IDpStatics_iface;
};

struct uwp_key_routed_args
{
    IInspectable IInspectable_iface;
    LONG ref;
    UINT vk;
    boolean handled;
    struct { UINT32 RepeatCount; UINT32 ScanCode; boolean IsExtendedKey; boolean IsMenuKeyDown; boolean WasKeyDown; boolean IsKeyReleased; } status;
};

struct uwp_dp_changed_args
{
    const void *vtbl;
    LONG ref;
    IInspectable *old_val;
    IInspectable *new_val;
    void *dp;
};

struct uwp_rd_entry
{
    WCHAR key[96];
    enum uwp_rd_kind kind;
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
    IInspectable IDependencyObject_iface;
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

struct xbf_rd_table
{
    struct xbf_rd_map_item *items;
    int count;
    int cap;
};

struct uwp_rd_bucket
{
    void *rd_key;
    struct uwp_element *owner_el;
    struct uwp_rd_entry *entries;
    int count;
    int cap;
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

struct uwp_vec_view
{
    IInspectable IInspectable_iface;
    LONG ref;
    IInspectable **items;
    unsigned int count;
    int cap;
    struct uwp_element *owner;
};

struct core_webview2
{
    IInspectable IInspectable_iface;
    IInspectable ICoreWebView2_2_iface;
    IInspectable ICoreWebView2_3_iface;
    IInspectable ICoreWebView2_11_iface;
    IInspectable ICoreWebView2_13_iface;
    IInspectable ICoreWebView2_stub_iface;
    LONG ref;
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

struct uwp_dp_cb_entry
{
    void *dp;
    IInspectable *callback;
    INT64 token;
};

struct uwp_ka_invoked_args
{
    IInspectable IInspectable_iface;
    LONG ref;
    boolean handled;
    IInspectable *element;
};

struct store_catalog_item
{
    WCHAR full_name[256];
    WCHAR name[128];
    WCHAR cat[64];
    WCHAR icon[8];
    WCHAR rating[32];
    WCHAR btn[16];
    BOOL is_framework;
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

struct xbf_type_info
{
    WCHAR xmlns[128];
    WCHAR full_name[128];
    WCHAR short_name[64];
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

struct pri_xbf_catalog
{
    struct pri_xbf_entry *entries;
    DWORD cnt;
    DWORD cap;
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

struct xaml_static_factory
{
    const WCHAR *name;
    IActivationFactory *factory;
};



typedef LONG (WINAPI *p_RegOpenKeyExW_t)( HKEY, LPCWSTR, DWORD, REGSAM, PHKEY );
typedef LSTATUS (WINAPI *p_RegCloseKey_t)( HKEY );
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
extern struct uwp_calc_vm g_calc_vm;

#define XAML_META_CACHE_SIZE 1024
/* Shared internal declarations for the windows.ui.xaml module split. */
struct xaml_type;
struct xaml_member;
struct xaml_metadata_provider;
struct notify_property_changed;
struct property_changed_args;
struct reference_int32;
struct command_iface;
/* Generic event delegate: { QueryInterface, AddRef, Release, Invoke(sender, args) }. */
struct xaml_event_handler;
UINT32 apply_opacity_to_argb( UINT32 argb, double opacity );
COLORREF blend_argb_over( UINT32 fg_argb, COLORREF bg_cr );
IInspectable *box_bool_value( boolean b );
IInspectable *box_int32_value( INT32 v );
IInspectable *box_string_value( const WCHAR *str );
enum uwp_elem_kind classify_uwp_class( const WCHAR *cls );
extern const void *dp_changed_args_vtbl[];
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
int uwp_tabview_get_items( struct uwp_element *tv, IInspectable **out_items, int max_items );
void uwp_tabview_get_item_title( IInspectable *item, WCHAR *buf, int buf_len );
void uwp_tabview_compute_rects( const RECT *trc, int count, int idx, RECT *pill_rc, RECT *close_rc, RECT *add_rc );
void uwp_tabview_fire_add_tab( struct uwp_element *tv );
void uwp_tabview_fire_close_tab( struct uwp_element *tv, IInspectable *item );
void uwp_tabview_select_item( struct uwp_element *tv, IInspectable *item );
HRESULT WINAPI element_wv22_QueryInterface( IInspectable *iface, REFIID iid, void **out );
ULONG WINAPI element_wv22_Release( IInspectable *iface );
ULONG WINAPI element_wv2_AddRef( IInspectable *iface );
HRESULT WINAPI element_wv2_GetRuntimeClassName( IInspectable *iface, HSTRING *name );
HRESULT WINAPI element_wv2_QueryInterface( IInspectable *iface, REFIID iid, void **out );
ULONG WINAPI element_wv2_Release( IInspectable *iface );
void ensure_winui_pri_cat( void );
struct uwp_rd_bucket *get_rd_bucket( void *rd_key, BOOL create );
HANDLE get_xaml_window_ready_event( void );
struct xaml_element *impl_from_IDependencyObject( void *iface );
struct xaml_element *impl_from_IWebView2( void *iface );
struct xaml_element *impl_from_IWebView22( void *iface );
struct xaml_async_action *impl_from_async_action( IAsyncAction *iface );
struct xaml_async_action *impl_from_async_info( IAsyncInfo *iface );
const BYTE *pri_get_data( DWORD *out_size );
void pri_map_file_to( const WCHAR *pri_path, const BYTE **out_data, DWORD *out_size );
struct uwp_rd_entry *rd_bucket_upsert( struct uwp_rd_bucket *b, const WCHAR *key );
void scan_disk_xbfs_in_dir( const WCHAR *dir_path, const char *rel_prefix, struct pri_xbf_catalog *cat );
extern struct uwp_class_factory scb_factory_obj;
void store_make_icon_monogram( const WCHAR *title, WCHAR *icon, size_t icon_len );
void uwp_accel_register( struct uwp_element *accel_el, struct uwp_element *target_el,
                                UINT vk, UINT mods, BOOL enabled );
void uwp_apply_style_by_name( struct xbf_ctx *ctx, struct uwp_element *el, const WCHAR *style_key );
void uwp_apply_style_entry( struct xbf_ctx *ctx, struct uwp_element *el,
                                   struct uwp_rd_bucket *b, struct uwp_rd_entry *se, int depth );
void uwp_arrange_element( struct uwp_element *el, RECT rc );
HRESULT WINAPI uwp_btn_add_Click( void *iface, void *handler, EventRegistrationToken *tok );
HRESULT WINAPI uwp_btn_get_Command( void *iface, void **out );
HRESULT WINAPI uwp_btn_get_CommandParameter( void *iface, void **out );
HRESULT WINAPI uwp_btn_put_Command( void *iface, void *val );
HRESULT WINAPI uwp_btn_put_CommandParameter( void *iface, void *val );
const WCHAR *uwp_btype_to_class_name( WORD tid );
HRESULT WINAPI uwp_cd2_get_CloseButtonText( void *iface, HSTRING *out );
HRESULT WINAPI uwp_cd2_get_DefaultButton( void *iface, int *out );
HRESULT WINAPI uwp_cd2_put_CloseButtonText( void *iface, HSTRING val );
HRESULT WINAPI uwp_cd2_put_DefaultButton( void *iface, int val );
ULONG WINAPI uwp_cf_AddRef( IActivationFactory *iface );
HRESULT WINAPI uwp_cf_GetIids( IActivationFactory *iface, ULONG *cnt, IID **iids );
HRESULT WINAPI uwp_cf_GetRuntimeClassName( IActivationFactory *iface, HSTRING *name );
HRESULT WINAPI uwp_cf_GetTrustLevel( IActivationFactory *iface, TrustLevel *lvl );
HRESULT WINAPI uwp_cf_QI( IActivationFactory *iface, REFIID iid, void **out );
ULONG WINAPI uwp_cf_Release( IActivationFactory *iface );
extern const void *uwp_cf_comp_vtbl[64];
void uwp_close_flyout_popup( BOOL fire_closed );
HRESULT WINAPI uwp_ctrl_ApplyTemplate( void *iface, boolean *ret );
HRESULT WINAPI uwp_ctrl_Focus( void *iface, int state, boolean *ret );
HRESULT WINAPI uwp_ctrl_get_FontSize( void *iface, double *val );
HRESULT WINAPI uwp_ctrl_get_IsEnabled( void *iface, boolean *val );
HRESULT WINAPI uwp_ctrl_prot_GetTemplateChild( void *iface, HSTRING child_name, void **out );
HRESULT WINAPI uwp_ctrl_put_FontSize( void *iface, double val );
HRESULT WINAPI uwp_ctrl_put_IsEnabled( void *iface, boolean val );
ULONG WINAPI uwp_disp_AddRef( IDispatch *iface );
HRESULT WINAPI uwp_disp_GetIDsOfNames( IDispatch *iface, REFIID riid, LPOLESTR *names,
        UINT count, LCID lcid, DISPID *ids );
HRESULT WINAPI uwp_disp_GetTypeInfo( IDispatch *iface, UINT idx, LCID lcid, ITypeInfo **info );
HRESULT WINAPI uwp_disp_GetTypeInfoCount( IDispatch *iface, UINT *count );
HRESULT WINAPI uwp_disp_Invoke( IDispatch *iface, DISPID id, REFIID riid, LCID lcid,
        WORD flags, DISPPARAMS *params, VARIANT *result, EXCEPINFO *excep, UINT *argerr );
HRESULT WINAPI uwp_disp_QueryInterface( IDispatch *iface, REFIID iid, void **out );
ULONG WINAPI uwp_disp_Release( IDispatch *iface );
HRESULT WINAPI uwp_do2_RegisterPropertyChangedCallback( void *iface, void *dp, void *cb, INT64 *token );
HRESULT WINAPI uwp_do_ClearValue( void *iface, void *dp );
HRESULT WINAPI uwp_do_GetValue( void *iface, void *dp, IInspectable **out );
HRESULT WINAPI uwp_do_SetValue( void *iface, void *dp, IInspectable *val );
HRESULT WINAPI uwp_do_get_Dispatcher( void *iface, void **value );
HRESULT WINAPI uwp_do_get_DispatcherQueue( void *iface, void **value );
void uwp_el_set_bg_argb( struct uwp_element *el, UINT32 argb );
void uwp_el_set_border_argb( struct uwp_element *el, UINT32 argb );
void uwp_el_set_fg_argb( struct uwp_element *el, UINT32 argb );
struct uwp_element *uwp_elem_from_any( void *ptr );
BOOL uwp_elem_is_descendant( struct uwp_element *ancestor, struct uwp_element *node );
void uwp_ensure_app_resources_loaded( void );
BOOL uwp_extract_resource_key( const WCHAR *in, WCHAR *out, size_t out_chars );
HRESULT WINAPI uwp_fe3_add_DataContextChanged( void *iface, void *handler, EventRegistrationToken *tok );
HRESULT WINAPI uwp_fe_FindName( void *iface, HSTRING name, void **out );
HRESULT WINAPI uwp_fe_add_Loaded( void *iface, void *handler, EventRegistrationToken *tok );
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
HRESULT WINAPI uwp_fe_put_Tag( void *iface, void *val );
HRESULT WINAPI uwp_fe_put_Width( void *iface, double val );
struct uwp_element *uwp_find_owning_page( struct uwp_element *el );
void uwp_fire_loaded_tree( struct uwp_element *el );
BOOL uwp_fire_preview_keydown_tree( struct uwp_element *el, struct uwp_key_routed_args *args );
const WCHAR *uwp_format_display_label( const struct uwp_element *el, WCHAR *tmp, size_t tmp_len );
HRESULT WINAPI uwp_frame_NavigateWithParam( void *iface, void *type_name_ptr, void *param, boolean *result );
HRESULT WINAPI uwp_generic_noop( void *iface, ... );
BOOL uwp_get_custom_xaml_member( void *app_outer, const WCHAR *owner_type_name,
                                        const WCHAR *prop_name, void *target_obj, IInspectable **out_val );
HRESULT WINAPI uwp_grid_get_ColumnDefinitions( void *iface, void **out );
HRESULT WINAPI uwp_grid_get_RowDefinitions( void *iface, void **out );
BOOL uwp_has_open_teachingtip( struct uwp_element *el );
void uwp_invalidate_window( void );
BOOL uwp_invoke_accel_entry( const struct uwp_accel_entry *e );
BOOL uwp_is_bg_pid( WORD pid );
BOOL uwp_is_border_pid( WORD pid );
BOOL uwp_is_display_result_element( const struct uwp_element *el );
BOOL uwp_is_fg_pid( WORD pid );
ULONG WINAPI uwp_ka_args_Release( void *iface );
ULONG WINAPI uwp_key_args_Release( void *iface );
void uwp_load_image_asset( struct uwp_element *el );
void uwp_load_res_xbf_into_bucket( const char *base_dir, const WCHAR *uri_or_path, struct uwp_rd_bucket *target );
void uwp_mark_xbf_ref_instantiated( struct uwp_element *root, const WCHAR *cls_name );
HRESULT WINAPI uwp_nav_args_QI( void *iface, REFIID iid, void **out );
struct uwp_element *uwp_nav_find_item( struct uwp_element *el, int depth );
void uwp_nav_fire( struct uwp_element *el, struct xaml_event_registry *reg,
                          IInspectable *item );
HRESULT WINAPI uwp_nav_get_FooterMenuItems( void *iface, void **out );
HRESULT WINAPI uwp_nav_get_IsPaneOpen( void *iface, BOOL *open );
COLORREF uwp_paint_resolve_color( struct uwp_element *el, const WCHAR *key,
                                         COLORREF fallback, COLORREF surface_bg );
HRESULT WINAPI uwp_panel_get_Children( void *iface, void **out );
BOOL uwp_panel_has_concrete_child( struct uwp_element *panel );
void uwp_parse_and_build_xbf( const BYTE *xbf, DWORD xbf_sz, IInspectable *component,
                                     void *connector, BOOL is_uwp_conn, void *app_outer,
                                     int only_sub_idx, struct uwp_element *attach_root,
                                     struct uwp_element **out_created );
BOOL uwp_parse_color_string( const WCHAR *s, UINT32 *out_argb );
UINT uwp_parse_vk_string( const WCHAR *s );
void uwp_parse_xbf_internal( const BYTE *xbf, DWORD xbf_sz, IInspectable *component,
                                    void *connector, BOOL is_uwp_conn, void *app_outer,
                                    int only_sub_idx, struct uwp_element *attach_root,
                                    struct uwp_element **out_created,
                                    const char *base_xbf_path, struct uwp_rd_bucket *target_rd,
                                    BOOL rd_only );
void uwp_parse_xbf_into_rd( const BYTE *xbf, DWORD xbf_sz, const char *base_xbf_path,
                                   struct uwp_rd_bucket *target_rd );
BOOL uwp_populate_element_from_pri_xbf( struct uwp_element *el, const WCHAR *short_name );
BOOL uwp_popup_handle_escape( void );
BOOL uwp_popup_is_open_for( struct uwp_element *flyout );
void uwp_pri_enum_xbfs_in_blob( const BYTE *pri, DWORD pri_sz,
                                       BOOL (*cb)(const char *path, const BYTE *xbf, DWORD sz, void *ctx), void *ctx );
BOOL uwp_pri_find_xbf_rel( const char *base_xbf_dir, const WCHAR *uri_or_name,
                                  const BYTE **out_ptr, DWORD *out_sz,
                                  char *out_resolved, size_t out_resolved_sz );
void uwp_propagate_data_context( struct uwp_element *impl, IInspectable *dc );
struct xaml_type *uwp_provider_get_xaml_type_cached( struct xaml_metadata_provider *mp,
                                                            const WCHAR *owner_type_name );
struct uwp_rd_bucket *uwp_rd_bucket_for_el( struct uwp_element *el, BOOL create );
BOOL uwp_rd_find_entry( struct uwp_rd_bucket *start_bucket, struct uwp_element *scope_el,
                               const WCHAR *raw_key, int depth,
                               struct uwp_rd_entry **out_entry, struct uwp_rd_bucket **out_bucket );
void uwp_rd_link_merged( struct uwp_rd_bucket *parent, struct uwp_rd_bucket *child );
void uwp_rd_link_theme( struct uwp_rd_bucket *parent, const WCHAR *theme_name, struct uwp_rd_bucket *child );
void uwp_realize_empty_panel_defers( struct uwp_element *root );
void uwp_record_xbf_ref( struct uwp_element *root, const WCHAR *full_name,
                                const WCHAR *short_name, BOOL is_resource_path );
BOOL uwp_resolve_brush_or_color( struct uwp_rd_bucket *start_bucket, struct uwp_element *scope_el,
                                        const WCHAR *key_or_expr, int depth, UINT32 *out_argb );
HRESULT WINAPI uwp_rowcol_get_Length( void *iface, struct uwp_grid_length *gl );
HRESULT WINAPI uwp_rowcol_put_Length( void *iface, struct uwp_grid_length *gl );
void uwp_set_custom_xaml_member( void *app_outer, const WCHAR *owner_type_name,
                                        const WCHAR *prop_name, void *target_obj, const WCHAR *str_val );
void uwp_show_flyout_popup( struct uwp_element *flyout, HWND owner_hwnd, int screen_x, int screen_y );
ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );
HRESULT WINAPI uwp_sub_GetIids( IInspectable *iface, ULONG *count, IID **iids );
HRESULT WINAPI uwp_sub_GetRuntimeClassName( IInspectable *iface, HSTRING *name );
HRESULT WINAPI uwp_sub_GetTrustLevel( IInspectable *iface, TrustLevel *level );
HRESULT WINAPI uwp_sub_QueryInterface( IInspectable *iface, REFIID iid, void **out );
ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );
HRESULT WINAPI uwp_tb_get_Text( void *iface, HSTRING *out );
HRESULT WINAPI uwp_tb_put_Text( void *iface, HSTRING val );
HRESULT WINAPI uwp_ui7_get_KeyboardAccelerators( void *iface, void **out );
HRESULT WINAPI uwp_ui_event_add( void *iface, void *handler, EventRegistrationToken *tok );
BOOL uwp_unbox_enum_int32( IInspectable *obj, INT32 *out_val, BOOL allow_plain_int );;
/* ============================================================================
 * XAML Type-Metadata Reflection Cache (IXamlMetadataProvider + xbf_known_types)
 *
 * O(1) FNV-1a hash index protected by SRWLOCK (shared read on lookup, exclusive
 * write on miss).  Pre-seeds all xbf_known_types[] entries on first use and
 * caches IXamlType instances (both builtin XAML types and app-provider RCWs).
 * ============================================================================ */
extern const GUID IID_IXamlType_WinUI3_g;
extern SRWLOCK xaml_meta_cache_lock;
extern INIT_ONCE xaml_meta_cache_once;
extern struct xaml_meta_cache_entry *xaml_meta_cache[XAML_META_CACHE_SIZE];
HRESULT WINAPI dp_args_QI( void *iface, REFIID iid, void **out );
ULONG WINAPI dp_args_AddRef( void *iface );
ULONG WINAPI dp_args_Release( void *iface );
HRESULT WINAPI dp_args_GetIids( void *iface, ULONG *count, IID **iids );
HRESULT WINAPI dp_args_GetRuntimeClassName( void *iface, HSTRING *name );
HRESULT WINAPI dp_args_GetTrustLevel( void *iface, TrustLevel *level );
HRESULT WINAPI dp_args_get_Property( void *iface, void **out );
HRESULT WINAPI dp_args_get_OldValue( void *iface, IInspectable **out );
HRESULT WINAPI dp_args_get_NewValue( void *iface, IInspectable **out );
extern const void *dp_changed_args_vtbl[];
extern const GUID IID_IReference_NumbersAndOperatorsEnum;
























































































































extern struct xbh_factory_obj_s xbh_factory_obj;


struct uwp_rtt_target_vtbl
{
    IInspectableVtbl base;
    HRESULT (WINAPI *AddRefToTracker)( IInspectable * );
    HRESULT (WINAPI *ReleaseToTracker)( IInspectable * );
    HRESULT (WINAPI *GetStrongValue)( IInspectable *, IInspectable ** );
};

struct xaml_element *impl_from_element( void *iface );

BOOL uwp_dispatch_keydown( HWND hwnd, UINT vk, LPARAM lparam, void *island_root_ptr );

void uwp_invoke_button( struct uwp_element *btn );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

BOOL uwp_unbox_string( void *obj, WCHAR *buf, int buf_chars );

HRESULT WINAPI uwp_vec_Append( void *iface, void *item );

HRESULT WINAPI uwp_vec_GetAt( void *iface, unsigned int idx, void **item );

HRESULT uwp_vec_create( struct uwp_element *owner, IInspectable **out );

HRESULT WINAPI uwp_vec_get_Size( void *iface, unsigned int *sz );

struct uwp_vsm_group *uwp_vsm_add_group( struct uwp_element *el, const WCHAR *name );

struct uwp_vsm_state *uwp_vsm_add_state( struct uwp_vsm_group *g, const WCHAR *name );

void uwp_vsm_evaluate_tree( struct uwp_element *el, double win_w, double win_h );

void uwp_vsm_invalidate_eval_chain( struct uwp_element *el );

void uwp_vsm_state_add_setter( struct uwp_vsm_state *st, const WCHAR *target, const WCHAR *prop,
                                      const WCHAR *val_str, int vt, double num, BOOL is_res_ref );

void vsm_clean_prop_name( const WCHAR *raw, WCHAR *out_prop, size_t max_prop );

void vsm_split_setter_target( const WCHAR *spec, WCHAR *out_target, size_t max_target,
                                     WCHAR *out_prop, size_t max_prop );

void **vtbl_dup( const void *src );

const BYTE *winui_pri_get_data( DWORD *out_size );

void wv2_ensure_host_subclass( HWND parent );

HWND wv2_get_controller_parent( void );

void wv2_start_browser( struct xaml_element *impl );

HRESULT WINAPI wv2_statics_get_SourceProperty( void *iface, void **value );

HRESULT WINAPI wv2_statics_get_prop( void *iface, void **value );

void wv2_sync_child_hwnds( HWND parent, UINT width, UINT height );

HRESULT xaml_async_action_create( IAsyncAction **out );

struct xaml_meta_cache_entry *xaml_meta_insert_locked( const WCHAR *full_name, WORD xbf_id );

BOOL WINAPI init_pri_once( INIT_ONCE *once, void *param, void **ctx );

BOOL WINAPI init_pri_xbf_cat_once( INIT_ONCE *once, void *param, void **ctx );

BOOL WINAPI init_xaml_meta_cache( INIT_ONCE *once, void *param, void **ctx );

BOOL pri_xbf_collect_cb( const char *path, const BYTE *xbf, DWORD sz, void *ctx );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

extern const GUID CLSID_WICImagingFactory_local;

extern const GUID GUID_WICPixelFormat32bppBGRA_local;

extern const GUID IID_IBindingFactory_UWP;

extern const GUID IID_IButtonBaseFactory_UWP;

extern const GUID IID_IButtonBaseStatics_UWP;

extern const GUID IID_IButtonFactory_UWP;

extern const GUID IID_ICommand_UWP;

extern const GUID IID_ICommand_UWP2;

extern const GUID IID_IComponentConnector2_UWP_g;

extern const GUID IID_IComponentConnector_UWP_g;

extern const GUID IID_IContentControlFactory_UWP;

extern const GUID IID_IContentControlFactory_UWP2;

extern const GUID IID_IContentDialogClosedEventArgs_UWP;

extern const GUID IID_IContentDialogFactory_UWP;

extern const GUID IID_IContentPresenterFactory_UWP;

extern const GUID IID_IControlFactory_UWP;

extern const GUID IID_IDataTemplateSelectorFactory_UWP;

extern const GUID IID_IDependencyObjectCollectionFactory_UWP;

extern const GUID IID_IDependencyObjectFactory_UWP;

extern const GUID IID_IDependencyPropertyChangedCallback_UWP;


extern const GUID IID_IDesktopWindowXamlSourceFactory_UWP;

extern const GUID IID_IDispatcherTimerFactory_UWP;

extern const GUID IID_IExecuteRequestedEventArgs_UWP;

extern const GUID IID_IFlyoutBaseFactory_UWP;

extern const GUID IID_IFrameFactory_UWP;

extern const GUID IID_IFrameworkElementFactory_UWP;

extern const GUID IID_IFrameworkElementStatics_UWP;

extern const GUID IID_IGridFactory_UWP;

extern const GUID IID_IGridStatics_UWP;

extern const GUID IID_IGridStatics_UWP2;

extern const GUID IID_IKeyRoutedEventArgs_UWP;

extern const GUID IID_IKeyboardAcceleratorFactory_UWP;

extern const GUID IID_IKeyboardAcceleratorInvokedEventArgs_UWP;

extern const GUID IID_IListViewFactory_UWP;

extern const GUID IID_IMarkupExtensionFactory_UWP;

extern const GUID IID_IMenuBarItem_WinUI;

extern const GUID IID_IMenuBar_WinUI;

extern const GUID IID_IMenuFlyoutItemBaseFactory_UWP;

extern const GUID IID_IMenuFlyoutItemFactory_UWP;

extern const GUID IID_IMenuFlyoutSeparatorFactory_UWP;

extern const GUID IID_INotifyPropertyChanged_UWP;

extern const GUID IID_IPageFactory_UWP;

extern const GUID IID_IPanelFactory_UWP;

extern const GUID IID_IPanelFactory_UWP2;

extern const GUID IID_IPropertyChangedEventArgs_UWP;

extern const GUID IID_IPropertyChangedEventHandler_UWP;

extern const GUID IID_IPropertyPathFactory_UWP;

extern const GUID IID_IRadioButtonFactory_UWP;

extern const GUID IID_IReferenceTracker;

extern const GUID IID_IReferenceTrackerTarget;

extern const GUID IID_IReferenceTrackerTarget_UWP;

extern const GUID IID_IReferenceTrackerTarget_UWP2;

extern const GUID IID_IReferenceTrackerTarget_WinUI;

extern const GUID IID_IReferenceTracker_UWP;

extern const GUID IID_IRelativeSourceFactory_UWP;

extern const GUID IID_IRoutedEventArgsFactory_UWP;

extern const GUID IID_IStateTriggerBaseFactory2_UWP;

extern const GUID IID_IStateTriggerBaseFactory_UWP;

extern const GUID IID_IStyleSelectorFactory_UWP;

extern const GUID IID_IStyleSelectorFactory_UWP2;

extern const GUID IID_IToggleButtonFactory2_UWP;

extern const GUID IID_IToggleButtonFactory_UWP;

extern const GUID IID_IToggleButtonStatics_UWP;

extern const GUID IID_IUIElementFactory_UWP;

extern const GUID IID_IUIElementStatics_UWP;

extern const GUID IID_IUriRuntimeClass_g;

extern const GUID IID_IUserControlFactory_UWP;

extern const GUID IID_IVisualStateManagerFactory_UWP;

extern const GUID IID_IVisualTreeHelperStatics_UWP;

extern const GUID IID_IWICImagingFactory_local;

extern const GUID IID_IXamlCompositionBrushBaseFactory_UWP;

extern const GUID IID_IXamlLightFactory_UWP;


extern const GUID IID_IXamlReaderStatics_UWP;

extern const GUID IID_IXamlType_UWP_g;

extern const void *uwp_asb_vtbl[32];

extern const void *uwp_brush_vtbl[16];

extern const void *uwp_cd2_vtbl[24];

extern const void *uwp_cd_vtbl[48];

extern const void *uwp_cf_act_vtbl[16];

extern const void *uwp_cf_statics_vtbl[64];

extern const void *uwp_ctrl_prot_vtbl[16];

extern const void *uwp_ctrl_vtbl[64];

extern const void *uwp_dp_statics_vtbl[32];

extern const void *uwp_dwxs_native_vtbl[16];

extern const void *uwp_fe3_vtbl[16];

extern const void *uwp_flyout_vtbl[128];

extern const void *uwp_grid_vtbl[32];

extern const void *uwp_icmd_vtbl[16];

extern const void *uwp_items_ctrl_vtbl[32];

extern const void *uwp_ka_args_vtbl[16];

extern const void *uwp_ka_vtbl[32];

extern const void *uwp_key_args_vtbl[16];

extern const void *uwp_mf2_vtbl[16];

extern const void *uwp_mfi3_vtbl[16];

extern const void *uwp_mfi_vtbl[32];

extern const void *uwp_mfsi_vtbl[16];

extern const void *uwp_navview_vtbl[112];

extern const void *uwp_rowcol_vtbl[32];

extern const void *uwp_selector_vtbl[20];

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

extern const void *uwp_tb_vtbl[64];

extern const void *uwp_teachingtip_vtbl[64];

extern const void *uwp_timer_vtbl[16];

extern const void *uwp_tmfi_vtbl[16];

extern const void *uwp_xaml_cmd_vtbl[32];

extern const void *wv22_vtbl[256];

extern const void *wv2_do_vtbl[64];

extern const void *wv2_vtbl[256];

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

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
extern int g_accel_cnt;
extern struct uwp_accel_entry *g_accel_entries;
extern struct xaml_element *g_active_wv2_impl;
extern int g_all_uwp_elem_cnt;
extern struct uwp_element *g_all_uwp_elems_head;
extern SRWLOCK g_all_uwp_elems_lock;
extern int g_app_rd_sentinel;
extern BOOL g_app_res_loaded;
extern BOOL g_app_res_loading;
extern struct uwp_element *g_captured_elem;
extern struct uwp_class_factory g_dyn_factories[256];
extern SRWLOCK g_dyn_factories_lock;
extern int g_dyn_factory_cnt;
extern struct uwp_element *g_focused_textbox;
extern struct uwp_element *g_hovered_elem;
extern ID3D11DeviceContext *g_island_d3d11_ctx;
extern ID3D11Device *g_island_d3d11_dev;
extern BOOL g_island_d3d11_failed;
/* Offscreen render-target frame pair + layout pass for WS_CHILD islands. */
BOOL xaml_gpu_begin_frame_rtv( ID3D11Device *dev, ID3D11DeviceContext *ctx, ID3D11RenderTargetView *rtv,
                               UINT width, UINT height, COLORREF clear_color );
BOOL xaml_gpu_end_frame_rtv( ID3D11DeviceContext *ctx, UINT *out_instances, UINT *out_hits, UINT *out_misses );
BOOL xaml_layout_render_gpu_rtv( HWND hwnd, int width, int height, IInspectable *content,
                                 ID3D11Device *dev, ID3D11DeviceContext *ctx, ID3D11RenderTargetView *rtv );
extern int g_island_hwnd_cnt;
extern HWND g_island_hwnds[16];
extern struct uwp_element *g_last_closed_flyout;
extern DWORD g_last_closed_tick;
extern struct uwp_element *g_last_created_page;
extern BOOL g_layout_dirty;
extern float g_layout_scale;
extern int g_loaded_res_xbf_cnt;
extern IInspectable *g_nav_args_item;
extern struct uwp_element *g_navigated_page_el;
extern COLORREF g_paint_page_bg;
extern HWND g_popup_hwnd;
extern struct uwp_element *g_popup_owner_el;
extern struct pri_xbf_catalog g_pri_cat;
extern const BYTE *g_pri_data;
extern INIT_ONCE g_pri_once;
extern DWORD g_pri_size;
extern struct uwp_rd_bucket *g_rd_buckets_head;
extern IDependencyProperty *g_routed_event_ptr_pressed;
extern HWND g_sub_popup_hwnd;
extern int g_theme_rd_sentinel;
extern struct pri_xbf_catalog g_winui_pri_cat;
extern BOOL g_winui_pri_cat_done;
extern const BYTE *g_winui_pri_data;
extern BOOL g_winui_pri_probed;
extern DWORD g_winui_pri_size;
extern HWND g_wv2_core_hwnd;
extern HWND g_wv2_host_hwnd;

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_AddRef( ITypeInfo *iface );

ULONG WINAPI uwp_disp_ti_Release( ITypeInfo *iface );

extern const size_t xaml_static_factories_count;
extern const struct xaml_static_factory *xaml_factory_hash[32];
extern INIT_ONCE xaml_factory_hash_once;
extern struct uwp_class_factory vsm_factory_obj;
extern struct uwp_class_factory auto_props_factory_obj;
extern struct uwp_class_factory vth_factory_obj;
extern struct uwp_class_factory feap_factory_obj;
extern ULONG WINAPI uwp_sub_AddRef( IInspectable *iface );
extern ULONG WINAPI uwp_sub_Release( IInspectable *iface );
extern IDependencyProperty *wv2_source_dp;
extern char g_loaded_res_xbfs[128][256];
HRESULT WINAPI uwp_ui_add_SelectionChanged( void *iface, void *handler, EventRegistrationToken *tok );
HRESULT WINAPI uwp_ui_remove_SelectionChanged( void *iface, EventRegistrationToken tok );
HRESULT WINAPI uwp_ui_add_ItemInvoked( void *iface, void *handler, EventRegistrationToken *tok );
HRESULT WINAPI uwp_ui_remove_ItemInvoked( void *iface, EventRegistrationToken tok );
extern const ITypeInfoVtbl uwp_disp_ti_vtbl;
extern const struct xaml_static_factory xaml_static_factories[];

#endif /* UWP_PRIVATE_H */
