/* WinRT StandardDataFormats statics
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
 * StandardDataFormats hands out the canonical format-name strings.  The
 * values below are the format identifiers the Windows shell and the XAML
 * drag/drop stack exchange (the projection keys DataPackage.Contains and
 * GetAvailableFormats off exactly these strings); every getter returns the
 * canonical HSTRING, and the DataPackage format table keys the same names.
 * The names without a statics property (Jpeg, Gif, Png, Json, XamlDocument,
 * Custom) travel through SetData/Contains/GetAvailableFormats as format keys;
 * the table keeps their canonical spelling in one place.
 */

const WCHAR format_text[] = L"Text";
const WCHAR format_html[] = L"Html";
const WCHAR format_uri[] = L"Uri";
const WCHAR format_rtf[] = L"Rtf";
const WCHAR format_bitmap[] = L"Bitmap";
const WCHAR format_storage_items[] = L"StorageItems";
const WCHAR format_web_link[] = L"WebLink";
const WCHAR format_application_link[] = L"ApplicationLink";
const WCHAR format_user_activity_json[] = L"UserActivityJsonArray";
const WCHAR format_jpeg[] = L"Jpeg";
const WCHAR format_gif[] = L"Gif";
const WCHAR format_png[] = L"Png";
const WCHAR format_json[] = L"Json";
const WCHAR format_xaml_document[] = L"XamlDocument";
const WCHAR format_custom[] = L"Custom";

struct standard_formats_statics
{
    const void *factory_vtbl;   /* IActivationFactory at offset 0 */
    const void *statics_vtbl;   /* IStandardDataFormatsStatics at offset 1 pointer */
    const void *statics2_vtbl;  /* IStandardDataFormatsStatics2 at offset 2 pointers */
    const void *statics3_vtbl;  /* IStandardDataFormatsStatics3 at offset 3 pointers */
    LONG ref;
};

static const void *standard_formats_factory_vtbl[];
static const void *standard_formats_statics_vtbl[];
static const void *standard_formats_statics2_vtbl[];
static const void *standard_formats_statics3_vtbl[];

/* Every interface group of the object shares the IUnknown/entry-point
 * implementations; resolve the object from the group's vtable pointer so one
 * set of method bodies serves every group at its own field offset. */
static struct standard_formats_statics *impl_from_any( const void *iface )
{
    static const void *groups[] = { standard_formats_factory_vtbl, standard_formats_statics_vtbl, standard_formats_statics2_vtbl, standard_formats_statics3_vtbl };
    static const int offsets[] = { 0, 1, 2, 3 };
    const void *vtbl = *(const void *const *)iface;
    int i;

    for (i = 0; i < (int)ARRAY_SIZE( groups ); i++)
        if (vtbl == groups[i])
            return (struct standard_formats_statics *)((const BYTE *)iface - offsets[i] * sizeof(void *));
    return (struct standard_formats_statics *)iface;
}

static const WCHAR standard_formats_class[] = L"Windows.ApplicationModel.DataTransfer.StandardDataFormats";

