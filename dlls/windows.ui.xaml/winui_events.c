/* WinUI3 UIElement/FrameworkElement event registry + routed-event args.
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
#include "uwp_private.h"
#include "winui_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(xaml);

/* Generic WinUI 3 UIElement / FrameworkElement event registry & dispatch */

struct winui_ev_entry
{
    enum xaml_winui_ev_kind kind;
    IInspectable *sender;
    void *handler;
    INT64 token;
};

static struct winui_ev_entry g_winui_evs[256];
static int g_winui_ev_count;
static LONG g_winui_ev_next_tok = 100;
static SRWLOCK g_winui_ev_lock = SRWLOCK_INIT;

HRESULT winui_ev_add( enum xaml_winui_ev_kind kind, struct xaml_element *impl,
                             void *handler, EventRegistrationToken *token )
{
    INT64 tok = InterlockedIncrement( &g_winui_ev_next_tok );
    if (token) token->value = tok;
    if (!handler) return S_OK;
    AcquireSRWLockExclusive( &g_winui_ev_lock );
    if (g_winui_ev_count < (int)ARRAY_SIZE(g_winui_evs))
    {
        struct winui_ev_entry *e = &g_winui_evs[g_winui_ev_count++];
        e->kind = kind;
        e->sender = (impl && impl->outer) ? impl->outer : (impl ? &impl->IInspectable_iface : NULL);
        e->handler = handler;
        e->token = tok;
        IUnknown_AddRef( (IUnknown *)handler );
        TRACE( "winui_ev_add kind=%d sender=%p handler=%p tok=%I64d\n", kind, e->sender, handler, tok );
    }
    ReleaseSRWLockExclusive( &g_winui_ev_lock );
    return S_OK;
}

HRESULT WINAPI winui_ev_remove( void *iface, EventRegistrationToken token )
{
    int i;
    void *to_release = NULL;
    AcquireSRWLockExclusive( &g_winui_ev_lock );
    for (i = 0; i < g_winui_ev_count; i++)
    {
        if (g_winui_evs[i].token == token.value)
        {
            to_release = g_winui_evs[i].handler;
            g_winui_evs[i] = g_winui_evs[--g_winui_ev_count];
            break;
        }
    }
    ReleaseSRWLockExclusive( &g_winui_ev_lock );
    if (to_release) IUnknown_Release( (IUnknown *)to_release );
    return S_OK;
}

static void winui_ev_fire( enum xaml_winui_ev_kind kind, void *args )
{
    struct winui_ev_entry snap[64];
    int n = 0, i;
    AcquireSRWLockShared( &g_winui_ev_lock );
    for (i = 0; i < g_winui_ev_count && n < (int)ARRAY_SIZE(snap); i++)
    {
        if (g_winui_evs[i].kind == kind && g_winui_evs[i].handler)
        {
            snap[n] = g_winui_evs[i];
            IUnknown_AddRef( (IUnknown *)snap[n].handler );
            n++;
        }
    }
    ReleaseSRWLockShared( &g_winui_ev_lock );
    for (i = 0; i < n; i++)
    {
        typedef HRESULT (WINAPI *invoke_fn)( void *, void *, void * );
        invoke_fn fn = (invoke_fn)((void **)((void **)snap[i].handler)[0])[3];
        HRESULT hr_inv = S_OK;
        __TRY
        {
            hr_inv = fn( snap[i].handler, snap[i].sender, args );
        }
        __EXCEPT_ALL { hr_inv = 0xdeadbeef; }
        __ENDTRY;
        TRACE( "winui_ev_fire kind=%d sender=%p hr=%#lx\n", kind, snap[i].sender, hr_inv );
        IUnknown_Release( (IUnknown *)snap[i].handler );
    }
}

