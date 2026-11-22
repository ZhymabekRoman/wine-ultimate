/* WinRT Windows.UI.Xaml.Controls.WebView2 bridge shared internals
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

#ifndef WV2_PRIVATE_H
#define WV2_PRIVATE_H

/* Include after private.h and uwp_private.h. */

/* ---------- COM / WinRT interface identifiers (defined in webview2_bridge.c) ---------- */
extern const GUID IID_ICoreWebView2_2_com;
extern const GUID IID_ICoreWebView2_11_com;
extern const GUID IID_ICoreWebView2Settings2_com;
extern const GUID IID_ICoreWebView2;
extern const GUID IID_ICoreWebView2_2;
extern const GUID IID_ICoreWebView2_3;
extern const GUID IID_ICoreWebView2_4;
extern const GUID IID_ICoreWebView2_5;
extern const GUID IID_ICoreWebView2_6;
extern const GUID IID_ICoreWebView2_7;
extern const GUID IID_ICoreWebView2_8;
extern const GUID IID_ICoreWebView2_9;
extern const GUID IID_ICoreWebView2_10;
extern const GUID IID_ICoreWebView2_11;
extern const GUID IID_ICoreWebView2_12;
extern const GUID IID_ICoreWebView2_13;
extern const GUID IID_ICoreWebView2Settings;
extern const GUID IID_ICoreWebView2Settings2;
extern const GUID IID_ICoreWebView2Settings3;
extern const GUID IID_ICoreWebView2Settings4;
extern const GUID IID_ICoreWebView2Settings5;
extern const GUID IID_ICoreWebView2Settings6;
extern const GUID IID_ICoreWebView2Settings7;
extern const GUID IID_ICoreWebView2Settings8;
extern const GUID IID_ICoreWebView2Settings9;
extern const GUID IID_ICoreWebView2Settings_Manual;
extern const GUID IID_ICoreWebView2Environment;
extern const GUID IID_ICoreWebView2NavigationStartingEventArgs_rt;
extern const GUID IID_ICoreWebView2NavigationCompletedEventArgs_rt;
extern const GUID IID_ICoreWebView2WebMessageReceivedEventArgs_rt;
extern const GUID IID_ICoreWebView2WebResourceRequestedEventArgs_rt;
extern const GUID IID_ICoreWebView2DOMContentLoadedEventArgs_rt;
extern const GUID IID_ICoreWebView2ContextMenuRequestedEventArgs_rt;
extern const GUID IID_ICoreWebView2InitializedEventArgs;
extern const GUID IID_ICoreWebView2CreateEnvHandler;
extern const GUID IID_ICoreWebView2CreateCtrlHandler;
extern const GUID IID_ICoreWebView2CreateCompCtrlHandler;
extern const GUID IID_ICoreWebView2Environment3_g;
extern const GUID IID_ICoreWebView2CompositionController_g;
extern const GUID IID_ICoreWebView2Controller_g;
extern const GUID IID_ICoreWebView2EnvironmentOptions_g;

/* ---------- shared object layouts ---------- */
struct cwv2_settings
{
    IInspectable IInspectable_iface;
    IInspectable ISettings2_iface;
    IInspectable ISettings_stub_iface;
    LONG ref;
    void *loader_settings;   /* COM ICoreWebView2Settings* */
    void *loader_settings2;  /* COM ICoreWebView2Settings2* */
    struct core_webview2 *owner;
};

struct cwv2_env
{
    IInspectable IInspectable_iface;
    LONG ref;
};

struct cwv2_env_asyncop
{
    IInspectable IInspectable_iface;
    IAsyncInfo IAsyncInfo_iface;
    LONG ref;
    void *handler;
    IInspectable *result;
};

/* Handler faces the app hands to our shells: every WinRT event/completed delegate starts
 * { QueryInterface, AddRef, Release, Invoke }. The 2nd/3rd Invoke parameters differ per
 * operation, so name the layout for each shape we fire instead of indexing slot 3. */
struct cwv2_action_completed_handler;
struct cwv2_action_completed_handler_vtbl
{
    HRESULT (WINAPI *QueryInterface)( struct cwv2_action_completed_handler *This, REFIID riid, void **ppv );
    ULONG (WINAPI *AddRef)( struct cwv2_action_completed_handler *This );
    ULONG (WINAPI *Release)( struct cwv2_action_completed_handler *This );
    HRESULT (WINAPI *Invoke)( struct cwv2_action_completed_handler *This, void *async_info, AsyncStatus status );
};
struct cwv2_action_completed_handler
{
    const struct cwv2_action_completed_handler_vtbl *lpVtbl;
};

