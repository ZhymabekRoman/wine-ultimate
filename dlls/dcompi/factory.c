/*
 * The Microsoft.UI.Composition.Compositor activation factory (dcompi.dll).
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
 * The interface table of the real CompositorFactory, from CanCastTo (dcompi 0x180040f78):
 * one object serves IActivationFactory, IAgileObject, IMarshal, the partner selector, the
 * statics and the restricted factory.
 */
static const GUID IID_IInteropCompositorFactoryPartner =
    { 0x22118adf, 0x23f1, 0x4801, { 0xbc, 0xfa, 0x66, 0xcb, 0xf4, 0x8c, 0xc5, 0x1b } };
static const GUID IID_ICompositorStatics =
    { 0x6baa947a, 0xf103, 0x55c6, { 0x91, 0xad, 0x3d, 0x27, 0x5b, 0xea, 0x65, 0xc4 } };
static const GUID IID_IInteropCompositorFactoryRestricted =
    { 0xb27006d1, 0xd095, 0x432c, { 0x9d, 0x79, 0x03, 0x8d, 0x26, 0x9d, 0x7a, 0xa3 } };
/* The class-interface IID of Microsoft.UI.Composition.ICompositor, which the .NET and
 * C++/WinRT projections pass straight to RoGetActivationFactory. */
static const GUID IID_IMUICompositor =
    { 0x95213c13, 0xc4cb, 0x57de, { 0xb2, 0x67, 0xd2, 0x1a, 0xb9, 0x01, 0xae, 0x38 } };

/*
 * The composition object this fork has is windows.ui.composition's DirectComposition-backed
 * Compositor, reachable through combase's route for Windows.UI.Composition.Compositor
 * (dlls/combase/roapi.c:126-130).  Microsoft.UI.Composition object classes (Visuals,
 * brushes, animations, ...) have no Wine implementation, so requests for them pass through
 * and the result is reported honestly.
 */
static HRESULT activate_bridge_compositor( REFIID iid, void **out )
{
    static const WCHAR bridge_class[] = L"Windows.UI.Composition.Compositor";
    HSTRING str = NULL;
    HRESULT hr;

    if (!out) return E_POINTER;
    *out = NULL;

    hr = WindowsCreateString( bridge_class, ARRAYSIZE( bridge_class ) - 1, &str );
    if (FAILED( hr )) return hr;
    hr = RoGetActivationFactory( str, iid, out );
    WindowsDeleteString( str );
    return hr;
}

struct compositor_factory
{
    struct IActivationFactory IActivationFactory_iface;
    struct IInteropCompositorFactoryPartner IInteropCompositorFactoryPartner_iface;
    struct ICompositorStatics ICompositorStatics_iface;
    struct IInteropCompositorFactoryRestricted IInteropCompositorFactoryRestricted_iface;
    LONG ref;
    float max_global_playback_rate;
};

static inline struct compositor_factory *impl_from_IActivationFactory( struct IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct compositor_factory, IActivationFactory_iface );
}

static inline struct compositor_factory *impl_from_IInteropCompositorFactoryPartner(
        struct IInteropCompositorFactoryPartner *iface )
{
    return CONTAINING_RECORD( iface, struct compositor_factory, IInteropCompositorFactoryPartner_iface );
}

static inline struct compositor_factory *impl_from_ICompositorStatics( struct ICompositorStatics *iface )
{
    return CONTAINING_RECORD( iface, struct compositor_factory, ICompositorStatics_iface );
}

static inline struct compositor_factory *impl_from_IInteropCompositorFactoryRestricted(
        struct IInteropCompositorFactoryRestricted *iface )
{
    return CONTAINING_RECORD( iface, struct compositor_factory, IInteropCompositorFactoryRestricted_iface );
}

static const struct IActivationFactoryVtbl factory_vtbl;
static const struct IInteropCompositorFactoryPartnerVtbl partner_vtbl;
static const struct ICompositorStaticsVtbl statics_vtbl;
static const struct IInteropCompositorFactoryRestrictedVtbl restricted_vtbl;

/*
 * One reference count for the whole object, one pointer per interface, exactly the WRL
 * Implements<> layout CanCastTo walks.  The FtmBase marker makes the factory free-threaded,
 * so the identity rules below are what the FTM hands out process-wide.
 */
