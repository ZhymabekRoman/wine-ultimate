/*
 * Copyright 2020 Nikolay Sivov for CodeWeavers
 * Copyright 2023 Zhiyi Zhang for CodeWeavers
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
#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "ntstatus.h"
#define WIN32_NO_STATUS
#define COBJMACROS
#include "windef.h"
#include "winbase.h"
#include "wingdi.h"
#include "winternl.h"
#include "objidl.h"
#include "dcomp_private.h"
#include "dxgi.h"
#include "d3d11_4.h"
#include "wine/server.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(dcomp);

HWND g_dcomp_captured_top_hwnd;

/*
 * A shared visual handle is created and consumed inside one process; the
 * window server only maps it to the root pointer of the target that published
 * it. That mapping is a window server round trip, and the server is the queue
 * every thread of the process is serialised on, so it is cached in process.
 * The cache is written by the publisher and refreshed by the composite tick
 * outside dcomp_cs; code that owns dcomp_cs only reads memory.
 */
struct shared_visual_entry
{
    HANDLE handle;
    IDCompositionVisual *root;
    struct list entry;
};

static IDCompositionVisual *shared_visual_cache_lookup(HANDLE handle);

static struct list shared_visual_cache = LIST_INIT(shared_visual_cache);

static IDCompositionVisual *shared_visual_cache_lookup(HANDLE handle)
{
    struct shared_visual_entry *entry;

    LIST_FOR_EACH_ENTRY(entry, &shared_visual_cache, struct shared_visual_entry, entry)
        if (entry->handle == handle)
            return entry->root;

    return NULL;
}

/* dcomp_cs held. */
void shared_visual_cache_set(HANDLE handle, IDCompositionVisual *root)
{
    struct shared_visual_entry *entry;

    LIST_FOR_EACH_ENTRY(entry, &shared_visual_cache, struct shared_visual_entry, entry)
    {
        if (entry->handle == handle)
        {
            if (entry->root != root)
                InterlockedIncrement(&dcomp_tree_version);
            entry->root = root;
            return;
        }
    }

    if ((entry = malloc(sizeof(*entry))))
    {
        entry->handle = handle;
        entry->root = root;
        list_add_tail(&shared_visual_cache, &entry->entry);
        InterlockedIncrement(&dcomp_tree_version);
    }
}

/* Root of a shared visual published by another process.
 *
 * The publisher's root is a pointer into its address space, so the consumer
 * cannot dereference it. Build a local stand-in (a 'proxy') once per handle and
 * hand that to the visual tree walk: it carries the published size, the shared
 * resource handle of the publisher's surface, and it is a struct this process
 * can read. The proxy is referenced by the cache, which lives for the lifetime
 * of the process, so the reference create_visual() hands out is kept here. */
static IDCompositionVisual *shared_visual_proxy_root(HANDLE shared_visual_handle, HANDLE surface,
        UINT width, UINT height, unsigned int dirty_count)
{
    IDCompositionVisual *root;
    struct composition_visual *impl;

    dcomp_lock();
    if ((root = shared_visual_cache_lookup(shared_visual_handle)))
    {
        impl = impl_from_IDCompositionVisual(root);
        if (impl->is_shared_proxy)
        {
            /* Republish: a resized surface, a new content handle after a
             * device lost, or a new frame presented by the producer. Bump the change
             * counter only when the server generation or geometry/surface changed. */
            if (impl->size.cx != (LONG)width || impl->size.cy != (LONG)height)
            {
                impl->size.cx = width;
                impl->size.cy = height;
                impl->content_version++;
            }
            if (surface && surface != impl->shared_content_handle)
            {
                HANDLE cap_surf = (impl->captured_hwnd && IsWindow(impl->captured_hwnd))
                        ? GetPropW(impl->captured_hwnd, L"__wine_dcomp_hwnd_surface") : NULL;
                if (!cap_surf || cap_surf == surface)
                {
                    impl->shared_content_handle = surface;
                    impl->content_version++;
                }
            }
            if (dirty_count != impl->shared_dirty_count)
            {
                impl->shared_dirty_count = dirty_count;
                impl->content_version++;
            }
            if (impl->shm_ptr)
            {
                UINT seq = ((const volatile UINT *)impl->shm_ptr)[3];
                if (seq && !(seq & 1) && seq != impl->shm_last_seq)
                    impl->content_version++;
            }
            dcomp_unlock();
            return root;
        }
    }
    dcomp_unlock();

    if (FAILED(create_visual(2, &IID_IDCompositionVisual, (void **)&root)))
        return NULL;

    impl = impl_from_IDCompositionVisual(root);
    impl->is_shared_proxy = TRUE;
    impl->shared_visual_handle = shared_visual_handle;
    impl->shared_content_handle = surface;
    impl->size.cx = width;
    impl->size.cy = height;
    impl->shared_dirty_count = dirty_count;
    impl->content_version = 1;
    InterlockedIncrement(&dcomp_tree_version);

    return root;
}

