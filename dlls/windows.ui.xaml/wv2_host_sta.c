/* WinRT Windows.UI.Xaml.Controls.WebView2 bridge support
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

#include <stdarg.h>
#include <stdio.h>

#include "private.h"
#include "winreg.h"
#include "oleauto.h"
#include "weakreference.h"

#include "wine/exception.h"

WINE_DEFAULT_DEBUG_CHANNEL(xaml);
#include "uwp_private.h"
#include "wv2_private.h"


static HRESULT WINAPI cwv2_args_QueryInterface( IInspectable *iface, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_ICoreWebView2InitializedEventArgs ))
    {
        *out = iface;
        IInspectable_AddRef( iface );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI cwv2_args_AddRef( IInspectable *iface )
{
    struct cwv2_args *impl = (struct cwv2_args *)iface;
    return InterlockedIncrement( &impl->ref );
}
ULONG WINAPI cwv2_args_Release( IInspectable *iface )
{
    struct cwv2_args *impl = (struct cwv2_args *)iface;
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref) free( impl );
    return ref;
}
static HRESULT WINAPI cwv2_args_GetIids( IInspectable *iface, ULONG *count, IID **iids )
{ if (count) *count = 0; if (iids) *iids = NULL; return S_OK; }
static HRESULT WINAPI cwv2_args_GetRuntimeClassName( IInspectable *iface, HSTRING *name )
{ return WindowsCreateString( L"Microsoft.UI.Xaml.Controls.CoreWebView2InitializedEventArgs", 59, name ); }
static HRESULT WINAPI cwv2_args_GetTrustLevel( IInspectable *iface, TrustLevel *level )
{ if (level) *level = BaseTrust; return S_OK; }
static HRESULT WINAPI cwv2_args_get_Exception( void *iface, HRESULT *ex )
{
    if (ex) *ex = S_OK;
    return S_OK;
}

const void *cwv2_args_vtbl[256] =
{
    [0] = cwv2_args_QueryInterface,
    [1] = cwv2_args_AddRef,
    [2] = cwv2_args_Release,
    [3] = cwv2_args_GetIids,
    [4] = cwv2_args_GetRuntimeClassName,
    [5] = cwv2_args_GetTrustLevel,
    [7 ... 255] = element_generic_slot,
    [6] = cwv2_args_get_Exception,    /* 6: get_Exception */
};

HRESULT WINAPI wv2_get_CoreWebView2( void *iface, void **out )
{
    struct xaml_element *impl = impl_from_IWebView2( iface );
    ERR( "wv2_get_CoreWebView2 iface %p, out %p, core %p\n", iface, out, impl->core_webview2 );
    if (!out) return E_POINTER;
    *out = impl->core_webview2;
    if (*out) IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}


void wv2_start_browser( struct xaml_element *impl );

static void wv2_paste_clipboard_text( HWND hwnd );

struct xaml_element *g_active_wv2_impl;
HWND g_wv2_core_hwnd;
HWND g_wv2_host_hwnd;
HWND g_wv2_subclassed_hwnd;
static WNDPROC g_wv2_prev_wndproc;
HWND g_wv2_sta_hwnd;
static BOOL g_wv2_focused;
static BOOL g_wv2_ctrl_down;
static BOOL g_wv2_shift_down;
static BOOL g_wv2_alt_down;
#define WM_APP_WV2_WHEEL (WM_APP + 0x58)
#define WM_APP_WV2_MOUSE (WM_APP + 0x59)
#define WM_APP_WV2_KEY   (WM_APP + 0x5a)
#define WM_APP_WV2_CHAR  (WM_APP + 0x5b)
#define WM_APP_WV2_PASTE (WM_APP + 0x5c)
#define WM_APP_WV2_INIT  (WM_APP + 0x5d)
#define WM_APP_WV2_RESIZE (WM_APP + 0x5e)
#define WV2_CHILD_SYNC_TIMER_ID 0x57563201

static BOOL CALLBACK wv2_post_wheel_cb( HWND child, LPARAM lparam )
{
    WCHAR cls[128] = {0};
    const MSG *m = (const MSG *)lparam;

    GetClassNameW( child, cls, ARRAY_SIZE( cls ) );
    if (wcsstr( cls, L"Chrome_RenderWidgetHostHWND" ) || wcsstr( cls, L"Chrome_WidgetWin" ))
        PostMessageW( child, m->message, m->wParam, m->lParam );
    EnumChildWindows( child, wv2_post_wheel_cb, lparam );
    return TRUE;
}

static BOOL CALLBACK wv2_find_client_cb( HWND child, LPARAM lparam )
{
    WCHAR cls[128] = {0};
    DWORD pid = 0;

    GetWindowThreadProcessId( child, &pid );
    if (pid == GetCurrentProcessId())
    {
        GetClassNameW( child, cls, ARRAY_SIZE( cls ) );
        if (wcsstr( cls, L"Chrome_WidgetWin" ))
        {
            *(HWND *)lparam = child;
            return FALSE;
        }
    }
    EnumChildWindows( child, wv2_find_client_cb, lparam );
    return *(HWND *)lparam == NULL;
}

