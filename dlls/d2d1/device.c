/*
 * Copyright 2014 Henri Verbeet for CodeWeavers
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

#include "d2d1_private.h"
#include "d2d1_shaders.h"
#include "unixlib.h"
#include <math.h>
#include <objidl.h>

WINE_DEFAULT_DEBUG_CHANNEL(d2d);

#define INITIAL_CLIP_STACK_SIZE 4

static const D2D1_MATRIX_3X2_F identity =
{{{
    1.0f, 0.0f,
    0.0f, 1.0f,
    0.0f, 0.0f,
}}};

struct d2d_draw_text_layout_ctx
{
    ID2D1Brush *brush;
    D2D1_DRAW_TEXT_OPTIONS options;
};

static inline struct d2d_device *impl_from_ID2D1Device(ID2D1Device6 *iface)
{
    return CONTAINING_RECORD(iface, struct d2d_device, ID2D1Device6_iface);
}

static ID2D1Brush *d2d_draw_get_text_brush(struct d2d_draw_text_layout_ctx *context, IUnknown *effect)
{
    ID2D1Brush *brush = NULL;

    if (effect && SUCCEEDED(IUnknown_QueryInterface(effect, &IID_ID2D1Brush, (void**)&brush)))
        return brush;

    ID2D1Brush_AddRef(context->brush);
    return context->brush;
}

static void d2d_rect_intersect(D2D1_RECT_F *dst, const D2D1_RECT_F *src)
{
    if (src->left > dst->left)
        dst->left = src->left;
    if (src->top > dst->top)
        dst->top = src->top;
    if (src->right < dst->right)
        dst->right = src->right;
    if (src->bottom < dst->bottom)
        dst->bottom = src->bottom;
}

static void d2d_rect_set(D2D1_RECT_F *dst, float left, float top, float right, float bottom)
{
    dst->left = left;
    dst->top = top;
    dst->right = right;
    dst->bottom = bottom;
}

static void d2d_size_set(D2D1_SIZE_U *dst, float width, float height)
{
    dst->width = width;
    dst->height = height;
}

static BOOL d2d_clip_stack_init(struct d2d_clip_stack *stack)
{
    if (!(stack->stack = malloc(INITIAL_CLIP_STACK_SIZE * sizeof(*stack->stack))))
        return FALSE;

    stack->size = INITIAL_CLIP_STACK_SIZE;
    stack->count = 0;

    return TRUE;
}

static void d2d_clip_stack_cleanup(struct d2d_clip_stack *stack)
{
    free(stack->stack);
}

static BOOL d2d_clip_stack_push(struct d2d_clip_stack *stack, const D2D1_RECT_F *rect)
{
    D2D1_RECT_F r;

    if (!d2d_array_reserve((void **)&stack->stack, &stack->size, stack->count + 1, sizeof(*stack->stack)))
        return FALSE;

    r = *rect;
    if (stack->count)
        d2d_rect_intersect(&r, &stack->stack[stack->count - 1]);
    stack->stack[stack->count++] = r;

    return TRUE;
}

static void d2d_clip_stack_pop(struct d2d_clip_stack *stack)
{
    if (!stack->count)
        return;
    --stack->count;
}

struct d2d_shadow_cb
{
    D2D1_VECTOR_4F color;
    float sigma;
    int radius;
    int src_offset_x;
    int src_offset_y;
    int src_min_x;
    int src_min_y;
    int src_max_x;
    int src_max_y;
};

static void d2d_device_context_release_msaa_resources(struct d2d_device_context *context)
{
    if (context->msaa_sync_srv)
    {
        ID3D11ShaderResourceView_Release(context->msaa_sync_srv);
        context->msaa_sync_srv = NULL;
    }
    if (context->msaa_sync_tex)
    {
        ID3D11Texture2D_Release(context->msaa_sync_tex);
        context->msaa_sync_tex = NULL;
    }
    if (context->msaa_rtv)
    {
        ID3D11RenderTargetView_Release(context->msaa_rtv);
        context->msaa_rtv = NULL;
    }
    if (context->msaa_tex)
    {
        ID3D11Texture2D_Release(context->msaa_tex);
        context->msaa_tex = NULL;
    }
    context->msaa_width = 0;
    context->msaa_height = 0;
    context->msaa_samples = 0;
    context->msaa_format = DXGI_FORMAT_UNKNOWN;
    context->msaa_dirty = FALSE;
}

void d2d_device_context_resolve_msaa(struct d2d_device_context *context)
{
    ID3D11DeviceContext *d3d_ctx;

    if (!context || !context->msaa_dirty)
        return;

    if (context->target.type == D2D_TARGET_BITMAP && context->target.bitmap
            && context->target.bitmap->resource && context->msaa_tex)
    {
        ID3D11Device1_GetImmediateContext(context->d3d_device, &d3d_ctx);
        ID3D11DeviceContext_ResolveSubresource(d3d_ctx, context->target.bitmap->resource, 0,
                (ID3D11Resource *)context->msaa_tex, 0, context->msaa_format);
        ID3D11DeviceContext_Release(d3d_ctx);
    }
    context->msaa_dirty = FALSE;
}

static BOOL d2d_device_context_ensure_msaa(struct d2d_device_context *render_target,
        ID3D11DeviceContext1 *context)
{
    ID3D11Device1 *device = render_target->d3d_device;
    struct d2d_bitmap *target_bm = render_target->target.bitmap;
    D3D11_RENDER_TARGET_VIEW_DESC rtv_desc;
    D3D11_TEXTURE2D_DESC tdesc;
    ID3D11Texture2D *target_tex = NULL;
    ID3D11ShaderResourceView *src_srv = NULL;
    UINT samples = 0, q = 0;

    if (!target_bm || !target_bm->rtv || !target_bm->resource || !render_target->rs_msaa)
        return FALSE;

    ID3D11RenderTargetView_GetDesc(target_bm->rtv, &rtv_desc);
    if (rtv_desc.ViewDimension != D3D11_RTV_DIMENSION_TEXTURE2D || rtv_desc.Texture2D.MipSlice != 0)
        return FALSE;

    if (FAILED(ID3D11Resource_QueryInterface(target_bm->resource, &IID_ID3D11Texture2D, (void **)&target_tex)))
        return FALSE;
    ID3D11Texture2D_GetDesc(target_tex, &tdesc);
    ID3D11Texture2D_Release(target_tex);

    if (tdesc.SampleDesc.Count != 1 || tdesc.ArraySize != 1 || tdesc.MipLevels != 1
            || !tdesc.Width || !tdesc.Height)
        return FALSE;

    if (render_target->msaa_tex && render_target->msaa_width == tdesc.Width
            && render_target->msaa_height == tdesc.Height && render_target->msaa_format == tdesc.Format)
    {
        samples = render_target->msaa_samples;
    }
    else
    {
        if (SUCCEEDED(ID3D11Device1_CheckMultisampleQualityLevels(device, tdesc.Format, 4, &q)) && q > 0)
            samples = 4;
        else if (SUCCEEDED(ID3D11Device1_CheckMultisampleQualityLevels(device, tdesc.Format, 2, &q)) && q > 0)
            samples = 2;
        else
            return FALSE;

        d2d_device_context_release_msaa_resources(render_target);

        {
            D3D11_TEXTURE2D_DESC mdesc = {0};
            mdesc.Width = tdesc.Width;
            mdesc.Height = tdesc.Height;
            mdesc.MipLevels = 1;
            mdesc.ArraySize = 1;
            mdesc.Format = tdesc.Format;
            mdesc.SampleDesc.Count = samples;
            mdesc.SampleDesc.Quality = 0;
            mdesc.Usage = D3D11_USAGE_DEFAULT;
            mdesc.BindFlags = D3D11_BIND_RENDER_TARGET;

            if (FAILED(ID3D11Device1_CreateTexture2D(device, &mdesc, NULL, &render_target->msaa_tex)))
                return FALSE;
            if (FAILED(ID3D11Device1_CreateRenderTargetView(device, (ID3D11Resource *)render_target->msaa_tex,
                    NULL, &render_target->msaa_rtv)))
            {
                d2d_device_context_release_msaa_resources(render_target);
                return FALSE;
            }
            render_target->msaa_width = tdesc.Width;
            render_target->msaa_height = tdesc.Height;
            render_target->msaa_samples = samples;
            render_target->msaa_format = tdesc.Format;
        }
    }

    if (!render_target->blit_vs || !render_target->blit_ps)
    {
        if (!render_target->blit_vs)
            ID3D11Device1_CreateVertexShader(device, d2d1_blit_vs_dxbc,
                    sizeof(d2d1_blit_vs_dxbc), NULL, &render_target->blit_vs);
        if (!render_target->blit_ps)
            ID3D11Device1_CreatePixelShader(device, d2d1_blit_ps_dxbc,
                    sizeof(d2d1_blit_ps_dxbc), NULL, &render_target->blit_ps);
        if (!render_target->blit_vs || !render_target->blit_ps)
            return FALSE;
    }

    if (!render_target->msaa_dirty)
    {
        D3D11_VIEWPORT full_vp = { 0.0f, 0.0f, (float)tdesc.Width, (float)tdesc.Height, 0.0f, 1.0f };
        D3D11_RECT full_sc = { 0, 0, (LONG)tdesc.Width, (LONG)tdesc.Height };
        ID3D11ShaderResourceView *null_srv = NULL;

        if (target_bm->srv)
        {
            src_srv = target_bm->srv;
        }
        else
        {
            if (!render_target->msaa_sync_tex)
            {
                D3D11_TEXTURE2D_DESC sdesc = {0};
                sdesc.Width = tdesc.Width;
                sdesc.Height = tdesc.Height;
                sdesc.MipLevels = 1;
                sdesc.ArraySize = 1;
                sdesc.Format = tdesc.Format;
                sdesc.SampleDesc.Count = 1;
                sdesc.Usage = D3D11_USAGE_DEFAULT;
                sdesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                if (SUCCEEDED(ID3D11Device1_CreateTexture2D(device, &sdesc, NULL, &render_target->msaa_sync_tex)))
                {
                    if (FAILED(ID3D11Device1_CreateShaderResourceView(device,
                            (ID3D11Resource *)render_target->msaa_sync_tex, NULL, &render_target->msaa_sync_srv)))
                    {
                        ID3D11Texture2D_Release(render_target->msaa_sync_tex);
                        render_target->msaa_sync_tex = NULL;
                    }
                }
            }
            if (!render_target->msaa_sync_srv)
                return FALSE;
            ID3D11DeviceContext1_CopyResource(context, (ID3D11Resource *)render_target->msaa_sync_tex,
                    target_bm->resource);
            src_srv = render_target->msaa_sync_srv;
        }

        {
            unsigned int blit_offset = 0;
            UINT num_scissors = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
            D3D11_RECT saved_scissors[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];

            ID3D11DeviceContext1_RSGetScissorRects(context, &num_scissors, saved_scissors);
            ID3D11DeviceContext1_OMSetRenderTargets(context, 1, &render_target->msaa_rtv, NULL);
            ID3D11DeviceContext1_OMSetBlendState(context, NULL, NULL, D3D11_DEFAULT_SAMPLE_MASK);
            ID3D11DeviceContext1_IASetInputLayout(context, render_target->shape_resources[D2D_SHAPE_TYPE_TRIANGLE].il);
            ID3D11DeviceContext1_IASetPrimitiveTopology(context, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            ID3D11DeviceContext1_IASetIndexBuffer(context, render_target->ib, DXGI_FORMAT_R16_UINT, 0);
            ID3D11DeviceContext1_IASetVertexBuffers(context, 0, 1, &render_target->vb,
                    &render_target->vb_stride, &blit_offset);
            ID3D11DeviceContext1_VSSetShader(context, render_target->blit_vs, NULL, 0);
            ID3D11DeviceContext1_PSSetShader(context, render_target->blit_ps, NULL, 0);
            ID3D11DeviceContext1_PSSetShaderResources(context, 0, 1, &src_srv);
            ID3D11DeviceContext1_RSSetViewports(context, 1, &full_vp);
            ID3D11DeviceContext1_RSSetScissorRects(context, 1, &full_sc);
            ID3D11DeviceContext1_RSSetState(context, render_target->rs_msaa);
            ID3D11DeviceContext1_DrawIndexed(context, 6, 0, 0);
            ID3D11DeviceContext1_PSSetShaderResources(context, 0, 1, &null_srv);
            if (num_scissors)
                ID3D11DeviceContext1_RSSetScissorRects(context, num_scissors, saved_scissors);
        }

        render_target->msaa_dirty = TRUE;
    }

    return TRUE;
}

static void d2d_device_context_draw_ex(struct d2d_device_context *render_target, enum d2d_shape_type shape_type,
        ID3D11Buffer *ib, unsigned int index_count, ID3D11Buffer *vb, unsigned int vb_stride,
        struct d2d_brush *brush, struct d2d_brush *opacity_brush, BOOL resolve_msaa)
{
    struct d2d_shape_resources *shape_resources = &render_target->shape_resources[shape_type];
    ID3DDeviceContextState *prev_state;
    ID3D11Device1 *device = render_target->d3d_device;
    ID3D11DeviceContext1 *context;
    ID3D11Buffer *vs_cb = render_target->vs_cb, *ps_cb = render_target->ps_cb;
    ID3D11RenderTargetView *rtv;
    ID3D11RasterizerState *rs;
    D3D11_RECT scissor_rect;
    unsigned int offset;
    D3D11_VIEWPORT vp;
    BOOL use_msaa = FALSE;

    vp.TopLeftX = 0;
    vp.TopLeftY = 0;
    vp.Width = render_target->pixel_size.width;
    vp.Height = render_target->pixel_size.height;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;

    if (render_target->cs)
        EnterCriticalSection(render_target->cs);

    ID3D11Device1_GetImmediateContext1(device, &context);
    ID3D11DeviceContext1_SwapDeviceContextState(context, render_target->d3d_state, &prev_state);

    if (brush && !opacity_brush && brush->type != D2D_BRUSH_TYPE_BITMAP
            && render_target->drawing_state.antialiasMode != D2D1_ANTIALIAS_MODE_ALIASED)
    {
        use_msaa = d2d_device_context_ensure_msaa(render_target, context);
    }

    rtv = use_msaa ? render_target->msaa_rtv : render_target->target.bitmap->rtv;
    rs = use_msaa ? render_target->rs_msaa : render_target->rs;

    ID3D11DeviceContext1_IASetInputLayout(context, shape_resources->il);
    ID3D11DeviceContext1_IASetPrimitiveTopology(context, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext1_IASetIndexBuffer(context, ib, DXGI_FORMAT_R16_UINT, 0);
    offset = 0;
    ID3D11DeviceContext1_IASetVertexBuffers(context, 0, 1, &vb, &vb_stride, &offset);
    ID3D11DeviceContext1_VSSetConstantBuffers(context, 0, 1, &vs_cb);
    ID3D11DeviceContext1_VSSetShader(context, shape_resources->vs, NULL, 0);
    ID3D11DeviceContext1_PSSetConstantBuffers(context, 0, 1, &ps_cb);
    ID3D11DeviceContext1_PSSetShader(context, render_target->ps, NULL, 0);
    ID3D11DeviceContext1_RSSetViewports(context, 1, &vp);
    if (render_target->clip_stack.count)
    {
        const D2D1_RECT_F *clip_rect;

        clip_rect = &render_target->clip_stack.stack[render_target->clip_stack.count - 1];
        scissor_rect.left = ceilf(clip_rect->left - 0.5f);
        scissor_rect.top = ceilf(clip_rect->top - 0.5f);
        scissor_rect.right = ceilf(clip_rect->right - 0.5f);
        scissor_rect.bottom = ceilf(clip_rect->bottom - 0.5f);
    }
    else
    {
        scissor_rect.left = 0.0f;
        scissor_rect.top = 0.0f;
        scissor_rect.right = render_target->pixel_size.width;
        scissor_rect.bottom = render_target->pixel_size.height;
    }
    if (render_target->has_priv_clip)
    {
        LONG pl = (LONG)floorf(render_target->priv_clip.left + 0.5f);
        LONG pt = (LONG)floorf(render_target->priv_clip.top + 0.5f);
        LONG pr = (LONG)floorf(render_target->priv_clip.right + 0.5f);
        LONG pb = (LONG)floorf(render_target->priv_clip.bottom + 0.5f);
        if (pl > scissor_rect.left) scissor_rect.left = pl;
        if (pt > scissor_rect.top) scissor_rect.top = pt;
        if (pr < scissor_rect.right) scissor_rect.right = pr;
        if (pb < scissor_rect.bottom) scissor_rect.bottom = pb;
        if (scissor_rect.right < scissor_rect.left) scissor_rect.right = scissor_rect.left;
        if (scissor_rect.bottom < scissor_rect.top) scissor_rect.bottom = scissor_rect.top;
    }
    if (render_target->has_subrect)
    {
        if (render_target->target_subrect.left > scissor_rect.left)
            scissor_rect.left = render_target->target_subrect.left;
        if (render_target->target_subrect.top > scissor_rect.top)
            scissor_rect.top = render_target->target_subrect.top;
        if (render_target->target_subrect.right < scissor_rect.right)
            scissor_rect.right = render_target->target_subrect.right;
        if (render_target->target_subrect.bottom < scissor_rect.bottom)
            scissor_rect.bottom = render_target->target_subrect.bottom;
        if (scissor_rect.right < scissor_rect.left) scissor_rect.right = scissor_rect.left;
        if (scissor_rect.bottom < scissor_rect.top) scissor_rect.bottom = scissor_rect.top;
    }
    ID3D11DeviceContext1_RSSetScissorRects(context, 1, &scissor_rect);
    ID3D11DeviceContext1_RSSetState(context, rs);
    ID3D11DeviceContext1_OMSetRenderTargets(context, 1, &rtv, NULL);
    if (brush)
    {
        ID3D11DeviceContext1_OMSetBlendState(context, render_target->bs, NULL, D3D11_DEFAULT_SAMPLE_MASK);
        d2d_brush_bind_resources(brush, render_target, 0);
    }
    else
    {
        ID3D11DeviceContext1_OMSetBlendState(context, NULL, NULL, D3D11_DEFAULT_SAMPLE_MASK);
    }
    if (opacity_brush)
        d2d_brush_bind_resources(opacity_brush, render_target, 1);

    if (ib)
        ID3D11DeviceContext1_DrawIndexed(context, index_count, 0, 0);
    else
        ID3D11DeviceContext1_Draw(context, index_count, 0);

    if (use_msaa && resolve_msaa)
    {
        ID3D11DeviceContext1_ResolveSubresource(context, render_target->target.bitmap->resource, 0,
                (ID3D11Resource *)render_target->msaa_tex, 0, render_target->msaa_format);
        render_target->msaa_dirty = FALSE;
    }

    ID3D11DeviceContext1_SwapDeviceContextState(context, prev_state, NULL);
    ID3D11DeviceContext1_Release(context);
    ID3DDeviceContextState_Release(prev_state);

    if (render_target->cs)
        LeaveCriticalSection(render_target->cs);
}

static void d2d_device_context_draw(struct d2d_device_context *render_target, enum d2d_shape_type shape_type,
        ID3D11Buffer *ib, unsigned int index_count, ID3D11Buffer *vb, unsigned int vb_stride,
        struct d2d_brush *brush, struct d2d_brush *opacity_brush)
{
    d2d_device_context_draw_ex(render_target, shape_type, ib, index_count, vb, vb_stride,
            brush, opacity_brush, TRUE);
}

static void d2d_device_context_set_error(struct d2d_device_context *context, HRESULT code)
{
    WARN("code %#lx.\n", code);
    context->error.code = code;
    context->error.tag1 = context->drawing_state.tag1;
    context->error.tag2 = context->drawing_state.tag2;
}

static inline struct d2d_device_context *impl_from_IUnknown(IUnknown *iface)
{
    return CONTAINING_RECORD(iface, struct d2d_device_context, IUnknown_iface);
}

static inline struct d2d_device_context *impl_from_ID2D1DeviceContext(ID2D1DeviceContext6 *iface)
{
    return CONTAINING_RECORD(iface, struct d2d_device_context, ID2D1DeviceContext6_iface);
}

static const GUID IID_ID2D1PrivateCompositorDeviceContext =
    {0x2ea67ed7, 0xd42e, 0x4c07, {0x9d, 0xd5, 0xa9, 0x1e, 0xa2, 0x3e, 0x01, 0xd2}};

static inline struct d2d_device_context *impl_from_ID2D1PrivateCompositorDeviceContext(
        ID2D1PrivateCompositorDeviceContext *iface)
{
    return CONTAINING_RECORD(iface, struct d2d_device_context, ID2D1PrivateCompositorDeviceContext_iface);
}

static HRESULT STDMETHODCALLTYPE d2d_priv_dc_QueryInterface(
        ID2D1PrivateCompositorDeviceContext *iface, REFIID iid, void **out)
{
    struct d2d_device_context *context = impl_from_ID2D1PrivateCompositorDeviceContext(iface);
    return IUnknown_QueryInterface(context->outer_unknown, iid, out);
}

static ULONG STDMETHODCALLTYPE d2d_priv_dc_AddRef(ID2D1PrivateCompositorDeviceContext *iface)
{
    struct d2d_device_context *context = impl_from_ID2D1PrivateCompositorDeviceContext(iface);
    return IUnknown_AddRef(context->outer_unknown);
}

static ULONG STDMETHODCALLTYPE d2d_priv_dc_Release(ID2D1PrivateCompositorDeviceContext *iface)
{
    struct d2d_device_context *context = impl_from_ID2D1PrivateCompositorDeviceContext(iface);
    return IUnknown_Release(context->outer_unknown);
}

static HRESULT STDMETHODCALLTYPE d2d_priv_dc_CreatePrimitiveGroupCommandList(
        ID2D1PrivateCompositorDeviceContext *iface, void *cmd_buf, void *prim_buf,
        void **images, UINT32 image_count, void **bitmaps, UINT32 bitmap_count,
        void *reserved1, UINT32 reserved2, void **out)
{
    TRACE("iface %p, out %p stub.\n", iface, out);
    if (out) *out = NULL;
    return S_OK;
}

static void STDMETHODCALLTYPE d2d_priv_dc_DrawPrimitiveGroupCommandList(
        ID2D1PrivateCompositorDeviceContext *iface, void *cmd_list, void *p2,
        const D2D_MATRIX_4X4_F *transform, UINT32 composite_mode, UINT32 flags)
{
    TRACE("iface %p, cmd_list %p stub.\n", iface, cmd_list);
}

static HRESULT STDMETHODCALLTYPE d2d_priv_dc_stub(ID2D1PrivateCompositorDeviceContext *iface)
{
    TRACE("iface %p stub.\n", iface);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d2d_priv_dc_CreateBitmapFromDxgiSurface(
        ID2D1PrivateCompositorDeviceContext *iface, IDXGISurface *surface,
        const D2D1_BITMAP_PROPERTIES1 *desc, ID2D1Bitmap1 **bitmap)
{
    struct d2d_device_context *context = impl_from_ID2D1PrivateCompositorDeviceContext(iface);
    ID2D1Bitmap1 *src_bitmap = NULL;
    IDXGISurface *dxgi_surface = NULL;
    HRESULT hr;

    TRACE("iface %p, surface %p, desc %p, bitmap %p.\n", iface, surface, desc, bitmap);

    if (surface && SUCCEEDED(IUnknown_QueryInterface((IUnknown *)surface, &IID_ID2D1Bitmap1, (void **)&src_bitmap)))
    {
        hr = ID2D1Bitmap1_GetSurface(src_bitmap, &dxgi_surface);
        ID2D1Bitmap1_Release(src_bitmap);
        if (SUCCEEDED(hr) && dxgi_surface)
        {
            hr = ID2D1DeviceContext6_CreateBitmapFromDxgiSurface(&context->ID2D1DeviceContext6_iface,
                    dxgi_surface, desc, bitmap);
            IDXGISurface_Release(dxgi_surface);
            return hr;
        }
    }
    return ID2D1DeviceContext6_CreateBitmapFromDxgiSurface(&context->ID2D1DeviceContext6_iface, surface, desc, bitmap);
}

static void STDMETHODCALLTYPE d2d_priv_dc_SetTarget(ID2D1PrivateCompositorDeviceContext *iface,
        ID2D1Image *target, IUnknown *depth_buffer)
{
    struct d2d_device_context *context = impl_from_ID2D1PrivateCompositorDeviceContext(iface);

    TRACE("iface %p, target %p, depth_buffer %p.\n", iface, target, depth_buffer);
    ID2D1DeviceContext6_SetTarget(&context->ID2D1DeviceContext6_iface, target);
}

static void STDMETHODCALLTYPE d2d_priv_dc_SetTargetSubRect(ID2D1PrivateCompositorDeviceContext *iface,
        ID2D1Image *target, const RECT *subrect)
{
    struct d2d_device_context *context = impl_from_ID2D1PrivateCompositorDeviceContext(iface);

    TRACE("iface %p, target %p, subrect %s.\n", iface, target, wine_dbgstr_rect(subrect));
    if (subrect)
    {
        context->has_subrect = TRUE;
        context->target_subrect = *subrect;
    }
    else
    {
        context->has_subrect = FALSE;
    }
}

static void STDMETHODCALLTYPE d2d_priv_dc_SetClip(ID2D1PrivateCompositorDeviceContext *iface,
        const D2D1_RECT_F *clip_rect, D2D1_ANTIALIAS_MODE aa_mode)
{
    struct d2d_device_context *context = impl_from_ID2D1PrivateCompositorDeviceContext(iface);

    TRACE("iface %p, clip_rect %s, aa_mode %u.\n", iface, debug_d2d_rect_f(clip_rect), aa_mode);
    if (clip_rect)
    {
        context->has_priv_clip = TRUE;
        context->priv_clip = *clip_rect;
    }
    else
    {
        context->has_priv_clip = FALSE;
    }
}

typedef HRESULT (STDMETHODCALLTYPE *d2d_priv_batch_render_fn)(IUnknown *batch);

static HRESULT STDMETHODCALLTYPE d2d_priv_dc_ExecuteDrawListBatch(ID2D1PrivateCompositorDeviceContext *iface,
        IUnknown *batch)
{
    static const GUID iid_partner = {0x26c5dc23, 0xe49c, 0x4b0a, {0x8f, 0x79, 0xe7, 0xb1, 0xac, 0x80, 0x4d, 0x32}};
    struct d2d_device_context *context = impl_from_ID2D1PrivateCompositorDeviceContext(iface);
    IUnknown *partner = NULL;
    ID3D11Texture2D *guard_tex = NULL;
    HRESULT hr = S_OK;

    TRACE("iface %p, batch %p.\n", iface, batch);

    if (!batch)
        return S_OK;

    if (context->target.type == D2D_TARGET_BITMAP && context->target.bitmap && context->target.bitmap->rtv)
    {
        ID3D11DeviceContext *d3d_ctx = NULL;
        D3D11_VIEWPORT vp;
        D3D11_RECT scissor;

        d2d_device_context_resolve_msaa(context);
        ID3D11Device1_GetImmediateContext(context->d3d_device, &d3d_ctx);
        if (d3d_ctx)
        {
            vp.TopLeftX = 0.0f;
            vp.TopLeftY = 0.0f;
            vp.Width = (float)context->pixel_size.width;
            vp.Height = (float)context->pixel_size.height;
            vp.MinDepth = 0.0f;
            vp.MaxDepth = 1.0f;
            scissor.left = 0;
            scissor.top = 0;
            scissor.right = context->pixel_size.width;
            scissor.bottom = context->pixel_size.height;
            if (context->clip_stack.count)
            {
                const D2D1_RECT_F *clip_rect = &context->clip_stack.stack[context->clip_stack.count - 1];
                LONG cl = (LONG)ceilf(clip_rect->left - 0.5f);
                LONG ct = (LONG)ceilf(clip_rect->top - 0.5f);
                LONG cr = (LONG)ceilf(clip_rect->right - 0.5f);
                LONG cb = (LONG)ceilf(clip_rect->bottom - 0.5f);
                if (cl > scissor.left) scissor.left = cl;
                if (ct > scissor.top) scissor.top = ct;
                if (cr < scissor.right) scissor.right = cr;
                if (cb < scissor.bottom) scissor.bottom = cb;
            }
            if (context->has_priv_clip)
            {
                LONG pl = (LONG)floorf(context->priv_clip.left + 0.5f);
                LONG pt = (LONG)floorf(context->priv_clip.top + 0.5f);
                LONG pr = (LONG)floorf(context->priv_clip.right + 0.5f);
                LONG pb = (LONG)floorf(context->priv_clip.bottom + 0.5f);
                if (pl > scissor.left) scissor.left = pl;
                if (pt > scissor.top) scissor.top = pt;
                if (pr < scissor.right) scissor.right = pr;
                if (pb < scissor.bottom) scissor.bottom = pb;
            }
            if (context->has_subrect)
            {
                if (context->target_subrect.left > scissor.left) scissor.left = context->target_subrect.left;
                if (context->target_subrect.top > scissor.top) scissor.top = context->target_subrect.top;
                if (context->target_subrect.right < scissor.right) scissor.right = context->target_subrect.right;
                if (context->target_subrect.bottom < scissor.bottom) scissor.bottom = context->target_subrect.bottom;
            }
            if (scissor.right < scissor.left) scissor.right = scissor.left;
            if (scissor.bottom < scissor.top) scissor.bottom = scissor.top;
            if ((context->clip_stack.count || context->has_priv_clip || context->has_subrect)
                    && context->target.bitmap->resource
                    && SUCCEEDED(ID3D11Resource_QueryInterface(context->target.bitmap->resource,
                            &IID_ID3D11Texture2D, (void **)&guard_tex))
                    && SUCCEEDED(ID3D11Device1_QueryInterface(context->d3d_device, &iid_partner, (void **)&partner)))
            {
                HRESULT (STDMETHODCALLTYPE *set_guard)(IUnknown *, ID3D11Texture2D *, const RECT *) =
                        (*(void ***)partner)[6];
                set_guard(partner, guard_tex, &scissor);
            }
            ID3D11DeviceContext_OMSetRenderTargets(d3d_ctx, 1, &context->target.bitmap->rtv, NULL);
            ID3D11DeviceContext_RSSetViewports(d3d_ctx, 1, &vp);
            ID3D11DeviceContext_RSSetScissorRects(d3d_ctx, 1, &scissor);
            ID3D11DeviceContext_Release(d3d_ctx);
        }
    }

    {
        void **vtbl = *(void ***)batch;
        d2d_priv_batch_render_fn render_fn = (d2d_priv_batch_render_fn)vtbl[3];
        if (render_fn)
            hr = render_fn(batch);
    }

    if (partner && guard_tex)
    {
        HRESULT (STDMETHODCALLTYPE *remove_guard)(IUnknown *, ID3D11Texture2D *) = (*(void ***)partner)[8];
        remove_guard(partner, guard_tex);
    }
    if (guard_tex) ID3D11Texture2D_Release(guard_tex);
    if (partner) IUnknown_Release(partner);

    return hr;
}

static const ID2D1PrivateCompositorDeviceContextVtbl d2d_priv_dc_vtbl =
{
    d2d_priv_dc_QueryInterface,
    d2d_priv_dc_AddRef,
    d2d_priv_dc_Release,
    d2d_priv_dc_CreatePrimitiveGroupCommandList,
    d2d_priv_dc_DrawPrimitiveGroupCommandList,
    d2d_priv_dc_stub,
    d2d_priv_dc_stub,
    d2d_priv_dc_SetTarget,                   /* 7 (+0x38) */
    d2d_priv_dc_stub,
    d2d_priv_dc_SetTargetSubRect,            /* 9 (+0x48) */
    d2d_priv_dc_SetClip,                     /* 10 (+0x50) */
    d2d_priv_dc_stub,
    d2d_priv_dc_CreateBitmapFromDxgiSurface, /* 12 (+0x60) */
    d2d_priv_dc_stub, /* 13 */
    d2d_priv_dc_stub, /* 14 */
    d2d_priv_dc_ExecuteDrawListBatch,        /* 15 (+0x78) */
    d2d_priv_dc_stub, /* 16 */
    d2d_priv_dc_stub, /* 17 */
    d2d_priv_dc_stub, /* 18 */
    d2d_priv_dc_stub, /* 19 */
    d2d_priv_dc_stub, /* 20 */
    d2d_priv_dc_stub, /* 21 */
    d2d_priv_dc_stub, /* 22 */
    d2d_priv_dc_stub, /* 23 */
};

static HRESULT STDMETHODCALLTYPE d2d_device_context_inner_QueryInterface(IUnknown *iface, REFIID iid, void **out)
{
    struct d2d_device_context *context = impl_from_IUnknown(iface);

    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_ID2D1DeviceContext6)
            || IsEqualGUID(iid, &IID_ID2D1DeviceContext5)
            || IsEqualGUID(iid, &IID_ID2D1DeviceContext4)
            || IsEqualGUID(iid, &IID_ID2D1DeviceContext3)
            || IsEqualGUID(iid, &IID_ID2D1DeviceContext2)
            || IsEqualGUID(iid, &IID_ID2D1DeviceContext1)
            || IsEqualGUID(iid, &IID_ID2D1DeviceContext)
            || IsEqualGUID(iid, &IID_ID2D1DeviceContext_ms)   /* published d2d1_1 id, see d2d1_private.h */
            || IsEqualGUID(iid, &IID_ID2D1RenderTarget)
            || IsEqualGUID(iid, &IID_ID2D1Resource)
            || IsEqualGUID(iid, &IID_IUnknown))
    {
        ID2D1DeviceContext6_AddRef(&context->ID2D1DeviceContext6_iface);
        *out = &context->ID2D1DeviceContext6_iface;
        return S_OK;
    }
    else if (IsEqualGUID(iid, &IID_ID2D1GdiInteropRenderTarget))
    {
        ID2D1GdiInteropRenderTarget_AddRef(&context->ID2D1GdiInteropRenderTarget_iface);
        *out = &context->ID2D1GdiInteropRenderTarget_iface;
        return S_OK;
    }
    else if (IsEqualGUID(iid, &IID_ID2D1PrivateCompositorDeviceContext))
    {
        ID2D1DeviceContext6_AddRef(&context->ID2D1DeviceContext6_iface);
        *out = &context->ID2D1PrivateCompositorDeviceContext_iface;
        return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid));

    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d2d_device_context_inner_AddRef(IUnknown *iface)
{
    struct d2d_device_context *context = impl_from_IUnknown(iface);
    ULONG refcount = InterlockedIncrement(&context->refcount);

    TRACE("%p increasing refcount to %lu.\n", iface, refcount);

    return refcount;
}

static ULONG STDMETHODCALLTYPE d2d_device_context_inner_Release(IUnknown *iface)
{
    struct d2d_device_context *context = impl_from_IUnknown(iface);
    ULONG refcount = InterlockedDecrement(&context->refcount);

    TRACE("%p decreasing refcount to %lu.\n", iface, refcount);

    if (!refcount)
    {
        unsigned int i, j, k;

        d2d_clip_stack_cleanup(&context->clip_stack);
        if (context->last_svg_doc)
            IUnknown_Release((IUnknown *)context->last_svg_doc);
        IDWriteRenderingParams_Release(context->default_text_rendering_params);
        if (context->text_rendering_params)
            IDWriteRenderingParams_Release(context->text_rendering_params);
        if (context->bs)
            ID3D11BlendState_Release(context->bs);
        d2d_device_context_release_msaa_resources(context);
        if (context->shadow_cb)
            ID3D11Buffer_Release(context->shadow_cb);
        if (context->shadow_v_ps)
            ID3D11PixelShader_Release(context->shadow_v_ps);
        if (context->shadow_h_ps)
            ID3D11PixelShader_Release(context->shadow_h_ps);
        if (context->blit_ps)
            ID3D11PixelShader_Release(context->blit_ps);
        if (context->blit_vs)
            ID3D11VertexShader_Release(context->blit_vs);
        if (context->rs_msaa)
            ID3D11RasterizerState_Release(context->rs_msaa);
        ID3D11RasterizerState_Release(context->rs);
        ID3D11Buffer_Release(context->vb);
        ID3D11Buffer_Release(context->ib);
        ID3D11Buffer_Release(context->ps_cb);
        ID3D11PixelShader_Release(context->ps);
        ID3D11Buffer_Release(context->vs_cb);
        for (i = 0; i < D2D_SHAPE_TYPE_COUNT; ++i)
        {
            ID3D11VertexShader_Release(context->shape_resources[i].vs);
            ID3D11InputLayout_Release(context->shape_resources[i].il);
        }
        for (i = 0; i < D2D_SAMPLER_INTERPOLATION_MODE_COUNT; ++i)
        {
            for (j = 0; j < D2D_SAMPLER_EXTEND_MODE_COUNT; ++j)
            {
                for (k = 0; k < D2D_SAMPLER_EXTEND_MODE_COUNT; ++k)
                {
                    if (context->sampler_states[i][j][k])
                        ID3D11SamplerState_Release(context->sampler_states[i][j][k]);
                }
            }
        }
        if (context->d3d_state)
            ID3DDeviceContextState_Release(context->d3d_state);
        if (context->target.object)
            IUnknown_Release(context->target.object);
        ID3D11Device1_Release(context->d3d_device);
        ID2D1Factory_Release(context->factory);
        ID2D1Device6_Release(&context->device->ID2D1Device6_iface);
        d2d_device_indexed_objects_clear(&context->vertex_buffers);
        free(context);
    }

    return refcount;
}

static const struct IUnknownVtbl d2d_device_context_inner_unknown_vtbl =
{
    d2d_device_context_inner_QueryInterface,
    d2d_device_context_inner_AddRef,
    d2d_device_context_inner_Release,
};

static HRESULT STDMETHODCALLTYPE d2d_device_context_QueryInterface(ID2D1DeviceContext6 *iface,
        REFIID iid, void **out)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);

    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    return IUnknown_QueryInterface(context->outer_unknown, iid, out);
}

static ULONG STDMETHODCALLTYPE d2d_device_context_AddRef(ID2D1DeviceContext6 *iface)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);

    TRACE("iface %p.\n", iface);

    return IUnknown_AddRef(context->outer_unknown);
}

static ULONG STDMETHODCALLTYPE d2d_device_context_Release(ID2D1DeviceContext6 *iface)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);

    TRACE("iface %p.\n", iface);

    return IUnknown_Release(context->outer_unknown);
}

