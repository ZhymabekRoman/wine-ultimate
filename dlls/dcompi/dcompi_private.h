/*
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

#ifndef __COMPI_PRIVATE_H
#define __COMPI_PRIVATE_H

#include <stdarg.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>

#define COBJMACROS

#include "windef.h"
#include "winbase.h"
#include "winerror.h"
#include "winstring.h"

#include "objidl.h"
#include "activation.h"
#include "inspectable.h"
#include "roapi.h"
#include "dcomp.h"

#include "wine/debug.h"

/*
 * The Microsoft.UI.Composition factory interfaces of the Microsoft.WindowsAppRuntime
 * composition provider (dcompi.dll).  Nothing declares them publicly; the layouts, the
 * IIDs and the slot counts below are recovered from the app's own dcompi.dll:
 *
 *   CompositorFactory vftables
 *     0x180122cc8  IActivationFactory                   (7 slots)
 *     0x180122c40  IInteropCompositorFactoryPartner     (8 slots)
 *     0x180122c00  ICompositorStatics                   (8 slots)
 *     0x180122bc8  IInteropCompositorFactoryRestricted  (7 slots)
 *   IID -> offset table: CanCastTo 0x180040f78
 *     {00000035-0000-0000-c000-000000000046} IActivationFactory,
 *     {94ea2b94-e9cc-49e0-c0ff-ee64ca8f5b90} IAgileObject,
 *     {00000003-0000-0000-c000-000000000046} IMarshal  -> the base pointer,
 *     {22118adf-23f1-4801-bcfa-66cbf48cc51b} IInteropCompositorFactoryPartner,
 *     {6baa947a-f103-55c6-91ad-3d275bea65c4} ICompositorStatics,
 *     {b27006d1-d095-432c-9d79-038d269d7aa3} IInteropCompositorFactoryRestricted.
 */

/*
 * Microsoft.UI.Composition.IInteropCompositorPartnerCallback: the callback the caller hands
 * to CreateInteropCompositor.  XAML's implementation is DCompInteropCompositorPartnerCallback
 * (Microsoft.ui.xaml.dll vftable 0x180a8e620): IUnknown + NotifyDirty (0x18022e750, which
 * arms DCompTreeHost::RequestMainDCompDeviceCommit).  Its IID is not observable in the
 * binary - the callback is only ever passed in, never queried for - so no IID is declared
 * here and the pointer is used as this concrete type, as dcompi does.  Declared first
 * because the factory interfaces below take it as a parameter.
 */
typedef struct IInteropCompositorPartnerCallback IInteropCompositorPartnerCallback;
typedef struct IInteropCompositorPartnerCallbackVtbl
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)( IInteropCompositorPartnerCallback *iface, REFIID riid, void **obj );
    ULONG (STDMETHODCALLTYPE *AddRef)( IInteropCompositorPartnerCallback *iface );
    ULONG (STDMETHODCALLTYPE *Release)( IInteropCompositorPartnerCallback *iface );
    HRESULT (STDMETHODCALLTYPE *NotifyDirty)( IInteropCompositorPartnerCallback *iface );
} IInteropCompositorPartnerCallbackVtbl;
struct IInteropCompositorPartnerCallback
{
    const struct IInteropCompositorPartnerCallbackVtbl *lpVtbl;
};

#define IInteropCompositorPartnerCallback_QueryInterface(i,a,b) (i)->lpVtbl->QueryInterface(i,a,b)
#define IInteropCompositorPartnerCallback_AddRef(i) (i)->lpVtbl->AddRef(i)
#define IInteropCompositorPartnerCallback_Release(i) (i)->lpVtbl->Release(i)
#define IInteropCompositorPartnerCallback_NotifyDirty(i) (i)->lpVtbl->NotifyDirty(i)

/*
 * Microsoft.UI.Composition.IInteropCompositorFactoryPartner
 * {22118adf-23f1-4801-bcfa-66cbf48cc51b}
 *
 * The interface Microsoft.ui.xaml.dll asks the activation factory for
 * (ActivationFactoryCache::GetInteropCompositorFactoryPartner, 0x18023586c) and the one
 * whose CreateInteropCompositor opens the composition channel
 * (DCompTreeHost::CreateWinRTInteropCompositionDevice, 0x1802357b8).
 */
typedef struct IInteropCompositorFactoryPartner IInteropCompositorFactoryPartner;
typedef struct IInteropCompositorFactoryPartnerVtbl
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)( IInteropCompositorFactoryPartner *iface, REFIID riid, void **obj );
    ULONG (STDMETHODCALLTYPE *AddRef)( IInteropCompositorFactoryPartner *iface );
    ULONG (STDMETHODCALLTYPE *Release)( IInteropCompositorFactoryPartner *iface );
    HRESULT (STDMETHODCALLTYPE *GetIids)( IInteropCompositorFactoryPartner *iface, ULONG *count, IID **iids );
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)( IInteropCompositorFactoryPartner *iface, HSTRING *name );
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)( IInteropCompositorFactoryPartner *iface, TrustLevel *level );
    HRESULT (STDMETHODCALLTYPE *CreateInteropCompositor)( IInteropCompositorFactoryPartner *iface,
            IUnknown *device, struct IInteropCompositorPartnerCallback *callback, REFIID riid, void **result );
    HRESULT (STDMETHODCALLTYPE *CheckEnabled)( IInteropCompositorFactoryPartner *iface, bool *enabled, bool *available );
} IInteropCompositorFactoryPartnerVtbl;
struct IInteropCompositorFactoryPartner
{
    const struct IInteropCompositorFactoryPartnerVtbl *lpVtbl;
};