static HWND wv2_find_client_hwnd( HWND host )
{
    HWND vis, cli = NULL;

    if (host && IsWindow( host ))
    {
        vis = (HWND)GetPropW( host, L"ClientVisualWindow" );
        if (vis && IsWindow( vis ))
        {
            cli = (HWND)GetPropW( vis, L"ClientWindow" );
            if (cli && IsWindow( cli )) return cli;
        }
        EnumChildWindows( host, wv2_find_client_cb, (LPARAM)&cli );
        if (cli && IsWindow( cli )) return cli;
    }
    return NULL;
}

static void wv2_ensure_focus( struct xaml_element *impl, HWND host, BOOL force )
{
    HWND cli;

    if (!impl) return;
    if (g_wv2_focused && !force) return;
    cli = wv2_find_client_hwnd( host );
    if (cli && IsWindow( cli ))
    {
        SendMessageW( cli, WM_SETFOCUS, 0, 0 );
        g_wv2_focused = TRUE;
    }
    if (force && impl->loader_core && impl->real_loader)
    {
        /* Slot 29 on ICoreWebView2: ExecuteScript(LPCWSTR javaScript, ICoreWebView2ExecuteScriptCompletedHandler *handler) */
        HRESULT (WINAPI *exec_script)( void *, LPCWSTR, void * ) =
            wv2_slot_fn( impl->loader_core, TRUE, 29, -1 );
        if (exec_script)
        {
            exec_script( impl->loader_core,
                L"(function(){function f(){var a=document.activeElement;"
                L"if(!a||(a.tagName!=='INPUT'&&a.tagName!=='TEXTAREA'&&!a.isContentEditable)){"
                L"var el=document.querySelector('input:not([type=hidden]),textarea,[contenteditable=true]');"
                L"if(el)el.focus();}}f();setTimeout(f,200);setTimeout(f,600);setTimeout(f,1500);setTimeout(f,3000);})()",
                NULL );
        }
    }
}

static HRESULT wv2_sta_send_keyboard_input( struct xaml_element *impl, HWND host,
                                            UINT key_msg, UINT32 vk, UINT32 scan,
                                            UINT32 repeat, UINT32 mods, LPARAM lparam )
{
    HWND cli;
    BYTE ks[256];
    BOOL had_ks = FALSE;
    HRESULT hr = E_NOINTERFACE;

    if (!impl) return E_POINTER;
    wv2_ensure_focus( impl, host, (vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL) && key_msg == WM_KEYDOWN );
    cli = wv2_find_client_hwnd( host );
    if (key_msg == WM_KEYDOWN && vk == 'V' && (mods & MK_CONTROL))
    {
        /* Ctrl+V is handled by wv2_paste_clipboard_text via per-char SendCharacterInput;
         * skip forwarding raw Ctrl+V to ClientWindowWin32 to avoid double-pasting. */
        hr = (cli || impl->loader_comp_ctrl) ? S_OK : S_FALSE;
        TRACE( "wv2_sta_wndproc: SendKeyboardInput hr=%#lx msg=%#x vk=%#x scan=%#x repeat=%u mods=%#x cli=%p\n",
               hr, key_msg, vk, scan, repeat, mods, cli );
        return hr;
    }
    if (!(mods & MK_CONTROL) && GetKeyboardState( ks ))
    {
        BYTE clean_ks[256];
        memcpy( clean_ks, ks, sizeof(clean_ks) );
        clean_ks[VK_CONTROL] = clean_ks[VK_LCONTROL] = clean_ks[VK_RCONTROL] = 0;
        clean_ks[VK_MENU] = clean_ks[VK_LMENU] = clean_ks[VK_RMENU] = 0;
        SetKeyboardState( clean_ks );
        had_ks = TRUE;
    }
    if (cli && IsWindow( cli ))
    {
        SendMessageW( cli, key_msg, vk, lparam );
        hr = S_OK;
    }
    else if (host && IsWindow( host ))
    {
        MSG m = { host, key_msg, vk, lparam };
        EnumChildWindows( host, wv2_post_wheel_cb, (LPARAM)&m );
        hr = impl->loader_comp_ctrl ? S_OK : S_FALSE;
    }
    if (had_ks)
        SetKeyboardState( ks );
    TRACE( "wv2_sta_wndproc: SendKeyboardInput hr=%#lx msg=%#x vk=%#x scan=%#x repeat=%u mods=%#x cli=%p\n",
           hr, key_msg, vk, scan, repeat, mods, cli );
    return hr;
}