/* Window server round trip: must run with dcomp_cs released. */
IDCompositionVisual *shared_visual_query_root(HANDLE shared_visual_handle)
{
    IDCompositionVisual *root = NULL;
    IDCompositionVisual *proxy;
    NTSTATUS status;
    HANDLE surface = NULL;
    UINT width = 0, height = 0;
    unsigned int dirty_count = 0;
    DWORD owner = 0;

    SERVER_START_REQ(dcomp_get_shared_visual_info)
    {
        req->handle = wine_server_obj_handle(shared_visual_handle);
        if (!(status = wine_server_call(req)))
        {
            root = wine_server_get_ptr(reply->target_root);
            surface = wine_server_ptr_handle(reply->target_surface);
            width = reply->width;
            height = reply->height;
            dirty_count = reply->dirty_count;
            owner = reply->owner_pid;
        }
    }
    SERVER_END_REQ;

    if (status)
    {
        struct composition_visual *cached;

        ERR("dcomp_get_shared_visual_info failed, status %#lx.\n", status);
        cached = shared_visual_cached_root(shared_visual_handle);
        return cached ? (IDCompositionVisual *)&cached->IDCompositionVisualUnknown_iface : NULL;
    }

    /* Cross-process shared visual (Chromium's GPU process publishes, this
     * process consumes): target_root names a visual in the publisher's address
     * space, so use a local proxy bound to the surface it published. */
    if (owner && owner != GetCurrentProcessId() && (proxy = shared_visual_proxy_root(shared_visual_handle,
            surface, width, height, dirty_count)))
        root = proxy;

    /* Publish the mapping on every successful query, including a cleared root:
     * a target that lost its root must not keep serving the stale one. */
    dcomp_lock();
    shared_visual_cache_set(shared_visual_handle, root);
    dcomp_unlock();

    return root;
}

/* dcomp_cs held: memory only. */
struct composition_visual *shared_visual_cached_root(HANDLE shared_visual_handle)
{
    IDCompositionVisual *root = shared_visual_cache_lookup(shared_visual_handle);

    return root ? impl_from_IDCompositionVisual(root) : NULL;
}

static struct composition_visual *find_container_visual_in_tree(struct composition_visual *visual,
                                                                 HANDLE shared_handle,
                                                                 struct composition_visual *target_root)
{
    struct composition_visual *child, *found;

    if (!visual)
        return NULL;

    if (visual->shared_visual_handle && shared_handle)
    {
        if (dcomp_compare_handles(visual->shared_visual_handle, shared_handle))
            return visual;

        if (target_root && shared_visual_cached_root(visual->shared_visual_handle) == target_root)
            return visual;
    }

    LIST_FOR_EACH_ENTRY(child, &visual->child_visuals, struct composition_visual, entry)
    {
        if (target_root && child == target_root)
            return visual;
        found = find_container_visual_in_tree(child, shared_handle, target_root);
        if (found)
            return found;
    }

    return NULL;
}

struct composition_visual *find_root_visual_container(struct composition_target *target,
                                                       struct composition_target **out_host_target)
{
    struct composition_visual *target_root = target->root ? impl_from_IDCompositionVisual(target->root) : NULL;
    struct composition_visual *container = NULL;
    struct composition_device *dev;
    struct composition_target *t;

    if (out_host_target)
        *out_host_target = NULL;

    LIST_FOR_EACH_ENTRY(dev, &dcomp_devices, struct composition_device, dev_entry)
    {
        LIST_FOR_EACH_ENTRY(t, &dev->targets, struct composition_target, entry)
        {
            if (t == target || !t->root)
                continue;

            container = find_container_visual_in_tree(impl_from_IDCompositionVisual(t->root),
                                                        target->shared_visual_handle,
                                                        target_root);
            if (container)
            {
                if (out_host_target)
                    *out_host_target = t;
                return container;
            }
        }
    }

    return NULL;
}

struct composition_visual *resolve_shared_visual_root(HANDLE shared_handle)
{
    struct composition_visual *root = NULL;
    struct composition_device *dev;
    struct composition_target *target;

