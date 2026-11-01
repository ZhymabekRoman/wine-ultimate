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

/*
 * The Microsoft.UI.Composition factory-side interfaces of the Microsoft.WindowsAppRuntime
 * composition provider (dcompi.dll).  They have no public header: the declarations below
 * are recovered from the app's own dcompi.dll, vtable-by-vtable.
 *
 * The CompositorFactory object is
 *   WRL::Implements<FtmBase, IInteropCompositorFactoryPartner> + ICompositorStatics
 *   + IInteropCompositorFactoryRestricted;  its IID -> offset table is CanCastTo
 *   (0x180040f78) and the four vtables are CompositorFactory vftables
 *     0x180122cc8  IActivationFactory
 *     0x180122c40  IInteropCompositorFactoryPartner  (8 slots)
 *     0x180122c00  ICompositorStatics                (8 slots)
 *     0x180122bc8  IInteropCompositorFactoryRestricted (7 slots)
 */

#ifndef __COMPI_TEST_H
#define __COMPI_TEST_H

#include "windef.h"
#include "winbase.h"
#include "objidl.h"
#include "inspectable.h"
#include "activation.h"

/* {22118adf-23f1-4801-bcfa-66cbf48cc51b} */
static const IID IID_IInteropCompositorFactoryPartner_test =
    { 0x22118adf, 0x23f1, 0x4801, { 0xbc, 0xfa, 0x66, 0xcb, 0xf4, 0x8c, 0xc5, 0x1b } };
/* {6baa947a-f103-55c6-91ad-3d275bea65c4} */
static const IID IID_ICompositorStatics_test =
    { 0x6baa947a, 0xf103, 0x55c6, { 0x91, 0xad, 0x3d, 0x27, 0x5b, 0xea, 0x65, 0xc4 } };
/* {b27006d1-d095-432c-9d79-038d269d7aa3} */
static const IID IID_IInteropCompositorFactoryRestricted_test =
    { 0xb27006d1, 0xd095, 0x432c, { 0x9d, 0x79, 0x03, 0x8d, 0x26, 0x9d, 0x7a, 0xa3 } };

/*
 * Microsoft.UI.Composition.IInteropCompositorPartnerCallback: XAML's implementation is
 * Microsoft.ui.xaml.dll vftable 0x180a8e620, IUnknown + NotifyDirty (0x18022e750 ->
 * RequestMainDCompDeviceCommit).  The IID is not observable from the binary (the callback is
 * only ever handed to the factory, never QId for), so none is declared.  Declared first
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

#define IInteropCompositorFactoryPartner_QueryInterface(i,a,b) (i)->lpVtbl->QueryInterface(i,a,b)
#define IInteropCompositorFactoryPartner_AddRef(i) (i)->lpVtbl->AddRef(i)
#define IInteropCompositorFactoryPartner_Release(i) (i)->lpVtbl->Release(i)
#define IInteropCompositorFactoryPartner_CreateInteropCompositor(i,a,b,c,d) (i)->lpVtbl->CreateInteropCompositor(i,a,b,c,d)
#define IInteropCompositorFactoryPartner_CheckEnabled(i,a,b) (i)->lpVtbl->CheckEnabled(i,a,b)

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

#define ICompositorStatics_QueryInterface(i,a,b) (i)->lpVtbl->QueryInterface(i,a,b)
#define ICompositorStatics_AddRef(i) (i)->lpVtbl->AddRef(i)
#define ICompositorStatics_Release(i) (i)->lpVtbl->Release(i)
#define ICompositorStatics_get_MaxGlobalPlaybackRate(i,r) (i)->lpVtbl->get_MaxGlobalPlaybackRate(i,r)
#define ICompositorStatics_put_MaxGlobalPlaybackRate(i,r) (i)->lpVtbl->put_MaxGlobalPlaybackRate(i,r)

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

#define IInteropCompositorFactoryRestricted_QueryInterface(i,a,b) (i)->lpVtbl->QueryInterface(i,a,b)
#define IInteropCompositorFactoryRestricted_AddRef(i) (i)->lpVtbl->AddRef(i)
#define IInteropCompositorFactoryRestricted_Release(i) (i)->lpVtbl->Release(i)
#define IInteropCompositorFactoryRestricted_CreateDwmCompositor(i,a,b,c,d,e) (i)->lpVtbl->CreateDwmCompositor(i,a,b,c,d,e)

#endif /* __COMPI_TEST_H */
