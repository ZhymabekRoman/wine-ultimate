/* WinRT Windows.UI.Xaml DependencyObject2, UIElement, FrameworkElement & Control
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
#include <math.h>
#include <excpt.h>
#include "wine/exception.h"
#include "private.h"
#include "xbf_known_types.h"
#include "weakreference.h"
#define WIDL_using_Windows_ApplicationModel
#define WIDL_using_Windows_Management_Deployment
#include "windows.management.deployment.h"
#define WIDL_using_Windows_UI_Composition
#include "windows.ui.composition.h"
#include "uwp_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(xaml);

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
    if (impl)
        TRACE( "uwp_ui_put_Visibility %s (%s) %d -> %d\n",
               debugstr_w(impl->class_name), debugstr_w(impl->name), impl->visibility, vis );
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

#define UWP_UI_PTR_EVENT_IMPL( linkage, name, field ) \
linkage HRESULT WINAPI uwp_ui_add_##name( void *iface, void *handler, EventRegistrationToken *tok ) \
{ \
    struct uwp_element *impl = uwp_elem_from_any( iface ); \
    struct uwp_event_bag *ev; \
    if (!impl) { if (tok) tok->value = 0; return E_INVALIDARG; } \
    ev = uwp_elem_ensure_events( impl ); \
    if (!ev) { if (tok) tok->value = 0; return E_OUTOFMEMORY; } \
    if (uwp_is_card( impl )) \
        WARN( "add_" #name " on %s (%s) impl=%p handler=%p\n", debugstr_w( impl->class_name ), debugstr_w( uwp_elem_name_w( impl ) ), impl, handler ); \
    TRACE( "add_" #name " on %s (%s) handler=%p\n", debugstr_w( impl->class_name ), debugstr_w( uwp_elem_name_w( impl ) ), handler ); \
    return xaml_event_registry_add( &ev->field, handler, tok ); \
} \
linkage HRESULT WINAPI uwp_ui_remove_##name( void *iface, EventRegistrationToken tok ) \
{ \
    struct uwp_element *impl = uwp_elem_from_any( iface ); \
    if (!impl) return E_INVALIDARG; \
    if (!impl->events) return S_OK; \
    return xaml_event_registry_remove( &impl->events->field, tok ); \
}

#define UWP_UI_PTR_EVENT( name, field ) UWP_UI_PTR_EVENT_IMPL( , name, field )
#define UWP_UI_PTR_EVENT_STATIC( name, field ) UWP_UI_PTR_EVENT_IMPL( static, name, field )

UWP_UI_PTR_EVENT_STATIC( PointerPressed, ptr_pressed_reg )
UWP_UI_PTR_EVENT_STATIC( PointerMoved, ptr_moved_reg )
UWP_UI_PTR_EVENT_STATIC( PointerReleased, ptr_released_reg )
UWP_UI_PTR_EVENT_STATIC( PointerEntered, ptr_entered_reg )
UWP_UI_PTR_EVENT_STATIC( PointerExited, ptr_exited_reg )
UWP_UI_PTR_EVENT_STATIC( PointerCaptureLost, ptr_capture_lost_reg )
UWP_UI_PTR_EVENT_STATIC( Tapped, tapped_reg )
UWP_UI_PTR_EVENT_STATIC( PointerCanceled, ptr_canceled_reg )
UWP_UI_PTR_EVENT_STATIC( PointerWheelChanged, ptr_wheel_reg )
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

static HRESULT WINAPI uwp_fe_get_Style( void *iface, IInspectable **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    *out = (impl && impl->style_anim) ? impl->style_anim->style_obj : NULL;
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
                if (re->kind == UWP_RD_DOUBLE)
                {
                    WCHAR nbuf[64];
                    swprintf( nbuf, ARRAY_SIZE(nbuf), L"%g", re->dval );
                    uwp_set_custom_xaml_member( app_outer, st->owner_cls, st->prop_name, target, nbuf );
                }
                else
                {
                    IInspectable *res_val = uwp_rd_materialize_entry( found_b, re );
                    WARN( "apply_style_setter %s.%s <- res %s (val=%p)\n",
                          debugstr_w(st->owner_cls), debugstr_w(st->prop_name), debugstr_w(st->str), res_val );
                    if (res_val)
                        uwp_set_custom_xaml_member_obj( app_outer, st->owner_cls, st->prop_name, target, res_val );
                }
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
    struct uwp_style_anim_ext *sa;
    if (!impl) return S_OK;
    if (val) IInspectable_AddRef( val );
    sa = uwp_elem_ensure_style_anim( impl );
    if (sa)
    {
        if (sa->style_obj) IInspectable_Release( sa->style_obj );
        sa->style_obj = val;
    }
    else if (val) IInspectable_Release( val );
    if (val) impl->is_accent_style = FALSE;
    st_el = uwp_elem_from_any( val );
    if (impl->class_flags & UWP_CF_RESP_ITEMS_REPEATER_LIST)
        WARN( "uwp_fe_put_Style on %s (%s) val=%p st_el=%p name=%s based_on=%s cnt=%d\n",
              debugstr_w(impl->class_name), debugstr_w(impl->name), val, st_el,
              st_el ? debugstr_w(st_el->name) : "\"-\"",
              (st_el && st_el->style_anim) ? debugstr_w(st_el->style_anim->style_based_on) : "\"-\"",
              (st_el && st_el->style_anim) ? st_el->style_anim->style_setter_cnt : -1 );
    if (st_el)
    {
        if (st_el->name[0] && uwp_style_is_accent_name( st_el->name ))
            impl->is_accent_style = TRUE;
        if (st_el->style_anim && st_el->style_anim->style_based_on[0])
            uwp_apply_runtime_style_by_name( impl, st_el->style_anim->style_based_on, 0 );
        if (st_el->template_xbf_data && st_el->template_xbf_size)
        {
            impl->template_xbf_data = st_el->template_xbf_data;
            impl->template_xbf_size = st_el->template_xbf_size;
            impl->template_sub_idx = st_el->template_sub_idx;
        }
        if (st_el->style_anim && st_el->style_anim->style_setter_cnt > 0 && st_el->style_anim->style_setters)
            uwp_apply_runtime_style_setters( impl, st_el->style_anim->style_setters, st_el->style_anim->style_setter_cnt );
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
    {
        BOOL is_defer = FALSE;
        if (root->defer_entries)
        {
            for (i = 0; i < root->defer_cnt; i++)
            {
                if (!wcscmp( root->defer_entries[i].name, want ))
                {
                    is_defer = TRUE;
                    if (!root->defer_entries[i].realized)
                        uwp_realize_deferred_entry( &root->defer_entries[i] );
                }
            }
        }
        if (impl != root && impl->defer_entries)
        {
            for (i = 0; i < impl->defer_cnt; i++)
            {
                if (!wcscmp( impl->defer_entries[i].name, want ))
                {
                    is_defer = TRUE;
                    if (!impl->defer_entries[i].realized)
                        uwp_realize_deferred_entry( &impl->defer_entries[i] );
                }
            }
        }

        /* 2. Search named elements in impl, root, and global list */
        for (i = 0; i < impl->named_cnt; i++)
        {
            struct uwp_element *nel = impl->named_elems[i];
            if (nel && !wcscmp( nel->name, want ))
            {
                if (is_defer && nel->visibility == 1)
                {
                    nel->visibility = 0;
                    g_layout_dirty = TRUE;
                    uwp_invalidate_window();
                }
                *out = nel->outer ? nel->outer : &nel->IInspectable_iface;
                IInspectable_AddRef( (IInspectable *)*out );
                return S_OK;
            }
        }
        if (root != impl)
        {
            for (i = 0; i < root->named_cnt; i++)
            {
                struct uwp_element *nel = root->named_elems[i];
                if (nel && !wcscmp( nel->name, want ))
                {
                    if (is_defer && nel->visibility == 1)
                    {
                        nel->visibility = 0;
                        g_layout_dirty = TRUE;
                        uwp_invalidate_window();
                    }
                    *out = nel->outer ? nel->outer : &nel->IInspectable_iface;
                    IInspectable_AddRef( (IInspectable *)*out );
                    return S_OK;
                }
            }
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
    struct uwp_event_bag *ev;
    if (tok) tok->value = 0;
    if (!impl || !handler) return S_OK;
    ev = uwp_elem_ensure_events( impl );
    if (!ev) return E_OUTOFMEMORY;
    TRACE( "add_Loaded on %s (%s) handler=%p\n", debugstr_w( impl->class_name ), debugstr_w( impl->name ), handler );
    return xaml_event_registry_add( &ev->loaded_reg, handler, tok );
}

