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
#include "robuffer.h"
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
                                    __x_ABI_CWindows_CStorage_CIStorageItem **items, UINT32 *count )
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

/* Test-side IStorageItem over a fixed path (the SetStorageItems payload of
 * the file round-trip: the module serializes get_Path() into CF_HDROP). */
struct t_item
{
    const struct __x_ABI_CWindows_CStorage_CIStorageItemVtbl *lpVtbl;
    const WCHAR *path;
};

static HRESULT WINAPI titem_QueryInterface( __x_ABI_CWindows_CStorage_CIStorageItem *iface, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IStorageItem ))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI titem_AddRef( __x_ABI_CWindows_CStorage_CIStorageItem *iface ) { return 2; }
static ULONG WINAPI titem_Release( __x_ABI_CWindows_CStorage_CIStorageItem *iface ) { return 1; }
static HRESULT WINAPI titem_GetIids( __x_ABI_CWindows_CStorage_CIStorageItem *iface, ULONG *c, IID **i ) { return E_NOTIMPL; }

static HRESULT WINAPI titem_GetRuntimeClassName( __x_ABI_CWindows_CStorage_CIStorageItem *iface, HSTRING *n )
{
    static const WCHAR cls[] = L"Windows.Storage.StorageFile";
    return WindowsCreateString( cls, ARRAY_SIZE( cls ) - 1, n );
}

static HRESULT WINAPI titem_GetTrustLevel( __x_ABI_CWindows_CStorage_CIStorageItem *iface, TrustLevel *l ) { return E_NOTIMPL; }
static HRESULT WINAPI titem_RenameAsyncOverload( __x_ABI_CWindows_CStorage_CIStorageItem *iface, HSTRING name,
                                                 __x_ABI_CWindows_CFoundation_CIAsyncAction **op ) { return E_NOTIMPL; }
static HRESULT WINAPI titem_RenameAsync( __x_ABI_CWindows_CStorage_CIStorageItem *iface, HSTRING name,
                                         enum __x_ABI_CWindows_CStorage_CNameCollisionOption o,
                                         __x_ABI_CWindows_CFoundation_CIAsyncAction **op2 ) { return E_NOTIMPL; }
static HRESULT WINAPI titem_DeleteAsyncOverload( __x_ABI_CWindows_CStorage_CIStorageItem *iface,
                                                 __x_ABI_CWindows_CFoundation_CIAsyncAction **op ) { return E_NOTIMPL; }
static HRESULT WINAPI titem_DeleteAsync( __x_ABI_CWindows_CStorage_CIStorageItem *iface,
                                         enum __x_ABI_CWindows_CStorage_CStorageDeleteOption o,
                                         __x_ABI_CWindows_CFoundation_CIAsyncAction **op2 ) { return E_NOTIMPL; }
static HRESULT WINAPI titem_GetBasicPropertiesAsync( __x_ABI_CWindows_CStorage_CIStorageItem *iface,
                                                     __FIAsyncOperation_1_Windows__CStorage__CFileProperties__CBasicProperties **op ) { return E_NOTIMPL; }

static HRESULT WINAPI titem_get_Name( __x_ABI_CWindows_CStorage_CIStorageItem *iface, HSTRING *value )
{
    struct t_item *impl = (struct t_item *)iface;
    const WCHAR *p = wcsrchr( impl->path, L'\\' );

    p = p ? p + 1 : impl->path;
    return WindowsCreateString( p, lstrlenW( p ), value );
}

static HRESULT WINAPI titem_get_Path( __x_ABI_CWindows_CStorage_CIStorageItem *iface, HSTRING *value )
{
    struct t_item *impl = (struct t_item *)iface;

    return WindowsCreateString( impl->path, lstrlenW( impl->path ), value );
}

static HRESULT WINAPI titem_get_Attributes( __x_ABI_CWindows_CStorage_CIStorageItem *iface,
                                            enum __x_ABI_CWindows_CStorage_CFileAttributes *value )
{
    *value = FileAttributes_Normal;
    return S_OK;
}

static HRESULT WINAPI titem_get_DateCreated( __x_ABI_CWindows_CStorage_CIStorageItem *iface,
                                             struct __x_ABI_CWindows_CFoundation_CDateTime *value ) { return E_NOTIMPL; }

static HRESULT WINAPI titem_IsOfType( __x_ABI_CWindows_CStorage_CIStorageItem *iface,
                                      enum __x_ABI_CWindows_CStorage_CStorageItemTypes type, boolean *value )
{
    *value = type == StorageItemTypes_File;
    return S_OK;
}

static const struct __x_ABI_CWindows_CStorage_CIStorageItemVtbl titem_vtbl =
{
    titem_QueryInterface, titem_AddRef, titem_Release, titem_GetIids, titem_GetRuntimeClassName, titem_GetTrustLevel,
    titem_RenameAsyncOverload, titem_RenameAsync, titem_DeleteAsyncOverload, titem_DeleteAsync,
    titem_GetBasicPropertiesAsync, titem_get_Name, titem_get_Path, titem_get_Attributes,
    titem_get_DateCreated, titem_IsOfType,
};

static struct t_item t_item_a = { &titem_vtbl, NULL };
static struct t_item t_item_b = { &titem_vtbl, NULL };
static ULONG titer_pos;

static HRESULT WINAPI titer_QueryInterface( __FIIterator_1_Windows__CStorage__CIStorageItem *iface, REFIID iid, void **out )
{
    *out = iface;
    return S_OK;
}
static ULONG WINAPI titer_AddRef( __FIIterator_1_Windows__CStorage__CIStorageItem *iface ) { return 2; }
static ULONG WINAPI titer_Release( __FIIterator_1_Windows__CStorage__CIStorageItem *iface ) { return 1; }
static HRESULT WINAPI titer_GetIids( __FIIterator_1_Windows__CStorage__CIStorageItem *iface, ULONG *c, IID **i ) { return E_NOTIMPL; }
static HRESULT WINAPI titer_GetRuntimeClassName( __FIIterator_1_Windows__CStorage__CIStorageItem *iface, HSTRING *n ) { return E_NOTIMPL; }
static HRESULT WINAPI titer_GetTrustLevel( __FIIterator_1_Windows__CStorage__CIStorageItem *iface, TrustLevel *l ) { return E_NOTIMPL; }

static HRESULT WINAPI titer_get_Current( __FIIterator_1_Windows__CStorage__CIStorageItem *iface,
                                         __x_ABI_CWindows_CStorage_CIStorageItem **value )
{
    *value = titer_pos == 0 ? (__x_ABI_CWindows_CStorage_CIStorageItem *)&t_item_a
                            : (__x_ABI_CWindows_CStorage_CIStorageItem *)&t_item_b;
    return S_OK;
}

static HRESULT WINAPI titer_get_HasCurrent( __FIIterator_1_Windows__CStorage__CIStorageItem *iface, boolean *value )
{
    *value = titer_pos < 2;
    return S_OK;
}

static HRESULT WINAPI titer_MoveNext( __FIIterator_1_Windows__CStorage__CIStorageItem *iface, boolean *value )
{
    titer_pos++;
    *value = titer_pos < 2;
    return S_OK;
}

static HRESULT WINAPI titer_GetMany( __FIIterator_1_Windows__CStorage__CIStorageItem *iface, UINT32 cap,
                                     __x_ABI_CWindows_CStorage_CIStorageItem **items, UINT32 *count )
{
    *count = 0;
    return S_OK;
}

static const struct __FIIterator_1_Windows__CStorage__CIStorageItemVtbl titer_vtbl =
{
    titer_QueryInterface, titer_AddRef, titer_Release, titer_GetIids, titer_GetRuntimeClassName, titer_GetTrustLevel,
    titer_get_Current, titer_get_HasCurrent, titer_MoveNext, titer_GetMany,
};

static __FIIterator_1_Windows__CStorage__CIStorageItem t_iterator = { &titer_vtbl };

