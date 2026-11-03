/*
 * Windows.Devices.Input.PenDevice tests
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

#include "initguid.h"
#include <stdarg.h>

#define COBJMACROS
#include "windef.h"
#include "winbase.h"
#include "winstring.h"

#include "activation.h"
#include "roapi.h"

#define WIDL_using_Windows_Foundation
#define WIDL_using_Windows_Devices_Input
#include "windows.foundation.h"
#include "windows.devices.input.h"

#include "wine/test.h"

static void check_interface_( unsigned int line, void *obj, const IID *iid, BOOL supported )
{
    IUnknown *iface = obj;
    IUnknown *unk;
    HRESULT hr;

    hr = IUnknown_QueryInterface( iface, iid, (void **)&unk );
    ok_(__FILE__, line)( hr == S_OK || (!supported && hr == E_NOINTERFACE), "got hr %#lx.\n", hr );
    if (SUCCEEDED(hr))
        IUnknown_Release( unk );
}
#define check_interface( obj, iid, supported ) check_interface_( __LINE__, obj, iid, supported )

/*
 * The class must resolve (gate50 probes Windows.Devices.Input.PenDevice from client 0468 and
 * retry-loops on REGDB_E_CLASSNOTREG), IPenDeviceStatics {9dfbbe01-0966-5180-bcb4-b85060e39479}
 * must be answerable, and its single documented member GetFromPointerId must return the documented
 * "no pen for this pointer" answer: S_OK with a null PenDevice (learn.microsoft.com, "Pen Device.
 * Get From Pointer Id(UInt32) Method": "Returns null if the pointer input does not correspond to
 * the pen input, or if the pen does not support a unique ID.").
 */
static void test_PenDevice_Statics(void)
{
    static const WCHAR pen_device_name[] = L"Windows.Devices.Input.PenDevice";
    IActivationFactory *factory = (void *)0xdeadbeef;
    IPenDeviceStatics *statics = (void *)0xdeadbeef;
    __x_ABI_CWindows_CDevices_CInput_CPenDevice *device = (void *)0xdeadbeef;
    HSTRING str = NULL;
    HRESULT hr;
    LONG ref;

    hr = WindowsCreateString( pen_device_name, ARRAYSIZE( pen_device_name ) - 1, &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&factory );
    WindowsDeleteString( str );
    ok( hr == S_OK || broken(hr == REGDB_E_CLASSNOTREG), "got hr %#lx.\n", hr );
    if (hr == REGDB_E_CLASSNOTREG)
    {
        win_skip( "%s runtimeclass not registered, skipping tests.\n", wine_dbgstr_w( pen_device_name ) );
        return;
    }

    check_interface( factory, &IID_IUnknown, TRUE );
    check_interface( factory, &IID_IInspectable, TRUE );
    check_interface( factory, &IID_IAgileObject, TRUE );
    check_interface( factory, &IID_IPenDeviceStatics, TRUE );

    /* Instances only come out of GetFromPointerId; the statics factory does not answer them. */
    check_interface( factory, &IID_IPenDevice, FALSE );

    /* The class has no activatable constructor in the metadata. */
    hr = IActivationFactory_ActivateInstance( factory, NULL );
    ok( hr == E_POINTER, "got hr %#lx.\n", hr );

    hr = IActivationFactory_QueryInterface( factory, &IID_IPenDeviceStatics, (void **)&statics );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    hr = IPenDeviceStatics_GetFromPointerId( statics, 1, NULL );
    ok( hr == E_POINTER, "got hr %#lx.\n", hr );

    device = (void *)0xdeadbeef;
    hr = IPenDeviceStatics_GetFromPointerId( statics, 1, &device );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ok( !device, "got device %p, expected NULL.\n", device );

    /* A pointer id that maps to no pen answers the same documented null. */
    device = (void *)0xdeadbeef;
    hr = IPenDeviceStatics_GetFromPointerId( statics, 0xdeadbeef, &device );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ok( !device, "got device %p, expected NULL.\n", device );

    ref = IPenDeviceStatics_Release( statics );
    ok( ref == 2, "got ref %ld.\n", ref );
    ref = IActivationFactory_Release( factory );
    ok( ref == 1, "got ref %ld.\n", ref );
}

START_TEST(pendevice)
{
    HRESULT hr;

    hr = RoInitialize( RO_INIT_MULTITHREADED );
    ok( hr == S_OK, "RoInitialize failed, hr %#lx\n", hr );

    test_PenDevice_Statics();

    RoUninitialize();
}
