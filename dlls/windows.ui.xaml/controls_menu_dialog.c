/* WinRT Windows.UI.Xaml UWP Keyboard Accelerators, Commands, MenuFlyout,
 * ContentDialog, AutoSuggestBox, SplitView & TeachingTip
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
#include <stdlib.h>
#include <string.h>
#include <excpt.h>
#include "wine/exception.h"
#include "private.h"
#include "uwp_private.h"
#include "controls_nav_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(xaml);

static HRESULT WINAPI uwp_ka_args_QI( void *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IKeyboardAcceleratorInvokedEventArgs_UWP ))
    {
        *out = iface;
        InterlockedIncrement( &((struct uwp_ka_invoked_args *)iface)->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI uwp_ka_args_AddRef( void *iface )
{
    return InterlockedIncrement( &((struct uwp_ka_invoked_args *)iface)->ref );
}
ULONG WINAPI uwp_ka_args_Release( void *iface )
{
    struct uwp_ka_invoked_args *a = (struct uwp_ka_invoked_args *)iface;
    ULONG r = InterlockedDecrement( &a->ref );
    if (!r)
    {
        if (a->element) IInspectable_Release( a->element );
        free( a );
    }
    return r;
}
static HRESULT WINAPI uwp_ka_args_get_Handled( void *iface, boolean *out )
{
    if (out) *out = ((struct uwp_ka_invoked_args *)iface)->handled;
    return S_OK;
}
static HRESULT WINAPI uwp_ka_args_put_Handled( void *iface, boolean val )
{
    ((struct uwp_ka_invoked_args *)iface)->handled = val ? TRUE : FALSE;
    return S_OK;
}
static HRESULT WINAPI uwp_ka_args_get_Element( void *iface, void **out )
{
    struct uwp_ka_invoked_args *a = (struct uwp_ka_invoked_args *)iface;
    if (!out) return E_POINTER;
    *out = a->element;
    if (*out) IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}
const void *uwp_ka_args_vtbl[16] =
{
    [0] = uwp_ka_args_QI,
    [1] = uwp_ka_args_AddRef,
    [2] = uwp_ka_args_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_ka_args_get_Handled,
    [7] = uwp_ka_args_put_Handled,
    [8] = uwp_ka_args_get_Element,
    [9 ... 15] = uwp_generic_noop,
};

static HRESULT WINAPI uwp_ka_get_Key( void *iface, UINT *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (out) *out = impl ? impl->accel_key : 0;
    return S_OK;
}
static HRESULT WINAPI uwp_ka_put_Key( void *iface, UINT val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl)
    {
        impl->accel_key = val;
        uwp_accel_register( impl, impl->accel_target, impl->accel_key, impl->accel_mods, impl->accel_enabled );
    }
    return S_OK;
}
static HRESULT WINAPI uwp_ka_get_Modifiers( void *iface, UINT *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (out) *out = impl ? impl->accel_mods : 0;
    return S_OK;
}
static HRESULT WINAPI uwp_ka_put_Modifiers( void *iface, UINT val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl)
    {
        impl->accel_mods = val;
        uwp_accel_register( impl, impl->accel_target, impl->accel_key, impl->accel_mods, impl->accel_enabled );
    }
    return S_OK;
}
static HRESULT WINAPI uwp_ka_get_IsEnabled( void *iface, boolean *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (out) *out = impl ? (impl->accel_enabled ? TRUE : FALSE) : TRUE;
    return S_OK;
}
static HRESULT WINAPI uwp_ka_put_IsEnabled( void *iface, boolean val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl)
    {
        impl->accel_enabled = val ? TRUE : FALSE;
        uwp_accel_register( impl, impl->accel_target, impl->accel_key, impl->accel_mods, impl->accel_enabled );
    }
    return S_OK;
}
static HRESULT WINAPI uwp_ka_get_ScopeOwner( void *iface, void **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    *out = impl ? impl->accel_scope_owner : NULL;
    if (*out) IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}
static HRESULT WINAPI uwp_ka_put_ScopeOwner( void *iface, void *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return S_OK;
    if (impl->accel_scope_owner) IInspectable_Release( impl->accel_scope_owner );
    impl->accel_scope_owner = (IInspectable *)val;
    if (impl->accel_scope_owner) IInspectable_AddRef( impl->accel_scope_owner );
    return S_OK;
}
static HRESULT WINAPI uwp_ka_add_Invoked( void *iface, void *handler, EventRegistrationToken *tok )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (tok) tok->value = 1;
    if (!impl || !handler) return S_OK;
    if (xaml_vec_grow( (void **)&impl->accel_invoked_handlers, &impl->accel_invoked_cap,
                       impl->accel_invoked_cnt + 1, sizeof(*impl->accel_invoked_handlers), 4 ))
    {
        impl->accel_invoked_handlers[impl->accel_invoked_cnt++] = (IInspectable *)handler;
        IUnknown_AddRef( (IUnknown *)handler );
        if (tok) tok->value = impl->accel_invoked_cnt;
    }
    return S_OK;
}
const void *uwp_ka_vtbl[32] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_ka_get_Key,
    [7] = uwp_ka_put_Key,
    [8] = uwp_ka_get_Modifiers,
    [9] = uwp_ka_put_Modifiers,
    [10] = uwp_ka_get_IsEnabled,
    [11] = uwp_ka_put_IsEnabled,
    [12] = uwp_ka_get_ScopeOwner,
    [13] = uwp_ka_put_ScopeOwner,
    [14] = uwp_ka_add_Invoked,
    [15 ... 31] = uwp_generic_noop,
};

struct uwp_exec_req_args
{
    IInspectable IInspectable_iface;
    LONG ref;
    IInspectable *param;
};
static HRESULT WINAPI uwp_exec_args_QI( void *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IExecuteRequestedEventArgs_UWP ))
    {
        *out = iface;
        InterlockedIncrement( &((struct uwp_exec_req_args *)iface)->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI uwp_exec_args_AddRef( void *iface )
{
    return InterlockedIncrement( &((struct uwp_exec_req_args *)iface)->ref );
}
static ULONG WINAPI uwp_exec_args_Release( void *iface )
{
    struct uwp_exec_req_args *a = (struct uwp_exec_req_args *)iface;
    ULONG r = InterlockedDecrement( &a->ref );
    if (!r)
    {
        if (a->param) IInspectable_Release( a->param );
        free( a );
    }
    return r;
}
static HRESULT WINAPI uwp_exec_args_get_Parameter( void *iface, void **out )
{
    struct uwp_exec_req_args *a = (struct uwp_exec_req_args *)iface;
    if (!out) return E_POINTER;
    *out = a->param;
    if (*out) IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}
static const void *uwp_exec_args_vtbl[16] =
{
    [0] = uwp_exec_args_QI,
    [1] = uwp_exec_args_AddRef,
    [2] = uwp_exec_args_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_exec_args_get_Parameter,
    [7 ... 15] = uwp_generic_noop,
};

static HRESULT WINAPI uwp_xaml_cmd_add_ExecuteRequested( void *iface, void *handler, EventRegistrationToken *tok )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (tok) tok->value = 1;
    if (!impl || !handler) return S_OK;
    if (xaml_vec_grow( (void **)&impl->exec_req_handlers, &impl->exec_req_cap,
                       impl->exec_req_cnt + 1, sizeof(*impl->exec_req_handlers), 4 ))
    {
        impl->exec_req_handlers[impl->exec_req_cnt++] = (IInspectable *)handler;
        IUnknown_AddRef( (IUnknown *)handler );
        if (tok) tok->value = impl->exec_req_cnt;
    }
    return S_OK;
}
const void *uwp_xaml_cmd_vtbl[32] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6 ... 9] = uwp_generic_noop,
    [10] = uwp_ui7_get_KeyboardAccelerators,
    [11 ... 14] = uwp_generic_noop,
    [15] = uwp_btn_get_Command,
    [16] = uwp_btn_put_Command,
    [17] = uwp_xaml_cmd_add_ExecuteRequested,
    [18 ... 31] = uwp_generic_noop,
};

static HRESULT WINAPI uwp_icmd_CanExecute( void *iface, void *param, boolean *out )
{
    if (out) *out = TRUE;
    return S_OK;
}
static HRESULT WINAPI uwp_icmd_Execute( void *iface, void *param )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    struct uwp_exec_req_args *args;
    IInspectable *sender;
    int i;
    if (!impl) return S_OK;
    if (impl->command)
    {
        void *sub_cmd = NULL;
        if ((SUCCEEDED(IInspectable_QueryInterface( impl->command, &IID_ICommand_UWP, &sub_cmd )) ||
             SUCCEEDED(IInspectable_QueryInterface( impl->command, &IID_ICommand_UWP2, &sub_cmd ))) && sub_cmd)
        {
            struct command_iface *cmd = (struct command_iface *)sub_cmd;
            if (cmd->lpVtbl->Execute) cmd->lpVtbl->Execute( cmd, param );
            IUnknown_Release( (IUnknown *)sub_cmd );
        }
    }
    args = calloc( 1, sizeof(*args) );
    if (!args) return E_OUTOFMEMORY;
    args->IInspectable_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_exec_args_vtbl;
    args->ref = 1;
    args->param = (IInspectable *)param;
    if (args->param) IInspectable_AddRef( args->param );
    sender = impl->outer ? impl->outer : &impl->IXamlUICommand_iface;
    for (i = 0; i < impl->exec_req_cnt; i++)
    {
        if (impl->exec_req_handlers[i])
        {
            struct xaml_event_handler *h = (struct xaml_event_handler *)impl->exec_req_handlers[i];
            h->lpVtbl->Invoke( h, sender, &args->IInspectable_iface );
        }
    }
    uwp_exec_args_Release( &args->IInspectable_iface );
    return S_OK;
}
const void *uwp_icmd_vtbl[16] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6 ... 7] = uwp_generic_noop,
    [8] = uwp_icmd_CanExecute,
    [9] = uwp_icmd_Execute,
    [10 ... 15] = uwp_generic_noop,
};

static HRESULT WINAPI uwp_mfi_get_Text( void *iface, HSTRING *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    const WCHAR *txt = uwp_elem_text( impl );
    if (!out) return E_POINTER;
    return WindowsCreateString( txt, (UINT32)wcslen( txt ), out );
}
static HRESULT WINAPI uwp_mfi_put_Text( void *iface, HSTRING val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    const WCHAR *raw;
    if ((ULONG_PTR)val < 0x10000) return S_OK;
    raw = WindowsGetStringRawBuffer( val, NULL );
    if (impl && raw) uwp_elem_set_text( impl, raw );
    return S_OK;
}
const void *uwp_mfi_vtbl[32] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_mfi_get_Text,
    [7] = uwp_mfi_put_Text,
    [8] = uwp_btn_get_Command,
    [9] = uwp_btn_put_Command,
    [10] = uwp_btn_get_CommandParameter,
    [11] = uwp_btn_put_CommandParameter,
    [12] = uwp_btn_add_Click,
    [13 ... 31] = uwp_generic_noop,
};

static HRESULT WINAPI uwp_mfi3_get_AccelText( void *iface, HSTRING *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    return WindowsCreateString( impl ? impl->accel_text : L"", impl ? wcslen(impl->accel_text) : 0, out );
}
static HRESULT WINAPI uwp_mfi3_put_AccelText( void *iface, HSTRING val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    const WCHAR *raw;
    if ((ULONG_PTR)val < 0x10000) return S_OK;
    raw = WindowsGetStringRawBuffer( val, NULL );
    if (impl && raw) lstrcpynW( impl->accel_text, raw, ARRAY_SIZE(impl->accel_text) );
    return S_OK;
}
const void *uwp_mfi3_vtbl[16] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_mfi3_get_AccelText,
    [7] = uwp_mfi3_put_AccelText,
    [8 ... 15] = uwp_generic_noop,
};

static HRESULT WINAPI uwp_tmfi_get_IsChecked( void *iface, boolean *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    *out = (impl && impl->is_checked == 1) ? TRUE : FALSE;
    return S_OK;
}
static HRESULT WINAPI uwp_tmfi_put_IsChecked( void *iface, boolean val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl) impl->is_checked = val ? 1 : 0;
    return S_OK;
}
const void *uwp_tmfi_vtbl[16] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_tmfi_get_IsChecked,
    [7] = uwp_tmfi_put_IsChecked,
    [8 ... 15] = uwp_generic_noop,
};

const void *uwp_mfsi_vtbl[16] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_panel_get_Children,
    [7] = uwp_mfi_get_Text,
    [8] = uwp_mfi_put_Text,
    [9 ... 15] = uwp_generic_noop,
};

static HRESULT WINAPI uwp_mf2_ShowAt( void *iface, void *target, UINT64 pt_raw )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    struct uwp_element *target_el = uwp_elem_from_any( target );
    float fx = 0.0f, fy = 0.0f;
    HWND owner = NULL;
    POINT spt;
    UINT dpi;
    int i;

    if (!impl) return S_OK;
    memcpy( &fx, &pt_raw, sizeof(float) );
    memcpy( &fy, (const char *)&pt_raw + sizeof(float), sizeof(float) );

    if (target_el && target_el->island_hwnd)
        owner = GetAncestor( target_el->island_hwnd, GA_ROOT );
    if (!owner) owner = GetActiveWindow();
    if (!owner) owner = GetForegroundWindow();
    if (!owner && g_island_hwnd_cnt > 0 && g_island_hwnds[0])
        owner = GetAncestor( g_island_hwnds[0], GA_ROOT );

    dpi = owner ? GetDpiForWindow( owner ) : 96;
    if (!dpi) dpi = 96;
    spt.x = MulDiv( (int)(fx + 0.5f), dpi, 96 );
    spt.y = MulDiv( (int)(fy + 0.5f), dpi, 96 );
    if (owner && IsWindow( owner ))
        ClientToScreen( owner, &spt );
    else
        GetCursorPos( &spt );

    TRACE( "IMenuFlyout2::ShowAt(%s, pt=%.1f,%.1f -> screen=%ld,%ld, owner=%p)\n",
           debugstr_w(impl->class_name), fx, fy, spt.x, spt.y, owner );

    for (i = 0; i < impl->flyout_opening_cnt; i++)
    {
        if (impl->flyout_opening_handlers[i])
        {
            IInspectable *sender = impl->outer ? impl->outer : &impl->IFlyout_iface;
            struct xaml_event_handler *h = (struct xaml_event_handler *)impl->flyout_opening_handlers[i];
            __TRY { h->lpVtbl->Invoke( h, sender, NULL ); }
            __EXCEPT_ALL {}
            __ENDTRY;
        }
    }

    uwp_show_flyout_popup( impl, owner, spt.x, spt.y );
    return S_OK;
}

static HRESULT WINAPI uwp_mf2_ShowAtWithOptions( void *iface, void *target, void *opts )
{
    POINT pt;
    UINT64 raw = 0;
    float fx, fy;
    HWND owner = GetActiveWindow();
    (void)opts;
    GetCursorPos( &pt );
    if (owner && IsWindow( owner )) ScreenToClient( owner, &pt );
    fx = (float)pt.x;
    fy = (float)pt.y;
    memcpy( &raw, &fx, sizeof(float) );
    memcpy( (char *)&raw + sizeof(float), &fy, sizeof(float) );
    return uwp_mf2_ShowAt( iface, target, raw );
}

const void *uwp_mf2_vtbl[16] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_mf2_ShowAt,
    [7] = uwp_mf2_ShowAtWithOptions,
    [8 ... 15] = uwp_generic_noop,
};

/* --- IContentDialog, IContentDialog2, IContentDialogClosedEventArgs --- */
struct uwp_cd_closed_args
{
    const void *vtbl;
    LONG ref;
    int result;
};

