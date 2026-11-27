/* WinRT Windows.UI.Xaml Boxed Values & ResourceDictionary Storage / Lookup
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
#include <excpt.h>
#include "wine/exception.h"
#include "private.h"
#include "appmodel.h"
#include "weakreference.h"
#include "winreg.h"
#include "uwp_private.h"
#include "dp_vsm_private.h"
WINE_DEFAULT_DEBUG_CHANNEL(xaml);

static INIT_ONCE g_pvs_once = INIT_ONCE_STATIC_INIT;
static IPropertyValueStatics *g_pvs_singleton = NULL;

static BOOL WINAPI init_pvs_once( INIT_ONCE *once, void *param, void **ctx )
{
    IActivationFactory *fac = NULL;
    HSTRING hcls = NULL;

    if (SUCCEEDED(WindowsCreateString( L"Windows.Foundation.PropertyValue", 32, &hcls )) && hcls)
    {
        if (SUCCEEDED(RoGetActivationFactory( hcls, &IID_IActivationFactory, (void **)&fac )) && fac)
        {
            IActivationFactory_QueryInterface( fac, &IID_IPropertyValueStatics, (void **)&g_pvs_singleton );
            IActivationFactory_Release( fac );
        }
        WindowsDeleteString( hcls );
    }
    return g_pvs_singleton != NULL;
}

IPropertyValueStatics *uwp_get_property_value_statics( void )
{
    InitOnceExecuteOnce( &g_pvs_once, init_pvs_once, NULL, NULL );
    return g_pvs_singleton;
}

IInspectable *box_double_value( double d )
{
    IPropertyValueStatics *pvs = uwp_get_property_value_statics();
    IInspectable *out = NULL;

    if (pvs) IPropertyValueStatics_CreateDouble( pvs, d, &out );
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
static const GUID IID_IReference_CornerRadius_UWP =
    { 0x96d922e6, 0xa7ca, 0x5c21, { 0xb9, 0xf7, 0xe4, 0x50, 0x4e, 0x8c, 0x71, 0x12 } };

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
        IsEqualGUID( iid, &IID_IReference_Thickness_UWP ) ||
        IsEqualGUID( iid, &IID_IReference_CornerRadius_UWP ))
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
    IPropertyValueStatics *pvs = uwp_get_property_value_statics();
    IInspectable *out = NULL;
    HSTRING hval = NULL;

    if (!str) str = L"";
    if (pvs)
    {
        WindowsCreateString( str, wcslen(str), &hval );
        IPropertyValueStatics_CreateString( pvs, hval, &out );
        if (hval) WindowsDeleteString( hval );
    }
    return out;
}

IInspectable *box_int32_value( INT32 v )
{
    IPropertyValueStatics *pvs = uwp_get_property_value_statics();
    IInspectable *out = NULL;

    if (pvs) IPropertyValueStatics_CreateInt32( pvs, v, &out );
    return out;
}

IInspectable *box_bool_value( boolean b )
{
    IPropertyValueStatics *pvs = uwp_get_property_value_statics();
    IInspectable *out = NULL;

    if (pvs) IPropertyValueStatics_CreateBoolean( pvs, b, &out );
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
        if (SUCCEEDED(IPropertyValue_get_Type( pv, &pt )))
        {
            if (pt == PropertyType_String && SUCCEEDED(IPropertyValue_GetString( pv, &hs )) && hs)
            {
                const WCHAR *raw = WindowsGetStringRawBuffer( hs, NULL );
                if (raw) lstrcpynW( buf, raw, buf_chars );
                WindowsDeleteString( hs );
            }
            else if (pt == PropertyType_Char16 && buf_chars >= 2)
            {
                WCHAR ch = 0;
                if (SUCCEEDED(IPropertyValue_GetChar16( pv, &ch )) && ch)
                {
                    buf[0] = ch;
                    buf[1] = 0;
                }
            }
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

BOOL uwp_unbox_bool( void *obj, boolean *out_val )
{
    static const GUID IID_IReference_Boolean =
        { 0x3c00fd60, 0x2950, 0x5939, { 0xa2, 0x1a, 0x2d, 0x12, 0xc5, 0xa0, 0x1b, 0x8a } };
    IPropertyValue *pv = NULL;
    void *iref = NULL;
    if (!obj || !out_val) return FALSE;
    if (SUCCEEDED(IUnknown_QueryInterface( (IUnknown *)obj, &IID_IReference_Boolean, &iref )) && iref)
    {
        const struct abi_iref_bool *air = (const struct abi_iref_bool *)iref;
        HRESULT hr = air->lpVtbl->get_Value ? air->lpVtbl->get_Value( iref, out_val ) : E_FAIL;
        IUnknown_Release( (IUnknown *)iref );
        if (SUCCEEDED(hr)) return TRUE;
    }
    if (SUCCEEDED(IUnknown_QueryInterface( (IUnknown *)obj, &IID_IPropertyValue, (void **)&pv )) && pv)
    {
        HRESULT hr = IPropertyValue_GetBoolean( pv, out_val );
        IPropertyValue_Release( pv );
        return SUCCEEDED(hr);
    }
    return FALSE;
}

BOOL uwp_unbox_double( void *obj, double *out_val )
{
    IPropertyValue *pv = NULL;
    if (!obj || !out_val) return FALSE;
    if (SUCCEEDED(IUnknown_QueryInterface( (IUnknown *)obj, &IID_IPropertyValue, (void **)&pv )) && pv)
    {
        HRESULT hr = IPropertyValue_GetDouble( pv, out_val );
        if (FAILED(hr))
        {
            float f = 0.0f;
            hr = IPropertyValue_GetSingle( pv, &f );
            if (SUCCEEDED(hr)) *out_val = (double)f;
        }
        IPropertyValue_Release( pv );
        return SUCCEEDED(hr);
    }
    return FALSE;
}

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
        if (pt != PropertyType_OtherType)
            return FALSE;
    }
    else return FALSE;

    {
        static const GUID IID_IStringable_local =
            { 0x96369f54, 0x8eb6, 0x48f0, { 0xab, 0xce, 0xc1, 0xb2, 0x11, 0xe6, 0x27, 0xc3 } };
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
                        IsEqualGUID( &iids[k], &IID_IMarshal ) ||
                        IsEqualGUID( &iids[k], &IID_IWeakReferenceSource ) ||
                        IsEqualGUID( &iids[k], &IID_IStringable_local ) ||
                        IsEqualGUID( &iids[k], &IID_IPropertyValue_g ))
                        continue;
                    iref = NULL;
                    if (SUCCEEDED(IInspectable_QueryInterface( obj, &iids[k], &iref )) && iref)
                    {
                        UINT64 raw_v = 0;
                        HRESULT hr = E_FAIL;
                        __TRY
                        {
                            struct reference_int32 *ref = (struct reference_int32 *)iref;
                            if (ref->lpVtbl->get_Value) hr = ref->lpVtbl->get_Value( ref, (INT32 *)&raw_v );
                        }
                        __EXCEPT_ALL { hr = E_FAIL; }
                        __ENDTRY;
                        IUnknown_Release( (IUnknown *)iref );
                        if (SUCCEEDED(hr))
                        {
                            *out_val = (INT32)raw_v;
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

/* ============================================================================
 * 3. ResourceDictionary Key-Value Store (Scoped Chain & XBF Resource Parsing)
 * ============================================================================ */