HRESULT WINAPI uwp_fe_remove_Loaded( void *iface, EventRegistrationToken tok )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl || !impl->events) return S_OK;
    return xaml_event_registry_remove( &impl->events->loaded_reg, tok );
}

static HRESULT WINAPI uwp_fe_add_SizeChanged( void *iface, void *handler, EventRegistrationToken *tok )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    struct uwp_event_bag *ev;
    if (tok) tok->value = 0;
    if (!impl || !handler) return S_OK;
    ev = uwp_elem_ensure_events( impl );
    if (!ev) return E_OUTOFMEMORY;
    TRACE( "add_SizeChanged on %s (%s) handler=%p\n", debugstr_w( impl->class_name ), debugstr_w( impl->name ), handler );
    return xaml_event_registry_add( &ev->size_changed_reg, handler, tok );
}

static HRESULT WINAPI uwp_fe_remove_SizeChanged( void *iface, EventRegistrationToken tok )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl || !impl->events) return S_OK;
    return xaml_event_registry_remove( &impl->events->size_changed_reg, tok );
}

static HRESULT WINAPI uwp_fe_add_LayoutUpdated( void *iface, void *handler, EventRegistrationToken *tok )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    struct uwp_event_bag *ev;
    if (tok) tok->value = 0;
    if (!impl || !handler) return S_OK;
    ev = uwp_elem_ensure_events( impl );
    if (!ev) return E_OUTOFMEMORY;
    TRACE( "add_LayoutUpdated on %s (%s) handler=%p\n", debugstr_w( impl->class_name ), debugstr_w( impl->name ), handler );
    return xaml_event_registry_add( &ev->layout_updated_reg, handler, tok );
}

static HRESULT WINAPI uwp_fe_remove_LayoutUpdated( void *iface, EventRegistrationToken tok )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    TRACE( "remove_LayoutUpdated on %s tok=%I64d\n", impl ? debugstr_w( impl->class_name ) : "?", tok.value );
    if (!impl || !impl->events) return S_OK;
    return xaml_event_registry_remove( &impl->events->layout_updated_reg, tok );
}

static HRESULT WINAPI uwp_fe_remove_Unloaded( void *iface, EventRegistrationToken tok )
{
    return S_OK;
}

HRESULT WINAPI uwp_fe3_add_DataContextChanged( void *iface, void *handler, EventRegistrationToken *tok )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    struct uwp_event_bag *ev;
    HRESULT hr;
    if (tok) tok->value = 0;
    if (!impl || !handler) return S_OK;
    ev = uwp_elem_ensure_events( impl );
    if (!ev) return E_OUTOFMEMORY;
    hr = xaml_event_registry_add( &ev->dc_reg, handler, tok );
    if (SUCCEEDED(hr))
    {
        /* If DataContext is already available, fire immediately */
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
    return hr;
}

HRESULT WINAPI uwp_fe3_remove_DataContextChanged( void *iface, EventRegistrationToken tok )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl || !impl->events) return S_OK;
    return xaml_event_registry_remove( &impl->events->dc_reg, tok );
}

HRESULT WINAPI uwp_fe3_add_Loading( void *iface, void *handler, EventRegistrationToken *tok )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    struct uwp_event_bag *ev;
    if (tok) tok->value = 0;
    if (!impl || !handler) return S_OK;
    ev = uwp_elem_ensure_events( impl );
    if (!ev) return E_OUTOFMEMORY;
    return xaml_event_registry_add( &ev->loading_reg, handler, tok );
}

HRESULT WINAPI uwp_fe3_remove_Loading( void *iface, EventRegistrationToken tok )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl || !impl->events) return S_OK;
    return xaml_event_registry_remove( &impl->events->loading_reg, tok );
}