static HRESULT WINAPI cd_closed_args_QI( void *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IContentDialogClosedEventArgs_UWP ))
    {
        *out = iface;
        InterlockedIncrement( &((struct uwp_cd_closed_args *)iface)->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI cd_closed_args_AddRef( void *iface )
{
    return InterlockedIncrement( &((struct uwp_cd_closed_args *)iface)->ref );
}
static ULONG WINAPI cd_closed_args_Release( void *iface )
{
    struct uwp_cd_closed_args *a = iface;
    ULONG r = InterlockedDecrement( &a->ref );
    if (!r) free( a );
    return r;
}
static HRESULT WINAPI cd_closed_args_get_Result( void *iface, int *out )
{
    struct uwp_cd_closed_args *a = iface;
    if (out) *out = a ? a->result : 0;
    return S_OK;
}
static const void *uwp_cd_closed_args_vtbl[8] =
{
    cd_closed_args_QI,
    cd_closed_args_AddRef,
    cd_closed_args_Release,
    uwp_sub_GetIids,
    uwp_sub_GetRuntimeClassName,
    uwp_sub_GetTrustLevel,
    cd_closed_args_get_Result,
    uwp_generic_noop,
};

static void uwp_cd_fire_closed( struct uwp_element *impl, int result )
{
    struct uwp_cd_closed_args *args;
    IInspectable *sender;
    int i;
    if (!impl) return;
    if (impl->dialog_hwnd && IsWindow( impl->dialog_hwnd ))
    {
        HWND dh = impl->dialog_hwnd;
        impl->dialog_hwnd = NULL;
        DestroyWindow( dh );
    }
    args = calloc( 1, sizeof(*args) );
    if (!args) return;
    args->vtbl = uwp_cd_closed_args_vtbl;
    args->ref = 1;
    args->result = result;
    sender = impl->outer ? impl->outer : &impl->IContentDialog_iface;
    for (i = 0; i < impl->flyout_closed_cnt; i++)
    {
        if (impl->flyout_closed_handlers[i])
        {
            struct xaml_event_handler *h = (struct xaml_event_handler *)impl->flyout_closed_handlers[i];
            __TRY { h->lpVtbl->Invoke( h, sender, args ); }
            __EXCEPT_ALL {}
            __ENDTRY;
        }
    }
    cd_closed_args_Release( args );
    if (g_island_hwnd_cnt > 0 && g_island_hwnds[0])
    {
        HWND root = GetAncestor( g_island_hwnds[0], GA_ROOT );
        if (root && IsWindow( root ))
        {
            RedrawWindow( root, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN );
            PostMessageW( root, WM_NULL, 0, 0 );
        }
    }
    uwp_invalidate_window();
}

static LRESULT CALLBACK uwp_cd_popup_wndproc( HWND hwnd, UINT msg, WPARAM wp, LPARAM lp )
{
    struct uwp_element *impl = (struct uwp_element *)GetWindowLongPtrW( hwnd, GWLP_USERDATA );
    if (msg == WM_NCCREATE)
    {
        CREATESTRUCTW *cs = (CREATESTRUCTW *)lp;
        SetWindowLongPtrW( hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams );
        return DefWindowProcW( hwnd, msg, wp, lp );
    }
    if (msg == WM_ERASEBKGND)
        return 1;
    if (msg == WM_APP + 1)
    {
        if (IsWindow( hwnd ) && IsWindowVisible( hwnd ))
            SetFocus( hwnd );
        return 0;
    }
    if ((msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) && impl)
    {
        if (wp == VK_ESCAPE)
        {
            uwp_cd_fire_closed( impl, 0 );
            return 0;
        }
        if (wp == VK_RETURN)
        {
            int res = (impl->dialog_default_btn == 1) ? 1 : ((impl->dialog_default_btn == 2) ? 2 : 0);
            uwp_cd_fire_closed( impl, res );
            return 0;
        }
    }
    if (msg == WM_LBUTTONUP && impl)
    {
        RECT rc;
        int mx = (short)LOWORD( lp ), my = (short)HIWORD( lp );
        GetClientRect( hwnd, &rc );
        if (my >= rc.bottom - 52 && my <= rc.bottom - 14)
        {
            int btn_w = (rc.right - rc.left - 36) / 3;
            if (mx >= rc.left + 12 && mx < rc.left + 12 + btn_w && impl->dialog_primary_text[0])
                uwp_cd_fire_closed( impl, 1 );
            else if (mx >= rc.left + 18 + btn_w && mx < rc.left + 18 + 2 * btn_w && impl->dialog_secondary_text[0])
                uwp_cd_fire_closed( impl, 2 );
            else if (mx >= rc.left + 24 + 2 * btn_w)
                uwp_cd_fire_closed( impl, 0 );
        }
        return 0;
    }
    if (msg == WM_PAINT && impl)
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint( hwnd, &ps );
        if (hdc)
        {
            RECT rc, trc, brc;
            HBRUSH bg_br = CreateSolidBrush( RGB( 43, 43, 43 ) );
            HBRUSH bd_br = CreateSolidBrush( RGB( 80, 80, 80 ) );
            HBRUSH p_br  = CreateSolidBrush( RGB( 0, 120, 212 ) );
            HBRUSH s_br  = CreateSolidBrush( RGB( 60, 60, 60 ) );
            HFONT f_title = CreateFontW( -18, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                         DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                         CLEARTYPE_QUALITY, FF_SWISS, L"Segoe UI" );
            HFONT f_body  = CreateFontW( -14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                         DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                         CLEARTYPE_QUALITY, FF_SWISS, L"Segoe UI" );
            HGDIOBJ old_f = SelectObject( hdc, f_title );
            int btn_w;

            GetClientRect( hwnd, &rc );
            FillRect( hdc, &rc, bg_br );
            FrameRect( hdc, &rc, bd_br );
            SetBkMode( hdc, TRANSPARENT );
            SetTextColor( hdc, RGB( 255, 255, 255 ) );

            SetRect( &trc, 20, 16, rc.right - 20, 46 );
            DrawTextW( hdc, impl->dialog_title[0] ? impl->dialog_title : L"Notepad", -1, &trc,
                       DT_LEFT | DT_VCENTER | DT_SINGLELINE );

            SelectObject( hdc, f_body );
            SetRect( &brc, 20, 52, rc.right - 20, rc.bottom - 62 );
            DrawTextW( hdc, uwp_elem_text( impl ), -1, &brc, DT_LEFT | DT_WORDBREAK );

            btn_w = (rc.right - rc.left - 36) / 3;
            if (impl->dialog_primary_text[0])
            {
                RECT r1 = { 12, rc.bottom - 46, 12 + btn_w, rc.bottom - 14 };
                FillRect( hdc, &r1, p_br );
                DrawTextW( hdc, impl->dialog_primary_text, -1, &r1, DT_CENTER | DT_VCENTER | DT_SINGLELINE );
            }
            if (impl->dialog_secondary_text[0])
            {
                RECT r2 = { 18 + btn_w, rc.bottom - 46, 18 + 2 * btn_w, rc.bottom - 14 };
                FillRect( hdc, &r2, s_br );
                DrawTextW( hdc, impl->dialog_secondary_text, -1, &r2, DT_CENTER | DT_VCENTER | DT_SINGLELINE );
            }
            {
                RECT r3 = { 24 + 2 * btn_w, rc.bottom - 46, rc.right - 12, rc.bottom - 14 };
                FillRect( hdc, &r3, s_br );
                DrawTextW( hdc, impl->dialog_close_text[0] ? impl->dialog_close_text : L"Cancel", -1, &r3,
                           DT_CENTER | DT_VCENTER | DT_SINGLELINE );
            }

            SelectObject( hdc, old_f );
            DeleteObject( f_body );
            DeleteObject( f_title );
            DeleteObject( s_br );
            DeleteObject( p_br );
            DeleteObject( bd_br );
            DeleteObject( bg_br );
        }
        EndPaint( hwnd, &ps );
        return 0;
    }
    return DefWindowProcW( hwnd, msg, wp, lp );
}

static HRESULT WINAPI uwp_cd_get_Title( void *iface, IInspectable **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    *out = impl ? impl->dialog_title_obj : NULL;
    if (*out) IInspectable_AddRef( *out );
    return S_OK;
}
static HRESULT WINAPI uwp_cd_put_Title( void *iface, IInspectable *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return S_OK;
    if (impl->dialog_title_obj) IInspectable_Release( impl->dialog_title_obj );
    impl->dialog_title_obj = val;
    if (val)
    {
        IInspectable_AddRef( val );
        uwp_unbox_string( val, impl->dialog_title, ARRAY_SIZE(impl->dialog_title) );
    }
    return S_OK;
}
static HRESULT WINAPI uwp_cd_get_NullOut( void *iface, void **out )
{
    if (out) *out = NULL;
    return S_OK;
}
static HRESULT WINAPI uwp_cd_get_BoolFalse( void *iface, boolean *out )
{
    if (out) *out = FALSE;
    return S_OK;
}
static HRESULT WINAPI uwp_cd_get_BoolTrue( void *iface, boolean *out )
{
    if (out) *out = TRUE;
    return S_OK;
}
static HRESULT WINAPI uwp_cd_get_PrimaryButtonText( void *iface, HSTRING *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    return WindowsCreateString( impl ? impl->dialog_primary_text : L"",
                                impl ? wcslen(impl->dialog_primary_text) : 0, out );
}
static HRESULT WINAPI uwp_cd_put_PrimaryButtonText( void *iface, HSTRING val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    const WCHAR *raw = ((ULONG_PTR)val >= 0x10000) ? WindowsGetStringRawBuffer( val, NULL ) : L"";
    if (impl) lstrcpynW( impl->dialog_primary_text, raw ? raw : L"", ARRAY_SIZE(impl->dialog_primary_text) );
    return S_OK;
}
static HRESULT WINAPI uwp_cd_get_SecondaryButtonText( void *iface, HSTRING *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    return WindowsCreateString( impl ? impl->dialog_secondary_text : L"",
                                impl ? wcslen(impl->dialog_secondary_text) : 0, out );
}
static HRESULT WINAPI uwp_cd_put_SecondaryButtonText( void *iface, HSTRING val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    const WCHAR *raw = ((ULONG_PTR)val >= 0x10000) ? WindowsGetStringRawBuffer( val, NULL ) : L"";
    if (impl) lstrcpynW( impl->dialog_secondary_text, raw ? raw : L"", ARRAY_SIZE(impl->dialog_secondary_text) );
    return S_OK;
}
static HRESULT WINAPI uwp_cd_Hide( void *iface )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl) uwp_cd_fire_closed( impl, 0 );
    return S_OK;
}
static const GUID IID_IAsyncOperation_ContentDialogResult_UWP =
    { 0x1f23bdd1, 0x06dc, 0x5be9, { 0x9a, 0x60, 0x0b, 0x4d, 0x94, 0xd4, 0xd7, 0x2c } };