static void uwp_set_brush_element_argb( IInspectable *br, UINT32 argb )
{
    struct uwp_element *el = uwp_elem_from_any( br );
    struct uwp_style_anim_ext *sa = uwp_elem_ensure_style_anim( el );
    if (sa)
    {
        sa->brush_argb = argb;
        sa->has_brush_argb = TRUE;
        sa->brush_opacity = (double)((argb >> 24) & 0xff) / 255.0;
    }
}

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

struct uwp_rd_bmap_slot
{
    void *key;
    struct uwp_rd_bucket *b;
};

static struct uwp_rd_bmap_slot *g_rd_bmap = NULL;
static UINT32 g_rd_bmap_cap = 0;
static UINT32 g_rd_bmap_cnt = 0;

static inline UINT32 rd_hash_ptr( const void *p )
{
    ULONG_PTR v = (ULONG_PTR)p;
    v = (v >> 3) ^ (v >> 17);
    return (UINT32)(v * 2654435761u);
}

static inline UINT32 rd_hash_wstr( const WCHAR *s )
{
    UINT32 h = 2166136261u;
    for (; *s; s++)
    {
        h ^= (UINT32)*s;
        h *= 16777619u;
    }
    return h;
}

static void rd_bmap_grow( void )
{
    UINT32 ncap = g_rd_bmap_cap ? g_rd_bmap_cap * 2 : 256;
    struct uwp_rd_bmap_slot *nt = calloc( ncap, sizeof(*nt) );
    UINT32 mask = ncap - 1, i;
    if (!nt) return;
    if (g_rd_bmap)
    {
        for (i = 0; i < g_rd_bmap_cap; i++)
        {
            if (g_rd_bmap[i].key)
            {
                UINT32 idx = rd_hash_ptr( g_rd_bmap[i].key ) & mask;
                while (nt[idx].key) idx = (idx + 1) & mask;
                nt[idx] = g_rd_bmap[i];
            }
        }
        free( g_rd_bmap );
    }
    g_rd_bmap = nt;
    g_rd_bmap_cap = ncap;
}