    if (!shared_handle)
        return NULL;

    root = shared_visual_cached_root(shared_handle);
    if (root && (root->content || !list_empty(&root->child_visuals)))
        return root;

    LIST_FOR_EACH_ENTRY(dev, &dcomp_devices, struct composition_device, dev_entry)
    {
        LIST_FOR_EACH_ENTRY(target, &dev->targets, struct composition_target, entry)
        {
            if (target->shared_visual_handle &&
                dcomp_compare_handles(target->shared_visual_handle, shared_handle))
            {
                if (target->root)
                    return impl_from_IDCompositionVisual(target->root);
            }
        }
    }

    return root;
}


BOOL visual_tree_has_content(struct composition_visual *visual)
{
    struct composition_visual *child;
    if (!visual) return FALSE;
    if (visual->content) return TRUE;
    LIST_FOR_EACH_ENTRY(child, &visual->child_visuals, struct composition_visual, entry)
    {
        if (visual_tree_has_content(child))
            return TRUE;
    }
    return FALSE;
}

static struct composition_target *find_in_process_target_for_root(const struct composition_target *exclude,
        HANDLE shared_handle, const struct composition_visual *child_root)
{
    struct composition_device *dev;
    struct composition_target *t;

    LIST_FOR_EACH_ENTRY(dev, &dcomp_devices, struct composition_device, dev_entry)
    {
        LIST_FOR_EACH_ENTRY(t, &dev->targets, struct composition_target, entry)
        {
            if (t == exclude || !t->root)
                continue;
            if (child_root && impl_from_IDCompositionVisual(t->root) == child_root)
                return t;
            if (shared_handle && t->shared_visual_handle
                    && dcomp_compare_handles(t->shared_visual_handle, shared_handle))
                return t;
        }
    }
    return NULL;
}

/* dcomp_cs held. */
BOOL shared_visual_has_in_process_target(HANDLE shared_handle, const struct composition_visual *child_root)
{
    return find_in_process_target_for_root(NULL, shared_handle, child_root) != NULL;
}

static BOOL visual_subtree_delegates_in_process(const struct composition_target *target,
        const struct composition_visual *visual, unsigned int depth, BOOL *found_delegate)
{
    const struct composition_visual *child;

    if (!visual || depth > 64)
        return TRUE;
    if (visual->content || visual->is_shared_proxy)
        return FALSE;
    if (visual->shared_visual_handle)
    {
        struct composition_visual *child_root = resolve_shared_visual_root(visual->shared_visual_handle);
        if (!child_root || child_root->is_shared_proxy)
            return FALSE;
        if (!find_in_process_target_for_root(target, visual->shared_visual_handle, child_root))
            return FALSE;
        *found_delegate = TRUE;
    }
    LIST_FOR_EACH_ENTRY(child, &visual->child_visuals, const struct composition_visual, entry)
    {
        if (!visual_subtree_delegates_in_process(target, child, depth + 1, found_delegate))
            return FALSE;
    }
    return TRUE;
}

/* Returns TRUE when 'target' has no local content or cross-process proxies of its own
 * and purely forwards through shared_visual_handle(s) to live in-process target(s)
 * that already composite their own visual trees to the resolved HWND. Skipping these
 * forwarding targets avoids creating competing swapchains and X11 client windows on
 * the same HWND (which otherwise expose/flush stale GDI surfaces and cause full-window
 * white flashes on popup/menu commits). */
BOOL target_delegates_to_in_process_target(const struct composition_target *target,
        const struct composition_visual *root)
{
    BOOL found_delegate = FALSE;

    if (!root)
        return FALSE;
    return visual_subtree_delegates_in_process(target, root, 0, &found_delegate) && found_delegate;
}

/* Surface of a shared visual published by another process.
 *
 * The publisher's content is a texture in the GPU's memory; a shared resource
 * is named by a handle that the window server resolves for any process, so
 * opening it on a device of this process yields a view of the same resource.
 * The device is created once per process and kept: shared-resource views are
 * not device specific, and creating one per frame would cost a device. */
