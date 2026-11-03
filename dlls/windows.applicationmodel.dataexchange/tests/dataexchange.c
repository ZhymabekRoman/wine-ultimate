/*
 * Windows.ApplicationModel.DataTransfer (windows.applicationmodel.dataexchange.dll) tests
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

#define COBJMACROS

/* The uuid import library owns the OLE GUID set; include those headers before
 * initguid.h so this translation unit only defines the WinRT GUIDs. */
#include "ole2.h"
#include "oleidl.h"
#include "servprov.h"
#include "propidl.h"
#include "initguid.h"
#include <stdarg.h>

#define COBJMACROS
#include "windef.h"
#include "winbase.h"
#include "winstring.h"

#include "activation.h"
#include "asyncinfo.h"
#include "eventtoken.h"
#include "roapi.h"

#define WIDL_using_Windows_Foundation
#define WIDL_using_Windows_Foundation_Collections
#define WIDL_using_Windows_Security_EnterpriseData
#define WIDL_using_Windows_Storage
#define WIDL_using_Windows_Storage_Streams
#define WIDL_using_Windows_UI
#define WIDL_using_Windows_ApplicationModel_DataTransfer
#include "windows.foundation.h"
#include "windows.foundation.collections.h"
#include "windows.security.enterprisedata.h"
#include "windows.storage.h"
#include "windows.storage.streams.h"
#include "windows.ui.h"
#include "windows.applicationmodel.datatransfer.h"

/* The DragDrop.Core surface (CoreDragDropManager) of the module; the
 * interfaces are the include/windows.applicationmodel.datatransfer.dragdrop.core.idl
 * and include/dragdropinterop.idl shapes. */
#define WIDL_using_Windows_Graphics_Imaging
#define WIDL_using_Windows_ApplicationModel_DataTransfer_DragDrop
#define WIDL_using_Windows_ApplicationModel_DataTransfer_DragDrop_Core
#include "windows.graphics.imaging.h"
#include "windows.applicationmodel.datatransfer.dragdrop.h"
#include "windows.applicationmodel.datatransfer.dragdrop.core.h"
#include "dragdropinterop.h"

typedef __FITypedEventHandler_2_Windows__CApplicationModel__CDataTransfer__CDragDrop__CCore__CCoreDragDropManager_Windows__CApplicationModel__CDataTransfer__CDragDrop__CCore__CCoreDropOperationTargetRequestedEventArgs TargetRequestedHandler;
typedef __FITypedEventHandler_2_Windows__CApplicationModel__CDataTransfer__CDragDrop__CCore__CCoreDragDropManager_Windows__CApplicationModel__CDataTransfer__CDragDrop__CCore__CCoreDropOperationTargetRequestedEventArgsVtbl TargetRequestedHandlerVtbl;

/* See the module's private.h: the interop interface declaration mirrors
 * include/shobjidl_core.idl:42 and include/shobjidl_core.h. */
typedef struct IDataTransferManagerInterop IDataTransferManagerInterop;
typedef struct IDataTransferManagerInteropVtbl
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)( IDataTransferManagerInterop *This, REFIID riid, void **ppv );
    ULONG (STDMETHODCALLTYPE *AddRef)( IDataTransferManagerInterop *This );
    ULONG (STDMETHODCALLTYPE *Release)( IDataTransferManagerInterop *This );
    HRESULT (STDMETHODCALLTYPE *GetForWindow)( IDataTransferManagerInterop *This, HWND hwnd, REFIID riid, void **manager );
    HRESULT (STDMETHODCALLTYPE *ShowShareUIForWindow)( IDataTransferManagerInterop *This, HWND hwnd );
} IDataTransferManagerInteropVtbl;
struct IDataTransferManagerInterop { const IDataTransferManagerInteropVtbl *lpVtbl; };
#define IDataTransferManagerInterop_QueryInterface(This,riid,ppv)   (This)->lpVtbl->QueryInterface(This,riid,ppv)
#define IDataTransferManagerInterop_AddRef(This)                    (This)->lpVtbl->AddRef(This)
#define IDataTransferManagerInterop_Release(This)                   (This)->lpVtbl->Release(This)
#define IDataTransferManagerInterop_GetForWindow(This,h,riid,ppm)   (This)->lpVtbl->GetForWindow(This,h,riid,ppm)
#define IDataTransferManagerInterop_ShowShareUIForWindow(This,h)    (This)->lpVtbl->ShowShareUIForWindow(This,h)
/* IID_IDataTransferManagerInterop comes from the uuid import library. */
EXTERN_C const IID IID_IDataTransferManagerInterop;

#include "wine/test.h"

static const WCHAR test_text[] = L"hello dataexchange";
static const WCHAR test_html[] = L"<b>hello</b>";

static void check_hresult( ULONG line, HRESULT hr, HRESULT expected )
{
    ok_(__FILE__, line)( hr == expected, "got hr %#lx, expected %#lx.\n", hr, expected );
}
#define check_hr( hr, expected ) check_hresult( __LINE__, hr, expected )

static HSTRING str( const WCHAR *raw )
{
    HSTRING hstr = NULL;

    WindowsCreateString( raw, lstrlenW( raw ), &hstr );
    return hstr;
}

static BOOL str_eq( HSTRING hstr, const WCHAR *expected )
{
    const WCHAR *raw = WindowsGetStringRawBuffer( hstr, NULL );

    return raw && !lstrcmpW( raw, expected );
}

static IActivationFactory *get_factory( const WCHAR *class_name )
{
    IActivationFactory *factory = NULL;
    HSTRING hstr = str( class_name );
    HRESULT hr;

    hr = RoGetActivationFactory( hstr, &IID_IActivationFactory, (void **)&factory );
    WindowsDeleteString( hstr );
    if (FAILED( hr )) return NULL;
    return factory;
}

