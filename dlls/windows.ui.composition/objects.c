/* WinRT Windows.UI.Composition composition objects implementation: animations,
 * easing functions, clips, brushes, targets, surfaces, graphics devices and batches
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

#include "private.h"

WINE_DEFAULT_DEBUG_CHANNEL(composition);

/* ICompositionObject implementation shared by every composition object in this
 * file. The object's Properties is a lazily created CompositionPropertySet. */
#define COMP_OBJECT_IMPL( pfx, impl_type )                                                        \
    static HRESULT WINAPI pfx##_get_Compositor( struct ICompositionObject *iface,                 \
            struct ICompositor **value )                                                           \
    {                                                                                              \
        impl_type *impl = impl_from_##pfx( iface );                                                \
        TRACE( "iface %p, value %p.\n", iface, value );                                            \
        if (!value) return E_POINTER;                                                              \
        *value = NULL;                                                                             \
        if (impl->compositor)                                                                      \
        {                                                                                          \
            *value = &impl->compositor->ICompositor_iface;                                         \
            IInspectable_AddRef( (IInspectable *)*value );                                         \
        }                                                                                          \
        return S_OK;                                                                               \
    }                                                                                              \
    static HRESULT WINAPI pfx##_get_Dispatcher( struct ICompositionObject *iface,                  \
            struct ICoreDispatcher **value )                                                        \
    {                                                                                              \
        FIXME( "iface %p, value %p stub!\n", iface, value );                                       \
        if (!value) return E_POINTER;                                                              \
        *value = NULL;                                                                             \
        return S_OK;                                                                               \
    }                                                                                              \
    static HRESULT WINAPI pfx##_get_Properties( struct ICompositionObject *iface,                  \
            struct ICompositionPropertySet **value )                                                \
    {                                                                                              \
        impl_type *impl = impl_from_##pfx( iface );                                                \
        TRACE( "iface %p, value %p.\n", iface, value );                                            \
        if (!value) return E_POINTER;                                                              \
        if (!impl->properties && !(impl->properties = prop_set_create( impl->compositor )))        \
            return E_OUTOFMEMORY;                                                                  \
        *value = prop_set_get_iface( impl->properties );                                           \
        IInspectable_AddRef( (IInspectable *)*value );                                             \
        return S_OK;                                                                               \
    }                                                                                              \
    static HRESULT WINAPI pfx##_StartAnimation( struct ICompositionObject *iface, HSTRING name,    \
            struct ICompositionAnimation *animation )                                               \
    {                                                                                              \
        TRACE( "iface %p, name %s, animation %p stub!\n", iface,                                   \
                debugstr_hstring( name ), animation );                                             \
        return S_OK;                                                                               \
    }                                                                                              \
    static HRESULT WINAPI pfx##_StopAnimation( struct ICompositionObject *iface, HSTRING name )    \
    {                                                                                              \
        TRACE( "iface %p, name %s stub!\n", iface, debugstr_hstring( name ) );                     \
        return S_OK;                                                                               \
    }

/* IClosable implementation shared by every composition object in this file. */
#define COMP_CLOSABLE_IMPL( pfx, impl_type )                                                       \
    static HRESULT WINAPI pfx##_Close( struct IClosable *iface )                                   \
    {                                                                                              \
        TRACE( "iface %p stub!\n", iface );                                                        \
        return S_OK;                                                                               \
    }

/*
 * ICompositionAnimation methods, shared by the keyframe and expression
 * animation roots. Animation parameters are accepted and discarded: there is
 * no animation engine, the objects exist to keep the WinAppSDK render path
 * from failing.
 */
#define COMP_ANIM_METHODS( pfx )                                                                   \
    static HRESULT WINAPI pfx##_ClearAllParameters( struct ICompositionAnimation *iface )          \
    {                                                                                              \
        TRACE( "iface %p stub!\n", iface );                                                        \
        return S_OK;                                                                               \
    }                                                                                              \
    static HRESULT WINAPI pfx##_ClearParameter( struct ICompositionAnimation *iface, HSTRING key ) \
    {                                                                                              \
        TRACE( "iface %p, key %s stub!\n", iface, debugstr_hstring( key ) );                       \
        return S_OK;                                                                               \
    }                                                                                              \
    static HRESULT WINAPI pfx##_SetColorParameter( struct ICompositionAnimation *iface,            \
            HSTRING key, Color value )                                                             \
    {                                                                                              \
        TRACE( "iface %p, key %s, value %08x stub!\n", iface, debugstr_hstring( key ),             \
                *(const UINT32 *)&value );                                                         \
        return S_OK;                                                                               \
    }                                                                                              \
    static HRESULT WINAPI pfx##_SetMatrix3x2Parameter( struct ICompositionAnimation *iface,        \
            HSTRING key, Matrix3x2 value )                                                         \
    {                                                                                              \
        TRACE( "iface %p, key %s stub!\n", iface, debugstr_hstring( key ) );                       \
        return S_OK;                                                                               \
    }                                                                                              \
    static HRESULT WINAPI pfx##_SetMatrix4x4Parameter( struct ICompositionAnimation *iface,        \
            HSTRING key, Matrix4x4 value )                                                         \
    {                                                                                              \
        TRACE( "iface %p, key %s stub!\n", iface, debugstr_hstring( key ) );                       \
        return S_OK;                                                                               \
    }                                                                                              \
    static HRESULT WINAPI pfx##_SetQuaternionParameter( struct ICompositionAnimation *iface,       \
            HSTRING key, Quaternion value )                                                        \
    {                                                                                              \
        TRACE( "iface %p, key %s stub!\n", iface, debugstr_hstring( key ) );                       \
        return S_OK;                                                                               \
    }                                                                                              \
    static HRESULT WINAPI pfx##_SetReferenceParameter( struct ICompositionAnimation *iface,        \
            HSTRING key, struct ICompositionObject *object )                                       \
    {                                                                                              \
        TRACE( "iface %p, key %s, object %p stub!\n", iface, debugstr_hstring( key ), object );    \
        return S_OK;                                                                               \
    }                                                                                              \
    static HRESULT WINAPI pfx##_SetScalarParameter( struct ICompositionAnimation *iface,           \
            HSTRING key, FLOAT value )                                                             \
    {                                                                                              \
        TRACE( "iface %p, key %s, value %g stub!\n", iface, debugstr_hstring( key ), value );      \
        return S_OK;                                                                               \
    }                                                                                              \
    static HRESULT WINAPI pfx##_SetVector2Parameter( struct ICompositionAnimation *iface,          \
            HSTRING key, Vector2 value )                                                           \
    {                                                                                              \
        TRACE( "iface %p, key %s stub!\n", iface, debugstr_hstring( key ) );                       \
        return S_OK;                                                                               \
    }                                                                                              \
    static HRESULT WINAPI pfx##_SetVector3Parameter( struct ICompositionAnimation *iface,          \
            HSTRING key, Vector3 value )                                                           \
    {                                                                                              \
        TRACE( "iface %p, key %s stub!\n", iface, debugstr_hstring( key ) );                       \
        return S_OK;                                                                               \
    }                                                                                              \
    static HRESULT WINAPI pfx##_SetVector4Parameter( struct ICompositionAnimation *iface,          \
            HSTRING key, Vector4 value )                                                           \
    {                                                                                              \
        TRACE( "iface %p, key %s stub!\n", iface, debugstr_hstring( key ) );                       \
        return S_OK;                                                                               \
    }

static void comp_release_refs( struct compositor *compositor, struct prop_set *properties )
{
    if (properties)
        IInspectable_Release( (IInspectable *)prop_set_get_iface( properties ) );
    if (compositor)
        IInspectable_Release( (IInspectable *)&compositor->ICompositor_iface );
}

/*
 * Keyframe animations.
 */

static const GUID IID_IKeyFrameAnimation_local =
    { 0x126e7f22, 0x3ae9, 0x4540, { 0x9a, 0x8a, 0xde, 0xae, 0x8a, 0x4a, 0x4a, 0x84 } };
static const GUID IID_IKeyFrameAnimation2_local =
    { 0x369b603e, 0xa80f, 0x4948, { 0x93, 0xe3, 0xed, 0x23, 0xfb, 0x38, 0xc6, 0xcb } };
static const GUID IID_IKeyFrameAnimation3_local =
    { 0xd51e030d, 0x7da4, 0x4bd7, { 0xbc, 0x2d, 0xf4, 0x51, 0x75, 0x29, 0xf4, 0x3a } };
static const GUID IID_IKeyFrameAnimation4_local =
    { 0x464c4c2c, 0x1caa, 0x4061, { 0x9b, 0x40, 0xe1, 0x3f, 0xde, 0x15, 0x03, 0xca } };
static const GUID IID_IKeyFrameAnimation5_local =
    { 0x770137be, 0x76bc, 0x4e23, { 0xbf, 0xed, 0xfe, 0x9c, 0xc2, 0x0f, 0x6e, 0xc9 } };
static const GUID IID_IKeyFrameAnimation6_local =
    { 0xf4b488bb, 0x2940, 0x4ec0, { 0xa4, 0x1a, 0xeb, 0x6d, 0x80, 0x1a, 0x2f, 0x18 } };
static const GUID IID_IKeyFrameAnimation7_local =
    { 0x845bf0b4, 0xd8de, 0x462f, { 0x87, 0x53, 0xc8, 0x0d, 0x43, 0xc6, 0xff, 0x5a } };

static const GUID IID_IPathKeyFrameAnimation_local =
    { 0x9d0d18c9, 0x1576, 0x4b3f, { 0xbe, 0x60, 0x1d, 0x50, 0x31, 0xf5, 0xe7, 0x1b } };
static const GUID IID_IBooleanKeyFrameAnimation_local =
    { 0x95e23a08, 0xd1f4, 0x4972, { 0x97, 0x70, 0x3e, 0xfe, 0x68, 0xd8, 0x2e, 0x14 } };

static inline struct keyframe_animation *impl_from_ICompositionAnimation( struct ICompositionAnimation *iface )
{
    return CONTAINING_RECORD( iface, struct keyframe_animation, ICompositionAnimation_iface );
}

static const GUID *keyframe_kind_iid( enum animation_kind kind )
{
    switch (kind)
    {
        case ANIM_COLOR_KEY: return &IID_IColorKeyFrameAnimation;
        case ANIM_SCALAR_KEY: return &IID_IScalarKeyFrameAnimation;
        case ANIM_VECTOR2_KEY: return &IID_IVector2KeyFrameAnimation;
        case ANIM_VECTOR3_KEY: return &IID_IVector3KeyFrameAnimation;
        case ANIM_VECTOR4_KEY: return &IID_IVector4KeyFrameAnimation;
        case ANIM_QUATERNION_KEY: return &IID_IQuaternionKeyFrameAnimation;
        case ANIM_PATH_KEY: return &IID_IPathKeyFrameAnimation_local;
        case ANIM_BOOLEAN_KEY: return &IID_IBooleanKeyFrameAnimation_local;
    }
    return NULL;
}

static void *keyframe_kind_iface( struct keyframe_animation *impl, enum animation_kind kind )
{
    switch (kind)
    {
        case ANIM_COLOR_KEY: return &impl->IColorKeyFrameAnimation_iface;
        case ANIM_SCALAR_KEY: return &impl->IScalarKeyFrameAnimation_iface;
        case ANIM_VECTOR2_KEY: return &impl->IVector2KeyFrameAnimation_iface;
        case ANIM_VECTOR3_KEY: return &impl->IVector3KeyFrameAnimation_iface;
        case ANIM_VECTOR4_KEY: return &impl->IVector4KeyFrameAnimation_iface;
        case ANIM_QUATERNION_KEY: return &impl->IQuaternionKeyFrameAnimation_iface;
        case ANIM_PATH_KEY: return &impl->IPathKeyFrameAnimation_iface;
        case ANIM_BOOLEAN_KEY: return &impl->IBooleanKeyFrameAnimation_iface;
    }
    return NULL;
}

static const WCHAR *keyframe_kind_class_name( enum animation_kind kind )
{
    switch (kind)
    {
        case ANIM_COLOR_KEY: return RuntimeClass_Windows_UI_Composition_ColorKeyFrameAnimation;
        case ANIM_SCALAR_KEY: return RuntimeClass_Windows_UI_Composition_ScalarKeyFrameAnimation;
        case ANIM_VECTOR2_KEY: return RuntimeClass_Windows_UI_Composition_Vector2KeyFrameAnimation;
        case ANIM_VECTOR3_KEY: return RuntimeClass_Windows_UI_Composition_Vector3KeyFrameAnimation;
        case ANIM_VECTOR4_KEY: return RuntimeClass_Windows_UI_Composition_Vector4KeyFrameAnimation;
        case ANIM_QUATERNION_KEY: return RuntimeClass_Windows_UI_Composition_QuaternionKeyFrameAnimation;
        case ANIM_PATH_KEY: return L"Windows.UI.Composition.PathKeyFrameAnimation";
        case ANIM_BOOLEAN_KEY: return L"Windows.UI.Composition.BooleanKeyFrameAnimation";
    }
    return NULL;
}

