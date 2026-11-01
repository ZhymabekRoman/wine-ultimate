/*
 * The Microsoft.UI.Composition.InteropCompositor object.
 *
 * Copyright 2026 Zhymabek Roman
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

#include "dcompi_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(dcompi);

/*
 * IDCompositionDesktopDevicePartner {d14b6158-c3fa-4bce-9c1f-b61d8665eab0}.  This is the
 * riid Microsoft.ui.xaml.dll asks CreateInteropCompositor for (0x180235843) and the object
 * it stores as DCompTreeHost::m_spMainDevice.  The fork implements the partner slots over
 * the DirectComposition device: dlls/dcomp/device.c slots 27/28/29
 * (desktop_device_CreateSharedResource, desktop_device_OpenSharedResourceHandle,
 * desktop_device_CreateFromSharedVisualHandle), and the partner pointer is the same object
 * as IDCompositionDevice2 (pinned by dlls/dcomp/tests/dcomp_chromium.c:433-487).
 */
static const GUID IID_IDCompositionDesktopDevicePartner =
    { 0xd14b6158, 0xc3fa, 0x4bce, { 0x9c, 0x1f, 0xb6, 0x1d, 0x86, 0x65, 0xea, 0xb0 } };

static const WCHAR compositor_class_w[] = L"Windows.UI.Composition.Compositor";

static inline struct interop_compositor *impl_from_IInspectable( struct IInspectable *iface )
{
    return CONTAINING_RECORD( iface, struct interop_compositor, IInspectable_iface );
}

static const struct IInspectableVtbl interop_compositor_vtbl;

/*
 * NotifyDirty (Microsoft.ui.xaml.dll 0x18022e750) arms DCompTreeHost::RequestMainDCompDeviceCommit:
 * on Windows the server-side frame pump of the Microsoft.UI nesting channel fires it.  This
 * module composites in-process through DirectComposition, whose commit is
 * IDCompositionDevice2_Commit on the device, and the fork's dcomp exposes no notification
 * hook on that path, so there is nothing to fire from.  The callback is stored and owned as
 * dcompi does, the fire site is left here so the wiring has one obvious home; it is not
 * called, honestly.
 */
static void interop_compositor_notify_dirty( struct interop_compositor *impl )
{
    if (!impl->callback) return;
    TRACE( "impl %p, callback %p: NotifyDirty is not wired, no in-process frame pump.\n",
            impl, impl->callback );
}

static HRESULT STDMETHODCALLTYPE interop_QueryInterface( struct IInspectable *iface, REFIID iid, void **out )
{
    struct interop_compositor *impl = impl_from_IInspectable( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (!out) return E_POINTER;
    *out = NULL;

    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable )
            || IsEqualGUID( iid, &IID_IAgileObject ))
    {
        *out = &impl->IInspectable_iface;
        IInspectable_AddRef( iface );
        return S_OK;
    }

    /* The composition device family: delegate to the fork dcomp device, which owns the
     * visual tree, the targets and the shared-resource plumbing to the WebView2/EB side. */
    if (impl->device2
            && (IsEqualGUID( iid, &IID_IDCompositionDesktopDevicePartner )
                || IsEqualGUID( iid, &IID_IDCompositionDevice2 )
                || IsEqualGUID( iid, &IID_IDCompositionDevice3 )
                || IsEqualGUID( iid, &IID_IDCompositionDesktopDevice )
                || IsEqualGUID( iid, &IID_IDCompositionDevice )))
    {
        return IDCompositionDevice2_QueryInterface( impl->device2, iid, out );
    }

    /* The Compositor family (ICompositor, ICompositorInterop, ...): the fork's
     * windows.ui.composition object, which is built over the same dcomp device. */
    if (impl->compositor)
    {
        HRESULT hr = IUnknown_QueryInterface( impl->compositor, iid, out );
        if (SUCCEEDED( hr )) return hr;
        *out = NULL;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE interop_AddRef( struct IInspectable *iface )
{
    return InterlockedIncrement( &impl_from_IInspectable( iface )->ref );
}

static ULONG STDMETHODCALLTYPE interop_Release( struct IInspectable *iface )
{
    struct interop_compositor *impl = impl_from_IInspectable( iface );
    LONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        interop_compositor_notify_dirty( impl );
        if (impl->callback) IInteropCompositorPartnerCallback_Release( impl->callback );
        if (impl->compositor) IUnknown_Release( impl->compositor );
        if (impl->partner) IUnknown_Release( impl->partner );
        if (impl->device2) IDCompositionDevice2_Release( impl->device2 );
        free( impl );
    }
    return ref;
}

