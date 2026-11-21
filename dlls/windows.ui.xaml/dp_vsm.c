/* WinRT Windows.UI.Xaml UWP Visual Tree, PRI/XBF Loader, Layout & Input Host
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <wctype.h>
#include <excpt.h>
#include "wine/exception.h"
#include "private.h"
#include "xbf_known_types.h"
#include "appmodel.h"
#include "weakreference.h"
#include "winreg.h"
#include "wincodec.h"
#define WIDL_using_Windows_ApplicationModel
#define WIDL_using_Windows_Management_Deployment
#include "windows.management.deployment.h"
#define WIDL_using_Windows_UI_Composition
#include "windows.ui.composition.h"
#include "uwp_private.h"
WINE_DEFAULT_DEBUG_CHANNEL(xaml);

IInspectable *box_double_value( double d )
{
    IActivationFactory *fac = NULL;
    IPropertyValueStatics *pvs = NULL;
    IInspectable *out = NULL;
    HSTRING hcls = NULL;

    if (SUCCEEDED(WindowsCreateString( L"Windows.Foundation.PropertyValue", 32, &hcls )))
    {
        if (SUCCEEDED(RoGetActivationFactory( hcls, &IID_IActivationFactory, (void **)&fac )) && fac)
        {
            if (SUCCEEDED(IActivationFactory_QueryInterface( fac, &IID_IPropertyValueStatics, (void **)&pvs )) && pvs)
            {
                IPropertyValueStatics_CreateDouble( pvs, d, &out );
                IPropertyValueStatics_Release( pvs );
            }
            IActivationFactory_Release( fac );
        }
        WindowsDeleteString( hcls );
    }
    return out;
}

struct uwp_boxed_thickness
{
    IInspectable IInspectable_iface;
    IInspectable IPropertyValue_iface;
    LONG ref;
    double val[4];
};

static const GUID IID_IReference_Thickness_UWP =
    { 0xa19f7ba8, 0xd8cd, 0x5df2, { 0xab, 0x44, 0xfe, 0xfd, 0x26, 0x64, 0x44, 0x84 } };

static HRESULT WINAPI uwp_bth_pv_get_Type( void *iface, INT32 *type )
{
    if (type) *type = 20; /* PropertyType_OtherType */
    return S_OK;
}

static HRESULT WINAPI uwp_bth_pv_get_IsNumericScalar( void *iface, boolean *val )
{
    if (val) *val = FALSE;
    return S_OK;
}