static void STDMETHODCALLTYPE d2d_device_context_GetFactory(ID2D1DeviceContext6 *iface, ID2D1Factory **factory)
{
    struct d2d_device_context *render_target = impl_from_ID2D1DeviceContext(iface);

    TRACE("iface %p, factory %p.\n", iface, factory);

    *factory = render_target->factory;
    ID2D1Factory_AddRef(*factory);
}

static HRESULT STDMETHODCALLTYPE d2d_device_context_CreateBitmap(ID2D1DeviceContext6 *iface,
        D2D1_SIZE_U size, const void *src_data, UINT32 pitch, const D2D1_BITMAP_PROPERTIES *desc, ID2D1Bitmap **bitmap)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);
    D2D1_BITMAP_PROPERTIES1 bitmap_desc;
    struct d2d_bitmap *object;
    HRESULT hr;

    TRACE("iface %p, size {%u, %u}, src_data %p, pitch %u, desc %p, bitmap %p.\n",
            iface, size.width, size.height, src_data, pitch, desc, bitmap);

    if (desc)
    {
        memcpy(&bitmap_desc, desc, sizeof(*desc));
        bitmap_desc.bitmapOptions = 0;
        bitmap_desc.colorContext = NULL;
    }

    if (SUCCEEDED(hr = d2d_bitmap_create(context, size, src_data, pitch, desc ? &bitmap_desc : NULL, &object)))
        *bitmap = (ID2D1Bitmap *)&object->ID2D1Bitmap1_iface;

    return hr;
}

static HRESULT STDMETHODCALLTYPE d2d_device_context_CreateBitmapFromWicBitmap(ID2D1DeviceContext6 *iface,
        IWICBitmapSource *bitmap_source, const D2D1_BITMAP_PROPERTIES *desc, ID2D1Bitmap **bitmap)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);
    D2D1_BITMAP_PROPERTIES1 bitmap_desc;
    struct d2d_bitmap *object;
    HRESULT hr;

    TRACE("iface %p, bitmap_source %p, desc %p, bitmap %p.\n",
            iface, bitmap_source, desc, bitmap);

    if (desc)
    {
        memcpy(&bitmap_desc, desc, sizeof(*desc));
        bitmap_desc.bitmapOptions = 0;
        bitmap_desc.colorContext = NULL;
    }

    if (SUCCEEDED(hr = d2d_bitmap_create_from_wic_bitmap(context, bitmap_source, desc ? &bitmap_desc : NULL, &object)))
        *bitmap = (ID2D1Bitmap *)&object->ID2D1Bitmap1_iface;

    return hr;
}

static HRESULT STDMETHODCALLTYPE d2d_device_context_CreateSharedBitmap(ID2D1DeviceContext6 *iface,
        REFIID iid, void *data, const D2D1_BITMAP_PROPERTIES *desc, ID2D1Bitmap **bitmap)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);
    D2D1_BITMAP_PROPERTIES1 bitmap_desc;
    struct d2d_bitmap *object;
    HRESULT hr;

    TRACE("iface %p, iid %s, data %p, desc %p, bitmap %p.\n",
            iface, debugstr_guid(iid), data, desc, bitmap);

    if (desc)
    {
        memcpy(&bitmap_desc, desc, sizeof(*desc));
        if (IsEqualIID(iid, &IID_IDXGISurface) || IsEqualIID(iid, &IID_IDXGISurface1))
            bitmap_desc.bitmapOptions = d2d_get_bitmap_options_for_surface(data);
        else
            bitmap_desc.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
        bitmap_desc.colorContext = NULL;
    }

    if (SUCCEEDED(hr = d2d_bitmap_create_shared(context, iid, data, desc ? &bitmap_desc : NULL, &object)))
        *bitmap = (ID2D1Bitmap *)&object->ID2D1Bitmap1_iface;

    return hr;
}

static HRESULT STDMETHODCALLTYPE d2d_device_context_CreateBitmapBrush(ID2D1DeviceContext6 *iface,
        ID2D1Bitmap *bitmap, const D2D1_BITMAP_BRUSH_PROPERTIES *bitmap_brush_desc,
        const D2D1_BRUSH_PROPERTIES *brush_desc, ID2D1BitmapBrush **brush)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);
    struct d2d_brush *object;
    HRESULT hr;

    TRACE("iface %p, bitmap %p, bitmap_brush_desc %p, brush_desc %p, brush %p.\n",
            iface, bitmap, bitmap_brush_desc, brush_desc, brush);

    if (SUCCEEDED(hr = d2d_bitmap_brush_create(context->factory, bitmap, (const D2D1_BITMAP_BRUSH_PROPERTIES1 *)bitmap_brush_desc,
            brush_desc, &object)))
        *brush = (ID2D1BitmapBrush *)&object->ID2D1Brush_iface;

    return hr;
}

static HRESULT STDMETHODCALLTYPE d2d_device_context_CreateSolidColorBrush(ID2D1DeviceContext6 *iface,
        const D2D1_COLOR_F *color, const D2D1_BRUSH_PROPERTIES *desc, ID2D1SolidColorBrush **brush)
{
    struct d2d_device_context *render_target = impl_from_ID2D1DeviceContext(iface);
    struct d2d_brush *object;
    HRESULT hr;

    TRACE("iface %p, color %p, desc %p, brush %p.\n", iface, color, desc, brush);

    if (SUCCEEDED(hr = d2d_solid_color_brush_create(render_target->factory, color, desc, &object)))
        *brush = (ID2D1SolidColorBrush *)&object->ID2D1Brush_iface;

    return hr;
}

static HRESULT STDMETHODCALLTYPE d2d_device_context_CreateGradientStopCollection(ID2D1DeviceContext6 *iface,
        const D2D1_GRADIENT_STOP *stops, UINT32 stop_count, D2D1_GAMMA gamma, D2D1_EXTEND_MODE extend_mode,
        ID2D1GradientStopCollection **gradient)
{
    struct d2d_device_context *render_target = impl_from_ID2D1DeviceContext(iface);
    struct d2d_gradient *object;
    HRESULT hr;

    TRACE("iface %p, stops %p, stop_count %u, gamma %#x, extend_mode %#x, gradient %p.\n",
            iface, stops, stop_count, gamma, extend_mode, gradient);

    if (SUCCEEDED(hr = d2d_gradient_create(render_target->factory, render_target->d3d_device,
            stops, stop_count, gamma, extend_mode, &object)))
        *gradient = &object->ID2D1GradientStopCollection_iface;

    return hr;
}

static HRESULT STDMETHODCALLTYPE d2d_device_context_CreateLinearGradientBrush(ID2D1DeviceContext6 *iface,
        const D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES *gradient_brush_desc, const D2D1_BRUSH_PROPERTIES *brush_desc,
        ID2D1GradientStopCollection *gradient, ID2D1LinearGradientBrush **brush)
{
    struct d2d_device_context *render_target = impl_from_ID2D1DeviceContext(iface);
    struct d2d_brush *object;
    HRESULT hr;

    TRACE("iface %p, gradient_brush_desc %p, brush_desc %p, gradient %p, brush %p.\n",
            iface, gradient_brush_desc, brush_desc, gradient, brush);

    if (SUCCEEDED(hr = d2d_linear_gradient_brush_create(render_target->factory, gradient_brush_desc, brush_desc,
        gradient, &object)))
        *brush = (ID2D1LinearGradientBrush *)&object->ID2D1Brush_iface;

    return hr;
}

static HRESULT STDMETHODCALLTYPE d2d_device_context_CreateRadialGradientBrush(ID2D1DeviceContext6 *iface,
        const D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES *gradient_brush_desc, const D2D1_BRUSH_PROPERTIES *brush_desc,
        ID2D1GradientStopCollection *gradient, ID2D1RadialGradientBrush **brush)
{
    struct d2d_device_context *render_target = impl_from_ID2D1DeviceContext(iface);
    struct d2d_brush *object;
    HRESULT hr;

    TRACE("iface %p, gradient_brush_desc %p, brush_desc %p, gradient %p, brush %p.\n",
            iface, gradient_brush_desc, brush_desc, gradient, brush);

    if (SUCCEEDED(hr = d2d_radial_gradient_brush_create(render_target->factory,
            gradient_brush_desc, brush_desc, gradient, &object)))
        *brush = (ID2D1RadialGradientBrush *)&object->ID2D1Brush_iface;

    return hr;
}

static HRESULT STDMETHODCALLTYPE d2d_device_context_CreateCompatibleRenderTarget(ID2D1DeviceContext6 *iface,
        const D2D1_SIZE_F *size, const D2D1_SIZE_U *pixel_size, const D2D1_PIXEL_FORMAT *format,
        D2D1_COMPATIBLE_RENDER_TARGET_OPTIONS options, ID2D1BitmapRenderTarget **rt)
{
    struct d2d_device_context *render_target = impl_from_ID2D1DeviceContext(iface);
    struct d2d_bitmap_render_target *object;
    HRESULT hr;

    TRACE("iface %p, size %p, pixel_size %p, format %p, options %#x, render_target %p.\n",
            iface, size, pixel_size, format, options, rt);

    if (!(object = calloc(1, sizeof(*object))))
        return E_OUTOFMEMORY;

    if (FAILED(hr = d2d_bitmap_render_target_init(object, render_target, size, pixel_size,
            format, options)))
    {
        WARN("Failed to initialise render target, hr %#lx.\n", hr);
        free(object);
        return hr;
    }

    TRACE("Created render target %p.\n", object);
    *rt = &object->ID2D1BitmapRenderTarget_iface;

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d2d_device_context_CreateLayer(ID2D1DeviceContext6 *iface,
        const D2D1_SIZE_F *size, ID2D1Layer **layer)
{
    struct d2d_device_context *render_target = impl_from_ID2D1DeviceContext(iface);
    struct d2d_layer *object;
    HRESULT hr;

    TRACE("iface %p, size %p, layer %p.\n", iface, size, layer);

    if (SUCCEEDED(hr = d2d_layer_create(render_target->factory, size, &object)))
        *layer = &object->ID2D1Layer_iface;

    return hr;
}

static HRESULT STDMETHODCALLTYPE d2d_device_context_CreateMesh(ID2D1DeviceContext6 *iface, ID2D1Mesh **mesh)
{
    struct d2d_device_context *render_target = impl_from_ID2D1DeviceContext(iface);
    struct d2d_mesh *object;
    HRESULT hr;

    TRACE("iface %p, mesh %p.\n", iface, mesh);

    if (SUCCEEDED(hr = d2d_mesh_create(render_target->factory, &object)))
        *mesh = &object->ID2D1Mesh_iface;

    return hr;
}

static void STDMETHODCALLTYPE d2d_device_context_DrawLine(ID2D1DeviceContext6 *iface,
        D2D1_POINT_2F p0, D2D1_POINT_2F p1, ID2D1Brush *brush, float stroke_width, ID2D1StrokeStyle *stroke_style)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);
    ID2D1PathGeometry *geometry;
    ID2D1GeometrySink *sink;
    HRESULT hr;

    TRACE("iface %p, p0 %s, p1 %s, brush %p, stroke_width %.8e, stroke_style %p.\n",
            iface, debug_d2d_point_2f(&p0), debug_d2d_point_2f(&p1), brush, stroke_width, stroke_style);

    if (FAILED(context->error.code))
        return;

    if (context->target.type == D2D_TARGET_COMMAND_LIST)
    {
        d2d_command_list_draw_line(context->target.command_list, context, p0, p1, brush, stroke_width, stroke_style);
        return;
    }

    if (FAILED(hr = ID2D1Factory_CreatePathGeometry(context->factory, &geometry)))
    {
        WARN("Failed to create path geometry, hr %#lx.\n", hr);
        return;
    }

    if (FAILED(hr = ID2D1PathGeometry_Open(geometry, &sink)))
    {
        WARN("Failed to open geometry sink, hr %#lx.\n", hr);
        ID2D1PathGeometry_Release(geometry);
        return;
    }

    ID2D1GeometrySink_BeginFigure(sink, p0, D2D1_FIGURE_BEGIN_HOLLOW);
    ID2D1GeometrySink_AddLine(sink, p1);
    ID2D1GeometrySink_EndFigure(sink, D2D1_FIGURE_END_OPEN);
    if (FAILED(hr = ID2D1GeometrySink_Close(sink)))
        WARN("Failed to close geometry sink, hr %#lx.\n", hr);
    ID2D1GeometrySink_Release(sink);

    ID2D1DeviceContext6_DrawGeometry(iface, (ID2D1Geometry *)geometry, brush, stroke_width, stroke_style);
    ID2D1PathGeometry_Release(geometry);
}

static void STDMETHODCALLTYPE d2d_device_context_DrawRectangle(ID2D1DeviceContext6 *iface,
        const D2D1_RECT_F *rect, ID2D1Brush *brush, float stroke_width, ID2D1StrokeStyle *stroke_style)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);
    ID2D1RectangleGeometry *geometry;
    HRESULT hr;

    TRACE("iface %p, rect %s, brush %p, stroke_width %.8e, stroke_style %p.\n",
            iface, debug_d2d_rect_f(rect), brush, stroke_width, stroke_style);

    if (FAILED(context->error.code))
        return;

    if (context->target.type == D2D_TARGET_COMMAND_LIST)
    {
        d2d_command_list_draw_rectangle(context->target.command_list, context, rect, brush, stroke_width, stroke_style);
        return;
    }

    if (FAILED(hr = ID2D1Factory_CreateRectangleGeometry(context->factory, rect, &geometry)))
    {
        ERR("Failed to create geometry, hr %#lx.\n", hr);
        return;
    }

    ID2D1DeviceContext6_DrawGeometry(iface, (ID2D1Geometry *)geometry, brush, stroke_width, stroke_style);
    ID2D1RectangleGeometry_Release(geometry);
}

static void STDMETHODCALLTYPE d2d_device_context_FillRectangle(ID2D1DeviceContext6 *iface,
        const D2D1_RECT_F *rect, ID2D1Brush *brush)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);
    ID2D1RectangleGeometry *geometry;
    HRESULT hr;

    TRACE("iface %p, rect %s, brush %p.\n", iface, debug_d2d_rect_f(rect), brush);

    if (FAILED(context->error.code))
        return;

    if (context->target.type == D2D_TARGET_COMMAND_LIST)
    {
        d2d_command_list_fill_rectangle(context->target.command_list, context, rect, brush);
        return;
    }

    if (FAILED(hr = ID2D1Factory_CreateRectangleGeometry(context->factory, rect, &geometry)))
    {
        ERR("Failed to create geometry, hr %#lx.\n", hr);
        return;
    }

    ID2D1DeviceContext6_FillGeometry(iface, (ID2D1Geometry *)geometry, brush, NULL);
    ID2D1RectangleGeometry_Release(geometry);
}

static void STDMETHODCALLTYPE d2d_device_context_DrawRoundedRectangle(ID2D1DeviceContext6 *iface,
        const D2D1_ROUNDED_RECT *rect, ID2D1Brush *brush, float stroke_width, ID2D1StrokeStyle *stroke_style)
{
    struct d2d_device_context *render_target = impl_from_ID2D1DeviceContext(iface);
    ID2D1RoundedRectangleGeometry *geometry;
    HRESULT hr;

    TRACE("iface %p, rect %p, brush %p, stroke_width %.8e, stroke_style %p.\n",
            iface, rect, brush, stroke_width, stroke_style);

    if (FAILED(render_target->error.code))
        return;

    if (FAILED(hr = ID2D1Factory_CreateRoundedRectangleGeometry(render_target->factory, rect, &geometry)))
    {
        ERR("Failed to create geometry, hr %#lx.\n", hr);
        return;
    }

    ID2D1DeviceContext6_DrawGeometry(iface, (ID2D1Geometry *)geometry, brush, stroke_width, stroke_style);
    ID2D1RoundedRectangleGeometry_Release(geometry);
}

static void STDMETHODCALLTYPE d2d_device_context_FillRoundedRectangle(ID2D1DeviceContext6 *iface,
        const D2D1_ROUNDED_RECT *rect, ID2D1Brush *brush)
{
    struct d2d_device_context *render_target = impl_from_ID2D1DeviceContext(iface);
    ID2D1RoundedRectangleGeometry *geometry;
    HRESULT hr;

    TRACE("iface %p, rect %p, brush %p.\n", iface, rect, brush);

    if (FAILED(render_target->error.code))
        return;

    if (FAILED(hr = ID2D1Factory_CreateRoundedRectangleGeometry(render_target->factory, rect, &geometry)))
    {
        ERR("Failed to create geometry, hr %#lx.\n", hr);
        return;
    }

    ID2D1DeviceContext6_FillGeometry(iface, (ID2D1Geometry *)geometry, brush, NULL);
    ID2D1RoundedRectangleGeometry_Release(geometry);
}

static void STDMETHODCALLTYPE d2d_device_context_DrawEllipse(ID2D1DeviceContext6 *iface,
        const D2D1_ELLIPSE *ellipse, ID2D1Brush *brush, float stroke_width, ID2D1StrokeStyle *stroke_style)
{
    struct d2d_device_context *render_target = impl_from_ID2D1DeviceContext(iface);
    ID2D1EllipseGeometry *geometry;
    HRESULT hr;

    TRACE("iface %p, ellipse %p, brush %p, stroke_width %.8e, stroke_style %p.\n",
            iface, ellipse, brush, stroke_width, stroke_style);

    if (FAILED(render_target->error.code))
        return;

    if (FAILED(hr = ID2D1Factory_CreateEllipseGeometry(render_target->factory, ellipse, &geometry)))
    {
        ERR("Failed to create geometry, hr %#lx.\n", hr);
        return;
    }

    ID2D1DeviceContext6_DrawGeometry(iface, (ID2D1Geometry *)geometry, brush, stroke_width, stroke_style);
    ID2D1EllipseGeometry_Release(geometry);
}

static void STDMETHODCALLTYPE d2d_device_context_FillEllipse(ID2D1DeviceContext6 *iface,
        const D2D1_ELLIPSE *ellipse, ID2D1Brush *brush)
{
    struct d2d_device_context *render_target = impl_from_ID2D1DeviceContext(iface);
    ID2D1EllipseGeometry *geometry;
    HRESULT hr;

    TRACE("iface %p, ellipse %p, brush %p.\n", iface, ellipse, brush);

    if (FAILED(render_target->error.code))
        return;

    if (FAILED(hr = ID2D1Factory_CreateEllipseGeometry(render_target->factory, ellipse, &geometry)))
    {
        ERR("Failed to create geometry, hr %#lx.\n", hr);
        return;
    }

    ID2D1DeviceContext6_FillGeometry(iface, (ID2D1Geometry *)geometry, brush, NULL);
    ID2D1EllipseGeometry_Release(geometry);
}

static HRESULT d2d_device_context_update_ps_cb(struct d2d_device_context *context,
        struct d2d_brush *brush, struct d2d_brush *opacity_brush, BOOL outline, BOOL is_arc)
{
    D3D11_MAPPED_SUBRESOURCE map_desc;
    ID3D11DeviceContext *d3d_context;
    struct d2d_ps_cb *cb_data;
    HRESULT hr;

    ID3D11Device1_GetImmediateContext(context->d3d_device, &d3d_context);

    if (FAILED(hr = ID3D11DeviceContext_Map(d3d_context, (ID3D11Resource *)context->ps_cb,
            0, D3D11_MAP_WRITE_DISCARD, 0, &map_desc)))
    {
        WARN("Failed to map constant buffer, hr %#lx.\n", hr);
        ID3D11DeviceContext_Release(d3d_context);
        return hr;
    }

    cb_data = map_desc.pData;
    cb_data->outline = outline;
    cb_data->is_arc = is_arc;
    cb_data->antialias = (context->drawing_state.antialiasMode != D2D1_ANTIALIAS_MODE_ALIASED);
    cb_data->pad = 0;
    if (!d2d_brush_fill_cb(brush, &cb_data->colour_brush))
        WARN("Failed to initialize colour brush buffer.\n");
    if (!d2d_brush_fill_cb(opacity_brush, &cb_data->opacity_brush))
        WARN("Failed to initialize opacity brush buffer.\n");

    ID3D11DeviceContext_Unmap(d3d_context, (ID3D11Resource *)context->ps_cb, 0);
    ID3D11DeviceContext_Release(d3d_context);

    return hr;
}

static HRESULT d2d_device_context_update_vs_cb(struct d2d_device_context *context,
        const D2D_MATRIX_3X2_F *geometry_transform, float stroke_width)
{
    D3D11_MAPPED_SUBRESOURCE map_desc;
    ID3D11DeviceContext *d3d_context;
    const D2D1_MATRIX_3X2_F *w;
    struct d2d_vs_cb *cb_data;
    float tmp_x, tmp_y;
    HRESULT hr;

    ID3D11Device1_GetImmediateContext(context->d3d_device, &d3d_context);

    if (FAILED(hr = ID3D11DeviceContext_Map(d3d_context, (ID3D11Resource *)context->vs_cb,
            0, D3D11_MAP_WRITE_DISCARD, 0, &map_desc)))
    {
        WARN("Failed to map constant buffer, hr %#lx.\n", hr);
        ID3D11DeviceContext_Release(d3d_context);
        return hr;
    }

    cb_data = map_desc.pData;
    cb_data->transform_geometry._11 = geometry_transform->_11;
    cb_data->transform_geometry._21 = geometry_transform->_21;
    cb_data->transform_geometry._31 = geometry_transform->_31;
    cb_data->transform_geometry.pad0 = 0.0f;
    cb_data->transform_geometry._12 = geometry_transform->_12;
    cb_data->transform_geometry._22 = geometry_transform->_22;
    cb_data->transform_geometry._32 = geometry_transform->_32;
    cb_data->transform_geometry.stroke_width = stroke_width;

    w = &context->drawing_state.transform;

    tmp_x = context->desc.dpiX / 96.0f;
    cb_data->transform_rtx.x = w->_11 * tmp_x;
    cb_data->transform_rtx.y = w->_21 * tmp_x;
    cb_data->transform_rtx.z = w->_31 * tmp_x;
    cb_data->transform_rtx.w = 2.0f / context->pixel_size.width;

    tmp_y = context->desc.dpiY / 96.0f;
    cb_data->transform_rty.x = w->_12 * tmp_y;
    cb_data->transform_rty.y = w->_22 * tmp_y;
    cb_data->transform_rty.z = w->_32 * tmp_y;
    cb_data->transform_rty.w = -2.0f / context->pixel_size.height;

    ID3D11DeviceContext_Unmap(d3d_context, (ID3D11Resource *)context->vs_cb, 0);
    ID3D11DeviceContext_Release(d3d_context);

    return S_OK;
}

struct d2d_dash_path
{
    D2D1_POINT_2F *points;
    size_t points_size;
    size_t count;
    BOOL closed;
};

struct d2d_dash_capture
{
    ID2D1GeometrySink ID2D1GeometrySink_iface;
    struct d2d_dash_path *paths;
    size_t paths_size;
    size_t path_count;
    D2D1_POINT_2F current;
};

static inline struct d2d_dash_capture *d2d_dash_capture_from_ID2D1GeometrySink(ID2D1GeometrySink *iface)
{
    return CONTAINING_RECORD(iface, struct d2d_dash_capture, ID2D1GeometrySink_iface);
}

static struct d2d_dash_path *d2d_dash_capture_add_path(struct d2d_dash_capture *capture)
{
    struct d2d_dash_path *path;

    if (!d2d_array_reserve((void **)&capture->paths, &capture->paths_size,
            capture->path_count + 1, sizeof(*capture->paths)))
    {
        WARN("Failed to grow dash path array.\n");
        return NULL;
    }
    path = &capture->paths[capture->path_count++];
    memset(path, 0, sizeof(*path));
    return path;
}

static void d2d_dash_capture_cleanup(struct d2d_dash_capture *capture)
{
    size_t i;

    for (i = 0; i < capture->path_count; ++i)
        free(capture->paths[i].points);
    free(capture->paths);
    capture->paths = NULL;
    capture->paths_size = 0;
    capture->path_count = 0;
}