struct cwv2_result_completed_handler;
struct cwv2_result_completed_handler_vtbl
{
    HRESULT (WINAPI *QueryInterface)( struct cwv2_result_completed_handler *This, REFIID riid, void **ppv );
    ULONG (WINAPI *AddRef)( struct cwv2_result_completed_handler *This );
    ULONG (WINAPI *Release)( struct cwv2_result_completed_handler *This );
    HRESULT (WINAPI *Invoke)( struct cwv2_result_completed_handler *This, HRESULT status, void *result );
};
struct cwv2_result_completed_handler
{
    const struct cwv2_result_completed_handler_vtbl *lpVtbl;
};

struct cwv2_typed_handler;
struct cwv2_typed_handler_vtbl
{
    HRESULT (WINAPI *QueryInterface)( struct cwv2_typed_handler *This, REFIID riid, void **ppv );
    ULONG (WINAPI *AddRef)( struct cwv2_typed_handler *This );
    ULONG (WINAPI *Release)( struct cwv2_typed_handler *This );
    HRESULT (WINAPI *Invoke)( struct cwv2_typed_handler *This, void *sender, void *args );
};
struct cwv2_typed_handler
{
    const struct cwv2_typed_handler_vtbl *lpVtbl;
};

/* WinRT IAsyncOperation<HSTRING> shell: supports both synchronous completion
 * (completed = TRUE at creation) and asynchronous completion driven by a COM
 * ICoreWebView2*CompletedHandler callback (completed = FALSE until Invoke). */
struct cwv2_asyncop
{
    IInspectable IInspectable_iface;
    IAsyncInfo IAsyncInfo_iface;
    LONG ref;
    BOOL completed;
    HRESULT hr;
    HSTRING result;
    void *handler;          /* AsyncOperationCompletedHandler<HSTRING> */
};

/* COM ICoreWebView2*CompletedHandler(HRESULT, LPCWSTR) -> cwv2_asyncop bridge */
struct cwv2_com_str_handler
{
    const void **vtbl;
    LONG ref;
    struct cwv2_asyncop *op;
};

/* s480: CoreWebView2Profile shell. The app awaits
 * CoreWebView2.Profile.AddBrowserExtensionAsync(path); slot 8 is get_Profile
 * and the profile's AddBrowserExtension slot returns a completed
 * IAsyncOperation<CoreWebView2BrowserExtension> (asyncop shell). */
struct cwv2_profile
{
    IInspectable IInspectable_iface;
    LONG ref;
};

struct cwv2_args
{
    IInspectable IInspectable_iface;
    LONG ref;
};

/* ---------- messages between the XAML host window and the STA bridge ---------- */
#define WM_APP_WV2_WHEEL (WM_APP + 0x58)
#define WM_APP_WV2_MOUSE (WM_APP + 0x59)
#define WM_APP_WV2_KEY   (WM_APP + 0x5a)
#define WM_APP_WV2_CHAR  (WM_APP + 0x5b)
#define WM_APP_WV2_PASTE (WM_APP + 0x5c)
#define WM_APP_WV2_INIT  (WM_APP + 0x5d)
#define WM_APP_WV2_RESIZE (WM_APP + 0x5e)

/* ---------- shared globals (defined in wv2_host_sta.c) ---------- */
extern HWND g_wv2_sta_hwnd;
extern HWND g_wv2_subclassed_hwnd;

/* ---------- shared vtables (heap-dup'ed via vtbl_dup) ---------- */
extern const void *cwv2_settings_vtbl[256];
extern const void *cwv2_settings2_vtbl[256];
extern const void *cwv2_settings_stub_vtbl[256];
extern const void *cwv2_env_vtbl[256];
extern const void *cwv2_profile_vtbl[256];
extern const void *cwv2_args_vtbl[256];

/* ---------- shared CONTAINING_RECORD helpers ---------- */
static inline struct core_webview2 *impl_from_cwv2( void *iface )
{
    return CONTAINING_RECORD( iface, struct core_webview2, IInspectable_iface );
}
static inline struct core_webview2 *impl_from_cwv2_2( void *iface )
{
    return CONTAINING_RECORD( iface, struct core_webview2, ICoreWebView2_2_iface );
}
static inline struct core_webview2 *impl_from_cwv2_11( void *iface )
{
    return CONTAINING_RECORD( iface, struct core_webview2, ICoreWebView2_11_iface );
}

/* ---------- cross-file functions ---------- */
/* webview2_bridge.c */
void cwv2_queue_op( struct core_webview2 *impl, enum cwv2_pending_kind kind,
        int arg_int, BOOL arg_bool, const WCHAR *s1, const WCHAR *s2,
        void *obj, const void **avtbl, const GUID *aiid );
