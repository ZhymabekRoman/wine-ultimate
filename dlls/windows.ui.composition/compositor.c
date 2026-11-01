/* WinRT Windows.UI.Composition Compositor implementation
 *
 * Copyright 2026 wine-ultimate contributors
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

#include <stdbool.h>

#include "private.h"

WINE_DEFAULT_DEBUG_CHANNEL(composition);

static HRESULT WINAPI compositor_CreateColorBrush( struct ICompositor *iface, struct ICompositionColorBrush **result );
static HRESULT WINAPI compositor_CreateColorBrushWithColor( struct ICompositor *iface, Color color,
        struct ICompositionColorBrush **result );
static HRESULT WINAPI compositor_CreateExpressionAnimationWithExpression( struct ICompositor *iface,
        HSTRING expression, struct IExpressionAnimation **result );
static HRESULT WINAPI compositor_CreateInsetClipWithInsets( struct ICompositor *iface, FLOAT left,
        FLOAT top, FLOAT right, FLOAT bottom, struct IInsetClip **result );
static HRESULT WINAPI compositor_CreateSurfaceBrushWithSurface( struct ICompositor *iface,
        struct ICompositionSurface *surface, struct ICompositionSurfaceBrush **result );

HRESULT compositor_ensure_device( struct compositor *impl )
{
    HRESULT hr;

    if (impl->desktop_device || impl->device2 || impl->device) return S_OK;

    if (SUCCEEDED( hr = DCompositionCreateDevice2( NULL, &IID_IDCompositionDevice2,
            (void **)&impl->device2 ) ))
    {
        if (FAILED( IDCompositionDevice2_QueryInterface( impl->device2,
                &IID_IDCompositionDesktopDevice, (void **)&impl->desktop_device ) ))
        {
            impl->desktop_device = NULL;
        }
        if (FAILED( IDCompositionDevice2_QueryInterface( impl->device2,
                &IID_IDCompositionDevice, (void **)&impl->device ) ))
        {
            impl->device = NULL;
        }
        return S_OK;
    }

    if (SUCCEEDED( hr = DCompositionCreateDevice( NULL, &IID_IDCompositionDevice,
            (void **)&impl->device ) ))
    {
        IDCompositionDevice_QueryInterface( impl->device, &IID_IDCompositionDesktopDevice,
                (void **)&impl->desktop_device );
        IDCompositionDevice_QueryInterface( impl->device, &IID_IDCompositionDevice2,
                (void **)&impl->device2 );
        return S_OK;
    }

    return hr;
}

static inline struct compositor *impl_from_ICompositor( struct ICompositor *iface )
{
    return CONTAINING_RECORD( iface, struct compositor, ICompositor_iface );
}

static HRESULT WINAPI compositor_QueryInterface( struct ICompositor *iface, REFIID iid, void **out )
{
    struct compositor *impl = impl_from_ICompositor( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown )
        || IsEqualGUID( iid, &IID_IInspectable )
        || IsEqualGUID( iid, &IID_IAgileObject )
        || IsEqualGUID( iid, &IID_ICompositor ))
    {
        *out = &impl->ICompositor_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IClosable ))
    {
        *out = &impl->IClosable_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_ICompositorInterop ))
    {
        *out = &impl->ICompositorInterop_iface;
        IUnknown_AddRef( (IUnknown *)*out );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_ICompositorDesktopInterop ))
    {
        *out = &impl->ICompositorDesktopInterop_iface;
        IUnknown_AddRef( (IUnknown *)*out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI compositor_AddRef( struct ICompositor *iface )
{
    struct compositor *impl = impl_from_ICompositor( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI compositor_Release( struct ICompositor *iface )
{
    struct compositor *impl = impl_from_ICompositor( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        if (impl->desktop_device) IDCompositionDesktopDevice_Release( impl->desktop_device );
        if (impl->device2) IDCompositionDevice2_Release( impl->device2 );
        if (impl->device) IDCompositionDevice_Release( impl->device );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI compositor_GetIids( struct ICompositor *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI compositor_GetRuntimeClassName( struct ICompositor *iface, HSTRING *class_name )
{
    return WindowsCreateString( RuntimeClass_Windows_UI_Composition_Compositor,
            ARRAYSIZE( RuntimeClass_Windows_UI_Composition_Compositor ) - 1, class_name );
}

static HRESULT WINAPI compositor_GetTrustLevel( struct ICompositor *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

/*
 * ICompositor factory methods.
 */

static HRESULT WINAPI compositor_CreateColorKeyFrameAnimation( struct ICompositor *iface,
        struct IColorKeyFrameAnimation **result )
{
    struct compositor *impl = impl_from_ICompositor( iface );
    struct keyframe_animation *animation;

    TRACE( "iface %p, result %p.\n", iface, result );

    if (!result) return E_POINTER;
    if (!(animation = keyframe_animation_create( impl, ANIM_COLOR_KEY ))) return E_OUTOFMEMORY;
    *result = (struct IColorKeyFrameAnimation *)&animation->IColorKeyFrameAnimation_iface;
    return S_OK;
}

static HRESULT WINAPI compositor_CreateColorBrush( struct ICompositor *iface,
        struct ICompositionColorBrush **result )
{
    Color color = {0};
    return compositor_CreateColorBrushWithColor( iface, color, result );
}

