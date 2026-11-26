/* WinRT Windows.UI.Xaml input: shared WinRT event registry, IPointerDevice,
 * IPointerPoint(+Properties), Pointer/Key/CharacterReceived event args.
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

CRITICAL_SECTION xaml_event_cs;
static CRITICAL_SECTION_DEBUG xaml_event_cs_debug =
{
    0, 0, &xaml_event_cs,
    { &xaml_event_cs_debug.ProcessLocksList, &xaml_event_cs_debug.ProcessLocksList },
    0, 0, { (DWORD_PTR)(__FILE__ ": xaml_event_cs") }
};
CRITICAL_SECTION xaml_event_cs = { &xaml_event_cs_debug, -1, 0, 0, 0, 0 };

HRESULT xaml_event_registry_add( struct xaml_event_registry *reg, void *handler, EventRegistrationToken *token )
{
    unsigned int i, slot;

    if (!reg) return E_INVALIDARG;
    if (!handler)
    {
        if (token) token->value = 0;
        return S_OK;
    }

    EnterCriticalSection( &xaml_event_cs );
    slot = reg->cap;
    for (i = 0; i < reg->cap; i++)
    {
        if (!reg->entries[i].handler)
        {
            slot = i;
            break;
        }
    }
    if (slot == reg->cap)
    {
        unsigned int new_cap = reg->cap ? (unsigned int)reg->cap * 2 : 4;
        struct xaml_event_entry *new_entries;
        if (new_cap > 32768)
        {
            LeaveCriticalSection( &xaml_event_cs );
            return E_OUTOFMEMORY;
        }
        new_entries = realloc( reg->entries, new_cap * sizeof(*new_entries) );
        if (!new_entries)
        {
            LeaveCriticalSection( &xaml_event_cs );
            return E_OUTOFMEMORY;
        }
        memset( new_entries + reg->cap, 0, (new_cap - reg->cap) * sizeof(*new_entries) );
        reg->entries = new_entries;
        reg->cap = (UINT16)new_cap;
    }
    {
        INT64 tok = InterlockedIncrement64( &reg->next_token );
        IUnknown_AddRef( (IUnknown *)handler );
        reg->entries[slot].handler = handler;
        reg->entries[slot].token = tok;
        __atomic_store_n( &reg->count, (UINT16)(reg->count + 1), __ATOMIC_RELEASE );
        if (token) token->value = tok;
    }
    LeaveCriticalSection( &xaml_event_cs );
    return S_OK;
}

HRESULT xaml_event_registry_remove( struct xaml_event_registry *reg, EventRegistrationToken token )
{
    unsigned int i;

    if (!reg) return E_INVALIDARG;
    if (__atomic_load_n( &reg->count, __ATOMIC_ACQUIRE ) == 0) return S_OK;
    EnterCriticalSection( &xaml_event_cs );
    for (i = 0; i < reg->cap; i++)
    {
        if (reg->entries[i].handler && reg->entries[i].token == token.value)
        {
            void *h = reg->entries[i].handler;
            reg->entries[i].handler = NULL;
            reg->entries[i].token = 0;
            if (reg->count > 0)
                __atomic_store_n( &reg->count, (UINT16)(reg->count - 1), __ATOMIC_RELEASE );
            LeaveCriticalSection( &xaml_event_cs );
            IUnknown_Release( (IUnknown *)h );
            return S_OK;
        }
    }
    LeaveCriticalSection( &xaml_event_cs );
    return S_OK;
}

unsigned int xaml_event_registry_snapshot( struct xaml_event_registry *reg, void **out, unsigned int max_out )
{
    unsigned int i, count = 0;

    if (!reg || !out || !max_out) return 0;
    if (__atomic_load_n( &reg->count, __ATOMIC_ACQUIRE ) == 0) return 0;
    EnterCriticalSection( &xaml_event_cs );
    for (i = 0; i < reg->cap && count < max_out; i++)
    {
        if (reg->entries[i].handler)
        {
            IUnknown_AddRef( (IUnknown *)reg->entries[i].handler );
            out[count++] = reg->entries[i].handler;
        }
    }
    LeaveCriticalSection( &xaml_event_cs );
    return count;
}

unsigned int xaml_event_registry_snapshot_alloc( struct xaml_event_registry *reg, void ***out_handlers )
{
    unsigned int i, count, idx = 0;
    void **buf;

    if (out_handlers) *out_handlers = NULL;
    if (!reg || !out_handlers) return 0;
    if (__atomic_load_n( &reg->count, __ATOMIC_ACQUIRE ) == 0) return 0;
    EnterCriticalSection( &xaml_event_cs );
    count = reg->count;
    if (!count || !reg->entries)
    {
        LeaveCriticalSection( &xaml_event_cs );
        return 0;
    }
    buf = malloc( count * sizeof(*buf) );
    if (!buf)
    {
        LeaveCriticalSection( &xaml_event_cs );
        return 0;
    }
    for (i = 0; i < reg->cap && idx < count; i++)
    {
        if (reg->entries[i].handler)
        {
            IUnknown_AddRef( (IUnknown *)reg->entries[i].handler );
            buf[idx++] = reg->entries[i].handler;
        }
    }
    LeaveCriticalSection( &xaml_event_cs );
    *out_handlers = buf;
    return idx;
}

void xaml_event_registry_clear( struct xaml_event_registry *reg )
{
    struct xaml_event_entry *entries;
    unsigned int i, cap;

    if (!reg) return;
    if (__atomic_load_n( &reg->count, __ATOMIC_ACQUIRE ) == 0 &&
        !__atomic_load_n( &reg->entries, __ATOMIC_ACQUIRE ))
        return;
    EnterCriticalSection( &xaml_event_cs );
    entries = reg->entries;
    cap = reg->cap;
    reg->entries = NULL;
    reg->cap = 0;
    __atomic_store_n( &reg->count, 0, __ATOMIC_RELEASE );
    LeaveCriticalSection( &xaml_event_cs );
    if (entries)
    {
        for (i = 0; i < cap; i++)
            if (entries[i].handler) IUnknown_Release( (IUnknown *)entries[i].handler );
        free( entries );
    }
}

/* ---- Mouse PointerDevice + PointerPoint + PointerPointProperties + Core PointerEventArgs ---- */
static HRESULT WINAPI mouse_dev_QueryInterface( IPointerDevice *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID___x_ABI_CWindows_CDevices_CInput_CIPointerDevice ))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI mouse_dev_AddRef( IPointerDevice *iface ) { return 2; }
static ULONG WINAPI mouse_dev_Release( IPointerDevice *iface ) { return 1; }
static HRESULT WINAPI mouse_dev_GetIids( IPointerDevice *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}
static HRESULT WINAPI mouse_dev_GetRuntimeClassName( IPointerDevice *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.Devices.Input.PointerDevice", 35, name );
}
static HRESULT WINAPI mouse_dev_GetTrustLevel( IPointerDevice *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}
static HRESULT WINAPI mouse_dev_get_PointerDeviceType( IPointerDevice *iface, PointerDeviceType *value )
{
    if (!value) return E_POINTER;
    *value = PointerDeviceType_Mouse;
    return S_OK;
}
static HRESULT WINAPI mouse_dev_get_IsIntegrated( IPointerDevice *iface, boolean *value )
{
    if (!value) return E_POINTER;
    *value = FALSE;
    return S_OK;
}
static HRESULT WINAPI mouse_dev_get_MaxContacts( IPointerDevice *iface, UINT32 *value )
{
    if (!value) return E_POINTER;
    *value = 1;
    return S_OK;
}
static HRESULT WINAPI mouse_dev_get_PhysicalDeviceRect( IPointerDevice *iface, Rect *value )
{
    if (!value) return E_POINTER;
    value->X = 0.0f; value->Y = 0.0f;
    value->Width = (float)GetSystemMetrics( SM_CXSCREEN );
    value->Height = (float)GetSystemMetrics( SM_CYSCREEN );
    return S_OK;
}
static HRESULT WINAPI mouse_dev_get_ScreenRect( IPointerDevice *iface, Rect *value )
{
    return mouse_dev_get_PhysicalDeviceRect( iface, value );
}
static HRESULT WINAPI mouse_dev_get_SupportedUsages( IPointerDevice *iface, __FIVectorView_1_PointerDeviceUsage **value )
{
    if (!value) return E_POINTER;
    *value = NULL;
    return S_OK;
}

