/* WinRT Windows.System.Profile.SystemManufacturers Implementation
 *
 * Copyright (C) 2022 Mohamad Al-Jaf
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

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(smbios);

static HRESULT create_hstring( const WCHAR *str, HSTRING *value )
{
    return WindowsCreateString( str, wcslen( str ), value );
}

struct smbios_statics
{
    IActivationFactory IActivationFactory_iface;
    ISmbiosInformationStatics ISmbiosInformationStatics_iface;
    LONG ref;
};

static inline struct smbios_statics *impl_from_IActivationFactory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct smbios_statics, IActivationFactory_iface );
}

static HRESULT WINAPI factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct smbios_statics *impl = impl_from_IActivationFactory( iface );

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

    if (IsEqualGUID( iid, &IID_ISmbiosInformationStatics ))
    {
        *out = &impl->ISmbiosInformationStatics_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI factory_AddRef( IActivationFactory *iface )
{
    struct smbios_statics *impl = impl_from_IActivationFactory( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p, ref %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI factory_Release( IActivationFactory *iface )
{
    struct smbios_statics *impl = impl_from_IActivationFactory( iface );
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

DEFINE_IINSPECTABLE( statics, ISmbiosInformationStatics, struct smbios_statics, IActivationFactory_iface )

static HRESULT get_bios_serialnumber( BSTR *value )
{
    const WCHAR *class = L"Win32_BIOS";
    IEnumWbemClassObject *wbem_enum;
    IWbemClassObject *wbem_class;
    IWbemServices *wbem_service;
    IWbemLocator *wbem_locator;
    VARIANT serial;
    ULONG count;
    HRESULT hr;
    BSTR bstr;

    hr = CoCreateInstance( &CLSID_WbemLocator, NULL, CLSCTX_INPROC_SERVER, &IID_IWbemLocator, (void**)&wbem_locator );
    if (FAILED(hr)) return hr;

    bstr = SysAllocString( L"ROOT\\CIMV2" );
    if (!bstr)
    {
        IWbemLocator_Release( wbem_locator );
        return E_OUTOFMEMORY;
    }
    hr = IWbemLocator_ConnectServer( wbem_locator, bstr, NULL, NULL, NULL, 0, NULL, NULL, &wbem_service );
    IWbemLocator_Release( wbem_locator );
    SysFreeString( bstr );
    if (FAILED(hr)) return hr;

    bstr = SysAllocString( class );
    if (!bstr)
    {
        IWbemServices_Release( wbem_service );
        return E_OUTOFMEMORY;
    }
    hr = IWbemServices_CreateInstanceEnum( wbem_service, bstr, WBEM_FLAG_SYSTEM_ONLY, NULL, &wbem_enum );
    IWbemServices_Release( wbem_service );
    SysFreeString( bstr );
    if (FAILED(hr)) return hr;

    hr = IEnumWbemClassObject_Next( wbem_enum, 1000, 1, &wbem_class, &count );
    IEnumWbemClassObject_Release( wbem_enum );
    if (FAILED(hr)) return hr;

    hr = IWbemClassObject_Get( wbem_class, L"SerialNumber", 0, &serial, NULL, NULL );
    IWbemClassObject_Release( wbem_class );
    if (FAILED(hr)) return hr;

    *value = V_BSTR( &serial );
    VariantClear( &serial );
    return hr;
}

static HRESULT WINAPI statics_get_SerialNumber( ISmbiosInformationStatics *iface, HSTRING *value )
{
    BSTR serial;
    HRESULT hr;

    TRACE( "iface %p, value %p.\n", iface, value );

    if (FAILED( hr = get_bios_serialnumber( &serial ) )) return hr;
    if (FAILED( hr = WindowsCreateString( serial, wcslen(serial), value ) )) return hr;

    TRACE( "Returning serial number: %s.\n", debugstr_w( serial ) );
    return hr;
}

static const struct ISmbiosInformationStaticsVtbl statics_vtbl =
{
    statics_QueryInterface,
    statics_AddRef,
    statics_Release,
    /* IInspectable methods */
    statics_GetIids,
    statics_GetRuntimeClassName,
    statics_GetTrustLevel,
    /* ISmbiosInformationStatics methods */
    statics_get_SerialNumber,
};

static struct smbios_statics smbios_statics =
{
    {&factory_vtbl},
    {&statics_vtbl},
    1,
};

static IActivationFactory *smbios_factory = &smbios_statics.IActivationFactory_iface;

struct oem_support_info
{
    IOemSupportInfo IOemSupportInfo_iface;
    LONG ref;
};

