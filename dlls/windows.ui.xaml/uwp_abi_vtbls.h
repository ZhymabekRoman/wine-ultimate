/* WinRT Windows.UI.Xaml typed ABI vtable declarations for external COM/WinRT calls
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

#ifndef __WINE_WINDOWS_UI_XAML_UWP_ABI_VTBLS_H
#define __WINE_WINDOWS_UI_XAML_UWP_ABI_VTBLS_H

#include "private.h"

struct uwp_size_f
{
    float width;
    float height;
};

/* Generic dynamic-slot vtable for COM/WinRT interfaces indexed by runtime slot number */
struct abi_slot_vtbl
{
    void *slots[128];
};

struct abi_slot_iface
{
    const struct abi_slot_vtbl *lpVtbl;
};

/* Generic typed event & async completion delegates */
struct abi_typed_event_handler_vtbl
{
    HRESULT (WINAPI *QueryInterface)( void *This, REFIID riid, void **ppv );
    ULONG (WINAPI *AddRef)( void *This );
    ULONG (WINAPI *Release)( void *This );
    HRESULT (WINAPI *Invoke)( void *This, void *sender, void *args );           /* slot 3 */
};
struct abi_typed_event_handler { const struct abi_typed_event_handler_vtbl *lpVtbl; };

struct abi_async_op_completed_handler_vtbl
{
    HRESULT (WINAPI *QueryInterface)( void *This, REFIID riid, void **ppv );
    ULONG (WINAPI *AddRef)( void *This );
    ULONG (WINAPI *Release)( void *This );
    HRESULT (WINAPI *Invoke)( void *This, void *async_info, int status );       /* slot 3 */
};
struct abi_async_op_completed_handler { const struct abi_async_op_completed_handler_vtbl *lpVtbl; };

/* CoreWebView2 COM interfaces */
struct abi_wv2_core_vtbl
{
    HRESULT (WINAPI *QueryInterface)( void *This, REFIID riid, void **ppv );
    ULONG (WINAPI *AddRef)( void *This );
    ULONG (WINAPI *Release)( void *This );
    HRESULT (WINAPI *get_Settings)( void *This, void **settings );              /* slot 3 */
};
struct abi_wv2_core { const struct abi_wv2_core_vtbl *lpVtbl; };

struct abi_wv2_settings2_vtbl
{
    HRESULT (WINAPI *QueryInterface)( void *This, REFIID riid, void **ppv );
    ULONG (WINAPI *AddRef)( void *This );
    ULONG (WINAPI *Release)( void *This );
    void *pad3_20[18];
    HRESULT (WINAPI *get_UserAgent)( void *This, LPWSTR *ua );                  /* slot 21 */
    HRESULT (WINAPI *put_UserAgent)( void *This, LPCWSTR ua );                  /* slot 22 */
};
struct abi_wv2_settings2 { const struct abi_wv2_settings2_vtbl *lpVtbl; };

struct abi_wv2_env_vtbl
{
    HRESULT (WINAPI *QueryInterface)( void *This, REFIID riid, void **ppv );
    ULONG (WINAPI *AddRef)( void *This );
    ULONG (WINAPI *Release)( void *This );
    HRESULT (WINAPI *CreateCoreWebView2Controller)( void *This, HWND hwnd, void *handler ); /* slot 3 */
};
struct abi_wv2_env { const struct abi_wv2_env_vtbl *lpVtbl; };

struct abi_wv2_env3_vtbl
{
    HRESULT (WINAPI *QueryInterface)( void *This, REFIID riid, void **ppv );
    ULONG (WINAPI *AddRef)( void *This );
    ULONG (WINAPI *Release)( void *This );
    void *pad3_8[6];
    HRESULT (WINAPI *CreateCoreWebView2CompositionController)( void *This, HWND hwnd, void *handler ); /* slot 9 */
};
struct abi_wv2_env3 { const struct abi_wv2_env3_vtbl *lpVtbl; };

struct abi_wv2_nav_comp_args_vtbl
{
    HRESULT (WINAPI *QueryInterface)( void *This, REFIID riid, void **ppv );
    ULONG (WINAPI *AddRef)( void *This );
    ULONG (WINAPI *Release)( void *This );
    void *pad3;
    HRESULT (WINAPI *get_WebErrorStatus)( void *This, int *out );               /* slot 4 */
};
struct abi_wv2_nav_comp_args { const struct abi_wv2_nav_comp_args_vtbl *lpVtbl; };