/* The module is a builtin of this tree; every namespace class must resolve. */
static void test_activation( void )
{
    static const WCHAR *classes[] =
    {
        RuntimeClass_Windows_ApplicationModel_DataTransfer_DataPackage,
        RuntimeClass_Windows_ApplicationModel_DataTransfer_DataPackageView,
        RuntimeClass_Windows_ApplicationModel_DataTransfer_DataPackagePropertySet,
        RuntimeClass_Windows_ApplicationModel_DataTransfer_DataPackagePropertySetView,
        RuntimeClass_Windows_ApplicationModel_DataTransfer_Clipboard,
        RuntimeClass_Windows_ApplicationModel_DataTransfer_StandardDataFormats,
        RuntimeClass_Windows_ApplicationModel_DataTransfer_DataTransferManager,
        RuntimeClass_Windows_ApplicationModel_DataTransfer_HtmlFormatHelper,
        RuntimeClass_Windows_ApplicationModel_DataTransfer_ShareProvider,
        RuntimeClass_Windows_ApplicationModel_DataTransfer_ShareUIOptions,
        RuntimeClass_Windows_ApplicationModel_DataTransfer_ClipboardContentOptions,
    };
    ULONG i;

    for (i = 0; i < ARRAY_SIZE( classes ); i++)
    {
        IActivationFactory *factory = get_factory( classes[i] );

        ok( !!factory, "no activation factory for %s.\n", debugstr_w( classes[i] ) );
        if (factory) IActivationFactory_Release( factory );
    }

    /* Activatable classes answer ActivateInstance; statics-only classes fail it. */
    {
        IActivationFactory *factory = get_factory( RuntimeClass_Windows_ApplicationModel_DataTransfer_DataPackage );
        IInspectable *obj = (void *)0xdeadbeef;

        ok( !!factory, "no DataPackage factory.\n" );
        if (factory)
        {
            check_hr( IActivationFactory_ActivateInstance( factory, &obj ), S_OK );
            ok( obj != (void *)0xdeadbeef, "no instance.\n" );
            IInspectable_Release( obj );
            IActivationFactory_Release( factory );
        }
    }
    {
        IActivationFactory *factory = get_factory( RuntimeClass_Windows_ApplicationModel_DataTransfer_DataPackageView );
        IInspectable *obj = (void *)0xdeadbeef;

        ok( !!factory, "no DataPackageView factory.\n" );
        if (factory)
        {
            check_hr( IActivationFactory_ActivateInstance( factory, &obj ), S_OK );
            IInspectable_Release( obj );
            IActivationFactory_Release( factory );
        }
    }
    {
        IActivationFactory *factory = get_factory( RuntimeClass_Windows_ApplicationModel_DataTransfer_DataPackagePropertySet );
        IInspectable *obj = (void *)0xdeadbeef;

        ok( !!factory, "no DataPackagePropertySet factory.\n" );
        if (factory)
        {
            check_hr( IActivationFactory_ActivateInstance( factory, &obj ), S_OK );
            IInspectable_Release( obj );
            IActivationFactory_Release( factory );
        }
    }
    {
        IActivationFactory *factory = get_factory( RuntimeClass_Windows_ApplicationModel_DataTransfer_DataPackagePropertySetView );
        IInspectable *obj = (void *)0xdeadbeef;

        ok( !!factory, "no DataPackagePropertySetView factory.\n" );
        if (factory)
        {
            check_hr( IActivationFactory_ActivateInstance( factory, &obj ), S_OK );
            IInspectable_Release( obj );
            IActivationFactory_Release( factory );
        }
    }
    {
        IActivationFactory *factory = get_factory( RuntimeClass_Windows_ApplicationModel_DataTransfer_Clipboard );

        ok( !!factory, "no Clipboard factory.\n" );
        if (factory)
        {
            IInspectable *obj = (void *)0xdeadbeef;
            check_hr( IActivationFactory_ActivateInstance( factory, &obj ), E_FAIL );
            ok( obj == (void *)0xdeadbeef, "instance handed out by a statics-only class.\n" );
            IActivationFactory_Release( factory );
        }
    }
}

/* A completed operation must report Completed, fire a late-completed handler
 * synchronously and hand out its result. */
static ULONG invoked_count;

static HRESULT WINAPI handler_QueryInterface( IAsyncOperationCompletedHandler_HSTRING *iface, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IAsyncOperationCompletedHandler_HSTRING ))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI handler_AddRef( IAsyncOperationCompletedHandler_HSTRING *iface ) { return 2; }
static ULONG WINAPI handler_Release( IAsyncOperationCompletedHandler_HSTRING *iface ) { return 1; }

static HRESULT WINAPI handler_Invoke( IAsyncOperationCompletedHandler_HSTRING *iface,
                                      IAsyncOperation_HSTRING *async, enum AsyncStatus status )
{
    invoked_count++;
    ok( status == Completed, "got status %u.\n", status );
    return S_OK;
}

static const struct IAsyncOperationCompletedHandler_HSTRINGVtbl handler_vtbl =
{
    handler_QueryInterface,
    handler_AddRef,
    handler_Release,
    handler_Invoke,
};

static IAsyncOperationCompletedHandler_HSTRING completed_handler = { &handler_vtbl };

static void check_string_op( IAsyncOperation_HSTRING *op, const WCHAR *expected )
{
    IAsyncOperationCompletedHandler_HSTRING *handler = (void *)0xdeadbeef;
    IAsyncInfo *info = (void *)0xdeadbeef;
    HSTRING result = NULL;
    enum AsyncStatus status = 0xdeadbeef;
    HRESULT code = 0xdeadbeef;
    HRESULT hr;

    hr = IAsyncOperation_HSTRING_QueryInterface( op, &IID_IAsyncInfo, (void **)&info );
    check_hr( hr, S_OK );
    hr = IAsyncInfo_get_Status( info, &status );
    check_hr( hr, S_OK );
    ok( status == Completed, "got status %u.\n", status );
    hr = IAsyncInfo_get_ErrorCode( info, &code );
    check_hr( hr, S_OK );
    ok( code == S_OK, "got error code %#lx.\n", code );
    hr = IAsyncInfo_Cancel( info );
    check_hr( hr, S_OK );
    hr = IAsyncInfo_Close( info );
    check_hr( hr, S_OK );
    IAsyncInfo_Release( info );

    invoked_count = 0;
    hr = IAsyncOperation_HSTRING_put_Completed( op, &completed_handler );
    check_hr( hr, S_OK );
    ok( invoked_count == 1, "got %lu handler invocations.\n", invoked_count );

    hr = IAsyncOperation_HSTRING_get_Completed( op, &handler );
    check_hr( hr, S_OK );
    ok( handler == &completed_handler, "got handler %p.\n", handler );
    IAsyncOperationCompletedHandler_HSTRING_Release( handler );

    hr = IAsyncOperation_HSTRING_QueryInterface( op, &IID_IAsyncOperation_HSTRING, (void **)&handler );
    check_hr( hr, S_OK );
    IUnknown_Release( (IUnknown *)handler );

    hr = IAsyncOperation_HSTRING_GetResults( op, &result );
    check_hr( hr, S_OK );
    ok( str_eq( result, expected ), "got result %s, expected %s.\n", debugstr_w( WindowsGetStringRawBuffer( result, NULL ) ),
        debugstr_w( expected ) );
    WindowsDeleteString( result );
}

/* Test-side IIterable<IStorageItem*> over one fake item (the module stores
 * the pointer as an opaque COM payload, the ABI contract is pointer
 * identity). */
