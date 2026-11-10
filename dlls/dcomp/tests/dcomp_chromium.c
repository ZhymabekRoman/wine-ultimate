/*
 * Unit test for the DirectComposition device sequence Chromium's GPU process runs
 * through ui/gl/direct_composition_support.cc InitializeDirectComposition():
 * device creation, the version queries, a window target with a visual root, a
 * B8G8R8A8 surface factory, BeginDraw/EndDraw and Commit, plus the private
 * commit-completion event interface behind WaitForCommitCompletion().
 *
 * Copyright 2026
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

#include <stdarg.h>

#include "ntstatus.h"
#define WIN32_NO_STATUS
#define COBJMACROS
#include <winternl.h>
/* No initguid.h here: this source is linked into the same test executable as
 * dcomp.c, which defines the GUIDs; defining them twice breaks the link. */
#include <d2d1_1.h>
#include <d2d1_3.h>
#include <d3d10_1.h>
#include <d3d11.h>
#include "dcomp.h"
#include "wine/test.h"

static HRESULT (WINAPI *pDCompositionCreateDevice)(IDXGIDevice *dxgi_device, REFIID iid, void **device);
static HRESULT (WINAPI *pDCompositionCreateDevice2)(IUnknown *rendering_device, REFIID iid, void **device);
static HRESULT (WINAPI *pDCompositionCreateDevice3)(IUnknown *rendering_device, REFIID iid, void **device);

/* Chromium's WaitForCommitCompletion() uses the private dcomp interface that carries
 * SetCommitCompletionEvent() at vtable slot 5 (0x28).  Only QI, AddRef, Release and
 * that slot are reached here, so the interface is declared minimally.  Defined
 * statically because the GUIDs of the public headers come from dcomp.c. */
static const GUID IID_dcomp_commit_event =
{ 0x6b556968, 0x2d25, 0x11e2, { 0x99, 0xf6, 0x8b, 0xc3, 0x60, 0x88, 0x70, 0x9b } };

/* Reported, not asserted: this is not an interface this dcomp build answers. */
static const GUID guid_c37ea93a_e7aa_450b =
{ 0xc37ea93a, 0xe7aa, 0x450b, { 0xb1, 0x6f, 0x97, 0x46, 0xcb, 0x04, 0x07, 0xf3 } };

/*
 * Private IDCompositionDesktopDevicePartner, the interface WebView2's
 * ClientWindow::ConnectToBrowserWithVisual() (EmbeddedBrowserWebView.dll
 * :0x1800492e8) queries on the composition device, and on which it calls
 * CreateSharedResource() at vftable offset 216 (slot 27, :0x180049343),
 * OpenSharedResourceHandle() at offset 224 (slot 28, :0x18004937b), while the
 * receiving side of the pair calls CreateFromSharedVisualHandle() at offset 232
 * (slot 29).  The layout is IDCompositionDesktopDevice (that is, the
 * IDCompositionDevice2 layout with the three desktop slots) plus these three.
 * Defined statically; the public GUIDs are defined by dcomp.c in this executable.
 */
static const GUID IID_dcomp_desktop_device_partner =
{ 0xd14b6158, 0xc3fa, 0x4bce, { 0x9c, 0x1f, 0xb6, 0x1d, 0x86, 0x65, 0xea, 0xb0 } };

static IDXGIDevice *create_device(unsigned int flags)
{
    IDXGIDevice *dxgi_device;
    ID3D10Device1 *device;
    HRESULT hr;

    hr = D3D10CreateDevice1(NULL, D3D10_DRIVER_TYPE_HARDWARE, NULL, flags, D3D10_FEATURE_LEVEL_10_0,
            D3D10_1_SDK_VERSION, &device);
    if (SUCCEEDED(hr))
        goto success;
    if (SUCCEEDED(D3D10CreateDevice1(NULL, D3D10_DRIVER_TYPE_WARP, NULL, flags,
            D3D10_FEATURE_LEVEL_10_0, D3D10_1_SDK_VERSION, &device)))
        goto success;
    if (SUCCEEDED(D3D10CreateDevice1(NULL, D3D10_DRIVER_TYPE_REFERENCE, NULL, flags,
            D3D10_FEATURE_LEVEL_10_0, D3D10_1_SDK_VERSION, &device)))
        goto success;

    return NULL;

success:
    hr = ID3D10Device1_QueryInterface(device, &IID_IDXGIDevice, (void **)&dxgi_device);
    ok(SUCCEEDED(hr), "Created device does not implement IDXGIDevice.\n");
    ID3D10Device1_Release(device);
    return dxgi_device;
}