struct abi_wv2_http_headers_vtbl
{
    HRESULT (WINAPI *QueryInterface)( void *This, REFIID riid, void **ppv );
    ULONG (WINAPI *AddRef)( void *This );
    ULONG (WINAPI *Release)( void *This );
    HRESULT (WINAPI *GetHeader)( void *This, LPCWSTR name, LPWSTR *val );       /* slot 3 */
    void *pad4;
    HRESULT (WINAPI *Contains)( void *This, LPCWSTR name, BOOL *has );          /* slot 5 */
    HRESULT (WINAPI *SetHeader)( void *This, LPCWSTR name, LPCWSTR val );       /* slot 6 */
    HRESULT (WINAPI *RemoveHeader)( void *This, LPCWSTR name );                 /* slot 7 */
};
struct abi_wv2_http_headers { const struct abi_wv2_http_headers_vtbl *lpVtbl; };

struct abi_wv2_wrr_req_vtbl
{
    HRESULT (WINAPI *QueryInterface)( void *This, REFIID riid, void **ppv );
    ULONG (WINAPI *AddRef)( void *This );
    ULONG (WINAPI *Release)( void *This );
    void *pad3_8[6];
    HRESULT (WINAPI *get_Headers)( void *This, void **headers );                /* slot 9 */
};
struct abi_wv2_wrr_req { const struct abi_wv2_wrr_req_vtbl *lpVtbl; };

struct abi_wv2_wrr_evargs_vtbl
{
    HRESULT (WINAPI *QueryInterface)( void *This, REFIID riid, void **ppv );
    ULONG (WINAPI *AddRef)( void *This );
    ULONG (WINAPI *Release)( void *This );
    HRESULT (WINAPI *get_Request)( void *This, void **req );                    /* slot 3 */
    void *pad4_6[3];
    HRESULT (WINAPI *get_ResourceContext)( void *This, int *ctx );              /* slot 7 */
};
struct abi_wv2_wrr_evargs { const struct abi_wv2_wrr_evargs_vtbl *lpVtbl; };

struct abi_wv2_deferral_vtbl
{
    HRESULT (WINAPI *QueryInterface)( void *This, REFIID riid, void **ppv );
    ULONG (WINAPI *AddRef)( void *This );
    ULONG (WINAPI *Release)( void *This );
    HRESULT (WINAPI *Complete)( void *This );                                   /* slot 3 */
};
struct abi_wv2_deferral { const struct abi_wv2_deferral_vtbl *lpVtbl; };

struct abi_wv2_procfail_args_vtbl
{
    HRESULT (WINAPI *QueryInterface)( void *This, REFIID riid, void **ppv );
    ULONG (WINAPI *AddRef)( void *This );
    ULONG (WINAPI *Release)( void *This );
    HRESULT (WINAPI *get_ProcessFailedKind)( void *This, int *kind );           /* slot 3 */
};
struct abi_wv2_procfail_args { const struct abi_wv2_procfail_args_vtbl *lpVtbl; };

/* Composition / Brush / Media interfaces */
struct abi_comp_surface_brush_vtbl
{
    HRESULT (WINAPI *QueryInterface)( void *This, REFIID riid, void **ppv );
    ULONG (WINAPI *AddRef)( void *This );
    ULONG (WINAPI *Release)( void *This );
    void *pad3_11[9];
    HRESULT (WINAPI *get_Surface)( void *This, IInspectable **surface );        /* slot 12 */
};
struct abi_comp_surface_brush { const struct abi_comp_surface_brush_vtbl *lpVtbl; };

struct abi_comp_mask_brush_vtbl
{
    HRESULT (WINAPI *QueryInterface)( void *This, REFIID riid, void **ppv );
    ULONG (WINAPI *AddRef)( void *This );
    ULONG (WINAPI *Release)( void *This );
    void *pad3_7[5];
    HRESULT (WINAPI *get_Source)( void *This, IInspectable **source );          /* slot 8 */
};
struct abi_comp_mask_brush { const struct abi_comp_mask_brush_vtbl *lpVtbl; };

struct abi_xcbb_overrides_vtbl
{
    HRESULT (WINAPI *QueryInterface)( void *This, REFIID riid, void **ppv );
    ULONG (WINAPI *AddRef)( void *This );
    ULONG (WINAPI *Release)( void *This );
    void *pad3_5[3];
    HRESULT (WINAPI *OnConnected)( void *This );                                /* slot 6 */
};
struct abi_xcbb_overrides { const struct abi_xcbb_overrides_vtbl *lpVtbl; };

struct abi_sprite_visual_vtbl
{
    HRESULT (WINAPI *QueryInterface)( void *This, REFIID riid, void **ppv );
    ULONG (WINAPI *AddRef)( void *This );
    ULONG (WINAPI *Release)( void *This );
    void *pad3_5[3];
    HRESULT (WINAPI *get_Brush)( void *This, IInspectable **brush );            /* slot 6 */
};
struct abi_sprite_visual { const struct abi_sprite_visual_vtbl *lpVtbl; };

struct abi_container_visual_vtbl
{
    HRESULT (WINAPI *QueryInterface)( void *This, REFIID riid, void **ppv );
    ULONG (WINAPI *AddRef)( void *This );
    ULONG (WINAPI *Release)( void *This );
    void *pad3_5[3];
    HRESULT (WINAPI *get_Children)( void *This, IInspectable **children );      /* slot 6 */
};
struct abi_container_visual { const struct abi_container_visual_vtbl *lpVtbl; };