static LONG fake_ref;

static HRESULT WINAPI fake_QueryInterface( IUnknown *iface, REFIID iid, void **out )
{
    *out = iface;
    return S_OK;
}

static ULONG WINAPI fake_AddRef( IUnknown *iface ) { return InterlockedIncrement( &fake_ref ); }
static ULONG WINAPI fake_Release( IUnknown *iface ) { return InterlockedDecrement( &fake_ref ); }

static const IUnknownVtbl fake_vtbl = { fake_QueryInterface, fake_AddRef, fake_Release };

static IUnknown fake_item = { &fake_vtbl };

static HRESULT WINAPI iter_QueryInterface( __FIIterator_1_Windows__CStorage__CIStorageItem *iface, REFIID iid, void **out )
{
    *out = iface;
    return S_OK;
}

static ULONG WINAPI iter_AddRef( __FIIterator_1_Windows__CStorage__CIStorageItem *iface ) { return 2; }
static ULONG WINAPI iter_Release( __FIIterator_1_Windows__CStorage__CIStorageItem *iface ) { return 1; }

static HRESULT WINAPI iter_GetIids( __FIIterator_1_Windows__CStorage__CIStorageItem *iface, ULONG *c, IID **i ) { return E_NOTIMPL; }

static HRESULT WINAPI iter_GetRuntimeClassName( __FIIterator_1_Windows__CStorage__CIStorageItem *iface, HSTRING *n ) { return E_NOTIMPL; }

static HRESULT WINAPI iter_GetTrustLevel( __FIIterator_1_Windows__CStorage__CIStorageItem *iface, TrustLevel *l ) { return E_NOTIMPL; }

static HRESULT WINAPI iter_get_Current( __FIIterator_1_Windows__CStorage__CIStorageItem *iface,
                                        __x_ABI_CWindows_CStorage_CIStorageItem **value )
{
    *value = (__x_ABI_CWindows_CStorage_CIStorageItem *)&fake_item;
    return S_OK;
}

static HRESULT WINAPI iter_get_HasCurrent( __FIIterator_1_Windows__CStorage__CIStorageItem *iface, boolean *value )
{
    *value = TRUE;
    return S_OK;
}

static HRESULT WINAPI iter_MoveNext( __FIIterator_1_Windows__CStorage__CIStorageItem *iface, boolean *value )
{
    *value = FALSE;
    return S_OK;
}

static HRESULT WINAPI iter_GetMany( __FIIterator_1_Windows__CStorage__CIStorageItem *iface, UINT32 cap,
                                    __x_ABI_CWindows_CStorage_CIStorageItem *items, UINT32 *count )
{
    *count = 0;
    return S_OK;
}

static const struct __FIIterator_1_Windows__CStorage__CIStorageItemVtbl iter_vtbl =
{
    iter_QueryInterface, iter_AddRef, iter_Release, iter_GetIids, iter_GetRuntimeClassName, iter_GetTrustLevel,
    iter_get_Current, iter_get_HasCurrent, iter_MoveNext, iter_GetMany,
};

static __FIIterator_1_Windows__CStorage__CIStorageItem test_iterator = { &iter_vtbl };

static HRESULT WINAPI iterable_QueryInterface( __FIIterable_1_Windows__CStorage__CIStorageItem *iface,
                                               REFIID iid, void **out )
{
    *out = iface;
    return S_OK;
}

static ULONG WINAPI iterable_AddRef( __FIIterable_1_Windows__CStorage__CIStorageItem *iface ) { return 2; }
static ULONG WINAPI iterable_Release( __FIIterable_1_Windows__CStorage__CIStorageItem *iface ) { return 1; }

static HRESULT WINAPI iterable_GetIids( __FIIterable_1_Windows__CStorage__CIStorageItem *iface, ULONG *c, IID **i ) { return E_NOTIMPL; }

static HRESULT WINAPI iterable_GetRuntimeClassName( __FIIterable_1_Windows__CStorage__CIStorageItem *iface, HSTRING *n ) { return E_NOTIMPL; }

static HRESULT WINAPI iterable_GetTrustLevel( __FIIterable_1_Windows__CStorage__CIStorageItem *iface, TrustLevel *l ) { return E_NOTIMPL; }

static HRESULT WINAPI iterable_First( __FIIterable_1_Windows__CStorage__CIStorageItem *iface,
                                      __FIIterator_1_Windows__CStorage__CIStorageItem **value )
{
    *value = &test_iterator;
    return S_OK;
}

static const struct __FIIterable_1_Windows__CStorage__CIStorageItemVtbl iterable_vtbl =
{
    iterable_QueryInterface, iterable_AddRef, iterable_Release, iterable_GetIids, iterable_GetRuntimeClassName,
    iterable_GetTrustLevel, iterable_First,
};

static __FIIterable_1_Windows__CStorage__CIStorageItem test_iterable = { &iterable_vtbl };

