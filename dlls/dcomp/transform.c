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

#include <math.h>
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

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

WINE_DEFAULT_DEBUG_CHANNEL(dcomp);

void dcomp_matrix_identity( D3DMATRIX *m )
{
    memset( m, 0, sizeof(*m) );
    m->m[0][0] = 1.0f;
    m->m[1][1] = 1.0f;
    m->m[2][2] = 1.0f;
    m->m[3][3] = 1.0f;
}

void dcomp_matrix_multiply( D3DMATRIX *out, const D3DMATRIX *a, const D3DMATRIX *b )
{
    D3DMATRIX tmp;
    int i, j;

    for (i = 0; i < 4; i++)
    {
        for (j = 0; j < 4; j++)
        {
            tmp.m[i][j] = a->m[i][0] * b->m[0][j]
                        + a->m[i][1] * b->m[1][j]
                        + a->m[i][2] * b->m[2][j]
                        + a->m[i][3] * b->m[3][j];
        }
    }
    *out = tmp;
}

void dcomp_matrix_from_2d( D3DMATRIX *out, const D2D_MATRIX_3X2_F *m2d )
{
    dcomp_matrix_identity( out );
    out->m[0][0] = m2d->m11;
    out->m[0][1] = m2d->m12;
    out->m[1][0] = m2d->m21;
    out->m[1][1] = m2d->m22;
    out->m[3][0] = m2d->dx;
    out->m[3][1] = m2d->dy;
}

BOOL dcomp_matrix_is_identity( const D3DMATRIX *m )
{
    static const D3DMATRIX id = {{
        {1.0f, 0.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 0.0f, 1.0f},
    }};
    return !memcmp( m, &id, sizeof(id) );
}

static void transform_update_matrix_3d( struct dcomp_transform *impl )
{
    D3DMATRIX *m = &impl->matrix3d;
    float rad, s, c, t;

    switch (impl->kind)
    {
    case TRANSFORM_TRANSLATE:
        dcomp_matrix_identity( m );
        m->m[3][0] = impl->offset_x;
        m->m[3][1] = impl->offset_y;
        break;
    case TRANSFORM_SCALE:
        dcomp_matrix_identity( m );
        m->m[0][0] = impl->scale_x;
        m->m[1][1] = impl->scale_y;
        m->m[3][0] = impl->center_x * (1.0f - impl->scale_x);
        m->m[3][1] = impl->center_y * (1.0f - impl->scale_y);
        break;
    case TRANSFORM_ROTATE:
        dcomp_matrix_identity( m );
        rad = impl->angle * (float)(M_PI / 180.0);
        s = sinf( rad );
        c = cosf( rad );
        m->m[0][0] = c;
        m->m[0][1] = s;
        m->m[1][0] = -s;
        m->m[1][1] = c;
        m->m[3][0] = impl->center_x - (impl->center_x * c - impl->center_y * s);
        m->m[3][1] = impl->center_y - (impl->center_x * s + impl->center_y * c);
        break;
    case TRANSFORM_SKEW:
    {
        float tx = tanf( impl->angle_x * (float)(M_PI / 180.0) );
        float ty = tanf( impl->angle_y * (float)(M_PI / 180.0) );
        dcomp_matrix_identity( m );
        m->m[0][1] = ty;
        m->m[1][0] = tx;
        m->m[3][0] = -impl->center_y * tx;
        m->m[3][1] = -impl->center_x * ty;
        break;
    }
    case TRANSFORM_MATRIX:
        dcomp_matrix_from_2d( m, &impl->matrix );
        break;
    case TRANSFORM_TRANSLATE_3D:
        dcomp_matrix_identity( m );
        m->m[3][0] = impl->offset_x;
        m->m[3][1] = impl->offset_y;
        m->m[3][2] = impl->offset_z;
        break;
    case TRANSFORM_SCALE_3D:
        dcomp_matrix_identity( m );
        m->m[0][0] = impl->scale_x;
        m->m[1][1] = impl->scale_y;
        m->m[2][2] = impl->scale_z;
        m->m[3][0] = impl->center_x * (1.0f - impl->scale_x);
        m->m[3][1] = impl->center_y * (1.0f - impl->scale_y);
        m->m[3][2] = impl->center_z * (1.0f - impl->scale_z);
        break;
    case TRANSFORM_ROTATE_3D:
    {
        float x = impl->axis_x, y = impl->axis_y, z = impl->axis_z;
        float len = sqrtf( x * x + y * y + z * z );
        dcomp_matrix_identity( m );
        if (len >= 1e-8f && impl->angle != 0.0f)
        {
            x /= len;
            y /= len;
            z /= len;
            rad = impl->angle * (float)(M_PI / 180.0);
            s = sinf( rad );
            c = cosf( rad );
            t = 1.0f - c;

            m->m[0][0] = t * x * x + c;
            m->m[0][1] = t * x * y + s * z;
            m->m[0][2] = t * x * z - s * y;

            m->m[1][0] = t * x * y - s * z;
            m->m[1][1] = t * y * y + c;
            m->m[1][2] = t * y * z + s * x;

            m->m[2][0] = t * x * z + s * y;
            m->m[2][1] = t * y * z - s * x;
            m->m[2][2] = t * z * z + c;

            m->m[3][0] = impl->center_x - (impl->center_x * m->m[0][0] + impl->center_y * m->m[1][0] + impl->center_z * m->m[2][0]);
            m->m[3][1] = impl->center_y - (impl->center_x * m->m[0][1] + impl->center_y * m->m[1][1] + impl->center_z * m->m[2][1]);
            m->m[3][2] = impl->center_z - (impl->center_x * m->m[0][2] + impl->center_y * m->m[1][2] + impl->center_z * m->m[2][2]);
        }
        break;
    }
    default:
        break;
    }
}

