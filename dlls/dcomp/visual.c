/*
 * Copyright 2023 Zhiyi Zhang for CodeWeavers
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

#include <assert.h>
#include <stdarg.h>

#define COBJMACROS
#include "windef.h"
#include "winbase.h"
#include "dcomp_private.h"
#include "d3d11_4.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(dcomp);

static const GUID IID_IDCompositionVisualDebug_local =
        {0xfed2b808, 0x5eb4, 0x43a0, {0xae, 0xa3, 0x35, 0xf6, 0x52, 0x80, 0xf9, 0x1b}};
static const GUID IID_IDCompositionVisual3_local =
        {0x2775f462, 0xb6c1, 0x4015, {0xb0, 0xbe, 0xb3, 0xe7, 0xd6, 0xa4, 0x97, 0x6d}};

static HRESULT STDMETHODCALLTYPE visual_QueryInterface(IDCompositionVisualUnknown *iface, REFIID iid,
        void **out)
{
    struct composition_visual *visual = impl_from_IDCompositionVisualUnknown(iface);

    TRACE("iface %p, iid %s, out %p stub!\n", iface, debugstr_guid(iid), out);

    if (dcomp_is_equal_guid(iid, &IID_IUnknown)
            || dcomp_is_equal_guid(iid, &IID_IDCompositionVisual)
            || (visual->version >= 2 && dcomp_is_equal_guid(iid, &IID_IDCompositionVisual2))
            || dcomp_is_equal_guid(iid, &IID_IDCompositionVisualDebug_local)
            || dcomp_is_equal_guid(iid, &IID_IDCompositionVisual3_local)
            || dcomp_is_equal_guid(iid, &IID_IDCompositionVisualUnknown))
    {
        IUnknown_AddRef(&visual->IDCompositionVisualUnknown_iface);
        *out = &visual->IDCompositionVisualUnknown_iface;
        return S_OK;
    }

    FIXME("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid));
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE visual_AddRef(IDCompositionVisualUnknown *iface)
{
    struct composition_visual *visual = impl_from_IDCompositionVisualUnknown(iface);
    ULONG ref = InterlockedIncrement(&visual->ref);

    TRACE("iface %p, ref %lu.\n", iface, ref);
    return ref;
}

static ULONG STDMETHODCALLTYPE visual_Release(IDCompositionVisualUnknown *iface)
{
    struct composition_visual *visual = impl_from_IDCompositionVisualUnknown(iface);
    ULONG ref = InterlockedDecrement(&visual->ref);
    struct composition_visual *child;

    TRACE("iface %p, ref %lu.\n", iface, ref);

    if (!ref)
    {
        ID2D1GdiInteropRenderTarget *interop = visual->interop;
        ID2D1DeviceContext *device_context = visual->device_context;
        IDCompositionClip *clip = visual->clip;
        IDCompositionEffect *effect = visual->effect;
        IUnknown *content = visual->content;

        visual->interop = NULL;
        visual->device_context = NULL;
        visual->clip = NULL;
        visual->effect = NULL;
        visual->content = NULL;
        visual->wined3d_swapchain = NULL;

        dcomp_lock();
        if (visual->is_child)
            list_remove(&visual->entry);
        LIST_FOR_EACH_ENTRY(child, &visual->child_visuals, struct composition_visual, entry)
        {
            list_remove(&child->entry);
            child->is_child = FALSE;
            child->parent = NULL;
        }
        InterlockedIncrement(&dcomp_tree_version);
        dcomp_unlock();

        /* A composite snapshot holds a reference to this visual, so reaching zero
         * means no snapshot references these objects any more. Releasing them
         * enters Direct3D and GDI, so it runs outside the critical section. */
        if (interop)
            ID2D1GdiInteropRenderTarget_Release(interop);
        if (device_context)
            ID2D1DeviceContext_Release(device_context);
        if (clip)
            IDCompositionClip_Release(clip);
        if (effect)
            IDCompositionEffect_Release(effect);
        if (content)
            IUnknown_Release(content);
        if (visual->cached_copy_srv)
            ID3D11ShaderResourceView_Release(visual->cached_copy_srv);
        if (visual->cached_copy_tex)
            ID3D11Texture2D_Release(visual->cached_copy_tex);
        if (visual->cached_copy_device)
            ID3D11Device_Release(visual->cached_copy_device);
        if (visual->transform)
            IDCompositionTransform_Release(visual->transform);
        if (visual->shared_visual_handle)
            CloseHandle(visual->shared_visual_handle);
        if (visual->shared_content_handle)
            CloseHandle(visual->shared_content_handle);
        if (visual->zc_tex)
            ID3D11Texture2D_Release(visual->zc_tex);
        if (visual->shm_tex)
            ID3D11Texture2D_Release(visual->shm_tex);
        if (visual->shm_ptr)
            UnmapViewOfFile(visual->shm_ptr);
        if (visual->shm_map)
            CloseHandle(visual->shm_map);
        free(visual);
    }

    return ref;
}

