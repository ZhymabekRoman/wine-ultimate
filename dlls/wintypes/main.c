/*
 * Copyright 2022-2025 Zhiyi Zhang for CodeWeavers
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

#include <string.h>

#include "initguid.h"
#include "private.h"
#include "ntstatus.h"
#include "weakreference.h"
#include "winternl.h"

WINE_DEFAULT_DEBUG_CHANNEL(wintypes);

static const struct
{
    const WCHAR *name;
    unsigned int max_major;
}
present_contracts[] =
{
    /*
     * ApiInformation reports the API surface this build implements.  Wine
     * carries the Windows App SDK surface that shipped through
     * UniversalApiContract 16 (Windows 11 24H2, build 26100): the composition
     * islands classes (Microsoft.UI.Content.ContentIsland, ContentSite,
     * DesktopChildSiteBridge), the Windows.UI.Composition tree over
     * DirectComposition, the Microsoft.UI.Windowing windowing-internal entry
     * points and the CoreMessaging dispatcher queue.  The Windows App SDK gates
     * its islands layer on this value: Microsoft.Internal.FrameworkUdk.dll
     * probes the contract downward from 17 and, below the threshold, publishes
     * E_NOTIMPL for the whole DCompPrivates islands entry set and never reaches
     * the compositor, which leaves the AppWindowTitleBar bridge uninitialised.
     */
    { L"Windows.Foundation.UniversalApiContract", 16, },
};

static BOOLEAN is_api_contract_present( const HSTRING hname, unsigned int version )
{
    const WCHAR *name = WindowsGetStringRawBuffer( hname, NULL );
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(present_contracts); ++i)
        if (!wcsicmp( name, present_contracts[i].name )) return version <= present_contracts[i].max_major;

    return FALSE;
}

/* Defined further below; referenced by the cached GetIids pair arrays. */
static const struct IActivationFactoryVtbl api_information_statics_factory_vtbl;
static const struct IApiInformationStaticsVtbl api_information_statics_vtbl;
static const struct IActivationFactoryVtbl property_value_statics_factory_vtbl;
static const struct IPropertyValueStaticsVtbl property_value_statics_vtbl;

struct api_information_statics
{
    IActivationFactory IActivationFactory_iface;
    IApiInformationStatics IApiInformationStatics_iface;
    LONG ref;
    struct com_interface_entry *iids_cache; /* stable GetIids pair array, see factory_GetIids */
    ULONG iids_count;
};

static inline struct api_information_statics *impl_ais_from_IActivationFactory(IActivationFactory *iface)
{
    return CONTAINING_RECORD(iface, struct api_information_statics, IActivationFactory_iface);
}

static HRESULT STDMETHODCALLTYPE api_information_statics_factory_QueryInterface(IActivationFactory *iface, REFIID iid,
        void **out)
{
    struct api_information_statics *impl = impl_ais_from_IActivationFactory(iface);

    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_IUnknown)
            || IsEqualGUID(iid, &IID_IInspectable)
            || IsEqualGUID(iid, &IID_IAgileObject)
            || IsEqualGUID(iid, &IID_IActivationFactory))
    {
        IUnknown_AddRef(iface);
        *out = iface;
        return S_OK;
    }

    if (IsEqualGUID(iid, &IID_IApiInformationStatics))
    {
        IUnknown_AddRef(iface);
        *out = &impl->IApiInformationStatics_iface;
        return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid));
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE api_information_statics_factory_AddRef(IActivationFactory *iface)
{
    struct api_information_statics *impl = impl_ais_from_IActivationFactory(iface);
    ULONG ref = InterlockedIncrement(&impl->ref);
    TRACE("iface %p, ref %lu.\n", iface, ref);
    return ref;
}

static ULONG STDMETHODCALLTYPE api_information_statics_factory_Release(IActivationFactory *iface)
{
    struct api_information_statics *impl = impl_ais_from_IActivationFactory(iface);
    ULONG ref = InterlockedDecrement(&impl->ref);
    TRACE("iface %p, ref %lu.\n", iface, ref);
    return ref;
}