static HRESULT WINAPI compositor_CreateColorBrushWithColor( struct ICompositor *iface, Color color,
        struct ICompositionColorBrush **result )
{
    struct compositor *impl = impl_from_ICompositor( iface );
    struct color_brush *brush;

    TRACE( "iface %p, result %p.\n", iface, result );

    if (!result) return E_POINTER;
    if (!(brush = color_brush_create( impl, color ))) return E_OUTOFMEMORY;
    *result = (struct ICompositionColorBrush *)&brush->ICompositionColorBrush_iface;
    return S_OK;
}

static HRESULT WINAPI compositor_CreateContainerVisual( struct ICompositor *iface,
        struct IContainerVisual **result )
{
    struct compositor *impl = impl_from_ICompositor( iface );
    struct visual *visual;

    TRACE( "iface %p, result %p.\n", iface, result );

    if (!result) return E_POINTER;
    if (!(visual = container_visual_create( impl ))) return E_OUTOFMEMORY;
    *result = (struct IContainerVisual *)&visual->IContainerVisual_iface;
    return S_OK;
}

static HRESULT WINAPI compositor_CreateCubicBezierEasingFunction( struct ICompositor *iface,
        Vector2 point1, Vector2 point2, struct ICubicBezierEasingFunction **result )
{
    struct compositor *impl = impl_from_ICompositor( iface );
    struct easing_function *easing;

    TRACE( "iface %p, result %p.\n", iface, result );

    if (!result) return E_POINTER;
    if (!(easing = cubic_bezier_easing_create( impl, point1, point2 ))) return E_OUTOFMEMORY;
    *result = (struct ICubicBezierEasingFunction *)&easing->ICubicBezierEasingFunction_iface;
    return S_OK;
}

static HRESULT WINAPI compositor_CreateEffectFactoryWithProperties( struct ICompositor *iface,
        struct IGraphicsEffect *effect, struct __FIIterable_1_HSTRING *properties,
        struct ICompositionEffectFactory **result );

static HRESULT WINAPI compositor_CreateEffectFactory( struct ICompositor *iface,
        struct IGraphicsEffect *effect, struct ICompositionEffectFactory **result )
{
    return compositor_CreateEffectFactoryWithProperties( iface, effect, NULL, result );
}

static HRESULT WINAPI compositor_CreateEffectFactoryWithProperties( struct ICompositor *iface,
        struct IGraphicsEffect *effect, struct __FIIterable_1_HSTRING *properties,
        struct ICompositionEffectFactory **result )
{
    struct compositor *impl = impl_from_ICompositor( iface );
    struct effect_factory *factory;

    TRACE( "iface %p, effect %p, properties %p, result %p.\n", iface, effect, properties, result );

    if (!result) return E_POINTER;
    if (!effect) return E_POINTER;
    if (!(factory = effect_factory_create( impl, (IInspectable *)effect, properties ))) return E_OUTOFMEMORY;
    *result = &factory->ICompositionEffectFactory_iface;
    return S_OK;
}

static HRESULT WINAPI compositor_CreateExpressionAnimation( struct ICompositor *iface,
        struct IExpressionAnimation **result )
{
    return compositor_CreateExpressionAnimationWithExpression( iface, NULL, result );
}

static HRESULT WINAPI compositor_CreateExpressionAnimationWithExpression( struct ICompositor *iface,
        HSTRING expression, struct IExpressionAnimation **result )
{
    struct compositor *impl = impl_from_ICompositor( iface );
    struct expression_animation *animation;

    TRACE( "iface %p, result %p.\n", iface, result );

    if (!result) return E_POINTER;
    if (!(animation = expression_animation_create( impl, expression ))) return E_OUTOFMEMORY;
    *result = (struct IExpressionAnimation *)&animation->IExpressionAnimation_iface;
    return S_OK;
}

static HRESULT WINAPI compositor_CreateInsetClip( struct ICompositor *iface, struct IInsetClip **result )
{
    return compositor_CreateInsetClipWithInsets( iface, 0.0f, 0.0f, 0.0f, 0.0f, result );
}

static HRESULT WINAPI compositor_CreateInsetClipWithInsets( struct ICompositor *iface, FLOAT left,
        FLOAT top, FLOAT right, FLOAT bottom, struct IInsetClip **result )
{
    struct compositor *impl = impl_from_ICompositor( iface );
    struct inset_clip *clip;

    TRACE( "iface %p, result %p.\n", iface, result );

    if (!result) return E_POINTER;
    if (!(clip = inset_clip_create( impl, left, top, right, bottom ))) return E_OUTOFMEMORY;
    *result = (struct IInsetClip *)&clip->IInsetClip_iface;
    return S_OK;
}

static HRESULT WINAPI compositor_CreateLinearEasingFunction( struct ICompositor *iface,
        struct ILinearEasingFunction **result )
{
    struct compositor *impl = impl_from_ICompositor( iface );
    struct easing_function *easing;

    TRACE( "iface %p, result %p.\n", iface, result );

    if (!result) return E_POINTER;
    if (!(easing = linear_easing_create( impl ))) return E_OUTOFMEMORY;
    *result = (struct ILinearEasingFunction *)&easing->ILinearEasingFunction_iface;
    return S_OK;
}