static void rd_bmap_insert( void *key, struct uwp_rd_bucket *b )
{
    UINT32 mask, idx;
    if (!g_rd_bmap || (g_rd_bmap_cnt + 1) * 100 >= g_rd_bmap_cap * 65)
        rd_bmap_grow();
    if (!g_rd_bmap) return;
    mask = g_rd_bmap_cap - 1;
    idx = rd_hash_ptr( key ) & mask;
    while (g_rd_bmap[idx].key && g_rd_bmap[idx].key != key)
        idx = (idx + 1) & mask;
    if (!g_rd_bmap[idx].key) g_rd_bmap_cnt++;
    g_rd_bmap[idx].key = key;
    g_rd_bmap[idx].b = b;
}

struct uwp_rd_bucket *get_rd_bucket( void *rd_key, BOOL create )
{
    struct uwp_rd_bucket *b = NULL;
    if (!rd_key) return NULL;
    if (g_rd_bmap && g_rd_bmap_cap)
    {
        UINT32 mask = g_rd_bmap_cap - 1;
        UINT32 idx = rd_hash_ptr( rd_key ) & mask;
        while (g_rd_bmap[idx].key)
        {
            if (g_rd_bmap[idx].key == rd_key) return g_rd_bmap[idx].b;
            idx = (idx + 1) & mask;
        }
    }
    if (!create) return NULL;
    b = calloc( 1, sizeof(*b) );
    if (!b) return NULL;
    b->rd_key = rd_key;
    b->next = g_rd_buckets_head;
    g_rd_buckets_head = b;
    rd_bmap_insert( rd_key, b );
    return b;
}

static void rd_bucket_rehash( struct uwp_rd_bucket *b, int new_size )
{
    int *ns = calloc( (size_t)new_size, sizeof(*ns) );
    int mask = new_size - 1, i;
    if (!ns) return;
    for (i = 0; i < b->count; i++)
    {
        UINT32 idx = rd_hash_wstr( b->entries[i].key ) & (UINT32)mask;
        while (ns[idx]) idx = (idx + 1) & (UINT32)mask;
        ns[idx] = i + 1;
    }
    free( b->hash_slots );
    b->hash_slots = ns;
    b->hash_mask = mask;
}