static const GUID IID_IAsyncInfo_cd =
    { 0x00000036, 0x0000, 0x0000, { 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };

struct cd_async_op
{
    const void *lpVtbl_op;
    const void *lpVtbl_info;
    LONG ref;
    void *handler;
};

static HRESULT WINAPI cd_op_QI( void *iface, REFIID iid, void **out )
{
    struct cd_async_op *impl = CONTAINING_RECORD( iface, struct cd_async_op, lpVtbl_op );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IAsyncOperation_ContentDialogResult_UWP ))
    {
        *out = &impl->lpVtbl_op;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IAsyncInfo_cd ))
    {
        *out = &impl->lpVtbl_info;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI cd_op_AddRef( void *iface )
{
    return InterlockedIncrement( &CONTAINING_RECORD( iface, struct cd_async_op, lpVtbl_op )->ref );
}
static ULONG WINAPI cd_op_Release( void *iface )
{
    struct cd_async_op *impl = CONTAINING_RECORD( iface, struct cd_async_op, lpVtbl_op );
    ULONG r = InterlockedDecrement( &impl->ref );
    if (!r)
    {
        if (impl->handler) IUnknown_Release( (IUnknown *)impl->handler );
        free( impl );
    }
    return r;
}
static HRESULT WINAPI cd_op_GetIids( void *iface, ULONG *c, IID **i ) { if (c) *c = 0; if (i) *i = NULL; return S_OK; }
static HRESULT WINAPI cd_op_GetRuntimeClassName( void *iface, HSTRING *n )
{
    static const WCHAR cls[] = L"Windows.Foundation.IAsyncOperation`1<Windows.UI.Xaml.Controls.ContentDialogResult>";
    return WindowsCreateString( cls, ARRAY_SIZE(cls) - 1, n );
}
static HRESULT WINAPI cd_op_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI cd_op_put_Completed( void *iface, void *handler )
{
    struct cd_async_op *impl = CONTAINING_RECORD( iface, struct cd_async_op, lpVtbl_op );
    if (impl->handler) IUnknown_Release( (IUnknown *)impl->handler );
    impl->handler = handler;
    if (handler)
    {
        typedef HRESULT (WINAPI *invoke_fn)(void *, void *, UINT32);
        void **vtbl = *(void ***)handler;
        IUnknown_AddRef( (IUnknown *)handler );
        ((invoke_fn)vtbl[3])( handler, &impl->lpVtbl_op, 1 /* Completed */ );
    }
    return S_OK;
}
static HRESULT WINAPI cd_op_get_Completed( void *iface, void **out )
{
    struct cd_async_op *impl = CONTAINING_RECORD( iface, struct cd_async_op, lpVtbl_op );
    if (!out) return E_POINTER;
    *out = impl->handler;
    if (*out) IUnknown_AddRef( (IUnknown *)*out );
    return S_OK;
}
static HRESULT WINAPI cd_op_GetResults( void *iface, INT32 *res )
{
    if (res) *res = 0; /* ContentDialogResult_None */
    return S_OK;
}
static HRESULT WINAPI cd_info_QI( void *iface, REFIID iid, void **out )
{
    return cd_op_QI( &CONTAINING_RECORD( iface, struct cd_async_op, lpVtbl_info )->lpVtbl_op, iid, out );
}
static ULONG WINAPI cd_info_AddRef( void *iface )
{
    return cd_op_AddRef( &CONTAINING_RECORD( iface, struct cd_async_op, lpVtbl_info )->lpVtbl_op );
}
static ULONG WINAPI cd_info_Release( void *iface )
{
    return cd_op_Release( &CONTAINING_RECORD( iface, struct cd_async_op, lpVtbl_info )->lpVtbl_op );
}
static HRESULT WINAPI cd_info_get_Id( void *iface, UINT32 *id ) { if (id) *id = 1; return S_OK; }
static HRESULT WINAPI cd_info_get_Status( void *iface, UINT32 *st ) { if (st) *st = 1; return S_OK; }
static HRESULT WINAPI cd_info_get_ErrorCode( void *iface, HRESULT *hr ) { if (hr) *hr = S_OK; return S_OK; }
static HRESULT WINAPI cd_info_Cancel( void *iface ) { return S_OK; }
static HRESULT WINAPI cd_info_Close( void *iface ) { return S_OK; }

static const void * const cd_op_vtbl[] =
{
    cd_op_QI, cd_op_AddRef, cd_op_Release, cd_op_GetIids, cd_op_GetRuntimeClassName, cd_op_GetTrustLevel,
    cd_op_put_Completed, cd_op_get_Completed, cd_op_GetResults,
};
static const void * const cd_info_vtbl[] =
{
    cd_info_QI, cd_info_AddRef, cd_info_Release, cd_op_GetIids, cd_op_GetRuntimeClassName, cd_op_GetTrustLevel,
    cd_info_get_Id, cd_info_get_Status, cd_info_get_ErrorCode, cd_info_Cancel, cd_info_Close,
};

static HRESULT WINAPI uwp_cd_ShowAsync( void *iface, void **out )
{
    static BOOL cls_reg = FALSE;
    struct uwp_element *impl = uwp_elem_from_any( iface );
    struct cd_async_op *op;
    HWND owner = NULL;
    RECT orc = { 100, 100, 900, 700 };
    int dw = 380, dh = 180, dx, dy;

    if (out)
    {
        if ((op = calloc( 1, sizeof(*op) )))
        {
            op->lpVtbl_op = cd_op_vtbl;
            op->lpVtbl_info = cd_info_vtbl;
            op->ref = 1;
            *out = &op->lpVtbl_op;
        }
        else *out = NULL;
    }
    if (!impl) return S_OK;

    uwp_fire_loaded_tree( impl );

    if (g_island_hwnd_cnt > 0 && g_island_hwnds[0])
        owner = GetAncestor( g_island_hwnds[0], GA_ROOT );
    if (!owner) owner = GetActiveWindow();
    if (owner && IsWindow( owner ))
        GetWindowRect( owner, &orc );
    dx = (orc.left + orc.right - dw) / 2;
    dy = (orc.top + orc.bottom - dh) / 2;

    if (!cls_reg)
    {
        WNDCLASSEXW wc = { .cbSize = sizeof(wc) };
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = uwp_cd_popup_wndproc;
        wc.hInstance = GetModuleHandleW( NULL );
        wc.hCursor = LoadCursorW( NULL, (LPCWSTR)IDC_ARROW );
        wc.lpszClassName = L"WineUwpContentDialogPopup";
        RegisterClassExW( &wc );
        cls_reg = TRUE;
    }
    if (impl->dialog_hwnd && IsWindow( impl->dialog_hwnd ))
        DestroyWindow( impl->dialog_hwnd );
    impl->dialog_hwnd = CreateWindowExW( WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
                                         L"WineUwpContentDialogPopup", L"",
                                         WS_POPUP | WS_VISIBLE,
                                         dx, dy, dw, dh,
                                         owner, NULL, GetModuleHandleW( NULL ), impl );
    if (impl->dialog_hwnd)
    {
        InvalidateRect( impl->dialog_hwnd, NULL, FALSE );
        UpdateWindow( impl->dialog_hwnd );
        PostMessageW( impl->dialog_hwnd, WM_APP + 1, 0, 0 );
    }
    return S_OK;
}

const void *uwp_cd_vtbl[48] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_cd_get_Title,
    [7] = uwp_cd_put_Title,
    [8] = uwp_cd_get_NullOut,
    [9] = uwp_generic_noop,
    [10] = uwp_cd_get_BoolFalse,
    [11] = uwp_generic_noop,
    [12] = uwp_cd_get_PrimaryButtonText,
    [13] = uwp_cd_put_PrimaryButtonText,
    [14] = uwp_cd_get_SecondaryButtonText,
    [15] = uwp_cd_put_SecondaryButtonText,
    [16] = uwp_cd_get_NullOut,
    [17] = uwp_generic_noop,
    [18] = uwp_cd_get_NullOut,
    [19] = uwp_generic_noop,
    [20] = uwp_cd_get_NullOut,
    [21] = uwp_generic_noop,
    [22] = uwp_cd_get_NullOut,
    [23] = uwp_generic_noop,
    [24] = uwp_cd_get_BoolTrue,
    [25] = uwp_generic_noop,
    [26] = uwp_cd_get_BoolTrue,
    [27] = uwp_generic_noop,
    [28] = uwp_ui_event_add,
    [29] = uwp_generic_noop,
    [30] = uwp_flyout_add_Closed,
    [31] = uwp_generic_noop,
    [32] = uwp_ui_event_add,
    [33] = uwp_generic_noop,
    [34] = uwp_ui_event_add,
    [35] = uwp_generic_noop,
    [36] = uwp_ui_event_add,
    [37] = uwp_generic_noop,
    [38] = uwp_cd_Hide,
    [39] = uwp_cd_ShowAsync,
    [40 ... 47] = uwp_generic_noop,
};

