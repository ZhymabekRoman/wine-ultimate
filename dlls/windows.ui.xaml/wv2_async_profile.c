/* WinRT Windows.UI.Xaml.Controls.WebView2 bridge support
 *
 * Copyright (C) 2026 Wine project
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
#include <stdio.h>

#include "private.h"
#include "winreg.h"
#include "oleauto.h"
#include "weakreference.h"

#include "wine/exception.h"

WINE_DEFAULT_DEBUG_CHANNEL(xaml);
#include "uwp_private.h"
#include "wv2_private.h"


static inline struct cwv2_settings *impl_from_settings( void *iface )
{
    return CONTAINING_RECORD( iface, struct cwv2_settings, IInspectable_iface );
}
static inline struct cwv2_settings *impl_from_settings2( void *iface )
{
    return CONTAINING_RECORD( iface, struct cwv2_settings, ISettings2_iface );
}
static inline struct cwv2_settings *impl_from_settings_stub( void *iface )
{
    return CONTAINING_RECORD( iface, struct cwv2_settings, ISettings_stub_iface );
}

static HRESULT WINAPI cwv2_settings_QueryInterface( IInspectable *iface, REFIID iid, void **out )
{
    struct cwv2_settings *impl = impl_from_settings( iface );
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_ICoreWebView2Settings ))
    {
        *out = &impl->IInspectable_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_ICoreWebView2Settings2 ))
    {
        *out = &impl->ISettings2_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_ICoreWebView2Settings3 ) ||
        IsEqualGUID( iid, &IID_ICoreWebView2Settings4 ) ||
        IsEqualGUID( iid, &IID_ICoreWebView2Settings5 ) ||
        IsEqualGUID( iid, &IID_ICoreWebView2Settings6 ) ||
        IsEqualGUID( iid, &IID_ICoreWebView2Settings7 ) ||
        IsEqualGUID( iid, &IID_ICoreWebView2Settings8 ) ||
        IsEqualGUID( iid, &IID_ICoreWebView2Settings9 ) ||
        IsEqualGUID( iid, &IID_ICoreWebView2Settings_Manual ))
    {
        *out = &impl->ISettings_stub_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    ERR( "cwv2_settings_QueryInterface: unknown IID %s\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI cwv2_settings_AddRef( IInspectable *iface )
{
    struct cwv2_settings *impl = impl_from_settings( iface );
    return InterlockedIncrement( &impl->ref );
}
static ULONG WINAPI cwv2_settings_Release( IInspectable *iface )
{
    struct cwv2_settings *impl = impl_from_settings( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        if (impl->loader_settings2) ((IUnknown *)impl->loader_settings2)->lpVtbl->Release( impl->loader_settings2 );
        if (impl->loader_settings) ((IUnknown *)impl->loader_settings)->lpVtbl->Release( impl->loader_settings );
        free( impl );
    }
    return ref;
}
static HRESULT WINAPI cwv2_settings2_QueryInterface( IInspectable *iface, REFIID iid, void **out )
{
    return cwv2_settings_QueryInterface( &impl_from_settings2( iface )->IInspectable_iface, iid, out );
}
static ULONG WINAPI cwv2_settings2_AddRef( IInspectable *iface )
{
    return cwv2_settings_AddRef( &impl_from_settings2( iface )->IInspectable_iface );
}
static ULONG WINAPI cwv2_settings2_Release( IInspectable *iface )
{
    return cwv2_settings_Release( &impl_from_settings2( iface )->IInspectable_iface );
}
static HRESULT WINAPI cwv2_settings_stub_QueryInterface( IInspectable *iface, REFIID iid, void **out )
{
    return cwv2_settings_QueryInterface( &impl_from_settings_stub( iface )->IInspectable_iface, iid, out );
}
static ULONG WINAPI cwv2_settings_stub_AddRef( IInspectable *iface )
{
    return cwv2_settings_AddRef( &impl_from_settings_stub( iface )->IInspectable_iface );
}
static ULONG WINAPI cwv2_settings_stub_Release( IInspectable *iface )
{
    return cwv2_settings_Release( &impl_from_settings_stub( iface )->IInspectable_iface );
}
static HRESULT WINAPI cwv2_settings_GetIids( IInspectable *iface, ULONG *count, IID **iids )
{
    IID *arr;
    if (!(arr = CoTaskMemAlloc( sizeof(IID) ))) return E_OUTOFMEMORY;
    *arr = IID_ICoreWebView2Settings;
    if (count) *count = 1;
    if (iids) *iids = arr; else CoTaskMemFree( arr );
    return S_OK;
}
static HRESULT WINAPI cwv2_settings_GetRuntimeClassName( IInspectable *iface, HSTRING *name )
{ return WindowsCreateString( L"Microsoft.Web.WebView2.Core.CoreWebView2Settings", 47, name ); }
static HRESULT WINAPI cwv2_settings_GetTrustLevel( IInspectable *iface, TrustLevel *level )
{ if (level) *level = BaseTrust; return S_OK; }

static HRESULT settings_get_bool( struct cwv2_settings *impl, int com_slot, boolean *val )
{
    BOOL b = TRUE;
    if (!val) return E_POINTER;
    if (impl->loader_settings && *(void ***)impl->loader_settings)
    {
        HRESULT (WINAPI *fn)( void *, BOOL * ) = (*(void ***)impl->loader_settings)[com_slot];
        if (fn && (ULONG_PTR)fn < (ULONG_PTR)0x7ffc00000000)
        {
            HRESULT hr = fn( impl->loader_settings, &b );
            *val = !!b;
            return hr;
        }
    }
    *val = TRUE;
    return S_OK;
}
static HRESULT settings_put_bool( struct cwv2_settings *impl, int com_slot, boolean val )
{
    if (impl->loader_settings && *(void ***)impl->loader_settings)
    {
        HRESULT (WINAPI *fn)( void *, BOOL ) = (*(void ***)impl->loader_settings)[com_slot];
        if (fn && (ULONG_PTR)fn < (ULONG_PTR)0x7ffc00000000)
            return fn( impl->loader_settings, !!val );
    }
    if (impl->owner)
        cwv2_queue_op( impl->owner, CWV2_OP_SETTING_BOOL, com_slot, !!val, NULL, NULL, NULL, NULL, NULL );
    return S_OK;
}

#define SETTINGS_BOOL_PAIR(name, com_get_slot, com_put_slot) \
    static HRESULT WINAPI cwv2_settings_get_##name( void *iface, boolean *val ) \
    { return settings_get_bool( impl_from_settings( iface ), com_get_slot, val ); } \
    static HRESULT WINAPI cwv2_settings_put_##name( void *iface, boolean val ) \
    { return settings_put_bool( impl_from_settings( iface ), com_put_slot, val ); }

SETTINGS_BOOL_PAIR( IsScriptEnabled, 3, 4 )
SETTINGS_BOOL_PAIR( IsWebMessageEnabled, 5, 6 )
SETTINGS_BOOL_PAIR( AreDefaultScriptDialogsEnabled, 7, 8 )
SETTINGS_BOOL_PAIR( IsStatusBarEnabled, 9, 10 )
SETTINGS_BOOL_PAIR( AreDevToolsEnabled, 11, 12 )
SETTINGS_BOOL_PAIR( AreDefaultContextMenusEnabled, 13, 14 )
SETTINGS_BOOL_PAIR( AreHostObjectsAllowed, 15, 16 )
SETTINGS_BOOL_PAIR( IsZoomControlEnabled, 17, 18 )
SETTINGS_BOOL_PAIR( IsBuiltInErrorPageEnabled, 19, 20 )

static HRESULT WINAPI cwv2_settings2_get_UserAgent( void *iface, HSTRING *ua )
{
    struct cwv2_settings *impl = impl_from_settings2( iface );
    if (!ua) return E_POINTER;
    if (impl->loader_settings2 && *(void ***)impl->loader_settings2)
    {
        HRESULT (WINAPI *fn)( void *, LPWSTR * ) = (*(void ***)impl->loader_settings2)[21];
        if (fn && (ULONG_PTR)fn < (ULONG_PTR)0x7ffc00000000)
        {
            LPWSTR raw = NULL;
            HRESULT hr = fn( impl->loader_settings2, &raw );
            if (SUCCEEDED( hr ) && raw)
            {
                hr = WindowsCreateString( raw, lstrlenW( raw ), ua );
                CoTaskMemFree( raw );
                return hr;
            }
            if (raw) CoTaskMemFree( raw );
            return hr;
        }
    }
    return WindowsCreateString( L"", 0, ua );
}

static HRESULT WINAPI cwv2_settings2_put_UserAgent( void *iface, HSTRING ua )
{
    struct cwv2_settings *impl = impl_from_settings2( iface );
    const WCHAR *s = ua ? WindowsGetStringRawBuffer( ua, NULL ) : L"";
    ERR( "cwv2_settings2_put_UserAgent: %s\n", debugstr_w( s ) );
    if (impl->loader_settings2 && *(void ***)impl->loader_settings2)
    {
        HRESULT (WINAPI *fn)( void *, LPCWSTR ) = (*(void ***)impl->loader_settings2)[22];
        if (fn && (ULONG_PTR)fn < (ULONG_PTR)0x7ffc00000000)
            return fn( impl->loader_settings2, s );
    }
    if (impl->owner)
        cwv2_queue_op( impl->owner, CWV2_OP_SETTING_UA, 0, FALSE, s, NULL, NULL, NULL, NULL );
    return S_OK;
}

const void *cwv2_settings_vtbl[256] =
{
    [0] = cwv2_settings_QueryInterface,
    [1] = cwv2_settings_AddRef,
    [2] = cwv2_settings_Release,
    [3] = cwv2_settings_GetIids,
    [4] = cwv2_settings_GetRuntimeClassName,
    [5] = cwv2_settings_GetTrustLevel,
    [24 ... 255] = element_generic_slot,
    [6] = cwv2_settings_get_IsScriptEnabled,
    [7] = cwv2_settings_put_IsScriptEnabled,
    [8] = cwv2_settings_get_IsWebMessageEnabled,
    [9] = cwv2_settings_put_IsWebMessageEnabled,
    [10] = cwv2_settings_get_AreDefaultScriptDialogsEnabled,
    [11] = cwv2_settings_put_AreDefaultScriptDialogsEnabled,
    [12] = cwv2_settings_get_IsStatusBarEnabled,
    [13] = cwv2_settings_put_IsStatusBarEnabled,
    [14] = cwv2_settings_get_AreDevToolsEnabled,
    [15] = cwv2_settings_put_AreDevToolsEnabled,
    [16] = cwv2_settings_get_AreDefaultContextMenusEnabled,
    [17] = cwv2_settings_put_AreDefaultContextMenusEnabled,
    [18] = cwv2_settings_get_AreHostObjectsAllowed,
    [19] = cwv2_settings_put_AreHostObjectsAllowed,
    [20] = cwv2_settings_get_IsZoomControlEnabled,
    [21] = cwv2_settings_put_IsZoomControlEnabled,
    [22] = cwv2_settings_get_IsBuiltInErrorPageEnabled,
    [23] = cwv2_settings_put_IsBuiltInErrorPageEnabled,
};

const void *cwv2_settings2_vtbl[256] =
{
    [0] = cwv2_settings2_QueryInterface,
    [1] = cwv2_settings2_AddRef,
    [2] = cwv2_settings2_Release,
    [3] = cwv2_settings_GetIids,
    [4] = cwv2_settings_GetRuntimeClassName,
    [5] = cwv2_settings_GetTrustLevel,
    [8 ... 255] = element_generic_slot,
    [6] = cwv2_settings2_get_UserAgent,
    [7] = cwv2_settings2_put_UserAgent,
};

const void *cwv2_settings_stub_vtbl[256] =
{
    [0] = cwv2_settings_stub_QueryInterface,
    [1] = cwv2_settings_stub_AddRef,
    [2] = cwv2_settings_stub_Release,
    [3] = cwv2_settings_GetIids,
    [4] = cwv2_settings_GetRuntimeClassName,
    [5] = cwv2_settings_GetTrustLevel,
    [6 ... 255] = element_generic_slot,
};


static HRESULT WINAPI cwv2_env_QueryInterface( IInspectable *iface, REFIID iid, void **out )
{
    static const GUID IID_ICoreWebView2Environment_rt =
        { 0xd8cc7831, 0xb783, 0x556b, { 0xb9, 0xce, 0x89, 0x9c, 0x1e, 0x95, 0xd5, 0x85 } };
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_ICoreWebView2Environment ) ||
        IsEqualGUID( iid, &IID_ICoreWebView2Environment_rt ))
    {
        *out = iface;
        IInspectable_AddRef( iface );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI cwv2_env_AddRef( IInspectable *iface )
{
    struct cwv2_env *impl = (struct cwv2_env *)iface;
    return InterlockedIncrement( &impl->ref );
}
static ULONG WINAPI cwv2_env_Release( IInspectable *iface )
{
    struct cwv2_env *impl = (struct cwv2_env *)iface;
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref) free( impl );
    return ref;
}
static HRESULT WINAPI cwv2_env_GetIids( IInspectable *iface, ULONG *count, IID **iids )
{ if (count) *count = 0; if (iids) *iids = NULL; return S_OK; }
static HRESULT WINAPI cwv2_env_GetRuntimeClassName( IInspectable *iface, HSTRING *name )
{ return WindowsCreateString( L"Microsoft.Web.WebView2.Core.CoreWebView2Environment", 50, name ); }
static HRESULT WINAPI cwv2_env_GetTrustLevel( IInspectable *iface, TrustLevel *level )
{ if (level) *level = BaseTrust; return S_OK; }

const void *cwv2_env_vtbl[256] =
{
    [0] = cwv2_env_QueryInterface,
    [1] = cwv2_env_AddRef,
    [2] = cwv2_env_Release,
    [3] = cwv2_env_GetIids,
    [4] = cwv2_env_GetRuntimeClassName,
    [5] = cwv2_env_GetTrustLevel,
    [6 ... 255] = element_generic_slot,
};

struct cwv2_env_asyncop
{
    IInspectable IInspectable_iface;
    IAsyncInfo IAsyncInfo_iface;
    LONG ref;
    void *handler;
    IInspectable *result;
};

static inline struct cwv2_env_asyncop *impl_from_env_asyncop( void *iface )
{
    return CONTAINING_RECORD( iface, struct cwv2_env_asyncop, IInspectable_iface );
}
static inline struct cwv2_env_asyncop *impl_from_env_asyncop_info( void *iface )
{
    return CONTAINING_RECORD( iface, struct cwv2_env_asyncop, IAsyncInfo_iface );
}
static HRESULT WINAPI env_asyncop_QueryInterface( void *iface, REFIID iid, void **obj )
{
    struct cwv2_env_asyncop *impl = impl_from_env_asyncop( iface );
    if (!obj) return E_POINTER;
    *obj = NULL;
    if (!iid || IsEqualGUID( iid, &IID_IMarshal ) || IsEqualGUID( iid, &IID_IWeakReferenceSource ) ||
        IsEqualGUID( iid, &IID_IReferenceTracker ) || IsEqualGUID( iid, &IID_IReferenceTrackerTarget ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget_WinUI ))
        return E_NOINTERFACE;
    if (IsEqualGUID( iid, &IID_IAsyncInfo ))
    {
        *obj = &impl->IAsyncInfo_iface;
        IInspectable_AddRef( &impl->IInspectable_iface );
        return S_OK;
    }
    *obj = &impl->IInspectable_iface;
    IInspectable_AddRef( (IInspectable *)*obj );
    return S_OK;
}
static ULONG WINAPI env_asyncop_AddRef( void *iface )
{
    return InterlockedIncrement( &impl_from_env_asyncop( iface )->ref );
}
static ULONG WINAPI env_asyncop_Release( void *iface )
{
    struct cwv2_env_asyncop *impl = impl_from_env_asyncop( iface );
    LONG r = InterlockedDecrement( &impl->ref );
    if (!r)
    {
        if (impl->result) IInspectable_Release( impl->result );
        if (impl->handler) ((IInspectable *)impl->handler)->lpVtbl->Release( impl->handler );
        free( impl );
    }
    return r;
}
static HRESULT WINAPI env_asyncop_GetIids( void *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}
static HRESULT WINAPI env_asyncop_GetRuntimeClassName( void *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.Foundation.IAsyncOperation`1<Microsoft.Web.WebView2.Core.CoreWebView2Environment>", 87, name );
}
static HRESULT WINAPI env_asyncop_GetTrustLevel( void *iface, TrustLevel *lvl )
{
    if (lvl) *lvl = BaseTrust;
    return S_OK;
}
static HRESULT WINAPI env_asyncop_put_Completed( void *iface, void *handler )
{
    struct cwv2_env_asyncop *impl = impl_from_env_asyncop( iface );
    if (impl->handler) ((IInspectable *)impl->handler)->lpVtbl->Release( impl->handler );
    impl->handler = handler;
    if (handler)
    {
        HRESULT (WINAPI *invoke)( void *, void *, int ) = (*(void ***)handler)[3];
        ((IInspectable *)handler)->lpVtbl->AddRef( handler );
        if (invoke) invoke( handler, iface, 1 /* Completed */ );
    }
    return S_OK;
}
static HRESULT WINAPI env_asyncop_get_Completed( void *iface, void **handler )
{
    struct cwv2_env_asyncop *impl = impl_from_env_asyncop( iface );
    if (!handler) return E_POINTER;
    *handler = impl->handler;
    if (*handler) ((IInspectable *)*handler)->lpVtbl->AddRef( *handler );
    return S_OK;
}
static HRESULT WINAPI env_asyncop_GetResults( void *iface, void **out )
{
    struct cwv2_env_asyncop *impl = impl_from_env_asyncop( iface );
    if (!out) return E_POINTER;
    *out = impl->result;
    if (*out) IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}
