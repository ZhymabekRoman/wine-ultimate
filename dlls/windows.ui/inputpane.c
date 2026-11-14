#include <stdlib.h>
#include <string.h>

#include "private.h"
#include "objbase.h"
#include "initguid.h"
#include "inputpaneinterop.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(ui);

/* Emulated soft keyboard strip, in device independent pixels: docked at the
 * bottom of the work area, the strip height is what the input pane occludes. */
#define KEYBOARD_HEIGHT 270.f

struct event_entry
{
    LONG64 token;
    ITypedEventHandler_InputPane_InputPaneVisibilityEventArgs *handler;
};

struct event_list
{
    struct event_entry *entries;
    ULONG count;
    ULONG size;
};

struct inputpane
{
    IInputPane IInputPane_iface;
    IInputPane2 IInputPane2_iface;
    IInputPaneControl IInputPaneControl_iface;
    CRITICAL_SECTION cs;
    boolean visible;
    struct event_list showing;
    struct event_list hiding;
    LONG ref;
};

static inline struct inputpane *impl_from_IInputPane( IInputPane *iface )
{
    return CONTAINING_RECORD( iface, struct inputpane, IInputPane_iface );
}

struct inputpane_visibility_args
{
    IInputPaneVisibilityEventArgs IInputPaneVisibilityEventArgs_iface;
    Rect rect;
    boolean ensured;
    LONG ref;
};

static inline struct inputpane_visibility_args *impl_from_IInputPaneVisibilityEventArgs(
        IInputPaneVisibilityEventArgs *iface )
{
    return CONTAINING_RECORD( iface, struct inputpane_visibility_args, IInputPaneVisibilityEventArgs_iface );
}