static const GUID IID_IAutoSuggestBoxTextChangedEventArgs_UWP =
    { 0x3a6f7254, 0x1ed5, 0x4bc5, { 0xa0, 0x60, 0x65, 0x55, 0x30, 0xbc, 0xa6, 0xba } };
static const GUID IID_IAutoSuggestBoxTextChangedEventArgs_Alt_UWP =
    { 0x38c5e3a3, 0x5f44, 0x47f4, { 0xa4, 0x50, 0x1c, 0x5e, 0x1f, 0x4d, 0x4b, 0x9f } };
static const GUID IID_IAutoSuggestBoxQuerySubmittedEventArgs_UWP =
    { 0x78dcb116, 0x818a, 0x4cb5, { 0xbc, 0xa7, 0x38, 0x2c, 0xe6, 0xdd, 0xc9, 0x0d } };
static const GUID IID_IAutoSuggestBoxQuerySubmittedEventArgs_Alt_UWP =
    { 0xd5f5b0d0, 0x7d72, 0x4c76, { 0x9e, 0x77, 0x28, 0xa3, 0xd5, 0xa2, 0xe7, 0x11 } };
static const GUID IID_IAutoSuggestBoxSuggestionChosenEventArgs_UWP =
    { 0x396f7254, 0x1ed5, 0x4bc5, { 0xa0, 0x60, 0x65, 0x55, 0x30, 0xbc, 0xa6, 0xba } };