IDXGISurface *shared_proxy_open_surface(struct composition_device *comp_dev, struct composition_visual *visual)
{
    static ID3D11Device *fallback_device;
    ID3D11Device *device = NULL;
    IUnknown *rendering_device = NULL;
    IDXGISurface *surface = NULL;
    HRESULT hr;

    if (!visual->shared_content_handle)
        return NULL;

    if (comp_dev && SUCCEEDED(device_get_or_create_rendering_device(comp_dev, &rendering_device)) && rendering_device)
    {
        IUnknown_QueryInterface(rendering_device, &IID_ID3D11Device, (void **)&device);
        IUnknown_Release(rendering_device);
    }
    if (!device)
    {
        if (!fallback_device)
        {
            if (FAILED(get_default_rendering_device(&rendering_device)) || !rendering_device)
                return NULL;
            fallback_device = (ID3D11Device *)rendering_device;
        }
        device = fallback_device;
        ID3D11Device_AddRef(device);
    }

    if (visual->shm_kmt_handle != visual->shared_content_handle || !visual->shm_ptr)
    {
        WCHAR shm_name[64];
        if (visual->shm_kmt_handle != visual->shared_content_handle)
        {
            if (visual->zc_tex)
            {
                ID3D11Texture2D_Release(visual->zc_tex);
                visual->zc_tex = NULL;
            }
            visual->zc_pid = 0;
            visual->zc_remote_handle = NULL;
            visual->zc_w = 0;
            visual->zc_h = 0;
            visual->zc_alloc_size = 0;
            visual->zc_gen = 0;
        }
        if (visual->shm_ptr) { UnmapViewOfFile(visual->shm_ptr); visual->shm_ptr = NULL; }
        if (visual->shm_map) { CloseHandle(visual->shm_map); visual->shm_map = NULL; }
        visual->shm_kmt_handle = visual->shared_content_handle;
        visual->shm_last_seq = 0;
        swprintf(shm_name, ARRAY_SIZE(shm_name), L"Local\\wine_dcomp_shm_%08x",
                (UINT)(ULONG_PTR)visual->shared_content_handle);
        if ((visual->shm_map = OpenFileMappingW(FILE_MAP_READ, FALSE, shm_name)))
            visual->shm_ptr = MapViewOfFile(visual->shm_map, FILE_MAP_READ, 0, 0, 0);
    }

    if (visual->shm_ptr)
    {
        const UINT *hdr = visual->shm_ptr;
        UINT s1 = hdr[3];
        MemoryBarrier();
        if (s1 && !(s1 & 1))
        {
            UINT w = hdr[0], h = hdr[1], stride = hdr[2];
            UINT magic = hdr[4];
            if (magic == 0x5A434750u && w > 0 && h > 0 && w <= 4096 && h <= 2160)
            {
                DWORD pid = hdr[5];
                HANDLE remote_h = (HANDLE)(ULONG_PTR)hdr[6];
                UINT alloc_sz = hdr[7];
                UINT gen = hdr[8];

                if (pid && remote_h && alloc_sz)
                {
                    if (!visual->zc_tex || visual->zc_pid != pid || visual->zc_remote_handle != remote_h
                            || visual->zc_w != w || visual->zc_h != h || visual->zc_alloc_size != alloc_sz
                            || visual->zc_gen != gen)
                    {
                        HANDLE src_proc = NULL, local_h = NULL;
                        ID3D11Texture2D *ztex = NULL;

                        if (visual->zc_tex)
                        {
                            ID3D11Texture2D_Release(visual->zc_tex);
                            visual->zc_tex = NULL;
                        }
                        if (pid == GetCurrentProcessId())
                        {
                            DuplicateHandle(GetCurrentProcess(), remote_h, GetCurrentProcess(), &local_h,
                                    0, FALSE, DUPLICATE_SAME_ACCESS);
                        }
                        else if ((src_proc = OpenProcess(PROCESS_DUP_HANDLE, FALSE, pid)))
                        {
                            DuplicateHandle(src_proc, remote_h, GetCurrentProcess(), &local_h,
                                    0, FALSE, DUPLICATE_SAME_ACCESS);
                            CloseHandle(src_proc);
                        }

                        if (local_h)
                        {
                            D3D11_TEXTURE2D_DESC tdesc = {0};
                            tdesc.Width = w;
                            tdesc.Height = h;
                            tdesc.MipLevels = 1;
                            tdesc.ArraySize = 1;
                            tdesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
                            tdesc.SampleDesc.Count = 1;
                            tdesc.Usage = D3D11_USAGE_DEFAULT;
                            tdesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                            if (SUCCEEDED(ID3D11Device_CreateTexture2D(device, &tdesc, NULL, &ztex)))
                            {
                                if (SUCCEEDED(dxgi_bind_external_memory((IUnknown *)ztex, local_h, alloc_sz)))
                                {
                                    visual->zc_tex = ztex;
                                    visual->zc_pid = pid;
                                    visual->zc_remote_handle = remote_h;
                                    visual->zc_w = w;
                                    visual->zc_h = h;
                                    visual->zc_alloc_size = alloc_sz;
                                    visual->zc_gen = gen;
                                    visual->size.cx = (LONG)w;
                                    visual->size.cy = (LONG)h;
                                    TRACE("DCOMP: Imported zero-copy GPU texture %ux%u (pid %lu, handle %p, size %u, gen %u)\n",
                                            w, h, pid, remote_h, alloc_sz, gen);
                                }
                                else
                                {
                                    ID3D11Texture2D_Release(ztex);
                                }
                            }
                            CloseHandle(local_h);
                        }
                    }

                    if (visual->zc_tex)
                    {
                        MemoryBarrier();
                        if (hdr[3] == s1)
                            visual->shm_last_seq = s1;
                        hr = ID3D11Texture2D_QueryInterface(visual->zc_tex, &IID_IDXGISurface, (void **)&surface);
                        ID3D11Device_Release(device);
                        return SUCCEEDED(hr) ? surface : NULL;
                    }
                }
            }
            if (magic != 0x5A434750u && w > 0 && h > 0 && w <= 4096 && h <= 2160 && stride >= w * 4)
            {
                if (!visual->shm_tex || visual->shm_tex_w != w || visual->shm_tex_h != h)
                {
                    D3D11_TEXTURE2D_DESC tdesc = {0};
                    if (visual->shm_tex)
                    {
                        ID3D11Texture2D_Release(visual->shm_tex);
                        visual->shm_tex = NULL;
                    }
                    tdesc.Width = w;
                    tdesc.Height = h;
                    tdesc.MipLevels = 1;
                    tdesc.ArraySize = 1;
                    tdesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
                    tdesc.SampleDesc.Count = 1;
                    tdesc.Usage = D3D11_USAGE_DEFAULT;
                    tdesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                    if (SUCCEEDED(ID3D11Device_CreateTexture2D(device, &tdesc, NULL, &visual->shm_tex)))
                    {
                        visual->shm_tex_w = w;
                        visual->shm_tex_h = h;
                        visual->size.cx = (LONG)w;
                        visual->size.cy = (LONG)h;
                    }
                }
                if (visual->shm_tex && s1 != visual->shm_last_seq)
                {
                    ID3D11DeviceContext *ctx = NULL;
                    ID3D11Device_GetImmediateContext(device, &ctx);
                    if (ctx)
                    {
                        ID3D11DeviceContext_UpdateSubresource(ctx, (ID3D11Resource *)visual->shm_tex,
                                0, NULL, (const BYTE *)hdr + 64, stride, 0);
                        ID3D11DeviceContext_Release(ctx);
                    }
                    MemoryBarrier();
                    if (hdr[3] == s1)
                        visual->shm_last_seq = s1;
                }
            }
        }
        if (visual->shm_tex)
        {
            hr = ID3D11Texture2D_QueryInterface(visual->shm_tex, &IID_IDXGISurface, (void **)&surface);
            ID3D11Device_Release(device);
            return SUCCEEDED(hr) ? surface : NULL;
        }
        ID3D11Device_Release(device);
        return NULL;
    }

    /* Producer has not created its SHM mapping yet; wait for first Present instead
     * of opening a blank unlinked cross-process D3D11 texture. */
    ID3D11Device_Release(device);
    return NULL;
}