static HRESULT WINAPI titerable_QueryInterface( __FIIterable_1_Windows__CStorage__CIStorageItem *iface,
                                                REFIID iid, void **out )
{
    *out = iface;
    return S_OK;
}
static ULONG WINAPI titerable_AddRef( __FIIterable_1_Windows__CStorage__CIStorageItem *iface ) { return 2; }
static ULONG WINAPI titerable_Release( __FIIterable_1_Windows__CStorage__CIStorageItem *iface ) { return 1; }
static HRESULT WINAPI titerable_GetIids( __FIIterable_1_Windows__CStorage__CIStorageItem *iface, ULONG *c, IID **i ) { return E_NOTIMPL; }
static HRESULT WINAPI titerable_GetRuntimeClassName( __FIIterable_1_Windows__CStorage__CIStorageItem *iface, HSTRING *n ) { return E_NOTIMPL; }
static HRESULT WINAPI titerable_GetTrustLevel( __FIIterable_1_Windows__CStorage__CIStorageItem *iface, TrustLevel *l ) { return E_NOTIMPL; }

static HRESULT WINAPI titerable_First( __FIIterable_1_Windows__CStorage__CIStorageItem *iface,
                                       __FIIterator_1_Windows__CStorage__CIStorageItem **value )
{
    titer_pos = 0;
    *value = &t_iterator;
    return S_OK;
}

static const struct __FIIterable_1_Windows__CStorage__CIStorageItemVtbl titerable_vtbl =
{
    titerable_QueryInterface, titerable_AddRef, titerable_Release, titerable_GetIids, titerable_GetRuntimeClassName,
    titerable_GetTrustLevel, titerable_First,
};

static __FIIterable_1_Windows__CStorage__CIStorageItem t_iterable = { &titerable_vtbl };

/* The CF_HDROP header (include/shlobj.h:1750), declared locally. */
typedef struct
{
    DWORD pFiles;
    POINT pt;
    BOOL  fNC;
    BOOL  fWide;
} t_dropfiles;

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
    ok( requested == DataPackageOperation_Link, "got operation %#lx.\n", (unsigned long)requested );

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
    check_format( statics, IStandardDataFormatsStatics_get_StorageItems, L"Shell IDList Array" );
    check_format( (IStandardDataFormatsStatics *)statics2,
                  (HRESULT (STDMETHODCALLTYPE *)( IStandardDataFormatsStatics *, HSTRING * ))IStandardDataFormatsStatics2_get_WebLink,
                  L"WebLink" );
    check_format( (IStandardDataFormatsStatics *)statics2,
                  (HRESULT (STDMETHODCALLTYPE *)( IStandardDataFormatsStatics *, HSTRING * ))IStandardDataFormatsStatics2_get_ApplicationLink,
                  L"ApplicationLink" );
    check_format( (IStandardDataFormatsStatics *)statics3,
                  (HRESULT (STDMETHODCALLTYPE *)( IStandardDataFormatsStatics *, HSTRING * ))IStandardDataFormatsStatics3_get_UserActivityJsonArray,
                  L"UserActivityJsonArray" );

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

static LONG cc_refs;
static LONG cc_invocations;
static HANDLE cc_event;

static HRESULT WINAPI cc_handler_QueryInterface( __FIEventHandler_1_IInspectable *iface, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID___FIEventHandler_1_IInspectable ))
    {
        *out = iface;
        InterlockedIncrement( &cc_refs );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI cc_handler_AddRef( __FIEventHandler_1_IInspectable *iface ) { return InterlockedIncrement( &cc_refs ); }
static ULONG WINAPI cc_handler_Release( __FIEventHandler_1_IInspectable *iface ) { return InterlockedDecrement( &cc_refs ); }

static HRESULT WINAPI cc_handler_Invoke( __FIEventHandler_1_IInspectable *iface, IInspectable *sender, IInspectable *args )
{
    InterlockedIncrement( &cc_invocations );
    if (cc_event) SetEvent( cc_event );
    return S_OK;
}

static const __FIEventHandler_1_IInspectableVtbl cc_handler_vtbl =
{
    cc_handler_QueryInterface,
    cc_handler_AddRef,
    cc_handler_Release,
    cc_handler_Invoke,
};

static __FIEventHandler_1_IInspectable cc_handler = { &cc_handler_vtbl };

static __x_ABI_CWindows_CStorage_CStreams_CIRandomAccessStreamReference *create_test_bitmap_ref( void )
{
    static const WCHAR stream_cls[] = L"Windows.Storage.Streams.InMemoryRandomAccessStream";
    static const WCHAR buf_cls[] = L"Windows.Storage.Streams.Buffer";
    static const WCHAR ref_cls[] = L"Windows.Storage.Streams.RandomAccessStreamReference";
    __x_ABI_CWindows_CStorage_CStreams_CIRandomAccessStream *stream = NULL;
    __x_ABI_CWindows_CStorage_CStreams_CIOutputStream *output = NULL;
    __x_ABI_CWindows_CStorage_CStreams_CIBufferFactory *buf_factory = NULL;
    __x_ABI_CWindows_CStorage_CStreams_CIBuffer *buf = NULL;
    IBufferByteAccess *access = NULL;
    __FIAsyncOperationWithProgress_2_UINT32_UINT32 *write_op = NULL;
    __x_ABI_CWindows_CStorage_CStreams_CIRandomAccessStreamReferenceStatics *ref_statics = NULL;
    __x_ABI_CWindows_CStorage_CStreams_CIRandomAccessStreamReference *ref = NULL;
    BYTE bmp[sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + 4 * 4 * 4];
    BITMAPFILEHEADER *bfh = (BITMAPFILEHEADER *)bmp;
    BITMAPINFOHEADER *bih = (BITMAPINFOHEADER *)(bmp + sizeof(BITMAPFILEHEADER));
    DWORD *pixels = (DWORD *)(bmp + sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER));
    BYTE *dst = NULL;
    HSTRING hstr;
    UINT32 written = 0, i;
    HRESULT hr;

    memset( bmp, 0, sizeof(bmp) );
    bfh->bfType = 0x4D42;
    bfh->bfSize = sizeof(bmp);
    bfh->bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    bih->biSize = sizeof(BITMAPINFOHEADER);
    bih->biWidth = 4;
    bih->biHeight = 4;
    bih->biPlanes = 1;
    bih->biBitCount = 32;
    bih->biCompression = BI_RGB;
    for (i = 0; i < 16; i++) pixels[i] = 0xFF112233;

    hstr = str( stream_cls );
    hr = RoActivateInstance( hstr, (IInspectable **)&stream );
    WindowsDeleteString( hstr );
    check_hr( hr, S_OK );
    if (FAILED( hr )) return NULL;

    hstr = str( buf_cls );
    hr = RoGetActivationFactory( hstr, &IID___x_ABI_CWindows_CStorage_CStreams_CIBufferFactory, (void **)&buf_factory );
    WindowsDeleteString( hstr );
    check_hr( hr, S_OK );
    hr = __x_ABI_CWindows_CStorage_CStreams_CIBufferFactory_Create( buf_factory, sizeof(bmp), &buf );
    check_hr( hr, S_OK );
    __x_ABI_CWindows_CStorage_CStreams_CIBufferFactory_Release( buf_factory );

    hr = __x_ABI_CWindows_CStorage_CStreams_CIBuffer_QueryInterface( buf, &IID_IBufferByteAccess, (void **)&access );
    check_hr( hr, S_OK );
    hr = IBufferByteAccess_Buffer( access, &dst );
    check_hr( hr, S_OK );
    memcpy( dst, bmp, sizeof(bmp) );
    hr = __x_ABI_CWindows_CStorage_CStreams_CIBuffer_put_Length( buf, sizeof(bmp) );
    check_hr( hr, S_OK );
    IBufferByteAccess_Release( access );

    hr = __x_ABI_CWindows_CStorage_CStreams_CIRandomAccessStream_QueryInterface(
            stream, &IID___x_ABI_CWindows_CStorage_CStreams_CIOutputStream, (void **)&output );
    check_hr( hr, S_OK );
    hr = __x_ABI_CWindows_CStorage_CStreams_CIOutputStream_WriteAsync( output, buf, &write_op );
    check_hr( hr, S_OK );
    __FIAsyncOperationWithProgress_2_UINT32_UINT32_GetResults( write_op, &written );
    ok( written == sizeof(bmp), "wrote %u bytes.\n", written );
    __FIAsyncOperationWithProgress_2_UINT32_UINT32_Release( write_op );
    __x_ABI_CWindows_CStorage_CStreams_CIOutputStream_Release( output );
    __x_ABI_CWindows_CStorage_CStreams_CIBuffer_Release( buf );

    hr = __x_ABI_CWindows_CStorage_CStreams_CIRandomAccessStream_Seek( stream, 0 );
    check_hr( hr, S_OK );

    hstr = str( ref_cls );
    hr = RoGetActivationFactory( hstr, &IID___x_ABI_CWindows_CStorage_CStreams_CIRandomAccessStreamReferenceStatics,
                                 (void **)&ref_statics );
    WindowsDeleteString( hstr );
    check_hr( hr, S_OK );
    hr = __x_ABI_CWindows_CStorage_CStreams_CIRandomAccessStreamReferenceStatics_CreateFromStream( ref_statics, stream, &ref );
    check_hr( hr, S_OK );
    __x_ABI_CWindows_CStorage_CStreams_CIRandomAccessStreamReferenceStatics_Release( ref_statics );
    __x_ABI_CWindows_CStorage_CStreams_CIRandomAccessStream_Release( stream );
    return ref;
}

