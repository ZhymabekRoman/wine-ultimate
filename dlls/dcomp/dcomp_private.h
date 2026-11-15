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
#ifndef __WINE_DCOMP_PRIVATE_H
#define __WINE_DCOMP_PRIVATE_H

#include "dcomp.h"
#include "dcomp_private_iface.h"
#include "d2d1_3.h"
#include <d3d11.h>
#include "dxgi1_2.h"
#include "wine/list.h"

struct wined3d_swapchain;
struct composite_frame;
struct target_snapshot;

struct composition_device
{
    IDCompositionDevice IDCompositionDevice_iface;
    IDCompositionDeviceUnknown IDCompositionDeviceUnknown_iface;
    IDCompositionSurfaceUnknown *drawing_surface;
    struct list targets;
    HANDLE thread;
    LONG exit_thread;
    LONG thread_exited;
    BOOL self_release;
    int version;
    LONG ref;
    IUnknown *rendering_device;
    IDCompositionSurfaceFactory *surface_factory;
    struct list dev_entry;
    struct composite_frame *frame_buf;
    unsigned int frame_buf_count;
    unsigned int frame_buf_cap;
    struct target_snapshot *snap_buf;
    unsigned int snap_buf_count;
    unsigned int snap_buf_cap;
    HANDLE *handle_buf;
    unsigned int handle_buf_count;
    unsigned int handle_buf_cap;
    LONG last_tree_version;
    LONG last_xform_version;
    BOOL last_has_shared;
    BOOL last_has_swapchain;
    BOOL committed;
    ULONGLONG last_composite_qpc;
    LARGE_INTEGER qpc_freq;
    unsigned int refresh_period_ms;
    LONG commit_pending;
    LONG composite_count;
    HANDLE wake_event;
};

enum dcomp_object_kind
{
    TRANSFORM_TRANSLATE,
    TRANSFORM_SCALE,
    TRANSFORM_ROTATE,
    TRANSFORM_SKEW,
    TRANSFORM_MATRIX,
    TRANSFORM_GROUP,
    TRANSFORM_TRANSLATE_3D,
    TRANSFORM_SCALE_3D,
    TRANSFORM_ROTATE_3D,
    TRANSFORM_MATRIX_3D,
    TRANSFORM_3D_GROUP,
    OBJECT_EFFECT_GROUP,
    OBJECT_RECTANGLE_CLIP,
};

/* One object implements every DirectComposition transform / effect-group /
 * rectangle-clip interface; the creation kind gates QueryInterface. */
struct dcomp_transform
{
    struct IDCompositionTranslateTransform IDCompositionTranslateTransform_iface;
    struct IDCompositionScaleTransform IDCompositionScaleTransform_iface;
    struct IDCompositionRotateTransform IDCompositionRotateTransform_iface;
    struct IDCompositionSkewTransform IDCompositionSkewTransform_iface;
    struct IDCompositionMatrixTransform IDCompositionMatrixTransform_iface;
    struct IDCompositionTranslateTransform3D IDCompositionTranslateTransform3D_iface;
    struct IDCompositionScaleTransform3D IDCompositionScaleTransform3D_iface;
    struct IDCompositionRotateTransform3D IDCompositionRotateTransform3D_iface;
    struct IDCompositionMatrixTransform3D IDCompositionMatrixTransform3D_iface;
    struct IDCompositionEffectGroup IDCompositionEffectGroup_iface;
    struct IDCompositionRectangleClip IDCompositionRectangleClip_iface;
    LONG ref;
    LONG version;
    struct composition_device *device;
    enum dcomp_object_kind kind;
    FLOAT offset_x, offset_y, offset_z;
    FLOAT scale_x, scale_y, scale_z;
    FLOAT center_x, center_y, center_z;
    FLOAT angle, angle_x, angle_y;
    FLOAT axis_x, axis_y, axis_z;
    FLOAT opacity;
    D2D_MATRIX_3X2_F matrix;
    D3DMATRIX matrix3d;
    FLOAT clip_left, clip_top, clip_right, clip_bottom;
    FLOAT clip_tl_radius_x, clip_tl_radius_y;
    FLOAT clip_tr_radius_x, clip_tr_radius_y;
    FLOAT clip_bl_radius_x, clip_bl_radius_y;
    FLOAT clip_br_radius_x, clip_br_radius_y;
    IDCompositionTransform3D *transform3d;
    IUnknown **group_items;
    UINT group_count;
};

struct dcomp_transform *dcomp_transform_create( struct composition_device *device,
        enum dcomp_object_kind kind );
struct dcomp_transform *dcomp_transform_group_create( struct composition_device *device,
        enum dcomp_object_kind kind, IUnknown *const *items, UINT count );