static HRESULT WINAPI compositor_CreatePropertySet( struct ICompositor *iface,
        struct ICompositionPropertySet **result )
{
    struct compositor *impl = impl_from_ICompositor( iface );
    struct prop_set *set;

    TRACE( "iface %p, result %p.\n", iface, result );

    if (!result) return E_POINTER;
    if (!(set = prop_set_create( impl ))) return E_OUTOFMEMORY;
    *result = prop_set_get_iface( set );
    return S_OK;
}

static HRESULT WINAPI compositor_CreateQuaternionKeyFrameAnimation( struct ICompositor *iface,
        struct IQuaternionKeyFrameAnimation **result )
{
    struct compositor *impl = impl_from_ICompositor( iface );
    struct keyframe_animation *animation;

    TRACE( "iface %p, result %p.\n", iface, result );

    if (!result) return E_POINTER;
    if (!(animation = keyframe_animation_create( impl, ANIM_QUATERNION_KEY ))) return E_OUTOFMEMORY;
    *result = (struct IQuaternionKeyFrameAnimation *)&animation->IQuaternionKeyFrameAnimation_iface;
    return S_OK;
}

static HRESULT WINAPI compositor_CreateScalarKeyFrameAnimation( struct ICompositor *iface,
        struct IScalarKeyFrameAnimation **result )
{
    struct compositor *impl = impl_from_ICompositor( iface );
    struct keyframe_animation *animation;

    TRACE( "iface %p, result %p.\n", iface, result );

    if (!result) return E_POINTER;
    if (!(animation = keyframe_animation_create( impl, ANIM_SCALAR_KEY ))) return E_OUTOFMEMORY;
    *result = (struct IScalarKeyFrameAnimation *)&animation->IScalarKeyFrameAnimation_iface;
    return S_OK;
}

static HRESULT WINAPI compositor_CreateScopedBatch( struct ICompositor *iface,
        enum CompositionBatchTypes types, struct ICompositionScopedBatch **result )
{
    struct compositor *impl = impl_from_ICompositor( iface );
    struct scoped_batch *batch;

    TRACE( "iface %p, types %#x, result %p.\n", iface, types, result );

    if (!result) return E_POINTER;
    if (!(batch = scoped_batch_create( impl ))) return E_OUTOFMEMORY;
    *result = (struct ICompositionScopedBatch *)&batch->ICompositionScopedBatch_iface;
    return S_OK;
}

static HRESULT WINAPI compositor_CreateSpriteVisual( struct ICompositor *iface,
        struct ISpriteVisual **result )
{
    struct compositor *impl = impl_from_ICompositor( iface );
    struct visual *visual;

    TRACE( "iface %p, result %p.\n", iface, result );

    if (!result) return E_POINTER;
    if (!(visual = sprite_visual_create( impl ))) return E_OUTOFMEMORY;
    *result = (struct ISpriteVisual *)&visual->ISpriteVisual_iface;
    return S_OK;
}

static HRESULT WINAPI compositor_CreateSurfaceBrush( struct ICompositor *iface,
        struct ICompositionSurfaceBrush **result )
{
    return compositor_CreateSurfaceBrushWithSurface( iface, NULL, result );
}

static HRESULT WINAPI compositor_CreateSurfaceBrushWithSurface( struct ICompositor *iface,
        struct ICompositionSurface *surface, struct ICompositionSurfaceBrush **result )
{
    struct compositor *impl = impl_from_ICompositor( iface );
    struct surface_brush *brush;

    TRACE( "iface %p, surface %p, result %p.\n", iface, surface, result );

    if (!result) return E_POINTER;
    if (!(brush = surface_brush_create( impl, surface ))) return E_OUTOFMEMORY;
    *result = (struct ICompositionSurfaceBrush *)&brush->ICompositionSurfaceBrush_iface;
    return S_OK;
}

static HRESULT WINAPI compositor_CreateTargetForCurrentView( struct ICompositor *iface,
        struct ICompositionTarget **result )
{
    struct compositor *impl = impl_from_ICompositor( iface );
    struct composition_target *target;

    FIXME( "iface %p, result %p: no per-view DirectComposition target, returning a standalone target.\n",
            iface, result );

    if (!result) return E_POINTER;
    if (!(target = composition_target_create( impl, NULL, NULL ))) return E_OUTOFMEMORY;
    *result = (struct ICompositionTarget *)&target->ICompositionTarget_iface;
    return S_OK;
}

static HRESULT WINAPI compositor_CreateVector2KeyFrameAnimation( struct ICompositor *iface,
        struct IVector2KeyFrameAnimation **result )
{
    struct compositor *impl = impl_from_ICompositor( iface );
    struct keyframe_animation *animation;

    TRACE( "iface %p, result %p.\n", iface, result );

    if (!result) return E_POINTER;
    if (!(animation = keyframe_animation_create( impl, ANIM_VECTOR2_KEY ))) return E_OUTOFMEMORY;
    *result = (struct IVector2KeyFrameAnimation *)&animation->IVector2KeyFrameAnimation_iface;
    return S_OK;
}

