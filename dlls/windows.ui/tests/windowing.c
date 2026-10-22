/*
 * Copyright 2026 wine-ultimate contributors
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

#include <stdarg.h>

#define COBJMACROS
#include "windef.h"
#include "unknwn.h"
#include "winbase.h"
#include "winuser.h"
#include "winerror.h"
#include "winstring.h"
#include "wine/test.h"

typedef struct
{
    UINT64 value;
} WINWINDOWID;

typedef struct
{
    UINT64 value;
} WINICONID;

typedef struct
{
    LUID luid;
    UINT64 target_id;
} WINDISPLAYID;

typedef HRESULT (WINAPI *get_window_id_from_window_func)(HWND, WINWINDOWID *);
typedef HRESULT (WINAPI *get_window_from_window_id_func)(WINWINDOWID, HWND *);
typedef HRESULT (WINAPI *get_icon_id_from_icon_func)(HICON, WINICONID *);
typedef HRESULT (WINAPI *get_icon_from_icon_id_func)(WINICONID, HICON *);
typedef HRESULT (WINAPI *get_display_id_from_monitor_func)(HMONITOR, WINDISPLAYID *);
typedef HRESULT (WINAPI *get_monitor_from_display_id_func)(const WINDISPLAYID *, HMONITOR *);
typedef HRESULT (WINAPI *get_activation_factory_func)(HSTRING, IUnknown **);

static get_window_id_from_window_func pWindowing_GetWindowIdFromWindow;
static get_window_from_window_id_func pWindowing_GetWindowFromWindowId;
static get_icon_id_from_icon_func pWindowing_GetIconIdFromIcon;
static get_icon_from_icon_id_func pWindowing_GetIconFromIconId;
static get_display_id_from_monitor_func pWindowing_GetDisplayIdFromMonitor;
static get_monitor_from_display_id_func pWindowing_GetMonitorFromDisplayId;
static get_activation_factory_func pDllGetActivationFactory;

static void init_func_ptrs( HMODULE module )
{
    pWindowing_GetWindowIdFromWindow = (void *)GetProcAddress( module, "Windowing_GetWindowIdFromWindow" );
    pWindowing_GetWindowFromWindowId = (void *)GetProcAddress( module, "Windowing_GetWindowFromWindowId" );
    pWindowing_GetIconIdFromIcon = (void *)GetProcAddress( module, "Windowing_GetIconIdFromIcon" );
    pWindowing_GetIconFromIconId = (void *)GetProcAddress( module, "Windowing_GetIconFromIconId" );
    pWindowing_GetDisplayIdFromMonitor = (void *)GetProcAddress( module, "Windowing_GetDisplayIdFromMonitor" );
    pWindowing_GetMonitorFromDisplayId = (void *)GetProcAddress( module, "Windowing_GetMonitorFromDisplayId" );
    pDllGetActivationFactory = (void *)GetProcAddress( module, "DllGetActivationFactory" );
    ok( pWindowing_GetWindowIdFromWindow && pWindowing_GetWindowFromWindowId
        && pWindowing_GetIconIdFromIcon && pWindowing_GetIconFromIconId
        && pWindowing_GetDisplayIdFromMonitor && pWindowing_GetMonitorFromDisplayId
        && pDllGetActivationFactory, "Missing windowing exports.\n" );
}

static void test_window_ids(void)
{
    WINWINDOWID id = { 0 }, id2 = { 0 };
    HWND hwnd, hwnd2 = NULL;
    HRESULT hr;

    hwnd = CreateWindowExW( 0, L"static", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, NULL, NULL );
    ok( hwnd != NULL, "Failed to create window, error %#lx.\n", GetLastError() );

    hr = pWindowing_GetWindowIdFromWindow( hwnd, &id );
    ok( hr == S_OK, "Unexpected hr %#lx.\n", hr );
    ok( id.value == (UINT64)(UINT_PTR)hwnd, "Got id %s, expected the window handle.\n",
        wine_dbgstr_longlong( id.value ) );

    hr = pWindowing_GetWindowFromWindowId( id, &hwnd2 );
    ok( hr == S_OK, "Unexpected hr %#lx.\n", hr );
    ok( hwnd2 == hwnd, "Got window %p, expected %p.\n", hwnd2, hwnd );

    id2.value = id.value;
    hr = pWindowing_GetWindowFromWindowId( id2, &hwnd2 );
    ok( hr == S_OK, "Unexpected hr %#lx.\n", hr );
    ok( hwnd2 == hwnd, "Got window %p, expected %p.\n", hwnd2, hwnd );

    hwnd2 = (HWND)0xdeadbeef;
    id.value = 0;
    hr = pWindowing_GetWindowFromWindowId( id, &hwnd2 );
    ok( FAILED( hr ), "Unexpected hr %#lx.\n", hr );
    ok( hwnd2 == NULL, "Unexpected window %p.\n", hwnd2 );

    hr = pWindowing_GetWindowIdFromWindow( NULL, &id );
    ok( FAILED( hr ), "Unexpected hr %#lx.\n", hr );
    hr = pWindowing_GetWindowIdFromWindow( hwnd, NULL );
    ok( hr == E_INVALIDARG, "Unexpected hr %#lx.\n", hr );

    DestroyWindow( hwnd );

    hwnd2 = (HWND)0xdeadbeef;
    id.value = (UINT64)(UINT_PTR)hwnd;
    hr = pWindowing_GetWindowFromWindowId( id, &hwnd2 );
    ok( FAILED( hr ), "Unexpected hr %#lx for a destroyed window.\n", hr );
    ok( hwnd2 == NULL, "Unexpected window %p.\n", hwnd2 );
}

static void test_icon_ids(void)
{
    WINICONID id = { 0 }, id2 = { 0 };
    HICON icon, icon2 = NULL;
    HRESULT hr;

    icon = LoadIconW( NULL, MAKEINTRESOURCEW( IDI_APPLICATION ));
    ok( icon != NULL, "Failed to load an icon, error %#lx.\n", GetLastError() );
    if (!icon) return;

    hr = pWindowing_GetIconIdFromIcon( icon, &id );
    ok( hr == S_OK, "Unexpected hr %#lx.\n", hr );

    hr = pWindowing_GetIconFromIconId( id, &icon2 );
    ok( hr == S_OK, "Unexpected hr %#lx.\n", hr );
    id2.value = id.value;
    hr = pWindowing_GetIconFromIconId( id2, &icon2 );
    ok( hr == S_OK, "Unexpected hr %#lx.\n", hr );

    icon2 = (HICON)0xdeadbeef;
    id.value = 0;
    hr = pWindowing_GetIconFromIconId( id, &icon2 );
    ok( FAILED( hr ), "Unexpected hr %#lx.\n", hr );
    ok( icon2 == NULL, "Unexpected icon %p.\n", icon2 );

    hr = pWindowing_GetIconIdFromIcon( NULL, &id );
    ok( FAILED( hr ), "Unexpected hr %#lx.\n", hr );
    hr = pWindowing_GetIconIdFromIcon( icon, NULL );
    ok( hr == E_INVALIDARG, "Unexpected hr %#lx.\n", hr );
}

struct first_monitor_info
{
    HMONITOR monitor;
};

static BOOL CALLBACK capture_first_monitor( HMONITOR monitor, HDC dc, RECT *rect, LPARAM lparam )
{
    struct first_monitor_info *info = (void *)lparam;

    if (!info->monitor)
        info->monitor = monitor;
    return FALSE;
}

static void test_display_ids(void)
{
    struct first_monitor_info info = { NULL };
    WINDISPLAYID id, id2;
    HMONITOR monitor, monitor2 = NULL;
    HRESULT hr;

    EnumDisplayMonitors( NULL, NULL, capture_first_monitor, (LPARAM)&info );
    ok( info.monitor != NULL, "No display monitor reported.\n" );
    if (!info.monitor) return;

    hr = pWindowing_GetDisplayIdFromMonitor( info.monitor, &id );
    ok( hr == S_OK, "Unexpected hr %#lx.\n", hr );

    hr = pWindowing_GetMonitorFromDisplayId( &id, &monitor2 );
    ok( hr == S_OK, "Unexpected hr %#lx.\n", hr );
    ok( monitor2 == info.monitor, "Got monitor %p, expected %p.\n", monitor2, info.monitor );

    id2 = id;
    id2.target_id = 0;
    hr = pWindowing_GetMonitorFromDisplayId( &id2, &monitor2 );
    ok( FAILED( hr ), "Unexpected hr %#lx.\n", hr );
    hr = pWindowing_GetMonitorFromDisplayId( NULL, &monitor2 );
    ok( hr == E_INVALIDARG, "Unexpected hr %#lx.\n", hr );
    hr = pWindowing_GetDisplayIdFromMonitor( NULL, &id );
    ok( hr == E_INVALIDARG, "Unexpected hr %#lx.\n", hr );
    hr = pWindowing_GetDisplayIdFromMonitor( info.monitor, NULL );
    ok( hr == E_INVALIDARG, "Unexpected hr %#lx.\n", hr );
}

static void test_activation_factory( void )
{
    static const WCHAR uisettings_class[] = L"Windows.UI.ViewManagement.UISettings";
    static const WCHAR titlebar_class[] = L"Microsoft.UI.Windowing.AppWindowTitleBar";
    IUnknown *factory = NULL;
    HSTRING hstr;
    HRESULT hr;

    WindowsCreateString( uisettings_class, ARRAYSIZE(uisettings_class) - 1, &hstr );
    factory = NULL;
    hr = pDllGetActivationFactory( hstr, &factory );
    ok( hr == S_OK, "Unexpected hr %#lx.\n", hr );
    ok( factory != NULL, "No factory returned.\n" );
    if (factory) IUnknown_Release( factory );
    WindowsDeleteString( hstr );

    WindowsCreateString( titlebar_class, ARRAYSIZE(titlebar_class) - 1, &hstr );
    factory = (IUnknown *)0xdeadbeef;
    hr = pDllGetActivationFactory( hstr, &factory );
    ok( hr == E_NOINTERFACE, "Unexpected hr %#lx.\n", hr );
    ok( factory == NULL, "Unexpected factory %p.\n", factory );
    WindowsDeleteString( hstr );
}

START_TEST( windowing )
{
    HMODULE module;

    if (!(module = LoadLibraryA( "windows.ui.dll" )))
    {
        win_skip( "Failed to load windows.ui.dll.\n" );
        return;
    }

    init_func_ptrs( module );
    test_window_ids();
    test_icon_ids();
    test_display_ids();
    test_activation_factory();

    FreeLibrary( module );
}