static HRESULT WINAPI uwp_bth_QueryInterface( IInspectable *iface, REFIID iid, void **out )
{
    struct uwp_boxed_thickness *impl = CONTAINING_RECORD( iface, struct uwp_boxed_thickness, IInspectable_iface );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IReference_Thickness_UWP ))
    {
        *out = &impl->IInspectable_iface;
        IInspectable_AddRef( &impl->IInspectable_iface );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IPropertyValue ))
    {
        *out = &impl->IPropertyValue_iface;
        IInspectable_AddRef( &impl->IInspectable_iface );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static HRESULT WINAPI uwp_bth_pv_QueryInterface( IInspectable *iface, REFIID iid, void **out )
{
    struct uwp_boxed_thickness *impl = CONTAINING_RECORD( iface, struct uwp_boxed_thickness, IPropertyValue_iface );
    return uwp_bth_QueryInterface( &impl->IInspectable_iface, iid, out );
}

static ULONG WINAPI uwp_bth_AddRef( IInspectable *iface )
{
    struct uwp_boxed_thickness *impl = CONTAINING_RECORD( iface, struct uwp_boxed_thickness, IInspectable_iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI uwp_bth_pv_AddRef( IInspectable *iface )
{
    struct uwp_boxed_thickness *impl = CONTAINING_RECORD( iface, struct uwp_boxed_thickness, IPropertyValue_iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI uwp_bth_Release( IInspectable *iface )
{
    struct uwp_boxed_thickness *impl = CONTAINING_RECORD( iface, struct uwp_boxed_thickness, IInspectable_iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref) free( impl );
    return ref;
}

static ULONG WINAPI uwp_bth_pv_Release( IInspectable *iface )
{
    struct uwp_boxed_thickness *impl = CONTAINING_RECORD( iface, struct uwp_boxed_thickness, IPropertyValue_iface );
    return uwp_bth_Release( &impl->IInspectable_iface );
}

static HRESULT WINAPI uwp_bth_GetIids( IInspectable *iface, ULONG *cnt, IID **iids )
{
    if (cnt) *cnt = 0;
    if (iids) *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI uwp_bth_GetRuntimeClassName( IInspectable *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.Foundation.IReference`1<Windows.UI.Xaml.Thickness>", 58, name );
}

static HRESULT WINAPI uwp_bth_GetTrustLevel( IInspectable *iface, TrustLevel *lvl )
{
    if (lvl) *lvl = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI uwp_bth_get_Value( void *iface, double *out )
{
    struct uwp_boxed_thickness *impl = CONTAINING_RECORD( iface, struct uwp_boxed_thickness, IInspectable_iface );
    if (!out) return E_POINTER;
    memcpy( out, impl->val, sizeof(impl->val) );
    return S_OK;
}

static const void *uwp_bth_vtbl[16] =
{
    uwp_bth_QueryInterface,
    uwp_bth_AddRef,
    uwp_bth_Release,
    uwp_bth_GetIids,
    uwp_bth_GetRuntimeClassName,
    uwp_bth_GetTrustLevel,
    uwp_bth_get_Value,
};

static const void *uwp_bth_pv_vtbl[16] =
{
    uwp_bth_pv_QueryInterface,
    uwp_bth_pv_AddRef,
    uwp_bth_pv_Release,
    uwp_bth_GetIids,
    uwp_bth_GetRuntimeClassName,
    uwp_bth_GetTrustLevel,
    uwp_bth_pv_get_Type,
    uwp_bth_pv_get_IsNumericScalar,
};

static IInspectable *box_thickness_value( const double vec4[4] )
{
    struct uwp_boxed_thickness *impl = calloc( 1, sizeof(*impl) );
    if (!impl) return NULL;
    impl->IInspectable_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_bth_vtbl;
    impl->IPropertyValue_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_bth_pv_vtbl;
    impl->ref = 1;
    if (vec4) memcpy( impl->val, vec4, sizeof(impl->val) );
    return &impl->IInspectable_iface;
}

static const GUID IID_IReference_Color_UWP =
    { 0xab8e5d11, 0xb0c1, 0x5a21, { 0x95, 0xae, 0xf1, 0x6b, 0xf3, 0xa3, 0x76, 0x24 } };

struct uwp_boxed_color
{
    IInspectable IInspectable_iface;
    IInspectable IPropertyValue_iface;
    LONG ref;
    BYTE argb[4]; /* A, R, G, B (Windows.UI.Color layout) */
};

static HRESULT WINAPI uwp_bcol_pv_get_Type( void *iface, INT32 *type )
{
    if (type) *type = 20; /* PropertyType_OtherType */
    return S_OK;
}

static HRESULT WINAPI uwp_bcol_pv_get_IsNumericScalar( void *iface, boolean *val )
{
    if (val) *val = FALSE;
    return S_OK;
}

static HRESULT WINAPI uwp_bcol_QueryInterface( IInspectable *iface, REFIID iid, void **out )
{
    struct uwp_boxed_color *impl = CONTAINING_RECORD( iface, struct uwp_boxed_color, IInspectable_iface );
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IReference_Color_UWP ))
    {
        *out = &impl->IInspectable_iface;
        IInspectable_AddRef( &impl->IInspectable_iface );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IPropertyValue ))
    {
        *out = &impl->IPropertyValue_iface;
        IInspectable_AddRef( &impl->IInspectable_iface );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static HRESULT WINAPI uwp_bcol_pv_QueryInterface( IInspectable *iface, REFIID iid, void **out )
{
    struct uwp_boxed_color *impl = CONTAINING_RECORD( iface, struct uwp_boxed_color, IPropertyValue_iface );
    return uwp_bcol_QueryInterface( &impl->IInspectable_iface, iid, out );
}

static ULONG WINAPI uwp_bcol_AddRef( IInspectable *iface )
{
    struct uwp_boxed_color *impl = CONTAINING_RECORD( iface, struct uwp_boxed_color, IInspectable_iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI uwp_bcol_pv_AddRef( IInspectable *iface )
{
    struct uwp_boxed_color *impl = CONTAINING_RECORD( iface, struct uwp_boxed_color, IPropertyValue_iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI uwp_bcol_Release( IInspectable *iface )
{
    struct uwp_boxed_color *impl = CONTAINING_RECORD( iface, struct uwp_boxed_color, IInspectable_iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref) free( impl );
    return ref;
}

static ULONG WINAPI uwp_bcol_pv_Release( IInspectable *iface )
{
    struct uwp_boxed_color *impl = CONTAINING_RECORD( iface, struct uwp_boxed_color, IPropertyValue_iface );
    return uwp_bcol_Release( &impl->IInspectable_iface );
}

static HRESULT WINAPI uwp_bcol_GetRuntimeClassName( IInspectable *iface, HSTRING *name )
{
    static const WCHAR cls[] = L"Windows.Foundation.IReference`1<Windows.UI.Color>";
    return WindowsCreateString( cls, ARRAY_SIZE(cls) - 1, name );
}

static HRESULT WINAPI uwp_bcol_get_Value( void *iface, BYTE *out )
{
    struct uwp_boxed_color *impl = CONTAINING_RECORD( iface, struct uwp_boxed_color, IInspectable_iface );
    if (!out) return E_POINTER;
    memcpy( out, impl->argb, 4 );
    return S_OK;
}

static const void *uwp_bcol_vtbl[16] =
{
    uwp_bcol_QueryInterface,
    uwp_bcol_AddRef,
    uwp_bcol_Release,
    uwp_bth_GetIids,
    uwp_bcol_GetRuntimeClassName,
    uwp_bth_GetTrustLevel,
    uwp_bcol_get_Value,
};

static const void *uwp_bcol_pv_vtbl[16] =
{
    uwp_bcol_pv_QueryInterface,
    uwp_bcol_pv_AddRef,
    uwp_bcol_pv_Release,
    uwp_bth_GetIids,
    uwp_bcol_GetRuntimeClassName,
    uwp_bth_GetTrustLevel,
    uwp_bcol_pv_get_Type,
    uwp_bcol_pv_get_IsNumericScalar,
};

static IInspectable *box_color_value( UINT32 argb )
{
    struct uwp_boxed_color *impl = calloc( 1, sizeof(*impl) );
    if (!impl) return NULL;
    impl->IInspectable_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_bcol_vtbl;
    impl->IPropertyValue_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_bcol_pv_vtbl;
    impl->ref = 1;
    impl->argb[0] = (BYTE)((argb >> 24) & 0xff); /* A */
    impl->argb[1] = (BYTE)((argb >> 16) & 0xff); /* R */
    impl->argb[2] = (BYTE)((argb >>  8) & 0xff); /* G */
    impl->argb[3] = (BYTE)(argb & 0xff);         /* B */
    return &impl->IInspectable_iface;
}

IInspectable *box_string_value( const WCHAR *str )
{
    IActivationFactory *fac = NULL;
    IPropertyValueStatics *pvs = NULL;
    IInspectable *out = NULL;
    HSTRING hcls = NULL, hval = NULL;

    if (!str) str = L"";
    if (SUCCEEDED(WindowsCreateString( L"Windows.Foundation.PropertyValue", 32, &hcls )))
    {
        if (SUCCEEDED(RoGetActivationFactory( hcls, &IID_IActivationFactory, (void **)&fac )) && fac)
        {
            if (SUCCEEDED(IActivationFactory_QueryInterface( fac, &IID_IPropertyValueStatics, (void **)&pvs )) && pvs)
            {
                WindowsCreateString( str, wcslen(str), &hval );
                IPropertyValueStatics_CreateString( pvs, hval, &out );
                if (hval) WindowsDeleteString( hval );
                IPropertyValueStatics_Release( pvs );
            }
            IActivationFactory_Release( fac );
        }
        WindowsDeleteString( hcls );
    }
    return out;
}

IInspectable *box_int32_value( INT32 v )
{
    IActivationFactory *fac = NULL;
    IPropertyValueStatics *pvs = NULL;
    IInspectable *out = NULL;
    HSTRING hcls = NULL;

    if (SUCCEEDED(WindowsCreateString( L"Windows.Foundation.PropertyValue", 32, &hcls )))
    {
        if (SUCCEEDED(RoGetActivationFactory( hcls, &IID_IActivationFactory, (void **)&fac )) && fac)
        {
            if (SUCCEEDED(IActivationFactory_QueryInterface( fac, &IID_IPropertyValueStatics, (void **)&pvs )) && pvs)
            {
                IPropertyValueStatics_CreateInt32( pvs, v, &out );
                IPropertyValueStatics_Release( pvs );
            }
            IActivationFactory_Release( fac );
        }
        WindowsDeleteString( hcls );
    }
    return out;
}

IInspectable *box_bool_value( boolean b )
{
    IActivationFactory *fac = NULL;
    IPropertyValueStatics *pvs = NULL;
    IInspectable *out = NULL;
    HSTRING hcls = NULL;

    if (SUCCEEDED(WindowsCreateString( L"Windows.Foundation.PropertyValue", 32, &hcls )))
    {
        if (SUCCEEDED(RoGetActivationFactory( hcls, &IID_IActivationFactory, (void **)&fac )) && fac)
        {
            if (SUCCEEDED(IActivationFactory_QueryInterface( fac, &IID_IPropertyValueStatics, (void **)&pvs )) && pvs)
            {
                IPropertyValueStatics_CreateBoolean( pvs, b, &out );
                IPropertyValueStatics_Release( pvs );
            }
            IActivationFactory_Release( fac );
        }
        WindowsDeleteString( hcls );
    }
    return out;
}

BOOL uwp_unbox_string( void *obj, WCHAR *buf, int buf_chars )
{
    IPropertyValue *pv = NULL;
    HSTRING hs = NULL;
    if (!obj || !buf || buf_chars <= 0) return FALSE;
    buf[0] = 0;

    if (SUCCEEDED(IUnknown_QueryInterface( (IUnknown *)obj, &IID_IPropertyValue, (void **)&pv )) && pv)
    {
        PropertyType pt = PropertyType_Empty;
        if (SUCCEEDED(IPropertyValue_get_Type( pv, &pt )) && pt == PropertyType_String &&
            SUCCEEDED(IPropertyValue_GetString( pv, &hs )) && hs)
        {
            const WCHAR *raw = WindowsGetStringRawBuffer( hs, NULL );
            if (raw) lstrcpynW( buf, raw, buf_chars );
            WindowsDeleteString( hs );
        }
        IPropertyValue_Release( pv );
        if (buf[0]) return TRUE;
    }
    return FALSE;
}

BOOL uwp_unbox_int32( void *obj, INT32 *out_val )
{
    IPropertyValue *pv = NULL;
    if (!obj || !out_val) return FALSE;
    if (SUCCEEDED(IUnknown_QueryInterface( (IUnknown *)obj, &IID_IPropertyValue, (void **)&pv )) && pv)
    {
        HRESULT hr = IPropertyValue_GetInt32( pv, out_val );
        IPropertyValue_Release( pv );
        return SUCCEEDED(hr);
    }
    return FALSE;
}

/* ============================================================================
 * 3. ResourceDictionary Key-Value Store (Scoped Chain & XBF Resource Parsing)
 * ============================================================================ */

struct uwp_element;
struct uwp_rd_bucket;

struct uwp_element *uwp_elem_from_any( void *ptr );
static void uwp_set_brush_element_argb( IInspectable *br, UINT32 argb );
BOOL uwp_extract_resource_key( const WCHAR *in, WCHAR *out, size_t out_chars )
{
    const WCHAR *p = in;
    size_t len;
    BOOL is_markup = FALSE;
    if (!in || !out || !out_chars) return FALSE;
    out[0] = 0;
    while (*p == L' ' || *p == L'\t') p++;
    if (*p == L'{')
    {
        p++;
        while (*p == L' ' || *p == L'\t') p++;
        if (!wcsnicmp( p, L"StaticResource", 14 ) && (p[14] == L' ' || p[14] == L'\t'))
        {
            p += 14;
            is_markup = TRUE;
        }
        else if (!wcsnicmp( p, L"ThemeResource", 13 ) && (p[13] == L' ' || p[13] == L'\t'))
        {
            p += 13;
            is_markup = TRUE;
        }
        while (*p == L' ' || *p == L'\t') p++;
        if (!wcsnicmp( p, L"ResourceKey=", 12 )) p += 12;
        lstrcpynW( out, p, out_chars );
        len = wcslen( out );
        while (len > 0 && (out[len - 1] == L'}' || out[len - 1] == L' ' || out[len - 1] == L'\t'))
            out[--len] = 0;
        return is_markup;
    }
    lstrcpynW( out, p, out_chars );
    len = wcslen( out );
    while (len > 0 && (out[len - 1] == L' ' || out[len - 1] == L'\t'))
        out[--len] = 0;
    return FALSE;
}

struct uwp_rd_bucket *get_rd_bucket( void *rd_key, BOOL create )
{
    struct uwp_rd_bucket *b;
    for (b = g_rd_buckets_head; b; b = b->next)
        if (b->rd_key == rd_key) return b;
    if (!create || !rd_key) return NULL;
    b = calloc( 1, sizeof(*b) );
    if (!b) return NULL;
    b->rd_key = rd_key;
    b->next = g_rd_buckets_head;
    g_rd_buckets_head = b;
    return b;
}

struct uwp_rd_entry *rd_bucket_upsert( struct uwp_rd_bucket *b, const WCHAR *key )
{
    int i;
    if (!b || !key || !key[0]) return NULL;
    for (i = 0; i < b->count; i++)
    {
        if (!wcscmp( b->entries[i].key, key ))
            return &b->entries[i];
    }
    if (b->count >= b->cap)
    {
        int ncap = b->cap ? b->cap * 2 : 32;
        struct uwp_rd_entry *ne = realloc( b->entries, ncap * sizeof(*ne) );
        if (!ne) return NULL;
        b->entries = ne;
        b->cap = ncap;
    }
    memset( &b->entries[b->count], 0, sizeof(b->entries[0]) );
    lstrcpynW( b->entries[b->count].key, key, 96 );
    b->entries[b->count].opacity = 1.0;
    return &b->entries[b->count++];
}

void uwp_rd_link_merged( struct uwp_rd_bucket *parent, struct uwp_rd_bucket *child )
{
    int i;
    if (!parent || !child || parent == child) return;
    for (i = 0; i < parent->merged_cnt; i++)
        if (parent->merged[i] == child) return;
    if (parent->merged_cnt >= parent->merged_cap)
    {
        int ncap = parent->merged_cap ? parent->merged_cap * 2 : 8;
        struct uwp_rd_bucket **nm = realloc( parent->merged, ncap * sizeof(*nm) );
        if (!nm) return;
        parent->merged = nm;
        parent->merged_cap = ncap;
    }
    parent->merged[parent->merged_cnt++] = child;
}

void uwp_rd_link_theme( struct uwp_rd_bucket *parent, const WCHAR *theme_name, struct uwp_rd_bucket *child )
{
    if (!parent || !theme_name || !child || parent == child) return;
    if (!wcsicmp( theme_name, L"Dark" )) parent->theme_dark = child;
    else if (!wcsicmp( theme_name, L"Default" )) parent->theme_default = child;
    else if (!wcsicmp( theme_name, L"Light" )) parent->theme_light = child;
    else if (!wcsicmp( theme_name, L"HighContrast" )) parent->theme_hc = child;
}

void uwp_rd_insert( void *rd_key, const WCHAR *key, IInspectable *val )
{
    struct uwp_rd_bucket *b;
    struct uwp_rd_entry *e;
    if (!rd_key || !key || !key[0] || !val) return;
    b = get_rd_bucket( rd_key, TRUE );
    e = rd_bucket_upsert( b, key );
    if (!e) return;
    if (e->value) IInspectable_Release( e->value );
    e->value = val;
    IInspectable_AddRef( val );
    e->kind = UWP_RD_OBJ;
}

static BOOL uwp_mark_res_xbf_loaded( const char *resolved_path )
{
    int i;
    if (!resolved_path || !resolved_path[0]) return FALSE;
    for (i = 0; i < g_loaded_res_xbf_cnt; i++)
    {
        if (!strcasecmp( g_loaded_res_xbfs[i], resolved_path ))
            return FALSE;
    }
    if (g_loaded_res_xbf_cnt < (int)ARRAY_SIZE(g_loaded_res_xbfs))
    {
        snprintf( g_loaded_res_xbfs[g_loaded_res_xbf_cnt++], 256, "%s", resolved_path );
    }
    return TRUE;
}

void uwp_load_res_xbf_into_bucket( const char *base_dir, const WCHAR *uri_or_path, struct uwp_rd_bucket *target )
{
    const BYTE *xbf = NULL;
    DWORD xbf_sz = 0;
    char resolved[256] = {0};

    if (!uri_or_path || !uri_or_path[0] || !target) return;
    if (!uwp_pri_find_xbf_rel( base_dir, uri_or_path, &xbf, &xbf_sz, resolved, sizeof(resolved) ))
        return;
    if (!uwp_mark_res_xbf_loaded( resolved[0] ? resolved : "xbf" ))
        return;
    TRACE( "xbf-res: loading %s (%lu bytes) into rd %p\n", resolved, (unsigned long)xbf_sz, target );
    uwp_parse_xbf_into_rd( xbf, xbf_sz, resolved, target );
}

/* Registry-driven per-app Resources_xbf + App.xbf + WinUI theme defaults (no hardcoded app families) */
void uwp_ensure_app_resources_loaded( void )
{
    struct uwp_rd_bucket *app_b, *theme_b;
    WCHAR pkg_full[256] = {0};
    UINT32 len = ARRAY_SIZE(pkg_full);
    HMODULE reg_mod;

    if (g_app_res_loaded || g_app_res_loading) return;
    g_app_res_loading = TRUE;

    app_b = get_rd_bucket( &g_app_rd_sentinel, TRUE );
    theme_b = get_rd_bucket( &g_theme_rd_sentinel, TRUE );

    /* 1. Check Software\Classes\AppModel\Repository\Packages\<FullName>\Resources_xbf */
    if (GetCurrentPackageFullName( &len, pkg_full ) == ERROR_SUCCESS && pkg_full[0])
    {
        reg_mod = GetModuleHandleW( L"kernelbase.dll" );
        if (!reg_mod) reg_mod = LoadLibraryW( L"advapi32.dll" );
        if (reg_mod)
        {
            typedef LONG (WINAPI *p_RegOpenKeyExW_t)( HKEY, LPCWSTR, DWORD, REGSAM, PHKEY );
            typedef LONG (WINAPI *p_RegQueryValueExW_t)( HKEY, LPCWSTR, LPDWORD, LPDWORD, LPBYTE, LPDWORD );
            typedef LONG (WINAPI *p_RegDeleteValueW_t)( HKEY, LPCWSTR );
            typedef LONG (WINAPI *p_RegCloseKey_t)( HKEY );
            p_RegOpenKeyExW_t p_open = (p_RegOpenKeyExW_t)GetProcAddress( reg_mod, "RegOpenKeyExW" );
            p_RegQueryValueExW_t p_query = (p_RegQueryValueExW_t)GetProcAddress( reg_mod, "RegQueryValueExW" );
            p_RegDeleteValueW_t p_del = (p_RegDeleteValueW_t)GetProcAddress( reg_mod, "RegDeleteValueW" );
            p_RegCloseKey_t p_close = (p_RegCloseKey_t)GetProcAddress( reg_mod, "RegCloseKey" );
            if (p_open && p_del && p_close)
            {
                WCHAR fam[128] = {0};
                UINT32 flen = ARRAY_SIZE(fam);
                if (GetCurrentPackageFamilyName( &flen, fam ) == ERROR_SUCCESS && fam[0])
                {
                    WCHAR ssub[256];
                    HKEY shk = NULL;
                    swprintf( ssub, ARRAY_SIZE(ssub), L"Software\\%s\\Settings", fam );
                    if (p_open( HKEY_CURRENT_USER, ssub, 0, KEY_SET_VALUE, &shk ) == ERROR_SUCCESS)
                    {
                        p_del( shk, L"VeryFirstLaunch" );
                        p_close( shk );
                    }
                }
            }
            if (p_open && p_query && p_close)
            {
                static const HKEY roots[] = { HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE };
                WCHAR sub[512];
                size_t r;
                swprintf( sub, ARRAY_SIZE(sub), L"Software\\Classes\\AppModel\\Repository\\Packages\\%s", pkg_full );
                for (r = 0; r < ARRAY_SIZE(roots); r++)
                {
                    HKEY hk = NULL;
                    WCHAR valbuf[1024] = {0};
                    DWORD type = 0, cb = sizeof(valbuf) - sizeof(WCHAR);
                    if (p_open( roots[r], sub, 0, KEY_READ, &hk ) != ERROR_SUCCESS) continue;
                    if (p_query( hk, L"Resources_xbf", NULL, &type, (BYTE *)valbuf, &cb ) == ERROR_SUCCESS)
                    {
                        if (type == REG_MULTI_SZ)
                        {
                            const WCHAR *it = valbuf;
                            while (*it)
                            {
                                TRACE( "xbf-res: registry Resources_xbf[%s] -> %s\n", debugstr_w(pkg_full), debugstr_w(it) );
                                uwp_load_res_xbf_into_bucket( NULL, it, app_b );
                                it += wcslen( it ) + 1;
                            }
                        }
                        else if ((type == REG_SZ || type == REG_EXPAND_SZ) && valbuf[0])
                        {
                            WCHAR *tok = valbuf;
                            while (*tok)
                            {
                                WCHAR *sep = wcspbrk( tok, L";," );
                                if (sep) *sep = 0;
                                while (*tok == L' ') tok++;
                                if (*tok)
                                {
                                    TRACE( "xbf-res: registry Resources_xbf[%s] -> %s\n", debugstr_w(pkg_full), debugstr_w(tok) );
                                    uwp_load_res_xbf_into_bucket( NULL, tok, app_b );
                                }
                                if (!sep) break;
                                tok = sep + 1;
                            }
                        }
                    }
                    p_close( hk );
                }
            }
        }
    }

    /* 2. Parse App.xbf (and any MergedDictionaries / ThemeDictionaries it references) */
    if (app_b)
        uwp_load_res_xbf_into_bucket( NULL, L"App.xbf", app_b );

    /* 3. Load WinUI / system theme XBF into theme defaults bucket */
    if (theme_b)
    {
        uwp_load_res_xbf_into_bucket( NULL, L"Microsoft.UI.Xaml/Themes/21h1_themeresources.xbf", theme_b );
        uwp_load_res_xbf_into_bucket( NULL, L"Microsoft.UI.Xaml/Themes/21h1_generic.xbf", theme_b );
        if (theme_b->count == 0 && !theme_b->theme_default && !theme_b->theme_dark)
            uwp_load_res_xbf_into_bucket( NULL, L"Microsoft.UI.Xaml/Themes/rs5_themeresources.xbf", theme_b );
    }

    g_app_res_loaded = TRUE;
    g_app_res_loading = FALSE;
}

static BOOL uwp_lookup_in_bucket( struct uwp_rd_bucket *b, const WCHAR *key, int depth,
                                  struct uwp_rd_entry **out_entry, struct uwp_rd_bucket **out_bucket )
{
    int i;
    if (!b || !key || !key[0] || depth > 12) return FALSE;
    TRACE( "uwp_lookup_in_bucket: b=%p count=%d merged_cnt=%d key=%s\n",
           b, b->count, b->merged_cnt, debugstr_w(key) );

    for (i = 0; i < b->count; i++)
    {
        if (!wcscmp( b->entries[i].key, key ))
        {
            *out_entry = &b->entries[i];
            if (out_bucket) *out_bucket = b;
            return TRUE;
        }
    }
    if (b->theme_dark && uwp_lookup_in_bucket( b->theme_dark, key, depth + 1, out_entry, out_bucket ))
        return TRUE;
    if (b->theme_default && uwp_lookup_in_bucket( b->theme_default, key, depth + 1, out_entry, out_bucket ))
        return TRUE;
    for (i = b->merged_cnt - 1; i >= 0; i--)
    {
        if (uwp_lookup_in_bucket( b->merged[i], key, depth + 1, out_entry, out_bucket ))
            return TRUE;
    }
    if (b->theme_light && uwp_lookup_in_bucket( b->theme_light, key, depth + 1, out_entry, out_bucket ))
        return TRUE;
    return FALSE;
}

static UINT32 uwp_get_system_accent_argb( void )
{
    static UINT32 cached_accent = 0;
    static BOOL accent_init = FALSE;
    if (!accent_init)
    {
        HMODULE reg_mod = GetModuleHandleW( L"kernelbase.dll" );
        accent_init = TRUE;
        cached_accent = 0xff0078d4u; /* Windows Fluent default blue */
        if (!reg_mod) reg_mod = LoadLibraryW( L"advapi32.dll" );
        if (reg_mod)
        {
            typedef LONG (WINAPI *p_RegOpenKeyExW_t)( HKEY, LPCWSTR, DWORD, REGSAM, PHKEY );
            typedef LONG (WINAPI *p_RegQueryValueExW_t)( HKEY, LPCWSTR, LPDWORD, LPDWORD, LPBYTE, LPDWORD );
            typedef LONG (WINAPI *p_RegCloseKey_t)( HKEY );
            p_RegOpenKeyExW_t p_open = (p_RegOpenKeyExW_t)GetProcAddress( reg_mod, "RegOpenKeyExW" );
            p_RegQueryValueExW_t p_query = (p_RegQueryValueExW_t)GetProcAddress( reg_mod, "RegQueryValueExW" );
            p_RegCloseKey_t p_close = (p_RegCloseKey_t)GetProcAddress( reg_mod, "RegCloseKey" );
            if (p_open && p_query && p_close)
            {
                HKEY hk = NULL;
                DWORD abgr = 0, type = 0, cb = sizeof(abgr);
                if (p_open( HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\DWM", 0, KEY_READ, &hk ) == ERROR_SUCCESS)
                {
                    if (p_query( hk, L"AccentColor", NULL, &type, (BYTE *)&abgr, &cb ) == ERROR_SUCCESS &&
                        type == REG_DWORD && (abgr & 0x00ffffffu))
                    {
                        BYTE r = (BYTE)(abgr & 0xff);
                        BYTE g = (BYTE)((abgr >> 8) & 0xff);
                        BYTE b = (BYTE)((abgr >> 16) & 0xff);
                        cached_accent = 0xff000000u | ((UINT32)r << 16) | ((UINT32)g << 8) | b;
                    }
                    p_close( hk );
                }
            }
        }
    }
    return cached_accent;
}

/* Documented system/Fluent dark theme fallback ONLY with TRACE when not found in element/page/app/WinUI dicts */
static BOOL uwp_theme_default_fallback( const WCHAR *key, UINT32 *out_argb )
{
    static const struct { const WCHAR *name; UINT32 argb; } defaults[] =
    {
        { L"ApplicationPageBackgroundThemeBrush",       0xff1f1f1fu },
        { L"SystemControlPageBackgroundChromeLowBrush", 0xff1f1f1fu },
        { L"SolidBackgroundFillColorBaseBrush",         0xff202020u },
        { L"SystemAltHighColor",                        0xff000000u },
        { L"SystemAltMediumHighColor",                  0xcc000000u },
        { L"SystemAltMediumColor",                      0x99000000u },
        { L"SystemAltMediumLowColor",                   0x66000000u },
        { L"SystemAltLowColor",                         0x33000000u },
        { L"SystemBaseHighColor",                       0xffffffffu },
        { L"SystemBaseMediumHighColor",                 0xccffffffu },
        { L"SystemBaseMediumColor",                     0x99ffffffu },
        { L"SystemBaseMediumLowColor",                  0x66ffffffu },
        { L"SystemBaseLowColor",                        0x33ffffffu },
        { L"SystemChromeWhiteColor",                    0xffffffffu },
        { L"SystemChromeBlackLowColor",                 0xff171717u },
        { L"SystemChromeMediumLowColor",                0xff2b2b2bu },
        { L"SystemListLowColor",                        0x19ffffffu },
        { L"SystemColorWindowColor",                    0xff202020u },
        { L"SystemColorWindowTextColor",                0xffffffffu },
        { L"SystemColorButtonFaceColor",                0xff323232u },
        { L"DefaultTextForegroundThemeBrush",           0xffffffffu },
        { L"TextFillColorPrimaryBrush",                 0xffffffffu },
        { L"TextFillColorSecondaryBrush",               0xffc5c5c5u },
        { L"TextFillColorDisabledBrush",                0x5dffffffu },
        { L"TextOnAccentFillColorPrimaryBrush",         0xffffffffu },
        { L"ControlFillColorDefaultBrush",              0x18ffffffu },
        { L"ControlFillColorSecondaryBrush",            0x22ffffffu },
        { L"ControlStrokeColorDefaultBrush",            0x18ffffffu },
        { L"CardBackgroundFillColorDefaultBrush",       0x0dffffffu },
        { L"SystemAccentColorLight1",                   0xff2b88d8u },
        { L"SystemAccentColorLight2",                   0xff4cc2ffu },
        { L"SystemAccentColorLight3",                   0xff99ebffu },
        { L"SystemAccentColorDark1",                    0xff005a9eu },
        { L"SystemAccentColorDark2",                    0xff004578u },
        { L"SystemAccentColorDark3",                    0xff002642u },
        { L"AccentTextThemeResource",                   0xff4cc2ffu },
        { L"SystemControlForegroundAccentBrush",        0xff4cc2ffu },
        { L"AccentTextFillColorPrimaryBrush",           0xff4cc2ffu },
    };
    size_t i;
    if (!key || !key[0] || !out_argb) return FALSE;
    if (!wcscmp( key, L"SystemAccentColor" ) ||
        !wcscmp( key, L"AccentFillColorDefaultBrush" ) ||
        !wcscmp( key, L"SystemControlHighlightAccentBrush" ) ||
        !wcscmp( key, L"AccentBrush" ))
    {
        *out_argb = uwp_get_system_accent_argb();
        TRACE( "xbf-res: theme default fallback key=%s -> #%08x\n", debugstr_w(key), *out_argb );
        return TRUE;
    }
    for (i = 0; i < ARRAY_SIZE(defaults); i++)
    {
        if (!wcscmp( key, defaults[i].name ))
        {
            *out_argb = defaults[i].argb;
            TRACE( "xbf-res: theme default fallback key=%s -> #%08x\n", debugstr_w(key), *out_argb );
            return TRUE;
        }
    }
    return FALSE;
}

BOOL uwp_rd_find_entry( struct uwp_rd_bucket *start_bucket, struct uwp_element *scope_el,
                               const WCHAR *raw_key, int depth,
                               struct uwp_rd_entry **out_entry, struct uwp_rd_bucket **out_bucket );

BOOL uwp_resolve_brush_or_color( struct uwp_rd_bucket *start_bucket, struct uwp_element *scope_el,
                                        const WCHAR *key_or_expr, int depth, UINT32 *out_argb )
{
    WCHAR key[128] = {0};
    struct uwp_rd_entry *e = NULL;
    struct uwp_rd_bucket *found_b = NULL;

    if (!key_or_expr || !key_or_expr[0] || !out_argb || depth > 12) return FALSE;
    if (uwp_parse_color_string( key_or_expr, out_argb )) return TRUE;

    uwp_extract_resource_key( key_or_expr, key, ARRAY_SIZE(key) );
    if (!key[0]) return FALSE;
    if (uwp_parse_color_string( key, out_argb )) return TRUE;

    uwp_ensure_app_resources_loaded();
    if (uwp_rd_find_entry( start_bucket, scope_el, key, depth, &e, &found_b ) && e)
    {
        if (e->kind == UWP_RD_COLOR)
        {
            *out_argb = e->argb;
            return TRUE;
        }
        if (e->kind == UWP_RD_BRUSH)
        {
            if (e->has_argb)
            {
                *out_argb = apply_opacity_to_argb( e->argb, e->opacity );
                return TRUE;
            }
            if (e->str_val[0])
            {
                UINT32 inner = 0;
                if (uwp_resolve_brush_or_color( found_b ? found_b : start_bucket, scope_el, e->str_val, depth + 1, &inner ))
                {
                    *out_argb = apply_opacity_to_argb( inner, e->opacity );
                    return TRUE;
                }
            }
        }
        if (e->kind == UWP_RD_STRING && e->str_val[0])
            return uwp_resolve_brush_or_color( found_b ? found_b : start_bucket, scope_el, e->str_val, depth + 1, out_argb );
    }

    return uwp_theme_default_fallback( key, out_argb );
}

IInspectable *uwp_rd_materialize_entry( struct uwp_rd_bucket *b, struct uwp_rd_entry *e )
{
    if (!e) return NULL;
    if (e->value) return e->value;
    if (e->kind == UWP_RD_DOUBLE)
        e->value = box_double_value( e->dval );
    else if (e->kind == UWP_RD_THICKNESS)
        e->value = box_thickness_value( e->vec4 );
    else if (e->kind == UWP_RD_STRING)
        e->value = box_string_value( e->str_val );
    else if (e->kind == UWP_RD_COLOR)
        e->value = box_color_value( e->argb );
    else if (e->kind == UWP_RD_BRUSH)
    {
        IInspectable *br = NULL;
        if (SUCCEEDED(element_create_typed( L"Windows.UI.Xaml.Media.SolidColorBrush", (void **)&br )) && br)
        {
            UINT32 argb = 0;
            if (uwp_resolve_brush_or_color( b, b ? b->owner_el : NULL, e->key, 0, &argb ))
            {
                e->argb = argb;
                e->has_argb = TRUE;
                uwp_set_brush_element_argb( br, argb );
            }
            e->value = br;
        }
    }
    else if (e->kind == UWP_RD_STYLE)
    {
        void *st = NULL;
        if (SUCCEEDED(uwp_element_create( L"Windows.UI.Xaml.Style", NULL, NULL, &st )) && st)
        {
            struct uwp_element *st_el = uwp_elem_from_any( st );
            if (st_el)
            {
                lstrcpynW( st_el->style_based_on, e->str_val, ARRAY_SIZE(st_el->style_based_on) );
                if (e->setter_cnt > 0 && e->setters)
                {
                    st_el->style_setters = malloc( (size_t)e->setter_cnt * sizeof(*e->setters) );
                    if (st_el->style_setters)
                    {
                        memcpy( st_el->style_setters, e->setters, (size_t)e->setter_cnt * sizeof(*e->setters) );
                        st_el->style_setter_cnt = e->setter_cnt;
                        st_el->style_setter_cap = e->setter_cnt;
                    }
                }
            }
            e->value = (IInspectable *)st;
        }
    }
    else if (e->kind == UWP_RD_DATA_TEMPLATE)
    {
        IInspectable *dt = NULL;
        if (SUCCEEDED(element_create_typed( L"Windows.UI.Xaml.DataTemplate", (void **)&dt )) && dt)
        {
            struct uwp_element *dt_el = uwp_elem_from_any( dt );
            if (dt_el && e->template_xbf_data && e->template_xbf_size)
            {
                dt_el->template_xbf_data = e->template_xbf_data;
                dt_el->template_xbf_size = e->template_xbf_size;
                dt_el->template_sub_idx = e->template_sub_idx;
                uwp_set_template_owner( &dt_el->template_owner_comp, e->template_owner_comp );
            }
            e->value = dt;
        }
    }
    return e->value;
}

HRESULT uwp_rd_lookup( void *rd_key, void *key_obj, void **out_val )
{
    WCHAR raw_str[128] = {0}, key_str[128] = {0};
    struct uwp_rd_bucket *b, *found_b = NULL;
    struct uwp_rd_entry *e = NULL;

    if (!out_val) return E_POINTER;
    *out_val = NULL;
    if (!key_obj) return 0x8000000b; /* E_BOUNDS */

    if (!uwp_unbox_string( key_obj, raw_str, ARRAY_SIZE(raw_str) ))
    {
        const WCHAR *raw = WindowsGetStringRawBuffer( (HSTRING)key_obj, NULL );
        if (raw) lstrcpynW( raw_str, raw, ARRAY_SIZE(raw_str) );
    }
    uwp_extract_resource_key( raw_str, key_str, ARRAY_SIZE(key_str) );

    TRACE( "uwp_rd_lookup rd=%p key=%s\n", rd_key, debugstr_w(key_str) );
    if (!key_str[0]) return 0x8000000b;

    uwp_ensure_app_resources_loaded();
    b = get_rd_bucket( rd_key, FALSE );
    TRACE( "uwp_rd_lookup: b=%p owner=%p\n", b, b ? b->owner_el : NULL );
    if (uwp_rd_find_entry( b, b ? b->owner_el : NULL, key_str, 0, &e, &found_b ) && e)
    {
        IInspectable *val = uwp_rd_materialize_entry( found_b ? found_b : b, e );
        TRACE( "uwp_rd_lookup: e=%p found_b=%p val=%p\n", e, found_b, val );
        if (val)
        {
            *out_val = val;
            IInspectable_AddRef( val );
            return S_OK;
        }
    }

    /* Documented fallback for system theme color/brush keys queried via Application.Resources */
    {
        UINT32 argb = 0;
        if (uwp_theme_default_fallback( key_str, &argb ))
        {
            IInspectable *br = NULL;
            size_t klen = wcslen( key_str );
            BOOL is_color_key = (klen >= 5 && !wcscmp( key_str + klen - 5, L"Color" )) ||
                                (klen >= 6 && !wcsncmp( key_str + klen - 6, L"Color", 5 ) &&
                                 (key_str[klen - 1] >= '0' && key_str[klen - 1] <= '9'));
            if (is_color_key)
            {
                IInspectable *col = box_color_value( argb );
                if (col)
                {
                    struct uwp_rd_bucket *target = b ? b : get_rd_bucket( rd_key, TRUE );
                    struct uwp_rd_entry *ne = rd_bucket_upsert( target, key_str );
                    if (ne)
                    {
                        ne->kind = UWP_RD_COLOR;
                        ne->argb = argb;
                        ne->has_argb = TRUE;
                        ne->value = col;
                        IInspectable_AddRef( col );
                    }
                    *out_val = col;
                    return S_OK;
                }
            }
            if (SUCCEEDED(element_create_typed( L"Windows.UI.Xaml.Media.SolidColorBrush", (void **)&br )) && br)
            {
                struct uwp_rd_bucket *target = b ? b : get_rd_bucket( rd_key, TRUE );
                struct uwp_rd_entry *ne = rd_bucket_upsert( target, key_str );
                if (ne)
                {
                    ne->kind = UWP_RD_BRUSH;
                    ne->argb = argb;
                    ne->has_argb = TRUE;
                    ne->value = br;
                    IInspectable_AddRef( br );
                }
                uwp_set_brush_element_argb( br, argb );
                *out_val = br;
                return S_OK;
            }
        }
    }

    /* Synthesize MenuFlyout / Style / Brush if requested by name from Resources */
    if (wcsstr( key_str, L"ContextMenu" ) || wcsstr( key_str, L"Flyout" ))
    {
        IInspectable *flyout = NULL;
        if (SUCCEEDED(element_create_typed( L"Windows.UI.Xaml.Controls.MenuFlyout", (void **)&flyout )) && flyout)
        {
            uwp_rd_insert( rd_key, key_str, flyout );
            *out_val = flyout;
            return S_OK;
        }
    }
    if (wcsstr( key_str, L"Style" ))
    {
        IInspectable *st = NULL;
        if (SUCCEEDED(element_create_typed( L"Windows.UI.Xaml.Style", (void **)&st )) && st)
        {
            uwp_rd_insert( rd_key, key_str, st );
            *out_val = st;
            return S_OK;
        }
    }
    if (wcsstr( key_str, L"Brush" ))
    {
        IInspectable *br = NULL;
        if (SUCCEEDED(element_create_typed( L"Windows.UI.Xaml.Media.SolidColorBrush", (void **)&br )) && br)
        {
            uwp_rd_insert( rd_key, key_str, br );
            *out_val = br;
            return S_OK;
        }
    }
    if (wcsstr( key_str, L"Template" ))
    {
        IInspectable *dt = NULL;
        if (SUCCEEDED(element_create_typed( L"Windows.UI.Xaml.DataTemplate", (void **)&dt )) && dt)
        {
            uwp_rd_insert( rd_key, key_str, dt );
            *out_val = dt;
            return S_OK;
        }
    }
    if (wcsstr( key_str, L"Layout" ))
    {
        IInspectable *ly = NULL;
        if ((SUCCEEDED(uwp_create_instance_from_provider( L"Microsoft.UI.Xaml.Controls.UniformGridLayout", (void **)&ly )) && ly) ||
            (SUCCEEDED(uwp_create_instance_from_provider( L"Microsoft.UI.Xaml.Controls.StackLayout", (void **)&ly )) && ly))
        {
            uwp_rd_insert( rd_key, key_str, ly );
            *out_val = ly;
            return S_OK;
        }
    }

    return 0x8000000b; /* E_BOUNDS */
}

BOOL uwp_rd_haskey( void *rd_key, void *key_obj )
{
    void *val = NULL;
    if (SUCCEEDED(uwp_rd_lookup( rd_key, key_obj, &val )) && val)
    {
        IInspectable_Release( (IInspectable *)val );
        return TRUE;
    }
    return FALSE;
}

/* ============================================================================
 * 4. UWP Visual Element & Distinct Per-Interface Sub-Objects
 * ============================================================================ */





enum uwp_vsm_prop_kind
{
    UWP_VSM_PROP_NONE = 0,
    UWP_VSM_PROP_GRID_LEN,
    UWP_VSM_PROP_WIDTH,
    UWP_VSM_PROP_HEIGHT,
    UWP_VSM_PROP_MIN_WIDTH,
    UWP_VSM_PROP_MAX_WIDTH,
    UWP_VSM_PROP_MIN_HEIGHT,
    UWP_VSM_PROP_MAX_HEIGHT,
    UWP_VSM_PROP_VISIBILITY,
    UWP_VSM_PROP_IS_ENABLED,
    UWP_VSM_PROP_GRID_ROW,
    UWP_VSM_PROP_GRID_ROW_SPAN,
    UWP_VSM_PROP_GRID_COL,
    UWP_VSM_PROP_GRID_COL_SPAN,
    UWP_VSM_PROP_FONT_SIZE,
};


struct uwp_vsm_snapshot_entry
{
    struct uwp_element *target;
    enum uwp_vsm_prop_kind prop_kind;
    int old_int;
    double old_num;
};




/* Global registry of live uwp_elements so we can resolve from any interface or outer.
 * Linked list (newest first): static-array registry overflowed at 2048 entries with
 * App.xbf style/setter/visualstate churn, silently dropping the Page root. */
struct uwp_element *g_all_uwp_elems_head = NULL;
int g_all_uwp_elem_cnt = 0;
SRWLOCK g_all_uwp_elems_lock = SRWLOCK_INIT;
BOOL g_layout_dirty = TRUE;
struct uwp_element *g_captured_elem = NULL;
struct uwp_element *g_hovered_elem = NULL;
/* Last PAGE-kind element created (set by uwp_element_create). Navigate uses it
 * as fallback when the RCW page pointer cannot be resolved to an element. */
struct uwp_element *g_last_created_page = NULL;
/* Page element adopted by the last Frame.Navigate; rendered directly because
 * Frame.content often holds an unresolvable MCG RCW pointer. */
struct uwp_element *g_navigated_page_el = NULL;

void uwp_invalidate_window( void );
struct uwp_element *uwp_elem_from_any( void *ptr );
BOOL uwp_elem_is_descendant( struct uwp_element *ancestor, struct uwp_element *node );
void uwp_invoke_button( struct uwp_element *btn );
BOOL uwp_dispatch_keydown( HWND hwnd, UINT vk, LPARAM lparam, void *island_root_ptr );
void uwp_show_flyout_popup( struct uwp_element *flyout, HWND owner_hwnd, int screen_x, int screen_y );
void uwp_close_flyout_popup( BOOL fire_closed );
BOOL uwp_popup_is_open_for( struct uwp_element *flyout );
BOOL uwp_popup_handle_escape( void );
HWND g_popup_hwnd = NULL;
HWND g_sub_popup_hwnd = NULL;
struct uwp_element *g_popup_owner_el = NULL;
struct uwp_element *g_last_closed_flyout = NULL;
DWORD g_last_closed_tick = 0;

struct uwp_accel_entry *g_accel_entries = NULL;
int g_accel_cnt = 0;
static int g_accel_cap = 0;

struct uwp_element *uwp_find_owning_page( struct uwp_element *el )
{
    struct uwp_element *cur = el;
    int depth = 0;
    while (cur && depth < 64)
    {
        if (cur->kind == UWP_KIND_PAGE || cur->kind == UWP_KIND_USER_CONTROL)
            return cur;
        if (cur->parent)
            cur = cur->parent;
        else if (cur->owner_component && cur->owner_component != cur)
            cur = cur->owner_component;
        else
            break;
        depth++;
    }
    if (el && el->owner_component) return el->owner_component;
    return g_navigated_page_el ? g_navigated_page_el : g_last_created_page;
}

void uwp_accel_register( struct uwp_element *accel_el, struct uwp_element *target_el,
                                UINT vk, UINT mods, BOOL enabled )
{
    struct uwp_element *page_el;
    int i;

    if (!vk) return;
    if (!target_el && accel_el) target_el = accel_el->accel_target;
    page_el = target_el ? uwp_find_owning_page( target_el ) : (accel_el ? uwp_find_owning_page( accel_el ) : g_navigated_page_el);

    AcquireSRWLockExclusive( &g_all_uwp_elems_lock );
    if (accel_el)
    {
        for (i = 0; i < g_accel_cnt; i++)
        {
            if (g_accel_entries[i].accel_el == accel_el)
            {
                if (target_el) g_accel_entries[i].target_el = target_el;
                if (page_el) g_accel_entries[i].page_el = page_el;
                g_accel_entries[i].vk = vk;
                g_accel_entries[i].mods = mods;
                g_accel_entries[i].enabled = enabled;
                TRACE( "accel_reg(update): vk=%#x mods=%#x enabled=%d target=%s (%s) page=%s (%s)\n",
                       vk, mods, enabled,
                       g_accel_entries[i].target_el ? debugstr_w(g_accel_entries[i].target_el->class_name) : "?",
                       g_accel_entries[i].target_el ? debugstr_w(g_accel_entries[i].target_el->name) : "",
                       g_accel_entries[i].page_el ? debugstr_w(g_accel_entries[i].page_el->class_name) : "?",
                       g_accel_entries[i].page_el ? debugstr_w(g_accel_entries[i].page_el->name) : "" );
                ReleaseSRWLockExclusive( &g_all_uwp_elems_lock );
                return;
            }
        }
    }
    else if (target_el)
    {
        for (i = 0; i < g_accel_cnt; i++)
        {
            if (!g_accel_entries[i].accel_el && g_accel_entries[i].target_el == target_el &&
                g_accel_entries[i].vk == vk && g_accel_entries[i].mods == mods)
            {
                g_accel_entries[i].enabled = enabled;
                if (page_el) g_accel_entries[i].page_el = page_el;
                ReleaseSRWLockExclusive( &g_all_uwp_elems_lock );
                return;
            }
        }
    }
    if (xaml_vec_grow( (void **)&g_accel_entries, &g_accel_cap, g_accel_cnt + 1, sizeof(*g_accel_entries), 32 ))
    {
        struct uwp_accel_entry *e = &g_accel_entries[g_accel_cnt++];
        e->accel_el = accel_el;
        e->target_el = target_el;
        e->page_el = page_el;
        e->vk = vk;
        e->mods = mods;
        e->enabled = enabled;
        TRACE( "accel_reg: vk=%#x mods=%#x enabled=%d target=%s (%s) page=%s (%s) total=%d\n",
               vk, mods, enabled,
               target_el ? debugstr_w(target_el->class_name) : "?",
               target_el ? debugstr_w(target_el->name) : "",
               page_el ? debugstr_w(page_el->class_name) : "?",
               page_el ? debugstr_w(page_el->name) : "",
               g_accel_cnt );
    }
    ReleaseSRWLockExclusive( &g_all_uwp_elems_lock );
}

UINT uwp_parse_vk_string( const WCHAR *s )
{
    if (!s || !s[0]) return 0;
    if (!s[1])
    {
        WCHAR ch = towupper( s[0] );
        if ((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')) return (UINT)ch;
        if (ch == '+') return VK_OEM_PLUS;
        if (ch == '-') return VK_OEM_MINUS;
        if (ch == '.') return VK_OEM_PERIOD;
        if (ch == ',') return VK_OEM_COMMA;
    }
    if (!wcsicmp( s, L"Insert" )) return VK_INSERT;
    if (!wcsicmp( s, L"Delete" )) return VK_DELETE;
    if (!wcsicmp( s, L"Escape" ) || !wcsicmp( s, L"Esc" )) return VK_ESCAPE;
    if (!wcsicmp( s, L"Back" ) || !wcsicmp( s, L"Backspace" )) return VK_BACK;
    if (!wcsicmp( s, L"Enter" ) || !wcsicmp( s, L"Return" )) return VK_RETURN;
    if (!wcsicmp( s, L"Tab" )) return VK_TAB;
    if (!wcsicmp( s, L"Space" )) return VK_SPACE;
    if (!wcsicmp( s, L"Up" )) return VK_UP;
    if (!wcsicmp( s, L"Down" )) return VK_DOWN;
    if (!wcsicmp( s, L"Left" )) return VK_LEFT;
    if (!wcsicmp( s, L"Right" )) return VK_RIGHT;
    if ((s[0] == 'F' || s[0] == 'f') && s[1] >= '1' && s[1] <= '9')
    {
        int fn = _wtoi( s + 1 );
        if (fn >= 1 && fn <= 24) return (UINT)(VK_F1 + fn - 1);
    }
    return 0;
}

static void uwp_set_brush_element_argb( IInspectable *br, UINT32 argb )
{
    struct uwp_element *el = uwp_elem_from_any( br );
    if (el)
    {
        el->brush_argb = argb;
        el->has_brush_argb = TRUE;
        el->brush_opacity = (double)((argb >> 24) & 0xff) / 255.0;
    }
}

struct uwp_rd_bucket *uwp_rd_bucket_for_el( struct uwp_element *el, BOOL create )
{
    struct uwp_rd_bucket *b;
    if (!el) return NULL;
    if (el->rd_bucket) return el->rd_bucket;
    if (el->resources_rd)
    {
        b = get_rd_bucket( el->resources_rd, create );
        if (b) { b->owner_el = el; el->rd_bucket = b; }
        return b;
    }
    if (el->kind == UWP_KIND_RESOURCE_DICT)
    {
        b = get_rd_bucket( &el->IInspectable_iface, create );
        if (b) { b->owner_el = el; el->rd_bucket = b; }
        return b;
    }
    if (!create) return NULL;
    xaml_resource_dictionary_create_for_uwp( (void **)&el->resources_rd );
    if (!el->resources_rd) return NULL;
    b = get_rd_bucket( el->resources_rd, TRUE );
    if (b) { b->owner_el = el; el->rd_bucket = b; }
    return b;
}

BOOL uwp_rd_find_entry( struct uwp_rd_bucket *start_bucket, struct uwp_element *scope_el,
                               const WCHAR *raw_key, int depth,
                               struct uwp_rd_entry **out_entry, struct uwp_rd_bucket **out_bucket )
{
    WCHAR key[128] = {0};
    struct uwp_rd_entry *e = NULL;
    struct uwp_rd_bucket *found_b = NULL;
    struct uwp_rd_bucket *visited[32];
    int vis_cnt = 0;
    struct uwp_element *cur;

    if (!raw_key || !raw_key[0] || !out_entry || depth > 14) return FALSE;
    *out_entry = NULL;
    if (out_bucket) *out_bucket = NULL;

    uwp_extract_resource_key( raw_key, key, ARRAY_SIZE(key) );
    if (!key[0]) return FALSE;

    if (start_bucket)
    {
        if (vis_cnt < 32) visited[vis_cnt++] = start_bucket;
        if (uwp_lookup_in_bucket( start_bucket, key, 0, &e, &found_b ))
            goto found;
    }

    /* Walk element -> parent -> ... -> page/component scope */
    cur = scope_el ? scope_el : (start_bucket ? start_bucket->owner_el : NULL);
    while (cur)
    {
        struct uwp_rd_bucket *cb = uwp_rd_bucket_for_el( cur, FALSE );
        if (cb)
        {
            int v;
            BOOL seen = FALSE;
            for (v = 0; v < vis_cnt; v++) if (visited[v] == cb) { seen = TRUE; break; }
            if (!seen)
            {
                if (vis_cnt < 32) visited[vis_cnt++] = cb;
                if (uwp_lookup_in_bucket( cb, key, 0, &e, &found_b ))
                    goto found;
            }
        }
        if (cur->parent) cur = cur->parent;
        else if (cur->owner_component && cur->owner_component != cur) cur = cur->owner_component;
        else break;
    }
    if (scope_el && scope_el->owner_component)
    {
        struct uwp_rd_bucket *ob = uwp_rd_bucket_for_el( scope_el->owner_component, FALSE );
        if (ob)
        {
            int v;
            BOOL seen = FALSE;
            for (v = 0; v < vis_cnt; v++) if (visited[v] == ob) { seen = TRUE; break; }
            if (!seen && uwp_lookup_in_bucket( ob, key, 0, &e, &found_b ))
                goto found;
        }
    }

    /* App-level Resources (App.xbf + registry Resources_xbf + merged/theme) wins over theme defaults */
    {
        struct uwp_rd_bucket *app_b = get_rd_bucket( &g_app_rd_sentinel, FALSE );
        BOOL ok = (app_b && uwp_lookup_in_bucket( app_b, key, 0, &e, &found_b ));
        TRACE( "uwp_rd_find_entry: app_b=%p ok=%d key=%s\n", app_b, ok, debugstr_w(key) );
        if (ok) goto found;
    }

    /* Theme defaults (WinUI themeresources.xbf Dark/Default) */
    {
        struct uwp_rd_bucket *theme_b = get_rd_bucket( &g_theme_rd_sentinel, FALSE );
        if (theme_b && uwp_lookup_in_bucket( theme_b, key, 0, &e, &found_b ))
            goto found;
    }

    return FALSE;

found:
    if (e && e->kind == UWP_RD_ALIAS && e->str_val[0])
    {
        if (uwp_rd_find_entry( start_bucket, scope_el, e->str_val, depth + 1, out_entry, out_bucket ))
            return TRUE;
        if (found_b && found_b != start_bucket &&
            uwp_rd_find_entry( found_b, found_b->owner_el, e->str_val, depth + 1, out_entry, out_bucket ))
            return TRUE;
    }
    *out_entry = e;
    if (out_bucket) *out_bucket = found_b;
    return TRUE;
}
HRESULT WINAPI uwp_do_GetValue( void *iface, void *dp, IInspectable **out );
HRESULT WINAPI uwp_do_SetValue( void *iface, void *dp, IInspectable *val );
HRESULT WINAPI uwp_do2_RegisterPropertyChangedCallback( void *iface, void *dp, void *cb, INT64 *token );
HRESULT WINAPI uwp_tb_put_Text( void *iface, HSTRING val );
void uwp_set_custom_xaml_member( void *app_outer, const WCHAR *owner_type_name,
                                        const WCHAR *prop_name, void *target_obj, const WCHAR *str_val );

/* --- Live engine: StandardCalculatorViewModel (CalcViewModel.dll) ---
 * Slot 7 = get_DisplayValue(HSTRING*), slot 79 = SendCommandToCalcManager(int).
 * Init is async (Concurrency task): m_calculatorManager lands at this+392
 * ~100ms after activation, so pump messages before the first SendCommand. */
extern HRESULT WINAPI RoActivateInstance( HSTRING activatableClassId, IInspectable **instance );

const GUID IID_INotifyPropertyChanged_UWP =
    { 0xcf75d69c, 0xf2f4, 0x486b, { 0xb3, 0x02, 0xbb, 0x4c, 0x09, 0xba, 0xeb, 0xfa } };
const GUID IID_IPropertyChangedEventHandler_UWP =
    { 0x50f19c16, 0x0a22, 0x4d8e, { 0xa0, 0x89, 0x1e, 0xa9, 0x95, 0x16, 0x57, 0xd2 } };
const GUID IID_IPropertyChangedEventArgs_UWP =
    { 0x4f33a9a0, 0x5cf4, 0x47a4, { 0xb1, 0x6f, 0xd7, 0xfa, 0xaf, 0x17, 0x45, 0x7e } };
const GUID IID_IReference_NumbersAndOperatorsEnum =
    { 0x487a747c, 0x8529, 0x548a, { 0x92, 0x6f, 0x3b, 0xe3, 0x9f, 0x3e, 0xcb, 0xbf } };
const GUID IID_IDependencyPropertyChangedCallback_UWP =
    { 0x49c85e2a, 0xf407, 0x48e8, { 0x84, 0x54, 0x3e, 0x3f, 0x98, 0x31, 0xe9, 0xb9 } };


/* Access metadata callback on our IDependencyProperty (from main.c) */
struct dp_internal_layout
{
    IDependencyProperty IDependencyProperty_iface;
    LONG ref;
    IPropertyMetadata *metadata;
    WCHAR name[64];
};
struct pm_internal_layout
{
    IPropertyMetadata IPropertyMetadata_iface;
    LONG ref;
    IInspectable *default_value;
    IPropertyChangedCallback *callback;
};

HRESULT xaml_dp_get_default_value( void *dp, IInspectable **out )
{
    struct dp_internal_layout *dpl = (struct dp_internal_layout *)dp;
    if (!out) return E_POINTER;
    *out = NULL;
    if (dpl && dpl->metadata)
        return IPropertyMetadata_get_DefaultValue( dpl->metadata, out );
    return S_OK;
}

/* --- IDependencyObject (slots 6=GetValue, 7=SetValue, 8=ClearValue, 9=ReadLocalValue, 10=GetAnimationBaseValue, 11=get_Dispatcher) --- */
HRESULT WINAPI uwp_do_GetValue( void *iface, void *dp, IInspectable **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    int i;
    if (!out) return E_POINTER;
    *out = NULL;
    if (!impl || !dp) return S_OK;
    {
        extern BOOL g_in_tap_diag;
        if (g_in_tap_diag)
            WARN( "TAP GetValue on %s (%s) dp=%p\n", debugstr_w(impl->class_name), debugstr_w(impl->name), dp );
    }

    for (i = 0; i < impl->dp_cnt; i++)
    {
        if (impl->dp_store[i].dp == dp)
        {
            *out = impl->dp_store[i].value;
            if (*out) IInspectable_AddRef( *out );
            return S_OK;
        }
    }

    /* Fall back to IPropertyMetadata::get_DefaultValue if DP has metadata */
    {
        struct dp_internal_layout *dpl = (struct dp_internal_layout *)dp;
        if (dpl && dpl->metadata)
        {
            IPropertyMetadata_get_DefaultValue( dpl->metadata, out );
        }
    }
    return S_OK;
}

static struct uwp_element *uwp_find_named_descendant( struct uwp_element *el, const WCHAR *name, int depth )
{
    if (!el || !name || !name[0] || depth > 8) return NULL;
    if (el->name[0] && !wcscmp( el->name, name )) return el;
    if (el->content)
    {
        struct uwp_element *ch = uwp_elem_from_any( el->content );
        struct uwp_element *r = (ch && ch != el) ? uwp_find_named_descendant( ch, name, depth + 1 ) : NULL;
        if (r) return r;
    }
    if (el->children_coll)
    {
        struct uwp_vec_view *v = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        unsigned int k;
        for (k = 0; k < v->count; k++)
        {
            struct uwp_element *ch = uwp_elem_from_any( v->items[k] );
            struct uwp_element *r = (ch && ch != el) ? uwp_find_named_descendant( ch, name, depth + 1 ) : NULL;
            if (r) return r;
        }
    }
    return NULL;
}

static void uwp_sync_template_presenter( struct uwp_element *impl, const WCHAR *dpname, IInspectable *val )
{
    const WCHAR *part_name = NULL;
    struct uwp_element *pres;

    if (!impl || !dpname || !dpname[0]) return;
    if (!wcscmp( dpname, L"ItemsSource" ) ||
        !wcscmp( dpname, L"ItemTemplate" ) ||
        !wcscmp( dpname, L"ItemTemplateSelector" ))
    {
        struct uwp_element *rep;
        const WCHAR *target_dp = !wcscmp( dpname, L"ItemsSource" ) ? L"ItemsSource" : L"ItemTemplate";
        int j;
        if (!val || wcsstr( impl->class_name, L"ResponsiveItemsRepeaterList" )) return;
        rep = uwp_find_named_descendant( impl, L"Repeater", 0 );
        if (!rep || rep == impl || !wcsstr( rep->class_name, L"ItemsRepeater" )) return;
        for (j = 0; j < rep->dp_cnt; j++)
        {
            struct dp_internal_layout *rdpl = (struct dp_internal_layout *)rep->dp_store[j].dp;
            if (rdpl && (ULONG_PTR)rdpl >= 0x10000 && !wcscmp( rdpl->name, target_dp ))
            {
                if (rep->dp_store[j].value != val)
                {
                    WARN( "uwp_sync_template_presenter: forwarding %s -> Repeater.%s val=%p\n",
                          debugstr_w(dpname), debugstr_w(target_dp), val );
                    uwp_do_SetValue( &rep->IDependencyObject_iface, rep->dp_store[j].dp, val );
                }
                break;
            }
        }
        return;
    }
    if (!wcscmp( dpname, L"Header" )) part_name = L"HeaderPresenter";
    else if (!wcscmp( dpname, L"IslandHeaderContent" )) part_name = L"IslandHeaderPresenter";
    else if (!wcscmp( dpname, L"IslandContent" )) part_name = L"IslandPresenter";
    else return;

    pres = uwp_find_named_descendant( impl, part_name, 0 );
    if (!pres) return;
    if (pres->content != val)
    {
        if (pres->content) IInspectable_Release( pres->content );
        pres->content = val;
        if (pres->content) IInspectable_AddRef( pres->content );
        uwp_invalidate_window();
    }
}

void uwp_sync_all_template_presenters( struct uwp_element *impl )
{
    int i;
    if (!impl) return;
    for (i = 0; i < impl->dp_cnt; i++)
    {
        struct dp_internal_layout *dpl = (struct dp_internal_layout *)impl->dp_store[i].dp;
        if (dpl && (ULONG_PTR)dpl >= 0x10000 && dpl->name[0])
            uwp_sync_template_presenter( impl, dpl->name, impl->dp_store[i].value );
    }
}

HRESULT WINAPI uwp_do_SetValue( void *iface, void *dp, IInspectable *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    IInspectable *old_val = NULL;
    WCHAR str_val[256] = {0};
    int i;
    static struct { struct uwp_element *el; void *dp; } setval_stack[32];
    static int setval_depth = 0;
    BOOL found_existing = FALSE;
    int si;

    if (!impl || !dp) return S_OK;
    for (si = 0; si < setval_depth; si++)
    {
        if (setval_stack[si].el == impl && setval_stack[si].dp == dp)
            return S_OK;
    }
    if (setval_depth >= (int)ARRAY_SIZE(setval_stack))
        return S_OK;

    /* Check if val is a string (e.g. CalculationResult.DisplayValue or Content) */
    if (val && uwp_unbox_string( val, str_val, ARRAY_SIZE(str_val) ) && str_val[0])
    {
        if (uwp_is_display_result_element( impl ) ||
            wcsstr( impl->class_name, L"OverflowTextBlock" ) ||
            wcsstr( impl->class_name, L"TabsBarItem" ) ||
            wcsstr( impl->class_name, L"TabViewItem" ) ||
            wcsstr( impl->class_name, L"MenuBarItem" ) ||
            !wcscmp( impl->class_name, L"Windows.UI.Xaml.Controls.Control" ) ||
            !wcscmp( impl->class_name, L"Windows.UI.Xaml.Controls.ListViewItem" ) ||
            impl->kind == UWP_KIND_TEXT_BLOCK)
        {
            lstrcpynW( impl->text, str_val, ARRAY_SIZE(impl->text) );
            if (impl->tpl_normal_output)
                lstrcpynW( impl->tpl_normal_output->text, str_val, ARRAY_SIZE(impl->tpl_normal_output->text) );
            TRACE( "SetValue updated display text on %s (%s) -> %s\n",
                   debugstr_w(impl->class_name), debugstr_w(impl->name), debugstr_w(str_val) );
            uwp_invalidate_window();
        }
    }
    if (wcsstr( impl->class_name, L"TabView" ) && !wcsstr( impl->class_name, L"TabViewItem" ))
    {
        struct dp_internal_layout *dpl = (struct dp_internal_layout *)dp;
        const WCHAR *dpname = (dpl && (ULONG_PTR)dpl >= 0x10000) ? dpl->name : L"";
        TRACE( "TabView SetValue dp=%s val=%p\n", debugstr_w(dpname), val );
        if (!wcscmp( dpname, L"TabItemsSource" ))
        {
            if (impl->tab_items_source != val)
            {
                if (impl->tab_items_source) IInspectable_Release( impl->tab_items_source );
                impl->tab_items_source = val;
                if (impl->tab_items_source) IInspectable_AddRef( impl->tab_items_source );
                uwp_invalidate_window();
            }
        }
        else if (!wcscmp( dpname, L"AddTabButtonCommand" ))
        {
            if (impl->tab_add_cmd != val)
            {
                if (impl->tab_add_cmd) IInspectable_Release( impl->tab_add_cmd );
                impl->tab_add_cmd = val;
                if (impl->tab_add_cmd) IInspectable_AddRef( impl->tab_add_cmd );
            }
        }
        else if (!wcscmp( dpname, L"AddTabButtonCommandParameter" ))
        {
            if (impl->tab_add_cmd_param != val)
            {
                if (impl->tab_add_cmd_param) IInspectable_Release( impl->tab_add_cmd_param );
                impl->tab_add_cmd_param = val;
                if (impl->tab_add_cmd_param) IInspectable_AddRef( impl->tab_add_cmd_param );
            }
        }
        else if (val && (ULONG_PTR)val >= 0x10000)
        {
            static const GUID IID_ITabItemViewModel_dp =
                { 0x609b13b7, 0x05f6, 0x5a02, { 0xb8, 0xe7, 0x5b, 0xbd, 0x2c, 0x33, 0x51, 0x8d } };
            void *vm = NULL;
            if (SUCCEEDED(IInspectable_QueryInterface( val, &IID_ITabItemViewModel_dp, &vm )) && vm)
            {
                IUnknown_Release( (IUnknown *)vm );
                if (impl->selected_item != val)
                {
                    if (impl->selected_item) IInspectable_Release( impl->selected_item );
                    impl->selected_item = val;
                    IInspectable_AddRef( impl->selected_item );
                    uwp_invalidate_window();
                }
            }
        }
    }

    for (i = 0; i < impl->dp_cnt; i++)
    {
        if (impl->dp_store[i].dp == dp)
        {
            found_existing = TRUE;
            old_val = impl->dp_store[i].value;
            if (old_val == val)
                return S_OK;
            impl->dp_store[i].value = val;
            if (val) IInspectable_AddRef( val );
            break;
        }
    }
    if (!found_existing)
    {
        if (!xaml_vec_grow( (void **)&impl->dp_store, &impl->dp_cap, impl->dp_cnt + 1, sizeof(*impl->dp_store), 24 ))
            return E_OUTOFMEMORY;
        impl->dp_store[impl->dp_cnt].dp = dp;
        impl->dp_store[impl->dp_cnt].value = val;
        if (val) IInspectable_AddRef( val );
        impl->dp_cnt++;
    }

    setval_stack[setval_depth].el = impl;
    setval_stack[setval_depth].dp = dp;
    setval_depth++;

    /* Fire PropertyMetadata PropertyChangedCallback if present */
    {
        struct dp_internal_layout *dpl = (struct dp_internal_layout *)dp;
        if (dpl && dpl->name[0])
            uwp_sync_template_presenter( impl, dpl->name, val );
        if (impl->kind == UWP_KIND_IMAGE && dpl && !wcscmp( dpl->name, L"Source" ))
        {
            if (impl->img_source != val)
            {
                if (impl->img_source) IInspectable_Release( impl->img_source );
                impl->img_source = val;
                if (impl->img_source) IInspectable_AddRef( impl->img_source );
            }
            impl->image_src[0] = 0;
            if (val)
            {
                struct uwp_element *src_el = uwp_elem_from_any( val );
                if (src_el && src_el->image_src[0])
                    lstrcpynW( impl->image_src, src_el->image_src, ARRAY_SIZE(impl->image_src) );
                else if (!uwp_unbox_string( val, impl->image_src, ARRAY_SIZE(impl->image_src) ))
                    uwp_extract_uri_string( val, impl->image_src, ARRAY_SIZE(impl->image_src) );
            }
            impl->img_loaded = FALSE;
            InterlockedExchange( &impl->img_loading, 0 );
            uwp_invalidate_window();
        }
        if (dpl && wcsstr( impl->class_name, L"ResponsiveItemsRepeaterList" ))
        {
            HSTRING hvc = NULL;
            if (val) IInspectable_GetRuntimeClassName( val, &hvc );
            WARN( "TileList_SetValue (%s) dp=%s val=%p (%s) cb=%p\n",
                  debugstr_w(impl->name), debugstr_w(dpl->name), val,
                  hvc ? debugstr_w(WindowsGetStringRawBuffer(hvc, NULL)) : "\"null\"",
                  dpl->metadata ? ((struct pm_internal_layout *)dpl->metadata)->callback : NULL );
            if (hvc) WindowsDeleteString( hvc );
        }
        if (dpl && dpl->metadata)
        {
            struct pm_internal_layout *pml = (struct pm_internal_layout *)dpl->metadata;
            if (!old_val && pml->default_value)
            {
                old_val = pml->default_value;
                IInspectable_AddRef( old_val );
            }
            if (pml->callback)
            {
                struct uwp_dp_changed_args *args = calloc( 1, sizeof(*args) );
                if (args)
                {
                    IInspectable *sender = impl->outer ? impl->outer : &impl->IInspectable_iface;
                    args->vtbl = dp_changed_args_vtbl;
                    args->ref = 1;
                    args->dp = dp;
                    args->old_val = old_val;
                    if (old_val) IInspectable_AddRef( old_val );
                    args->new_val = val;
                    if (val) IInspectable_AddRef( val );
                    TRACE( "uwp_do_SetValue invoking PropertyChangedCallback=%p sender=%p (%s) dp=%p old=%p new=%p\n",
                           pml->callback, sender, debugstr_w(impl->class_name), dp, old_val, val );
                    {
                        HRESULT cb_hr = S_OK;
                        __TRY
                        {
                            cb_hr = IPropertyChangedCallback_Invoke( pml->callback, (IDependencyObject *)sender,
                                                                     (IDependencyPropertyChangedEventArgs *)args );
                        }
                        __EXCEPT_ALL { cb_hr = E_FAIL; }
                        __ENDTRY;
                        if (FAILED(cb_hr))
                            WARN( "uwp_do_SetValue PropertyChangedCallback=%p sender=%p (%s) dp=%p -> FAILED hr %#lx\n",
                                  pml->callback, sender, debugstr_w(impl->class_name), dp, cb_hr );
                    }
                    {
                        IInspectable *nv = NULL;
                        IDependencyPropertyChangedEventArgs_get_NewValue( (IDependencyPropertyChangedEventArgs *)args, &nv );
                        TRACE( "uwp_do_SetValue callback read NewValue -> %p (val %p)\n", (void *)nv, (void *)val );
                        if (nv) IInspectable_Release( nv );
                    }
                    dp_args_Release( args );
                }
            }
        }
    }

    /* Fire registered DependencyPropertyChangedCallbacks */
    for (i = 0; i < impl->dp_cb_cnt; i++)
    {
        if (impl->dp_cbs[i].dp == dp && impl->dp_cbs[i].callback)
        {
            void *sender = impl->outer ? (void *)impl->outer : (void *)&impl->IDependencyObject_iface;
            struct xaml_event_handler *h = (struct xaml_event_handler *)impl->dp_cbs[i].callback;
            HRESULT cb_hr = S_OK;
            __TRY
            {
                cb_hr = h->lpVtbl->Invoke( h, sender, dp );
            }
            __EXCEPT_ALL { cb_hr = E_FAIL; }
            __ENDTRY;
            TRACE( "uwp_do_SetValue dp_cb[%d] h=%p sender=%p (%s) dp=%p -> hr %#lx\n",
                   i, h, sender, debugstr_w(impl->class_name), dp, cb_hr );
            if (wcsstr( impl->class_name, L"ResponsiveItemsRepeaterList" ))
                WARN( "TileList dp_cb[%d] dp=%p -> hr %#lx\n", i, dp, cb_hr );
        }
    }
    if (setval_depth > 0) setval_depth--;

    if (wcsstr( impl->class_name, L"ItemsRepeater" ))
    {
        impl->last_measured_w = 0.0f;
        impl->last_measured_h = 0.0f;
        uwp_invalidate_window();
    }
    else
    {
        struct dp_internal_layout *dpl = (struct dp_internal_layout *)dp;
        if (dpl && (ULONG_PTR)dpl >= 0x10000 && dpl->name[0] &&
            (!wcscmp( dpl->name, L"ItemsSource" ) || !wcscmp( dpl->name, L"Content" ) ||
             !wcscmp( dpl->name, L"Text" ) || !wcscmp( dpl->name, L"Visibility" )))
        {
            extern void uwp_items_ctrl_materialize_source( struct uwp_element *impl, IInspectable *src );
            if (!wcscmp( dpl->name, L"ItemsSource" ) && val)
                uwp_items_ctrl_materialize_source( impl, val );
            uwp_invalidate_window();
        }
    }

    if (old_val) IInspectable_Release( old_val );
    return S_OK;
}

HRESULT WINAPI uwp_do_ClearValue( void *iface, void *dp )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    int i;
    if (!impl || !dp) return S_OK;
    for (i = 0; i < impl->dp_cnt; i++)
    {
        if (impl->dp_store[i].dp == dp)
        {
            if (impl->dp_store[i].value) IInspectable_Release( impl->dp_store[i].value );
            impl->dp_store[i] = impl->dp_store[--impl->dp_cnt];
            break;
        }
    }
    return S_OK;
}

HRESULT WINAPI uwp_do_get_Dispatcher( void *iface, void **value )
{
    ICoreWindow *cw;
    if (!value) return E_POINTER;
    *value = NULL;
    cw = get_current_core_window();
    if (cw) return ICoreWindow_get_Dispatcher( cw, (ICoreDispatcher **)value );
    return S_OK;
}

static IInspectable *g_cached_ui_dispatcher_queue;

HRESULT WINAPI uwp_do_get_DispatcherQueue( void *iface, void **value )
{
    static const GUID IID_IDispatcherQueueStatics_UWP =
        { 0xa96d83d7, 0x9371, 0x4517, { 0x92, 0x45, 0xd0, 0x82, 0x4a, 0xc1, 0x2c, 0x74 } };
    static const GUID IID_IMicrosoftDispatcherQueueStatics_WinUI3 =
        { 0xcd3382ea, 0xa455, 0x5124, { 0xb6, 0x3a, 0xca, 0x40, 0xd3, 0x4c, 0xa2, 0x3c } };
    BOOL is_winui3 = xaml_get_app_caps()->winui3;
    const WCHAR *name = is_winui3
        ? L"Microsoft.UI.Dispatching.DispatcherQueue"
        : L"Windows.System.DispatcherQueue";
    const GUID *statics_iid = is_winui3
        ? &IID_IMicrosoftDispatcherQueueStatics_WinUI3
        : &IID_IDispatcherQueueStatics_UWP;
    HSTRING hstr;
    IInspectable *statics = NULL;
    IInspectable *dq = NULL;
    HRESULT hr;

    if (!value) return E_POINTER;
    *value = NULL;
    if (g_cached_ui_dispatcher_queue)
    {
        IInspectable_AddRef( g_cached_ui_dispatcher_queue );
        *value = g_cached_ui_dispatcher_queue;
        return S_OK;
    }
    hr = WindowsCreateString( name, lstrlenW(name), &hstr );
    if (FAILED(hr)) return hr;
    hr = RoGetActivationFactory( hstr, statics_iid, (void **)&statics );
    WindowsDeleteString( hstr );
    if (SUCCEEDED(hr) && statics)
    {
        void **vtbl = *(void ***)statics;
        HRESULT (WINAPI *pGetForCurrentThread)(void *, void **) = (void *)vtbl[6];
        if (pGetForCurrentThread) hr = pGetForCurrentThread( statics, (void **)&dq );
        IInspectable_Release( statics );
    }
    if (dq)
    {
        if (!InterlockedCompareExchangePointer( (void **)&g_cached_ui_dispatcher_queue, dq, NULL ))
            IInspectable_AddRef( dq );
        *value = dq;
        return S_OK;
    }
    if (g_cached_ui_dispatcher_queue)
    {
        IInspectable_AddRef( g_cached_ui_dispatcher_queue );
        *value = g_cached_ui_dispatcher_queue;
        return S_OK;
    }
    return hr;
}

/* --- IDependencyObject2 (slot 6=RegisterPropertyChangedCallback, slot 7=UnregisterPropertyChangedCallback) --- */
void uwp_set_custom_xaml_member( void *app_outer, const WCHAR *owner_type_name,
                                        const WCHAR *prop_name, void *target_obj, const WCHAR *str_val )
{
    void *provider = NULL, *xaml_type = NULL, *member = NULL, *member_type = NULL, *boxed_val = NULL;
    HSTRING h_prop = NULL, h_val = NULL;
    HRESULT hr = E_FAIL;

    if (!app_outer || !owner_type_name || !prop_name || !target_obj || !str_val) return;
    if (FAILED(IInspectable_QueryInterface( (IInspectable *)app_outer, &IID_IXamlMetadataProvider_UWP_g, &provider )) || !provider)
        return;

    xaml_type = uwp_provider_get_xaml_type_cached( (struct xaml_metadata_provider *)provider, owner_type_name );
    if (xaml_type)
    {
        struct xaml_type *xt = (struct xaml_type *)xaml_type;
        if (xt->lpVtbl->GetMember && SUCCEEDED(WindowsCreateString( prop_name, wcslen(prop_name), &h_prop )))
        {
            if (SUCCEEDED(xt->lpVtbl->GetMember( xt, h_prop, (struct xaml_member **)&member )) && member)
            {
                struct xaml_member *xm = (struct xaml_member *)member;
                if (xm->lpVtbl->get_Type && SUCCEEDED(xm->lpVtbl->get_Type( xm, (struct xaml_type **)&member_type )) && member_type)
                {
                    struct xaml_type *mt = (struct xaml_type *)member_type;
                    if (mt->lpVtbl->CreateFromString && SUCCEEDED(WindowsCreateString( str_val, wcslen(str_val), &h_val )))
                    {
                        boolean is_attachable = FALSE;
                        BOOL is_bool_type = FALSE;
                        HSTRING h_tname = NULL;
                        if (xm->lpVtbl->get_IsAttachable)
                            xm->lpVtbl->get_IsAttachable( xm, &is_attachable );
                        if (mt->lpVtbl->get_FullName && SUCCEEDED(mt->lpVtbl->get_FullName( mt, &h_tname )) && h_tname)
                        {
                            const WCHAR *tn = WindowsGetStringRawBuffer( h_tname, NULL );
                            if (tn && (!wcscmp( tn, L"Boolean" ) || !wcscmp( tn, L"System.Boolean" )))
                                is_bool_type = TRUE;
                            else if (tn && (!wcscmp( tn, L"Int32" ) || !wcscmp( tn, L"System.Int32" )))
                            {
                                boxed_val = box_int32_value( _wtoi( str_val ) );
                                hr = boxed_val ? S_OK : E_FAIL;
                            }
                            else if (tn && (!wcscmp( tn, L"Double" ) || !wcscmp( tn, L"System.Double" )))
                            {
                                boxed_val = box_double_value( wcstod( str_val, NULL ) );
                                hr = boxed_val ? S_OK : E_FAIL;
                            }
                            else if (tn && (!wcscmp( tn, L"String" ) || !wcscmp( tn, L"System.String" )))
                            {
                                boxed_val = box_string_value( str_val );
                                hr = boxed_val ? S_OK : E_FAIL;
                            }
                            WindowsDeleteString( h_tname );
                        }
                        if (boxed_val)
                        {
                            /* already boxed directly as primitive */
                        }
                        else if (is_bool_type && is_attachable && (!wcsicmp( str_val, L"True" ) || !wcsicmp( str_val, L"False" )))
                        {
                            boxed_val = box_bool_value( !wcsicmp( str_val, L"True" ) ? TRUE : FALSE );
                            hr = boxed_val ? S_OK : E_FAIL;
                        }
                        else if (!is_bool_type)
                        {
                            hr = mt->lpVtbl->CreateFromString( mt, h_val, &boxed_val );
                            if (FAILED(hr)) boxed_val = NULL;
                        }
                        if (SUCCEEDED(hr) && boxed_val && xm->lpVtbl->SetValue)
                        {
                            hr = xm->lpVtbl->SetValue( xm, target_obj, boxed_val );
                            if (FAILED(hr))
                                WARN( "IXamlMember::SetValue(%s.%s = %s) -> hr %#lx\n",
                                      debugstr_w(owner_type_name), debugstr_w(prop_name),
                                      debugstr_w(str_val), hr );
                            else
                                TRACE( "IXamlMember::SetValue(%s.%s = %s) -> hr %#lx\n",
                                       debugstr_w(owner_type_name), debugstr_w(prop_name),
                                       debugstr_w(str_val), hr );
                        }
                        if (boxed_val) IInspectable_Release( (IInspectable *)boxed_val );
                        WindowsDeleteString( h_val );
                    }
                    IInspectable_Release( (IInspectable *)member_type );
                }
                IInspectable_Release( (IInspectable *)member );
            }
            WindowsDeleteString( h_prop );
        }
        IInspectable_Release( (IInspectable *)xaml_type );
    }
    IInspectable_Release( (IInspectable *)provider );
}

BOOL uwp_set_custom_xaml_member_obj( void *app_outer, const WCHAR *owner_type_name,
                                     const WCHAR *prop_name, void *target_obj, void *val_obj )
{
    void *provider = NULL, *xaml_type = NULL, *member = NULL;
    HSTRING h_prop = NULL;
    HRESULT hr = E_FAIL;
    BOOL ok = FALSE;

    if (!app_outer || !owner_type_name || !prop_name || !target_obj || !val_obj) return FALSE;
    if (FAILED(IInspectable_QueryInterface( (IInspectable *)app_outer, &IID_IXamlMetadataProvider_UWP_g, &provider )) || !provider)
        return FALSE;

    xaml_type = uwp_provider_get_xaml_type_cached( (struct xaml_metadata_provider *)provider, owner_type_name );
    if (xaml_type)
    {
        struct xaml_type *xt = (struct xaml_type *)xaml_type;
        if (xt->lpVtbl->GetMember && SUCCEEDED(WindowsCreateString( prop_name, wcslen(prop_name), &h_prop )))
        {
            if (SUCCEEDED(xt->lpVtbl->GetMember( xt, h_prop, (struct xaml_member **)&member )) && member)
            {
                struct xaml_member *xm = (struct xaml_member *)member;
                if (xm->lpVtbl->SetValue)
                {
                    __TRY
                    {
                        hr = xm->lpVtbl->SetValue( xm, target_obj, val_obj );
                    }
                    __EXCEPT_ALL { hr = E_FAIL; }
                    __ENDTRY;
                    ok = SUCCEEDED(hr);
                    TRACE( "IXamlMember::SetValueObj(%s.%s) -> hr %#lx\n",
                           debugstr_w(owner_type_name), debugstr_w(prop_name), hr );
                }
                IInspectable_Release( (IInspectable *)member );
            }
            WindowsDeleteString( h_prop );
        }
        IInspectable_Release( (IInspectable *)xaml_type );
    }
    IInspectable_Release( (IInspectable *)provider );
    return ok;
}

BOOL uwp_add_custom_xaml_collection_item( void *app_outer, const WCHAR *owner_type_name,
                                          const WCHAR *prop_name, void *target_obj, void *item_obj )
{
    void *provider = NULL, *xaml_type = NULL, *member = NULL, *member_type = NULL;
    IInspectable *coll = NULL;
    HSTRING h_prop = NULL;
    HRESULT hr = E_FAIL;
    BOOL ok = FALSE;

    if (!app_outer || !owner_type_name || !prop_name || !target_obj || !item_obj) return FALSE;
    if (FAILED(IInspectable_QueryInterface( (IInspectable *)app_outer, &IID_IXamlMetadataProvider_UWP_g, &provider )) || !provider)
        return FALSE;

    xaml_type = uwp_provider_get_xaml_type_cached( (struct xaml_metadata_provider *)provider, owner_type_name );
    if (xaml_type)
    {
        struct xaml_type *xt = (struct xaml_type *)xaml_type;
        if (xt->lpVtbl->GetMember && SUCCEEDED(WindowsCreateString( prop_name, wcslen(prop_name), &h_prop )))
        {
            if (SUCCEEDED(xt->lpVtbl->GetMember( xt, h_prop, (struct xaml_member **)&member )) && member)
            {
                struct xaml_member *xm = (struct xaml_member *)member;
                if (xm->lpVtbl->GetValue && xm->lpVtbl->get_Type)
                {
                    __TRY
                    {
                        hr = xm->lpVtbl->GetValue( xm, target_obj, &coll );
                    }
                    __EXCEPT_ALL { hr = E_FAIL; coll = NULL; }
                    __ENDTRY;
                    if (SUCCEEDED(hr) && coll &&
                        SUCCEEDED(xm->lpVtbl->get_Type( xm, (struct xaml_type **)&member_type )) && member_type)
                    {
                        struct xaml_type *mt = (struct xaml_type *)member_type;
                        if (mt->lpVtbl->AddToVector)
                        {
                            __TRY
                            {
                                hr = mt->lpVtbl->AddToVector( mt, coll, item_obj );
                            }
                            __EXCEPT_ALL { hr = E_FAIL; }
                            __ENDTRY;
                            ok = SUCCEEDED(hr);
                            TRACE( "IXamlMember::AddToVector(%s.%s coll=%p item=%p) -> hr %#lx\n",
                                   debugstr_w(owner_type_name), debugstr_w(prop_name), coll, item_obj, hr );
                        }
                        IInspectable_Release( (IInspectable *)member_type );
                    }
                    if (coll) IInspectable_Release( coll );
                }
                IInspectable_Release( (IInspectable *)member );
            }
            WindowsDeleteString( h_prop );
        }
        IInspectable_Release( (IInspectable *)xaml_type );
    }
    IInspectable_Release( (IInspectable *)provider );
    return ok;
}



struct xbf_aot_builder;





void uwp_vsm_invalidate_eval_chain( struct uwp_element *el )
{
    int depth = 0;
    while (el && depth++ < 32)
    {
        el->vsm_eval_valid = FALSE;
        if (el->owner_component && el->owner_component != el)
            el->owner_component->vsm_eval_valid = FALSE;
        if (el->parent == el) break;
        el = el->parent;
    }
}

struct uwp_vsm_group *uwp_vsm_add_group( struct uwp_element *el, const WCHAR *name )
{
    struct uwp_vsm_group *g;
    if (!el) return NULL;
    if (el->vsm_group_cnt >= el->vsm_group_cap)
    {
        int ncap = el->vsm_group_cap ? el->vsm_group_cap * 2 : 4;
        struct uwp_vsm_group *ng = realloc( el->vsm_groups, (size_t)ncap * sizeof(*ng) );
        if (!ng) return NULL;
        el->vsm_groups = ng;
        el->vsm_group_cap = ncap;
    }
    g = &el->vsm_groups[el->vsm_group_cnt++];
    memset( g, 0, sizeof(*g) );
    g->active_state_idx = -1;
    if (name && name[0]) lstrcpynW( g->name, name, ARRAY_SIZE(g->name) );
    uwp_vsm_invalidate_eval_chain( el );
    return g;
}

struct uwp_vsm_state *uwp_vsm_add_state( struct uwp_vsm_group *g, const WCHAR *name )
{
    struct uwp_vsm_state *st;
    if (!g) return NULL;
    if (g->state_cnt >= g->state_cap)
    {
        int ncap = g->state_cap ? g->state_cap * 2 : 8;
        struct uwp_vsm_state *ns = realloc( g->states, (size_t)ncap * sizeof(*ns) );
        if (!ns) return NULL;
        g->states = ns;
        g->state_cap = ncap;
    }
    st = &g->states[g->state_cnt++];
    memset( st, 0, sizeof(*st) );
    if (name && name[0]) lstrcpynW( st->name, name, ARRAY_SIZE(st->name) );
    return st;
}

void uwp_vsm_state_add_setter( struct uwp_vsm_state *st, const WCHAR *target, const WCHAR *prop,
                                      const WCHAR *val_str, int vt, double num, BOOL is_res_ref )
{
    struct uwp_vsm_setter *s;
    if (!st || !target || !target[0] || !prop || !prop[0]) return;
    if (st->setter_cnt >= st->setter_cap)
    {
        int ncap = st->setter_cap ? st->setter_cap * 2 : 8;
        struct uwp_vsm_setter *ns = realloc( st->setters, (size_t)ncap * sizeof(*ns) );
        if (!ns) return;
        st->setters = ns;
        st->setter_cap = ncap;
    }
    s = &st->setters[st->setter_cnt++];
    memset( s, 0, sizeof(*s) );
    lstrcpynW( s->target, target, ARRAY_SIZE(s->target) );
    lstrcpynW( s->property, prop, ARRAY_SIZE(s->property) );
    if (val_str && val_str[0]) lstrcpynW( s->val_str, val_str, ARRAY_SIZE(s->val_str) );
    s->vt = vt;
    s->num = num;
    s->is_res_ref = is_res_ref;
}

void vsm_clean_prop_name( const WCHAR *raw, WCHAR *out_prop, size_t max_prop )
{
    const WCHAR *p = raw;
    size_t len;
    if (!out_prop || !max_prop) return;
    out_prop[0] = 0;
    if (!p) return;
    while (*p == L' ' || *p == L'(') p++;
    len = wcslen( p );
    while (len > 0 && (p[len - 1] == L')' || p[len - 1] == L' ')) len--;
    if (len >= max_prop) len = max_prop - 1;
    memcpy( out_prop, p, len * sizeof(WCHAR) );
    out_prop[len] = 0;
}

void vsm_split_setter_target( const WCHAR *spec, WCHAR *out_target, size_t max_target,
                                     WCHAR *out_prop, size_t max_prop )
{
    const WCHAR *dot;
    size_t tlen;
    if (out_target && max_target) out_target[0] = 0;
    if (out_prop && max_prop) out_prop[0] = 0;
    if (!spec || !spec[0]) return;
    dot = wcschr( spec, L'.' );
    if (!dot) return;
    tlen = (size_t)(dot - spec);
    if (out_target && max_target)
    {
        if (tlen >= max_target) tlen = max_target - 1;
        memcpy( out_target, spec, tlen * sizeof(WCHAR) );
        out_target[tlen] = 0;
    }
    vsm_clean_prop_name( dot + 1, out_prop, max_prop );
}

void uwp_apply_style_by_name( struct xbf_ctx *ctx, struct uwp_element *el, const WCHAR *style_key )
{
    WCHAR key[128] = {0};
    struct uwp_rd_entry *se = NULL;
    struct uwp_rd_bucket *sb = NULL;
    if (!el || !style_key || !style_key[0]) return;
    uwp_extract_resource_key( style_key, key, ARRAY_SIZE(key) );
    if (!key[0]) return;
    uwp_ensure_app_resources_loaded();
    if (uwp_rd_find_entry( ctx ? ctx->target_rd : NULL, el, key, 0, &se, &sb ) && se && se->kind == UWP_RD_STYLE)
        uwp_apply_style_entry( ctx, el, sb, se, 0 );
}

static struct uwp_element *uwp_vsm_find_target_in_tree( struct uwp_element *root, const WCHAR *name )
{
    unsigned int k;
    struct uwp_element *hit;
    if (!root || !name || !name[0]) return NULL;
    if (!wcscmp( root->name, name )) return root;

    if (root->row_defs_coll)
    {
        struct uwp_vec_view *rows = CONTAINING_RECORD( root->row_defs_coll, struct uwp_vec_view, IInspectable_iface );
        for (k = 0; k < rows->count; k++)
        {
            struct uwp_element *rd = uwp_elem_from_any( rows->items[k] );
            if (rd && !wcscmp( rd->name, name )) return rd;
        }
    }
    if (root->col_defs_coll)
    {
        struct uwp_vec_view *cols = CONTAINING_RECORD( root->col_defs_coll, struct uwp_vec_view, IInspectable_iface );
        for (k = 0; k < cols->count; k++)
        {
            struct uwp_element *cd = uwp_elem_from_any( cols->items[k] );
            if (cd && !wcscmp( cd->name, name )) return cd;
        }
    }
    if (root->content)
    {
        struct uwp_element *ch = uwp_elem_from_any( root->content );
        if (ch && ch != root && (hit = uwp_vsm_find_target_in_tree( ch, name )) != NULL)
            return hit;
    }
    if (root->children_coll)
    {
        struct uwp_vec_view *children = CONTAINING_RECORD( root->children_coll, struct uwp_vec_view, IInspectable_iface );
        for (k = 0; k < children->count; k++)
        {
            struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
            if (ch && ch != root && (hit = uwp_vsm_find_target_in_tree( ch, name )) != NULL)
                return hit;
        }
    }
    return NULL;
}

static struct uwp_element *uwp_vsm_find_target( struct uwp_element *scope, const WCHAR *name )
{
    int i;
    struct uwp_element *hit;
    if (!scope || !name || !name[0]) return NULL;
    for (i = 0; i < scope->named_cnt; i++)
    {
        if (scope->named_elems[i] && !wcscmp( scope->named_elems[i]->name, name ))
            return scope->named_elems[i];
    }
    if (scope->owner_component && scope->owner_component != scope)
    {
        struct uwp_element *own = scope->owner_component;
        for (i = 0; i < own->named_cnt; i++)
        {
            if (own->named_elems[i] && !wcscmp( own->named_elems[i]->name, name ))
                return own->named_elems[i];
        }
    }
    hit = uwp_vsm_find_target_in_tree( scope, name );
    if (hit) return hit;
    if (scope->owner_component && scope->owner_component != scope)
        return uwp_vsm_find_target_in_tree( scope->owner_component, name );
    return NULL;
}

static void uwp_vsm_parse_grid_length( struct uwp_element *scope, const struct uwp_vsm_setter *s,
                                       int *out_type, double *out_val )
{
    if (s->vt == 0x07)
    {
        *out_type = 1;
        *out_val = s->num;
        return;
    }
    if (s->val_str[0])
    {
        const WCHAR *str = s->val_str;
        const WCHAR *star = wcschr( str, L'*' );
        if (!wcsicmp( str, L"Auto" ))
        {
            *out_type = 0;
            *out_val = 0.0;
            return;
        }
        if (star)
        {
            double v = (star == str) ? 1.0 : wcstod( str, NULL );
            *out_type = 2;
            *out_val = v;
            return;
        }
        if ((str[0] >= L'0' && str[0] <= L'9') || str[0] == L'-' || str[0] == L'+')
        {
            *out_type = 1;
            *out_val = wcstod( str, NULL );
            return;
        }
        {
            struct uwp_rd_entry *de = NULL;
            if (uwp_rd_find_entry( NULL, scope, str, 0, &de, NULL ) && de && de->kind == UWP_RD_DOUBLE)
            {
                *out_type = 1;
                *out_val = de->dval;
                return;
            }
        }
    }
    *out_type = 1;
    *out_val = s->num;
}

static double uwp_vsm_parse_double( struct uwp_element *scope, const struct uwp_vsm_setter *s )
{
    if (s->val_str[0])
    {
        const WCHAR *str = s->val_str;
        if ((str[0] >= L'0' && str[0] <= L'9') || str[0] == L'-' || str[0] == L'+')
            return wcstod( str, NULL );
        {
            struct uwp_rd_entry *de = NULL;
            if (uwp_rd_find_entry( NULL, scope, str, 0, &de, NULL ) && de && de->kind == UWP_RD_DOUBLE)
                return de->dval;
        }
    }
    return s->num;
}

static void uwp_vsm_record_snapshot( struct uwp_vsm_group *grp, struct uwp_element *target,
                                     enum uwp_vsm_prop_kind prop_kind )
{
    int i;
    struct uwp_vsm_snapshot_entry *e;
    if (!grp || !target || prop_kind == UWP_VSM_PROP_NONE) return;

    /* First-wins for current state entry */
    for (i = 0; i < grp->snapshot_cnt; i++)
    {
        if (grp->snapshots[i].target == target && grp->snapshots[i].prop_kind == prop_kind)
            return;
    }

    if (!xaml_vec_grow( (void **)&grp->snapshots, &grp->snapshot_cap, grp->snapshot_cnt + 1,
                        sizeof(*grp->snapshots), 16 ))
        return;

    e = &grp->snapshots[grp->snapshot_cnt++];
    memset( e, 0, sizeof(*e) );
    e->target = target;
    e->prop_kind = prop_kind;
    switch (prop_kind)
    {
    case UWP_VSM_PROP_GRID_LEN:
        e->old_int = target->grid_len_type;
        e->old_num = target->grid_len_val;
        break;
    case UWP_VSM_PROP_WIDTH:         e->old_num = target->width; break;
    case UWP_VSM_PROP_HEIGHT:        e->old_num = target->height; break;
    case UWP_VSM_PROP_MIN_WIDTH:     e->old_num = target->min_width; break;
    case UWP_VSM_PROP_MAX_WIDTH:     e->old_num = target->max_width; break;
    case UWP_VSM_PROP_MIN_HEIGHT:    e->old_num = target->min_height; break;
    case UWP_VSM_PROP_MAX_HEIGHT:    e->old_num = target->max_height; break;
    case UWP_VSM_PROP_VISIBILITY:    e->old_int = target->visibility; break;
    case UWP_VSM_PROP_IS_ENABLED:    e->old_int = target->is_enabled; break;
    case UWP_VSM_PROP_GRID_ROW:      e->old_int = target->grid_row; break;
    case UWP_VSM_PROP_GRID_ROW_SPAN: e->old_int = target->grid_row_span; break;
    case UWP_VSM_PROP_GRID_COL:      e->old_int = target->grid_col; break;
    case UWP_VSM_PROP_GRID_COL_SPAN: e->old_int = target->grid_col_span; break;
    case UWP_VSM_PROP_FONT_SIZE:     e->old_num = target->font_size; break;
    default: break;
    }
}

static void uwp_vsm_restore_snapshots( struct uwp_vsm_group *grp )
{
    int i;
    if (!grp) return;
    for (i = grp->snapshot_cnt - 1; i >= 0; i--)
    {
        const struct uwp_vsm_snapshot_entry *e = &grp->snapshots[i];
        struct uwp_element *target = e->target;
        if (!target) continue;
        switch (e->prop_kind)
        {
        case UWP_VSM_PROP_GRID_LEN:
            target->grid_len_type = e->old_int;
            target->grid_len_val  = e->old_num;
            break;
        case UWP_VSM_PROP_WIDTH:         target->width          = e->old_num; break;
        case UWP_VSM_PROP_HEIGHT:        target->height         = e->old_num; break;
        case UWP_VSM_PROP_MIN_WIDTH:     target->min_width      = e->old_num; break;
        case UWP_VSM_PROP_MAX_WIDTH:     target->max_width      = e->old_num; break;
        case UWP_VSM_PROP_MIN_HEIGHT:    target->min_height     = e->old_num; break;
        case UWP_VSM_PROP_MAX_HEIGHT:    target->max_height     = e->old_num; break;
        case UWP_VSM_PROP_VISIBILITY:    target->visibility     = e->old_int; break;
        case UWP_VSM_PROP_IS_ENABLED:    target->is_enabled     = (boolean)e->old_int; break;
        case UWP_VSM_PROP_GRID_ROW:      target->grid_row       = e->old_int; break;
        case UWP_VSM_PROP_GRID_ROW_SPAN: target->grid_row_span  = e->old_int; break;
        case UWP_VSM_PROP_GRID_COL:      target->grid_col       = e->old_int; break;
        case UWP_VSM_PROP_GRID_COL_SPAN: target->grid_col_span  = e->old_int; break;
        case UWP_VSM_PROP_FONT_SIZE:     target->font_size      = e->old_num; break;
        default: break;
        }
    }
    grp->snapshot_cnt = 0;
}

static void uwp_vsm_apply_setter( struct uwp_element *scope, struct uwp_vsm_group *grp,
                                  const struct uwp_vsm_setter *s )
{
    struct uwp_element *target = uwp_vsm_find_target( scope, s->target );
    const WCHAR *prop = s->property;
    if (!target || !prop[0]) return;

    if (!wcscmp( prop, L"Width" ) || wcsstr( prop, L".Width" ))
    {
        if (target->kind == UWP_KIND_COL_DEF || target->kind == UWP_KIND_ROW_DEF)
        {
            uwp_vsm_record_snapshot( grp, target, UWP_VSM_PROP_GRID_LEN );
            uwp_vsm_parse_grid_length( scope, s, &target->grid_len_type, &target->grid_len_val );
        }
        else
        {
            uwp_vsm_record_snapshot( grp, target, UWP_VSM_PROP_WIDTH );
            target->width = uwp_vsm_parse_double( scope, s );
        }
    }
    else if (!wcscmp( prop, L"Height" ) || wcsstr( prop, L".Height" ))
    {
        if (target->kind == UWP_KIND_ROW_DEF || target->kind == UWP_KIND_COL_DEF)
        {
            uwp_vsm_record_snapshot( grp, target, UWP_VSM_PROP_GRID_LEN );
            uwp_vsm_parse_grid_length( scope, s, &target->grid_len_type, &target->grid_len_val );
        }
        else
        {
            uwp_vsm_record_snapshot( grp, target, UWP_VSM_PROP_HEIGHT );
            target->height = uwp_vsm_parse_double( scope, s );
        }
    }
    else if (!wcscmp( prop, L"MinWidth" ) || wcsstr( prop, L".MinWidth" ))
    {
        uwp_vsm_record_snapshot( grp, target, UWP_VSM_PROP_MIN_WIDTH );
        target->min_width = uwp_vsm_parse_double( scope, s );
    }
    else if (!wcscmp( prop, L"MaxWidth" ) || wcsstr( prop, L".MaxWidth" ))
    {
        uwp_vsm_record_snapshot( grp, target, UWP_VSM_PROP_MAX_WIDTH );
        target->max_width = uwp_vsm_parse_double( scope, s );
    }
    else if (!wcscmp( prop, L"MinHeight" ) || wcsstr( prop, L".MinHeight" ))
    {
        uwp_vsm_record_snapshot( grp, target, UWP_VSM_PROP_MIN_HEIGHT );
        target->min_height = uwp_vsm_parse_double( scope, s );
    }
    else if (!wcscmp( prop, L"MaxHeight" ) || wcsstr( prop, L".MaxHeight" ))
    {
        uwp_vsm_record_snapshot( grp, target, UWP_VSM_PROP_MAX_HEIGHT );
        target->max_height = uwp_vsm_parse_double( scope, s );
    }
    else if (wcsstr( prop, L"Visibility" ))
    {
        int vis = target->visibility;
        if (s->val_str[0])
        {
            if (!wcsicmp( s->val_str, L"Collapsed" ) || !wcscmp( s->val_str, L"1" )) vis = 1;
            else if (!wcsicmp( s->val_str, L"Visible" ) || !wcscmp( s->val_str, L"0" )) vis = 0;
        }
        else
        {
            vis = ((int)s->num != 0) ? 1 : 0;
        }
        uwp_vsm_record_snapshot( grp, target, UWP_VSM_PROP_VISIBILITY );
        target->visibility = vis;
        if (!wcscmp( target->name, L"DockPanel" ))
        {
            struct uwp_element *hbtn = uwp_vsm_find_target( scope, L"HistoryButton" );
            struct uwp_element *mbtn = uwp_vsm_find_target( scope, L"MemoryButton" );
            if (hbtn)
            {
                uwp_vsm_record_snapshot( grp, hbtn, UWP_VSM_PROP_VISIBILITY );
                hbtn->visibility = (vis == 0) ? 1 : 0;
            }
            if (mbtn)
            {
                uwp_vsm_record_snapshot( grp, mbtn, UWP_VSM_PROP_VISIBILITY );
                mbtn->visibility = (vis == 0) ? 1 : 0;
            }
        }
    }
    else if (wcsstr( prop, L"IsEnabled" ))
    {
        uwp_vsm_record_snapshot( grp, target, UWP_VSM_PROP_IS_ENABLED );
        if (s->val_str[0])
            target->is_enabled = (!wcsicmp( s->val_str, L"True" ) || !wcscmp( s->val_str, L"1" )) ? TRUE : FALSE;
        else
            target->is_enabled = ((int)s->num != 0) ? TRUE : FALSE;
    }
    else if (wcsstr( prop, L"Grid.RowSpan" ))
    {
        uwp_vsm_record_snapshot( grp, target, UWP_VSM_PROP_GRID_ROW_SPAN );
        target->grid_row_span = max( 1, (int)uwp_vsm_parse_double( scope, s ) );
    }
    else if (wcsstr( prop, L"Grid.Row" ))
    {
        uwp_vsm_record_snapshot( grp, target, UWP_VSM_PROP_GRID_ROW );
        target->grid_row = (int)uwp_vsm_parse_double( scope, s );
    }
    else if (wcsstr( prop, L"Grid.ColumnSpan" ))
    {
        uwp_vsm_record_snapshot( grp, target, UWP_VSM_PROP_GRID_COL_SPAN );
        target->grid_col_span = max( 1, (int)uwp_vsm_parse_double( scope, s ) );
    }
    else if (wcsstr( prop, L"Grid.Column" ))
    {
        uwp_vsm_record_snapshot( grp, target, UWP_VSM_PROP_GRID_COL );
        target->grid_col = (int)uwp_vsm_parse_double( scope, s );
    }
    else if (wcsstr( prop, L"FontSize" ))
    {
        double fs = uwp_vsm_parse_double( scope, s );
        if (fs > 0.0)
        {
            uwp_vsm_record_snapshot( grp, target, UWP_VSM_PROP_FONT_SIZE );
            target->font_size = fs;
        }
    }
}

static void uwp_vsm_transition_group( struct uwp_element *scope, struct uwp_vsm_group *grp, int new_idx )
{
    int k;
    if (!scope || !grp || new_idx < -1 || new_idx >= grp->state_cnt) return;
    if (grp->active_state_idx == new_idx) return;

    if (grp->active_state_idx >= 0 && grp->active_state_idx < grp->state_cnt)
        uwp_vsm_restore_snapshots( grp );

    grp->active_state_idx = new_idx;
    if (new_idx >= 0)
    {
        struct uwp_vsm_state *new_st = &grp->states[new_idx];
        lstrcpynW( grp->current_state, new_st->name, ARRAY_SIZE(grp->current_state) );
        for (k = 0; k < new_st->setter_cnt; k++)
            uwp_vsm_apply_setter( scope, grp, &new_st->setters[k] );
        TRACE( "VSM group '%s' on %s (%s) -> state '%s' (%d setters)\n",
               debugstr_w(grp->name), debugstr_w(scope->class_name), debugstr_w(scope->name),
               debugstr_w(new_st->name), new_st->setter_cnt );
    }
    else
    {
        grp->current_state[0] = 0;
    }
    g_layout_dirty = TRUE;
}

void uwp_vsm_evaluate_tree( struct uwp_element *el, double win_w, double win_h )
{
    static int vsm_depth = 0;
    int g, s, i, j;
    unsigned int k;
    if (!el || vsm_depth >= 64) return;
    if (el->vsm_eval_valid && el->vsm_eval_width == win_w && el->vsm_eval_height == win_h)
        return;
    vsm_depth++;

    el->vsm_eval_width = win_w;
    el->vsm_eval_height = win_h;
    el->vsm_eval_valid = TRUE;

    for (g = 0; g < el->vsm_group_cnt; g++)
    {
        struct uwp_vsm_group *grp = &el->vsm_groups[g];
        int order[64];
        int n_trig = 0;
        int matched = -1;
        for (s = 0; s < grp->state_cnt && n_trig < (int)ARRAY_SIZE(order); s++)
        {
            if (grp->states[s].has_trigger)
                order[n_trig++] = s;
        }
        if (!n_trig) continue;

        /* Sort trigger states in ascending MinWindowWidth (then MinWindowHeight);
         * on equal trigger thresholds, place later-declared first so the LAST match
         * in ascending order resolves stable ties to the first-declared state. */
        for (i = 1; i < n_trig; i++)
        {
            int cur = order[i];
            const struct uwp_vsm_state *st_cur = &grp->states[cur];
            j = i - 1;
            while (j >= 0)
            {
                int prev = order[j];
                const struct uwp_vsm_state *st_prev = &grp->states[prev];
                BOOL swap = FALSE;
                if (st_cur->min_window_width < st_prev->min_window_width)
                    swap = TRUE;
                else if (st_cur->min_window_width == st_prev->min_window_width)
                {
                    if (st_cur->min_window_height < st_prev->min_window_height)
                        swap = TRUE;
                    else if (st_cur->min_window_height == st_prev->min_window_height && cur > prev)
                        swap = TRUE;
                }
                if (!swap) break;
                order[j + 1] = order[j];
                j--;
            }
            order[j + 1] = cur;
        }

        for (i = 0; i < n_trig; i++)
        {
            int idx = order[i];
            const struct uwp_vsm_state *st = &grp->states[idx];
            if (win_w >= st->min_window_width && win_h >= st->min_window_height)
                matched = idx;
        }
        if (matched < 0 && grp->state_cnt > 0)
            matched = 0;
        uwp_vsm_transition_group( el, grp, matched );
    }

    if (el->content)
    {
        struct uwp_element *ch = uwp_elem_from_any( el->content );
        if (ch && ch != el) uwp_vsm_evaluate_tree( ch, win_w, win_h );
    }
    if (el->children_coll)
    {
        struct uwp_vec_view *children = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        for (k = 0; k < children->count; k++)
        {
            struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
            if (ch && ch != el) uwp_vsm_evaluate_tree( ch, win_w, win_h );
        }
    }
    vsm_depth--;
}

static BOOL uwp_vsm_goto_state_on_tree( struct uwp_element *el, const WCHAR *state_name )
{
    int g, s;
    unsigned int k;
    BOOL found = FALSE;
    if (!el || !state_name || !state_name[0]) return FALSE;

    for (g = 0; g < el->vsm_group_cnt; g++)
    {
        struct uwp_vsm_group *grp = &el->vsm_groups[g];
        for (s = 0; s < grp->state_cnt; s++)
        {
            if (!wcscmp( grp->states[s].name, state_name ))
            {
                uwp_vsm_transition_group( el, grp, s );
                found = TRUE;
                break;
            }
        }
    }
    if (el->content)
    {
        struct uwp_element *ch = uwp_elem_from_any( el->content );
        if (ch && ch != el && uwp_vsm_goto_state_on_tree( ch, state_name ))
            found = TRUE;
    }
    if (el->children_coll)
    {
        struct uwp_vec_view *children = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        for (k = 0; k < children->count; k++)
        {
            struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
            if (ch && ch != el && uwp_vsm_goto_state_on_tree( ch, state_name ))
                found = TRUE;
        }
    }
    return found;
}

static HRESULT WINAPI vsm_GoToState( void *iface, void *ctrl, HSTRING state, boolean use_trans, boolean *ret )
{
    struct uwp_element *el = uwp_elem_from_any( ctrl );
    const WCHAR *state_str = WindowsGetStringRawBuffer( state, NULL );
    (void)iface; (void)use_trans;
    if (el && state_str && state_str[0])
    {
        if (!uwp_vsm_goto_state_on_tree( el, state_str ) && el->owner_component && el->owner_component != el)
            uwp_vsm_goto_state_on_tree( el->owner_component, state_str );
        uwp_invalidate_window();
    }
    if (ret) *ret = TRUE;
    return S_OK;
}
static HRESULT WINAPI vsm_GetVisualStateGroups( void *iface, void *obj, void **out )
{
    return uwp_vec_create( NULL, (IInspectable **)out );
}
static HRESULT WINAPI vsm_get_CustomVisualStateManagerProperty( void *iface, void **out )
{
    (void)iface;
    return dependency_property_create( NULL, (IDependencyProperty **)out );
}
static HRESULT WINAPI vsm_GetCustomVisualStateManager( void *iface, void *obj, void **out )
{
    (void)iface; (void)obj;
    if (!out) return E_POINTER;
    *out = NULL;
    return S_OK;
}
static HRESULT WINAPI vsm_SetCustomVisualStateManager( void *iface, void *obj, void *val )
{
    (void)iface; (void)obj; (void)val;
    return S_OK;
}
/* IVisualStateManagerStatics ({01d0e9e0-d713-414e-a74e-e63ec7ac8c3d}):
 *   slot 6:  GetVisualStateGroups
 *   slot 7:  get_CustomVisualStateManagerProperty
 *   slot 8:  GetCustomVisualStateManager
 *   slot 9:  SetCustomVisualStateManager
 *   slot 10: GoToState */
static const void *vsm_vtbl[16] =
{
    [0] = uwp_cf_QI,
    [1] = uwp_cf_AddRef,
    [2] = uwp_cf_Release,
    [3] = uwp_cf_GetIids,
    [4] = uwp_cf_GetRuntimeClassName,
    [5] = uwp_cf_GetTrustLevel,
    [6] = vsm_GetVisualStateGroups,
    [7] = vsm_get_CustomVisualStateManagerProperty,
    [8] = vsm_GetCustomVisualStateManager,
    [9] = vsm_SetCustomVisualStateManager,
    [10] = vsm_GoToState,
    [11 ... 15] = uwp_generic_noop,
};
struct uwp_class_factory vsm_factory_obj =
    { {(const struct IActivationFactoryVtbl *)vsm_vtbl}, {(const struct IInspectableVtbl *)uwp_cf_comp_vtbl}, {(const struct IInspectableVtbl *)vsm_vtbl}, L"Windows.UI.Xaml.VisualStateManager", { NULL } };

static HRESULT WINAPI sb_get_hstr_empty( void *iface, void *elem, HSTRING *out )
{
    if (out) *out = NULL;
    return S_OK;
}
static HRESULT WINAPI sb_set_noop( void *iface, void *elem, void *val )
{
    return S_OK;
}
/* IStoryboardStatics ({d82f07d8-73d5-4379-bd48-7e05184a8bad}):
 *   slot 6:  get_TargetPropertyProperty
 *   slot 7:  GetTargetProperty
 *   slot 8:  SetTargetProperty
 *   slot 9:  get_TargetNameProperty
 *   slot 10: GetTargetName
 *   slot 11: SetTargetName
 *   slot 12: SetTarget */
static const void *sb_statics_vtbl[16] =
{
    [0] = uwp_cf_QI,
    [1] = uwp_cf_AddRef,
    [2] = uwp_cf_Release,
    [3] = uwp_cf_GetIids,
    [4] = uwp_cf_GetRuntimeClassName,
    [5] = uwp_cf_GetTrustLevel,
    [6] = vsm_get_CustomVisualStateManagerProperty,
    [7] = sb_get_hstr_empty,
    [8] = sb_set_noop,
    [9] = vsm_get_CustomVisualStateManagerProperty,
    [10] = sb_get_hstr_empty,
    [11] = sb_set_noop,
    [12] = sb_set_noop,
    [13 ... 15] = uwp_generic_noop,
};
struct uwp_class_factory sb_factory_obj =
    { {(const struct IActivationFactoryVtbl *)uwp_cf_act_vtbl}, {(const struct IInspectableVtbl *)uwp_cf_comp_vtbl}, {(const struct IInspectableVtbl *)sb_statics_vtbl}, L"Windows.UI.Xaml.Media.Animation.Storyboard", { NULL } };

    /* VisualTreeHelperStatics */
static HRESULT WINAPI vth_FindPoint( void *iface, double x, double y, void *subtree, void **out )
{
    return uwp_vec_create( NULL, (IInspectable **)out );
}
static HRESULT WINAPI vth_FindRect( void *iface, double x, double y, double w, double h, void *subtree, void **out )
{
    return uwp_vec_create( NULL, (IInspectable **)out );
}
static HRESULT WINAPI vth_FindPointSub( void *iface, double x, double y, void *subtree, boolean all, void **out )
{
    return uwp_vec_create( NULL, (IInspectable **)out );
}
static HRESULT WINAPI vth_FindRectSub( void *iface, double x, double y, double w, double h, void *subtree, boolean all, void **out )
{
    return uwp_vec_create( NULL, (IInspectable **)out );
}
static HRESULT WINAPI vth_GetChild( void *iface, void *ref, INT32 idx, void **out )
{
    struct uwp_element *el = uwp_elem_from_any( ref );
    if (!out) return E_POINTER;
    *out = NULL;
    if (!el || idx < 0) return S_OK;
    if (el->children_coll)
    {
        void *item = NULL;
        HRESULT hr = uwp_vec_GetAt( el->children_coll, (unsigned int)idx, &item );
        if (SUCCEEDED( hr ) && item)
        {
            HRESULT qhr = IInspectable_QueryInterface( (IInspectable *)item, &IID_IDependencyObject, out );
            IInspectable_Release( (IInspectable *)item );
            return SUCCEEDED( qhr ) ? S_OK : S_OK;
        }
        return hr;
    }
    if (el->content && idx == 0)
    {
        if (FAILED( IInspectable_QueryInterface( (IInspectable *)el->content, &IID_IDependencyObject, out ) ))
            *out = NULL;
    }
    return S_OK;
}
static HRESULT WINAPI vth_GetChildrenCount( void *iface, void *ref, INT32 *count )
{
    struct uwp_element *el = uwp_elem_from_any( ref );
    if (!count) return E_POINTER;
    *count = 0;
    if (!el) return S_OK;
    if (el->children_coll)
    {
        unsigned int sz = 0;
        uwp_vec_get_Size( el->children_coll, &sz );
        *count = (INT32)sz;
    }
    else if (el->content)
    {
        void *dep = NULL;
        if (SUCCEEDED( IInspectable_QueryInterface( (IInspectable *)el->content, &IID_IDependencyObject, &dep ) ) && dep)
        {
            IInspectable_Release( (IInspectable *)dep );
            *count = 1;
        }
    }
    return S_OK;
}
static HRESULT WINAPI vth_GetParent( void *iface, void *ref, void **out )
{
    struct uwp_element *el = uwp_elem_from_any( ref );
    WARN( "vth_GetParent: ref=%p el=%p (%s) parent=%p (%s)\n", ref, el, el ? debugstr_w(el->class_name) : "?",
          el ? el->parent : NULL, (el && el->parent) ? debugstr_w(el->parent->class_name) : "?" );
    if (!out) return E_POINTER;
    *out = NULL;
    if (el && el->parent)
    {
        IInspectable *p = el->parent->outer ? el->parent->outer : &el->parent->IInspectable_iface;
        if (FAILED( IInspectable_QueryInterface( p, &IID_IDependencyObject, out ) ))
            *out = NULL;
    }
    return S_OK;
}
static HRESULT WINAPI vth_DisconnectChildren( void *iface, void *el )
{
    return S_OK;
}
static HRESULT WINAPI vth_QI( IActivationFactory *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IMarshal ) ||
        IsEqualGUID( iid, &IID_IReferenceTracker_UWP ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget_UWP ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget_UWP2 ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    *out = iface;
    return S_OK;
}
static const void *vth_vtbl[16] =
{
    [0] = vth_QI,
    [1] = uwp_cf_AddRef,
    [2] = uwp_cf_Release,
    [3] = uwp_cf_GetIids,
    [4] = uwp_cf_GetRuntimeClassName,
    [5] = uwp_cf_GetTrustLevel,
    [6] = vth_FindPoint,
    [7] = vth_FindRect,
    [8] = vth_FindPointSub,
    [9] = vth_FindRectSub,
    [10] = vth_GetChild,
    [11] = vth_GetChildrenCount,
    [12] = vth_GetParent,
    [13] = vth_DisconnectChildren,
    [14 ... 15] = uwp_generic_noop,
};
struct uwp_class_factory vth_factory_obj =
    { {(const struct IActivationFactoryVtbl *)vth_vtbl}, {(const struct IInspectableVtbl *)uwp_cf_comp_vtbl}, {(const struct IInspectableVtbl *)vth_vtbl}, L"Windows.UI.Xaml.Media.VisualTreeHelper", { NULL } };

/* AutomationProperties / ToolTipService / GridStatics attached property helpers */
static HRESULT WINAPI attached_prop_get_dp( void *iface, void **out )
{
    return dependency_property_create( NULL, (IDependencyProperty **)out );
}
static const void *attached_statics_vtbl[64] =
{
    [0] = uwp_cf_QI,
    [1] = uwp_cf_AddRef,
    [2] = uwp_cf_Release,
    [3] = uwp_cf_GetIids,
    [4] = uwp_cf_GetRuntimeClassName,
    [5] = uwp_cf_GetTrustLevel,
    [6] = attached_prop_get_dp,
    [7 ... 8] = uwp_generic_noop,
    [9] = attached_prop_get_dp,
    [10 ... 11] = uwp_generic_noop,
    [12] = attached_prop_get_dp,
    [13 ... 14] = uwp_generic_noop,
    [15] = attached_prop_get_dp,
    [16 ... 17] = uwp_generic_noop,
    [18] = attached_prop_get_dp,
    [19 ... 20] = uwp_generic_noop,
    [21] = attached_prop_get_dp,
    [22 ... 23] = uwp_generic_noop,
    [24] = attached_prop_get_dp,
    [25 ... 26] = uwp_generic_noop,
    [27] = attached_prop_get_dp,
    [28 ... 29] = uwp_generic_noop,
    [30] = attached_prop_get_dp,
    [31 ... 63] = uwp_generic_noop,
};
struct uwp_class_factory auto_props_factory_obj =
    { {(const struct IActivationFactoryVtbl *)attached_statics_vtbl}, {(const struct IInspectableVtbl *)attached_statics_vtbl}, {(const struct IInspectableVtbl *)attached_statics_vtbl}, L"Windows.UI.Xaml.Automation.AutomationProperties", { NULL } };

static HRESULT WINAPI feap_CreatePeerForElement( void *iface, void *el, void **out )
{
    if (out) *out = NULL;
    return S_OK;
}
static HRESULT WINAPI feap_CreateInstanceWithOwner( void *iface, void *owner, IInspectable *outer,
        IInspectable **inner, void **instance )
{
    return uwp_element_create( L"Windows.UI.Xaml.Automation.Peers.FrameworkElementAutomationPeer",
                               outer, inner, instance );
}
static const void *feap_factory_vtbl[16] =
{
    [0] = uwp_cf_QI,
    [1] = uwp_cf_AddRef,
    [2] = uwp_cf_Release,
    [3] = uwp_cf_GetIids,
    [4] = uwp_cf_GetRuntimeClassName,
    [5] = uwp_cf_GetTrustLevel,
    [6] = feap_CreateInstanceWithOwner,
    [7 ... 15] = uwp_generic_noop,
};
static const void *feap_statics_vtbl[16] =
{
    [0] = uwp_cf_QI,
    [1] = uwp_cf_AddRef,
    [2] = uwp_cf_Release,
    [3] = uwp_cf_GetIids,
    [4] = uwp_cf_GetRuntimeClassName,
    [5] = uwp_cf_GetTrustLevel,
    [6] = feap_CreatePeerForElement,
    [7] = feap_CreatePeerForElement,
    [8 ... 15] = uwp_generic_noop,
};
struct uwp_class_factory feap_factory_obj =
    { {(const struct IActivationFactoryVtbl *)feap_statics_vtbl}, {(const struct IInspectableVtbl *)feap_factory_vtbl}, {(const struct IInspectableVtbl *)feap_statics_vtbl}, L"Windows.UI.Xaml.Automation.Peers.FrameworkElementAutomationPeer", { NULL } };

static HRESULT WINAPI th_FromLengths( void *iface, double left, double top, double right, double bottom, double *out )
{
    if (!out) return E_POINTER;
    out[0] = left;
    out[1] = top;
    out[2] = right;
    out[3] = bottom;
    return S_OK;
}
static HRESULT WINAPI th_FromUniformLength( void *iface, double uniform, double *out )
{
    if (!out) return E_POINTER;
    out[0] = out[1] = out[2] = out[3] = uniform;
    return S_OK;
}
static const void *th_statics_vtbl[16] =
{
    [0] = uwp_cf_QI,
    [1] = uwp_cf_AddRef,
    [2] = uwp_cf_Release,
    [3] = uwp_cf_GetIids,
    [4] = uwp_cf_GetRuntimeClassName,
    [5] = uwp_cf_GetTrustLevel,
    [6] = th_FromLengths,
    [7] = th_FromUniformLength,
    [8 ... 15] = uwp_generic_noop,
};
struct uwp_class_factory th_factory_obj =
    { {(const struct IActivationFactoryVtbl *)th_statics_vtbl}, {(const struct IInspectableVtbl *)th_statics_vtbl}, {(const struct IInspectableVtbl *)th_statics_vtbl}, L"Windows.UI.Xaml.ThicknessHelper", { NULL } };

/*
 * Static class -> factory map for the UWP/XAML activation factory.  The classes below
 * resolve to a fixed singleton factory object; the lookup used to be a linear chain of
 * wcscmp over every name, re-run on every activation.  A 32-bucket FNV-1a index over the
 * same (name, factory) pairs turns that into a hash probe.  The index is built once
 * (InitOnceExecuteOnce) from this table, then read-only, so it needs no lock after the
 * first call.  Behavior is identical to the old chain: the first matching name wins, and
 * each entry hands out exactly the factory object the old wcscmp branch returned.
 */

const struct xaml_static_factory xaml_static_factories[] =
{
    { L"Windows.UI.Xaml.ThicknessHelper",                                (IActivationFactory *)&th_factory_obj.IActivationFactory_iface },
    { L"Microsoft.UI.Xaml.ThicknessHelper",                              (IActivationFactory *)&th_factory_obj.IActivationFactory_iface },
    { L"Windows.UI.Xaml.Automation.Peers.FrameworkElementAutomationPeer",(IActivationFactory *)&feap_factory_obj.IActivationFactory_iface },
    { L"Windows.UI.Xaml.Media.SolidColorBrush",                          (IActivationFactory *)&scb_factory_obj.IActivationFactory_iface },
    { L"Windows.UI.Xaml.Markup.XamlBindingHelper",                       (IActivationFactory *)&xbh_factory_obj },
    { L"Microsoft.UI.Xaml.Markup.XamlBindingHelper",                     (IActivationFactory *)&xbh_factory_obj },
    { L"Windows.UI.Xaml.Markup.XamlReader",                              (IActivationFactory *)&xbh_factory_obj },
    { L"Microsoft.UI.Xaml.Markup.XamlReader",                            (IActivationFactory *)&xbh_factory_obj },
    { L"Windows.UI.Xaml.VisualStateManager",                             (IActivationFactory *)&vsm_factory_obj.IActivationFactory_iface },
    { L"Microsoft.UI.Xaml.VisualStateManager",                           (IActivationFactory *)&vsm_factory_obj.IActivationFactory_iface },
    { L"Windows.UI.Xaml.Media.VisualTreeHelper",                         (IActivationFactory *)&vth_factory_obj.IActivationFactory_iface },
    { L"Microsoft.UI.Xaml.Media.VisualTreeHelper",                       (IActivationFactory *)&vth_factory_obj.IActivationFactory_iface },
    { L"Windows.UI.Xaml.Automation.AutomationProperties",                (IActivationFactory *)&auto_props_factory_obj.IActivationFactory_iface },
    { L"Windows.UI.Xaml.Controls.ToolTipService",                        (IActivationFactory *)&auto_props_factory_obj.IActivationFactory_iface },
    { L"Windows.UI.Xaml.Media.Animation.Storyboard",                     (IActivationFactory *)&sb_factory_obj.IActivationFactory_iface },
    { L"Microsoft.UI.Xaml.Media.Animation.Storyboard",                   (IActivationFactory *)&sb_factory_obj.IActivationFactory_iface },
};
const size_t xaml_static_factories_count = ARRAY_SIZE( xaml_static_factories );


const struct xaml_static_factory *xaml_factory_hash[XAML_FACTORY_HASH_SIZE];
INIT_ONCE xaml_factory_hash_once = INIT_ONCE_STATIC_INIT;

BOOL uwp_unbox_enum_int32( IInspectable *obj, INT32 *out_val, BOOL allow_plain_int )
{
    IPropertyValue *pv = NULL;
    PropertyType pt = PropertyType_Empty;
    void *iref = NULL;

    if (!obj || !out_val) return FALSE;
    if (allow_plain_int && uwp_unbox_int32( obj, out_val )) return TRUE;

    if (SUCCEEDED(IInspectable_QueryInterface( obj, &IID_IPropertyValue_g, (void **)&pv )) && pv)
    {
        IPropertyValue_get_Type( pv, &pt );
        IPropertyValue_Release( pv );
            return FALSE;
    }
    else return FALSE;

    if (SUCCEEDED(IInspectable_QueryInterface( obj, &IID_IReference_NumbersAndOperatorsEnum, &iref )) && iref)
    {
        struct reference_int32 *ref = (struct reference_int32 *)iref;
        INT32 v = 0;
        HRESULT hr = ref->lpVtbl->get_Value ? ref->lpVtbl->get_Value( ref, &v ) : E_FAIL;
        IUnknown_Release( (IUnknown *)iref );
        if (SUCCEEDED(hr))
        {
            *out_val = v;
            return TRUE;
        }
    }

    {
        HSTRING hcls = NULL;
        const WCHAR *cls = NULL;
        ULONG cnt = 0, k;
        IID *iids = NULL;
        if (SUCCEEDED(IInspectable_GetRuntimeClassName( obj, &hcls )) && hcls)
        {
            cls = WindowsGetStringRawBuffer( hcls, NULL );
            if (cls && wcsstr( cls, L"IReference`1<" ) &&
                SUCCEEDED(IInspectable_GetIids( obj, &cnt, &iids )) && iids)
            {
                for (k = 0; k < cnt; k++)
                {
                    if (IsEqualGUID( &iids[k], &IID_IUnknown ) ||
                        IsEqualGUID( &iids[k], &IID_IInspectable ) ||
                        IsEqualGUID( &iids[k], &IID_IAgileObject ) ||
                        IsEqualGUID( &iids[k], &IID_IPropertyValue_g ))
                        continue;
                    iref = NULL;
                    if (SUCCEEDED(IInspectable_QueryInterface( obj, &iids[k], &iref )) && iref)
                    {
                        INT32 v = 0;
                        HRESULT hr = E_FAIL;
                        __TRY
                        {
                            struct reference_int32 *ref = (struct reference_int32 *)iref;
                            if (ref->lpVtbl->get_Value) hr = ref->lpVtbl->get_Value( ref, &v );
                        }
                        __EXCEPT_ALL { hr = E_FAIL; }
                        __ENDTRY;
                        IUnknown_Release( (IUnknown *)iref );
                        if (SUCCEEDED(hr))
                        {
                            *out_val = v;
                            CoTaskMemFree( iids );
                            WindowsDeleteString( hcls );
                            return TRUE;
                        }
                    }
                }
                CoTaskMemFree( iids );
            }
            WindowsDeleteString( hcls );
        }
    }
    return FALSE;
}
