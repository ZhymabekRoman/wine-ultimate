/* WinRT Windows.UI.Composition Visual, ContainerVisual, SpriteVisual and
 * VisualCollection implementation
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


static inline struct visual *impl_from_IVisual( struct IVisual *iface )
{
    return CONTAINING_RECORD( iface, struct visual, IVisual_iface );
}

struct visual *visual_from_IVisual( struct IVisual *iface )
{
    return impl_from_IVisual( iface );
}

static HRESULT visual_reparent( struct visual *visual, struct visual *parent );
static HRESULT WINAPI container_visual_get_Children( struct IContainerVisual *iface, struct IVisualCollection **value );
static HRESULT WINAPI sprite_visual_get_Brush( struct ISpriteVisual *iface, struct ICompositionBrush **value );
static HRESULT WINAPI sprite_visual_put_Brush( struct ISpriteVisual *iface, struct ICompositionBrush *value );
static struct visual_collection *visual_collection_create( struct compositor *compositor, struct visual *owner );

static inline struct visual *impl_from_IContainerVisual( struct IContainerVisual *iface )
{
    return CONTAINING_RECORD( iface, struct visual, IContainerVisual_iface );
}

static inline struct visual *impl_from_ISpriteVisual( struct ISpriteVisual *iface )
{
    return CONTAINING_RECORD( iface, struct visual, ISpriteVisual_iface );
}

static HRESULT WINAPI visual_QueryInterface( struct IVisual *iface, REFIID iid, void **out )
{
    struct visual *impl = impl_from_IVisual( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown )
        || IsEqualGUID( iid, &IID_IInspectable )
        || IsEqualGUID( iid, &IID_IAgileObject )
        || IsEqualGUID( iid, &IID_IVisual ))
    {
        *out = &impl->IVisual_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IContainerVisual ))
    {
        *out = &impl->IContainerVisual_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_ISpriteVisual ))
    {
        if (!impl->is_sprite)
        {
            FIXME( "IID_ISpriteVisual requested on a ContainerVisual, returning E_NOINTERFACE.\n" );
            *out = NULL;
            return E_NOINTERFACE;
        }
        *out = &impl->ISpriteVisual_iface;
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
    /* Public composable factory interfaces; the composition tree is a stub, so
     * these return the object itself (the IInspectable prologue is shared). */
    if (IsEqualGUID( iid, &IID_IVisualFactory ) || IsEqualGUID( iid, &IID_IContainerVisualFactory ))
    {
        *out = &impl->IVisual_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI visual_AddRef( struct IVisual *iface )
{
    struct visual *impl = impl_from_IVisual( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI visual_Release( struct IVisual *iface )
{
    struct visual *impl = impl_from_IVisual( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        if (impl->children) IInspectable_Release( (IInspectable *)&impl->children->IVisualCollection_iface );
        if (impl->brush) IInspectable_Release( (IInspectable *)impl->brush );
        if (impl->clip) IInspectable_Release( (IInspectable *)impl->clip );
        if (impl->properties)
            IInspectable_Release( (IInspectable *)prop_set_get_iface( impl->properties ) );
        if (impl->compositor)
            IInspectable_Release( (IInspectable *)&impl->compositor->ICompositor_iface );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI visual_GetIids( struct IVisual *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI visual_GetRuntimeClassName( struct IVisual *iface, HSTRING *class_name )
{
    struct visual *impl = impl_from_IVisual( iface );
    const WCHAR *name = impl->is_sprite ? RuntimeClass_Windows_UI_Composition_SpriteVisual
            : RuntimeClass_Windows_UI_Composition_ContainerVisual;

    return WindowsCreateString( name, lstrlenW( name ), class_name );
}

static HRESULT WINAPI visual_GetTrustLevel( struct IVisual *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

#define VISUAL_GETTER_VECTOR2( name, field ) \
    static HRESULT WINAPI visual_get_##name( struct IVisual *iface, Vector2 *value ) \
    { \
        struct visual *impl = impl_from_IVisual( iface ); \
        if (!value) return E_POINTER; \
        *value = impl->field; \
        return S_OK; \
    } \
    static HRESULT WINAPI visual_put_##name( struct IVisual *iface, Vector2 value ) \
    { \
        struct visual *impl = impl_from_IVisual( iface ); \
        TRACE( "iface %p, value %g, %g.\n", iface, value.X, value.Y ); \
        impl->field = value; \
        return S_OK; \
    }

#define VISUAL_GETTER_VECTOR3( name, field ) \
    static HRESULT WINAPI visual_get_##name( struct IVisual *iface, Vector3 *value ) \
    { \
        struct visual *impl = impl_from_IVisual( iface ); \
        if (!value) return E_POINTER; \
        *value = impl->field; \
        return S_OK; \
    } \
    static HRESULT WINAPI visual_put_##name( struct IVisual *iface, Vector3 value ) \
    { \
        struct visual *impl = impl_from_IVisual( iface ); \
        TRACE( "iface %p, value %g, %g, %g.\n", iface, value.X, value.Y, value.Z ); \
        impl->field = value; \
        return S_OK; \
    }

#define VISUAL_GETTER_FLOAT( name, field ) \
    static HRESULT WINAPI visual_get_##name( struct IVisual *iface, FLOAT *value ) \
    { \
        struct visual *impl = impl_from_IVisual( iface ); \
        if (!value) return E_POINTER; \
        *value = impl->field; \
        return S_OK; \
    } \
    static HRESULT WINAPI visual_put_##name( struct IVisual *iface, FLOAT value ) \
    { \
        struct visual *impl = impl_from_IVisual( iface ); \
        TRACE( "iface %p, value %g.\n", iface, value ); \
        impl->field = value; \
        return S_OK; \
    }

#define VISUAL_GETTER_ENUM( name, type, field ) \
    static HRESULT WINAPI visual_get_##name( struct IVisual *iface, type *value ) \
    { \
        struct visual *impl = impl_from_IVisual( iface ); \
        if (!value) return E_POINTER; \
        *value = impl->field; \
        return S_OK; \
    } \
    static HRESULT WINAPI visual_put_##name( struct IVisual *iface, type value ) \
    { \
        struct visual *impl = impl_from_IVisual( iface ); \
        impl->field = value; \
        return S_OK; \
    }

VISUAL_GETTER_VECTOR2( AnchorPoint, anchor_point )
VISUAL_GETTER_ENUM( BackfaceVisibility, enum CompositionBackfaceVisibility, backface_visibility )
VISUAL_GETTER_ENUM( BorderMode, enum CompositionBorderMode, border_mode )
VISUAL_GETTER_VECTOR3( CenterPoint, center_point )
VISUAL_GETTER_ENUM( CompositeMode, enum CompositionCompositeMode, composite_mode )
VISUAL_GETTER_VECTOR3( Offset, offset )
VISUAL_GETTER_FLOAT( Opacity, opacity )
VISUAL_GETTER_FLOAT( RotationAngle, rotation_angle )
VISUAL_GETTER_FLOAT( RotationAngleInDegrees, rotation_angle_in_degrees )
VISUAL_GETTER_VECTOR3( RotationAxis, rotation_axis )
VISUAL_GETTER_VECTOR3( Scale, scale )
VISUAL_GETTER_VECTOR2( Size, size )

static HRESULT WINAPI visual_get_Clip( struct IVisual *iface, struct ICompositionClip **value )
{
    struct visual *impl = impl_from_IVisual( iface );

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    *value = impl->clip;
    if (*value) IInspectable_AddRef( (IInspectable *)*value );
    return S_OK;
}

static HRESULT WINAPI visual_put_Clip( struct IVisual *iface, struct ICompositionClip *value )
{
    struct visual *impl = impl_from_IVisual( iface );

    TRACE( "iface %p, value %p.\n", iface, value );

    if (impl->clip) IInspectable_Release( (IInspectable *)impl->clip );
    impl->clip = value;
    if (impl->clip) IInspectable_AddRef( (IInspectable *)impl->clip );
    return S_OK;
}

static HRESULT WINAPI visual_get_IsVisible( struct IVisual *iface, boolean *value )
{
    struct visual *impl = impl_from_IVisual( iface );

    if (!value) return E_POINTER;
    *value = impl->is_visible;
    return S_OK;
}

static HRESULT WINAPI visual_put_IsVisible( struct IVisual *iface, boolean value )
{
    struct visual *impl = impl_from_IVisual( iface );

    impl->is_visible = value;
    return S_OK;
}

static HRESULT WINAPI visual_get_Orientation( struct IVisual *iface, Quaternion *value )
{
    struct visual *impl = impl_from_IVisual( iface );

    if (!value) return E_POINTER;
    *value = impl->orientation;
    return S_OK;
}

static HRESULT WINAPI visual_put_Orientation( struct IVisual *iface, Quaternion value )
{
    struct visual *impl = impl_from_IVisual( iface );

    impl->orientation = value;
    return S_OK;
}

static HRESULT WINAPI visual_get_Parent( struct IVisual *iface, struct IContainerVisual **value )
{
    struct visual *impl = impl_from_IVisual( iface );

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    *value = NULL;
    if (!impl->parent) return S_OK;
    *value = &impl->parent->IContainerVisual_iface;
    IInspectable_AddRef( (IInspectable *)*value );
    return S_OK;
}

static HRESULT WINAPI visual_get_TransformMatrix( struct IVisual *iface, Matrix4x4 *value )
{
    struct visual *impl = impl_from_IVisual( iface );

    if (!value) return E_POINTER;
    *value = impl->transform_matrix;
    return S_OK;
}

static HRESULT WINAPI visual_put_TransformMatrix( struct IVisual *iface, Matrix4x4 value )
{
    struct visual *impl = impl_from_IVisual( iface );

    impl->transform_matrix = value;
    return S_OK;
}

static const struct IVisualVtbl visual_vtbl =
{
    visual_QueryInterface,
    visual_AddRef,
    visual_Release,
    visual_GetIids,
    visual_GetRuntimeClassName,
    visual_GetTrustLevel,
    visual_get_AnchorPoint,
    visual_put_AnchorPoint,
    visual_get_BackfaceVisibility,
    visual_put_BackfaceVisibility,
    visual_get_BorderMode,
    visual_put_BorderMode,
    visual_get_CenterPoint,
    visual_put_CenterPoint,
    visual_get_Clip,
    visual_put_Clip,
    visual_get_CompositeMode,
    visual_put_CompositeMode,
    visual_get_IsVisible,
    visual_put_IsVisible,
    visual_get_Offset,
    visual_put_Offset,
    visual_get_Opacity,
    visual_put_Opacity,
    visual_get_Orientation,
    visual_put_Orientation,
    visual_get_Parent,
    visual_get_RotationAngle,
    visual_put_RotationAngle,
    visual_get_RotationAngleInDegrees,
    visual_put_RotationAngleInDegrees,
    visual_get_RotationAxis,
    visual_put_RotationAxis,
    visual_get_Scale,
    visual_put_Scale,
    visual_get_Size,
    visual_put_Size,
    visual_get_TransformMatrix,
    visual_put_TransformMatrix,
};

DEFINE_INHERITED_PROLOGUE( container_visual, IContainerVisual, struct visual, IVisual_iface )

static const struct IContainerVisualVtbl container_visual_vtbl =
{
    container_visual_QueryInterface,
    container_visual_AddRef,
    container_visual_Release,
    container_visual_GetIids,
    container_visual_GetRuntimeClassName,
    container_visual_GetTrustLevel,
    container_visual_get_Children,
};

DEFINE_INHERITED_PROLOGUE( sprite_visual, ISpriteVisual, struct visual, IVisual_iface )

static const struct ISpriteVisualVtbl sprite_visual_vtbl =
{
    sprite_visual_QueryInterface,
    sprite_visual_AddRef,
    sprite_visual_Release,
    sprite_visual_GetIids,
    sprite_visual_GetRuntimeClassName,
    sprite_visual_GetTrustLevel,
    sprite_visual_get_Brush,
    sprite_visual_put_Brush,
};

DEFINE_INHERITED_PROLOGUE( visual_object, ICompositionObject, struct visual, IVisual_iface )

static HRESULT WINAPI visual_object_get_Compositor( struct ICompositionObject *iface,
        struct ICompositor **value )
{
    struct visual *impl = impl_from_visual_object( iface );

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    *value = NULL;
    if (!impl->compositor) return S_OK;
    *value = &impl->compositor->ICompositor_iface;
    IInspectable_AddRef( (IInspectable *)*value );
    return S_OK;
}

static HRESULT WINAPI visual_object_get_Dispatcher( struct ICompositionObject *iface,
        struct ICoreDispatcher **value )
{
    FIXME( "iface %p, value %p stub!\n", iface, value );
    if (!value) return E_POINTER;
    *value = NULL;
    return S_OK;
}

static HRESULT WINAPI visual_object_get_Properties( struct ICompositionObject *iface,
        struct ICompositionPropertySet **value )
{
    struct visual *impl = impl_from_visual_object( iface );

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    if (!impl->properties && !(impl->properties = prop_set_create( impl->compositor )))
        return E_OUTOFMEMORY;
    *value = prop_set_get_iface( impl->properties );
    IInspectable_AddRef( (IInspectable *)*value );
    return S_OK;
}

static HRESULT WINAPI visual_object_StartAnimation( struct ICompositionObject *iface,
        HSTRING name, struct ICompositionAnimation *animation )
{
    TRACE( "iface %p, name %s, animation %p stub!\n", iface, debugstr_hstring( name ), animation );
    return S_OK;
}

static HRESULT WINAPI visual_object_StopAnimation( struct ICompositionObject *iface, HSTRING name )
{
    TRACE( "iface %p, name %s stub!\n", iface, debugstr_hstring( name ) );
    return S_OK;
}

static const struct ICompositionObjectVtbl visual_object_vtbl =
{
    visual_object_QueryInterface,
    visual_object_AddRef,
    visual_object_Release,
    visual_object_GetIids,
    visual_object_GetRuntimeClassName,
    visual_object_GetTrustLevel,
    visual_object_get_Compositor,
    visual_object_get_Dispatcher,
    visual_object_get_Properties,
    visual_object_StartAnimation,
    visual_object_StopAnimation,
};

DEFINE_INHERITED_PROLOGUE( visual_closable, IClosable, struct visual, IVisual_iface )

static HRESULT WINAPI visual_closable_Close( struct IClosable *iface )
{
    TRACE( "iface %p stub!\n", iface );
    return S_OK;
}

static const struct IClosableVtbl visual_closable_vtbl =
{
    visual_closable_QueryInterface,
    visual_closable_AddRef,
    visual_closable_Release,
    visual_closable_GetIids,
    visual_closable_GetRuntimeClassName,
    visual_closable_GetTrustLevel,
    visual_closable_Close,
};

/* IContainerVisual */

static HRESULT WINAPI container_visual_get_Children( struct IContainerVisual *iface,
        struct IVisualCollection **value )
{
    struct visual *impl = impl_from_IContainerVisual( iface );
    struct visual_collection *collection;

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    if (!impl->children)
    {
        if (!(collection = visual_collection_create( impl->compositor, impl )))
            return E_OUTOFMEMORY;
        impl->children = collection;
    }
    *value = &impl->children->IVisualCollection_iface;
    IInspectable_AddRef( (IInspectable *)*value );
    return S_OK;
}

/* ISpriteVisual */

static HRESULT WINAPI sprite_visual_get_Brush( struct ISpriteVisual *iface,
        struct ICompositionBrush **value )
{
    struct visual *impl = impl_from_ISpriteVisual( iface );

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    *value = impl->brush;
    if (*value) IInspectable_AddRef( (IInspectable *)*value );
    return S_OK;
}

static HRESULT WINAPI sprite_visual_put_Brush( struct ISpriteVisual *iface,
        struct ICompositionBrush *value )
{
    struct visual *impl = impl_from_ISpriteVisual( iface );

    TRACE( "iface %p, value %p.\n", iface, value );

    if (impl->brush) IInspectable_Release( (IInspectable *)impl->brush );
    impl->brush = value;
    if (impl->brush) IInspectable_AddRef( (IInspectable *)impl->brush );
    return S_OK;
}

static struct visual *visual_create( struct compositor *compositor, BOOL is_sprite )
{
    struct visual *impl;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return NULL;
    impl->IVisual_iface.lpVtbl = &visual_vtbl;
    impl->IContainerVisual_iface.lpVtbl = &container_visual_vtbl;
    impl->ISpriteVisual_iface.lpVtbl = &sprite_visual_vtbl;
    impl->ICompositionObject_iface.lpVtbl = &visual_object_vtbl;
    impl->IClosable_iface.lpVtbl = &visual_closable_vtbl;
    impl->ref = 1;
    impl->compositor = compositor;
    impl->is_sprite = is_sprite;
    impl->is_visible = TRUE;
    impl->opacity = 1.0f;
    impl->scale.X = impl->scale.Y = impl->scale.Z = 1.0f;
    impl->transform_matrix.M11 = impl->transform_matrix.M22 = impl->transform_matrix.M33 =
            impl->transform_matrix.M44 = 1.0f;
    impl->orientation.X = 0.0f;
    impl->orientation.Y = 0.0f;
    impl->orientation.Z = 0.0f;
    impl->orientation.W = 1.0f;
    if (compositor) IInspectable_AddRef( (IInspectable *)&compositor->ICompositor_iface );
    return impl;
}

struct visual *container_visual_create( struct compositor *compositor )
{
    return visual_create( compositor, FALSE );
}

struct visual *sprite_visual_create( struct compositor *compositor )
{
    return visual_create( compositor, TRUE );
}

/* VisualCollection */

static inline struct visual_collection *impl_from_IVisualCollection( struct IVisualCollection *iface )
{
    return CONTAINING_RECORD( iface, struct visual_collection, IVisualCollection_iface );
}


static HRESULT WINAPI collection_QueryInterface( struct IVisualCollection *iface, REFIID iid, void **out )
{
    struct visual_collection *impl = impl_from_IVisualCollection( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown )
        || IsEqualGUID( iid, &IID_IInspectable )
        || IsEqualGUID( iid, &IID_IAgileObject )
        || IsEqualGUID( iid, &IID_IVisualCollection ))
    {
        *out = &impl->IVisualCollection_iface;
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

static ULONG WINAPI collection_AddRef( struct IVisualCollection *iface )
{
    struct visual_collection *impl = impl_from_IVisualCollection( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI collection_Release( struct IVisualCollection *iface )
{
    struct visual_collection *impl = impl_from_IVisualCollection( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        unsigned int i;

        for (i = 0; i < impl->count; i++)
        {
            struct visual *child = impl->items[i];
            if (child->parent == impl->owner) visual_reparent( child, NULL );
            IInspectable_Release( (IInspectable *)&child->IVisual_iface );
        }
        free( impl->items );
        if (impl->compositor)
            IInspectable_Release( (IInspectable *)&impl->compositor->ICompositor_iface );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI collection_GetIids( struct IVisualCollection *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI collection_GetRuntimeClassName( struct IVisualCollection *iface, HSTRING *class_name )
{
    return WindowsCreateString( RuntimeClass_Windows_UI_Composition_VisualCollection,
            ARRAYSIZE( RuntimeClass_Windows_UI_Composition_VisualCollection ) - 1, class_name );
}

static HRESULT WINAPI collection_GetTrustLevel( struct IVisualCollection *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static int collection_find( struct visual_collection *impl, struct visual *child )
{
    unsigned int i;

    for (i = 0; i < impl->count; i++)
        if (impl->items[i] == child) return i;
    return -1;
}

static HRESULT collection_reserve( struct visual_collection *impl, unsigned int count )
{
    if (count <= impl->capacity) return S_OK;
    if (!(impl->items = realloc( impl->items, count * sizeof(*impl->items) )))
        return E_OUTOFMEMORY;
    impl->capacity = count;
    return S_OK;
}

static HRESULT collection_insert( struct visual_collection *impl, unsigned int index, struct visual *child )
{
    HRESULT hr;

    if ((hr = collection_reserve( impl, impl->count + 1 )) != S_OK) return hr;

    if (index > impl->count) index = impl->count;
    memmove( &impl->items[index + 1], &impl->items[index],
            (impl->count - index) * sizeof(*impl->items) );
    impl->items[index] = child;
    impl->count++;
    IInspectable_AddRef( (IInspectable *)&child->IVisual_iface );
    visual_reparent( child, impl->owner );
    return S_OK;
}

static HRESULT collection_remove_at( struct visual_collection *impl, unsigned int index )
{
    struct visual *child;

    if (index >= impl->count) return E_INVALIDARG;
    child = impl->items[index];
    memmove( &impl->items[index], &impl->items[index + 1],
            (impl->count - index - 1) * sizeof(*impl->items) );
    impl->count--;
    if (child->parent == impl->owner) visual_reparent( child, NULL );
    IInspectable_Release( (IInspectable *)&child->IVisual_iface );
    return S_OK;
}

static HRESULT WINAPI collection_get_Count( struct IVisualCollection *iface, INT32 *value )
{
    struct visual_collection *impl = impl_from_IVisualCollection( iface );

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    *value = (INT32)impl->count;
    return S_OK;
}

static struct visual *visual_from_IVisual_iface( struct IVisual *visual_iface )
{
    return impl_from_IVisual( (struct IVisual *)visual_iface );
}

static HRESULT WINAPI collection_InsertAbove( struct IVisualCollection *iface,
        struct IVisual *child, struct IVisual *sibling )
{
    struct visual_collection *impl = impl_from_IVisualCollection( iface );
    int index;

    TRACE( "iface %p, child %p, sibling %p.\n", iface, child, sibling );

    if (!child) return E_INVALIDARG;
    index = collection_find( impl, visual_from_IVisual_iface( sibling ) );
    if (index < 0) return E_INVALIDARG;
    return collection_insert( impl, index + 1, visual_from_IVisual_iface( child ) );
}

static HRESULT WINAPI collection_InsertAtBottom( struct IVisualCollection *iface, struct IVisual *child )
{
    struct visual_collection *impl = impl_from_IVisualCollection( iface );

    TRACE( "iface %p, child %p.\n", iface, child );

    if (!child) return E_INVALIDARG;
    return collection_insert( impl, 0, visual_from_IVisual_iface( child ) );
}

static HRESULT WINAPI collection_InsertAtTop( struct IVisualCollection *iface, struct IVisual *child )
{
    struct visual_collection *impl = impl_from_IVisualCollection( iface );

    TRACE( "iface %p, child %p.\n", iface, child );

    if (!child) return E_INVALIDARG;
    return collection_insert( impl, impl->count, visual_from_IVisual_iface( child ) );
}

static HRESULT WINAPI collection_InsertBelow( struct IVisualCollection *iface,
        struct IVisual *child, struct IVisual *sibling )
{
    struct visual_collection *impl = impl_from_IVisualCollection( iface );
    int index;

    TRACE( "iface %p, child %p, sibling %p.\n", iface, child, sibling );

    if (!child) return E_INVALIDARG;
    index = collection_find( impl, visual_from_IVisual_iface( sibling ) );
    if (index < 0) return E_INVALIDARG;
    return collection_insert( impl, index, visual_from_IVisual_iface( child ) );
}

static HRESULT WINAPI collection_Remove( struct IVisualCollection *iface, struct IVisual *child )
{
    struct visual_collection *impl = impl_from_IVisualCollection( iface );
    int index;

    TRACE( "iface %p, child %p.\n", iface, child );

    if (!child) return E_INVALIDARG;
    index = collection_find( impl, visual_from_IVisual_iface( child ) );
    if (index < 0) return E_INVALIDARG;
    return collection_remove_at( impl, index );
}

static HRESULT WINAPI collection_RemoveAll( struct IVisualCollection *iface )
{
    struct visual_collection *impl = impl_from_IVisualCollection( iface );

    TRACE( "iface %p.\n", iface );

    while (impl->count) collection_remove_at( impl, impl->count - 1 );
    return S_OK;
}

static const struct IVisualCollectionVtbl collection_vtbl =
{
    collection_QueryInterface,
    collection_AddRef,
    collection_Release,
    collection_GetIids,
    collection_GetRuntimeClassName,
    collection_GetTrustLevel,
    collection_get_Count,
    collection_InsertAbove,
    collection_InsertAtBottom,
    collection_InsertAtTop,
    collection_InsertBelow,
    collection_Remove,
    collection_RemoveAll,
};

DEFINE_INHERITED_PROLOGUE( collection_object, ICompositionObject, struct visual_collection, IVisualCollection_iface )

static HRESULT WINAPI collection_object_get_Compositor( struct ICompositionObject *iface,
        struct ICompositor **value )
{
    struct visual_collection *impl = impl_from_collection_object( iface );

    if (!value) return E_POINTER;
    *value = NULL;
    if (!impl->compositor) return S_OK;
    *value = &impl->compositor->ICompositor_iface;
    IInspectable_AddRef( (IInspectable *)*value );
    return S_OK;
}

static HRESULT WINAPI collection_object_get_Dispatcher( struct ICompositionObject *iface,
        struct ICoreDispatcher **value )
{
    FIXME( "iface %p, value %p stub!\n", iface, value );
    if (!value) return E_POINTER;
    *value = NULL;
    return S_OK;
}

static HRESULT WINAPI collection_object_get_Properties( struct ICompositionObject *iface,
        struct ICompositionPropertySet **value )
{
    FIXME( "iface %p, value %p stub!\n", iface, value );
    if (!value) return E_POINTER;
    *value = NULL;
    return S_OK;
}

static HRESULT WINAPI collection_object_StartAnimation( struct ICompositionObject *iface,
        HSTRING name, struct ICompositionAnimation *animation )
{
    TRACE( "iface %p, name %s stub!\n", iface, debugstr_hstring( name ) );
    return S_OK;
}

static HRESULT WINAPI collection_object_StopAnimation( struct ICompositionObject *iface, HSTRING name )
{
    TRACE( "iface %p, name %s stub!\n", iface, debugstr_hstring( name ) );
    return S_OK;
}

static const struct ICompositionObjectVtbl collection_object_vtbl =
{
    collection_object_QueryInterface,
    collection_object_AddRef,
    collection_object_Release,
    collection_object_GetIids,
    collection_object_GetRuntimeClassName,
    collection_object_GetTrustLevel,
    collection_object_get_Compositor,
    collection_object_get_Dispatcher,
    collection_object_get_Properties,
    collection_object_StartAnimation,
    collection_object_StopAnimation,
};

DEFINE_INHERITED_PROLOGUE( collection_closable, IClosable, struct visual_collection, IVisualCollection_iface )

static HRESULT WINAPI collection_closable_Close( struct IClosable *iface )
{
    TRACE( "iface %p stub!\n", iface );
    return S_OK;
}

static const struct IClosableVtbl collection_closable_vtbl =
{
    collection_closable_QueryInterface,
    collection_closable_AddRef,
    collection_closable_Release,
    collection_closable_GetIids,
    collection_closable_GetRuntimeClassName,
    collection_closable_GetTrustLevel,
    collection_closable_Close,
};

static struct visual_collection *visual_collection_create( struct compositor *compositor, struct visual *owner )
{
    struct visual_collection *impl;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return NULL;
    impl->IVisualCollection_iface.lpVtbl = &collection_vtbl;
    impl->ICompositionObject_iface.lpVtbl = &collection_object_vtbl;
    impl->IClosable_iface.lpVtbl = &collection_closable_vtbl;
    impl->ref = 1;
    impl->compositor = compositor;
    impl->owner = owner;
    if (compositor) IInspectable_AddRef( (IInspectable *)&compositor->ICompositor_iface );
    return impl;
}

static HRESULT visual_reparent( struct visual *visual, struct visual *parent )
{
    if (visual->parent == parent) return S_OK;
    if (visual->parent) IInspectable_Release( (IInspectable *)&visual->parent->IContainerVisual_iface );
    visual->parent = parent;
    if (visual->parent) IInspectable_AddRef( (IInspectable *)&visual->parent->IContainerVisual_iface );
    return S_OK;
}