static HRESULT WINAPI compositor_CreateVector3KeyFrameAnimation( struct ICompositor *iface,
        struct IVector3KeyFrameAnimation **result )
{
    struct compositor *impl = impl_from_ICompositor( iface );
    struct keyframe_animation *animation;

    TRACE( "iface %p, result %p.\n", iface, result );

    if (!result) return E_POINTER;
    if (!(animation = keyframe_animation_create( impl, ANIM_VECTOR3_KEY ))) return E_OUTOFMEMORY;
    *result = (struct IVector3KeyFrameAnimation *)&animation->IVector3KeyFrameAnimation_iface;
    return S_OK;
}

static HRESULT WINAPI compositor_CreateVector4KeyFrameAnimation( struct ICompositor *iface,
        struct IVector4KeyFrameAnimation **result )
{
    struct compositor *impl = impl_from_ICompositor( iface );
    struct keyframe_animation *animation;

    TRACE( "iface %p, result %p.\n", iface, result );

    if (!result) return E_POINTER;
    if (!(animation = keyframe_animation_create( impl, ANIM_VECTOR4_KEY ))) return E_OUTOFMEMORY;
    *result = (struct IVector4KeyFrameAnimation *)&animation->IVector4KeyFrameAnimation_iface;
    return S_OK;
}

static HRESULT WINAPI compositor_GetCommitBatch( struct ICompositor *iface,
        enum CompositionBatchTypes types, struct ICompositionCommitBatch **result )
{
    struct compositor *impl = impl_from_ICompositor( iface );
    struct commit_batch *batch;

    TRACE( "iface %p, types %#x, result %p.\n", iface, types, result );

    if (!result) return E_POINTER;
    if (!(batch = commit_batch_create( impl ))) return E_OUTOFMEMORY;
    *result = (struct ICompositionCommitBatch *)&batch->ICompositionCommitBatch_iface;
    return S_OK;
}

static const struct ICompositorVtbl compositor_vtbl =
{
    compositor_QueryInterface,
    compositor_AddRef,
    compositor_Release,
    compositor_GetIids,
    compositor_GetRuntimeClassName,
    compositor_GetTrustLevel,
    compositor_CreateColorKeyFrameAnimation,
    compositor_CreateColorBrush,
    compositor_CreateColorBrushWithColor,
    compositor_CreateContainerVisual,
    compositor_CreateCubicBezierEasingFunction,
    compositor_CreateEffectFactory,
    compositor_CreateEffectFactoryWithProperties,
    compositor_CreateExpressionAnimation,
    compositor_CreateExpressionAnimationWithExpression,
    compositor_CreateInsetClip,
    compositor_CreateInsetClipWithInsets,
    compositor_CreateLinearEasingFunction,
    compositor_CreatePropertySet,
    compositor_CreateQuaternionKeyFrameAnimation,
    compositor_CreateScalarKeyFrameAnimation,
    compositor_CreateScopedBatch,
    compositor_CreateSpriteVisual,
    compositor_CreateSurfaceBrush,
    compositor_CreateSurfaceBrushWithSurface,
    compositor_CreateTargetForCurrentView,
    compositor_CreateVector2KeyFrameAnimation,
    compositor_CreateVector3KeyFrameAnimation,
    compositor_CreateVector4KeyFrameAnimation,
    compositor_GetCommitBatch,
};

/*
 * ICompositorInterop: native interop bridge over DirectComposition.
 *
 * The interop interfaces are plain COM (IUnknown-based); their prologue
 * delegates to the object's root WinRT interface, sharing the refcount.
 */

#define COM_INTEROP_PROLOGUE( pfx, iface_type )                                                         static inline struct compositor *impl_from_##pfx( iface_type *iface )                               {                                                                                                      return CONTAINING_RECORD( iface, struct compositor, iface_type##_iface );                      }                                                                                                  static HRESULT WINAPI pfx##_QueryInterface( iface_type *iface, REFIID iid, void **out )            {                                                                                                      struct compositor *impl = impl_from_##pfx( iface );                                                return IInspectable_QueryInterface( (IInspectable *)&impl->ICompositor_iface, iid, out );      }                                                                                                  static ULONG WINAPI pfx##_AddRef( iface_type *iface )                                              {                                                                                                      struct compositor *impl = impl_from_##pfx( iface );                                                return IInspectable_AddRef( (IInspectable *)&impl->ICompositor_iface );                        }                                                                                                  static ULONG WINAPI pfx##_Release( iface_type *iface )                                             {                                                                                                      struct compositor *impl = impl_from_##pfx( iface );                                                return IInspectable_Release( (IInspectable *)&impl->ICompositor_iface );                       }

COM_INTEROP_PROLOGUE( cinterop, ICompositorInterop )