static HRESULT WINAPI formats_QueryInterface( const void *iface, REFIID iid, void **out )
{
    struct standard_formats_statics *impl = impl_from_any( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (!out) return E_POINTER;
    *out = NULL;

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = (void *)impl;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IStandardDataFormatsStatics ))
    {
        *out = (void *)&impl->statics_vtbl;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IStandardDataFormatsStatics2 ))
    {
        *out = (void *)&impl->statics2_vtbl;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IStandardDataFormatsStatics3 ))
    {
        *out = (void *)&impl->statics3_vtbl;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG WINAPI formats_AddRef( const void *iface )
{
    struct standard_formats_statics *impl = impl_from_any( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI formats_Release( const void *iface )
{
    struct standard_formats_statics *impl = impl_from_any( iface );
    return InterlockedDecrement( &impl->ref );
}

static HRESULT WINAPI formats_GetIids( const void *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI formats_GetRuntimeClassName( const void *iface, HSTRING *class_name )
{
    if (!class_name) return E_POINTER;
    return WindowsCreateString( standard_formats_class, lstrlenW( standard_formats_class ), class_name );
}

static HRESULT WINAPI formats_GetTrustLevel( const void *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI formats_ActivateInstance( const void *iface, void **instance )
{
    TRACE( "iface %p, instance %p.\n", iface, instance );
    return E_FAIL;
}

static const void *standard_formats_factory_vtbl[] =
{
    formats_QueryInterface,
    formats_AddRef,
    formats_Release,
    formats_GetIids,
    formats_GetRuntimeClassName,
    formats_GetTrustLevel,
    formats_ActivateInstance,
};

static HRESULT WINAPI fstatics_QueryInterface( const void *iface, REFIID iid, void **out )
{
    return formats_QueryInterface( (void *)impl_from_any( iface ), iid, out );
}

static ULONG WINAPI fstatics_AddRef( const void *iface )
{
    return formats_AddRef( (void *)impl_from_any( iface ) );
}

static ULONG WINAPI fstatics_Release( const void *iface )
{
    return formats_Release( (void *)impl_from_any( iface ) );
}

static HRESULT WINAPI fstatics2_QueryInterface( const void *iface, REFIID iid, void **out )
{
    return formats_QueryInterface( (void *)impl_from_any( iface ), iid, out );
}

static ULONG WINAPI fstatics2_AddRef( const void *iface )
{
    return formats_AddRef( (void *)impl_from_any( iface ) );
}

static ULONG WINAPI fstatics2_Release( const void *iface )
{
    return formats_Release( (void *)impl_from_any( iface ) );
}

static HRESULT WINAPI fstatics3_QueryInterface( const void *iface, REFIID iid, void **out )
{
    return formats_QueryInterface( (void *)impl_from_any( iface ), iid, out );
}

static ULONG WINAPI fstatics3_AddRef( const void *iface )
{
    return formats_AddRef( (void *)impl_from_any( iface ) );
}

static ULONG WINAPI fstatics3_Release( const void *iface )
{
    return formats_Release( (void *)impl_from_any( iface ) );
}

static HRESULT format_getter( const WCHAR *value, HSTRING *out )
{
    if (!out) return E_POINTER;
    return WindowsCreateString( value, lstrlenW( value ), out );
}

static HRESULT WINAPI statics_get_Text( const void *iface, HSTRING *value )
{
    TRACE( "iface %p, value %p.\n", iface, value );
    return format_getter( format_text, value );
}

static HRESULT WINAPI statics_get_Uri( const void *iface, HSTRING *value )
{
    TRACE( "iface %p, value %p.\n", iface, value );
    return format_getter( format_uri, value );
}

static HRESULT WINAPI statics_get_Html( const void *iface, HSTRING *value )
{
    TRACE( "iface %p, value %p.\n", iface, value );
    return format_getter( format_html, value );
}

static HRESULT WINAPI statics_get_Rtf( const void *iface, HSTRING *value )
{
    TRACE( "iface %p, value %p.\n", iface, value );
    return format_getter( format_rtf, value );
}

static HRESULT WINAPI statics_get_Bitmap( const void *iface, HSTRING *value )
{
    TRACE( "iface %p, value %p.\n", iface, value );
    return format_getter( format_bitmap, value );
}

static HRESULT WINAPI statics_get_StorageItems( const void *iface, HSTRING *value )
{
    TRACE( "iface %p, value %p.\n", iface, value );
    return format_getter( format_storage_items, value );
}

static const void *standard_formats_statics_vtbl[] =
{
    fstatics_QueryInterface,
    fstatics_AddRef,
    fstatics_Release,
    formats_GetIids,
    formats_GetRuntimeClassName,
    formats_GetTrustLevel,
    statics_get_Text,
    statics_get_Uri,
    statics_get_Html,
    statics_get_Rtf,
    statics_get_Bitmap,
    statics_get_StorageItems,
};

static HRESULT WINAPI statics2_get_WebLink( const void *iface, HSTRING *value )
{
    TRACE( "iface %p, value %p.\n", iface, value );
    return format_getter( format_web_link, value );
}

static HRESULT WINAPI statics2_get_ApplicationLink( const void *iface, HSTRING *value )
{
    TRACE( "iface %p, value %p.\n", iface, value );
    return format_getter( format_application_link, value );
}

static const void *standard_formats_statics2_vtbl[] =
{
    fstatics2_QueryInterface,
    fstatics2_AddRef,
    fstatics2_Release,
    formats_GetIids,
    formats_GetRuntimeClassName,
    formats_GetTrustLevel,
    statics2_get_WebLink,
    statics2_get_ApplicationLink,
};

static HRESULT WINAPI statics3_get_UserActivityJsonArray( const void *iface, HSTRING *value )
{
    TRACE( "iface %p, value %p.\n", iface, value );
    return format_getter( format_user_activity_json, value );
}

static const void *standard_formats_statics3_vtbl[] =
{
    fstatics3_QueryInterface,
    fstatics3_AddRef,
    fstatics3_Release,
    formats_GetIids,
    formats_GetRuntimeClassName,
    formats_GetTrustLevel,
    statics3_get_UserActivityJsonArray,
};

static struct standard_formats_statics standard_formats_statics =
{
    standard_formats_factory_vtbl,
    standard_formats_statics_vtbl,
    standard_formats_statics2_vtbl,
    standard_formats_statics3_vtbl,
    1,
};

IActivationFactory *standard_data_formats_factory = (IActivationFactory *)&standard_formats_statics;
