/* WinRT DataPackagePropertySet / DataPackagePropertySetView objects
 *
 * Copyright 2026 Wine Developers
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

WINE_DEFAULT_DEBUG_CHANNEL(dataexchange);

/*
 * The property set the app decorates a DataPackage with: the named properties
 * (Title, Description, ...) live as plain fields, the IMap`2<HSTRING,
 * IInspectable*> surface the projection exposes is the generic map from
 * collections.c, and FileTypes is a real IVector`1<HSTRING>.  The view
 * snapshots the set; its map side answers the read-only IMapView`2 half of
 * the same storage contract.  Everything is agile; GetIids/GetTrustLevel
 * follow the tree convention of E_NOTIMPL.
 */

struct property_set
{
    const void *set_vtbl;       /* IDataPackagePropertySet (set) / IDataPackagePropertySetView (view) at offset 0 */
    const void *set2_vtbl;
    const void *set3_vtbl;
    const void *set4_vtbl;
    const void *set5_vtbl;      /* view-only: IDataPackagePropertySetView5 */
    LONG ref;
    BOOL is_view;
    const WCHAR *class_name;
    struct winrt_map *map;
    HSTRING title, description, appname, package_family, enterprise_id, user_activity_json;
    void *thumbnail, *square30, *app_listing_uri, *web_link, *app_link;
    __x_ABI_CWindows_CUI_CColor bg;
    struct winrt_vector *file_types;
};

static const void *property_set_vtbl[];
static const void *property_set2_vtbl[];
static const void *property_set3_vtbl[];
static const void *property_set4_vtbl[];
static const void *property_set_view5_vtbl[];
static const void *property_set_view_vtbl[];
static const void *property_set_view2_vtbl[];
static const void *property_set_view3_vtbl[];
static const void *property_set_view4_vtbl[];

/* Every interface group of the object shares the IUnknown/entry-point
 * implementations; resolve the object from the group's vtable pointer so one
 * set of method bodies serves every group at its own field offset. */
static struct property_set *impl_from_any( const void *iface )
{
    static const void *groups[] = { property_set_vtbl, property_set2_vtbl, property_set3_vtbl, property_set4_vtbl, property_set_view5_vtbl, property_set_view_vtbl, property_set_view2_vtbl, property_set_view3_vtbl, property_set_view4_vtbl };
    static const int offsets[] = { 0, 1, 2, 3, 4, 0, 1, 2, 3 };
    const void *vtbl = *(const void *const *)iface;
    int i;

    for (i = 0; i < (int)ARRAY_SIZE( groups ); i++)
        if (vtbl == groups[i])
            return (struct property_set *)((const BYTE *)iface - offsets[i] * sizeof(void *));
    return (struct property_set *)iface;
}

static void set_hstring( HSTRING *field, HSTRING value )
{
    HSTRING dup = NULL;

    if (value) WindowsDuplicateString( value, &dup );
    WindowsDeleteString( *field );
    *field = dup;
}

static void set_com( void **field, void *value )
{
    if (value) IUnknown_AddRef( (IUnknown *)value );
    if (*field) IUnknown_Release( (IUnknown *)*field );
    *field = value;
}

static void property_set_destroy( struct property_set *impl )
{
    WindowsDeleteString( impl->title );
    WindowsDeleteString( impl->description );
    WindowsDeleteString( impl->appname );
    WindowsDeleteString( impl->package_family );
    WindowsDeleteString( impl->enterprise_id );
    WindowsDeleteString( impl->user_activity_json );
    set_com( &impl->thumbnail, NULL );
    set_com( &impl->square30, NULL );
    set_com( &impl->app_listing_uri, NULL );
    set_com( &impl->web_link, NULL );
    set_com( &impl->app_link, NULL );
    if (impl->map)
    {
        /* The map delegates its IUnknown to the set; drop that link first so
         * destroying the map does not re-enter this object's Release at ref 0. */
        map_set_outer( impl->map, NULL );
        map_release( impl->map );
    }
    if (impl->file_types) vector_release( impl->file_types );
    free( impl );
}