static HWND create_window(void)
{
    RECT r = {0, 0, 640, 480};

    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW | WS_VISIBLE, FALSE);
    return CreateWindowW(L"static", L"dcomp_chromium_test", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0,
            r.right - r.left, r.bottom - r.top, NULL, NULL, NULL, NULL);
}

/*
 * The whole InitializeDirectComposition() call sequence, on a device created through
 * the v1 entry point, which is what Chromium does.
 */
static void test_chromium_initialize_sequence(IDXGIDevice *dxgi_device)
{
    IDCompositionSurfaceFactory *surface_factory;
    IDCompositionDevice3 *device3;
    IDCompositionDevice2 *device2;
    IDCompositionDevice *device;
    IDCompositionTarget *target;
    IDCompositionVisual *visual;
    IDCompositionSurface *surface;
    IUnknown *draw_object, *unknown;
    POINT offset;
    HRESULT hr;
    HWND hwnd;

    hr = pDCompositionCreateDevice(dxgi_device, &IID_IDCompositionDevice, (void **)&device);
    ok(hr == S_OK, "DCompositionCreateDevice hr %#lx, expected S_OK.\n", hr);
    if (FAILED(hr))
        return;

    /* Chromium: device.As(&compositordevice) asks for IDCompositionDevice3 on a v1
     * device; every device version has to be answered for the GPU process to start. */
    hr = IDCompositionDevice_QueryInterface(device, &IID_IDCompositionDevice2, (void **)&device2);
    ok(hr == S_OK, "IDCompositionDevice2 hr %#lx, expected S_OK.\n", hr);
    if (FAILED(hr))
    {
        IDCompositionDevice_Release(device);
        return;
    }
    hr = IDCompositionDevice_QueryInterface(device, &IID_IDCompositionDevice3, (void **)&device3);
    ok(hr == S_OK, "IDCompositionDevice3 hr %#lx, expected S_OK.\n", hr);
    if (FAILED(hr))
    {
        IDCompositionDevice2_Release(device2);
        IDCompositionDevice_Release(device);
        return;
    }
    hr = IDCompositionDevice_QueryInterface(device, &IID_IDCompositionDesktopDevice, (void **)&unknown);
    ok(hr == S_OK, "IDCompositionDesktopDevice hr %#lx, expected S_OK.\n", hr);
    IUnknown_Release(unknown);
    hr = IDCompositionDevice_QueryInterface(device, &guid_c37ea93a_e7aa_450b, (void **)&unknown);
    trace("QI for {c37ea93a-e7aa-450b-b16f-9746cb0407f3} on the v1 device: hr %#lx.\n", hr);
    if (SUCCEEDED(hr))
        IUnknown_Release(unknown);

    /* CreateDeviceTarget(hwnd) */
    hwnd = create_window();
    ok(hwnd != NULL, "Failed to create window.\n");
    hr = IDCompositionDevice_CreateTargetForHwnd(device, hwnd, TRUE, &target);
    ok(hr == S_OK, "CreateTargetForHwnd hr %#lx, expected S_OK.\n", hr);
    ok(target != NULL, "CreateTargetForHwnd returned no target.\n");
    if (FAILED(hr) || !target)
    {
        IDCompositionDevice2_Release(device2);
        IDCompositionDevice3_Release(device3);
        IDCompositionDevice_Release(device);
        DestroyWindow(hwnd);
        return;
    }

    hr = IDCompositionDevice_CreateVisual(device, &visual);
    ok(hr == S_OK, "CreateVisual hr %#lx, expected S_OK.\n", hr);
    hr = IDCompositionTarget_SetRoot(target, visual);
    ok(hr == S_OK, "SetRoot hr %#lx, expected S_OK.\n", hr);

    /* CreateSurfaceFactory(DXGI_FORMAT_B8G8R8A8_UNORM): Chromium passes its rendering
     * device to the v2/v3 CreateSurfaceFactory, then creates the surface. */
    hr = IDCompositionDevice2_CreateSurfaceFactory(device2, (IUnknown *)dxgi_device, &surface_factory);
    ok(hr == S_OK, "CreateSurfaceFactory hr %#lx, expected S_OK.\n", hr);
    hr = IDCompositionSurfaceFactory_CreateSurface(surface_factory, 64, 64,
            DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_ALPHA_MODE_PREMULTIPLIED, &surface);
    ok(hr == S_OK, "CreateSurface hr %#lx, expected S_OK.\n", hr);

    /* Chromium's BeginDraw asks for ID2D1Surface; that interface does not exist in
     * this tree (no ID2D1Surface in include/), and a composition surface here is
     * backed by a D3D11 texture, so the draw-interface query is what Chromium fails
     * on.  What has to hold is that a failing draw interface does not leave the
     * surface claimed as the device's drawing surface: the draw below must succeed. */
    hr = IDCompositionSurface_BeginDraw(surface, NULL, &IID_IDXGISwapChain, (void **)&draw_object, &offset);
    ok(FAILED(hr), "BeginDraw(unsupported draw interface) hr %#lx, expected failure.\n", hr);
    hr = IDCompositionSurface_BeginDraw(surface, NULL, &IID_ID2D1DeviceContext, (void **)&draw_object, &offset);
    ok(hr == E_NOTIMPL, "BeginDraw(IID_ID2D1DeviceContext) hr %#lx, expected E_NOTIMPL.\n", hr);

    hr = IDCompositionSurface_BeginDraw(surface, NULL, &IID_ID3D11Texture2D, (void **)&draw_object, &offset);
    ok(hr == S_OK, "BeginDraw(IID_ID3D11Texture2D) hr %#lx, expected S_OK.\n", hr);
    ok(offset.x == 0 && offset.y == 0, "Got draw offset %ld, %ld.\n", offset.x, offset.y);
    IUnknown_Release(draw_object);
    hr = IDCompositionSurface_EndDraw(surface);
    ok(hr == S_OK, "EndDraw hr %#lx, expected S_OK.\n", hr);

    hr = IDCompositionVisual_SetContent(visual, (IUnknown *)surface);
    ok(hr == S_OK, "SetContent hr %#lx, expected S_OK.\n", hr);

    hr = IDCompositionDevice_Commit(device);
    ok(hr == S_OK, "Commit hr %#lx, expected S_OK.\n", hr);
    hr = IDCompositionDevice_WaitForCommitCompletion(device);
    ok(hr == S_OK, "WaitForCommitCompletion hr %#lx, expected S_OK.\n", hr);

    hr = IDCompositionVisual_SetContent(visual, NULL);
    ok(hr == S_OK, "SetContent(NULL) hr %#lx, expected S_OK.\n", hr);
    IDCompositionSurface_Release(surface);
    IDCompositionSurfaceFactory_Release(surface_factory);
    IDCompositionTarget_Release(target);
    IDCompositionVisual_Release(visual);
    IDCompositionDevice2_Release(device2);
    IDCompositionDevice3_Release(device3);
    IDCompositionDevice_Release(device);
    DestroyWindow(hwnd);
}