static const IPointerDeviceVtbl mouse_dev_vtbl =
{
    mouse_dev_QueryInterface,
    mouse_dev_AddRef,
    mouse_dev_Release,
    mouse_dev_GetIids,
    mouse_dev_GetRuntimeClassName,
    mouse_dev_GetTrustLevel,
    mouse_dev_get_PointerDeviceType,
    mouse_dev_get_IsIntegrated,
    mouse_dev_get_MaxContacts,
    mouse_dev_get_PhysicalDeviceRect,
    mouse_dev_get_ScreenRect,
    mouse_dev_get_SupportedUsages,
};
static IPointerDevice mouse_pointer_device = { &mouse_dev_vtbl };

struct xaml_pointer_point
{
    IPointerPoint IPointerPoint_iface;
    IPointerPointProperties IPointerPointProperties_iface;
    LONG ref;
    float x;
    float y;
    UINT32 pointer_id;
    UINT32 frame_id;
    UINT64 timestamp;
    BOOL is_in_contact;
    BOOL is_in_window;
    BOOL is_left;
    BOOL is_right;
    BOOL is_middle;
    INT32 wheel_delta;
    PointerUpdateKind update_kind;
};

static LONG xaml_pointer_frame_seq;

static inline struct xaml_pointer_point *impl_from_IPointerPoint( IPointerPoint *iface )
{
    return CONTAINING_RECORD( iface, struct xaml_pointer_point, IPointerPoint_iface );
}
static inline struct xaml_pointer_point *impl_from_IPointerPointProperties( IPointerPointProperties *iface )
{
    return CONTAINING_RECORD( iface, struct xaml_pointer_point, IPointerPointProperties_iface );
}

