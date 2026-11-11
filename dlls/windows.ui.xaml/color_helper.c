/* WinRT Windows.UI.ColorHelper Implementation
 *
 * Copyright (C) 2025 Mohamad Al-Jaf
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

WINE_DEFAULT_DEBUG_CHANNEL(xaml);

struct color_helper_statics
{
    IActivationFactory IActivationFactory_iface;
    IColorHelperStatics IColorHelperStatics_iface;
    LONG ref;
};

static inline struct color_helper_statics *impl_from_IActivationFactory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct color_helper_statics, IActivationFactory_iface );
}

static HRESULT WINAPI factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct color_helper_statics *impl = impl_from_IActivationFactory( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = &impl->IActivationFactory_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IColorHelperStatics ))
    {
        *out = &impl->IColorHelperStatics_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI factory_AddRef( IActivationFactory *iface )
{
    struct color_helper_statics *impl = impl_from_IActivationFactory( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p increasing refcount to %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI factory_Release( IActivationFactory *iface )
{
    struct color_helper_statics *impl = impl_from_IActivationFactory( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    TRACE( "iface %p decreasing refcount to %lu.\n", iface, ref );
    return ref;
}

static HRESULT WINAPI factory_GetIids( IActivationFactory *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *class_name )
{
    FIXME( "iface %p, class_name %p stub!\n", iface, class_name );
    return E_NOTIMPL;
}

static HRESULT WINAPI factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    FIXME( "iface %p, instance %p stub!\n", iface, instance );
    return E_NOTIMPL;
}

static const struct IActivationFactoryVtbl factory_vtbl =
{
    factory_QueryInterface,
    factory_AddRef,
    factory_Release,
    /* IInspectable methods */
    factory_GetIids,
    factory_GetRuntimeClassName,
    factory_GetTrustLevel,
    /* IActivationFactory methods */
    factory_ActivateInstance,
};

DEFINE_IINSPECTABLE( color_helper_statics, IColorHelperStatics, struct color_helper_statics, IActivationFactory_iface )

static HRESULT WINAPI color_helper_statics_FromArgb( IColorHelperStatics *iface, BYTE a, BYTE r, BYTE g, BYTE b, Color *value )
{
    Color color;

    TRACE( "iface %p, a %u, r %u, g %u, b %u, value %p\n", iface, a, r, g, b, value );

    if (!value) return E_POINTER;

    color.A = a;
    color.R = r;
    color.G = g;
    color.B = b;

    *value = color;
    return S_OK;
}

static const struct IColorHelperStaticsVtbl color_helper_statics_vtbl =
{
    color_helper_statics_QueryInterface,
    color_helper_statics_AddRef,
    color_helper_statics_Release,
    /* IInspectable methods */
    color_helper_statics_GetIids,
    color_helper_statics_GetRuntimeClassName,
    color_helper_statics_GetTrustLevel,
    /* IColorHelperStatics methods */
    color_helper_statics_FromArgb,
};

static struct color_helper_statics color_helper_statics =
{
    {&factory_vtbl},
    {&color_helper_statics_vtbl},
    1,
};

IActivationFactory *color_helper_factory = &color_helper_statics.IActivationFactory_iface;

/* Windows.UI.Xaml.SizeHelper */
static const GUID IID_ISizeHelperStatics =
    { 0x6286c5b2, 0xcf78, 0x4915, { 0xaa, 0x40, 0x76, 0x00, 0x4a, 0x16, 0x5f, 0x5e } };

typedef struct ISizeHelperStatics ISizeHelperStatics;

typedef struct ISizeHelperStaticsVtbl
{
    HRESULT (WINAPI *QueryInterface)( ISizeHelperStatics *iface, REFIID iid, void **out );
    ULONG (WINAPI *AddRef)( ISizeHelperStatics *iface );
    ULONG (WINAPI *Release)( ISizeHelperStatics *iface );
    HRESULT (WINAPI *GetIids)( ISizeHelperStatics *iface, ULONG *count, IID **iids );
    HRESULT (WINAPI *GetRuntimeClassName)( ISizeHelperStatics *iface, HSTRING *name );
    HRESULT (WINAPI *GetTrustLevel)( ISizeHelperStatics *iface, TrustLevel *level );
    HRESULT (WINAPI *get_Empty)( ISizeHelperStatics *iface, Size *value );
    HRESULT (WINAPI *FromDimensions)( ISizeHelperStatics *iface, FLOAT width, FLOAT height, Size *value );
    HRESULT (WINAPI *GetIsEmpty)( ISizeHelperStatics *iface, Size target, boolean *value );
    HRESULT (WINAPI *Equals)( ISizeHelperStatics *iface, Size value1, Size value2, boolean *value );
} ISizeHelperStaticsVtbl;

