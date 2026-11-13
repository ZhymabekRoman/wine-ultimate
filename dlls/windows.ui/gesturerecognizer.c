/* WinRT Windows.UI.Input.GestureRecognizer Implementation
 *
 * Copyright (C) 2026 Zhymabek Roman
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

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "private.h"

#include "wine/debug.h"
#include "wine/list.h"

WINE_DEFAULT_DEBUG_CHANNEL(ui);

#define MANIPULATION_THRESHOLD 4.0f

typedef __x_ABI_CWindows_CUI_CInput_CIPointerPoint PointerPoint;
typedef __FIVector_1_Windows__CUI__CInput__CPointerPoint PointerPointVector;
typedef __FITypedEventHandler_2_Windows__CUI__CInput__CGestureRecognizer_Windows__CUI__CInput__CTappedEventArgs TappedEventHandler;
typedef __FITypedEventHandler_2_Windows__CUI__CInput__CGestureRecognizer_Windows__CUI__CInput__CHoldingEventArgs HoldingEventHandler;
typedef __FITypedEventHandler_2_Windows__CUI__CInput__CGestureRecognizer_Windows__CUI__CInput__CManipulationStartedEventArgs ManipulationStartedEventHandler;
typedef __FITypedEventHandler_2_Windows__CUI__CInput__CGestureRecognizer_Windows__CUI__CInput__CManipulationUpdatedEventArgs ManipulationUpdatedEventHandler;
typedef __FITypedEventHandler_2_Windows__CUI__CInput__CGestureRecognizer_Windows__CUI__CInput__CManipulationInertiaStartingEventArgs ManipulationInertiaStartingEventHandler;
typedef __FITypedEventHandler_2_Windows__CUI__CInput__CGestureRecognizer_Windows__CUI__CInput__CManipulationCompletedEventArgs ManipulationCompletedEventHandler;

struct event_entry
{
    struct list entry;
    EventRegistrationToken token;
    IUnknown *handler;
};

struct gesture_recognizer
{
    IGestureRecognizer IGestureRecognizer_iface;
    LONG ref;

    struct list tapped;
    struct list right_tapped;
    struct list holding;
    struct list manipulation_started;
    struct list manipulation_updated;
    struct list manipulation_inertia_starting;
    struct list manipulation_completed;

    enum GestureSettings settings;
    ULONGLONG next_token;
    boolean is_active;
    boolean is_inertial;
    boolean in_manipulation;
    Point start_pos;
    Point last_pos;
    float delta_x;
    float delta_y;
};

static inline struct gesture_recognizer *impl_from_IGestureRecognizer( IGestureRecognizer *iface )
{
    return CONTAINING_RECORD( iface, struct gesture_recognizer, IGestureRecognizer_iface );
}

static void list_add_event( struct list *events, IUnknown *handler, EventRegistrationToken *token,
                            struct gesture_recognizer *impl )
{
    struct event_entry *entry = malloc( sizeof(*entry) );

    if (!entry)
    {
        token->value = 0;
        return;
    }

    IUnknown_AddRef( handler );
    entry->handler = handler;
    entry->token.value = ++impl->next_token;
    token->value = entry->token.value;
    list_add_tail( events, &entry->entry );
}

static void list_remove_event( struct list *events, EventRegistrationToken token )
{
    struct event_entry *entry, *tmp;

    LIST_FOR_EACH_ENTRY_SAFE( entry, tmp, events, struct event_entry, entry )
    {
        if (entry->token.value == token.value)
        {
            list_remove( &entry->entry );
            IUnknown_Release( entry->handler );
            free( entry );
            return;
        }
    }
}

static void list_free_events( struct list *events )
{
    struct event_entry *entry, *tmp;

    LIST_FOR_EACH_ENTRY_SAFE( entry, tmp, events, struct event_entry, entry )
    {
        list_remove( &entry->entry );
        IUnknown_Release( entry->handler );
        free( entry );
    }
}

#define FIRE_EVENT( events, handler_type, args_iface )                                             \
    do                                                                                             \
    {                                                                                              \
        struct event_entry *entry;                                                                 \
        LIST_FOR_EACH_ENTRY( entry, events, struct event_entry, entry )                            \
        {                                                                                          \
            handler_type *handler = (handler_type *)entry->handler;                                \
            handler->lpVtbl->Invoke( handler, &impl->IGestureRecognizer_iface, args_iface );       \
        }                                                                                          \
    }                                                                                              \
    while (0)

/* Event arguments objects. */

