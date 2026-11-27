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
    BOOL is_corner_radius;
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
        (!impl->is_corner_radius && IsEqualGUID( iid, &IID_IReference_Thickness_UWP )) ||
        (impl->is_corner_radius && IsEqualGUID( iid, &IID_IReference_CornerRadius_UWP )))
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
    static const WCHAR th_cls[] = L"Windows.Foundation.IReference`1<Windows.UI.Xaml.Thickness>";
    static const WCHAR cr_cls[] = L"Windows.Foundation.IReference`1<Windows.UI.Xaml.CornerRadius>";
    struct uwp_boxed_thickness *impl = CONTAINING_RECORD( iface, struct uwp_boxed_thickness, IInspectable_iface );
    if (impl->is_corner_radius)
        return WindowsCreateString( cr_cls, ARRAY_SIZE(cr_cls) - 1, name );
    return WindowsCreateString( th_cls, ARRAY_SIZE(th_cls) - 1, name );
}

static HRESULT WINAPI uwp_bth_pv_GetRuntimeClassName( IInspectable *iface, HSTRING *name )
{
    struct uwp_boxed_thickness *impl = CONTAINING_RECORD( iface, struct uwp_boxed_thickness, IPropertyValue_iface );
    return uwp_bth_GetRuntimeClassName( &impl->IInspectable_iface, name );
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
    uwp_bth_pv_GetRuntimeClassName,
    uwp_bth_GetTrustLevel,
    uwp_bth_pv_get_Type,
    uwp_bth_pv_get_IsNumericScalar,
};