ULONG WINAPI cwv2_AddRef( IInspectable *iface );
ULONG WINAPI cwv2_Release( IInspectable *iface );
HRESULT WINAPI cwv2_GetIids( IInspectable *iface, ULONG *count, IID **iids );
HRESULT WINAPI cwv2_GetRuntimeClassName( IInspectable *iface, HSTRING *name );
HRESULT WINAPI cwv2_GetTrustLevel( IInspectable *iface, TrustLevel *level );
void *wv2_slot_fn( void *obj, BOOL real_loader, int slot_real, int slot_builtin );
void trigger_core_webview2_initialized( struct xaml_element *impl, void *iface );
void wv2_defer_start_browser( struct xaml_element *impl );

/* wv2_async_profile.c */
HRESULT cwv2_asyncop_create( HSTRING result, void **out );
HRESULT cwv2_asyncop_create_ex( HSTRING result, BOOL completed, struct cwv2_asyncop **out_impl );
struct cwv2_com_str_handler *cwv2_com_str_handler_create( struct cwv2_asyncop *op );
ULONG WINAPI asyncop_Release( void *iface );
ULONG WINAPI com_str_h_Release( void *t );

/* wv2_events.c */
HRESULT cwv2_forward_add_event( struct core_webview2 *impl, void *com_obj, int com_slot,
        void *winrt_handler, EventRegistrationToken *token, const void **args_vtbl, const GUID *args_iid );
void *cwv2_get_com_iface( struct core_webview2 *impl, void **cached, const GUID *iid );
HRESULT WINAPI cwv2_generic_slot( void *iface, ... );
HRESULT WINAPI cwv2_get_Profile( void *iface, void **out );
HRESULT WINAPI cwv2_2_add_WebResourceResponseReceived( void *iface, void *handler, EventRegistrationToken *token );
HRESULT WINAPI cwv2_2_remove_WebResourceResponseReceived( void *iface, EventRegistrationToken token );
HRESULT WINAPI cwv2_2_NavigateWithWebResourceRequest( void *iface, void *request );
HRESULT WINAPI cwv2_2_add_DOMContentLoaded( void *iface, void *handler, EventRegistrationToken *token );
HRESULT WINAPI cwv2_2_remove_DOMContentLoaded( void *iface, EventRegistrationToken token );
HRESULT WINAPI cwv2_11_add_ContextMenuRequested( void *iface, void *handler, EventRegistrationToken *token );
HRESULT WINAPI cwv2_11_remove_ContextMenuRequested( void *iface, EventRegistrationToken token );
HRESULT WINAPI cwv2_11_CallDevToolsProtocolMethodForSessionAsync( void *iface, HSTRING sid, HSTRING method, HSTRING params, void **op );

/* wv2_host_sta.c */
DWORD WINAPI wv2_sta_thread( void *arg );
void wv2_ensure_sta_hwnd( void );
HRESULT WINAPI wv2_get_CoreWebView2( void *iface, void **out );
ULONG WINAPI cwv2_args_Release( IInspectable *iface );

/* ---------- event add/remove slot pairs (generated in wv2_events.c) ---------- */
#define CWV2_EVENT_PAIR_DECL(name) \
    HRESULT WINAPI cwv2_add_##name( void *iface, void *handler, EventRegistrationToken *token ); \
    HRESULT WINAPI cwv2_remove_##name( void *iface, EventRegistrationToken token );

CWV2_EVENT_PAIR_DECL( NavigationStarting )
CWV2_EVENT_PAIR_DECL( ContentLoading )
CWV2_EVENT_PAIR_DECL( SourceChanged )
CWV2_EVENT_PAIR_DECL( HistoryChanged )
CWV2_EVENT_PAIR_DECL( NavigationCompleted )
CWV2_EVENT_PAIR_DECL( FrameNavigationStarting )
CWV2_EVENT_PAIR_DECL( FrameNavigationCompleted )
CWV2_EVENT_PAIR_DECL( ScriptDialogOpening )
CWV2_EVENT_PAIR_DECL( PermissionRequested )
CWV2_EVENT_PAIR_DECL( ProcessFailed )
CWV2_EVENT_PAIR_DECL( WebMessageReceived )
CWV2_EVENT_PAIR_DECL( NewWindowRequested )
CWV2_EVENT_PAIR_DECL( DocumentTitleChanged )
CWV2_EVENT_PAIR_DECL( ContainsFullScreenElementChanged )
CWV2_EVENT_PAIR_DECL( WebResourceRequested )
CWV2_EVENT_PAIR_DECL( WindowCloseRequested )

#endif /* WV2_PRIVATE_H */