/* --- IFrameworkElement2 RequestedTheme, IControl & IControlProtected --- */
HRESULT WINAPI uwp_fe2_get_RequestedTheme( void *iface, int *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (out) *out = impl ? impl->requested_theme : 0;
    return S_OK;
}
HRESULT WINAPI uwp_fe2_put_RequestedTheme( void *iface, int val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl && impl->requested_theme != val)
    {
        impl->requested_theme = val;
        uwp_invalidate_window();
    }
    return S_OK;
}
HRESULT WINAPI uwp_ctrl_get_FontFamily( void *iface, void **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    HRESULT hr;
    if (!out) return E_POINTER;
    *out = NULL;
    hr = uwp_element_create( L"Windows.UI.Xaml.Media.FontFamily", NULL, NULL, out );
    if (SUCCEEDED(hr) && *out)
    {
        struct uwp_element *ff = uwp_elem_from_any( *out );
        if (ff)
        {
            const WCHAR *src_ff = uwp_elem_font_family( impl );
            uwp_elem_set_font_family( ff, src_ff[0] ? src_ff : L"Segoe UI" );
        }
    }
    return hr;
}
HRESULT WINAPI uwp_ctrl_put_FontFamily( void *iface, IInspectable *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    WCHAR raw[128] = {0}, face[64] = {0};
    if (!impl) return S_OK;
    if (val)
    {
        struct uwp_element *ff = uwp_elem_from_any( val );
        const WCHAR *ff_str = uwp_elem_font_family( ff );
        if (ff && ff_str[0])
        {
            lstrcpynW( raw, ff_str, ARRAY_SIZE(raw) );
        }
        else
        {
            void *iff = NULL;
            if (SUCCEEDED(IInspectable_QueryInterface( val, &IID_IFontFamily_UWP, &iff )) && iff)
            {
                const struct abi_font_family *aff = (const struct abi_font_family *)iff;
                HSTRING hs = NULL;
                if (aff->lpVtbl->get_Source && SUCCEEDED(aff->lpVtbl->get_Source( iff, &hs )) && hs)
                {
                    const WCHAR *s = WindowsGetStringRawBuffer( hs, NULL );
                    if (s) lstrcpynW( raw, s, ARRAY_SIZE(raw) );
                    WindowsDeleteString( hs );
                }
                IUnknown_Release( (IUnknown *)iff );
            }
            else
            {
                uwp_unbox_string( val, raw, ARRAY_SIZE(raw) );
            }
        }
    }
    uwp_elem_set_font_family( impl, raw );
    if (raw[0]) uwp_resolve_font_family( raw, face, ARRAY_SIZE(face) );
    uwp_invalidate_window();
    return S_OK;
}
HRESULT WINAPI uwp_ctrl_get_FontSize( void *iface, double *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (val) *val = (impl && impl->font_size > 0) ? impl->font_size : 14.0;
    return S_OK;
}
HRESULT WINAPI uwp_ctrl_put_FontSize( void *iface, double val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl && val > 0) { impl->font_size = val; impl->has_font_size = TRUE; }
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
HRESULT WINAPI uwp_ctrl_prot_get_DefaultStyleKey( void *iface, IInspectable **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    *out = impl ? impl->default_style_key : NULL;
    if (*out) IInspectable_AddRef( *out );
    return S_OK;
}
HRESULT WINAPI uwp_ctrl_prot_put_DefaultStyleKey( void *iface, IInspectable *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return S_OK;
    if (impl->default_style_key != val)
    {
        if (impl->default_style_key) IInspectable_Release( impl->default_style_key );
        impl->default_style_key = val;
        if (val) IInspectable_AddRef( val );
    }
    {
        WCHAR dsk_buf[128] = {0};
        if (val)
        {
            if (!uwp_unbox_string( val, dsk_buf, ARRAY_SIZE(dsk_buf) ))
            {
                static const GUID IID_IReference_TypeName =
                    { 0x3830ad99, 0xd8da, 0x53f3, { 0x98, 0x9b, 0xfc, 0x92, 0xad, 0x22, 0x27, 0x78 } };
                void *iref = NULL;
                if (SUCCEEDED(IInspectable_QueryInterface( val, &IID_IReference_TypeName, &iref )) && iref)
                {
                    const struct abi_iref_typename *air = (const struct abi_iref_typename *)iref;
                    TypeName tn = { NULL, 0 };
                    if (air->lpVtbl->get_Value && SUCCEEDED(air->lpVtbl->get_Value( iref, &tn )) && tn.Name)
                    {
                        const WCHAR *s = WindowsGetStringRawBuffer( tn.Name, NULL );
                        if (s) lstrcpynW( dsk_buf, s, ARRAY_SIZE(dsk_buf) );
                        WindowsDeleteString( tn.Name );
                    }
                    IUnknown_Release( (IUnknown *)iref );
                }
                else
                    uwp_inspectable_to_string( val, dsk_buf, ARRAY_SIZE(dsk_buf) );
            }
            /* Strip assembly-qualified suffix (e.g. "Ns.Type, Assembly, Version=...") */
            {
                WCHAR *comma = wcschr( dsk_buf, L',' );
                if (comma) *comma = 0;
            }
        }
        uwp_elem_set_default_style_key_name( impl, dsk_buf );
    }
    uwp_elem_sync_class_flags( impl );
    TRACE( "IControlProtected::put_DefaultStyleKey on %s -> %s\n",
           debugstr_w(impl->class_name), debugstr_w(uwp_elem_default_style_key_name( impl )) );
    return S_OK;
}

static void uwp_collect_template_names( struct uwp_element *owner, struct uwp_element *node, int depth )
{
    if (!owner || !node || depth > 24) return;
    node->templated_parent = owner;
    if (node->name[0])
    {
        int i;
        BOOL exists = FALSE;
        for (i = 0; i < owner->named_cnt; i++)
            if (owner->named_elems[i] && !wcscmp( owner->named_elems[i]->name, node->name )) { exists = TRUE; break; }
        if (!exists && xaml_vec_grow( (void **)&owner->named_elems, &owner->named_cap,
                                      owner->named_cnt + 1, sizeof(*owner->named_elems), 16 ))
            owner->named_elems[owner->named_cnt++] = node;
    }
    if (node->content)
    {
        struct uwp_element *ch = uwp_elem_from_any( node->content );
        if (ch && ch != node) uwp_collect_template_names( owner, ch, depth + 1 );
    }
    if (node->children_coll)
    {
        struct uwp_vec_view *v = CONTAINING_RECORD( node->children_coll, struct uwp_vec_view, IInspectable_iface );
        unsigned int k;
        for (k = 0; k < v->count; k++)
        {
            struct uwp_element *ch = uwp_elem_from_any( v->items[k] );
            if (ch && ch != node) uwp_collect_template_names( owner, ch, depth + 1 );
        }
    }
}

