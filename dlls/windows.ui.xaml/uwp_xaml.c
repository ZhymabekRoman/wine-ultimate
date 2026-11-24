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
#include <excpt.h>
#include "wine/exception.h"
#include "private.h"
#include "xbf_known_types.h"
#include "weakreference.h"
#include "wincodec.h"
#define WIDL_using_Windows_ApplicationModel
#define WIDL_using_Windows_Management_Deployment
#include "windows.management.deployment.h"
#define WIDL_using_Windows_UI_Composition
#include "windows.ui.composition.h"
#include "uwp_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(xaml);


/* Faces we QI and call raw-indexed: the metadata provider, XAML type/member, property-change
 * notification, boxed IReference<T>, ICommand and generic event delegates. The objects behind
 * them are served by our own combase-side code and Calculator's .NET Native RCW projection,
 * whose vtables follow the WinRT ABI. Naming each slot we call lets callers use typed calls;
 * earlier, uncalled slots stay as void * placeholders. */
struct xaml_metadata_provider;



struct notify_property_changed;

struct property_changed_args;

struct reference_int32;

struct command_iface;

/* Generic event delegate: { QueryInterface, AddRef, Release, Invoke(sender, args) }. */
struct xaml_event_handler;

/* ============================================================================
 * 1. PRI v2 (mrm_pri2) Container Reader
 * ============================================================================ */

const BYTE *g_pri_data = NULL;
DWORD g_pri_size = 0;
INIT_ONCE g_pri_once = INIT_ONCE_STATIC_INIT;

const BYTE *g_winui_pri_data = NULL;
DWORD g_winui_pri_size = 0;
BOOL g_winui_pri_probed = FALSE;

BOOL WINAPI init_pri_once( INIT_ONCE *once, void *param, void **ctx )
{
    WCHAR pri_path[MAX_PATH];
    WCHAR *slash;

    GetModuleFileNameW( NULL, pri_path, MAX_PATH );
    slash = wcsrchr( pri_path, '\\' );
    if (slash) *(slash + 1) = 0;
    lstrcatW( pri_path, L"resources.pri" );
    pri_map_file_to( pri_path, &g_pri_data, &g_pri_size );
    return TRUE;
}

BOOL WINAPI init_pri_xbf_cat_once( INIT_ONCE *once, void *param, void **ctx )
{
    DWORD pri_sz = 0;
    const BYTE *pri = pri_get_data( &pri_sz );
    WCHAR exe_dir[MAX_PATH], pattern[MAX_PATH];
    WCHAR *slash;

    uwp_pri_enum_xbfs_in_blob( pri, pri_sz, pri_xbf_collect_cb, &g_pri_cat );

    if (GetModuleFileNameW( NULL, exe_dir, ARRAY_SIZE(exe_dir) ) && (slash = wcsrchr( exe_dir, '\\' )))
    {
        WIN32_FIND_DATAW fd;
        HANDLE hfind;
        *slash = 0;
        scan_disk_xbfs_in_dir( exe_dir, "", &g_pri_cat );
        if (wcslen( exe_dir ) + 3 < ARRAY_SIZE(pattern))
        {
            lstrcpyW( pattern, exe_dir );
            lstrcatW( pattern, L"\\*" );
            hfind = FindFirstFileW( pattern, &fd );
            if (hfind != INVALID_HANDLE_VALUE)
            {
                do
                {
                    if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                        wcscmp( fd.cFileName, L"." ) && wcscmp( fd.cFileName, L".." ) &&
                        wcsicmp( fd.cFileName, L"dependencies" ))
                    {
                        WCHAR sub_dir[MAX_PATH];
                        char sub_a[128] = {0};
                        if (wcslen( exe_dir ) + 1 + wcslen( fd.cFileName ) < ARRAY_SIZE(sub_dir))
                        {
                            lstrcpyW( sub_dir, exe_dir );
                            lstrcatW( sub_dir, L"\\" );
                            lstrcatW( sub_dir, fd.cFileName );
                            WideCharToMultiByte( CP_UTF8, 0, fd.cFileName, -1, sub_a, sizeof(sub_a) - 1, NULL, NULL );
                            scan_disk_xbfs_in_dir( sub_dir, sub_a, &g_pri_cat );
                        }
                    }
                } while (FindNextFileW( hfind, &fd ));
                FindClose( hfind );
            }
        }
    }
    TRACE( "PRI XBF catalog indexed %lu .xbf entries\n", (unsigned long)g_pri_cat.cnt );
    return TRUE;
}

void ensure_winui_pri_cat( void )
{
    if (!g_winui_pri_cat_done)
    {
        DWORD sz = 0;
        const BYTE *pri = winui_pri_get_data( &sz );
        if (pri)
        {
            g_winui_pri_cat_done = TRUE;
            uwp_pri_enum_xbfs_in_blob( pri, sz, pri_xbf_collect_cb, &g_winui_pri_cat );
            TRACE( "WinUI PRI XBF catalog indexed %lu .xbf entries\n", (unsigned long)g_winui_pri_cat.cnt );
        }
    }
}

BOOL WINAPI init_xaml_meta_cache( INIT_ONCE *once, void *param, void **ctx )
{
    size_t i;
    AcquireSRWLockExclusive( &xaml_meta_cache_lock );
    for (i = 0; i < ARRAY_SIZE(xbf_known_types); i++)
    {
        if (xbf_known_types[i])
            xaml_meta_insert_locked( xbf_known_types[i], (WORD)(0x8000 | i) );
    }
    ReleaseSRWLockExclusive( &xaml_meta_cache_lock );
    return TRUE;
}

int uwp_resolve_button_calc_code( struct uwp_element *btn )
{
    IInspectable *val = NULL;
    INT32 code = -1;
    int i;

    if (!btn) return -1;

    /* 1. Read ButtonId dynamically from the app's IXamlMetadataProvider */
    if (btn->class_name[0])
    {
        IInspectable *target = btn->outer ? btn->outer : &btn->IInspectable_iface;
        if (uwp_get_custom_xaml_member( uwp_get_app_outer(), btn->class_name, L"ButtonId", target, &val ) && val)
        {
            BOOL ok = uwp_unbox_enum_int32( val, &code, TRUE );
            IInspectable_Release( val );
            if (ok && code >= 0)
            {
                TRACE( "uwp_resolve_button_calc_code: provider ButtonId on %s -> %d\n",
                       debugstr_w(btn->name), (int)code );
                return (int)code;
            }
        }
    }

    /* 2. Read CommandParameter set by CalculatorButton::OnButtonIdPropertyChanged */
    if (btn->command_param && uwp_unbox_enum_int32( btn->command_param, &code, TRUE ) && code >= 0)
    {
        TRACE( "uwp_resolve_button_calc_code: CommandParameter on %s -> %d\n",
               debugstr_w(btn->name), (int)code );
        return (int)code;
    }

    /* 3. Read boxed ButtonId enum from element's DependencyProperty bag */
    for (i = 0; i < btn->dp_cnt; i++)
    {
        if (btn->dp_store[i].value &&
            uwp_unbox_enum_int32( btn->dp_store[i].value, &code, FALSE ) && code >= 0)
        {
            TRACE( "uwp_resolve_button_calc_code: dp_store[%d] on %s -> %d\n",
                   i, debugstr_w(btn->name), (int)code );
            return (int)code;
        }
    }

    return -1;
}

/* DependencyPropertyChangedCallback attached to CalculationResult controls */
static HRESULT WINAPI calc_result_dp_cb_QI( void *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IDependencyPropertyChangedCallback_UWP ))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI calc_result_dp_cb_AddRef( void *iface ) { return 2; }
static ULONG WINAPI calc_result_dp_cb_Release( void *iface ) { return 1; }
static HRESULT WINAPI calc_result_dp_cb_Invoke( void *iface, void *sender, void *dp )
{
    struct uwp_element *res = uwp_elem_from_any( sender );
    IInspectable *val = NULL;
    WCHAR buf[256] = {0};

    if (!res || !dp) return S_OK;
    if (SUCCEEDED(uwp_do_GetValue( &res->IDependencyObject_iface, dp, &val )) && val)
    {
        if (uwp_unbox_string( val, buf, ARRAY_SIZE(buf) ) && buf[0])
        {
            lstrcpynW( res->text, buf, ARRAY_SIZE(res->text) );
            if (res->tpl_normal_output)
            {
                HSTRING htxt = NULL;
                if (SUCCEEDED(WindowsCreateString( buf, wcslen(buf), &htxt )) && htxt)
                {
                    uwp_tb_put_Text( &res->tpl_normal_output->ITextBlock_iface, htxt );
                    WindowsDeleteString( htxt );
                }
                else
                {
                    lstrcpynW( res->tpl_normal_output->text, buf, ARRAY_SIZE(res->tpl_normal_output->text) );
                    uwp_invalidate_window();
                }
            }
            else
            {
                uwp_invalidate_window();
            }
        }
        IInspectable_Release( val );
    }
    return S_OK;
}
static const void *g_calc_result_dp_cb_vtbl[] =
{
    calc_result_dp_cb_QI,
    calc_result_dp_cb_AddRef,
    calc_result_dp_cb_Release,
    calc_result_dp_cb_Invoke,
};
static struct { const void *vtbl; } g_calc_result_dp_cb = { g_calc_result_dp_cb_vtbl };

BOOL uwp_is_display_result_element( const struct uwp_element *el )
{
    if (!el) return FALSE;
    if (!wcscmp( el->name, L"Results" ) || !wcscmp( el->name, L"NormalOutput" ))
        return TRUE;
    if (wcsstr( el->class_name, L"CalculationResult" ) != NULL)
        return TRUE;
    return FALSE;
}

static void uwp_bind_calc_result_elements( void )
{
    struct uwp_element *scan, *matches[32];
    int mcnt = 0, m;
    if (!g_calc_vm.display_dp)
        dependency_property_create( NULL, &g_calc_vm.display_dp );
    if (!g_calc_vm.display_dp) return;

    AcquireSRWLockExclusive( &g_all_uwp_elems_lock );
    for (scan = g_all_uwp_elems_head; scan && mcnt < (int)ARRAY_SIZE(matches); scan = scan->next_reg)
    {
        if (uwp_is_display_result_element( scan ))
            matches[mcnt++] = scan;
    }
    ReleaseSRWLockExclusive( &g_all_uwp_elems_lock );

    for (m = 0; m < mcnt; m++)
    {
        BOOL already = FALSE;
        int i;
        scan = matches[m];
        for (i = 0; i < scan->dp_cb_cnt; i++)
        {
            if (scan->dp_cbs[i].dp == g_calc_vm.display_dp) { already = TRUE; break; }
        }
        if (!already)
        {
            INT64 tok = 0;
            uwp_do2_RegisterPropertyChangedCallback( &scan->IDependencyObject2_iface,
                                                     g_calc_vm.display_dp, &g_calc_result_dp_cb, &tok );
            TRACE( "uwp_bind_calc_result_elements: bound DP changed callback on %p (%s)\n",
                   scan, debugstr_w(scan->name) );
        }
    }
}

static void uwp_push_display_value_to_results( const WCHAR *s )
{
    IInspectable *boxed = NULL;
    struct uwp_element *scan, *matches[32];
    int mcnt = 0, m;

    if (!s || !s[0]) return;
    uwp_bind_calc_result_elements();
    if (!g_calc_vm.display_dp) return;

    boxed = box_string_value( s );
    if (!boxed) return;

    AcquireSRWLockExclusive( &g_all_uwp_elems_lock );
    for (scan = g_all_uwp_elems_head; scan && mcnt < (int)ARRAY_SIZE(matches); scan = scan->next_reg)
    {
        if (uwp_is_display_result_element( scan ))
            matches[mcnt++] = scan;
    }
    ReleaseSRWLockExclusive( &g_all_uwp_elems_lock );

    for (m = 0; m < mcnt; m++)
    {
        IInspectable *target;
        scan = matches[m];
        target = scan->outer ? scan->outer : &scan->IInspectable_iface;
        if (scan->class_name[0] && uwp_get_app_outer())
            uwp_set_custom_xaml_member( uwp_get_app_outer(), scan->class_name, L"DisplayValue", target, s );
        uwp_do_SetValue( &scan->IDependencyObject_iface, g_calc_vm.display_dp, boxed );
    }
    IInspectable_Release( boxed );
}

static BOOL uwp_vm_read_display_value( IInspectable *vm, const WCHAR *vm_class_name, WCHAR *out_buf, size_t max_chars )
{
    IInspectable *val = NULL;
    HSTRING hval = NULL;
    HRESULT hr = E_FAIL;

    if (!vm || !out_buf || !max_chars) return FALSE;
    out_buf[0] = 0;

    /* 1. Try IXamlMetadataProvider member "DisplayValue" when provider is available */
    if (vm_class_name && vm_class_name[0] && uwp_get_app_outer() &&
        uwp_get_custom_xaml_member( uwp_get_app_outer(), vm_class_name, L"DisplayValue", vm, &val ) && val)
    {
        BOOL ok = uwp_unbox_string( val, out_buf, (int)max_chars );
        IInspectable_Release( val );
        if (ok && out_buf[0]) return TRUE;
    }

    /* 2. Try unboxing directly if VM is a PropertyValue string */
    if (uwp_unbox_string( vm, out_buf, (int)max_chars ) && out_buf[0])
        return TRUE;

    /* 3. Fallback to default interface slot 7 get_DisplayValue(HSTRING*) */
    __TRY
    {
        HRESULT (WINAPI *get_disp)(void *, HSTRING *) = (void *)(*(void ***)vm)[7];
        if (get_disp) hr = get_disp( vm, &hval );
    }
    __EXCEPT_ALL
    {
        hr = E_FAIL;
        hval = NULL;
    }
    __ENDTRY;

    if (SUCCEEDED(hr) && hval)
    {
        const WCHAR *s = WindowsGetStringRawBuffer( hval, NULL );
        if (s && s[0]) lstrcpynW( out_buf, s, (int)max_chars );
        WindowsDeleteString( hval );
        return out_buf[0] != 0;
    }
    return FALSE;
}

void uwp_sync_calc_display_from_vm( void )
{
    WCHAR buf[256] = {0};

    if (!g_calc_vm.vm) return;
    if (uwp_vm_read_display_value( g_calc_vm.vm, g_calc_vm.vm_class_name, buf, ARRAY_SIZE(buf) ))
    {
        TRACE( "uwp_sync_calc_display_from_vm: DisplayValue = %s\n", debugstr_w(buf) );
        uwp_push_display_value_to_results( buf );
    }
}

/* INotifyPropertyChanged listener for ViewModel DisplayValue */
static HRESULT WINAPI calc_vm_npc_QI( void *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IPropertyChangedEventHandler_UWP ))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI calc_vm_npc_AddRef( void *iface ) { return 2; }
static ULONG WINAPI calc_vm_npc_Release( void *iface ) { return 1; }
static HRESULT WINAPI calc_vm_npc_Invoke( void *iface, void *sender, void *args )
{
    HSTRING hprop = NULL;
    const WCHAR *pname = NULL;
    BOOL match = FALSE;

    if (args)
    {
        void *pcea = NULL;
        if (SUCCEEDED(IUnknown_QueryInterface( (IUnknown *)args, &IID_IPropertyChangedEventArgs_UWP, &pcea )) && pcea)
        {
            struct property_changed_args *pca = (struct property_changed_args *)pcea;
            if (pca->lpVtbl->get_PropertyName && SUCCEEDED(pca->lpVtbl->get_PropertyName( pca, &hprop )) && hprop)
                pname = WindowsGetStringRawBuffer( hprop, NULL );
            IUnknown_Release( (IUnknown *)pcea );
        }
    }
    if (!pname || !pname[0] || !wcscmp( pname, L"DisplayValue" ))
        match = TRUE;
    if (hprop) WindowsDeleteString( hprop );

    if (match)
        uwp_sync_calc_display_from_vm();
    return S_OK;
}
static const void *g_calc_vm_npc_vtbl[] =
{
    calc_vm_npc_QI,
    calc_vm_npc_AddRef,
    calc_vm_npc_Release,
    calc_vm_npc_Invoke,
};
static struct { const void *vtbl; } g_calc_vm_npc_handler = { g_calc_vm_npc_vtbl };

void uwp_calc_vm_teardown( BOOL final_cleanup )
{
    if (g_calc_vm.npc)
    {
        if (g_calc_vm.npc_hooked)
        {
            struct notify_property_changed *npc = (struct notify_property_changed *)g_calc_vm.npc;
            if (npc->lpVtbl->remove_PropertyChanged)
            {
                __TRY { npc->lpVtbl->remove_PropertyChanged( npc, g_calc_vm.npc_token ); }
                __EXCEPT_ALL { }
                __ENDTRY;
            }
        }
        IInspectable_Release( g_calc_vm.npc );
        g_calc_vm.npc = NULL;
        g_calc_vm.npc_hooked = FALSE;
        g_calc_vm.npc_token.value = 0;
    }
    if (g_calc_vm.cmd)
    {
        IInspectable_Release( g_calc_vm.cmd );
        g_calc_vm.cmd = NULL;
    }
    if (g_calc_vm.vm)
    {
        IInspectable_Release( g_calc_vm.vm );
        g_calc_vm.vm = NULL;
    }
    g_calc_vm.vm_class_name[0] = 0;
    g_calc_vm.pumped = FALSE;
    if (final_cleanup && g_calc_vm.display_dp)
    {
        IDependencyProperty_Release( g_calc_vm.display_dp );
        g_calc_vm.display_dp = NULL;
    }
}

/* Resolve ViewModel type name (and optionally activate via IXamlType) from the page/view XBF
 * metadata via IXamlMetadataProvider::GetXamlTypeByFullName(view_type)->GetMember("Model")->get_Type(). */
static void uwp_resolve_vm_type_from_metadata( WCHAR *out_type_name, size_t max_chars, IInspectable **out_instance )
{
    static const WCHAR * const member_names[] = { L"Model", L"ViewModel", L"DataContext" };
    void *app_outer = uwp_get_app_outer();
    void *provider = NULL;
    WCHAR candidates[8][128];
    int ncand = 0, c;

    if (out_type_name && max_chars) out_type_name[0] = 0;
    if (out_instance) *out_instance = NULL;

    /* Collect view/page candidate types from parsed XBF tree */
    AcquireSRWLockExclusive( &g_all_uwp_elems_lock );
    if (g_navigated_page_el)
    {
        int r;
        for (r = 0; r < g_navigated_page_el->xbf_ref_cnt && ncand < (int)ARRAY_SIZE(candidates); r++)
        {
            if (!g_navigated_page_el->xbf_refs[r].is_resource_path &&
                g_navigated_page_el->xbf_refs[r].full_name[0])
                lstrcpynW( candidates[ncand++], g_navigated_page_el->xbf_refs[r].full_name, 128 );
        }
        if (g_navigated_page_el->x_class[0] && ncand < (int)ARRAY_SIZE(candidates))
            lstrcpynW( candidates[ncand++], g_navigated_page_el->x_class, 128 );
    }
    {
        struct uwp_element *scan;
        for (scan = g_all_uwp_elems_head; scan && ncand < (int)ARRAY_SIZE(candidates); scan = scan->next_reg)
        {
            const WCHAR *cls = scan->x_class[0] ? scan->x_class : scan->class_name;
            if ((scan->kind == UWP_KIND_USER_CONTROL || scan->kind == UWP_KIND_PAGE) &&
                cls[0] && wcsncmp( cls, L"Windows.UI.Xaml.", 16 ) != 0 &&
                wcsncmp( cls, L"Microsoft.UI.Xaml.", 18 ) != 0)
            {
                int k;
                BOOL dup = FALSE;
                for (k = 0; k < ncand; k++) if (!wcscmp( candidates[k], cls )) { dup = TRUE; break; }
                if (!dup) lstrcpynW( candidates[ncand++], cls, 128 );
            }
        }
    }
    ReleaseSRWLockExclusive( &g_all_uwp_elems_lock );

    if (app_outer && ncand > 0 &&
        SUCCEEDED(IInspectable_QueryInterface( (IInspectable *)app_outer, &IID_IXamlMetadataProvider_UWP_g, &provider )) && provider)
    {
        struct xaml_metadata_provider *mp = (struct xaml_metadata_provider *)provider;
        for (c = 0; c < ncand && (!out_type_name || !out_type_name[0]); c++)
        {
            HSTRING h_cls = NULL;
            void *view_xt = NULL;
            if (mp->lpVtbl->GetXamlTypeByFullName && SUCCEEDED(WindowsCreateString( candidates[c], wcslen(candidates[c]), &h_cls )))
            {
                if (SUCCEEDED(mp->lpVtbl->GetXamlTypeByFullName( mp, h_cls, (struct xaml_type **)&view_xt )) && view_xt)
                {
                    struct xaml_type *vxt = (struct xaml_type *)view_xt;
                    size_t m;
                    for (m = 0; vxt->lpVtbl->GetMember && m < ARRAY_SIZE(member_names); m++)
                    {
                        HSTRING h_mem = NULL;
                        void *xm = NULL;
                        if (SUCCEEDED(WindowsCreateString( member_names[m], wcslen(member_names[m]), &h_mem )))
                        {
                            if (SUCCEEDED(vxt->lpVtbl->GetMember( vxt, h_mem, (struct xaml_member **)&xm )) && xm)
                            {
                                struct xaml_member *xm2 = (struct xaml_member *)xm;
                                void *vm_xt = NULL;
                                if (xm2->lpVtbl->get_Type && SUCCEEDED(xm2->lpVtbl->get_Type( xm2, (struct xaml_type **)&vm_xt )) && vm_xt)
                                {
                                    /* Follow nested CalculatorViewModel if Model is ApplicationViewModel */
                                    struct xaml_type *vmxt = (struct xaml_type *)vm_xt;
                                    HSTRING h_sub = NULL;
                                    void *sub_xm = NULL;
                                    if (vmxt->lpVtbl->GetMember && SUCCEEDED(WindowsCreateString( L"CalculatorViewModel", 19, &h_sub )))
                                    {
                                        if (SUCCEEDED(vmxt->lpVtbl->GetMember( vmxt, h_sub, (struct xaml_member **)&sub_xm )) && sub_xm)
                                        {
                                            struct xaml_member *sxm = (struct xaml_member *)sub_xm;
                                            void *sub_xt = NULL;
                                            if (sxm->lpVtbl->get_Type && SUCCEEDED(sxm->lpVtbl->get_Type( sxm, (struct xaml_type **)&sub_xt )) && sub_xt)
                                            {
                                                IInspectable_Release( (IInspectable *)vm_xt );
                                                vm_xt = sub_xt;
                                            }
                                            IInspectable_Release( (IInspectable *)sub_xm );
                                        }
                                        WindowsDeleteString( h_sub );
                                    }
                                    {
                                        HSTRING h_fn = NULL;
                                        if (vmxt->lpVtbl->get_FullName && SUCCEEDED(vmxt->lpVtbl->get_FullName( vmxt, &h_fn )) && h_fn)
                                        {
                                            const WCHAR *fn = WindowsGetStringRawBuffer( h_fn, NULL );
                                            if (fn && fn[0] && out_type_name && max_chars)
                                            {
                                                lstrcpynW( out_type_name, fn, (int)max_chars );
                                                TRACE( "uwp_resolve_vm_type_from_metadata: %s.%s -> %s\n",
                                                       debugstr_w(candidates[c]), debugstr_w(member_names[m]),
                                                       debugstr_w(out_type_name) );
                                            }
                                            WindowsDeleteString( h_fn );
                                        }
                                    }
                                    if (out_instance && !*out_instance)
                                    {
                                        if (vmxt->lpVtbl->ActivateInstance)
                                        {
                                            __TRY { vmxt->lpVtbl->ActivateInstance( vmxt, out_instance ); }
                                            __EXCEPT_ALL { *out_instance = NULL; }
                                            __ENDTRY;
                                        }
                                    }
                                    IInspectable_Release( (IInspectable *)vm_xt );
                                }
                                IInspectable_Release( (IInspectable *)xm );
                            }
                            WindowsDeleteString( h_mem );
                        }
                        if (out_type_name && out_type_name[0]) break;
                    }
                    IInspectable_Release( (IInspectable *)view_xt );
                }
                WindowsDeleteString( h_cls );
            }
        }
        IInspectable_Release( (IInspectable *)provider );
    }

    /* Single fallback call site when IXamlMetadataProvider is unavailable */
    if (out_type_name && max_chars && !out_type_name[0] && xaml_get_app_caps()->vm_type[0])
        lstrcpynW( out_type_name, xaml_get_app_caps()->vm_type, (int)max_chars );
}

static IInspectable *uwp_make_button_pressed_args( int code );

void uwp_calc_vm_set_instance( IInspectable *vm, const WCHAR *class_name )
{
    void *npc = NULL;
    IInspectable *cmd_prop = NULL;
    void *cmd_iface = NULL;

    if (g_calc_vm.vm == vm && vm != NULL)
    {
        uwp_bind_calc_result_elements();
        uwp_sync_calc_display_from_vm();
        return;
    }

    uwp_calc_vm_teardown( FALSE );
    if (!vm) return;

    g_calc_vm.vm = vm;
    IInspectable_AddRef( vm );

    if (class_name && class_name[0])
        lstrcpynW( g_calc_vm.vm_class_name, class_name, ARRAY_SIZE(g_calc_vm.vm_class_name) );
    else
    {
        HSTRING hcls = NULL;
        if (SUCCEEDED(IInspectable_GetRuntimeClassName( vm, &hcls )) && hcls)
        {
            const WCHAR *raw = WindowsGetStringRawBuffer( hcls, NULL );
            if (raw) lstrcpynW( g_calc_vm.vm_class_name, raw, ARRAY_SIZE(g_calc_vm.vm_class_name) );
            WindowsDeleteString( hcls );
        }
    }

    /* Subscribe to INotifyPropertyChanged and retain token + interface for teardown */
    if (SUCCEEDED(IInspectable_QueryInterface( vm, &IID_INotifyPropertyChanged_UWP, &npc )) && npc)
    {
        struct notify_property_changed *npcp = (struct notify_property_changed *)npc;
        EventRegistrationToken tok = {0};
        if (npcp->lpVtbl->add_PropertyChanged &&
            SUCCEEDED(npcp->lpVtbl->add_PropertyChanged( npcp, &g_calc_vm_npc_handler, &tok )))
        {
            g_calc_vm.npc = (IInspectable *)npc;
            g_calc_vm.npc_token = tok;
            g_calc_vm.npc_hooked = TRUE;
        }
        else
        {
            IUnknown_Release( (IUnknown *)npc );
        }
    }

    /* Resolve ICommand face via IXamlMember ("ButtonPressed") or direct QI on VM */
    if (g_calc_vm.vm_class_name[0] && uwp_get_app_outer() &&
        uwp_get_custom_xaml_member( uwp_get_app_outer(), g_calc_vm.vm_class_name, L"ButtonPressed", vm, &cmd_prop ) && cmd_prop)
    {
        HRESULT qhr = IInspectable_QueryInterface( cmd_prop, &IID_ICommand_UWP2, &cmd_iface );
        if (FAILED(qhr))
            qhr = IInspectable_QueryInterface( cmd_prop, &IID_ICommand_UWP, &cmd_iface );
        TRACE( "uwp_calc_vm_set_instance: ButtonPressed=%p QI(ICommand) -> %#lx (%p)\n",
               (void *)cmd_prop, qhr, cmd_iface );
        if (SUCCEEDED(qhr) && cmd_iface)
            g_calc_vm.cmd = (IInspectable *)cmd_iface;
        else
        {
            g_calc_vm.cmd = cmd_prop;
            IInspectable_AddRef( cmd_prop );
        }
        IInspectable_Release( cmd_prop );
    }
    if (!g_calc_vm.cmd &&
        (SUCCEEDED(IInspectable_QueryInterface( vm, &IID_ICommand_UWP2, &cmd_iface )) ||
         SUCCEEDED(IInspectable_QueryInterface( vm, &IID_ICommand_UWP, &cmd_iface ))) && cmd_iface)
        g_calc_vm.cmd = (IInspectable *)cmd_iface;

    if (g_calc_vm.cmd)
    {
        IInspectable *mode_args = uwp_make_button_pressed_args( 200 );
        if (mode_args)
        {
            HRESULT mhr = E_FAIL;
            __TRY
            {
                struct command_iface *cmd = (struct command_iface *)g_calc_vm.cmd;
                if (cmd->lpVtbl->Execute) mhr = cmd->lpVtbl->Execute( cmd, mode_args );
            }
            __EXCEPT_ALL { mhr = E_FAIL; }
            __ENDTRY;
            TRACE( "uwp_calc_vm_set_instance: ModeBasic(200) via ICommand::Execute -> %#lx\n", mhr );
            IInspectable_Release( mode_args );
        }
    }

    g_calc_vm.pumped = TRUE;
    uwp_sync_calc_display_from_vm();
}