static HRESULT factory_query( struct compositor_factory *impl, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    *out = NULL;

    if (IsEqualGUID( iid, &IID_IUnknown )
            || IsEqualGUID( iid, &IID_IInspectable )
            || IsEqualGUID( iid, &IID_IAgileObject )
            || IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = &impl->IActivationFactory_iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    /* IMarshal: CanCastTo answers it from the base pointer; FtmBase supplies the real
     * free-threaded marshaller on Windows, Wine's apartment layer does not need one here. */
    if (IsEqualGUID( iid, &IID_IMarshal ))
    {
        TRACE( "IMarshal: answering with the factory itself, as CanCastTo's base pointer does.\n" );
        *out = &impl->IActivationFactory_iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IInteropCompositorFactoryPartner ))
    {
        *out = &impl->IInteropCompositorFactoryPartner_iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_ICompositorStatics ))
    {
        *out = &impl->ICompositorStatics_iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IInteropCompositorFactoryRestricted ))
    {
        *out = &impl->IInteropCompositorFactoryRestricted_iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    /* The class-interface request shape: the factory hands out the composition object. */
    if (IsEqualGUID( iid, &IID_IMUICompositor ))
    {
        FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
        return E_NOINTERFACE;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static HRESULT STDMETHODCALLTYPE factory_QueryInterface( struct IActivationFactory *iface, REFIID iid, void **out )
{
    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );
    return factory_query( impl_from_IActivationFactory( iface ), iid, out );
}

static ULONG STDMETHODCALLTYPE factory_AddRef( struct IActivationFactory *iface )
{
    return InterlockedIncrement( &impl_from_IActivationFactory( iface )->ref );
}

static ULONG STDMETHODCALLTYPE factory_Release( struct IActivationFactory *iface )
{
    return InterlockedDecrement( &impl_from_IActivationFactory( iface )->ref );
}

static HRESULT STDMETHODCALLTYPE factory_GetIids( struct IActivationFactory *iface, ULONG *count, IID **iids )
{
    FIXME( "iface %p, count %p, iids %p stub!\n", iface, count, iids );
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE factory_GetRuntimeClassName( struct IActivationFactory *iface, HSTRING *name )
{
    return WindowsCreateString( dcompi_compositor_class, lstrlenW( dcompi_compositor_class ), name );
}

static HRESULT STDMETHODCALLTYPE factory_GetTrustLevel( struct IActivationFactory *iface, TrustLevel *level )
{
    FIXME( "iface %p, level %p stub!\n", iface, level );
    return E_NOTIMPL;
}

/* Activating the class hands out the composition object, the same one the partner factory
 * binds a device to. */
static HRESULT STDMETHODCALLTYPE factory_ActivateInstance( struct IActivationFactory *iface,
        IInspectable **instance )
{
    TRACE( "iface %p, instance %p.\n", iface, instance );

    if (!instance) return E_POINTER;

    return activate_bridge_compositor( &IID_IInspectable, (void **)instance );
}

static const struct IActivationFactoryVtbl factory_vtbl =
{
    factory_QueryInterface,
    factory_AddRef,
    factory_Release,
    factory_GetIids,
    factory_GetRuntimeClassName,
    factory_GetTrustLevel,
    factory_ActivateInstance,
};

/*
 * IInteropCompositorFactoryPartner, the interface Microsoft.ui.xaml.dll activates the class
 * for (ActivationFactoryCache::GetInteropCompositorFactoryPartner, 0x18023586c).
 */

static HRESULT STDMETHODCALLTYPE partner_QueryInterface( struct IInteropCompositorFactoryPartner *iface,
        REFIID iid, void **out )
{
    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IInteropCompositorFactoryPartner ))
    {
        *out = iface;
        InterlockedIncrement( &impl_from_IInteropCompositorFactoryPartner( iface )->ref );
        return S_OK;
    }
    return factory_query( impl_from_IInteropCompositorFactoryPartner( iface ), iid, out );
}

static ULONG STDMETHODCALLTYPE partner_AddRef( struct IInteropCompositorFactoryPartner *iface )
{
    return InterlockedIncrement( &impl_from_IInteropCompositorFactoryPartner( iface )->ref );
}

static ULONG STDMETHODCALLTYPE partner_Release( struct IInteropCompositorFactoryPartner *iface )
{
    return InterlockedDecrement( &impl_from_IInteropCompositorFactoryPartner( iface )->ref );
}

