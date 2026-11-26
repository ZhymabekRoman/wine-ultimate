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
#include <stdbool.h>

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

    /* ContainerVisual must NOT answer ISpriteVisual; SpriteVisual must answer both. */
    {
        struct ISpriteVisual *sprite = NULL;
        struct IContainerVisual *sprite_as_cv = NULL;

        hr = IContainerVisual_QueryInterface( visual, &IID_ISpriteVisual, (void **)&sprite );
        ok( hr == E_NOINTERFACE && !sprite, "ContainerVisual should not answer ISpriteVisual, hr %#lx.\n", hr );

        hr = ICompositor_CreateSpriteVisual( compositor, &sprite );
        ok( hr == S_OK && sprite, "failed to create sprite visual, hr %#lx.\n", hr );
        if (sprite)
        {
            hr = ISpriteVisual_QueryInterface( sprite, &IID_IContainerVisual, (void **)&sprite_as_cv );
            ok( hr == S_OK && sprite_as_cv, "SpriteVisual should answer IContainerVisual, hr %#lx.\n", hr );
            if (sprite_as_cv) IContainerVisual_Release( sprite_as_cv );
            ISpriteVisual_Release( sprite );
        }
    }

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

    {
        static const GUID IID_IAnimationObject_test =
            { 0xe7141e0a, 0x04b8, 0x4fc5, { 0xa4, 0xdc, 0x19, 0x53, 0x92, 0xe5, 0x78, 0x07 } };
        IInspectable *anim_obj = NULL;
        HRESULT (WINAPI *populate)(void *, HSTRING, void *);

        hr = ICompositionPropertySet_QueryInterface( set, &IID_IAnimationObject_test, (void **)&anim_obj );
        ok( hr == S_OK && anim_obj, "failed to QI propset for IAnimationObject, hr %#lx.\n", hr );
        if (anim_obj)
        {
            populate = (void *)((void **)anim_obj->lpVtbl)[6];
            hr = populate( anim_obj, name, NULL );
            ok( hr == S_OK, "IAnimationObject::PopulatePropertyInfo failed, hr %#lx.\n", hr );
            IInspectable_Release( anim_obj );
        }
    }

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
    {
        static const GUID IID_ICompositor6_test =
            { 0x7a38b2bd, 0xcec8, 0x4eeb, { 0x83, 0x0f, 0xd8, 0xd0, 0x7a, 0xed, 0xeb, 0xc3 } };
        static const GUID IID_ICompositionGeometricClip_test =
            { 0xc840b581, 0x81c9, 0x4444, { 0xa2, 0xc1, 0xcc, 0xae, 0xce, 0x3a, 0x50, 0xe5 } };
        IInspectable *comp6 = NULL;
        IInspectable *gclip = NULL;
        struct ICompositionClip *base_clip = NULL;
        struct IContainerVisual *cv = NULL;
        struct IVisual *vis = NULL;

        iface = NULL;
        hr = IInsetClip_QueryInterface( clip, &IID_ICompositionGeometricClip_test, &iface );
        ok( hr == E_NOINTERFACE && !iface, "InsetClip should not answer ICompositionGeometricClip, hr %#lx.\n", hr );

        hr = ICompositor_QueryInterface( compositor, &IID_ICompositor6_test, (void **)&comp6 );
        ok( hr == S_OK && comp6, "Compositor should answer ICompositor6, hr %#lx.\n", hr );
        if (comp6)
        {
            HRESULT (WINAPI *create_gclip)(void *, void **) = (void *)((void **)comp6->lpVtbl)[6];
            hr = create_gclip( comp6, (void **)&gclip );
            ok( hr == S_OK && gclip, "CreateGeometricClip failed, hr %#lx.\n", hr );
            if (gclip)
            {
                iface = NULL;
                hr = IInspectable_QueryInterface( gclip, &IID_IInsetClip, &iface );
                ok( hr == E_NOINTERFACE && !iface,
                        "CompositionGeometricClip should not answer IInsetClip, hr %#lx.\n", hr );

                hr = IInspectable_QueryInterface( gclip, &IID_ICompositionClip, (void **)&base_clip );
                ok( hr == S_OK && base_clip,
                        "CompositionGeometricClip should answer ICompositionClip, hr %#lx.\n", hr );

                hr = ICompositor_CreateContainerVisual( compositor, &cv );
                if (hr == S_OK && cv && base_clip)
                {
                    hr = IContainerVisual_QueryInterface( cv, &IID_IVisual, (void **)&vis );
                    if (hr == S_OK && vis)
                    {
                        hr = IVisual_put_Clip( vis, base_clip );
                        ok( hr == S_OK, "put_Clip(CompositionGeometricClip) failed, hr %#lx.\n", hr );
                        IVisual_Release( vis );
                    }
                    IContainerVisual_Release( cv );
                }
                if (base_clip) ICompositionClip_Release( base_clip );
                IInspectable_Release( gclip );
            }
            IInspectable_Release( comp6 );
        }
    }
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

