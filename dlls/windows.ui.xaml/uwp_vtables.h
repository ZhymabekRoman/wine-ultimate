/* WinRT Windows.UI.Xaml shared vtable and singleton factory declarations
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

#ifndef __WINE_WINDOWS_UI_XAML_UWP_VTABLES_H
#define __WINE_WINDOWS_UI_XAML_UWP_VTABLES_H

#include "uwp_types.h"
#include "uwp_vsm_rd.h"

struct xbh_factory_obj_s;

/* Typed vtable structs */
struct uwp_binding_vtbl
{
    HRESULT (WINAPI *QueryInterface)( IInspectable *iface, REFIID iid, void **out );
    ULONG (WINAPI *AddRef)( IInspectable *iface );
    ULONG (WINAPI *Release)( IInspectable *iface );
    HRESULT (WINAPI *GetIids)( IInspectable *iface, ULONG *count, IID **iids );
    HRESULT (WINAPI *GetRuntimeClassName)( IInspectable *iface, HSTRING *name );
    HRESULT (WINAPI *GetTrustLevel)( IInspectable *iface, TrustLevel *level );
    HRESULT (WINAPI *get_Path)( void *iface, IInspectable **out );
    HRESULT (WINAPI *put_Path)( void *iface, IInspectable *val );
    HRESULT (WINAPI *pad8_9[2])( void *iface, ... );
    HRESULT (WINAPI *get_Source)( void *iface, IInspectable **out );
    HRESULT (WINAPI *put_Source)( void *iface, IInspectable *val );
    HRESULT (WINAPI *pad12_31[20])( void *iface, ... );
};

struct uwp_pp_vtbl
{
    HRESULT (WINAPI *QueryInterface)( IInspectable *iface, REFIID iid, void **out );
    ULONG (WINAPI *AddRef)( IInspectable *iface );
    ULONG (WINAPI *Release)( IInspectable *iface );
    HRESULT (WINAPI *GetIids)( IInspectable *iface, ULONG *count, IID **iids );
    HRESULT (WINAPI *GetRuntimeClassName)( IInspectable *iface, HSTRING *name );
    HRESULT (WINAPI *GetTrustLevel)( IInspectable *iface, TrustLevel *level );
    HRESULT (WINAPI *get_Path)( void *iface, HSTRING *out );
    HRESULT (WINAPI *pad7_15[9])( void *iface, ... );
};

struct uwp_state_trigger_vtbl
{
    HRESULT (WINAPI *QueryInterface)( IInspectable *iface, REFIID iid, void **out );
    ULONG (WINAPI *AddRef)( IInspectable *iface );
    ULONG (WINAPI *Release)( IInspectable *iface );
    HRESULT (WINAPI *GetIids)( IInspectable *iface, ULONG *count, IID **iids );
    HRESULT (WINAPI *GetRuntimeClassName)( IInspectable *iface, HSTRING *name );
    HRESULT (WINAPI *GetTrustLevel)( IInspectable *iface, TrustLevel *level );
    HRESULT (WINAPI *SetActive)( void *iface, boolean is_active );
    HRESULT (WINAPI *pad7_15[9])( void *iface, ... );
};

struct uwp_state_trigger_pub_vtbl
{
    HRESULT (WINAPI *QueryInterface)( IInspectable *iface, REFIID iid, void **out );
    ULONG (WINAPI *AddRef)( IInspectable *iface );
    ULONG (WINAPI *Release)( IInspectable *iface );
    HRESULT (WINAPI *GetIids)( IInspectable *iface, ULONG *count, IID **iids );
    HRESULT (WINAPI *GetRuntimeClassName)( IInspectable *iface, HSTRING *name );
    HRESULT (WINAPI *GetTrustLevel)( IInspectable *iface, TrustLevel *level );
    HRESULT (WINAPI *get_IsActive)( void *iface, boolean *out );
    HRESULT (WINAPI *put_IsActive)( void *iface, boolean is_active );
    HRESULT (WINAPI *pad8_15[8])( void *iface, ... );
};

/* DependencyObject / UIElement / FrameworkElement / VSM / Noop vtables */
extern const void *dp_changed_args_vtbl[];
extern const void *dc_args_vtbl[];
extern const void *uwp_ccc_vtbl[];
extern const void *uwp_tap_vtbl[];
extern const void *uwp_tap_routed_vtbl[];
extern const void *uwp_do_vtbl[16];
extern const void *uwp_do2_vtbl[16];
extern const void *uwp_ui_vtbl[128];
extern const void *uwp_fe_vtbl[112];
extern const void *uwp_fe2_vtbl[16];
extern const void *uwp_fe3_vtbl[16];
extern const void *uwp_fe6_vtbl[12];
extern const void *uwp_fe7_vtbl[32];
extern const void *uwp_visual_state_vtbl[32];
extern const void *uwp_noop_vtbl[128];

/* Class factory & statics vtables */
extern const void *uwp_cf_act_vtbl[16];
extern const void *uwp_cf_comp_vtbl[64];
extern const void *uwp_cf_statics_vtbl[64];
extern const void *uwp_dp_statics_vtbl[32];
extern const void *uwp_bitmap_image_factory_vtbl[16];
extern const void *uwp_font_family_factory_vtbl[16];