void uwp_calc_vm_ensure( void )
{
    WCHAR vm_type_name[128] = {0};
    IInspectable *vm_inst = NULL;
    HSTRING h = NULL;

    if (g_calc_vm.vm)
    {
        uwp_bind_calc_result_elements();
        return;
    }

    uwp_resolve_vm_type_from_metadata( vm_type_name, ARRAY_SIZE(vm_type_name), &vm_inst );
    if (!vm_inst && vm_type_name[0] &&
        SUCCEEDED(WindowsCreateString( vm_type_name, wcslen(vm_type_name), &h )) && h)
    {
        HRESULT hr = RoActivateInstance( h, &vm_inst );
        TRACE( "uwp_calc_vm_ensure: RoActivateInstance(%s) -> %#lx vm %p\n",
               debugstr_w(vm_type_name), hr, (void *)vm_inst );
        WindowsDeleteString( h );
        if (FAILED(hr)) vm_inst = NULL;
    }

    if (vm_inst)
    {
        uwp_calc_vm_set_instance( vm_inst, vm_type_name );
        IInspectable_Release( vm_inst );
    }
}

static IInspectable *uwp_make_button_pressed_args( int code )
{
    static const WCHAR args_cls[] = L"CalculatorApp.ViewModel.Common.CalculatorButtonPressedEventArgs";
    HSTRING h_cls = NULL;
    IInspectable *args = NULL;
    HRESULT hr;

    if (SUCCEEDED(WindowsCreateString( args_cls, ARRAY_SIZE(args_cls) - 1, &h_cls )) && h_cls)
    {
        IActivationFactory *af = NULL;
        hr = RoGetActivationFactory( h_cls, &IID_IActivationFactory, (void **)&af );
        TRACE( "uwp_make_button_pressed_args: RoGetActivationFactory -> %#lx af=%p\n", hr, (void *)af );
        if (SUCCEEDED(hr) && af)
        {
            ULONG iid_cnt = 0, i;
            IID *iids = NULL;
            if (SUCCEEDED(af->lpVtbl->GetIids( af, &iid_cnt, &iids )) && iids)
            {
                for (i = 0; i < iid_cnt; i++)
                {
                    void *fac = NULL;
                    if (IsEqualGUID( &iids[i], &IID_IActivationFactory )) continue;
                    if (SUCCEEDED(IActivationFactory_QueryInterface( af, &iids[i], &fac )) && fac)
                    {
                        HRESULT chr = E_FAIL;
                        __TRY
                        {
                            HRESULT (WINAPI *create_inst)(void *, HSTRING, INT32, IInspectable **) =
                                (void *)(*(void ***)fac)[6];
                            if (create_inst) chr = create_inst( fac, NULL, code, &args );
                        }
                        __EXCEPT_ALL { chr = E_FAIL; args = NULL; }
                        __ENDTRY;
                        TRACE( "uwp_make_button_pressed_args: iid[%lu] %s CreateInstance(%d) -> %#lx args=%p\n",
                               i, debugstr_guid(&iids[i]), code, chr, (void *)args );
                        IUnknown_Release( (IUnknown *)fac );
                        if (SUCCEEDED(chr) && args) break;
                        args = NULL;
                    }
                }
                CoTaskMemFree( iids );
            }
            IActivationFactory_Release( af );
        }
        WindowsDeleteString( h_cls );
    }
    return args;
}

void uwp_calc_vm_send( int code, IInspectable *cmd_param )
{
    WCHAR pre_disp[256] = {0}, post_disp[256] = {0};

    uwp_calc_vm_ensure();
    if (!g_calc_vm.vm) return;

    uwp_vm_read_display_value( g_calc_vm.vm, g_calc_vm.vm_class_name, pre_disp, ARRAY_SIZE(pre_disp) );
    TRACE( "uwp_calc_vm_send: code %d pre DisplayValue = %s\n", code, debugstr_w(pre_disp) );

    if (g_calc_vm.cmd)
    {
        IInspectable *args = uwp_make_button_pressed_args( code );
        IInspectable *param = args ? args : (cmd_param ? cmd_param : box_int32_value( code ));
        HRESULT chr = E_FAIL;
        if (!args && cmd_param) IInspectable_AddRef( cmd_param );
        __TRY
        {
            struct command_iface *cmd = (struct command_iface *)g_calc_vm.cmd;
            if (cmd->lpVtbl->Execute) chr = cmd->lpVtbl->Execute( cmd, param );
        }
        __EXCEPT_ALL
        {
            chr = E_FAIL;
        }
        __ENDTRY;
        if (param) IInspectable_Release( param );
        uwp_vm_read_display_value( g_calc_vm.vm, g_calc_vm.vm_class_name, post_disp, ARRAY_SIZE(post_disp) );
        TRACE( "uwp_calc_vm_send: code %d (args=%p) Execute -> %#lx post DisplayValue = %s\n",
               code, (void *)args, chr, debugstr_w(post_disp) );
    }

    uwp_sync_calc_display_from_vm();
}

static void register_uwp_elem( struct uwp_element *el )
{
    AcquireSRWLockExclusive( &g_all_uwp_elems_lock );
    el->next_reg = g_all_uwp_elems_head;
    g_all_uwp_elems_head = el;
    g_all_uwp_elem_cnt++;
    ReleaseSRWLockExclusive( &g_all_uwp_elems_lock );
}

static const GUID IID_UWP_ELEM_SELF =
    { 0x7a1b2c3d, 0x4e5f, 0x4678, { 0x9a, 0xbc, 0xde, 0xf0, 0x12, 0x34, 0x56, 0x79 } };

struct uwp_element *uwp_elem_from_any( void *ptr )
{
    struct uwp_element *el;
    void *self_iface = NULL;
    void *inner = NULL;
    if (!ptr || (ULONG_PTR)ptr < 0x10000) return NULL;
    AcquireSRWLockExclusive( &g_all_uwp_elems_lock );
    for (el = g_all_uwp_elems_head; el; el = el->next_reg)
    {
        if ((void *)ptr == (void *)el->outer ||
            (void *)ptr == (void *)el->provider_inst ||
            ((char *)ptr >= (char *)el && (char *)ptr < (char *)(el + 1)))
        {
            ReleaseSRWLockExclusive( &g_all_uwp_elems_lock );
            return el;
        }
    }
    ReleaseSRWLockExclusive( &g_all_uwp_elems_lock );
    /* .NET Native hands us __ComObject RCWs that answer QI from their own
     * projection table (only IUnknown): unwrap get_Inner (slot 6) /
     * get_InnerIUnknown (slot 7) to reach our CCW face, then resolve that.
     * Only when the SELF identity QI failed (i.e. ptr is a foreign RCW). */
    if (SUCCEEDED(IUnknown_QueryInterface( (IUnknown *)ptr, &IID_UWP_ELEM_SELF, &self_iface )) && self_iface)
    {
        void *probe_ptr = self_iface;
        AcquireSRWLockExclusive( &g_all_uwp_elems_lock );
        for (el = g_all_uwp_elems_head; el; el = el->next_reg)
        {
            if (probe_ptr == (void *)&el->nondelegating_iface ||
                ((char *)probe_ptr >= (char *)el && (char *)probe_ptr < (char *)(el + 1)))
            {
                ReleaseSRWLockExclusive( &g_all_uwp_elems_lock );
                IUnknown_Release( (IUnknown *)self_iface );
                return el;
            }
        }
        ReleaseSRWLockExclusive( &g_all_uwp_elems_lock );
        IUnknown_Release( (IUnknown *)self_iface );
    }
    if (ptr != uwp_get_app_outer())
    {
        static const GUID IID_IResourceDictionary_UWP_g =
            { 0x44a9796f, 0x723e, 0x4fdf, { 0xa2, 0x18, 0x03, 0x3e, 0x75, 0xb0, 0xc0, 0x84 } };
        static const GUID IID_IResourceDictionary_WinUI_g =
            { 0x1b690975, 0xa710, 0x5783, { 0xa6, 0xe1, 0x15, 0x83, 0x6f, 0x61, 0x86, 0xc2 } };
        void *pv_iface = NULL;
        void *ui_iface = NULL;
        void *cand = NULL;
        HSTRING hcls = NULL;
        HRESULT cls_hr;
        /* Boxed primitive PropertyValues and ResourceDictionaries are never UIElements; do not probe vtbl[6]/[7] on them */
        if ((SUCCEEDED(IUnknown_QueryInterface( (IUnknown *)ptr, &IID_IPropertyValue_g, &pv_iface )) && pv_iface) ||
            (SUCCEEDED(IUnknown_QueryInterface( (IUnknown *)ptr, &IID_IResourceDictionary_UWP_g, &pv_iface )) && pv_iface) ||
            (SUCCEEDED(IUnknown_QueryInterface( (IUnknown *)ptr, &IID_IResourceDictionary_WinUI_g, &pv_iface )) && pv_iface))
        {
            IUnknown_Release( (IUnknown *)pv_iface );
            return NULL;
        }
        /* If ptr answers IUIElement, resolve directly via IID_UWP_ELEM_SELF or table without probing vtbl[6] */
        if (SUCCEEDED(IUnknown_QueryInterface( (IUnknown *)ptr, &IID_IUIElement_UWP, &ui_iface )) && ui_iface)
        {
            if (SUCCEEDED(IUnknown_QueryInterface( (IUnknown *)ui_iface, &IID_UWP_ELEM_SELF, &self_iface )) && self_iface)
            {
                AcquireSRWLockExclusive( &g_all_uwp_elems_lock );
                for (el = g_all_uwp_elems_head; el; el = el->next_reg)
                {
                    if (self_iface == (void *)&el->nondelegating_iface ||
                        ((char *)self_iface >= (char *)el && (char *)self_iface < (char *)(el + 1)))
                    {
                        ReleaseSRWLockExclusive( &g_all_uwp_elems_lock );
                        IUnknown_Release( (IUnknown *)self_iface );
                        IUnknown_Release( (IUnknown *)ui_iface );
                        return el;
                    }
                }
                ReleaseSRWLockExclusive( &g_all_uwp_elems_lock );
                IUnknown_Release( (IUnknown *)self_iface );
            }
            IUnknown_Release( (IUnknown *)ui_iface );
        }
        /* Only probe MCG __ComObject get_Inner (vtbl[6]/[7]) if GetRuntimeClassName is not implemented */
        cls_hr = E_FAIL;
        __TRY
        {
            cls_hr = IInspectable_GetRuntimeClassName( (IInspectable *)ptr, &hcls );
        }
        __EXCEPT_ALL
        {
            cls_hr = E_FAIL;
        }
        __ENDTRY;
        if (SUCCEEDED(cls_hr))
        {
            if (hcls) WindowsDeleteString( hcls );
            return NULL;
        }
        __TRY
        {
            void **vtbl = *(void ***)ptr;
            HRESULT (WINAPI *get_inner)(void *, void **) = (void *)vtbl[6];
            HRESULT (WINAPI *get_inner_iu)(void *, void **) = (void *)vtbl[7];
            if (get_inner && SUCCEEDED(get_inner( ptr, &cand )) && cand)
                inner = cand;
            else if (get_inner_iu && SUCCEEDED(get_inner_iu( ptr, &cand )) && cand)
                inner = cand;
            else inner = NULL;
        }
        __EXCEPT_ALL
        {
            inner = NULL;
        }
        __ENDTRY;
        if (inner)
        {
            /* Validate: trust inner only when it answers our identity IID
             * (slot 6 on native objects is an arbitrary property getter). */
            void *id = NULL;
            if (FAILED(IUnknown_QueryInterface( (IUnknown *)inner, &IID_UWP_ELEM_SELF, &id )) || !id)
            {
                IUnknown_Release( (IUnknown *)inner );
                return NULL;
            }
            IUnknown_Release( (IUnknown *)id );
            AcquireSRWLockExclusive( &g_all_uwp_elems_lock );
            for (el = g_all_uwp_elems_head; el; el = el->next_reg)
            {
                if ((void *)inner == (void *)el->outer ||
                    (void *)inner == (void *)&el->nondelegating_iface ||
                    ((char *)inner >= (char *)el && (char *)inner < (char *)(el + 1)))
                {
                    ReleaseSRWLockExclusive( &g_all_uwp_elems_lock );
                    IUnknown_Release( (IUnknown *)inner );
                    return el;
                }
            }
            TRACE( "uwp_elem_from_any: INNER-MISS ptr=%p inner=%p cnt=%d\n", ptr, inner, g_all_uwp_elem_cnt );
            ReleaseSRWLockExclusive( &g_all_uwp_elems_lock );
            IUnknown_Release( (IUnknown *)inner );
            return NULL;
        }
    }
    /* Fallback: Try QI for IID_IDependencyObject_UWP to unwrap MCG RCW/CCW */
    {
        void *do_iface = NULL;
        if (SUCCEEDED(IUnknown_QueryInterface( (IUnknown *)ptr, &IID_IDependencyObject_UWP, &do_iface )) && do_iface)
        {
            struct uwp_element *e2;
            int k;
            el = NULL;
            /* RCW may return its own projected face; probe all our faces on it */
            for (k = 0; k < 8; k++)
            {
                void *cand = NULL;
                static const GUID *iids[8] = { &IID_IUIElement_UWP, &IID_IFrameworkElement_UWP,
                    &IID_IControl_UWP, &IID_IColumnDefinition_UWP };
                if (k >= 4) break;
                if (SUCCEEDED(IUnknown_QueryInterface( (IUnknown *)do_iface, iids[k], &cand )) && cand)
                {
                    AcquireSRWLockExclusive( &g_all_uwp_elems_lock );
                    for (e2 = g_all_uwp_elems_head; e2; e2 = e2->next_reg)
                    {
                        if ((void *)cand == (void *)e2->outer ||
                            ((char *)cand >= (char *)e2 && (char *)cand < (char *)(e2 + 1)))
                        { el = e2; break; }
                    }
                    ReleaseSRWLockExclusive( &g_all_uwp_elems_lock );
                    IUnknown_Release( (IUnknown *)cand );
                    if (el) break;
                }
            }
            if (el)
            {
                IUnknown_Release( (IUnknown *)do_iface );
                return el;
            }
            AcquireSRWLockExclusive( &g_all_uwp_elems_lock );
            TRACE( "uwp_elem_from_any: MISS ptr=%p do_iface=%p cnt=%d\n", ptr, do_iface, g_all_uwp_elem_cnt );
            ReleaseSRWLockExclusive( &g_all_uwp_elems_lock );
            IUnknown_Release( (IUnknown *)do_iface );
        }
    }
    return NULL;
}

HWND g_island_hwnds[16];
int g_island_hwnd_cnt = 0;

void uwp_invalidate_window( void )
{
    if (g_island_hwnd_cnt > 0)
    {
        int i;
        g_layout_dirty = TRUE;
        for (i = 0; i < g_island_hwnd_cnt; i++)
        {
            if (g_island_hwnds[i] && IsWindow( g_island_hwnds[i] ) && IsWindowVisible( g_island_hwnds[i] ))
                InvalidateRect( g_island_hwnds[i], NULL, FALSE );
        }
        return;
    }
    {
        HWND top = xaml_find_core_window();
        xaml_mark_window_dirty();
        if (top)
        {
            HWND root = GetAncestor( top, GA_ROOT );
            if (root && root != top) InvalidateRect( root, NULL, FALSE );
            InvalidateRect( top, NULL, FALSE );
        }
    }
}

static HRESULT WINAPI uwp_nondelegating_QueryInterface( IInspectable *iface, REFIID iid, void **out );
static ULONG WINAPI uwp_nondelegating_AddRef( IInspectable *iface );
static ULONG WINAPI uwp_nondelegating_Release( IInspectable *iface );

struct uwp_weak_ref
{
    IWeakReference IWeakReference_iface;
    LONG ref;
    struct uwp_element *elem;
};

static HRESULT WINAPI uwp_wr_QI( IWeakReference *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IWeakReference_local ))
    {
        *out = iface;
        InterlockedIncrement( &((struct uwp_weak_ref *)iface)->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI uwp_wr_AddRef( IWeakReference *iface )
{
    return InterlockedIncrement( &((struct uwp_weak_ref *)iface)->ref );
}
static ULONG WINAPI uwp_wr_Release( IWeakReference *iface )
{
    struct uwp_weak_ref *wr = (struct uwp_weak_ref *)iface;
    ULONG r = InterlockedDecrement( &wr->ref );
    if (!r) free( wr );
    return r;
}
static HRESULT WINAPI uwp_wr_Resolve( IWeakReference *iface, REFIID iid, IInspectable **out )
{
    struct uwp_weak_ref *wr = (struct uwp_weak_ref *)iface;
    if (!out) return E_POINTER;
    *out = NULL;
    if (!wr->elem) return S_OK;
    if (wr->elem->outer)
        return IInspectable_QueryInterface( wr->elem->outer, iid, (void **)out );
    return IInspectable_QueryInterface( &wr->elem->IInspectable_iface, iid, (void **)out );
}
static const struct IWeakReferenceVtbl uwp_weak_ref_vtbl =
{
    uwp_wr_QI,
    uwp_wr_AddRef,
    uwp_wr_Release,
    uwp_wr_Resolve,
};

static HRESULT WINAPI uwp_wrs_QI( IWeakReferenceSource *iface, REFIID iid, void **out )
{
    struct uwp_element *impl = CONTAINING_RECORD( iface, struct uwp_element, IWeakReferenceSource_iface );
    return uwp_nondelegating_QueryInterface( &impl->nondelegating_iface, iid, out );
}
static ULONG WINAPI uwp_wrs_AddRef( IWeakReferenceSource *iface )
{
    struct uwp_element *impl = CONTAINING_RECORD( iface, struct uwp_element, IWeakReferenceSource_iface );
    return uwp_nondelegating_AddRef( &impl->nondelegating_iface );
}
static ULONG WINAPI uwp_wrs_Release( IWeakReferenceSource *iface )
{
    struct uwp_element *impl = CONTAINING_RECORD( iface, struct uwp_element, IWeakReferenceSource_iface );
    return uwp_nondelegating_Release( &impl->nondelegating_iface );
}
static HRESULT WINAPI uwp_wrs_GetWeakReference( IWeakReferenceSource *iface, IWeakReference **out )
{
    struct uwp_element *impl = CONTAINING_RECORD( iface, struct uwp_element, IWeakReferenceSource_iface );
    struct uwp_weak_ref *wr;
    if (!out) return E_POINTER;
    wr = calloc( 1, sizeof(*wr) );
    if (!wr) return E_OUTOFMEMORY;
    wr->IWeakReference_iface.lpVtbl = &uwp_weak_ref_vtbl;
    wr->ref = 1;
    wr->elem = impl;
    *out = &wr->IWeakReference_iface;
    return S_OK;
}
static const struct IWeakReferenceSourceVtbl uwp_wrs_vtbl =
{
    uwp_wrs_QI,
    uwp_wrs_AddRef,
    uwp_wrs_Release,
    uwp_wrs_GetWeakReference,
};

HRESULT WINAPI uwp_sub_QueryInterface( IInspectable *iface, REFIID iid, void **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return E_NOINTERFACE;
    /* Private identity IID: answer locally so app-side RCW QIs can be
     * resolved back to our element (delegation to outer loses this). */
    if (IsEqualGUID( iid, &IID_UWP_ELEM_SELF ))
    {
        if (!out) return E_POINTER;
        *out = &impl->nondelegating_iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IWeakReferenceSource_local ))
    {
        if (!out) return E_POINTER;
        *out = &impl->IWeakReferenceSource_iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    if (impl->outer && *(void **)impl->outer)
    {
        HRESULT hr = E_NOINTERFACE;
        BOOL faulted = FALSE;
        __TRY
        {
            hr = IInspectable_QueryInterface( impl->outer, iid, out );
        }
        __EXCEPT_ALL
        {
            faulted = TRUE;
        }
        __ENDTRY;
        if (!faulted) return hr;
        ERR( "uwp_sub_QI: outer=%p FAULTED on %s (%s), falling back to inner\n",
             impl->outer, debugstr_w(impl->class_name), debugstr_w(impl->name) );
        impl->outer = NULL;
        impl->delegating = FALSE;
    }
    return uwp_nondelegating_QueryInterface( &impl->nondelegating_iface, iid, out );
}

ULONG WINAPI uwp_sub_AddRef( IInspectable *iface )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return 1;
    if (impl->delegating && impl->outer)
        return IInspectable_AddRef( impl->outer );
    return uwp_nondelegating_AddRef( &impl->nondelegating_iface );
}

ULONG WINAPI uwp_sub_Release( IInspectable *iface )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return 1;
    if (impl->delegating && impl->outer)
        return IInspectable_Release( impl->outer );
    return uwp_nondelegating_Release( &impl->nondelegating_iface );
}

HRESULT WINAPI uwp_sub_GetIids( IInspectable *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI uwp_nondelegating_GetRuntimeClassName( IInspectable *iface, HSTRING *name )
{
    struct uwp_element *impl = CONTAINING_RECORD( iface, struct uwp_element, nondelegating_iface );
    const WCHAR *cls = (impl && impl->class_name[0]) ? impl->class_name : L"Windows.UI.Xaml.FrameworkElement";
    return WindowsCreateString( cls, wcslen(cls), name );
}

HRESULT WINAPI uwp_sub_GetRuntimeClassName( IInspectable *iface, HSTRING *name )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    const WCHAR *cls;
    if (impl && impl->outer && iface != &impl->nondelegating_iface)
        return IInspectable_GetRuntimeClassName( impl->outer, name );
    cls = (impl && impl->class_name[0]) ? impl->class_name : L"Windows.UI.Xaml.FrameworkElement";
    return WindowsCreateString( cls, wcslen(cls), name );
}

HRESULT WINAPI uwp_sub_GetTrustLevel( IInspectable *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}

HRESULT WINAPI uwp_generic_noop( void *iface, ... )
{
    return S_OK;
}

