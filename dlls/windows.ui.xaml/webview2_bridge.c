/* WinRT Windows.UI.Xaml.Controls.Frame Implementation
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

HRESULT WINAPI wv2_statics_get_SourceProperty( void *iface, void **value )
{
    if (!value) return E_POINTER;
    if (!wv2_source_dp)
        dependency_property_create( NULL, &wv2_source_dp );
    *value = wv2_source_dp;
    if (*value) IDependencyProperty_AddRef( wv2_source_dp );
    return *value ? S_OK : E_OUTOFMEMORY;
}

HRESULT WINAPI wv2_statics_get_prop( void *iface, void **value )
{
    if (!value) return E_POINTER;
    return dependency_property_create( NULL, (IDependencyProperty **)value );
}

extern struct xaml_element *impl_from_IWebView2( void *iface )
{
    return CONTAINING_RECORD( iface, struct xaml_element, IWebView2_iface );
}
extern struct xaml_element *impl_from_IWebView22( void *iface )
{
    return CONTAINING_RECORD( iface, struct xaml_element, IWebView22_iface );
}
#define WVPROT(i) HRESULT WINAPI wv2_probe##i( void *iface, ... );

WVPROT(0) WVPROT(1) WVPROT(2) WVPROT(3) WVPROT(4) WVPROT(5) WVPROT(6) WVPROT(7)
WVPROT(8) WVPROT(9) WVPROT(10) WVPROT(11) WVPROT(12) WVPROT(13) WVPROT(14) WVPROT(15)
WVPROT(16) WVPROT(17) WVPROT(18) WVPROT(19) WVPROT(20) WVPROT(21) WVPROT(22) WVPROT(23)
WVPROT(24) WVPROT(25) WVPROT(26) WVPROT(27) WVPROT(28) WVPROT(29) WVPROT(30) WVPROT(31)
WVPROT(32) WVPROT(33) WVPROT(34) WVPROT(35) WVPROT(36) WVPROT(37) WVPROT(38) WVPROT(39)
WVPROT(40) WVPROT(41) WVPROT(42) WVPROT(43) WVPROT(44) WVPROT(45) WVPROT(46) WVPROT(47)
WVPROT(48) WVPROT(49) WVPROT(50) WVPROT(51) WVPROT(52) WVPROT(53) WVPROT(54) WVPROT(55)
WVPROT(56) WVPROT(57) WVPROT(58) WVPROT(59) WVPROT(60) WVPROT(61) WVPROT(62) WVPROT(63)

/* FrameworkElement numeric getters (Width/Height/ActualWidth/ActualHeight
 * share this slot body): report the hosting window's real client width,
 * falling back to monitor-derived layout metrics until a window exists. */
HRESULT WINAPI element_get_double( void *iface, double *val )
{
    struct xaml_element *impl = impl_from_element( (IInspectable *)iface );
    HWND hw = impl->parent_hwnd;
    SIZE sz;

    if (!hw || !IsWindow( hw )) hw = xaml_get_window_hwnd();
    if (val)
    {
        xaml_get_layout_metrics( hw, &sz, NULL );
        *val = (double)sz.cx;
    }
    return S_OK;
}

extern struct xaml_async_action *impl_from_async_action( IAsyncAction *iface )
{
    return CONTAINING_RECORD( iface, struct xaml_async_action, IAsyncAction_iface );
}

extern struct xaml_async_action *impl_from_async_info( IAsyncInfo *iface )
{
    return CONTAINING_RECORD( iface, struct xaml_async_action, IAsyncInfo_iface );
}
void cwv2_queue_op( struct core_webview2 *impl, enum cwv2_pending_kind kind,
        int arg_int, BOOL arg_bool, const WCHAR *s1, const WCHAR *s2,
        void *obj, const void **avtbl, const GUID *aiid )
{
    struct cwv2_pending_op *op;
    if (!impl || !(op = calloc( 1, sizeof(*op) ))) return;
    op->kind = kind;
    op->arg_int = arg_int;
    op->arg_bool = arg_bool;
    if (s1) op->str1 = wcsdup( s1 );
    if (s2) op->str2 = wcsdup( s2 );
    op->obj = obj;
    if (obj) ((IUnknown *)obj)->lpVtbl->AddRef( (IUnknown *)obj );
    op->avtbl = avtbl;
    op->aiid = aiid;
    if (impl->pending_tail) impl->pending_tail->next = op;
    else impl->pending_head = op;
    impl->pending_tail = op;
}

const GUID IID_ICoreWebView2_2_com =
    { 0x9e8f0cf8, 0xe670, 0x4b5e, { 0xb2, 0xbc, 0x73, 0xe0, 0x61, 0xe3, 0x18, 0x4c } };
const GUID IID_ICoreWebView2_11_com =
    { 0x0be78e56, 0xc193, 0x4051, { 0xb9, 0x43, 0x23, 0xb4, 0x60, 0xc0, 0x8b, 0xdb } };
const GUID IID_ICoreWebView2Settings2_com =
    { 0xee9a0f68, 0xf46c, 0x4e32, { 0xac, 0x23, 0xef, 0x8c, 0xac, 0x22, 0x4d, 0x2a } };

const GUID IID_ICoreWebView2 =
    { 0x3a3f559a, 0xe5e9, 0x5338, { 0xbb, 0x67, 0x4e, 0xb0, 0x50, 0x4a, 0x4f, 0x14 } };
const GUID IID_ICoreWebView2_2 =
    { 0x578cb133, 0x2873, 0x5408, { 0xbd, 0x9e, 0x38, 0x9b, 0xbe, 0x9f, 0xa7, 0xfa } };
const GUID IID_ICoreWebView2_3 =
    { 0xa8c76ae7, 0x6170, 0x5dfe, { 0x8f, 0x00, 0x79, 0xcd, 0x76, 0xa9, 0xb4, 0xd9 } };
const GUID IID_ICoreWebView2_4 =
    { 0x4ac595ce, 0x1502, 0x5775, { 0xb2, 0xc8, 0x22, 0xc1, 0x1a, 0x36, 0x9c, 0x25 } };
const GUID IID_ICoreWebView2_5 =
    { 0xdd6af643, 0x220c, 0x5dc6, { 0xb0, 0xa8, 0x22, 0xc4, 0x1e, 0x47, 0x25, 0x95 } };
const GUID IID_ICoreWebView2_6 =
    { 0x92b34b96, 0x853d, 0x5bb6, { 0xac, 0x52, 0x30, 0x29, 0x7c, 0xe8, 0x05, 0xf1 } };
const GUID IID_ICoreWebView2_7 =
    { 0xf9b7107a, 0x2e09, 0x5596, { 0xa0, 0x33, 0x91, 0x1b, 0xa1, 0x23, 0x15, 0xf7 } };
const GUID IID_ICoreWebView2_8 =
    { 0xaa2503c0, 0x8d1c, 0x5a3d, { 0xb8, 0x98, 0xf5, 0x5f, 0x75, 0x95, 0x26, 0x8a } };
const GUID IID_ICoreWebView2_9 =
    { 0x64b2ec16, 0x0b29, 0x5216, { 0xbf, 0x86, 0xe5, 0x75, 0xc8, 0x8f, 0x70, 0x31 } };
const GUID IID_ICoreWebView2_10 =
    { 0xa7b20434, 0x970f, 0x54b1, { 0xaa, 0x63, 0x3c, 0x42, 0x67, 0x1f, 0xa9, 0xab } };
const GUID IID_ICoreWebView2_11 =
    { 0xc00acbb1, 0xae32, 0x501f, { 0xad, 0x19, 0x9d, 0x0a, 0xc3, 0x2d, 0x61, 0x42 } };
const GUID IID_ICoreWebView2_12 =
    { 0xdbbbe9a1, 0x18d3, 0x5f67, { 0xb3, 0x62, 0x0f, 0x4a, 0xe9, 0x37, 0xd7, 0x54 } };
const GUID IID_ICoreWebView2_13 =
    { 0x314b5846, 0xdbc7, 0x5de4, { 0xa7, 0x92, 0x64, 0x7e, 0xa0, 0xf3, 0x29, 0x6a } };

const GUID IID_ICoreWebView2Settings =
    { 0x003b325e, 0x74cd, 0x52dd, { 0x80, 0x24, 0xeb, 0xb8, 0xbe, 0x38, 0xe4, 0x8e } };
const GUID IID_ICoreWebView2Settings2 =
    { 0x377d3480, 0xfdb2, 0x56e7, { 0xba, 0xde, 0x50, 0x7d, 0x35, 0x28, 0x87, 0xe9 } };
const GUID IID_ICoreWebView2Settings3 =
    { 0x52200f01, 0x5309, 0x5b2e, { 0xa0, 0x3c, 0x3d, 0x26, 0x77, 0x59, 0x19, 0x40 } };
const GUID IID_ICoreWebView2Settings4 =
    { 0xd6a955f0, 0xdaef, 0x5a6a, { 0xa6, 0xf6, 0xc7, 0x2f, 0x0e, 0xde, 0x76, 0x20 } };
const GUID IID_ICoreWebView2Settings5 =
    { 0xafc42b23, 0x4839, 0x5d73, { 0xac, 0xf7, 0xe0, 0x33, 0x56, 0x31, 0xab, 0xf5 } };
const GUID IID_ICoreWebView2Settings6 =
    { 0x3fe4ae85, 0x0540, 0x5bf1, { 0xb4, 0xd9, 0x99, 0xec, 0x57, 0xaa, 0x64, 0xf5 } };
const GUID IID_ICoreWebView2Settings7 =
    { 0x688027cd, 0x9f84, 0x59e8, { 0x8d, 0x5c, 0x91, 0x12, 0x3d, 0xf2, 0x4b, 0x92 } };
const GUID IID_ICoreWebView2Settings8 =
    { 0x956f1a8f, 0x3198, 0x5577, { 0xb2, 0x50, 0x7d, 0x91, 0xd1, 0x7f, 0x7e, 0xed } };
const GUID IID_ICoreWebView2Settings9 =
    { 0x4c346681, 0x714d, 0x5a3d, { 0x81, 0x05, 0x2a, 0x7b, 0x80, 0xbe, 0xea, 0xb5 } };
const GUID IID_ICoreWebView2Settings_Manual =
    { 0x0a538c87, 0xe000, 0x511c, { 0x87, 0xca, 0xde, 0xd3, 0x41, 0x3d, 0x03, 0xda } };
const GUID IID_ICoreWebView2Environment =
    { 0xd8cc7831, 0xb783, 0x556b, { 0xb9, 0xce, 0x89, 0x9c, 0x1e, 0x95, 0xd5, 0x85 } };

static inline struct core_webview2 *impl_from_cwv2_3( void *iface ){    return CONTAINING_RECORD( iface, struct core_webview2, ICoreWebView2_3_iface );}

static inline struct core_webview2 *impl_from_cwv2_13( void *iface )
{
    return CONTAINING_RECORD( iface, struct core_webview2, ICoreWebView2_13_iface );
}
static inline struct core_webview2 *impl_from_cwv2_stub( void *iface )
{
    return CONTAINING_RECORD( iface, struct core_webview2, ICoreWebView2_stub_iface );
}