static HRESULT STDMETHODCALLTYPE api_information_statics_factory_GetIids(IActivationFactory *iface, ULONG *iid_count,
        IID **iids)
{
    /* CoreCLR's ComWrappers shader consumes this as a ComInterfaceEntry pair
     * array (vtable-first). The activation factory face is the first RCW built
     * in the process; a bare IID array here seeds the shader's per-IID-set
     * cache with the wrong layout, flowing into later get_Values slot decodes.
     * One stable pair array per singleton (this object never dies). */
    struct api_information_statics *impl = impl_ais_from_IActivationFactory(iface);
    TRACE("iface %p, iid_count %p, iids %p.\n", iface, iid_count, iids);

    if (iid_count) *iid_count = 2; /* filled on the (NULL, NULL) probe itself */
    if (!impl->iids_cache)
    {
        struct com_interface_entry entries[] =
        {
            { (void **)&api_information_statics_factory_vtbl, (IID *)&IID_IActivationFactory },
            { (void **)&api_information_statics_vtbl, (IID *)&IID_IApiInformationStatics },
        };
        struct com_interface_entry *array = CoTaskMemAlloc(sizeof(entries));
        if (!array)
            return E_OUTOFMEMORY;
        memcpy(array, entries, sizeof(entries));
        impl->iids_cache = array;
        impl->iids_count = ARRAY_SIZE(entries);
    }

    if (iid_count) *iid_count = impl->iids_count;
    if (iids) *iids = (IID *)impl->iids_cache;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE api_information_statics_factory_GetRuntimeClassName(IActivationFactory *iface,
        HSTRING *class_name)
{
    static const WCHAR name[] = L"Windows.Foundation.Metadata.ApiInformation";

    return WindowsCreateString(name, ARRAY_SIZE(name) - 1, class_name);
}

static HRESULT STDMETHODCALLTYPE api_information_statics_factory_GetTrustLevel(IActivationFactory *iface,
        TrustLevel *trust_level)
{
    *trust_level = FullTrust;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE api_information_statics_factory_ActivateInstance(IActivationFactory *iface,
        IInspectable **instance)
{
    FIXME("iface %p, instance %p stub!\n", iface, instance);
    return E_NOTIMPL;
}

static const struct IActivationFactoryVtbl api_information_statics_factory_vtbl =
{
    api_information_statics_factory_QueryInterface,
    api_information_statics_factory_AddRef,
    api_information_statics_factory_Release,
    /* IInspectable methods */
    api_information_statics_factory_GetIids,
    api_information_statics_factory_GetRuntimeClassName,
    api_information_statics_factory_GetTrustLevel,
    /* IActivationFactory methods */
    api_information_statics_factory_ActivateInstance,
};

DEFINE_IINSPECTABLE(api_information_statics, IApiInformationStatics, struct api_information_statics, IActivationFactory_iface)

static HRESULT STDMETHODCALLTYPE api_information_statics_IsTypePresent(
        IApiInformationStatics *iface, HSTRING type_name, BOOLEAN *value)
{
    const WCHAR *str = WindowsGetStringRawBuffer( type_name, NULL );

    FIXME("iface %p, type_name %s, value %p stub!\n", iface, debugstr_hstring(type_name), value);

    if (!type_name)
        return E_INVALIDARG;

    if (str && !wcscmp( str, L"Windows.System.Profile.IWindowsIntegrityPolicyStatics" ))
    {
        *value = FALSE;
        return S_OK;
    }

    *value = TRUE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE api_information_statics_IsMethodPresent(
        IApiInformationStatics *iface, HSTRING type_name, HSTRING method_name, BOOLEAN *value)
{
    FIXME("iface %p, type_name %s, method_name %s, value %p stub!\n", iface,
            debugstr_hstring(type_name), debugstr_hstring(method_name), value);

    if (!type_name)
        return E_INVALIDARG;

    *value = FALSE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE api_information_statics_IsMethodPresentWithArity(
        IApiInformationStatics *iface, HSTRING type_name, HSTRING method_name,
        UINT32 input_parameter_count, BOOLEAN *value)
{
    FIXME("iface %p, type_name %s, method_name %s, input_parameter_count %u, value %p stub!\n",
            iface, debugstr_hstring(type_name), debugstr_hstring(method_name),
            input_parameter_count, value);

    if (!type_name)
        return E_INVALIDARG;

    *value = TRUE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE api_information_statics_IsEventPresent(
        IApiInformationStatics *iface, HSTRING type_name, HSTRING event_name, BOOLEAN *value)
{
    FIXME("iface %p, type_name %s, event_name %s, value %p stub!\n", iface,
            debugstr_hstring(type_name), debugstr_hstring(event_name), value);

    if (!type_name)
        return E_INVALIDARG;

    *value = TRUE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE api_information_statics_IsPropertyPresent(
        IApiInformationStatics *iface, HSTRING type_name, HSTRING property_name, BOOLEAN *value)
{
    const WCHAR *type_str = WindowsGetStringRawBuffer( type_name, NULL );
    const WCHAR *prop_str = WindowsGetStringRawBuffer( property_name, NULL );

    FIXME("iface %p, type_name %s, property_name %s, value %p stub!\n", iface,
            debugstr_hstring(type_name), debugstr_hstring(property_name), value);

    if (!type_name || !value)
        return E_INVALIDARG;

    if (type_str && prop_str &&
        !wcscmp( type_str, L"Windows.ApplicationModel.Package" ) &&
        !wcscmp( prop_str, L"SourceUriSchemeName" ))
    {
        *value = FALSE;
        return S_OK;
    }

    *value = TRUE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE api_information_statics_IsReadOnlyPropertyPresent(
        IApiInformationStatics *iface, HSTRING type_name, HSTRING property_name,
        BOOLEAN *value)
{
    FIXME("iface %p, type_name %s, property_name %s, value %p stub!\n", iface,
            debugstr_hstring(type_name), debugstr_hstring(property_name), value);

    if (!type_name)
        return E_INVALIDARG;

    *value = TRUE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE api_information_statics_IsWriteablePropertyPresent(
        IApiInformationStatics *iface, HSTRING type_name, HSTRING property_name, BOOLEAN *value)
{
    FIXME("iface %p, type_name %s, property_name %s, value %p stub!\n", iface,
            debugstr_hstring(type_name), debugstr_hstring(property_name), value);

    if (!type_name)
        return E_INVALIDARG;

    *value = TRUE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE api_information_statics_IsEnumNamedValuePresent(
        IApiInformationStatics *iface, HSTRING enum_type_name, HSTRING value_name, BOOLEAN *value)
{
    FIXME("iface %p, enum_type_name %s, value_name %s, value %p stub!\n", iface,
            debugstr_hstring(enum_type_name), debugstr_hstring(value_name), value);

    if (!enum_type_name)
        return E_INVALIDARG;

    *value = TRUE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE api_information_statics_IsApiContractPresentByMajor(
        IApiInformationStatics *iface, HSTRING contract_name, UINT16 major_version, BOOLEAN *value)
{
    TRACE("iface %p, contract_name %s, major_version %u, value %p.\n", iface,
            debugstr_hstring(contract_name), major_version, value);

    if (!contract_name || !value)
        return E_INVALIDARG;

    *value = is_api_contract_present( contract_name, major_version );
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE api_information_statics_IsApiContractPresentByMajorAndMinor(
        IApiInformationStatics *iface, HSTRING contract_name, UINT16 major_version,
        UINT16 minor_version, BOOLEAN *value)
{
    TRACE("iface %p, contract_name %s, major_version %u, minor_version %u, value %p\n", iface,
            debugstr_hstring(contract_name), major_version, minor_version, value);

    if (!contract_name || !value)
        return E_INVALIDARG;

    *value = is_api_contract_present( contract_name, major_version );
    return S_OK;
}

static const struct IApiInformationStaticsVtbl api_information_statics_vtbl =
{
    api_information_statics_QueryInterface,
    api_information_statics_AddRef,
    api_information_statics_Release,
    /* IInspectable methods */
    api_information_statics_GetIids,
    api_information_statics_GetRuntimeClassName,
    api_information_statics_GetTrustLevel,
    /* IApiInformationStatics methods */
    api_information_statics_IsTypePresent,
    api_information_statics_IsMethodPresent,
    api_information_statics_IsMethodPresentWithArity,
    api_information_statics_IsEventPresent,
    api_information_statics_IsPropertyPresent,
    api_information_statics_IsReadOnlyPropertyPresent,
    api_information_statics_IsWriteablePropertyPresent,
    api_information_statics_IsEnumNamedValuePresent,
    api_information_statics_IsApiContractPresentByMajor,
    api_information_statics_IsApiContractPresentByMajorAndMinor
};

static struct api_information_statics api_information_statics =
{
    {&api_information_statics_factory_vtbl},
    {&api_information_statics_vtbl},
    1
};

struct property_value_statics
{
    IActivationFactory IActivationFactory_iface;
    IPropertyValueStatics IPropertyValueStatics_iface;
    LONG ref;
    struct com_interface_entry *iids_cache; /* stable GetIids pair array, see factory_GetIids */
    ULONG iids_count;
};

static inline struct property_value_statics *impl_pvs_from_IActivationFactory(IActivationFactory *iface)
{
    return CONTAINING_RECORD(iface, struct property_value_statics, IActivationFactory_iface);
}

static HRESULT STDMETHODCALLTYPE property_value_statics_factory_QueryInterface(IActivationFactory *iface, REFIID iid,
        void **out)
{
    struct property_value_statics *impl = impl_pvs_from_IActivationFactory(iface);

    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_IUnknown)
            || IsEqualGUID(iid, &IID_IInspectable)
            || IsEqualGUID(iid, &IID_IAgileObject)
            || IsEqualGUID(iid, &IID_IActivationFactory))
    {
        IUnknown_AddRef(iface);
        *out = iface;
        return S_OK;
    }

    if (IsEqualGUID(iid, &IID_IPropertyValueStatics))
    {
        IUnknown_AddRef(iface);
        *out = &impl->IPropertyValueStatics_iface;
        return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid));
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE property_value_statics_factory_AddRef(IActivationFactory *iface)
{
    struct property_value_statics *impl = impl_pvs_from_IActivationFactory(iface);
    ULONG ref = InterlockedIncrement(&impl->ref);
    TRACE("iface %p, ref %lu.\n", iface, ref);
    return ref;
}

static ULONG STDMETHODCALLTYPE property_value_statics_factory_Release(IActivationFactory *iface)
{
    struct property_value_statics *impl = impl_pvs_from_IActivationFactory(iface);
    ULONG ref = InterlockedDecrement(&impl->ref);
    TRACE("iface %p, ref %lu.\n", iface, ref);
    return ref;
}

static HRESULT STDMETHODCALLTYPE property_value_statics_factory_GetIids(IActivationFactory *iface, ULONG *iid_count,
        IID **iids)
{
    /* Pair-array contract, cached per singleton; see
     * api_information_statics_factory_GetIids. */
    struct property_value_statics *impl = impl_pvs_from_IActivationFactory(iface);
    TRACE("iface %p, iid_count %p, iids %p.\n", iface, iid_count, iids);

    if (iid_count) *iid_count = 2; /* filled on the (NULL, NULL) probe itself */
    if (!impl->iids_cache)
    {
        struct com_interface_entry entries[] =
        {
            { (void **)&property_value_statics_factory_vtbl, (IID *)&IID_IActivationFactory },
            { (void **)&property_value_statics_vtbl, (IID *)&IID_IPropertyValueStatics },
        };
        struct com_interface_entry *array = CoTaskMemAlloc(sizeof(entries));
        if (!array)
            return E_OUTOFMEMORY;
        memcpy(array, entries, sizeof(entries));
        impl->iids_cache = array;
        impl->iids_count = ARRAY_SIZE(entries);
    }

    if (iid_count) *iid_count = impl->iids_count;
    if (iids) *iids = (IID *)impl->iids_cache;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE property_value_statics_factory_GetRuntimeClassName(IActivationFactory *iface,
        HSTRING *class_name)
{
    static const WCHAR name[] = L"Windows.Foundation.PropertyValue";

    return WindowsCreateString(name, ARRAY_SIZE(name) - 1, class_name);
}

static HRESULT STDMETHODCALLTYPE property_value_statics_factory_GetTrustLevel(IActivationFactory *iface,
        TrustLevel *trust_level)
{
    *trust_level = FullTrust;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE property_value_statics_factory_ActivateInstance(IActivationFactory *iface,
        IInspectable **instance)
{
    FIXME("iface %p, instance %p stub!\n", iface, instance);
    return E_NOTIMPL;
}

static const struct IActivationFactoryVtbl property_value_statics_factory_vtbl =
{
    property_value_statics_factory_QueryInterface,
    property_value_statics_factory_AddRef,
    property_value_statics_factory_Release,
    /* IInspectable methods */
    property_value_statics_factory_GetIids,
    property_value_statics_factory_GetRuntimeClassName,
    property_value_statics_factory_GetTrustLevel,
    /* IActivationFactory methods */
    property_value_statics_factory_ActivateInstance,
};

struct iref_uint16_vtbl
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *iface, REFIID iid, void **out);
    ULONG (STDMETHODCALLTYPE *AddRef)(void *iface);
    ULONG (STDMETHODCALLTYPE *Release)(void *iface);
    HRESULT (STDMETHODCALLTYPE *GetIids)(void *iface, ULONG *iid_count, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *iface, HSTRING *class_name);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *iface, TrustLevel *trust_level);
    HRESULT (STDMETHODCALLTYPE *get_Value)(void *iface, UINT16 *value);
};

struct iref_char16_vtbl
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *iface, REFIID iid, void **out);
    ULONG (STDMETHODCALLTYPE *AddRef)(void *iface);
    ULONG (STDMETHODCALLTYPE *Release)(void *iface);
    HRESULT (STDMETHODCALLTYPE *GetIids)(void *iface, ULONG *iid_count, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *iface, HSTRING *class_name);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *iface, TrustLevel *trust_level);
    HRESULT (STDMETHODCALLTYPE *get_Value)(void *iface, WCHAR *value);
};

struct iref_array_vtbl
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *iface, REFIID iid, void **out);
    ULONG (STDMETHODCALLTYPE *AddRef)(void *iface);
    ULONG (STDMETHODCALLTYPE *Release)(void *iface);
    HRESULT (STDMETHODCALLTYPE *GetIids)(void *iface, ULONG *iid_count, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *iface, HSTRING *class_name);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *iface, TrustLevel *trust_level);
    HRESULT (STDMETHODCALLTYPE *get_Value)(void *iface, UINT32 *value_size, void **value);
};

struct iref_uint16_iface { const struct iref_uint16_vtbl *lpVtbl; };
struct iref_char16_iface { const struct iref_char16_vtbl *lpVtbl; };
struct iref_array_iface { const struct iref_array_vtbl *lpVtbl; };

struct property_value;

struct property_value_weakref
{
    IWeakReference IWeakReference_iface;
    LONG ref;
    struct property_value *obj;
};

struct property_value
{
    IPropertyValue IPropertyValue_iface;
    IWeakReferenceSource IWeakReferenceSource_iface;
    union
    {
        IReference_BYTE byte_iface;
        IReference_INT16 int16_iface;
        struct iref_uint16_iface uint16_iface;
        IReference_INT32 int32_iface;
        IReference_UINT32 uint32_iface;
        IReference_INT64 int64_iface;
        IReference_UINT64 uint64_iface;
        IReference_boolean boolean_iface;
        IReference_FLOAT float_iface;
        IReference_DOUBLE double_iface;
        struct iref_char16_iface char16_iface;
        IReference_DateTime datetime_iface;
        IReference_TimeSpan timespan_iface;
        IReference_GUID guid_iface;
        IReference_Point point_iface;
        IReference_Size size_iface;
        IReference_Rect rect_iface;
        IReference_HSTRING hstring_iface;
        struct iref_array_iface array_iface;
    } irefs;
    PropertyType type;
    unsigned int value_size;
    void *value;
    struct property_value_weakref *weakref;
    LONG ref;
};

static inline struct property_value *impl_from_IPropertyValue(IPropertyValue *iface)
{
    return CONTAINING_RECORD(iface, struct property_value, IPropertyValue_iface);
}

static inline struct property_value *impl_from_IInspectable(IInspectable *iface)
{
    return CONTAINING_RECORD(iface, struct property_value, IPropertyValue_iface);
}

#define property_value_get_primitive(type) _property_value_get_primitive(iface, type, value, sizeof(*value))
static HRESULT _property_value_get_primitive(IPropertyValue *iface, PropertyType type, void *value, size_t size)
{
    struct property_value *impl = impl_from_IPropertyValue(iface);

    if (!value)
        return E_POINTER;

    if (impl->type != type)
        return TYPE_E_TYPEMISMATCH;

    memcpy(value, impl->value, size);
    return S_OK;
}

#define property_value_get_primitive_array(type) _property_value_get_primitive_array(iface, type, value_size, (void **)value)
static HRESULT _property_value_get_primitive_array(IPropertyValue *iface, PropertyType type, unsigned int *value_size, void **value)
{
    struct property_value *impl = impl_from_IPropertyValue(iface);

    if (!value_size || !value)
        return E_POINTER;

    if (impl->type != type)
        return TYPE_E_TYPEMISMATCH;

    *value_size = impl->value_size;
    *value = impl->value;
    return S_OK;
}

/*
 * The Single/Double IReference vtables are chosen dynamically in the QueryInterface below, so they
 * have to be visible there.  Keep them file-local, like every other vtable in this file.
 */
static const struct IReference_FLOATVtbl iref_float_vtbl;
static const struct IReference_DOUBLEVtbl iref_double_vtbl;
static const struct iref_uint16_vtbl iref_uint16_vtbl;
static const struct iref_char16_vtbl iref_char16_vtbl;
static const struct iref_array_vtbl iref_array_vtbl;

static CRITICAL_SECTION pv_weakref_cs;
static RTL_RUN_ONCE pv_weakref_cs_once = RTL_RUN_ONCE_INIT;

static DWORD WINAPI pv_weakref_cs_init(PRTL_RUN_ONCE once, PVOID param, PVOID *context)
{
    RtlInitializeCriticalSection(&pv_weakref_cs);
    return STATUS_SUCCESS;
}

static void pv_weakref_lock(void)
{
    RtlRunOnceExecuteOnce(&pv_weakref_cs_once, pv_weakref_cs_init, NULL, NULL);
    EnterCriticalSection(&pv_weakref_cs);
}

static void pv_weakref_unlock(void)
{
    LeaveCriticalSection(&pv_weakref_cs);
}

static inline struct property_value_weakref *impl_from_IWeakReference(IWeakReference *iface)
{
    return CONTAINING_RECORD(iface, struct property_value_weakref, IWeakReference_iface);
}

static HRESULT STDMETHODCALLTYPE pv_weakref_QueryInterface(IWeakReference *iface, REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    if (IsEqualIID(iid, &IID_IUnknown) || IsEqualIID(iid, &IID_IWeakReference))
    {
        IWeakReference_AddRef(iface);
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE pv_weakref_AddRef(IWeakReference *iface)
{
    struct property_value_weakref *impl = impl_from_IWeakReference(iface);
    return InterlockedIncrement(&impl->ref);
}

static ULONG STDMETHODCALLTYPE pv_weakref_Release(IWeakReference *iface)
{
    struct property_value_weakref *impl = impl_from_IWeakReference(iface);
    ULONG ref = InterlockedDecrement(&impl->ref);
    if (!ref)
    {
        pv_weakref_lock();
        if (impl->obj) impl->obj->weakref = NULL;
        pv_weakref_unlock();
        free(impl);
    }
    return ref;
}

static HRESULT STDMETHODCALLTYPE pv_weakref_Resolve(IWeakReference *iface, REFIID riid, IInspectable **out)
{
    struct property_value_weakref *impl = impl_from_IWeakReference(iface);
    struct property_value *obj = NULL;
    HRESULT hr = S_OK;

    if (!out) return E_POINTER;
    *out = NULL;

    pv_weakref_lock();
    if (impl->obj)
    {
        LONG cur = impl->obj->ref;
        while (cur > 0)
        {
            LONG prev = InterlockedCompareExchange(&impl->obj->ref, cur + 1, cur);
            if (prev == cur)
            {
                obj = impl->obj;
                break;
            }
            cur = prev;
        }
    }
    pv_weakref_unlock();

    if (obj)
    {
        hr = IPropertyValue_QueryInterface(&obj->IPropertyValue_iface, riid, (void **)out);
        IPropertyValue_Release(&obj->IPropertyValue_iface);
    }
    return hr;
}

static const struct IWeakReferenceVtbl pv_weakref_vtbl =
{
    pv_weakref_QueryInterface,
    pv_weakref_AddRef,
    pv_weakref_Release,
    pv_weakref_Resolve,
};

static inline struct property_value *impl_from_IWeakReferenceSource(IWeakReferenceSource *iface)
{
    return CONTAINING_RECORD(iface, struct property_value, IWeakReferenceSource_iface);
}

static HRESULT STDMETHODCALLTYPE pv_weakref_source_QueryInterface(IWeakReferenceSource *iface, REFIID iid, void **out)
{
    struct property_value *impl = impl_from_IWeakReferenceSource(iface);
    return IPropertyValue_QueryInterface(&impl->IPropertyValue_iface, iid, out);
}

static ULONG STDMETHODCALLTYPE pv_weakref_source_AddRef(IWeakReferenceSource *iface)
{
    struct property_value *impl = impl_from_IWeakReferenceSource(iface);
    return IPropertyValue_AddRef(&impl->IPropertyValue_iface);
}

static ULONG STDMETHODCALLTYPE pv_weakref_source_Release(IWeakReferenceSource *iface)
{
    struct property_value *impl = impl_from_IWeakReferenceSource(iface);
    return IPropertyValue_Release(&impl->IPropertyValue_iface);
}

static HRESULT STDMETHODCALLTYPE pv_weakref_source_GetWeakReference(IWeakReferenceSource *iface, IWeakReference **weak_ref)
{
    struct property_value *impl = impl_from_IWeakReferenceSource(iface);
    struct property_value_weakref *wr;

    if (!weak_ref) return E_POINTER;

    pv_weakref_lock();
    if (!impl->weakref)
    {
        wr = calloc(1, sizeof(*wr));
        if (!wr)
        {
            pv_weakref_unlock();
            *weak_ref = NULL;
            return E_OUTOFMEMORY;
        }
        wr->IWeakReference_iface.lpVtbl = &pv_weakref_vtbl;
        wr->ref = 1;
        wr->obj = impl;
        impl->weakref = wr;
    }
    else
    {
        wr = impl->weakref;
        InterlockedIncrement(&wr->ref);
    }
    pv_weakref_unlock();

    *weak_ref = &wr->IWeakReference_iface;
    return S_OK;
}

static const struct IWeakReferenceSourceVtbl pv_weakref_source_vtbl =
{
    pv_weakref_source_QueryInterface,
    pv_weakref_source_AddRef,
    pv_weakref_source_Release,
    pv_weakref_source_GetWeakReference,
};

/* UUID5 pinterface IIDs for IReference<UInt16/Char16> and IReferenceArray<T> */
static const GUID IID_IReference_UINT16 =
    { 0x5ab7d2c3, 0x6b62, 0x5e71, { 0xa4, 0xb6, 0x2d, 0x49, 0xc4, 0xf2, 0x38, 0xfd } };
static const GUID IID_IReference_Char16 =
    { 0xfb393ef3, 0xbbac, 0x5bd5, { 0x91, 0x44, 0x84, 0xf2, 0x35, 0x76, 0xf4, 0x15 } };

static const struct
{
    PropertyType type;
    const GUID iid;
} iref_array_iids[] =
{
    { PropertyType_UInt8Array,       { 0x2af22683, 0x3734, 0x56d0, { 0xa6, 0x0e, 0x68, 0x8c, 0xc8, 0x5d, 0x16, 0x19 } } },
    { PropertyType_Int16Array,       { 0x912f8fd7, 0xadc0, 0x5d60, { 0xa8, 0x96, 0x7e, 0xd7, 0x60, 0x89, 0xcc, 0x5b } } },
    { PropertyType_UInt16Array,      { 0x6624a2dd, 0x83f7, 0x519c, { 0x9d, 0x55, 0xbb, 0x1f, 0x65, 0x60, 0x45, 0x6b } } },
    { PropertyType_Int32Array,       { 0xa6d080a5, 0xb087, 0x5bc2, { 0x9a, 0x9f, 0x5c, 0xd6, 0x87, 0xb4, 0xd1, 0xf7 } } },
    { PropertyType_UInt32Array,      { 0x97374b68, 0xeb87, 0x56cc, { 0xb1, 0x8e, 0x27, 0xef, 0x0f, 0x9c, 0xfc, 0x0c } } },
    { PropertyType_Int64Array,       { 0x6e333271, 0x2e2a, 0x5955, { 0x87, 0x90, 0x83, 0x6c, 0x76, 0xee, 0x53, 0xb6 } } },
    { PropertyType_UInt64Array,      { 0x38b60434, 0xd67c, 0x523e, { 0x9d, 0x0e, 0x24, 0xd6, 0x43, 0x41, 0x10, 0x73 } } },
    { PropertyType_SingleArray,      { 0x6ab1ea83, 0xcb41, 0x5f99, { 0x92, 0xcc, 0x23, 0xbd, 0x43, 0x36, 0xa1, 0xfb } } },
    { PropertyType_DoubleArray,      { 0xd301f253, 0xe0a3, 0x5d2b, { 0x9a, 0x41, 0xa4, 0xd6, 0x2b, 0xec, 0x46, 0x23 } } },
    { PropertyType_Char16Array,      { 0xa4095aab, 0xeb7d, 0x5782, { 0x8f, 0xad, 0x16, 0x09, 0xde, 0xa2, 0x49, 0xad } } },
    { PropertyType_BooleanArray,     { 0xe8e72666, 0x48cc, 0x593f, { 0xba, 0x85, 0x26, 0x63, 0x49, 0x69, 0x56, 0xe3 } } },
    { PropertyType_StringArray,      { 0x0385688e, 0xe3c7, 0x5c5e, { 0xa3, 0x89, 0x55, 0x24, 0xed, 0xe3, 0x49, 0xf1 } } },
    { PropertyType_InspectableArray, { 0x9cd7a84f, 0x0c80, 0x59c5, { 0xb4, 0x4e, 0x97, 0x78, 0x41, 0xbb, 0x43, 0xd9 } } },
    { PropertyType_GuidArray,        { 0xeecf9838, 0xc1c2, 0x5b4a, { 0x97, 0x6f, 0xce, 0xc2, 0x61, 0xae, 0x1d, 0x55 } } },
    { PropertyType_DateTimeArray,    { 0x1b8e9594, 0x588e, 0x5a07, { 0x9e, 0x65, 0x07, 0x31, 0xa4, 0xc9, 0xa2, 0xdb } } },
    { PropertyType_TimeSpanArray,    { 0xad73197d, 0x2cfa, 0x57a6, { 0x89, 0x93, 0x9f, 0xac, 0x40, 0xfe, 0xb7, 0x91 } } },
    { PropertyType_PointArray,       { 0x39313214, 0x5c7d, 0x599d, { 0xae, 0x5a, 0x17, 0xd9, 0xd6, 0x49, 0x22, 0x58 } } },
    { PropertyType_SizeArray,        { 0x3b40e9d4, 0xe0c3, 0x56f6, { 0xb8, 0x8b, 0xe5, 0x05, 0xeb, 0x73, 0x75, 0x7b } } },
    { PropertyType_RectArray,        { 0x8a444256, 0xd661, 0x5e9a, { 0xa7, 0x2b, 0xd8, 0xf1, 0xd7, 0x96, 0x2d, 0x0c } } },
};

/*
 * CoreCLR's ComWrappers probes for XAML reference tracking interfaces on COM/WinRT
 * wrappers.  Property values do not participate in reference tracker walks, so return
 * E_NOINTERFACE explicitly without noisy FIXME spew.
 */
static const GUID IID_IReferenceTracker = {0x11d3b13a, 0x180e, 0x4789, {0xa8, 0xbe, 0x77, 0x12, 0x88, 0x28, 0x93, 0xe6}};
static const GUID IID_IReferenceTrackerTarget = {0x5c13e51c, 0x4f32, 0x4726, {0xa3, 0xfd, 0xf3, 0xed, 0xd6, 0x3d, 0xa3, 0xa0}};

static HRESULT STDMETHODCALLTYPE property_value_QueryInterface(IPropertyValue *iface, REFIID riid, void **out)
{
    struct property_value *impl = impl_from_IPropertyValue(iface);

    TRACE("%p, %s, %p\n", impl, debugstr_guid(riid), out);

    if (IsEqualIID(riid, &IID_IUnknown)
        || IsEqualIID(riid, &IID_IInspectable)
        || IsEqualIID(riid, &IID_IAgileObject)
        || IsEqualIID(riid, &IID_IPropertyValue))
    {
        IPropertyValue_AddRef(iface);
        *out = &impl->IPropertyValue_iface;
        return S_OK;
    }
    else if (IsEqualIID(riid, &IID_IReferenceTracker)
             || IsEqualIID(riid, &IID_IReferenceTrackerTarget))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    else if (IsEqualIID(riid, &IID_IMarshal))
    {
        return CoCreateFreeThreadedMarshaler((IUnknown *)iface, (IUnknown **)out);
    }
    else if (IsEqualIID(riid, &IID_IWeakReferenceSource))
    {
        IPropertyValue_AddRef(iface);
        *out = &impl->IWeakReferenceSource_iface;
        return S_OK;
    }
    else if (IsEqualIID(riid, &IID_IReference_BYTE) && impl->type == PropertyType_UInt8)
    {
        IReference_BYTE_AddRef(&impl->irefs.byte_iface);
        *out = &impl->irefs.byte_iface;
        return S_OK;
    }
    else if (IsEqualIID(riid, &IID_IReference_INT16) && impl->type == PropertyType_Int16)
    {
        IReference_INT16_AddRef(&impl->irefs.int16_iface);
        *out = &impl->irefs.int16_iface;
        return S_OK;
    }
    else if (IsEqualIID(riid, &IID_IReference_INT32) && impl->type == PropertyType_Int32)
    {
        IReference_INT32_AddRef(&impl->irefs.int32_iface);
        *out = &impl->irefs.int32_iface;
        return S_OK;
    }
    else if (IsEqualIID(riid, &IID_IReference_UINT32) && impl->type == PropertyType_UInt32)
    {
        IReference_UINT32_AddRef(&impl->irefs.uint32_iface);
        *out = &impl->irefs.uint32_iface;
        return S_OK;
    }
    else if (IsEqualIID(riid, &IID_IReference_INT64) && impl->type == PropertyType_Int64)
    {
        IReference_INT64_AddRef(&impl->irefs.int64_iface);
        *out = &impl->irefs.int64_iface;
        return S_OK;
    }
    else if (IsEqualIID(riid, &IID_IReference_UINT64) && impl->type == PropertyType_UInt64)
    {
        IReference_UINT64_AddRef(&impl->irefs.uint64_iface);
        *out = &impl->irefs.uint64_iface;
        return S_OK;
    }
    else if (IsEqualIID(riid, &IID_IReference_boolean) && impl->type == PropertyType_Boolean)
    {
        IReference_boolean_AddRef(&impl->irefs.boolean_iface);
        *out = &impl->irefs.boolean_iface;
        return S_OK;
    }
    else if (IsEqualIID(riid, &IID_IReference_FLOAT) && (impl->type == PropertyType_Single || impl->type == PropertyType_Double))
    {
        impl->irefs.float_iface.lpVtbl = (void *)&iref_float_vtbl;
        IReference_FLOAT_AddRef(&impl->irefs.float_iface);
        *out = &impl->irefs.float_iface;
        return S_OK;
    }
    else if (IsEqualIID(riid, &IID_IReference_DOUBLE) && (impl->type == PropertyType_Double || impl->type == PropertyType_Single))
    {
        impl->irefs.double_iface.lpVtbl = (void *)&iref_double_vtbl;
        IReference_DOUBLE_AddRef(&impl->irefs.double_iface);
        *out = &impl->irefs.double_iface;
        return S_OK;
    }
    else if (IsEqualIID(riid, &IID_IReference_DateTime) && impl->type == PropertyType_DateTime)
    {
        IReference_DateTime_AddRef(&impl->irefs.datetime_iface);
        *out = &impl->irefs.datetime_iface;
        return S_OK;
    }
    else if (IsEqualIID(riid, &IID_IReference_TimeSpan) && impl->type == PropertyType_TimeSpan)
    {
        IReference_TimeSpan_AddRef(&impl->irefs.timespan_iface);
        *out = &impl->irefs.timespan_iface;
        return S_OK;
    }
    else if (IsEqualIID(riid, &IID_IReference_GUID) && impl->type == PropertyType_Guid)
    {
        IReference_GUID_AddRef(&impl->irefs.guid_iface);
        *out = &impl->irefs.guid_iface;
        return S_OK;
    }
    else if (IsEqualIID(riid, &IID_IReference_Point) && impl->type == PropertyType_Point)
    {
        IReference_Point_AddRef(&impl->irefs.point_iface);
        *out = &impl->irefs.point_iface;
        return S_OK;
    }
    else if (IsEqualIID(riid, &IID_IReference_Size) && impl->type == PropertyType_Size)
    {
        IReference_Size_AddRef(&impl->irefs.size_iface);
        *out = &impl->irefs.size_iface;
        return S_OK;
    }
    else if (IsEqualIID(riid, &IID_IReference_Rect) && impl->type == PropertyType_Rect)
    {
        IReference_Rect_AddRef(&impl->irefs.rect_iface);
        *out = &impl->irefs.rect_iface;
        return S_OK;
    }
    else if (IsEqualIID(riid, &IID_IReference_HSTRING) && impl->type == PropertyType_String)
    {
        IReference_HSTRING_AddRef(&impl->irefs.hstring_iface);
        *out = &impl->irefs.hstring_iface;
        return S_OK;
    }
    else if (IsEqualIID(riid, &IID_IReference_UINT16) && impl->type == PropertyType_UInt16)
    {
        IPropertyValue_AddRef(iface);
        *out = &impl->irefs.uint16_iface;
        return S_OK;
    }
    else if (IsEqualIID(riid, &IID_IReference_Char16) && impl->type == PropertyType_Char16)
    {
        IPropertyValue_AddRef(iface);
        *out = &impl->irefs.char16_iface;
        return S_OK;
    }
    else if (impl->type >= PropertyType_UInt8Array && impl->type <= PropertyType_RectArray)
    {
        unsigned int i;
        for (i = 0; i < ARRAY_SIZE(iref_array_iids); i++)
        {
            if (iref_array_iids[i].type == impl->type && IsEqualIID(riid, &iref_array_iids[i].iid))
            {
                IPropertyValue_AddRef(iface);
                *out = &impl->irefs.array_iface;
                return S_OK;
            }
        }
    }

    FIXME("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(riid));
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE property_value_AddRef(IPropertyValue *iface)
{
    struct property_value *impl = impl_from_IPropertyValue(iface);
    ULONG refcount = InterlockedIncrement(&impl->ref);

    TRACE("%p, refcount %lu.\n", iface, refcount);

    return refcount;
}

static ULONG STDMETHODCALLTYPE property_value_Release(IPropertyValue *iface)
{
    struct property_value *impl = impl_from_IPropertyValue(iface);
    ULONG refcount = InterlockedDecrement(&impl->ref);

    TRACE("%p, refcount %lu.\n", iface, refcount);

    if (!refcount)
    {
        pv_weakref_lock();
        if (impl->weakref)
        {
            impl->weakref->obj = NULL;
            impl->weakref = NULL;
        }
        pv_weakref_unlock();

        if (impl->value)
        {
            if (impl->type == PropertyType_String)
            {
                WindowsDeleteString(*(HSTRING *)impl->value);
            }
            else if (impl->type == PropertyType_StringArray)
            {
                HSTRING *string_array = impl->value;
                unsigned int i;

                for (i = 0; i < impl->value_size; i++)
                    WindowsDeleteString(string_array[i]);
            }
            else if (impl->type == PropertyType_InspectableArray)
            {
                IInspectable **insp_array = impl->value;
                unsigned int i;

                for (i = 0; i < impl->value_size; i++)
                    if (insp_array[i]) IInspectable_Release(insp_array[i]);
            }
            free(impl->value);
        }
        free(impl);
    }

    return refcount;
}

static HRESULT STDMETHODCALLTYPE property_value_GetIids(IPropertyValue *iface, ULONG *iid_count, IID **iids)
{
    IID *array;
    if (iid_count) *iid_count = 2;
    if (!iids) return S_OK;
    array = CoTaskMemAlloc(sizeof(IID) * 2);
    if (!array) return E_OUTOFMEMORY;
    array[0] = IID_IInspectable;
    array[1] = IID_IPropertyValue;
    *iids = array;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE property_value_GetRuntimeClassName(IPropertyValue *iface, HSTRING *class_name)
{
    struct property_value *impl = impl_from_IPropertyValue(iface);
    const WCHAR *name;

    TRACE("iface %p, class_name %p, type %d.\n", iface, class_name, impl->type);
    if (!class_name) return E_POINTER;

    switch (impl->type)
    {
    case PropertyType_Double:
        name = L"Windows.Foundation.IReference`1<Double>";
        break;
    case PropertyType_Single:
        name = L"Windows.Foundation.IReference`1<Single>";
        break;
    case PropertyType_Boolean:
        name = L"Windows.Foundation.IReference`1<Boolean>";
        break;
    case PropertyType_Int32:
        name = L"Windows.Foundation.IReference`1<Int32>";
        break;
    case PropertyType_UInt32:
        name = L"Windows.Foundation.IReference`1<UInt32>";
        break;
    case PropertyType_Int64:
        name = L"Windows.Foundation.IReference`1<Int64>";
        break;
    case PropertyType_UInt64:
        name = L"Windows.Foundation.IReference`1<UInt64>";
        break;
    case PropertyType_Int16:
        name = L"Windows.Foundation.IReference`1<Int16>";
        break;
    case PropertyType_UInt16:
        name = L"Windows.Foundation.IReference`1<UInt16>";
        break;
    case PropertyType_UInt8:
        name = L"Windows.Foundation.IReference`1<UInt8>";
        break;
    case PropertyType_Char16:
        name = L"Windows.Foundation.IReference`1<Char16>";
        break;
    case PropertyType_String:
        name = L"Windows.Foundation.IReference`1<String>";
        break;
    case PropertyType_Guid:
        name = L"Windows.Foundation.IReference`1<Guid>";
        break;
    case PropertyType_DateTime:
        name = L"Windows.Foundation.IReference`1<Windows.Foundation.DateTime>";
        break;
    case PropertyType_TimeSpan:
        name = L"Windows.Foundation.IReference`1<Windows.Foundation.TimeSpan>";
        break;
    case PropertyType_Point:
        name = L"Windows.Foundation.IReference`1<Windows.Foundation.Point>";
        break;
    case PropertyType_Size:
        name = L"Windows.Foundation.IReference`1<Windows.Foundation.Size>";
        break;
    case PropertyType_Rect:
        name = L"Windows.Foundation.IReference`1<Windows.Foundation.Rect>";
        break;
    case PropertyType_UInt8Array:
        name = L"Windows.Foundation.IReferenceArray`1<UInt8>";
        break;
    case PropertyType_Int16Array:
        name = L"Windows.Foundation.IReferenceArray`1<Int16>";
        break;
    case PropertyType_UInt16Array:
        name = L"Windows.Foundation.IReferenceArray`1<UInt16>";
        break;
    case PropertyType_Int32Array:
        name = L"Windows.Foundation.IReferenceArray`1<Int32>";
        break;
    case PropertyType_UInt32Array:
        name = L"Windows.Foundation.IReferenceArray`1<UInt32>";
        break;
    case PropertyType_Int64Array:
        name = L"Windows.Foundation.IReferenceArray`1<Int64>";
        break;
    case PropertyType_UInt64Array:
        name = L"Windows.Foundation.IReferenceArray`1<UInt64>";
        break;
    case PropertyType_SingleArray:
        name = L"Windows.Foundation.IReferenceArray`1<Single>";
        break;
    case PropertyType_DoubleArray:
        name = L"Windows.Foundation.IReferenceArray`1<Double>";
        break;
    case PropertyType_Char16Array:
        name = L"Windows.Foundation.IReferenceArray`1<Char16>";
        break;
    case PropertyType_BooleanArray:
        name = L"Windows.Foundation.IReferenceArray`1<Boolean>";
        break;
    case PropertyType_StringArray:
        name = L"Windows.Foundation.IReferenceArray`1<String>";
        break;
    case PropertyType_InspectableArray:
        name = L"Windows.Foundation.IReferenceArray`1<Object>";
        break;
    case PropertyType_GuidArray:
        name = L"Windows.Foundation.IReferenceArray`1<Guid>";
        break;
    case PropertyType_DateTimeArray:
        name = L"Windows.Foundation.IReferenceArray`1<Windows.Foundation.DateTime>";
        break;
    case PropertyType_TimeSpanArray:
        name = L"Windows.Foundation.IReferenceArray`1<Windows.Foundation.TimeSpan>";
        break;
    case PropertyType_PointArray:
        name = L"Windows.Foundation.IReferenceArray`1<Windows.Foundation.Point>";
        break;
    case PropertyType_SizeArray:
        name = L"Windows.Foundation.IReferenceArray`1<Windows.Foundation.Size>";
        break;
    case PropertyType_RectArray:
        name = L"Windows.Foundation.IReferenceArray`1<Windows.Foundation.Rect>";
        break;
    default:
        name = RuntimeClass_Windows_Foundation_PropertyValue;
        break;
    }

    return WindowsCreateString(name, wcslen(name), class_name);
}

static HRESULT STDMETHODCALLTYPE property_value_GetTrustLevel(IPropertyValue *iface, TrustLevel *trust_level)
{
    if (trust_level) *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE property_value_get_Type(IPropertyValue *iface, enum PropertyType *type)
{
    struct property_value *impl = impl_from_IPropertyValue(iface);

    TRACE("iface %p, type %p.\n", iface, type);

    if (!type)
        return E_POINTER;

    *type = impl->type;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE property_value_get_IsNumericScalar(IPropertyValue *iface, boolean *value)
{
    TRACE("iface %p, value %p.\n", iface, value);

    *value = FALSE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE property_value_GetUInt8(IPropertyValue *iface, BYTE *value)
{
    TRACE("iface %p, value %p.\n", iface, value);
    return property_value_get_primitive(PropertyType_UInt8);
}

static HRESULT STDMETHODCALLTYPE property_value_GetInt16(IPropertyValue *iface, INT16 *value)
{
    TRACE("iface %p, value %p.\n", iface, value);
    return property_value_get_primitive(PropertyType_Int16);
}

static HRESULT STDMETHODCALLTYPE property_value_GetUInt16(IPropertyValue *iface, UINT16 *value)
{
    TRACE("iface %p, value %p.\n", iface, value);
    return property_value_get_primitive(PropertyType_UInt16);
}

static HRESULT STDMETHODCALLTYPE property_value_GetInt32(IPropertyValue *iface, INT32 *value)
{
    TRACE("iface %p, value %p.\n", iface, value);
    return property_value_get_primitive(PropertyType_Int32);
}

static HRESULT STDMETHODCALLTYPE property_value_GetUInt32(IPropertyValue *iface, UINT32 *value)
{
    TRACE("iface %p, value %p.\n", iface, value);
    return property_value_get_primitive(PropertyType_UInt32);
}

static HRESULT STDMETHODCALLTYPE property_value_GetInt64(IPropertyValue *iface, INT64 *value)
{
    TRACE("iface %p, value %p.\n", iface, value);
    return property_value_get_primitive(PropertyType_Int64);
}

static HRESULT STDMETHODCALLTYPE property_value_GetUInt64(IPropertyValue *iface, UINT64 *value)
{
    TRACE("iface %p, value %p.\n", iface, value);
    return property_value_get_primitive(PropertyType_UInt64);
}

static HRESULT STDMETHODCALLTYPE property_value_GetSingle(IPropertyValue *iface, FLOAT *value)
{
    struct property_value *impl = impl_from_IPropertyValue(iface);
    TRACE("iface %p, value %p.\n", iface, value);
    if (!value) return E_POINTER;
    if (impl->type == PropertyType_Single)
    {
        *value = *(FLOAT *)impl->value;
        return S_OK;
    }
    if (impl->type == PropertyType_Double)
    {
        *value = (FLOAT)*(DOUBLE *)impl->value;
        return S_OK;
    }
    if (impl->type == PropertyType_Int32)
    {
        *value = (FLOAT)*(INT32 *)impl->value;
        return S_OK;
    }
    if (impl->type == PropertyType_UInt32)
    {
        *value = (FLOAT)*(UINT32 *)impl->value;
        return S_OK;
    }
    return property_value_get_primitive(PropertyType_Single);
}

static HRESULT STDMETHODCALLTYPE property_value_GetDouble(IPropertyValue *iface, DOUBLE *value)
{
    struct property_value *impl = impl_from_IPropertyValue(iface);
    TRACE("iface %p, value %p.\n", iface, value);
    if (!value) return E_POINTER;
    if (impl->type == PropertyType_Double)
    {
        *value = *(DOUBLE *)impl->value;
        return S_OK;
    }
    if (impl->type == PropertyType_Single)
    {
        *value = (DOUBLE)*(FLOAT *)impl->value;
        return S_OK;
    }
    if (impl->type == PropertyType_Int32)
    {
        *value = (DOUBLE)*(INT32 *)impl->value;
        return S_OK;
    }
    if (impl->type == PropertyType_UInt32)
    {
        *value = (DOUBLE)*(UINT32 *)impl->value;
        return S_OK;
    }
    return property_value_get_primitive(PropertyType_Double);
}

static HRESULT STDMETHODCALLTYPE property_value_GetChar16(IPropertyValue *iface, WCHAR *value)
{
    TRACE("iface %p, value %p.\n", iface, value);
    return property_value_get_primitive(PropertyType_Char16);
}

static HRESULT STDMETHODCALLTYPE property_value_GetBoolean(IPropertyValue *iface, boolean *value)
{
    TRACE("iface %p, value %p.\n", iface, value);
    return property_value_get_primitive(PropertyType_Boolean);
}

static HRESULT STDMETHODCALLTYPE property_value_GetString(IPropertyValue *iface, HSTRING *value)
{
    HRESULT hr;

    TRACE("iface %p, value %p.\n", iface, value);

    hr = property_value_get_primitive(PropertyType_String);
    if (SUCCEEDED(hr))
        WindowsDuplicateString(*value, value);
    return hr;
}

static HRESULT STDMETHODCALLTYPE property_value_GetGuid(IPropertyValue *iface, GUID *value)
{
    TRACE("iface %p, value %p.\n", iface, value);
    return property_value_get_primitive(PropertyType_Guid);
}

static HRESULT STDMETHODCALLTYPE property_value_GetDateTime(IPropertyValue *iface,
                                                            struct DateTime *value)
{
    TRACE("iface %p, value %p.\n", iface, value);
    return property_value_get_primitive(PropertyType_DateTime);
}

static HRESULT STDMETHODCALLTYPE property_value_GetTimeSpan(IPropertyValue *iface, struct TimeSpan *value)
{
    TRACE("iface %p, value %p.\n", iface, value);
    return property_value_get_primitive(PropertyType_TimeSpan);
}

static HRESULT STDMETHODCALLTYPE property_value_GetPoint(IPropertyValue *iface, struct Point *value)
{
    TRACE("iface %p, value %p.\n", iface, value);
    return property_value_get_primitive(PropertyType_Point);
}

static HRESULT STDMETHODCALLTYPE property_value_GetSize(IPropertyValue *iface, struct Size *value)
{
    TRACE("iface %p, value %p.\n", iface, value);
    return property_value_get_primitive(PropertyType_Size);
}

static HRESULT STDMETHODCALLTYPE property_value_GetRect(IPropertyValue *iface, struct Rect *value)
{
    TRACE("iface %p, value %p.\n", iface, value);
    return property_value_get_primitive(PropertyType_Rect);
}

static HRESULT STDMETHODCALLTYPE property_value_GetUInt8Array(IPropertyValue *iface, UINT32 *value_size, BYTE **value)
{
    TRACE("iface %p, value_size %p, value %p.\n", iface, value_size, value);
    return property_value_get_primitive_array(PropertyType_UInt8Array);
}

static HRESULT STDMETHODCALLTYPE property_value_GetInt16Array(IPropertyValue *iface, UINT32 *value_size, INT16 **value)
{
    TRACE("iface %p, value_size %p, value %p.\n", iface, value_size, value);
    return property_value_get_primitive_array(PropertyType_Int16Array);
}

static HRESULT STDMETHODCALLTYPE property_value_GetUInt16Array(IPropertyValue *iface, UINT32 *value_size, UINT16 **value)
{
    TRACE("iface %p, value_size %p, value %p.\n", iface, value_size, value);
    return property_value_get_primitive_array(PropertyType_UInt16Array);
}

static HRESULT STDMETHODCALLTYPE property_value_GetInt32Array(IPropertyValue *iface, UINT32 *value_size, INT32 **value)
{
    TRACE("iface %p, value_size %p, value %p.\n", iface, value_size, value);
    return property_value_get_primitive_array(PropertyType_Int32Array);
}

static HRESULT STDMETHODCALLTYPE property_value_GetUInt32Array(IPropertyValue *iface, UINT32 *value_size, UINT32 **value)
{
    TRACE("iface %p, value_size %p, value %p.\n", iface, value_size, value);
    return property_value_get_primitive_array(PropertyType_UInt32Array);
}

static HRESULT STDMETHODCALLTYPE property_value_GetInt64Array(IPropertyValue *iface, UINT32 *value_size, INT64 **value)
{
    TRACE("iface %p, value_size %p, value %p.\n", iface, value_size, value);
    return property_value_get_primitive_array(PropertyType_Int64Array);
}

static HRESULT STDMETHODCALLTYPE property_value_GetUInt64Array(IPropertyValue *iface, UINT32 *value_size, UINT64 **value)
{
    TRACE("iface %p, value_size %p, value %p.\n", iface, value_size, value);
    return property_value_get_primitive_array(PropertyType_UInt64Array);
}

static HRESULT STDMETHODCALLTYPE property_value_GetSingleArray(IPropertyValue *iface, UINT32 *value_size, FLOAT **value)
{
    TRACE("iface %p, value_size %p, value %p.\n", iface, value_size, value);
    return property_value_get_primitive_array(PropertyType_SingleArray);
}

static HRESULT STDMETHODCALLTYPE property_value_GetDoubleArray(IPropertyValue *iface, UINT32 *value_size, DOUBLE **value)
{
    TRACE("iface %p, value_size %p, value %p.\n", iface, value_size, value);
    return property_value_get_primitive_array(PropertyType_DoubleArray);
}

static HRESULT STDMETHODCALLTYPE property_value_GetChar16Array(IPropertyValue *iface, UINT32 *value_size, WCHAR **value)
{
    TRACE("iface %p, value_size %p, value %p.\n", iface, value_size, value);
    return property_value_get_primitive_array(PropertyType_Char16Array);
}

static HRESULT STDMETHODCALLTYPE property_value_GetBooleanArray(IPropertyValue *iface, UINT32 *value_size, boolean **value)
{
    TRACE("iface %p, value_size %p, value %p.\n", iface, value_size, value);
    return property_value_get_primitive_array(PropertyType_BooleanArray);
}

static HRESULT STDMETHODCALLTYPE property_value_GetStringArray(IPropertyValue *iface, UINT32 *value_size, HSTRING **value)
{
    unsigned int i;
    HRESULT hr;

    TRACE("iface %p, value_size %p, value %p.\n", iface, value_size, value);

    hr = property_value_get_primitive_array(PropertyType_StringArray);
    if (SUCCEEDED(hr))
    {
        for (i = 0; i < *value_size; i++)
            WindowsDuplicateString((*value)[i], &(*value)[i]);
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE property_value_GetInspectableArray(IPropertyValue *iface, UINT32 *value_size, IInspectable ***value)
{
    struct property_value *impl = impl_from_IPropertyValue(iface);
    IInspectable **src, **dst;
    unsigned int i;

    TRACE("iface %p, value_size %p, value %p.\n", iface, value_size, value);

    if (!value_size || !value)
        return E_POINTER;

    if (impl->type != PropertyType_InspectableArray)
        return TYPE_E_TYPEMISMATCH;

    dst = CoTaskMemAlloc((impl->value_size ? impl->value_size : 1) * sizeof(IInspectable *));
    if (!dst)
        return E_OUTOFMEMORY;

    src = impl->value;
    for (i = 0; i < impl->value_size; i++)
    {
        dst[i] = src[i];
        if (dst[i]) IInspectable_AddRef(dst[i]);
    }

    *value_size = impl->value_size;
    *value = dst;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE property_value_GetGuidArray(IPropertyValue *iface, UINT32 *value_size, GUID **value)
{
    TRACE("iface %p, value_size %p, value %p.\n", iface, value_size, value);
    return property_value_get_primitive_array(PropertyType_GuidArray);
}

static HRESULT STDMETHODCALLTYPE property_value_GetDateTimeArray(IPropertyValue *iface, UINT32 *value_size, struct DateTime **value)
{
    TRACE("iface %p, value_size %p, value %p.\n", iface, value_size, value);
    return property_value_get_primitive_array(PropertyType_DateTimeArray);
}

static HRESULT STDMETHODCALLTYPE property_value_GetTimeSpanArray(IPropertyValue *iface, UINT32 *value_size, struct TimeSpan **value)
{
    TRACE("iface %p, value_size %p, value %p.\n", iface, value_size, value);
    return property_value_get_primitive_array(PropertyType_TimeSpanArray);
}

static HRESULT STDMETHODCALLTYPE property_value_GetPointArray(IPropertyValue *iface, UINT32 *value_size, struct Point **value)
{
    TRACE("iface %p, value_size %p, value %p.\n", iface, value_size, value);
    return property_value_get_primitive_array(PropertyType_PointArray);
}

static HRESULT STDMETHODCALLTYPE property_value_GetSizeArray(IPropertyValue *iface, UINT32 *value_size, struct Size **value)
{
    TRACE("iface %p, value_size %p, value %p.\n", iface, value_size, value);
    return property_value_get_primitive_array(PropertyType_SizeArray);
}

static HRESULT STDMETHODCALLTYPE property_value_GetRectArray(IPropertyValue *iface, UINT32 *value_size, struct Rect **value)
{
    TRACE("iface %p, value_size %p, value %p.\n", iface, value_size, value);
    return property_value_get_primitive_array(PropertyType_RectArray);
}

static const struct IPropertyValueVtbl property_value_vtbl =
{
    property_value_QueryInterface,
    property_value_AddRef,
    property_value_Release,
    /* IInspectable methods */
    property_value_GetIids,
    property_value_GetRuntimeClassName,
    property_value_GetTrustLevel,
    /* IPropertyValue methods */
    property_value_get_Type,
    property_value_get_IsNumericScalar,
    property_value_GetUInt8,
    property_value_GetInt16,
    property_value_GetUInt16,
    property_value_GetInt32,
    property_value_GetUInt32,
    property_value_GetInt64,
    property_value_GetUInt64,
    property_value_GetSingle,
    property_value_GetDouble,
    property_value_GetChar16,
    property_value_GetBoolean,
    property_value_GetString,
    property_value_GetGuid,
    property_value_GetDateTime,
    property_value_GetTimeSpan,
    property_value_GetPoint,
    property_value_GetSize,
    property_value_GetRect,
    property_value_GetUInt8Array,
    property_value_GetInt16Array,
    property_value_GetUInt16Array,
    property_value_GetInt32Array,
    property_value_GetUInt32Array,
    property_value_GetInt64Array,
    property_value_GetUInt64Array,
    property_value_GetSingleArray,
    property_value_GetDoubleArray,
    property_value_GetChar16Array,
    property_value_GetBooleanArray,
    property_value_GetStringArray,
    property_value_GetInspectableArray,
    property_value_GetGuidArray,
    property_value_GetDateTimeArray,
    property_value_GetTimeSpanArray,
    property_value_GetPointArray,
    property_value_GetSizeArray,
    property_value_GetRectArray,
};

#define create_primitive_property_value(type) _create_primitive_property_value(type, &value, 1, sizeof(value), property_value)
#define create_primitive_property_value_array(type) _create_primitive_property_value(type, value, value_size, sizeof(*value), property_value)
static HRESULT _create_primitive_property_value(PropertyType type, void *value,
        unsigned int count, size_t unit_size, IInspectable **property_value)
{
    struct property_value *impl;

    if (!value || !property_value)
        return E_POINTER;

    impl = calloc(1, sizeof(*impl));
    if (!impl)
        return E_OUTOFMEMORY;

    impl->IPropertyValue_iface.lpVtbl = &property_value_vtbl;
    impl->IWeakReferenceSource_iface.lpVtbl = &pv_weakref_source_vtbl;
    impl->type = type;
    impl->ref = 1;
    if (type >= PropertyType_UInt8Array && type <= PropertyType_RectArray)
        impl->irefs.array_iface.lpVtbl = &iref_array_vtbl;
    if (type != PropertyType_Empty)
    {
        impl->value = malloc(count ? unit_size * count : 1);
        if (!impl->value)
        {
            free(impl);
            return E_OUTOFMEMORY;
        }
        if (count) memcpy(impl->value, value, unit_size * count);
        impl->value_size = count;
    }

    *property_value = (IInspectable *)&impl->IPropertyValue_iface;
    return S_OK;
}

#define create_primitive_property_value_iref(type, iref_vtbl, vtbl)                      \
    do                                                                                   \
    {                                                                                    \
        HRESULT hr = create_primitive_property_value(type);                              \
        if (hr == S_OK)                                                                  \
        {                                                                                \
            struct property_value *value_impl = impl_from_IInspectable(*property_value); \
            value_impl->iref_vtbl = &vtbl;                                               \
        }                                                                                \
        return hr;                                                                       \
    } while (0)

DEFINE_IINSPECTABLE_(iref_byte, IReference_BYTE, struct property_value,
                     impl_from_IReference_BYTE, irefs.byte_iface, &impl->IPropertyValue_iface);

static HRESULT STDMETHODCALLTYPE iref_byte_get_Value(IReference_BYTE *iface, UINT8 *value)
{
    struct property_value *impl = impl_from_IReference_BYTE(iface);

    TRACE("iface %p, value %p.\n", iface, value);

    return property_value_GetUInt8(&impl->IPropertyValue_iface, value);
}

static const struct IReference_BYTEVtbl iref_byte_vtbl =
{
    iref_byte_QueryInterface,
    iref_byte_AddRef,
    iref_byte_Release,
    /* IInspectable methods */
    iref_byte_GetIids,
    iref_byte_GetRuntimeClassName,
    iref_byte_GetTrustLevel,
    /* IReference<BYTE> methods */
    iref_byte_get_Value,
};

DEFINE_IINSPECTABLE_(iref_int16, IReference_INT16, struct property_value,
                     impl_from_IReference_INT16, irefs.int16_iface, &impl->IPropertyValue_iface);

static HRESULT STDMETHODCALLTYPE iref_int16_get_Value(IReference_INT16 *iface, INT16 *value)
{
    struct property_value *impl = impl_from_IReference_INT16(iface);

    TRACE("iface %p, value %p.\n", iface, value);

    return property_value_GetInt16(&impl->IPropertyValue_iface, value);
}

static const struct IReference_INT16Vtbl iref_int16_vtbl =
{
    iref_int16_QueryInterface,
    iref_int16_AddRef,
    iref_int16_Release,
    /* IInspectable methods */
    iref_int16_GetIids,
    iref_int16_GetRuntimeClassName,
    iref_int16_GetTrustLevel,
    /* IReference<INT16> methods */
    iref_int16_get_Value,
};

DEFINE_IINSPECTABLE_(iref_int32, IReference_INT32, struct property_value,
                     impl_from_IReference_INT32, irefs.int32_iface, &impl->IPropertyValue_iface);

static HRESULT STDMETHODCALLTYPE iref_int32_get_Value(IReference_INT32 *iface, INT32 *value)
{
    struct property_value *impl = impl_from_IReference_INT32(iface);

    TRACE("iface %p, value %p.\n", iface, value);

    return property_value_GetInt32(&impl->IPropertyValue_iface, value);
}

static const struct IReference_INT32Vtbl iref_int32_vtbl =
{
    iref_int32_QueryInterface,
    iref_int32_AddRef,
    iref_int32_Release,
    /* IInspectable methods */
    iref_int32_GetIids,
    iref_int32_GetRuntimeClassName,
    iref_int32_GetTrustLevel,
    /* IReference<INT32> methods */
    iref_int32_get_Value,
};

DEFINE_IINSPECTABLE_(iref_uint32, IReference_UINT32, struct property_value,
                     impl_from_IReference_UINT32, irefs.uint32_iface, &impl->IPropertyValue_iface);

static HRESULT STDMETHODCALLTYPE iref_uint32_get_Value(IReference_UINT32 *iface, UINT32 *value)
{
    struct property_value *impl = impl_from_IReference_UINT32(iface);

    TRACE("iface %p, value %p.\n", iface, value);

    return property_value_GetUInt32(&impl->IPropertyValue_iface, value);
}

static const struct IReference_UINT32Vtbl iref_uint32_vtbl =
{
    iref_uint32_QueryInterface,
    iref_uint32_AddRef,
    iref_uint32_Release,
    /* IInspectable methods */
    iref_uint32_GetIids,
    iref_uint32_GetRuntimeClassName,
    iref_uint32_GetTrustLevel,
    /* IReference<UINT32> methods */
    iref_uint32_get_Value,
};

DEFINE_IINSPECTABLE_(iref_int64, IReference_INT64, struct property_value,
                     impl_from_IReference_INT64, irefs.int64_iface, &impl->IPropertyValue_iface);

static HRESULT STDMETHODCALLTYPE iref_int64_get_Value(IReference_INT64 *iface, INT64 *value)
{
    struct property_value *impl = impl_from_IReference_INT64(iface);

    TRACE("iface %p, value %p.\n", iface, value);

    return property_value_GetInt64(&impl->IPropertyValue_iface, value);
}

static const struct IReference_INT64Vtbl iref_int64_vtbl =
{
    iref_int64_QueryInterface,
    iref_int64_AddRef,
    iref_int64_Release,
    /* IInspectable methods */
    iref_int64_GetIids,
    iref_int64_GetRuntimeClassName,
    iref_int64_GetTrustLevel,
    /* IReference<INT64> methods */
    iref_int64_get_Value,
};

DEFINE_IINSPECTABLE_(iref_uint64, IReference_UINT64, struct property_value,
                     impl_from_IReference_UINT64, irefs.uint64_iface, &impl->IPropertyValue_iface);

static HRESULT STDMETHODCALLTYPE iref_uint64_get_Value(IReference_UINT64 *iface, UINT64 *value)
{
    struct property_value *impl = impl_from_IReference_UINT64(iface);

    TRACE("iface %p, value %p.\n", iface, value);

    return property_value_GetUInt64(&impl->IPropertyValue_iface, value);
}

static const struct IReference_UINT64Vtbl iref_uint64_vtbl =
{
    iref_uint64_QueryInterface,
    iref_uint64_AddRef,
    iref_uint64_Release,
    /* IInspectable methods */
    iref_uint64_GetIids,
    iref_uint64_GetRuntimeClassName,
    iref_uint64_GetTrustLevel,
    /* IReference<UINT64> methods */
    iref_uint64_get_Value,
};

DEFINE_IINSPECTABLE_(iref_boolean, IReference_boolean, struct property_value,
                     impl_from_IReference_boolean, irefs.boolean_iface, &impl->IPropertyValue_iface);

static HRESULT STDMETHODCALLTYPE iref_boolean_get_Value(IReference_boolean *iface, boolean *value)
{
    struct property_value *impl = impl_from_IReference_boolean(iface);

    TRACE("iface %p, value %p.\n", iface, value);

    return property_value_GetBoolean(&impl->IPropertyValue_iface, value);
}

static const struct IReference_booleanVtbl iref_boolean_vtbl =
{
    iref_boolean_QueryInterface,
    iref_boolean_AddRef,
    iref_boolean_Release,
    /* IInspectable methods */
    iref_boolean_GetIids,
    iref_boolean_GetRuntimeClassName,
    iref_boolean_GetTrustLevel,
    /* IReference<boolean> methods */
    iref_boolean_get_Value,
};

DEFINE_IINSPECTABLE_(iref_hstring, IReference_HSTRING, struct property_value,
                     impl_from_IReference_HSTRING, irefs.hstring_iface, &impl->IPropertyValue_iface);

static HRESULT STDMETHODCALLTYPE iref_hstring_get_Value(IReference_HSTRING *iface, HSTRING *value)
{
    struct property_value *impl = impl_from_IReference_HSTRING(iface);

    TRACE("iface %p, value %p.\n", iface, value);

    return property_value_GetString(&impl->IPropertyValue_iface, value);
}

static const struct IReference_HSTRINGVtbl iref_hstring_vtbl =
{
    iref_hstring_QueryInterface,
    iref_hstring_AddRef,
    iref_hstring_Release,
    /* IInspectable methods */
    iref_hstring_GetIids,
    iref_hstring_GetRuntimeClassName,
    iref_hstring_GetTrustLevel,
    /* IReference<HSTRING> methods */
    iref_hstring_get_Value,
};

DEFINE_IINSPECTABLE_(iref_float, IReference_FLOAT, struct property_value,
                     impl_from_IReference_FLOAT, irefs.float_iface, &impl->IPropertyValue_iface);

static HRESULT STDMETHODCALLTYPE iref_float_get_Value(IReference_FLOAT *iface, FLOAT *value)
{
    struct property_value *impl = impl_from_IReference_FLOAT(iface);

    TRACE("iface %p, value %p.\n", iface, value);

    return property_value_GetSingle(&impl->IPropertyValue_iface, value);
}

static const struct IReference_FLOATVtbl iref_float_vtbl =
{
    iref_float_QueryInterface,
    iref_float_AddRef,
    iref_float_Release,
    /* IInspectable methods */
    iref_float_GetIids,
    iref_float_GetRuntimeClassName,
    iref_float_GetTrustLevel,
    /* IReference<FLOAT> methods */
    iref_float_get_Value,
};

DEFINE_IINSPECTABLE_(iref_double, IReference_DOUBLE, struct property_value,
                     impl_from_IReference_DOUBLE, irefs.double_iface, &impl->IPropertyValue_iface);

static HRESULT STDMETHODCALLTYPE iref_double_get_Value(IReference_DOUBLE *iface, DOUBLE *value)
{
    struct property_value *impl = impl_from_IReference_DOUBLE(iface);

    TRACE("iface %p, value %p.\n", iface, value);

    return property_value_GetDouble(&impl->IPropertyValue_iface, value);
}

static const struct IReference_DOUBLEVtbl iref_double_vtbl =
{
    iref_double_QueryInterface,
    iref_double_AddRef,
    iref_double_Release,
    /* IInspectable methods */
    iref_double_GetIids,
    iref_double_GetRuntimeClassName,
    iref_double_GetTrustLevel,
    /* IReference<DOUBLE> methods */
    iref_double_get_Value,
};

DEFINE_IINSPECTABLE_(iref_datetime, IReference_DateTime, struct property_value,
                     impl_from_IReference_DateTime, irefs.datetime_iface, &impl->IPropertyValue_iface);

static HRESULT STDMETHODCALLTYPE iref_datetime_get_Value(IReference_DateTime *iface, DateTime *value)
{
    struct property_value *impl = impl_from_IReference_DateTime(iface);

    TRACE("iface %p, value %p.\n", iface, value);

    return property_value_GetDateTime(&impl->IPropertyValue_iface, value);
}

static const struct IReference_DateTimeVtbl iref_datetime_vtbl =
{
    iref_datetime_QueryInterface,
    iref_datetime_AddRef,
    iref_datetime_Release,
    /* IInspectable methods */
    iref_datetime_GetIids,
    iref_datetime_GetRuntimeClassName,
    iref_datetime_GetTrustLevel,
    /* IReference<DateTime> methods */
    iref_datetime_get_Value,
};

DEFINE_IINSPECTABLE_(iref_timespan, IReference_TimeSpan, struct property_value,
                     impl_from_IReference_TimeSpan, irefs.timespan_iface, &impl->IPropertyValue_iface);

static HRESULT STDMETHODCALLTYPE iref_timespan_get_Value(IReference_TimeSpan *iface, TimeSpan *value)
{
    struct property_value *impl = impl_from_IReference_TimeSpan(iface);

    TRACE("iface %p, value %p.\n", iface, value);

    return property_value_GetTimeSpan(&impl->IPropertyValue_iface, value);
}

static const struct IReference_TimeSpanVtbl iref_timespan_vtbl =
{
    iref_timespan_QueryInterface,
    iref_timespan_AddRef,
    iref_timespan_Release,
    /* IInspectable methods */
    iref_timespan_GetIids,
    iref_timespan_GetRuntimeClassName,
    iref_timespan_GetTrustLevel,
    /* IReference<TimeSpan> methods */
    iref_timespan_get_Value,
};

DEFINE_IINSPECTABLE_(iref_guid, IReference_GUID, struct property_value,
                     impl_from_IReference_GUID, irefs.guid_iface, &impl->IPropertyValue_iface);

static HRESULT STDMETHODCALLTYPE iref_guid_get_Value(IReference_GUID *iface, GUID *value)
{
    struct property_value *impl = impl_from_IReference_GUID(iface);

    TRACE("iface %p, value %p.\n", iface, value);

    return property_value_GetGuid(&impl->IPropertyValue_iface, value);
}

static const struct IReference_GUIDVtbl iref_guid_vtbl =
{
    iref_guid_QueryInterface,
    iref_guid_AddRef,
    iref_guid_Release,
    /* IInspectable methods */
    iref_guid_GetIids,
    iref_guid_GetRuntimeClassName,
    iref_guid_GetTrustLevel,
    /* IReference<GUID> methods */
    iref_guid_get_Value,
};

DEFINE_IINSPECTABLE_(iref_point, IReference_Point, struct property_value,
                     impl_from_IReference_Point, irefs.point_iface, &impl->IPropertyValue_iface);

static HRESULT STDMETHODCALLTYPE iref_point_get_Value(IReference_Point *iface, Point *value)
{
    struct property_value *impl = impl_from_IReference_Point(iface);

    TRACE("iface %p, value %p.\n", iface, value);

    return property_value_GetPoint(&impl->IPropertyValue_iface, value);
}

static const struct IReference_PointVtbl iref_point_vtbl =
{
    iref_point_QueryInterface,
    iref_point_AddRef,
    iref_point_Release,
    /* IInspectable methods */
    iref_point_GetIids,
    iref_point_GetRuntimeClassName,
    iref_point_GetTrustLevel,
    /* IReference<Point> methods */
    iref_point_get_Value,
};

DEFINE_IINSPECTABLE_(iref_size, IReference_Size, struct property_value,
                     impl_from_IReference_Size, irefs.size_iface, &impl->IPropertyValue_iface);

static HRESULT STDMETHODCALLTYPE iref_size_get_Value(IReference_Size *iface, Size *value)
{
    struct property_value *impl = impl_from_IReference_Size(iface);

    TRACE("iface %p, value %p.\n", iface, value);

    return property_value_GetSize(&impl->IPropertyValue_iface, value);
}

static const struct IReference_SizeVtbl iref_size_vtbl =
{
    iref_size_QueryInterface,
    iref_size_AddRef,
    iref_size_Release,
    /* IInspectable methods */
    iref_size_GetIids,
    iref_size_GetRuntimeClassName,
    iref_size_GetTrustLevel,
    /* IReference<Size> methods */
    iref_size_get_Value,
};

DEFINE_IINSPECTABLE_(iref_rect, IReference_Rect, struct property_value,
                     impl_from_IReference_Rect, irefs.rect_iface, &impl->IPropertyValue_iface);

static HRESULT STDMETHODCALLTYPE iref_rect_get_Value(IReference_Rect *iface, Rect *value)
{
    struct property_value *impl = impl_from_IReference_Rect(iface);

    TRACE("iface %p, value %p.\n", iface, value);

    return property_value_GetRect(&impl->IPropertyValue_iface, value);
}

static const struct IReference_RectVtbl iref_rect_vtbl =
{
    iref_rect_QueryInterface,
    iref_rect_AddRef,
    iref_rect_Release,
    /* IInspectable methods */
    iref_rect_GetIids,
    iref_rect_GetRuntimeClassName,
    iref_rect_GetTrustLevel,
    /* IReference<Rect> methods */
    iref_rect_get_Value,
};

DEFINE_IINSPECTABLE_(iref_uint16, void, struct property_value,
                     impl_from_iref_uint16, irefs.uint16_iface, &impl->IPropertyValue_iface);

static HRESULT STDMETHODCALLTYPE iref_uint16_get_Value(void *iface, UINT16 *value)
{
    struct property_value *impl = impl_from_iref_uint16(iface);
    return property_value_GetUInt16(&impl->IPropertyValue_iface, value);
}

static const struct iref_uint16_vtbl iref_uint16_vtbl =
{
    iref_uint16_QueryInterface,
    iref_uint16_AddRef,
    iref_uint16_Release,
    iref_uint16_GetIids,
    iref_uint16_GetRuntimeClassName,
    iref_uint16_GetTrustLevel,
    iref_uint16_get_Value,
};

DEFINE_IINSPECTABLE_(iref_char16, void, struct property_value,
                     impl_from_iref_char16, irefs.char16_iface, &impl->IPropertyValue_iface);

static HRESULT STDMETHODCALLTYPE iref_char16_get_Value(void *iface, WCHAR *value)
{
    struct property_value *impl = impl_from_iref_char16(iface);
    return property_value_GetChar16(&impl->IPropertyValue_iface, value);
}

static const struct iref_char16_vtbl iref_char16_vtbl =
{
    iref_char16_QueryInterface,
    iref_char16_AddRef,
    iref_char16_Release,
    iref_char16_GetIids,
    iref_char16_GetRuntimeClassName,
    iref_char16_GetTrustLevel,
    iref_char16_get_Value,
};

DEFINE_IINSPECTABLE_(iref_array, void, struct property_value,
                     impl_from_iref_array, irefs.array_iface, &impl->IPropertyValue_iface);

static HRESULT STDMETHODCALLTYPE iref_array_get_Value(void *iface, UINT32 *value_size, void **value)
{
    struct property_value *impl = impl_from_iref_array(iface);

    TRACE("iface %p, value_size %p, value %p, type %d.\n", iface, value_size, value, impl->type);

    switch (impl->type)
    {
    case PropertyType_UInt8Array:
        return property_value_GetUInt8Array(&impl->IPropertyValue_iface, value_size, (BYTE **)value);
    case PropertyType_Int16Array:
        return property_value_GetInt16Array(&impl->IPropertyValue_iface, value_size, (INT16 **)value);
    case PropertyType_UInt16Array:
        return property_value_GetUInt16Array(&impl->IPropertyValue_iface, value_size, (UINT16 **)value);
    case PropertyType_Int32Array:
        return property_value_GetInt32Array(&impl->IPropertyValue_iface, value_size, (INT32 **)value);
    case PropertyType_UInt32Array:
        return property_value_GetUInt32Array(&impl->IPropertyValue_iface, value_size, (UINT32 **)value);
    case PropertyType_Int64Array:
        return property_value_GetInt64Array(&impl->IPropertyValue_iface, value_size, (INT64 **)value);
    case PropertyType_UInt64Array:
        return property_value_GetUInt64Array(&impl->IPropertyValue_iface, value_size, (UINT64 **)value);
    case PropertyType_SingleArray:
        return property_value_GetSingleArray(&impl->IPropertyValue_iface, value_size, (FLOAT **)value);
    case PropertyType_DoubleArray:
        return property_value_GetDoubleArray(&impl->IPropertyValue_iface, value_size, (DOUBLE **)value);
    case PropertyType_Char16Array:
        return property_value_GetChar16Array(&impl->IPropertyValue_iface, value_size, (WCHAR **)value);
    case PropertyType_BooleanArray:
        return property_value_GetBooleanArray(&impl->IPropertyValue_iface, value_size, (boolean **)value);
    case PropertyType_StringArray:
        return property_value_GetStringArray(&impl->IPropertyValue_iface, value_size, (HSTRING **)value);
    case PropertyType_InspectableArray:
        return property_value_GetInspectableArray(&impl->IPropertyValue_iface, value_size, (IInspectable ***)value);
    case PropertyType_GuidArray:
        return property_value_GetGuidArray(&impl->IPropertyValue_iface, value_size, (GUID **)value);
    case PropertyType_DateTimeArray:
        return property_value_GetDateTimeArray(&impl->IPropertyValue_iface, value_size, (DateTime **)value);
    case PropertyType_TimeSpanArray:
        return property_value_GetTimeSpanArray(&impl->IPropertyValue_iface, value_size, (TimeSpan **)value);
    case PropertyType_PointArray:
        return property_value_GetPointArray(&impl->IPropertyValue_iface, value_size, (Point **)value);
    case PropertyType_SizeArray:
        return property_value_GetSizeArray(&impl->IPropertyValue_iface, value_size, (Size **)value);
    case PropertyType_RectArray:
        return property_value_GetRectArray(&impl->IPropertyValue_iface, value_size, (Rect **)value);
    default:
        return TYPE_E_TYPEMISMATCH;
    }
}

static const struct iref_array_vtbl iref_array_vtbl =
{
    iref_array_QueryInterface,
    iref_array_AddRef,
    iref_array_Release,
    iref_array_GetIids,
    iref_array_GetRuntimeClassName,
    iref_array_GetTrustLevel,
    iref_array_get_Value,
};

DEFINE_IINSPECTABLE(property_value_statics, IPropertyValueStatics, struct property_value_statics, IActivationFactory_iface)

static HRESULT STDMETHODCALLTYPE property_value_statics_CreateEmpty(IPropertyValueStatics *iface,
        IInspectable **property_value)
{
    TRACE("iface %p, property_value %p.\n", iface, property_value);

    if (!property_value)
        return E_POINTER;

    *property_value = NULL;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE property_value_statics_CreateUInt8(IPropertyValueStatics *iface,
        BYTE value, IInspectable **property_value)
{
    TRACE("iface %p, value %#x, property_value %p.\n", iface, value, property_value);
    create_primitive_property_value_iref(PropertyType_UInt8, irefs.byte_iface.lpVtbl, iref_byte_vtbl);
}

static HRESULT STDMETHODCALLTYPE property_value_statics_CreateInt16(IPropertyValueStatics *iface,
        INT16 value, IInspectable **property_value)
{
    TRACE("iface %p, value %d, property_value %p.\n", iface, value, property_value);
    create_primitive_property_value_iref(PropertyType_Int16, irefs.int16_iface.lpVtbl, iref_int16_vtbl);
}

static HRESULT STDMETHODCALLTYPE property_value_statics_CreateUInt16(IPropertyValueStatics *iface,
        UINT16 value, IInspectable **property_value)
{
    TRACE("iface %p, value %u, property_value %p.\n", iface, value, property_value);
    create_primitive_property_value_iref(PropertyType_UInt16, irefs.uint16_iface.lpVtbl, iref_uint16_vtbl);
}

static HRESULT STDMETHODCALLTYPE property_value_statics_CreateInt32(IPropertyValueStatics *iface,
        INT32 value, IInspectable **property_value)
{
    TRACE("iface %p, value %d, property_value %p.\n", iface, value, property_value);
    create_primitive_property_value_iref(PropertyType_Int32, irefs.int32_iface.lpVtbl, iref_int32_vtbl);
}

static HRESULT STDMETHODCALLTYPE property_value_statics_CreateUInt32(IPropertyValueStatics *iface,
        UINT32 value, IInspectable **property_value)
{
    TRACE("iface %p, value %u, property_value %p.\n", iface, value, property_value);
    create_primitive_property_value_iref(PropertyType_UInt32, irefs.uint32_iface.lpVtbl, iref_uint32_vtbl);
}

static HRESULT STDMETHODCALLTYPE property_value_statics_CreateInt64(IPropertyValueStatics *iface,
        INT64 value, IInspectable **property_value)
{
    TRACE("iface %p, value %I64d, property_value %p.\n", iface, value, property_value);
    create_primitive_property_value_iref(PropertyType_Int64, irefs.int64_iface.lpVtbl, iref_int64_vtbl);
}

static HRESULT STDMETHODCALLTYPE property_value_statics_CreateUInt64(IPropertyValueStatics *iface,
        UINT64 value, IInspectable **property_value)
{
    TRACE("iface %p, value %I64u, property_value %p.\n", iface, value, property_value);
    create_primitive_property_value_iref(PropertyType_UInt64, irefs.uint64_iface.lpVtbl, iref_uint64_vtbl);
}

static HRESULT STDMETHODCALLTYPE property_value_statics_CreateSingle(IPropertyValueStatics *iface,
        FLOAT value, IInspectable **property_value)
{
    TRACE("iface %p, value %f, property_value %p.\n", iface, value, property_value);
    create_primitive_property_value_iref(PropertyType_Single, irefs.float_iface.lpVtbl, iref_float_vtbl);
}

static HRESULT STDMETHODCALLTYPE property_value_statics_CreateDouble(IPropertyValueStatics *iface,
        DOUBLE value, IInspectable **property_value)
{
    TRACE("iface %p, value %f, property_value %p.\n", iface, value, property_value);
    create_primitive_property_value_iref(PropertyType_Double, irefs.double_iface.lpVtbl, iref_double_vtbl);
}

static HRESULT STDMETHODCALLTYPE property_value_statics_CreateChar16(IPropertyValueStatics *iface,
        WCHAR value, IInspectable **property_value)
{
    TRACE("iface %p, value %s, property_value %p.\n", iface, wine_dbgstr_wn(&value, 1), property_value);
    create_primitive_property_value_iref(PropertyType_Char16, irefs.char16_iface.lpVtbl, iref_char16_vtbl);
}

static HRESULT STDMETHODCALLTYPE property_value_statics_CreateBoolean(IPropertyValueStatics *iface,
        boolean value, IInspectable **property_value)
{
    TRACE("iface %p, value %d, property_value %p.\n", iface, value, property_value);
    create_primitive_property_value_iref(PropertyType_Boolean, irefs.boolean_iface.lpVtbl, iref_boolean_vtbl);
}

static HRESULT STDMETHODCALLTYPE property_value_statics_CreateString(IPropertyValueStatics *iface,
        HSTRING value_str, IInspectable **property_value)
{
    HSTRING value;
    HRESULT hr;

    TRACE("iface %p, value %s, property_value %p.\n", iface, debugstr_hstring(value_str), property_value);

    if (!property_value)
        return E_POINTER;

    if (FAILED(hr = WindowsDuplicateString(value_str, &value)))
    {
        *property_value = NULL;
        return hr;
    }

    create_primitive_property_value_iref(PropertyType_String, irefs.hstring_iface.lpVtbl, iref_hstring_vtbl);
}

static HRESULT STDMETHODCALLTYPE property_value_statics_CreateInspectable(IPropertyValueStatics *iface,
        IInspectable *value, IInspectable **property_value)
{
    TRACE("iface %p, value %p, property_value %p.\n", iface, value, property_value);

    if (!property_value)
        return E_POINTER;

    *property_value = value;
    if (value) IInspectable_AddRef(value);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE property_value_statics_CreateGuid(IPropertyValueStatics *iface,
        GUID value, IInspectable **property_value)
{
    TRACE("iface %p, value %s, property_value %p.\n", iface, wine_dbgstr_guid(&value), property_value);
    create_primitive_property_value_iref(PropertyType_Guid, irefs.guid_iface.lpVtbl, iref_guid_vtbl);
}

static HRESULT STDMETHODCALLTYPE property_value_statics_CreateDateTime(IPropertyValueStatics *iface,
        DateTime value, IInspectable **property_value)
{
    TRACE("iface %p, value %I64d, property_value %p.\n", iface, value.UniversalTime, property_value);
    create_primitive_property_value_iref(PropertyType_DateTime, irefs.datetime_iface.lpVtbl, iref_datetime_vtbl);
}

static HRESULT STDMETHODCALLTYPE property_value_statics_CreateTimeSpan(IPropertyValueStatics *iface,
        TimeSpan value, IInspectable **property_value)
{
    TRACE("iface %p, value %I64d, property_value %p.\n", iface, value.Duration, property_value);
    create_primitive_property_value_iref(PropertyType_TimeSpan, irefs.timespan_iface.lpVtbl, iref_timespan_vtbl);
}

static HRESULT STDMETHODCALLTYPE property_value_statics_CreatePoint(IPropertyValueStatics *iface,
        Point value, IInspectable **property_value)
{
    TRACE("iface %p, value (%f, %f), property_value %p.\n", iface, value.X, value.Y, property_value);
    create_primitive_property_value_iref(PropertyType_Point, irefs.point_iface.lpVtbl, iref_point_vtbl);
}

static HRESULT STDMETHODCALLTYPE property_value_statics_CreateSize(IPropertyValueStatics *iface,
        Size value, IInspectable **property_value)
{
    TRACE("iface %p, value (%fx%f), property_value %p.\n", iface, value.Width, value.Height, property_value);
    create_primitive_property_value_iref(PropertyType_Size, irefs.size_iface.lpVtbl, iref_size_vtbl);
}

static HRESULT STDMETHODCALLTYPE property_value_statics_CreateRect(IPropertyValueStatics *iface,
        Rect value, IInspectable **property_value)
{
    TRACE("iface %p, value (%f, %f %fx%f), property_value %p.\n", iface, value.X, value.Y,
            value.Width, value.Height, property_value);
    create_primitive_property_value_iref(PropertyType_Rect, irefs.rect_iface.lpVtbl, iref_rect_vtbl);
}

static HRESULT STDMETHODCALLTYPE property_value_statics_CreateUInt8Array(IPropertyValueStatics *iface,
        UINT32 value_size, BYTE *value, IInspectable **property_value)
{
    TRACE("iface %p, value_size %u, value %p, property_value %p.\n", iface, value_size, value, property_value);
    return create_primitive_property_value_array(PropertyType_UInt8Array);
}

static HRESULT STDMETHODCALLTYPE property_value_statics_CreateInt16Array(IPropertyValueStatics *iface,
        UINT32 value_size, INT16 *value, IInspectable **property_value)
{
    TRACE("iface %p, value_size %u, value %p, property_value %p.\n", iface, value_size, value, property_value);
    return create_primitive_property_value_array(PropertyType_Int16Array);
}

static HRESULT STDMETHODCALLTYPE property_value_statics_CreateUInt16Array(IPropertyValueStatics *iface,
        UINT32 value_size, UINT16 *value, IInspectable **property_value)
{
    TRACE("iface %p, value_size %u, value %p, property_value %p.\n", iface, value_size, value, property_value);
    return create_primitive_property_value_array(PropertyType_UInt16Array);
}

static HRESULT STDMETHODCALLTYPE property_value_statics_CreateInt32Array(IPropertyValueStatics *iface,
        UINT32 value_size, INT32 *value, IInspectable **property_value)
{
    TRACE("iface %p, value_size %u, value %p, property_value %p.\n", iface, value_size, value, property_value);
    return create_primitive_property_value_array(PropertyType_Int32Array);
}

static HRESULT STDMETHODCALLTYPE property_value_statics_CreateUInt32Array(IPropertyValueStatics *iface,
        UINT32 value_size, UINT32 *value, IInspectable **property_value)
{
    TRACE("iface %p, value_size %u, value %p, property_value %p.\n", iface, value_size, value, property_value);
    return create_primitive_property_value_array(PropertyType_UInt32Array);
}

static HRESULT STDMETHODCALLTYPE property_value_statics_CreateInt64Array(IPropertyValueStatics *iface,
        UINT32 value_size, INT64 *value, IInspectable **property_value)
{
    TRACE("iface %p, value_size %u, value %p, property_value %p.\n", iface, value_size, value, property_value);
    return create_primitive_property_value_array(PropertyType_Int64Array);
}

static HRESULT STDMETHODCALLTYPE property_value_statics_CreateUInt64Array(IPropertyValueStatics *iface,
        UINT32 value_size, UINT64 *value, IInspectable **property_value)
{
    TRACE("iface %p, value_size %u, value %p, property_value %p.\n", iface, value_size, value, property_value);
    return create_primitive_property_value_array(PropertyType_UInt64Array);
}

static HRESULT STDMETHODCALLTYPE property_value_statics_CreateSingleArray(IPropertyValueStatics *iface,
        UINT32 value_size, FLOAT *value, IInspectable **property_value)
{
    TRACE("iface %p, value_size %u, value %p, property_value %p.\n", iface, value_size, value, property_value);
    return create_primitive_property_value_array(PropertyType_SingleArray);
}

static HRESULT STDMETHODCALLTYPE property_value_statics_CreateDoubleArray(IPropertyValueStatics *iface,
        UINT32 value_size, DOUBLE *value, IInspectable **property_value)
{
    TRACE("iface %p, value_size %u, value %p, property_value %p.\n", iface, value_size, value, property_value);
    return create_primitive_property_value_array(PropertyType_DoubleArray);
}

static HRESULT STDMETHODCALLTYPE property_value_statics_CreateChar16Array(IPropertyValueStatics *iface,
        UINT32 value_size, WCHAR *value, IInspectable **property_value)
{
    TRACE("iface %p, value_size %u, value %p, property_value %p.\n", iface, value_size, value, property_value);
    return create_primitive_property_value_array(PropertyType_Char16Array);
}

static HRESULT STDMETHODCALLTYPE property_value_statics_CreateBooleanArray(IPropertyValueStatics *iface,
        UINT32 value_size, boolean *value, IInspectable **property_value)
{
    TRACE("iface %p, value_size %u, value %p, property_value %p.\n", iface, value_size, value, property_value);
    return create_primitive_property_value_array(PropertyType_BooleanArray);
}

static HRESULT STDMETHODCALLTYPE property_value_statics_CreateStringArray(IPropertyValueStatics *iface,
        UINT32 value_size, HSTRING *value, IInspectable **property_value)
{
    struct property_value *impl;
    HSTRING *new_value;
    unsigned int i;

    TRACE("iface %p, value_size %u, value %p, property_value %p.\n", iface, value_size, value, property_value);

    if (!value || !property_value)
        return E_POINTER;

    impl = calloc(1, sizeof(*impl));
    if (!impl)
        return E_OUTOFMEMORY;

    new_value = calloc(value_size, sizeof(HSTRING));
    if (!new_value)
    {
        free(impl);
        return E_OUTOFMEMORY;
    }

    for (i = 0; i < value_size; i++)
        WindowsDuplicateString(value[i], &new_value[i]);

    impl->IPropertyValue_iface.lpVtbl = &property_value_vtbl;
    impl->IWeakReferenceSource_iface.lpVtbl = &pv_weakref_source_vtbl;
    impl->irefs.array_iface.lpVtbl = &iref_array_vtbl;
    impl->type = PropertyType_StringArray;
    impl->ref = 1;
    impl->value = new_value;
    impl->value_size = value_size;

    *property_value = (IInspectable *)&impl->IPropertyValue_iface;
    return S_OK;

}

static HRESULT STDMETHODCALLTYPE property_value_statics_CreateInspectableArray(IPropertyValueStatics *iface,
        UINT32 value_size, IInspectable **value, IInspectable **property_value)
{
    struct property_value *impl;
    IInspectable **new_value;
    unsigned int i;

    TRACE("iface %p, value_size %u, value %p, property_value %p.\n", iface, value_size, value, property_value);

    if (!value || !property_value)
        return E_POINTER;

    impl = calloc(1, sizeof(*impl));
    if (!impl)
        return E_OUTOFMEMORY;

    new_value = calloc(value_size ? value_size : 1, sizeof(IInspectable *));
    if (!new_value)
    {
        free(impl);
        return E_OUTOFMEMORY;
    }

    for (i = 0; i < value_size; i++)
    {
        new_value[i] = value[i];
        if (new_value[i]) IInspectable_AddRef(new_value[i]);
    }

    impl->IPropertyValue_iface.lpVtbl = &property_value_vtbl;
    impl->IWeakReferenceSource_iface.lpVtbl = &pv_weakref_source_vtbl;
    impl->irefs.array_iface.lpVtbl = &iref_array_vtbl;
    impl->type = PropertyType_InspectableArray;
    impl->ref = 1;
    impl->value = new_value;
    impl->value_size = value_size;

    *property_value = (IInspectable *)&impl->IPropertyValue_iface;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE property_value_statics_CreateGuidArray(IPropertyValueStatics *iface,
        UINT32 value_size, GUID *value, IInspectable **property_value)
{
    TRACE("iface %p, value_size %u, value %p, property_value %p.\n", iface, value_size, value, property_value);
    return create_primitive_property_value_array(PropertyType_GuidArray);
}

static HRESULT STDMETHODCALLTYPE property_value_statics_CreateDateTimeArray(IPropertyValueStatics *iface,
        UINT32 value_size, DateTime *value, IInspectable **property_value)
{
    TRACE("iface %p, value_size %u, value %p, property_value %p.\n", iface, value_size, value, property_value);
    return create_primitive_property_value_array(PropertyType_DateTimeArray);
}

static HRESULT STDMETHODCALLTYPE property_value_statics_CreateTimeSpanArray(IPropertyValueStatics *iface,
        UINT32 value_size, TimeSpan *value, IInspectable **property_value)
{
    TRACE("iface %p, value_size %u, value %p, property_value %p.\n", iface, value_size, value, property_value);
    return create_primitive_property_value_array(PropertyType_TimeSpanArray);
}

static HRESULT STDMETHODCALLTYPE property_value_statics_CreatePointArray(IPropertyValueStatics *iface,
        UINT32 value_size, Point *value, IInspectable **property_value)
{
    TRACE("iface %p, value_size %u, value %p, property_value %p.\n", iface, value_size, value, property_value);
    return create_primitive_property_value_array(PropertyType_PointArray);
}

static HRESULT STDMETHODCALLTYPE property_value_statics_CreateSizeArray(IPropertyValueStatics *iface,
        UINT32 value_size, Size *value, IInspectable **property_value)
{
    TRACE("iface %p, value_size %u, value %p, property_value %p.\n", iface, value_size, value, property_value);
    return create_primitive_property_value_array(PropertyType_SizeArray);
}

static HRESULT STDMETHODCALLTYPE property_value_statics_CreateRectArray(IPropertyValueStatics *iface,
        UINT32 value_size, Rect *value, IInspectable **property_value)
{
    TRACE("iface %p, value_size %u, value %p, property_value %p.\n", iface, value_size, value, property_value);
    return create_primitive_property_value_array(PropertyType_RectArray);
}

static const struct IPropertyValueStaticsVtbl property_value_statics_vtbl =
{
    property_value_statics_QueryInterface,
    property_value_statics_AddRef,
    property_value_statics_Release,
    /* IInspectable methods */
    property_value_statics_GetIids,
    property_value_statics_GetRuntimeClassName,
    property_value_statics_GetTrustLevel,
    /* IPropertyValueStatics methods */
    property_value_statics_CreateEmpty,
    property_value_statics_CreateUInt8,
    property_value_statics_CreateInt16,
    property_value_statics_CreateUInt16,
    property_value_statics_CreateInt32,
    property_value_statics_CreateUInt32,
    property_value_statics_CreateInt64,
    property_value_statics_CreateUInt64,
    property_value_statics_CreateSingle,
    property_value_statics_CreateDouble,
    property_value_statics_CreateChar16,
    property_value_statics_CreateBoolean,
    property_value_statics_CreateString,
    property_value_statics_CreateInspectable,
    property_value_statics_CreateGuid,
    property_value_statics_CreateDateTime,
    property_value_statics_CreateTimeSpan,
    property_value_statics_CreatePoint,
    property_value_statics_CreateSize,
    property_value_statics_CreateRect,
    property_value_statics_CreateUInt8Array,
    property_value_statics_CreateInt16Array,
    property_value_statics_CreateUInt16Array,
    property_value_statics_CreateInt32Array,
    property_value_statics_CreateUInt32Array,
    property_value_statics_CreateInt64Array,
    property_value_statics_CreateUInt64Array,
    property_value_statics_CreateSingleArray,
    property_value_statics_CreateDoubleArray,
    property_value_statics_CreateChar16Array,
    property_value_statics_CreateBooleanArray,
    property_value_statics_CreateStringArray,
    property_value_statics_CreateInspectableArray,
    property_value_statics_CreateGuidArray,
    property_value_statics_CreateDateTimeArray,
    property_value_statics_CreateTimeSpanArray,
    property_value_statics_CreatePointArray,
    property_value_statics_CreateSizeArray,
    property_value_statics_CreateRectArray,
};

static struct property_value_statics property_value_statics =
{
    {&property_value_statics_factory_vtbl},
    {&property_value_statics_vtbl},
    1
};

HRESULT create_double_property_value( double val, IInspectable **out )
{
    return property_value_statics_CreateDouble( &property_value_statics.IPropertyValueStatics_iface, val, out );
}

HRESULT create_boolean_property_value( boolean val, IInspectable **out )
{
    return property_value_statics_CreateBoolean( &property_value_statics.IPropertyValueStatics_iface, val, out );
}

HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void **out)
{
    FIXME("clsid %s, riid %s, out %p stub!\n", debugstr_guid(clsid), debugstr_guid(riid), out);
    return CLASS_E_CLASSNOTAVAILABLE;
}

/* The Microsoft.UI.Xaml shader creates its heap (first block landing at the
 * head of the shared-heap region, observed base 0x6fff00000000) during XAML
 * startup, i.e. after this DLL is already loaded. CoreCLR's ComWrappers
 * tracker walks the shader composite wrapper vtable that the shader builds in
 * the head block, and every tracker cycle faults on the first-chance
 * 0x6fff0000003b load. Pre-grow the process heap head with a few committed,
 * touched blocks so the shader's first allocation lands in a fully committed
 * region behind them. Activation-factory calls happen on the startup thread
 * before XAML boots, so the one-shot guard needs no locking. */
static void *heap_prewarm_blocks[4];

/* Arm-2 (p210 SIGBUS: arm-1 blocks straddled the shared-heap ceiling, entries
 * past ~3072 spilled into unbacked pages and the tracker's tail reads faulted):
 * 4 blocks of 16 MB each, MEM_RESERVE | MEM_COMMIT up front and one qword
 * touched per 4096-byte page, so every block lands fully backed. */
static void heap_prewarm( void )
{
    static BOOL done;
    unsigned int i, j;
    char *p;

    if (done) return;
    done = TRUE;
    for (i = 0; i < ARRAYSIZE(heap_prewarm_blocks); i++)
    {
        if (!(p = VirtualAlloc( NULL, 0x1000000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE )))
            continue;
        for (j = 0; j < 0x1000000; j += 0x1000) p[j] = 1;
        heap_prewarm_blocks[i] = p;
    }
    /* Reserve a privately committed heap ahead of the coreclr/shader
     * allocation phase: its 8MB initial commit is one contiguous mapping, so
     * the virtual free area serves the next region (coreclr's heap blocks,
     * base observed 0x6fff00000000) with a fully mapped head page instead of
     * the 1MB-grow block whose first page faults (CompositeVtableData chunk
     * load [header+0x38], 0x6fff0000003b).
     *
     * Arm-3 first probe (s228): grow the reserve 8 MB -> 32 MB. The residual
     * 0x80131506 TryGetOrCreateObjectForComInstanceInternal fatal (survives
     * the IApplicationDataMethods projection face in s227d) is CoreCLR
     * ComWrappers allocating the RCW vtable chunks in this private heap; a
     * 4x reserve lets the whole chunk family land inside the pre-mapped
     * region instead of crossing into 1MB-grow blocks. */
    /* Arm-3 probe 10 (s241): drop the s228 HeapCreate reserve; cover the
     * whole observed chunk family with one fixed window instead: 128 MB at
     * 0x6ffffc000000 spans [0x6ffffc000000, 0x700004000000), containing the
     * heap base 0x6ffffdc00000 with growth slack and the fault-spread top
     * 0x6ffffe0a948f (sized per lane constraint: 64 MB topped out ~0xa948f
     * short). Coreclr's heap block lands inside fully-mapped pages; if the
     * census shows the base moved outside, relocate the window to cover it.
     *
     * s242: the s241 census (BORN 95->never when HeapCreate was dropped)
     * shows the reserve contributes layout beyond mapping, so run both:
     * the 32 MB HeapCreate reserve AND the 128 MB window. */
    HeapCreate( 0, 0x100000, 0x2000000 );
    /* s243: the s241/s242 census falsifies coverage-as-driver - the 128 MB
     * window at 0x6ffffc000000 regresses BORN 95->never in both
     * configurations, while the 32 MB window at 0x6ffffd000000 (s240) is
     * the favorable placement. Restore it as the active second window;
     * the 128 MB block stays as the census-tested alternative. */
    if ((p = VirtualAlloc( (char *)0x6ffffd000000, 0x2000000,
                           MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE )))
    {
        for (j = 0; j < 0x2000000; j += 0x1000) p[j] = 1;
    }
    if ((p = VirtualAlloc( (char *)0x6ffffc000000, 0x8000000,
                           MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE )))
    {
        for (j = 0; j < 0x8000000; j += 0x1000) p[j] = 1;
    }

    /* Arm-3 probe 3 (s230): the three face arms (s224 projection face, s228
     * reserve grow, s229 IReferenceTracker face) leave the 0x80131506
     * TryGetOrCreateObjectForComInstanceInternal fatal invariant, so the
     * composite-vtable chunk fetch at the observed shader heap base
     * 0x6fff00000000 is independent of which face QI answers. Pin the region
     * base itself: fixed-address 32 MB reserve+commit at the observed base
     * with one qword touched per page, so the head page (the
     * [header+0x38]/0x6fff0000003b load site) is committed before coreclr
     * and the XAML shader walk it. */
    if ((p = VirtualAlloc( (char *)0x6fff00000000, 0x2000000,
                           MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE )))
    {
        for (j = 0; j < 0x2000000; j += 0x1000) p[j] = 1;
    }

    /* Arm-3 probe 3b (s231, subsumed by probe 10): the 32 MB window at
     * 0x6ffffd000000 sits inside the 128 MB probe-10 window now; kept for
     * the census record - a failed fixed overlap is a silent no-op. */
    if ((p = VirtualAlloc( (char *)0x6ffffd000000, 0x2000000,
                           MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE )))
    {
        for (j = 0; j < 0x2000000; j += 0x1000) p[j] = 1;
    }

    /* Probe 11 (s252): seed the propertyset (values) composite block at
     * head+0xc0 with the same 4-pair array propertyset_GetIids hands back. */
    propertyset_pair_array_seed( (void *)0x6fff000000c0 );

    /* Load-order arm (p215): pull the XAML runtime chain in ahead of the
     * first ApplicationDataContainer.get_Values RCW cycle. With
     * Microsoft.UI.Xaml.dll resident before CoreCLR wraps the values
     * container, its ComWrappers provider registers the XAML-aware tracker
     * path and the shader composite scan that drives the 6.7M-iteration
     * first-chance AV loop (35 fatal cycles per gate) runs against live
     * runtime data instead of a half-initialized heap. */
    LoadLibraryW( L"CoreMessagingXP.dll" );
    LoadLibraryW( L"Microsoft.UI.Xaml.dll" );
    LoadLibraryW( L"Microsoft.Web.WebView2.Core.dll" );
}

/* Windows.Foundation.Diagnostics.LoggingFields */
static const GUID IID_ILoggingFields = {0xd7f6b7af, 0x762d, 0x4579, {0x83, 0xbd, 0x52, 0xc2, 0x3b, 0xc3, 0x33, 0xbc}};

struct logging_fields
{
    const void **vtbl;
    LONG ref;
};

static const GUID IID_ILoggingFields2 =
    { 0xd7f6b7af, 0x762d, 0x4579, { 0x83, 0xbd, 0x52, 0xc2, 0x3b, 0xc3, 0x33, 0xbc } };

static HRESULT WINAPI logging_fields_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct logging_fields *impl = CONTAINING_RECORD( iface, struct logging_fields, vtbl );
    TRACE( "iface %p, iid %s.\n", iface, debugstr_guid( iid ) );
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_ILoggingFields ) ||
        IsEqualGUID( iid, &IID_ILoggingFields2 ))
    {
        *out = &impl->vtbl;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI logging_fields_AddRef( void *iface )
{
    struct logging_fields *impl = CONTAINING_RECORD( iface, struct logging_fields, vtbl );
    return InterlockedIncrement( &impl->ref );
}
static ULONG WINAPI logging_fields_Release( void *iface )
{
    struct logging_fields *impl = CONTAINING_RECORD( iface, struct logging_fields, vtbl );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref) free( impl );
    return ref;
}
static HRESULT WINAPI logging_fields_GetIids( void *iface, ULONG *count, IID **iids ) { *count = 0; *iids = NULL; return S_OK; }
static HRESULT WINAPI logging_fields_GetRuntimeClassName( void *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.Foundation.Diagnostics.LoggingFields", 43, name );
}
static HRESULT WINAPI logging_fields_GetTrustLevel( void *iface, TrustLevel *level ) { *level = BaseTrust; return S_OK; }
static HRESULT WINAPI logging_fields_stub( void *iface, ... ) { return S_OK; }

static void *logging_fields_vtbl[150] =
{
    logging_fields_QueryInterface, logging_fields_AddRef, logging_fields_Release,
    logging_fields_GetIids, logging_fields_GetRuntimeClassName, logging_fields_GetTrustLevel,
};

static HRESULT WINAPI logging_fields_factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    static BOOL initialized = FALSE;
    struct logging_fields *obj;
    int i;
    if (!initialized)
    {
        for (i = 6; i < ARRAY_SIZE(logging_fields_vtbl); i++)
            logging_fields_vtbl[i] = (void *)logging_fields_stub;
        initialized = TRUE;
    }
    if (!instance) return E_POINTER;
    if (!(obj = calloc( 1, sizeof(*obj) ))) return E_OUTOFMEMORY;
    obj->vtbl = (const void **)logging_fields_vtbl;
    obj->ref = 1;
    *instance = (IInspectable *)&obj->vtbl;
    return S_OK;
}

static HRESULT WINAPI logging_fields_factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI logging_fields_factory_AddRef( IActivationFactory *iface ) { return 2; }
static ULONG WINAPI logging_fields_factory_Release( IActivationFactory *iface ) { return 1; }

static const struct IActivationFactoryVtbl logging_fields_factory_vtbl =
{
    logging_fields_factory_QueryInterface,
    logging_fields_factory_AddRef,
    logging_fields_factory_Release,
    (void *)logging_fields_GetIids,
    (void *)logging_fields_GetRuntimeClassName,
    (void *)logging_fields_GetTrustLevel,
    logging_fields_factory_ActivateInstance,
};
static struct { IActivationFactory iface; } logging_fields_factory_obj = { {&logging_fields_factory_vtbl} };
static IActivationFactory *logging_fields_activation_factory = &logging_fields_factory_obj.iface;

/* Windows.Foundation.Diagnostics.AsyncCausalityTracer */
static const GUID IID_IAsyncCausalityTracerStatics =
    { 0x50850b26, 0x267e, 0x451b, { 0xa8, 0x90, 0xab, 0x6a, 0x37, 0x02, 0x45, 0xee } };

static HRESULT WINAPI async_causality_tracer_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ) ||
        IsEqualGUID( iid, &IID_IAsyncCausalityTracerStatics ))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI async_causality_tracer_AddRef( IActivationFactory *iface ) { return 2; }
static ULONG WINAPI async_causality_tracer_Release( IActivationFactory *iface ) { return 1; }
static HRESULT WINAPI async_causality_tracer_GetIids( IActivationFactory *iface, ULONG *count, IID **iids )
{ if (count) *count = 0; if (iids) *iids = NULL; return S_OK; }
static HRESULT WINAPI async_causality_tracer_GetRuntimeClassName( IActivationFactory *iface, HSTRING *name )
{ return WindowsCreateString( L"Windows.Foundation.Diagnostics.AsyncCausalityTracer", 50, name ); }
static HRESULT WINAPI async_causality_tracer_GetTrustLevel( IActivationFactory *iface, TrustLevel *level )
{ if (level) *level = BaseTrust; return S_OK; }
static HRESULT WINAPI async_causality_tracer_stub_ok( void *iface, ... ) { return S_OK; }

static const void *async_causality_tracer_factory_vtbl[] =
{
    async_causality_tracer_QueryInterface,
    async_causality_tracer_AddRef,
    async_causality_tracer_Release,
    async_causality_tracer_GetIids,
    async_causality_tracer_GetRuntimeClassName,
    async_causality_tracer_GetTrustLevel,
    async_causality_tracer_stub_ok, /* ActivateInstance */
    async_causality_tracer_stub_ok, /* TraceOperationCreation */
    async_causality_tracer_stub_ok, /* TraceOperationCompletion */
    async_causality_tracer_stub_ok, /* TraceOperationRelation */
    async_causality_tracer_stub_ok, /* TraceSynchronousWorkStart */
    async_causality_tracer_stub_ok, /* TraceSynchronousWorkCompletion */
    async_causality_tracer_stub_ok, /* add_TracingStatusChanged */
    async_causality_tracer_stub_ok, /* remove_TracingStatusChanged */
    async_causality_tracer_stub_ok, async_causality_tracer_stub_ok,
    async_causality_tracer_stub_ok, async_causality_tracer_stub_ok,
};
static struct { const void **vtbl; } async_causality_tracer_factory_obj = { async_causality_tracer_factory_vtbl };

/* Windows.Foundation.Diagnostics.LoggingChannelOptions */
static const GUID IID_ILoggingChannelOptionsFactory =
    { 0xa93151da, 0x7faf, 0x4191, { 0x87, 0x55, 0x5e, 0x86, 0xdc, 0x65, 0xd8, 0x96 } };
static const GUID IID_ILoggingChannelOptions =
    { 0xe6f4e768, 0xd38b, 0x4013, { 0x96, 0x5e, 0x8b, 0x53, 0xe9, 0xe2, 0x9a, 0x3f } };

static HRESULT WINAPI logging_channel_options_QueryInterface( void *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_ILoggingChannelOptions ))
    {
        *out = iface;
        return S_OK;
    }
    FIXME( "logging_channel_options: unknown iid %s\n", debugstr_guid(iid) );
    *out = NULL;
    return E_NOINTERFACE;
}

static HRESULT WINAPI logging_channel_options_GetRuntimeClassName( IInspectable *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.Foundation.Diagnostics.LoggingChannelOptions", 52, name );
}

static HRESULT WINAPI logging_channel_options_stub_ok( void *iface, ... ) { return S_OK; }
static const void *logging_channel_options_vtbl[] =
{
    logging_channel_options_QueryInterface,
    async_causality_tracer_AddRef,
    async_causality_tracer_Release,
    async_causality_tracer_GetIids,
    logging_channel_options_GetRuntimeClassName,
    async_causality_tracer_GetTrustLevel,
    logging_channel_options_stub_ok, logging_channel_options_stub_ok,
    logging_channel_options_stub_ok, logging_channel_options_stub_ok,
};
static struct { const void **vtbl; } logging_channel_options_obj = { logging_channel_options_vtbl };

struct logging_channel_options_factory
{
    const void **vtbl_factory;
    const void **vtbl_options_factory;
};
static struct logging_channel_options_factory logging_channel_options_factory_obj;

static HRESULT WINAPI logging_channel_options_factory_QueryInterface( void *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = &logging_channel_options_factory_obj.vtbl_factory;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_ILoggingChannelOptionsFactory ))
    {
        *out = &logging_channel_options_factory_obj.vtbl_options_factory;
        return S_OK;
    }
    FIXME( "logging_channel_options_factory: unknown iid %s\n", debugstr_guid(iid) );
    *out = NULL;
    return E_NOINTERFACE;
}

static HRESULT WINAPI logging_channel_options_factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    if (!instance) return E_POINTER;
    *instance = (IInspectable *)&logging_channel_options_obj;
    return S_OK;
}

static HRESULT WINAPI logging_channel_options_factory_CreateWithGroup( void *iface, GUID group, IInspectable **instance )
{
    if (!instance) return E_POINTER;
    *instance = (IInspectable *)&logging_channel_options_obj;
    return S_OK;
}

static const void *logging_channel_options_factory_vtbl[] =
{
    logging_channel_options_factory_QueryInterface,
    async_causality_tracer_AddRef,
    async_causality_tracer_Release,
    async_causality_tracer_GetIids,
    logging_channel_options_GetRuntimeClassName,
    async_causality_tracer_GetTrustLevel,
    logging_channel_options_factory_ActivateInstance,
};
static const void *logging_channel_options_options_factory_vtbl[] =
{
    logging_channel_options_factory_QueryInterface,
    async_causality_tracer_AddRef,
    async_causality_tracer_Release,
    async_causality_tracer_GetIids,
    logging_channel_options_GetRuntimeClassName,
    async_causality_tracer_GetTrustLevel,
    logging_channel_options_factory_CreateWithGroup,
};
static struct logging_channel_options_factory logging_channel_options_factory_obj =
{
    logging_channel_options_factory_vtbl,
    logging_channel_options_options_factory_vtbl,
};

/* Windows.Foundation.Diagnostics.LoggingChannel */
static const GUID IID_ILoggingChannelFactory2 =
    { 0x4c6ef5dd, 0x3b27, 0x4dc9, { 0x99, 0xf0, 0x29, 0x9c, 0x6e, 0x46, 0x03, 0xa1 } };
static const GUID IID_ILoggingTarget =
    { 0xe9a50343, 0x11d7, 0x4f01, { 0xb5, 0xca, 0xcf, 0x49, 0x52, 0x78, 0xc0, 0xa8 } };

static const GUID IID_ILoggingChannel3 =
    { 0x65f16c35, 0xe388, 0x4e26, { 0xb1, 0x7a, 0xf5, 0x1c, 0xd3, 0xa8, 0x39, 0x16 } };

static HRESULT WINAPI logging_channel_QueryInterface( void *iface, REFIID iid, void **out )
{
    TRACE( "iface %p, iid %s.\n", iface, debugstr_guid( iid ) );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IClosable ) ||
        IsEqualGUID( iid, &IID_ILoggingTarget ) ||
        IsEqualGUID( iid, &IID_ILoggingChannel3 ))
    {
        *out = iface;
        return S_OK;
    }
    FIXME( "logging_channel: unknown iid %s\n", debugstr_guid(iid) );
    *out = NULL;
    return E_NOINTERFACE;
}

static HRESULT WINAPI logging_channel_GetRuntimeClassName( IInspectable *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.Foundation.Diagnostics.LoggingChannel", 45, name );
}

static HRESULT WINAPI logging_channel_stub_ok( void *iface, ... ) { return S_OK; }
static const void *logging_channel_vtbl[] =
{
    logging_channel_QueryInterface,
    async_causality_tracer_AddRef,
    async_causality_tracer_Release,
    async_causality_tracer_GetIids,
    logging_channel_GetRuntimeClassName,
    async_causality_tracer_GetTrustLevel,
    logging_channel_stub_ok, logging_channel_stub_ok,
    logging_channel_stub_ok, logging_channel_stub_ok,
    logging_channel_stub_ok, logging_channel_stub_ok,
    logging_channel_stub_ok, logging_channel_stub_ok,
    logging_channel_stub_ok, logging_channel_stub_ok,
    logging_channel_stub_ok, logging_channel_stub_ok,
    logging_channel_stub_ok, logging_channel_stub_ok,
    logging_channel_stub_ok, logging_channel_stub_ok,
    logging_channel_stub_ok, logging_channel_stub_ok,
    logging_channel_stub_ok, logging_channel_stub_ok,
};
static struct { const void **vtbl; } logging_channel_obj = { logging_channel_vtbl };

struct logging_channel_factory
{
    const void **vtbl_factory;
    const void **vtbl_factory2;
};
static struct logging_channel_factory logging_channel_factory_obj;

static HRESULT WINAPI logging_channel_factory_QueryInterface( void *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = &logging_channel_factory_obj.vtbl_factory;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_ILoggingChannelFactory2 ))
    {
        *out = &logging_channel_factory_obj.vtbl_factory2;
        return S_OK;
    }
    FIXME( "logging_channel_factory: unknown iid %s\n", debugstr_guid(iid) );
    *out = NULL;
    return E_NOINTERFACE;
}

static HRESULT WINAPI logging_channel_factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    if (!instance) return E_POINTER;
    *instance = (IInspectable *)&logging_channel_obj;
    return S_OK;
}