static HRESULT STDMETHODCALLTYPE d2d_dash_capture_QueryInterface(ID2D1GeometrySink *iface,
        REFIID iid, void **out)
{
    if (IsEqualGUID(iid, &IID_ID2D1GeometrySink) || IsEqualGUID(iid, &IID_IUnknown))
    {
        *out = iface;
        return S_OK;
    }

    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d2d_dash_capture_AddRef(ID2D1GeometrySink *iface)
{
    return 1;
}

static ULONG STDMETHODCALLTYPE d2d_dash_capture_Release(ID2D1GeometrySink *iface)
{
    return 1;
}

static void STDMETHODCALLTYPE d2d_dash_capture_SetFillMode(ID2D1GeometrySink *iface, D2D1_FILL_MODE fill_mode)
{
}

static void STDMETHODCALLTYPE d2d_dash_capture_SetSegmentSettings(ID2D1GeometrySink *iface,
        D2D1_PATH_SEGMENT settings)
{
}

static struct d2d_dash_path *d2d_dash_capture_current_path(struct d2d_dash_capture *capture)
{
    return capture->path_count ? &capture->paths[capture->path_count - 1] : NULL;
}

static void d2d_dash_path_add_point(struct d2d_dash_path *path, const D2D1_POINT_2F *p)
{
    const D2D1_POINT_2F *last;

    if (!path)
        return;

    last = path->count ? &path->points[path->count - 1] : NULL;
    if (last && fabsf(last->x - p->x) < 1e-6f && fabsf(last->y - p->y) < 1e-6f)
        return;

    if (!d2d_array_reserve((void **)&path->points, &path->points_size,
            path->count + 1, sizeof(*path->points)))
    {
        WARN("Failed to grow dash path.\n");
        return;
    }
    path->points[path->count++] = *p;
}

static void d2d_dash_capture_add_point(struct d2d_dash_capture *capture, const D2D1_POINT_2F *p)
{
    d2d_dash_path_add_point(d2d_dash_capture_current_path(capture), p);
    capture->current = *p;
}

static void STDMETHODCALLTYPE d2d_dash_capture_BeginFigure(ID2D1GeometrySink *iface,
        D2D1_POINT_2F start_point, D2D1_FIGURE_BEGIN figure_begin)
{
    struct d2d_dash_capture *capture = d2d_dash_capture_from_ID2D1GeometrySink(iface);
    struct d2d_dash_path *path;

    if (!(path = d2d_dash_capture_add_path(capture)))
        return;
    capture->current = start_point;
    d2d_dash_path_add_point(path, &start_point);
}

static void STDMETHODCALLTYPE d2d_dash_capture_EndFigure(ID2D1GeometrySink *iface, D2D1_FIGURE_END figure_end)
{
    struct d2d_dash_capture *capture = d2d_dash_capture_from_ID2D1GeometrySink(iface);
    struct d2d_dash_path *path = d2d_dash_capture_current_path(capture);

    if (path)
        path->closed = (figure_end == D2D1_FIGURE_END_CLOSED);
}

static void STDMETHODCALLTYPE d2d_dash_capture_AddLine(ID2D1GeometrySink *iface, D2D1_POINT_2F point)
{
    struct d2d_dash_capture *capture = d2d_dash_capture_from_ID2D1GeometrySink(iface);

    d2d_dash_capture_add_point(capture, &point);
}

static void STDMETHODCALLTYPE d2d_dash_capture_AddLines(ID2D1GeometrySink *iface,
        const D2D1_POINT_2F *points, UINT32 count)
{
    struct d2d_dash_capture *capture = d2d_dash_capture_from_ID2D1GeometrySink(iface);
    UINT32 i;

    for (i = 0; i < count; ++i)
        d2d_dash_capture_add_point(capture, &points[i]);
}

static void d2d_dash_capture_cubic(struct d2d_dash_capture *capture,
        const D2D1_POINT_2F *p0, const D2D1_POINT_2F *p1, const D2D1_POINT_2F *p2, const D2D1_POINT_2F *p3)
{
    D2D1_POINT_2F q;
    unsigned int i;
    float t, u;

    for (i = 1; i <= 8; ++i)
    {
        t = i / 8.0f;
        u = 1.0f - t;
        q.x = u * u * u * p0->x + 3.0f * u * u * t * p1->x + 3.0f * u * t * t * p2->x + t * t * t * p3->x;
        q.y = u * u * u * p0->y + 3.0f * u * u * t * p1->y + 3.0f * u * t * t * p2->y + t * t * t * p3->y;
        d2d_dash_capture_add_point(capture, &q);
    }
}

static void STDMETHODCALLTYPE d2d_dash_capture_AddBezier(ID2D1GeometrySink *iface,
        const D2D1_BEZIER_SEGMENT *bezier)
{
    struct d2d_dash_capture *capture = d2d_dash_capture_from_ID2D1GeometrySink(iface);

    d2d_dash_capture_cubic(capture, &capture->current, &bezier->point1, &bezier->point2, &bezier->point3);
}

static void STDMETHODCALLTYPE d2d_dash_capture_AddBeziers(ID2D1GeometrySink *iface,
        const D2D1_BEZIER_SEGMENT *beziers, UINT32 count)
{
    struct d2d_dash_capture *capture = d2d_dash_capture_from_ID2D1GeometrySink(iface);
    UINT32 i;

    for (i = 0; i < count; ++i)
        d2d_dash_capture_cubic(capture, &capture->current,
                &beziers[i].point1, &beziers[i].point2, &beziers[i].point3);
}

static void STDMETHODCALLTYPE d2d_dash_capture_AddQuadraticBezier(ID2D1GeometrySink *iface,
        const D2D1_QUADRATIC_BEZIER_SEGMENT *bezier)
{
    struct d2d_dash_capture *capture = d2d_dash_capture_from_ID2D1GeometrySink(iface);
    D2D1_POINT_2F p0 = capture->current, c1, c2;

    c1.x = p0.x + 2.0f / 3.0f * (bezier->point1.x - p0.x);
    c1.y = p0.y + 2.0f / 3.0f * (bezier->point1.y - p0.y);
    c2.x = bezier->point2.x + 2.0f / 3.0f * (bezier->point1.x - bezier->point2.x);
    c2.y = bezier->point2.y + 2.0f / 3.0f * (bezier->point1.y - bezier->point2.y);

    d2d_dash_capture_cubic(capture, &p0, &c1, &c2, &bezier->point2);
}

static void STDMETHODCALLTYPE d2d_dash_capture_AddQuadraticBeziers(ID2D1GeometrySink *iface,
        const D2D1_QUADRATIC_BEZIER_SEGMENT *beziers, UINT32 count)
{
    UINT32 i;

    for (i = 0; i < count; ++i)
        d2d_dash_capture_AddQuadraticBezier(iface, &beziers[i]);
}

static void STDMETHODCALLTYPE d2d_dash_capture_AddArc(ID2D1GeometrySink *iface, const D2D1_ARC_SEGMENT *arc)
{
    static const float pi = 3.14159265358979323846f;
    struct d2d_dash_capture *capture = d2d_dash_capture_from_ID2D1GeometrySink(iface);
    D2D1_POINT_2F p0 = capture->current, p1 = arc->point;
    D2D1_POINT_2F c1, c2, p;
    float phi, cos_phi, sin_phi, rx, ry, rx2, ry2;
    float x1p, y1p, lam, co, cxp, cyp, cx, cy, theta1, dtheta, dt;
    float num, den, k;
    unsigned int quadrants, i;
    BOOL large, cw;

    rx = arc->size.width;
    ry = arc->size.height;
    if (rx <= 0.0f || ry <= 0.0f)
    {
        d2d_dash_capture_add_point(capture, &p1);
        return;
    }

    phi = arc->rotationAngle * pi / 180.0f;
    cos_phi = cosf(phi);
    sin_phi = sinf(phi);

    x1p = cos_phi * (p0.x - p1.x) * 0.5f + sin_phi * (p0.y - p1.y) * 0.5f;
    y1p = -sin_phi * (p0.x - p1.x) * 0.5f + cos_phi * (p0.y - p1.y) * 0.5f;
    rx2 = rx * rx;
    ry2 = ry * ry;

    lam = (x1p * x1p) / rx2 + (y1p * y1p) / ry2;
    if (lam > 1.0f)
    {
        lam = sqrtf(lam);
        rx *= lam;
        ry *= lam;
        rx2 = rx * rx;
        ry2 = ry * ry;
    }

    large = (arc->arcSize == D2D1_ARC_SIZE_LARGE);
    cw = (arc->sweepDirection == D2D1_SWEEP_DIRECTION_CLOCKWISE);

    num = rx2 * ry2 - rx2 * y1p * y1p - ry2 * x1p * x1p;
    den = rx2 * y1p * y1p + ry2 * x1p * x1p;
    co = (den > 0.0f) ? ((large != cw) ? sqrtf(fmaxf(0.0f, num / den)) : -sqrtf(fmaxf(0.0f, num / den))) : 0.0f;
    cxp = co * rx * y1p / ry;
    cyp = -co * ry * x1p / rx;
    cx = cos_phi * cxp - sin_phi * cyp + (p0.x + p1.x) * 0.5f;
    cy = sin_phi * cxp + cos_phi * cyp + (p0.y + p1.y) * 0.5f;

    theta1 = atan2f((y1p - cyp) / ry, (x1p - cxp) / rx);
    dtheta = atan2f((-y1p - cyp) / ry, (-x1p - cxp) / rx) - theta1;
    if (cw)
    {
        while (dtheta < 0.0f)
            dtheta += 2.0f * pi;
    }
    else
    {
        while (dtheta > 0.0f)
            dtheta -= 2.0f * pi;
    }

    quadrants = (unsigned int)ceilf(fabsf(dtheta) / (pi * 0.5f));
    if (!quadrants)
    {
        d2d_dash_capture_add_point(capture, &p1);
        return;
    }
    dt = dtheta / quadrants;
    k = 4.0f / 3.0f * tanf(dt * 0.25f);

    for (i = 0; i < quadrants; ++i)
    {
        float t0 = theta1 + dt * i;
        float t1 = t0 + dt;

        p.x = cx + rx * cosf(t0);
        p.y = cy + ry * sinf(t0);
        c1.x = p.x - k * rx * sinf(t0);
        c1.y = p.y + k * ry * cosf(t0);
        p.x = cx + rx * cosf(t1);
        p.y = cy + ry * sinf(t1);
        c2.x = p.x + k * rx * sinf(t1);
        c2.y = p.y - k * ry * cosf(t1);

        d2d_dash_capture_cubic(capture, &capture->current, &c1, &c2, &p);
    }
}

static HRESULT STDMETHODCALLTYPE d2d_dash_capture_Close(ID2D1GeometrySink *iface)
{
    return S_OK;
}

static const struct ID2D1GeometrySinkVtbl d2d_dash_capture_vtbl =
{
    d2d_dash_capture_QueryInterface,
    d2d_dash_capture_AddRef,
    d2d_dash_capture_Release,
    d2d_dash_capture_SetFillMode,
    d2d_dash_capture_SetSegmentSettings,
    d2d_dash_capture_BeginFigure,
    d2d_dash_capture_AddLines,
    d2d_dash_capture_AddBeziers,
    d2d_dash_capture_EndFigure,
    d2d_dash_capture_Close,
    d2d_dash_capture_AddLine,
    d2d_dash_capture_AddBezier,
    d2d_dash_capture_AddQuadraticBezier,
    d2d_dash_capture_AddQuadraticBeziers,
    d2d_dash_capture_AddArc,
};

struct d2d_dash_mesh
{
    struct d2d_outline_vertex *vertices;
    size_t vertices_size;
    size_t vertex_count;

    struct d2d_face *faces;
    size_t faces_size;
    size_t face_count;
};

static void d2d_dash_mesh_cleanup(struct d2d_dash_mesh *mesh)
{
    free(mesh->vertices);
    free(mesh->faces);
    memset(mesh, 0, sizeof(*mesh));
}

static void d2d_dash_outline_vertex_set(struct d2d_outline_vertex *v,
        float x, float y, float prev_x, float prev_y, float next_x, float next_y)
{
    d2d_point_set(&v->position, x, y);
    d2d_point_set(&v->prev, prev_x, prev_y);
    d2d_point_set(&v->next, next_x, next_y);
}

static void d2d_dash_point_subtract(D2D1_POINT_2F *dst, const D2D1_POINT_2F *p0, const D2D1_POINT_2F *p1)
{
    dst->x = p0->x - p1->x;
    dst->y = p0->y - p1->y;
}

static BOOL d2d_dash_point_normalise(D2D1_POINT_2F *p)
{
    float l = sqrtf(p->x * p->x + p->y * p->y);

    if (!(l > 0.0f))
        return FALSE;
    p->x /= l;
    p->y /= l;
    return TRUE;
}

static BOOL d2d_dash_mesh_reserve(struct d2d_dash_mesh *mesh, size_t vertex_count, size_t face_count)
{
    if (!d2d_array_reserve((void **)&mesh->vertices, &mesh->vertices_size,
            mesh->vertex_count + vertex_count, sizeof(*mesh->vertices)))
        return FALSE;
    if (!d2d_array_reserve((void **)&mesh->faces, &mesh->faces_size,
            mesh->face_count + face_count, sizeof(*mesh->faces)))
        return FALSE;
    return TRUE;
}

static BOOL d2d_dash_mesh_add_line(struct d2d_dash_mesh *mesh,
        const D2D1_POINT_2F *p0, const D2D1_POINT_2F *p1)
{
    struct d2d_outline_vertex *v;
    D2D1_POINT_2F q;
    struct d2d_face *f;
    size_t base_idx;

    d2d_dash_point_subtract(&q, p1, p0);
    if (!d2d_dash_point_normalise(&q))
        return TRUE;

    if (!d2d_dash_mesh_reserve(mesh, 4, 2))
        return FALSE;
    base_idx = mesh->vertex_count;
    v = &mesh->vertices[base_idx];
    f = &mesh->faces[mesh->face_count];

    d2d_dash_outline_vertex_set(&v[0], p0->x, p0->y, q.x, q.y, q.x, q.y);
    d2d_dash_outline_vertex_set(&v[1], p0->x, p0->y, -q.x, -q.y, -q.x, -q.y);
    d2d_dash_outline_vertex_set(&v[2], p1->x, p1->y, q.x, q.y, q.x, q.y);
    d2d_dash_outline_vertex_set(&v[3], p1->x, p1->y, -q.x, -q.y, -q.x, -q.y);
    mesh->vertex_count += 4;

    f[0].v[0] = base_idx + 0;
    f[0].v[1] = base_idx + 1;
    f[0].v[2] = base_idx + 2;
    f[1].v[0] = base_idx + 2;
    f[1].v[1] = base_idx + 1;
    f[1].v[2] = base_idx + 3;
    mesh->face_count += 2;

    return TRUE;
}

static BOOL d2d_dash_mesh_add_join(struct d2d_dash_mesh *mesh,
        const D2D1_POINT_2F *prev, const D2D1_POINT_2F *p0, const D2D1_POINT_2F *next)
{
    struct d2d_outline_vertex *v;
    struct d2d_face *f;
    size_t base_idx;
    float ccw;

    ccw = prev->x * next->y - prev->y * next->x;
    if (ccw == 0.0f && prev->x == next->x && prev->y == next->y)
        return TRUE;

    if (!d2d_dash_mesh_reserve(mesh, 4, 2))
        return FALSE;
    base_idx = mesh->vertex_count;
    v = &mesh->vertices[base_idx];
    f = &mesh->faces[mesh->face_count];

    if (ccw == 0.0f)
    {
        d2d_dash_outline_vertex_set(&v[0], p0->x, p0->y, -prev->x, -prev->y, -prev->x, -prev->y);
        d2d_dash_outline_vertex_set(&v[1], p0->x, p0->y, prev->x, prev->y, prev->x, prev->y);
        d2d_dash_outline_vertex_set(&v[2], p0->x + 25.0f * -prev->x, p0->y + 25.0f * -prev->y,
                prev->x, prev->y, prev->x, prev->y);
        d2d_dash_outline_vertex_set(&v[3], p0->x + 25.0f * -prev->x, p0->y + 25.0f * -prev->y,
                -prev->x, -prev->y, -prev->x, -prev->y);
    }
    else if (ccw < 0.0f)
    {
        d2d_dash_outline_vertex_set(&v[0], p0->x, p0->y, next->x, next->y, -prev->x, -prev->y);
        d2d_dash_outline_vertex_set(&v[1], p0->x, p0->y, -next->x, -next->y, -prev->x, -prev->y);
        d2d_dash_outline_vertex_set(&v[2], p0->x, p0->y, -next->x, -next->y, prev->x, prev->y);
        d2d_dash_outline_vertex_set(&v[3], p0->x, p0->y, prev->x, prev->y, prev->x, prev->y);
    }
    else
    {
        d2d_dash_outline_vertex_set(&v[0], p0->x, p0->y, prev->x, prev->y, -next->x, -next->y);
        d2d_dash_outline_vertex_set(&v[1], p0->x, p0->y, -prev->x, -prev->y, -prev->x, -prev->y);
        d2d_dash_outline_vertex_set(&v[2], p0->x, p0->y, -prev->x, -prev->y, next->x, next->y);
        d2d_dash_outline_vertex_set(&v[3], p0->x, p0->y, next->x, next->y, next->x, next->y);
    }
    mesh->vertex_count += 4;

    f[0].v[0] = base_idx + 1;
    f[0].v[1] = base_idx + 0;
    f[0].v[2] = base_idx + 2;
    f[1].v[0] = base_idx + 2;
    f[1].v[1] = base_idx + 0;
    f[1].v[2] = base_idx + 3;
    mesh->face_count += 2;

    return TRUE;
}

static BOOL d2d_dash_mesh_add_path(struct d2d_dash_mesh *mesh, const D2D1_POINT_2F *points, size_t count)
{
    D2D1_POINT_2F q_prev, q_next;
    size_t i;

    if (count < 2)
        return TRUE;

    if (!d2d_dash_mesh_add_line(mesh, &points[0], &points[1]))
        return FALSE;

    for (i = 2; i < count; ++i)
    {
        d2d_dash_point_subtract(&q_prev, &points[i - 2], &points[i - 1]);
        d2d_dash_point_subtract(&q_next, &points[i], &points[i - 1]);
        if (!d2d_dash_point_normalise(&q_prev) || !d2d_dash_point_normalise(&q_next))
            continue;
        if (q_prev.x == q_next.x && q_prev.y == q_next.y)
            continue;
        if (!d2d_dash_mesh_add_join(mesh, &q_prev, &points[i - 1], &q_next))
            return FALSE;
        if (!d2d_dash_mesh_add_line(mesh, &points[i - 1], &points[i]))
            return FALSE;
    }

    return TRUE;
}

struct d2d_dash_walk
{
    D2D1_POINT_2F *points;
    size_t count;
    size_t seg_count;
    BOOL closed;
    float *cum;
    float total;
};

static void d2d_dash_walk_point(const struct d2d_dash_walk *w, float s, D2D1_POINT_2F *dst)
{
    const D2D1_POINT_2F *p0, *p1;
    size_t seg;
    float t;

    if (w->closed)
    {
        while (s < 0.0f)
            s += w->total;
        while (s >= w->total)
            s -= w->total;
    }
    else
    {
        s = min(s, w->total);
        s = max(s, 0.0f);
    }

    seg = 0;
    while (seg + 1 < w->seg_count && w->cum[seg + 1] <= s)
        ++seg;

    p0 = &w->points[seg];
    p1 = &w->points[(seg + 1) % w->count];
    t = (w->cum[seg + 1] - w->cum[seg] > 0.0f)
            ? (s - w->cum[seg]) / (w->cum[seg + 1] - w->cum[seg]) : 0.0f;
    dst->x = p0->x + t * (p1->x - p0->x);
    dst->y = p0->y + t * (p1->y - p0->y);
}

static void d2d_dash_mesh_add_range(struct d2d_dash_mesh *mesh,
        const struct d2d_dash_walk *w, float a, float b)
{
    size_t count = 0, i, capacity, n;
    D2D1_POINT_2F *pts, *last, q;

    if (b <= a)
        return;

    /* For a closed figure the closing corner (points[0], at arclength total)
     * is a real join, so scan one extra vertex to include it. */
    n = w->closed ? w->seg_count + 1 : w->seg_count;
    capacity = n + 2;
    if (!(pts = malloc(capacity * sizeof(*pts))))
        return;

    d2d_dash_walk_point(w, a, &pts[count++]);

    for (i = 1; i < n && count < capacity; ++i)
    {
        float s = w->cum[i];
        if (w->closed)
        {
            /* Map the corner arclength cyclically into [a, a + total) so a
             * range straddling the seam (a < total < b) picks up the closing
             * corner and the corners just past it. */
            while (s < a) s += w->total;
            while (s >= a + w->total) s -= w->total;
            if (s > b)
                continue;
        }
        else if (s <= a || s > b)
        {
            continue;
        }
        pts[count++] = w->points[i % w->count];
    }

    if (count < capacity)
    {
        last = &pts[count - 1];
        d2d_dash_walk_point(w, b, &q);
        if (fabsf(last->x - q.x) > 1e-6f || fabsf(last->y - q.y) > 1e-6f)
            pts[count++] = q;
    }

    d2d_dash_mesh_add_path(mesh, pts, count);
    free(pts);
}

static void d2d_device_context_draw_dash_mesh(struct d2d_device_context *render_target,
        const D2D_MATRIX_3X2_F *transform, const struct d2d_dash_mesh *mesh,
        struct d2d_brush *brush, float stroke_width)
{
    D3D11_SUBRESOURCE_DATA buffer_data;
    D3D11_BUFFER_DESC buffer_desc;
    ID3D11Buffer *ib, *vb;
    HRESULT hr;

    if (FAILED(hr = d2d_device_context_update_vs_cb(render_target, transform, stroke_width)))
    {
        WARN("Failed to update vs constant buffer, hr %#lx.\n", hr);
        return;
    }

    if (FAILED(hr = d2d_device_context_update_ps_cb(render_target, brush, NULL, TRUE, FALSE)))
    {
        WARN("Failed to update ps constant buffer, hr %#lx.\n", hr);
        return;
    }

    buffer_desc.Usage = D3D11_USAGE_DEFAULT;
    buffer_desc.CPUAccessFlags = 0;
    buffer_desc.MiscFlags = 0;

    buffer_data.SysMemPitch = 0;
    buffer_data.SysMemSlicePitch = 0;

    buffer_desc.ByteWidth = mesh->face_count * sizeof(*mesh->faces);
    buffer_desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    buffer_data.pSysMem = mesh->faces;

    if (FAILED(hr = ID3D11Device1_CreateBuffer(render_target->d3d_device, &buffer_desc, &buffer_data, &ib)))
    {
        WARN("Failed to create index buffer, hr %#lx.\n", hr);
        return;
    }

    buffer_desc.ByteWidth = mesh->vertex_count * sizeof(*mesh->vertices);
    buffer_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    buffer_data.pSysMem = mesh->vertices;

    if (FAILED(hr = ID3D11Device1_CreateBuffer(render_target->d3d_device, &buffer_desc, &buffer_data, &vb)))
    {
        ERR("Failed to create vertex buffer, hr %#lx.\n", hr);
        ID3D11Buffer_Release(ib);
        return;
    }

    d2d_device_context_draw(render_target, D2D_SHAPE_TYPE_OUTLINE, ib, 3 * mesh->face_count, vb,
            sizeof(*mesh->vertices), brush, NULL);

    ID3D11Buffer_Release(vb);
    ID3D11Buffer_Release(ib);
}

static void d2d_device_context_draw_dashed_geometry(struct d2d_device_context *context,
        const struct d2d_geometry *geometry, struct d2d_brush *brush, float stroke_width,
        const struct d2d_stroke_style *style)
{
    const D2D_MATRIX_3X2_F *xform = d2d_geometry_outline_transform(geometry);
    float w_l, unit, period, offset, acc;
    struct d2d_dash_capture capture = {0};
    struct d2d_dash_mesh mesh = {0};
    D2D_MATRIX_3X2_F m;
    size_t path_idx;
    unsigned int it, pk;
    int k;

    capture.ID2D1GeometrySink_iface.lpVtbl = &d2d_dash_capture_vtbl;
    geometry->ops->stream((struct d2d_geometry *)geometry, NULL, &capture.ID2D1GeometrySink_iface);

    m = context->drawing_state.transform;
    d2d_matrix_multiply(&m, xform);
    w_l = sqrtf(fabsf(m._11 * m._22 - m._12 * m._21));
    if (!(w_l > 0.0f))
        w_l = 1.0f;

    /* Dash pattern lengths are multiples of the stroke width, in post-transform
     * units. The tessellation above is in geometry space, so scale by the
     * geometry and render target transforms. */
    unit = w_l = stroke_width / w_l;

    period = 0.0f;
    for (k = 0; k < (int)style->dash_count; ++k)
        period += style->dashes[k] * unit;
    if (!(period > 1e-6f))
        goto done;

    offset = fmodf(style->desc.dashOffset * unit, period);
    if (offset < 0.0f)
        offset += period;

    for (path_idx = 0; path_idx < capture.path_count; ++path_idx)
    {
        struct d2d_dash_path *path = &capture.paths[path_idx];
        struct d2d_dash_walk walk;
        float *cum = NULL;
        size_t i;

        if (path->count < 2)
            continue;

        walk.points = path->points;
        walk.count = path->count;
        walk.closed = path->closed;
        walk.seg_count = path->closed ? path->count : path->count - 1;
        if (!(cum = malloc((walk.seg_count + 1) * sizeof(*cum))))
            break;
        walk.cum = cum;
        cum[0] = 0.0f;
        for (i = 0; i < walk.seg_count; ++i)
        {
            const D2D1_POINT_2F *p0 = &path->points[i];
            const D2D1_POINT_2F *p1 = &path->points[(i + 1) % path->count];
            float dx = p1->x - p0->x, dy = p1->y - p0->y;
            cum[i + 1] = cum[i] + sqrtf(dx * dx + dy * dy);
        }
        walk.total = cum[walk.seg_count];
        if (!(walk.total > 1e-6f))
        {
            free(cum);
            continue;
        }

        acc = 0.0f;
        pk = 0;
        for (it = 0; it < 4096; ++it)
        {
            float seg = style->dashes[pk % style->dash_count] * unit;
            float a = acc - offset, b = acc + seg - offset;
            BOOL is_dash = !(pk & 1);

            if (is_dash && b > 0.0f && a < walk.total + (walk.closed ? period : 0.0f))
            {
                float ext_a, ext_b, a0, b0;

                /* Closed figures: emit the dash as one continuous range. The
                 * walk wraps past the closing corner, so a dash straddling it
                 * joins seamlessly instead of ending in two capped stubs that
                 * leave a miter gap at the corner. */
                if (walk.closed)
                {
                    a0 = a;
                    b0 = b;
                }
                else
                {
                    a0 = max(a, 0.0f);
                    b0 = min(b, walk.total);
                }

                if (b0 > a0)
                {
                    if (seg < 1e-6f)
                        ext_a = ext_b = w_l * 0.5f;
                    else if (style->desc.dashCap == D2D1_CAP_STYLE_SQUARE || style->desc.dashCap == D2D1_CAP_STYLE_ROUND)
                        ext_a = ext_b = w_l * 0.5f;
                    else
                        ext_a = ext_b = 0.0f;

                    if (!walk.closed)
                    {
                        if (a <= 0.0f)
                            ext_a = (style->desc.startCap == D2D1_CAP_STYLE_SQUARE
                                    || style->desc.startCap == D2D1_CAP_STYLE_ROUND) ? w_l * 0.5f : 0.0f;
                        if (b >= walk.total)
                            ext_b = (style->desc.endCap == D2D1_CAP_STYLE_SQUARE
                                    || style->desc.endCap == D2D1_CAP_STYLE_ROUND) ? w_l * 0.5f : 0.0f;
                    }

                    d2d_dash_mesh_add_range(&mesh, &walk, a0 - ext_a, b0 + ext_b);
                }
            }

            acc += seg;
            ++pk;
            if (acc - offset > walk.total + (walk.closed ? period : 0.0f))
                break;
        }

        free(cum);
    }

done:
    if (mesh.face_count)
        d2d_device_context_draw_dash_mesh(context, xform, &mesh, brush, stroke_width);

    d2d_dash_mesh_cleanup(&mesh);
    d2d_dash_capture_cleanup(&capture);
}

static void d2d_device_context_draw_geometry(struct d2d_device_context *render_target,
        const struct d2d_geometry *geometry, struct d2d_brush *brush, float stroke_width)
{
    D3D11_SUBRESOURCE_DATA buffer_data;
    D3D11_BUFFER_DESC buffer_desc;
    ID3D11Buffer *ib, *vb;
    HRESULT hr;

    if (FAILED(hr = d2d_device_context_update_vs_cb(render_target, &geometry->transform, stroke_width)))
    {
        WARN("Failed to update vs constant buffer, hr %#lx.\n", hr);
        return;
    }

    if (FAILED(hr = d2d_device_context_update_ps_cb(render_target, brush, NULL, TRUE, FALSE)))
    {
        WARN("Failed to update ps constant buffer, hr %#lx.\n", hr);
        return;
    }

    buffer_desc.Usage = D3D11_USAGE_DEFAULT;
    buffer_desc.CPUAccessFlags = 0;
    buffer_desc.MiscFlags = 0;

    buffer_data.SysMemPitch = 0;
    buffer_data.SysMemSlicePitch = 0;

    if (geometry->outline.face_count)
    {
        buffer_desc.ByteWidth = geometry->outline.face_count * sizeof(*geometry->outline.faces);
        buffer_desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
        buffer_data.pSysMem = geometry->outline.faces;

        if (FAILED(hr = ID3D11Device1_CreateBuffer(render_target->d3d_device, &buffer_desc, &buffer_data, &ib)))
        {
            WARN("Failed to create index buffer, hr %#lx.\n", hr);
            return;
        }

        buffer_desc.ByteWidth = geometry->outline.vertex_count * sizeof(*geometry->outline.vertices);
        buffer_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        buffer_data.pSysMem = geometry->outline.vertices;

        if (FAILED(hr = ID3D11Device1_CreateBuffer(render_target->d3d_device, &buffer_desc, &buffer_data, &vb)))
        {
            ERR("Failed to create vertex buffer, hr %#lx.\n", hr);
            ID3D11Buffer_Release(ib);
            return;
        }

        d2d_device_context_draw_ex(render_target, D2D_SHAPE_TYPE_OUTLINE, ib, 3 * geometry->outline.face_count, vb,
                sizeof(*geometry->outline.vertices), brush, NULL, FALSE);

        ID3D11Buffer_Release(vb);
        ID3D11Buffer_Release(ib);
    }

    if (geometry->outline.bezier_face_count)
    {
        buffer_desc.ByteWidth = geometry->outline.bezier_face_count * sizeof(*geometry->outline.bezier_faces);
        buffer_desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
        buffer_data.pSysMem = geometry->outline.bezier_faces;

        if (FAILED(hr = ID3D11Device1_CreateBuffer(render_target->d3d_device, &buffer_desc, &buffer_data, &ib)))
        {
            WARN("Failed to create curves index buffer, hr %#lx.\n", hr);
            return;
        }

        buffer_desc.ByteWidth = geometry->outline.bezier_count * sizeof(*geometry->outline.beziers);
        buffer_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        buffer_data.pSysMem = geometry->outline.beziers;

        if (FAILED(hr = ID3D11Device1_CreateBuffer(render_target->d3d_device, &buffer_desc, &buffer_data, &vb)))
        {
            ERR("Failed to create curves vertex buffer, hr %#lx.\n", hr);
            ID3D11Buffer_Release(ib);
            return;
        }

        d2d_device_context_draw_ex(render_target, D2D_SHAPE_TYPE_BEZIER_OUTLINE, ib,
                3 * geometry->outline.bezier_face_count, vb,
                sizeof(*geometry->outline.beziers), brush, NULL, FALSE);

        ID3D11Buffer_Release(vb);
        ID3D11Buffer_Release(ib);
    }

    if (geometry->outline.arc_face_count)
    {
        buffer_desc.ByteWidth = geometry->outline.arc_face_count * sizeof(*geometry->outline.arc_faces);
        buffer_desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
        buffer_data.pSysMem = geometry->outline.arc_faces;

        if (FAILED(hr = ID3D11Device1_CreateBuffer(render_target->d3d_device, &buffer_desc, &buffer_data, &ib)))
        {
            WARN("Failed to create arcs index buffer, hr %#lx.\n", hr);
            return;
        }

        buffer_desc.ByteWidth = geometry->outline.arc_count * sizeof(*geometry->outline.arcs);
        buffer_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        buffer_data.pSysMem = geometry->outline.arcs;

        if (FAILED(hr = ID3D11Device1_CreateBuffer(render_target->d3d_device, &buffer_desc, &buffer_data, &vb)))
        {
            ERR("Failed to create arcs vertex buffer, hr %#lx.\n", hr);
            ID3D11Buffer_Release(ib);
            return;
        }

        if (SUCCEEDED(d2d_device_context_update_ps_cb(render_target, brush, NULL, TRUE, TRUE)))
            d2d_device_context_draw_ex(render_target, D2D_SHAPE_TYPE_ARC_OUTLINE, ib,
                    3 * geometry->outline.arc_face_count, vb,
                    sizeof(*geometry->outline.arcs), brush, NULL, FALSE);

        ID3D11Buffer_Release(vb);
        ID3D11Buffer_Release(ib);
    }

    d2d_device_context_resolve_msaa(render_target);
}

static void STDMETHODCALLTYPE d2d_device_context_DrawGeometry(ID2D1DeviceContext6 *iface,
        ID2D1Geometry *geometry, ID2D1Brush *brush, float stroke_width, ID2D1StrokeStyle *stroke_style)
{
    const struct d2d_geometry *geometry_impl = unsafe_impl_from_ID2D1Geometry(geometry);
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);
    struct d2d_brush *brush_impl = unsafe_impl_from_ID2D1Brush(brush);
    struct d2d_stroke_style *stroke_style_impl = unsafe_impl_from_ID2D1StrokeStyle(stroke_style);

    TRACE("iface %p, geometry %p, brush %p, stroke_width %.8e, stroke_style %p.\n",
            iface, geometry, brush, stroke_width, stroke_style);

    if (FAILED(context->error.code))
        return;

    if (context->target.type == D2D_TARGET_UNKNOWN)
    {
        d2d_device_context_set_error(context, D2DERR_WRONG_STATE);
        return;
    }

    if (context->target.type == D2D_TARGET_COMMAND_LIST)
    {
        d2d_command_list_draw_geometry(context->target.command_list, context, geometry, brush,
                stroke_width, stroke_style);
        return;
    }

    if (stroke_style_impl)
    {
        if (stroke_style_impl->desc.transformType == D2D1_STROKE_TRANSFORM_TYPE_FIXED)
            stroke_width /= context->drawing_state.transform.m11;

        if (stroke_style_impl->dash_count && stroke_style_impl->desc.dashStyle != D2D1_DASH_STYLE_SOLID)
        {
            d2d_device_context_draw_dashed_geometry(context, geometry_impl, brush_impl,
                    stroke_width, stroke_style_impl);
            return;
        }
    }

    d2d_device_context_draw_geometry(context, geometry_impl, brush_impl, stroke_width);
}

static void d2d_device_context_fill_geometry(struct d2d_device_context *render_target,
        const struct d2d_geometry *geometry, struct d2d_brush *brush, struct d2d_brush *opacity_brush)
{
    D3D11_SUBRESOURCE_DATA buffer_data;
    D3D11_BUFFER_DESC buffer_desc;
    ID3D11Buffer *ib, *vb;
    HRESULT hr;

    buffer_desc.Usage = D3D11_USAGE_DEFAULT;
    buffer_desc.CPUAccessFlags = 0;
    buffer_desc.MiscFlags = 0;

    buffer_data.SysMemPitch = 0;
    buffer_data.SysMemSlicePitch = 0;

    if (FAILED(hr = d2d_device_context_update_vs_cb(render_target, &geometry->transform, 0.0f)))
    {
        WARN("Failed to update vs constant buffer, hr %#lx.\n", hr);
        return;
    }

    if (FAILED(hr = d2d_device_context_update_ps_cb(render_target, brush, opacity_brush, FALSE, FALSE)))
    {
        WARN("Failed to update ps constant buffer, hr %#lx.\n", hr);
        return;
    }

    if (geometry->fill.face_count)
    {
        buffer_desc.ByteWidth = geometry->fill.face_count * sizeof(*geometry->fill.faces);
        buffer_desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
        buffer_data.pSysMem = geometry->fill.faces;

        if (FAILED(hr = ID3D11Device1_CreateBuffer(render_target->d3d_device, &buffer_desc, &buffer_data, &ib)))
        {
            WARN("Failed to create index buffer, hr %#lx.\n", hr);
            return;
        }

        buffer_desc.ByteWidth = geometry->fill.vertex_count * sizeof(*geometry->fill.vertices);
        buffer_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        buffer_data.pSysMem = geometry->fill.vertices;

        if (FAILED(hr = ID3D11Device1_CreateBuffer(render_target->d3d_device, &buffer_desc, &buffer_data, &vb)))
        {
            ERR("Failed to create vertex buffer, hr %#lx.\n", hr);
            ID3D11Buffer_Release(ib);
            return;
        }

        d2d_device_context_draw_ex(render_target, D2D_SHAPE_TYPE_TRIANGLE, ib, 3 * geometry->fill.face_count, vb,
                sizeof(*geometry->fill.vertices), brush, opacity_brush, FALSE);

        ID3D11Buffer_Release(vb);
        ID3D11Buffer_Release(ib);
    }

    if (geometry->fill.bezier_vertex_count)
    {
        buffer_desc.ByteWidth = geometry->fill.bezier_vertex_count * sizeof(*geometry->fill.bezier_vertices);
        buffer_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        buffer_data.pSysMem = geometry->fill.bezier_vertices;

        if (FAILED(hr = ID3D11Device1_CreateBuffer(render_target->d3d_device, &buffer_desc, &buffer_data, &vb)))
        {
            ERR("Failed to create curves vertex buffer, hr %#lx.\n", hr);
            return;
        }

        d2d_device_context_draw_ex(render_target, D2D_SHAPE_TYPE_CURVE, NULL, geometry->fill.bezier_vertex_count, vb,
                sizeof(*geometry->fill.bezier_vertices), brush, opacity_brush, FALSE);

        ID3D11Buffer_Release(vb);
    }

    if (geometry->fill.arc_vertex_count)
    {
        buffer_desc.ByteWidth = geometry->fill.arc_vertex_count * sizeof(*geometry->fill.arc_vertices);
        buffer_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        buffer_data.pSysMem = geometry->fill.arc_vertices;

        if (FAILED(hr = ID3D11Device1_CreateBuffer(render_target->d3d_device, &buffer_desc, &buffer_data, &vb)))
        {
            ERR("Failed to create arc vertex buffer, hr %#lx.\n", hr);
            return;
        }

        if (SUCCEEDED(d2d_device_context_update_ps_cb(render_target, brush, opacity_brush, FALSE, TRUE)))
            d2d_device_context_draw_ex(render_target, D2D_SHAPE_TYPE_CURVE, NULL, geometry->fill.arc_vertex_count, vb,
                    sizeof(*geometry->fill.arc_vertices), brush, opacity_brush, FALSE);

        ID3D11Buffer_Release(vb);
    }

    d2d_device_context_resolve_msaa(render_target);
}

static void STDMETHODCALLTYPE d2d_device_context_FillGeometry(ID2D1DeviceContext6 *iface,
        ID2D1Geometry *geometry, ID2D1Brush *brush, ID2D1Brush *opacity_brush)
{
    const struct d2d_geometry *geometry_impl = unsafe_impl_from_ID2D1Geometry(geometry);
    struct d2d_brush *opacity_brush_impl = unsafe_impl_from_ID2D1Brush(opacity_brush);
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);
    struct d2d_brush *brush_impl = unsafe_impl_from_ID2D1Brush(brush);

    TRACE("iface %p, geometry %p, brush %p, opacity_brush %p.\n", iface, geometry, brush, opacity_brush);

    if (FAILED(context->error.code))
        return;

    if (context->target.type == D2D_TARGET_UNKNOWN)
    {
        d2d_device_context_set_error(context, D2DERR_WRONG_STATE);
        return;
    }

    if (opacity_brush && brush_impl->type != D2D_BRUSH_TYPE_BITMAP)
    {
        d2d_device_context_set_error(context, D2DERR_INCOMPATIBLE_BRUSH_TYPES);
        return;
    }

    if (context->target.type == D2D_TARGET_COMMAND_LIST)
        d2d_command_list_fill_geometry(context->target.command_list, context, geometry, brush, opacity_brush);
    else
        d2d_device_context_fill_geometry(context, geometry_impl, brush_impl, opacity_brush_impl);
}

static void STDMETHODCALLTYPE d2d_device_context_FillMesh(ID2D1DeviceContext6 *iface,
        ID2D1Mesh *mesh, ID2D1Brush *brush)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);

    FIXME("iface %p, mesh %p, brush %p stub!\n", iface, mesh, brush);

    if (context->target.type == D2D_TARGET_COMMAND_LIST)
        d2d_command_list_fill_mesh(context->target.command_list, context, mesh, brush);
}

static void STDMETHODCALLTYPE d2d_device_context_FillOpacityMask(ID2D1DeviceContext6 *iface,
        ID2D1Bitmap *mask, ID2D1Brush *brush, D2D1_OPACITY_MASK_CONTENT content,
        const D2D1_RECT_F *dst_rect, const D2D1_RECT_F *src_rect)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);

    FIXME("iface %p, mask %p, brush %p, content %#x, dst_rect %s, src_rect %s stub!\n",
            iface, mask, brush, content, debug_d2d_rect_f(dst_rect), debug_d2d_rect_f(src_rect));

    if (FAILED(context->error.code))
        return;

    if (context->drawing_state.antialiasMode != D2D1_ANTIALIAS_MODE_ALIASED)
    {
        d2d_device_context_set_error(context, D2DERR_WRONG_STATE);
        return;
    }

    if ((unsigned int)content > D2D1_OPACITY_MASK_CONTENT_TEXT_GDI_COMPATIBLE)
    {
        d2d_device_context_set_error(context, E_INVALIDARG);
        return;
    }

    if (context->target.type == D2D_TARGET_COMMAND_LIST)
        d2d_command_list_fill_opacity_mask(context->target.command_list, context, mask, brush, dst_rect, src_rect);
}

static void d2d_device_context_draw_bitmap(struct d2d_device_context *context, ID2D1Bitmap *bitmap,
        const D2D1_RECT_F *dst_rect, float opacity, D2D1_INTERPOLATION_MODE interpolation_mode,
        const D2D1_RECT_F *src_rect, const D2D1_POINT_2F *offset,
        const D2D1_MATRIX_4X4_F *perspective_transform)
{
    D2D1_BITMAP_BRUSH_PROPERTIES1 bitmap_brush_desc;
    D2D1_BRUSH_PROPERTIES brush_desc;
    struct d2d_brush *brush;
    D2D1_SIZE_F size;
    D2D1_RECT_F s, d;
    HRESULT hr;

    if (perspective_transform)
        FIXME("Perspective transform is ignored.\n");

    size = ID2D1Bitmap_GetSize(bitmap);
    d2d_rect_set(&s, 0.0f, 0.0f, size.width, size.height);
    if (src_rect && src_rect->left <= src_rect->right
            && src_rect->top <= src_rect->bottom)
    {
        d2d_rect_intersect(&s, src_rect);
    }

    if (s.left == s.right || s.top == s.bottom)
        return;

    if (dst_rect)
    {
        d = *dst_rect;
    }
    else
    {
        d.left = 0.0f;
        d.top = 0.0f;
        d.right = s.right - s.left;
        d.bottom = s.bottom - s.top;
    }

    if (offset)
    {
        d.left += offset->x;
        d.top += offset->y;
        d.right += offset->x;
        d.bottom += offset->y;
    }

    bitmap_brush_desc.extendModeX = D2D1_EXTEND_MODE_CLAMP;
    bitmap_brush_desc.extendModeY = D2D1_EXTEND_MODE_CLAMP;
    bitmap_brush_desc.interpolationMode = interpolation_mode;

    brush_desc.opacity = opacity;
    brush_desc.transform._11 = fabsf((d.right - d.left) / (s.right - s.left));
    brush_desc.transform._21 = 0.0f;
    brush_desc.transform._31 = min(d.left, d.right) - min(s.left, s.right) * brush_desc.transform._11;
    brush_desc.transform._12 = 0.0f;
    brush_desc.transform._22 = fabsf((d.bottom - d.top) / (s.bottom - s.top));
    brush_desc.transform._32 = min(d.top, d.bottom) - min(s.top, s.bottom) * brush_desc.transform._22;

    if (FAILED(hr = d2d_bitmap_brush_create(context->factory, bitmap, &bitmap_brush_desc, &brush_desc, &brush)))
    {
        ERR("Failed to create bitmap brush, hr %#lx.\n", hr);
        return;
    }

    d2d_device_context_FillRectangle(&context->ID2D1DeviceContext6_iface, &d, &brush->ID2D1Brush_iface);
    ID2D1Brush_Release(&brush->ID2D1Brush_iface);
}

static void STDMETHODCALLTYPE d2d_device_context_DrawBitmap(ID2D1DeviceContext6 *iface,
        ID2D1Bitmap *bitmap, const D2D1_RECT_F *dst_rect, float opacity,
        D2D1_BITMAP_INTERPOLATION_MODE interpolation_mode, const D2D1_RECT_F *src_rect)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);

    TRACE("iface %p, bitmap %p, dst_rect %s, opacity %.8e, interpolation_mode %#x, src_rect %s.\n",
            iface, bitmap, debug_d2d_rect_f(dst_rect), opacity, interpolation_mode, debug_d2d_rect_f(src_rect));

    if (FAILED(context->error.code))
        return;

    if (context->target.type == D2D_TARGET_UNKNOWN)
    {
        d2d_device_context_set_error(context, D2DERR_WRONG_STATE);
        return;
    }

    if (interpolation_mode != D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR
            && interpolation_mode != D2D1_BITMAP_INTERPOLATION_MODE_LINEAR)
    {
        d2d_device_context_set_error(context, E_INVALIDARG);
        return;
    }

    if (context->target.type == D2D_TARGET_COMMAND_LIST)
    {
        d2d_command_list_draw_bitmap(context->target.command_list, bitmap, dst_rect, opacity,
                d2d1_1_interp_mode_from_d2d1(interpolation_mode), src_rect, NULL);
    }
    else
    {
        d2d_device_context_draw_bitmap(context, bitmap, dst_rect, opacity,
                d2d1_1_interp_mode_from_d2d1(interpolation_mode), src_rect, NULL, NULL);
    }
}

static void STDMETHODCALLTYPE d2d_device_context_DrawText(ID2D1DeviceContext6 *iface,
        const WCHAR *string, UINT32 string_len, IDWriteTextFormat *text_format, const D2D1_RECT_F *layout_rect,
        ID2D1Brush *brush, D2D1_DRAW_TEXT_OPTIONS options, DWRITE_MEASURING_MODE measuring_mode)
{
    struct d2d_device_context *render_target = impl_from_ID2D1DeviceContext(iface);
    IDWriteTextLayout *text_layout;
    IDWriteFactory *dwrite_factory;
    D2D1_POINT_2F origin;
    float width, height;
    HRESULT hr;

    TRACE("iface %p, string %s, string_len %u, text_format %p, layout_rect %s, "
            "brush %p, options %#x, measuring_mode %#x.\n",
            iface, debugstr_wn(string, string_len), string_len, text_format, debug_d2d_rect_f(layout_rect),
            brush, options, measuring_mode);

    if (FAILED(hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
            &IID_IDWriteFactory, (IUnknown **)&dwrite_factory)))
    {
        ERR("Failed to create dwrite factory, hr %#lx.\n", hr);
        return;
    }

    width = max(0.0f, layout_rect->right - layout_rect->left);
    height = max(0.0f, layout_rect->bottom - layout_rect->top);
    if (measuring_mode == DWRITE_MEASURING_MODE_NATURAL)
        hr = IDWriteFactory_CreateTextLayout(dwrite_factory, string, string_len, text_format,
                width, height, &text_layout);
    else
        hr = IDWriteFactory_CreateGdiCompatibleTextLayout(dwrite_factory, string, string_len, text_format,
                width, height, render_target->desc.dpiX / 96.0f, (DWRITE_MATRIX *)&render_target->drawing_state.transform,
                measuring_mode == DWRITE_MEASURING_MODE_GDI_NATURAL, &text_layout);
    IDWriteFactory_Release(dwrite_factory);
    if (FAILED(hr))
    {
        ERR("Failed to create text layout, hr %#lx.\n", hr);
        return;
    }

    d2d_point_set(&origin, min(layout_rect->left, layout_rect->right), min(layout_rect->top, layout_rect->bottom));
    ID2D1DeviceContext1_DrawTextLayout((ID2D1DeviceContext1 *)iface, origin, text_layout, brush, options);
    IDWriteTextLayout_Release(text_layout);
}

static void STDMETHODCALLTYPE d2d_device_context_DrawTextLayout(ID2D1DeviceContext6 *iface,
        D2D1_POINT_2F origin, IDWriteTextLayout *layout, ID2D1Brush *brush, D2D1_DRAW_TEXT_OPTIONS options)
{
    struct d2d_device_context *render_target = impl_from_ID2D1DeviceContext(iface);
    struct d2d_draw_text_layout_ctx ctx;
    HRESULT hr;

    TRACE("iface %p, origin %s, layout %p, brush %p, options %#x.\n",
            iface, debug_d2d_point_2f(&origin), layout, brush, options);

    ctx.brush = brush;
    ctx.options = options;

    if (FAILED(hr = IDWriteTextLayout_Draw(layout,
            &ctx, &render_target->IDWriteTextRenderer_iface, origin.x, origin.y)))
        FIXME("Failed to draw text layout, hr %#lx.\n", hr);
}

static D2D1_ANTIALIAS_MODE d2d_device_context_set_aa_mode_from_text_aa_mode(struct d2d_device_context *rt)
{
    D2D1_ANTIALIAS_MODE prev_antialias_mode = rt->drawing_state.antialiasMode;
    rt->drawing_state.antialiasMode = rt->drawing_state.textAntialiasMode == D2D1_TEXT_ANTIALIAS_MODE_ALIASED ?
            D2D1_ANTIALIAS_MODE_ALIASED : D2D1_ANTIALIAS_MODE_PER_PRIMITIVE;
    return prev_antialias_mode;
}

static HRESULT d2d_device_context_get_glyph_run_geometry(struct d2d_device_context *context,
        const DWRITE_GLYPH_RUN *glyph_run, ID2D1PathGeometry **result)
{
    ID2D1PathGeometry *geometry;
    ID2D1GeometrySink *sink;
    HRESULT hr;

    *result = NULL;

    if (FAILED(hr = ID2D1Factory_CreatePathGeometry(context->factory, &geometry)))
        return hr;

    if (FAILED(hr = ID2D1PathGeometry_Open(geometry, &sink)))
    {
        ID2D1PathGeometry_Release(geometry);
        return hr;
    }

    if (FAILED(hr = IDWriteFontFace_GetGlyphRunOutline(glyph_run->fontFace, glyph_run->fontEmSize,
            glyph_run->glyphIndices, glyph_run->glyphAdvances, glyph_run->glyphOffsets, glyph_run->glyphCount,
            glyph_run->isSideways, glyph_run->bidiLevel & 1, (IDWriteGeometrySink *)sink)))
    {
        ERR("Failed to get glyph run outline, hr %#lx.\n", hr);
        ID2D1GeometrySink_Release(sink);
        ID2D1PathGeometry_Release(geometry);
        return hr;
    }

    if (FAILED(hr = ID2D1GeometrySink_Close(sink)))
        ERR("Failed to close geometry sink, hr %#lx.\n", hr);
    ID2D1GeometrySink_Release(sink);

    if (hr == S_OK)
        *result = geometry;
    else
        ID2D1PathGeometry_Release(geometry);

    return hr;
}