static IInspectable *box_thickness_value( const double vec4[4], BOOL is_corner_radius )
{
    struct uwp_boxed_thickness *impl = calloc( 1, sizeof(*impl) );
    if (!impl) return NULL;
    impl->IInspectable_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_bth_vtbl;
    impl->IPropertyValue_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_bth_pv_vtbl;
    impl->ref = 1;
    impl->is_corner_radius = is_corner_radius;
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

static SRWLOCK g_rd_bmap_lock = SRWLOCK_INIT;
static struct uwp_rd_bmap_slot *g_rd_bmap = NULL;
static UINT32 g_rd_bmap_cap = 0;
static UINT32 g_rd_bmap_cnt = 0;
static SRWLOCK g_loaded_res_xbfs_lock = SRWLOCK_INIT;

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

static struct uwp_rd_bucket *rd_bmap_find_locked( void *rd_key )
{
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
    return NULL;
}

struct uwp_rd_bucket *get_rd_bucket( void *rd_key, BOOL create )
{
    struct uwp_rd_bucket *b = NULL;
    if (!rd_key) return NULL;
    AcquireSRWLockShared( &g_rd_bmap_lock );
    b = rd_bmap_find_locked( rd_key );
    ReleaseSRWLockShared( &g_rd_bmap_lock );
    if (b || !create) return b;

    AcquireSRWLockExclusive( &g_rd_bmap_lock );
    b = rd_bmap_find_locked( rd_key );
    if (!b)
    {
        b = calloc( 1, sizeof(*b) );
        if (b)
        {
            b->rd_key = rd_key;
            b->next = g_rd_buckets_head;
            g_rd_buckets_head = b;
            rd_bmap_insert( rd_key, b );
        }
    }
    ReleaseSRWLockExclusive( &g_rd_bmap_lock );
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

static BOOL uwp_mark_res_xbf_loaded( const char *resolved_path )
{
    int i;
    if (!resolved_path || !resolved_path[0]) return FALSE;
    AcquireSRWLockExclusive( &g_loaded_res_xbfs_lock );
    for (i = 0; i < g_loaded_res_xbf_cnt; i++)
    {
        if (!_stricmp( g_loaded_res_xbfs[i], resolved_path ))
        {
            ReleaseSRWLockExclusive( &g_loaded_res_xbfs_lock );
            return FALSE;
        }
    }
    if (xaml_vec_grow( (void **)&g_loaded_res_xbfs, &g_loaded_res_xbf_cap,
                       g_loaded_res_xbf_cnt + 1, sizeof(*g_loaded_res_xbfs), 128 ))
    {
        snprintf( g_loaded_res_xbfs[g_loaded_res_xbf_cnt++], 256, "%s", resolved_path );
    }
    ReleaseSRWLockExclusive( &g_loaded_res_xbfs_lock );
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

static UINT32 uwp_get_system_accent_argb( void )
{
    static UINT32 cached_accent = 0;
    static BOOL accent_init = FALSE;
    if (!accent_init)
    {
        HKEY hk = NULL;
        DWORD abgr = 0, type = 0, cb = sizeof(abgr);
        accent_init = TRUE;
        cached_accent = 0xff0078d4u; /* Windows Fluent default blue */
        if (RegOpenKeyExW( HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\DWM", 0, KEY_READ, &hk ) == ERROR_SUCCESS)
        {
            if (RegQueryValueExW( hk, L"AccentColor", NULL, &type, (BYTE *)&abgr, &cb ) == ERROR_SUCCESS &&
                type == REG_DWORD && (abgr & 0x00ffffffu))
            {
                BYTE r = (BYTE)(abgr & 0xff);
                BYTE g = (BYTE)((abgr >> 8) & 0xff);
                BYTE b = (BYTE)((abgr >> 16) & 0xff);
                cached_accent = 0xff000000u | ((UINT32)r << 16) | ((UINT32)g << 8) | b;
            }
            RegCloseKey( hk );
        }
    }
    return cached_accent;
}

/* Seed documented Fluent Light & Dark system theme defaults into a dedicated
 * fallback bucket linked at the lowest priority of g_theme_rd_sentinel. */
static void uwp_seed_theme_fallback_buckets( struct uwp_rd_bucket *theme_b )
{
    static int g_theme_fb_sentinel = 0;
    static int g_theme_fb_dark_sentinel = 0;
    static int g_theme_fb_light_sentinel = 0;
    static const struct { const WCHAR *name; enum uwp_rd_kind kind; UINT32 dark_argb; UINT32 light_argb; } defaults[] =
    {
        { L"ApplicationPageBackgroundThemeBrush",       UWP_RD_BRUSH, 0xff1f1f1fu, 0xfff3f3f3u },
        { L"SystemControlPageBackgroundChromeLowBrush", UWP_RD_BRUSH, 0xff1f1f1fu, 0xfff3f3f3u },
        { L"SolidBackgroundFillColorBaseBrush",         UWP_RD_BRUSH, 0xff202020u, 0xfff3f3f3u },
        { L"SolidBackgroundFillColorTertiaryBrush",     UWP_RD_BRUSH, 0xff2c2c2cu, 0xfff9f9f9u },
        { L"SurfaceStrokeColorFlyoutBrush",             UWP_RD_BRUSH, 0xff525252u, 0x19000000u },
        { L"SystemAltHighColor",                        UWP_RD_COLOR, 0xff000000u, 0xffffffffu },
        { L"SystemAltMediumHighColor",                  UWP_RD_COLOR, 0xcc000000u, 0xccffffffu },
        { L"SystemAltMediumColor",                      UWP_RD_COLOR, 0x99000000u, 0x99ffffffu },
        { L"SystemAltMediumLowColor",                   UWP_RD_COLOR, 0x66000000u, 0x66ffffffu },
        { L"SystemAltLowColor",                         UWP_RD_COLOR, 0x33000000u, 0x33ffffffu },
        { L"SystemBaseHighColor",                       UWP_RD_COLOR, 0xffffffffu, 0xff000000u },
        { L"SystemBaseMediumHighColor",                 UWP_RD_COLOR, 0xccffffffu, 0xcc000000u },
        { L"SystemBaseMediumColor",                     UWP_RD_COLOR, 0x99ffffffu, 0x99000000u },
        { L"SystemBaseMediumLowColor",                  UWP_RD_COLOR, 0x66ffffffu, 0x66000000u },
        { L"SystemBaseLowColor",                        UWP_RD_COLOR, 0x33ffffffu, 0x33000000u },
        { L"SystemChromeWhiteColor",                    UWP_RD_COLOR, 0xffffffffu, 0xffffffffu },
        { L"SystemChromeBlackLowColor",                 UWP_RD_COLOR, 0xff171717u, 0xfff2f2f2u },
        { L"SystemChromeMediumLowColor",                UWP_RD_COLOR, 0xff2b2b2bu, 0xfff2f2f2u },
        { L"SystemListLowColor",                        UWP_RD_COLOR, 0x19ffffffu, 0x19000000u },
        { L"SystemColorWindowColor",                    UWP_RD_COLOR, 0xff202020u, 0xffffffffu },
        { L"SystemColorWindowTextColor",                UWP_RD_COLOR, 0xffffffffu, 0xff1a1a1au },
        { L"SystemColorButtonFaceColor",                UWP_RD_COLOR, 0xff323232u, 0xfff0f0f0u },
        { L"DefaultTextForegroundThemeBrush",           UWP_RD_BRUSH, 0xffffffffu, 0xff1a1a1au },
        { L"TextFillColorPrimaryBrush",                 UWP_RD_BRUSH, 0xffffffffu, 0xff1a1a1au },
        { L"TextFillColorSecondaryBrush",               UWP_RD_BRUSH, 0xffc5c5c5u, 0xff5c5c5cu },
        { L"TextFillColorDisabledBrush",                UWP_RD_BRUSH, 0x5dffffffu, 0x5c000000u },
        { L"TextOnAccentFillColorPrimaryBrush",         UWP_RD_BRUSH, 0xffffffffu, 0xffffffffu },
        { L"PrimaryForegroundBrush",                    UWP_RD_BRUSH, 0xffffffffu, 0xff1a1a1au },
        { L"SecondaryForegroundBrush",                  UWP_RD_BRUSH, 0xffb4b4b4u, 0xff5c5c5cu },
        { L"ControlFillColorDefaultBrush",              UWP_RD_BRUSH, 0x18ffffffu, 0xb3ffffffu },
        { L"ControlFillColorSecondaryBrush",            UWP_RD_BRUSH, 0x22ffffffu, 0x80f9f9f9u },
        { L"ControlStrokeColorDefaultBrush",            UWP_RD_BRUSH, 0x18ffffffu, 0x19000000u },
        { L"CardBackgroundFillColorDefaultBrush",       UWP_RD_BRUSH, 0x0dffffffu, 0xb3ffffffu },
        { L"SystemAccentColorLight1",                   UWP_RD_COLOR, 0xff2b88d8u, 0xff2b88d8u },
        { L"SystemAccentColorLight2",                   UWP_RD_COLOR, 0xff4cc2ffu, 0xff4cc2ffu },
        { L"SystemAccentColorLight3",                   UWP_RD_COLOR, 0xff99ebffu, 0xff99ebffu },
        { L"SystemAccentColorDark1",                    UWP_RD_COLOR, 0xff005a9eu, 0xff005a9eu },
        { L"SystemAccentColorDark2",                    UWP_RD_COLOR, 0xff004578u, 0xff004578u },
        { L"SystemAccentColorDark3",                    UWP_RD_COLOR, 0xff002642u, 0xff002642u },
        { L"AccentTextThemeResource",                   UWP_RD_BRUSH, 0xff4cc2ffu, 0xff0067c0u },
        { L"SystemControlForegroundAccentBrush",        UWP_RD_BRUSH, 0xff4cc2ffu, 0xff0067c0u },
        { L"AccentTextFillColorPrimaryBrush",           UWP_RD_BRUSH, 0xff4cc2ffu, 0xff0067c0u },
        { L"AccentButtonBackground",                    UWP_RD_BRUSH, 0xff0078d4u, 0xff0067c0u },
        { L"AccentButtonForeground",                    UWP_RD_BRUSH, 0xffffffffu, 0xffffffffu },
    };
    static const struct { const WCHAR *name; enum uwp_rd_kind kind; } accent_keys[] =
    {
        { L"SystemAccentColor",                 UWP_RD_COLOR },
        { L"AccentFillColorDefaultBrush",       UWP_RD_BRUSH },
        { L"SystemControlHighlightAccentBrush", UWP_RD_BRUSH },
        { L"AccentBrush",                       UWP_RD_BRUSH },
    };
    struct uwp_rd_bucket *fb, *fb_dark, *fb_light;
    UINT32 sys_accent = uwp_get_system_accent_argb();
    size_t i;

    if (!theme_b) return;
    fb = get_rd_bucket( &g_theme_fb_sentinel, TRUE );
    fb_dark = get_rd_bucket( &g_theme_fb_dark_sentinel, TRUE );
    fb_light = get_rd_bucket( &g_theme_fb_light_sentinel, TRUE );
    if (!fb || !fb_dark || !fb_light) return;

    for (i = 0; i < ARRAY_SIZE(defaults); i++)
    {
        struct uwp_rd_entry *ed = rd_bucket_upsert( fb_dark, defaults[i].name );
        struct uwp_rd_entry *el = rd_bucket_upsert( fb_light, defaults[i].name );
        if (ed)
        {
            ed->kind = defaults[i].kind;
            ed->argb = defaults[i].dark_argb;
            ed->has_argb = TRUE;
            ed->opacity = 1.0;
        }
        if (el)
        {
            el->kind = defaults[i].kind;
            el->argb = defaults[i].light_argb;
            el->has_argb = TRUE;
            el->opacity = 1.0;
        }
    }
    for (i = 0; i < ARRAY_SIZE(accent_keys); i++)
    {
        struct uwp_rd_entry *ed = rd_bucket_upsert( fb_dark, accent_keys[i].name );
        struct uwp_rd_entry *el = rd_bucket_upsert( fb_light, accent_keys[i].name );
        if (ed)
        {
            ed->kind = accent_keys[i].kind;
            ed->argb = sys_accent;
            ed->has_argb = TRUE;
            ed->opacity = 1.0;
        }
        if (el)
        {
            el->kind = accent_keys[i].kind;
            el->argb = 0xff0067c0u;
            el->has_argb = TRUE;
            el->opacity = 1.0;
        }
    }
    uwp_rd_link_theme( fb, L"Dark", fb_dark );
    uwp_rd_link_theme( fb, L"Light", fb_light );
    uwp_rd_link_merged( theme_b, fb );
}

/* Compare two versioned theme XBF basenames (e.g. "21h1_themeresources.xbf" vs
 * "rs5_themeresources.xbf"). Prefers non-_v1, then digit-prefixed release tags
 * (YYhN) over letter-prefixed tags (rsN), then lexicographically higher tag. */
static int uwp_compare_theme_xbf_rank( const char *base_a, const char *base_b )
{
    BOOL a_v1, b_v1, a_dig, b_dig;
    size_t la, lb;
    if (!base_a) return -1;
    if (!base_b) return 1;
    la = strlen( base_a );
    lb = strlen( base_b );
    a_v1 = (la >= 7 && !_stricmp( base_a + la - 7, "_v1.xbf" ));
    b_v1 = (lb >= 7 && !_stricmp( base_b + lb - 7, "_v1.xbf" ));
    if (a_v1 != b_v1) return a_v1 ? -1 : 1;
    a_dig = (base_a[0] >= '0' && base_a[0] <= '9');
    b_dig = (base_b[0] >= '0' && base_b[0] <= '9');
    if (a_dig != b_dig) return a_dig ? 1 : -1;
    return _stricmp( base_a, base_b );
}

static BOOL uwp_path_in_themes_dir( const char *path, const char **out_base )
{
    const char *p, *slash;
    if (!path || !path[0]) return FALSE;
    slash = strrchr( path, '/' );
    if (!slash) slash = strrchr( path, '\\' );
    if (!slash || slash < path + 6) return FALSE;
    p = slash - 6;
    if (_strnicmp( p, "Themes", 6 ) != 0) return FALSE;
    if (p > path && p[-1] != '/' && p[-1] != '\\') return FALSE;
    if (out_base) *out_base = slash + 1;
    return TRUE;
}

static void uwp_discover_and_load_theme_xbfs( struct uwp_rd_bucket *theme_b )
{
    static BOOL (*const cat_getters[2])( DWORD, const char **, const BYTE **, DWORD * ) =
    {
        uwp_pri_get_cat_entry,
        uwp_pri_get_winui_cat_entry,
    };
    const char *best_tr_path = NULL, *best_tr_base = NULL;
    const BYTE *best_tr_data = NULL;
    DWORD best_tr_sz = 0;
    const char *best_gen_path = NULL, *best_gen_base = NULL;
    const BYTE *best_gen_data = NULL;
    DWORD best_gen_sz = 0;
    int ci;

    for (ci = 0; ci < 2; ci++)
    {
        DWORD k;
        const char *path = NULL;
        const BYTE *data = NULL;
        DWORD sz = 0;
        for (k = 0; cat_getters[ci]( k, &path, &data, &sz ); k++)
        {
            const char *base = NULL;
            size_t blen;
            if (!data || !sz || !uwp_path_in_themes_dir( path, &base ) || !base) continue;
            blen = strlen( base );
            /* Exact Themes/Generic.xbf from package and sub-libraries: load all */
            if (!_stricmp( base, "Generic.xbf" ))
            {
                if (uwp_mark_res_xbf_loaded( path ))
                {
                    TRACE( "xbf-res: loading generic theme %s (%lu bytes)\n", path, (unsigned long)sz );
                    uwp_parse_xbf_into_rd( data, sz, path, theme_b );
                }
                continue;
            }
            /* Versioned *themeresources*.xbf: pick highest-ranked release */
            if ((blen >= 18 && !_stricmp( base + blen - 18, "themeresources.xbf" )) ||
                (blen >= 21 && !_stricmp( base + blen - 21, "themeresources_v1.xbf" )))
            {
                if (!best_tr_base || uwp_compare_theme_xbf_rank( base, best_tr_base ) > 0)
                {
                    best_tr_path = path;
                    best_tr_base = base;
                    best_tr_data = data;
                    best_tr_sz = sz;
                }
                continue;
            }
            /* Versioned *generic*.xbf: pick highest-ranked release */
            if ((blen > 11 && !_stricmp( base + blen - 11, "generic.xbf" )) ||
                (blen > 14 && !_stricmp( base + blen - 14, "generic_v1.xbf" )))
            {
                if (!best_gen_base || uwp_compare_theme_xbf_rank( base, best_gen_base ) > 0)
                {
                    best_gen_path = path;
                    best_gen_base = base;
                    best_gen_data = data;
                    best_gen_sz = sz;
                }
            }
        }
    }

    if (best_tr_path && best_tr_data && best_tr_sz && uwp_mark_res_xbf_loaded( best_tr_path ))
    {
        TRACE( "xbf-res: loading discovered theme resources %s (%lu bytes)\n", best_tr_path, (unsigned long)best_tr_sz );
        uwp_parse_xbf_into_rd( best_tr_data, best_tr_sz, best_tr_path, theme_b );
    }
    if (best_gen_path && best_gen_data && best_gen_sz && uwp_mark_res_xbf_loaded( best_gen_path ))
    {
        TRACE( "xbf-res: loading discovered versioned generic %s (%lu bytes)\n", best_gen_path, (unsigned long)best_gen_sz );
        uwp_parse_xbf_into_rd( best_gen_data, best_gen_sz, best_gen_path, theme_b );
    }
}

/* Registry-driven per-app Resources_xbf + App.xbf + WinUI theme defaults (no hardcoded app families) */
void uwp_ensure_app_resources_loaded( void )
{
    struct uwp_rd_bucket *app_b, *theme_b;
    WCHAR pkg_full[256] = {0};
    UINT32 len = ARRAY_SIZE(pkg_full);

    if (g_app_res_loaded || g_app_res_loading) return;
    g_app_res_loading = TRUE;

    app_b = get_rd_bucket( &g_app_rd_sentinel, TRUE );
    theme_b = get_rd_bucket( &g_theme_rd_sentinel, TRUE );

    /* Seed system fallback Light/Dark buckets at lowest priority (merged[0]) before XBFs */
    uwp_seed_theme_fallback_buckets( theme_b );

    /* 1. Check Software\Classes\AppModel\Repository\Packages\<FullName>\Resources_xbf */
    if (GetCurrentPackageFullName( &len, pkg_full ) == ERROR_SUCCESS && pkg_full[0])
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
            if (RegOpenKeyExW( roots[r], sub, 0, KEY_READ, &hk ) != ERROR_SUCCESS) continue;
            if (RegQueryValueExW( hk, L"Resources_xbf", NULL, &type, (BYTE *)valbuf, &cb ) == ERROR_SUCCESS)
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
            RegCloseKey( hk );
        }
    }

    /* 2. Parse App.xbf (and any MergedDictionaries / ThemeDictionaries it references) */
    if (app_b)
        uwp_load_res_xbf_into_bucket( NULL, L"App.xbf", app_b );

    /* 3. Dynamically discover and load package/WinUI theme resource and generic XBFs */
    if (theme_b)
        uwp_discover_and_load_theme_xbfs( theme_b );

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
        HKEY hk = NULL;
        DWORD val = 0, type = 0, cb = sizeof(val);
        sys_theme_init = TRUE;
        sys_is_light = FALSE;
        if (RegOpenKeyExW( HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                           0, KEY_READ, &hk ) == ERROR_SUCCESS)
        {
            if (RegQueryValueExW( hk, L"AppsUseLightTheme", NULL, &type, (BYTE *)&val, &cb ) == ERROR_SUCCESS &&
                type == REG_DWORD)
                sys_is_light = (val != 0);
            RegCloseKey( hk );
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

static BOOL uwp_resolve_brush_entry( struct uwp_rd_bucket *start_bucket, struct uwp_element *scope_el,
                                     struct uwp_rd_entry *e, struct uwp_rd_bucket *found_b,
                                     int depth, UINT32 *out_argb )
{
    if (!e) return FALSE;
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
    return FALSE;
}

BOOL uwp_resolve_brush_or_color( struct uwp_rd_bucket *start_bucket, struct uwp_element *scope_el,
                                        const WCHAR *key_or_expr, int depth, UINT32 *out_argb )
{
    WCHAR key[128] = {0};
    struct uwp_rd_entry *e = NULL;
    struct uwp_rd_bucket *found_b = NULL;
    const WCHAR *fallback_key = NULL;

    if (!key_or_expr || !key_or_expr[0] || !out_argb || depth > 12) return FALSE;
    if (uwp_parse_color_string( key_or_expr, out_argb )) return TRUE;

    uwp_extract_resource_key( key_or_expr, key, ARRAY_SIZE(key) );
    if (!key[0]) return FALSE;
    if (uwp_parse_color_string( key, out_argb )) return TRUE;

    uwp_ensure_app_resources_loaded();
    if (uwp_rd_find_entry( start_bucket, scope_el, key, depth, &e, &found_b ) && e &&
        uwp_resolve_brush_entry( start_bucket, scope_el, e, found_b, depth, out_argb ))
        return TRUE;

    /* Fall back for standard Button / DropDownButton theme brush keys only on miss */
    if (!scope_el || !scope_el->is_accent_style)
    {
        if (!wcscmp( key, L"ButtonBackground" ) || !wcscmp( key, L"DropDownButtonBackground" ))
            fallback_key = L"ControlFillColorDefaultBrush";
        else if (!wcscmp( key, L"ButtonForeground" ) || !wcscmp( key, L"DropDownButtonForeground" ))
            fallback_key = L"TextFillColorPrimaryBrush";
        else if (!wcscmp( key, L"ButtonBorderBrush" ) || !wcscmp( key, L"DropDownButtonBorderBrush" ))
            fallback_key = L"ControlStrokeColorDefaultBrush";
        if (fallback_key &&
            uwp_rd_find_entry( start_bucket, scope_el, fallback_key, depth + 1, &e, &found_b ) && e)
            return uwp_resolve_brush_entry( start_bucket, scope_el, e, found_b, depth + 1, out_argb );
    }

    return FALSE;
}

IInspectable *uwp_rd_materialize_entry( struct uwp_rd_bucket *b, struct uwp_rd_entry *e )
{
    if (!e) return NULL;
    if (e->value) return e->value;
    if (e->kind == UWP_RD_DOUBLE)
        e->value = (e->tid == XBF_TYPE_INT32) ? box_int32_value( (INT32)e->dval ) : box_double_value( e->dval );
    else if (e->kind == UWP_RD_THICKNESS)
        e->value = box_thickness_value( e->vec4, e->tid == XBF_TYPE_CORNER_RADIUS );
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

    return 0x8000000b; /* E_BOUNDS */
}

BOOL uwp_rd_haskey( void *rd_key, void *key_obj )
{
    WCHAR raw_str[128] = {0}, key_str[128] = {0};
    struct uwp_rd_bucket *b, *found_b = NULL;
    struct uwp_rd_entry *e = NULL;

    if (!key_obj) return FALSE;
    if (!uwp_unbox_string( key_obj, raw_str, ARRAY_SIZE(raw_str) ))
    {
        const WCHAR *raw = WindowsGetStringRawBuffer( (HSTRING)key_obj, NULL );
        if (raw) lstrcpynW( raw_str, raw, ARRAY_SIZE(raw_str) );
    }
    uwp_extract_resource_key( raw_str, key_str, ARRAY_SIZE(key_str) );
    if (!key_str[0]) return FALSE;

    uwp_ensure_app_resources_loaded();
    b = get_rd_bucket( rd_key, FALSE );
    if (uwp_rd_find_entry( b, b ? b->owner_el : NULL, key_str, 0, &e, &found_b ) && e)
        return e->value != NULL || e->kind != UWP_RD_OBJ;
    return FALSE;
}
