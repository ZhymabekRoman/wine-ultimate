/* WinRT Windows.UI.Xaml Static Factories & Composition Preview Bridges
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
#include "weakreference.h"
#define WIDL_using_Windows_ApplicationModel
#define WIDL_using_Windows_Management_Deployment
#include "windows.management.deployment.h"
#define WIDL_using_Windows_UI_Composition
#include "windows.ui.composition.h"
#include "uwp_private.h"
WINE_DEFAULT_DEBUG_CHANNEL(xaml);

static inline struct uwp_class_factory *uwp_cf_from_any( void *iface )
{
    int i;
    if ((char *)iface >= (char *)&vsm_factory_obj && (char *)iface < (char *)(&vsm_factory_obj + 1))
        return &vsm_factory_obj;
    if ((char *)iface >= (char *)&auto_props_factory_obj && (char *)iface < (char *)(&auto_props_factory_obj + 1))
        return &auto_props_factory_obj;
    if ((char *)iface >= (char *)&vth_factory_obj && (char *)iface < (char *)(&vth_factory_obj + 1))
        return &vth_factory_obj;
    if ((char *)iface >= (char *)&feap_factory_obj && (char *)iface < (char *)(&feap_factory_obj + 1))
        return &feap_factory_obj;
    if ((char *)iface >= (char *)&scb_factory_obj && (char *)iface < (char *)(&scb_factory_obj + 1))
        return &scb_factory_obj;
    AcquireSRWLockShared( &g_dyn_factories_lock );
    for (i = 0; i < g_dyn_factory_cnt; i++)
    {
        if (g_dyn_factories[i] &&
            (char *)iface >= (char *)g_dyn_factories[i] &&
            (char *)iface < (char *)(g_dyn_factories[i] + 1))
        {
            struct uwp_class_factory *cf = g_dyn_factories[i];
            ReleaseSRWLockShared( &g_dyn_factories_lock );
            return cf;
        }
    }
    ReleaseSRWLockShared( &g_dyn_factories_lock );
    return NULL;
}

static const GUID IID_IScrollViewerStatics_local =
    { 0x33d1299d, 0x8d6e, 0x4290, { 0xbf, 0x22, 0x90, 0x5c, 0xcc, 0xd0, 0x4d, 0x31 } };
static const GUID IID_IComboBoxFactory_local =
    { 0xac0d0444, 0xa65b, 0x4abd, { 0x86, 0xdf, 0x30, 0x16, 0x04, 0x9e, 0xfe, 0xdc } };
static const GUID IID_ISolidColorBrushFactory_local =
    { 0xd935ce0c, 0x86f5, 0x4da6, { 0x8a, 0x27, 0xb1, 0x61, 0x9e, 0xf7, 0xf9, 0x2b } };
static const GUID IID_IButtonBaseStatics_Calc =
    { 0x67ef17e1, 0xfe37, 0x474f, { 0x9e, 0x97, 0x3b, 0x5e, 0x0b, 0x30, 0xf2, 0xdf } };

static const GUID IID_IElementCompositionPreviewStatics_UWP =
    { 0x08c92b38, 0xec99, 0x4c55, { 0xbc, 0x85, 0xa1, 0xc1, 0x80, 0xb2, 0x76, 0x46 } };
static const GUID IID_IElementCompositionPreviewStatics2_UWP =
    { 0x24148fbb, 0x23d6, 0x4f37, { 0xba, 0x0c, 0x07, 0x33, 0xe7, 0x99, 0x72, 0x2d } };
static const GUID IID_IElementCompositionPreviewStatics_WinUI3 =
    { 0x585a3a99, 0x9d25, 0x5673, { 0xa1, 0x0d, 0xd1, 0xf2, 0x92, 0xb8, 0x50, 0x53 } };
static const GUID IID_IElementCompositionPreviewStatics_WinUI3_v2 =
    { 0x84da5a6c, 0x0cfa, 0x532b, { 0x9b, 0x15, 0xcc, 0xd9, 0x86, 0x37, 0x43, 0x42 } };

static __x_ABI_CWindows_CUI_CComposition_CICompositor *g_ecp_compositor = NULL;
static HRESULT (WINAPI *g_orig_visual_co4_slot6)( void *iface, HSTRING prop, void **out ) = NULL;

struct ecp_comp5_stub_hdr
{
    const void *vtbl;
    const void *co4_vtbl;
    LONG ref;
    const WCHAR *class_name;
};

static HRESULT WINAPI ecp_visual_co4_slot6_shim( void *iface, void *a1, void **a2 )
{
    NT_TIB *tib = (NT_TIB *)NtCurrentTeb();
    if (!g_orig_visual_co4_slot6) return E_NOTIMPL;
    if (tib && a1 >= tib->StackLimit && a1 < tib->StackBase && *(void **)a1 == NULL)
    {
        void *stub = NULL;
        HRESULT hr = g_orig_visual_co4_slot6( iface, NULL, &stub );
        if (SUCCEEDED(hr) && stub)
            ((struct ecp_comp5_stub_hdr *)stub)->class_name = L"Windows.UI.Composition.CompositionShapeCollection";
        *(void **)a1 = stub;
        return hr;
    }
    return g_orig_visual_co4_slot6( iface, (HSTRING)a1, a2 );
}

static __x_ABI_CWindows_CUI_CComposition_CICompositor *ecp_get_compositor( void )
{
    if (!g_ecp_compositor)
    {
        HSTRING cls = NULL;
        IActivationFactory *af = NULL;
        if (SUCCEEDED(WindowsCreateString( L"Windows.UI.Composition.Compositor", 33, &cls )) && cls)
        {
            if (SUCCEEDED(RoGetActivationFactory( cls, &IID_IActivationFactory, (void **)&af )) && af)
            {
                IInspectable *inst = NULL;
                if (SUCCEEDED(IActivationFactory_ActivateInstance( af, &inst )) && inst)
                {
                    IInspectable_QueryInterface( inst, &IID___x_ABI_CWindows_CUI_CComposition_CICompositor, (void **)&g_ecp_compositor );
                    IInspectable_Release( inst );
                }
                IActivationFactory_Release( af );
            }
            WindowsDeleteString( cls );
        }
        if (g_ecp_compositor && !g_orig_visual_co4_slot6)
        {
            static const GUID IID_IShapeVisual_co4 =
                { 0xf2bd13c3, 0xba7e, 0x4b0f, { 0x91, 0x26, 0xff, 0xb7, 0x53, 0x6b, 0x81, 0x76 } };
            __x_ABI_CWindows_CUI_CComposition_CIContainerVisual *cv = NULL;
            if (SUCCEEDED(__x_ABI_CWindows_CUI_CComposition_CICompositor_CreateContainerVisual( g_ecp_compositor, &cv )) && cv)
            {
                void *co4 = NULL;
                if (SUCCEEDED(__x_ABI_CWindows_CUI_CComposition_CIContainerVisual_QueryInterface( cv, &IID_IShapeVisual_co4, &co4 )) && co4)
                {
                    void **vtbl = *(void ***)co4;
                    DWORD old_prot = 0;
                    if (vtbl && vtbl[6] && vtbl[6] != (void *)ecp_visual_co4_slot6_shim &&
                        VirtualProtect( &vtbl[6], sizeof(void *), PAGE_READWRITE, &old_prot ))
                    {
                        g_orig_visual_co4_slot6 = (void *)vtbl[6];
                        vtbl[6] = (void *)ecp_visual_co4_slot6_shim;
                        VirtualProtect( &vtbl[6], sizeof(void *), old_prot, &old_prot );
                    }
                    IUnknown_Release( (IUnknown *)co4 );
                }
                __x_ABI_CWindows_CUI_CComposition_CIContainerVisual_Release( cv );
            }
        }
    }
    return g_ecp_compositor;
}

static const void *ecp_statics_obj;
static const void *ecp_statics2_obj;

static HRESULT WINAPI ecp_QueryInterface( void *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = iface;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IElementCompositionPreviewStatics_UWP ) ||
        IsEqualGUID( iid, &IID_IElementCompositionPreviewStatics_WinUI3 ) ||
        IsEqualGUID( iid, &IID_IElementCompositionPreviewStatics_WinUI3_v2 ))
    {
        *out = (void *)&ecp_statics_obj;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IElementCompositionPreviewStatics2_UWP ))
    {
        *out = (void *)&ecp_statics2_obj;
        return S_OK;
    }
    TRACE( "ecp_QueryInterface unknown iid %s\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI ecp_AddRef( void *iface ) { return 2; }
static ULONG WINAPI ecp_Release( void *iface ) { return 1; }
static HRESULT WINAPI ecp_GetIids( void *iface, ULONG *c, IID **i )
{
    if (c) *c = 0;
    if (i) *i = NULL;
    return S_OK;
}
static HRESULT WINAPI ecp_GetRuntimeClassName( void *iface, HSTRING *n )
{
    return WindowsCreateString( L"Windows.UI.Xaml.Hosting.ElementCompositionPreview", 49, n );
}
static HRESULT WINAPI ecp_GetTrustLevel( void *iface, TrustLevel *l )
{
    if (l) *l = BaseTrust;
    return S_OK;
}
static HRESULT WINAPI ecp_GetElementVisual( void *iface, void *elem, void **out )
{
    __x_ABI_CWindows_CUI_CComposition_CICompositor *comp;
    __x_ABI_CWindows_CUI_CComposition_CIContainerVisual *cv = NULL;
    HRESULT hr;
    if (!out) return E_POINTER;
    *out = NULL;
    comp = ecp_get_compositor();
    if (!comp) return E_FAIL;
    hr = __x_ABI_CWindows_CUI_CComposition_CICompositor_CreateContainerVisual( comp, &cv );
    if (SUCCEEDED(hr) && cv)
    {
        hr = __x_ABI_CWindows_CUI_CComposition_CIContainerVisual_QueryInterface( cv, &IID___x_ABI_CWindows_CUI_CComposition_CIVisual, out );
        __x_ABI_CWindows_CUI_CComposition_CIContainerVisual_Release( cv );
    }
    TRACE( "ecp_GetElementVisual(%p) -> hr %#lx, visual %p\n", elem, hr, *out );
    return hr;
}
static HRESULT WINAPI ecp_GetElementChildVisual( void *iface, void *elem, void **out )
{
    if (!out) return E_POINTER;
    *out = NULL;
    return S_OK;
}
static IDCompositionVisual *g_ecp_child_visual = NULL;

static HRESULT WINAPI ecp_SetElementChildVisual( void *iface, void *elem, void *visual )
{
    IDCompositionVisual *root = xaml_get_dcomp_root_visual();
    IDCompositionVisual *dcomp_vis = NULL;
    struct uwp_element *el = uwp_elem_from_any( elem );
    if (el)
    {
        if (el->ecp_visual != (IInspectable *)visual)
        {
            if (el->ecp_visual) IInspectable_Release( el->ecp_visual );
            el->ecp_visual = (IInspectable *)visual;
            if (el->ecp_visual) IInspectable_AddRef( el->ecp_visual );
        }
        if (visual)
        {
            WCHAR tmp_uri[512] = {0};
            if (uwp_extract_visual_image_src( (IInspectable *)visual, tmp_uri, ARRAY_SIZE(tmp_uri) ))
            {
                struct uwp_media_ext *m = uwp_elem_ensure_media( el );
                if (m)
                {
                    lstrcpynW( m->image_src, tmp_uri, ARRAY_SIZE(m->image_src) );
                    TRACE( "ecp_SetElementChildVisual(%s): extracted uri=%s\n",
                           debugstr_w(el->name), debugstr_w(m->image_src) );
                    m->img_loaded = FALSE;
                    InterlockedExchange( &m->img_loading, 0 );
                    uwp_load_image_asset( el );
                    uwp_invalidate_window();
                }
            }
        }
    }
    if (visual)
        IUnknown_QueryInterface( (IUnknown *)visual, &IID_IDCompositionVisual, (void **)&dcomp_vis );
    if (root)
    {
        if (g_ecp_child_visual && g_ecp_child_visual != dcomp_vis)
        {
            IDCompositionVisual_RemoveVisual( root, g_ecp_child_visual );
            IDCompositionVisual_Release( g_ecp_child_visual );
            g_ecp_child_visual = NULL;
        }
        if (dcomp_vis && dcomp_vis != root && g_ecp_child_visual != dcomp_vis)
        {
            HRESULT hr_add = IDCompositionVisual_AddVisual( root, dcomp_vis, TRUE, NULL );
            TRACE( "ecp_SetElementChildVisual elem=%p vis=%p dcomp=%p root=%p hr=%#lx\n",
                 elem, visual, dcomp_vis, root, hr_add );
            if (SUCCEEDED(hr_add))
            {
                g_ecp_child_visual = dcomp_vis;
                IDCompositionVisual_AddRef( g_ecp_child_visual );
            }
        }
        xaml_dcomp_commit();
    }
    if (dcomp_vis) IDCompositionVisual_Release( dcomp_vis );
    return S_OK;
}
static HRESULT WINAPI ecp_GetScrollViewerManipulationPropertySet( void *iface, void *sv, void **out )
{
    __x_ABI_CWindows_CUI_CComposition_CICompositor *comp;
    __x_ABI_CWindows_CUI_CComposition_CICompositionPropertySet *ps = NULL;
    HRESULT hr;
    if (!out) return E_POINTER;
    *out = NULL;
    comp = ecp_get_compositor();
    if (!comp) return E_FAIL;
    hr = __x_ABI_CWindows_CUI_CComposition_CICompositor_CreatePropertySet( comp, &ps );
    if (SUCCEEDED(hr) && ps) *out = ps;
    return hr;
}
static HRESULT WINAPI ecp_SetImplicitShowAnimation( void *iface, void *elem, void *anim ) { return S_OK; }
static HRESULT WINAPI ecp_SetImplicitHideAnimation( void *iface, void *elem, void *anim ) { return S_OK; }
static HRESULT WINAPI ecp_SetIsTranslationEnabled( void *iface, void *elem, boolean val ) { return S_OK; }
static HRESULT WINAPI ecp_GetPointerPositionPropertySet( void *iface, void *elem, void **out )
{
    return ecp_GetScrollViewerManipulationPropertySet( iface, elem, out );
}

static const void *ecp_statics_vtbl[] =
{
    ecp_QueryInterface,
    ecp_AddRef,
    ecp_Release,
    ecp_GetIids,
    ecp_GetRuntimeClassName,
    ecp_GetTrustLevel,
    ecp_GetElementVisual,
    ecp_GetElementChildVisual,
    ecp_SetElementChildVisual,
    ecp_GetScrollViewerManipulationPropertySet,
};
static const void *ecp_statics_obj = ecp_statics_vtbl;

static const void *ecp_statics2_vtbl[] =
{
    ecp_QueryInterface,
    ecp_AddRef,
    ecp_Release,
    ecp_GetIids,
    ecp_GetRuntimeClassName,
    ecp_GetTrustLevel,
    ecp_SetImplicitShowAnimation,
    ecp_SetImplicitHideAnimation,
    ecp_SetIsTranslationEnabled,
    ecp_GetPointerPositionPropertySet,
};
static const void *ecp_statics2_obj = ecp_statics2_vtbl;

IActivationFactory *xaml_get_ecp_factory( void )
{
    return (IActivationFactory *)&ecp_statics_obj;
}

struct uwp_size_f_gpu { float width; float height; };
struct uwp_rect_f_gpu { float x; float y; float width; float height; };

static HRESULT WINAPI layout_info_GetAvailableSize( void *iface, void *el_ptr, struct uwp_size_f_gpu *out )
{
    struct uwp_element *el = uwp_elem_from_any( el_ptr );
    if (!out) return E_POINTER;
    out->width = el ? (float)el->actual_w : 0.0f;
    out->height = el ? (float)el->actual_h : 0.0f;
    return S_OK;
}
static HRESULT WINAPI layout_info_GetLayoutExceptionElement( void *iface, void *dispatcher, void **out )
{
    if (out) *out = NULL;
    return S_OK;
}
static HRESULT WINAPI layout_info_GetLayoutSlot( void *iface, void *el_ptr, struct uwp_rect_f_gpu *out )
{
    struct uwp_element *el = uwp_elem_from_any( el_ptr );
    if (!out) return E_POINTER;
    if (el && el->has_bounds)
    {
        out->x = (float)el->bounds.left;
        out->y = (float)el->bounds.top;
        out->width = (float)(el->bounds.right - el->bounds.left);
        out->height = (float)(el->bounds.bottom - el->bounds.top);
    }
    else
    {
        out->x = out->y = out->width = out->height = 0.0f;
    }
    return S_OK;
}
static const void *layout_info_statics_vtbl[] =
{
    ecp_QueryInterface,
    ecp_AddRef,
    ecp_Release,
    ecp_GetIids,
    ecp_GetRuntimeClassName,
    ecp_GetTrustLevel,
    layout_info_GetAvailableSize,
    layout_info_GetLayoutExceptionElement,
    layout_info_GetLayoutSlot,
};
static const void *layout_info_statics_obj = layout_info_statics_vtbl;

static const GUID IID_IConnectedAnimationService_UWP =
    { 0x1c6875c9, 0x19bb, 0x4d47, { 0xb9, 0xaa, 0x66, 0xc8, 0x02, 0xdc, 0xb9, 0xff } };
static const GUID IID_IConnectedAnimationServiceStatics_UWP =
    { 0xc7078ea5, 0xd688, 0x40e8, { 0x8f, 0x90, 0x96, 0xa6, 0x27, 0x92, 0x73, 0xd2 } };

static HRESULT WINAPI cas_QI( void *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IConnectedAnimationService_UWP ))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static HRESULT WINAPI cas_GetRuntimeClassName( void *iface, HSTRING *name )
{
    static const WCHAR cls[] = L"Windows.UI.Xaml.Media.Animation.ConnectedAnimationService";
    return WindowsCreateString( cls, ARRAY_SIZE(cls) - 1, name );
}
static HRESULT WINAPI cas_get_DefaultDuration( void *iface, INT64 *val )
{
    if (val) *val = 3000000; /* 300 ms */
    return S_OK;
}
static HRESULT WINAPI cas_put_DefaultDuration( void *iface, INT64 val ) { return S_OK; }
static HRESULT WINAPI cas_get_DefaultEasingFunction( void *iface, void **out )
{
    if (out) *out = NULL;
    return S_OK;
}
static HRESULT WINAPI cas_put_DefaultEasingFunction( void *iface, void *val ) { return S_OK; }
static HRESULT WINAPI cas_PrepareToAnimate( void *iface, HSTRING key, void *src, void **out )
{
    if (out) *out = NULL;
    return S_OK;
}
static HRESULT WINAPI cas_GetAnimation( void *iface, HSTRING key, void **out )
{
    if (out) *out = NULL;
    return S_OK;
}
static const void *cas_vtbl[] =
{
    cas_QI, ecp_AddRef, ecp_Release, ecp_GetIids, cas_GetRuntimeClassName, ecp_GetTrustLevel,
    cas_get_DefaultDuration, cas_put_DefaultDuration,
    cas_get_DefaultEasingFunction, cas_put_DefaultEasingFunction,
    cas_PrepareToAnimate, cas_GetAnimation,
};
static const void *cas_obj = cas_vtbl;