static struct uwp_element *uwp_find_content_presenter( struct uwp_element *node, int depth )
{
    if (!node || depth > 16) return NULL;
    if (node->class_flags & UWP_CF_CONTENT_PRESENTER) return node;
    if (node->content)
    {
        struct uwp_element *ch = uwp_elem_from_any( node->content );
        struct uwp_element *hit = (ch && ch != node) ? uwp_find_content_presenter( ch, depth + 1 ) : NULL;
        if (hit) return hit;
    }
    if (node->children_coll)
    {
        struct uwp_vec_view *v = CONTAINING_RECORD( node->children_coll, struct uwp_vec_view, IInspectable_iface );
        unsigned int k;
        for (k = 0; k < v->count; k++)
        {
            struct uwp_element *ch = uwp_elem_from_any( v->items[k] );
            struct uwp_element *hit = (ch && ch != node) ? uwp_find_content_presenter( ch, depth + 1 ) : NULL;
            if (hit) return hit;
        }
    }
    return NULL;
}

BOOL uwp_inflate_control_template( struct uwp_element *el )
{
    static volatile LONG inflate_depth = 0;
    struct uwp_rd_entry *se = NULL;
    struct uwp_rd_bucket *sb = NULL;
    const WCHAR *key = NULL;

    if (!el || el->template_inflated) return el && el->template_root != NULL;
    if (uwp_is_exact_autosuggest( el ))
        return FALSE;
    if (InterlockedIncrement( &inflate_depth ) > 8)
    {
        InterlockedDecrement( &inflate_depth );
        return FALSE;
    }

    if (!el->template_xbf_data || !el->template_xbf_size)
    {
        uwp_ensure_app_resources_loaded();
        if (uwp_elem_default_style_key_name( el )[0])
            key = uwp_elem_default_style_key_name( el );
        else if (el->class_name[0] &&
                 !(el->class_flags & (UWP_CF_UWP_PREFIX | UWP_CF_WINUI_PREFIX)))
            key = el->class_name;

        if (key && key[0])
        {
            if (!uwp_rd_find_entry( NULL, el, key, 0, &se, &sb ) || !se || se->kind != UWP_RD_STYLE)
            {
                const WCHAR *dot = wcsrchr( key, L'.' );
                se = NULL; sb = NULL;
                if (dot && dot[1])
                    uwp_rd_find_entry( NULL, el, dot + 1, 0, &se, &sb );
            }
            if ((!se || se->kind != UWP_RD_STYLE) && el->class_name[0] && wcscmp( key, el->class_name ) != 0)
            {
                se = NULL; sb = NULL;
                if (!uwp_rd_find_entry( NULL, el, el->class_name, 0, &se, &sb ) || !se || se->kind != UWP_RD_STYLE)
                {
                    const WCHAR *dot = wcsrchr( el->class_name, L'.' );
                    se = NULL; sb = NULL;
                    if (dot && dot[1])
                        uwp_rd_find_entry( NULL, el, dot + 1, 0, &se, &sb );
                }
            }
            if (se && se->kind == UWP_RD_STYLE)
            {
                uwp_apply_style_entry( NULL, el, sb, se, 0 );
                if (se->template_xbf_data && se->template_xbf_size)
                {
                    el->template_xbf_data = se->template_xbf_data;
                    el->template_xbf_size = se->template_xbf_size;
                    el->template_sub_idx = se->template_sub_idx;
                }
            }
        }
    }

    if (el->template_xbf_data && el->template_xbf_size)
    {
        IInspectable *tpl_obj;
        struct uwp_rd_bucket *tpl_rd = el->template_rd_bucket ? el->template_rd_bucket : sb;
        if (!tpl_rd)
        {
            struct uwp_element *anc;
            for (anc = el; anc; anc = anc->parent)
            {
                if (anc->rd_bucket) { tpl_rd = anc->rd_bucket; break; }
            }
            if (!tpl_rd && el->owner_component && el->owner_component->rd_bucket)
                tpl_rd = el->owner_component->rd_bucket;
        }
        el->template_inflated = TRUE;
        tpl_obj = uwp_instantiate_xbf_substream_rd( el->template_xbf_data, el->template_xbf_size,
                                                    el->template_sub_idx, NULL, tpl_rd );
        if (tpl_obj)
        {
            struct uwp_element *tpl_root = uwp_elem_from_any( tpl_obj );
            if (tpl_root)
            {
                el->template_root = tpl_root;
                el->in_visual_tree = TRUE;
                tpl_root->parent = el;
                tpl_root->in_visual_tree = TRUE;
                uwp_collect_template_names( el, tpl_root, 0 );
                if (el->content)
                {
                    struct uwp_element *cp = uwp_find_content_presenter( tpl_root, 0 );
                    if (cp && !cp->content)
                    {
                        cp->content = el->content;
                        IInspectable_AddRef( el->content );
                        {
                            struct uwp_element *ce = uwp_elem_from_any( el->content );
                            if (ce) ce->parent = cp;
                        }
                    }
                }
                else if (!el->children_coll && !uwp_has_text( el ) &&
                         el->kind != UWP_KIND_BUTTON && el->kind != UWP_KIND_TOGGLE_BUTTON)
                {
                    el->content = tpl_obj;
                    IInspectable_AddRef( tpl_obj );
                }
                TRACE( "uwp_inflate_control_template(%s %s): inflated sub=%u -> %s named_cnt=%d\n",
                       debugstr_w(el->class_name), debugstr_w(el->name),
                       (unsigned)el->template_sub_idx, debugstr_w(tpl_root->class_name), el->named_cnt );
            }
            IInspectable_Release( tpl_obj );
        }
    }

    InterlockedDecrement( &inflate_depth );
    return el->template_root != NULL;
}