static HRESULT wv2_sta_send_character_input( struct xaml_element *impl, HWND host,
                                             WCHAR ch, BOOL keydown, LPARAM lparam )
{
    HWND cli;
    BYTE ks[256];
    BOOL had_ks = FALSE;
    HRESULT hr = E_NOINTERFACE;

    if (!impl) return E_POINTER;
    wv2_ensure_focus( impl, host, FALSE );
    cli = wv2_find_client_hwnd( host );
    if (GetKeyboardState( ks ))
    {
        BYTE clean_ks[256];
        memcpy( clean_ks, ks, sizeof(clean_ks) );
        clean_ks[VK_CONTROL] = clean_ks[VK_LCONTROL] = clean_ks[VK_RCONTROL] = 0;
        clean_ks[VK_MENU] = clean_ks[VK_LMENU] = clean_ks[VK_RMENU] = 0;
        SetKeyboardState( clean_ks );
        had_ks = TRUE;
    }
    if (cli && IsWindow( cli ))
    {
        SendMessageW( cli, WM_CHAR, (WPARAM)ch, lparam ? lparam : 1 );
        hr = S_OK;
    }
    else if (host && IsWindow( host ))
    {
        MSG m = { host, WM_CHAR, (WPARAM)ch, lparam ? lparam : 1 };
        EnumChildWindows( host, wv2_post_wheel_cb, (LPARAM)&m );
        hr = impl->loader_comp_ctrl ? S_OK : S_FALSE;
    }
    if (impl->loader_core && impl->real_loader)
    {
        HRESULT (WINAPI *exec_script)( void *, LPCWSTR, void * ) =
            wv2_slot_fn( impl->loader_core, TRUE, 29, -1 );
        if (exec_script)
        {
            WCHAR script[512];
            if (ch == '\b')
            {
                lstrcpyW( script,
                    L"(function(){var a=document.activeElement;"
                    L"if(!a||(a.tagName!=='INPUT'&&a.tagName!=='TEXTAREA'&&!a.isContentEditable)){"
                    L"a=document.querySelector('input:not([type=hidden]),textarea,[contenteditable=true]');"
                    L"if(a)a.focus();}"
                    L"if(a)document.execCommand('delete',false,null);})()" );
            }
            else if (ch >= 0x20)
            {
                swprintf( script, ARRAY_SIZE(script),
                    L"(function(){var a=document.activeElement;"
                    L"if(!a||(a.tagName!=='INPUT'&&a.tagName!=='TEXTAREA'&&!a.isContentEditable)){"
                    L"a=document.querySelector('input:not([type=hidden]),textarea,[contenteditable=true]');"
                    L"if(a)a.focus();}"
                    L"if(a)document.execCommand('insertText',false,'\\u%04x');})()",
                    (unsigned int)ch );
            }
            else
            {
                script[0] = 0;
            }
            if (script[0])
                exec_script( impl->loader_core, script, NULL );
            hr = S_OK;
        }
    }
    if (had_ks)
        SetKeyboardState( ks );
    TRACE( "wv2_sta_wndproc: SendCharacterInput hr=%#lx ch=%#x keydown=%d cli=%p\n",
           hr, (unsigned int)ch, keydown, cli );
    return hr;
}

