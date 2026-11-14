/* WinRT Windows.Graphics Implementation
 *
 * Copyright 2026 Zhiyi Zhang for CodeWeavers
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

#include "initguid.h"
#include "private.h"

WINE_DEFAULT_DEBUG_CHANNEL(display);

struct display_info_statics
{
    IActivationFactory IActivationFactory_iface;
    IDisplayInformationStatics IDisplayInformationStatics_iface;
    LONG ref;
};

static inline struct display_info_statics *impl_from_IActivationFactory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct display_info_statics, IActivationFactory_iface );
}

static HRESULT WINAPI factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct display_info_statics *impl = impl_from_IActivationFactory( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown )
        || IsEqualGUID( iid, &IID_IInspectable )
        || IsEqualGUID( iid, &IID_IAgileObject )
        || IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = &impl->IActivationFactory_iface;
        IActivationFactory_AddRef( *out );
        return S_OK;
    }
    else if (IsEqualGUID( iid, &IID_IDisplayInformationStatics ))
    {
        *out = &impl->IDisplayInformationStatics_iface;
        IDisplayInformationStatics_AddRef( *out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI factory_AddRef( IActivationFactory *iface )
{
    struct display_info_statics *impl = impl_from_IActivationFactory( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p, ref %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI factory_Release( IActivationFactory *iface )
{
    struct display_info_statics *impl = impl_from_IActivationFactory( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    TRACE( "iface %p, ref %lu.\n", iface, ref );
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

struct display_information
{
    IDisplayInformation IDisplayInformation_iface;
    IDisplayInformation2 IDisplayInformation2_iface;
    IDisplayInformation3 IDisplayInformation3_iface;
    IDisplayInformation4 IDisplayInformation4_iface;
    IDisplayInformation5 IDisplayInformation5_iface;
    LONG ref;
};

static inline struct display_information *impl_from_IDisplayInformation( IDisplayInformation *iface )
{
    return CONTAINING_RECORD( iface, struct display_information, IDisplayInformation_iface );
}
static inline struct display_information *impl_from_IDisplayInformation2( IDisplayInformation2 *iface )
{
    return CONTAINING_RECORD( iface, struct display_information, IDisplayInformation2_iface );
}
static inline struct display_information *impl_from_IDisplayInformation3( IDisplayInformation3 *iface )
{
    return CONTAINING_RECORD( iface, struct display_information, IDisplayInformation3_iface );
}
static inline struct display_information *impl_from_IDisplayInformation4( IDisplayInformation4 *iface )
{
    return CONTAINING_RECORD( iface, struct display_information, IDisplayInformation4_iface );
}
static inline struct display_information *impl_from_IDisplayInformation5( IDisplayInformation5 *iface )
{
    return CONTAINING_RECORD( iface, struct display_information, IDisplayInformation5_iface );
}

static HRESULT WINAPI display_info_QueryInterface( IDisplayInformation *iface, REFIID iid, void **out )
{
    struct display_information *impl = impl_from_IDisplayInformation( iface );
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IDisplayInformation ))
    {
        *out = &impl->IDisplayInformation_iface;
        IDisplayInformation_AddRef( *out );
        return S_OK;
    }
    else if (IsEqualGUID( iid, &IID_IDisplayInformation2 ))
    {
        *out = &impl->IDisplayInformation2_iface;
        IDisplayInformation2_AddRef( *out );
        return S_OK;
    }
    else if (IsEqualGUID( iid, &IID_IDisplayInformation3 ))
    {
        *out = &impl->IDisplayInformation3_iface;
        IDisplayInformation3_AddRef( *out );
        return S_OK;
    }
    else if (IsEqualGUID( iid, &IID_IDisplayInformation4 ))
    {
        *out = &impl->IDisplayInformation4_iface;
        IDisplayInformation4_AddRef( *out );
        return S_OK;
    }
    else if (IsEqualGUID( iid, &IID_IDisplayInformation5 ))
    {
        *out = &impl->IDisplayInformation5_iface;
        IDisplayInformation5_AddRef( *out );
        return S_OK;
    }
    FIXME( "%s not implemented.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI display_info_AddRef( IDisplayInformation *iface )
{
    struct display_information *impl = impl_from_IDisplayInformation( iface );
    return InterlockedIncrement( &impl->ref );
}
static ULONG WINAPI display_info_Release( IDisplayInformation *iface )
{
    struct display_information *impl = impl_from_IDisplayInformation( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref) free( impl );
    return ref;
}
static HRESULT WINAPI display_info_GetIids( IDisplayInformation *iface, ULONG *count, IID **iids )
{
    static const IID iid_array[] =
    {
        IID_IDisplayInformation,
        IID_IDisplayInformation2,
        IID_IDisplayInformation3,
        IID_IDisplayInformation4,
        IID_IDisplayInformation5,
    };
    IID *array = CoTaskMemAlloc( sizeof(iid_array) );
    if (!array) return E_OUTOFMEMORY;
    memcpy( array, iid_array, sizeof(iid_array) );
    *count = ARRAY_SIZE(iid_array);
    *iids = array;
    return S_OK;
}
static HRESULT WINAPI display_info_GetRuntimeClassName( IDisplayInformation *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.Graphics.Display.DisplayInformation", 41, name );
}
static HRESULT WINAPI display_info_GetTrustLevel( IDisplayInformation *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI display_info_get_CurrentOrientation( IDisplayInformation *iface, DisplayOrientations *value )
{
    if (value) *value = DisplayOrientations_Landscape;
    return S_OK;
}
static HRESULT WINAPI display_info_get_NativeOrientation( IDisplayInformation *iface, DisplayOrientations *value )
{
    if (value) *value = DisplayOrientations_Landscape;
    return S_OK;
}
static HRESULT WINAPI display_info_add_OrientationChanged( IDisplayInformation *iface,
        ITypedEventHandler_DisplayInformation_IInspectable *h, EventRegistrationToken *t )
{
    if (t) t->value = 0;
    return S_OK;
}
static HRESULT WINAPI display_info_remove_OrientationChanged( IDisplayInformation *iface, EventRegistrationToken t )
{
    return S_OK;
}
static HRESULT WINAPI display_info_get_ResolutionScale( IDisplayInformation *iface, ResolutionScale *value )
{
    if (value) *value = ResolutionScale_Scale100Percent;
    return S_OK;
}
static HRESULT WINAPI display_info_get_LogicalDpi( IDisplayInformation *iface, FLOAT *value )
{
    if (value) *value = 96.0f;
    return S_OK;
}
static HRESULT WINAPI display_info_get_RawDpiX( IDisplayInformation *iface, FLOAT *value )
{
    if (value) *value = 96.0f;
    return S_OK;
}
static HRESULT WINAPI display_info_get_RawDpiY( IDisplayInformation *iface, FLOAT *value )
{
    if (value) *value = 96.0f;
    return S_OK;
}
static HRESULT WINAPI display_info_add_DpiChanged( IDisplayInformation *iface,
        ITypedEventHandler_DisplayInformation_IInspectable *h, EventRegistrationToken *t )
{
    if (t) t->value = 0;
    return S_OK;
}
static HRESULT WINAPI display_info_remove_DpiChanged( IDisplayInformation *iface, EventRegistrationToken t )
{
    return S_OK;
}
static HRESULT WINAPI display_info_get_StereoEnabled( IDisplayInformation *iface, boolean *value )
{
    if (value) *value = FALSE;
    return S_OK;
}
static HRESULT WINAPI display_info_add_StereoEnabledChanged( IDisplayInformation *iface,
        ITypedEventHandler_DisplayInformation_IInspectable *h, EventRegistrationToken *t )
{
    if (t) t->value = 0;
    return S_OK;
}
static HRESULT WINAPI display_info_remove_StereoEnabledChanged( IDisplayInformation *iface, EventRegistrationToken t )
{
    return S_OK;
}
static HRESULT WINAPI display_info_GetColorProfileAsync( IDisplayInformation *iface,
        IAsyncOperation_IRandomAccessStream **async_info )
{
    if (async_info) *async_info = NULL;
    return E_NOTIMPL;
}
static HRESULT WINAPI display_info_add_ColorProfileChanged( IDisplayInformation *iface,
        ITypedEventHandler_DisplayInformation_IInspectable *h, EventRegistrationToken *t )
{
    if (t) t->value = 0;
    return S_OK;
}
static HRESULT WINAPI display_info_remove_ColorProfileChanged( IDisplayInformation *iface, EventRegistrationToken t )
{
    return S_OK;
}

static const struct IDisplayInformationVtbl display_info_vtbl =
{
    display_info_QueryInterface,
    display_info_AddRef,
    display_info_Release,
    display_info_GetIids,
    display_info_GetRuntimeClassName,
    display_info_GetTrustLevel,
    display_info_get_CurrentOrientation,
    display_info_get_NativeOrientation,
    display_info_add_OrientationChanged,
    display_info_remove_OrientationChanged,
    display_info_get_ResolutionScale,
    display_info_get_LogicalDpi,
    display_info_get_RawDpiX,
    display_info_get_RawDpiY,
    display_info_add_DpiChanged,
    display_info_remove_DpiChanged,
    display_info_get_StereoEnabled,
    display_info_add_StereoEnabledChanged,
    display_info_remove_StereoEnabledChanged,
    display_info_GetColorProfileAsync,
    display_info_add_ColorProfileChanged,
    display_info_remove_ColorProfileChanged,
};

static HRESULT WINAPI display_info2_QueryInterface( IDisplayInformation2 *iface, REFIID iid, void **out )
{
    struct display_information *impl = impl_from_IDisplayInformation2( iface );
    return IDisplayInformation_QueryInterface( &impl->IDisplayInformation_iface, iid, out );
}
static ULONG WINAPI display_info2_AddRef( IDisplayInformation2 *iface )
{
    struct display_information *impl = impl_from_IDisplayInformation2( iface );
    return IDisplayInformation_AddRef( &impl->IDisplayInformation_iface );
}
static ULONG WINAPI display_info2_Release( IDisplayInformation2 *iface )
{
    struct display_information *impl = impl_from_IDisplayInformation2( iface );
    return IDisplayInformation_Release( &impl->IDisplayInformation_iface );
}
static HRESULT WINAPI display_info2_GetIids( IDisplayInformation2 *iface, ULONG *c, IID **i )
{
    struct display_information *impl = impl_from_IDisplayInformation2( iface );
    return display_info_GetIids( &impl->IDisplayInformation_iface, c, i );
}
static HRESULT WINAPI display_info2_GetRuntimeClassName( IDisplayInformation2 *iface, HSTRING *n )
{
    struct display_information *impl = impl_from_IDisplayInformation2( iface );
    return display_info_GetRuntimeClassName( &impl->IDisplayInformation_iface, n );
}
static HRESULT WINAPI display_info2_GetTrustLevel( IDisplayInformation2 *iface, TrustLevel *l )
{
    struct display_information *impl = impl_from_IDisplayInformation2( iface );
    return display_info_GetTrustLevel( &impl->IDisplayInformation_iface, l );
}
static HRESULT WINAPI display_info2_get_RawPixelsPerViewPixel( IDisplayInformation2 *iface, DOUBLE *value )
{
    if (value) *value = 1.0;
    return S_OK;
}

static const struct IDisplayInformation2Vtbl display_info2_vtbl =
{
    display_info2_QueryInterface,
    display_info2_AddRef,
    display_info2_Release,
    display_info2_GetIids,
    display_info2_GetRuntimeClassName,
    display_info2_GetTrustLevel,
    display_info2_get_RawPixelsPerViewPixel,
};

static HRESULT WINAPI display_info3_QueryInterface( IDisplayInformation3 *iface, REFIID iid, void **out )
{
    struct display_information *impl = impl_from_IDisplayInformation3( iface );
    return IDisplayInformation_QueryInterface( &impl->IDisplayInformation_iface, iid, out );
}
static ULONG WINAPI display_info3_AddRef( IDisplayInformation3 *iface )
{
    struct display_information *impl = impl_from_IDisplayInformation3( iface );
    return IDisplayInformation_AddRef( &impl->IDisplayInformation_iface );
}
static ULONG WINAPI display_info3_Release( IDisplayInformation3 *iface )
{
    struct display_information *impl = impl_from_IDisplayInformation3( iface );
    return IDisplayInformation_Release( &impl->IDisplayInformation_iface );
}
static HRESULT WINAPI display_info3_GetIids( IDisplayInformation3 *iface, ULONG *c, IID **i )
{
    struct display_information *impl = impl_from_IDisplayInformation3( iface );
    return display_info_GetIids( &impl->IDisplayInformation_iface, c, i );
}
static HRESULT WINAPI display_info3_GetRuntimeClassName( IDisplayInformation3 *iface, HSTRING *n )
{
    struct display_information *impl = impl_from_IDisplayInformation3( iface );
    return display_info_GetRuntimeClassName( &impl->IDisplayInformation_iface, n );
}
static HRESULT WINAPI display_info3_GetTrustLevel( IDisplayInformation3 *iface, TrustLevel *l )
{
    struct display_information *impl = impl_from_IDisplayInformation3( iface );
    return display_info_GetTrustLevel( &impl->IDisplayInformation_iface, l );
}
static HRESULT WINAPI display_info3_get_DiagonalSizeInInches( IDisplayInformation3 *iface, IReference_DOUBLE **value )
{
    if (value) *value = NULL;
    return S_OK;
}
static const struct IDisplayInformation3Vtbl display_info3_vtbl =
{
    display_info3_QueryInterface,
    display_info3_AddRef,
    display_info3_Release,
    display_info3_GetIids,
    display_info3_GetRuntimeClassName,
    display_info3_GetTrustLevel,
    display_info3_get_DiagonalSizeInInches,
};

static HRESULT WINAPI display_info4_QueryInterface( IDisplayInformation4 *iface, REFIID iid, void **out )
{
    struct display_information *impl = impl_from_IDisplayInformation4( iface );
    return IDisplayInformation_QueryInterface( &impl->IDisplayInformation_iface, iid, out );
}
static ULONG WINAPI display_info4_AddRef( IDisplayInformation4 *iface )
{
    struct display_information *impl = impl_from_IDisplayInformation4( iface );
    return IDisplayInformation_AddRef( &impl->IDisplayInformation_iface );
}
static ULONG WINAPI display_info4_Release( IDisplayInformation4 *iface )
{
    struct display_information *impl = impl_from_IDisplayInformation4( iface );
    return IDisplayInformation_Release( &impl->IDisplayInformation_iface );
}
static HRESULT WINAPI display_info4_GetIids( IDisplayInformation4 *iface, ULONG *c, IID **i )
{
    struct display_information *impl = impl_from_IDisplayInformation4( iface );
    return display_info_GetIids( &impl->IDisplayInformation_iface, c, i );
}
static HRESULT WINAPI display_info4_GetRuntimeClassName( IDisplayInformation4 *iface, HSTRING *n )
{
    struct display_information *impl = impl_from_IDisplayInformation4( iface );
    return display_info_GetRuntimeClassName( &impl->IDisplayInformation_iface, n );
}
static HRESULT WINAPI display_info4_GetTrustLevel( IDisplayInformation4 *iface, TrustLevel *l )
{
    struct display_information *impl = impl_from_IDisplayInformation4( iface );
    return display_info_GetTrustLevel( &impl->IDisplayInformation_iface, l );
}
static HRESULT WINAPI display_info4_get_ScreenWidthInRawPixels( IDisplayInformation4 *iface, UINT32 *value )
{
    if (value) *value = 1920;
    return S_OK;
}
static HRESULT WINAPI display_info4_get_ScreenHeightInRawPixels( IDisplayInformation4 *iface, UINT32 *value )
{
    if (value) *value = 1080;
    return S_OK;
}
static const struct IDisplayInformation4Vtbl display_info4_vtbl =
{
    display_info4_QueryInterface,
    display_info4_AddRef,
    display_info4_Release,
    display_info4_GetIids,
    display_info4_GetRuntimeClassName,
    display_info4_GetTrustLevel,
    display_info4_get_ScreenWidthInRawPixels,
    display_info4_get_ScreenHeightInRawPixels,
};

static HRESULT WINAPI display_info5_QueryInterface( IDisplayInformation5 *iface, REFIID iid, void **out )
{
    struct display_information *impl = impl_from_IDisplayInformation5( iface );
    return IDisplayInformation_QueryInterface( &impl->IDisplayInformation_iface, iid, out );
}
static ULONG WINAPI display_info5_AddRef( IDisplayInformation5 *iface )
{
    struct display_information *impl = impl_from_IDisplayInformation5( iface );
    return IDisplayInformation_AddRef( &impl->IDisplayInformation_iface );
}
static ULONG WINAPI display_info5_Release( IDisplayInformation5 *iface )
{
    struct display_information *impl = impl_from_IDisplayInformation5( iface );
    return IDisplayInformation_Release( &impl->IDisplayInformation_iface );
}
static HRESULT WINAPI display_info5_GetIids( IDisplayInformation5 *iface, ULONG *c, IID **i )
{
    struct display_information *impl = impl_from_IDisplayInformation5( iface );
    return display_info_GetIids( &impl->IDisplayInformation_iface, c, i );
}
static HRESULT WINAPI display_info5_GetRuntimeClassName( IDisplayInformation5 *iface, HSTRING *n )
{
    struct display_information *impl = impl_from_IDisplayInformation5( iface );
    return display_info_GetRuntimeClassName( &impl->IDisplayInformation_iface, n );
}
static HRESULT WINAPI display_info5_GetTrustLevel( IDisplayInformation5 *iface, TrustLevel *l )
{
    struct display_information *impl = impl_from_IDisplayInformation5( iface );
    return display_info_GetTrustLevel( &impl->IDisplayInformation_iface, l );
}
static HRESULT WINAPI display_info5_GetAdvancedColorInfo( IDisplayInformation5 *iface, IAdvancedColorInfo **value )
{
    if (value) *value = NULL;
    return S_OK;
}
static HRESULT WINAPI display_info5_add_AdvancedColorInfoChanged( IDisplayInformation5 *iface,
        __FITypedEventHandler_2_Windows__CGraphics__CDisplay__CDisplayInformation_IInspectable *h, EventRegistrationToken *t )
{
    if (t) t->value = 0;
    return S_OK;
}
static HRESULT WINAPI display_info5_remove_AdvancedColorInfoChanged( IDisplayInformation5 *iface, EventRegistrationToken t )
{
    return S_OK;
}
static const struct IDisplayInformation5Vtbl display_info5_vtbl =
{
    display_info5_QueryInterface,
    display_info5_AddRef,
    display_info5_Release,
    display_info5_GetIids,
    display_info5_GetRuntimeClassName,
    display_info5_GetTrustLevel,
    display_info5_GetAdvancedColorInfo,
    display_info5_add_AdvancedColorInfoChanged,
    display_info5_remove_AdvancedColorInfoChanged,
};

DEFINE_IINSPECTABLE( display_info_statics, IDisplayInformationStatics, struct display_info_statics,
                     IActivationFactory_iface )

static HRESULT WINAPI display_info_statics_GetForCurrentView( IDisplayInformationStatics *iface,
        IDisplayInformation **current )
{
    struct display_information *impl;
    TRACE( "iface %p, current %p.\n", iface, current );
    if (!current) return E_POINTER;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->IDisplayInformation_iface.lpVtbl = &display_info_vtbl;
    impl->IDisplayInformation2_iface.lpVtbl = &display_info2_vtbl;
    impl->IDisplayInformation3_iface.lpVtbl = &display_info3_vtbl;
    impl->IDisplayInformation4_iface.lpVtbl = &display_info4_vtbl;
    impl->IDisplayInformation5_iface.lpVtbl = &display_info5_vtbl;
    impl->ref = 1;
    *current = &impl->IDisplayInformation_iface;
    return S_OK;
}

static HRESULT WINAPI display_info_statics_get_AutoRotationPreferences( IDisplayInformationStatics *iface,
        DisplayOrientations *value )
{
    FIXME( "iface %p, value %p stub!\n", iface, value );
    return E_NOTIMPL;
}

static HRESULT WINAPI display_info_statics_put_AutoRotationPreferences( IDisplayInformationStatics *iface,
        DisplayOrientations value )
{
    FIXME( "iface %p, value %#x stub!\n", iface, value );
    return E_NOTIMPL;
}

static HRESULT WINAPI display_info_statics_add_DisplayContentsInvalidated( IDisplayInformationStatics *iface,
        ITypedEventHandler_DisplayInformation_IInspectable *handler, EventRegistrationToken *token )
{
    FIXME( "iface %p, handler %p, token %p stub!\n", iface, handler, token );
    return E_NOTIMPL;
}

static HRESULT WINAPI display_info_statics_remove_DisplayContentsInvalidated( IDisplayInformationStatics *iface,
        EventRegistrationToken token )
{
    FIXME( "iface %p, token %I64x stub!\n", iface, token.value );
    return E_NOTIMPL;
}

static const struct IDisplayInformationStaticsVtbl display_info_statics_vtbl =
{
    display_info_statics_QueryInterface,
    display_info_statics_AddRef,
    display_info_statics_Release,
    /* IInspectable methods */
    display_info_statics_GetIids,
    display_info_statics_GetRuntimeClassName,
    display_info_statics_GetTrustLevel,
    /* IDisplayInformationStatics methods */
    display_info_statics_GetForCurrentView,
    display_info_statics_get_AutoRotationPreferences,
    display_info_statics_put_AutoRotationPreferences,
    display_info_statics_add_DisplayContentsInvalidated,
    display_info_statics_remove_DisplayContentsInvalidated
};

static struct display_info_statics display_info_statics =
{
    {&factory_vtbl},
    {&display_info_statics_vtbl},
    1,
};

static IActivationFactory *display_info_factory = &display_info_statics.IActivationFactory_iface;

HRESULT WINAPI DllGetClassObject( REFCLSID clsid, REFIID riid, void **out )
{
    FIXME( "clsid %s, riid %s, out %p stub!\n", debugstr_guid( clsid ), debugstr_guid( riid ), out );
    return CLASS_E_CLASSNOTAVAILABLE;
}

HRESULT WINAPI DllGetActivationFactory( HSTRING classid, IActivationFactory **factory )
{
    const WCHAR *name = WindowsGetStringRawBuffer( classid, NULL );

    TRACE( "classid %s, factory %p.\n", debugstr_hstring( classid ), factory );

    *factory = NULL;

    if (!wcscmp( name, RuntimeClass_Windows_Graphics_Display_DisplayInformation ))
        IActivationFactory_QueryInterface( display_info_factory, &IID_IActivationFactory, (void **)factory );

    return *factory ? S_OK : E_NOINTERFACE;
}