HRESULT WINAPI uwp_ctrl_ApplyTemplate( void *iface, boolean *ret )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    BOOL inflated = impl ? uwp_inflate_control_template( impl ) : FALSE;
    if (ret) *ret = inflated || (impl && impl->template_applied) ? TRUE : FALSE;
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

    /* Prefer real elements instantiated from the control's inflated XBF ControlTemplate
     * before falling back to synthetic template parts. */
    if (impl->template_root && impl->named_cnt > 0 &&
        wcscmp( nm, L"TextContainer" ) != 0 && wcscmp( nm, L"NormalOutput" ) != 0)
    {
        int i;
        for (i = 0; i < impl->named_cnt; i++)
        {
            if (impl->named_elems[i] && !wcscmp( impl->named_elems[i]->name, nm ))
            {
                child = impl->named_elems[i];
                break;
            }
        }
    }

    if (child)
    {
        /* Found in inflated ControlTemplate */
    }
    else if (!wcscmp( nm, L"TextContainer" ))
    {
        if (!impl->tpl_text_container)
        {
            IInspectable *raw = NULL;
            if (SUCCEEDED(uwp_element_create( L"Windows.UI.Xaml.Controls.ScrollViewer", NULL, NULL, (void **)&raw )) && raw)
            {
                child = uwp_elem_from_any( raw );
                if (child)
                {
                    child->parent = impl;
                    child->in_visual_tree = TRUE;
                    child->actual_w = 320.0;
                    child->actual_h = 80.0;
                    lstrcpynW( child->name, nm, ARRAY_SIZE(child->name) );
                    impl->tpl_text_container = child;
                }
                IInspectable_Release( raw );
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
                if (child)
                {
                    child->parent = impl->tpl_text_container ? impl->tpl_text_container : impl;
                    child->in_visual_tree = TRUE;
                    child->actual_w = 300.0;
                    child->actual_h = 64.0;
                    child->font_size = 44.0;
                    lstrcpynW( child->name, nm, ARRAY_SIZE(child->name) );
                    if (uwp_has_text( impl )) uwp_elem_set_text( child, uwp_elem_text( impl ) );
                    impl->tpl_normal_output = child;
                    if (impl->tpl_text_container)
                    {
                        impl->tpl_text_container->content = &child->IInspectable_iface;
                        IInspectable_AddRef( &child->IInspectable_iface );
                    }
                    if (impl->content) IInspectable_Release( impl->content );
                    impl->content = &child->IInspectable_iface;
                    IInspectable_AddRef( &child->IInspectable_iface );
                }
                IInspectable_Release( raw );
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
            if (child)
            {
                child->parent = impl;
                child->in_visual_tree = TRUE;
                child->visibility = 1; /* Collapsed by default */
                lstrcpynW( child->name, nm, ARRAY_SIZE(child->name) );
            }
            IInspectable_Release( raw );
        }
    }
    else if (!wcscmp( nm, L"ContentRootGrid" ) || !wcscmp( nm, L"RootGrid" ) ||
             !wcscmp( nm, L"DialogSpace" ) || !wcscmp( nm, L"CommandSpace" ))
    {
        IInspectable *raw = NULL;
        if (SUCCEEDED(uwp_element_create( L"Windows.UI.Xaml.Controls.Grid", NULL, NULL, (void **)&raw )) && raw)
        {
            child = uwp_elem_from_any( raw );
            if (child)
            {
                child->parent = impl;
                child->in_visual_tree = TRUE;
                child->actual_w = 360.0;
                child->actual_h = 180.0;
                lstrcpynW( child->name, nm, ARRAY_SIZE(child->name) );
            }
            IInspectable_Release( raw );
        }
    }
    else if (!wcscmp( nm, L"PrimaryButton" ) || !wcscmp( nm, L"SecondaryButton" ) ||
             !wcscmp( nm, L"CloseButton" ))
    {
        IInspectable *raw = NULL;
        if (SUCCEEDED(uwp_element_create( L"Windows.UI.Xaml.Controls.Button", NULL, NULL, (void **)&raw )) && raw)
        {
            child = uwp_elem_from_any( raw );
            if (child)
            {
                child->parent = impl;
                child->in_visual_tree = TRUE;
                lstrcpynW( child->name, nm, ARRAY_SIZE(child->name) );
            }
            IInspectable_Release( raw );
        }
    }
    else if (!wcscmp( nm, L"TabListView" ))
    {
        IInspectable *raw = NULL;
        if (SUCCEEDED(uwp_element_create( L"Windows.UI.Xaml.Controls.ListView", NULL, NULL, (void **)&raw )) && raw)
        {
            child = uwp_elem_from_any( raw );
            if (child)
            {
                child->parent = impl;
                child->in_visual_tree = TRUE;
                lstrcpynW( child->name, nm, ARRAY_SIZE(child->name) );
            }
            IInspectable_Release( raw );
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
            IInspectable *hold_raw = NULL;
            if (!wcscmp( part_cls, L"Microsoft.UI.Xaml.Controls.ItemsRepeater" ) &&
                (!wcscmp( nm, L"MenuItemsHost" ) || !wcscmp( nm, L"FooterMenuItemsHost" )))
            {
                if (SUCCEEDED(uwp_create_instance_from_provider( part_cls, (void **)&raw )) && raw)
                {
                    child = uwp_elem_from_any( raw );
                    if (child)
                    {
                        lstrcpynW( child->class_name, part_cls, ARRAY_SIZE(child->class_name) );
                        uwp_elem_sync_class_flags( child );
                        hold_raw = raw;
                    }
                    else
                        IInspectable_Release( raw );
                }
            }
            if (!child && SUCCEEDED(uwp_element_create( part_cls, NULL, NULL, (void **)&raw )) && raw)
            {
                child = uwp_elem_from_any( raw );
                if (child) hold_raw = raw;
                else IInspectable_Release( raw );
            }
            if (child)
            {
                child->parent = impl;
                child->in_visual_tree = TRUE;
                child->visibility = uwp_tpl_part_collapsed( nm ) ? 1 : 0;
                lstrcpynW( child->name, nm, ARRAY_SIZE(child->name) );
                uwp_element_add_named( impl, child );
            }
            if (hold_raw) IInspectable_Release( hold_raw );
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

    if (child && (impl->class_flags & UWP_CF_NAV_VIEW_ANY) &&
        (!wcscmp( nm, L"TogglePaneButton" ) || !wcscmp( nm, L"MenuItemsHost" ) ||
         !wcscmp( nm, L"FooterMenuItemsHost" )))
    {
        BOOL already_attached = FALSE;
        child->parent = impl;
        if (!wcscmp( nm, L"TogglePaneButton" ))
        {
            child->width = 40.0;
            child->height = 36.0;
            if (!uwp_has_text( child ))
                uwp_elem_set_text( child, L"\x2630" );
        }
        if (!impl->children_coll) uwp_vec_create( impl, &impl->children_coll );
        if (impl->children_coll)
        {
            struct uwp_vec_view *v = CONTAINING_RECORD( impl->children_coll, struct uwp_vec_view, IInspectable_iface );
            unsigned int vi;
            for (vi = 0; vi < v->count; vi++)
            {
                if (uwp_elem_from_any( v->items[vi] ) == child)
                {
                    already_attached = TRUE;
                    break;
                }
            }
            if (!already_attached)
            {
                uwp_vec_Append( impl->children_coll, child->outer ? child->outer : &child->IInspectable_iface );
                impl->in_visual_tree = TRUE;
                child->in_visual_tree = TRUE;
            }
        }
    }

    if (child)
    {
        *out = &child->IDependencyObject_iface;
        uwp_sub_AddRef( (IInspectable *)*out );
    }
    return S_OK;
}

const void *uwp_do_vtbl[16] =
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

const void *uwp_do2_vtbl[16] =
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
IDependencyProperty *g_routed_event_ptr_released = NULL;
IDependencyProperty *g_routed_event_tapped = NULL;
IDependencyProperty *g_routed_event_other = NULL;

static HRESULT WINAPI uwp_ui_AddHandler( void *iface, void *routed_event, void *handler, boolean handled_too )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    struct xaml_event_registry *target_reg = NULL;
    BOOL is_tapped = FALSE;
    (void)handled_too;
    if (!impl || !handler) return S_OK;
    WARN( "uwp_ui_AddHandler on %s (%s) ev=%p ptr_ev=%p handler=%p handled_too=%d\n",
          debugstr_w(impl->class_name), debugstr_w(impl->name), routed_event, g_routed_event_ptr_pressed, handler, handled_too );
    if (g_routed_event_ptr_pressed && routed_event == (void *)g_routed_event_ptr_pressed)
    {
        struct uwp_event_bag *ev = uwp_elem_ensure_events( impl );
        target_reg = ev ? &ev->ptr_pressed_reg : NULL;
    }
    else if (g_routed_event_ptr_released && routed_event == (void *)g_routed_event_ptr_released)
    {
        struct uwp_event_bag *ev = uwp_elem_ensure_events( impl );
        target_reg = ev ? &ev->ptr_released_reg : NULL;
    }
    else if (g_routed_event_tapped && routed_event == (void *)g_routed_event_tapped)
    {
        struct uwp_event_bag *ev = uwp_elem_ensure_events( impl );
        target_reg = ev ? &ev->tapped_reg : NULL;
        is_tapped = TRUE;
    }

    if (target_reg && !is_tapped)
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
            xaml_event_registry_add( target_reg, del, NULL );
            IUnknown_Release( (IUnknown *)peh );
        }
        else if (SUCCEEDED(IUnknown_QueryInterface( (IUnknown *)handler, &IID_IReference_PointerEventHandler, &iref )) && iref)
        {
            const struct abi_iref_ptr *air = (const struct abi_iref_ptr *)iref;
            void *unwrapped = NULL;
            if (air->lpVtbl->get_Value && SUCCEEDED(air->lpVtbl->get_Value( iref, &unwrapped )) && unwrapped)
            {
                if (SUCCEEDED(IUnknown_QueryInterface( (IUnknown *)unwrapped, &IID_IPointerEventHandler_UWP, &peh )) && peh)
                {
                    del = peh;
                    xaml_event_registry_add( target_reg, del, NULL );
                    IUnknown_Release( (IUnknown *)peh );
                }
                IUnknown_Release( (IUnknown *)unwrapped );
            }
            IUnknown_Release( (IUnknown *)iref );
        }
        TRACE( "IUIElement::AddHandler(Pointer) on %s (%s) -> del %p\n",
               debugstr_w(impl->class_name), debugstr_w(impl->name), del );
    }
    else if (target_reg && is_tapped)
    {
        static const GUID IID_ITappedEventHandler_UWP =
            { 0x68d940cc, 0x9ff0, 0x49ce, { 0xb1, 0x41, 0x3f, 0x07, 0xec, 0x47, 0x7b, 0x97 } };
        void *teh = NULL;
        if (SUCCEEDED(IUnknown_QueryInterface( (IUnknown *)handler, &IID_ITappedEventHandler_UWP, &teh )) && teh)
        {
            xaml_event_registry_add( target_reg, teh, NULL );
            IUnknown_Release( (IUnknown *)teh );
        }
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

static HRESULT WINAPI uwp_ui_UpdateLayout( void *iface )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    uwp_ui_InvalidateLayout( iface );
    if (impl && UWP_EV_CNT( impl, layout_updated_reg ) > 0 && !impl->in_measure_arrange)
    {
        unsigned int realized = impl->children_coll
            ? CONTAINING_RECORD( impl->children_coll, struct uwp_vec_view, IInspectable_iface )->count : 0;
        if (realized < 24)
        {
            impl->in_measure_arrange = TRUE;
            uwp_fire_event_registry( &impl->events->layout_updated_reg, NULL, NULL, "UpdateLayout" );
            impl->in_measure_arrange = FALSE;
        }
    }
    return S_OK;
}

