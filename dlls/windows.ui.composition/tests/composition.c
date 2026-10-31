/*
 * Unit tests for Windows.UI.Composition DirectComposition bridge
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
#include "winstring.h"
#include "activation.h"
#include "roapi.h"
#include "dcomp.h"
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

const GUID IID_IDCompositionVisual = { 0x4d93059d, 0x097b, 0x4651, { 0x9a, 0x60, 0xf0, 0xf2, 0x51, 0x16, 0xe2, 0xf3 } };
const GUID IID_IDCompositionTarget = { 0xeacdd04c, 0x117e, 0x4e17, { 0x88, 0xf4, 0xd1, 0xb1, 0x2b, 0x0e, 0x3d, 0x89 } };

static BOOL register_composition_test_class( void )
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

static void unregister_composition_test_class( void )
{
    RegDeleteKeyW( HKEY_LOCAL_MACHINE,
            L"Software\\Microsoft\\WindowsRuntime\\ActivatableClassId\\Windows.UI.Composition.Compositor" );
}

static void test_dcomp_propagation( void )
{
    struct ICompositorDesktopInterop *desktop_interop = NULL;
    struct ICompositionTarget *target = NULL;
    struct IContainerVisual *root_visual = NULL;
    struct IContainerVisual *child_visual = NULL;
    struct IVisualCollection *children = NULL;
    struct ICompositor *compositor = NULL;
    IActivationFactory *factory = NULL;
    IInspectable *instance = NULL;
    IInspectable *target_obj = NULL;
    IDCompositionVisual *dcomp_visual = NULL;
    IDCompositionVisual *child_dcomp_visual = NULL;
    IDCompositionTarget *dcomp_target = NULL;
    struct IVisual *vis = NULL;
    struct IVisual *child_vis = NULL;
    struct IVisual *got_root = NULL;
    Vector3 offset = { 10.0f, 20.0f, 0.0f }, got_offset = { 0 };
    Vector2 size = { 300.0f, 200.0f }, got_size = { 0 };
    INT32 count = 0;
    HWND hwnd = NULL;
    HSTRING str = NULL;
    HRESULT hr;

    hr = WindowsCreateString( RuntimeClass_Windows_UI_Composition_Compositor,
            ARRAYSIZE( RuntimeClass_Windows_UI_Composition_Compositor ) - 1, &str );
    ok( hr == S_OK, "failed to create class name string, hr %#lx.\n", hr );

    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&factory );
    WindowsDeleteString( str );
    if (FAILED( hr ))
    {
        win_skip( "RoGetActivationFactory failed, hr %#lx.\n", hr );
        return;
    }

    hr = IActivationFactory_ActivateInstance( factory, &instance );
    ok( hr == S_OK && instance, "failed to activate instance, hr %#lx.\n", hr );
    IActivationFactory_Release( factory );

    hr = IInspectable_QueryInterface( instance, &IID_ICompositor, (void **)&compositor );
    ok( hr == S_OK && compositor, "failed to QI ICompositor, hr %#lx.\n", hr );

    /* 1. Create root ContainerVisual */
    hr = ICompositor_CreateContainerVisual( compositor, &root_visual );
    ok( hr == S_OK && root_visual, "failed to create container visual, hr %#lx.\n", hr );

    /* 2. Verify backing IDCompositionVisual is present */
    hr = IContainerVisual_QueryInterface( root_visual, &IID_IDCompositionVisual, (void **)&dcomp_visual );
    ok( hr == S_OK && dcomp_visual, "failed to QI IDCompositionVisual from visual, hr %#lx.\n", hr );

    /* 3. Verify offset and size forwarding */
    hr = IContainerVisual_QueryInterface( root_visual, &IID_IVisual, (void **)&vis );
    ok( hr == S_OK && vis, "failed to QI IVisual, hr %#lx.\n", hr );

    hr = IVisual_put_Offset( vis, offset );
    ok( hr == S_OK, "put_Offset failed, hr %#lx.\n", hr );
    hr = IVisual_get_Offset( vis, &got_offset );
    ok( hr == S_OK && got_offset.X == 10.0f && got_offset.Y == 20.0f, "offset mismatch.\n" );

    hr = IVisual_put_Size( vis, size );
    ok( hr == S_OK, "put_Size failed, hr %#lx.\n", hr );
    hr = IVisual_get_Size( vis, &got_size );
    ok( hr == S_OK && got_size.X == 300.0f && got_size.Y == 200.0f, "size mismatch.\n" );

    /* 4. Create DesktopWindowTarget */
    hwnd = CreateWindowA( "static", "composition_test", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, NULL, NULL, NULL, NULL );
    ok( !!hwnd, "failed to create test window, err %lu.\n", GetLastError() );

    hr = IInspectable_QueryInterface( instance, &IID_ICompositorDesktopInterop, (void **)&desktop_interop );
    ok( hr == S_OK && desktop_interop, "failed to QI ICompositorDesktopInterop, hr %#lx.\n", hr );

    hr = ICompositorDesktopInterop_CreateDesktopWindowTarget( desktop_interop, hwnd, TRUE, &target_obj );
    ok( hr == S_OK && target_obj, "CreateDesktopWindowTarget failed, hr %#lx.\n", hr );
    ICompositorDesktopInterop_Release( desktop_interop );

    hr = IInspectable_QueryInterface( target_obj, &IID_ICompositionTarget, (void **)&target );
    ok( hr == S_OK && target, "failed to QI ICompositionTarget, hr %#lx.\n", hr );
    IInspectable_Release( target_obj );

    /* 5. Verify backing IDCompositionTarget is present */
    hr = ICompositionTarget_QueryInterface( target, &IID_IDCompositionTarget, (void **)&dcomp_target );
    ok( hr == S_OK && dcomp_target, "failed to QI IDCompositionTarget from target, hr %#lx.\n", hr );

    /* 6. Verify target->put_Root(vis) propagates to DirectComposition */
    hr = ICompositionTarget_put_Root( target, vis );
    ok( hr == S_OK, "put_Root failed, hr %#lx.\n", hr );

    /* When attached, DirectComposition marks visual as root; attaching again returns E_INVALIDARG */
    if (dcomp_target && dcomp_visual)
    {
        hr = IDCompositionTarget_SetRoot( dcomp_target, dcomp_visual );
        ok( hr == E_INVALIDARG, "expected E_INVALIDARG because visual is already root, hr %#lx.\n", hr );
    }

    /* 7. Verify get_Root returns the visual */
    hr = ICompositionTarget_get_Root( target, &got_root );
    ok( hr == S_OK && got_root == vis, "get_Root returned wrong visual, hr %#lx.\n", hr );
    if (got_root) IVisual_Release( got_root );

    /* 8. Verify child visual creation and hierarchy propagation */
    hr = ICompositor_CreateContainerVisual( compositor, &child_visual );
    ok( hr == S_OK && child_visual, "failed to create child visual, hr %#lx.\n", hr );

    hr = IContainerVisual_QueryInterface( child_visual, &IID_IDCompositionVisual, (void **)&child_dcomp_visual );
    ok( hr == S_OK && child_dcomp_visual, "failed to QI child IDCompositionVisual, hr %#lx.\n", hr );

    hr = IContainerVisual_get_Children( root_visual, &children );
    ok( hr == S_OK && children, "failed to get children collection, hr %#lx.\n", hr );

    hr = IContainerVisual_QueryInterface( child_visual, &IID_IVisual, (void **)&child_vis );
    ok( hr == S_OK && child_vis, "failed to QI child IVisual, hr %#lx.\n", hr );

    hr = IVisualCollection_InsertAtTop( children, child_vis );
    ok( hr == S_OK, "InsertAtTop failed, hr %#lx.\n", hr );

    hr = IVisualCollection_get_Count( children, &count );
    ok( hr == S_OK && count == 1, "children count mismatch %d.\n", (int)count );

    /* 9. Verify unsetting root */
    hr = ICompositionTarget_put_Root( target, NULL );
    ok( hr == S_OK, "put_Root(NULL) failed, hr %#lx.\n", hr );

    got_root = (void *)0xdeadbeef;
    hr = ICompositionTarget_get_Root( target, &got_root );
    ok( hr == S_OK && got_root == NULL, "get_Root should be NULL, got %p.\n", got_root );

    /* Now dcomp_visual is detached, so direct attachment to dcomp_target succeeds */
    if (dcomp_target && dcomp_visual)
    {
        hr = IDCompositionTarget_SetRoot( dcomp_target, dcomp_visual );
        ok( hr == S_OK, "attaching detached visual directly to dcomp_target failed, hr %#lx.\n", hr );
        IDCompositionTarget_SetRoot( dcomp_target, NULL );
    }

    /* Cleanup */
    IVisualCollection_Release( children );
    if (child_vis) IVisual_Release( child_vis );
    if (child_dcomp_visual) IDCompositionVisual_Release( child_dcomp_visual );
    IContainerVisual_Release( child_visual );
    if (dcomp_target) IDCompositionTarget_Release( dcomp_target );
    ICompositionTarget_Release( target );
    DestroyWindow( hwnd );
    IVisual_Release( vis );
    if (dcomp_visual) IDCompositionVisual_Release( dcomp_visual );
    IContainerVisual_Release( root_visual );
    ICompositor_Release( compositor );
    IInspectable_Release( instance );
}

START_TEST( composition )
{
    if (FAILED( CoInitializeEx( NULL, COINIT_APARTMENTTHREADED ) ))
    {
        win_skip( "failed to initialize COM.\n" );
        return;
    }

    if (!register_composition_test_class())
    {
        CoUninitialize();
        return;
    }

    test_dcomp_propagation();

    unregister_composition_test_class();
    CoUninitialize();
}
