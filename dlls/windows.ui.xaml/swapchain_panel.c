/* WinRT Windows.UI.Xaml.Controls.SwapChainPanel Implementation
 *
 * Copyright (C) 2026 Wine project
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

#include "private.h"

WINE_DEFAULT_DEBUG_CHANNEL(xaml);

struct swapchain_panel
{
    IInspectable IInspectable_iface;
    ISwapChainPanelNative2 ISwapChainPanelNative2_iface;
    LONG ref;
    IDXGISwapChain *swapchain;
    HANDLE swapchain_handle;
};

static inline struct swapchain_panel *impl_from_IInspectable( IInspectable *iface )
{
    return CONTAINING_RECORD( iface, struct swapchain_panel, IInspectable_iface );
}

static inline struct swapchain_panel *impl_from_ISwapChainPanelNative2( ISwapChainPanelNative2 *iface )
{
    return CONTAINING_RECORD( iface, struct swapchain_panel, ISwapChainPanelNative2_iface );
}

static HRESULT WINAPI swapchain_panel_QueryInterface( IInspectable *iface, REFIID iid, void **out )
{
    struct swapchain_panel *impl = impl_from_IInspectable( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ))
    {
        *out = &impl->IInspectable_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_ISwapChainPanelNative ) ||
        IsEqualGUID( iid, &IID_ISwapChainPanelNative2 ))
    {
        *out = &impl->ISwapChainPanelNative2_iface;
        IInspectable_AddRef( (IInspectable *)&impl->IInspectable_iface );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI swapchain_panel_AddRef( IInspectable *iface )
{
    struct swapchain_panel *impl = impl_from_IInspectable( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p increasing refcount to %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI swapchain_panel_Release( IInspectable *iface )
{
    struct swapchain_panel *impl = impl_from_IInspectable( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    TRACE( "iface %p decreasing refcount to %lu.\n", iface, ref );
    if (!ref)
    {
        if (impl->swapchain)
            IDXGISwapChain_Release( impl->swapchain );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI swapchain_panel_GetIids( IInspectable *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI swapchain_panel_GetRuntimeClassName( IInspectable *iface, HSTRING *class_name )
{
    return WindowsCreateString( L"Windows.UI.Xaml.Controls.SwapChainPanel",
                                wcslen( L"Windows.UI.Xaml.Controls.SwapChainPanel" ),
                                class_name );
}

static HRESULT WINAPI swapchain_panel_GetTrustLevel( IInspectable *iface, TrustLevel *trust_level )
{
    if (!trust_level) return E_POINTER;
    *trust_level = BaseTrust;
    return S_OK;
}

static const struct IInspectableVtbl swapchain_panel_vtbl =
{
    swapchain_panel_QueryInterface,
    swapchain_panel_AddRef,
    swapchain_panel_Release,
    swapchain_panel_GetIids,
    swapchain_panel_GetRuntimeClassName,
    swapchain_panel_GetTrustLevel,
};

static HRESULT WINAPI panel_native_QueryInterface( ISwapChainPanelNative2 *iface, REFIID iid, void **out )
{
    struct swapchain_panel *impl = impl_from_ISwapChainPanelNative2( iface );
    return swapchain_panel_QueryInterface( &impl->IInspectable_iface, iid, out );
}

static ULONG WINAPI panel_native_AddRef( ISwapChainPanelNative2 *iface )
{
    struct swapchain_panel *impl = impl_from_ISwapChainPanelNative2( iface );
    return swapchain_panel_AddRef( &impl->IInspectable_iface );
}

static ULONG WINAPI panel_native_Release( ISwapChainPanelNative2 *iface )
{
    struct swapchain_panel *impl = impl_from_ISwapChainPanelNative2( iface );
    return swapchain_panel_Release( &impl->IInspectable_iface );
}

static HRESULT WINAPI panel_native_SetSwapChain( ISwapChainPanelNative2 *iface, IDXGISwapChain *swapchain )
{
    struct swapchain_panel *impl = impl_from_ISwapChainPanelNative2( iface );

    TRACE( "iface %p, swapchain %p.\n", iface, swapchain );

    if (impl->swapchain)
        IDXGISwapChain_Release( impl->swapchain );
    impl->swapchain = swapchain;
    if (swapchain)
        IDXGISwapChain_AddRef( swapchain );

    return S_OK;
}

static HRESULT WINAPI panel_native_SetSwapChainHandle( ISwapChainPanelNative2 *iface, HANDLE handle )
{
    struct swapchain_panel *impl = impl_from_ISwapChainPanelNative2( iface );

    TRACE( "iface %p, handle %p.\n", iface, handle );

    impl->swapchain_handle = handle;
    return S_OK;
}

static const struct ISwapChainPanelNative2Vtbl panel_native_vtbl =
{
    panel_native_QueryInterface,
    panel_native_AddRef,
    panel_native_Release,
    panel_native_SetSwapChain,
    panel_native_SetSwapChainHandle,
};

struct swapchain_panel_factory
{
    IActivationFactory IActivationFactory_iface;
    ISwapChainPanelNative2 ISwapChainPanelNative2_iface;
    LONG ref;
};

static inline struct swapchain_panel_factory *impl_from_IActivationFactory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct swapchain_panel_factory, IActivationFactory_iface );
}

static inline struct swapchain_panel_factory *factory_from_ISwapChainPanelNative2( ISwapChainPanelNative2 *iface )
{
    return CONTAINING_RECORD( iface, struct swapchain_panel_factory, ISwapChainPanelNative2_iface );
}

static HRESULT WINAPI factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct swapchain_panel_factory *impl = impl_from_IActivationFactory( iface );

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

    if (IsEqualGUID( iid, &IID_ISwapChainPanelNative ) ||
        IsEqualGUID( iid, &IID_ISwapChainPanelNative2 ))
    {
        *out = &impl->ISwapChainPanelNative2_iface;
        IInspectable_AddRef( (IInspectable *)&impl->IActivationFactory_iface );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI factory_AddRef( IActivationFactory *iface )
{
    struct swapchain_panel_factory *impl = impl_from_IActivationFactory( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p increasing refcount to %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI factory_Release( IActivationFactory *iface )
{
    struct swapchain_panel_factory *impl = impl_from_IActivationFactory( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    TRACE( "iface %p decreasing refcount to %lu.\n", iface, ref );
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
    struct swapchain_panel *impl;

    TRACE( "iface %p, instance %p.\n", iface, instance );

    if (!instance) return E_POINTER;

    impl = calloc( 1, sizeof(*impl) );
    if (!impl) return E_OUTOFMEMORY;

    impl->IInspectable_iface.lpVtbl = &swapchain_panel_vtbl;
    impl->ISwapChainPanelNative2_iface.lpVtbl = &panel_native_vtbl;
    impl->ref = 1;

    *instance = &impl->IInspectable_iface;
    return S_OK;
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

static HRESULT WINAPI factory_native_QueryInterface( ISwapChainPanelNative2 *iface, REFIID iid, void **out )
{
    struct swapchain_panel_factory *impl = factory_from_ISwapChainPanelNative2( iface );
    return factory_QueryInterface( &impl->IActivationFactory_iface, iid, out );
}

static ULONG WINAPI factory_native_AddRef( ISwapChainPanelNative2 *iface )
{
    struct swapchain_panel_factory *impl = factory_from_ISwapChainPanelNative2( iface );
    return factory_AddRef( &impl->IActivationFactory_iface );
}

static ULONG WINAPI factory_native_Release( ISwapChainPanelNative2 *iface )
{
    struct swapchain_panel_factory *impl = factory_from_ISwapChainPanelNative2( iface );
    return factory_Release( &impl->IActivationFactory_iface );
}

static HRESULT WINAPI factory_native_SetSwapChain( ISwapChainPanelNative2 *iface, IDXGISwapChain *swapchain )
{
    TRACE( "factory iface %p, swapchain %p.\n", iface, swapchain );
    return S_OK;
}

static HRESULT WINAPI factory_native_SetSwapChainHandle( ISwapChainPanelNative2 *iface, HANDLE handle )
{
    TRACE( "factory iface %p, handle %p.\n", iface, handle );
    return S_OK;
}

static const struct ISwapChainPanelNative2Vtbl factory_native_vtbl =
{
    factory_native_QueryInterface,
    factory_native_AddRef,
    factory_native_Release,
    factory_native_SetSwapChain,
    factory_native_SetSwapChainHandle,
};

static struct swapchain_panel_factory swapchain_panel_factory_obj =
{
    {&factory_vtbl},
    {&factory_native_vtbl},
    1,
};

IActivationFactory *swapchain_panel_factory = &swapchain_panel_factory_obj.IActivationFactory_iface;