static HRESULT WINAPI cas_statics_QI( void *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IConnectedAnimationServiceStatics_UWP ))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static HRESULT WINAPI cas_statics_GetForCurrentView( void *iface, void **out )
{
    if (!out) return E_POINTER;
    *out = (void *)&cas_obj;
    return S_OK;
}
static const void *cas_statics_vtbl[] =
{
    cas_statics_QI, ecp_AddRef, ecp_Release, ecp_GetIids, cas_GetRuntimeClassName, ecp_GetTrustLevel,
    cas_statics_GetForCurrentView,
};
static const void *cas_statics_obj = cas_statics_vtbl;

static HRESULT WINAPI uwp_lis_statics_QI( void *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_ILoadedImageSurfaceStatics_UWP ))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static HRESULT WINAPI uwp_lis_statics_GetRuntimeClassName( void *iface, HSTRING *n )
{
    return WindowsCreateString( L"Windows.UI.Xaml.Media.LoadedImageSurface", 40, n );
}
static HRESULT WINAPI uwp_lis_StartLoadFromUriWithSize( void *iface, IInspectable *uri,
                                                        struct __x_ABI_CWindows_CFoundation_CSize desired_max_size, IInspectable **out )
{
    void *inst = NULL;
    struct uwp_element *el;
    HRESULT hr;
    if (!out) return E_POINTER;
    *out = NULL;
    hr = uwp_element_create( L"Windows.UI.Xaml.Media.LoadedImageSurface", NULL, NULL, &inst );
    if (FAILED(hr) || !inst) return FAILED(hr) ? hr : E_OUTOFMEMORY;
    el = uwp_elem_from_any( inst );
    if (el)
    {
        struct uwp_media_ext *m = uri ? uwp_elem_ensure_media( el ) : NULL;
        if (uri && m) uwp_extract_uri_string( uri, m->image_src, ARRAY_SIZE(m->image_src) );
        TRACE( "LoadedImageSurface.StartLoadFromUriWithSize uri=%s\n", debugstr_w(uwp_elem_image_src( el )) );
        if (m && m->image_src[0]) uwp_load_image_asset( el );
        *out = &el->ILoadedImageSurface_iface;
    }
    else
        *out = (IInspectable *)inst;
    return S_OK;
}
static HRESULT WINAPI uwp_lis_StartLoadFromUri( void *iface, IInspectable *uri, IInspectable **out )
{
    struct __x_ABI_CWindows_CFoundation_CSize sz = { 0.0f, 0.0f };
    return uwp_lis_StartLoadFromUriWithSize( iface, uri, sz, out );
}
static HRESULT WINAPI uwp_lis_StartLoadFromStreamWithSize( void *iface, IInspectable *stream,
                                                           struct __x_ABI_CWindows_CFoundation_CSize desired_max_size, IInspectable **out )
{
    void *inst = NULL;
    struct uwp_element *el;
    HRESULT hr;
    if (!out) return E_POINTER;
    *out = NULL;
    hr = uwp_element_create( L"Windows.UI.Xaml.Media.LoadedImageSurface", NULL, NULL, &inst );
    if (FAILED(hr) || !inst) return FAILED(hr) ? hr : E_OUTOFMEMORY;
    el = uwp_elem_from_any( inst );
    *out = el ? &el->ILoadedImageSurface_iface : (IInspectable *)inst;
    return S_OK;
}
static HRESULT WINAPI uwp_lis_StartLoadFromStream( void *iface, IInspectable *stream, IInspectable **out )
{
    struct __x_ABI_CWindows_CFoundation_CSize sz = { 0.0f, 0.0f };
    return uwp_lis_StartLoadFromStreamWithSize( iface, stream, sz, out );
}
static const void *uwp_lis_statics_vtbl[] =
{
    uwp_lis_statics_QI, ecp_AddRef, ecp_Release, ecp_GetIids, uwp_lis_statics_GetRuntimeClassName, ecp_GetTrustLevel,
    uwp_lis_StartLoadFromUriWithSize,
    uwp_lis_StartLoadFromUri,
    uwp_lis_StartLoadFromStreamWithSize,
    uwp_lis_StartLoadFromStream,
};
static const void *uwp_lis_statics_obj = uwp_lis_statics_vtbl;

