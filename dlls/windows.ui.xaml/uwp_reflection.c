/* WinRT Windows.UI.Xaml UWP Reflection & Property Extraction Helpers
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
#include <stdlib.h>
#include <string.h>
#include <wctype.h>
#include <excpt.h>
#include "wine/exception.h"
#include "private.h"
#include "uwp_private.h"
#include "dp_vsm_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(xaml);

SRWLOCK xaml_meta_cache_lock = SRWLOCK_INIT;
INIT_ONCE xaml_meta_cache_once = INIT_ONCE_STATIC_INIT;
struct xaml_meta_cache_entry *xaml_meta_cache[XAML_META_CACHE_SIZE];

struct xaml_member_cache_entry
{
    WCHAR key[320];
    struct xaml_member *member;
    BOOL resolved;
    BOOL no_getter;
};
static SRWLOCK g_member_cache_lock = SRWLOCK_INIT;
static struct xaml_member_cache_entry g_member_cache[2048];

BOOL uwp_get_custom_xaml_member( void *app_outer, const WCHAR *owner_type_name,
                                 const WCHAR *prop_name, void *target_obj, IInspectable **out_val )
{
    void *provider = NULL, *xaml_type = NULL;
    struct xaml_member *xm = NULL;
    HSTRING h_prop = NULL;
    WCHAR key[320];
    UINT32 h = 2166136261u;
    const WCHAR *p;
    unsigned int idx, probe;
    BOOL ok = FALSE, found_entry = FALSE, no_getter = FALSE;

    if (!out_val) return FALSE;
    *out_val = NULL;
    if (!app_outer || !owner_type_name || !owner_type_name[0] || !prop_name || !target_obj) return FALSE;

    swprintf( key, ARRAY_SIZE(key), L"%s.%s", owner_type_name, prop_name );
    for (p = key; *p; p++) h = (h ^ (UINT32)*p) * 16777619u;
    idx = h & 2047u;

    AcquireSRWLockShared( &g_member_cache_lock );
    for (probe = 0; probe < 32; probe++)
    {
        unsigned int slot = (idx + probe) & 2047u;
        if (!g_member_cache[slot].resolved) break;
        if (!wcscmp( g_member_cache[slot].key, key ))
        {
            found_entry = TRUE;
            no_getter = g_member_cache[slot].no_getter;
            xm = g_member_cache[slot].member;
            if (xm && !no_getter) IInspectable_AddRef( (IInspectable *)xm );
            break;
        }
    }
    ReleaseSRWLockShared( &g_member_cache_lock );

    if (found_entry && (no_getter || !xm))
        return FALSE;

    if (!found_entry)
    {
        if (FAILED(IInspectable_QueryInterface( (IInspectable *)app_outer, &IID_IXamlMetadataProvider_UWP, &provider )) || !provider)
            return FALSE;
        xaml_type = uwp_provider_get_xaml_type_cached( (struct xaml_metadata_provider *)provider, owner_type_name );
        if (xaml_type)
        {
            struct xaml_type *xt = (struct xaml_type *)xaml_type;
            if (xt->lpVtbl->GetMember && SUCCEEDED(WindowsCreateString( prop_name, wcslen(prop_name), &h_prop )))
            {
                xt->lpVtbl->GetMember( xt, h_prop, &xm );
                WindowsDeleteString( h_prop );
            }
            IInspectable_Release( (IInspectable *)xaml_type );
        }
        IInspectable_Release( (IInspectable *)provider );

        AcquireSRWLockExclusive( &g_member_cache_lock );
        for (probe = 0; probe < 32; probe++)
        {
            unsigned int slot = (idx + probe) & 2047u;
            if (!g_member_cache[slot].resolved || !wcscmp( g_member_cache[slot].key, key ))
            {
                if (!g_member_cache[slot].resolved)
                {
                    lstrcpynW( g_member_cache[slot].key, key, ARRAY_SIZE(g_member_cache[slot].key) );
                    g_member_cache[slot].member = xm;
                    if (xm) IInspectable_AddRef( (IInspectable *)xm );
                    g_member_cache[slot].no_getter = (xm == NULL || xm->lpVtbl->GetValue == NULL);
                    g_member_cache[slot].resolved = TRUE;
                }
                break;
            }
        }
        ReleaseSRWLockExclusive( &g_member_cache_lock );
    }

    if (xm)
    {
        if (xm->lpVtbl->GetValue)
        {
            HRESULT hr = E_FAIL;
            __TRY
            {
                hr = xm->lpVtbl->GetValue( xm, target_obj, out_val );
            }
            __EXCEPT_ALL { hr = E_FAIL; *out_val = NULL; }
            __ENDTRY;
            if (SUCCEEDED(hr) && *out_val)
                ok = TRUE;
            else if (hr == E_NOTIMPL)
            {
                AcquireSRWLockExclusive( &g_member_cache_lock );
                for (probe = 0; probe < 32; probe++)
                {
                    unsigned int slot = (idx + probe) & 2047u;
                    if (!g_member_cache[slot].resolved) break;
                    if (!wcscmp( g_member_cache[slot].key, key ))
                    {
                        g_member_cache[slot].no_getter = TRUE;
                        break;
                    }
                }
                ReleaseSRWLockExclusive( &g_member_cache_lock );
            }
        }
        IInspectable_Release( (IInspectable *)xm );
    }
    return ok;
}

void uwp_set_custom_xaml_member( void *app_outer, const WCHAR *owner_type_name,
                                 const WCHAR *prop_name, void *target_obj, const WCHAR *str_val )
{
    void *provider = NULL, *xaml_type = NULL, *member = NULL, *member_type = NULL, *boxed_val = NULL;
    HSTRING h_prop = NULL, h_val = NULL;
    HRESULT hr = E_FAIL;

    if (!app_outer || !owner_type_name || !prop_name || !target_obj || !str_val) return;
    if (FAILED(IInspectable_QueryInterface( (IInspectable *)app_outer, &IID_IXamlMetadataProvider_UWP, &provider )) || !provider)
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
                            else if (tn && (!wcscmp( tn, L"TimeSpan" ) || !wcscmp( tn, L"System.TimeSpan" ) ||
                                            !wcscmp( tn, L"Windows.Foundation.TimeSpan" )))
                            {
                                TimeSpan ts = { 0 };
                                const WCHAR *c1 = wcschr( str_val, L':' );
                                if (c1)
                                {
                                    const WCHAR *c2 = wcschr( c1 + 1, L':' );
                                    double h = wcstod( str_val, NULL );
                                    double m = c2 ? wcstod( c1 + 1, NULL ) : 0.0;
                                    double s = c2 ? wcstod( c2 + 1, NULL ) : wcstod( c1 + 1, NULL );
                                    ts.Duration = (INT64)((h * 3600.0 + m * 60.0 + s) * 10000000.0);
                                }
                                else
                                {
                                    ts.Duration = (INT64)(wcstod( str_val, NULL ) * 10000000.0);
                                }
                                {
                                    IPropertyValueStatics *pvs = uwp_get_property_value_statics();
                                    if (pvs)
                                        IPropertyValueStatics_CreateTimeSpan( pvs, ts, (IInspectable **)&boxed_val );
                                }
                                hr = boxed_val ? S_OK : E_FAIL;
                                is_bool_type = TRUE; /* prevent CreateFromString fallback */
                            }
                            WindowsDeleteString( h_tname );
                        }
                        if (boxed_val)
                        {
                            /* already boxed directly as primitive */
                        }
                        else if (is_bool_type && (!wcsicmp( str_val, L"True" ) || !wcsicmp( str_val, L"False" )))
                        {
                            boxed_val = box_bool_value( !wcsicmp( str_val, L"True" ) ? TRUE : FALSE );
                            hr = boxed_val ? S_OK : E_FAIL;
                        }
                        else if (!is_bool_type)
                        {
                            __TRY { hr = mt->lpVtbl->CreateFromString( mt, h_val, &boxed_val ); }
                            __EXCEPT_ALL { hr = E_FAIL; boxed_val = NULL; }
                            __ENDTRY;
                            if (FAILED(hr)) boxed_val = NULL;
                        }
                        if (SUCCEEDED(hr) && boxed_val && xm->lpVtbl->SetValue)
                        {
                            __TRY { hr = xm->lpVtbl->SetValue( xm, target_obj, boxed_val ); }
                            __EXCEPT_ALL { hr = E_FAIL; }
                            __ENDTRY;
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
    if (FAILED(IInspectable_QueryInterface( (IInspectable *)app_outer, &IID_IXamlMetadataProvider_UWP, &provider )) || !provider)
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
                BOOL type_mismatch = FALSE;
                if (uwp_elem_from_any( val_obj ) && xm->lpVtbl->get_Type)
                {
                    struct xaml_type *mt = NULL;
                    if (SUCCEEDED(xm->lpVtbl->get_Type( xm, &mt )) && mt)
                    {
                        HSTRING h_mt = NULL;
                        if (mt->lpVtbl->get_FullName && SUCCEEDED(mt->lpVtbl->get_FullName( mt, &h_mt )) && h_mt)
                        {
                            const WCHAR *mtn = WindowsGetStringRawBuffer( h_mt, NULL );
                            if (mtn && (!wcscmp( mtn, L"Double" ) || !wcscmp( mtn, L"Single" ) ||
                                        !wcscmp( mtn, L"Int32" ) || !wcscmp( mtn, L"UInt32" ) ||
                                        !wcscmp( mtn, L"Int64" ) || !wcscmp( mtn, L"UInt64" ) ||
                                        !wcscmp( mtn, L"Boolean" ) || !wcscmp( mtn, L"String" ) ||
                                        !wcscmp( mtn, L"Windows.UI.Xaml.GridLength" ) ||
                                        !wcscmp( mtn, L"Windows.UI.Xaml.Thickness" ) ||
                                        !wcscmp( mtn, L"Windows.UI.Xaml.CornerRadius" )))
                                type_mismatch = TRUE;
                            WindowsDeleteString( h_mt );
                        }
                        IInspectable_Release( (IInspectable *)mt );
                    }
                }
                if (!type_mismatch && xm->lpVtbl->SetValue)
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
    if (FAILED(IInspectable_QueryInterface( (IInspectable *)app_outer, &IID_IXamlMetadataProvider_UWP, &provider )) || !provider)
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
                    BOOL target_is_coll = FALSE;
                    boolean is_attachable = FALSE;
                    struct uwp_element *tel = uwp_elem_from_any( target_obj );
                    if (xm->lpVtbl->get_IsAttachable)
                        xm->lpVtbl->get_IsAttachable( xm, &is_attachable );
                    if (is_attachable && (!tel || wcsstr( tel->class_name, L"Collection" ) || wcsstr( tel->class_name, L"AnimationSet" )))
                        target_is_coll = TRUE;
                    if (SUCCEEDED(xm->lpVtbl->get_Type( xm, (struct xaml_type **)&member_type )) && member_type && !target_is_coll)
                    {
                        struct xaml_type *mt = (struct xaml_type *)member_type;
                        HSTRING h_mt = NULL, h_tc = NULL;
                        if (mt->lpVtbl->get_FullName && SUCCEEDED(mt->lpVtbl->get_FullName( mt, &h_mt )) && h_mt &&
                            SUCCEEDED(IInspectable_GetRuntimeClassName( (IInspectable *)target_obj, &h_tc )) && h_tc)
                        {
                            const WCHAR *mtn = WindowsGetStringRawBuffer( h_mt, NULL );
                            const WCHAR *tcn = WindowsGetStringRawBuffer( h_tc, NULL );
                            if (mtn && tcn && (!wcscmp( mtn, tcn ) ||
                                               wcsstr( tcn, L"Collection" ) || wcsstr( tcn, L"AnimationSet" )))
                                target_is_coll = TRUE;
                        }
                        if (h_mt) WindowsDeleteString( h_mt );
                        if (h_tc) WindowsDeleteString( h_tc );
                    }
                    if (target_is_coll)
                    {
                        hr = E_FAIL;
                    }
                    else
                    {
                        __TRY
                        {
                            hr = xm->lpVtbl->GetValue( xm, target_obj, &coll );
                        }
                        __EXCEPT_ALL { hr = E_FAIL; coll = NULL; }
                        __ENDTRY;
                    }
                    if (SUCCEEDED(hr) && coll && member_type)
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
                    }
                    if (member_type) IInspectable_Release( (IInspectable *)member_type );
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