static void d2d_device_context_draw_glyph_run_outline(struct d2d_device_context *context,
        D2D1_POINT_2F baseline_origin, const DWRITE_GLYPH_RUN *glyph_run, ID2D1Brush *brush)
{
    D2D1_MATRIX_3X2_F *transform, prev_transform;
    D2D1_ANTIALIAS_MODE prev_antialias_mode;
    ID2D1PathGeometry *geometry;
    HRESULT hr;

    if (FAILED(hr = d2d_device_context_get_glyph_run_geometry(context, glyph_run, &geometry)))
    {
        ERR("Failed to create geometry, hr %#lx.\n", hr);
        return;
    }

    transform = &context->drawing_state.transform;
    prev_transform = *transform;
    transform->_31 += baseline_origin.x * transform->_11 + baseline_origin.y * transform->_21;
    transform->_32 += baseline_origin.x * transform->_12 + baseline_origin.y * transform->_22;
    prev_antialias_mode = d2d_device_context_set_aa_mode_from_text_aa_mode(context);
    d2d_device_context_fill_geometry(context, unsafe_impl_from_ID2D1Geometry((ID2D1Geometry *)geometry),
            unsafe_impl_from_ID2D1Brush(brush), NULL);
    context->drawing_state.antialiasMode = prev_antialias_mode;
    *transform = prev_transform;

    ID2D1PathGeometry_Release(geometry);
}

static HRESULT d2d_device_context_get_glyph_run_analysis(struct d2d_device_context *context,
        D2D1_POINT_2F baseline_origin, const DWRITE_GLYPH_RUN *glyph_run,
        DWRITE_RENDERING_MODE rendering_mode, DWRITE_MEASURING_MODE measuring_mode,
        DWRITE_TEXT_ANTIALIAS_MODE antialias_mode, DWRITE_TEXTURE_TYPE *texture_type,
        IDWriteGlyphRunAnalysis **result)
{
    IDWriteGlyphRunAnalysis *analysis;
    IDWriteFactory2 *dwrite_factory;
    D2D1_MATRIX_3X2_F *transform;
    float scale_x, scale_y;
    DWRITE_MATRIX m;
    HRESULT hr;

    *result = NULL;

    if (FAILED(hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, &IID_IDWriteFactory2,
            (IUnknown **)&dwrite_factory)))
    {
        ERR("Failed to create dwrite factory, hr %#lx.\n", hr);
        return hr;
    }

    transform = &context->drawing_state.transform;

    scale_x = context->desc.dpiX / 96.0f;
    m.m11 = transform->_11 * scale_x;
    m.m21 = transform->_21 * scale_x;
    m.dx  = transform->_31 * scale_x;

    scale_y = context->desc.dpiY / 96.0f;
    m.m12 = transform->_12 * scale_y;
    m.m22 = transform->_22 * scale_y;
    m.dy  = transform->_32 * scale_y;

    hr = IDWriteFactory2_CreateGlyphRunAnalysis(dwrite_factory, glyph_run, &m, rendering_mode,
            measuring_mode, DWRITE_GRID_FIT_MODE_DEFAULT, antialias_mode, baseline_origin.x,
            baseline_origin.y, &analysis);
    IDWriteFactory2_Release(dwrite_factory);
    if (FAILED(hr))
    {
        ERR("Failed to create glyph run analysis, hr %#lx.\n", hr);
        return hr;
    }

    if (rendering_mode == DWRITE_RENDERING_MODE_ALIASED || antialias_mode == DWRITE_TEXT_ANTIALIAS_MODE_GRAYSCALE)
        *texture_type = DWRITE_TEXTURE_ALIASED_1x1;
    else
        *texture_type = DWRITE_TEXTURE_CLEARTYPE_3x1;

    *result = analysis;

    return S_OK;
}

static void d2d_device_context_draw_glyph_run_bitmap(struct d2d_device_context *context,
        D2D1_POINT_2F baseline_origin, const DWRITE_GLYPH_RUN *glyph_run, ID2D1Brush *brush,
        DWRITE_RENDERING_MODE rendering_mode, DWRITE_MEASURING_MODE measuring_mode,
        DWRITE_TEXT_ANTIALIAS_MODE antialias_mode)
{
    ID2D1RectangleGeometry *geometry = NULL;
    ID2D1BitmapBrush *opacity_brush = NULL;
    D2D1_BITMAP_PROPERTIES bitmap_desc;
    ID2D1Bitmap *opacity_bitmap = NULL;
    IDWriteGlyphRunAnalysis *analysis;
    DWRITE_TEXTURE_TYPE texture_type;
    D2D1_BRUSH_PROPERTIES brush_desc;
    D2D1_MATRIX_3X2_F *transform, m;
    void *opacity_values = NULL;
    size_t opacity_values_size;
    D2D1_SIZE_U bitmap_size;
    float scale_x, scale_y;
    D2D1_RECT_F run_rect;
    RECT bounds;
    HRESULT hr;

    hr = d2d_device_context_get_glyph_run_analysis(context, baseline_origin, glyph_run,
            rendering_mode, measuring_mode, antialias_mode, &texture_type, &analysis);
    if (FAILED(hr))
    {
        ERR("Failed to create glyph run analysis, hr %#lx.\n", hr);
        return;
    }

    if (FAILED(hr = IDWriteGlyphRunAnalysis_GetAlphaTextureBounds(analysis, texture_type, &bounds)))
    {
        ERR("Failed to get alpha texture bounds, hr %#lx.\n", hr);
        goto done;
    }

    d2d_size_set(&bitmap_size, bounds.right - bounds.left, bounds.bottom - bounds.top);
    if (!bitmap_size.width || !bitmap_size.height)
    {
        /* Empty run, nothing to do. */
        goto done;
    }

    if (texture_type == DWRITE_TEXTURE_CLEARTYPE_3x1)
        bitmap_size.width *= 3;
    if (!(opacity_values = calloc(bitmap_size.height, bitmap_size.width)))
    {
        ERR("Failed to allocate opacity values.\n");
        goto done;
    }
    opacity_values_size = bitmap_size.height * bitmap_size.width;

    if (FAILED(hr = IDWriteGlyphRunAnalysis_CreateAlphaTexture(analysis,
            texture_type, &bounds, opacity_values, opacity_values_size)))
    {
        ERR("Failed to create alpha texture, hr %#lx.\n", hr);
        goto done;
    }

    bitmap_desc.pixelFormat.format = DXGI_FORMAT_A8_UNORM;
    bitmap_desc.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
    bitmap_desc.dpiX = context->desc.dpiX;
    if (texture_type == DWRITE_TEXTURE_CLEARTYPE_3x1)
        bitmap_desc.dpiX *= 3.0f;
    bitmap_desc.dpiY = context->desc.dpiY;
    if (FAILED(hr = d2d_device_context_CreateBitmap(&context->ID2D1DeviceContext6_iface,
            bitmap_size, opacity_values, bitmap_size.width, &bitmap_desc, &opacity_bitmap)))
    {
        ERR("Failed to create opacity bitmap, hr %#lx.\n", hr);
        goto done;
    }

    scale_x = context->desc.dpiX / 96.0f;
    scale_y = context->desc.dpiY / 96.0f;
    d2d_rect_set(&run_rect, bounds.left / scale_x, bounds.top / scale_y,
            bounds.right / scale_x, bounds.bottom / scale_y);

    brush_desc.opacity = 1.0f;
    brush_desc.transform._11 = 1.0f;
    brush_desc.transform._12 = 0.0f;
    brush_desc.transform._21 = 0.0f;
    brush_desc.transform._22 = 1.0f;
    brush_desc.transform._31 = run_rect.left;
    brush_desc.transform._32 = run_rect.top;
    if (FAILED(hr = d2d_device_context_CreateBitmapBrush(&context->ID2D1DeviceContext6_iface,
            opacity_bitmap, NULL, &brush_desc, &opacity_brush)))
    {
        ERR("Failed to create opacity bitmap brush, hr %#lx.\n", hr);
        goto done;
    }

    if (FAILED(hr = ID2D1Factory_CreateRectangleGeometry(context->factory, &run_rect, &geometry)))
    {
        ERR("Failed to create geometry, hr %#lx.\n", hr);
        goto done;
    }

    transform = &context->drawing_state.transform;
    m = *transform;
    *transform = identity;
    d2d_device_context_fill_geometry(context, unsafe_impl_from_ID2D1Geometry((ID2D1Geometry *)geometry),
            unsafe_impl_from_ID2D1Brush(brush), unsafe_impl_from_ID2D1Brush((ID2D1Brush *)opacity_brush));
    *transform = m;

done:
    if (geometry)
        ID2D1RectangleGeometry_Release(geometry);
    if (opacity_brush)
        ID2D1BitmapBrush_Release(opacity_brush);
    if (opacity_bitmap)
        ID2D1Bitmap_Release(opacity_bitmap);
    free(opacity_values);
    IDWriteGlyphRunAnalysis_Release(analysis);
}

static HRESULT d2d_device_context_get_text_rendering_mode(struct d2d_device_context *context,
        const DWRITE_GLYPH_RUN *glyph_run, DWRITE_MEASURING_MODE measuring_mode,
        DWRITE_TEXT_ANTIALIAS_MODE *antialias_mode, DWRITE_RENDERING_MODE *rendering_mode)
{
    IDWriteRenderingParams *rendering_params;
    HRESULT hr = S_OK;

    rendering_params = context->text_rendering_params ? context->text_rendering_params
            : context->default_text_rendering_params;

    *antialias_mode = DWRITE_TEXT_ANTIALIAS_MODE_GRAYSCALE;
    *rendering_mode = IDWriteRenderingParams_GetRenderingMode(rendering_params);

    switch (context->drawing_state.textAntialiasMode)
    {
        case D2D1_TEXT_ANTIALIAS_MODE_ALIASED:
            if (*rendering_mode == DWRITE_RENDERING_MODE_CLEARTYPE_NATURAL
                    || *rendering_mode == DWRITE_RENDERING_MODE_CLEARTYPE_NATURAL_SYMMETRIC
                    || *rendering_mode == DWRITE_RENDERING_MODE_CLEARTYPE_GDI_NATURAL
                    || *rendering_mode == DWRITE_RENDERING_MODE_CLEARTYPE_GDI_CLASSIC)
            {
                return E_INVALIDARG;
            }
            break;

        case D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE:
            if (*rendering_mode == DWRITE_RENDERING_MODE_ALIASED
                    || *rendering_mode == DWRITE_RENDERING_MODE_OUTLINE)
            {
                return E_INVALIDARG;
            }
            break;

        case D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE:
            if (*rendering_mode == DWRITE_RENDERING_MODE_ALIASED)
                return E_INVALIDARG;
            break;

        default:
            break;
    }

    *rendering_mode = DWRITE_RENDERING_MODE_DEFAULT;
    switch (context->drawing_state.textAntialiasMode)
    {
        case D2D1_TEXT_ANTIALIAS_MODE_DEFAULT:
            if (IDWriteRenderingParams_GetClearTypeLevel(rendering_params) > 0.0f)
                *antialias_mode = DWRITE_TEXT_ANTIALIAS_MODE_CLEARTYPE;
            break;

        case D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE:
            *antialias_mode = DWRITE_TEXT_ANTIALIAS_MODE_CLEARTYPE;
            break;

        case D2D1_TEXT_ANTIALIAS_MODE_ALIASED:
            *rendering_mode = DWRITE_RENDERING_MODE_ALIASED;
            break;

        default:
            break;
    }

    if (*rendering_mode == DWRITE_RENDERING_MODE_DEFAULT)
    {
        if (FAILED(hr = IDWriteFontFace_GetRecommendedRenderingMode(glyph_run->fontFace, glyph_run->fontEmSize,
                max(context->desc.dpiX, context->desc.dpiY) / 96.0f,
                measuring_mode, rendering_params, rendering_mode)))
        {
            ERR("Failed to get recommended rendering mode, hr %#lx.\n", hr);
            *rendering_mode = DWRITE_RENDERING_MODE_OUTLINE;
        }
    }

    return hr;
}

static void d2d_device_context_draw_glyph_run(struct d2d_device_context *context,
        D2D1_POINT_2F baseline_origin, const DWRITE_GLYPH_RUN *glyph_run,
        const DWRITE_GLYPH_RUN_DESCRIPTION *glyph_run_desc, ID2D1Brush *brush, DWRITE_MEASURING_MODE measuring_mode)
{
    DWRITE_TEXT_ANTIALIAS_MODE antialias_mode;
    DWRITE_RENDERING_MODE rendering_mode;
    HRESULT hr;

    if (FAILED(context->error.code))
        return;

    if (context->target.type == D2D_TARGET_UNKNOWN)
    {
        d2d_device_context_set_error(context, D2DERR_WRONG_STATE);
        return;
    }

    if (context->target.type == D2D_TARGET_COMMAND_LIST)
    {
        d2d_command_list_draw_glyph_run(context->target.command_list, context, baseline_origin, glyph_run,
                glyph_run_desc, brush, measuring_mode);
        return;
    }

    if (FAILED(hr = d2d_device_context_get_text_rendering_mode(context, glyph_run, measuring_mode,
            &antialias_mode, &rendering_mode)))
    {
        d2d_device_context_set_error(context, hr);
        return;
    }

    if (rendering_mode == DWRITE_RENDERING_MODE_OUTLINE)
        d2d_device_context_draw_glyph_run_outline(context, baseline_origin, glyph_run, brush);
    else
        d2d_device_context_draw_glyph_run_bitmap(context, baseline_origin, glyph_run, brush,
                rendering_mode, measuring_mode, antialias_mode);
}

static void STDMETHODCALLTYPE d2d_device_context_DrawGlyphRun(ID2D1DeviceContext6 *iface,
        D2D1_POINT_2F baseline_origin, const DWRITE_GLYPH_RUN *glyph_run, ID2D1Brush *brush,
        DWRITE_MEASURING_MODE measuring_mode)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);

    TRACE("iface %p, baseline_origin %s, glyph_run %p, brush %p, measuring_mode %#x.\n",
            iface, debug_d2d_point_2f(&baseline_origin), glyph_run, brush, measuring_mode);

    d2d_device_context_draw_glyph_run(context, baseline_origin, glyph_run, NULL, brush, measuring_mode);
}

static void STDMETHODCALLTYPE d2d_device_context_SetTransform(ID2D1DeviceContext6 *iface,
        const D2D1_MATRIX_3X2_F *transform)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);

    TRACE("iface %p, transform %p.\n", iface, transform);

    if (context->target.type == D2D_TARGET_COMMAND_LIST)
        d2d_command_list_set_transform(context->target.command_list, transform);

    context->drawing_state.transform = *transform;
}

static void STDMETHODCALLTYPE d2d_device_context_GetTransform(ID2D1DeviceContext6 *iface,
        D2D1_MATRIX_3X2_F *transform)
{
    struct d2d_device_context *render_target = impl_from_ID2D1DeviceContext(iface);

    TRACE("iface %p, transform %p.\n", iface, transform);

    *transform = render_target->drawing_state.transform;
}

static void STDMETHODCALLTYPE d2d_device_context_SetAntialiasMode(ID2D1DeviceContext6 *iface,
        D2D1_ANTIALIAS_MODE antialias_mode)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);

    TRACE("iface %p, antialias_mode %#x stub!\n", iface, antialias_mode);

    if (context->target.type == D2D_TARGET_COMMAND_LIST)
        d2d_command_list_set_antialias_mode(context->target.command_list, antialias_mode);

    context->drawing_state.antialiasMode = antialias_mode;
}

static D2D1_ANTIALIAS_MODE STDMETHODCALLTYPE d2d_device_context_GetAntialiasMode(ID2D1DeviceContext6 *iface)
{
    struct d2d_device_context *render_target = impl_from_ID2D1DeviceContext(iface);

    TRACE("iface %p.\n", iface);

    return render_target->drawing_state.antialiasMode;
}

static void STDMETHODCALLTYPE d2d_device_context_SetTextAntialiasMode(ID2D1DeviceContext6 *iface,
        D2D1_TEXT_ANTIALIAS_MODE antialias_mode)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);

    TRACE("iface %p, antialias_mode %#x.\n", iface, antialias_mode);

    if (context->target.type == D2D_TARGET_COMMAND_LIST)
        d2d_command_list_set_text_antialias_mode(context->target.command_list, antialias_mode);

    context->drawing_state.textAntialiasMode = antialias_mode;
}

static D2D1_TEXT_ANTIALIAS_MODE STDMETHODCALLTYPE d2d_device_context_GetTextAntialiasMode(ID2D1DeviceContext6 *iface)
{
    struct d2d_device_context *render_target = impl_from_ID2D1DeviceContext(iface);

    TRACE("iface %p.\n", iface);

    return render_target->drawing_state.textAntialiasMode;
}

static void STDMETHODCALLTYPE d2d_device_context_SetTextRenderingParams(ID2D1DeviceContext6 *iface,
        IDWriteRenderingParams *text_rendering_params)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);

    TRACE("iface %p, text_rendering_params %p.\n", iface, text_rendering_params);

    if (context->target.type == D2D_TARGET_COMMAND_LIST)
        d2d_command_list_set_text_rendering_params(context->target.command_list, text_rendering_params);

    if (text_rendering_params)
        IDWriteRenderingParams_AddRef(text_rendering_params);
    if (context->text_rendering_params)
        IDWriteRenderingParams_Release(context->text_rendering_params);
    context->text_rendering_params = text_rendering_params;
}

static void STDMETHODCALLTYPE d2d_device_context_GetTextRenderingParams(ID2D1DeviceContext6 *iface,
        IDWriteRenderingParams **text_rendering_params)
{
    struct d2d_device_context *render_target = impl_from_ID2D1DeviceContext(iface);

    TRACE("iface %p, text_rendering_params %p.\n", iface, text_rendering_params);

    if ((*text_rendering_params = render_target->text_rendering_params))
        IDWriteRenderingParams_AddRef(*text_rendering_params);
}

static void STDMETHODCALLTYPE d2d_device_context_SetTags(ID2D1DeviceContext6 *iface, D2D1_TAG tag1, D2D1_TAG tag2)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);

    TRACE("iface %p, tag1 %s, tag2 %s.\n", iface, wine_dbgstr_longlong(tag1), wine_dbgstr_longlong(tag2));

    if (context->target.type == D2D_TARGET_COMMAND_LIST)
        d2d_command_list_set_tags(context->target.command_list, tag1, tag2);

    context->drawing_state.tag1 = tag1;
    context->drawing_state.tag2 = tag2;
}

static void STDMETHODCALLTYPE d2d_device_context_GetTags(ID2D1DeviceContext6 *iface, D2D1_TAG *tag1, D2D1_TAG *tag2)
{
    struct d2d_device_context *render_target = impl_from_ID2D1DeviceContext(iface);

    TRACE("iface %p, tag1 %p, tag2 %p.\n", iface, tag1, tag2);

    *tag1 = render_target->drawing_state.tag1;
    *tag2 = render_target->drawing_state.tag2;
}

static void d2d_device_context_push_layer_clip(struct d2d_device_context *context,
        const D2D1_RECT_F *content_bounds, ID2D1Geometry *geometric_mask,
        const D2D1_MATRIX_3X2_F *mask_transform)
{
    D2D1_RECT_F bounds, mask_bounds, transformed_rect;
    float x_scale, y_scale;
    D2D1_POINT_2F point;
    BOOL has_bounds = FALSE;

    if (content_bounds && fabsf(content_bounds->left) < 1e30f && fabsf(content_bounds->top) < 1e30f
            && fabsf(content_bounds->right) < 1e30f && fabsf(content_bounds->bottom) < 1e30f)
    {
        bounds = *content_bounds;
        has_bounds = TRUE;
    }

    if (geometric_mask && SUCCEEDED(ID2D1Geometry_GetBounds(geometric_mask, mask_transform, &mask_bounds))
            && fabsf(mask_bounds.left) < 1e30f && fabsf(mask_bounds.top) < 1e30f
            && fabsf(mask_bounds.right) < 1e30f && fabsf(mask_bounds.bottom) < 1e30f)
    {
        if (has_bounds)
        {
            if (mask_bounds.left > bounds.left) bounds.left = mask_bounds.left;
            if (mask_bounds.top > bounds.top) bounds.top = mask_bounds.top;
            if (mask_bounds.right < bounds.right) bounds.right = mask_bounds.right;
            if (mask_bounds.bottom < bounds.bottom) bounds.bottom = mask_bounds.bottom;
        }
        else
        {
            bounds = mask_bounds;
            has_bounds = TRUE;
        }
    }

    if (!has_bounds)
    {
        d2d_rect_set(&transformed_rect, -1e7f, -1e7f, 1e7f, 1e7f);
    }
    else
    {
        x_scale = context->desc.dpiX / 96.0f;
        y_scale = context->desc.dpiY / 96.0f;
        d2d_point_transform(&point, &context->drawing_state.transform,
                bounds.left * x_scale, bounds.top * y_scale);
        d2d_rect_set(&transformed_rect, point.x, point.y, point.x, point.y);
        d2d_point_transform(&point, &context->drawing_state.transform,
                bounds.left * x_scale, bounds.bottom * y_scale);
        d2d_rect_expand(&transformed_rect, &point);
        d2d_point_transform(&point, &context->drawing_state.transform,
                bounds.right * x_scale, bounds.top * y_scale);
        d2d_rect_expand(&transformed_rect, &point);
        d2d_point_transform(&point, &context->drawing_state.transform,
                bounds.right * x_scale, bounds.bottom * y_scale);
        d2d_rect_expand(&transformed_rect, &point);
    }

    if (!d2d_clip_stack_push(&context->clip_stack, &transformed_rect))
        WARN("Failed to push layer clip rect.\n");
}

static void STDMETHODCALLTYPE d2d_device_context_PushLayer(ID2D1DeviceContext6 *iface,
        const D2D1_LAYER_PARAMETERS *layer_parameters, ID2D1Layer *layer)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);

    TRACE("iface %p, layer_parameters %p, layer %p.\n", iface, layer_parameters, layer);

    if (context->target.type == D2D_TARGET_COMMAND_LIST)
    {
        D2D1_LAYER_PARAMETERS1 parameters;

        memcpy(&parameters, layer_parameters, sizeof(*layer_parameters));
        parameters.layerOptions = D2D1_LAYER_OPTIONS1_NONE;
        d2d_command_list_push_layer(context->target.command_list, context, &parameters);
    }

    d2d_device_context_push_layer_clip(context,
            layer_parameters ? &layer_parameters->contentBounds : NULL,
            layer_parameters ? layer_parameters->geometricMask : NULL,
            layer_parameters ? &layer_parameters->maskTransform : NULL);
}

static void STDMETHODCALLTYPE d2d_device_context_PopLayer(ID2D1DeviceContext6 *iface)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);

    TRACE("iface %p.\n", iface);

    if (context->target.type == D2D_TARGET_COMMAND_LIST)
        d2d_command_list_pop_layer(context->target.command_list);

    d2d_clip_stack_pop(&context->clip_stack);
}

static HRESULT STDMETHODCALLTYPE d2d_device_context_Flush(ID2D1DeviceContext6 *iface, D2D1_TAG *tag1, D2D1_TAG *tag2)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);

    FIXME("iface %p, tag1 %p, tag2 %p stub!\n", iface, tag1, tag2);

    d2d_device_context_resolve_msaa(context);

    if (context->ops && context->ops->device_context_present)
        context->ops->device_context_present(context->outer_unknown);

    return S_OK;
}

static void STDMETHODCALLTYPE d2d_device_context_SaveDrawingState(ID2D1DeviceContext6 *iface,
        ID2D1DrawingStateBlock *state_block)
{
    struct d2d_device_context *render_target = impl_from_ID2D1DeviceContext(iface);
    struct d2d_state_block *state_block_impl;

    TRACE("iface %p, state_block %p.\n", iface, state_block);

    if (!(state_block_impl = unsafe_impl_from_ID2D1DrawingStateBlock(state_block))) return;
    state_block_impl->drawing_state = render_target->drawing_state;
    if (render_target->text_rendering_params)
        IDWriteRenderingParams_AddRef(render_target->text_rendering_params);
    if (state_block_impl->text_rendering_params)
        IDWriteRenderingParams_Release(state_block_impl->text_rendering_params);
    state_block_impl->text_rendering_params = render_target->text_rendering_params;
}

static void STDMETHODCALLTYPE d2d_device_context_RestoreDrawingState(ID2D1DeviceContext6 *iface,
        ID2D1DrawingStateBlock *state_block)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);
    struct d2d_state_block *state_block_impl;

    TRACE("iface %p, state_block %p.\n", iface, state_block);

    if (!(state_block_impl = unsafe_impl_from_ID2D1DrawingStateBlock(state_block))) return;
    if (context->target.type == D2D_TARGET_COMMAND_LIST)
    {
        struct d2d_command_list *command_list = context->target.command_list;

        if (context->drawing_state.antialiasMode != state_block_impl->drawing_state.antialiasMode)
            d2d_command_list_set_antialias_mode(command_list, state_block_impl->drawing_state.antialiasMode);
        d2d_command_list_set_text_antialias_mode(command_list, state_block_impl->drawing_state.textAntialiasMode);
        d2d_command_list_set_tags(command_list, state_block_impl->drawing_state.tag1, state_block_impl->drawing_state.tag2);
        d2d_command_list_set_transform(command_list, &state_block_impl->drawing_state.transform);
        d2d_command_list_set_primitive_blend(command_list, state_block_impl->drawing_state.primitiveBlend);
        d2d_command_list_set_unit_mode(command_list, state_block_impl->drawing_state.unitMode);
        d2d_command_list_set_text_rendering_params(command_list, state_block_impl->text_rendering_params);
    }

    context->drawing_state = state_block_impl->drawing_state;
    if (state_block_impl->text_rendering_params)
        IDWriteRenderingParams_AddRef(state_block_impl->text_rendering_params);
    if (context->text_rendering_params)
        IDWriteRenderingParams_Release(context->text_rendering_params);
    context->text_rendering_params = state_block_impl->text_rendering_params;
}

static void STDMETHODCALLTYPE d2d_device_context_PushAxisAlignedClip(ID2D1DeviceContext6 *iface,
        const D2D1_RECT_F *clip_rect, D2D1_ANTIALIAS_MODE antialias_mode)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);
    D2D1_RECT_F transformed_rect;
    float x_scale, y_scale;
    D2D1_POINT_2F point;

    TRACE("iface %p, clip_rect %s, antialias_mode %#x.\n", iface, debug_d2d_rect_f(clip_rect), antialias_mode);

    if (context->target.type == D2D_TARGET_COMMAND_LIST)
        d2d_command_list_push_clip(context->target.command_list, clip_rect, antialias_mode);

    if (antialias_mode != D2D1_ANTIALIAS_MODE_ALIASED)
        FIXME("Ignoring antialias_mode %#x.\n", antialias_mode);

    x_scale = context->desc.dpiX / 96.0f;
    y_scale = context->desc.dpiY / 96.0f;
    d2d_point_transform(&point, &context->drawing_state.transform,
            clip_rect->left * x_scale, clip_rect->top * y_scale);
    d2d_rect_set(&transformed_rect, point.x, point.y, point.x, point.y);
    d2d_point_transform(&point, &context->drawing_state.transform,
            clip_rect->left * x_scale, clip_rect->bottom * y_scale);
    d2d_rect_expand(&transformed_rect, &point);
    d2d_point_transform(&point, &context->drawing_state.transform,
            clip_rect->right * x_scale, clip_rect->top * y_scale);
    d2d_rect_expand(&transformed_rect, &point);
    d2d_point_transform(&point, &context->drawing_state.transform,
            clip_rect->right * x_scale, clip_rect->bottom * y_scale);
    d2d_rect_expand(&transformed_rect, &point);

    if (!d2d_clip_stack_push(&context->clip_stack, &transformed_rect))
        WARN("Failed to push clip rect.\n");
}

static void STDMETHODCALLTYPE d2d_device_context_PopAxisAlignedClip(ID2D1DeviceContext6 *iface)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);

    TRACE("iface %p.\n", iface);

    if (context->target.type == D2D_TARGET_COMMAND_LIST)
        d2d_command_list_pop_clip(context->target.command_list);

    d2d_clip_stack_pop(&context->clip_stack);
}

static void STDMETHODCALLTYPE d2d_device_context_Clear(ID2D1DeviceContext6 *iface, const D2D1_COLOR_F *colour)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);
    D3D11_MAPPED_SUBRESOURCE map_desc;
    ID3D11DeviceContext *d3d_context;
    struct d2d_ps_cb *ps_cb_data;
    struct d2d_vs_cb *vs_cb_data;
    D2D1_COLOR_F *c;
    HRESULT hr;

    TRACE("iface %p, colour %p.\n", iface, colour);

    if (FAILED(context->error.code))
        return;

    if (context->target.type == D2D_TARGET_UNKNOWN)
    {
        d2d_device_context_set_error(context, D2DERR_WRONG_STATE);
        return;
    }

    if (context->target.type == D2D_TARGET_COMMAND_LIST)
    {
        d2d_command_list_clear(context->target.command_list, colour);
        return;
    }

    ID3D11Device1_GetImmediateContext(context->d3d_device, &d3d_context);

    if (FAILED(hr = ID3D11DeviceContext_Map(d3d_context, (ID3D11Resource *)context->vs_cb,
            0, D3D11_MAP_WRITE_DISCARD, 0, &map_desc)))
    {
        WARN("Failed to map vs constant buffer, hr %#lx.\n", hr);
        ID3D11DeviceContext_Release(d3d_context);
        return;
    }

    vs_cb_data = map_desc.pData;
    vs_cb_data->transform_geometry._11 = 1.0f;
    vs_cb_data->transform_geometry._21 = 0.0f;
    vs_cb_data->transform_geometry._31 = 0.0f;
    vs_cb_data->transform_geometry.pad0 = 0.0f;
    vs_cb_data->transform_geometry._12 = 0.0f;
    vs_cb_data->transform_geometry._22 = 1.0f;
    vs_cb_data->transform_geometry._32 = 0.0f;
    vs_cb_data->transform_geometry.stroke_width = 0.0f;
    vs_cb_data->transform_rtx.x = 1.0f;
    vs_cb_data->transform_rtx.y = 0.0f;
    vs_cb_data->transform_rtx.z = 1.0f;
    vs_cb_data->transform_rtx.w = 1.0f;
    vs_cb_data->transform_rty.x = 0.0f;
    vs_cb_data->transform_rty.y = 1.0f;
    vs_cb_data->transform_rty.z = 1.0f;
    vs_cb_data->transform_rty.w = -1.0f;

    ID3D11DeviceContext_Unmap(d3d_context, (ID3D11Resource *)context->vs_cb, 0);

    if (FAILED(hr = ID3D11DeviceContext_Map(d3d_context, (ID3D11Resource *)context->ps_cb,
            0, D3D11_MAP_WRITE_DISCARD, 0, &map_desc)))
    {
        WARN("Failed to map ps constant buffer, hr %#lx.\n", hr);
        ID3D11DeviceContext_Release(d3d_context);
        return;
    }

    ps_cb_data = map_desc.pData;
    memset(ps_cb_data, 0, sizeof(*ps_cb_data));
    ps_cb_data->colour_brush.type = D2D_BRUSH_TYPE_SOLID;
    ps_cb_data->colour_brush.opacity = 1.0f;
    ps_cb_data->opacity_brush.type = D2D_BRUSH_TYPE_COUNT;
    c = &ps_cb_data->colour_brush.u.solid.colour;
    if (colour)
        *c = *colour;
    if (context->desc.pixelFormat.alphaMode == D2D1_ALPHA_MODE_IGNORE)
        c->a = 1.0f;
    c->r *= c->a;
    c->g *= c->a;
    c->b *= c->a;

    ID3D11DeviceContext_Unmap(d3d_context, (ID3D11Resource *)context->ps_cb, 0);
    ID3D11DeviceContext_Release(d3d_context);

    d2d_device_context_draw(context, D2D_SHAPE_TYPE_TRIANGLE, context->ib, 6,
            context->vb, context->vb_stride, NULL, NULL);
}

static void STDMETHODCALLTYPE d2d_device_context_BeginDraw(ID2D1DeviceContext6 *iface)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);

    TRACE("iface %p.\n", iface);

    if (context->target.type == D2D_TARGET_COMMAND_LIST)
        d2d_command_list_begin_draw(context->target.command_list, context);

    memset(&context->error, 0, sizeof(context->error));
}

static HRESULT STDMETHODCALLTYPE d2d_device_context_EndDraw(ID2D1DeviceContext6 *iface,
        D2D1_TAG *tag1, D2D1_TAG *tag2)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);
    HRESULT hr;

    TRACE("iface %p, tag1 %p, tag2 %p.\n", iface, tag1, tag2);

    if (context->target.type == D2D_TARGET_COMMAND_LIST)
    {
        FIXME("Unimplemented for command list target.\n");
        return E_NOTIMPL;
    }

    if (tag1)
        *tag1 = context->error.tag1;
    if (tag2)
        *tag2 = context->error.tag2;

    d2d_device_context_resolve_msaa(context);

    if (context->ops && context->ops->device_context_present)
    {
        if (FAILED(hr = context->ops->device_context_present(context->outer_unknown)))
            context->error.code = hr;
    }

    return context->error.code;
}

static D2D1_PIXEL_FORMAT * STDMETHODCALLTYPE d2d_device_context_GetPixelFormat(ID2D1DeviceContext6 *iface,
        D2D1_PIXEL_FORMAT *format)
{
    struct d2d_device_context *render_target = impl_from_ID2D1DeviceContext(iface);

    TRACE("iface %p, format %p.\n", iface, format);

    *format = render_target->desc.pixelFormat;
    return format;
}

static void STDMETHODCALLTYPE d2d_device_context_SetDpi(ID2D1DeviceContext6 *iface, float dpi_x, float dpi_y)
{
    struct d2d_device_context *render_target = impl_from_ID2D1DeviceContext(iface);

    TRACE("iface %p, dpi_x %.8e, dpi_y %.8e.\n", iface, dpi_x, dpi_y);

    if (dpi_x == 0.0f && dpi_y == 0.0f)
    {
        dpi_x = 96.0f;
        dpi_y = 96.0f;
    }
    else if (dpi_x <= 0.0f || dpi_y <= 0.0f)
        return;

    render_target->desc.dpiX = dpi_x;
    render_target->desc.dpiY = dpi_y;
}

static void STDMETHODCALLTYPE d2d_device_context_GetDpi(ID2D1DeviceContext6 *iface, float *dpi_x, float *dpi_y)
{
    struct d2d_device_context *render_target = impl_from_ID2D1DeviceContext(iface);

    TRACE("iface %p, dpi_x %p, dpi_y %p.\n", iface, dpi_x, dpi_y);

    *dpi_x = render_target->desc.dpiX;
    *dpi_y = render_target->desc.dpiY;
}

static D2D1_SIZE_F * STDMETHODCALLTYPE d2d_device_context_GetSize(ID2D1DeviceContext6 *iface, D2D1_SIZE_F *size)
{
    struct d2d_device_context *render_target = impl_from_ID2D1DeviceContext(iface);

    TRACE("iface %p, size %p.\n", iface, size);

    size->width = render_target->pixel_size.width / (render_target->desc.dpiX / 96.0f);
    size->height = render_target->pixel_size.height / (render_target->desc.dpiY / 96.0f);
    return size;
}

static D2D1_SIZE_U * STDMETHODCALLTYPE d2d_device_context_GetPixelSize(ID2D1DeviceContext6 *iface,
        D2D1_SIZE_U *pixel_size)
{
    struct d2d_device_context *render_target = impl_from_ID2D1DeviceContext(iface);

    TRACE("iface %p, pixel_size %p.\n", iface, pixel_size);

    *pixel_size = render_target->pixel_size;
    return pixel_size;
}

static UINT32 STDMETHODCALLTYPE d2d_device_context_GetMaximumBitmapSize(ID2D1DeviceContext6 *iface)
{
    TRACE("iface %p.\n", iface);

    return D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION;
}

static BOOL STDMETHODCALLTYPE d2d_device_context_IsSupported(ID2D1DeviceContext6 *iface,
        const D2D1_RENDER_TARGET_PROPERTIES *desc)
{
    FIXME("iface %p, desc %p stub!\n", iface, desc);

    return FALSE;
}

static HRESULT STDMETHODCALLTYPE d2d_device_context_ID2D1DeviceContext_CreateBitmap(ID2D1DeviceContext6 *iface,
        D2D1_SIZE_U size, const void *src_data, UINT32 pitch,
        const D2D1_BITMAP_PROPERTIES1 *desc, ID2D1Bitmap1 **bitmap)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);
    struct d2d_bitmap *object;
    HRESULT hr;

    TRACE("iface %p, size {%u, %u}, src_data %p, pitch %u, desc %p, bitmap %p.\n",
            iface, size.width, size.height, src_data, pitch, desc, bitmap);

    if (SUCCEEDED(hr = d2d_bitmap_create(context, size, src_data, pitch, desc, &object)))
        *bitmap = &object->ID2D1Bitmap1_iface;

    return hr;
}

static HRESULT STDMETHODCALLTYPE d2d_device_context_ID2D1DeviceContext_CreateBitmapFromWicBitmap(
        ID2D1DeviceContext6 *iface, IWICBitmapSource *bitmap_source,
        const D2D1_BITMAP_PROPERTIES1 *desc, ID2D1Bitmap1 **bitmap)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);
    struct d2d_bitmap *object;
    HRESULT hr;

    TRACE("iface %p, bitmap_source %p, desc %p, bitmap %p.\n", iface, bitmap_source, desc, bitmap);

    if (SUCCEEDED(hr = d2d_bitmap_create_from_wic_bitmap(context, bitmap_source, desc, &object)))
        *bitmap = &object->ID2D1Bitmap1_iface;

    return hr;
}

