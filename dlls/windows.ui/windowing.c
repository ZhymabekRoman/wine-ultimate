/* Windows App SDK windowing identifiers
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

#include "windows.h"
#include "wingdi.h"
#include "winuser.h"
#include "winerror.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(ui);

/* Identifiers handed to and from the Windows App SDK through the
 * ext-ms-win-windowing-internal-l1-1-0 apiset.  They are opaque values that the caller only
 * round-trips, so Wine returns the window and icon handles themselves - both live in a single
 * process wide handle table - and derives display identifiers from the display monitor
 * enumeration order of a host presenting one display adapter. */

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

static const LUID wine_display_adapter_luid = { 1, 0 };

HRESULT WINAPI Windowing_GetWindowIdFromWindow( HWND hwnd, WINWINDOWID *id )
{
    TRACE( "hwnd %p, id %p.\n", hwnd, id );

    if (!id)
        return E_INVALIDARG;
    if (!hwnd || !IsWindow( hwnd ))
        return HRESULT_FROM_WIN32( ERROR_INVALID_WINDOW_HANDLE );

    id->value = (UINT64)(UINT_PTR)hwnd;
    return S_OK;
}

HRESULT WINAPI Windowing_GetWindowFromWindowId( WINWINDOWID id, HWND *hwnd )
{
    TRACE( "id %s, hwnd %p.\n", wine_dbgstr_longlong( id.value ), hwnd );

    if (!hwnd)
        return E_INVALIDARG;

    *hwnd = NULL;

    if (!id.value || !IsWindow( (HWND)(UINT_PTR)id.value ))
        return HRESULT_FROM_WIN32( ERROR_INVALID_WINDOW_HANDLE );

    *hwnd = (HWND)(UINT_PTR)id.value;
    return S_OK;
}

HRESULT WINAPI Windowing_GetIconIdFromIcon( HICON icon, WINICONID *id )
{
    ICONINFO info;

    TRACE( "icon %p, id %p.\n", icon, id );

    if (!id)
        return E_INVALIDARG;
    if (!icon || !GetIconInfo( icon, &info ))
        return HRESULT_FROM_WIN32( ERROR_INVALID_HANDLE );

    if (info.hbmMask)
        DeleteObject( info.hbmMask );
    if (info.hbmColor)
        DeleteObject( info.hbmColor );

    id->value = (UINT64)(UINT_PTR)icon;
    return S_OK;
}

HRESULT WINAPI Windowing_GetIconFromIconId( WINICONID id, HICON *icon )
{
    ICONINFO info;

    TRACE( "id %s, icon %p.\n", wine_dbgstr_longlong( id.value ), icon );

    if (!icon)
        return E_INVALIDARG;

    *icon = NULL;

    if (!id.value || !GetIconInfo( (HICON)(UINT_PTR)id.value, &info ))
        return HRESULT_FROM_WIN32( ERROR_INVALID_HANDLE );

    if (info.hbmMask)
        DeleteObject( info.hbmMask );
    if (info.hbmColor)
        DeleteObject( info.hbmColor );

    *icon = (HICON)(UINT_PTR)id.value;
    return S_OK;
}

struct monitor_index_info
{
    HMONITOR monitor;
    UINT64 index;
    BOOL found;
};

static BOOL CALLBACK find_monitor_index( HMONITOR monitor, HDC dc, RECT *rect, LPARAM lparam )
{
    struct monitor_index_info *info = (void *)lparam;

    if (info->monitor == monitor)
    {
        info->found = TRUE;
        return FALSE;
    }

    info->index++;
    return TRUE;
}

HRESULT WINAPI Windowing_GetDisplayIdFromMonitor( HMONITOR monitor, WINDISPLAYID *id )
{
    struct monitor_index_info info = { monitor, 0, FALSE };

    TRACE( "monitor %p, id %p.\n", monitor, id );

    if (!id)
        return E_INVALIDARG;
    if (!monitor)
        return E_INVALIDARG;

    EnumDisplayMonitors( NULL, NULL, find_monitor_index, (LPARAM)&info );
    if (!info.found)
        return HRESULT_FROM_WIN32( ERROR_INVALID_MONITOR_HANDLE );

    id->luid = wine_display_adapter_luid;
    id->target_id = info.index + 1;
    return S_OK;
}

struct index_monitor_info
{
    UINT64 index;
    HMONITOR monitor;
};

static BOOL CALLBACK find_index_monitor( HMONITOR monitor, HDC dc, RECT *rect, LPARAM lparam )
{
    struct index_monitor_info *info = (void *)lparam;

    if (info->index)
    {
        info->index--;
        return TRUE;
    }

    info->monitor = monitor;
    return FALSE;
}

HRESULT WINAPI Windowing_GetMonitorFromDisplayId( const WINDISPLAYID *id, HMONITOR *monitor )
{
    struct index_monitor_info info = { 0, NULL };

    TRACE( "id %s, monitor %p.\n", id ? wine_dbgstr_longlong( id->target_id ) : "(nil)", monitor );

    if (!id || !monitor)
        return E_INVALIDARG;
    if (!id->target_id)
        return E_INVALIDARG;

    *monitor = NULL;

    if (id->luid.HighPart != wine_display_adapter_luid.HighPart || id->luid.LowPart != wine_display_adapter_luid.LowPart)
        return HRESULT_FROM_WIN32( ERROR_INVALID_MONITOR_HANDLE );

    info.index = id->target_id - 1;
    EnumDisplayMonitors( NULL, NULL, find_index_monitor, (LPARAM)&info );
    if (!info.monitor)
        return HRESULT_FROM_WIN32( ERROR_INVALID_MONITOR_HANDLE );

    *monitor = info.monitor;
    return S_OK;
}