/*
 * The unpackaged WinUI 3 startup path (WhatsApp Desktop): the app's XAML root activates
 * Windows.UI.Composition.Compositor while the prefix has NO
 * Software\Microsoft\WindowsRuntime\ActivatableClassId entry for it, because such an app
 * registers its classes through the framework package.  combase has to resolve the class
 * to this module through its own routing; a miss shows up in the app trace as
 * "err:combase:roapi.c:325 Failed to find library for Windows.UI.Composition.Compositor"
 * and the UI thread then parks.
 *
 * The .NET projection (WinRTModule.GetActivationFactory<I>(runtimeClassId, iid)) and the
 * C++/WinRT projection pass the class interface IID (IID_ICompositor), not
 * IID_IActivationFactory, so both request shapes are pinned here, together with the
 * factory methods the XAML root calls on the resulting compositor.
 */
static void test_activation_without_registry( void )
{
    struct IVisualCollection *collection = NULL;
    struct ICompositionPropertySet *props = NULL;
    struct ICompositionColorBrush *brush = NULL;
    struct ICompositor *compositor = NULL;
    struct IContainerVisual *visual = NULL;
    struct ISpriteVisual *sprite = NULL;
    IActivationFactory *factory = NULL;
    IInspectable *instance = NULL;
    HSTRING str, name = NULL;
    INT32 count = -1;
    HRESULT hr;

    /* The prefix registers this class for the built-in module (loader/wine.inf.in); drop
     * the key so this probe exercises the route combase has to provide when no
     * ActivatableClassId entry exists, as on a prefix that predates the registration. */
    RegDeleteKeyW( HKEY_LOCAL_MACHINE,
            L"Software\\Microsoft\\WindowsRuntime\\ActivatableClassId\\Windows.UI.Composition.Compositor" );

    hr = WindowsCreateString( RuntimeClass_Windows_UI_Composition_Compositor,
            ARRAYSIZE( RuntimeClass_Windows_UI_Composition_Compositor ) - 1, &str );
    ok( hr == S_OK, "failed to create class name string, %#lx.\n", hr );

    compositor = NULL;
    hr = RoGetActivationFactory( str, &IID_ICompositor, (void **)&compositor );
    ok( hr == S_OK, "RoGetActivationFactory(IID_ICompositor) failed, hr %#lx.\n", hr );
    ok( !!compositor, "RoGetActivationFactory(IID_ICompositor) returned compositor %p.\n", compositor );

    factory = NULL;
    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&factory );
    ok( hr == S_OK, "RoGetActivationFactory(IID_IActivationFactory) failed, hr %#lx.\n", hr );
    ok( !!factory, "RoGetActivationFactory(IID_IActivationFactory) returned factory %p.\n", factory );

    instance = NULL;
    hr = RoActivateInstance( str, &instance );
    ok( hr == S_OK, "RoActivateInstance failed, hr %#lx.\n", hr );
    ok( !!instance, "RoActivateInstance returned instance %p.\n", instance );
    if (instance) IInspectable_Release( instance );

    if (factory)
    {
        instance = NULL;
        hr = IActivationFactory_ActivateInstance( factory, &instance );
        ok( hr == S_OK && instance, "ActivateInstance failed, hr %#lx.\n", hr );
        if (instance)
        {
            struct ICompositor *activated = NULL;
            hr = IInspectable_QueryInterface( instance, &IID_ICompositor, (void **)&activated );
            ok( hr == S_OK && activated, "failed to QI the activated instance for ICompositor, hr %#lx.\n", hr );
            if (activated) ICompositor_Release( activated );

            name = NULL;
            hr = IInspectable_GetRuntimeClassName( instance, &name );
            ok( hr == S_OK, "GetRuntimeClassName failed, hr %#lx.\n", hr );
            ok( !wcscmp( WindowsGetStringRawBuffer( name, NULL ),
                         RuntimeClass_Windows_UI_Composition_Compositor ),
                    "got class name %s.\n", debugstr_w( WindowsGetStringRawBuffer( name, NULL ) ) );
            WindowsDeleteString( name );
            IInspectable_Release( instance );
        }
    }

    if (!compositor)
    {
        /* Nothing to exercise: the class did not resolve, which is the regression this
         * test pins.  Skip the compositor-body checks instead of crashing. */
        if (factory) IActivationFactory_Release( factory );
        WindowsDeleteString( str );
        return;
    }

    /* The compositor the app gets must build the visual tree the XAML root asks for. */
    visual = NULL;
    hr = ICompositor_CreateContainerVisual( compositor, &visual );
    ok( hr == S_OK && visual, "failed to create container visual, hr %#lx.\n", hr );
    if (visual)
    {
        collection = NULL;
        hr = IContainerVisual_get_Children( visual, &collection );
        ok( hr == S_OK && collection, "failed to get visual children, hr %#lx.\n", hr );
        if (collection)
        {
            count = -1;
            hr = IVisualCollection_get_Count( collection, &count );
            ok( hr == S_OK && count == 0, "new container visual has %d children, hr %#lx.\n", count, hr );
            IVisualCollection_Release( collection );
        }
        IContainerVisual_Release( visual );
    }

    sprite = NULL;
    hr = ICompositor_CreateSpriteVisual( compositor, &sprite );
    ok( hr == S_OK && sprite, "failed to create sprite visual, hr %#lx.\n", hr );
    if (sprite) ISpriteVisual_Release( sprite );

    brush = NULL;
    hr = ICompositor_CreateColorBrush( compositor, &brush );
    ok( hr == S_OK && brush, "failed to create brush, hr %#lx.\n", hr );
    if (brush) ICompositionColorBrush_Release( brush );

    props = NULL;
    hr = ICompositor_CreatePropertySet( compositor, &props );
    ok( hr == S_OK && props, "failed to create property set, hr %#lx.\n", hr );
    if (props) ICompositionPropertySet_Release( props );

    ICompositor_Release( compositor );
    if (factory) IActivationFactory_Release( factory );
    WindowsDeleteString( str );
}

