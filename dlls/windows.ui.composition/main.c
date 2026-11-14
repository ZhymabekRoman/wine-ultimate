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

static const GUID IID_ICompositionEffectSourceParameterFactory_local =
    { 0xb3d9f276, 0xaba3, 0x4724, { 0xac, 0xf3, 0xd0, 0x39, 0x74, 0x64, 0xdb, 0x1c } };
static const GUID IID_ICompositionEffectSourceParameter_local =
    { 0x859e13ec, 0x6952, 0x4d48, { 0x96, 0x47, 0x3f, 0x7e, 0x8e, 0x5d, 0x06, 0x7c } };
static const GUID IID_IGraphicsEffectSource_local =
    { 0x2d8f9ddc, 0x4339, 0x4eb9, { 0x92, 0x16, 0xf9, 0xde, 0xb7, 0x56, 0x58, 0xa2 } };

struct cesp_obj
{
    const void *vtbl;
    LONG ref;
    HSTRING name;
};

static HRESULT WINAPI cesp_QI( void *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_ICompositionEffectSourceParameter_local ) ||
        IsEqualGUID( iid, &IID_IGraphicsEffectSource_local ))
    {
        *out = iface;
        InterlockedIncrement( &((struct cesp_obj *)iface)->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI cesp_AddRef( void *iface ) { return InterlockedIncrement( &((struct cesp_obj *)iface)->ref ); }
static ULONG WINAPI cesp_Release( void *iface )
{
    struct cesp_obj *obj = iface;
    ULONG r = InterlockedDecrement( &obj->ref );
    if (!r) { if (obj->name) WindowsDeleteString( obj->name ); free( obj ); }
    return r;
}
static HRESULT WINAPI cesp_GetIids( void *iface, ULONG *cnt, IID **iids )
{
    if (cnt) *cnt = 0;
    if (iids) *iids = NULL;
    return S_OK;
}
static HRESULT WINAPI cesp_GetRuntimeClassName( void *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.UI.Composition.CompositionEffectSourceParameter", 55, name );
}
static HRESULT WINAPI cesp_GetTrustLevel( void *iface, TrustLevel *lvl )
{
    if (lvl) *lvl = BaseTrust;
    return S_OK;
}
static HRESULT WINAPI cesp_get_Name( void *iface, HSTRING *name )
{
    struct cesp_obj *obj = iface;
    if (!name) return E_POINTER;
    return WindowsDuplicateString( obj->name, name );
}
static const void *cesp_vtbl[] =
{
    cesp_QI, cesp_AddRef, cesp_Release,
    cesp_GetIids, cesp_GetRuntimeClassName, cesp_GetTrustLevel,
    cesp_get_Name,
};

static HRESULT WINAPI cesp_factory_QI( IActivationFactory *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ) ||
        IsEqualGUID( iid, &IID_ICompositionEffectSourceParameterFactory_local ))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI cesp_factory_AddRef( IActivationFactory *iface ) { return 2; }
static ULONG WINAPI cesp_factory_Release( IActivationFactory *iface ) { return 1; }
static HRESULT WINAPI cesp_factory_GetIids( IActivationFactory *iface, ULONG *cnt, IID **iids )
{
    if (cnt) *cnt = 0;
    if (iids) *iids = NULL;
    return S_OK;
}
static HRESULT WINAPI cesp_factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.UI.Composition.CompositionEffectSourceParameter", 55, name );
}
static HRESULT WINAPI cesp_factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *lvl )
{
    if (lvl) *lvl = BaseTrust;
    return S_OK;
}
static HRESULT WINAPI cesp_factory_Create( IActivationFactory *iface, HSTRING name, void **out )
{
    struct cesp_obj *obj;
    if (!out) return E_POINTER;
    obj = calloc( 1, sizeof(*obj) );
    if (!obj) return E_OUTOFMEMORY;
    obj->vtbl = cesp_vtbl;
    obj->ref = 1;
    if (name) WindowsDuplicateString( name, &obj->name );
    *out = obj;
    return S_OK;
}
static const void *cesp_factory_vtbl[] =
{
    cesp_factory_QI, cesp_factory_AddRef, cesp_factory_Release,
    cesp_factory_GetIids, cesp_factory_GetRuntimeClassName, cesp_factory_GetTrustLevel,
    cesp_factory_Create,
};
static struct { const void **vtbl; } cesp_factory_obj = { cesp_factory_vtbl };

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
    else if (name && !wcscmp( name, L"Windows.UI.Composition.CompositionEffectSourceParameter" ))
        *factory = (IActivationFactory *)&cesp_factory_obj;

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