static HRESULT WINAPI uwp_nondelegating_QueryInterface( IInspectable *iface, REFIID iid, void **out )
{
    struct uwp_element *impl = CONTAINING_RECORD( iface, struct uwp_element, nondelegating_iface );
    if (!out) return E_POINTER;
    *out = NULL;

    if (IsEqualGUID( iid, &IID_UWP_ELEM_SELF ))
    {
        *out = &impl->nondelegating_iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ))
    {
        *out = &impl->nondelegating_iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IDispatch ))
    {
        *out = &impl->IDispatch_iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IAgileObject ))
    {
        if (impl->delegating && impl->outer)
        {
            *out = &impl->IInspectable_iface;
            IInspectable_AddRef( impl->outer );
            return S_OK;
        }
        *out = &impl->nondelegating_iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IWeakReferenceSource_local ))
    {
        *out = &impl->IWeakReferenceSource_iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IDependencyObject_UWP ))
        *out = &impl->IDependencyObject_iface;
    else if (IsEqualGUID( iid, &IID_IDependencyObject2_UWP ))
        *out = &impl->IDependencyObject2_iface;
    else if (IsEqualGUID( iid, &IID_IUIElement_UWP ))
        *out = &impl->IUIElement_iface;
    else if (IsEqualGUID( iid, &IID_IFrameworkElement_UWP ))
        *out = &impl->IFrameworkElement_iface;
    else if (IsEqualGUID( iid, &IID_IFrameworkElement2_UWP ))
        *out = &impl->IFrameworkElement2_iface;
    else if (IsEqualGUID( iid, &IID_IFrameworkElement3_UWP ))
        *out = &impl->IFrameworkElement3_iface;
    else if (IsEqualGUID( iid, &IID_IControl_UWP ))
        *out = &impl->IControl_iface;
    else if (IsEqualGUID( iid, &IID_IControlProtected_UWP ))
        *out = &impl->IControlProtected_iface;
    else if (IsEqualGUID( iid, &IID_IContentControl_UWP ) ||
             IsEqualGUID( iid, &IID_INavigationView_UWP ) ||
             IsEqualGUID( iid, &IID_INavigationView2_UWP ) ||
             IsEqualGUID( iid, &IID_IDesktopWindowXamlSource_UWP ))
        *out = &impl->IContentControl_iface;
    else if (IsEqualGUID( iid, &IID_IDesktopWindowXamlSourceNative_UWP ) ||
             IsEqualGUID( iid, &IID_IDesktopWindowXamlSourceNative2_UWP ))
        *out = &impl->IDesktopWindowXamlSourceNative_iface;
    else if (IsEqualGUID( iid, &IID_IUserControl_UWP ))
        *out = &impl->IUserControl_iface;
    else if (IsEqualGUID( iid, &IID_IPage_UWP ))
        *out = &impl->IPage_iface;
    else if (IsEqualGUID( iid, &IID_IFrame_UWP ) ||
             IsEqualGUID( iid, &IID_INavigate_UWP ))
        *out = &impl->IFrame_iface;
    else if (IsEqualGUID( iid, &IID_IButtonBase_UWP ) ||
             IsEqualGUID( iid, &IID_IHyperlinkButton_UWP ))
        *out = &impl->IButtonBase_iface;
    else if (IsEqualGUID( iid, &IID_IToggleButton_UWP ) ||
             IsEqualGUID( iid, &IID_IRadioButton_UWP ))
        *out = &impl->IToggleButton_iface;
    else if (IsEqualGUID( iid, &IID_IToggleMenuFlyoutItem_UWP ))
    {
        if (wcsstr( impl->class_name, L"ToggleMenuFlyoutItem" ))
            *out = &impl->IToggleMenuFlyoutItem_iface;
    }
    else if (IsEqualGUID( iid, &IID_IRadioButtons_WinUI ))
        *out = &impl->IStyle_iface;
    else if (IsEqualGUID( iid, &IID_IDataTemplate_UWP ))
        *out = &impl->IDataTemplate_iface;
    else if (IsEqualGUID( iid, &IID_IDataTemplateSelector_UWP ))
        *out = &impl->IDataTemplateSelector_iface;
    else if (IsEqualGUID( iid, &IID_IDataTemplateSelector2_UWP ))
        *out = &impl->IDataTemplateSelector2_iface;
    else if (IsEqualGUID( iid, &IID_IPanel_UWP ) ||
             IsEqualGUID( iid, &IID_IStackPanel_UWP ) ||
             IsEqualGUID( iid, &IID_IStackPanel_UWP2 ) ||
             IsEqualGUID( iid, &IID_IStackPanel2_Real_UWP ) ||
             IsEqualGUID( iid, &IID_IMenuBar_WinUI ))
        *out = &impl->IPanel_iface;
    else if (IsEqualGUID( iid, &IID_IMenuFlyoutSubItem_UWP ))
    {
        if (wcsstr( impl->class_name, L"MenuFlyoutSubItem" ))
            *out = &impl->IMenuFlyoutSubItem_iface;
    }
    else if (IsEqualGUID( iid, &IID_IUIElement4_Real_UWP ) ||
             IsEqualGUID( iid, &IID_IUIElement4_UWP ))
        *out = &impl->IUIElement4_iface;
    else if (IsEqualGUID( iid, &IID_IUIElement5_Real_UWP ) ||
             IsEqualGUID( iid, &IID_IUIElement5_UWP ))
        *out = &impl->IUIElement5_iface;
    else if (IsEqualGUID( iid, &IID_IButtonWithFlyout_Real_UWP ) ||
             IsEqualGUID( iid, &IID_IButtonWithFlyout_UWP ))
        *out = &impl->IButtonWithFlyout_iface;
    else if (IsEqualGUID( iid, &IID_IUIElement7_UWP ))
        *out = &impl->IUIElement7_iface;
    else if (IsEqualGUID( iid, &IID_IKeyboardAccelerator_UWP ))
        *out = &impl->IKeyboardAccelerator_iface;
    else if (IsEqualGUID( iid, &IID_IXamlUICommand_UWP ))
        *out = &impl->IXamlUICommand_iface;
    else if (IsEqualGUID( iid, &IID_ICommand_UWP ) ||
             IsEqualGUID( iid, &IID_ICommand_UWP2 ))
        *out = &impl->ICommand_iface;
    else if (IsEqualGUID( iid, &IID_IMenuFlyoutItem_UWP ))
    {
        if (wcsstr( impl->class_name, L"MenuFlyoutItem" ))
            *out = &impl->IMenuFlyoutItem_iface;
    }
    else if (IsEqualGUID( iid, &IID_IMenuFlyoutItem3_UWP ))
    {
        if (wcsstr( impl->class_name, L"MenuFlyoutItem" ))
            *out = &impl->IMenuFlyoutItem3_iface;
    }
    else if (IsEqualGUID( iid, &IID_IMenuFlyout2_Real_UWP ))
        *out = &impl->IMenuFlyout2_iface;
    else if (IsEqualGUID( iid, &IID_IAutoSuggestBox_Real_UWP ))
    {
        if (wcsstr( impl->class_name, L"AutoSuggestBox" ))
            *out = &impl->IAutoSuggestBox2_iface;
        else
            *out = &impl->IItemsControl_iface;
    }
    else if (IsEqualGUID( iid, &IID_IAutoSuggestBox2_Real_UWP ))
    {
        if (wcsstr( impl->class_name, L"AutoSuggestBox" ))
            *out = &impl->IAutoSuggestBox2_iface;
        else
            *out = &impl->ITextBlock_iface;
    }
    else if (IsEqualGUID( iid, &IID_IContentDialog_UWP ) ||
             IsEqualGUID( iid, &IID_IContentDialog_UWP_Alt ))
    {
        if (wcsstr( impl->class_name, L"AutoSuggestBox" ))
            *out = &impl->IAutoSuggestBox_iface;
        else
            *out = &impl->IContentDialog_iface;
    }
    else if (IsEqualGUID( iid, &IID_IContentDialog2_UWP ))
        *out = &impl->IContentDialog2_iface;
    else if (IsEqualGUID( iid, &IID_ISwapChainPanel_UWP ))
        *out = &impl->ISwapChainPanel_iface;
    else if (IsEqualGUID( iid, &IID_ISwapChainPanelNative_UWP ) ||
             IsEqualGUID( iid, &IID_ISwapChainPanelNative2_UWP ))
        *out = &impl->ISwapChainPanelNative2_iface;
    else if (IsEqualGUID( iid, &IID_ITeachingTip_WinUI ))
        *out = &impl->ITeachingTip_iface;
    else if (IsEqualGUID( iid, &IID_IGrid_UWP ) ||
             IsEqualGUID( iid, &IID_IGrid_UWP2 ))
        *out = &impl->IGrid_iface;
    else if (IsEqualGUID( iid, &IID_IGrid3_Calc ) ||
             IsEqualGUID( iid, &IID_IGrid3_UWP ))
        *out = &impl->IGrid3_iface;
    else if (IsEqualGUID( iid, &IID_IBorder_UWP ) ||
             IsEqualGUID( iid, &IID_IBorder2_Calc ))
        *out = &impl->IBorder_iface;
    else if (IsEqualGUID( iid, &IID_ITextBlock_UWP ))
        *out = &impl->ITextBlock_iface;
    else if ((iid->Data1 == 0xe5fff9e2 && iid->Data2 == 0xb968 && iid->Data3 == 0x49e7) || /* IRichTextBlock */
             (iid->Data1 == 0xf83ef59a && iid->Data2 == 0xfa61 && iid->Data3 == 0x4bef) || /* IParagraph */
             (iid->Data1 == 0x54f03373 && iid->Data2 == 0xc273 && iid->Data3 == 0x4f3b))   /* ISpan */
        *out = &impl->IParagraph_iface;
    else if (iid->Data1 == 0x59553c83 && iid->Data2 == 0x0e14 && iid->Data3 == 0x49bd)     /* IRun */
        *out = &impl->IRun_iface;
    else if (iid->Data1 == 0x3f7a0c6b && iid->Data2 == 0xd00f && iid->Data3 == 0x4730)     /* IBinding */
        *out = &impl->IBinding_iface;
    else if (iid->Data1 == 0x300e5d8a && iid->Data2 == 0x1ff3 && iid->Data3 == 0x4d2c)     /* IPropertyPath */
        *out = &impl->IPropertyPath_iface;
    else if (IsEqualGUID( iid, &IID_IRowDefinition_UWP ))
        *out = &impl->IRowDefinition_iface;
    else if (IsEqualGUID( iid, &IID_IColumnDefinition_UWP ))
        *out = &impl->IColumnDefinition_iface;
    else if (IsEqualGUID( iid, &IID_IScrollViewer_UWP ) ||
             IsEqualGUID( iid, &IID_IScrollViewer2_UWP ) ||
             IsEqualGUID( iid, &IID_IScrollViewer3_UWP ))
        *out = &impl->IScrollViewer_iface;
    else if (IsEqualGUID( iid, &IID_IFrameworkElement7_UWP ) ||
             IsEqualGUID( iid, &IID_IFrameworkElement7_Proj ))
        *out = &impl->IFrameworkElement7_iface;
    else if (IsEqualGUID( iid, &IID_IUIElement2_UWP ) ||
             IsEqualGUID( iid, &IID_IUIElement3_UWP ) ||
             IsEqualGUID( iid, &IID_IUIElement5_UWP ) ||
             IsEqualGUID( iid, &IID_IUIElementOverrides_UWP ) ||
             IsEqualGUID( iid, &IID_IUIElement_Calc2 ) ||
             IsEqualGUID( iid, &IID_IFrameworkElement4_UWP ) ||
             IsEqualGUID( iid, &IID_IFrameworkElement6_UWP ) ||
             IsEqualGUID( iid, &IID_IFrameworkElementOverrides_UWP ) ||
             IsEqualGUID( iid, &IID_IFrameworkElementOverrides2_UWP ) ||
             IsEqualGUID( iid, &IID_IControl2_UWP ) ||
             IsEqualGUID( iid, &IID_IControl3_UWP ) ||
             IsEqualGUID( iid, &IID_IControl4_UWP ) ||
             IsEqualGUID( iid, &IID_IControl5_UWP ) ||
             IsEqualGUID( iid, &IID_IControlOverrides_UWP ) ||
             IsEqualGUID( iid, &IID_IContentControl2_UWP ) ||
             IsEqualGUID( iid, &IID_IContentControlOverrides_UWP ) ||
             IsEqualGUID( iid, &IID_IContentControlOvr_Calc ) ||
             IsEqualGUID( iid, &IID_IPageOverrides_UWP ) ||
             IsEqualGUID( iid, &IID_IButton_UWP ) ||
             IsEqualGUID( iid, &IID_IButtonWithFlyout_UWP ) ||
             IsEqualGUID( iid, &IID_IToggleButtonOverrides_UWP ) ||
             IsEqualGUID( iid, &IID_ITextBlock2_UWP ) ||
             IsEqualGUID( iid, &IID_ITextBlock3_UWP ) ||
             IsEqualGUID( iid, &IID_ITextBlock6_UWP ) ||
             IsEqualGUID( iid, &IID_ITextBlock_Calc2 ) ||
             IsEqualGUID( iid, &IID_ITextBlock_Calc3 ) ||
             IsEqualGUID( iid, &IID_ITextBlock_Calc4 ) ||
             IsEqualGUID( iid, &IID_IPopup_UWP ) ||
             IsEqualGUID( iid, &IID_IPopup2_UWP ) ||
             IsEqualGUID( iid, &IID_IPopup3_UWP ) ||
             IsEqualGUID( iid, &IID_IPopup_WinUI ) ||
             IsEqualGUID( iid, &IID_IPopup2_WinUI ) ||
             IsEqualGUID( iid, &IID_IPopup3_WinUI ) ||
             IsEqualGUID( iid, &IID_IStyle_UWP ) ||
             IsEqualGUID( iid, &IID_ISetter_UWP ) ||
             IsEqualGUID( iid, &IID_ISetterBase_UWP ) ||
             IsEqualGUID( iid, &IID_IVisualStateGroup_UWP ) ||
             IsEqualGUID( iid, &IID_IVisualState_UWP ))
        *out = &impl->IStyle_iface;
    else if (IsEqualGUID( iid, &IID_IBrush_UWP ))
        *out = &impl->IBrush_iface;
    else if (IsEqualGUID( iid, &IID_ISolidColorBrush_UWP ))
        *out = &impl->ISolidColorBrush_iface;
    else
    {
        static const GUID IID_IXamlCompositionBrushBaseProtected_UWP =
            { 0x1513f3d8, 0x0457, 0x4e1c, { 0xad, 0x77, 0x11, 0xc1, 0xd9, 0x87, 0x97, 0x43 } };
        static const GUID IID_IXamlCompositionBrushBase_UWP =
            { 0x410b99e7, 0x9d63, 0x4449, { 0xa3, 0x58, 0x49, 0x62, 0xfd, 0x93, 0x5f, 0x39 } };
        static const GUID IID_ILoadedImageSurface_UWP =
            { 0x091e237c, 0x5217, 0x424b, { 0xb6, 0xea, 0x19, 0x34, 0x18, 0xc2, 0x6d, 0x0b } };
        static const GUID IID_ICompositionSurface_UWP =
            { 0x1527540d, 0x42c7, 0x47a6, { 0xa4, 0x08, 0x66, 0x8f, 0x79, 0xa9, 0x0d, 0xfb } };
        if (IsEqualGUID( iid, &IID_IXamlCompositionBrushBaseProtected_UWP ))
            *out = &impl->IXamlCompositionBrushBaseProtected_iface;
        else if (IsEqualGUID( iid, &IID_IXamlCompositionBrushBase_UWP ))
            *out = &impl->IStyle_iface;
        else if (IsEqualGUID( iid, &IID_ILoadedImageSurface_UWP ))
            *out = &impl->ILoadedImageSurface_iface;
        else if (IsEqualGUID( iid, &IID_ICompositionSurface_UWP ))
            *out = &impl->IInspectable_iface;
    }
    if (!*out && (IsEqualGUID( iid, &IID_IRoutedEventArgs_QI_UWP ) ||
              IsEqualGUID( iid, &IID_IRoutedEventArgs_Clone_UWP ) ||
              IsEqualGUID( iid, &IID_IRoutedEventArgs2_UWP ) ||
              IsEqualGUID( iid, &IID_IRoutedEventArgs_WinUI3 )))
        *out = &impl->IInspectable_iface;
    else if (IsEqualGUID( iid, &IID_ISplitView_UWP ))
        *out = &impl->ISplitView_iface;
    else if (IsEqualGUID( iid, &IID_ISplitView2_UWP ))
        *out = &impl->ISplitView2_iface;
    else if (IsEqualGUID( iid, &IID_IItemsRepeater_UWP ) ||
             IsEqualGUID( iid, &IID_IItemsRepeaterSizingTarget_UWP ))
        *out = &impl->IPanel_iface;
    else if (IsEqualGUID( iid, &IID_IContentPresenter_UWP ))
        *out = &impl->IContentControl_iface;
    else if ((IsEqualGUID( iid, &IID_IFrameProj_UWP ) &&
             (wcsstr( impl->class_name, L".Controls.Frame" ) ||
              wcsstr( impl->class_name, L".Controls.ContentFrame" ))))
        *out = &impl->IFrame2_iface; /* IFrame2: Navigate(TypeName, param, transitionInfo) */
    else if (IsEqualGUID( iid, &IID_IFrame3_UWP ) &&
             (wcsstr( impl->class_name, L".Controls.Frame" ) ||
              wcsstr( impl->class_name, L".Controls.ContentFrame" )))
        *out = &impl->IFrame3_iface;
    else if (IsEqualGUID( iid, &IID_IFrame5_UWP ) &&
             (wcsstr( impl->class_name, L".Controls.Frame" ) ||
              wcsstr( impl->class_name, L".Controls.ContentFrame" )))
        *out = &impl->IFrame5_iface;
    else if (IsEqualGUID( iid, &IID_IMediaPlayerElement_UWP ) &&
             wcsstr( impl->class_name, L"MediaPlayerElement" ))
        *out = &impl->IMediaPlayerElement_iface;
    else if ((IsEqualGUID( iid, &IID_INavigationTransitionInfo_UWP ) ||
              IsEqualGUID( iid, &IID_IEntranceNavigationTransitionInfo_UWP )) &&
             wcsstr( impl->class_name, L"NavigationTransitionInfo" ))
        *out = &impl->IStyle_iface; /* INavigationTransitionInfo / IEntranceNavigationTransitionInfo */
    else if (IsEqualGUID( iid, &IID_IFlyoutBase5_UWP ) &&
             wcsstr( impl->class_name, L"Flyout" ))
        *out = &impl->IStyle_iface; /* IFlyoutBase5 */
    else if (IsEqualGUID( iid, &IID_IImage2_UWP ) &&
             wcsstr( impl->class_name, L"Image" ))
        *out = &impl->IImage_iface; /* Windows.UI.Xaml.Controls.IImage */
    else if (IsEqualGUID( iid, &IID_IEasingFunctionBase_UWP ) &&
             (wcsstr( impl->class_name, L"Ease" ) || wcsstr( impl->class_name, L"EasingFunction" )))
        *out = &impl->IEasingFunctionBase_iface;
    else if (IsEqualGUID( iid, &IID_INavigationView_UWP ) &&
             !wcscmp( impl->class_name, L"Microsoft.UI.Xaml.Controls.NavigationView" ))
        *out = &impl->IStyle_iface;
    else if (IsEqualGUID( iid, &IID_INavigationViewItem_UWP ) &&
             !wcscmp( impl->class_name, L"Microsoft.UI.Xaml.Controls.NavigationViewItem" ))
        *out = &impl->IStyle_iface;
    else if (IsEqualGUID( iid, &IID_IFlyoutBase_UWP ) ||
             IsEqualGUID( iid, &IID_IFlyoutBase3_UWP ) ||
             IsEqualGUID( iid, &IID_IMenuFlyout_UWP ) ||
             IsEqualGUID( iid, &IID_IMenuFlyout_UWP2 ) ||
             IsEqualGUID( iid, &IID_IMenuFlyoutItemBase_UWP ) ||
             IsEqualGUID( iid, &IID_IMenuFlyoutItemBase_UWP2 ) ||
             IsEqualGUID( iid, &IID_IMenuBarItem_WinUI ) ||
             IsEqualGUID( iid, &IID_ITabView_WinUI ))
        *out = &impl->IFlyout_iface;
    if (!*out)
    {
        static const GUID IID_IListViewBase_UWP =
            { 0xf6ce8c6d, 0xfe96, 0x41ad, { 0xa6, 0x4a, 0xc2, 0xb8, 0x1c, 0x4a, 0xf7, 0xf8 } };
        static const GUID IID_ISelector_UWP =
            { 0xe30eb3a5, 0xb36b, 0x42dc, { 0x85, 0x27, 0xcd, 0x25, 0x13, 0x6c, 0x08, 0x3c } };
        static const GUID IID_IItemsControl_UWP =
            { 0x3d0813ba, 0x6890, 0x4537, { 0xbf, 0xe5, 0x79, 0x6d, 0x94, 0x58, 0xed, 0xd6 } };
        static const GUID IID_IItemsControl_UWP2 =
            { 0xf4a91dd8, 0xd979, 0x4381, { 0x86, 0x52, 0xbd, 0xa0, 0x34, 0x2a, 0x76, 0x5e } };
        static const GUID IID_IListViewBase2_UWP =
            { 0xd1c2a4f7, 0x78a3, 0x4491, { 0x86, 0xe0, 0x2d, 0xde, 0xbc, 0x00, 0x7a, 0xc5 } };
        static const GUID IID_IListViewBase4_UWP =
            { 0x18b7792b, 0x11dc, 0x4eb6, { 0xa7, 0xbc, 0x74, 0x1a, 0xdb, 0x3e, 0xeb, 0x79 } };
        static const GUID IID_IListView_UWP =
            { 0x8eed9bc2, 0xa58c, 0x4453, { 0xaf, 0x0f, 0xa9, 0x2e, 0xe0, 0x6d, 0x03, 0x17 } };
        static const GUID IID_IShape_UWP =
            { 0x786f2b75, 0x9aa0, 0x454d, { 0xae, 0x06, 0xa2, 0x46, 0x6e, 0x37, 0xc8, 0x32 } };
        static const GUID IID_IScrollViewer3_Real_UWP =
            { 0x74e9be21, 0x5de2, 0x595e, { 0xbc, 0x08, 0xdb, 0xd3, 0xc5, 0x77, 0x49, 0x2c } };
        static const GUID IID_IStateTrigger_UWP =
            { 0x67adef2e, 0xd8d9, 0x49f7, { 0xa1, 0xfd, 0x2e, 0x35, 0xee, 0xdd, 0x23, 0xcd } };
        static const GUID IID_IBitmapImage_UWP =
            { 0x31af3271, 0xe3b4, 0x442d, { 0xa3, 0x41, 0x4c, 0x02, 0x26, 0xb2, 0x72, 0x5b } };
        static const GUID IID_IBitmapImage2_UWP =
            { 0x1abaee23, 0x74ee, 0x4cc7, { 0x99, 0xba, 0xb1, 0x71, 0xe3, 0xcd, 0xa6, 0x1e } };
        static const GUID IID_IBitmapImage2_Real_UWP =
            { 0x1069c1b6, 0x8c9b, 0x4762, { 0xbe, 0x3d, 0x75, 0x9f, 0x56, 0x98, 0xf2, 0xb3 } };
        static const GUID IID_IBitmapImage3_UWP =
            { 0x7311e0d4, 0xfe94, 0x4e70, { 0x9b, 0x90, 0xcd, 0xd4, 0x7a, 0xc2, 0x3a, 0xfb } };
        static const GUID IID_IBitmapSource_UWP =
            { 0x23d86411, 0x202f, 0x41b2, { 0x8c, 0x5b, 0xa8, 0xa3, 0xb3, 0x33, 0x80, 0x0b } };
        if (IsEqualGUID( iid, &IID_IListViewBase_UWP ))
            *out = &impl->IFlyout_iface;
        else if (IsEqualGUID( iid, &IID_IBitmapImage_UWP ))
            *out = &impl->IBitmapImage_iface;
        else if (IsEqualGUID( iid, &IID_IBitmapSource_UWP ))
            *out = &impl->IBitmapSource_iface;
        else if (IsEqualGUID( iid, &IID_IStateTrigger_UWP ))
            *out = &impl->IStateTriggerPub_iface;
        else if (IsEqualGUID( iid, &IID_IShape_UWP ))
            *out = &impl->IShape_iface;
        else if (IsEqualGUID( iid, &IID_IListViewBase2_UWP ) ||
                 IsEqualGUID( iid, &IID_IListViewBase4_UWP ) ||
                 IsEqualGUID( iid, &IID_IListView_UWP ) ||
                 IsEqualGUID( iid, &IID_IScrollViewer3_Real_UWP ) ||
                 IsEqualGUID( iid, &IID_IBitmapImage2_UWP ) ||
                 IsEqualGUID( iid, &IID_IBitmapImage2_Real_UWP ) ||
                 IsEqualGUID( iid, &IID_IBitmapImage3_UWP ))
            *out = &impl->IStyle_iface;
        else if (IsEqualGUID( iid, &IID_ISelector_UWP ))
            *out = &impl->ISelector_iface;
        else if (IsEqualGUID( iid, &IID_IItemsControl_UWP ) ||
                 IsEqualGUID( iid, &IID_IItemsControl_UWP2 ))
            *out = &impl->IItemsControl_iface;
    }
    if (!*out)
    {
        if (IsEqualGUID( iid, &IID_IDispatcherTimer_UWP ))
            *out = &impl->IInspectable_iface;
        else if (IsEqualGUID( iid, &IID_IStateTriggerBase_UWP ) ||
                 IsEqualGUID( iid, &IID_IMarkupExtension_UWP ) ||
                 IsEqualGUID( iid, &IID_IMarkupExtensionOverrides_UWP ))
            *out = &impl->IStyle_iface;
        else if (IsEqualGUID( iid, &IID_IStateTriggerBaseProtected_UWP ) ||
                 IsEqualGUID( iid, &IID_IUIElement10_UWP ) ||
                 IsEqualGUID( iid, &IID_IXamlRoot_UWP ))
            *out = &impl->IStateTrigger_iface;
        else
    {
        static const GUID IID_IObservableVector_DO =
            { 0xfac6f4d2, 0x96fd, 0x5ceb, { 0x80, 0xbf, 0x46, 0xac, 0x65, 0x29, 0xfc, 0x65 } };
        static const GUID IID_IObservableVector_ICBE =
            { 0xd5644006, 0x717d, 0x512d, { 0x87, 0x85, 0xb4, 0x8c, 0x17, 0xe7, 0x34, 0x68 } };
        static const GUID IID_IVector_DO =
            { 0x771b857e, 0xab5c, 0x5db8, { 0xa0, 0x21, 0x39, 0x7c, 0x92, 0xcd, 0xc4, 0x4c } };
        static const GUID IID_IVector_Insp =
            { 0xb32bdca4, 0x5e52, 0x5b27, { 0xbc, 0x5d, 0xd6, 0x6a, 0x1a, 0x26, 0x8c, 0x2a } };
        static const GUID IID_IVector_MBI =
            { 0xef83f4ba, 0x0b73, 0x51d5, { 0xb1, 0x3b, 0x89, 0xa2, 0xb1, 0x56, 0xad, 0x9a } };
        static const GUID IID_IVector_MFIB =
            { 0xf7f1c37b, 0xb4df, 0x5128, { 0xa4, 0x76, 0xfc, 0x83, 0x99, 0xbc, 0x10, 0xaf } };
        static const GUID IID_IIterable_DO =
            { 0xf66c6bd3, 0x55b4, 0x5bbb, { 0xb8, 0x2a, 0x6d, 0x9c, 0xe3, 0x83, 0x09, 0x1a } };
        static const GUID IID_IVectorView_DO =
            { 0xfe750d77, 0x1307, 0x5df2, { 0xa0, 0x21, 0x1c, 0x7a, 0x8d, 0x6b, 0x80, 0xad } };
        if (IsEqualGUID( iid, &IID_IObservableVector_DO ) ||
            IsEqualGUID( iid, &IID_IObservableVector_ICBE ))
            *out = &impl->IObservableVector_iface;
        else if (IsEqualGUID( iid, &IID_IVector_DO ) ||
                 IsEqualGUID( iid, &IID_IVector_Insp ) ||
                 IsEqualGUID( iid, &IID_IVector_MBI ) ||
                 IsEqualGUID( iid, &IID_IVector_MFIB ))
            *out = &impl->IVector_iface;
        else if (IsEqualGUID( iid, &IID_IIterable_DO ))
            *out = &impl->IIterable_iface;
        else if (IsEqualGUID( iid, &IID_IVectorView_DO ))
            *out = &impl->IVectorView_iface;
        else
        {
            size_t k;
            for (k = 0; k < ARRAY_SIZE(IID_IUIE_Extra); k++)
            {
                if (IsEqualGUID( iid, &IID_IUIE_Extra[k] ))
                {
                    *out = &impl->IStyle_iface;
                    break;
                }
            }
        }
    }
    }
    if (!*out && (IsEqualGUID( iid, &IID_IDCompositionVisual ) ||
                  IsEqualGUID( iid, &IID_IDCompositionVisual2 )))
    {
        if (impl->visual)
        {
            *out = impl->visual;
            IDCompositionVisual_AddRef( impl->visual );
            return S_OK;
        }
    }

    if (*out)
    {
        TRACE( "uwp_nondelegating_QI: matched %s -> %p on %s\n", debugstr_guid(iid), *out, debugstr_w(impl->class_name) );
        uwp_sub_AddRef( (IInspectable *)*out );
        return S_OK;
    }

    if (iid->Data1 != 0x11d3b13a && wcsstr( impl->class_name, L"Grid" ))
        WARN( "uwp_nondelegating_QI: %s not matched on %s\n", debugstr_guid(iid), debugstr_w(impl->class_name) );
    else
        TRACE( "uwp_nondelegating_QI: %s not matched on %s\n", debugstr_guid(iid), debugstr_w(impl->class_name) );
    return E_NOINTERFACE;
}

static ULONG WINAPI uwp_nondelegating_AddRef( IInspectable *iface )
{
    struct uwp_element *impl = CONTAINING_RECORD( iface, struct uwp_element, nondelegating_iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI uwp_nondelegating_Release( IInspectable *iface )
{
    struct uwp_element *impl = CONTAINING_RECORD( iface, struct uwp_element, nondelegating_iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        /* Keep registered elements alive for visual tree lifetime */
        impl->outer = NULL;
        impl->ref = 1;
    }
    return ref;
}

/* IDependencyPropertyChangedEventArgs implementation (slots 6=NewValue, 7=OldValue, 8=Property) */

HRESULT WINAPI uwp_do2_RegisterPropertyChangedCallback( void *iface, void *dp, void *cb, INT64 *token )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (token) *token = 1;
    if (!impl || !cb) return S_OK;
    AcquireSRWLockExclusive( &impl->dp_lock );
    if (xaml_vec_grow( (void **)&impl->dp_cbs, &impl->dp_cb_cap, impl->dp_cb_cnt + 1, sizeof(*impl->dp_cbs), 16 ))
    {
        int idx = impl->dp_cb_cnt++;
        INT64 tok = ++impl->dp_cb_next_token;
        impl->dp_cbs[idx].dp = dp;
        impl->dp_cbs[idx].callback = (IInspectable *)cb;
        impl->dp_cbs[idx].token = tok;
        if ((ULONG_PTR)dp >= 0x10000) IUnknown_AddRef( (IUnknown *)dp );
        IUnknown_AddRef( (IUnknown *)cb );
        if (token) *token = tok;
    }
    ReleaseSRWLockExclusive( &impl->dp_lock );
    return S_OK;
}

static HRESULT WINAPI uwp_do2_UnregisterPropertyChangedCallback( void *iface, void *dp, INT64 token )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    IInspectable *old_cb = NULL;
    void *old_dp = NULL;
    int i;
    if (!impl) return S_OK;
    AcquireSRWLockExclusive( &impl->dp_lock );
    for (i = 0; i < impl->dp_cb_cnt; i++)
    {
        if (impl->dp_cbs[i].dp == dp && impl->dp_cbs[i].token == token)
        {
            old_cb = impl->dp_cbs[i].callback;
            old_dp = impl->dp_cbs[i].dp;
            impl->dp_cbs[i] = impl->dp_cbs[--impl->dp_cb_cnt];
            break;
        }
    }
    ReleaseSRWLockExclusive( &impl->dp_lock );
    if (old_cb) IUnknown_Release( (IUnknown *)old_cb );
    if ((ULONG_PTR)old_dp >= 0x10000) IUnknown_Release( (IUnknown *)old_dp );
    return S_OK;
}