BOOL uwp_inspectable_to_string( IInspectable *obj, WCHAR *buf, int buf_cch )
{
    void *strable = NULL;

    if (!buf || buf_cch <= 0) return FALSE;
    buf[0] = 0;
    if (!obj || (ULONG_PTR)obj < 0x10000) return FALSE;

    if (uwp_unbox_string( obj, buf, buf_cch ) && buf[0])
        return TRUE;

    if (SUCCEEDED(IInspectable_QueryInterface( obj, &IID_IStringable_UWP, &strable )) && strable)
    {
        HSTRING hs = NULL;
        HRESULT (WINAPI *to_str)(void *, HSTRING *) = (void *)(*(void ***)strable)[6];
        if (to_str && SUCCEEDED(to_str( strable, &hs )) && hs)
        {
            const WCHAR *s = WindowsGetStringRawBuffer( hs, NULL );
            if (s && s[0]) lstrcpynW( buf, s, buf_cch );
            WindowsDeleteString( hs );
        }
        IUnknown_Release( (IUnknown *)strable );
    }
    return buf[0] != 0;
}

static void uwp_vm_extract_nested_url( IInspectable *pval, const WCHAR *const *props, unsigned int count,
                                       WCHAR *out, size_t out_len, int depth );

static BOOL uwp_vm_extract_hstring_vec_first( IInspectable *pval, WCHAR *out, size_t out_len )
{
    static const GUID IID_IVector_HSTRING_UWP =
        { 0x98b9acc1, 0x4b56, 0x532e, { 0xac, 0x73, 0x03, 0xd5, 0x29, 0x1c, 0xca, 0x90 } };
    static const GUID IID_IVectorView_HSTRING_UWP =
        { 0x2f13c006, 0xa03a, 0x5f69, { 0xb0, 0x90, 0x75, 0xa4, 0x3e, 0x33, 0x42, 0x3e } };
    static const GUID IID_IIterable_HSTRING_UWP =
        { 0xe2fcc7c1, 0x3bfc, 0x5a0b, { 0xb2, 0xb0, 0x72, 0xe7, 0x69, 0xd1, 0xcb, 0x7e } };
    void *svec = NULL;
    if (!pval || !out || !out_len) return FALSE;
    if (SUCCEEDED(IInspectable_QueryInterface( pval, &IID_IVector_HSTRING_UWP, &svec )) ||
        SUCCEEDED(IInspectable_QueryInterface( pval, &IID_IVectorView_HSTRING_UWP, &svec )))
    {
        HRESULT (WINAPI *get_at)(void *, unsigned int, HSTRING *) = (void *)(*(void ***)svec)[6];
        HRESULT (WINAPI *get_sz)(void *, unsigned int *) = (void *)(*(void ***)svec)[7];
        unsigned int sz = 0;
        HSTRING hs = NULL;
        if (get_sz && get_at && SUCCEEDED(get_sz( svec, &sz )) && sz > 0 &&
            SUCCEEDED(get_at( svec, 0, &hs )) && hs)
        {
            const WCHAR *raw = WindowsGetStringRawBuffer( hs, NULL );
            if (raw && raw[0]) lstrcpynW( out, raw, (int)out_len );
            WindowsDeleteString( hs );
        }
        IUnknown_Release( (IUnknown *)svec );
        if (out[0]) return TRUE;
    }
    if (SUCCEEDED(IInspectable_QueryInterface( pval, &IID_IIterable_HSTRING_UWP, &svec )) && svec)
    {
        HRESULT (WINAPI *first)(void *, void **) = (void *)(*(void ***)svec)[6];
        void *iter = NULL;
        if (first && SUCCEEDED(first( svec, &iter )) && iter)
        {
            HRESULT (WINAPI *get_cur)(void *, HSTRING *) = (void *)(*(void ***)iter)[6];
            HRESULT (WINAPI *has_cur)(void *, boolean *) = (void *)(*(void ***)iter)[7];
            boolean has = FALSE;
            HSTRING hs = NULL;
            if (has_cur && SUCCEEDED(has_cur( iter, &has )) && has &&
                get_cur && SUCCEEDED(get_cur( iter, &hs )) && hs)
            {
                const WCHAR *raw = WindowsGetStringRawBuffer( hs, NULL );
                if (raw && raw[0]) lstrcpynW( out, raw, (int)out_len );
                WindowsDeleteString( hs );
            }
            IUnknown_Release( (IUnknown *)iter );
        }
        IUnknown_Release( (IUnknown *)svec );
        if (out[0]) return TRUE;
    }
    return FALSE;
}

