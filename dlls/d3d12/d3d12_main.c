/*
 * Copyright 2018 Józef Kucia for CodeWeavers
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
 *
 */

#define COBJMACROS
#define VKD3D_NO_VULKAN_H
#define VKD3D_NO_WIN32_TYPES

#include "windef.h"
#include "dxgi1_6.h"
#include "d3d12.h"
#include "wine/vulkan.h"

#include <vkd3d.h>

#include "initguid.h"
#include "dxcore.h"
#include "wine/wined3d.h"
#include "wine/winedxgi.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(d3d12);
WINE_DECLARE_DEBUG_CHANNEL(winediag);

static HMODULE vulkan_module;

/* FIXME: We should unload vulkan-1.dll. */
static BOOL WINAPI load_vulkan_dll_once(INIT_ONCE *once, void *param, void **context)
{
    vulkan_module = LoadLibraryA("vulkan-1.dll");
    return TRUE;
}

static PFN_vkGetInstanceProcAddr load_vulkan(void)
{
    static INIT_ONCE init_once = INIT_ONCE_STATIC_INIT;

    InitOnceExecuteOnce(&init_once, load_vulkan_dll_once, NULL, NULL);

    if (vulkan_module)
        return (void *)GetProcAddress(vulkan_module, "vkGetInstanceProcAddr");

    return NULL;
}

HRESULT WINAPI D3D12GetDebugInterface(REFIID iid, void **debug)
{
    TRACE("iid %s, debug %p.\n", debugstr_guid(iid), debug);

    WARN("Returning DXGI_ERROR_SDK_COMPONENT_MISSING.\n");
    return DXGI_ERROR_SDK_COMPONENT_MISSING;
}

DEFINE_GUID(GUID_D3D12Debug, 0xc2a15b95, 0xbad7, 0x44e4, 0x92,0xee, 0x8e,0x2b,0xb9,0xe7,0x7f,0x8f);
DEFINE_GUID(GUID_D3D12Debug1, 0xaffc3dc5, 0x2e17, 0x4355, 0xa9,0x3d, 0x9d,0x33,0x83,0x13,0x03,0xfe);
DEFINE_GUID(GUID_D3D12Debug2, 0xc4f529f5, 0x5de4, 0x4905, 0x96,0x1c, 0x01,0x9c,0xc9,0x13,0x61,0x78);

struct d3d12_debug
{
    ID3D12Debug5 ID3D12Debug5_iface;
    LONG refcount;
    BOOL gpu_validation;
    BOOL sync_validation;
    D3D12_GPU_BASED_VALIDATION_FLAGS gpu_flags;
};

static inline struct d3d12_debug *impl_from_ID3D12Debug5( ID3D12Debug5 *iface )
{
    return CONTAINING_RECORD( iface, struct d3d12_debug, ID3D12Debug5_iface );
}