static const void *comp_target_statics_obj;
static const void *binding_ops_statics_obj;
static const void *thickness_helper_statics_obj;
static const void *uwp_uielement_statics_obj;
static const void *uwp_uielement_routed_statics_obj;
static const void *uwp_grid_statics_obj;
static const GUID IID_IUIElementStatics4_UWP;
static const GUID IID_IUIElementStatics6_UWP;
static const GUID IID_IUIElementStatics7_UWP;
static const GUID IID_IUIElementStatics8_UWP;
static const void *xaml_markup_helper_statics_obj;

HRESULT WINAPI uwp_cf_QI( IActivationFactory *iface, REFIID iid, void **out )
{
    struct uwp_class_factory *f = uwp_cf_from_any( iface );
    TRACE( "uwp_cf_QI enter: cls=%s, iid=%s\n", f ? debugstr_w(f->class_name) : "?", debugstr_guid(iid) );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IMarshal ) ||
        IsEqualGUID( iid, &IID_IReferenceTracker_UWP ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget_UWP ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget_UWP2 ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = f ? (void *)&f->IActivationFactory_iface : (void *)iface;
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IElementCompositionPreviewStatics_UWP ))
    {
        *out = (void *)&ecp_statics_obj;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IElementCompositionPreviewStatics2_UWP ))
    {
        *out = (void *)&ecp_statics2_obj;
        return S_OK;
    }
    if (f && (f->factory_flags & UWP_FF_COMP_TARGET))
    {
        *out = (void *)&comp_target_statics_obj;
        return S_OK;
    }

    {
        static const GUID IID_IControlStatics_UWP =
            { 0x473b305b, 0x877d, 0x4c35, { 0x8f, 0xca, 0x46, 0xa0, 0x5f, 0x96, 0x60, 0x7a } };
        static const GUID IID_IRangeBaseStatics_UWP =
            { 0x67ef17e1, 0xfe37, 0x474f, { 0x9e, 0x97, 0x3b, 0x5e, 0x0b, 0x30, 0xf2, 0xe0 } };
        static const GUID IID_IAutoSuggestBoxStatics_UWP =
            { 0xee22c7f4, 0xc15d, 0x4ff3, { 0x8a, 0x94, 0xf5, 0x0d, 0xfd, 0xfb, 0xe8, 0x9a } };
        static const GUID IID_ISplitViewStatics_UWP =
            { 0x859b4f6f, 0x44ab, 0x4e4b, { 0x91, 0xc1, 0x17, 0xb7, 0x05, 0x6d, 0x9b, 0x5f } };
        static const GUID IID_IContentControlStatics_UWP =
            { 0x116982a4, 0x5cf5, 0x4c05, { 0xa5, 0x3f, 0x15, 0xe4, 0x87, 0x6c, 0x85, 0xec } };
        static const GUID IID_ISymbolIconStatics_UWP =
            { 0x8624fbdf, 0x952c, 0x4f76, { 0xb0, 0x3f, 0x30, 0x83, 0x3b, 0xe9, 0x95, 0x72 } };
        static const GUID IID_IBindingOperationsStatics_UWP =
            { 0xe155ef73, 0x95a0, 0x4aab, { 0x8c, 0x7d, 0x2a, 0x47, 0xda, 0x07, 0x3c, 0x79 } };
        static const GUID IID_IThicknessHelperStatics_UWP =
            { 0xc0991a7c, 0x070c, 0x4da6, { 0x87, 0x84, 0x01, 0xca, 0x80, 0x0e, 0xb7, 0x3a } };
        static const GUID IID_ILayoutInformationStatics_UWP =
            { 0xcf06cf99, 0x58e9, 0x4682, { 0x83, 0x26, 0x50, 0xca, 0xab, 0x65, 0xed, 0x7c } };
        static const GUID IID_IStoryboardStatics_UWP =
            { 0xd82f07d8, 0x73d5, 0x4379, { 0xbd, 0x48, 0x7e, 0x05, 0x18, 0x4a, 0x8b, 0xad } };
        if (IsEqualGUID( iid, &IID_IStoryboardStatics_UWP ))
        {
            *out = (void *)&sb_factory_obj.IStatics_iface;
            return S_OK;
        }
        if (IsEqualGUID( iid, &IID_ILayoutInformationStatics_UWP ))
        {
            *out = (void *)&layout_info_statics_obj;
            return S_OK;
        }
        if (IsEqualGUID( iid, &IID_IConnectedAnimationServiceStatics_UWP ))
        {
            *out = (void *)&cas_statics_obj;
            return S_OK;
        }
        if (IsEqualGUID( iid, &IID_IBindingOperationsStatics_UWP ) ||
            IsEqualGUID( iid, &IID_IXamlBindingHelperStatics_UWP ))
        {
            *out = (void *)&binding_ops_statics_obj;
            return S_OK;
        }
        if (IsEqualGUID( iid, &IID_IThicknessHelperStatics_UWP ))
        {
            *out = (void *)&thickness_helper_statics_obj;
            return S_OK;
        }
        if (IsEqualGUID( iid, &IID_IButtonBaseStatics_UWP ) ||
            IsEqualGUID( iid, &IID_IButtonBaseStatics_Calc ) ||
            IsEqualGUID( iid, &IID_IToggleButtonStatics_UWP ) ||
            IsEqualGUID( iid, &IID_IControlStatics_UWP ) ||
            IsEqualGUID( iid, &IID_IRangeBaseStatics_UWP ) ||
            IsEqualGUID( iid, &IID_IAutoSuggestBoxStatics_UWP ) ||
            IsEqualGUID( iid, &IID_ISplitViewStatics_UWP ) ||
            IsEqualGUID( iid, &IID_IContentControlStatics_UWP ) ||
            IsEqualGUID( iid, &IID_ISymbolIconStatics_UWP ) ||
            IsEqualGUID( iid, &IID_IToggleButtonFactory2_UWP )) /* ISelectorStatics {13300b06-...} */
        {
            *out = f && f->IDpStatics_iface.lpVtbl ? (void *)&f->IDpStatics_iface : (void *)&f->IStatics_iface;
            return S_OK;
        }
    }

    {
        static const GUID IID_IPopupStatics_UWP =
            { 0xaa119f27, 0x2055, 0x483e, { 0xa4, 0xe0, 0x6f, 0x25, 0x58, 0x73, 0x2d, 0x9b } };
        if (IsEqualGUID( iid, &IID_IPopupStatics_UWP ))
        {
            *out = f ? (void *)&f->IStatics_iface : (void *)iface;
            return S_OK;
        }
    }
    {
        static const GUID IID_IFrameworkElementAutomationPeerFactory_UWP =
            { 0x0db9b8bc, 0xb812, 0x48e3, { 0xaf, 0x1f, 0xdb, 0xc5, 0x76, 0x00, 0xc3, 0x25 } };
        if (IsEqualGUID( iid, &IID_IFrameworkElementAutomationPeerFactory_UWP ))
        {
            *out = f ? (void *)&f->IComposableFactory_iface : (void *)iface;
            return S_OK;
        }
    }
    if (IsEqualGUID( iid, &IID_IUIElementStatics_UWP ))
    {
        *out = (void *)&uwp_uielement_statics_obj;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IUIElementStatics4_UWP ) ||
        IsEqualGUID( iid, &IID_IUIElementStatics6_UWP ) ||
        IsEqualGUID( iid, &IID_IUIElementStatics7_UWP ) ||
        IsEqualGUID( iid, &IID_IUIElementStatics8_UWP ))
    {
        *out = (void *)&uwp_uielement_routed_statics_obj;
        return S_OK;
    }
    {
        static const GUID IID_IGridStatics_UWP3 =
            { 0x64fe2e9f, 0xf951, 0x42b6, { 0xa9, 0xce, 0xbb, 0x17, 0x9a, 0xf1, 0x15, 0x95 } };
        if (IsEqualGUID( iid, &IID_IGridStatics_UWP ) ||
            IsEqualGUID( iid, &IID_IGridStatics_UWP2 ) ||
            IsEqualGUID( iid, &IID_IGridStatics_UWP3 ))
        {
            *out = (void *)&uwp_grid_statics_obj;
            return S_OK;
        }
    }
    if (f == &auto_props_factory_obj ||
        f == &feap_factory_obj ||
        IsEqualGUID( iid, &IID_ICanvasStatics_UWP ) ||
        IsEqualGUID( iid, &IID_IScrollViewerStatics_local ) ||
        IsEqualGUID( iid, &IID_IVisualTreeHelperStatics_UWP ) ||
        IsEqualGUID( iid, &IID_IFrameworkElementStatics_UWP ) ||
        IsEqualGUID( iid, &IID_IToggleButtonStatics_UWP ) ||
        (iid->Data1 == 0x90143898 && iid->Data2 == 0xa16c && iid->Data3 == 0x4759) || /* IButtonStaticsWithFlyout */
        (iid->Data1 == 0x16c724fb && iid->Data2 == 0x6c1a && iid->Data3 == 0x4852) || /* IToggleMenuFlyoutItemStatics */
        (iid->Data1 == 0x1c781b6b && iid->Data2 == 0x37bf && iid->Data3 == 0x4ddc) || /* IFontIconStatics */
        (iid->Data1 == 0xe6080672 && iid->Data2 == 0x08f8 && iid->Data3 == 0x43d3))   /* IFontIconStatics2 */
    {
        *out = (void *)&f->IStatics_iface;
        return S_OK;
    }
    {
        static const GUID IID_IRangeBaseFactory_UWP =
            { 0x389b7c71, 0x5220, 0x42b2, { 0x99, 0x92, 0x26, 0x90, 0xc1, 0xa6, 0x70, 0x30 } };
        static const GUID IID_IMediaPlayerElementFactory_UWP =
            { 0x77e525c3, 0xeb17, 0x4b8d, { 0x88, 0x9d, 0x1e, 0xa8, 0xab, 0xdb, 0xd4, 0xef } };
        static const GUID IID_ISwapChainPanelFactory_UWP =
            { 0xf38f8d7f, 0x1a48, 0x49cb, { 0x86, 0xd2, 0x10, 0xea, 0xaa, 0xf6, 0xfd, 0x70 } };
        if (IsEqualGUID( iid, &IID_IRangeBaseFactory_UWP ) ||
            IsEqualGUID( iid, &IID_IMediaPlayerElementFactory_UWP ) ||
            IsEqualGUID( iid, &IID_ISwapChainPanelFactory_UWP ))
        {
            *out = f ? (void *)&f->IComposableFactory_iface : (void *)iface;
            return S_OK;
        }
    }
    {
        static const GUID IID_IBitmapImageFactory_UWP =
            { 0xc9132978, 0x4810, 0x4e5e, { 0x80, 0x87, 0x03, 0x67, 0x1e, 0xe6, 0x0d, 0x85 } };
        if (IsEqualGUID( iid, &IID_IBitmapImageFactory_UWP ) && f && f->IBitmapImageFactory_iface.lpVtbl)
        {
            *out = (void *)&f->IBitmapImageFactory_iface;
            return S_OK;
        }
        if (IsEqualGUID( iid, &IID_IFontFamilyFactory_UWP ) && f && f->IFontFamilyFactory_iface.lpVtbl)
        {
            *out = (void *)&f->IFontFamilyFactory_iface;
            return S_OK;
        }
    }
    if (IsEqualGUID( iid, &IID_IFrameFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IPageFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IUserControlFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IContentControlFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IContentControlFactory_UWP2 ) ||
        IsEqualGUID( iid, &IID_IControlFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IUIElementFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IFrameworkElementFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IDependencyObjectFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IButtonFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IButtonBaseFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IToggleButtonFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IPanelFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IPanelFactory_UWP2 ) ||
        IsEqualGUID( iid, &IID_IGridFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IListViewFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IComboBoxFactory_local ) ||
        IsEqualGUID( iid, &IID_ISolidColorBrushFactory_local ) ||
        IsEqualGUID( iid, &IID_IXamlLightFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IContentPresenterFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IXamlCompositionBrushBaseFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IFlyoutBaseFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IStateTriggerBaseFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IMenuFlyoutItemBaseFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IMenuFlyoutItemFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IMenuFlyoutSeparatorFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IMarkupExtensionFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IRoutedEventArgsFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IRoutedEventArgsFactory_WinUI3 ) ||
        IsEqualGUID( iid, &IID_IBindingFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IRelativeSourceFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IRadioButtonFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IPropertyPathFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IDependencyObjectCollectionFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IVisualStateManagerFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IDataTemplateSelectorFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IStyleSelectorFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IStyleSelectorFactory_UWP2 ) ||
        IsEqualGUID( iid, &IID_IStateTriggerBaseFactory2_UWP ) ||
        IsEqualGUID( iid, &IID_IDesktopWindowXamlSourceFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IDispatcherTimerFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IKeyboardAcceleratorFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IContentDialogFactory_UWP ))
    {
        *out = f ? (void *)&f->IComposableFactory_iface : (void *)iface;
        return S_OK;
    }
    if (f && (f->factory_flags & UWP_FF_COMPOSABLE_FALLBACK))
    {
        WARN( "uwp_cf_QI(%s) unknown iid %s -> ComposableFactory fallback\n",
              debugstr_w(f->class_name), debugstr_guid(iid) );
        *out = (void *)&f->IComposableFactory_iface;
        return S_OK;
    }
    {
        static const GUID IID_IXamlMarkupHelperStatics_UWP =
            { 0xc9bc3725, 0xf34f, 0x445c, { 0x81, 0xa2, 0x6b, 0x72, 0xa5, 0xe8, 0xf0, 0x72 } };
        if (IsEqualGUID( iid, &IID_IXamlMarkupHelperStatics_UWP ))
        {
            *out = (void *)&xaml_markup_helper_statics_obj;
            return S_OK;
        }
        if (IsEqualGUID( iid, &IID_ILoadedImageSurfaceStatics_UWP ))
        {
            *out = (void *)&uwp_lis_statics_obj;
            return S_OK;
        }
    }
    /* Unknown IID on factory: log so we can see exact Statics/Factory IID, return Statics */
    WARN( "uwp_cf_QI(%s) unknown iid %s -> Statics fallback\n",
          f ? debugstr_w(f->class_name) : "?", debugstr_guid(iid) );
    *out = f ? (void *)&f->IStatics_iface : (void *)iface;
    return S_OK;
}
ULONG WINAPI uwp_cf_AddRef( IActivationFactory *iface ) { return 2; }
ULONG WINAPI uwp_cf_Release( IActivationFactory *iface ) { return 1; }
HRESULT WINAPI uwp_cf_GetIids( IActivationFactory *iface, ULONG *cnt, IID **iids )
{
    if (cnt) *cnt = 0;
    if (iids) *iids = NULL;
    return S_OK;
}
HRESULT WINAPI uwp_cf_GetRuntimeClassName( IActivationFactory *iface, HSTRING *name )
{
    struct uwp_class_factory *f = uwp_cf_from_any( iface );
    const WCHAR *cls = f ? f->class_name : L"Windows.UI.Xaml.FrameworkElement";
    return WindowsCreateString( cls, wcslen(cls), name );
}
HRESULT WINAPI uwp_cf_GetTrustLevel( IActivationFactory *iface, TrustLevel *lvl )
{
    if (lvl) *lvl = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI uwp_cf_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    struct uwp_class_factory *f = uwp_cf_from_any( iface );
    const WCHAR *cls = f ? f->class_name : L"Windows.UI.Xaml.FrameworkElement";
    TRACE( "uwp_cf_ActivateInstance(%s, %p)\n", debugstr_w(cls), instance );
    return uwp_element_create( cls, NULL, NULL, (void **)instance );
}

