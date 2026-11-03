/* WinRT Windows.ApplicationModel.DataTransfer Implementation
 *
 * Copyright 2026 Wine Developers
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

/* The uuid import library owns the OLE GUID set; include those headers before
 * initguid.h so this translation unit only defines the WinRT GUIDs, and the
 * WinRT idl chains (windows.storage streams) find them declared. */
#include "ole2.h"
#include "oleidl.h"
#include "servprov.h"
#include "initguid.h"
#include "private.h"

/* IID_IDataTransferManagerInterop (include/shobjidl_core.h, see private.h) is
 * owned by the uuid import library; declared in private.h, not defined here. */

WINE_DEFAULT_DEBUG_CHANNEL(dataexchange);

/*
 * Activation factories for the Windows.ApplicationModel.DataTransfer classes,
 * served out of windows.applicationmodel.dataexchange.dll - the dll Windows
 * itself hosts the namespace's implementation in.  The activatable classes
 * create their objects; the statics-only classes (Clipboard,
 * StandardDataFormats, DataTransferManager, HtmlFormatHelper, ShareProvider)
 * answer their statics interfaces and fail ActivateInstance, as Windows'
 * factories do for classes without a default constructor.  Classes the
 * namespace declares beyond this module's surface keep answering
 * E_NOINTERFACE, the documented fall-through of the combase route.
 */

struct class_factory
{
    IActivationFactory IActivationFactory_iface;
    LONG ref;
    IInspectable *(*create)( void );
};

static inline struct class_factory *impl_from_class_factory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct class_factory, IActivationFactory_iface );
}