/*
 * The private commit-completion event interface: Chromium sets an event it waits on
 * for a commit, and WaitForCommitCompletion() has to complete.
 */
static void test_commit_completion_event(IDXGIDevice *dxgi_device)
{
    HRESULT (STDMETHODCALLTYPE *pSetCommitCompletionEvent)(void *iface, HANDLE event);
    IDCompositionDevice *device;
    IUnknown *commit_event;
    HANDLE event;
    HRESULT hr;

    hr = pDCompositionCreateDevice(dxgi_device, &IID_IDCompositionDevice, (void **)&device);
    ok(hr == S_OK, "DCompositionCreateDevice hr %#lx, expected S_OK.\n", hr);
    if (FAILED(hr))
        return;

    hr = IDCompositionDevice_QueryInterface(device, &IID_dcomp_commit_event, (void **)&commit_event);
    ok(hr == S_OK, "Commit completion interface hr %#lx, expected S_OK.\n", hr);
    if (FAILED(hr))
    {
        IDCompositionDevice_Release(device);
        return;
    }

    /* Vtable slot 5 (0x28) of the interface object, not offset 5 of the object. */
    pSetCommitCompletionEvent = (void *)((*(void ***)commit_event)[5]);
    event = CreateEventW(NULL, TRUE, FALSE, NULL);
    hr = pSetCommitCompletionEvent(commit_event, event);
    ok(hr == S_OK, "SetCommitCompletionEvent hr %#lx, expected S_OK.\n", hr);
    hr = IDCompositionDevice_Commit(device);
    ok(hr == S_OK, "Commit hr %#lx, expected S_OK.\n", hr);
    hr = IDCompositionDevice_WaitForCommitCompletion(device);
    ok(hr == S_OK, "WaitForCommitCompletion hr %#lx, expected S_OK.\n", hr);
    ok(WaitForSingleObject(event, 100) == WAIT_OBJECT_0, "Commit completion event not signalled.\n");

    CloseHandle(event);
    IUnknown_Release(commit_event);
    IDCompositionDevice_Release(device);
}