struct dcomp_transform *unsafe_impl_from_dcomp_transform( IUnknown *iface );
void dcomp_matrix_identity( D3DMATRIX *m );
void dcomp_matrix_multiply( D3DMATRIX *out, const D3DMATRIX *a, const D3DMATRIX *b );
void dcomp_matrix_from_2d( D3DMATRIX *out, const D2D_MATRIX_3X2_F *m2d );
BOOL dcomp_matrix_is_identity( const D3DMATRIX *m );
void dcomp_transform_get_matrix_3d( const struct dcomp_transform *impl, D3DMATRIX *out );
unsigned int dcomp_transform_get_version( const struct dcomp_transform *impl );

struct composition_surface_factory
{
    IDCompositionSurfaceFactory IDCompositionSurfaceFactory_iface;
    IDCompositionDevice *device;
    IUnknown *rendering_device;
    GUID rendering_device_iid;
    LONG ref;
};

struct composition_surface
{
    IDCompositionSurfaceUnknown IDCompositionSurfaceUnknown_iface;
    IDCompositionSurfaceFactory *factory;
    ID3D11Texture2D *draw_surface;
    RECT draw_rect;
    IUnknown *physical_surface;
    GUID physical_surface_iid;
    UINT width;
    UINT height;
    DXGI_FORMAT pixel_format;
    DXGI_ALPHA_MODE alpha_mode;
    BOOL is_virtual;          /* answers IDCompositionVirtualSurface, BeginDraw takes sub-rects */
    RECT *trim_rects;         /* copy of the last Trim() rectangles, see surface_Trim() */
    UINT trim_count;
    unsigned int content_version;
    LONG ref;
};

struct composition_target
{
    IDCompositionTarget IDCompositionTarget_iface;
    IDCompositionDevice *device;
    IDCompositionVisual *root;
    BOOL topmost;
    HWND hwnd;
    HANDLE shared_visual_handle;
    struct list entry;
    struct composition_surface *composited_surface; /* last fallback surface blitted to the window */
    /* Zero-copy GPU presentation: a swapchain created on the content's D3D11
     * device whose back buffer owns the target window's drawable. Content is
     * sampled into it on the GPU (opacity and clip applied by the pixel
     * shader) and wined3d_swapchain_present() copies the drawable region for
     * the visual's accumulated offset to the window. Recreated when the
     * producing device or the content size changes. */
    ID3D11Device *composite_device;         /* device composite_swapchain was created on */
    IDXGISwapChain1 *composite_swapchain;   /* presents to the target window */
    ID3D11RenderTargetView *composite_rtv;  /* render target view of the back buffer */
    UINT composite_width;
    UINT composite_height;
    LONG ref;
};

struct composition_visual
{
    IDCompositionVisualUnknown IDCompositionVisualUnknown_iface;
    ID2D1GdiInteropRenderTarget *interop;
    ID2D1DeviceContext *device_context;
    IUnknown *content;
    GUID content_iid;
    struct wined3d_swapchain *wined3d_swapchain;
    enum DCOMPOSITION_BITMAP_INTERPOLATION_MODE interpolation_mode;
    enum DCOMPOSITION_BORDER_MODE border_mode;
    enum DCOMPOSITION_BACKFACE_VISIBILITY visibility;
    float offset_x;
    float offset_y;
    SIZE size;
    IDCompositionTransform *transform;
    D2D_MATRIX_3X2_F transform_matrix;
    BOOL has_transform_matrix;
    IDCompositionEffect *effect;
    IDCompositionClip *clip;        /* geometry clip set via SetClipObject; stored, not composited (conformance: accept + refcount) */
    D2D_RECT_F rect_clip;           /* rectangle clip set via SetClip */
    BOOL has_rect_clip;             /* TRUE when rect_clip is set (SetClip with non-NULL rect) */
    struct list child_visuals; /* visuals closer to head are lower in z-order */
    struct list entry;
    struct composition_visual *parent;
    BOOL is_root;
    BOOL is_child;
    BOOL is_shared_proxy;           /* stand-in for a shared visual published by another process */
    HANDLE shared_visual_handle;
    HANDLE shared_content_handle;   /* shared resource handle of the content: published by the
                                     * publisher of this tree, opened by the consumer of a proxy */
    unsigned int shared_dirty_count;/* server-side dirty/generation counter of the shared visual */
    int version;
    unsigned int content_version;   /* bumped whenever the visual content is (re)set: change counter */
    unsigned int transform_version; /* bumped on offset/transform/effect/clip changes (no tree rebuild) */
    unsigned int composited_version;/* content_version as of the last blit to the window */
    unsigned int composited_transform_version; /* transform/effect version as of last blit */
    HWND composited_hwnd;           /* window last composited into */
    LONG ref;
};