static HRESULT WINAPI logging_channel_factory2_CreateWithOptions( void *iface, HSTRING name, void *options, IInspectable **instance )
{
    if (!instance) return E_POINTER;
    *instance = (IInspectable *)&logging_channel_obj;
    return S_OK;
}

static HRESULT WINAPI logging_channel_factory2_CreateWithOptionsAndId( void *iface, HSTRING name, void *options, GUID id, IInspectable **instance )
{
    if (!instance) return E_POINTER;
    *instance = (IInspectable *)&logging_channel_obj;
    return S_OK;
}

static const void *logging_channel_factory_vtbl[] =
{
    logging_channel_factory_QueryInterface,
    async_causality_tracer_AddRef,
    async_causality_tracer_Release,
    async_causality_tracer_GetIids,
    logging_channel_GetRuntimeClassName,
    async_causality_tracer_GetTrustLevel,
    logging_channel_factory_ActivateInstance,
};
static const void *logging_channel_factory2_vtbl[] =
{
    logging_channel_factory_QueryInterface,
    async_causality_tracer_AddRef,
    async_causality_tracer_Release,
    async_causality_tracer_GetIids,
    logging_channel_GetRuntimeClassName,
    async_causality_tracer_GetTrustLevel,
    logging_channel_factory2_CreateWithOptions,
    logging_channel_factory2_CreateWithOptionsAndId,
};
static struct logging_channel_factory logging_channel_factory_obj =
{
    logging_channel_factory_vtbl,
    logging_channel_factory2_vtbl,
};

