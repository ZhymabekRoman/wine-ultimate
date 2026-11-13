/*
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

#define COBJMACROS
#include <stdarg.h>

#include "windef.h"
#include "winbase.h"
#include "winstring.h"

#include "roapi.h"

#define WIDL_using_Windows_Foundation
#include "windows.foundation.h"
#define WIDL_using_Windows_Foundation_Collections
#include "windows.foundation.collections.h"
#define WINDOWS_FOUNDATION_UNIVERSALAPICONTRACT_VERSION 0xc0000
#define WIDL_using_Windows_UI_Input
#include "windows.ui.input.h"

#include "wine/test.h"

typedef __x_ABI_CWindows_CUI_CInput_CIPointerPoint PointerPoint;
typedef __x_ABI_CWindows_CUI_CInput_CIPointerPointProperties PointerPointProperties;
typedef __x_ABI_CWindows_CDevices_CInput_CIPointerDevice PointerDevice;

typedef HRESULT (WINAPI *get_activation_factory_func)(HSTRING, IUnknown **);

#define TAPPED_HANDLER_T IID___FITypedEventHandler_2_Windows__CUI__CInput__CGestureRecognizer_Windows__CUI__CInput__CTappedEventArgs
#define MANIP_STARTED_HANDLER_T IID___FITypedEventHandler_2_Windows__CUI__CInput__CGestureRecognizer_Windows__CUI__CInput__CManipulationStartedEventArgs
#define MANIP_UPDATED_HANDLER_T IID___FITypedEventHandler_2_Windows__CUI__CInput__CGestureRecognizer_Windows__CUI__CInput__CManipulationUpdatedEventArgs
#define MANIP_COMPLETED_HANDLER_T IID___FITypedEventHandler_2_Windows__CUI__CInput__CGestureRecognizer_Windows__CUI__CInput__CManipulationCompletedEventArgs

/* PointerPoint stub. */

struct pointer_point
{
    IPointerPoint IPointerPoint_iface;
    LONG ref;
    Point pos;
};

static inline struct pointer_point *impl_from_IPointerPoint( IPointerPoint *iface )
{
    return CONTAINING_RECORD( iface, struct pointer_point, IPointerPoint_iface );
}

