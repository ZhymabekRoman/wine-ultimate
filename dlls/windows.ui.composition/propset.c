/* WinRT Windows.UI.Composition CompositionPropertySet implementation
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

#define PROP_SET_MAX_ENTRIES 64

enum prop_type
{
    PROP_NONE,
    PROP_COLOR,
    PROP_MATRIX3X2,
    PROP_MATRIX4X4,
    PROP_QUATERNION,
    PROP_SCALAR,
    PROP_VECTOR2,
    PROP_VECTOR3,
    PROP_VECTOR4,
};

struct prop_set_entry
{
    WCHAR *name;
    enum prop_type type;
    union
    {
        Color color;
        Matrix3x2 matrix3x2;
        Matrix4x4 matrix4x4;
        Quaternion quaternion;
        FLOAT scalar;
        Vector2 vector2;
        Vector3 vector3;
        Vector4 vector4;
    } value;
};

struct prop_set
{
    struct ICompositionPropertySet ICompositionPropertySet_iface;
    struct ICompositionObject ICompositionObject_iface;
    struct IClosable IClosable_iface;
    LONG ref;
    struct compositor *compositor;
    struct prop_set_entry entries[PROP_SET_MAX_ENTRIES];
    unsigned int count;
};

static inline struct prop_set *impl_from_ICompositionPropertySet( struct ICompositionPropertySet *iface )
{
    return CONTAINING_RECORD( iface, struct prop_set, ICompositionPropertySet_iface );
}

static struct prop_set_entry *prop_set_find_entry( struct prop_set *impl, const WCHAR *name )
{
    unsigned int i;

    for (i = 0; i < impl->count; i++)
        if (!wcscmp( impl->entries[i].name, name ))
            return &impl->entries[i];
    return NULL;
}

static struct prop_set_entry *prop_set_get_entry( struct prop_set *impl, const WCHAR *name )
{
    struct prop_set_entry *entry = prop_set_find_entry( impl, name );

    if (entry) return entry;
    if (impl->count >= ARRAYSIZE( impl->entries )) return NULL;
    entry = &impl->entries[impl->count];
    if (!(entry->name = wcsdup( name ))) return NULL;
    entry->type = PROP_NONE;
    impl->count++;
    return entry;
}

static HRESULT prop_set_insert( struct prop_set *impl, HSTRING name, enum prop_type type,
        const void *value, size_t size )
{
    const WCHAR *buffer = WindowsGetStringRawBuffer( name, NULL );
    struct prop_set_entry *entry;

    if (!buffer) return E_INVALIDARG;
    if (!(entry = prop_set_get_entry( impl, buffer ))) return E_OUTOFMEMORY;
    if (entry->type && entry->type != type) return E_INVALIDARG;
    entry->type = type;
    memcpy( &entry->value, value, size );
    return S_OK;
}

static HRESULT prop_set_try_get( struct prop_set *impl, HSTRING name, enum prop_type type,
        void *value, size_t size, enum CompositionGetValueStatus *status )
{
    const WCHAR *buffer = WindowsGetStringRawBuffer( name, NULL );
    struct prop_set_entry *entry;

    if (!buffer) return E_INVALIDARG;
    if (!status) return E_POINTER;

    entry = prop_set_find_entry( impl, buffer );
    if (!entry)
    {
        *status = CompositionGetValueStatus_NotFound;
        return S_OK;
    }
    if (entry->type != type)
    {
        *status = CompositionGetValueStatus_TypeMismatch;
        return S_OK;
    }
    if (value) memcpy( value, &entry->value, size );
    *status = CompositionGetValueStatus_Succeeded;
    return S_OK;
}

static HRESULT WINAPI propset_QueryInterface( struct ICompositionPropertySet *iface, REFIID iid, void **out )
{
    struct prop_set *impl = impl_from_ICompositionPropertySet( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown )
        || IsEqualGUID( iid, &IID_IInspectable )
        || IsEqualGUID( iid, &IID_IAgileObject )
        || IsEqualGUID( iid, &IID_ICompositionPropertySet ))
    {
        *out = &impl->ICompositionPropertySet_iface;
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
    if (IsEqualGUID( iid, &IID_ICompositionObjectFactory ))
    {
        *out = &impl->ICompositionPropertySet_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI propset_AddRef( struct ICompositionPropertySet *iface )
{
    struct prop_set *impl = impl_from_ICompositionPropertySet( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI propset_Release( struct ICompositionPropertySet *iface )
{
    struct prop_set *impl = impl_from_ICompositionPropertySet( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        unsigned int i;

        for (i = 0; i < impl->count; i++) free( impl->entries[i].name );
        if (impl->compositor)
            IInspectable_Release( (IInspectable *)&impl->compositor->ICompositor_iface );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI propset_GetIids( struct ICompositionPropertySet *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI propset_GetRuntimeClassName( struct ICompositionPropertySet *iface, HSTRING *class_name )
{
    return WindowsCreateString( RuntimeClass_Windows_UI_Composition_CompositionPropertySet,
            ARRAYSIZE( RuntimeClass_Windows_UI_Composition_CompositionPropertySet ) - 1, class_name );
}

static HRESULT WINAPI propset_GetTrustLevel( struct ICompositionPropertySet *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI propset_InsertColor( struct ICompositionPropertySet *iface, HSTRING name, Color value )
{
    struct prop_set *impl = impl_from_ICompositionPropertySet( iface );

    TRACE( "iface %p, name %s.\n", iface, debugstr_hstring( name ) );

    return prop_set_insert( impl, name, PROP_COLOR, &value, sizeof(value) );
}

static HRESULT WINAPI propset_InsertMatrix3x2( struct ICompositionPropertySet *iface, HSTRING name, Matrix3x2 value )
{
    struct prop_set *impl = impl_from_ICompositionPropertySet( iface );

    TRACE( "iface %p, name %s.\n", iface, debugstr_hstring( name ) );

    return prop_set_insert( impl, name, PROP_MATRIX3X2, &value, sizeof(value) );
}

static HRESULT WINAPI propset_InsertMatrix4x4( struct ICompositionPropertySet *iface, HSTRING name, Matrix4x4 value )
{
    struct prop_set *impl = impl_from_ICompositionPropertySet( iface );

    TRACE( "iface %p, name %s.\n", iface, debugstr_hstring( name ) );

    return prop_set_insert( impl, name, PROP_MATRIX4X4, &value, sizeof(value) );
}

static HRESULT WINAPI propset_InsertQuaternion( struct ICompositionPropertySet *iface, HSTRING name, Quaternion value )
{
    struct prop_set *impl = impl_from_ICompositionPropertySet( iface );

    TRACE( "iface %p, name %s.\n", iface, debugstr_hstring( name ) );

    return prop_set_insert( impl, name, PROP_QUATERNION, &value, sizeof(value) );
}

static HRESULT WINAPI propset_InsertScalar( struct ICompositionPropertySet *iface, HSTRING name, FLOAT value )
{
    struct prop_set *impl = impl_from_ICompositionPropertySet( iface );

    TRACE( "iface %p, name %s, value %g.\n", iface, debugstr_hstring( name ), value );

    return prop_set_insert( impl, name, PROP_SCALAR, &value, sizeof(value) );
}

static HRESULT WINAPI propset_InsertVector2( struct ICompositionPropertySet *iface, HSTRING name, Vector2 value )
{
    struct prop_set *impl = impl_from_ICompositionPropertySet( iface );

    TRACE( "iface %p, name %s.\n", iface, debugstr_hstring( name ) );

    return prop_set_insert( impl, name, PROP_VECTOR2, &value, sizeof(value) );
}

static HRESULT WINAPI propset_InsertVector3( struct ICompositionPropertySet *iface, HSTRING name, Vector3 value )
{
    struct prop_set *impl = impl_from_ICompositionPropertySet( iface );

    TRACE( "iface %p, name %s.\n", iface, debugstr_hstring( name ) );

    return prop_set_insert( impl, name, PROP_VECTOR3, &value, sizeof(value) );
}

static HRESULT WINAPI propset_InsertVector4( struct ICompositionPropertySet *iface, HSTRING name, Vector4 value )
{
    struct prop_set *impl = impl_from_ICompositionPropertySet( iface );

    TRACE( "iface %p, name %s.\n", iface, debugstr_hstring( name ) );

    return prop_set_insert( impl, name, PROP_VECTOR4, &value, sizeof(value) );
}

static HRESULT WINAPI propset_TryGetColor( struct ICompositionPropertySet *iface, HSTRING name,
        Color *value, enum CompositionGetValueStatus *status )
{
    struct prop_set *impl = impl_from_ICompositionPropertySet( iface );

    return prop_set_try_get( impl, name, PROP_COLOR, value, sizeof(*value), status );
}

static HRESULT WINAPI propset_TryGetMatrix3x2( struct ICompositionPropertySet *iface, HSTRING name,
        Matrix3x2 *value, enum CompositionGetValueStatus *status )
{
    struct prop_set *impl = impl_from_ICompositionPropertySet( iface );

    return prop_set_try_get( impl, name, PROP_MATRIX3X2, value, sizeof(*value), status );
}

static HRESULT WINAPI propset_TryGetMatrix4x4( struct ICompositionPropertySet *iface, HSTRING name,
        Matrix4x4 *value, enum CompositionGetValueStatus *status )
{
    struct prop_set *impl = impl_from_ICompositionPropertySet( iface );

    return prop_set_try_get( impl, name, PROP_MATRIX4X4, value, sizeof(*value), status );
}

static HRESULT WINAPI propset_TryGetQuaternion( struct ICompositionPropertySet *iface, HSTRING name,
        Quaternion *value, enum CompositionGetValueStatus *status )
{
    struct prop_set *impl = impl_from_ICompositionPropertySet( iface );

    return prop_set_try_get( impl, name, PROP_QUATERNION, value, sizeof(*value), status );
}

static HRESULT WINAPI propset_TryGetScalar( struct ICompositionPropertySet *iface, HSTRING name,
        FLOAT *value, enum CompositionGetValueStatus *status )
{
    struct prop_set *impl = impl_from_ICompositionPropertySet( iface );

    return prop_set_try_get( impl, name, PROP_SCALAR, value, sizeof(*value), status );
}

static HRESULT WINAPI propset_TryGetVector2( struct ICompositionPropertySet *iface, HSTRING name,
        Vector2 *value, enum CompositionGetValueStatus *status )
{
    struct prop_set *impl = impl_from_ICompositionPropertySet( iface );

    return prop_set_try_get( impl, name, PROP_VECTOR2, value, sizeof(*value), status );
}

static HRESULT WINAPI propset_TryGetVector3( struct ICompositionPropertySet *iface, HSTRING name,
        Vector3 *value, enum CompositionGetValueStatus *status )
{
    struct prop_set *impl = impl_from_ICompositionPropertySet( iface );

    return prop_set_try_get( impl, name, PROP_VECTOR3, value, sizeof(*value), status );
}

static HRESULT WINAPI propset_TryGetVector4( struct ICompositionPropertySet *iface, HSTRING name,
        Vector4 *value, enum CompositionGetValueStatus *status )
{
    struct prop_set *impl = impl_from_ICompositionPropertySet( iface );

    return prop_set_try_get( impl, name, PROP_VECTOR4, value, sizeof(*value), status );
}

static const struct ICompositionPropertySetVtbl propset_vtbl =
{
    propset_QueryInterface,
    propset_AddRef,
    propset_Release,
    propset_GetIids,
    propset_GetRuntimeClassName,
    propset_GetTrustLevel,
    propset_InsertColor,
    propset_InsertMatrix3x2,
    propset_InsertMatrix4x4,
    propset_InsertQuaternion,
    propset_InsertScalar,
    propset_InsertVector2,
    propset_InsertVector3,
    propset_InsertVector4,
    propset_TryGetColor,
    propset_TryGetMatrix3x2,
    propset_TryGetMatrix4x4,
    propset_TryGetQuaternion,
    propset_TryGetScalar,
    propset_TryGetVector2,
    propset_TryGetVector3,
    propset_TryGetVector4,
};

DEFINE_INHERITED_PROLOGUE( propset_object, ICompositionObject, struct prop_set, ICompositionPropertySet_iface )

static inline struct prop_set *impl_from_ICompositionObject( struct ICompositionObject *iface )
{
    return CONTAINING_RECORD( iface, struct prop_set, ICompositionObject_iface );
}

static HRESULT WINAPI propset_object_get_Compositor( struct ICompositionObject *iface,
        struct ICompositor **value )
{
    struct prop_set *impl = impl_from_ICompositionObject( iface );

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    if (!impl->compositor)
    {
        *value = NULL;
        return S_OK;
    }
    *value = &impl->compositor->ICompositor_iface;
    IInspectable_AddRef( (IInspectable *)*value );
    return S_OK;
}

static HRESULT WINAPI propset_object_get_Dispatcher( struct ICompositionObject *iface,
        struct ICoreDispatcher **value )
{
    FIXME( "iface %p, value %p stub!\n", iface, value );
    if (!value) return E_POINTER;
    *value = NULL;
    return S_OK;
}

static HRESULT WINAPI propset_object_get_Properties( struct ICompositionObject *iface,
        struct ICompositionPropertySet **value )
{
    struct prop_set *impl = impl_from_ICompositionObject( iface );

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    *value = &impl->ICompositionPropertySet_iface;
    IInspectable_AddRef( (IInspectable *)*value );
    return S_OK;
}

static HRESULT WINAPI propset_object_StartAnimation( struct ICompositionObject *iface,
        HSTRING name, struct ICompositionAnimation *animation )
{
    TRACE( "iface %p, name %s, animation %p stub!\n", iface, debugstr_hstring( name ), animation );
    return S_OK;
}

static HRESULT WINAPI propset_object_StopAnimation( struct ICompositionObject *iface, HSTRING name )
{
    TRACE( "iface %p, name %s stub!\n", iface, debugstr_hstring( name ) );
    return S_OK;
}

static const struct ICompositionObjectVtbl propset_object_vtbl =
{
    propset_object_QueryInterface,
    propset_object_AddRef,
    propset_object_Release,
    propset_object_GetIids,
    propset_object_GetRuntimeClassName,
    propset_object_GetTrustLevel,
    propset_object_get_Compositor,
    propset_object_get_Dispatcher,
    propset_object_get_Properties,
    propset_object_StartAnimation,
    propset_object_StopAnimation,
};

DEFINE_INHERITED_PROLOGUE( propset_closable, IClosable, struct prop_set, ICompositionPropertySet_iface )

static HRESULT WINAPI propset_closable_Close( struct IClosable *iface )
{
    TRACE( "iface %p stub!\n", iface );
    return S_OK;
}

struct ICompositionPropertySet *prop_set_get_iface( struct prop_set *set )
{
    return &set->ICompositionPropertySet_iface;
}

static const struct IClosableVtbl propset_closable_vtbl =
{
    propset_closable_QueryInterface,
    propset_closable_AddRef,
    propset_closable_Release,
    propset_closable_GetIids,
    propset_closable_GetRuntimeClassName,
    propset_closable_GetTrustLevel,
    propset_closable_Close,
};

struct prop_set *prop_set_create( struct compositor *compositor )
{
    struct prop_set *impl;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return NULL;
    impl->ICompositionPropertySet_iface.lpVtbl = &propset_vtbl;
    impl->ICompositionObject_iface.lpVtbl = &propset_object_vtbl;
    impl->IClosable_iface.lpVtbl = &propset_closable_vtbl;
    impl->ref = 1;
    impl->compositor = compositor;
    if (compositor) IInspectable_AddRef( (IInspectable *)&compositor->ICompositor_iface );
    return impl;
}