/*
 * The WinUI 3 XAML root caches the Compositor's interop partner factory:
 * Microsoft.ui.xaml.dll!ActivationFactoryCache::GetInteropCompositorFactoryPartner()
 * calls RoGetActivationFactory( Compositor class name, IID_IInteropCompositorFactoryPartner )
 * and, when that call fails, runs straight into FailFastWithStowedExceptions (verified in
 * IDA: OnFailure -> TraceForFailFast -> FailFastWithStowedExceptions).  The interface is
 * Microsoft.UI.Composition.IInteropCompositorFactoryPartner {22118adf-23f1-4801-bcfa-
 * 66cbf48cc51b}, recovered from dcompi.dll's Microsoft::UI::Composition::CompositorFactory,
 * which is WRL Implements<FtmBase, IInteropCompositorFactoryPartner> + ICompositorStatics +
 * IInteropCompositorFactoryRestricted.  Its vtable (dcompi 0x180122c40) is
 * IUnknown + IActivationFactory + CreateInteropCompositor + CheckEnabled, so the factory
 * has to answer this QI as the same activatable factory object.
 */
/* Declared here (not from a header): the interface has no published IDL in the tree. The
 * slot order below is the one dcompi.dll's factory object exposes, not an assumption:
 * vftable 0x180122c40 = QueryInterface, AddRef, Release, GetIids, GetRuntimeClassName,
 * GetTrustLevel, CreateInteropCompositor (0x18005a5d0), CheckEnabled (0x180056d00). */