static HRESULT WINAPI uwp_ui_get_Opacity( void *iface, double *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (out) *out = impl ? impl->opacity : 1.0;
    return S_OK;
}
static HRESULT WINAPI uwp_ui_put_Opacity( void *iface, double val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl)
    {
        if (val < 0.0) val = 0.0;
        if (val > 1.0) val = 1.0;
        if (impl->opacity != val)
        {
            impl->opacity = val;
            uwp_invalidate_window();
        }
    }
    return S_OK;
}

static HRESULT WINAPI uwp_ui_add_GotFocus( void *iface, void *handler, EventRegistrationToken *tok )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    struct uwp_event_bag *ev;
    if (!impl) return E_NOINTERFACE;
    ev = uwp_elem_ensure_events( impl );
    if (!ev) return E_OUTOFMEMORY;
    return xaml_event_registry_add( &ev->got_focus_reg, handler, tok );
}
static HRESULT WINAPI uwp_ui_remove_GotFocus( void *iface, EventRegistrationToken tok )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return E_NOINTERFACE;
    if (!impl->events) return S_OK;
    return xaml_event_registry_remove( &impl->events->got_focus_reg, tok );
}
static HRESULT WINAPI uwp_ui_add_LostFocus( void *iface, void *handler, EventRegistrationToken *tok )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    struct uwp_event_bag *ev;
    if (!impl) return E_NOINTERFACE;
    ev = uwp_elem_ensure_events( impl );
    if (!ev) return E_OUTOFMEMORY;
    return xaml_event_registry_add( &ev->lost_focus_reg, handler, tok );
}
static HRESULT WINAPI uwp_ui_remove_LostFocus( void *iface, EventRegistrationToken tok )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return E_NOINTERFACE;
    if (!impl->events) return S_OK;
    return xaml_event_registry_remove( &impl->events->lost_focus_reg, tok );
}