static inline struct oem_support_info *impl_from_IOemSupportInfo( IOemSupportInfo *iface )
{
    return CONTAINING_RECORD( iface, struct oem_support_info, IOemSupportInfo_iface );
}

static HRESULT activate_uri( const WCHAR *raw, IUriRuntimeClass **uri )
{
    static const WCHAR class_name[] = L"Windows.Foundation.Uri";
    IUriRuntimeClassFactory *factory;
    IActivationFactory *activation;
    HSTRING hstr;
    HRESULT hr;

    *uri = NULL;
    if (FAILED( hr = WindowsCreateString( class_name, ARRAYSIZE(class_name) - 1, &hstr ) )) return hr;
    hr = RoGetActivationFactory( hstr, &IID_IActivationFactory, (void **)&activation );
    WindowsDeleteString( hstr );
    if (FAILED(hr)) return hr;

    hr = IActivationFactory_QueryInterface( activation, &IID_IUriRuntimeClassFactory, (void **)&factory );
    IActivationFactory_Release( activation );
    if (FAILED(hr)) return hr;

    if (FAILED( hr = WindowsCreateString( raw, wcslen( raw ), &hstr ) ))
    {
        IUriRuntimeClassFactory_Release( factory );
        return hr;
    }
    hr = IUriRuntimeClassFactory_CreateUri( factory, hstr, uri );
    WindowsDeleteString( hstr );
    IUriRuntimeClassFactory_Release( factory );
    return hr;
}