typedef struct IInteropCompositorFactoryPartner IInteropCompositorFactoryPartner;
typedef struct IInteropCompositorFactoryPartnerVtbl
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)( IInteropCompositorFactoryPartner *iface, REFIID riid, void **obj );
    ULONG (STDMETHODCALLTYPE *AddRef)( IInteropCompositorFactoryPartner *iface );
    ULONG (STDMETHODCALLTYPE *Release)( IInteropCompositorFactoryPartner *iface );
    HRESULT (STDMETHODCALLTYPE *GetIids)( IInteropCompositorFactoryPartner *iface, ULONG *count, IID **iids );
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)( IInteropCompositorFactoryPartner *iface, HSTRING *name );
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)( IInteropCompositorFactoryPartner *iface, TrustLevel *level );
    HRESULT (STDMETHODCALLTYPE *CreateInteropCompositor)( IInteropCompositorFactoryPartner *iface,
            IUnknown *device, IUnknown *callback, REFIID riid, void **result );
    HRESULT (STDMETHODCALLTYPE *CheckEnabled)( IInteropCompositorFactoryPartner *iface, bool *enabled, bool *second );
} IInteropCompositorFactoryPartnerVtbl;
struct IInteropCompositorFactoryPartner { const struct IInteropCompositorFactoryPartnerVtbl *lpVtbl; };

#define IPartner_QueryInterface(p,a,b)        (p)->lpVtbl->QueryInterface(p,a,b)
#define IPartner_Release(p)                   (p)->lpVtbl->Release(p)
#define IPartner_CreateInteropCompositor(p,a,b,c,d) (p)->lpVtbl->CreateInteropCompositor(p,a,b,c,d)
#define IPartner_CheckEnabled(p,a,b)          (p)->lpVtbl->CheckEnabled(p,a,b)

static const IID IID_IInteropCompositorFactoryPartner_test =
    { 0x22118adf, 0x23f1, 0x4801, { 0xbc, 0xfa, 0x66, 0xcb, 0xf4, 0x8c, 0xc5, 0x1b } };

static const IID IID_test_unimplemented =
    { 0x11223344, 0x5566, 0x7788, { 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x00 } };