/*
 * IDCompositionDevice::CreateVirtualSurface(): a virtual surface answers
 * IDCompositionVirtualSurface, is resizable, accepts a sub-rect BeginDraw, and
 * trims.  The surface factory's CreateVirtualSurface() creates the same object.
 */
static void test_virtual_surface(IDXGIDevice *dxgi_device)
{
    IDCompositionVirtualSurface *virtual_surface, *factory_virtual_surface;
    IDCompositionSurfaceFactory *surface_factory;
    IDCompositionDevice2 *device2;
    IDCompositionDevice *device;
    IDCompositionSurface *surface;
    IUnknown *draw_object, *unknown;
    const RECT out_of_bounds = {0, 0, 128, 128};
    const RECT sub_rect = {0, 0, 16, 16};
    const RECT rect = {0, 0, 8, 8};
    POINT offset;
    HRESULT hr;

    hr = pDCompositionCreateDevice(dxgi_device, &IID_IDCompositionDevice, (void **)&device);
    ok(hr == S_OK, "DCompositionCreateDevice hr %#lx, expected S_OK.\n", hr);
    if (FAILED(hr))
        return;

    hr = IDCompositionDevice_CreateVirtualSurface(device, 64, 64, DXGI_FORMAT_B8G8R8A8_UNORM,
            DXGI_ALPHA_MODE_PREMULTIPLIED, &virtual_surface);
    ok(hr == S_OK, "CreateVirtualSurface hr %#lx, expected S_OK.\n", hr);
    if (FAILED(hr))
    {
        IDCompositionDevice_Release(device);
        return;
    }

    hr = IDCompositionVirtualSurface_QueryInterface(virtual_surface, &IID_IDCompositionVirtualSurface,
            (void **)&unknown);
    ok(hr == S_OK, "IDCompositionVirtualSurface hr %#lx, expected S_OK.\n", hr);
    IUnknown_Release(unknown);
    hr = IDCompositionVirtualSurface_QueryInterface(virtual_surface, &IID_IDCompositionSurface,
            (void **)&surface);
    ok(hr == S_OK, "IDCompositionSurface hr %#lx, expected S_OK.\n", hr);

    /* A virtual surface takes a sub-rect from the first draw on. */
    hr = IDCompositionSurface_BeginDraw(surface, &sub_rect, &IID_ID3D11Texture2D, (void **)&draw_object, &offset);
    ok(hr == S_OK, "BeginDraw(sub-rect) hr %#lx, expected S_OK.\n", hr);
    ok(offset.x == 0 && offset.y == 0, "Got draw offset %ld, %ld.\n", offset.x, offset.y);
    IUnknown_Release(draw_object);
    hr = IDCompositionSurface_EndDraw(surface);
    ok(hr == S_OK, "EndDraw hr %#lx, expected S_OK.\n", hr);

    hr = IDCompositionVirtualSurface_Resize(virtual_surface, 32, 32);
    ok(hr == S_OK, "Resize hr %#lx, expected S_OK.\n", hr);

    /* Out of bounds after the resize: 64x64 no longer fits. */
    hr = IDCompositionVirtualSurface_Trim(virtual_surface, &out_of_bounds, 1);
    ok(hr == E_INVALIDARG, "Trim(out of bounds) hr %#lx, expected E_INVALIDARG.\n", hr);
    hr = IDCompositionVirtualSurface_Trim(virtual_surface, &rect, 1);
    ok(hr == S_OK, "Trim hr %#lx, expected S_OK.\n", hr);
    hr = IDCompositionVirtualSurface_Trim(virtual_surface, NULL, 0);
    ok(hr == S_OK, "Trim(NULL, 0) hr %#lx, expected S_OK.\n", hr);

    IDCompositionSurface_Release(surface);

    /* A non-virtual surface of the same size still rejects a sub-rect first draw. */
    hr = IDCompositionDevice_CreateSurface(device, 64, 64, DXGI_FORMAT_B8G8R8A8_UNORM,
            DXGI_ALPHA_MODE_PREMULTIPLIED, &surface);
    ok(hr == S_OK, "CreateSurface hr %#lx, expected S_OK.\n", hr);
    hr = IDCompositionSurface_BeginDraw(surface, &sub_rect, &IID_ID3D11Texture2D, (void **)&draw_object, &offset);
    ok(hr == E_INVALIDARG, "Non-virtual BeginDraw(sub-rect) hr %#lx, expected E_INVALIDARG.\n", hr);
    IDCompositionSurface_Release(surface);

    IDCompositionVirtualSurface_Release(virtual_surface);

    /* The same object through IDCompositionSurfaceFactory::CreateVirtualSurface(). */
    hr = IDCompositionDevice_QueryInterface(device, &IID_IDCompositionDevice2, (void **)&device2);
    ok(hr == S_OK, "IDCompositionDevice2 hr %#lx, expected S_OK.\n", hr);
    hr = IDCompositionDevice2_CreateSurfaceFactory(device2, (IUnknown *)dxgi_device, &surface_factory);
    ok(hr == S_OK, "CreateSurfaceFactory hr %#lx, expected S_OK.\n", hr);
    hr = IDCompositionSurfaceFactory_CreateVirtualSurface(surface_factory, 64, 64,
            DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_ALPHA_MODE_PREMULTIPLIED, &factory_virtual_surface);
    ok(hr == S_OK, "factory CreateVirtualSurface hr %#lx, expected S_OK.\n", hr);
    if (SUCCEEDED(hr))
    {
        hr = IDCompositionVirtualSurface_Resize(factory_virtual_surface, 0, 0);
        ok(hr == E_INVALIDARG, "Resize(0, 0) hr %#lx, expected E_INVALIDARG.\n", hr);
        IDCompositionVirtualSurface_Release(factory_virtual_surface);
    }

    IDCompositionSurfaceFactory_Release(surface_factory);
    IDCompositionDevice2_Release(device2);
    IDCompositionDevice_Release(device);
}