struct asb_tc_args
{
    const void **lpVtbl;
    LONG ref;
    INT32 reason;
};

static HRESULT WINAPI asb_tc_QI( void *iface, REFIID iid, void **out )
{
    struct asb_tc_args *a = iface;
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IAutoSuggestBoxTextChangedEventArgs_UWP ) ||
        IsEqualGUID( iid, &IID_IAutoSuggestBoxTextChangedEventArgs_Alt_UWP ))
    {
        InterlockedIncrement( &a->ref );
        *out = iface;
        return S_OK;
    }
    if (!IsEqualGUID( iid, &IID_IReferenceTracker ))
        FIXME( "asb_tc_args unsupported iid %s\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI asb_tc_AddRef( void *iface ) { return InterlockedIncrement( &((struct asb_tc_args *)iface)->ref ); }
static ULONG WINAPI asb_tc_Release( void *iface )
{
    struct asb_tc_args *a = iface;
    ULONG r = InterlockedDecrement( &a->ref );
    if (!r) free( a );
    return r;
}
static HRESULT WINAPI asb_tc_GetIids( void *iface, ULONG *cnt, IID **iids )
{
    IID *out;
    if (!cnt || !iids) return E_POINTER;
    out = CoTaskMemAlloc( sizeof(IID) );
    if (!out) return E_OUTOFMEMORY;
    out[0] = IID_IAutoSuggestBoxTextChangedEventArgs_UWP;
    *cnt = 1;
    *iids = out;
    return S_OK;
}
static HRESULT WINAPI asb_tc_GetRuntimeClassName( void *iface, HSTRING *name )
{
    static const WCHAR cls[] = L"Windows.UI.Xaml.Controls.AutoSuggestBoxTextChangedEventArgs";
    return WindowsCreateString( cls, ARRAY_SIZE(cls) - 1, name );
}
static HRESULT WINAPI asb_tc_GetTrustLevel( void *iface, TrustLevel *lvl )
{
    if (lvl) *lvl = BaseTrust;
    return S_OK;
}
static HRESULT WINAPI asb_tc_get_Reason( void *iface, INT32 *val )
{
    struct asb_tc_args *a = iface;
    if (val) *val = a->reason;
    return S_OK;
}
static HRESULT WINAPI asb_tc_put_Reason( void *iface, INT32 val )
{
    struct asb_tc_args *a = iface;
    a->reason = val;
    return S_OK;
}
static HRESULT WINAPI asb_tc_CheckCurrent( void *iface, boolean *val )
{
    if (val) *val = TRUE;
    return S_OK;
}
static const void *asb_tc_vtbl[] =
{
    asb_tc_QI, asb_tc_AddRef, asb_tc_Release,
    asb_tc_GetIids, asb_tc_GetRuntimeClassName, asb_tc_GetTrustLevel,
    asb_tc_get_Reason, asb_tc_put_Reason, asb_tc_CheckCurrent,
};

struct asb_qs_args
{
    const void **lpVtbl;
    LONG ref;
    HSTRING query_text;
    IInspectable *chosen_suggestion;
};

static HRESULT WINAPI asb_qs_QI( void *iface, REFIID iid, void **out )
{
    struct asb_qs_args *a = iface;
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IAutoSuggestBoxQuerySubmittedEventArgs_UWP ) ||
        IsEqualGUID( iid, &IID_IAutoSuggestBoxQuerySubmittedEventArgs_Alt_UWP ))
    {
        InterlockedIncrement( &a->ref );
        *out = iface;
        return S_OK;
    }
    if (!IsEqualGUID( iid, &IID_IReferenceTracker ))
        FIXME( "asb_qs_args unsupported iid %s\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI asb_qs_AddRef( void *iface ) { return InterlockedIncrement( &((struct asb_qs_args *)iface)->ref ); }
static ULONG WINAPI asb_qs_Release( void *iface )
{
    struct asb_qs_args *a = iface;
    ULONG r = InterlockedDecrement( &a->ref );
    if (!r)
    {
        if (a->query_text) WindowsDeleteString( a->query_text );
        if (a->chosen_suggestion) IInspectable_Release( a->chosen_suggestion );
        free( a );
    }
    return r;
}
static HRESULT WINAPI asb_qs_GetIids( void *iface, ULONG *cnt, IID **iids )
{
    IID *out;
    if (!cnt || !iids) return E_POINTER;
    out = CoTaskMemAlloc( sizeof(IID) );
    if (!out) return E_OUTOFMEMORY;
    out[0] = IID_IAutoSuggestBoxQuerySubmittedEventArgs_UWP;
    *cnt = 1;
    *iids = out;
    return S_OK;
}
static HRESULT WINAPI asb_qs_GetRuntimeClassName( void *iface, HSTRING *name )
{
    static const WCHAR cls[] = L"Windows.UI.Xaml.Controls.AutoSuggestBoxQuerySubmittedEventArgs";
    return WindowsCreateString( cls, ARRAY_SIZE(cls) - 1, name );
}
static HRESULT WINAPI asb_qs_GetTrustLevel( void *iface, TrustLevel *lvl )
{
    if (lvl) *lvl = BaseTrust;
    return S_OK;
}
static HRESULT WINAPI asb_qs_get_QueryText( void *iface, HSTRING *val )
{
    struct asb_qs_args *a = iface;
    if (!val) return E_POINTER;
    return WindowsDuplicateString( a->query_text, val );
}
static HRESULT WINAPI asb_qs_get_ChosenSuggestion( void *iface, IInspectable **val )
{
    struct asb_qs_args *a = iface;
    if (!val) return E_POINTER;
    *val = a->chosen_suggestion;
    if (*val) IInspectable_AddRef( *val );
    return S_OK;
}
static const void *asb_qs_vtbl[] =
{
    asb_qs_QI, asb_qs_AddRef, asb_qs_Release,
    asb_qs_GetIids, asb_qs_GetRuntimeClassName, asb_qs_GetTrustLevel,
    asb_qs_get_QueryText, asb_qs_get_ChosenSuggestion,
};

struct asb_sc_args
{
    const void **lpVtbl;
    LONG ref;
    IInspectable *selected_item;
};

static HRESULT WINAPI asb_sc_QI( void *iface, REFIID iid, void **out )
{
    struct asb_sc_args *a = iface;
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IAutoSuggestBoxSuggestionChosenEventArgs_UWP ))
    {
        InterlockedIncrement( &a->ref );
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI asb_sc_AddRef( void *iface ) { return InterlockedIncrement( &((struct asb_sc_args *)iface)->ref ); }
static ULONG WINAPI asb_sc_Release( void *iface )
{
    struct asb_sc_args *a = iface;
    ULONG r = InterlockedDecrement( &a->ref );
    if (!r)
    {
        if (a->selected_item) IInspectable_Release( a->selected_item );
        free( a );
    }
    return r;
}
static HRESULT WINAPI asb_sc_GetIids( void *iface, ULONG *cnt, IID **iids )
{
    IID *out;
    if (!cnt || !iids) return E_POINTER;
    out = CoTaskMemAlloc( sizeof(IID) );
    if (!out) return E_OUTOFMEMORY;
    out[0] = IID_IAutoSuggestBoxSuggestionChosenEventArgs_UWP;
    *cnt = 1;
    *iids = out;
    return S_OK;
}
static HRESULT WINAPI asb_sc_GetRuntimeClassName( void *iface, HSTRING *name )
{
    static const WCHAR cls[] = L"Windows.UI.Xaml.Controls.AutoSuggestBoxSuggestionChosenEventArgs";
    return WindowsCreateString( cls, ARRAY_SIZE(cls) - 1, name );
}
static HRESULT WINAPI asb_sc_get_SelectedItem( void *iface, IInspectable **val )
{
    struct asb_sc_args *a = iface;
    if (!val) return E_POINTER;
    *val = a->selected_item;
    if (*val) IInspectable_AddRef( *val );
    return S_OK;
}
static const void *asb_sc_vtbl[] =
{
    asb_sc_QI, asb_sc_AddRef, asb_sc_Release,
    asb_sc_GetIids, asb_sc_GetRuntimeClassName, asb_qs_GetTrustLevel,
    asb_sc_get_SelectedItem,
};

static HRESULT WINAPI uwp_asb_add_SuggestionChosen( void *iface, void *handler, EventRegistrationToken *tok )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return E_NOINTERFACE;
    return xaml_event_registry_add( &impl->asb_suggestion_chosen_reg, handler, tok );
}
static HRESULT WINAPI uwp_asb_remove_SuggestionChosen( void *iface, EventRegistrationToken tok )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return E_NOINTERFACE;
    return xaml_event_registry_remove( &impl->asb_suggestion_chosen_reg, tok );
}
static HRESULT WINAPI uwp_asb_add_TextChanged( void *iface, void *handler, EventRegistrationToken *tok )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return E_NOINTERFACE;
    TRACE( "uwp_asb_add_TextChanged on %s (%s) handler=%p\n", debugstr_w(impl->class_name), debugstr_w(impl->name), handler );
    return xaml_event_registry_add( &impl->asb_text_changed_reg, handler, tok );
}
static HRESULT WINAPI uwp_asb_remove_TextChanged( void *iface, EventRegistrationToken tok )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return E_NOINTERFACE;
    return xaml_event_registry_remove( &impl->asb_text_changed_reg, tok );
}
static HRESULT WINAPI uwp_asb_add_QuerySubmitted( void *iface, void *handler, EventRegistrationToken *tok )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return E_NOINTERFACE;
    TRACE( "uwp_asb_add_QuerySubmitted on %s (%s) handler=%p\n", debugstr_w(impl->class_name), debugstr_w(impl->name), handler );
    return xaml_event_registry_add( &impl->asb_query_submitted_reg, handler, tok );
}
static HRESULT WINAPI uwp_asb_remove_QuerySubmitted( void *iface, EventRegistrationToken tok )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return E_NOINTERFACE;
    return xaml_event_registry_remove( &impl->asb_query_submitted_reg, tok );
}