static void uwp_vm_extract_val_url( IInspectable *pval, const WCHAR *const *props, unsigned int count,
                                    WCHAR *out, size_t out_len, int depth )
{
    double dv = 0.0;
    if (uwp_unbox_string( pval, out, out_len ))
        return;
    if (uwp_unbox_double( pval, &dv ) && dv > 0.0 && dv < 100000.0)
    {
        swprintf( out, out_len, L"%.1f", dv );
        return;
    }
    if (uwp_vm_extract_hstring_vec_first( pval, out, out_len ))
        return;
    uwp_vm_extract_nested_url( pval, props, count, out, out_len, depth );
}

static void uwp_vm_probe_xaml_members( IInspectable *vm, const WCHAR *cls,
                                       const WCHAR *const *props, unsigned int count,
                                       WCHAR *out, size_t out_len, int depth )
{
    unsigned int pi;
    if (!cls || !cls[0]) return;
    for (pi = 0; pi < count && !out[0]; pi++)
    {
        IInspectable *pval = NULL;
        if (uwp_get_custom_xaml_member( uwp_get_app_outer(), cls, props[pi], vm, &pval ) && pval)
        {
            uwp_vm_extract_val_url( pval, props, count, out, out_len, depth );
            IInspectable_Release( pval );
        }
    }
}

