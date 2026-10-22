/* WinRT Windows.UI.Composition Implementation
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

#ifndef __WINE_WINDOWS_UI_COMPOSITION_PRIVATE_H
#define __WINE_WINDOWS_UI_COMPOSITION_PRIVATE_H

#include <stdarg.h>
#include <stddef.h>

#define COBJMACROS
#include "windef.h"
#include "winbase.h"
#include "winstring.h"

#include "activation.h"
#include "dcomp.h"
#include "dxgi.h"

#define WIDL_using_Windows_Foundation
#include "windows.foundation.h"
#define WIDL_using_Windows_Foundation_Collections
#include "windows.foundation.collections.h"
#define WIDL_using_Windows_Foundation_Numerics
#include "windows.foundation.numerics.h"
#define WIDL_using_Windows_Graphics_DirectX
#include "windows.graphics.directx.h"
#define WIDL_using_Windows_Graphics_Effects
#include "windows.graphics.effects.h"
#define WIDL_using_Windows_UI
#include "windows.ui.h"
#define WIDL_using_Windows_UI_Core
#include "windows.ui.core.h"
#define WIDL_using_Windows_UI_Composition
#include "windows.ui.composition.h"
#define WIDL_using_Windows_UI_Composition_Interop
#include "windows.ui.composition.interop.h"

#include "wine/debug.h"

/*
 * WinRT object layout convention used throughout this module:
 *
 * Every runtimeclass object embeds one vtable struct per supported WinRT
 * interface, named <Interface>_iface, and shares a single reference count.
 * The object's default interface implements the full IInspectable prologue
 * (QueryInterface/AddRef/Release/GetIids/GetRuntimeClassName/GetTrustLevel);
 * the prologue of every other embedded interface delegates to the default
 * interface through DEFINE_INHERITED_PROLOGUE() below.
 */