static const void *env_asyncop_vtbl[] =
{
    env_asyncop_QueryInterface,
    env_asyncop_AddRef,
    env_asyncop_Release,
    env_asyncop_GetIids,
    env_asyncop_GetRuntimeClassName,
    env_asyncop_GetTrustLevel,
    env_asyncop_put_Completed,
    env_asyncop_get_Completed,
    env_asyncop_GetResults,
};
static HRESULT WINAPI env_asyncop_info_QI( IAsyncInfo *iface, REFIID iid, void **obj )
{
    return env_asyncop_QueryInterface( &impl_from_env_asyncop_info( iface )->IInspectable_iface, iid, obj );
}
static ULONG WINAPI env_asyncop_info_AddRef( IAsyncInfo *iface )
{
    return env_asyncop_AddRef( &impl_from_env_asyncop_info( iface )->IInspectable_iface );
}
static ULONG WINAPI env_asyncop_info_Release( IAsyncInfo *iface )
{
    return env_asyncop_Release( &impl_from_env_asyncop_info( iface )->IInspectable_iface );
}
static HRESULT WINAPI env_asyncop_info_get_Id( IAsyncInfo *iface, UINT32 *id ) { if (id) *id = 1; return S_OK; }
static HRESULT WINAPI env_asyncop_info_get_Status( IAsyncInfo *iface, AsyncStatus *s ) { if (s) *s = 1; return S_OK; }
static HRESULT WINAPI env_asyncop_info_get_ErrorCode( IAsyncInfo *iface, HRESULT *c ) { if (c) *c = S_OK; return S_OK; }
static HRESULT WINAPI env_asyncop_info_Cancel( IAsyncInfo *iface ) { return S_OK; }
static HRESULT WINAPI env_asyncop_info_Close( IAsyncInfo *iface ) { return S_OK; }
static const struct IAsyncInfoVtbl env_asyncop_info_vtbl =
{
    env_asyncop_info_QI, env_asyncop_info_AddRef, env_asyncop_info_Release,
    (void *)env_asyncop_GetIids, (void *)env_asyncop_GetRuntimeClassName, (void *)env_asyncop_GetTrustLevel,
    env_asyncop_info_get_Id, env_asyncop_info_get_Status, env_asyncop_info_get_ErrorCode,
    env_asyncop_info_Cancel, env_asyncop_info_Close,
};