static HRESULT WINAPI uwp_cf_CreateInstance( void *iface, IInspectable *outer, IInspectable **inner, void **instance )
{
    struct uwp_class_factory *f = uwp_cf_from_any( iface );
    const WCHAR *cls = f ? f->class_name : L"Windows.UI.Xaml.FrameworkElement";
    /* IPropertyPathFactory::CreateInstance(HSTRING path, IPropertyPath **out)
     * and ISymbolIconFactory::CreateInstanceWithIcon(Symbol sym, ISymbolIcon **out)
     * take 2 args after iface, so the output pointer arrives in `inner`. */
    if (f && (f->factory_flags & (UWP_FF_PROP_PATH | UWP_FF_SYMBOL_ICON)))
    {
        HRESULT hr;
        TRACE( "uwp_cf_CreateInstance(%s, arg1=%p, out=%p)\n", debugstr_w(cls), outer, inner );
        hr = uwp_element_create( cls, NULL, NULL, (void **)inner );
        if (SUCCEEDED(hr) && inner && *(void **)inner && (f->factory_flags & UWP_FF_PROP_PATH) &&
            (ULONG_PTR)outer >= 0x10000)
        {
            struct uwp_element *pp_el = uwp_elem_from_any( *(void **)inner );
            UINT32 len = 0;
            const WCHAR *raw = WindowsGetStringRawBuffer( (HSTRING)outer, &len );
            if (pp_el && raw && len > 0)
                uwp_elem_set_text_n( pp_el, raw, len );
        }
        if (SUCCEEDED(hr) && inner && *(void **)inner && (f->factory_flags & UWP_FF_SYMBOL_ICON))
        {
            struct uwp_element *sym_el = uwp_elem_from_any( *(void **)inner );
            UINT_PTR code = (UINT_PTR)outer;
            if (sym_el && code >= 0xE000 && code <= 0xFFFF)
            {
                WCHAR sym = (WCHAR)code;
                uwp_elem_set_text_n( sym_el, &sym, 1 );
                uwp_elem_set_font_family( sym_el, L"Segoe MDL2 Assets" );
            }
        }
        return hr;
    }
    /* If called as I*Statics::get_*Property(iface, IDependencyProperty **out)
     * (e.g. IPopupStatics on Controls.Primitives.Popup routed to the
     * ComposableFactory vtbl), `outer` is the out-pointer. */
    if ((ULONG_PTR)outer >= 0x10000 && *(void **)outer == NULL)
        return dependency_property_create( NULL, (IDependencyProperty **)outer );
    if ((ULONG_PTR)outer < 0x10000) outer = NULL;
    TRACE( "uwp_cf_CreateInstance(%s, outer=%p, inner=%p, inst=%p)\n", debugstr_w(cls), outer, inner, instance );
    return uwp_element_create( cls, outer, inner, instance );
}