static HRESULT WINAPI cinterop_CreateCompositionSurfaceForHandle( struct ICompositorInterop *iface,
        HANDLE handle, struct ICompositionSurface **result )
{
    struct compositor *impl = impl_from_cinterop( iface );
    struct composition_surface *surface;
    IUnknown *dcomp_surface = NULL;
    HRESULT hr;

    TRACE( "iface %p, handle %p, result %p.\n", iface, handle, result );

    if (!result) return E_POINTER;
    *result = NULL;

    if (FAILED( hr = compositor_ensure_device( impl ) )) return hr;
    if (FAILED( hr = IDCompositionDesktopDevice_CreateSurfaceFromHandle( impl->desktop_device,
            handle, &dcomp_surface ) ))
        return hr;

    if (!(surface = composition_surface_create( impl, dcomp_surface, NULL )))
    {
        IUnknown_Release( dcomp_surface );
        return E_OUTOFMEMORY;
    }
    IUnknown_Release( dcomp_surface );
    *result = &surface->ICompositionSurface_iface;
    return S_OK;
}

static HRESULT WINAPI cinterop_CreateCompositionSurfaceForSwapChain( struct ICompositorInterop *iface,
        IUnknown *swap_chain, struct ICompositionSurface **result )
{
    struct compositor *impl = impl_from_cinterop( iface );
    struct composition_surface *surface;

    TRACE( "iface %p, swap_chain %p, result %p.\n", iface, swap_chain, result );

    if (!result) return E_POINTER;
    *result = NULL;
    if (!swap_chain) return E_INVALIDARG;

    /* The swap chain is wrapped directly: pixels presented to it are composited by the
     * DirectComposition visual that references the surface (see dlls/dcomp). */
    if (!(surface = composition_surface_create( impl, NULL, (struct IDXGISwapChain *)swap_chain )))
        return E_OUTOFMEMORY;
    *result = &surface->ICompositionSurface_iface;
    return S_OK;
}

static HRESULT WINAPI cinterop_CreateGraphicsDevice( struct ICompositorInterop *iface,
        IUnknown *device, struct ICompositionGraphicsDevice **result )
{
    struct compositor *impl = impl_from_cinterop( iface );
    struct IDCompositionDevice2 *dcomp_device = NULL;
    struct graphics_device *graphics_device;
    HRESULT hr;

    TRACE( "iface %p, device %p, result %p.\n", iface, device, result );

    if (!result) return E_POINTER;
    *result = NULL;

    if (device)
    {
        if (FAILED( hr = DCompositionCreateDevice2( device, &IID_IDCompositionDevice2,
                (void **)&dcomp_device ) ))
            return hr;
    }
    else
    {
        if (FAILED( hr = compositor_ensure_device( impl ) )) return hr;
        dcomp_device = impl->device2;
        IDCompositionDevice2_AddRef( dcomp_device );
    }

    if (!(graphics_device = graphics_device_create( impl, dcomp_device )))
    {
        IDCompositionDevice2_Release( dcomp_device );
        return E_OUTOFMEMORY;
    }
    IDCompositionDevice2_Release( dcomp_device );
    *result = &graphics_device->ICompositionGraphicsDevice_iface;
    return S_OK;
}

static const struct ICompositorInteropVtbl cinterop_vtbl =
{
    cinterop_QueryInterface,
    cinterop_AddRef,
    cinterop_Release,
    cinterop_CreateCompositionSurfaceForHandle,
    cinterop_CreateCompositionSurfaceForSwapChain,
    cinterop_CreateGraphicsDevice,
};

/*
 * ICompositorDesktopInterop: DesktopWindowTarget creation over DirectComposition.
 */

COM_INTEROP_PROLOGUE( cdesktop, ICompositorDesktopInterop )

static HRESULT WINAPI cdesktop_CreateDesktopWindowTarget( struct ICompositorDesktopInterop *iface,
        HWND hwnd, BOOL is_topmost, IInspectable **result )
{
    struct compositor *impl = impl_from_cdesktop( iface );
    struct IDCompositionTarget *dcomp_target = NULL;
    struct composition_target *target;
    HRESULT hr;

    TRACE( "iface %p, hwnd %p, is_topmost %d, result %p.\n", iface, hwnd, is_topmost, result );

    if (!result) return E_POINTER;
    *result = NULL;
    if (!hwnd) return E_INVALIDARG;

    if (FAILED( hr = compositor_ensure_device( impl ) )) return hr;
    if (FAILED( hr = IDCompositionDesktopDevice_CreateTargetForHwnd( impl->desktop_device,
            hwnd, is_topmost, &dcomp_target ) ))
        return hr;

    if (!(target = composition_target_create( impl, dcomp_target, hwnd )))
    {
        IDCompositionTarget_Release( dcomp_target );
        return E_OUTOFMEMORY;
    }
    *result = (IInspectable *)&target->ICompositionTarget_iface;
    return S_OK;
}

static HRESULT WINAPI cdesktop_EnsureOnThread( struct ICompositorDesktopInterop *iface, UINT32 thread_id )
{
    FIXME( "iface %p, thread_id %u stub!\n", iface, thread_id );
    return S_OK;
}

static const struct ICompositorDesktopInteropVtbl cdesktop_vtbl =
{
    cdesktop_QueryInterface,
    cdesktop_AddRef,
    cdesktop_Release,
    cdesktop_CreateDesktopWindowTarget,
    cdesktop_EnsureOnThread,
};

/*
 * IClosable.
 */

DEFINE_INHERITED_PROLOGUE( compositor_closable, IClosable, struct compositor, ICompositor_iface )

static HRESULT WINAPI compositor_closable_Close( struct IClosable *iface )
{
    TRACE( "iface %p stub!\n", iface );
    return S_OK;
}