static HRESULT cwv2_env_create_op( void **op )
{
    struct cwv2_env *e;
    struct cwv2_env_asyncop *a;
    if (!op) return E_POINTER;
    if (!(e = calloc( 1, sizeof(*e) ))) return E_OUTOFMEMORY;
    e->IInspectable_iface.lpVtbl = (const struct IInspectableVtbl *)vtbl_dup( cwv2_env_vtbl );
    e->ref = 1;
    if (!(a = calloc( 1, sizeof(*a) ))) { IInspectable_Release( &e->IInspectable_iface ); return E_OUTOFMEMORY; }
    a->IInspectable_iface.lpVtbl = (const struct IInspectableVtbl *)env_asyncop_vtbl;
    a->IAsyncInfo_iface.lpVtbl = &env_asyncop_info_vtbl;
    a->ref = 1;
    a->result = &e->IInspectable_iface;
    *op = &a->IInspectable_iface;
    return S_OK;
}

static HRESULT WINAPI cwv2_env_factory_QI( IActivationFactory *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IMarshal ) || IsEqualGUID( iid, &IID_IWeakReferenceSource ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    *out = iface;
    IActivationFactory_AddRef( iface );
    return S_OK;
}
static ULONG WINAPI cwv2_env_factory_AddRef( IActivationFactory *iface ) { return 2; }
static ULONG WINAPI cwv2_env_factory_Release( IActivationFactory *iface ) { return 1; }
static HRESULT WINAPI cwv2_env_factory_GetIids( IActivationFactory *iface, ULONG *cnt, IID **iids )
{ if (cnt) *cnt = 0; if (iids) *iids = NULL; return S_OK; }
static HRESULT WINAPI cwv2_env_factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *name )
{ return WindowsCreateString( L"Microsoft.Web.WebView2.Core.CoreWebView2Environment", 51, name ); }
static HRESULT WINAPI cwv2_env_factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *lvl )
{ if (lvl) *lvl = BaseTrust; return S_OK; }
static HRESULT WINAPI cwv2_env_factory_CreateAsync( void *iface, void **op )
{
    ERR( "cwv2_env_factory_CreateAsync op=%p\n", op );
    return cwv2_env_create_op( op );
}
static HRESULT WINAPI cwv2_env_factory_CreateWithOptionsAsync( void *iface, HSTRING browser, HSTRING udf, void *opts, void **op )
{
    ERR( "cwv2_env_factory_CreateWithOptionsAsync udf=%s op=%p\n", debugstr_hstring( udf ), op );
    return cwv2_env_create_op( op );
}
static HRESULT WINAPI cwv2_env_factory_GetAvailableBrowserVersionString( void *iface, HSTRING *ver )
{
    return WindowsCreateString( L"156.0.4314.42", 13, ver );
}
static HRESULT WINAPI cwv2_env_factory_GetAvailableBrowserVersionString2( void *iface, HSTRING browser, HSTRING *ver )
{
    return WindowsCreateString( L"156.0.4314.42", 13, ver );
}
static HRESULT WINAPI cwv2_env_factory_CompareBrowserVersionString( void *iface, HSTRING v1, HSTRING v2, INT32 *res )
{
    if (res) *res = 0;
    return S_OK;
}
static const void *cwv2_env_factory_vtbl[] =
{
    cwv2_env_factory_QI,
    cwv2_env_factory_AddRef,
    cwv2_env_factory_Release,
    cwv2_env_factory_GetIids,
    cwv2_env_factory_GetRuntimeClassName,
    cwv2_env_factory_GetTrustLevel,
    cwv2_env_factory_CreateAsync,
    cwv2_env_factory_CreateWithOptionsAsync,
    cwv2_env_factory_GetAvailableBrowserVersionString,
    cwv2_env_factory_GetAvailableBrowserVersionString2,
    cwv2_env_factory_CompareBrowserVersionString,
};
static struct { const void **vtbl; } cwv2_env_factory_obj = { cwv2_env_factory_vtbl };
IActivationFactory *cwv2_env_factory = (IActivationFactory *)&cwv2_env_factory_obj;