/*
 * The v2 and v3 creation entry points, and the device state queries Chromium polls.
 */
static void test_device_entry_points_and_state(IDXGIDevice *dxgi_device)
{
    IDCompositionDesktopDevice *desktop;
    IDCompositionDevice3 *device3;
    IDCompositionDevice2 *device2;
    IDCompositionDevice *device;
    IUnknown *unknown;
    BOOL valid;
    HRESULT hr;

    hr = pDCompositionCreateDevice2((IUnknown *)dxgi_device, &IID_IDCompositionDevice2, (void **)&device2);
    ok(hr == S_OK, "DCompositionCreateDevice2(IDCompositionDevice2) hr %#lx, expected S_OK.\n", hr);
    if (SUCCEEDED(hr))
    {
        hr = IDCompositionDevice2_QueryInterface(device2, &IID_IDCompositionDevice, (void **)&device);
        ok(hr == S_OK, "IDCompositionDevice on a v2 device hr %#lx, expected S_OK.\n", hr);
        IDCompositionDevice_Release(device);
        hr = IDCompositionDevice2_QueryInterface(device2, &IID_IDCompositionDesktopDevice, (void **)&desktop);
        ok(hr == S_OK, "IDCompositionDesktopDevice on a v2 device hr %#lx, expected S_OK.\n", hr);
        IDCompositionDesktopDevice_Release(desktop);
        IDCompositionDevice2_Release(device2);
    }

    hr = pDCompositionCreateDevice3((IUnknown *)dxgi_device, &IID_IDCompositionDevice3, (void **)&device3);
    ok(hr == S_OK, "DCompositionCreateDevice3(IDCompositionDevice3) hr %#lx, expected S_OK.\n", hr);
    if (SUCCEEDED(hr))
    {
        hr = IDCompositionDevice3_QueryInterface(device3, &IID_IDCompositionDevice2, (void **)&device2);
        ok(hr == S_OK, "IDCompositionDevice2 on a v3 device hr %#lx, expected S_OK.\n", hr);
        IDCompositionDevice2_Release(device2);
        hr = IDCompositionDevice3_QueryInterface(device3, &IID_IDCompositionDevice, (void **)&device);
        ok(hr == S_OK, "IDCompositionDevice on a v3 device hr %#lx, expected S_OK.\n", hr);
        IDCompositionDevice_Release(device);
        hr = IDCompositionDevice3_QueryInterface(device3, &IID_IDCompositionDesktopDevice, (void **)&desktop);
        ok(hr == S_OK, "IDCompositionDesktopDevice on a v3 device hr %#lx, expected S_OK.\n", hr);
        IDCompositionDesktopDevice_Release(desktop);
        IDCompositionDevice3_Release(device3);
    }

    /* DCompositionCreateDevice() only accepts IID_IDCompositionDevice as its iid. */
    hr = pDCompositionCreateDevice(dxgi_device, &IID_IDCompositionDevice2, (void **)&device2);
    ok(hr == E_NOINTERFACE, "DCompositionCreateDevice(IDCompositionDevice2) hr %#lx, expected E_NOINTERFACE.\n", hr);

    hr = pDCompositionCreateDevice(dxgi_device, &IID_IDCompositionDevice, NULL);
    ok(hr == E_INVALIDARG, "DCompositionCreateDevice(NULL out) hr %#lx, expected E_INVALIDARG.\n", hr);

    hr = pDCompositionCreateDevice(dxgi_device, &IID_IDCompositionDevice, (void **)&device);
    ok(hr == S_OK, "DCompositionCreateDevice hr %#lx, expected S_OK.\n", hr);
    if (FAILED(hr))
        return;

    hr = IDCompositionDevice_CheckDeviceState(device, &valid);
    ok(hr == S_OK, "CheckDeviceState hr %#lx, expected S_OK.\n", hr);
    ok(valid, "CheckDeviceState reported an invalid device.\n");

    hr = IDCompositionDevice_GetFrameStatistics(device, NULL);
    ok(hr == E_POINTER, "GetFrameStatistics(NULL) hr %#lx, expected E_POINTER.\n", hr);

    /* No shared resource handle exists for this device; the call has to fail
     * cleanly, and not report success with a NULL surface. */
    hr = IDCompositionDevice_CreateSurfaceFromHandle(device, (HANDLE)0x1234, &unknown);
    ok(FAILED(hr), "CreateSurfaceFromHandle(bogus handle) hr %#lx, expected failure.\n", hr);
    hr = IDCompositionDevice_CreateSurfaceFromHandle(device, NULL, &unknown);
    ok(hr == E_INVALIDARG, "CreateSurfaceFromHandle(NULL) hr %#lx, expected E_INVALIDARG.\n", hr);

    IDCompositionDevice_Release(device);
}