static void test_data_package( void )
{
    IDataPackageView2 *view2 = (void *)0xdeadbeef;
    IDataPackagePropertySet *props = (void *)0xdeadbeef;
    __FIVectorView_1_HSTRING *formats = (void *)0xdeadbeef;
    IAsyncOperation_HSTRING *op = (void *)0xdeadbeef;
    IDataPackageView *view = (void *)0xdeadbeef;
    IDataPackage2 *package2 = (void *)0xdeadbeef;
    IDataPackage *package = (void *)0xdeadbeef;
    IActivationFactory *factory;
    HSTRING hstr, fmt;
    DataPackageOperation requested;
    boolean found;
    UINT32 size;
    HRESULT hr;

    factory = get_factory( RuntimeClass_Windows_ApplicationModel_DataTransfer_DataPackage );
    ok( !!factory, "no DataPackage factory.\n" );
    if (!factory) return;
    check_hr( IActivationFactory_ActivateInstance( factory, (IInspectable **)&package ), S_OK );
    IActivationFactory_Release( factory );

    hr = IDataPackage_SetText( package, NULL );
    check_hr( hr, E_POINTER );

    hstr = str( test_text );
    hr = IDataPackage_SetText( package, hstr );
    check_hr( hr, S_OK );
    hr = IDataPackage_SetHtmlFormat( package, str( test_html ) );
    check_hr( hr, S_OK );

    hr = IDataPackage_put_RequestedOperation( package, DataPackageOperation_Link );
    check_hr( hr, S_OK );

    hr = IDataPackage_GetView( package, &view );
    check_hr( hr, S_OK );

    hr = IDataPackageView_get_RequestedOperation( view, &requested );
    check_hr( hr, S_OK );
    ok( requested == DataPackageOperation_Link, "got operation %#lx.\n", requested );

    hr = IDataPackageView_ReportOperationCompleted( view, DataPackageOperation_Copy );
    check_hr( hr, S_OK );

    /* Text round-trip through the born-completed operation. */
    hr = IDataPackageView_GetTextAsync( view, &op );
    check_hr( hr, S_OK );
    check_string_op( op, test_text );
    IAsyncOperation_HSTRING_Release( op );

    hr = IDataPackageView_GetHtmlFormatAsync( view, &op );
    check_hr( hr, S_OK );
    check_string_op( op, test_html );
    IAsyncOperation_HSTRING_Release( op );

    fmt = str( L"Text" );
    hr = IDataPackageView_Contains( view, fmt, &found );
    check_hr( hr, S_OK );
    ok( found, "Text not contained.\n" );
    fmt = str( L"Rtf" );
    hr = IDataPackageView_Contains( view, fmt, &found );
    check_hr( hr, S_OK );
    ok( !found, "Rtf unexpectedly contained.\n" );
    WindowsDeleteString( fmt );

    hr = IDataPackageView_get_AvailableFormats( view, &formats );
    check_hr( hr, S_OK );
    hr = __FIVectorView_1_HSTRING_get_Size( formats, &size );
    check_hr( hr, S_OK );
    ok( size == 2, "got %u formats.\n", size );

    /* Properties: the IMap contract (Insert/Lookup/HasKey/Remove/Clear) and
     * the Title field round-trip. */
    hr = IDataPackage_get_Properties( package, &props );
    check_hr( hr, S_OK );

    {
        __FIMap_2_HSTRING_IInspectable *map = (void *)0xdeadbeef;
        IInspectable *value = (void *)0xdeadbeef;
        HSTRING key = str( L"System.Title" );
        boolean replaced;

        hr = IDataPackagePropertySet_QueryInterface( props, &IID___FIMap_2_HSTRING_IInspectable, (void **)&map );
        check_hr( hr, S_OK );
        hr = __FIMap_2_HSTRING_IInspectable_Insert( map, key, (IInspectable *)package, &replaced );
        check_hr( hr, S_OK );
        ok( !replaced, "unexpected replace.\n" );
        hr = __FIMap_2_HSTRING_IInspectable_HasKey( map, key, &found );
        check_hr( hr, S_OK );
        ok( found, "key not found.\n" );
        hr = __FIMap_2_HSTRING_IInspectable_Lookup( map, key, &value );
        check_hr( hr, S_OK );
        ok( value == (IInspectable *)package, "got value %p.\n", value );
        IInspectable_Release( value );
        hr = __FIMap_2_HSTRING_IInspectable_get_Size( map, &size );
        check_hr( hr, S_OK );
        ok( size == 1, "got size %u.\n", size );
        hr = __FIMap_2_HSTRING_IInspectable_Remove( map, key );
        check_hr( hr, S_OK );
        hr = __FIMap_2_HSTRING_IInspectable_HasKey( map, key, &found );
        check_hr( hr, S_OK );
        ok( !found, "key survived Remove.\n" );

        hr = __FIMap_2_HSTRING_IInspectable_Insert( map, key, (IInspectable *)package, &replaced );
        check_hr( hr, S_OK );
        hr = __FIMap_2_HSTRING_IInspectable_Clear( map );
        check_hr( hr, S_OK );
        hr = __FIMap_2_HSTRING_IInspectable_get_Size( map, &size );
        check_hr( hr, S_OK );
        ok( size == 0, "got size %u after Clear.\n", size );

        WindowsDeleteString( key );
        __FIMap_2_HSTRING_IInspectable_Release( map );
    }

    {
        HSTRING title = str( L"share title" ), got = NULL;

        hr = IDataPackagePropertySet_put_Title( props, title );
        check_hr( hr, S_OK );
        hr = IDataPackagePropertySet_get_Title( props, &got );
        check_hr( hr, S_OK );
        ok( str_eq( got, L"share title" ), "got title %s.\n",
            debugstr_w( WindowsGetStringRawBuffer( got, NULL ) ) );
        WindowsDeleteString( got );
        WindowsDeleteString( title );
    }
    IDataPackagePropertySet_Release( props );

    /* Storage items: the iterable payload materializes into the view. */
    hr = IDataPackage_SetStorageItemsReadOnly( package, &test_iterable );
    check_hr( hr, S_OK );
    {
        __FIVectorView_1_Windows__CStorage__CIStorageItem *items = (void *)0xdeadbeef;
        IAsyncOperation_IVectorView_IStorageItem *items_op = (void *)0xdeadbeef;
        __x_ABI_CWindows_CStorage_CIStorageItem *item = (void *)0xdeadbeef;
        IAsyncInfo *info = (void *)0xdeadbeef;

        hr = IDataPackageView_GetStorageItemsAsync( view, &items_op );
        check_hr( hr, S_OK );
        hr = IAsyncOperation_IVectorView_IStorageItem_QueryInterface( items_op,
                                                                                              &IID_IAsyncInfo, (void **)&info );
        check_hr( hr, S_OK );
        {
            enum AsyncStatus status = 0xdeadbeef;
            hr = IAsyncInfo_get_Status( info, &status );
            check_hr( hr, S_OK );
            ok( status == Completed, "got status %u.\n", status );
        }
        IAsyncInfo_Release( info );
        hr = IAsyncOperation_IVectorView_IStorageItem_GetResults( items_op, &items );
        check_hr( hr, S_OK );
        hr = __FIVectorView_1_Windows__CStorage__CIStorageItem_get_Size( items, &size );
        check_hr( hr, S_OK );
        ok( size == 1, "got %u items.\n", size );
        hr = __FIVectorView_1_Windows__CStorage__CIStorageItem_GetAt( items, 0, &item );
        check_hr( hr, S_OK );
        ok( (IUnknown *)item == &fake_item, "got item %p.\n", item );
        IUnknown_Release( (IUnknown *)item );
        __FIVectorView_1_Windows__CStorage__CIStorageItem_Release( items );
        IAsyncOperation_IVectorView_IStorageItem_Release( items_op );
    }

    /* The Uri slot round-trips the pointer through the async getter. */
    {
        IAsyncOperation_Uri *uri_op = (void *)0xdeadbeef;
        __x_ABI_CWindows_CFoundation_CIUriRuntimeClass *uri = (void *)0xdeadbeef;

        hr = IDataPackageView_GetUriAsync( view, &uri_op );
        check_hr( hr, S_OK );
        hr = IAsyncOperation_Uri_GetResults( uri_op, &uri );
        check_hr( hr, S_OK );
        ok( uri == NULL, "got uri %p.\n", uri );
        IAsyncOperation_Uri_Release( uri_op );
    }

    hr = IDataPackageView_QueryInterface( view, &IID_IDataPackageView2, (void **)&view2 );
    check_hr( hr, S_OK );
    IDataPackageView2_Release( view2 );
    hr = IDataPackage_QueryInterface( package, &IID_IDataPackage2, (void **)&package2 );
    check_hr( hr, S_OK );
    IDataPackage2_Release( package2 );

    __FIVectorView_1_HSTRING_Release( formats );
    IDataPackageView_Release( view );
    WindowsDeleteString( hstr );
    IDataPackage_Release( package );
}