static void check_bitmap_ref_valid( __x_ABI_CWindows_CStorage_CStreams_CIRandomAccessStreamReference *ref )
{
    static const WCHAR buf_cls[] = L"Windows.Storage.Streams.Buffer";
    __FIAsyncOperation_1_Windows__CStorage__CStreams__CIRandomAccessStreamWithContentType *open_op = NULL;
    __x_ABI_CWindows_CStorage_CStreams_CIRandomAccessStreamWithContentType *wct = NULL;
    __x_ABI_CWindows_CStorage_CStreams_CIRandomAccessStream *stream = NULL;
    __x_ABI_CWindows_CStorage_CStreams_CIInputStream *input = NULL;
    __x_ABI_CWindows_CStorage_CStreams_CIBufferFactory *buf_factory = NULL;
    __x_ABI_CWindows_CStorage_CStreams_CIBuffer *buf = NULL, *read_buf = NULL;
    __FIAsyncOperationWithProgress_2_Windows__CStorage__CStreams__CIBuffer_UINT32 *read_op = NULL;
    IBufferByteAccess *access = NULL;
    UINT64 sz = 0;
    UINT32 len = 0;
    BYTE *bytes = NULL;
    HSTRING hstr;
    HRESULT hr;

    ok( !!ref, "bitmap ref is NULL.\n" );
    if (!ref) return;

    hr = __x_ABI_CWindows_CStorage_CStreams_CIRandomAccessStreamReference_OpenReadAsync( ref, &open_op );
    check_hr( hr, S_OK );
    hr = __FIAsyncOperation_1_Windows__CStorage__CStreams__CIRandomAccessStreamWithContentType_GetResults( open_op, &wct );
    check_hr( hr, S_OK );
    __FIAsyncOperation_1_Windows__CStorage__CStreams__CIRandomAccessStreamWithContentType_Release( open_op );

    hr = __x_ABI_CWindows_CStorage_CStreams_CIRandomAccessStreamWithContentType_QueryInterface(
            wct, &IID___x_ABI_CWindows_CStorage_CStreams_CIRandomAccessStream, (void **)&stream );
    check_hr( hr, S_OK );
    __x_ABI_CWindows_CStorage_CStreams_CIRandomAccessStreamWithContentType_Release( wct );

    hr = __x_ABI_CWindows_CStorage_CStreams_CIRandomAccessStream_get_Size( stream, &sz );
    check_hr( hr, S_OK );
    ok( sz >= sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER), "got bitmap stream size %I64u.\n", sz );
    hr = __x_ABI_CWindows_CStorage_CStreams_CIRandomAccessStream_Seek( stream, 0 );
    check_hr( hr, S_OK );
    hr = __x_ABI_CWindows_CStorage_CStreams_CIRandomAccessStream_QueryInterface(
            stream, &IID___x_ABI_CWindows_CStorage_CStreams_CIInputStream, (void **)&input );
    check_hr( hr, S_OK );
    __x_ABI_CWindows_CStorage_CStreams_CIRandomAccessStream_Release( stream );

    hstr = str( buf_cls );
    hr = RoGetActivationFactory( hstr, &IID___x_ABI_CWindows_CStorage_CStreams_CIBufferFactory, (void **)&buf_factory );
    WindowsDeleteString( hstr );
    check_hr( hr, S_OK );
    hr = __x_ABI_CWindows_CStorage_CStreams_CIBufferFactory_Create( buf_factory, (UINT32)sz, &buf );
    check_hr( hr, S_OK );
    __x_ABI_CWindows_CStorage_CStreams_CIBufferFactory_Release( buf_factory );

    hr = __x_ABI_CWindows_CStorage_CStreams_CIInputStream_ReadAsync( input, buf, (UINT32)sz, InputStreamOptions_None, &read_op );
    check_hr( hr, S_OK );
    hr = __FIAsyncOperationWithProgress_2_Windows__CStorage__CStreams__CIBuffer_UINT32_GetResults( read_op, &read_buf );
    check_hr( hr, S_OK );
    __FIAsyncOperationWithProgress_2_Windows__CStorage__CStreams__CIBuffer_UINT32_Release( read_op );
    __x_ABI_CWindows_CStorage_CStreams_CIInputStream_Release( input );

    hr = __x_ABI_CWindows_CStorage_CStreams_CIBuffer_get_Length( read_buf, &len );
    check_hr( hr, S_OK );
    hr = __x_ABI_CWindows_CStorage_CStreams_CIBuffer_QueryInterface( read_buf, &IID_IBufferByteAccess, (void **)&access );
    check_hr( hr, S_OK );
    hr = IBufferByteAccess_Buffer( access, &bytes );
    check_hr( hr, S_OK );
    ok( len >= sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER), "got %u bitmap bytes.\n", len );
    if (len >= sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER))
    {
        const BITMAPFILEHEADER *bfh = (const BITMAPFILEHEADER *)bytes;
        const BITMAPINFOHEADER *bih = (const BITMAPINFOHEADER *)(bytes + sizeof(BITMAPFILEHEADER));
        ok( bfh->bfType == 0x4D42, "got bfType %#x.\n", bfh->bfType );
        ok( bih->biWidth == 4 && abs( bih->biHeight ) == 4, "got %ldx%ld.\n", bih->biWidth, bih->biHeight );
    }
    IBufferByteAccess_Release( access );
    __x_ABI_CWindows_CStorage_CStreams_CIBuffer_Release( read_buf );
    __x_ABI_CWindows_CStorage_CStreams_CIBuffer_Release( buf );
}

/* Verify a Bitmap stream reference carries our 4x4 32bpp BMP payload:
 * full 'BM' file, image/bmp content type and the exact pixel dwords. */