static HRESULT WINAPI pp_QueryInterface( IPointerPoint *iface, REFIID iid, void **out )
{
    struct pointer_point *impl = impl_from_IPointerPoint( iface );

    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IPointerPoint ))
    {
        *out = iface;
        IPointerPoint_AddRef( iface );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI pp_AddRef( IPointerPoint *iface )
{
    struct pointer_point *impl = impl_from_IPointerPoint( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI pp_Release( IPointerPoint *iface )
{
    struct pointer_point *impl = impl_from_IPointerPoint( iface );
    LONG ref = InterlockedDecrement( &impl->ref );
    if (!ref) free( impl );
    return ref;
}

static HRESULT WINAPI pp_GetIids( IPointerPoint *iface, ULONG *count, IID **iids ) { *count = 0; *iids = NULL; return S_OK; }
static HRESULT WINAPI pp_GetRuntimeClassName( IPointerPoint *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.UI.Input.PointerPoint", 27, name );
}
static HRESULT WINAPI pp_GetTrustLevel( IPointerPoint *iface, TrustLevel *level ) { *level = FullTrust; return S_OK; }
static HRESULT WINAPI pp_PointerDevice( IPointerPoint *iface, PointerDevice **value ) { return S_OK; }
static HRESULT WINAPI pp_Position( IPointerPoint *iface, Point *value )
{
    struct pointer_point *impl = impl_from_IPointerPoint( iface );
    *value = impl->pos;
    return S_OK;
}
static HRESULT WINAPI pp_RawPosition( IPointerPoint *iface, Point *value ) { return pp_Position( iface, value ); }
static HRESULT WINAPI pp_pointer_id( IPointerPoint *iface, UINT32 *value ) { *value = 1; return S_OK; }
static HRESULT WINAPI pp_FrameId( IPointerPoint *iface, UINT32 *value ) { *value = 1; return S_OK; }
static HRESULT WINAPI pp_Timestamp( IPointerPoint *iface, UINT64 *value ) { *value = 0; return S_OK; }
static HRESULT WINAPI pp_IsInContact( IPointerPoint *iface, boolean *value ) { *value = TRUE; return S_OK; }
static HRESULT WINAPI pp_Properties( IPointerPoint *iface, PointerPointProperties **value ) { return S_OK; }

static const struct IPointerPointVtbl pp_vtbl =
{
    pp_QueryInterface, pp_AddRef, pp_Release,
    pp_GetIids, pp_GetRuntimeClassName, pp_GetTrustLevel,
    pp_PointerDevice, pp_Position, pp_RawPosition, pp_pointer_id,
    pp_FrameId, pp_Timestamp, pp_IsInContact, pp_Properties,
};

static PointerPoint *create_pointer_point( FLOAT x, FLOAT y )
{
    struct pointer_point *impl = calloc( 1, sizeof(*impl) );

    impl->IPointerPoint_iface.lpVtbl = &pp_vtbl;
    impl->ref = 1;
    impl->pos.X = x;
    impl->pos.Y = y;
    return (PointerPoint *)&impl->IPointerPoint_iface;
}

/* IVector<PointerPoint *> stub. */

struct pointer_point_vector
{
    __FIVector_1_Windows__CUI__CInput__CPointerPoint IVector_iface;
    LONG ref;
    ULONG count;
    PointerPoint *points[8];
};

static inline struct pointer_point_vector *impl_from_IVector( __FIVector_1_Windows__CUI__CInput__CPointerPoint *iface )
{
    return CONTAINING_RECORD( iface, struct pointer_point_vector, IVector_iface );
}

static HRESULT WINAPI vec_QueryInterface( __FIVector_1_Windows__CUI__CInput__CPointerPoint *iface, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID___FIVector_1_Windows__CUI__CInput__CPointerPoint ))
    {
        *out = iface;
        IUnknown_AddRef( (IUnknown *)iface );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI vec_AddRef( __FIVector_1_Windows__CUI__CInput__CPointerPoint *iface )
{
    struct pointer_point_vector *impl = impl_from_IVector( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI vec_Release( __FIVector_1_Windows__CUI__CInput__CPointerPoint *iface )
{
    struct pointer_point_vector *impl = impl_from_IVector( iface );
    LONG ref = InterlockedDecrement( &impl->ref );
    if (!ref) free( impl );
    return ref;
}

static HRESULT WINAPI vec_GetIids( __FIVector_1_Windows__CUI__CInput__CPointerPoint *iface, ULONG *count, IID **iids ) { *count = 0; *iids = NULL; return S_OK; }
static HRESULT WINAPI vec_GetRuntimeClassName( __FIVector_1_Windows__CUI__CInput__CPointerPoint *iface, HSTRING *name ) { return S_OK; }
static HRESULT WINAPI vec_GetTrustLevel( __FIVector_1_Windows__CUI__CInput__CPointerPoint *iface, TrustLevel *level ) { *level = FullTrust; return S_OK; }

static HRESULT WINAPI vec_GetAt( __FIVector_1_Windows__CUI__CInput__CPointerPoint *iface, UINT32 index, PointerPoint **item )
{
    struct pointer_point_vector *impl = impl_from_IVector( iface );

    if (index >= impl->count) return E_INVALIDARG;
    if (item)
    {
        *item = impl->points[index];
        IUnknown_AddRef( (IUnknown *)*item );
    }
    return S_OK;
}

static HRESULT WINAPI vec_GetSize( __FIVector_1_Windows__CUI__CInput__CPointerPoint *iface, UINT32 *size )
{
    struct pointer_point_vector *impl = impl_from_IVector( iface );
    *size = impl->count;
    return S_OK;
}

static HRESULT WINAPI vec_GetView( __FIVector_1_Windows__CUI__CInput__CPointerPoint *iface, __FIVectorView_1_Windows__CUI__CInput__CPointerPoint **view ) { return S_OK; }
static HRESULT WINAPI vec_IndexOf( __FIVector_1_Windows__CUI__CInput__CPointerPoint *iface, PointerPoint *item, UINT32 *index, boolean *found ) { return S_OK; }
static HRESULT WINAPI vec_SetAt( __FIVector_1_Windows__CUI__CInput__CPointerPoint *iface, UINT32 index, PointerPoint *item ) { return S_OK; }
static HRESULT WINAPI vec_InsertAt( __FIVector_1_Windows__CUI__CInput__CPointerPoint *iface, UINT32 index, PointerPoint *item ) { return S_OK; }
static HRESULT WINAPI vec_RemoveAt( __FIVector_1_Windows__CUI__CInput__CPointerPoint *iface, UINT32 index ) { return S_OK; }
static HRESULT WINAPI vec_RemoveAtEnd( __FIVector_1_Windows__CUI__CInput__CPointerPoint *iface, PointerPoint *item ) { return S_OK; }
static HRESULT WINAPI vec_Clear( __FIVector_1_Windows__CUI__CInput__CPointerPoint *iface ) { return S_OK; }

static const __FIVector_1_Windows__CUI__CInput__CPointerPointVtbl vec_vtbl =
{
    vec_QueryInterface, vec_AddRef, vec_Release,
    vec_GetIids, vec_GetRuntimeClassName, vec_GetTrustLevel,
    vec_GetAt, vec_GetSize, vec_GetView, vec_IndexOf,
    vec_SetAt, vec_InsertAt, vec_RemoveAt, vec_RemoveAtEnd, vec_Clear,
};

static __FIVector_1_Windows__CUI__CInput__CPointerPoint *create_vector( ULONG count, PointerPoint **points )
{
    struct pointer_point_vector *impl = calloc( 1, sizeof(*impl) );
    ULONG i;

    impl->IVector_iface.lpVtbl = &vec_vtbl;
    impl->ref = 1;
    impl->count = count;
    for (i = 0; i < count; ++i)
    {
        impl->points[i] = points[i];
        IUnknown_AddRef( (IUnknown *)points[i] );
    }
    return &impl->IVector_iface;
}

/* Event handler stubs. */

struct tapped_handler
{
    __FITypedEventHandler_2_Windows__CUI__CInput__CGestureRecognizer_Windows__CUI__CInput__CTappedEventArgs Invoke_iface;
    LONG ref;
    ULONG calls;
    Point pos;
};

static inline struct tapped_handler *handler_from_tapped( __FITypedEventHandler_2_Windows__CUI__CInput__CGestureRecognizer_Windows__CUI__CInput__CTappedEventArgs *iface )
{
    return CONTAINING_RECORD( iface, struct tapped_handler, Invoke_iface );
}

static HRESULT WINAPI tapped_handler_QueryInterface( __FITypedEventHandler_2_Windows__CUI__CInput__CGestureRecognizer_Windows__CUI__CInput__CTappedEventArgs *iface, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &TAPPED_HANDLER_T ))
    {
        *out = iface;
        IUnknown_AddRef( (IUnknown *)iface );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI tapped_handler_AddRef( __FITypedEventHandler_2_Windows__CUI__CInput__CGestureRecognizer_Windows__CUI__CInput__CTappedEventArgs *iface )
{
    struct tapped_handler *impl = handler_from_tapped( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI tapped_handler_Release( __FITypedEventHandler_2_Windows__CUI__CInput__CGestureRecognizer_Windows__CUI__CInput__CTappedEventArgs *iface )
{
    struct tapped_handler *impl = handler_from_tapped( iface );
    LONG ref = InterlockedDecrement( &impl->ref );
    if (!ref) free( impl );
    return ref;
}

static HRESULT WINAPI tapped_handler_Invoke( __FITypedEventHandler_2_Windows__CUI__CInput__CGestureRecognizer_Windows__CUI__CInput__CTappedEventArgs *iface,
                                             IGestureRecognizer *sender, ITappedEventArgs *args )
{
    struct tapped_handler *impl = handler_from_tapped( iface );

    impl->calls++;
    if (args)
    {
        Point pos = { -1, -1 };
        ITappedEventArgs_get_Position( args, &pos );
        impl->pos = pos;
        ok( sender != NULL, "Expected sender.\n" );
    }
    return S_OK;
}

static const struct __FITypedEventHandler_2_Windows__CUI__CInput__CGestureRecognizer_Windows__CUI__CInput__CTappedEventArgsVtbl tapped_handler_vtbl =
{
    tapped_handler_QueryInterface, tapped_handler_AddRef, tapped_handler_Release, tapped_handler_Invoke,
};

struct manip_handler
{
    __FITypedEventHandler_2_Windows__CUI__CInput__CGestureRecognizer_Windows__CUI__CInput__CManipulationStartedEventArgs Invoke_iface;
    LONG ref;
    ULONG calls;
};

static inline struct manip_handler *handler_from_manip( __FITypedEventHandler_2_Windows__CUI__CInput__CGestureRecognizer_Windows__CUI__CInput__CManipulationStartedEventArgs *iface )
{
    return CONTAINING_RECORD( iface, struct manip_handler, Invoke_iface );
}

static HRESULT WINAPI manip_handler_QueryInterface( __FITypedEventHandler_2_Windows__CUI__CInput__CGestureRecognizer_Windows__CUI__CInput__CManipulationStartedEventArgs *iface, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &MANIP_STARTED_HANDLER_T ) ||
        IsEqualGUID( iid, &MANIP_UPDATED_HANDLER_T ) ||
        IsEqualGUID( iid, &MANIP_COMPLETED_HANDLER_T ))
    {
        *out = iface;
        IUnknown_AddRef( (IUnknown *)iface );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI manip_handler_AddRef( __FITypedEventHandler_2_Windows__CUI__CInput__CGestureRecognizer_Windows__CUI__CInput__CManipulationStartedEventArgs *iface )
{
    struct manip_handler *impl = handler_from_manip( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI manip_handler_Release( __FITypedEventHandler_2_Windows__CUI__CInput__CGestureRecognizer_Windows__CUI__CInput__CManipulationStartedEventArgs *iface )
{
    struct manip_handler *impl = handler_from_manip( iface );
    LONG ref = InterlockedDecrement( &impl->ref );
    if (!ref) free( impl );
    return ref;
}

static HRESULT WINAPI manip_handler_Invoke( __FITypedEventHandler_2_Windows__CUI__CInput__CGestureRecognizer_Windows__CUI__CInput__CManipulationStartedEventArgs *iface,
                                            IGestureRecognizer *sender, IManipulationStartedEventArgs *args )
{
    struct manip_handler *impl = handler_from_manip( iface );
    impl->calls++;
    return S_OK;
}

static const struct __FITypedEventHandler_2_Windows__CUI__CInput__CGestureRecognizer_Windows__CUI__CInput__CManipulationStartedEventArgsVtbl manip_started_handler_vtbl =
{
    manip_handler_QueryInterface, manip_handler_AddRef, manip_handler_Release, manip_handler_Invoke,
};

static const struct __FITypedEventHandler_2_Windows__CUI__CInput__CGestureRecognizer_Windows__CUI__CInput__CManipulationUpdatedEventArgsVtbl manip_updated_handler_vtbl =
{
    (void *)manip_handler_QueryInterface, (void *)manip_handler_AddRef, (void *)manip_handler_Release, (void *)manip_handler_Invoke,
};

static const struct __FITypedEventHandler_2_Windows__CUI__CInput__CGestureRecognizer_Windows__CUI__CInput__CManipulationCompletedEventArgsVtbl manip_completed_handler_vtbl =
{
    (void *)manip_handler_QueryInterface, (void *)manip_handler_AddRef, (void *)manip_handler_Release, (void *)manip_handler_Invoke,
};

static struct tapped_handler *create_tapped_handler( void )
{
    struct tapped_handler *impl = calloc( 1, sizeof(*impl) );
    impl->Invoke_iface.lpVtbl = &tapped_handler_vtbl;
    impl->ref = 1;
    return impl;
}

static struct manip_handler *create_manip_handler( const void *vtbl )
{
    struct manip_handler *impl = calloc( 1, sizeof(*impl) );
    impl->Invoke_iface.lpVtbl = vtbl;
    impl->ref = 1;
    return impl;
}

static IActivationFactory *get_factory( get_activation_factory_func pGetActivationFactory, const WCHAR *class )
{
    IActivationFactory *factory = NULL;
    HSTRING str;

    ok( SUCCEEDED( WindowsCreateString( class, lstrlenW( class ), &str ) ), "Failed to create string.\n" );
    ok( SUCCEEDED( pGetActivationFactory( str, (IUnknown **)&factory ) ), "Failed to get factory.\n" );
    WindowsDeleteString( str );
    return factory;
}

static void test_gesturerecognizer( get_activation_factory_func pGetActivationFactory )
{
    IGestureRecognizer *recognizer;
    struct tapped_handler *tapped;
    struct manip_handler *started, *updated, *completed;
    IActivationFactory *factory;
    __FIVector_1_Windows__CUI__CInput__CPointerPoint *vector;
    IInspectable *inspectable, *inspectable2;
    EventRegistrationToken token = { 0 };
    PointerPoint *points[1];
    HSTRING str;
    HRESULT hr;

    factory = get_factory( pGetActivationFactory, L"Windows.UI.Input.GestureRecognizer" );

    hr = IActivationFactory_ActivateInstance( factory, &inspectable );
    ok( hr == S_OK, "Unexpected hr %#lx.\n", hr );

    inspectable2 = inspectable;
    hr = IInspectable_QueryInterface( inspectable, &IID_IGestureRecognizer, (void **)&recognizer );
    ok( hr == S_OK, "Unexpected hr %#lx.\n", hr );

    /* Default settings. */
    {
        enum GestureSettings settings;
        hr = IGestureRecognizer_get_GestureSettings( recognizer, &settings );
        ok( hr == S_OK, "Unexpected hr %#lx.\n", hr );
        ok( settings == GestureSettings_Tap, "Got settings %#lx.\n", settings );

        hr = IGestureRecognizer_put_GestureSettings( recognizer, GestureSettings_Tap | GestureSettings_ManipulationTranslateX );
        ok( hr == S_OK, "Unexpected hr %#lx.\n", hr );
        hr = IGestureRecognizer_get_GestureSettings( recognizer, &settings );
        ok( hr == S_OK, "Unexpected hr %#lx.\n", hr );
        ok( settings == (GestureSettings_Tap | GestureSettings_ManipulationTranslateX), "Got settings %#lx.\n", settings );
    }

    hr = IInspectable_GetRuntimeClassName( inspectable, &str );
    ok( hr == S_OK, "Unexpected hr %#lx.\n", hr );
    ok( !wcscmp( WindowsGetStringRawBuffer( str, NULL ), L"Windows.UI.Input.GestureRecognizer" ),
        "Got class %s.\n", debugstr_w( WindowsGetStringRawBuffer( str, NULL ) ) );
    WindowsDeleteString( str );

    /* Tap. */
    tapped = create_tapped_handler();
    hr = IGestureRecognizer_add_Tapped( recognizer, (__FITypedEventHandler_2_Windows__CUI__CInput__CGestureRecognizer_Windows__CUI__CInput__CTappedEventArgs *)tapped, &token );
    ok( hr == S_OK, "Unexpected hr %#lx.\n", hr );
    ok( token.value != 0, "Got token value %I64u.\n", token.value );

    {
        boolean active;
        hr = IGestureRecognizer_get_IsActive( recognizer, &active );
        ok( hr == S_OK && !active, "Unexpected active %d.\n", active );
    }

    points[0] = create_pointer_point( 10, 10 );
    hr = IGestureRecognizer_ProcessDownEvent( recognizer, points[0] );
    ok( hr == S_OK, "Unexpected hr %#lx.\n", hr );

    {
        boolean active;
        hr = IGestureRecognizer_get_IsActive( recognizer, &active );
        ok( hr == S_OK && active, "Expected active after down event.\n" );
    }

    hr = IGestureRecognizer_ProcessUpEvent( recognizer, points[0] );
    ok( hr == S_OK, "Unexpected hr %#lx.\n", hr );
    ok( tapped->calls == 1, "Got %u tapped calls.\n", tapped->calls );
    ok( tapped->pos.X == 10 && tapped->pos.Y == 10, "Got tap position %f,%f.\n", tapped->pos.X, tapped->pos.Y );

    {
        boolean active;
        hr = IGestureRecognizer_get_IsActive( recognizer, &active );
        ok( hr == S_OK && !active, "Expected inactive after up event.\n" );
    }

    /* Remove handler: no more calls. */
    hr = IGestureRecognizer_remove_Tapped( recognizer, token );
    ok( hr == S_OK, "Unexpected hr %#lx.\n", hr );

    hr = IGestureRecognizer_ProcessDownEvent( recognizer, points[0] );
    ok( hr == S_OK, "Unexpected hr %#lx.\n", hr );
    hr = IGestureRecognizer_ProcessUpEvent( recognizer, points[0] );
    ok( hr == S_OK, "Unexpected hr %#lx.\n", hr );
    ok( tapped->calls == 1, "Got %u tapped calls.\n", tapped->calls );

    IUnknown_Release( (IUnknown *)tapped );

    /* Manipulation. */
    hr = IGestureRecognizer_put_GestureSettings( recognizer, GestureSettings_ManipulationTranslateX | GestureSettings_ManipulationTranslateY );
    ok( hr == S_OK, "Unexpected hr %#lx.\n", hr );

    started = create_manip_handler( &manip_started_handler_vtbl );
    updated = create_manip_handler( &manip_updated_handler_vtbl );
    completed = create_manip_handler( &manip_completed_handler_vtbl );

    hr = IGestureRecognizer_add_ManipulationStarted( recognizer, (__FITypedEventHandler_2_Windows__CUI__CInput__CGestureRecognizer_Windows__CUI__CInput__CManipulationStartedEventArgs *)started, &token );
    ok( hr == S_OK, "Unexpected hr %#lx.\n", hr );
    hr = IGestureRecognizer_add_ManipulationUpdated( recognizer, (__FITypedEventHandler_2_Windows__CUI__CInput__CGestureRecognizer_Windows__CUI__CInput__CManipulationUpdatedEventArgs *)updated, &token );
    ok( hr == S_OK, "Unexpected hr %#lx.\n", hr );
    hr = IGestureRecognizer_add_ManipulationCompleted( recognizer, (__FITypedEventHandler_2_Windows__CUI__CInput__CGestureRecognizer_Windows__CUI__CInput__CManipulationCompletedEventArgs *)completed, &token );
    ok( hr == S_OK, "Unexpected hr %#lx.\n", hr );

    points[0] = create_pointer_point( 0, 0 );
    hr = IGestureRecognizer_ProcessDownEvent( recognizer, points[0] );
    ok( hr == S_OK, "Unexpected hr %#lx.\n", hr );

    points[0] = create_pointer_point( 10, 0 );
    vector = create_vector( 1, points );
    hr = IGestureRecognizer_ProcessMoveEvents( recognizer, vector );
    ok( hr == S_OK, "Unexpected hr %#lx.\n", hr );
    ok( started->calls == 1, "Got %u started calls.\n", started->calls );
    ok( updated->calls == 1, "Got %u updated calls.\n", updated->calls );

    points[0] = create_pointer_point( 20, 0 );
    vector->lpVtbl->SetAt( vector, 0, points[0] );
    hr = IGestureRecognizer_ProcessMoveEvents( recognizer, vector );
    ok( hr == S_OK, "Unexpected hr %#lx.\n", hr );
    ok( started->calls == 1, "Got %u started calls.\n", started->calls );
    ok( updated->calls == 2, "Got %u updated calls.\n", updated->calls );

    points[0] = create_pointer_point( 20, 0 );
    hr = IGestureRecognizer_ProcessUpEvent( recognizer, points[0] );
    ok( hr == S_OK, "Unexpected hr %#lx.\n", hr );
    ok( completed->calls == 1, "Got %u completed calls.\n", completed->calls );

    IUnknown_Release( (IUnknown *)vector );
    IUnknown_Release( (IUnknown *)started );
    IUnknown_Release( (IUnknown *)updated );
    IUnknown_Release( (IUnknown *)completed );

    IInspectable_Release( inspectable2 );
    IActivationFactory_Release( factory );
}

void func_gesturerecognizer( void )
{
    get_activation_factory_func pGetActivationFactory;
    BOOL ret;
    HMODULE module;

    if (!(module = LoadLibraryA( "windows.ui.dll" )))
    {
        ok( 0, "Failed to load windows.ui.dll, error %#lx.\n", GetLastError() );
        return;
    }

    pGetActivationFactory = (void *)GetProcAddress( module, "DllGetActivationFactory" );
    ok( pGetActivationFactory != NULL, "Failed to get DllGetActivationFactory.\n" );

    test_gesturerecognizer( pGetActivationFactory );

    ret = FreeLibrary( module );
    ok( ret, "Failed to free module.\n" );
}