static struct uwp_rd_entry *rd_bucket_find_local( struct uwp_rd_bucket *b, const WCHAR *key )
{
    if (!b || b->count <= 0) return NULL;
    if (b->hash_slots)
    {
        UINT32 mask = (UINT32)b->hash_mask;
        UINT32 idx = rd_hash_wstr( key ) & mask;
        int slot;
        while ((slot = b->hash_slots[idx]) != 0)
        {
            if (!wcscmp( b->entries[slot - 1].key, key ))
                return &b->entries[slot - 1];
            idx = (idx + 1) & mask;
        }
        return NULL;
    }
    {
        int i;
        for (i = 0; i < b->count; i++)
            if (!wcscmp( b->entries[i].key, key ))
                return &b->entries[i];
    }
    return NULL;
}

struct uwp_rd_entry *rd_bucket_upsert( struct uwp_rd_bucket *b, const WCHAR *key )
{
    WCHAR kbuf[96];
    struct uwp_rd_entry *existing;
    if (!b || !key || !key[0]) return NULL;
    lstrcpynW( kbuf, key, ARRAY_SIZE(kbuf) );
    existing = rd_bucket_find_local( b, kbuf );
    if (existing) return existing;
    if (b->count >= b->cap)
    {
        int ncap = b->cap ? b->cap * 2 : 32;
        struct uwp_rd_entry *ne = realloc( b->entries, ncap * sizeof(*ne) );
        if (!ne) return NULL;
        b->entries = ne;
        b->cap = ncap;
    }
    memset( &b->entries[b->count], 0, sizeof(b->entries[0]) );
    lstrcpynW( b->entries[b->count].key, kbuf, 96 );
    b->entries[b->count].opacity = 1.0;
    b->count++;
    if (!b->hash_slots || b->count * 10 >= (b->hash_mask + 1) * 6)
    {
        int nsz = b->hash_slots ? (b->hash_mask + 1) * 2 : 64;
        while (b->count * 10 >= nsz * 6) nsz *= 2;
        rd_bucket_rehash( b, nsz );
    }
    else
    {
        UINT32 mask = (UINT32)b->hash_mask;
        UINT32 idx = rd_hash_wstr( kbuf ) & mask;
        while (b->hash_slots[idx]) idx = (idx + 1) & mask;
        b->hash_slots[idx] = b->count;
    }
    return &b->entries[b->count - 1];
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

static void uwp_rd_insert( void *rd_key, const WCHAR *key, IInspectable *val )
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
        if (!_stricmp( g_loaded_res_xbfs[i], resolved_path ))
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
            typedef LONG (WINAPI *p_RegCloseKey_t)( HKEY );
            p_RegOpenKeyExW_t p_open = (p_RegOpenKeyExW_t)GetProcAddress( reg_mod, "RegOpenKeyExW" );
            p_RegQueryValueExW_t p_query = (p_RegQueryValueExW_t)GetProcAddress( reg_mod, "RegQueryValueExW" );
            p_RegCloseKey_t p_close = (p_RegCloseKey_t)GetProcAddress( reg_mod, "RegCloseKey" );
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

    /* 2b. Generically load package and sub-library Themes/Generic.xbf files into theme_b */
    if (theme_b)
    {
        DWORD k;
        const char *path = NULL;
        const BYTE *data = NULL;
        DWORD sz = 0;
        for (k = 0; uwp_pri_get_cat_entry( k, &path, &data, &sz ); k++)
        {
            size_t plen = path ? strlen( path ) : 0;
            BOOL is_generic = FALSE;
            if (plen >= 19 && !_stricmp( path + plen - 19, "/Themes/Generic.xbf" ))
                is_generic = TRUE;
            else if (plen >= 19 && !_stricmp( path + plen - 19, "\\Themes\\Generic.xbf" ))
                is_generic = TRUE;
            else if (plen == 18 && !_stricmp( path, "Themes/Generic.xbf" ))
                is_generic = TRUE;
            if (is_generic && data && sz > 0 && uwp_mark_res_xbf_loaded( path ))
            {
                TRACE( "xbf-res: loading generic theme %s (%lu bytes)\n", path, (unsigned long)sz );
                uwp_parse_xbf_into_rd( data, sz, path, theme_b );
            }
        }
    }