static HRESULT WINAPI cwv2_QueryInterface( IInspectable *iface, REFIID iid, void **out )
{
    struct core_webview2 *impl = impl_from_cwv2( iface );
    TRACE( "cwv2_QI %s\n", debugstr_guid( iid ) );
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_ICoreWebView2 ))
    {
        *out = &impl->IInspectable_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_ICoreWebView2_2 ))
    {
        *out = &impl->ICoreWebView2_2_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_ICoreWebView2_3 ))
    {
        *out = &impl->ICoreWebView2_3_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_ICoreWebView2_11 ))
    {
        *out = &impl->ICoreWebView2_11_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_ICoreWebView2_13 ))
    {
        *out = &impl->ICoreWebView2_13_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_ICoreWebView2_4 ) ||
        IsEqualGUID( iid, &IID_ICoreWebView2_5 ) ||
        IsEqualGUID( iid, &IID_ICoreWebView2_6 ) ||
        IsEqualGUID( iid, &IID_ICoreWebView2_7 ) ||
        IsEqualGUID( iid, &IID_ICoreWebView2_8 ) ||
        IsEqualGUID( iid, &IID_ICoreWebView2_9 ) ||
        IsEqualGUID( iid, &IID_ICoreWebView2_10 ) ||
        IsEqualGUID( iid, &IID_ICoreWebView2_12 ))
    {
        *out = &impl->ICoreWebView2_stub_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    ERR( "cwv2_QueryInterface: unknown IID %s\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}
ULONG WINAPI cwv2_AddRef( IInspectable *iface )
{
    struct core_webview2 *impl = impl_from_cwv2( iface );
    return InterlockedIncrement( &impl->ref );
}
ULONG WINAPI cwv2_Release( IInspectable *iface )
{
    struct core_webview2 *impl = impl_from_cwv2( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        if (impl->source_uri) WindowsDeleteString( impl->source_uri );
        if (impl->loader_core2) ((IUnknown *)impl->loader_core2)->lpVtbl->Release( impl->loader_core2 );
        if (impl->loader_core11) ((IUnknown *)impl->loader_core11)->lpVtbl->Release( impl->loader_core11 );
        if (impl->loader_core13) ((IUnknown *)impl->loader_core13)->lpVtbl->Release( impl->loader_core13 );
        free( impl );
    }
    return ref;
}
HRESULT WINAPI cwv2_GetIids( IInspectable *iface, ULONG *count, IID **iids )
{
    IID *arr;
    ERR( "cwv2_GetIids iface %p\n", iface );
    if (!(arr = CoTaskMemAlloc( sizeof(IID) ))) return E_OUTOFMEMORY;
    *arr = IID_ICoreWebView2;
    if (count) *count = 1;
    if (iids) *iids = arr; else CoTaskMemFree( arr );
    return S_OK;
}
HRESULT WINAPI cwv2_GetRuntimeClassName( IInspectable *iface, HSTRING *name )
{ ERR( "cwv2_GetRuntimeClassName\n" ); return WindowsCreateString( L"Microsoft.Web.WebView2.Core.CoreWebView2", 39, name ); }
HRESULT WINAPI cwv2_GetTrustLevel( IInspectable *iface, TrustLevel *level )
{ if (level) *level = BaseTrust; return S_OK; }

static HRESULT WINAPI cwv2_2_QueryInterface( IInspectable *iface, REFIID iid, void **out )
{
    return cwv2_QueryInterface( &impl_from_cwv2_2( iface )->IInspectable_iface, iid, out );
}
static ULONG WINAPI cwv2_2_AddRef( IInspectable *iface )
{
    return cwv2_AddRef( &impl_from_cwv2_2( iface )->IInspectable_iface );
}
static ULONG WINAPI cwv2_2_Release( IInspectable *iface )
{
    return cwv2_Release( &impl_from_cwv2_2( iface )->IInspectable_iface );
}

#define CWV2_SUB_IUNKNOWN(sub) \
    static HRESULT WINAPI cwv2_##sub##_QueryInterface( IInspectable *iface, REFIID iid, void **out ) \
    { return cwv2_QueryInterface( &impl_from_cwv2_##sub( iface )->IInspectable_iface, iid, out ); } \
    static ULONG WINAPI cwv2_##sub##_AddRef( IInspectable *iface ) \
    { return cwv2_AddRef( &impl_from_cwv2_##sub( iface )->IInspectable_iface ); } \
    static ULONG WINAPI cwv2_##sub##_Release( IInspectable *iface ) \
    { return cwv2_Release( &impl_from_cwv2_##sub( iface )->IInspectable_iface ); }

CWV2_SUB_IUNKNOWN( 3 )
CWV2_SUB_IUNKNOWN( 11 )
CWV2_SUB_IUNKNOWN( 13 )
CWV2_SUB_IUNKNOWN( stub )

static HRESULT WINAPI cwv2_get_Settings( void *iface, void **settings )
{
    struct core_webview2 *impl = impl_from_cwv2( iface );
    struct cwv2_settings *s;
    void *com_settings = NULL;

    if (!settings) return E_POINTER;
    if (!(s = calloc( 1, sizeof(*s) ))) return E_OUTOFMEMORY;
    s->IInspectable_iface.lpVtbl = (const struct IInspectableVtbl *)vtbl_dup( cwv2_settings_vtbl );
    s->ISettings2_iface.lpVtbl = (const struct IInspectableVtbl *)vtbl_dup( cwv2_settings2_vtbl );
    s->ISettings_stub_iface.lpVtbl = (const struct IInspectableVtbl *)vtbl_dup( cwv2_settings_stub_vtbl );
    s->ref = 1;
    s->owner = impl;

    if (impl->loader_core && impl->real_loader && *(void ***)impl->loader_core)
    {
        HRESULT (WINAPI *fn)( void *, void ** ) = (*(void ***)impl->loader_core)[3];
        if (fn && (ULONG_PTR)fn < (ULONG_PTR)0x7ffc00000000)
        {
            __TRY
            {
                HRESULT hr = fn( impl->loader_core, &com_settings );
                if (SUCCEEDED( hr ) && com_settings)
                {
                    s->loader_settings = com_settings;
                    ((IUnknown *)com_settings)->lpVtbl->QueryInterface( com_settings,
                            &IID_ICoreWebView2Settings2_com, &s->loader_settings2 );
                }
            }
            __EXCEPT_PAGE_FAULT
            {
                ERR( "cwv2_get_Settings: loader_core->get_Settings faulted\n" );
            }
            __ENDTRY;
        }
    }

    *settings = &s->IInspectable_iface;
    ERR( "cwv2_get_Settings -> %p (com=%p com2=%p)\n", *settings, s->loader_settings, s->loader_settings2 );
    return S_OK;
}



void *wv2_slot_fn( void *obj, BOOL real_loader, int slot_real, int slot_builtin )
{
    int slot = real_loader ? slot_real : slot_builtin;
    if (!obj || !*(void ***)obj || slot < 0) return NULL;
    return (*(void ***)obj)[slot];
}

static HRESULT wv2_call_navigate( void *obj, BOOL real_loader, int slot_real, int slot_builtin, LPCWSTR uri )
{
    void *fn = wv2_slot_fn( obj, real_loader, slot_real, slot_builtin );
    if (!fn || (ULONG_PTR)fn >= (ULONG_PTR)0x7ffc00000000) return E_NOINTERFACE;
    if (real_loader)
        return ((HRESULT (WINAPI *)( void *, LPCWSTR ))fn)( obj, uri );
    return ((HRESULT (WINAPI *)( void *, LPCWSTR, void * ))fn)( obj, uri, NULL );
}

static HRESULT wv2_call_get_source( void *obj, BOOL real_loader, int slot_real, int slot_builtin, LPWSTR *raw )
{
    void *fn = wv2_slot_fn( obj, real_loader, slot_real, slot_builtin );
    if (!fn || (ULONG_PTR)fn >= (ULONG_PTR)0x7ffc00000000) return E_NOINTERFACE;
    return ((HRESULT (WINAPI *)( void *, LPWSTR * ))fn)( obj, raw );
}

static HRESULT wv2_call_bounds( void *obj, BOOL real_loader, int slot_real, int slot_builtin, RECT rc )
{
    void *fn = wv2_slot_fn( obj, real_loader, slot_real, slot_builtin );
    if (!fn || (ULONG_PTR)fn >= (ULONG_PTR)0x7ffc00000000) return E_NOINTERFACE;
    return ((HRESULT (WINAPI *)( void *, RECT ))fn)( obj, rc );
}

static HRESULT wv2_call_bool( void *obj, BOOL real_loader, int slot_real, int slot_builtin, BOOL val )
{
    void *fn = wv2_slot_fn( obj, real_loader, slot_real, slot_builtin );
    if (!fn || (ULONG_PTR)fn >= (ULONG_PTR)0x7ffc00000000) return E_NOINTERFACE;
    return ((HRESULT (WINAPI *)( void *, BOOL ))fn)( obj, val );
}

static HRESULT wv2_call_hwnd( void *obj, BOOL real_loader, int slot_real, int slot_builtin, HWND hwnd )
{
    void *fn = wv2_slot_fn( obj, real_loader, slot_real, slot_builtin );
    if (!fn || (ULONG_PTR)fn >= (ULONG_PTR)0x7ffc00000000) return E_NOINTERFACE;
    return ((HRESULT (WINAPI *)( void *, HWND ))fn)( obj, hwnd );
}

static HRESULT wv2_call_void( void *obj, BOOL real_loader, int slot_real, int slot_builtin )
{
    void *fn = wv2_slot_fn( obj, real_loader, slot_real, slot_builtin );
    if (!fn || (ULONG_PTR)fn >= (ULONG_PTR)0x7ffc00000000) return E_NOINTERFACE;
    return ((HRESULT (WINAPI *)( void * ))fn)( obj );
}

static HRESULT wv2_call_get_core( void *controller, BOOL real_loader, int slot_real, int slot_builtin, void **core )
{
    HRESULT (WINAPI *get_core)( void *, void ** ) = wv2_slot_fn( controller, real_loader, slot_real, slot_builtin );
    if ((ULONG_PTR)get_core >= (ULONG_PTR)0x7ffc00000000)  /* stale shared-page slot, see s380 */
    {
        HMODULE loader = GetModuleHandleW( L"webview2loader.dll" );
        get_core = (void *)GetProcAddress( loader, "GetCoreWebView2FromController" );
        ERR( "wv2_call_get_core: stale slot%d, using export %p\n", real_loader ? slot_real : slot_builtin, get_core );
    }
    if (!get_core) return E_NOINTERFACE;
    return get_core( controller, core );
}

static BOOL wv2_probe_bounds( ICoreWebView2Controller *c )  /* call slot-4 getter/putter with known rect, read back */
{
    static const RECT probe_rc = { 0, 0, 321, 123 };
    RECT orig = {0}, readback = {0};
    HRESULT hr_put, hr_get;
    BOOL ok;

    if (!c || !c->lpVtbl || !c->lpVtbl->get_Bounds || !c->lpVtbl->put_Bounds)
        return FALSE;
    c->lpVtbl->get_Bounds( c, &orig );
    hr_put = c->lpVtbl->put_Bounds( c, probe_rc );
    hr_get = c->lpVtbl->get_Bounds( c, &readback );
    ok = SUCCEEDED( hr_put ) && SUCCEEDED( hr_get ) &&
         readback.right == probe_rc.right && readback.bottom == probe_rc.bottom;
    c->lpVtbl->put_Bounds( c, orig );
    ERR( "wv2_probe_bounds: ctrl=%p put=%#lx get=%#lx read=%ldx%ld ok=%d\n",
         c, hr_put, hr_get, readback.right, readback.bottom, ok );
    return ok;
}

static HRESULT WINAPI cwv2_Navigate( void *iface, HSTRING uri )
{
    struct core_webview2 *impl = impl_from_cwv2( iface );
    ERR( "=== CoreWebView2::Navigate: %s ===\n", debugstr_hstring( uri ) );
    if (impl->source_uri) WindowsDeleteString( impl->source_uri );
    impl->source_uri = NULL;
    if (uri) WindowsDuplicateString( uri, &impl->source_uri );
    if (impl->loader_core)
    {
        const WCHAR *s = uri ? WindowsGetStringRawBuffer( uri, NULL ) : L"";
        HRESULT hr2 = wv2_call_navigate( impl->loader_core, impl->real_loader, 5, 6, s );
        if (impl->owner) impl->owner->navigated = TRUE;
        ERR( "cwv2_Navigate: loader forward hr %#lx\n", hr2 );
        return hr2;
    }
    return S_OK;
}
static HRESULT WINAPI cwv2_NavigateToString( void *iface, HSTRING html )
{
    struct core_webview2 *impl = impl_from_cwv2( iface );
    const WCHAR *raw = html ? WindowsGetStringRawBuffer( html, NULL ) : L"";
    ERR( "=== CoreWebView2::NavigateToString (%u chars) ===\n", (unsigned)lstrlenW( raw ) );
    if (impl->owner) impl->owner->navigated = TRUE;
    if (impl->loader_core)
    {
        LPCWSTR s = impl->real_loader ? raw : L"data:text/html,";
        HRESULT hr2 = wv2_call_navigate( impl->loader_core, impl->real_loader, 6, 6, s );
        ERR( "cwv2_NavigateToString: loader forward hr %#lx\n", hr2 );
        return hr2;
    }
    return S_OK;
}
static HRESULT WINAPI cwv2_get_Source( void *iface, HSTRING *uri )
{
    struct core_webview2 *impl = impl_from_cwv2( iface );
    if (!uri) return E_POINTER;
    if (impl->loader_core && impl->real_loader)
    {
        LPWSTR raw = NULL;
        if (SUCCEEDED( wv2_call_get_source( impl->loader_core, impl->real_loader, 4, 8, &raw ) ) && raw)
        {
            HRESULT hr = WindowsCreateString( raw, lstrlenW( raw ), uri );
            CoTaskMemFree( raw );
            return hr;
        }
    }
    if (impl->source_uri) return WindowsDuplicateString( impl->source_uri, uri );
    if (impl->owner && impl->owner->wv2_source_uri)
        return WindowsDuplicateString( impl->owner->wv2_source_uri, uri );
    return WindowsCreateString( L"about:blank", 11, uri );
}

static HRESULT WINAPI cwv2_2_get_Environment( void *iface, void **env )
{
    struct cwv2_env *e;
    if (!env) return E_POINTER;
    if (!(e = calloc( 1, sizeof(*e) ))) return E_OUTOFMEMORY;
    e->IInspectable_iface.lpVtbl = (const struct IInspectableVtbl *)vtbl_dup( cwv2_env_vtbl );
    e->ref = 1;
    *env = &e->IInspectable_iface;
    return S_OK;
}


void *cwv2_get_com_iface( struct core_webview2 *impl, void **cached, const GUID *iid )
{
    if (*cached) return *cached;
    if (impl->loader_core && impl->real_loader)
        ((IUnknown *)impl->loader_core)->lpVtbl->QueryInterface( impl->loader_core, iid, cached );
    return *cached;
}

static HRESULT WINAPI cwv2_get_BrowserProcessId( void *iface, UINT32 *pid )
{
    struct core_webview2 *impl = impl_from_cwv2( iface );
    if (!pid) return E_POINTER;
    if (impl->loader_core && impl->real_loader)
    {
        HRESULT (WINAPI *fn)( void *, UINT32 * ) = wv2_slot_fn( impl->loader_core, TRUE, 37, -1 );
        if (fn && (ULONG_PTR)fn < (ULONG_PTR)0x7ffc00000000)
            return fn( impl->loader_core, pid );
    }
    *pid = GetCurrentProcessId();
    return S_OK;
}

static HRESULT WINAPI cwv2_get_CanGoBack( void *iface, boolean *val )
{
    struct core_webview2 *impl = impl_from_cwv2( iface );
    BOOL b = FALSE;
    if (!val) return E_POINTER;
    if (impl->loader_core && impl->real_loader)
    {
        HRESULT (WINAPI *fn)( void *, BOOL * ) = wv2_slot_fn( impl->loader_core, TRUE, 38, -1 );
        if (fn && (ULONG_PTR)fn < (ULONG_PTR)0x7ffc00000000)
        {
            HRESULT hr = fn( impl->loader_core, &b );
            *val = !!b;
            return hr;
        }
    }
    *val = FALSE;
    return S_OK;
}

static HRESULT WINAPI cwv2_get_CanGoForward( void *iface, boolean *val )
{
    struct core_webview2 *impl = impl_from_cwv2( iface );
    BOOL b = FALSE;
    if (!val) return E_POINTER;
    if (impl->loader_core && impl->real_loader)
    {
        HRESULT (WINAPI *fn)( void *, BOOL * ) = wv2_slot_fn( impl->loader_core, TRUE, 39, -1 );
        if (fn && (ULONG_PTR)fn < (ULONG_PTR)0x7ffc00000000)
        {
            HRESULT hr = fn( impl->loader_core, &b );
            *val = !!b;
            return hr;
        }
    }
    *val = FALSE;
    return S_OK;
}

static HRESULT WINAPI cwv2_get_DocumentTitle( void *iface, HSTRING *title )
{
    struct core_webview2 *impl = impl_from_cwv2( iface );
    if (!title) return E_POINTER;
    if (impl->loader_core && impl->real_loader)
    {
        LPWSTR raw = NULL;
        HRESULT (WINAPI *fn)( void *, LPWSTR * ) = wv2_slot_fn( impl->loader_core, TRUE, 48, -1 );
        if (fn && (ULONG_PTR)fn < (ULONG_PTR)0x7ffc00000000)
        {
            HRESULT hr = fn( impl->loader_core, &raw );
            if (SUCCEEDED( hr ) && raw)
            {
                hr = WindowsCreateString( raw, lstrlenW( raw ), title );
                CoTaskMemFree( raw );
                return hr;
            }
            if (raw) CoTaskMemFree( raw );
            return hr;
        }
    }
    return WindowsCreateString( L"", 0, title );
}

static HRESULT WINAPI cwv2_get_ContainsFullScreenElement( void *iface, boolean *val )
{
    struct core_webview2 *impl = impl_from_cwv2( iface );
    BOOL b = FALSE;
    if (!val) return E_POINTER;
    if (impl->loader_core && impl->real_loader)
    {
        HRESULT (WINAPI *fn)( void *, BOOL * ) = wv2_slot_fn( impl->loader_core, TRUE, 54, -1 );
        if (fn && (ULONG_PTR)fn < (ULONG_PTR)0x7ffc00000000)
        {
            HRESULT hr = fn( impl->loader_core, &b );
            *val = !!b;
            return hr;
        }
    }
    *val = FALSE;
    return S_OK;
}

static HRESULT WINAPI cwv2_AddScriptToExecuteOnDocumentCreatedAsync( void *iface, HSTRING js, void **op )
{
    struct core_webview2 *impl = impl_from_cwv2( iface );
    struct cwv2_asyncop *aop = NULL;
    const WCHAR *s = js ? WindowsGetStringRawBuffer( js, NULL ) : L"";
    HRESULT hr;
    if (!op) return E_POINTER;
    if (impl->loader_core && impl->real_loader)
    {
        HRESULT (WINAPI *fn)( void *, LPCWSTR, void * ) = wv2_slot_fn( impl->loader_core, TRUE, 27, -1 );
        if (fn && (ULONG_PTR)fn < (ULONG_PTR)0x7ffc00000000)
        {
            struct cwv2_com_str_handler *ch;
            if (FAILED( hr = cwv2_asyncop_create_ex( NULL, FALSE, &aop ) )) return hr;
            ch = cwv2_com_str_handler_create( aop );
            if (!ch) { asyncop_Release( &aop->IInspectable_iface ); return E_OUTOFMEMORY; }
            hr = fn( impl->loader_core, s, ch );
            com_str_h_Release( ch );
            if (FAILED( hr )) { aop->hr = hr; aop->completed = TRUE; }
            *op = &aop->IInspectable_iface;
            return S_OK;
        }
    }
    cwv2_queue_op( impl, CWV2_OP_ADD_SCRIPT, 0, FALSE, s, NULL, NULL, NULL, NULL );
    return cwv2_asyncop_create( NULL, op );
}

static HRESULT WINAPI cwv2_RemoveScriptToExecuteOnDocumentCreated( void *iface, HSTRING id )
{
    struct core_webview2 *impl = impl_from_cwv2( iface );
    const WCHAR *s = id ? WindowsGetStringRawBuffer( id, NULL ) : L"";
    if (impl->loader_core && impl->real_loader)
    {
        HRESULT (WINAPI *fn)( void *, LPCWSTR ) = wv2_slot_fn( impl->loader_core, TRUE, 28, -1 );
        if (fn && (ULONG_PTR)fn < (ULONG_PTR)0x7ffc00000000)
            return fn( impl->loader_core, s );
    }
    return S_OK;
}

static HRESULT WINAPI cwv2_ExecuteScriptAsync( void *iface, HSTRING js, void **op )
{
    struct core_webview2 *impl = impl_from_cwv2( iface );
    struct cwv2_asyncop *aop = NULL;
    const WCHAR *s = js ? WindowsGetStringRawBuffer( js, NULL ) : L"";
    HRESULT hr;
    if (!op) return E_POINTER;
    if (impl->loader_core && impl->real_loader)
    {
        HRESULT (WINAPI *fn)( void *, LPCWSTR, void * ) = wv2_slot_fn( impl->loader_core, TRUE, 29, -1 );
        if (fn && (ULONG_PTR)fn < (ULONG_PTR)0x7ffc00000000)
        {
            struct cwv2_com_str_handler *ch;
            if (FAILED( hr = cwv2_asyncop_create_ex( NULL, FALSE, &aop ) )) return hr;
            ch = cwv2_com_str_handler_create( aop );
            if (!ch) { asyncop_Release( &aop->IInspectable_iface ); return E_OUTOFMEMORY; }
            hr = fn( impl->loader_core, s, ch );
            com_str_h_Release( ch );
            if (FAILED( hr )) { aop->hr = hr; aop->completed = TRUE; }
            *op = &aop->IInspectable_iface;
            return S_OK;
        }
    }
    return cwv2_asyncop_create( NULL, op );
}

static HRESULT WINAPI cwv2_Reload( void *iface )
{
    struct core_webview2 *impl = impl_from_cwv2( iface );
    if (impl->loader_core && impl->real_loader)
        return wv2_call_void( impl->loader_core, TRUE, 31, -1 );
    return S_OK;
}

static HRESULT WINAPI cwv2_PostWebMessageAsJson( void *iface, HSTRING json )
{
    struct core_webview2 *impl = impl_from_cwv2( iface );
    const WCHAR *s = json ? WindowsGetStringRawBuffer( json, NULL ) : L"";
    if (impl->loader_core && impl->real_loader)
    {
        HRESULT (WINAPI *fn)( void *, LPCWSTR ) = wv2_slot_fn( impl->loader_core, TRUE, 32, -1 );
        if (fn && (ULONG_PTR)fn < (ULONG_PTR)0x7ffc00000000)
            return fn( impl->loader_core, s );
    }
    return S_OK;
}

static HRESULT WINAPI cwv2_PostWebMessageAsString( void *iface, HSTRING msg )
{
    struct core_webview2 *impl = impl_from_cwv2( iface );
    const WCHAR *s = msg ? WindowsGetStringRawBuffer( msg, NULL ) : L"";
    if (impl->loader_core && impl->real_loader)
    {
        HRESULT (WINAPI *fn)( void *, LPCWSTR ) = wv2_slot_fn( impl->loader_core, TRUE, 33, -1 );
        if (fn && (ULONG_PTR)fn < (ULONG_PTR)0x7ffc00000000)
            return fn( impl->loader_core, s );
    }
    return S_OK;
}

static HRESULT WINAPI cwv2_CallDevToolsProtocolMethodAsync( void *iface, HSTRING method, HSTRING params, void **op )
{
    struct core_webview2 *impl = impl_from_cwv2( iface );
    const WCHAR *m = method ? WindowsGetStringRawBuffer( method, NULL ) : L"";
    const WCHAR *p = params ? WindowsGetStringRawBuffer( params, NULL ) : L"{}";
    ERR( "cwv2_CallDevToolsProtocolMethodAsync %s %s\n", debugstr_w( m ), debugstr_w( p ) );
    if (!op) return E_POINTER;
    if (impl->loader_core && impl->real_loader)
    {
        HRESULT (WINAPI *fn)( void *, LPCWSTR, LPCWSTR, void * ) = wv2_slot_fn( impl->loader_core, TRUE, 36, -1 );
        if (fn && (ULONG_PTR)fn < (ULONG_PTR)0x7ffc00000000)
        {
            fn( impl->loader_core, m, p, NULL );
            return cwv2_asyncop_create( NULL, op );
        }
    }
    cwv2_queue_op( impl, CWV2_OP_DEVTOOLS, 0, FALSE, m, p, NULL, NULL, NULL );
    return cwv2_asyncop_create( NULL, op );
}

static HRESULT WINAPI cwv2_GoBack( void *iface )
{
    struct core_webview2 *impl = impl_from_cwv2( iface );
    if (impl->loader_core && impl->real_loader)
        return wv2_call_void( impl->loader_core, TRUE, 40, -1 );
    return S_OK;
}

static HRESULT WINAPI cwv2_GoForward( void *iface )
{
    struct core_webview2 *impl = impl_from_cwv2( iface );
    if (impl->loader_core && impl->real_loader)
        return wv2_call_void( impl->loader_core, TRUE, 41, -1 );
    return S_OK;
}

static HRESULT WINAPI cwv2_Stop( void *iface )
{
    struct core_webview2 *impl = impl_from_cwv2( iface );
    if (impl->loader_core && impl->real_loader)
        return wv2_call_void( impl->loader_core, TRUE, 43, -1 );
    return S_OK;
}

static HRESULT WINAPI cwv2_AddHostObjectToScript( void *iface, HSTRING name, IInspectable *raw_object )
{
    struct core_webview2 *impl = impl_from_cwv2( iface );
    const WCHAR *s = name ? WindowsGetStringRawBuffer( name, NULL ) : L"";
    ERR( "=== CoreWebView2::AddHostObjectToScript: %s obj=%p ===\n", debugstr_w( s ), raw_object );
    if (impl->loader_core && impl->real_loader)
    {
        HRESULT (WINAPI *fn)( void *, LPCWSTR, VARIANT * ) = wv2_slot_fn( impl->loader_core, TRUE, 49, -1 );
        if (fn && (ULONG_PTR)fn < (ULONG_PTR)0x7ffc00000000)
        {
            VARIANT v;
            IDispatch *disp = NULL;
            HRESULT hr;
            memset( &v, 0, sizeof(v) );
            if (raw_object && SUCCEEDED( IInspectable_QueryInterface( raw_object, &IID_IDispatch, (void **)&disp ) ) && disp)
            {
                V_VT( &v ) = VT_DISPATCH;
                V_DISPATCH( &v ) = disp;
            }
            else
            {
                V_VT( &v ) = VT_UNKNOWN;
                V_UNKNOWN( &v ) = (IUnknown *)raw_object;
                if (raw_object) IInspectable_AddRef( raw_object );
            }
            hr = fn( impl->loader_core, s, &v );
            if (V_UNKNOWN( &v )) ((IUnknown *)V_UNKNOWN( &v ))->lpVtbl->Release( V_UNKNOWN( &v ) );
            ERR( "cwv2_AddHostObjectToScript(%s) -> %#lx\n", debugstr_w( s ), hr );
            return hr;
        }
    }
    cwv2_queue_op( impl, CWV2_OP_ADD_HOST_OBJECT, 0, FALSE, s, NULL, raw_object, NULL, NULL );
    return S_OK;
}

static HRESULT WINAPI cwv2_RemoveHostObjectFromScript( void *iface, HSTRING name )
{
    struct core_webview2 *impl = impl_from_cwv2( iface );
    const WCHAR *s = name ? WindowsGetStringRawBuffer( name, NULL ) : L"";
    if (impl->loader_core && impl->real_loader)
    {
        HRESULT (WINAPI *fn)( void *, LPCWSTR ) = wv2_slot_fn( impl->loader_core, TRUE, 50, -1 );
        if (fn && (ULONG_PTR)fn < (ULONG_PTR)0x7ffc00000000)
            return fn( impl->loader_core, s );
    }
    return S_OK;
}

static HRESULT WINAPI cwv2_OpenDevToolsWindow( void *iface )
{
    struct core_webview2 *impl = impl_from_cwv2( iface );
    if (impl->loader_core && impl->real_loader)
        return wv2_call_void( impl->loader_core, TRUE, 51, -1 );
    return S_OK;
}

static HRESULT WINAPI cwv2_AddWebResourceRequestedFilter( void *iface, HSTRING uri, int resource_ctx )
{
    struct core_webview2 *impl = impl_from_cwv2( iface );
    const WCHAR *s = uri ? WindowsGetStringRawBuffer( uri, NULL ) : L"";
    ERR( "cwv2_AddWebResourceRequestedFilter: %s ctx=%d\n", debugstr_w( s ), resource_ctx );
    if (impl->loader_core && impl->real_loader)
    {
        HRESULT (WINAPI *fn)( void *, LPCWSTR, int ) = wv2_slot_fn( impl->loader_core, TRUE, 57, -1 );
        if (fn && (ULONG_PTR)fn < (ULONG_PTR)0x7ffc00000000)
            return fn( impl->loader_core, s, resource_ctx );
    }
    cwv2_queue_op( impl, CWV2_OP_ADD_WRS_FILTER, resource_ctx, FALSE, s, NULL, NULL, NULL, NULL );
    return S_OK;
}

static HRESULT WINAPI cwv2_RemoveWebResourceRequestedFilter( void *iface, HSTRING uri, int resource_ctx )
{
    struct core_webview2 *impl = impl_from_cwv2( iface );
    const WCHAR *s = uri ? WindowsGetStringRawBuffer( uri, NULL ) : L"";
    if (impl->loader_core && impl->real_loader)
    {
        HRESULT (WINAPI *fn)( void *, LPCWSTR, int ) = wv2_slot_fn( impl->loader_core, TRUE, 58, -1 );
        if (fn && (ULONG_PTR)fn < (ULONG_PTR)0x7ffc00000000)
            return fn( impl->loader_core, s, resource_ctx );
    }
    return S_OK;
}

/* WinRT EventArgs wrapper around COM ICoreWebView2*EventArgs */
const GUID IID_ICoreWebView2NavigationStartingEventArgs_rt =
    { 0x548d23d3, 0xfea3, 0x5616, { 0xbd, 0x05, 0xae, 0x08, 0x06, 0x6c, 0x86, 0xd3 } };
const GUID IID_ICoreWebView2NavigationCompletedEventArgs_rt =
    { 0x4865e238, 0x036a, 0x5664, { 0x95, 0xa3, 0x44, 0x7e, 0xc4, 0x4c, 0xf4, 0x98 } };
const GUID IID_ICoreWebView2WebMessageReceivedEventArgs_rt =
    { 0xeb066159, 0xb725, 0x5d5b, { 0xad, 0xc8, 0xf5, 0xd7, 0xb9, 0x29, 0x03, 0x04 } };
const GUID IID_ICoreWebView2WebResourceRequestedEventArgs_rt =
    { 0x577f1fc4, 0xc943, 0x54a9, { 0x97, 0x00, 0xbd, 0x46, 0x9b, 0x48, 0xbd, 0x41 } };
const GUID IID_ICoreWebView2DOMContentLoadedEventArgs_rt =
    { 0xc474d0a3, 0x24ac, 0x59fc, { 0xb7, 0x8b, 0xda, 0x75, 0x62, 0xa6, 0xa0, 0x52 } };
const GUID IID_ICoreWebView2ContextMenuRequestedEventArgs_rt =
    { 0xd77bdd8c, 0x9b3e, 0x596e, { 0xae, 0x80, 0x32, 0x0c, 0x0d, 0xf4, 0xec, 0xbc } };

static const void *cwv2_vtbl[256] =
{
    [0] = cwv2_QueryInterface,
    [1] = cwv2_AddRef,
    [2] = cwv2_Release,
    [3] = cwv2_GetIids,
    [4] = cwv2_GetRuntimeClassName,
    [5] = cwv2_GetTrustLevel,
    [50] = cwv2_generic_slot,

    [57] = cwv2_generic_slot,

    [64 ... 255] = cwv2_generic_slot,
    [6] = cwv2_get_Settings,
    [7] = cwv2_get_Source,
    [8] = cwv2_get_BrowserProcessId,
    [9] = cwv2_get_CanGoBack,
    [10] = cwv2_get_CanGoForward,
    [11] = cwv2_get_DocumentTitle,
    [12] = cwv2_get_ContainsFullScreenElement,
    [13] = cwv2_add_NavigationStarting,
    [14] = cwv2_remove_NavigationStarting,
    [15] = cwv2_add_ContentLoading,
    [16] = cwv2_remove_ContentLoading,
    [17] = cwv2_add_SourceChanged,
    [18] = cwv2_remove_SourceChanged,
    [19] = cwv2_add_HistoryChanged,
    [20] = cwv2_remove_HistoryChanged,
    [21] = cwv2_add_NavigationCompleted,
    [22] = cwv2_remove_NavigationCompleted,
    [23] = cwv2_add_FrameNavigationStarting,
    [24] = cwv2_remove_FrameNavigationStarting,
    [25] = cwv2_add_FrameNavigationCompleted,
    [26] = cwv2_remove_FrameNavigationCompleted,
    [27] = cwv2_add_ScriptDialogOpening,
    [28] = cwv2_remove_ScriptDialogOpening,
    [29] = cwv2_add_PermissionRequested,
    [30] = cwv2_remove_PermissionRequested,
    [31] = cwv2_add_ProcessFailed,
    [32] = cwv2_remove_ProcessFailed,
    [33] = cwv2_add_WebMessageReceived,
    [34] = cwv2_remove_WebMessageReceived,
    [35] = cwv2_add_NewWindowRequested,
    [36] = cwv2_remove_NewWindowRequested,
    [37] = cwv2_add_DocumentTitleChanged,
    [38] = cwv2_remove_DocumentTitleChanged,
    [39] = cwv2_add_ContainsFullScreenElementChanged,
    [40] = cwv2_remove_ContainsFullScreenElementChanged,
    [41] = cwv2_add_WebResourceRequested,
    [42] = cwv2_remove_WebResourceRequested,
    [43] = cwv2_add_WindowCloseRequested,
    [44] = cwv2_remove_WindowCloseRequested,
    [45] = cwv2_Navigate,
    [46] = cwv2_NavigateToString,
    [47] = cwv2_AddScriptToExecuteOnDocumentCreatedAsync,
    [48] = cwv2_RemoveScriptToExecuteOnDocumentCreated,
    [49] = cwv2_ExecuteScriptAsync,
    [51] = cwv2_Reload,
    [52] = cwv2_PostWebMessageAsJson,
    [53] = cwv2_PostWebMessageAsString,
    [54] = cwv2_CallDevToolsProtocolMethodAsync,
    [55] = cwv2_GoBack,
    [56] = cwv2_GoForward,
    [58] = cwv2_Stop,
    [59] = cwv2_AddHostObjectToScript,
    [60] = cwv2_RemoveHostObjectFromScript,
    [61] = cwv2_OpenDevToolsWindow,
    [62] = cwv2_AddWebResourceRequestedFilter,
    [63] = cwv2_RemoveWebResourceRequestedFilter,
};

static const void *cwv2_2_vtbl[256] =
{
    [0] = cwv2_2_QueryInterface,
    [1] = cwv2_2_AddRef,
    [2] = cwv2_2_Release,
    [3] = cwv2_GetIids,
    [4] = cwv2_GetRuntimeClassName,
    [5] = cwv2_GetTrustLevel,
    [6] = cwv2_generic_slot,

    [13 ... 255] = cwv2_generic_slot,
    [7] = cwv2_2_get_Environment,
    [8] = cwv2_2_add_WebResourceResponseReceived,
    [9] = cwv2_2_remove_WebResourceResponseReceived,
    [10] = cwv2_2_add_DOMContentLoaded,
    [11] = cwv2_2_remove_DOMContentLoaded,
    [12] = cwv2_2_NavigateWithWebResourceRequest,
};

static const void *cwv2_3_vtbl[256] =
{
    [0] = cwv2_3_QueryInterface,
    [1] = cwv2_3_AddRef,
    [2] = cwv2_3_Release,
    [3] = cwv2_GetIids,
    [4] = cwv2_GetRuntimeClassName,
    [5] = cwv2_GetTrustLevel,
    [6 ... 255] = cwv2_generic_slot,
};

static const void *cwv2_11_vtbl[256] =
{
    [0] = cwv2_11_QueryInterface,
    [1] = cwv2_11_AddRef,
    [2] = cwv2_11_Release,
    [3] = cwv2_GetIids,
    [4] = cwv2_GetRuntimeClassName,
    [5] = cwv2_GetTrustLevel,
    [9 ... 255] = cwv2_generic_slot,
    [6] = cwv2_11_add_ContextMenuRequested,
    [7] = cwv2_11_remove_ContextMenuRequested,
    [8] = cwv2_11_CallDevToolsProtocolMethodForSessionAsync,
};

static const void *cwv2_13_vtbl[256] =
{
    [0] = cwv2_13_QueryInterface,
    [1] = cwv2_13_AddRef,
    [2] = cwv2_13_Release,
    [3] = cwv2_GetIids,
    [4] = cwv2_GetRuntimeClassName,
    [5] = cwv2_GetTrustLevel,
    [7 ... 255] = cwv2_generic_slot,
    [6] = cwv2_get_Profile,
};

static const void *cwv2_stub_vtbl[256] =
{
    [0] = cwv2_stub_QueryInterface,
    [1] = cwv2_stub_AddRef,
    [2] = cwv2_stub_Release,
    [3] = cwv2_GetIids,
    [4] = cwv2_GetRuntimeClassName,
    [5] = cwv2_GetTrustLevel,
    [6 ... 255] = cwv2_generic_slot,
};

const GUID IID_ICoreWebView2InitializedEventArgs =
    { 0xee59d277, 0x8b2e, 0x57ab, { 0x86, 0x31, 0x91, 0xd2, 0x7b, 0x12, 0xeb, 0xd9 } };

void wv2_defer_start_browser( struct xaml_element *impl )
{
    HANDLE thread;

    g_active_wv2_impl = impl;
    if (impl->browser_queued) return;
    impl->browser_queued = TRUE;
    if ((thread = CreateThread( NULL, 0, wv2_sta_thread, impl, 0, NULL )))
        CloseHandle( thread );
    else
        wv2_start_browser( impl );
}

static HRESULT WINAPI wv2_add_CoreWebView2Initialized( void *iface, void *handler, EventRegistrationToken *token )
{
    struct xaml_element *impl = impl_from_IWebView2( iface );
    ERR( "wv2_add_CoreWebView2Initialized iface %p, handler %p, token %p\n", iface, handler, token );
    impl->init_handler = (IInspectable *)handler;
    if (impl->init_handler) IInspectable_AddRef( impl->init_handler );
    if (token) token->value = 1;
    wv2_defer_start_browser( impl );
    return S_OK;
}

static HRESULT WINAPI wv2_remove_CoreWebView2Initialized( void *iface, EventRegistrationToken token )
{
    TRACE( "wv2_remove_CoreWebView2Initialized iface %p\n", iface );
    return S_OK;
}

static HSTRING wv2_extract_uri_string( IInspectable *obj )
{
    static const GUID IID_IUriRuntimeClass_local =
        { 0x9e365e57, 0x48b2, 0x4160, { 0x95, 0x6f, 0xc7, 0x38, 0x51, 0x20, 0xbb, 0xfc } };
    static const GUID IID_IPropertyValue_local =
        { 0x4bd682dd, 0x7554, 0x40e9, { 0x9a, 0x9b, 0x82, 0x65, 0x4e, 0xde, 0x7e, 0x62 } };
    void *uri_rt = NULL, *pv = NULL;
    HSTRING hstr = NULL;

    if (!obj) return NULL;
    if (SUCCEEDED(IInspectable_QueryInterface( obj, &IID_IUriRuntimeClass_local, &uri_rt )) && uri_rt)
    {
        if (FAILED(IUriRuntimeClass_get_AbsoluteUri( (IUriRuntimeClass *)uri_rt, &hstr )) || !hstr)
        {
            IUriRuntimeClass_get_RawUri( (IUriRuntimeClass *)uri_rt, &hstr );
        }
        IInspectable_Release( (IInspectable *)uri_rt );
        if (hstr) return hstr;
    }
    if (SUCCEEDED(IInspectable_QueryInterface( obj, &IID_IPropertyValue_local, &pv )) && pv)
    {
        IPropertyValue_GetString( (IPropertyValue *)pv, &hstr );
        IInspectable_Release( (IInspectable *)pv );
    }
    return hstr;
}

static void wv2_navigate_once( struct xaml_element *impl )
{
    const WCHAR *url = NULL;

    if (impl->navigated) return;
    if (!impl->loader_core) { ERR( "wv2_navigate_once: no loader_core\n" ); return; }
    if (impl->wv2_source_uri)
        url = WindowsGetStringRawBuffer( impl->wv2_source_uri, NULL );
    else if (impl->core_webview2)
    {
        struct core_webview2 *cwv = impl_from_cwv2( impl->core_webview2 );
        if (cwv->source_uri)
            url = WindowsGetStringRawBuffer( cwv->source_uri, NULL );
    }
    if (!url || !*url)
    {
        ERR( "wv2_navigate_once: no WebView2.Source URI set, deferring navigation\n" );
        return;
    }
    impl->navigated = TRUE;
    {
        HRESULT hr = wv2_call_navigate( impl->loader_core, impl->real_loader, 5, 6, url );
        ERR( "wv2_navigate_once (%s): loader_core %p url=%s hr %#lx\n",
             impl->real_loader ? "real" : "builtin", impl->loader_core, debugstr_w( url ), hr );
    }
}

static HRESULT WINAPI wv2_get_Source( void *iface, IInspectable **value )
{
    struct xaml_element *impl = impl_from_IWebView2( iface );
    if (!value) return E_POINTER;
    *value = impl->wv2_source;
    if (*value) IInspectable_AddRef( *value );
    return S_OK;
}

static HRESULT WINAPI wv2_put_Source( void *iface, IInspectable *value )
{
    struct xaml_element *impl = impl_from_IWebView2( iface );
    HSTRING hstr;

    if (impl->wv2_source) IInspectable_Release( impl->wv2_source );
    impl->wv2_source = value;
    if (impl->wv2_source) IInspectable_AddRef( impl->wv2_source );

    if (impl->wv2_source_uri)
    {
        WindowsDeleteString( impl->wv2_source_uri );
        impl->wv2_source_uri = NULL;
    }
    hstr = wv2_extract_uri_string( value );
    if (hstr)
    {
        impl->wv2_source_uri = hstr;
        ERR( "wv2_put_Source: set Source URI %s on %p\n", debugstr_hstring( hstr ), impl );
        if (impl->core_webview2)
        {
            struct core_webview2 *cwv = impl_from_cwv2( impl->core_webview2 );
            if (cwv->source_uri) WindowsDeleteString( cwv->source_uri );
            cwv->source_uri = NULL;
            WindowsDuplicateString( hstr, &cwv->source_uri );
        }
        if (impl->loader_core)
        {
            impl->navigated = FALSE;
            wv2_navigate_once( impl );
        }
    }
    return S_OK;
}

void trigger_core_webview2_initialized( struct xaml_element *impl, void *iface )
{
    if (!impl->core_webview2)
    {
        struct core_webview2 *cwv = calloc( 1, sizeof(*cwv) );
        if (cwv)
        {
            cwv->IInspectable_iface.lpVtbl = (const struct IInspectableVtbl *)vtbl_dup( cwv2_vtbl );
            cwv->ICoreWebView2_2_iface.lpVtbl = (const struct IInspectableVtbl *)vtbl_dup( cwv2_2_vtbl );
            cwv->ICoreWebView2_3_iface.lpVtbl = (const struct IInspectableVtbl *)vtbl_dup( cwv2_3_vtbl );
            cwv->ICoreWebView2_11_iface.lpVtbl = (const struct IInspectableVtbl *)vtbl_dup( cwv2_11_vtbl );
            cwv->ICoreWebView2_13_iface.lpVtbl = (const struct IInspectableVtbl *)vtbl_dup( cwv2_13_vtbl );
            cwv->ICoreWebView2_stub_iface.lpVtbl = (const struct IInspectableVtbl *)vtbl_dup( cwv2_stub_vtbl );
            cwv->ref = 1;
            cwv->real_loader = impl->real_loader;
            cwv->loader_core = impl->loader_core;
            cwv->owner = impl;
            if (impl->wv2_source_uri)
                WindowsDuplicateString( impl->wv2_source_uri, &cwv->source_uri );
            impl->core_webview2 = &cwv->IInspectable_iface;
        }
    }
    else if (impl->loader_core)
    {
        struct core_webview2 *cwv = impl_from_cwv2( impl->core_webview2 );
        cwv->loader_core = impl->loader_core;
        cwv->real_loader = impl->real_loader;
    }
    if (impl->init_handler && !impl->init_triggered)
    {
        struct cwv2_args *args = calloc( 1, sizeof(*args) );
        struct cwv2_typed_handler *h = (struct cwv2_typed_handler *)impl->init_handler;
        impl->init_triggered = TRUE;
        if (args)
        {
            args->IInspectable_iface.lpVtbl = (const struct IInspectableVtbl *)vtbl_dup( cwv2_args_vtbl );
            args->ref = 1;
            ERR( "invoking init_handler %p on WebView %p with args %p\n", impl->init_handler, &impl->IInspectable_iface, args );
            h->lpVtbl->Invoke( h, &impl->IInspectable_iface, &args->IInspectable_iface );
            cwv2_args_Release( &args->IInspectable_iface );
        }
    }
    wv2_navigate_once( impl );
}

/* Drive the real browser: the webview2loader builtin launches msedgewebview2
 * (env creation) and hands back an environment/controller/CoreWebView2 chain.
 * Kick it off from EnsureCoreWebView2Async so the app's init handler receives a
 * live CoreWebView2 and its Navigate reaches the browser over the mojo pipes. */
struct wv2_env_handler
{
    const void **vtbl;
    LONG ref;
    struct xaml_element *impl;
};

struct wv2_ctrl_handler
{
    const void **vtbl;
    LONG ref;
    struct xaml_element *impl;
};

const GUID IID_ICoreWebView2CreateEnvHandler =
    { 0x4e8a3389, 0xc9d8, 0x4bd2, { 0xb6, 0xb5, 0x12, 0x4f, 0xee, 0x6c, 0xc1, 0x4d } };
const GUID IID_ICoreWebView2CreateCtrlHandler =
    { 0x6c4819f3, 0xc9b7, 0x4260, { 0x81, 0x27, 0xc9, 0xf5, 0xbd, 0xe7, 0xf6, 0x8c } };
const GUID IID_ICoreWebView2CreateCompCtrlHandler =
    { 0x02fab84b, 0x1428, 0x4fb7, { 0xad, 0x45, 0x1b, 0x2e, 0x64, 0x73, 0x61, 0x84 } };
const GUID IID_ICoreWebView2Environment3_g =
    { 0x80a22ae3, 0xbe7c, 0x4ce2, { 0xaf, 0xe1, 0x5a, 0x50, 0x05, 0x6c, 0xde, 0xeb } };
const GUID IID_ICoreWebView2CompositionController_g =
    { 0x3df9b733, 0xb9ae, 0x4a15, { 0x86, 0xb4, 0xeb, 0x9e, 0xe9, 0x82, 0x64, 0x69 } };
const GUID IID_ICoreWebView2Controller_g =
    { 0x4d00c0d1, 0x9434, 0x4eb6, { 0x80, 0x78, 0x86, 0x97, 0xa5, 0x60, 0x33, 0x4f } };

static HRESULT WINAPI wv2h_QueryInterface( void *t, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_ICoreWebView2CreateEnvHandler ) ||
        IsEqualGUID( iid, &IID_ICoreWebView2CreateCtrlHandler ) ||
        IsEqualGUID( iid, &IID_ICoreWebView2CreateCompCtrlHandler ))
    {
        *out = t;
        InterlockedIncrement( (LONG *)((char *)t + sizeof(void *)) );
        return S_OK;
    }
    ERR( "wv2h_QueryInterface: unknown iid %s\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI wv2h_AddRef( void *t )
{
    LONG *ref = (LONG *)((char *)t + sizeof(void *));
    return InterlockedIncrement( ref );
}

static ULONG WINAPI wv2h_Release( void *t )
{
    LONG *ref = (LONG *)((char *)t + sizeof(void *));
    ULONG r = InterlockedDecrement( ref );
    if (!r)
    {
        HeapFree( GetProcessHeap(), 0, *(void **)t );
        HeapFree( GetProcessHeap(), 0, t );
    }
    return r;
}

static HRESULT WINAPI wv2_ctrl_handler_Invoke( void *t, HRESULT hr, void *controller )
{
    struct wv2_ctrl_handler *h = t;
    void *core = NULL;

    ERR( "wv2_ctrl_handler_Invoke hr %#lx controller %p real=%d\n", hr, controller, h->impl->real_loader );
    if (FAILED( hr ))
    {
        ERR( "wv2_ctrl_handler_Invoke: controller creation failed (%#lx), ignoring broken controller\n", hr );
        return hr;
    }
    if (controller)
    {
        if (h->impl->real_loader)
        {
            static BOOL bounds_probed;
            HWND parent = h->impl->parent_hwnd ? h->impl->parent_hwnd : wv2_get_controller_parent();
            SIZE sz;
            float scale = 1.0f;
            RECT rc;
            HRESULT hr_b = S_OK, hr_v = S_OK, hr_c = S_OK;
            ICoreWebView2CompositionController *comp_ctrl = NULL;
            ICoreWebView2Controller *ctrl = NULL;

            xaml_get_layout_metrics( parent, &sz, &scale );
            SetRect( &rc, 0, 0, sz.cx, sz.cy );
            if (parent && IsWindow( parent ))
            {
                RECT cr;
                if (GetClientRect( parent, &cr ) && cr.right > 0 && cr.bottom > 0)
                    rc = cr;
            }

            if (SUCCEEDED( ((IUnknown *)controller)->lpVtbl->QueryInterface(
                    (IUnknown *)controller, &IID_ICoreWebView2CompositionController_g, (void **)&comp_ctrl) ) && comp_ctrl)
            {
                IDCompositionVisual *root = xaml_get_dcomp_root_visual();
                IDCompositionVisual *vis = h->impl->visual;
                D2D_RECT_F clip = { 0.0f, 0.0f, (float)rc.right, (float)rc.bottom };
                if (!vis) { xaml_create_dcomp_visual( &h->impl->visual ); vis = h->impl->visual; }
                if (root)
                    IDCompositionVisual_SetClip( root, &clip );
                if (vis)
                    IDCompositionVisual_SetClip( vis, &clip );
                if (root && vis && vis != root && !h->impl->visual_attached)
                {
                    HRESULT hr_add = IDCompositionVisual_AddVisual( root, vis, TRUE, NULL );
                    if (SUCCEEDED( hr_add ))
                    {
                        h->impl->visual_attached = TRUE;
                        xaml_dcomp_commit();
                    }
                    ERR( "wv2_ctrl_handler_Invoke: AddVisual root=%p vis=%p scale=%.2f hr=%#lx\n", root, vis, scale, hr_add );
                }
                ERR( "wv2_ctrl_handler_Invoke: found CompositionController %p visual=%p root=%p scale=%.2f\n", comp_ctrl, vis, root, scale );
                if (vis)
                {
                    HRESULT hr_rv = comp_ctrl->lpVtbl->put_RootVisualTarget( comp_ctrl, (IUnknown *)vis );
                    if (SUCCEEDED( hr_rv ))
                    {
                        h->impl->has_comp_ctrl = TRUE;
                        xaml_dcomp_commit();
                    }
                    ERR( "wv2_ctrl_handler_Invoke: put_RootVisualTarget hr=%#lx\n", hr_rv );
                }
                h->impl->loader_comp_ctrl = comp_ctrl;
            }

            if (SUCCEEDED( ((IUnknown *)controller)->lpVtbl->QueryInterface(
                    (IUnknown *)controller, &IID_ICoreWebView2Controller_g, (void **)&ctrl) ) && ctrl)
            {
                h->impl->loader_controller = ctrl;
                if (!bounds_probed)
                {
                    bounds_probed = TRUE;
                    wv2_probe_bounds( ctrl );
                }
                if (parent) ctrl->lpVtbl->put_ParentWindow( ctrl, parent );
                hr_b = ctrl->lpVtbl->put_Bounds( ctrl, rc );
                hr_v = ctrl->lpVtbl->put_IsVisible( ctrl, TRUE );
                ctrl->lpVtbl->NotifyParentWindowPositionChanged( ctrl );
                hr_c = ctrl->lpVtbl->get_CoreWebView2( ctrl, &core );
                ERR( "wv2_ctrl_handler_Invoke (ctrl): parent=%p rc=%ldx%ld put_Bounds=%#lx put_IsVisible=%#lx get_CoreWebView2=%#lx core=%p\n",
                     parent, rc.right, rc.bottom, hr_b, hr_v, hr_c, core );
            }
            else if (!comp_ctrl)
            {
                /* Last-resort fallback only when controller is NOT a 7-slot CompositionController */
                h->impl->loader_controller = controller;
                ((IUnknown *)controller)->lpVtbl->AddRef( (IUnknown *)controller );
                if (parent) wv2_call_hwnd( controller, TRUE, 22, -1, parent );
                hr_b = wv2_call_bounds( controller, TRUE, 6, 4, rc );
                hr_v = wv2_call_bool( controller, TRUE, 4, 4, TRUE );
                wv2_call_void( controller, TRUE, 23, -1 );
                hr_c = wv2_call_get_core( controller, TRUE, 25, 7, &core );
                ERR( "wv2_ctrl_handler_Invoke (fallback): parent=%p rc=%ldx%ld put_Bounds=%#lx put_IsVisible=%#lx hr_c=%#lx core=%p\n",
                     parent, rc.right, rc.bottom, hr_b, hr_v, hr_c, core );
            }
            wv2_ensure_sta_hwnd();
            wv2_ensure_host_subclass( parent );
            wv2_sync_child_hwnds( parent, rc.right, rc.bottom );
            if (g_wv2_sta_hwnd && h->impl->has_comp_ctrl)
                SetTimer( g_wv2_sta_hwnd, 0x5757, 1000, NULL );

            if (core && !h->impl->loader_core)
            {
                h->impl->loader_core = core;
                if (h->impl->core_webview2)
                {
                    struct core_webview2 *cwv = impl_from_cwv2( h->impl->core_webview2 );
                    void *ls = NULL, *ls2 = NULL;
                    struct cwv2_pending_op *op, *next;
                    int replayed = 0;

                    cwv->loader_core = core;
                    cwv->real_loader = h->impl->real_loader;
                    for (op = cwv->pending_head; op; op = next)
                    {
                        next = op->next;
                        switch (op->kind)
                        {
                        case CWV2_OP_SETTING_BOOL:
                        case CWV2_OP_SETTING_UA:
                            if (!ls && *(void ***)core)
                            {
                                HRESULT (WINAPI *get_set)( void *, void ** ) = (*(void ***)core)[3];
                                if (get_set && (ULONG_PTR)get_set < (ULONG_PTR)0x7ffc00000000)
                                {
                                    __TRY
                                    {
                                        if (SUCCEEDED( get_set( core, &ls ) ) && ls)
                                            ((IUnknown *)ls)->lpVtbl->QueryInterface( ls, &IID_ICoreWebView2Settings2_com, &ls2 );
                                    }
                                    __EXCEPT_PAGE_FAULT
                                    {
                                        ERR( "cwv2_replay: core->get_Settings faulted\n" );
                                        ls = NULL;
                                    }
                                    __ENDTRY;
                                }
                            }
                            if (op->kind == CWV2_OP_SETTING_BOOL && ls && *(void ***)ls)
                            {
                                HRESULT (WINAPI *fn)( void *, BOOL ) = (*(void ***)ls)[op->arg_int];
                                if (fn && (ULONG_PTR)fn < (ULONG_PTR)0x7ffc00000000) fn( ls, op->arg_bool );
                            }
                            else if (op->kind == CWV2_OP_SETTING_UA && ls2 && *(void ***)ls2)
                            {
                                HRESULT (WINAPI *fn)( void *, LPCWSTR ) = (*(void ***)ls2)[22];
                                if (fn && (ULONG_PTR)fn < (ULONG_PTR)0x7ffc00000000) fn( ls2, op->str1 ? op->str1 : L"" );
                            }
                            break;
                        case CWV2_OP_ADD_HOST_OBJECT:
                            if (cwv->real_loader)
                            {
                                HRESULT (WINAPI *fn)( void *, LPCWSTR, VARIANT * ) = wv2_slot_fn( core, TRUE, 49, -1 );
                                if (fn && (ULONG_PTR)fn < (ULONG_PTR)0x7ffc00000000)
                                {
                                    VARIANT v;
                                    IDispatch *disp = NULL;
                                    HRESULT hr_h;
                                    memset( &v, 0, sizeof(v) );
                                    if (op->obj && SUCCEEDED( ((IUnknown *)op->obj)->lpVtbl->QueryInterface( op->obj, &IID_IDispatch, (void **)&disp ) ) && disp)
                                    {
                                        V_VT( &v ) = VT_DISPATCH;
                                        V_DISPATCH( &v ) = disp;
                                    }
                                    else
                                    {
                                        V_VT( &v ) = VT_UNKNOWN;
                                        V_UNKNOWN( &v ) = (IUnknown *)op->obj;
                                        if (op->obj) ((IUnknown *)op->obj)->lpVtbl->AddRef( (IUnknown *)op->obj );
                                    }
                                    hr_h = fn( core, op->str1 ? op->str1 : L"", &v );
                                    if (V_UNKNOWN( &v )) ((IUnknown *)V_UNKNOWN( &v ))->lpVtbl->Release( V_UNKNOWN( &v ) );
                                    ERR( "cwv2_replay AddHostObjectToScript(%s) -> %#lx\n", debugstr_w( op->str1 ), hr_h );
                                }
                            }
                            break;
                        case CWV2_OP_ADD_SCRIPT:
                            if (cwv->real_loader)
                            {
                                HRESULT (WINAPI *fn)( void *, LPCWSTR, void * ) = wv2_slot_fn( core, TRUE, 27, -1 );
                                if (fn && (ULONG_PTR)fn < (ULONG_PTR)0x7ffc00000000)
                                    fn( core, op->str1 ? op->str1 : L"", NULL );
                            }
                            break;
                        case CWV2_OP_ADD_WRS_FILTER:
                            if (cwv->real_loader)
                            {
                                HRESULT (WINAPI *fn)( void *, LPCWSTR, int ) = wv2_slot_fn( core, TRUE, 57, -1 );
                                if (fn && (ULONG_PTR)fn < (ULONG_PTR)0x7ffc00000000)
                                    fn( core, op->str1 ? op->str1 : L"", op->arg_int );
                            }
                            break;
                        case CWV2_OP_ADD_EVENT:
                        case CWV2_OP_ADD_EVENT2:
                        case CWV2_OP_ADD_EVENT11:
                        {
                            void *com_obj = core;
                            EventRegistrationToken dummy_tok;
                            if (op->kind == CWV2_OP_ADD_EVENT2)
                                com_obj = cwv2_get_com_iface( cwv, &cwv->loader_core2, &IID_ICoreWebView2_2_com );
                            else if (op->kind == CWV2_OP_ADD_EVENT11)
                                com_obj = cwv2_get_com_iface( cwv, &cwv->loader_core11, &IID_ICoreWebView2_11_com );
                            if (com_obj)
                                cwv2_forward_add_event( cwv, com_obj, op->arg_int, op->obj, &dummy_tok, op->avtbl, op->aiid );
                            break;
                        }
                        case CWV2_OP_DEVTOOLS:
                            if (cwv->real_loader)
                            {
                                HRESULT (WINAPI *fn)( void *, LPCWSTR, LPCWSTR, void * ) = wv2_slot_fn( core, TRUE, 36, -1 );
                                if (fn && (ULONG_PTR)fn < (ULONG_PTR)0x7ffc00000000)
                                    fn( core, op->str1 ? op->str1 : L"", op->str2 ? op->str2 : L"{}", NULL );
                            }
                            break;
                        }
                        if (op->obj) ((IUnknown *)op->obj)->lpVtbl->Release( (IUnknown *)op->obj );
                        free( op->str1 );
                        free( op->str2 );
                        free( op );
                        replayed++;
                    }
                    cwv->pending_head = cwv->pending_tail = NULL;
                    if (ls2) ((IUnknown *)ls2)->lpVtbl->Release( (IUnknown *)ls2 );
                    if (ls) ((IUnknown *)ls)->lpVtbl->Release( (IUnknown *)ls );
                    ERR( "wv2_ctrl_handler_Invoke: replayed %d queued core_webview2 ops\n", replayed );
                }
                wv2_navigate_once( h->impl );
                if (h->impl->core_webview2)
                {
                    struct core_webview2 *cwv = impl_from_cwv2( h->impl->core_webview2 );
                    cwv->loader_core = h->impl->loader_core;
                    cwv->real_loader = h->impl->real_loader;
                }
                if (!h->impl->init_triggered)
                {
                    if (g_wv2_subclassed_hwnd && IsWindow( g_wv2_subclassed_hwnd ))
                        PostMessageW( g_wv2_subclassed_hwnd, WM_APP_WV2_INIT, 0, (LPARAM)h->impl );
                    else
                        trigger_core_webview2_initialized( h->impl, &h->impl->IWebView2_iface );
                }
            }
            return S_OK;
        }

        if (SUCCEEDED( wv2_call_get_core( controller, FALSE, 25, 7, &core ) ) && core && !h->impl->loader_core)
        {
            /* Keep the loader object as the forward target; trigger creates the
             * cwv2 shell (static vtbl) as the RCW the app sees (r46 lesson: the
             * loader heap-vtbl object crashes ComWrappers with 0x80131506). */
            h->impl->loader_core = core;
            trigger_core_webview2_initialized( h->impl, &h->impl->IWebView2_iface );
        }
    }
    return S_OK;
}

/* Heap vtbls (per-slot code stores): static .rdata vtbl arrays live in shared
 * builtin pages that get relocated per-page by whichever process maps them
 * first (app vs msedgewebview2 child), so their entries read back as child
 * image-base VAs in the app (r39: ctrl handler dispatch jumped to a stale
 * child VA -> privileged instruction). */
static const void **wv2_ctrl_handler_vtbl_fill( void )
{
    const void **vt = HeapAlloc( GetProcessHeap(), 0, 4 * sizeof(*vt) );
    if (vt)
    {
        vt[0] = wv2h_QueryInterface;
        vt[1] = wv2h_AddRef;
        vt[2] = wv2h_Release;
        vt[3] = wv2_ctrl_handler_Invoke;
    }
    return vt;
}

static HRESULT WINAPI wv2_env_handler_Invoke( void *t, HRESULT hr, void *env )
{
    struct wv2_env_handler *h = t;
    HRESULT (WINAPI *create_ctrl)( void *, HWND, void * );
    struct wv2_ctrl_handler *ch;
    HWND hw;

    ERR( "wv2_env_handler_Invoke hr %#lx env %p real=%d\n", hr, env, h->impl->real_loader );
    if (FAILED( hr ) || !env) return hr;
    if (h->impl->real_loader)
    {
        h->impl->loader_env = env;
        ((IUnknown *)env)->lpVtbl->AddRef( (IUnknown *)env );
    }
    hw = wv2_get_controller_parent();

    if (!h->impl->real_loader)
    {
        /* s430: prefer the synchronous loader export: one call, no handler vtbl hops */
        HMODULE loader = GetModuleHandleW( L"webview2loader.dll" );
        void *(WINAPI *p_sync)( HWND, HRESULT * );
        if (!loader) loader = LoadLibraryW( L"webview2loader.dll" );
        p_sync = (void *)GetProcAddress( loader, "CreateCoreWebView2ControllerSync" );
        if (p_sync)
        {
            HRESULT hr2 = S_OK;
            void *core;
            ERR( "wv2_env_handler_Invoke: controller parent hwnd %p\n", hw );
            core = p_sync( hw, &hr2 );
            ERR( "wv2_env_handler_Invoke: sync core %p hr %#lx\n", core, hr2 );
            if (SUCCEEDED( hr2 ) && core && !h->impl->core_webview2)
            {
                /* s460: the app's CoreCLR RCW chokes (0x80131506) on the loader
                 * webview object; hand it the builtin cwv2 shell (known-good RCW
                 * layout) and keep the loader object for Navigate forwarding. */
                h->impl->real_loader = FALSE;
                h->impl->loader_core = core;
                trigger_core_webview2_initialized( h->impl, &h->impl->IWebView2_iface );
            }
            {
                SIZE sz;
                xaml_get_layout_metrics( hw, &sz, NULL );
                wv2_sync_child_hwnds( hw, sz.cx, sz.cy );
            }
            return hr2;
        }
    }

    if (!(ch = HeapAlloc( GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*ch) ))) return E_OUTOFMEMORY;
    if (!(ch->vtbl = wv2_ctrl_handler_vtbl_fill()))
    {
        HeapFree( GetProcessHeap(), 0, ch );
        return E_OUTOFMEMORY;
    }
    ch->ref = 1;
    ch->impl = h->impl;
    ERR( "wv2_env_handler_Invoke: ch %p vt %p vt3 %p hw %p\n", ch, ch->vtbl,
         ((void **)ch->vtbl)[3], hw );

    if (h->impl->real_loader && !GetEnvironmentVariableW( L"WV2_WINDOWED", NULL, 0 ))
    {
        void *env3 = NULL;
        /* First try ICoreWebView2Environment3::CreateCoreWebView2CompositionController (slot 9) */
        if (SUCCEEDED( ((IUnknown *)env)->lpVtbl->QueryInterface(
                (IUnknown *)env, &IID_ICoreWebView2Environment3_g, &env3 ) ) && env3)
        {
            HRESULT (WINAPI *create_comp_ctrl)( void *, HWND, void * ) =
                (void *)(*(void ***)env3)[9];
            HRESULT hr_comp;
            ERR( "wv2_env_handler_Invoke (real): calling CreateCoreWebView2CompositionController env3=%p slot9=%p hw=%p\n",
                 env3, create_comp_ctrl, hw );
            hr_comp = create_comp_ctrl( env3, hw, ch );
            ERR( "wv2_env_handler_Invoke (real): CreateCoreWebView2CompositionController -> %#lx\n", hr_comp );
            ((IUnknown *)env3)->lpVtbl->Release( (IUnknown *)env3 );
            if (SUCCEEDED( hr_comp )) return hr_comp;
        }
    }

    create_ctrl = (void *)(*(void ***)env)[3];  /* ICoreWebView2Environment::CreateCoreWebView2Controller */
    ERR( "wv2_env_handler_Invoke: env vtbl %p slot3 %p\n", *(void **)env, create_ctrl );
    {
        HRESULT hr_c;
        ERR( "wv2_env_handler_Invoke: calling create_ctrl env=%p hw=%p vis=%d ch=%p\n",
             env, hw, hw ? IsWindowVisible(hw) : -1, ch );
        hr_c = create_ctrl( env, hw, ch );
        ERR( "wv2_env_handler_Invoke: create_ctrl -> %#lx\n", hr_c );
        return hr_c;
    }
}

static const void **wv2_env_handler_vtbl_fill( void )
{
    const void **vt = HeapAlloc( GetProcessHeap(), 0, 4 * sizeof(*vt) );
    if (vt)
    {
        vt[0] = wv2h_QueryInterface;
        vt[1] = wv2h_AddRef;
        vt[2] = wv2h_Release;
        vt[3] = wv2_env_handler_Invoke;
    }
    return vt;
}

static BOOL wv2_folder_has_runtime( const WCHAR *dir )
{
    WCHAR path[1024];
    if (!dir || !*dir) return FALSE;
    lstrcpynW( path, dir, ARRAY_SIZE(path) - 48 );
    lstrcatW( path, L"\\msedgewebview2.exe" );
    if (GetFileAttributesW( path ) == INVALID_FILE_ATTRIBUTES) return FALSE;
    lstrcpynW( path, dir, ARRAY_SIZE(path) - 48 );
    lstrcatW( path, L"\\EBWebView\\x64\\EmbeddedBrowserWebView.dll" );
    if (GetFileAttributesW( path ) != INVALID_FILE_ATTRIBUTES) return TRUE;
    lstrcpynW( path, dir, ARRAY_SIZE(path) - 48 );
    lstrcatW( path, L"\\EmbeddedBrowserWebView.dll" );
    return GetFileAttributesW( path ) != INVALID_FILE_ATTRIBUTES;
}

static int wv2_compare_versions( const WCHAR *a, const WCHAR *b )
{
    unsigned int av[4] = {0}, bv[4] = {0};
    int i;
    swscanf( a, L"%u.%u.%u.%u", &av[0], &av[1], &av[2], &av[3] );
    swscanf( b, L"%u.%u.%u.%u", &bv[0], &bv[1], &bv[2], &bv[3] );
    for (i = 0; i < 4; i++)
    {
        if (av[i] != bv[i]) return av[i] > bv[i] ? 1 : -1;
    }
    return 0;
}

static void wv2_extract_folder_version( const WCHAR *dir, WCHAR *out_ver, size_t out_len )
{
    WCHAR tmp[1024];
    WCHAR *slash;
    size_t len;

    out_ver[0] = 0;
    if (!dir || !*dir) return;
    lstrcpynW( tmp, dir, ARRAY_SIZE(tmp) );
    len = lstrlenW( tmp );
    while (len > 0 && (tmp[len - 1] == '\\' || tmp[len - 1] == '/'))
        tmp[--len] = 0;
    slash = wcsrchr( tmp, '\\' );
    if (!slash) slash = wcsrchr( tmp, '/' );
    slash = slash ? slash + 1 : tmp;
    if (slash[0] >= L'0' && slash[0] <= L'9')
        lstrcpynW( out_ver, slash, out_len );
}

static void wv2_add_runtime_candidate( WCHAR candidates[][1024], WCHAR versions[][64], int *ncand, int max_cand,
                                       const WCHAR *dir, const WCHAR *ver )
{
    WCHAR folder_ver[64] = {0};
    const WCHAR *stored_ver;
    int i;
    if (!dir || !*dir || !wv2_folder_has_runtime( dir )) return;
    wv2_extract_folder_version( dir, folder_ver, ARRAY_SIZE(folder_ver) );
    stored_ver = (ver && *ver && wcscmp( ver, L"9999.0.0.0" )) ? ver : folder_ver;
    for (i = 0; i < *ncand; i++)
    {
        if (!wcsicmp( candidates[i], dir ))
        {
            if (!versions[i][0] && stored_ver[0])
                lstrcpynW( versions[i], stored_ver, 64 );
            return;
        }
    }
    if (*ncand >= max_cand) return;

    /* Insert sorted newest-version-first when version string is known */
    i = *ncand;
    if (ver && *ver)
    {
        for (i = 0; i < *ncand; i++)
        {
            if (!versions[i][0] || wv2_compare_versions( ver, versions[i] ) > 0)
                break;
        }
        if (i < *ncand)
        {
            int j;
            for (j = *ncand; j > i; j--)
            {
                lstrcpynW( candidates[j], candidates[j - 1], 1024 );
                lstrcpynW( versions[j], versions[j - 1], 64 );
            }
        }
    }
    lstrcpynW( candidates[i], dir, 1024 );
    lstrcpynW( versions[i], stored_ver, 64 );
    (*ncand)++;
}

static void wv2_scan_app_base( const WCHAR *base_dir, WCHAR candidates[][1024], WCHAR versions[][64],
                               int *ncand, int max_cand )
{
    WCHAR pattern[1024];
    WIN32_FIND_DATAW fd;
    HANDLE hfind;

    if (!base_dir || !*base_dir) return;
    lstrcpynW( pattern, base_dir, ARRAY_SIZE(pattern) - 4 );
    lstrcatW( pattern, L"\\*" );
    hfind = FindFirstFileW( pattern, &fd );
    if (hfind == INVALID_HANDLE_VALUE) return;
    do
    {
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
            fd.cFileName[0] >= L'0' && fd.cFileName[0] <= L'9')
        {
            WCHAR full[1024];
            lstrcpynW( full, base_dir, ARRAY_SIZE(full) - 68 );
            lstrcatW( full, L"\\" );
            lstrcatW( full, fd.cFileName );
            wv2_add_runtime_candidate( candidates, versions, ncand, max_cand, full, fd.cFileName );
        }
    } while (FindNextFileW( hfind, &fd ));
    FindClose( hfind );
}

static void wv2_discover_runtimes( WCHAR candidates[][1024], WCHAR versions[][64], int *ncand, int max_cand, const WCHAR *exe_dir )
{
    static const GUID FOLDERID_ProgramFilesX86_g =
        { 0x7c5a40ef, 0xa0fb, 0x4bfc, { 0x87, 0x4a, 0xc0, 0xf2, 0xe0, 0xb9, 0xfa, 0x8e } };
    WCHAR env_browser[1024] = {0}, pf86[1024] = {0}, pf[1024] = {0}, app_dir[1024];
    HMODULE reg_mod, shell_mod;
    typedef LONG (WINAPI *p_RegOpenKeyExW_t)( HKEY, LPCWSTR, DWORD, REGSAM, PHKEY );
    typedef LONG (WINAPI *p_RegQueryValueExW_t)( HKEY, LPCWSTR, LPDWORD, LPDWORD, LPBYTE, LPDWORD );
    typedef LONG (WINAPI *p_RegCloseKey_t)( HKEY );
    typedef HRESULT (WINAPI *p_SHGetKnownFolderPath_t)( const GUID *, DWORD, HANDLE, PWSTR * );

    *ncand = 0;
    memset( versions, 0, sizeof(WCHAR) * 64 * max_cand );

    if (GetEnvironmentVariableW( L"WEBVIEW2_BROWSER_EXECUTABLE_FOLDER", env_browser, ARRAY_SIZE(env_browser) ))
        wv2_add_runtime_candidate( candidates, versions, ncand, max_cand, env_browser, L"9999.0.0.0" );

    if (ExpandEnvironmentStringsW( L"%ProgramFiles(x86)%", pf86, ARRAY_SIZE(pf86) ) == 0 || wcschr( pf86, L'%' ))
    {
        pf86[0] = 0;
        shell_mod = GetModuleHandleW( L"shell32.dll" );
        if (!shell_mod) shell_mod = LoadLibraryW( L"shell32.dll" );
        if (shell_mod)
        {
            p_SHGetKnownFolderPath_t p_known = (p_SHGetKnownFolderPath_t)GetProcAddress( shell_mod, "SHGetKnownFolderPath" );
            PWSTR p = NULL;
            if (p_known && SUCCEEDED( p_known( &FOLDERID_ProgramFilesX86_g, 0, NULL, &p ) ) && p)
            {
                lstrcpynW( pf86, p, ARRAY_SIZE(pf86) );
                CoTaskMemFree( p );
            }
        }
    }

    reg_mod = GetModuleHandleW( L"kernelbase.dll" );
    if (!reg_mod) reg_mod = LoadLibraryW( L"advapi32.dll" );
    if (reg_mod)
    {
        p_RegOpenKeyExW_t p_open = (p_RegOpenKeyExW_t)GetProcAddress( reg_mod, "RegOpenKeyExW" );
        p_RegQueryValueExW_t p_query = (p_RegQueryValueExW_t)GetProcAddress( reg_mod, "RegQueryValueExW" );
        p_RegCloseKey_t p_close = (p_RegCloseKey_t)GetProcAddress( reg_mod, "RegCloseKey" );
        if (p_open && p_query && p_close)
        {
            static const HKEY roots[] = { HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER };
            static const WCHAR *reg_paths[] = {
                L"SOFTWARE\\WOW6432Node\\Microsoft\\EdgeUpdate\\Clients\\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}",
                L"SOFTWARE\\Microsoft\\EdgeUpdate\\Clients\\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}",
            };
            size_t r, p;
            for (r = 0; r < ARRAY_SIZE(roots); r++)
            {
                for (p = 0; p < ARRAY_SIZE(reg_paths); p++)
                {
                    HKEY hk = NULL;
                    WCHAR pv[64] = {0}, loc[1024] = {0};
                    DWORD type = 0, cb;
                    REGSAM sam = KEY_READ | (p == 1 ? KEY_WOW64_32KEY : 0);
                    if (p_open( roots[r], reg_paths[p], 0, sam, &hk ) != ERROR_SUCCESS) continue;
                    cb = sizeof(pv);
                    if (p_query( hk, L"pv", NULL, &type, (BYTE *)pv, &cb ) != ERROR_SUCCESS || type != REG_SZ)
                        pv[0] = 0;
                    cb = sizeof(loc);
                    if (p_query( hk, L"location", NULL, &type, (BYTE *)loc, &cb ) == ERROR_SUCCESS && type == REG_SZ && loc[0])
                        wv2_add_runtime_candidate( candidates, versions, ncand, max_cand, loc, pv );
                    if (pv[0] && pf86[0])
                    {
                        lstrcpynW( app_dir, pf86, ARRAY_SIZE(app_dir) - 96 );
                        lstrcatW( app_dir, L"\\Microsoft\\EdgeWebView\\Application\\" );
                        lstrcatW( app_dir, pv );
                        wv2_add_runtime_candidate( candidates, versions, ncand, max_cand, app_dir, pv );
                    }
                    p_close( hk );
                }
            }
        }
    }

    if (pf86[0])
    {
        lstrcpynW( app_dir, pf86, ARRAY_SIZE(app_dir) - 48 );
        lstrcatW( app_dir, L"\\Microsoft\\EdgeWebView\\Application" );
        wv2_scan_app_base( app_dir, candidates, versions, ncand, max_cand );
    }
    if (ExpandEnvironmentStringsW( L"%ProgramFiles%", pf, ARRAY_SIZE(pf) ) > 0 && !wcschr( pf, L'%' ) &&
        (!pf86[0] || wcsicmp( pf, pf86 ) != 0))
    {
        lstrcpynW( app_dir, pf, ARRAY_SIZE(app_dir) - 48 );
        lstrcatW( app_dir, L"\\Microsoft\\EdgeWebView\\Application" );
        wv2_scan_app_base( app_dir, candidates, versions, ncand, max_cand );
    }

    if (exe_dir && *exe_dir)
        wv2_add_runtime_candidate( candidates, versions, ncand, max_cand, exe_dir, L"" );
}

static void wv2_resolve_default_udf( WCHAR *out_udf, size_t out_len )
{
    static const GUID FOLDERID_LocalAppData_g =
        { 0xf1b32785, 0x6fba, 0x4fcf, { 0x9d, 0x55, 0x7b, 0x8e, 0x7f, 0x15, 0x70, 0x91 } };
    typedef HRESULT (WINAPI *p_SHGetKnownFolderPath_t)( const GUID *, DWORD, HANDLE, PWSTR * );
    typedef int (WINAPI *p_SHCreateDirectoryExW_t)( HWND, LPCWSTR, const SECURITY_ATTRIBUTES * );
    typedef LONG (WINAPI *p_GetCurrentPackageFamilyName_t)( UINT32 *, PWSTR );
    HMODULE shell_mod, k32_mod;
    PWSTR local_app_data = NULL;
    WCHAR family[256] = {0};

    out_udf[0] = 0;
    shell_mod = GetModuleHandleW( L"shell32.dll" );
    if (!shell_mod) shell_mod = LoadLibraryW( L"shell32.dll" );
    if (shell_mod)
    {
        p_SHGetKnownFolderPath_t p_known = (p_SHGetKnownFolderPath_t)GetProcAddress( shell_mod, "SHGetKnownFolderPath" );
        p_SHCreateDirectoryExW_t p_mkdir = (p_SHCreateDirectoryExW_t)GetProcAddress( shell_mod, "SHCreateDirectoryExW" );
        if (p_known && SUCCEEDED( p_known( &FOLDERID_LocalAppData_g, 0, NULL, &local_app_data ) ) && local_app_data)
        {
            k32_mod = GetModuleHandleW( L"kernelbase.dll" );
            if (!k32_mod) k32_mod = GetModuleHandleW( L"kernel32.dll" );
            if (k32_mod)
            {
                p_GetCurrentPackageFamilyName_t p_fam =
                    (p_GetCurrentPackageFamilyName_t)GetProcAddress( k32_mod, "GetCurrentPackageFamilyName" );
                UINT32 flen = ARRAY_SIZE(family);
                if (p_fam && p_fam( &flen, family ) == ERROR_SUCCESS && family[0])
                {
                    swprintf( out_udf, out_len, L"%s\\Packages\\%s\\LocalCache\\EBWebView",
                              local_app_data, family );
                    if (GetFileAttributesW( out_udf ) == INVALID_FILE_ATTRIBUTES && p_mkdir)
                        p_mkdir( NULL, out_udf, NULL );
                    CoTaskMemFree( local_app_data );
                    return;
                }
            }
            swprintf( out_udf, out_len, L"%s\\Microsoft\\Edge\\User Data", local_app_data );
            CoTaskMemFree( local_app_data );
            return;
        }
    }
    if (ExpandEnvironmentStringsW( L"%LOCALAPPDATA%\\Microsoft\\Edge\\User Data", out_udf, out_len ) == 0 ||
        wcschr( out_udf, L'%' ))
        lstrcpynW( out_udf, L"C:\\Microsoft\\Edge\\User Data", out_len );
}

const GUID IID_ICoreWebView2EnvironmentOptions_g =
    { 0x2fde08a8, 0x1e9a, 0x4766, { 0x8c, 0x05, 0x95, 0xa9, 0xce, 0xb9, 0xd1, 0xc5 } };
struct wv2_env_options
{
    const void **vtbl;
    LONG ref;
    WCHAR args[1024];
    WCHAR target_version[64];
    HSTRING target_version_hstr;
};

static LPWSTR wv2_cotask_dup( const WCHAR *s )
{
    size_t len = s ? lstrlenW( s ) : 0;
    LPWSTR p = CoTaskMemAlloc( (len + 1) * sizeof(WCHAR) );
    if (p)
    {
        if (len) memcpy( p, s, len * sizeof(WCHAR) );
        p[len] = 0;
    }
    return p;
}

static HRESULT WINAPI wv2_opts_QueryInterface( void *t, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_ICoreWebView2EnvironmentOptions_g ))
    {
        *out = t;
        InterlockedIncrement( &((struct wv2_env_options *)t)->ref );
        return S_OK;
    }
    TRACE( "wv2_opts_QueryInterface: unknown iid %s\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI wv2_opts_AddRef( void *t )
{
    return InterlockedIncrement( &((struct wv2_env_options *)t)->ref );
}

static ULONG WINAPI wv2_opts_Release( void *t )
{
    struct wv2_env_options *o = t;
    ULONG r = InterlockedDecrement( &o->ref );
    if (!r)
    {
        if (o->target_version_hstr) WindowsDeleteString( o->target_version_hstr );
        HeapFree( GetProcessHeap(), 0, (void *)o->vtbl );
        HeapFree( GetProcessHeap(), 0, o );
    }
    return r;
}

static HRESULT WINAPI wv2_opts_get_AdditionalBrowserArguments( void *t, LPWSTR *val )
{
    struct wv2_env_options *o = t;
    if (!val) return E_POINTER;
    *val = wv2_cotask_dup( o->args );
    ERR( "wv2_opts_get_AdditionalBrowserArguments -> %s\n", debugstr_w( o->args ) );
    return *val ? S_OK : E_OUTOFMEMORY;
}

static HRESULT WINAPI wv2_opts_put_AdditionalBrowserArguments( void *t, LPCWSTR val )
{
    struct wv2_env_options *o = t;
    lstrcpynW( o->args, val ? val : L"", ARRAY_SIZE(o->args) );
    return S_OK;
}

static HRESULT WINAPI wv2_opts_get_Language( void *t, LPWSTR *val )
{
    if (!val) return E_POINTER;
    *val = NULL;
    return S_OK;
}

static HRESULT WINAPI wv2_opts_put_Language( void *t, LPCWSTR val )
{
    return S_OK;
}

static HRESULT WINAPI wv2_opts_get_TargetCompatibleBrowserVersion( void *t, LPWSTR *val )
{
    struct wv2_env_options *o = t;
    const WCHAR *ver = o->target_version_hstr
        ? WindowsGetStringRawBuffer( o->target_version_hstr, NULL )
        : (o->target_version[0] ? o->target_version : L"156.0.4314.42");
    if (!val) return E_POINTER;
    *val = wv2_cotask_dup( ver );
    return *val ? S_OK : E_OUTOFMEMORY;
}

static HRESULT WINAPI wv2_opts_put_TargetCompatibleBrowserVersion( void *t, LPCWSTR val )
{
    struct wv2_env_options *o = t;
    const WCHAR *ver = (val && *val) ? val : L"156.0.4314.42";
    if (o->target_version_hstr)
    {
        WindowsDeleteString( o->target_version_hstr );
        o->target_version_hstr = NULL;
    }
    WindowsCreateString( ver, lstrlenW( ver ), &o->target_version_hstr );
    lstrcpynW( o->target_version, ver, ARRAY_SIZE(o->target_version) );
    return S_OK;
}

static HRESULT WINAPI wv2_opts_get_AllowSSO( void *t, BOOL *val )
{
    if (val) *val = FALSE;
    return S_OK;
}

static HRESULT WINAPI wv2_opts_put_AllowSSO( void *t, BOOL val )
{
    return S_OK;
}

static struct wv2_env_options *wv2_create_env_options( struct xaml_element *impl, const WCHAR *discovered_ver )
{
    struct wv2_env_options *o;
    const void **vt;
    WCHAR env_args[512] = {0};
    WCHAR sf_txt[32] = L"1.00";
    WCHAR sf_arg[64] = {0};
    UINT dpi = 0;
    double sf = 1.0;
    LONG desktop_h = 0;
    SIZE sz;
    HWND hw = impl->parent_hwnd ? impl->parent_hwnd : wv2_get_controller_parent();

    o = HeapAlloc( GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*o) );
    if (!o) return NULL;
    vt = HeapAlloc( GetProcessHeap(), 0, 11 * sizeof(*vt) );
    if (!vt)
    {
        HeapFree( GetProcessHeap(), 0, o );
        return NULL;
    }
    vt[0] = wv2_opts_QueryInterface;
    vt[1] = wv2_opts_AddRef;
    vt[2] = wv2_opts_Release;
    vt[3] = wv2_opts_get_AdditionalBrowserArguments;
    vt[4] = wv2_opts_put_AdditionalBrowserArguments;
    vt[5] = wv2_opts_get_Language;
    vt[6] = wv2_opts_put_Language;
    vt[7] = wv2_opts_get_TargetCompatibleBrowserVersion;
    vt[8] = wv2_opts_put_TargetCompatibleBrowserVersion;
    vt[9] = wv2_opts_get_AllowSSO;
    vt[10] = wv2_opts_put_AllowSSO;
    o->vtbl = vt;
    o->ref = 1;
    wv2_opts_put_TargetCompatibleBrowserVersion( o, discovered_ver );

    GetEnvironmentVariableW( L"WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS", env_args, ARRAY_SIZE(env_args) );
    xaml_get_layout_metrics( hw, &sz, NULL );

    /* Chromium device scale factor, in order of precedence:
     *   1. an explicit --force-device-scale-factor in WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS wins;
     *   2. the real Linux/Wine DPI of the window. Xft.dpi / XSettings / Wayland display scale reach
     *      Wine as HKCU\Control Panel\Desktop\LogPixels, surfaced by GetDpiForWindow()
     *      (96 DPI = 1.00x, 144 = 1.50x, 192 = 2.00x);
     *   3. at the 96 DPI default, a desktop of <= 1024 px means a headless Xvfb gate lane
     *      (1024x768) where the login card only fits above the fold when zoomed out:
     *      desktop height / 2048 clamped to [0.50, 1.00];
     *   4. otherwise no override at all, i.e. the native 1.00x.
     */
    if (wcsstr( env_args, L"--force-device-scale-factor" ))
    {
        lstrcpynW( sf_txt, L"env", ARRAY_SIZE(sf_txt) );
    }
    else
    {
        if (hw && IsWindow( hw )) dpi = GetDpiForWindow( hw );
        if (!dpi) dpi = GetDpiForSystem();
        if (!dpi) dpi = 96;

        if (dpi != 96) sf = (double)dpi / 96.0;
        else
        {
            /* Headless Xvfb gate lanes: a 1024 px desktop (1024x768 screen + Wine virtual desktop
             * 1280x1024), where the WhatsApp login page (1600 px design height) only fits above the
             * fold when zoomed out. A real desktop (2560x1440) keeps the native 1.00x. */
            desktop_h = GetSystemMetrics( SM_CYSCREEN );
            if (desktop_h > 0 && desktop_h < 768)
            {
                sf = (double)desktop_h / 2048.0;
                if (sf < 0.5) sf = 0.5;
                if (sf > 1.0) sf = 1.0;
            }
        }
        swprintf( sf_txt, ARRAY_SIZE(sf_txt), L"%.2f", sf );
        if (sf != 1.0) swprintf( sf_arg, ARRAY_SIZE(sf_arg), L"--force-device-scale-factor=%.2f", sf );
    }

    if (env_args[0]) lstrcpynW( o->args, env_args, ARRAY_SIZE(o->args) );
    if (sf_arg[0])
    {
        if (o->args[0]) lstrcatW( o->args, L" " );
        lstrcatW( o->args, sf_arg );
    }
    ERR( "wv2_create_env_options: ver=%s args=%s dpi=%u cy=%ld desktop=%ld sf=%s\n",
         debugstr_w( o->target_version ), debugstr_w( o->args ), dpi, sz.cy, desktop_h,
         debugstr_w( sf_txt ) );
    return o;
}

void wv2_start_browser( struct xaml_element *impl )
{
    HRESULT (WINAPI *p_create)( LPCWSTR, LPCWSTR, void *, void * );
    struct wv2_env_handler *eh;
    HMODULE module = NULL;
    WCHAR exe_dir[1024] = {0};
    WCHAR udf[1024] = {0}, default_udf[1024] = {0};
    const WCHAR *browser_folder = NULL, *user_data_folder = NULL;

    if (impl->browser_started) return;
    impl->browser_started = TRUE;

    SetEnvironmentVariableW( L"WINE_HEADLESS_CONSOLE", L"1" );
    CoInitializeEx( NULL, COINIT_APARTMENTTHREADED );

    if (GetModuleFileNameW( NULL, exe_dir, ARRAY_SIZE(exe_dir) ))
    {
        WCHAR *slash = wcsrchr( exe_dir, '\\' );
        if (slash) *slash = 0;
    }

    /* Optional registry override HKLM\Software\Microsoft\EdgeUpdate\WebView2\LoaderName;
     * otherwise load the standard delegating webview2loader.dll. */
    {
        WCHAR loader_name[256] = {0};
        HKEY hkey = NULL;
        DWORD type = 0, cb = sizeof(loader_name);

        if (RegOpenKeyExW( HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\EdgeUpdate\\WebView2",
                           0, KEY_READ, &hkey ) == ERROR_SUCCESS)
        {
            if (RegQueryValueExW( hkey, L"LoaderName", NULL, &type, (BYTE *)loader_name, &cb ) == ERROR_SUCCESS &&
                type == REG_SZ && loader_name[0])
            {
                WCHAR cand_loader[1024] = {0};
                if (wcschr( loader_name, '\\' ) || wcschr( loader_name, '/' ))
                    lstrcpynW( cand_loader, loader_name, ARRAY_SIZE(cand_loader) );
                else if (exe_dir[0])
                    swprintf( cand_loader, ARRAY_SIZE(cand_loader), L"%s\\%s", exe_dir, loader_name );
                if (cand_loader[0] && GetFileAttributesW( cand_loader ) != INVALID_FILE_ATTRIBUTES)
                    module = LoadLibraryW( cand_loader );
                if (!module)
                    module = LoadLibraryW( loader_name );
                ERR( "wv2_start_browser: LoaderName(%s) -> %p\n", debugstr_w( loader_name ), module );
            }
            RegCloseKey( hkey );
        }
    }

    if (!module)
    {
        module = LoadLibraryW( L"webview2loader.dll" );
        ERR( "wv2_start_browser: LoadLibrary(webview2loader.dll) -> %p gle=%lu\n",
             module, GetLastError() );
    }

    if (module && (p_create = (void *)GetProcAddress( module, "CreateCoreWebView2EnvironmentWithOptions" )))
    {
        WCHAR candidates[16][1024];
        WCHAR versions[16][64];
        int ncand = 0, ci;

        wv2_discover_runtimes( candidates, versions, &ncand, ARRAY_SIZE(candidates) - 1, exe_dir );

        if (GetEnvironmentVariableW( L"WEBVIEW2_USER_DATA_FOLDER", udf, ARRAY_SIZE(udf) ) && udf[0])
            user_data_folder = udf;
        else
        {
            wv2_resolve_default_udf( default_udf, ARRAY_SIZE(default_udf) );
            user_data_folder = default_udf;
        }

        if ((eh = HeapAlloc( GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*eh) )))
        {
            eh->vtbl = wv2_env_handler_vtbl_fill();
            if (!eh->vtbl)
            {
                HeapFree( GetProcessHeap(), 0, eh );
                return;
            }
            eh->ref = 1;
            eh->impl = impl;
            impl->real_loader = TRUE;
            for (ci = 0; ci <= ncand; ci++)
            {
                HRESULT hr;
                const WCHAR *chosen_ver = (ci < ncand && versions[ci][0])
                    ? versions[ci]
                    : (ncand > 0 && versions[0][0] ? versions[0] : NULL);
                struct wv2_env_options *opts = wv2_create_env_options( impl, chosen_ver );
                browser_folder = (ci < ncand) ? candidates[ci] : NULL;
                ERR( "wv2_start_browser: try real loader[%d] browser=%s ver=%s udf=%s\n",
                     ci, debugstr_w( browser_folder ), debugstr_w( chosen_ver ), debugstr_w( user_data_folder ) );
                hr = p_create( browser_folder, user_data_folder, opts, eh );
                if (opts) wv2_opts_Release( opts );
                ERR( "wv2_start_browser: real CreateCoreWebView2EnvironmentWithOptions[%d] -> %#lx\n", ci, hr );
                if (SUCCEEDED( hr )) return;
            }
            /* Direct fallback: load EmbeddedBrowserWebView.dll from discovered runtime candidates */
            for (ci = 0; ci < ncand; ci++)
            {
                static const WCHAR *rel_dlls[] = {
                    L"\\EBWebView\\x64\\EmbeddedBrowserWebView.dll",
                    L"\\EmbeddedBrowserWebView.dll",
                };
                int ri;
                for (ri = 0; ri < (int)ARRAY_SIZE(rel_dlls); ri++)
                {
                    WCHAR eb_path[1024];
                    HMODULE eb;
                    HRESULT (WINAPI *p_int)( BOOL, int, LPCWSTR, void *, void * );
                    lstrcpynW( eb_path, candidates[ci], ARRAY_SIZE(eb_path) - 48 );
                    lstrcatW( eb_path, rel_dlls[ri] );
                    if (GetFileAttributesW( eb_path ) == INVALID_FILE_ATTRIBUTES) continue;
                    eb = LoadLibraryExW( eb_path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH );
                    ERR( "wv2_start_browser: LoadLibraryEx(%s) -> %p gle=%lu\n",
                         debugstr_w( eb_path ), eb, GetLastError() );
                    if (eb && (p_int = (void *)GetProcAddress( eb, "CreateWebViewEnvironmentWithOptionsInternal" )))
                    {
                        const WCHAR *chosen_ver = versions[ci][0] ? versions[ci] : (versions[0][0] ? versions[0] : NULL);
                        struct wv2_env_options *opts = wv2_create_env_options( impl, chosen_ver );
                        HRESULT hr = p_int( TRUE, 0, user_data_folder, opts, eh );
                        if (opts) wv2_opts_Release( opts );
                        ERR( "wv2_start_browser: CreateWebViewEnvironmentWithOptionsInternal -> %#lx\n", hr );
                        if (SUCCEEDED( hr )) return;
                    }
                }
            }
            impl->real_loader = FALSE;
            wv2h_Release( eh );
        }
    }

    if (!(module = LoadLibraryW( L"webview2loader.dll" )))
    {
        ERR( "wv2_start_browser: no webview2loader.dll\n" );
        return;
    }
    if (!(p_create = (void *)GetProcAddress( module, "CreateCoreWebView2EnvironmentWithOptions" )))
    {
        ERR( "wv2_start_browser: no CreateCoreWebView2EnvironmentWithOptions\n" );
        return;
    }
    if (!(eh = HeapAlloc( GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*eh) ))) return;
    if (!(eh->vtbl = wv2_env_handler_vtbl_fill()))
    {
        HeapFree( GetProcessHeap(), 0, eh );
        return;
    }
    eh->ref = 1;
    eh->impl = impl;
    impl->real_loader = FALSE;
    ERR( "wv2_start_browser: creating builtin environment\n" );
    p_create( NULL, NULL, NULL, eh );
}

static HRESULT WINAPI wv2_EnsureCoreWebView2Async( void *iface, void **op )
{
    struct xaml_element *impl = impl_from_IWebView2( iface );
    ERR( "wv2_EnsureCoreWebView2Async iface %p, op %p\n", iface, op );
    wv2_start_browser( impl );
    trigger_core_webview2_initialized( impl, iface );
    return xaml_async_action_create( (IAsyncAction **)op );
}

static HRESULT WINAPI wv22_EnsureCoreWebView2Async( void *iface, void *env, void **op )
{
    struct xaml_element *impl = impl_from_IWebView22( iface );
    ERR( "wv22_EnsureCoreWebView2Async iface %p, env %p, op %p\n", iface, env, op );
    wv2_defer_start_browser( impl );
    trigger_core_webview2_initialized( impl, iface );
    return xaml_async_action_create( (IAsyncAction **)op );
}

static HRESULT WINAPI wv22_EnsureCoreWebView2AsyncWithOptions( void *iface, void *env, void *options, void **op )
{
    struct xaml_element *impl = impl_from_IWebView22( iface );
    ERR( "wv22_EnsureCoreWebView2AsyncWithOptions iface %p, env %p, options %p, op %p\n", iface, env, options, op );
    trigger_core_webview2_initialized( impl, iface );
    return xaml_async_action_create( (IAsyncAction **)op );
}

/* IV1 (af86e2e0) slot-layout probe: 64 named variadic stubs cycling across
 * [6..255]; slot identity = (idx-6)%64, unique within 6..69. Writes NULL to
 * the first pointer out-param so getters hand back benign objects. */
#define WVP(i) \
    HRESULT WINAPI wv2_probe##i( void *iface, ... ) \
    { \
        void *a0; \
        va_list args; va_start( args, iface ); \
        a0 = va_arg( args, void * ); \
        va_end( args ); \
        ERR( "WVPROBE slot%u iface %p a0 %p\n", (i) + 6, iface, a0 ); \
        if (a0) *(void **)a0 = NULL; \
        return S_OK; \
    }
WVP(0) WVP(1) WVP(2) WVP(3) WVP(4) WVP(5) WVP(6) WVP(7)
WVP(8) WVP(9) WVP(10) WVP(11) WVP(12) WVP(13) WVP(14) WVP(15)
WVP(16) WVP(17) WVP(18) WVP(19) WVP(20) WVP(21) WVP(22) WVP(23)
WVP(24) WVP(25) WVP(26) WVP(27) WVP(28) WVP(29) WVP(30) WVP(31)
WVP(32) WVP(33) WVP(34) WVP(35) WVP(36) WVP(37) WVP(38) WVP(39)
WVP(40) WVP(41) WVP(42) WVP(43) WVP(44) WVP(45) WVP(46) WVP(47)
WVP(48) WVP(49) WVP(50) WVP(51) WVP(52) WVP(53) WVP(54) WVP(55)
WVP(56) WVP(57) WVP(58) WVP(59) WVP(60) WVP(61) WVP(62) WVP(63)
static HRESULT WINAPI wv2_do_QueryInterface( IInspectable *iface, REFIID iid, void **out )
{
    struct xaml_element *impl = impl_from_IDependencyObject( iface );
    return element_QueryInterface( &impl->IInspectable_iface, iid, out );
}
static ULONG WINAPI wv2_do_AddRef( IInspectable *iface )
{
    struct xaml_element *impl = impl_from_IDependencyObject( iface );
    return element_AddRef( &impl->IInspectable_iface );
}
static ULONG WINAPI wv2_do_Release( IInspectable *iface )
{
    struct xaml_element *impl = impl_from_IDependencyObject( iface );
    return element_Release( &impl->IInspectable_iface );
}
static HRESULT WINAPI wv2_do_GetValue( void *iface, void *dp, IInspectable **out )
{
    struct xaml_element *impl = impl_from_IDependencyObject( iface );
    int i;
    if (!out) return E_POINTER;
    *out = NULL;
    if (dp && dp == wv2_source_dp)
        return wv2_get_Source( &impl->IWebView2_iface, out );
    for (i = 0; i < impl->dp_count; i++)
    {
        if (impl->dp_store[i].dp == dp)
        {
            *out = impl->dp_store[i].val;
            if (*out) IInspectable_AddRef( *out );
            return S_OK;
        }
    }
    if (dp)
        xaml_dp_get_default_value( dp, out );
    return S_OK;
}
static HRESULT WINAPI wv2_do_SetValue( void *iface, void *dp, IInspectable *value )
{
    struct xaml_element *impl = impl_from_IDependencyObject( iface );
    int i;
    if (dp && dp == wv2_source_dp)
        return wv2_put_Source( &impl->IWebView2_iface, value );
    for (i = 0; i < impl->dp_count; i++)
    {
        if (impl->dp_store[i].dp == dp)
        {
            if (value) IInspectable_AddRef( value );
            if (impl->dp_store[i].val) IInspectable_Release( impl->dp_store[i].val );
            impl->dp_store[i].val = value;
            return S_OK;
        }
    }
    if (xaml_vec_grow( (void **)&impl->dp_store, &impl->dp_cap, impl->dp_count + 1, sizeof(*impl->dp_store), 16 ))
    {
        impl->dp_store[impl->dp_count].dp = dp;
        impl->dp_store[impl->dp_count].val = value;
        if (value) IInspectable_AddRef( value );
        impl->dp_count++;
    }
    return S_OK;
}

static HRESULT WINAPI wv2_do_RegisterPropertyChangedCallback( void *iface, void *dp, void *cb, INT64 *token )
{
    static LONG next_tok = 100;
    if (token) *token = InterlockedIncrement( &next_tok );
    return S_OK;
}

static HRESULT WINAPI wv2_do_UnregisterPropertyChangedCallback( void *iface, void *dp, INT64 token )
{
    return S_OK;
}

const void *wv2_do_vtbl[64] =
{
    [0] = wv2_do_QueryInterface,
    [1] = wv2_do_AddRef,
    [2] = wv2_do_Release,
    [3] = element_GetIids,
    [4] = element_GetRuntimeClassName,
    [5] = element_GetTrustLevel,
    [6] = wv2_do_GetValue,
    [7] = wv2_do_SetValue,
    [8] = element_generic_slot,
    [9] = wv2_do_GetValue,
    [10] = wv2_do_SetValue,
    [11] = wv2_do_RegisterPropertyChangedCallback,
    [12] = wv2_do_UnregisterPropertyChangedCallback,
    [13] = element_generic_slot,
    [14] = uwp_do_get_DispatcherQueue,
    [15 ... 63] = element_generic_slot,
};

const void *wv2_vtbl[256] =
{
    [0] = element_wv2_QueryInterface,         /* 0: QI */
    [1] = element_wv2_AddRef,                 /* 1: AddRef */
    [2] = element_wv2_Release,                /* 2: Release */
    [3] = element_GetIids,                    /* 3: GetIids */
    [4] = element_wv2_GetRuntimeClassName,    /* 4: GetRuntimeClassName */
    [5] = element_GetTrustLevel,              /* 5: GetTrustLevel */
    [8] = element_generic_slot,

    [11] = element_generic_slot,

    [14 ... 19] = element_generic_slot,

    [21 ... 26] = element_generic_slot,

    [29] = element_generic_slot,

    [32] = element_generic_slot,

    [35 ... 74] = element_generic_slot,

    [77 ... 255] = element_generic_slot,
    [6] = wv2_get_CoreWebView2,               /* 6: get_CoreWebView2 */
    [7] = wv2_EnsureCoreWebView2Async,        /* 7: EnsureCoreWebView2Async */
    [9] = wv2_get_Source,                     /* 9: get_Source */
    [10] = wv2_put_Source,                    /* 10: put_Source */
    [12] = wv2_get_Source,                    /* 12: get_Source (legacy alias) */
    [13] = wv2_put_Source,                    /* 13: put_Source (legacy alias) */
    [20] = wv2_get_CoreWebView2,              /* 20: get_CoreWebView2 (alias) */
    [27] = wv2_add_CoreWebView2Initialized,
    [28] = wv2_remove_CoreWebView2Initialized,
    [30] = wv2_add_CoreWebView2Initialized,   /* 30: add_CoreWebView2Initialized */
    [31] = wv2_remove_CoreWebView2Initialized,/* 31: remove_CoreWebView2Initialized */
    [33] = wv2_add_CoreWebView2Initialized,
    [34] = wv2_remove_CoreWebView2Initialized,
    [75] = element_add_Loaded,
    [76] = element_remove_Loaded,
};

const void *wv22_vtbl[256] =
{
    [0] = element_wv22_QueryInterface,        /* 0: QI */
    [1] = element_wv22_AddRef,                /* 1: AddRef */
    [2] = element_wv22_Release,               /* 2: Release */
    [3] = element_GetIids,                    /* 3: GetIids */
    [4] = element_wv22_GetRuntimeClassName,   /* 4: GetRuntimeClassName */
    [5] = element_GetTrustLevel,              /* 5: GetTrustLevel */
    [70 ... 255] = element_generic_slot,
    [6] = wv22_EnsureCoreWebView2Async,   [7] = wv22_EnsureCoreWebView2AsyncWithOptions,   [8] = wv2_probe2,   [9] = wv2_probe3,
    [10] = element_grid_get_RowDefinitions_abi3,  /* IV1 c3c01020 slot10 = RowDefinitions(this, HSTRING, vec**) */
    [11] = wv2_probe5,  [12] = wv2_probe6,  [13] = wv2_probe7,
    [14] = wv2_probe8,  [15] = wv2_probe9,  [16] = wv2_probe10, [17] = wv2_probe11,
    [18] = wv2_probe12, [19] = wv2_probe13, [20] = wv2_probe14, [21] = wv2_probe15,
    [22] = wv2_probe16, [23] = wv2_probe17, [24] = wv2_probe18, [25] = wv2_probe19,
    [26] = wv2_probe20, [27] = wv2_probe21, [28] = wv2_probe22, [29] = wv2_probe23,
    [30] = wv2_probe24, [31] = wv2_probe25, [32] = wv2_probe26, [33] = wv2_probe27,
    [34] = wv2_probe28, [35] = wv2_probe29, [36] = wv2_probe30, [37] = wv2_probe31,
    [38] = wv2_probe32, [39] = wv2_probe33, [40] = wv2_probe34, [41] = wv2_probe35,
    [42] = wv2_probe36, [43] = wv2_probe37, [44] = wv2_probe38, [45] = wv2_probe39,
    [46] = wv2_probe40, [47] = wv2_probe41, [48] = wv2_probe42, [49] = wv2_probe43,
    [50] = wv2_probe44, [51] = wv2_probe45, [52] = wv2_probe46, [53] = wv2_probe47,
    [54] = wv2_probe48, [55] = wv2_probe49, [56] = wv2_probe50, [57] = wv2_probe51,
    [58] = wv2_probe52, [59] = wv2_probe53, [60] = wv2_probe54, [61] = wv2_probe55,
    [62] = wv2_probe56, [63] = wv2_probe57, [64] = wv2_probe58, [65] = wv2_probe59,
    [66] = wv2_probe60, [67] = wv2_probe61, [68] = wv2_probe62, [69] = wv2_probe63,
};

/* WinUI3 IUIElement IV slots: set_Visibility is slot 10, get_Visibility 11.
 * On the shared element/grid vtbl, slot 10 collides with Grid IV2
 * get_RowDefinitions and WebView2 CreateCoreWebView2Controller — serve IUIElement
 * from its own view so the app's projection hits the right slots. */