static void test_clipboard( void )
{
    IDataPackageView *view1 = (void *)0xdeadbeef, *view2 = (void *)0xdeadbeef;
    IClipboardStatics2 *statics2 = (void *)0xdeadbeef;
    IClipboardStatics *statics = (void *)0xdeadbeef;
    IDataPackage *package = (void *)0xdeadbeef;
    IActivationFactory *factory;
    EventRegistrationToken token = { 0 };
    boolean found;
    HRESULT hr;

    factory = get_factory( RuntimeClass_Windows_ApplicationModel_DataTransfer_Clipboard );
    ok( !!factory, "no Clipboard factory.\n" );
    if (!factory) return;
    hr = IActivationFactory_QueryInterface( factory, &IID_IClipboardStatics, (void **)&statics );
    check_hr( hr, S_OK );
    hr = IActivationFactory_QueryInterface( factory, &IID_IClipboardStatics2, (void **)&statics2 );
    check_hr( hr, S_OK );
    IActivationFactory_Release( factory );

    /* The initial content is an empty view: no formats, no text. */
    hr = IClipboardStatics_GetContent( statics, &view1 );
    check_hr( hr, S_OK );
    {
        __FIVectorView_1_HSTRING *formats = (void *)0xdeadbeef;
        UINT32 size = 0xdeadbeef;

        hr = IDataPackageView_get_AvailableFormats( view1, &formats );
        check_hr( hr, S_OK );
        hr = __FIVectorView_1_HSTRING_get_Size( formats, &size );
        check_hr( hr, S_OK );
        ok( size == 0, "got %u formats on an empty clipboard.\n", size );
        __FIVectorView_1_HSTRING_Release( formats );
    }

    /* GetContent hands out the cached view: same pointer every call. */
    hr = IClipboardStatics_GetContent( statics, &view2 );
    check_hr( hr, S_OK );
    ok( view1 == view2, "got views %p and %p.\n", view1, view2 );
    IDataPackageView_Release( view2 );
    IDataPackageView_Release( view1 );

    /* SetContent / GetContent identity: the view mirrors the package. */
    {
        IAsyncOperation_HSTRING *op = (void *)0xdeadbeef;
        HSTRING hstr = str( test_text );

        factory = get_factory( RuntimeClass_Windows_ApplicationModel_DataTransfer_DataPackage );
        check_hr( IActivationFactory_ActivateInstance( factory, (IInspectable **)&package ), S_OK );
        IActivationFactory_Release( factory );
        check_hr( IDataPackage_SetText( package, hstr ), S_OK );
        WindowsDeleteString( hstr );

        hr = IClipboardStatics_SetContent( statics, package );
        check_hr( hr, S_OK );
        hr = IClipboardStatics_Flush( statics );
        check_hr( hr, S_OK );

        hr = IClipboardStatics_GetContent( statics, &view1 );
        check_hr( hr, S_OK );
        hr = IDataPackageView_GetTextAsync( view1, &op );
        check_hr( hr, S_OK );
        check_string_op( op, test_text );
        IAsyncOperation_HSTRING_Release( op );

        {
            HSTRING fmt = str( L"Text" );
            hr = IDataPackageView_Contains( view1, fmt, &found );
            check_hr( hr, S_OK );
            ok( found, "Text not on the clipboard.\n" );
            WindowsDeleteString( fmt );
        }
    }

    hr = IClipboardStatics_add_ContentChanged( statics, NULL, &token );
    check_hr( hr, S_OK );
    ok( token.value != 0, "got token %s.\n", wine_dbgstr_longlong( token.value ) );
    hr = IClipboardStatics_remove_ContentChanged( statics, token );
    check_hr( hr, S_OK );

    /* History: completed, coherent, empty. */
    {
        IAsyncOperation_ClipboardHistoryItemsResult *op = (void *)0xdeadbeef;
        IClipboardHistoryItemsResult *result = (void *)0xdeadbeef;
        __FIVectorView_1_Windows__CApplicationModel__CDataTransfer__CClipboardHistoryItem *items = (void *)0xdeadbeef;
        ClipboardHistoryItemsResultStatus status = 0xdeadbeef;
        IAsyncInfo *info = (void *)0xdeadbeef;
        enum AsyncStatus op_status = 0xdeadbeef;
        UINT32 size = 0xdeadbeef;
        boolean enabled = FALSE;

        hr = IClipboardStatics2_GetHistoryItemsAsync( statics2, &op );
        check_hr( hr, S_OK );
        hr = IAsyncOperation_ClipboardHistoryItemsResult_QueryInterface(
                op, &IID_IAsyncInfo, (void **)&info );
        check_hr( hr, S_OK );
        hr = IAsyncInfo_get_Status( info, &op_status );
        check_hr( hr, S_OK );
        ok( op_status == Completed, "got status %u.\n", op_status );
        IAsyncInfo_Release( info );

        hr = IAsyncOperation_ClipboardHistoryItemsResult_GetResults(
                op, &result );
        check_hr( hr, S_OK );
        hr = IClipboardHistoryItemsResult_get_Status( result, &status );
        check_hr( hr, S_OK );
        hr = IClipboardHistoryItemsResult_get_Items( result, &items );
        check_hr( hr, S_OK );
        hr = __FIVectorView_1_Windows__CApplicationModel__CDataTransfer__CClipboardHistoryItem_get_Size( items, &size );
        check_hr( hr, S_OK );
        ok( size == 0, "got %u history items.\n", size );
        __FIVectorView_1_Windows__CApplicationModel__CDataTransfer__CClipboardHistoryItem_Release( items );
        IClipboardHistoryItemsResult_Release( result );
        IAsyncOperation_ClipboardHistoryItemsResult_Release( op );

        hr = IClipboardStatics2_ClearHistory( statics2, &enabled );
        check_hr( hr, S_OK );
        ok( enabled, "ClearHistory reported failure.\n" );
        hr = IClipboardStatics2_IsHistoryEnabled( statics2, &enabled );
        check_hr( hr, S_OK );
    }

    /* Clear empties the clipboard; the content view is replaced. */
    hr = IClipboardStatics_Clear( statics );
    check_hr( hr, S_OK );
    hr = IClipboardStatics_GetContent( statics, &view2 );
    check_hr( hr, S_OK );
    {
        HSTRING fmt = str( L"Text" );
        hr = IDataPackageView_Contains( view2, fmt, &found );
        check_hr( hr, S_OK );
        ok( !found, "Text survived Clear.\n" );
        WindowsDeleteString( fmt );
    }
    ok( view2 != view1, "the view was not replaced by Clear.\n" );
    IDataPackageView_Release( view2 );
    IDataPackageView_Release( view1 );
    IDataPackage_Release( package );

    IClipboardStatics2_Release( statics2 );
    IClipboardStatics_Release( statics );
}