static HRESULT WINAPI class_factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct class_factory *impl = impl_from_class_factory( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (!out) return E_POINTER;
    *out = NULL;

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = &impl->IActivationFactory_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG WINAPI class_factory_AddRef( IActivationFactory *iface )
{
    struct class_factory *impl = impl_from_class_factory( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI class_factory_Release( IActivationFactory *iface )
{
    struct class_factory *impl = impl_from_class_factory( iface );
    return InterlockedDecrement( &impl->ref );
}

static HRESULT WINAPI class_factory_GetIids( IActivationFactory *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI class_factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *class_name )
{
    FIXME( "iface %p, class_name %p stub!\n", iface, class_name );
    return E_NOTIMPL;
}

static HRESULT WINAPI class_factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI class_factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    struct class_factory *impl = impl_from_class_factory( iface );
    IInspectable *obj;

    TRACE( "iface %p, instance %p.\n", iface, instance );

    if (!instance) return E_POINTER;
    if (!(obj = impl->create())) return E_OUTOFMEMORY;

    *instance = obj;
    return S_OK;
}

static const struct IActivationFactoryVtbl class_factory_vtbl =
{
    class_factory_QueryInterface,
    class_factory_AddRef,
    class_factory_Release,
    class_factory_GetIids,
    class_factory_GetRuntimeClassName,
    class_factory_GetTrustLevel,
    class_factory_ActivateInstance,
};

#define DEFINE_CLASS_FACTORY( name, create_func )                                                  \
    static struct class_factory name = { { &class_factory_vtbl }, 1, create_func }

IInspectable *create_core_drag_drop_manager( void );
IInspectable *create_share_ui_options( void );
IInspectable *create_clipboard_content_options( void );

/* DataPackageView and DataPackagePropertySetView are activatable (the
 * projections default-construct them); the stand-ins are the empty objects. */
static IInspectable *create_data_package_view_standin( void ) { return create_data_package_view( NULL ); }
static IInspectable *create_property_set_view_standin( void ) { return create_property_set_view( NULL ); }

DEFINE_CLASS_FACTORY( data_package_factory, create_data_package );
DEFINE_CLASS_FACTORY( data_package_view_factory, create_data_package_view_standin );
DEFINE_CLASS_FACTORY( property_set_factory, create_property_set );
DEFINE_CLASS_FACTORY( property_set_view_factory, create_property_set_view_standin );
DEFINE_CLASS_FACTORY( core_drag_drop_manager_factory, create_core_drag_drop_manager );
DEFINE_CLASS_FACTORY( share_ui_options_factory, create_share_ui_options );
DEFINE_CLASS_FACTORY( clipboard_content_options_factory, create_clipboard_content_options );

HRESULT WINAPI DllGetActivationFactory( HSTRING classid, IActivationFactory **factory )
{
    const WCHAR *name = WindowsGetStringRawBuffer( classid, NULL );

    TRACE( "classid %s, factory %p.\n", debugstr_hstring( classid ), factory );

    if (!factory) return E_POINTER;
    *factory = NULL;

    if (!name) return E_INVALIDARG;

    if (!wcscmp( name, RuntimeClass_Windows_ApplicationModel_DataTransfer_DataPackage ))
        IActivationFactory_QueryInterface( &data_package_factory.IActivationFactory_iface, &IID_IActivationFactory,
                                           (void **)factory );
    else if (!wcscmp( name, RuntimeClass_Windows_ApplicationModel_DataTransfer_DataPackageView ))
        IActivationFactory_QueryInterface( &data_package_view_factory.IActivationFactory_iface, &IID_IActivationFactory,
                                           (void **)factory );
    else if (!wcscmp( name, RuntimeClass_Windows_ApplicationModel_DataTransfer_DataPackagePropertySet ))
        IActivationFactory_QueryInterface( &property_set_factory.IActivationFactory_iface, &IID_IActivationFactory,
                                           (void **)factory );
    else if (!wcscmp( name, RuntimeClass_Windows_ApplicationModel_DataTransfer_DataPackagePropertySetView ))
        IActivationFactory_QueryInterface( &property_set_view_factory.IActivationFactory_iface, &IID_IActivationFactory,
                                           (void **)factory );
    else if (!wcscmp( name, RuntimeClass_Windows_ApplicationModel_DataTransfer_ShareUIOptions ))
        IActivationFactory_QueryInterface( &share_ui_options_factory.IActivationFactory_iface, &IID_IActivationFactory,
                                           (void **)factory );
    else if (!wcscmp( name, RuntimeClass_Windows_ApplicationModel_DataTransfer_ClipboardContentOptions ))
        IActivationFactory_QueryInterface( &clipboard_content_options_factory.IActivationFactory_iface, &IID_IActivationFactory,
                                           (void **)factory );
    else if (!wcscmp( name, RuntimeClass_Windows_ApplicationModel_DataTransfer_Clipboard ))
        IActivationFactory_QueryInterface( clipboard_statics_factory, &IID_IActivationFactory, (void **)factory );
    else if (!wcscmp( name, RuntimeClass_Windows_ApplicationModel_DataTransfer_StandardDataFormats ))
        IActivationFactory_QueryInterface( standard_data_formats_factory, &IID_IActivationFactory, (void **)factory );
    else if (!wcscmp( name, RuntimeClass_Windows_ApplicationModel_DataTransfer_DataTransferManager ))
        IActivationFactory_QueryInterface( data_transfer_manager_factory, &IID_IActivationFactory, (void **)factory );
    else if (!wcscmp( name, RuntimeClass_Windows_ApplicationModel_DataTransfer_HtmlFormatHelper ))
        IActivationFactory_QueryInterface( html_format_helper_factory, &IID_IActivationFactory, (void **)factory );
    else if (!wcscmp( name, RuntimeClass_Windows_ApplicationModel_DataTransfer_ShareProvider ))
        IActivationFactory_QueryInterface( share_provider_factory, &IID_IActivationFactory, (void **)factory );
    else if (!wcscmp( name, L"Windows.ApplicationModel.DataTransfer.DragDrop.Core.CoreDragDropManager" ))
        IActivationFactory_QueryInterface( &core_drag_drop_manager_factory.IActivationFactory_iface, &IID_IActivationFactory,
                                           (void **)factory );

    if (*factory) return S_OK;
    return E_NOINTERFACE;
}

HRESULT WINAPI DllCanUnloadNow( void )
{
    return S_FALSE;
}

HRESULT WINAPI DllGetClassObject( REFCLSID clsid, REFIID riid, void **out )
{
    FIXME( "clsid %s, riid %s, out %p stub!\n", debugstr_guid( clsid ), debugstr_guid( riid ), out );
    return CLASS_E_CLASSNOTAVAILABLE;
}