void uwp_ui_fire_got_focus( struct uwp_element *el )
{
    struct uwp_element *cur;
    for (cur = el; cur; cur = cur->parent)
    {
        void **handlers = NULL;
        unsigned int cnt = cur->events ? xaml_event_registry_snapshot_alloc( &cur->events->got_focus_reg, &handlers ) : 0;
        unsigned int i;
        if (!cnt) continue;
        for (i = 0; i < cnt; i++)
        {
            struct xaml_event_handler *h = (struct xaml_event_handler *)handlers[i];
            IInspectable *sender = cur->outer ? cur->outer : &cur->IInspectable_iface;
            IInspectable *args = (IInspectable *)g_routed_args();
            __TRY { h->lpVtbl->Invoke( h, sender, args ); }
            __EXCEPT_ALL { WARN( "%s: exception 0x%08lx\n", __func__, (unsigned long)GetExceptionCode() ); }
            __ENDTRY;
            if (args) IInspectable_Release( args );
            IUnknown_Release( (IUnknown *)h );
        }
        free( handlers );
    }
}

void uwp_ui_fire_lost_focus( struct uwp_element *el )
{
    struct uwp_element *cur;
    for (cur = el; cur; cur = cur->parent)
    {
        void **handlers = NULL;
        unsigned int cnt = cur->events ? xaml_event_registry_snapshot_alloc( &cur->events->lost_focus_reg, &handlers ) : 0;
        unsigned int i;
        if (!cnt) continue;
        for (i = 0; i < cnt; i++)
        {
            struct xaml_event_handler *h = (struct xaml_event_handler *)handlers[i];
            IInspectable *sender = cur->outer ? cur->outer : &cur->IInspectable_iface;
            IInspectable *args = (IInspectable *)g_routed_args();
            __TRY { h->lpVtbl->Invoke( h, sender, args ); }
            __EXCEPT_ALL { WARN( "%s: exception 0x%08lx\n", __func__, (unsigned long)GetExceptionCode() ); }
            __ENDTRY;
            if (args) IInspectable_Release( args );
            IUnknown_Release( (IUnknown *)h );
        }
        free( handlers );
    }
}

