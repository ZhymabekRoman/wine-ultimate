/*
 * DirectComposition transform, effect group and clip objects
 *
 * The IDCompositionDevice Create*Transform / CreateEffectGroup /
 * CreateRectangleClip entry points hand out these objects.  They are
 * parameter stores: the composition engine reads the recorded values when
 * the object is attached to a visual (IDCompositionVisual::SetTransform /
 * SetClip / SetEffect).  The WinAppSDK windowing bridge builds a
 * ScaleTransform while laying out the caption buttons, so the factories
 * must return real objects.
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

#define COBJMACROS

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "windef.h"
#include "winbase.h"
#include "winerror.h"
#include "unknwn.h"
#include "wine/debug.h"
#include "d2d1_3.h"

#include "dcomp_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(dcomp);

/* IDCompositionTransform / Transform3D / Effect base interface GUIDs
 * (the generated header exposes them only through C++ __declspec(uuid)). */
static const GUID GUID_IDCompositionTransform =
    {0x3ee707f3, 0xf8cb, 0x4dbc, {0xa5, 0x3f, 0x93, 0x75, 0x4a, 0xf4, 0x55, 0x54}};
static const GUID GUID_IDCompositionTransform3D =
    {0x2be42e71, 0xc5c1, 0x4ff4, {0xaa, 0xbf, 0x16, 0xf2, 0x64, 0x2d, 0x86, 0x01}};
static const GUID GUID_IDCompositionEffect =
    {0x8883610f, 0x659e, 0x4a3f, {0xa9, 0xf5, 0x14, 0x83, 0xff, 0x8e, 0x79, 0xe6}};

static HRESULT transform_QI( struct dcomp_transform *impl, void *this_iface, REFIID iid, void **out )
{
    TRACE( "iface %p, iid %s, out %p.\n", this_iface, debugstr_guid( iid ), out );

    if (!out) return E_POINTER;
    *out = NULL;

    if (IsEqualGUID( iid, &IID_IUnknown )
            || IsEqualGUID( iid, &GUID_IDCompositionTransform )
            || IsEqualGUID( iid, &GUID_IDCompositionTransform3D )
            || IsEqualGUID( iid, &GUID_IDCompositionEffect ))
    {
        *out = this_iface;
    }
    else switch (impl->kind)
    {
    case TRANSFORM_TRANSLATE:
        if (IsEqualGUID( iid, &IID_IDCompositionTranslateTransform )) *out = &impl->IDCompositionTranslateTransform_iface;
        break;
    case TRANSFORM_SCALE:
        if (IsEqualGUID( iid, &IID_IDCompositionScaleTransform )) *out = &impl->IDCompositionScaleTransform_iface;
        break;
    case TRANSFORM_ROTATE:
        if (IsEqualGUID( iid, &IID_IDCompositionRotateTransform )) *out = &impl->IDCompositionRotateTransform_iface;
        break;
    case TRANSFORM_SKEW:
        if (IsEqualGUID( iid, &IID_IDCompositionSkewTransform )) *out = &impl->IDCompositionSkewTransform_iface;
        break;
    case TRANSFORM_MATRIX:
        if (IsEqualGUID( iid, &IID_IDCompositionMatrixTransform )) *out = &impl->IDCompositionMatrixTransform_iface;
        break;
    case TRANSFORM_TRANSLATE_3D:
        if (IsEqualGUID( iid, &IID_IDCompositionTranslateTransform3D )) *out = &impl->IDCompositionTranslateTransform3D_iface;
        break;
    case TRANSFORM_SCALE_3D:
        if (IsEqualGUID( iid, &IID_IDCompositionScaleTransform3D )) *out = &impl->IDCompositionScaleTransform3D_iface;
        break;
    case TRANSFORM_ROTATE_3D:
        if (IsEqualGUID( iid, &IID_IDCompositionRotateTransform3D )) *out = &impl->IDCompositionRotateTransform3D_iface;
        break;
    case TRANSFORM_MATRIX_3D:
        if (IsEqualGUID( iid, &IID_IDCompositionMatrixTransform3D )) *out = &impl->IDCompositionMatrixTransform3D_iface;
        break;
    case OBJECT_EFFECT_GROUP:
        if (IsEqualGUID( iid, &IID_IDCompositionEffectGroup )) *out = &impl->IDCompositionEffectGroup_iface;
        break;
    case OBJECT_RECTANGLE_CLIP:
        if (IsEqualGUID( iid, &IID_IDCompositionRectangleClip )) *out = &impl->IDCompositionRectangleClip_iface;
        if (IsEqualGUID( iid, &IID_IDCompositionClip )) *out = &impl->IDCompositionRectangleClip_iface;
        break;
    default:
        break;
    }

    if (!*out)
    {
        FIXME( "%s no interface.\n", debugstr_guid( iid ) );
        return E_NOINTERFACE;
    }

    IUnknown_AddRef( (IUnknown *)*out );
    return S_OK;
}