/* Control vtables */
extern const void *uwp_asb_vtbl[32];
extern const void *uwp_asb2_vtbl[16];
extern const void *uwp_splitview_vtbl[32];
extern const void *uwp_splitview2_vtbl[16];
extern const void *uwp_cd2_vtbl[24];
extern const void *uwp_cd_vtbl[48];
extern const void *uwp_ctrl_prot_vtbl[16];
extern const void *uwp_ctrl_vtbl[64];
extern const void *uwp_dwxs_native_vtbl[16];
extern const void *uwp_flyout_vtbl[128];
extern const void *uwp_grid_vtbl[32];
extern const void *uwp_grid3_vtbl[16];
extern const void *uwp_icmd_vtbl[16];
extern const void *uwp_icm_vtbl[12];
extern const void *uwp_scale_transform_vtbl[16];
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
extern const void *uwp_combobox_vtbl[24];
extern const void *uwp_combobox6_vtbl[24];
extern const void *uwp_pivot_vtbl[32];
extern const void *uwp_pivot_item_vtbl[12];
extern const void *uwp_cdp_vtbl[64];
extern const void *uwp_uie10_vtbl[16];
extern const void *uwp_xaml_root_vtbl[16];
extern const void *uwp_tb_vtbl[64];
extern const void *uwp_tb2_vtbl[24];
extern const void *uwp_tb6_vtbl[16];
extern const void *uwp_rtb_vtbl[64];
extern const void *uwp_sb_vtbl[24];
extern const void *uwp_paragraph_vtbl[64];
extern const void *uwp_run_vtbl[16];
extern const void *uwp_teachingtip_vtbl[64];
extern const void *uwp_timer_vtbl[16];
extern const void *uwp_tmfi_vtbl[16];
extern const void *uwp_xaml_cmd_vtbl[32];
extern const void *uwp_cc_vtbl[128];
extern const void *uwp_border_vtbl[32];
extern const void *uwp_btn_vtbl[32];
extern const void *uwp_tbtn_vtbl[32];
extern const void *uwp_popup_vtbl[32];
extern const void *uwp_popup2_vtbl[16];
extern const void *uwp_popup3_vtbl[16];
extern const void *uwp_panel_vtbl[32];
extern const void *uwp_ui7_vtbl[32];
extern const void *uwp_ui4_vtbl[32];
extern const void *uwp_ui5_vtbl[36];
extern const void *uwp_button_flyout_vtbl[8];
extern const void *uwp_frame3_vtbl[16];
extern const void *uwp_frame5_vtbl[16];
extern const void *uwp_frame2_vtbl[16];
extern const void *uwp_page_vtbl[16];
extern const void *uwp_sv_vtbl[72];
extern const void *uwp_data_template_vtbl[];
extern const void *uwp_dts_vtbl[];
extern const void *uwp_dts2_vtbl[];
extern const void *uwp_el_obsvec_vtbl[16];
extern const void *uwp_el_vec_vtbl[24];
extern const void *uwp_el_vecview_vtbl[16];
extern const void *uwp_el_iterable_vtbl[16];
extern const ITypeInfoVtbl uwp_disp_ti_vtbl;
extern const struct IDispatchVtbl uwp_disp_vtbl;

/* Binding & PropertyPath vtables */
extern const struct uwp_binding_vtbl uwp_binding_vtbl;
extern const struct uwp_pp_vtbl uwp_pp_vtbl;

/* Brush / media / font / geometry / StateTrigger vtables */
extern const void *uwp_brush_vtbl[16];
extern const void *uwp_font_family_vtbl[16];
extern const void *uwp_font_icon_vtbl[24];
extern const void *uwp_image_brush_vtbl[16];
extern const void *uwp_tile_brush_vtbl[16];
extern const void *uwp_grad_brush_vtbl[16];
extern const void *uwp_lgb_vtbl[16];
extern const void *uwp_grad_stop_vtbl[16];
extern const void *uwp_path_geom_vtbl[16];
extern const void *uwp_path_geom_f2_vtbl[16];
extern const void *uwp_mpe_vtbl[24];
extern const void *uwp_bi_vtbl[24];
extern const void *uwp_bs_vtbl[16];
extern const void *uwp_img_vtbl[24];
extern const void *uwp_shape_vtbl[32];
extern const void *uwp_xcbb_prot_vtbl[16];
extern const void *uwp_lis_vtbl[16];
extern const void *uwp_scb_vtbl[16];
extern const struct uwp_state_trigger_vtbl uwp_state_trigger_vtbl;
extern const struct uwp_state_trigger_pub_vtbl uwp_state_trigger_pub_vtbl;
extern const void *uwp_adaptive_trigger_vtbl[16];

/* WebView2 vtables */
extern const void *wv22_vtbl[256];
extern const void *wv2_do_vtbl[64];
extern const void *wv2_vtbl[256];

/* Singleton class-factory objects */
extern struct uwp_class_factory scb_factory_obj;
extern struct uwp_class_factory vsm_factory_obj;
extern struct uwp_class_factory sb_factory_obj;
extern struct uwp_class_factory auto_props_factory_obj;
extern struct uwp_class_factory vth_factory_obj;
extern struct uwp_class_factory feap_factory_obj;
extern struct xbh_factory_obj_s xbh_factory_obj;

#endif /* __WINE_WINDOWS_UI_XAML_UWP_VTABLES_H */
