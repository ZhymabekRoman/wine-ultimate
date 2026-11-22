/*
 * Copyright 2022 Zhiyi Zhang for CodeWeavers
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
#include <initguid.h>
#include <windef.h>
#include <winbase.h>
#include <winstring.h>
#include <activation.h>
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(winsock);

static const GUID IID_IProximityDeviceStatics =
    {0x914ba01d, 0xf6e1, 0x47c4, {0xa1, 0x4c, 0x14, 0x8a, 0x19, 0x03, 0xd0, 0xc6}};

static HRESULT WINAPI prox_QueryInterface( void *iface, REFIID riid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( riid, &IID_IUnknown ) ||
        IsEqualGUID( riid, &IID_IInspectable ) ||
        IsEqualGUID( riid, &IID_IAgileObject ) ||
        IsEqualGUID( riid, &IID_IActivationFactory ) ||
        IsEqualGUID( riid, &IID_IProximityDeviceStatics ))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI prox_AddRef( void *iface ) { return 2; }
static ULONG WINAPI prox_Release( void *iface ) { return 1; }
static HRESULT WINAPI prox_GetIids( void *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}
static HRESULT WINAPI prox_GetRuntimeClassName( void *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.Networking.Proximity.ProximityDevice", 44, name );
}
static HRESULT WINAPI prox_GetTrustLevel( void *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}
static HRESULT WINAPI prox_GetDeviceSelector( void *iface, HSTRING *value )
{
    if (!value) return E_POINTER;
    return WindowsCreateString( L"", 0, value );
}
static HRESULT WINAPI prox_GetDefault( void *iface, void **value )
{
    if (!value) return E_POINTER;
    *value = NULL;
    return S_OK;
}
static HRESULT WINAPI prox_FromId( void *iface, HSTRING device_id, void **value )
{
    if (!value) return E_POINTER;
    *value = NULL;
    return S_OK;
}
static const void *prox_statics_vtbl[] =
{
    prox_QueryInterface,
    prox_AddRef,
    prox_Release,
    prox_GetIids,
    prox_GetRuntimeClassName,
    prox_GetTrustLevel,
    prox_GetDeviceSelector,
    prox_GetDefault,
    prox_FromId,
};
static struct { const void **vtbl; } prox_statics_obj = { prox_statics_vtbl };

HRESULT WINAPI SetSocketMediaStreamingMode(BOOL value)
{
    FIXME("value %d stub!\n", value);
    return S_OK;
}

HRESULT WINAPI DllGetActivationFactory(HSTRING classid, void **factory)
{
    const WCHAR *name = WindowsGetStringRawBuffer(classid, NULL);
    TRACE("class %s, factory %p.\n", debugstr_hstring(classid), factory);

    if (!factory) return E_POINTER;
    *factory = NULL;

    if (name && !wcscmp(name, L"Windows.Networking.Proximity.ProximityDevice"))
    {
        *factory = &prox_statics_obj;
        return S_OK;
    }

    FIXME("class %s, factory %p.\n", debugstr_hstring(classid), factory);
    return E_NOINTERFACE;
}