static HRESULT WINAPI ptr_point_QueryInterface( IPointerPoint *iface, REFIID iid, void **out )
{
    struct xaml_pointer_point *impl = impl_from_IPointerPoint( iface );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID___x_ABI_CWindows_CUI_CInput_CIPointerPoint ))
    {
        InterlockedIncrement( &impl->ref );
        *out = &impl->IPointerPoint_iface;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID___x_ABI_CWindows_CUI_CInput_CIPointerPointProperties ))
    {
        InterlockedIncrement( &impl->ref );
        *out = &impl->IPointerPointProperties_iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI ptr_point_AddRef( IPointerPoint *iface )
{
    return InterlockedIncrement( &impl_from_IPointerPoint( iface )->ref );
}
static ULONG WINAPI ptr_point_Release( IPointerPoint *iface )
{
    struct xaml_pointer_point *impl = impl_from_IPointerPoint( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref) free( impl );
    return ref;
}
static HRESULT WINAPI ptr_point_GetIids( IPointerPoint *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}
static HRESULT WINAPI ptr_point_GetRuntimeClassName( IPointerPoint *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.UI.Input.PointerPoint", 29, name );
}
static HRESULT WINAPI ptr_point_GetTrustLevel( IPointerPoint *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}
static HRESULT WINAPI ptr_point_get_PointerDevice( IPointerPoint *iface, IPointerDevice **value )
{
    if (!value) return E_POINTER;
    *value = &mouse_pointer_device;
    return S_OK;
}
static HRESULT WINAPI ptr_point_get_Position( IPointerPoint *iface, Point *value )
{
    struct xaml_pointer_point *impl = impl_from_IPointerPoint( iface );
    if (!value) return E_POINTER;
    value->X = impl->x;
    value->Y = impl->y;
    return S_OK;
}
static HRESULT WINAPI ptr_point_get_RawPosition( IPointerPoint *iface, Point *value )
{
    struct xaml_pointer_point *impl = impl_from_IPointerPoint( iface );
    if (!value) return E_POINTER;
    value->X = impl->x;
    value->Y = impl->y;
    return S_OK;
}
static HRESULT WINAPI ptr_point_get_pointer_id( IPointerPoint *iface, UINT32 *value )
{
    struct xaml_pointer_point *impl = impl_from_IPointerPoint( iface );
    if (!value) return E_POINTER;
    *value = impl->pointer_id;
    return S_OK;
}
static HRESULT WINAPI ptr_point_get_FrameId( IPointerPoint *iface, UINT32 *value )
{
    struct xaml_pointer_point *impl = impl_from_IPointerPoint( iface );
    if (!value) return E_POINTER;
    *value = impl->frame_id;
    return S_OK;
}
static HRESULT WINAPI ptr_point_get_Timestamp( IPointerPoint *iface, UINT64 *value )
{
    struct xaml_pointer_point *impl = impl_from_IPointerPoint( iface );
    if (!value) return E_POINTER;
    *value = impl->timestamp;
    return S_OK;
}
static HRESULT WINAPI ptr_point_get_IsInContact( IPointerPoint *iface, boolean *value )
{
    struct xaml_pointer_point *impl = impl_from_IPointerPoint( iface );
    if (!value) return E_POINTER;
    *value = impl->is_in_contact ? TRUE : FALSE;
    return S_OK;
}
static HRESULT WINAPI ptr_point_get_Properties( IPointerPoint *iface, IPointerPointProperties **value )
{
    struct xaml_pointer_point *impl = impl_from_IPointerPoint( iface );
    if (!value) return E_POINTER;
    InterlockedIncrement( &impl->ref );
    *value = &impl->IPointerPointProperties_iface;
    return S_OK;
}

static const IPointerPointVtbl ptr_point_vtbl =
{
    ptr_point_QueryInterface,
    ptr_point_AddRef,
    ptr_point_Release,
    ptr_point_GetIids,
    ptr_point_GetRuntimeClassName,
    ptr_point_GetTrustLevel,
    ptr_point_get_PointerDevice,
    ptr_point_get_Position,
    ptr_point_get_RawPosition,
    ptr_point_get_pointer_id,
    ptr_point_get_FrameId,
    ptr_point_get_Timestamp,
    ptr_point_get_IsInContact,
    ptr_point_get_Properties,
};

static HRESULT WINAPI ptr_props_QueryInterface( IPointerPointProperties *iface, REFIID iid, void **out )
{
    struct xaml_pointer_point *impl = impl_from_IPointerPointProperties( iface );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID___x_ABI_CWindows_CUI_CInput_CIPointerPointProperties ))
    {
        InterlockedIncrement( &impl->ref );
        *out = &impl->IPointerPointProperties_iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI ptr_props_AddRef( IPointerPointProperties *iface )
{
    return InterlockedIncrement( &impl_from_IPointerPointProperties( iface )->ref );
}
static ULONG WINAPI ptr_props_Release( IPointerPointProperties *iface )
{
    return ptr_point_Release( &impl_from_IPointerPointProperties( iface )->IPointerPoint_iface );
}
static HRESULT WINAPI ptr_props_GetIids( IPointerPointProperties *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}
static HRESULT WINAPI ptr_props_GetRuntimeClassName( IPointerPointProperties *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.UI.Input.PointerPointProperties", 39, name );
}
static HRESULT WINAPI ptr_props_GetTrustLevel( IPointerPointProperties *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}
static HRESULT WINAPI ptr_props_get_Pressure( IPointerPointProperties *iface, FLOAT *value )
{
    if (!value) return E_POINTER;
    *value = 0.5f;
    return S_OK;
}
static HRESULT WINAPI ptr_props_get_IsInverted( IPointerPointProperties *iface, boolean *value )
{
    if (!value) return E_POINTER;
    *value = FALSE;
    return S_OK;
}
static HRESULT WINAPI ptr_props_get_IsEraser( IPointerPointProperties *iface, boolean *value )
{
    if (!value) return E_POINTER;
    *value = FALSE;
    return S_OK;
}
static HRESULT WINAPI ptr_props_get_Orientation( IPointerPointProperties *iface, FLOAT *value )
{
    if (!value) return E_POINTER;
    *value = 0.0f;
    return S_OK;
}
static HRESULT WINAPI ptr_props_get_XTilt( IPointerPointProperties *iface, FLOAT *value )
{
    if (!value) return E_POINTER;
    *value = 0.0f;
    return S_OK;
}
static HRESULT WINAPI ptr_props_get_YTilt( IPointerPointProperties *iface, FLOAT *value )
{
    if (!value) return E_POINTER;
    *value = 0.0f;
    return S_OK;
}
static HRESULT WINAPI ptr_props_get_Twist( IPointerPointProperties *iface, FLOAT *value )
{
    if (!value) return E_POINTER;
    *value = 0.0f;
    return S_OK;
}
static HRESULT WINAPI ptr_props_get_ContactRect( IPointerPointProperties *iface, Rect *value )
{
    struct xaml_pointer_point *impl = impl_from_IPointerPointProperties( iface );
    if (!value) return E_POINTER;
    value->X = impl->x;
    value->Y = impl->y;
    value->Width = 1.0f;
    value->Height = 1.0f;
    return S_OK;
}
static HRESULT WINAPI ptr_props_get_ContactRectRaw( IPointerPointProperties *iface, Rect *value )
{
    return ptr_props_get_ContactRect( iface, value );
}
static HRESULT WINAPI ptr_props_get_TouchConfidence( IPointerPointProperties *iface, boolean *value )
{
    if (!value) return E_POINTER;
    *value = FALSE;
    return S_OK;
}
static HRESULT WINAPI ptr_props_get_IsLeftButtonPressed( IPointerPointProperties *iface, boolean *value )
{
    struct xaml_pointer_point *impl = impl_from_IPointerPointProperties( iface );
    if (!value) return E_POINTER;
    *value = impl->is_left ? TRUE : FALSE;
    return S_OK;
}
static HRESULT WINAPI ptr_props_get_IsRightButtonPressed( IPointerPointProperties *iface, boolean *value )
{
    struct xaml_pointer_point *impl = impl_from_IPointerPointProperties( iface );
    if (!value) return E_POINTER;
    *value = impl->is_right ? TRUE : FALSE;
    return S_OK;
}
static HRESULT WINAPI ptr_props_get_IsMiddleButtonPressed( IPointerPointProperties *iface, boolean *value )
{
    struct xaml_pointer_point *impl = impl_from_IPointerPointProperties( iface );
    if (!value) return E_POINTER;
    *value = impl->is_middle ? TRUE : FALSE;
    return S_OK;
}
static HRESULT WINAPI ptr_props_get_MouseWheelDelta( IPointerPointProperties *iface, INT32 *value )
{
    struct xaml_pointer_point *impl = impl_from_IPointerPointProperties( iface );
    if (!value) return E_POINTER;
    *value = impl->wheel_delta;
    return S_OK;
}
static HRESULT WINAPI ptr_props_get_IsHorizontalMouseWheel( IPointerPointProperties *iface, boolean *value )
{
    if (!value) return E_POINTER;
    *value = FALSE;
    return S_OK;
}
static HRESULT WINAPI ptr_props_get_IsPrimary( IPointerPointProperties *iface, boolean *value )
{
    if (!value) return E_POINTER;
    *value = TRUE;
    return S_OK;
}
static HRESULT WINAPI ptr_props_get_IsInRange( IPointerPointProperties *iface, boolean *value )
{
    struct xaml_pointer_point *impl = impl_from_IPointerPointProperties( iface );
    if (!value) return E_POINTER;
    *value = impl->is_in_window ? TRUE : FALSE;
    return S_OK;
}
static HRESULT WINAPI ptr_props_get_IsCanceled( IPointerPointProperties *iface, boolean *value )
{
    if (!value) return E_POINTER;
    *value = FALSE;
    return S_OK;
}
static HRESULT WINAPI ptr_props_get_IsBarrelButtonPressed( IPointerPointProperties *iface, boolean *value )
{
    if (!value) return E_POINTER;
    *value = FALSE;
    return S_OK;
}
static HRESULT WINAPI ptr_props_get_IsXButton1Pressed( IPointerPointProperties *iface, boolean *value )
{
    if (!value) return E_POINTER;
    *value = FALSE;
    return S_OK;
}
static HRESULT WINAPI ptr_props_get_IsXButton2Pressed( IPointerPointProperties *iface, boolean *value )
{
    if (!value) return E_POINTER;
    *value = FALSE;
    return S_OK;
}
static HRESULT WINAPI ptr_props_get_PointerUpdateKind( IPointerPointProperties *iface, PointerUpdateKind *value )
{
    struct xaml_pointer_point *impl = impl_from_IPointerPointProperties( iface );
    if (!value) return E_POINTER;
    *value = impl->update_kind;
    return S_OK;
}
static HRESULT WINAPI ptr_props_HasUsage( IPointerPointProperties *iface, UINT32 page, UINT32 id, boolean *value )
{
    if (!value) return E_POINTER;
    *value = FALSE;
    return S_OK;
}
static HRESULT WINAPI ptr_props_GetUsageValue( IPointerPointProperties *iface, UINT32 page, UINT32 id, INT32 *value )
{
    if (!value) return E_POINTER;
    *value = 0;
    return S_OK;
}

static const IPointerPointPropertiesVtbl ptr_props_vtbl =
{
    ptr_props_QueryInterface,
    ptr_props_AddRef,
    ptr_props_Release,
    ptr_props_GetIids,
    ptr_props_GetRuntimeClassName,
    ptr_props_GetTrustLevel,
    ptr_props_get_Pressure,
    ptr_props_get_IsInverted,
    ptr_props_get_IsEraser,
    ptr_props_get_Orientation,
    ptr_props_get_XTilt,
    ptr_props_get_YTilt,
    ptr_props_get_Twist,
    ptr_props_get_ContactRect,
    ptr_props_get_ContactRectRaw,
    ptr_props_get_TouchConfidence,
    ptr_props_get_IsLeftButtonPressed,
    ptr_props_get_IsRightButtonPressed,
    ptr_props_get_IsMiddleButtonPressed,
    ptr_props_get_MouseWheelDelta,
    ptr_props_get_IsHorizontalMouseWheel,
    ptr_props_get_IsPrimary,
    ptr_props_get_IsInRange,
    ptr_props_get_IsCanceled,
    ptr_props_get_IsBarrelButtonPressed,
    ptr_props_get_IsXButton1Pressed,
    ptr_props_get_IsXButton2Pressed,
    ptr_props_get_PointerUpdateKind,
    ptr_props_HasUsage,
    ptr_props_GetUsageValue,
};

IPointerPoint *xaml_pointer_point_create( const struct xaml_pointer_info *info, float rel_x, float rel_y )
{
    struct xaml_pointer_point *impl = calloc( 1, sizeof(*impl) );
    if (!impl) return NULL;
    impl->IPointerPoint_iface.lpVtbl = &ptr_point_vtbl;
    impl->IPointerPointProperties_iface.lpVtbl = &ptr_props_vtbl;
    impl->ref = 1;
    impl->x = rel_x;
    impl->y = rel_y;
    impl->pointer_id = info && info->pointer_id ? info->pointer_id : 1;
    impl->frame_id = (UINT32)InterlockedIncrement( &xaml_pointer_frame_seq );
    impl->timestamp = GetTickCount64() * 1000;
    if (info)
    {
        impl->is_in_contact = info->is_in_contact;
        impl->is_in_window = info->is_in_window;
        impl->is_left = info->is_left;
        impl->is_right = info->is_right;
        impl->is_middle = info->is_middle;
        impl->wheel_delta = info->wheel_delta;
        impl->update_kind = info->update_kind;
    }
    return &impl->IPointerPoint_iface;
}

struct xaml_core_pointer_args
{
    __x_ABI_CWindows_CUI_CCore_CIPointerEventArgs IPointerEventArgs_iface;
    __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs ICoreWindowEventArgs_iface;
    LONG ref;
    IPointerPoint *point;
    UINT32 key_modifiers;
    boolean handled;
};

static inline struct xaml_core_pointer_args *impl_from_cw_ptr_args( __x_ABI_CWindows_CUI_CCore_CIPointerEventArgs *iface )
{
    return CONTAINING_RECORD( iface, struct xaml_core_pointer_args, IPointerEventArgs_iface );
}
static inline struct xaml_core_pointer_args *impl_from_cw_core_args( __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs *iface )
{
    return CONTAINING_RECORD( iface, struct xaml_core_pointer_args, ICoreWindowEventArgs_iface );
}

static HRESULT WINAPI cw_ptr_args_QueryInterface( __x_ABI_CWindows_CUI_CCore_CIPointerEventArgs *iface, REFIID iid, void **out )
{
    struct xaml_core_pointer_args *impl = impl_from_cw_ptr_args( iface );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID___x_ABI_CWindows_CUI_CCore_CIPointerEventArgs ))
    {
        InterlockedIncrement( &impl->ref );
        *out = &impl->IPointerEventArgs_iface;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID___x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs ))
    {
        InterlockedIncrement( &impl->ref );
        *out = &impl->ICoreWindowEventArgs_iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI cw_ptr_args_AddRef( __x_ABI_CWindows_CUI_CCore_CIPointerEventArgs *iface )
{
    return InterlockedIncrement( &impl_from_cw_ptr_args( iface )->ref );
}
static ULONG WINAPI cw_ptr_args_Release( __x_ABI_CWindows_CUI_CCore_CIPointerEventArgs *iface )
{
    struct xaml_core_pointer_args *impl = impl_from_cw_ptr_args( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        if (impl->point) IPointerPoint_Release( impl->point );
        free( impl );
    }
    return ref;
}
static HRESULT WINAPI cw_ptr_args_GetIids( __x_ABI_CWindows_CUI_CCore_CIPointerEventArgs *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}
static HRESULT WINAPI cw_ptr_args_GetRuntimeClassName( __x_ABI_CWindows_CUI_CCore_CIPointerEventArgs *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.UI.Core.PointerEventArgs", 32, name );
}
static HRESULT WINAPI cw_ptr_args_GetTrustLevel( __x_ABI_CWindows_CUI_CCore_CIPointerEventArgs *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}
static HRESULT WINAPI cw_ptr_args_get_CurrentPoint( __x_ABI_CWindows_CUI_CCore_CIPointerEventArgs *iface, IPointerPoint **value )
{
    struct xaml_core_pointer_args *impl = impl_from_cw_ptr_args( iface );
    if (!value) return E_POINTER;
    *value = impl->point;
    if (*value) IPointerPoint_AddRef( *value );
    return S_OK;
}
static HRESULT WINAPI cw_ptr_args_get_KeyModifiers( __x_ABI_CWindows_CUI_CCore_CIPointerEventArgs *iface, UINT32 *value )
{
    struct xaml_core_pointer_args *impl = impl_from_cw_ptr_args( iface );
    if (!value) return E_POINTER;
    *value = impl->key_modifiers;
    return S_OK;
}
static HRESULT WINAPI cw_ptr_args_GetIntermediatePoints( __x_ABI_CWindows_CUI_CCore_CIPointerEventArgs *iface,
    __FIVector_1_Windows__CUI__CInput__CPointerPoint **value )
{
    if (!value) return E_POINTER;
    *value = NULL;
    return S_OK;
}

static const struct __x_ABI_CWindows_CUI_CCore_CIPointerEventArgsVtbl cw_ptr_args_vtbl =
{
    cw_ptr_args_QueryInterface,
    cw_ptr_args_AddRef,
    cw_ptr_args_Release,
    cw_ptr_args_GetIids,
    cw_ptr_args_GetRuntimeClassName,
    cw_ptr_args_GetTrustLevel,
    cw_ptr_args_get_CurrentPoint,
    cw_ptr_args_get_KeyModifiers,
    cw_ptr_args_GetIntermediatePoints,
};

static HRESULT WINAPI cw_core_args_QueryInterface( __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs *iface, REFIID iid, void **out )
{
    return cw_ptr_args_QueryInterface( &impl_from_cw_core_args( iface )->IPointerEventArgs_iface, iid, out );
}
static ULONG WINAPI cw_core_args_AddRef( __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs *iface )
{
    return cw_ptr_args_AddRef( &impl_from_cw_core_args( iface )->IPointerEventArgs_iface );
}
static ULONG WINAPI cw_core_args_Release( __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs *iface )
{
    return cw_ptr_args_Release( &impl_from_cw_core_args( iface )->IPointerEventArgs_iface );
}
static HRESULT WINAPI cw_core_args_GetIids( __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}
static HRESULT WINAPI cw_core_args_GetRuntimeClassName( __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.UI.Core.PointerEventArgs", 32, name );
}
static HRESULT WINAPI cw_core_args_GetTrustLevel( __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}
static HRESULT WINAPI cw_core_args_get_Handled( __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs *iface, boolean *value )
{
    if (!value) return E_POINTER;
    *value = impl_from_cw_core_args( iface )->handled;
    return S_OK;
}
static HRESULT WINAPI cw_core_args_put_Handled( __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs *iface, boolean value )
{
    impl_from_cw_core_args( iface )->handled = value;
    return S_OK;
}

static const struct __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgsVtbl cw_core_args_vtbl =
{
    cw_core_args_QueryInterface,
    cw_core_args_AddRef,
    cw_core_args_Release,
    cw_core_args_GetIids,
    cw_core_args_GetRuntimeClassName,
    cw_core_args_GetTrustLevel,
    cw_core_args_get_Handled,
    cw_core_args_put_Handled,
};

__x_ABI_CWindows_CUI_CCore_CIPointerEventArgs *xaml_core_pointer_args_create( const struct xaml_pointer_info *info )
{
    struct xaml_core_pointer_args *impl = calloc( 1, sizeof(*impl) );
    if (!impl) return NULL;
    TRACE( "creating core pointer args.\n" );
    impl->IPointerEventArgs_iface.lpVtbl = &cw_ptr_args_vtbl;
    impl->ICoreWindowEventArgs_iface.lpVtbl = &cw_core_args_vtbl;
    impl->ref = 1;
    impl->key_modifiers = info ? info->key_modifiers : 0;
    impl->point = xaml_pointer_point_create( info, info ? info->x : 0.0f, info ? info->y : 0.0f );
    return &impl->IPointerEventArgs_iface;
}


struct xaml_core_key_args
{
    __x_ABI_CWindows_CUI_CCore_CIKeyEventArgs IKeyEventArgs_iface;
    __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs ICoreWindowEventArgs_iface;
    LONG ref;
    __x_ABI_CWindows_CSystem_CVirtualKey key;
    __x_ABI_CWindows_CUI_CCore_CCorePhysicalKeyStatus status;
    boolean handled;
};

static inline struct xaml_core_key_args *impl_from_key_args( __x_ABI_CWindows_CUI_CCore_CIKeyEventArgs *iface )
{
    return CONTAINING_RECORD( iface, struct xaml_core_key_args, IKeyEventArgs_iface );
}
static inline struct xaml_core_key_args *impl_from_key_core_args( __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs *iface )
{
    return CONTAINING_RECORD( iface, struct xaml_core_key_args, ICoreWindowEventArgs_iface );
}

static HRESULT WINAPI cw_key_args_QI( __x_ABI_CWindows_CUI_CCore_CIKeyEventArgs *iface, REFIID iid, void **out )
{
    struct xaml_core_key_args *impl = impl_from_key_args( iface );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID___x_ABI_CWindows_CUI_CCore_CIKeyEventArgs ))
    {
        InterlockedIncrement( &impl->ref );
        *out = &impl->IKeyEventArgs_iface;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID___x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs ))
    {
        InterlockedIncrement( &impl->ref );
        *out = &impl->ICoreWindowEventArgs_iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI cw_key_args_AddRef( __x_ABI_CWindows_CUI_CCore_CIKeyEventArgs *iface )
{
    return InterlockedIncrement( &impl_from_key_args( iface )->ref );
}
static ULONG WINAPI cw_key_args_Release( __x_ABI_CWindows_CUI_CCore_CIKeyEventArgs *iface )
{
    struct xaml_core_key_args *impl = impl_from_key_args( iface );
    LONG ref = InterlockedDecrement( &impl->ref );
    if (!ref) free( impl );
    return ref;
}
static HRESULT WINAPI cw_key_args_GetIids( __x_ABI_CWindows_CUI_CCore_CIKeyEventArgs *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}
static HRESULT WINAPI cw_key_args_GetRuntimeClassName( __x_ABI_CWindows_CUI_CCore_CIKeyEventArgs *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.UI.Core.KeyEventArgs", 28, name );
}
static HRESULT WINAPI cw_key_args_GetTrustLevel( __x_ABI_CWindows_CUI_CCore_CIKeyEventArgs *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}
static HRESULT WINAPI cw_key_args_get_Key( __x_ABI_CWindows_CUI_CCore_CIKeyEventArgs *iface,
    __x_ABI_CWindows_CSystem_CVirtualKey *value )
{
    if (!value) return E_POINTER;
    *value = impl_from_key_args( iface )->key;
    return S_OK;
}
static HRESULT WINAPI cw_key_args_get_KeyStatus( __x_ABI_CWindows_CUI_CCore_CIKeyEventArgs *iface,
    __x_ABI_CWindows_CUI_CCore_CCorePhysicalKeyStatus *value )
{
    if (!value) return E_POINTER;
    *value = impl_from_key_args( iface )->status;
    return S_OK;
}
static const __x_ABI_CWindows_CUI_CCore_CIKeyEventArgsVtbl cw_key_args_vtbl =
{
    cw_key_args_QI,
    cw_key_args_AddRef,
    cw_key_args_Release,
    cw_key_args_GetIids,
    cw_key_args_GetRuntimeClassName,
    cw_key_args_GetTrustLevel,
    cw_key_args_get_Key,
    cw_key_args_get_KeyStatus,
};

static HRESULT WINAPI cw_key_core_args_QI( __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs *iface, REFIID iid, void **out )
{
    struct xaml_core_key_args *impl = impl_from_key_core_args( iface );
    return __x_ABI_CWindows_CUI_CCore_CIKeyEventArgs_QueryInterface( &impl->IKeyEventArgs_iface, iid, out );
}
static ULONG WINAPI cw_key_core_args_AddRef( __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs *iface )
{
    struct xaml_core_key_args *impl = impl_from_key_core_args( iface );
    return __x_ABI_CWindows_CUI_CCore_CIKeyEventArgs_AddRef( &impl->IKeyEventArgs_iface );
}
static ULONG WINAPI cw_key_core_args_Release( __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs *iface )
{
    struct xaml_core_key_args *impl = impl_from_key_core_args( iface );
    return __x_ABI_CWindows_CUI_CCore_CIKeyEventArgs_Release( &impl->IKeyEventArgs_iface );
}
static HRESULT WINAPI cw_key_core_args_GetIids( __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}
static HRESULT WINAPI cw_key_core_args_GetRuntimeClassName( __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.UI.Core.KeyEventArgs", 28, name );
}
static HRESULT WINAPI cw_key_core_args_GetTrustLevel( __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}
static HRESULT WINAPI cw_key_core_args_get_Handled( __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs *iface, boolean *value )
{
    if (!value) return E_POINTER;
    *value = impl_from_key_core_args( iface )->handled;
    return S_OK;
}
static HRESULT WINAPI cw_key_core_args_put_Handled( __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs *iface, boolean value )
{
    impl_from_key_core_args( iface )->handled = value;
    return S_OK;
}
static const __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgsVtbl cw_key_core_args_vtbl =
{
    cw_key_core_args_QI,
    cw_key_core_args_AddRef,
    cw_key_core_args_Release,
    cw_key_core_args_GetIids,
    cw_key_core_args_GetRuntimeClassName,
    cw_key_core_args_GetTrustLevel,
    cw_key_core_args_get_Handled,
    cw_key_core_args_put_Handled,
};

struct xaml_core_char_args
{
    __x_ABI_CWindows_CUI_CCore_CICharacterReceivedEventArgs ICharacterReceivedEventArgs_iface;
    __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs ICoreWindowEventArgs_iface;
    LONG ref;
    UINT32 code;
    __x_ABI_CWindows_CUI_CCore_CCorePhysicalKeyStatus status;
    boolean handled;
};

static inline struct xaml_core_char_args *impl_from_char_args( __x_ABI_CWindows_CUI_CCore_CICharacterReceivedEventArgs *iface )
{
    return CONTAINING_RECORD( iface, struct xaml_core_char_args, ICharacterReceivedEventArgs_iface );
}
static inline struct xaml_core_char_args *impl_from_char_core_args( __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs *iface )
{
    return CONTAINING_RECORD( iface, struct xaml_core_char_args, ICoreWindowEventArgs_iface );
}

static HRESULT WINAPI cw_char_args_QI( __x_ABI_CWindows_CUI_CCore_CICharacterReceivedEventArgs *iface, REFIID iid, void **out )
{
    struct xaml_core_char_args *impl = impl_from_char_args( iface );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID___x_ABI_CWindows_CUI_CCore_CICharacterReceivedEventArgs ))
    {
        InterlockedIncrement( &impl->ref );
        *out = &impl->ICharacterReceivedEventArgs_iface;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID___x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs ))
    {
        InterlockedIncrement( &impl->ref );
        *out = &impl->ICoreWindowEventArgs_iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI cw_char_args_AddRef( __x_ABI_CWindows_CUI_CCore_CICharacterReceivedEventArgs *iface )
{
    return InterlockedIncrement( &impl_from_char_args( iface )->ref );
}
static ULONG WINAPI cw_char_args_Release( __x_ABI_CWindows_CUI_CCore_CICharacterReceivedEventArgs *iface )
{
    struct xaml_core_char_args *impl = impl_from_char_args( iface );
    LONG ref = InterlockedDecrement( &impl->ref );
    if (!ref) free( impl );
    return ref;
}
static HRESULT WINAPI cw_char_args_GetIids( __x_ABI_CWindows_CUI_CCore_CICharacterReceivedEventArgs *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}
static HRESULT WINAPI cw_char_args_GetRuntimeClassName( __x_ABI_CWindows_CUI_CCore_CICharacterReceivedEventArgs *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.UI.Core.CharacterReceivedEventArgs", 42, name );
}
static HRESULT WINAPI cw_char_args_GetTrustLevel( __x_ABI_CWindows_CUI_CCore_CICharacterReceivedEventArgs *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}
static HRESULT WINAPI cw_char_args_get_KeyCode( __x_ABI_CWindows_CUI_CCore_CICharacterReceivedEventArgs *iface, UINT32 *value )
{
    if (!value) return E_POINTER;
    *value = impl_from_char_args( iface )->code;
    return S_OK;
}
static HRESULT WINAPI cw_char_args_get_KeyStatus( __x_ABI_CWindows_CUI_CCore_CICharacterReceivedEventArgs *iface,
    __x_ABI_CWindows_CUI_CCore_CCorePhysicalKeyStatus *value )
{
    if (!value) return E_POINTER;
    *value = impl_from_char_args( iface )->status;
    return S_OK;
}
static const __x_ABI_CWindows_CUI_CCore_CICharacterReceivedEventArgsVtbl cw_char_args_vtbl =
{
    cw_char_args_QI,
    cw_char_args_AddRef,
    cw_char_args_Release,
    cw_char_args_GetIids,
    cw_char_args_GetRuntimeClassName,
    cw_char_args_GetTrustLevel,
    cw_char_args_get_KeyCode,
    cw_char_args_get_KeyStatus,
};

static HRESULT WINAPI cw_char_core_args_QI( __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs *iface, REFIID iid, void **out )
{
    struct xaml_core_char_args *impl = impl_from_char_core_args( iface );
    return __x_ABI_CWindows_CUI_CCore_CICharacterReceivedEventArgs_QueryInterface( &impl->ICharacterReceivedEventArgs_iface, iid, out );
}
static ULONG WINAPI cw_char_core_args_AddRef( __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs *iface )
{
    struct xaml_core_char_args *impl = impl_from_char_core_args( iface );
    return __x_ABI_CWindows_CUI_CCore_CICharacterReceivedEventArgs_AddRef( &impl->ICharacterReceivedEventArgs_iface );
}
static ULONG WINAPI cw_char_core_args_Release( __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs *iface )
{
    struct xaml_core_char_args *impl = impl_from_char_core_args( iface );
    return __x_ABI_CWindows_CUI_CCore_CICharacterReceivedEventArgs_Release( &impl->ICharacterReceivedEventArgs_iface );
}
static HRESULT WINAPI cw_char_core_args_GetIids( __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}
static HRESULT WINAPI cw_char_core_args_GetRuntimeClassName( __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.UI.Core.CharacterReceivedEventArgs", 42, name );
}
static HRESULT WINAPI cw_char_core_args_GetTrustLevel( __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}
static HRESULT WINAPI cw_char_core_args_get_Handled( __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs *iface, boolean *value )
{
    if (!value) return E_POINTER;
    *value = impl_from_char_core_args( iface )->handled;
    return S_OK;
}
static HRESULT WINAPI cw_char_core_args_put_Handled( __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs *iface, boolean value )
{
    impl_from_char_core_args( iface )->handled = value;
    return S_OK;
}
static const __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgsVtbl cw_char_core_args_vtbl =
{
    cw_char_core_args_QI,
    cw_char_core_args_AddRef,
    cw_char_core_args_Release,
    cw_char_core_args_GetIids,
    cw_char_core_args_GetRuntimeClassName,
    cw_char_core_args_GetTrustLevel,
    cw_char_core_args_get_Handled,
    cw_char_core_args_put_Handled,
};

static void cw_fill_key_status( __x_ABI_CWindows_CUI_CCore_CCorePhysicalKeyStatus *status, UINT msg, LPARAM lparam )
{
    status->RepeatCount = LOWORD( lparam );
    status->ScanCode = (UINT16)((lparam >> 16) & 0xff);
    status->IsExtendedKey = !!(lparam & 0x01000000);
    status->IsMenuKeyDown = (GetKeyState( VK_MENU ) & 0x8000) != 0;
    status->WasKeyDown = (msg == WM_KEYDOWN);
    status->IsKeyReleased = (msg == WM_KEYUP);
}

static __x_ABI_CWindows_CUI_CCore_CIKeyEventArgs *xaml_core_key_args_create( WPARAM wparam, UINT msg, LPARAM lparam )
{
    struct xaml_core_key_args *impl = calloc( 1, sizeof(*impl) );
    if (!impl) return NULL;
    impl->IKeyEventArgs_iface.lpVtbl = &cw_key_args_vtbl;
    impl->ICoreWindowEventArgs_iface.lpVtbl = &cw_key_core_args_vtbl;
    impl->ref = 1;
    impl->key = (__x_ABI_CWindows_CSystem_CVirtualKey)wparam;
    cw_fill_key_status( &impl->status, msg, lparam );
    return &impl->IKeyEventArgs_iface;
}

static __x_ABI_CWindows_CUI_CCore_CICharacterReceivedEventArgs *xaml_core_char_args_create( WPARAM wparam, LPARAM lparam )
{
    struct xaml_core_char_args *impl = calloc( 1, sizeof(*impl) );
    if (!impl) return NULL;
    impl->ICharacterReceivedEventArgs_iface.lpVtbl = &cw_char_args_vtbl;
    impl->ICoreWindowEventArgs_iface.lpVtbl = &cw_char_core_args_vtbl;
    impl->ref = 1;
    impl->code = (UINT32)wparam;
    cw_fill_key_status( &impl->status, WM_KEYDOWN, lparam );
    return &impl->ICharacterReceivedEventArgs_iface;
}

void xaml_cw_fire_key_event( struct xaml_core_window *cw, UINT msg, WPARAM wparam, LPARAM lparam )
{
    struct xaml_event_registry *reg;
    void *handlers[64];
    unsigned int i, count;

    if (!cw) return;
    reg = msg == WM_KEYDOWN ? &cw->key_down : msg == WM_KEYUP ? &cw->key_up : &cw->character_received;
    count = xaml_event_registry_snapshot( reg, handlers, ARRAY_SIZE( handlers ) );
    if (!count) return;

    if (msg == WM_CHAR)
    {
        __x_ABI_CWindows_CUI_CCore_CICharacterReceivedEventArgs *args = xaml_core_char_args_create( wparam, lparam );
        if (!args)
        {
            for (i = 0; i < count; i++) IUnknown_Release( (IUnknown *)handlers[i] );
            return;
        }
        for (i = 0; i < count; i++)
        {
            __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CCharacterReceivedEventArgs *h = handlers[i];
            __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CCharacterReceivedEventArgs_Invoke(
                h, &cw->ICoreWindow_iface, args );
            IUnknown_Release( (IUnknown *)h );
        }
        __x_ABI_CWindows_CUI_CCore_CICharacterReceivedEventArgs_Release( args );
    }
    else
    {
        __x_ABI_CWindows_CUI_CCore_CIKeyEventArgs *args = xaml_core_key_args_create( wparam, msg, lparam );
        if (!args)
        {
            for (i = 0; i < count; i++) IUnknown_Release( (IUnknown *)handlers[i] );
            return;
        }
        for (i = 0; i < count; i++)
        {
            __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CKeyEventArgs *h = handlers[i];
            __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CKeyEventArgs_Invoke(
                h, &cw->ICoreWindow_iface, args );
            IUnknown_Release( (IUnknown *)h );
        }
        __x_ABI_CWindows_CUI_CCore_CIKeyEventArgs_Release( args );
    }
}

/* Fire the handlers the app registered on the inner windows.ui CoreWindow
 * (store apps reach it through twinapi's view.CoreWindow, not the XAML
 * wrapper).  Resolved by name out of windows.ui, the same way the module
 * reaches the core-window creation entry points. */
HRESULT notify_inner_core_window_key( HWND hwnd, UINT msg, WPARAM wparam )
{
    static HRESULT (WINAPI *pnotify)( HWND, UINT, WPARAM );
    HMODULE module;

    if (!pnotify)
    {
        module = GetModuleHandleW( L"windows.ui.dll" );
        if (!module) module = LoadLibraryW( L"windows.ui.dll" );
        if (!module) return E_FAIL;
        if (!(pnotify = (void *)GetProcAddress( module, "CoreWindow_NotifyKeyMsg" ))) return E_FAIL;
    }
    return pnotify( hwnd, msg, wparam );
}

void xaml_cw_fire_pointer_event( struct xaml_core_window *cw, enum xaml_pointer_event_kind kind,
                                        const struct xaml_pointer_info *info )
{
    struct xaml_event_registry *reg = NULL;
    void *handlers[64];
    unsigned int i, count;
    __x_ABI_CWindows_CUI_CCore_CIPointerEventArgs *args;

    if (!cw) return;
    switch (kind)
    {
    case XAML_PTR_PRESSED:      reg = &cw->pointer_pressed; break;
    case XAML_PTR_MOVED:        reg = &cw->pointer_moved; break;
    case XAML_PTR_RELEASED:     reg = &cw->pointer_released; break;
    case XAML_PTR_WHEEL:        reg = &cw->pointer_wheel_changed; break;
    case XAML_PTR_ENTERED:      reg = &cw->pointer_entered; break;
    case XAML_PTR_EXITED:       reg = &cw->pointer_exited; break;
    case XAML_PTR_CAPTURE_LOST: reg = &cw->pointer_capture_lost; break;
    }
    if (!reg) return;
    count = xaml_event_registry_snapshot( reg, handlers, ARRAY_SIZE( handlers ) );
    if (!count) return;

    args = xaml_core_pointer_args_create( info );
    if (!args)
    {
        for (i = 0; i < count; i++) IUnknown_Release( (IUnknown *)handlers[i] );
        return;
    }
    for (i = 0; i < count; i++)
    {
        __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CPointerEventArgs *h = handlers[i];
        __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CPointerEventArgs_Invoke(
            h, &cw->ICoreWindow_iface, args );
        IUnknown_Release( (IUnknown *)h );
    }
    __x_ABI_CWindows_CUI_CCore_CIPointerEventArgs_Release( args );
}
