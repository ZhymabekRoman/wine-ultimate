/*
 * Copyright 2025 Zhiyi Zhang for CodeWeavers
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
#define COBJMACROS
#include "windef.h"
#include "winbase.h"
#include "winstring.h"
#include "roapi.h"
#include "wine/test.h"

#define WIDL_using_Windows_UI_Core
#include "windows.ui.core.h"

static const GUID IID_ICoreWindowInterop = { 0x45d64a29, 0xa63e, 0x4cb6, { 0xb4, 0x98, 0x57, 0x81, 0xd2, 0x98, 0xcb, 0x4f } };

typedef struct ICoreWindowInterop ICoreWindowInterop;
typedef struct ICoreWindowInteropVtbl
{
    HRESULT (WINAPI *QueryInterface)( ICoreWindowInterop *This, REFIID riid, void **ppvObject );
    ULONG (WINAPI *AddRef)( ICoreWindowInterop *This );
    ULONG (WINAPI *Release)( ICoreWindowInterop *This );
    HRESULT (WINAPI *GetWindowHandle)( ICoreWindowInterop *This, HWND *hwnd );
    HRESULT (WINAPI *SetMessageHandled)( ICoreWindowInterop *This, boolean value );
} ICoreWindowInteropVtbl;

struct ICoreWindowInterop
{
    const ICoreWindowInteropVtbl *lpVtbl;
};

#define ICoreWindowInterop_QueryInterface(This,riid,ppvObject) (This)->lpVtbl->QueryInterface(This,riid,ppvObject)
#define ICoreWindowInterop_AddRef(This) (This)->lpVtbl->AddRef(This)
#define ICoreWindowInterop_Release(This) (This)->lpVtbl->Release(This)
#define ICoreWindowInterop_GetWindowHandle(This,hwnd) (This)->lpVtbl->GetWindowHandle(This,hwnd)
#define ICoreWindowInterop_SetMessageHandled(This,value) (This)->lpVtbl->SetMessageHandled(This,value)

#define check_interface( obj, iid, exp ) check_interface_( __LINE__, obj, iid, exp )
static void check_interface_( unsigned int line, void *obj, const IID *iid, BOOL supported )
{
    IUnknown *unk, *iface = obj;
    HRESULT hr, expected_hr;

    expected_hr = supported ? S_OK : E_NOINTERFACE;
    hr = IUnknown_QueryInterface( iface, iid, (void **)&unk );
    ok_(__FILE__, line)( hr == expected_hr, "Got hr %#lx, expected %#lx.\n", hr, expected_hr );
    if (SUCCEEDED( hr ))
        IUnknown_Release( unk );
}

static void test_CoreWindow(void)
{
    static const WCHAR *class_name = RuntimeClass_Windows_UI_Core_CoreWindow;
    ICoreWindowStatic *core_window_static;
    IActivationFactory *factory;
    ICoreWindow *core_window;
    ICoreWindowInterop *interop;
    ICoreDispatcher *dispatcher;
    HWND hwnd;
    HSTRING str;
    HRESULT hr;
    LONG ref;

    hr = WindowsCreateString( class_name, wcslen( class_name ), &str );
    ok( hr == S_OK, "Got unexpected hr %#lx.\n", hr );

    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&factory );
    ok( hr == S_OK || broken( hr == REGDB_E_CLASSNOTREG ), "got hr %#lx.\n", hr );
    WindowsDeleteString( str );
    if (FAILED( hr ))
    {
        win_skip( "%s runtimeclass not registered, skipping tests.\n", wine_dbgstr_w( class_name ) );
        return;
    }

    check_interface( factory, &IID_IUnknown, TRUE );
    check_interface( factory, &IID_IAgileObject, TRUE );
    check_interface( factory, &IID_IInspectable, TRUE );
    check_interface( factory, &IID_IActivationFactory, TRUE );
    check_interface( factory, &IID_ICoreWindowStatic, TRUE );

    /* Test ICoreWindowStatic */
    hr = IActivationFactory_QueryInterface( factory, &IID_ICoreWindowStatic, (void **)&core_window_static );
    ok( hr == S_OK, "Got unexpected hr %#lx.\n", hr );

    /* Crash on Windows */
    if (0)
    {
    hr = ICoreWindowStatic_GetForCurrentThread( core_window_static, 0 );
    ok( hr == E_INVALIDARG, "Got unexpected hr %#lx.\n", hr );
    }

    /* Test GetForCurrentThread, Dispatcher, and ICoreWindowInterop */
    core_window = NULL;
    hr = ICoreWindowStatic_GetForCurrentThread( core_window_static, &core_window );
    ok( hr == S_OK, "Got unexpected hr %#lx.\n", hr );
    ok( core_window != NULL, "Expected non-NULL core_window.\n" );

    if (core_window)
    {
        dispatcher = NULL;
        hr = ICoreWindow_get_Dispatcher( core_window, &dispatcher );
        ok( hr == S_OK, "Got unexpected hr %#lx.\n", hr );
        ok( dispatcher != NULL, "Expected non-NULL dispatcher.\n" );
        if (dispatcher)
            ICoreDispatcher_Release( dispatcher );

        interop = NULL;
        hr = ICoreWindow_QueryInterface( core_window, &IID_ICoreWindowInterop, (void **)&interop );
        ok( hr == S_OK, "Got unexpected hr %#lx.\n", hr );
        ok( interop != NULL, "Expected non-NULL interop.\n" );

        if (interop)
        {
            hwnd = NULL;
            hr = ICoreWindowInterop_GetWindowHandle( interop, &hwnd );
            ok( hr == S_OK, "Got unexpected hr %#lx.\n", hr );
            ok( hwnd != NULL, "Expected non-NULL hwnd.\n" );
            ICoreWindowInterop_Release( interop );
        }

        ICoreWindow_Release( core_window );
    }

    ICoreWindowStatic_Release( core_window_static );

    ref = IActivationFactory_Release( factory );
    ok( ref == 1, "Got unexpected ref %ld.\n", ref );
}

START_TEST(corewindow)
{
    HRESULT hr;

    hr = RoInitialize( RO_INIT_MULTITHREADED );
    ok( hr == S_OK, "RoInitialize failed, hr %#lx\n", hr );

    test_CoreWindow();

    RoUninitialize();
}