static const struct IClosableVtbl compositor_closable_vtbl =
{
    compositor_closable_QueryInterface,
    compositor_closable_AddRef,
    compositor_closable_Release,
    compositor_closable_GetIids,
    compositor_closable_GetRuntimeClassName,
    compositor_closable_GetTrustLevel,
    compositor_closable_Close,
};

struct compositor *compositor_create( void )
{
    struct compositor *impl;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return NULL;
    impl->ICompositor_iface.lpVtbl = &compositor_vtbl;
    impl->ICompositorInterop_iface.lpVtbl = &cinterop_vtbl;
    impl->ICompositorDesktopInterop_iface.lpVtbl = &cdesktop_vtbl;
    impl->IClosable_iface.lpVtbl = &compositor_closable_vtbl;
    impl->ref = 1;
    return impl;
}

/*
 * Activation factory. Compositor is activatable; activating it (RoGetActivationFactory
 * with the default interface, or ActivateInstance) creates a new compositor instance.
 */

/*
 * Microsoft.UI.Composition.IInteropCompositorFactoryPartner {22118adf-23f1-4801-bcfa-
 * 66cbf48cc51b} is the interface the WinUI 3 XAML root asks the Compositor activation
 * factory for: Microsoft.ui.xaml.dll!ActivationFactoryCache::GetInteropCompositorFactoryPartner()
 * calls RoGetActivationFactory for it and, on failure, goes straight through OnFailure ->
 * TraceForFailFast -> FailFastWithStowedExceptions (verified in IDA), which is how the app
 * UI thread dies with the compositor await never completing.
 *
 * Windows hosts it on Microsoft::UI::Composition::CompositorFactory (dcompi.dll), a WRL
 * Implements<FtmBase, IInteropCompositorFactoryPartner> + ICompositorStatics +
 * IInteropCompositorFactoryRestricted object; the partner is handed out as a selector
 * vftable of that same instance, so it is one object answering the activation interfaces
 * and the partner interface alike.  Its layout is IUnknown + IActivationFactory +
 * CreateInteropCompositor + CheckEnabled (dcompi vftable 0x180122c40, methods 0x18005a5d0
 * and 0x180056d00).  There is no published IDL for it, so it is declared here.
 *
 * CreateInteropCompositor hands out this module's own Compositor QI'd for the requested
 * riid (the Windows factory builds a fresh InteropCompositor per call and QIs it the same
 * way, dcompi 0x18005a5d0/0x18005a759); CheckEnabled reports enabled and available the way
 * dcompi 0x180056d00 does.  The observed callers are Microsoft.UI.Windowing.Core.dll!
 * CTitleBar::EvaluateNonClientCustomizationSupported (0x18003249a, TitleBar.cxx):
 * RoGetActivationFactory(Windows.UI.Composition.Compositor, IActivationFactory), QI
 * {22118adf-...}, CreateInteropCompositor(NULL, NULL, ICompositor {b403ca50-7f8c-4e83-
 * 985f-cc45060036d8}) and then a QI for {ca67b562-1c32-4017-9dd9-3d4b7e2510aa} on the
 * returned compositor, which sets the static s_nonClientSupport capability flag when it
 * succeeds; and Microsoft.ui.xaml.dll!DCompTreeHost::CreateWinRTInteropCompositionDevice
 * (0x1802357b8), which passes a DCompInteropCompositorPartnerCallback and requests
 * {d14b6158-c3fa-4bce-9c1f-b61d8665eab0} (IDCompositionDesktopDevicePartner, see
 * dlls/dcomp/dcomp_private_iface.idl).  {ca67b562-...} is not documented anywhere in this
 * tree and is not implemented: the probe degrades to the s_nonClientSupport=false fallback
 * path.  The Windows factory also answers ICompositorStatics {6baa947a-f103-55c6-91ad-
 * 3d275bea65c4} and IInteropCompositorFactoryRestricted {b27006d1-d095-432c-9d79-
 * 038d269d7aa3}; those IIDs are RECOVERED, not documented: they are the guid_of
 * comparisons of the WRL CanCastTo chain at dcompi 0x180040f78 (cmp at 0x18004102f
 * matches 0x6baa947a, the following cmp at 0x180041063 matches 0xb27006d1, in
 * Implements<> template order), and no caller on this system requests them.
 */
const GUID IID_IInteropCompositorFactoryPartner =
    { 0x22118adf, 0x23f1, 0x4801, { 0xbc, 0xfa, 0x66, 0xcb, 0xf4, 0x8c, 0xc5, 0x1b } };