BOOL uwp_vm_query_string_prop_depth( IInspectable *vm, const WCHAR *const *props, unsigned int count,
                                     WCHAR *out, size_t out_len, int depth )
{
    HSTRING hcls = NULL;
    void *cpp = NULL;
    unsigned int pi;

    if (!vm || !out || !out_len) return FALSE;
    out[0] = 0;

    if (SUCCEEDED(IInspectable_GetRuntimeClassName( vm, &hcls )) && hcls)
    {
        uwp_vm_probe_xaml_members( vm, WindowsGetStringRawBuffer( hcls, NULL ), props, count, out, out_len, depth );
        WindowsDeleteString( hcls );
    }
    if (!out[0] && SUCCEEDED(IInspectable_QueryInterface( vm, &IID_ICustomPropertyProvider_UWP, &cpp )) && cpp)
    {
        HRESULT (WINAPI *get_prop)(void *, HSTRING, void **) = (void *)(*(void ***)cpp)[6];
        HRESULT (WINAPI *get_type)(void *, TypeName *) = (void *)(*(void ***)cpp)[9];
        if (get_type)
        {
            TypeName tn = {0};
            if (SUCCEEDED(get_type( cpp, &tn )) && tn.Name)
            {
                WCHAR tcls[256] = {0};
                WCHAR *comma;
                const WCHAR *raw = WindowsGetStringRawBuffer( tn.Name, NULL );
                if (raw) lstrcpynW( tcls, raw, ARRAY_SIZE(tcls) );
                comma = wcschr( tcls, ',' );
                if (comma) *comma = 0;
                uwp_vm_probe_xaml_members( vm, tcls, props, count, out, out_len, depth );
                WindowsDeleteString( tn.Name );
            }
        }
        for (pi = 0; pi < count && !out[0]; pi++)
        {
            HSTRING hp = NULL;
            void *cp = NULL;
            if (get_prop && SUCCEEDED(WindowsCreateString( props[pi], wcslen(props[pi]), &hp )) && hp)
            {
                if (SUCCEEDED(get_prop( cpp, hp, &cp )) && cp)
                {
                    HRESULT (WINAPI *get_val)(void *, IInspectable *, IInspectable **) = (void *)(*(void ***)cp)[8];
                    IInspectable *pval = NULL;
                    if (get_val && SUCCEEDED(get_val( cp, vm, &pval )) && pval)
                    {
                        uwp_vm_extract_val_url( pval, props, count, out, out_len, depth );
                        IInspectable_Release( pval );
                    }
                    IUnknown_Release( (IUnknown *)cp );
                }
                WindowsDeleteString( hp );
            }
        }
        IUnknown_Release( (IUnknown *)cpp );
    }
    return out[0] != 0;
}