static HRESULT STDMETHODCALLTYPE interop_GetIids( struct IInspectable *iface, ULONG *count, IID **iids )
{
    FIXME( "iface %p, count %p, iids %p stub!\n", iface, count, iids );
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE interop_GetRuntimeClassName( struct IInspectable *iface, HSTRING *name )
{
    return WindowsCreateString( dcompi_compositor_class, lstrlenW( dcompi_compositor_class ), name );
}

static HRESULT STDMETHODCALLTYPE interop_GetTrustLevel( struct IInspectable *iface, TrustLevel *level )
{
    FIXME( "iface %p, level %p stub!\n", iface, level );
    return E_NOTIMPL;
}

static const struct IInspectableVtbl interop_compositor_vtbl =
{
    interop_QueryInterface,
    interop_AddRef,
    interop_Release,
    interop_GetIids,
    interop_GetRuntimeClassName,
    interop_GetTrustLevel,
};

/*
 * Mirror of dcompi's InteropCompositor::RuntimeClassInitialize (0x1800f0800), with the two
 * dependencies the real one needs and Wine does not have left out: CoreHelper::
 * GetCurrentDispatcherQueue (0x1800cdcec) and GetCurrentCoreWindow (0x1800cdba0), both of
 * which have to succeed there before Compositor::RuntimeClassInitialize runs.  The device
 * argument is fed to the DirectComposition device creation the same way the fork's
 * windows.ui.composition compositor_init_render_device() does it; both observed callers
 * pass NULL (Microsoft.UI.Windowing.Core.dll 0x180032609, Microsoft.ui.xaml.dll 0x180235843),
 * which leaves the device to be created here.
 */
HRESULT interop_compositor_create( IUnknown *device, IInteropCompositorPartnerCallback *callback,
        struct interop_compositor **out )
{
    struct interop_compositor *impl;
    HRESULT hr;

    TRACE( "device %p, callback %p, out %p.\n", device, callback, out );

    if (!out) return E_POINTER;
    *out = NULL;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->IInspectable_iface.lpVtbl = &interop_compositor_vtbl;
    impl->ref = 1;

    if (device)
    {
        hr = IUnknown_QueryInterface( device, &IID_IDCompositionDevice2, (void **)&impl->device2 );
        if (FAILED( hr ))
        {
            TRACE( "device %p has no IDCompositionDevice2, falling back to our own device, hr %#lx.\n",
                    device, hr );
        }
    }
    if (!impl->device2)
    {
        hr = DCompositionCreateDevice2( NULL, &IID_IDCompositionDevice2, (void **)&impl->device2 );
        if (FAILED( hr ) || !impl->device2)
        {
            ERR( "DCompositionCreateDevice2 failed, hr %#lx.\n", hr );
            free( impl );
            return FAILED( hr ) ? hr : E_FAIL;
        }
    }

    /* The partner view of the device: slots 27/28/29 of dlls/dcomp, delegated.  Pinning
     * that the device answers {d14b6158} here turns a missing partner implementation into
     * an obvious E_NOINTERFACE at the channel entry instead of a late failure inside XAML. */
    hr = IDCompositionDevice2_QueryInterface( impl->device2, &IID_IDCompositionDesktopDevicePartner,
            (void **)&impl->partner );
    if (FAILED( hr ) || !impl->partner)
    {
        ERR( "the dcomp device has no IDCompositionDesktopDevicePartner, hr %#lx.\n", hr );
        IDCompositionDevice2_Release( impl->device2 );
        free( impl );
        return FAILED( hr ) ? hr : E_NOINTERFACE;
    }

    /* The Composition object behind the same device, for the ICompositor family.  It is
     * activated through combase's route for Windows.UI.Composition.Compositor
     * (dlls/combase/roapi.c:126-130); if that module is not present the composition object
     * simply answers E_NOINTERFACE for that family, and the device family stays live. */
    {
        HSTRING str = NULL;
        if (SUCCEEDED( WindowsCreateString( compositor_class_w, ARRAYSIZE( compositor_class_w ) - 1, &str ) ))
        {
            hr = RoGetActivationFactory( str, &IID_IUnknown, (void **)&impl->compositor );
            WindowsDeleteString( str );
        }
        else hr = E_OUTOFMEMORY;
    }
    if (FAILED( hr ))
    {
        TRACE( "no Windows.UI.Composition.Compositor for the Compositor family, hr %#lx.\n", hr );
        impl->compositor = NULL;
    }

    if (callback)
    {
        impl->callback = callback;
        IInteropCompositorPartnerCallback_AddRef( callback );
    }

    *out = impl;
    return S_OK;
}