struct abi_media_player_source2_vtbl
{
    HRESULT (WINAPI *QueryInterface)( void *This, REFIID riid, void **ppv );
    ULONG (WINAPI *AddRef)( void *This );
    ULONG (WINAPI *Release)( void *This );
    void *pad3_5[3];
    HRESULT (WINAPI *get_Source)( void *This, IInspectable **source );          /* slot 6 */
};
struct abi_media_player_source2 { const struct abi_media_player_source2_vtbl *lpVtbl; };

struct abi_media_source4_vtbl
{
    HRESULT (WINAPI *QueryInterface)( void *This, REFIID riid, void **ppv );
    ULONG (WINAPI *AddRef)( void *This );
    ULONG (WINAPI *Release)( void *This );
    void *pad3_8[6];
    HRESULT (WINAPI *get_Uri)( void *This, IInspectable **uri );                /* slot 9 */
};
struct abi_media_source4 { const struct abi_media_source4_vtbl *lpVtbl; };

/* WinRT Collections & Iteration */
struct abi_winrt_vector_vtbl
{
    HRESULT (WINAPI *QueryInterface)( void *This, REFIID riid, void **ppv );
    ULONG (WINAPI *AddRef)( void *This );
    ULONG (WINAPI *Release)( void *This );
    void *pad3_5[3];
    HRESULT (WINAPI *GetAt)( void *This, unsigned int index, void **item );     /* slot 6 */
    HRESULT (WINAPI *get_Size)( void *This, unsigned int *size );               /* slot 7 */
    HRESULT (WINAPI *GetView)( void *This, void **view );                       /* slot 8 */
    HRESULT (WINAPI *IndexOf)( void *This, void *item, unsigned int *idx, boolean *found ); /* slot 9 */
    HRESULT (WINAPI *SetAt)( void *This, unsigned int index, void *item );      /* slot 10 */
    HRESULT (WINAPI *InsertAt)( void *This, unsigned int index, void *item );   /* slot 11 */
    HRESULT (WINAPI *RemoveAt)( void *This, unsigned int index );               /* slot 12 */
    HRESULT (WINAPI *Append)( void *This, void *item );                         /* slot 13 */
};
struct abi_winrt_vector { const struct abi_winrt_vector_vtbl *lpVtbl; };

struct abi_winrt_hstring_vector_vtbl
{
    HRESULT (WINAPI *QueryInterface)( void *This, REFIID riid, void **ppv );
    ULONG (WINAPI *AddRef)( void *This );
    ULONG (WINAPI *Release)( void *This );
    void *pad3_5[3];
    HRESULT (WINAPI *GetAt)( void *This, unsigned int index, HSTRING *item );   /* slot 6 */
    HRESULT (WINAPI *get_Size)( void *This, unsigned int *size );               /* slot 7 */
};
struct abi_winrt_hstring_vector { const struct abi_winrt_hstring_vector_vtbl *lpVtbl; };

struct abi_winrt_iterable_vtbl
{
    HRESULT (WINAPI *QueryInterface)( void *This, REFIID riid, void **ppv );
    ULONG (WINAPI *AddRef)( void *This );
    ULONG (WINAPI *Release)( void *This );
    void *pad3_5[3];
    HRESULT (WINAPI *First)( void *This, void **iterator );                     /* slot 6 */
};
struct abi_winrt_iterable { const struct abi_winrt_iterable_vtbl *lpVtbl; };

struct abi_winrt_iterator_vtbl
{
    HRESULT (WINAPI *QueryInterface)( void *This, REFIID riid, void **ppv );
    ULONG (WINAPI *AddRef)( void *This );
    ULONG (WINAPI *Release)( void *This );
    void *pad3_5[3];
    HRESULT (WINAPI *get_Current)( void *This, void **current );                /* slot 6 */
    HRESULT (WINAPI *get_HasCurrent)( void *This, boolean *has_current );       /* slot 7 */
    HRESULT (WINAPI *MoveNext)( void *This, boolean *has_current );             /* slot 8 */
};
struct abi_winrt_iterator { const struct abi_winrt_iterator_vtbl *lpVtbl; };

struct abi_winrt_hstring_iterator_vtbl
{
    HRESULT (WINAPI *QueryInterface)( void *This, REFIID riid, void **ppv );
    ULONG (WINAPI *AddRef)( void *This );
    ULONG (WINAPI *Release)( void *This );
    void *pad3_5[3];
    HRESULT (WINAPI *get_Current)( void *This, HSTRING *current );              /* slot 6 */
    HRESULT (WINAPI *get_HasCurrent)( void *This, boolean *has_current );       /* slot 7 */
};
struct abi_winrt_hstring_iterator { const struct abi_winrt_hstring_iterator_vtbl *lpVtbl; };

