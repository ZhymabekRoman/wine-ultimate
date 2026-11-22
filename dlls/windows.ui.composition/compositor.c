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

static const GUID IID_ICompositor2_local =
    { 0x735081dc, 0x5e24, 0x45da, { 0xa3, 0x8f, 0xe3, 0x2c, 0xc3, 0x49, 0xa9, 0xa0 } };
static const GUID IID_ICompositorWithVisualSurface_local =
    { 0xcfa1658b, 0x0123, 0x4551, { 0x88, 0x91, 0x89, 0xbd, 0xcc, 0x40, 0x32, 0x2b } };
static const GUID IID_ICompositorWithBlurredWallpaper_local =
    { 0x0d8fb190, 0xf122, 0x5b8d, { 0x9f, 0xdd, 0x54, 0x3b, 0x0d, 0x8e, 0xb7, 0xf3 } };
static const GUID IID_ICompositor4_local =
    { 0xae47e78a, 0x7910, 0x4425, { 0xa4, 0x82, 0xa0, 0x5b, 0x75, 0x8a, 0xdc, 0xe9 } };
static const GUID IID_ICompositor5_local =
    { 0x48ea31ad, 0x7fcd, 0x4076, { 0xa7, 0x9c, 0x90, 0xcc, 0x4b, 0x85, 0x2c, 0x9b } };
/* Declared here; the interface itself is implemented at its vtable below. */
static const GUID IID_ICompositor6_local =
    { 0x7a38b2bd, 0xcec8, 0x4eeb, { 0x83, 0x0f, 0xd8, 0xd0, 0x7a, 0xed, 0xeb, 0xc3 } };