static HRESULT STDMETHODCALLTYPE d2d_device_context_CreateColorContext(ID2D1DeviceContext6 *iface,
        D2D1_COLOR_SPACE space, const BYTE *profile, UINT32 profile_size, ID2D1ColorContext **color_context)
{
    FIXME("iface %p, space %#x, profile %p, profile_size %u, color_context %p stub!\n",
            iface, space, profile, profile_size, color_context);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_device_context_CreateColorContextFromFilename(ID2D1DeviceContext6 *iface,
        const WCHAR *filename, ID2D1ColorContext **color_context)
{
    FIXME("iface %p, filename %s, color_context %p stub!\n", iface, debugstr_w(filename), color_context);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_device_context_CreateColorContextFromWicColorContext(ID2D1DeviceContext6 *iface,
        IWICColorContext *wic_color_context, ID2D1ColorContext **color_context)
{
    FIXME("iface %p, wic_color_context %p, color_context %p stub!\n", iface, wic_color_context, color_context);

    return E_NOTIMPL;
}

static BOOL d2d_bitmap_check_options_with_surface(unsigned int options, unsigned int surface_options)
{
    switch (options)
    {
        case D2D1_BITMAP_OPTIONS_NONE:
        case D2D1_BITMAP_OPTIONS_TARGET:
        case D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW:
        case D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW | D2D1_BITMAP_OPTIONS_GDI_COMPATIBLE:
        case D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_GDI_COMPATIBLE:
        case D2D1_BITMAP_OPTIONS_CANNOT_DRAW | D2D1_BITMAP_OPTIONS_CPU_READ:
        case D2D1_BITMAP_OPTIONS_CANNOT_DRAW:
            break;
        default:
            WARN("Invalid bitmap options %#x.\n", options);
            return FALSE;
    }

    if (options && (options & D2D1_BITMAP_OPTIONS_TARGET) != (surface_options & D2D1_BITMAP_OPTIONS_TARGET))
        return FALSE;
    if (!(options & D2D1_BITMAP_OPTIONS_CANNOT_DRAW) && (surface_options & D2D1_BITMAP_OPTIONS_CANNOT_DRAW))
        return FALSE;
    if (options & D2D1_BITMAP_OPTIONS_TARGET)
    {
        if (options & D2D1_BITMAP_OPTIONS_GDI_COMPATIBLE && !(surface_options & D2D1_BITMAP_OPTIONS_GDI_COMPATIBLE))
            return FALSE;
        return TRUE;
    }

    if (options & D2D1_BITMAP_OPTIONS_CANNOT_DRAW)
    {
        if (!(surface_options & D2D1_BITMAP_OPTIONS_CANNOT_DRAW))
            return FALSE;

        if (options & D2D1_BITMAP_OPTIONS_CPU_READ && !(surface_options & D2D1_BITMAP_OPTIONS_CPU_READ))
            return FALSE;
    }

    return TRUE;
}

static HRESULT STDMETHODCALLTYPE d2d_device_context_CreateBitmapFromDxgiSurface(ID2D1DeviceContext6 *iface,
        IDXGISurface *surface, const D2D1_BITMAP_PROPERTIES1 *desc, ID2D1Bitmap1 **bitmap)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);
    D2D1_BITMAP_PROPERTIES1 bitmap_desc;
    unsigned int surface_options;
    struct d2d_bitmap *object;
    HRESULT hr;

    TRACE("iface %p, surface %p, desc %p, bitmap %p.\n", iface, surface, desc, bitmap);

    surface_options = d2d_get_bitmap_options_for_surface(surface);

    if (desc)
    {
        if (!d2d_bitmap_check_options_with_surface(desc->bitmapOptions, surface_options))
        {
            WARN("Incompatible bitmap options %#x, surface options %#x.\n",
                    desc->bitmapOptions, surface_options);
            return E_INVALIDARG;
        }
    }
    else
    {
        DXGI_SURFACE_DESC surface_desc;

        if (FAILED(hr = IDXGISurface_GetDesc(surface, &surface_desc)))
        {
            WARN("Failed to get surface desc, hr %#lx.\n", hr);
            return hr;
        }

        memset(&bitmap_desc, 0, sizeof(bitmap_desc));
        bitmap_desc.pixelFormat.format = surface_desc.Format;
        bitmap_desc.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
        bitmap_desc.bitmapOptions = surface_options;
        desc = &bitmap_desc;
    }

    if (SUCCEEDED(hr = d2d_bitmap_create_shared(context, &IID_IDXGISurface, surface, desc, &object)))
        *bitmap = &object->ID2D1Bitmap1_iface;

    return hr;
}

static HRESULT STDMETHODCALLTYPE d2d_device_context_CreateEffect(ID2D1DeviceContext6 *iface,
        REFCLSID effect_id, ID2D1Effect **effect)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);

    TRACE("iface %p, effect_id %s, effect %p.\n", iface, debugstr_guid(effect_id), effect);

    return d2d_effect_create(context, effect_id, effect);
}

static HRESULT STDMETHODCALLTYPE d2d_device_context_ID2D1DeviceContext_CreateGradientStopCollection(
        ID2D1DeviceContext6 *iface, const D2D1_GRADIENT_STOP *stops, UINT32 stop_count,
        D2D1_COLOR_SPACE preinterpolation_space, D2D1_COLOR_SPACE postinterpolation_space,
        D2D1_BUFFER_PRECISION buffer_precision, D2D1_EXTEND_MODE extend_mode,
        D2D1_COLOR_INTERPOLATION_MODE color_interpolation_mode, ID2D1GradientStopCollection1 **gradient)
{
    FIXME("iface %p, stops %p, stop_count %u, preinterpolation_space %#x, postinterpolation_space %#x, "
            "buffer_precision %#x, extend_mode %#x, color_interpolation_mode %#x, gradient %p stub!\n",
            iface, stops, stop_count, preinterpolation_space, postinterpolation_space,
            buffer_precision, extend_mode, color_interpolation_mode, gradient);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_device_context_CreateImageBrush(ID2D1DeviceContext6 *iface,
        ID2D1Image *image, const D2D1_IMAGE_BRUSH_PROPERTIES *image_brush_desc,
        const D2D1_BRUSH_PROPERTIES *brush_desc, ID2D1ImageBrush **brush)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);
    struct d2d_brush *object;
    HRESULT hr;

    TRACE("iface %p, image %p, image_brush_desc %p, brush_desc %p, brush %p.\n", iface, image, image_brush_desc,
            brush_desc, brush);

    if (SUCCEEDED(hr = d2d_image_brush_create(context->factory, image, image_brush_desc,
            brush_desc, &object)))
        *brush = (ID2D1ImageBrush *)&object->ID2D1Brush_iface;

    return hr;
}

static HRESULT STDMETHODCALLTYPE d2d_device_context_ID2D1DeviceContext_CreateBitmapBrush(ID2D1DeviceContext6 *iface,
        ID2D1Bitmap *bitmap, const D2D1_BITMAP_BRUSH_PROPERTIES1 *bitmap_brush_desc,
        const D2D1_BRUSH_PROPERTIES *brush_desc, ID2D1BitmapBrush1 **brush)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);
    struct d2d_brush *object;
    HRESULT hr;

    TRACE("iface %p, bitmap %p, bitmap_brush_desc %p, brush_desc %p, brush %p.\n", iface, bitmap, bitmap_brush_desc,
            brush_desc, brush);

    if (SUCCEEDED(hr = d2d_bitmap_brush_create(context->factory, bitmap, bitmap_brush_desc, brush_desc, &object)))
        *brush = (ID2D1BitmapBrush1 *)&object->ID2D1Brush_iface;

    return hr;
}

static HRESULT STDMETHODCALLTYPE d2d_device_context_CreateCommandList(ID2D1DeviceContext6 *iface,
        ID2D1CommandList **command_list)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);
    struct d2d_command_list *object;
    HRESULT hr;

    TRACE("iface %p, command_list %p.\n", iface, command_list);

    if (SUCCEEDED(hr = d2d_command_list_create(context->factory, &object)))
        *command_list = &object->ID2D1CommandList_iface;

    return hr;
}

static BOOL STDMETHODCALLTYPE d2d_device_context_IsDxgiFormatSupported(ID2D1DeviceContext6 *iface, DXGI_FORMAT format)
{
    FIXME("iface %p, format %#x stub!\n", iface, format);

    return FALSE;
}

static BOOL STDMETHODCALLTYPE d2d_device_context_IsBufferPrecisionSupported(ID2D1DeviceContext6 *iface,
        D2D1_BUFFER_PRECISION buffer_precision)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);
    DXGI_FORMAT format;
    UINT support = 0;
    HRESULT hr;

    TRACE("iface %p, buffer_precision %u.\n", iface, buffer_precision);

    switch (buffer_precision)
    {
        case D2D1_BUFFER_PRECISION_8BPC_UNORM: format = DXGI_FORMAT_R8G8B8A8_UNORM; break;
        case D2D1_BUFFER_PRECISION_8BPC_UNORM_SRGB: format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB; break;
        case D2D1_BUFFER_PRECISION_16BPC_UNORM: format = DXGI_FORMAT_R16G16B16A16_UNORM; break;
        case D2D1_BUFFER_PRECISION_16BPC_FLOAT: format = DXGI_FORMAT_R16G16B16A16_FLOAT; break;
        case D2D1_BUFFER_PRECISION_32BPC_FLOAT: format = DXGI_FORMAT_R32G32B32A32_FLOAT; break;
        default:
            WARN("Unexpected precision %u.\n", buffer_precision);
            return FALSE;
    }

    if (FAILED(hr = ID3D11Device1_CheckFormatSupport(context->d3d_device, format, &support)))
    {
        WARN("Format support check failed, hr %#lx.\n", hr);
    }

    return !!(support & D3D11_FORMAT_SUPPORT_BUFFER);
}

static HRESULT STDMETHODCALLTYPE d2d_device_context_GetImageLocalBounds(ID2D1DeviceContext6 *iface,
        ID2D1Image *image, D2D1_RECT_F *local_bounds)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);
    D2D_SIZE_U pixel_size;
    ID2D1Bitmap *bitmap;
    D2D_SIZE_F size;

    TRACE("iface %p, image %p, local_bounds %p.\n", iface, image, local_bounds);

    if (SUCCEEDED(ID2D1Image_QueryInterface(image, &IID_ID2D1Bitmap, (void **)&bitmap)))
    {
        local_bounds->left = 0.0f;
        local_bounds->top  = 0.0f;
        switch (context->drawing_state.unitMode)
        {
            case D2D1_UNIT_MODE_DIPS:
                size = ID2D1Bitmap_GetSize(bitmap);
                local_bounds->right  = size.width;
                local_bounds->bottom = size.height;
                break;

            case D2D1_UNIT_MODE_PIXELS:
                pixel_size = ID2D1Bitmap_GetPixelSize(bitmap);
                local_bounds->right  = pixel_size.width;
                local_bounds->bottom = pixel_size.height;
                break;

            default:
                WARN("Unknown unit mode %#x.\n", context->drawing_state.unitMode);
                break;
        }
        ID2D1Bitmap_Release(bitmap);

        return S_OK;
    }
    else
    {
        FIXME("Unable to get local bounds of image %p.\n", image);

        return E_NOTIMPL;
    }
}

static HRESULT STDMETHODCALLTYPE d2d_device_context_GetImageWorldBounds(ID2D1DeviceContext6 *iface,
        ID2D1Image *image, D2D1_RECT_F *world_bounds)
{
    FIXME("iface %p, image %p, world_bounds %p stub!\n", iface, image, world_bounds);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_device_context_GetGlyphRunWorldBounds(ID2D1DeviceContext6 *iface,
        D2D1_POINT_2F baseline_origin, const DWRITE_GLYPH_RUN *glyph_run,
        DWRITE_MEASURING_MODE measuring_mode, D2D1_RECT_F *bounds)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);
    DWRITE_TEXT_ANTIALIAS_MODE antialias_mode;
    DWRITE_RENDERING_MODE rendering_mode;
    IDWriteGlyphRunAnalysis *analysis;
    DWRITE_TEXTURE_TYPE texture_type;
    ID2D1PathGeometry *geometry;
    D2D1_MATRIX_3X2_F transform;
    float scale_x, scale_y;
    HRESULT hr;
    RECT rect;

    TRACE("iface %p, baseline_origin %s, glyph_run %p, measuring_mode %#x, bounds %p.\n",
            iface, debug_d2d_point_2f(&baseline_origin), glyph_run, measuring_mode, bounds);

    if (FAILED(hr = d2d_device_context_get_text_rendering_mode(context, glyph_run, measuring_mode,
            &antialias_mode, &rendering_mode)))
    {
        return hr;
    }

    if (rendering_mode == DWRITE_RENDERING_MODE_OUTLINE)
    {
        if (FAILED(hr = d2d_device_context_get_glyph_run_geometry(context, glyph_run, &geometry)))
            return hr;

        transform = context->drawing_state.transform;
        transform._31 += baseline_origin.x * transform._11 + baseline_origin.y * transform._21;
        transform._32 += baseline_origin.x * transform._12 + baseline_origin.y * transform._22;
        hr = ID2D1PathGeometry_GetBounds(geometry, &transform, bounds);
        ID2D1PathGeometry_Release(geometry);
    }
    else
    {
        if (FAILED(hr = d2d_device_context_get_glyph_run_analysis(context, baseline_origin,
                glyph_run, rendering_mode, measuring_mode, antialias_mode, &texture_type, &analysis)))
        {
            return hr;
        }

        if (SUCCEEDED(hr = IDWriteGlyphRunAnalysis_GetAlphaTextureBounds(analysis, texture_type, &rect)))
        {
            scale_x = context->desc.dpiX / 96.0f;
            scale_y = context->desc.dpiY / 96.0f;
            d2d_rect_set(bounds, rect.left / scale_x, rect.top / scale_y,
                    rect.right / scale_x, rect.bottom / scale_y);
        }

        IDWriteGlyphRunAnalysis_Release(analysis);
    }

    return hr;
}

static void STDMETHODCALLTYPE d2d_device_context_GetDevice(ID2D1DeviceContext6 *iface, ID2D1Device **device)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);

    TRACE("iface %p, device %p.\n", iface, device);

    *device = (ID2D1Device *)&context->device->ID2D1Device6_iface;
    ID2D1Device_AddRef(*device);
}

static void d2d_device_context_reset_target(struct d2d_device_context *context)
{
    if (!context->target.object)
        return;

    d2d_device_context_resolve_msaa(context);
    IUnknown_Release(context->target.object);
    memset(&context->target, 0, sizeof(context->target));

    /* Note that DPI settings are kept. */
    memset(&context->desc.pixelFormat, 0, sizeof(context->desc.pixelFormat));
    memset(&context->pixel_size, 0, sizeof(context->pixel_size));

    if (context->bs)
        ID3D11BlendState_Release(context->bs);
    context->bs = NULL;
}

static void STDMETHODCALLTYPE d2d_device_context_SetTarget(ID2D1DeviceContext6 *iface, ID2D1Image *target)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);
    struct d2d_command_list *command_list_impl;
    struct d2d_bitmap *bitmap_impl;
    ID2D1CommandList *command_list;
    D3D11_BLEND_DESC blend_desc;
    ID2D1Bitmap *bitmap;
    HRESULT hr;

    TRACE("iface %p, target %p.\n", iface, target);

    if (!target)
    {
        context->has_subrect = FALSE;
        d2d_device_context_reset_target(context);
        return;
    }

    if (SUCCEEDED(ID2D1Image_QueryInterface(target, &IID_ID2D1Bitmap, (void **)&bitmap)))
    {
        bitmap_impl = unsafe_impl_from_ID2D1Bitmap(bitmap);

        if (!(bitmap_impl->options & D2D1_BITMAP_OPTIONS_TARGET))
        {
            ID2D1Bitmap_Release(bitmap);
            d2d_device_context_set_error(context, D2DERR_INVALID_TARGET);
            return;
        }

        d2d_device_context_reset_target(context);

        /* Set sizes and pixel format. */
        context->pixel_size = bitmap_impl->pixel_size;
        context->desc.pixelFormat = bitmap_impl->format;
        context->target.bitmap = bitmap_impl;
        context->target.object = target;
        context->target.type = D2D_TARGET_BITMAP;

        memset(&blend_desc, 0, sizeof(blend_desc));
        blend_desc.IndependentBlendEnable = FALSE;
        blend_desc.RenderTarget[0].BlendEnable = TRUE;
        blend_desc.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
        blend_desc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        blend_desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        blend_desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        blend_desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        blend_desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        blend_desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        if (FAILED(hr = ID3D11Device1_CreateBlendState(context->d3d_device, &blend_desc, &context->bs)))
            WARN("Failed to create blend state, hr %#lx.\n", hr);
    }
    else if (SUCCEEDED(ID2D1Image_QueryInterface(target, &IID_ID2D1CommandList, (void **)&command_list)))
    {
        command_list_impl = unsafe_impl_from_ID2D1CommandList(command_list);

        d2d_device_context_reset_target(context);

        context->target.command_list = command_list_impl;
        context->target.object = target;
        context->target.type = D2D_TARGET_COMMAND_LIST;
    }
    else
    {
        WARN("Unsupported target type.\n");
    }
}

static void STDMETHODCALLTYPE d2d_device_context_GetTarget(ID2D1DeviceContext6 *iface, ID2D1Image **target)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);

    TRACE("iface %p, target %p.\n", iface, target);

    *target = context->target.object ? context->target.object : NULL;
    if (*target)
        ID2D1Image_AddRef(*target);
}

static void STDMETHODCALLTYPE d2d_device_context_SetRenderingControls(ID2D1DeviceContext6 *iface,
        const D2D1_RENDERING_CONTROLS *rendering_controls)
{
    FIXME("iface %p, rendering_controls %p stub!\n", iface, rendering_controls);
}

static void STDMETHODCALLTYPE d2d_device_context_GetRenderingControls(ID2D1DeviceContext6 *iface,
        D2D1_RENDERING_CONTROLS *rendering_controls)
{
    FIXME("iface %p, rendering_controls %p stub!\n", iface, rendering_controls);
}

static void STDMETHODCALLTYPE d2d_device_context_SetPrimitiveBlend(ID2D1DeviceContext6 *iface,
        D2D1_PRIMITIVE_BLEND primitive_blend)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);

    TRACE("iface %p, primitive_blend %u.\n", iface, primitive_blend);

    if (primitive_blend > D2D1_PRIMITIVE_BLEND_MAX)
    {
        WARN("Unknown blend mode %u.\n", primitive_blend);
        return;
    }

    if (context->target.type == D2D_TARGET_COMMAND_LIST)
        d2d_command_list_set_primitive_blend(context->target.command_list, primitive_blend);

    context->drawing_state.primitiveBlend = primitive_blend;
}

static D2D1_PRIMITIVE_BLEND STDMETHODCALLTYPE d2d_device_context_GetPrimitiveBlend(ID2D1DeviceContext6 *iface)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);

    TRACE("iface %p.\n", iface);

    return context->drawing_state.primitiveBlend;
}

static void STDMETHODCALLTYPE d2d_device_context_SetUnitMode(ID2D1DeviceContext6 *iface, D2D1_UNIT_MODE unit_mode)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);

    TRACE("iface %p, unit_mode %#x.\n", iface, unit_mode);

    if (unit_mode != D2D1_UNIT_MODE_DIPS && unit_mode != D2D1_UNIT_MODE_PIXELS)
    {
        WARN("Unknown unit mode %#x.\n", unit_mode);
        return;
    }

    if (context->target.type == D2D_TARGET_COMMAND_LIST)
        d2d_command_list_set_unit_mode(context->target.command_list, unit_mode);

    context->drawing_state.unitMode = unit_mode;
}

static D2D1_UNIT_MODE STDMETHODCALLTYPE d2d_device_context_GetUnitMode(ID2D1DeviceContext6 *iface)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);

    TRACE("iface %p.\n", iface);

    return context->drawing_state.unitMode;
}

static void STDMETHODCALLTYPE d2d_device_context_ID2D1DeviceContext_DrawGlyphRun(ID2D1DeviceContext6 *iface,
        D2D1_POINT_2F baseline_origin, const DWRITE_GLYPH_RUN *glyph_run,
        const DWRITE_GLYPH_RUN_DESCRIPTION *glyph_run_desc, ID2D1Brush *brush, DWRITE_MEASURING_MODE measuring_mode)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);

    TRACE("iface %p, baseline_origin %s, glyph_run %p, glyph_run_desc %p, brush %p, measuring_mode %#x.\n",
            iface, debug_d2d_point_2f(&baseline_origin), glyph_run, glyph_run_desc, brush, measuring_mode);

    d2d_device_context_draw_glyph_run(context, baseline_origin, glyph_run, glyph_run_desc, brush, measuring_mode);
}

static BOOL d2d_device_context_ensure_shadow_resources(struct d2d_device_context *context)
{
    ID3D11Device1 *device = context->d3d_device;

    if (!context->blit_vs)
        ID3D11Device1_CreateVertexShader(device, d2d1_blit_vs_dxbc,
                sizeof(d2d1_blit_vs_dxbc), NULL, &context->blit_vs);
    if (!context->shadow_h_ps)
        ID3D11Device1_CreatePixelShader(device, d2d1_shadow_h_ps_dxbc,
                sizeof(d2d1_shadow_h_ps_dxbc), NULL, &context->shadow_h_ps);
    if (!context->shadow_v_ps)
        ID3D11Device1_CreatePixelShader(device, d2d1_shadow_v_ps_dxbc,
                sizeof(d2d1_shadow_v_ps_dxbc), NULL, &context->shadow_v_ps);
    if (!context->shadow_cb)
    {
        D3D11_BUFFER_DESC cb_desc = {0};
        cb_desc.ByteWidth = sizeof(struct d2d_shadow_cb);
        cb_desc.Usage = D3D11_USAGE_DEFAULT;
        cb_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        ID3D11Device1_CreateBuffer(device, &cb_desc, NULL, &context->shadow_cb);
    }

    return context->blit_vs && context->shadow_h_ps && context->shadow_v_ps && context->shadow_cb;
}

static void d2d_device_context_draw_shadow(struct d2d_device_context *context, ID2D1Effect *effect,
        const D2D1_POINT_2F *target_offset, const D2D1_RECT_F *image_rect,
        D2D1_INTERPOLATION_MODE interpolation_mode)
{
    ID2D1Image *input = NULL;
    ID2D1Bitmap *src_bitmap = NULL;
    struct d2d_bitmap *src_bm, *shadow_bm = NULL;
    ID3D11Device1 *device = context->d3d_device;
    ID3D11Texture2D *src_copy_tex = NULL, *temp_tex = NULL, *out_tex = NULL;
    ID3D11ShaderResourceView *src_srv = NULL, *src_copy_srv = NULL, *temp_srv = NULL;
    ID3D11RenderTargetView *temp_rtv = NULL, *out_rtv = NULL;
    IDXGISurface *out_surface = NULL;
    D3D11_TEXTURE2D_DESC tex_desc = {0};
    D2D1_BITMAP_PROPERTIES1 bm_desc = {{0}};
    struct d2d_shadow_cb cb = {{0}};
    float blur_std_dev = 3.0f, sigma, dpi_x, dpi_y, pad_dip_x, pad_dip_y;
    D2D1_VECTOR_4F color = { 0.0f, 0.0f, 0.0f, 1.0f };
    UINT src_w, src_h, out_w, out_h;
    int radius, hop;

    ID2D1Effect_GetInput(effect, 0, &input);
    for (hop = 0; hop < 4 && input; ++hop)
    {
        ID2D1Effect *nested = NULL;
        if (SUCCEEDED(ID2D1Image_QueryInterface(input, &IID_ID2D1Bitmap, (void **)&src_bitmap)))
            break;
        if (SUCCEEDED(ID2D1Image_QueryInterface(input, &IID_ID2D1Effect, (void **)&nested)))
        {
            ID2D1Image *next = NULL;
            ID2D1Effect_GetInput(nested, 0, &next);
            ID2D1Effect_Release(nested);
            ID2D1Image_Release(input);
            input = next;
        }
        else
        {
            break;
        }
    }
    if (input)
        ID2D1Image_Release(input);

    if (!src_bitmap)
        return;

    src_bm = unsafe_impl_from_ID2D1Bitmap(src_bitmap);
    src_w = src_bm->pixel_size.width;
    src_h = src_bm->pixel_size.height;
    if (!src_w || !src_h || !src_bm->resource || !d2d_device_context_ensure_shadow_resources(context))
    {
        ID2D1Bitmap_Release(src_bitmap);
        return;
    }

    ID2D1Effect_GetValue(effect, D2D1_SHADOW_PROP_BLUR_STANDARD_DEVIATION, D2D1_PROPERTY_TYPE_FLOAT,
            (BYTE *)&blur_std_dev, sizeof(blur_std_dev));
    ID2D1Effect_GetValue(effect, D2D1_SHADOW_PROP_COLOR, D2D1_PROPERTY_TYPE_VECTOR4,
            (BYTE *)&color, sizeof(color));

    if (blur_std_dev < 0.0f)
        blur_std_dev = 0.0f;

    dpi_x = src_bm->dpi_x > 0.0f ? src_bm->dpi_x : 96.0f;
    dpi_y = src_bm->dpi_y > 0.0f ? src_bm->dpi_y : 96.0f;
    sigma = blur_std_dev * (dpi_x / 96.0f);
    radius = (sigma > 0.001f) ? (int)ceilf(3.0f * sigma) : 0;
    if (radius > 64)
        radius = 64;

    out_w = src_w + (UINT)(2 * radius);
    out_h = src_h + (UINT)(2 * radius);

    if (src_bm->srv)
    {
        src_srv = src_bm->srv;
    }
    else
    {
        ID3D11Texture2D *src_tex = NULL;
        D3D11_TEXTURE2D_DESC sdesc;
        if (FAILED(ID3D11Resource_QueryInterface(src_bm->resource, &IID_ID3D11Texture2D, (void **)&src_tex)))
            goto done;
        ID3D11Texture2D_GetDesc(src_tex, &sdesc);
        ID3D11Texture2D_Release(src_tex);

        sdesc.MipLevels = 1;
        sdesc.ArraySize = 1;
        sdesc.SampleDesc.Count = 1;
        sdesc.SampleDesc.Quality = 0;
        sdesc.Usage = D3D11_USAGE_DEFAULT;
        sdesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        sdesc.CPUAccessFlags = 0;
        sdesc.MiscFlags = 0;

        if (FAILED(ID3D11Device1_CreateTexture2D(device, &sdesc, NULL, &src_copy_tex)))
            goto done;
        if (FAILED(ID3D11Device1_CreateShaderResourceView(device, (ID3D11Resource *)src_copy_tex,
                NULL, &src_copy_srv)))
            goto done;
        src_srv = src_copy_srv;
    }

    tex_desc.Width = out_w;
    tex_desc.Height = out_h;
    tex_desc.MipLevels = 1;
    tex_desc.ArraySize = 1;
    tex_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    tex_desc.SampleDesc.Count = 1;
    tex_desc.Usage = D3D11_USAGE_DEFAULT;
    tex_desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    if (FAILED(ID3D11Device1_CreateTexture2D(device, &tex_desc, NULL, &temp_tex)))
        goto done;
    if (FAILED(ID3D11Device1_CreateRenderTargetView(device, (ID3D11Resource *)temp_tex, NULL, &temp_rtv)))
        goto done;
    if (FAILED(ID3D11Device1_CreateShaderResourceView(device, (ID3D11Resource *)temp_tex, NULL, &temp_srv)))
        goto done;

    tex_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    if (FAILED(ID3D11Device1_CreateTexture2D(device, &tex_desc, NULL, &out_tex)))
        goto done;
    if (FAILED(ID3D11Device1_CreateRenderTargetView(device, (ID3D11Resource *)out_tex, NULL, &out_rtv)))
        goto done;

    cb.color = color;
    cb.sigma = sigma;
    cb.radius = radius;
    cb.src_offset_x = -radius;
    cb.src_offset_y = -radius;
    cb.src_min_x = 0;
    cb.src_min_y = 0;
    cb.src_max_x = (int)src_w;
    cb.src_max_y = (int)src_h;

    {
        ID3D11DeviceContext1 *d3d_ctx;
        ID3DDeviceContextState *prev_state;
        D3D11_VIEWPORT vp = { 0.0f, 0.0f, (float)out_w, (float)out_h, 0.0f, 1.0f };
        D3D11_RECT sc = { 0, 0, (LONG)out_w, (LONG)out_h };
        ID3D11ShaderResourceView *null_srv = NULL;
        ID3D11RenderTargetView *null_rtv = NULL;
        unsigned int offset = 0;

        if (context->cs)
            EnterCriticalSection(context->cs);

        ID3D11Device1_GetImmediateContext1(device, &d3d_ctx);
        ID3D11DeviceContext1_SwapDeviceContextState(d3d_ctx, context->d3d_state, &prev_state);

        if (src_copy_tex)
            ID3D11DeviceContext1_CopyResource(d3d_ctx, (ID3D11Resource *)src_copy_tex, src_bm->resource);

        ID3D11DeviceContext1_UpdateSubresource(d3d_ctx, (ID3D11Resource *)context->shadow_cb, 0, NULL, &cb, 0, 0);

        ID3D11DeviceContext1_OMSetRenderTargets(d3d_ctx, 1, &temp_rtv, NULL);
        ID3D11DeviceContext1_OMSetBlendState(d3d_ctx, NULL, NULL, D3D11_DEFAULT_SAMPLE_MASK);
        ID3D11DeviceContext1_IASetInputLayout(d3d_ctx, context->shape_resources[D2D_SHAPE_TYPE_TRIANGLE].il);
        ID3D11DeviceContext1_IASetPrimitiveTopology(d3d_ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D11DeviceContext1_IASetIndexBuffer(d3d_ctx, context->ib, DXGI_FORMAT_R16_UINT, 0);
        ID3D11DeviceContext1_IASetVertexBuffers(d3d_ctx, 0, 1, &context->vb, &context->vb_stride, &offset);
        ID3D11DeviceContext1_VSSetShader(d3d_ctx, context->blit_vs, NULL, 0);
        ID3D11DeviceContext1_PSSetShader(d3d_ctx, context->shadow_h_ps, NULL, 0);
        ID3D11DeviceContext1_PSSetConstantBuffers(d3d_ctx, 0, 1, &context->shadow_cb);
        ID3D11DeviceContext1_PSSetShaderResources(d3d_ctx, 0, 1, &src_srv);
        ID3D11DeviceContext1_RSSetViewports(d3d_ctx, 1, &vp);
        ID3D11DeviceContext1_RSSetScissorRects(d3d_ctx, 1, &sc);
        ID3D11DeviceContext1_RSSetState(d3d_ctx, context->rs);
        ID3D11DeviceContext1_DrawIndexed(d3d_ctx, 6, 0, 0);
        ID3D11DeviceContext1_PSSetShaderResources(d3d_ctx, 0, 1, &null_srv);

        cb.src_max_y = (int)out_h;
        ID3D11DeviceContext1_UpdateSubresource(d3d_ctx, (ID3D11Resource *)context->shadow_cb, 0, NULL, &cb, 0, 0);

        ID3D11DeviceContext1_OMSetRenderTargets(d3d_ctx, 1, &out_rtv, NULL);
        ID3D11DeviceContext1_PSSetShader(d3d_ctx, context->shadow_v_ps, NULL, 0);
        ID3D11DeviceContext1_PSSetShaderResources(d3d_ctx, 0, 1, &temp_srv);
        ID3D11DeviceContext1_DrawIndexed(d3d_ctx, 6, 0, 0);
        ID3D11DeviceContext1_PSSetShaderResources(d3d_ctx, 0, 1, &null_srv);
        ID3D11DeviceContext1_OMSetRenderTargets(d3d_ctx, 1, &null_rtv, NULL);

        ID3D11DeviceContext1_SwapDeviceContextState(d3d_ctx, prev_state, NULL);
        ID3DDeviceContextState_Release(prev_state);
        ID3D11DeviceContext1_Release(d3d_ctx);

        if (context->cs)
            LeaveCriticalSection(context->cs);
    }

    if (FAILED(ID3D11Texture2D_QueryInterface(out_tex, &IID_IDXGISurface, (void **)&out_surface)))
        goto done;

    bm_desc.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
    bm_desc.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
    bm_desc.dpiX = dpi_x;
    bm_desc.dpiY = dpi_y;
    bm_desc.bitmapOptions = D2D1_BITMAP_OPTIONS_NONE;

    if (FAILED(d2d_bitmap_create_shared(context, &IID_IDXGISurface, out_surface, &bm_desc, &shadow_bm)))
        goto done;

    pad_dip_x = (float)radius * 96.0f / dpi_x;
    pad_dip_y = (float)radius * 96.0f / dpi_y;

    if (image_rect)
    {
        D2D1_RECT_F adj_rect =
        {
            image_rect->left + pad_dip_x,
            image_rect->top + pad_dip_y,
            image_rect->right + pad_dip_x,
            image_rect->bottom + pad_dip_y,
        };
        d2d_device_context_draw_bitmap(context, (ID2D1Bitmap *)&shadow_bm->ID2D1Bitmap1_iface, NULL, 1.0f,
                interpolation_mode, &adj_rect, target_offset, NULL);
    }
    else
    {
        D2D1_POINT_2F adj_offset =
        {
            (target_offset ? target_offset->x : 0.0f) - pad_dip_x,
            (target_offset ? target_offset->y : 0.0f) - pad_dip_y,
        };
        d2d_device_context_draw_bitmap(context, (ID2D1Bitmap *)&shadow_bm->ID2D1Bitmap1_iface, NULL, 1.0f,
                interpolation_mode, NULL, &adj_offset, NULL);
    }

done:
    if (shadow_bm)
        ID2D1Bitmap1_Release(&shadow_bm->ID2D1Bitmap1_iface);
    if (out_surface)
        IDXGISurface_Release(out_surface);
    if (out_rtv)
        ID3D11RenderTargetView_Release(out_rtv);
    if (out_tex)
        ID3D11Texture2D_Release(out_tex);
    if (temp_srv)
        ID3D11ShaderResourceView_Release(temp_srv);
    if (temp_rtv)
        ID3D11RenderTargetView_Release(temp_rtv);
    if (temp_tex)
        ID3D11Texture2D_Release(temp_tex);
    if (src_copy_srv)
        ID3D11ShaderResourceView_Release(src_copy_srv);
    if (src_copy_tex)
        ID3D11Texture2D_Release(src_copy_tex);
    ID2D1Bitmap_Release(src_bitmap);
}

static void STDMETHODCALLTYPE d2d_device_context_DrawImage(ID2D1DeviceContext6 *iface, ID2D1Image *image,
        const D2D1_POINT_2F *target_offset, const D2D1_RECT_F *image_rect, D2D1_INTERPOLATION_MODE interpolation_mode,
        D2D1_COMPOSITE_MODE composite_mode)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);
    ID2D1Bitmap *bitmap;

    TRACE("iface %p, image %p, target_offset %s, image_rect %s, interpolation_mode %#x, composite_mode %#x.\n",
            iface, image, debug_d2d_point_2f(target_offset), debug_d2d_rect_f(image_rect),
            interpolation_mode, composite_mode);

    if (FAILED(context->error.code))
        return;

    if (context->target.type == D2D_TARGET_UNKNOWN)
    {
        d2d_device_context_set_error(context, D2DERR_WRONG_STATE);
        return;
    }

    if (context->target.type == D2D_TARGET_COMMAND_LIST)
    {
        d2d_command_list_draw_image(context->target.command_list, image, target_offset, image_rect,
                interpolation_mode, composite_mode);
        return;
    }

    if (composite_mode != D2D1_COMPOSITE_MODE_SOURCE_OVER)
        FIXME("Unhandled composite mode %#x.\n", composite_mode);

    if (SUCCEEDED(ID2D1Image_QueryInterface(image, &IID_ID2D1Bitmap, (void **)&bitmap)))
    {
        d2d_device_context_draw_bitmap(context, bitmap, NULL, 1.0f, interpolation_mode, image_rect, target_offset, NULL);

        ID2D1Bitmap_Release(bitmap);
        return;
    }

    {
        ID2D1Effect *effect;
        if (SUCCEEDED(ID2D1Image_QueryInterface(image, &IID_ID2D1Effect, (void **)&effect)))
        {
            struct d2d_effect *effect_impl = CONTAINING_RECORD(effect, struct d2d_effect, ID2D1Effect_iface);
            if (IsEqualGUID(&effect_impl->clsid, &CLSID_D2D1Shadow))
            {
                d2d_device_context_draw_shadow(context, effect, target_offset, image_rect, interpolation_mode);
            }
            else
            {
                ID2D1Image *input = NULL;
                TRACE("Forwarding input image for unhandled ID2D1Effect %p.\n", effect);
                ID2D1Effect_GetInput(effect, 0, &input);
                if (input)
                {
                    ID2D1DeviceContext6_DrawImage(iface, input, target_offset, image_rect,
                            interpolation_mode, composite_mode);
                    ID2D1Image_Release(input);
                }
            }
            ID2D1Effect_Release(effect);
            return;
        }
    }

    FIXME("Unhandled image %p.\n", image);
}

static void STDMETHODCALLTYPE d2d_device_context_DrawGdiMetafile(ID2D1DeviceContext6 *iface,
        ID2D1GdiMetafile *metafile, const D2D1_POINT_2F *target_offset)
{
    FIXME("iface %p, metafile %p, target_offset %s stub!\n",
            iface, metafile, debug_d2d_point_2f(target_offset));
}

static void STDMETHODCALLTYPE d2d_device_context_ID2D1DeviceContext_DrawBitmap(ID2D1DeviceContext6 *iface,
        ID2D1Bitmap *bitmap, const D2D1_RECT_F *dst_rect, float opacity, D2D1_INTERPOLATION_MODE interpolation_mode,
        const D2D1_RECT_F *src_rect, const D2D1_MATRIX_4X4_F *perspective_transform)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);

    TRACE("iface %p, bitmap %p, dst_rect %s, opacity %.8e, interpolation_mode %#x, "
            "src_rect %s, perspective_transform %p.\n",
            iface, bitmap, debug_d2d_rect_f(dst_rect), opacity, interpolation_mode,
            debug_d2d_rect_f(src_rect), perspective_transform);

    if (FAILED(context->error.code))
        return;

    if (context->target.type == D2D_TARGET_UNKNOWN)
    {
        d2d_device_context_set_error(context, D2DERR_WRONG_STATE);
        return;
    }

    if (context->target.type == D2D_TARGET_COMMAND_LIST)
    {
        d2d_command_list_draw_bitmap(context->target.command_list, bitmap, dst_rect, opacity, interpolation_mode,
                src_rect, perspective_transform);
    }
    else
    {
        d2d_device_context_draw_bitmap(context, bitmap, dst_rect, opacity, interpolation_mode, src_rect,
                NULL, perspective_transform);
    }
}

static void STDMETHODCALLTYPE d2d_device_context_ID2D1DeviceContext_PushLayer(ID2D1DeviceContext6 *iface,
        const D2D1_LAYER_PARAMETERS1 *layer_parameters, ID2D1Layer *layer)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);

    TRACE("iface %p, layer_parameters %p, layer %p.\n", iface, layer_parameters, layer);

    if (context->target.type == D2D_TARGET_COMMAND_LIST)
        d2d_command_list_push_layer(context->target.command_list, context, layer_parameters);

    d2d_device_context_push_layer_clip(context,
            layer_parameters ? &layer_parameters->contentBounds : NULL,
            layer_parameters ? layer_parameters->geometricMask : NULL,
            layer_parameters ? &layer_parameters->maskTransform : NULL);
}