/*
 * Microsoft.UI.Composition.ICompositorStatics {6baa947a-f103-55c6-91ad-3d275bea65c4}
 * get_MaxGlobalPlaybackRate is dcompi 0x18004cd40 (*rate = 16.0f, S_OK),
 * put_MaxGlobalPlaybackRate is the folded stub at slot 7 (no validation).
 */
typedef struct ICompositorStatics ICompositorStatics;
typedef struct ICompositorStaticsVtbl
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)( ICompositorStatics *iface, REFIID riid, void **obj );
    ULONG (STDMETHODCALLTYPE *AddRef)( ICompositorStatics *iface );
    ULONG (STDMETHODCALLTYPE *Release)( ICompositorStatics *iface );
    HRESULT (STDMETHODCALLTYPE *GetIids)( ICompositorStatics *iface, ULONG *count, IID **iids );
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)( ICompositorStatics *iface, HSTRING *name );
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)( ICompositorStatics *iface, TrustLevel *level );
    HRESULT (STDMETHODCALLTYPE *get_MaxGlobalPlaybackRate)( ICompositorStatics *iface, float *rate );
    HRESULT (STDMETHODCALLTYPE *put_MaxGlobalPlaybackRate)( ICompositorStatics *iface, float rate );
} ICompositorStaticsVtbl;
struct ICompositorStatics
{
    const struct ICompositorStaticsVtbl *lpVtbl;
};

/*
 * Microsoft.UI.Composition.IInteropCompositorFactoryRestricted
 * {b27006d1-d095-432c-9d79-038d269d7aa3}
 * CreateDwmCompositor (dcompi 0x18006cd90) pre-nulls both outputs and returns E_NOTIMPL.
 */
typedef struct IInteropCompositorFactoryRestricted IInteropCompositorFactoryRestricted;
typedef struct IInteropCompositorFactoryRestrictedVtbl
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)( IInteropCompositorFactoryRestricted *iface, REFIID riid, void **obj );
    ULONG (STDMETHODCALLTYPE *AddRef)( IInteropCompositorFactoryRestricted *iface );
    ULONG (STDMETHODCALLTYPE *Release)( IInteropCompositorFactoryRestricted *iface );
    HRESULT (STDMETHODCALLTYPE *GetIids)( IInteropCompositorFactoryRestricted *iface, ULONG *count, IID **iids );
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)( IInteropCompositorFactoryRestricted *iface, HSTRING *name );
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)( IInteropCompositorFactoryRestricted *iface, TrustLevel *level );
    HRESULT (STDMETHODCALLTYPE *CreateDwmCompositor)( IInteropCompositorFactoryRestricted *iface,
            IUnknown *device, struct IInteropCompositorPartnerCallback *callback, REFIID riid,
            void **result, void **dwm_channel );
} IInteropCompositorFactoryRestrictedVtbl;
struct IInteropCompositorFactoryRestricted
{
    const struct IInteropCompositorFactoryRestrictedVtbl *lpVtbl;
};

/* The activatable class this module is built for. */
extern const WCHAR dcompi_compositor_class[];
/* Namespace prefix of every class the real dcompi.dll carries in its creator map. */
extern const WCHAR dcompi_namespace[];

IActivationFactory *compositor_factory_get( void );

/*
 * The InteropCompositor object, created per CreateInteropCompositor call (dcompi
 * 0x18005a60c) over the fork's DirectComposition device.
 */
struct interop_compositor
{
    struct IInspectable IInspectable_iface;
    LONG ref;
    /* The DirectComposition device this object is built over - the same device the fork's
     * windows.ui.composition Compositor drives the XAML visual tree through. */
    IDCompositionDevice2 *device2;
    /* The {d14b6158} IDCompositionDesktopDevicePartner view of that device, handed out by
     * QI: the partner slots 27/28/29 are dlls/dcomp's implementation, reached by
     * delegation, not reimplemented here.  Kept as an IUnknown because the module never
     * calls through the partner vtable; the pointer QI hands out is the one XAML stores as
     * DCompTreeHost::m_spMainDevice. */
    IUnknown *partner;
    /* The Windows.UI.Composition Compositor over the same device, for the ICompositor
     * family of requests. */
    IUnknown *compositor;
    /* The caller's IInteropCompositorPartnerCallback; dcompi stores it at object+1240
     * (0x1800f08ec). */
    IInteropCompositorPartnerCallback *callback;
};

HRESULT interop_compositor_create( IUnknown *device, IInteropCompositorPartnerCallback *callback,
        struct interop_compositor **out );

#endif /* __COMPI_PRIVATE_H */