static LRESULT CALLBACK wv2_sta_wndproc( HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam )
{
    if (msg == WM_TIMER && wparam == 0x5757)
    {
        static int sync_ticks;
        if (g_active_wv2_impl && g_active_wv2_impl->has_comp_ctrl)
            wv2_sync_child_hwnds( NULL, 0, 0 );
        if (++sync_ticks >= 15)
            KillTimer( hwnd, 0x5757 );
        return 0;
    }
    if (msg == WM_APP_WV2_RESIZE && g_active_wv2_impl)
    {
        LONG w = LOWORD( lparam ), h = HIWORD( lparam );
        if (w > 0 && h > 0)
        {
            IDCompositionVisual *root = xaml_get_dcomp_root_visual();
            D2D_RECT_F clip = { 0.0f, 0.0f, (float)w, (float)h };
            if (root) IDCompositionVisual_SetClip( root, &clip );
            if (g_active_wv2_impl->visual) IDCompositionVisual_SetClip( g_active_wv2_impl->visual, &clip );
            if (g_active_wv2_impl->loader_controller && g_active_wv2_impl->real_loader)
            {
                ICoreWebView2Controller *ctrl = g_active_wv2_impl->loader_controller;
                RECT rc = { 0, 0, w, h };
                ctrl->lpVtbl->put_Bounds( ctrl, rc );
                ctrl->lpVtbl->NotifyParentWindowPositionChanged( ctrl );
                wv2_sync_child_hwnds( wv2_get_controller_parent(), (int)w, (int)h );
            }
            xaml_dcomp_commit();
        }
        return 0;
    }
    if (msg == WM_APP_WV2_WHEEL && g_active_wv2_impl)
    {
        HWND host = g_active_wv2_impl->parent_hwnd ? g_active_wv2_impl->parent_hwnd : g_wv2_core_hwnd;
        POINT pt = { (short)LOWORD( lparam ), (short)HIWORD( lparam ) };
        if (host && IsWindow( host )) ScreenToClient( host, &pt );
        if (g_active_wv2_impl->loader_comp_ctrl)
        {
            ICoreWebView2CompositionController *comp_ctrl = g_active_wv2_impl->loader_comp_ctrl;
            HRESULT hr_m = comp_ctrl->lpVtbl->SendMouseInput( comp_ctrl, WM_MOUSEWHEEL,
                (UINT32)LOWORD( wparam ), (UINT32)(short)HIWORD( wparam ), pt );
            TRACE( "wv2_sta_wndproc: SendMouseInput(WM_MOUSEWHEEL) pt=%ld,%ld delta=%d hr=%#lx\n",
                   pt.x, pt.y, (int)(short)HIWORD( wparam ), hr_m );
        }
        else if (host)
        {
            MSG m = { host, WM_MOUSEWHEEL, wparam, lparam };
            EnumChildWindows( host, wv2_post_wheel_cb, (LPARAM)&m );
        }
        return 0;
    }
    if (msg == WM_APP_WV2_MOUSE && g_active_wv2_impl)
    {
        HWND host = g_active_wv2_impl->parent_hwnd ? g_active_wv2_impl->parent_hwnd : g_wv2_core_hwnd;
        UINT32 ev_kind = LOWORD( wparam );
        UINT32 vkeys = HIWORD( wparam );
        POINT pt = { (short)LOWORD( lparam ), (short)HIWORD( lparam ) };
        if (ev_kind == WM_MOUSELEAVE)
        {
            vkeys = 0;
            pt.x = 0;
            pt.y = 0;
        }
        if (ev_kind == WM_LBUTTONDOWN || ev_kind == WM_LBUTTONUP ||
            ev_kind == WM_RBUTTONDOWN || ev_kind == WM_MBUTTONDOWN)
            wv2_ensure_focus( g_active_wv2_impl, host, TRUE );
        if (g_active_wv2_impl->loader_comp_ctrl)
        {
            ICoreWebView2CompositionController *comp_ctrl = g_active_wv2_impl->loader_comp_ctrl;
            HRESULT hr_m = comp_ctrl->lpVtbl->SendMouseInput( comp_ctrl, ev_kind, vkeys, 0, pt );
            if (ev_kind == WM_LBUTTONDOWN || ev_kind == WM_LBUTTONUP)
                ERR( "wv2_sta_wndproc: SendMouseInput(%#x) pt=%ld,%ld vkeys=%#x hr=%#lx\n",
                     ev_kind, pt.x, pt.y, vkeys, hr_m );
            else if (ev_kind != WM_MOUSEMOVE)
                TRACE( "wv2_sta_wndproc: SendMouseInput(%#x) pt=%ld,%ld vkeys=%#x hr=%#lx\n",
                       ev_kind, pt.x, pt.y, vkeys, hr_m );
        }
        else if (host)
        {
            MSG m = { host, ev_kind, vkeys, lparam };
            EnumChildWindows( host, wv2_post_wheel_cb, (LPARAM)&m );
        }
        return 0;
    }
    if (msg == WM_APP_WV2_KEY && g_active_wv2_impl)
    {
        HWND host = g_active_wv2_impl->parent_hwnd ? g_active_wv2_impl->parent_hwnd : g_wv2_core_hwnd;
        UINT32 vk = LOWORD( wparam ) & 0xff;
        UINT32 mods = (LOWORD( wparam ) >> 8) & 0xff;
        UINT key_msg = HIWORD( wparam );
        UINT32 repeat = (UINT32)LOWORD( lparam );
        UINT32 scan = (UINT32)((lparam >> 16) & 0xff);
        wv2_sta_send_keyboard_input( g_active_wv2_impl, host, key_msg, vk, scan, repeat, mods, lparam );
        return 0;
    }
    if (msg == WM_APP_WV2_CHAR && g_active_wv2_impl)
    {
        HWND host = g_active_wv2_impl->parent_hwnd ? g_active_wv2_impl->parent_hwnd : g_wv2_core_hwnd;
        WCHAR ch = (WCHAR)LOWORD( wparam );
        BOOL keydown = HIWORD( wparam ) != 0;
        wv2_sta_send_character_input( g_active_wv2_impl, host, ch, keydown, lparam );
        return 0;
    }
    if (msg == WM_APP_WV2_PASTE)
    {
        /* The whole clipboard read + per-character key/char pump runs here, on
         * the STA bridge thread (same pattern as WM_APP_WV2_WHEEL), so the
         * host wndproc returns from the Ctrl+V keydown without blocking. */
        wv2_paste_clipboard_text( hwnd );
        return 0;
    }
    return DefWindowProcW( hwnd, msg, wparam, lparam );
}

void wv2_ensure_sta_hwnd( void )
{
    WNDCLASSW wc;
    if (g_wv2_sta_hwnd && IsWindow( g_wv2_sta_hwnd )) return;
    memset( &wc, 0, sizeof(wc) );
    wc.lpfnWndProc = wv2_sta_wndproc;
    wc.hInstance = GetModuleHandleW( NULL );
    wc.lpszClassName = L"WineWv2StaWheelBridge";
    RegisterClassW( &wc );
    g_wv2_sta_hwnd = CreateWindowExW( 0, L"WineWv2StaWheelBridge", L"", 0, 0, 0, 0, 0,
                                      HWND_MESSAGE, NULL, wc.hInstance, NULL );
}