static HRESULT WINAPI keyframe_QueryInterface( struct ICompositionAnimation *iface, REFIID iid, void **out )
{
    struct keyframe_animation *impl = impl_from_ICompositionAnimation( iface );
    const GUID *kind_iid;

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (!out) return E_POINTER;

    if (IsEqualGUID( iid, &IID_IUnknown )
        || IsEqualGUID( iid, &IID_IInspectable )
        || IsEqualGUID( iid, &IID_IAgileObject )
        || IsEqualGUID( iid, &IID_ICompositionAnimation ))
    {
        *out = &impl->ICompositionAnimation_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_ICompositionAnimationBase ))
    {
        *out = &impl->ICompositionAnimationBase_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IKeyFrameAnimation_local )
        || IsEqualGUID( iid, &IID_IKeyFrameAnimation4_local ))
    {
        *out = &impl->IKeyFrameAnimation_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IKeyFrameAnimation2_local ))
    {
        *out = &impl->IKeyFrameAnimation2_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IKeyFrameAnimation3_local ))
    {
        *out = &impl->IKeyFrameAnimation3_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IKeyFrameAnimation5_local )
        || IsEqualGUID( iid, &IID_IKeyFrameAnimation6_local )
        || IsEqualGUID( iid, &IID_IKeyFrameAnimation7_local ))
    {
        *out = &impl->IKeyFrameAnimation6_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    kind_iid = keyframe_kind_iid( impl->kind );
    if (kind_iid && IsEqualGUID( iid, kind_iid ))
    {
        *out = keyframe_kind_iface( impl, impl->kind );
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_ICompositionObject ))
    {
        *out = &impl->ICompositionObject_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IClosable ))
    {
        *out = &impl->IClosable_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI keyframe_AddRef( struct ICompositionAnimation *iface )
{
    struct keyframe_animation *impl = impl_from_ICompositionAnimation( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI keyframe_Release( struct ICompositionAnimation *iface )
{
    struct keyframe_animation *impl = impl_from_ICompositionAnimation( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        comp_release_refs( impl->compositor, impl->properties );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI keyframe_GetIids( struct ICompositionAnimation *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI keyframe_GetRuntimeClassName( struct ICompositionAnimation *iface, HSTRING *class_name )
{
    struct keyframe_animation *impl = impl_from_ICompositionAnimation( iface );
    const WCHAR *name = keyframe_kind_class_name( impl->kind );

    return WindowsCreateString( name, lstrlenW( name ), class_name );
}

static HRESULT WINAPI keyframe_GetTrustLevel( struct ICompositionAnimation *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

COMP_ANIM_METHODS( keyframe )

static const struct ICompositionAnimationVtbl keyframe_vtbl =
{
    keyframe_QueryInterface,
    keyframe_AddRef,
    keyframe_Release,
    keyframe_GetIids,
    keyframe_GetRuntimeClassName,
    keyframe_GetTrustLevel,
    keyframe_ClearAllParameters,
    keyframe_ClearParameter,
    keyframe_SetColorParameter,
    keyframe_SetMatrix3x2Parameter,
    keyframe_SetMatrix4x4Parameter,
    keyframe_SetQuaternionParameter,
    keyframe_SetReferenceParameter,
    keyframe_SetScalarParameter,
    keyframe_SetVector2Parameter,
    keyframe_SetVector3Parameter,
    keyframe_SetVector4Parameter,
};

DEFINE_INHERITED_PROLOGUE( keyframe_base, ICompositionAnimationBase, struct keyframe_animation,
        ICompositionAnimation_iface )

static const struct ICompositionAnimationBaseVtbl keyframe_base_vtbl =
{
    keyframe_base_QueryInterface,
    keyframe_base_AddRef,
    keyframe_base_Release,
    keyframe_base_GetIids,
    keyframe_base_GetRuntimeClassName,
    keyframe_base_GetTrustLevel,
};

DEFINE_INHERITED_PROLOGUE( scalarkey, IScalarKeyFrameAnimation, struct keyframe_animation,
        ICompositionAnimation_iface )

static HRESULT WINAPI scalarkey_InsertKeyFrame( struct IScalarKeyFrameAnimation *iface, FLOAT key, FLOAT value )
{
    TRACE( "iface %p, key %g stub!\n", iface, key );
    return S_OK;
}

static HRESULT WINAPI scalarkey_InsertKeyFrameWithEasingFunction( struct IScalarKeyFrameAnimation *iface,
        FLOAT key, FLOAT value, struct ICompositionEasingFunction *function )
{
    TRACE( "iface %p, key %g, function %p stub!\n", iface, key, function );
    return S_OK;
}

static const struct IScalarKeyFrameAnimationVtbl scalarkey_vtbl =
{
    scalarkey_QueryInterface,
    scalarkey_AddRef,
    scalarkey_Release,
    scalarkey_GetIids,
    scalarkey_GetRuntimeClassName,
    scalarkey_GetTrustLevel,
    scalarkey_InsertKeyFrame,
    scalarkey_InsertKeyFrameWithEasingFunction,
};

DEFINE_INHERITED_PROLOGUE( vector2key, IVector2KeyFrameAnimation, struct keyframe_animation,
        ICompositionAnimation_iface )

static HRESULT WINAPI vector2key_InsertKeyFrame( struct IVector2KeyFrameAnimation *iface, FLOAT key, Vector2 value )
{
    TRACE( "iface %p, key %g stub!\n", iface, key );
    return S_OK;
}

static HRESULT WINAPI vector2key_InsertKeyFrameWithEasingFunction( struct IVector2KeyFrameAnimation *iface,
        FLOAT key, Vector2 value, struct ICompositionEasingFunction *function )
{
    TRACE( "iface %p, key %g, function %p stub!\n", iface, key, function );
    return S_OK;
}

static const struct IVector2KeyFrameAnimationVtbl vector2key_vtbl =
{
    vector2key_QueryInterface,
    vector2key_AddRef,
    vector2key_Release,
    vector2key_GetIids,
    vector2key_GetRuntimeClassName,
    vector2key_GetTrustLevel,
    vector2key_InsertKeyFrame,
    vector2key_InsertKeyFrameWithEasingFunction,
};

DEFINE_INHERITED_PROLOGUE( vector3key, IVector3KeyFrameAnimation, struct keyframe_animation,
        ICompositionAnimation_iface )

static HRESULT WINAPI vector3key_InsertKeyFrame( struct IVector3KeyFrameAnimation *iface, FLOAT key, Vector3 value )
{
    TRACE( "iface %p, key %g stub!\n", iface, key );
    return S_OK;
}

static HRESULT WINAPI vector3key_InsertKeyFrameWithEasingFunction( struct IVector3KeyFrameAnimation *iface,
        FLOAT key, Vector3 value, struct ICompositionEasingFunction *function )
{
    TRACE( "iface %p, key %g, function %p stub!\n", iface, key, function );
    return S_OK;
}

static const struct IVector3KeyFrameAnimationVtbl vector3key_vtbl =
{
    vector3key_QueryInterface,
    vector3key_AddRef,
    vector3key_Release,
    vector3key_GetIids,
    vector3key_GetRuntimeClassName,
    vector3key_GetTrustLevel,
    vector3key_InsertKeyFrame,
    vector3key_InsertKeyFrameWithEasingFunction,
};

DEFINE_INHERITED_PROLOGUE( vector4key, IVector4KeyFrameAnimation, struct keyframe_animation,
        ICompositionAnimation_iface )

static HRESULT WINAPI vector4key_InsertKeyFrame( struct IVector4KeyFrameAnimation *iface, FLOAT key, Vector4 value )
{
    TRACE( "iface %p, key %g stub!\n", iface, key );
    return S_OK;
}

static HRESULT WINAPI vector4key_InsertKeyFrameWithEasingFunction( struct IVector4KeyFrameAnimation *iface,
        FLOAT key, Vector4 value, struct ICompositionEasingFunction *function )
{
    TRACE( "iface %p, key %g, function %p stub!\n", iface, key, function );
    return S_OK;
}

static const struct IVector4KeyFrameAnimationVtbl vector4key_vtbl =
{
    vector4key_QueryInterface,
    vector4key_AddRef,
    vector4key_Release,
    vector4key_GetIids,
    vector4key_GetRuntimeClassName,
    vector4key_GetTrustLevel,
    vector4key_InsertKeyFrame,
    vector4key_InsertKeyFrameWithEasingFunction,
};

DEFINE_INHERITED_PROLOGUE( quatkey, IQuaternionKeyFrameAnimation, struct keyframe_animation,
        ICompositionAnimation_iface )

static HRESULT WINAPI quatkey_InsertKeyFrame( struct IQuaternionKeyFrameAnimation *iface, FLOAT key,
        Quaternion value )
{
    TRACE( "iface %p, key %g stub!\n", iface, key );
    return S_OK;
}

static HRESULT WINAPI quatkey_InsertKeyFrameWithEasingFunction( struct IQuaternionKeyFrameAnimation *iface,
        FLOAT key, Quaternion value, struct ICompositionEasingFunction *function )
{
    TRACE( "iface %p, key %g, function %p stub!\n", iface, key, function );
    return S_OK;
}

static const struct IQuaternionKeyFrameAnimationVtbl quatkey_vtbl =
{
    quatkey_QueryInterface,
    quatkey_AddRef,
    quatkey_Release,
    quatkey_GetIids,
    quatkey_GetRuntimeClassName,
    quatkey_GetTrustLevel,
    quatkey_InsertKeyFrame,
    quatkey_InsertKeyFrameWithEasingFunction,
};

DEFINE_INHERITED_PROLOGUE( colorkey, IColorKeyFrameAnimation, struct keyframe_animation,
        ICompositionAnimation_iface )

static HRESULT WINAPI colorkey_get_InterpolationColorSpace( struct IColorKeyFrameAnimation *iface,
        enum CompositionColorSpace *value )
{
    if (!value) return E_POINTER;
    *value = CompositionColorSpace_Auto;
    return S_OK;
}

static HRESULT WINAPI colorkey_put_InterpolationColorSpace( struct IColorKeyFrameAnimation *iface,
        enum CompositionColorSpace value )
{
    TRACE( "iface %p, value %u stub!\n", iface, value );
    return S_OK;
}

static HRESULT WINAPI colorkey_InsertKeyFrame( struct IColorKeyFrameAnimation *iface, FLOAT key,
        Color value )
{
    TRACE( "iface %p, key %g stub!\n", iface, key );
    return S_OK;
}

static HRESULT WINAPI colorkey_InsertKeyFrameWithEasingFunction( struct IColorKeyFrameAnimation *iface,
        FLOAT key, Color value, struct ICompositionEasingFunction *function )
{
    TRACE( "iface %p, key %g, function %p stub!\n", iface, key, function );
    return S_OK;
}

static const struct IColorKeyFrameAnimationVtbl colorkey_vtbl =
{
    colorkey_QueryInterface,
    colorkey_AddRef,
    colorkey_Release,
    colorkey_GetIids,
    colorkey_GetRuntimeClassName,
    colorkey_GetTrustLevel,
    colorkey_get_InterpolationColorSpace,
    colorkey_put_InterpolationColorSpace,
    colorkey_InsertKeyFrame,
    colorkey_InsertKeyFrameWithEasingFunction,
};

/*
 * BooleanKeyFrameAnimation. Compositor::CreateBooleanKeyFrameAnimation
 * (ICompositor6, UniversalApiContract 7.0) returns this object. Its default
 * interface IBooleanKeyFrameAnimation {95e23a08-d1f4-4972-9770-3efe68d82e14}
 * is not in include/windows.ui.composition.idl, so it is answered through a
 * struct IInspectable plus a function-pointer vtable: IInspectable prologue
 * followed by the single InsertKeyFrame(normalizedProgress, value) slot. There
 * is no animation engine, so the frames are accepted and discarded.
 */

static inline struct keyframe_animation *impl_from_booleankey( void *iface )
{
    return CONTAINING_RECORD( iface, struct keyframe_animation, IBooleanKeyFrameAnimation_iface );
}
static HRESULT WINAPI booleankey_QueryInterface( void *iface, REFIID iid, void **out ) { return keyframe_QueryInterface( &impl_from_booleankey(iface)->ICompositionAnimation_iface, iid, out ); }
static ULONG WINAPI booleankey_AddRef( void *iface ) { return keyframe_AddRef( &impl_from_booleankey(iface)->ICompositionAnimation_iface ); }
static ULONG WINAPI booleankey_Release( void *iface ) { return keyframe_Release( &impl_from_booleankey(iface)->ICompositionAnimation_iface ); }
static HRESULT WINAPI booleankey_GetIids( void *iface, ULONG *c, IID **i ) { return keyframe_GetIids( &impl_from_booleankey(iface)->ICompositionAnimation_iface, c, i ); }
static HRESULT WINAPI booleankey_GetRuntimeClassName( void *iface, HSTRING *n ) { return keyframe_GetRuntimeClassName( &impl_from_booleankey(iface)->ICompositionAnimation_iface, n ); }
static HRESULT WINAPI booleankey_GetTrustLevel( void *iface, TrustLevel *l ) { return keyframe_GetTrustLevel( &impl_from_booleankey(iface)->ICompositionAnimation_iface, l ); }

static HRESULT WINAPI booleankey_InsertKeyFrame( void *iface, FLOAT key, BOOLEAN value )
{
    TRACE( "iface %p, key %g, value %d stub!\n", iface, key, value );
    return S_OK;
}

static const void *booleankey_vtbl[] =
{
    booleankey_QueryInterface,
    booleankey_AddRef,
    booleankey_Release,
    booleankey_GetIids,
    booleankey_GetRuntimeClassName,
    booleankey_GetTrustLevel,
    booleankey_InsertKeyFrame,
};

static inline struct keyframe_animation *impl_from_pathkey( void *iface )
{
    return CONTAINING_RECORD( iface, struct keyframe_animation, IPathKeyFrameAnimation_iface );
}
static HRESULT WINAPI pathkey_QueryInterface( void *iface, REFIID iid, void **out ) { return keyframe_QueryInterface( &impl_from_pathkey(iface)->ICompositionAnimation_iface, iid, out ); }
static ULONG WINAPI pathkey_AddRef( void *iface ) { return keyframe_AddRef( &impl_from_pathkey(iface)->ICompositionAnimation_iface ); }
static ULONG WINAPI pathkey_Release( void *iface ) { return keyframe_Release( &impl_from_pathkey(iface)->ICompositionAnimation_iface ); }
static HRESULT WINAPI pathkey_GetIids( void *iface, ULONG *c, IID **i ) { return keyframe_GetIids( &impl_from_pathkey(iface)->ICompositionAnimation_iface, c, i ); }
static HRESULT WINAPI pathkey_GetRuntimeClassName( void *iface, HSTRING *n ) { return keyframe_GetRuntimeClassName( &impl_from_pathkey(iface)->ICompositionAnimation_iface, n ); }
static HRESULT WINAPI pathkey_GetTrustLevel( void *iface, TrustLevel *l ) { return keyframe_GetTrustLevel( &impl_from_pathkey(iface)->ICompositionAnimation_iface, l ); }

static HRESULT WINAPI pathkey_InsertKeyFrame( void *iface, FLOAT key, void *path )
{
    TRACE( "iface %p, key %g, path %p stub!\n", iface, key, path );
    return S_OK;
}
static HRESULT WINAPI pathkey_InsertKeyFrameWithEasingFunction( void *iface, FLOAT key, void *path, void *easing )
{
    TRACE( "iface %p, key %g, path %p, easing %p stub!\n", iface, key, path, easing );
    return S_OK;
}

static const void *pathkey_vtbl[] =
{
    pathkey_QueryInterface,
    pathkey_AddRef,
    pathkey_Release,
    pathkey_GetIids,
    pathkey_GetRuntimeClassName,
    pathkey_GetTrustLevel,
    pathkey_InsertKeyFrame,
    pathkey_InsertKeyFrameWithEasingFunction,
};

static inline struct keyframe_animation *impl_from_kfa( void *iface )
{
    return CONTAINING_RECORD( iface, struct keyframe_animation, IKeyFrameAnimation_iface );
}
static HRESULT WINAPI kfa_QueryInterface( void *iface, REFIID iid, void **out ) { return keyframe_QueryInterface( &impl_from_kfa(iface)->ICompositionAnimation_iface, iid, out ); }
static ULONG WINAPI kfa_AddRef( void *iface ) { return keyframe_AddRef( &impl_from_kfa(iface)->ICompositionAnimation_iface ); }
static ULONG WINAPI kfa_Release( void *iface ) { return keyframe_Release( &impl_from_kfa(iface)->ICompositionAnimation_iface ); }
static HRESULT WINAPI kfa_GetIids( void *iface, ULONG *c, IID **i ) { return keyframe_GetIids( &impl_from_kfa(iface)->ICompositionAnimation_iface, c, i ); }
static HRESULT WINAPI kfa_GetRuntimeClassName( void *iface, HSTRING *n ) { return keyframe_GetRuntimeClassName( &impl_from_kfa(iface)->ICompositionAnimation_iface, n ); }
static HRESULT WINAPI kfa_GetTrustLevel( void *iface, TrustLevel *l ) { return keyframe_GetTrustLevel( &impl_from_kfa(iface)->ICompositionAnimation_iface, l ); }

static HRESULT WINAPI kfa_get_DelayTime( void *iface, struct TimeSpan *value )
{
    struct keyframe_animation *impl = impl_from_kfa( iface );
    if (value) *value = impl->delay;
    return S_OK;
}
static HRESULT WINAPI kfa_put_DelayTime( void *iface, struct TimeSpan value )
{
    struct keyframe_animation *impl = impl_from_kfa( iface );
    impl->delay = value;
    return S_OK;
}
static HRESULT WINAPI kfa_get_Duration( void *iface, struct TimeSpan *value )
{
    struct keyframe_animation *impl = impl_from_kfa( iface );
    if (value) *value = impl->duration;
    return S_OK;
}
static HRESULT WINAPI kfa_put_Duration( void *iface, struct TimeSpan value )
{
    struct keyframe_animation *impl = impl_from_kfa( iface );
    TRACE( "iface %p, duration %s\n", iface, wine_dbgstr_longlong( value.Duration ) );
    impl->duration = value;
    return S_OK;
}
static HRESULT WINAPI kfa_get_IterationBehavior( void *iface, UINT32 *value ) { if (value) *value = 0; return S_OK; }
static HRESULT WINAPI kfa_put_IterationBehavior( void *iface, UINT32 value ) { TRACE( "iface %p, val %u\n", iface, value ); return S_OK; }
static HRESULT WINAPI kfa_get_IterationCount( void *iface, INT32 *value )
{
    struct keyframe_animation *impl = impl_from_kfa( iface );
    if (value) *value = impl->iteration_count;
    return S_OK;
}
static HRESULT WINAPI kfa_put_IterationCount( void *iface, INT32 value )
{
    struct keyframe_animation *impl = impl_from_kfa( iface );
    impl->iteration_count = value;
    return S_OK;
}
static HRESULT WINAPI kfa_get_KeyFrameCount( void *iface, INT32 *value ) { if (value) *value = 0; return S_OK; }
static HRESULT WINAPI kfa_InsertExpressionKeyFrame( void *iface, FLOAT key, HSTRING value ) { return S_OK; }
static HRESULT WINAPI kfa_InsertExpressionKeyFrameWithEasingFunction( void *iface, FLOAT key, HSTRING value, void *easing ) { return S_OK; }
static HRESULT WINAPI kfa_get_StopBehavior( void *iface, UINT32 *value ) { if (value) *value = 0; return S_OK; }
static HRESULT WINAPI kfa_put_StopBehavior( void *iface, UINT32 value ) { return S_OK; }

static const void *kfa_vtbl[] =
{
    kfa_QueryInterface,
    kfa_AddRef,
    kfa_Release,
    kfa_GetIids,
    kfa_GetRuntimeClassName,
    kfa_GetTrustLevel,
    kfa_get_DelayTime,
    kfa_put_DelayTime,
    kfa_get_Duration,
    kfa_put_Duration,
    kfa_get_IterationBehavior,
    kfa_put_IterationBehavior,
    kfa_get_IterationCount,
    kfa_put_IterationCount,
    kfa_get_KeyFrameCount,
    kfa_InsertExpressionKeyFrame,
    kfa_InsertExpressionKeyFrameWithEasingFunction,
    kfa_get_StopBehavior,
    kfa_put_StopBehavior,
};

static inline struct keyframe_animation *impl_from_kfa2( void *iface )
{
    return CONTAINING_RECORD( iface, struct keyframe_animation, IKeyFrameAnimation2_iface );
}
static HRESULT WINAPI kfa2_QueryInterface( void *iface, REFIID iid, void **out ) { return keyframe_QueryInterface( &impl_from_kfa2(iface)->ICompositionAnimation_iface, iid, out ); }
static ULONG WINAPI kfa2_AddRef( void *iface ) { return keyframe_AddRef( &impl_from_kfa2(iface)->ICompositionAnimation_iface ); }
static ULONG WINAPI kfa2_Release( void *iface ) { return keyframe_Release( &impl_from_kfa2(iface)->ICompositionAnimation_iface ); }
static HRESULT WINAPI kfa2_GetIids( void *iface, ULONG *c, IID **i ) { return keyframe_GetIids( &impl_from_kfa2(iface)->ICompositionAnimation_iface, c, i ); }
static HRESULT WINAPI kfa2_GetRuntimeClassName( void *iface, HSTRING *n ) { return keyframe_GetRuntimeClassName( &impl_from_kfa2(iface)->ICompositionAnimation_iface, n ); }
static HRESULT WINAPI kfa2_GetTrustLevel( void *iface, TrustLevel *l ) { return keyframe_GetTrustLevel( &impl_from_kfa2(iface)->ICompositionAnimation_iface, l ); }
static HRESULT WINAPI kfa2_get_Direction( void *iface, UINT32 *value ) { TRACE( "iface %p\n", iface ); if (value) *value = 0; return S_OK; }
static HRESULT WINAPI kfa2_put_Direction( void *iface, UINT32 value ) { TRACE( "iface %p, val %u\n", iface, value ); return S_OK; }
static HRESULT WINAPI kfa2_slot8( void *iface, void *a ) { TRACE( "iface %p, a %p\n", iface, a ); return S_OK; }
static HRESULT WINAPI kfa2_slot9( void *iface, void *a ) { TRACE( "iface %p, a %p\n", iface, a ); return S_OK; }
static const void *kfa2_vtbl[] =
{
    kfa2_QueryInterface,
    kfa2_AddRef,
    kfa2_Release,
    kfa2_GetIids,
    kfa2_GetRuntimeClassName,
    kfa2_GetTrustLevel,
    kfa2_get_Direction,
    kfa2_put_Direction,
    kfa2_slot8,
    kfa2_slot9,
};

static inline struct keyframe_animation *impl_from_kfa3( void *iface )
{
    return CONTAINING_RECORD( iface, struct keyframe_animation, IKeyFrameAnimation3_iface );
}
static HRESULT WINAPI kfa3_QueryInterface( void *iface, REFIID iid, void **out ) { return keyframe_QueryInterface( &impl_from_kfa3(iface)->ICompositionAnimation_iface, iid, out ); }
static ULONG WINAPI kfa3_AddRef( void *iface ) { return keyframe_AddRef( &impl_from_kfa3(iface)->ICompositionAnimation_iface ); }
static ULONG WINAPI kfa3_Release( void *iface ) { return keyframe_Release( &impl_from_kfa3(iface)->ICompositionAnimation_iface ); }
static HRESULT WINAPI kfa3_GetIids( void *iface, ULONG *c, IID **i ) { return keyframe_GetIids( &impl_from_kfa3(iface)->ICompositionAnimation_iface, c, i ); }
static HRESULT WINAPI kfa3_GetRuntimeClassName( void *iface, HSTRING *n ) { return keyframe_GetRuntimeClassName( &impl_from_kfa3(iface)->ICompositionAnimation_iface, n ); }
static HRESULT WINAPI kfa3_GetTrustLevel( void *iface, TrustLevel *l ) { return keyframe_GetTrustLevel( &impl_from_kfa3(iface)->ICompositionAnimation_iface, l ); }
static HRESULT WINAPI kfa3_get_DelayBehavior( void *iface, UINT32 *value ) { if (value) *value = 0; return S_OK; }
static HRESULT WINAPI kfa3_put_DelayBehavior( void *iface, UINT32 value ) { return S_OK; }
static const void *kfa3_vtbl[] =
{
    kfa3_QueryInterface,
    kfa3_AddRef,
    kfa3_Release,
    kfa3_GetIids,
    kfa3_GetRuntimeClassName,
    kfa3_GetTrustLevel,
    kfa3_get_DelayBehavior,
    kfa3_put_DelayBehavior,
};

static inline struct keyframe_animation *impl_from_kfa6( void *iface )
{
    return CONTAINING_RECORD( iface, struct keyframe_animation, IKeyFrameAnimation6_iface );
}
static HRESULT WINAPI kfa6_QueryInterface( void *iface, REFIID iid, void **out ) { return keyframe_QueryInterface( &impl_from_kfa6(iface)->ICompositionAnimation_iface, iid, out ); }
static ULONG WINAPI kfa6_AddRef( void *iface ) { return keyframe_AddRef( &impl_from_kfa6(iface)->ICompositionAnimation_iface ); }
static ULONG WINAPI kfa6_Release( void *iface ) { return keyframe_Release( &impl_from_kfa6(iface)->ICompositionAnimation_iface ); }
static HRESULT WINAPI kfa6_GetIids( void *iface, ULONG *c, IID **i ) { return keyframe_GetIids( &impl_from_kfa6(iface)->ICompositionAnimation_iface, c, i ); }
static HRESULT WINAPI kfa6_GetRuntimeClassName( void *iface, HSTRING *n ) { return keyframe_GetRuntimeClassName( &impl_from_kfa6(iface)->ICompositionAnimation_iface, n ); }
static HRESULT WINAPI kfa6_GetTrustLevel( void *iface, TrustLevel *l ) { return keyframe_GetTrustLevel( &impl_from_kfa6(iface)->ICompositionAnimation_iface, l ); }
static HRESULT WINAPI kfa6_stub( void *iface, void *a1, void *a2 ) { TRACE( "iface %p, a1 %p, a2 %p\n", iface, a1, a2 ); return S_OK; }
static const void *kfa6_vtbl[] =
{
    kfa6_QueryInterface,
    kfa6_AddRef,
    kfa6_Release,
    kfa6_GetIids,
    kfa6_GetRuntimeClassName,
    kfa6_GetTrustLevel,
    kfa6_stub,
    kfa6_stub,
    kfa6_stub,
    kfa6_stub,
    kfa6_stub,
    kfa6_stub,
};

DEFINE_INHERITED_PROLOGUE( keyframe_object, ICompositionObject, struct keyframe_animation,
        ICompositionAnimation_iface )
COMP_OBJECT_IMPL( keyframe_object, struct keyframe_animation )

static const struct ICompositionObjectVtbl keyframe_object_vtbl =
{
    keyframe_object_QueryInterface,
    keyframe_object_AddRef,
    keyframe_object_Release,
    keyframe_object_GetIids,
    keyframe_object_GetRuntimeClassName,
    keyframe_object_GetTrustLevel,
    keyframe_object_get_Compositor,
    keyframe_object_get_Dispatcher,
    keyframe_object_get_Properties,
    keyframe_object_StartAnimation,
    keyframe_object_StopAnimation,
};

DEFINE_INHERITED_PROLOGUE( keyframe_closable, IClosable, struct keyframe_animation,
        ICompositionAnimation_iface )
COMP_CLOSABLE_IMPL( keyframe_closable, struct keyframe_animation )

static const struct IClosableVtbl keyframe_closable_vtbl =
{
    keyframe_closable_QueryInterface,
    keyframe_closable_AddRef,
    keyframe_closable_Release,
    keyframe_closable_GetIids,
    keyframe_closable_GetRuntimeClassName,
    keyframe_closable_GetTrustLevel,
    keyframe_closable_Close,
};

struct keyframe_animation *keyframe_animation_create( struct compositor *compositor, enum animation_kind kind )
{
    struct keyframe_animation *impl;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return NULL;
    impl->ICompositionAnimation_iface.lpVtbl = &keyframe_vtbl;
    impl->ICompositionAnimationBase_iface.lpVtbl = &keyframe_base_vtbl;
    impl->IColorKeyFrameAnimation_iface.lpVtbl = &colorkey_vtbl;
    impl->IScalarKeyFrameAnimation_iface.lpVtbl = &scalarkey_vtbl;
    impl->IVector2KeyFrameAnimation_iface.lpVtbl = &vector2key_vtbl;
    impl->IVector3KeyFrameAnimation_iface.lpVtbl = &vector3key_vtbl;
    impl->IVector4KeyFrameAnimation_iface.lpVtbl = &vector4key_vtbl;
    impl->IQuaternionKeyFrameAnimation_iface.lpVtbl = &quatkey_vtbl;
    impl->IBooleanKeyFrameAnimation_iface.lpVtbl = (const struct IInspectableVtbl *)booleankey_vtbl;
    impl->IPathKeyFrameAnimation_iface.lpVtbl = (const struct IInspectableVtbl *)pathkey_vtbl;
    impl->IKeyFrameAnimation_iface.lpVtbl = (const struct IInspectableVtbl *)kfa_vtbl;
    impl->IKeyFrameAnimation2_iface.lpVtbl = (const struct IInspectableVtbl *)kfa2_vtbl;
    impl->IKeyFrameAnimation3_iface.lpVtbl = (const struct IInspectableVtbl *)kfa3_vtbl;
    impl->IKeyFrameAnimation6_iface.lpVtbl = (const struct IInspectableVtbl *)kfa6_vtbl;
    impl->ICompositionObject_iface.lpVtbl = &keyframe_object_vtbl;
    impl->IClosable_iface.lpVtbl = &keyframe_closable_vtbl;
    impl->ref = 1;
    impl->compositor = compositor;
    impl->kind = kind;
    impl->iteration_count = 1;
    if (compositor) IInspectable_AddRef( (IInspectable *)&compositor->ICompositor_iface );
    return impl;
}

/*
 * Expression animation.
 */

static inline struct expression_animation *impl_from_expression_animation( struct ICompositionAnimation *iface )
{
    return CONTAINING_RECORD( iface, struct expression_animation, ICompositionAnimation_iface );
}

static HRESULT WINAPI expression_QueryInterface( struct ICompositionAnimation *iface, REFIID iid, void **out )
{
    struct expression_animation *impl = impl_from_expression_animation( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown )
        || IsEqualGUID( iid, &IID_IInspectable )
        || IsEqualGUID( iid, &IID_IAgileObject )
        || IsEqualGUID( iid, &IID_ICompositionAnimation ))
    {
        *out = &impl->ICompositionAnimation_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_ICompositionAnimationBase ))
    {
        *out = &impl->ICompositionAnimationBase_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IExpressionAnimation ))
    {
        *out = &impl->IExpressionAnimation_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_ICompositionObject ))
    {
        *out = &impl->ICompositionObject_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IKeyFrameAnimation5_local ))
    {
        *out = &impl->ICompositionAnimation5_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    {
        static const GUID IID_ICompositionObject2_local =
            { 0x3052b611, 0x56c3, 0x4c3e, { 0x8b, 0xf3, 0xf6, 0xe1, 0xad, 0x47, 0x3f, 0x06 } };
        if (IsEqualGUID( iid, &IID_ICompositionObject2_local ))
        {
            *out = &impl->ICompositionObject2_iface;
            IInspectable_AddRef( (IInspectable *)*out );
            return S_OK;
        }
    }
    if (IsEqualGUID( iid, &IID_IClosable ))
    {
        *out = &impl->IClosable_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI expression_AddRef( struct ICompositionAnimation *iface )
{
    struct expression_animation *impl = impl_from_expression_animation( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI expression_Release( struct ICompositionAnimation *iface )
{
    struct expression_animation *impl = impl_from_expression_animation( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        WindowsDeleteString( impl->expression );
        comp_release_refs( impl->compositor, impl->properties );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI expression_GetIids( struct ICompositionAnimation *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI expression_GetRuntimeClassName( struct ICompositionAnimation *iface, HSTRING *class_name )
{
    return WindowsCreateString( RuntimeClass_Windows_UI_Composition_ExpressionAnimation,
            ARRAYSIZE( RuntimeClass_Windows_UI_Composition_ExpressionAnimation ) - 1, class_name );
}

static HRESULT WINAPI expression_GetTrustLevel( struct ICompositionAnimation *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

COMP_ANIM_METHODS( expression )

static const struct ICompositionAnimationVtbl expression_vtbl =
{
    expression_QueryInterface,
    expression_AddRef,
    expression_Release,
    expression_GetIids,
    expression_GetRuntimeClassName,
    expression_GetTrustLevel,
    expression_ClearAllParameters,
    expression_ClearParameter,
    expression_SetColorParameter,
    expression_SetMatrix3x2Parameter,
    expression_SetMatrix4x4Parameter,
    expression_SetQuaternionParameter,
    expression_SetReferenceParameter,
    expression_SetScalarParameter,
    expression_SetVector2Parameter,
    expression_SetVector3Parameter,
    expression_SetVector4Parameter,
};

DEFINE_INHERITED_PROLOGUE( expression_base, ICompositionAnimationBase, struct expression_animation,
        ICompositionAnimation_iface )

static const struct ICompositionAnimationBaseVtbl expression_base_vtbl =
{
    expression_base_QueryInterface,
    expression_base_AddRef,
    expression_base_Release,
    expression_base_GetIids,
    expression_base_GetRuntimeClassName,
    expression_base_GetTrustLevel,
};

DEFINE_INHERITED_PROLOGUE( expression_iface, IExpressionAnimation, struct expression_animation,
        ICompositionAnimation_iface )

static HRESULT WINAPI expression_iface_get_Expression( struct IExpressionAnimation *iface, HSTRING *value )
{
    struct expression_animation *impl = impl_from_expression_iface( iface );

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    return WindowsDuplicateString( impl->expression, value );
}

static HRESULT WINAPI expression_iface_put_Expression( struct IExpressionAnimation *iface, HSTRING value )
{
    struct expression_animation *impl = impl_from_expression_iface( iface );
    HSTRING copy;

    TRACE( "iface %p, value %s.\n", iface, debugstr_hstring( value ) );

    WindowsDuplicateString( value, &copy );
    WindowsDeleteString( impl->expression );
    impl->expression = copy;
    return S_OK;
}

static const struct IExpressionAnimationVtbl expression_iface_vtbl =
{
    expression_iface_QueryInterface,
    expression_iface_AddRef,
    expression_iface_Release,
    expression_iface_GetIids,
    expression_iface_GetRuntimeClassName,
    expression_iface_GetTrustLevel,
    expression_iface_get_Expression,
    expression_iface_put_Expression,
};

DEFINE_INHERITED_PROLOGUE( expression_object, ICompositionObject, struct expression_animation,
        ICompositionAnimation_iface )
COMP_OBJECT_IMPL( expression_object, struct expression_animation )

static const struct ICompositionObjectVtbl expression_object_vtbl =
{
    expression_object_QueryInterface,
    expression_object_AddRef,
    expression_object_Release,
    expression_object_GetIids,
    expression_object_GetRuntimeClassName,
    expression_object_GetTrustLevel,
    expression_object_get_Compositor,
    expression_object_get_Dispatcher,
    expression_object_get_Properties,
    expression_object_StartAnimation,
    expression_object_StopAnimation,
};

DEFINE_INHERITED_PROLOGUE( expression_closable, IClosable, struct expression_animation,
        ICompositionAnimation_iface )
COMP_CLOSABLE_IMPL( expression_closable, struct expression_animation )

static HRESULT WINAPI expression_anim5_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct expression_animation *impl = CONTAINING_RECORD( iface, struct expression_animation, ICompositionAnimation5_iface );
    return expression_QueryInterface( &impl->ICompositionAnimation_iface, iid, out );
}
static ULONG WINAPI expression_anim5_AddRef( void *iface )
{
    struct expression_animation *impl = CONTAINING_RECORD( iface, struct expression_animation, ICompositionAnimation5_iface );
    return expression_AddRef( &impl->ICompositionAnimation_iface );
}
static ULONG WINAPI expression_anim5_Release( void *iface )
{
    struct expression_animation *impl = CONTAINING_RECORD( iface, struct expression_animation, ICompositionAnimation5_iface );
    return expression_Release( &impl->ICompositionAnimation_iface );
}
static HRESULT WINAPI expression_anim5_GetIids( void *iface, ULONG *c, IID **i )
{
    struct expression_animation *impl = CONTAINING_RECORD( iface, struct expression_animation, ICompositionAnimation5_iface );
    return expression_GetIids( &impl->ICompositionAnimation_iface, c, i );
}
static HRESULT WINAPI expression_anim5_GetRuntimeClassName( void *iface, HSTRING *n )
{
    struct expression_animation *impl = CONTAINING_RECORD( iface, struct expression_animation, ICompositionAnimation5_iface );
    return expression_GetRuntimeClassName( &impl->ICompositionAnimation_iface, n );
}
static HRESULT WINAPI expression_anim5_GetTrustLevel( void *iface, TrustLevel *l )
{
    struct expression_animation *impl = CONTAINING_RECORD( iface, struct expression_animation, ICompositionAnimation5_iface );
    return expression_GetTrustLevel( &impl->ICompositionAnimation_iface, l );
}
static HRESULT WINAPI expression_anim5_stub( void *iface, void *a1, void *a2 )
{
    return S_OK;
}
static const void *expression_anim5_vtbl[] =
{
    expression_anim5_QueryInterface,
    expression_anim5_AddRef,
    expression_anim5_Release,
    expression_anim5_GetIids,
    expression_anim5_GetRuntimeClassName,
    expression_anim5_GetTrustLevel,
    expression_anim5_stub,
    expression_anim5_stub,
    expression_anim5_stub,
    expression_anim5_stub,
    expression_anim5_stub,
    expression_anim5_stub,
    expression_anim5_stub,
    expression_anim5_stub,
};

static const struct IClosableVtbl expression_closable_vtbl =
{
    expression_closable_QueryInterface,
    expression_closable_AddRef,
    expression_closable_Release,
    expression_closable_GetIids,
    expression_closable_GetRuntimeClassName,
    expression_closable_GetTrustLevel,
    expression_closable_Close,
};

struct expression_animation *expression_animation_create( struct compositor *compositor, HSTRING expression )
{
    struct expression_animation *impl;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return NULL;
    impl->ICompositionAnimation_iface.lpVtbl = &expression_vtbl;
    impl->ICompositionAnimationBase_iface.lpVtbl = &expression_base_vtbl;
    impl->IExpressionAnimation_iface.lpVtbl = &expression_iface_vtbl;
    impl->ICompositionObject_iface.lpVtbl = &expression_object_vtbl;
    impl->ICompositionObject2_iface.lpVtbl = expression_anim5_vtbl;
    impl->ICompositionAnimation5_iface.lpVtbl = expression_anim5_vtbl;
    impl->IClosable_iface.lpVtbl = &expression_closable_vtbl;
    impl->ref = 1;
    impl->compositor = compositor;
    WindowsDuplicateString( expression, &impl->expression );
    if (compositor) IInspectable_AddRef( (IInspectable *)&compositor->ICompositor_iface );
    return impl;
}

/*
 * Easing functions: CubicBezierEasingFunction and LinearEasingFunction share
 * one object; the QI chain answers the derived interface matching the kind.
 */

static inline struct easing_function *impl_from_ICompositionEasingFunction(
        struct ICompositionEasingFunction *iface )
{
    return CONTAINING_RECORD( iface, struct easing_function, ICompositionEasingFunction_iface );
}

static HRESULT WINAPI easing_QueryInterface( struct ICompositionEasingFunction *iface, REFIID iid, void **out )
{
    struct easing_function *impl = impl_from_ICompositionEasingFunction( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown )
        || IsEqualGUID( iid, &IID_IInspectable )
        || IsEqualGUID( iid, &IID_IAgileObject )
        || IsEqualGUID( iid, &IID_ICompositionEasingFunction ))
    {
        *out = &impl->ICompositionEasingFunction_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_ICubicBezierEasingFunction ))
    {
        if (!impl->is_cubic)
        {
            *out = NULL;
            return E_NOINTERFACE;
        }
        *out = &impl->ICubicBezierEasingFunction_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_ILinearEasingFunction ))
    {
        if (impl->is_cubic)
        {
            *out = NULL;
            return E_NOINTERFACE;
        }
        *out = &impl->ILinearEasingFunction_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_ICompositionObject ))
    {
        *out = &impl->ICompositionObject_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IClosable ))
    {
        *out = &impl->IClosable_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI easing_AddRef( struct ICompositionEasingFunction *iface )
{
    struct easing_function *impl = impl_from_ICompositionEasingFunction( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI easing_Release( struct ICompositionEasingFunction *iface )
{
    struct easing_function *impl = impl_from_ICompositionEasingFunction( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        comp_release_refs( impl->compositor, impl->properties );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI easing_GetIids( struct ICompositionEasingFunction *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI easing_GetRuntimeClassName( struct ICompositionEasingFunction *iface, HSTRING *class_name )
{
    struct easing_function *impl = impl_from_ICompositionEasingFunction( iface );
    const WCHAR *name = impl->is_cubic ? RuntimeClass_Windows_UI_Composition_CubicBezierEasingFunction
            : RuntimeClass_Windows_UI_Composition_LinearEasingFunction;

    return WindowsCreateString( name, lstrlenW( name ), class_name );
}

static HRESULT WINAPI easing_GetTrustLevel( struct ICompositionEasingFunction *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static const struct ICompositionEasingFunctionVtbl easing_vtbl =
{
    easing_QueryInterface,
    easing_AddRef,
    easing_Release,
    easing_GetIids,
    easing_GetRuntimeClassName,
    easing_GetTrustLevel,
};

DEFINE_INHERITED_PROLOGUE( cubic, ICubicBezierEasingFunction, struct easing_function,
        ICompositionEasingFunction_iface )

static HRESULT WINAPI cubic_get_ControlPoint1( struct ICubicBezierEasingFunction *iface, Vector2 *value )
{
    struct easing_function *impl = impl_from_cubic( iface );
    if (!value) return E_POINTER;
    *value = impl->control_point1;
    return S_OK;
}

static HRESULT WINAPI cubic_get_ControlPoint2( struct ICubicBezierEasingFunction *iface, Vector2 *value )
{
    struct easing_function *impl = impl_from_cubic( iface );
    if (!value) return E_POINTER;
    *value = impl->control_point2;
    return S_OK;
}

static const struct ICubicBezierEasingFunctionVtbl cubic_vtbl =
{
    cubic_QueryInterface,
    cubic_AddRef,
    cubic_Release,
    cubic_GetIids,
    cubic_GetRuntimeClassName,
    cubic_GetTrustLevel,
    cubic_get_ControlPoint1,
    cubic_get_ControlPoint2,
};

DEFINE_INHERITED_PROLOGUE( linear, ILinearEasingFunction, struct easing_function,
        ICompositionEasingFunction_iface )

static const struct ILinearEasingFunctionVtbl linear_vtbl =
{
    linear_QueryInterface,
    linear_AddRef,
    linear_Release,
    linear_GetIids,
    linear_GetRuntimeClassName,
    linear_GetTrustLevel,
};

DEFINE_INHERITED_PROLOGUE( easing_object, ICompositionObject, struct easing_function,
        ICompositionEasingFunction_iface )
COMP_OBJECT_IMPL( easing_object, struct easing_function )

static const struct ICompositionObjectVtbl easing_object_vtbl =
{
    easing_object_QueryInterface,
    easing_object_AddRef,
    easing_object_Release,
    easing_object_GetIids,
    easing_object_GetRuntimeClassName,
    easing_object_GetTrustLevel,
    easing_object_get_Compositor,
    easing_object_get_Dispatcher,
    easing_object_get_Properties,
    easing_object_StartAnimation,
    easing_object_StopAnimation,
};

DEFINE_INHERITED_PROLOGUE( easing_closable, IClosable, struct easing_function,
        ICompositionEasingFunction_iface )
COMP_CLOSABLE_IMPL( easing_closable, struct easing_function )

static const struct IClosableVtbl easing_closable_vtbl =
{
    easing_closable_QueryInterface,
    easing_closable_AddRef,
    easing_closable_Release,
    easing_closable_GetIids,
    easing_closable_GetRuntimeClassName,
    easing_closable_GetTrustLevel,
    easing_closable_Close,
};

static struct easing_function *easing_function_create( struct compositor *compositor, BOOL is_cubic,
        Vector2 point1, Vector2 point2 )
{
    struct easing_function *impl;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return NULL;
    impl->ICompositionEasingFunction_iface.lpVtbl = &easing_vtbl;
    impl->ICubicBezierEasingFunction_iface.lpVtbl = &cubic_vtbl;
    impl->ILinearEasingFunction_iface.lpVtbl = &linear_vtbl;
    impl->ICompositionObject_iface.lpVtbl = &easing_object_vtbl;
    impl->IClosable_iface.lpVtbl = &easing_closable_vtbl;
    impl->ref = 1;
    impl->compositor = compositor;
    impl->is_cubic = is_cubic;
    impl->control_point1 = point1;
    impl->control_point2 = point2;
    if (compositor) IInspectable_AddRef( (IInspectable *)&compositor->ICompositor_iface );
    return impl;
}

struct easing_function *cubic_bezier_easing_create( struct compositor *compositor, Vector2 point1, Vector2 point2 )
{
    return easing_function_create( compositor, TRUE, point1, point2 );
}

struct easing_function *linear_easing_create( struct compositor *compositor )
{
    Vector2 zero = { 0, 0 };

    return easing_function_create( compositor, FALSE, zero, zero );
}

/*
 * InsetClip.
 */

static inline struct inset_clip *impl_from_ICompositionClip( struct ICompositionClip *iface )
{
    return CONTAINING_RECORD( iface, struct inset_clip, ICompositionClip_iface );
}

static HRESULT WINAPI inset_clip_QueryInterface( struct ICompositionClip *iface, REFIID iid, void **out )
{
    struct inset_clip *impl = impl_from_ICompositionClip( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown )
        || IsEqualGUID( iid, &IID_IInspectable )
        || IsEqualGUID( iid, &IID_IAgileObject )
        || IsEqualGUID( iid, &IID_ICompositionClip ))
    {
        *out = &impl->ICompositionClip_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IInsetClip ))
    {
        *out = &impl->IInsetClip_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_ICompositionObject ))
    {
        *out = &impl->ICompositionObject_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IClosable ))
    {
        *out = &impl->IClosable_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI inset_clip_AddRef( struct ICompositionClip *iface )
{
    struct inset_clip *impl = impl_from_ICompositionClip( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI inset_clip_Release( struct ICompositionClip *iface )
{
    struct inset_clip *impl = impl_from_ICompositionClip( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        comp_release_refs( impl->compositor, impl->properties );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI inset_clip_GetIids( struct ICompositionClip *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI inset_clip_GetRuntimeClassName( struct ICompositionClip *iface, HSTRING *class_name )
{
    return WindowsCreateString( RuntimeClass_Windows_UI_Composition_InsetClip,
            ARRAYSIZE( RuntimeClass_Windows_UI_Composition_InsetClip ) - 1, class_name );
}

static HRESULT WINAPI inset_clip_GetTrustLevel( struct ICompositionClip *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static const struct ICompositionClipVtbl inset_clip_vtbl =
{
    inset_clip_QueryInterface,
    inset_clip_AddRef,
    inset_clip_Release,
    inset_clip_GetIids,
    inset_clip_GetRuntimeClassName,
    inset_clip_GetTrustLevel,
};

DEFINE_INHERITED_PROLOGUE( inset_iface, IInsetClip, struct inset_clip, ICompositionClip_iface )

static HRESULT WINAPI inset_iface_get_BottomInset( struct IInsetClip *iface, FLOAT *value )
{
    struct inset_clip *impl = impl_from_inset_iface( iface );
    if (!value) return E_POINTER;
    *value = impl->bottom;
    return S_OK;
}

static HRESULT WINAPI inset_iface_put_BottomInset( struct IInsetClip *iface, FLOAT value )
{
    struct inset_clip *impl = impl_from_inset_iface( iface );
    impl->bottom = value;
    return S_OK;
}

static HRESULT WINAPI inset_iface_get_LeftInset( struct IInsetClip *iface, FLOAT *value )
{
    struct inset_clip *impl = impl_from_inset_iface( iface );
    if (!value) return E_POINTER;
    *value = impl->left;
    return S_OK;
}

static HRESULT WINAPI inset_iface_put_LeftInset( struct IInsetClip *iface, FLOAT value )
{
    struct inset_clip *impl = impl_from_inset_iface( iface );
    impl->left = value;
    return S_OK;
}

static HRESULT WINAPI inset_iface_get_RightInset( struct IInsetClip *iface, FLOAT *value )
{
    struct inset_clip *impl = impl_from_inset_iface( iface );
    if (!value) return E_POINTER;
    *value = impl->right;
    return S_OK;
}

static HRESULT WINAPI inset_iface_put_RightInset( struct IInsetClip *iface, FLOAT value )
{
    struct inset_clip *impl = impl_from_inset_iface( iface );
    impl->right = value;
    return S_OK;
}

static HRESULT WINAPI inset_iface_get_TopInset( struct IInsetClip *iface, FLOAT *value )
{
    struct inset_clip *impl = impl_from_inset_iface( iface );
    if (!value) return E_POINTER;
    *value = impl->top;
    return S_OK;
}

static HRESULT WINAPI inset_iface_put_TopInset( struct IInsetClip *iface, FLOAT value )
{
    struct inset_clip *impl = impl_from_inset_iface( iface );
    impl->top = value;
    return S_OK;
}

static const struct IInsetClipVtbl inset_iface_vtbl =
{
    inset_iface_QueryInterface,
    inset_iface_AddRef,
    inset_iface_Release,
    inset_iface_GetIids,
    inset_iface_GetRuntimeClassName,
    inset_iface_GetTrustLevel,
    inset_iface_get_BottomInset,
    inset_iface_put_BottomInset,
    inset_iface_get_LeftInset,
    inset_iface_put_LeftInset,
    inset_iface_get_RightInset,
    inset_iface_put_RightInset,
    inset_iface_get_TopInset,
    inset_iface_put_TopInset,
};

DEFINE_INHERITED_PROLOGUE( inset_object, ICompositionObject, struct inset_clip, ICompositionClip_iface )
COMP_OBJECT_IMPL( inset_object, struct inset_clip )

static const struct ICompositionObjectVtbl inset_object_vtbl =
{
    inset_object_QueryInterface,
    inset_object_AddRef,
    inset_object_Release,
    inset_object_GetIids,
    inset_object_GetRuntimeClassName,
    inset_object_GetTrustLevel,
    inset_object_get_Compositor,
    inset_object_get_Dispatcher,
    inset_object_get_Properties,
    inset_object_StartAnimation,
    inset_object_StopAnimation,
};

DEFINE_INHERITED_PROLOGUE( inset_closable, IClosable, struct inset_clip, ICompositionClip_iface )
COMP_CLOSABLE_IMPL( inset_closable, struct inset_clip )

static const struct IClosableVtbl inset_closable_vtbl =
{
    inset_closable_QueryInterface,
    inset_closable_AddRef,
    inset_closable_Release,
    inset_closable_GetIids,
    inset_closable_GetRuntimeClassName,
    inset_closable_GetTrustLevel,
    inset_closable_Close,
};

struct inset_clip *inset_clip_create( struct compositor *compositor, FLOAT left, FLOAT top, FLOAT right,
        FLOAT bottom )
{
    struct inset_clip *impl;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return NULL;
    impl->ICompositionClip_iface.lpVtbl = &inset_clip_vtbl;
    impl->IInsetClip_iface.lpVtbl = &inset_iface_vtbl;
    impl->ICompositionObject_iface.lpVtbl = &inset_object_vtbl;
    impl->IClosable_iface.lpVtbl = &inset_closable_vtbl;
    impl->ref = 1;
    impl->compositor = compositor;
    impl->left = left;
    impl->top = top;
    impl->right = right;
    impl->bottom = bottom;
    if (compositor) IInspectable_AddRef( (IInspectable *)&compositor->ICompositor_iface );
    return impl;
}

/*
 * CompositionGeometricClip.
 *
 * Compositor::CreateGeometricClip (ICompositor6, UniversalApiContract 7.0)
 * returns this object, and Compositor::CreateGeometricClipWithGeometry seeds
 * its Geometry. The app-visible default interface is
 * ICompositionGeometricClip {c840b581-81c9-4444-a2c1-ccaece3a50e5}, which is
 * not in include/windows.ui.composition.idl; like ICompositor5 it is answered
 * through a struct IInspectable plus a function-pointer vtable. Slots after
 * the IInspectable prologue are Geometry/get, Geometry/put, ViewBox/get,
 * ViewBox/put. The clip shape is not rendered (the DirectComposition bridge has
 * no geometry path), so Geometry and ViewBox round-trip the values the app
 * stores on them and stay empty otherwise.
 */

static const GUID IID_ICompositionGeometricClip_local =
    { 0xc840b581, 0x81c9, 0x4444, { 0xa2, 0xc1, 0xcc, 0xae, 0xce, 0x3a, 0x50, 0xe5 } };

static inline struct geometric_clip *impl_from_geometric_clip( struct ICompositionClip *iface )
{
    return CONTAINING_RECORD( iface, struct geometric_clip, ICompositionClip_iface );
}

static HRESULT WINAPI geometric_clip_QueryInterface( struct ICompositionClip *iface, REFIID iid, void **out )
{
    struct geometric_clip *impl = impl_from_geometric_clip( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (!out) return E_POINTER;

    if (IsEqualGUID( iid, &IID_IUnknown )
        || IsEqualGUID( iid, &IID_IInspectable )
        || IsEqualGUID( iid, &IID_IAgileObject )
        || IsEqualGUID( iid, &IID_ICompositionClip ))
    {
        *out = &impl->ICompositionClip_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_ICompositionGeometricClip_local ))
    {
        *out = &impl->ICompositionGeometricClip_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_ICompositionObject ))
    {
        *out = &impl->ICompositionObject_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IClosable ))
    {
        *out = &impl->IClosable_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI geometric_clip_AddRef( struct ICompositionClip *iface )
{
    struct geometric_clip *impl = impl_from_geometric_clip( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI geometric_clip_Release( struct ICompositionClip *iface )
{
    struct geometric_clip *impl = impl_from_geometric_clip( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        if (impl->geometry) IInspectable_Release( (IInspectable *)impl->geometry );
        if (impl->view_box) IInspectable_Release( (IInspectable *)impl->view_box );
        comp_release_refs( impl->compositor, impl->properties );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI geometric_clip_GetIids( struct ICompositionClip *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI geometric_clip_GetRuntimeClassName( struct ICompositionClip *iface, HSTRING *class_name )
{
    return WindowsCreateString( L"Windows.UI.Composition.CompositionGeometricClip",
            ARRAYSIZE( L"Windows.UI.Composition.CompositionGeometricClip" ) - 1, class_name );
}

static HRESULT WINAPI geometric_clip_GetTrustLevel( struct ICompositionClip *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static const struct ICompositionClipVtbl geometric_clip_vtbl =
{
    geometric_clip_QueryInterface,
    geometric_clip_AddRef,
    geometric_clip_Release,
    geometric_clip_GetIids,
    geometric_clip_GetRuntimeClassName,
    geometric_clip_GetTrustLevel,
};

static inline struct geometric_clip *impl_from_geometric_iface( void *iface )
{
    return CONTAINING_RECORD( iface, struct geometric_clip, ICompositionGeometricClip_iface );
}

static HRESULT WINAPI geometric_iface_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct geometric_clip *impl = impl_from_geometric_iface( iface );
    return geometric_clip_QueryInterface( &impl->ICompositionClip_iface, iid, out );
}
static ULONG WINAPI geometric_iface_AddRef( void *iface )
{
    struct geometric_clip *impl = impl_from_geometric_iface( iface );
    return geometric_clip_AddRef( &impl->ICompositionClip_iface );
}
static ULONG WINAPI geometric_iface_Release( void *iface )
{
    struct geometric_clip *impl = impl_from_geometric_iface( iface );
    return geometric_clip_Release( &impl->ICompositionClip_iface );
}
static HRESULT WINAPI geometric_iface_GetIids( void *iface, ULONG *c, IID **i )
{
    struct geometric_clip *impl = impl_from_geometric_iface( iface );
    return geometric_clip_GetIids( &impl->ICompositionClip_iface, c, i );
}
static HRESULT WINAPI geometric_iface_GetRuntimeClassName( void *iface, HSTRING *n )
{
    struct geometric_clip *impl = impl_from_geometric_iface( iface );
    return geometric_clip_GetRuntimeClassName( &impl->ICompositionClip_iface, n );
}
static HRESULT WINAPI geometric_iface_GetTrustLevel( void *iface, TrustLevel *l )
{
    struct geometric_clip *impl = impl_from_geometric_iface( iface );
    return geometric_clip_GetTrustLevel( &impl->ICompositionClip_iface, l );
}

static HRESULT WINAPI geometric_iface_get_Geometry( void *iface, void **value )
{
    struct geometric_clip *impl = impl_from_geometric_iface( iface );

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    *value = impl->geometry;
    if (*value) IInspectable_AddRef( (IInspectable *)*value );
    return S_OK;
}

static HRESULT WINAPI geometric_iface_put_Geometry( void *iface, void *value )
{
    struct geometric_clip *impl = impl_from_geometric_iface( iface );

    TRACE( "iface %p, value %p.\n", iface, value );

    if (value) IInspectable_AddRef( (IInspectable *)value );
    if (impl->geometry) IInspectable_Release( (IInspectable *)impl->geometry );
    impl->geometry = value;
    return S_OK;
}

static HRESULT WINAPI geometric_iface_get_ViewBox( void *iface, void **value )
{
    struct geometric_clip *impl = impl_from_geometric_iface( iface );

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    *value = impl->view_box;
    if (*value) IInspectable_AddRef( (IInspectable *)*value );
    return S_OK;
}

static HRESULT WINAPI geometric_iface_put_ViewBox( void *iface, void *value )
{
    struct geometric_clip *impl = impl_from_geometric_iface( iface );

    TRACE( "iface %p, value %p.\n", iface, value );

    if (value) IInspectable_AddRef( (IInspectable *)value );
    if (impl->view_box) IInspectable_Release( (IInspectable *)impl->view_box );
    impl->view_box = value;
    return S_OK;
}

/* IInspectable prologue + Geometry get/put + ViewBox get/put. */
static const void *geometric_iface_vtbl[] =
{
    geometric_iface_QueryInterface,
    geometric_iface_AddRef,
    geometric_iface_Release,
    geometric_iface_GetIids,
    geometric_iface_GetRuntimeClassName,
    geometric_iface_GetTrustLevel,
    geometric_iface_get_Geometry,
    geometric_iface_put_Geometry,
    geometric_iface_get_ViewBox,
    geometric_iface_put_ViewBox,
};

DEFINE_INHERITED_PROLOGUE( geometric_object, ICompositionObject, struct geometric_clip, ICompositionClip_iface )
COMP_OBJECT_IMPL( geometric_object, struct geometric_clip )

static const struct ICompositionObjectVtbl geometric_object_vtbl =
{
    geometric_object_QueryInterface,
    geometric_object_AddRef,
    geometric_object_Release,
    geometric_object_GetIids,
    geometric_object_GetRuntimeClassName,
    geometric_object_GetTrustLevel,
    geometric_object_get_Compositor,
    geometric_object_get_Dispatcher,
    geometric_object_get_Properties,
    geometric_object_StartAnimation,
    geometric_object_StopAnimation,
};

DEFINE_INHERITED_PROLOGUE( geometric_closable, IClosable, struct geometric_clip, ICompositionClip_iface )
COMP_CLOSABLE_IMPL( geometric_closable, struct geometric_clip )

static const struct IClosableVtbl geometric_closable_vtbl =
{
    geometric_closable_QueryInterface,
    geometric_closable_AddRef,
    geometric_closable_Release,
    geometric_closable_GetIids,
    geometric_closable_GetRuntimeClassName,
    geometric_closable_GetTrustLevel,
    geometric_closable_Close,
};

struct geometric_clip *geometric_clip_create( struct compositor *compositor, void *geometry )
{
    struct geometric_clip *impl;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return NULL;
    impl->ICompositionClip_iface.lpVtbl = &geometric_clip_vtbl;
    impl->ICompositionGeometricClip_iface.lpVtbl = (const struct IInspectableVtbl *)geometric_iface_vtbl;
    impl->ICompositionObject_iface.lpVtbl = &geometric_object_vtbl;
    impl->IClosable_iface.lpVtbl = &geometric_closable_vtbl;
    impl->ref = 1;
    impl->compositor = compositor;
    if (geometry)
    {
        IInspectable_AddRef( (IInspectable *)geometry );
        impl->geometry = geometry;
    }
    if (compositor) IInspectable_AddRef( (IInspectable *)&compositor->ICompositor_iface );
    return impl;
}

/*
 * ColorBrush.
 */

static inline struct color_brush *impl_from_ICompositionBrush_color( struct ICompositionBrush *iface )
{
    return CONTAINING_RECORD( iface, struct color_brush, ICompositionBrush_iface );
}

static HRESULT WINAPI color_brush_QueryInterface( struct ICompositionBrush *iface, REFIID iid, void **out )
{
    struct color_brush *impl = impl_from_ICompositionBrush_color( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown )
        || IsEqualGUID( iid, &IID_IInspectable )
        || IsEqualGUID( iid, &IID_IAgileObject )
        || IsEqualGUID( iid, &IID_ICompositionBrush ))
    {
        *out = &impl->ICompositionBrush_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_ICompositionColorBrush ))
    {
        *out = &impl->ICompositionColorBrush_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_ICompositionObject ))
    {
        *out = &impl->ICompositionObject_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IClosable ))
    {
        *out = &impl->IClosable_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI color_brush_AddRef( struct ICompositionBrush *iface )
{
    struct color_brush *impl = impl_from_ICompositionBrush_color( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI color_brush_Release( struct ICompositionBrush *iface )
{
    struct color_brush *impl = impl_from_ICompositionBrush_color( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        comp_release_refs( impl->compositor, impl->properties );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI color_brush_GetIids( struct ICompositionBrush *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI color_brush_GetRuntimeClassName( struct ICompositionBrush *iface, HSTRING *class_name )
{
    return WindowsCreateString( RuntimeClass_Windows_UI_Composition_CompositionColorBrush,
            ARRAYSIZE( RuntimeClass_Windows_UI_Composition_CompositionColorBrush ) - 1, class_name );
}

static HRESULT WINAPI color_brush_GetTrustLevel( struct ICompositionBrush *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static const struct ICompositionBrushVtbl color_brush_vtbl =
{
    color_brush_QueryInterface,
    color_brush_AddRef,
    color_brush_Release,
    color_brush_GetIids,
    color_brush_GetRuntimeClassName,
    color_brush_GetTrustLevel,
};

DEFINE_INHERITED_PROLOGUE( cb_iface, ICompositionColorBrush, struct color_brush, ICompositionBrush_iface )

static HRESULT WINAPI cb_iface_get_Color( struct ICompositionColorBrush *iface, Color *value )
{
    struct color_brush *impl = impl_from_cb_iface( iface );
    if (!value) return E_POINTER;
    *value = impl->color;
    return S_OK;
}

static HRESULT WINAPI cb_iface_put_Color( struct ICompositionColorBrush *iface, Color value )
{
    struct color_brush *impl = impl_from_cb_iface( iface );
    impl->color = value;
    return S_OK;
}

static const struct ICompositionColorBrushVtbl cb_iface_vtbl =
{
    cb_iface_QueryInterface,
    cb_iface_AddRef,
    cb_iface_Release,
    cb_iface_GetIids,
    cb_iface_GetRuntimeClassName,
    cb_iface_GetTrustLevel,
    cb_iface_get_Color,
    cb_iface_put_Color,
};

DEFINE_INHERITED_PROLOGUE( cb_object, ICompositionObject, struct color_brush, ICompositionBrush_iface )
COMP_OBJECT_IMPL( cb_object, struct color_brush )

static const struct ICompositionObjectVtbl cb_object_vtbl =
{
    cb_object_QueryInterface,
    cb_object_AddRef,
    cb_object_Release,
    cb_object_GetIids,
    cb_object_GetRuntimeClassName,
    cb_object_GetTrustLevel,
    cb_object_get_Compositor,
    cb_object_get_Dispatcher,
    cb_object_get_Properties,
    cb_object_StartAnimation,
    cb_object_StopAnimation,
};

DEFINE_INHERITED_PROLOGUE( cb_closable, IClosable, struct color_brush, ICompositionBrush_iface )
COMP_CLOSABLE_IMPL( cb_closable, struct color_brush )

static const struct IClosableVtbl cb_closable_vtbl =
{
    cb_closable_QueryInterface,
    cb_closable_AddRef,
    cb_closable_Release,
    cb_closable_GetIids,
    cb_closable_GetRuntimeClassName,
    cb_closable_GetTrustLevel,
    cb_closable_Close,
};

struct color_brush *color_brush_create( struct compositor *compositor, Color color )
{
    struct color_brush *impl;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return NULL;
    impl->ICompositionBrush_iface.lpVtbl = &color_brush_vtbl;
    impl->ICompositionColorBrush_iface.lpVtbl = &cb_iface_vtbl;
    impl->ICompositionObject_iface.lpVtbl = &cb_object_vtbl;
    impl->IClosable_iface.lpVtbl = &cb_closable_vtbl;
    impl->ref = 1;
    impl->compositor = compositor;
    impl->color = color;
    if (compositor) IInspectable_AddRef( (IInspectable *)&compositor->ICompositor_iface );
    return impl;
}

/*
 * EffectFactory + EffectBrush.
 *
 * The WinAppSDK titlebar code compiles its caption-button glyph effect and
 * creates a CompositionEffectFactory from it, then takes an effect brush and
 * binds source parameters on it.  There is no GPU effect graph on the
 * DirectComposition compositor, so the factory reports a successful load and
 * hands out a functional brush object that records its source parameters; the
 * brush is accepted by visuals like any other composition brush (same
 * accept-and-discard policy as the animation objects in this file).
 */

static inline struct effect_brush *impl_from_ICompositionBrush_effect( struct ICompositionBrush *iface )
{
    return CONTAINING_RECORD( iface, struct effect_brush, ICompositionBrush_iface );
}

static HRESULT WINAPI effect_brush_QueryInterface( struct ICompositionBrush *iface, REFIID iid, void **out )
{
    struct effect_brush *impl = impl_from_ICompositionBrush_effect( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown )
        || IsEqualGUID( iid, &IID_IInspectable )
        || IsEqualGUID( iid, &IID_IAgileObject )
        || IsEqualGUID( iid, &IID_ICompositionBrush ))
    {
        *out = &impl->ICompositionBrush_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_ICompositionEffectBrush ))
    {
        *out = &impl->ICompositionEffectBrush_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_ICompositionObject ))
    {
        *out = &impl->ICompositionObject_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IClosable ))
    {
        *out = &impl->IClosable_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI effect_brush_AddRef( struct ICompositionBrush *iface )
{
    struct effect_brush *impl = impl_from_ICompositionBrush_effect( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI effect_brush_Release( struct ICompositionBrush *iface )
{
    struct effect_brush *impl = impl_from_ICompositionBrush_effect( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        unsigned int i;
        for (i = 0; i < impl->source_count; i++)
        {
            if (impl->sources[i].brush)
                IInspectable_Release( (IInspectable *)impl->sources[i].brush );
            free( impl->sources[i].name );
        }
        free( impl->sources );
        comp_release_refs( impl->compositor, impl->properties );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI effect_brush_GetIids( struct ICompositionBrush *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI effect_brush_GetRuntimeClassName( struct ICompositionBrush *iface, HSTRING *class_name )
{
    return WindowsCreateString( RuntimeClass_Windows_UI_Composition_CompositionEffectBrush,
            ARRAYSIZE( RuntimeClass_Windows_UI_Composition_CompositionEffectBrush ) - 1, class_name );
}

static HRESULT WINAPI effect_brush_GetTrustLevel( struct ICompositionBrush *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static const struct ICompositionBrushVtbl effect_brush_vtbl =
{
    effect_brush_QueryInterface,
    effect_brush_AddRef,
    effect_brush_Release,
    effect_brush_GetIids,
    effect_brush_GetRuntimeClassName,
    effect_brush_GetTrustLevel,
};

DEFINE_INHERITED_PROLOGUE( eb_iface, ICompositionEffectBrush, struct effect_brush, ICompositionBrush_iface )

static HRESULT WINAPI eb_iface_GetSourceParameter( struct ICompositionEffectBrush *iface,
        HSTRING name, struct ICompositionBrush **result )
{
    struct effect_brush *impl = impl_from_eb_iface( iface );
    const WCHAR *buffer = WindowsGetStringRawBuffer( name, NULL );
    unsigned int i;

    TRACE( "iface %p, name %s, result %p.\n", iface, debugstr_hstring( name ), result );

    if (!result) return E_POINTER;
    *result = NULL;
    if (!buffer) return E_INVALIDARG;

    for (i = 0; i < impl->source_count; i++)
    {
        if (!wcscmp( impl->sources[i].name, buffer ))
        {
            *result = impl->sources[i].brush;
            if (*result) IInspectable_AddRef( (IInspectable *)*result );
            return S_OK;
        }
    }
    return E_INVALIDARG;
}

static HRESULT WINAPI eb_iface_SetSourceParameter( struct ICompositionEffectBrush *iface,
        HSTRING name, struct ICompositionBrush *source )
{
    struct effect_brush *impl = impl_from_eb_iface( iface );
    const WCHAR *buffer = WindowsGetStringRawBuffer( name, NULL );
    struct effect_source *slot = NULL;
    unsigned int i;

    TRACE( "iface %p, name %s, source %p.\n", iface, debugstr_hstring( name ), source );

    if (!source) return E_POINTER;
    if (!buffer) return E_INVALIDARG;

    for (i = 0; i < impl->source_count; i++)
    {
        if (!wcscmp( impl->sources[i].name, buffer ))
        {
            slot = &impl->sources[i];
            break;
        }
    }

    if (!slot)
    {
        if (impl->source_count == impl->source_capacity)
        {
            unsigned int capacity = impl->source_capacity ? impl->source_capacity * 2 : 8;
            struct effect_source *sources = realloc( impl->sources, capacity * sizeof(*sources) );
            if (!sources) return E_OUTOFMEMORY;
            impl->sources = sources;
            impl->source_capacity = capacity;
        }
        slot = &impl->sources[impl->source_count++];
        slot->name = wcsdup( buffer );
        if (!slot->name)
        {
            impl->source_count--;
            return E_OUTOFMEMORY;
        }
        slot->brush = NULL;
    }

    if (slot->brush) IInspectable_Release( (IInspectable *)slot->brush );
    slot->brush = source;
    IInspectable_AddRef( (IInspectable *)source );
    return S_OK;
}

static const struct ICompositionEffectBrushVtbl eb_iface_vtbl =
{
    eb_iface_QueryInterface,
    eb_iface_AddRef,
    eb_iface_Release,
    eb_iface_GetIids,
    eb_iface_GetRuntimeClassName,
    eb_iface_GetTrustLevel,
    eb_iface_GetSourceParameter,
    eb_iface_SetSourceParameter,
};

DEFINE_INHERITED_PROLOGUE( eb_object, ICompositionObject, struct effect_brush, ICompositionBrush_iface )
COMP_OBJECT_IMPL( eb_object, struct effect_brush )

static const struct ICompositionObjectVtbl eb_object_vtbl =
{
    eb_object_QueryInterface,
    eb_object_AddRef,
    eb_object_Release,
    eb_object_GetIids,
    eb_object_GetRuntimeClassName,
    eb_object_GetTrustLevel,
    eb_object_get_Compositor,
    eb_object_get_Dispatcher,
    eb_object_get_Properties,
    eb_object_StartAnimation,
    eb_object_StopAnimation,
};

DEFINE_INHERITED_PROLOGUE( eb_closable, IClosable, struct effect_brush, ICompositionBrush_iface )
COMP_CLOSABLE_IMPL( eb_closable, struct effect_brush )

static const struct IClosableVtbl eb_closable_vtbl =
{
    eb_closable_QueryInterface,
    eb_closable_AddRef,
    eb_closable_Release,
    eb_closable_GetIids,
    eb_closable_GetRuntimeClassName,
    eb_closable_GetTrustLevel,
    eb_closable_Close,
};

static struct effect_brush *effect_brush_create( struct compositor *compositor )
{
    struct effect_brush *impl;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return NULL;
    impl->ICompositionBrush_iface.lpVtbl = &effect_brush_vtbl;
    impl->ICompositionEffectBrush_iface.lpVtbl = &eb_iface_vtbl;
    impl->ICompositionObject_iface.lpVtbl = &eb_object_vtbl;
    impl->IClosable_iface.lpVtbl = &eb_closable_vtbl;
    impl->ref = 1;
    impl->compositor = compositor;
    if (compositor) IInspectable_AddRef( (IInspectable *)&compositor->ICompositor_iface );
    return impl;
}

static inline struct effect_factory *impl_from_ICompositionEffectFactory( struct ICompositionEffectFactory *iface )
{
    return CONTAINING_RECORD( iface, struct effect_factory, ICompositionEffectFactory_iface );
}

static HRESULT WINAPI effect_factory_QueryInterface( struct ICompositionEffectFactory *iface, REFIID iid, void **out )
{
    struct effect_factory *impl = impl_from_ICompositionEffectFactory( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown )
        || IsEqualGUID( iid, &IID_IInspectable )
        || IsEqualGUID( iid, &IID_IAgileObject )
        || IsEqualGUID( iid, &IID_ICompositionEffectFactory ))
    {
        *out = &impl->ICompositionEffectFactory_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_ICompositionObject ))
    {
        *out = &impl->ICompositionObject_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IClosable ))
    {
        *out = &impl->IClosable_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI effect_factory_AddRef( struct ICompositionEffectFactory *iface )
{
    struct effect_factory *impl = impl_from_ICompositionEffectFactory( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI effect_factory_Release( struct ICompositionEffectFactory *iface )
{
    struct effect_factory *impl = impl_from_ICompositionEffectFactory( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        if (impl->effect) IInspectable_Release( impl->effect );
        if (impl->string_parameters)
            IInspectable_Release( (IInspectable *)impl->string_parameters );
        comp_release_refs( impl->compositor, impl->properties );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI effect_factory_GetIids( struct ICompositionEffectFactory *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI effect_factory_GetRuntimeClassName( struct ICompositionEffectFactory *iface, HSTRING *class_name )
{
    return WindowsCreateString( RuntimeClass_Windows_UI_Composition_CompositionEffectFactory,
            ARRAYSIZE( RuntimeClass_Windows_UI_Composition_CompositionEffectFactory ) - 1, class_name );
}

static HRESULT WINAPI effect_factory_GetTrustLevel( struct ICompositionEffectFactory *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI ef_iface_CreateBrush( struct ICompositionEffectFactory *iface,
        struct ICompositionEffectBrush **result )
{
    struct effect_factory *impl = impl_from_ICompositionEffectFactory( iface );
    struct effect_brush *brush;

    TRACE( "iface %p, result %p.\n", iface, result );

    if (!result) return E_POINTER;
    if (!(brush = effect_brush_create( impl->compositor ))) return E_OUTOFMEMORY;
    *result = &brush->ICompositionEffectBrush_iface;
    return S_OK;
}

static HRESULT WINAPI ef_iface_get_ExtendedError( struct ICompositionEffectFactory *iface, HRESULT *value )
{
    TRACE( "iface %p, value %p.\n", iface, value );
    if (!value) return E_POINTER;
    *value = S_OK;
    return S_OK;
}

static HRESULT WINAPI ef_iface_get_LoadStatus( struct ICompositionEffectFactory *iface,
        enum CompositionEffectFactoryLoadStatus *value )
{
    TRACE( "iface %p, value %p.\n", iface, value );
    if (!value) return E_POINTER;
    *value = CompositionEffectFactoryLoadStatus_Success;
    return S_OK;
}

static const struct ICompositionEffectFactoryVtbl ef_iface_vtbl =
{
    effect_factory_QueryInterface,
    effect_factory_AddRef,
    effect_factory_Release,
    effect_factory_GetIids,
    effect_factory_GetRuntimeClassName,
    effect_factory_GetTrustLevel,
    ef_iface_CreateBrush,
    ef_iface_get_ExtendedError,
    ef_iface_get_LoadStatus,
};

DEFINE_INHERITED_PROLOGUE( ef_object, ICompositionObject, struct effect_factory, ICompositionEffectFactory_iface )
COMP_OBJECT_IMPL( ef_object, struct effect_factory )

static const struct ICompositionObjectVtbl ef_object_vtbl =
{
    ef_object_QueryInterface,
    ef_object_AddRef,
    ef_object_Release,
    ef_object_GetIids,
    ef_object_GetRuntimeClassName,
    ef_object_GetTrustLevel,
    ef_object_get_Compositor,
    ef_object_get_Dispatcher,
    ef_object_get_Properties,
    ef_object_StartAnimation,
    ef_object_StopAnimation,
};

DEFINE_INHERITED_PROLOGUE( ef_closable, IClosable, struct effect_factory, ICompositionEffectFactory_iface )
COMP_CLOSABLE_IMPL( ef_closable, struct effect_factory )

static const struct IClosableVtbl ef_closable_vtbl =
{
    ef_closable_QueryInterface,
    ef_closable_AddRef,
    ef_closable_Release,
    ef_closable_GetIids,
    ef_closable_GetRuntimeClassName,
    ef_closable_GetTrustLevel,
    ef_closable_Close,
};

struct effect_factory *effect_factory_create( struct compositor *compositor,
        IInspectable *effect, struct __FIIterable_1_HSTRING *string_parameters )
{
    struct effect_factory *impl;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return NULL;
    impl->ICompositionEffectFactory_iface.lpVtbl = &ef_iface_vtbl;
    impl->ICompositionObject_iface.lpVtbl = &ef_object_vtbl;
    impl->IClosable_iface.lpVtbl = &ef_closable_vtbl;
    impl->ref = 1;
    impl->compositor = compositor;
    impl->effect = effect;
    impl->string_parameters = string_parameters;
    if (effect) IInspectable_AddRef( effect );
    if (string_parameters) IInspectable_AddRef( (IInspectable *)string_parameters );
    if (compositor) IInspectable_AddRef( (IInspectable *)&compositor->ICompositor_iface );
    return impl;
}

/*
 * SurfaceBrush.
 */

static inline struct surface_brush *impl_from_sb_root( struct ICompositionBrush *iface )
{
    return CONTAINING_RECORD( iface, struct surface_brush, ICompositionBrush_iface );
}

static HRESULT WINAPI surface_brush_QueryInterface( struct ICompositionBrush *iface, REFIID iid, void **out )
{
    struct surface_brush *impl = impl_from_sb_root( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown )
        || IsEqualGUID( iid, &IID_IInspectable )
        || IsEqualGUID( iid, &IID_IAgileObject )
        || IsEqualGUID( iid, &IID_ICompositionBrush ))
    {
        *out = &impl->ICompositionBrush_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_ICompositionSurfaceBrush ))
    {
        *out = &impl->ICompositionSurfaceBrush_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_ICompositionObject ))
    {
        *out = &impl->ICompositionObject_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IClosable ))
    {
        *out = &impl->IClosable_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI surface_brush_AddRef( struct ICompositionBrush *iface )
{
    struct surface_brush *impl = impl_from_sb_root( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI surface_brush_Release( struct ICompositionBrush *iface )
{
    struct surface_brush *impl = impl_from_sb_root( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        if (impl->surface) IInspectable_Release( (IInspectable *)impl->surface );
        comp_release_refs( impl->compositor, impl->properties );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI surface_brush_GetIids( struct ICompositionBrush *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI surface_brush_GetRuntimeClassName( struct ICompositionBrush *iface, HSTRING *class_name )
{
    return WindowsCreateString( RuntimeClass_Windows_UI_Composition_CompositionSurfaceBrush,
            ARRAYSIZE( RuntimeClass_Windows_UI_Composition_CompositionSurfaceBrush ) - 1, class_name );
}

static HRESULT WINAPI surface_brush_GetTrustLevel( struct ICompositionBrush *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static const struct ICompositionBrushVtbl surface_brush_vtbl =
{
    surface_brush_QueryInterface,
    surface_brush_AddRef,
    surface_brush_Release,
    surface_brush_GetIids,
    surface_brush_GetRuntimeClassName,
    surface_brush_GetTrustLevel,
};

DEFINE_INHERITED_PROLOGUE( sb_iface, ICompositionSurfaceBrush, struct surface_brush, ICompositionBrush_iface )

static HRESULT WINAPI sb_iface_get_BitmapInterpolationMode( struct ICompositionSurfaceBrush *iface,
        enum CompositionBitmapInterpolationMode *value )
{
    struct surface_brush *impl = impl_from_sb_iface( iface );
    if (!value) return E_POINTER;
    *value = impl->interpolation;
    return S_OK;
}

static HRESULT WINAPI sb_iface_put_BitmapInterpolationMode( struct ICompositionSurfaceBrush *iface,
        enum CompositionBitmapInterpolationMode value )
{
    struct surface_brush *impl = impl_from_sb_iface( iface );
    impl->interpolation = value;
    return S_OK;
}

static HRESULT WINAPI sb_iface_get_HorizontalAlignmentRatio( struct ICompositionSurfaceBrush *iface, FLOAT *value )
{
    struct surface_brush *impl = impl_from_sb_iface( iface );
    if (!value) return E_POINTER;
    *value = impl->horizontal_alignment_ratio;
    return S_OK;
}

static HRESULT WINAPI sb_iface_put_HorizontalAlignmentRatio( struct ICompositionSurfaceBrush *iface, FLOAT value )
{
    struct surface_brush *impl = impl_from_sb_iface( iface );
    impl->horizontal_alignment_ratio = value;
    return S_OK;
}

static HRESULT WINAPI sb_iface_get_Stretch( struct ICompositionSurfaceBrush *iface, enum CompositionStretch *value )
{
    struct surface_brush *impl = impl_from_sb_iface( iface );
    if (!value) return E_POINTER;
    *value = impl->stretch;
    return S_OK;
}

static HRESULT WINAPI sb_iface_put_Stretch( struct ICompositionSurfaceBrush *iface, enum CompositionStretch value )
{
    struct surface_brush *impl = impl_from_sb_iface( iface );
    impl->stretch = value;
    return S_OK;
}

static HRESULT WINAPI sb_iface_get_Surface( struct ICompositionSurfaceBrush *iface,
        struct ICompositionSurface **value )
{
    struct surface_brush *impl = impl_from_sb_iface( iface );

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    *value = NULL;
    if (!impl->surface) return S_OK;
    *value = impl->surface;
    IInspectable_AddRef( (IInspectable *)*value );
    return S_OK;
}

static HRESULT WINAPI sb_iface_put_Surface( struct ICompositionSurfaceBrush *iface,
        struct ICompositionSurface *value )
{
    struct surface_brush *impl = impl_from_sb_iface( iface );
    struct ICompositionSurface *s = NULL;

    TRACE( "iface %p, value %p.\n", iface, value );

    if (value)
    {
        if (FAILED( IInspectable_QueryInterface( (IInspectable *)value, &IID_IUnknown, (void **)&s ) ))
            return E_NOINTERFACE;
    }
    if (impl->surface) IInspectable_Release( (IInspectable *)impl->surface );
    impl->surface = value ? (struct ICompositionSurface *)s : NULL;
    return S_OK;
}

static HRESULT WINAPI sb_iface_get_VerticalAlignmentRatio( struct ICompositionSurfaceBrush *iface, FLOAT *value )
{
    struct surface_brush *impl = impl_from_sb_iface( iface );
    if (!value) return E_POINTER;
    *value = impl->vertical_alignment_ratio;
    return S_OK;
}

static HRESULT WINAPI sb_iface_put_VerticalAlignmentRatio( struct ICompositionSurfaceBrush *iface, FLOAT value )
{
    struct surface_brush *impl = impl_from_sb_iface( iface );
    impl->vertical_alignment_ratio = value;
    return S_OK;
}

static const struct ICompositionSurfaceBrushVtbl sb_iface_vtbl =
{
    sb_iface_QueryInterface,
    sb_iface_AddRef,
    sb_iface_Release,
    sb_iface_GetIids,
    sb_iface_GetRuntimeClassName,
    sb_iface_GetTrustLevel,
    sb_iface_get_BitmapInterpolationMode,
    sb_iface_put_BitmapInterpolationMode,
    sb_iface_get_HorizontalAlignmentRatio,
    sb_iface_put_HorizontalAlignmentRatio,
    sb_iface_get_Stretch,
    sb_iface_put_Stretch,
    sb_iface_get_Surface,
    sb_iface_put_Surface,
    sb_iface_get_VerticalAlignmentRatio,
    sb_iface_put_VerticalAlignmentRatio,
};

DEFINE_INHERITED_PROLOGUE( sb_object, ICompositionObject, struct surface_brush, ICompositionBrush_iface )
COMP_OBJECT_IMPL( sb_object, struct surface_brush )

static const struct ICompositionObjectVtbl sb_object_vtbl =
{
    sb_object_QueryInterface,
    sb_object_AddRef,
    sb_object_Release,
    sb_object_GetIids,
    sb_object_GetRuntimeClassName,
    sb_object_GetTrustLevel,
    sb_object_get_Compositor,
    sb_object_get_Dispatcher,
    sb_object_get_Properties,
    sb_object_StartAnimation,
    sb_object_StopAnimation,
};

DEFINE_INHERITED_PROLOGUE( sb_closable, IClosable, struct surface_brush, ICompositionBrush_iface )
COMP_CLOSABLE_IMPL( sb_closable, struct surface_brush )

static const struct IClosableVtbl sb_closable_vtbl =
{
    sb_closable_QueryInterface,
    sb_closable_AddRef,
    sb_closable_Release,
    sb_closable_GetIids,
    sb_closable_GetRuntimeClassName,
    sb_closable_GetTrustLevel,
    sb_closable_Close,
};

struct surface_brush *surface_brush_create( struct compositor *compositor, struct ICompositionSurface *surface )
{
    struct surface_brush *impl;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return NULL;
    impl->ICompositionBrush_iface.lpVtbl = &surface_brush_vtbl;
    impl->ICompositionSurfaceBrush_iface.lpVtbl = &sb_iface_vtbl;
    impl->ICompositionObject_iface.lpVtbl = &sb_object_vtbl;
    impl->IClosable_iface.lpVtbl = &sb_closable_vtbl;
    impl->ref = 1;
    impl->compositor = compositor;
    if (surface)
    {
        impl->surface = surface;
        IInspectable_AddRef( (IInspectable *)surface );
    }
    if (compositor) IInspectable_AddRef( (IInspectable *)&compositor->ICompositor_iface );
    return impl;
}

/*
 * CompositionTarget: wraps an IDCompositionTarget created for an HWND, and
 * exposes the COM interop IDesktopWindowTargetInterop for WinAppSDK.
 */

static inline struct composition_target *impl_from_ICompositionTarget( struct ICompositionTarget *iface )
{
    return CONTAINING_RECORD( iface, struct composition_target, ICompositionTarget_iface );
}

static HRESULT WINAPI target_QueryInterface( struct ICompositionTarget *iface, REFIID iid, void **out )
{
    struct composition_target *impl = impl_from_ICompositionTarget( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown )
        || IsEqualGUID( iid, &IID_IInspectable )
        || IsEqualGUID( iid, &IID_IAgileObject )
        || IsEqualGUID( iid, &IID_ICompositionTarget ))
    {
        *out = &impl->ICompositionTarget_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_ICompositionObject ))
    {
        *out = &impl->ICompositionObject_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IClosable ))
    {
        *out = &impl->IClosable_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IDesktopWindowTargetInterop ))
    {
        *out = &impl->IDesktopWindowTargetInterop_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IDCompositionTarget ))
    {
        if (!impl->dcomp_target)
        {
            *out = NULL;
            return E_NOINTERFACE;
        }
        *out = impl->dcomp_target;
        IDCompositionTarget_AddRef( impl->dcomp_target );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI target_AddRef( struct ICompositionTarget *iface )
{
    struct composition_target *impl = impl_from_ICompositionTarget( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI target_Release( struct ICompositionTarget *iface )
{
    struct composition_target *impl = impl_from_ICompositionTarget( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        if (impl->dcomp_target)
        {
            IDCompositionTarget_SetRoot( impl->dcomp_target, NULL );
            IDCompositionTarget_Release( impl->dcomp_target );
        }
        if (impl->root)
            IInspectable_Release( (IInspectable *)&impl->root->IVisual_iface );
        comp_release_refs( impl->compositor, impl->properties );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI target_GetIids( struct ICompositionTarget *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI target_GetRuntimeClassName( struct ICompositionTarget *iface, HSTRING *class_name )
{
    return WindowsCreateString( RuntimeClass_Windows_UI_Composition_CompositionTarget,
            ARRAYSIZE( RuntimeClass_Windows_UI_Composition_CompositionTarget ) - 1, class_name );
}

static HRESULT WINAPI target_GetTrustLevel( struct ICompositionTarget *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI target_get_Root( struct ICompositionTarget *iface, struct IVisual **value );
static HRESULT WINAPI target_put_Root( struct ICompositionTarget *iface, struct IVisual *value );

static const struct ICompositionTargetVtbl target_vtbl =
{
    target_QueryInterface,
    target_AddRef,
    target_Release,
    target_GetIids,
    target_GetRuntimeClassName,
    target_GetTrustLevel,
    target_get_Root,
    target_put_Root,
};

static HRESULT WINAPI target_get_Root( struct ICompositionTarget *iface, struct IVisual **value )
{
    struct composition_target *impl = impl_from_ICompositionTarget( iface );

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    *value = NULL;
    if (!impl->root) return S_OK;
    *value = &impl->root->IVisual_iface;
    IInspectable_AddRef( (IInspectable *)*value );
    return S_OK;
}

static HRESULT WINAPI target_put_Root( struct ICompositionTarget *iface, struct IVisual *value )
{
    struct composition_target *impl = impl_from_ICompositionTarget( iface );
    struct visual *root = NULL;

    TRACE( "iface %p, value %p.\n", iface, value );

    if (value)
    {
        if (FAILED( IInspectable_QueryInterface( (IInspectable *)value, &IID_IUnknown,
                (void **)&root ) ))
            return E_NOINTERFACE;
    }
    if (impl->root == (value ? visual_from_IVisual( &root->IVisual_iface ) : NULL))
    {
        if (root) IInspectable_Release( (IInspectable *)&root->IVisual_iface );
        return S_OK;
    }
    if (impl->dcomp_target && impl->root && impl->root->dcomp_visual)
    {
        IDCompositionTarget_SetRoot( impl->dcomp_target, NULL );
    }
    if (impl->root) IInspectable_Release( (IInspectable *)&impl->root->IVisual_iface );
    impl->root = value ? visual_from_IVisual( &root->IVisual_iface ) : NULL;
    if (impl->root)
    {
        IInspectable_AddRef( (IInspectable *)&impl->root->IVisual_iface );
        IInspectable_Release( (IInspectable *)&root->IVisual_iface );

        if (impl->dcomp_target)
        {
            if (!impl->root->dcomp_visual && impl->compositor)
            {
                compositor_ensure_device( impl->compositor );
                if (impl->compositor->device)
                    IDCompositionDevice_CreateVisual( impl->compositor->device, &impl->root->dcomp_visual );
                else if (impl->compositor->desktop_device)
                    IDCompositionDesktopDevice_CreateVisual( impl->compositor->desktop_device,
                            (IDCompositionVisual2 **)&impl->root->dcomp_visual );
                else if (impl->compositor->device2)
                    IDCompositionDevice2_CreateVisual( impl->compositor->device2,
                            (IDCompositionVisual2 **)&impl->root->dcomp_visual );
            }
            if (impl->root->dcomp_visual)
            {
                IDCompositionVisual_SetOffsetX( impl->root->dcomp_visual, impl->root->offset.X );
                IDCompositionVisual_SetOffsetY( impl->root->dcomp_visual, impl->root->offset.Y );
                IDCompositionTarget_SetRoot( impl->dcomp_target, impl->root->dcomp_visual );
            }
        }
    }
    else if (impl->dcomp_target)
    {
        IDCompositionTarget_SetRoot( impl->dcomp_target, NULL );
    }
    return S_OK;
}

/* IDesktopWindowTargetInterop is a COM interop interface: plain IUnknown prologue. */
static inline struct composition_target *impl_from_target_dwt( struct IDesktopWindowTargetInterop *iface )
{
    return CONTAINING_RECORD( iface, struct composition_target, IDesktopWindowTargetInterop_iface );
}

static HRESULT WINAPI target_dwt_QueryInterface( struct IDesktopWindowTargetInterop *iface, REFIID iid,
        void **out )
{
    struct composition_target *impl = impl_from_target_dwt( iface );
    return IInspectable_QueryInterface( (IInspectable *)&impl->ICompositionTarget_iface, iid, out );
}

static ULONG WINAPI target_dwt_AddRef( struct IDesktopWindowTargetInterop *iface )
{
    struct composition_target *impl = impl_from_target_dwt( iface );
    return IInspectable_AddRef( (IInspectable *)&impl->ICompositionTarget_iface );
}

static ULONG WINAPI target_dwt_Release( struct IDesktopWindowTargetInterop *iface )
{
    struct composition_target *impl = impl_from_target_dwt( iface );
    return IInspectable_Release( (IInspectable *)&impl->ICompositionTarget_iface );
}

static HRESULT WINAPI target_dwt_Hwnd( struct IDesktopWindowTargetInterop *iface, HWND *hwnd )
{
    struct composition_target *impl = impl_from_target_dwt( iface );

    TRACE( "iface %p, hwnd %p.\n", iface, hwnd );

    if (!hwnd) return E_POINTER;
    *hwnd = impl->hwnd;
    return S_OK;
}

static const struct IDesktopWindowTargetInteropVtbl target_dwt_vtbl =
{
    target_dwt_QueryInterface,
    target_dwt_AddRef,
    target_dwt_Release,
    target_dwt_Hwnd,
};

DEFINE_INHERITED_PROLOGUE( target_object, ICompositionObject, struct composition_target,
        ICompositionTarget_iface )
COMP_OBJECT_IMPL( target_object, struct composition_target )

static const struct ICompositionObjectVtbl target_object_vtbl =
{
    target_object_QueryInterface,
    target_object_AddRef,
    target_object_Release,
    target_object_GetIids,
    target_object_GetRuntimeClassName,
    target_object_GetTrustLevel,
    target_object_get_Compositor,
    target_object_get_Dispatcher,
    target_object_get_Properties,
    target_object_StartAnimation,
    target_object_StopAnimation,
};

DEFINE_INHERITED_PROLOGUE( target_closable, IClosable, struct composition_target,
        ICompositionTarget_iface )
COMP_CLOSABLE_IMPL( target_closable, struct composition_target )

static const struct IClosableVtbl target_closable_vtbl =
{
    target_closable_QueryInterface,
    target_closable_AddRef,
    target_closable_Release,
    target_closable_GetIids,
    target_closable_GetRuntimeClassName,
    target_closable_GetTrustLevel,
    target_closable_Close,
};

struct composition_target *composition_target_create( struct compositor *compositor,
        struct IDCompositionTarget *dcomp_target, HWND hwnd )
{
    struct composition_target *impl;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return NULL;
    impl->ICompositionTarget_iface.lpVtbl = &target_vtbl;
    impl->ICompositionObject_iface.lpVtbl = &target_object_vtbl;
    impl->IClosable_iface.lpVtbl = &target_closable_vtbl;
    impl->IDesktopWindowTargetInterop_iface.lpVtbl = &target_dwt_vtbl;
    impl->ref = 1;
    impl->compositor = compositor;
    impl->dcomp_target = dcomp_target;
    impl->hwnd = hwnd;
    if (dcomp_target) IDCompositionTarget_AddRef( dcomp_target );
    if (compositor) IInspectable_AddRef( (IInspectable *)&compositor->ICompositor_iface );
    return impl;
}

/*
 * CompositionGraphicsDevice.
 */

static inline struct graphics_device *impl_from_ICompositionGraphicsDevice(
        struct ICompositionGraphicsDevice *iface )
{
    return CONTAINING_RECORD( iface, struct graphics_device, ICompositionGraphicsDevice_iface );
}

static HRESULT WINAPI graphics_device_QueryInterface( struct ICompositionGraphicsDevice *iface,
        REFIID iid, void **out )
{
    struct graphics_device *impl = impl_from_ICompositionGraphicsDevice( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown )
        || IsEqualGUID( iid, &IID_IInspectable )
        || IsEqualGUID( iid, &IID_IAgileObject )
        || IsEqualGUID( iid, &IID_ICompositionGraphicsDevice ))
    {
        *out = &impl->ICompositionGraphicsDevice_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_ICompositionObject ))
    {
        *out = &impl->ICompositionObject_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IClosable ))
    {
        *out = &impl->IClosable_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI graphics_device_AddRef( struct ICompositionGraphicsDevice *iface )
{
    struct graphics_device *impl = impl_from_ICompositionGraphicsDevice( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI graphics_device_Release( struct ICompositionGraphicsDevice *iface )
{
    struct graphics_device *impl = impl_from_ICompositionGraphicsDevice( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        if (impl->dcomp_device) IDCompositionDevice2_Release( impl->dcomp_device );
        comp_release_refs( impl->compositor, impl->properties );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI graphics_device_GetIids( struct ICompositionGraphicsDevice *iface,
        ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI graphics_device_GetRuntimeClassName( struct ICompositionGraphicsDevice *iface,
        HSTRING *class_name )
{
    return WindowsCreateString( RuntimeClass_Windows_UI_Composition_CompositionGraphicsDevice,
            ARRAYSIZE( RuntimeClass_Windows_UI_Composition_CompositionGraphicsDevice ) - 1, class_name );
}

static HRESULT WINAPI graphics_device_GetTrustLevel( struct ICompositionGraphicsDevice *iface,
        TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI graphics_device_CreateDrawingSurface( struct ICompositionGraphicsDevice *iface,
        Size pixels, enum DirectXPixelFormat format, enum DirectXAlphaMode mode,
        struct ICompositionDrawingSurface **result )
{
    FIXME( "iface %p, pixels %gx%g, format %u, mode %u, result %p stub!\n", iface,
            pixels.Width, pixels.Height, format, mode, result );
    if (result) *result = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI graphics_device_add_RenderingDeviceReplaced( struct ICompositionGraphicsDevice *iface,
        __FITypedEventHandler_2_Windows__CUI__CComposition__CCompositionGraphicsDevice_Windows__CUI__CComposition__CRenderingDeviceReplacedEventArgs *handler,
        EventRegistrationToken *token )
{
    FIXME( "iface %p, handler %p, token %p stub!\n", iface, handler, token );
    if (token) token->value = 1;
    return S_OK;
}

static HRESULT WINAPI graphics_device_remove_RenderingDeviceReplaced( struct ICompositionGraphicsDevice *iface,
        EventRegistrationToken token )
{
    TRACE( "iface %p, token %s stub!\n", iface, wine_dbgstr_longlong( token.value ) );
    return S_OK;
}

static const struct ICompositionGraphicsDeviceVtbl graphics_device_vtbl =
{
    graphics_device_QueryInterface,
    graphics_device_AddRef,
    graphics_device_Release,
    graphics_device_GetIids,
    graphics_device_GetRuntimeClassName,
    graphics_device_GetTrustLevel,
    graphics_device_CreateDrawingSurface,
    graphics_device_add_RenderingDeviceReplaced,
    graphics_device_remove_RenderingDeviceReplaced,
};

DEFINE_INHERITED_PROLOGUE( graphics_device_object, ICompositionObject, struct graphics_device,
        ICompositionGraphicsDevice_iface )
COMP_OBJECT_IMPL( graphics_device_object, struct graphics_device )

static const struct ICompositionObjectVtbl graphics_device_object_vtbl =
{
    graphics_device_object_QueryInterface,
    graphics_device_object_AddRef,
    graphics_device_object_Release,
    graphics_device_object_GetIids,
    graphics_device_object_GetRuntimeClassName,
    graphics_device_object_GetTrustLevel,
    graphics_device_object_get_Compositor,
    graphics_device_object_get_Dispatcher,
    graphics_device_object_get_Properties,
    graphics_device_object_StartAnimation,
    graphics_device_object_StopAnimation,
};

DEFINE_INHERITED_PROLOGUE( graphics_device_closable, IClosable, struct graphics_device,
        ICompositionGraphicsDevice_iface )
COMP_CLOSABLE_IMPL( graphics_device_closable, struct graphics_device )

static const struct IClosableVtbl graphics_device_closable_vtbl =
{
    graphics_device_closable_QueryInterface,
    graphics_device_closable_AddRef,
    graphics_device_closable_Release,
    graphics_device_closable_GetIids,
    graphics_device_closable_GetRuntimeClassName,
    graphics_device_closable_GetTrustLevel,
    graphics_device_closable_Close,
};

struct graphics_device *graphics_device_create( struct compositor *compositor,
        struct IDCompositionDevice2 *dcomp_device )
{
    struct graphics_device *impl;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return NULL;
    impl->ICompositionGraphicsDevice_iface.lpVtbl = &graphics_device_vtbl;
    impl->ICompositionObject_iface.lpVtbl = &graphics_device_object_vtbl;
    impl->IClosable_iface.lpVtbl = &graphics_device_closable_vtbl;
    impl->ref = 1;
    impl->compositor = compositor;
    impl->dcomp_device = dcomp_device;
    if (impl->dcomp_device) IDCompositionDevice2_AddRef( impl->dcomp_device );
    if (compositor) IInspectable_AddRef( (IInspectable *)&compositor->ICompositor_iface );
    return impl;
}

/*
 * CompositionSurface: a WinRT wrapper over a DirectComposition surface (created
 * from an HWND, a shared handle or the compositor device) or over a DXGI swap chain.
 */

static inline struct composition_surface *impl_from_ICompositionSurface( struct ICompositionSurface *iface )
{
    return CONTAINING_RECORD( iface, struct composition_surface, ICompositionSurface_iface );
}

static HRESULT WINAPI surface_QueryInterface( struct ICompositionSurface *iface, REFIID iid, void **out )
{
    struct composition_surface *impl = impl_from_ICompositionSurface( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown )
        || IsEqualGUID( iid, &IID_IInspectable )
        || IsEqualGUID( iid, &IID_IAgileObject )
        || IsEqualGUID( iid, &IID_ICompositionSurface ))
    {
        *out = &impl->ICompositionSurface_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_ICompositionObject ))
    {
        *out = &impl->ICompositionObject_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IClosable ))
    {
        *out = &impl->IClosable_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    /* Interop: expose the wrapped DirectComposition object through this interface. */
    if (impl->dcomp_surface)
    {
        HRESULT hr = IUnknown_QueryInterface( impl->dcomp_surface, iid, out );
        if (SUCCEEDED( hr )) return S_OK;
    }
    if (impl->swap_chain)
    {
        HRESULT hr = IDXGISwapChain_QueryInterface( impl->swap_chain, iid, out );
        if (SUCCEEDED( hr )) return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI surface_AddRef( struct ICompositionSurface *iface )
{
    struct composition_surface *impl = impl_from_ICompositionSurface( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI surface_Release( struct ICompositionSurface *iface )
{
    struct composition_surface *impl = impl_from_ICompositionSurface( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        if (impl->dcomp_surface) IUnknown_Release( impl->dcomp_surface );
        if (impl->swap_chain) IDXGISwapChain_Release( impl->swap_chain );
        comp_release_refs( impl->compositor, impl->properties );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI surface_GetIids( struct ICompositionSurface *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static const WCHAR composition_surface_class_name[] = L"Windows.UI.Composition.CompositionSurface";

static HRESULT WINAPI surface_GetRuntimeClassName( struct ICompositionSurface *iface, HSTRING *class_name )
{
    return WindowsCreateString( composition_surface_class_name,
            lstrlenW( composition_surface_class_name ), class_name );
}

static HRESULT WINAPI surface_GetTrustLevel( struct ICompositionSurface *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static const struct ICompositionSurfaceVtbl surface_vtbl =
{
    surface_QueryInterface,
    surface_AddRef,
    surface_Release,
    surface_GetIids,
    surface_GetRuntimeClassName,
    surface_GetTrustLevel,
};

DEFINE_INHERITED_PROLOGUE( surface_object, ICompositionObject, struct composition_surface,
        ICompositionSurface_iface )
COMP_OBJECT_IMPL( surface_object, struct composition_surface )

static const struct ICompositionObjectVtbl surface_object_vtbl =
{
    surface_object_QueryInterface,
    surface_object_AddRef,
    surface_object_Release,
    surface_object_GetIids,
    surface_object_GetRuntimeClassName,
    surface_object_GetTrustLevel,
    surface_object_get_Compositor,
    surface_object_get_Dispatcher,
    surface_object_get_Properties,
    surface_object_StartAnimation,
    surface_object_StopAnimation,
};

DEFINE_INHERITED_PROLOGUE( surface_closable, IClosable, struct composition_surface,
        ICompositionSurface_iface )
COMP_CLOSABLE_IMPL( surface_closable, struct composition_surface )

static const struct IClosableVtbl surface_closable_vtbl =
{
    surface_closable_QueryInterface,
    surface_closable_AddRef,
    surface_closable_Release,
    surface_closable_GetIids,
    surface_closable_GetRuntimeClassName,
    surface_closable_GetTrustLevel,
    surface_closable_Close,
};

struct composition_surface *composition_surface_create( struct compositor *compositor,
        struct IUnknown *dcomp_surface, struct IDXGISwapChain *swap_chain )
{
    struct composition_surface *impl;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return NULL;
    impl->ICompositionSurface_iface.lpVtbl = &surface_vtbl;
    impl->ICompositionObject_iface.lpVtbl = &surface_object_vtbl;
    impl->IClosable_iface.lpVtbl = &surface_closable_vtbl;
    impl->ref = 1;
    impl->compositor = compositor;
    impl->dcomp_surface = dcomp_surface;
    impl->swap_chain = swap_chain;
    if (impl->dcomp_surface) IUnknown_AddRef( impl->dcomp_surface );
    if (impl->swap_chain) IDXGISwapChain_AddRef( impl->swap_chain );
    if (compositor) IInspectable_AddRef( (IInspectable *)&compositor->ICompositor_iface );
    return impl;
}

/*
 * Batches.
 */

static inline struct scoped_batch *impl_from_ICompositionScopedBatch( struct ICompositionScopedBatch *iface )
{
    return CONTAINING_RECORD( iface, struct scoped_batch, ICompositionScopedBatch_iface );
}

static HRESULT WINAPI scoped_batch_QueryInterface( struct ICompositionScopedBatch *iface, REFIID iid, void **out )
{
    struct scoped_batch *impl = impl_from_ICompositionScopedBatch( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown )
        || IsEqualGUID( iid, &IID_IInspectable )
        || IsEqualGUID( iid, &IID_IAgileObject )
        || IsEqualGUID( iid, &IID_ICompositionScopedBatch ))
    {
        *out = &impl->ICompositionScopedBatch_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_ICompositionObject ))
    {
        *out = &impl->ICompositionObject_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IClosable ))
    {
        *out = &impl->IClosable_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI scoped_batch_AddRef( struct ICompositionScopedBatch *iface )
{
    struct scoped_batch *impl = impl_from_ICompositionScopedBatch( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI scoped_batch_Release( struct ICompositionScopedBatch *iface )
{
    struct scoped_batch *impl = impl_from_ICompositionScopedBatch( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        comp_release_refs( impl->compositor, impl->properties );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI scoped_batch_GetIids( struct ICompositionScopedBatch *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI scoped_batch_GetRuntimeClassName( struct ICompositionScopedBatch *iface, HSTRING *class_name )
{
    return WindowsCreateString( RuntimeClass_Windows_UI_Composition_CompositionScopedBatch,
            ARRAYSIZE( RuntimeClass_Windows_UI_Composition_CompositionScopedBatch ) - 1, class_name );
}

static HRESULT WINAPI scoped_batch_GetTrustLevel( struct ICompositionScopedBatch *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI scoped_batch_get_IsActive( struct ICompositionScopedBatch *iface, boolean *value )
{
    struct scoped_batch *impl = impl_from_ICompositionScopedBatch( iface );
    if (!value) return E_POINTER;
    *value = impl->active;
    return S_OK;
}

static HRESULT WINAPI scoped_batch_get_IsEnded( struct ICompositionScopedBatch *iface, boolean *value )
{
    struct scoped_batch *impl = impl_from_ICompositionScopedBatch( iface );
    if (!value) return E_POINTER;
    *value = impl->ended;
    return S_OK;
}

static HRESULT WINAPI scoped_batch_End( struct ICompositionScopedBatch *iface )
{
    struct scoped_batch *impl = impl_from_ICompositionScopedBatch( iface );

    TRACE( "iface %p.\n", iface );
    impl->active = FALSE;
    impl->ended = TRUE;
    return S_OK;
}

static HRESULT WINAPI scoped_batch_Resume( struct ICompositionScopedBatch *iface )
{
    struct scoped_batch *impl = impl_from_ICompositionScopedBatch( iface );

    TRACE( "iface %p.\n", iface );
    impl->active = TRUE;
    return S_OK;
}

static HRESULT WINAPI scoped_batch_Suspend( struct ICompositionScopedBatch *iface )
{
    struct scoped_batch *impl = impl_from_ICompositionScopedBatch( iface );

    TRACE( "iface %p.\n", iface );
    impl->active = FALSE;
    return S_OK;
}

static HRESULT WINAPI scoped_batch_add_Completed( struct ICompositionScopedBatch *iface,
        __FITypedEventHandler_2_IInspectable_Windows__CUI__CComposition__CCompositionBatchCompletedEventArgs *handler,
        EventRegistrationToken *token )
{
    FIXME( "iface %p, handler %p, token %p stub!\n", iface, handler, token );
    if (token) token->value = 1;
    return S_OK;
}

static HRESULT WINAPI scoped_batch_remove_Completed( struct ICompositionScopedBatch *iface,
        EventRegistrationToken token )
{
    TRACE( "iface %p, token %s stub!\n", iface, wine_dbgstr_longlong( token.value ) );
    return S_OK;
}

static const struct ICompositionScopedBatchVtbl scoped_batch_vtbl =
{
    scoped_batch_QueryInterface,
    scoped_batch_AddRef,
    scoped_batch_Release,
    scoped_batch_GetIids,
    scoped_batch_GetRuntimeClassName,
    scoped_batch_GetTrustLevel,
    scoped_batch_get_IsActive,
    scoped_batch_get_IsEnded,
    scoped_batch_End,
    scoped_batch_Resume,
    scoped_batch_Suspend,
    scoped_batch_add_Completed,
    scoped_batch_remove_Completed,
};

DEFINE_INHERITED_PROLOGUE( scoped_batch_object, ICompositionObject, struct scoped_batch,
        ICompositionScopedBatch_iface )
COMP_OBJECT_IMPL( scoped_batch_object, struct scoped_batch )

static const struct ICompositionObjectVtbl scoped_batch_object_vtbl =
{
    scoped_batch_object_QueryInterface,
    scoped_batch_object_AddRef,
    scoped_batch_object_Release,
    scoped_batch_object_GetIids,
    scoped_batch_object_GetRuntimeClassName,
    scoped_batch_object_GetTrustLevel,
    scoped_batch_object_get_Compositor,
    scoped_batch_object_get_Dispatcher,
    scoped_batch_object_get_Properties,
    scoped_batch_object_StartAnimation,
    scoped_batch_object_StopAnimation,
};

DEFINE_INHERITED_PROLOGUE( scoped_batch_closable, IClosable, struct scoped_batch,
        ICompositionScopedBatch_iface )
COMP_CLOSABLE_IMPL( scoped_batch_closable, struct scoped_batch )

static const struct IClosableVtbl scoped_batch_closable_vtbl =
{
    scoped_batch_closable_QueryInterface,
    scoped_batch_closable_AddRef,
    scoped_batch_closable_Release,
    scoped_batch_closable_GetIids,
    scoped_batch_closable_GetRuntimeClassName,
    scoped_batch_closable_GetTrustLevel,
    scoped_batch_closable_Close,
};

struct scoped_batch *scoped_batch_create( struct compositor *compositor )
{
    struct scoped_batch *impl;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return NULL;
    impl->ICompositionScopedBatch_iface.lpVtbl = &scoped_batch_vtbl;
    impl->ICompositionObject_iface.lpVtbl = &scoped_batch_object_vtbl;
    impl->IClosable_iface.lpVtbl = &scoped_batch_closable_vtbl;
    impl->ref = 1;
    impl->compositor = compositor;
    impl->active = TRUE;
    if (compositor) IInspectable_AddRef( (IInspectable *)&compositor->ICompositor_iface );
    return impl;
}

static inline struct commit_batch *impl_from_ICompositionCommitBatch( struct ICompositionCommitBatch *iface )
{
    return CONTAINING_RECORD( iface, struct commit_batch, ICompositionCommitBatch_iface );
}

static HRESULT WINAPI commit_batch_QueryInterface( struct ICompositionCommitBatch *iface, REFIID iid, void **out )
{
    struct commit_batch *impl = impl_from_ICompositionCommitBatch( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown )
        || IsEqualGUID( iid, &IID_IInspectable )
        || IsEqualGUID( iid, &IID_IAgileObject )
        || IsEqualGUID( iid, &IID_ICompositionCommitBatch ))
    {
        *out = &impl->ICompositionCommitBatch_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_ICompositionObject ))
    {
        *out = &impl->ICompositionObject_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IClosable ))
    {
        *out = &impl->IClosable_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI commit_batch_AddRef( struct ICompositionCommitBatch *iface )
{
    struct commit_batch *impl = impl_from_ICompositionCommitBatch( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI commit_batch_Release( struct ICompositionCommitBatch *iface )
{
    struct commit_batch *impl = impl_from_ICompositionCommitBatch( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        comp_release_refs( impl->compositor, impl->properties );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI commit_batch_GetIids( struct ICompositionCommitBatch *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI commit_batch_GetRuntimeClassName( struct ICompositionCommitBatch *iface, HSTRING *class_name )
{
    return WindowsCreateString( RuntimeClass_Windows_UI_Composition_CompositionCommitBatch,
            ARRAYSIZE( RuntimeClass_Windows_UI_Composition_CompositionCommitBatch ) - 1, class_name );
}

static HRESULT WINAPI commit_batch_GetTrustLevel( struct ICompositionCommitBatch *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI commit_batch_get_IsActive( struct ICompositionCommitBatch *iface, boolean *value )
{
    struct commit_batch *impl = impl_from_ICompositionCommitBatch( iface );
    if (!value) return E_POINTER;
    *value = impl->active;
    return S_OK;
}

static HRESULT WINAPI commit_batch_get_IsEnded( struct ICompositionCommitBatch *iface, boolean *value )
{
    struct commit_batch *impl = impl_from_ICompositionCommitBatch( iface );
    if (!value) return E_POINTER;
    *value = impl->ended;
    return S_OK;
}

static HRESULT WINAPI commit_batch_add_Completed( struct ICompositionCommitBatch *iface,
        __FITypedEventHandler_2_IInspectable_Windows__CUI__CComposition__CCompositionBatchCompletedEventArgs *handler,
        EventRegistrationToken *token )
{
    FIXME( "iface %p, handler %p, token %p stub!\n", iface, handler, token );
    if (token) token->value = 1;
    return S_OK;
}

static HRESULT WINAPI commit_batch_remove_Completed( struct ICompositionCommitBatch *iface,
        EventRegistrationToken token )
{
    TRACE( "iface %p, token %s stub!\n", iface, wine_dbgstr_longlong( token.value ) );
    return S_OK;
}

static const struct ICompositionCommitBatchVtbl commit_batch_vtbl =
{
    commit_batch_QueryInterface,
    commit_batch_AddRef,
    commit_batch_Release,
    commit_batch_GetIids,
    commit_batch_GetRuntimeClassName,
    commit_batch_GetTrustLevel,
    commit_batch_get_IsActive,
    commit_batch_get_IsEnded,
    commit_batch_add_Completed,
    commit_batch_remove_Completed,
};

DEFINE_INHERITED_PROLOGUE( commit_batch_object, ICompositionObject, struct commit_batch,
        ICompositionCommitBatch_iface )
COMP_OBJECT_IMPL( commit_batch_object, struct commit_batch )

static const struct ICompositionObjectVtbl commit_batch_object_vtbl =
{
    commit_batch_object_QueryInterface,
    commit_batch_object_AddRef,
    commit_batch_object_Release,
    commit_batch_object_GetIids,
    commit_batch_object_GetRuntimeClassName,
    commit_batch_object_GetTrustLevel,
    commit_batch_object_get_Compositor,
    commit_batch_object_get_Dispatcher,
    commit_batch_object_get_Properties,
    commit_batch_object_StartAnimation,
    commit_batch_object_StopAnimation,
};

DEFINE_INHERITED_PROLOGUE( commit_batch_closable, IClosable, struct commit_batch,
        ICompositionCommitBatch_iface )
COMP_CLOSABLE_IMPL( commit_batch_closable, struct commit_batch )

static const struct IClosableVtbl commit_batch_closable_vtbl =
{
    commit_batch_closable_QueryInterface,
    commit_batch_closable_AddRef,
    commit_batch_closable_Release,
    commit_batch_closable_GetIids,
    commit_batch_closable_GetRuntimeClassName,
    commit_batch_closable_GetTrustLevel,
    commit_batch_closable_Close,
};

struct commit_batch *commit_batch_create( struct compositor *compositor )
{
    struct commit_batch *impl;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return NULL;
    impl->ICompositionCommitBatch_iface.lpVtbl = &commit_batch_vtbl;
    impl->ICompositionObject_iface.lpVtbl = &commit_batch_object_vtbl;
    impl->IClosable_iface.lpVtbl = &commit_batch_closable_vtbl;
    impl->ref = 1;
    impl->compositor = compositor;
    impl->active = TRUE;
    if (compositor) IInspectable_AddRef( (IInspectable *)&compositor->ICompositor_iface );
    return impl;
}
