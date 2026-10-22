/* WinRT Windows.UI.Composition Implementation
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

#include "initguid.h"
#include "private.h"

WINE_DEFAULT_DEBUG_CHANNEL(composition);

extern IActivationFactory *compositor_factory_get( void );

HRESULT WINAPI DllCanUnloadNow( void )
{
    return S_OK;
}

HRESULT WINAPI DllGetClassObject( REFCLSID clsid, REFIID riid, void **out )
{
    FIXME( "clsid %s, riid %s, out %p stub!\n", debugstr_guid( clsid ), debugstr_guid( riid ), out );
    return CLASS_E_CLASSNOTAVAILABLE;
}

HRESULT WINAPI DllGetActivationFactory( HSTRING classid, IActivationFactory **factory )
{
    const WCHAR *name = WindowsGetStringRawBuffer( classid, NULL );

    TRACE( "class %s, factory %p.\n", debugstr_hstring( classid ), factory );

    *factory = NULL;

    if (name && !wcscmp( name, RuntimeClass_Windows_UI_Composition_Compositor ))
        IActivationFactory_QueryInterface( compositor_factory_get(), &IID_IActivationFactory,
                (void **)factory );

    if (*factory) return S_OK;

    /*
     * The remaining Windows.UI.Composition classes are not activatable (they are
     * created through the Compositor factory). Report E_NOINTERFACE, matching the
     * value generated C++/WinRT factories report for classes they do not implement:
     * the Windows App SDK chains its activation probes and only falls through to
     * its own factories on E_NOINTERFACE.
     */
    return E_NOINTERFACE;
}

HRESULT WINAPI DllRegisterServer( void )
{
    static const struct
    {
        const WCHAR *class_name;
        const WCHAR *dll_path;
    }
    classes[] =
    {
        { L"Windows.UI.Composition.Compositor", L"system32\\windows.ui.composition.dll" },
    };
    unsigned int i;

    for (i = 0; i < ARRAYSIZE( classes ); i++)
    {
        HKEY hkey;
        LONG ret;

        ret = RegCreateKeyExW( HKEY_LOCAL_MACHINE,
                L"Software\\Microsoft\\WindowsRuntime\\ActivatableClassId", 0, NULL,
                REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hkey, NULL );
        if (ret) return HRESULT_FROM_WIN32( ret );
        ret = RegCreateKeyExW( hkey, classes[i].class_name, 0, NULL, REG_OPTION_NON_VOLATILE,
                KEY_WRITE, NULL, &hkey, NULL );
        RegCloseKey( hkey );
        if (ret) return HRESULT_FROM_WIN32( ret );
        ret = RegSetValueExW( hkey, L"DllPath", 0, REG_EXPAND_SZ, (const BYTE *)classes[i].dll_path,
                (lstrlenW( classes[i].dll_path ) + 1) * sizeof(WCHAR) );
        RegCloseKey( hkey );
        if (ret) return HRESULT_FROM_WIN32( ret );
    }
    return S_OK;
}

HRESULT WINAPI DllUnregisterServer( void )
{
    static const WCHAR path[] = L"Software\\Microsoft\\WindowsRuntime\\ActivatableClassId\\Windows.UI.Composition.Compositor";

    return HRESULT_FROM_WIN32( RegDeleteKeyW( HKEY_LOCAL_MACHINE, path ) );
}
