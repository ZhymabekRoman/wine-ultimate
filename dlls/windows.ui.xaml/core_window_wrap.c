/* WinRT Windows.UI.Xaml ICoreWindow wrapper: multi-handler pointer/key event
 * registries over the shell core window, plus ICoreWindowInterop.
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

#include <stdlib.h>

#include "private.h"
#include "app_private.h"
#include "weakreference.h"
#include "wine/exception.h"
#include "windows.applicationmodel.activation.h"

WINE_DEFAULT_DEBUG_CHANNEL(xaml);

const GUID IID_ICoreWindowInterop =
    { 0x45d64a29, 0xa63e, 0x4cb6, { 0xb4, 0x98, 0x57, 0x81, 0xd2, 0x98, 0xcb, 0x4f } };

static const ICoreWindowVtbl xaml_cw_vtbl;

static inline struct xaml_core_window *impl_from_cw_ICoreWindow( ICoreWindow *iface )
{
    return CONTAINING_RECORD( iface, struct xaml_core_window, ICoreWindow_iface );
}
static inline struct xaml_core_window *impl_from_cw_Interop( ICoreWindowInterop *iface )
{
    return CONTAINING_RECORD( iface, struct xaml_core_window, ICoreWindowInterop_iface );
}

struct xaml_core_window *as_xaml_core_window( ICoreWindow *cw )
{
    if (!cw || cw->lpVtbl != &xaml_cw_vtbl) return NULL;
    return impl_from_cw_ICoreWindow( cw );
}

static HWND xaml_cw_resolve_hwnd( struct xaml_core_window *impl )
{
    ICoreWindowInterop *interop = NULL;
    HWND hw = NULL;
    if (!impl) return NULL;
    if (impl->hwnd && IsWindow( impl->hwnd )) return impl->hwnd;
    if (impl->inner && SUCCEEDED(ICoreWindow_QueryInterface( impl->inner, &IID_ICoreWindowInterop, (void **)&interop )) && interop)
    {
        interop->lpVtbl->GetWindowHandle( interop, &hw );
        interop->lpVtbl->Release( interop );
        if (hw && IsWindow( hw )) impl->hwnd = hw;
    }
    return hw;
}

static HRESULT WINAPI xaml_cw_QueryInterface( ICoreWindow *iface, REFIID iid, void **out )
{
    struct xaml_core_window *impl = impl_from_cw_ICoreWindow( iface );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_ICoreWindow ))
    {
        InterlockedIncrement( &impl->ref );
        *out = &impl->ICoreWindow_iface;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_ICoreWindowInterop ))
    {
        InterlockedIncrement( &impl->ref );
        *out = &impl->ICoreWindowInterop_iface;
        return S_OK;
    }
    if (impl->inner)
        return ICoreWindow_QueryInterface( impl->inner, iid, out );
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI xaml_cw_AddRef( ICoreWindow *iface )
{
    return InterlockedIncrement( &impl_from_cw_ICoreWindow( iface )->ref );
}
static ULONG WINAPI xaml_cw_Release( ICoreWindow *iface )
{
    struct xaml_core_window *impl = impl_from_cw_ICoreWindow( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        xaml_event_registry_clear( &impl->pointer_pressed );
        xaml_event_registry_clear( &impl->pointer_moved );
        xaml_event_registry_clear( &impl->pointer_released );
        xaml_event_registry_clear( &impl->pointer_wheel_changed );
        xaml_event_registry_clear( &impl->pointer_entered );
        xaml_event_registry_clear( &impl->pointer_exited );
        xaml_event_registry_clear( &impl->pointer_capture_lost );
        xaml_event_registry_clear( &impl->key_down );
        xaml_event_registry_clear( &impl->key_up );
        xaml_event_registry_clear( &impl->character_received );
        if (impl->inner) ICoreWindow_Release( impl->inner );
        free( impl );
    }
    return ref;
}
static HRESULT WINAPI xaml_cw_GetIids( ICoreWindow *iface, ULONG *count, IID **iids )
{
    struct xaml_core_window *impl = impl_from_cw_ICoreWindow( iface );
    if (impl->inner) return ICoreWindow_GetIids( impl->inner, count, iids );
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}
static HRESULT WINAPI xaml_cw_GetRuntimeClassName( ICoreWindow *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.UI.Core.CoreWindow", 26, name );
}
static HRESULT WINAPI xaml_cw_GetTrustLevel( ICoreWindow *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}
static HRESULT WINAPI xaml_cw_get_AutomationHostProvider( ICoreWindow *iface, IInspectable **value )
{
    struct xaml_core_window *impl = impl_from_cw_ICoreWindow( iface );
    return impl->inner ? ICoreWindow_get_AutomationHostProvider( impl->inner, value ) : E_NOTIMPL;
}
static HRESULT WINAPI xaml_cw_get_Bounds( ICoreWindow *iface, Rect *value )
{
    struct xaml_core_window *impl = impl_from_cw_ICoreWindow( iface );
    if (impl->inner) return ICoreWindow_get_Bounds( impl->inner, value );
    if (!value) return E_POINTER;
    value->X = 0; value->Y = 0; value->Width = 0; value->Height = 0;
    return S_OK;
}
static HRESULT WINAPI xaml_cw_get_CustomProperties( ICoreWindow *iface, __x_ABI_CWindows_CFoundation_CCollections_CIPropertySet **value )
{
    struct xaml_core_window *impl = impl_from_cw_ICoreWindow( iface );
    return impl->inner ? ICoreWindow_get_CustomProperties( impl->inner, value ) : E_NOTIMPL;
}
static HRESULT WINAPI xaml_cw_get_Dispatcher( ICoreWindow *iface, ICoreDispatcher **value )
{
    struct xaml_core_window *impl = impl_from_cw_ICoreWindow( iface );
    return impl->inner ? ICoreWindow_get_Dispatcher( impl->inner, value ) : E_NOTIMPL;
}
static HRESULT WINAPI xaml_cw_get_InputProperties( ICoreWindow *iface,
    __x_ABI_CWindows_CUI_CCore_CICoreWindowInputProperties **value )
{
    struct xaml_core_window *impl = impl_from_cw_ICoreWindow( iface );
    return impl->inner ? ICoreWindow_get_InputProperties( impl->inner, value ) : E_NOTIMPL;
}
static HRESULT WINAPI xaml_cw_get_FlowDirection( ICoreWindow *iface, CoreWindowFlowDirection *value )
{
    struct xaml_core_window *impl = impl_from_cw_ICoreWindow( iface );
    return impl->inner ? ICoreWindow_get_FlowDirection( impl->inner, value ) : E_NOTIMPL;
}
static HRESULT WINAPI xaml_cw_put_FlowDirection( ICoreWindow *iface, CoreWindowFlowDirection value )
{
    struct xaml_core_window *impl = impl_from_cw_ICoreWindow( iface );
    return impl->inner ? ICoreWindow_put_FlowDirection( impl->inner, value ) : E_NOTIMPL;
}
static HRESULT WINAPI xaml_cw_get_IsInputEnabled( ICoreWindow *iface, boolean *value )
{
    struct xaml_core_window *impl = impl_from_cw_ICoreWindow( iface );
    return impl->inner ? ICoreWindow_get_IsInputEnabled( impl->inner, value ) : E_NOTIMPL;
}
static HRESULT WINAPI xaml_cw_put_IsInputEnabled( ICoreWindow *iface, boolean value )
{
    struct xaml_core_window *impl = impl_from_cw_ICoreWindow( iface );
    return impl->inner ? ICoreWindow_put_IsInputEnabled( impl->inner, value ) : E_NOTIMPL;
}
static HRESULT WINAPI xaml_cw_get_PointerCursor( ICoreWindow *iface, ICoreCursor **value )
{
    struct xaml_core_window *impl = impl_from_cw_ICoreWindow( iface );
    return impl->inner ? ICoreWindow_get_PointerCursor( impl->inner, value ) : E_NOTIMPL;
}
static HRESULT WINAPI xaml_cw_put_PointerCursor( ICoreWindow *iface, ICoreCursor *value )
{
    struct xaml_core_window *impl = impl_from_cw_ICoreWindow( iface );
    return impl->inner ? ICoreWindow_put_PointerCursor( impl->inner, value ) : E_NOTIMPL;
}
static HRESULT WINAPI xaml_cw_get_PointerPosition( ICoreWindow *iface, Point *value )
{
    struct xaml_core_window *impl = impl_from_cw_ICoreWindow( iface );
    HWND hw;
    if (!value) return E_POINTER;
    if (impl->has_pos)
    {
        value->X = (float)impl->last_pos.x;
        value->Y = (float)impl->last_pos.y;
        return S_OK;
    }
    hw = xaml_cw_resolve_hwnd( impl );
    if (hw && IsWindow( hw ))
    {
        POINT pt;
        if (GetCursorPos( &pt ) && ScreenToClient( hw, &pt ))
        {
            value->X = (float)pt.x;
            value->Y = (float)pt.y;
            return S_OK;
        }
    }
    if (impl->inner) return ICoreWindow_get_PointerPosition( impl->inner, value );
    value->X = 0.0f; value->Y = 0.0f;
    return S_OK;
}
static HRESULT WINAPI xaml_cw_get_Visible( ICoreWindow *iface, boolean *value )
{
    struct xaml_core_window *impl = impl_from_cw_ICoreWindow( iface );
    return impl->inner ? ICoreWindow_get_Visible( impl->inner, value ) : E_NOTIMPL;
}
static HRESULT WINAPI xaml_cw_Activate( ICoreWindow *iface )
{
    struct xaml_core_window *impl = impl_from_cw_ICoreWindow( iface );
    return impl->inner ? ICoreWindow_Activate( impl->inner ) : S_OK;
}
static HRESULT WINAPI xaml_cw_Close( ICoreWindow *iface )
{
    struct xaml_core_window *impl = impl_from_cw_ICoreWindow( iface );
    return impl->inner ? ICoreWindow_Close( impl->inner ) : S_OK;
}
static HRESULT WINAPI xaml_cw_GetAsyncKeyState( ICoreWindow *iface, __x_ABI_CWindows_CSystem_CVirtualKey key, CoreVirtualKeyStates *state )
{
    struct xaml_core_window *impl = impl_from_cw_ICoreWindow( iface );
    return impl->inner ? ICoreWindow_GetAsyncKeyState( impl->inner, key, state ) : E_NOTIMPL;
}
static HRESULT WINAPI xaml_cw_GetKeyState( ICoreWindow *iface, __x_ABI_CWindows_CSystem_CVirtualKey key, CoreVirtualKeyStates *state )
{
    struct xaml_core_window *impl = impl_from_cw_ICoreWindow( iface );
    return impl->inner ? ICoreWindow_GetKeyState( impl->inner, key, state ) : E_NOTIMPL;
}
static HRESULT WINAPI xaml_cw_ReleasePointerCapture( ICoreWindow *iface )
{
    struct xaml_core_window *impl = impl_from_cw_ICoreWindow( iface );
    if (impl->has_capture)
    {
        ReleaseCapture();
        impl->has_capture = FALSE;
    }
    return S_OK;
}
static HRESULT WINAPI xaml_cw_SetPointerCapture( ICoreWindow *iface )
{
    struct xaml_core_window *impl = impl_from_cw_ICoreWindow( iface );
    HWND hw = xaml_cw_resolve_hwnd( impl );
    if (hw && IsWindow( hw ))
    {
        SetCapture( hw );
        impl->has_capture = TRUE;
    }
    return S_OK;
}
static HRESULT WINAPI xaml_cw_add_Activated( ICoreWindow *iface,
    __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CWindowActivatedEventArgs *h, EventRegistrationToken *c )
{
    struct xaml_core_window *impl = impl_from_cw_ICoreWindow( iface );
    return impl->inner ? ICoreWindow_add_Activated( impl->inner, h, c ) : E_NOTIMPL;
}
static HRESULT WINAPI xaml_cw_remove_Activated( ICoreWindow *iface, EventRegistrationToken c )
{
    struct xaml_core_window *impl = impl_from_cw_ICoreWindow( iface );
    return impl->inner ? ICoreWindow_remove_Activated( impl->inner, c ) : E_NOTIMPL;
}
static HRESULT WINAPI xaml_cw_add_AutomationProviderRequested( ICoreWindow *iface,
    __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CAutomationProviderRequestedEventArgs *h, EventRegistrationToken *c )
{
    struct xaml_core_window *impl = impl_from_cw_ICoreWindow( iface );
    return impl->inner ? ICoreWindow_add_AutomationProviderRequested( impl->inner, h, c ) : E_NOTIMPL;
}
static HRESULT WINAPI xaml_cw_remove_AutomationProviderRequested( ICoreWindow *iface, EventRegistrationToken c )
{
    struct xaml_core_window *impl = impl_from_cw_ICoreWindow( iface );
    return impl->inner ? ICoreWindow_remove_AutomationProviderRequested( impl->inner, c ) : E_NOTIMPL;
}
static void xaml_event_registry_set_inner_token( struct xaml_event_registry *reg,
    EventRegistrationToken *inner_tokens, EventRegistrationToken wrapper_token, EventRegistrationToken inner_token )
{
    unsigned int i;

    if (!reg || !inner_tokens || !wrapper_token.value) return;
    EnterCriticalSection( &xaml_event_cs );
    for (i = 0; i < ARRAY_SIZE( reg->entries ); i++)
    {
        if (reg->entries[i].handler && reg->entries[i].token == wrapper_token.value)
        {
            inner_tokens[i] = inner_token;
            break;
        }
    }
    LeaveCriticalSection( &xaml_event_cs );
}

static HRESULT xaml_event_registry_remove_with_inner( struct xaml_event_registry *reg,
    EventRegistrationToken *inner_tokens, EventRegistrationToken token,
    EventRegistrationToken *out_inner_token, BOOL *out_found )
{
    unsigned int i;

    if (out_inner_token) out_inner_token->value = 0;
    if (out_found) *out_found = FALSE;
    if (!reg) return E_INVALIDARG;
    EnterCriticalSection( &xaml_event_cs );
    for (i = 0; i < ARRAY_SIZE( reg->entries ); i++)
    {
        if (reg->entries[i].handler && reg->entries[i].token == token.value)
        {
            void *h = reg->entries[i].handler;
            reg->entries[i].handler = NULL;
            reg->entries[i].token = 0;
            if (inner_tokens)
            {
                if (out_inner_token) *out_inner_token = inner_tokens[i];
                inner_tokens[i].value = 0;
            }
            if (out_found) *out_found = TRUE;
            LeaveCriticalSection( &xaml_event_cs );
            IUnknown_Release( (IUnknown *)h );
            return S_OK;
        }
    }
    LeaveCriticalSection( &xaml_event_cs );
    return S_OK;
}

static HRESULT WINAPI xaml_cw_add_CharacterReceived( ICoreWindow *iface,
    __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CCharacterReceivedEventArgs *h, EventRegistrationToken *c )
{
    struct xaml_core_window *impl = impl_from_cw_ICoreWindow( iface );
    EventRegistrationToken tok = { 0 }, inner_tok = { 0 };
    HRESULT hr = xaml_event_registry_add( &impl->character_received, h, &tok );
    if (c) *c = tok;
    if (SUCCEEDED( hr ) && impl->inner && impl->inner->lpVtbl->add_CharacterReceived)
    {
        if (SUCCEEDED( impl->inner->lpVtbl->add_CharacterReceived( impl->inner, h, &inner_tok ) ))
            xaml_event_registry_set_inner_token( &impl->character_received, impl->character_received_inner_tokens, tok, inner_tok );
    }
    return hr;
}
static HRESULT WINAPI xaml_cw_remove_CharacterReceived( ICoreWindow *iface, EventRegistrationToken c )
{
    struct xaml_core_window *impl = impl_from_cw_ICoreWindow( iface );
    EventRegistrationToken inner_tok = { 0 };
    BOOL found = FALSE;
    HRESULT hr = xaml_event_registry_remove_with_inner( &impl->character_received, impl->character_received_inner_tokens, c, &inner_tok, &found );
    if (found && impl->inner && impl->inner->lpVtbl->remove_CharacterReceived)
        impl->inner->lpVtbl->remove_CharacterReceived( impl->inner, inner_tok );
    return hr;
}
static HRESULT WINAPI xaml_cw_add_Closed( ICoreWindow *iface,
    __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CCoreWindowEventArgs *h, EventRegistrationToken *c )
{
    struct xaml_core_window *impl = impl_from_cw_ICoreWindow( iface );
    return impl->inner ? ICoreWindow_add_Closed( impl->inner, h, c ) : E_NOTIMPL;
}
static HRESULT WINAPI xaml_cw_remove_Closed( ICoreWindow *iface, EventRegistrationToken c )
{
    struct xaml_core_window *impl = impl_from_cw_ICoreWindow( iface );
    return impl->inner ? ICoreWindow_remove_Closed( impl->inner, c ) : E_NOTIMPL;
}
static HRESULT WINAPI xaml_cw_add_InputEnabled( ICoreWindow *iface,
    __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CInputEnabledEventArgs *h, EventRegistrationToken *c )
{
    struct xaml_core_window *impl = impl_from_cw_ICoreWindow( iface );
    return impl->inner ? ICoreWindow_add_InputEnabled( impl->inner, h, c ) : E_NOTIMPL;
}
static HRESULT WINAPI xaml_cw_remove_InputEnabled( ICoreWindow *iface, EventRegistrationToken c )
{
    struct xaml_core_window *impl = impl_from_cw_ICoreWindow( iface );
    return impl->inner ? ICoreWindow_remove_InputEnabled( impl->inner, c ) : E_NOTIMPL;
}
static HRESULT WINAPI xaml_cw_add_KeyDown( ICoreWindow *iface,
    __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CKeyEventArgs *h, EventRegistrationToken *c )
{
    struct xaml_core_window *impl = impl_from_cw_ICoreWindow( iface );
    EventRegistrationToken tok = { 0 }, inner_tok = { 0 };
    HRESULT hr = xaml_event_registry_add( &impl->key_down, h, &tok );
    if (c) *c = tok;
    if (SUCCEEDED( hr ) && impl->inner && impl->inner->lpVtbl->add_KeyDown)
    {
        if (SUCCEEDED( impl->inner->lpVtbl->add_KeyDown( impl->inner, h, &inner_tok ) ))
            xaml_event_registry_set_inner_token( &impl->key_down, impl->key_down_inner_tokens, tok, inner_tok );
    }
    return hr;
}
static HRESULT WINAPI xaml_cw_remove_KeyDown( ICoreWindow *iface, EventRegistrationToken c )
{
    struct xaml_core_window *impl = impl_from_cw_ICoreWindow( iface );
    EventRegistrationToken inner_tok = { 0 };
    BOOL found = FALSE;
    HRESULT hr = xaml_event_registry_remove_with_inner( &impl->key_down, impl->key_down_inner_tokens, c, &inner_tok, &found );
    if (found && impl->inner && impl->inner->lpVtbl->remove_KeyDown)
        impl->inner->lpVtbl->remove_KeyDown( impl->inner, inner_tok );
    return hr;
}
static HRESULT WINAPI xaml_cw_add_KeyUp( ICoreWindow *iface,
    __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CKeyEventArgs *h, EventRegistrationToken *c )
{
    struct xaml_core_window *impl = impl_from_cw_ICoreWindow( iface );
    EventRegistrationToken tok = { 0 }, inner_tok = { 0 };
    HRESULT hr = xaml_event_registry_add( &impl->key_up, h, &tok );
    if (c) *c = tok;
    if (SUCCEEDED( hr ) && impl->inner && impl->inner->lpVtbl->add_KeyUp)
    {
        if (SUCCEEDED( impl->inner->lpVtbl->add_KeyUp( impl->inner, h, &inner_tok ) ))
            xaml_event_registry_set_inner_token( &impl->key_up, impl->key_up_inner_tokens, tok, inner_tok );
    }
    return hr;
}
static HRESULT WINAPI xaml_cw_remove_KeyUp( ICoreWindow *iface, EventRegistrationToken c )
{
    struct xaml_core_window *impl = impl_from_cw_ICoreWindow( iface );
    EventRegistrationToken inner_tok = { 0 };
    BOOL found = FALSE;
    HRESULT hr = xaml_event_registry_remove_with_inner( &impl->key_up, impl->key_up_inner_tokens, c, &inner_tok, &found );
    if (found && impl->inner && impl->inner->lpVtbl->remove_KeyUp)
        impl->inner->lpVtbl->remove_KeyUp( impl->inner, inner_tok );
    return hr;
}
static HRESULT WINAPI xaml_cw_add_PointerCaptureLost( ICoreWindow *iface,
    __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CPointerEventArgs *h, EventRegistrationToken *c )
{
    return xaml_event_registry_add( &impl_from_cw_ICoreWindow( iface )->pointer_capture_lost, h, c );
}
static HRESULT WINAPI xaml_cw_remove_PointerCaptureLost( ICoreWindow *iface, EventRegistrationToken c )
{
    return xaml_event_registry_remove( &impl_from_cw_ICoreWindow( iface )->pointer_capture_lost, c );
}
static HRESULT WINAPI xaml_cw_add_PointerEntered( ICoreWindow *iface,
    __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CPointerEventArgs *h, EventRegistrationToken *c )
{
    return xaml_event_registry_add( &impl_from_cw_ICoreWindow( iface )->pointer_entered, h, c );
}
static HRESULT WINAPI xaml_cw_remove_PointerEntered( ICoreWindow *iface, EventRegistrationToken c )
{
    return xaml_event_registry_remove( &impl_from_cw_ICoreWindow( iface )->pointer_entered, c );
}
static HRESULT WINAPI xaml_cw_add_PointerExited( ICoreWindow *iface,
    __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CPointerEventArgs *h, EventRegistrationToken *c )
{
    return xaml_event_registry_add( &impl_from_cw_ICoreWindow( iface )->pointer_exited, h, c );
}
static HRESULT WINAPI xaml_cw_remove_PointerExited( ICoreWindow *iface, EventRegistrationToken c )
{
    return xaml_event_registry_remove( &impl_from_cw_ICoreWindow( iface )->pointer_exited, c );
}
static HRESULT WINAPI xaml_cw_add_PointerMoved( ICoreWindow *iface,
    __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CPointerEventArgs *h, EventRegistrationToken *c )
{
    return xaml_event_registry_add( &impl_from_cw_ICoreWindow( iface )->pointer_moved, h, c );
}
static HRESULT WINAPI xaml_cw_remove_PointerMoved( ICoreWindow *iface, EventRegistrationToken c )
{
    return xaml_event_registry_remove( &impl_from_cw_ICoreWindow( iface )->pointer_moved, c );
}
static HRESULT WINAPI xaml_cw_add_PointerPressed( ICoreWindow *iface,
    __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CPointerEventArgs *h, EventRegistrationToken *c )
{
    return xaml_event_registry_add( &impl_from_cw_ICoreWindow( iface )->pointer_pressed, h, c );
}
static HRESULT WINAPI xaml_cw_remove_PointerPressed( ICoreWindow *iface, EventRegistrationToken c )
{
    return xaml_event_registry_remove( &impl_from_cw_ICoreWindow( iface )->pointer_pressed, c );
}
static HRESULT WINAPI xaml_cw_add_PointerReleased( ICoreWindow *iface,
    __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CPointerEventArgs *h, EventRegistrationToken *c )
{
    return xaml_event_registry_add( &impl_from_cw_ICoreWindow( iface )->pointer_released, h, c );
}
static HRESULT WINAPI xaml_cw_remove_PointerReleased( ICoreWindow *iface, EventRegistrationToken c )
{
    return xaml_event_registry_remove( &impl_from_cw_ICoreWindow( iface )->pointer_released, c );
}
static HRESULT WINAPI xaml_cw_add_TouchHitTesting( ICoreWindow *iface,
    __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CTouchHitTestingEventArgs *h, EventRegistrationToken *c )
{
    struct xaml_core_window *impl = impl_from_cw_ICoreWindow( iface );
    return impl->inner ? ICoreWindow_add_TouchHitTesting( impl->inner, h, c ) : E_NOTIMPL;
}
static HRESULT WINAPI xaml_cw_remove_TouchHitTesting( ICoreWindow *iface, EventRegistrationToken c )
{
    struct xaml_core_window *impl = impl_from_cw_ICoreWindow( iface );
    return impl->inner ? ICoreWindow_remove_TouchHitTesting( impl->inner, c ) : E_NOTIMPL;
}
static HRESULT WINAPI xaml_cw_add_PointerWheelChanged( ICoreWindow *iface,
    __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CPointerEventArgs *h, EventRegistrationToken *c )
{
    return xaml_event_registry_add( &impl_from_cw_ICoreWindow( iface )->pointer_wheel_changed, h, c );
}
static HRESULT WINAPI xaml_cw_remove_PointerWheelChanged( ICoreWindow *iface, EventRegistrationToken c )
{
    return xaml_event_registry_remove( &impl_from_cw_ICoreWindow( iface )->pointer_wheel_changed, c );
}
static HRESULT WINAPI xaml_cw_add_SizeChanged( ICoreWindow *iface,
    __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CWindowSizeChangedEventArgs *h, EventRegistrationToken *c )
{
    struct xaml_core_window *impl = impl_from_cw_ICoreWindow( iface );
    return impl->inner ? ICoreWindow_add_SizeChanged( impl->inner, h, c ) : E_NOTIMPL;
}
static HRESULT WINAPI xaml_cw_remove_SizeChanged( ICoreWindow *iface, EventRegistrationToken c )
{
    struct xaml_core_window *impl = impl_from_cw_ICoreWindow( iface );
    return impl->inner ? ICoreWindow_remove_SizeChanged( impl->inner, c ) : E_NOTIMPL;
}
static HRESULT WINAPI xaml_cw_add_VisibilityChanged( ICoreWindow *iface,
    __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CVisibilityChangedEventArgs *h, EventRegistrationToken *c )
{
    struct xaml_core_window *impl = impl_from_cw_ICoreWindow( iface );
    return impl->inner ? ICoreWindow_add_VisibilityChanged( impl->inner, h, c ) : E_NOTIMPL;
}
static HRESULT WINAPI xaml_cw_remove_VisibilityChanged( ICoreWindow *iface, EventRegistrationToken c )
{
    struct xaml_core_window *impl = impl_from_cw_ICoreWindow( iface );
    return impl->inner ? ICoreWindow_remove_VisibilityChanged( impl->inner, c ) : E_NOTIMPL;
}

static const ICoreWindowVtbl xaml_cw_vtbl =
{
    xaml_cw_QueryInterface,
    xaml_cw_AddRef,
    xaml_cw_Release,
    xaml_cw_GetIids,
    xaml_cw_GetRuntimeClassName,
    xaml_cw_GetTrustLevel,
    xaml_cw_get_AutomationHostProvider,
    xaml_cw_get_Bounds,
    xaml_cw_get_CustomProperties,
    xaml_cw_get_Dispatcher,
    xaml_cw_get_InputProperties,    xaml_cw_get_PointerCursor,
    xaml_cw_put_PointerCursor,
    xaml_cw_get_PointerPosition,
    xaml_cw_get_Visible,
    xaml_cw_Activate,
    xaml_cw_Close,
    xaml_cw_GetAsyncKeyState,
    xaml_cw_GetKeyState,
    xaml_cw_ReleasePointerCapture,
    xaml_cw_SetPointerCapture,
    xaml_cw_add_Activated,
    xaml_cw_remove_Activated,
    xaml_cw_add_AutomationProviderRequested,
    xaml_cw_remove_AutomationProviderRequested,
    xaml_cw_add_CharacterReceived,
    xaml_cw_remove_CharacterReceived,
    xaml_cw_add_Closed,
    xaml_cw_remove_Closed,
    xaml_cw_add_InputEnabled,
    xaml_cw_remove_InputEnabled,
    xaml_cw_add_KeyDown,
    xaml_cw_remove_KeyDown,
    xaml_cw_add_KeyUp,
    xaml_cw_remove_KeyUp,
    xaml_cw_add_PointerCaptureLost,
    xaml_cw_remove_PointerCaptureLost,
    xaml_cw_add_PointerEntered,
    xaml_cw_remove_PointerEntered,
    xaml_cw_add_PointerExited,
    xaml_cw_remove_PointerExited,
    xaml_cw_add_PointerMoved,
    xaml_cw_remove_PointerMoved,
    xaml_cw_add_PointerPressed,
    xaml_cw_remove_PointerPressed,
    xaml_cw_add_PointerReleased,
    xaml_cw_remove_PointerReleased,    xaml_cw_add_PointerWheelChanged,
    xaml_cw_remove_PointerWheelChanged,
    xaml_cw_add_SizeChanged,
    xaml_cw_remove_SizeChanged,
    xaml_cw_add_VisibilityChanged,
    xaml_cw_remove_VisibilityChanged,
    xaml_cw_add_TouchHitTesting,
    xaml_cw_remove_TouchHitTesting,
    xaml_cw_get_FlowDirection,
    xaml_cw_put_FlowDirection,
    xaml_cw_get_IsInputEnabled,
    xaml_cw_put_IsInputEnabled,
};

static HRESULT WINAPI xaml_cw_interop_QueryInterface( ICoreWindowInterop *iface, REFIID iid, void **out )
{
    return xaml_cw_QueryInterface( &impl_from_cw_Interop( iface )->ICoreWindow_iface, iid, out );
}
static ULONG WINAPI xaml_cw_interop_AddRef( ICoreWindowInterop *iface )
{
    return xaml_cw_AddRef( &impl_from_cw_Interop( iface )->ICoreWindow_iface );
}
static ULONG WINAPI xaml_cw_interop_Release( ICoreWindowInterop *iface )
{
    return xaml_cw_Release( &impl_from_cw_Interop( iface )->ICoreWindow_iface );
}
static HRESULT WINAPI xaml_cw_interop_GetWindowHandle( ICoreWindowInterop *iface, HWND *hwnd )
{
    struct xaml_core_window *impl = impl_from_cw_Interop( iface );
    ICoreWindowInterop *interop = NULL;
    HRESULT hr = E_NOINTERFACE;
    if (!hwnd) return E_POINTER;
    if (impl->hwnd && IsWindow( impl->hwnd ))
    {
        *hwnd = impl->hwnd;
        return S_OK;
    }
    if (impl->inner && SUCCEEDED( hr = ICoreWindow_QueryInterface( impl->inner, &IID_ICoreWindowInterop, (void **)&interop )) && interop)
    {
        hr = interop->lpVtbl->GetWindowHandle( interop, hwnd );
        interop->lpVtbl->Release( interop );
        if (SUCCEEDED(hr) && *hwnd && IsWindow( *hwnd ))
            impl->hwnd = *hwnd;
        return hr;
    }
    *hwnd = NULL;
    return hr;
}
static HRESULT WINAPI xaml_cw_interop_SetMessageHandled( ICoreWindowInterop *iface, boolean value )
{
    struct xaml_core_window *impl = impl_from_cw_Interop( iface );
    ICoreWindowInterop *interop = NULL;
    HRESULT hr = S_OK;
    if (impl->inner && SUCCEEDED( ICoreWindow_QueryInterface( impl->inner, &IID_ICoreWindowInterop, (void **)&interop )) && interop)
    {
        hr = interop->lpVtbl->SetMessageHandled( interop, value );
        interop->lpVtbl->Release( interop );
    }
    return hr;
}

static const ICoreWindowInteropVtbl xaml_cw_interop_vtbl =
{
    xaml_cw_interop_QueryInterface,
    xaml_cw_interop_AddRef,
    xaml_cw_interop_Release,
    xaml_cw_interop_GetWindowHandle,
    xaml_cw_interop_SetMessageHandled,
};

ICoreWindow *wrap_core_window( ICoreWindow *inner )
{
    struct xaml_core_window *wrap;
    TRACE( "wrapping inner core window %p\n", inner );
    if (!inner) return NULL;
    if (inner->lpVtbl == &xaml_cw_vtbl)
    {
        ICoreWindow_AddRef( inner );
        return inner;
    }
    if (!(wrap = calloc( 1, sizeof(*wrap) ))) return inner;
    wrap->ICoreWindow_iface.lpVtbl = &xaml_cw_vtbl;
    wrap->ICoreWindowInterop_iface.lpVtbl = &xaml_cw_interop_vtbl;
    wrap->ref = 1;
    wrap->inner = inner;
    ICoreWindow_AddRef( inner );
    xaml_cw_resolve_hwnd( wrap );
    return &wrap->ICoreWindow_iface;
}


