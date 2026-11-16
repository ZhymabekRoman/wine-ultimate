/* WinRT Windows.UI.Xaml Implementation
 *
 * Copyright (C) 2025 Mohamad Al-Jaf
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

#ifndef __WINE_WINDOWS_UI_XAML_PRIVATE_H
#define __WINE_WINDOWS_UI_XAML_PRIVATE_H

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#define COBJMACROS
#include "windef.h"
#include "winbase.h"
#include "winstring.h"

#include "activation.h"
#include "roapi.h"
#include "windowscontracts.h"

#include "wine/debug.h"

#define WIDL_using_Windows_Foundation
#define WIDL_using_Windows_Foundation_Collections
#define WIDL_using_Windows_Devices_Input
#define WIDL_using_Windows_UI
#define WIDL_using_Windows_UI_Input
#define WIDL_using_Windows_UI_Core
#define WIDL_using_Windows_ApplicationModel_Core
#define WIDL_using_Windows_UI_Xaml
#define WIDL_using_Windows_UI_Xaml_Controls
#define WIDL_using_Windows_UI_Xaml_Interop

#include "windows.foundation.h"
#include "windows.devices.input.h"
#include "windows.ui.h"
#include "windows.applicationmodel.core.h"
#include "windows.ui.input.h"
#include "windows.ui.core.h"
#include "windows.ui.xaml.h"

#include "dxgi.h"
#include <dxgi1_2.h>
#include <d3d11.h>
#include <d2d1_1.h>
#include <dcomp.h>
#include "windows.ui.xaml.media.dxinterop.h"

extern IActivationFactory *color_helper_factory;
extern IActivationFactory *size_helper_factory;
extern IActivationFactory *frame_factory;
extern IActivationFactory *page_factory;
extern IActivationFactory *grid_factory;
extern IActivationFactory *row_def_factory;
extern IActivationFactory *col_def_factory;
extern IActivationFactory *coll_factory;
extern IActivationFactory *swapchain_panel_factory;
extern IActivationFactory *application_factory;
extern IActivationFactory *window_factory;
extern IActivationFactory *property_metadata_factory;
extern IActivationFactory *dependency_property_factory;
extern IActivationFactory *webview2_factory;
extern IActivationFactory *user_control_factory;
extern IActivationFactory *content_control_factory;
extern IActivationFactory *element_factory;
extern IActivationFactory *resource_dictionary_factory;
extern IActivationFactory *canvas_factory;
HRESULT dependency_property_create( IPropertyMetadata *metadata, IDependencyProperty **out );
HRESULT xaml_create_page( void *type_name_ptr, IInspectable **out );
HRESULT xaml_create_dcomp_visual( IDCompositionVisual **out_visual );
HRESULT element_create( IInspectable *outer, IInspectable **inner, void **instance );
HRESULT element_create_typed( const WCHAR *class_name, void **instance );
HRESULT xaml_create_webview2( void **out );
void xaml_layout_render( HWND hwnd, HDC hdc, int width, int height, IInspectable *content );
HRESULT xaml_window_present_current( void );
void xaml_mark_window_dirty( void );
void xaml_fire_all_loaded( void );
void xaml_register_loaded( void *handler, IInspectable *sender );
#define WM_APP_WV2_START (WM_APP + 0x57)
#define XAML_DEFAULT_CLIENT_SIZE ((SIZE){ 1024, 768 })
void xaml_get_layout_metrics( HWND hwnd, SIZE *client, float *scale );
HWND xaml_get_window_hwnd( void );
HWND xaml_find_core_window( void );
void xaml_package_warm( void );
IDCompositionVisual *xaml_get_dcomp_root_visual( void );
void xaml_dcomp_commit( void );
void xaml_wv2_on_window_ready( HWND hwnd, UINT width, UINT height );
void xaml_wv2_on_window_resize( HWND hwnd, UINT width, UINT height );
void xaml_wv2_handle_start_msg( LPARAM lparam );
/* Runtime app capabilities, probed from the running application's own
 * COM/WinRT surface (IXamlMetadataProvider QI + registered XAML types),
 * not from package-family name tables. */
struct xaml_app_caps
{
    BOOL winui3;                 /* App answers IXamlMetadataProvider(WinUI3): Microsoft.UI.Xaml family */
    BOOL coreapp_seed;           /* CoreApplication-driven UWP app: seed tree + view lifecycle */
    WCHAR seed_page[64];         /* Short root-page name registered in the app provider, empty = no seed tree */
    BOOL teardown_vm;            /* uwp_xaml.c: DataContext-clear keeps the VM alive (provider-registered VM) */
    BOOL display_sync;           /* uwp_xaml.c: sync result display from VM (provider-registered VM) */
    WCHAR vm_type[128];          /* Full VM type name registered in the app provider, empty = none */
    WCHAR design_component[128]; /* Design-time panel registered in the app provider, empty = none */
};

const struct xaml_app_caps *xaml_get_app_caps( void );
HRESULT xaml_metadata_cache_get_type( const WCHAR *full_name, void **out );
HRESULT xaml_metadata_cache_get_type_hstring( HSTRING full_name, void **out );
WORD xaml_metadata_cache_find_xbf_id( const WCHAR *full_name );
void *uwp_get_app_outer( void );
HRESULT uwp_create_instance_from_provider( const WCHAR *full_name, void **out );
HRESULT uwp_element_create( const WCHAR *class_name, IInspectable *outer, IInspectable **inner, void **instance );
HRESULT xaml_resource_dictionary_create_for_uwp( void **out );
HRESULT uwp_rd_lookup( void *rd_key, void *key_obj, void **out_val );
BOOL uwp_rd_haskey( void *rd_key, void *key_obj );
BOOL uwp_try_load_xbf_component( void *component, void *resourceLocator, void *app_outer );
BOOL uwp_handle_mouse_input( HWND hwnd, UINT msg, int x, int y, int width, int height, IInspectable *root_content );
HRESULT uwp_get_activation_factory( HSTRING classid, IActivationFactory **factory );
ICoreWindow *get_current_core_window( void );
BOOL uwp_pri_find_xbf( const WCHAR *uri_or_name, const BYTE **out_ptr, DWORD *out_sz );