typedef struct IInteropCompositorFactoryPartner IInteropCompositorFactoryPartner;
typedef struct IInteropCompositorFactoryPartnerVtbl
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)( IInteropCompositorFactoryPartner *iface, REFIID riid, void **obj );
    ULONG (STDMETHODCALLTYPE *AddRef)( IInteropCompositorFactoryPartner *iface );
    ULONG (STDMETHODCALLTYPE *Release)( IInteropCompositorFactoryPartner *iface );
    HRESULT (STDMETHODCALLTYPE *GetIids)( IInteropCompositorFactoryPartner *iface, ULONG *count, IID **iids );
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)( IInteropCompositorFactoryPartner *iface, HSTRING *name );
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)( IInteropCompositorFactoryPartner *iface, TrustLevel *level );
    HRESULT (STDMETHODCALLTYPE *CreateInteropCompositor)( IInteropCompositorFactoryPartner *iface,
            IUnknown *device, IUnknown *callback, REFIID riid, void **result );
    HRESULT (STDMETHODCALLTYPE *CheckEnabled)( IInteropCompositorFactoryPartner *iface,
            bool *enabled, bool *available );
} IInteropCompositorFactoryPartnerVtbl;
struct IInteropCompositorFactoryPartner
{
    const struct IInteropCompositorFactoryPartnerVtbl *lpVtbl;
};

struct compositor_factory
{
    struct IActivationFactory IActivationFactory_iface;
    struct IInteropCompositorFactoryPartner IInteropCompositorFactoryPartner_iface;
    LONG ref;
};

static inline struct compositor_factory *impl_from_IInteropCompositorFactoryPartner(
        struct IInteropCompositorFactoryPartner *iface )
{
    return CONTAINING_RECORD( iface, struct compositor_factory, IInteropCompositorFactoryPartner_iface );
}

static inline struct compositor_factory *impl_from_IActivationFactory( struct IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct compositor_factory, IActivationFactory_iface );
}