static HRESULT transform_QI( struct dcomp_transform *impl, void *this_iface, REFIID iid, void **out )
{
    TRACE( "iface %p, iid %s, out %p.\n", this_iface, debugstr_guid( iid ), out );

    if (!out) return E_POINTER;
    *out = NULL;

    if (IsEqualGUID( iid, &IID_IUnknown ))
    {
        *out = this_iface;
    }
    else if (IsEqualGUID( iid, &IID_IDCompositionEffect )
            && impl->kind != OBJECT_RECTANGLE_CLIP)
    {
        *out = this_iface;
    }
    else if (IsEqualGUID( iid, &IID_IDCompositionTransform3D )
            && (impl->kind == TRANSFORM_TRANSLATE || impl->kind == TRANSFORM_SCALE
                || impl->kind == TRANSFORM_ROTATE || impl->kind == TRANSFORM_SKEW
                || impl->kind == TRANSFORM_MATRIX || impl->kind == TRANSFORM_GROUP
                || impl->kind == TRANSFORM_TRANSLATE_3D || impl->kind == TRANSFORM_SCALE_3D
                || impl->kind == TRANSFORM_ROTATE_3D || impl->kind == TRANSFORM_MATRIX_3D
                || impl->kind == TRANSFORM_3D_GROUP))
    {
        *out = this_iface;
    }
    else if (IsEqualGUID( iid, &IID_IDCompositionTransform )
            && (impl->kind == TRANSFORM_TRANSLATE || impl->kind == TRANSFORM_SCALE
                || impl->kind == TRANSFORM_ROTATE || impl->kind == TRANSFORM_SKEW
                || impl->kind == TRANSFORM_MATRIX || impl->kind == TRANSFORM_GROUP))
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
    UINT i;

    if (!ref)
    {
        if (impl->transform3d) IDCompositionTransform3D_Release( impl->transform3d );
        if (impl->group_items)
        {
            for (i = 0; i < impl->group_count; i++)
                if (impl->group_items[i]) IUnknown_Release( impl->group_items[i] );
            free( impl->group_items );
        }
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
        transform_update_matrix_3d( impl );                                                        \
        InterlockedIncrement( &impl->version );                                                    \
        InterlockedIncrement( &dcomp_transform_version );                                          \
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
    transform_update_matrix_3d( impl );
    InterlockedIncrement( &impl->version );
    InterlockedIncrement( &dcomp_transform_version );
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
    transform_update_matrix_3d( impl );
    InterlockedIncrement( &impl->version );
    InterlockedIncrement( &dcomp_transform_version );
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
    InterlockedIncrement( &impl->version );
    InterlockedIncrement( &dcomp_transform_version );
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

    TRACE( "iface %p, row %d, column %d, value %g.\n", iface, row, column, value );

    if (row < 0 || row > 3 || column < 0 || column > 3) return E_INVALIDARG;
    impl->matrix3d.m[row][column] = value;
    InterlockedIncrement( &impl->version );
    InterlockedIncrement( &dcomp_transform_version );
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
    InterlockedIncrement( &impl->version );
    InterlockedIncrement( &dcomp_transform_version );
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
        InterlockedIncrement( &impl->version );
        InterlockedIncrement( &dcomp_transform_version );
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
        InterlockedIncrement( &impl->version );                                                    \
        InterlockedIncrement( &dcomp_transform_version );                                          \
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

static HRESULT STDMETHODCALLTYPE tg_QueryInterface( IDCompositionTransform *iface, REFIID iid, void **out )
{
    struct dcomp_transform *impl = CONTAINING_RECORD( (void *)iface, struct dcomp_transform, IDCompositionTranslateTransform_iface );
    return transform_QI( impl, (void *)iface, iid, out );
}

static ULONG STDMETHODCALLTYPE tg_AddRef( IDCompositionTransform *iface )
{
    struct dcomp_transform *impl = CONTAINING_RECORD( (void *)iface, struct dcomp_transform, IDCompositionTranslateTransform_iface );
    return transform_addref( impl );
}

static ULONG STDMETHODCALLTYPE tg_Release( IDCompositionTransform *iface )
{
    struct dcomp_transform *impl = CONTAINING_RECORD( (void *)iface, struct dcomp_transform, IDCompositionTranslateTransform_iface );
    return transform_release( impl );
}

static const struct IDCompositionTransformVtbl transform_group_vtbl =
{
    tg_QueryInterface,
    tg_AddRef,
    tg_Release,
};

static HRESULT STDMETHODCALLTYPE tg3_QueryInterface( IDCompositionTransform3D *iface, REFIID iid, void **out )
{
    struct dcomp_transform *impl = CONTAINING_RECORD( (void *)iface, struct dcomp_transform, IDCompositionTranslateTransform3D_iface );
    return transform_QI( impl, (void *)iface, iid, out );
}

static ULONG STDMETHODCALLTYPE tg3_AddRef( IDCompositionTransform3D *iface )
{
    struct dcomp_transform *impl = CONTAINING_RECORD( (void *)iface, struct dcomp_transform, IDCompositionTranslateTransform3D_iface );
    return transform_addref( impl );
}

static ULONG STDMETHODCALLTYPE tg3_Release( IDCompositionTransform3D *iface )
{
    struct dcomp_transform *impl = CONTAINING_RECORD( (void *)iface, struct dcomp_transform, IDCompositionTranslateTransform3D_iface );
    return transform_release( impl );
}

static const struct IDCompositionTransform3DVtbl transform3d_group_vtbl =
{
    tg3_QueryInterface,
    tg3_AddRef,
    tg3_Release,
};

struct dcomp_transform *unsafe_impl_from_dcomp_transform( IUnknown *iface )
{
    const void *vtbl;

    if (!iface) return NULL;
    vtbl = iface->lpVtbl;

    if (vtbl == &translate_transform_vtbl || vtbl == &transform_group_vtbl)
        return CONTAINING_RECORD( (void *)iface, struct dcomp_transform, IDCompositionTranslateTransform_iface );
    if (vtbl == &scale_transform_vtbl)
        return CONTAINING_RECORD( (void *)iface, struct dcomp_transform, IDCompositionScaleTransform_iface );
    if (vtbl == &rotate_transform_vtbl)
        return CONTAINING_RECORD( (void *)iface, struct dcomp_transform, IDCompositionRotateTransform_iface );
    if (vtbl == &skew_transform_vtbl)
        return CONTAINING_RECORD( (void *)iface, struct dcomp_transform, IDCompositionSkewTransform_iface );
    if (vtbl == &matrix_transform_vtbl)
        return CONTAINING_RECORD( (void *)iface, struct dcomp_transform, IDCompositionMatrixTransform_iface );
    if (vtbl == &translate_transform3d_vtbl || vtbl == &transform3d_group_vtbl)
        return CONTAINING_RECORD( (void *)iface, struct dcomp_transform, IDCompositionTranslateTransform3D_iface );
    if (vtbl == &scale_transform3d_vtbl)
        return CONTAINING_RECORD( (void *)iface, struct dcomp_transform, IDCompositionScaleTransform3D_iface );
    if (vtbl == &rotate_transform3d_vtbl)
        return CONTAINING_RECORD( (void *)iface, struct dcomp_transform, IDCompositionRotateTransform3D_iface );
    if (vtbl == &matrix_transform3d_vtbl)
        return CONTAINING_RECORD( (void *)iface, struct dcomp_transform, IDCompositionMatrixTransform3D_iface );
    if (vtbl == &effect_group_vtbl)
        return CONTAINING_RECORD( (void *)iface, struct dcomp_transform, IDCompositionEffectGroup_iface );
    if (vtbl == &rectangle_clip_vtbl)
        return CONTAINING_RECORD( (void *)iface, struct dcomp_transform, IDCompositionRectangleClip_iface );

    return NULL;
}

static void transform_get_matrix_3d_depth( const struct dcomp_transform *impl, D3DMATRIX *out, unsigned int depth )
{
    D3DMATRIX child_m;
    UINT i;

    if (!impl || depth > 64)
    {
        dcomp_matrix_identity( out );
        return;
    }

    switch (impl->kind)
    {
    case TRANSFORM_GROUP:
    case TRANSFORM_3D_GROUP:
        dcomp_matrix_identity( out );
        for (i = 0; i < impl->group_count; i++)
        {
            struct dcomp_transform *child = unsafe_impl_from_dcomp_transform( impl->group_items[i] );
            if (child)
            {
                transform_get_matrix_3d_depth( child, &child_m, depth + 1 );
                dcomp_matrix_multiply( out, out, &child_m );
            }
        }
        break;
    case OBJECT_EFFECT_GROUP:
        if (impl->transform3d)
        {
            struct dcomp_transform *child = unsafe_impl_from_dcomp_transform( (IUnknown *)impl->transform3d );
            if (child)
            {
                transform_get_matrix_3d_depth( child, out, depth + 1 );
                return;
            }
        }
        dcomp_matrix_identity( out );
        break;
    case OBJECT_RECTANGLE_CLIP:
        dcomp_matrix_identity( out );
        break;
    default:
        *out = impl->matrix3d;
        break;
    }
}

void dcomp_transform_get_matrix_3d( const struct dcomp_transform *impl, D3DMATRIX *out )
{
    transform_get_matrix_3d_depth( impl, out, 0 );
}

static unsigned int transform_get_version_depth( const struct dcomp_transform *impl, unsigned int depth )
{
    unsigned int ver;
    UINT i;

    if (!impl || depth > 64)
        return 0;

    ver = (unsigned int)impl->version;
    if (impl->kind == OBJECT_EFFECT_GROUP && impl->transform3d)
    {
        struct dcomp_transform *child = unsafe_impl_from_dcomp_transform( (IUnknown *)impl->transform3d );
        if (child)
            ver += transform_get_version_depth( child, depth + 1 );
    }
    else if (impl->kind == TRANSFORM_GROUP || impl->kind == TRANSFORM_3D_GROUP)
    {
        for (i = 0; i < impl->group_count; i++)
        {
            struct dcomp_transform *child = unsafe_impl_from_dcomp_transform( impl->group_items[i] );
            if (child)
                ver += transform_get_version_depth( child, depth + 1 );
        }
    }
    return ver;
}

unsigned int dcomp_transform_get_version( const struct dcomp_transform *impl )
{
    return transform_get_version_depth( impl, 0 );
}

struct dcomp_transform *dcomp_transform_create( struct composition_device *device,
        enum dcomp_object_kind kind )
{
    struct dcomp_transform *impl;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return NULL;
    impl->ref = 1;
    impl->version = 1;
    impl->kind = kind;
    impl->device = device;
    impl->scale_x = impl->scale_y = impl->scale_z = 1.0f;
    impl->axis_z = 1.0f;
    impl->opacity = 1.0f;
    impl->matrix.m11 = impl->matrix.m22 = 1.0f;
    dcomp_matrix_identity( &impl->matrix3d );

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
    case TRANSFORM_GROUP:
        impl->IDCompositionTranslateTransform_iface.lpVtbl = (const struct IDCompositionTranslateTransformVtbl *)&transform_group_vtbl;
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
    case TRANSFORM_3D_GROUP:
        impl->IDCompositionTranslateTransform3D_iface.lpVtbl = (const struct IDCompositionTranslateTransform3DVtbl *)&transform3d_group_vtbl;
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

struct dcomp_transform *dcomp_transform_group_create( struct composition_device *device,
        enum dcomp_object_kind kind, IUnknown *const *items, UINT count )
{
    struct dcomp_transform *impl;
    UINT i;

    if (!items || !count) return NULL;
    for (i = 0; i < count; i++)
        if (!items[i]) return NULL;

    if (!(impl = dcomp_transform_create( device, kind )))
        return NULL;

    if (!(impl->group_items = calloc( count, sizeof(impl->group_items[0]) )))
    {
        transform_release( impl );
        return NULL;
    }

    impl->group_count = count;
    for (i = 0; i < count; i++)
    {
        impl->group_items[i] = items[i];
        IUnknown_AddRef( items[i] );
    }

    return impl;
}