static void check_bitmap_ref_dib( __x_ABI_CWindows_CStorage_CStreams_CIRandomAccessStreamReference *ref,
                                  DWORD expected_pixel )
{
    static const WCHAR buf_cls[] = L"Windows.Storage.Streams.Buffer";
    static const WCHAR bmp_type[] = L"image/bmp";
    __FIAsyncOperation_1_Windows__CStorage__CStreams__CIRandomAccessStreamWithContentType *open_op = NULL;
    __x_ABI_CWindows_CStorage_CStreams_CIRandomAccessStreamWithContentType *wct = NULL;
    __x_ABI_CWindows_CStorage_CStreams_CIRandomAccessStream *stream = NULL;
    __x_ABI_CWindows_CStorage_CStreams_CIInputStream *input = NULL;
    __x_ABI_CWindows_CStorage_CStreams_CIBufferFactory *buf_factory = NULL;
    __x_ABI_CWindows_CStorage_CStreams_CIBuffer *buf = NULL, *read_buf = NULL;
    __FIAsyncOperationWithProgress_2_Windows__CStorage__CStreams__CIBuffer_UINT32 *read_op = NULL;
    IBufferByteAccess *access = NULL;
    UINT64 sz = 0;
    UINT32 len = 0, i, bad = 0;
    BYTE *bytes = NULL;
    HSTRING hstr, ctype = NULL;
    HRESULT hr;

    ok( !!ref, "bitmap ref is NULL.\n" );
    if (!ref) return;

    hr = __x_ABI_CWindows_CStorage_CStreams_CIRandomAccessStreamReference_OpenReadAsync( ref, &open_op );
    check_hr( hr, S_OK );
    hr = __FIAsyncOperation_1_Windows__CStorage__CStreams__CIRandomAccessStreamWithContentType_GetResults( open_op, &wct );
    check_hr( hr, S_OK );
    __FIAsyncOperation_1_Windows__CStorage__CStreams__CIRandomAccessStreamWithContentType_Release( open_op );
    if (FAILED( hr ) || !wct) return;

    /* Win11 C projection: get_ContentType sits at vtable slot 15 on the
     * RandomAccessStreamWithContentType face (the tree's C header models the
     * interface as bare IInspectable, so call the slot directly). */
    hr = ((HRESULT (STDMETHODCALLTYPE *)(void *, HSTRING *))*((void **)wct->lpVtbl + 15))( wct, &ctype );
    check_hr( hr, S_OK );
    ok( str_eq( ctype, bmp_type ), "got content type %s, expected image/bmp.\n",
        debugstr_w( WindowsGetStringRawBuffer( ctype, NULL ) ) );
    WindowsDeleteString( ctype );

    hr = __x_ABI_CWindows_CStorage_CStreams_CIRandomAccessStreamWithContentType_QueryInterface(
            wct, &IID___x_ABI_CWindows_CStorage_CStreams_CIRandomAccessStream, (void **)&stream );
    check_hr( hr, S_OK );
    __x_ABI_CWindows_CStorage_CStreams_CIRandomAccessStreamWithContentType_Release( wct );
    if (FAILED( hr )) return;

    hr = __x_ABI_CWindows_CStorage_CStreams_CIRandomAccessStream_get_Size( stream, &sz );
    check_hr( hr, S_OK );
    hr = __x_ABI_CWindows_CStorage_CStreams_CIRandomAccessStream_Seek( stream, 0 );
    check_hr( hr, S_OK );
    hr = __x_ABI_CWindows_CStorage_CStreams_CIRandomAccessStream_QueryInterface(
            stream, &IID___x_ABI_CWindows_CStorage_CStreams_CIInputStream, (void **)&input );
    check_hr( hr, S_OK );
    __x_ABI_CWindows_CStorage_CStreams_CIRandomAccessStream_Release( stream );
    if (FAILED( hr )) return;

    hstr = str( buf_cls );
    hr = RoGetActivationFactory( hstr, &IID___x_ABI_CWindows_CStorage_CStreams_CIBufferFactory, (void **)&buf_factory );
    WindowsDeleteString( hstr );
    check_hr( hr, S_OK );
    hr = __x_ABI_CWindows_CStorage_CStreams_CIBufferFactory_Create( buf_factory, (UINT32)sz, &buf );
    check_hr( hr, S_OK );
    __x_ABI_CWindows_CStorage_CStreams_CIBufferFactory_Release( buf_factory );

    hr = __x_ABI_CWindows_CStorage_CStreams_CIInputStream_ReadAsync( input, buf, (UINT32)sz, InputStreamOptions_None, &read_op );
    check_hr( hr, S_OK );
    hr = __FIAsyncOperationWithProgress_2_Windows__CStorage__CStreams__CIBuffer_UINT32_GetResults( read_op, &read_buf );
    check_hr( hr, S_OK );
    __FIAsyncOperationWithProgress_2_Windows__CStorage__CStreams__CIBuffer_UINT32_Release( read_op );
    __x_ABI_CWindows_CStorage_CStreams_CIInputStream_Release( input );

    hr = __x_ABI_CWindows_CStorage_CStreams_CIBuffer_get_Length( read_buf, &len );
    check_hr( hr, S_OK );
    hr = __x_ABI_CWindows_CStorage_CStreams_CIBuffer_QueryInterface( read_buf, &IID_IBufferByteAccess, (void **)&access );
    check_hr( hr, S_OK );
    hr = IBufferByteAccess_Buffer( access, &bytes );
    check_hr( hr, S_OK );
    ok( len == sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + 4 * 4 * 4, "got %u bitmap bytes.\n", len );
    if (len >= sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + 4 * 4 * 4)
    {
        const BITMAPFILEHEADER *bfh = (const BITMAPFILEHEADER *)bytes;
        const BITMAPINFOHEADER *bih = (const BITMAPINFOHEADER *)(bytes + sizeof(BITMAPFILEHEADER));
        const DWORD *px = (const DWORD *)(bih + 1);

        ok( bfh->bfType == 0x4D42, "got bfType %#x.\n", bfh->bfType );
        ok( bih->biSize == sizeof(*bih) && bih->biWidth == 4 && abs( bih->biHeight ) == 4 &&
            bih->biBitCount == 32 && bih->biCompression == BI_RGB,
            "got header size %lu, %ldx%ld, %u bpp, compression %#lx.\n",
            bih->biSize, bih->biWidth, bih->biHeight, bih->biBitCount, bih->biCompression );
        for (i = 0; i < 16; i++)
            if (px[i] != expected_pixel) bad++;
        ok( !bad, "%u of 16 pixels differ from %#lx.\n", bad, expected_pixel );
    }
    IBufferByteAccess_Release( access );
    __x_ABI_CWindows_CStorage_CStreams_CIBuffer_Release( read_buf );
    __x_ABI_CWindows_CStorage_CStreams_CIBuffer_Release( buf );
}