static inline struct cwv2_asyncop *impl_from_asyncop( void *iface )
{
    return CONTAINING_RECORD( iface, struct cwv2_asyncop, IInspectable_iface );
}
static inline struct cwv2_asyncop *impl_from_asyncop_info( void *iface )
{
    return CONTAINING_RECORD( iface, struct cwv2_asyncop, IAsyncInfo_iface );
}

static HRESULT WINAPI asyncop_QueryInterface( void *iface, REFIID iid, void **obj )
{
    struct cwv2_asyncop *impl = impl_from_asyncop( iface );
    *obj = NULL;
    if (!iid) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IAsyncOperation_HSTRING ))
    {
        *obj = &impl->IInspectable_iface;
        IInspectable_AddRef( (IInspectable *)*obj );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IAsyncInfo ))
    {
        *obj = &impl->IAsyncInfo_iface;
        IInspectable_AddRef( (IInspectable *)&impl->IInspectable_iface );
        return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG WINAPI asyncop_AddRef( void *iface )
{
    return InterlockedIncrement( &impl_from_asyncop( iface )->ref );
}
ULONG WINAPI asyncop_Release( void *iface )
{
    struct cwv2_asyncop *impl = impl_from_asyncop( iface );
    LONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        if (impl->result) WindowsDeleteString( impl->result );
        if (impl->handler) ((IInspectable *)impl->handler)->lpVtbl->Release( impl->handler );
        free( impl );
    }
    return ref;
}
static HRESULT WINAPI asyncop_GetResults( void *iface, HSTRING *s )
{
    struct cwv2_asyncop *impl = impl_from_asyncop( iface );
    if (!s) return E_POINTER;
    if (FAILED( impl->hr )) return impl->hr;
    if (impl->result) return WindowsDuplicateString( impl->result, s );
    return WindowsCreateString( L"{}", 2, s );
}
static HRESULT WINAPI asyncop_put_Completed( void *iface, void *handler )
{
    struct cwv2_asyncop *impl = impl_from_asyncop( iface );
    struct cwv2_action_completed_handler *h = (struct cwv2_action_completed_handler *)handler;
    if (impl->handler) ((IInspectable *)impl->handler)->lpVtbl->Release( impl->handler );
    impl->handler = handler;
    if (handler)
    {
        ((IInspectable *)handler)->lpVtbl->AddRef( handler );
        if (impl->completed)
            h->lpVtbl->Invoke( h, iface, FAILED( impl->hr ) ? Error : Completed );
    }
    return S_OK;
}
static HRESULT WINAPI asyncop_get_Completed( void *iface, void **handler )
{
    struct cwv2_asyncop *impl = impl_from_asyncop( iface );
    if (!handler) return E_POINTER;
    *handler = impl->handler;
    if (*handler) ((IInspectable *)*handler)->lpVtbl->AddRef( *handler );
    return S_OK;
}