static HRESULT STDMETHODCALLTYPE partner_GetIids( struct IInteropCompositorFactoryPartner *iface,
        ULONG *count, IID **iids )
{
    FIXME( "iface %p, count %p, iids %p stub!\n", iface, count, iids );
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE partner_GetRuntimeClassName( struct IInteropCompositorFactoryPartner *iface,
        HSTRING *name )
{
    return WindowsCreateString( dcompi_compositor_class, lstrlenW( dcompi_compositor_class ), name );
}

static HRESULT STDMETHODCALLTYPE partner_GetTrustLevel( struct IInteropCompositorFactoryPartner *iface,
        TrustLevel *level )
{
    FIXME( "iface %p, level %p stub!\n", iface, level );
    return E_NOTIMPL;
}

/*
 * The composition channel.  dcompi's CompositorFactory::CreateInteropCompositor
 * (0x18005a5d0, source onecoreuap\windows\dwm\dcomp\winrtnested\wrtcompositor.cpp:204A-204C):
 * pre-null *result, allocate a fresh InteropCompositor per call (0x18005a60c),
 * RuntimeClassInitialize(device, callback) (0x18005a6c7 -> 0x1800f0800, which stores the
 * callback at object+1240, 0x1800f08ec), QI the object for the caller's riid (0x18005a759),
 * return it and drop the local reference.  A failure is fatal for the caller: XAML's
 * DCompTreeHost::CreateWinRTInteropCompositionDevice (0x180235843) FailFasts the process
 * on any hr < 0.
 */
static HRESULT WINAPI partner_CreateInteropCompositor( struct IInteropCompositorFactoryPartner *iface,
        IUnknown *device, IInteropCompositorPartnerCallback *callback, REFIID riid, void **result )
{
    struct interop_compositor *compositor;
    HRESULT hr;

    TRACE( "iface %p, device %p, callback %p, riid %s, result %p.\n",
            iface, device, callback, debugstr_guid( riid ), result );

    if (!result) return E_POINTER;
    *result = NULL;

    hr = interop_compositor_create( device, callback, &compositor );
    if (FAILED( hr ))
    {
        ERR( "no composition device for the interop compositor channel, hr %#lx.\n", hr );
        return hr;
    }

    hr = IInspectable_QueryInterface( &compositor->IInspectable_iface, riid, result );
    IInspectable_Release( &compositor->IInspectable_iface );
    if (FAILED( hr )) *result = NULL;
    return hr;
}

/* dcompi 0x180056d00: writes true to both outputs and returns S_OK unconditionally; there
 * is no registry or configuration gate. */
static HRESULT WINAPI partner_CheckEnabled( struct IInteropCompositorFactoryPartner *iface,
        bool *enabled, bool *available )
{
    TRACE( "iface %p, enabled %p, available %p.\n", iface, enabled, available );

    if (!enabled || !available) return E_POINTER;

    *enabled = true;
    *available = true;
    return S_OK;
}

static const struct IInteropCompositorFactoryPartnerVtbl partner_vtbl =
{
    partner_QueryInterface,
    partner_AddRef,
    partner_Release,
    partner_GetIids,
    partner_GetRuntimeClassName,
    partner_GetTrustLevel,
    partner_CreateInteropCompositor,
    partner_CheckEnabled,
};

/*
 * ICompositorStatics, slot 6/7 of the 8-slot vtable at dcompi 0x180122c00.
 */

static HRESULT STDMETHODCALLTYPE statics_QueryInterface( struct ICompositorStatics *iface, REFIID iid, void **out )
{
    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_ICompositorStatics ))
    {
        *out = iface;
        InterlockedIncrement( &impl_from_ICompositorStatics( iface )->ref );
        return S_OK;
    }
    return factory_query( impl_from_ICompositorStatics( iface ), iid, out );
}

static ULONG STDMETHODCALLTYPE statics_AddRef( struct ICompositorStatics *iface )
{
    return InterlockedIncrement( &impl_from_ICompositorStatics( iface )->ref );
}

static ULONG STDMETHODCALLTYPE statics_Release( struct ICompositorStatics *iface )
{
    return InterlockedDecrement( &impl_from_ICompositorStatics( iface )->ref );
}