static void wv2_send_keyboard_input( HWND hwnd, UINT key_msg, UINT32 vk, UINT32 scan,
                                     UINT32 repeat, UINT32 mods, LPARAM lparam )
{
    WPARAM wp = MAKEWPARAM( (vk & 0xff) | ((mods & 0xff) << 8), key_msg & 0xffff );
    if (g_wv2_sta_hwnd && IsWindow( g_wv2_sta_hwnd ))
        PostMessageW( g_wv2_sta_hwnd, WM_APP_WV2_KEY, wp, lparam );
    else if (hwnd)
    {
        MSG m = { hwnd, key_msg, vk, lparam };
        EnumChildWindows( hwnd, wv2_post_wheel_cb, (LPARAM)&m );
    }
}

static void wv2_send_character_input( HWND hwnd, WCHAR ch, BOOL keydown, LPARAM lparam )
{
    WPARAM wp = MAKEWPARAM( (WORD)ch, (WORD)(keydown ? 1 : 0) );
    if (g_wv2_sta_hwnd && IsWindow( g_wv2_sta_hwnd ))
        PostMessageW( g_wv2_sta_hwnd, WM_APP_WV2_CHAR, wp, lparam );
    else if (hwnd)
    {
        MSG m = { hwnd, WM_CHAR, (WPARAM)ch, lparam };
        EnumChildWindows( hwnd, wv2_post_wheel_cb, (LPARAM)&m );
    }
}

/* Paste path choice: read CF_UNICODETEXT (with CF_TEXT fallback) from the Win32
 * clipboard and dispatch a per-character SendKeyboardInput(WM_KEYDOWN) +
 * SendCharacterInput(WM_CHAR) + SendKeyboardInput(WM_KEYUP) loop with VK_CONTROL
 * cleared in the STA thread's keyboard state. Chosen over CoreWebView2
 * ExecuteScript("document.execCommand('paste')") because Chromium blocks
 * programmatic paste without clipboard-read permission / transient user activation
 * and bypasses the native keydown/keypress/beforeinput/input sequence expected by
 * controlled React inputs (such as WhatsApp Web's phone-number field). */
static void wv2_paste_clipboard_text( HWND hwnd )
{
    WCHAR *buf = NULL;
    size_t i, len = 0;

    if (!OpenClipboard( NULL ) && (!hwnd || !OpenClipboard( hwnd )))
    {
        TRACE( "wv2_paste_clipboard_text: OpenClipboard failed err=%lu\n", GetLastError() );
        return;
    }

    if (IsClipboardFormatAvailable( CF_UNICODETEXT ))
    {
        HANDLE h = GetClipboardData( CF_UNICODETEXT );
        const WCHAR *src = h ? GlobalLock( h ) : NULL;
        if (src)
        {
            len = lstrlenW( src );
            if ((buf = HeapAlloc( GetProcessHeap(), 0, (len + 1) * sizeof(WCHAR) )))
                memcpy( buf, src, (len + 1) * sizeof(WCHAR) );
            GlobalUnlock( h );
        }
    }
    else if (IsClipboardFormatAvailable( CF_TEXT ))
    {
        HANDLE h = GetClipboardData( CF_TEXT );
        const char *src = h ? GlobalLock( h ) : NULL;
        if (src)
        {
            int wlen = MultiByteToWideChar( CP_UTF8, 0, src, -1, NULL, 0 );
            if (wlen > 0 && (buf = HeapAlloc( GetProcessHeap(), 0, wlen * sizeof(WCHAR) )))
            {
                MultiByteToWideChar( CP_UTF8, 0, src, -1, buf, wlen );
                len = (size_t)(wlen - 1);
            }
            GlobalUnlock( h );
        }
    }
    CloseClipboard();

    if (!buf || !len)
    {
        HeapFree( GetProcessHeap(), 0, buf );
        return;
    }

    if (len > 32768) len = 32768; /* clamp to 64 KiB of UTF-16 code units: each
                                   * unit costs a PostMessage + a Blink
                                   * execCommand round-trip, unbounded clips
                                   * stalled the paste pump */

    TRACE( "wv2_paste_clipboard_text: pasting %lu chars %s\n", (unsigned long)len, debugstr_w( buf ) );
    for (i = 0; i < len; i++)
    {
        WCHAR ch = buf[i], ch2 = 0;
        SHORT vks;
        UINT32 vk = 0, scan = 0;
        LPARAM lp_down, lp_up;

        /* CRLF is one Enter keypress: emit it as keydown(VK_RETURN) +
         * WM_CHAR('\r') + keyup once (the CR carried the key events, the LF
         * used to arrive as a bare, keydown-less WM_CHAR('\n')). */
        if (ch == '\r' && i + 1 < len && buf[i + 1] == '\n') i++;
        else if (ch >= 0xd800 && ch <= 0xdbff && i + 1 < len &&
                 buf[i + 1] >= 0xdc00 && buf[i + 1] <= 0xdfff)
        {
            /* Surrogate pair (emoji, astral CJK): Chromium's WM_CHAR
             * convention sends a pair as two WM_CHAR messages — high unit
             * first, then low — inside ONE keydown/keyup press. Two separate
             * keypresses made Blink decode 2× U+FFFD. */
            ch2 = buf[++i];
        }

        vks = VkKeyScanW( ch );
        if (vks != -1)
        {
            vk = LOBYTE( vks ) & 0xff;
            scan = MapVirtualKeyW( vk, MAPVK_VK_TO_VSC ) & 0xff;
        }
        lp_down = 1 | ((LPARAM)scan << 16);
        lp_up = lp_down | (LPARAM)0xc0000000;
        if (vk)
            wv2_send_keyboard_input( hwnd, WM_KEYDOWN, vk, scan, 1, 0, lp_down );
        wv2_send_character_input( hwnd, ch, TRUE, lp_down );
        if (ch2)
            wv2_send_character_input( hwnd, ch2, TRUE, lp_down );
        if (vk)
            wv2_send_keyboard_input( hwnd, WM_KEYUP, vk, scan, 1, 0, lp_up );
    }
    HeapFree( GetProcessHeap(), 0, buf );
}