/* --- IUIElement (slots 6=get_DesiredSize, 21=get_Visibility, 22=put_Visibility, 23=get_RenderSize, ...) --- */
static HRESULT WINAPI uwp_ui_get_DesiredSize( void *iface, Size *sz )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (sz)
    {
        sz->Width = (impl && impl->actual_w > 0) ? (FLOAT)impl->actual_w : 100.0f;
        sz->Height = (impl && impl->actual_h > 0) ? (FLOAT)impl->actual_h : 40.0f;
    }
    return S_OK;
}

static HRESULT WINAPI uwp_ui_get_Visibility( void *iface, int *vis )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (vis) *vis = impl ? impl->visibility : 0;
    return S_OK;
}

static HRESULT WINAPI uwp_ui_put_Visibility( void *iface, int vis )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl && impl->visibility != vis)
    {
        impl->visibility = vis;
        uwp_invalidate_window();
    }
    return S_OK;
}

static HRESULT WINAPI uwp_ui_get_RenderSize( void *iface, Size *sz )
{
    return uwp_ui_get_DesiredSize( iface, sz );
}

HRESULT WINAPI uwp_ui_event_add( void *iface, void *handler, EventRegistrationToken *tok )
{
    if (tok && (ULONG_PTR)tok > 0x10000 && !IsBadWritePtr( tok, sizeof(*tok) ))
        tok->value = 1;
    return S_OK;
}

static HRESULT WINAPI uwp_ui_get_IsHitTestVisible( void *iface, boolean *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (val) *val = (impl && impl->is_hit_test_visible) ? TRUE : FALSE;
    return S_OK;
}
static HRESULT WINAPI uwp_ui_put_IsHitTestVisible( void *iface, boolean val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl) impl->is_hit_test_visible = val ? TRUE : FALSE;
    return S_OK;
}

static const WCHAR *uwp_elem_name_w( struct uwp_element *el ) { return el ? el->name : L"?"; }

#define UWP_UI_PTR_EVENT( name, field ) \
HRESULT WINAPI uwp_ui_add_##name( void *iface, void *handler, EventRegistrationToken *tok ) \
{ \
    struct uwp_element *impl = uwp_elem_from_any( iface ); \
    if (!impl) { if (tok) tok->value = 0; return E_INVALIDARG; } \
    if (wcsstr( impl->class_name, L"Card" )) \
        WARN( "add_" #name " on %s (%s) impl=%p handler=%p\n", debugstr_w( impl->class_name ), debugstr_w( uwp_elem_name_w( impl ) ), impl, handler ); \
    TRACE( "add_" #name " on %s (%s) handler=%p\n", debugstr_w( impl->class_name ), debugstr_w( uwp_elem_name_w( impl ) ), handler ); \
    return xaml_event_registry_add( &impl->field, handler, tok ); \
} \
HRESULT WINAPI uwp_ui_remove_##name( void *iface, EventRegistrationToken tok ) \
{ \
    struct uwp_element *impl = uwp_elem_from_any( iface ); \
    if (!impl) return E_INVALIDARG; \
    return xaml_event_registry_remove( &impl->field, tok ); \
}

UWP_UI_PTR_EVENT( PointerPressed, ptr_pressed_reg )
UWP_UI_PTR_EVENT( PointerMoved, ptr_moved_reg )
UWP_UI_PTR_EVENT( PointerReleased, ptr_released_reg )
UWP_UI_PTR_EVENT( PointerEntered, ptr_entered_reg )
UWP_UI_PTR_EVENT( PointerExited, ptr_exited_reg )
UWP_UI_PTR_EVENT( PointerCaptureLost, ptr_capture_lost_reg )
UWP_UI_PTR_EVENT( Tapped, tapped_reg )
UWP_UI_PTR_EVENT( PointerCanceled, ptr_canceled_reg )
UWP_UI_PTR_EVENT( PointerWheelChanged, ptr_wheel_reg )
UWP_UI_PTR_EVENT( SelectionChanged, nav_sel_changed_reg )
UWP_UI_PTR_EVENT( ItemInvoked, nav_item_invoked_reg )

/* INavigationViewSelectionChangedEventArgs / INavigationViewItemInvokedEventArgs
 * projected face (singleton, like g_sel_changed_args).  ABI after the 6
 * IInspectable slots: 6 = get_SelectedItem / get_InvokedItem, 7 = get_IsSelected
 * / get_IsSettingsInvoked, 8 = get_IsSettingsSelected, 9 = get_SelectedItemOverride.
 * The item slots serve the value in g_nav_args_item set by uwp_nav_fire(). */
IInspectable *g_nav_args_item;
HRESULT WINAPI uwp_nav_args_QI( void *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    *out = iface;
    return S_OK;
}
static HRESULT WINAPI uwp_ui_CapturePointer( void *iface, void *pointer, boolean *ret )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl) g_captured_elem = impl;
    if (ret) *ret = impl != NULL;
    return S_OK;
}
static HRESULT WINAPI uwp_ui_ReleasePointerCapture( void *iface, void *pointer )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl && g_captured_elem == impl) g_captured_elem = NULL;
    return S_OK;
}
static HRESULT WINAPI uwp_ui_ReleasePointerCaptures( void *iface )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl && g_captured_elem == impl) g_captured_elem = NULL;
    return S_OK;
}

/* --- IFrameworkElement --- */
void uwp_realize_deferred_entry( struct uwp_defer_entry *de );
void uwp_fire_loaded_tree( struct uwp_element *el );

HRESULT WINAPI uwp_fe_get_Resources( void *iface, void **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    *out = NULL;
    if (!impl) return S_OK;
    if (!impl->resources_rd)
        xaml_resource_dictionary_create_for_uwp( (void **)&impl->resources_rd );
    if (impl->resources_rd && !impl->rd_bucket)
    {
        impl->rd_bucket = get_rd_bucket( impl->resources_rd, TRUE );
        if (impl->rd_bucket) impl->rd_bucket->owner_el = impl;
    }
    *out = impl->resources_rd;
    if (*out) IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}

HRESULT WINAPI uwp_fe_put_Resources( void *iface, void *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return S_OK;
    if (impl->resources_rd) IInspectable_Release( impl->resources_rd );
    impl->resources_rd = (IInspectable *)val;
    impl->rd_bucket = NULL;
    if (impl->resources_rd)
    {
        IInspectable_AddRef( impl->resources_rd );
        impl->rd_bucket = get_rd_bucket( impl->resources_rd, TRUE );
        if (impl->rd_bucket) impl->rd_bucket->owner_el = impl;
    }
    return S_OK;
}

HRESULT WINAPI uwp_fe_get_Tag( void *iface, void **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    if ((ULONG_PTR)out < 0x10000 || IsBadWritePtr( out, sizeof(void *) )) return S_OK;
    *out = impl ? impl->tag : NULL;
    if (*out) IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}

HRESULT WINAPI uwp_fe_put_Tag( void *iface, void *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return S_OK;
    if (impl->tag) IInspectable_Release( impl->tag );
    impl->tag = (IInspectable *)val;
    if (impl->tag) IInspectable_AddRef( impl->tag );
    return S_OK;
}

HRESULT WINAPI uwp_fe_get_Style( void *iface, IInspectable **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    *out = impl ? impl->style_obj : NULL;
    if (*out) IInspectable_AddRef( *out );
    return S_OK;
}

static void uwp_apply_runtime_style_setters( struct uwp_element *target_el, const struct uwp_style_setter *setters, int cnt )
{
    void *app_outer = uwp_get_app_outer();
    IInspectable *target = target_el->outer ? target_el->outer : &target_el->IInspectable_iface;
    int i;
    for (i = 0; i < cnt; i++)
    {
        const struct uwp_style_setter *st = &setters[i];
        WARN( "  setter[%d] pid=%#x prop=%s owner=%s res=%d vt=%d str=%s\n",
              i, st->pid, debugstr_w(st->prop_name), debugstr_w(st->owner_cls), st->is_res_ref, st->vt, debugstr_w(st->str) );
        if (st->pid & 0x8000)
        {
            xbf_apply_property( NULL, target_el, NULL, st->pid, st->vt, st->num, st->str, st->vec4, st->is_res_ref );
            continue;
        }
        if (!st->prop_name[0] || !st->owner_cls[0]) continue;
        if (!wcsncmp( st->owner_cls, L"Windows.UI.Xaml.", 16 ))
        {
            xbf_apply_property( NULL, target_el, NULL, st->pid, st->vt, st->num, st->str, st->vec4, st->is_res_ref );
            continue;
        }
        if (!app_outer) continue;
        if (st->is_res_ref && st->str[0])
        {
            struct uwp_rd_entry *re = NULL;
            struct uwp_rd_bucket *found_b = NULL;
            if (uwp_rd_find_entry( NULL, target_el, st->str, 0, &re, &found_b ) && re)
            {
                IInspectable *res_val = uwp_rd_materialize_entry( found_b, re );
                WARN( "apply_style_setter %s.%s <- res %s (val=%p)\n",
                      debugstr_w(st->owner_cls), debugstr_w(st->prop_name), debugstr_w(st->str), res_val );
                if (res_val)
                    uwp_set_custom_xaml_member_obj( app_outer, st->owner_cls, st->prop_name, target, res_val );
            }
        }
        else if (st->vt == 0x01 || st->vt == 0x02)
        {
            uwp_set_custom_xaml_member( app_outer, st->owner_cls, st->prop_name, target,
                                        (st->vt == 0x02) ? L"True" : L"False" );
        }
        else if (st->str[0])
        {
            uwp_set_custom_xaml_member( app_outer, st->owner_cls, st->prop_name, target, st->str );
        }
    }
}

static void uwp_apply_runtime_style_by_name( struct uwp_element *target_el, const WCHAR *key, int depth )
{
    struct uwp_rd_entry *se = NULL;
    struct uwp_rd_bucket *sb = NULL;
    if (!target_el || !key || !key[0] || depth > 8) return;
    if (uwp_style_is_accent_name( key ))
        target_el->is_accent_style = TRUE;
    if (uwp_rd_find_entry( NULL, target_el, key, 0, &se, &sb ) && se && se->kind == UWP_RD_STYLE)
    {
        if (se->str_val[0])
            uwp_apply_runtime_style_by_name( target_el, se->str_val, depth + 1 );
        if (se->setter_cnt > 0 && se->setters)
            uwp_apply_runtime_style_setters( target_el, se->setters, se->setter_cnt );
    }
}

HRESULT WINAPI uwp_fe_put_Style( void *iface, IInspectable *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    struct uwp_element *st_el;
    if (!impl) return S_OK;
    if (val) IInspectable_AddRef( val );
    if (impl->style_obj) IInspectable_Release( impl->style_obj );
    impl->style_obj = val;
    if (val) impl->is_accent_style = FALSE;
    st_el = uwp_elem_from_any( val );
    if (wcsstr( impl->class_name, L"ResponsiveItemsRepeaterList" ))
        WARN( "uwp_fe_put_Style on %s (%s) val=%p st_el=%p name=%s based_on=%s cnt=%d\n",
              debugstr_w(impl->class_name), debugstr_w(impl->name), val, st_el,
              st_el ? debugstr_w(st_el->name) : "\"-\"",
              st_el ? debugstr_w(st_el->style_based_on) : "\"-\"",
              st_el ? st_el->style_setter_cnt : -1 );
    if (st_el)
    {
        if (st_el->name[0] && uwp_style_is_accent_name( st_el->name ))
            impl->is_accent_style = TRUE;
        if (st_el->style_based_on[0])
            uwp_apply_runtime_style_by_name( impl, st_el->style_based_on, 0 );
        if (st_el->style_setter_cnt > 0 && st_el->style_setters)
            uwp_apply_runtime_style_setters( impl, st_el->style_setters, st_el->style_setter_cnt );
    }
    uwp_invalidate_window();
    return S_OK;
}

HRESULT WINAPI uwp_fe_get_ActualWidth( void *iface, double *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (val) *val = (impl && impl->actual_w > 0) ? impl->actual_w : 320.0;
    return S_OK;
}

HRESULT WINAPI uwp_fe_get_ActualHeight( void *iface, double *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (val) *val = (impl && impl->actual_h > 0) ? impl->actual_h : 500.0;
    return S_OK;
}

HRESULT WINAPI uwp_fe_get_Width( void *iface, double *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (val) *val = impl ? impl->width : 0.0;
    return S_OK;
}
HRESULT WINAPI uwp_fe_put_Width( void *iface, double val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl) { impl->width = val; g_layout_dirty = TRUE; }
    return S_OK;
}
HRESULT WINAPI uwp_fe_get_Height( void *iface, double *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (val) *val = impl ? impl->height : 0.0;
    return S_OK;
}
HRESULT WINAPI uwp_fe_put_Height( void *iface, double val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl) { impl->height = val; g_layout_dirty = TRUE; }
    return S_OK;
}
static HRESULT WINAPI uwp_fe_get_MinWidth( void *iface, double *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (val) *val = impl ? impl->min_width : 0.0;
    return S_OK;
}
static HRESULT WINAPI uwp_fe_put_MinWidth( void *iface, double val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl) impl->min_width = val;
    return S_OK;
}
static HRESULT WINAPI uwp_fe_get_MaxWidth( void *iface, double *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (val) *val = impl ? impl->max_width : 1e9;
    return S_OK;
}
static HRESULT WINAPI uwp_fe_put_MaxWidth( void *iface, double val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl) impl->max_width = val;
    return S_OK;
}
static HRESULT WINAPI uwp_fe_get_MinHeight( void *iface, double *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (val) *val = impl ? impl->min_height : 0.0;
    return S_OK;
}
static HRESULT WINAPI uwp_fe_put_MinHeight( void *iface, double val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl) impl->min_height = val;
    return S_OK;
}
static HRESULT WINAPI uwp_fe_get_MaxHeight( void *iface, double *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (val) *val = impl ? impl->max_height : 1e9;
    return S_OK;
}
static HRESULT WINAPI uwp_fe_put_MaxHeight( void *iface, double val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl) impl->max_height = val;
    return S_OK;
}


HRESULT WINAPI uwp_fe_get_Margin( void *iface, struct uwp_thickness *t )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (t)
    {
        t->Left = impl ? impl->margin_l : 0;
        t->Top = impl ? impl->margin_t : 0;
        t->Right = impl ? impl->margin_r : 0;
        t->Bottom = impl ? impl->margin_b : 0;
    }
    return S_OK;
}

HRESULT WINAPI uwp_fe_put_Margin( void *iface, struct uwp_thickness *t )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl && t)
    {
        impl->margin_l = t->Left;
        impl->margin_t = t->Top;
        impl->margin_r = t->Right;
        impl->margin_b = t->Bottom;
        g_layout_dirty = TRUE;
    }
    return S_OK;
}

HRESULT WINAPI uwp_fe_get_Name( void *iface, HSTRING *name )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    const WCHAR *s = impl ? impl->name : L"";
    return WindowsCreateString( s, wcslen(s), name );
}

HRESULT WINAPI uwp_fe_put_Name( void *iface, HSTRING name )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    const WCHAR *s = WindowsGetStringRawBuffer( name, NULL );
    if (impl && s) lstrcpynW( impl->name, s, ARRAY_SIZE(impl->name) );
    return S_OK;
}

HRESULT WINAPI uwp_fe_get_Parent( void *iface, void **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    WARN( "uwp_fe_get_Parent: iface=%p impl=%p (%s) parent=%p (%s)\n", iface, impl,
          impl ? debugstr_w(impl->class_name) : "?", impl ? impl->parent : NULL,
          (impl && impl->parent) ? debugstr_w(impl->parent->class_name) : "?" );
    if (!out) return E_POINTER;
    *out = NULL;
    if (impl && impl->parent)
    {
        *out = impl->parent->outer ? impl->parent->outer : &impl->parent->IInspectable_iface;
        IInspectable_AddRef( (IInspectable *)*out );
    }
    return S_OK;
}

/* DataContextChangedEventArgs (slot 6 = get_NewValue, slot 7 = get_Handled, slot 8 = put_Handled) */
static HRESULT WINAPI dc_args_get_NewValue( void *iface, IInspectable **out )
{
    struct uwp_dc_args *a = iface;
    if (!out) return E_POINTER;
    *out = a->new_val;
    if (*out) IInspectable_AddRef( *out );
    return S_OK;
}
static HRESULT WINAPI dc_args_get_Handled( void *iface, boolean *h )
{
    struct uwp_dc_args *a = iface;
    if (h) *h = a->handled;
    return S_OK;
}
static HRESULT WINAPI dc_args_put_Handled( void *iface, boolean h )
{
    struct uwp_dc_args *a = iface;
    a->handled = h;
    return S_OK;
}
static HRESULT WINAPI dc_args_QI( void *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IDataContextChangedEventArgs_UWP ))
    {
        *out = iface;
        InterlockedIncrement( &((struct uwp_dc_args *)iface)->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
ULONG WINAPI dc_args_Release( void *iface )
{
    struct uwp_dc_args *a = iface;
    ULONG r = InterlockedDecrement( &a->ref );
    if (!r)
    {
        if (a->new_val) IInspectable_Release( a->new_val );
        free( a );
    }
    return r;
}
static HRESULT WINAPI dc_args_GetRuntimeClassName( void *iface, HSTRING *name )
{
    const WCHAR *cls = L"Windows.UI.Xaml.DataContextChangedEventArgs";
    return WindowsCreateString( cls, wcslen(cls), name );
}
const void *dc_args_vtbl[] =
{
    dc_args_QI,
    dp_args_AddRef,
    dc_args_Release,
    dp_args_GetIids,
    dc_args_GetRuntimeClassName,
    dp_args_GetTrustLevel,
    dc_args_get_NewValue,
    dc_args_get_Handled,
    dc_args_put_Handled,
};

void uwp_propagate_data_context( struct uwp_element *impl, IInspectable *dc );

HRESULT WINAPI uwp_fe_get_DataContext( void *iface, void **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    struct uwp_element *cur = impl;
    if (!out) return E_POINTER;
    *out = NULL;
    while (cur)
    {
        if (cur->data_context)
        {
            *out = cur->data_context;
            IInspectable_AddRef( (IInspectable *)*out );
            return S_OK;
        }
        cur = cur->parent;
    }
    return S_OK;
}

HRESULT WINAPI uwp_fe_put_DataContext( void *iface, void *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return S_OK;
    TRACE( "uwp_fe_put_DataContext on %s (%s) -> %p\n",
           debugstr_w(impl->class_name), debugstr_w(impl->name), val );
    uwp_propagate_data_context( impl, (IInspectable *)val );
    return S_OK;
}

HRESULT WINAPI uwp_fe_FindName( void *iface, HSTRING name, void **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    struct uwp_element *root;
    struct uwp_element *el;
    const WCHAR *want = WindowsGetStringRawBuffer( name, NULL );
    int i;

    if (!out) return E_POINTER;
    *out = NULL;
    if (!impl || !want || !want[0]) return S_OK;

    root = impl->owner_component ? impl->owner_component : impl;
    TRACE( "uwp_fe_FindName(%s) on %s (root=%s)\n",
           debugstr_w(want), debugstr_w(impl->class_name), debugstr_w(root->class_name) );

    /* 1. Check deferred entries on root and impl */
    if (root->defer_entries)
    {
        for (i = 0; i < root->defer_cnt; i++)
        {
            if (!wcscmp( root->defer_entries[i].name, want ) && !root->defer_entries[i].realized)
                uwp_realize_deferred_entry( &root->defer_entries[i] );
        }
    }
    if (impl != root && impl->defer_entries)
    {
        for (i = 0; i < impl->defer_cnt; i++)
        {
            if (!wcscmp( impl->defer_entries[i].name, want ) && !impl->defer_entries[i].realized)
                uwp_realize_deferred_entry( &impl->defer_entries[i] );
        }
    }

    /* 2. Search named elements in root, impl, and global list */
    for (i = 0; i < root->named_cnt; i++)
    {
        struct uwp_element *el = root->named_elems[i];
        if (el && !wcscmp( el->name, want ))
        {
            *out = el->outer ? el->outer : &el->IInspectable_iface;
            IInspectable_AddRef( (IInspectable *)*out );
            return S_OK;
        }
    }
    AcquireSRWLockExclusive( &g_all_uwp_elems_lock );
    for (el = g_all_uwp_elems_head; el; el = el->next_reg)
    {
        if (!wcscmp( el->name, want ) &&
            (!el->owner_component || el->owner_component == root || el->owner_component == impl))
        {
            *out = el->outer ? el->outer : &el->IInspectable_iface;
            break;
        }
    }
    ReleaseSRWLockExclusive( &g_all_uwp_elems_lock );
    if (*out) IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}

HRESULT WINAPI uwp_fe_add_Loaded( void *iface, void *handler, EventRegistrationToken *tok )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (tok) tok->value = 1;
    if (!impl || !handler) return S_OK;
    TRACE( "add_Loaded on %s (%s) handler=%p\n", debugstr_w( impl->class_name ), debugstr_w( impl->name ), handler );
    if (xaml_vec_grow( (void **)&impl->loaded_handlers, &impl->loaded_cap, impl->loaded_cnt + 1, sizeof(*impl->loaded_handlers), 8 ))
    {
        impl->loaded_handlers[impl->loaded_cnt++] = (IInspectable *)handler;
        IUnknown_AddRef( (IUnknown *)handler );
    }
    return S_OK;
}

static HRESULT WINAPI uwp_fe_add_SizeChanged( void *iface, void *handler, EventRegistrationToken *tok )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (tok) tok->value = 1;
    if (!impl || !handler) return S_OK;
    TRACE( "add_SizeChanged on %s (%s) handler=%p\n", debugstr_w( impl->class_name ), debugstr_w( impl->name ), handler );
    if (xaml_vec_grow( (void **)&impl->size_changed_handlers, &impl->size_changed_cap, impl->size_changed_cnt + 1, sizeof(*impl->size_changed_handlers), 8 ))
    {
        impl->size_changed_handlers[impl->size_changed_cnt++] = (IInspectable *)handler;
        IUnknown_AddRef( (IUnknown *)handler );
    }
    return S_OK;
}

static HRESULT WINAPI uwp_fe_add_LayoutUpdated( void *iface, void *handler, EventRegistrationToken *tok )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (tok) tok->value = 1;
    if (!impl || !handler) return S_OK;
    TRACE( "add_LayoutUpdated on %s (%s) handler=%p\n", debugstr_w( impl->class_name ), debugstr_w( impl->name ), handler );
    if (xaml_vec_grow( (void **)&impl->layout_updated_handlers, &impl->layout_updated_cap, impl->layout_updated_cnt + 1, sizeof(*impl->layout_updated_handlers), 8 ))
    {
        impl->layout_updated_handlers[impl->layout_updated_cnt++] = (IInspectable *)handler;
        IUnknown_AddRef( (IUnknown *)handler );
        if (tok) tok->value = impl->layout_updated_cnt;
    }
    return S_OK;
}

static HRESULT WINAPI uwp_fe_remove_LayoutUpdated( void *iface, EventRegistrationToken tok )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    TRACE( "remove_LayoutUpdated on %s tok=%I64d\n", impl ? debugstr_w( impl->class_name ) : "?", tok.value );
    if (impl && tok.value >= 1 && tok.value <= impl->layout_updated_cnt)
    {
        int idx = (int)tok.value - 1;
        if (impl->layout_updated_handlers[idx])
        {
            IUnknown_Release( (IUnknown *)impl->layout_updated_handlers[idx] );
            impl->layout_updated_handlers[idx] = NULL;
        }
    }
    return S_OK;
}

static HRESULT WINAPI uwp_fe_remove_Unloaded( void *iface, EventRegistrationToken tok )
{
    return S_OK;
}

HRESULT WINAPI uwp_fe3_add_DataContextChanged( void *iface, void *handler, EventRegistrationToken *tok )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (tok) tok->value = 1;
    if (!impl || !handler) return S_OK;
    if (xaml_vec_grow( (void **)&impl->dc_handlers, &impl->dc_cap, impl->dc_cnt + 1, sizeof(*impl->dc_handlers), 8 ))
    {
        impl->dc_handlers[impl->dc_cnt++] = (IInspectable *)handler;
        IUnknown_AddRef( (IUnknown *)handler );
        /* If DataContext is already available, fire immediately */
        {
            void *cur_dc = NULL;
            uwp_fe_get_DataContext( iface, &cur_dc );
            if (cur_dc)
            {
                struct uwp_dc_args *args = calloc( 1, sizeof(*args) );
                if (args)
                {
                    IInspectable *sender = impl->outer ? impl->outer : &impl->IInspectable_iface;
                    struct xaml_event_handler *h = (struct xaml_event_handler *)handler;
                    args->vtbl = dc_args_vtbl;
                    args->ref = 1;
                    args->new_val = (IInspectable *)cur_dc;
                    h->lpVtbl->Invoke( h, sender, args );
                    dc_args_Release( args );
                }
                else IInspectable_Release( (IInspectable *)cur_dc );
            }
        }
    }
    return S_OK;
}

HRESULT WINAPI uwp_fe3_add_Loading( void *iface, void *handler, EventRegistrationToken *tok )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (tok) tok->value = 1;
    if (!impl || !handler) return S_OK;
    if (xaml_vec_grow( (void **)&impl->loading_handlers, &impl->loading_cap, impl->loading_cnt + 1, sizeof(*impl->loading_handlers), 8 ))
    {
        impl->loading_handlers[impl->loading_cnt++] = (IInspectable *)handler;
        IUnknown_AddRef( (IUnknown *)handler );
    }
    return S_OK;
}

/* --- IControl & IControlProtected --- */
HRESULT WINAPI uwp_ctrl_get_FontSize( void *iface, double *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (val) *val = (impl && impl->font_size > 0) ? impl->font_size : 14.0;
    return S_OK;
}
HRESULT WINAPI uwp_ctrl_put_FontSize( void *iface, double val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl && val > 0) impl->font_size = val;
    return S_OK;
}
HRESULT WINAPI uwp_ctrl_get_IsEnabled( void *iface, boolean *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (val) *val = impl ? impl->is_enabled : TRUE;
    return S_OK;
}
HRESULT WINAPI uwp_ctrl_put_IsEnabled( void *iface, boolean val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl && impl->is_enabled != val)
    {
        impl->is_enabled = val;
        uwp_invalidate_window();
    }
    return S_OK;
}
HRESULT WINAPI uwp_ctrl_ApplyTemplate( void *iface, boolean *ret )
{
    if (ret) *ret = TRUE;
    return S_OK;
}
HRESULT WINAPI uwp_ctrl_Focus( void *iface, int state, boolean *ret )
{
    if (ret) *ret = TRUE;
    return S_OK;
}

/* Named template parts synthesised for WinUI templated controls that ship a
 * rich ControlTemplate we do not fully model.  Keyed by part name so the
 * mapping is generic (no per-app shims).  Collapsed parts keep their slots
 * without stealing layout space from the visible pane/content. */