/* WinUI 3 SizeChangedEventArgs ({fe76324e-6dfb-58b1-9dcd-886ca8f9a2ea}) */
struct winui_size_args
{
    const void *vtbl;
    LONG ref;
    Size prev_sz;
    Size new_sz;
};
static HRESULT WINAPI winui_size_args_QI( void *iface, REFIID iid, void **out )
{
    static const GUID IID_ISizeChangedEventArgs_WinUI3 =
        { 0xfe76324e, 0x6dfb, 0x58b1, { 0x9d, 0xcd, 0x88, 0x6c, 0xa8, 0xf9, 0xa2, 0xea } };
    struct winui_size_args *a = iface;
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_ISizeChangedEventArgs_WinUI3 ) ||
        IsEqualGUID( iid, &IID_IRoutedEventArgs_WinUI3 ))
    {
        InterlockedIncrement( &a->ref );
        *out = a;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI winui_size_args_AddRef( void *iface ) { return InterlockedIncrement( &((struct winui_size_args *)iface)->ref ); }
static ULONG WINAPI winui_size_args_Release( void *iface )
{
    struct winui_size_args *a = iface;
    ULONG r = InterlockedDecrement( &a->ref );
    if (!r) free( a );
    return r;
}
static HRESULT WINAPI winui_size_args_get_PreviousSize( void *iface, Size *out )
{
    if (out) *out = ((struct winui_size_args *)iface)->prev_sz;
    return S_OK;
}
static HRESULT WINAPI winui_size_args_get_NewSize( void *iface, Size *out )
{
    if (out) *out = ((struct winui_size_args *)iface)->new_sz;
    return S_OK;
}
static const void *winui_size_args_vtbl[8] =
{
    [0] = winui_size_args_QI, [1] = winui_size_args_AddRef, [2] = winui_size_args_Release,
    [3] = xaml_root_GetIids, [4] = xaml_root_GetRuntimeClassName, [5] = xaml_root_GetTrustLevel,
    [6] = winui_size_args_get_PreviousSize,
    [7] = winui_size_args_get_NewSize,
};

/* WinUI 3 EffectiveViewportChangedEventArgs ({636e8159-2d82-538a-8483-cd576e41d0df}) */
struct winui_vp_args
{
    const void *vtbl;
    LONG ref;
    Rect vp;
};
static HRESULT WINAPI winui_vp_args_QI( void *iface, REFIID iid, void **out )
{
    static const GUID IID_IEffectiveViewportChangedEventArgs_WinUI3 =
        { 0x636e8159, 0x2d82, 0x538a, { 0x84, 0x83, 0xcd, 0x57, 0x6e, 0x41, 0xd0, 0xdf } };
    struct winui_vp_args *a = iface;
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IEffectiveViewportChangedEventArgs_WinUI3 ))
    {
        InterlockedIncrement( &a->ref );
        *out = a;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI winui_vp_args_AddRef( void *iface ) { return InterlockedIncrement( &((struct winui_vp_args *)iface)->ref ); }
static ULONG WINAPI winui_vp_args_Release( void *iface )
{
    struct winui_vp_args *a = iface;
    ULONG r = InterlockedDecrement( &a->ref );
    if (!r) free( a );
    return r;
}
static HRESULT WINAPI winui_vp_args_get_Viewport( void *iface, Rect *out )
{
    if (out) *out = ((struct winui_vp_args *)iface)->vp;
    return S_OK;
}
static HRESULT WINAPI winui_vp_args_get_ZeroDouble( void *iface, double *out )
{
    if (out) *out = 0.0;
    return S_OK;
}
static const void *winui_vp_args_vtbl[10] =
{
    [0] = winui_vp_args_QI, [1] = winui_vp_args_AddRef, [2] = winui_vp_args_Release,
    [3] = xaml_root_GetIids, [4] = xaml_root_GetRuntimeClassName, [5] = xaml_root_GetTrustLevel,
    [6] = winui_vp_args_get_Viewport,
    [7] = winui_vp_args_get_Viewport,
    [8] = winui_vp_args_get_ZeroDouble,
    [9] = winui_vp_args_get_ZeroDouble,
};

void xaml_winui_fire_size_changed( float old_w, float old_h, float new_w, float new_h )
{
    struct winui_size_args *sa;
    struct winui_vp_args *va;
    if (new_w > 0.0f && new_h > 0.0f)
    {
        g_winui_actual_w = new_w;
        g_winui_actual_h = new_h;
    }
    if ((sa = calloc( 1, sizeof(*sa) )))
    {
        sa->vtbl = winui_size_args_vtbl;
        sa->ref = 1;
        sa->prev_sz.Width = old_w;
        sa->prev_sz.Height = old_h;
        sa->new_sz.Width = new_w;
        sa->new_sz.Height = new_h;
        winui_ev_fire( WINUI_EV_SIZE_CHANGED, sa );
        winui_size_args_Release( sa );
    }
    if ((va = calloc( 1, sizeof(*va) )))
    {
        va->vtbl = winui_vp_args_vtbl;
        va->ref = 1;
        va->vp.X = 0.0f; va->vp.Y = 0.0f;
        va->vp.Width = new_w; va->vp.Height = new_h;
        winui_ev_fire( WINUI_EV_EFFECTIVE_VIEWPORT, va );
        winui_vp_args_Release( va );
    }
}

/* WinUI 3 PointerRoutedEventArgs + IPointer + Microsoft.UI.Input.IPointerPoint + IPointerPointProperties */
struct winui_ptr_args
{
    const void *args_vtbl;
    const void *ptr_vtbl;
    const void *pt_vtbl;
    const void *props_vtbl;
    LONG ref;
    struct xaml_pointer_info info;
    boolean handled;
    UINT64 timestamp;
};
static inline struct winui_ptr_args *wptr_from_args( void *p ) { return CONTAINING_RECORD( p, struct winui_ptr_args, args_vtbl ); }
static inline struct winui_ptr_args *wptr_from_ptr( void *p ) { return CONTAINING_RECORD( p, struct winui_ptr_args, ptr_vtbl ); }
static inline struct winui_ptr_args *wptr_from_pt( void *p ) { return CONTAINING_RECORD( p, struct winui_ptr_args, pt_vtbl ); }
static inline struct winui_ptr_args *wptr_from_props( void *p ) { return CONTAINING_RECORD( p, struct winui_ptr_args, props_vtbl ); }

static HRESULT winui_ptr_qi_common( struct winui_ptr_args *a, REFIID iid, void **out )
{
    static const GUID IID_IPointerRoutedEventArgs_WinUI3 =
        { 0x66e78a9a, 0x1bec, 0x5f92, { 0xb1, 0xa1, 0xea, 0x63, 0x34, 0xee, 0x51, 0x1c } };
    static const GUID IID_IPointer_WinUI3 =
        { 0x1f9afbf5, 0x11a3, 0x5e68, { 0xaa, 0x1b, 0x72, 0xfe, 0xbf, 0xa0, 0xab, 0x23 } };
    static const GUID IID_IPointerPoint_WinUI3 =
        { 0x0d430ee6, 0x252c, 0x59a4, { 0xb2, 0xa2, 0xd4, 0x42, 0x64, 0xdc, 0x6a, 0x40 } };
    static const GUID IID_IPointerPointProperties_WinUI3 =
        { 0xd760ed77, 0x4b10, 0x57a5, { 0xb3, 0xcc, 0xd9, 0xbf, 0x34, 0x13, 0xe9, 0x96 } };
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IPointerRoutedEventArgs_WinUI3 ) ||
        IsEqualGUID( iid, &IID_IRoutedEventArgs_WinUI3 ))
    {
        InterlockedIncrement( &a->ref );
        *out = &a->args_vtbl;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IPointer_WinUI3 ))
    {
        InterlockedIncrement( &a->ref );
        *out = &a->ptr_vtbl;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IPointerPoint_WinUI3 ))
    {
        InterlockedIncrement( &a->ref );
        *out = &a->pt_vtbl;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IPointerPointProperties_WinUI3 ))
    {
        InterlockedIncrement( &a->ref );
        *out = &a->props_vtbl;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG winui_ptr_release_common( struct winui_ptr_args *a )
{
    ULONG r = InterlockedDecrement( &a->ref );
    if (!r) free( a );
    return r;
}
static HRESULT WINAPI wptr_args_QI( void *i, REFIID id, void **o ) { return winui_ptr_qi_common( wptr_from_args(i), id, o ); }
static ULONG WINAPI wptr_args_AddRef( void *i ) { return InterlockedIncrement( &wptr_from_args(i)->ref ); }
static ULONG WINAPI wptr_args_Release( void *i ) { return winui_ptr_release_common( wptr_from_args(i) ); }
static HRESULT WINAPI wptr_args_get_Pointer( void *i, void **o )
{
    struct winui_ptr_args *a = wptr_from_args(i);
    if (!o) return E_POINTER;
    InterlockedIncrement( &a->ref );
    *o = &a->ptr_vtbl;
    return S_OK;
}
static HRESULT WINAPI wptr_args_get_KeyModifiers( void *i, UINT32 *o ) { if (o) *o = wptr_from_args(i)->info.key_modifiers; return S_OK; }
static HRESULT WINAPI wptr_args_get_Handled( void *i, boolean *o ) { if (o) *o = wptr_from_args(i)->handled; return S_OK; }
static HRESULT WINAPI wptr_args_put_Handled( void *i, boolean v ) { wptr_from_args(i)->handled = v; return S_OK; }
static HRESULT WINAPI wptr_args_get_IsGenerated( void *i, boolean *o ) { if (o) *o = FALSE; return S_OK; }
static HRESULT WINAPI wptr_args_GetCurrentPoint( void *i, void *rel, void **o )
{
    struct winui_ptr_args *a = wptr_from_args(i);
    if (!o) return E_POINTER;
    InterlockedIncrement( &a->ref );
    *o = &a->pt_vtbl;
    return S_OK;
}
static HRESULT WINAPI wptr_args_GetIntermediatePoints( void *i, void *rel, void **o ) { if (o) *o = NULL; return S_OK; }

static HRESULT WINAPI wptr_ptr_QI( void *i, REFIID id, void **o ) { return winui_ptr_qi_common( wptr_from_ptr(i), id, o ); }
static ULONG WINAPI wptr_ptr_AddRef( void *i ) { return InterlockedIncrement( &wptr_from_ptr(i)->ref ); }
static ULONG WINAPI wptr_ptr_Release( void *i ) { return winui_ptr_release_common( wptr_from_ptr(i) ); }
static HRESULT WINAPI wptr_ptr_get_PointerId( void *i, UINT32 *o ) { if (o) *o = wptr_from_ptr(i)->info.pointer_id ? wptr_from_ptr(i)->info.pointer_id : 1; return S_OK; }
static HRESULT WINAPI wptr_ptr_get_PointerDeviceType( void *i, int *o ) { if (o) *o = 2; return S_OK; } /* Mouse */
static HRESULT WINAPI wptr_ptr_get_IsInContact( void *i, boolean *o ) { if (o) *o = wptr_from_ptr(i)->info.is_in_contact ? TRUE : FALSE; return S_OK; }
static HRESULT WINAPI wptr_ptr_get_IsInRange( void *i, boolean *o ) { if (o) *o = TRUE; return S_OK; }

static HRESULT WINAPI wptr_pt_QI( void *i, REFIID id, void **o ) { return winui_ptr_qi_common( wptr_from_pt(i), id, o ); }
static ULONG WINAPI wptr_pt_AddRef( void *i ) { return InterlockedIncrement( &wptr_from_pt(i)->ref ); }
static ULONG WINAPI wptr_pt_Release( void *i ) { return winui_ptr_release_common( wptr_from_pt(i) ); }
static HRESULT WINAPI wptr_pt_get_FrameId( void *i, UINT32 *o ) { if (o) *o = 1; return S_OK; }
static HRESULT WINAPI wptr_pt_get_IsInContact( void *i, boolean *o ) { if (o) *o = wptr_from_pt(i)->info.is_in_contact ? TRUE : FALSE; return S_OK; }
static HRESULT WINAPI wptr_pt_get_PointerDeviceType( void *i, int *o ) { if (o) *o = 2; return S_OK; }
static HRESULT WINAPI wptr_pt_get_PointerId( void *i, UINT32 *o ) { if (o) *o = wptr_from_pt(i)->info.pointer_id ? wptr_from_pt(i)->info.pointer_id : 1; return S_OK; }
static HRESULT WINAPI wptr_pt_get_Position( void *i, Point *o )
{
    struct winui_ptr_args *a = wptr_from_pt(i);
    if (o) { o->X = a->info.x; o->Y = a->info.y; }
    return S_OK;
}
static HRESULT WINAPI wptr_pt_get_Properties( void *i, void **o )
{
    struct winui_ptr_args *a = wptr_from_pt(i);
    if (!o) return E_POINTER;
    InterlockedIncrement( &a->ref );
    *o = &a->props_vtbl;
    return S_OK;
}
static HRESULT WINAPI wptr_pt_get_Timestamp( void *i, UINT64 *o ) { if (o) *o = wptr_from_pt(i)->timestamp; return S_OK; }
static HRESULT WINAPI wptr_pt_GetTransformedPoint( void *i, void *xf, void **o )
{
    struct winui_ptr_args *a = wptr_from_pt(i);
    if (!o) return E_POINTER;
    InterlockedIncrement( &a->ref );
    *o = &a->pt_vtbl;
    return S_OK;
}

static HRESULT WINAPI wptr_props_QI( void *i, REFIID id, void **o ) { return winui_ptr_qi_common( wptr_from_props(i), id, o ); }
static ULONG WINAPI wptr_props_AddRef( void *i ) { return InterlockedIncrement( &wptr_from_props(i)->ref ); }
static ULONG WINAPI wptr_props_Release( void *i ) { return winui_ptr_release_common( wptr_from_props(i) ); }
static HRESULT WINAPI wptr_props_get_ContactRect( void *i, Rect *o )
{
    struct winui_ptr_args *a = wptr_from_props(i);
    if (o) { o->X = a->info.x; o->Y = a->info.y; o->Width = 1.0f; o->Height = 1.0f; }
    return S_OK;
}
static HRESULT WINAPI wptr_props_false( void *i, boolean *o ) { if (o) *o = FALSE; return S_OK; }
static HRESULT WINAPI wptr_props_true( void *i, boolean *o ) { if (o) *o = TRUE; return S_OK; }
static HRESULT WINAPI wptr_props_left( void *i, boolean *o ) { if (o) *o = wptr_from_props(i)->info.is_left ? TRUE : FALSE; return S_OK; }
static HRESULT WINAPI wptr_props_middle( void *i, boolean *o ) { if (o) *o = wptr_from_props(i)->info.is_middle ? TRUE : FALSE; return S_OK; }
static HRESULT WINAPI wptr_props_right( void *i, boolean *o ) { if (o) *o = wptr_from_props(i)->info.is_right ? TRUE : FALSE; return S_OK; }
static HRESULT WINAPI wptr_props_wheel( void *i, INT32 *o ) { if (o) *o = wptr_from_props(i)->info.wheel_delta; return S_OK; }
static HRESULT WINAPI wptr_props_update_kind( void *i, int *o ) { if (o) *o = (int)wptr_from_props(i)->info.update_kind; return S_OK; }
static HRESULT WINAPI wptr_props_zero_f( void *i, float *o ) { if (o) *o = 0.0f; return S_OK; }
static HRESULT WINAPI wptr_props_pressure( void *i, float *o ) { if (o) *o = 0.5f; return S_OK; }

static const void *winui_ptr_args_vtbl[14] =
{
    [0] = wptr_args_QI, [1] = wptr_args_AddRef, [2] = wptr_args_Release,
    [3] = xaml_root_GetIids, [4] = xaml_root_GetRuntimeClassName, [5] = xaml_root_GetTrustLevel,
    [6] = wptr_args_get_Pointer,
    [7] = wptr_args_get_KeyModifiers,
    [8] = wptr_args_get_Handled,
    [9] = wptr_args_put_Handled,
    [10] = wptr_args_get_IsGenerated,
    [11] = wptr_args_GetCurrentPoint,
    [12] = wptr_args_GetIntermediatePoints,
    [13] = element_generic_slot,
};
static const void *winui_ptr_ptr_vtbl[10] =
{
    [0] = wptr_ptr_QI, [1] = wptr_ptr_AddRef, [2] = wptr_ptr_Release,
    [3] = xaml_root_GetIids, [4] = xaml_root_GetRuntimeClassName, [5] = xaml_root_GetTrustLevel,
    [6] = wptr_ptr_get_PointerId,
    [7] = wptr_ptr_get_PointerDeviceType,
    [8] = wptr_ptr_get_IsInContact,
    [9] = wptr_ptr_get_IsInRange,
};
static const void *winui_ptr_pt_vtbl[14] =
{
    [0] = wptr_pt_QI, [1] = wptr_pt_AddRef, [2] = wptr_pt_Release,
    [3] = xaml_root_GetIids, [4] = xaml_root_GetRuntimeClassName, [5] = xaml_root_GetTrustLevel,
    [6] = wptr_pt_get_FrameId,
    [7] = wptr_pt_get_IsInContact,
    [8] = wptr_pt_get_PointerDeviceType,
    [9] = wptr_pt_get_PointerId,
    [10] = wptr_pt_get_Position,
    [11] = wptr_pt_get_Properties,
    [12] = wptr_pt_get_Timestamp,
    [13] = wptr_pt_GetTransformedPoint,
};
static const void *winui_ptr_props_vtbl[27] =
{
    [0] = wptr_props_QI, [1] = wptr_props_AddRef, [2] = wptr_props_Release,
    [3] = xaml_root_GetIids, [4] = xaml_root_GetRuntimeClassName, [5] = xaml_root_GetTrustLevel,
    [6] = wptr_props_get_ContactRect,
    [7] = wptr_props_false, [8] = wptr_props_false, [9] = wptr_props_false, [10] = wptr_props_false,
    [11] = wptr_props_true, [12] = wptr_props_false,
    [13] = wptr_props_left, [14] = wptr_props_middle, [15] = wptr_props_true, [16] = wptr_props_right,
    [17] = wptr_props_false, [18] = wptr_props_false,
    [19] = wptr_props_wheel, [20] = wptr_props_zero_f, [21] = wptr_props_update_kind,
    [22] = wptr_props_pressure, [23] = wptr_props_false,
    [24] = wptr_props_zero_f, [25] = wptr_props_zero_f, [26] = wptr_props_zero_f,
};

void xaml_winui_fire_pointer_event( enum xaml_pointer_event_kind kind, const struct xaml_pointer_info *info )
{
    struct winui_ptr_args *a;
    enum xaml_winui_ev_kind ev;
    if (!info) return;
    switch (kind)
    {
    case XAML_PTR_PRESSED:      ev = WINUI_EV_POINTER_PRESSED; break;
    case XAML_PTR_MOVED:        ev = WINUI_EV_POINTER_MOVED; break;
    case XAML_PTR_RELEASED:     ev = WINUI_EV_POINTER_RELEASED; break;
    case XAML_PTR_WHEEL:        ev = WINUI_EV_POINTER_WHEEL; break;
    case XAML_PTR_ENTERED:      ev = WINUI_EV_POINTER_ENTERED; break;
    case XAML_PTR_EXITED:       ev = WINUI_EV_POINTER_EXITED; break;
    case XAML_PTR_CAPTURE_LOST: ev = WINUI_EV_POINTER_CAPTURE_LOST; break;
    default: return;
    }
    if (!(a = calloc( 1, sizeof(*a) ))) return;
    a->args_vtbl = winui_ptr_args_vtbl;
    a->ptr_vtbl = winui_ptr_ptr_vtbl;
    a->pt_vtbl = winui_ptr_pt_vtbl;
    a->props_vtbl = winui_ptr_props_vtbl;
    a->ref = 1;
    a->info = *info;
    a->timestamp = GetTickCount64() * 1000;
    winui_ev_fire( ev, &a->args_vtbl );
    winui_ptr_release_common( a );
}

/* WinUI 3 KeyRoutedEventArgs ({ee357007-a2d6-5c75-9431-05fd66ec7915}) */
struct winui_key_args
{
    const void *vtbl;
    LONG ref;
    UINT32 vk;
    CorePhysicalKeyStatus status;
    boolean handled;
};
static HRESULT WINAPI wkey_QI( void *iface, REFIID iid, void **out )
{
    static const GUID IID_IKeyRoutedEventArgs_WinUI3 =
        { 0xee357007, 0xa2d6, 0x5c75, { 0x94, 0x31, 0x05, 0xfd, 0x66, 0xec, 0x79, 0x15 } };
    struct winui_key_args *a = iface;
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IKeyRoutedEventArgs_WinUI3 ) ||
        IsEqualGUID( iid, &IID_IRoutedEventArgs_WinUI3 ))
    {
        InterlockedIncrement( &a->ref );
        *out = a;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI wkey_AddRef( void *iface ) { return InterlockedIncrement( &((struct winui_key_args *)iface)->ref ); }
static ULONG WINAPI wkey_Release( void *iface )
{
    struct winui_key_args *a = iface;
    ULONG r = InterlockedDecrement( &a->ref );
    if (!r) free( a );
    return r;
}
static HRESULT WINAPI wkey_get_Key( void *iface, UINT32 *o ) { if (o) *o = ((struct winui_key_args *)iface)->vk; return S_OK; }
static HRESULT WINAPI wkey_get_KeyStatus( void *iface, CorePhysicalKeyStatus *o ) { if (o) *o = ((struct winui_key_args *)iface)->status; return S_OK; }
static HRESULT WINAPI wkey_get_Handled( void *iface, boolean *o ) { if (o) *o = ((struct winui_key_args *)iface)->handled; return S_OK; }
static HRESULT WINAPI wkey_put_Handled( void *iface, boolean v ) { ((struct winui_key_args *)iface)->handled = v; return S_OK; }
static HRESULT WINAPI wkey_get_DeviceId( void *iface, HSTRING *o ) { if (o) *o = NULL; return S_OK; }
static const void *winui_key_args_vtbl[12] =
{
    [0] = wkey_QI, [1] = wkey_AddRef, [2] = wkey_Release,
    [3] = xaml_root_GetIids, [4] = xaml_root_GetRuntimeClassName, [5] = xaml_root_GetTrustLevel,
    [6] = wkey_get_Key,
    [7] = wkey_get_KeyStatus,
    [8] = wkey_get_Handled,
    [9] = wkey_put_Handled,
    [10] = wkey_get_Key,
    [11] = wkey_get_DeviceId,
};

void xaml_winui_fire_key_event( BOOL is_down, UINT32 vk, LPARAM lparam )
{
    struct winui_key_args *a;
    if (!(a = calloc( 1, sizeof(*a) ))) return;
    a->vtbl = winui_key_args_vtbl;
    a->ref = 1;
    a->vk = vk;
    a->status.RepeatCount = (UINT32)(lparam & 0xffff);
    a->status.ScanCode = (UINT32)((lparam >> 16) & 0xff);
    a->status.IsExtendedKey = (lparam & (1 << 24)) != 0;
    a->status.IsMenuKeyDown = (lparam & (1 << 29)) != 0;
    a->status.WasKeyDown = (lparam & (1 << 30)) != 0;
    a->status.IsKeyReleased = !is_down;
    winui_ev_fire( is_down ? WINUI_EV_KEY_DOWN : WINUI_EV_KEY_UP, a );
    wkey_Release( a );
}

void xaml_winui_fire_focus_event( BOOL got_focus )
{
    void *args = NULL;
    element_create_typed( L"Microsoft.UI.Xaml.RoutedEventArgs", &args );
    winui_ev_fire( got_focus ? WINUI_EV_GOT_FOCUS : WINUI_EV_LOST_FOCUS, args );
    if (args) IInspectable_Release( (IInspectable *)args );
}