static LRESULT CALLBACK wv2_host_wndproc( HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam )
{
    if (msg == WM_ERASEBKGND && g_active_wv2_impl && g_active_wv2_impl->has_comp_ctrl)
        return 1;
    if (msg == WM_SIZE)
    {
        if (g_active_wv2_impl && g_wv2_sta_hwnd && IsWindow( g_wv2_sta_hwnd ))
            PostMessageW( g_wv2_sta_hwnd, WM_APP_WV2_RESIZE, wparam, lparam );
        else if (!g_active_wv2_impl)
            wv2_sync_child_hwnds( hwnd, LOWORD( lparam ), HIWORD( lparam ) );
    }
    if (msg == WM_TIMER && wparam == WV2_CHILD_SYNC_TIMER_ID)
    {
        if (!g_active_wv2_impl)
            wv2_sync_child_hwnds( hwnd, 0, 0 );
        else
            KillTimer( hwnd, WV2_CHILD_SYNC_TIMER_ID );
        return 0;
    }
    if (msg == WM_APP_WV2_INIT && lparam)
    {
        struct xaml_element *impl = (struct xaml_element *)lparam;
        trigger_core_webview2_initialized( impl, &impl->IWebView2_iface );
        return 0;
    }
    if (msg == WM_MOUSEWHEEL && g_active_wv2_impl)
    {
        if (g_wv2_sta_hwnd && IsWindow( g_wv2_sta_hwnd ))
        {
            PostMessageW( g_wv2_sta_hwnd, WM_APP_WV2_WHEEL, wparam, lparam );
            TRACE( "wv2_host_wndproc: forwarded WM_MOUSEWHEEL delta=%d to sta=%p\n",
                   (int)(short)HIWORD( wparam ), g_wv2_sta_hwnd );
        }
        else
        {
            MSG m = { hwnd, msg, wparam, lparam };
            EnumChildWindows( hwnd, wv2_post_wheel_cb, (LPARAM)&m );
        }
    }
    else if ((msg == WM_MOUSEMOVE || msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP ||
              msg == WM_LBUTTONDBLCLK || msg == WM_RBUTTONDOWN || msg == WM_RBUTTONUP ||
              msg == WM_RBUTTONDBLCLK || msg == WM_MBUTTONDOWN || msg == WM_MBUTTONUP ||
              msg == WM_MBUTTONDBLCLK || msg == WM_MOUSELEAVE) && g_active_wv2_impl)
    {
        WPARAM wp = MAKEWPARAM( msg, LOWORD( wparam ) & 0x7f );
        if (g_wv2_sta_hwnd && IsWindow( g_wv2_sta_hwnd ))
            PostMessageW( g_wv2_sta_hwnd, WM_APP_WV2_MOUSE, wp, lparam );
        else
        {
            MSG m = { hwnd, msg, wparam, lparam };
            EnumChildWindows( hwnd, wv2_post_wheel_cb, (LPARAM)&m );
        }
    }
    else if ((msg == WM_KEYDOWN || msg == WM_KEYUP || msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP) &&
             g_active_wv2_impl)
    {
        UINT32 vk = (UINT32)wparam;
        UINT32 repeat = (UINT32)LOWORD( lparam );
        UINT32 scan = (UINT32)((lparam >> 16) & 0xff);
        UINT32 mods = 0;
        BOOL is_down = (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN);

        if (vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL) g_wv2_ctrl_down = is_down;
        if (vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT) g_wv2_shift_down = is_down;
        if (vk == VK_MENU || vk == VK_LMENU || vk == VK_RMENU) g_wv2_alt_down = is_down;
        if (g_wv2_shift_down || (GetKeyState( VK_SHIFT ) & 0x8000)) mods |= MK_SHIFT;
        if (g_wv2_ctrl_down || (GetKeyState( VK_CONTROL ) & 0x8000) || (GetAsyncKeyState( VK_CONTROL ) & 0x8000))
            mods |= MK_CONTROL;
        if (g_wv2_alt_down || (GetKeyState( VK_MENU ) & 0x8000)) mods |= 0x20;

        wv2_send_keyboard_input( hwnd, msg, vk, scan, repeat, mods, lparam );
        if (msg == WM_KEYDOWN && vk == 'V' && (mods & MK_CONTROL))
        {
            /* Hand the paste loop to the STA bridge (pattern: WM_APP_WV2_WHEEL)
             * so the clipboard read + per-char pump does not block this
             * wndproc; the bridge window always exists once the host window is
             * live, the inline call is the fallback. */
            wv2_ensure_sta_hwnd();
            if (g_wv2_sta_hwnd && IsWindow( g_wv2_sta_hwnd ))
                PostMessageW( g_wv2_sta_hwnd, WM_APP_WV2_PASTE, 0, 0 );
            else
                wv2_paste_clipboard_text( hwnd );
        }
    }
    else if (msg == WM_CHAR && g_active_wv2_impl)
    {
        WCHAR ch = (WCHAR)wparam;
        if (!g_wv2_ctrl_down && (ch >= 0x20 || ch == '\b' || ch == '\t' || ch == '\r'))
            wv2_send_character_input( hwnd, ch, TRUE, lparam );
    }
    if (g_wv2_prev_wndproc && g_wv2_prev_wndproc != wv2_host_wndproc)
        return CallWindowProcW( g_wv2_prev_wndproc, hwnd, msg, wparam, lparam );
    return DefWindowProcW( hwnd, msg, wparam, lparam );
}

