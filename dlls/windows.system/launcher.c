/* windows.system.dll: Windows.System.Launcher activation factory
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
 * Windows.System.Launcher (ILauncherStatics/2/3): every Launch* entry point hands the target
 * (URI absolute string, IStorageItem path, or folder path) to ShellExecuteW(NULL, L"open", ...),
 * which wine maps onto the registered shell open command (winebrowser/xdg-open for URI schemes,
 * the file manager for folders), and answers with a completed IAsyncOperation<boolean> whose
 * result mirrors the documented ShellExecuteW > 32 success rule.  The operations are born
 * completed like the ones in async.c; they carry a scalar result (boolean / enum), so they get
 * their own vtable pair here.  QueryUriSupportAsync answers Available for http/https/mailto/file
 * and for schemes registered under HKEY_CLASSES_ROOT, NotSupported otherwise.
 *
 * ILauncherOptions / IFolderLauncherOptions inputs are accepted and ignored (default behaviour);
 * the ILauncherStatics2 methods with no native counterpart (results/data handlers,
 * package-family-name queries, handler enumeration) return completed operations with the
 * documented "no provider" answer (NULL object).
 */

#include <stdlib.h>

#include "private.h"
#include "shellapi.h"

WINE_DEFAULT_DEBUG_CHANNEL(winsystem);

/* {277151c3-9e3e-42f6-91a4-5dfdeb232451} */
static const IID IID_ILauncherStatics =
    { 0x277151c3, 0x9e3e, 0x42f6, { 0x91, 0xa4, 0x5d, 0xfd, 0xeb, 0x23, 0x24, 0x51 } };
/* {59ba2fbb-24cb-4c02-a4c4-8294569d54f1} */
static const IID IID_ILauncherStatics2 =
    { 0x59ba2fbb, 0x24cb, 0x4c02, { 0xa4, 0xc4, 0x82, 0x94, 0x56, 0x9d, 0x54, 0xf1 } };
/* {234261a8-9db3-4683-aa42-dc6f51d33847} */
static const IID IID_ILauncherStatics3 =
    { 0x234261a8, 0x9db3, 0x4683, { 0xaa, 0x42, 0xdc, 0x6f, 0x51, 0xd3, 0x38, 0x47 } };

/***********************************************************************
 * completed IAsyncOperation with scalar (boolean / enum) result
 */

enum op_result_kind
{
    RESULT_BOOL,
    RESULT_ENUM,
};

struct launch_op
{
    const void **self_vtbl;
    const void **info_vtbl;
    LONG ref;
    const IID *iid;           /* canonical closed-instantiation IID */
    const IID *iid2;          /* C++/WinRT projection IID, accepted on QI */
    const WCHAR *class_name;
    void *handler;            /* IAsyncOperationCompletedHandler`1<T> *, owned */
    INT32 result;             /* boolean (0/1) or enum value */
    enum op_result_kind kind;
};

static inline struct launch_op *launch_op_from_self( const void *iface )
{
    return (struct launch_op *)iface;
}

static inline struct launch_op *launch_op_from_info( const void *iface )
{
    return (struct launch_op *)((const BYTE *)iface - sizeof(void *));
}

static void launch_op_invoke_handler( struct launch_op *impl )
{
    void **vtbl;
    HRESULT (STDMETHODCALLTYPE *invoke)( void *handler, void *async, enum AsyncStatus status );

    if (!impl->handler) return;

    /* The completed handlers are { QueryInterface, AddRef, Release, Invoke }. */
    vtbl = *(void ***)impl->handler;
    invoke = (void *)vtbl[3];
    invoke( impl->handler, (void *)impl, Completed );
}

static HRESULT WINAPI launch_op_QueryInterface( const void *iface, REFIID iid, void **out )
{
    struct launch_op *impl = launch_op_from_self( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (!out) return E_POINTER;
    *out = NULL;

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, impl->iid ) ||
        (impl->iid2 && IsEqualGUID( iid, impl->iid2 )))
    {
        *out = (void *)impl;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IAsyncInfo ))
    {
        *out = (void *)&impl->info_vtbl;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    return E_NOINTERFACE;
}