static inline BOOL is_kmt_handle(HANDLE h)
{
    return ((ULONG_PTR)h & 0xc0000000) != 0;
}

HRESULT dcomp_create_surface_from_handle(struct composition_device *device,
        HANDLE handle, IUnknown **surface)
{
    IUnknown *rendering_device = NULL;
    ID3D11Device *d3d11_device = NULL;
    ID3D11Device1 *d3d11_device1 = NULL;
    IDXGISurface *dxgi_surface = NULL;
    HRESULT hr;

    TRACE("DCOMP: device_CreateSurfaceFromHandle device %p, handle %p, surface %p\n", device, handle, surface);

    if (!surface)
        return E_POINTER;
    *surface = NULL;

    if (!handle)
        return E_INVALIDARG;

    hr = device_get_or_create_rendering_device(device, &rendering_device);
    if (FAILED(hr) || !rendering_device)
        return hr;

    if (SUCCEEDED(IUnknown_QueryInterface(rendering_device, &IID_ID3D11Device1, (void **)&d3d11_device1)))
    {
        if (is_kmt_handle(handle))
            hr = ID3D11Device_OpenSharedResource((ID3D11Device *)d3d11_device1, handle, &IID_IDXGISurface, (void **)&dxgi_surface);
        else
            hr = ID3D11Device1_OpenSharedResource1(d3d11_device1, handle, &IID_IDXGISurface, (void **)&dxgi_surface);
        ID3D11Device1_Release(d3d11_device1);
    }
    else if (SUCCEEDED(IUnknown_QueryInterface(rendering_device, &IID_ID3D11Device, (void **)&d3d11_device)))
    {
        hr = ID3D11Device_OpenSharedResource(d3d11_device, handle, &IID_IDXGISurface, (void **)&dxgi_surface);
        ID3D11Device_Release(d3d11_device);
    }
    else
    {
        /* A rendering device that is not an ID3D11Device (an IDXGIDevice or ID2D1Device
         * from a client that never passed a D3D11 device) has no shared-resource
         * opening entry point at all; report that instead of succeeding with a NULL
         * surface. */
        hr = E_NOINTERFACE;
    }
    IUnknown_Release(rendering_device);

    if (FAILED(hr) || !dxgi_surface)
    {
        TRACE("DCOMP: Failed to open shared resource handle %p, hr %#lx\n", handle, hr);
        return hr;
    }

    hr = create_surface_from_dxgi_surface(device, dxgi_surface, surface);
    IDXGISurface_Release(dxgi_surface);
    return hr;
}

