/*
 * Copyright 2008 Henri Verbeet for CodeWeavers
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

#include "dxgi_private.h"

#define VKD3D_NO_VULKAN_H
#define VKD3D_NO_WIN32_TYPES
#include "wine/vulkan.h"
#include <vkd3d.h>

WINE_DEFAULT_DEBUG_CHANNEL(dxgi);
WINE_DECLARE_DEBUG_CHANNEL(winediag);

static DXGI_SWAP_EFFECT dxgi_swap_effect_from_wined3d(enum wined3d_swap_effect swap_effect)
{
    switch (swap_effect)
    {
        case WINED3D_SWAP_EFFECT_DISCARD:
            return DXGI_SWAP_EFFECT_DISCARD;
        case WINED3D_SWAP_EFFECT_SEQUENTIAL:
            return DXGI_SWAP_EFFECT_SEQUENTIAL;
        case WINED3D_SWAP_EFFECT_FLIP_DISCARD:
            return DXGI_SWAP_EFFECT_FLIP_DISCARD;
        case WINED3D_SWAP_EFFECT_FLIP_SEQUENTIAL:
            return DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        default:
            FIXME("Invalid swap effect %#x.\n", swap_effect);
            return DXGI_SWAP_EFFECT_DISCARD;
    }
}

static BOOL dxgi_validate_flip_swap_effect_format(DXGI_FORMAT format)
{
    switch (format)
    {
        case DXGI_FORMAT_R16G16B16A16_FLOAT:
        case DXGI_FORMAT_R10G10B10A2_UNORM:
        case DXGI_FORMAT_R8G8B8A8_UNORM:
        case DXGI_FORMAT_B8G8R8A8_UNORM:
            return TRUE;
        default:
            WARN("Invalid swapchain format %#x for flip presentation model.\n", format);
            return FALSE;
    }
}

BOOL dxgi_validate_swapchain_desc(const DXGI_SWAP_CHAIN_DESC1 *desc)
{
    unsigned int min_buffer_count;

    switch (desc->SwapEffect)
    {
        case DXGI_SWAP_EFFECT_DISCARD:
        case DXGI_SWAP_EFFECT_SEQUENTIAL:
            min_buffer_count = 1;
            break;

        case DXGI_SWAP_EFFECT_FLIP_DISCARD:
        case DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL:
            min_buffer_count = 2;

            if (desc->Format && !dxgi_validate_flip_swap_effect_format(desc->Format))
                return FALSE;

            if (desc->SampleDesc.Count != 1 || desc->SampleDesc.Quality)
            {
                WARN("Invalid sample desc %u, %u for swap effect %#x.\n",
                        desc->SampleDesc.Count, desc->SampleDesc.Quality, desc->SwapEffect);
                return FALSE;
            }
            break;

        default:
            WARN("Invalid swap effect %u used.\n", desc->SwapEffect);
            return FALSE;
    }

    if (desc->BufferCount < min_buffer_count || desc->BufferCount > DXGI_MAX_SWAP_CHAIN_BUFFERS)
    {
        WARN("BufferCount is %u.\n", desc->BufferCount);
        return FALSE;
    }

    return TRUE;
}

BOOL dxgi_validate_swapchain_fullscreen_desc(const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *desc)
{
    if (desc && !desc->Windowed)
    {
        if (!(desc->ScanlineOrdering >= DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED
                && desc->ScanlineOrdering <= DXGI_MODE_SCANLINE_ORDER_LOWER_FIELD_FIRST))
        {
            WARN("Invalid scaline order %#x.\n", desc->ScanlineOrdering);
            return FALSE;
        }

        if (!(desc->Scaling >= DXGI_MODE_SCALING_UNSPECIFIED
                && desc->Scaling <= DXGI_MODE_SCALING_STRETCHED))
        {
            WARN("Invalid scaling %#x.\n", desc->Scaling);
            return FALSE;
        }
    }

    return TRUE;
}

HRESULT dxgi_get_output_from_window(IWineDXGIFactory *factory, HWND window, IDXGIOutput **dxgi_output)
{
    unsigned int adapter_idx, output_idx;
    DXGI_OUTPUT_DESC desc;
    IDXGIAdapter *adapter;
    IDXGIOutput *output;
    HMONITOR monitor;
    HRESULT hr;

    if (!(monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST)))
    {
        WARN("Failed to get monitor from window; trying adapter 0 output 0.\n");
        if (SUCCEEDED(IWineDXGIFactory_EnumAdapters(factory, 0, &adapter)))
        {
            if (SUCCEEDED(IDXGIAdapter_EnumOutputs(adapter, 0, &output)))
            {
                *dxgi_output = output;
                IDXGIAdapter_Release(adapter);
                return S_OK;
            }
            IDXGIAdapter_Release(adapter);
        }
        return DXGI_ERROR_INVALID_CALL;
    }

    for (adapter_idx = 0; SUCCEEDED(hr = IWineDXGIFactory_EnumAdapters(factory, adapter_idx, &adapter));
            ++adapter_idx)
    {
        for (output_idx = 0; SUCCEEDED(hr = IDXGIAdapter_EnumOutputs(adapter, output_idx,
                &output)); ++output_idx)
        {
            if (FAILED(hr = IDXGIOutput_GetDesc(output, &desc)))
            {
                WARN("Adapter %u output %u: Failed to get output desc, hr %#lx.\n", adapter_idx,
                        output_idx, hr);
                IDXGIOutput_Release(output);
                continue;
            }

            if (desc.Monitor == monitor)
            {
                *dxgi_output = output;
                IDXGIAdapter_Release(adapter);
                return S_OK;
            }

            IDXGIOutput_Release(output);
        }
        IDXGIAdapter_Release(adapter);
    }

    if (hr != DXGI_ERROR_NOT_FOUND)
        WARN("Failed to enumerate outputs, hr %#lx.\n", hr);

    if (SUCCEEDED(IWineDXGIFactory_EnumAdapters(factory, 0, &adapter)))
    {
        if (SUCCEEDED(IDXGIAdapter_EnumOutputs(adapter, 0, &output)))
        {
            WARN("Monitor %p not matched; falling back to adapter 0 output 0.\n", monitor);
            *dxgi_output = output;
            IDXGIAdapter_Release(adapter);
            return S_OK;
        }
        IDXGIAdapter_Release(adapter);
    }

    WARN("Output could not be found.\n");
    return DXGI_ERROR_NOT_FOUND;
}

static HRESULT dxgi_swapchain_resize_target(struct wined3d_swapchain_state *state,
        const DXGI_MODE_DESC *target_mode_desc)
{
    struct wined3d_display_mode mode;

    if (!target_mode_desc)
    {
        WARN("Invalid pointer.\n");
        return DXGI_ERROR_INVALID_CALL;
    }

    TRACE("Mode: %s.\n", debug_dxgi_mode(target_mode_desc));

    if (target_mode_desc->Scaling)
        FIXME("Ignoring scaling %#x.\n", target_mode_desc->Scaling);

    wined3d_display_mode_from_dxgi(&mode, target_mode_desc);

    return wined3d_swapchain_state_resize_target(state, &mode);
}

static HWND d3d11_swapchain_get_hwnd(struct d3d11_swapchain *swapchain)
{
    struct wined3d_swapchain_desc wined3d_desc;

    wined3d_mutex_lock();
    wined3d_swapchain_get_desc(swapchain->wined3d_swapchain, &wined3d_desc);
    wined3d_mutex_unlock();

    return wined3d_desc.device_window;
}

static inline struct d3d11_swapchain *d3d11_swapchain_from_IDXGISwapChain4(IDXGISwapChain4 *iface)
{
    return CONTAINING_RECORD(iface, struct d3d11_swapchain, IDXGISwapChain4_iface);
}

/* Accessor for composing DLLs (DirectComposition): the wined3d swapchain
 * behind a DXGI swapchain, so the compositor can present it directly to the
 * window drawable instead of copying through GDI. */
struct wined3d_swapchain *dxgi_get_wined3d_swapchain(struct IDXGISwapChain *swapchain)
{
    struct d3d11_swapchain *impl;
    IDXGISwapChain4 *iface4;

    if (FAILED(IDXGISwapChain_QueryInterface(swapchain, &IID_IDXGISwapChain4, (void **)&iface4)))
        return NULL;
    impl = d3d11_swapchain_from_IDXGISwapChain4(iface4);
    IDXGISwapChain4_Release(iface4);
    return impl->wined3d_swapchain;
}

/* Exported by dcomp (dcomp.spec). Signal DirectComposition that a composition
 * swapchain pushed a new frame, so its composite thread re-composites the
 * composition target and presents it without waiting for an app
 * IDCompositionDevice::Commit(). Dynamically resolved via GetModuleHandleW so
 * dxgi.dll has no circular hard import on dcomp.dll and degrades to a no-op if
 * a native/overridden dcomp.dll lacks the export. */
static void dxgi_notify_dcomp_presented( void *swapchain )
{
    typedef void (WINAPI *pfn_wine_dcomp_swapchain_presented)( void * );
    static pfn_wine_dcomp_swapchain_presented pfn;
    static BOOL resolved;

    if (!resolved)
    {
        HMODULE dcomp = GetModuleHandleW( L"dcomp.dll" );
        if (dcomp)
        {
            pfn = (pfn_wine_dcomp_swapchain_presented)GetProcAddress( dcomp, "wine_dcomp_swapchain_presented" );
            resolved = TRUE;
        }
    }
    if (pfn)
        pfn( swapchain );
}

static HRESULT STDMETHODCALLTYPE d3d11_swapchain_GetBuffer(IDXGISwapChain4 *iface,
        UINT buffer_idx, REFIID riid, void **surface);

/* IUnknown methods */

static const GUID IID_IDXGISwapChainPartner_local =
    {0x7652c85a, 0x0df5, 0x4567, {0xbc, 0xdd, 0x81, 0x76, 0xa3, 0x32, 0xb9, 0x9b}};

static inline struct d3d11_swapchain *impl_from_IDXGISwapChainPartner(IUnknown *iface)
{
    return CONTAINING_RECORD(iface, struct d3d11_swapchain, IDXGISwapChainPartner_iface);
}

static HRESULT STDMETHODCALLTYPE dxgi_swapchain_partner_QueryInterface(IUnknown *iface, REFIID riid, void **out)
{
    struct d3d11_swapchain *sc = impl_from_IDXGISwapChainPartner(iface);
    return IDXGISwapChain4_QueryInterface(&sc->IDXGISwapChain4_iface, riid, out);
}

static ULONG STDMETHODCALLTYPE dxgi_swapchain_partner_AddRef(IUnknown *iface)
{
    struct d3d11_swapchain *sc = impl_from_IDXGISwapChainPartner(iface);
    return IDXGISwapChain4_AddRef(&sc->IDXGISwapChain4_iface);
}

static ULONG STDMETHODCALLTYPE dxgi_swapchain_partner_Release(IUnknown *iface)
{
    struct d3d11_swapchain *sc = impl_from_IDXGISwapChainPartner(iface);
    return IDXGISwapChain4_Release(&sc->IDXGISwapChain4_iface);
}

static HRESULT STDMETHODCALLTYPE dxgi_swapchain_partner_GetCompositionSurfaceHandle(IUnknown *iface, HANDLE *handle)
{
    struct d3d11_swapchain *sc = impl_from_IDXGISwapChainPartner(iface);
    IDXGIResource1 *res = NULL;
    HRESULT hr;

    TRACE("iface %p, handle %p.\n", iface, handle);
    if (!handle)
        return E_POINTER;
    *handle = NULL;

    hr = d3d11_swapchain_GetBuffer(&sc->IDXGISwapChain4_iface, 0, &IID_IDXGIResource1, (void **)&res);
    if (SUCCEEDED(hr) && res)
    {
        hr = dxgi_resource_create_composition_handle(res, handle);
        IDXGIResource1_Release(res);
        if (SUCCEEDED(hr))
        {
            sc->is_composition_surface = TRUE;
            return S_OK;
        }
    }

    WARN("Failed to create composition surface handle from buffer 0 (hr %#lx), falling back to event.\n", hr);
    *handle = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!*handle)
        return HRESULT_FROM_WIN32(GetLastError());
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dxgi_swapchain_partner_stub(IUnknown *iface)
{
    FIXME("iface %p stub!\n", iface);
    return S_OK;
}

static const void * const dxgi_swapchain_partner_vtbl[] =
{
    dxgi_swapchain_partner_QueryInterface,
    dxgi_swapchain_partner_AddRef,
    dxgi_swapchain_partner_Release,
    dxgi_swapchain_partner_stub, /* 3 */
    dxgi_swapchain_partner_stub, /* 4 */
    dxgi_swapchain_partner_stub, /* 5 */
    dxgi_swapchain_partner_stub, /* 6 */
    dxgi_swapchain_partner_stub, /* 7 */
    dxgi_swapchain_partner_stub, /* 8 */
    dxgi_swapchain_partner_stub, /* 9 */
    dxgi_swapchain_partner_stub, /* 10 */
    dxgi_swapchain_partner_GetCompositionSurfaceHandle, /* 11 (+0x58) */
    dxgi_swapchain_partner_stub, /* 12 */
    dxgi_swapchain_partner_stub, /* 13 */
    dxgi_swapchain_partner_stub, /* 14 */
    dxgi_swapchain_partner_stub, /* 15 */
};