static ULONG WINAPI launch_op_AddRef( const void *iface )
{
    struct launch_op *impl = launch_op_from_self( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI launch_op_Release( const void *iface )
{
    struct launch_op *impl = launch_op_from_self( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        if (impl->handler) IUnknown_Release( (IUnknown *)impl->handler );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI launch_op_GetIids( const void *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI launch_op_GetRuntimeClassName( const void *iface, HSTRING *class_name )
{
    struct launch_op *impl = launch_op_from_self( iface );

    if (!class_name) return E_POINTER;
    return WindowsCreateString( impl->class_name, lstrlenW( impl->class_name ), class_name );
}

static HRESULT WINAPI launch_op_GetTrustLevel( const void *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI launch_op_put_Completed( const void *iface, void *handler )
{
    struct launch_op *impl = launch_op_from_self( iface );

    TRACE( "iface %p, handler %p.\n", iface, handler );

    if (impl->handler) IUnknown_Release( (IUnknown *)impl->handler );
    impl->handler = handler;
    if (handler) IUnknown_AddRef( (IUnknown *)handler );

    /* The operation is born completed: a handler attached later fires at once. */
    launch_op_invoke_handler( impl );
    return S_OK;
}

static HRESULT WINAPI launch_op_get_Completed( const void *iface, void **handler )
{
    struct launch_op *impl = launch_op_from_self( iface );

    if (!handler) return E_POINTER;
    *handler = impl->handler;
    if (*handler) IUnknown_AddRef( (IUnknown *)*handler );
    return S_OK;
}

static HRESULT WINAPI launch_op_GetResults_bool( const void *iface, boolean *results )
{
    struct launch_op *impl = launch_op_from_self( iface );

    TRACE( "iface %p, results %p.\n", iface, results );

    if (!results) return E_POINTER;
    *results = (boolean)impl->result;
    return S_OK;
}

static HRESULT WINAPI launch_op_GetResults_enum( const void *iface, INT32 *results )
{
    struct launch_op *impl = launch_op_from_self( iface );

    TRACE( "iface %p, results %p.\n", iface, results );

    if (!results) return E_POINTER;
    *results = impl->result;
    return S_OK;
}

static const void *launch_op_bool_vtbl[] =
{
    launch_op_QueryInterface,
    launch_op_AddRef,
    launch_op_Release,
    launch_op_GetIids,
    launch_op_GetRuntimeClassName,
    launch_op_GetTrustLevel,
    launch_op_put_Completed,
    launch_op_get_Completed,
    launch_op_GetResults_bool,
};

static const void *launch_op_enum_vtbl[] =
{
    launch_op_QueryInterface,
    launch_op_AddRef,
    launch_op_Release,
    launch_op_GetIids,
    launch_op_GetRuntimeClassName,
    launch_op_GetTrustLevel,
    launch_op_put_Completed,
    launch_op_get_Completed,
    launch_op_GetResults_enum,
};

/* IAsyncInfo face of the same object */

static HRESULT WINAPI launch_info_QueryInterface( const void *iface, REFIID iid, void **out )
{
    struct launch_op *impl = launch_op_from_info( iface );
    return launch_op_QueryInterface( (void *)impl, iid, out );
}

static ULONG WINAPI launch_info_AddRef( const void *iface )
{
    struct launch_op *impl = launch_op_from_info( iface );
    return launch_op_AddRef( (void *)impl );
}

static ULONG WINAPI launch_info_Release( const void *iface )
{
    struct launch_op *impl = launch_op_from_info( iface );
    return launch_op_Release( (void *)impl );
}

static HRESULT WINAPI launch_info_GetIids( const void *iface, ULONG *iid_count, IID **iids )
{
    struct launch_op *impl = launch_op_from_info( iface );
    return launch_op_GetIids( (void *)impl, iid_count, iids );
}

static HRESULT WINAPI launch_info_GetRuntimeClassName( const void *iface, HSTRING *class_name )
{
    if (!class_name) return E_POINTER;
    return WindowsCreateString( L"Windows.Foundation.IAsyncInfo", 29, class_name );
}

static HRESULT WINAPI launch_info_GetTrustLevel( const void *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI launch_info_get_Id( const void *iface, UINT32 *id )
{
    if (!id) return E_POINTER;
    *id = 1;
    return S_OK;
}

static HRESULT WINAPI launch_info_get_Status( const void *iface, enum AsyncStatus *status )
{
    if (!status) return E_POINTER;
    *status = Completed;
    return S_OK;
}

static HRESULT WINAPI launch_info_get_ErrorCode( const void *iface, HRESULT *error_code )
{
    if (!error_code) return E_POINTER;
    *error_code = S_OK;
    return S_OK;
}

static HRESULT WINAPI launch_info_Cancel( const void *iface )
{
    return S_OK;
}

static HRESULT WINAPI launch_info_Close( const void *iface )
{
    return S_OK;
}

static const void *launch_info_vtbl[] =
{
    launch_info_QueryInterface,
    launch_info_AddRef,
    launch_info_Release,
    launch_info_GetIids,
    launch_info_GetRuntimeClassName,
    launch_info_GetTrustLevel,
    launch_info_get_Id,
    launch_info_get_Status,
    launch_info_get_ErrorCode,
    launch_info_Cancel,
    launch_info_Close,
};

static struct launch_op *create_launch_op( const IID *iid, const IID *iid2,
                                           enum op_result_kind kind, const WCHAR *class_name )
{
    struct launch_op *impl;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return NULL;

    impl->self_vtbl = kind == RESULT_BOOL ? launch_op_bool_vtbl : launch_op_enum_vtbl;
    impl->info_vtbl = launch_info_vtbl;
    impl->ref = 1;
    impl->iid = iid;
    impl->iid2 = iid2;
    impl->class_name = class_name;
    impl->kind = kind;
    return impl;
}

/***********************************************************************
 * closed-instantiation IIDs
 *
 * The boolean operation answers both the Windows SDK C-ABI pids IID
 * (windows.foundation.h __FIAsyncOperation_1_boolean) and the C++/WinRT projection IID; the
 * LaunchQuerySupportStatus operation answers the pids IID computed over the metadata type
 * signature (uuid5 of "Windows.Foundation.IAsyncOperation`1<enum(Windows.System.
 * LaunchQuerySupportStatus;i4)>" in namespace {11f47ad5-7b73-42c0-abae-878b1e16adee}) plus the
 * projection-name form, so both the C ABI and the C++/WinRT app projections GetResults on the
 * object we hand back.
 */

/* {cdb5efb3-5788-509d-9be1-71ccb8a3362a} */
static const IID IID_op_bool =
    { 0xcdb5efb3, 0x5788, 0x509d, { 0x9b, 0xe1, 0x71, 0xcc, 0xb8, 0xa3, 0x36, 0x2a } };
/* {72819163-60d9-4be1-a537-99772f586822} */
static const IID IID_op_bool_winrt =
    { 0x72819163, 0x60d9, 0x4be1, { 0xa5, 0x37, 0x99, 0x77, 0x2f, 0x58, 0x68, 0x22 } };
/* {a2d13cfe-9746-5caa-b01d-bbe364ad2930} */
static const IID IID_op_lqss =
    { 0xa2d13cfe, 0x9746, 0x5caa, { 0xb0, 0x1d, 0xbb, 0xe3, 0x64, 0xad, 0x29, 0x30 } };
/* {75075a8e-b7c5-5b63-b3a1-0ab107de507f} */
static const IID IID_op_lqss_alt =
    { 0x75075a8e, 0xb7c5, 0x5b63, { 0xb3, 0xa1, 0x0a, 0xb1, 0x07, 0xde, 0x50, 0x7f } };
/* {48962314-6120-5b7d-99da-b91650d63b6d} __FIAsyncOperation_1_ILauncherResult */
static const IID IID_op_launcher_result =
    { 0x48962314, 0x6120, 0x5b7d, { 0x99, 0xda, 0xb9, 0x16, 0x50, 0xd6, 0x3b, 0x6d } };
/* {4cff9b20-aae7-5d4e-b15d-cc62484d36e5}
 * __FIAsyncOperation_1_IVectorView`1<Windows.System.ILaunchableSchemeEntry> */
static const IID IID_op_scheme_entries =
    { 0x4cff9b20, 0xaae7, 0x5d4e, { 0xb1, 0x5d, 0xcc, 0x62, 0x48, 0x4d, 0x36, 0xe5 } };
/* {5bb83595-c675-5802-83bf-a537e40f394b}
 * __FIAsyncOperation_1_IVectorView`1<Windows.System.IFileHandler> */
static const IID IID_op_file_handlers =
    { 0x5bb83595, 0xc675, 0x5802, { 0x83, 0xbf, 0xa5, 0x37, 0xe4, 0x0f, 0x39, 0x4b } };

static const WCHAR bool_op_class[] = L"Windows.Foundation.IAsyncOperation`1<Boolean>";
static const WCHAR lqss_op_class[] =
    L"Windows.Foundation.IAsyncOperation`1<Windows.System.LaunchQuerySupportStatus>";

/***********************************************************************
 * Windows.System.Launcher
 */

enum LaunchQuerySupportStatus
{
    LaunchQuerySupportStatus_Available = 0,
    LaunchQuerySupportStatus_PlatformNotSupported = 1,
    LaunchQuerySupportStatus_AppNotInstalled = 2,
    LaunchQuerySupportStatus_NotSupported = 3,
    LaunchQuerySupportStatus_Unknown = 4
};

enum LaunchQuerySupportType
{
    LaunchQuerySupportType_Uri = 0,
    LaunchQuerySupportType_File = 1
};

static HRESULT set_results_op_bool( struct launch_op *impl, const WCHAR *target,
                                    IAsyncOperation_boolean **op )
{
    HINSTANCE hinst;

    hinst = ShellExecuteW( NULL, L"open", target, NULL, NULL, SW_SHOWNORMAL );
    impl->result = ((INT_PTR)hinst > 32) ? TRUE : FALSE;
    TRACE( "ShellExecuteW(%s) -> %p, result %d.\n", debugstr_w( target ), hinst, impl->result );

    *op = (IAsyncOperation_boolean *)impl;
    return S_OK;
}

static HRESULT launch_string( const WCHAR *target, IAsyncOperation_boolean **op )
{
    struct launch_op *impl;

    if (!op) return E_POINTER;
    if (!target || !target[0]) return E_INVALIDARG;

    if (!(impl = create_launch_op( &IID_op_bool, &IID_op_bool_winrt, RESULT_BOOL, bool_op_class )))
        return E_OUTOFMEMORY;

    return set_results_op_bool( impl, target, op );
}

static HRESULT uri_absolute( IUriRuntimeClass *uri, WCHAR *buf, DWORD size )
{
    const WCHAR *raw;
    HSTRING str;
    HRESULT hr;

    hr = IUriRuntimeClass_get_AbsoluteUri( uri, &str );
    if (FAILED( hr )) return hr;

    raw = WindowsGetStringRawBuffer( str, NULL );
    if (!raw || !raw[0])
    {
        WindowsDeleteString( str );
        return E_INVALIDARG;
    }
    lstrcpynW( buf, raw, size );
    WindowsDeleteString( str );
    return S_OK;
}

static HRESULT storage_path( IUnknown *item, WCHAR *buf, DWORD size )
{
    IStorageItem *storage_item;
    const WCHAR *raw;
    HSTRING str;
    HRESULT hr;

    if ((hr = IUnknown_QueryInterface( item, &IID_IStorageItem, (void **)&storage_item )))
        return hr;

    hr = IStorageItem_get_Path( storage_item, &str );
    IStorageItem_Release( storage_item );
    if (FAILED( hr )) return hr;

    raw = WindowsGetStringRawBuffer( str, NULL );
    if (!raw || !raw[0])
    {
        WindowsDeleteString( str );
        return E_INVALIDARG;
    }
    lstrcpynW( buf, raw, size );
    WindowsDeleteString( str );
    return S_OK;
}

static HRESULT uri_scheme_supported( IUriRuntimeClass *uri, enum LaunchQuerySupportStatus *status )
{
    static const WCHAR *const known[] = { L"http", L"https", L"mailto", L"file" };
    enum LaunchQuerySupportStatus result = LaunchQuerySupportStatus_NotSupported;
    HKEY key;
    HSTRING str;
    const WCHAR *raw;
    unsigned int i;
    HRESULT hr;

    hr = IUriRuntimeClass_get_SchemeName( uri, &str );
    if (FAILED( hr )) return hr;

    raw = WindowsGetStringRawBuffer( str, NULL );
    if (raw && raw[0])
    {
        for (i = 0; i < ARRAYSIZE( known ); i++)
        {
            if (!lstrcmpiW( raw, known[i] ))
            {
                result = LaunchQuerySupportStatus_Available;
                break;
            }
        }
    }
    if (result == LaunchQuerySupportStatus_NotSupported && raw && raw[0]
            && !RegOpenKeyExW( HKEY_CLASSES_ROOT, raw, 0, KEY_READ, &key ))
    {
        RegCloseKey( key );
        result = LaunchQuerySupportStatus_Available;
    }
    WindowsDeleteString( str );

    *status = result;
    return S_OK;
}

static HRESULT query_scheme_op( IUriRuntimeClass *uri, void **op )
{
    enum LaunchQuerySupportStatus status;
    struct launch_op *impl;
    HRESULT hr;

    if (!op) return E_POINTER;
    if (!uri) return E_INVALIDARG;

    if (!(impl = create_launch_op( &IID_op_lqss, &IID_op_lqss_alt, RESULT_ENUM, lqss_op_class )))
        return E_OUTOFMEMORY;

    if ((hr = uri_scheme_supported( uri, &status )) == S_OK)
    {
        impl->result = status;
        *op = impl;
        return S_OK;
    }

    free( impl );
    return hr;
}

static HRESULT query_file_op( IStorageFile *file, void **op )
{
    enum LaunchQuerySupportStatus status = LaunchQuerySupportStatus_Available;
    WCHAR buf[MAX_PATH * 4], *dot;
    struct launch_op *impl;
    HKEY key;
    HRESULT hr;

    if (!op) return E_POINTER;
    if (!file) return E_INVALIDARG;
    if (FAILED( hr = storage_path( (IUnknown *)file, buf, ARRAYSIZE( buf ) ))) return hr;

    /* Answer Available for a file whose extension carries a registered association (the answer
     * an unregistered extension gets on a desktop is NotSupported). */
    if ((dot = wcsrchr( buf, '.' )) && dot[1])
    {
        if (RegOpenKeyExW( HKEY_CLASSES_ROOT, dot, 0, KEY_READ, &key ))
            status = LaunchQuerySupportStatus_NotSupported;
        else
            RegCloseKey( key );
    }

    if (!(impl = create_launch_op( &IID_op_lqss, &IID_op_lqss_alt, RESULT_ENUM, lqss_op_class )))
        return E_OUTOFMEMORY;

    impl->result = status;
    *op = impl;
    return S_OK;
}

static HRESULT completed_null_op( const IID *iid, const WCHAR *class_name, void **op )
{
    struct async_op *impl;

    if (!op) return E_POINTER;
    if (!(impl = create_async_op( iid, class_name, NULL ))) return E_OUTOFMEMORY;
    *op = async_op_iface( impl );
    async_op_release( impl );
    return S_OK;
}

/* IActivationFactory/IInspectable prologue shared by the four faces */

static const IID *const launcher_iids[] =
{
    &IID_ILauncherStatics, &IID_ILauncherStatics2, &IID_ILauncherStatics3,
};

static HRESULT WINAPI launcher_GetIids( void *iface, ULONG *iid_count, IID **iids )
{
    IID *arr;

    if (!iid_count || !iids) return E_POINTER;

    if (!(arr = CoTaskMemAlloc( ARRAYSIZE( launcher_iids ) * sizeof(*arr) )))
        return E_OUTOFMEMORY;

    for (unsigned int i = 0; i < ARRAYSIZE( launcher_iids ); i++)
        arr[i] = *launcher_iids[i];

    *iid_count = ARRAYSIZE( launcher_iids );
    *iids = arr;
    return S_OK;
}

static HRESULT WINAPI launcher_GetRuntimeClassName( void *iface, HSTRING *class_name )
{
    if (!class_name) return E_POINTER;
    return WindowsCreateString( L"Windows.System.Launcher", 23, class_name );
}

static HRESULT WINAPI launcher_GetTrustLevel( void *iface, TrustLevel *trust_level )
{
    if (!trust_level) return E_POINTER;
    *trust_level = FullTrust;
    return S_OK;
}

/* ILauncherStatics */

static HRESULT WINAPI launcher_LaunchUriAsync( void *iface, IUriRuntimeClass *uri,
                                               IAsyncOperation_boolean **op )
{
    WCHAR buf[MAX_PATH * 4];
    HRESULT hr;

    TRACE( "iface %p, uri %p, op %p.\n", iface, uri, op );

    if (!op) return E_POINTER;
    if (!uri) return E_INVALIDARG;
    if (FAILED( hr = uri_absolute( uri, buf, ARRAYSIZE( buf ) ))) return hr;

    return launch_string( buf, op );
}

static HRESULT WINAPI launcher_LaunchUriWithOptionsAsync( void *iface, IUriRuntimeClass *uri,
                                                          void *options,
                                                          IAsyncOperation_boolean **op )
{
    TRACE( "iface %p, uri %p, options %p, op %p.\n", iface, uri, options, op );
    return launcher_LaunchUriAsync( iface, uri, op );
}

static HRESULT WINAPI launcher_LaunchFileAsync( void *iface, IStorageFile *file,
                                                IAsyncOperation_boolean **op )
{
    WCHAR buf[MAX_PATH * 4];
    HRESULT hr;

    TRACE( "iface %p, file %p, op %p.\n", iface, file, op );

    if (!op) return E_POINTER;
    if (!file) return E_INVALIDARG;
    if (FAILED( hr = storage_path( (IUnknown *)file, buf, ARRAYSIZE( buf ) ))) return hr;

    return launch_string( buf, op );
}

static HRESULT WINAPI launcher_LaunchFileWithOptionsAsync( void *iface, IStorageFile *file,
                                                           void *options,
                                                           IAsyncOperation_boolean **op )
{
    TRACE( "iface %p, file %p, options %p, op %p.\n", iface, file, options, op );
    return launcher_LaunchFileAsync( iface, file, op );
}

/* ILauncherStatics2 */

static HRESULT WINAPI launcher2_LaunchUriForResultsAsync( void *iface, IUriRuntimeClass *uri,
                                                          void *options, void **op )
{
    WCHAR buf[MAX_PATH * 4];
    HRESULT hr;

    TRACE( "iface %p, uri %p, options %p, op %p.\n", iface, uri, options, op );

    if (!op) return E_POINTER;
    if (!uri) return E_INVALIDARG;
    if (FAILED( hr = uri_absolute( uri, buf, ARRAYSIZE( buf ) ))) return hr;

    ShellExecuteW( NULL, L"open", buf, NULL, NULL, SW_SHOWNORMAL );
    return completed_null_op( &IID_op_launcher_result,
                              L"Windows.Foundation.IAsyncOperation`1<Windows.System.LauncherResult>",
                              op );
}

static HRESULT WINAPI launcher2_LaunchUriForResultsWithDataAsync( void *iface, IUriRuntimeClass *uri,
                                                                  void *options, void *data,
                                                                  void **op )
{
    TRACE( "iface %p, uri %p, options %p, data %p, op %p.\n", iface, uri, options, data, op );
    return launcher2_LaunchUriForResultsAsync( iface, uri, options, op );
}

static HRESULT WINAPI launcher2_LaunchUriWithDataAsync( void *iface, IUriRuntimeClass *uri,
                                                        void *data,
                                                        IAsyncOperation_boolean **op )
{
    TRACE( "iface %p, uri %p, data %p, op %p.\n", iface, uri, data, op );
    return launcher_LaunchUriAsync( iface, uri, op );
}

static HRESULT WINAPI launcher2_QueryUriSupportAsync( void *iface, IUriRuntimeClass *uri,
                                                      enum LaunchQuerySupportType type,
                                                      void **op )
{
    TRACE( "iface %p, uri %p, type %d, op %p.\n", iface, uri, type, op );
    return query_scheme_op( uri, op );
}

static HRESULT WINAPI launcher2_QueryUriSupportWithPackageFamilyNameAsync( void *iface,
                                                                           IUriRuntimeClass *uri,
                                                                           enum LaunchQuerySupportType type,
                                                                           HSTRING package_family_name,
                                                                           void **op )
{
    TRACE( "iface %p, uri %p, type %d, package_family_name %s, op %p.\n",
           iface, uri, type, debugstr_hstring( package_family_name ), op );
    return query_scheme_op( uri, op );
}

static HRESULT WINAPI launcher2_QueryFileSupportAsync( void *iface, IStorageFile *file, void **op )
{
    TRACE( "iface %p, file %p, op %p.\n", iface, file, op );

    return query_file_op( file, op );
}

static HRESULT WINAPI launcher2_QueryFileSupportWithPackageFamilyNameAsync( void *iface,
                                                                            IStorageFile *file,
                                                                            HSTRING package_family_name,
                                                                            void **op )
{
    TRACE( "iface %p, file %p, package_family_name %s, op %p.\n",
           iface, file, debugstr_hstring( package_family_name ), op );
    return launcher2_QueryFileSupportAsync( iface, file, op );
}

static HRESULT WINAPI launcher2_FindUriSchemeHandlersAsync( void *iface, HSTRING uri_scheme,
                                                            enum LaunchQuerySupportType type,
                                                            HSTRING package_family_name,
                                                            void **op )
{
    TRACE( "iface %p, uri_scheme %s, type %d, package_family_name %s, op %p.\n",
           iface, debugstr_hstring( uri_scheme ), type, debugstr_hstring( package_family_name ), op );

    return completed_null_op( &IID_op_scheme_entries,
                              L"Windows.Foundation.IAsyncOperation`1<Windows.Foundation.Collections.IVectorView`1<Windows.System.LaunchableSchemeEntry>>",
                              op );
}

static HRESULT WINAPI launcher2_FindFileHandlersAsync( void *iface, HSTRING file_type,
                                                       void **op )
{
    TRACE( "iface %p, file_type %s, op %p.\n", iface, debugstr_hstring( file_type ), op );

    return completed_null_op( &IID_op_file_handlers,
                              L"Windows.Foundation.IAsyncOperation`1<Windows.Foundation.Collections.IVectorView`1<Windows.System.FileHandler>>",
                              op );
}

/* ILauncherStatics3 */

static HRESULT WINAPI launcher3_LaunchFolderAsync( void *iface, IStorageFolder *folder,
                                                   IAsyncOperation_boolean **op )
{
    WCHAR buf[MAX_PATH * 4];
    HRESULT hr;

    TRACE( "iface %p, folder %p, op %p.\n", iface, folder, op );

    if (!op) return E_POINTER;
    if (!folder) return E_INVALIDARG;
    if (FAILED( hr = storage_path( (IUnknown *)folder, buf, ARRAYSIZE( buf ) ))) return hr;

    return launch_string( buf, op );
}

static HRESULT WINAPI launcher3_LaunchFolderWithOptionsAsync( void *iface, IStorageFolder *folder,
                                                             void *options,
                                                             IAsyncOperation_boolean **op )
{
    TRACE( "iface %p, folder %p, options %p, op %p.\n", iface, folder, options, op );
    return launcher3_LaunchFolderAsync( iface, folder, op );
}

static HRESULT WINAPI launcher3_LaunchFolderPathAsync( void *iface, HSTRING path,
                                                       IAsyncOperation_boolean **op )
{
    const WCHAR *raw;

    TRACE( "iface %p, path %s, op %p.\n", iface, debugstr_hstring( path ), op );

    if (!op) return E_POINTER;
    if (!(raw = WindowsGetStringRawBuffer( path, NULL )) || !raw[0]) return E_INVALIDARG;

    return launch_string( raw, op );
}

static HRESULT WINAPI launcher3_LaunchFolderPathWithOptionsAsync( void *iface, HSTRING path,
                                                                 void *options,
                                                                 IAsyncOperation_boolean **op )
{
    TRACE( "iface %p, path %s, options %p, op %p.\n",
           iface, debugstr_hstring( path ), options, op );
    return launcher3_LaunchFolderPathAsync( iface, path, op );
}

struct launcher_statics
{
    const void **vtbl_factory;
    const void **vtbl_statics;
    const void **vtbl_statics2;
    const void **vtbl_statics3;
};

static struct launcher_statics launcher_statics_obj;

static HRESULT WINAPI launcher_statics_QueryInterface( void *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    *out = NULL;

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
        *out = &launcher_statics_obj.vtbl_factory;
    else if (IsEqualGUID( iid, &IID_ILauncherStatics ))
        *out = &launcher_statics_obj.vtbl_statics;
    else if (IsEqualGUID( iid, &IID_ILauncherStatics2 ))
        *out = &launcher_statics_obj.vtbl_statics2;
    else if (IsEqualGUID( iid, &IID_ILauncherStatics3 ))
        *out = &launcher_statics_obj.vtbl_statics3;
    else
        return E_NOINTERFACE;

    return S_OK;
}

static ULONG WINAPI launcher_statics_AddRef( void *iface ) { return 2; }
static ULONG WINAPI launcher_statics_Release( void *iface ) { return 1; }

static HRESULT WINAPI launcher_statics_ActivateInstance( void *iface, IInspectable **instance )
{
    TRACE( "iface %p, instance %p.\n", iface, instance );
    if (!instance) return E_POINTER;
    *instance = NULL;
    return E_NOTIMPL;  /* Windows.System.Launcher is a statics-only class */
}

static const void *launcher_factory_vtbl[] =
{
    launcher_statics_QueryInterface,
    launcher_statics_AddRef,
    launcher_statics_Release,
    launcher_GetIids,
    launcher_GetRuntimeClassName,
    launcher_GetTrustLevel,
    launcher_statics_ActivateInstance,
};

static const void *launcher_statics_vtbl[] =
{
    launcher_statics_QueryInterface,
    launcher_statics_AddRef,
    launcher_statics_Release,
    launcher_GetIids,
    launcher_GetRuntimeClassName,
    launcher_GetTrustLevel,
    launcher_LaunchFileAsync,
    launcher_LaunchFileWithOptionsAsync,
    launcher_LaunchUriAsync,
    launcher_LaunchUriWithOptionsAsync,
};

static const void *launcher_statics2_vtbl[] =
{
    launcher_statics_QueryInterface,
    launcher_statics_AddRef,
    launcher_statics_Release,
    launcher_GetIids,
    launcher_GetRuntimeClassName,
    launcher_GetTrustLevel,
    launcher2_LaunchUriForResultsAsync,
    launcher2_LaunchUriForResultsWithDataAsync,
    launcher2_LaunchUriWithDataAsync,
    launcher2_QueryUriSupportAsync,
    launcher2_QueryUriSupportWithPackageFamilyNameAsync,
    launcher2_QueryFileSupportAsync,
    launcher2_QueryFileSupportWithPackageFamilyNameAsync,
    launcher2_FindUriSchemeHandlersAsync,
    launcher2_FindFileHandlersAsync,
};

static const void *launcher_statics3_vtbl[] =
{
    launcher_statics_QueryInterface,
    launcher_statics_AddRef,
    launcher_statics_Release,
    launcher_GetIids,
    launcher_GetRuntimeClassName,
    launcher_GetTrustLevel,
    launcher3_LaunchFolderAsync,
    launcher3_LaunchFolderWithOptionsAsync,
    launcher3_LaunchFolderPathAsync,
    launcher3_LaunchFolderPathWithOptionsAsync,
};

static struct launcher_statics launcher_statics_obj =
{
    launcher_factory_vtbl,
    launcher_statics_vtbl,
    launcher_statics2_vtbl,
    launcher_statics3_vtbl,
};

IActivationFactory *launcher_factory = (IActivationFactory *)&launcher_statics_obj.vtbl_factory;
