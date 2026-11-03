/*
 * Windows.System.Profile.EducationSettings tests
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
#define WIDL_using_Windows_System_Profile
#include "windows.foundation.h"
#include "windows.system.profile.h"

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
 * The class must resolve (the gate50 probe of Windows.System.Profile.EducationSettings from
 * client 042c currently answers REGDB_E_CLASSNOTREG and the app retry-loops), the statics
 * interface IEducationSettingsStatics {fc53f0ef-4d3e-4e13-9b23-505f4d091e92} must be
 * answerable, and its single documented member must return the documented value: FALSE,
 * because a Wine prefix is not an education-provisioned environment.
 */
static void test_EducationSettings_Statics(void)
{
    static const WCHAR education_settings_name[] = L"Windows.System.Profile.EducationSettings";
    IActivationFactory *factory = (void *)0xdeadbeef;
    IEducationSettingsStatics *statics = (void *)0xdeadbeef;
    boolean enabled = 0xdeadbeef;
    HSTRING str = NULL;
    HRESULT hr;
    LONG ref;

    hr = WindowsCreateString( education_settings_name, ARRAYSIZE( education_settings_name ) - 1, &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&factory );
    WindowsDeleteString( str );
    ok( hr == S_OK || broken(hr == REGDB_E_CLASSNOTREG), "got hr %#lx.\n", hr );
    if (hr == REGDB_E_CLASSNOTREG)
    {
        win_skip( "%s runtimeclass not registered, skipping tests.\n", wine_dbgstr_w( education_settings_name ) );
        return;
    }

    check_interface( factory, &IID_IUnknown, TRUE );
    check_interface( factory, &IID_IInspectable, TRUE );
    check_interface( factory, &IID_IAgileObject, TRUE );
    check_interface( factory, &IID_IEducationSettingsStatics, TRUE );

    /* The class has no activatable constructor in the metadata. */
    hr = IActivationFactory_ActivateInstance( factory, NULL );
    ok( hr == E_POINTER, "got hr %#lx.\n", hr );

    hr = IActivationFactory_QueryInterface( factory, &IID_IEducationSettingsStatics, (void **)&statics );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    hr = IEducationSettingsStatics_get_IsEducationEnvironment( statics, NULL );
    ok( hr == E_POINTER, "got hr %#lx.\n", hr );
    hr = IEducationSettingsStatics_get_IsEducationEnvironment( statics, &enabled );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ok( enabled == FALSE, "got %d, expected FALSE.\n", enabled );

    /* A second read must give the same documented value. */
    enabled = TRUE;
    hr = IEducationSettingsStatics_get_IsEducationEnvironment( statics, &enabled );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ok( enabled == FALSE, "got %d, expected FALSE.\n", enabled );

    ref = IEducationSettingsStatics_Release( statics );
    ok( ref == 2, "got ref %ld.\n", ref );
    ref = IActivationFactory_Release( factory );
    ok( ref == 1, "got ref %ld.\n", ref );
}

START_TEST(education)
{
    HRESULT hr;

    hr = RoInitialize( RO_INIT_MULTITHREADED );
    ok( hr == S_OK, "RoInitialize failed, hr %#lx\n", hr );

    test_EducationSettings_Statics();

    RoUninitialize();
}