static const GUID IID_ICompositorPartner_local =
    { 0x9cbd9312, 0x070d, 0x4588, { 0x9b, 0xf3, 0xbb, 0xf5, 0x28, 0xcf, 0x3e, 0x84 } };

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
    if (IsEqualGUID( iid, &IID_ICompositor2_local ))
    {
        *out = &impl->ICompositor2_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_ICompositorWithVisualSurface_local ))
    {
        *out = &impl->ICompositorWithVisualSurface_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_ICompositorWithBlurredWallpaper_local ))
    {
        *out = &impl->ICompositorWithBlurredWallpaper_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_ICompositor4_local ))
    {
        *out = &impl->ICompositor4_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_ICompositor5_local ))
    {
        *out = &impl->ICompositor5_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_ICompositor6_local ))
    {
        *out = &impl->ICompositor6_iface;
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
    if (IsEqualGUID( iid, &IID_ICompositorPartner_local ))
    {
        *out = &impl->ICompositorPartner_iface;
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

/*
 * Generic lightweight WinRT stub object for ICompositor5 shape/geometry/visual factories.
 */
struct comp5_stub
{
    const void *vtbl;
    const void *co4_vtbl;
    LONG ref;
    const WCHAR *class_name;
    struct compositor *compositor;
    void *prop1;
    void *prop2;
    void *prop3;
    float f1, f2, f3, f4;
};

static const GUID IID_IReferenceTracker_local =
    { 0x11d3b13a, 0x180e, 0x4789, { 0xa8, 0xbe, 0x77, 0x12, 0x88, 0x28, 0x93, 0xe6 } };
static const GUID IID_ICompositionObject4_local =
    { 0xf2bd13c3, 0xba7e, 0x4b0f, { 0x91, 0x26, 0xff, 0xb7, 0x53, 0x6b, 0x81, 0x76 } };

static HRESULT WINAPI comp5_stub_QI( void *iface, REFIID iid, void **out )
{
    struct comp5_stub *obj = iface;
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IReferenceTracker_local ) || IsEqualGUID( iid, &IID_IMarshal ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    if (IsEqualGUID( iid, &IID_ICompositionObject4_local ))
    {
        *out = &obj->co4_vtbl;
        InterlockedIncrement( &obj->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IClosable ) ||
        IsEqualGUID( iid, &IID_ICompositionObject ))
    {
        *out = iface;
        InterlockedIncrement( &obj->ref );
        return S_OK;
    }
    /* Composition shapes, geometries, and visual stubs expose IInspectable + stubbed property slots */
    TRACE( "comp5_stub(%s) QI %s -> returning self\n", debugstr_w(obj->class_name), debugstr_guid(iid) );
    *out = iface;
    InterlockedIncrement( &obj->ref );
    return S_OK;
}
static ULONG WINAPI comp5_stub_AddRef( void *iface ) { return InterlockedIncrement( &((struct comp5_stub *)iface)->ref ); }
static ULONG WINAPI comp5_stub_Release( void *iface )
{
    struct comp5_stub *obj = iface;
    ULONG r = InterlockedDecrement( &obj->ref );
    if (!r) free( obj );
    return r;
}
static HRESULT WINAPI comp5_stub_GetIids( void *iface, ULONG *cnt, IID **iids )
{
    if (cnt) *cnt = 0;
    if (iids) *iids = NULL;
    return S_OK;
}
static HRESULT WINAPI comp5_stub_GetRuntimeClassName( void *iface, HSTRING *name )
{
    struct comp5_stub *obj = iface;
    return WindowsCreateString( obj->class_name, lstrlenW( obj->class_name ), name );
}
static HRESULT WINAPI comp5_stub_GetTrustLevel( void *iface, TrustLevel *lvl )
{
    if (lvl) *lvl = BaseTrust;
    return S_OK;
}
static HRESULT WINAPI comp5_stub_noop( void *iface, void *a1, void *a2, void *a3 )
{
    return S_OK;
}
static HRESULT comp5_stub_create( const WCHAR *name, void **out );

static HRESULT WINAPI comp5_stub_get_child( void *iface, void **out )
{
    struct comp5_stub *obj = iface;
    if (!out) return E_POINTER;
    if (obj->compositor)
    {
        *out = &obj->compositor->ICompositor_iface;
        ICompositor_AddRef( &obj->compositor->ICompositor_iface );
        return S_OK;
    }
    TRACE( "comp5_stub(%s) get_child -> creating child stub\n", debugstr_w(obj->class_name) );
    if (!obj->prop1)
    {
        const WCHAR *child_cls = (obj->class_name && !wcscmp( obj->class_name, L"Windows.UI.Composition.CompositionPathGeometry" ))
            ? L"Windows.UI.Composition.AnimationController"
            : L"Windows.UI.Composition.CompositionShapeCollection";
        comp5_stub_create( child_cls, &obj->prop1 );
    }
    *out = obj->prop1;
    if (*out) InterlockedIncrement( &((struct comp5_stub *)*out)->ref );
    return S_OK;
}
static HRESULT WINAPI comp5_stub_get_color_stops( void *iface, void **out )
{
    struct comp5_stub *obj = iface;
    if (obj->class_name && wcsstr( obj->class_name, L"GradientBrush" ))
    {
        if (!out) return E_POINTER;
        if (!obj->prop2)
            comp5_stub_create( L"Windows.UI.Composition.CompositionColorGradientStopCollection", &obj->prop2 );
        *out = obj->prop2;
        if (*out) InterlockedIncrement( &((struct comp5_stub *)*out)->ref );
        return S_OK;
    }
    return S_OK;
}
static HRESULT WINAPI comp5_stub_get_zero( void *iface, void **out )
{
    if (out) *out = NULL;
    return S_OK;
}
static const void *comp5_stub_vtbl[48] =
{
    comp5_stub_QI, comp5_stub_AddRef, comp5_stub_Release,
    comp5_stub_GetIids, comp5_stub_GetRuntimeClassName, comp5_stub_GetTrustLevel,
    comp5_stub_get_child, comp5_stub_noop, comp5_stub_noop, comp5_stub_noop,
    comp5_stub_get_color_stops, comp5_stub_noop, comp5_stub_noop, comp5_stub_noop,
    comp5_stub_noop, comp5_stub_noop, comp5_stub_noop, comp5_stub_noop,
    comp5_stub_noop, comp5_stub_noop, comp5_stub_noop, comp5_stub_noop,
    comp5_stub_noop, comp5_stub_noop, comp5_stub_noop, comp5_stub_noop,
    comp5_stub_get_zero,
    [27 ... 47] = comp5_stub_noop,
};

static inline struct comp5_stub *comp5_from_co4( void *iface )
{
    return CONTAINING_RECORD( iface, struct comp5_stub, co4_vtbl );
}
static HRESULT WINAPI comp5_co4_QI( void *iface, REFIID iid, void **out ) { return comp5_stub_QI( comp5_from_co4(iface), iid, out ); }
static ULONG WINAPI comp5_co4_AddRef( void *iface ) { return comp5_stub_AddRef( comp5_from_co4(iface) ); }
static ULONG WINAPI comp5_co4_Release( void *iface ) { return comp5_stub_Release( comp5_from_co4(iface) ); }
static HRESULT WINAPI comp5_co4_GetIids( void *iface, ULONG *cnt, IID **iids ) { return comp5_stub_GetIids( comp5_from_co4(iface), cnt, iids ); }
static HRESULT WINAPI comp5_co4_GetRuntimeClassName( void *iface, HSTRING *name ) { return comp5_stub_GetRuntimeClassName( comp5_from_co4(iface), name ); }
static HRESULT WINAPI comp5_co4_GetTrustLevel( void *iface, TrustLevel *lvl ) { return comp5_stub_GetTrustLevel( comp5_from_co4(iface), lvl ); }
static HRESULT WINAPI comp5_co4_TryGetAnimationController( void *iface, HSTRING prop, void **out )
{
    if (!out) return E_POINTER;
    return comp5_stub_create( L"Windows.UI.Composition.AnimationController", out );
}
static const void *comp5_co4_vtbl[16] =
{
    comp5_co4_QI, comp5_co4_AddRef, comp5_co4_Release,
    comp5_co4_GetIids, comp5_co4_GetRuntimeClassName, comp5_co4_GetTrustLevel,
    comp5_co4_TryGetAnimationController,
    [7 ... 15] = comp5_stub_noop,
};

static HRESULT comp5_stub_create( const WCHAR *name, void **out )
{
    struct comp5_stub *obj;
    if (!out) return E_POINTER;
    if (!(obj = calloc( 1, sizeof(*obj) ))) return E_OUTOFMEMORY;
    obj->vtbl = comp5_stub_vtbl;
    obj->co4_vtbl = comp5_co4_vtbl;
    obj->ref = 1;
    obj->class_name = name;
    *out = obj;
    return S_OK;
}

/* ImplicitAnimationCollection: ICompositionObject + IMap<HSTRING, ICompositionAnimationBase> */
struct implicit_anim_entry
{
    HSTRING key;
    IUnknown *val;
};

struct implicit_anim_collection
{
    const void *const *obj_vtbl;
    const void *const *map_vtbl;
    LONG ref;
    struct compositor *compositor;
    struct implicit_anim_entry *entries;
    UINT32 count;
    UINT32 cap;
};

static const GUID IID_IImplicitAnimationCollection =
    { 0x0598a2af, 0x2e91, 0x49a4, { 0xa7, 0xe3, 0x28, 0x6a, 0x62, 0x2a, 0x5a, 0x05 } };
static const GUID IID_IMap_HSTRING_ICompAnimBase =
    { 0x4ba9aa1d, 0x57ac, 0x54de, { 0xa8, 0x07, 0xb5, 0x2e, 0x68, 0x9b, 0xfc, 0x04 } };
static const GUID IID_IMapView_HSTRING_ICompAnimBase =
    { 0xb65e6ece, 0xd9af, 0x5d4a, { 0xab, 0xaa, 0xc0, 0x8e, 0x34, 0xa5, 0xf8, 0x15 } };

static inline struct implicit_anim_collection *iac_from_obj( void *iface )
{
    return CONTAINING_RECORD( iface, struct implicit_anim_collection, obj_vtbl );
}
static inline struct implicit_anim_collection *iac_from_map( void *iface )
{
    return CONTAINING_RECORD( iface, struct implicit_anim_collection, map_vtbl );
}

static HRESULT iac_qi( struct implicit_anim_collection *impl, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IClosable ) ||
        IsEqualGUID( iid, &IID_ICompositionObject ) || IsEqualGUID( iid, &IID_IImplicitAnimationCollection ))
    {
        *out = &impl->obj_vtbl;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IMap_HSTRING_ICompAnimBase ) ||
        IsEqualGUID( iid, &IID_IMapView_HSTRING_ICompAnimBase ))
    {
        *out = &impl->map_vtbl;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG iac_release( struct implicit_anim_collection *impl )
{
    ULONG r = InterlockedDecrement( &impl->ref );
    if (!r)
    {
        UINT32 i;
        for (i = 0; i < impl->count; i++)
        {
            if (impl->entries[i].key) WindowsDeleteString( impl->entries[i].key );
            if (impl->entries[i].val) IUnknown_Release( impl->entries[i].val );
        }
        free( impl->entries );
        if (impl->compositor) ICompositor_Release( &impl->compositor->ICompositor_iface );
        free( impl );
    }
    return r;
}

static HRESULT WINAPI iac_obj_QI( void *iface, REFIID iid, void **out ) { return iac_qi( iac_from_obj(iface), iid, out ); }
static ULONG WINAPI iac_obj_AddRef( void *iface ) { return InterlockedIncrement( &iac_from_obj(iface)->ref ); }
static ULONG WINAPI iac_obj_Release( void *iface ) { return iac_release( iac_from_obj(iface) ); }
static HRESULT WINAPI iac_obj_GetIids( void *iface, ULONG *cnt, IID **iids ) { if (cnt) *cnt = 0; if (iids) *iids = NULL; return S_OK; }
static HRESULT WINAPI iac_obj_GetRuntimeClassName( void *iface, HSTRING *name )
{
    static const WCHAR cls[] = L"Windows.UI.Composition.ImplicitAnimationCollection";
    return WindowsCreateString( cls, ARRAY_SIZE(cls) - 1, name );
}
static HRESULT WINAPI iac_obj_GetTrustLevel( void *iface, TrustLevel *lvl ) { if (lvl) *lvl = BaseTrust; return S_OK; }
static HRESULT WINAPI iac_obj_get_Compositor( void *iface, void **out )
{
    struct implicit_anim_collection *impl = iac_from_obj( iface );
    if (!out) return E_POINTER;
    if (impl->compositor)
    {
        *out = &impl->compositor->ICompositor_iface;
        ICompositor_AddRef( &impl->compositor->ICompositor_iface );
        return S_OK;
    }
    *out = NULL;
    return S_OK;
}
static HRESULT WINAPI iac_obj_noop( void *iface, void *a1, void *a2, void *a3 ) { return S_OK; }

static const void *const iac_obj_vtbl[24] =
{
    iac_obj_QI, iac_obj_AddRef, iac_obj_Release,
    iac_obj_GetIids, iac_obj_GetRuntimeClassName, iac_obj_GetTrustLevel,
    iac_obj_get_Compositor,
    [7 ... 23] = iac_obj_noop,
};

static int iac_find( struct implicit_anim_collection *impl, HSTRING key )
{
    UINT32 i;
    INT32 cmp = 0;
    for (i = 0; i < impl->count; i++)
    {
        if (SUCCEEDED(WindowsCompareStringOrdinal( impl->entries[i].key, key, &cmp )) && cmp == 0)
            return (int)i;
    }
    return -1;
}

static HRESULT WINAPI iac_map_QI( void *iface, REFIID iid, void **out ) { return iac_qi( iac_from_map(iface), iid, out ); }
static ULONG WINAPI iac_map_AddRef( void *iface ) { return InterlockedIncrement( &iac_from_map(iface)->ref ); }
static ULONG WINAPI iac_map_Release( void *iface ) { return iac_release( iac_from_map(iface) ); }
static HRESULT WINAPI iac_map_GetIids( void *iface, ULONG *cnt, IID **iids ) { if (cnt) *cnt = 0; if (iids) *iids = NULL; return S_OK; }
static HRESULT WINAPI iac_map_GetRuntimeClassName( void *iface, HSTRING *name )
{
    static const WCHAR cls[] = L"Windows.UI.Composition.ImplicitAnimationCollection";
    return WindowsCreateString( cls, ARRAY_SIZE(cls) - 1, name );
}
static HRESULT WINAPI iac_map_GetTrustLevel( void *iface, TrustLevel *lvl ) { if (lvl) *lvl = BaseTrust; return S_OK; }
static HRESULT WINAPI iac_map_Lookup( void *iface, HSTRING key, IUnknown **value )
{
    struct implicit_anim_collection *impl = iac_from_map( iface );
    int idx;
    if (!value) return E_POINTER;
    *value = NULL;
    idx = iac_find( impl, key );
    if (idx < 0) return E_BOUNDS;
    *value = impl->entries[idx].val;
    if (*value) IUnknown_AddRef( *value );
    return S_OK;
}
static HRESULT WINAPI iac_map_get_Size( void *iface, UINT32 *size )
{
    if (!size) return E_POINTER;
    *size = iac_from_map( iface )->count;
    return S_OK;
}
static HRESULT WINAPI iac_map_HasKey( void *iface, HSTRING key, boolean *found )
{
    if (!found) return E_POINTER;
    *found = (iac_find( iac_from_map( iface ), key ) >= 0);
    return S_OK;
}
static HRESULT WINAPI iac_map_GetView( void *iface, void **view )
{
    struct implicit_anim_collection *impl = iac_from_map( iface );
    if (!view) return E_POINTER;
    *view = &impl->map_vtbl;
    InterlockedIncrement( &impl->ref );
    return S_OK;
}
static HRESULT WINAPI iac_map_Insert( void *iface, HSTRING key, IUnknown *value, boolean *replaced )
{
    struct implicit_anim_collection *impl = iac_from_map( iface );
    int idx = iac_find( impl, key );
    if (idx >= 0)
    {
        if (value) IUnknown_AddRef( value );
        if (impl->entries[idx].val) IUnknown_Release( impl->entries[idx].val );
        impl->entries[idx].val = value;
        if (replaced) *replaced = TRUE;
        return S_OK;
    }
    if (impl->count >= impl->cap)
    {
        UINT32 ncap = impl->cap ? impl->cap * 2 : 8;
        struct implicit_anim_entry *ne = realloc( impl->entries, ncap * sizeof(*ne) );
        if (!ne) return E_OUTOFMEMORY;
        impl->entries = ne;
        impl->cap = ncap;
    }
    impl->entries[impl->count].key = NULL;
    WindowsDuplicateString( key, &impl->entries[impl->count].key );
    if (value) IUnknown_AddRef( value );
    impl->entries[impl->count].val = value;
    impl->count++;
    if (replaced) *replaced = FALSE;
    return S_OK;
}
static HRESULT WINAPI iac_map_Remove( void *iface, HSTRING key )
{
    struct implicit_anim_collection *impl = iac_from_map( iface );
    int idx = iac_find( impl, key );
    if (idx < 0) return E_BOUNDS;
    if (impl->entries[idx].key) WindowsDeleteString( impl->entries[idx].key );
    if (impl->entries[idx].val) IUnknown_Release( impl->entries[idx].val );
    if ((UINT32)idx + 1 < impl->count)
        memmove( &impl->entries[idx], &impl->entries[idx + 1], (impl->count - idx - 1) * sizeof(*impl->entries) );
    impl->count--;
    return S_OK;
}
static HRESULT WINAPI iac_map_Clear( void *iface )
{
    struct implicit_anim_collection *impl = iac_from_map( iface );
    UINT32 i;
    for (i = 0; i < impl->count; i++)
    {
        if (impl->entries[i].key) WindowsDeleteString( impl->entries[i].key );
        if (impl->entries[i].val) IUnknown_Release( impl->entries[i].val );
    }
    impl->count = 0;
    return S_OK;
}

static const void *const iac_map_vtbl[] =
{
    iac_map_QI, iac_map_AddRef, iac_map_Release,
    iac_map_GetIids, iac_map_GetRuntimeClassName, iac_map_GetTrustLevel,
    iac_map_Lookup, iac_map_get_Size, iac_map_HasKey, iac_map_GetView,
    iac_map_Insert, iac_map_Remove, iac_map_Clear,
};

static HRESULT implicit_anim_collection_create( struct compositor *comp, void **out )
{
    struct implicit_anim_collection *impl;
    if (!out) return E_POINTER;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->obj_vtbl = iac_obj_vtbl;
    impl->map_vtbl = iac_map_vtbl;
    impl->ref = 1;
    impl->compositor = comp;
    if (comp) ICompositor_AddRef( &comp->ICompositor_iface );
    *out = &impl->obj_vtbl;
    return S_OK;
}

struct comp5_stub *comp5_stub_create_public( const WCHAR *name )
{
    void *out = NULL;
    comp5_stub_create( name, &out );
    return (struct comp5_stub *)out;
}

static inline struct compositor *impl_from_ICompositor4( void *iface )
{
    return CONTAINING_RECORD( iface, struct compositor, ICompositor4_iface );
}
static HRESULT WINAPI compositor4_QueryInterface( void *iface, REFIID iid, void **out )
{
    return compositor_QueryInterface( &impl_from_ICompositor4(iface)->ICompositor_iface, iid, out );
}
static ULONG WINAPI compositor4_AddRef( void *iface )
{
    return compositor_AddRef( &impl_from_ICompositor4(iface)->ICompositor_iface );
}
static ULONG WINAPI compositor4_Release( void *iface )
{
    return compositor_Release( &impl_from_ICompositor4(iface)->ICompositor_iface );
}
static HRESULT WINAPI compositor4_GetIids( void *iface, ULONG *c, IID **i )
{
    return compositor_GetIids( &impl_from_ICompositor4(iface)->ICompositor_iface, c, i );
}
static HRESULT WINAPI compositor4_GetRuntimeClassName( void *iface, HSTRING *n )
{
    return compositor_GetRuntimeClassName( &impl_from_ICompositor4(iface)->ICompositor_iface, n );
}
static HRESULT WINAPI compositor4_GetTrustLevel( void *iface, TrustLevel *l )
{
    return compositor_GetTrustLevel( &impl_from_ICompositor4(iface)->ICompositor_iface, l );
}
static HRESULT WINAPI compositor4_CreateColorGradientStop( void *iface, void **res )
{
    return comp5_stub_create( L"Windows.UI.Composition.CompositionColorGradientStop", res );
}
static HRESULT WINAPI compositor4_CreateColorGradientStopWithOffsetAndColor( void *iface, FLOAT offset, UINT32 color, void **res )
{
    return comp5_stub_create( L"Windows.UI.Composition.CompositionColorGradientStop", res );
}
static HRESULT WINAPI compositor4_CreateLinearGradientBrush( void *iface, void **res )
{
    struct compositor *impl = impl_from_ICompositor4( iface );
    HRESULT hr = comp5_stub_create( L"Windows.UI.Composition.CompositionLinearGradientBrush", res );
    if (SUCCEEDED(hr) && *res)
        ((struct comp5_stub *)*res)->compositor = impl;
    return hr;
}
static HRESULT WINAPI compositor4_CreateSpringScalarAnimation( void *iface, void **res )
{
    return comp5_stub_create( L"Windows.UI.Composition.SpringScalarNaturalMotionAnimation", res );
}
static HRESULT WINAPI compositor4_CreateSpringVector2Animation( void *iface, void **res )
{
    return comp5_stub_create( L"Windows.UI.Composition.SpringVector2NaturalMotionAnimation", res );
}
static HRESULT WINAPI compositor4_CreateSpringVector3Animation( void *iface, void **res )
{
    return comp5_stub_create( L"Windows.UI.Composition.SpringVector3NaturalMotionAnimation", res );
}
static const void *compositor4_vtbl[] =
{
    compositor4_QueryInterface,
    compositor4_AddRef,
    compositor4_Release,
    compositor4_GetIids,
    compositor4_GetRuntimeClassName,
    compositor4_GetTrustLevel,
    compositor4_CreateColorGradientStop,
    compositor4_CreateColorGradientStopWithOffsetAndColor,
    compositor4_CreateLinearGradientBrush,
    compositor4_CreateSpringScalarAnimation,
    compositor4_CreateSpringVector2Animation,
    compositor4_CreateSpringVector3Animation,
};

static inline struct compositor *impl_from_ICompositor5( void *iface )
{
    return CONTAINING_RECORD( iface, struct compositor, ICompositor5_iface );
}
static HRESULT WINAPI compositor5_QueryInterface( void *iface, REFIID iid, void **out )
{
    return compositor_QueryInterface( &impl_from_ICompositor5(iface)->ICompositor_iface, iid, out );
}
static ULONG WINAPI compositor5_AddRef( void *iface )
{
    return compositor_AddRef( &impl_from_ICompositor5(iface)->ICompositor_iface );
}
static ULONG WINAPI compositor5_Release( void *iface )
{
    return compositor_Release( &impl_from_ICompositor5(iface)->ICompositor_iface );
}
static HRESULT WINAPI compositor5_GetIids( void *iface, ULONG *c, IID **i )
{
    return compositor_GetIids( &impl_from_ICompositor5(iface)->ICompositor_iface, c, i );
}
static HRESULT WINAPI compositor5_GetRuntimeClassName( void *iface, HSTRING *n )
{
    return compositor_GetRuntimeClassName( &impl_from_ICompositor5(iface)->ICompositor_iface, n );
}
static HRESULT WINAPI compositor5_GetTrustLevel( void *iface, TrustLevel *l )
{
    return compositor_GetTrustLevel( &impl_from_ICompositor5(iface)->ICompositor_iface, l );
}
static HRESULT WINAPI compositor5_get_Comment( void *iface, HSTRING *val ) { return WindowsCreateString( L"", 0, val ); }
static HRESULT WINAPI compositor5_put_Comment( void *iface, HSTRING val ) { return S_OK; }
static HRESULT WINAPI compositor5_get_GlobalPlaybackRate( void *iface, FLOAT *val ) { if (val) *val = 1.0f; return S_OK; }
static HRESULT WINAPI compositor5_put_GlobalPlaybackRate( void *iface, FLOAT val ) { return S_OK; }
static HRESULT WINAPI compositor5_CreateAnimationController( void *iface, void **res ) { return comp5_stub_create( L"Windows.UI.Composition.AnimationController", res ); }
static HRESULT WINAPI compositor5_CreateBounceScalarAnimation( void *iface, void **res ) { return comp5_stub_create( L"Windows.UI.Composition.BounceScalarNaturalMotionAnimation", res ); }
static HRESULT WINAPI compositor5_CreateBounceVector2Animation( void *iface, void **res ) { return comp5_stub_create( L"Windows.UI.Composition.BounceVector2NaturalMotionAnimation", res ); }
static HRESULT WINAPI compositor5_CreateBounceVector3Animation( void *iface, void **res ) { return comp5_stub_create( L"Windows.UI.Composition.BounceVector3NaturalMotionAnimation", res ); }
static HRESULT WINAPI compositor5_CreateContainerShape( void *iface, void **res ) { return comp5_stub_create( L"Windows.UI.Composition.CompositionContainerShape", res ); }
static HRESULT WINAPI compositor5_CreateEllipseGeometry( void *iface, void **res ) { return comp5_stub_create( L"Windows.UI.Composition.CompositionEllipseGeometry", res ); }
static HRESULT WINAPI compositor5_CreateLineGeometry( void *iface, void **res ) { return comp5_stub_create( L"Windows.UI.Composition.CompositionLineGeometry", res ); }
static HRESULT WINAPI compositor5_CreatePathGeometry( void *iface, void **res ) { return comp5_stub_create( L"Windows.UI.Composition.CompositionPathGeometry", res ); }
static HRESULT WINAPI compositor5_CreatePathGeometryWithPath( void *iface, void *path, void **res )
{
    TRACE( "iface %p, path %p, res %p\n", iface, path, res );
    return comp5_stub_create( L"Windows.UI.Composition.CompositionPathGeometry", res );
}
static HRESULT WINAPI compositor5_CreatePathKeyFrameAnimation( void *iface, void **res )
{
    struct compositor *impl = impl_from_ICompositor5( iface );
    struct keyframe_animation *anim;
    TRACE( "iface %p, res %p\n", iface, res );
    if (!res) return E_POINTER;
    if (!(anim = keyframe_animation_create( impl, ANIM_PATH_KEY ))) return E_OUTOFMEMORY;
    *res = &anim->ICompositionAnimation_iface;
    return S_OK;
}
static HRESULT WINAPI compositor5_CreateRectangleGeometry( void *iface, void **res ) { return comp5_stub_create( L"Windows.UI.Composition.CompositionRectangleGeometry", res ); }
static HRESULT WINAPI compositor5_CreateRoundedRectangleGeometry( void *iface, void **res ) { return comp5_stub_create( L"Windows.UI.Composition.CompositionRoundedRectangleGeometry", res ); }
static HRESULT WINAPI compositor5_CreateShapeVisual( void *iface, void **res )
{
    struct compositor *impl = impl_from_ICompositor5( iface );
    struct visual *visual;
    TRACE( "iface %p, res %p\n", iface, res );
    if (!res) return E_POINTER;
    if (!(visual = shape_visual_create( impl ))) return E_OUTOFMEMORY;
    *res = &visual->IShapeVisual_iface;
    return S_OK;
}
static HRESULT WINAPI compositor5_CreateSpringScalarAnimation( void *iface, void **res ) { return comp5_stub_create( L"Windows.UI.Composition.SpringScalarNaturalMotionAnimation", res ); }
static HRESULT WINAPI compositor5_CreateSpringVector2Animation( void *iface, void **res ) { return comp5_stub_create( L"Windows.UI.Composition.SpringVector2NaturalMotionAnimation", res ); }
static HRESULT WINAPI compositor5_CreateSpringVector3Animation( void *iface, void **res ) { return comp5_stub_create( L"Windows.UI.Composition.SpringVector3NaturalMotionAnimation", res ); }
static HRESULT WINAPI compositor5_CreateSpriteShape( void *iface, void **res ) { return comp5_stub_create( L"Windows.UI.Composition.CompositionSpriteShape", res ); }
static HRESULT WINAPI compositor5_CreateSpriteShapeWithGeometry( void *iface, void *geom, void **res )
{
    TRACE( "iface %p, geom %p, res %p\n", iface, geom, res );
    return comp5_stub_create( L"Windows.UI.Composition.CompositionSpriteShape", res );
}
static HRESULT WINAPI compositor5_CreateViewBox( void *iface, void **res ) { return comp5_stub_create( L"Windows.UI.Composition.CompositionViewBox", res ); }
static HRESULT WINAPI compositor5_RequestCommitAsync( void *iface, void **res ) { return E_NOTIMPL; }

static const void *compositor5_vtbl[] =
{
    compositor5_QueryInterface,                /* 0 */
    compositor5_AddRef,                        /* 1 */
    compositor5_Release,                       /* 2 */
    compositor5_GetIids,                       /* 3 */
    compositor5_GetRuntimeClassName,           /* 4 */
    compositor5_GetTrustLevel,                 /* 5 */
    compositor5_get_Comment,                   /* 6 */
    compositor5_put_Comment,                   /* 7 */
    compositor5_get_GlobalPlaybackRate,        /* 8 */
    compositor5_put_GlobalPlaybackRate,        /* 9 */
    compositor5_CreateBounceScalarAnimation,   /* 10 */
    compositor5_CreateBounceVector2Animation,  /* 11 */
    compositor5_CreateBounceVector3Animation,  /* 12 */
    compositor5_CreateContainerShape,          /* 13 */
    compositor5_CreateEllipseGeometry,         /* 14 */
    compositor5_CreateLineGeometry,            /* 15 */
    compositor5_CreatePathGeometry,            /* 16 */
    compositor5_CreatePathGeometryWithPath,    /* 17 */
    compositor5_CreatePathKeyFrameAnimation,   /* 18 */
    compositor5_CreateRectangleGeometry,       /* 19 */
    compositor5_CreateRoundedRectangleGeometry,/* 20 */
    compositor5_CreateShapeVisual,             /* 21 */
    compositor5_CreateSpriteShape,             /* 22 */
    compositor5_CreateSpriteShapeWithGeometry, /* 23 */
    compositor5_CreateViewBox,                 /* 24 */
    compositor5_RequestCommitAsync,            /* 25 */
};

/*
 * ICompositor6 {7a38b2bd-cec8-4eeb-830f-d8d07aedebc3} (UniversalApiContract
 * 7.0, Windows 10 1809) is the Compositor slice the WinUI CompositionAnimations
 * use: Compositor::CreateGeometricClip(+WithGeometry), CreateRedirectVisual
 * (+WithSourceVisual) and CreateBooleanKeyFrameAnimation. It is not in
 * include/windows.ui.composition.idl, so, like ICompositor5, it is answered
 * through a struct IInspectable plus a function-pointer vtable. Slots after the
 * IInspectable prologue follow the idl order of the real
 * Windows.UI.Composition.ICompositor6.
 */

static inline struct compositor *impl_from_ICompositor6( void *iface )
{
    return CONTAINING_RECORD( iface, struct compositor, ICompositor6_iface );
}
static HRESULT WINAPI compositor6_QueryInterface( void *iface, REFIID iid, void **out )
{
    return compositor_QueryInterface( &impl_from_ICompositor6(iface)->ICompositor_iface, iid, out );
}
static ULONG WINAPI compositor6_AddRef( void *iface )
{
    return compositor_AddRef( &impl_from_ICompositor6(iface)->ICompositor_iface );
}
static ULONG WINAPI compositor6_Release( void *iface )
{
    return compositor_Release( &impl_from_ICompositor6(iface)->ICompositor_iface );
}
static HRESULT WINAPI compositor6_GetIids( void *iface, ULONG *c, IID **i )
{
    return compositor_GetIids( &impl_from_ICompositor6(iface)->ICompositor_iface, c, i );
}
static HRESULT WINAPI compositor6_GetRuntimeClassName( void *iface, HSTRING *n )
{
    return compositor_GetRuntimeClassName( &impl_from_ICompositor6(iface)->ICompositor_iface, n );
}
static HRESULT WINAPI compositor6_GetTrustLevel( void *iface, TrustLevel *l )
{
    return compositor_GetTrustLevel( &impl_from_ICompositor6(iface)->ICompositor_iface, l );
}

static HRESULT WINAPI compositor6_CreateGeometricClip( void *iface, void **res )
{
    struct compositor *impl = impl_from_ICompositor6( iface );
    struct geometric_clip *clip;

    TRACE( "iface %p, res %p.\n", iface, res );

    if (!res) return E_POINTER;
    if (!(clip = geometric_clip_create( impl, NULL ))) return E_OUTOFMEMORY;
    *res = &clip->ICompositionGeometricClip_iface;
    return S_OK;
}

static HRESULT WINAPI compositor6_CreateGeometricClipWithGeometry( void *iface, void *geometry, void **res )
{
    struct compositor *impl = impl_from_ICompositor6( iface );
    struct geometric_clip *clip;

    TRACE( "iface %p, geometry %p, res %p.\n", iface, geometry, res );

    if (!res) return E_POINTER;
    if (!(clip = geometric_clip_create( impl, geometry ))) return E_OUTOFMEMORY;
    *res = &clip->ICompositionGeometricClip_iface;
    return S_OK;
}

static HRESULT WINAPI compositor6_CreateRedirectVisual( void *iface, void **res )
{
    struct compositor *impl = impl_from_ICompositor6( iface );
    struct visual *visual;

    TRACE( "iface %p, res %p.\n", iface, res );

    if (!res) return E_POINTER;
    if (!(visual = container_visual_create( impl ))) return E_OUTOFMEMORY;
    *res = &visual->IRedirectVisual_iface;
    return S_OK;
}

static HRESULT WINAPI compositor6_CreateRedirectVisualWithSourceVisual( void *iface, void *source, void **res )
{
    struct compositor *impl = impl_from_ICompositor6( iface );
    struct visual *visual;

    TRACE( "iface %p, source %p, res %p.\n", iface, source, res );

    if (!res) return E_POINTER;
    if (!(visual = container_visual_create( impl ))) return E_OUTOFMEMORY;
    if (source)
    {
        IInspectable_AddRef( (IInspectable *)source );
        visual->redirect_source = source;
    }
    *res = &visual->IRedirectVisual_iface;
    return S_OK;
}

static HRESULT WINAPI compositor6_CreateBooleanKeyFrameAnimation( void *iface, void **res )
{
    struct compositor *impl = impl_from_ICompositor6( iface );
    struct keyframe_animation *anim;

    TRACE( "iface %p, res %p.\n", iface, res );

    if (!res) return E_POINTER;
    if (!(anim = keyframe_animation_create( impl, ANIM_BOOLEAN_KEY ))) return E_OUTOFMEMORY;
    *res = &anim->IBooleanKeyFrameAnimation_iface;
    return S_OK;
}

static const void *compositor6_vtbl[] =
{
    compositor6_QueryInterface,
    compositor6_AddRef,
    compositor6_Release,
    compositor6_GetIids,
    compositor6_GetRuntimeClassName,
    compositor6_GetTrustLevel,
    compositor6_CreateGeometricClip,
    compositor6_CreateGeometricClipWithGeometry,
    compositor6_CreateRedirectVisual,
    compositor6_CreateRedirectVisualWithSourceVisual,
    compositor6_CreateBooleanKeyFrameAnimation,
};

static inline struct compositor *impl_from_ICompositor2( void *iface )
{
    return CONTAINING_RECORD( iface, struct compositor, ICompositor2_iface );
}
static HRESULT WINAPI compositor2_QueryInterface( void *iface, REFIID iid, void **out )
{
    return compositor_QueryInterface( &impl_from_ICompositor2(iface)->ICompositor_iface, iid, out );
}
static ULONG WINAPI compositor2_AddRef( void *iface )
{
    return compositor_AddRef( &impl_from_ICompositor2(iface)->ICompositor_iface );
}
static ULONG WINAPI compositor2_Release( void *iface )
{
    return compositor_Release( &impl_from_ICompositor2(iface)->ICompositor_iface );
}
static HRESULT WINAPI compositor2_GetIids( void *iface, ULONG *c, IID **i )
{
    return compositor_GetIids( &impl_from_ICompositor2(iface)->ICompositor_iface, c, i );
}
static HRESULT WINAPI compositor2_GetRuntimeClassName( void *iface, HSTRING *n )
{
    return compositor_GetRuntimeClassName( &impl_from_ICompositor2(iface)->ICompositor_iface, n );
}
static HRESULT WINAPI compositor2_GetTrustLevel( void *iface, TrustLevel *l )
{
    return compositor_GetTrustLevel( &impl_from_ICompositor2(iface)->ICompositor_iface, l );
}
static HRESULT WINAPI compositor2_CreateAmbientLight( void *iface, void **res )
{
    return comp5_stub_create( L"Windows.UI.Composition.AmbientLight", res );
}
static HRESULT WINAPI compositor2_CreateAnimationGroup( void *iface, void **res )
{
    return comp5_stub_create( L"Windows.UI.Composition.CompositionAnimationGroup", res );
}
static HRESULT WINAPI compositor2_CreateBackdropBrush( void *iface, void **res )
{
    return comp5_stub_create( L"Windows.UI.Composition.CompositionBackdropBrush", res );
}
static HRESULT WINAPI compositor2_CreateDistantLight( void *iface, void **res )
{
    return comp5_stub_create( L"Windows.UI.Composition.DistantLight", res );
}
static HRESULT WINAPI compositor2_CreateDropShadow( void *iface, void **res )
{
    return comp5_stub_create( L"Windows.UI.Composition.DropShadow", res );
}
static HRESULT WINAPI compositor2_CreateImplicitAnimationCollection( void *iface, void **res )
{
    return implicit_anim_collection_create( impl_from_ICompositor2( iface ), res );
}
static HRESULT WINAPI compositor2_CreateLayerVisual( void *iface, void **res )
{
    struct compositor *impl = impl_from_ICompositor2( iface );
    struct IContainerVisual *cv = NULL;
    HRESULT hr;
    if (!res) return E_POINTER;
    *res = NULL;
    hr = compositor_CreateContainerVisual( &impl->ICompositor_iface, &cv );
    if (SUCCEEDED(hr) && cv) *res = cv;
    return hr;
}
static HRESULT WINAPI compositor2_CreateMaskBrush( void *iface, void **res )
{
    struct compositor *impl = impl_from_ICompositor2( iface );
    struct mask_brush *brush;

    if (!res) return E_POINTER;
    if (!(brush = mask_brush_create( impl ))) return E_OUTOFMEMORY;
    *res = &brush->ICompositionMaskBrush_iface;
    return S_OK;
}
static HRESULT WINAPI compositor2_CreateNineGridBrush( void *iface, void **res )
{
    return comp5_stub_create( L"Windows.UI.Composition.CompositionNineGridBrush", res );
}
static HRESULT WINAPI compositor2_CreatePointLight( void *iface, void **res )
{
    return comp5_stub_create( L"Windows.UI.Composition.PointLight", res );
}
static HRESULT WINAPI compositor2_CreateSpotLight( void *iface, void **res )
{
    return comp5_stub_create( L"Windows.UI.Composition.SpotLight", res );
}
static HRESULT WINAPI compositor2_CreateStepEasingFunction( void *iface, void **res )
{
    return comp5_stub_create( L"Windows.UI.Composition.StepEasingFunction", res );
}
static HRESULT WINAPI compositor2_CreateStepEasingFunctionWithStepCount( void *iface, INT32 stepCount, void **res )
{
    return comp5_stub_create( L"Windows.UI.Composition.StepEasingFunction", res );
}
static const void *compositor2_vtbl[] =
{
    compositor2_QueryInterface,
    compositor2_AddRef,
    compositor2_Release,
    compositor2_GetIids,
    compositor2_GetRuntimeClassName,
    compositor2_GetTrustLevel,
    compositor2_CreateAmbientLight,
    compositor2_CreateAnimationGroup,
    compositor2_CreateBackdropBrush,
    compositor2_CreateDistantLight,
    compositor2_CreateDropShadow,
    compositor2_CreateImplicitAnimationCollection,
    compositor2_CreateLayerVisual,
    compositor2_CreateMaskBrush,
    compositor2_CreateNineGridBrush,
    compositor2_CreatePointLight,
    compositor2_CreateSpotLight,
    compositor2_CreateStepEasingFunction,
    compositor2_CreateStepEasingFunctionWithStepCount,
};

static inline struct compositor *impl_from_ICompositorWithVisualSurface( void *iface )
{
    return CONTAINING_RECORD( iface, struct compositor, ICompositorWithVisualSurface_iface );
}
static HRESULT WINAPI comp_vs_QueryInterface( void *iface, REFIID iid, void **out )
{
    return compositor_QueryInterface( &impl_from_ICompositorWithVisualSurface(iface)->ICompositor_iface, iid, out );
}
static ULONG WINAPI comp_vs_AddRef( void *iface )
{
    return compositor_AddRef( &impl_from_ICompositorWithVisualSurface(iface)->ICompositor_iface );
}
static ULONG WINAPI comp_vs_Release( void *iface )
{
    return compositor_Release( &impl_from_ICompositorWithVisualSurface(iface)->ICompositor_iface );
}
static HRESULT WINAPI comp_vs_GetIids( void *iface, ULONG *c, IID **i )
{
    return compositor_GetIids( &impl_from_ICompositorWithVisualSurface(iface)->ICompositor_iface, c, i );
}
static HRESULT WINAPI comp_vs_GetRuntimeClassName( void *iface, HSTRING *n )
{
    return compositor_GetRuntimeClassName( &impl_from_ICompositorWithVisualSurface(iface)->ICompositor_iface, n );
}
static HRESULT WINAPI comp_vs_GetTrustLevel( void *iface, TrustLevel *l )
{
    return compositor_GetTrustLevel( &impl_from_ICompositorWithVisualSurface(iface)->ICompositor_iface, l );
}
static HRESULT WINAPI comp_vs_CreateVisualSurface( void *iface, void **res )
{
    return comp5_stub_create( L"Windows.UI.Composition.CompositionVisualSurface", res );
}
static const void *compositor_vs_vtbl[] =
{
    comp_vs_QueryInterface,
    comp_vs_AddRef,
    comp_vs_Release,
    comp_vs_GetIids,
    comp_vs_GetRuntimeClassName,
    comp_vs_GetTrustLevel,
    comp_vs_CreateVisualSurface,
};

static inline struct compositor *impl_from_ICompositorWithBlurredWallpaper( void *iface )
{
    return CONTAINING_RECORD( iface, struct compositor, ICompositorWithBlurredWallpaper_iface );
}
static HRESULT WINAPI comp_bw_QueryInterface( void *iface, REFIID iid, void **out )
{
    return compositor_QueryInterface( &impl_from_ICompositorWithBlurredWallpaper(iface)->ICompositor_iface, iid, out );
}
static ULONG WINAPI comp_bw_AddRef( void *iface )
{
    return compositor_AddRef( &impl_from_ICompositorWithBlurredWallpaper(iface)->ICompositor_iface );
}
static ULONG WINAPI comp_bw_Release( void *iface )
{
    return compositor_Release( &impl_from_ICompositorWithBlurredWallpaper(iface)->ICompositor_iface );
}
static HRESULT WINAPI comp_bw_GetIids( void *iface, ULONG *c, IID **i )
{
    return compositor_GetIids( &impl_from_ICompositorWithBlurredWallpaper(iface)->ICompositor_iface, c, i );
}
static HRESULT WINAPI comp_bw_GetRuntimeClassName( void *iface, HSTRING *n )
{
    return compositor_GetRuntimeClassName( &impl_from_ICompositorWithBlurredWallpaper(iface)->ICompositor_iface, n );
}
static HRESULT WINAPI comp_bw_GetTrustLevel( void *iface, TrustLevel *l )
{
    return compositor_GetTrustLevel( &impl_from_ICompositorWithBlurredWallpaper(iface)->ICompositor_iface, l );
}
static HRESULT WINAPI comp_bw_TryCreateBlurredWallpaperBackdropBrush( void *iface, void **res )
{
    return comp5_stub_create( L"Windows.UI.Composition.CompositionBackdropBrush", res );
}
static const void *compositor_bw_vtbl[] =
{
    comp_bw_QueryInterface,
    comp_bw_AddRef,
    comp_bw_Release,
    comp_bw_GetIids,
    comp_bw_GetRuntimeClassName,
    comp_bw_GetTrustLevel,
    comp_bw_TryCreateBlurredWallpaperBackdropBrush,
};

static inline struct compositor *impl_from_ICompositorPartner( void *iface )
{
    return CONTAINING_RECORD( iface, struct compositor, ICompositorPartner_iface );
}
static HRESULT WINAPI cpartner_QueryInterface( void *iface, REFIID iid, void **out )
{
    return compositor_QueryInterface( &impl_from_ICompositorPartner(iface)->ICompositor_iface, iid, out );
}
static ULONG WINAPI cpartner_AddRef( void *iface )
{
    return compositor_AddRef( &impl_from_ICompositorPartner(iface)->ICompositor_iface );
}
static ULONG WINAPI cpartner_Release( void *iface )
{
    return compositor_Release( &impl_from_ICompositorPartner(iface)->ICompositor_iface );
}
static HRESULT WINAPI cpartner_CreateSharedVisual( void *iface, void *arg1, void *arg2 )
{
    FIXME( "iface %p, arg1 %p, arg2 %p stub!\n", iface, arg1, arg2 );
    return E_NOTIMPL;
}
static HRESULT WINAPI cpartner_OpenSharedResourceHandle( void *iface, void *obj, HANDLE *handle )
{
    if (!handle) return E_POINTER;
    *handle = CreateEventW( NULL, FALSE, FALSE, NULL );
    return *handle ? S_OK : HRESULT_FROM_WIN32( GetLastError() );
}
static HRESULT WINAPI cpartner_OpenSharedTargetFromHandle( void *iface, HANDLE handle, void **target_out )
{
    FIXME( "iface %p, handle %p, target_out %p stub!\n", iface, handle, target_out );
    if (target_out) *target_out = NULL;
    return E_NOTIMPL;
}
static HRESULT WINAPI cpartner_noop( void *iface, ... ) { return S_OK; }

static const void *const cpartner_vtbl[16] =
{
    cpartner_QueryInterface,             /* 0 */
    cpartner_AddRef,                     /* 1 */
    cpartner_Release,                    /* 2 */
    cpartner_noop,                       /* 3 */
    cpartner_noop,                       /* 4 */
    cpartner_CreateSharedVisual,         /* 5 (+0x28) */
    cpartner_noop,                       /* 6 */
    cpartner_noop,                       /* 7 */
    cpartner_OpenSharedResourceHandle,   /* 8 (+0x40) */
    cpartner_OpenSharedTargetFromHandle, /* 9 (+0x48) */
    [10 ... 15] = cpartner_noop,
};

struct compositor *compositor_create( void )
{
    struct compositor *impl;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return NULL;
    impl->ICompositor_iface.lpVtbl = &compositor_vtbl;
    impl->ICompositor2_iface.lpVtbl = (const struct IInspectableVtbl *)compositor2_vtbl;
    impl->ICompositorWithVisualSurface_iface.lpVtbl = (const struct IInspectableVtbl *)compositor_vs_vtbl;
    impl->ICompositorWithBlurredWallpaper_iface.lpVtbl = (const struct IInspectableVtbl *)compositor_bw_vtbl;
    impl->ICompositor4_iface.lpVtbl = (const struct IInspectableVtbl *)compositor4_vtbl;
    impl->ICompositor5_iface.lpVtbl = (const struct IInspectableVtbl *)compositor5_vtbl;
    impl->ICompositor6_iface.lpVtbl = (const struct IInspectableVtbl *)compositor6_vtbl;
    impl->ICompositorInterop_iface.lpVtbl = &cinterop_vtbl;
    impl->ICompositorDesktopInterop_iface.lpVtbl = &cdesktop_vtbl;
    impl->ICompositorPartner_iface.lpVtbl = (const struct IInspectableVtbl *)cpartner_vtbl;
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