static HRESULT STDMETHODCALLTYPE statics_GetIids( struct ICompositorStatics *iface, ULONG *count, IID **iids )
{
    FIXME( "iface %p, count %p, iids %p stub!\n", iface, count, iids );
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE statics_GetRuntimeClassName( struct ICompositorStatics *iface, HSTRING *name )
{
    return WindowsCreateString( dcompi_compositor_class, lstrlenW( dcompi_compositor_class ), name );
}

static HRESULT STDMETHODCALLTYPE statics_GetTrustLevel( struct ICompositorStatics *iface, TrustLevel *level )
{
    FIXME( "iface %p, level %p stub!\n", iface, level );
    return E_NOTIMPL;
}

/* dcompi 0x18004cd40: *rate = 16.0f, S_OK - the global playback-rate ceiling, a constant. */
#define DCOMPI_MAX_GLOBAL_PLAYBACK_RATE 16.0f

static HRESULT STDMETHODCALLTYPE statics_get_MaxGlobalPlaybackRate( struct ICompositorStatics *iface,
        float *rate )
{
    TRACE( "iface %p, rate %p.\n", iface, rate );

    if (!rate) return E_POINTER;
    *rate = DCOMPI_MAX_GLOBAL_PLAYBACK_RATE;
    return S_OK;
}

/*
 * Slot 7 of the vtable.  In the binary it is code-folded with
 * AnimationControllerFactory::get_MinPlaybackRate (0x18004cd50, *p = 0.0625, S_OK), i.e.
 * the setter carries no validation and no state a getter can observe; the getter stays the
 * 16.0 constant.  The value is stored so a future animation pump has it.
 */
static HRESULT STDMETHODCALLTYPE statics_put_MaxGlobalPlaybackRate( struct ICompositorStatics *iface,
        float rate )
{
    struct compositor_factory *impl = impl_from_ICompositorStatics( iface );

    TRACE( "iface %p, rate %.8g.\n", iface, rate );

    impl->max_global_playback_rate = rate;
    return S_OK;
}

static const struct ICompositorStaticsVtbl statics_vtbl =
{
    statics_QueryInterface,
    statics_AddRef,
    statics_Release,
    statics_GetIids,
    statics_GetRuntimeClassName,
    statics_GetTrustLevel,
    statics_get_MaxGlobalPlaybackRate,
    statics_put_MaxGlobalPlaybackRate,
};

/*
 * IInteropCompositorFactoryRestricted, the 7-slot vtable at dcompi 0x180122bc8.
 */

static HRESULT STDMETHODCALLTYPE restricted_QueryInterface( struct IInteropCompositorFactoryRestricted *iface,
        REFIID iid, void **out )
{
    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IInteropCompositorFactoryRestricted ))
    {
        *out = iface;
        InterlockedIncrement( &impl_from_IInteropCompositorFactoryRestricted( iface )->ref );
        return S_OK;
    }
    return factory_query( impl_from_IInteropCompositorFactoryRestricted( iface ), iid, out );
}

static ULONG STDMETHODCALLTYPE restricted_AddRef( struct IInteropCompositorFactoryRestricted *iface )
{
    return InterlockedIncrement( &impl_from_IInteropCompositorFactoryRestricted( iface )->ref );
}

static ULONG STDMETHODCALLTYPE restricted_Release( struct IInteropCompositorFactoryRestricted *iface )
{
    return InterlockedDecrement( &impl_from_IInteropCompositorFactoryRestricted( iface )->ref );
}

static HRESULT STDMETHODCALLTYPE restricted_GetIids( struct IInteropCompositorFactoryRestricted *iface,
        ULONG *count, IID **iids )
{
    FIXME( "iface %p, count %p, iids %p stub!\n", iface, count, iids );
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE restricted_GetRuntimeClassName(
        struct IInteropCompositorFactoryRestricted *iface, HSTRING *name )
{
    return WindowsCreateString( dcompi_compositor_class, lstrlenW( dcompi_compositor_class ), name );
}

static HRESULT STDMETHODCALLTYPE restricted_GetTrustLevel(
        struct IInteropCompositorFactoryRestricted *iface, TrustLevel *level )
{
    FIXME( "iface %p, level %p stub!\n", iface, level );
    return E_NOTIMPL;
}

/*
 * dcompi 0x18006cd90 pre-nulls both outputs and returns E_NOTIMPL (0x80004001) on Windows
 * itself: the DwmChannel path is the DWM-hosted nesting channel.  Wine has no DwmChannel,
 * so the module answers exactly what the binary answers for a request it cannot serve.
 */
static HRESULT STDMETHODCALLTYPE restricted_CreateDwmCompositor(
        struct IInteropCompositorFactoryRestricted *iface, IUnknown *device,
        IInteropCompositorPartnerCallback *callback, REFIID riid, void **result, void **dwm_channel )
{
    TRACE( "iface %p, device %p, callback %p, riid %s, result %p, dwm_channel %p.\n",
            iface, device, callback, debugstr_guid( riid ), result, dwm_channel );

    if (result) *result = NULL;
    if (dwm_channel) *dwm_channel = NULL;
    return E_NOTIMPL;
}

static const struct IInteropCompositorFactoryRestrictedVtbl restricted_vtbl =
{
    restricted_QueryInterface,
    restricted_AddRef,
    restricted_Release,
    restricted_GetIids,
    restricted_GetRuntimeClassName,
    restricted_GetTrustLevel,
    restricted_CreateDwmCompositor,
};

/*
 * One factory instance for the life of the process, the WRL module cache
 * (Details::GetCacheEntry, used by DllGetActivationFactory 0x180046970 and DllGetClassObject
 * 0x180045a30) plus FtmBase: every activation of the class resolves to this identity.
 */
static struct compositor_factory compositor_factory =
{
    { &factory_vtbl },
    { &partner_vtbl },
    { &statics_vtbl },
    { &restricted_vtbl },
    1,
    DCOMPI_MAX_GLOBAL_PLAYBACK_RATE,
};

IActivationFactory *compositor_factory_get( void )
{
    return (IActivationFactory *)&compositor_factory;
}