void xaml_wv2_post_resize( UINT width, UINT height )
{
    if (g_wv2_sta_hwnd && IsWindow( g_wv2_sta_hwnd ) && width > 0 && height > 0)
        PostMessageW( g_wv2_sta_hwnd, WM_APP_WV2_RESIZE, 0, MAKELPARAM( width, height ) );
}

void wv2_ensure_host_subclass( HWND parent )
{
    WNDPROC prev;
    if (!parent || !IsWindow( parent ) || g_wv2_subclassed_hwnd == parent) return;
    if (GetWindowThreadProcessId( parent, NULL ) != GetCurrentThreadId()) return;
    prev = (WNDPROC)SetWindowLongPtrW( parent, GWLP_WNDPROC, (LONG_PTR)wv2_host_wndproc );
    if (prev && prev != wv2_host_wndproc)
    {
        g_wv2_prev_wndproc = prev;
        g_wv2_subclassed_hwnd = parent;
        if (!g_active_wv2_impl)
            SetTimer( parent, WV2_CHILD_SYNC_TIMER_ID, 250, NULL );
        ERR( "wv2_ensure_host_subclass: subclassed parent=%p prev=%p\n", parent, prev );
    }
}

struct wv2_enum_ctx
{
    int width;
    int height;
    int count;
};

HWND wv2_get_controller_parent( void );

static BOOL CALLBACK wv2_sync_child_cb( HWND child, LPARAM lparam )
{
    struct wv2_enum_ctx *ctx = (struct wv2_enum_ctx *)lparam;
    WCHAR cls[128] = {0};

    GetClassNameW( child, cls, ARRAY_SIZE( cls ) );
    if (wcsstr( cls, L"Chrome_WidgetWin" ) || wcsstr( cls, L"Intermediate D3D" ) ||
        wcsstr( cls, L"Edge" ) || wcsstr( cls, L"WebView" ) || wcsstr( cls, L"RenderWidget" ))
    {
        if (!g_active_wv2_impl || !g_active_wv2_impl->has_comp_ctrl)
        {
            RECT cur_rc;
            if (!GetWindowRect( child, &cur_rc ) ||
                (cur_rc.right - cur_rc.left) != ctx->width ||
                (cur_rc.bottom - cur_rc.top) != ctx->height)
            {
                ShowWindow( child, SW_SHOW );
                SetWindowPos( child, HWND_TOP, 0, 0, ctx->width, ctx->height,
                              SWP_SHOWWINDOW | SWP_NOACTIVATE );
                ERR( "wv2_sync_child_cb: positioned child %p (%s) to %dx%d\n",
                       child, debugstr_w( cls ), ctx->width, ctx->height );
            }
        }
        ctx->count++;
    }
    EnumChildWindows( child, wv2_sync_child_cb, lparam );
    return TRUE;
}

static HANDLE g_xaml_window_ready_event;

HANDLE get_xaml_window_ready_event( void )
{
    if (!g_xaml_window_ready_event)
    {
        HANDLE ev = CreateEventW( NULL, FALSE, FALSE, NULL );
        if (ev && InterlockedCompareExchangePointer( &g_xaml_window_ready_event, ev, NULL ) != NULL)
            CloseHandle( ev );
    }
    return g_xaml_window_ready_event;
}

static BOOL CALLBACK wv2_find_process_window_cb( HWND hwnd, LPARAM lparam )
{
    DWORD pid = 0;
    if (!IsWindowVisible( hwnd )) return TRUE;
    if ((GetWindowLongW( hwnd, GWL_STYLE ) & WS_OVERLAPPEDWINDOW) != WS_OVERLAPPEDWINDOW) return TRUE;
    GetWindowThreadProcessId( hwnd, &pid );
    if (pid != GetCurrentProcessId()) return TRUE;
    *(HWND *)lparam = hwnd;
    return FALSE;
}