/* XAML / Application / Window / Reflection / Controls */
struct abi_app_overrides_winui_vtbl
{
    HRESULT (WINAPI *QueryInterface)( void *This, REFIID riid, void **ppv );
    ULONG (WINAPI *AddRef)( void *This );
    ULONG (WINAPI *Release)( void *This );
    void *pad3_5[3];
    HRESULT (WINAPI *OnLaunched)( void *This, void *args );                     /* slot 6 */
};
struct abi_app_overrides_winui { const struct abi_app_overrides_winui_vtbl *lpVtbl; };

struct abi_app_overrides_uwp_vtbl
{
    HRESULT (WINAPI *QueryInterface)( void *This, REFIID riid, void **ppv );
    ULONG (WINAPI *AddRef)( void *This );
    ULONG (WINAPI *Release)( void *This );
    void *pad3_5[3];
    HRESULT (WINAPI *OnActivated)( void *This, void *args );                    /* slot 6 */
    HRESULT (WINAPI *OnLaunched)( void *This, void *args );                     /* slot 7 */
};
struct abi_app_overrides_uwp { const struct abi_app_overrides_uwp_vtbl *lpVtbl; };

struct abi_xaml_metadata_provider_vtbl
{
    HRESULT (WINAPI *QueryInterface)( void *This, REFIID riid, void **ppv );
    ULONG (WINAPI *AddRef)( void *This );
    ULONG (WINAPI *Release)( void *This );
    void *pad3_5[3];
    HRESULT (WINAPI *GetXamlType)( void *This, TypeName type, void **xaml_type ); /* slot 6 */
    HRESULT (WINAPI *GetXamlTypeByFullName)( void *This, HSTRING name, void **xaml_type ); /* slot 7 */
};
struct abi_xaml_metadata_provider { const struct abi_xaml_metadata_provider_vtbl *lpVtbl; };

struct abi_xaml_type_vtbl
{
    HRESULT (WINAPI *QueryInterface)( void *This, REFIID riid, void **ppv );
    ULONG (WINAPI *AddRef)( void *This );
    ULONG (WINAPI *Release)( void *This );
    void *pad3_10[8];
    HRESULT (WINAPI *get_IsConstructible)( void *This, boolean *value );        /* slot 11 */
    void *pad12_17[6];
    HRESULT (WINAPI *ActivateInstance_UWP)( void *This, void **instance );      /* slot 18 */
    HRESULT (WINAPI *ActivateInstance_WinUI)( void *This, void **instance );    /* slot 19 */
};
struct abi_xaml_type { const struct abi_xaml_type_vtbl *lpVtbl; };

struct abi_control_uwp_vtbl
{
    HRESULT (WINAPI *QueryInterface)( void *This, REFIID riid, void **ppv );
    ULONG (WINAPI *AddRef)( void *This );
    ULONG (WINAPI *Release)( void *This );
    void *pad3_22[20];
    HRESULT (WINAPI *put_IsEnabled)( void *This, boolean value );               /* slot 23 */
};
struct abi_control_uwp { const struct abi_control_uwp_vtbl *lpVtbl; };

struct abi_uri_runtime_class_vtbl
{
    HRESULT (WINAPI *QueryInterface)( void *This, REFIID riid, void **ppv );
    ULONG (WINAPI *AddRef)( void *This );
    ULONG (WINAPI *Release)( void *This );
    void *pad3_5[3];
    HRESULT (WINAPI *get_AbsoluteUri)( void *This, HSTRING *value );            /* slot 6 */
};
struct abi_uri_runtime_class { const struct abi_uri_runtime_class_vtbl *lpVtbl; };

struct abi_uri_factory_vtbl
{
    HRESULT (WINAPI *QueryInterface)( void *This, REFIID riid, void **ppv );
    ULONG (WINAPI *AddRef)( void *This );
    ULONG (WINAPI *Release)( void *This );
    void *pad3_5[3];
    HRESULT (WINAPI *CreateUri)( void *This, HSTRING uri, void **out );         /* slot 6 */
};
struct abi_uri_factory { const struct abi_uri_factory_vtbl *lpVtbl; };

struct abi_property_value_statics_vtbl
{
    HRESULT (WINAPI *QueryInterface)( void *This, REFIID riid, void **ppv );
    ULONG (WINAPI *AddRef)( void *This );
    ULONG (WINAPI *Release)( void *This );
    void *pad3_9[7];
    HRESULT (WINAPI *CreateInt32)( void *This, INT32 val, IInspectable **out );   /* slot 10 */
    HRESULT (WINAPI *CreateUInt32)( void *This, UINT32 val, IInspectable **out ); /* slot 11 */
    HRESULT (WINAPI *CreateInt64)( void *This, INT64 val, IInspectable **out );   /* slot 12 */
    HRESULT (WINAPI *CreateUInt64)( void *This, UINT64 val, IInspectable **out ); /* slot 13 */
    HRESULT (WINAPI *CreateSingle)( void *This, float val, IInspectable **out );  /* slot 14 */
    HRESULT (WINAPI *CreateDouble)( void *This, double val, IInspectable **out ); /* slot 15 */
    void *pad16;
    HRESULT (WINAPI *CreateBoolean)( void *This, boolean val, IInspectable **out ); /* slot 17 */
    void *pad18_20[3];
    HRESULT (WINAPI *CreateDateTime)( void *This, INT64 ticks, IInspectable **out ); /* slot 21 */
};
struct abi_property_value_statics { const struct abi_property_value_statics_vtbl *lpVtbl; };