static HRESULT WINAPI set_QueryInterface( const void *iface, REFIID iid, void **out )
{
    struct property_set *impl = impl_from_any( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (!out) return E_POINTER;
    *out = NULL;

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        (impl->is_view ? IsEqualGUID( iid, &IID_IDataPackagePropertySetView )
                       : IsEqualGUID( iid, &IID_IDataPackagePropertySet )))
    {
        *out = (void *)impl;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    if (!impl->is_view)
    {
        if (IsEqualGUID( iid, &IID_IDataPackagePropertySet2 ))
        {
            *out = (void *)&impl->set2_vtbl;
            InterlockedIncrement( &impl->ref );
            return S_OK;
        }
        if (IsEqualGUID( iid, &IID_IDataPackagePropertySet3 ))
        {
            *out = (void *)&impl->set3_vtbl;
            InterlockedIncrement( &impl->ref );
            return S_OK;
        }
        if (IsEqualGUID( iid, &IID_IDataPackagePropertySet4 ))
        {
            *out = (void *)&impl->set4_vtbl;
            InterlockedIncrement( &impl->ref );
            return S_OK;
        }
        if (IsEqualGUID( iid, &IID___FIMap_2_HSTRING_IInspectable ))
        {
            *out = map_iface( impl->map );
            InterlockedIncrement( &impl->ref ); /* the map's IUnknown delegates here */
            return S_OK;
        }
    }
    else
    {
        if (IsEqualGUID( iid, &IID_IDataPackagePropertySetView2 ))
        {
            *out = (void *)&impl->set2_vtbl;
            InterlockedIncrement( &impl->ref );
            return S_OK;
        }
        if (IsEqualGUID( iid, &IID_IDataPackagePropertySetView3 ))
        {
            *out = (void *)&impl->set3_vtbl;
            InterlockedIncrement( &impl->ref );
            return S_OK;
        }
        if (IsEqualGUID( iid, &IID_IDataPackagePropertySetView4 ))
        {
            *out = (void *)&impl->set4_vtbl;
            InterlockedIncrement( &impl->ref );
            return S_OK;
        }
        if (IsEqualGUID( iid, &IID_IDataPackagePropertySetView5 ))
        {
            *out = (void *)&impl->set5_vtbl;
            InterlockedIncrement( &impl->ref );
            return S_OK;
        }
        if (IsEqualGUID( iid, &IID___FIMapView_2_HSTRING_IInspectable ))
        {
            *out = map_view_iface( impl->map );
            InterlockedIncrement( &impl->ref ); /* the map's IUnknown delegates here */
            return S_OK;
        }
    }

    if (IsEqualGUID( iid, &IID___FIIterable_1___FIKeyValuePair_2_HSTRING_IInspectable ))
    {
        *out = map_iterable_iface( impl->map );
        InterlockedIncrement( &impl->ref ); /* the map's IUnknown delegates here */
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG WINAPI set_AddRef( const void *iface )
{
    struct property_set *impl = impl_from_any( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI set_Release( const void *iface )
{
    struct property_set *impl = impl_from_any( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref) property_set_destroy( impl );
    return ref;
}

static HRESULT WINAPI set_GetIids( const void *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI set_GetRuntimeClassName( const void *iface, HSTRING *class_name )
{
    struct property_set *impl = impl_from_any( iface );

    if (!class_name) return E_POINTER;
    return WindowsCreateString( impl->class_name, lstrlenW( impl->class_name ), class_name );
}

static HRESULT WINAPI set_GetTrustLevel( const void *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

/* Shared field accessors, wired into both the set and the view vtables. */

static HRESULT WINAPI set_get_Title( const void *iface, HSTRING *value )
{
    struct property_set *impl = impl_from_any( iface );

    TRACE( "iface %p, value %p.\n", iface, value );
    if (!value) return E_POINTER;
    return WindowsDuplicateString( impl->title, value );
}

static HRESULT WINAPI set_put_Title( const void *iface, HSTRING value )
{
    struct property_set *impl = impl_from_any( iface );

    TRACE( "iface %p, value %s.\n", iface, debugstr_hstring( value ) );
    set_hstring( &impl->title, value );
    return S_OK;
}

static HRESULT WINAPI set_get_Description( const void *iface, HSTRING *value )
{
    struct property_set *impl = impl_from_any( iface );

    TRACE( "iface %p, value %p.\n", iface, value );
    if (!value) return E_POINTER;
    return WindowsDuplicateString( impl->description, value );
}

static HRESULT WINAPI set_put_Description( const void *iface, HSTRING value )
{
    struct property_set *impl = impl_from_any( iface );

    TRACE( "iface %p, value %s.\n", iface, debugstr_hstring( value ) );
    set_hstring( &impl->description, value );
    return S_OK;
}

static HRESULT WINAPI set_get_Thumbnail( const void *iface, void **value )
{
    struct property_set *impl = impl_from_any( iface );

    TRACE( "iface %p, value %p.\n", iface, value );
    if (!value) return E_POINTER;
    *value = impl->thumbnail;
    if (*value) IUnknown_AddRef( (IUnknown *)*value );
    return S_OK;
}

static HRESULT WINAPI set_put_Thumbnail( const void *iface, void *value )
{
    struct property_set *impl = impl_from_any( iface );

    TRACE( "iface %p, value %p.\n", iface, value );
    set_com( &impl->thumbnail, value );
    return S_OK;
}

static HRESULT WINAPI set_get_FileTypes( const void *iface, void **value )
{
    struct property_set *impl = impl_from_any( iface );

    TRACE( "iface %p, value %p.\n", iface, value );
    if (!value) return E_POINTER;

    if (impl->is_view) *value = vector_view_iface( impl->file_types );
    else *value = vector_iface( impl->file_types );
    vector_add_ref( impl->file_types );
    return S_OK;
}

static HRESULT WINAPI set_get_ApplicationName( const void *iface, HSTRING *value )
{
    struct property_set *impl = impl_from_any( iface );

    TRACE( "iface %p, value %p.\n", iface, value );
    if (!value) return E_POINTER;
    return WindowsDuplicateString( impl->appname, value );
}

static HRESULT WINAPI set_put_ApplicationName( const void *iface, HSTRING value )
{
    struct property_set *impl = impl_from_any( iface );

    TRACE( "iface %p, value %s.\n", iface, debugstr_hstring( value ) );
    set_hstring( &impl->appname, value );
    return S_OK;
}

static HRESULT WINAPI set_get_ApplicationListingUri( const void *iface, void **value )
{
    struct property_set *impl = impl_from_any( iface );

    TRACE( "iface %p, value %p.\n", iface, value );
    if (!value) return E_POINTER;
    *value = impl->app_listing_uri;
    if (*value) IUnknown_AddRef( (IUnknown *)*value );
    return S_OK;
}

static HRESULT WINAPI set_put_ApplicationListingUri( const void *iface, void *value )
{
    struct property_set *impl = impl_from_any( iface );

    TRACE( "iface %p, value %p.\n", iface, value );
    set_com( &impl->app_listing_uri, value );
    return S_OK;
}

static HRESULT WINAPI set2_QueryInterface( const void *iface, REFIID iid, void **out )
{
    return set_QueryInterface( (void *)impl_from_any( iface ), iid, out );
}

static ULONG WINAPI set2_AddRef( const void *iface )
{
    return set_AddRef( (void *)impl_from_any( iface ) );
}

static ULONG WINAPI set2_Release( const void *iface )
{
    return set_Release( (void *)impl_from_any( iface ) );
}

static HRESULT WINAPI set3_QueryInterface( const void *iface, REFIID iid, void **out )
{
    return set_QueryInterface( (void *)impl_from_any( iface ), iid, out );
}

static ULONG WINAPI set3_AddRef( const void *iface )
{
    return set_AddRef( (void *)impl_from_any( iface ) );
}

static ULONG WINAPI set3_Release( const void *iface )
{
    return set_Release( (void *)impl_from_any( iface ) );
}

static HRESULT WINAPI set4_QueryInterface( const void *iface, REFIID iid, void **out )
{
    return set_QueryInterface( (void *)impl_from_any( iface ), iid, out );
}

static ULONG WINAPI set4_AddRef( const void *iface )
{
    return set_AddRef( (void *)impl_from_any( iface ) );
}

static ULONG WINAPI set4_Release( const void *iface )
{
    return set_Release( (void *)impl_from_any( iface ) );
}

static HRESULT WINAPI set5_QueryInterface( const void *iface, REFIID iid, void **out )
{
    return set_QueryInterface( (void *)impl_from_any( iface ), iid, out );
}

static ULONG WINAPI set5_AddRef( const void *iface )
{
    return set_AddRef( (void *)impl_from_any( iface ) );
}

static ULONG WINAPI set5_Release( const void *iface )
{
    return set_Release( (void *)impl_from_any( iface ) );
}

static HRESULT WINAPI set2_get_ContentSourceWebLink( const void *iface, void **value )
{
    struct property_set *impl = impl_from_any( iface );

    TRACE( "iface %p, value %p.\n", iface, value );
    if (!value) return E_POINTER;
    *value = impl->web_link;
    if (*value) IUnknown_AddRef( (IUnknown *)*value );
    return S_OK;
}

static HRESULT WINAPI set2_put_ContentSourceWebLink( const void *iface, void *value )
{
    struct property_set *impl = impl_from_any( iface );

    TRACE( "iface %p, value %p.\n", iface, value );
    set_com( &impl->web_link, value );
    return S_OK;
}

static HRESULT WINAPI set2_get_ContentSourceApplicationLink( const void *iface, void **value )
{
    struct property_set *impl = impl_from_any( iface );

    TRACE( "iface %p, value %p.\n", iface, value );
    if (!value) return E_POINTER;
    *value = impl->app_link;
    if (*value) IUnknown_AddRef( (IUnknown *)*value );
    return S_OK;
}

static HRESULT WINAPI set2_put_ContentSourceApplicationLink( const void *iface, void *value )
{
    struct property_set *impl = impl_from_any( iface );

    TRACE( "iface %p, value %p.\n", iface, value );
    set_com( &impl->app_link, value );
    return S_OK;
}

static HRESULT WINAPI set2_get_PackageFamilyName( const void *iface, HSTRING *value )
{
    struct property_set *impl = impl_from_any( iface );

    TRACE( "iface %p, value %p.\n", iface, value );
    if (!value) return E_POINTER;
    return WindowsDuplicateString( impl->package_family, value );
}

static HRESULT WINAPI set2_put_PackageFamilyName( const void *iface, HSTRING value )
{
    struct property_set *impl = impl_from_any( iface );

    TRACE( "iface %p, value %s.\n", iface, debugstr_hstring( value ) );
    set_hstring( &impl->package_family, value );
    return S_OK;
}

static HRESULT WINAPI set2_get_Square30x30Logo( const void *iface, void **value )
{
    struct property_set *impl = impl_from_any( iface );

    TRACE( "iface %p, value %p.\n", iface, value );
    if (!value) return E_POINTER;
    *value = impl->square30;
    if (*value) IUnknown_AddRef( (IUnknown *)*value );
    return S_OK;
}

static HRESULT WINAPI set2_put_Square30x30Logo( const void *iface, void *value )
{
    struct property_set *impl = impl_from_any( iface );

    TRACE( "iface %p, value %p.\n", iface, value );
    set_com( &impl->square30, value );
    return S_OK;
}

static HRESULT WINAPI set2_get_LogoBackgroundColor( const void *iface, __x_ABI_CWindows_CUI_CColor *value )
{
    struct property_set *impl = impl_from_any( iface );

    TRACE( "iface %p, value %p.\n", iface, value );
    if (!value) return E_POINTER;
    *value = impl->bg;
    return S_OK;
}

static HRESULT WINAPI set2_put_LogoBackgroundColor( const void *iface, __x_ABI_CWindows_CUI_CColor value )
{
    struct property_set *impl = impl_from_any( iface );

    TRACE( "iface %p.\n", iface );
    impl->bg = value;
    return S_OK;
}

static HRESULT WINAPI set3_get_EnterpriseId( const void *iface, HSTRING *value )
{
    struct property_set *impl = impl_from_any( iface );

    TRACE( "iface %p, value %p.\n", iface, value );
    if (!value) return E_POINTER;
    return WindowsDuplicateString( impl->enterprise_id, value );
}

static HRESULT WINAPI set3_put_EnterpriseId( const void *iface, HSTRING value )
{
    struct property_set *impl = impl_from_any( iface );

    TRACE( "iface %p, value %s.\n", iface, debugstr_hstring( value ) );
    set_hstring( &impl->enterprise_id, value );
    return S_OK;
}

static HRESULT WINAPI set4_get_ContentSourceUserActivityJson( const void *iface, HSTRING *value )
{
    struct property_set *impl = impl_from_any( iface );

    TRACE( "iface %p, value %p.\n", iface, value );
    if (!value) return E_POINTER;
    return WindowsDuplicateString( impl->user_activity_json, value );
}

static HRESULT WINAPI set4_put_ContentSourceUserActivityJson( const void *iface, HSTRING value )
{
    struct property_set *impl = impl_from_any( iface );

    TRACE( "iface %p, value %s.\n", iface, debugstr_hstring( value ) );
    set_hstring( &impl->user_activity_json, value );
    return S_OK;
}

static HRESULT WINAPI set5_get_IsFromRoamingClipboard( const void *iface, BOOLEAN *value )
{
    TRACE( "iface %p, value %p.\n", iface, value );
    if (!value) return E_POINTER;
    *value = FALSE;
    return S_OK;
}

/* The map-side mutation entry points of the set live in the generic map
 * (collections.c); the set itself only exposes the named properties. */

static const void *property_set_vtbl[] =
{
    set_QueryInterface,
    set_AddRef,
    set_Release,
    set_GetIids,
    set_GetRuntimeClassName,
    set_GetTrustLevel,
    set_get_Title,
    set_put_Title,
    set_get_Description,
    set_put_Description,
    set_get_Thumbnail,
    set_put_Thumbnail,
    set_get_FileTypes,
    set_get_ApplicationName,
    set_put_ApplicationName,
    set_get_ApplicationListingUri,
    set_put_ApplicationListingUri,
};

static const void *property_set2_vtbl[] =
{
    set2_QueryInterface,
    set2_AddRef,
    set2_Release,
    set_GetIids,
    set_GetRuntimeClassName,
    set_GetTrustLevel,
    set2_get_ContentSourceWebLink,
    set2_put_ContentSourceWebLink,
    set2_get_ContentSourceApplicationLink,
    set2_put_ContentSourceApplicationLink,
    set2_get_PackageFamilyName,
    set2_put_PackageFamilyName,
    set2_get_Square30x30Logo,
    set2_put_Square30x30Logo,
    set2_get_LogoBackgroundColor,
    set2_put_LogoBackgroundColor,
};

static const void *property_set3_vtbl[] =
{
    set3_QueryInterface,
    set3_AddRef,
    set3_Release,
    set_GetIids,
    set_GetRuntimeClassName,
    set_GetTrustLevel,
    set3_get_EnterpriseId,
    set3_put_EnterpriseId,
};

static const void *property_set4_vtbl[] =
{
    set4_QueryInterface,
    set4_AddRef,
    set4_Release,
    set_GetIids,
    set_GetRuntimeClassName,
    set_GetTrustLevel,
    set4_get_ContentSourceUserActivityJson,
    set4_put_ContentSourceUserActivityJson,
};

/* The view vtables: the same accessors, minus every propput entry point. */

static const void *property_set_view_vtbl[] =
{
    set_QueryInterface,
    set_AddRef,
    set_Release,
    set_GetIids,
    set_GetRuntimeClassName,
    set_GetTrustLevel,
    set_get_Title,
    set_get_Description,
    set_get_Thumbnail,
    set_get_FileTypes,
    set_get_ApplicationName,
    set_get_ApplicationListingUri,
};

static const void *property_set_view2_vtbl[] =
{
    set2_QueryInterface,
    set2_AddRef,
    set2_Release,
    set_GetIids,
    set_GetRuntimeClassName,
    set_GetTrustLevel,
    set2_get_PackageFamilyName,
    set2_get_ContentSourceWebLink,
    set2_get_ContentSourceApplicationLink,
    set2_get_Square30x30Logo,
    set2_get_LogoBackgroundColor,
};

static const void *property_set_view3_vtbl[] =
{
    set3_QueryInterface,
    set3_AddRef,
    set3_Release,
    set_GetIids,
    set_GetRuntimeClassName,
    set_GetTrustLevel,
    set3_get_EnterpriseId,
};

static const void *property_set_view4_vtbl[] =
{
    set4_QueryInterface,
    set4_AddRef,
    set4_Release,
    set_GetIids,
    set_GetRuntimeClassName,
    set_GetTrustLevel,
    set4_get_ContentSourceUserActivityJson,
};

static const void *property_set_view5_vtbl[] =
{
    set5_QueryInterface,
    set5_AddRef,
    set5_Release,
    set_GetIids,
    set_GetRuntimeClassName,
    set_GetTrustLevel,
    set5_get_IsFromRoamingClipboard,
};

static const IID *iid_kvp_iterator = &IID___FIIterator_1___FIKeyValuePair_2_HSTRING_IInspectable;
static const IID *iid_kvp = &IID___FIKeyValuePair_2_HSTRING_IInspectable;

static struct property_set *create_property_set_object( BOOL is_view )
{
    struct property_set *impl;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return NULL;

    if (is_view)
    {
        impl->set_vtbl = property_set_view_vtbl;
        impl->set2_vtbl = property_set_view2_vtbl;
        impl->set3_vtbl = property_set_view3_vtbl;
        impl->set4_vtbl = property_set_view4_vtbl;
        impl->set5_vtbl = property_set_view5_vtbl;
    }
    else
    {
        impl->set_vtbl = property_set_vtbl;
        impl->set2_vtbl = property_set2_vtbl;
        impl->set3_vtbl = property_set3_vtbl;
        impl->set4_vtbl = property_set4_vtbl;
        impl->set5_vtbl = property_set_view5_vtbl;
    }
    impl->ref = 1;
    impl->is_view = is_view;
    impl->class_name = is_view ? RuntimeClass_Windows_ApplicationModel_DataTransfer_DataPackagePropertySetView
                               : RuntimeClass_Windows_ApplicationModel_DataTransfer_DataPackagePropertySet;
    impl->bg.A = 0;
    impl->bg.R = 0;
    impl->bg.G = 0;
    impl->bg.B = 0;

    impl->map = create_map( is_view ? NULL : &IID___FIMap_2_HSTRING_IInspectable,
                            is_view ? &IID___FIMapView_2_HSTRING_IInspectable : NULL,
                            &IID___FIIterable_1___FIKeyValuePair_2_HSTRING_IInspectable,
                            iid_kvp_iterator, iid_kvp, ELEM_COM,
                            L"Windows.Foundation.Collections.IMap`2<HSTRING, IInspectable>" );
    if (!impl->map)
    {
        free( impl );
        return NULL;
    }
    map_set_outer( impl->map, (IInspectable *)impl );

    impl->file_types = create_vector( &IID___FIVector_1_HSTRING, &IID___FIVectorView_1_HSTRING,
                                      &IID___FIIterable_1_HSTRING, &IID___FIIterator_1_HSTRING,
                                      ELEM_STRING, L"Windows.Foundation.Collections.IVector`1<HSTRING>" );
    if (!impl->file_types)
    {
        map_release( impl->map );
        free( impl );
        return NULL;
    }

    TRACE( "created property set %p (view %d).\n", impl, is_view );
    return impl;
}

IInspectable *create_property_set( void )
{
    return (IInspectable *)create_property_set_object( FALSE );
}

IInspectable *create_property_set_view( struct property_set *set )
{
    struct property_set *view;
    ULONG i;

    if (!(view = create_property_set_object( TRUE ))) return NULL;

    if (set)
    {
        set_hstring( &view->title, set->title );
        set_hstring( &view->description, set->description );
        set_hstring( &view->appname, set->appname );
        set_hstring( &view->package_family, set->package_family );
        set_hstring( &view->enterprise_id, set->enterprise_id );
        set_hstring( &view->user_activity_json, set->user_activity_json );
        set_com( &view->thumbnail, set->thumbnail );
        set_com( &view->square30, set->square30 );
        set_com( &view->app_listing_uri, set->app_listing_uri );
        set_com( &view->web_link, set->web_link );
        set_com( &view->app_link, set->app_link );
        view->bg = set->bg;

        for (i = 0; i < vector_size( set->file_types ); i++)
            vector_append( view->file_types, vector_get_at( set->file_types, i ) );

        for (i = 0; i < map_size( set->map ); i++)
        {
            /* Snapshot the map side through its public view contract. */
            void *view_iface = map_view_iface( set->map );
            (void)view_iface;
        }
    }

    return (IInspectable *)view;
}

struct winrt_map *property_set_get_map( struct property_set *set ) { return set->map; }