static void check_format( IStandardDataFormatsStatics *statics,
                          HRESULT (STDMETHODCALLTYPE *getter)( IStandardDataFormatsStatics *, HSTRING * ),
                          const WCHAR *expected )
{
    HSTRING value = NULL;

    check_hr( getter( statics, NULL ), E_POINTER );
    check_hr( getter( statics, &value ), S_OK );
    ok( str_eq( value, expected ), "got format %s, expected %s.\n",
        debugstr_w( WindowsGetStringRawBuffer( value, NULL ) ), debugstr_w( expected ) );
    WindowsDeleteString( value );
}

static void test_standard_formats( void )
{
    IStandardDataFormatsStatics3 *statics3 = (void *)0xdeadbeef;
    IStandardDataFormatsStatics2 *statics2 = (void *)0xdeadbeef;
    IStandardDataFormatsStatics *statics = (void *)0xdeadbeef;
    IActivationFactory *factory;
    HRESULT hr;

    factory = get_factory( RuntimeClass_Windows_ApplicationModel_DataTransfer_StandardDataFormats );
    ok( !!factory, "no StandardDataFormats factory.\n" );
    if (!factory) return;
    hr = IActivationFactory_QueryInterface( factory, &IID_IStandardDataFormatsStatics, (void **)&statics );
    check_hr( hr, S_OK );
    hr = IActivationFactory_QueryInterface( factory, &IID_IStandardDataFormatsStatics2, (void **)&statics2 );
    check_hr( hr, S_OK );
    hr = IActivationFactory_QueryInterface( factory, &IID_IStandardDataFormatsStatics3, (void **)&statics3 );
    check_hr( hr, S_OK );
    IActivationFactory_Release( factory );

    check_format( statics, IStandardDataFormatsStatics_get_Text, L"Text" );
    check_format( statics, IStandardDataFormatsStatics_get_Html, L"Html" );
    check_format( statics, IStandardDataFormatsStatics_get_Uri, L"Uri" );
    check_format( statics, IStandardDataFormatsStatics_get_Rtf, L"Rtf" );
    check_format( statics, IStandardDataFormatsStatics_get_Bitmap, L"Bitmap" );
    check_format( statics, IStandardDataFormatsStatics_get_StorageItems, L"StorageItems" );
    check_format( statics2, IStandardDataFormatsStatics2_get_WebLink, L"WebLink" );
    check_format( statics2, IStandardDataFormatsStatics2_get_ApplicationLink, L"ApplicationLink" );
    check_format( statics3, IStandardDataFormatsStatics3_get_UserActivityJsonArray, L"UserActivityJsonArray" );

    IStandardDataFormatsStatics3_Release( statics3 );
    IStandardDataFormatsStatics2_Release( statics2 );
    IStandardDataFormatsStatics_Release( statics );
}

/* The canonical names without a statics getter (the XAML drag/drop set) key
 * the DataPackage format table through SetData/Contains. */
static void test_format_keys( void )
{
    static const WCHAR *keys[] = { L"Jpeg", L"Gif", L"Png", L"Json", L"XamlDocument", L"Custom" };
    IDataPackage *package = (void *)0xdeadbeef;
    IDataPackageView *view = (void *)0xdeadbeef;
    IActivationFactory *factory;
    ULONG i;

    factory = get_factory( RuntimeClass_Windows_ApplicationModel_DataTransfer_DataPackage );
    ok( !!factory, "no DataPackage factory.\n" );
    if (!factory) return;
    check_hr( IActivationFactory_ActivateInstance( factory, (IInspectable **)&package ), S_OK );
    IActivationFactory_Release( factory );

    for (i = 0; i < ARRAY_SIZE( keys ); i++)
    {
        HSTRING fmt = str( keys[i] );
        boolean found = FALSE;

        check_hr( IDataPackage_SetData( package, fmt, (IInspectable *)&fake_item ), S_OK );
        check_hr( IDataPackage_GetView( package, &view ), S_OK );
        check_hr( IDataPackageView_Contains( view, fmt, &found ), S_OK );
        ok( found, "%s not contained.\n", debugstr_w( keys[i] ) );
        IDataPackageView_Release( view );
        WindowsDeleteString( fmt );
    }

    /* Remove drops the format again: a null SetData payload removes. */
    {
        HSTRING fmt = str( L"Custom" );
        boolean found = TRUE;

        check_hr( IDataPackage_SetData( package, fmt, NULL ), S_OK );
        check_hr( IDataPackage_GetView( package, &view ), S_OK );
        check_hr( IDataPackageView_Contains( view, fmt, &found ), S_OK );
        ok( !found, "Custom survived removal.\n" );
        IDataPackageView_Release( view );
        WindowsDeleteString( fmt );
    }

    IDataPackage_Release( package );
}