static void test_clipboard_roundtrip( void )
{
    static const WCHAR wa_text[] = L"hello wa";
    static const WCHAR wa_html[] = L"<b>hello wa</b>";
    IClipboardStatics *statics = NULL;
    IHtmlFormatHelperStatics *helper = NULL;
    IDataPackage *package = NULL;
    IDataPackageView *view = NULL;
    IDataObject *data_obj = NULL;
    IActivationFactory *factory;
    __x_ABI_CWindows_CStorage_CStreams_CIRandomAccessStreamReference *bmp_ref = NULL, *got_bmp = NULL;
    __FIAsyncOperation_1_Windows__CStorage__CStreams__CRandomAccessStreamReference *bmp_op = NULL;
    IAsyncOperation_HSTRING *op = NULL;
    EventRegistrationToken cc_token = { 0 };
    HSTRING hstr, html_res = NULL, frag = NULL;
    const WCHAR *raw;
    boolean found = FALSE;
    DWORD wait_res;
    HRESULT hr;

    factory = get_factory( RuntimeClass_Windows_ApplicationModel_DataTransfer_Clipboard );
    ok( !!factory, "no Clipboard factory.\n" );
    if (!factory) return;
    check_hr( IActivationFactory_QueryInterface( factory, &IID_IClipboardStatics, (void **)&statics ), S_OK );
    IActivationFactory_Release( factory );

    factory = get_factory( RuntimeClass_Windows_ApplicationModel_DataTransfer_HtmlFormatHelper );
    ok( !!factory, "no HtmlFormatHelper factory.\n" );
    check_hr( IActivationFactory_QueryInterface( factory, &IID_IHtmlFormatHelperStatics, (void **)&helper ), S_OK );
    IActivationFactory_Release( factory );

    cc_refs = 0;
    cc_invocations = 0;
    cc_event = CreateEventW( NULL, FALSE, FALSE, NULL );
    hr = IClipboardStatics_add_ContentChanged( statics, &cc_handler, &cc_token );
    check_hr( hr, S_OK );
    ok( cc_token.value != 0, "got zero token.\n" );
    ok( cc_refs >= 1, "handler ref not taken.\n" );

    factory = get_factory( RuntimeClass_Windows_ApplicationModel_DataTransfer_DataPackage );
    check_hr( IActivationFactory_ActivateInstance( factory, (IInspectable **)&package ), S_OK );
    IActivationFactory_Release( factory );

    hstr = str( wa_text );
    check_hr( IDataPackage_SetText( package, hstr ), S_OK );
    WindowsDeleteString( hstr );

    hstr = str( wa_html );
    check_hr( IDataPackage_SetHtmlFormat( package, hstr ), S_OK );
    WindowsDeleteString( hstr );

    bmp_ref = create_test_bitmap_ref();
    ok( !!bmp_ref, "failed to create test bitmap ref.\n" );
    if (bmp_ref)
    {
        check_hr( IDataPackage_SetBitmap( package, bmp_ref ), S_OK );
        __x_ABI_CWindows_CStorage_CStreams_CIRandomAccessStreamReference_Release( bmp_ref );
    }

    /* SetContent + Flush must trigger WM_CLIPBOARDUPDATE -> ContentChanged and persist formats. */
    ResetEvent( cc_event );
    check_hr( IClipboardStatics_SetContent( statics, package ), S_OK );
    check_hr( IClipboardStatics_Flush( statics ), S_OK );

    wait_res = WaitForSingleObject( cc_event, 3000 );
    ok( wait_res == WAIT_OBJECT_0, "ContentChanged did not fire after SetContent/Flush (wait %lu).\n", wait_res );
    ok( cc_invocations >= 1, "expected ContentChanged invocations >= 1, got %ld.\n", cc_invocations );

    check_hr( IClipboardStatics_GetContent( statics, &view ), S_OK );

    /* DataPackageView must QI for IDataObject. */
    hr = IDataPackageView_QueryInterface( view, &IID_IDataObject, (void **)&data_obj );
    check_hr( hr, S_OK );
    ok( !!data_obj, "no IDataObject on DataPackageView.\n" );

    hstr = str( L"Text" );
    check_hr( IDataPackageView_Contains( view, hstr, &found ), S_OK );
    ok( found, "Text missing from clipboard view.\n" );
    WindowsDeleteString( hstr );

    hstr = str( L"Html" );
    check_hr( IDataPackageView_Contains( view, hstr, &found ), S_OK );
    ok( found, "Html missing from clipboard view.\n" );
    WindowsDeleteString( hstr );

    hstr = str( L"Bitmap" );
    check_hr( IDataPackageView_Contains( view, hstr, &found ), S_OK );
    ok( found, "Bitmap missing from clipboard view.\n" );
    WindowsDeleteString( hstr );

    check_hr( IDataPackageView_GetTextAsync( view, &op ), S_OK );
    check_string_op( op, wa_text );
    IAsyncOperation_HSTRING_Release( op );

    check_hr( IDataPackageView_GetHtmlFormatAsync( view, &op ), S_OK );
    check_hr( IAsyncOperation_HSTRING_GetResults( op, &html_res ), S_OK );
    IAsyncOperation_HSTRING_Release( op );
    raw = WindowsGetStringRawBuffer( html_res, NULL );
    ok( raw && wcsstr( raw, L"<!--StartFragment-->" ) != NULL, "missing <!--StartFragment--> in %s.\n", debugstr_w( raw ) );
    ok( raw && wcsstr( raw, wa_html ) != NULL, "missing fragment in %s.\n", debugstr_w( raw ) );
    check_hr( IHtmlFormatHelperStatics_GetStaticFragment( helper, html_res, &frag ), S_OK );
    ok( str_eq( frag, wa_html ), "got fragment %s, expected %s.\n",
        debugstr_w( WindowsGetStringRawBuffer( frag, NULL ) ), debugstr_w( wa_html ) );
    WindowsDeleteString( frag );
    WindowsDeleteString( html_res );

    check_hr( IDataPackageView_GetBitmapAsync( view, &bmp_op ), S_OK );
    check_hr( __FIAsyncOperation_1_Windows__CStorage__CStreams__CRandomAccessStreamReference_GetResults( bmp_op, &got_bmp ), S_OK );
    __FIAsyncOperation_1_Windows__CStorage__CStreams__CRandomAccessStreamReference_Release( bmp_op );
    check_bitmap_ref_valid( got_bmp );
    if (got_bmp) __x_ABI_CWindows_CStorage_CStreams_CIRandomAccessStreamReference_Release( got_bmp );

    /* Verify direct IDataObject reads from the clipboard view. */
    if (data_obj)
    {
        FORMATETC fmt_u = { CF_UNICODETEXT, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
        FORMATETC fmt_b = { CF_BITMAP, NULL, DVASPECT_CONTENT, -1, TYMED_GDI };
        STGMEDIUM med = { 0 };

        hr = IDataObject_GetData( data_obj, &fmt_u, &med );
        check_hr( hr, S_OK );
        if (SUCCEEDED( hr ))
        {
            const WCHAR *p = GlobalLock( med.hGlobal );
            ok( p && !wcscmp( p, wa_text ), "IDataObject CF_UNICODETEXT got %s.\n", debugstr_w( p ) );
            GlobalUnlock( med.hGlobal );
            ReleaseStgMedium( &med );
        }

        memset( &med, 0, sizeof(med) );
        hr = IDataObject_GetData( data_obj, &fmt_b, &med );
        check_hr( hr, S_OK );
        if (SUCCEEDED( hr ))
        {
            ok( med.tymed == TYMED_GDI && med.hBitmap != NULL, "IDataObject CF_BITMAP missing.\n" );
            ReleaseStgMedium( &med );
        }

        /* CF_DIB: the BMP stream payload minus the 14-byte file header. */
        {
            FORMATETC fmt_dib = { CF_DIB, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
            const BITMAPINFOHEADER *bih;
            const DWORD *px;
            SIZE_T sz;

            memset( &med, 0, sizeof(med) );
            hr = IDataObject_GetData( data_obj, &fmt_dib, &med );
            check_hr( hr, S_OK );
            if (SUCCEEDED( hr ) && med.tymed == TYMED_HGLOBAL)
            {
                sz = GlobalSize( med.hGlobal );
                bih = GlobalLock( med.hGlobal );
                ok( sz == sizeof(*bih) + 4 * 4 * 4, "CF_DIB is %lu bytes.\n", (unsigned long)sz );
                if (bih && sz == sizeof(*bih) + 4 * 4 * 4)
                {
                    px = (const DWORD *)(bih + 1);
                    ok( bih->biSize == sizeof(*bih) && bih->biWidth == 4 && bih->biBitCount == 32,
                        "CF_DIB header %lu, %ldx%ld, %u bpp.\n", bih->biSize, bih->biWidth, bih->biHeight, bih->biBitCount );
                    ok( px[0] == 0xFF112233 && px[15] == 0xFF112233, "CF_DIB pixels %#lx/%#lx.\n", px[0], px[15] );
                }
                GlobalUnlock( med.hGlobal );
            }
            ReleaseStgMedium( &med );
        }
        IDataObject_Release( data_obj );
    }

    IDataPackageView_Release( view );
    IDataPackage_Release( package );

    /* Win32 SetClipboardData -> ContentChanged + GetContent live clipboard read. */
    {
        static const WCHAR win32_txt[] = L"win32 hello";
        static const char win32_html[] =
            "Version:0.9\r\nStartHTML:0000000097\r\nEndHTML:0000000174\r\n"
            "StartFragment:0000000131\r\nEndFragment:0000000143\r\n"
            "<html><body>\r\n<!--StartFragment--><i>win32</i><!--EndFragment-->\r\n</body></html>";
        UINT cf_html = RegisterClipboardFormatW( L"HTML Format" );
        HGLOBAL hg_txt, hg_html, hg_dib;
        HBITMAP hbm;
        BYTE *bits = NULL;
        BITMAPINFO bmi = { { sizeof(BITMAPINFOHEADER), 4, 4, 1, 32, BI_RGB } };
        HDC hdc;
        LONG prev_cc = cc_invocations;

        ResetEvent( cc_event );
        ok( OpenClipboard( NULL ), "OpenClipboard failed.\n" );
        EmptyClipboard();
        hg_txt = GlobalAlloc( GMEM_MOVEABLE | GMEM_DDESHARE, sizeof(win32_txt) );
        memcpy( GlobalLock( hg_txt ), win32_txt, sizeof(win32_txt) );
        GlobalUnlock( hg_txt );
        SetClipboardData( CF_UNICODETEXT, hg_txt );

        hg_html = GlobalAlloc( GMEM_MOVEABLE | GMEM_DDESHARE, sizeof(win32_html) );
        memcpy( GlobalLock( hg_html ), win32_html, sizeof(win32_html) );
        GlobalUnlock( hg_html );
        SetClipboardData( cf_html, hg_html );

        hdc = GetDC( NULL );
        hbm = CreateDIBSection( hdc, &bmi, DIB_RGB_COLORS, (void **)&bits, NULL, 0 );
        ReleaseDC( NULL, hdc );
        if (bits) memset( bits, 0x7f, 4 * 4 * 4 );
        SetClipboardData( CF_BITMAP, hbm );

        hg_dib = GlobalAlloc( GMEM_MOVEABLE | GMEM_DDESHARE, sizeof(BITMAPINFOHEADER) + 4 * 4 * 4 );
        {
            BYTE *p = GlobalLock( hg_dib );
            memcpy( p, &bmi.bmiHeader, sizeof(BITMAPINFOHEADER) );
            memset( p + sizeof(BITMAPINFOHEADER), 0x7f, 4 * 4 * 4 );
            GlobalUnlock( hg_dib );
        }
        SetClipboardData( CF_DIB, hg_dib );
        CloseClipboard();

        wait_res = WaitForSingleObject( cc_event, 3000 );
        ok( wait_res == WAIT_OBJECT_0, "ContentChanged did not fire on Win32 SetClipboardData (wait %lu).\n", wait_res );
        ok( cc_invocations > prev_cc, "ContentChanged count did not increment.\n" );

        check_hr( IClipboardStatics_GetContent( statics, &view ), S_OK );
        check_hr( IDataPackageView_GetTextAsync( view, &op ), S_OK );
        check_string_op( op, win32_txt );
        IAsyncOperation_HSTRING_Release( op );

        check_hr( IDataPackageView_GetHtmlFormatAsync( view, &op ), S_OK );
        check_hr( IAsyncOperation_HSTRING_GetResults( op, &html_res ), S_OK );
        IAsyncOperation_HSTRING_Release( op );
        raw = WindowsGetStringRawBuffer( html_res, NULL );
        ok( raw && wcsstr( raw, L"<i>win32</i>" ) != NULL, "missing win32 html in %s.\n", debugstr_w( raw ) );
        WindowsDeleteString( html_res );

        check_hr( IDataPackageView_GetBitmapAsync( view, &bmp_op ), S_OK );
        check_hr( __FIAsyncOperation_1_Windows__CStorage__CStreams__CRandomAccessStreamReference_GetResults( bmp_op, &got_bmp ), S_OK );
        __FIAsyncOperation_1_Windows__CStorage__CStreams__CRandomAccessStreamReference_Release( bmp_op );
        check_bitmap_ref_dib( got_bmp, 0x7f7f7f7f );
        if (got_bmp) __x_ABI_CWindows_CStorage_CStreams_CIRandomAccessStreamReference_Release( got_bmp );
        IDataPackageView_Release( view );
    }

    /* Win32 CF_DIB-only -> WinRT: the view must build the BMP stream from the
     * raw DIB bytes (BITMAPFILEHEADER prefix, image/bmp, exact pixels). */
    {
        HGLOBAL hg_dib;

        ResetEvent( cc_event );
        ok( OpenClipboard( NULL ), "OpenClipboard failed.\n" );
        EmptyClipboard();
        hg_dib = GlobalAlloc( GMEM_MOVEABLE | GMEM_DDESHARE, sizeof(BITMAPINFOHEADER) + 4 * 4 * 4 );
        {
            BYTE *p = GlobalLock( hg_dib );
            BITMAPINFOHEADER bmih = { sizeof(bmih), 4, 4, 1, 32, BI_RGB };
            memcpy( p, &bmih, sizeof(bmih) );
            memset( p + sizeof(bmih), 0xab, 4 * 4 * 4 );
            GlobalUnlock( hg_dib );
        }
        SetClipboardData( CF_DIB, hg_dib );
        CloseClipboard();

        wait_res = WaitForSingleObject( cc_event, 3000 );
        ok( wait_res == WAIT_OBJECT_0, "ContentChanged did not fire on CF_DIB set (wait %lu).\n", wait_res );

        check_hr( IClipboardStatics_GetContent( statics, &view ), S_OK );
        hstr = str( L"Bitmap" );
        check_hr( IDataPackageView_Contains( view, hstr, &found ), S_OK );
        ok( found, "Bitmap missing from CF_DIB-only clipboard view.\n" );
        WindowsDeleteString( hstr );

        check_hr( IDataPackageView_GetBitmapAsync( view, &bmp_op ), S_OK );
        check_hr( __FIAsyncOperation_1_Windows__CStorage__CStreams__CRandomAccessStreamReference_GetResults( bmp_op, &got_bmp ), S_OK );
        __FIAsyncOperation_1_Windows__CStorage__CStreams__CRandomAccessStreamReference_Release( bmp_op );
        check_bitmap_ref_dib( got_bmp, 0xabababab );
        if (got_bmp) __x_ABI_CWindows_CStorage_CStreams_CIRandomAccessStreamReference_Release( got_bmp );
        IDataPackageView_Release( view );
    }

    /* WinRT DataPackage::SetBitmap -> Clipboard::SetContent -> Win32 CF_DIB. */
    {
        FORMATETC fmt_dib = { CF_DIB, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
        STGMEDIUM med = { 0 };

        ResetEvent( cc_event );
        factory = get_factory( RuntimeClass_Windows_ApplicationModel_DataTransfer_DataPackage );
        check_hr( IActivationFactory_ActivateInstance( factory, (IInspectable **)&package ), S_OK );
        IActivationFactory_Release( factory );

        bmp_ref = create_test_bitmap_ref();
        ok( !!bmp_ref, "failed to create test bitmap ref.\n" );
        check_hr( IDataPackage_SetBitmap( package, bmp_ref ), S_OK );
        __x_ABI_CWindows_CStorage_CStreams_CIRandomAccessStreamReference_Release( bmp_ref );

        check_hr( IClipboardStatics_SetContent( statics, package ), S_OK );
        check_hr( IClipboardStatics_Flush( statics ), S_OK );
        wait_res = WaitForSingleObject( cc_event, 3000 );
        ok( wait_res == WAIT_OBJECT_0, "ContentChanged did not fire after bitmap SetContent/Flush (wait %lu).\n", wait_res );
        IDataPackage_Release( package );

        ok( OpenClipboard( NULL ), "OpenClipboard failed.\n" );
        ok( IsClipboardFormatAvailable( CF_DIB ), "CF_DIB not offered by the WinRT clipboard push.\n" );
        ok( IsClipboardFormatAvailable( CF_BITMAP ), "CF_BITMAP not offered by the WinRT clipboard push.\n" );
        {
            HGLOBAL hg = GetClipboardData( CF_DIB );
            ok( !!hg, "GetClipboardData(CF_DIB) returned NULL.\n" );
            if (hg)
            {
                const BITMAPINFOHEADER *bih = GlobalLock( hg );
                SIZE_T sz = GlobalSize( hg );
                ok( sz == sizeof(*bih) + 4 * 4 * 4, "Win32 CF_DIB is %lu bytes.\n", (unsigned long)sz );
                if (bih && sz == sizeof(*bih) + 4 * 4 * 4)
                {
                    const DWORD *px = (const DWORD *)(bih + 1);
                    ok( bih->biSize == sizeof(*bih) && bih->biWidth == 4 && abs( bih->biHeight ) == 4 &&
                        bih->biBitCount == 32 && bih->biCompression == BI_RGB,
                        "Win32 CF_DIB header %lu, %ldx%ld, %u bpp, compression %#lx.\n",
                        bih->biSize, bih->biWidth, bih->biHeight, bih->biBitCount, bih->biCompression );
                    ok( px[0] == 0xFF112233 && px[15] == 0xFF112233, "Win32 CF_DIB pixels %#lx/%#lx.\n", px[0], px[15] );
                }
                GlobalUnlock( hg );
            }
        }
        CloseClipboard();

        /* And through the view's IDataObject, exercising the same GetData path. */
        check_hr( IClipboardStatics_GetContent( statics, &view ), S_OK );
        hr = IDataPackageView_QueryInterface( view, &IID_IDataObject, (void **)&data_obj );
        check_hr( hr, S_OK );
        if (data_obj)
        {
            hr = IDataObject_GetData( data_obj, &fmt_dib, &med );
            check_hr( hr, S_OK );
            if (SUCCEEDED( hr ) && med.tymed == TYMED_HGLOBAL)
            {
                const BITMAPINFOHEADER *bih = GlobalLock( med.hGlobal );
                ok( bih && bih->biWidth == 4, "view CF_DIB header missing.\n" );
                GlobalUnlock( med.hGlobal );
            }
            ReleaseStgMedium( &med );
            IDataObject_Release( data_obj );
        }
        IDataPackageView_Release( view );
    }

    check_hr( IClipboardStatics_remove_ContentChanged( statics, cc_token ), S_OK );
    ok( cc_refs == 0, "handler ref leaked (%ld).\n", cc_refs );
    check_hr( IClipboardStatics_Clear( statics ), S_OK );

    CloseHandle( cc_event );
    cc_event = NULL;
    IHtmlFormatHelperStatics_Release( helper );
    IClipboardStatics_Release( statics );
}

/* Win32 CF_HDROP <-> WinRT StorageItems both directions: the wide and ANSI
 * DROPFILES payloads, the StandardDataFormats::StorageItems name, the
 * IStorageItem path/name/attributes, and the SetStorageItems -> SetContent ->
 * GetClipboardData(CF_HDROP) serialization. */
static HGLOBAL make_hdrop_wide( const WCHAR *p1, const WCHAR *p2 )
{
    SIZE_T l1 = lstrlenW( p1 ), l2 = p2 ? lstrlenW( p2 ) : 0;
    SIZE_T size = sizeof(t_dropfiles) + (l1 + 1 + l2 + 1) * sizeof(WCHAR);
    t_dropfiles *df;
    WCHAR *dst;
    HGLOBAL h;

    if (!(h = GlobalAlloc( GMEM_MOVEABLE | GMEM_DDESHARE, size ))) return NULL;
    df = GlobalLock( h );
    df->pFiles = sizeof(t_dropfiles);
    df->pt.x = df->pt.y = 0;
    df->fNC = FALSE;
    df->fWide = TRUE;
    dst = (WCHAR *)(df + 1);
    memcpy( dst, p1, (l1 + 1) * sizeof(WCHAR) );
    dst += l1 + 1;
    if (p2)
    {
        memcpy( dst, p2, (l2 + 1) * sizeof(WCHAR) );
        dst += l2 + 1;
    }
    *dst = 0;
    GlobalUnlock( h );
    return h;
}

static HGLOBAL make_hdrop_ansi( const WCHAR *p1 )
{
    int alen = WideCharToMultiByte( CP_ACP, 0, p1, -1, NULL, 0, NULL, NULL );
    SIZE_T size = sizeof(t_dropfiles) + alen + 1;
    t_dropfiles *df;
    char *dst;
    HGLOBAL h;

    if (!(h = GlobalAlloc( GMEM_MOVEABLE | GMEM_DDESHARE, size ))) return NULL;
    df = GlobalLock( h );
    df->pFiles = sizeof(t_dropfiles);
    df->pt.x = df->pt.y = 0;
    df->fNC = FALSE;
    df->fWide = FALSE;
    dst = (char *)(df + 1);
    WideCharToMultiByte( CP_ACP, 0, p1, -1, dst, alen, NULL, NULL );
    dst[alen] = '\0';
    GlobalUnlock( h );
    return h;
}

/* One IStorageItem from GetStorageItemsAsync: path, name, attributes, type. */
static void check_storage_item( __x_ABI_CWindows_CStorage_CIStorageItem *item,
                                const WCHAR *path, const WCHAR *name, BOOL is_dir )
{
    enum __x_ABI_CWindows_CStorage_CFileAttributes got_attrs = 0xdeadbeef;
    enum __x_ABI_CWindows_CStorage_CStorageItemTypes type;
    IStorageItem2 *item2 = NULL;
    HSTRING hstr = NULL;
    boolean is_type = FALSE;
    HRESULT hr;

    hr = __x_ABI_CWindows_CStorage_CIStorageItem_QueryInterface( item, &IID_IStorageItem2, (void **)&item2 );
    check_hr( hr, E_NOINTERFACE );
    if (item2) IStorageItem2_Release( item2 );

    hr = __x_ABI_CWindows_CStorage_CIStorageItem_get_Path( item, &hstr );
    check_hr( hr, S_OK );
    ok( str_eq( hstr, path ), "got path %s, expected %s.\n",
        debugstr_w( WindowsGetStringRawBuffer( hstr, NULL ) ), debugstr_w( path ) );
    WindowsDeleteString( hstr );

    hstr = NULL;
    hr = __x_ABI_CWindows_CStorage_CIStorageItem_get_Name( item, &hstr );
    check_hr( hr, S_OK );
    ok( str_eq( hstr, name ), "got name %s, expected %s.\n",
        debugstr_w( WindowsGetStringRawBuffer( hstr, NULL ) ), debugstr_w( name ) );
    WindowsDeleteString( hstr );

    hr = __x_ABI_CWindows_CStorage_CIStorageItem_get_Attributes( item, &got_attrs );
    check_hr( hr, S_OK );
    if (is_dir) ok( (got_attrs & FileAttributes_Directory) != 0, "got attributes %#lx.\n", (unsigned long)got_attrs );
    else ok( !(got_attrs & FileAttributes_Directory) && (got_attrs & (FileAttributes_Normal | FileAttributes_Archive)),
             "got attributes %#lx.\n", (unsigned long)got_attrs );

    type = StorageItemTypes_File;
    hr = __x_ABI_CWindows_CStorage_CIStorageItem_IsOfType( item, type, &is_type );
    check_hr( hr, S_OK );
    ok( is_type == !is_dir, "IsOfType(File) = %u.\n", is_type );
    is_type = FALSE;
    type = StorageItemTypes_Folder;
    hr = __x_ABI_CWindows_CStorage_CIStorageItem_IsOfType( item, type, &is_type );
    check_hr( hr, S_OK );
    ok( is_type == !!is_dir, "IsOfType(Folder) = %u.\n", is_type );
}

static void test_clipboard_files( void )
{
    static const WCHAR file_name[] = L"wine-de-file-a.txt";
    static const WCHAR dir_name[] = L"wine-de-dir-b";
    __FIVectorView_1_Windows__CStorage__CIStorageItem *items = NULL;
    IAsyncOperation_IVectorView_IStorageItem *items_op = NULL;
    __x_ABI_CWindows_CStorage_CIStorageItem *item = NULL;
    IStandardDataFormatsStatics *fmt_statics = NULL;
    __FIVectorView_1_HSTRING *formats = NULL;
    IDataPackage *package = NULL;
    IDataPackageView *view = NULL;
    IClipboardStatics *statics = NULL;
    IActivationFactory *factory;
    HSTRING path_h = NULL, fmt_name = NULL;
    WCHAR path_a[MAX_PATH], path_dir[MAX_PATH], base[MAX_PATH];
    HANDLE file;
    boolean found = FALSE;
    UINT32 size = 0;
    HRESULT hr;

    factory = get_factory( RuntimeClass_Windows_ApplicationModel_DataTransfer_Clipboard );
    ok( !!factory, "no Clipboard factory.\n" );
    if (!factory) return;
    check_hr( IActivationFactory_QueryInterface( factory, &IID_IClipboardStatics, (void **)&statics ), S_OK );
    IActivationFactory_Release( factory );

    factory = get_factory( RuntimeClass_Windows_ApplicationModel_DataTransfer_StandardDataFormats );
    ok( !!factory, "no StandardDataFormats factory.\n" );
    check_hr( IActivationFactory_QueryInterface( factory, &IID_IStandardDataFormatsStatics, (void **)&fmt_statics ), S_OK );
    IActivationFactory_Release( factory );
    check_hr( IStandardDataFormatsStatics_get_StorageItems( fmt_statics, &fmt_name ), S_OK );
    ok( str_eq( fmt_name, L"Shell IDList Array" ), "got StorageItems name %s.\n",
        debugstr_w( WindowsGetStringRawBuffer( fmt_name, NULL ) ) );

    GetTempPathW( ARRAY_SIZE( base ), base );
    lstrcpynW( path_a, base, ARRAY_SIZE( path_a ) );
    lstrcatW( path_a, file_name );
    lstrcpynW( path_dir, base, ARRAY_SIZE( path_dir ) );
    lstrcatW( path_dir, dir_name );
    file = CreateFileW( path_a, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL );
    ok( file != INVALID_HANDLE_VALUE, "CreateFileW(%s) failed.\n", debugstr_w( path_a ) );
    CloseHandle( file );
    CreateDirectoryW( path_dir, NULL );

    /* Win32 SetClipboardData(CF_HDROP) -> StorageItems, wide paths, in order. */
    {
        HGLOBAL hg = make_hdrop_wide( path_a, path_dir );

        ok( OpenClipboard( NULL ), "OpenClipboard failed.\n" );
        EmptyClipboard();
        ok( !!SetClipboardData( CF_HDROP, hg ), "SetClipboardData(CF_HDROP) failed.\n" );
        CloseClipboard();

        check_hr( IClipboardStatics_GetContent( statics, &view ), S_OK );
        check_hr( IDataPackageView_Contains( view, fmt_name, &found ), S_OK );
        ok( found, "StorageItems missing from the CF_HDROP clipboard view.\n" );

        hr = IDataPackageView_get_AvailableFormats( view, &formats );
        check_hr( hr, S_OK );
        {
            HSTRING name = NULL;

            hr = __FIVectorView_1_HSTRING_get_Size( formats, &size );
            check_hr( hr, S_OK );
            ok( size == 1, "got %u formats for the CF_HDROP clipboard.\n", size );
            hr = __FIVectorView_1_HSTRING_GetAt( formats, 0, &name );
            check_hr( hr, S_OK );
            ok( str_eq( name, L"Shell IDList Array" ), "AvailableFormats[0] = %s.\n",
                debugstr_w( WindowsGetStringRawBuffer( name, NULL ) ) );
            WindowsDeleteString( name );
        }
        __FIVectorView_1_HSTRING_Release( formats );

        check_hr( IDataPackageView_GetStorageItemsAsync( view, &items_op ), S_OK );
        check_hr( IAsyncOperation_IVectorView_IStorageItem_GetResults( items_op, &items ), S_OK );
        check_hr( __FIVectorView_1_Windows__CStorage__CIStorageItem_get_Size( items, &size ), S_OK );
        ok( size == 2, "got %u storage items.\n", size );

        check_hr( __FIVectorView_1_Windows__CStorage__CIStorageItem_GetAt( items, 0, &item ), S_OK );
        check_storage_item( item, path_a, file_name, FALSE );
        __x_ABI_CWindows_CStorage_CIStorageItem_Release( item );

        item = NULL;
        check_hr( __FIVectorView_1_Windows__CStorage__CIStorageItem_GetAt( items, 1, &item ), S_OK );
        check_storage_item( item, path_dir, dir_name, TRUE );
        __x_ABI_CWindows_CStorage_CIStorageItem_Release( item );

        __FIVectorView_1_Windows__CStorage__CIStorageItem_Release( items );
        IAsyncOperation_IVectorView_IStorageItem_Release( items_op );
        IDataPackageView_Release( view );
    }

    /* ANSI DROPFILES (fWide = FALSE): the same single path comes back wide. */
    {
        HGLOBAL hg = make_hdrop_ansi( path_a );

        ok( OpenClipboard( NULL ), "OpenClipboard failed.\n" );
        EmptyClipboard();
        ok( !!SetClipboardData( CF_HDROP, hg ), "SetClipboardData(CF_HDROP) failed.\n" );
        CloseClipboard();

        check_hr( IClipboardStatics_GetContent( statics, &view ), S_OK );
        check_hr( IDataPackageView_GetStorageItemsAsync( view, &items_op ), S_OK );
        check_hr( IAsyncOperation_IVectorView_IStorageItem_GetResults( items_op, &items ), S_OK );
        check_hr( __FIVectorView_1_Windows__CStorage__CIStorageItem_get_Size( items, &size ), S_OK );
        ok( size == 1, "got %u storage items for the ANSI drop list.\n", size );
        check_hr( __FIVectorView_1_Windows__CStorage__CIStorageItem_GetAt( items, 0, &item ), S_OK );
        check_storage_item( item, path_a, file_name, FALSE );
        __x_ABI_CWindows_CStorage_CIStorageItem_Release( item );
        __FIVectorView_1_Windows__CStorage__CIStorageItem_Release( items );
        IAsyncOperation_IVectorView_IStorageItem_Release( items_op );
        IDataPackageView_Release( view );
    }

    /* WinRT SetStorageItems -> SetContent/Flush -> Win32 CF_HDROP round-trip. */
    {
        t_dropfiles *df;
        SIZE_T size_bytes;
        HGLOBAL hg;

        t_item_a.path = path_a;
        t_item_b.path = path_dir;
        factory = get_factory( RuntimeClass_Windows_ApplicationModel_DataTransfer_DataPackage );
        check_hr( IActivationFactory_ActivateInstance( factory, (IInspectable **)&package ), S_OK );
        IActivationFactory_Release( factory );
        check_hr( IDataPackage_SetStorageItemsReadOnly( package, &t_iterable ), S_OK );
        check_hr( IClipboardStatics_SetContent( statics, package ), S_OK );
        check_hr( IClipboardStatics_Flush( statics ), S_OK );
        IDataPackage_Release( package );

        ok( OpenClipboard( NULL ), "OpenClipboard failed.\n" );
        ok( IsClipboardFormatAvailable( CF_HDROP ), "CF_HDROP not offered by the WinRT clipboard push.\n" );
        hg = GetClipboardData( CF_HDROP );
        ok( !!hg, "GetClipboardData(CF_HDROP) returned NULL.\n" );
        if (hg)
        {
            WCHAR *p;

            size_bytes = GlobalSize( hg );
            df = GlobalLock( hg );
            ok( size_bytes >= sizeof(*df) + (lstrlenW( path_a ) + 1 + lstrlenW( path_dir ) + 1) * sizeof(WCHAR),
                "CF_HDROP is %lu bytes.\n", (unsigned long)size_bytes );
            ok( df && df->pFiles == sizeof(*df) && df->fWide,
                "DROPFILES header pFiles %u, fWide %d.\n", df ? (UINT)df->pFiles : 0, df ? df->fWide : -1 );
            p = (WCHAR *)((BYTE *)df + df->pFiles);
            ok( !wcscmp( p, path_a ), "CF_HDROP path 1 %s.\n", debugstr_w( p ) );
            p += lstrlenW( p ) + 1;
            ok( !wcscmp( p, path_dir ), "CF_HDROP path 2 %s.\n", debugstr_w( p ) );
            p += lstrlenW( p ) + 1;
            ok( !*p, "CF_HDROP list not double-NUL terminated.\n" );
            GlobalUnlock( hg );
        }
        CloseClipboard();

        /* The WinRT side reads its own serialization back, in order. */
        check_hr( IClipboardStatics_GetContent( statics, &view ), S_OK );
        check_hr( IDataPackageView_Contains( view, fmt_name, &found ), S_OK );
        ok( found, "StorageItems missing after the WinRT push.\n" );
        check_hr( IDataPackageView_GetStorageItemsAsync( view, &items_op ), S_OK );
        check_hr( IAsyncOperation_IVectorView_IStorageItem_GetResults( items_op, &items ), S_OK );
        check_hr( __FIVectorView_1_Windows__CStorage__CIStorageItem_get_Size( items, &size ), S_OK );
        ok( size == 2, "got %u items after the WinRT push.\n", size );
        check_hr( __FIVectorView_1_Windows__CStorage__CIStorageItem_GetAt( items, 0, &item ), S_OK );
        check_storage_item( item, path_a, file_name, FALSE );
        __x_ABI_CWindows_CStorage_CIStorageItem_Release( item );
        item = NULL;
        check_hr( __FIVectorView_1_Windows__CStorage__CIStorageItem_GetAt( items, 1, &item ), S_OK );
        check_storage_item( item, path_dir, dir_name, TRUE );
        __x_ABI_CWindows_CStorage_CIStorageItem_Release( item );
        __FIVectorView_1_Windows__CStorage__CIStorageItem_Release( items );
        IAsyncOperation_IVectorView_IStorageItem_Release( items_op );
        IDataPackageView_Release( view );
    }

    DeleteFileW( path_a );
    RemoveDirectoryW( path_dir );
    WindowsDeleteString( path_h );
    WindowsDeleteString( fmt_name );
    IStandardDataFormatsStatics_Release( fmt_statics );
    check_hr( IClipboardStatics_Clear( statics ), S_OK );
    IClipboardStatics_Release( statics );
}

START_TEST( dataexchange )
{
    HRESULT hr;

    hr = RoInitialize( RO_INIT_MULTITHREADED );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    test_activation();
    test_data_package();
    test_clipboard();
    test_clipboard_roundtrip();
    test_clipboard_files();
    test_standard_formats();
    test_format_keys();
    test_share_surface();
    test_core_drag_drop();

    RoUninitialize();
}