static HRESULT STDMETHODCALLTYPE d3d11_swapchain_QueryInterface(IDXGISwapChain4 *iface, REFIID riid, void **object)
{
    struct d3d11_swapchain *swapchain = CONTAINING_RECORD(iface, struct d3d11_swapchain, IDXGISwapChain4_iface);

    TRACE("iface %p, riid %s, object %p\n", iface, debugstr_guid(riid), object);

    if (IsEqualGUID(riid, &IID_IDXGISwapChainPartner_local))
    {
        IUnknown_AddRef(iface);
        *object = &swapchain->IDXGISwapChainPartner_iface;
        return S_OK;
    }

    if (IsEqualGUID(riid, &IID_IUnknown)
            || IsEqualGUID(riid, &IID_IDXGIObject)
            || IsEqualGUID(riid, &IID_IDXGIDeviceSubObject)
            || IsEqualGUID(riid, &IID_IDXGISwapChain)
            || IsEqualGUID(riid, &IID_IDXGISwapChain1)
            || IsEqualGUID(riid, &IID_IDXGISwapChain2)
            || IsEqualGUID(riid, &IID_IDXGISwapChain3)
            || IsEqualGUID(riid, &IID_IDXGISwapChain4))
    {
        IUnknown_AddRef(iface);
        *object = iface;
        return S_OK;
    }

    if (IsEqualGUID(riid, &IID_IDXGIResource)
            || IsEqualGUID(riid, &IID_IDXGIResource1)
            || IsEqualGUID(riid, &IID_IDXGISurface2)
            || IsEqualGUID(riid, &IID_IDXGISurface1)
            || IsEqualGUID(riid, &IID_IDXGISurface))
    {
        return d3d11_swapchain_GetBuffer(iface, 0, riid, object);
    }

    WARN("%s not implemented, returning E_NOINTERFACE\n", debugstr_guid(riid));

    *object = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d3d11_swapchain_AddRef(IDXGISwapChain4 *iface)
{
    struct d3d11_swapchain *swapchain = d3d11_swapchain_from_IDXGISwapChain4(iface);
    ULONG refcount = InterlockedIncrement(&swapchain->refcount);

    TRACE("%p increasing refcount to %lu.\n", swapchain, refcount);

    if (refcount == 1)
        wined3d_swapchain_incref(swapchain->wined3d_swapchain);

    return refcount;
}

static ULONG STDMETHODCALLTYPE d3d11_swapchain_Release(IDXGISwapChain4 *iface)
{
    struct d3d11_swapchain *swapchain = d3d11_swapchain_from_IDXGISwapChain4(iface);
    ULONG refcount = InterlockedDecrement(&swapchain->refcount);

    TRACE("%p decreasing refcount to %lu.\n", swapchain, refcount);

    if (!refcount)
    {
        IWineDXGIDevice *device = swapchain->device;
        ID3D11Texture2D *snap_to_release[3];
        unsigned int i;

        AcquireSRWLockExclusive(&swapchain->snap_lock);
        for (i = 0; i < 3; ++i)
        {
            snap_to_release[i] = swapchain->snap_tex[i];
            swapchain->snap_tex[i] = NULL;
        }
        swapchain->snap_valid = FALSE;
        ReleaseSRWLockExclusive(&swapchain->snap_lock);

        for (i = 0; i < 3; ++i)
        {
            if (snap_to_release[i])
                ID3D11Texture2D_Release(snap_to_release[i]);
        }

        if (swapchain->zc_tex)
        {
            wined3d_mutex_lock();
            wined3d_texture_decref(swapchain->zc_tex);
            wined3d_mutex_unlock();
            swapchain->zc_tex = NULL;
        }
        if (swapchain->zc_nt_handle || swapchain->zc_vk_image || swapchain->zc_vk_memory)
        {
            extern void dxgi_vk_free_shared_image(UINT64 vk_image, UINT64 vk_memory, HANDLE nt_handle);
            dxgi_vk_free_shared_image(swapchain->zc_vk_image, swapchain->zc_vk_memory, swapchain->zc_nt_handle);
            swapchain->zc_vk_image = 0;
            swapchain->zc_vk_memory = 0;
            swapchain->zc_nt_handle = NULL;
        }
        if (swapchain->shm_staging)
        {
            wined3d_mutex_lock();
            wined3d_texture_decref(swapchain->shm_staging);
            wined3d_mutex_unlock();
            swapchain->shm_staging = NULL;
        }
        if (swapchain->shm_ptr)
        {
            UnmapViewOfFile(swapchain->shm_ptr);
            swapchain->shm_ptr = NULL;
        }
        if (swapchain->shm_map)
        {
            CloseHandle(swapchain->shm_map);
            swapchain->shm_map = NULL;
        }
        if (swapchain->target)
        {
            WARN("Releasing fullscreen swapchain.\n");
            IDXGIOutput_Release(swapchain->target);
        }
        IWineDXGIFactory_Release(swapchain->factory);
        wined3d_swapchain_decref(swapchain->wined3d_swapchain);
        IWineDXGIDevice_Release(device);
    }

    return refcount;
}

/* IDXGIObject methods */

static HRESULT STDMETHODCALLTYPE d3d11_swapchain_SetPrivateData(IDXGISwapChain4 *iface,
        REFGUID guid, UINT data_size, const void *data)
{
    struct d3d11_swapchain *swapchain = d3d11_swapchain_from_IDXGISwapChain4(iface);

    TRACE("iface %p, guid %s, data_size %u, data %p.\n", iface, debugstr_guid(guid), data_size, data);

    return dxgi_set_private_data(&swapchain->private_store, guid, data_size, data);
}

static HRESULT STDMETHODCALLTYPE d3d11_swapchain_SetPrivateDataInterface(IDXGISwapChain4 *iface,
        REFGUID guid, const IUnknown *object)
{
    struct d3d11_swapchain *swapchain = d3d11_swapchain_from_IDXGISwapChain4(iface);

    TRACE("iface %p, guid %s, object %p.\n", iface, debugstr_guid(guid), object);

    return dxgi_set_private_data_interface(&swapchain->private_store, guid, object);
}

static HRESULT STDMETHODCALLTYPE d3d11_swapchain_GetPrivateData(IDXGISwapChain4 *iface,
        REFGUID guid, UINT *data_size, void *data)
{
    struct d3d11_swapchain *swapchain = d3d11_swapchain_from_IDXGISwapChain4(iface);

    TRACE("iface %p, guid %s, data_size %p, data %p.\n", iface, debugstr_guid(guid), data_size, data);

    return dxgi_get_private_data(&swapchain->private_store, guid, data_size, data);
}

static HRESULT STDMETHODCALLTYPE d3d11_swapchain_GetParent(IDXGISwapChain4 *iface, REFIID riid, void **parent)
{
    struct d3d11_swapchain *swapchain = d3d11_swapchain_from_IDXGISwapChain4(iface);

    TRACE("iface %p, riid %s, parent %p.\n", iface, debugstr_guid(riid), parent);

    return IWineDXGIFactory_QueryInterface(swapchain->factory, riid, parent);
}

/* IDXGIDeviceSubObject methods */

static HRESULT STDMETHODCALLTYPE d3d11_swapchain_GetDevice(IDXGISwapChain4 *iface, REFIID riid, void **device)
{
    struct d3d11_swapchain *swapchain = d3d11_swapchain_from_IDXGISwapChain4(iface);

    TRACE("iface %p, riid %s, device %p.\n", iface, debugstr_guid(riid), device);

    return IWineDXGIDevice_QueryInterface(swapchain->device, riid, device);
}

/* IDXGISwapChain1 methods */

#define WINE_DCOMP_ZCGP_MAGIC 0x5A434750u /* 'ZCGP' */

static struct
{
    LONG init_state; /* 0=uninit, 1=ok, -1=failed */
    CRITICAL_SECTION cs;
    HMODULE vk_mod;
    VkInstance instance;
    VkPhysicalDevice phys_dev;
    VkDevice device;
    VkPhysicalDeviceMemoryProperties mem_props;
    PFN_vkCreateImage p_vkCreateImage;
    PFN_vkDestroyImage p_vkDestroyImage;
    PFN_vkGetImageMemoryRequirements p_vkGetImageMemoryRequirements;
    PFN_vkAllocateMemory p_vkAllocateMemory;
    PFN_vkFreeMemory p_vkFreeMemory;
    PFN_vkBindImageMemory p_vkBindImageMemory;
    PFN_vkGetMemoryWin32HandleKHR p_vkGetMemoryWin32HandleKHR;
} dxgi_vk_exp;

static BOOL dxgi_vk_exporter_init(void)
{
    PFN_vkCreateInstance p_vkCreateInstance;
    PFN_vkEnumeratePhysicalDevices p_vkEnumeratePhysicalDevices;
    PFN_vkGetPhysicalDeviceMemoryProperties p_vkGetPhysicalDeviceMemoryProperties;
    PFN_vkCreateDevice p_vkCreateDevice;
    PFN_vkGetDeviceProcAddr p_vkGetDeviceProcAddr;
    VkApplicationInfo app_info = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
    VkInstanceCreateInfo inst_info = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    VkDeviceQueueCreateInfo queue_info = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
    VkDeviceCreateInfo dev_info = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    const char *dev_exts[] = { VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME };
    float queue_prio = 1.0f;
    VkPhysicalDevice phys[8];
    uint32_t count = ARRAY_SIZE(phys);
    static LONG cs_init = 0;

    if (InterlockedCompareExchange(&dxgi_vk_exp.init_state, 0, 0) == 1)
        return TRUE;
    if (InterlockedCompareExchange(&dxgi_vk_exp.init_state, 0, 0) == -1)
        return FALSE;

    if (InterlockedCompareExchange(&cs_init, 1, 0) == 0)
        InitializeCriticalSection(&dxgi_vk_exp.cs);
    else
        while (InterlockedCompareExchange(&cs_init, 0, 0) == 0) Sleep(0);

    EnterCriticalSection(&dxgi_vk_exp.cs);
    if (dxgi_vk_exp.init_state != 0)
    {
        BOOL ok = (dxgi_vk_exp.init_state == 1);
        LeaveCriticalSection(&dxgi_vk_exp.cs);
        return ok;
    }

    dxgi_vk_exp.vk_mod = LoadLibraryW(L"vulkan-1.dll");
    if (!dxgi_vk_exp.vk_mod)
        goto fail;

    p_vkCreateInstance = (void *)GetProcAddress(dxgi_vk_exp.vk_mod, "vkCreateInstance");
    p_vkEnumeratePhysicalDevices = (void *)GetProcAddress(dxgi_vk_exp.vk_mod, "vkEnumeratePhysicalDevices");
    p_vkGetPhysicalDeviceMemoryProperties = (void *)GetProcAddress(dxgi_vk_exp.vk_mod, "vkGetPhysicalDeviceMemoryProperties");
    p_vkCreateDevice = (void *)GetProcAddress(dxgi_vk_exp.vk_mod, "vkCreateDevice");
    p_vkGetDeviceProcAddr = (void *)GetProcAddress(dxgi_vk_exp.vk_mod, "vkGetDeviceProcAddr");
    if (!p_vkCreateInstance || !p_vkEnumeratePhysicalDevices || !p_vkGetPhysicalDeviceMemoryProperties
            || !p_vkCreateDevice || !p_vkGetDeviceProcAddr)
        goto fail;

    app_info.apiVersion = VK_API_VERSION_1_1;
    inst_info.pApplicationInfo = &app_info;
    if (p_vkCreateInstance(&inst_info, NULL, &dxgi_vk_exp.instance) != VK_SUCCESS)
        goto fail;

    if (p_vkEnumeratePhysicalDevices(dxgi_vk_exp.instance, &count, phys) < VK_SUCCESS || !count)
        goto fail;
    dxgi_vk_exp.phys_dev = phys[0];
    p_vkGetPhysicalDeviceMemoryProperties(dxgi_vk_exp.phys_dev, &dxgi_vk_exp.mem_props);

    queue_info.queueFamilyIndex = 0;
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = &queue_prio;
    dev_info.queueCreateInfoCount = 1;
    dev_info.pQueueCreateInfos = &queue_info;
    dev_info.enabledExtensionCount = ARRAY_SIZE(dev_exts);
    dev_info.ppEnabledExtensionNames = dev_exts;
    if (p_vkCreateDevice(dxgi_vk_exp.phys_dev, &dev_info, NULL, &dxgi_vk_exp.device) != VK_SUCCESS)
        goto fail;

    dxgi_vk_exp.p_vkCreateImage = (void *)p_vkGetDeviceProcAddr(dxgi_vk_exp.device, "vkCreateImage");
    dxgi_vk_exp.p_vkDestroyImage = (void *)p_vkGetDeviceProcAddr(dxgi_vk_exp.device, "vkDestroyImage");
    dxgi_vk_exp.p_vkGetImageMemoryRequirements = (void *)p_vkGetDeviceProcAddr(dxgi_vk_exp.device, "vkGetImageMemoryRequirements");
    dxgi_vk_exp.p_vkAllocateMemory = (void *)p_vkGetDeviceProcAddr(dxgi_vk_exp.device, "vkAllocateMemory");
    dxgi_vk_exp.p_vkFreeMemory = (void *)p_vkGetDeviceProcAddr(dxgi_vk_exp.device, "vkFreeMemory");
    dxgi_vk_exp.p_vkBindImageMemory = (void *)p_vkGetDeviceProcAddr(dxgi_vk_exp.device, "vkBindImageMemory");
    dxgi_vk_exp.p_vkGetMemoryWin32HandleKHR = (void *)p_vkGetDeviceProcAddr(dxgi_vk_exp.device, "vkGetMemoryWin32HandleKHR");
    if (!dxgi_vk_exp.p_vkCreateImage || !dxgi_vk_exp.p_vkDestroyImage || !dxgi_vk_exp.p_vkGetImageMemoryRequirements
            || !dxgi_vk_exp.p_vkAllocateMemory || !dxgi_vk_exp.p_vkFreeMemory || !dxgi_vk_exp.p_vkBindImageMemory
            || !dxgi_vk_exp.p_vkGetMemoryWin32HandleKHR)
        goto fail;

    InterlockedExchange(&dxgi_vk_exp.init_state, 1);
    LeaveCriticalSection(&dxgi_vk_exp.cs);
    TRACE("Initialized Vulkan external-memory zero-copy exporter.\n");
    return TRUE;

fail:
    InterlockedExchange(&dxgi_vk_exp.init_state, -1);
    LeaveCriticalSection(&dxgi_vk_exp.cs);
    WARN("Vulkan external-memory exporter unavailable; using SHM fallback.\n");
    return FALSE;
}

static BOOL dxgi_vk_alloc_shared_image(UINT width, UINT height, UINT64 *out_image,
        UINT64 *out_memory, HANDLE *out_handle, UINT64 *out_alloc_size)
{
    VkExternalMemoryImageCreateInfo ext_img_info = { VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO };
    VkImageCreateInfo img_info = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    VkExportMemoryAllocateInfo exp_mem_info = { VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO };
    VkMemoryDedicatedAllocateInfo ded_mem_info = { VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO };
    VkMemoryAllocateInfo alloc_info = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    VkMemoryGetWin32HandleInfoKHR get_h_info = { VK_STRUCTURE_TYPE_MEMORY_GET_WIN32_HANDLE_INFO_KHR };
    VkMemoryRequirements mem_reqs;
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory mem = VK_NULL_HANDLE;
    HANDLE nt_handle = NULL;
    uint32_t mem_type = ~0u, i;

    if (!dxgi_vk_exporter_init())
        return FALSE;

    ext_img_info.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
    img_info.pNext = &ext_img_info;
    img_info.imageType = VK_IMAGE_TYPE_2D;
    img_info.format = VK_FORMAT_B8G8R8A8_UNORM;
    img_info.extent.width = width;
    img_info.extent.height = height;
    img_info.extent.depth = 1;
    img_info.mipLevels = 1;
    img_info.arrayLayers = 1;
    img_info.samples = VK_SAMPLE_COUNT_1_BIT;
    img_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    img_info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
            | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    img_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    img_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (dxgi_vk_exp.p_vkCreateImage(dxgi_vk_exp.device, &img_info, NULL, &image) != VK_SUCCESS)
        return FALSE;

    dxgi_vk_exp.p_vkGetImageMemoryRequirements(dxgi_vk_exp.device, image, &mem_reqs);
    for (i = 0; i < dxgi_vk_exp.mem_props.memoryTypeCount; i++)
    {
        if ((mem_reqs.memoryTypeBits & (1u << i))
                && (dxgi_vk_exp.mem_props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
        {
            mem_type = i;
            break;
        }
    }
    if (mem_type == ~0u)
    {
        for (i = 0; i < dxgi_vk_exp.mem_props.memoryTypeCount; i++)
        {
            if (mem_reqs.memoryTypeBits & (1u << i)) { mem_type = i; break; }
        }
    }
    if (mem_type == ~0u)
    {
        dxgi_vk_exp.p_vkDestroyImage(dxgi_vk_exp.device, image, NULL);
        return FALSE;
    }

    ded_mem_info.image = image;
    exp_mem_info.pNext = &ded_mem_info;
    exp_mem_info.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
    alloc_info.pNext = &exp_mem_info;
    alloc_info.allocationSize = mem_reqs.size;
    alloc_info.memoryTypeIndex = mem_type;

    if (dxgi_vk_exp.p_vkAllocateMemory(dxgi_vk_exp.device, &alloc_info, NULL, &mem) != VK_SUCCESS)
    {
        dxgi_vk_exp.p_vkDestroyImage(dxgi_vk_exp.device, image, NULL);
        return FALSE;
    }

    if (dxgi_vk_exp.p_vkBindImageMemory(dxgi_vk_exp.device, image, mem, 0) != VK_SUCCESS)
    {
        dxgi_vk_exp.p_vkFreeMemory(dxgi_vk_exp.device, mem, NULL);
        dxgi_vk_exp.p_vkDestroyImage(dxgi_vk_exp.device, image, NULL);
        return FALSE;
    }

    get_h_info.memory = mem;
    get_h_info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
    if (dxgi_vk_exp.p_vkGetMemoryWin32HandleKHR(dxgi_vk_exp.device, &get_h_info, &nt_handle) != VK_SUCCESS || !nt_handle)
    {
        dxgi_vk_exp.p_vkFreeMemory(dxgi_vk_exp.device, mem, NULL);
        dxgi_vk_exp.p_vkDestroyImage(dxgi_vk_exp.device, image, NULL);
        return FALSE;
    }

    *out_image = (UINT64)(ULONG_PTR)image;
    *out_memory = (UINT64)(ULONG_PTR)mem;
    *out_handle = nt_handle;
    *out_alloc_size = mem_reqs.size;
    return TRUE;
}

void dxgi_vk_free_shared_image(UINT64 vk_image, UINT64 vk_memory, HANDLE nt_handle)
{
    if (nt_handle)
        CloseHandle(nt_handle);
    if (InterlockedCompareExchange(&dxgi_vk_exp.init_state, 0, 0) == 1)
    {
        if (vk_image)
            dxgi_vk_exp.p_vkDestroyImage(dxgi_vk_exp.device, (VkImage)(ULONG_PTR)vk_image, NULL);
        if (vk_memory)
            dxgi_vk_exp.p_vkFreeMemory(dxgi_vk_exp.device, (VkDeviceMemory)(ULONG_PTR)vk_memory, NULL);
    }
}

static void STDMETHODCALLTYPE shm_staging_destroyed(void *parent) {}
static const struct wined3d_parent_ops shm_staging_parent_ops = { shm_staging_destroyed };
static LONG g_zc_alloc_gen = 0;

#define WINE_DCOMP_SHM_HEADER_SIZE 64

static BOOL d3d11_swapchain_export_shm(struct d3d11_swapchain *swapchain)
{
    IDXGIResource1 *res = NULL;
    HANDLE kmt;
    struct wined3d_texture *src_tex;
    struct wined3d_resource *src_res, *dst_res;
    struct wined3d_resource_desc rdesc;
    struct wined3d_device *wined3d_device;
    struct wined3d_device_context *ctx;
    struct wined3d_map_desc map_desc;
    SIZE_T needed;
    UINT *hdr;
    BYTE *dst_px;
    UINT y, row_bytes;
    HRESULT hr;

    if (FAILED(d3d11_swapchain_GetBuffer(&swapchain->IDXGISwapChain4_iface, 0, &IID_IDXGIResource1, (void **)&res)) || !res)
        return FALSE;
    kmt = dxgi_resource_get_existing_kmt_handle(res);
    if (!kmt && (swapchain->shm_kmt_handle || swapchain->is_composition_surface))
        IDXGIResource1_GetSharedHandle(res, &kmt);
    IDXGIResource1_Release(res);
    if (!kmt)
        kmt = swapchain->shm_kmt_handle;
    if (!kmt)
        return FALSE;

    wined3d_mutex_lock();
    src_tex = wined3d_swapchain_get_back_buffer(swapchain->wined3d_swapchain, 0);
    if (!src_tex)
    {
        wined3d_mutex_unlock();
        return TRUE;
    }
    src_res = wined3d_texture_get_resource(src_tex);
    wined3d_resource_get_desc(src_res, &rdesc);
    if (!rdesc.width || !rdesc.height || rdesc.width > 4096 || rdesc.height > 2160)
    {
        wined3d_mutex_unlock();
        return TRUE;
    }

    row_bytes = rdesc.width * 4;
    needed = WINE_DCOMP_SHM_HEADER_SIZE + (SIZE_T)row_bytes * rdesc.height;
    if (swapchain->shm_kmt_handle != kmt || swapchain->shm_size < needed)
    {
        /* Reserve up to 4096x2160x4 + header (~35 MB virtual; only touched pages use RAM)
         * so ResizeBuffers (e.g. 1272x990 -> 2560x1440) never hits ERROR_ALREADY_EXISTS
         * with a smaller file mapping. */
        SIZE_T map_size = WINE_DCOMP_SHM_HEADER_SIZE + (SIZE_T)4096 * 2160 * 4;
        WCHAR name[64];
        if (needed > map_size) map_size = needed;
        if (swapchain->shm_ptr) { UnmapViewOfFile(swapchain->shm_ptr); swapchain->shm_ptr = NULL; }
        if (swapchain->shm_map) { CloseHandle(swapchain->shm_map); swapchain->shm_map = NULL; }
        swprintf(name, ARRAY_SIZE(name), L"Local\\wine_dcomp_shm_%08x", (UINT)(ULONG_PTR)kmt);
        swapchain->shm_map = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
                0, (DWORD)map_size, name);
        if (swapchain->shm_map)
            swapchain->shm_ptr = MapViewOfFile(swapchain->shm_map, FILE_MAP_WRITE, 0, 0, map_size);
        swapchain->shm_kmt_handle = kmt;
        swapchain->shm_size = swapchain->shm_ptr ? map_size : 0;
    }
    if (!swapchain->shm_ptr)
    {
        wined3d_mutex_unlock();
        return TRUE;
    }

    wined3d_device = wined3d_swapchain_get_device(swapchain->wined3d_swapchain);
    ctx = wined3d_device_get_immediate_context(wined3d_device);

    /* Fast path: Zero-copy GPU-to-GPU shared external memory (Vulkan -> GL_EXT_memory_object_win32). */
    if (!swapchain->zc_failed)
    {
        if (!swapchain->zc_tex || swapchain->zc_width != rdesc.width || swapchain->zc_height != rdesc.height)
        {
            struct wined3d_resource_desc zdesc = rdesc;
            UINT64 vk_img = 0, vk_mem = 0, alloc_sz = 0;
            HANDLE nt_h = NULL;
            struct wined3d_texture *ztex = NULL;

            if (swapchain->zc_tex)
            {
                wined3d_texture_decref(swapchain->zc_tex);
                swapchain->zc_tex = NULL;
            }
            if (swapchain->zc_nt_handle || swapchain->zc_vk_image || swapchain->zc_vk_memory)
            {
                dxgi_vk_free_shared_image(swapchain->zc_vk_image, swapchain->zc_vk_memory, swapchain->zc_nt_handle);
                swapchain->zc_vk_image = 0;
                swapchain->zc_vk_memory = 0;
                swapchain->zc_nt_handle = NULL;
            }

            zdesc.access = WINED3D_RESOURCE_ACCESS_GPU;
            zdesc.usage = 0;
            zdesc.bind_flags = WINED3D_BIND_SHADER_RESOURCE | WINED3D_BIND_RENDER_TARGET;
            zdesc.multisample_type = WINED3D_MULTISAMPLE_NONE;
            zdesc.multisample_quality = 0;

            if (dxgi_vk_alloc_shared_image(rdesc.width, rdesc.height, &vk_img, &vk_mem, &nt_h, &alloc_sz)
                    && SUCCEEDED(wined3d_texture_create(wined3d_device, &zdesc, 1, 1, 0, NULL, NULL,
                            &shm_staging_parent_ops, &ztex))
                    && SUCCEEDED(wined3d_texture_set_external_memory(ztex, nt_h, alloc_sz)))
            {
                swapchain->zc_tex = ztex;
                swapchain->zc_vk_image = vk_img;
                swapchain->zc_vk_memory = vk_mem;
                swapchain->zc_nt_handle = nt_h;
                swapchain->zc_alloc_size = alloc_sz;
                swapchain->zc_width = rdesc.width;
                swapchain->zc_height = rdesc.height;
                swapchain->zc_gen = (UINT)InterlockedIncrement(&g_zc_alloc_gen);
                TRACE("Zero-copy GPU surface allocated %ux%u (handle %p, size %s, gen %u).\n",
                        rdesc.width, rdesc.height, nt_h, wine_dbgstr_longlong(alloc_sz), swapchain->zc_gen);
            }
            else
            {
                if (ztex) wined3d_texture_decref(ztex);
                dxgi_vk_free_shared_image(vk_img, vk_mem, nt_h);
                swapchain->zc_failed = TRUE;
                WARN("Zero-copy GPU surface setup failed; falling back to POSIX SHM.\n");
            }
        }

        if (swapchain->zc_tex)
        {
            dst_res = wined3d_texture_get_resource(swapchain->zc_tex);
            wined3d_device_context_copy_resource(ctx, dst_res, src_res);
            wined3d_device_context_flush(ctx);

            hdr = (UINT *)swapchain->shm_ptr;
            InterlockedIncrement((LONG *)&hdr[3]);
            MemoryBarrier();
            hdr[0] = rdesc.width;
            hdr[1] = rdesc.height;
            hdr[2] = row_bytes;
            hdr[4] = WINE_DCOMP_ZCGP_MAGIC;
            hdr[5] = GetCurrentProcessId();
            hdr[6] = (UINT)(ULONG_PTR)swapchain->zc_nt_handle;
            hdr[7] = (UINT)swapchain->zc_alloc_size;
            hdr[8] = swapchain->zc_gen;
            MemoryBarrier();
            InterlockedIncrement((LONG *)&hdr[3]);

            wined3d_mutex_unlock();
            return TRUE;
        }
    }

    if (!swapchain->shm_staging || swapchain->shm_width != rdesc.width || swapchain->shm_height != rdesc.height)
    {
        struct wined3d_resource_desc sdesc = rdesc;
        if (swapchain->shm_staging)
        {
            wined3d_texture_decref(swapchain->shm_staging);
            swapchain->shm_staging = NULL;
        }
        sdesc.access = WINED3D_RESOURCE_ACCESS_CPU | WINED3D_RESOURCE_ACCESS_MAP_R;
        sdesc.usage = 0;
        sdesc.bind_flags = 0;
        sdesc.multisample_type = WINED3D_MULTISAMPLE_NONE;
        sdesc.multisample_quality = 0;
        if (FAILED(wined3d_texture_create(wined3d_device, &sdesc, 1, 1, 0, NULL, NULL,
                &shm_staging_parent_ops, &swapchain->shm_staging)))
        {
            wined3d_mutex_unlock();
            return TRUE;
        }
        swapchain->shm_width = rdesc.width;
        swapchain->shm_height = rdesc.height;
    }

    dst_res = wined3d_texture_get_resource(swapchain->shm_staging);
    wined3d_device_context_copy_resource(ctx, dst_res, src_res);
    hr = wined3d_device_context_map(ctx, dst_res, 0, &map_desc, NULL, WINED3D_MAP_READ);
    if (SUCCEEDED(hr) && map_desc.data)
    {
        hdr = (UINT *)swapchain->shm_ptr;
        dst_px = (BYTE *)swapchain->shm_ptr + WINE_DCOMP_SHM_HEADER_SIZE;
        /* Seqlock: odd = write in progress, even = complete stable frame. */
        InterlockedIncrement((LONG *)&hdr[3]);
        MemoryBarrier();
        hdr[0] = rdesc.width;
        hdr[1] = rdesc.height;
        hdr[2] = row_bytes;
        hdr[4] = 0;
        for (y = 0; y < rdesc.height; y++)
            memcpy(dst_px + (SIZE_T)y * row_bytes, (const BYTE *)map_desc.data + (SIZE_T)y * map_desc.row_pitch, row_bytes);
        MemoryBarrier();
        InterlockedIncrement((LONG *)&hdr[3]);
        wined3d_device_context_unmap(ctx, dst_res, 0);
    }
    wined3d_mutex_unlock();
    return TRUE;
}

static BOOL dxgi_swapchain_is_dcomp_captured(struct d3d11_swapchain *swapchain)
{
    HWND hwnd = d3d11_swapchain_get_hwnd(swapchain), root;

    if (!hwnd || !IsWindow(hwnd))
        return FALSE;
    if (GetPropW(hwnd, L"__wine_dcomp_captured_hwnd") || GetPropW(hwnd, L"__wine_dwm_cloaked"))
        return TRUE;
    root = GetAncestor(hwnd, GA_ROOT);
    if (root && root != hwnd
            && (GetPropW(root, L"__wine_dcomp_captured_hwnd") || GetPropW(root, L"__wine_dwm_cloaked")))
        return TRUE;
    return FALSE;
}

static void d3d11_swapchain_snapshot_present(struct d3d11_swapchain *swapchain)
{
    ID3D11Device *d3d_dev = NULL;
    ID3D11DeviceContext *d3d_ctx = NULL;
    ID3D11Texture2D *src = NULL, *dst = NULL;
    ID3D11Texture2D *to_release[3] = { NULL, NULL, NULL };
    D3D11_TEXTURE2D_DESC src_desc, dst_desc;
    UINT write_idx, rel_cnt = 0, i;
    HRESULT hr;

    if (FAILED(IWineDXGIDevice_QueryInterface(swapchain->device, &IID_ID3D11Device, (void **)&d3d_dev)) || !d3d_dev)
        return;

    ID3D11Device_GetImmediateContext(d3d_dev, &d3d_ctx);
    if (!d3d_ctx)
    {
        ID3D11Device_Release(d3d_dev);
        return;
    }

    if (FAILED(d3d11_swapchain_GetBuffer(&swapchain->IDXGISwapChain4_iface, 0,
            &IID_ID3D11Texture2D, (void **)&src)) || !src)
    {
        ID3D11DeviceContext_Release(d3d_ctx);
        ID3D11Device_Release(d3d_dev);
        return;
    }

    ID3D11Texture2D_GetDesc(src, &src_desc);
    if (!src_desc.Width || !src_desc.Height)
    {
        ID3D11Texture2D_Release(src);
        ID3D11DeviceContext_Release(d3d_ctx);
        ID3D11Device_Release(d3d_dev);
        return;
    }

    /* Never hold snap_lock across D3D11/wined3d calls to avoid lock inversion with wined3d_mutex. */
    AcquireSRWLockShared(&swapchain->snap_lock);
    write_idx = swapchain->snap_valid ? ((swapchain->snap_front + 1) % 3) : 0;
    if (swapchain->snap_tex[write_idx]
            && swapchain->snap_width == src_desc.Width
            && swapchain->snap_height == src_desc.Height
            && swapchain->snap_format == src_desc.Format)
    {
        dst = swapchain->snap_tex[write_idx];
        ID3D11Texture2D_AddRef(dst);
    }
    ReleaseSRWLockShared(&swapchain->snap_lock);

    if (!dst)
    {
        memset(&dst_desc, 0, sizeof(dst_desc));
        dst_desc.Width = src_desc.Width;
        dst_desc.Height = src_desc.Height;
        dst_desc.MipLevels = 1;
        dst_desc.ArraySize = 1;
        dst_desc.Format = src_desc.Format;
        dst_desc.SampleDesc.Count = 1;
        dst_desc.SampleDesc.Quality = 0;
        dst_desc.Usage = D3D11_USAGE_DEFAULT;
        dst_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        dst_desc.CPUAccessFlags = 0;
        dst_desc.MiscFlags = 0;

        hr = ID3D11Device_CreateTexture2D(d3d_dev, &dst_desc, NULL, &dst);
        if (FAILED(hr) || !dst)
        {
            WARN("Failed to create snapshot texture %ux%u fmt %u, hr %#lx.\n",
                    src_desc.Width, src_desc.Height, src_desc.Format, hr);
            ID3D11Texture2D_Release(src);
            ID3D11DeviceContext_Release(d3d_ctx);
            ID3D11Device_Release(d3d_dev);
            return;
        }
    }

    if (src_desc.SampleDesc.Count > 1)
        ID3D11DeviceContext_ResolveSubresource(d3d_ctx, (ID3D11Resource *)dst, 0,
                (ID3D11Resource *)src, 0, src_desc.Format);
    else
        ID3D11DeviceContext_CopyResource(d3d_ctx, (ID3D11Resource *)dst, (ID3D11Resource *)src);
    ID3D11DeviceContext_Flush(d3d_ctx);

    ID3D11Texture2D_Release(src);
    ID3D11DeviceContext_Release(d3d_ctx);
    ID3D11Device_Release(d3d_dev);

    AcquireSRWLockExclusive(&swapchain->snap_lock);
    if (swapchain->snap_width != src_desc.Width
            || swapchain->snap_height != src_desc.Height
            || swapchain->snap_format != src_desc.Format)
    {
        for (i = 0; i < 3; ++i)
        {
            if (i != write_idx && swapchain->snap_tex[i])
            {
                to_release[rel_cnt++] = swapchain->snap_tex[i];
                swapchain->snap_tex[i] = NULL;
            }
        }
    }
    if (swapchain->snap_tex[write_idx] != dst)
    {
        if (swapchain->snap_tex[write_idx])
            to_release[rel_cnt++] = swapchain->snap_tex[write_idx];
        swapchain->snap_tex[write_idx] = dst;
        dst = NULL;
    }
    swapchain->snap_width = src_desc.Width;
    swapchain->snap_height = src_desc.Height;
    swapchain->snap_format = src_desc.Format;
    swapchain->snap_front = write_idx;
    swapchain->snap_valid = TRUE;
    ReleaseSRWLockExclusive(&swapchain->snap_lock);

    if (dst)
        ID3D11Texture2D_Release(dst);
    for (i = 0; i < rel_cnt; ++i)
        ID3D11Texture2D_Release(to_release[i]);
}

static HRESULT d3d11_swapchain_present(struct d3d11_swapchain *swapchain,
        unsigned int sync_interval, unsigned int flags)
{
    HRESULT hr;

    TRACE("swapchain %p, hwnd %p, sync_interval %u, flags %#x.\n",
            swapchain, d3d11_swapchain_get_hwnd(swapchain), sync_interval, flags);

    if (sync_interval > 4)
    {
        WARN("Invalid sync interval %u.\n", sync_interval);
        return DXGI_ERROR_INVALID_CALL;
    }

    if (flags & DXGI_PRESENT_ALLOW_TEARING)
    {
        if (sync_interval || !swapchain->flip_model
                || !(swapchain->dxgi_flags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING))
        {
            WARN("DXGI_PRESENT_ALLOW_TEARING requires flip model, sync_interval 0, and DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING.\n");
            return DXGI_ERROR_INVALID_CALL;
        }
    }

    if (IsIconic(d3d11_swapchain_get_hwnd(swapchain)))
        return DXGI_STATUS_OCCLUDED;

    if (flags & ~(DXGI_PRESENT_TEST | DXGI_PRESENT_ALLOW_TEARING
            | DXGI_PRESENT_DO_NOT_SEQUENCE | DXGI_PRESENT_DO_NOT_WAIT
            | DXGI_PRESENT_RESTART | DXGI_PRESENT_USE_DURATION))
        FIXME("Unimplemented flags %#x.\n", flags);
    if (flags & DXGI_PRESENT_TEST)
    {
        TRACE("Returning S_OK for DXGI_PRESENT_TEST.\n");
        return S_OK;
    }

    swapchain->present_interval = sync_interval;

    if (d3d11_swapchain_export_shm(swapchain)
            || (swapchain->is_composition_surface && !IsWindowVisible(d3d11_swapchain_get_hwnd(swapchain)))
            || dxgi_swapchain_is_dcomp_captured(swapchain))
    {
        /* Composition, DComp-exported, or DComp-captured swapchain: pixels were
         * exported directly to DComp SHM; skip X11/GLX/Vulkan SwapBuffers on the
         * cloaked/offscreen HWND so XWayland never stalls waiting for wl_surface.frame. */
        d3d11_swapchain_snapshot_present(swapchain);
        if (!(flags & DXGI_PRESENT_DO_NOT_SEQUENCE))
            InterlockedIncrement(&swapchain->present_count);
        /* A composition swapchain (SwapChainPanel) has no window of its own: the
         * frame is on screen only once DirectComposition re-composites the target
         * that has this swapchain as content. dcomp normally re-composites on
         * IDCompositionDevice::Commit(); an app that only calls Present() between
         * commits (e.g. MS Paint strokes) leaves the window on the initial frame.
         * Notify dcomp so its composite thread re-composites now. */
        dxgi_notify_dcomp_presented( &swapchain->IDXGISwapChain4_iface );
        if (swapchain->dxgi_flags & DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT)
        {
            HANDLE waitable = wined3d_swapchain_get_frame_latency_waitable_object(swapchain->wined3d_swapchain);
            if (waitable)
            {
                if (!ReleaseSemaphore(waitable, 1, NULL))
                    ERR("Failed to release frame latency semaphore, last error %lu.\n", GetLastError());
                CloseHandle(waitable);
            }
        }
        if (sync_interval && !(flags & DXGI_PRESENT_DO_NOT_WAIT))
            Sleep(16);
        return S_OK;
    }

    if (SUCCEEDED(hr = wined3d_swapchain_present(swapchain->wined3d_swapchain,
            NULL, NULL, NULL, sync_interval, 0)))
    {
        if (!(flags & DXGI_PRESENT_DO_NOT_SEQUENCE))
        {
            InterlockedIncrement(&swapchain->present_count);
            if (swapchain->flip_model && swapchain->buffer_count > 1)
            {
                swapchain->front_buffer_index = swapchain->back_buffer_index;
                swapchain->back_buffer_index = (swapchain->back_buffer_index + 1) % swapchain->buffer_count;
            }
        }
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE DECLSPEC_HOTPATCH d3d11_swapchain_Present(IDXGISwapChain4 *iface, UINT sync_interval, UINT flags)
{
    struct d3d11_swapchain *swapchain = d3d11_swapchain_from_IDXGISwapChain4(iface);

    TRACE("iface %p, sync_interval %u, flags %#x.\n", iface, sync_interval, flags);

    return d3d11_swapchain_present(swapchain, sync_interval, flags);
}

static HRESULT STDMETHODCALLTYPE d3d11_swapchain_GetBuffer(IDXGISwapChain4 *iface,
        UINT buffer_idx, REFIID riid, void **surface)
{
    struct d3d11_swapchain *swapchain = d3d11_swapchain_from_IDXGISwapChain4(iface);
    struct wined3d_texture *texture;
    IUnknown *parent;
    HRESULT hr;

    TRACE("iface %p, buffer_idx %u, riid %s, surface %p\n",
            iface, buffer_idx, debugstr_guid(riid), surface);

    wined3d_mutex_lock();

    if (!(texture = wined3d_swapchain_get_back_buffer(swapchain->wined3d_swapchain, buffer_idx)))
    {
        wined3d_mutex_unlock();
        return DXGI_ERROR_INVALID_CALL;
    }

    parent = wined3d_texture_get_parent(texture);
    hr = IUnknown_QueryInterface(parent, riid, surface);
    wined3d_mutex_unlock();

    return hr;
}

static HRESULT STDMETHODCALLTYPE DECLSPEC_HOTPATCH d3d11_swapchain_SetFullscreenState(IDXGISwapChain4 *iface,
        BOOL fullscreen, IDXGIOutput *target)
{
    struct d3d11_swapchain *swapchain = d3d11_swapchain_from_IDXGISwapChain4(iface);
    struct wined3d_swapchain_desc swapchain_desc;
    struct wined3d_swapchain_state *state;
    struct dxgi_output *dxgi_output;
    LONG in_set_fullscreen_state;
    BOOL old_fs;
    HRESULT hr;

    TRACE("iface %p, fullscreen %#x, target %p.\n", iface, fullscreen, target);

    if (!fullscreen && target)
    {
        WARN("Invalid call.\n");
        return DXGI_ERROR_INVALID_CALL;
    }

    if (fullscreen)
    {
        DXGI_SWAP_CHAIN_FULLSCREEN_DESC test_fullscreen_desc = swapchain->fullscreen_desc;

        test_fullscreen_desc.Windowed = FALSE;
        if (!dxgi_validate_swapchain_fullscreen_desc(&test_fullscreen_desc))
            return DXGI_ERROR_INVALID_CALL;
    }

    if (target)
    {
        IDXGIOutput_AddRef(target);
    }
    else if (FAILED(hr = IDXGISwapChain4_GetContainingOutput(iface, &target)))
    {
        WARN("Failed to get target output for swapchain, hr %#lx.\n", hr);
        return hr;
    }
    dxgi_output = unsafe_impl_from_IDXGIOutput(target);

    /* DXGI catches nested SetFullscreenState invocations, earlier versions of d3d
     * do not. Final Fantasy XIV depends on this behavior. It tries to call SFS on
     * WM_WINDOWPOSCHANGED messages. */
    in_set_fullscreen_state = InterlockedExchange(&swapchain->in_set_fullscreen_state, 1);
    if (in_set_fullscreen_state)
    {
        WARN("Nested invocation of SetFullscreenState.\n");
        IDXGIOutput_Release(target);
        IDXGISwapChain4_GetFullscreenState(iface, &old_fs, NULL);
        return old_fs == fullscreen ? S_OK : DXGI_STATUS_MODE_CHANGE_IN_PROGRESS;
    }

    wined3d_mutex_lock();
    state = wined3d_swapchain_get_state(swapchain->wined3d_swapchain);
    wined3d_swapchain_get_desc(swapchain->wined3d_swapchain, &swapchain_desc);
    swapchain_desc.output = dxgi_output->wined3d_output;
    swapchain_desc.windowed = !fullscreen;
    hr = wined3d_swapchain_state_set_fullscreen(state, &swapchain_desc, NULL);
    if (FAILED(hr))
    {
        IDXGIOutput_Release(target);
        hr = DXGI_ERROR_NOT_CURRENTLY_AVAILABLE;
        goto done;
    }

    if (!fullscreen)
    {
        IDXGIOutput_Release(target);
        target = NULL;
    }

    if (swapchain->target)
        IDXGIOutput_Release(swapchain->target);
    swapchain->target = target;

done:
    wined3d_mutex_unlock();
    InterlockedExchange(&swapchain->in_set_fullscreen_state, 0);
    return hr;
}

static HRESULT STDMETHODCALLTYPE d3d11_swapchain_GetFullscreenState(IDXGISwapChain4 *iface,
        BOOL *fullscreen, IDXGIOutput **target)
{
    struct d3d11_swapchain *swapchain = d3d11_swapchain_from_IDXGISwapChain4(iface);
    struct wined3d_swapchain_state *state;
    BOOL windowed;
    HRESULT hr;

    TRACE("iface %p, fullscreen %p, target %p.\n", iface, fullscreen, target);

    if (fullscreen || target)
    {
        wined3d_mutex_lock();
        state = wined3d_swapchain_get_state(swapchain->wined3d_swapchain);
        windowed = wined3d_swapchain_state_is_windowed(state);
        wined3d_mutex_unlock();
    }

    if (fullscreen)
        *fullscreen = !windowed;

    if (target)
    {
        if (!windowed)
        {
            if (!swapchain->target && FAILED(hr = IDXGISwapChain4_GetContainingOutput(iface, &swapchain->target)))
                return hr;

            *target = swapchain->target;
            IDXGIOutput_AddRef(*target);
        }
        else
        {
            *target = NULL;
        }
    }

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d11_swapchain_GetDesc(IDXGISwapChain4 *iface, DXGI_SWAP_CHAIN_DESC *desc)
{
    struct d3d11_swapchain *swapchain = d3d11_swapchain_from_IDXGISwapChain4(iface);
    struct wined3d_swapchain_desc wined3d_desc;

    TRACE("iface %p, desc %p.\n", iface, desc);

    if (!desc)
    {
        WARN("Invalid pointer.\n");
        return E_INVALIDARG;
    }

    wined3d_mutex_lock();
    wined3d_swapchain_get_desc(swapchain->wined3d_swapchain, &wined3d_desc);
    wined3d_mutex_unlock();

    FIXME("Ignoring ScanlineOrdering and Scaling.\n");

    desc->BufferDesc.Width = wined3d_desc.backbuffer_width;
    desc->BufferDesc.Height = wined3d_desc.backbuffer_height;
    desc->BufferDesc.RefreshRate.Numerator = wined3d_desc.refresh_rate;
    desc->BufferDesc.RefreshRate.Denominator = 1;
    desc->BufferDesc.Format = dxgi_format_from_wined3dformat(wined3d_desc.backbuffer_format);
    desc->BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
    desc->BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
    dxgi_sample_desc_from_wined3d(&desc->SampleDesc,
            wined3d_desc.multisample_type, wined3d_desc.multisample_quality);
    desc->BufferUsage = dxgi_usage_from_wined3d_bind_flags(wined3d_desc.backbuffer_bind_flags);
    desc->BufferCount = wined3d_desc.backbuffer_count;
    desc->OutputWindow = wined3d_desc.device_window;
    desc->Windowed = wined3d_desc.windowed;
    desc->SwapEffect = dxgi_swap_effect_from_wined3d(wined3d_desc.swap_effect);
    desc->Flags = dxgi_swapchain_flags_from_wined3d(wined3d_desc.flags)
            | (swapchain->dxgi_flags & (DXGI_SWAP_CHAIN_FLAG_NONPREROTATED
                    | DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT
                    | DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING));

    return S_OK;
}

static HRESULT d3d11_swapchain_create_d3d11_textures(struct d3d11_swapchain *swapchain,
        IWineDXGIDevice *device, struct wined3d_swapchain_desc *desc);

static HRESULT STDMETHODCALLTYPE d3d11_swapchain_ResizeBuffers(IDXGISwapChain4 *iface,
        UINT buffer_count, UINT width, UINT height, DXGI_FORMAT format, UINT flags)
{
    static const UINT supported_flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH
            | DXGI_SWAP_CHAIN_FLAG_GDI_COMPATIBLE
            | DXGI_SWAP_CHAIN_FLAG_NONPREROTATED
            | DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT
            | DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
    struct d3d11_swapchain *swapchain = d3d11_swapchain_from_IDXGISwapChain4(iface);
    struct wined3d_swapchain_desc wined3d_desc;
    struct wined3d_texture *texture;
    IUnknown *parent;
    unsigned int i;
    HRESULT hr;

    TRACE("iface %p, buffer_count %u, width %u, height %u, format %s, flags %#x.\n",
            iface, buffer_count, width, height, debug_dxgi_format(format), flags);

    if (flags & ~supported_flags)
        FIXME("Ignoring flags %#x.\n", flags & ~supported_flags);

    wined3d_mutex_lock();
    wined3d_swapchain_get_desc(swapchain->wined3d_swapchain, &wined3d_desc);
    if (!(wined3d_desc.flags & WINED3D_SWAPCHAIN_FRAME_LATENCY_WAITABLE_OBJECT)
            != !(flags & DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT))
    {
        wined3d_mutex_unlock();
        return E_INVALIDARG;
    }
    for (i = 0; i < wined3d_desc.backbuffer_count; ++i)
    {
        texture = wined3d_swapchain_get_back_buffer(swapchain->wined3d_swapchain, i);
        parent = wined3d_texture_get_parent(texture);
        IUnknown_AddRef(parent);
        if (IUnknown_Release(parent))
        {
            wined3d_mutex_unlock();
            return DXGI_ERROR_INVALID_CALL;
        }
    }
    if (format != DXGI_FORMAT_UNKNOWN)
        wined3d_desc.backbuffer_format = wined3dformat_from_dxgi_format(format);
    wined3d_desc.flags = wined3d_swapchain_flags_from_dxgi(flags);
    hr = wined3d_swapchain_resize_buffers(swapchain->wined3d_swapchain, buffer_count, width, height,
            wined3d_desc.backbuffer_format, wined3d_desc.multisample_type,
            wined3d_desc.multisample_quality, wined3d_desc.flags);
    if (SUCCEEDED(hr))
    {
        wined3d_swapchain_get_desc(swapchain->wined3d_swapchain, &wined3d_desc);
        swapchain->buffer_count = wined3d_desc.backbuffer_count;
        swapchain->back_buffer_index = 0;
        swapchain->front_buffer_index = swapchain->buffer_count > 1 ? swapchain->buffer_count - 1 : 0;
        if (flags)
        {
            UINT preserve = swapchain->dxgi_flags & DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
            swapchain->dxgi_flags = (flags & ~DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT) | preserve;
        }
    }
    /* wined3d_swapchain_resize_buffers() may recreate swapchain textures.
     * We do not need to remove the reference to the wined3d swapchain from the
     * old d3d11 textures: we just validated above that they have 0 references,
     * and therefore they are not actually holding a reference to the wined3d
     * swapchain, and will not do anything with it when they are destroyed. */
    d3d11_swapchain_create_d3d11_textures(swapchain, swapchain->device, &wined3d_desc);
    wined3d_mutex_unlock();

    return hr;
}

static HRESULT STDMETHODCALLTYPE d3d11_swapchain_ResizeTarget(IDXGISwapChain4 *iface,
        const DXGI_MODE_DESC *target_mode_desc)
{
    struct d3d11_swapchain *swapchain = d3d11_swapchain_from_IDXGISwapChain4(iface);
    struct wined3d_swapchain_state *state;

    TRACE("iface %p, target_mode_desc %p.\n", iface, target_mode_desc);

    state = wined3d_swapchain_get_state(swapchain->wined3d_swapchain);

    return dxgi_swapchain_resize_target(state, target_mode_desc);
}

static HRESULT STDMETHODCALLTYPE d3d11_swapchain_GetContainingOutput(IDXGISwapChain4 *iface, IDXGIOutput **output)
{
    struct d3d11_swapchain *swapchain = d3d11_swapchain_from_IDXGISwapChain4(iface);
    HWND window;

    TRACE("iface %p, output %p.\n", iface, output);

    if (swapchain->target)
    {
        IDXGIOutput_AddRef(*output = swapchain->target);
        return S_OK;
    }

    window = d3d11_swapchain_get_hwnd(swapchain);
    return dxgi_get_output_from_window(swapchain->factory, window, output);
}

static HRESULT STDMETHODCALLTYPE d3d11_swapchain_GetFrameStatistics(IDXGISwapChain4 *iface,
        DXGI_FRAME_STATISTICS *stats)
{
    FIXME("iface %p, stats %p stub!\n", iface, stats);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d11_swapchain_GetLastPresentCount(IDXGISwapChain4 *iface,
        UINT *last_present_count)
{
    struct d3d11_swapchain *swapchain = d3d11_swapchain_from_IDXGISwapChain4(iface);

    TRACE("iface %p, last_present_count %p.\n", iface, last_present_count);

    *last_present_count =  swapchain->present_count;

    return S_OK;
}

/* IDXGISwapChain1 methods */

static HRESULT STDMETHODCALLTYPE d3d11_swapchain_GetDesc1(IDXGISwapChain4 *iface, DXGI_SWAP_CHAIN_DESC1 *desc)
{
    struct d3d11_swapchain *swapchain = d3d11_swapchain_from_IDXGISwapChain4(iface);
    struct wined3d_swapchain_desc wined3d_desc;

    TRACE("iface %p, desc %p.\n", iface, desc);

    if (!desc)
    {
        WARN("Invalid pointer.\n");
        return E_INVALIDARG;
    }

    wined3d_mutex_lock();
    wined3d_swapchain_get_desc(swapchain->wined3d_swapchain, &wined3d_desc);
    wined3d_mutex_unlock();

    FIXME("Ignoring Stereo, Scaling and AlphaMode.\n");

    desc->Width = wined3d_desc.backbuffer_width;
    desc->Height = wined3d_desc.backbuffer_height;
    desc->Format = dxgi_format_from_wined3dformat(wined3d_desc.backbuffer_format);
    desc->Stereo = FALSE;
    dxgi_sample_desc_from_wined3d(&desc->SampleDesc,
            wined3d_desc.multisample_type, wined3d_desc.multisample_quality);
    desc->BufferUsage = dxgi_usage_from_wined3d_bind_flags(wined3d_desc.backbuffer_bind_flags);
    desc->BufferCount = wined3d_desc.backbuffer_count;
    desc->Scaling = DXGI_SCALING_STRETCH;
    desc->SwapEffect = dxgi_swap_effect_from_wined3d(wined3d_desc.swap_effect);
    desc->AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    desc->Flags = dxgi_swapchain_flags_from_wined3d(wined3d_desc.flags)
            | (swapchain->dxgi_flags & (DXGI_SWAP_CHAIN_FLAG_NONPREROTATED
                    | DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT
                    | DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING));

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d11_swapchain_GetFullscreenDesc(IDXGISwapChain4 *iface,
        DXGI_SWAP_CHAIN_FULLSCREEN_DESC *desc)
{
    struct d3d11_swapchain *swapchain = d3d11_swapchain_from_IDXGISwapChain4(iface);
    struct wined3d_swapchain_state *state;
    BOOL windowed;

    TRACE("iface %p, desc %p.\n", iface, desc);

    if (!desc)
    {
        WARN("Invalid pointer.\n");
        return E_INVALIDARG;
    }

    wined3d_mutex_lock();
    state = wined3d_swapchain_get_state(swapchain->wined3d_swapchain);
    windowed = wined3d_swapchain_state_is_windowed(state);
    wined3d_mutex_unlock();

    *desc = swapchain->fullscreen_desc;
    desc->Windowed = windowed;

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d11_swapchain_GetHwnd(IDXGISwapChain4 *iface, HWND *hwnd)
{
    struct d3d11_swapchain *swapchain = d3d11_swapchain_from_IDXGISwapChain4(iface);

    TRACE("iface %p, hwnd %p.\n", iface, hwnd);

    if (!hwnd)
    {
        WARN("Invalid pointer.\n");
        return DXGI_ERROR_INVALID_CALL;
    }

    *hwnd = d3d11_swapchain_get_hwnd(swapchain);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d11_swapchain_GetCoreWindow(IDXGISwapChain4 *iface,
        REFIID iid, void **core_window)
{
    FIXME("iface %p, iid %s, core_window %p stub!\n", iface, debugstr_guid(iid), core_window);

    if (core_window)
        *core_window = NULL;

    return DXGI_ERROR_INVALID_CALL;
}

static HRESULT STDMETHODCALLTYPE d3d11_swapchain_Present1(IDXGISwapChain4 *iface,
        UINT sync_interval, UINT flags, const DXGI_PRESENT_PARAMETERS *present_parameters)
{
    struct d3d11_swapchain *swapchain = d3d11_swapchain_from_IDXGISwapChain4(iface);

    TRACE("iface %p, sync_interval %u, flags %#x, present_parameters %p.\n",
            iface, sync_interval, flags, present_parameters);

    if (present_parameters)
    {
        if ((present_parameters->DirtyRectsCount && !present_parameters->pDirtyRects)
                || (present_parameters->pScrollRect && !present_parameters->pScrollOffset))
            return DXGI_ERROR_INVALID_CALL;
        TRACE("Present parameters: dirty_rects %u, scroll_rect %p.\n",
                present_parameters->DirtyRectsCount, present_parameters->pScrollRect);
    }

    return d3d11_swapchain_present(swapchain, sync_interval, flags);
}

static BOOL STDMETHODCALLTYPE d3d11_swapchain_IsTemporaryMonoSupported(IDXGISwapChain4 *iface)
{
    FIXME("iface %p stub!\n", iface);

    return FALSE;
}

static HRESULT STDMETHODCALLTYPE d3d11_swapchain_GetRestrictToOutput(IDXGISwapChain4 *iface, IDXGIOutput **output)
{
    FIXME("iface %p, output %p stub!\n", iface, output);

    if (!output)
    {
        WARN("Invalid pointer.\n");
        return E_INVALIDARG;
    }

    *output = NULL;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d11_swapchain_SetBackgroundColor(IDXGISwapChain4 *iface, const DXGI_RGBA *color)
{
    FIXME("iface %p, color %p stub!\n", iface, color);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d11_swapchain_GetBackgroundColor(IDXGISwapChain4 *iface, DXGI_RGBA *color)
{
    FIXME("iface %p, color %p stub!\n", iface, color);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d11_swapchain_SetRotation(IDXGISwapChain4 *iface, DXGI_MODE_ROTATION rotation)
{
    FIXME("iface %p, rotation %#x stub!\n", iface, rotation);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d11_swapchain_GetRotation(IDXGISwapChain4 *iface, DXGI_MODE_ROTATION *rotation)
{
    FIXME("iface %p, rotation %p stub!\n", iface, rotation);

    return E_NOTIMPL;
}

/* IDXGISwapChain2 methods */

static HRESULT STDMETHODCALLTYPE d3d11_swapchain_SetSourceSize(IDXGISwapChain4 *iface, UINT width, UINT height)
{
    FIXME("iface %p, width %u, height %u stub!\n", iface, width, height);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d11_swapchain_GetSourceSize(IDXGISwapChain4 *iface, UINT *width, UINT *height)
{
    FIXME("iface %p, width %p, height %p stub!\n", iface, width, height);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d11_swapchain_SetMaximumFrameLatency(IDXGISwapChain4 *iface, UINT max_latency)
{
    struct d3d11_swapchain *swapchain = d3d11_swapchain_from_IDXGISwapChain4(iface);
    HRESULT hr;

    TRACE("iface %p, max_latency %u.\n", iface, max_latency);

    wined3d_mutex_lock();
    hr = wined3d_swapchain_set_max_frame_latency(swapchain->wined3d_swapchain, max_latency);
    wined3d_mutex_unlock();
    return hr == WINED3DERR_INVALIDCALL ? DXGI_ERROR_INVALID_CALL : hr;
}

static HRESULT STDMETHODCALLTYPE d3d11_swapchain_GetMaximumFrameLatency(IDXGISwapChain4 *iface, UINT *max_latency)
{
    struct d3d11_swapchain *swapchain = d3d11_swapchain_from_IDXGISwapChain4(iface);
    HRESULT hr;

    TRACE("iface %p, max_latency %p.\n", iface, max_latency);

    wined3d_mutex_lock();
    hr = wined3d_swapchain_get_max_frame_latency(swapchain->wined3d_swapchain, max_latency);
    wined3d_mutex_unlock();
    return hr == WINED3DERR_INVALIDCALL ? DXGI_ERROR_INVALID_CALL : hr;
}

static HANDLE STDMETHODCALLTYPE d3d11_swapchain_GetFrameLatencyWaitableObject(IDXGISwapChain4 *iface)
{
    struct d3d11_swapchain *swapchain = d3d11_swapchain_from_IDXGISwapChain4(iface);

    TRACE("iface %p.\n", iface);

    return wined3d_swapchain_get_frame_latency_waitable_object(swapchain->wined3d_swapchain);
}

static HRESULT STDMETHODCALLTYPE d3d11_swapchain_SetMatrixTransform(IDXGISwapChain4 *iface,
        const DXGI_MATRIX_3X2_F *matrix)
{
    struct d3d11_swapchain *swapchain = d3d11_swapchain_from_IDXGISwapChain4(iface);

    TRACE("iface %p, matrix %p.\n", iface, matrix);

    if (!matrix)
        return E_INVALIDARG;

    swapchain->matrix_transform = *matrix;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d11_swapchain_GetMatrixTransform(IDXGISwapChain4 *iface,
        DXGI_MATRIX_3X2_F *matrix)
{
    struct d3d11_swapchain *swapchain = d3d11_swapchain_from_IDXGISwapChain4(iface);

    TRACE("iface %p, matrix %p.\n", iface, matrix);

    if (!matrix)
        return E_INVALIDARG;

    *matrix = swapchain->matrix_transform;
    return S_OK;
}

/* IDXGISwapChain3 methods */

static UINT STDMETHODCALLTYPE d3d11_swapchain_GetCurrentBackBufferIndex(IDXGISwapChain4 *iface)
{
    struct d3d11_swapchain *swapchain = d3d11_swapchain_from_IDXGISwapChain4(iface);

    /* D3D11 rotates buffer 0 to the current back buffer inside wined3d on Present,
     * so GetBuffer(0) always aliases the active back buffer and GetCurrentBackBufferIndex
     * returns 0 on D3D11 while internal flip-model indices track the ring slot. */
    TRACE("iface %p, internal back_buffer_index %u, front_buffer_index %u.\n",
            iface, swapchain->back_buffer_index, swapchain->front_buffer_index);

    return 0;
}

static HRESULT STDMETHODCALLTYPE d3d11_swapchain_CheckColorSpaceSupport(IDXGISwapChain4 *iface,
        DXGI_COLOR_SPACE_TYPE colour_space, UINT *colour_space_support)
{
    FIXME("iface %p, colour_space %#x, colour_space_support %p stub!\n",
            iface, colour_space, colour_space_support);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d11_swapchain_SetColorSpace1(IDXGISwapChain4 *iface,
        DXGI_COLOR_SPACE_TYPE colour_space)
{
    FIXME("iface %p, colour_space %#x stub!\n", iface, colour_space);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d11_swapchain_ResizeBuffers1(IDXGISwapChain4 *iface,
        UINT buffer_count, UINT width, UINT height, DXGI_FORMAT format, UINT flags,
        const UINT *node_mask, IUnknown * const *present_queue)
{
    FIXME("iface %p, buffer_count %u, width %u, height %u, format %s, flags %#x, "
            "node_mask %p, present_queue %p stub!\n",
            iface, buffer_count, width, height, debug_dxgi_format(format), flags, node_mask, present_queue);

    return E_NOTIMPL;
}

/* IDXGISwapChain4 methods */

static HRESULT STDMETHODCALLTYPE d3d11_swapchain_SetHDRMetaData(IDXGISwapChain4 *iface,
        DXGI_HDR_METADATA_TYPE type, UINT size, void *metadata)
{
    FIXME("iface %p, type %#x, size %#x, metadata %p stub!\n", iface, type, size, metadata);

    return E_NOTIMPL;
}

static const struct IDXGISwapChain4Vtbl d3d11_swapchain_vtbl =
{
    /* IUnknown methods */
    d3d11_swapchain_QueryInterface,
    d3d11_swapchain_AddRef,
    d3d11_swapchain_Release,
    /* IDXGIObject methods */
    d3d11_swapchain_SetPrivateData,
    d3d11_swapchain_SetPrivateDataInterface,
    d3d11_swapchain_GetPrivateData,
    d3d11_swapchain_GetParent,
    /* IDXGIDeviceSubObject methods */
    d3d11_swapchain_GetDevice,
    /* IDXGISwapChain methods */
    d3d11_swapchain_Present,
    d3d11_swapchain_GetBuffer,
    d3d11_swapchain_SetFullscreenState,
    d3d11_swapchain_GetFullscreenState,
    d3d11_swapchain_GetDesc,
    d3d11_swapchain_ResizeBuffers,
    d3d11_swapchain_ResizeTarget,
    d3d11_swapchain_GetContainingOutput,
    d3d11_swapchain_GetFrameStatistics,
    d3d11_swapchain_GetLastPresentCount,
    /* IDXGISwapChain1 methods */
    d3d11_swapchain_GetDesc1,
    d3d11_swapchain_GetFullscreenDesc,
    d3d11_swapchain_GetHwnd,
    d3d11_swapchain_GetCoreWindow,
    d3d11_swapchain_Present1,
    d3d11_swapchain_IsTemporaryMonoSupported,
    d3d11_swapchain_GetRestrictToOutput,
    d3d11_swapchain_SetBackgroundColor,
    d3d11_swapchain_GetBackgroundColor,
    d3d11_swapchain_SetRotation,
    d3d11_swapchain_GetRotation,
    /* IDXGISwapChain2 methods */
    d3d11_swapchain_SetSourceSize,
    d3d11_swapchain_GetSourceSize,
    d3d11_swapchain_SetMaximumFrameLatency,
    d3d11_swapchain_GetMaximumFrameLatency,
    d3d11_swapchain_GetFrameLatencyWaitableObject,
    d3d11_swapchain_SetMatrixTransform,
    d3d11_swapchain_GetMatrixTransform,
    /* IDXGISwapChain3 methods */
    d3d11_swapchain_GetCurrentBackBufferIndex,
    d3d11_swapchain_CheckColorSpaceSupport,
    d3d11_swapchain_SetColorSpace1,
    d3d11_swapchain_ResizeBuffers1,
    /* IDXGISwapChain4 methods */
    d3d11_swapchain_SetHDRMetaData,
};

static void STDMETHODCALLTYPE d3d11_swapchain_wined3d_object_released(void *parent)
{
    struct d3d11_swapchain *swapchain = parent;

    wined3d_private_store_cleanup(&swapchain->private_store);
    free(parent);
}

static const struct wined3d_parent_ops d3d11_swapchain_wined3d_parent_ops =
{
    d3d11_swapchain_wined3d_object_released,
};

static inline struct d3d11_swapchain *d3d11_swapchain_from_wined3d_swapchain_state_parent(struct wined3d_swapchain_state_parent *parent)
{
    return CONTAINING_RECORD(parent, struct d3d11_swapchain, state_parent);
}

static void CDECL d3d11_swapchain_windowed_state_changed(struct wined3d_swapchain_state_parent *parent,
        BOOL windowed)
{
    struct d3d11_swapchain *swapchain = d3d11_swapchain_from_wined3d_swapchain_state_parent(parent);

    TRACE("parent %p, windowed %d.\n", parent, windowed);

    if (windowed && swapchain->target)
    {
        IDXGIOutput_Release(swapchain->target);
        swapchain->target = NULL;
    }
}

static const struct wined3d_swapchain_state_parent_ops d3d11_swapchain_state_parent_ops =
{
    d3d11_swapchain_windowed_state_changed,
};

static HRESULT d3d11_swapchain_create_d3d11_textures(struct d3d11_swapchain *swapchain,
        IWineDXGIDevice *device, struct wined3d_swapchain_desc *desc)
{
    IWineDXGIDeviceParent *dxgi_device_parent;
    unsigned int texture_flags = 0;
    unsigned int i;
    HRESULT hr;

    if (FAILED(hr = IWineDXGIDevice_QueryInterface(device,
            &IID_IWineDXGIDeviceParent, (void **)&dxgi_device_parent)))
    {
        ERR("Device should implement IWineDXGIDeviceParent.\n");
        return E_FAIL;
    }

    if (desc->flags & WINED3D_SWAPCHAIN_GDI_COMPATIBLE)
        texture_flags |= WINED3D_TEXTURE_CREATE_GET_DC;

    for (i = 0; i < desc->backbuffer_count; ++i)
    {
        IDXGISurface *surface;

        if (FAILED(hr = IWineDXGIDeviceParent_register_swapchain_texture(dxgi_device_parent,
                wined3d_swapchain_get_back_buffer(swapchain->wined3d_swapchain, i), texture_flags, &surface)))
        {
            ERR("Failed to create parent swapchain texture, hr %#lx.\n", hr);
            break;
        }
        IDXGISurface_Release(surface);
    }

    IWineDXGIDeviceParent_Release(dxgi_device_parent);
    return hr;
}

void d3d11_swapchain_mark_composition(IDXGISwapChain1 *iface)
{
    if (iface && (const void *)iface->lpVtbl == (const void *)&d3d11_swapchain_vtbl)
    {
        struct d3d11_swapchain *sc = d3d11_swapchain_from_IDXGISwapChain4((IDXGISwapChain4 *)iface);
        sc->is_composition_surface = TRUE;
    }
}

HRESULT WINAPI dxgi_swapchain_get_presented_surface(IDXGISwapChain *swapchain, IDXGISurface **surface)
{
    struct d3d11_swapchain *impl;
    IDXGISwapChain4 *iface4;
    ID3D11Texture2D *tex = NULL;
    BOOL is_comp;
    HRESULT hr;

    if (!surface)
        return E_POINTER;
    *surface = NULL;
    if (!swapchain)
        return E_INVALIDARG;

    if (FAILED(IDXGISwapChain_QueryInterface(swapchain, &IID_IDXGISwapChain4, (void **)&iface4)))
        return E_FAIL;

    if ((const void *)iface4->lpVtbl != (const void *)&d3d11_swapchain_vtbl)
    {
        IDXGISwapChain4_Release(iface4);
        return E_FAIL;
    }

    impl = d3d11_swapchain_from_IDXGISwapChain4(iface4);
    AcquireSRWLockShared(&impl->snap_lock);
    if (impl->snap_valid && impl->snap_tex[impl->snap_front])
    {
        tex = impl->snap_tex[impl->snap_front];
        ID3D11Texture2D_AddRef(tex);
    }
    is_comp = impl->is_composition_surface;
    ReleaseSRWLockShared(&impl->snap_lock);
    IDXGISwapChain4_Release(iface4);

    if (tex)
    {
        hr = ID3D11Texture2D_QueryInterface(tex, &IID_IDXGISurface, (void **)surface);
        ID3D11Texture2D_Release(tex);
        return hr;
    }

    return is_comp ? S_FALSE : E_FAIL;
}

HRESULT d3d11_swapchain_init(struct d3d11_swapchain *swapchain, struct dxgi_device *device,
        struct wined3d_swapchain_desc *desc, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *fullscreen_desc, UINT dxgi_flags)
{
    struct wined3d_swapchain_state *state;
    BOOL fullscreen;
    HRESULT hr;

    if (desc->backbuffer_format == WINED3DFMT_UNKNOWN)
        return E_INVALIDARG;

    if (FAILED(hr = IWineDXGIAdapter_GetParent(device->adapter,
            &IID_IWineDXGIFactory, (void **)&swapchain->factory)))
    {
        WARN("Failed to get adapter parent, hr %#lx.\n", hr);
        return hr;
    }
    IWineDXGIDevice_AddRef(swapchain->device = &device->IWineDXGIDevice_iface);

    swapchain->IDXGISwapChain4_iface.lpVtbl = &d3d11_swapchain_vtbl;
    swapchain->IDXGISwapChainPartner_iface.lpVtbl = (const IUnknownVtbl *)dxgi_swapchain_partner_vtbl;
    swapchain->state_parent.ops = &d3d11_swapchain_state_parent_ops;
    if (fullscreen_desc)
        swapchain->fullscreen_desc = *fullscreen_desc;
    else
    {
        memset(&swapchain->fullscreen_desc, 0, sizeof(swapchain->fullscreen_desc));
        swapchain->fullscreen_desc.Windowed = TRUE;
    }
    swapchain->refcount = 1;
    InitializeSRWLock(&swapchain->snap_lock);
    swapchain->snap_tex[0] = NULL;
    swapchain->snap_tex[1] = NULL;
    swapchain->snap_tex[2] = NULL;
    swapchain->snap_front = 0;
    swapchain->snap_width = 0;
    swapchain->snap_height = 0;
    swapchain->snap_format = DXGI_FORMAT_UNKNOWN;
    swapchain->snap_valid = FALSE;
    swapchain->swap_effect = dxgi_swap_effect_from_wined3d(desc->swap_effect);
    swapchain->flip_model = (swapchain->swap_effect == DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL
            || swapchain->swap_effect == DXGI_SWAP_EFFECT_FLIP_DISCARD);
    swapchain->matrix_transform._11 = 1.0f;
    swapchain->matrix_transform._22 = 1.0f;
    swapchain->buffer_count = desc->backbuffer_count;
    swapchain->dxgi_flags = dxgi_flags;
    swapchain->back_buffer_index = 0;
    swapchain->front_buffer_index = desc->backbuffer_count > 1 ? desc->backbuffer_count - 1 : 0;
    swapchain->present_interval = 0;
    wined3d_mutex_lock();
    wined3d_private_store_init(&swapchain->private_store);

    if (!desc->windowed && (!desc->backbuffer_width || !desc->backbuffer_height))
        FIXME("Fullscreen swapchain with back buffer width/height equal to 0 not supported properly.\n");

    fullscreen = !desc->windowed;
    desc->windowed = TRUE;
    if (FAILED(hr = wined3d_swapchain_create(device->wined3d_device, desc, &swapchain->state_parent,
            swapchain, &d3d11_swapchain_wined3d_parent_ops, &swapchain->wined3d_swapchain)))
    {
        WARN("Failed to create wined3d swapchain, hr %#lx.\n", hr);
        if (hr == WINED3DERR_INVALIDCALL)
            hr = E_INVALIDARG;
        goto cleanup;
    }

    state = wined3d_swapchain_get_state(swapchain->wined3d_swapchain);
    wined3d_swapchain_state_get_size(state, &desc->backbuffer_width, &desc->backbuffer_height);

    if (FAILED(hr = d3d11_swapchain_create_d3d11_textures(swapchain, &device->IWineDXGIDevice_iface, desc)))
    {
        ERR("Failed to create d3d11 textures, hr %#lx.\n", hr);
        goto cleanup;
    }

    swapchain->target = NULL;
    if (fullscreen)
    {
        desc->windowed = FALSE;

        if (FAILED(hr = IDXGISwapChain4_GetContainingOutput(&swapchain->IDXGISwapChain4_iface,
                &swapchain->target)))
        {
            WARN("Failed to get target output for fullscreen swapchain, hr %#lx.\n", hr);
            wined3d_swapchain_decref(swapchain->wined3d_swapchain);
            goto cleanup;
        }

        if (FAILED(hr = wined3d_swapchain_state_set_fullscreen(state, desc, NULL)))
        {
            WARN("Failed to set fullscreen state, hr %#lx.\n", hr);
            IDXGIOutput_Release(swapchain->target);
            wined3d_swapchain_decref(swapchain->wined3d_swapchain);
            goto cleanup;
        }

    }
    wined3d_mutex_unlock();

    return S_OK;

cleanup:
    wined3d_private_store_cleanup(&swapchain->private_store);
    wined3d_mutex_unlock();
    IWineDXGIFactory_Release(swapchain->factory);
    IWineDXGIDevice_Release(swapchain->device);
    return hr;
}

struct dxgi_vk_funcs
{
    PFN_vkAcquireNextImageKHR p_vkAcquireNextImageKHR;
    PFN_vkAllocateCommandBuffers p_vkAllocateCommandBuffers;
    PFN_vkAllocateMemory p_vkAllocateMemory;
    PFN_vkBeginCommandBuffer p_vkBeginCommandBuffer;
    PFN_vkBindImageMemory p_vkBindImageMemory;
    PFN_vkCmdBlitImage p_vkCmdBlitImage;
    PFN_vkCmdPipelineBarrier p_vkCmdPipelineBarrier;
    PFN_vkCreateCommandPool p_vkCreateCommandPool;
    PFN_vkCreateFence p_vkCreateFence;
    PFN_vkCreateImage p_vkCreateImage;
    PFN_vkCreateSemaphore p_vkCreateSemaphore;
    PFN_vkCreateSwapchainKHR p_vkCreateSwapchainKHR;
    PFN_vkCreateWin32SurfaceKHR p_vkCreateWin32SurfaceKHR;
    PFN_vkDestroyCommandPool p_vkDestroyCommandPool;
    PFN_vkDestroyFence p_vkDestroyFence;
    PFN_vkDestroyImage p_vkDestroyImage;
    PFN_vkDestroySemaphore p_vkDestroySemaphore;
    PFN_vkDestroySurfaceKHR p_vkDestroySurfaceKHR;
    PFN_vkResetCommandBuffer p_vkResetCommandBuffer;
    PFN_vkDestroySwapchainKHR p_vkDestroySwapchainKHR;
    PFN_vkEndCommandBuffer p_vkEndCommandBuffer;
    PFN_vkFreeMemory p_vkFreeMemory;
    PFN_vkGetImageMemoryRequirements p_vkGetImageMemoryRequirements;
    PFN_vkGetInstanceProcAddr p_vkGetInstanceProcAddr;
    PFN_vkGetPhysicalDeviceMemoryProperties p_vkGetPhysicalDeviceMemoryProperties;
    PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR p_vkGetPhysicalDeviceSurfaceCapabilitiesKHR;
    PFN_vkGetPhysicalDeviceSurfaceFormatsKHR p_vkGetPhysicalDeviceSurfaceFormatsKHR;
    PFN_vkGetPhysicalDeviceSurfacePresentModesKHR p_vkGetPhysicalDeviceSurfacePresentModesKHR;
    PFN_vkGetPhysicalDeviceSurfaceSupportKHR p_vkGetPhysicalDeviceSurfaceSupportKHR;
    PFN_vkGetPhysicalDeviceWin32PresentationSupportKHR p_vkGetPhysicalDeviceWin32PresentationSupportKHR;
    PFN_vkGetSwapchainImagesKHR p_vkGetSwapchainImagesKHR;
    PFN_vkQueuePresentKHR p_vkQueuePresentKHR;
    PFN_vkQueueSubmit p_vkQueueSubmit;
    PFN_vkQueueWaitIdle p_vkQueueWaitIdle;
    PFN_vkResetFences p_vkResetFences;
    PFN_vkWaitForFences p_vkWaitForFences;

    void *vulkan_module;
};

static HRESULT hresult_from_vk_result(VkResult vr)
{
    switch (vr)
    {
        case VK_SUCCESS:
            return S_OK;
        case VK_ERROR_OUT_OF_HOST_MEMORY:
        case VK_ERROR_OUT_OF_DEVICE_MEMORY:
            return E_OUTOFMEMORY;
        default:
            FIXME("Unhandled VkResult %d.\n", vr);
            return E_FAIL;
    }
}

#define INVALID_VK_IMAGE_INDEX (~(uint32_t)0)

struct d3d12_swapchain
{
    IDXGISwapChain4 IDXGISwapChain4_iface;
    LONG refcount;
    struct wined3d_private_store private_store;

    struct wined3d_swapchain_state *state;
    struct wined3d_swapchain_state_parent state_parent;

    VkSurfaceKHR vk_surface;
    VkFence vk_fence;
    VkInstance vk_instance;
    VkDevice vk_device;
    VkPhysicalDevice vk_physical_device;

    HANDLE worker_thread;
    CRITICAL_SECTION worker_cs;
    CONDITION_VARIABLE worker_cv;
    bool worker_running;
    struct list worker_ops;

    /* D3D12 side of the swapchain (frontend): these objects are
     * visible to the IDXGISwapChain client, so they must never be
     * recreated, except when ResizeBuffers*() is called. */
    unsigned int buffer_count;
    VkDeviceMemory vk_memory;
    VkImage vk_images[DXGI_MAX_SWAP_CHAIN_BUFFERS];
    ID3D12Resource *buffers[DXGI_MAX_SWAP_CHAIN_BUFFERS];
    unsigned int current_buffer_index;
    VkFormat vk_format;

    /* Vulkan side of the swapchain (backend): these objects are also
     * destroyed and recreated when the Vulkan swapchain becomes out
     * of date or when the synchronization interval is changed; this
     * operation should be transparent to the IDXGISwapChain client
     * (except for timings: recreating the Vulkan swapchain creates a
     * noticeable delay, unfortunately). */
    VkSwapchainKHR vk_swapchain;
    VkCommandPool vk_cmd_pool;
    VkImage vk_swapchain_images[DXGI_MAX_SWAP_CHAIN_BUFFERS];
    VkCommandBuffer vk_cmd_buffers[DXGI_MAX_SWAP_CHAIN_BUFFERS];
    VkSemaphore vk_semaphores[DXGI_MAX_SWAP_CHAIN_BUFFERS];
    unsigned int vk_swapchain_width;
    unsigned int vk_swapchain_height;
    VkPresentModeKHR present_mode;
    DXGI_SWAP_CHAIN_DESC1 backend_desc;

    ID3D12Fence *present_fence;

    uint32_t vk_image_index;

    struct dxgi_vk_funcs vk_funcs;

    ID3D12CommandQueue *command_queue;
    ID3D12Device *device;
    IWineDXGIFactory *factory;

    HWND window;
    IDXGIOutput *target;
    DXGI_SWAP_CHAIN_DESC1 desc;
    DXGI_SWAP_CHAIN_FULLSCREEN_DESC fullscreen_desc;
    LONG in_set_fullscreen_state;

    HANDLE frame_latency_semaphore;

    uint64_t frame_number;
    uint32_t frame_latency;
};

enum d3d12_swapchain_op_type
{
    D3D12_SWAPCHAIN_OP_PRESENT,
    D3D12_SWAPCHAIN_OP_RESIZE_BUFFERS,
};

struct d3d12_swapchain_op
{
    struct list entry;
    enum d3d12_swapchain_op_type type;
    union
    {
        struct
        {
            unsigned int sync_interval;
            VkImage vk_image;
            unsigned int frame_number;
        } present;
        struct
        {
            /* The resize buffers op takes ownership of the memory and
             * images used to back the previous ID3D12Resource
             * objects, so that they're only released once the worker
             * thread is done with them. */
            VkDeviceMemory vk_memory;
            VkImage vk_images[DXGI_MAX_SWAP_CHAIN_BUFFERS];
            DXGI_SWAP_CHAIN_DESC1 desc;
        } resize_buffers;
    };
};

static void d3d12_swapchain_op_destroy(struct d3d12_swapchain *swapchain, struct d3d12_swapchain_op *op)
{
    const struct dxgi_vk_funcs *vk_funcs = &swapchain->vk_funcs;
    unsigned int i;

    if (op->type == D3D12_SWAPCHAIN_OP_RESIZE_BUFFERS)
    {
        assert(swapchain->vk_device);

        for (i = 0; i < DXGI_MAX_SWAP_CHAIN_BUFFERS; ++i)
            vk_funcs->p_vkDestroyImage(swapchain->vk_device, op->resize_buffers.vk_images[i], NULL);

        vk_funcs->p_vkFreeMemory(swapchain->vk_device, op->resize_buffers.vk_memory, NULL);
    }

    free(op);
}

static HRESULT d3d12_swapchain_op_present_execute(struct d3d12_swapchain *swapchain, struct d3d12_swapchain_op *op);
static HRESULT d3d12_swapchain_op_resize_buffers_execute(struct d3d12_swapchain *swapchain, struct d3d12_swapchain_op *op);

static DWORD WINAPI d3d12_swapchain_worker_proc(void *data)
{
    struct d3d12_swapchain *swapchain = data;

    EnterCriticalSection(&swapchain->worker_cs);

    while (swapchain->worker_running)
    {
        if (!list_empty(&swapchain->worker_ops))
        {
            struct d3d12_swapchain_op *op = LIST_ENTRY(list_head(&swapchain->worker_ops), struct d3d12_swapchain_op, entry);

            list_remove(&op->entry);

            LeaveCriticalSection(&swapchain->worker_cs);

            switch (op->type)
            {
                case D3D12_SWAPCHAIN_OP_PRESENT:
                    d3d12_swapchain_op_present_execute(swapchain, op);
                    break;

                case D3D12_SWAPCHAIN_OP_RESIZE_BUFFERS:
                    d3d12_swapchain_op_resize_buffers_execute(swapchain, op);
                    break;
            }

            d3d12_swapchain_op_destroy(swapchain, op);

            EnterCriticalSection(&swapchain->worker_cs);
        }
        else if (!SleepConditionVariableCS(&swapchain->worker_cv, &swapchain->worker_cs, INFINITE))
        {
            ERR("Cannot sleep on condition variable, last error %ld.\n", GetLastError());
            break;
        }
    }

    LeaveCriticalSection(&swapchain->worker_cs);

    return 0;
}

static DXGI_FORMAT dxgi_format_from_vk_format(VkFormat vk_format)
{
    switch (vk_format)
    {
        case VK_FORMAT_B8G8R8A8_UNORM: return DXGI_FORMAT_B8G8R8A8_UNORM;
        case VK_FORMAT_R8G8B8A8_UNORM: return DXGI_FORMAT_R8G8B8A8_UNORM;
        case VK_FORMAT_A2B10G10R10_UNORM_PACK32: return DXGI_FORMAT_R10G10B10A2_UNORM;
        case VK_FORMAT_R16G16B16A16_SFLOAT: return DXGI_FORMAT_R16G16B16A16_FLOAT;
        default:
            WARN("Unhandled format %#x.\n", vk_format);
            return DXGI_FORMAT_UNKNOWN;
    }
}

static VkFormat get_swapchain_fallback_format(VkFormat vk_format)
{
    switch (vk_format)
    {
        case VK_FORMAT_R8G8B8A8_UNORM:
        case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
        case VK_FORMAT_R16G16B16A16_SFLOAT:
            return VK_FORMAT_B8G8R8A8_UNORM;
        default:
            WARN("Unhandled format %#x.\n", vk_format);
            return VK_FORMAT_UNDEFINED;
    }
}

static HRESULT select_vk_format(const struct dxgi_vk_funcs *vk_funcs,
        VkPhysicalDevice vk_physical_device, VkSurfaceKHR vk_surface,
        const DXGI_SWAP_CHAIN_DESC1 *swapchain_desc, VkFormat *vk_format)
{
    VkSurfaceFormatKHR *formats;
    uint32_t format_count;
    VkFormat format;
    unsigned int i;
    VkResult vr;

    *vk_format = VK_FORMAT_UNDEFINED;

    format = vkd3d_get_vk_format(swapchain_desc->Format);

    vr = vk_funcs->p_vkGetPhysicalDeviceSurfaceFormatsKHR(vk_physical_device, vk_surface, &format_count, NULL);
    if (vr < 0 || !format_count)
    {
        WARN("Failed to get supported surface formats, vr %d.\n", vr);
        return DXGI_ERROR_INVALID_CALL;
    }

    if (!(formats = calloc(format_count, sizeof(*formats))))
        return E_OUTOFMEMORY;

    if ((vr = vk_funcs->p_vkGetPhysicalDeviceSurfaceFormatsKHR(vk_physical_device,
            vk_surface, &format_count, formats)) < 0)
    {
        WARN("Failed to enumerate supported surface formats, vr %d.\n", vr);
        free(formats);
        return hresult_from_vk_result(vr);
    }

    for (i = 0; i < format_count; ++i)
    {
        if (formats[i].format == format && formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            break;
    }
    if (i == format_count)
    {
        /* Try to create a swapchain with format conversion. */
        format = get_swapchain_fallback_format(format);
        WARN("Failed to find Vulkan swapchain format for %s.\n", debug_dxgi_format(swapchain_desc->Format));
        for (i = 0; i < format_count; ++i)
        {
            if (formats[i].format == format && formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            {
                format = formats[i].format;
                break;
            }
        }
    }
    free(formats);
    if (i == format_count)
    {
        FIXME("Failed to find Vulkan swapchain format for %s.\n", debug_dxgi_format(swapchain_desc->Format));
        return DXGI_ERROR_UNSUPPORTED;
    }

    TRACE("Using Vulkan swapchain format %#x.\n", format);

    *vk_format = format;
    return S_OK;
}

static HRESULT vk_select_memory_type(const struct dxgi_vk_funcs *vk_funcs,
        VkPhysicalDevice vk_physical_device, uint32_t memory_type_mask,
        VkMemoryPropertyFlags flags, uint32_t *memory_type_index)
{
    VkPhysicalDeviceMemoryProperties memory_properties;
    unsigned int i;

    vk_funcs->p_vkGetPhysicalDeviceMemoryProperties(vk_physical_device, &memory_properties);
    for (i = 0; i < memory_properties.memoryTypeCount; ++i)
    {
        if (!(memory_type_mask & (1u << i)))
            continue;

        if ((memory_properties.memoryTypes[i].propertyFlags & flags) == flags)
        {
            *memory_type_index = i;
            return S_OK;
        }
    }

    FIXME("Failed to find memory type (allowed types %#x).\n", memory_type_mask);
    return E_FAIL;
}

static BOOL d3d12_swapchain_is_present_mode_supported(struct d3d12_swapchain *swapchain,
        VkPresentModeKHR present_mode)
{
    VkPhysicalDevice vk_physical_device = swapchain->vk_physical_device;
    const struct dxgi_vk_funcs *vk_funcs = &swapchain->vk_funcs;
    VkPresentModeKHR *modes;
    uint32_t count, i;
    BOOL supported;
    VkResult vr;

    if (present_mode == VK_PRESENT_MODE_FIFO_KHR)
        return TRUE;

    if ((vr = vk_funcs->p_vkGetPhysicalDeviceSurfacePresentModesKHR(vk_physical_device,
            swapchain->vk_surface, &count, NULL)) < 0)
    {
        WARN("Failed to get count of available present modes, vr %d.\n", vr);
        return FALSE;
    }

    supported = FALSE;

    if (!(modes = calloc(count, sizeof(*modes))))
        return FALSE;
    if ((vr = vk_funcs->p_vkGetPhysicalDeviceSurfacePresentModesKHR(vk_physical_device,
            swapchain->vk_surface, &count, modes)) >= 0)
    {
        for (i = 0; i < count; ++i)
        {
            if (modes[i] == present_mode)
            {
                supported = TRUE;
                break;
            }
        }
    }
    else
    {
        WARN("Failed to get available present modes, vr %d.\n", vr);
    }
    free(modes);

    return supported;
}

static HRESULT d3d12_swapchain_create_user_buffers(struct d3d12_swapchain *swapchain)
{
    const struct dxgi_vk_funcs *vk_funcs = &swapchain->vk_funcs;
    VkDeviceSize image_offset[DXGI_MAX_SWAP_CHAIN_BUFFERS];
    VkDevice vk_device = swapchain->vk_device;
    VkMemoryAllocateInfo allocate_info;
    VkMemoryRequirements requirements;
    VkImageCreateInfo image_info;
    uint32_t memory_type_mask;
    VkDeviceSize memory_size;
    unsigned int i;
    VkResult vr;
    HRESULT hr;

    memset(&image_info, 0, sizeof(image_info));
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.flags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = swapchain->vk_format;
    image_info.extent.width = swapchain->desc.Width;
    image_info.extent.height = swapchain->desc.Height;
    image_info.extent.depth = 1;
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT
            | VK_IMAGE_USAGE_TRANSFER_DST_BIT
            | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (swapchain->desc.BufferUsage & DXGI_USAGE_SHADER_INPUT)
            image_info.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_info.queueFamilyIndexCount = 0;
    image_info.pQueueFamilyIndices = NULL;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    for (i = 0; i < swapchain->desc.BufferCount; ++i)
    {
        assert(swapchain->vk_images[i] == VK_NULL_HANDLE);
        if ((vr = vk_funcs->p_vkCreateImage(vk_device, &image_info, NULL, &swapchain->vk_images[i])) < 0)
        {
            WARN("Failed to create Vulkan image, vr %d.\n", vr);
            swapchain->vk_images[i] = VK_NULL_HANDLE;
            return hresult_from_vk_result(vr);
        }
    }

    memory_size = 0;
    memory_type_mask = ~0u;
    for (i = 0; i < swapchain->desc.BufferCount; ++i)
    {
        vk_funcs->p_vkGetImageMemoryRequirements(vk_device, swapchain->vk_images[i], &requirements);

        TRACE("Size %s, alignment %s, memory types %#x.\n",
                wine_dbgstr_longlong(requirements.size), wine_dbgstr_longlong(requirements.alignment),
                requirements.memoryTypeBits);

        image_offset[i] = (memory_size + (requirements.alignment - 1)) & ~(requirements.alignment - 1);
        memory_size = image_offset[i] + requirements.size;

        memory_type_mask &= requirements.memoryTypeBits;
    }

    TRACE("Allocating %s bytes for user images.\n", wine_dbgstr_longlong(memory_size));

    allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocate_info.pNext = NULL;
    allocate_info.allocationSize = memory_size;

    if (FAILED(hr = vk_select_memory_type(vk_funcs, swapchain->vk_physical_device,
            memory_type_mask, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &allocate_info.memoryTypeIndex)))
        return hr;

    assert(swapchain->vk_memory == VK_NULL_HANDLE);
    if ((vr = vk_funcs->p_vkAllocateMemory(vk_device, &allocate_info, NULL, &swapchain->vk_memory)) < 0)
    {
        WARN("Failed to allocate device memory, vr %d.\n", vr);
        swapchain->vk_memory = VK_NULL_HANDLE;
        return hresult_from_vk_result(vr);
    }

    for (i = 0; i < swapchain->desc.BufferCount; ++i)
    {
        if ((vr = vk_funcs->p_vkBindImageMemory(vk_device, swapchain->vk_images[i],
                swapchain->vk_memory, image_offset[i])) < 0)
        {
            WARN("Failed to bind image memory, vr %d.\n", vr);
            return hresult_from_vk_result(vr);
        }
    }

    return S_OK;
}

static void vk_cmd_image_barrier(const struct dxgi_vk_funcs *vk_funcs, VkCommandBuffer cmd_buffer,
        VkPipelineStageFlags src_stage_mask, VkPipelineStageFlags dst_stage_mask,
        VkAccessFlags src_access_mask, VkAccessFlags dst_access_mask,
        VkImageLayout old_layout, VkImageLayout new_layout, VkImage image)
{
    VkImageMemoryBarrier barrier;

    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.pNext = NULL;
    barrier.srcAccessMask = src_access_mask;
    barrier.dstAccessMask = dst_access_mask;
    barrier.oldLayout = old_layout;
    barrier.newLayout = new_layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;

    vk_funcs->p_vkCmdPipelineBarrier(cmd_buffer,
            src_stage_mask, dst_stage_mask, 0, 0, NULL, 0, NULL, 1, &barrier);
}

static VkResult d3d12_swapchain_record_swapchain_blit(struct d3d12_swapchain *swapchain,
        VkCommandBuffer vk_cmd_buffer, VkImage vk_dst_image, VkImage vk_src_image)
{
    const struct dxgi_vk_funcs *vk_funcs = &swapchain->vk_funcs;
    VkCommandBufferBeginInfo begin_info;
    VkImageBlit blit;
    VkFilter filter;
    VkResult vr;

    if (swapchain->desc.Width != swapchain->vk_swapchain_width
            || swapchain->desc.Height != swapchain->vk_swapchain_height)
        filter = VK_FILTER_LINEAR;
    else
        filter = VK_FILTER_NEAREST;

    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.pNext = NULL;
    begin_info.flags = 0;
    begin_info.pInheritanceInfo = NULL;

    if ((vr = vk_funcs->p_vkBeginCommandBuffer(vk_cmd_buffer, &begin_info)) < 0)
    {
        WARN("Failed to begin command buffer, vr %d.\n", vr);
        return vr;
    }

    vk_cmd_image_barrier(vk_funcs, vk_cmd_buffer,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, vk_dst_image);

    blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.srcSubresource.mipLevel = 0;
    blit.srcSubresource.baseArrayLayer = 0;
    blit.srcSubresource.layerCount = 1;
    blit.srcOffsets[0].x = 0;
    blit.srcOffsets[0].y = 0;
    blit.srcOffsets[0].z = 0;
    blit.srcOffsets[1].x = swapchain->desc.Width;
    blit.srcOffsets[1].y = swapchain->desc.Height;
    blit.srcOffsets[1].z = 1;
    blit.dstSubresource = blit.srcSubresource;
    blit.dstOffsets[0].x = 0;
    blit.dstOffsets[0].y = 0;
    blit.dstOffsets[0].z = 0;
    if (swapchain->desc.Scaling == DXGI_SCALING_NONE)
    {
        blit.srcOffsets[1].x = min(swapchain->vk_swapchain_width, blit.srcOffsets[1].x);
        blit.srcOffsets[1].y = min(swapchain->vk_swapchain_height, blit.srcOffsets[1].y);
        blit.dstOffsets[1].x = blit.srcOffsets[1].x;
        blit.dstOffsets[1].y = blit.srcOffsets[1].y;
    }
    else
    {
        /* FIXME: handle DXGI_SCALING_ASPECT_RATIO_STRETCH. */
        blit.dstOffsets[1].x = swapchain->vk_swapchain_width;
        blit.dstOffsets[1].y = swapchain->vk_swapchain_height;
    }
    blit.dstOffsets[1].z = 1;

    vk_funcs->p_vkCmdBlitImage(vk_cmd_buffer,
            vk_src_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            vk_dst_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &blit, filter);

    vk_cmd_image_barrier(vk_funcs, vk_cmd_buffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            VK_ACCESS_TRANSFER_WRITE_BIT, 0,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, vk_dst_image);

    if ((vr = vk_funcs->p_vkEndCommandBuffer(vk_cmd_buffer)) < 0)
        WARN("Failed to end command buffer, vr %d.\n", vr);

    return vr;
}

static HRESULT d3d12_swapchain_create_command_buffers(struct d3d12_swapchain *swapchain)
{
    const struct dxgi_vk_funcs *vk_funcs = &swapchain->vk_funcs;
    VkDevice vk_device = swapchain->vk_device;
    VkCommandBufferAllocateInfo allocate_info;
    VkSemaphoreCreateInfo semaphore_info;
    VkCommandPoolCreateInfo pool_info;
    unsigned int i;
    VkResult vr;

    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.pNext = NULL;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = vkd3d_get_vk_queue_family_index(swapchain->command_queue);

    assert(swapchain->vk_cmd_pool == VK_NULL_HANDLE);
    if ((vr = vk_funcs->p_vkCreateCommandPool(vk_device, &pool_info,
            NULL, &swapchain->vk_cmd_pool)) < 0)
    {
        WARN("Failed to create command pool, vr %d.\n", vr);
        swapchain->vk_cmd_pool = VK_NULL_HANDLE;
        return hresult_from_vk_result(vr);
    }

    allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocate_info.pNext = NULL;
    allocate_info.commandPool = swapchain->vk_cmd_pool;
    allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocate_info.commandBufferCount = swapchain->buffer_count;

    if ((vr = vk_funcs->p_vkAllocateCommandBuffers(vk_device, &allocate_info,
            swapchain->vk_cmd_buffers)) < 0)
    {
        WARN("Failed to allocate command buffers, vr %d.\n", vr);
        return hresult_from_vk_result(vr);
    }

    for (i = 0; i < swapchain->buffer_count; ++i)
    {
        semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        semaphore_info.pNext = NULL;
        semaphore_info.flags = 0;

        assert(swapchain->vk_semaphores[i] == VK_NULL_HANDLE);
        if ((vr = vk_funcs->p_vkCreateSemaphore(vk_device, &semaphore_info,
                NULL, &swapchain->vk_semaphores[i])) < 0)
        {
            WARN("Failed to create semaphore, vr %d.\n", vr);
            swapchain->vk_semaphores[i] = VK_NULL_HANDLE;
            return hresult_from_vk_result(vr);
        }
    }

    return S_OK;
}

static HRESULT d3d12_swapchain_create_image_resources(struct d3d12_swapchain *swapchain)
{
    struct vkd3d_image_resource_create_info resource_info;
    ID3D12Device *device = swapchain->device;
    unsigned int i;
    HRESULT hr;

    resource_info.type = VKD3D_STRUCTURE_TYPE_IMAGE_RESOURCE_CREATE_INFO;
    resource_info.next = NULL;
    resource_info.desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resource_info.desc.Alignment = 0;
    resource_info.desc.Width = swapchain->desc.Width;
    resource_info.desc.Height = swapchain->desc.Height;
    resource_info.desc.DepthOrArraySize = 1;
    resource_info.desc.MipLevels = 1;
    resource_info.desc.Format = dxgi_format_from_vk_format(swapchain->vk_format);
    resource_info.desc.SampleDesc.Count = 1;
    resource_info.desc.SampleDesc.Quality = 0;
    resource_info.desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    resource_info.desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    resource_info.flags = VKD3D_RESOURCE_INITIAL_STATE_TRANSITION | VKD3D_RESOURCE_PRESENT_STATE_TRANSITION;
    resource_info.present_state = D3D12_RESOURCE_STATE_COPY_SOURCE;

    for (i = 0; i < swapchain->desc.BufferCount; ++i)
    {
        assert(swapchain->vk_images[i]);
        assert(!swapchain->buffers[i]);

        resource_info.vk_image = swapchain->vk_images[i];

        if (FAILED(hr = vkd3d_create_image_resource(device, &resource_info, &swapchain->buffers[i])))
        {
            WARN("Failed to create vkd3d resource for Vulkan image %u, hr %#lx.\n", i, hr);
            return hr;
        }

        vkd3d_resource_incref(swapchain->buffers[i]);
        ID3D12Resource_Release(swapchain->buffers[i]);
    }

    return S_OK;
}

static VkResult d3d12_swapchain_acquire_next_vulkan_image(struct d3d12_swapchain *swapchain)
{
    const struct dxgi_vk_funcs *vk_funcs = &swapchain->vk_funcs;
    VkDevice vk_device = swapchain->vk_device;
    VkFence vk_fence = swapchain->vk_fence;
    VkResult vr;

    swapchain->vk_image_index = INVALID_VK_IMAGE_INDEX;

    if ((vr = vk_funcs->p_vkAcquireNextImageKHR(vk_device, swapchain->vk_swapchain, UINT64_MAX,
            VK_NULL_HANDLE, vk_fence, &swapchain->vk_image_index)) < 0)
    {
        WARN("Failed to acquire next Vulkan image, vr %d.\n", vr);
        return vr;
    }

    if ((vr = vk_funcs->p_vkWaitForFences(vk_device, 1, &vk_fence, VK_TRUE, UINT64_MAX)) != VK_SUCCESS)
    {
        ERR("Failed to wait for fence, vr %d.\n", vr);
        return vr;
    }
    if ((vr = vk_funcs->p_vkResetFences(vk_device, 1, &vk_fence)) < 0)
        ERR("Failed to reset fence, vr %d.\n", vr);

    return vr;
}

static void d3d12_swapchain_destroy_vulkan_resources(struct d3d12_swapchain *swapchain)
{
    const struct dxgi_vk_funcs *vk_funcs = &swapchain->vk_funcs;
    VkQueue vk_queue;
    unsigned int i;

    if (swapchain->command_queue)
    {
        vk_queue = vkd3d_acquire_vk_queue(swapchain->command_queue);
        vk_funcs->p_vkQueueWaitIdle(vk_queue);
        vkd3d_release_vk_queue(swapchain->command_queue);
    }

    if (swapchain->vk_device)
    {
        for (i = 0; i < swapchain->buffer_count; ++i)
        {
            vk_funcs->p_vkDestroySemaphore(swapchain->vk_device, swapchain->vk_semaphores[i], NULL);
            swapchain->vk_semaphores[i] = VK_NULL_HANDLE;
        }

        vk_funcs->p_vkDestroyCommandPool(swapchain->vk_device, swapchain->vk_cmd_pool, NULL);
        swapchain->vk_cmd_pool = VK_NULL_HANDLE;
    }
}

static void d3d12_swapchain_destroy_d3d12_resources(struct d3d12_swapchain *swapchain)
{
    const struct dxgi_vk_funcs *vk_funcs = &swapchain->vk_funcs;
    unsigned int i;

    for (i = 0; i < swapchain->desc.BufferCount; ++i)
    {
        if (swapchain->buffers[i])
        {
            vkd3d_resource_decref(swapchain->buffers[i]);
            swapchain->buffers[i] = NULL;
        }
        if (swapchain->vk_device)
        {
            vk_funcs->p_vkDestroyImage(swapchain->vk_device, swapchain->vk_images[i], NULL);
            swapchain->vk_images[i] = VK_NULL_HANDLE;
        }
    }
    if (swapchain->vk_device)
    {
        vk_funcs->p_vkFreeMemory(swapchain->vk_device, swapchain->vk_memory, NULL);
        swapchain->vk_memory = VK_NULL_HANDLE;
    }
}

static HRESULT d3d12_swapchain_create_vulkan_swapchain(struct d3d12_swapchain *swapchain)
{
    VkPhysicalDevice vk_physical_device = swapchain->vk_physical_device;
    const struct dxgi_vk_funcs *vk_funcs = &swapchain->vk_funcs;
    VkSwapchainCreateInfoKHR vk_swapchain_desc;
    VkDevice vk_device = swapchain->vk_device;
    unsigned int width, height, image_count;
    VkSurfaceCapabilitiesKHR surface_caps;
    VkFormat vk_swapchain_format;
    VkSwapchainKHR vk_swapchain;
    VkImageUsageFlags usage;
    VkResult vr;
    HRESULT hr;

    if (FAILED(hr = select_vk_format(vk_funcs, vk_physical_device,
            swapchain->vk_surface, &swapchain->backend_desc, &vk_swapchain_format)))
        return hr;

    if ((vr = vk_funcs->p_vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vk_physical_device,
            swapchain->vk_surface, &surface_caps)) < 0)
    {
        WARN("Failed to get surface capabilities, vr %d.\n", vr);
        return hresult_from_vk_result(vr);
    }

    image_count = swapchain->backend_desc.BufferCount;
    image_count = max(image_count, surface_caps.minImageCount);
    if (surface_caps.maxImageCount)
        image_count = min(image_count, surface_caps.maxImageCount);

    if (image_count != swapchain->backend_desc.BufferCount)
    {
        WARN("Buffer count %u is not supported (%u-%u).\n", swapchain->backend_desc.BufferCount,
                surface_caps.minImageCount, surface_caps.maxImageCount);
    }

    width = swapchain->backend_desc.Width;
    height = swapchain->backend_desc.Height;
    width = max(width, surface_caps.minImageExtent.width);
    width = min(width, surface_caps.maxImageExtent.width);
    height = max(height, surface_caps.minImageExtent.height);
    height = min(height, surface_caps.maxImageExtent.height);

    if (width != swapchain->backend_desc.Width || height != swapchain->backend_desc.Height)
    {
        WARN("Swapchain dimensions %ux%u are not supported (%u-%u x %u-%u).\n",
                swapchain->backend_desc.Width, swapchain->backend_desc.Height,
                surface_caps.minImageExtent.width, surface_caps.maxImageExtent.width,
                surface_caps.minImageExtent.height, surface_caps.maxImageExtent.height);
    }

    TRACE("Vulkan swapchain extent %ux%u.\n", width, height);

    if (!(surface_caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR))
    {
        FIXME("Unsupported alpha mode.\n");
        return DXGI_ERROR_UNSUPPORTED;
    }

    usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    usage |= surface_caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    usage |= surface_caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (!(usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) || !(usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT))
        WARN("Transfer not supported for swapchain images.\n");
    if (swapchain->backend_desc.BufferUsage & DXGI_USAGE_SHADER_INPUT)
    {
        usage |= surface_caps.supportedUsageFlags & VK_IMAGE_USAGE_SAMPLED_BIT;
        if (!(usage & VK_IMAGE_USAGE_SAMPLED_BIT))
            WARN("Sampling not supported for swapchain images.\n");
    }

    vk_swapchain_desc.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    vk_swapchain_desc.pNext = NULL;
    vk_swapchain_desc.flags = 0;
    vk_swapchain_desc.surface = swapchain->vk_surface;
    vk_swapchain_desc.minImageCount = image_count;
    vk_swapchain_desc.imageFormat = vk_swapchain_format;
    vk_swapchain_desc.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    vk_swapchain_desc.imageExtent.width = width;
    vk_swapchain_desc.imageExtent.height = height;
    vk_swapchain_desc.imageArrayLayers = 1;
    vk_swapchain_desc.imageUsage = usage;
    vk_swapchain_desc.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vk_swapchain_desc.queueFamilyIndexCount = 0;
    vk_swapchain_desc.pQueueFamilyIndices = NULL;
    vk_swapchain_desc.preTransform = surface_caps.currentTransform;
    vk_swapchain_desc.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    vk_swapchain_desc.presentMode = swapchain->present_mode;
    vk_swapchain_desc.clipped = VK_TRUE;
    vk_swapchain_desc.oldSwapchain = swapchain->vk_swapchain;
    if ((vr = vk_funcs->p_vkCreateSwapchainKHR(vk_device, &vk_swapchain_desc, NULL, &vk_swapchain)) < 0)
    {
        WARN("Failed to create Vulkan swapchain, vr %d.\n", vr);
        return hresult_from_vk_result(vr);
    }

    if (swapchain->vk_swapchain)
        vk_funcs->p_vkDestroySwapchainKHR(swapchain->vk_device, swapchain->vk_swapchain, NULL);

    if ((vr = vk_funcs->p_vkGetSwapchainImagesKHR(vk_device, vk_swapchain, &image_count, NULL)) < 0)
    {
        WARN("Failed to get Vulkan swapchain images, vr %d.\n", vr);
        return hresult_from_vk_result(vr);
    }
    if (image_count > ARRAY_SIZE(swapchain->vk_swapchain_images))
    {
        FIXME("Unsupported Vulkan swapchain image count %u.\n", image_count);
        return E_FAIL;
    }
    swapchain->buffer_count = image_count;
    if ((vr = vk_funcs->p_vkGetSwapchainImagesKHR(vk_device, vk_swapchain,
            &image_count, swapchain->vk_swapchain_images)) < 0)
    {
        WARN("Failed to get Vulkan swapchain images, vr %d.\n", vr);
        return hresult_from_vk_result(vr);
    }

    swapchain->vk_swapchain = vk_swapchain;
    swapchain->vk_swapchain_width = width;
    swapchain->vk_swapchain_height = height;

    swapchain->vk_image_index = INVALID_VK_IMAGE_INDEX;

    return S_OK;
}

static HRESULT d3d12_swapchain_create_vulkan_resources(struct d3d12_swapchain *swapchain)
{
    HRESULT hr;

    if (FAILED(hr = d3d12_swapchain_create_vulkan_swapchain(swapchain)))
        return hr;

    return d3d12_swapchain_create_command_buffers(swapchain);
}

static HRESULT d3d12_swapchain_create_d3d12_resources(struct d3d12_swapchain *swapchain)
{
    HRESULT hr;

    if (!(swapchain->vk_format = vkd3d_get_vk_format(swapchain->desc.Format)))
    {
        WARN("Invalid format %#x.\n", swapchain->desc.Format);
        return DXGI_ERROR_INVALID_CALL;
    }

    if (FAILED(hr = d3d12_swapchain_create_user_buffers(swapchain)))
        return hr;

    return d3d12_swapchain_create_image_resources(swapchain);
}

static inline struct d3d12_swapchain *d3d12_swapchain_from_IDXGISwapChain4(IDXGISwapChain4 *iface)
{
    return CONTAINING_RECORD(iface, struct d3d12_swapchain, IDXGISwapChain4_iface);
}

/* IUnknown methods */

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_QueryInterface(IDXGISwapChain4 *iface, REFIID iid, void **object)
{
    TRACE("iface %p, iid %s, object %p.\n", iface, debugstr_guid(iid), object);

    if (IsEqualGUID(iid, &IID_IUnknown)
            || IsEqualGUID(iid, &IID_IDXGIObject)
            || IsEqualGUID(iid, &IID_IDXGIDeviceSubObject)
            || IsEqualGUID(iid, &IID_IDXGISwapChain)
            || IsEqualGUID(iid, &IID_IDXGISwapChain1)
            || IsEqualGUID(iid, &IID_IDXGISwapChain2)
            || IsEqualGUID(iid, &IID_IDXGISwapChain3)
            || IsEqualGUID(iid, &IID_IDXGISwapChain4))
    {
        IUnknown_AddRef(iface);
        *object = iface;
        return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid));

    *object = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d3d12_swapchain_AddRef(IDXGISwapChain4 *iface)
{
    struct d3d12_swapchain *swapchain = d3d12_swapchain_from_IDXGISwapChain4(iface);
    ULONG refcount = InterlockedIncrement(&swapchain->refcount);

    TRACE("%p increasing refcount to %lu.\n", swapchain, refcount);

    return refcount;
}

static void d3d12_swapchain_destroy(struct d3d12_swapchain *swapchain)
{
    const struct dxgi_vk_funcs *vk_funcs = &swapchain->vk_funcs;
    void *vulkan_module = vk_funcs->vulkan_module;
    struct d3d12_swapchain_op *op, *op2;
    DWORD ret;

    EnterCriticalSection(&swapchain->worker_cs);
    swapchain->worker_running = false;
    WakeAllConditionVariable(&swapchain->worker_cv);
    LeaveCriticalSection(&swapchain->worker_cs);

    if (swapchain->worker_thread)
    {
        if ((ret = WaitForSingleObject(swapchain->worker_thread, INFINITE)) != WAIT_OBJECT_0)
            ERR("Failed to wait for worker thread, return value %ld.\n", ret);

        if (!CloseHandle(swapchain->worker_thread))
            ERR("Failed to close worker thread, last error %ld.\n", GetLastError());
    }

    DeleteCriticalSection(&swapchain->worker_cs);

    LIST_FOR_EACH_ENTRY_SAFE(op, op2, &swapchain->worker_ops, struct d3d12_swapchain_op, entry)
    {
        d3d12_swapchain_op_destroy(swapchain, op);
    }

    d3d12_swapchain_destroy_vulkan_resources(swapchain);
    d3d12_swapchain_destroy_d3d12_resources(swapchain);

    if (swapchain->present_fence)
        ID3D12Fence_Release(swapchain->present_fence);

    if (swapchain->frame_latency_semaphore)
        CloseHandle(swapchain->frame_latency_semaphore);

    if (swapchain->command_queue)
        ID3D12CommandQueue_Release(swapchain->command_queue);

    wined3d_private_store_cleanup(&swapchain->private_store);

    if (swapchain->vk_device)
    {
        vk_funcs->p_vkDestroyFence(swapchain->vk_device, swapchain->vk_fence, NULL);
        vk_funcs->p_vkDestroySwapchainKHR(swapchain->vk_device, swapchain->vk_swapchain, NULL);
    }

    if (swapchain->vk_instance)
        vk_funcs->p_vkDestroySurfaceKHR(swapchain->vk_instance, swapchain->vk_surface, NULL);

    if (swapchain->target)
    {
        WARN("Destroying fullscreen swapchain.\n");
        IDXGIOutput_Release(swapchain->target);
    }

    if (swapchain->device)
        ID3D12Device_Release(swapchain->device);

    if (swapchain->factory)
        IWineDXGIFactory_Release(swapchain->factory);

    FreeLibrary(vulkan_module);

    wined3d_swapchain_state_destroy(swapchain->state);
}

static ULONG STDMETHODCALLTYPE d3d12_swapchain_Release(IDXGISwapChain4 *iface)
{
    struct d3d12_swapchain *swapchain = d3d12_swapchain_from_IDXGISwapChain4(iface);
    ULONG refcount = InterlockedDecrement(&swapchain->refcount);

    TRACE("%p decreasing refcount to %lu.\n", swapchain, refcount);

    if (!refcount)
    {
        d3d12_swapchain_destroy(swapchain);
        free(swapchain);
    }

    return refcount;
}

/* IDXGIObject methods */

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_SetPrivateData(IDXGISwapChain4 *iface,
        REFGUID guid, UINT data_size, const void *data)
{
    struct d3d12_swapchain *swapchain = d3d12_swapchain_from_IDXGISwapChain4(iface);

    TRACE("iface %p, guid %s, data_size %u, data %p.\n", iface, debugstr_guid(guid), data_size, data);

    return dxgi_set_private_data(&swapchain->private_store, guid, data_size, data);
}

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_SetPrivateDataInterface(IDXGISwapChain4 *iface,
        REFGUID guid, const IUnknown *object)
{
    struct d3d12_swapchain *swapchain = d3d12_swapchain_from_IDXGISwapChain4(iface);

    TRACE("iface %p, guid %s, object %p.\n", iface, debugstr_guid(guid), object);

    return dxgi_set_private_data_interface(&swapchain->private_store, guid, object);
}

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_GetPrivateData(IDXGISwapChain4 *iface,
        REFGUID guid, UINT *data_size, void *data)
{
    struct d3d12_swapchain *swapchain = d3d12_swapchain_from_IDXGISwapChain4(iface);

    TRACE("iface %p, guid %s, data_size %p, data %p.\n", iface, debugstr_guid(guid), data_size, data);

    return dxgi_get_private_data(&swapchain->private_store, guid, data_size, data);
}

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_GetParent(IDXGISwapChain4 *iface, REFIID iid, void **parent)
{
    struct d3d12_swapchain *swapchain = d3d12_swapchain_from_IDXGISwapChain4(iface);

    TRACE("iface %p, iid %s, parent %p.\n", iface, debugstr_guid(iid), parent);

    return IWineDXGIFactory_QueryInterface(swapchain->factory, iid, parent);
}

/* IDXGIDeviceSubObject methods */

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_GetDevice(IDXGISwapChain4 *iface, REFIID iid, void **device)
{
    struct d3d12_swapchain *swapchain = d3d12_swapchain_from_IDXGISwapChain4(iface);

    TRACE("iface %p, iid %s, device %p.\n", iface, debugstr_guid(iid), device);

    return ID3D12Device_QueryInterface(swapchain->device, iid, device);
}

/* IDXGISwapChain methods */

static HRESULT d3d12_swapchain_set_sync_interval(struct d3d12_swapchain *swapchain,
        unsigned int sync_interval)
{
    VkPresentModeKHR present_mode;

    switch (sync_interval)
    {
        case 0:
            if (d3d12_swapchain_is_present_mode_supported(swapchain, VK_PRESENT_MODE_IMMEDIATE_KHR))
                present_mode = VK_PRESENT_MODE_IMMEDIATE_KHR;
            else if (d3d12_swapchain_is_present_mode_supported(swapchain, VK_PRESENT_MODE_MAILBOX_KHR))
                present_mode = VK_PRESENT_MODE_MAILBOX_KHR;
            else
                present_mode = VK_PRESENT_MODE_FIFO_KHR;
            break;
        default:
            FIXME("Unsupported sync interval %u.\n", sync_interval);
        case 1:
            present_mode = VK_PRESENT_MODE_FIFO_KHR;
            break;
    }

    if (swapchain->present_mode == present_mode)
        return S_OK;

    if (!d3d12_swapchain_is_present_mode_supported(swapchain, present_mode))
    {
        FIXME("Vulkan present mode %#x is not supported.\n", present_mode);
        return S_OK;
    }

    d3d12_swapchain_destroy_vulkan_resources(swapchain);
    swapchain->present_mode = present_mode;
    return d3d12_swapchain_create_vulkan_resources(swapchain);
}

static VkResult d3d12_swapchain_queue_present(struct d3d12_swapchain *swapchain, VkImage vk_src_image,
        uint64_t frame_number)
{
    const struct dxgi_vk_funcs *vk_funcs = &swapchain->vk_funcs;
    VkPresentInfoKHR present_info;
    VkCommandBuffer vk_cmd_buffer;
    VkSubmitInfo submit_info;
    VkImage vk_dst_image;
    VkQueue vk_queue;
    VkResult vr;
    HRESULT hr;

    if (swapchain->vk_image_index == INVALID_VK_IMAGE_INDEX)
    {
        if ((vr = d3d12_swapchain_acquire_next_vulkan_image(swapchain)) < 0)
            return vr;
    }

    assert(swapchain->vk_image_index < swapchain->buffer_count);

    vk_cmd_buffer = swapchain->vk_cmd_buffers[swapchain->vk_image_index];
    vk_dst_image = swapchain->vk_swapchain_images[swapchain->vk_image_index];

    if ((vr = vk_funcs->p_vkResetCommandBuffer(vk_cmd_buffer, 0)) < 0)
    {
        ERR("Failed to reset command buffer, vr %d.\n", vr);
        return vr;
    }

    if ((vr = d3d12_swapchain_record_swapchain_blit(swapchain,
            vk_cmd_buffer, vk_dst_image, vk_src_image)) < 0 )
        return vr;

    if (FAILED(hr = ID3D12Fence_SetEventOnCompletion(swapchain->present_fence, frame_number + 1, NULL)))
    {
        ERR("Failed to wait for present event, hr %#lx.\n", hr);
        return VK_ERROR_UNKNOWN;
    }

    vk_queue = vkd3d_acquire_vk_queue(swapchain->command_queue);

    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.pNext = NULL;
    submit_info.waitSemaphoreCount = 0;
    submit_info.pWaitSemaphores = NULL;
    submit_info.pWaitDstStageMask = NULL;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &vk_cmd_buffer;
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores = &swapchain->vk_semaphores[swapchain->vk_image_index];

    if ((vr = vk_funcs->p_vkQueueSubmit(vk_queue, 1, &submit_info, VK_NULL_HANDLE)) < 0)
    {
        ERR("Failed to blit swapchain buffer, vr %d.\n", vr);
        vkd3d_release_vk_queue(swapchain->command_queue);
        return vr;
    }

    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.pNext = NULL;
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores = &swapchain->vk_semaphores[swapchain->vk_image_index];
    present_info.swapchainCount = 1;
    present_info.pSwapchains = &swapchain->vk_swapchain;
    present_info.pImageIndices = &swapchain->vk_image_index;
    present_info.pResults = NULL;

    if ((vr = vk_funcs->p_vkQueuePresentKHR(vk_queue, &present_info)) >= 0)
        swapchain->vk_image_index = INVALID_VK_IMAGE_INDEX;

    vkd3d_release_vk_queue(swapchain->command_queue);

    return vr;
}

static HRESULT d3d12_swapchain_op_present_execute(struct d3d12_swapchain *swapchain, struct d3d12_swapchain_op *op)
{
    VkResult vr;
    HRESULT hr;

    if (FAILED(hr = d3d12_swapchain_set_sync_interval(swapchain, op->present.sync_interval)))
        return hr;

    vr = d3d12_swapchain_queue_present(swapchain, op->present.vk_image, op->present.frame_number);
    if (vr == VK_ERROR_OUT_OF_DATE_KHR)
    {
        TRACE("Recreating Vulkan swapchain.\n");

        d3d12_swapchain_destroy_vulkan_resources(swapchain);
        if (FAILED(hr = d3d12_swapchain_create_vulkan_resources(swapchain)))
            return hr;

        if ((vr = d3d12_swapchain_queue_present(swapchain, op->present.vk_image, op->present.frame_number)) < 0)
            ERR("Failed to present after recreating swapchain, vr %d.\n", vr);
    }

    if (vr < 0)
    {
        ERR("Failed to queue present, vr %d.\n", vr);
        return hresult_from_vk_result(vr);
    }

    if (!ReleaseSemaphore(swapchain->frame_latency_semaphore, 1, NULL))
    {
        ERR("Failed to release frame latency semaphore, last error %ld.\n", GetLastError());
        return HRESULT_FROM_WIN32(GetLastError());
    }

    return S_OK;
}

static HRESULT d3d12_swapchain_present(struct d3d12_swapchain *swapchain,
        unsigned int sync_interval, unsigned int flags)
{
    struct d3d12_swapchain_op *op;
    HRESULT hr;

    if (sync_interval > 4)
    {
        WARN("Invalid sync interval %u.\n", sync_interval);
        return DXGI_ERROR_INVALID_CALL;
    }

    if (flags & ~DXGI_PRESENT_TEST)
        FIXME("Unimplemented flags %#x.\n", flags);
    if (flags & DXGI_PRESENT_TEST)
    {
        WARN("Returning S_OK for DXGI_PRESENT_TEST.\n");
        return S_OK;
    }

    if (!(swapchain->desc.Flags & DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT))
    {
        /* After around two seconds Present() will succeed anyway, without any error. */
        DWORD ret = WaitForSingleObject(swapchain->frame_latency_semaphore, 2000);
        if (ret != WAIT_OBJECT_0 && ret != WAIT_TIMEOUT)
        {
            ERR("Failed to wait for frame latency semaphore, last error %ld.\n", GetLastError());
            return HRESULT_FROM_WIN32(GetLastError());
        }
    }

    if (!(op = calloc(1, sizeof(*op))))
    {
        WARN("Cannot allocate memory.\n");
        return E_OUTOFMEMORY;
    }

    op->type = D3D12_SWAPCHAIN_OP_PRESENT;
    op->present.sync_interval = sync_interval;
    op->present.vk_image = swapchain->vk_images[swapchain->current_buffer_index];
    op->present.frame_number = swapchain->frame_number;

    EnterCriticalSection(&swapchain->worker_cs);
    list_add_tail(&swapchain->worker_ops, &op->entry);
    WakeAllConditionVariable(&swapchain->worker_cv);
    LeaveCriticalSection(&swapchain->worker_cs);

    ++swapchain->frame_number;

    if (FAILED(hr = vkd3d_queue_signal_on_cpu(swapchain->command_queue,
            swapchain->present_fence, swapchain->frame_number)))
    {
        ERR("Failed to signal present fence, hf %#lx.\n", hr);
        return hr;
    }

    swapchain->current_buffer_index = (swapchain->current_buffer_index + 1) % swapchain->desc.BufferCount;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_Present(IDXGISwapChain4 *iface, UINT sync_interval, UINT flags)
{
    struct d3d12_swapchain *swapchain = d3d12_swapchain_from_IDXGISwapChain4(iface);

    TRACE("iface %p, sync_interval %u, flags %#x.\n", iface, sync_interval, flags);

    return d3d12_swapchain_present(swapchain, sync_interval, flags);
}

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_GetBuffer(IDXGISwapChain4 *iface,
        UINT buffer_idx, REFIID iid, void **surface)
{
    struct d3d12_swapchain *swapchain = d3d12_swapchain_from_IDXGISwapChain4(iface);

    TRACE("iface %p, buffer_idx %u, iid %s, surface %p.\n",
            iface, buffer_idx, debugstr_guid(iid), surface);

    if (buffer_idx >= swapchain->desc.BufferCount)
    {
        WARN("Invalid buffer index %u.\n", buffer_idx);
        return DXGI_ERROR_INVALID_CALL;
    }

    assert(swapchain->buffers[buffer_idx]);
    return ID3D12Resource_QueryInterface(swapchain->buffers[buffer_idx], iid, surface);
}

static HRESULT STDMETHODCALLTYPE DECLSPEC_HOTPATCH d3d12_swapchain_SetFullscreenState(IDXGISwapChain4 *iface,
        BOOL fullscreen, IDXGIOutput *target)
{
    struct d3d12_swapchain *swapchain = d3d12_swapchain_from_IDXGISwapChain4(iface);
    DXGI_SWAP_CHAIN_FULLSCREEN_DESC *fullscreen_desc = &swapchain->fullscreen_desc;
    const DXGI_SWAP_CHAIN_DESC1 *swapchain_desc = &swapchain->desc;
    struct wined3d_swapchain_desc wined3d_desc;
    HWND window = swapchain->window;
    LONG in_set_fullscreen_state;
    BOOL old_fs;
    HRESULT hr;

    TRACE("iface %p, fullscreen %#x, target %p.\n", iface, fullscreen, target);

    if (!fullscreen && target)
    {
        WARN("Invalid call.\n");
        return DXGI_ERROR_INVALID_CALL;
    }

    if (fullscreen)
    {
        DXGI_SWAP_CHAIN_FULLSCREEN_DESC test_fullscreen_desc = *fullscreen_desc;

        test_fullscreen_desc.Windowed = FALSE;
        if (!dxgi_validate_swapchain_fullscreen_desc(&test_fullscreen_desc))
            return DXGI_ERROR_INVALID_CALL;
    }

    if (target)
    {
        IDXGIOutput_AddRef(target);
    }
    else if (FAILED(hr = IDXGISwapChain4_GetContainingOutput(iface, &target)))
    {
        WARN("Failed to get target output for swapchain, hr %#lx.\n", hr);
        return hr;
    }

    if (FAILED(hr = wined3d_swapchain_desc_from_dxgi(&wined3d_desc, target, window, swapchain_desc,
            fullscreen_desc)))
    {
        IDXGIOutput_Release(target);
        return hr;
    }

    in_set_fullscreen_state = InterlockedExchange(&swapchain->in_set_fullscreen_state, 1);
    if (in_set_fullscreen_state)
    {
        WARN("Nested invocation of SetFullscreenState.\n");
        IDXGIOutput_Release(target);
        IDXGISwapChain4_GetFullscreenState(iface, &old_fs, NULL);
        return old_fs == fullscreen ? S_OK : DXGI_STATUS_MODE_CHANGE_IN_PROGRESS;
    }

    wined3d_mutex_lock();
    wined3d_desc.windowed = !fullscreen;
    hr = wined3d_swapchain_state_set_fullscreen(swapchain->state, &wined3d_desc, NULL);
    if (FAILED(hr))
    {
        IDXGIOutput_Release(target);
        hr = DXGI_ERROR_NOT_CURRENTLY_AVAILABLE;
        goto done;
    }

    fullscreen_desc->Windowed = wined3d_desc.windowed;
    if (!fullscreen)
    {
        IDXGIOutput_Release(target);
        target = NULL;
    }

    if (swapchain->target)
        IDXGIOutput_Release(swapchain->target);
    swapchain->target = target;

done:
    wined3d_mutex_unlock();
    InterlockedExchange(&swapchain->in_set_fullscreen_state, 0);
    return hr;
}

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_GetFullscreenState(IDXGISwapChain4 *iface,
        BOOL *fullscreen, IDXGIOutput **target)
{
    struct d3d12_swapchain *swapchain = d3d12_swapchain_from_IDXGISwapChain4(iface);
    BOOL windowed;
    HRESULT hr;

    TRACE("iface %p, fullscreen %p, target %p.\n", iface, fullscreen, target);

    if (fullscreen || target)
    {
        wined3d_mutex_lock();
        windowed = wined3d_swapchain_state_is_windowed(swapchain->state);
        wined3d_mutex_unlock();
    }

    if (fullscreen)
        *fullscreen = !windowed;

    if (target)
    {
        if (!windowed)
        {
            if (!swapchain->target && FAILED(hr = IDXGISwapChain4_GetContainingOutput(iface,
                    &swapchain->target)))
                return hr;

            *target = swapchain->target;
            IDXGIOutput_AddRef(*target);
        }
        else
        {
            *target = NULL;
        }
    }

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_GetDesc(IDXGISwapChain4 *iface, DXGI_SWAP_CHAIN_DESC *desc)
{
    struct d3d12_swapchain *swapchain = d3d12_swapchain_from_IDXGISwapChain4(iface);
    const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *fullscreen_desc = &swapchain->fullscreen_desc;
    const DXGI_SWAP_CHAIN_DESC1 *swapchain_desc = &swapchain->desc;
    BOOL windowed;

    TRACE("iface %p, desc %p.\n", iface, desc);

    if (!desc)
    {
        WARN("Invalid pointer.\n");
        return E_INVALIDARG;
    }

    wined3d_mutex_lock();
    windowed = wined3d_swapchain_state_is_windowed(swapchain->state);
    wined3d_mutex_unlock();

    desc->BufferDesc.Width = swapchain_desc->Width;
    desc->BufferDesc.Height = swapchain_desc->Height;
    desc->BufferDesc.RefreshRate = fullscreen_desc->RefreshRate;
    desc->BufferDesc.Format = swapchain_desc->Format;
    desc->BufferDesc.ScanlineOrdering = fullscreen_desc->ScanlineOrdering;
    desc->BufferDesc.Scaling = fullscreen_desc->Scaling;
    desc->SampleDesc = swapchain_desc->SampleDesc;
    desc->BufferUsage = swapchain_desc->BufferUsage;
    desc->BufferCount = swapchain_desc->BufferCount;
    desc->OutputWindow = swapchain->window;
    desc->Windowed = windowed;
    desc->SwapEffect = swapchain_desc->SwapEffect;
    desc->Flags = swapchain_desc->Flags;

    return S_OK;
}

static HRESULT d3d12_swapchain_op_resize_buffers_execute(struct d3d12_swapchain *swapchain, struct d3d12_swapchain_op *op)
{
    d3d12_swapchain_destroy_vulkan_resources(swapchain);

    swapchain->backend_desc = op->resize_buffers.desc;

    return d3d12_swapchain_create_vulkan_resources(swapchain);
}

static HRESULT d3d12_swapchain_resize_buffers(struct d3d12_swapchain *swapchain,
        UINT buffer_count, UINT width, UINT height, DXGI_FORMAT format, UINT flags)
{
    static const UINT supported_flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH
            | DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
    DXGI_SWAP_CHAIN_DESC1 *desc, new_desc;
    struct d3d12_swapchain_op *op;
    unsigned int i;
    ULONG refcount;

    if (flags & ~supported_flags)
        FIXME("Ignoring flags %#x.\n", flags & ~supported_flags);

    if ((swapchain->desc.Flags ^ flags) & DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT)
        return E_INVALIDARG;

    for (i = 0; i < swapchain->desc.BufferCount; ++i)
    {
        ID3D12Resource_AddRef(swapchain->buffers[i]);
        if ((refcount = ID3D12Resource_Release(swapchain->buffers[i])))
        {
            WARN("Buffer %p has %lu references left.\n", swapchain->buffers[i], refcount);
            return DXGI_ERROR_INVALID_CALL;
        }
    }

    desc = &swapchain->desc;
    new_desc = swapchain->desc;
    new_desc.Flags = flags;

    if (buffer_count)
        new_desc.BufferCount = buffer_count;
    if (!width || !height)
    {
        RECT client_rect;

        if (!GetClientRect(swapchain->window, &client_rect))
        {
            WARN("Failed to get client rect, last error %#lx.\n", GetLastError());
            return DXGI_ERROR_INVALID_CALL;
        }

        if (!width)
            width = client_rect.right;
        if (!height)
            height = client_rect.bottom;
    }
    new_desc.Width = width;
    new_desc.Height = height;

    if (format)
        new_desc.Format = format;

    if (!dxgi_validate_swapchain_desc(&new_desc))
        return DXGI_ERROR_INVALID_CALL;

    if (desc->Width == new_desc.Width && desc->Height == new_desc.Height
            && desc->Format == new_desc.Format && desc->BufferCount == new_desc.BufferCount)
    {
        swapchain->current_buffer_index = 0;
        /* DXGI_SWAP_CHAIN_FLAG_GDI_COMPATIBLE is not applicable on D3D12. So there is no need to
         * recreate buffers when the flag gets toggled */
        swapchain->desc.Flags = flags;
        return S_OK;
    }

    if (!(op = calloc(1, sizeof(*op))))
    {
        WARN("Cannot allocate memory.\n");
        return E_OUTOFMEMORY;
    }

    op->type = D3D12_SWAPCHAIN_OP_RESIZE_BUFFERS;
    op->resize_buffers.vk_memory = swapchain->vk_memory;
    swapchain->vk_memory = VK_NULL_HANDLE;
    memcpy(&op->resize_buffers.vk_images, &swapchain->vk_images, sizeof(swapchain->vk_images));
    memset(&swapchain->vk_images, 0, sizeof(swapchain->vk_images));
    op->resize_buffers.desc = new_desc;

    EnterCriticalSection(&swapchain->worker_cs);
    list_add_tail(&swapchain->worker_ops, &op->entry);
    WakeAllConditionVariable(&swapchain->worker_cv);
    LeaveCriticalSection(&swapchain->worker_cs);

    swapchain->current_buffer_index = 0;

    d3d12_swapchain_destroy_d3d12_resources(swapchain);

    swapchain->desc = new_desc;

    return d3d12_swapchain_create_d3d12_resources(swapchain);
}

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_ResizeBuffers(IDXGISwapChain4 *iface,
        UINT buffer_count, UINT width, UINT height, DXGI_FORMAT format, UINT flags)
{
    struct d3d12_swapchain *swapchain = d3d12_swapchain_from_IDXGISwapChain4(iface);

    TRACE("iface %p, buffer_count %u, width %u, height %u, format %s, flags %#x.\n",
            iface, buffer_count, width, height, debug_dxgi_format(format), flags);

    return d3d12_swapchain_resize_buffers(swapchain, buffer_count, width, height, format, flags);
}

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_ResizeTarget(IDXGISwapChain4 *iface,
        const DXGI_MODE_DESC *target_mode_desc)
{
    struct d3d12_swapchain *swapchain = d3d12_swapchain_from_IDXGISwapChain4(iface);

    TRACE("iface %p, target_mode_desc %p.\n", iface, target_mode_desc);

    return dxgi_swapchain_resize_target(swapchain->state, target_mode_desc);
}

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_GetContainingOutput(IDXGISwapChain4 *iface,
        IDXGIOutput **output)
{
    struct d3d12_swapchain *swapchain = d3d12_swapchain_from_IDXGISwapChain4(iface);
    IUnknown *device_parent;
    IWineDXGIFactory *factory;
    IDXGIAdapter *adapter;
    HRESULT hr;

    TRACE("iface %p, output %p.\n", iface, output);

    if (swapchain->target)
    {
        IDXGIOutput_AddRef(*output = swapchain->target);
        return S_OK;
    }

    device_parent = vkd3d_get_device_parent(swapchain->device);

    if (FAILED(hr = IUnknown_QueryInterface(device_parent, &IID_IDXGIAdapter, (void **)&adapter)))
    {
        WARN("Failed to get adapter, hr %#lx.\n", hr);
        return hr;
    }

    if (FAILED(hr = IDXGIAdapter_GetParent(adapter, &IID_IWineDXGIFactory, (void **)&factory)))
    {
        WARN("Failed to get factory, hr %#lx.\n", hr);
        IDXGIAdapter_Release(adapter);
        return hr;
    }

    hr = dxgi_get_output_from_window(factory, swapchain->window, output);
    IWineDXGIFactory_Release(factory);
    IDXGIAdapter_Release(adapter);
    return hr;
}

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_GetFrameStatistics(IDXGISwapChain4 *iface,
        DXGI_FRAME_STATISTICS *stats)
{
    FIXME("iface %p, stats %p stub!\n", iface, stats);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_GetLastPresentCount(IDXGISwapChain4 *iface,
        UINT *last_present_count)
{
    struct d3d12_swapchain *swapchain = d3d12_swapchain_from_IDXGISwapChain4(iface);

    TRACE("iface %p, last_present_count %p.\n", iface, last_present_count);

    *last_present_count = swapchain->frame_number;

    return S_OK;
}

/* IDXGISwapChain1 methods */

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_GetDesc1(IDXGISwapChain4 *iface, DXGI_SWAP_CHAIN_DESC1 *desc)
{
    struct d3d12_swapchain *swapchain = d3d12_swapchain_from_IDXGISwapChain4(iface);

    TRACE("iface %p, desc %p.\n", iface, desc);

    if (!desc)
    {
        WARN("Invalid pointer.\n");
        return E_INVALIDARG;
    }

    *desc = swapchain->desc;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_GetFullscreenDesc(IDXGISwapChain4 *iface,
        DXGI_SWAP_CHAIN_FULLSCREEN_DESC *desc)
{
    struct d3d12_swapchain *swapchain = d3d12_swapchain_from_IDXGISwapChain4(iface);
    BOOL windowed;

    TRACE("iface %p, desc %p.\n", iface, desc);

    if (!desc)
    {
        WARN("Invalid pointer.\n");
        return E_INVALIDARG;
    }

    wined3d_mutex_lock();
    windowed = wined3d_swapchain_state_is_windowed(swapchain->state);
    wined3d_mutex_unlock();

    *desc = swapchain->fullscreen_desc;
    desc->Windowed = windowed;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_GetHwnd(IDXGISwapChain4 *iface, HWND *hwnd)
{
    struct d3d12_swapchain *swapchain = d3d12_swapchain_from_IDXGISwapChain4(iface);

    TRACE("iface %p, hwnd %p.\n", iface, hwnd);

    if (!hwnd)
    {
        WARN("Invalid pointer.\n");
        return DXGI_ERROR_INVALID_CALL;
    }

    *hwnd = swapchain->window;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_GetCoreWindow(IDXGISwapChain4 *iface,
        REFIID iid, void **core_window)
{
    FIXME("iface %p, iid %s, core_window %p stub!\n", iface, debugstr_guid(iid), core_window);

    if (core_window)
        *core_window = NULL;

    return DXGI_ERROR_INVALID_CALL;
}

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_Present1(IDXGISwapChain4 *iface,
        UINT sync_interval, UINT flags, const DXGI_PRESENT_PARAMETERS *present_parameters)
{
    struct d3d12_swapchain *swapchain = d3d12_swapchain_from_IDXGISwapChain4(iface);

    TRACE("iface %p, sync_interval %u, flags %#x, present_parameters %p.\n",
            iface, sync_interval, flags, present_parameters);

    if (present_parameters)
        FIXME("Ignored present parameters %p.\n", present_parameters);

    return d3d12_swapchain_present(swapchain, sync_interval, flags);
}

static BOOL STDMETHODCALLTYPE d3d12_swapchain_IsTemporaryMonoSupported(IDXGISwapChain4 *iface)
{
    FIXME("iface %p stub!\n", iface);

    return FALSE;
}

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_GetRestrictToOutput(IDXGISwapChain4 *iface, IDXGIOutput **output)
{
    FIXME("iface %p, output %p stub!\n", iface, output);

    if (!output)
    {
        WARN("Invalid pointer.\n");
        return E_INVALIDARG;
    }

    *output = NULL;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_SetBackgroundColor(IDXGISwapChain4 *iface, const DXGI_RGBA *color)
{
    FIXME("iface %p, color %p stub!\n", iface, color);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_GetBackgroundColor(IDXGISwapChain4 *iface, DXGI_RGBA *color)
{
    FIXME("iface %p, color %p stub!\n", iface, color);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_SetRotation(IDXGISwapChain4 *iface, DXGI_MODE_ROTATION rotation)
{
    FIXME("iface %p, rotation %#x stub!\n", iface, rotation);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_GetRotation(IDXGISwapChain4 *iface, DXGI_MODE_ROTATION *rotation)
{
    FIXME("iface %p, rotation %p stub!\n", iface, rotation);

    return E_NOTIMPL;
}

/* IDXGISwapChain2 methods */

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_SetSourceSize(IDXGISwapChain4 *iface, UINT width, UINT height)
{
    FIXME("iface %p, width %u, height %u stub!\n", iface, width, height);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_GetSourceSize(IDXGISwapChain4 *iface, UINT *width, UINT *height)
{
    FIXME("iface %p, width %p, height %p stub!\n", iface, width, height);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_SetMaximumFrameLatency(IDXGISwapChain4 *iface, UINT max_latency)
{
    struct d3d12_swapchain *swapchain = d3d12_swapchain_from_IDXGISwapChain4(iface);

    TRACE("iface %p, max_latency %u.\n", iface, max_latency);

    if (!(swapchain->desc.Flags & DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT))
    {
        WARN("DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT not set for swap chain %p.\n", iface);
        return DXGI_ERROR_INVALID_CALL;
    }

    if (!max_latency)
    {
        WARN("Invalid maximum frame latency %u.\n", max_latency);
        return DXGI_ERROR_INVALID_CALL;
    }

    if (max_latency > swapchain->frame_latency)
    {
        if (!ReleaseSemaphore(swapchain->frame_latency_semaphore, max_latency - swapchain->frame_latency, NULL))
        {
            ERR("Failed to release frame latency semaphore, last error %lu.\n", GetLastError());
            return HRESULT_FROM_WIN32(GetLastError());
        }
    }

    swapchain->frame_latency = max_latency;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_GetMaximumFrameLatency(IDXGISwapChain4 *iface, UINT *max_latency)
{
    struct d3d12_swapchain *swapchain = d3d12_swapchain_from_IDXGISwapChain4(iface);

    TRACE("iface %p, max_latency %p.\n", iface, max_latency);

    if (!(swapchain->desc.Flags & DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT))
    {
        WARN("DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT not set for swap chain %p.\n", iface);
        return DXGI_ERROR_INVALID_CALL;
    }

    *max_latency = swapchain->frame_latency;
    return S_OK;
}

static HANDLE STDMETHODCALLTYPE d3d12_swapchain_GetFrameLatencyWaitableObject(IDXGISwapChain4 *iface)
{
    struct d3d12_swapchain *swapchain = d3d12_swapchain_from_IDXGISwapChain4(iface);
    HANDLE dup;
    BOOL ret;

    TRACE("iface %p.\n", iface);

    if (!(swapchain->desc.Flags & DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT))
    {
        WARN("DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT not set for swap chain %p.\n", iface);
        return NULL;
    }

    ret = DuplicateHandle(GetCurrentProcess(), swapchain->frame_latency_semaphore, GetCurrentProcess(),
            &dup, 0, FALSE, DUPLICATE_SAME_ACCESS);

    if (!ret)
    {
        ERR("Cannot duplicate handle, last error %lu.\n", GetLastError());
        return NULL;
    }

    return dup;
}

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_SetMatrixTransform(IDXGISwapChain4 *iface,
        const DXGI_MATRIX_3X2_F *matrix)
{
    FIXME("iface %p, matrix %p stub!\n", iface, matrix);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_GetMatrixTransform(IDXGISwapChain4 *iface,
        DXGI_MATRIX_3X2_F *matrix)
{
    FIXME("iface %p, matrix %p stub!\n", iface, matrix);

    return E_NOTIMPL;
}

/* IDXGISwapChain3 methods */

static UINT STDMETHODCALLTYPE d3d12_swapchain_GetCurrentBackBufferIndex(IDXGISwapChain4 *iface)
{
    struct d3d12_swapchain *swapchain = d3d12_swapchain_from_IDXGISwapChain4(iface);

    TRACE("iface %p.\n", iface);

    TRACE("Current back buffer index %u.\n", swapchain->current_buffer_index);
    assert(swapchain->current_buffer_index < swapchain->desc.BufferCount);
    return swapchain->current_buffer_index;
}

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_CheckColorSpaceSupport(IDXGISwapChain4 *iface,
        DXGI_COLOR_SPACE_TYPE colour_space, UINT *colour_space_support)
{
    UINT support_flags = 0;

    FIXME("iface %p, colour_space %#x, colour_space_support %p semi-stub!\n",
            iface, colour_space, colour_space_support);

    if (!colour_space_support)
        return E_INVALIDARG;

    if (colour_space == DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709)
      support_flags |= DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT;

    *colour_space_support = support_flags;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_SetColorSpace1(IDXGISwapChain4 *iface,
        DXGI_COLOR_SPACE_TYPE colour_space)
{
    FIXME("iface %p, colour_space %#x semi-stub!\n", iface, colour_space);

    if (colour_space != DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709)
    {
        WARN("Colour space %u not supported.\n", colour_space);
        return E_INVALIDARG;
    }

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_ResizeBuffers1(IDXGISwapChain4 *iface,
        UINT buffer_count, UINT width, UINT height, DXGI_FORMAT format, UINT flags,
        const UINT *node_mask, IUnknown * const *present_queue)
{
    struct d3d12_swapchain *swapchain = d3d12_swapchain_from_IDXGISwapChain4(iface);
    size_t i, count;

    TRACE("iface %p, buffer_count %u, width %u, height %u, format %s, flags %#x, "
            "node_mask %p, present_queue %p.\n",
            iface, buffer_count, width, height, debug_dxgi_format(format), flags, node_mask, present_queue);

    if (!node_mask || !present_queue)
        return DXGI_ERROR_INVALID_CALL;

    count = buffer_count ? buffer_count : swapchain->desc.BufferCount;
    for (i = 0; i < count; ++i)
    {
        if (node_mask[i] > 1 || !present_queue[i])
            return DXGI_ERROR_INVALID_CALL;
        if ((ID3D12CommandQueue*)present_queue[i] != swapchain->command_queue)
            FIXME("Ignoring present queue %p.\n", present_queue[i]);
    }

    return d3d12_swapchain_resize_buffers(swapchain, buffer_count, width, height, format, flags);
}

/* IDXGISwapChain4 methods */

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_SetHDRMetaData(IDXGISwapChain4 *iface,
        DXGI_HDR_METADATA_TYPE type, UINT size, void *metadata)
{
    FIXME("iface %p, type %#x, size %#x, metadata %p stub!\n", iface, type, size, metadata);

    return E_NOTIMPL;
}

static const struct IDXGISwapChain4Vtbl d3d12_swapchain_vtbl =
{
    /* IUnknown methods */
    d3d12_swapchain_QueryInterface,
    d3d12_swapchain_AddRef,
    d3d12_swapchain_Release,
    /* IDXGIObject methods */
    d3d12_swapchain_SetPrivateData,
    d3d12_swapchain_SetPrivateDataInterface,
    d3d12_swapchain_GetPrivateData,
    d3d12_swapchain_GetParent,
    /* IDXGIDeviceSubObject methods */
    d3d12_swapchain_GetDevice,
    /* IDXGISwapChain methods */
    d3d12_swapchain_Present,
    d3d12_swapchain_GetBuffer,
    d3d12_swapchain_SetFullscreenState,
    d3d12_swapchain_GetFullscreenState,
    d3d12_swapchain_GetDesc,
    d3d12_swapchain_ResizeBuffers,
    d3d12_swapchain_ResizeTarget,
    d3d12_swapchain_GetContainingOutput,
    d3d12_swapchain_GetFrameStatistics,
    d3d12_swapchain_GetLastPresentCount,
    /* IDXGISwapChain1 methods */
    d3d12_swapchain_GetDesc1,
    d3d12_swapchain_GetFullscreenDesc,
    d3d12_swapchain_GetHwnd,
    d3d12_swapchain_GetCoreWindow,
    d3d12_swapchain_Present1,
    d3d12_swapchain_IsTemporaryMonoSupported,
    d3d12_swapchain_GetRestrictToOutput,
    d3d12_swapchain_SetBackgroundColor,
    d3d12_swapchain_GetBackgroundColor,
    d3d12_swapchain_SetRotation,
    d3d12_swapchain_GetRotation,
    /* IDXGISwapChain2 methods */
    d3d12_swapchain_SetSourceSize,
    d3d12_swapchain_GetSourceSize,
    d3d12_swapchain_SetMaximumFrameLatency,
    d3d12_swapchain_GetMaximumFrameLatency,
    d3d12_swapchain_GetFrameLatencyWaitableObject,
    d3d12_swapchain_SetMatrixTransform,
    d3d12_swapchain_GetMatrixTransform,
    /* IDXGISwapChain3 methods */
    d3d12_swapchain_GetCurrentBackBufferIndex,
    d3d12_swapchain_CheckColorSpaceSupport,
    d3d12_swapchain_SetColorSpace1,
    d3d12_swapchain_ResizeBuffers1,
    /* IDXGISwapChain4 methods */
    d3d12_swapchain_SetHDRMetaData,
};

static BOOL init_vk_funcs(struct dxgi_vk_funcs *dxgi, VkInstance vk_instance, VkDevice vk_device)
{
    PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr;
    PFN_vkGetDeviceProcAddr vkGetDeviceProcAddr;

    dxgi->vulkan_module = LoadLibraryA("vulkan-1.dll");
    if (!(vkGetInstanceProcAddr = (void *)GetProcAddress(dxgi->vulkan_module, "vkGetInstanceProcAddr")))
    {
        ERR_(winediag)("Failed to load Vulkan.\n");
        return FALSE;
    }

    vkGetDeviceProcAddr = (void *)vkGetInstanceProcAddr(vk_instance, "vkGetDeviceProcAddr");

#define LOAD_INSTANCE_PFN(name) \
    if (!(dxgi->p_##name = (void *)vkGetInstanceProcAddr(vk_instance, #name))) \
    { \
        ERR("Failed to get instance proc "#name".\n"); \
        FreeLibrary(dxgi->vulkan_module); \
        return FALSE; \
    }
    LOAD_INSTANCE_PFN(vkCreateWin32SurfaceKHR)
    LOAD_INSTANCE_PFN(vkDestroySurfaceKHR)
    LOAD_INSTANCE_PFN(vkGetPhysicalDeviceMemoryProperties)
    LOAD_INSTANCE_PFN(vkGetPhysicalDeviceSurfaceCapabilitiesKHR)
    LOAD_INSTANCE_PFN(vkGetPhysicalDeviceSurfaceFormatsKHR)
    LOAD_INSTANCE_PFN(vkGetPhysicalDeviceSurfacePresentModesKHR)
    LOAD_INSTANCE_PFN(vkGetPhysicalDeviceSurfaceSupportKHR)
    LOAD_INSTANCE_PFN(vkGetPhysicalDeviceWin32PresentationSupportKHR)
#undef LOAD_INSTANCE_PFN

#define LOAD_DEVICE_PFN(name) \
    if (!(dxgi->p_##name = (void *)vkGetDeviceProcAddr(vk_device, #name))) \
    { \
        ERR("Failed to get device proc "#name".\n"); \
        FreeLibrary(dxgi->vulkan_module); \
        return FALSE; \
    }
    LOAD_DEVICE_PFN(vkAcquireNextImageKHR)
    LOAD_DEVICE_PFN(vkAllocateCommandBuffers)
    LOAD_DEVICE_PFN(vkAllocateMemory)
    LOAD_DEVICE_PFN(vkBeginCommandBuffer)
    LOAD_DEVICE_PFN(vkBindImageMemory)
    LOAD_DEVICE_PFN(vkCmdBlitImage)
    LOAD_DEVICE_PFN(vkCmdPipelineBarrier)
    LOAD_DEVICE_PFN(vkCreateCommandPool)
    LOAD_DEVICE_PFN(vkCreateFence)
    LOAD_DEVICE_PFN(vkCreateImage)
    LOAD_DEVICE_PFN(vkCreateSemaphore)
    LOAD_DEVICE_PFN(vkCreateSwapchainKHR)
    LOAD_DEVICE_PFN(vkDestroyCommandPool)
    LOAD_DEVICE_PFN(vkDestroyFence)
    LOAD_DEVICE_PFN(vkDestroyImage)
    LOAD_DEVICE_PFN(vkDestroySemaphore)
    LOAD_DEVICE_PFN(vkDestroySwapchainKHR)
    LOAD_DEVICE_PFN(vkEndCommandBuffer)
    LOAD_DEVICE_PFN(vkFreeMemory)
    LOAD_DEVICE_PFN(vkResetCommandBuffer)
    LOAD_DEVICE_PFN(vkGetImageMemoryRequirements)
    LOAD_DEVICE_PFN(vkGetSwapchainImagesKHR)
    LOAD_DEVICE_PFN(vkQueuePresentKHR)
    LOAD_DEVICE_PFN(vkQueueSubmit)
    LOAD_DEVICE_PFN(vkQueueWaitIdle)
    LOAD_DEVICE_PFN(vkResetFences)
    LOAD_DEVICE_PFN(vkWaitForFences)
#undef LOAD_DEVICE_PFN

    return TRUE;
}

static inline struct d3d12_swapchain *d3d12_swapchain_from_wined3d_swapchain_state_parent(struct wined3d_swapchain_state_parent *parent)
{
    return CONTAINING_RECORD(parent, struct d3d12_swapchain, state_parent);
}

static void CDECL d3d12_swapchain_windowed_state_changed(struct wined3d_swapchain_state_parent *parent,
        BOOL windowed)
{
    struct d3d12_swapchain *swapchain = d3d12_swapchain_from_wined3d_swapchain_state_parent(parent);

    TRACE("parent %p, windowed %d.\n", parent, windowed);

    if (windowed && swapchain->target)
    {
        IDXGIOutput_Release(swapchain->target);
        swapchain->target = NULL;
    }
}

static const struct wined3d_swapchain_state_parent_ops d3d12_swapchain_state_parent_ops =
{
    d3d12_swapchain_windowed_state_changed,
};

static HRESULT d3d12_swapchain_init(struct d3d12_swapchain *swapchain, IWineDXGIFactory *factory,
        ID3D12Device *device, ID3D12CommandQueue *queue, HWND window,
        const DXGI_SWAP_CHAIN_DESC1 *swapchain_desc, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *fullscreen_desc)
{
    const struct dxgi_vk_funcs *vk_funcs = &swapchain->vk_funcs;
    struct wined3d_swapchain_desc wined3d_desc;
    VkWin32SurfaceCreateInfoKHR surface_desc;
    VkPhysicalDevice vk_physical_device;
    struct dxgi_factory *dxgi_factory;
    VkFenceCreateInfo fence_desc;
    uint32_t queue_family_index;
    VkSurfaceKHR vk_surface;
    VkInstance vk_instance;
    IDXGIOutput *output;
    VkBool32 supported;
    VkDevice vk_device;
    VkFence vk_fence;
    bool fullscreen;
    VkResult vr;
    HRESULT hr;

    if (window == GetDesktopWindow())
    {
        WARN("D3D12 swapchain cannot be created on desktop window.\n");
        return E_ACCESSDENIED;
    }

    swapchain->IDXGISwapChain4_iface.lpVtbl = &d3d12_swapchain_vtbl;
    swapchain->state_parent.ops = &d3d12_swapchain_state_parent_ops;
    swapchain->refcount = 1;

    swapchain->window = window;
    swapchain->desc = *swapchain_desc;
    swapchain->backend_desc = *swapchain_desc;
    swapchain->fullscreen_desc = *fullscreen_desc;

    swapchain->present_mode = VK_PRESENT_MODE_FIFO_KHR;

    switch (swapchain_desc->SwapEffect)
    {
        case DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL:
        case DXGI_SWAP_EFFECT_FLIP_DISCARD:
            FIXME("Ignoring swap effect %#x.\n", swapchain_desc->SwapEffect);
            break;
        default:
            WARN("Invalid swap effect %#x.\n", swapchain_desc->SwapEffect);
            return DXGI_ERROR_INVALID_CALL;
    }

    if (FAILED(hr = dxgi_get_output_from_window(factory, window, &output)))
    {
        WARN("Failed to get output from window %p, hr %#lx.\n", window, hr);
        return hr;
    }

    hr = wined3d_swapchain_desc_from_dxgi(&wined3d_desc, output, window, swapchain_desc,
            fullscreen_desc);
    if (FAILED(hr))
    {
        IDXGIOutput_Release(output);
        return hr;
    }

    fullscreen = !wined3d_desc.windowed;
    wined3d_desc.windowed = TRUE;

    dxgi_factory = unsafe_impl_from_IDXGIFactory((IDXGIFactory *)factory);
    if (FAILED(hr = wined3d_swapchain_state_create(&wined3d_desc, window, dxgi_factory->wined3d,
            &swapchain->state_parent, &swapchain->state)))
    {
        IDXGIOutput_Release(output);
        return hr;
    }

    wined3d_swapchain_state_get_size(swapchain->state, &swapchain->desc.Width, &swapchain->desc.Height);

    if (fullscreen)
    {
        wined3d_desc.windowed = FALSE;
        hr = wined3d_swapchain_state_set_fullscreen(swapchain->state, &wined3d_desc, NULL);
        if (FAILED(hr))
        {
            wined3d_swapchain_state_destroy(swapchain->state);
            IDXGIOutput_Release(output);
            return hr;
        }

        swapchain->target = output;
    }
    else
    {
        IDXGIOutput_Release(output);
    }

    if (swapchain_desc->BufferUsage & ~(DXGI_USAGE_RENDER_TARGET_OUTPUT | DXGI_USAGE_SHADER_INPUT))
        FIXME("Ignoring buffer usage %#x.\n", swapchain_desc->BufferUsage & ~(DXGI_USAGE_RENDER_TARGET_OUTPUT | DXGI_USAGE_SHADER_INPUT));
    if (swapchain_desc->Scaling != DXGI_SCALING_STRETCH && swapchain_desc->Scaling != DXGI_SCALING_NONE)
        FIXME("Ignoring scaling %#x.\n", swapchain_desc->Scaling);
    if (swapchain_desc->AlphaMode && swapchain_desc->AlphaMode != DXGI_ALPHA_MODE_IGNORE)
        FIXME("Ignoring alpha mode %#x.\n", swapchain_desc->AlphaMode);
    if (swapchain_desc->Flags & ~(DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT))
        FIXME("Ignoring swapchain flags %#x.\n", swapchain_desc->Flags);

    if (fullscreen_desc->RefreshRate.Numerator || fullscreen_desc->RefreshRate.Denominator)
        FIXME("Ignoring refresh rate.\n");
    if (fullscreen_desc->ScanlineOrdering)
        FIXME("Unhandled scanline ordering %#x.\n", fullscreen_desc->ScanlineOrdering);
    if (fullscreen_desc->Scaling)
        FIXME("Unhandled mode scaling %#x.\n", fullscreen_desc->Scaling);

    vk_instance = vkd3d_instance_get_vk_instance(vkd3d_instance_from_device(device));
    vk_physical_device = vkd3d_get_vk_physical_device(device);
    vk_device = vkd3d_get_vk_device(device);

    swapchain->vk_instance = vk_instance;
    swapchain->vk_device = vk_device;
    swapchain->vk_physical_device = vk_physical_device;

    if (!init_vk_funcs(&swapchain->vk_funcs, vk_instance, vk_device))
    {
        wined3d_swapchain_state_destroy(swapchain->state);
        return E_FAIL;
    }

    wined3d_private_store_init(&swapchain->private_store);

    InitializeCriticalSection(&swapchain->worker_cs);
    InitializeConditionVariable(&swapchain->worker_cv);
    swapchain->worker_running = true;
    list_init(&swapchain->worker_ops);

    surface_desc.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    surface_desc.pNext = NULL;
    surface_desc.flags = 0;
    surface_desc.hinstance = GetModuleHandleA("dxgi.dll");
    surface_desc.hwnd = window;
    if ((vr = vk_funcs->p_vkCreateWin32SurfaceKHR(vk_instance, &surface_desc, NULL, &vk_surface)) < 0)
    {
        WARN("Failed to create Vulkan surface, vr %d.\n", vr);
        d3d12_swapchain_destroy(swapchain);
        return hresult_from_vk_result(vr);
    }
    swapchain->vk_surface = vk_surface;

    queue_family_index = vkd3d_get_vk_queue_family_index(queue);
    if ((vr = vk_funcs->p_vkGetPhysicalDeviceSurfaceSupportKHR(vk_physical_device,
            queue_family_index, vk_surface, &supported)) < 0 || !supported)
    {
        FIXME("Queue family does not support presentation, vr %d.\n", vr);
        d3d12_swapchain_destroy(swapchain);
        return DXGI_ERROR_UNSUPPORTED;
    }

    ID3D12CommandQueue_AddRef(swapchain->command_queue = queue);
    ID3D12Device_AddRef(swapchain->device = device);

    if (FAILED(hr = d3d12_swapchain_create_d3d12_resources(swapchain)))
    {
        d3d12_swapchain_destroy(swapchain);
        return hr;
    }

    if (FAILED(hr = d3d12_swapchain_create_vulkan_resources(swapchain)))
    {
        d3d12_swapchain_destroy(swapchain);
        return hr;
    }

    fence_desc.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_desc.pNext = NULL;
    fence_desc.flags = 0;
    if ((vr = vk_funcs->p_vkCreateFence(vk_device, &fence_desc, NULL, &vk_fence)) < 0)
    {
        WARN("Failed to create Vulkan fence, vr %d.\n", vr);
        d3d12_swapchain_destroy(swapchain);
        return hresult_from_vk_result(vr);
    }
    swapchain->vk_fence = vk_fence;

    swapchain->current_buffer_index = 0;

    if (swapchain_desc->Flags & DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT)
        swapchain->frame_latency = 1;
    else
        swapchain->frame_latency = 3;

    if (!(swapchain->frame_latency_semaphore = CreateSemaphoreW(NULL, swapchain->frame_latency, LONG_MAX, NULL)))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        WARN("Failed to create frame latency semaphore, hr %#lx.\n", hr);
        d3d12_swapchain_destroy(swapchain);
        return hr;
    }

    if (FAILED(hr = ID3D12Device_CreateFence(device, 0, 0,
            &IID_ID3D12Fence, (void **)&swapchain->present_fence)))
    {
        WARN("Failed to create present fence, hr %#lx.\n", hr);
        d3d12_swapchain_destroy(swapchain);
        return hr;
    }

    IWineDXGIFactory_AddRef(swapchain->factory = factory);

    if (!(swapchain->worker_thread = CreateThread(NULL, 0, d3d12_swapchain_worker_proc, swapchain, 0, NULL)))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        WARN("Failed to create worker thread, hr %#lx.\n", hr);
        d3d12_swapchain_destroy(swapchain);
        return hr;
    }

    return S_OK;
}

HRESULT d3d12_swapchain_create(IWineDXGIFactory *factory, ID3D12CommandQueue *queue, HWND window,
        const DXGI_SWAP_CHAIN_DESC1 *swapchain_desc, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *fullscreen_desc,
        IDXGISwapChain1 **swapchain)
{
    struct D3D12_COMMAND_QUEUE_DESC queue_desc;
    struct d3d12_swapchain *object;
    ID3D12Device *device;
    HRESULT hr;

    if (swapchain_desc->Format == DXGI_FORMAT_UNKNOWN)
        return DXGI_ERROR_INVALID_CALL;

    queue_desc = ID3D12CommandQueue_GetDesc(queue);
    if (queue_desc.Type != D3D12_COMMAND_LIST_TYPE_DIRECT)
        return DXGI_ERROR_INVALID_CALL;

    if (!(object = calloc(1, sizeof(*object))))
        return E_OUTOFMEMORY;

    if (FAILED(hr = ID3D12CommandQueue_GetDevice(queue, &IID_ID3D12Device, (void **)&device)))
    {
        ERR("Failed to get d3d12 device, hr %#lx.\n", hr);
        free(object);
        return hr;
    }

    hr = d3d12_swapchain_init(object, factory, device, queue, window, swapchain_desc, fullscreen_desc);
    ID3D12Device_Release(device);
    if (FAILED(hr))
    {
        free(object);
        return hr;
    }

    TRACE("Created swapchain %p.\n", object);

    *swapchain = (IDXGISwapChain1 *)&object->IDXGISwapChain4_iface;

    return S_OK;
}