BOOL CALLBACK dcomp_enforce_own_captured_cb(HWND hwnd, LPARAM lparam)
{
    DWORD wpid = 0;

    if (!GetPropW(hwnd, L"__wine_dcomp_captured_hwnd"))
        return TRUE;
    GetWindowThreadProcessId(hwnd, &wpid);
    if (wpid == (DWORD)lparam)
    {
        g_dcomp_captured_top_hwnd = hwnd;
        dcomp_enforce_captured_toolwindow(hwnd);
    }
    return TRUE;
}

void dcomp_enforce_captured_toolwindow(HWND top)
{
    LONG_PTR ex;
    DWORD wpid = 0;

    if (!top || !IsWindow(top) || !IsWindowVisible(top))
        return;
    GetWindowThreadProcessId(top, &wpid);
    if (wpid != GetCurrentProcessId())
        return;
    ex = GetWindowLongPtrW(top, GWL_EXSTYLE);
    if ((ex & WS_EX_APPWINDOW) || !(ex & WS_EX_TOOLWINDOW) || !(ex & WS_EX_NOACTIVATE))
    {
        SetWindowLongPtrW(top, GWL_EXSTYLE,
                (ex & ~WS_EX_APPWINDOW) | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE);
        SetWindowPos(top, 0, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_ASYNCWINDOWPOS);
    }
}

void dcomp_hide_captured_hwnd(HWND hwnd)
{
    HWND top;

    SetPropW(hwnd, L"__wine_dcomp_captured_hwnd", (HANDLE)1);
    top = GetAncestor(hwnd, GA_ROOT);
    if (!top) top = hwnd;
    if (top != hwnd)
        SetPropW(top, L"__wine_dcomp_captured_hwnd", (HANDLE)1);
    g_dcomp_captured_top_hwnd = top;
    dcomp_enforce_captured_toolwindow(top);
}

HRESULT WINAPI DCompositionCreateSharedVisualHandle(HANDLE *handle)
{
    struct composition_visual *visual = NULL;
    NTSTATUS status;

    if (!handle)
        return STATUS_INVALID_PARAMETER;

    SERVER_START_REQ( dcomp_create_shared_visual )
    {
        if (!(status = wine_server_call( req )))
            *handle = wine_server_ptr_handle( reply->handle );
    }
    SERVER_END_REQ;

    if (!status && handle && *handle)
    {
        if (SUCCEEDED(create_visual(2, &IID_IDCompositionVisual, (void **)&visual)))
        {
            SERVER_START_REQ( dcomp_set_shared_visual_info )
            {
                req->handle = wine_server_obj_handle( *handle );
                req->target_root = wine_server_client_ptr( visual );
                wine_server_call( req );
            }
            SERVER_END_REQ;
            TRACE("DCOMP: DCompositionCreateSharedVisualHandle created and bound target_root %p to handle %p\n", visual, *handle);
        }
    }

    return (HRESULT)status;
}