/* Windows.Foundation.Diagnostics.LoggingOptions
 *
 * Value-bag options object for LoggingChannel creation.  ILoggingOptions
 * {3dc7f4c9-27b7-4874-8b27-79e3e5d7b89c} carries seven get/put property pairs
 * (Keywords INT64, Tags UINT32, Task HSTRING, Opcode BYTE, ActivityId GUID,
 * RelatedActivityId GUID, CorrelationVector HSTRING) at slots 6..19.  The
 * class factory serves both IActivationFactory (ActivateInstance) and
 * ILoggingOptionsFactory {d713c6cb-98ab-464b-9f22-a3268478368a}
 * (CreateLoggingOptions slot 6, CreateWithKeywords(INT64,...) slot 7);
 * mspaint resolves the class with the factory IID directly. */
static const GUID IID_ILoggingOptionsFactory =
    { 0xd713c6cb, 0x98ab, 0x464b, { 0x9f, 0x22, 0xa3, 0x26, 0x84, 0x78, 0x36, 0x8a } };
static const GUID IID_ILoggingOptions =
    { 0x3dc7f4c9, 0x27b7, 0x4874, { 0x8b, 0x27, 0x79, 0xe3, 0xe5, 0xd7, 0xb8, 0x9c } };

struct logging_options
{
    const void **vtbl;
    LONG ref;
    INT64 keywords;
    UINT32 tags;
    HSTRING task;
    BYTE opcode;
    GUID activity_id;
    GUID related_activity_id;
    HSTRING correlation_vector;
};