struct abi_property_value_vtbl
{
    HRESULT (WINAPI *QueryInterface)( void *This, REFIID riid, void **ppv );
    ULONG (WINAPI *AddRef)( void *This );
    ULONG (WINAPI *Release)( void *This );
    void *pad3_20[18];
    HRESULT (WINAPI *GetDateTime)( void *This, INT64 *out );                   /* slot 21 */
};
struct abi_property_value { const struct abi_property_value_vtbl *lpVtbl; };

struct abi_iref_int64_vtbl
{
    HRESULT (WINAPI *QueryInterface)( void *This, REFIID riid, void **ppv );
    ULONG (WINAPI *AddRef)( void *This );
    ULONG (WINAPI *Release)( void *This );
    void *pad3_5[3];
    HRESULT (WINAPI *get_Value)( void *This, INT64 *out );                     /* slot 6 */
};
struct abi_iref_int64 { const struct abi_iref_int64_vtbl *lpVtbl; };

struct abi_solid_color_brush_vtbl
{
    HRESULT (WINAPI *QueryInterface)( void *This, REFIID riid, void **ppv );
    ULONG (WINAPI *AddRef)( void *This );
    ULONG (WINAPI *Release)( void *This );
    void *pad3_5[3];
    HRESULT (WINAPI *get_Color)( void *This, Color *out );                     /* slot 6 */
};
struct abi_solid_color_brush { const struct abi_solid_color_brush_vtbl *lpVtbl; };

struct abi_observable_vector_vtbl
{
    HRESULT (WINAPI *QueryInterface)( void *This, REFIID riid, void **ppv );
    ULONG (WINAPI *AddRef)( void *This );
    ULONG (WINAPI *Release)( void *This );
    void *pad3_5[3];
    HRESULT (WINAPI *add_VectorChanged)( void *This, void *cb, EventRegistrationToken *tok ); /* slot 6 */
};
struct abi_observable_vector { const struct abi_observable_vector_vtbl *lpVtbl; };

struct abi_dispatcher_queue_statics_vtbl
{
    HRESULT (WINAPI *QueryInterface)( void *This, REFIID riid, void **ppv );
    ULONG (WINAPI *AddRef)( void *This );
    ULONG (WINAPI *Release)( void *This );
    void *pad3_5[3];
    HRESULT (WINAPI *GetForCurrentThread)( void *This, void **out );            /* slot 6 */
};
struct abi_dispatcher_queue_statics { const struct abi_dispatcher_queue_statics_vtbl *lpVtbl; };

struct abi_app_window_statics_vtbl
{
    HRESULT (WINAPI *QueryInterface)( void *This, REFIID riid, void **ppv );
    ULONG (WINAPI *AddRef)( void *This );
    ULONG (WINAPI *Release)( void *This );
    void *pad3_8[6];
    HRESULT (WINAPI *GetFromWindowId)( void *This, UINT64 id, void **out );     /* slot 9 */
};
struct abi_app_window_statics { const struct abi_app_window_statics_vtbl *lpVtbl; };

struct abi_stringable_vtbl
{
    HRESULT (WINAPI *QueryInterface)( void *This, REFIID riid, void **ppv );
    ULONG (WINAPI *AddRef)( void *This );
    ULONG (WINAPI *Release)( void *This );
    void *pad3_5[3];
    HRESULT (WINAPI *ToString)( void *This, HSTRING *value );                   /* slot 6 */
};
struct abi_stringable { const struct abi_stringable_vtbl *lpVtbl; };

struct abi_custom_property_provider_vtbl
{
    HRESULT (WINAPI *QueryInterface)( void *This, REFIID riid, void **ppv );
    ULONG (WINAPI *AddRef)( void *This );
    ULONG (WINAPI *Release)( void *This );
    void *pad3_5[3];
    HRESULT (WINAPI *GetCustomProperty)( void *This, HSTRING name, void **prop ); /* slot 6 */
    void *pad7_8[2];
    HRESULT (WINAPI *get_Type)( void *This, TypeName *type );                   /* slot 9 */
};
struct abi_custom_property_provider { const struct abi_custom_property_provider_vtbl *lpVtbl; };