static void test_interop_partner_factory( void )
{
    IInteropCompositorFactoryPartner *partner = NULL;
    IActivationFactory *factory = NULL;
    IUnknown *unknown = NULL;
    ICompositorInterop *interop = NULL;
    struct ICompositor *compositor = NULL, *compositor2 = NULL;
    IContainerVisual *visual = NULL;
    IVisualCollection *collection = NULL;
    HSTRING str, name;
    bool a = true, b = true;
    ULONG ref;
    INT count;
    HRESULT hr;

    hr = WindowsCreateString( RuntimeClass_Windows_UI_Composition_Compositor,
            ARRAYSIZE( RuntimeClass_Windows_UI_Composition_Compositor ) - 1, &str );
    ok( hr == S_OK, "failed to create class name string, %#lx.\n", hr );

    partner = NULL;
    hr = RoGetActivationFactory( str, &IID_IInteropCompositorFactoryPartner_test, (void **)&partner );
    ok( hr == S_OK && partner, "RoGetActivationFactory(IInteropCompositorFactoryPartner) failed, hr %#lx.\n", hr );

    if (!partner)
    {
        WindowsDeleteString( str );
        return;
    }

    /* The partner is the activatable factory itself: dcompi hands out a selector vftable of
     * the same instance, so the activation interfaces answer through it. */
    unknown = NULL;
    hr = IPartner_QueryInterface( partner, &IID_IUnknown, (void **)&unknown );
    ok( hr == S_OK && unknown, "QI for IUnknown on the partner failed, hr %#lx.\n", hr );
    IUnknown_Release( unknown );

    factory = NULL;
    hr = IPartner_QueryInterface( partner, &IID_IActivationFactory, (void **)&factory );
    ok( hr == S_OK && factory, "QI for IActivationFactory on the partner failed, hr %#lx.\n", hr );

    /* CreateInteropCompositor is what the WinUI-side clients drive on this interface:
     * Microsoft.UI.Windowing.Core.dll!CTitleBar::EvaluateNonClientCustomizationSupported
     * (0x18003249a, TitleBar.cxx) calls it as (NULL, NULL, IID_ICompositor) right after
     * the QI above, and Microsoft.ui.xaml.dll!DCompTreeHost::CreateWinRTInteropCompositionDevice
     * (0x1802357b8) calls it as (NULL, callback, IDCompositionDesktopDevicePartner).
     * dcompi.dll's CompositorFactory::CreateInteropCompositor (0x18005a5d0) constructs a
     * fresh compositor object per call and QIs it for the requested riid (0x18005a759);
     * on this bridge that object is the module's own Compositor, so the call must hand
     * back a working ICompositor for the riid XAML passes. */
    compositor = NULL;
    hr = IPartner_CreateInteropCompositor( partner, NULL, NULL, &IID_ICompositor, (void **)&compositor );
    ok( hr == S_OK && compositor, "CreateInteropCompositor(ICompositor) failed, hr %#lx.\n", hr );

    if (compositor)
    {
        /* It is the activatable class's compositor: same runtime class name, the root
         * interface answers as its own IUnknown (object identity), and it builds the
         * visual tree the XAML root asks for. */
        name = NULL;
        hr = ICompositor_GetRuntimeClassName( compositor, &name );
        ok( hr == S_OK, "GetRuntimeClassName on the interop compositor failed, hr %#lx.\n", hr );
        ok( !wcscmp( WindowsGetStringRawBuffer( name, NULL ),
                     RuntimeClass_Windows_UI_Composition_Compositor ),
                "got class name %s.\n", debugstr_w( WindowsGetStringRawBuffer( name, NULL ) ) );
        WindowsDeleteString( name );

        unknown = NULL;
        hr = ICompositor_QueryInterface( compositor, &IID_IUnknown, (void **)&unknown );
        ok( hr == S_OK && unknown == (IUnknown *)compositor,
                "root ICompositor QI'd IUnknown %p (self %p), hr %#lx.\n", unknown, compositor, hr );
        IUnknown_Release( unknown );

        visual = NULL;
        hr = ICompositor_CreateContainerVisual( compositor, &visual );
        ok( hr == S_OK && visual, "interop compositor failed to create container visual, hr %#lx.\n", hr );
        if (visual)
        {
            collection = NULL;
            hr = IContainerVisual_get_Children( visual, &collection );
            ok( hr == S_OK && collection, "failed to get visual children, hr %#lx.\n", hr );
            if (collection)
            {
                count = -1;
                hr = IVisualCollection_get_Count( collection, &count );
                ok( hr == S_OK && count == 0, "new container visual has %d children, hr %#lx.\n", count, hr );
                IVisualCollection_Release( collection );
            }
            IContainerVisual_Release( visual );
        }

        /* Refcount sanity: a balanced AddRef/Release pair leaves the caller's reference. */
        ref = ICompositor_AddRef( compositor );
        ICompositor_Release( compositor );
        ok( ref >= 1, "interop compositor refcount started at %lu.\n", ref );
    }

    /* Fresh object per call: dcompi runs the constructor + QI + release of the local
     * reference for every CreateInteropCompositor. */
    compositor2 = NULL;
    hr = IPartner_CreateInteropCompositor( partner, NULL, NULL, &IID_ICompositor, (void **)&compositor2 );
    ok( hr == S_OK && compositor2 && compositor2 != compositor,
            "second CreateInteropCompositor returned %p (first %p), hr %#lx.\n", compositor2, compositor, hr );

    /* riid is a pass-through QI of the constructed compositor, and a refused interface
     * comes back NULL with its HRESULT (dcompi nulls the result before the work,
     * 0x18005a609, and never writes an object on the failure path). */
    interop = NULL;
    hr = IPartner_CreateInteropCompositor( partner, NULL, NULL, &IID_ICompositorInterop, (void **)&interop );
    ok( hr == S_OK && interop, "CreateInteropCompositor(ICompositorInterop) failed, hr %#lx.\n", hr );
    if (interop) ICompositorInterop_Release( interop );

    unknown = NULL;
    hr = IPartner_CreateInteropCompositor( partner, NULL, NULL, &IID_test_unimplemented, (void **)&unknown );
    ok( hr == E_NOINTERFACE, "CreateInteropCompositor(unimplemented riid) should give E_NOINTERFACE, hr %#lx.\n", hr );
    ok( !unknown, "CreateInteropCompositor left output %p for an unimplemented riid.\n", unknown );

    if (compositor2) ICompositor_Release( compositor2 );
    if (compositor) ICompositor_Release( compositor );

    /* CheckEnabled: dcompi's CompositorFactory::CheckEnabled (0x180056d00) writes true
     * to both bools and returns S_OK unconditionally: there is no registry gate. */
    a = b = false;
    hr = IPartner_CheckEnabled( partner, &a, &b );
    ok( hr == S_OK, "CheckEnabled failed, hr %#lx.\n", hr );
    ok( a && b, "CheckEnabled must report enabled and available, a %d b %d.\n", a, b );

    if (factory) IActivationFactory_Release( factory );
    IPartner_Release( partner );
    WindowsDeleteString( str );
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
    ok( RegisterClassA( &cls ), "failed to register class, err %lu.\n", GetLastError() );
    return CreateWindowA( cls.lpszClassName, "test", 0, 0, 0, 100, 100, NULL, NULL, cls.hInstance, NULL );
}