struct ISizeHelperStatics
{
    const struct ISizeHelperStaticsVtbl *lpVtbl;
};

struct size_helper_statics
{
    IActivationFactory IActivationFactory_iface;
    ISizeHelperStatics ISizeHelperStatics_iface;
    LONG ref;
};

static inline struct size_helper_statics *impl_from_size_IActivationFactory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct size_helper_statics, IActivationFactory_iface );
}

static HRESULT WINAPI size_factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct size_helper_statics *impl = impl_from_size_IActivationFactory( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = &impl->IActivationFactory_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_ISizeHelperStatics ))
    {
        *out = &impl->ISizeHelperStatics_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI size_factory_AddRef( IActivationFactory *iface )
{
    struct size_helper_statics *impl = impl_from_size_IActivationFactory( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI size_factory_Release( IActivationFactory *iface )
{
    struct size_helper_statics *impl = impl_from_size_IActivationFactory( iface );
    return InterlockedDecrement( &impl->ref );
}

static HRESULT WINAPI size_factory_GetIids( IActivationFactory *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI size_factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.UI.Xaml.SizeHelper", 25, name );
}

static HRESULT WINAPI size_factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI size_factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    return E_NOTIMPL;
}

static const struct IActivationFactoryVtbl size_factory_vtbl =
{
    size_factory_QueryInterface,
    size_factory_AddRef,
    size_factory_Release,
    size_factory_GetIids,
    size_factory_GetRuntimeClassName,
    size_factory_GetTrustLevel,
    size_factory_ActivateInstance,
};

static inline struct size_helper_statics *impl_from_ISizeHelperStatics( ISizeHelperStatics *iface )
{
    return CONTAINING_RECORD( iface, struct size_helper_statics, ISizeHelperStatics_iface );
}

static HRESULT WINAPI size_helper_QueryInterface( ISizeHelperStatics *iface, REFIID iid, void **out )
{
    struct size_helper_statics *impl = impl_from_ISizeHelperStatics( iface );
    return IActivationFactory_QueryInterface( &impl->IActivationFactory_iface, iid, out );
}

static ULONG WINAPI size_helper_AddRef( ISizeHelperStatics *iface )
{
    struct size_helper_statics *impl = impl_from_ISizeHelperStatics( iface );
    return IActivationFactory_AddRef( &impl->IActivationFactory_iface );
}

static ULONG WINAPI size_helper_Release( ISizeHelperStatics *iface )
{
    struct size_helper_statics *impl = impl_from_ISizeHelperStatics( iface );
    return IActivationFactory_Release( &impl->IActivationFactory_iface );
}

static HRESULT WINAPI size_helper_GetIids( ISizeHelperStatics *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI size_helper_GetRuntimeClassName( ISizeHelperStatics *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.UI.Xaml.SizeHelper", 25, name );
}

static HRESULT WINAPI size_helper_GetTrustLevel( ISizeHelperStatics *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI size_helper_get_Empty( ISizeHelperStatics *iface, Size *value )
{
    if (!value) return E_POINTER;
    value->Width = 0.0f;
    value->Height = 0.0f;
    return S_OK;
}

static HRESULT WINAPI size_helper_FromDimensions( ISizeHelperStatics *iface, FLOAT width, FLOAT height, Size *value )
{
    TRACE( "iface %p, width %f, height %f, value %p\n", iface, width, height, value );
    if (!value) return E_POINTER;
    value->Width = width;
    value->Height = height;
    return S_OK;
}

static HRESULT WINAPI size_helper_GetIsEmpty( ISizeHelperStatics *iface, Size target, boolean *value )
{
    if (!value) return E_POINTER;
    *value = (target.Width == 0.0f && target.Height == 0.0f);
    return S_OK;
}

static HRESULT WINAPI size_helper_Equals( ISizeHelperStatics *iface, Size value1, Size value2, boolean *value )
{
    if (!value) return E_POINTER;
    *value = (value1.Width == value2.Width && value1.Height == value2.Height);
    return S_OK;
}

static const struct ISizeHelperStaticsVtbl size_helper_statics_vtbl =
{
    size_helper_QueryInterface,
    size_helper_AddRef,
    size_helper_Release,
    size_helper_GetIids,
    size_helper_GetRuntimeClassName,
    size_helper_GetTrustLevel,
    size_helper_get_Empty,
    size_helper_FromDimensions,
    size_helper_GetIsEmpty,
    size_helper_Equals,
};

static struct size_helper_statics size_helper_statics =
{
    {&size_factory_vtbl},
    {&size_helper_statics_vtbl},
    1,
};

IActivationFactory *size_helper_factory = &size_helper_statics.IActivationFactory_iface;