struct abi_custom_property_vtbl
{
    HRESULT (WINAPI *QueryInterface)( void *This, REFIID riid, void **ppv );
    ULONG (WINAPI *AddRef)( void *This );
    ULONG (WINAPI *Release)( void *This );
    void *pad3_7[5];
    HRESULT (WINAPI *GetValue)( void *This, IInspectable *target, IInspectable **out ); /* slot 8 */
    HRESULT (WINAPI *SetValue)( void *This, IInspectable *target, IInspectable *val );  /* slot 9 */
};
struct abi_custom_property { const struct abi_custom_property_vtbl *lpVtbl; };

struct abi_data_template_extension_vtbl
{
    HRESULT (WINAPI *QueryInterface)( void *This, REFIID riid, void **ppv );
    ULONG (WINAPI *AddRef)( void *This );
    ULONG (WINAPI *Release)( void *This );
    void *pad3_7[5];
    HRESULT (WINAPI *ProcessBindings)( void *This, void *args, INT32 *phase );  /* slot 8 */
};
struct abi_data_template_extension { const struct abi_data_template_extension_vtbl *lpVtbl; };

struct abi_fe_overrides_vtbl
{
    HRESULT (WINAPI *QueryInterface)( void *This, REFIID riid, void **ppv );
    ULONG (WINAPI *AddRef)( void *This );
    ULONG (WINAPI *Release)( void *This );
    void *pad3_5[3];
    HRESULT (WINAPI *MeasureOverride)( void *This, struct uwp_size_f avail, struct uwp_size_f *desired ); /* slot 6 */
    HRESULT (WINAPI *ArrangeOverride)( void *This, struct uwp_size_f final_sz, struct uwp_size_f *arranged ); /* slot 7 */
    HRESULT (WINAPI *OnApplyTemplate)( void *This );                            /* slot 8 */
};
struct abi_fe_overrides { const struct abi_fe_overrides_vtbl *lpVtbl; };

struct abi_page_overrides_vtbl
{
    HRESULT (WINAPI *QueryInterface)( void *This, REFIID riid, void **ppv );
    ULONG (WINAPI *AddRef)( void *This );
    ULONG (WINAPI *Release)( void *This );
    void *pad3_6[4];
    HRESULT (WINAPI *OnNavigatedTo)( void *This, void *args );                  /* slot 7 */
};
struct abi_page_overrides { const struct abi_page_overrides_vtbl *lpVtbl; };

struct abi_delegating_inner_vtbl
{
    HRESULT (WINAPI *QueryInterface)( void *This, REFIID riid, void **ppv );
    ULONG (WINAPI *AddRef)( void *This );
    ULONG (WINAPI *Release)( void *This );
    void *pad3_5[3];
    HRESULT (WINAPI *get_Inner)( void *This, void **out );                      /* slot 6 */
    HRESULT (WINAPI *get_InnerIU)( void *This, void **out );                    /* slot 7 */
};
struct abi_delegating_inner { const struct abi_delegating_inner_vtbl *lpVtbl; };

struct abi_font_family_vtbl
{
    HRESULT (WINAPI *QueryInterface)( void *This, REFIID riid, void **ppv );
    ULONG (WINAPI *AddRef)( void *This );
    ULONG (WINAPI *Release)( void *This );
    void *pad3_5[3];
    HRESULT (WINAPI *get_Source)( void *This, HSTRING *out );                   /* slot 6 */
};
struct abi_font_family { const struct abi_font_family_vtbl *lpVtbl; };

struct abi_property_path_vtbl
{
    HRESULT (WINAPI *QueryInterface)( void *This, REFIID riid, void **ppv );
    ULONG (WINAPI *AddRef)( void *This );
    ULONG (WINAPI *Release)( void *This );
    void *pad3_5[3];
    HRESULT (WINAPI *get_Path)( void *This, HSTRING *out );                     /* slot 6 */
};
struct abi_property_path { const struct abi_property_path_vtbl *lpVtbl; };

struct abi_iref_typename_vtbl
{
    HRESULT (WINAPI *QueryInterface)( void *This, REFIID riid, void **ppv );
    ULONG (WINAPI *AddRef)( void *This );
    ULONG (WINAPI *Release)( void *This );
    void *pad3_5[3];
    HRESULT (WINAPI *get_Value)( void *This, TypeName *out );                   /* slot 6 */
};
struct abi_iref_typename { const struct abi_iref_typename_vtbl *lpVtbl; };

struct abi_iref_ptr_vtbl
{
    HRESULT (WINAPI *QueryInterface)( void *This, REFIID riid, void **ppv );
    ULONG (WINAPI *AddRef)( void *This );
    ULONG (WINAPI *Release)( void *This );
    void *pad3_5[3];
    HRESULT (WINAPI *get_Value)( void *This, void **out );                      /* slot 6 */
};
struct abi_iref_ptr { const struct abi_iref_ptr_vtbl *lpVtbl; };