static HRESULT WINAPI uwp_cf_dp_prop( void *iface, void **out )
{
    if (!out || (ULONG_PTR)out < 0x10000) return S_OK;
    return dependency_property_create( NULL, (IDependencyProperty **)out );
}

static HRESULT uwp_cf_routed_event_slot( IDependencyProperty **slot, void **out )
{
    if (!out || (ULONG_PTR)out < 0x10000) return S_OK;
    if (!*slot)
        uwp_element_create( L"Windows.UI.Xaml.RoutedEvent", NULL, NULL, (void **)slot );
    *out = *slot;
    if (*out) IUnknown_AddRef( (IUnknown *)*out );
    return S_OK;
}

static HRESULT WINAPI uwp_cf_dp_prop_slot6( void *iface, void **out )
{
    struct uwp_class_factory *f = uwp_cf_from_any( iface );
    const WCHAR *dp_name = NULL;
    HRESULT hr;
    if (!out || (ULONG_PTR)out < 0x10000) return S_OK;
    if (f)
    {
        if (f->factory_flags & UWP_FF_UIELEMENT)
            return uwp_cf_routed_event_slot( &g_routed_event_other, out );
        if (f->factory_flags & UWP_FF_BUTTON)
            dp_name = L"Flyout";
        else if (f->factory_flags & UWP_FF_TOGGLE_BTN_OR_MENU)
            dp_name = L"IsChecked";
    }
    hr = dependency_property_create( NULL, (IDependencyProperty **)out );
    if (SUCCEEDED(hr) && *out && dp_name)
    {
        struct { IDependencyProperty iface; LONG ref; void *meta; WCHAR name[64]; } *dpl = *out;
        lstrcpynW( dpl->name, dp_name, ARRAY_SIZE(dpl->name) );
    }
    return hr;
}

static HRESULT WINAPI uwp_cf_routed_event_other( void *iface, void **out )
{
    return uwp_cf_routed_event_slot( &g_routed_event_other, out );
}

static HRESULT WINAPI uwp_cf_routed_event_ptr_pressed( void *iface, void **out )
{
    return uwp_cf_routed_event_slot( &g_routed_event_ptr_pressed, out );
}

static HRESULT WINAPI uwp_cf_routed_event_ptr_released( void *iface, void **out )
{
    return uwp_cf_routed_event_slot( &g_routed_event_ptr_released, out );
}

static HRESULT WINAPI uwp_cf_routed_event_tapped( void *iface, void **out )
{
    return uwp_cf_routed_event_slot( &g_routed_event_tapped, out );
}

static const GUID IID_IUIElementStatics4_UWP =
    { 0x5d1157a0, 0x0588, 0x4b15, { 0x89, 0x5c, 0x76, 0x99, 0x5c, 0x45, 0x8d, 0x83 } };