    /* 3. Load WinUI / system theme XBF into theme defaults bucket */
    if (theme_b)
    {
        uwp_load_res_xbf_into_bucket( NULL, L"Microsoft.UI.Xaml/Themes/21h1_themeresources.xbf", theme_b );
        uwp_load_res_xbf_into_bucket( NULL, L"Microsoft.UI.Xaml/Themes/21h1_generic.xbf", theme_b );
        if (theme_b->count == 0 && !theme_b->theme_default && !theme_b->theme_dark && !theme_b->theme_light)
            uwp_load_res_xbf_into_bucket( NULL, L"Microsoft.UI.Xaml/Themes/rs5_themeresources.xbf", theme_b );
    }

    g_app_res_loaded = TRUE;
    g_app_res_loading = FALSE;
}

BOOL uwp_is_light_theme( struct uwp_element *scope_el )
{
    static BOOL sys_theme_init = FALSE;
    static BOOL sys_is_light = FALSE;
    struct uwp_element *cur;
    int hops = 0;

    for (cur = scope_el; cur && hops < 16; cur = cur->parent, hops++)
    {
        if (cur->requested_theme == 1) return TRUE;  /* ElementTheme_Light */
        if (cur->requested_theme == 2) return FALSE; /* ElementTheme_Dark */
    }
    if (g_app_requested_theme == 0) return TRUE;  /* ApplicationTheme_Light */
    if (g_app_requested_theme == 1) return FALSE; /* ApplicationTheme_Dark */

    if (!sys_theme_init)
    {
        HMODULE reg_mod = GetModuleHandleW( L"kernelbase.dll" );
        sys_theme_init = TRUE;
        sys_is_light = FALSE;
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
                DWORD val = 0, type = 0, cb = sizeof(val);
                if (p_open( HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                            0, KEY_READ, &hk ) == ERROR_SUCCESS)
                {
                    if (p_query( hk, L"AppsUseLightTheme", NULL, &type, (BYTE *)&val, &cb ) == ERROR_SUCCESS &&
                        type == REG_DWORD)
                        sys_is_light = (val != 0);
                    p_close( hk );
                }
            }
        }
    }
    return sys_is_light;
}

static BOOL uwp_lookup_in_bucket( struct uwp_rd_bucket *b, const WCHAR *key, BOOL prefer_light, int depth,
                                  struct uwp_rd_entry **out_entry, struct uwp_rd_bucket **out_bucket )
{
    int i;
    struct uwp_rd_bucket *primary_theme, *secondary_theme;
    if (!b || !key || !key[0] || depth > 12) return FALSE;
    TRACE( "uwp_lookup_in_bucket: b=%p count=%d merged_cnt=%d light=%d key=%s\n",
           b, b->count, b->merged_cnt, prefer_light, debugstr_w(key) );

    {
        struct uwp_rd_entry *hit = rd_bucket_find_local( b, key );
        if (hit)
        {
            *out_entry = hit;
            if (out_bucket) *out_bucket = b;
            return TRUE;
        }
    }
    primary_theme = prefer_light ? b->theme_light : b->theme_dark;
    secondary_theme = prefer_light ? b->theme_dark : b->theme_light;
    if (primary_theme && uwp_lookup_in_bucket( primary_theme, key, prefer_light, depth + 1, out_entry, out_bucket ))
        return TRUE;
    if (b->theme_default && uwp_lookup_in_bucket( b->theme_default, key, prefer_light, depth + 1, out_entry, out_bucket ))
        return TRUE;
    for (i = b->merged_cnt - 1; i >= 0; i--)
    {
        if (uwp_lookup_in_bucket( b->merged[i], key, prefer_light, depth + 1, out_entry, out_bucket ))
            return TRUE;
    }
    if (secondary_theme && uwp_lookup_in_bucket( secondary_theme, key, prefer_light, depth + 1, out_entry, out_bucket ))
        return TRUE;
    return FALSE;
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
    BOOL prefer_light;