static inline struct composition_device *impl_from_IDCompositionDevice(IDCompositionDevice *iface)
{
    return CONTAINING_RECORD(iface, struct composition_device, IDCompositionDevice_iface);
}

static inline struct composition_device *impl_from_IDCompositionDeviceUnknown(IDCompositionDeviceUnknown *iface)
{
    return CONTAINING_RECORD(iface, struct composition_device, IDCompositionDeviceUnknown_iface);
}

static inline struct composition_surface *impl_from_IDCompositionSurfaceUnknown(IDCompositionSurfaceUnknown *iface)
{
    return CONTAINING_RECORD(iface, struct composition_surface, IDCompositionSurfaceUnknown_iface);
}

static inline struct composition_surface *impl_from_IDCompositionSurface(IDCompositionSurface *iface)
{
    return CONTAINING_RECORD(iface, struct composition_surface, IDCompositionSurfaceUnknown_iface);
}

static inline struct composition_surface_factory *impl_from_IDCompositionSurfaceFactory(IDCompositionSurfaceFactory *iface)
{
    return CONTAINING_RECORD(iface, struct composition_surface_factory, IDCompositionSurfaceFactory_iface);
}

struct composition_surface *unsafe_impl_from_IDCompositionSurface(IDCompositionSurface *iface);

static inline struct composition_target *impl_from_IDCompositionTarget(IDCompositionTarget *iface)
{
    return CONTAINING_RECORD(iface, struct composition_target, IDCompositionTarget_iface);
}

static inline struct composition_visual *impl_from_IDCompositionVisual(IDCompositionVisual *iface)
{
    return CONTAINING_RECORD(iface, struct composition_visual, IDCompositionVisualUnknown_iface);
}

static inline struct composition_visual *impl_from_IDCompositionVisualUnknown(IDCompositionVisualUnknown *iface)
{
    return CONTAINING_RECORD(iface, struct composition_visual, IDCompositionVisualUnknown_iface);
}

static inline BOOL dcomp_is_equal_guid(const GUID *a, const GUID *b)
{
    const ULONG64 *a64 = (const ULONG64 *)a;
    const ULONG64 *b64 = (const ULONG64 *)b;
    return a == b || ((a64[0] == b64[0]) && (a64[1] == b64[1]));
}

void dcomp_lock(void);
void dcomp_unlock(void);
void dcomp_reclaim_push(IUnknown *object);
void shared_visual_cache_set(HANDLE handle, IDCompositionVisual *root);
HRESULT create_surface(struct composition_surface_factory *factory, UINT width, UINT height,
        DXGI_FORMAT pixel_format, DXGI_ALPHA_MODE alpha_mode, IDCompositionSurface **dcomp_surface);
HRESULT create_virtual_surface(struct composition_surface_factory *factory, UINT width, UINT height,
        DXGI_FORMAT pixel_format, DXGI_ALPHA_MODE alpha_mode, IDCompositionVirtualSurface **surface);
HRESULT create_surface_factory(struct composition_device *device, IUnknown *rendering_device, IDCompositionSurfaceFactory **factory);
HRESULT create_target(struct composition_device *device, HWND hwnd, BOOL topmost, IDCompositionTarget **target);
HRESULT create_target_from_shared_visual_handle(struct composition_device *device, HANDLE shared_visual_handle, void **new_target);
HRESULT create_visual(int version, REFIID iid, void **visual);
HRESULT create_visual_from_shared_visual_handle(HANDLE shared_visual_handle, void **new_visual);
void visual_publish_shared_content(struct composition_visual *visual, HANDLE *handle, UINT *width, UINT *height);
void shared_visual_target_set_root(HANDLE shared_visual_handle, IDCompositionVisual *visual);
IDXGISurface *shared_proxy_open_surface(struct composition_visual *visual);
HRESULT create_surface_from_dxgi_surface(struct composition_device *device, IDXGISurface *dxgi_surface, IUnknown **out_surface);
void ensure_composite_thread(struct composition_device *device);
BOOL dcomp_has_active_composite_thread(void);
void visual_tree_mark_dirty(struct composition_visual *visual);
void visual_get_local_transform_3d(const struct composition_visual *visual, D3DMATRIX *out);
void visual_get_transform_3d(const struct composition_visual *visual, UINT width, UINT height,
        const D3DMATRIX *world_transform, float vp_x, float vp_y, float vp_w, float vp_h, D3DMATRIX *out);
unsigned int visual_get_transform_version(const struct composition_visual *visual);
struct wined3d_swapchain *dxgi_get_wined3d_swapchain(struct IDXGISwapChain *swapchain);
extern struct list dcomp_devices;
extern volatile LONG dcomp_tree_version;
extern volatile LONG dcomp_transform_version;


#endif /* __WINE_DCOMP_PRIVATE_H */