static const GUID IID_IUIElementStatics6_UWP =
    { 0x62809849, 0x2689, 0x4266, { 0xba, 0x3b, 0x3c, 0x5e, 0x86, 0xc3, 0xa8, 0x26 } };
static const GUID IID_IUIElementStatics7_UWP =
    { 0x933e9348, 0xb1e5, 0x4005, { 0x8d, 0x3a, 0x29, 0xba, 0xb9, 0xe7, 0xb9, 0x73 } };
static const GUID IID_IUIElementStatics8_UWP =
    { 0xa6890878, 0x664e, 0x4679, { 0x9c, 0x6b, 0x96, 0x67, 0x25, 0x16, 0x2f, 0x59 } };

static const void *uwp_uielement_routed_statics_vtbl[32] =
{
    [0] = uwp_cf_QI,
    [1] = uwp_cf_AddRef,
    [2] = uwp_cf_Release,
    [3] = uwp_cf_GetIids,
    [4] = uwp_cf_GetRuntimeClassName,
    [5] = uwp_cf_GetTrustLevel,
    [6 ... 31] = uwp_cf_routed_event_other,
};
static const void *uwp_uielement_routed_statics_obj = uwp_uielement_routed_statics_vtbl;

static HRESULT WINAPI uwp_grid_statics_SetProp( void *iface, void *elem, INT32 val )
{
    return S_OK;
}
static HRESULT WINAPI uwp_grid_statics_GetProp( void *iface, void *elem, INT32 *out )
{
    if (out) *out = 0;
    return S_OK;
}
static const void *uwp_grid_statics_vtbl[24] =
{
    [0] = uwp_cf_QI,
    [1] = uwp_cf_AddRef,
    [2] = uwp_cf_Release,
    [3] = uwp_cf_GetIids,
    [4] = uwp_cf_GetRuntimeClassName,
    [5] = uwp_cf_GetTrustLevel,
    [6] = uwp_cf_dp_prop,             /* RowProperty */
    [7] = uwp_grid_statics_GetProp,   /* GetRow */
    [8] = uwp_grid_statics_SetProp,   /* SetRow */
    [9] = uwp_cf_dp_prop,             /* ColumnProperty */
    [10] = uwp_grid_statics_GetProp,  /* GetColumn */
    [11] = uwp_grid_statics_SetProp,  /* SetColumn */
    [12] = uwp_cf_dp_prop,            /* RowSpanProperty */
    [13] = uwp_grid_statics_GetProp,  /* GetRowSpan */
    [14] = uwp_grid_statics_SetProp,  /* SetRowSpan */
    [15] = uwp_cf_dp_prop,            /* ColumnSpanProperty */
    [16] = uwp_grid_statics_GetProp,  /* GetColumnSpan */
    [17] = uwp_grid_statics_SetProp,  /* SetColumnSpan */
    [18 ... 23] = uwp_generic_noop,
};
static const void *uwp_grid_statics_obj = uwp_grid_statics_vtbl;

static const void *uwp_uielement_statics_vtbl[64] =
{
    [0] = uwp_cf_QI,
    [1] = uwp_cf_AddRef,
    [2] = uwp_cf_Release,
    [3] = uwp_cf_GetIids,
    [4] = uwp_cf_GetRuntimeClassName,
    [5] = uwp_cf_GetTrustLevel,
    [6 ... 8] = uwp_cf_routed_event_other,
    [9] = uwp_cf_routed_event_ptr_pressed,
    [10] = uwp_cf_routed_event_other,
    [11] = uwp_cf_routed_event_ptr_released,
    [12 ... 15] = uwp_cf_routed_event_other,
    [16] = uwp_cf_routed_event_tapped,
    [17 ... 24] = uwp_cf_routed_event_other,
    [25 ... 63] = uwp_cf_dp_prop,
};
static const void *uwp_uielement_statics_obj = uwp_uielement_statics_vtbl;

static HRESULT WINAPI uwp_cf_dp_prop_slot9( void *iface, void **out )
{
    struct uwp_class_factory *f = uwp_cf_from_any( iface );
    if (f && (f->factory_flags & UWP_FF_UIELEMENT))
    {
        if (!out) return E_POINTER;
        if (!g_routed_event_ptr_pressed)
            uwp_element_create( L"Windows.UI.Xaml.RoutedEvent", NULL, NULL, (void **)&g_routed_event_ptr_pressed );
        *out = g_routed_event_ptr_pressed;
        if (*out) IUnknown_AddRef( (IUnknown *)*out );
        return S_OK;
    }
    if (!out || (ULONG_PTR)out < 0x10000) return S_OK;
    return dependency_property_create( NULL, (IDependencyProperty **)out );
}

const void *uwp_cf_act_vtbl[16] =
{
    [0] = uwp_cf_QI,
    [1] = uwp_cf_AddRef,
    [2] = uwp_cf_Release,
    [3] = uwp_cf_GetIids,
    [4] = uwp_cf_GetRuntimeClassName,
    [5] = uwp_cf_GetTrustLevel,
    [6] = uwp_cf_ActivateInstance,
    [7 ... 15] = uwp_cf_dp_prop,
};

const void *uwp_cf_comp_vtbl[64] =
{
    [0] = uwp_cf_QI,
    [1] = uwp_cf_AddRef,
    [2] = uwp_cf_Release,
    [3] = uwp_cf_GetIids,
    [4] = uwp_cf_GetRuntimeClassName,
    [5] = uwp_cf_GetTrustLevel,
    [6] = uwp_cf_CreateInstance,
    [7] = uwp_cf_CreateInstance,
    [8] = uwp_cf_CreateInstance,
    [9] = uwp_cf_CreateInstance,
    [10 ... 63] = uwp_generic_noop,
};

static HRESULT WINAPI comp_target_GetCompositorForCurrentThread( void *iface, void **out )
{
    __x_ABI_CWindows_CUI_CComposition_CICompositor *comp;
    if (!out) return E_POINTER;
    *out = NULL;
    comp = ecp_get_compositor();
    TRACE( "comp_target_GetCompositorForCurrentThread -> %p\n", comp );
    if (!comp) return E_FAIL;
    __x_ABI_CWindows_CUI_CComposition_CICompositor_AddRef( comp );
    *out = comp;
    return S_OK;
}

static HRESULT WINAPI comp_target_QI( void *iface, REFIID iid, void **out )
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
    TRACE( "comp_target_QI iid=%s\n", debugstr_guid( iid ) );
    *out = (void *)&comp_target_statics_obj;
    return S_OK;
}

static const GUID IID_IRenderingEventArgs_UWP =
    { 0x58b55d6d, 0x50a6, 0x435e, { 0x96, 0x9c, 0xf0, 0xe8, 0x18, 0xa9, 0x6e, 0x35 } };

static HRESULT WINAPI rendering_args_QI( void *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IRenderingEventArgs_UWP ))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static HRESULT WINAPI rendering_args_get_RenderingTime( void *iface, INT64 *val )
{
    if (val) *val = (INT64)GetTickCount64() * 10000;
    return S_OK;
}
static const void *rendering_args_vtbl[] =
{
    rendering_args_QI, ecp_AddRef, ecp_Release, ecp_GetIids, ecp_GetRuntimeClassName, ecp_GetTrustLevel,
    rendering_args_get_RenderingTime,
};
static const void *rendering_args_obj = rendering_args_vtbl;

struct ct_rendering_entry { IInspectable *handler; INT64 token; };
static struct ct_rendering_entry *g_ct_rendering = NULL;
static int g_ct_rendering_cnt = 0;
static int g_ct_rendering_cap = 0;
static LONG g_ct_rendering_next_tok = 1;
static BOOL g_ct_in_rendering = FALSE;
static SRWLOCK g_ct_rendering_lock = SRWLOCK_INIT;

static HRESULT WINAPI comp_target_add_Rendering( void *iface, void *handler, EventRegistrationToken *tok )
{
    INT64 t = InterlockedIncrement( &g_ct_rendering_next_tok );
    if (tok && (ULONG_PTR)tok > 0x10000 && !IsBadWritePtr( tok, sizeof(*tok) ))
        tok->value = t;
    if (handler)
    {
        AcquireSRWLockExclusive( &g_ct_rendering_lock );
        if (xaml_vec_grow( (void **)&g_ct_rendering, &g_ct_rendering_cap,
                           g_ct_rendering_cnt + 1, sizeof(*g_ct_rendering), 16 ))
        {
            TRACE( "CompositionTarget::add_Rendering handler=%p cnt=%d\n", handler, g_ct_rendering_cnt + 1 );
            g_ct_rendering[g_ct_rendering_cnt].handler = (IInspectable *)handler;
            g_ct_rendering[g_ct_rendering_cnt].token = t;
            g_ct_rendering_cnt++;
            IInspectable_AddRef( (IInspectable *)handler );
            ReleaseSRWLockExclusive( &g_ct_rendering_lock );
            uwp_invalidate_window();
            return S_OK;
        }
        ReleaseSRWLockExclusive( &g_ct_rendering_lock );
    }
    return S_OK;
}