    if (!raw_key || !raw_key[0] || !out_entry || depth > 14) return FALSE;
    *out_entry = NULL;
    if (out_bucket) *out_bucket = NULL;

    uwp_extract_resource_key( raw_key, key, ARRAY_SIZE(key) );
    if (!key[0]) return FALSE;
    prefer_light = uwp_is_light_theme( scope_el ? scope_el : (start_bucket ? start_bucket->owner_el : NULL) );

    if (start_bucket)
    {
        if (vis_cnt < 32) visited[vis_cnt++] = start_bucket;
        if (uwp_lookup_in_bucket( start_bucket, key, prefer_light, 0, &e, &found_b ))
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
                if (uwp_lookup_in_bucket( cb, key, prefer_light, 0, &e, &found_b ))
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
            if (!seen && uwp_lookup_in_bucket( ob, key, prefer_light, 0, &e, &found_b ))
                goto found;
        }
    }

    /* App-level Resources (App.xbf + registry Resources_xbf + merged/theme) wins over theme defaults */
    {
        struct uwp_rd_bucket *app_b = get_rd_bucket( &g_app_rd_sentinel, FALSE );
        BOOL ok = (app_b && uwp_lookup_in_bucket( app_b, key, prefer_light, 0, &e, &found_b ));
        TRACE( "uwp_rd_find_entry: app_b=%p ok=%d key=%s\n", app_b, ok, debugstr_w(key) );
        if (ok) goto found;
    }

    /* Theme defaults (WinUI themeresources.xbf + Generic.xbf) */
    {
        struct uwp_rd_bucket *theme_b = get_rd_bucket( &g_theme_rd_sentinel, FALSE );
        if (theme_b && uwp_lookup_in_bucket( theme_b, key, prefer_light, 0, &e, &found_b ))
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

/* Documented system/Fluent Light & Dark theme fallback ONLY with TRACE when not found in element/page/app/WinUI dicts */
static BOOL uwp_theme_default_fallback( const WCHAR *key, BOOL is_light, UINT32 *out_argb )
{
    static const struct { const WCHAR *name; UINT32 dark_argb; UINT32 light_argb; } defaults[] =
    {
        { L"ApplicationPageBackgroundThemeBrush",       0xff1f1f1fu, 0xfff3f3f3u },
        { L"SystemControlPageBackgroundChromeLowBrush", 0xff1f1f1fu, 0xfff3f3f3u },
        { L"SolidBackgroundFillColorBaseBrush",         0xff202020u, 0xfff3f3f3u },
        { L"SolidBackgroundFillColorTertiaryBrush",     0xff2c2c2cu, 0xfff9f9f9u },
        { L"SurfaceStrokeColorFlyoutBrush",             0xff525252u, 0x19000000u },
        { L"SystemAltHighColor",                        0xff000000u, 0xffffffffu },
        { L"SystemAltMediumHighColor",                  0xcc000000u, 0xccffffffu },
        { L"SystemAltMediumColor",                      0x99000000u, 0x99ffffffu },
        { L"SystemAltMediumLowColor",                   0x66000000u, 0x66ffffffu },
        { L"SystemAltLowColor",                         0x33000000u, 0x33ffffffu },
        { L"SystemBaseHighColor",                       0xffffffffu, 0xff000000u },
        { L"SystemBaseMediumHighColor",                 0xccffffffu, 0xcc000000u },
        { L"SystemBaseMediumColor",                     0x99ffffffu, 0x99000000u },
        { L"SystemBaseMediumLowColor",                  0x66ffffffu, 0x66000000u },
        { L"SystemBaseLowColor",                        0x33ffffffu, 0x33000000u },
        { L"SystemChromeWhiteColor",                    0xffffffffu, 0xffffffffu },
        { L"SystemChromeBlackLowColor",                 0xff171717u, 0xfff2f2f2u },
        { L"SystemChromeMediumLowColor",                0xff2b2b2bu, 0xfff2f2f2u },
        { L"SystemListLowColor",                        0x19ffffffu, 0x19000000u },
        { L"SystemColorWindowColor",                    0xff202020u, 0xffffffffu },
        { L"SystemColorWindowTextColor",                0xffffffffu, 0xff1a1a1au },
        { L"SystemColorButtonFaceColor",                0xff323232u, 0xfff0f0f0u },
        { L"DefaultTextForegroundThemeBrush",           0xffffffffu, 0xff1a1a1au },
        { L"TextFillColorPrimaryBrush",                 0xffffffffu, 0xff1a1a1au },
        { L"TextFillColorSecondaryBrush",               0xffc5c5c5u, 0xff5c5c5cu },
        { L"TextFillColorDisabledBrush",                0x5dffffffu, 0x5c000000u },
        { L"TextOnAccentFillColorPrimaryBrush",         0xffffffffu, 0xffffffffu },
        { L"PrimaryForegroundBrush",                    0xffffffffu, 0xff1a1a1au },
        { L"SecondaryForegroundBrush",                  0xffb4b4b4u, 0xff5c5c5cu },
        { L"ControlFillColorDefaultBrush",              0x18ffffffu, 0xb3ffffffu },
        { L"ControlFillColorSecondaryBrush",            0x22ffffffu, 0x80f9f9f9u },
        { L"ControlStrokeColorDefaultBrush",            0x18ffffffu, 0x19000000u },
        { L"CardBackgroundFillColorDefaultBrush",       0x0dffffffu, 0xb3ffffffu },
        { L"SystemAccentColorLight1",                   0xff2b88d8u, 0xff2b88d8u },
        { L"SystemAccentColorLight2",                   0xff4cc2ffu, 0xff4cc2ffu },
        { L"SystemAccentColorLight3",                   0xff99ebffu, 0xff99ebffu },
        { L"SystemAccentColorDark1",                    0xff005a9eu, 0xff005a9eu },
        { L"SystemAccentColorDark2",                    0xff004578u, 0xff004578u },
        { L"SystemAccentColorDark3",                    0xff002642u, 0xff002642u },
        { L"AccentTextThemeResource",                   0xff4cc2ffu, 0xff0067c0u },
        { L"SystemControlForegroundAccentBrush",        0xff4cc2ffu, 0xff0067c0u },
        { L"AccentTextFillColorPrimaryBrush",           0xff4cc2ffu, 0xff0067c0u },
        { L"AccentButtonBackground",                    0xff0078d4u, 0xff0067c0u },
        { L"AccentButtonForeground",                    0xffffffffu, 0xffffffffu },
    };
    size_t i;
    if (!key || !key[0] || !out_argb) return FALSE;
    if (!wcscmp( key, L"SystemAccentColor" ) ||
        !wcscmp( key, L"AccentFillColorDefaultBrush" ) ||
        !wcscmp( key, L"SystemControlHighlightAccentBrush" ) ||
        !wcscmp( key, L"AccentBrush" ))
    {
        *out_argb = is_light ? 0xff0067c0u : uwp_get_system_accent_argb();
        TRACE( "xbf-res: theme default fallback key=%s light=%d -> #%08x\n", debugstr_w(key), is_light, *out_argb );
        return TRUE;
    }
    for (i = 0; i < ARRAY_SIZE(defaults); i++)
    {
        if (!wcscmp( key, defaults[i].name ))
        {
            *out_argb = is_light ? defaults[i].light_argb : defaults[i].dark_argb;
            TRACE( "xbf-res: theme default fallback key=%s light=%d -> #%08x\n", debugstr_w(key), is_light, *out_argb );
            return TRUE;
        }
    }
    return FALSE;
}

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

    if (!scope_el || !scope_el->is_accent_style)
    {
        if (!wcscmp( key, L"ButtonBackground" ) || !wcscmp( key, L"DropDownButtonBackground" ))
            lstrcpynW( key, L"ControlFillColorDefaultBrush", ARRAY_SIZE(key) );
        else if (!wcscmp( key, L"ButtonForeground" ) || !wcscmp( key, L"DropDownButtonForeground" ))
            lstrcpynW( key, L"TextFillColorPrimaryBrush", ARRAY_SIZE(key) );
        else if (!wcscmp( key, L"ButtonBorderBrush" ) || !wcscmp( key, L"DropDownButtonBorderBrush" ))
            lstrcpynW( key, L"ControlStrokeColorDefaultBrush", ARRAY_SIZE(key) );
    }

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

    return uwp_theme_default_fallback( key, uwp_is_light_theme( scope_el ), out_argb );
}