static void test_shape_visual_and_co4( struct ICompositor *compositor )
{
    static const GUID IID_ICompositor5_test =
        { 0x48ea31ad, 0x7fcd, 0x4076, { 0xa7, 0x9c, 0x90, 0xcc, 0x4b, 0x85, 0x2c, 0x9b } };
    static const GUID IID_ICompositionObject4_winrt_test =
        { 0x0bb3784c, 0x346b, 0x4a7c, { 0x96, 0x6b, 0x73, 0x10, 0x96, 0x65, 0x53, 0xd5 } };
    static const GUID IID_ICompositionObject4_alt_test =
        { 0xf2bd13c3, 0xba7e, 0x4b0f, { 0x91, 0x26, 0xff, 0xb7, 0x53, 0x6b, 0x81, 0x76 } };
    IInspectable *comp5 = NULL, *shape_vis = NULL, *co4 = NULL, *co4_alt = NULL;
    IInspectable *shapes = NULL, *ctrl = NULL;
    struct IContainerVisual *cv = NULL;
    HSTRING prop = NULL, cls = NULL;
    HRESULT hr;

    hr = ICompositor_QueryInterface( compositor, &IID_ICompositor5_test, (void **)&comp5 );
    ok( hr == S_OK && comp5, "Compositor should answer ICompositor5, hr %#lx.\n", hr );
    if (comp5)
    {
        HRESULT (WINAPI *create_shape_visual)(void *, void **) = (void *)((void **)comp5->lpVtbl)[21];
        hr = create_shape_visual( comp5, (void **)&shape_vis );
        ok( hr == S_OK && shape_vis, "CreateShapeVisual failed, hr %#lx.\n", hr );
        if (shape_vis)
        {
            HRESULT (WINAPI *get_shapes)(void *, void **) = (void *)((void **)shape_vis->lpVtbl)[6];

            hr = IInspectable_GetRuntimeClassName( shape_vis, &cls );
            ok( hr == S_OK && cls && !wcscmp( WindowsGetStringRawBuffer( cls, NULL ),
                    L"Windows.UI.Composition.ShapeVisual" ),
                    "unexpected ShapeVisual class name, hr %#lx.\n", hr );
            if (cls) { WindowsDeleteString( cls ); cls = NULL; }

            hr = get_shapes( shape_vis, (void **)&shapes );
            ok( hr == S_OK && shapes, "IShapeVisual::get_Shapes failed, hr %#lx.\n", hr );
            if (shapes)
            {
                hr = IInspectable_GetRuntimeClassName( shapes, &cls );
                ok( hr == S_OK && cls && !wcscmp( WindowsGetStringRawBuffer( cls, NULL ),
                        L"Windows.UI.Composition.CompositionShapeCollection" ),
                        "unexpected Shapes class name, hr %#lx.\n", hr );
                if (cls) { WindowsDeleteString( cls ); cls = NULL; }
                IInspectable_Release( shapes );
                shapes = NULL;
            }

            hr = IInspectable_QueryInterface( shape_vis, &IID_ICompositionObject4_alt_test, (void **)&co4_alt );
            ok( hr == S_OK && co4_alt, "ShapeVisual QI {f2bd13c3} failed, hr %#lx.\n", hr );
            if (co4_alt)
            {
                HRESULT (WINAPI *slot6_get)(void *, void **) = (void *)((void **)co4_alt->lpVtbl)[6];
                HRESULT (WINAPI *slot6_try)(void *, HSTRING, void **) = (void *)((void **)co4_alt->lpVtbl)[6];

                shapes = NULL;
                hr = slot6_get( co4_alt, (void **)&shapes );
                ok( hr == S_OK && shapes, "{f2bd13c3} 2-arg slot6 failed, hr %#lx.\n", hr );
                if (shapes)
                {
                    hr = IInspectable_GetRuntimeClassName( shapes, &cls );
                    ok( hr == S_OK && cls && !wcscmp( WindowsGetStringRawBuffer( cls, NULL ),
                            L"Windows.UI.Composition.CompositionShapeCollection" ),
                            "unexpected {f2bd13c3} 2-arg class name, hr %#lx.\n", hr );
                    if (cls) { WindowsDeleteString( cls ); cls = NULL; }
                    IInspectable_Release( shapes );
                    shapes = NULL;
                }

                WindowsCreateString( L"Offset", 6, &prop );
                ctrl = NULL;
                hr = slot6_try( co4_alt, prop, (void **)&ctrl );
                ok( hr == S_OK && ctrl, "{f2bd13c3} 3-arg slot6 failed, hr %#lx.\n", hr );
                if (ctrl)
                {
                    hr = IInspectable_GetRuntimeClassName( ctrl, &cls );
                    ok( hr == S_OK && cls && !wcscmp( WindowsGetStringRawBuffer( cls, NULL ),
                            L"Windows.UI.Composition.AnimationController" ),
                            "unexpected {f2bd13c3} 3-arg class name, hr %#lx.\n", hr );
                    if (cls) { WindowsDeleteString( cls ); cls = NULL; }
                    IInspectable_Release( ctrl );
                    ctrl = NULL;
                }
                WindowsDeleteString( prop );
                prop = NULL;
                IInspectable_Release( co4_alt );
                co4_alt = NULL;
            }
            IInspectable_Release( shape_vis );
        }
        IInspectable_Release( comp5 );
    }

    hr = ICompositor_CreateContainerVisual( compositor, &cv );
    ok( hr == S_OK && cv, "CreateContainerVisual failed, hr %#lx.\n", hr );
    if (cv)
    {
        hr = IContainerVisual_QueryInterface( cv, &IID_ICompositionObject4_winrt_test, (void **)&co4 );
        ok( hr == S_OK && co4, "ContainerVisual QI ICompositionObject4 failed, hr %#lx.\n", hr );
        if (co4)
        {
            HRESULT (WINAPI *try_get)(void *, HSTRING, void **) = (void *)((void **)co4->lpVtbl)[6];
            WindowsCreateString( L"Opacity", 7, &prop );
            ctrl = NULL;
            hr = try_get( co4, prop, (void **)&ctrl );
            ok( hr == S_OK && ctrl, "ICompositionObject4::TryGetAnimationController failed, hr %#lx.\n", hr );
            if (ctrl)
            {
                hr = IInspectable_GetRuntimeClassName( ctrl, &cls );
                ok( hr == S_OK && cls && !wcscmp( WindowsGetStringRawBuffer( cls, NULL ),
                        L"Windows.UI.Composition.AnimationController" ),
                        "unexpected AnimationController class name, hr %#lx.\n", hr );
                if (cls) { WindowsDeleteString( cls ); cls = NULL; }
                IInspectable_Release( ctrl );
            }
            WindowsDeleteString( prop );
            IInspectable_Release( co4 );
        }
        IContainerVisual_Release( cv );
    }
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

    /* Probe before any ActivatableClassId registration exists (fresh prefix): this is the
     * exact shape of the unpackaged WinUI 3 startup activation. */
    test_activation_without_registry();
    test_interop_partner_factory();

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
    test_shape_visual_and_co4( compositor );

    hwnd = create_test_window();
    ok( !!hwnd, "failed to create test window, err %lu.\n", GetLastError() );
    if (hwnd) test_desktop_target( compositor, hwnd );

    ICompositor_Release( compositor );
    IInspectable_Release( instance );
    IActivationFactory_Release( factory );
    unregister_test_class();
    CoUninitialize();
}