static HRESULT WINAPI asyncop_info_QueryInterface( IAsyncInfo *iface, REFIID iid, void **obj )
{
    return asyncop_QueryInterface( &impl_from_asyncop_info( iface )->IInspectable_iface, iid, obj );
}
static ULONG WINAPI asyncop_info_AddRef( IAsyncInfo *iface )
{
    return asyncop_AddRef( &impl_from_asyncop_info( iface )->IInspectable_iface );
}
static ULONG WINAPI asyncop_info_Release( IAsyncInfo *iface )
{
    return asyncop_Release( &impl_from_asyncop_info( iface )->IInspectable_iface );
}
static HRESULT WINAPI asyncop_info_GetIids( IAsyncInfo *iface, ULONG *count, IID **iids )
{
    return cwv2_GetIids( &impl_from_asyncop_info( iface )->IInspectable_iface, count, iids );
}
static HRESULT WINAPI asyncop_info_GetRuntimeClassName( IAsyncInfo *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.Foundation.IAsyncOperation`1<String>", 44, name );
}
static HRESULT WINAPI asyncop_info_GetTrustLevel( IAsyncInfo *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}
static HRESULT WINAPI asyncop_info_get_Id( IAsyncInfo *iface, UINT32 *id )
{
    if (id) *id = 1;
    return S_OK;
}
static HRESULT WINAPI asyncop_info_get_Status( IAsyncInfo *iface, AsyncStatus *status )
{
    struct cwv2_asyncop *impl = impl_from_asyncop_info( iface );
    if (status)
        *status = impl->completed ? (FAILED( impl->hr ) ? Error : Completed) : Started;
    return S_OK;
}
static HRESULT WINAPI asyncop_info_get_ErrorCode( IAsyncInfo *iface, HRESULT *err )
{
    struct cwv2_asyncop *impl = impl_from_asyncop_info( iface );
    if (err) *err = impl->hr;
    return S_OK;
}
static HRESULT WINAPI asyncop_info_Cancel( IAsyncInfo *iface ) { return S_OK; }
static HRESULT WINAPI asyncop_info_Close( IAsyncInfo *iface ) { return S_OK; }

static const IAsyncInfoVtbl cwv2_asyncop_info_vtbl =
{
    asyncop_info_QueryInterface,
    asyncop_info_AddRef,
    asyncop_info_Release,
    asyncop_info_GetIids,
    asyncop_info_GetRuntimeClassName,
    asyncop_info_GetTrustLevel,
    asyncop_info_get_Id,
    asyncop_info_get_Status,
    asyncop_info_get_ErrorCode,
    asyncop_info_Cancel,
    asyncop_info_Close,
};

static const void *cwv2_asyncop_vtbl[256] =
{
    [0] = asyncop_QueryInterface,
    [1] = asyncop_AddRef,
    [2] = asyncop_Release,
    [3] = cwv2_GetIids,             /* IInspectable: GetIids/GetRuntimeClassName/GetTrustLevel */
    [4] = cwv2_GetRuntimeClassName,
    [5] = cwv2_GetTrustLevel,
    [6] = asyncop_put_Completed,
    [7] = asyncop_get_Completed,
    [8] = asyncop_GetResults,
};

/* Create an async-op carrying `result` (may be NULL -> "{}"). */
HRESULT cwv2_asyncop_create_ex( HSTRING result, BOOL completed, struct cwv2_asyncop **out_impl )
{
    struct cwv2_asyncop *op = calloc( 1, sizeof(*op) );
    if (!op) return E_OUTOFMEMORY;
    op->IInspectable_iface.lpVtbl = (const struct IInspectableVtbl *)vtbl_dup( cwv2_asyncop_vtbl );
    op->IAsyncInfo_iface.lpVtbl = &cwv2_asyncop_info_vtbl;
    op->ref = 1;
    op->completed = completed;
    op->hr = S_OK;
    if (result) WindowsDuplicateString( result, &op->result );
    *out_impl = op;
    return S_OK;
}

HRESULT cwv2_asyncop_create( HSTRING result, void **out )
{
    struct cwv2_asyncop *op = NULL;
    HRESULT hr = cwv2_asyncop_create_ex( result, TRUE, &op );
    if (SUCCEEDED( hr )) *out = &op->IInspectable_iface;
    return hr;
}


static HRESULT WINAPI com_str_h_QueryInterface( void *t, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IAgileObject ))
    {
        *out = t;
        InterlockedIncrement( (LONG *)((char *)t + sizeof(void *)) );
        return S_OK;
    }
    /* Accept any specific WebView2 completed handler IID */
    *out = t;
    InterlockedIncrement( (LONG *)((char *)t + sizeof(void *)) );
    return S_OK;
}
static ULONG WINAPI com_str_h_AddRef( void *t )
{
    return InterlockedIncrement( &((struct cwv2_com_str_handler *)t)->ref );
}
ULONG WINAPI com_str_h_Release( void *t )
{
    struct cwv2_com_str_handler *h = t;
    ULONG r = InterlockedDecrement( &h->ref );
    if (!r)
    {
        if (h->op) asyncop_Release( &h->op->IInspectable_iface );
        HeapFree( GetProcessHeap(), 0, (void *)h->vtbl );
        HeapFree( GetProcessHeap(), 0, h );
    }
    return r;
}
static HRESULT WINAPI com_str_h_Invoke( void *t, HRESULT err, LPCWSTR res )
{
    struct cwv2_com_str_handler *h = t;
    struct cwv2_asyncop *op = h->op;
    if (op)
    {
        op->hr = err;
        if (op->result) { WindowsDeleteString( op->result ); op->result = NULL; }
        if (res) WindowsCreateString( res, lstrlenW( res ), &op->result );
        op->completed = TRUE;
        if (op->handler)
        {
            struct cwv2_action_completed_handler *wh = (struct cwv2_action_completed_handler *)op->handler;
            wh->lpVtbl->Invoke( wh, &op->IInspectable_iface, FAILED( err ) ? Error : Completed );
        }
    }
    return S_OK;
}
struct cwv2_com_str_handler *cwv2_com_str_handler_create( struct cwv2_asyncop *op )
{
    struct cwv2_com_str_handler *h = HeapAlloc( GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*h) );
    const void **vt = HeapAlloc( GetProcessHeap(), 0, 4 * sizeof(*vt) );
    if (!h || !vt)
    {
        HeapFree( GetProcessHeap(), 0, h );
        HeapFree( GetProcessHeap(), 0, vt );
        return NULL;
    }
    vt[0] = com_str_h_QueryInterface;
    vt[1] = com_str_h_AddRef;
    vt[2] = com_str_h_Release;
    vt[3] = com_str_h_Invoke;
    h->vtbl = vt;
    h->ref = 1;
    h->op = op;
    if (op) asyncop_AddRef( &op->IInspectable_iface );
    return h;
}

static inline struct cwv2_profile *impl_from_profile( void *iface )
{
    return CONTAINING_RECORD( iface, struct cwv2_profile, IInspectable_iface );
}

static HRESULT WINAPI profile_QueryInterface( void *iface, REFIID iid, void **obj )
{
    struct cwv2_profile *impl = impl_from_profile( iface );
    *obj = NULL;
    if (!iid) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IWeakReferenceSource ) ||
        IsEqualGUID( iid, &IID_IMarshal ) ||
        IsEqualGUID( iid, &IID_IReferenceTracker ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget_WinUI ))
        return E_NOINTERFACE;
    /* Answer WebView2 Profile IIDs + IUnknown/IInspectable/IAgileObject with self. */
    *obj = &impl->IInspectable_iface;
    IInspectable_AddRef( *obj );
    return S_OK;
}
static HRESULT WINAPI profile_GetIids( IInspectable *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}
static HRESULT WINAPI profile_GetRuntimeClassName( IInspectable *iface, HSTRING *name )
{
    return WindowsCreateString( L"Microsoft.Web.WebView2.Core.CoreWebView2Profile", 47, name );
}
static ULONG WINAPI profile_AddRef( void *iface )
{
    return InterlockedIncrement( &impl_from_profile( iface )->ref );
}
static ULONG WINAPI profile_Release( void *iface )
{
    struct cwv2_profile *impl = impl_from_profile( iface );
    LONG ref = InterlockedDecrement( &impl->ref );
    if (!ref) free( impl );
    return ref;
}
/* AddBrowserExtension(folderPath, ICoreWebView2ProfileAddBrowserExtensionCompletedHandler*)
 * .NET SDK uses the completed-handler pattern: fire the handler synchronously
 * with a shell extension object (the asyncop shell answers get_Id/get_Name). */
static HRESULT WINAPI profile_AddBrowserExtension( void *iface, HSTRING path, void *handler )
{
    struct cwv2_result_completed_handler *h = (struct cwv2_result_completed_handler *)handler;
    void *ext = NULL;
    (void)iface;
    ERR( "profile_AddBrowserExtension %s handler %p\n", debugstr_hstring( path ), handler );
    cwv2_asyncop_create( NULL, &ext );
    if (handler) h->lpVtbl->Invoke( h, S_OK, ext );
    return S_OK;
}
static HRESULT WINAPI profile_ClearBrowsingDataAllAsync( void *iface, void **op )
{
    ERR( "profile_ClearBrowsingDataAllAsync iface %p, op %p\n", iface, op );
    return xaml_async_action_create( (IAsyncAction **)op );
}

/* the returned extension object answers .Id with the Zoom extension id */
static HRESULT WINAPI profile_generic_slot( void *iface, ... )
{
    const void **vt = *(const void ***)iface;
    int slot = 0;
    while (slot < 256 && vt[slot] != (const void *)profile_generic_slot) slot++;
    ERR( "profile_slot %d iface %p\n", slot, iface );
    return S_OK;
}

const void *cwv2_profile_vtbl[256] =
{
    [0] = profile_QueryInterface,
    [1] = profile_AddRef,
    [2] = profile_Release,
    [3] = profile_GetIids,
    [4] = profile_GetRuntimeClassName,
    [5] = cwv2_GetTrustLevel,
    [6] = profile_generic_slot,

    [9 ... 255] = profile_generic_slot,
    [7] = profile_ClearBrowsingDataAllAsync,
    [8] = profile_AddBrowserExtension,   /* WinRT class: 6 BrowserExtensions, 7 DefaultBackgroundColor/ClearBrowsingDataAsync, 8 AddBrowserExtension */
};