/*
 * The WebView2 visual-bind sequence, exactly as ClientWindow::ConnectToBrowserWithVisual()
 * (EmbeddedBrowserWebView.dll :0x1800491fe) drives it: QI of {d14b6158-c3fa-4bce-
 * 9c1f-b61d8665eab0} on the device, then CreateSharedResource(IID_IDCompositionVisual)
 * through vftable offset 216, OpenSharedResourceHandle through offset 224, and the
 * handle consumed by CreateFromSharedResourceHandle through offset 232 on the
 * receiving device.  The app calls these by vftable offset, so the test calls them
 * by vftable offset too.  The handle leaves the app process through
 * HandlesInTransitionHelper::TransferHandle (mojo, DuplicateHandle based) and the
 * app's delegate closes its own copy afterwards: the handle must be a duplicable
 * server object handle, not a pointer.
 */
static void test_desktop_device_partner(IDXGIDevice *dxgi_device)
{
    HRESULT (STDMETHODCALLTYPE *pCreateSharedResource)(void *iface, REFIID riid, void **obj);
    HRESULT (STDMETHODCALLTYPE *pOpenSharedResourceHandle)(void *iface, IUnknown *unknown, HANDLE *handle);
    HRESULT (STDMETHODCALLTYPE *pCreateFromSharedVisualHandle)(void *iface, HANDLE handle, REFIID iid, void **out);
    HRESULT (STDMETHODCALLTYPE *pSlot16)(void *iface, IDCompositionTranslateTransform3D **transform);
    IDCompositionTranslateTransform3D *transform3d;
    IDCompositionDevice *device, *receiver;
    IDCompositionDevice2 *device2;
    IDCompositionVisual *visual;
    IUnknown *partner, *receiver_partner, *unknown;
    HANDLE handle, dup;
    IUnknown *out;
    HRESULT hr;

    hr = pDCompositionCreateDevice(dxgi_device, &IID_IDCompositionDevice, (void **)&device);
    ok(hr == S_OK, "DCompositionCreateDevice hr %#lx, expected S_OK.\n", hr);
    if (FAILED(hr))
        return;

    /* The app QIs the private partner interface (:0x1800492e8); the gate block
     * never runs when this fails. */
    partner = NULL;
    hr = IDCompositionDevice_QueryInterface(device, &IID_dcomp_desktop_device_partner, (void **)&partner);
    ok(hr == S_OK, "QI {d14b6158-c3fa-4bce-9c1f-b61d8665eab0} hr %#lx, expected S_OK.\n", hr);
    ok(partner != NULL, "QI {d14b6158} returned no interface.\n");
    if (FAILED(hr) || !partner)
    {
        IDCompositionDevice_Release(device);
        return;
    }

    /* Same object as IDCompositionDevice2, as on Windows: the QI'd device vftable
     * is the extended one, slots 3..26 are the IDCompositionDevice2/
     * IDCompositionDesktopDevice methods. */
    hr = IDCompositionDevice_QueryInterface(device, &IID_IDCompositionDevice2, (void **)&device2);
    ok(hr == S_OK, "IDCompositionDevice2 hr %#lx, expected S_OK.\n", hr);
    ok((IUnknown *)device2 == partner, "partner QI returns a different object than IDCompositionDevice2.\n");
    IDCompositionDevice2_Release(device2);

    pCreateSharedResource = (void *)((*(void ***)partner)[27]);
    pOpenSharedResourceHandle = (void *)((*(void ***)partner)[28]);
    pCreateFromSharedVisualHandle = (void *)((*(void ***)partner)[29]);

    /* Slot 16 (offset 128) of this interface is CreateTranslateTransform3D in the
     * IDCompositionDevice2 layout; WebView2's :0x180049296 gate predicate is the
     * delegate's Controller::HasClosed, not this slot, but the app-side block does
     * require a working transform factory on the queried object. */
    transform3d = NULL;
    pSlot16 = (void *)((*(void ***)partner)[16]);
    hr = pSlot16(partner, &transform3d);
    ok(hr == S_OK, "partner slot 16 (CreateTranslateTransform3D) hr %#lx, expected S_OK.\n", hr);
    ok(transform3d != NULL, "partner slot 16 returned no transform.\n");
    if (SUCCEEDED(hr) && transform3d)
        IDCompositionTranslateTransform3D_Release(transform3d);

    /* :0x180049343: CreateSharedResource(&IID_IDCompositionVisual, &resource). */
    visual = NULL;
    hr = pCreateSharedResource(partner, &IID_IDCompositionVisual, (void **)&visual);
    ok(hr == S_OK, "CreateSharedResource(IID_IDCompositionVisual) hr %#lx, expected S_OK.\n", hr);
    ok(visual != NULL, "CreateSharedResource returned no resource.\n");
    if (FAILED(hr) || !visual)
        goto done;

    /* The resource answers its own interface (offset 128 = AddVisual, used by
     * SetHostRootVisualTarget :0x180049936) and is a usable visual. */
    unknown = NULL;
    hr = IDCompositionVisual_QueryInterface(visual, &IID_IDCompositionVisual, (void **)&unknown);
    ok(hr == S_OK, "resource QI IDCompositionVisual hr %#lx, expected S_OK.\n", hr);
    if (SUCCEEDED(hr))
        IDCompositionVisual_Release((IDCompositionVisual *)unknown);
    hr = IDCompositionVisual_SetOffsetX(visual, 0.0f);
    ok(hr == S_OK, "SetOffsetX hr %#lx, expected S_OK.\n", hr);

    /* :0x18004937b: OpenSharedResourceHandle(resource, &handle). */
    handle = NULL;
    hr = pOpenSharedResourceHandle(partner, (IUnknown *)visual, &handle);
    ok(hr == S_OK, "OpenSharedResourceHandle hr %#lx, expected S_OK.\n", hr);
    ok(handle && handle != INVALID_HANDLE_VALUE, "OpenSharedResourceHandle returned handle %p.\n", handle);
    if (FAILED(hr) || !handle || handle == INVALID_HANDLE_VALUE)
        goto done;

    /* The handle crosses to the browser process through DuplicateHandle-based
     * mojo transfer and the app closes its copy: it must be a real object handle. */
    dup = NULL;
    ok(DuplicateHandle(GetCurrentProcess(), handle, GetCurrentProcess(), &dup, 0, TRUE, DUPLICATE_SAME_ACCESS),
            "shared resource handle %p is not duplicable.\n", handle);
    if (dup)
        CloseHandle(dup);

    /* Receiver side (the browser process in the app): a second device opens the
     * handle and gets a visual bound to the shared resource, in-process round-trip. */
    receiver = NULL;
    hr = pDCompositionCreateDevice(dxgi_device, &IID_IDCompositionDevice, (void **)&receiver);
    ok(hr == S_OK, "receiver DCompositionCreateDevice hr %#lx, expected S_OK.\n", hr);
    if (SUCCEEDED(hr))
    {
        receiver_partner = NULL;
        hr = IDCompositionDevice_QueryInterface(receiver, &IID_dcomp_desktop_device_partner, (void **)&receiver_partner);
        ok(hr == S_OK, "receiver QI {d14b6158} hr %#lx, expected S_OK.\n", hr);
        if (SUCCEEDED(hr))
        {
            out = NULL;
            hr = pCreateFromSharedVisualHandle(receiver_partner, handle, &IID_IDCompositionVisual, (void **)&out);
            ok(hr == S_OK, "CreateFromSharedVisualHandle hr %#lx, expected S_OK.\n", hr);
            ok(out != NULL, "CreateFromSharedVisualHandle returned no object.\n");
            if (SUCCEEDED(hr) && out)
            {
                unknown = NULL;
                hr = IUnknown_QueryInterface(out, &IID_IDCompositionVisual, (void **)&unknown);
                ok(hr == S_OK, "round-trip object QI IDCompositionVisual hr %#lx, expected S_OK.\n", hr);
                if (SUCCEEDED(hr))
                    IDCompositionVisual_Release((IDCompositionVisual *)unknown);
                IUnknown_Release(out);
            }
            IUnknown_Release(receiver_partner);
        }

        /* A bogus handle must fail cleanly. */
        out = NULL;
        hr = pCreateFromSharedVisualHandle(receiver_partner ? receiver_partner : partner,
                (HANDLE)(UINT_PTR)0x1234, &IID_IDCompositionVisual, (void **)&out);
        ok(FAILED(hr), "CreateFromSharedVisualHandle(bogus handle) hr %#lx, expected failure.\n", hr);
        if (SUCCEEDED(hr) && out)
            IUnknown_Release(out);

        IDCompositionDevice_Release(receiver);
    }

    /* Error paths: NULL arguments fail cleanly, with a NULL out. */
    hr = pOpenSharedResourceHandle(partner, NULL, &handle);
    ok(hr == E_INVALIDARG, "OpenSharedResourceHandle(NULL) hr %#lx, expected E_INVALIDARG.\n", hr);
    hr = pOpenSharedResourceHandle(partner, (IUnknown *)visual, NULL);
    ok(hr == E_POINTER, "OpenSharedResourceHandle(NULL out) hr %#lx, expected E_POINTER.\n", hr);
    out = (void *)0x1;
    hr = pCreateSharedResource(partner, &IID_IDCompositionVisual, NULL);
    ok(hr == E_POINTER, "CreateSharedResource(NULL out) hr %#lx, expected E_POINTER.\n", hr);

done:
    if (handle && handle != INVALID_HANDLE_VALUE)
        CloseHandle(handle);
    if (visual)
        IDCompositionVisual_Release(visual);
    IUnknown_Release(partner);
    IDCompositionDevice_Release(device);
}