static const WCHAR *uwp_tpl_part_class( const WCHAR *nm )
{
    static const struct { const WCHAR *name, *cls; } map[] =
    {
        { L"RootSplitView", L"Microsoft.UI.Xaml.Controls.SplitView" },
        { L"MenuItemsHost", L"Microsoft.UI.Xaml.Controls.ItemsRepeater" },
        { L"TopNavMenuItemsHost", L"Microsoft.UI.Xaml.Controls.ItemsRepeater" },
        { L"TopNavMenuItemsOverflowHost", L"Microsoft.UI.Xaml.Controls.ItemsRepeater" },
        { L"FooterMenuItemsHost", L"Microsoft.UI.Xaml.Controls.ItemsRepeater" },
        { L"TopFooterMenuItemsHost", L"Microsoft.UI.Xaml.Controls.ItemsRepeater" },
        { L"TopNavOverflowButton", L"Microsoft.UI.Xaml.Controls.Button" },
        { L"TogglePaneButton", L"Microsoft.UI.Xaml.Controls.Button" },
        { L"PaneAutoSuggestButton", L"Microsoft.UI.Xaml.Controls.Button" },
        { L"NavigationViewBackButton", L"Windows.UI.Xaml.Controls.Button" },
        { L"NavigationViewCloseButton", L"Windows.UI.Xaml.Controls.Button" },
        { L"Popup", L"Windows.UI.Xaml.Controls.Primitives.Popup" },
        { L"PopupContent", L"Windows.UI.Xaml.Controls.Grid" },
        { L"ExpandCollapseButton", L"Windows.UI.Xaml.Controls.Primitives.ToggleButton" },
        { L"PaneAutoSuggestBoxPresenter", L"Microsoft.UI.Xaml.Controls.ContentPresenter" },
        { L"TopPaneAutoSuggestBoxPresenter", L"Microsoft.UI.Xaml.Controls.ContentPresenter" },
        { L"PaneHeaderOnTopPane", L"Microsoft.UI.Xaml.Controls.ContentPresenter" },
        { L"PaneTitleOnTopPane", L"Microsoft.UI.Xaml.Controls.ContentPresenter" },
        { L"PaneCustomContentOnTopPane", L"Microsoft.UI.Xaml.Controls.ContentPresenter" },
        { L"PaneFooterOnTopPane", L"Microsoft.UI.Xaml.Controls.ContentPresenter" },
        { L"PaneTitlePresenter", L"Microsoft.UI.Xaml.Controls.ContentPresenter" },
        { L"PaneTitleTextBlock", L"Windows.UI.Xaml.Controls.TextBlock" },
        { L"PaneContentGrid", L"Windows.UI.Xaml.Controls.Grid" },
        { L"ContentGrid", L"Windows.UI.Xaml.Controls.Grid" },
        { L"TopNavGrid", L"Windows.UI.Xaml.Controls.Grid" },
        { L"TopNavContentOverlayAreaGrid", L"Windows.UI.Xaml.Controls.Grid" },
        { L"ItemsContainerGrid", L"Windows.UI.Xaml.Controls.Grid" },
        { L"MenuItemsScrollViewer", L"Windows.UI.Xaml.Controls.ScrollViewer" },
        { L"FooterItemsScrollViewer", L"Windows.UI.Xaml.Controls.ScrollViewer" },
        { L"Shape", L"Windows.UI.Xaml.Controls.Border" },
        { L"OldElement", L"Windows.UI.Xaml.Controls.ContentPresenter" },
        { L"NewElement", L"Windows.UI.Xaml.Controls.ContentPresenter" },
    };
    size_t i;
    for (i = 0; i < ARRAY_SIZE(map); i++)
        if (!wcscmp( nm, map[i].name )) return map[i].cls;
    return NULL;
}

static BOOL uwp_tpl_part_collapsed( const WCHAR *nm )
{
    static const WCHAR *const cols[] =
    {
        L"TopNavMenuItemsHost", L"TopNavMenuItemsOverflowHost", L"TopFooterMenuItemsHost",
        L"TopNavOverflowButton", L"TopNavGrid", L"TopNavContentOverlayAreaGrid",
        L"PaneAutoSuggestBoxPresenter", L"TopPaneAutoSuggestBoxPresenter",
        L"PaneAutoSuggestButton", L"NavigationViewBackButton", L"NavigationViewCloseButton",
        L"PaneHeaderOnTopPane", L"PaneTitleOnTopPane", L"PaneCustomContentOnTopPane",
        L"PaneFooterOnTopPane",
    };
    size_t i;
    for (i = 0; i < ARRAY_SIZE(cols); i++)
        if (!wcscmp( nm, cols[i] )) return TRUE;
    return FALSE;
}

static void uwp_element_add_named( struct uwp_element *owner, struct uwp_element *el )
{
    if (!owner || !el) return;
    if (xaml_vec_grow( (void **)&owner->named_elems, &owner->named_cap,
                      owner->named_cnt + 1, sizeof(*owner->named_elems), 8 ))
        owner->named_elems[owner->named_cnt++] = el;
}

HRESULT WINAPI uwp_ctrl_prot_GetTemplateChild( void *iface, HSTRING child_name, void **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    const WCHAR *nm = WindowsGetStringRawBuffer( child_name, NULL );
    struct uwp_element *child = NULL;

    if (!out) return E_POINTER;
    *out = NULL;
    if (!impl || !nm) return S_OK;

    TRACE( "GetTemplateChild(%s) on %s (%s)\n",
           debugstr_w(nm), debugstr_w(impl->class_name), debugstr_w(impl->name) );

    if (!wcscmp( nm, L"TextContainer" ))
    {
        if (!impl->tpl_text_container)
        {
            IInspectable *raw = NULL;
            if (SUCCEEDED(uwp_element_create( L"Windows.UI.Xaml.Controls.ScrollViewer", NULL, NULL, (void **)&raw )) && raw)
            {
                child = uwp_elem_from_any( raw );
                IInspectable_Release( raw );
            }
            if (child)
            {
                child->parent = impl;
                child->actual_w = 320.0;
                child->actual_h = 80.0;
                lstrcpynW( child->name, nm, ARRAY_SIZE(child->name) );
                impl->tpl_text_container = child;
            }
        }
        child = impl->tpl_text_container;
    }
    else if (!wcscmp( nm, L"NormalOutput" ))
    {
        if (!impl->tpl_normal_output)
        {
            IInspectable *raw = NULL;
            if (SUCCEEDED(uwp_element_create( L"Windows.UI.Xaml.Controls.TextBlock", NULL, NULL, (void **)&raw )) && raw)
            {
                child = uwp_elem_from_any( raw );
                IInspectable_Release( raw );
            }
            if (child)
            {
                child->parent = impl->tpl_text_container ? impl->tpl_text_container : impl;
                child->actual_w = 300.0;
                child->actual_h = 64.0;
                child->font_size = 44.0;
                lstrcpynW( child->name, nm, ARRAY_SIZE(child->name) );
                if (impl->text[0]) lstrcpynW( child->text, impl->text, ARRAY_SIZE(child->text) );
                impl->tpl_normal_output = child;
                if (impl->tpl_text_container)
                    impl->tpl_text_container->content = &child->IInspectable_iface;
                impl->content = &child->IInspectable_iface;
            }
        }
        child = impl->tpl_normal_output;
    }
    else if (!wcscmp( nm, L"ScrollLeft" ) || !wcscmp( nm, L"ScrollRight" ))
    {
        IInspectable *raw = NULL;
        if (SUCCEEDED(uwp_element_create( L"Windows.UI.Xaml.Controls.HyperlinkButton", NULL, NULL, (void **)&raw )) && raw)
        {
            child = uwp_elem_from_any( raw );
            IInspectable_Release( raw );
        }
        if (child)
        {
            child->parent = impl;
            child->visibility = 1; /* Collapsed by default */
            lstrcpynW( child->name, nm, ARRAY_SIZE(child->name) );
        }
    }
    else if (!wcscmp( nm, L"ContentRootGrid" ) || !wcscmp( nm, L"RootGrid" ) ||
             !wcscmp( nm, L"DialogSpace" ) || !wcscmp( nm, L"CommandSpace" ))
    {
        IInspectable *raw = NULL;
        if (SUCCEEDED(uwp_element_create( L"Windows.UI.Xaml.Controls.Grid", NULL, NULL, (void **)&raw )) && raw)
        {
            child = uwp_elem_from_any( raw );
            IInspectable_Release( raw );
        }
        if (child)
        {
            child->parent = impl;
            child->actual_w = 360.0;
            child->actual_h = 180.0;
            lstrcpynW( child->name, nm, ARRAY_SIZE(child->name) );
        }
    }
    else if (!wcscmp( nm, L"PrimaryButton" ) || !wcscmp( nm, L"SecondaryButton" ) ||
             !wcscmp( nm, L"CloseButton" ))
    {
        IInspectable *raw = NULL;
        if (SUCCEEDED(uwp_element_create( L"Windows.UI.Xaml.Controls.Button", NULL, NULL, (void **)&raw )) && raw)
        {
            child = uwp_elem_from_any( raw );
            IInspectable_Release( raw );
        }
        if (child)
        {
            child->parent = impl;
            lstrcpynW( child->name, nm, ARRAY_SIZE(child->name) );
        }
    }
    else if (!wcscmp( nm, L"TabListView" ))
    {
        IInspectable *raw = NULL;
        if (SUCCEEDED(uwp_element_create( L"Windows.UI.Xaml.Controls.ListView", NULL, NULL, (void **)&raw )) && raw)
        {
            child = uwp_elem_from_any( raw );
            IInspectable_Release( raw );
            if (child)
            {
                child->parent = impl;
                lstrcpynW( child->name, nm, ARRAY_SIZE(child->name) );
            }
        }
    }
    else if (uwp_tpl_part_class( nm ))
    {
        /* WinUI templated controls (NavigationView, ...) call GetTemplateChild for
         * their named template parts.  The generic builtin template is minimal, so
         * synthesise the part with the class the control casts it to and cache it on
         * impl->named_elems so repeated lookups return the same element. */
        int i;
        for (i = 0; i < impl->named_cnt; i++)
        {
            if (impl->named_elems[i] && !wcscmp( impl->named_elems[i]->name, nm ))
            {
                child = impl->named_elems[i];
                break;
            }
        }
        if (!child)
        {
            const WCHAR *part_cls = uwp_tpl_part_class( nm );
            IInspectable *raw = NULL;
            if (!wcscmp( part_cls, L"Microsoft.UI.Xaml.Controls.ItemsRepeater" ) &&
                (!wcscmp( nm, L"MenuItemsHost" ) || !wcscmp( nm, L"FooterMenuItemsHost" )))
            {
                if (SUCCEEDED(uwp_create_instance_from_provider( part_cls, (void **)&raw )) && raw)
                {
                    child = uwp_elem_from_any( raw );
                    if (child) lstrcpynW( child->class_name, part_cls, ARRAY_SIZE(child->class_name) );
                    IInspectable_Release( raw );
                }
            }
            if (!child && SUCCEEDED(uwp_element_create( part_cls, NULL, NULL, (void **)&raw )) && raw)
            {
                child = uwp_elem_from_any( raw );
                IInspectable_Release( raw );
            }
            if (child)
            {
                child->parent = impl;
                child->visibility = uwp_tpl_part_collapsed( nm ) ? 1 : 0;
                lstrcpynW( child->name, nm, ARRAY_SIZE(child->name) );
                if (!wcscmp( nm, L"MenuItemsHost" ) || !wcscmp( nm, L"FooterMenuItemsHost" ) ||
                    !wcscmp( nm, L"PaneContentGrid" ))
                    child->width = 68.0;
                if (wcsstr( impl->class_name, L"NavigationView" ) &&
                    (!wcscmp( nm, L"TogglePaneButton" ) || !wcscmp( nm, L"MenuItemsHost" ) ||
                     !wcscmp( nm, L"FooterMenuItemsHost" )))
                {
                    if (!wcscmp( nm, L"TogglePaneButton" ))
                    {
                        child->width = 48.0;
                        child->height = 40.0;
                        if (!child->text[0])
                            lstrcpynW( child->text, L"\x2630", ARRAY_SIZE(child->text) );
                    }
                    if (!impl->children_coll) uwp_vec_create( impl, &impl->children_coll );
                    if (impl->children_coll)
                        uwp_vec_Append( impl->children_coll, child->outer ? child->outer : &child->IInspectable_iface );
                }
                uwp_element_add_named( impl, child );
            }
        }
    }
    else
    {
        HRESULT hr_fn = uwp_fe_FindName( iface, child_name, out );
        if (!*out)
            WARN( "GetTemplateChild(%s) on %s (%s) -> NULL\n",
                  debugstr_w(nm), debugstr_w(impl->class_name), debugstr_w(impl->name) );
        return hr_fn;
    }

    if (child)
    {
        *out = &child->IDependencyObject_iface;
        uwp_sub_AddRef( (IInspectable *)*out );
    }
    return S_OK;
}

/* --- IContentControl / IUserControl / IBorder --- */
BOOL g_in_tap_diag = FALSE;

static const void *uwp_nondelegating_vtbl[16] =
{
    uwp_nondelegating_QueryInterface,
    uwp_nondelegating_AddRef,
    uwp_nondelegating_Release,
    uwp_sub_GetIids,
    uwp_nondelegating_GetRuntimeClassName,
    uwp_sub_GetTrustLevel,
    [6 ... 15] = uwp_generic_noop,
};

static const void *uwp_do_vtbl[16] =
{
    uwp_sub_QueryInterface,
    uwp_sub_AddRef,
    uwp_sub_Release,
    uwp_sub_GetIids,
    uwp_sub_GetRuntimeClassName,
    uwp_sub_GetTrustLevel,
    uwp_do_GetValue,              /* 6: GetValue */
    uwp_do_SetValue,              /* 7: SetValue */
    uwp_do_ClearValue,            /* 8: ClearValue */
    uwp_do_GetValue,              /* 9: ReadLocalValue */
    uwp_do_GetValue,              /* 10: GetAnimationBaseValue */
    uwp_do_get_Dispatcher,       /* 11: get_Dispatcher */
    uwp_do_get_DispatcherQueue,  /* 12: get_DispatcherQueue */
    [13 ... 15] = uwp_generic_noop,
};

static const void *uwp_do2_vtbl[16] =
{
    uwp_sub_QueryInterface,
    uwp_sub_AddRef,
    uwp_sub_Release,
    uwp_sub_GetIids,
    uwp_sub_GetRuntimeClassName,
    uwp_sub_GetTrustLevel,
    uwp_do2_RegisterPropertyChangedCallback,   /* 6 */
    uwp_do2_UnregisterPropertyChangedCallback, /* 7 */
    [8 ... 15] = uwp_generic_noop,
};

IDependencyProperty *g_routed_event_ptr_pressed = NULL;

static HRESULT WINAPI uwp_ui_AddHandler( void *iface, void *routed_event, void *handler, boolean handled_too )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    (void)handled_too;
    if (!impl || !handler) return S_OK;
    WARN( "uwp_ui_AddHandler on %s (%s) ev=%p ptr_ev=%p handler=%p handled_too=%d\n",
          debugstr_w(impl->class_name), debugstr_w(impl->name), routed_event, g_routed_event_ptr_pressed, handler, handled_too );
    if (g_routed_event_ptr_pressed && routed_event == (void *)g_routed_event_ptr_pressed)
    {
        static const GUID IID_IPointerEventHandler_UWP =
            { 0xe4385929, 0xc004, 0x4bcf, { 0x89, 0x70, 0x35, 0x94, 0x86, 0xe3, 0x9f, 0x88 } };
        static const GUID IID_IReference_PointerEventHandler =
            { 0x34de1275, 0x0aa0, 0x54ef, { 0xb5, 0x5a, 0xa2, 0xb5, 0x98, 0x68, 0x5f, 0xcb } };
        void *peh = NULL;
        void *iref = NULL;
        void *del = NULL;
        if (SUCCEEDED(IUnknown_QueryInterface( (IUnknown *)handler, &IID_IPointerEventHandler_UWP, &peh )) && peh)
        {
            del = peh;
            xaml_event_registry_add( &impl->ptr_pressed_reg, del, NULL );
            IUnknown_Release( (IUnknown *)peh );
        }
        else if (SUCCEEDED(IUnknown_QueryInterface( (IUnknown *)handler, &IID_IReference_PointerEventHandler, &iref )) && iref)
        {
            void *unwrapped = NULL;
            HRESULT (WINAPI *get_val)(void *, void **) = (void *)(*(void ***)iref)[6];
            if (get_val && SUCCEEDED(get_val( iref, &unwrapped )) && unwrapped)
            {
                del = unwrapped;
                xaml_event_registry_add( &impl->ptr_pressed_reg, del, NULL );
                IUnknown_Release( (IUnknown *)unwrapped );
            }
            IUnknown_Release( (IUnknown *)iref );
        }
        else
        {
            del = handler;
            xaml_event_registry_add( &impl->ptr_pressed_reg, del, NULL );
        }
        TRACE( "IUIElement::AddHandler(PointerPressed) on %s (%s) -> del %p\n",
               debugstr_w(impl->class_name), debugstr_w(impl->name), del );
    }
    return S_OK;
}

struct uwp_general_transform
{
    IInspectable IGeneralTransform_iface;
    LONG ref;
    float dx, dy;
};
static const GUID IID_IGeneralTransform_UWP =
    { 0xa06798b7, 0xa2ec, 0x415f, { 0xad, 0xe2, 0xad, 0xe9, 0x37, 0xd8, 0xb8, 0x06 } };
static HRESULT WINAPI uwp_gt_QI( IInspectable *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IGeneralTransform_UWP ))
    {
        *out = iface;
        IInspectable_AddRef( iface );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI uwp_gt_AddRef( IInspectable *iface )
{
    struct uwp_general_transform *gt = CONTAINING_RECORD( iface, struct uwp_general_transform, IGeneralTransform_iface );
    return InterlockedIncrement( &gt->ref );
}
static ULONG WINAPI uwp_gt_Release( IInspectable *iface )
{
    struct uwp_general_transform *gt = CONTAINING_RECORD( iface, struct uwp_general_transform, IGeneralTransform_iface );
    ULONG r = InterlockedDecrement( &gt->ref );
    if (!r) free( gt );
    return r;
}
static HRESULT WINAPI uwp_gt_GetIids( IInspectable *iface, ULONG *cnt, IID **iids )
{
    if (cnt) *cnt = 0;
    if (iids) *iids = NULL;
    return S_OK;
}
static HRESULT WINAPI uwp_gt_GetRuntimeClassName( IInspectable *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.UI.Xaml.Media.GeneralTransform", 38, name );
}
static HRESULT WINAPI uwp_gt_GetTrustLevel( IInspectable *iface, TrustLevel *lvl )
{
    if (lvl) *lvl = BaseTrust;
    return S_OK;
}
static const void *uwp_gt_vtbl[];
static HRESULT WINAPI uwp_gt_get_Inverse( void *iface, void **out )
{
    struct uwp_general_transform *gt = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_general_transform, IGeneralTransform_iface );
    struct uwp_general_transform *inv;
    if (!out) return E_POINTER;
    inv = calloc( 1, sizeof(*inv) );
    if (!inv) return E_OUTOFMEMORY;
    inv->IGeneralTransform_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_gt_vtbl;
    inv->ref = 1;
    inv->dx = -gt->dx;
    inv->dy = -gt->dy;
    *out = &inv->IGeneralTransform_iface;
    return S_OK;
}
static HRESULT WINAPI uwp_gt_TransformPoint( void *iface, UINT64 pt_bits, float *out_pt )
{
    struct uwp_general_transform *gt = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_general_transform, IGeneralTransform_iface );
    float px, py;
    if (!out_pt) return E_POINTER;
    memcpy( &px, (const char *)&pt_bits, 4 );
    memcpy( &py, (const char *)&pt_bits + 4, 4 );
    out_pt[0] = px + gt->dx;
    out_pt[1] = py + gt->dy;
    return S_OK;
}
static HRESULT WINAPI uwp_gt_TryTransform( void *iface, UINT64 pt_bits, float *out_pt, boolean *res )
{
    uwp_gt_TransformPoint( iface, pt_bits, out_pt );
    if (res) *res = TRUE;
    return S_OK;
}
static HRESULT WINAPI uwp_gt_TransformBounds( void *iface, const float *in_rc, float *out_rc )
{
    struct uwp_general_transform *gt = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_general_transform, IGeneralTransform_iface );
    if (!out_rc) return E_POINTER;
    if (in_rc)
    {
        out_rc[0] = in_rc[0] + gt->dx;
        out_rc[1] = in_rc[1] + gt->dy;
        out_rc[2] = in_rc[2];
        out_rc[3] = in_rc[3];
    }
    return S_OK;
}
static const void *uwp_gt_vtbl[] =
{
    uwp_gt_QI,
    uwp_gt_AddRef,
    uwp_gt_Release,
    uwp_gt_GetIids,
    uwp_gt_GetRuntimeClassName,
    uwp_gt_GetTrustLevel,
    uwp_gt_get_Inverse,
    uwp_gt_TransformPoint,
    uwp_gt_TryTransform,
    uwp_gt_TransformBounds,
};
static HRESULT WINAPI uwp_ui_TransformToVisual( void *iface, void *visual, void **out )
{
    struct uwp_element *src = uwp_elem_from_any( iface );
    struct uwp_element *dst = visual ? uwp_elem_from_any( visual ) : NULL;
    struct uwp_general_transform *gt;
    if (!out) return E_POINTER;
    gt = calloc( 1, sizeof(*gt) );
    if (!gt) return E_OUTOFMEMORY;
    gt->IGeneralTransform_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_gt_vtbl;
    gt->ref = 1;
    if (src && dst)
    {
        gt->dx = (float)(src->bounds.left - dst->bounds.left);
        gt->dy = (float)(src->bounds.top - dst->bounds.top);
    }
    else if (src)
    {
        gt->dx = (float)src->bounds.left;
        gt->dy = (float)src->bounds.top;
    }
    *out = &gt->IGeneralTransform_iface;
    return S_OK;
}

static HRESULT WINAPI uwp_ui_InvalidateLayout( void *iface )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl)
    {
        impl->last_measured_w = 0.0f;
        impl->last_measured_h = 0.0f;
    }
    g_layout_dirty = TRUE;
    uwp_invalidate_window();
    return S_OK;
}

static const void *uwp_ui_vtbl[128] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_ui_get_DesiredSize,
    [7 ... 18] = uwp_generic_noop,
    [19] = uwp_ui_get_IsHitTestVisible,
    [20] = uwp_ui_put_IsHitTestVisible,
    [21] = uwp_ui_get_Visibility,
    [22] = uwp_ui_put_Visibility,
    [23] = uwp_ui_get_RenderSize,
    [24 ... 40] = uwp_generic_noop,
    [41 ... 56] = uwp_ui_event_add,
    [57] = uwp_ui_add_PointerPressed,
    [58] = uwp_ui_remove_PointerPressed,
    [59] = uwp_ui_add_PointerMoved,
    [60] = uwp_ui_remove_PointerMoved,
    [61] = uwp_ui_add_PointerReleased,
    [62] = uwp_ui_remove_PointerReleased,
    [63] = uwp_ui_add_PointerEntered,
    [64] = uwp_ui_remove_PointerEntered,
    [65] = uwp_ui_add_PointerExited,
    [66] = uwp_ui_remove_PointerExited,
    [67] = uwp_ui_add_PointerCaptureLost,
    [68] = uwp_ui_remove_PointerCaptureLost,
    [69] = uwp_ui_add_PointerCanceled,
    [70] = uwp_ui_remove_PointerCanceled,
    [71] = uwp_ui_add_PointerWheelChanged,
    [72] = uwp_ui_remove_PointerWheelChanged,
    [73] = uwp_ui_add_Tapped,
    [74] = uwp_ui_remove_Tapped,
    [75 ... 85] = uwp_ui_event_add,
    [86 ... 92] = uwp_generic_noop,
    [93] = uwp_ui_CapturePointer,
    [94] = uwp_ui_ReleasePointerCapture,
    [95] = uwp_ui_ReleasePointerCaptures,
    [96] = uwp_ui_AddHandler,
    [97] = uwp_generic_noop,
    [98] = uwp_ui_TransformToVisual,
    [99] = uwp_ui_InvalidateLayout,  /* InvalidateMeasure */
    [100] = uwp_ui_InvalidateLayout, /* InvalidateArrange */
    [101] = uwp_ui_InvalidateLayout, /* UpdateLayout */
    [102 ... 127] = uwp_generic_noop,
};

static HRESULT WINAPI uwp_fe_get_FlowDirection( void *iface, int *dir )
{
    if (dir) *dir = 0;
    return S_OK;
}
static HRESULT WINAPI uwp_fe_put_FlowDirection( void *iface, int dir )
{
    return S_OK;
}
static HRESULT WINAPI uwp_fe_get_BaseUri( void *iface, void **uri )
{
    if (uri) *uri = NULL;
    return S_OK;
}

/* INavigationView own-method getters, ABI slots 56/62 on the FE face
 * (INavigationView : IFrameworkElement, 3 + 53 IFE = base 56):
 * own[0] = get_IsPaneOpen, own[6] = get_FooterMenuItems. */