static void test_share_surface( void )
{
    IHtmlFormatHelperStatics *helper = (void *)0xdeadbeef;
    IDataTransferManagerStatics2 *statics2 = (void *)0xdeadbeef;
    IDataTransferManagerStatics *statics = (void *)0xdeadbeef;
    IDataTransferManager *manager = (void *)0xdeadbeef;
    IDataTransferManagerInterop *interop = (void *)0xdeadbeef;
    IActivationFactory *factory;
    EventRegistrationToken token = { 0 };
    boolean supported = FALSE;
    HRESULT hr;

    factory = get_factory( RuntimeClass_Windows_ApplicationModel_DataTransfer_DataTransferManager );
    ok( !!factory, "no DataTransferManager factory.\n" );
    if (factory)
    {
        hr = IActivationFactory_QueryInterface( factory, &IID_IDataTransferManagerStatics, (void **)&statics );
        check_hr( hr, S_OK );
        hr = IActivationFactory_QueryInterface( factory, &IID_IDataTransferManagerStatics2, (void **)&statics2 );
        check_hr( hr, S_OK );
        hr = IActivationFactory_QueryInterface( factory, &IID_IDataTransferManagerInterop, (void **)&interop );
        check_hr( hr, S_OK );
        IActivationFactory_Release( factory );

        check_hr( IDataTransferManagerStatics_ShowShareUI( statics ), S_OK );
        check_hr( IDataTransferManagerStatics_GetForCurrentView( statics, &manager ), S_OK );
        check_hr( IDataTransferManager_add_DataRequested( manager, NULL, &token ), S_OK );
        ok( token.value != 0, "got token %s.\n", wine_dbgstr_longlong( token.value ) );
        check_hr( IDataTransferManager_remove_DataRequested( manager, token ), S_OK );
        IDataTransferManager_Release( manager );

        check_hr( IDataTransferManagerStatics2_IsSupported( statics2, &supported ), S_OK );
        ok( supported, "share reported unsupported.\n" );

        manager = (void *)0xdeadbeef;
        check_hr( IDataTransferManagerInterop_GetForWindow( interop, (HWND)0x1234567, &IID_IDataTransferManager,
                                                            (void **)&manager ), S_OK );
        ok( manager != (void *)0xdeadbeef, "no manager for the window.\n" );
        IDataTransferManager_Release( manager );
        check_hr( IDataTransferManagerInterop_ShowShareUIForWindow( interop, (HWND)0x1234567 ), S_OK );

        IDataTransferManagerInterop_Release( interop );
        IDataTransferManagerStatics2_Release( statics2 );
        IDataTransferManagerStatics_Release( statics );
    }

    factory = get_factory( RuntimeClass_Windows_ApplicationModel_DataTransfer_HtmlFormatHelper );
    ok( !!factory, "no HtmlFormatHelper factory.\n" );
    if (!factory) return;
    hr = IActivationFactory_QueryInterface( factory, &IID_IHtmlFormatHelperStatics, (void **)&helper );
    check_hr( hr, S_OK );
    IActivationFactory_Release( factory );

    {
        HSTRING fragment = str( L"<i>piece</i>" ), format = NULL, back = NULL;

        check_hr( IHtmlFormatHelperStatics_CreateHtmlFormat( helper, fragment, &format ), S_OK );
        ok( !!format, "no format.\n" );
        check_hr( IHtmlFormatHelperStatics_GetStaticFragment( helper, format, &back ), S_OK );
        ok( str_eq( back, L"<i>piece</i>" ), "got fragment %s.\n",
            debugstr_w( WindowsGetStringRawBuffer( back, NULL ) ) );
        WindowsDeleteString( back );
        WindowsDeleteString( format );
        WindowsDeleteString( fragment );
    }

    IHtmlFormatHelperStatics_Release( helper );
}

/* ------------------------------------------- DragDrop.Core CoreDragDropManager */

/* The TargetRequested delegate of the test: a refcounted object, so the test
 * sees the manager take the registration reference and give it back on
 * remove. */
static LONG handler_refs;
static ULONG target_requested_invocations;