static void uwp_vm_extract_nested_url( IInspectable *pval, const WCHAR *const *props, unsigned int count,
                                       WCHAR *out, size_t out_len, int depth )
{
    static const WCHAR *const nested_url_props[] =
        { L"Url", L"Uri", L"ImageUrl", L"ImageUri", L"Source", L"AbsoluteUri", L"RawUri", L"Location", L"Path",
          L"UriString", L"UrlString", L"ImageSource", L"BitmapImage", L"ImageLinkUri", L"PrimaryImageUrl",
          L"TileImage", L"RequestedImage", L"CurrentImage", L"DefaultImage", L"BackgroundImage", L"ForegroundImage",
          L"SmallLogoImage", L"Icon", L"Image", L"Images" };
    struct uwp_element *pel = uwp_elem_from_any( pval );
    IInspectable *items[6];
    unsigned int cnt, i;

    if (pel && pel->image_src[0])
    {
        lstrcpynW( out, pel->image_src, out_len );
        return;
    }
    uwp_extract_uri_string( pval, out, out_len );
    if (out[0] || depth <= 0) return;

    cnt = uwp_winrt_vec_snapshot( pval, items, ARRAY_SIZE(items) );
    for (i = 0; i < cnt; i++)
    {
        if (!out[0] && !uwp_unbox_string( items[i], out, out_len ))
        {
            if (props && count > 0)
                uwp_vm_query_string_prop_depth( items[i], props, count, out, out_len, depth - 1 );
            if (!out[0] && props != nested_url_props)
                uwp_vm_query_string_prop_depth( items[i], nested_url_props, ARRAY_SIZE(nested_url_props),
                                                out, out_len, depth - 1 );
        }
        IInspectable_Release( items[i] );
    }
    if (!out[0] && cnt == 0)
    {
        if (props && count > 0)
            uwp_vm_query_string_prop_depth( pval, props, count, out, out_len, depth - 1 );
        if (!out[0] && props != nested_url_props)
            uwp_vm_query_string_prop_depth( pval, nested_url_props, ARRAY_SIZE(nested_url_props), out, out_len, depth - 1 );
    }
}

BOOL uwp_vm_query_string_prop( IInspectable *vm, const WCHAR *const *props, unsigned int count,
                               WCHAR *out, size_t out_len )
{
    return uwp_vm_query_string_prop_depth( vm, props, count, out, out_len, 0 );
}