HRESULT WINAPI uwp_fe_probe6( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 6 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe11( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 11 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe12( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 12 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe27( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 27 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe28( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 28 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe29( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 29 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe30( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 30 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe38( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 38 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe39( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 39 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe44( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 44 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe48( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 48 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe52( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 52 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe53( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 53 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe54( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 54 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe55( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 55 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe57( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 57 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe58( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 58 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe59( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 59 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe60( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 60 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe61( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 61 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe63( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 63 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe64( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 64 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe65( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 65 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe66( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 66 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe67( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 67 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe68( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 68 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe69( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 69 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe70( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 70 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe71( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 71 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe72( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 72 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe73( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 73 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe74( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 74 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe75( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 75 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe76( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 76 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe77( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 77 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe78( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 78 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe79( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 79 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe80( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 80 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe81( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 81 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe82( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 82 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe83( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 83 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe84( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 84 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe85( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 85 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe86( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 86 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe87( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 87 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe88( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 88 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe89( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 89 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe90( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 90 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe91( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 91 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe92( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 92 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe93( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 93 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe94( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 94 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe95( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 95 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe96( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 96 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe97( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 97 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe98( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 98 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe99( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 99 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe100( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 100 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe101( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 101 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe106( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 106 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe107( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 107 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe108( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 108 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe109( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 109 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe110( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 110 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe111( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 111 hit on NavView\n" ); return S_OK; }

static IInspectable *uwp_resolve_binding_path( IInspectable *src, const WCHAR *path )
{
    static const GUID IID_IXamlMetadataProvider_l =
        { 0xb3765d69, 0x68a5, 0x4b32, { 0x88, 0x61, 0xfd, 0xb9, 0x0c, 0x1f, 0x58, 0x36 } };
    void *app_outer = uwp_get_app_outer();
    void *provider = NULL;
    IInspectable *cur = src;
    BOOL cur_owned = FALSE;
    const WCHAR *p = path;

    if (!src || !path || !path[0] || !app_outer) return NULL;
    if (FAILED(IInspectable_QueryInterface( (IInspectable *)app_outer, &IID_IXamlMetadataProvider_l, &provider )) || !provider)
        return NULL;

    __TRY
    {
        while (cur && p && *p)
        {
            const WCHAR *dot = wcschr( p, L'.' );
            size_t seg_len = dot ? (size_t)(dot - p) : wcslen( p );
            HSTRING h_cls = NULL, h_prop = NULL;
            IInspectable *next = NULL;

            if (seg_len > 0 && SUCCEEDED(IInspectable_GetRuntimeClassName( cur, &h_cls )) && h_cls)
            {
                const WCHAR *cls_name = WindowsGetStringRawBuffer( h_cls, NULL );
                if (cls_name && cls_name[0])
                {
                    struct xaml_type *xt = uwp_provider_get_xaml_type_cached( (struct xaml_metadata_provider *)provider, cls_name );
                    if (xt && xt->lpVtbl->GetMember && SUCCEEDED(WindowsCreateString( p, (UINT32)seg_len, &h_prop )))
                    {
                        struct xaml_member *xm = NULL;
                        if (SUCCEEDED(xt->lpVtbl->GetMember( xt, h_prop, &xm )) && xm)
                        {
                            if (xm->lpVtbl->GetValue)
                                xm->lpVtbl->GetValue( xm, cur, &next );
                            IUnknown_Release( (IUnknown *)xm );
                        }
                        WindowsDeleteString( h_prop );
                    }
                }
                WindowsDeleteString( h_cls );
            }
            if (cur_owned) IInspectable_Release( cur );
            cur = next;
            cur_owned = (cur != NULL);
            p = dot ? dot + 1 : NULL;
        }
    }
    __EXCEPT_ALL
    {
        if (cur_owned && cur) IInspectable_Release( cur );
        cur = NULL;
        cur_owned = FALSE;
    }
    __ENDTRY;

    IUnknown_Release( (IUnknown *)provider );
    return cur_owned ? cur : NULL;
}

void uwp_eval_xaml_binding( struct uwp_element *tel, const WCHAR *owner_cls,
                                   const WCHAR *prop_name, const WCHAR *path, IInspectable *src )
{
    IInspectable *target;
    void *app_outer = uwp_get_app_outer();
    if (!tel || !owner_cls || !owner_cls[0] || !prop_name || !prop_name[0] || !src || !app_outer) return;
    target = tel->outer ? tel->outer : &tel->IInspectable_iface;
    if (!path || !path[0])
    {
        uwp_set_custom_xaml_member_obj( app_outer, owner_cls, prop_name, target, src );
    }
    else
    {
        IInspectable *resolved = uwp_resolve_binding_path( src, path );
        if (resolved)
        {
            uwp_set_custom_xaml_member_obj( app_outer, owner_cls, prop_name, target, resolved );
            IInspectable_Release( resolved );
        }
    }
}

void uwp_add_xaml_binding( struct uwp_element *tel, const WCHAR *owner_cls,
                           const WCHAR *prop_name, struct uwp_element *bel )
{
    static const GUID IID_IPropertyPath_l =
        { 0x300e5d8a, 0x1ff3, 0x4d2c, { 0x95, 0xec, 0x27, 0xf8, 0x1d, 0xeb, 0xac, 0xb8 } };
    WCHAR path_buf[128] = { 0 };
    IInspectable *src = NULL;

    if (!tel || !owner_cls || !owner_cls[0] || !prop_name || !prop_name[0]) return;
    if (bel && bel->tag)
    {
        void *pp = NULL;
        if (SUCCEEDED(IInspectable_QueryInterface( bel->tag, &IID_IPropertyPath_l, &pp )) && pp)
        {
            HSTRING h_path = NULL;
            HRESULT (WINAPI *get_path)(void *, HSTRING *) = (void *)(*(void ***)pp)[6];
            if (get_path && SUCCEEDED(get_path( pp, &h_path )) && h_path)
            {
                const WCHAR *raw = WindowsGetStringRawBuffer( h_path, NULL );
                if (raw) lstrcpynW( path_buf, raw, ARRAY_SIZE(path_buf) );
                WindowsDeleteString( h_path );
            }
            IUnknown_Release( (IUnknown *)pp );
        }
    }
    if (!path_buf[0] && bel && bel->text[0])
        lstrcpynW( path_buf, bel->text, ARRAY_SIZE(path_buf) );

    if (bel && bel->data_context)
    {
        src = bel->data_context;
    }
    else
    {
        struct uwp_element *anc;
        if (xaml_vec_grow( (void **)&tel->xaml_bindings, &tel->xaml_binding_cap,
                           tel->xaml_binding_cnt + 1, sizeof(*tel->xaml_bindings), 8 ))
        {
            int idx = tel->xaml_binding_cnt++;
            lstrcpynW( tel->xaml_bindings[idx].owner_cls, owner_cls, ARRAY_SIZE(tel->xaml_bindings[idx].owner_cls) );
            lstrcpynW( tel->xaml_bindings[idx].prop_name, prop_name, ARRAY_SIZE(tel->xaml_bindings[idx].prop_name) );
            lstrcpynW( tel->xaml_bindings[idx].path, path_buf, ARRAY_SIZE(tel->xaml_bindings[idx].path) );
            tel->xaml_bindings[idx].source = NULL;
        }
        for (anc = tel; anc; anc = anc->parent)
            if (anc->data_context) { src = anc->data_context; break; }
    }

    if (src)
        uwp_eval_xaml_binding( tel, owner_cls, prop_name, path_buf, src );
}

void uwp_apply_binding( struct uwp_element *tel, void *dp, struct uwp_element *bel )
{
    static const GUID IID_IPropertyPath_l =
        { 0x300e5d8a, 0x1ff3, 0x4d2c, { 0x95, 0xec, 0x27, 0xf8, 0x1d, 0xeb, 0xac, 0xb8 } };
    HSTRING h_path = NULL;
    const WCHAR *path_str = NULL;
    IInspectable *src = NULL;
    int i;

    if (!tel || !dp) return;

    if (bel && bel->tag)
    {
        void *pp = NULL;
        if (SUCCEEDED(IInspectable_QueryInterface( bel->tag, &IID_IPropertyPath_l, &pp )) && pp)
        {
            HRESULT (WINAPI *get_path)(void *, HSTRING *) = (void *)(*(void ***)pp)[6];
            if (get_path && SUCCEEDED(get_path( pp, &h_path )) && h_path)
                path_str = WindowsGetStringRawBuffer( h_path, NULL );
            IUnknown_Release( (IUnknown *)pp );
        }
    }

    if (bel && bel->data_context)
    {
        src = bel->data_context;
    }
    else
    {
        struct uwp_element *anc;
        for (i = 0; i < tel->dc_bound_dp_cnt; i++)
            if (tel->dc_bound_dps[i].dp == dp) break;
        if (i == tel->dc_bound_dp_cnt &&
            xaml_vec_grow( (void **)&tel->dc_bound_dps, &tel->dc_bound_dp_cap,
                           tel->dc_bound_dp_cnt + 1, sizeof(*tel->dc_bound_dps), 8 ))
        {
            tel->dc_bound_dps[tel->dc_bound_dp_cnt].dp = dp;
            if ((ULONG_PTR)dp >= 0x10000) IUnknown_AddRef( (IUnknown *)dp );
            tel->dc_bound_dps[tel->dc_bound_dp_cnt].binding = bel;
            if (bel) IInspectable_AddRef( &bel->IInspectable_iface );
            tel->dc_bound_dp_cnt++;
        }
        for (anc = tel; anc; anc = anc->parent)
            if (anc->data_context) { src = anc->data_context; break; }
    }

    if (src)
    {
        if (!path_str || !path_str[0])
        {
            uwp_do_SetValue( &tel->IDependencyObject_iface, dp, src );
        }
        else
        {
            IInspectable *resolved = uwp_resolve_binding_path( src, path_str );
            if (resolved)
            {
                uwp_do_SetValue( &tel->IDependencyObject_iface, dp, resolved );
                IInspectable_Release( resolved );
            }
        }
    }

    if (h_path) WindowsDeleteString( h_path );
}

static HRESULT WINAPI uwp_fe_SetBinding( void *iface, void *dp, void *binding )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    struct uwp_element *bel = uwp_elem_from_any( binding );
    TRACE( "uwp_fe_SetBinding(%p, dp=%p, binding=%p)\n", impl, dp, binding );
    uwp_apply_binding( impl, dp, bel );
    return S_OK;
}

static const void *uwp_fe_vtbl[112] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_fe_probe6,
    [11] = uwp_fe_probe11,
    [12] = uwp_fe_probe12,
    [27] = uwp_fe_probe27,
    [28] = uwp_fe_probe28,
    [29] = uwp_fe_probe29,
    [30] = uwp_fe_probe30,
    [38] = uwp_fe_get_Style,
    [39] = uwp_fe_put_Style,
    [44] = uwp_fe_probe44,
    [48] = uwp_fe_probe48,
    [53] = uwp_fe_probe53,
    [54] = uwp_fe_probe54,
    [55] = uwp_fe_probe55,
    [57] = uwp_fe_probe57,
    [58] = uwp_fe_probe58,
    [59] = uwp_fe_probe59,
    [60] = uwp_fe_probe60,
    [61] = uwp_fe_probe61,
    [63] = uwp_fe_probe63,
    [7] = uwp_fe_get_Resources,
    [8] = uwp_fe_put_Resources,
    [9] = uwp_fe_get_Tag,
    [10] = uwp_fe_put_Tag,
    [13] = uwp_fe_get_ActualWidth,
    [14] = uwp_fe_get_ActualHeight,
    [15] = uwp_fe_get_Width,
    [16] = uwp_fe_put_Width,
    [17] = uwp_fe_get_Height,
    [18] = uwp_fe_put_Height,
    [19] = uwp_fe_get_MinWidth,
    [20] = uwp_fe_put_MinWidth,
    [21] = uwp_fe_get_MaxWidth,
    [22] = uwp_fe_put_MaxWidth,
    [23] = uwp_fe_get_MinHeight,
    [24] = uwp_fe_put_MinHeight,
    [25] = uwp_fe_get_MaxHeight,
    [26] = uwp_fe_put_MaxHeight,
    [31] = uwp_fe_get_Margin,
    [32] = uwp_fe_put_Margin,
    [33] = uwp_fe_get_Name,
    [34] = uwp_fe_put_Name,
    [35] = uwp_fe_get_BaseUri,
    [36] = uwp_fe_get_DataContext,
    [37] = uwp_fe_put_DataContext,
    [40] = uwp_fe_get_Parent,
    [41] = uwp_fe_get_FlowDirection,
    [42] = uwp_fe_put_FlowDirection,
    [43] = uwp_fe_add_Loaded,
    [45] = uwp_ui_event_add,
    [46] = uwp_fe_remove_Unloaded,
    [47] = uwp_fe_add_SizeChanged,
    [49] = uwp_fe_add_LayoutUpdated,
    [50] = uwp_fe_remove_LayoutUpdated,
    [51] = uwp_fe_FindName,
    [52] = uwp_fe_SetBinding,
    /* INavigationView ABI slots when this face is cast to NavigationView.
     * Chain: IInspectable(3) + IFrameworkElement(53) = base 56, own-method
     * order from Microsoft.UI.Xaml.winmd 2.8: own[0]=IsPaneOpen(56),
     * own[6]=FooterMenuItems(62), own[46]=SelectionChanged(102),
     * own[48]=ItemInvoked(104) — Time.dll calls *0x1c0,*0x1f0,*0x340,*0x348. */
    [56] = uwp_nav_get_IsPaneOpen,
    [62] = uwp_nav_get_FooterMenuItems,
    [102] = uwp_ui_add_SelectionChanged,
    [103] = uwp_ui_remove_SelectionChanged,
    [104] = uwp_ui_add_ItemInvoked,
    [105] = uwp_ui_remove_ItemInvoked,
    [64] = uwp_fe_probe64, [65] = uwp_fe_probe65, [66] = uwp_fe_probe66, [67] = uwp_fe_probe67, [68] = uwp_fe_probe68, [69] = uwp_fe_probe69, [70] = uwp_fe_probe70, [71] = uwp_fe_probe71, [72] = uwp_fe_probe72, [73] = uwp_fe_probe73, [74] = uwp_fe_probe74, [75] = uwp_fe_probe75, [76] = uwp_fe_probe76, [77] = uwp_fe_probe77, [78] = uwp_fe_probe78, [79] = uwp_fe_probe79, [80] = uwp_fe_probe80, [81] = uwp_fe_probe81, [82] = uwp_fe_probe82, [83] = uwp_fe_probe83, [84] = uwp_fe_probe84, [85] = uwp_fe_probe85, [86] = uwp_fe_probe86, [87] = uwp_fe_probe87, [88] = uwp_fe_probe88, [89] = uwp_fe_probe89, [90] = uwp_fe_probe90, [91] = uwp_fe_probe91, [92] = uwp_fe_probe92, [93] = uwp_fe_probe93, [94] = uwp_fe_probe94, [95] = uwp_fe_probe95, [96] = uwp_fe_probe96, [97] = uwp_fe_probe97, [98] = uwp_fe_probe98, [99] = uwp_fe_probe99, [100] = uwp_fe_probe100, [101] = uwp_fe_probe101, [106] = uwp_fe_probe106, [107] = uwp_fe_probe107, [108] = uwp_fe_probe108, [109] = uwp_fe_probe109, [110] = uwp_fe_probe110, [111] = uwp_fe_probe111
};

/* Default face for Microsoft.UI.Xaml.Controls.NavigationView: CoreRT
 * projections call INavigationView members straight through the instance
 * vtable, so the INavigationView ABI slots (41/42 ItemInvoked, 53/54
 * SelectionChanged per alphabetical winmd metadata over Control's 24 slots)
 * must live in the face the element hands out at creation.  All other slots
 * mirror the FrameworkElement face. */
static HRESULT WINAPI uwp_vs_get_Name( void *iface, HSTRING *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    *out = NULL;
    if (!impl || !impl->name[0]) return S_OK;
    return WindowsCreateString( impl->name, lstrlenW( impl->name ), out );
}

static const void *uwp_visual_state_vtbl[32] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_vs_get_Name,
    [7 ... 31] = uwp_generic_noop,
};

static const void *uwp_noop_vtbl[128] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    /* INavigationView ABI events (disasm-confirmed slots over Control's 24):
     * 56/57 = ItemInvoked, 62/63 = SelectionChanged. */
    [6 ... 55] = uwp_generic_noop,
    [56] = uwp_ui_add_ItemInvoked,
    [57] = uwp_ui_remove_ItemInvoked,
    [58 ... 61] = uwp_generic_noop,
    [62] = uwp_ui_add_SelectionChanged,
    [63] = uwp_ui_remove_SelectionChanged,
    [64 ... 127] = uwp_generic_noop,
};

HRESULT uwp_frame_navigate_impl( void *iface, void *type_name_ptr, void *param, void *transition_info, boolean *result );
static HRESULT WINAPI uwp_frame_Navigate( void *iface, void *type_name_ptr, boolean *result )
{
    return uwp_frame_navigate_impl( iface, type_name_ptr, NULL, NULL, result );
}
HRESULT WINAPI uwp_frame2_Navigate( void *iface, void *type_name_ptr, void *param, void *transition_info, boolean *result )
{
    return uwp_frame_navigate_impl( iface, type_name_ptr, param, transition_info, result );
}

static HRESULT WINAPI uwp_frame_add_Navigated( void *iface, void *handler, EventRegistrationToken *token )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return E_NOINTERFACE;
    TRACE( "uwp_frame_add_Navigated on %s (%s) handler=%p\n",
           debugstr_w(impl->class_name), debugstr_w(impl->name), handler );
    return xaml_event_registry_add( &impl->frame_navigated_reg, handler, token );
}
static HRESULT WINAPI uwp_frame_remove_Navigated( void *iface, EventRegistrationToken token )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return E_NOINTERFACE;
    return xaml_event_registry_remove( &impl->frame_navigated_reg, token );
}

static HRESULT WINAPI uwp_frame_get_CacheSize( void *iface, INT32 *out )
{
    if (out) *out = 10;
    return S_OK;
}
static HRESULT WINAPI uwp_frame_get_CanGoBack( void *iface, boolean *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    unsigned int sz = 0;
    if (impl && impl->back_stack) uwp_vec_get_Size( impl->back_stack, &sz );
    if (out) *out = sz > 0 ? TRUE : FALSE;
    return S_OK;
}
static HRESULT WINAPI uwp_frame_get_CanGoForward( void *iface, boolean *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    unsigned int sz = 0;
    if (impl && impl->forward_stack) uwp_vec_get_Size( impl->forward_stack, &sz );
    if (out) *out = sz > 0 ? TRUE : FALSE;
    return S_OK;
}
static HRESULT WINAPI uwp_frame_get_SourcePageType( void *iface, TypeName *out )
{
    if (out) { out->Name = NULL; out->Kind = 0; }
    return S_OK;
}
static HRESULT WINAPI uwp_frame_get_BackStackDepth( void *iface, INT32 *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    unsigned int sz = 0;
    if (impl && impl->back_stack) uwp_vec_get_Size( impl->back_stack, &sz );
    if (out) *out = (INT32)sz;
    return S_OK;
}

static const void *uwp_frame_vtbl[48] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_frame_get_CacheSize,
    [7] = uwp_generic_noop,        /* put_CacheSize */
    [8] = uwp_frame_get_CanGoBack,
    [9] = uwp_frame_get_CanGoForward,
    [10] = uwp_frame_get_SourcePageType, /* get_CurrentSourcePageType */
    [11] = uwp_frame_get_SourcePageType, /* get_SourcePageType */
    [12] = uwp_generic_noop,       /* put_SourcePageType */
    [13] = uwp_frame_get_BackStackDepth,
    [14] = uwp_frame_add_Navigated,
    [15] = uwp_frame_remove_Navigated,
    [16] = uwp_ui_event_add,       /* add_Navigating */
    [17] = uwp_generic_noop,       /* remove_Navigating */
    [18] = uwp_ui_event_add,       /* add_NavigationFailed */
    [19] = uwp_generic_noop,       /* remove_NavigationFailed */
    [20] = uwp_ui_event_add,       /* add_NavigationStopped */
    [21] = uwp_generic_noop,       /* remove_NavigationStopped */
    [22] = uwp_generic_noop,       /* GoBack */
    [23] = uwp_frame_Navigate,
    [24] = uwp_frame_NavigateWithParam,
    [25 ... 47] = uwp_generic_noop,
};





void xaml_layout_render( HWND hwnd, HDC hdc, int width, int height, IInspectable *content );
BOOL uwp_handle_mouse_input( HWND hwnd, UINT msg, int x, int y, int width, int height, IInspectable *root_content );


ID3D11Device *g_island_d3d11_dev = NULL;
ID3D11DeviceContext *g_island_d3d11_ctx = NULL;
BOOL g_island_d3d11_failed = FALSE;

static HRESULT WINAPI uwp_rtt_QI( IInspectable *iface, REFIID iid, void **out )
{
    return uwp_sub_QueryInterface( iface, iid, out );
}
static ULONG WINAPI uwp_rtt_AddRef( IInspectable *iface )
{
    return uwp_sub_AddRef( iface );
}
static ULONG WINAPI uwp_rtt_Release( IInspectable *iface )
{
    return uwp_sub_Release( iface );
}
static HRESULT WINAPI uwp_rtt_GetIids_( IInspectable *iface, ULONG *count, GUID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}
static HRESULT WINAPI uwp_rtt_GetRuntimeClassName_( IInspectable *iface, HSTRING *name )
{
    return uwp_sub_GetRuntimeClassName( iface, name );
}
static HRESULT WINAPI uwp_rtt_GetTrustLevel_( IInspectable *iface, UINT *level )
{
    if (level) *level = 0 /* BaseTrust */;
    return S_OK;
}
static HRESULT WINAPI uwp_rtt_AddRefToTracker( IInspectable *iface )
{
    uwp_sub_AddRef( iface );
    return S_OK;
}
static HRESULT WINAPI uwp_rtt_ReleaseToTracker( IInspectable *iface )
{
    uwp_sub_Release( iface );
    return S_OK;
}
static HRESULT WINAPI uwp_rtt_GetStrongValue( IInspectable *iface, IInspectable **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    *out = &impl->nondelegating_iface;
    uwp_nondelegating_AddRef( &impl->nondelegating_iface );
    return S_OK;
}
const struct uwp_rtt_target_vtbl uwp_rtt_vtbl =
{
    { uwp_rtt_QI, uwp_rtt_AddRef, uwp_rtt_Release,
      uwp_rtt_GetIids_, uwp_rtt_GetRuntimeClassName_, uwp_rtt_GetTrustLevel_ },
    uwp_rtt_AddRefToTracker,
    uwp_rtt_ReleaseToTracker,
    uwp_rtt_GetStrongValue,
};
static double uwp_ease_in_core( const WCHAR *cls, double t )
{
    if (wcsstr( cls, L"SineEase" ))
        return 1.0 - sin( (1.0 - t) * 1.5707963267948966 );
    if (wcsstr( cls, L"QuadraticEase" ))
        return t * t;
    if (wcsstr( cls, L"CubicEase" ))
        return t * t * t;
    if (wcsstr( cls, L"QuarticEase" ))
        return t * t * t * t;
    if (wcsstr( cls, L"QuinticEase" ))
        return t * t * t * t * t;
    if (wcsstr( cls, L"CircleEase" ))
        return 1.0 - sqrt( fmax( 0.0, 1.0 - t * t ) );
    if (wcsstr( cls, L"ExponentialEase" ))
        return (t <= 0.0) ? 0.0 : (exp( 2.0 * t ) - 1.0) / (exp( 2.0 ) - 1.0);
    if (wcsstr( cls, L"PowerEase" ))
        return t * t;
    return t;
}

static HRESULT WINAPI uwp_ease_get_EasingMode( IInspectable *iface, INT32 *val )
{
    struct uwp_element *impl = CONTAINING_RECORD( iface, struct uwp_element, IEasingFunctionBase_iface );
    if (!val) return E_POINTER;
    *val = impl->easing_mode;
    return S_OK;
}

static HRESULT WINAPI uwp_ease_put_EasingMode( IInspectable *iface, INT32 val )
{
    struct uwp_element *impl = CONTAINING_RECORD( iface, struct uwp_element, IEasingFunctionBase_iface );
    impl->easing_mode = val;
    return S_OK;
}

static HRESULT WINAPI uwp_ease_Ease( IInspectable *iface, double t, double *out )
{
    struct uwp_element *impl = CONTAINING_RECORD( iface, struct uwp_element, IEasingFunctionBase_iface );
    if (!out) return E_POINTER;
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;
    /* EasingMode: 0=EaseOut, 1=EaseIn, 2=EaseInOut */
    if (impl->easing_mode == 1)
        *out = uwp_ease_in_core( impl->class_name, t );
    else if (impl->easing_mode == 2)
        *out = (t < 0.5) ? (uwp_ease_in_core( impl->class_name, t * 2.0 ) * 0.5)
                         : ((1.0 - uwp_ease_in_core( impl->class_name, (1.0 - t) * 2.0 )) * 0.5 + 0.5);
    else
        *out = 1.0 - uwp_ease_in_core( impl->class_name, 1.0 - t );
    return S_OK;
}

static const void *uwp_ease_vtbl[] =
{
    uwp_sub_QueryInterface, uwp_sub_AddRef, uwp_sub_Release,
    uwp_sub_GetIids, uwp_sub_GetRuntimeClassName, uwp_sub_GetTrustLevel,
    uwp_ease_get_EasingMode, /* slot 6: get_EasingMode */
    uwp_ease_put_EasingMode, /* slot 7: put_EasingMode */
    uwp_ease_Ease,           /* slot 8: Ease(double) */
};


static HRESULT WINAPI uwp_scp_get_CompositionScaleX( void *iface, float *val )
{
    if (val) *val = 1.0f;
    return S_OK;
}
static HRESULT WINAPI uwp_scp_get_CompositionScaleY( void *iface, float *val )
{
    if (val) *val = 1.0f;
    return S_OK;
}
static const GUID IID_ICoreInputSourceBase_UWP =
    { 0x9f488807, 0x4580, 0x4be8, { 0xbe, 0x68, 0x92, 0xa9, 0x31, 0x17, 0x13, 0xbb } };
static const GUID IID_ICorePointerInputSource_UWP =
    { 0xbbf1bb18, 0xe47a, 0x48eb, { 0x88, 0x07, 0xf8, 0xf8, 0xd3, 0xea, 0x45, 0x51 } };
static const GUID IID_ICorePointerInputSource2_UWP =
    { 0xd70370ba, 0x1953, 0x4271, { 0xb8, 0x65, 0x72, 0x77, 0x0c, 0x92, 0x7d, 0x03 } };

#define CIIS_MAX_HANDLERS 16
#define CIIS_MAX_SOURCES  16

struct core_ind_input_source
{
    const void **lpVtbl_base;
    const void **lpVtbl_ptr;
    const void **lpVtbl_ptr2;
    LONG ref;
    LONG next_token;
    boolean input_enabled;
    boolean has_capture;
    float last_x;
    float last_y;
    void *handlers[7][CIIS_MAX_HANDLERS];
    INT64 tokens[7][CIIS_MAX_HANDLERS];
};

static CRITICAL_SECTION g_ciis_cs = { NULL, -1, 0, 0, 0, 0 };
static struct core_ind_input_source *g_ciis_sources[CIIS_MAX_SOURCES];

static inline struct core_ind_input_source *ciis_from_base( void *iface )
{ return CONTAINING_RECORD( iface, struct core_ind_input_source, lpVtbl_base ); }
static inline struct core_ind_input_source *ciis_from_ptr( void *iface )
{ return CONTAINING_RECORD( iface, struct core_ind_input_source, lpVtbl_ptr ); }
static inline struct core_ind_input_source *ciis_from_ptr2( void *iface )
{ return CONTAINING_RECORD( iface, struct core_ind_input_source, lpVtbl_ptr2 ); }

static HRESULT WINAPI ciis_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct core_ind_input_source *impl = ciis_from_base( iface );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_ICoreInputSourceBase_UWP ))
    {
        InterlockedIncrement( &impl->ref );
        *out = &impl->lpVtbl_base;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_ICorePointerInputSource_UWP ))
    {
        InterlockedIncrement( &impl->ref );
        *out = &impl->lpVtbl_ptr;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_ICorePointerInputSource2_UWP ))
    {
        InterlockedIncrement( &impl->ref );
        *out = &impl->lpVtbl_ptr2;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI ciis_AddRef( void *iface )
{ return InterlockedIncrement( &ciis_from_base( iface )->ref ); }
static ULONG WINAPI ciis_Release( void *iface )
{
    struct core_ind_input_source *impl = ciis_from_base( iface );
    ULONG r = InterlockedDecrement( &impl->ref );
    if (!r)
    {
        int i, k;
        EnterCriticalSection( &g_ciis_cs );
        for (i = 0; i < CIIS_MAX_SOURCES; i++)
            if (g_ciis_sources[i] == impl) g_ciis_sources[i] = NULL;
        LeaveCriticalSection( &g_ciis_cs );
        for (k = 0; k < 7; k++)
            for (i = 0; i < CIIS_MAX_HANDLERS; i++)
                if (impl->handlers[k][i]) IUnknown_Release( (IUnknown *)impl->handlers[k][i] );
        free( impl );
    }
    return r;
}
static HRESULT WINAPI ciis_GetIids( void *iface, ULONG *cnt, IID **iids )
{ if (cnt) *cnt = 0; if (iids) *iids = NULL; return S_OK; }
static HRESULT WINAPI ciis_GetRuntimeClassName( void *iface, HSTRING *name )
{ return WindowsCreateString( L"Windows.UI.Core.CoreIndependentInputSource", 42, name ); }
static HRESULT WINAPI ciis_GetTrustLevel( void *iface, TrustLevel *lvl )
{ if (lvl) *lvl = BaseTrust; return S_OK; }

static HRESULT WINAPI ciis_get_Dispatcher( void *iface, ICoreDispatcher **out )
{
    ICoreWindow *cw = get_current_core_window();
    if (!out) return E_POINTER;
    *out = NULL;
    if (cw) return ICoreWindow_get_Dispatcher( cw, out );
    return S_OK;
}
static HRESULT WINAPI ciis_get_IsInputEnabled( void *iface, boolean *val )
{ if (val) *val = ciis_from_base( iface )->input_enabled; return S_OK; }
static HRESULT WINAPI ciis_put_IsInputEnabled( void *iface, boolean val )
{ ciis_from_base( iface )->input_enabled = val; return S_OK; }
static HRESULT WINAPI ciis_event_add( void *iface, void *handler, EventRegistrationToken *token )
{ if (token) token->value = 1; return S_OK; }
static HRESULT WINAPI ciis_event_remove( void *iface, EventRegistrationToken token )
{ return S_OK; }

static HRESULT ciis_ptr_add_kind( void *iface, int kind, void *handler, EventRegistrationToken *token )
{
    struct core_ind_input_source *impl = ciis_from_ptr( iface );
    int i;
    if (!handler) return E_POINTER;
    EnterCriticalSection( &g_ciis_cs );
    for (i = 0; i < CIIS_MAX_HANDLERS; i++)
    {
        if (!impl->handlers[kind][i])
        {
            INT64 tok = InterlockedIncrement( &impl->next_token );
            IUnknown_AddRef( (IUnknown *)handler );
            impl->handlers[kind][i] = handler;
            impl->tokens[kind][i] = tok;
            if (token) token->value = tok;
            LeaveCriticalSection( &g_ciis_cs );
            ERR( "ciis_ptr_add_kind kind=%d handler=%p tok=%lld\n", kind, handler, (long long)tok );
            return S_OK;
        }
    }
    LeaveCriticalSection( &g_ciis_cs );
    return E_OUTOFMEMORY;
}

static HRESULT ciis_ptr_remove_kind( void *iface, int kind, EventRegistrationToken token )
{
    struct core_ind_input_source *impl = ciis_from_ptr( iface );
    int i;
    EnterCriticalSection( &g_ciis_cs );
    for (i = 0; i < CIIS_MAX_HANDLERS; i++)
    {
        if (impl->handlers[kind][i] && impl->tokens[kind][i] == token.value)
        {
            void *h = impl->handlers[kind][i];
            impl->handlers[kind][i] = NULL;
            impl->tokens[kind][i] = 0;
            LeaveCriticalSection( &g_ciis_cs );
            IUnknown_Release( (IUnknown *)h );
            return S_OK;
        }
    }
    LeaveCriticalSection( &g_ciis_cs );
    return S_OK;
}

#define CIIS_PTR_EV( name, kind ) \
static HRESULT WINAPI ciis_add_##name( void *iface, void *h, EventRegistrationToken *t ) \
{ return ciis_ptr_add_kind( iface, kind, h, t ); } \
static HRESULT WINAPI ciis_remove_##name( void *iface, EventRegistrationToken t ) \
{ return ciis_ptr_remove_kind( iface, kind, t ); }

CIIS_PTR_EV( PointerPressed,      XAML_PTR_PRESSED )
CIIS_PTR_EV( PointerMoved,        XAML_PTR_MOVED )
CIIS_PTR_EV( PointerReleased,     XAML_PTR_RELEASED )
CIIS_PTR_EV( PointerWheelChanged, XAML_PTR_WHEEL )
CIIS_PTR_EV( PointerEntered,      XAML_PTR_ENTERED )
CIIS_PTR_EV( PointerExited,       XAML_PTR_EXITED )
CIIS_PTR_EV( PointerCaptureLost,  XAML_PTR_CAPTURE_LOST )

void uwp_ciis_dispatch_pointer_event( enum xaml_pointer_event_kind kind, const struct xaml_pointer_info *info )
{
    struct core_ind_input_source *srcs[CIIS_MAX_SOURCES];
    void *handlers[CIIS_MAX_HANDLERS];
    int n_srcs = 0, i, k;
    __x_ABI_CWindows_CUI_CCore_CIPointerEventArgs *args = NULL;

    if ((unsigned int)kind >= 7 || !info) return;

    EnterCriticalSection( &g_ciis_cs );
    for (i = 0; i < CIIS_MAX_SOURCES; i++)
    {
        if (g_ciis_sources[i] && g_ciis_sources[i]->input_enabled)
        {
            InterlockedIncrement( &g_ciis_sources[i]->ref );
            srcs[n_srcs++] = g_ciis_sources[i];
        }
    }
    LeaveCriticalSection( &g_ciis_cs );

    if (!n_srcs) return;
    if (kind == XAML_PTR_PRESSED || kind == XAML_PTR_WHEEL)
        ERR( "uwp_ciis_dispatch_pointer_event kind=%d pos=(%.1f,%.1f) R=%d L=%d wheel=%d srcs=%d\n",
             kind, info->x, info->y, info->is_right, info->is_left, info->wheel_delta, n_srcs );
    args = xaml_core_pointer_args_create( info );
    if (!args)
    {
        for (i = 0; i < n_srcs; i++) ciis_Release( &srcs[i]->lpVtbl_base );
        return;
    }

    for (i = 0; i < n_srcs; i++)
    {
        struct core_ind_input_source *src = srcs[i];
        int n_h = 0;
        src->last_x = info->x;
        src->last_y = info->y;
        EnterCriticalSection( &g_ciis_cs );
        for (k = 0; k < CIIS_MAX_HANDLERS; k++)
        {
            if (src->handlers[kind][k])
            {
                IUnknown_AddRef( (IUnknown *)src->handlers[kind][k] );
                handlers[n_h++] = src->handlers[kind][k];
            }
        }
        LeaveCriticalSection( &g_ciis_cs );
        for (k = 0; k < n_h; k++)
        {
            typedef HRESULT (WINAPI *invoke_fn)( void *iface, void *sender, void *args );
            void ***vtbl = (void ***)handlers[k];
            invoke_fn fn = (invoke_fn)((*vtbl)[3]);
            __TRY
            {
                fn( handlers[k], &src->lpVtbl_ptr, args );
            }
            __EXCEPT_ALL
            {
            }
            __ENDTRY;
            IUnknown_Release( (IUnknown *)handlers[k] );
        }
        ciis_Release( &src->lpVtbl_base );
    }
    __x_ABI_CWindows_CUI_CCore_CIPointerEventArgs_Release( args );
}

static const void *ciis_base_vtbl[] =
{
    ciis_QueryInterface,
    ciis_AddRef,
    ciis_Release,
    ciis_GetIids,
    ciis_GetRuntimeClassName,
    ciis_GetTrustLevel,
    ciis_get_Dispatcher,
    ciis_get_IsInputEnabled,
    ciis_put_IsInputEnabled,
    ciis_event_add,
    ciis_event_remove,
};

static HRESULT WINAPI ciis_ptr_QI( void *iface, REFIID iid, void **out )
{ return ciis_QueryInterface( &ciis_from_ptr( iface )->lpVtbl_base, iid, out ); }
static ULONG WINAPI ciis_ptr_AddRef( void *iface )
{ return ciis_AddRef( &ciis_from_ptr( iface )->lpVtbl_base ); }
static ULONG WINAPI ciis_ptr_Release( void *iface )
{ return ciis_Release( &ciis_from_ptr( iface )->lpVtbl_base ); }
static HRESULT WINAPI ciis_ptr_ReleaseCapture( void *iface )
{ ciis_from_ptr( iface )->has_capture = FALSE; return S_OK; }
static HRESULT WINAPI ciis_ptr_SetCapture( void *iface )
{ ciis_from_ptr( iface )->has_capture = TRUE; return S_OK; }
static HRESULT WINAPI ciis_ptr_get_HasCapture( void *iface, boolean *val )
{ if (val) *val = ciis_from_ptr( iface )->has_capture; return S_OK; }
static HRESULT WINAPI ciis_ptr_get_PointerPosition( void *iface, Point *pt )
{
    struct core_ind_input_source *impl = ciis_from_ptr( iface );
    if (pt) { pt->X = impl->last_x; pt->Y = impl->last_y; }
    return S_OK;
}
static HRESULT WINAPI ciis_ptr_get_PointerCursor( void *iface, void **out )
{ if (out) *out = NULL; return S_OK; }
static HRESULT WINAPI ciis_ptr_put_PointerCursor( void *iface, void *cursor )
{ return S_OK; }

static const void *ciis_ptr_vtbl[] =
{
    ciis_ptr_QI,
    ciis_ptr_AddRef,
    ciis_ptr_Release,
    ciis_GetIids,
    ciis_GetRuntimeClassName,
    ciis_GetTrustLevel,
    ciis_ptr_ReleaseCapture,      /* 6: ReleasePointerCapture */
    ciis_ptr_SetCapture,          /* 7: SetPointerCapture */
    ciis_ptr_get_HasCapture,      /* 8: get_HasCapture */
    ciis_ptr_get_PointerPosition, /* 9: get_PointerPosition */
    ciis_ptr_get_PointerCursor,   /* 10: get_PointerCursor */
    ciis_ptr_put_PointerCursor,   /* 11: put_PointerCursor */
    ciis_add_PointerCaptureLost,      /* 12: add_PointerCaptureLost */
    ciis_remove_PointerCaptureLost,   /* 13: remove_PointerCaptureLost */
    ciis_add_PointerEntered,          /* 14: add_PointerEntered */
    ciis_remove_PointerEntered,       /* 15: remove_PointerEntered */
    ciis_add_PointerExited,           /* 16: add_PointerExited */
    ciis_remove_PointerExited,        /* 17: remove_PointerExited */
    ciis_add_PointerMoved,            /* 18: add_PointerMoved */
    ciis_remove_PointerMoved,         /* 19: remove_PointerMoved */
    ciis_add_PointerPressed,          /* 20: add_PointerPressed */
    ciis_remove_PointerPressed,       /* 21: remove_PointerPressed */
    ciis_add_PointerReleased,         /* 22: add_PointerReleased */
    ciis_remove_PointerReleased,      /* 23: remove_PointerReleased */
    ciis_add_PointerWheelChanged,     /* 24: add_PointerWheelChanged */
    ciis_remove_PointerWheelChanged,  /* 25: remove_PointerWheelChanged */
};

static HRESULT WINAPI ciis_ptr2_QI( void *iface, REFIID iid, void **out )
{ return ciis_QueryInterface( &ciis_from_ptr2( iface )->lpVtbl_base, iid, out ); }
static ULONG WINAPI ciis_ptr2_AddRef( void *iface )
{ return ciis_AddRef( &ciis_from_ptr2( iface )->lpVtbl_base ); }
static ULONG WINAPI ciis_ptr2_Release( void *iface )
{ return ciis_Release( &ciis_from_ptr2( iface )->lpVtbl_base ); }
static HRESULT WINAPI ciis_ptr2_get_DispatcherQueue( void *iface, void **out )
{ return uwp_do_get_DispatcherQueue( NULL, out ); }

static const void *ciis_ptr2_vtbl[] =
{
    ciis_ptr2_QI,
    ciis_ptr2_AddRef,
    ciis_ptr2_Release,
    ciis_GetIids,
    ciis_GetRuntimeClassName,
    ciis_GetTrustLevel,
    ciis_ptr2_get_DispatcherQueue,
};

static HRESULT WINAPI uwp_scp_CreateCoreIndependentInputSource( void *iface, UINT32 device_types, void **out )
{
    struct core_ind_input_source *src;
    int i;
    ERR( "uwp_scp_CreateCoreIndependentInputSource dev=%#x\n", device_types );
    if (!out) return E_POINTER;
    if (!(src = calloc( 1, sizeof(*src) ))) return E_OUTOFMEMORY;
    src->lpVtbl_base = ciis_base_vtbl;
    src->lpVtbl_ptr = ciis_ptr_vtbl;
    src->lpVtbl_ptr2 = ciis_ptr2_vtbl;
    src->ref = 1;
    src->input_enabled = TRUE;
    EnterCriticalSection( &g_ciis_cs );
    for (i = 0; i < CIIS_MAX_SOURCES; i++)
    {
        if (!g_ciis_sources[i])
        {
            g_ciis_sources[i] = src;
            break;
        }
    }
    LeaveCriticalSection( &g_ciis_cs );
    *out = &src->lpVtbl_base;
    return S_OK;
}
static const void *uwp_scp_vtbl[16] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_scp_get_CompositionScaleX,
    [7] = uwp_scp_get_CompositionScaleY,
    [8] = uwp_ui_event_add,
    [9] = uwp_generic_noop,
    [10] = uwp_scp_CreateCoreIndependentInputSource,
    [11 ... 15] = uwp_generic_noop,
};
void uwp_notify_swapchain_panel_resize( void *el_ptr, UINT w, UINT h )
{
    struct uwp_element *impl = (struct uwp_element *)el_ptr;
    if (!impl || w <= 1 || h <= 1) return;
    impl->actual_w = (double)w;
    impl->actual_h = (double)h;
    uwp_fire_size_and_layout_changed( impl );
}

static HRESULT WINAPI uwp_scp_native_SetSwapChain( void *iface, void *sc )
{
    struct uwp_element *impl = CONTAINING_RECORD( iface, struct uwp_element, ISwapChainPanelNative2_iface );
    HRESULT hr;
    ERR( "uwp_scp_native_SetSwapChain iface=%p sc=%p\n", iface, sc );
    xaml_set_active_swapchain_panel( impl, sc );
    hr = xaml_attach_swapchain_panel_content( (IUnknown *)sc, &impl->scp_visual );
    if (sc)
    {
        UINT w = 0, h = 0;
        xaml_get_host_window_size( &w, &h );
        if (w > 1 && h > 1)
        {
            impl->actual_w = (double)w;
            impl->actual_h = (double)h;
            ERR( "uwp_scp_native_SetSwapChain firing SizeChanged %ux%u (handlers=%d)\n",
                 w, h, impl->size_changed_cnt );
            uwp_fire_size_and_layout_changed( impl );
        }
    }
    return hr;
}
static HRESULT WINAPI uwp_scp_native_SetSwapChainHandle( void *iface, HANDLE h )
{
    struct uwp_element *impl = CONTAINING_RECORD( iface, struct uwp_element, ISwapChainPanelNative2_iface );
    ERR( "uwp_scp_native_SetSwapChainHandle iface=%p h=%p\n", iface, h );
    return xaml_attach_swapchain_panel_handle( h, &impl->scp_visual );
}
static const void *uwp_scp_native_vtbl[8] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_scp_native_SetSwapChain,
    [4] = uwp_scp_native_SetSwapChainHandle,
    [5 ... 7] = uwp_generic_noop,
};

HRESULT uwp_element_create( const WCHAR *class_name, IInspectable *outer, IInspectable **inner, void **instance )
{
    struct uwp_element *impl;
    if (!instance) return E_POINTER;
    impl = calloc( 1, sizeof(*impl) );
    if (!impl) return E_OUTOFMEMORY;

    impl->nondelegating_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_nondelegating_vtbl;
    impl->IInspectable_iface.lpVtbl = (class_name && !wcscmp( class_name, L"Windows.UI.Xaml.DispatcherTimer" ))
        ? (const struct IInspectableVtbl *)uwp_timer_vtbl
        : (class_name && !wcscmp( class_name, L"Microsoft.UI.Xaml.Controls.NavigationView" ))
        ? (const struct IInspectableVtbl *)uwp_navview_vtbl
        : (const struct IInspectableVtbl *)uwp_fe_vtbl;
    impl->IDependencyObject_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_do_vtbl;
    impl->IDependencyObject2_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_do2_vtbl;
    impl->IUIElement_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_ui_vtbl;
    impl->IFrameworkElement_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_fe_vtbl;
    impl->IFrameworkElement2_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_fe2_vtbl;
    impl->IFrameworkElement3_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_fe3_vtbl;
    impl->IFrameworkElement7_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_fe7_vtbl;
    impl->IControl_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_ctrl_vtbl;
    impl->IControlProtected_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_ctrl_prot_vtbl;
    impl->IContentControl_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_cc_vtbl;
    impl->IUserControl_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_cc_vtbl;
    impl->IPage_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_page_vtbl;
    impl->IFrame_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_frame_vtbl;
    impl->IFrame2_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_frame2_vtbl;
    impl->IFrame3_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_frame3_vtbl;
    impl->IFrame5_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_frame5_vtbl;
    impl->nav_stack_enabled = TRUE;
    impl->IButtonBase_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_btn_vtbl;
    impl->IToggleButton_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_tbtn_vtbl;
    impl->IPanel_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_panel_vtbl;
    impl->IGrid_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_grid_vtbl;
    impl->IGrid3_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_grid3_vtbl;
    impl->IBorder_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_border_vtbl;
    impl->ITextBlock_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_tb_vtbl;
    impl->IParagraph_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_paragraph_vtbl;
    impl->IRun_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_run_vtbl;
    impl->IBinding_iface.lpVtbl = (const struct IInspectableVtbl *)&uwp_binding_vtbl;
    impl->IPropertyPath_iface.lpVtbl = (const struct IInspectableVtbl *)&uwp_pp_vtbl;
    impl->IRowDefinition_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_rowcol_vtbl;
    impl->IColumnDefinition_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_rowcol_vtbl;
    impl->IStyle_iface.lpVtbl = (!wcscmp( class_name, L"Windows.UI.Xaml.VisualState" ) ||
                                 !wcscmp( class_name, L"Windows.UI.Xaml.VisualStateGroup" ))
        ? (const struct IInspectableVtbl *)uwp_visual_state_vtbl
        : (const struct IInspectableVtbl *)uwp_noop_vtbl;
    impl->IDataTemplate_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_data_template_vtbl;
    impl->IDataTemplateSelector_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_dts_vtbl;
    impl->IDataTemplateSelector2_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_dts2_vtbl;
    impl->IBrush_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_brush_vtbl;
    impl->ISolidColorBrush_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_scb_vtbl;
    impl->IFlyout_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_flyout_vtbl;
    impl->ISelector_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_selector_vtbl;
    impl->IItemsControl_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_items_ctrl_vtbl;
    impl->IStateTrigger_iface.lpVtbl = (const struct IInspectableVtbl *)&uwp_state_trigger_vtbl;
    impl->IStateTriggerPub_iface.lpVtbl = (const struct IInspectableVtbl *)&uwp_state_trigger_pub_vtbl;
    impl->IObservableVector_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_el_obsvec_vtbl;
    impl->IVector_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_el_vec_vtbl;
    impl->IIterable_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_el_iterable_vtbl;
    impl->IVectorView_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_el_vecview_vtbl;
    impl->IScrollViewer_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_sv_vtbl;
    impl->IDesktopWindowXamlSourceNative_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_dwxs_native_vtbl;
    impl->IUIElement7_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_ui7_vtbl;
    impl->IKeyboardAccelerator_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_ka_vtbl;
    impl->IXamlUICommand_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_xaml_cmd_vtbl;
    impl->ICommand_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_icmd_vtbl;
    impl->IMenuFlyoutItem_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_mfi_vtbl;
    impl->IMenuFlyoutItem3_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_mfi3_vtbl;
    impl->IToggleMenuFlyoutItem_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_tmfi_vtbl;
    impl->IMenuFlyoutSubItem_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_mfsi_vtbl;
    impl->IMenuFlyout2_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_mf2_vtbl;
    impl->IUIElement4_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_ui4_vtbl;
    impl->IUIElement5_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_ui5_vtbl;
    impl->IButtonWithFlyout_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_button_flyout_vtbl;
    impl->IContentDialog_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_cd_vtbl;
    impl->IContentDialog2_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_cd2_vtbl;
    impl->ISwapChainPanel_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_scp_vtbl;
    impl->ISwapChainPanelNative2_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_scp_native_vtbl;
    impl->ITeachingTip_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_teachingtip_vtbl;
    impl->IAutoSuggestBox_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_asb_vtbl;
    impl->IAutoSuggestBox2_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_asb2_vtbl;
    impl->ISplitView_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_splitview_vtbl;
    impl->ISplitView2_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_splitview2_vtbl;
    impl->IEasingFunctionBase_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_ease_vtbl;
    impl->IMediaPlayerElement_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_mpe_vtbl;
    impl->IBitmapImage_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_bi_vtbl;
    impl->IBitmapSource_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_bs_vtbl;
    impl->IImage_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_img_vtbl;
    impl->IShape_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_shape_vtbl;
    impl->IXamlCompositionBrushBaseProtected_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_xcbb_prot_vtbl;
    impl->ILoadedImageSurface_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_lis_vtbl;
    impl->media_stretch = 2; /* Uniform */
    impl->IWeakReferenceSource_iface.lpVtbl = &uwp_wrs_vtbl;
    impl->IDispatch_iface.lpVtbl = &uwp_disp_vtbl;
    impl->IReferenceTrackerTarget_iface.lpVtbl = (const IInspectableVtbl *)&uwp_rtt_vtbl;
    impl->ITypeInfo_iface.lpVtbl = &uwp_disp_ti_vtbl;

    impl->ref = 1;
    impl->outer = outer;
    impl->accel_enabled = TRUE;
    lstrcpynW( impl->class_name, class_name ? class_name : L"Windows.UI.Xaml.FrameworkElement", ARRAY_SIZE(impl->class_name) );
    impl->kind = classify_uwp_class( impl->class_name );
    if (impl->kind == UWP_KIND_PAGE) g_last_created_page = impl;
    impl->grid_row_span = 1;
    impl->grid_col_span = 1;
    impl->grid_len_type = 2; /* Star */
    impl->grid_len_val = 1.0;
    impl->is_enabled = TRUE;
    impl->is_hit_test_visible = TRUE;
    impl->opacity = 1.0;
    impl->brush_opacity = 1.0;
    impl->h_align = 3; /* Stretch */
    impl->v_align = 3; /* Stretch */
    impl->font_size = 16.0;
    g_layout_dirty = TRUE;
    if (impl->kind != UWP_KIND_BRUSH && impl->kind != UWP_KIND_STYLE &&
        impl->kind != UWP_KIND_RESOURCE_DICT && impl->kind != UWP_KIND_COLOR &&
        impl->kind != UWP_KIND_ROW_DEF && impl->kind != UWP_KIND_COL_DEF &&
        impl->kind != UWP_KIND_DEFERRED && impl->kind != UWP_KIND_ACCELERATOR &&
        impl->kind != UWP_KIND_XAML_COMMAND)
        xaml_create_dcomp_visual( &impl->visual );

    register_uwp_elem( impl );

    if (inner)
    {
        void *ui = NULL;
        HRESULT qi_hr = E_FAIL;
        *inner = &impl->nondelegating_iface;
        /* Delegation mode is a property of the outer object, not the app: an outer
         * that answers IUIElement is a real app-side element, so the element's
         * lifetime rides on it; otherwise the element owns its own refcount. */
        if (outer &&
            !IsBadReadPtr( outer, sizeof(void *) ) &&
            !IsBadReadPtr( *(void **)outer, 2 * sizeof(void *) ))
        {
            __TRY
            {
                qi_hr = IInspectable_QueryInterface( outer, &IID_IUIElement_UWP, &ui );
            }
            __EXCEPT_ALL
            {
                qi_hr = E_FAIL;
            }
            __ENDTRY;
        }
        if (SUCCEEDED( qi_hr ) && ui)
        {
            IInspectable_Release( (IInspectable *)ui );
            impl->delegating = TRUE;
            IInspectable_AddRef( outer );
        }
        else
            InterlockedIncrement( &impl->ref );
    }
    if (impl->kind == UWP_KIND_FRAME)
        *instance = &impl->IFrame_iface;
    else if (impl->kind == UWP_KIND_GRID || impl->kind == UWP_KIND_STACK_PANEL)
        *instance = &impl->IGrid_iface;
    else if (impl->kind == UWP_KIND_BORDER)
        *instance = &impl->IBorder_iface;
    else if (impl->kind == UWP_KIND_BUTTON)
        *instance = &impl->IButtonBase_iface;
    else if (impl->kind == UWP_KIND_TOGGLE_BUTTON)
        *instance = &impl->IToggleButton_iface;
    else if (impl->kind == UWP_KIND_TEXT_BLOCK)
        *instance = &impl->ITextBlock_iface;
    else if (impl->kind == UWP_KIND_ROW_DEF)
        *instance = &impl->IRowDefinition_iface;
    else if (impl->kind == UWP_KIND_COL_DEF)
        *instance = &impl->IColumnDefinition_iface;
    else if (impl->kind == UWP_KIND_CONTENT_CONTROL || impl->kind == UWP_KIND_USER_CONTROL || impl->kind == UWP_KIND_PAGE)
        *instance = &impl->IContentControl_iface;
    else if (impl->kind == UWP_KIND_ACCELERATOR || wcsstr( impl->class_name, L"Accelerator" ))
        *instance = &impl->IKeyboardAccelerator_iface;
    else if (impl->kind == UWP_KIND_XAML_COMMAND || wcsstr( impl->class_name, L"XamlUICommand" ))
        *instance = &impl->IXamlUICommand_iface;
    else if (wcsstr( impl->class_name, L"FlyoutItem" ))
        *instance = &impl->IMenuFlyoutItem_iface;
    else if (wcsstr( impl->class_name, L"ContentDialog" ))
        *instance = &impl->IContentDialog_iface;
    else if (!wcscmp( impl->class_name, L"Windows.UI.Xaml.DispatcherTimer" ))
        *instance = &impl->IInspectable_iface;
    else if (!wcscmp( impl->class_name, L"Windows.UI.Xaml.Data.Binding" ) ||
             !wcscmp( impl->class_name, L"Windows.UI.Xaml.Data.RelativeSource" ) ||
             !wcscmp( impl->class_name, L"Windows.UI.Xaml.Controls.SymbolIcon" ))
        *instance = &impl->IStyle_iface;
    else
        *instance = &impl->IFrameworkElement_iface;

    return S_OK;
}

/* ============================================================================
 * 5. DataContext Propagation, Loaded & OnApplyTemplate Dispatch, Frame Navigate
 * ============================================================================ */


static HRESULT WINAPI uwp_ccc_QueryInterface( void *iface, REFIID iid, void **out )
{
    static const GUID IID_IContainerContentChangingEventArgs_g =
        { 0x13922481, 0x41f3, 0x4e20, { 0xb6, 0x3a, 0x3f, 0x53, 0x63, 0x2e, 0x10, 0x85 } };
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IContainerContentChangingEventArgs_g ))
    {
        *out = iface;
        InterlockedIncrement( &((struct uwp_ccc_args *)iface)->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI uwp_ccc_AddRef( void *iface )
{
    return InterlockedIncrement( &((struct uwp_ccc_args *)iface)->ref );
}

ULONG WINAPI uwp_ccc_Release( void *iface )
{
    struct uwp_ccc_args *a = (struct uwp_ccc_args *)iface;
    LONG r = InterlockedDecrement( &a->ref );
    if (r == 0)
    {
        if (a->item) IInspectable_Release( a->item );
        if (a->item_container) IInspectable_Release( a->item_container );
        free( a );
    }
    return r;
}

static HRESULT WINAPI uwp_ccc_GetIids( void *iface, ULONG *cnt, IID **iids )
{
    if (cnt) *cnt = 0;
    if (iids) *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI uwp_ccc_GetRuntimeClassName( void *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.UI.Xaml.Controls.ContainerContentChangingEventArgs", 58, name );
}

static HRESULT WINAPI uwp_ccc_GetTrustLevel( void *iface, TrustLevel *tl )
{
    if (tl) *tl = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI uwp_ccc_get_ItemContainer( void *iface, void **out )
{
    struct uwp_ccc_args *a = (struct uwp_ccc_args *)iface;
    if (!out) return E_POINTER;
    *out = a->item_container;
    if (*out) IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}
static HRESULT WINAPI uwp_ccc_get_InRecycleQueue( void *iface, boolean *out ) { if (out) *out = ((struct uwp_ccc_args *)iface)->in_recycle; return S_OK; }
static HRESULT WINAPI uwp_ccc_get_ItemIndex( void *iface, INT32 *out ) { if (out) *out = 0; return S_OK; }
static HRESULT WINAPI uwp_ccc_get_Item( void *iface, IInspectable **out )
{
    struct uwp_ccc_args *a = (struct uwp_ccc_args *)iface;
    if (!out) return E_POINTER;
    *out = a->item;
    if (*out) IInspectable_AddRef( *out );
    return S_OK;
}
static HRESULT WINAPI uwp_ccc_get_Phase( void *iface, UINT32 *out ) { if (out) *out = ((struct uwp_ccc_args *)iface)->phase; return S_OK; }
static HRESULT WINAPI uwp_ccc_put_Handled( void *iface, boolean v ) { ((struct uwp_ccc_args *)iface)->handled = v; return S_OK; }
static HRESULT WINAPI uwp_ccc_get_Handled( void *iface, boolean *out ) { if (out) *out = ((struct uwp_ccc_args *)iface)->handled; return S_OK; }
static HRESULT WINAPI uwp_ccc_RegisterUpdateCallback( void *iface, void *cb ) { return S_OK; }
static HRESULT WINAPI uwp_ccc_RegisterUpdateCallbackWithPhase( void *iface, UINT32 p, void *cb ) { return S_OK; }

const void *uwp_ccc_vtbl[] =
{
    uwp_ccc_QueryInterface,
    uwp_ccc_AddRef,
    uwp_ccc_Release,
    uwp_ccc_GetIids,
    uwp_ccc_GetRuntimeClassName,
    uwp_ccc_GetTrustLevel,
    uwp_ccc_get_ItemContainer,
    uwp_ccc_get_InRecycleQueue,
    uwp_ccc_get_ItemIndex,
    uwp_ccc_get_Item,
    uwp_ccc_get_Phase,
    uwp_ccc_put_Handled,
    uwp_ccc_get_Handled,
    uwp_ccc_RegisterUpdateCallback,
    uwp_ccc_RegisterUpdateCallbackWithPhase,
};


static struct uwp_dp_changed_args g_dp_routed_args = { (const void *)dp_changed_args_vtbl, 0x100000, NULL, NULL, NULL };
/* Loaded/Click/Navigation callbacks receive this: McgMarshal ComInterfaceToObject
 * calls GetRuntimeClassName on it, so it must be an element face naming
 * RoutedEventArgs. */
struct uwp_element *g_routed_args_el = NULL;
/* McgMarshal caches the RCW (__ComObject) per IUnknown pointer and pins its
 * interop type at the first crossing.  Handlers registered on one element
 * often use distinct delegate args types (RoutedEventArgs vs RoutedEventArgs2),
 * so each fire allocates its own args object to keep cached RCW types matching. */
void *g_routed_args(void)
{
    void *inst = NULL;
    struct uwp_element *el;
    uwp_element_create( L"Windows.UI.Xaml.RoutedEventArgs", NULL, NULL, &inst );
    el = uwp_elem_from_any( inst );
    if (el && !g_routed_args_el) g_routed_args_el = el;
    if (el && el->outer) return el->outer;
    if (el) return &el->IInspectable_iface;
    return &g_dp_routed_args;
}
#define g_dummy_routed_args g_routed_args()


static const GUID IID_ISizeChangedEventArgs_UWP =
    { 0xd5312e60, 0x5cc1, 0x42a1, { 0x92, 0x0c, 0x1a, 0xf4, 0x6b, 0xe2, 0xf9, 0x86 } };
static const GUID IID_IRoutedEventArgs_sc =
    { 0x5c985ac6, 0xd802, 0x4b38, { 0xa2, 0x23, 0xbf, 0x07, 0x0c, 0x43, 0xfe, 0xdf } };

static HRESULT WINAPI uwp_sc_args_QI( void *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IRoutedEventArgs_sc ) ||
        IsEqualGUID( iid, &IID_ISizeChangedEventArgs_UWP ))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI uwp_sc_args_AddRef( void *iface ) { return 2; }
static ULONG WINAPI uwp_sc_args_Release( void *iface ) { return 1; }
static HRESULT WINAPI uwp_sc_args_GetIids( void *iface, ULONG *c, IID **i ) { if (c) *c = 0; if (i) *i = NULL; return S_OK; }
static HRESULT WINAPI uwp_sc_args_GetRuntimeClassName( void *iface, HSTRING *n )
{
    static const WCHAR cls[] = L"Windows.UI.Xaml.SizeChangedEventArgs";
    return WindowsCreateString( cls, ARRAY_SIZE(cls) - 1, n );
}
static HRESULT WINAPI uwp_sc_args_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI uwp_sc_args_get_PreviousSize( void *iface, Size *sz )
{
    if (sz) { sz->Width = 0.0f; sz->Height = 0.0f; }
    return S_OK;
}
float g_sc_new_w = 1272.0f, g_sc_new_h = 990.0f;
static HRESULT WINAPI uwp_sc_args_get_NewSize( void *iface, Size *sz )
{
    if (sz) { sz->Width = g_sc_new_w > 0.0f ? g_sc_new_w : 1272.0f; sz->Height = g_sc_new_h > 0.0f ? g_sc_new_h : 990.0f; }
    return S_OK;
}
static const void *const uwp_sc_args_vtbl[] =
{
    uwp_sc_args_QI, uwp_sc_args_AddRef, uwp_sc_args_Release,
    uwp_sc_args_GetIids, uwp_sc_args_GetRuntimeClassName, uwp_sc_args_GetTrustLevel,
    uwp_sc_args_get_PreviousSize,
    uwp_sc_args_get_NewSize,
};
struct uwp_sc_args_obj uwp_sc_args_obj = { uwp_sc_args_vtbl };




HRESULT uwp_frame_navigate_impl( void *iface, void *type_name_ptr, void *param, void *transition_info, boolean *result )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    TypeName *type = (TypeName *)type_name_ptr;
    const WCHAR *name = (type && type->Name) ? WindowsGetStringRawBuffer( type->Name, NULL ) : NULL;

    WARN( "uwp_frame_navigate_impl iface=%p (%s) type=%s param=%p trans=%p\n",
          iface, impl ? debugstr_w(impl->name) : "\"?\"", debugstr_w(name), param, transition_info );
    if (result) *result = TRUE;
    if (!impl || !name) return S_OK;

    {
        IInspectable *page = NULL;
        HRESULT hr = xaml_create_page( type_name_ptr, &page );
        TRACE( "xaml_create_page(%s) -> hr %#lx, page %p\n", debugstr_w(name), hr, page );
        if (SUCCEEDED(hr) && page)
        {
            struct uwp_element *page_el = uwp_elem_from_any( page );
            TRACE( "NAVIGATE probe: page=%p g_last_created_page=%p kind=%d\n",
                   page, g_last_created_page, g_last_created_page ? g_last_created_page->kind : -1 );
            if (!page_el && g_last_created_page)
            {
                page_el = g_last_created_page;
                TRACE( "uwp_frame_navigate_impl: RCW page unresolvable, using last PAGE element %p\n", page_el );
            }
            TRACE( "uwp_frame_navigate_impl page_el=%p outer=%p\n", page_el, page_el ? page_el->outer : NULL );
            if (page_el)
            {
                const WCHAR *comma = wcschr( name, ',' );
                size_t len = comma ? (size_t)(comma - name) : wcslen( name );
                IInspectable *new_content;
                if (len && len < ARRAY_SIZE( page_el->class_name ))
                {
                    lstrcpynW( page_el->class_name, name, (int)len + 1 );
                    page_el->class_name[len] = 0;
                }
                new_content = page_el->outer ? page_el->outer : &page_el->IInspectable_iface;
                IInspectable_AddRef( new_content );
                if (impl->content) IInspectable_Release( impl->content );
                impl->content = new_content;
                if (!impl->parent || !g_navigated_page_el)
                    g_navigated_page_el = page_el;
                page_el->parent = impl;

                {
                    struct uwp_nav_event_args *nea = uwp_nav_event_args_create(
                        new_content, (IInspectable *)param, type, (IInspectable *)transition_info );

                    /* Invoke IPageOverrides::OnNavigatedTo (slot 7) if implemented */
                    if (page_el->outer)
                    {
                        void *page_ovr = NULL;
                        HRESULT qi_hr = IInspectable_QueryInterface( page_el->outer, &IID_IPageOverrides_UWP, &page_ovr );
                        TRACE( "uwp_frame_navigate_impl QI IPageOverrides -> hr %#lx ptr %p\n", qi_hr, page_ovr );
                        if (SUCCEEDED(qi_hr) && page_ovr)
                        {
                            HRESULT (WINAPI *on_nav)(void *, void *) = (void *)(*(void ***)page_ovr)[7];
                            HRESULT nav_hr = S_OK;
                            __TRY
                            {
                                nav_hr = on_nav ? on_nav( page_ovr, nea ? (void *)&nea->INavigationEventArgs_iface : g_routed_args() ) : S_OK;
                            }
                            __EXCEPT_ALL { nav_hr = E_FAIL; }
                            __ENDTRY;
                            TRACE( "uwp_frame_navigate_impl OnNavigatedTo -> hr %#lx\n", nav_hr );
                            IUnknown_Release( (IUnknown *)page_ovr );
                        }
                    }

                    /* Fire Frame::Navigated handlers */
                    if (impl->frame_navigated_reg.next_token > 0)
                    {
                        void **handlers = NULL;
                        unsigned int cnt = xaml_event_registry_snapshot_alloc( &impl->frame_navigated_reg, &handlers );
                        IInspectable *sender = impl->outer ? impl->outer : &impl->IInspectable_iface;
                        unsigned int hi;
                        for (hi = 0; hi < cnt; hi++)
                        {
                            struct xaml_event_handler *h = (struct xaml_event_handler *)handlers[hi];
                            TRACE( "uwp_frame_navigate_impl firing Navigated[%u] on %s (%s)\n",
                                   hi, debugstr_w(impl->class_name), debugstr_w(impl->name) );
                            __TRY
                            {
                                h->lpVtbl->Invoke( h, sender, nea ? &nea->INavigationEventArgs_iface : g_routed_args() );
                            }
                            __EXCEPT_ALL { WARN( "Frame::Navigated[%u] threw\n", hi ); }
                            __ENDTRY;
                            IUnknown_Release( (IUnknown *)handlers[hi] );
                        }
                        free( handlers );
                    }
                    if (nea) uwp_nea_Release( &nea->INavigationEventArgs_iface );
                }
                uwp_fire_loaded_tree( page_el );

                /* Populate any empty placeholder containers from the page XBF's referenced PRI resources */
                uwp_populate_page_from_xbf_refs( page_el );
            }
            else
            {
                if (impl->content) IInspectable_Release( impl->content );
                impl->content = page;
            }
            xaml_window_present_current();
        }
    }
    return S_OK;
}

HRESULT WINAPI uwp_frame_NavigateWithParam( void *iface, void *type_name_ptr, void *param, boolean *result )
{
    return uwp_frame_navigate_impl( iface, type_name_ptr, param, NULL, result );
}

/* ============================================================================
 * 6. XBF v2 Stack Parser & Visual Tree Builder
 * ============================================================================ */


static const GUID IID_ITappedRoutedEventArgs_UWP =
    { 0xafa4bd0f, 0xa05b, 0x4665, { 0x8d, 0x2c, 0xa6, 0xf6, 0x73, 0x67, 0x23, 0x8a } };
static const GUID IID_IRoutedEventArgs_tap_UWP =
    { 0x5c985ac6, 0xd802, 0x4b38, { 0xa2, 0x23, 0xbf, 0x07, 0x0c, 0x43, 0xfe, 0xdf } };


static inline struct uwp_tapped_args *impl_from_tap( void *iface )
{
    return CONTAINING_RECORD( iface, struct uwp_tapped_args, ITappedRoutedEventArgs_iface );
}
static inline struct uwp_tapped_args *impl_from_tap_routed( void *iface )
{
    return CONTAINING_RECORD( iface, struct uwp_tapped_args, IRoutedEventArgs_iface );
}
static HRESULT WINAPI uwp_tap_QI( void *iface, REFIID iid, void **out )
{
    struct uwp_tapped_args *a = impl_from_tap( iface );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_ITappedRoutedEventArgs_UWP ))
    {
        InterlockedIncrement( &a->ref );
        *out = &a->ITappedRoutedEventArgs_iface;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IRoutedEventArgs_tap_UWP ))
    {
        InterlockedIncrement( &a->ref );
        *out = &a->IRoutedEventArgs_iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI uwp_tap_AddRef( void *iface ) { return InterlockedIncrement( &impl_from_tap( iface )->ref ); }
ULONG WINAPI uwp_tap_Release( void *iface )
{
    struct uwp_tapped_args *a = impl_from_tap( iface );
    ULONG r = InterlockedDecrement( &a->ref );
    if (!r)
    {
        if (a->orig_source) IInspectable_Release( a->orig_source );
        free( a );
    }
    return r;
}
static HRESULT WINAPI uwp_tap_GetIids( void *iface, ULONG *cnt, IID **iids ) { *cnt = 0; *iids = NULL; return S_OK; }
static HRESULT WINAPI uwp_tap_GetRuntimeClassName( void *iface, HSTRING *n )
{
    static const WCHAR cls[] = L"Windows.UI.Xaml.Input.TappedRoutedEventArgs";
    return WindowsCreateString( cls, ARRAY_SIZE(cls) - 1, n );
}
static HRESULT WINAPI uwp_tap_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI uwp_tap_get_PointerDeviceType( void *iface, INT32 *v ) { if (v) *v = 2; return S_OK; } /* Mouse */
static HRESULT WINAPI uwp_tap_get_Handled( void *iface, boolean *v ) { if (v) *v = impl_from_tap( iface )->handled; return S_OK; }
static HRESULT WINAPI uwp_tap_put_Handled( void *iface, boolean v ) { impl_from_tap( iface )->handled = v; return S_OK; }
static HRESULT WINAPI uwp_tap_GetPosition( void *iface, void *rel, Point *pt )
{
    struct uwp_tapped_args *a = impl_from_tap( iface );
    if (pt) { pt->X = a->x; pt->Y = a->y; }
    return S_OK;
}
static HRESULT WINAPI uwp_tap_routed_QI( void *iface, REFIID iid, void **out ) { return uwp_tap_QI( &impl_from_tap_routed(iface)->ITappedRoutedEventArgs_iface, iid, out ); }
static ULONG WINAPI uwp_tap_routed_AddRef( void *iface ) { return uwp_tap_AddRef( &impl_from_tap_routed(iface)->ITappedRoutedEventArgs_iface ); }
static ULONG WINAPI uwp_tap_routed_Release( void *iface ) { return uwp_tap_Release( &impl_from_tap_routed(iface)->ITappedRoutedEventArgs_iface ); }
static HRESULT WINAPI uwp_tap_routed_GetIids( void *iface, ULONG *c, IID **i ) { return uwp_tap_GetIids( &impl_from_tap_routed(iface)->ITappedRoutedEventArgs_iface, c, i ); }
static HRESULT WINAPI uwp_tap_routed_GetRuntimeClassName( void *iface, HSTRING *n ) { return uwp_tap_GetRuntimeClassName( &impl_from_tap_routed(iface)->ITappedRoutedEventArgs_iface, n ); }
static HRESULT WINAPI uwp_tap_routed_GetTrustLevel( void *iface, TrustLevel *l ) { return uwp_tap_GetTrustLevel( &impl_from_tap_routed(iface)->ITappedRoutedEventArgs_iface, l ); }
static HRESULT WINAPI uwp_tap_routed_get_OriginalSource( void *iface, IInspectable **out )
{
    struct uwp_tapped_args *a = impl_from_tap_routed( iface );
    if (!out) return E_POINTER;
    *out = a->orig_source;
    if (*out) IInspectable_AddRef( *out );
    return S_OK;
}
const void *uwp_tap_vtbl[] =
{
    uwp_tap_QI, uwp_tap_AddRef, uwp_tap_Release,
    uwp_tap_GetIids, uwp_tap_GetRuntimeClassName, uwp_tap_GetTrustLevel,
    uwp_tap_get_PointerDeviceType, uwp_tap_get_Handled, uwp_tap_put_Handled, uwp_tap_GetPosition,
};
const void *uwp_tap_routed_vtbl[] =
{
    uwp_tap_routed_QI, uwp_tap_routed_AddRef, uwp_tap_routed_Release,
    uwp_tap_routed_GetIids, uwp_tap_routed_GetRuntimeClassName, uwp_tap_routed_GetTrustLevel,
    uwp_tap_routed_get_OriginalSource,
};


/* ============================================================================
 * 7b. Generic Pointer Hit-Testing, Capture, Bubbling & PointerRoutedEventArgs
 * ============================================================================ */

static const GUID IID_IRoutedEventArgs_UWP =
    { 0x5c985ac6, 0xd802, 0x4b38, { 0xa2, 0x23, 0xbf, 0x07, 0x0c, 0x43, 0xfe, 0xdf } };
static const GUID IID_IPointerRoutedEventArgs_UWP =
    { 0xda628f0a, 0x9752, 0x49e2, { 0xbd, 0xe2, 0x49, 0xec, 0xca, 0xb9, 0x19, 0x4d } };
static const GUID IID_IPointer_UWP =
    { 0x5ee8f39f, 0x747d, 0x4171, { 0x90, 0xe6, 0xcd, 0x37, 0xa9, 0xdf, 0xfb, 0x11 } };

struct uwp_pointer
{
    const void **lpVtbl;
    LONG ref;
    UINT32 pointer_id;
    BOOL is_in_contact;
    BOOL is_in_range;
};

static HRESULT WINAPI uwp_ptr_QI( void *iface, REFIID iid, void **out )
{
    struct uwp_pointer *p = iface;
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IPointer_UWP ))
    {
        InterlockedIncrement( &p->ref );
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI uwp_ptr_AddRef( void *iface )
{
    return InterlockedIncrement( &((struct uwp_pointer *)iface)->ref );
}
static ULONG WINAPI uwp_ptr_Release( void *iface )
{
    struct uwp_pointer *p = iface;
    ULONG ref = InterlockedDecrement( &p->ref );
    if (!ref) free( p );
    return ref;
}
static HRESULT WINAPI uwp_ptr_GetRuntimeClassName( void *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.UI.Xaml.Input.Pointer", 29, name );
}
static HRESULT WINAPI uwp_ptr_get_PointerId( void *iface, UINT32 *val )
{
    if (!val) return E_POINTER;
    *val = ((struct uwp_pointer *)iface)->pointer_id;
    return S_OK;
}
static HRESULT WINAPI uwp_ptr_get_PointerDeviceType( void *iface, PointerDeviceType *val )
{
    if (!val) return E_POINTER;
    *val = PointerDeviceType_Mouse;
    return S_OK;
}
static HRESULT WINAPI uwp_ptr_get_IsInContact( void *iface, boolean *val )
{
    if (!val) return E_POINTER;
    *val = ((struct uwp_pointer *)iface)->is_in_contact ? TRUE : FALSE;
    return S_OK;
}
static HRESULT WINAPI uwp_ptr_get_IsInRange( void *iface, boolean *val )
{
    if (!val) return E_POINTER;
    *val = ((struct uwp_pointer *)iface)->is_in_range ? TRUE : FALSE;
    return S_OK;
}
static const void *uwp_ptr_vtbl[] =
{
    uwp_ptr_QI,
    uwp_ptr_AddRef,
    uwp_ptr_Release,
    uwp_sub_GetIids,
    uwp_ptr_GetRuntimeClassName,
    uwp_sub_GetTrustLevel,
    uwp_ptr_get_PointerId,
    uwp_ptr_get_PointerDeviceType,
    uwp_ptr_get_IsInContact,
    uwp_ptr_get_IsInRange,
};


static inline struct uwp_pointer_routed_args *pra_from_routed( void *iface )
{
    return CONTAINING_RECORD( iface, struct uwp_pointer_routed_args, IRoutedEventArgs_vtbl );
}

static HRESULT WINAPI pra_QI( void *iface, REFIID iid, void **out )
{
    struct uwp_pointer_routed_args *a = iface;
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IPointerRoutedEventArgs_UWP ))
    {
        InterlockedIncrement( &a->ref );
        *out = &a->IPointerRoutedEventArgs_vtbl;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IRoutedEventArgs_UWP ))
    {
        InterlockedIncrement( &a->ref );
        *out = &a->IRoutedEventArgs_vtbl;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI pra_AddRef( void *iface )
{
    return InterlockedIncrement( &((struct uwp_pointer_routed_args *)iface)->ref );
}
ULONG WINAPI pra_Release( void *iface )
{
    struct uwp_pointer_routed_args *a = iface;
    ULONG ref = InterlockedDecrement( &a->ref );
    if (!ref)
    {
        if (a->orig_source) IInspectable_Release( a->orig_source );
        if (a->pointer) uwp_ptr_Release( a->pointer );
        free( a );
    }
    return ref;
}
static HRESULT WINAPI pra_GetRuntimeClassName( void *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.UI.Xaml.Input.PointerRoutedEventArgs", 44, name );
}
static HRESULT WINAPI pra_get_Pointer( void *iface, void **value )
{
    struct uwp_pointer_routed_args *a = iface;
    if (!value) return E_POINTER;
    if (!a->pointer)
    {
        a->pointer = calloc( 1, sizeof(*a->pointer) );
        if (a->pointer)
        {
            a->pointer->lpVtbl = uwp_ptr_vtbl;
            a->pointer->ref = 1;
            a->pointer->pointer_id = a->info.pointer_id ? a->info.pointer_id : 1;
            a->pointer->is_in_contact = a->info.is_in_contact;
            a->pointer->is_in_range = a->info.is_in_window;
        }
    }
    *value = a->pointer;
    if (*value) uwp_ptr_AddRef( *value );
    return S_OK;
}
static HRESULT WINAPI pra_get_KeyModifiers( void *iface, UINT32 *value )
{
    struct uwp_pointer_routed_args *a = iface;
    if (!value) return E_POINTER;
    *value = a->info.key_modifiers;
    return S_OK;
}
static HRESULT WINAPI pra_get_Handled( void *iface, boolean *value )
{
    struct uwp_pointer_routed_args *a = iface;
    if (!value) return E_POINTER;
    *value = a->handled;
    return S_OK;
}
static HRESULT WINAPI pra_put_Handled( void *iface, boolean value )
{
    struct uwp_pointer_routed_args *a = iface;
    a->handled = value;
    return S_OK;
}
static HRESULT WINAPI pra_GetCurrentPoint( void *iface, void *relative_to, IPointerPoint **value )
{
    struct uwp_pointer_routed_args *a = iface;
    float rx = a->info.x, ry = a->info.y;
    struct uwp_element *rel;

    if (!value) return E_POINTER;
    if (relative_to && (rel = uwp_elem_from_any( relative_to )) != NULL && rel->has_bounds)
    {
        rx -= (float)rel->bounds.left;
        ry -= (float)rel->bounds.top;
    }
    *value = xaml_pointer_point_create( &a->info, rx, ry );
    return *value ? S_OK : E_OUTOFMEMORY;
}
static HRESULT WINAPI pra_GetIntermediatePoints( void *iface, void *relative_to, void **value )
{
    if (!value) return E_POINTER;
    *value = NULL;
    return S_OK;
}

static const void *pra_vtbl[] =
{
    pra_QI,
    pra_AddRef,
    pra_Release,
    uwp_sub_GetIids,
    pra_GetRuntimeClassName,
    uwp_sub_GetTrustLevel,
    pra_get_Pointer,
    pra_get_KeyModifiers,
    pra_get_Handled,
    pra_put_Handled,
    pra_GetCurrentPoint,
    pra_GetIntermediatePoints,
};

static HRESULT WINAPI pra_routed_QI( void *iface, REFIID iid, void **out )
{
    return pra_QI( pra_from_routed( iface ), iid, out );
}
static ULONG WINAPI pra_routed_AddRef( void *iface )
{
    return pra_AddRef( pra_from_routed( iface ) );
}
static ULONG WINAPI pra_routed_Release( void *iface )
{
    return pra_Release( pra_from_routed( iface ) );
}
static HRESULT WINAPI pra_routed_get_OriginalSource( void *iface, IInspectable **value )
{
    struct uwp_pointer_routed_args *a = pra_from_routed( iface );
    if (!value) return E_POINTER;
    *value = a->orig_source;
    if (*value) IInspectable_AddRef( *value );
    return S_OK;
}
static const void *pra_routed_vtbl[] =
{
    pra_routed_QI,
    pra_routed_AddRef,
    pra_routed_Release,
    uwp_sub_GetIids,
    pra_GetRuntimeClassName,
    uwp_sub_GetTrustLevel,
    pra_routed_get_OriginalSource,
};

struct uwp_pointer_routed_args *uwp_pointer_routed_args_create( const struct xaml_pointer_info *info,
                                                                      IInspectable *orig_source )
{
    struct uwp_pointer_routed_args *a = calloc( 1, sizeof(*a) );
    if (!a) return NULL;
    a->IPointerRoutedEventArgs_vtbl = pra_vtbl;
    a->IRoutedEventArgs_vtbl = pra_routed_vtbl;
    a->ref = 1;
    if (info) a->info = *info;
    a->orig_source = orig_source;
    if (orig_source) IInspectable_AddRef( orig_source );
    return a;
}


struct xaml_event_registry *uwp_elem_ptr_registry( struct uwp_element *el, enum xaml_pointer_event_kind kind )
{
    if (!el) return NULL;
    switch (kind)
    {
    case XAML_PTR_PRESSED:      return &el->ptr_pressed_reg;
    case XAML_PTR_MOVED:        return &el->ptr_moved_reg;
    case XAML_PTR_RELEASED:     return &el->ptr_released_reg;
    case XAML_PTR_WHEEL:        return &el->ptr_wheel_reg;
    case XAML_PTR_ENTERED:      return &el->ptr_entered_reg;
    case XAML_PTR_EXITED:       return &el->ptr_exited_reg;
    case XAML_PTR_CAPTURE_LOST: return &el->ptr_capture_lost_reg;
    }
    return NULL;
}



BOOL uwp_route_pointer_event( HWND hwnd, enum xaml_pointer_event_kind kind, const struct xaml_pointer_info *info,
                              int width, int height, IInspectable *root_content )
{
    static struct uwp_element *last_root = NULL;
    static int last_w = 0, last_h = 0;
    struct uwp_element *root_el = uwp_elem_from_any( root_content );
    struct uwp_element *target = NULL;
    BOOL fired = FALSE;

    if (!info) return FALSE;
    if (!root_el && g_navigated_page_el) root_el = g_navigated_page_el;
    if (!root_el) return FALSE;
    if (root_el->kind == UWP_KIND_FRAME && g_navigated_page_el)
        root_el = g_navigated_page_el;

    if (width > 0 && height > 0 &&
        (!root_el->has_bounds || g_layout_dirty || root_el != last_root || width != last_w || height != last_h))
    {
        RECT rc = { 0, 0, width, height };
        uwp_arrange_element( root_el, rc );
        g_layout_dirty = FALSE;
        last_root = root_el;
        last_w = width;
        last_h = height;
    }

    if (kind == XAML_PTR_EXITED)
    {
        if (g_hovered_elem)
        {
            g_hovered_elem->is_hovered = FALSE;
            uwp_fire_element_pointer( g_hovered_elem, XAML_PTR_EXITED, info, FALSE );
            g_hovered_elem = NULL;
        }
        return TRUE;
    }

    if (kind == XAML_PTR_CAPTURE_LOST)
    {
        if (g_captured_elem)
        {
            struct uwp_element *cap = g_captured_elem;
            g_captured_elem = NULL;
            uwp_fire_element_pointer( cap, XAML_PTR_CAPTURE_LOST, info, FALSE );
        }
        return TRUE;
    }

    target = g_captured_elem ? g_captured_elem : uwp_hit_test_element( root_el, (int)info->x, (int)info->y );

    if (!g_captured_elem && target != g_hovered_elem)
    {
        if (g_hovered_elem)
        {
            g_hovered_elem->is_hovered = FALSE;
            uwp_fire_element_pointer( g_hovered_elem, XAML_PTR_EXITED, info, FALSE );
        }
        g_hovered_elem = target;
        if (target)
        {
            target->is_hovered = TRUE;
            uwp_fire_element_pointer( target, XAML_PTR_ENTERED, info, FALSE );
        }
    }

    if (!target) return FALSE;

    if (kind == XAML_PTR_PRESSED)
    {
        g_captured_elem = target;
        TRACE( "pointer: pressed routed to element %p (%s / %s) at (%d,%d)\n",
               target, debugstr_w(target->class_name), debugstr_w(target->name), (int)info->x, (int)info->y );
        fired = uwp_fire_element_pointer( target, XAML_PTR_PRESSED, info, TRUE );
    }
    else if (kind == XAML_PTR_MOVED)
    {
        fired = uwp_fire_element_pointer( target, XAML_PTR_MOVED, info, TRUE );
    }
    else if (kind == XAML_PTR_RELEASED)
    {
        fired = uwp_fire_element_pointer( target, XAML_PTR_RELEASED, info, TRUE );
        if (uwp_fire_tapped_bubble( target, (int)info->x, (int)info->y ))
            fired = TRUE;
        g_captured_elem = NULL;
    }
    else if (kind == XAML_PTR_WHEEL)
    {
        fired = uwp_fire_element_pointer( target, XAML_PTR_WHEEL, info, TRUE );
    }

    return fired || (kind == XAML_PTR_PRESSED);
}

