/*
 * WinRT Windows.UI.Composition tests
 *
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
#include <stddef.h>

#include "windef.h"
#include "winbase.h"

#define COBJMACROS
#include "initguid.h"
#include "winstring.h"
#include "activation.h"
#include "roapi.h"
#include "wine/test.h"

#define WIDL_using_Windows_Foundation
#include "windows.foundation.h"
#define WIDL_using_Windows_Foundation_Collections
#include "windows.foundation.collections.h"
#define WIDL_using_Windows_Foundation_Numerics
#include "windows.foundation.numerics.h"
#define WIDL_using_Windows_Graphics_DirectX
#include "windows.graphics.directx.h"
#define WIDL_using_Windows_Graphics_Effects
#include "windows.graphics.effects.h"
#define WIDL_using_Windows_UI
#include "windows.ui.h"
#define WIDL_using_Windows_UI_Core
#include "windows.ui.core.h"
#define WIDL_using_Windows_UI_Composition
#include "windows.ui.composition.h"
#define WIDL_using_Windows_UI_Composition_Interop
#include "windows.ui.composition.interop.h"

static const WCHAR test_window_class[] = L"winrt_composition_test_window";

static void test_compositor_activation( void )
{
    IActivationFactory *factory;
    struct ICompositor *compositor;
    IInspectable *instance;
    HSTRING str;
    HRESULT hr;

    hr = WindowsCreateString( RuntimeClass_Windows_UI_Composition_Compositor,
            ARRAYSIZE( RuntimeClass_Windows_UI_Composition_Compositor ) - 1, &str );
    ok( hr == S_OK, "failed to create class name string, %#lx.\n", hr );

    factory = NULL;
    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&factory );
    ok( hr == S_OK, "failed to get activation factory, hr %#lx.\n", hr );
    ok( !!factory, "factory should not be NULL.\n" );

    instance = NULL;
    hr = IActivationFactory_ActivateInstance( factory, &instance );
    ok( hr == S_OK, "failed to activate instance, hr %#lx.\n", hr );
    ok( !!instance, "instance should not be NULL.\n" );

    compositor = NULL;
    hr = IInspectable_QueryInterface( instance, &IID_ICompositor, (void **)&compositor );
    ok( hr == S_OK && compositor, "failed to QI for ICompositor, hr %#lx.\n", hr );

    hr = IInspectable_QueryInterface( instance, &IID_IInspectable, (void **)&instance );
    ok( hr == S_OK, "failed to QI for IInspectable, hr %#lx.\n", hr );
    IInspectable_Release( instance );

    hr = IInspectable_QueryInterface( instance, &IID_IAgileObject, (void **)&instance );
    ok( hr == S_OK, "failed to QI for IAgileObject, hr %#lx.\n", hr );
    IInspectable_Release( instance );

    hr = IInspectable_QueryInterface( instance, &IID_IClosable, (void **)&instance );
    ok( hr == S_OK, "failed to QI for IClosable, hr %#lx.\n", hr );
    IInspectable_Release( instance );

    hr = IInspectable_QueryInterface( instance, &IID_ICompositorInterop, (void **)&instance );
    ok( hr == S_OK, "failed to QI for ICompositorInterop, hr %#lx.\n", hr );
    IInspectable_Release( instance );

    hr = IInspectable_QueryInterface( instance, &IID_ICompositorDesktopInterop, (void **)&instance );
    ok( hr == S_OK, "failed to QI for ICompositorDesktopInterop, hr %#lx.\n", hr );
    IInspectable_Release( instance );

    ICompositor_Release( compositor );
    IActivationFactory_Release( factory );
    WindowsDeleteString( str );
}

static void test_visuals( struct ICompositor *compositor )
{
    struct IVisualCollection *collection;
    struct IContainerVisual *visual;
    struct IVisual *vis;
    struct ICompositionObject *object;
    HSTRING str;
    INT32 count;
    FLOAT opacity;
    HRESULT hr;

    visual = NULL;
    hr = ICompositor_CreateContainerVisual( compositor, &visual );
    ok( hr == S_OK && visual, "failed to create container visual, hr %#lx.\n", hr );

    vis = NULL;
    hr = IContainerVisual_QueryInterface( visual, &IID_IVisual, (void **)&vis );
    ok( hr == S_OK && vis, "failed to QI for IVisual, hr %#lx.\n", hr );

    hr = IContainerVisual_QueryInterface( visual, &IID_ICompositionObject, (void **)&object );
    ok( hr == S_OK && object, "failed to QI for ICompositionObject, hr %#lx.\n", hr );
    ICompositionObject_Release( object );

    WindowsCreateString( RuntimeClass_Windows_UI_Composition_ContainerVisual,
            ARRAYSIZE( RuntimeClass_Windows_UI_Composition_ContainerVisual ) - 1, &str );
    hr = IInspectable_GetRuntimeClassName( (IInspectable *)visual, &str );
    ok( hr == S_OK, "failed to get runtime class name, hr %#lx.\n", hr );
    ok( !wcscmp( WindowsGetStringRawBuffer( str, NULL ),
            RuntimeClass_Windows_UI_Composition_ContainerVisual ), "unexpected class name.\n" );
    WindowsDeleteString( str );

    opacity = 0;
    hr = IVisual_put_Opacity( vis, 0.5f );
    ok( hr == S_OK, "failed to put opacity, hr %#lx.\n", hr );
    hr = IVisual_get_Opacity( vis, &opacity );
    ok( hr == S_OK && opacity == 0.5f, "opacity roundtrip failed, hr %#lx, opacity %g.\n", hr, opacity );
    IVisual_Release( vis );

    collection = NULL;
    hr = IContainerVisual_get_Children( visual, &collection );
    ok( hr == S_OK && collection, "failed to get children, hr %#lx.\n", hr );

    count = 1;
    hr = IVisualCollection_get_Count( collection, &count );
    ok( hr == S_OK && count == 0, "children should start empty, hr %#lx, count %d.\n", hr, count );

    hr = IVisualCollection_InsertAtBottom( collection, (struct IVisual *)visual );
    ok( hr == S_OK, "failed to insert child, hr %#lx.\n", hr );

    count = 0;
    hr = IVisualCollection_get_Count( collection, &count );
    ok( hr == S_OK && count == 1, "children count should be 1, hr %#lx, count %d.\n", hr, count );

    hr = IVisualCollection_RemoveAll( collection );
    ok( hr == S_OK, "failed to remove children, hr %#lx.\n", hr );

    count = 1;
    hr = IVisualCollection_get_Count( collection, &count );
    ok( hr == S_OK && count == 0, "children count should be 0, hr %#lx, count %d.\n", hr, count );

    IVisualCollection_Release( collection );
    IContainerVisual_Release( visual );
}

static void test_property_set( struct ICompositor *compositor )
{
    struct ICompositionPropertySet *set;
    struct ICompositionObject *object;
    struct IContainerVisual *visual;
    enum CompositionGetValueStatus status = CompositionGetValueStatus_NotFound;
    FLOAT scalar = 0;
    HSTRING name;
    HRESULT hr;

    visual = NULL;
    hr = ICompositor_CreateContainerVisual( compositor, &visual );
    ok( hr == S_OK && visual, "failed to create container visual, hr %#lx.\n", hr );

    object = NULL;
    hr = IContainerVisual_QueryInterface( visual, &IID_ICompositionObject, (void **)&object );
    ok( hr == S_OK && object, "failed to QI for ICompositionObject, hr %#lx.\n", hr );

    set = NULL;
    hr = ICompositionObject_get_Properties( object, &set );
    ok( hr == S_OK && set, "failed to get properties, hr %#lx.\n", hr );

    WindowsCreateString( L"test_scalar", 11, &name );

    hr = ICompositionPropertySet_InsertScalar( set, name, 3.0f );
    ok( hr == S_OK, "failed to insert scalar, hr %#lx.\n", hr );

    hr = ICompositionPropertySet_TryGetScalar( set, name, &scalar, &status );
    ok( hr == S_OK && status == CompositionGetValueStatus_Succeeded && scalar == 3.0f,
            "scalar roundtrip failed, hr %#lx, status %u, value %g.\n", hr, status, scalar );

    WindowsDeleteString( name );
    ICompositionPropertySet_Release( set );
    ICompositionObject_Release( object );
    IContainerVisual_Release( visual );
}

static void test_brushes_clips_easing( struct ICompositor *compositor )
{
    struct ICompositionColorBrush *brush;
    struct IInsetClip *clip;
    struct ILinearEasingFunction *linear;
    struct ICubicBezierEasingFunction *cubic;
    Color expected = { 255, 255, 0, 0 }, color = { 0 };
    Vector2 point1 = { 0.1f, 0.2f }, point2 = { 0.3f, 0.4f }, got = { 0 };
    FLOAT inset = 0;
    void *iface;
    HRESULT hr;

    brush = NULL;
    hr = ICompositor_CreateColorBrushWithColor( compositor, expected, &brush );
    ok( hr == S_OK && brush, "failed to create color brush, hr %#lx.\n", hr );

    color.A = 0;
    hr = ICompositionColorBrush_get_Color( brush, &color );
    ok( hr == S_OK && !memcmp( &color, &expected, sizeof(color) ),
            "color roundtrip failed, hr %#lx.\n", hr );
    ICompositionColorBrush_Release( brush );

    clip = NULL;
    hr = ICompositor_CreateInsetClip( compositor, &clip );
    ok( hr == S_OK && clip, "failed to create inset clip, hr %#lx.\n", hr );

    hr = IInsetClip_put_LeftInset( clip, 5.0f );
    ok( hr == S_OK, "failed to put left inset, hr %#lx.\n", hr );
    hr = IInsetClip_get_LeftInset( clip, &inset );
    ok( hr == S_OK && inset == 5.0f, "inset roundtrip failed, hr %#lx, inset %g.\n", hr, inset );
    IInsetClip_Release( clip );

    linear = NULL;
    hr = ICompositor_CreateLinearEasingFunction( compositor, &linear );
    ok( hr == S_OK && linear, "failed to create linear easing, hr %#lx.\n", hr );

    iface = NULL;
    hr = ILinearEasingFunction_QueryInterface( linear, &IID_ICubicBezierEasingFunction, &iface );
    ok( hr == E_NOINTERFACE && !iface, "linear easing should not answer ICubicBezierEasingFunction, hr %#lx.\n", hr );
    ILinearEasingFunction_Release( linear );

    cubic = NULL;
    hr = ICompositor_CreateCubicBezierEasingFunction( compositor, point1, point2, &cubic );
    ok( hr == S_OK && cubic, "failed to create cubic easing, hr %#lx.\n", hr );

    iface = NULL;
    hr = ICubicBezierEasingFunction_QueryInterface( cubic, &IID_ILinearEasingFunction, &iface );
    ok( hr == E_NOINTERFACE && !iface, "cubic easing should not answer ILinearEasingFunction, hr %#lx.\n", hr );

    hr = ICubicBezierEasingFunction_get_ControlPoint1( cubic, &got );
    ok( hr == S_OK && got.X == 0.1f && got.Y == 0.2f, "control point roundtrip failed, hr %#lx.\n", hr );
    ICubicBezierEasingFunction_Release( cubic );
}

static void test_desktop_target( struct ICompositor *compositor, HWND hwnd )
{
    struct ICompositorDesktopInterop *interop;
    struct ICompositionTarget *target;
    struct IDesktopWindowTargetInterop *dwt;
    struct IContainerVisual *root;
    struct IVisual *root2, *vis;
    IInspectable *result;
    HWND got = NULL;
    HRESULT hr;

    interop = NULL;
    hr = IInspectable_QueryInterface( (IInspectable *)compositor, &IID_ICompositorDesktopInterop,
            (void **)&interop );
    ok( hr == S_OK && interop, "failed to QI for ICompositorDesktopInterop, hr %#lx.\n", hr );

    result = NULL;
    hr = ICompositorDesktopInterop_CreateDesktopWindowTarget( interop, hwnd, TRUE, &result );
    ok( hr == S_OK && result, "failed to create desktop window target, hr %#lx.\n", hr );

    target = NULL;
    hr = IInspectable_QueryInterface( result, &IID_ICompositionTarget, (void **)&target );
    ok( hr == S_OK && target, "failed to QI for ICompositionTarget, hr %#lx.\n", hr );

    dwt = NULL;
    hr = IInspectable_QueryInterface( result, &IID_IDesktopWindowTargetInterop, (void **)&dwt );
    ok( hr == S_OK && dwt, "failed to QI for IDesktopWindowTargetInterop, hr %#lx.\n", hr );

    hr = IDesktopWindowTargetInterop_Hwnd( dwt, &got );
    ok( hr == S_OK && got == hwnd, "Hwnd roundtrip failed, hr %#lx.\n", hr );

    root = NULL;
    hr = ICompositor_CreateContainerVisual( compositor, &root );
    ok( hr == S_OK && root, "failed to create root visual, hr %#lx.\n", hr );

    hr = ICompositionTarget_put_Root( target, (struct IVisual *)root );
    ok( hr == S_OK, "failed to set target root, hr %#lx.\n", hr );

    root2 = NULL;
    hr = ICompositionTarget_get_Root( target, &root2 );
    vis = NULL;
    hr = IContainerVisual_QueryInterface( root, &IID_IVisual, (void **)&vis );
    ok( hr == S_OK && vis, "failed to QI for IVisual, hr %#lx.\n", hr );
    ok( hr == S_OK && root2 == vis, "root roundtrip failed, hr %#lx.\n", hr );

    IVisual_Release( root2 );
    IVisual_Release( vis );
    IContainerVisual_Release( root );
    IDesktopWindowTargetInterop_Release( dwt );
    ICompositionTarget_Release( target );
    IInspectable_Release( result );
    ICompositorDesktopInterop_Release( interop );
}

static BOOL register_test_class( void )
{
    static const WCHAR path[] = L"Software\\Microsoft\\WindowsRuntime\\ActivatableClassId\\Windows.UI.Composition.Compositor";
    static const WCHAR dll[] = L"windows.ui.composition.dll";
    HKEY hkey;

    if (RegCreateKeyExW( HKEY_LOCAL_MACHINE, path, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hkey, NULL ))
    {
        win_skip( "cannot register test class.\n" );
        return FALSE;
    }
    RegSetValueExW( hkey, L"DllPath", 0, REG_SZ, (const BYTE *)dll, (lstrlenW( dll ) + 1) * sizeof(WCHAR) );
    RegCloseKey( hkey );
    return TRUE;
}

static void unregister_test_class( void )
{
    RegDeleteKeyW( HKEY_LOCAL_MACHINE,
            L"Software\\Microsoft\\WindowsRuntime\\ActivatableClassId\\Windows.UI.Composition.Compositor" );
}

static HWND create_test_window( void )
{
    WNDCLASSA cls = { 0 };

    cls.lpfnWndProc = DefWindowProcA;
    cls.hInstance = GetModuleHandleA( NULL );
    cls.lpszClassName = "winrt_composition_test_window";
    ok( RegisterClassA( &cls ), "failed to register class, err %u.\n", GetLastError() );
    return CreateWindowA( cls.lpszClassName, "test", 0, 0, 0, 100, 100, NULL, NULL, cls.hInstance, NULL );
}

START_TEST( comp )
{
    struct ICompositor *compositor;
    IActivationFactory *factory;
    IInspectable *instance;
    HSTRING str;
    HWND hwnd;
    HRESULT hr;

    if (FAILED( CoInitializeEx( NULL, COINIT_APARTMENTTHREADED ) ))
    {
        win_skip( "failed to initialize COM.\n" );
        return;
    }

    if (!register_test_class())
    {
        CoUninitialize();
        return;
    }

    WindowsCreateString( RuntimeClass_Windows_UI_Composition_Compositor,
            ARRAYSIZE( RuntimeClass_Windows_UI_Composition_Compositor ) - 1, &str );
    factory = NULL;
    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&factory );
    WindowsDeleteString( str );
    if (hr != S_OK)
    {
        win_skip( "Compositor class not registered, hr %#lx.\n", hr );
        CoUninitialize();
        return;
    }

    test_compositor_activation();

    hr = IActivationFactory_ActivateInstance( factory, &instance );
    ok( hr == S_OK, "failed to activate instance, hr %#lx.\n", hr );
    compositor = NULL;
    hr = IInspectable_QueryInterface( instance, &IID_ICompositor, (void **)&compositor );
    ok( hr == S_OK && compositor, "failed to QI for ICompositor, hr %#lx.\n", hr );

    test_visuals( compositor );
    test_property_set( compositor );
    test_brushes_clips_easing( compositor );

    hwnd = create_test_window();
    ok( !!hwnd, "failed to create test window, err %u.\n", GetLastError() );
    if (hwnd) test_desktop_target( compositor, hwnd );

    ICompositor_Release( compositor );
    IInspectable_Release( instance );
    IActivationFactory_Release( factory );
    unregister_test_class();
    CoUninitialize();
}
