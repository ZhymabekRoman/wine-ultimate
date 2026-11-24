/*
 * Copyright 2025 Zhiyi Zhang for CodeWeavers
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
#include <assert.h>
#include <stdlib.h>

#define COBJMACROS
#include "windef.h"
#include "winbase.h"
#include "dcomp_private.h"
#include "d3d11_4.h"
#include "wine/debug.h"

/* wine/heap.h and the winecrt0 heap helpers are not present in this tree;
 * use the same local shim as dlls/wintypes/uri.c */
#define heap_calloc(num,size) calloc( num, size )
#define heap_free(ptr) free( ptr )

WINE_DEFAULT_DEBUG_CHANNEL(dcomp);

static HRESULT STDMETHODCALLTYPE surface_QueryInterface(IDCompositionSurfaceUnknown *iface, REFIID iid, void **out)
{
    struct composition_surface *surface = impl_from_IDCompositionSurfaceUnknown(iface);

    TRACE("iface %p, iid %s, out %p!\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_IUnknown)
            || IsEqualGUID(iid, &IID_IDCompositionSurface)
            || IsEqualGUID(iid, &IID_IDCompositionSurfaceUnknown)
            || (surface->is_virtual && IsEqualGUID(iid, &IID_IDCompositionVirtualSurface)))
    {
        IUnknown_AddRef(iface);
        *out = iface;
        return S_OK;
    }

    /*
     * A composition surface that is backed by a physical surface exposes that
     * surface through the query interface, so that a caller which got the surface
     * from the compositor can reach the DXGI object underneath it (BeginDraw with
     * IID_ID2D1Surface and the IDXGISurface path both rely on this).
     */
    if (surface->physical_surface
            && (IsEqualGUID(iid, &IID_IDXGISurface) || IsEqualGUID(iid, &IID_IDXGISurface1)
                || IsEqualGUID(iid, &IID_IDXGISurface2)))
    {
        HRESULT hr = IUnknown_QueryInterface(surface->physical_surface, iid, out);

        if (SUCCEEDED(hr))
            return hr;
    }

    FIXME("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid));
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE surface_AddRef(IDCompositionSurfaceUnknown *iface)
{
    struct composition_surface *surface = impl_from_IDCompositionSurfaceUnknown(iface);
    ULONG ref = InterlockedIncrement(&surface->ref);

    TRACE("iface %p, ref %lu.\n", iface, ref);
    return ref;
}

extern struct composition_surface *g_last_rendered_surface;

static ULONG STDMETHODCALLTYPE surface_Release(IDCompositionSurfaceUnknown *iface)
{
    struct composition_surface *surface = impl_from_IDCompositionSurfaceUnknown(iface);
    ULONG ref = InterlockedDecrement(&surface->ref);

    TRACE("iface %p, ref %lu.\n", iface, ref);

    if (!ref)
    {
        dcomp_lock();
        if (g_last_rendered_surface == surface)
            g_last_rendered_surface = NULL;
        dcomp_unlock();
        if (surface->cached_srv)
            ID3D11ShaderResourceView_Release(surface->cached_srv);
        if (surface->cached_srv_device)
            ID3D11Device_Release(surface->cached_srv_device);
        if (surface->source_swapchain)
            IDXGISwapChain_Release(surface->source_swapchain);
        if (surface->physical_surface)
            IUnknown_Release(surface->physical_surface);
        if (surface->factory)
            IDCompositionSurfaceFactory_Release(surface->factory);
        if (surface->draw_surface)
            ID3D11Texture2D_Release(surface->draw_surface);
        free(surface->trim_rects);
        free(surface);
    }

    return ref;
}

static const struct IDCompositionSurfaceUnknownVtbl surface_vtbl;

HRESULT create_surface_from_dxgi_surface(struct composition_device *device, IDXGISurface *dxgi_surface, IUnknown **out_surface)
{
    struct composition_surface *surface;
    DXGI_SURFACE_DESC desc;

    if (!out_surface)
        return E_POINTER;
    *out_surface = NULL;

    if (!dxgi_surface)
        return E_INVALIDARG;

    if (FAILED(IDXGISurface_GetDesc(dxgi_surface, &desc)))
        return E_FAIL;

    if (!(surface = calloc(1, sizeof(*surface))))
        return E_OUTOFMEMORY;

    surface->IDCompositionSurfaceUnknown_iface.lpVtbl = &surface_vtbl;
    surface->physical_surface = (IUnknown *)dxgi_surface;
    IDXGISurface_AddRef(dxgi_surface);
    surface->physical_surface_iid = IID_IDXGISurface;
    {
        UINT sc_size = sizeof(surface->source_swapchain);
        if (FAILED(IDXGISurface_GetPrivateData(dxgi_surface, &IID_IDXGISwapChain,
                &sc_size, &surface->source_swapchain)))
            surface->source_swapchain = NULL;
    }
    surface->width = desc.Width;
    surface->height = desc.Height;
    surface->pixel_format = desc.Format;
    surface->alpha_mode = DXGI_ALPHA_MODE_PREMULTIPLIED;
    surface->ref = 1;

    *out_surface = (IUnknown *)&surface->IDCompositionSurfaceUnknown_iface;
    return S_OK;
}

struct composition_surface *g_last_rendered_surface = NULL;

static HRESULT factory_get_dxgi_device(struct composition_surface_factory *factory, IDXGIDevice **dxgi_device)
{
    if (IsEqualGUID(&factory->rendering_device_iid, &IID_IDXGIDevice))
    {
        *dxgi_device = (IDXGIDevice *)factory->rendering_device;
        IDXGIDevice_AddRef(*dxgi_device);
        return S_OK;
    }
    else if (IsEqualGUID(&factory->rendering_device_iid, &IID_ID2D1Device))
    {
        ID2D1Device2 *d2d2 = NULL;

        if (SUCCEEDED(IUnknown_QueryInterface(factory->rendering_device, &IID_ID2D1Device2, (void **)&d2d2)))
        {
            HRESULT hr = ID2D1Device2_GetDxgiDevice(d2d2, dxgi_device);
            ID2D1Device2_Release(d2d2);
            if (SUCCEEDED(hr) && *dxgi_device)
                return S_OK;
        }

        if (SUCCEEDED(IUnknown_QueryInterface(factory->rendering_device, &IID_IDXGIDevice, (void **)dxgi_device)))
            return S_OK;
    }

    *dxgi_device = NULL;
    return E_NOINTERFACE;
}

static HRESULT factory_get_d3d11_device(struct composition_surface_factory *factory, ID3D11Device **d3d11_device)
{
    IDXGIDevice *dxgi_device = NULL;
    HRESULT hr;

    if (SUCCEEDED(IUnknown_QueryInterface(factory->rendering_device, &IID_ID3D11Device, (void **)d3d11_device)))
        return S_OK;

    hr = factory_get_dxgi_device(factory, &dxgi_device);
    if (FAILED(hr))
        return hr;

    hr = IDXGIDevice_QueryInterface(dxgi_device, &IID_ID3D11Device, (void **)d3d11_device);
    IDXGIDevice_Release(dxgi_device);
    return hr;
}

static HRESULT STDMETHODCALLTYPE surface_BeginDraw(IDCompositionSurfaceUnknown *iface,
        const RECT *rect, REFIID iid, void **object, POINT *offset)
{
    struct composition_surface *surface = impl_from_IDCompositionSurfaceUnknown(iface);
    struct composition_surface_factory *factory = impl_from_IDCompositionSurfaceFactory(surface->factory);
    struct composition_device *device = impl_from_IDCompositionDevice(factory->device);
    D3D11_TEXTURE2D_DESC texture_desc;
    ID3D11Texture2D *draw_surface;
    ID3D11Device *d3d11_device;
    RECT whole_rect;
    HRESULT hr;

    TRACE("DCOMP: surface_BeginDraw iface %p, rect %s, iid %s, object %p, offset %p\n", iface, wine_dbgstr_rect(rect),
            debugstr_guid(iid), object, offset);

    if (IsEqualGUID(iid, &IID_ID2D1DeviceContext))
    {
        FIXME("ID2D1DeviceContext draw surface is currently unsupported.\n");
        return E_NOTIMPL;
    }

    if (!rect)
    {
        SetRect(&whole_rect, 0, 0, surface->width, surface->height);
        rect = &whole_rect;
    }

    /* The first BeginDraw must use the whole surface for non-virtual IDCompositionSurface;
     * a virtual surface is resizable and scrollable, so it accepts any in-bounds sub-rect. */
    if (!surface->is_virtual && !surface->draw_surface && !(rect->left == 0 && rect->top == 0
            && rect->right == surface->width && rect->bottom == surface->height))
        return E_INVALIDARG;

    if (rect->left < 0 || rect->top < 0 || rect->right > surface->width
            || rect->bottom > surface->height)
        return E_INVALIDARG;

    if (!object || !offset)
        return E_INVALIDARG;

    if (!surface->draw_surface)
    {
        hr = factory_get_d3d11_device(factory, &d3d11_device);
        if (FAILED(hr))
        {
            FIXME("Failed to get a d3d11 device, should a new one be created? hr %#lx\n", hr);
            return hr;
        }

        texture_desc.Width = surface->width;
        texture_desc.Height = surface->height;
        texture_desc.MipLevels = 1;
        texture_desc.ArraySize = 1;
        texture_desc.Format = surface->pixel_format;
        texture_desc.SampleDesc.Count = 1;
        texture_desc.SampleDesc.Quality = 0;
        texture_desc.Usage = D3D11_USAGE_DEFAULT;
        texture_desc.BindFlags = D3D11_BIND_RENDER_TARGET;
        texture_desc.CPUAccessFlags = 0;
        texture_desc.MiscFlags = D3D11_RESOURCE_MISC_GUARDED | D3D11_RESOURCE_MISC_SHARED;
        if (surface->pixel_format == DXGI_FORMAT_B8G8R8A8_UNORM)
            texture_desc.MiscFlags |= D3D11_RESOURCE_MISC_GDI_COMPATIBLE;
        hr = ID3D11Device_CreateTexture2D(d3d11_device, &texture_desc, NULL, &draw_surface);
        ID3D11Device_Release(d3d11_device);
        if (FAILED(hr))
        {
            ERR("Failed to create a draw surface.\n");
            return hr;
        }

        if (InterlockedCompareExchangePointer((void **)&surface->draw_surface, draw_surface, 0))
            ID3D11Texture2D_Release(draw_surface);
    }

    dcomp_lock();
    if (device->drawing_surface && device->drawing_surface != &surface->IDCompositionSurfaceUnknown_iface)
    {
        dcomp_unlock();
        return DCOMPOSITION_ERROR_SURFACE_BEING_RENDERED;
    }
    device->drawing_surface = &surface->IDCompositionSurfaceUnknown_iface;
    dcomp_unlock();

    hr = IUnknown_QueryInterface(surface->draw_surface, iid, object);
    if (FAILED(hr))
    {
        /* The draw never started: release the claim taken above, otherwise the surface
         * stays 'being rendered' and every later BeginDraw / Resize is rejected with
         * DCOMPOSITION_ERROR_SURFACE_BEING_RENDERED.  Chromium's first BeginDraw asks
         * for ID2D1Surface, which is not what backs the surface here, so this path is
         * taken on the way to a working IID_ID3D11Texture2D / IID_ID3D11Surface draw. */
        dcomp_lock();
        if (device->drawing_surface == &surface->IDCompositionSurfaceUnknown_iface)
            device->drawing_surface = NULL;
        dcomp_unlock();
        return hr;
    }

    offset->x = rect->left;
    offset->y = rect->top;
    surface->draw_rect = *rect;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE surface_EndDraw(IDCompositionSurfaceUnknown *iface)
{
    struct composition_surface *surface = impl_from_IDCompositionSurfaceUnknown(iface);
    struct composition_surface_factory *factory = impl_from_IDCompositionSurfaceFactory(surface->factory);
    struct composition_device *device = impl_from_IDCompositionDevice(factory->device);
    ID3D11Resource *dst_resource, *src_resource;
    ID3D11DeviceContext *d3d11_device_context;
    ID3D11Multithread *multithread = NULL;
    ID3D11Device *d3d11_device;
    D3D11_BOX box;
    RECT draw_rect;
    HRESULT hr;

    TRACE("iface %p.\n", iface);

    dcomp_lock();
    if (!(device->drawing_surface && device->drawing_surface == &surface->IDCompositionSurfaceUnknown_iface))
    {
        dcomp_unlock();
        return DCOMPOSITION_ERROR_SURFACE_NOT_BEING_RENDERED;
    }
    draw_rect = surface->draw_rect;
    dcomp_unlock();
    TRACE("DCOMP: surface_EndDraw surface %p (size %ux%u)\n", surface, surface->width, surface->height);

    /* TODO: Copy data after Commit() is called instead of doing it immediately here */

    hr = factory_get_d3d11_device(factory, &d3d11_device);
    if (FAILED(hr))
    {
        FIXME("Failed to get a d3d11 device, should a new one be created? hr %#lx\n", hr);
        dcomp_lock();
        if (device->drawing_surface == &surface->IDCompositionSurfaceUnknown_iface)
            device->drawing_surface = NULL;
        SetRectEmpty(&surface->draw_rect);
        dcomp_unlock();
        return hr;
    }

    hr = ID3D11Texture2D_QueryInterface(surface->draw_surface, &IID_ID3D11Resource, (void **)&src_resource);
    if (FAILED(hr))
    {
        FIXME("Failed to get the source ID3D11Resource.\n");
        ID3D11Device_Release(d3d11_device);
        dcomp_lock();
        if (device->drawing_surface == &surface->IDCompositionSurfaceUnknown_iface)
            device->drawing_surface = NULL;
        SetRectEmpty(&surface->draw_rect);
        dcomp_unlock();
        return hr;
    }

    hr = IUnknown_QueryInterface(surface->physical_surface, &IID_ID3D11Resource, (void **)&dst_resource);
    if (FAILED(hr))
    {
        FIXME("Failed to get the destination ID3D11Resource.\n");
        ID3D11Resource_Release(src_resource);
        ID3D11Device_Release(d3d11_device);
        dcomp_lock();
        if (device->drawing_surface == &surface->IDCompositionSurfaceUnknown_iface)
            device->drawing_surface = NULL;
        SetRectEmpty(&surface->draw_rect);
        dcomp_unlock();
        return hr;
    }

    box.left = draw_rect.left;
    box.top = draw_rect.top;
    box.front = 0;
    box.right = draw_rect.right;
    box.bottom = draw_rect.bottom;
    box.back = 1;

    ID3D11Device_GetImmediateContext(d3d11_device, &d3d11_device_context);
    if (SUCCEEDED(ID3D11DeviceContext_QueryInterface(d3d11_device_context,
            &IID_ID3D11Multithread, (void **)&multithread)))
        ID3D11Multithread_Enter(multithread);
    ID3D11DeviceContext_CopySubresourceRegion(d3d11_device_context, dst_resource, 0,
            draw_rect.left, draw_rect.top, 0, src_resource, 0, &box);
    ID3D11DeviceContext_Flush(d3d11_device_context);
    if (multithread)
    {
        ID3D11Multithread_Leave(multithread);
        ID3D11Multithread_Release(multithread);
    }
    ID3D11DeviceContext_Release(d3d11_device_context);
    ID3D11Resource_Release(dst_resource);
    ID3D11Resource_Release(src_resource);
    ID3D11Device_Release(d3d11_device);

    dcomp_lock();
    if (device->drawing_surface == &surface->IDCompositionSurfaceUnknown_iface)
        device->drawing_surface = NULL;
    SetRectEmpty(&surface->draw_rect);
    surface->content_version++;
    InterlockedIncrement(&dcomp_tree_version);
    g_last_rendered_surface = surface;
    dcomp_unlock();

    if (device->wake_event)
        SetEvent(device->wake_event);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE surface_SuspendDraw(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_ResumeDraw(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_Scroll(IDCompositionSurfaceUnknown *iface, const RECT *scroll,
        const RECT *clip, int offset_x, int offset_y)
{
    FIXME("iface %p, scroll %s, clip %s, offset_x %d, offset_y %d stub!\n", iface,
            wine_dbgstr_rect(scroll), wine_dbgstr_rect(clip), offset_x, offset_y);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot8(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot9(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

/* Replaces the surface backing with one of the new size.  Both the undocumented
 * IDCompositionSurfaceUnknown slot 3 and IDCompositionVirtualSurface::Resize() do
 * exactly this, so the work lives here; the size check and the 'being rendered'
 * rejection are the same for both callers. */
static HRESULT composition_surface_resize(struct composition_surface *surface, UINT width, UINT height)
{
    struct composition_surface_factory *factory = impl_from_IDCompositionSurfaceFactory(surface->factory);
    struct composition_device *device = impl_from_IDCompositionDevice(factory->device);
    IDXGIDevice *dxgi_surface_device = NULL;
    IDXGISurface *dxgi_surface;
    DXGI_SURFACE_DESC desc;
    HRESULT hr;

    if (!width || !height)
        return E_INVALIDARG;

    dcomp_lock();
    if (device->drawing_surface)
    {
        dcomp_unlock();
        return DCOMPOSITION_ERROR_SURFACE_BEING_RENDERED;
    }

    if (width == surface->width && height == surface->height)
    {
        dcomp_unlock();
        return S_OK;
    }

    if (surface->cached_srv)
    {
        ID3D11ShaderResourceView_Release(surface->cached_srv);
        surface->cached_srv = NULL;
    }
    if (surface->cached_srv_device)
    {
        ID3D11Device_Release(surface->cached_srv_device);
        surface->cached_srv_device = NULL;
    }
    IUnknown_Release(surface->physical_surface);
    if (surface->draw_surface)
    {
        ID3D11Texture2D_Release(surface->draw_surface);
        surface->draw_surface = NULL;
    }

    desc.Width = width;
    desc.Height = height;
    desc.Format = surface->pixel_format;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    hr = factory_get_dxgi_device(factory, &dxgi_surface_device);
    if (FAILED(hr))
    {
        dcomp_unlock();
        ERR("Failed to get dxgi device, hr %#lx.\n", hr);
        return hr;
    }

    hr = IDXGIDevice_CreateSurface(dxgi_surface_device, &desc, 1,
            DXGI_USAGE_BACK_BUFFER | DXGI_USAGE_SHADER_INPUT, NULL, &dxgi_surface);
    IDXGIDevice_Release(dxgi_surface_device);
    if (FAILED(hr))
    {
        dcomp_unlock();
        ERR("Failed to create a IDXGISurface.\n");
        return hr;
    }

    surface->width = width;
    surface->height = height;
    surface->physical_surface = (IUnknown *)dxgi_surface;
    /* A resized surface has no trimmed area any more, the rectangles describe the old size. */
    free(surface->trim_rects);
    surface->trim_rects = NULL;
    surface->trim_count = 0;
    surface->content_version = 0;
    InterlockedIncrement(&dcomp_tree_version);
    dcomp_unlock();
    return S_OK;
}

/* Undocumented. Looks like a resize method. */
static HRESULT STDMETHODCALLTYPE surface_ResizePrivate(IDCompositionSurfaceUnknown *iface, UINT width, UINT height)
{
    struct composition_surface *surface = impl_from_IDCompositionSurfaceUnknown(iface);

    FIXME("iface %p width %d height %d!\n", iface, width, height);

    return composition_surface_resize(surface, width, height);
}

/* Undocumented. Win11 FrameworkUdk ResizeDCompositionSurface calls slot 11 (+0x58). */
static HRESULT STDMETHODCALLTYPE surface_ResizeFrameworkUdk(IDCompositionSurfaceUnknown *iface, UINT width, UINT height)
{
    struct composition_surface *surface = impl_from_IDCompositionSurfaceUnknown(iface);

    TRACE("iface %p, width %u, height %u.\n", iface, width, height);

    return composition_surface_resize(surface, width, height);
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot12(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot13(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot14(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot15(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot16(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot17(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot18(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot19(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot20(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot21(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot22(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot23(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot24(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot25(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot26(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot27(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot28(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot29(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot30(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot31(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot32(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot33(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot34(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot35(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot36(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot37(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot38(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot39(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot40(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot41(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot42(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot43(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot44(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot45(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot46(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot47(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot48(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot49(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot50(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot51(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot52(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot53(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot54(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot55(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot56(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot57(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot58(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot59(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot60(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot61(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot62(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot63(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot64(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot65(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot66(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot67(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot68(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot69(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot70(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot71(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot72(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot73(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot74(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot75(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot76(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot77(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot78(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot79(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot80(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot81(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot82(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot83(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot84(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot85(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot86(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot87(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot88(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot89(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot90(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot91(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot92(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot93(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot94(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot95(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot96(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot97(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot98(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot99(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot100(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot101(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot102(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot103(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot104(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot105(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE surface_reserved_slot106(IDCompositionSurfaceUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static const struct IDCompositionSurfaceUnknownVtbl surface_vtbl =
{
    /* IUnknown methods */
    surface_QueryInterface,
    surface_AddRef,
    surface_Release,
    /* IDCompositionSurface methods */
    surface_BeginDraw,
    surface_EndDraw,
    surface_SuspendDraw,
    surface_ResumeDraw,
    surface_Scroll,
    /* IDCompositionSurfaceUnknown methods */
    surface_reserved_slot8,
    surface_reserved_slot9,
    surface_ResizePrivate,
    surface_ResizeFrameworkUdk,
    surface_reserved_slot12,
    surface_reserved_slot13,
    surface_reserved_slot14,
    surface_reserved_slot15,
    surface_reserved_slot16,
    surface_reserved_slot17,
    surface_reserved_slot18,
    surface_reserved_slot19,
    surface_reserved_slot20,
    surface_reserved_slot21,
    surface_reserved_slot22,
    surface_reserved_slot23,
    surface_reserved_slot24,
    surface_reserved_slot25,
    surface_reserved_slot26,
    surface_reserved_slot27,
    surface_reserved_slot28,
    surface_reserved_slot29,
    surface_reserved_slot30,
    surface_reserved_slot31,
    surface_reserved_slot32,
    surface_reserved_slot33,
    surface_reserved_slot34,
    surface_reserved_slot35,
    surface_reserved_slot36,
    surface_reserved_slot37,
    surface_reserved_slot38,
    surface_reserved_slot39,
    surface_reserved_slot40,
    surface_reserved_slot41,
    surface_reserved_slot42,
    surface_reserved_slot43,
    surface_reserved_slot44,
    surface_reserved_slot45,
    surface_reserved_slot46,
    surface_reserved_slot47,
    surface_reserved_slot48,
    surface_reserved_slot49,
    surface_reserved_slot50,
    surface_reserved_slot51,
    surface_reserved_slot52,
    surface_reserved_slot53,
    surface_reserved_slot54,
    surface_reserved_slot55,
    surface_reserved_slot56,
    surface_reserved_slot57,
    surface_reserved_slot58,
    surface_reserved_slot59,
    surface_reserved_slot60,
    surface_reserved_slot61,
    surface_reserved_slot62,
    surface_reserved_slot63,
    surface_reserved_slot64,
    surface_reserved_slot65,
    surface_reserved_slot66,
    surface_reserved_slot67,
    surface_reserved_slot68,
    surface_reserved_slot69,
    surface_reserved_slot70,
    surface_reserved_slot71,
    surface_reserved_slot72,
    surface_reserved_slot73,
    surface_reserved_slot74,
    surface_reserved_slot75,
    surface_reserved_slot76,
    surface_reserved_slot77,
    surface_reserved_slot78,
    surface_reserved_slot79,
    surface_reserved_slot80,
    surface_reserved_slot81,
    surface_reserved_slot82,
    surface_reserved_slot83,
    surface_reserved_slot84,
    surface_reserved_slot85,
    surface_reserved_slot86,
    surface_reserved_slot87,
    surface_reserved_slot88,
    surface_reserved_slot89,
    surface_reserved_slot90,
    surface_reserved_slot91,
    surface_reserved_slot92,
    surface_reserved_slot93,
    surface_reserved_slot94,
    surface_reserved_slot95,
    surface_reserved_slot96,
    surface_reserved_slot97,
    surface_reserved_slot98,
    surface_reserved_slot99,
    surface_reserved_slot100,
    surface_reserved_slot101,
    surface_reserved_slot102,
    surface_reserved_slot103,
    surface_reserved_slot104,
    surface_reserved_slot105,
    surface_reserved_slot106,
};

/* IDCompositionVirtualSurface::Resize().  Same resize as the private slot 3 above;
 * Windows additionally keeps the previously drawn content, which Wine's compositor
 * does not need because the surface is re-drawn by the client after a resize. */
static HRESULT STDMETHODCALLTYPE surface_Resize(IDCompositionSurfaceUnknown *iface, UINT width, UINT height)
{
    struct composition_surface *surface = impl_from_IDCompositionSurfaceUnknown(iface);

    TRACE("iface %p, width %u, height %u.\n", iface, width, height);

    return composition_surface_resize(surface, width, height);
}

/* IDCompositionVirtualSurface::Trim(): tells the compositor which rectangles of a
 * virtual surface are not used, so that they may be dropped from the surface.  The
 * rectangles are validated and remembered (they are observable through the surface
 * state, and the call has to reject an out-of-surface rectangle and a surface that
 * is currently being drawn); Wine composites a virtual surface by blitting the
 * whole surface to the target window, so trimming a rectangle out of the presented
 * content is not implemented. */
static HRESULT STDMETHODCALLTYPE surface_Trim(IDCompositionSurfaceUnknown *iface,
        const RECT *rectangles, UINT count)
{
    struct composition_surface *surface = impl_from_IDCompositionSurfaceUnknown(iface);
    struct composition_surface_factory *factory = impl_from_IDCompositionSurfaceFactory(surface->factory);
    struct composition_device *device = impl_from_IDCompositionDevice(factory->device);
    RECT *new_rects = NULL;
    UINT i;

    TRACE("iface %p, rectangles %p, count %u.\n", iface, rectangles, count);

    if (!count && !rectangles)
    {
        dcomp_lock();
        free(surface->trim_rects);
        surface->trim_rects = NULL;
        surface->trim_count = 0;
        dcomp_unlock();
        return S_OK;
    }

    if (!rectangles)
        return E_INVALIDARG;

    for (i = 0; i < count; i++)
    {
        const RECT *r = &rectangles[i];

        if (r->left < 0 || r->top < 0 || r->right > (int)surface->width
                || r->bottom > (int)surface->height || r->right <= r->left || r->bottom <= r->top)
            return E_INVALIDARG;
    }

    if (!(new_rects = heap_calloc(count, sizeof(*new_rects))))
        return E_OUTOFMEMORY;

    dcomp_lock();
    if (device->drawing_surface == &surface->IDCompositionSurfaceUnknown_iface)
    {
        dcomp_unlock();
        heap_free(new_rects);
        return DCOMPOSITION_ERROR_SURFACE_BEING_RENDERED;
    }
    free(surface->trim_rects);
    for (i = 0; i < count; i++)
        new_rects[i] = rectangles[i];
    surface->trim_rects = new_rects;
    surface->trim_count = count;
    dcomp_unlock();

    return S_OK;
}

/*
 * IDCompositionVirtualSurface has the layout of IDCompositionSurface (BeginDraw,
 * EndDraw, SuspendDraw, ResumeDraw, Scroll) followed by Resize() and Trim().  The
 * first eight slots are shared with surface_vtbl above, so a virtual surface is
 * the same object with a vtable whose slots 8 and 9 (the private Unknown1/Unknown2
 * slots, unused here) are Resize() and Trim().  Kept as a plain function pointer
 * array because the two slot signatures differ from the private interface's.
 */
static const void *virtual_surface_vtbl[] =
{
    surface_QueryInterface,   /* 0 (0x00) IUnknown */
    surface_AddRef,           /* 1 (0x08) */
    surface_Release,          /* 2 (0x10) */
    surface_BeginDraw,        /* 3 (0x18) */
    surface_EndDraw,          /* 4 (0x20) */
    surface_SuspendDraw,      /* 5 (0x28) */
    surface_ResumeDraw,       /* 6 (0x30) */
    surface_Scroll,           /* 7 (0x38) */
    surface_Resize,           /* 8 (0x40) IDCompositionVirtualSurface::Resize */
    surface_Trim,             /* 9 (0x48) IDCompositionVirtualSurface::Trim */
};

struct composition_surface *unsafe_impl_from_IDCompositionSurface(IDCompositionSurface *iface)
{
    if (!iface)
        return NULL;
    if ((void *)iface->lpVtbl != (void *)&surface_vtbl
            && (void *)iface->lpVtbl != (void *)&virtual_surface_vtbl)
        return NULL;
    return CONTAINING_RECORD(iface, struct composition_surface, IDCompositionSurfaceUnknown_iface);
}

static HRESULT create_surface_impl(struct composition_surface_factory *factory, UINT width, UINT height,
        DXGI_FORMAT pixel_format, DXGI_ALPHA_MODE alpha_mode, BOOL is_virtual,
        IDCompositionSurface **dcomp_surface)
{
    struct composition_surface *surface;
    const GUID *physical_surface_iid;
    IUnknown *physical_surface;
    HRESULT hr;

    TRACE("DCOMP: create_surface width %u, height %u, format %#x, alpha_mode %#x, virtual %d\n",
        width, height, pixel_format, alpha_mode, is_virtual);

    if (!width || !height)
        return E_INVALIDARG;

    if (pixel_format != DXGI_FORMAT_B8G8R8A8_UNORM && pixel_format != DXGI_FORMAT_R8G8B8A8_UNORM
            && pixel_format != DXGI_FORMAT_R16G16B16A16_FLOAT)
        return E_INVALIDARG;

    if (alpha_mode == DXGI_ALPHA_MODE_UNSPECIFIED)
        alpha_mode = DXGI_ALPHA_MODE_IGNORE;

    if (alpha_mode != DXGI_ALPHA_MODE_PREMULTIPLIED && alpha_mode != DXGI_ALPHA_MODE_IGNORE)
        return E_INVALIDARG;

    {
        IDXGIDevice *dxgi_device = NULL;

        hr = factory_get_dxgi_device(factory, &dxgi_device);
        if (SUCCEEDED(hr) && dxgi_device)
        {
            IDXGISurface *dxgi_surface;
            DXGI_SURFACE_DESC desc;

            desc.Width = width;
            desc.Height = height;
            desc.Format = pixel_format;
            desc.SampleDesc.Count = 1;
            desc.SampleDesc.Quality = 0;
            /* TODO: What about alpha_mode ? */

            hr = IDXGIDevice_CreateSurface(dxgi_device, &desc, 1,
                    DXGI_USAGE_BACK_BUFFER | DXGI_USAGE_SHADER_INPUT, NULL, &dxgi_surface);
            IDXGIDevice_Release(dxgi_device);
            if (FAILED(hr))
            {
                ERR("Failed to create a IDXGISurface, hr %#lx.\n", hr);
                return hr;
            }

            physical_surface = (IUnknown *)dxgi_surface;
            physical_surface_iid = &IID_IDXGISurface;
        }
        else
        {
            FIXME("Only IDCompositionSurfaceFactory created with an IDXGIDevice or ID2D1Device rendering device is "
                  "currently supported, rendering device guid %s.\n",
                  wine_dbgstr_guid(&factory->rendering_device_iid));
            return E_NOTIMPL;
        }
    }

    surface = calloc(1, sizeof(*surface));
    if (!surface)
        return E_OUTOFMEMORY;

    surface->IDCompositionSurfaceUnknown_iface.lpVtbl = is_virtual ? (void *)virtual_surface_vtbl : &surface_vtbl;
    surface->factory = &factory->IDCompositionSurfaceFactory_iface;
    IDCompositionSurfaceFactory_AddRef(surface->factory);
    surface->physical_surface = physical_surface;
    surface->physical_surface_iid = *physical_surface_iid;
    surface->width = width;
    surface->height = height;
    surface->pixel_format = pixel_format;
    surface->alpha_mode = alpha_mode;
    surface->is_virtual = is_virtual;
    surface->ref = 1;

    *dcomp_surface = (IDCompositionSurface *)&surface->IDCompositionSurfaceUnknown_iface;
    return S_OK;
}

HRESULT create_surface(struct composition_surface_factory *factory, UINT width, UINT height,
        DXGI_FORMAT pixel_format, DXGI_ALPHA_MODE alpha_mode, IDCompositionSurface **dcomp_surface)
{
    return create_surface_impl(factory, width, height, pixel_format, alpha_mode, FALSE, dcomp_surface);
}

HRESULT create_virtual_surface(struct composition_surface_factory *factory, UINT width, UINT height,
        DXGI_FORMAT pixel_format, DXGI_ALPHA_MODE alpha_mode, IDCompositionVirtualSurface **surface)
{
    return create_surface_impl(factory, width, height, pixel_format, alpha_mode, TRUE,
            (IDCompositionSurface **)surface);
}

static HRESULT STDMETHODCALLTYPE factory_QueryInterface(IDCompositionSurfaceFactory *iface, REFIID iid, void **out)
{
    TRACE("iface %p, iid %s, out %p!\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_IUnknown)
            || IsEqualGUID(iid, &IID_IDCompositionSurfaceFactory))
    {
        IUnknown_AddRef(iface);
        *out = iface;
        return S_OK;
    }

    FIXME("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid));
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE factory_AddRef(IDCompositionSurfaceFactory *iface)
{
    struct composition_surface_factory *factory = impl_from_IDCompositionSurfaceFactory(iface);
    ULONG ref = InterlockedIncrement(&factory->ref);

    TRACE("iface %p, ref %lu.\n", iface, ref);
    return ref;
}

static ULONG STDMETHODCALLTYPE factory_Release(IDCompositionSurfaceFactory *iface)
{
    struct composition_surface_factory *factory = impl_from_IDCompositionSurfaceFactory(iface);
    ULONG ref = InterlockedDecrement(&factory->ref);

    TRACE("iface %p, ref %lu.\n", iface, ref);

    if (!ref)
    {
        IDCompositionDevice_Release(factory->device);
        IUnknown_Release(factory->rendering_device);
        free(factory);
    }

    return ref;
}

static HRESULT STDMETHODCALLTYPE factory_CreateSurface(IDCompositionSurfaceFactory *iface,
        UINT width, UINT height, DXGI_FORMAT pixel_format, DXGI_ALPHA_MODE alpha_mode,
        IDCompositionSurface **surface)
{
    struct composition_surface_factory *factory = impl_from_IDCompositionSurfaceFactory(iface);

    FIXME("iface %p, width %u, height %u, format %#x, alpha_mode %#x, surface %p semi-stub!\n", iface,
            width, height, pixel_format, alpha_mode, surface);

    return create_surface(factory, width, height, pixel_format, alpha_mode, surface);
}

static HRESULT STDMETHODCALLTYPE factory_CreateVirtualSurface(IDCompositionSurfaceFactory *iface,
        UINT width, UINT height, DXGI_FORMAT pixel_format, DXGI_ALPHA_MODE alpha_mode,
        IDCompositionVirtualSurface **surface)
{
    struct composition_surface_factory *factory = impl_from_IDCompositionSurfaceFactory(iface);

    TRACE("iface %p, width %u, height %u, format %#x, alpha_mode %#x, surface %p.\n", iface,
            width, height, pixel_format, alpha_mode, surface);

    if (!surface)
        return E_POINTER;

    *surface = NULL;

    return create_virtual_surface(factory, width, height, pixel_format, alpha_mode, surface);
}

static const struct IDCompositionSurfaceFactoryVtbl factory_vtbl =
{
    /* IUnknown methods */
    factory_QueryInterface,
    factory_AddRef,
    factory_Release,
    /* IDCompositionSurfaceFactory methods */
    factory_CreateSurface,
    factory_CreateVirtualSurface,
};

HRESULT create_surface_factory(struct composition_device *device, IUnknown *rendering_device,
        IDCompositionSurfaceFactory **obj)
{
    struct composition_surface_factory *factory;
    IDXGIDevice *dxgi_device;
    ID2D1Device *d2d_device;
    const GUID *iid;

    if (!rendering_device)
        return E_INVALIDARG;

    if (SUCCEEDED(IUnknown_QueryInterface(rendering_device, &IID_IDXGIDevice, (void *)&dxgi_device)))
    {
        TRACE("Creating a surface factory with an IDXGIDevice rendering device.\n");
        iid = &IID_IDXGIDevice;
    }
    else if (SUCCEEDED(IUnknown_QueryInterface(rendering_device, &IID_ID2D1Device, (void *)&d2d_device)))
    {
        TRACE("Creating a surface factory with an IID_ID2D1Device rendering device.\n");
        iid = &IID_ID2D1Device;
    }
    else
    {
        ERR("Unknown rendering device.\n");
        return E_NOINTERFACE;
    }

    factory = calloc(1, sizeof(*factory));
    if (!factory)
        return E_OUTOFMEMORY;

    factory->IDCompositionSurfaceFactory_iface.lpVtbl = &factory_vtbl;
    factory->ref = 1;
    factory->rendering_device = IsEqualGUID(iid, &IID_IDXGIDevice) ? (IUnknown *)dxgi_device : (IUnknown *)d2d_device;
    factory->rendering_device_iid = *iid;
    factory->device = &device->IDCompositionDevice_iface;
    IDCompositionDevice_AddRef(&device->IDCompositionDevice_iface);

    *obj = &factory->IDCompositionSurfaceFactory_iface;
    return S_OK;
}