static HRESULT WINAPI d3d12_debug_QueryInterface( ID3D12Debug5 *iface, REFIID iid, void **out )
{
    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_ID3D12Debug ) ||
        IsEqualGUID( iid, &IID_ID3D12Debug1 ) ||
        IsEqualGUID( iid, &IID_ID3D12Debug2 ) ||
        IsEqualGUID( iid, &IID_ID3D12Debug3 ) ||
        IsEqualGUID( iid, &IID_ID3D12Debug4 ) ||
        IsEqualGUID( iid, &IID_ID3D12Debug5 ))
    {
        ID3D12Debug5_AddRef( iface );
        *out = iface;
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI d3d12_debug_AddRef( ID3D12Debug5 *iface )
{
    struct d3d12_debug *debug = impl_from_ID3D12Debug5( iface );
    return InterlockedIncrement( &debug->refcount );
}

static ULONG WINAPI d3d12_debug_Release( ID3D12Debug5 *iface )
{
    struct d3d12_debug *debug = impl_from_ID3D12Debug5( iface );
    ULONG refcount = InterlockedDecrement( &debug->refcount );

    if (!refcount)
        HeapFree( GetProcessHeap(), 0, debug );
    return refcount;
}

static void WINAPI d3d12_debug_EnableDebugLayer( ID3D12Debug5 *iface )
{
    struct d3d12_debug *debug = impl_from_ID3D12Debug5( iface );

    TRACE( "iface %p.\n", iface );
    debug->gpu_validation = true;
}

static void WINAPI d3d12_debug_SetEnableGPUBasedValidation( ID3D12Debug5 *iface, BOOL enable )
{
    struct d3d12_debug *debug = impl_from_ID3D12Debug5( iface );

    TRACE( "iface %p, enable %s.\n", iface, enable ? "TRUE" : "FALSE" );
    debug->gpu_validation = enable;
}

static void WINAPI d3d12_debug_SetEnableSynchronizedCommandQueueValidation( ID3D12Debug5 *iface, BOOL enable )
{
    struct d3d12_debug *debug = impl_from_ID3D12Debug5( iface );

    TRACE( "iface %p, enable %s.\n", iface, enable ? "TRUE" : "FALSE" );
    debug->sync_validation = enable;
}

static void WINAPI d3d12_debug_SetGPUBasedValidationFlags( ID3D12Debug5 *iface,
        D3D12_GPU_BASED_VALIDATION_FLAGS flags )
{
    struct d3d12_debug *debug = impl_from_ID3D12Debug5( iface );

    TRACE( "iface %p, flags %#x.\n", iface, flags );
    debug->gpu_flags = flags;
}

static void WINAPI d3d12_debug_DisableDebugLayer( ID3D12Debug5 *iface )
{
    struct d3d12_debug *debug = impl_from_ID3D12Debug5( iface );

    TRACE( "iface %p.\n", iface );
    debug->gpu_validation = false;
}

static void WINAPI d3d12_debug_SetEnableAutoName( ID3D12Debug5 *iface, BOOL enable )
{
    FIXME( "iface %p, enable %s, stub!\n", iface, enable ? "TRUE" : "FALSE" );
}

static const struct ID3D12Debug5Vtbl d3d12_debug_vtbl =
{
    d3d12_debug_QueryInterface,
    d3d12_debug_AddRef,
    d3d12_debug_Release,
    d3d12_debug_EnableDebugLayer,
    d3d12_debug_SetEnableGPUBasedValidation,
    d3d12_debug_SetEnableSynchronizedCommandQueueValidation,
    d3d12_debug_SetGPUBasedValidationFlags,
    d3d12_debug_DisableDebugLayer,
    d3d12_debug_SetEnableAutoName,
};

static void d3d12_debug_init( struct d3d12_debug *debug )
{
    static const ID3D12Debug5Vtbl *vtbl = &d3d12_debug_vtbl;

    debug->ID3D12Debug5_iface.lpVtbl = vtbl;
    debug->refcount = 1;
}

HRESULT WINAPI D3D12GetInterface(REFCLSID rclsid, REFIID riid, void **ppv)
{
    struct d3d12_debug *debug;
    HRESULT hr;

    TRACE( "rclsid %s, riid %s, ppv %p.\n", debugstr_guid( rclsid ), debugstr_guid( riid ), ppv );

    if (!ppv)
        return E_INVALIDARG;
    *ppv = NULL;

    if (IsEqualGUID( rclsid, &GUID_D3D12Debug ) ||
        IsEqualGUID( rclsid, &GUID_D3D12Debug1 ) ||
        IsEqualGUID( rclsid, &GUID_D3D12Debug2 ))
    {
        if (!(debug = HeapAlloc( GetProcessHeap(), 0, sizeof(*debug) )))
            return E_OUTOFMEMORY;
        d3d12_debug_init( debug );
        hr = ID3D12Debug5_QueryInterface( &debug->ID3D12Debug5_iface, riid, ppv );
        ID3D12Debug5_Release( &debug->ID3D12Debug5_iface );
        return hr;
    }

    FIXME( "rclsid %s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( rclsid ) );
    return E_NOINTERFACE;
}

HRESULT WINAPI D3D12EnableExperimentalFeatures(UINT feature_count,
        const IID *iids, void *configurations, UINT *configurations_sizes)
{
    FIXME("feature_count %u, iids %p, configurations %p, configurations_sizes %p stub!\n",
            feature_count, iids, configurations, configurations_sizes);

    return E_NOINTERFACE;
}

static HRESULT d3d12_signal_event(HANDLE event)
{
    return SetEvent(event) ? S_OK : E_FAIL;
}

static HRESULT d3d12_get_dxgi_adapter_for_dxcore_adapter(IDXCoreAdapter *adapter,
        IDXGIAdapter **dxgi_adapter)
{
    IDXGIFactory4 *factory;
    HRESULT hr;
    LUID luid;

    if (FAILED(hr = IDXCoreAdapter_GetProperty(adapter, InstanceLuid, sizeof(luid), &luid)))
    {
        WARN("Failed to get LUID for dxcore adapter, hr %#lx.\n", hr);
        return hr;
    }

    if (FAILED(hr = CreateDXGIFactory2(0, &IID_IDXGIFactory4, (void **)&factory)))
    {
        WARN("Failed to create DXGI factory, hr %#lx.\n", hr);
        return hr;
    }

    if (FAILED(hr = IDXGIFactory4_EnumAdapterByLuid(factory, luid, &IID_IDXGIAdapter,
            (void **)dxgi_adapter)))
    {
        WARN("Failed to enumerate adapter by LUID, hr %#lx.\n", hr);
    }

    IDXGIFactory4_Release(factory);

    return hr;
}

static HRESULT d3d12_get_adapter(IWineDXGIAdapter **wine_adapter, IUnknown *adapter)
{
    IDXGIAdapter *dxgi_adapter = NULL;
    IDXGIFactory4 *factory = NULL;
    IDXCoreAdapter *dxcore_adapter;
    HRESULT hr;

    if (!adapter)
    {
        if (FAILED(hr = CreateDXGIFactory2(0, &IID_IDXGIFactory4, (void **)&factory)))
        {
            WARN("Failed to create DXGI factory, hr %#lx.\n", hr);
            goto done;
        }

        if (FAILED(hr = IDXGIFactory4_EnumAdapters(factory, 0, &dxgi_adapter)))
        {
            WARN("Failed to enumerate primary adapter, hr %#lx.\n", hr);
            goto done;
        }

        adapter = (IUnknown *)dxgi_adapter;
    }
    else if (SUCCEEDED(IUnknown_QueryInterface(adapter, &IID_IDXCoreAdapter, (void **)&dxcore_adapter)))
    {
        if (FAILED(hr = d3d12_get_dxgi_adapter_for_dxcore_adapter(dxcore_adapter, &dxgi_adapter)))
        {
            WARN("Failed to create DXGI adapter for DXCore adapter, hr %#lx.\n", hr);
            goto done;
        }
        IDXCoreAdapter_Release(dxcore_adapter);

        adapter = (IUnknown *)dxgi_adapter;
    }

    if (FAILED(hr = IUnknown_QueryInterface(adapter, &IID_IWineDXGIAdapter, (void **)wine_adapter)))
        WARN("Invalid adapter %p, hr %#lx.\n", adapter, hr);

done:
    if (dxgi_adapter)
        IDXGIAdapter_Release(dxgi_adapter);
    if (factory)
        IDXGIFactory4_Release(factory);

    return hr;
}

static BOOL check_vk_instance_extension(VkInstance vk_instance,
        PFN_vkGetInstanceProcAddr pfn_vkGetInstanceProcAddr, const char *name)
{
    PFN_vkEnumerateInstanceExtensionProperties pfn_vkEnumerateInstanceExtensionProperties;
    VkExtensionProperties *properties;
    BOOL ret = FALSE;
    unsigned int i;
    uint32_t count;

    pfn_vkEnumerateInstanceExtensionProperties
            = (void *)pfn_vkGetInstanceProcAddr(vk_instance, "vkEnumerateInstanceExtensionProperties");

    if (pfn_vkEnumerateInstanceExtensionProperties(NULL, &count, NULL) < 0)
        return FALSE;

    if (!(properties = calloc(count, sizeof(*properties))))
        return FALSE;

    if (pfn_vkEnumerateInstanceExtensionProperties(NULL, &count, properties) >= 0)
    {
        for (i = 0; i < count; ++i)
        {
            if (!strcmp(properties[i].extensionName, name))
            {
                ret = TRUE;
                break;
            }
        }
    }

    free(properties);
    return ret;
}

static VkPhysicalDevice d3d12_get_vk_physical_device(struct vkd3d_instance *instance,
        PFN_vkGetInstanceProcAddr pfn_vkGetInstanceProcAddr, const struct wine_dxgi_adapter_info *adapter_info)
{
    PFN_vkGetPhysicalDeviceProperties2 pfn_vkGetPhysicalDeviceProperties2 = NULL;
    PFN_vkGetPhysicalDeviceProperties pfn_vkGetPhysicalDeviceProperties;
    PFN_vkEnumeratePhysicalDevices pfn_vkEnumeratePhysicalDevices;
    VkPhysicalDevice vk_physical_device = VK_NULL_HANDLE;
    VkPhysicalDeviceIDProperties id_properties;
    VkPhysicalDeviceProperties2 properties2;
    VkPhysicalDeviceProperties properties;
    VkPhysicalDevice *vk_physical_devices;
    VkInstance vk_instance;
    unsigned int i;
    uint32_t count;
    VkResult vr;

    vk_instance = vkd3d_instance_get_vk_instance(instance);

    pfn_vkEnumeratePhysicalDevices = (void *)pfn_vkGetInstanceProcAddr(vk_instance, "vkEnumeratePhysicalDevices");

    pfn_vkGetPhysicalDeviceProperties = (void *)pfn_vkGetInstanceProcAddr(vk_instance, "vkGetPhysicalDeviceProperties");
    if (check_vk_instance_extension(vk_instance, pfn_vkGetInstanceProcAddr, VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME))
        pfn_vkGetPhysicalDeviceProperties2 = (void *)pfn_vkGetInstanceProcAddr(vk_instance, "vkGetPhysicalDeviceProperties2KHR");

    if ((vr = pfn_vkEnumeratePhysicalDevices(vk_instance, &count, NULL)) < 0)
    {
        WARN("Failed to get device count, vr %d.\n", vr);
        return VK_NULL_HANDLE;
    }
    if (!count)
    {
        WARN("No physical device available.\n");
        return VK_NULL_HANDLE;
    }

    if (!(vk_physical_devices = calloc(count, sizeof(*vk_physical_devices))))
        return VK_NULL_HANDLE;

    if ((vr = pfn_vkEnumeratePhysicalDevices(vk_instance, &count, vk_physical_devices)) < 0)
        goto done;

    if (!IsEqualGUID(&adapter_info->driver_uuid, &GUID_NULL) && pfn_vkGetPhysicalDeviceProperties2
            && check_vk_instance_extension(vk_instance, pfn_vkGetInstanceProcAddr,
                    VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME))
    {
        TRACE("Matching adapters by UUIDs.\n");

        for (i = 0; i < count; ++i)
        {
            memset(&id_properties, 0, sizeof(id_properties));
            id_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;

            properties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
            properties2.pNext = &id_properties;

            pfn_vkGetPhysicalDeviceProperties2(vk_physical_devices[i], &properties2);

            if (!memcmp(id_properties.driverUUID, &adapter_info->driver_uuid, VK_UUID_SIZE)
                    && !memcmp(id_properties.deviceUUID, &adapter_info->device_uuid, VK_UUID_SIZE))
            {
                vk_physical_device = vk_physical_devices[i];
                break;
            }
        }
    }

    if (!vk_physical_device)
    {
        WARN("Matching adapters by PCI IDs.\n");

        for (i = 0; i < count; ++i)
        {
            pfn_vkGetPhysicalDeviceProperties(vk_physical_devices[i], &properties);

            if (properties.vendorID == adapter_info->vendor_id && properties.deviceID == adapter_info->device_id)
            {
                vk_physical_device = vk_physical_devices[i];
                break;
            }
        }
    }

    if (!vk_physical_device)
        FIXME("Could not find Vulkan physical device for DXGI adapter.\n");

done:
    free(vk_physical_devices);
    return vk_physical_device;
}

HRESULT WINAPI D3D12CreateDevice(IUnknown *adapter, D3D_FEATURE_LEVEL minimum_feature_level,
        REFIID iid, void **device)
{
    struct vkd3d_optional_instance_extensions_info optional_extensions_info;
    struct vkd3d_instance_create_info instance_create_info;
    PFN_vkGetInstanceProcAddr pfn_vkGetInstanceProcAddr;
    struct vkd3d_device_create_info device_create_info;
    struct wine_dxgi_adapter_info adapter_info;
    struct vkd3d_instance *instance;
    IWineDXGIAdapter *wine_adapter;
    HRESULT hr;

    static const char * const instance_extensions[] =
    {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
    };
    static const char * const optional_instance_extensions[] =
    {
        VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME,
        VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
    };
    static const char * const device_extensions[] =
    {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    };
    static const struct vkd3d_application_info application_info =
    {
        .type = VKD3D_STRUCTURE_TYPE_APPLICATION_INFO,
        .api_version = VKD3D_API_VERSION_1_2,
    };

    TRACE("adapter %p, minimum_feature_level %#x, iid %s, device %p.\n",
            adapter, minimum_feature_level, debugstr_guid(iid), device);

    if (!(pfn_vkGetInstanceProcAddr = load_vulkan()))
    {
        ERR_(winediag)("Failed to load Vulkan library.\n");
        return E_FAIL;
    }

    if (FAILED(hr = d3d12_get_adapter(&wine_adapter, adapter)))
        return hr;

    if (FAILED(hr = IWineDXGIAdapter_get_adapter_info(wine_adapter, &adapter_info)))
    {
        WARN("Failed to get adapter info, hr %#lx.\n", hr);
        goto done;
    }

    optional_extensions_info.type = VKD3D_STRUCTURE_TYPE_OPTIONAL_INSTANCE_EXTENSIONS_INFO;
    optional_extensions_info.next = &application_info;
    optional_extensions_info.extensions = optional_instance_extensions;
    optional_extensions_info.extension_count = ARRAY_SIZE(optional_instance_extensions);

    instance_create_info.type = VKD3D_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instance_create_info.next = &optional_extensions_info;
    instance_create_info.pfn_signal_event = d3d12_signal_event;
    instance_create_info.pfn_create_thread = NULL;
    instance_create_info.pfn_join_thread = NULL;
    instance_create_info.wchar_size = sizeof(WCHAR);
    instance_create_info.pfn_vkGetInstanceProcAddr = pfn_vkGetInstanceProcAddr;
    instance_create_info.instance_extensions = instance_extensions;
    instance_create_info.instance_extension_count = ARRAY_SIZE(instance_extensions);

    if (FAILED(hr = vkd3d_create_instance(&instance_create_info, &instance)))
    {
        WARN("Failed to create vkd3d instance, hr %#lx.\n", hr);
        goto done;
    }

    device_create_info.type = VKD3D_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_create_info.next = NULL;
    device_create_info.minimum_feature_level = minimum_feature_level;
    device_create_info.instance = instance;
    device_create_info.instance_create_info = NULL;
    device_create_info.vk_physical_device = d3d12_get_vk_physical_device(instance, pfn_vkGetInstanceProcAddr, &adapter_info);
    device_create_info.device_extensions = device_extensions;
    device_create_info.device_extension_count = ARRAY_SIZE(device_extensions);
    device_create_info.parent = (IUnknown *)wine_adapter;
    device_create_info.adapter_luid = adapter_info.luid;

    hr = vkd3d_create_device(&device_create_info, iid, device);

    vkd3d_instance_decref(instance);

done:
    IWineDXGIAdapter_Release(wine_adapter);
    return hr;
}

HRESULT WINAPI D3D12CreateRootSignatureDeserializer(const void *data, SIZE_T data_size,
        REFIID iid, void **deserializer)
{
    TRACE("data %p, data_size %Iu, iid %s, deserializer %p.\n",
            data, data_size, debugstr_guid(iid), deserializer);

    return vkd3d_create_root_signature_deserializer(data, data_size, iid, deserializer);
}

HRESULT WINAPI D3D12CreateVersionedRootSignatureDeserializer(const void *data, SIZE_T data_size,
        REFIID iid, void **deserializer)
{
    TRACE("data %p, data_size %Iu, iid %s, deserializer %p.\n",
            data, data_size, debugstr_guid(iid), deserializer);

    return vkd3d_create_versioned_root_signature_deserializer(data, data_size, iid, deserializer);
}

HRESULT WINAPI D3D12SerializeRootSignature(const D3D12_ROOT_SIGNATURE_DESC *root_signature_desc,
        D3D_ROOT_SIGNATURE_VERSION version, ID3DBlob **blob, ID3DBlob **error_blob)
{
    TRACE("root_signature_desc %p, version %#x, blob %p, error_blob %p.\n",
            root_signature_desc, version, blob, error_blob);

    return vkd3d_serialize_root_signature(root_signature_desc, version, blob, error_blob);
}

HRESULT WINAPI D3D12SerializeVersionedRootSignature(const D3D12_VERSIONED_ROOT_SIGNATURE_DESC *desc,
        ID3DBlob **blob, ID3DBlob **error_blob)
{
    TRACE("desc %p, blob %p, error_blob %p.\n", desc, blob, error_blob);

    return vkd3d_serialize_versioned_root_signature(desc, blob, error_blob);
}