static HRESULT STDMETHODCALLTYPE visual_SetOffsetXAnimation(IDCompositionVisualUnknown *iface,
        IDCompositionAnimation *animation)
{
    FIXME("iface %p, animation %p stub!\n", iface, animation);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE visual_SetOffsetX(IDCompositionVisualUnknown *iface, float offset_x)
{
    struct composition_visual *visual = impl_from_IDCompositionVisualUnknown(iface);

    TRACE("iface %p, offset_x %f.\n", iface, offset_x);

    dcomp_lock();
    if (visual->offset_x != offset_x)
    {
        visual->offset_x = offset_x;
        visual->transform_version++;
        InterlockedIncrement(&dcomp_transform_version);
    }
    dcomp_unlock();
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE visual_SetOffsetYAnimation(IDCompositionVisualUnknown *iface,
        IDCompositionAnimation *animation)
{
    FIXME("iface %p, animation %p stub!\n", iface, animation);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE visual_SetOffsetY(IDCompositionVisualUnknown *iface, float offset_y)
{
    struct composition_visual *visual = impl_from_IDCompositionVisualUnknown(iface);

    TRACE("iface %p, offset_y %f.\n", iface, offset_y);

    dcomp_lock();
    if (visual->offset_y != offset_y)
    {
        visual->offset_y = offset_y;
        visual->transform_version++;
        InterlockedIncrement(&dcomp_transform_version);
    }
    dcomp_unlock();
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE visual_SetTransformObject(IDCompositionVisualUnknown *iface,
        IDCompositionTransform *transform)
{
    struct composition_visual *visual = impl_from_IDCompositionVisualUnknown(iface);
    IDCompositionTransform *old;

    TRACE("iface %p, transform %p.\n", iface, transform);

    /* The transform is applied by the compositor at draw time; Wine keeps the
     * object so that later queries and the visual's teardown can use it. */
    if (transform) IDCompositionTransform_AddRef(transform);
    dcomp_lock();
    old = visual->transform;
    visual->transform = transform;
    visual->has_transform_matrix = FALSE;
    visual->transform_version++;
    InterlockedIncrement(&dcomp_transform_version);
    dcomp_unlock();
    if (old) IDCompositionTransform_Release(old);

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE visual_SetTransform(IDCompositionVisualUnknown *iface,
        const D2D_MATRIX_3X2_F *matrix)
{
    struct composition_visual *visual = impl_from_IDCompositionVisualUnknown(iface);
    IDCompositionTransform *old;

    TRACE("iface %p, matrix %p.\n", iface, matrix);

    if (!matrix)
        return E_INVALIDARG;

    dcomp_lock();
    old = visual->transform;
    visual->transform = NULL;
    visual->transform_matrix = *matrix;
    visual->has_transform_matrix = TRUE;
    visual->transform_version++;
    InterlockedIncrement(&dcomp_transform_version);
    dcomp_unlock();
    if (old) IDCompositionTransform_Release(old);

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE visual_SetTransformParent(IDCompositionVisualUnknown *iface,
        IDCompositionVisual *visual)
{
    FIXME("iface %p, visual %p stub!\n", iface, visual);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE visual_SetEffect(IDCompositionVisualUnknown *iface,
        IDCompositionEffect *effect)
{
    struct composition_visual *visual = impl_from_IDCompositionVisualUnknown(iface);
    IDCompositionEffect *old;

    TRACE("iface %p, effect %p.\n", iface, effect);

    if (effect) IDCompositionEffect_AddRef(effect);
    dcomp_lock();
    old = visual->effect;
    visual->effect = effect;
    visual->transform_version++;
    InterlockedIncrement(&dcomp_transform_version);
    dcomp_unlock();
    if (old) IDCompositionEffect_Release(old);

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE visual_SetBitmapInterpolationMode(IDCompositionVisualUnknown *iface,
        enum DCOMPOSITION_BITMAP_INTERPOLATION_MODE interpolation_mode)
{
    struct composition_visual *visual = impl_from_IDCompositionVisualUnknown(iface);

    TRACE("iface %p, interpolation_mode %d.\n", iface, interpolation_mode);

    if (interpolation_mode != DCOMPOSITION_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR
        && interpolation_mode != DCOMPOSITION_BITMAP_INTERPOLATION_MODE_LINEAR
        && interpolation_mode != DCOMPOSITION_BITMAP_INTERPOLATION_MODE_INHERIT)
        return E_INVALIDARG;

    visual->interpolation_mode = interpolation_mode;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE visual_SetBorderMode(IDCompositionVisualUnknown *iface,
        enum DCOMPOSITION_BORDER_MODE border_mode)
{
    struct composition_visual *visual = impl_from_IDCompositionVisualUnknown(iface);

    TRACE("iface %p, border_mode %d.\n", iface, border_mode);

    if (border_mode != DCOMPOSITION_BORDER_MODE_SOFT
        && border_mode != DCOMPOSITION_BORDER_MODE_HARD
        && border_mode != DCOMPOSITION_BORDER_MODE_INHERIT)
        return E_INVALIDARG;

    visual->border_mode = border_mode;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE visual_SetClipObject(IDCompositionVisualUnknown *iface,
        IDCompositionClip *clip)
{
    struct composition_visual *visual = impl_from_IDCompositionVisualUnknown(iface);
    IDCompositionClip *old;

    TRACE("DCOMP: visual_SetClipObject iface %p, clip %p.\n", iface, clip);

    /* A NULL clip object removes any current clip.  The Wine compositor does not
     * apply clip geometry to the window, so the clip object is stored and refcounted
     * (matching the real COM lifetime contract) and the call succeeds, which is what
     * Chromium's DCLayerTree::VisualTree::BuildTree requires (COM_CHECK on SetClipObject). */
    dcomp_lock();
    old = visual->clip;
    if (clip)
        IDCompositionClip_AddRef(clip);
    visual->clip = clip;
    visual->has_rect_clip = FALSE;
    visual->transform_version++;
    InterlockedIncrement(&dcomp_transform_version);
    dcomp_unlock();

    if (old)
        IDCompositionClip_Release(old);
    return S_OK;
}

/* C++ method: SetClip(THIS_ const D2D_RECT_F &rect).  Use pointer instead of reference */
static HRESULT STDMETHODCALLTYPE visual_SetClip(IDCompositionVisualUnknown *iface, const D2D_RECT_F *rect)
{
    struct composition_visual *visual = impl_from_IDCompositionVisualUnknown(iface);

    TRACE("DCOMP: visual_SetClip iface %p, rect %p.\n", iface, rect);

    if (!rect)
        return E_INVALIDARG;

    /* Store the rectangle clip; applied in the visual's coordinate space so
     * child offset animations stay clipped in parent space. */
    dcomp_lock();
    visual->rect_clip = *rect;
    visual->has_rect_clip = TRUE;
    if (visual->clip)
    {
        IDCompositionClip_Release(visual->clip);
        visual->clip = NULL;
    }
    visual->transform_version++;
    InterlockedIncrement(&dcomp_transform_version);
    dcomp_unlock();

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE visual_SetContent(IDCompositionVisualUnknown *iface, IUnknown *content)
{
    struct composition_visual *visual = impl_from_IDCompositionVisualUnknown(iface);
    struct wined3d_swapchain *wined3d_swapchain = NULL;
    ID2D1GdiInteropRenderTarget *interop = NULL;
    ID2D1DeviceContext *device_context = NULL;
    IDXGISwapChain *dxgi_swapchain = NULL;
    IDCompositionSurface *suface = NULL;
    IDXGISurface *dxgi_surface = NULL;
    ID3D11Texture2D *texture = NULL;
    IDXGIDevice *dxgi_device = NULL;
    ID2D1Device *d2d_device = NULL;
    ID2D1GdiInteropRenderTarget *old_interop;
    ID2D1DeviceContext *old_context;
    IUnknown *old_content;
    const GUID *iid = NULL;
    HRESULT hr;

    TRACE("DCOMP: visual_SetContent iface %p, content %p\n", iface, content);

    /* Identifying the content and building the Direct2D context below enters
     * Direct3D and Direct2D, so it runs with dcomp_cs released; only the pointer
     * swap is done under it. */
    if (content)
    {
        if (SUCCEEDED(IUnknown_QueryInterface(content, &IID_IDXGISwapChain,
                (void **)&dxgi_swapchain)) ||
            SUCCEEDED(IUnknown_QueryInterface(content, &IID_IDXGISwapChain1,
                (void **)&dxgi_swapchain)))
        {
            iid = &IID_IDXGISwapChain;
            wined3d_swapchain = dxgi_get_wined3d_swapchain(dxgi_swapchain);
            if (!wined3d_swapchain)
            {
                hr = IDXGISwapChain_GetDevice(dxgi_swapchain, &IID_IDXGIDevice, (void **)&dxgi_device);
                if (FAILED(hr))
                {
                    IDXGISwapChain_Release(dxgi_swapchain);
                    ERR("Failed to get the swapchain device, hr %#lx.\n", hr);
                    return hr;
                }
            }
            IDXGISwapChain_Release(dxgi_swapchain);
        }
        else if (SUCCEEDED(IUnknown_QueryInterface(content, &IID_IDCompositionSurface, (void **)&suface)))
        {
            struct composition_surface *dcomp_surface = unsafe_impl_from_IDCompositionSurface(suface);

            if (dcomp_surface && dcomp_surface->physical_surface &&
                SUCCEEDED(IUnknown_QueryInterface(dcomp_surface->physical_surface, &IID_IDXGISurface, (void **)&dxgi_surface)))
            {
                iid = &IID_IDCompositionSurface;

                hr = IDXGISurface_GetDevice(dxgi_surface, &IID_IDXGIDevice, (void **)&dxgi_device);
                IDXGISurface_Release(dxgi_surface);
                IDCompositionSurface_Release(suface);
                if (FAILED(hr))
                {
                    ERR("Failed to get the dxgi surface device, hr %#lx.\n", hr);
                    return hr;
                }
            }
            else if (dcomp_surface && dcomp_surface->draw_surface &&
                SUCCEEDED(ID3D11Texture2D_QueryInterface(dcomp_surface->draw_surface, &IID_IDXGISurface, (void **)&dxgi_surface)))
            {
                /* Surface drawn through BeginDraw(): the GDI-compatible Direct2D
                 * texture is the content. It is a wined3d texture, so the
                 * compositor samples it on the GPU, no CPU staging readback. */
                iid = &IID_IDCompositionSurface;

                hr = IDXGISurface_GetDevice(dxgi_surface, &IID_IDXGIDevice, (void **)&dxgi_device);
                IDXGISurface_Release(dxgi_surface);
                IDCompositionSurface_Release(suface);
                if (FAILED(hr))
                {
                    ERR("Failed to get the dxgi surface device, hr %#lx.\n", hr);
                    return hr;
                }
            }
            else
            {
                FIXME("Only IDCompositionSurface from IDXGISurface is currently supported.\n");
                IDCompositionSurface_Release(suface);
                return E_INVALIDARG;
            }
        }
        else if (SUCCEEDED(IUnknown_QueryInterface(content, &IID_IDXGISurface, (void **)&dxgi_surface)))
        {
            iid = &IID_IDXGISurface;
            hr = IDXGISurface_GetDevice(dxgi_surface, &IID_IDXGIDevice, (void **)&dxgi_device);
            IDXGISurface_Release(dxgi_surface);
            if (FAILED(hr))
            {
                ERR("Failed to get the dxgi surface device, hr %#lx.\n", hr);
                return hr;
            }
        }
        else if (SUCCEEDED(IUnknown_QueryInterface(content, &IID_ID3D11Texture2D, (void **)&texture)))
        {
            hr = ID3D11Texture2D_QueryInterface(texture, &IID_IDXGISurface, (void **)&dxgi_surface);
            ID3D11Texture2D_Release(texture);
            if (SUCCEEDED(hr))
            {
                iid = &IID_IDXGISurface;
                hr = IDXGISurface_GetDevice(dxgi_surface, &IID_IDXGIDevice, (void **)&dxgi_device);
                IDXGISurface_Release(dxgi_surface);
                if (FAILED(hr))
                {
                    ERR("Failed to get the dxgi surface device, hr %#lx.\n", hr);
                    return hr;
                }
            }
            else
            {
                ERR("Failed to query IDXGISurface from ID3D11Texture2D, hr %#lx.\n", hr);
                return hr;
            }
        }
        else
        {
            FIXME("Only IDXGISwapChain, IDCompositionSurface, or IDXGISurface are currently supported.\n");
            return E_INVALIDARG;
        }

        if (dxgi_device)
        {
            hr = D2D1CreateDevice(dxgi_device, NULL, &d2d_device);
            IDXGIDevice_Release(dxgi_device);
            if (FAILED(hr))
            {
                ERR("Failed to create a D2D device, hr %#lx.\n", hr);
                return hr;
            }

            hr = ID2D1Device_CreateDeviceContext(d2d_device, D2D1_DEVICE_CONTEXT_OPTIONS_NONE,
                    &device_context);
            ID2D1Device_Release(d2d_device);
            if (FAILED(hr))
            {
                ERR("Failed to create a D2D device context, hr %#lx.\n", hr);
                return hr;
            }

            if (FAILED(hr = ID2D1DeviceContext_QueryInterface(device_context,
                    &IID_ID2D1GdiInteropRenderTarget, (void **)&interop)))
            {
                ERR("Failed to get a ID2D1GdiInteropRenderTarget, hr %#lx.\n", hr);
                ID2D1DeviceContext_Release(device_context);
                return hr;
            }
        }
    }

    dcomp_lock();

    old_interop = visual->interop;
    old_context = visual->device_context;
    old_content = visual->content;
    visual->interop = NULL;
    visual->device_context = NULL;
    visual->content = NULL;
    visual->wined3d_swapchain = NULL;
    visual->shared_content_handle = NULL;

    if (content)
    {
        IUnknown_AddRef(content);
        visual->content = content;
        visual->content_iid = *iid;
        visual->wined3d_swapchain = wined3d_swapchain;
        visual->device_context = device_context;
        visual->interop = interop;
    }

    /* Mark the visual's content as changed so the compositor re-blits it to the window
     * exactly once (change/commit-driven), instead of re-copying it every refresh tick. */
    visual->content_version++;
    InterlockedIncrement(&dcomp_tree_version);

    dcomp_unlock();

    /* Releasing the replaced objects enters Direct3D and GDI. If a composite thread
     * is active and this visual is part of a visual tree, defer them to the reclaim list;
     * otherwise release immediately. */
    if (dcomp_has_active_composite_thread() && (visual->is_root || visual->is_child || visual->parent))
    {
        if (old_interop)
            dcomp_reclaim_push((IUnknown *)old_interop);
        if (old_context)
            dcomp_reclaim_push((IUnknown *)old_context);
        if (old_content)
            dcomp_reclaim_push(old_content);
    }
    else
    {
        if (old_interop)
            ID2D1GdiInteropRenderTarget_Release(old_interop);
        if (old_context)
            ID2D1DeviceContext_Release(old_context);
        if (old_content)
            IUnknown_Release(old_content);
    }

    return S_OK;
}

void visual_get_local_transform_3d(const struct composition_visual *visual, D3DMATRIX *out)
{
    struct dcomp_transform *t;
    D3DMATRIX m;

    dcomp_matrix_identity(out);
    if (!visual)
        return;

    if (visual->transform)
    {
        t = unsafe_impl_from_dcomp_transform((IUnknown *)visual->transform);
        if (t && t->kind != OBJECT_RECTANGLE_CLIP)
        {
            dcomp_transform_get_matrix_3d(t, &m);
            dcomp_matrix_multiply(out, out, &m);
        }
    }
    else if (visual->has_transform_matrix)
    {
        dcomp_matrix_from_2d(&m, &visual->transform_matrix);
        dcomp_matrix_multiply(out, out, &m);
    }

    if (visual->effect)
    {
        t = unsafe_impl_from_dcomp_transform((IUnknown *)visual->effect);
        if (t && t->kind != OBJECT_RECTANGLE_CLIP)
        {
            dcomp_transform_get_matrix_3d(t, &m);
            dcomp_matrix_multiply(out, out, &m);
        }
    }
}

void visual_get_transform_3d(const struct composition_visual *visual, UINT width, UINT height,
        const D3DMATRIX *world_transform, float vp_x, float vp_y, float vp_w, float vp_h, D3DMATRIX *out)
{
    D3DMATRIX ndc_to_px, local_m, px_to_clip;
    float w = width ? (float)width : 1.0f;
    float h = height ? (float)height : 1.0f;
    float vw = vp_w > 0.0f ? vp_w : w;
    float vh = vp_h > 0.0f ? vp_h : h;

    /* Quad vertex NDC (-1..1, -1..1) -> visual local pixels (0..w, 0..h). */
    dcomp_matrix_identity(&ndc_to_px);
    ndc_to_px.m[0][0] = 0.5f * w;
    ndc_to_px.m[1][1] = -0.5f * h;
    ndc_to_px.m[3][0] = 0.5f * w;
    ndc_to_px.m[3][1] = 0.5f * h;

    if (world_transform)
        local_m = *world_transform;
    else
        visual_get_local_transform_3d(visual, &local_m);

    /* Transformed pixel space [vp_x..vp_x+vw, vp_y..vp_y+vh] -> D3D11 clip space. */
    memset(&px_to_clip, 0, sizeof(px_to_clip));
    px_to_clip.m[0][0] = 2.0f / vw;
    px_to_clip.m[1][1] = -2.0f / vh;
    px_to_clip.m[3][0] = -(1.0f + 2.0f * vp_x / vw);
    px_to_clip.m[3][1] = 1.0f + 2.0f * vp_y / vh;
    px_to_clip.m[3][2] = 0.5f;
    px_to_clip.m[3][3] = 1.0f;

    dcomp_matrix_multiply(out, &ndc_to_px, &local_m);
    dcomp_matrix_multiply(out, out, &px_to_clip);
}

unsigned int visual_get_transform_version(const struct composition_visual *visual)
{
    struct dcomp_transform *t;
    unsigned int ver;

    if (!visual)
        return 0;

    ver = visual->transform_version;
    if (visual->transform)
    {
        t = unsafe_impl_from_dcomp_transform((IUnknown *)visual->transform);
        if (t) ver += dcomp_transform_get_version(t);
    }
    if (visual->effect)
    {
        t = unsafe_impl_from_dcomp_transform((IUnknown *)visual->effect);
        if (t) ver += dcomp_transform_get_version(t);
    }
    if (visual->clip)
    {
        t = unsafe_impl_from_dcomp_transform((IUnknown *)visual->clip);
        if (t) ver += dcomp_transform_get_version(t);
    }
    return ver;
}

void visual_tree_mark_dirty(struct composition_visual *visual)
{
    struct composition_visual *child;

    if (!visual)
        return;
    if (visual->content)
        visual->content_version++;
    LIST_FOR_EACH_ENTRY(child, &visual->child_visuals, struct composition_visual, entry)
        visual_tree_mark_dirty(child);
}

static HRESULT STDMETHODCALLTYPE visual_AddVisual(IDCompositionVisualUnknown *iface,
        IDCompositionVisual *visual, BOOL insert_above, IDCompositionVisual *reference_visual)
{
    struct composition_visual *parent_visual_impl = impl_from_IDCompositionVisualUnknown(iface);
    struct composition_visual *child_visual_impl, *ref_visual_impl = NULL;

    TRACE("DCOMP: visual_AddVisual parent %p, child %p, insert_above %d, ref %p\n",
        parent_visual_impl, visual, insert_above, reference_visual);

    if (!visual)
        return E_INVALIDARG;

    child_visual_impl = impl_from_IDCompositionVisual(visual);
    if (child_visual_impl->is_root || child_visual_impl->is_child)
        return E_INVALIDARG;

    dcomp_lock();
    if (reference_visual)
    {
        ref_visual_impl = impl_from_IDCompositionVisual(reference_visual);
        if (!(ref_visual_impl->is_child && ref_visual_impl->parent == parent_visual_impl))
        {
            dcomp_unlock();
            return E_INVALIDARG;
        }

        if (insert_above)
            list_add_after(&ref_visual_impl->entry, &child_visual_impl->entry);
        else
            list_add_before(&ref_visual_impl->entry, &child_visual_impl->entry);
    }
    else
    {
        if (insert_above)
            list_add_tail(&parent_visual_impl->child_visuals, &child_visual_impl->entry);
        else
            list_add_head(&parent_visual_impl->child_visuals, &child_visual_impl->entry);
    }

    child_visual_impl->is_child = TRUE;
    child_visual_impl->parent = parent_visual_impl;
    parent_visual_impl->content_version++;
    visual_tree_mark_dirty(child_visual_impl);
    InterlockedIncrement(&dcomp_tree_version);
    dcomp_unlock();
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE visual_RemoveVisual(IDCompositionVisualUnknown *iface,
        IDCompositionVisual *visual)
{
    struct composition_visual *parent_visual_impl = impl_from_IDCompositionVisualUnknown(iface);
    struct composition_visual *child_visual_impl;

    TRACE("iface %p, visual %p.\n", iface, visual);

    if (!visual)
        return E_INVALIDARG;

    child_visual_impl = impl_from_IDCompositionVisual(visual);
    dcomp_lock();
    if (!(child_visual_impl->is_child && child_visual_impl->parent == parent_visual_impl))
    {
        dcomp_unlock();
        return E_INVALIDARG;
    }

    list_remove(&child_visual_impl->entry);
    child_visual_impl->is_child = FALSE;
    child_visual_impl->parent = NULL;
    parent_visual_impl->content_version++;
    visual_tree_mark_dirty(child_visual_impl);
    InterlockedIncrement(&dcomp_tree_version);
    dcomp_unlock();
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE visual_RemoveAllVisuals(IDCompositionVisualUnknown *iface)
{
    struct composition_visual *parent_visual_impl = impl_from_IDCompositionVisualUnknown(iface);
    struct composition_visual *child_visual_impl;

    TRACE("iface %p.\n", iface);

    dcomp_lock();
    LIST_FOR_EACH_ENTRY(child_visual_impl, &parent_visual_impl->child_visuals, struct composition_visual, entry)
    {
        list_remove(&child_visual_impl->entry);
        child_visual_impl->is_child = FALSE;
        child_visual_impl->parent = NULL;
        visual_tree_mark_dirty(child_visual_impl);
    }
    parent_visual_impl->content_version++;
    InterlockedIncrement(&dcomp_tree_version);
    dcomp_unlock();
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE visual_SetCompositeMode(IDCompositionVisualUnknown *iface,
        enum DCOMPOSITION_COMPOSITE_MODE composite_mode)
{
    FIXME("iface %p, composite_mode %d stub!\n", iface, composite_mode);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE visual_SetOpacityMode(IDCompositionVisualUnknown *iface,
        enum DCOMPOSITION_OPACITY_MODE opacity_mode)
{
    FIXME("iface %p, opacity_mode %d stub!\n", iface, opacity_mode);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE visual_SetBackFaceVisibility(IDCompositionVisualUnknown *iface,
        enum DCOMPOSITION_BACKFACE_VISIBILITY visibility)
{
    struct composition_visual *visual = impl_from_IDCompositionVisualUnknown(iface);

    TRACE("iface %p, visibility %d.\n", iface, visibility);

    if (visibility != DCOMPOSITION_BACKFACE_VISIBILITY_VISIBLE
        && visibility != DCOMPOSITION_BACKFACE_VISIBILITY_HIDDEN
        && visibility != DCOMPOSITION_BACKFACE_VISIBILITY_INHERIT)
        return E_INVALIDARG;

    dcomp_lock();
    visual->visibility = visibility;
    InterlockedIncrement(&dcomp_tree_version);
    dcomp_unlock();
    return S_OK;
}

static HRESULT WINAPI visual_unknown_method1(IDCompositionVisualUnknown *iface)
{
    TRACE("iface %p stub.\n", iface);
    return S_OK;
}

static HRESULT WINAPI visual_unknown_method2(IDCompositionVisualUnknown *iface)
{
    TRACE("iface %p stub.\n", iface);
    return S_OK;
}

static HRESULT WINAPI visual_unknown_method3(IDCompositionVisualUnknown *iface)
{
    TRACE("iface %p stub.\n", iface);
    return S_OK;
}

static HRESULT WINAPI visual_unknown_method4(IDCompositionVisualUnknown *iface)
{
    TRACE("iface %p stub.\n", iface);
    return S_OK;
}

static HRESULT WINAPI visual_unknown_method5(IDCompositionVisualUnknown *iface)
{
    TRACE("iface %p stub.\n", iface);
    return S_OK;
}

static HRESULT WINAPI visual_unknown_method6(IDCompositionVisualUnknown *iface)
{
    TRACE("iface %p stub.\n", iface);
    return S_OK;
}

static HRESULT WINAPI visual_unknown_method7(IDCompositionVisualUnknown *iface)
{
    TRACE("iface %p stub.\n", iface);
    return S_OK;
}

static HRESULT WINAPI visual_unknown_method8(IDCompositionVisualUnknown *iface)
{
    TRACE("iface %p stub.\n", iface);
    return S_OK;
}

static HRESULT WINAPI visual_unknown_method9(IDCompositionVisualUnknown *iface,
        const D2D_MATRIX_4X4_F *matrix)
{
    D2D_MATRIX_3X2_F m;

    TRACE("iface %p, matrix %p.\n", iface, matrix);
    if (!matrix)
        return S_OK;

    m._11 = matrix->_11;
    m._12 = matrix->_12;
    m._21 = matrix->_21;
    m._22 = matrix->_22;
    m._31 = matrix->_41;
    m._32 = matrix->_42;
    return visual_SetTransform(iface, &m);
}

static HRESULT WINAPI visual_unknown_method10(IDCompositionVisualUnknown *iface,
        void *transform)
{
    TRACE("iface %p, transform %p.\n", iface, transform);
    return visual_SetTransformObject(iface, (IDCompositionTransform *)transform);
}

static HRESULT WINAPI visual_unknown_method11(IDCompositionVisualUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT WINAPI visual_unknown_method12(IDCompositionVisualUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT WINAPI visual_unknown_method13(IDCompositionVisualUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

/* parameter1 is from the second parameter of NtCreateCompositionInputSink() */
static HRESULT WINAPI visual_unknown_method14(IDCompositionVisualUnknown *iface, void *parameter1)
{
    FIXME("iface %p parameter1 %p stub!\n", iface, parameter1);
    return S_OK;
}

/*
 * Private IDCompositionVisualUnknown requests.  The Windows App SDK
 * composition-islands layer (Microsoft.UI.Windowing.Core.dll driving
 * Microsoft.UI.Input.dll, which hosts the Microsoft.UI.Content.ContentIsland,
 * ContentSite and DesktopChildSiteBridge classes) issues these on the site
 * visuals while it assembles the island visual tree, and treats a failed
 * request as "islands unavailable": it then drops the site bridge it was
 * building, the app-side title-bar bridge member stays null, and the following
 * layout pass dereferences it.  Wine has no compositor-side semantics for
 * these private slots, so a request on a live visual is accepted and reported
 * as satisfied; the value-carrying slots record the value on the trace channel.
 */
static HRESULT WINAPI visual_unknown_method15(IDCompositionVisualUnknown *iface)
{
    TRACE("iface %p.\n", iface);
    return S_OK;
}

static HRESULT WINAPI visual_unknown_method16(IDCompositionVisualUnknown *iface)
{
    TRACE("iface %p.\n", iface);
    return S_OK;
}

static HRESULT WINAPI visual_unknown_method17(IDCompositionVisualUnknown *iface)
{
    TRACE("iface %p.\n", iface);
    return S_OK;
}

static HRESULT WINAPI visual_unknown_method18(IDCompositionVisualUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT WINAPI visual_unknown_method19(IDCompositionVisualUnknown *iface, float parameter1)
{
    TRACE("iface %p, parameter1 %f.\n", iface, parameter1);
    return S_OK;
}

static HRESULT WINAPI visual_unknown_method20(IDCompositionVisualUnknown *iface, float parameter1)
{
    TRACE("iface %p, parameter1 %f.\n", iface, parameter1);
    return S_OK;
}

static HRESULT WINAPI visual_unknown_method21(IDCompositionVisualUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT WINAPI visual_unknown_method22(IDCompositionVisualUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT WINAPI visual_unknown_method23(IDCompositionVisualUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT WINAPI visual_unknown_method24(IDCompositionVisualUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT WINAPI visual_unknown_method25(IDCompositionVisualUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT WINAPI visual_unknown_method26(IDCompositionVisualUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT WINAPI visual_unknown_method27(IDCompositionVisualUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT WINAPI visual_unknown_method28(IDCompositionVisualUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT WINAPI visual_unknown_method29(IDCompositionVisualUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT WINAPI visual_unknown_method30(IDCompositionVisualUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT WINAPI visual_unknown_method31(IDCompositionVisualUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT WINAPI visual_unknown_method32(IDCompositionVisualUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT WINAPI visual_unknown_method33(IDCompositionVisualUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT WINAPI visual_unknown_method34(IDCompositionVisualUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT WINAPI visual_unknown_method35(IDCompositionVisualUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT WINAPI visual_unknown_method36(IDCompositionVisualUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT WINAPI visual_unknown_method37(IDCompositionVisualUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT WINAPI visual_unknown_method38(IDCompositionVisualUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT WINAPI visual_unknown_method39(IDCompositionVisualUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT WINAPI visual_unknown_method40(IDCompositionVisualUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT WINAPI visual_unknown_method41(IDCompositionVisualUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT WINAPI visual_unknown_method42(IDCompositionVisualUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT WINAPI visual_unknown_method43(IDCompositionVisualUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT WINAPI visual_unknown_method44(IDCompositionVisualUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT WINAPI visual_unknown_method45(IDCompositionVisualUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT WINAPI visual_unknown_method46(IDCompositionVisualUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT WINAPI visual_unknown_method47(IDCompositionVisualUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT WINAPI visual_unknown_method48(IDCompositionVisualUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT WINAPI visual_unknown_method49(IDCompositionVisualUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT WINAPI visual_unknown_method50(IDCompositionVisualUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT WINAPI visual_unknown_method51(IDCompositionVisualUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT WINAPI visual_unknown_method52(IDCompositionVisualUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT WINAPI visual_unknown_method53(IDCompositionVisualUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT WINAPI visual_unknown_method54(IDCompositionVisualUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT WINAPI visual_unknown_method55(IDCompositionVisualUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT WINAPI visual_unknown_method56(IDCompositionVisualUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT WINAPI visual_unknown_method57(IDCompositionVisualUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT WINAPI visual_unknown_method58(IDCompositionVisualUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT WINAPI visual_unknown_method59(IDCompositionVisualUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT WINAPI visual_unknown_method60(IDCompositionVisualUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT WINAPI visual_unknown_method61(IDCompositionVisualUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT WINAPI visual_unknown_method62(IDCompositionVisualUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT WINAPI visual_unknown_method63(IDCompositionVisualUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT WINAPI visual_unknown_method64(IDCompositionVisualUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT WINAPI visual_unknown_method65(IDCompositionVisualUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT WINAPI visual_unknown_method66(IDCompositionVisualUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT WINAPI visual_unknown_method67(IDCompositionVisualUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT WINAPI visual_unknown_method68(IDCompositionVisualUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT WINAPI visual_unknown_method69(IDCompositionVisualUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT WINAPI visual_unknown_method70(IDCompositionVisualUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT WINAPI visual_unknown_method71(IDCompositionVisualUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT WINAPI visual_unknown_method72(IDCompositionVisualUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT WINAPI visual_unknown_method73(IDCompositionVisualUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT WINAPI visual_unknown_method74(IDCompositionVisualUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT WINAPI visual_unknown_method75(IDCompositionVisualUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT WINAPI visual_unknown_method76(IDCompositionVisualUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT WINAPI visual_unknown_method77(IDCompositionVisualUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT WINAPI visual_unknown_method78(IDCompositionVisualUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT WINAPI visual_unknown_method79(IDCompositionVisualUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT WINAPI visual_unknown_method80(IDCompositionVisualUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT WINAPI visual_unknown_method81(IDCompositionVisualUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT WINAPI visual_unknown_method82(IDCompositionVisualUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT WINAPI visual_unknown_method83(IDCompositionVisualUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT WINAPI visual_unknown_method84(IDCompositionVisualUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT WINAPI visual_unknown_method85(IDCompositionVisualUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT WINAPI visual_unknown_method86(IDCompositionVisualUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT WINAPI visual_unknown_method87(IDCompositionVisualUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT WINAPI visual_unknown_method88(IDCompositionVisualUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT WINAPI visual_unknown_method89(IDCompositionVisualUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT WINAPI visual_unknown_method90(IDCompositionVisualUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT WINAPI visual_unknown_method91(IDCompositionVisualUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT WINAPI visual_unknown_method92(IDCompositionVisualUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT WINAPI visual_unknown_method93(IDCompositionVisualUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT WINAPI visual_unknown_method94(IDCompositionVisualUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT WINAPI visual_unknown_method95(IDCompositionVisualUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT WINAPI visual_unknown_method96(IDCompositionVisualUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT WINAPI visual_unknown_method97(IDCompositionVisualUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT WINAPI visual_unknown_method98(IDCompositionVisualUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT WINAPI visual_unknown_method99(IDCompositionVisualUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static const struct IDCompositionVisualUnknownVtbl visual_unknown_vtbl =
{
    /* IUnknown methods */
    visual_QueryInterface,
    visual_AddRef,
    visual_Release,
    /* IDCompositionVisual methods */
    visual_SetOffsetXAnimation,
    visual_SetOffsetX,
    visual_SetOffsetYAnimation,
    visual_SetOffsetY,
    visual_SetTransformObject,
    visual_SetTransform,
    visual_SetTransformParent,
    visual_SetEffect,
    visual_SetBitmapInterpolationMode,
    visual_SetBorderMode,
    visual_SetClipObject,
    visual_SetClip,
    visual_SetContent,
    visual_AddVisual,
    visual_RemoveVisual,
    visual_RemoveAllVisuals,
    visual_SetCompositeMode,
    /* IDCompositionVisual2 methods */
    visual_SetOpacityMode,
    visual_SetBackFaceVisibility,
    /* IDCompositionVisualUnknown methods */
    visual_unknown_method1,
    visual_unknown_method2,
    visual_unknown_method3,
    visual_unknown_method4,
    visual_unknown_method5,
    visual_unknown_method6,
    visual_unknown_method7,
    visual_unknown_method8,
    visual_unknown_method9,
    visual_unknown_method10,
    visual_unknown_method11,
    visual_unknown_method12,
    visual_unknown_method13,
    visual_unknown_method14,
    visual_unknown_method15,
    visual_unknown_method16,
    visual_unknown_method17,
    visual_unknown_method18,
    visual_unknown_method19,
    visual_unknown_method20,
    visual_unknown_method21,
    visual_unknown_method22,
    visual_unknown_method23,
    visual_unknown_method24,
    visual_unknown_method25,
    visual_unknown_method26,
    visual_unknown_method27,
    visual_unknown_method28,
    visual_unknown_method29,
    visual_unknown_method30,
    visual_unknown_method31,
    visual_unknown_method32,
    visual_unknown_method33,
    visual_unknown_method34,
    visual_unknown_method35,
    visual_unknown_method36,
    visual_unknown_method37,
    visual_unknown_method38,
    visual_unknown_method39,
    visual_unknown_method40,
    visual_unknown_method41,
    visual_unknown_method42,
    visual_unknown_method43,
    visual_unknown_method44,
    visual_unknown_method45,
    visual_unknown_method46,
    visual_unknown_method47,
    visual_unknown_method48,
    visual_unknown_method49,
    visual_unknown_method50,
    visual_unknown_method51,
    visual_unknown_method52,
    visual_unknown_method53,
    visual_unknown_method54,
    visual_unknown_method55,
    visual_unknown_method56,
    visual_unknown_method57,
    visual_unknown_method58,
    visual_unknown_method59,
    visual_unknown_method60,
    visual_unknown_method61,
    visual_unknown_method62,
    visual_unknown_method63,
    visual_unknown_method64,
    visual_unknown_method65,
    visual_unknown_method66,
    visual_unknown_method67,
    visual_unknown_method68,
    visual_unknown_method69,
    visual_unknown_method70,
    visual_unknown_method71,
    visual_unknown_method72,
    visual_unknown_method73,
    visual_unknown_method74,
    visual_unknown_method75,
    visual_unknown_method76,
    visual_unknown_method77,
    visual_unknown_method78,
    visual_unknown_method79,
    visual_unknown_method80,
    visual_unknown_method81,
    visual_unknown_method82,
    visual_unknown_method83,
    visual_unknown_method84,
    visual_unknown_method85,
    visual_unknown_method86,
    visual_unknown_method87,
    visual_unknown_method88,
    visual_unknown_method89,
    visual_unknown_method90,
    visual_unknown_method91,
    visual_unknown_method92,
    visual_unknown_method93,
    visual_unknown_method94,
    visual_unknown_method95,
    visual_unknown_method96,
    visual_unknown_method97,
    visual_unknown_method98,
    visual_unknown_method99,
};

/* Shared-resource identity of a visual's content.
 *
 * A shared visual handle travels to a consumer process (Chromium's GPU process
 * publishes, WhatsApp's dcomp consumes). The publisher's visual tree is a set
 * of pointers in the publisher's address space, so the only thing the consumer
 * can use is the GPU surface the content is rendered into, named by a shared
 * resource handle that the window server resolves for any process. The
 * consumer opens that handle on its own device and samples the texture on the
 * GPU: no CPU staging readback on either side.
 *
 * Content kinds a composition visual can carry are covered: IDCompositionSurface
 * (through its physical surface / draw surface), a swapchain (its current back
 * buffer), and a bare texture or DXGI surface. A visual with no GPU-backed
 * content publishes no handle, and the consumer composites the tree structure
 * alone, as before. */
static HRESULT content_get_shared_handle(struct composition_visual *visual, IUnknown *content, HANDLE *handle)
{
    IDCompositionSurface *surface = NULL;
    IDXGISwapChain *swapchain = NULL;
    IDXGIResource *dxgi_resource = NULL;
    ID3D11Texture2D *texture = NULL;
    IUnknown *physical = NULL;
    HRESULT hr;

    *handle = NULL;

    if (SUCCEEDED(IUnknown_QueryInterface(content, &IID_IDCompositionSurface, (void **)&surface)))
    {
        struct composition_surface *surface_impl = unsafe_impl_from_IDCompositionSurface(surface);

        if (surface_impl && surface_impl->source_hwnd)
        {
            HANDLE hwnd_surf = GetPropW(surface_impl->source_hwnd, L"__wine_dcomp_hwnd_surface");
            IDCompositionSurface_Release(surface);
            if (hwnd_surf)
            {
                *handle = hwnd_surf;
                return S_OK;
            }
            return E_FAIL;
        }
        if (surface_impl && surface_impl->physical_surface)
        {
            physical = surface_impl->physical_surface;
            IUnknown_AddRef(physical);
        }
        if (!physical && surface_impl && surface_impl->draw_surface)
            ID3D11Texture2D_QueryInterface(surface_impl->draw_surface, &IID_IUnknown, (void **)&physical);
        IDCompositionSurface_Release(surface);
        hr = physical ? IUnknown_QueryInterface(physical, &IID_IDXGIResource, (void **)&dxgi_resource) : E_FAIL;
    }
    else if (SUCCEEDED(IUnknown_QueryInterface(content, &IID_IDXGISwapChain, (void **)&swapchain)))
    {
        /* Cross-process consumers open Local\wine_dcomp_shm_%08x keyed on buffer 0's
         * KMT handle, which dxgi/swapchain.c publishes atomically inside Present().
         * Calling GetSharedHandle on buffer 0 both activates that SHM publishing and
         * returns the matching handle. In-process compositing still uses
         * visual_snapshot_swapchain_content() directly in device.c. */
        hr = IDXGISwapChain_GetBuffer(swapchain, 0, &IID_ID3D11Texture2D, (void **)&texture);
        if (SUCCEEDED(hr))
        {
            hr = ID3D11Texture2D_QueryInterface(texture, &IID_IDXGIResource, (void **)&dxgi_resource);
            ID3D11Texture2D_Release(texture);
        }
        IDXGISwapChain_Release(swapchain);
    }
    else
    {
        hr = IUnknown_QueryInterface(content, &IID_IDXGIResource, (void **)&dxgi_resource);
    }

    if (FAILED(hr))
        return hr;

    hr = IDXGIResource_GetSharedHandle(dxgi_resource, handle);
    IDXGIResource_Release(dxgi_resource);
    if (SUCCEEDED(hr) && !*handle)
        return E_FAIL; /* content is not shared yet */
    return hr;
}

static HRESULT content_get_size(IUnknown *content, UINT *width, UINT *height)
{
    IDCompositionSurface *surface = NULL;
    IDXGISwapChain *swapchain = NULL;
    DXGI_SWAP_CHAIN_DESC sc_desc;
    DXGI_SURFACE_DESC desc;
    ID3D11Texture2D *texture = NULL;
    IDXGISurface *dxgi_surface = NULL;
    D3D11_TEXTURE2D_DESC tex_desc;
    HRESULT hr;

    if (SUCCEEDED(IUnknown_QueryInterface(content, &IID_IDCompositionSurface, (void **)&surface)))
    {
        struct composition_surface *surface_impl = unsafe_impl_from_IDCompositionSurface(surface);

        if (surface_impl && surface_impl->source_hwnd)
        {
            ULONG_PTR sz = (ULONG_PTR)GetPropW(surface_impl->source_hwnd, L"__wine_dcomp_hwnd_size");
            if (sz)
            {
                IDCompositionSurface_Release(surface);
                *width = (UINT)(sz >> 16);
                *height = (UINT)(sz & 0xffff);
                return S_OK;
            }
        }
        IDCompositionSurface_Release(surface);
        if (surface_impl && surface_impl->width && surface_impl->height)
        {
            *width = surface_impl->width;
            *height = surface_impl->height;
            return S_OK;
        }
        return E_FAIL;
    }

    if (SUCCEEDED(IUnknown_QueryInterface(content, &IID_IDXGISurface, (void **)&dxgi_surface)))
    {
        hr = IDXGISurface_GetDesc(dxgi_surface, &desc);
        IDXGISurface_Release(dxgi_surface);
        if (SUCCEEDED(hr))
        {
            *width = desc.Width;
            *height = desc.Height;
            return S_OK;
        }
        return hr;
    }

    if (SUCCEEDED(IUnknown_QueryInterface(content, &IID_IDXGISwapChain, (void **)&swapchain)))
    {
        hr = IDXGISwapChain_GetDesc(swapchain, &sc_desc);
        IDXGISwapChain_Release(swapchain);
        if (SUCCEEDED(hr))
        {
            *width = sc_desc.BufferDesc.Width;
            *height = sc_desc.BufferDesc.Height;
            return S_OK;
        }
        return hr;
    }

    if (SUCCEEDED(IUnknown_QueryInterface(content, &IID_ID3D11Texture2D, (void **)&texture)))
    {
        ID3D11Texture2D_GetDesc(texture, &tex_desc);
        IUnknown_Release(texture);
        *width = tex_desc.Width;
        *height = tex_desc.Height;
        return S_OK;
    }

    return E_FAIL;
}

static void visual_find_best_shared_content(struct composition_visual *visual,
        struct composition_visual **best_vis, HANDLE *best_handle,
        UINT *best_w, UINT *best_h, ULONGLONG *best_area)
{
    struct composition_visual *child;
    HANDLE h = NULL;

    if (!visual)
        return;
    if (visual->content)
    {
        if (FAILED(content_get_shared_handle(visual, visual->content, &h)) || !h)
            h = visual->shared_content_handle;
        if (h)
        {
            UINT w = 0, ht = 0;
            ULONGLONG area;
            if (FAILED(content_get_size(visual->content, &w, &ht)))
            {
                w = (UINT)visual->size.cx;
                ht = (UINT)visual->size.cy;
            }
            area = (ULONGLONG)w * ht;
            if (!*best_vis || area >= *best_area)
            {
                *best_vis = visual;
                *best_handle = h;
                *best_w = w;
                *best_h = ht;
                *best_area = area;
            }
        }
    }
    LIST_FOR_EACH_ENTRY(child, &visual->child_visuals, struct composition_visual, entry)
        visual_find_best_shared_content(child, best_vis, best_handle, best_w, best_h, best_area);
}

/* Publish this visual's content to whoever consumes its shared visual handle.
 * Runs with dcomp_cs released: taking the shared handle enters Direct3D. The
 * handle is cached on the visual (a shared resource keeps its name for the
 * lifetime of the texture), so the per-frame republish is a memory read. */
void visual_publish_shared_content(struct composition_visual *visual, HANDLE *handle, UINT *width, UINT *height)
{
    struct composition_visual *cv = NULL;
    HANDLE shared_handle = NULL;
    UINT w = 0, h = 0;
    ULONGLONG area = 0;

    *handle = NULL;
    *width = 0;
    *height = 0;

    if (!visual)
        return;

    visual_find_best_shared_content(visual, &cv, &shared_handle, &w, &h, &area);
    if (!cv || !cv->content)
        return;

    if (shared_handle)
        cv->shared_content_handle = shared_handle;

    *handle = shared_handle;
    *width = w;
    *height = h;
}

HRESULT create_visual(int version, REFIID iid, void **new_visual)
{
    struct composition_visual *visual;
    HRESULT hr;

    if (!new_visual)
        return E_INVALIDARG;
    *new_visual = NULL;

    visual = calloc(1, sizeof(*visual));
    if (!visual)
        return E_OUTOFMEMORY;

    visual->IDCompositionVisualUnknown_iface.lpVtbl = &visual_unknown_vtbl;
    visual->version = version;
    visual->ref = 1;
    list_init(&visual->child_visuals);
    hr = IUnknown_QueryInterface(&visual->IDCompositionVisualUnknown_iface, iid, new_visual);
    IUnknown_Release(&visual->IDCompositionVisualUnknown_iface);
    return hr;
}

HRESULT create_visual_from_shared_visual_handle(HANDLE shared_visual_handle, void **new_visual)
{
    struct composition_visual *visual_impl;
    HANDLE handle = NULL;
    HRESULT hr;

    if (!new_visual)
        return E_POINTER;
    *new_visual = NULL;

    if (!shared_visual_handle)
        return E_INVALIDARG;

    /* The handle reaches the receiving process through the mojo handle transfer,
     * which DuplicateHandles it into this process's table; a self-duplication is
     * what validates it.  A handle that does not name an object in this process
     * is bogus: the raw value means nothing in the wineserver lookup, so report
     * it instead of binding a visual to it. */
    if (!DuplicateHandle(GetCurrentProcess(), shared_visual_handle, GetCurrentProcess(), &handle, 0,
                         FALSE, DUPLICATE_SAME_ACCESS))
        return E_INVALIDARG;

    hr = create_visual(2, &IID_IDCompositionVisual, (void **)new_visual);
    if (FAILED(hr))
    {
        if (handle != shared_visual_handle)
            CloseHandle(handle);
        return hr;
    }

    visual_impl = impl_from_IDCompositionVisualUnknown(*new_visual);
    visual_impl->shared_visual_handle = handle;
    return S_OK;
}