struct composition_batch_query
{
    IUnknown IUnknown_iface;
    LONG ref;
};

static inline struct composition_batch_query *impl_from_batch_query(IUnknown *iface)
{
    return CONTAINING_RECORD(iface, struct composition_batch_query, IUnknown_iface);
}

static HRESULT WINAPI batch_query_QueryInterface(IUnknown *iface, REFIID riid, void **out)
{
    if (IsEqualIID(riid, &IID_IUnknown))
    {
        IUnknown_AddRef(iface);
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI batch_query_AddRef(IUnknown *iface)
{
    struct composition_batch_query *impl = impl_from_batch_query(iface);
    return InterlockedIncrement(&impl->ref);
}

static ULONG WINAPI batch_query_Release(IUnknown *iface)
{
    struct composition_batch_query *impl = impl_from_batch_query(iface);
    ULONG ref = InterlockedDecrement(&impl->ref);
    if (!ref) free(impl);
    return ref;
}

static HRESULT WINAPI batch_query_slot3(IUnknown *iface, DWORD *batch_id)
{
    if (batch_id) *batch_id = (DWORD)InterlockedIncrement(&g_batch_id_counter);
    return S_OK;
}

static HRESULT WINAPI batch_query_GetBatchId(IUnknown *iface, DWORD *batch_id)
{
    if (batch_id) *batch_id = (DWORD)InterlockedIncrement(&g_batch_id_counter);
    return S_OK;
}

struct batch_query_vtbl
{
    HRESULT (WINAPI *QueryInterface)( IUnknown *iface, REFIID riid, void **out );
    ULONG (WINAPI *AddRef)( IUnknown *iface );
    ULONG (WINAPI *Release)( IUnknown *iface );
    HRESULT (WINAPI *slot3)( IUnknown *iface, DWORD *batch_id );
    HRESULT (WINAPI *GetBatchId)( IUnknown *iface, DWORD *batch_id );
    HRESULT (WINAPI *slot5)( IUnknown *iface, DWORD *batch_id );
    HRESULT (WINAPI *slot6)( IUnknown *iface, DWORD *batch_id );
    HRESULT (WINAPI *slot7)( IUnknown *iface, DWORD *batch_id );
};

static const struct batch_query_vtbl batch_query_vtbl =
{
    batch_query_QueryInterface,
    batch_query_AddRef,
    batch_query_Release,
    batch_query_slot3,
    batch_query_GetBatchId,
    batch_query_slot3,
    batch_query_slot3,
    batch_query_slot3,
};

HRESULT WINAPI DCompPrivates_CreateCompositionBatchQuery(IDCompositionDevice *device, void **out_query)
{
    struct composition_batch_query *impl;

    TRACE("device %p, out_query %p.\n", device, out_query);

    if (!out_query) return E_POINTER;
    if (!(impl = calloc(1, sizeof(*impl)))) return E_OUTOFMEMORY;
    impl->IUnknown_iface.lpVtbl = (const IUnknownVtbl *)&batch_query_vtbl;
    impl->ref = 1;
    *out_query = &impl->IUnknown_iface;
    return S_OK;
}

HRESULT WINAPI DCompPrivates_CreateSharedVisualHandle(IDCompositionDevice *device, HANDLE *out_handle)
{
    TRACE("device %p, out_handle %p.\n", device, out_handle);

    if (!out_handle) return E_POINTER;
    return DCompositionCreateSharedVisualHandle(out_handle);
}

HRESULT WINAPI DCompPrivates_OpenAndAttachSharedTarget(IDCompositionDevice *device, HWND hwnd, void *target_in, void **target_out)
{
    IDCompositionTarget *target = NULL;
    HRESULT hr;

    TRACE("device %p, hwnd %p, target_in %p, target_out %p.\n", device, hwnd, target_in, target_out);

    if (!hwnd) hwnd = find_root_top_level_window();

    hr = IDCompositionDevice_CreateTargetForHwnd(device, hwnd, TRUE, &target);
    if (SUCCEEDED(hr) && target)
    {
        struct composition_target *target_impl = impl_from_IDCompositionTarget(target);
        if (target_in)
        {
            target_impl->root = (IDCompositionVisual *)target_in;
            IDCompositionVisual_AddRef(target_impl->root);
        }
        if (target_out) *target_out = target;
        TRACE("Attached target %p to hwnd %p.\n", target, hwnd);
    }
    return hr;
}

HRESULT WINAPI DCompPrivates_OpenSharedDCompositionObject(IUnknown *device_unk, HANDLE handle, REFIID riid, void **out)
{
    struct composition_device *device_impl = NULL;
    IDCompositionDevice *device = NULL;

    TRACE("DCOMP: DCompPrivates_OpenSharedDCompositionObject device %p, handle %p, riid %s, out %p\n",
        device_unk, handle, debugstr_guid(riid), out);

    if (!device_unk || !out)
        return E_POINTER;

    if (SUCCEEDED(IUnknown_QueryInterface(device_unk, &IID_IDCompositionDevice, (void **)&device)))
    {
        device_impl = impl_from_IDCompositionDevice(device);
        IDCompositionDevice_Release(device);
    }
    else
    {
        device_impl = (struct composition_device *)device_unk;
    }

    if (IsEqualGUID(riid, &IID_IDCompositionTarget))
    {
        return create_target_from_shared_visual_handle(device_impl, handle, out);
    }
    else if (IsEqualGUID(riid, &IID_IDCompositionVisual))
    {
        return create_visual_from_shared_visual_handle(handle, out);
    }

    FIXME("Unsupported riid %s\n", debugstr_guid(riid));
    return E_NOINTERFACE;
}

HRESULT WINAPI DCompPrivates_SetCommitCompletionEvent(IDCompositionDevice *device, HANDLE event)
{
    TRACE("device %p, event %p.\n", device, event);
    g_commit_completion_event = event;
    if (event) SetEvent(event);
    return S_OK;
}

/*
 * Composition islands entry points.
 *
 * The Windows App SDK composition-islands layer - Microsoft.UI.Input.dll, which
 * hosts the Microsoft.UI.Content.ContentIsland, ContentSite, ContentIsland-
 * Environment and DesktopChildSiteBridge classes, driven from
 * Microsoft.UI.Windowing.Core.dll - reaches the compositor through these
 * private dcomp exports: Microsoft.UI.Input.dll imports the DCompPrivates_* set
 * by name from Microsoft.Internal.FrameworkUdk.dll, which forwards it to
 * dcomp.dll.  The site bridge that AppWindowTitleBar stores at obj+0x250 is
 * assembled from the shared visual and the shared target these produce, so
 * answering S_OK with an uninitialised out-parameter leaves the application
 * holding a null bridge, and its title-bar layout pass then dereferences it
 * into a FailFast.
 */

HRESULT WINAPI DCompPrivates_CreateSharedVisual(IDCompositionDevice *device, IDCompositionVisual **visual)
{
    struct composition_device *device_impl;

    TRACE("device %p, visual %p.\n", device, visual);

    if (!visual)
        return E_POINTER;
    *visual = NULL;
    if (!device)
        return E_INVALIDARG;

    device_impl = impl_from_IDCompositionDevice(device);
    return dcomp_desktop_device_create_shared_resource(&device_impl->IDCompositionDeviceUnknown_iface,
            &IID_IDCompositionVisual, (void **)visual);
}

HRESULT WINAPI DCompPrivates_OpenSharedResourceHandle(IDCompositionDevice *device, IUnknown *unknown, HANDLE *handle)
{
    struct composition_device *device_impl;

    TRACE("device %p, unknown %p, handle %p.\n", device, unknown, handle);

    if (!handle)
        return E_POINTER;
    *handle = NULL;
    if (!device)
        return E_INVALIDARG;

    device_impl = impl_from_IDCompositionDevice(device);
    return dcomp_desktop_device_open_shared_resource_handle(&device_impl->IDCompositionDeviceUnknown_iface, unknown, handle);
}

/*
 * The size arrives as a SIZE by value in a single 8-byte stack slot on both
 * architectures, so the specification declares it as a 64-bit scalar and the
 * components are split here.
 */
static HRESULT set_visual_size(IDCompositionVisual *visual, LONGLONG packed)
{
    struct composition_visual *visual_impl;

    if (!visual)
        return E_INVALIDARG;

    visual_impl = impl_from_IDCompositionVisual(visual);
    visual_impl->size.cx = (LONG)(ULONGLONG)packed;
    visual_impl->size.cy = (LONG)((ULONGLONG)packed >> 32);
    return S_OK;
}

HRESULT WINAPI DCompPrivates_SetVisualRelativeSize(IDCompositionVisual *visual, LONGLONG size)
{
    TRACE("visual %p, size %ldx%ld.\n", visual, (LONG)(ULONGLONG)size, (LONG)((ULONGLONG)size >> 32));
    return set_visual_size(visual, size);
}