void uwp_asb_fire_text_changed( struct uwp_element *el )
{
    void **handlers = NULL;
    unsigned int cnt, i;
    struct asb_tc_args *args;
    IInspectable *sender;

    if (!el) return;
    if (el->dp_cb_cnt > 0)
    {
        struct xaml_event_handler *cbs_snap[32];
        void *dps_snap[32];
        int snap_cnt = 0, ci;

        AcquireSRWLockShared( &el->dp_lock );
        for (ci = 0; ci < el->dp_cb_cnt && snap_cnt < (int)ARRAY_SIZE(cbs_snap); ci++)
        {
            if (el->dp_cbs[ci].callback)
            {
                cbs_snap[snap_cnt] = (struct xaml_event_handler *)el->dp_cbs[ci].callback;
                dps_snap[snap_cnt] = el->dp_cbs[ci].dp;
                IUnknown_AddRef( (IUnknown *)cbs_snap[snap_cnt] );
                snap_cnt++;
            }
        }
        ReleaseSRWLockShared( &el->dp_lock );

        for (ci = 0; ci < snap_cnt; ci++)
        {
            void *dp_sender = el->outer ? (void *)el->outer : (void *)&el->IDependencyObject_iface;
            struct xaml_event_handler *h = cbs_snap[ci];
            __TRY { h->lpVtbl->Invoke( h, dp_sender, dps_snap[ci] ); }
            __EXCEPT_ALL { }
            __ENDTRY;
            IUnknown_Release( (IUnknown *)h );
        }
    }
    cnt = xaml_event_registry_snapshot_alloc( &el->asb_text_changed_reg, &handlers );
    if (!cnt) return;
    args = calloc( 1, sizeof(*args) );
    if (!args)
    {
        for (i = 0; i < cnt; i++) IUnknown_Release( (IUnknown *)handlers[i] );
        free( handlers );
        return;
    }
    args->lpVtbl = asb_tc_vtbl;
    args->ref = 1;
    args->reason = 0; /* UserInput */
    sender = el->outer ? el->outer : &el->IInspectable_iface;
    for (i = 0; i < cnt; i++)
    {
        struct xaml_event_handler *h = (struct xaml_event_handler *)handlers[i];
        __TRY { h->lpVtbl->Invoke( h, sender, (IInspectable *)args ); }
        __EXCEPT_ALL { TRACE( "uwp_asb_fire_text_changed handler %u faulted\n", i ); }
        __ENDTRY;
        IUnknown_Release( (IUnknown *)h );
    }
    free( handlers );
    asb_tc_Release( args );
}

void uwp_asb_fire_suggestion_chosen( struct uwp_element *el, IInspectable *chosen )
{
    void **handlers = NULL;
    unsigned int cnt, i;
    struct asb_sc_args *args;
    IInspectable *sender;

    if (!el || !chosen) return;
    cnt = xaml_event_registry_snapshot_alloc( &el->asb_suggestion_chosen_reg, &handlers );
    if (!cnt) return;
    args = calloc( 1, sizeof(*args) );
    if (!args)
    {
        for (i = 0; i < cnt; i++) IUnknown_Release( (IUnknown *)handlers[i] );
        free( handlers );
        return;
    }
    args->lpVtbl = asb_sc_vtbl;
    args->ref = 1;
    args->selected_item = chosen;
    IInspectable_AddRef( chosen );
    sender = el->outer ? el->outer : &el->IInspectable_iface;
    for (i = 0; i < cnt; i++)
    {
        struct xaml_event_handler *h = (struct xaml_event_handler *)handlers[i];
        __TRY { h->lpVtbl->Invoke( h, sender, (IInspectable *)args ); }
        __EXCEPT_ALL { TRACE( "uwp_asb_fire_suggestion_chosen handler %u faulted\n", i ); }
        __ENDTRY;
        IUnknown_Release( (IUnknown *)h );
    }
    free( handlers );
    asb_sc_Release( args );
}

void uwp_asb_fire_query_submitted_ex( struct uwp_element *el, IInspectable *chosen )
{
    void **handlers = NULL;
    unsigned int cnt, i;
    struct asb_qs_args *args;
    IInspectable *sender;

    if (!el) return;
    cnt = xaml_event_registry_snapshot_alloc( &el->asb_query_submitted_reg, &handlers );
    if (!cnt) return;
    args = calloc( 1, sizeof(*args) );
    if (!args)
    {
        for (i = 0; i < cnt; i++) IUnknown_Release( (IUnknown *)handlers[i] );
        free( handlers );
        return;
    }
    args->lpVtbl = asb_qs_vtbl;
    args->ref = 1;
    {
        const WCHAR *qt = uwp_elem_text( el );
        WindowsCreateString( qt, (UINT32)wcslen( qt ), &args->query_text );
    }
    if (chosen)
    {
        args->chosen_suggestion = chosen;
        IInspectable_AddRef( chosen );
    }
    sender = el->outer ? el->outer : &el->IInspectable_iface;
    TRACE( "uwp_asb_fire_query_submitted on %s (%s) query=%s chosen=%p handlers=%u\n",
           debugstr_w(el->class_name), debugstr_w(el->name), debugstr_w(uwp_elem_text( el )), chosen, cnt );
    for (i = 0; i < cnt; i++)
    {
        struct xaml_event_handler *h = (struct xaml_event_handler *)handlers[i];
        __TRY { h->lpVtbl->Invoke( h, sender, (IInspectable *)args ); }
        __EXCEPT_ALL { TRACE( "uwp_asb_fire_query_submitted handler %u faulted\n", i ); }
        __ENDTRY;
        IUnknown_Release( (IUnknown *)h );
    }
    free( handlers );
    asb_qs_Release( args );
}

void uwp_asb_fire_query_submitted( struct uwp_element *el )
{
    uwp_asb_fire_query_submitted_ex( el, NULL );
}

int uwp_asb_get_suggestions( struct uwp_element *asb, IInspectable **out_items, int max_items )
{
    int count = 0;
    if (!asb || !out_items || max_items <= 0) return 0;
    if (!uwp_is_autosuggest( asb )) return 0;
    if (asb->items_source)
        count = (int)uwp_winrt_vec_snapshot( asb->items_source, out_items, (unsigned int)max_items );
    if (count == 0 && asb->children_coll)
    {
        struct uwp_vec_view *v = CONTAINING_RECORD( asb->children_coll, struct uwp_vec_view, IInspectable_iface );
        unsigned int i;
        for (i = 0; v && i < v->count && count < max_items; i++)
        {
            if (v->items[i])
            {
                out_items[count] = v->items[i];
                IInspectable_AddRef( out_items[count] );
                count++;
            }
        }
    }
    return count;
}