IInspectable *uwp_rd_materialize_entry( struct uwp_rd_bucket *b, struct uwp_rd_entry *e )
{
    if (!e) return NULL;
    if (e->value) return e->value;
    if (e->kind == UWP_RD_DOUBLE)
        e->value = (e->tid == XBF_TYPE_INT32) ? box_int32_value( (INT32)e->dval ) : box_double_value( e->dval );
    else if (e->kind == UWP_RD_THICKNESS)
        e->value = box_thickness_value( e->vec4 );
    else if (e->kind == UWP_RD_STRING)
        e->value = box_string_value( e->str_val );
    else if (e->kind == UWP_RD_FONT_FAMILY)
    {
        void *ff = NULL;
        if (SUCCEEDED(uwp_element_create( L"Windows.UI.Xaml.Media.FontFamily", NULL, NULL, &ff )) && ff)
        {
            struct uwp_element *ff_el = uwp_elem_from_any( ff );
            if (ff_el && e->str_val[0])
            {
                WCHAR face[64] = {0};
                uwp_elem_set_font_family( ff_el, e->str_val );
                uwp_resolve_font_family( e->str_val, face, ARRAY_SIZE(face) );
            }
            e->value = (IInspectable *)ff;
        }
    }
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
                struct uwp_style_anim_ext *sa = uwp_elem_ensure_style_anim( st_el );
                lstrcpynW( st_el->name, e->key, ARRAY_SIZE(st_el->name) );
                if (sa) lstrcpynW( sa->style_based_on, e->str_val, ARRAY_SIZE(sa->style_based_on) );
                st_el->template_xbf_data = e->template_xbf_data;
                st_el->template_xbf_size = e->template_xbf_size;
                st_el->template_sub_idx = e->template_sub_idx;
                if (sa && e->setter_cnt > 0 && e->setters)
                {
                    sa->style_setters = malloc( (size_t)e->setter_cnt * sizeof(*e->setters) );
                    if (sa->style_setters)
                    {
                        memcpy( sa->style_setters, e->setters, (size_t)e->setter_cnt * sizeof(*e->setters) );
                        sa->style_setter_cnt = e->setter_cnt;
                        sa->style_setter_cap = e->setter_cnt;
                    }
                }
            }
            e->value = (IInspectable *)st;
        }
    }
    else if (e->kind == UWP_RD_DATA_TEMPLATE)
    {
        IInspectable *dt = NULL;
        if (SUCCEEDED(uwp_element_create( L"Windows.UI.Xaml.DataTemplate", NULL, NULL, (void **)&dt )) && dt)
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
        if (uwp_theme_default_fallback( key_str, uwp_is_light_theme( b ? b->owner_el : NULL ), &argb ))
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

    /* Synthesize Style / Brush / MenuFlyout if requested by name from Resources */
    if (wcsstr( key_str, L"Style" ))
    {
        void *st = NULL;
        if (SUCCEEDED(uwp_element_create( L"Windows.UI.Xaml.Style", NULL, NULL, &st )) && st)
        {
            struct uwp_element *st_el = uwp_elem_from_any( st );
            if (st_el) lstrcpynW( st_el->name, key_str, ARRAY_SIZE(st_el->name) );
            uwp_rd_insert( rd_key, key_str, (IInspectable *)st );
            *out_val = st;
            return S_OK;
        }
    }
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