struct abi_iref_bool_vtbl
{
    HRESULT (WINAPI *QueryInterface)( void *This, REFIID riid, void **ppv );
    ULONG (WINAPI *AddRef)( void *This );
    ULONG (WINAPI *Release)( void *This );
    void *pad3_5[3];
    HRESULT (WINAPI *get_Value)( void *This, boolean *out );                    /* slot 6 */
};
struct abi_iref_bool { const struct abi_iref_bool_vtbl *lpVtbl; };

struct abi_value_converter_vtbl
{
    HRESULT (WINAPI *QueryInterface)( void *This, REFIID riid, void **ppv );
    ULONG (WINAPI *AddRef)( void *This );
    ULONG (WINAPI *Release)( void *This );
    void *pad3_5[3];
    HRESULT (WINAPI *Convert)( void *This, IInspectable *value, TypeName targetType,
                               IInspectable *parameter, HSTRING language, IInspectable **result ); /* slot 6 */
    HRESULT (WINAPI *ConvertBack)( void *This, IInspectable *value, TypeName targetType,
                                   IInspectable *parameter, HSTRING language, IInspectable **result ); /* slot 7 */
};
struct abi_value_converter { const struct abi_value_converter_vtbl *lpVtbl; };

struct abi_command_full_vtbl
{
    HRESULT (WINAPI *QueryInterface)( void *This, REFIID riid, void **ppv );
    ULONG (WINAPI *AddRef)( void *This );
    ULONG (WINAPI *Release)( void *This );
    void *pad3_5[3];
    HRESULT (WINAPI *add_CanExecuteChanged)( void *This, void *handler, EventRegistrationToken *tok ); /* slot 6 */
    HRESULT (WINAPI *remove_CanExecuteChanged)( void *This, EventRegistrationToken tok );           /* slot 7 */
    HRESULT (WINAPI *CanExecute)( void *This, void *parameter, boolean *returnValue );               /* slot 8 */
    HRESULT (WINAPI *Execute)( void *This, void *parameter );                                        /* slot 9 */
};
struct abi_command_full { const struct abi_command_full_vtbl *lpVtbl; };

struct abi_data_template_vtbl
{
    HRESULT (WINAPI *QueryInterface)( void *This, REFIID riid, void **ppv );
    ULONG (WINAPI *AddRef)( void *This );
    ULONG (WINAPI *Release)( void *This );
    void *pad3_5[3];
    HRESULT (WINAPI *LoadContent)( void *This, void **out );                    /* slot 6 */
};
struct abi_data_template { const struct abi_data_template_vtbl *lpVtbl; };

struct abi_data_template_selector_vtbl
{
    HRESULT (WINAPI *QueryInterface)( void *This, REFIID riid, void **ppv );
    ULONG (WINAPI *AddRef)( void *This );
    ULONG (WINAPI *Release)( void *This );
    void *pad3_5[3];
    HRESULT (WINAPI *SelectTemplate)( void *This, void *item, void *container, IInspectable **out ); /* slot 6 */
};
struct abi_data_template_selector { const struct abi_data_template_selector_vtbl *lpVtbl; };

struct abi_data_template_selector2_vtbl
{
    HRESULT (WINAPI *QueryInterface)( void *This, REFIID riid, void **ppv );
    ULONG (WINAPI *AddRef)( void *This );
    ULONG (WINAPI *Release)( void *This );
    void *pad3_5[3];
    HRESULT (WINAPI *SelectTemplateForItem)( void *This, void *item, IInspectable **out ); /* slot 6 */
};
struct abi_data_template_selector2 { const struct abi_data_template_selector2_vtbl *lpVtbl; };

struct abi_dts_overrides_vtbl
{
    HRESULT (WINAPI *QueryInterface)( void *This, REFIID riid, void **ppv );
    ULONG (WINAPI *AddRef)( void *This );
    ULONG (WINAPI *Release)( void *This );
    void *pad3_5[3];
    HRESULT (WINAPI *SelectTemplateCore)( void *This, void *item, void *container, void **out ); /* slot 6 */
};
struct abi_dts_overrides { const struct abi_dts_overrides_vtbl *lpVtbl; };

struct abi_dts_overrides2_vtbl
{
    HRESULT (WINAPI *QueryInterface)( void *This, REFIID riid, void **ppv );
    ULONG (WINAPI *AddRef)( void *This );
    ULONG (WINAPI *Release)( void *This );
    void *pad3_5[3];
    HRESULT (WINAPI *SelectTemplateForItemCore)( void *This, void *item, void **out ); /* slot 6 */
};
struct abi_dts_overrides2 { const struct abi_dts_overrides2_vtbl *lpVtbl; };