#define DEFINE_INHERITED_PROLOGUE( pfx, iface_type, impl_type, base_iface )                        \
    static inline impl_type *impl_from_##pfx( iface_type *iface )                                  \
    {                                                                                              \
        return CONTAINING_RECORD( iface, impl_type, iface_type##_iface );                          \
    }                                                                                              \
    static HRESULT WINAPI pfx##_QueryInterface( iface_type *iface, REFIID iid, void **out )        \
    {                                                                                              \
        impl_type *impl = impl_from_##pfx( iface );                                                \
        return IInspectable_QueryInterface( (IInspectable *)&impl->base_iface, iid, out );         \
    }                                                                                              \
    static ULONG WINAPI pfx##_AddRef( iface_type *iface )                                          \
    {                                                                                              \
        impl_type *impl = impl_from_##pfx( iface );                                                \
        return IInspectable_AddRef( (IInspectable *)&impl->base_iface );                           \
    }                                                                                              \
    static ULONG WINAPI pfx##_Release( iface_type *iface )                                         \
    {                                                                                              \
        impl_type *impl = impl_from_##pfx( iface );                                                \
        return IInspectable_Release( (IInspectable *)&impl->base_iface );                          \
    }                                                                                              \
    static HRESULT WINAPI pfx##_GetIids( iface_type *iface, ULONG *iid_count, IID **iids )         \
    {                                                                                              \
        impl_type *impl = impl_from_##pfx( iface );                                                \
        return IInspectable_GetIids( (IInspectable *)&impl->base_iface, iid_count, iids );         \
    }                                                                                              \
    static HRESULT WINAPI pfx##_GetRuntimeClassName( iface_type *iface, HSTRING *class_name )      \
    {                                                                                              \
        impl_type *impl = impl_from_##pfx( iface );                                                \
        return IInspectable_GetRuntimeClassName( (IInspectable *)&impl->base_iface, class_name );  \
    }                                                                                              \
    static HRESULT WINAPI pfx##_GetTrustLevel( iface_type *iface, TrustLevel *trust_level )        \
    {                                                                                              \
        impl_type *impl = impl_from_##pfx( iface );                                                \
        return IInspectable_GetTrustLevel( (IInspectable *)&impl->base_iface, trust_level );       \
    }

struct compositor;
struct prop_set;
struct visual;
struct visual_collection;

struct compositor
{
    struct ICompositor ICompositor_iface;
    struct ICompositorInterop ICompositorInterop_iface;
    struct ICompositorDesktopInterop ICompositorDesktopInterop_iface;
    struct IClosable IClosable_iface;
    LONG ref;
    /* Lazily created DirectComposition bridge. */
    struct IDCompositionDevice2 *device2;
    struct IDCompositionDesktopDevice *desktop_device;
};

enum animation_kind
{
    ANIM_COLOR_KEY,
    ANIM_SCALAR_KEY,
    ANIM_VECTOR2_KEY,
    ANIM_VECTOR3_KEY,
    ANIM_VECTOR4_KEY,
    ANIM_QUATERNION_KEY,
};

struct keyframe_animation
{
    struct ICompositionAnimation ICompositionAnimation_iface;
    struct ICompositionAnimationBase ICompositionAnimationBase_iface;
    struct IColorKeyFrameAnimation IColorKeyFrameAnimation_iface;
    struct IScalarKeyFrameAnimation IScalarKeyFrameAnimation_iface;
    struct IVector2KeyFrameAnimation IVector2KeyFrameAnimation_iface;
    struct IVector3KeyFrameAnimation IVector3KeyFrameAnimation_iface;
    struct IVector4KeyFrameAnimation IVector4KeyFrameAnimation_iface;
    struct IQuaternionKeyFrameAnimation IQuaternionKeyFrameAnimation_iface;
    struct ICompositionObject ICompositionObject_iface;
    struct IClosable IClosable_iface;
    LONG ref;
    struct compositor *compositor;
    struct prop_set *properties;
    enum animation_kind kind;
    struct TimeSpan duration;
    struct TimeSpan delay;
    FLOAT normalized_delay;
    INT32 iteration_count;
};

struct expression_animation
{
    struct ICompositionAnimation ICompositionAnimation_iface;
    struct ICompositionAnimationBase ICompositionAnimationBase_iface;
    struct IExpressionAnimation IExpressionAnimation_iface;
    struct ICompositionObject ICompositionObject_iface;
    struct IClosable IClosable_iface;
    LONG ref;
    struct compositor *compositor;
    struct prop_set *properties;
    HSTRING expression;
};

struct easing_function
{
    struct ICompositionEasingFunction ICompositionEasingFunction_iface;
    struct ICubicBezierEasingFunction ICubicBezierEasingFunction_iface;
    struct ILinearEasingFunction ILinearEasingFunction_iface;
    struct ICompositionObject ICompositionObject_iface;
    struct IClosable IClosable_iface;
    LONG ref;
    struct compositor *compositor;
    struct prop_set *properties;
    BOOL is_cubic;
    Vector2 control_point1, control_point2;
};

struct inset_clip
{
    struct ICompositionClip ICompositionClip_iface;
    struct IInsetClip IInsetClip_iface;
    struct ICompositionObject ICompositionObject_iface;
    struct IClosable IClosable_iface;
    LONG ref;
    struct compositor *compositor;
    struct prop_set *properties;
    FLOAT left, top, right, bottom;
};

struct color_brush
{
    struct ICompositionBrush ICompositionBrush_iface;
    struct ICompositionColorBrush ICompositionColorBrush_iface;
    struct ICompositionObject ICompositionObject_iface;
    struct IClosable IClosable_iface;
    LONG ref;
    struct compositor *compositor;
    struct prop_set *properties;
    Color color;
};

struct surface_brush
{
    struct ICompositionBrush ICompositionBrush_iface;
    struct ICompositionSurfaceBrush ICompositionSurfaceBrush_iface;
    struct ICompositionObject ICompositionObject_iface;
    struct IClosable IClosable_iface;
    LONG ref;
    struct compositor *compositor;
    struct prop_set *properties;
    struct ICompositionSurface *surface;
    FLOAT horizontal_alignment_ratio, vertical_alignment_ratio;
    enum CompositionStretch stretch;
    enum CompositionBitmapInterpolationMode interpolation;
};

struct effect_brush
{
    struct ICompositionBrush ICompositionBrush_iface;
    struct ICompositionEffectBrush ICompositionEffectBrush_iface;
    struct ICompositionObject ICompositionObject_iface;
    struct IClosable IClosable_iface;
    LONG ref;
    struct compositor *compositor;
    struct prop_set *properties;
    struct effect_source
    {
        WCHAR *name;
        struct ICompositionBrush *brush;
    } *sources;
    unsigned int source_count, source_capacity;
};

struct effect_factory
{
    struct ICompositionEffectFactory ICompositionEffectFactory_iface;
    struct ICompositionObject ICompositionObject_iface;
    struct IClosable IClosable_iface;
    LONG ref;
    struct compositor *compositor;
    struct prop_set *properties;
    IInspectable *effect;
    struct __FIIterable_1_HSTRING *string_parameters;
};

struct composition_target
{
    struct ICompositionTarget ICompositionTarget_iface;
    struct ICompositionObject ICompositionObject_iface;
    struct IClosable IClosable_iface;
    struct IDesktopWindowTargetInterop IDesktopWindowTargetInterop_iface;
    LONG ref;
    struct compositor *compositor;
    struct prop_set *properties;
    struct visual *root;
    struct IDCompositionTarget *dcomp_target;
    HWND hwnd;
};

struct graphics_device
{
    struct ICompositionGraphicsDevice ICompositionGraphicsDevice_iface;
    struct ICompositionObject ICompositionObject_iface;
    struct IClosable IClosable_iface;
    LONG ref;
    struct compositor *compositor;
    struct prop_set *properties;
    struct IDCompositionDevice2 *dcomp_device;
};

struct composition_surface
{
    struct ICompositionSurface ICompositionSurface_iface;
    struct ICompositionObject ICompositionObject_iface;
    struct IClosable IClosable_iface;
    LONG ref;
    struct compositor *compositor;
    struct prop_set *properties;
    struct IUnknown *dcomp_surface;
    struct IDXGISwapChain *swap_chain;
};

struct scoped_batch
{
    struct ICompositionScopedBatch ICompositionScopedBatch_iface;
    struct ICompositionObject ICompositionObject_iface;
    struct IClosable IClosable_iface;
    LONG ref;
    struct compositor *compositor;
    struct prop_set *properties;
    BOOL active, ended;
};

struct commit_batch
{
    struct ICompositionCommitBatch ICompositionCommitBatch_iface;
    struct ICompositionObject ICompositionObject_iface;
    struct IClosable IClosable_iface;
    LONG ref;
    struct compositor *compositor;
    struct prop_set *properties;
    BOOL active, ended;
};

struct visual
{
    struct IVisual IVisual_iface;
    struct IContainerVisual IContainerVisual_iface;
    struct ISpriteVisual ISpriteVisual_iface;
    struct ICompositionObject ICompositionObject_iface;
    struct IClosable IClosable_iface;
    LONG ref;
    struct compositor *compositor;
    struct prop_set *properties;
    struct visual_collection *children;
    struct visual *parent;
    struct ICompositionBrush *brush;
    BOOL is_sprite;
    Vector2 anchor_point;
    enum CompositionBackfaceVisibility backface_visibility;
    enum CompositionBorderMode border_mode;
    Vector3 center_point;
    struct ICompositionClip *clip;
    enum CompositionCompositeMode composite_mode;
    BOOL is_visible;
    Vector3 offset;
    FLOAT opacity;
    Quaternion orientation;
    FLOAT rotation_angle;
    FLOAT rotation_angle_in_degrees;
    Vector3 rotation_axis;
    Vector3 scale;
    Vector2 size;
    Matrix4x4 transform_matrix;
};

struct visual_collection
{
    struct IVisualCollection IVisualCollection_iface;
    struct ICompositionObject ICompositionObject_iface;
    struct IClosable IClosable_iface;
    LONG ref;
    struct compositor *compositor;
    struct visual *owner;
    struct visual **items;
    unsigned int count, capacity;
};

/* Object constructors, defined in propset.c / visual.c / objects.c / compositor.c. */
struct compositor *compositor_create( void );
struct prop_set *prop_set_create( struct compositor *compositor );
struct visual *container_visual_create( struct compositor *compositor );
struct visual *sprite_visual_create( struct compositor *compositor );
struct keyframe_animation *keyframe_animation_create( struct compositor *compositor, enum animation_kind kind );
struct expression_animation *expression_animation_create( struct compositor *compositor, HSTRING expression );
struct easing_function *cubic_bezier_easing_create( struct compositor *compositor, Vector2 point1, Vector2 point2 );
struct easing_function *linear_easing_create( struct compositor *compositor );
struct inset_clip *inset_clip_create( struct compositor *compositor, FLOAT left, FLOAT top, FLOAT right, FLOAT bottom );
struct color_brush *color_brush_create( struct compositor *compositor, Color color );
struct surface_brush *surface_brush_create( struct compositor *compositor, struct ICompositionSurface *surface );
struct composition_target *composition_target_create( struct compositor *compositor, struct IDCompositionTarget *dcomp_target, HWND hwnd );
struct graphics_device *graphics_device_create( struct compositor *compositor, struct IDCompositionDevice2 *dcomp_device );
struct composition_surface *composition_surface_create( struct compositor *compositor,
        struct IUnknown *dcomp_surface, struct IDXGISwapChain *swap_chain );
struct scoped_batch *scoped_batch_create( struct compositor *compositor );
struct commit_batch *commit_batch_create( struct compositor *compositor );
struct effect_factory *effect_factory_create( struct compositor *compositor,
        IInspectable *effect, struct __FIIterable_1_HSTRING *string_parameters );

/* Cross-file accessors. */
struct ICompositionPropertySet *prop_set_get_iface( struct prop_set *set );
struct visual *visual_from_IVisual( struct IVisual *iface );

#endif /* __WINE_WINDOWS_UI_COMPOSITION_PRIVATE_H */