static HRESULT WINAPI comp_target_remove_Rendering( void *iface, EventRegistrationToken tok )
{
    int i;
    IInspectable *h = NULL;
    AcquireSRWLockExclusive( &g_ct_rendering_lock );
    for (i = 0; i < g_ct_rendering_cnt; i++)
    {
        if (g_ct_rendering[i].token == tok.value)
        {
            h = g_ct_rendering[i].handler;
            g_ct_rendering[i] = g_ct_rendering[--g_ct_rendering_cnt];
            break;
        }
    }
    ReleaseSRWLockExclusive( &g_ct_rendering_lock );
    if (h) IInspectable_Release( h );
    return S_OK;
}

void uwp_fire_composition_target_rendering( void )
{
    IInspectable **snap = NULL;
    int n = 0, i;
    if (g_ct_in_rendering) return;
    AcquireSRWLockShared( &g_ct_rendering_lock );
    if (g_ct_rendering_cnt <= 0)
    {
        ReleaseSRWLockShared( &g_ct_rendering_lock );
        return;
    }
    snap = malloc( (size_t)g_ct_rendering_cnt * sizeof(*snap) );
    if (!snap)
    {
        ReleaseSRWLockShared( &g_ct_rendering_lock );
        return;
    }
    for (i = 0; i < g_ct_rendering_cnt; i++)
    {
        if (g_ct_rendering[i].handler)
        {
            snap[n++] = g_ct_rendering[i].handler;
            IInspectable_AddRef( g_ct_rendering[i].handler );
        }
    }
    ReleaseSRWLockShared( &g_ct_rendering_lock );
    g_ct_in_rendering = TRUE;
    for (i = 0; i < n; i++)
    {
        struct xaml_event_handler *h = (struct xaml_event_handler *)snap[i];
        __TRY
        {
            h->lpVtbl->Invoke( h, NULL, (IInspectable *)&rendering_args_obj );
        }
        __EXCEPT_ALL
        {
            WARN( "%s: exception 0x%08lx\n", __func__, (unsigned long)GetExceptionCode() );
        }
        __ENDTRY;
        IInspectable_Release( snap[i] );
    }
    free( snap );
    g_ct_in_rendering = FALSE;
}

static const void *comp_target_statics_vtbl[16] =
{
    [0] = comp_target_QI,
    [1] = ecp_AddRef,
    [2] = ecp_Release,
    [3] = ecp_GetIids,
    [4] = ecp_GetRuntimeClassName,
    [5] = ecp_GetTrustLevel,
    [6] = comp_target_add_Rendering,
    [7] = comp_target_remove_Rendering,
    [8] = uwp_ui_event_add,
    [9] = uwp_generic_noop,
    [10] = uwp_ui_event_add,
    [11] = uwp_generic_noop,
    [12] = comp_target_GetCompositorForCurrentThread,
    [13 ... 15] = uwp_generic_noop,
};
static const void *comp_target_statics_obj = comp_target_statics_vtbl;

IActivationFactory *xaml_get_comp_target_factory( void )
{
    return (IActivationFactory *)&comp_target_statics_obj;
}

/* Microsoft.UI.Content.ContentExternalOutputLink bridge -> PlacementVisual + IDCompositionTarget */
struct xaml_ceol
{
    const void *link_vtbl;
    const void *target_vtbl;
    LONG ref;
    __x_ABI_CWindows_CUI_CComposition_CIContainerVisual *placement_visual;
    IDCompositionVisual *placement_dcomp_vis;
};

static inline struct xaml_ceol *ceol_from_link( void *iface )
{
    return CONTAINING_RECORD( iface, struct xaml_ceol, link_vtbl );
}
static inline struct xaml_ceol *ceol_from_target( void *iface )
{
    return CONTAINING_RECORD( iface, struct xaml_ceol, target_vtbl );
}

static const GUID IID_IDCompositionVisual_local =
    { 0x4d93059d, 0x097b, 0x4651, { 0x9a, 0x60, 0xf0, 0xf2, 0x51, 0x16, 0xe2, 0xf3 } };

static HRESULT WINAPI ceol_QueryInterface( void *iface, REFIID iid, void **out )
{
    static const GUID IID_IDCompositionTarget_local =
        { 0xeacdd04c, 0x117e, 0x4e17, { 0x88, 0xf4, 0xd1, 0xb1, 0x2b, 0x0e, 0x3d, 0x89 } };
    static const GUID IID_IDCompositionVisual2_local =
        { 0xe8de1639, 0x4331, 0x4b26, { 0xbc, 0x5f, 0x6a, 0x32, 0x1d, 0x34, 0x7a, 0x85 } };
    static const GUID IID_IContentExternalOutputLink_WinUI3 =
        { 0x1054bf83, 0xb35b, 0x5fde, { 0x8d, 0xd7, 0xac, 0x3b, 0xb3, 0xe6, 0xce, 0x27 } };
    struct xaml_ceol *impl = ceol_from_link( iface );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IContentExternalOutputLink_WinUI3 ))
    {
        InterlockedIncrement( &impl->ref );
        *out = &impl->link_vtbl;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IDCompositionTarget_local ))
    {
        InterlockedIncrement( &impl->ref );
        *out = &impl->target_vtbl;
        TRACE( "ceol_QueryInterface -> IDCompositionTarget %p (dcomp_vis=%p)\n", *out, impl->placement_dcomp_vis );
        return S_OK;
    }
    if ((IsEqualGUID( iid, &IID_IDCompositionVisual_local ) ||
         IsEqualGUID( iid, &IID_IDCompositionVisual2_local )) && impl->placement_dcomp_vis)
    {
        HRESULT hr = IDCompositionVisual_QueryInterface( impl->placement_dcomp_vis, iid, out );
        TRACE( "ceol_QueryInterface -> IDCompositionVisual %p hr=%#lx\n", *out, hr );
        return hr;
    }
    TRACE( "ceol_QueryInterface unsupported iid %s\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI ceol_AddRef( void *iface )
{
    return InterlockedIncrement( &ceol_from_link( iface )->ref );
}
static ULONG WINAPI ceol_Release( void *iface )
{
    struct xaml_ceol *impl = ceol_from_link( iface );
    ULONG r = InterlockedDecrement( &impl->ref );
    if (!r)
    {
        if (impl->placement_dcomp_vis) IDCompositionVisual_Release( impl->placement_dcomp_vis );
        if (impl->placement_visual) __x_ABI_CWindows_CUI_CComposition_CIContainerVisual_Release( impl->placement_visual );
        free( impl );
    }
    return r;
}
static HRESULT WINAPI ceol_get_PlacementVisual( void *iface, void **out )
{
    struct xaml_ceol *impl = ceol_from_link( iface );
    if (!out) return E_POINTER;
    *out = impl->placement_visual;
    if (impl->placement_visual)
        __x_ABI_CWindows_CUI_CComposition_CIContainerVisual_AddRef( impl->placement_visual );
    TRACE( "ceol_get_PlacementVisual -> %p\n", *out );
    return S_OK;
}
static HRESULT WINAPI ceol_target_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct xaml_ceol *impl = ceol_from_target( iface );
    return ceol_QueryInterface( &impl->link_vtbl, iid, out );
}
static ULONG WINAPI ceol_target_AddRef( void *iface )
{
    return InterlockedIncrement( &ceol_from_target( iface )->ref );
}
static ULONG WINAPI ceol_target_Release( void *iface )
{
    struct xaml_ceol *impl = ceol_from_target( iface );
    return ceol_Release( &impl->link_vtbl );
}
static HRESULT WINAPI ceol_target_SetRoot( void *iface, IDCompositionVisual *visual )
{
    struct xaml_ceol *impl = ceol_from_target( iface );
    HRESULT hr = S_OK;
    TRACE( "ceol_target_SetRoot(impl=%p, visual=%p, placement_dcomp_vis=%p)\n", impl, visual, impl->placement_dcomp_vis );
    if (impl->placement_dcomp_vis)
    {
        IDCompositionVisual_RemoveAllVisuals( impl->placement_dcomp_vis );
        if (visual)
            hr = IDCompositionVisual_AddVisual( impl->placement_dcomp_vis, visual, TRUE, NULL );
        xaml_dcomp_commit();
    }
    return hr;
}

static const void *ceol_link_vtbl[24] =
{
    [0] = ceol_QueryInterface,
    [1] = ceol_AddRef,
    [2] = ceol_Release,
    [3] = ecp_GetIids,
    [4] = ecp_GetRuntimeClassName,
    [5] = ecp_GetTrustLevel,
    [6 ... 12] = uwp_generic_noop,
    [13] = ceol_get_PlacementVisual,
    [14 ... 23] = uwp_generic_noop,
};
static const void *ceol_target_vtbl[4] =
{
    [0] = ceol_target_QueryInterface,
    [1] = ceol_target_AddRef,
    [2] = ceol_target_Release,
    [3] = ceol_target_SetRoot,
};