struct abi_markup_ext_overrides_vtbl
{
    HRESULT (WINAPI *QueryInterface)( void *This, REFIID riid, void **ppv );
    ULONG (WINAPI *AddRef)( void *This );
    ULONG (WINAPI *Release)( void *This );
    void *pad3_5[3];
    HRESULT (WINAPI *ProvideValue)( void *This, IInspectable **out ); /* slot 6 */
};
struct abi_markup_ext_overrides { const struct abi_markup_ext_overrides_vtbl *lpVtbl; };

struct abi_tab_item_vm_vtbl
{
    HRESULT (WINAPI *QueryInterface)( void *This, REFIID riid, void **ppv );
    ULONG (WINAPI *AddRef)( void *This );
    ULONG (WINAPI *Release)( void *This );
    void *pad3_5[3];
    HRESULT (WINAPI *get_Name)( void *This, HSTRING *name );                    /* slot 6 */
};
struct abi_tab_item_vm { const struct abi_tab_item_vm_vtbl *lpVtbl; };

struct abi_run_vtbl
{
    HRESULT (WINAPI *QueryInterface)( void *This, REFIID riid, void **ppv );
    ULONG (WINAPI *AddRef)( void *This );
    ULONG (WINAPI *Release)( void *This );
    void *pad3_5[3];
    HRESULT (WINAPI *get_Text)( void *This, HSTRING *out );                     /* slot 6 */
};
struct abi_run { const struct abi_run_vtbl *lpVtbl; };

struct abi_span_vtbl
{
    HRESULT (WINAPI *QueryInterface)( void *This, REFIID riid, void **ppv );
    ULONG (WINAPI *AddRef)( void *This );
    ULONG (WINAPI *Release)( void *This );
    void *pad3_5[3];
    HRESULT (WINAPI *get_Inlines)( void *This, void **out );                    /* slot 6 */
};
struct abi_span { const struct abi_span_vtbl *lpVtbl; };

struct abi_menu_bar_vtbl
{
    HRESULT (WINAPI *QueryInterface)( void *This, REFIID riid, void **ppv );
    ULONG (WINAPI *AddRef)( void *This );
    ULONG (WINAPI *Release)( void *This );
    void *pad3_5[3];
    HRESULT (WINAPI *get_Items)( void *This, void **out );                      /* slot 6 */
};
struct abi_menu_bar { const struct abi_menu_bar_vtbl *lpVtbl; };

struct abi_menu_bar_item_vtbl
{
    HRESULT (WINAPI *QueryInterface)( void *This, REFIID riid, void **ppv );
    ULONG (WINAPI *AddRef)( void *This );
    ULONG (WINAPI *Release)( void *This );
    void *pad3_7[5];
    HRESULT (WINAPI *get_Items)( void *This, void **out );                      /* slot 8 */
};
struct abi_menu_bar_item { const struct abi_menu_bar_item_vtbl *lpVtbl; };

struct abi_component_connector_vtbl
{
    HRESULT (WINAPI *QueryInterface)( void *This, REFIID riid, void **ppv );
    ULONG (WINAPI *AddRef)( void *This );
    ULONG (WINAPI *Release)( void *This );
    void *pad3_5[3];
    HRESULT (WINAPI *Connect)( void *This, int cid, void *target );             /* slot 6 */
};
struct abi_component_connector { const struct abi_component_connector_vtbl *lpVtbl; };

struct abi_component_connector2_vtbl
{
    HRESULT (WINAPI *QueryInterface)( void *This, REFIID riid, void **ppv );
    ULONG (WINAPI *AddRef)( void *This );
    ULONG (WINAPI *Release)( void *This );
    void *pad3_5[3];
    HRESULT (WINAPI *GetBindingConnector)( void *This, int cid, void *target, void **out ); /* slot 6 */
};
struct abi_component_connector2 { const struct abi_component_connector2_vtbl *lpVtbl; };

struct abi_component_connector_winui_vtbl
{
    HRESULT (WINAPI *QueryInterface)( void *This, REFIID riid, void **ppv );
    ULONG (WINAPI *AddRef)( void *This );
    ULONG (WINAPI *Release)( void *This );
    void *pad3_5[3];
    HRESULT (WINAPI *Connect)( void *This, int cid, void *target );             /* slot 6 */
    HRESULT (WINAPI *GetBindingConnector)( void *This, int cid, void *target, void **out ); /* slot 7 */
};
struct abi_component_connector_winui { const struct abi_component_connector_winui_vtbl *lpVtbl; };

struct abi_ctrl_overrides_vtbl
{
    HRESULT (WINAPI *QueryInterface)( void *This, REFIID riid, void **ppv );
    ULONG (WINAPI *AddRef)( void *This );
    ULONG (WINAPI *Release)( void *This );
    void *pad3_13[11];
    HRESULT (WINAPI *OnTapped)( void *This, void *args );                       /* slot 14 */
};
struct abi_ctrl_overrides { const struct abi_ctrl_overrides_vtbl *lpVtbl; };

#endif /* __WINE_WINDOWS_UI_XAML_UWP_ABI_VTBLS_H */