START_TEST(dcomp_chromium)
{
    IDCompositionDevice *device;
    IDXGIDevice *dxgi_device;
    HMODULE module;
    HRESULT hr;

    module = LoadLibraryW(L"dcomp.dll");
    if (!module)
    {
        win_skip("dcomp.dll not found.\n");
        return;
    }

    pDCompositionCreateDevice = (void *)GetProcAddress(module, "DCompositionCreateDevice");
    pDCompositionCreateDevice2 = (void *)GetProcAddress(module, "DCompositionCreateDevice2");
    pDCompositionCreateDevice3 = (void *)GetProcAddress(module, "DCompositionCreateDevice3");
    if (!pDCompositionCreateDevice)
    {
        win_skip("DCompositionCreateDevice() is unavailable.\n");
        FreeLibrary(module);
        return;
    }

    if (!(dxgi_device = create_device(D3D10_CREATE_DEVICE_BGRA_SUPPORT)))
    {
        win_skip("Failed to create a rendering device.\n");
        FreeLibrary(module);
        return;
    }

    test_chromium_initialize_sequence(dxgi_device);
    test_commit_completion_event(dxgi_device);
    test_virtual_surface(dxgi_device);
    test_device_entry_points_and_state(dxgi_device);
    test_desktop_device_partner(dxgi_device);

    /* A device created without a rendering device must still complete the sequence:
     * the composition surface factory is built on a default D3D11 device. */
    hr = pDCompositionCreateDevice(NULL, &IID_IDCompositionDevice, (void **)&device);
    ok(hr == S_OK, "DCompositionCreateDevice(NULL) hr %#lx, expected S_OK.\n", hr);
    if (SUCCEEDED(hr))
    {
        IDCompositionSurface *surface;

        hr = IDCompositionDevice_CreateSurface(device, 64, 64, DXGI_FORMAT_B8G8R8A8_UNORM,
                DXGI_ALPHA_MODE_PREMULTIPLIED, &surface);
        ok(hr == S_OK, "CreateSurface on a device with no rendering device hr %#lx, expected S_OK.\n", hr);
        if (SUCCEEDED(hr))
            IDCompositionSurface_Release(surface);
        IDCompositionDevice_Release(device);
    }

    IDXGIDevice_Release(dxgi_device);
    FreeLibrary(module);
}