static HRESULT STDMETHODCALLTYPE d2d_device_context_InvalidateEffectInputRectangle(ID2D1DeviceContext6 *iface,
        ID2D1Effect *effect, UINT32 input, const D2D1_RECT_F *input_rect)
{
    FIXME("iface %p, effect %p, input %u, input_rect %s stub!\n",
            iface, effect, input, debug_d2d_rect_f(input_rect));

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_device_context_GetEffectInvalidRectangleCount(ID2D1DeviceContext6 *iface,
        ID2D1Effect *effect, UINT32 *rect_count)
{
    FIXME("iface %p, effect %p, rect_count %p stub!\n", iface, effect, rect_count);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_device_context_GetEffectInvalidRectangles(ID2D1DeviceContext6 *iface,
        ID2D1Effect *effect, D2D1_RECT_F *rectangles, UINT32 rect_count)
{
    FIXME("iface %p, effect %p, rectangles %p, rect_count %u stub!\n", iface, effect, rectangles, rect_count);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_device_context_GetEffectRequiredInputRectangles(ID2D1DeviceContext6 *iface,
        ID2D1Effect *effect, const D2D1_RECT_F *image_rect, const D2D1_EFFECT_INPUT_DESCRIPTION *desc,
        D2D1_RECT_F *input_rect, UINT32 input_count)
{
    FIXME("iface %p, effect %p, image_rect %s, desc %p, input_rect %p, input_count %u stub!\n",
            iface, effect, debug_d2d_rect_f(image_rect), desc, input_rect, input_count);

    return E_NOTIMPL;
}

static void STDMETHODCALLTYPE d2d_device_context_ID2D1DeviceContext_FillOpacityMask(ID2D1DeviceContext6 *iface,
        ID2D1Bitmap *mask, ID2D1Brush *brush, const D2D1_RECT_F *dst_rect, const D2D1_RECT_F *src_rect)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);

    FIXME("iface %p, mask %p, brush %p, dst_rect %s, src_rect %s stub!\n",
            iface, mask, brush, debug_d2d_rect_f(dst_rect), debug_d2d_rect_f(src_rect));

    if (FAILED(context->error.code))
        return;

    if (context->drawing_state.antialiasMode != D2D1_ANTIALIAS_MODE_ALIASED)
    {
        d2d_device_context_set_error(context, D2DERR_WRONG_STATE);
        return;
    }

    if (context->target.type == D2D_TARGET_COMMAND_LIST)
        d2d_command_list_fill_opacity_mask(context->target.command_list, context, mask, brush, dst_rect, src_rect);
}

static HRESULT STDMETHODCALLTYPE d2d_device_context_CreateFilledGeometryRealization(ID2D1DeviceContext6 *iface,
        ID2D1Geometry *geometry, float tolerance, ID2D1GeometryRealization **realization)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);
    struct d2d_geometry_realization *object;
    HRESULT hr;

    TRACE("iface %p, geometry %p, tolerance %.8e, realization %p.\n", iface, geometry, tolerance,
            realization);

    if (!(object = calloc(1, sizeof(*object))))
        return E_OUTOFMEMORY;

    if (FAILED(hr = d2d_geometry_realization_init(object, context->factory, geometry)))
    {
        WARN("Failed to initialise geometry realization, hr %#lx.\n", hr);
        free(object);
        return hr;
    }
    object->filled = true;

    TRACE("Created geometry realization %p.\n", object);
    *realization = &object->ID2D1GeometryRealization_iface;

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d2d_device_context_CreateStrokedGeometryRealization(
        ID2D1DeviceContext6 *iface, ID2D1Geometry *geometry, float tolerance, float stroke_width,
        ID2D1StrokeStyle *stroke_style, ID2D1GeometryRealization **realization)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);
    struct d2d_geometry_realization *object;
    HRESULT hr;

    TRACE("iface %p, geometry %p, tolerance %.8e, stroke_width %.8e, stroke_style %p, realization %p.\n",
            iface, geometry, tolerance, stroke_width, stroke_style, realization);

    if (!(object = calloc(1, sizeof(*object))))
        return E_OUTOFMEMORY;

    if (FAILED(hr = d2d_geometry_realization_init(object, context->factory, geometry)))
    {
        WARN("Failed to initialise geometry realization, hr %#lx.\n", hr);
        free(object);
        return hr;
    }
    object->stroke_width = stroke_width;
    object->stroke_style = stroke_style;
    if (object->stroke_style)
        ID2D1StrokeStyle_AddRef(object->stroke_style);

    TRACE("Created geometry realization %p.\n", object);
    *realization = &object->ID2D1GeometryRealization_iface;

    return S_OK;
}

static void STDMETHODCALLTYPE d2d_device_context_DrawGeometryRealization(ID2D1DeviceContext6 *iface,
        ID2D1GeometryRealization *realization, ID2D1Brush *brush)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);
    struct d2d_geometry_realization *r = unsafe_impl_from_ID2D1GeometryRealization(realization);

    FIXME("iface %p, realization %p, brush %p semi-stub!\n", iface, realization, brush);

    if (context->target.type == D2D_TARGET_COMMAND_LIST)
    {
        if (r->filled)
        {
            d2d_command_list_fill_geometry(context->target.command_list, context, r->geometry, brush, NULL);
        }
        else
        {
            d2d_command_list_draw_geometry(context->target.command_list, context, r->geometry, brush,
                    r->stroke_width, r->stroke_style);
        }
        return;
    }
}