static HRESULT WINAPI trhandler_QueryInterface( TargetRequestedHandler *iface, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_ITypedEventHandler_CoreDragDropManager_CoreDropOperationTargetRequestedEventArgs ))
    {
        *out = iface;
        InterlockedIncrement( &handler_refs );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI trhandler_AddRef( TargetRequestedHandler *iface ) { return InterlockedIncrement( &handler_refs ); }
static ULONG WINAPI trhandler_Release( TargetRequestedHandler *iface ) { return InterlockedDecrement( &handler_refs ); }

static HRESULT WINAPI trhandler_Invoke( TargetRequestedHandler *iface, ICoreDragDropManager *sender,
                                        ICoreDropOperationTargetRequestedEventArgs *args )
{
    target_requested_invocations++;
    return S_OK;
}

static const TargetRequestedHandlerVtbl trhandler_vtbl =
{
    trhandler_QueryInterface,
    trhandler_AddRef,
    trhandler_Release,
    trhandler_Invoke,
};

static TargetRequestedHandler target_requested_handler = { &trhandler_vtbl };

static BOOL iid_in_list( const IID *iids, ULONG count, const IID *iid )
{
    ULONG i;

    for (i = 0; i < count; i++)
        if (IsEqualGUID( &iids[i], iid )) return TRUE;
    return FALSE;
}

static void test_core_drag_drop( void )
{
    static const WCHAR core_ddm_class[] = L"Windows.ApplicationModel.DataTransfer.DragDrop.Core.CoreDragDropManager";
    ICoreDragDropManagerStatics *statics = NULL, *statics2 = NULL;
    IDragDropManagerInterop *interop = NULL;
    ICoreDragDropManager *manager = NULL, *manager2 = NULL;
    IActivationFactory *factory;
    IInspectable *instance = NULL;
    EventRegistrationToken token, token2, token3;
    TrustLevel trust = 0xdeadbeef;
    IID *iids = NULL;
    HSTRING name = NULL;
    ULONG count = 0;
    BOOLEAN enabled = FALSE;

    /* A foreign IID: nothing outside the declared interface set is answered. */
    static const IID foreign = { 0x12345678, 0x1234, 0x1234, { 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0 } };

    factory = get_factory( core_ddm_class );
    ok( !!factory, "no CoreDragDropManager factory.\n" );
    if (!factory) return;

    /* The projections reach the class through its statics and its window interop. */
    statics = (ICoreDragDropManagerStatics *)0xdeadbeef;
    check_hr( IActivationFactory_QueryInterface( factory, &IID_ICoreDragDropManagerStatics, (void **)&statics ), S_OK );
    ok( !!statics, "no ICoreDragDropManagerStatics.\n" );
    interop = (IDragDropManagerInterop *)0xdeadbeef;
    check_hr( IActivationFactory_QueryInterface( factory, &IID_IDragDropManagerInterop, (void **)&interop ), S_OK );
    ok( !!interop, "no IDragDropManagerInterop.\n" );

    /* RoActivateInstance hands out the manager, which answers the default interface. */
    check_hr( IActivationFactory_ActivateInstance( factory, &instance ), S_OK );
    ok( !!instance, "no instance.\n" );
    manager = (ICoreDragDropManager *)0xdeadbeef;
    check_hr( IInspectable_QueryInterface( instance, &IID_ICoreDragDropManager, (void **)&manager ), S_OK );
    ok( !!manager, "no ICoreDragDropManager on the instance.\n" );
    manager2 = (ICoreDragDropManager *)0xdeadbeef;
    check_hr( ICoreDragDropManager_QueryInterface( manager, &IID_ICoreDragDropManager, (void **)&manager2 ), S_OK );
    ok( (void *)manager2 == (void *)manager, "the default interface is not the instance.\n" );
    if (manager2) ICoreDragDropManager_Release( manager2 );

    /* GetForCurrentView answers S_OK with a usable manager. */
    manager2 = (ICoreDragDropManager *)0xdeadbeef;
    check_hr( ICoreDragDropManagerStatics_GetForCurrentView( statics, &manager2 ), S_OK );
    ok( !!manager2, "no manager for the current view.\n" );
    if (manager2)
    {
        check_hr( ICoreDragDropManager_get_AreConcurrentOperationsEnabled( manager2, &enabled ), S_OK );
        ICoreDragDropManager_Release( manager2 );
    }

    /* A foreign IID is refused on both the manager and the factory. */
    statics2 = (ICoreDragDropManagerStatics *)0xdeadbeef;
    check_hr( ICoreDragDropManager_QueryInterface( manager, &foreign, (void **)&statics2 ), E_NOINTERFACE );
    if (statics2) ICoreDragDropManagerStatics_Release( statics2 );
    manager2 = (ICoreDragDropManager *)0xdeadbeef;
    check_hr( IActivationFactory_QueryInterface( factory, &foreign, (void **)&manager2 ), E_NOINTERFACE );
    if (manager2) ICoreDragDropManager_Release( manager2 );

    /* The factory is not the manager: its default-interface IID is refused. */
    manager2 = (ICoreDragDropManager *)0xdeadbeef;
    check_hr( IActivationFactory_QueryInterface( factory, &IID_ICoreDragDropManager, (void **)&manager2 ),
              E_NOINTERFACE );
    if (manager2) ICoreDragDropManager_Release( manager2 );

    /* GetForWindow hands out the manager for the requested interface only. */
    manager2 = (ICoreDragDropManager *)0xdeadbeef;
    check_hr( IDragDropManagerInterop_GetForWindow( interop, (HWND)0x1234, &IID_ICoreDragDropManager,
                                                    (void **)&manager2 ), S_OK );
    ok( !!manager2, "no manager for the window.\n" );
    statics2 = (ICoreDragDropManagerStatics *)0xdeadbeef;
    check_hr( IDragDropManagerInterop_GetForWindow( interop, (HWND)0x1234, &IID_ICoreDragDropManagerStatics,
                                                    (void **)&statics2 ), E_NOINTERFACE );
    if (manager2) ICoreDragDropManager_Release( manager2 );
    if (statics2) ICoreDragDropManagerStatics_Release( statics2 );

    /* The event points: registration takes a reference and a nonzero token,
     * remove is idempotent and a null delegate is the documented E_POINTER. */
    handler_refs = 0;
    token.value = 0;
    check_hr( ICoreDragDropManager_add_TargetRequested( manager, &target_requested_handler, &token ), S_OK );
    ok( token.value != 0, "got token %s.\n", wine_dbgstr_longlong( token.value ) );
    ok( handler_refs == 1, "the manager does not hold the delegate reference (refs %ld).\n", handler_refs );
    token2.value = 0;
    check_hr( ICoreDragDropManager_add_TargetRequested( manager, &target_requested_handler, &token2 ), S_OK );
    ok( token2.value && token2.value != token.value, "token %s reused.\n", wine_dbgstr_longlong( token2.value ) );
    ok( handler_refs == 2, "the second registration is not held (refs %ld).\n", handler_refs );
    token3.value = -1;
    check_hr( ICoreDragDropManager_add_TargetRequested( manager, NULL, &token3 ), E_POINTER );
    ok( token3.value == -1, "a token was written for the null-delegate registration (%s).\n",
        wine_dbgstr_longlong( token3.value ) );
    check_hr( ICoreDragDropManager_remove_TargetRequested( manager, token ), S_OK );
    ok( handler_refs == 1, "remove did not release the delegate (refs %ld).\n", handler_refs );
    check_hr( ICoreDragDropManager_remove_TargetRequested( manager, token ), S_OK );
    ok( handler_refs == 1, "the repeated remove released twice (refs %ld).\n", handler_refs );
    token.value = 0;   /* the zero token was never issued: accepted as a no-op */
    check_hr( ICoreDragDropManager_remove_TargetRequested( manager, token ), S_OK );
    check_hr( ICoreDragDropManager_remove_TargetRequested( manager, token2 ), S_OK );
    ok( handler_refs == 0, "the last registration is not released (refs %ld).\n", handler_refs );

    /* AreConcurrentOperationsEnabled is the manager's own state. */
    check_hr( ICoreDragDropManager_get_AreConcurrentOperationsEnabled( manager, &enabled ), S_OK );
    ok( enabled, "concurrent operations disabled by default.\n" );
    check_hr( ICoreDragDropManager_put_AreConcurrentOperationsEnabled( manager, FALSE ), S_OK );
    check_hr( ICoreDragDropManager_get_AreConcurrentOperationsEnabled( manager, &enabled ), S_OK );
    ok( !enabled, "the property did not stick.\n" );

    /* The IInspectable surface names the class and lists the real interfaces. */
    check_hr( ICoreDragDropManager_GetRuntimeClassName( manager, &name ), S_OK );
    ok( str_eq( name, core_ddm_class ), "got class name %s.\n", debugstr_w( WindowsGetStringRawBuffer( name, NULL ) ) );
    WindowsDeleteString( name );
    name = NULL;
    check_hr( ICoreDragDropManager_GetTrustLevel( manager, &trust ), S_OK );
    ok( trust == 0, "got trust level %u.\n", trust );
    check_hr( ICoreDragDropManager_GetIids( manager, &count, &iids ), S_OK );
    ok( count >= 1 && !!iids, "got %lu iids.\n", count );
    if (iids)
    {
        ok( iid_in_list( iids, count, &IID_ICoreDragDropManager ), "GetIids omits the default interface.\n" );
        ok( iid_in_list( iids, count,
                         &IID_ITypedEventHandler_CoreDragDropManager_CoreDropOperationTargetRequestedEventArgs ),
            "GetIids omits the TargetRequested delegate interface.\n" );
        CoTaskMemFree( iids );
    }

    /* The manager stores the delegate; it has no drag session to raise it in,
     * so nothing may have been invoked behind the app's back. */
    ok( target_requested_invocations == 0, "got %lu invocations.\n", target_requested_invocations );

    ICoreDragDropManager_Release( manager );
    IInspectable_Release( instance );
    ICoreDragDropManagerStatics_Release( statics );
    IDragDropManagerInterop_Release( interop );
    IActivationFactory_Release( factory );
}

START_TEST( dataexchange )
{
    HRESULT hr;

    hr = RoInitialize( RO_INIT_MULTITHREADED );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    test_activation();
    test_data_package();
    test_clipboard();
    test_standard_formats();
    test_format_keys();
    test_share_surface();
    test_core_drag_drop();

    RoUninitialize();
}