BOOL uwp_asb_compute_popup_rect( const struct uwp_element *asb, int count, int win_w, int win_h,
                                 RECT *out_rc, int *out_item_h, int *out_vis_cnt, int *out_pad_y )
{
    float sc = (g_layout_scale > 0.25f) ? g_layout_scale : 1.0f;
    int item_h = max( 32, (int)(38.0f * sc + 0.5f) );
    int pad_y = max( 4, (int)(6.0f * sc + 0.5f) );
    int vis_cnt, pop_w, pop_h, x0, y0, bw;

    if (!asb || !asb->has_bounds || asb->visibility != 0 || count <= 0 || !out_rc) return FALSE;
    vis_cnt = min( count, 8 );
    bw = asb->bounds.right - asb->bounds.left;
    if (bw >= 560)
    {
        pop_w = 560;
        x0 = asb->bounds.left + (bw - pop_w) / 2;
    }
    else
    {
        pop_w = max( bw, (int)(320.0f * sc + 0.5f) );
        x0 = asb->bounds.left;
    }
    pop_h = pad_y * 2 + vis_cnt * item_h;
    if (win_w > 0 && x0 + pop_w > win_w - 4) x0 = max( 4, win_w - 4 - pop_w );
    y0 = asb->bounds.bottom + 2;
    if (win_h > 0 && y0 + pop_h > win_h - 4)
        pop_h = max( item_h + pad_y * 2, win_h - 4 - y0 );
    SetRect( out_rc, x0, y0, x0 + pop_w, y0 + pop_h );
    if (out_item_h) *out_item_h = item_h;
    if (out_vis_cnt) *out_vis_cnt = vis_cnt;
    if (out_pad_y) *out_pad_y = pad_y;
    return TRUE;
}

static void uwp_asb_collect_el_texts( const struct uwp_element *el, WCHAR *out_p, size_t max_p,
                                      WCHAR *out_s, size_t max_s, int depth )
{
    WCHAR rich_buf[512];
    const WCHAR *txt;
    if (!el || depth > 8 || el->visibility != 0) return;
    txt = uwp_elem_text( el );
    if (!txt[0] && el->inlines_coll && uwp_flatten_inlines_text( el, rich_buf, ARRAY_SIZE(rich_buf) ) > 0)
        txt = rich_buf;
    if (!txt[0] && el->automation_name[0])
        txt = el->automation_name;
    if (txt[0] && !uwp_is_font_icon( el ) && !(wcslen( txt ) == 1 && (UINT)txt[0] >= 0xE000))
    {
        if (!out_p[0])
            lstrcpynW( out_p, txt, (int)max_p );
        else if (out_s && max_s > 0 && !out_s[0] && wcscmp( out_p, txt ) != 0)
            lstrcpynW( out_s, txt, (int)max_s );
    }
    if (el->content)
    {
        struct uwp_element *ch = uwp_elem_from_any( el->content );
        if (ch && ch != el) uwp_asb_collect_el_texts( ch, out_p, max_p, out_s, max_s, depth + 1 );
    }
    if (el->children_coll)
    {
        struct uwp_vec_view *v = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        unsigned int i;
        for (i = 0; v && i < v->count; i++)
        {
            struct uwp_element *ch = uwp_elem_from_any( v->items[i] );
            if (ch && ch != el) uwp_asb_collect_el_texts( ch, out_p, max_p, out_s, max_s, depth + 1 );
        }
    }
}

void uwp_asb_get_item_label( struct uwp_element *asb, int idx, IInspectable *item,
                             WCHAR *out_primary, size_t max_primary,
                             WCHAR *out_secondary, size_t max_secondary )
{
    static const WCHAR *const prim_props[] =
        { L"Title", L"ProductTitle", L"DisplayTitle", L"DisplayText", L"QueryText",
          L"Term", L"SuggestedTerm", L"SuggestionText", L"Text", L"Name", L"Label", L"Header" };
    static const WCHAR *const sec_props[] =
        { L"Category", L"Subtitle", L"SubTitle", L"PublisherName", L"DisplayPrice", L"MediaType" };
    struct uwp_element *item_el;

    if (out_primary && max_primary > 0) out_primary[0] = 0;
    if (out_secondary && max_secondary > 0) out_secondary[0] = 0;
    if (!out_primary || max_primary == 0) return;

    if (asb && asb->children_coll && idx >= 0)
    {
        struct uwp_vec_view *v = CONTAINING_RECORD( asb->children_coll, struct uwp_vec_view, IInspectable_iface );
        if (v && (unsigned int)idx < v->count)
        {
            struct uwp_element *cel = uwp_elem_from_any( v->items[idx] );
            if (cel)
                uwp_asb_collect_el_texts( cel, out_primary, max_primary, out_secondary, max_secondary, 0 );
        }
    }
    item_el = uwp_elem_from_any( item );
    if (!out_primary[0] && item_el)
        uwp_asb_collect_el_texts( item_el, out_primary, max_primary, out_secondary, max_secondary, 0 );
    if (!out_primary[0] && item)
    {
        uwp_vm_query_string_prop_depth( item, prim_props, ARRAY_SIZE(prim_props), out_primary, max_primary, 2 );
        if (out_secondary && max_secondary > 0 && !out_secondary[0])
            uwp_vm_query_string_prop_depth( item, sec_props, ARRAY_SIZE(sec_props), out_secondary, max_secondary, 2 );
    }
    if (!out_primary[0] && item)
        uwp_inspectable_to_string( item, out_primary, max_primary );
}

static HRESULT WINAPI uwp_asb_get_IsSuggestionListOpen( void *iface, boolean *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (val) *val = impl ? (boolean)impl->is_open : FALSE;
    return S_OK;
}
static HRESULT WINAPI uwp_asb_put_IsSuggestionListOpen( void *iface, boolean val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl)
    {
        impl->is_open = (val != 0);
        uwp_invalidate_window();
    }
    return S_OK;
}

const void *uwp_asb_vtbl[32] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6 ... 7] = uwp_generic_noop,
    [8] = uwp_asb_get_IsSuggestionListOpen,
    [9] = uwp_asb_put_IsSuggestionListOpen,
    [10] = uwp_tb_get_Text,
    [11] = uwp_generic_noop,
    [12] = uwp_tb_get_Text,
    [13] = uwp_tb_put_Text,
    [14] = uwp_cd_get_BoolTrue,
    [15] = uwp_generic_noop,
    [16] = uwp_tb_get_Text,
    [17] = uwp_generic_noop,
    [18] = uwp_cd_get_NullOut,
    [19] = uwp_generic_noop,
    [20] = uwp_cd_get_BoolTrue,
    [21] = uwp_generic_noop,
    [22] = uwp_cd_get_NullOut,
    [23] = uwp_generic_noop,
    [24] = uwp_asb_add_SuggestionChosen,
    [25] = uwp_asb_remove_SuggestionChosen,
    [26] = uwp_asb_add_TextChanged,
    [27] = uwp_asb_remove_TextChanged,
    [28] = uwp_asb_add_QuerySubmitted,
    [29] = uwp_asb_remove_QuerySubmitted,
    [30 ... 31] = uwp_generic_noop,
};

const void *uwp_asb2_vtbl[16] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_cd_get_NullOut,
    [7] = uwp_generic_noop,
    [8] = uwp_asb_add_QuerySubmitted,
    [9] = uwp_asb_remove_QuerySubmitted,
    [10 ... 15] = uwp_generic_noop,
};

static HRESULT WINAPI uwp_sv_get_IsPaneOpen( void *iface, boolean *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (val) *val = impl ? (boolean)impl->is_open : FALSE;
    return S_OK;
}
static HRESULT WINAPI uwp_sv_put_IsPaneOpen( void *iface, boolean val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl) impl->is_open = (val != 0);
    return S_OK;
}
static HRESULT WINAPI uwp_sv_get_OpenPaneLength( void *iface, double *val )
{
    if (val) *val = 320.0;
    return S_OK;
}
static HRESULT WINAPI uwp_sv_get_CompactPaneLength( void *iface, double *val )
{
    if (val) *val = 48.0;
    return S_OK;
}
static HRESULT WINAPI uwp_sv_get_IntZero( void *iface, INT32 *val )
{
    if (val) *val = 0;
    return S_OK;
}
static HRESULT WINAPI uwp_sv_get_DisplayMode( void *iface, INT32 *val )
{
    if (val) *val = 2; /* SplitViewDisplayMode_CompactInline */
    return S_OK;
}

static BOOL uwp_sv_would_cycle( struct uwp_element *parent, struct uwp_element *child )
{
    int guard = 0;
    struct uwp_element *cur = parent;
    if (!child || !parent) return FALSE;
    while (cur && guard++ < 64)
    {
        if (cur == child) return TRUE;
        cur = cur->parent;
    }
    return guard >= 64;
}

static HRESULT WINAPI uwp_sv_get_Content( void *iface, void **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    *out = impl ? impl->content : NULL;
    if (*out) IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}