static HRESULT WINAPI factory_QueryInterface( struct IActivationFactory *iface, REFIID iid, void **out )
{
    struct compositor_factory *impl = impl_from_IActivationFactory( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown )
        || IsEqualGUID( iid, &IID_IInspectable )
        || IsEqualGUID( iid, &IID_IAgileObject )
        || IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = &impl->IActivationFactory_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    /* Activatable class: requesting an instance interface activates a new Compositor. */
    if (IsEqualGUID( iid, &IID_ICompositor )
        || IsEqualGUID( iid, &IID_ICompositorInterop )
        || IsEqualGUID( iid, &IID_ICompositorDesktopInterop ))
    {
        struct compositor *compositor;
        HRESULT hr;

        if (!(compositor = compositor_create() )) return E_OUTOFMEMORY;
        hr = ICompositor_QueryInterface( &compositor->ICompositor_iface, iid, out );
        IInspectable_Release( (IInspectable *)&compositor->ICompositor_iface ); /* drop creation ref */
        return hr;
    }

    /* The WinUI 3 XAML root requests this interface from the activation factory and
     * fail-fasts when it is refused: hand out the partner selector of the same object. */
    if (IsEqualGUID( iid, &IID_IInteropCompositorFactoryPartner ))
    {
        *out = &impl->IInteropCompositorFactoryPartner_iface;
        IInspectable_AddRef( (IInspectable *)&impl->IActivationFactory_iface );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI factory_AddRef( struct IActivationFactory *iface )
{
    struct compositor_factory *impl = impl_from_IActivationFactory( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI factory_Release( struct IActivationFactory *iface )
{
    struct compositor_factory *impl = impl_from_IActivationFactory( iface );
    return InterlockedDecrement( &impl->ref );
}

static HRESULT WINAPI factory_GetIids( struct IActivationFactory *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI factory_GetRuntimeClassName( struct IActivationFactory *iface, HSTRING *class_name )
{
    return WindowsCreateString( RuntimeClass_Windows_UI_Composition_Compositor,
            ARRAYSIZE( RuntimeClass_Windows_UI_Composition_Compositor ) - 1, class_name );
}

static HRESULT WINAPI factory_GetTrustLevel( struct IActivationFactory *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI factory_ActivateInstance( struct IActivationFactory *iface,
        IInspectable **instance )
{
    struct compositor *compositor;

    TRACE( "iface %p, instance %p.\n", iface, instance );

    if (!instance) return E_POINTER;
    if (!(compositor = compositor_create() )) return E_OUTOFMEMORY;
    *instance = (IInspectable *)&compositor->ICompositor_iface;
    return S_OK;
}

static const struct IActivationFactoryVtbl factory_vtbl =
{
    factory_QueryInterface,
    factory_AddRef,
    factory_Release,
    factory_GetIids,
    factory_GetRuntimeClassName,
    factory_GetTrustLevel,
    factory_ActivateInstance,
};

/*
 * IInteropCompositorFactoryPartner, served by the same instance as the activation factory
 * (see the declaration above for the layout evidence).
 */

static HRESULT WINAPI partner_QueryInterface( struct IInteropCompositorFactoryPartner *iface,
        REFIID iid, void **out )
{
    struct compositor_factory *impl = impl_from_IInteropCompositorFactoryPartner( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IInteropCompositorFactoryPartner ))
    {
        *out = &impl->IInteropCompositorFactoryPartner_iface;
        IInspectable_AddRef( (IInspectable *)&impl->IActivationFactory_iface );
        return S_OK;
    }

    return factory_QueryInterface( &impl->IActivationFactory_iface, iid, out );
}

static ULONG WINAPI partner_AddRef( struct IInteropCompositorFactoryPartner *iface )
{
    return factory_AddRef( &impl_from_IInteropCompositorFactoryPartner( iface )->IActivationFactory_iface );
}

static ULONG WINAPI partner_Release( struct IInteropCompositorFactoryPartner *iface )
{
    return factory_Release( &impl_from_IInteropCompositorFactoryPartner( iface )->IActivationFactory_iface );
}

static HRESULT WINAPI partner_GetIids( struct IInteropCompositorFactoryPartner *iface,
        ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI partner_GetRuntimeClassName( struct IInteropCompositorFactoryPartner *iface,
        HSTRING *class_name )
{
    return WindowsCreateString( RuntimeClass_Windows_UI_Composition_Compositor,
            ARRAYSIZE( RuntimeClass_Windows_UI_Composition_Compositor ) - 1, class_name );
}

static HRESULT WINAPI partner_GetTrustLevel( struct IInteropCompositorFactoryPartner *iface,
        TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

/* Bind a caller-supplied rendering device, mirroring the lazy bridge in
 * compositor_ensure_device(): dcompi's CreateInteropCompositor hands the device
 * argument to InteropCompositor::RuntimeClassInitialize (0x1800f0800), which feeds it
 * to Compositor::RuntimeClassInitialize to create the DirectComposition device.  Both
 * observed callers pass a NULL device (Microsoft.UI.Windowing.Core.dll!CTitleBar::
 * EvaluateNonClientCustomizationSupported 0x180032609 and Microsoft.ui.xaml.dll!
 * DCompTreeHost::CreateWinRTInteropCompositionDevice 0x180235843), which leaves the
 * device lazily created on first use. */
static HRESULT compositor_init_render_device( struct compositor *impl, IUnknown *render_target )
{
    HRESULT hr;

    if (FAILED( hr = DCompositionCreateDevice2( render_target, &IID_IDCompositionDevice2,
            (void **)&impl->device2 ) ))
        return hr;

    if (FAILED( IDCompositionDevice2_QueryInterface( impl->device2,
            &IID_IDCompositionDesktopDevice, (void **)&impl->desktop_device ) ))
    {
        impl->desktop_device = NULL;
    }
    if (FAILED( IDCompositionDevice2_QueryInterface( impl->device2,
            &IID_IDCompositionDevice, (void **)&impl->device ) ))
    {
        impl->device = NULL;
    }
    return S_OK;
}

static HRESULT WINAPI partner_CreateInteropCompositor( struct IInteropCompositorFactoryPartner *iface,
        IUnknown *device, IUnknown *callback, REFIID riid, void **result )
{
    struct compositor *compositor;
    HRESULT hr;

    TRACE( "iface %p, device %p, callback %p, riid %s, result %p.\n",
            iface, device, callback, debugstr_guid( riid ), result );

    if (!result) return E_POINTER;
    *result = NULL;

    /* dcompi's CompositorFactory::CreateInteropCompositor (0x18005a5d0) constructs a fresh
     * Microsoft.UI.Composition.InteropCompositor per call (0x18005a60c), runs
     * RuntimeClassInitialize with the device and callback arguments (0x18005a6c7), QIs the
     * object for the requested riid (0x18005a759), and returns that; the local reference
     * is dropped.  The bridge's compositor object is the in-process equivalent: this
     * module's own Compositor over DirectComposition. */
    if (!(compositor = compositor_create() )) return E_OUTOFMEMORY;

    if (device && FAILED( hr = compositor_init_render_device( compositor, device ) ))
        goto done;

    /* The callback (IInteropCompositorPartnerCallback; XAML's implementation is
     * Microsoft.ui.xaml.dll vftable 0x180a8e620: IUnknown + NotifyDirty (0x18022e750,
     * arming DCompTreeHost::RequestMainDCompDeviceCommit) + one more slot) is stored by
     * dcompi at InteropCompositor+1240 (0x1800f08ec) and fired by the server frame pump
     * of the Microsoft.UI nesting channel.  This module composites in-process through
     * DirectComposition and has no frame pump to fire it from; Windowing.Core's probe
     * passes NULL.  Accepted and not wired, honestly. */
    hr = ICompositor_QueryInterface( &compositor->ICompositor_iface, riid, result );

done:
    IInspectable_Release( (IInspectable *)&compositor->ICompositor_iface ); /* drop creation ref */
    return hr;
}

static HRESULT WINAPI partner_CheckEnabled( struct IInteropCompositorFactoryPartner *iface,
        bool *enabled, bool *available )
{
    TRACE( "iface %p, enabled %p, available %p.\n", iface, enabled, available );

    if (!enabled || !available) return E_POINTER;

    /* dcompi's CompositorFactory::CheckEnabled (0x180056d00) writes true to both
     * outputs and returns S_OK unconditionally; there is no registry or config gate. */
    *enabled = true;
    *available = true;
    return S_OK;
}

static const struct IInteropCompositorFactoryPartnerVtbl partner_vtbl =
{
    partner_QueryInterface,
    partner_AddRef,
    partner_Release,
    partner_GetIids,
    partner_GetRuntimeClassName,
    partner_GetTrustLevel,
    partner_CreateInteropCompositor,
    partner_CheckEnabled,
};

static struct compositor_factory compositor_factory =
{
    {&factory_vtbl},
    {&partner_vtbl},
    1,
};

IActivationFactory *compositor_factory_get( void )
{
    return (IActivationFactory *)&compositor_factory;
}