static HRESULT WINAPI visibility_args_QueryInterface( IInputPaneVisibilityEventArgs *iface, REFIID iid, void **out )
{
    struct inputpane_visibility_args *impl = impl_from_IInputPaneVisibilityEventArgs( iface );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IInputPaneVisibilityEventArgs ))
    {
        *out = &impl->IInputPaneVisibilityEventArgs_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI visibility_args_AddRef( IInputPaneVisibilityEventArgs *iface )
{
    struct inputpane_visibility_args *impl = impl_from_IInputPaneVisibilityEventArgs( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI visibility_args_Release( IInputPaneVisibilityEventArgs *iface )
{
    struct inputpane_visibility_args *impl = impl_from_IInputPaneVisibilityEventArgs( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref) free( impl );
    return ref;
}

static HRESULT WINAPI visibility_args_GetIids( IInputPaneVisibilityEventArgs *iface, ULONG *iid_count, IID **iids )
{
    *iid_count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI visibility_args_GetRuntimeClassName( IInputPaneVisibilityEventArgs *iface, HSTRING *class_name )
{
    return WindowsCreateString( RuntimeClass_Windows_UI_ViewManagement_InputPaneVisibilityEventArgs,
                                lstrlenW( RuntimeClass_Windows_UI_ViewManagement_InputPaneVisibilityEventArgs ),
                                class_name );
}

static HRESULT WINAPI visibility_args_GetTrustLevel( IInputPaneVisibilityEventArgs *iface, TrustLevel *trust_level )
{
    *trust_level = FullTrust;
    return S_OK;
}

static HRESULT WINAPI visibility_args_get_OccludedRect( IInputPaneVisibilityEventArgs *iface, Rect *value )
{
    struct inputpane_visibility_args *impl = impl_from_IInputPaneVisibilityEventArgs( iface );
    *value = impl->rect;
    return S_OK;
}

static HRESULT WINAPI visibility_args_put_EnsuredFocusedElementInView( IInputPaneVisibilityEventArgs *iface, boolean value )
{
    struct inputpane_visibility_args *impl = impl_from_IInputPaneVisibilityEventArgs( iface );
    impl->ensured = value;
    return S_OK;
}

static HRESULT WINAPI visibility_args_get_EnsuredFocusedElementInView( IInputPaneVisibilityEventArgs *iface, boolean *value )
{
    struct inputpane_visibility_args *impl = impl_from_IInputPaneVisibilityEventArgs( iface );
    *value = impl->ensured;
    return S_OK;
}

static const struct IInputPaneVisibilityEventArgsVtbl visibility_args_vtbl =
{
    visibility_args_QueryInterface,
    visibility_args_AddRef,
    visibility_args_Release,
    visibility_args_GetIids,
    visibility_args_GetRuntimeClassName,
    visibility_args_GetTrustLevel,
    visibility_args_get_OccludedRect,
    visibility_args_put_EnsuredFocusedElementInView,
    visibility_args_get_EnsuredFocusedElementInView,
};

static HRESULT visibility_args_create( const Rect *rect, struct inputpane_visibility_args **args )
{
    struct inputpane_visibility_args *impl = calloc( 1, sizeof(*impl) );

    if (!impl) return E_OUTOFMEMORY;
    impl->IInputPaneVisibilityEventArgs_iface.lpVtbl = &visibility_args_vtbl;
    impl->rect = *rect;
    impl->ensured = TRUE;
    impl->ref = 1;
    *args = impl;
    return S_OK;
}

static void inputpane_occluded_rect( const struct inputpane *impl, Rect *rect )
{
    static const float width = 1280.f;
    static const float height = 720.f;

    rect->X = 0;
    rect->Width = width;
    if (impl->visible)
    {
        rect->Y = height - KEYBOARD_HEIGHT;
        rect->Height = KEYBOARD_HEIGHT;
    }
    else
    {
        rect->Y = height;
        rect->Height = 0;
    }
}

static void event_list_add( struct event_list *list, LONG64 token,
        ITypedEventHandler_InputPane_InputPaneVisibilityEventArgs *handler )
{
    if (list->count == list->size)
    {
        ULONG size = list->size ? list->size * 2 : 4;
        struct event_entry *entries = realloc( list->entries, size * sizeof(*entries) );

        if (!entries) return;
        list->entries = entries;
        list->size = size;
    }
    list->entries[list->count].token = token;
    list->entries[list->count].handler = handler;
    list->count++;
    IUnknown_AddRef( (IUnknown *)handler );
}

static HRESULT event_list_remove( struct event_list *list, LONG64 token )
{
    ULONG i;

    for (i = 0; i < list->count; i++)
    {
        if (list->entries[i].token != token) continue;
        IUnknown_Release( (IUnknown *)list->entries[i].handler );
        list->count--;
        memmove( &list->entries[i], &list->entries[i + 1], (list->count - i) * sizeof(*list->entries) );
        return S_OK;
    }
    return E_INVALIDARG;
}

static void event_list_fire( struct event_list *list, struct inputpane *impl, const Rect *rect )
{
    struct inputpane_visibility_args *args;
    ULONG i;

    if (!list->count) return;
    if (FAILED( visibility_args_create( rect, &args ) )) return;

    for (i = 0; i < list->count; i++)
        ITypedEventHandler_InputPane_InputPaneVisibilityEventArgs_Invoke( list->entries[i].handler,
                &impl->IInputPane_iface, &args->IInputPaneVisibilityEventArgs_iface );

    IInputPaneVisibilityEventArgs_Release( &args->IInputPaneVisibilityEventArgs_iface );
}

static void inputpane_set_visible( struct inputpane *impl, boolean visible )
{
    Rect rect;

    if (impl->visible == visible) return;

    if (visible)
    {
        /* Showing fires before the pane becomes visible, with the future rect. */
        impl->visible = TRUE;
        inputpane_occluded_rect( impl, &rect );
        event_list_fire( &impl->showing, impl, &rect );
    }
    else
    {
        impl->visible = FALSE;
        inputpane_occluded_rect( impl, &rect );
        /* Hiding fires after the pane is gone, with the emptied rect. */
        event_list_fire( &impl->hiding, impl, &rect );
    }
}

static HRESULT WINAPI inputpane_QueryInterface( IInputPane *iface, REFIID iid, void **out )
{
    struct inputpane *impl = impl_from_IInputPane( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    *out = NULL;

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IInputPane ))
    {
        *out = &impl->IInputPane_iface;
    }
    else if (IsEqualGUID( iid, &IID_IInputPane2 ))
    {
        *out = &impl->IInputPane2_iface;
    }
    else if (IsEqualGUID( iid, &IID_IInputPaneControl ))
    {
        *out = &impl->IInputPaneControl_iface;
    }

    if (!*out)
    {
        TRACE( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
        return E_NOINTERFACE;
    }

    IUnknown_AddRef( (IUnknown *)*out );
    return S_OK;
}

static ULONG WINAPI inputpane_AddRef( IInputPane *iface )
{
    struct inputpane *impl = impl_from_IInputPane( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p, ref %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI inputpane_Release( IInputPane *iface )
{
    struct inputpane *impl = impl_from_IInputPane( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    TRACE( "iface %p, ref %lu.\n", iface, ref );

    if (!ref)
    {
        ULONG i;
        for (i = 0; i < impl->showing.count; i++)
            IUnknown_Release( (IUnknown *)impl->showing.entries[i].handler );
        for (i = 0; i < impl->hiding.count; i++)
            IUnknown_Release( (IUnknown *)impl->hiding.entries[i].handler );
        free( impl->showing.entries );
        free( impl->hiding.entries );
        DeleteCriticalSection( &impl->cs );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI inputpane_GetIids( IInputPane *iface, ULONG *iid_count, IID **iids )
{
    IID iid_array[] =
    {
        IID_IInputPane,
        IID_IInputPane2,
        IID_IInputPaneControl,
    };
    IID *array = CoTaskMemAlloc( sizeof(iid_array) );

    if (!array) return E_OUTOFMEMORY;
    memcpy( array, iid_array, sizeof(iid_array) );
    *iid_count = ARRAY_SIZE(iid_array);
    *iids = array;
    return S_OK;
}

static HRESULT WINAPI inputpane_GetRuntimeClassName( IInputPane *iface, HSTRING *class_name )
{
    return WindowsCreateString( RuntimeClass_Windows_UI_ViewManagement_InputPane,
                                lstrlenW( RuntimeClass_Windows_UI_ViewManagement_InputPane ), class_name );
}

static HRESULT WINAPI inputpane_GetTrustLevel( IInputPane *iface, TrustLevel *trust_level )
{
    *trust_level = FullTrust;
    return S_OK;
}

static HRESULT WINAPI inputpane_add_Showing( IInputPane *iface, ITypedEventHandler_InputPane_InputPaneVisibilityEventArgs *handler, EventRegistrationToken *token )
{
    struct inputpane *impl = impl_from_IInputPane( iface );
    static LONG64 next_token;

    TRACE( "iface %p, handler %p, token %p.\n", iface, handler, token );

    if (!handler || !token) return E_INVALIDARG;

    EnterCriticalSection( &impl->cs );
    token->value = InterlockedIncrement64( &next_token );
    event_list_add( &impl->showing, token->value, handler );
    LeaveCriticalSection( &impl->cs );
    return S_OK;
}

static HRESULT WINAPI inputpane_remove_Showing( IInputPane *iface, EventRegistrationToken token )
{
    struct inputpane *impl = impl_from_IInputPane( iface );
    HRESULT hr;

    TRACE( "iface %p, token %#I64x.\n", iface, token.value );

    EnterCriticalSection( &impl->cs );
    hr = event_list_remove( &impl->showing, token.value );
    LeaveCriticalSection( &impl->cs );
    return hr;
}

static HRESULT WINAPI inputpane_add_Hiding( IInputPane *iface, ITypedEventHandler_InputPane_InputPaneVisibilityEventArgs *handler, EventRegistrationToken *token )
{
    struct inputpane *impl = impl_from_IInputPane( iface );
    static LONG64 next_token;

    TRACE( "iface %p, handler %p, token %p.\n", iface, handler, token );

    if (!handler || !token) return E_INVALIDARG;

    EnterCriticalSection( &impl->cs );
    token->value = InterlockedIncrement64( &next_token );
    event_list_add( &impl->hiding, token->value, handler );
    LeaveCriticalSection( &impl->cs );
    return S_OK;
}

static HRESULT WINAPI inputpane_remove_Hiding( IInputPane *iface, EventRegistrationToken token )
{
    struct inputpane *impl = impl_from_IInputPane( iface );
    HRESULT hr;

    TRACE( "iface %p, token %#I64x.\n", iface, token.value );

    EnterCriticalSection( &impl->cs );
    hr = event_list_remove( &impl->hiding, token.value );
    LeaveCriticalSection( &impl->cs );
    return hr;
}

static HRESULT WINAPI inputpane_OccludedRect( IInputPane *iface, Rect *value )
{
    struct inputpane *impl = impl_from_IInputPane( iface );

    EnterCriticalSection( &impl->cs );
    inputpane_occluded_rect( impl, value );
    LeaveCriticalSection( &impl->cs );
    return S_OK;
}

static const struct IInputPaneVtbl inputpane_vtbl =
{
    inputpane_QueryInterface,
    inputpane_AddRef,
    inputpane_Release,

    inputpane_GetIids,
    inputpane_GetRuntimeClassName,
    inputpane_GetTrustLevel,

    inputpane_add_Showing,
    inputpane_remove_Showing,
    inputpane_add_Hiding,
    inputpane_remove_Hiding,
    inputpane_OccludedRect,
};

DEFINE_IINSPECTABLE( inputpane2, IInputPane2, struct inputpane, IInputPane_iface );

static HRESULT WINAPI inputpane2_TryShow( IInputPane2 *iface, boolean *result )
{
    struct inputpane *impl = impl_from_IInputPane2( iface );

    EnterCriticalSection( &impl->cs );
    inputpane_set_visible( impl, TRUE );
    LeaveCriticalSection( &impl->cs );
    *result = TRUE;
    return S_OK;
}

static HRESULT WINAPI inputpane2_TryHide( IInputPane2 *iface, boolean *result )
{
    struct inputpane *impl = impl_from_IInputPane2( iface );

    EnterCriticalSection( &impl->cs );
    inputpane_set_visible( impl, FALSE );
    LeaveCriticalSection( &impl->cs );
    *result = TRUE;
    return S_OK;
}

static const struct IInputPane2Vtbl inputpane2_vtbl =
{
    inputpane2_QueryInterface,
    inputpane2_AddRef,
    inputpane2_Release,

    inputpane2_GetIids,
    inputpane2_GetRuntimeClassName,
    inputpane2_GetTrustLevel,

    inputpane2_TryShow,
    inputpane2_TryHide,
};

DEFINE_IINSPECTABLE( inputpane_control, IInputPaneControl, struct inputpane, IInputPane_iface );

static HRESULT WINAPI inputpane_control_get_Visible( IInputPaneControl *iface, boolean *value )
{
    struct inputpane *impl = impl_from_IInputPaneControl( iface );

    EnterCriticalSection( &impl->cs );
    *value = impl->visible;
    LeaveCriticalSection( &impl->cs );
    return S_OK;
}

static HRESULT WINAPI inputpane_control_put_Visible( IInputPaneControl *iface, boolean value )
{
    struct inputpane *impl = impl_from_IInputPaneControl( iface );

    EnterCriticalSection( &impl->cs );
    inputpane_set_visible( impl, value );
    LeaveCriticalSection( &impl->cs );
    return S_OK;
}

static const struct IInputPaneControlVtbl inputpane_control_vtbl =
{
    inputpane_control_QueryInterface,
    inputpane_control_AddRef,
    inputpane_control_Release,

    inputpane_control_GetIids,
    inputpane_control_GetRuntimeClassName,
    inputpane_control_GetTrustLevel,

    inputpane_control_get_Visible,
    inputpane_control_put_Visible,
};

static struct inputpane *inputpane_create( void )
{
    struct inputpane *impl = calloc( 1, sizeof(*impl) );

    if (!impl) return NULL;
    InitializeCriticalSection( &impl->cs );
    impl->IInputPane_iface.lpVtbl = &inputpane_vtbl;
    impl->IInputPane2_iface.lpVtbl = &inputpane2_vtbl;
    impl->IInputPaneControl_iface.lpVtbl = &inputpane_control_vtbl;
    impl->visible = FALSE;
    impl->ref = 1;
    return impl;
}

/* Per-view singleton: every GetForCurrentView()/GetForWindow()/GetForUIContext()
 * caller must observe (and fire) the same instance, event registrations made
 * through one lookup have to be visible to the next. */
static struct inputpane *inputpane_instance;

static BOOL WINAPI inputpane_init_singleton( INIT_ONCE *once, void *param, void **context )
{
    struct inputpane *impl = inputpane_create();

    if (!impl) return FALSE;
    inputpane_instance = impl; /* kept alive with ref 1 for the process lifetime */
    return TRUE;
}

static struct inputpane *inputpane_singleton( void )
{
    static INIT_ONCE init_once = INIT_ONCE_STATIC_INIT;

    if (!InitOnceExecuteOnce( &init_once, inputpane_init_singleton, NULL, NULL )) return NULL;
    IInputPane_AddRef( &inputpane_instance->IInputPane_iface );
    return inputpane_instance;
}

struct inputpane_statics
{
    IActivationFactory IActivationFactory_iface;
    IInputPaneInterop  IInputPaneInterop_iface;
    IInputPaneStatics  IInputPaneStatics_iface;
    IInputPaneStatics2 IInputPaneStatics2_iface;
    IInputPane IInputPane_iface;
    LONG ref;
};

static inline struct inputpane_statics *impl_from_IActivationFactory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct inputpane_statics, IActivationFactory_iface );
}

static HRESULT WINAPI factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct inputpane_statics *impl = impl_from_IActivationFactory( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = &impl->IActivationFactory_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }
    else if (IsEqualGUID( iid, &IID_IInputPaneInterop ))
    {
        *out = &impl->IInputPaneInterop_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }
    else if (IsEqualGUID( iid, &IID_IInputPaneStatics ))
    {
        *out = &impl->IInputPaneStatics_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }
    else if (IsEqualGUID( iid, &IID_IInputPaneStatics2 ))
    {
        *out = &impl->IInputPaneStatics2_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IInputPane ))
    {
        *out = &impl->IInputPane_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI factory_AddRef( IActivationFactory *iface )
{
    struct inputpane_statics *impl = impl_from_IActivationFactory( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p, ref %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI factory_Release( IActivationFactory *iface )
{
    struct inputpane_statics *impl = impl_from_IActivationFactory( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    TRACE( "iface %p, ref %lu.\n", iface, ref );
    return ref;
}

static HRESULT WINAPI factory_GetIids( IActivationFactory *iface, ULONG *iid_count, IID **iids )
{
    IID factory_iids[] =
    {
        IID_IActivationFactory,
        IID_IInputPaneStatics,
        IID_IInputPaneInterop,
        IID_IInputPaneStatics2,
        IID_IInputPane,
    };
    IID *array = CoTaskMemAlloc( sizeof(factory_iids) );

    if (!array) return E_OUTOFMEMORY;
    memcpy( array, factory_iids, sizeof(factory_iids) );
    *iid_count = ARRAY_SIZE(factory_iids);
    *iids = array;
    return S_OK;
}

static HRESULT WINAPI factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *class_name )
{
    return WindowsCreateString( RuntimeClass_Windows_UI_ViewManagement_InputPane, lstrlenW( RuntimeClass_Windows_UI_ViewManagement_InputPane ), class_name );
}

static HRESULT WINAPI factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *trust_level )
{
    *trust_level = FullTrust;
    return S_OK;
}

static HRESULT WINAPI factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    struct inputpane *impl;

    TRACE( "iface %p, instance %p.\n", iface, instance );

    if (!(impl = inputpane_singleton()))
    {
        *instance = NULL;
        return E_OUTOFMEMORY;
    }

    *instance = (IInspectable *)&impl->IInputPane_iface;
    return S_OK;
}

static const struct IActivationFactoryVtbl factory_vtbl =
{
    factory_QueryInterface,
    factory_AddRef,
    factory_Release,
    factory_GetIids,
    factory_GetRuntimeClassName,
    factory_GetTrustLevel,
    factory_ActivateInstance,
};

DEFINE_IINSPECTABLE( input_pane_interop, IInputPaneInterop, struct inputpane_statics, IActivationFactory_iface );

static HRESULT WINAPI input_pane_interop_GetForWindow( IInputPaneInterop *iface, HWND window, REFIID riid, void **inputpane )
{
    struct inputpane *impl;

    TRACE( "(window %p, riid %s, inputpane %p)\n", window, debugstr_guid( riid ), inputpane );

    if (!(impl = inputpane_singleton())) return E_OUTOFMEMORY;
    return IInputPane_QueryInterface( &impl->IInputPane_iface, riid, inputpane );
}

static const struct IInputPaneInteropVtbl input_pane_interop_vtbl =
{
    input_pane_interop_QueryInterface,
    input_pane_interop_AddRef,
    input_pane_interop_Release,

    input_pane_interop_GetIids,
    input_pane_interop_GetRuntimeClassName,
    input_pane_interop_GetTrustLevel,

    input_pane_interop_GetForWindow,
};

DEFINE_IINSPECTABLE( input_pane_statics, IInputPaneStatics, struct inputpane_statics, IActivationFactory_iface );

static HRESULT WINAPI input_pane_statics_GetForCurrentView( IInputPaneStatics *iface, IInputPane **inputpane )
{
    struct inputpane *impl;

    TRACE( "(inputpane %p)\n", inputpane );

    if (!(impl = inputpane_singleton())) return E_OUTOFMEMORY;
    *inputpane = &impl->IInputPane_iface;
    return S_OK;
}

static const struct IInputPaneStaticsVtbl input_pane_statics_vtbl =
{
    input_pane_statics_QueryInterface,
    input_pane_statics_AddRef,
    input_pane_statics_Release,

    input_pane_statics_GetIids,
    input_pane_statics_GetRuntimeClassName,
    input_pane_statics_GetTrustLevel,

    input_pane_statics_GetForCurrentView,
};

DEFINE_IINSPECTABLE( input_pane_statics2, IInputPaneStatics2, struct inputpane_statics, IActivationFactory_iface );

static HRESULT WINAPI input_pane_statics2_GetForUIContext( IInputPaneStatics2 *iface, IUIContext *context, IInputPane **inputpane )
{
    struct inputpane *impl;

    TRACE( "(context %p, inputpane %p)\n", context, inputpane );

    if (!(impl = inputpane_singleton())) return E_OUTOFMEMORY;
    *inputpane = &impl->IInputPane_iface;
    return S_OK;
}

static const struct IInputPaneStatics2Vtbl input_pane_statics2_vtbl =
{
    input_pane_statics2_QueryInterface,
    input_pane_statics2_AddRef,
    input_pane_statics2_Release,

    input_pane_statics2_GetIids,
    input_pane_statics2_GetRuntimeClassName,
    input_pane_statics2_GetTrustLevel,

    input_pane_statics2_GetForUIContext,
};

DEFINE_IINSPECTABLE_( factory_def, IInputPane, struct inputpane_statics, impl_from_IInputPane_factory_def,
                      IInputPane_iface, &impl->IActivationFactory_iface )

static HRESULT WINAPI factory_def_add_Showing( IInputPane *iface, __FITypedEventHandler_2_Windows__CUI__CViewManagement__CInputPane_Windows__CUI__CViewManagement__CInputPaneVisibilityEventArgs *handler, EventRegistrationToken *token )
{
    struct inputpane *impl = inputpane_singleton();
    return impl ? IInputPane_add_Showing( &impl->IInputPane_iface, handler, token ) : E_OUTOFMEMORY;
}

static HRESULT WINAPI factory_def_remove_Showing( IInputPane *iface, EventRegistrationToken token )
{
    struct inputpane *impl = inputpane_singleton();
    return impl ? IInputPane_remove_Showing( &impl->IInputPane_iface, token ) : E_OUTOFMEMORY;
}

static HRESULT WINAPI factory_def_add_Hiding( IInputPane *iface, __FITypedEventHandler_2_Windows__CUI__CViewManagement__CInputPane_Windows__CUI__CViewManagement__CInputPaneVisibilityEventArgs *handler, EventRegistrationToken *token )
{
    struct inputpane *impl = inputpane_singleton();
    return impl ? IInputPane_add_Hiding( &impl->IInputPane_iface, handler, token ) : E_OUTOFMEMORY;
}

static HRESULT WINAPI factory_def_remove_Hiding( IInputPane *iface, EventRegistrationToken token )
{
    struct inputpane *impl = inputpane_singleton();
    return impl ? IInputPane_remove_Hiding( &impl->IInputPane_iface, token ) : E_OUTOFMEMORY;
}

static HRESULT WINAPI factory_def_get_OccludedRect( IInputPane *iface, Rect *value )
{
    struct inputpane *impl = inputpane_singleton();
    return impl ? IInputPane_get_OccludedRect( &impl->IInputPane_iface, value ) : E_OUTOFMEMORY;
}

static const struct IInputPaneVtbl factory_def_vtbl =
{
    factory_def_QueryInterface,
    factory_def_AddRef,
    factory_def_Release,
    factory_def_GetIids,
    factory_def_GetRuntimeClassName,
    factory_def_GetTrustLevel,
    factory_def_add_Showing,
    factory_def_remove_Showing,
    factory_def_add_Hiding,
    factory_def_remove_Hiding,
    factory_def_get_OccludedRect,
};

static struct inputpane_statics inputpane_statics =
{
    {&factory_vtbl},
    {&input_pane_interop_vtbl},
    {&input_pane_statics_vtbl},
    {&input_pane_statics2_vtbl},
    {&factory_def_vtbl}, 1,
};

IActivationFactory *inputpane_factory = &inputpane_statics.IActivationFactory_iface;