struct tapped_event_args
{
    ITappedEventArgs ITappedEventArgs_iface;
    LONG ref;
    Point position;
    UINT32 tap_count;
};

static inline struct tapped_event_args *impl_from_ITappedEventArgs( ITappedEventArgs *iface )
{
    return CONTAINING_RECORD( iface, struct tapped_event_args, ITappedEventArgs_iface );
}

static HRESULT WINAPI tapped_args_QueryInterface( ITappedEventArgs *iface, REFIID iid, void **out )
{
    struct tapped_event_args *impl = impl_from_ITappedEventArgs( iface );

    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_ITappedEventArgs ))
    {
        *out = &impl->ITappedEventArgs_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI tapped_args_AddRef( ITappedEventArgs *iface )
{
    struct tapped_event_args *impl = impl_from_ITappedEventArgs( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI tapped_args_Release( ITappedEventArgs *iface )
{
    struct tapped_event_args *impl = impl_from_ITappedEventArgs( iface );
    LONG ref = InterlockedDecrement( &impl->ref );
    if (!ref) free( impl );
    return ref;
}

static HRESULT WINAPI tapped_args_GetIids( ITappedEventArgs *iface, ULONG *count, IID **iids )
{
    IID *array = CoTaskMemAlloc( sizeof(IID) );
    if (!array) return E_OUTOFMEMORY;
    *array = IID_ITappedEventArgs;
    *count = 1;
    *iids = array;
    return S_OK;
}

static HRESULT WINAPI tapped_args_GetRuntimeClassName( ITappedEventArgs *iface, HSTRING *name )
{
    return WindowsCreateString( RuntimeClass_Windows_UI_Input_TappedEventArgs,
                                lstrlenW( RuntimeClass_Windows_UI_Input_TappedEventArgs ), name );
}

static HRESULT WINAPI tapped_args_GetTrustLevel( ITappedEventArgs *iface, TrustLevel *level )
{
    *level = FullTrust;
    return S_OK;
}

static HRESULT WINAPI tapped_args_get_Position( ITappedEventArgs *iface, Point *value )
{
    struct tapped_event_args *impl = impl_from_ITappedEventArgs( iface );
    if (!value) return E_POINTER;
    *value = impl->position;
    return S_OK;
}

static HRESULT WINAPI tapped_args_get_TapCount( ITappedEventArgs *iface, UINT32 *value )
{
    struct tapped_event_args *impl = impl_from_ITappedEventArgs( iface );
    if (!value) return E_POINTER;
    *value = impl->tap_count;
    return S_OK;
}

static const struct ITappedEventArgsVtbl tapped_args_vtbl =
{
    tapped_args_QueryInterface,
    tapped_args_AddRef,
    tapped_args_Release,
    tapped_args_GetIids,
    tapped_args_GetRuntimeClassName,
    tapped_args_GetTrustLevel,
    tapped_args_get_Position,
    tapped_args_get_TapCount,
};

struct manipulation_started_event_args
{
    IManipulationStartedEventArgs IManipulationStartedEventArgs_iface;
    LONG ref;
    Point position;
    ManipulationDelta cumulative;
};

static inline struct manipulation_started_event_args *impl_from_IManipulationStartedEventArgs(
        IManipulationStartedEventArgs *iface )
{
    return CONTAINING_RECORD( iface, struct manipulation_started_event_args,
                              IManipulationStartedEventArgs_iface );
}

static HRESULT WINAPI started_args_QueryInterface( IManipulationStartedEventArgs *iface, REFIID iid, void **out )
{
    struct manipulation_started_event_args *impl = impl_from_IManipulationStartedEventArgs( iface );

    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IManipulationStartedEventArgs ))
    {
        *out = &impl->IManipulationStartedEventArgs_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI started_args_AddRef( IManipulationStartedEventArgs *iface )
{
    struct manipulation_started_event_args *impl = impl_from_IManipulationStartedEventArgs( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI started_args_Release( IManipulationStartedEventArgs *iface )
{
    struct manipulation_started_event_args *impl = impl_from_IManipulationStartedEventArgs( iface );
    LONG ref = InterlockedDecrement( &impl->ref );
    if (!ref) free( impl );
    return ref;
}

static HRESULT WINAPI started_args_GetIids( IManipulationStartedEventArgs *iface, ULONG *count, IID **iids )
{
    IID *array = CoTaskMemAlloc( sizeof(IID) );
    if (!array) return E_OUTOFMEMORY;
    *array = IID_IManipulationStartedEventArgs;
    *count = 1;
    *iids = array;
    return S_OK;
}

static HRESULT WINAPI started_args_GetRuntimeClassName( IManipulationStartedEventArgs *iface, HSTRING *name )
{
    return WindowsCreateString( RuntimeClass_Windows_UI_Input_ManipulationStartedEventArgs,
                                lstrlenW( RuntimeClass_Windows_UI_Input_ManipulationStartedEventArgs ), name );
}

static HRESULT WINAPI started_args_GetTrustLevel( IManipulationStartedEventArgs *iface, TrustLevel *level )
{
    *level = FullTrust;
    return S_OK;
}

static HRESULT WINAPI started_args_get_Position( IManipulationStartedEventArgs *iface, Point *value )
{
    struct manipulation_started_event_args *impl = impl_from_IManipulationStartedEventArgs( iface );
    if (!value) return E_POINTER;
    *value = impl->position;
    return S_OK;
}

static HRESULT WINAPI started_args_get_Cumulative( IManipulationStartedEventArgs *iface, ManipulationDelta *value )
{
    struct manipulation_started_event_args *impl = impl_from_IManipulationStartedEventArgs( iface );
    if (!value) return E_POINTER;
    *value = impl->cumulative;
    return S_OK;
}

static const struct IManipulationStartedEventArgsVtbl started_args_vtbl =
{
    started_args_QueryInterface,
    started_args_AddRef,
    started_args_Release,
    started_args_GetIids,
    started_args_GetRuntimeClassName,
    started_args_GetTrustLevel,
    started_args_get_Position,
    started_args_get_Cumulative,
};

struct manipulation_updated_event_args
{
    IManipulationUpdatedEventArgs IManipulationUpdatedEventArgs_iface;
    LONG ref;
    Point position;
    ManipulationDelta cumulative;
    ManipulationDelta delta;
};

static inline struct manipulation_updated_event_args *impl_from_IManipulationUpdatedEventArgs(
        IManipulationUpdatedEventArgs *iface )
{
    return CONTAINING_RECORD( iface, struct manipulation_updated_event_args,
                              IManipulationUpdatedEventArgs_iface );
}

static HRESULT WINAPI updated_args_QueryInterface( IManipulationUpdatedEventArgs *iface, REFIID iid, void **out )
{
    struct manipulation_updated_event_args *impl = impl_from_IManipulationUpdatedEventArgs( iface );

    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IManipulationUpdatedEventArgs ))
    {
        *out = &impl->IManipulationUpdatedEventArgs_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI updated_args_AddRef( IManipulationUpdatedEventArgs *iface )
{
    struct manipulation_updated_event_args *impl = impl_from_IManipulationUpdatedEventArgs( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI updated_args_Release( IManipulationUpdatedEventArgs *iface )
{
    struct manipulation_updated_event_args *impl = impl_from_IManipulationUpdatedEventArgs( iface );
    LONG ref = InterlockedDecrement( &impl->ref );
    if (!ref) free( impl );
    return ref;
}

static HRESULT WINAPI updated_args_GetIids( IManipulationUpdatedEventArgs *iface, ULONG *count, IID **iids )
{
    IID *array = CoTaskMemAlloc( sizeof(IID) );
    if (!array) return E_OUTOFMEMORY;
    *array = IID_IManipulationUpdatedEventArgs;
    *count = 1;
    *iids = array;
    return S_OK;
}

static HRESULT WINAPI updated_args_GetRuntimeClassName( IManipulationUpdatedEventArgs *iface, HSTRING *name )
{
    return WindowsCreateString( RuntimeClass_Windows_UI_Input_ManipulationUpdatedEventArgs,
                                lstrlenW( RuntimeClass_Windows_UI_Input_ManipulationUpdatedEventArgs ), name );
}

static HRESULT WINAPI updated_args_GetTrustLevel( IManipulationUpdatedEventArgs *iface, TrustLevel *level )
{
    *level = FullTrust;
    return S_OK;
}

static HRESULT WINAPI updated_args_get_Position( IManipulationUpdatedEventArgs *iface, Point *value )
{
    struct manipulation_updated_event_args *impl = impl_from_IManipulationUpdatedEventArgs( iface );
    if (!value) return E_POINTER;
    *value = impl->position;
    return S_OK;
}

static HRESULT WINAPI updated_args_get_Cumulative( IManipulationUpdatedEventArgs *iface, ManipulationDelta *value )
{
    struct manipulation_updated_event_args *impl = impl_from_IManipulationUpdatedEventArgs( iface );
    if (!value) return E_POINTER;
    *value = impl->cumulative;
    return S_OK;
}

static HRESULT WINAPI updated_args_get_Delta( IManipulationUpdatedEventArgs *iface, ManipulationDelta *value )
{
    struct manipulation_updated_event_args *impl = impl_from_IManipulationUpdatedEventArgs( iface );
    if (!value) return E_POINTER;
    *value = impl->delta;
    return S_OK;
}

static const struct IManipulationUpdatedEventArgsVtbl updated_args_vtbl =
{
    updated_args_QueryInterface,
    updated_args_AddRef,
    updated_args_Release,
    updated_args_GetIids,
    updated_args_GetRuntimeClassName,
    updated_args_GetTrustLevel,
    updated_args_get_Position,
    updated_args_get_Cumulative,
    updated_args_get_Delta,
};

struct manipulation_completed_event_args
{
    IManipulationCompletedEventArgs IManipulationCompletedEventArgs_iface;
    LONG ref;
    Point position;
    ManipulationDelta cumulative;
    ManipulationVelocities velocities;
};

static inline struct manipulation_completed_event_args *impl_from_IManipulationCompletedEventArgs(
        IManipulationCompletedEventArgs *iface )
{
    return CONTAINING_RECORD( iface, struct manipulation_completed_event_args,
                              IManipulationCompletedEventArgs_iface );
}

static HRESULT WINAPI completed_args_QueryInterface( IManipulationCompletedEventArgs *iface, REFIID iid, void **out )
{
    struct manipulation_completed_event_args *impl = impl_from_IManipulationCompletedEventArgs( iface );

    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IManipulationCompletedEventArgs ))
    {
        *out = &impl->IManipulationCompletedEventArgs_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI completed_args_AddRef( IManipulationCompletedEventArgs *iface )
{
    struct manipulation_completed_event_args *impl = impl_from_IManipulationCompletedEventArgs( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI completed_args_Release( IManipulationCompletedEventArgs *iface )
{
    struct manipulation_completed_event_args *impl = impl_from_IManipulationCompletedEventArgs( iface );
    LONG ref = InterlockedDecrement( &impl->ref );
    if (!ref) free( impl );
    return ref;
}

static HRESULT WINAPI completed_args_GetIids( IManipulationCompletedEventArgs *iface, ULONG *count, IID **iids )
{
    IID *array = CoTaskMemAlloc( sizeof(IID) );
    if (!array) return E_OUTOFMEMORY;
    *array = IID_IManipulationCompletedEventArgs;
    *count = 1;
    *iids = array;
    return S_OK;
}

static HRESULT WINAPI completed_args_GetRuntimeClassName( IManipulationCompletedEventArgs *iface, HSTRING *name )
{
    return WindowsCreateString( RuntimeClass_Windows_UI_Input_ManipulationCompletedEventArgs,
                                lstrlenW( RuntimeClass_Windows_UI_Input_ManipulationCompletedEventArgs ), name );
}

static HRESULT WINAPI completed_args_GetTrustLevel( IManipulationCompletedEventArgs *iface, TrustLevel *level )
{
    *level = FullTrust;
    return S_OK;
}

static HRESULT WINAPI completed_args_get_Position( IManipulationCompletedEventArgs *iface, Point *value )
{
    struct manipulation_completed_event_args *impl = impl_from_IManipulationCompletedEventArgs( iface );
    if (!value) return E_POINTER;
    *value = impl->position;
    return S_OK;
}

static HRESULT WINAPI completed_args_get_Cumulative( IManipulationCompletedEventArgs *iface, ManipulationDelta *value )
{
    struct manipulation_completed_event_args *impl = impl_from_IManipulationCompletedEventArgs( iface );
    if (!value) return E_POINTER;
    *value = impl->cumulative;
    return S_OK;
}

static HRESULT WINAPI completed_args_get_Velocities( IManipulationCompletedEventArgs *iface, ManipulationVelocities *value )
{
    struct manipulation_completed_event_args *impl = impl_from_IManipulationCompletedEventArgs( iface );
    if (!value) return E_POINTER;
    *value = impl->velocities;
    return S_OK;
}

static const struct IManipulationCompletedEventArgsVtbl completed_args_vtbl =
{
    completed_args_QueryInterface,
    completed_args_AddRef,
    completed_args_Release,
    completed_args_GetIids,
    completed_args_GetRuntimeClassName,
    completed_args_GetTrustLevel,
    completed_args_get_Position,
    completed_args_get_Cumulative,
    completed_args_get_Velocities,
};

/* IGestureRecognizer */

static HRESULT WINAPI gesturerecognizer_QueryInterface( IGestureRecognizer *iface, REFIID iid, void **out )
{
    struct gesture_recognizer *impl = impl_from_IGestureRecognizer( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IGestureRecognizer ))
    {
        *out = &impl->IGestureRecognizer_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI gesturerecognizer_AddRef( IGestureRecognizer *iface )
{
    struct gesture_recognizer *impl = impl_from_IGestureRecognizer( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p, ref %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI gesturerecognizer_Release( IGestureRecognizer *iface )
{
    struct gesture_recognizer *impl = impl_from_IGestureRecognizer( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    TRACE( "iface %p, ref %lu.\n", iface, ref );

    if (!ref)
    {
        list_free_events( &impl->tapped );
        list_free_events( &impl->right_tapped );
        list_free_events( &impl->holding );
        list_free_events( &impl->manipulation_started );
        list_free_events( &impl->manipulation_updated );
        list_free_events( &impl->manipulation_inertia_starting );
        list_free_events( &impl->manipulation_completed );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI gesturerecognizer_GetIids( IGestureRecognizer *iface, ULONG *count, IID **iids )
{
    IID *array = CoTaskMemAlloc( sizeof(IID) );
    if (!array) return E_OUTOFMEMORY;
    *array = IID_IGestureRecognizer;
    *count = 1;
    *iids = array;
    return S_OK;
}

static HRESULT WINAPI gesturerecognizer_GetRuntimeClassName( IGestureRecognizer *iface, HSTRING *name )
{
    return WindowsCreateString( RuntimeClass_Windows_UI_Input_GestureRecognizer,
                                lstrlenW( RuntimeClass_Windows_UI_Input_GestureRecognizer ), name );
}

static HRESULT WINAPI gesturerecognizer_GetTrustLevel( IGestureRecognizer *iface, TrustLevel *level )
{
    *level = FullTrust;
    return S_OK;
}

static HRESULT WINAPI gesturerecognizer_get_GestureSettings( IGestureRecognizer *iface, enum GestureSettings *value )
{
    struct gesture_recognizer *impl = impl_from_IGestureRecognizer( iface );
    TRACE( "iface %p, value %p.\n", iface, value );
    if (!value) return E_POINTER;
    *value = impl->settings;
    return S_OK;
}

static HRESULT WINAPI gesturerecognizer_put_GestureSettings( IGestureRecognizer *iface, enum GestureSettings value )
{
    struct gesture_recognizer *impl = impl_from_IGestureRecognizer( iface );
    TRACE( "iface %p, value %#lx.\n", iface, value );
    impl->settings = value;
    return S_OK;
}

static HRESULT WINAPI gesturerecognizer_get_IsActive( IGestureRecognizer *iface, boolean *value )
{
    struct gesture_recognizer *impl = impl_from_IGestureRecognizer( iface );
    TRACE( "iface %p, value %p.\n", iface, value );
    if (!value) return E_POINTER;
    *value = impl->is_active;
    return S_OK;
}

static HRESULT WINAPI gesturerecognizer_get_IsInertial( IGestureRecognizer *iface, boolean *value )
{
    struct gesture_recognizer *impl = impl_from_IGestureRecognizer( iface );
    TRACE( "iface %p, value %p.\n", iface, value );
    if (!value) return E_POINTER;
    *value = impl->is_inertial;
    return S_OK;
}

static HRESULT WINAPI gesturerecognizer_ProcessDownEvent( IGestureRecognizer *iface, IPointerPoint *value )
{
    struct gesture_recognizer *impl = impl_from_IGestureRecognizer( iface );
    Point pos = { 0, 0 };
    HRESULT hr;

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    if (FAILED( hr = IPointerPoint_get_Position( value, &pos ) )) return hr;

    impl->is_active = TRUE;
    impl->is_inertial = FALSE;
    impl->in_manipulation = FALSE;
    impl->start_pos = pos;
    impl->last_pos = pos;
    impl->delta_x = 0;
    impl->delta_y = 0;
    return S_OK;
}

static HRESULT WINAPI gesturerecognizer_ProcessMoveEvents( IGestureRecognizer *iface,
                                                           PointerPointVector *value )
{
    struct gesture_recognizer *impl = impl_from_IGestureRecognizer( iface );
    UINT32 i, size = 0;
    HRESULT hr;

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    if (FAILED( hr = IVector_PointerPoint_GetAt( value, 0, NULL ) ) ||
        FAILED( hr = IVector_PointerPoint_get_Size( value, &size ) )) return hr;

    for (i = 0; i < size; ++i)
    {
        struct manipulation_started_event_args *started_args;
        struct manipulation_updated_event_args *updated_args;
        ManipulationDelta cumulative = { { 0, 0 }, 1, 0, 0 };
        ManipulationDelta delta = { { 0, 0 }, 1, 0, 0 };
        Point pos = { 0, 0 };
        IPointerPoint *point;
        float dx, dy;

        if (FAILED( hr = IVector_PointerPoint_GetAt( value, i, &point ) )) return hr;
        hr = IPointerPoint_get_Position( point, &pos );
        IPointerPoint_Release( point );
        if (FAILED( hr )) return hr;

        impl->delta_x = pos.X - impl->start_pos.X;
        impl->delta_y = pos.Y - impl->start_pos.Y;
        dx = impl->delta_x;
        dy = impl->delta_y;

        if (!(impl->settings & (GestureSettings_ManipulationTranslateX | GestureSettings_ManipulationTranslateY)))
            continue;
        if (fabsf( dx ) <= MANIPULATION_THRESHOLD && fabsf( dy ) <= MANIPULATION_THRESHOLD)
            continue;

        if (!(impl->settings & GestureSettings_ManipulationTranslateX)) dx = 0;
        if (!(impl->settings & GestureSettings_ManipulationTranslateY)) dy = 0;

        if (!impl->in_manipulation)
        {
            if (!(started_args = calloc( 1, sizeof(*started_args) ))) return E_OUTOFMEMORY;
            started_args->IManipulationStartedEventArgs_iface.lpVtbl = &started_args_vtbl;
            started_args->ref = 1;
            started_args->position = impl->start_pos;
            FIRE_EVENT( &impl->manipulation_started,
                        ManipulationStartedEventHandler,
                        &started_args->IManipulationStartedEventArgs_iface );
            IInspectable_Release( (IInspectable *)&started_args->IManipulationStartedEventArgs_iface );
            impl->in_manipulation = TRUE;
            impl->last_pos = impl->start_pos;
        }

        delta.Translation.X = pos.X - impl->last_pos.X;
        delta.Translation.Y = pos.Y - impl->last_pos.Y;
        cumulative.Translation.X = dx;
        cumulative.Translation.Y = dy;

        if (!(updated_args = calloc( 1, sizeof(*updated_args) ))) return E_OUTOFMEMORY;
        updated_args->IManipulationUpdatedEventArgs_iface.lpVtbl = &updated_args_vtbl;
        updated_args->ref = 1;
        updated_args->position = pos;
        updated_args->cumulative = cumulative;
        updated_args->delta = delta;
        FIRE_EVENT( &impl->manipulation_updated,
                    ManipulationUpdatedEventHandler,
                    &updated_args->IManipulationUpdatedEventArgs_iface );
        IInspectable_Release( (IInspectable *)&updated_args->IManipulationUpdatedEventArgs_iface );

        impl->last_pos = pos;
    }

    return S_OK;
}

static HRESULT WINAPI gesturerecognizer_ProcessUpEvent( IGestureRecognizer *iface, IPointerPoint *value )
{
    struct gesture_recognizer *impl = impl_from_IGestureRecognizer( iface );
    struct manipulation_completed_event_args *completed_args;
    struct tapped_event_args *tapped_args;
    ManipulationDelta cumulative = { { 0, 0 }, 1, 0, 0 };
    Point pos = { 0, 0 };
    HRESULT hr;

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    if (FAILED( hr = IPointerPoint_get_Position( value, &pos ) )) return hr;

    impl->delta_x = pos.X - impl->start_pos.X;
    impl->delta_y = pos.Y - impl->start_pos.Y;

    if ((impl->settings & GestureSettings_Tap) &&
        fabsf( impl->delta_x ) <= MANIPULATION_THRESHOLD &&
        fabsf( impl->delta_y ) <= MANIPULATION_THRESHOLD)
    {
        if (!(tapped_args = calloc( 1, sizeof(*tapped_args) ))) return E_OUTOFMEMORY;
        tapped_args->ITappedEventArgs_iface.lpVtbl = &tapped_args_vtbl;
        tapped_args->ref = 1;
        tapped_args->position = pos;
        tapped_args->tap_count = 1;
        FIRE_EVENT( &impl->tapped,
                    TappedEventHandler,
                    &tapped_args->ITappedEventArgs_iface );
        IInspectable_Release( (IInspectable *)&tapped_args->ITappedEventArgs_iface );
    }

    if (impl->in_manipulation)
    {
        if (!(completed_args = calloc( 1, sizeof(*completed_args) ))) return E_OUTOFMEMORY;
        completed_args->IManipulationCompletedEventArgs_iface.lpVtbl = &completed_args_vtbl;
        completed_args->ref = 1;
        completed_args->position = pos;
        cumulative.Translation.X = impl->delta_x;
        cumulative.Translation.Y = impl->delta_y;
        completed_args->cumulative = cumulative;
        FIRE_EVENT( &impl->manipulation_completed,
                    ManipulationCompletedEventHandler,
                    &completed_args->IManipulationCompletedEventArgs_iface );
        IInspectable_Release( (IInspectable *)&completed_args->IManipulationCompletedEventArgs_iface );
    }

    impl->is_active = FALSE;
    impl->in_manipulation = FALSE;
    return S_OK;
}

static HRESULT WINAPI gesturerecognizer_CompleteGesture( IGestureRecognizer *iface )
{
    struct gesture_recognizer *impl = impl_from_IGestureRecognizer( iface );
    TRACE( "iface %p.\n", iface );
    impl->is_active = FALSE;
    impl->in_manipulation = FALSE;
    return S_OK;
}

#define DEFINE_GESTURERECOGNIZER_EVENT( name, handler_type )                                       \
    static HRESULT WINAPI gesturerecognizer_add_##name( IGestureRecognizer *iface,                  \
            handler_type *handler, EventRegistrationToken *token )                                 \
    {                                                                                              \
        struct gesture_recognizer *impl = impl_from_IGestureRecognizer( iface );                   \
        TRACE( "iface %p, handler %p, token %p.\n", iface, handler, token );                       \
        if (!handler || !token) return E_POINTER;                                                  \
        list_add_event( &impl->name, (IUnknown *)handler, token, impl );                           \
        return S_OK;                                                                               \
    }                                                                                              \
    static HRESULT WINAPI gesturerecognizer_remove_##name( IGestureRecognizer *iface,              \
            EventRegistrationToken token )                                                         \
    {                                                                                              \
        struct gesture_recognizer *impl = impl_from_IGestureRecognizer( iface );                   \
        TRACE( "iface %p, token %#I64x.\n", iface, token.value );                                  \
        list_remove_event( &impl->name, token );                                                   \
        return S_OK;                                                                               \
    }

DEFINE_GESTURERECOGNIZER_EVENT( tapped, TappedEventHandler )
DEFINE_GESTURERECOGNIZER_EVENT( right_tapped, TappedEventHandler )
DEFINE_GESTURERECOGNIZER_EVENT( holding, HoldingEventHandler )
DEFINE_GESTURERECOGNIZER_EVENT( manipulation_started, ManipulationStartedEventHandler )
DEFINE_GESTURERECOGNIZER_EVENT( manipulation_updated, ManipulationUpdatedEventHandler )
DEFINE_GESTURERECOGNIZER_EVENT( manipulation_inertia_starting, ManipulationInertiaStartingEventHandler )
DEFINE_GESTURERECOGNIZER_EVENT( manipulation_completed, ManipulationCompletedEventHandler )

static const struct IGestureRecognizerVtbl gesturerecognizer_vtbl =
{
    gesturerecognizer_QueryInterface,
    gesturerecognizer_AddRef,
    gesturerecognizer_Release,
    /* IInspectable methods */
    gesturerecognizer_GetIids,
    gesturerecognizer_GetRuntimeClassName,
    gesturerecognizer_GetTrustLevel,
    /* IGestureRecognizer methods */
    gesturerecognizer_get_GestureSettings,
    gesturerecognizer_put_GestureSettings,
    gesturerecognizer_get_IsActive,
    gesturerecognizer_get_IsInertial,
    gesturerecognizer_ProcessDownEvent,
    gesturerecognizer_ProcessMoveEvents,
    gesturerecognizer_ProcessUpEvent,
    gesturerecognizer_CompleteGesture,
    gesturerecognizer_add_tapped,
    gesturerecognizer_remove_tapped,
    gesturerecognizer_add_right_tapped,
    gesturerecognizer_remove_right_tapped,
    gesturerecognizer_add_holding,
    gesturerecognizer_remove_holding,
    gesturerecognizer_add_manipulation_started,
    gesturerecognizer_remove_manipulation_started,
    gesturerecognizer_add_manipulation_updated,
    gesturerecognizer_remove_manipulation_updated,
    gesturerecognizer_add_manipulation_inertia_starting,
    gesturerecognizer_remove_manipulation_inertia_starting,
    gesturerecognizer_add_manipulation_completed,
    gesturerecognizer_remove_manipulation_completed,
};

/* Activation factory */

struct gesturerecognizer_factory
{
    IActivationFactory IActivationFactory_iface;
    LONG ref;
};

static inline struct gesturerecognizer_factory *impl_from_IActivationFactory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct gesturerecognizer_factory, IActivationFactory_iface );
}

static HRESULT WINAPI factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct gesturerecognizer_factory *impl = impl_from_IActivationFactory( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = &impl->IActivationFactory_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI factory_AddRef( IActivationFactory *iface )
{
    struct gesturerecognizer_factory *impl = impl_from_IActivationFactory( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI factory_Release( IActivationFactory *iface )
{
    struct gesturerecognizer_factory *impl = impl_from_IActivationFactory( iface );
    return InterlockedDecrement( &impl->ref );
}

static HRESULT WINAPI factory_GetIids( IActivationFactory *iface, ULONG *count, IID **iids )
{
    *count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *name )
{
    return WindowsCreateString( RuntimeClass_Windows_UI_Input_GestureRecognizer,
                                lstrlenW( RuntimeClass_Windows_UI_Input_GestureRecognizer ), name );
}

static HRESULT WINAPI factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *level )
{
    *level = FullTrust;
    return S_OK;
}

static HRESULT WINAPI factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    struct gesture_recognizer *impl;

    TRACE( "iface %p, instance %p.\n", iface, instance );

    if (!instance) return E_POINTER;
    if (!(impl = calloc( 1, sizeof(*impl) )))
    {
        *instance = NULL;
        return E_OUTOFMEMORY;
    }

    impl->IGestureRecognizer_iface.lpVtbl = &gesturerecognizer_vtbl;
    impl->ref = 1;
    impl->settings = GestureSettings_Tap;
    impl->next_token = 0;
    list_init( &impl->tapped );
    list_init( &impl->right_tapped );
    list_init( &impl->holding );
    list_init( &impl->manipulation_started );
    list_init( &impl->manipulation_updated );
    list_init( &impl->manipulation_inertia_starting );
    list_init( &impl->manipulation_completed );

    *instance = (IInspectable *)&impl->IGestureRecognizer_iface;
    return S_OK;
}

static const struct IActivationFactoryVtbl factory_vtbl =
{
    factory_QueryInterface,
    factory_AddRef,
    factory_Release,
    /* IInspectable methods */
    factory_GetIids,
    factory_GetRuntimeClassName,
    factory_GetTrustLevel,
    /* IActivationFactory methods */
    factory_ActivateInstance,
};

static struct gesturerecognizer_factory factory_obj =
{
    {&factory_vtbl},
    1,
};

IActivationFactory *gesturerecognizer_factory = &factory_obj.IActivationFactory_iface;