static ULONG transform_addref( struct dcomp_transform *impl )
{
    return InterlockedIncrement( &impl->ref );
}

static ULONG transform_release( struct dcomp_transform *impl )
{
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        if (impl->transform3d) IDCompositionTransform3D_Release( impl->transform3d );
        free( impl );
    }
    return ref;
}

#define TRANSFORM_PROLOGUE( pfx, iface_type, member )                                             \
    static inline struct dcomp_transform *impl_from_##pfx( iface_type *iface )                     \
    {                                                                                              \
        return CONTAINING_RECORD( iface, struct dcomp_transform, member );                         \
    }                                                                                              \
    static HRESULT STDMETHODCALLTYPE pfx##_QueryInterface( iface_type *iface, REFIID iid, void **out ) \
    {                                                                                              \
        struct dcomp_transform *impl = impl_from_##pfx( iface );                                   \
        return transform_QI( impl, (void *)iface, iid, out );                                      \
    }                                                                                              \
    static ULONG STDMETHODCALLTYPE pfx##_AddRef( iface_type *iface )                               \
    {                                                                                              \
        return transform_addref( impl_from_##pfx( iface ) );                                       \
    }                                                                                              \
    static ULONG STDMETHODCALLTYPE pfx##_Release( iface_type *iface )                              \
    {                                                                                              \
        return transform_release( impl_from_##pfx( iface ) );                                      \
    }

#define TRANSFORM_SETTER( pfx, iface_type, name, field )                                          \
    static HRESULT STDMETHODCALLTYPE pfx##_##name( iface_type *iface, float value )                \
    {                                                                                              \
        struct dcomp_transform *impl = impl_from_##pfx( iface );                                   \
        TRACE( "iface %p, value %g.\n", iface, value );                                            \
        impl->field = value;                                                                       \
        return S_OK;                                                                               \
    }

#define TRANSFORM_ANIM_STUB( pfx, iface_type, name )                                              \
    static HRESULT STDMETHODCALLTYPE pfx##_##name( iface_type *iface,                              \
            IDCompositionAnimation *animation )                                                    \
    {                                                                                              \
        TRACE( "iface %p, animation %p.\n", iface, animation );                                    \
        return S_OK;                                                                               \
    }

/*
 * 2D transforms.
 */

TRANSFORM_PROLOGUE( tt, IDCompositionTranslateTransform, IDCompositionTranslateTransform_iface )
TRANSFORM_ANIM_STUB( tt, IDCompositionTranslateTransform, SetOffsetXAnimation )
TRANSFORM_SETTER( tt, IDCompositionTranslateTransform, SetOffsetX, offset_x )
TRANSFORM_ANIM_STUB( tt, IDCompositionTranslateTransform, SetOffsetYAnimation )
TRANSFORM_SETTER( tt, IDCompositionTranslateTransform, SetOffsetY, offset_y )

static const struct IDCompositionTranslateTransformVtbl translate_transform_vtbl =
{
    tt_QueryInterface,
    tt_AddRef,
    tt_Release,
    tt_SetOffsetXAnimation,
    tt_SetOffsetX,
    tt_SetOffsetYAnimation,
    tt_SetOffsetY,
};

TRANSFORM_PROLOGUE( st, IDCompositionScaleTransform, IDCompositionScaleTransform_iface )
TRANSFORM_ANIM_STUB( st, IDCompositionScaleTransform, SetScaleXAnimation )
TRANSFORM_SETTER( st, IDCompositionScaleTransform, SetScaleX, scale_x )
TRANSFORM_ANIM_STUB( st, IDCompositionScaleTransform, SetScaleYAnimation )
TRANSFORM_SETTER( st, IDCompositionScaleTransform, SetScaleY, scale_y )
TRANSFORM_ANIM_STUB( st, IDCompositionScaleTransform, SetCenterXAnimation )
TRANSFORM_SETTER( st, IDCompositionScaleTransform, SetCenterX, center_x )
TRANSFORM_ANIM_STUB( st, IDCompositionScaleTransform, SetCenterYAnimation )
TRANSFORM_SETTER( st, IDCompositionScaleTransform, SetCenterY, center_y )

static const struct IDCompositionScaleTransformVtbl scale_transform_vtbl =
{
    st_QueryInterface,
    st_AddRef,
    st_Release,
    st_SetScaleXAnimation,
    st_SetScaleX,
    st_SetScaleYAnimation,
    st_SetScaleY,
    st_SetCenterXAnimation,
    st_SetCenterX,
    st_SetCenterYAnimation,
    st_SetCenterY,
};

TRANSFORM_PROLOGUE( rt, IDCompositionRotateTransform, IDCompositionRotateTransform_iface )
TRANSFORM_ANIM_STUB( rt, IDCompositionRotateTransform, SetAngleAnimation )
TRANSFORM_SETTER( rt, IDCompositionRotateTransform, SetAngle, angle )
TRANSFORM_ANIM_STUB( rt, IDCompositionRotateTransform, SetCenterXAnimation )
TRANSFORM_SETTER( rt, IDCompositionRotateTransform, SetCenterX, center_x )
TRANSFORM_ANIM_STUB( rt, IDCompositionRotateTransform, SetCenterYAnimation )
TRANSFORM_SETTER( rt, IDCompositionRotateTransform, SetCenterY, center_y )

static const struct IDCompositionRotateTransformVtbl rotate_transform_vtbl =
{
    rt_QueryInterface,
    rt_AddRef,
    rt_Release,
    rt_SetAngleAnimation,
    rt_SetAngle,
    rt_SetCenterXAnimation,
    rt_SetCenterX,
    rt_SetCenterYAnimation,
    rt_SetCenterY,
};

TRANSFORM_PROLOGUE( kt, IDCompositionSkewTransform, IDCompositionSkewTransform_iface )
TRANSFORM_ANIM_STUB( kt, IDCompositionSkewTransform, SetAngleXAnimation )
TRANSFORM_SETTER( kt, IDCompositionSkewTransform, SetAngleX, angle_x )
TRANSFORM_ANIM_STUB( kt, IDCompositionSkewTransform, SetAngleYAnimation )
TRANSFORM_SETTER( kt, IDCompositionSkewTransform, SetAngleY, angle_y )
TRANSFORM_ANIM_STUB( kt, IDCompositionSkewTransform, SetCenterXAnimation )
TRANSFORM_SETTER( kt, IDCompositionSkewTransform, SetCenterX, center_x )
TRANSFORM_ANIM_STUB( kt, IDCompositionSkewTransform, SetCenterYAnimation )
TRANSFORM_SETTER( kt, IDCompositionSkewTransform, SetCenterY, center_y )

static const struct IDCompositionSkewTransformVtbl skew_transform_vtbl =
{
    kt_QueryInterface,
    kt_AddRef,
    kt_Release,
    kt_SetAngleXAnimation,
    kt_SetAngleX,
    kt_SetAngleYAnimation,
    kt_SetAngleY,
    kt_SetCenterXAnimation,
    kt_SetCenterX,
    kt_SetCenterYAnimation,
    kt_SetCenterY,
};

TRANSFORM_PROLOGUE( mt, IDCompositionMatrixTransform, IDCompositionMatrixTransform_iface )

static HRESULT STDMETHODCALLTYPE mt_SetMatrix( IDCompositionMatrixTransform *iface,
        const D2D_MATRIX_3X2_F *matrix )
{
    struct dcomp_transform *impl = impl_from_mt( iface );

    TRACE( "iface %p, matrix %p.\n", iface, matrix );

    if (!matrix) return E_INVALIDARG;
    impl->matrix = *matrix;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE mt_SetMatrixElementAnimation( IDCompositionMatrixTransform *iface,
        int row, int column, IDCompositionAnimation *animation )
{
    TRACE( "iface %p, row %d, column %d, animation %p.\n", iface, row, column, animation );

    if (row < 0 || row > 2 || column < 0 || column > 1) return E_INVALIDARG;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE mt_SetMatrixElement( IDCompositionMatrixTransform *iface,
        int row, int column, float value )
{
    struct dcomp_transform *impl = impl_from_mt( iface );
    float *m = (float *)&impl->matrix;

    TRACE( "iface %p, row %d, column %d, value %g.\n", iface, row, column, value );

    if (row < 0 || row > 2 || column < 0 || column > 1) return E_INVALIDARG;
    m[row * 2 + column] = value;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE mt_GetMatrix( IDCompositionMatrixTransform *iface,
        D2D_MATRIX_3X2_F *matrix )
{
    struct dcomp_transform *impl = impl_from_mt( iface );

    TRACE( "iface %p, matrix %p.\n", iface, matrix );

    if (!matrix) return E_POINTER;
    *matrix = impl->matrix;
    return S_OK;
}

static const struct IDCompositionMatrixTransformVtbl matrix_transform_vtbl =
{
    mt_QueryInterface,
    mt_AddRef,
    mt_Release,
    mt_SetMatrix,
    mt_SetMatrixElementAnimation,
    mt_SetMatrixElement,
    mt_GetMatrix,
};

/*
 * 3D transforms.
 */

TRANSFORM_PROLOGUE( tt3, IDCompositionTranslateTransform3D, IDCompositionTranslateTransform3D_iface )
TRANSFORM_ANIM_STUB( tt3, IDCompositionTranslateTransform3D, SetOffsetXAnimation )
TRANSFORM_SETTER( tt3, IDCompositionTranslateTransform3D, SetOffsetX, offset_x )
TRANSFORM_ANIM_STUB( tt3, IDCompositionTranslateTransform3D, SetOffsetYAnimation )
TRANSFORM_SETTER( tt3, IDCompositionTranslateTransform3D, SetOffsetY, offset_y )
TRANSFORM_ANIM_STUB( tt3, IDCompositionTranslateTransform3D, SetOffsetZAnimation )
TRANSFORM_SETTER( tt3, IDCompositionTranslateTransform3D, SetOffsetZ, offset_z )

static const struct IDCompositionTranslateTransform3DVtbl translate_transform3d_vtbl =
{
    tt3_QueryInterface,
    tt3_AddRef,
    tt3_Release,
    tt3_SetOffsetXAnimation,
    tt3_SetOffsetX,
    tt3_SetOffsetYAnimation,
    tt3_SetOffsetY,
    tt3_SetOffsetZAnimation,
    tt3_SetOffsetZ,
};

TRANSFORM_PROLOGUE( st3, IDCompositionScaleTransform3D, IDCompositionScaleTransform3D_iface )
TRANSFORM_ANIM_STUB( st3, IDCompositionScaleTransform3D, SetScaleXAnimation )
TRANSFORM_SETTER( st3, IDCompositionScaleTransform3D, SetScaleX, scale_x )
TRANSFORM_ANIM_STUB( st3, IDCompositionScaleTransform3D, SetScaleYAnimation )
TRANSFORM_SETTER( st3, IDCompositionScaleTransform3D, SetScaleY, scale_y )
TRANSFORM_ANIM_STUB( st3, IDCompositionScaleTransform3D, SetScaleZAnimation )
TRANSFORM_SETTER( st3, IDCompositionScaleTransform3D, SetScaleZ, scale_z )
TRANSFORM_ANIM_STUB( st3, IDCompositionScaleTransform3D, SetCenterXAnimation )
TRANSFORM_SETTER( st3, IDCompositionScaleTransform3D, SetCenterX, center_x )
TRANSFORM_ANIM_STUB( st3, IDCompositionScaleTransform3D, SetCenterYAnimation )
TRANSFORM_SETTER( st3, IDCompositionScaleTransform3D, SetCenterY, center_y )
TRANSFORM_ANIM_STUB( st3, IDCompositionScaleTransform3D, SetCenterZAnimation )
TRANSFORM_SETTER( st3, IDCompositionScaleTransform3D, SetCenterZ, center_z )

static const struct IDCompositionScaleTransform3DVtbl scale_transform3d_vtbl =
{
    st3_QueryInterface,
    st3_AddRef,
    st3_Release,
    st3_SetScaleXAnimation,
    st3_SetScaleX,
    st3_SetScaleYAnimation,
    st3_SetScaleY,
    st3_SetScaleZAnimation,
    st3_SetScaleZ,
    st3_SetCenterXAnimation,
    st3_SetCenterX,
    st3_SetCenterYAnimation,
    st3_SetCenterY,
    st3_SetCenterZAnimation,
    st3_SetCenterZ,
};

TRANSFORM_PROLOGUE( rt3, IDCompositionRotateTransform3D, IDCompositionRotateTransform3D_iface )
TRANSFORM_ANIM_STUB( rt3, IDCompositionRotateTransform3D, SetAngleAnimation )
TRANSFORM_SETTER( rt3, IDCompositionRotateTransform3D, SetAngle, angle )
TRANSFORM_ANIM_STUB( rt3, IDCompositionRotateTransform3D, SetAxisXAnimation )
TRANSFORM_SETTER( rt3, IDCompositionRotateTransform3D, SetAxisX, axis_x )
TRANSFORM_ANIM_STUB( rt3, IDCompositionRotateTransform3D, SetAxisYAnimation )
TRANSFORM_SETTER( rt3, IDCompositionRotateTransform3D, SetAxisY, axis_y )
TRANSFORM_ANIM_STUB( rt3, IDCompositionRotateTransform3D, SetAxisZAnimation )
TRANSFORM_SETTER( rt3, IDCompositionRotateTransform3D, SetAxisZ, axis_z )
TRANSFORM_ANIM_STUB( rt3, IDCompositionRotateTransform3D, SetCenterXAnimation )
TRANSFORM_SETTER( rt3, IDCompositionRotateTransform3D, SetCenterX, center_x )
TRANSFORM_ANIM_STUB( rt3, IDCompositionRotateTransform3D, SetCenterYAnimation )
TRANSFORM_SETTER( rt3, IDCompositionRotateTransform3D, SetCenterY, center_y )
TRANSFORM_ANIM_STUB( rt3, IDCompositionRotateTransform3D, SetCenterZAnimation )
TRANSFORM_SETTER( rt3, IDCompositionRotateTransform3D, SetCenterZ, center_z )

static const struct IDCompositionRotateTransform3DVtbl rotate_transform3d_vtbl =
{
    rt3_QueryInterface,
    rt3_AddRef,
    rt3_Release,
    rt3_SetAngleAnimation,
    rt3_SetAngle,
    rt3_SetAxisXAnimation,
    rt3_SetAxisX,
    rt3_SetAxisYAnimation,
    rt3_SetAxisY,
    rt3_SetAxisZAnimation,
    rt3_SetAxisZ,
    rt3_SetCenterXAnimation,
    rt3_SetCenterX,
    rt3_SetCenterYAnimation,
    rt3_SetCenterY,
    rt3_SetCenterZAnimation,
    rt3_SetCenterZ,
};

TRANSFORM_PROLOGUE( mt3, IDCompositionMatrixTransform3D, IDCompositionMatrixTransform3D_iface )

static HRESULT STDMETHODCALLTYPE mt3_SetMatrix( IDCompositionMatrixTransform3D *iface,
        const D3DMATRIX *matrix )
{
    struct dcomp_transform *impl = impl_from_mt3( iface );

    TRACE( "iface %p, matrix %p.\n", iface, matrix );

    if (!matrix) return E_INVALIDARG;
    impl->matrix3d = *matrix;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE mt3_SetMatrixElementAnimation( IDCompositionMatrixTransform3D *iface,
        int row, int column, IDCompositionAnimation *animation )
{
    TRACE( "iface %p, row %d, column %d, animation %p.\n", iface, row, column, animation );

    if (row < 0 || row > 3 || column < 0 || column > 3) return E_INVALIDARG;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE mt3_SetMatrixElement( IDCompositionMatrixTransform3D *iface,
        int row, int column, float value )
{
    struct dcomp_transform *impl = impl_from_mt3( iface );
    float *m = (float *)&impl->matrix3d;

    TRACE( "iface %p, row %d, column %d, value %g.\n", iface, row, column, value );

    if (row < 0 || row > 3 || column < 0 || column > 3) return E_INVALIDARG;
    m[row * 4 + column] = value;
    return S_OK;
}

static const struct IDCompositionMatrixTransform3DVtbl matrix_transform3d_vtbl =
{
    mt3_QueryInterface,
    mt3_AddRef,
    mt3_Release,
    mt3_SetMatrix,
    mt3_SetMatrixElementAnimation,
    mt3_SetMatrixElement,
};

/*
 * EffectGroup: opacity + 3D transform.  IDCompositionEffect is a bare
 * IUnknown marker in the DirectComposition contract.
 */

TRANSFORM_PROLOGUE( eg, IDCompositionEffectGroup, IDCompositionEffectGroup_iface )

static HRESULT STDMETHODCALLTYPE eg_SetOpacityAnimation( IDCompositionEffectGroup *iface,
        IDCompositionAnimation *animation )
{
    TRACE( "iface %p, animation %p.\n", iface, animation );
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE eg_SetOpacity( IDCompositionEffectGroup *iface, float opacity )
{
    struct dcomp_transform *impl = impl_from_eg( iface );

    TRACE( "iface %p, opacity %g.\n", iface, opacity );
    impl->opacity = opacity;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE eg_SetTransform3D( IDCompositionEffectGroup *iface,
        IDCompositionTransform3D *transform )
{
    struct dcomp_transform *impl = impl_from_eg( iface );

    TRACE( "iface %p, transform %p.\n", iface, transform );

    if (transform != impl->transform3d)
    {
        if (transform) IDCompositionTransform3D_AddRef( transform );
        if (impl->transform3d) IDCompositionTransform3D_Release( impl->transform3d );
        impl->transform3d = transform;
    }
    return S_OK;
}

static const struct IDCompositionEffectGroupVtbl effect_group_vtbl =
{
    eg_QueryInterface,
    eg_AddRef,
    eg_Release,
    eg_SetOpacityAnimation,
    eg_SetOpacity,
    eg_SetTransform3D,
};

/*
 * RectangleClip: IDCompositionEffectGroup + LTRB and per-corner radii.
 */

TRANSFORM_PROLOGUE( rc, IDCompositionRectangleClip, IDCompositionRectangleClip_iface )

#define CLIP_SETTER( name, field )                                                                \
    static HRESULT STDMETHODCALLTYPE rc_Set##name( IDCompositionRectangleClip *iface, float value )\
    {                                                                                              \
        struct dcomp_transform *impl = impl_from_rc( iface );                                      \
        TRACE( "iface %p, value %g.\n", iface, value );                                            \
        impl->field = value;                                                                       \
        return S_OK;                                                                               \
    }

#define CLIP_ANIM_STUB( name )                                                                    \
    static HRESULT STDMETHODCALLTYPE rc_Set##name##Animation( IDCompositionRectangleClip *iface,   \
            IDCompositionAnimation *animation )                                                    \
    {                                                                                              \
        TRACE( "iface %p, animation %p.\n", iface, animation );                                    \
        return S_OK;                                                                               \
    }

CLIP_ANIM_STUB( Left )
CLIP_SETTER( Left, clip_left )
CLIP_ANIM_STUB( Top )
CLIP_SETTER( Top, clip_top )
CLIP_ANIM_STUB( Right )
CLIP_SETTER( Right, clip_right )
CLIP_ANIM_STUB( Bottom )
CLIP_SETTER( Bottom, clip_bottom )
CLIP_ANIM_STUB( TopLeftRadiusX )
CLIP_SETTER( TopLeftRadiusX, clip_tl_radius_x )
CLIP_ANIM_STUB( TopLeftRadiusY )
CLIP_SETTER( TopLeftRadiusY, clip_tl_radius_y )
CLIP_ANIM_STUB( TopRightRadiusX )
CLIP_SETTER( TopRightRadiusX, clip_tr_radius_x )
CLIP_ANIM_STUB( TopRightRadiusY )
CLIP_SETTER( TopRightRadiusY, clip_tr_radius_y )
CLIP_ANIM_STUB( BottomLeftRadiusX )
CLIP_SETTER( BottomLeftRadiusX, clip_bl_radius_x )
CLIP_ANIM_STUB( BottomLeftRadiusY )
CLIP_SETTER( BottomLeftRadiusY, clip_bl_radius_y )
CLIP_ANIM_STUB( BottomRightRadiusX )
CLIP_SETTER( BottomRightRadiusX, clip_br_radius_x )
CLIP_ANIM_STUB( BottomRightRadiusY )
CLIP_SETTER( BottomRightRadiusY, clip_br_radius_y )

static const struct IDCompositionRectangleClipVtbl rectangle_clip_vtbl =
{
    rc_QueryInterface,
    rc_AddRef,
    rc_Release,
    rc_SetLeftAnimation,
    rc_SetLeft,
    rc_SetTopAnimation,
    rc_SetTop,
    rc_SetRightAnimation,
    rc_SetRight,
    rc_SetBottomAnimation,
    rc_SetBottom,
    rc_SetTopLeftRadiusXAnimation,
    rc_SetTopLeftRadiusX,
    rc_SetTopLeftRadiusYAnimation,
    rc_SetTopLeftRadiusY,
    rc_SetTopRightRadiusXAnimation,
    rc_SetTopRightRadiusX,
    rc_SetTopRightRadiusYAnimation,
    rc_SetTopRightRadiusY,
    rc_SetBottomLeftRadiusXAnimation,
    rc_SetBottomLeftRadiusX,
    rc_SetBottomLeftRadiusYAnimation,
    rc_SetBottomLeftRadiusY,
    rc_SetBottomRightRadiusXAnimation,
    rc_SetBottomRightRadiusX,
    rc_SetBottomRightRadiusYAnimation,
    rc_SetBottomRightRadiusY,
};

struct dcomp_transform *dcomp_transform_create( struct composition_device *device,
        enum dcomp_object_kind kind )
{
    struct dcomp_transform *impl;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return NULL;
    impl->ref = 1;
    impl->kind = kind;
    impl->device = device;
    impl->scale_x = impl->scale_y = impl->scale_z = 1.0f;
    impl->axis_x = 1.0f;
    impl->opacity = 1.0f;
    impl->matrix.m11 = impl->matrix.m22 = 1.0f;
    impl->matrix3d.m[0][0] = impl->matrix3d.m[1][1] = impl->matrix3d.m[2][2] = impl->matrix3d.m[3][3] = 1.0f;

    switch (kind)
    {
    case TRANSFORM_TRANSLATE:
        impl->IDCompositionTranslateTransform_iface.lpVtbl = &translate_transform_vtbl;
        break;
    case TRANSFORM_SCALE:
        impl->IDCompositionScaleTransform_iface.lpVtbl = &scale_transform_vtbl;
        break;
    case TRANSFORM_ROTATE:
        impl->IDCompositionRotateTransform_iface.lpVtbl = &rotate_transform_vtbl;
        break;
    case TRANSFORM_SKEW:
        impl->IDCompositionSkewTransform_iface.lpVtbl = &skew_transform_vtbl;
        break;
    case TRANSFORM_MATRIX:
        impl->IDCompositionMatrixTransform_iface.lpVtbl = &matrix_transform_vtbl;
        break;
    case TRANSFORM_TRANSLATE_3D:
        impl->IDCompositionTranslateTransform3D_iface.lpVtbl = &translate_transform3d_vtbl;
        break;
    case TRANSFORM_SCALE_3D:
        impl->IDCompositionScaleTransform3D_iface.lpVtbl = &scale_transform3d_vtbl;
        break;
    case TRANSFORM_ROTATE_3D:
        impl->IDCompositionRotateTransform3D_iface.lpVtbl = &rotate_transform3d_vtbl;
        break;
    case TRANSFORM_MATRIX_3D:
        impl->IDCompositionMatrixTransform3D_iface.lpVtbl = &matrix_transform3d_vtbl;
        break;
    case OBJECT_EFFECT_GROUP:
        impl->IDCompositionEffectGroup_iface.lpVtbl = &effect_group_vtbl;
        break;
    case OBJECT_RECTANGLE_CLIP:
        impl->IDCompositionRectangleClip_iface.lpVtbl = &rectangle_clip_vtbl;
        break;
    default:
        free( impl );
        return NULL;
    }

    TRACE( "created object %p, kind %u.\n", impl, kind );
    return impl;
}