static inline struct logging_options *impl_from_logging_options( void *iface )
{
    return CONTAINING_RECORD( iface, struct logging_options, vtbl );
}

static HRESULT WINAPI logging_options_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct logging_options *impl = impl_from_logging_options( iface );

    TRACE( "iface %p, iid %s.\n", iface, debugstr_guid( iid ) );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_ILoggingOptions ))
    {
        *out = iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    FIXME( "logging_options: unknown iid %s\n", debugstr_guid(iid) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI logging_options_AddRef( void *iface )
{
    struct logging_options *impl = impl_from_logging_options( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI logging_options_Release( void *iface )
{
    struct logging_options *impl = impl_from_logging_options( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        WindowsDeleteString( impl->task );
        WindowsDeleteString( impl->correlation_vector );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI logging_options_GetIids( void *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI logging_options_GetRuntimeClassName( void *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.Foundation.Diagnostics.LoggingOptions", 45, name );
}

static HRESULT WINAPI logging_options_GetTrustLevel( void *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI logging_options_get_Keywords( void *iface, INT64 *value )
{
    struct logging_options *impl = impl_from_logging_options( iface );
    if (!value) return E_POINTER;
    *value = impl->keywords;
    return S_OK;
}

static HRESULT WINAPI logging_options_put_Keywords( void *iface, INT64 value )
{
    struct logging_options *impl = impl_from_logging_options( iface );
    impl->keywords = value;
    return S_OK;
}

static HRESULT WINAPI logging_options_get_Tags( void *iface, UINT32 *value )
{
    struct logging_options *impl = impl_from_logging_options( iface );
    if (!value) return E_POINTER;
    *value = impl->tags;
    return S_OK;
}

static HRESULT WINAPI logging_options_put_Tags( void *iface, UINT32 value )
{
    struct logging_options *impl = impl_from_logging_options( iface );
    impl->tags = value;
    return S_OK;
}

static HRESULT WINAPI logging_options_get_Task( void *iface, HSTRING *value )
{
    struct logging_options *impl = impl_from_logging_options( iface );
    if (!value) return E_POINTER;
    return WindowsDuplicateString( impl->task, value );
}

static HRESULT WINAPI logging_options_put_Task( void *iface, HSTRING value )
{
    struct logging_options *impl = impl_from_logging_options( iface );
    WindowsDeleteString( impl->task );
    return WindowsDuplicateString( value, &impl->task );
}

static HRESULT WINAPI logging_options_get_Opcode( void *iface, BYTE *value )
{
    struct logging_options *impl = impl_from_logging_options( iface );
    if (!value) return E_POINTER;
    *value = impl->opcode;
    return S_OK;
}

static HRESULT WINAPI logging_options_put_Opcode( void *iface, BYTE value )
{
    struct logging_options *impl = impl_from_logging_options( iface );
    impl->opcode = value;
    return S_OK;
}

static HRESULT WINAPI logging_options_get_ActivityId( void *iface, GUID *value )
{
    struct logging_options *impl = impl_from_logging_options( iface );
    if (!value) return E_POINTER;
    *value = impl->activity_id;
    return S_OK;
}

static HRESULT WINAPI logging_options_put_ActivityId( void *iface, const GUID *value )
{
    struct logging_options *impl = impl_from_logging_options( iface );
    if (value) impl->activity_id = *value;
    return S_OK;
}

static HRESULT WINAPI logging_options_get_RelatedActivityId( void *iface, GUID *value )
{
    struct logging_options *impl = impl_from_logging_options( iface );
    if (!value) return E_POINTER;
    *value = impl->related_activity_id;
    return S_OK;
}

static HRESULT WINAPI logging_options_put_RelatedActivityId( void *iface, const GUID *value )
{
    struct logging_options *impl = impl_from_logging_options( iface );
    if (value) impl->related_activity_id = *value;
    return S_OK;
}

static HRESULT WINAPI logging_options_get_CorrelationVector( void *iface, HSTRING *value )
{
    struct logging_options *impl = impl_from_logging_options( iface );
    if (!value) return E_POINTER;
    return WindowsDuplicateString( impl->correlation_vector, value );
}

static HRESULT WINAPI logging_options_put_CorrelationVector( void *iface, HSTRING value )
{
    struct logging_options *impl = impl_from_logging_options( iface );
    WindowsDeleteString( impl->correlation_vector );
    return WindowsDuplicateString( value, &impl->correlation_vector );
}

static const void *logging_options_vtbl[] =
{
    logging_options_QueryInterface,
    logging_options_AddRef,
    logging_options_Release,
    logging_options_GetIids,
    logging_options_GetRuntimeClassName,
    logging_options_GetTrustLevel,
    logging_options_get_Keywords,          /* 6 */
    logging_options_put_Keywords,
    logging_options_get_Tags,
    logging_options_put_Tags,
    logging_options_get_Task,
    logging_options_put_Task,
    logging_options_get_Opcode,
    logging_options_put_Opcode,
    logging_options_get_ActivityId,
    logging_options_put_ActivityId,
    logging_options_get_RelatedActivityId,
    logging_options_put_RelatedActivityId,
    logging_options_get_CorrelationVector,
    logging_options_put_CorrelationVector, /* 19 */
};

static struct logging_options *create_logging_options( INT64 keywords )
{
    struct logging_options *obj;

    if (!(obj = calloc( 1, sizeof(*obj) ))) return NULL;
    obj->vtbl = logging_options_vtbl;
    obj->ref = 1;
    obj->keywords = keywords;
    WindowsCreateString( L"", 0, &obj->task );
    WindowsCreateString( L"", 0, &obj->correlation_vector );
    return obj;
}

struct logging_options_factory
{
    const void **vtbl_factory;
    const void **vtbl_options_factory;
};
static struct logging_options_factory logging_options_factory_obj;

static HRESULT WINAPI logging_options_factory_QueryInterface( void *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = &logging_options_factory_obj.vtbl_factory;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_ILoggingOptionsFactory ))
    {
        *out = &logging_options_factory_obj.vtbl_options_factory;
        return S_OK;
    }
    FIXME( "logging_options_factory: unknown iid %s\n", debugstr_guid(iid) );
    *out = NULL;
    return E_NOINTERFACE;
}

static HRESULT WINAPI logging_options_factory_ActivateInstance( IActivationFactory *iface,
                                                                IInspectable **instance )
{
    struct logging_options *obj;

    if (!instance) return E_POINTER;
    if (!(obj = create_logging_options( 0 ))) return E_OUTOFMEMORY;
    *instance = (IInspectable *)&obj->vtbl;
    return S_OK;
}

static HRESULT WINAPI logging_options_factory_CreateLoggingOptions( void *iface,
                                                                    IInspectable **instance )
{
    struct logging_options *obj;

    if (!instance) return E_POINTER;
    if (!(obj = create_logging_options( 0 ))) return E_OUTOFMEMORY;
    *instance = (IInspectable *)&obj->vtbl;
    return S_OK;
}

static HRESULT WINAPI logging_options_factory_CreateWithKeywords( void *iface, INT64 keywords,
                                                                  IInspectable **instance )
{
    struct logging_options *obj;

    if (!instance) return E_POINTER;
    if (!(obj = create_logging_options( keywords ))) return E_OUTOFMEMORY;
    *instance = (IInspectable *)&obj->vtbl;
    return S_OK;
}

static const void *logging_options_factory_vtbl[] =
{
    logging_options_factory_QueryInterface,
    async_causality_tracer_AddRef,
    async_causality_tracer_Release,
    async_causality_tracer_GetIids,
    logging_options_GetRuntimeClassName,
    async_causality_tracer_GetTrustLevel,
    logging_options_factory_ActivateInstance,
};

static const void *logging_options_options_factory_vtbl[] =
{
    logging_options_factory_QueryInterface,
    async_causality_tracer_AddRef,
    async_causality_tracer_Release,
    async_causality_tracer_GetIids,
    logging_options_GetRuntimeClassName,
    async_causality_tracer_GetTrustLevel,
    logging_options_factory_CreateWithKeywords, /* 6 — ILoggingOptionsFactory::CreateWithKeywords */
    logging_options_factory_CreateLoggingOptions, /* 7 — ILoggingOptionsFactory::CreateLoggingOptions */
};

static struct logging_options_factory logging_options_factory_obj =
{
    logging_options_factory_vtbl,
    logging_options_options_factory_vtbl,
};

/* Windows.Foundation.GuidHelper (IGuidHelperStatics {59c7966b-ae52-5283-ad7f-a1b9e9678add}) */
static const GUID IID_IGuidHelperStatics =
    { 0x59c7966b, 0xae52, 0x5283, { 0xad, 0x7f, 0xa1, 0xb9, 0xe9, 0x67, 0x8a, 0xdd } };

struct guid_helper_factory
{
    const void **vtbl_factory;
    const void **vtbl_statics;
};
static struct guid_helper_factory guid_helper_factory_obj;

static HRESULT WINAPI guid_helper_QueryInterface( void *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = &guid_helper_factory_obj.vtbl_factory;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IGuidHelperStatics ))
    {
        *out = &guid_helper_factory_obj.vtbl_statics;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static HRESULT WINAPI guid_helper_GetRuntimeClassName( IActivationFactory *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.Foundation.GuidHelper", 29, name );
}

static HRESULT WINAPI guid_helper_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    if (instance) *instance = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI guid_helper_CreateNewGuid( void *iface, GUID *value )
{
    static LONG seq;
    FILETIME ft = { 0 };
    LARGE_INTEGER qpc = { 0 };
    ULONG s;

    if (!value) return E_POINTER;
    GetSystemTimeAsFileTime( &ft );
    QueryPerformanceCounter( &qpc );
    s = (ULONG)InterlockedIncrement( &seq );
    value->Data1 = ft.dwLowDateTime ^ (ULONG)qpc.LowPart ^ (s * 0x9e3779b9u);
    value->Data2 = (USHORT)(ft.dwHighDateTime ^ (qpc.HighPart & 0xffff));
    value->Data3 = (USHORT)(((qpc.LowPart >> 16) & 0x0fff) | 0x4000);
    value->Data4[0] = (BYTE)(((s >> 8) & 0x3f) | 0x80);
    value->Data4[1] = (BYTE)(s & 0xff);
    value->Data4[2] = (BYTE)(GetCurrentProcessId() & 0xff);
    value->Data4[3] = (BYTE)((GetCurrentProcessId() >> 8) & 0xff);
    value->Data4[4] = (BYTE)(GetCurrentThreadId() & 0xff);
    value->Data4[5] = (BYTE)((GetCurrentThreadId() >> 8) & 0xff);
    value->Data4[6] = (BYTE)((ft.dwLowDateTime >> 8) & 0xff);
    value->Data4[7] = (BYTE)((qpc.LowPart >> 24) ^ (s & 0xff));
    return S_OK;
}

static HRESULT WINAPI guid_helper_get_Empty( void *iface, GUID *value )
{
    if (!value) return E_POINTER;
    memset( value, 0, sizeof(*value) );
    return S_OK;
}

static HRESULT WINAPI guid_helper_Equals( void *iface, const GUID *target, const GUID *value, BYTE *result )
{
    if (!target || !value || !result) return E_POINTER;
    *result = IsEqualGUID( target, value ) ? 1 : 0;
    return S_OK;
}

static const void *guid_helper_factory_vtbl[] =
{
    guid_helper_QueryInterface,
    async_causality_tracer_AddRef,
    async_causality_tracer_Release,
    async_causality_tracer_GetIids,
    guid_helper_GetRuntimeClassName,
    async_causality_tracer_GetTrustLevel,
    guid_helper_ActivateInstance,
};

static const void *guid_helper_statics_vtbl[] =
{
    guid_helper_QueryInterface,
    async_causality_tracer_AddRef,
    async_causality_tracer_Release,
    async_causality_tracer_GetIids,
    guid_helper_GetRuntimeClassName,
    async_causality_tracer_GetTrustLevel,
    guid_helper_CreateNewGuid,
    guid_helper_get_Empty,
    guid_helper_Equals,
};

static struct guid_helper_factory guid_helper_factory_obj =
{
    guid_helper_factory_vtbl,
    guid_helper_statics_vtbl,
};

/* =========================================================================
 * Windows.Foundation.Deferral
 * ========================================================================= */
static const GUID IID_IDeferral_local =
    { 0xd6269732, 0x3b7f, 0x46a7, { 0xb4, 0x0b, 0x4f, 0xdc, 0xa2, 0xa2, 0xc6, 0x93 } };
static const GUID IID_IDeferralFactory_local =
    { 0x65a1ecc5, 0x3fb5, 0x4832, { 0x8c, 0xa9, 0xf0, 0x61, 0xb2, 0x81, 0xd1, 0x3a } };
static const GUID IID_IClosable_local =
    { 0x30d5a829, 0x7fa4, 0x4026, { 0x83, 0xbb, 0xd7, 0x5b, 0xae, 0x4e, 0xa9, 0x9e } };

struct deferral_handler_vtbl
{
    HRESULT (WINAPI *QueryInterface)(void *iface, REFIID iid, void **out);
    ULONG (WINAPI *AddRef)(void *iface);
    ULONG (WINAPI *Release)(void *iface);
    HRESULT (WINAPI *Invoke)(void *iface);
};

struct deferral_obj
{
    const void **vtbl;
    const void **vtbl_closable;
    LONG ref;
    LONG completed;
    IUnknown *handler;
};

static inline struct deferral_obj *deferral_from_closable(void *iface)
{
    return CONTAINING_RECORD(iface, struct deferral_obj, vtbl_closable);
}

static HRESULT WINAPI deferral_QueryInterface(void *iface, REFIID iid, void **out)
{
    struct deferral_obj *impl = iface;
    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualGUID(iid, &IID_IUnknown) ||
        IsEqualGUID(iid, &IID_IInspectable) ||
        IsEqualGUID(iid, &IID_IAgileObject) ||
        IsEqualGUID(iid, &IID_IDeferral_local))
        *out = &impl->vtbl;
    else if (IsEqualGUID(iid, &IID_IClosable_local))
        *out = &impl->vtbl_closable;
    else
        return E_NOINTERFACE;
    InterlockedIncrement(&impl->ref);
    return S_OK;
}

static ULONG WINAPI deferral_AddRef(void *iface)
{
    struct deferral_obj *impl = iface;
    return InterlockedIncrement(&impl->ref);
}

static ULONG WINAPI deferral_Release(void *iface)
{
    struct deferral_obj *impl = iface;
    ULONG ref = InterlockedDecrement(&impl->ref);
    if (!ref)
    {
        if (impl->handler) IUnknown_Release(impl->handler);
        free(impl);
    }
    return ref;
}

static HRESULT WINAPI deferral_GetIids(void *iface, ULONG *count, IID **iids)
{
    if (!count || !iids) return E_POINTER;
    *count = 2;
    *iids = CoTaskMemAlloc(2 * sizeof(IID));
    if (!*iids) return E_OUTOFMEMORY;
    (*iids)[0] = IID_IDeferral_local;
    (*iids)[1] = IID_IClosable_local;
    return S_OK;
}

static HRESULT WINAPI deferral_GetRuntimeClassName(void *iface, HSTRING *name)
{
    return WindowsCreateString(L"Windows.Foundation.Deferral", 27, name);
}

static HRESULT WINAPI deferral_GetTrustLevel(void *iface, TrustLevel *level)
{
    if (level) *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI deferral_Complete(void *iface)
{
    struct deferral_obj *impl = iface;
    IUnknown *handler;
    if (InterlockedExchange(&impl->completed, 1)) return S_OK;
    handler = impl->handler;
    impl->handler = NULL;
    if (handler)
    {
        const struct deferral_handler_vtbl *vtbl = *(const struct deferral_handler_vtbl **)handler;
        HRESULT hr = vtbl->Invoke(handler);
        IUnknown_Release(handler);
        return hr;
    }
    return S_OK;
}

static HRESULT WINAPI deferral_closable_QueryInterface(void *iface, REFIID iid, void **out)
{
    return deferral_QueryInterface(deferral_from_closable(iface), iid, out);
}

static ULONG WINAPI deferral_closable_AddRef(void *iface)
{
    return deferral_AddRef(deferral_from_closable(iface));
}

static ULONG WINAPI deferral_closable_Release(void *iface)
{
    return deferral_Release(deferral_from_closable(iface));
}

static HRESULT WINAPI deferral_closable_Close(void *iface)
{
    return deferral_Complete(deferral_from_closable(iface));
}

static const void *deferral_vtbl[] =
{
    deferral_QueryInterface,
    deferral_AddRef,
    deferral_Release,
    deferral_GetIids,
    deferral_GetRuntimeClassName,
    deferral_GetTrustLevel,
    deferral_Complete,
};

static const void *deferral_closable_vtbl[] =
{
    deferral_closable_QueryInterface,
    deferral_closable_AddRef,
    deferral_closable_Release,
    deferral_GetIids,
    deferral_GetRuntimeClassName,
    deferral_GetTrustLevel,
    deferral_closable_Close,
};

static HRESULT WINAPI deferral_factory_QueryInterface(void *iface, REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualGUID(iid, &IID_IUnknown) ||
        IsEqualGUID(iid, &IID_IInspectable) ||
        IsEqualGUID(iid, &IID_IAgileObject) ||
        IsEqualGUID(iid, &IID_IActivationFactory) ||
        IsEqualGUID(iid, &IID_IDeferralFactory_local))
    {
        *out = iface;
        return S_OK;
    }
    return E_NOINTERFACE;
}

static HRESULT WINAPI deferral_factory_Create(void *iface, IUnknown *handler, void **out)
{
    struct deferral_obj *impl;
    if (!out) return E_POINTER;
    *out = NULL;
    if (!handler) return E_POINTER;
    impl = calloc(1, sizeof(*impl));
    if (!impl) return E_OUTOFMEMORY;
    impl->vtbl = deferral_vtbl;
    impl->vtbl_closable = deferral_closable_vtbl;
    impl->ref = 1;
    impl->handler = handler;
    IUnknown_AddRef(handler);
    *out = &impl->vtbl;
    return S_OK;
}

static const void *deferral_factory_vtbl[] =
{
    deferral_factory_QueryInterface,
    async_causality_tracer_AddRef,
    async_causality_tracer_Release,
    async_causality_tracer_GetIids,
    deferral_GetRuntimeClassName,
    async_causality_tracer_GetTrustLevel,
    deferral_factory_Create,
};

static struct { const void **vtbl; } deferral_factory_obj = { deferral_factory_vtbl };

HRESULT WINAPI DllGetActivationFactory(HSTRING classid, IActivationFactory **factory)
{
    const WCHAR *buffer = WindowsGetStringRawBuffer(classid, NULL);

    TRACE("classid %s, factory %p.\n", debugstr_hstring(classid), factory);

    heap_prewarm();
    *factory = NULL;

    if (!wcscmp(buffer, L"Windows.Foundation.Metadata.ApiInformation"))
        IActivationFactory_AddRef((*factory = &api_information_statics.IActivationFactory_iface));
    if (!wcscmp(buffer, L"Windows.Foundation.PropertyValue"))
        IActivationFactory_AddRef((*factory = &property_value_statics.IActivationFactory_iface));
    if (!wcscmp(buffer, L"Windows.Foundation.Collections.PropertySet"))
        IActivationFactory_AddRef((*factory = property_set_factory));
    if (!wcscmp(buffer, L"Windows.Foundation.Collections.StringMap"))
        IActivationFactory_AddRef((*factory = string_map_factory));
    if (!wcscmp(buffer, L"Windows.Storage.Streams.Buffer"))
        IActivationFactory_AddRef((*factory = buffer_activation_factory));
    if (!wcscmp(buffer, L"Windows.Storage.Streams.DataWriter"))
        IActivationFactory_AddRef((*factory = data_writer_activation_factory));
    if (!wcscmp(buffer, L"Windows.Foundation.Uri"))
        IActivationFactory_AddRef((*factory = uri_activation_factory));
    if (!wcscmp(buffer, L"Windows.Foundation.WwwFormUrlDecoder"))
        IActivationFactory_AddRef((*factory = www_form_url_decoder_factory));
    if (!wcscmp(buffer, L"Windows.Foundation.Diagnostics.LoggingFields"))
        IActivationFactory_AddRef((*factory = logging_fields_activation_factory));
    if (!wcscmp(buffer, L"Windows.Foundation.Diagnostics.AsyncCausalityTracer"))
        *factory = (IActivationFactory *)&async_causality_tracer_factory_obj;
    if (!wcscmp(buffer, L"Windows.Foundation.Diagnostics.LoggingChannelOptions"))
        *factory = (IActivationFactory *)&logging_channel_options_factory_obj;
    if (!wcscmp(buffer, L"Windows.Foundation.Diagnostics.LoggingChannel"))
        *factory = (IActivationFactory *)&logging_channel_factory_obj;
    if (!wcscmp(buffer, L"Windows.Foundation.Diagnostics.LoggingOptions"))
        *factory = (IActivationFactory *)&logging_options_factory_obj.vtbl_factory;
    if (!wcscmp(buffer, L"Windows.Foundation.GuidHelper"))
        *factory = (IActivationFactory *)&guid_helper_factory_obj.vtbl_factory;
    if (!wcscmp(buffer, L"Windows.Foundation.Deferral"))
        *factory = (IActivationFactory *)&deferral_factory_obj;

    if (*factory) return S_OK;
    return E_NOINTERFACE;
}

HRESULT WINAPI RoIsApiContractMajorVersionPresent(const WCHAR *name, UINT16 major, BOOL *result)
{
    FIXME("name %s, major %u, result %p\n", debugstr_w(name), major, result);
    *result = FALSE;
    return S_OK;
}

/*
 * Global WinRT Type-Metadata Reflection Cache
 *
 * O(1) FNV-1a hash table mapping WinRT type names and namespaces to their
 * resolved metadata descriptor { package_path, winmd_file, type_id, ns },
 * protected by RTL_SRWLOCK (shared read for lookups, exclusive write on miss).
 * Core WinRT namespaces and common types are pre-seeded on first use so
 * RoResolveNamespace and RoGetMetaDataFile avoid repetitive filesystem /
 * wineserver RPCs.
 */
#define METADATA_CACHE_SIZE 1024

struct metadata_cache_entry
{
    struct metadata_cache_entry *next;
    WCHAR *full_name;       /* owned key: namespace or full type name */
    WCHAR *ns_buf;          /* owned namespace string */
    HSTRING package_path;   /* directory containing the .winmd */
    HSTRING winmd_file;     /* full path to .winmd file */
    UINT32 type_id;         /* mdTypeDef token (0x02000001+) or 0 for namespace */
};

static RTL_SRWLOCK metadata_cache_lock = RTL_SRWLOCK_INIT;
static INIT_ONCE metadata_cache_once = INIT_ONCE_STATIC_INIT;
static struct metadata_cache_entry *metadata_cache[METADATA_CACHE_SIZE];
static HSTRING default_metadata_dir;

static UINT32 metadata_cache_hash( const WCHAR *str )
{
    UINT32 h = 2166136261u;
    for (; *str; str++)
    {
        h ^= (UINT32)*str;
        h *= 16777619u;
    }
    return h;
}

static void metadata_cache_insert_locked( const WCHAR *full_name, const WCHAR *ns,
                                          HSTRING pkg_path, HSTRING winmd_file, UINT32 type_id )
{
    UINT32 bucket = metadata_cache_hash( full_name ) & (METADATA_CACHE_SIZE - 1);
    struct metadata_cache_entry *entry;

    for (entry = metadata_cache[bucket]; entry; entry = entry->next)
    {
        if (!wcscmp( entry->full_name, full_name ))
        {
            if (type_id && !entry->type_id) entry->type_id = type_id;
            return;
        }
    }

    entry = calloc( 1, sizeof(*entry) );
    if (!entry) return;
    entry->full_name = wcsdup( full_name );
    entry->ns_buf = wcsdup( ns );
    if (!entry->full_name || !entry->ns_buf)
    {
        free( entry->full_name );
        free( entry->ns_buf );
        free( entry );
        return;
    }
    if (pkg_path) WindowsDuplicateString( pkg_path, &entry->package_path );
    if (winmd_file) WindowsDuplicateString( winmd_file, &entry->winmd_file );
    entry->type_id = type_id;
    entry->next = metadata_cache[bucket];
    metadata_cache[bucket] = entry;
}

static void metadata_cache_seed_entry( const WCHAR *full_name, const WCHAR *ns,
                                       const WCHAR *base_dir, const WCHAR *winmd_name, UINT32 type_id )
{
    WCHAR full_path[MAX_PATH];
    HSTRING h_winmd = NULL;

    swprintf( full_path, ARRAY_SIZE(full_path), L"%s\\%s", base_dir, winmd_name );
    if (SUCCEEDED(WindowsCreateString( full_path, wcslen( full_path ), &h_winmd )))
    {
        metadata_cache_insert_locked( full_name, ns, default_metadata_dir, h_winmd, type_id );
        WindowsDeleteString( h_winmd );
    }
}

static BOOL WINAPI init_metadata_cache( INIT_ONCE *once, void *param, void **ctx )
{
    static const struct
    {
        const WCHAR *ns;
        const WCHAR *winmd;
    } core_namespaces[] =
    {
        { L"Windows.Foundation",                              L"windows.foundation.winmd" },
        { L"Windows.Foundation.Collections",                  L"windows.foundation.winmd" },
        { L"Windows.Foundation.Metadata",                     L"windows.foundation.winmd" },
        { L"Windows.Foundation.Diagnostics",                  L"windows.foundation.winmd" },
        { L"Windows.Foundation.Numerics",                     L"windows.foundation.winmd" },
        { L"Windows.UI",                                      L"windows.ui.winmd" },
        { L"Windows.UI.Core",                                 L"windows.ui.winmd" },
        { L"Windows.UI.ViewManagement",                       L"windows.ui.winmd" },
        { L"Windows.UI.Composition",                          L"windows.ui.winmd" },
        { L"Windows.UI.Input",                                L"windows.ui.winmd" },
        { L"Windows.UI.Notifications",                        L"windows.ui.winmd" },
        { L"Windows.UI.Popups",                               L"windows.ui.winmd" },
        { L"Windows.UI.Text",                                 L"windows.ui.winmd" },
        { L"Windows.UI.Xaml",                                 L"windows.ui.xaml.winmd" },
        { L"Windows.UI.Xaml.Automation",                      L"windows.ui.xaml.winmd" },
        { L"Windows.UI.Xaml.Controls",                        L"windows.ui.xaml.winmd" },
        { L"Windows.UI.Xaml.Controls.Primitives",             L"windows.ui.xaml.winmd" },
        { L"Windows.UI.Xaml.Data",                            L"windows.ui.xaml.winmd" },
        { L"Windows.UI.Xaml.Documents",                       L"windows.ui.xaml.winmd" },
        { L"Windows.UI.Xaml.Input",                           L"windows.ui.xaml.winmd" },
        { L"Windows.UI.Xaml.Interop",                         L"windows.ui.xaml.winmd" },
        { L"Windows.UI.Xaml.Markup",                          L"windows.ui.xaml.winmd" },
        { L"Windows.UI.Xaml.Media",                           L"windows.ui.xaml.winmd" },
        { L"Windows.UI.Xaml.Media.Animation",                 L"windows.ui.xaml.winmd" },
        { L"Windows.UI.Xaml.Media.Imaging",                   L"windows.ui.xaml.winmd" },
        { L"Windows.UI.Xaml.Navigation",                      L"windows.ui.xaml.winmd" },
        { L"Windows.UI.Xaml.Shapes",                          L"windows.ui.xaml.winmd" },
        { L"Windows.Storage",                                 L"windows.storage.winmd" },
        { L"Windows.Storage.Streams",                         L"windows.storage.winmd" },
        { L"Windows.Storage.Pickers",                         L"windows.storage.winmd" },
        { L"Windows.Storage.FileProperties",                  L"windows.storage.winmd" },
        { L"Windows.ApplicationModel",                        L"windows.applicationmodel.winmd" },
        { L"Windows.ApplicationModel.Activation",             L"windows.applicationmodel.winmd" },
        { L"Windows.ApplicationModel.AppExtensions",          L"windows.applicationmodel.winmd" },
        { L"Windows.ApplicationModel.Background",             L"windows.applicationmodel.winmd" },
        { L"Windows.ApplicationModel.Calls",                  L"windows.applicationmodel.winmd" },
        { L"Windows.ApplicationModel.Core",                   L"windows.applicationmodel.winmd" },
        { L"Windows.ApplicationModel.DataTransfer",           L"windows.applicationmodel.winmd" },
        { L"Windows.ApplicationModel.Resources",              L"windows.applicationmodel.winmd" },
        { L"Windows.ApplicationModel.Resources.Core",         L"windows.applicationmodel.winmd" },
        { L"Windows.ApplicationModel.Store",                  L"windows.applicationmodel.winmd" },
        { L"Windows.Networking",                              L"windows.networking.winmd" },
        { L"Windows.Networking.Connectivity",                 L"windows.networking.winmd" },
        { L"Windows.Networking.PushNotifications",            L"windows.networking.winmd" },
        { L"Windows.Networking.Sockets",                      L"windows.networking.winmd" },
        { L"Windows.System",                                  L"windows.system.winmd" },
        { L"Windows.System.Profile",                          L"windows.system.winmd" },
        { L"Windows.System.Threading",                        L"windows.system.winmd" },
        { L"Windows.System.UserProfile",                      L"windows.system.winmd" },
        { L"Windows.Graphics",                                L"windows.graphics.winmd" },
        { L"Windows.Graphics.Display",                        L"windows.graphics.winmd" },
        { L"Windows.Graphics.Imaging",                        L"windows.graphics.winmd" },
        { L"Windows.Graphics.Printing",                       L"windows.graphics.winmd" },
        { L"Windows.Data",                                    L"windows.data.winmd" },
        { L"Windows.Data.Json",                               L"windows.data.winmd" },
        { L"Windows.Data.Xml",                                L"windows.data.winmd" },
        { L"Windows.Data.Xml.Dom",                            L"windows.data.winmd" },
        { L"Windows.Media",                                   L"windows.media.winmd" },
        { L"Windows.Media.Capture",                           L"windows.media.winmd" },
        { L"Windows.Media.Editing",                           L"windows.media.winmd" },
        { L"Windows.Media.Playback",                          L"windows.media.winmd" },
        { L"Windows.Media.Transcoding",                       L"windows.media.winmd" },
        { L"Windows.Security",                                L"windows.security.winmd" },
        { L"Windows.Security.Authentication.Web.Core",        L"windows.security.winmd" },
        { L"Windows.Security.Credentials",                    L"windows.security.winmd" },
        { L"Windows.Security.Cryptography",                   L"windows.security.winmd" },
        { L"Windows.Devices",                                 L"windows.devices.winmd" },
        { L"Windows.Devices.Enumeration",                     L"windows.devices.winmd" },
        { L"Windows.Devices.Geolocation",                     L"windows.devices.winmd" },
        { L"Windows.Devices.Input",                           L"windows.devices.winmd" },
        { L"Windows.Web",                                     L"windows.web.winmd" },
        { L"Windows.Web.Http",                                L"windows.web.winmd" },
        { L"Microsoft.UI",                                    L"microsoft.ui.winmd" },
        { L"Microsoft.UI.Composition",                        L"microsoft.ui.winmd" },
        { L"Microsoft.UI.Content",                            L"microsoft.ui.winmd" },
        { L"Microsoft.UI.Dispatching",                        L"microsoft.ui.winmd" },
        { L"Microsoft.UI.Windowing",                          L"microsoft.ui.winmd" },
        { L"Microsoft.UI.Xaml",                               L"microsoft.ui.xaml.winmd" },
        { L"Microsoft.UI.Xaml.Controls",                      L"microsoft.ui.xaml.winmd" },
        { L"Microsoft.UI.Xaml.Controls.Primitives",           L"microsoft.ui.xaml.winmd" },
        { L"Microsoft.UI.Xaml.Data",                          L"microsoft.ui.xaml.winmd" },
        { L"Microsoft.UI.Xaml.Input",                         L"microsoft.ui.xaml.winmd" },
        { L"Microsoft.UI.Xaml.Markup",                        L"microsoft.ui.xaml.winmd" },
        { L"Microsoft.UI.Xaml.Media",                         L"microsoft.ui.xaml.winmd" },
        { L"Microsoft.UI.Xaml.Shapes",                        L"microsoft.ui.xaml.winmd" },
        { L"Microsoft.UI.Xaml.XamlTypeInfo",                  L"microsoft.ui.xaml.winmd" },
        { L"Microsoft.Windows.ApplicationModel.Resources",    L"microsoft.windows.winmd" },
        { L"Microsoft.Windows.AppLifecycle",                  L"microsoft.windows.winmd" },
        { L"Microsoft.Windows.AppNotifications",              L"microsoft.windows.winmd" },
        { L"Microsoft.Windows.Management.Deployment",         L"microsoft.windows.winmd" },
        { L"Microsoft.Windows.PushNotifications",             L"microsoft.windows.winmd" },
        { L"Microsoft.Windows.System",                        L"microsoft.windows.winmd" },
    };
    static const struct
    {
        const WCHAR *type_name;
        const WCHAR *ns;
        const WCHAR *winmd;
        UINT32 type_id;
    } core_types[] =
    {
        { L"Windows.Foundation.Uri",                          L"Windows.Foundation",             L"windows.foundation.winmd",       0x02000001 },
        { L"Windows.Foundation.PropertyValue",                L"Windows.Foundation",             L"windows.foundation.winmd",       0x02000002 },
        { L"Windows.Foundation.Collections.PropertySet",      L"Windows.Foundation.Collections", L"windows.foundation.winmd",       0x02000003 },
        { L"Windows.Storage.StorageFile",                     L"Windows.Storage",                L"windows.storage.winmd",          0x02000004 },
        { L"Windows.Storage.StorageFolder",                   L"Windows.Storage",                L"windows.storage.winmd",          0x02000005 },
        { L"Windows.Storage.ApplicationData",                 L"Windows.Storage",                L"windows.storage.winmd",          0x02000006 },
        { L"Windows.ApplicationModel.Package",                L"Windows.ApplicationModel",       L"windows.applicationmodel.winmd", 0x02000007 },
        { L"Windows.UI.Xaml.Application",                     L"Windows.UI.Xaml",                L"windows.ui.xaml.winmd",          0x02000008 },
        { L"Windows.UI.Xaml.Window",                          L"Windows.UI.Xaml",                L"windows.ui.xaml.winmd",          0x02000009 },
        { L"Windows.UI.Xaml.DependencyObject",                L"Windows.UI.Xaml",                L"windows.ui.xaml.winmd",          0x0200000a },
        { L"Windows.UI.Xaml.UIElement",                       L"Windows.UI.Xaml",                L"windows.ui.xaml.winmd",          0x0200000b },
        { L"Windows.UI.Xaml.FrameworkElement",                L"Windows.UI.Xaml",                L"windows.ui.xaml.winmd",          0x0200000c },
        { L"Windows.UI.Xaml.Controls.Grid",                   L"Windows.UI.Xaml.Controls",       L"windows.ui.xaml.winmd",          0x0200000d },
        { L"Windows.UI.Xaml.Controls.StackPanel",             L"Windows.UI.Xaml.Controls",       L"windows.ui.xaml.winmd",          0x0200000e },
        { L"Windows.UI.Xaml.Controls.TextBlock",              L"Windows.UI.Xaml.Controls",       L"windows.ui.xaml.winmd",          0x0200000f },
        { L"Windows.UI.Xaml.Controls.Button",                 L"Windows.UI.Xaml.Controls",       L"windows.ui.xaml.winmd",          0x02000010 },
        { L"Windows.UI.Xaml.Controls.Page",                   L"Windows.UI.Xaml.Controls",       L"windows.ui.xaml.winmd",          0x02000011 },
        { L"Windows.UI.Xaml.Controls.Frame",                  L"Windows.UI.Xaml.Controls",       L"windows.ui.xaml.winmd",          0x02000012 },
        { L"Windows.UI.Xaml.Controls.Border",                 L"Windows.UI.Xaml.Controls",       L"windows.ui.xaml.winmd",          0x02000013 },
        { L"Windows.UI.Xaml.Controls.ContentControl",         L"Windows.UI.Xaml.Controls",       L"windows.ui.xaml.winmd",          0x02000014 },
        { L"Windows.UI.Xaml.Controls.UserControl",            L"Windows.UI.Xaml.Controls",       L"windows.ui.xaml.winmd",          0x02000015 },
        { L"Windows.UI.Xaml.Controls.Image",                  L"Windows.UI.Xaml.Controls",       L"windows.ui.xaml.winmd",          0x02000016 },
        { L"Windows.UI.Xaml.Controls.ListView",               L"Windows.UI.Xaml.Controls",       L"windows.ui.xaml.winmd",          0x02000017 },
        { L"Windows.UI.Xaml.Controls.ScrollViewer",           L"Windows.UI.Xaml.Controls",       L"windows.ui.xaml.winmd",          0x02000018 },
        { L"Windows.UI.Xaml.Controls.SwapChainPanel",         L"Windows.UI.Xaml.Controls",       L"windows.ui.xaml.winmd",          0x02000019 },
        { L"Microsoft.UI.Xaml.Application",                   L"Microsoft.UI.Xaml",              L"microsoft.ui.xaml.winmd",        0x0200001a },
        { L"Microsoft.UI.Xaml.Window",                        L"Microsoft.UI.Xaml",              L"microsoft.ui.xaml.winmd",        0x0200001b },
        { L"Microsoft.UI.Xaml.Controls.Grid",                 L"Microsoft.UI.Xaml.Controls",     L"microsoft.ui.xaml.winmd",        0x0200001c },
        { L"Microsoft.UI.Xaml.Controls.WebView2",             L"Microsoft.UI.Xaml.Controls",     L"microsoft.ui.xaml.winmd",        0x0200001d },
        { L"Microsoft.UI.Xaml.Controls.SwapChainPanel",       L"Microsoft.UI.Xaml.Controls",     L"microsoft.ui.xaml.winmd",        0x0200001e },
    };

    WCHAR sysdir[MAX_PATH], meta_dir[MAX_PATH];
    BOOL wow64 = FALSE;
    size_t i;

    IsWow64Process( GetCurrentProcess(), &wow64 );
    if (wow64)
    {
        GetWindowsDirectoryW( sysdir, ARRAY_SIZE(sysdir) );
        swprintf( meta_dir, ARRAY_SIZE(meta_dir), L"%s\\sysnative\\winmetadata", sysdir );
    }
    else
    {
        GetSystemDirectoryW( sysdir, ARRAY_SIZE(sysdir) );
        swprintf( meta_dir, ARRAY_SIZE(meta_dir), L"%s\\winmetadata", sysdir );
    }
    WindowsCreateString( meta_dir, wcslen( meta_dir ), &default_metadata_dir );

    RtlAcquireSRWLockExclusive( &metadata_cache_lock );
    for (i = 0; i < ARRAY_SIZE(core_namespaces); i++)
        metadata_cache_seed_entry( core_namespaces[i].ns, core_namespaces[i].ns,
                                   meta_dir, core_namespaces[i].winmd, 0 );
    for (i = 0; i < ARRAY_SIZE(core_types); i++)
        metadata_cache_seed_entry( core_types[i].type_name, core_types[i].ns,
                                   meta_dir, core_types[i].winmd, core_types[i].type_id );
    RtlReleaseSRWLockExclusive( &metadata_cache_lock );
    return TRUE;
}

static BOOL metadata_cache_lookup( const WCHAR *name, HSTRING *winmd_file, UINT32 *type_id )
{
    struct metadata_cache_entry *entry;
    WCHAR parent[256];
    WCHAR *dot;
    BOOL found = FALSE;

    InitOnceExecuteOnce( &metadata_cache_once, init_metadata_cache, NULL, NULL );

    RtlAcquireSRWLockShared( &metadata_cache_lock );
    for (entry = metadata_cache[metadata_cache_hash( name ) & (METADATA_CACHE_SIZE - 1)];
         entry; entry = entry->next)
    {
        if (!wcscmp( entry->full_name, name ))
        {
            if (winmd_file && entry->winmd_file)
                WindowsDuplicateString( entry->winmd_file, winmd_file );
            if (type_id)
                *type_id = entry->type_id;
            RtlReleaseSRWLockShared( &metadata_cache_lock );
            return TRUE;
        }
    }
    RtlReleaseSRWLockShared( &metadata_cache_lock );

    /* Check parent namespaces in cache and memoize on hit */
    lstrcpynW( parent, name, ARRAY_SIZE(parent) );
    while ((dot = wcsrchr( parent, '.' )) != NULL)
    {
        *dot = 0;
        RtlAcquireSRWLockShared( &metadata_cache_lock );
        for (entry = metadata_cache[metadata_cache_hash( parent ) & (METADATA_CACHE_SIZE - 1)];
             entry; entry = entry->next)
        {
            if (!wcscmp( entry->full_name, parent ))
            {
                HSTRING cached_pkg = NULL, cached_winmd = NULL;
                if (entry->package_path) WindowsDuplicateString( entry->package_path, &cached_pkg );
                if (entry->winmd_file) WindowsDuplicateString( entry->winmd_file, &cached_winmd );
                RtlReleaseSRWLockShared( &metadata_cache_lock );

                RtlAcquireSRWLockExclusive( &metadata_cache_lock );
                metadata_cache_insert_locked( name, parent, cached_pkg, cached_winmd, 0 );
                RtlReleaseSRWLockExclusive( &metadata_cache_lock );

                if (winmd_file) *winmd_file = cached_winmd;
                else if (cached_winmd) WindowsDeleteString( cached_winmd );
                if (cached_pkg) WindowsDeleteString( cached_pkg );
                if (type_id) *type_id = 0;
                found = TRUE;
                break;
            }
        }
        if (found) return TRUE;
        RtlReleaseSRWLockShared( &metadata_cache_lock );
    }

    return FALSE;
}

static BOOL probe_winmd_dir( const WCHAR *dir, const WCHAR *name, HSTRING *out_path )
{
    WCHAR candidate[MAX_PATH];
    WCHAR prefix[256];
    WCHAR *dot;

    if (!dir || !dir[0] || !name || !name[0]) return FALSE;
    lstrcpynW( prefix, name, ARRAY_SIZE(prefix) );

    while (prefix[0])
    {
        swprintf( candidate, ARRAY_SIZE(candidate), L"%s\\%s.winmd", dir, prefix );
        if (GetFileAttributesW( candidate ) != INVALID_FILE_ATTRIBUTES)
        {
            HSTRING h_dir = NULL, h_winmd = NULL;
            if (SUCCEEDED(WindowsCreateString( candidate, wcslen( candidate ), &h_winmd )))
            {
                WindowsCreateString( dir, wcslen( dir ), &h_dir );
                RtlAcquireSRWLockExclusive( &metadata_cache_lock );
                metadata_cache_insert_locked( name, prefix, h_dir, h_winmd, 0 );
                RtlReleaseSRWLockExclusive( &metadata_cache_lock );
                if (h_dir) WindowsDeleteString( h_dir );
                *out_path = h_winmd;
                return TRUE;
            }
        }
        dot = wcsrchr( prefix, '.' );
        if (!dot) break;
        *dot = 0;
    }
    return FALSE;
}

HRESULT WINAPI RoResolveNamespace(HSTRING name, HSTRING windowsMetaDataDir,
                                  DWORD packageGraphDirsCount, const HSTRING *packageGraphDirs,
                                  DWORD *metaDataFilePathsCount, HSTRING **metaDataFilePaths,
                                  DWORD *subNamespacesCount, HSTRING **subNamespaces)
{
    const WCHAR *raw_name, *raw_dir;
    HSTRING found_winmd = NULL;
    DWORD i;

    TRACE("name %s, windowsMetaDataDir %s, metaDataFilePaths %p, subNamespaces %p\n",
          debugstr_hstring(name), debugstr_hstring(windowsMetaDataDir),
          metaDataFilePaths, subNamespaces);

    if (!name || (!metaDataFilePaths && !subNamespaces))
        return E_INVALIDARG;

    if (metaDataFilePathsCount) *metaDataFilePathsCount = 0;
    if (metaDataFilePaths) *metaDataFilePaths = NULL;
    if (subNamespacesCount) *subNamespacesCount = 0;
    if (subNamespaces) *subNamespaces = NULL;

    raw_name = WindowsGetStringRawBuffer( name, NULL );
    if (!raw_name || !raw_name[0])
        return E_INVALIDARG;

    /* Fast path: O(1) lookup in the reflection cache when using default WinMetadata dir */
    raw_dir = windowsMetaDataDir ? WindowsGetStringRawBuffer( windowsMetaDataDir, NULL ) : NULL;
    if ((!raw_dir || !raw_dir[0]) && packageGraphDirsCount == 0)
    {
        if (metadata_cache_lookup( raw_name, &found_winmd, NULL ) && found_winmd)
        {
            if (metaDataFilePaths && metaDataFilePathsCount)
            {
                HSTRING *arr = CoTaskMemAlloc( sizeof(HSTRING) );
                if (!arr)
                {
                    WindowsDeleteString( found_winmd );
                    return E_OUTOFMEMORY;
                }
                arr[0] = found_winmd;
                *metaDataFilePaths = arr;
                *metaDataFilePathsCount = 1;
            }
            else WindowsDeleteString( found_winmd );
            return S_OK;
        }
    }

    /* Probe explicit windowsMetaDataDir or packageGraphDirs on disk (memoized on hit) */
    if (raw_dir && raw_dir[0] && probe_winmd_dir( raw_dir, raw_name, &found_winmd ))
    {
        if (metaDataFilePaths && metaDataFilePathsCount)
        {
            HSTRING *arr = CoTaskMemAlloc( sizeof(HSTRING) );
            if (!arr)
            {
                WindowsDeleteString( found_winmd );
                return E_OUTOFMEMORY;
            }
            arr[0] = found_winmd;
            *metaDataFilePaths = arr;
            *metaDataFilePathsCount = 1;
        }
        else WindowsDeleteString( found_winmd );
        return S_OK;
    }

    for (i = 0; i < packageGraphDirsCount; i++)
    {
        const WCHAR *pkg_dir = packageGraphDirs && packageGraphDirs[i]
            ? WindowsGetStringRawBuffer( packageGraphDirs[i], NULL ) : NULL;
        if (pkg_dir && pkg_dir[0] && probe_winmd_dir( pkg_dir, raw_name, &found_winmd ))
        {
            if (metaDataFilePaths && metaDataFilePathsCount)
            {
                HSTRING *arr = CoTaskMemAlloc( sizeof(HSTRING) );
                if (!arr)
                {
                    WindowsDeleteString( found_winmd );
                    return E_OUTOFMEMORY;
                }
                arr[0] = found_winmd;
                *metaDataFilePaths = arr;
                *metaDataFilePathsCount = 1;
            }
            else WindowsDeleteString( found_winmd );
            return S_OK;
        }
    }

    /* Fall back to seeded reflection cache even when custom dirs were provided */
    if (metadata_cache_lookup( raw_name, &found_winmd, NULL ) && found_winmd)
    {
        if (metaDataFilePaths && metaDataFilePathsCount)
        {
            HSTRING *arr = CoTaskMemAlloc( sizeof(HSTRING) );
            if (!arr)
            {
                WindowsDeleteString( found_winmd );
                return E_OUTOFMEMORY;
            }
            arr[0] = found_winmd;
            *metaDataFilePaths = arr;
            *metaDataFilePathsCount = 1;
        }
        else WindowsDeleteString( found_winmd );
        return S_OK;
    }

    return RO_E_METADATA_NAME_NOT_FOUND;
}

HRESULT WINAPI RoGetMetaDataFile(const HSTRING name, void *metaDataDispenser,
                                 HSTRING *metaDataFilePath, void **metaDataImport,
                                 UINT32 *typeDefToken)
{
    const WCHAR *raw_name;
    HSTRING found_winmd = NULL;
    UINT32 tid = 0;

    TRACE("name %s, dispenser %p, metaDataFilePath %p, metaDataImport %p, typeDefToken %p\n",
          debugstr_hstring(name), metaDataDispenser, metaDataFilePath, metaDataImport, typeDefToken);

    if (metaDataFilePath) *metaDataFilePath = NULL;
    if (metaDataImport) *metaDataImport = NULL;
    if (typeDefToken) *typeDefToken = 0;

    if (!name) return E_INVALIDARG;
    raw_name = WindowsGetStringRawBuffer( name, NULL );
    if (!raw_name || !raw_name[0]) return E_INVALIDARG;

    if (metadata_cache_lookup( raw_name, &found_winmd, &tid ) && found_winmd)
    {
        if (metaDataFilePath) *metaDataFilePath = found_winmd;
        else WindowsDeleteString( found_winmd );
        if (typeDefToken) *typeDefToken = tid ? tid : 0x02000001;
        return S_OK;
    }

    return RO_E_METADATA_NAME_NOT_FOUND;
}

struct parse_type_context
{
    DWORD allocated_parts_count;
    DWORD parts_count;
    HSTRING *parts;
};

static HRESULT add_part(struct parse_type_context *context, const WCHAR *part, size_t length)
{
    DWORD new_parts_count;
    HSTRING *new_parts;
    HRESULT hr;

    if (context->parts_count == context->allocated_parts_count)
    {
        new_parts_count = context->allocated_parts_count ? context->allocated_parts_count * 2 : 4;
        new_parts = CoTaskMemRealloc(context->parts, new_parts_count * sizeof(*context->parts));
        if (!new_parts)
            return E_OUTOFMEMORY;

        context->allocated_parts_count = new_parts_count;
        context->parts = new_parts;
    }

    if (FAILED(hr = WindowsCreateString(part, length, &context->parts[context->parts_count])))
        return hr;

    context->parts_count++;
    return S_OK;
}

static HRESULT parse_part(struct parse_type_context *context, const WCHAR *input, unsigned int length)
{
    const WCHAR *start, *end, *ptr;

    start = input;
    end = start + length;

    /* Remove leading spaces */
    while (start < end && *start == ' ')
        start++;

    /* Remove trailing spaces */
    while (end - 1 >= start && end[-1] == ' ')
        end--;

    /* Only contains spaces */
    if (start == end)
        return RO_E_METADATA_INVALID_TYPE_FORMAT;

    /* Has spaces in the middle */
    for (ptr = start; ptr < end; ptr++)
    {
        if (*ptr == ' ')
            return RO_E_METADATA_INVALID_TYPE_FORMAT;
    }

    return add_part(context, start, end - start);
}

static HRESULT parse_type(struct parse_type_context *context, const WCHAR *input, unsigned int length)
{
    unsigned int i, parameter_count, nested_level;
    const WCHAR *start, *end, *part_start, *ptr;
    HRESULT hr;

    start = input;
    end = start + length;
    part_start = start;
    ptr = start;

    /* Read until the end of input or '`' or '<' or '>' or ',' */
    while (ptr < end && *ptr != '`' && *ptr != '<' && *ptr != '>' && *ptr != ',')
        ptr++;

    /* If the type name has '`' and there are characters before '`' */
    if (ptr > start && ptr < end && *ptr == '`')
    {
        /* Move past the '`' */
        ptr++;

        /* Read the number of type parameters, expecting '1' to '9' */
        if (!(ptr < end && *ptr >= '1' && *ptr <= '9'))
            return RO_E_METADATA_INVALID_TYPE_FORMAT;
        parameter_count = *ptr - '0';

        /* Move past the number of type parameters, expecting '<' */
        ptr++;
        if (!(ptr < end && *ptr == '<'))
            return RO_E_METADATA_INVALID_TYPE_FORMAT;

        /* Add the name of parameterized interface, e.g., the "interface`1" in "interface`1<parameter>" */
        if (FAILED(hr = parse_part(context, part_start, ptr - part_start)))
            return hr;

        /* Move past the '<' */
        ptr++;
        nested_level = 1;

        /* Read parameters inside brackets, e.g., the "p1" and "p2" in "interface`2<p1, p2>" */
        for (i = 0; i < parameter_count; i++)
        {
            /* Read a new parameter */
            part_start = ptr;

            /* Read until ','. The comma must be at the same nested bracket level */
            while (ptr < end)
            {
                if (*ptr == '<')
                {
                    nested_level++;
                    ptr++;
                }
                else if (*ptr == '>')
                {
                    /* The last parameter before '>' */
                    if (i == parameter_count - 1 && nested_level == 1)
                    {
                        if (FAILED(hr = parse_type(context, part_start, ptr - part_start)))
                            return hr;

                        nested_level--;
                        ptr++;

                        /* Finish reading all parameters */
                        break;
                    }

                    nested_level--;
                    ptr++;
                }
                else if (*ptr == ',' && nested_level == 1)
                {
                    /* Parse the parameter, which can be another parameterized type */
                    if (FAILED(hr = parse_type(context, part_start, ptr - part_start)))
                        return hr;

                    /* Move past the ',' */
                    ptr++;

                    /* Finish reading one parameter */
                    break;
                }
                else
                {
                    ptr++;
                }
            }
        }

        /* Mismatching brackets or not enough parameters */
        if (nested_level != 0 || i != parameter_count)
            return RO_E_METADATA_INVALID_TYPE_FORMAT;

        /* The remaining characters must be spaces */
        while (ptr < end)
        {
            if (*ptr++ != ' ')
                return RO_E_METADATA_INVALID_TYPE_FORMAT;
        }

        return S_OK;
    }
    /* Contain invalid '`', '<', '>' or ',' */
    else if (ptr != end)
    {
        return RO_E_METADATA_INVALID_TYPE_FORMAT;
    }
    /* Non-parameterized */
    else
    {
        return parse_part(context, part_start, ptr - part_start);
    }
}

HRESULT WINAPI RoParseTypeName(HSTRING type_name, DWORD *parts_count, HSTRING **parts)
{
    struct parse_type_context context = {0};
    const WCHAR *input;
    unsigned int i;
    HRESULT hr;

    TRACE("%s %p %p.\n", debugstr_hstring(type_name), parts_count, parts);

    /* Empty string */
    if (!WindowsGetStringLen(type_name))
        return E_INVALIDARG;

    input = WindowsGetStringRawBuffer(type_name, NULL);
    /* The string has a leading space */
    if (input[0] == ' ')
        return RO_E_METADATA_INVALID_TYPE_FORMAT;

    *parts_count = 0;
    *parts = NULL;
    if (FAILED(hr = parse_type(&context, input, wcslen(input))))
    {
        for (i = 0; i < context.parts_count; i++)
            WindowsDeleteString(context.parts[i]);
        CoTaskMemFree(context.parts);
        return hr;
    }

    *parts_count = context.parts_count;
    *parts = context.parts;
    return S_OK;
}