static HRESULT WINAPI ceol_statics_Create( void *iface, void *compositor_arg, void **out )
{
    __x_ABI_CWindows_CUI_CComposition_CICompositor *comp = ecp_get_compositor();
    struct xaml_ceol *impl;
    HRESULT hr;
    if (!out) return E_POINTER;
    *out = NULL;
    if (!comp) return E_FAIL;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->link_vtbl = ceol_link_vtbl;
    impl->target_vtbl = ceol_target_vtbl;
    impl->ref = 1;
    hr = __x_ABI_CWindows_CUI_CComposition_CICompositor_CreateContainerVisual( comp, &impl->placement_visual );
    if (FAILED(hr) || !impl->placement_visual)
    {
        free( impl );
        return FAILED(hr) ? hr : E_FAIL;
    }
    __x_ABI_CWindows_CUI_CComposition_CIContainerVisual_QueryInterface(
        impl->placement_visual, &IID_IDCompositionVisual_local, (void **)&impl->placement_dcomp_vis );
    TRACE( "ceol_statics_Create -> %p (placement_visual=%p, dcomp_vis=%p)\n",
         impl, impl->placement_visual, impl->placement_dcomp_vis );
    *out = &impl->link_vtbl;
    return S_OK;
}

static const void *ceol_statics_obj;
static HRESULT WINAPI ceol_statics_QI( void *iface, REFIID iid, void **out )
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
    *out = (void *)&ceol_statics_obj;
    return S_OK;
}
static const void *ceol_statics_vtbl[8] =
{
    [0] = ceol_statics_QI,
    [1] = ecp_AddRef,
    [2] = ecp_Release,
    [3] = ecp_GetIids,
    [4] = ecp_GetRuntimeClassName,
    [5] = ecp_GetTrustLevel,
    [6] = ceol_statics_Create,
    [7] = uwp_generic_noop,
};
static const void *ceol_statics_obj = ceol_statics_vtbl;

IActivationFactory *xaml_get_ceol_factory( void )
{
    return (IActivationFactory *)&ceol_statics_obj;
}

static HRESULT WINAPI binding_ops_SetBinding( void *iface, void *target, void *dp, void *binding )
{
    struct uwp_element *tel = uwp_elem_from_any( target );
    struct uwp_element *bel = uwp_elem_from_any( binding );
    TRACE( "binding_ops_SetBinding(target=%p, dp=%p, binding=%p)\n", target, dp, binding );
    uwp_apply_binding( tel, dp, bel );
    return S_OK;
}
static const void *binding_ops_statics_vtbl[8] =
{
    [0] = uwp_cf_QI,
    [1] = uwp_cf_AddRef,
    [2] = uwp_cf_Release,
    [3] = uwp_cf_GetIids,
    [4] = uwp_cf_GetRuntimeClassName,
    [5] = uwp_cf_GetTrustLevel,
    [6] = binding_ops_SetBinding,
    [7] = uwp_generic_noop,
};
static const void *binding_ops_statics_obj = binding_ops_statics_vtbl;

static HRESULT WINAPI thickness_helper_FromLengths( void *iface, double left, double top, double right, double bottom, double *out )
{
    if (!out) return E_POINTER;
    out[0] = left;
    out[1] = top;
    out[2] = right;
    out[3] = bottom;
    return S_OK;
}
static HRESULT WINAPI thickness_helper_FromUniformLength( void *iface, double uniform, double *out )
{
    if (!out) return E_POINTER;
    out[0] = out[1] = out[2] = out[3] = uniform;
    return S_OK;
}
static const void *thickness_helper_statics_vtbl[8] =
{
    [0] = uwp_cf_QI,
    [1] = uwp_cf_AddRef,
    [2] = uwp_cf_Release,
    [3] = uwp_cf_GetIids,
    [4] = uwp_cf_GetRuntimeClassName,
    [5] = uwp_cf_GetTrustLevel,
    [6] = thickness_helper_FromLengths,
    [7] = thickness_helper_FromUniformLength,
};
static const void *thickness_helper_statics_obj = thickness_helper_statics_vtbl;

static HRESULT WINAPI xaml_markup_helper_UnloadObject( void *iface, void *element )
{
    struct uwp_element *el = uwp_elem_from_any( element );
    if (el)
    {
        el->visibility = 1; /* Collapsed */
        el->has_bounds = FALSE;
        g_layout_dirty = TRUE;
        uwp_invalidate_window();
    }
    return S_OK;
}
static const void *xaml_markup_helper_statics_vtbl[16] =
{
    [0] = uwp_cf_QI,
    [1] = uwp_cf_AddRef,
    [2] = uwp_cf_Release,
    [3] = uwp_cf_GetIids,
    [4] = uwp_cf_GetRuntimeClassName,
    [5] = uwp_cf_GetTrustLevel,
    [6] = xaml_markup_helper_UnloadObject,
    [7 ... 15] = uwp_generic_noop,
};
static const void *xaml_markup_helper_statics_obj = xaml_markup_helper_statics_vtbl;

const void *uwp_cf_statics_vtbl[64] =
{
    [0] = uwp_cf_QI,
    [1] = uwp_cf_AddRef,
    [2] = uwp_cf_Release,
    [3] = uwp_cf_GetIids,
    [4] = uwp_cf_GetRuntimeClassName,
    [5] = uwp_cf_GetTrustLevel,
    [6] = uwp_cf_dp_prop_slot6,
    [7 ... 8] = uwp_generic_noop,
    [9] = uwp_cf_dp_prop_slot9,
    [10 ... 11] = uwp_generic_noop,
    [12] = uwp_cf_dp_prop,
    [13 ... 14] = uwp_generic_noop,
    [15] = uwp_cf_dp_prop,
    [16 ... 17] = uwp_generic_noop,
    [18] = uwp_cf_dp_prop,
    [19 ... 20] = uwp_generic_noop,
    [21] = uwp_cf_dp_prop,
    [22 ... 23] = uwp_generic_noop,
    [24] = uwp_cf_dp_prop,
    [25 ... 26] = uwp_generic_noop,
    [27] = uwp_cf_dp_prop,
    [28 ... 29] = uwp_generic_noop,
    [30] = uwp_cf_dp_prop,
    [31 ... 32] = uwp_generic_noop,
    [33] = uwp_cf_dp_prop,
    [34 ... 35] = uwp_generic_noop,
    [36] = uwp_cf_dp_prop,
    [37 ... 38] = uwp_generic_noop,
    [39] = uwp_cf_dp_prop,
    [40 ... 41] = uwp_generic_noop,
    [42] = uwp_cf_dp_prop,
    [43 ... 44] = uwp_generic_noop,
    [45] = uwp_cf_dp_prop,
    [46 ... 63] = uwp_generic_noop,
};

const void *uwp_dp_statics_vtbl[32] =
{
    [0] = uwp_cf_QI,
    [1] = uwp_cf_AddRef,
    [2] = uwp_cf_Release,
    [3] = uwp_cf_GetIids,
    [4] = uwp_cf_GetRuntimeClassName,
    [5] = uwp_cf_GetTrustLevel,
    [6] = uwp_cf_dp_prop_slot6,
    [7 ... 31] = uwp_cf_dp_prop,
};

/* VisualStateManagerStatics (slot 6 = GoToState(Control, HSTRING, bool, bool*), slot 7 = GetVisualStateGroups) */
static HRESULT WINAPI scb_factory_CreateInstanceWithColor( void *iface, Color color, void **out )
{
    HRESULT hr;
    if (!out) return E_POINTER;
    hr = uwp_element_create( L"Windows.UI.Xaml.Media.SolidColorBrush", NULL, NULL, out );
    if (SUCCEEDED(hr) && *out)
    {
        struct uwp_element *el = uwp_elem_from_any( *out );
        if (el)
        {
            struct uwp_style_anim_ext *sa = uwp_elem_ensure_style_anim( el );
            if (sa)
            {
                sa->brush_argb = ((UINT32)color.A << 24) | ((UINT32)color.R << 16) | ((UINT32)color.G << 8) | color.B;
                sa->has_brush_argb = TRUE;
                sa->brush_opacity = 1.0;
            }
        }
    }
    return hr;
}
const void *scb_factory_vtbl[16] =
{
    [0] = uwp_cf_QI,
    [1] = uwp_cf_AddRef,
    [2] = uwp_cf_Release,
    [3] = uwp_cf_GetIids,
    [4] = uwp_cf_GetRuntimeClassName,
    [5] = uwp_cf_GetTrustLevel,
    [6] = scb_factory_CreateInstanceWithColor,
    [7 ... 15] = uwp_generic_noop,
};
struct uwp_class_factory scb_factory_obj =
    { {(const struct IActivationFactoryVtbl *)uwp_cf_act_vtbl}, {(const struct IInspectableVtbl *)scb_factory_vtbl}, {(const struct IInspectableVtbl *)uwp_cf_statics_vtbl}, L"Windows.UI.Xaml.Media.SolidColorBrush", { NULL } };