static HRESULT STDMETHODCALLTYPE d2d_device_context_CreateInk(ID2D1DeviceContext6 *iface,
        const D2D1_INK_POINT *start_point, ID2D1Ink **ink)
{
    FIXME("iface %p, start_point %p, ink %p stub!\n", iface, start_point, ink);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_device_context_CreateInkStyle(ID2D1DeviceContext6 *iface,
        const D2D1_INK_STYLE_PROPERTIES *ink_style_properties, ID2D1InkStyle **ink_style)
{
    FIXME("iface %p, ink_style_properties %p, ink_style %p stub!\n", iface, ink_style_properties, ink_style);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_device_context_CreateGradientMesh(ID2D1DeviceContext6 *iface,
        const D2D1_GRADIENT_MESH_PATCH *patches, UINT32 patches_count,
        ID2D1GradientMesh **gradient_mesh)
{
    FIXME("iface %p, patches %p, patches_count %u, gradient_mesh %p stub!\n", iface, patches,
            patches_count, gradient_mesh);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_device_context_CreateImageSourceFromWic(ID2D1DeviceContext6 *iface,
        IWICBitmapSource *wic_bitmap_source, D2D1_IMAGE_SOURCE_LOADING_OPTIONS loading_options,
        D2D1_ALPHA_MODE alpha_mode, ID2D1ImageSourceFromWic **image_source)
{
    FIXME("iface %p, wic_bitmap_source %p, loading_options %#x, alpha_mode %u, image_source %p stub!\n",
            iface, wic_bitmap_source, loading_options, alpha_mode, image_source);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_device_context_CreateLookupTable3D(ID2D1DeviceContext6 *iface,
        D2D1_BUFFER_PRECISION precision, const UINT32 *extents, const BYTE *data,
        UINT32 data_count, const UINT32 *strides, ID2D1LookupTable3D **lookup_table)
{
    FIXME("iface %p, precision %u, extents %p, data %p, data_count %u, strides %p, lookup_table %p stub!\n",
            iface, precision, extents, data, data_count, strides, lookup_table);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_device_context_CreateImageSourceFromDxgi(ID2D1DeviceContext6 *iface,
        IDXGISurface **surfaces, UINT32 surface_count, DXGI_COLOR_SPACE_TYPE color_space,
        D2D1_IMAGE_SOURCE_FROM_DXGI_OPTIONS options, ID2D1ImageSource **image_source)
{
    FIXME("iface %p, surfaces %p, surface_count %u, color_space %u, options %#x, image_source %p stub!\n",
            iface, surfaces, surface_count, color_space, options, image_source);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_device_context_GetGradientMeshWorldBounds(ID2D1DeviceContext6 *iface,
        ID2D1GradientMesh *gradient_mesh, D2D1_RECT_F *bounds)
{
    FIXME("iface %p, gradient_mesh %p, bounds %p stub!\n", iface, gradient_mesh, bounds);

    return E_NOTIMPL;
}

static void STDMETHODCALLTYPE d2d_device_context_DrawInk(ID2D1DeviceContext6 *iface, ID2D1Ink *ink,
        ID2D1Brush *brush, ID2D1InkStyle *ink_style)
{
    FIXME("iface %p, ink %p, brush %p, ink_style %p stub!\n", iface, ink, brush, ink_style);
}

static void STDMETHODCALLTYPE d2d_device_context_DrawGradientMesh(ID2D1DeviceContext6 *iface,
        ID2D1GradientMesh *gradient_mesh)
{
    FIXME("iface %p, gradient_mesh %p stub!\n", iface, gradient_mesh);
}

static void STDMETHODCALLTYPE d2d_device_context_ID2D1DeviceContext2_DrawGdiMetafile(
        ID2D1DeviceContext6 *iface, ID2D1GdiMetafile *gdi_metafile, const D2D1_RECT_F *dst_rect,
        const D2D1_RECT_F *src_rect)
{
    FIXME("iface %p, gdi_metafile %p, dst_rect %s, src_rect %s stub!\n", iface, gdi_metafile,
            debug_d2d_rect_f(dst_rect), debug_d2d_rect_f(src_rect));
}

static HRESULT STDMETHODCALLTYPE d2d_device_context_CreateTransformedImageSource(ID2D1DeviceContext6 *iface,
        ID2D1ImageSource *source, const D2D1_TRANSFORMED_IMAGE_SOURCE_PROPERTIES *props,
        ID2D1TransformedImageSource **transformed)
{
    FIXME("iface %p, source %p, props %p, transformed %p stub!\n", iface, source, props, transformed);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_device_context_CreateSpriteBatch(ID2D1DeviceContext6 *iface,
        ID2D1SpriteBatch **sprite_batch)
{
    FIXME("iface %p, sprite_batch %p stub!\n", iface, sprite_batch);

    return E_NOTIMPL;
}

static void STDMETHODCALLTYPE d2d_device_context_DrawSpriteBatch(ID2D1DeviceContext6 *iface,
        ID2D1SpriteBatch *sprite_batch, UINT32 start_index, UINT32 sprite_count, ID2D1Bitmap *bitmap,
        D2D1_BITMAP_INTERPOLATION_MODE interpolation_mode, D2D1_SPRITE_OPTIONS sprite_options)
{
    FIXME("iface %p, sprite_batch %p, start_index %u, sprite_count %u, bitmap %p, interpolation_mode %u,"
            "sprite_options %u stub!\n", iface, sprite_batch, start_index, sprite_count, bitmap,
            interpolation_mode, sprite_options);
}

static HRESULT STDMETHODCALLTYPE d2d_device_context_CreateSvgGlyphStyle(ID2D1DeviceContext6 *iface,
        ID2D1SvgGlyphStyle **svg_glyph_style)
{
    FIXME("iface %p, svg_glyph_style %p stub!\n", iface, svg_glyph_style);

    return E_NOTIMPL;
}

static void STDMETHODCALLTYPE d2d_device_context_ID2D1DeviceContext4_DrawText(ID2D1DeviceContext6 *iface,
        const WCHAR *string, UINT32 string_length, IDWriteTextFormat *text_format, const D2D1_RECT_F *layout_rect,
        ID2D1Brush *default_fill_brush, ID2D1SvgGlyphStyle *svg_glyph_style, UINT32 color_palette_index,
        D2D1_DRAW_TEXT_OPTIONS options, DWRITE_MEASURING_MODE measuring_mode)
{
    FIXME("iface %p, string %s, string_length %u, text_format %p, layout_rect %s, default_fill_brush %p,"
            "svg_glyph_style %p, color_palette_index %u, options %#x, measuring_mode %u stub!\n",
            iface, debugstr_wn(string, string_length), string_length, text_format, debug_d2d_rect_f(layout_rect),
            default_fill_brush, svg_glyph_style, color_palette_index, options, measuring_mode);
}

static void STDMETHODCALLTYPE d2d_device_context_ID2D1DeviceContext4_DrawTextLayout(ID2D1DeviceContext6 *iface,
        D2D1_POINT_2F origin, IDWriteTextLayout *text_layout, ID2D1Brush *default_fill_brush,
        ID2D1SvgGlyphStyle *svg_glyph_style, UINT32 color_palette_index, D2D1_DRAW_TEXT_OPTIONS options)
{
    FIXME("iface %p, origin %s, text_layout %p, default_fill_brush %p, svg_glyph_style %p, color_palette_index %u,"
            "options %#x stub!\n", iface, debug_d2d_point_2f(&origin), text_layout, default_fill_brush,
            svg_glyph_style, color_palette_index, options);
}

static void STDMETHODCALLTYPE d2d_device_context_DrawColorBitmapGlyphRun(ID2D1DeviceContext6 *iface,
        DWRITE_GLYPH_IMAGE_FORMATS glyph_image_format, D2D1_POINT_2F baseline_origin, const DWRITE_GLYPH_RUN *glyph_run,
        DWRITE_MEASURING_MODE measuring_mode, D2D1_COLOR_BITMAP_GLYPH_SNAP_OPTION bitmap_snap_option)
{
    FIXME("iface %p, glyph_image_format %#x, baseline_origin %s, glyph_run %p, measuring_mode %u, bitmap_snap_option %#x stub!\n",
            iface, glyph_image_format, debug_d2d_point_2f(&baseline_origin), glyph_run, measuring_mode, bitmap_snap_option);
}

static void STDMETHODCALLTYPE d2d_device_context_DrawSvgGlyphRun(ID2D1DeviceContext6 *iface,
        D2D1_POINT_2F baseline_origin, const DWRITE_GLYPH_RUN *glyph_run, ID2D1Brush *default_fill_brush,
        ID2D1SvgGlyphStyle *svg_glyph_style, UINT32 color_palette_index, DWRITE_MEASURING_MODE measuring_mode)
{
    FIXME("iface %p, baseline_origin %s, glyph_run %p, default_fill_brush %p, svg_glyph_style %p,"
            "color_palette_index %u, measuring_mode %u stub!\n", iface, debug_d2d_point_2f(&baseline_origin),
            glyph_run, default_fill_brush, svg_glyph_style, color_palette_index, measuring_mode);
}

static HRESULT STDMETHODCALLTYPE d2d_device_context_GetColorBitmapGlyphImage(ID2D1DeviceContext6 *iface,
        DWRITE_GLYPH_IMAGE_FORMATS glyph_image_format, D2D1_POINT_2F glyph_origin, IDWriteFontFace *font_face,
        FLOAT font_em_size, UINT16 glyph_index, BOOL is_sideways, const D2D1_MATRIX_3X2_F *world_transform,
        FLOAT dpi_x, FLOAT dpi_y, D2D1_MATRIX_3X2_F *glyph_transform, ID2D1Image **glyph_image)
{
    FIXME("iface %p, glyph_image_format %u, glyph_origin %s, font_face %p, font_em_size %f, glyph_index %u,"
            "is_sideways %d, world_transform %p, dpi_x %f, dpi_y %f, glyph_transform %p, glyph_image %p stub!\n",
            iface, glyph_image_format, debug_d2d_point_2f(&glyph_origin), font_face, font_em_size, glyph_index,
            is_sideways, world_transform, dpi_x, dpi_y, glyph_transform, glyph_image);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_device_context_GetSvgGlyphImage(ID2D1DeviceContext6 *iface,
        D2D1_POINT_2F glyph_origin, IDWriteFontFace *font_face, FLOAT font_em_size, UINT16 glyph_index,
        BOOL is_sideways, const D2D1_MATRIX_3X2_F *world_transform, ID2D1Brush *default_fill_brush,
        ID2D1SvgGlyphStyle *svg_glyph_style, UINT32 color_palette_index, D2D1_MATRIX_3X2_F *glyph_transform,
        ID2D1CommandList **glyph_image)
{
    FIXME("iface %p, glyph_origin %s, font_face %p, font_em_size %f, glyph_index %u, is_sideways %d,"
            "world_transform %p, default_fill_brush %p, svg_glyph_style %p, color_palette_index %u,"
            "glyph_transform %p, glyph_image %p stub!\n", iface, debug_d2d_point_2f(&glyph_origin),
            font_face, font_em_size, glyph_index, is_sideways, world_transform, default_fill_brush,
            svg_glyph_style, color_palette_index, glyph_transform, glyph_image);

    return E_NOTIMPL;
}

static const GUID IID_ID2D1SvgDocument_local =
        {0x86b88e4d, 0xafa4, 0x4d7b, {0x88, 0xe4, 0x68, 0xa5, 0x1c, 0x4a, 0x0a, 0xec}};
static const GUID IID_ID2D1SvgElement_local =
        {0xac7b67a6, 0x183e, 0x49c1, {0xa8, 0x23, 0x0e, 0xbe, 0x40, 0xb0, 0xdb, 0x29}};

struct d2d_svg_document;

struct d2d_svg_root_element
{
    const void * const *lpVtbl;
    struct d2d_svg_document *doc;
};

struct d2d_svg_document
{
    const void * const *lpVtbl;
    LONG refcount;
    ID2D1Factory *factory;
    D2D1_SIZE_F viewport_size;
    float vb_x, vb_y, vb_w, vb_h;
    BOOL has_vb;
    char *xml_data;
    UINT32 xml_len;
    ID2D1Bitmap1 *cached_bitmap;
    ID2D1DeviceContext6 *cached_ctx;
    UINT32 cached_w;
    UINT32 cached_h;
    struct d2d_svg_document *source_doc;
    struct d2d_svg_root_element root;
};

static inline struct d2d_svg_document *svg_doc_from_iface(ID2D1SvgDocument *iface)
{
    return (struct d2d_svg_document *)iface;
}

static HRESULT STDMETHODCALLTYPE d2d_svg_doc_QueryInterface(ID2D1SvgDocument *iface, REFIID iid, void **out)
{
    if (IsEqualGUID(iid, &IID_ID2D1SvgDocument_local)
            || IsEqualGUID(iid, &IID_ID2D1Resource)
            || IsEqualGUID(iid, &IID_IUnknown))
    {
        IUnknown_AddRef((IUnknown *)iface);
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d2d_svg_doc_AddRef(ID2D1SvgDocument *iface)
{
    struct d2d_svg_document *doc = svg_doc_from_iface(iface);
    return InterlockedIncrement(&doc->refcount);
}

static ULONG STDMETHODCALLTYPE d2d_svg_doc_Release(ID2D1SvgDocument *iface)
{
    struct d2d_svg_document *doc = svg_doc_from_iface(iface);
    ULONG ref = InterlockedDecrement(&doc->refcount);
    if (!ref)
    {
        if (doc->cached_bitmap)
            ID2D1Bitmap1_Release(doc->cached_bitmap);
        free(doc->xml_data);
        if (doc->source_doc)
            d2d_svg_doc_Release((ID2D1SvgDocument *)doc->source_doc);
        if (doc->factory)
            ID2D1Factory_Release(doc->factory);
        free(doc);
    }
    return ref;
}

static void STDMETHODCALLTYPE d2d_svg_doc_GetFactory(ID2D1SvgDocument *iface, ID2D1Factory **factory)
{
    struct d2d_svg_document *doc = svg_doc_from_iface(iface);
    if (factory)
    {
        *factory = doc->factory;
        if (*factory)
            ID2D1Factory_AddRef(*factory);
    }
}

static HRESULT STDMETHODCALLTYPE d2d_svg_doc_SetViewportSize(ID2D1SvgDocument *iface, D2D1_SIZE_F size)
{
    struct d2d_svg_document *doc = svg_doc_from_iface(iface);
    doc->viewport_size = size;
    return S_OK;
}

static D2D1_SIZE_F * STDMETHODCALLTYPE d2d_svg_doc_GetViewportSize(ID2D1SvgDocument *iface, D2D1_SIZE_F *size)
{
    struct d2d_svg_document *doc = svg_doc_from_iface(iface);
    *size = doc->viewport_size;
    return size;
}

static const void * const d2d_svg_elem_vtbl[];

static HRESULT STDMETHODCALLTYPE d2d_svg_doc_SetRoot(ID2D1SvgDocument *iface, void *root)
{
    struct d2d_svg_document *doc = svg_doc_from_iface(iface);
    struct d2d_svg_root_element *elem = root;

    if (doc->source_doc)
    {
        d2d_svg_doc_Release((ID2D1SvgDocument *)doc->source_doc);
        doc->source_doc = NULL;
    }
    if (elem && elem->lpVtbl == d2d_svg_elem_vtbl && elem->doc && elem->doc != doc)
    {
        doc->source_doc = elem->doc;
        InterlockedIncrement(&doc->source_doc->refcount);
    }
    return S_OK;
}

static void STDMETHODCALLTYPE d2d_svg_doc_GetRoot(ID2D1SvgDocument *iface, void **root)
{
    struct d2d_svg_document *doc = svg_doc_from_iface(iface);
    if (!root) return;
    InterlockedIncrement(&doc->refcount);
    *root = &doc->root;
}

static HRESULT STDMETHODCALLTYPE d2d_svg_doc_FindElementById(ID2D1SvgDocument *iface, const WCHAR *id, void **elem)
{
    if (elem) *elem = NULL;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d2d_svg_doc_Serialize(ID2D1SvgDocument *iface,
        IStream *output_xml_stream, void *subtree)
{
    struct d2d_svg_document *doc = svg_doc_from_iface(iface);
    struct d2d_svg_document *src = (doc && doc->source_doc) ? doc->source_doc : doc;
    ULONG written = 0;

    if (!output_xml_stream)
        return E_INVALIDARG;
    if (src && src->xml_data && src->xml_len)
        return IStream_Write(output_xml_stream, src->xml_data, src->xml_len, &written);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d2d_svg_doc_Deserialize(ID2D1SvgDocument *iface,
        IStream *input_xml_stream, void **subtree)
{
    struct d2d_svg_document *doc = svg_doc_from_iface(iface);

    if (subtree)
    {
        InterlockedIncrement(&doc->refcount);
        *subtree = &doc->root;
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d2d_svg_doc_stub(ID2D1SvgDocument *iface)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_svg_elem_QueryInterface(struct d2d_svg_root_element *iface, REFIID iid, void **out)
{
    if (IsEqualGUID(iid, &IID_ID2D1SvgElement_local)
            || IsEqualGUID(iid, &IID_ID2D1Resource)
            || IsEqualGUID(iid, &IID_IUnknown))
    {
        InterlockedIncrement(&iface->doc->refcount);
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d2d_svg_elem_AddRef(struct d2d_svg_root_element *iface)
{
    return InterlockedIncrement(&iface->doc->refcount);
}

static ULONG STDMETHODCALLTYPE d2d_svg_elem_Release(struct d2d_svg_root_element *iface)
{
    return d2d_svg_doc_Release((ID2D1SvgDocument *)iface->doc);
}

static void STDMETHODCALLTYPE d2d_svg_elem_GetFactory(struct d2d_svg_root_element *iface, ID2D1Factory **factory)
{
    d2d_svg_doc_GetFactory((ID2D1SvgDocument *)iface->doc, factory);
}

static void STDMETHODCALLTYPE d2d_svg_elem_GetDocument(struct d2d_svg_root_element *iface, ID2D1SvgDocument **doc)
{
    if (!doc) return;
    InterlockedIncrement(&iface->doc->refcount);
    *doc = (ID2D1SvgDocument *)iface->doc;
}

static BOOL STDMETHODCALLTYPE d2d_svg_elem_IsAttributeSpecified(struct d2d_svg_root_element *iface,
        const WCHAR *name, BOOL *inherited)
{
    if (inherited) *inherited = FALSE;
    if (!name) return FALSE;
    if (!wcscmp(name, L"viewBox")) return iface->doc->has_vb;
    if (!wcscmp(name, L"width") || !wcscmp(name, L"height")) return !iface->doc->has_vb;
    return FALSE;
}

static HRESULT STDMETHODCALLTYPE d2d_svg_elem_GetAttributeValue(struct d2d_svg_root_element *iface,
        const WCHAR *name, UINT32 type, void *value, UINT32 value_size)
{
    if (!name || !value) return E_INVALIDARG;
    if (!wcscmp(name, L"viewBox") && value_size >= 16)
    {
        float *f = value;
        f[0] = iface->doc->vb_x;
        f[1] = iface->doc->vb_y;
        f[2] = iface->doc->vb_w;
        f[3] = iface->doc->vb_h;
        return S_OK;
    }
    if (!wcscmp(name, L"width") && value_size >= 4)
    {
        float *f = value;
        f[0] = iface->doc->has_vb ? iface->doc->vb_w : iface->doc->viewport_size.width;
        if (value_size >= 8) ((UINT32 *)value)[1] = 0; /* D2D1_SVG_LENGTH_UNITS_NUMBER */
        return S_OK;
    }
    if (!wcscmp(name, L"height") && value_size >= 4)
    {
        float *f = value;
        f[0] = iface->doc->has_vb ? iface->doc->vb_h : iface->doc->viewport_size.height;
        if (value_size >= 8) ((UINT32 *)value)[1] = 0; /* D2D1_SVG_LENGTH_UNITS_NUMBER */
        return S_OK;
    }
    return D2DERR_INVALID_PROPERTY;
}

static HRESULT STDMETHODCALLTYPE d2d_svg_elem_AppendChild(struct d2d_svg_root_element *iface, void *child)
{
    struct d2d_svg_root_element *ce = child;
    if (ce && ce->lpVtbl == d2d_svg_elem_vtbl && ce->doc && ce->doc != iface->doc)
        return d2d_svg_doc_SetRoot((ID2D1SvgDocument *)iface->doc, child);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d2d_svg_elem_GetTagName(struct d2d_svg_root_element *iface,
        WCHAR *name, UINT32 name_count)
{
    if (!name || !name_count) return E_INVALIDARG;
    lstrcpynW(name, L"svg", name_count);
    return S_OK;
}

static UINT32 STDMETHODCALLTYPE d2d_svg_elem_GetTagNameLength(struct d2d_svg_root_element *iface)
{
    return 3;
}

static BOOL STDMETHODCALLTYPE d2d_svg_elem_return_false(struct d2d_svg_root_element *iface)
{
    return FALSE;
}

static void STDMETHODCALLTYPE d2d_svg_elem_out_null(struct d2d_svg_root_element *iface, void **out)
{
    if (out) *out = NULL;
}

static HRESULT STDMETHODCALLTYPE d2d_svg_elem_sibling_null(struct d2d_svg_root_element *iface,
        void *ref, void **out)
{
    if (out) *out = NULL;
    return S_OK;
}

static UINT32 STDMETHODCALLTYPE d2d_svg_elem_GetSpecifiedAttributeCount(struct d2d_svg_root_element *iface)
{
    return 0;
}

static HRESULT STDMETHODCALLTYPE d2d_svg_elem_GetAttributeValueLength(struct d2d_svg_root_element *iface,
        const WCHAR *name, UINT32 type, UINT32 *len)
{
    if (len) *len = 0;
    return D2DERR_INVALID_PROPERTY;
}

static HRESULT STDMETHODCALLTYPE d2d_svg_elem_stub(struct d2d_svg_root_element *iface)
{
    return S_OK;
}

/* ID2D1SvgElement vtable (slots 0..33) */
static const void * const d2d_svg_elem_vtbl[] =
{
    d2d_svg_elem_QueryInterface,       /* 0 */
    d2d_svg_elem_AddRef,               /* 1 */
    d2d_svg_elem_Release,              /* 2 */
    d2d_svg_elem_GetFactory,           /* 3 */
    d2d_svg_elem_GetDocument,          /* 4 */
    d2d_svg_elem_GetTagName,           /* 5 GetTagName */
    d2d_svg_elem_GetTagNameLength,     /* 6 GetTagNameLength */
    d2d_svg_elem_return_false,         /* 7 IsTextContent */
    d2d_svg_elem_out_null,             /* 8 GetParent */
    d2d_svg_elem_return_false,         /* 9 HasChildren */
    d2d_svg_elem_out_null,             /* 10 GetFirstChild */
    d2d_svg_elem_out_null,             /* 11 GetLastChild */
    d2d_svg_elem_sibling_null,         /* 12 GetPreviousChild */
    d2d_svg_elem_sibling_null,         /* 13 GetNextChild */
    d2d_svg_elem_stub,                 /* 14 InsertChildBefore */
    d2d_svg_elem_AppendChild,          /* 15 AppendChild */
    d2d_svg_elem_stub,                 /* 16 ReplaceChild */
    d2d_svg_elem_stub,                 /* 17 RemoveChild */
    d2d_svg_elem_stub,                 /* 18 CreateChild */
    d2d_svg_elem_IsAttributeSpecified, /* 19 */
    d2d_svg_elem_GetSpecifiedAttributeCount, /* 20 GetSpecifiedAttributeCount */
    d2d_svg_elem_stub,                 /* 21 GetSpecifiedAttributeName */
    d2d_svg_elem_stub,                 /* 22 GetSpecifiedAttributeNameLength */
    d2d_svg_elem_stub,                 /* 23 RemoveAttribute */
    d2d_svg_elem_stub,                 /* 24 SetTextValue */
    d2d_svg_elem_stub,                 /* 25 GetTextValue */
    d2d_svg_elem_stub,                 /* 26 GetTextValueLength */
    d2d_svg_elem_stub,                 /* 27 SetAttributeValue(str) */
    d2d_svg_elem_stub,                 /* 28 GetAttributeValue(str) */
    d2d_svg_elem_GetAttributeValueLength, /* 29 GetAttributeValueLength */
    d2d_svg_elem_stub,                 /* 30 SetAttributeValue(pod) */
    d2d_svg_elem_GetAttributeValue,    /* 31 GetAttributeValue(pod) */
    d2d_svg_elem_stub,                 /* 32 SetAttributeValue(obj) */
    d2d_svg_elem_stub,                 /* 33 GetAttributeValue(obj) */
};

static const void * const d2d_svg_doc_vtbl[] =
{
    d2d_svg_doc_QueryInterface,
    d2d_svg_doc_AddRef,
    d2d_svg_doc_Release,
    d2d_svg_doc_GetFactory,
    d2d_svg_doc_SetViewportSize,
    d2d_svg_doc_GetViewportSize,
    d2d_svg_doc_SetRoot,
    d2d_svg_doc_GetRoot,
    d2d_svg_doc_FindElementById,
    d2d_svg_doc_Serialize,
    d2d_svg_doc_Deserialize,
    d2d_svg_doc_stub,
    d2d_svg_doc_stub,
    d2d_svg_doc_stub,
    d2d_svg_doc_stub,
};

static HRESULT STDMETHODCALLTYPE d2d_device_context_CreateSvgDocument(ID2D1DeviceContext6 *iface,
        IStream *input_xml_stream, D2D1_SIZE_F viewport_size, ID2D1SvgDocument **svg_document)
{
    struct d2d_device_context *context = impl_from_ID2D1DeviceContext(iface);
    struct d2d_svg_document *doc;

    TRACE("iface %p, input_xml_stream %p, viewport_size {%.2f, %.2f}, svg_document %p.\n",
            iface, input_xml_stream, viewport_size.width, viewport_size.height, svg_document);

    if (!svg_document)
        return E_INVALIDARG;
    *svg_document = NULL;

    if (!(doc = calloc(1, sizeof(*doc))))
        return E_OUTOFMEMORY;

    doc->lpVtbl = d2d_svg_doc_vtbl;
    doc->refcount = 1;
    doc->factory = context->factory;
    if (doc->factory)
        ID2D1Factory_AddRef(doc->factory);
    doc->viewport_size = viewport_size;
    doc->root.lpVtbl = d2d_svg_elem_vtbl;
    doc->root.doc = doc;

    if (input_xml_stream)
    {
        LARGE_INTEGER zero = { .QuadPart = 0 };
        size_t cap = 8192, len = 0;
        char *buf = malloc(cap);

        IStream_Seek(input_xml_stream, zero, STREAM_SEEK_SET, NULL);
        if (buf)
        {
            for (;;)
            {
                ULONG got = 0;
                HRESULT hr;
                if (len + 4096 >= cap)
                {
                    char *nb;
                    if (cap >= 16u * 1024u * 1024u)
                        break;
                    cap *= 2;
                    if (!(nb = realloc(buf, cap)))
                        break;
                    buf = nb;
                }
                hr = IStream_Read(input_xml_stream, buf + len, 4096, &got);
                len += got;
                if (FAILED(hr) || got == 0)
                    break;
            }
            buf[len] = 0;
            if (len > 0)
            {
                struct d2d1_svg_dimensions_params dim_params;

                doc->xml_data = buf;
                doc->xml_len = (UINT32)len;

                memset(&dim_params, 0, sizeof(dim_params));
                dim_params.xml = doc->xml_data;
                dim_params.xml_len = doc->xml_len;
                if (!D2D1_UNIX_CALL(svg_get_dimensions, &dim_params) && dim_params.has_vb)
                {
                    doc->has_vb = TRUE;
                    doc->vb_x = dim_params.vb_x;
                    doc->vb_y = dim_params.vb_y;
                    doc->vb_w = dim_params.vb_w;
                    doc->vb_h = dim_params.vb_h;
                }
            }
            else
            {
                free(buf);
            }
        }

        if (doc->xml_len)
        {
            if (context->last_svg_doc)
                d2d_svg_doc_Release(context->last_svg_doc);
            context->last_svg_doc = (ID2D1SvgDocument *)doc;
            InterlockedIncrement(&doc->refcount);
        }
    }
    else if (context->last_svg_doc)
    {
        doc->source_doc = svg_doc_from_iface(context->last_svg_doc);
        InterlockedIncrement(&doc->source_doc->refcount);
    }

    TRACE("Created SVG document %p (xml_len %u, vb %.1f %.1f %.1f %.1f).\n",
            doc, doc->xml_len, doc->vb_x, doc->vb_y, doc->vb_w, doc->vb_h);
    *svg_document = (ID2D1SvgDocument *)doc;
    return S_OK;
}

static void STDMETHODCALLTYPE d2d_device_context_DrawSvgDocument(ID2D1DeviceContext6 *iface,
        ID2D1SvgDocument *svg_document)
{
    struct d2d_svg_document *doc = svg_doc_from_iface(svg_document);
    struct d2d_svg_document *src = (doc && doc->source_doc) ? doc->source_doc : doc;
    D2D1_MATRIX_3X2_F xf;
    D2D1_RECT_F dest_rect;
    float dpi_x = 96.0f, dpi_y = 96.0f;
    float scale_x, scale_y, vp_w, vp_h;
    UINT32 px_w, px_h;

    TRACE("iface %p, svg_document %p (src %p, xml_len %u).\n",
            iface, svg_document, src, src ? src->xml_len : 0);

    if (!doc || !src || !src->xml_data || !src->xml_len)
        return;

    vp_w = doc->viewport_size.width > 0.0f ? doc->viewport_size.width : (src->has_vb && src->vb_w > 0.0f ? src->vb_w : 24.0f);
    vp_h = doc->viewport_size.height > 0.0f ? doc->viewport_size.height : (src->has_vb && src->vb_h > 0.0f ? src->vb_h : 24.0f);

    ID2D1DeviceContext6_GetTransform(iface, &xf);
    ID2D1DeviceContext6_GetDpi(iface, &dpi_x, &dpi_y);
    if (dpi_x <= 0.0f) dpi_x = 96.0f;
    if (dpi_y <= 0.0f) dpi_y = 96.0f;

    scale_x = hypotf(xf._11, xf._12) * (dpi_x / 96.0f);
    scale_y = hypotf(xf._21, xf._22) * (dpi_y / 96.0f);
    if (scale_x < 1.0f) scale_x = 1.0f;
    if (scale_y < 1.0f) scale_y = 1.0f;

    px_w = (UINT32)ceilf(vp_w * scale_x);
    px_h = (UINT32)ceilf(vp_h * scale_y);
    if (px_w < 1) px_w = 1;
    if (px_h < 1) px_h = 1;
    if (px_w > 2048) px_w = 2048;
    if (px_h > 2048) px_h = 2048;

    if (!doc->cached_bitmap || doc->cached_ctx != iface || doc->cached_w != px_w || doc->cached_h != px_h)
    {
        UINT32 stride = px_w * 4;
        void *bgra = malloc((size_t)stride * px_h);

        if (doc->cached_bitmap)
        {
            ID2D1Bitmap1_Release(doc->cached_bitmap);
            doc->cached_bitmap = NULL;
        }
        doc->cached_ctx = NULL;
        doc->cached_w = 0;
        doc->cached_h = 0;

        if (bgra)
        {
            struct d2d1_svg_rasterize_params rparams;
            rparams.xml = src->xml_data;
            rparams.xml_len = src->xml_len;
            rparams.width = px_w;
            rparams.height = px_h;
            rparams.out_bgra = bgra;
            rparams.out_stride = stride;

            if (!D2D1_UNIX_CALL(svg_rasterize, &rparams))
            {
                D2D1_BITMAP_PROPERTIES1 bmp_props;
                D2D1_SIZE_U bmp_size = { px_w, px_h };

                memset(&bmp_props, 0, sizeof(bmp_props));
                bmp_props.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
                bmp_props.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
                bmp_props.dpiX = dpi_x;
                bmp_props.dpiY = dpi_y;

                if (SUCCEEDED(ID2D1DeviceContext6_CreateBitmap(iface, bmp_size, bgra, stride,
                        &bmp_props, &doc->cached_bitmap)))
                {
                    doc->cached_ctx = iface;
                    doc->cached_w = px_w;
                    doc->cached_h = px_h;
                }
            }
            free(bgra);
        }
    }

    if (!doc->cached_bitmap)
        return;

    dest_rect.left = 0.0f;
    dest_rect.top = 0.0f;
    dest_rect.right = vp_w;
    dest_rect.bottom = vp_h;
    ID2D1DeviceContext6_DrawBitmap(iface, (ID2D1Bitmap *)doc->cached_bitmap, &dest_rect, 1.0f,
            D2D1_INTERPOLATION_MODE_LINEAR, NULL, NULL);
}

static HRESULT STDMETHODCALLTYPE d2d_device_context_CreateColorContextFromDxgiColorSpace(
        ID2D1DeviceContext6 *iface, DXGI_COLOR_SPACE_TYPE color_space, ID2D1ColorContext1 **color_context)
{
    FIXME("iface %p, color_space %u, color_context %p stub!\n", iface, color_space, color_context);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_device_context_CreateColorContextFromSimpleColorProfile(
        ID2D1DeviceContext6 *iface, const D2D1_SIMPLE_COLOR_PROFILE *simple_profile, ID2D1ColorContext1 **color_context)
{
    FIXME("iface %p, simple_profile %p, color_context %p stub!\n", iface, simple_profile, color_context);

    return E_NOTIMPL;
}

static void STDMETHODCALLTYPE d2d_device_context_BlendImage(ID2D1DeviceContext6 *iface, ID2D1Image *image,
        D2D1_BLEND_MODE blend_mode, const D2D1_POINT_2F *target_offset, const D2D1_RECT_F *image_rect,
        D2D1_INTERPOLATION_MODE interpolation_mode)
{
    FIXME("iface %p, image %p, blend_mode %u, target_offset %s, image_rect %s, interpolation_mode %u stub!\n",
            iface, image, blend_mode, debug_d2d_point_2f(target_offset), debug_d2d_rect_f(image_rect),
            interpolation_mode);
}

static const struct ID2D1DeviceContext6Vtbl d2d_device_context_vtbl =
{
    d2d_device_context_QueryInterface,
    d2d_device_context_AddRef,
    d2d_device_context_Release,
    d2d_device_context_GetFactory,
    d2d_device_context_CreateBitmap,
    d2d_device_context_CreateBitmapFromWicBitmap,
    d2d_device_context_CreateSharedBitmap,
    d2d_device_context_CreateBitmapBrush,
    d2d_device_context_CreateSolidColorBrush,
    d2d_device_context_CreateGradientStopCollection,
    d2d_device_context_CreateLinearGradientBrush,
    d2d_device_context_CreateRadialGradientBrush,
    d2d_device_context_CreateCompatibleRenderTarget,
    d2d_device_context_CreateLayer,
    d2d_device_context_CreateMesh,
    d2d_device_context_DrawLine,
    d2d_device_context_DrawRectangle,
    d2d_device_context_FillRectangle,
    d2d_device_context_DrawRoundedRectangle,
    d2d_device_context_FillRoundedRectangle,
    d2d_device_context_DrawEllipse,
    d2d_device_context_FillEllipse,
    d2d_device_context_DrawGeometry,
    d2d_device_context_FillGeometry,
    d2d_device_context_FillMesh,
    d2d_device_context_FillOpacityMask,
    d2d_device_context_DrawBitmap,
    d2d_device_context_DrawText,
    d2d_device_context_DrawTextLayout,
    d2d_device_context_DrawGlyphRun,
    d2d_device_context_SetTransform,
    d2d_device_context_GetTransform,
    d2d_device_context_SetAntialiasMode,
    d2d_device_context_GetAntialiasMode,
    d2d_device_context_SetTextAntialiasMode,
    d2d_device_context_GetTextAntialiasMode,
    d2d_device_context_SetTextRenderingParams,
    d2d_device_context_GetTextRenderingParams,
    d2d_device_context_SetTags,
    d2d_device_context_GetTags,
    d2d_device_context_PushLayer,
    d2d_device_context_PopLayer,
    d2d_device_context_Flush,
    d2d_device_context_SaveDrawingState,
    d2d_device_context_RestoreDrawingState,
    d2d_device_context_PushAxisAlignedClip,
    d2d_device_context_PopAxisAlignedClip,
    d2d_device_context_Clear,
    d2d_device_context_BeginDraw,
    d2d_device_context_EndDraw,
    d2d_device_context_GetPixelFormat,
    d2d_device_context_SetDpi,
    d2d_device_context_GetDpi,
    d2d_device_context_GetSize,
    d2d_device_context_GetPixelSize,
    d2d_device_context_GetMaximumBitmapSize,
    d2d_device_context_IsSupported,
    d2d_device_context_ID2D1DeviceContext_CreateBitmap,
    d2d_device_context_ID2D1DeviceContext_CreateBitmapFromWicBitmap,
    d2d_device_context_CreateColorContext,
    d2d_device_context_CreateColorContextFromFilename,
    d2d_device_context_CreateColorContextFromWicColorContext,
    d2d_device_context_CreateBitmapFromDxgiSurface,
    d2d_device_context_CreateEffect,
    d2d_device_context_ID2D1DeviceContext_CreateGradientStopCollection,
    d2d_device_context_CreateImageBrush,
    d2d_device_context_ID2D1DeviceContext_CreateBitmapBrush,
    d2d_device_context_CreateCommandList,
    d2d_device_context_IsDxgiFormatSupported,
    d2d_device_context_IsBufferPrecisionSupported,
    d2d_device_context_GetImageLocalBounds,
    d2d_device_context_GetImageWorldBounds,
    d2d_device_context_GetGlyphRunWorldBounds,
    d2d_device_context_GetDevice,
    d2d_device_context_SetTarget,
    d2d_device_context_GetTarget,
    d2d_device_context_SetRenderingControls,
    d2d_device_context_GetRenderingControls,
    d2d_device_context_SetPrimitiveBlend,
    d2d_device_context_GetPrimitiveBlend,
    d2d_device_context_SetUnitMode,
    d2d_device_context_GetUnitMode,
    d2d_device_context_ID2D1DeviceContext_DrawGlyphRun,
    d2d_device_context_DrawImage,
    d2d_device_context_DrawGdiMetafile,
    d2d_device_context_ID2D1DeviceContext_DrawBitmap,
    d2d_device_context_ID2D1DeviceContext_PushLayer,
    d2d_device_context_InvalidateEffectInputRectangle,
    d2d_device_context_GetEffectInvalidRectangleCount,
    d2d_device_context_GetEffectInvalidRectangles,
    d2d_device_context_GetEffectRequiredInputRectangles,
    d2d_device_context_ID2D1DeviceContext_FillOpacityMask,
    d2d_device_context_CreateFilledGeometryRealization,
    d2d_device_context_CreateStrokedGeometryRealization,
    d2d_device_context_DrawGeometryRealization,
    d2d_device_context_CreateInk,
    d2d_device_context_CreateInkStyle,
    d2d_device_context_CreateGradientMesh,
    d2d_device_context_CreateImageSourceFromWic,
    d2d_device_context_CreateLookupTable3D,
    d2d_device_context_CreateImageSourceFromDxgi,
    d2d_device_context_GetGradientMeshWorldBounds,
    d2d_device_context_DrawInk,
    d2d_device_context_DrawGradientMesh,
    d2d_device_context_ID2D1DeviceContext2_DrawGdiMetafile,
    d2d_device_context_CreateTransformedImageSource,
    d2d_device_context_CreateSpriteBatch,
    d2d_device_context_DrawSpriteBatch,
    d2d_device_context_CreateSvgGlyphStyle,
    d2d_device_context_ID2D1DeviceContext4_DrawText,
    d2d_device_context_ID2D1DeviceContext4_DrawTextLayout,
    d2d_device_context_DrawColorBitmapGlyphRun,
    d2d_device_context_DrawSvgGlyphRun,
    d2d_device_context_GetColorBitmapGlyphImage,
    d2d_device_context_GetSvgGlyphImage,
    d2d_device_context_CreateSvgDocument,
    d2d_device_context_DrawSvgDocument,
    d2d_device_context_CreateColorContextFromDxgiColorSpace,
    d2d_device_context_CreateColorContextFromSimpleColorProfile,
    d2d_device_context_BlendImage,
};

static inline struct d2d_device_context *impl_from_IDWriteTextRenderer(IDWriteTextRenderer *iface)
{
    return CONTAINING_RECORD(iface, struct d2d_device_context, IDWriteTextRenderer_iface);
}

static HRESULT STDMETHODCALLTYPE d2d_text_renderer_QueryInterface(IDWriteTextRenderer *iface, REFIID iid, void **out)
{
    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_IDWriteTextRenderer)
            || IsEqualGUID(iid, &IID_IDWritePixelSnapping)
            || IsEqualGUID(iid, &IID_IUnknown))
    {
        IDWriteTextRenderer_AddRef(iface);
        *out = iface;
        return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid));

    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d2d_text_renderer_AddRef(IDWriteTextRenderer *iface)
{
    struct d2d_device_context *context = impl_from_IDWriteTextRenderer(iface);

    TRACE("iface %p.\n", iface);

    return d2d_device_context_AddRef(&context->ID2D1DeviceContext6_iface);
}

static ULONG STDMETHODCALLTYPE d2d_text_renderer_Release(IDWriteTextRenderer *iface)
{
    struct d2d_device_context *context = impl_from_IDWriteTextRenderer(iface);

    TRACE("iface %p.\n", iface);

    return d2d_device_context_Release(&context->ID2D1DeviceContext6_iface);
}

static HRESULT STDMETHODCALLTYPE d2d_text_renderer_IsPixelSnappingDisabled(IDWriteTextRenderer *iface,
        void *ctx, BOOL *disabled)
{
    struct d2d_draw_text_layout_ctx *context = ctx;

    TRACE("iface %p, ctx %p, disabled %p.\n", iface, ctx, disabled);

    *disabled = context->options & D2D1_DRAW_TEXT_OPTIONS_NO_SNAP;

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d2d_text_renderer_GetCurrentTransform(IDWriteTextRenderer *iface,
        void *ctx, DWRITE_MATRIX *transform)
{
    struct d2d_device_context *context = impl_from_IDWriteTextRenderer(iface);

    TRACE("iface %p, ctx %p, transform %p.\n", iface, ctx, transform);

    d2d_device_context_GetTransform(&context->ID2D1DeviceContext6_iface, (D2D1_MATRIX_3X2_F *)transform);

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d2d_text_renderer_GetPixelsPerDip(IDWriteTextRenderer *iface, void *ctx, float *ppd)
{
    struct d2d_device_context *render_target = impl_from_IDWriteTextRenderer(iface);

    TRACE("iface %p, ctx %p, ppd %p.\n", iface, ctx, ppd);

    *ppd = render_target->desc.dpiY / 96.0f;

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d2d_text_renderer_DrawGlyphRun(IDWriteTextRenderer *iface, void *ctx,
        float baseline_origin_x, float baseline_origin_y, DWRITE_MEASURING_MODE measuring_mode,
        const DWRITE_GLYPH_RUN *glyph_run, const DWRITE_GLYPH_RUN_DESCRIPTION *glyph_run_desc, IUnknown *effect)
{
    struct d2d_device_context *render_target = impl_from_IDWriteTextRenderer(iface);
    D2D1_POINT_2F baseline_origin = {baseline_origin_x, baseline_origin_y};
    struct d2d_draw_text_layout_ctx *context = ctx;
    BOOL color_font = FALSE;
    ID2D1Brush *brush;

    TRACE("iface %p, ctx %p, baseline_origin_x %.8e, baseline_origin_y %.8e, "
            "measuring_mode %#x, glyph_run %p, glyph_run_desc %p, effect %p.\n",
            iface, ctx, baseline_origin_x, baseline_origin_y,
            measuring_mode, glyph_run, glyph_run_desc, effect);

    if (context->options & ~(D2D1_DRAW_TEXT_OPTIONS_NO_SNAP | D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT))
        FIXME("Ignoring options %#x.\n", context->options);

    brush = d2d_draw_get_text_brush(context, effect);

    TRACE("%s\n", debugstr_wn(glyph_run_desc->string, glyph_run_desc->stringLength));

    if (context->options & D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT)
    {
        IDWriteFontFace2 *fontface;

        if (SUCCEEDED(IDWriteFontFace_QueryInterface(glyph_run->fontFace,
                &IID_IDWriteFontFace2, (void **)&fontface)))
        {
            color_font = IDWriteFontFace2_IsColorFont(fontface);
            IDWriteFontFace2_Release(fontface);
        }
    }

    if (color_font)
    {
        IDWriteColorGlyphRunEnumerator *layers;
        IDWriteFactory2 *dwrite_factory;
        HRESULT hr;

        if (FAILED(hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, &IID_IDWriteFactory2,
                (IUnknown **)&dwrite_factory)))
        {
            ERR("Failed to create dwrite factory, hr %#lx.\n", hr);
            ID2D1Brush_Release(brush);
            return hr;
        }

        hr = IDWriteFactory2_TranslateColorGlyphRun(dwrite_factory, baseline_origin_x, baseline_origin_y,
                glyph_run, glyph_run_desc, measuring_mode, (DWRITE_MATRIX *)&render_target->drawing_state.transform, 0, &layers);
        IDWriteFactory2_Release(dwrite_factory);
        if (FAILED(hr))
        {
            ERR("Failed to create colour glyph run enumerator, hr %#lx.\n", hr);
            ID2D1Brush_Release(brush);
            return hr;
        }

        for (;;)
        {
            const DWRITE_COLOR_GLYPH_RUN *color_run;
            ID2D1Brush *color_brush;
            D2D1_POINT_2F origin;
            BOOL has_run = FALSE;

            if (FAILED(hr = IDWriteColorGlyphRunEnumerator_MoveNext(layers, &has_run)))
            {
                ERR("Failed to switch colour glyph layer, hr %#lx.\n", hr);
                break;
            }

            if (!has_run)
                break;

            if (FAILED(hr = IDWriteColorGlyphRunEnumerator_GetCurrentRun(layers, &color_run)))
            {
                ERR("Failed to get current colour run, hr %#lx.\n", hr);
                break;
            }

            if (color_run->paletteIndex == 0xffff)
                color_brush = brush;
            else
            {
                if (FAILED(hr = d2d_device_context_CreateSolidColorBrush(&render_target->ID2D1DeviceContext6_iface,
                        &color_run->runColor, NULL, (ID2D1SolidColorBrush **)&color_brush)))
                {
                    ERR("Failed to create solid colour brush, hr %#lx.\n", hr);
                    break;
                }
            }

            origin.x = color_run->baselineOriginX;
            origin.y = color_run->baselineOriginY;
            d2d_device_context_draw_glyph_run(render_target, origin, &color_run->glyphRun,
                    color_run->glyphRunDescription, color_brush, measuring_mode);

            if (color_brush != brush)
                ID2D1Brush_Release(color_brush);
        }

        IDWriteColorGlyphRunEnumerator_Release(layers);
    }
    else
        d2d_device_context_draw_glyph_run(render_target, baseline_origin, glyph_run, glyph_run_desc,
                brush, measuring_mode);

    ID2D1Brush_Release(brush);

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d2d_text_renderer_DrawUnderline(IDWriteTextRenderer *iface, void *ctx,
        float baseline_origin_x, float baseline_origin_y, const DWRITE_UNDERLINE *underline, IUnknown *effect)
{
    struct d2d_device_context *render_target = impl_from_IDWriteTextRenderer(iface);
    const D2D1_MATRIX_3X2_F *m = &render_target->drawing_state.transform;
    struct d2d_draw_text_layout_ctx *context = ctx;
    D2D1_ANTIALIAS_MODE prev_antialias_mode;
    D2D1_POINT_2F start, end;
    ID2D1Brush *brush;
    float thickness;

    TRACE("iface %p, ctx %p, baseline_origin_x %.8e, baseline_origin_y %.8e, underline %p, effect %p\n",
            iface, ctx, baseline_origin_x, baseline_origin_y, underline, effect);

    /* minimal thickness in DIPs that will result in at least 1 pixel thick line */
    thickness = max(96.0f / (render_target->desc.dpiY * sqrtf(m->_21 * m->_21 + m->_22 * m->_22)),
            underline->thickness);

    brush = d2d_draw_get_text_brush(context, effect);

    start.x = baseline_origin_x;
    start.y = baseline_origin_y + underline->offset + thickness / 2.0f;
    end.x = start.x + underline->width;
    end.y = start.y;
    prev_antialias_mode = d2d_device_context_set_aa_mode_from_text_aa_mode(render_target);
    d2d_device_context_DrawLine(&render_target->ID2D1DeviceContext6_iface, start, end, brush, thickness, NULL);
    render_target->drawing_state.antialiasMode = prev_antialias_mode;

    ID2D1Brush_Release(brush);

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d2d_text_renderer_DrawStrikethrough(IDWriteTextRenderer *iface, void *ctx,
        float baseline_origin_x, float baseline_origin_y, const DWRITE_STRIKETHROUGH *strikethrough, IUnknown *effect)
{
    struct d2d_device_context *render_target = impl_from_IDWriteTextRenderer(iface);
    const D2D1_MATRIX_3X2_F *m = &render_target->drawing_state.transform;
    struct d2d_draw_text_layout_ctx *context = ctx;
    D2D1_ANTIALIAS_MODE prev_antialias_mode;
    D2D1_POINT_2F start, end;
    ID2D1Brush *brush;
    float thickness;

    TRACE("iface %p, ctx %p, baseline_origin_x %.8e, baseline_origin_y %.8e, strikethrough %p, effect %p.\n",
            iface, ctx, baseline_origin_x, baseline_origin_y, strikethrough, effect);

    /* minimal thickness in DIPs that will result in at least 1 pixel thick line */
    thickness = max(96.0f / (render_target->desc.dpiY * sqrtf(m->_21 * m->_21 + m->_22 * m->_22)),
            strikethrough->thickness);

    brush = d2d_draw_get_text_brush(context, effect);

    start.x = baseline_origin_x;
    start.y = baseline_origin_y + strikethrough->offset + thickness / 2.0f;
    end.x = start.x + strikethrough->width;
    end.y = start.y;
    prev_antialias_mode = d2d_device_context_set_aa_mode_from_text_aa_mode(render_target);
    d2d_device_context_DrawLine(&render_target->ID2D1DeviceContext6_iface, start, end, brush, thickness, NULL);
    render_target->drawing_state.antialiasMode = prev_antialias_mode;

    ID2D1Brush_Release(brush);

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d2d_text_renderer_DrawInlineObject(IDWriteTextRenderer *iface, void *ctx,
        float origin_x, float origin_y, IDWriteInlineObject *object, BOOL is_sideways, BOOL is_rtl, IUnknown *effect)
{
    struct d2d_draw_text_layout_ctx *context = ctx;
    ID2D1Brush *brush;
    HRESULT hr;

    TRACE("iface %p, ctx %p, origin_x %.8e, origin_y %.8e, object %p, is_sideways %#x, is_rtl %#x, effect %p.\n",
            iface, ctx, origin_x, origin_y, object, is_sideways, is_rtl, effect);

    /* Inline objects may not pass effects all the way down, when using layout object internally for example.
       This is how default trimming sign object in DirectWrite works - it does not use effect passed to Draw(),
       and resulting DrawGlyphRun() is always called with NULL effect, however original effect is used and correct
       brush is selected at Direct2D level. */
    brush = context->brush;
    context->brush = d2d_draw_get_text_brush(context, effect);

    hr = IDWriteInlineObject_Draw(object, ctx, iface, origin_x, origin_y, is_sideways, is_rtl, effect);

    ID2D1Brush_Release(context->brush);
    context->brush = brush;

    return hr;
}

static const struct IDWriteTextRendererVtbl d2d_text_renderer_vtbl =
{
    d2d_text_renderer_QueryInterface,
    d2d_text_renderer_AddRef,
    d2d_text_renderer_Release,
    d2d_text_renderer_IsPixelSnappingDisabled,
    d2d_text_renderer_GetCurrentTransform,
    d2d_text_renderer_GetPixelsPerDip,
    d2d_text_renderer_DrawGlyphRun,
    d2d_text_renderer_DrawUnderline,
    d2d_text_renderer_DrawStrikethrough,
    d2d_text_renderer_DrawInlineObject,
};

static inline struct d2d_device_context *impl_from_ID2D1GdiInteropRenderTarget(ID2D1GdiInteropRenderTarget *iface)
{
    return CONTAINING_RECORD(iface, struct d2d_device_context, ID2D1GdiInteropRenderTarget_iface);
}

static HRESULT STDMETHODCALLTYPE d2d_gdi_interop_render_target_QueryInterface(ID2D1GdiInteropRenderTarget *iface,
        REFIID iid, void **out)
{
    struct d2d_device_context *render_target = impl_from_ID2D1GdiInteropRenderTarget(iface);

    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    return IUnknown_QueryInterface(render_target->outer_unknown, iid, out);
}

static ULONG STDMETHODCALLTYPE d2d_gdi_interop_render_target_AddRef(ID2D1GdiInteropRenderTarget *iface)
{
    struct d2d_device_context *render_target = impl_from_ID2D1GdiInteropRenderTarget(iface);

    TRACE("iface %p.\n", iface);

    return IUnknown_AddRef(render_target->outer_unknown);
}

static ULONG STDMETHODCALLTYPE d2d_gdi_interop_render_target_Release(ID2D1GdiInteropRenderTarget *iface)
{
    struct d2d_device_context *render_target = impl_from_ID2D1GdiInteropRenderTarget(iface);

    TRACE("iface %p.\n", iface);

    return IUnknown_Release(render_target->outer_unknown);
}

static HRESULT d2d_gdi_interop_get_surface(struct d2d_device_context *context, IDXGISurface1 **surface)
{
    ID3D11Resource *resource;
    HRESULT hr;

    if (context->target.type != D2D_TARGET_BITMAP)
    {
        FIXME("Unimplemented for target type %u.\n", context->target.type);
        return E_NOTIMPL;
    }

    if (!(context->target.bitmap->options & D2D1_BITMAP_OPTIONS_GDI_COMPATIBLE))
        return D2DERR_TARGET_NOT_GDI_COMPATIBLE;

    ID3D11RenderTargetView_GetResource(context->target.bitmap->rtv, &resource);
    hr = ID3D11Resource_QueryInterface(resource, &IID_IDXGISurface1, (void **)surface);
    ID3D11Resource_Release(resource);
    if (FAILED(hr))
    {
        *surface = NULL;
        WARN("Failed to get DXGI surface, %#lx.\n", hr);
        return hr;
    }

    return hr;
}

static HRESULT STDMETHODCALLTYPE d2d_gdi_interop_render_target_GetDC(ID2D1GdiInteropRenderTarget *iface,
        D2D1_DC_INITIALIZE_MODE mode, HDC *dc)
{
    struct d2d_device_context *render_target = impl_from_ID2D1GdiInteropRenderTarget(iface);
    IDXGISurface1 *surface;
    HRESULT hr;

    TRACE("iface %p, mode %d, dc %p.\n", iface, mode, dc);

    *dc = NULL;

    if (render_target->target.hdc)
        return D2DERR_WRONG_STATE;

    d2d_device_context_resolve_msaa(render_target);

    if (FAILED(hr = d2d_gdi_interop_get_surface(render_target, &surface)))
        return hr;

    hr = IDXGISurface1_GetDC(surface, mode != D2D1_DC_INITIALIZE_MODE_COPY, &render_target->target.hdc);
    IDXGISurface1_Release(surface);

    if (SUCCEEDED(hr))
        *dc = render_target->target.hdc;

    return hr;
}

static HRESULT STDMETHODCALLTYPE d2d_gdi_interop_render_target_ReleaseDC(ID2D1GdiInteropRenderTarget *iface,
        const RECT *update)
{
    struct d2d_device_context *render_target = impl_from_ID2D1GdiInteropRenderTarget(iface);
    IDXGISurface1 *surface;
    RECT update_rect;
    HRESULT hr;

    TRACE("iface %p, update rect %s.\n", iface, wine_dbgstr_rect(update));

    if (!render_target->target.hdc)
        return D2DERR_WRONG_STATE;

    if (FAILED(hr = d2d_gdi_interop_get_surface(render_target, &surface)))
        return hr;

    render_target->target.hdc = NULL;
    if (update)
        update_rect = *update;
    hr = IDXGISurface1_ReleaseDC(surface, update ? &update_rect : NULL);
    IDXGISurface1_Release(surface);

    return hr;
}

static const struct ID2D1GdiInteropRenderTargetVtbl d2d_gdi_interop_render_target_vtbl =
{
    d2d_gdi_interop_render_target_QueryInterface,
    d2d_gdi_interop_render_target_AddRef,
    d2d_gdi_interop_render_target_Release,
    d2d_gdi_interop_render_target_GetDC,
    d2d_gdi_interop_render_target_ReleaseDC,
};


static const D3D11_INPUT_ELEMENT_DESC shape_il_desc_outline[] =
{
    {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"PREV", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"NEXT", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
};
static const D3D11_INPUT_ELEMENT_DESC shape_il_desc_curve_outline[] =
{
    {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"P", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"P", 1, DXGI_FORMAT_R32G32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"P", 2, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"PREV", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"NEXT", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 40, D3D11_INPUT_PER_VERTEX_DATA, 0},
};
static const D3D11_INPUT_ELEMENT_DESC shape_il_desc_triangle[] =
{
    {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
};
static const D3D11_INPUT_ELEMENT_DESC shape_il_desc_curve[] =
{
    {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0},
};
static const struct shape_info
{
    enum d2d_shape_type shape_type;
    const D3D11_INPUT_ELEMENT_DESC *il_desc;
    unsigned int il_element_count;
    const char *name;
    const unsigned char *vs_dxbc;
    size_t vs_dxbc_size;
}
shape_info[] =
{
    {D2D_SHAPE_TYPE_OUTLINE,        shape_il_desc_outline,        ARRAY_SIZE(shape_il_desc_outline),
     "outline",                     d2d1_shape_vs_outline_dxbc,        sizeof(d2d1_shape_vs_outline_dxbc)},
    {D2D_SHAPE_TYPE_BEZIER_OUTLINE, shape_il_desc_curve_outline,  ARRAY_SIZE(shape_il_desc_curve_outline),
     "bezier_outline",              d2d1_shape_vs_bezier_outline_dxbc, sizeof(d2d1_shape_vs_bezier_outline_dxbc)},
    {D2D_SHAPE_TYPE_ARC_OUTLINE,    shape_il_desc_curve_outline,  ARRAY_SIZE(shape_il_desc_curve_outline),
     "arc_outline",                 d2d1_shape_vs_arc_outline_dxbc,    sizeof(d2d1_shape_vs_arc_outline_dxbc)},
    {D2D_SHAPE_TYPE_TRIANGLE,       shape_il_desc_triangle,       ARRAY_SIZE(shape_il_desc_triangle),
     "triangle",                    d2d1_shape_vs_triangle_dxbc,       sizeof(d2d1_shape_vs_triangle_dxbc)},
    {D2D_SHAPE_TYPE_CURVE,          shape_il_desc_curve,          ARRAY_SIZE(shape_il_desc_curve),
     "curve",                       d2d1_shape_vs_curve_dxbc,          sizeof(d2d1_shape_vs_curve_dxbc)},
};

static HRESULT d2d_device_context_init(struct d2d_device_context *render_target,
        struct d2d_device *device, IUnknown *outer_unknown, const struct d2d_device_context_ops *ops)
{
    D3D11_SUBRESOURCE_DATA buffer_data;
    IDWriteFactory *dwrite_factory;
    D3D11_RASTERIZER_DESC rs_desc;
    D3D11_BUFFER_DESC buffer_desc;
    struct d2d_factory *factory;
    unsigned int i;
    HRESULT hr;

    static const struct
    {
        float x, y;
    }
    quad[] =
    {
        {-1.0f,  1.0f},
        {-1.0f, -1.0f},
        { 1.0f,  1.0f},
        { 1.0f, -1.0f},
    };
    static const UINT16 indices[] = {0, 1, 2, 2, 1, 3};
    static const D3D_FEATURE_LEVEL feature_levels = D3D_FEATURE_LEVEL_10_0;

    render_target->ID2D1DeviceContext6_iface.lpVtbl = &d2d_device_context_vtbl;
    render_target->ID2D1GdiInteropRenderTarget_iface.lpVtbl = &d2d_gdi_interop_render_target_vtbl;
    render_target->IDWriteTextRenderer_iface.lpVtbl = &d2d_text_renderer_vtbl;
    render_target->ID2D1PrivateCompositorDeviceContext_iface.lpVtbl = &d2d_priv_dc_vtbl;
    render_target->IUnknown_iface.lpVtbl = &d2d_device_context_inner_unknown_vtbl;
    render_target->refcount = 1;
    ID2D1Device1_GetFactory((ID2D1Device1 *)&device->ID2D1Device6_iface, &render_target->factory);
    render_target->device = device;
    ID2D1Device6_AddRef(&render_target->device->ID2D1Device6_iface);

    factory = unsafe_impl_from_ID2D1Factory(render_target->factory);
    if (factory->factory_type == D2D1_FACTORY_TYPE_MULTI_THREADED)
        render_target->cs = &factory->cs;

    render_target->outer_unknown = outer_unknown ? outer_unknown : &render_target->IUnknown_iface;
    render_target->ops = ops;

    if (FAILED(hr = IDXGIDevice_QueryInterface(device->dxgi_device,
            &IID_ID3D11Device1, (void **)&render_target->d3d_device)))
    {
        WARN("Failed to query ID3D11Device1 interface, hr %#lx.\n", hr);
        goto err;
    }

    if (FAILED(hr = ID3D11Device1_CreateDeviceContextState(render_target->d3d_device,
            0, &feature_levels, 1, D3D11_SDK_VERSION, &IID_ID3D11Device1, NULL,
            &render_target->d3d_state)))
    {
        WARN("Failed to create device context state, hr %#lx.\n", hr);
        goto err;
    }

    for (i = 0; i < ARRAY_SIZE(shape_info); ++i)
    {
        const struct shape_info *si = &shape_info[i];

        if (FAILED(hr = ID3D11Device1_CreateInputLayout(render_target->d3d_device, si->il_desc, si->il_element_count,
                si->vs_dxbc, si->vs_dxbc_size,
                &render_target->shape_resources[si->shape_type].il)))
        {
            WARN("Failed to create input layout for shape type %#x, hr %#lx.\n", si->shape_type, hr);
            goto err;
        }

        if (FAILED(hr = ID3D11Device1_CreateVertexShader(render_target->d3d_device,
                si->vs_dxbc, si->vs_dxbc_size,
                NULL, &render_target->shape_resources[si->shape_type].vs)))
        {
            WARN("Failed to create vertex shader for shape type %#x, hr %#lx.\n", si->shape_type, hr);
            goto err;
        }
    }

    buffer_desc.ByteWidth = sizeof(struct d2d_vs_cb);
    buffer_desc.Usage = D3D11_USAGE_DYNAMIC;
    buffer_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    buffer_desc.MiscFlags = 0;

    if (FAILED(hr = ID3D11Device1_CreateBuffer(render_target->d3d_device, &buffer_desc, NULL,
            &render_target->vs_cb)))
    {
        WARN("Failed to create constant buffer, hr %#lx.\n", hr);
        goto err;
    }

    if (FAILED(hr = ID3D11Device1_CreatePixelShader(render_target->d3d_device,
            d2d1_shape_ps_dxbc, sizeof(d2d1_shape_ps_dxbc),
            NULL, &render_target->ps)))
    {
        WARN("Failed to create pixel shader, hr %#lx.\n", hr);
        goto err;
    }

    buffer_desc.ByteWidth = sizeof(struct d2d_ps_cb);
    buffer_desc.Usage = D3D11_USAGE_DYNAMIC;
    buffer_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    buffer_desc.MiscFlags = 0;

    if (FAILED(hr = ID3D11Device1_CreateBuffer(render_target->d3d_device, &buffer_desc, NULL,
            &render_target->ps_cb)))
    {
        WARN("Failed to create constant buffer, hr %#lx.\n", hr);
        goto err;
    }

    buffer_desc.ByteWidth = sizeof(indices);
    buffer_desc.Usage = D3D11_USAGE_DEFAULT;
    buffer_desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    buffer_desc.CPUAccessFlags = 0;
    buffer_desc.MiscFlags = 0;

    buffer_data.pSysMem = indices;
    buffer_data.SysMemPitch = 0;
    buffer_data.SysMemSlicePitch = 0;

    if (FAILED(hr = ID3D11Device1_CreateBuffer(render_target->d3d_device,
            &buffer_desc, &buffer_data, &render_target->ib)))
    {
        WARN("Failed to create clear index buffer, hr %#lx.\n", hr);
        goto err;
    }

    buffer_desc.ByteWidth = sizeof(quad);
    buffer_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    buffer_data.pSysMem = quad;

    render_target->vb_stride = sizeof(*quad);
    if (FAILED(hr = ID3D11Device1_CreateBuffer(render_target->d3d_device,
            &buffer_desc, &buffer_data, &render_target->vb)))
    {
        WARN("Failed to create clear vertex buffer, hr %#lx.\n", hr);
        goto err;
    }

    rs_desc.FillMode = D3D11_FILL_SOLID;
    rs_desc.CullMode = D3D11_CULL_NONE;
    rs_desc.FrontCounterClockwise = FALSE;
    rs_desc.DepthBias = 0;
    rs_desc.DepthBiasClamp = 0.0f;
    rs_desc.SlopeScaledDepthBias = 0.0f;
    rs_desc.DepthClipEnable = TRUE;
    rs_desc.ScissorEnable = TRUE;
    rs_desc.MultisampleEnable = FALSE;
    rs_desc.AntialiasedLineEnable = FALSE;
    if (FAILED(hr = ID3D11Device1_CreateRasterizerState(render_target->d3d_device, &rs_desc, &render_target->rs)))
    {
        WARN("Failed to create clear rasteriser state, hr %#lx.\n", hr);
        goto err;
    }
    rs_desc.MultisampleEnable = TRUE;
    if (FAILED(hr = ID3D11Device1_CreateRasterizerState(render_target->d3d_device, &rs_desc, &render_target->rs_msaa)))
    {
        WARN("Failed to create MSAA rasteriser state, hr %#lx.\n", hr);
        render_target->rs_msaa = NULL;
    }

    if (FAILED(hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
            &IID_IDWriteFactory, (IUnknown **)&dwrite_factory)))
    {
        ERR("Failed to create dwrite factory, hr %#lx.\n", hr);
        goto err;
    }

    hr = IDWriteFactory_CreateRenderingParams(dwrite_factory, &render_target->default_text_rendering_params);
    IDWriteFactory_Release(dwrite_factory);
    if (FAILED(hr))
    {
        ERR("Failed to create default text rendering parameters, hr %#lx.\n", hr);
        goto err;
    }

    render_target->drawing_state.transform = identity;

    if (!d2d_clip_stack_init(&render_target->clip_stack))
    {
        WARN("Failed to initialize clip stack.\n");
        hr = E_FAIL;
        goto err;
    }

    render_target->desc.dpiX = 96.0f;
    render_target->desc.dpiY = 96.0f;

    return S_OK;

err:
    if (render_target->default_text_rendering_params)
        IDWriteRenderingParams_Release(render_target->default_text_rendering_params);
    if (render_target->rs)
        ID3D11RasterizerState_Release(render_target->rs);
    if (render_target->vb)
        ID3D11Buffer_Release(render_target->vb);
    if (render_target->ib)
        ID3D11Buffer_Release(render_target->ib);
    if (render_target->ps_cb)
        ID3D11Buffer_Release(render_target->ps_cb);
    if (render_target->ps)
        ID3D11PixelShader_Release(render_target->ps);
    if (render_target->vs_cb)
        ID3D11Buffer_Release(render_target->vs_cb);
    for (i = 0; i < D2D_SHAPE_TYPE_COUNT; ++i)
    {
        if (render_target->shape_resources[i].vs)
            ID3D11VertexShader_Release(render_target->shape_resources[i].vs);
        if (render_target->shape_resources[i].il)
            ID3D11InputLayout_Release(render_target->shape_resources[i].il);
    }
    if (render_target->d3d_state)
        ID3DDeviceContextState_Release(render_target->d3d_state);
    if (render_target->d3d_device)
        ID3D11Device1_Release(render_target->d3d_device);
    ID2D1Device6_Release(&render_target->device->ID2D1Device6_iface);
    ID2D1Factory_Release(render_target->factory);
    return hr;
}

HRESULT d2d_d3d_create_render_target(struct d2d_device *device, IDXGISurface *surface, IUnknown *outer_unknown,
        const struct d2d_device_context_ops *ops, const D2D1_RENDER_TARGET_PROPERTIES *desc, void **render_target)
{
    D2D1_BITMAP_PROPERTIES1 bitmap_desc;
    struct d2d_device_context *object;
    ID2D1Bitmap1 *bitmap;
    HRESULT hr;

    if (desc->type != D2D1_RENDER_TARGET_TYPE_DEFAULT && desc->type != D2D1_RENDER_TARGET_TYPE_HARDWARE)
        WARN("Ignoring render target type %#x.\n", desc->type);
    if (desc->usage != D2D1_RENDER_TARGET_USAGE_NONE)
        FIXME("Ignoring render target usage %#x.\n", desc->usage);
    if (desc->minLevel != D2D1_FEATURE_LEVEL_DEFAULT)
        WARN("Ignoring feature level %#x.\n", desc->minLevel);

    bitmap_desc.dpiX = desc->dpiX;
    bitmap_desc.dpiY = desc->dpiY;

    if (bitmap_desc.dpiX == 0.0f && bitmap_desc.dpiY == 0.0f)
    {
        bitmap_desc.dpiX = 96.0f;
        bitmap_desc.dpiY = 96.0f;
    }
    else if (bitmap_desc.dpiX <= 0.0f || bitmap_desc.dpiY <= 0.0f)
        return E_INVALIDARG;

    if (!(object = calloc(1, sizeof(*object))))
        return E_OUTOFMEMORY;

    if (FAILED(hr = d2d_device_context_init(object, device, outer_unknown, ops)))
    {
        WARN("Failed to initialise render target, hr %#lx.\n", hr);
        free(object);
        return hr;
    }

    ID2D1DeviceContext6_SetDpi(&object->ID2D1DeviceContext6_iface, bitmap_desc.dpiX, bitmap_desc.dpiY);

    if (surface)
    {
        bitmap_desc.pixelFormat = desc->pixelFormat;
        bitmap_desc.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
        if (desc->usage & D2D1_RENDER_TARGET_USAGE_GDI_COMPATIBLE)
            bitmap_desc.bitmapOptions |= D2D1_BITMAP_OPTIONS_GDI_COMPATIBLE;
        bitmap_desc.colorContext = NULL;

        if (FAILED(hr = ID2D1DeviceContext6_CreateBitmapFromDxgiSurface(&object->ID2D1DeviceContext6_iface,
                surface, &bitmap_desc, &bitmap)))
        {
            WARN("Failed to create target bitmap, hr %#lx.\n", hr);
            IUnknown_Release(&object->IUnknown_iface);
            return hr;
        }

        ID2D1DeviceContext6_SetTarget(&object->ID2D1DeviceContext6_iface, (ID2D1Image *)bitmap);
        ID2D1Bitmap1_Release(bitmap);
    }
    else
        object->desc.pixelFormat = desc->pixelFormat;

    TRACE("Created render target %p.\n", object);
    *render_target = outer_unknown ? &object->IUnknown_iface : (IUnknown *)&object->ID2D1DeviceContext6_iface;

    return S_OK;
}

static const GUID IID_ID2D1PrivateCompositorDevice_local =
    {0x46fdbfea, 0x53f8, 0x440c, {0xb9, 0xe2, 0x81, 0xdf, 0xec, 0x61, 0x2a, 0x72}};

static inline struct d2d_device *impl_from_ID2D1PrivateCompositorDevice(IUnknown *iface)
{
    return CONTAINING_RECORD(iface, struct d2d_device, ID2D1PrivateCompositorDevice_iface);
}

static HRESULT WINAPI d2d_priv_dev_QueryInterface(IUnknown *iface, REFIID iid, void **out)
{
    struct d2d_device *device = impl_from_ID2D1PrivateCompositorDevice(iface);
    return ID2D1Device6_QueryInterface(&device->ID2D1Device6_iface, iid, out);
}

static ULONG WINAPI d2d_priv_dev_AddRef(IUnknown *iface)
{
    struct d2d_device *device = impl_from_ID2D1PrivateCompositorDevice(iface);
    return ID2D1Device6_AddRef(&device->ID2D1Device6_iface);
}

static ULONG WINAPI d2d_priv_dev_Release(IUnknown *iface)
{
    struct d2d_device *device = impl_from_ID2D1PrivateCompositorDevice(iface);
    return ID2D1Device6_Release(&device->ID2D1Device6_iface);
}

static HRESULT WINAPI d2d_priv_dev_stub(IUnknown *iface)
{
    TRACE("iface %p stub.\n", iface);
    return S_OK;
}

static const void * const d2d_priv_dev_vtbl[] =
{
    d2d_priv_dev_QueryInterface,
    d2d_priv_dev_AddRef,
    d2d_priv_dev_Release,
    d2d_priv_dev_stub, /* 3 */
    d2d_priv_dev_stub, /* 4 */
    d2d_priv_dev_stub, /* 5 */
    d2d_priv_dev_stub, /* 6 */
    d2d_priv_dev_stub, /* 7 */
    d2d_priv_dev_stub, /* 8 */
    d2d_priv_dev_stub, /* 9 */
    d2d_priv_dev_stub, /* 10 */
    d2d_priv_dev_stub, /* 11 */
    d2d_priv_dev_stub, /* 12 */
    d2d_priv_dev_stub, /* 13 */
    d2d_priv_dev_stub, /* 14 */
    d2d_priv_dev_stub, /* 15 */
};

static HRESULT WINAPI d2d_device_QueryInterface(ID2D1Device6 *iface, REFIID iid, void **out)
{
    struct d2d_device *device = impl_from_ID2D1Device(iface);

    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_ID2D1PrivateCompositorDevice_local))
    {
        ID2D1Device6_AddRef(iface);
        *out = &device->ID2D1PrivateCompositorDevice_iface;
        return S_OK;
    }

    if (IsEqualGUID(iid, &IID_ID2D1Device6)
            || IsEqualGUID(iid, &IID_ID2D1Device5)
            || IsEqualGUID(iid, &IID_ID2D1Device4)
            || IsEqualGUID(iid, &IID_ID2D1Device3)
            || IsEqualGUID(iid, &IID_ID2D1Device2)
            || IsEqualGUID(iid, &IID_ID2D1Device1)
            || IsEqualGUID(iid, &IID_ID2D1Device)
            || IsEqualGUID(iid, &IID_ID2D1Device_ms)   /* published d2d1_1 ids, see d2d1_private.h */
            || IsEqualGUID(iid, &IID_ID2D1Device1_ms)
            || IsEqualGUID(iid, &IID_ID2D1Resource)
            || IsEqualGUID(iid, &IID_IUnknown))
    {
        ID2D1Device6_AddRef(iface);
        *out = iface;
        return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid));

    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI d2d_device_AddRef(ID2D1Device6 *iface)
{
    struct d2d_device *device = impl_from_ID2D1Device(iface);
    ULONG refcount = InterlockedIncrement(&device->refcount);

    TRACE("%p increasing refcount to %lu.\n", iface, refcount);

    return refcount;
}

void d2d_device_indexed_objects_clear(struct d2d_indexed_objects *objects)
{
    size_t i;

    for (i = 0; i < objects->count; ++i)
        IUnknown_Release(objects->elements[i].object);
    free(objects->elements);
    objects->elements = NULL;
}

static ULONG WINAPI d2d_device_Release(ID2D1Device6 *iface)
{
    struct d2d_device *device = impl_from_ID2D1Device(iface);
    ULONG refcount = InterlockedDecrement(&device->refcount);

    TRACE("%p decreasing refcount to %lu.\n", iface, refcount);

    if (!refcount)
    {
        IDXGIDevice_Release(device->dxgi_device);
        ID2D1Factory1_Release(device->factory);
        d2d_device_indexed_objects_clear(&device->shaders);
        free(device);
    }

    return refcount;
}

static void WINAPI d2d_device_GetFactory(ID2D1Device6 *iface, ID2D1Factory **factory)
{
    struct d2d_device *device = impl_from_ID2D1Device(iface);

    TRACE("iface %p, factory %p.\n", iface, factory);

    *factory = (ID2D1Factory *)device->factory;
    ID2D1Factory1_AddRef(device->factory);
}

static HRESULT d2d_device_create_device_context(struct d2d_device *device,
        D2D1_DEVICE_CONTEXT_OPTIONS options, REFIID iid, void **context)
{
    struct d2d_device_context *object;
    HRESULT hr;

    if (options)
        FIXME("Options are ignored %#x.\n", options);

    if (!(object = calloc(1, sizeof(*object))))
        return E_OUTOFMEMORY;

    if (FAILED(hr = d2d_device_context_init(object, device, NULL, NULL)))
    {
        WARN("Failed to initialise device context, hr %#lx.\n", hr);
        free(object);
        return hr;
    }

    TRACE("Created device context %p.\n", object);

    hr = ID2D1DeviceContext6_QueryInterface(&object->ID2D1DeviceContext6_iface, iid, context);
    ID2D1DeviceContext6_Release(&object->ID2D1DeviceContext6_iface);

    return hr;
}

static HRESULT WINAPI d2d_device_CreateDeviceContext(ID2D1Device6 *iface, D2D1_DEVICE_CONTEXT_OPTIONS options,
        ID2D1DeviceContext **context)
{
    struct d2d_device *device = impl_from_ID2D1Device(iface);

    TRACE("iface %p, options %#x, context %p.\n", iface, options, context);

    return d2d_device_create_device_context(device, options, &IID_ID2D1DeviceContext, (void **)context);
}

static HRESULT WINAPI d2d_device_CreatePrintControl(ID2D1Device6 *iface, IWICImagingFactory *wic_factory,
        IPrintDocumentPackageTarget *document_target, const D2D1_PRINT_CONTROL_PROPERTIES *desc,
        ID2D1PrintControl **print_control)
{
    FIXME("iface %p, wic_factory %p, document_target %p, desc %p, print_control %p stub!\n", iface, wic_factory,
            document_target, desc, print_control);

    return E_NOTIMPL;
}

static void WINAPI d2d_device_SetMaximumTextureMemory(ID2D1Device6 *iface, UINT64 max_texture_memory)
{
    FIXME("iface %p, max_texture_memory %s stub!\n", iface, wine_dbgstr_longlong(max_texture_memory));
}

static UINT64 WINAPI d2d_device_GetMaximumTextureMemory(ID2D1Device6 *iface)
{
    FIXME("iface %p stub!\n", iface);

    return 0;
}

static HRESULT WINAPI d2d_device_ClearResources(ID2D1Device6 *iface, UINT msec_since_use)
{
    FIXME("iface %p, msec_since_use %u stub!\n", iface, msec_since_use);

    return E_NOTIMPL;
}

static D2D1_RENDERING_PRIORITY WINAPI d2d_device_GetRenderingPriority(ID2D1Device6 *iface)
{
    FIXME("iface %p stub!\n", iface);

    return D2D1_RENDERING_PRIORITY_NORMAL;
}

static void WINAPI d2d_device_SetRenderingPriority(ID2D1Device6 *iface, D2D1_RENDERING_PRIORITY priority)
{
    FIXME("iface %p, priority %#x stub!\n", iface, priority);
}

static HRESULT WINAPI d2d_device_CreateDeviceContext1(ID2D1Device6 *iface, D2D1_DEVICE_CONTEXT_OPTIONS options,
        ID2D1DeviceContext1 **context)
{
    struct d2d_device *device = impl_from_ID2D1Device(iface);

    TRACE("iface %p, options %#x, context %p.\n", iface, options, context);

    return d2d_device_create_device_context(device, options, &IID_ID2D1DeviceContext1, (void **)context);
}

static HRESULT STDMETHODCALLTYPE d2d_device_ID2D1Device2_CreateDeviceContext(ID2D1Device6 *iface,
        D2D1_DEVICE_CONTEXT_OPTIONS options, ID2D1DeviceContext2 **context)
{
    struct d2d_device *device = impl_from_ID2D1Device(iface);

    TRACE("iface %p, options %#x, context %p.\n", iface, options, context);

    return d2d_device_create_device_context(device, options, &IID_ID2D1DeviceContext2, (void **)context);
}

static void STDMETHODCALLTYPE d2d_device_FlushDeviceContexts(ID2D1Device6 *iface,
        ID2D1Bitmap *bitmap)
{
    FIXME("iface %p, bitmap %p stub!\n", iface, bitmap);
}

static HRESULT STDMETHODCALLTYPE d2d_device_GetDxgiDevice(ID2D1Device6 *iface,
        IDXGIDevice **dxgi_device)
{
    struct d2d_device *device = impl_from_ID2D1Device(iface);

    TRACE("iface %p, dxgi_device %p.\n", iface, dxgi_device);

    if (!device->allow_get_dxgi_device)
    {
        *dxgi_device = NULL;
        return D2DERR_INVALID_CALL;
    }

    IDXGIDevice_AddRef(device->dxgi_device);
    *dxgi_device = device->dxgi_device;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d2d_device_ID2D1Device3_CreateDeviceContext(ID2D1Device6 *iface,
        D2D1_DEVICE_CONTEXT_OPTIONS options, ID2D1DeviceContext3 **context)
{
    struct d2d_device *device = impl_from_ID2D1Device(iface);

    TRACE("iface %p, options %#x, context %p.\n", iface, options, context);

    return d2d_device_create_device_context(device, options, &IID_ID2D1DeviceContext3, (void **)context);
}

static HRESULT STDMETHODCALLTYPE d2d_device_ID2D1Device4_CreateDeviceContext(ID2D1Device6 *iface,
        D2D1_DEVICE_CONTEXT_OPTIONS options, ID2D1DeviceContext4 **context)
{
    struct d2d_device *device = impl_from_ID2D1Device(iface);

    TRACE("iface %p, options %#x, context %p.\n", iface, options, context);

    return d2d_device_create_device_context(device, options, &IID_ID2D1DeviceContext4, (void **)context);
}

static void STDMETHODCALLTYPE d2d_device_SetMaximumColorGlyphCacheMemory(ID2D1Device6 *iface,
        UINT64 size)
{
    FIXME("iface %p, size %s stub!\n", iface, wine_dbgstr_longlong(size));
}

static UINT64 STDMETHODCALLTYPE d2d_device_GetMaximumColorGlyphCacheMemory(ID2D1Device6 *iface)
{
    FIXME("iface %p stub!\n", iface);

    return 0;
}

static HRESULT STDMETHODCALLTYPE d2d_device_ID2D1Device5_CreateDeviceContext(ID2D1Device6 *iface,
        D2D1_DEVICE_CONTEXT_OPTIONS options, ID2D1DeviceContext5 **context)
{
    struct d2d_device *device = impl_from_ID2D1Device(iface);

    TRACE("iface %p, options %#x, context %p.\n", iface, options, context);

    return d2d_device_create_device_context(device, options, &IID_ID2D1DeviceContext5, (void **)context);
}

static HRESULT STDMETHODCALLTYPE d2d_device_ID2D1Device6_CreateDeviceContext(ID2D1Device6 *iface,
        D2D1_DEVICE_CONTEXT_OPTIONS options, ID2D1DeviceContext6 **context)
{
    struct d2d_device *device = impl_from_ID2D1Device(iface);

    TRACE("iface %p, options %#x, context %p.\n", iface, options, context);

    return d2d_device_create_device_context(device, options, &IID_ID2D1DeviceContext6, (void **)context);
}

static const struct ID2D1Device6Vtbl d2d_device_vtbl =
{
    d2d_device_QueryInterface,
    d2d_device_AddRef,
    d2d_device_Release,
    d2d_device_GetFactory,
    d2d_device_CreateDeviceContext,
    d2d_device_CreatePrintControl,
    d2d_device_SetMaximumTextureMemory,
    d2d_device_GetMaximumTextureMemory,
    d2d_device_ClearResources,
    d2d_device_GetRenderingPriority,
    d2d_device_SetRenderingPriority,
    d2d_device_CreateDeviceContext1,
    d2d_device_ID2D1Device2_CreateDeviceContext,
    d2d_device_FlushDeviceContexts,
    d2d_device_GetDxgiDevice,
    d2d_device_ID2D1Device3_CreateDeviceContext,
    d2d_device_ID2D1Device4_CreateDeviceContext,
    d2d_device_SetMaximumColorGlyphCacheMemory,
    d2d_device_GetMaximumColorGlyphCacheMemory,
    d2d_device_ID2D1Device5_CreateDeviceContext,
    d2d_device_ID2D1Device6_CreateDeviceContext,
};

struct d2d_device *unsafe_impl_from_ID2D1Device(ID2D1Device1 *iface)
{
    if (!iface)
        return NULL;
    assert(iface->lpVtbl == (ID2D1Device1Vtbl *)&d2d_device_vtbl);
    return CONTAINING_RECORD(iface, struct d2d_device, ID2D1Device6_iface);
}

HRESULT d2d_device_init(struct d2d_device *device, ID2D1Factory1 *factory, IDXGIDevice *dxgi_device,
    bool allow_get_dxgi_device)
{
    device->ID2D1Device6_iface.lpVtbl = &d2d_device_vtbl;
    device->ID2D1PrivateCompositorDevice_iface.lpVtbl = (const IUnknownVtbl *)d2d_priv_dev_vtbl;
    device->refcount = 1;
    device->factory = factory;
    ID2D1Factory1_AddRef(device->factory);
    device->dxgi_device = dxgi_device;
    IDXGIDevice_AddRef(device->dxgi_device);
    device->allow_get_dxgi_device = allow_get_dxgi_device;

    return S_OK;
}

HRESULT d2d_device_add_indexed_object(struct d2d_indexed_objects *objects,
        const GUID *id, IUnknown *object)
{
    if (!d2d_array_reserve((void **)&objects->elements, &objects->size, objects->count + 1,
            sizeof(*objects->elements)))
    {
        WARN("Failed to resize elements array.\n");
        return E_OUTOFMEMORY;
    }

    objects->elements[objects->count].id = *id;
    objects->elements[objects->count].object = object;
    IUnknown_AddRef(object);
    objects->count++;

    return S_OK;
}

BOOL d2d_device_get_indexed_object(struct d2d_indexed_objects *objects, const GUID *id,
        IUnknown **object)
{
    size_t i;

    for (i = 0; i < objects->count; ++i)
    {
        if (IsEqualGUID(id, &objects->elements[i].id))
        {
            if (object)
            {
                *object = objects->elements[i].object;
                IUnknown_AddRef(*object);
            }
            return TRUE;
        }
    }

    if (object) *object = NULL;
    return FALSE;
}


/* Forwarder for the factory's implicit device; see the comment in d2d1_private.h.
 * Every device method realizes the real device first (on a thread that is not
 * inside DllMain, i.e. without loader_section held) and delegates to it through
 * its vtable. The forwarder interface pointers are the stable client identity. */

static inline struct d2d_device_forwarder *forwarder_from_ID2D1Device(ID2D1Device *iface)
{
    return CONTAINING_RECORD(iface, struct d2d_device_forwarder, ID2D1Device_iface);
}

static inline struct d2d_device_forwarder *forwarder_from_ID2D1Device1(ID2D1Device1 *iface)
{
    return CONTAINING_RECORD(iface, struct d2d_device_forwarder, ID2D1Device1_iface);
}

static ULONG d2d_device_forwarder_AddRef_impl(struct d2d_device_forwarder *forwarder)
{
    ULONG refcount = InterlockedIncrement(&forwarder->refcount);

    TRACE("%p increasing refcount to %lu.\n", forwarder, refcount);

    return refcount;
}

static ULONG d2d_device_forwarder_Release_impl(struct d2d_device_forwarder *forwarder)
{
    ID2D1Device6 *device = forwarder->device;
    struct d2d_factory *factory = forwarder->factory;
    ULONG refcount = InterlockedDecrement(&forwarder->refcount);

    TRACE("%p decreasing refcount to %lu.\n", forwarder, refcount);

    if (!refcount)
    {
        if (device)
            ID2D1Device6_Release(device);
        DeleteCriticalSection(&forwarder->cs);
        ID2D1Factory7_Release(&factory->ID2D1Factory7_iface);
        free(forwarder);
    }

    return refcount;
}

static HRESULT d2d_device_forwarder_get_device(struct d2d_device_forwarder *forwarder,
        ID2D1Device6 **device)
{
    HRESULT hr = S_OK;

    EnterCriticalSection(&forwarder->cs);

    if (!forwarder->device && FAILED(hr = d2d_factory_create_implicit_device(forwarder->factory,
            &forwarder->device)))
        WARN("Failed to create the implicit device, hr %#lx.\n", hr);

    *device = forwarder->device;

    LeaveCriticalSection(&forwarder->cs);

    return hr;
}

static void d2d_device_forwarder_get_factory(struct d2d_device_forwarder *forwarder,
        ID2D1Factory **factory)
{
    ID2D1Factory1 *f = (ID2D1Factory1 *)&forwarder->factory->ID2D1Factory7_iface;
    HRESULT hr;

    /* The resource factory is the factory object itself; answering this does not
     * need the realized device. */
    hr = ID2D1Factory1_QueryInterface(f, &IID_ID2D1Factory, (void **)factory);
    if (FAILED(hr))
    {
        WARN("Failed to query the factory, hr %#lx.\n", hr);
        *factory = NULL;
    }
}

static HRESULT d2d_device_forwarder_QueryInterface_impl(struct d2d_device_forwarder *forwarder,
        REFIID iid, void **out)
{
    ID2D1Device6 *device;
    HRESULT hr;

    if (IsEqualGUID(iid, &IID_IUnknown)
            || IsEqualGUID(iid, &IID_ID2D1Resource)
            || IsEqualGUID(iid, &IID_ID2D1Device)
            || IsEqualGUID(iid, &IID_ID2D1Device_ms))
    {
        d2d_device_forwarder_AddRef_impl(forwarder);
        *out = &forwarder->ID2D1Device_iface;
        return S_OK;
    }
    if (IsEqualGUID(iid, &IID_ID2D1Device1) || IsEqualGUID(iid, &IID_ID2D1Device1_ms))
    {
        d2d_device_forwarder_AddRef_impl(forwarder);
        *out = &forwarder->ID2D1Device1_iface;
        return S_OK;
    }
    if (IsEqualGUID(iid, &IID_IAgileObject))   /* d2d1 objects are agile */
    {
        d2d_device_forwarder_AddRef_impl(forwarder);
        *out = &forwarder->ID2D1Device_iface;
        return S_OK;
    }

    /* Interfaces the shim does not implement are answered by the realized device
     * with its own pointer identity; realize on demand. */
    if (FAILED(hr = d2d_device_forwarder_get_device(forwarder, &device)))
    {
        *out = NULL;
        return hr;
    }

    return ID2D1Device6_QueryInterface(device, iid, out);
}

static HRESULT STDMETHODCALLTYPE d2d_device_forwarder_QueryInterface(ID2D1Device *iface,
        REFIID iid, void **out)
{
    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    return d2d_device_forwarder_QueryInterface_impl(forwarder_from_ID2D1Device(iface), iid, out);
}

static HRESULT STDMETHODCALLTYPE d2d_device_forwarder_QueryInterface1(ID2D1Device1 *iface,
        REFIID iid, void **out)
{
    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    return d2d_device_forwarder_QueryInterface_impl(forwarder_from_ID2D1Device1(iface), iid, out);
}

static ULONG STDMETHODCALLTYPE d2d_device_forwarder_AddRef(ID2D1Device *iface)
{
    return d2d_device_forwarder_AddRef_impl(forwarder_from_ID2D1Device(iface));
}

static ULONG STDMETHODCALLTYPE d2d_device_forwarder_AddRef1(ID2D1Device1 *iface)
{
    return d2d_device_forwarder_AddRef_impl(forwarder_from_ID2D1Device1(iface));
}

static ULONG STDMETHODCALLTYPE d2d_device_forwarder_Release(ID2D1Device *iface)
{
    return d2d_device_forwarder_Release_impl(forwarder_from_ID2D1Device(iface));
}

static ULONG STDMETHODCALLTYPE d2d_device_forwarder_Release1(ID2D1Device1 *iface)
{
    return d2d_device_forwarder_Release_impl(forwarder_from_ID2D1Device1(iface));
}

static void STDMETHODCALLTYPE d2d_device_forwarder_GetFactory1(ID2D1Device1 *iface,
        ID2D1Factory **factory)
{
    TRACE("iface %p, factory %p.\n", iface, factory);

    d2d_device_forwarder_get_factory(forwarder_from_ID2D1Device1(iface), factory);
}

static HRESULT STDMETHODCALLTYPE d2d_device_forwarder_CreateDeviceContext_1(ID2D1Device1 *iface,
        D2D1_DEVICE_CONTEXT_OPTIONS options, ID2D1DeviceContext **context)
{
    struct d2d_device_forwarder *forwarder = forwarder_from_ID2D1Device1(iface);
    ID2D1Device6 *device;
    HRESULT hr;

    TRACE("iface %p, options %#x, context %p.\n", iface, options, context);

    if (FAILED(hr = d2d_device_forwarder_get_device(forwarder, &device)))
        return hr;

    return device->lpVtbl->CreateDeviceContext(device, options, context);
}

static HRESULT STDMETHODCALLTYPE d2d_device_forwarder_CreateDeviceContext1(ID2D1Device1 *iface,
        D2D1_DEVICE_CONTEXT_OPTIONS options, ID2D1DeviceContext1 **context)
{
    struct d2d_device_forwarder *forwarder = forwarder_from_ID2D1Device1(iface);
    ID2D1Device6 *device;
    HRESULT hr;

    TRACE("iface %p, options %#x, context %p.\n", iface, options, context);

    if (FAILED(hr = d2d_device_forwarder_get_device(forwarder, &device)))
        return hr;

    return device->lpVtbl->ID2D1Device1_CreateDeviceContext(device, options, context);
}

static HRESULT STDMETHODCALLTYPE d2d_device_forwarder_CreatePrintControl1(ID2D1Device1 *iface,
        IWICImagingFactory *wic_factory, IPrintDocumentPackageTarget *target,
        const D2D1_PRINT_CONTROL_PROPERTIES *desc, ID2D1PrintControl **print_control)
{
    struct d2d_device_forwarder *forwarder = forwarder_from_ID2D1Device1(iface);
    ID2D1Device6 *device;
    HRESULT hr;

    TRACE("iface %p, wic_factory %p, target %p, desc %p, print_control %p.\n",
            iface, wic_factory, target, desc, print_control);

    if (FAILED(hr = d2d_device_forwarder_get_device(forwarder, &device)))
        return hr;

    return device->lpVtbl->CreatePrintControl(device, wic_factory, target, desc, print_control);
}

#define FORWARDER_TEXTURE_MEMORY(name, impl_from, iface_type) \
static void STDMETHODCALLTYPE name##_set_max_texture_memory(iface_type *iface, UINT64 size) \
{ \
    struct d2d_device_forwarder *forwarder = impl_from(iface); \
    ID2D1Device6 *device; \
    \
    TRACE("iface %p, size %s.\n", iface, wine_dbgstr_longlong(size)); \
    \
    if (FAILED(d2d_device_forwarder_get_device(forwarder, &device))) \
        return; \
    \
    device->lpVtbl->SetMaximumTextureMemory(device, size); \
} \
\
static UINT64 STDMETHODCALLTYPE name##_get_max_texture_memory(iface_type *iface) \
{ \
    struct d2d_device_forwarder *forwarder = impl_from(iface); \
    ID2D1Device6 *device; \
    \
    TRACE("iface %p.\n", iface); \
    \
    if (FAILED(d2d_device_forwarder_get_device(forwarder, &device))) \
        return 0; \
    \
    return device->lpVtbl->GetMaximumTextureMemory(device); \
}

FORWARDER_TEXTURE_MEMORY(d2d_device_forwarder1, forwarder_from_ID2D1Device1, ID2D1Device1)

static HRESULT STDMETHODCALLTYPE d2d_device_forwarder_ClearResources1(ID2D1Device1 *iface,
        UINT msec_since_use)
{
    struct d2d_device_forwarder *forwarder = forwarder_from_ID2D1Device1(iface);
    ID2D1Device6 *device;
    HRESULT hr;

    TRACE("iface %p, msec_since_use %u.\n", iface, msec_since_use);

    if (FAILED(hr = d2d_device_forwarder_get_device(forwarder, &device)))
        return hr;

    return device->lpVtbl->ClearResources(device, msec_since_use);
}

static D2D1_RENDERING_PRIORITY STDMETHODCALLTYPE d2d_device_forwarder_GetRenderingPriority(
        ID2D1Device1 *iface)
{
    struct d2d_device_forwarder *forwarder = forwarder_from_ID2D1Device1(iface);
    ID2D1Device6 *device;

    TRACE("iface %p.\n", iface);

    if (FAILED(d2d_device_forwarder_get_device(forwarder, &device)))
        return D2D1_RENDERING_PRIORITY_NORMAL;

    return device->lpVtbl->GetRenderingPriority(device);
}

static void STDMETHODCALLTYPE d2d_device_forwarder_SetRenderingPriority(ID2D1Device1 *iface,
        D2D1_RENDERING_PRIORITY priority)
{
    struct d2d_device_forwarder *forwarder = forwarder_from_ID2D1Device1(iface);
    ID2D1Device6 *device;

    TRACE("iface %p, priority %u.\n", iface, priority);

    if (FAILED(d2d_device_forwarder_get_device(forwarder, &device)))
        return;

    device->lpVtbl->SetRenderingPriority(device, priority);
}

static HRESULT STDMETHODCALLTYPE d2d_priv_factory_CreateRegionGeometry(ID2D1Device *iface,
        const D2D1_RECT_L *rects, UINT32 rect_count, ID2D1Geometry **geometry)
{
    struct d2d_device_forwarder *forwarder = forwarder_from_ID2D1Device(iface);
    ID2D1PathGeometry1 *path = NULL;
    ID2D1GeometrySink *sink = NULL;
    UINT32 i;
    HRESULT hr;

    TRACE("iface %p, rects %p, rect_count %u, geometry %p.\n", iface, rects, rect_count, geometry);

    if (!geometry)
        return E_INVALIDARG;
    *geometry = NULL;

    if (FAILED(hr = ID2D1Factory7_CreatePathGeometry(&forwarder->factory->ID2D1Factory7_iface, &path)))
        return hr;

    if (SUCCEEDED(hr = ID2D1PathGeometry1_Open(path, &sink)))
    {
        for (i = 0; i < rect_count && rects; ++i)
        {
            D2D1_POINT_2F p0 = { (float)rects[i].left, (float)rects[i].top };
            D2D1_POINT_2F p1 = { (float)rects[i].right, (float)rects[i].top };
            D2D1_POINT_2F p2 = { (float)rects[i].right, (float)rects[i].bottom };
            D2D1_POINT_2F p3 = { (float)rects[i].left, (float)rects[i].bottom };
            ID2D1GeometrySink_BeginFigure(sink, p0, D2D1_FIGURE_BEGIN_FILLED);
            ID2D1GeometrySink_AddLine(sink, p1);
            ID2D1GeometrySink_AddLine(sink, p2);
            ID2D1GeometrySink_AddLine(sink, p3);
            ID2D1GeometrySink_EndFigure(sink, D2D1_FIGURE_END_CLOSED);
        }
        hr = ID2D1GeometrySink_Close(sink);
        ID2D1GeometrySink_Release(sink);
    }

    if (SUCCEEDED(hr))
        *geometry = (ID2D1Geometry *)path;
    else
        ID2D1PathGeometry1_Release(path);
    return hr;
}

static HRESULT STDMETHODCALLTYPE d2d_priv_factory_CombineGeometries(ID2D1Device *iface,
        D2D1_COMBINE_MODE combine_mode, ID2D1Geometry *geo1, const D2D1_MATRIX_3X2_F *transform1,
        ID2D1Geometry *geo2, const D2D1_MATRIX_3X2_F *transform2, float tolerance, ID2D1Geometry **out)
{
    struct d2d_device_forwarder *forwarder = forwarder_from_ID2D1Device(iface);
    ID2D1PathGeometry1 *path = NULL;
    ID2D1GeometrySink *sink = NULL;
    HRESULT hr;

    TRACE("iface %p, mode %u, geo1 %p, t1 %p, geo2 %p, t2 %p, tol %.4f, out %p.\n",
            iface, combine_mode, geo1, transform1, geo2, transform2, tolerance, out);

    if (!out)
        return E_INVALIDARG;
    *out = NULL;

    if (FAILED(hr = ID2D1Factory7_CreatePathGeometry(&forwarder->factory->ID2D1Factory7_iface, &path)))
        return hr;

    if (SUCCEEDED(hr = ID2D1PathGeometry1_Open(path, &sink)))
    {
        float tol = tolerance > 0.0f ? tolerance : 0.25f;

        if (geo1 && geo2)
        {
            ID2D1TransformedGeometry *tgeo1 = NULL;
            ID2D1Geometry *src1 = geo1;

            if (transform1)
            {
                hr = ID2D1Factory7_CreateTransformedGeometry(&forwarder->factory->ID2D1Factory7_iface,
                        geo1, transform1, &tgeo1);
                if (SUCCEEDED(hr))
                    src1 = (ID2D1Geometry *)tgeo1;
            }
            if (SUCCEEDED(hr))
                hr = ID2D1Geometry_CombineWithGeometry(src1, geo2, combine_mode, transform2,
                        tol, (ID2D1SimplifiedGeometrySink *)sink);
            if (tgeo1)
                ID2D1TransformedGeometry_Release(tgeo1);
        }
        else if (geo1)
        {
            hr = ID2D1Geometry_Simplify(geo1, D2D1_GEOMETRY_SIMPLIFICATION_OPTION_LINES,
                    transform1, tol, (ID2D1SimplifiedGeometrySink *)sink);
        }
        else if (geo2)
        {
            hr = ID2D1Geometry_Simplify(geo2, D2D1_GEOMETRY_SIMPLIFICATION_OPTION_LINES,
                    transform2, tol, (ID2D1SimplifiedGeometrySink *)sink);
        }
        if (SUCCEEDED(hr))
            hr = ID2D1GeometrySink_Close(sink);
        ID2D1GeometrySink_Release(sink);
    }

    if (SUCCEEDED(hr))
        *out = (ID2D1Geometry *)path;
    else
        ID2D1PathGeometry1_Release(path);
    return hr;
}

static BYTE clamp_u8(float v)
{
    if (v <= 0.0f) return 0;
    if (v >= 1.0f) return 255;
    return (BYTE)(v * 255.0f + 0.5f);
}

static HRESULT STDMETHODCALLTYPE d2d_priv_factory_GenerateGradientTexture(ID2D1Device *iface,
        const D2D1_GRADIENT_STOP *stops, UINT32 stop_count, UINT32 is_radial, UINT32 unk4,
        UINT32 color_interp_mode, UINT32 unk6, D2D1_EXTEND_MODE extend_mode, UINT32 unk8,
        DXGI_FORMAT format, void *out_buffer, UINT32 out_buffer_size)
{
    BYTE *dst = out_buffer;
    UINT32 count, i, s;

    TRACE("iface %p, stops %p, stop_count %u, format %u, out %p, size %u.\n",
            iface, stops, stop_count, format, out_buffer, out_buffer_size);

    if (!out_buffer || !out_buffer_size)
        return E_INVALIDARG;

    if (!stops || !stop_count)
    {
        memset(out_buffer, 0, out_buffer_size);
        return S_OK;
    }

    count = out_buffer_size / 4;
    for (i = 0; i < count; ++i)
    {
        float t = count > 1 ? (float)i / (float)(count - 1) : 0.0f;
        float r, g, b, a;

        if (t <= stops[0].position || stop_count == 1)
        {
            r = stops[0].color.r;
            g = stops[0].color.g;
            b = stops[0].color.b;
            a = stops[0].color.a;
        }
        else if (t >= stops[stop_count - 1].position)
        {
            r = stops[stop_count - 1].color.r;
            g = stops[stop_count - 1].color.g;
            b = stops[stop_count - 1].color.b;
            a = stops[stop_count - 1].color.a;
        }
        else
        {
            r = stops[0].color.r;
            g = stops[0].color.g;
            b = stops[0].color.b;
            a = stops[0].color.a;
            for (s = 0; s + 1 < stop_count; ++s)
            {
                if (t >= stops[s].position && t <= stops[s + 1].position)
                {
                    float span = stops[s + 1].position - stops[s].position;
                    float f = span > 1e-6f ? (t - stops[s].position) / span : 0.0f;
                    r = stops[s].color.r + (stops[s + 1].color.r - stops[s].color.r) * f;
                    g = stops[s].color.g + (stops[s + 1].color.g - stops[s].color.g) * f;
                    b = stops[s].color.b + (stops[s + 1].color.b - stops[s].color.b) * f;
                    a = stops[s].color.a + (stops[s + 1].color.a - stops[s].color.a) * f;
                    break;
                }
            }
        }

        /* Premultiply for BGRA8 (DXGI_FORMAT_B8G8R8A8_UNORM = 87) */
        dst[i * 4 + 0] = clamp_u8(b * a);
        dst[i * 4 + 1] = clamp_u8(g * a);
        dst[i * 4 + 2] = clamp_u8(r * a);
        dst[i * 4 + 3] = clamp_u8(a);
    }

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d2d_device_forwarder_priv_stub(ID2D1Device *iface)
{
    TRACE("iface %p stub.\n", iface);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d2d_device_forwarder_priv_CreateDevice(ID2D1Device *iface,
        DWORD flags, IDXGIDevice *dxgi_device, ID2D1Device **device)
{
    struct d2d_device_forwarder *forwarder = forwarder_from_ID2D1Device(iface);
    IDXGIDevice *dxgi = NULL;
    HRESULT hr;

    TRACE("iface %p, flags %#lx, dxgi_device %p, device %p.\n", iface, flags, dxgi_device, device);

    if (!device)
        return E_INVALIDARG;
    *device = NULL;
    if (!dxgi_device)
        return E_INVALIDARG;

    if (FAILED(hr = IUnknown_QueryInterface((IUnknown *)dxgi_device, &IID_IDXGIDevice, (void **)&dxgi)))
        return hr;

    hr = ID2D1Factory7_CreateDevice(&forwarder->factory->ID2D1Factory7_iface, dxgi, (ID2D1Device6 **)device);
    IDXGIDevice_Release(dxgi);
    return hr;
}

/* ID2D1PrivateFactory ({6f72c0a2-6db7-46e9-9b62-b58a23f4928b}, queried on
 * ID2D1Factory by dwmcorei.dll). */
static const void * const d2d_device_forwarder_ext_vtbl[] =
{
    d2d_device_forwarder_QueryInterface,
    d2d_device_forwarder_AddRef,
    d2d_device_forwarder_Release,
    d2d_priv_factory_CreateRegionGeometry,   /* 3 (+0x18) */
    d2d_priv_factory_CombineGeometries,      /* 4 (+0x20) */
    d2d_priv_factory_GenerateGradientTexture,/* 5 (+0x28) */
    d2d_device_forwarder_priv_stub,          /* 6 (+0x30) */
    d2d_device_forwarder_priv_stub,          /* 7 */
    d2d_device_forwarder_priv_stub,          /* 8 */
    d2d_device_forwarder_priv_stub,          /* 9 */
    d2d_device_forwarder_priv_stub,          /* 10 */
    d2d_device_forwarder_priv_stub,          /* 11 */
    d2d_device_forwarder_priv_stub,          /* 12 */
    d2d_device_forwarder_priv_stub,          /* 13 */
    d2d_device_forwarder_priv_stub,          /* 14 */
    d2d_device_forwarder_priv_CreateDevice,  /* 15 (+0x78) */
    d2d_device_forwarder_priv_stub,          /* 16 */
    d2d_device_forwarder_priv_stub,          /* 17 */
    d2d_device_forwarder_priv_stub,          /* 18 */
    d2d_device_forwarder_priv_stub,          /* 19 */
};

static const ID2D1Device1Vtbl d2d_device_forwarder_vtbl1 =
{
    d2d_device_forwarder_QueryInterface1,
    d2d_device_forwarder_AddRef1,
    d2d_device_forwarder_Release1,
    d2d_device_forwarder_GetFactory1,
    d2d_device_forwarder_CreateDeviceContext_1,
    d2d_device_forwarder_CreatePrintControl1,
    d2d_device_forwarder1_set_max_texture_memory,
    d2d_device_forwarder1_get_max_texture_memory,
    d2d_device_forwarder_ClearResources1,
    d2d_device_forwarder_GetRenderingPriority,
    d2d_device_forwarder_SetRenderingPriority,
    d2d_device_forwarder_CreateDeviceContext1,
};

void d2d_device_forwarder_init(struct d2d_device_forwarder *forwarder, struct d2d_factory *factory)
{
    forwarder->ID2D1Device_iface.lpVtbl = (const ID2D1DeviceVtbl *)d2d_device_forwarder_ext_vtbl;
    forwarder->ID2D1Device1_iface.lpVtbl = &d2d_device_forwarder_vtbl1;
    forwarder->refcount = 1;
    forwarder->factory = factory;
    forwarder->device = NULL;
    InitializeCriticalSection(&forwarder->cs);
    ID2D1Factory7_AddRef(&factory->ID2D1Factory7_iface);
}
