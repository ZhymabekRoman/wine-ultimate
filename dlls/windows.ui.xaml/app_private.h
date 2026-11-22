/* WinRT Windows.UI.Xaml Application family: shared internals for
 * application.c, core_input.c, core_window_wrap.c and app_identity.c.
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

#ifndef __WINE_WINDOWS_UI_XAML_APP_PRIVATE_H
#define __WINE_WINDOWS_UI_XAML_APP_PRIVATE_H

#include "private.h"

/* ---- Application object (application.c) ---- */
struct application
{
    IApplication IApplication_iface;
    IInspectable inner_iface;
    IInspectable ms_IApplication_iface;
    IInspectable overrides_uwp_iface;
    IInspectable metadata_provider_iface;
    LONG ref;
    IInspectable *outer;
    IInspectable *resources;   /* lazy Windows.Foundation.Collections.PropertySet for the Resources QI set */
};
extern struct application *current_application;

/* ---- ICoreWindowInterop (not in the generated headers) ---- */
typedef struct ICoreWindowInterop ICoreWindowInterop;
typedef struct ICoreWindowInteropVtbl
{
    HRESULT (WINAPI *QueryInterface)( ICoreWindowInterop *This, REFIID riid, void **ppvObject );
    ULONG (WINAPI *AddRef)( ICoreWindowInterop *This );
    ULONG (WINAPI *Release)( ICoreWindowInterop *This );
    HRESULT (WINAPI *GetWindowHandle)( ICoreWindowInterop *This, HWND *hwnd );
    HRESULT (WINAPI *SetMessageHandled)( ICoreWindowInterop *This, boolean value );
} ICoreWindowInteropVtbl;
struct ICoreWindowInterop { const ICoreWindowInteropVtbl *lpVtbl; };

/* ---- CoreWindow wrapper (core_window_wrap.c) ---- */
struct xaml_core_window
{
    ICoreWindow ICoreWindow_iface;
    ICoreWindowInterop ICoreWindowInterop_iface;
    LONG ref;
    ICoreWindow *inner;
    HWND hwnd;
    POINT last_pos;
    BOOL has_pos;
    BOOL pointer_inside;
    BOOL has_capture;
    struct xaml_event_registry pointer_pressed;
    struct xaml_event_registry pointer_moved;
    struct xaml_event_registry pointer_released;
    struct xaml_event_registry pointer_wheel_changed;
    struct xaml_event_registry pointer_entered;
    struct xaml_event_registry pointer_exited;
    struct xaml_event_registry pointer_capture_lost;
    struct xaml_event_registry key_down;
    struct xaml_event_registry key_up;
    struct xaml_event_registry character_received;
    EventRegistrationToken key_down_inner_tokens[64];
    EventRegistrationToken key_up_inner_tokens[64];
    EventRegistrationToken character_received_inner_tokens[64];
};

struct xaml_core_window *as_xaml_core_window( ICoreWindow *cw );
ICoreWindow *wrap_core_window( ICoreWindow *inner );

/* ---- Input event plumbing (core_input.c) ---- */
extern CRITICAL_SECTION xaml_event_cs;
void xaml_cw_fire_key_event( struct xaml_core_window *cw, UINT msg, WPARAM wparam, LPARAM lparam );
void xaml_cw_fire_pointer_event( struct xaml_core_window *cw, enum xaml_pointer_event_kind kind,
                                 const struct xaml_pointer_info *info );
HRESULT notify_inner_core_window_key( HWND hwnd, UINT msg, WPARAM wparam );
extern const GUID IID_ICoreWindowInterop;

/* ---- Package identity (app_identity.c) ---- */
void xaml_apply_window_icon( HWND top );
const WCHAR *xaml_package_display_name( void );
extern const GUID IID_IXamlMetadataProvider_WinUI;
extern const GUID IID_IXamlMetadataProvider_UWP;
extern const void *app_meta_provider_vtbl[];

#endif /* __WINE_WINDOWS_UI_XAML_APP_PRIVATE_H */