const void *uwp_ui_vtbl[128] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_ui_get_DesiredSize,
    [7 ... 8] = uwp_generic_noop,
    [9] = uwp_ui_get_Opacity,
    [10] = uwp_ui_put_Opacity,
    [11 ... 18] = uwp_generic_noop,
    [19] = uwp_ui_get_IsHitTestVisible,
    [20] = uwp_ui_put_IsHitTestVisible,
    [21] = uwp_ui_get_Visibility,
    [22] = uwp_ui_put_Visibility,
    [23] = uwp_ui_get_RenderSize,
    [24 ... 40] = uwp_generic_noop,
    [41 ... 44] = uwp_ui_event_add,
    [45] = uwp_ui_add_GotFocus,
    [46] = uwp_ui_remove_GotFocus,
    [47] = uwp_ui_add_LostFocus,
    [48] = uwp_ui_remove_LostFocus,
    [49 ... 56] = uwp_ui_event_add,
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
    [101] = uwp_ui_UpdateLayout,     /* UpdateLayout */
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
#define DEFINE_FE_PROBE(n) \
static HRESULT WINAPI uwp_fe_probe##n( void *iface, ... ) \
{ \
    struct uwp_element *impl = uwp_elem_from_any( iface ); \
    if (impl && (impl->class_flags & UWP_CF_NAV_VIEW_ANY)) \
        TRACE( "fe-vtbl slot " #n " hit on NavView\n" ); \
    return S_OK; \
}

DEFINE_FE_PROBE(6)   DEFINE_FE_PROBE(11)  DEFINE_FE_PROBE(12)  DEFINE_FE_PROBE(27)
DEFINE_FE_PROBE(28)  DEFINE_FE_PROBE(29)  DEFINE_FE_PROBE(30)
DEFINE_FE_PROBE(53)  DEFINE_FE_PROBE(54)  DEFINE_FE_PROBE(55)
DEFINE_FE_PROBE(58)  DEFINE_FE_PROBE(59)  DEFINE_FE_PROBE(60)  DEFINE_FE_PROBE(61)
DEFINE_FE_PROBE(63)  DEFINE_FE_PROBE(64)  DEFINE_FE_PROBE(65)  DEFINE_FE_PROBE(66)
DEFINE_FE_PROBE(67)  DEFINE_FE_PROBE(68)  DEFINE_FE_PROBE(69)  DEFINE_FE_PROBE(70)
DEFINE_FE_PROBE(71)  DEFINE_FE_PROBE(72)  DEFINE_FE_PROBE(73)  DEFINE_FE_PROBE(74)
DEFINE_FE_PROBE(75)  DEFINE_FE_PROBE(76)  DEFINE_FE_PROBE(77)  DEFINE_FE_PROBE(78)
DEFINE_FE_PROBE(79)  DEFINE_FE_PROBE(80)  DEFINE_FE_PROBE(81)  DEFINE_FE_PROBE(82)
DEFINE_FE_PROBE(83)  DEFINE_FE_PROBE(84)  DEFINE_FE_PROBE(85)  DEFINE_FE_PROBE(86)
DEFINE_FE_PROBE(87)  DEFINE_FE_PROBE(88)  DEFINE_FE_PROBE(89)  DEFINE_FE_PROBE(90)
DEFINE_FE_PROBE(91)  DEFINE_FE_PROBE(92)  DEFINE_FE_PROBE(93)  DEFINE_FE_PROBE(94)
DEFINE_FE_PROBE(95)  DEFINE_FE_PROBE(96)  DEFINE_FE_PROBE(97)  DEFINE_FE_PROBE(98)
DEFINE_FE_PROBE(99)  DEFINE_FE_PROBE(100) DEFINE_FE_PROBE(101) DEFINE_FE_PROBE(106)
DEFINE_FE_PROBE(107) DEFINE_FE_PROBE(108) DEFINE_FE_PROBE(109) DEFINE_FE_PROBE(110)
DEFINE_FE_PROBE(111)

#undef DEFINE_FE_PROBE

static IInspectable *uwp_resolve_binding_path( IInspectable *src, const WCHAR *path )
{
    void *app_outer = uwp_get_app_outer();
    void *provider = NULL;
    IInspectable *cur = src;
    BOOL cur_owned = FALSE;
    const WCHAR *p = path;

    if (!src || !path || !path[0] || !app_outer) return NULL;
    if (FAILED(IInspectable_QueryInterface( (IInspectable *)app_outer, &IID_IXamlMetadataProvider_UWP, &provider )) || !provider)
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

void uwp_update_binding_source( IInspectable *src, const WCHAR *path, IInspectable *new_val )
{
    void *app_outer = uwp_get_app_outer();
    void *provider = NULL;
    IInspectable *cur = src;
    BOOL cur_owned = FALSE;
    const WCHAR *last_dot;
    const WCHAR *leaf = path;

    if (!src || !path || !path[0] || !app_outer) return;
    last_dot = wcsrchr( path, L'.' );
    if (last_dot)
    {
        WCHAR prefix[128] = {0};
        size_t plen = (size_t)(last_dot - path);
        if (plen >= ARRAY_SIZE(prefix)) plen = ARRAY_SIZE(prefix) - 1;
        memcpy( prefix, path, plen * sizeof(WCHAR) );
        prefix[plen] = 0;
        cur = uwp_resolve_binding_path( src, prefix );
        if (!cur) return;
        cur_owned = TRUE;
        leaf = last_dot + 1;
    }
    if (!leaf[0])
    {
        if (cur_owned && cur) IInspectable_Release( cur );
        return;
    }
    if (SUCCEEDED(IInspectable_QueryInterface( (IInspectable *)app_outer, &IID_IXamlMetadataProvider_UWP, &provider )) && provider)
    {
        __TRY
        {
            HSTRING h_cls = NULL, h_prop = NULL;
            if (SUCCEEDED(IInspectable_GetRuntimeClassName( cur, &h_cls )) && h_cls)
            {
                const WCHAR *cls_name = WindowsGetStringRawBuffer( h_cls, NULL );
                if (cls_name && cls_name[0])
                {
                    struct xaml_type *xt = uwp_provider_get_xaml_type_cached( (struct xaml_metadata_provider *)provider, cls_name );
                    if (xt && xt->lpVtbl->GetMember && SUCCEEDED(WindowsCreateString( leaf, (UINT32)wcslen( leaf ), &h_prop )))
                    {
                        struct xaml_member *xm = NULL;
                        if (SUCCEEDED(xt->lpVtbl->GetMember( xt, h_prop, &xm )) && xm)
                        {
                            if (xm->lpVtbl->SetValue)
                                xm->lpVtbl->SetValue( xm, cur, new_val );
                            IUnknown_Release( (IUnknown *)xm );
                        }
                        WindowsDeleteString( h_prop );
                    }
                }
                WindowsDeleteString( h_cls );
            }
        }
        __EXCEPT_ALL { WARN( "%s: exception 0x%08lx\n", __func__, (unsigned long)GetExceptionCode() ); }
        __ENDTRY;
        IUnknown_Release( (IUnknown *)provider );
    }
    if (cur_owned && cur) IInspectable_Release( cur );
}

void uwp_refresh_element_bindings( struct uwp_element *el )
{
    int i;
    if (!el) return;
    for (i = 0; i < el->dc_bound_dp_cnt; i++)
    {
        if (el->dc_bound_dps[i].dp)
            uwp_apply_binding( el, el->dc_bound_dps[i].dp, el->dc_bound_dps[i].binding );
    }
    if (el->children_coll)
    {
        struct uwp_vec_view *v = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        unsigned int k;
        for (k = 0; v && k < v->count; k++)
        {
            struct uwp_element *ch = uwp_elem_from_any( v->items[k] );
            if (ch && ch != el)
                uwp_refresh_element_bindings( ch );
        }
    }
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
    WCHAR path_buf[128] = { 0 };
    IInspectable *src = NULL;

    if (!tel || !owner_cls || !owner_cls[0] || !prop_name || !prop_name[0]) return;
    if (bel && bel->tag)
    {
        void *pp = NULL;
        if (SUCCEEDED(IInspectable_QueryInterface( bel->tag, &IID_IPropertyPath_UWP, &pp )) && pp)
        {
            const struct abi_property_path *app = (const struct abi_property_path *)pp;
            HSTRING h_path = NULL;
            if (app->lpVtbl->get_Path && SUCCEEDED(app->lpVtbl->get_Path( pp, &h_path )) && h_path)
            {
                const WCHAR *raw = WindowsGetStringRawBuffer( h_path, NULL );
                if (raw) lstrcpynW( path_buf, raw, ARRAY_SIZE(path_buf) );
                WindowsDeleteString( h_path );
            }
            IUnknown_Release( (IUnknown *)pp );
        }
    }
    if (!path_buf[0] && uwp_has_text( bel ))
        lstrcpynW( path_buf, uwp_elem_text( bel ), ARRAY_SIZE(path_buf) );

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
    HSTRING h_path = NULL;
    const WCHAR *path_str = NULL;
    IInspectable *src = NULL;
    int i;

    if (!tel || !dp) return;

    if (bel && bel->tag)
    {
        void *pp = NULL;
        if (SUCCEEDED(IInspectable_QueryInterface( bel->tag, &IID_IPropertyPath_UWP, &pp )) && pp)
        {
            const struct abi_property_path *app = (const struct abi_property_path *)pp;
            if (app->lpVtbl->get_Path && SUCCEEDED(app->lpVtbl->get_Path( pp, &h_path )) && h_path)
                path_str = WindowsGetStringRawBuffer( h_path, NULL );
            IUnknown_Release( (IUnknown *)pp );
        }
    }

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
    else if (i < tel->dc_bound_dp_cnt && bel && tel->dc_bound_dps[i].binding != bel)
    {
        if (tel->dc_bound_dps[i].binding)
            IInspectable_Release( &tel->dc_bound_dps[i].binding->IInspectable_iface );
        tel->dc_bound_dps[i].binding = bel;
        IInspectable_AddRef( &bel->IInspectable_iface );
    }

    if (bel && bel->data_context)
    {
        src = bel->data_context;
    }
    else
    {
        struct uwp_element *anc;
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

const void *uwp_fe_vtbl[112] =
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
    [44] = uwp_fe_remove_Loaded,
    [48] = uwp_fe_remove_SizeChanged,
    [53] = uwp_fe_probe53,
    [54] = uwp_fe_probe54,
    [55] = uwp_fe_probe55,
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
    [57] = uwp_nav_put_IsPaneOpen,
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

const void *uwp_visual_state_vtbl[32] =
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

const void *uwp_noop_vtbl[128] =
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