/* Shared token-based WinRT event registry (twinapi bg_events pattern) */
struct xaml_event_entry
{
    void *handler;
    INT64 token;
};

struct xaml_event_registry
{
    struct xaml_event_entry entries[64];
    INT64 next_token;
};

HRESULT xaml_event_registry_add( struct xaml_event_registry *reg, void *handler, EventRegistrationToken *token );
HRESULT xaml_event_registry_remove( struct xaml_event_registry *reg, EventRegistrationToken token );
unsigned int xaml_event_registry_snapshot( struct xaml_event_registry *reg, void **out, unsigned int max_out );
void xaml_event_registry_clear( struct xaml_event_registry *reg );

/* Realloc-backed growable vector helper (grows capacity x2) */
static inline BOOL xaml_vec_grow( void **items, int *cap, int needed, size_t elem_sz, int init_cap )
{
    int ncap;
    void *nbuf;
    if (needed <= *cap) return TRUE;
    ncap = *cap > 0 ? *cap : (init_cap > 0 ? init_cap : 8);
    while (ncap < needed) ncap *= 2;
    nbuf = realloc( *items, (size_t)ncap * elem_sz );
    if (!nbuf) return FALSE;
    if (ncap > *cap)
        memset( (char *)nbuf + (size_t)(*cap) * elem_sz, 0, (size_t)(ncap - *cap) * elem_sz );
    *items = nbuf;
    *cap = ncap;
    return TRUE;
}

enum xaml_pointer_event_kind
{
    XAML_PTR_PRESSED = 0,
    XAML_PTR_MOVED,
    XAML_PTR_RELEASED,
    XAML_PTR_WHEEL,
    XAML_PTR_ENTERED,
    XAML_PTR_EXITED,
    XAML_PTR_CAPTURE_LOST,
};

struct xaml_pointer_info
{
    float x;
    float y;
    UINT32 pointer_id;
    UINT32 key_modifiers;
    INT32 wheel_delta;
    BOOL is_in_contact;
    BOOL is_in_window;
    BOOL is_left;
    BOOL is_right;
    BOOL is_middle;
    PointerUpdateKind update_kind;
};

IPointerPoint *xaml_pointer_point_create( const struct xaml_pointer_info *info, float rel_x, float rel_y );
__x_ABI_CWindows_CUI_CCore_CIPointerEventArgs *xaml_core_pointer_args_create( const struct xaml_pointer_info *info );
BOOL uwp_route_pointer_event( HWND hwnd, enum xaml_pointer_event_kind kind, const struct xaml_pointer_info *info,
                              int width, int height, IInspectable *root_content );

#define DEFINE_IINSPECTABLE_( pfx, iface_type, impl_type, impl_from, iface_mem, expr )             \
    static inline impl_type *impl_from( iface_type *iface )                                        \
    {                                                                                              \
        return CONTAINING_RECORD( iface, impl_type, iface_mem );                                   \
    }                                                                                              \
    static HRESULT WINAPI pfx##_QueryInterface( iface_type *iface, REFIID iid, void **out )        \
    {                                                                                              \
        impl_type *impl = impl_from( iface );                                                      \
        return IInspectable_QueryInterface( (IInspectable *)(expr), iid, out );                    \
    }                                                                                              \
    static ULONG WINAPI pfx##_AddRef( iface_type *iface )                                          \
    {                                                                                              \
        impl_type *impl = impl_from( iface );                                                      \
        return IInspectable_AddRef( (IInspectable *)(expr) );                                      \
    }                                                                                              \
    static ULONG WINAPI pfx##_Release( iface_type *iface )                                         \
    {                                                                                              \
        impl_type *impl = impl_from( iface );                                                      \
        return IInspectable_Release( (IInspectable *)(expr) );                                     \
    }                                                                                              \
    static HRESULT WINAPI pfx##_GetIids( iface_type *iface, ULONG *iid_count, IID **iids )         \
    {                                                                                              \
        impl_type *impl = impl_from( iface );                                                      \
        return IInspectable_GetIids( (IInspectable *)(expr), iid_count, iids );                    \
    }                                                                                              \
    static HRESULT WINAPI pfx##_GetRuntimeClassName( iface_type *iface, HSTRING *class_name )      \
    {                                                                                              \
        impl_type *impl = impl_from( iface );                                                      \
        return IInspectable_GetRuntimeClassName( (IInspectable *)(expr), class_name );             \
    }                                                                                              \
    static HRESULT WINAPI pfx##_GetTrustLevel( iface_type *iface, TrustLevel *trust_level )        \
    {                                                                                              \
        impl_type *impl = impl_from( iface );                                                      \
        return IInspectable_GetTrustLevel( (IInspectable *)(expr), trust_level );                  \
    }
#define DEFINE_IINSPECTABLE( pfx, iface_type, impl_type, base_iface )                              \
    DEFINE_IINSPECTABLE_( pfx, iface_type, impl_type, impl_from_##iface_type, iface_type##_iface, &impl->base_iface )

#endif