HWND wv2_get_controller_parent( void )
{
    HWND hw = g_active_wv2_impl && g_active_wv2_impl->parent_hwnd ? g_active_wv2_impl->parent_hwnd : g_wv2_core_hwnd;
    if (hw && IsWindow( hw ))
    {
        HWND root = GetAncestor( hw, GA_ROOT );
        return root ? root : hw;
    }
    hw = xaml_get_window_hwnd();
    if (hw && IsWindow( hw )) return hw;
    hw = xaml_find_core_window();
    if (hw) return hw;
    hw = NULL;
    EnumWindows( wv2_find_process_window_cb, (LPARAM)&hw );
    if (hw) return hw;
    return NULL;
}

static BOOL CALLBACK wv2_sync_top_cb( HWND hwnd, LPARAM lparam )
{
    struct wv2_enum_ctx *ctx = (struct wv2_enum_ctx *)lparam;
    WCHAR cls[128] = {0};
    DWORD pid = 0;

    GetWindowThreadProcessId( hwnd, &pid );
    GetClassNameW( hwnd, cls, ARRAY_SIZE( cls ) );
    if (pid != GetCurrentProcessId())
        return TRUE;

    TRACE( "wv2_sync_top_cb: seen hwnd %p class %s\n", hwnd, debugstr_w(cls) );
    if (g_active_wv2_impl && !g_active_wv2_impl->has_comp_ctrl &&
        (wcsstr( cls, L"Chrome_WidgetWin" ) || wcsstr( cls, L"Intermediate D3D" ) ||
         wcsstr( cls, L"RenderWidget" )))
    {
        HWND parent = wv2_get_controller_parent();
        if (parent && IsWindow( parent ) && hwnd != parent)
        {
            SetParent( hwnd, parent );
            ShowWindow( hwnd, SW_SHOW );
            SetWindowPos( hwnd, HWND_TOP, 0, 0, ctx->width, ctx->height,
                          SWP_SHOWWINDOW | SWP_NOACTIVATE );
            ERR( "wv2_sync_top_cb: re-parented and positioned child %p (%s) to parent %p %dx%d\n",
                 hwnd, debugstr_w( cls ), parent, ctx->width, ctx->height );
            EnumChildWindows( hwnd, wv2_sync_child_cb, lparam );
            ctx->count++;
        }
    }
    return TRUE;
}

void wv2_sync_child_hwnds( HWND parent, UINT width, UINT height )
{
    struct wv2_enum_ctx ctx;

    if (g_active_wv2_impl && g_active_wv2_impl->has_comp_ctrl) return;

    if (!parent || !IsWindow( parent )) parent = wv2_get_controller_parent();
    if ((int)width <= 0 || (int)height <= 0)
    {
        RECT rc;
        if (parent && IsWindow( parent ) && GetClientRect( parent, &rc ) &&
            rc.right > rc.left && rc.bottom > rc.top)
        {
            if ((int)width <= 0) width = (UINT)(rc.right - rc.left);
            if ((int)height <= 0) height = (UINT)(rc.bottom - rc.top);
        }
        else
        {
            SIZE sz;
            xaml_get_layout_metrics( parent, &sz, NULL );
            if ((int)width <= 0) width = (UINT)sz.cx;
            if ((int)height <= 0) height = (UINT)sz.cy;
        }
    }

    ctx.width = (int)width;
    ctx.height = (int)height;
    ctx.count = 0;

    if (parent && IsWindow( parent ))
        EnumChildWindows( parent, wv2_sync_child_cb, (LPARAM)&ctx );
    if (g_active_wv2_impl)
        EnumWindows( wv2_sync_top_cb, (LPARAM)&ctx );
    if (!g_active_wv2_impl && ctx.count > 0 && parent && IsWindow( parent ))
        KillTimer( parent, WV2_CHILD_SYNC_TIMER_ID );
}

DWORD WINAPI wv2_sta_thread( void *arg )
{
    struct xaml_element *impl = arg;
    HWND hw = NULL;
    HANDLE ev;
    DWORD start;
    MSG msg;

    CoInitializeEx( NULL, COINIT_APARTMENTTHREADED );
    wv2_ensure_sta_hwnd();
    ev = get_xaml_window_ready_event();
    start = GetTickCount();
    while (!(hw = xaml_get_window_hwnd()) || !IsWindow( hw ))
    {
        DWORD elapsed = GetTickCount() - start;
        if (elapsed >= 2000) break;
        if (!ev || WaitForSingleObject( ev, 2000 - elapsed ) != WAIT_OBJECT_0)
        {
            hw = xaml_get_window_hwnd();
            break;
        }
    }
    ERR( "wv2_sta_thread: starting browser, target=%p\n", hw );
    impl->parent_hwnd = hw;
    g_wv2_core_hwnd = hw;
    wv2_start_browser( impl );
    while (GetMessageW( &msg, NULL, 0, 0 ) > 0)
    {
        TranslateMessage( &msg );
        DispatchMessageW( &msg );
    }
    return 0;
}