static HRESULT WINAPI oem_QueryInterface( IOemSupportInfo *iface, REFIID iid, void **out )
{
    struct oem_support_info *impl = impl_from_IOemSupportInfo( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IOemSupportInfo ))
    {
        *out = &impl->IOemSupportInfo_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI oem_AddRef( IOemSupportInfo *iface )
{
    struct oem_support_info *impl = impl_from_IOemSupportInfo( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p, ref %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI oem_Release( IOemSupportInfo *iface )
{
    struct oem_support_info *impl = impl_from_IOemSupportInfo( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    TRACE( "iface %p, ref %lu.\n", iface, ref );
    return ref;
}

static HRESULT WINAPI oem_GetIids( IOemSupportInfo *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI oem_GetRuntimeClassName( IOemSupportInfo *iface, HSTRING *class_name )
{
    return create_hstring( RuntimeClass_Windows_System_Profile_SystemManufacturers_OemSupportInfo, class_name );
}

static HRESULT WINAPI oem_GetTrustLevel( IOemSupportInfo *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI oem_get_SupportLink( IOemSupportInfo *iface, IUriRuntimeClass **value )
{
    FIXME( "iface %p, value %p.\n", iface, value );
    return activate_uri( L"https://www.example.com/support", value );
}

static HRESULT WINAPI oem_get_SupportAppLink( IOemSupportInfo *iface, IUriRuntimeClass **value )
{
    FIXME( "iface %p, value %p.\n", iface, value );
    return activate_uri( L"ms-windows-store://pdp/?productid=9WZDNCRD4QMR", value );
}

static HRESULT WINAPI oem_get_SupportProvider( IOemSupportInfo *iface, HSTRING *value )
{
    static const WCHAR provider[] = L"wine";

    TRACE( "iface %p, value %p.\n", iface, value );

    return create_hstring( provider, value );
}

static const struct IOemSupportInfoVtbl oem_vtbl =
{
    oem_QueryInterface,
    oem_AddRef,
    oem_Release,
    /* IInspectable methods */
    oem_GetIids,
    oem_GetRuntimeClassName,
    oem_GetTrustLevel,
    /* IOemSupportInfo methods */
    oem_get_SupportLink,
    oem_get_SupportAppLink,
    oem_get_SupportProvider,
};

static struct oem_support_info oem_support_info =
{
    {&oem_vtbl},
    1,
};

struct device_info
{
    ISystemSupportDeviceInfo ISystemSupportDeviceInfo_iface;
    LONG ref;
};

static inline struct device_info *impl_from_ISystemSupportDeviceInfo( ISystemSupportDeviceInfo *iface )
{
    return CONTAINING_RECORD( iface, struct device_info, ISystemSupportDeviceInfo_iface );
}

static HRESULT WINAPI device_QueryInterface( ISystemSupportDeviceInfo *iface, REFIID iid, void **out )
{
    struct device_info *impl = impl_from_ISystemSupportDeviceInfo( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_ISystemSupportDeviceInfo ))
    {
        *out = &impl->ISystemSupportDeviceInfo_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI device_AddRef( ISystemSupportDeviceInfo *iface )
{
    struct device_info *impl = impl_from_ISystemSupportDeviceInfo( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p, ref %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI device_Release( ISystemSupportDeviceInfo *iface )
{
    struct device_info *impl = impl_from_ISystemSupportDeviceInfo( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    TRACE( "iface %p, ref %lu.\n", iface, ref );
    return ref;
}

static HRESULT WINAPI device_GetIids( ISystemSupportDeviceInfo *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI device_GetRuntimeClassName( ISystemSupportDeviceInfo *iface, HSTRING *class_name )
{
    return create_hstring( RuntimeClass_Windows_System_Profile_SystemManufacturers_SystemSupportDeviceInfo, class_name );
}

static HRESULT WINAPI device_GetTrustLevel( ISystemSupportDeviceInfo *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI device_get_OperatingSystem( ISystemSupportDeviceInfo *iface, HSTRING *value )
{
    static const WCHAR str[] = L"Windows 11 Pro";

    TRACE( "iface %p, value %p.\n", iface, value );

    return create_hstring( str, value );
}

static HRESULT WINAPI device_get_FriendlyName( ISystemSupportDeviceInfo *iface, HSTRING *value )
{
    static const WCHAR str[] = L"wine";

    TRACE( "iface %p, value %p.\n", iface, value );

    return create_hstring( str, value );
}

static HRESULT WINAPI device_get_SystemManufacturer( ISystemSupportDeviceInfo *iface, HSTRING *value )
{
    static const WCHAR str[] = L"wine";

    TRACE( "iface %p, value %p.\n", iface, value );

    return create_hstring( str, value );
}

static HRESULT WINAPI device_get_SystemProductName( ISystemSupportDeviceInfo *iface, HSTRING *value )
{
    static const WCHAR str[] = L"wine";

    TRACE( "iface %p, value %p.\n", iface, value );

    return create_hstring( str, value );
}

static HRESULT WINAPI device_get_SystemSku( ISystemSupportDeviceInfo *iface, HSTRING *value )
{
    static const WCHAR str[] = L"wine";

    TRACE( "iface %p, value %p.\n", iface, value );

    return create_hstring( str, value );
}

static HRESULT WINAPI device_get_SystemHardwareVersion( ISystemSupportDeviceInfo *iface, HSTRING *value )
{
    static const WCHAR str[] = L"wine";

    TRACE( "iface %p, value %p.\n", iface, value );

    return create_hstring( str, value );
}

static HRESULT WINAPI device_get_SystemFirmwareVersion( ISystemSupportDeviceInfo *iface, HSTRING *value )
{
    static const WCHAR str[] = L"wine";

    TRACE( "iface %p, value %p.\n", iface, value );

    return create_hstring( str, value );
}

static const struct ISystemSupportDeviceInfoVtbl device_vtbl =
{
    device_QueryInterface,
    device_AddRef,
    device_Release,
    /* IInspectable methods */
    device_GetIids,
    device_GetRuntimeClassName,
    device_GetTrustLevel,
    /* ISystemSupportDeviceInfo methods */
    device_get_OperatingSystem,
    device_get_FriendlyName,
    device_get_SystemManufacturer,
    device_get_SystemProductName,
    device_get_SystemSku,
    device_get_SystemHardwareVersion,
    device_get_SystemFirmwareVersion,
};

static struct device_info device_info =
{
    {&device_vtbl},
    1,
};

struct support_statics
{
    IActivationFactory IActivationFactory_iface;
    ISystemSupportInfoStatics ISystemSupportInfoStatics_iface;
    ISystemSupportInfoStatics2 ISystemSupportInfoStatics2_iface;
    LONG ref;
};

static inline struct support_statics *support_impl_from_IActivationFactory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct support_statics, IActivationFactory_iface );
}

static HRESULT WINAPI support_factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct support_statics *impl = support_impl_from_IActivationFactory( iface );

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

    if (IsEqualGUID( iid, &IID_ISystemSupportInfoStatics ))
    {
        *out = &impl->ISystemSupportInfoStatics_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_ISystemSupportInfoStatics2 ))
    {
        *out = &impl->ISystemSupportInfoStatics2_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI support_factory_AddRef( IActivationFactory *iface )
{
    struct support_statics *impl = support_impl_from_IActivationFactory( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p, ref %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI support_factory_Release( IActivationFactory *iface )
{
    struct support_statics *impl = support_impl_from_IActivationFactory( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    TRACE( "iface %p, ref %lu.\n", iface, ref );
    return ref;
}

static HRESULT WINAPI support_factory_GetIids( IActivationFactory *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI support_factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *class_name )
{
    FIXME( "iface %p, class_name %p stub!\n", iface, class_name );
    return E_NOTIMPL;
}

static HRESULT WINAPI support_factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI support_factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    FIXME( "iface %p, instance %p stub!\n", iface, instance );
    return E_NOTIMPL;
}

static const struct IActivationFactoryVtbl support_factory_vtbl =
{
    support_factory_QueryInterface,
    support_factory_AddRef,
    support_factory_Release,
    /* IInspectable methods */
    support_factory_GetIids,
    support_factory_GetRuntimeClassName,
    support_factory_GetTrustLevel,
    /* IActivationFactory methods */
    support_factory_ActivateInstance,
};

DEFINE_IINSPECTABLE( support_statics, ISystemSupportInfoStatics, struct support_statics, IActivationFactory_iface )
DEFINE_IINSPECTABLE( support_statics2, ISystemSupportInfoStatics2, struct support_statics, IActivationFactory_iface )

static HRESULT WINAPI support_statics_get_LocalSystemEdition( ISystemSupportInfoStatics *iface, HSTRING *value )
{
    static const WCHAR edition[] = L"Windows 11 Pro";

    TRACE( "iface %p, value %p.\n", iface, value );

    return create_hstring( edition, value );
}

static HRESULT WINAPI support_statics_get_OemSupportInfo( ISystemSupportInfoStatics *iface, IOemSupportInfo **value )
{
    TRACE( "iface %p, value %p.\n", iface, value );

    *value = &oem_support_info.IOemSupportInfo_iface;
    IOemSupportInfo_AddRef( *value );
    return S_OK;
}

static const struct ISystemSupportInfoStaticsVtbl support_statics_vtbl =
{
    support_statics_QueryInterface,
    support_statics_AddRef,
    support_statics_Release,
    /* IInspectable methods */
    support_statics_GetIids,
    support_statics_GetRuntimeClassName,
    support_statics_GetTrustLevel,
    /* ISystemSupportInfoStatics methods */
    support_statics_get_LocalSystemEdition,
    support_statics_get_OemSupportInfo,
};

static HRESULT WINAPI support_statics2_get_LocalDeviceInfo( ISystemSupportInfoStatics2 *iface, ISystemSupportDeviceInfo **value )
{
    TRACE( "iface %p, value %p.\n", iface, value );

    *value = &device_info.ISystemSupportDeviceInfo_iface;
    ISystemSupportDeviceInfo_AddRef( *value );
    return S_OK;
}

static const struct ISystemSupportInfoStatics2Vtbl support_statics2_vtbl =
{
    support_statics2_QueryInterface,
    support_statics2_AddRef,
    support_statics2_Release,
    /* IInspectable methods */
    support_statics2_GetIids,
    support_statics2_GetRuntimeClassName,
    support_statics2_GetTrustLevel,
    /* ISystemSupportInfoStatics2 methods */
    support_statics2_get_LocalDeviceInfo,
};

static struct support_statics support_statics =
{
    {&support_factory_vtbl},
    {&support_statics_vtbl},
    {&support_statics2_vtbl},
    1,
};

static IActivationFactory *support_factory = &support_statics.IActivationFactory_iface;

HRESULT WINAPI DllGetClassObject( REFCLSID clsid, REFIID riid, void **out )
{
    FIXME( "clsid %s, riid %s, out %p stub!\n", debugstr_guid(clsid), debugstr_guid(riid), out );
    return CLASS_E_CLASSNOTAVAILABLE;
}

HRESULT WINAPI DllGetActivationFactory( HSTRING classid, IActivationFactory **factory )
{
    const WCHAR *name = WindowsGetStringRawBuffer( classid, NULL );

    TRACE( "classid %s, factory %p.\n", debugstr_hstring(classid), factory );

    *factory = NULL;

    if (!wcscmp( name, RuntimeClass_Windows_System_Profile_SystemManufacturers_SmbiosInformation ))
        IActivationFactory_QueryInterface( smbios_factory, &IID_IActivationFactory, (void **)factory );

    if (!wcscmp( name, RuntimeClass_Windows_System_Profile_SystemManufacturers_SystemSupportInfo ))
        IActivationFactory_QueryInterface( support_factory, &IID_IActivationFactory, (void **)factory );

    if (*factory) return S_OK;
    return E_NOINTERFACE;
}
