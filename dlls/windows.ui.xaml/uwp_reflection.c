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

static HRESULT xaml_append_to_winrt_vector( IInspectable *coll, IInspectable *item )
{
    static const GUID IID_IVector_DO_refl =
        { 0x771b857e, 0xab5c, 0x5db8, { 0xa0, 0x21, 0x39, 0x7c, 0x92, 0xcd, 0xc4, 0x4c } };
    static const GUID *const vec_iids[] =
    {
        &IID_IVector_DO_refl,
        &IID_IVector_IInspectable_UWP,
        &IID_IBindableVector_UWP,
    };
    unsigned int qi;
    if (!coll || !item) return E_INVALIDARG;
    for (qi = 0; qi < ARRAY_SIZE(vec_iids); qi++)
    {
        void *vec = NULL;
        if (SUCCEEDED(IInspectable_QueryInterface( coll, vec_iids[qi], &vec )) && vec)
        {
            const struct abi_winrt_vector *avec = (const struct abi_winrt_vector *)vec;
            HRESULT hr = E_NOTIMPL;
            if (avec->lpVtbl->Append)
                hr = avec->lpVtbl->Append( vec, item );
            IUnknown_Release( (IUnknown *)vec );
            return hr;
        }
    }
    return E_NOINTERFACE;
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
                    if (is_attachable && (!tel || (tel->class_flags2 & (UWP_CF2_COLLECTION | UWP_CF2_ANIMATION_SET))))
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
                        struct uwp_element *cel = uwp_elem_from_any( coll );
                        unsigned int before_cnt = 0;
                        if (cel && cel->children_coll)
                        {
                            struct uwp_vec_view *cv = CONTAINING_RECORD( cel->children_coll, struct uwp_vec_view, IInspectable_iface );
                            before_cnt = cv->count;
                        }
                        if (mt->lpVtbl->AddToVector)
                        {
                            __TRY
                            {
                                hr = mt->lpVtbl->AddToVector( mt, coll, item_obj );
                            }
                            __EXCEPT_ALL { hr = E_FAIL; }
                            __ENDTRY;
                            if (SUCCEEDED(hr) && cel && !wcscmp( cel->class_name, L"Windows.UI.Xaml.DependencyObjectCollection" ))
                            {
                                unsigned int after_cnt = 0;
                                if (cel->children_coll)
                                {
                                    struct uwp_vec_view *cv = CONTAINING_RECORD( cel->children_coll, struct uwp_vec_view, IInspectable_iface );
                                    after_cnt = cv->count;
                                }
                                if (after_cnt == before_cnt)
                                {
                                    HSTRING h_mtn = NULL;
                                    if (mt->lpVtbl->get_FullName) mt->lpVtbl->get_FullName( mt, &h_mtn );
                                    WARN( "AddToVector(%s.%s mt=%s) left DependencyObjectCollection count=%u unchanged; appending via IVector\n",
                                          debugstr_w(owner_type_name), debugstr_w(prop_name),
                                          h_mtn ? debugstr_w(WindowsGetStringRawBuffer( h_mtn, NULL )) : "(null)", after_cnt );
                                    if (h_mtn) WindowsDeleteString( h_mtn );
                                    hr = xaml_append_to_winrt_vector( coll, (IInspectable *)item_obj );
                                }
                            }
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
        const struct abi_stringable *astr = (const struct abi_stringable *)strable;
        HSTRING hs = NULL;
        if (astr->lpVtbl->ToString && SUCCEEDED(astr->lpVtbl->ToString( strable, &hs )) && hs)
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
        const struct abi_winrt_hstring_vector *ahvec = (const struct abi_winrt_hstring_vector *)svec;
        unsigned int sz = 0;
        HSTRING hs = NULL;
        if (ahvec->lpVtbl->get_Size && ahvec->lpVtbl->GetAt && SUCCEEDED(ahvec->lpVtbl->get_Size( svec, &sz )) && sz > 0 &&
            SUCCEEDED(ahvec->lpVtbl->GetAt( svec, 0, &hs )) && hs)
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
        const struct abi_winrt_iterable *ait = (const struct abi_winrt_iterable *)svec;
        void *iter = NULL;
        if (ait->lpVtbl->First && SUCCEEDED(ait->lpVtbl->First( svec, &iter )) && iter)
        {
            const struct abi_winrt_hstring_iterator *ahiter = (const struct abi_winrt_hstring_iterator *)iter;
            boolean has = FALSE;
            HSTRING hs = NULL;
            if (ahiter->lpVtbl->get_HasCurrent && SUCCEEDED(ahiter->lpVtbl->get_HasCurrent( iter, &has )) && has &&
                ahiter->lpVtbl->get_Current && SUCCEEDED(ahiter->lpVtbl->get_Current( iter, &hs )) && hs)
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
        const struct abi_custom_property_provider *acpp = (const struct abi_custom_property_provider *)cpp;
        if (acpp->lpVtbl->get_Type)
        {
            TypeName tn = {0};
            if (SUCCEEDED(acpp->lpVtbl->get_Type( cpp, &tn )) && tn.Name)
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
            if (acpp->lpVtbl->GetCustomProperty && SUCCEEDED(WindowsCreateString( props[pi], wcslen(props[pi]), &hp )) && hp)
            {
                if (SUCCEEDED(acpp->lpVtbl->GetCustomProperty( cpp, hp, &cp )) && cp)
                {
                    const struct abi_custom_property *acp = (const struct abi_custom_property *)cp;
                    IInspectable *pval = NULL;
                    if (acp->lpVtbl->GetValue && SUCCEEDED(acp->lpVtbl->GetValue( cp, vm, &pval )) && pval)
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

    if (uwp_elem_image_src( pel )[0])
    {
        lstrcpynW( out, uwp_elem_image_src( pel ), out_len );
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

static UINT32 xaml_meta_hash( const WCHAR *s )
{
    UINT32 h = 2166136261u;
    for (; *s; s++)
    {
        h ^= (UINT32)*s;
        h *= 16777619u;
    }
    return h;
}

static SRWLOCK g_ct_weak_lock = SRWLOCK_INIT;

struct xaml_ct_weak_ctrl
{
    LONG ref;
    struct xaml_cached_type *obj;
};

struct xaml_ct_weak_ref
{
    const IWeakReferenceVtbl *lpVtbl;
    LONG ref;
    struct xaml_ct_weak_ctrl *ctrl;
};

static void xaml_ct_weak_ctrl_release( struct xaml_ct_weak_ctrl *ctrl )
{
    if (ctrl && !InterlockedDecrement( &ctrl->ref ))
        free( ctrl );
}

static inline struct xaml_cached_type *xaml_ct_from_uwp( void *iface )
{
    return CONTAINING_RECORD( iface, struct xaml_cached_type, lpVtbl );
}

static inline struct xaml_cached_type *xaml_ct_from_winui3( void *iface )
{
    return CONTAINING_RECORD( iface, struct xaml_cached_type, lpVtbl_winui3 );
}

static inline struct xaml_cached_type *xaml_ct_from_weak_src( void *iface )
{
    return CONTAINING_RECORD( iface, struct xaml_cached_type, lpVtbl_weak_src );
}

static HRESULT WINAPI xaml_ct_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct xaml_cached_type *t = xaml_ct_from_uwp( iface );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IXamlType_UWP_g ))
    {
        *out = &t->lpVtbl;
        InterlockedIncrement( &t->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IXamlType_WinUI3_g ))
    {
        *out = &t->lpVtbl_winui3;
        InterlockedIncrement( &t->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IWeakReferenceSource_local ))
    {
        *out = &t->lpVtbl_weak_src;
        InterlockedIncrement( &t->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI xaml_ct_AddRef( void *iface )
{
    return InterlockedIncrement( &xaml_ct_from_uwp( iface )->ref );
}

static ULONG WINAPI xaml_ct_Release( void *iface )
{
    /* Singleton-cached in xaml_meta_cache for process lifetime; ref stays >= 1 */
    struct xaml_cached_type *t = xaml_ct_from_uwp( iface );
    LONG r = InterlockedDecrement( &t->ref );
    if (r == 0)
    {
        struct xaml_ct_weak_ctrl *ctrl;
        AcquireSRWLockExclusive( &g_ct_weak_lock );
        ctrl = t->weak_ctrl;
        t->weak_ctrl = NULL;
        if (ctrl) ctrl->obj = NULL;
        ReleaseSRWLockExclusive( &g_ct_weak_lock );
        xaml_ct_weak_ctrl_release( ctrl );
        free( t->full_name );
        free( t );
    }
    return r;
}

static HRESULT WINAPI xaml_ct_winui3_QueryInterface( void *iface, REFIID iid, void **out )
{
    return xaml_ct_QueryInterface( &xaml_ct_from_winui3( iface )->lpVtbl, iid, out );
}

static ULONG WINAPI xaml_ct_winui3_AddRef( void *iface )
{
    return xaml_ct_AddRef( &xaml_ct_from_winui3( iface )->lpVtbl );
}

static ULONG WINAPI xaml_ct_winui3_Release( void *iface )
{
    return xaml_ct_Release( &xaml_ct_from_winui3( iface )->lpVtbl );
}

static HRESULT WINAPI xaml_ct_wr_QI( IWeakReference *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IWeakReference_local ))
    {
        *out = iface;
        InterlockedIncrement( &((struct xaml_ct_weak_ref *)iface)->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI xaml_ct_wr_AddRef( IWeakReference *iface )
{
    return InterlockedIncrement( &((struct xaml_ct_weak_ref *)iface)->ref );
}

static ULONG WINAPI xaml_ct_wr_Release( IWeakReference *iface )
{
    struct xaml_ct_weak_ref *wr = (struct xaml_ct_weak_ref *)iface;
    ULONG r = InterlockedDecrement( &wr->ref );
    if (!r)
    {
        xaml_ct_weak_ctrl_release( wr->ctrl );
        free( wr );
    }
    return r;
}

static HRESULT WINAPI xaml_ct_wr_Resolve( IWeakReference *iface, REFIID iid, IInspectable **out )
{
    struct xaml_ct_weak_ref *wr = (struct xaml_ct_weak_ref *)iface;
    struct xaml_cached_type *obj = NULL;
    HRESULT hr;
    if (!out) return E_POINTER;
    *out = NULL;
    if (!wr->ctrl) return S_OK;
    AcquireSRWLockShared( &g_ct_weak_lock );
    obj = wr->ctrl->obj;
    if (obj)
    {
        LONG cur = obj->ref;
        while (cur > 0)
        {
            if (InterlockedCompareExchange( &obj->ref, cur + 1, cur ) == cur)
                break;
            cur = obj->ref;
        }
        if (cur <= 0) obj = NULL;
    }
    ReleaseSRWLockShared( &g_ct_weak_lock );
    if (!obj) return S_OK;
    hr = xaml_ct_QueryInterface( &obj->lpVtbl, iid, (void **)out );
    xaml_ct_Release( &obj->lpVtbl );
    return hr;
}

static const struct IWeakReferenceVtbl xaml_ct_weak_ref_vtbl =
{
    xaml_ct_wr_QI,
    xaml_ct_wr_AddRef,
    xaml_ct_wr_Release,
    xaml_ct_wr_Resolve,
};

static HRESULT WINAPI xaml_ct_wrs_QueryInterface( void *iface, REFIID iid, void **out )
{
    return xaml_ct_QueryInterface( &xaml_ct_from_weak_src( iface )->lpVtbl, iid, out );
}

static ULONG WINAPI xaml_ct_wrs_AddRef( void *iface )
{
    return xaml_ct_AddRef( &xaml_ct_from_weak_src( iface )->lpVtbl );
}

static ULONG WINAPI xaml_ct_wrs_Release( void *iface )
{
    return xaml_ct_Release( &xaml_ct_from_weak_src( iface )->lpVtbl );
}

static HRESULT WINAPI xaml_ct_wrs_GetWeakReference( void *iface, IWeakReference **out )
{
    struct xaml_cached_type *t = xaml_ct_from_weak_src( iface );
    struct xaml_ct_weak_ctrl *ctrl;
    struct xaml_ct_weak_ref *wr;
    if (!out) return E_POINTER;
    *out = NULL;
    AcquireSRWLockExclusive( &g_ct_weak_lock );
    ctrl = t->weak_ctrl;
    if (!ctrl)
    {
        ctrl = calloc( 1, sizeof(*ctrl) );
        if (!ctrl)
        {
            ReleaseSRWLockExclusive( &g_ct_weak_lock );
            return E_OUTOFMEMORY;
        }
        ctrl->ref = 1;
        ctrl->obj = t;
        t->weak_ctrl = ctrl;
    }
    InterlockedIncrement( &ctrl->ref );
    ReleaseSRWLockExclusive( &g_ct_weak_lock );
    wr = calloc( 1, sizeof(*wr) );
    if (!wr)
    {
        xaml_ct_weak_ctrl_release( ctrl );
        return E_OUTOFMEMORY;
    }
    wr->lpVtbl = &xaml_ct_weak_ref_vtbl;
    wr->ref = 1;
    wr->ctrl = ctrl;
    *out = (IWeakReference *)wr;
    return S_OK;
}

static const void *xaml_cached_type_wrs_vtbl[] =
{
    xaml_ct_wrs_QueryInterface,
    xaml_ct_wrs_AddRef,
    xaml_ct_wrs_Release,
    xaml_ct_wrs_GetWeakReference,
};

static HRESULT WINAPI xaml_ct_GetIids( void *iface, ULONG *cnt, IID **iids )
{
    if (cnt) *cnt = 0;
    if (iids) *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI xaml_ct_GetRuntimeClassName( void *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.UI.Xaml.Markup.IXamlType", 32, name );
}

static HRESULT WINAPI xaml_ct_GetTrustLevel( void *iface, TrustLevel *lvl )
{
    if (lvl) *lvl = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI xaml_ct_get_NullPtr( void *iface, void **out )
{
    if (out) *out = NULL;
    return S_OK;
}

static HRESULT WINAPI xaml_ct_get_False( void *iface, boolean *out )
{
    if (out) *out = FALSE;
    return S_OK;
}

static HRESULT WINAPI xaml_ct_get_True( void *iface, boolean *out )
{
    if (out) *out = TRUE;
    return S_OK;
}

static HRESULT WINAPI xaml_ct_get_FullName( void *iface, HSTRING *name )
{
    struct xaml_cached_type *t = xaml_ct_from_uwp( iface );
    if (!name) return E_POINTER;
    return WindowsCreateString( t->full_name, wcslen( t->full_name ), name );
}

static HRESULT WINAPI xaml_ct_get_Name( void *iface, HSTRING *name )
{
    struct xaml_cached_type *t = xaml_ct_from_uwp( iface );
    if (!name) return E_POINTER;
    return WindowsCreateString( t->short_name, wcslen( t->short_name ), name );
}

static HRESULT WINAPI xaml_ct_ActivateInstance( void *iface, IInspectable **instance )
{
    struct xaml_cached_type *t = xaml_ct_from_uwp( iface );
    HRESULT hr;
    if (!instance) return E_POINTER;
    *instance = NULL;
    hr = uwp_element_create( t->full_name, NULL, NULL, (void **)instance );
    if (SUCCEEDED(hr) && *instance) return hr;
    {
        HSTRING h_cls = NULL;
        if (SUCCEEDED(WindowsCreateString( t->full_name, wcslen( t->full_name ), &h_cls )))
        {
            hr = RoActivateInstance( h_cls, instance );
            WindowsDeleteString( h_cls );
        }
    }
    return hr;
}

static HRESULT WINAPI xaml_ct_CreateFromString( void *iface, HSTRING input, void **value )
{
    if (value) *value = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI xaml_ct_GetMember( void *iface, HSTRING name, struct xaml_member **member )
{
    if (member) *member = NULL;
    return S_OK;
}

static HRESULT WINAPI xaml_ct_RunInitializer( void *iface )
{
    return S_OK;
}

static HRESULT WINAPI xaml_ct_get_UnderlyingType( void *iface, TypeName *type )
{
    struct xaml_cached_type *t = xaml_ct_from_uwp( iface );
    if (!type) return E_POINTER;
    type->Kind = TypeKind_Metadata;
    return WindowsCreateString( t->full_name, wcslen( t->full_name ), &type->Name );
}

static HRESULT WINAPI xaml_ct_AddToVector( void *iface, IInspectable *instance, IInspectable *value )
{
    HRESULT hr = xaml_append_to_winrt_vector( instance, value );
    return (hr == E_NOINTERFACE) ? S_OK : hr;
}

static HRESULT WINAPI xaml_ct_AddToMap( void *iface, IInspectable *instance, IInspectable *key, IInspectable *value )
{
    return S_OK;
}

static const void *xaml_cached_type_uwp_vtbl[] =
{
    xaml_ct_QueryInterface,      /* 0 */
    xaml_ct_AddRef,              /* 1 */
    xaml_ct_Release,             /* 2 */
    xaml_ct_GetIids,             /* 3 */
    xaml_ct_GetRuntimeClassName, /* 4 */
    xaml_ct_GetTrustLevel,       /* 5 */
    xaml_ct_get_NullPtr,         /* 6: get_BaseType */
    xaml_ct_get_NullPtr,         /* 7: get_ContentProperty */
    xaml_ct_get_FullName,        /* 8: get_FullName */
    xaml_ct_get_False,           /* 9: get_IsArray */
    xaml_ct_get_False,           /* 10: get_IsCollection */
    xaml_ct_get_True,            /* 11: get_IsConstructible */
    xaml_ct_get_False,           /* 12: get_IsDictionary */
    xaml_ct_get_False,           /* 13: get_IsMarkupExtension */
    xaml_ct_get_False,           /* 14: get_IsBindable */
    xaml_ct_get_NullPtr,         /* 15: get_ItemType */
    xaml_ct_get_NullPtr,         /* 16: get_KeyType */
    xaml_ct_get_UnderlyingType,  /* 17: get_UnderlyingType */
    xaml_ct_ActivateInstance,    /* 18: ActivateInstance (UWP) */
    xaml_ct_CreateFromString,    /* 19: CreateFromString */
    xaml_ct_GetMember,           /* 20: GetMember */
    xaml_ct_AddToVector,         /* 21: AddToVector */
    xaml_ct_AddToMap,            /* 22: AddToMap */
    xaml_ct_RunInitializer,      /* 23: RunInitializer */
};

static HRESULT WINAPI xaml_ct_winui3_get_FullName( void *iface, HSTRING *name )
{ return xaml_ct_get_FullName( &xaml_ct_from_winui3( iface )->lpVtbl, name ); }
static HRESULT WINAPI xaml_ct_winui3_get_Name( void *iface, HSTRING *name )
{ return xaml_ct_get_Name( &xaml_ct_from_winui3( iface )->lpVtbl, name ); }
static HRESULT WINAPI xaml_ct_winui3_ActivateInstance( void *iface, IInspectable **instance )
{ return xaml_ct_ActivateInstance( &xaml_ct_from_winui3( iface )->lpVtbl, instance ); }
static HRESULT WINAPI xaml_ct_winui3_get_UnderlyingType( void *iface, TypeName *type )
{ return xaml_ct_get_UnderlyingType( &xaml_ct_from_winui3( iface )->lpVtbl, type ); }

static const void *xaml_cached_type_winui3_vtbl[] =
{
    xaml_ct_winui3_QueryInterface,      /* 0 */
    xaml_ct_winui3_AddRef,              /* 1 */
    xaml_ct_winui3_Release,             /* 2 */
    xaml_ct_GetIids,                    /* 3 */
    xaml_ct_GetRuntimeClassName,        /* 4 */
    xaml_ct_GetTrustLevel,              /* 5 */
    xaml_ct_get_NullPtr,                /* 6: get_BaseType */
    xaml_ct_get_NullPtr,                /* 7: get_ContentProperty */
    xaml_ct_winui3_get_FullName,        /* 8: get_FullName */
    xaml_ct_get_False,                  /* 9: get_IsArray */
    xaml_ct_get_False,                  /* 10: get_IsCollection */
    xaml_ct_get_True,                   /* 11: get_IsConstructible */
    xaml_ct_get_False,                  /* 12: get_IsDictionary */
    xaml_ct_get_False,                  /* 13: get_IsMarkupExtension */
    xaml_ct_get_False,                  /* 14: get_IsEnum */
    xaml_ct_get_False,                  /* 15: get_IsBindable */
    xaml_ct_get_NullPtr,                /* 16: get_ItemType */
    xaml_ct_get_NullPtr,                /* 17: get_KeyType */
    xaml_ct_winui3_get_Name,            /* 18: get_Name */
    xaml_ct_winui3_ActivateInstance,    /* 19: ActivateInstance (WinUI 3) */
    xaml_ct_CreateFromString,           /* 20: CreateFromString */
    xaml_ct_GetMember,                  /* 21: GetMember */
    xaml_ct_RunInitializer,             /* 22: RunInitializer */
    xaml_ct_winui3_get_UnderlyingType,  /* 23: get_UnderlyingType */
};

static struct xaml_cached_type *xaml_cached_type_create( const WCHAR *full_name, WORD xbf_id )
{
    struct xaml_cached_type *t = calloc( 1, sizeof(*t) );
    const WCHAR *dot;
    if (!t) return NULL;
    t->lpVtbl = xaml_cached_type_uwp_vtbl;
    t->lpVtbl_winui3 = xaml_cached_type_winui3_vtbl;
    t->lpVtbl_weak_src = xaml_cached_type_wrs_vtbl;
    t->ref = 1;
    t->full_name = wcsdup( full_name );
    if (!t->full_name) { free( t ); return NULL; }
    dot = wcsrchr( t->full_name, '.' );
    t->short_name = dot ? dot + 1 : t->full_name;
    t->xbf_type_id = xbf_id;
    return t;
}

struct xaml_meta_cache_entry *xaml_meta_insert_locked( const WCHAR *full_name, WORD xbf_id )
{
    UINT32 bucket = xaml_meta_hash( full_name ) & (XAML_META_CACHE_SIZE - 1);
    struct xaml_meta_cache_entry *e;

    for (e = xaml_meta_cache[bucket]; e; e = e->next)
    {
        if (!wcscmp( e->full_name, full_name ))
        {
            if (xbf_id && !e->xbf_type_id) e->xbf_type_id = xbf_id;
            return e;
        }
    }

    e = calloc( 1, sizeof(*e) );
    if (!e) return NULL;
    e->full_name = wcsdup( full_name );
    if (!e->full_name) { free( e ); return NULL; }
    e->xbf_type_id = xbf_id;
    e->next = xaml_meta_cache[bucket];
    xaml_meta_cache[bucket] = e;
    return e;
}

static HRESULT xaml_metadata_cache_get_type( const WCHAR *full_name, void **out )
{
    struct xaml_meta_cache_entry *e;
    struct xaml_cached_type *ct = NULL;
    UINT32 bucket;

    if (!out) return E_POINTER;
    *out = NULL;
    if (!full_name || !full_name[0]) return E_INVALIDARG;

    InitOnceExecuteOnce( &xaml_meta_cache_once, init_xaml_meta_cache, NULL, NULL );
    bucket = xaml_meta_hash( full_name ) & (XAML_META_CACHE_SIZE - 1);

    AcquireSRWLockShared( &xaml_meta_cache_lock );
    for (e = xaml_meta_cache[bucket]; e; e = e->next)
    {
        if (!wcscmp( e->full_name, full_name ) && e->builtin_type)
        {
            ct = e->builtin_type;
            InterlockedIncrement( &ct->ref );
            ReleaseSRWLockShared( &xaml_meta_cache_lock );
            *out = ct;
            return S_OK;
        }
    }
    ReleaseSRWLockShared( &xaml_meta_cache_lock );

    AcquireSRWLockExclusive( &xaml_meta_cache_lock );
    e = xaml_meta_insert_locked( full_name, 0 );
    if (e)
    {
        if (!e->builtin_type)
            e->builtin_type = xaml_cached_type_create( full_name, e->xbf_type_id );
        ct = e->builtin_type;
        if (ct) InterlockedIncrement( &ct->ref );
    }
    ReleaseSRWLockExclusive( &xaml_meta_cache_lock );

    *out = ct;
    return ct ? S_OK : E_OUTOFMEMORY;
}

HRESULT xaml_metadata_cache_get_type_hstring( HSTRING full_name, void **out )
{
    const WCHAR *raw;
    if (!out) return E_POINTER;
    *out = NULL;
    if (!full_name) return S_OK;
    raw = WindowsGetStringRawBuffer( full_name, NULL );
    if (!raw || !raw[0]) return S_OK;
    return xaml_metadata_cache_get_type( raw, out );
}

struct xaml_type *uwp_provider_get_xaml_type_cached( struct xaml_metadata_provider *mp,
                                                            const WCHAR *owner_type_name )
{
    struct xaml_meta_cache_entry *e;
    struct xaml_type *xt = NULL;
    HSTRING h_owner = NULL;
    UINT32 bucket;

    if (!mp || !owner_type_name || !owner_type_name[0]) return NULL;
    InitOnceExecuteOnce( &xaml_meta_cache_once, init_xaml_meta_cache, NULL, NULL );
    bucket = xaml_meta_hash( owner_type_name ) & (XAML_META_CACHE_SIZE - 1);

    AcquireSRWLockShared( &xaml_meta_cache_lock );
    for (e = xaml_meta_cache[bucket]; e; e = e->next)
    {
        if (!wcscmp( e->full_name, owner_type_name ) && e->provider_key == mp && e->provider_type)
        {
            xt = e->provider_type;
            IInspectable_AddRef( (IInspectable *)xt );
            ReleaseSRWLockShared( &xaml_meta_cache_lock );
            return xt;
        }
    }
    ReleaseSRWLockShared( &xaml_meta_cache_lock );

    if (mp->lpVtbl->GetXamlTypeByFullName &&
        SUCCEEDED(WindowsCreateString( owner_type_name, wcslen( owner_type_name ), &h_owner )))
    {
        if (SUCCEEDED(mp->lpVtbl->GetXamlTypeByFullName( mp, h_owner, &xt )) && xt)
        {
            AcquireSRWLockExclusive( &xaml_meta_cache_lock );
            e = xaml_meta_insert_locked( owner_type_name, 0 );
            if (e && !e->provider_type)
            {
                e->provider_key = mp;
                e->provider_type = xt;
                IInspectable_AddRef( (IInspectable *)xt );
            }
            ReleaseSRWLockExclusive( &xaml_meta_cache_lock );
        }
        WindowsDeleteString( h_owner );
    }
    return xt;
}