static HRESULT WINAPI uwp_sv_put_Content( void *iface, void *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    struct uwp_element *ch;
    if (!impl) return S_OK;
    ch = uwp_elem_from_any( val );
    ERR( "uwp_sv_put_Content impl=%p(%s) val=%p(%s)\n",
         impl, debugstr_w(impl->class_name), val, ch ? debugstr_w(ch->class_name) : "?" );
    if (ch && uwp_sv_would_cycle( impl, ch ))
    {
        WARN( "uwp_sv_put_Content: cycle detected, skipping\n" );
        return S_OK;
    }
    if (impl->content) IInspectable_Release( impl->content );
    impl->content = (IInspectable *)val;
    if (impl->content) IInspectable_AddRef( impl->content );
    if (ch) ch->parent = impl;
    g_layout_dirty = TRUE;
    uwp_invalidate_window();
    return S_OK;
}
static HRESULT WINAPI uwp_sv_get_Pane( void *iface, void **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    *out = impl ? impl->pane : NULL;
    if (*out) IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}
static HRESULT WINAPI uwp_sv_put_Pane( void *iface, void *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    struct uwp_element *ch;
    if (!impl) return S_OK;
    if (impl->pane) IInspectable_Release( impl->pane );
    impl->pane = (IInspectable *)val;
    if (impl->pane) IInspectable_AddRef( impl->pane );
    if ((ch = uwp_elem_from_any( val )) != NULL) ch->parent = impl;
    g_layout_dirty = TRUE;
    uwp_invalidate_window();
    return S_OK;
}

const void *uwp_splitview_vtbl[32] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_sv_get_Content,        /* get_Content */
    [7] = uwp_sv_put_Content,        /* put_Content */
    [8] = uwp_sv_get_Pane,           /* get_Pane */
    [9] = uwp_sv_put_Pane,           /* put_Pane */
    [10] = uwp_sv_get_IsPaneOpen,    /* get_IsPaneOpen */
    [11] = uwp_sv_put_IsPaneOpen,    /* put_IsPaneOpen */
    [12] = uwp_sv_get_OpenPaneLength,/* get_OpenPaneLength */
    [13] = uwp_generic_noop,         /* put_OpenPaneLength */
    [14] = uwp_sv_get_CompactPaneLength, /* get_CompactPaneLength */
    [15] = uwp_generic_noop,         /* put_CompactPaneLength */
    [16] = uwp_sv_get_IntZero,       /* get_PanePlacement */
    [17] = uwp_generic_noop,         /* put_PanePlacement */
    [18] = uwp_sv_get_DisplayMode,   /* get_DisplayMode */
    [19] = uwp_generic_noop,         /* put_DisplayMode */
    [20] = uwp_cd_get_NullOut,       /* get_TemplateSettings */
    [21] = uwp_cd_get_NullOut,       /* get_PaneBackground */
    [22] = uwp_generic_noop,         /* put_PaneBackground */
    [23] = uwp_ui_event_add,         /* add_PaneClosing */
    [24] = uwp_generic_noop,         /* remove_PaneClosing */
    [25] = uwp_ui_event_add,         /* add_PaneClosed */
    [26] = uwp_generic_noop,         /* remove_PaneClosed */
    [27 ... 31] = uwp_generic_noop,
};

const void *uwp_splitview2_vtbl[16] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_ui_event_add,          /* ISplitView3::add_PaneOpening */
    [7] = uwp_generic_noop,          /* ISplitView3::remove_PaneOpening */
    [8] = uwp_ui_event_add,          /* ISplitView3::add_PaneOpened */
    [9] = uwp_generic_noop,          /* ISplitView3::remove_PaneOpened */
    [10 ... 15] = uwp_generic_noop,
};

const void *uwp_cd2_vtbl[24] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_cd2_get_CloseButtonText,
    [7] = uwp_cd2_put_CloseButtonText,
    [8] = uwp_cd_get_NullOut,
    [9] = uwp_generic_noop,
    [10] = uwp_cd_get_NullOut,
    [11] = uwp_generic_noop,
    [12] = uwp_cd_get_BoolTrue,
    [13] = uwp_generic_noop,
    [14] = uwp_ui_event_add,
    [15] = uwp_generic_noop,
    [16] = uwp_cd_get_NullOut,
    [17] = uwp_generic_noop,
    [18] = uwp_cd2_get_DefaultButton,
    [19] = uwp_cd2_put_DefaultButton,
    [20 ... 23] = uwp_generic_noop,
};

static HRESULT WINAPI uwp_tt_get_IsOpen( void *iface, boolean *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (out) *out = (impl && impl->is_open) ? TRUE : FALSE;
    return S_OK;
}
BOOL uwp_has_open_teachingtip( struct uwp_element *el )
{
    if (!el) return FALSE;
    if (uwp_is_teaching_tip( el ) && el->is_open) return TRUE;
    if (el->content && uwp_has_open_teachingtip( uwp_elem_from_any( el->content ) )) return TRUE;
    if (el->children_coll)
    {
        struct uwp_vec_view *children = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        unsigned int k;
        for (k = 0; k < children->count; k++)
            if (uwp_has_open_teachingtip( uwp_elem_from_any( children->items[k] ) )) return TRUE;
    }
    return FALSE;
}

static HRESULT WINAPI uwp_tt_put_IsOpen( void *iface, boolean val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    BOOL prev;
    if (!impl) return S_OK;
    prev = impl->is_open;
    impl->is_open = val ? TRUE : FALSE;
    g_layout_dirty = TRUE;
    TRACE( "ITeachingTip::put_IsOpen on %s (%s) -> %d (prev %d)\n",
           debugstr_w(impl->class_name), debugstr_w(impl->name), impl->is_open, prev );
    if (impl->is_open)
    {
        int ii;
        for (ii = 0; ii < g_island_hwnd_cnt; ii++)
        {
            HWND ih = g_island_hwnds[ii];
            struct uwp_element *isl = ih ? (struct uwp_element *)GetWindowLongPtrW( ih, GWLP_USERDATA ) : NULL;
            struct uwp_element *root_el = (isl && isl->content) ? uwp_elem_from_any( isl->content ) : NULL;
            if (root_el && (root_el == impl || uwp_elem_is_descendant( root_el, impl )))
                SetWindowPos( ih, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW );
        }
    }
    if (prev && !impl->is_open)
    {
        IInspectable *sender = impl->outer ? impl->outer : &impl->ITeachingTip_iface;
        struct uwp_sel_changed_args *args = uwp_sel_args_create( NULL, NULL );
        int i;
        if (args)
        {
            for (i = 0; i < impl->tip_closing_cnt; i++)
            {
                if (impl->tip_closing_handlers[i])
                {
                    struct xaml_event_handler *h = (struct xaml_event_handler *)impl->tip_closing_handlers[i];
                    h->lpVtbl->Invoke( h, sender, &args->IInspectable_iface );
                }
            }
            for (i = 0; i < impl->tip_closed_cnt; i++)
            {
                if (impl->tip_closed_handlers[i])
                {
                    struct xaml_event_handler *h = (struct xaml_event_handler *)impl->tip_closed_handlers[i];
                    h->lpVtbl->Invoke( h, sender, &args->IInspectable_iface );
                }
            }
            uwp_sel_args_Release( &args->IInspectable_iface );
        }
    }
    uwp_invalidate_window();
    return S_OK;
}
static HRESULT WINAPI uwp_tt_add_Closing( void *iface, void *handler, EventRegistrationToken *tok )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (tok) tok->value = 1;
    if (!impl || !handler) return S_OK;
    if (xaml_vec_grow( (void **)&impl->tip_closing_handlers, &impl->tip_closing_cap,
                       impl->tip_closing_cnt + 1, sizeof(*impl->tip_closing_handlers), 4 ))
    {
        impl->tip_closing_handlers[impl->tip_closing_cnt++] = (IInspectable *)handler;
        IUnknown_AddRef( (IUnknown *)handler );
        if (tok) tok->value = impl->tip_closing_cnt;
    }
    return S_OK;
}
static HRESULT WINAPI uwp_tt_add_Closed( void *iface, void *handler, EventRegistrationToken *tok )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (tok) tok->value = 1;
    if (!impl || !handler) return S_OK;
    if (xaml_vec_grow( (void **)&impl->tip_closed_handlers, &impl->tip_closed_cap,
                       impl->tip_closed_cnt + 1, sizeof(*impl->tip_closed_handlers), 4 ))
    {
        impl->tip_closed_handlers[impl->tip_closed_cnt++] = (IInspectable *)handler;
        IUnknown_AddRef( (IUnknown *)handler );
        if (tok) tok->value = impl->tip_closed_cnt;
    }
    return S_OK;
}
const void *uwp_teachingtip_vtbl[64] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6 ... 9] = uwp_generic_noop,
    [10] = uwp_tt_get_IsOpen,
    [11] = uwp_tt_put_IsOpen,
    [12 ... 50] = uwp_generic_noop,
    [51] = uwp_tt_add_Closing,
    [52] = uwp_generic_noop,
    [53] = uwp_tt_add_Closed,
    [54 ... 63] = uwp_generic_noop,
};
