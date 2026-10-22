/*
 * Copyright (C) 2022 Mohamad Al-Jaf
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
#include "initguid.h"
#include <stdarg.h>
#include <string.h>

#include "windef.h"
#include "winbase.h"
#include "winstring.h"

#include "roapi.h"

#define WIDL_using_Windows_Foundation
#define WIDL_using_Windows_Foundation_Collections
#include "windows.foundation.h"
#define WIDL_using_Windows_Storage_Streams
#include "windows.storage.streams.h"
#define WIDL_using_Windows_Security_Cryptography
#include "windows.security.cryptography.h"
#define WIDL_using_Windows_Security_Cryptography_Core
#include "windows.security.cryptography.core.h"
#define WIDL_using_Windows_Security_Credentials
#include "windows.security.credentials.h"
#include "robuffer.h"

#include "wine/test.h"

static HRESULT (WINAPI *pWindowsDeleteString)( HSTRING str );
static const WCHAR* (WINAPI *pWindowsGetStringRawBuffer)( HSTRING, UINT32* );

static BOOL load_combase_functions(void)
{
    HMODULE combase = GetModuleHandleW( L"combase.dll" );

#define LOAD_FUNC(m, f) if (!(p ## f = (void *)GetProcAddress( m, #f ))) goto failed;
    LOAD_FUNC( combase, WindowsDeleteString );
    LOAD_FUNC( combase, WindowsGetStringRawBuffer );
#undef LOAD_FUNC

    return TRUE;

failed:
    win_skip("Failed to load combase.dll functions, skipping tests\n");
    return FALSE;
}

#define check_interface( obj, iid ) check_interface_( __LINE__, obj, iid )
static void check_interface_( unsigned int line, void *obj, const IID *iid )
{
    IUnknown *iface = obj;
    IUnknown *unk;
    HRESULT hr;

    hr = IUnknown_QueryInterface( iface, iid, (void **)&unk );
    ok_(__FILE__, line)( hr == S_OK, "got hr %#lx.\n", hr );
    IUnknown_Release( unk );
}

#define check_runtimeclass( a, b ) check_runtimeclass_( __LINE__, (IInspectable *)a, b )
static void check_runtimeclass_( int line, IInspectable *inspectable, const WCHAR *class_name )
{
    const WCHAR *buffer;
    UINT32 length;
    HSTRING str;
    HRESULT hr;

    hr = IInspectable_GetRuntimeClassName( inspectable, &str );
    ok_(__FILE__, line)( hr == S_OK, "GetRuntimeClassName returned %#lx\n", hr );
    buffer = pWindowsGetStringRawBuffer( str, &length );
    ok_(__FILE__, line)( !wcscmp( buffer, class_name ), "got class name %s\n", debugstr_w(buffer) );
    pWindowsDeleteString( str );
}

static void check_nointerface( void *obj, const GUID *iid )
{
    IInspectable *iface = obj;
    IInspectable *inspectable;
    HRESULT hr;

    inspectable = (void *)0xdeadbeef;
    hr = IInspectable_QueryInterface( iface, iid, (void **)&inspectable );
    ok( hr == E_NOINTERFACE, "got hr %#lx.\n", hr );
    ok( inspectable == NULL, "got interface %p.\n", inspectable );
}

static UINT32 buffer_length( IBuffer *buffer )
{
    UINT32 length = 0;
    HRESULT hr;

    hr = IBuffer_get_Length( buffer, &length );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    return length;
}

#define check_bool_async( a, b, c, d, e ) check_bool_async_( __LINE__, a, b, c, d, e )
static void check_bool_async_( int line, IAsyncOperation_boolean *async, UINT32 expect_id, AsyncStatus expect_status,
                               HRESULT expect_hr, BOOLEAN expect_result )
{
    AsyncStatus async_status;
    IAsyncInfo *async_info;
    HRESULT hr, async_hr;
    UINT32 async_id;
    BOOLEAN result;

    hr = IAsyncOperation_boolean_QueryInterface( async, &IID_IAsyncInfo, (void **)&async_info );
    ok_(__FILE__, line)( hr == S_OK, "QueryInterface returned %#lx\n", hr );

    async_id = 0xdeadbeef;
    hr = IAsyncInfo_get_Id( async_info, &async_id );
    if (expect_status < 4) ok_(__FILE__, line)( hr == S_OK, "get_Id returned %#lx\n", hr );
    else ok_(__FILE__, line)( hr == E_ILLEGAL_METHOD_CALL, "get_Id returned %#lx\n", hr );
    if (expect_id) ok_(__FILE__, line)( async_id == expect_id, "got id %u\n", async_id );
    else trace("Skipping async_id check, got id %u\n", async_id );

    async_status = 0xdeadbeef;
    hr = IAsyncInfo_get_Status( async_info, &async_status );
    if (expect_status < 4) ok_(__FILE__, line)( hr == S_OK, "get_Status returned %#lx\n", hr );
    else ok_(__FILE__, line)( hr == E_ILLEGAL_METHOD_CALL, "get_Status returned %#lx\n", hr );
    ok_(__FILE__, line)( async_status == expect_status || broken( async_status == Error ), "got status %u\n", async_status );

    async_hr = 0xdeadbeef;
    hr = IAsyncInfo_get_ErrorCode( async_info, &async_hr );
    if (expect_status < 4) ok_(__FILE__, line)( hr == S_OK, "get_ErrorCode returned %#lx\n", hr );
    else ok_(__FILE__, line)( hr == E_ILLEGAL_METHOD_CALL, "get_ErrorCode returned %#lx\n", hr );
    if (expect_status < 4)
        todo_wine_if(FAILED(expect_hr)) ok_(__FILE__, line)( async_hr == expect_hr || broken( async_hr == E_INVALIDARG), "got error %#lx\n", async_hr );
    else ok_(__FILE__, line)( async_hr == E_ILLEGAL_METHOD_CALL, "got error %#lx\n", async_hr );

    IAsyncInfo_Release( async_info );

    result = !expect_result;
    hr = IAsyncOperation_boolean_GetResults( async, &result );
    switch (expect_status)
    {
    case Completed:
    case Error:
        todo_wine_if(FAILED(expect_hr))
        ok_(__FILE__, line)( hr == expect_hr || broken( hr == E_INVALIDARG ), "GetResults returned %#lx\n", hr );
        ok_(__FILE__, line)( result == expect_result, "got result %u\n", result );
        break;
    case Canceled:
    case Started:
    default:
        ok_(__FILE__, line)( hr == E_ILLEGAL_METHOD_CALL, "GetResults returned %#lx\n", hr );
        break;
    }
}

struct bool_async_handler
{
    IAsyncOperationCompletedHandler_boolean IAsyncOperationCompletedHandler_boolean_iface;
    IAsyncOperation_boolean *async;
    AsyncStatus status;
    BOOL invoked;
    HANDLE event;
};

static inline struct bool_async_handler *impl_from_IAsyncOperationCompletedHandler_boolean( IAsyncOperationCompletedHandler_boolean *iface )
{
    return CONTAINING_RECORD( iface, struct bool_async_handler, IAsyncOperationCompletedHandler_boolean_iface );
}

static HRESULT WINAPI bool_async_handler_QueryInterface( IAsyncOperationCompletedHandler_boolean *iface, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IAsyncOperationCompletedHandler_boolean ))
    {
        IUnknown_AddRef( iface );
        *out = iface;
        return S_OK;
    }

    trace( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI bool_async_handler_AddRef( IAsyncOperationCompletedHandler_boolean *iface )
{
    return 2;
}

static ULONG WINAPI bool_async_handler_Release( IAsyncOperationCompletedHandler_boolean *iface )
{
    return 1;
}

static HRESULT WINAPI bool_async_handler_Invoke( IAsyncOperationCompletedHandler_boolean *iface,
                                                 IAsyncOperation_boolean *async, AsyncStatus status )
{
    struct bool_async_handler *impl = impl_from_IAsyncOperationCompletedHandler_boolean( iface );

    trace( "iface %p, async %p, status %u\n", iface, async, status );

    ok( !impl->invoked, "invoked twice\n" );
    impl->invoked = TRUE;
    impl->async = async;
    impl->status = status;
    if (impl->event) SetEvent( impl->event );

    return S_OK;
}

static IAsyncOperationCompletedHandler_booleanVtbl bool_async_handler_vtbl =
{
    /*** IUnknown methods ***/
    bool_async_handler_QueryInterface,
    bool_async_handler_AddRef,
    bool_async_handler_Release,
    /*** IAsyncOperationCompletedHandler<boolean> methods ***/
    bool_async_handler_Invoke,
};

static struct bool_async_handler default_bool_async_handler = {{&bool_async_handler_vtbl}};

static void test_CryptobufferStatics(void)
{
    static const WCHAR *cryptobuffer_statics_name = L"Windows.Security.Cryptography.CryptographicBuffer";
    static const WCHAR *buffer_name = L"Windows.Storage.Streams.Buffer";
    ICryptographicBufferStatics *cryptobuffer_statics;
    IActivationFactory *factory, *factory2;
    IBufferByteAccess *buffer_access;
    IBufferFactory *buffer_factory;
    const WCHAR *base64_str;
    IBuffer *buffer;
    HSTRING str;
    byte *data;
    UINT32 len;
    HRESULT hr;
    LONG ref;

    hr = WindowsCreateString( cryptobuffer_statics_name, wcslen( cryptobuffer_statics_name ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&factory );
    WindowsDeleteString( str );
    ok( hr == S_OK || broken( hr == REGDB_E_CLASSNOTREG ), "got hr %#lx.\n", hr );
    if (hr == REGDB_E_CLASSNOTREG)
    {
        win_skip( "%s runtimeclass not registered, skipping tests.\n", wine_dbgstr_w( cryptobuffer_statics_name ) );
        return;
    }

    check_interface( factory, &IID_IUnknown );
    check_interface( factory, &IID_IInspectable );
    check_interface( factory, &IID_IAgileObject );

    hr = IActivationFactory_QueryInterface( factory, &IID_ICryptographicBufferStatics, (void **)&cryptobuffer_statics );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    /* NULL buffer */
    hr = ICryptographicBufferStatics_EncodeToBase64String( cryptobuffer_statics, NULL, &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    base64_str = WindowsGetStringRawBuffer( str, &len );
    ok( !wcscmp( L"", base64_str ) && !len, "got str %s, len %d.\n", wine_dbgstr_w( base64_str ), len );
    WindowsDeleteString( str );

    hr = WindowsCreateString( buffer_name, wcslen( buffer_name ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&factory2 );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IActivationFactory_QueryInterface( factory2, &IID_IBufferFactory, (void **)&buffer_factory );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    WindowsDeleteString( str );

    /* Empty buffer */
    hr = IBufferFactory_Create( buffer_factory, 0, &buffer );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = ICryptographicBufferStatics_EncodeToBase64String( cryptobuffer_statics, buffer, &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    base64_str = WindowsGetStringRawBuffer( str, &len );
    ok( !wcscmp( L"", base64_str ) && !len, "got str %s, len %d.\n", wine_dbgstr_w( base64_str ), len );
    WindowsDeleteString( str );
    IBuffer_Release( buffer );

    /* Test with contents */
    hr = IBufferFactory_Create( buffer_factory, 16, &buffer );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IBuffer_QueryInterface( buffer, &IID_IBufferByteAccess, (void **)&buffer_access );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IBufferByteAccess_Buffer( buffer_access, &data );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    for (int i = 0; i < 16; ++i)
        data[i] = i;
    hr = IBuffer_put_Length( buffer, 16 );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    IBufferByteAccess_Release( buffer_access );

    hr = ICryptographicBufferStatics_EncodeToBase64String( cryptobuffer_statics, buffer, &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    base64_str = WindowsGetStringRawBuffer( str, &len );
    ok( !wcscmp( L"AAECAwQFBgcICQoLDA0ODw==", base64_str ), "got str %s, len %d.\n", wine_dbgstr_w( base64_str ), len );
    WindowsDeleteString( str );
    IBuffer_Release( buffer );

    IBufferFactory_Release( buffer_factory );
    IActivationFactory_Release( factory2 );

    ref = ICryptographicBufferStatics_Release( cryptobuffer_statics );
    ok( ref == 2, "got ref %ld.\n", ref );
    ref = IActivationFactory_Release( factory );
    ok( ref == 1, "got ref %ld.\n", ref );
}

static void test_Credentials_Statics(void)
{
    static const WCHAR *credentials_statics_name = L"Windows.Security.Credentials.KeyCredentialManager";
    IAsyncOperationCompletedHandler_boolean *tmp_handler;
    IKeyCredentialManagerStatics *credentials_statics;
    struct bool_async_handler bool_async_handler;
    IAsyncOperation_boolean *bool_async;
    IActivationFactory *factory;
    IAsyncInfo *async_info;
    HSTRING str;
    HRESULT hr;
    DWORD ret;

    hr = WindowsCreateString( credentials_statics_name, wcslen( credentials_statics_name ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&factory );
    WindowsDeleteString( str );
    ok( hr == S_OK || broken( hr == REGDB_E_CLASSNOTREG ), "got hr %#lx.\n", hr );
    if (hr == REGDB_E_CLASSNOTREG)
    {
        win_skip( "%s runtimeclass not registered, skipping tests.\n", wine_dbgstr_w( credentials_statics_name ) );
        return;
    }

    check_interface( factory, &IID_IUnknown );
    check_interface( factory, &IID_IInspectable );
    check_interface( factory, &IID_IAgileObject );

    hr = IActivationFactory_QueryInterface( factory, &IID_IKeyCredentialManagerStatics, (void **)&credentials_statics );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    if (!load_combase_functions()) return;

    hr = IKeyCredentialManagerStatics_IsSupportedAsync( credentials_statics, &bool_async );
    ok( hr == S_OK, "IsSupportedAsync returned %#lx\n", hr );

    check_interface( bool_async, &IID_IUnknown );
    check_interface( bool_async, &IID_IInspectable );
    check_interface( bool_async, &IID_IAgileObject );
    check_interface( bool_async, &IID_IAsyncInfo );
    check_interface( bool_async, &IID_IAsyncOperation_boolean );
    check_runtimeclass( bool_async, L"Windows.Foundation.IAsyncOperation`1<Boolean>" );

    hr = IAsyncOperation_boolean_get_Completed( bool_async, &tmp_handler );
    ok( hr == S_OK, "get_Completed returned %#lx\n", hr );
    ok( tmp_handler == NULL, "got handler %p\n", tmp_handler );
    bool_async_handler = default_bool_async_handler;
    bool_async_handler.event = CreateEventW( NULL, FALSE, FALSE, NULL );
    hr = IAsyncOperation_boolean_put_Completed( bool_async, &bool_async_handler.IAsyncOperationCompletedHandler_boolean_iface );
    ok( hr == S_OK, "put_Completed returned %#lx\n", hr );
    ret = WaitForSingleObject( bool_async_handler.event, 1000 );
    ok( !ret, "WaitForSingleObject returned %#lx\n", ret );
    ret = CloseHandle( bool_async_handler.event );
    ok( ret, "CloseHandle failed, error %lu\n", GetLastError() );
    ok( bool_async_handler.invoked, "handler not invoked\n" );
    ok( bool_async_handler.async == bool_async, "got async %p\n", bool_async_handler.async );
    ok( bool_async_handler.status == Completed || broken( bool_async_handler.status == Error ), "got status %u\n", bool_async_handler.status );
    bool_async_handler = default_bool_async_handler;
    hr = IAsyncOperation_boolean_put_Completed( bool_async, &bool_async_handler.IAsyncOperationCompletedHandler_boolean_iface );
    ok( hr == E_ILLEGAL_DELEGATE_ASSIGNMENT, "put_Completed returned %#lx\n", hr );
    ok( !bool_async_handler.invoked, "handler invoked\n" );
    ok( bool_async_handler.async == NULL, "got async %p\n", bool_async_handler.async );
    ok( bool_async_handler.status == Started, "got status %u\n", bool_async_handler.status );

    hr = IAsyncOperation_boolean_QueryInterface( bool_async, &IID_IAsyncInfo, (void **)&async_info );
    ok( hr == S_OK, "QueryInterface returned %#lx\n", hr );
    hr = IAsyncInfo_Cancel( async_info );
    ok( hr == S_OK, "Cancel returned %#lx\n", hr );
    check_bool_async( bool_async, 0, Completed, S_OK, FALSE );
    hr = IAsyncInfo_Close( async_info );
    ok( hr == S_OK, "Close returned %#lx\n", hr );
    check_bool_async( bool_async, 0, 4, S_OK, FALSE );
    IAsyncInfo_Release( async_info );

    IAsyncOperation_boolean_Release( bool_async );

    IKeyCredentialManagerStatics_Release( credentials_statics );
    IActivationFactory_Release( factory );
}

/* -------------------------------------------------------------------------
 * Windows.Security.Cryptography.DataProtection
 * ------------------------------------------------------------------------- */

static const GUID IID_IDataProtectionProvider = {0x09639948, 0xed22, 0x4270, {0xbd, 0x1c, 0x6d, 0x72, 0xc0, 0x0f, 0x87, 0x87}};
static const GUID IID_IDataProtectionProviderFactory = {0xadf33dac, 0x4932, 0x4cdf, {0xac, 0x41, 0x72, 0x14, 0x33, 0x35, 0x14, 0xca}};

typedef struct IDataProtectionProvider IDataProtectionProvider;

typedef struct IDataProtectionProviderVtbl
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IDataProtectionProvider *This, REFIID iid, void **out);
    ULONG (STDMETHODCALLTYPE *AddRef)(IDataProtectionProvider *This);
    ULONG (STDMETHODCALLTYPE *Release)(IDataProtectionProvider *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(IDataProtectionProvider *This, ULONG *count, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(IDataProtectionProvider *This, HSTRING *name);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(IDataProtectionProvider *This, TrustLevel *level);
    HRESULT (STDMETHODCALLTYPE *ProtectAsync)(IDataProtectionProvider *This, IBuffer *data, IAsyncOperation_IBuffer **value);
    HRESULT (STDMETHODCALLTYPE *UnprotectAsync)(IDataProtectionProvider *This, IBuffer *data, IAsyncOperation_IBuffer **value);
    HRESULT (STDMETHODCALLTYPE *ProtectStreamAsync)(IDataProtectionProvider *This, IInputStream *src, IOutputStream *dst, IAsyncAction **value);
    HRESULT (STDMETHODCALLTYPE *UnprotectStreamAsync)(IDataProtectionProvider *This, IInputStream *src, IOutputStream *dst, IAsyncAction **value);
} IDataProtectionProviderVtbl;
/* COBJMACROS-style call macros for the locally declared interfaces. */

#define IDataProtectionProvider_QueryInterface(This,...) (This)->lpVtbl->QueryInterface(This, ##__VA_ARGS__)
#define IDataProtectionProvider_AddRef(This,...) (This)->lpVtbl->AddRef(This, ##__VA_ARGS__)
#define IDataProtectionProvider_Release(This,...) (This)->lpVtbl->Release(This, ##__VA_ARGS__)
#define IDataProtectionProvider_GetIids(This,...) (This)->lpVtbl->GetIids(This, ##__VA_ARGS__)
#define IDataProtectionProvider_GetRuntimeClassName(This,...) (This)->lpVtbl->GetRuntimeClassName(This, ##__VA_ARGS__)
#define IDataProtectionProvider_GetTrustLevel(This,...) (This)->lpVtbl->GetTrustLevel(This, ##__VA_ARGS__)
#define IDataProtectionProvider_ProtectAsync(This,...) (This)->lpVtbl->ProtectAsync(This, ##__VA_ARGS__)
#define IDataProtectionProvider_UnprotectAsync(This,...) (This)->lpVtbl->UnprotectAsync(This, ##__VA_ARGS__)
#define IDataProtectionProvider_ProtectStreamAsync(This,...) (This)->lpVtbl->ProtectStreamAsync(This, ##__VA_ARGS__)
#define IDataProtectionProvider_UnprotectStreamAsync(This,...) (This)->lpVtbl->UnprotectStreamAsync(This, ##__VA_ARGS__)


struct IDataProtectionProvider
{
    const IDataProtectionProviderVtbl *lpVtbl;
};

static ICryptographicBufferStatics *get_cryptobuffer_statics(void)
{
    static const WCHAR *class_name = L"Windows.Security.Cryptography.CryptographicBuffer";
    ICryptographicBufferStatics *statics;
    IActivationFactory *factory;
    HSTRING str;
    HRESULT hr;

    hr = WindowsCreateString( class_name, wcslen( class_name ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&factory );
    WindowsDeleteString( str );
    if (hr != S_OK) return NULL;
    hr = IActivationFactory_QueryInterface( factory, &IID_ICryptographicBufferStatics, (void **)&statics );
    IActivationFactory_Release( factory );
    if (hr != S_OK) return NULL;
    return statics;
}

static IBuffer *create_buffer( const ICryptographicBufferStatics *statics, const BYTE *data, UINT32 size )
{
    IBuffer *buffer = NULL;
    HRESULT hr;

    hr = ICryptographicBufferStatics_CreateFromByteArray( (ICryptographicBufferStatics *)statics, size, (BYTE *)data, &buffer );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    return buffer;
}

static void compare_buffers( const ICryptographicBufferStatics *statics, IBuffer *a, IBuffer *b, boolean expect )
{
    boolean is_equal = !expect;
    HRESULT hr;

    hr = ICryptographicBufferStatics_Compare( (ICryptographicBufferStatics *)statics, a, b, &is_equal );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ok( is_equal == expect, "got equal %u, expected %u.\n", is_equal, expect );
}

static const char *hex_dump( const BYTE *data, UINT32 len )
{
    static char buffers[3][128];
    static int idx;
    char *buf = buffers[idx++ % 3];
    UINT32 i, n = len < 32 ? len : 32;

    for (i = 0; i < n; i++) sprintf( buf + i * 2, "%02x", data[i] );
    buf[n * 2] = 0;
    return buf;
}

static void check_buffer_bytes( unsigned int line, const ICryptographicBufferStatics *statics, IBuffer *buffer,
                                const BYTE *expected, UINT32 size )
{
    UINT32 len = 0;
    BYTE *data = NULL;
    HRESULT hr;

    hr = ICryptographicBufferStatics_CopyToByteArray( (ICryptographicBufferStatics *)statics, buffer, &len, &data );
    ok_(__FILE__, line)( hr == S_OK, "got hr %#lx.\n", hr );
    ok_(__FILE__, line)( len == size, "got size %u, expected %u.\n", len, size );
    if (len == size && data)
        ok_(__FILE__, line)( !memcmp( data, expected, size ), "buffer contents mismatch: %s.\n", hex_dump( data, size ) );
    CoTaskMemFree( data );
}

static void test_Cryptobuffer_Bytes(void)
{
    static const BYTE bytes[] = {0xde, 0xad, 0xbe, 0xef};
    static const BYTE bytes2[] = {0xde, 0xad, 0xbe, 0xee};
    ICryptographicBufferStatics *statics;
    IBuffer *buffer, *buffer2;
    HSTRING str;
    boolean is_equal;
    UINT32 len;
    UINT32 num;
    byte *data;
    HRESULT hr;

    statics = get_cryptobuffer_statics();
    if (!statics)
    {
        win_skip( "Windows.Security.Cryptography.CryptographicBuffer not registered, skipping tests.\n" );
        return;
    }

    hr = ICryptographicBufferStatics_CreateFromByteArray( statics, sizeof(bytes), (BYTE *)bytes, NULL );
    ok( hr == E_POINTER, "got hr %#lx.\n", hr );

    buffer = create_buffer( statics, bytes, sizeof(bytes) );
    len = 0;
    hr = IBuffer_get_Capacity( buffer, &len );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ok( len == sizeof(bytes), "got capacity %u.\n", len );
    len = 0;
    hr = IBuffer_get_Length( buffer, &len );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ok( len == sizeof(bytes), "got length %u.\n", len );

    /* CopyToByteArray roundtrip */
    data = (void *)0xdeadbeef;
    len = 0xdeadbeef;
    hr = ICryptographicBufferStatics_CopyToByteArray( statics, buffer, &len, &data );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ok( len == sizeof(bytes), "got size %u.\n", len );
    ok( data && !memcmp( data, bytes, sizeof(bytes) ), "copied data mismatch.\n" );
    CoTaskMemFree( data );

    hr = ICryptographicBufferStatics_CopyToByteArray( statics, buffer, NULL, &data );
    ok( hr == E_POINTER, "got hr %#lx.\n", hr );

    /* Compare */
    buffer2 = create_buffer( statics, bytes, sizeof(bytes) );
    compare_buffers( statics, buffer, buffer2, TRUE );
    compare_buffers( statics, buffer, buffer, TRUE );
    IBuffer_Release( buffer2 );
    buffer2 = create_buffer( statics, bytes2, sizeof(bytes2) );
    compare_buffers( statics, buffer, buffer2, FALSE );
    IBuffer_Release( buffer2 );

    /* Zero-length buffers compare equal */
    buffer2 = create_buffer( statics, NULL, 0 );
    compare_buffers( statics, buffer2, buffer2, TRUE );
    IBuffer_Release( buffer2 );

    /* GenerateRandom / GenerateRandomNumber */
    buffer2 = NULL;
    hr = ICryptographicBufferStatics_GenerateRandom( statics, 8, &buffer2 );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    len = 0;
    hr = IBuffer_get_Length( buffer2, &len );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ok( len == 8, "got length %u.\n", len );
    IBuffer_Release( buffer2 );

    num = 0;
    hr = ICryptographicBufferStatics_GenerateRandomNumber( statics, &num );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    /* Hex encoding roundtrip */
    buffer2 = NULL;
    hr = WindowsCreateString( L"deadBEEF", 8, &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = ICryptographicBufferStatics_DecodeFromHexString( statics, str, &buffer2 );
    WindowsDeleteString( str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    check_buffer_bytes( __LINE__, statics, buffer2, bytes, sizeof(bytes) );
    IBuffer_Release( buffer2 );

    buffer2 = NULL;
    hr = WindowsCreateString( L"abc", 3, &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = ICryptographicBufferStatics_DecodeFromHexString( statics, str, &buffer2 );
    WindowsDeleteString( str );
    ok( hr == E_INVALIDARG, "got hr %#lx.\n", hr );
    ok( buffer2 == NULL, "got buffer %p.\n", buffer2 );

    hr = ICryptographicBufferStatics_EncodeToHexString( statics, buffer, &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ok( !wcscmp( WindowsGetStringRawBuffer( str, NULL ), L"DEADBEEF" ), "got str %s.\n",
        debugstr_w( WindowsGetStringRawBuffer( str, NULL ) ) );
    WindowsDeleteString( str );

    /* Base64 roundtrip */
    buffer2 = NULL;
    hr = WindowsCreateString( L"AAEC", 4, &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = ICryptographicBufferStatics_DecodeFromBase64String( statics, str, &buffer2 );
    WindowsDeleteString( str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    check_buffer_bytes( __LINE__, statics, buffer2, (const BYTE *)"\x00\x01\x02", 3 );
    IBuffer_Release( buffer2 );

    /* String conversion roundtrip */
    buffer2 = NULL;
    hr = WindowsCreateString( L"abc", 3, &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = ICryptographicBufferStatics_ConvertStringToBinary( statics, str, BinaryStringEncoding_Utf8, &buffer2 );
    WindowsDeleteString( str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    check_buffer_bytes( __LINE__, statics, buffer2, (const BYTE *)"abc", 3 );
    str = NULL;
    hr = ICryptographicBufferStatics_ConvertBinaryToString( statics, BinaryStringEncoding_Utf8, buffer2, &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ok( !wcscmp( WindowsGetStringRawBuffer( str, NULL ), L"abc" ), "got str %s.\n",
        debugstr_w( WindowsGetStringRawBuffer( str, NULL ) ) );
    WindowsDeleteString( str );
    IBuffer_Release( buffer2 );

    IBuffer_Release( buffer );
    ICryptographicBufferStatics_Release( statics );
}

static void test_SymmetricKeyAlgorithmProvider(void)
{
    static const BYTE key_material[16] =
    {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    };
    static const WCHAR *class_name = RuntimeClass_Windows_Security_Cryptography_Core_SymmetricKeyAlgorithmProvider;
    ISymmetricKeyAlgorithmProviderStatics *statics = (void *)0xdeadbeef;
    ISymmetricKeyAlgorithmProvider *provider = (void *)0xdeadbeef;
    ICryptographicBufferStatics *buffer_statics;
    ISymmetricKeyAlgorithmProvider *rc4 = (void *)0xdeadbeef;
    IActivationFactory *factory = (void *)0xdeadbeef;
    ICryptographicKey *key = (void *)0xdeadbeef;
    IBuffer *material, *exported = (void *)0xdeadbeef;
    HSTRING str;
    UINT32 length;
    HRESULT hr;
    LONG ref;

    hr = WindowsCreateString( class_name, wcslen( class_name ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&factory );
    WindowsDeleteString( str );
    ok( hr == S_OK || broken( hr == REGDB_E_CLASSNOTREG ), "got hr %#lx.\n", hr );
    if (hr == REGDB_E_CLASSNOTREG)
    {
        win_skip( "%s runtimeclass not registered, skipping tests.\n", wine_dbgstr_w( class_name ) );
        return;
    }

    check_interface( factory, &IID_IUnknown );
    check_interface( factory, &IID_IInspectable );
    check_interface( factory, &IID_IAgileObject );

    hr = IActivationFactory_QueryInterface( factory, &IID_ISymmetricKeyAlgorithmProviderStatics, (void **)&statics );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    /* Statics-only class */
    hr = IActivationFactory_ActivateInstance( factory, (IInspectable **)&provider );
    ok( hr == E_NOTIMPL, "got hr %#lx.\n", hr );

    hr = WindowsCreateString( L"AES_CBC_PKCS7", 13, &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = ISymmetricKeyAlgorithmProviderStatics_OpenAlgorithm( statics, str, &provider );
    WindowsDeleteString( str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    check_interface( provider, &IID_ISymmetricKeyAlgorithmProvider );
    check_runtimeclass( (IInspectable *)provider, class_name );

    hr = ISymmetricKeyAlgorithmProvider_get_AlgorithmName( provider, &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ok( !wcscmp( WindowsGetStringRawBuffer( str, NULL ), L"AES_CBC_PKCS7" ), "got name %s.\n",
        debugstr_w( WindowsGetStringRawBuffer( str, NULL ) ) );
    WindowsDeleteString( str );

    length = 0;
    hr = ISymmetricKeyAlgorithmProvider_get_BlockLength( provider, &length );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ok( length == 16, "got block length %u.\n", length );

    hr = WindowsCreateString( L"RC4", 3, &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = ISymmetricKeyAlgorithmProviderStatics_OpenAlgorithm( statics, str, &rc4 );
    WindowsDeleteString( str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    length = 0;
    hr = ISymmetricKeyAlgorithmProvider_get_BlockLength( rc4, &length );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ok( length == 1, "got block length %u.\n", length );
    ISymmetricKeyAlgorithmProvider_Release( rc4 );

    /* CreateSymmetricKey */
    buffer_statics = get_cryptobuffer_statics();
    material = create_buffer( buffer_statics, key_material, sizeof(key_material) );

    hr = ISymmetricKeyAlgorithmProvider_CreateSymmetricKey( provider, material, &key );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    check_interface( key, &IID_ICryptographicKey );
    check_runtimeclass( (IInspectable *)key, RuntimeClass_Windows_Security_Cryptography_Core_CryptographicKey );

    length = 0;
    hr = ICryptographicKey_get_KeySize( key, &length );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ok( length == 128, "got key size %u.\n", length );

    hr = ICryptographicKey_ExportDefaultPrivateKeyBlobType( key, &exported );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    check_buffer_bytes( __LINE__, buffer_statics, exported, key_material, sizeof(key_material) );
    IBuffer_Release( exported );

    IBuffer_Release( material );
    ICryptographicBufferStatics_Release( buffer_statics );
    ICryptographicKey_Release( key );
    ISymmetricKeyAlgorithmProvider_Release( provider );

    ref = ISymmetricKeyAlgorithmProviderStatics_Release( statics );
    ok( ref == 1, "got ref %ld.\n", ref );
    ref = IActivationFactory_Release( factory );
    ok( ref == 1, "got ref %ld.\n", ref );
}

static void test_CryptographicEngine(void)
{
    static const BYTE key_material[16] =
    {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    };
    static const BYTE iv[16] =
    {
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
        0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
    };
    static const BYTE plaintext[13] = "Wine Conforms";
    static const BYTE nonce[12] =
    {
        0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b,
    };
    static const BYTE aad[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    static const WCHAR *class_name = RuntimeClass_Windows_Security_Cryptography_Core_CryptographicEngine;
    ISymmetricKeyAlgorithmProviderStatics *prov_statics = (void *)0xdeadbeef;
    ISymmetricKeyAlgorithmProvider *provider = (void *)0xdeadbeef;
    ICryptographicEngineStatics2 *statics2 = (void *)0xdeadbeef;
    ICryptographicEngineStatics *statics = (void *)0xdeadbeef;
    IActivationFactory *symm_factory = (void *)0xdeadbeef;
    IActivationFactory *factory = (void *)0xdeadbeef;
    ICryptographicBufferStatics *buffer_statics;
    IEncryptedAndAuthenticatedData *auth = (void *)0xdeadbeef;
    IAsyncOperation_IBuffer *async = (void *)0xdeadbeef;
    ICryptographicKey *key = (void *)0xdeadbeef;
    IBuffer *data, *encrypted, *decrypted, *signature;
    IBuffer *material, *iv_buf, *nonce_buf, *aad_buf;
    IAsyncInfo *info;
    boolean is_authenticated;
    HSTRING str;
    HRESULT hr;
    LONG ref;

    hr = WindowsCreateString( class_name, wcslen( class_name ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&factory );
    WindowsDeleteString( str );
    ok( hr == S_OK || broken( hr == REGDB_E_CLASSNOTREG ), "got hr %#lx.\n", hr );
    if (hr == REGDB_E_CLASSNOTREG)
    {
        win_skip( "%s runtimeclass not registered, skipping tests.\n", wine_dbgstr_w( class_name ) );
        return;
    }

    check_interface( factory, &IID_IUnknown );
    check_interface( factory, &IID_IInspectable );
    check_interface( factory, &IID_IAgileObject );

    hr = IActivationFactory_QueryInterface( factory, &IID_ICryptographicEngineStatics, (void **)&statics );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IActivationFactory_QueryInterface( factory, &IID_ICryptographicEngineStatics2, (void **)&statics2 );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    /* Statics-only class */
    hr = IActivationFactory_ActivateInstance( factory, (IInspectable **)&statics2 );
    ok( hr == E_NOTIMPL, "got hr %#lx.\n", hr );

    hr = ICryptographicEngineStatics_Encrypt( statics, NULL, NULL, NULL, &data );
    ok( hr == E_POINTER, "got hr %#lx.\n", hr );

    /* Prepare AES_CBC_PKCS7 key */
    hr = WindowsCreateString( RuntimeClass_Windows_Security_Cryptography_Core_SymmetricKeyAlgorithmProvider,
                              wcslen( RuntimeClass_Windows_Security_Cryptography_Core_SymmetricKeyAlgorithmProvider ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&symm_factory );
    WindowsDeleteString( str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IActivationFactory_QueryInterface( symm_factory, &IID_ISymmetricKeyAlgorithmProviderStatics, (void **)&prov_statics );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    hr = WindowsCreateString( L"AES_CBC_PKCS7", 13, &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = ISymmetricKeyAlgorithmProviderStatics_OpenAlgorithm( prov_statics, str, &provider );
    WindowsDeleteString( str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    buffer_statics = get_cryptobuffer_statics();
    material = create_buffer( buffer_statics, key_material, sizeof(key_material) );
    iv_buf = create_buffer( buffer_statics, iv, sizeof(iv) );
    data = create_buffer( buffer_statics, plaintext, sizeof(plaintext) );
    hr = ISymmetricKeyAlgorithmProvider_CreateSymmetricKey( provider, material, &key );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    /* Encrypt pads to the AES block size, Decrypt returns the plaintext */
    encrypted = NULL;
    hr = ICryptographicEngineStatics_Encrypt( statics, key, data, iv_buf, &encrypted );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ok( encrypted != NULL, "got buffer %p.\n", encrypted );
    ok( buffer_length( encrypted ) == 16, "got encrypted length %u.\n", buffer_length( encrypted ) );

    decrypted = NULL;
    hr = ICryptographicEngineStatics_Decrypt( statics, key, encrypted, iv_buf, &decrypted );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    check_buffer_bytes( __LINE__, buffer_statics, decrypted, plaintext, sizeof(plaintext) );
    compare_buffers( buffer_statics, data, decrypted, TRUE );
    IBuffer_Release( decrypted );

    /* Sign / VerifySignature: HMAC-SHA256 produces a 32-byte tag */
    signature = NULL;
    hr = ICryptographicEngineStatics_Sign( statics, key, data, &signature );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ok( buffer_length( signature ) == 32, "got signature length %u.\n", buffer_length( signature ) );

    is_authenticated = FALSE;
    hr = ICryptographicEngineStatics_VerifySignature( statics, key, data, signature, &is_authenticated );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ok( is_authenticated, "signature not authenticated.\n" );

    /* SignHashedData (ICryptographicEngineStatics2) matches Sign */
    encrypted = NULL;
    hr = ICryptographicEngineStatics2_SignHashedData( statics2, key, data, &encrypted );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    compare_buffers( buffer_statics, signature, encrypted, TRUE );

    is_authenticated = FALSE;
    hr = ICryptographicEngineStatics2_VerifySignatureWithHashInput( statics2, key, data, encrypted, &is_authenticated );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ok( is_authenticated, "hashed signature not authenticated.\n" );

    /* SignAsync */
    hr = ICryptographicEngineStatics2_SignAsync( statics2, key, data, &async );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    check_interface( async, &IID_IAsyncOperation_IBuffer );
    check_interface( async, &IID_IAsyncInfo );
    check_runtimeclass( (IInspectable *)async, L"Windows.Foundation.IAsyncOperation`1<Windows.Storage.Streams.IBuffer>" );
    hr = IAsyncOperation_IBuffer_QueryInterface( async, &IID_IAsyncInfo, (void **)&info );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IAsyncInfo_Close( info );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    IAsyncInfo_Release( info );
    decrypted = NULL;
    hr = IAsyncOperation_IBuffer_GetResults( async, &decrypted );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    compare_buffers( buffer_statics, signature, decrypted, TRUE );
    IBuffer_Release( decrypted );
    IAsyncOperation_IBuffer_Release( async );

    /* AES_GCM authenticated encryption */
    hr = WindowsCreateString( L"AES_GCM", 7, &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = ISymmetricKeyAlgorithmProviderStatics_OpenAlgorithm( prov_statics, str, &provider );
    WindowsDeleteString( str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ISymmetricKeyAlgorithmProvider_Release( provider );
    nonce_buf = create_buffer( buffer_statics, nonce, sizeof(nonce) );
    aad_buf = create_buffer( buffer_statics, aad, sizeof(aad) );

    auth = NULL;
    hr = ICryptographicEngineStatics_EncryptAndAuthenticate( statics, key, data, nonce_buf, aad_buf, &auth );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    check_interface( auth, &IID_IEncryptedAndAuthenticatedData );
    encrypted = NULL;
    hr = IEncryptedAndAuthenticatedData_get_EncryptedData( auth, &encrypted );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    signature = NULL;
    hr = IEncryptedAndAuthenticatedData_get_AuthenticationTag( auth, &signature );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    decrypted = NULL;
    hr = ICryptographicEngineStatics_DecryptAndAuthenticate( statics, key, encrypted, nonce_buf, signature, aad_buf, &decrypted );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    check_buffer_bytes( __LINE__, buffer_statics, decrypted, plaintext, sizeof(plaintext) );
    IBuffer_Release( decrypted );
    IBuffer_Release( encrypted );
    IBuffer_Release( signature );
    IEncryptedAndAuthenticatedData_Release( auth );

    IBuffer_Release( nonce_buf );
    IBuffer_Release( aad_buf );
    IBuffer_Release( signature );
    IBuffer_Release( encrypted );
    IBuffer_Release( data );
    IBuffer_Release( iv_buf );
    IBuffer_Release( material );
    ICryptographicBufferStatics_Release( buffer_statics );
    ICryptographicKey_Release( key );
    ISymmetricKeyAlgorithmProvider_Release( provider );
    ISymmetricKeyAlgorithmProviderStatics_Release( prov_statics );
    IActivationFactory_Release( symm_factory );

    ICryptographicEngineStatics2_Release( statics2 );
    ref = ICryptographicEngineStatics_Release( statics );
    ok( ref == 1, "got ref %ld.\n", ref );
    ref = IActivationFactory_Release( factory );
    ok( ref == 1, "got ref %ld.\n", ref );
}

static void test_HashAlgorithmProvider(void)
{
    static const BYTE abc[] = {'a', 'b', 'c'};
    static const BYTE sha256_abc[32] =
    {
        0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea,
        0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
        0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c,
        0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad,
    };
    static const WCHAR *class_name = RuntimeClass_Windows_Security_Cryptography_Core_HashAlgorithmProvider;
    IHashAlgorithmProviderStatics *statics = (void *)0xdeadbeef;
    ICryptographicBufferStatics *buffer_statics;
    IActivationFactory *factory = (void *)0xdeadbeef;
    IHashAlgorithmProvider *provider = (void *)0xdeadbeef;
    IHashComputation *hash = (void *)0xdeadbeef;
    IBuffer *data, *digest, *digest2;
    HSTRING str;
    UINT32 length;
    HRESULT hr;
    LONG ref;

    hr = WindowsCreateString( class_name, wcslen( class_name ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&factory );
    WindowsDeleteString( str );
    ok( hr == S_OK || broken( hr == REGDB_E_CLASSNOTREG ), "got hr %#lx.\n", hr );
    if (hr == REGDB_E_CLASSNOTREG)
    {
        win_skip( "%s runtimeclass not registered, skipping tests.\n", wine_dbgstr_w( class_name ) );
        return;
    }

    check_interface( factory, &IID_IUnknown );
    check_interface( factory, &IID_IInspectable );
    check_interface( factory, &IID_IAgileObject );

    hr = IActivationFactory_QueryInterface( factory, &IID_IHashAlgorithmProviderStatics, (void **)&statics );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    hr = WindowsCreateString( L"SHA256", 6, &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IHashAlgorithmProviderStatics_OpenAlgorithm( statics, str, &provider );
    WindowsDeleteString( str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    check_interface( provider, &IID_IHashAlgorithmProvider );

    length = 0;
    hr = IHashAlgorithmProvider_get_HashLength( provider, &length );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ok( length == 32, "got hash length %u.\n", length );

    buffer_statics = get_cryptobuffer_statics();
    data = create_buffer( buffer_statics, abc, sizeof(abc) );

    digest = NULL;
    hr = IHashAlgorithmProvider_HashData( provider, data, &digest );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    check_buffer_bytes( __LINE__, buffer_statics, digest, sha256_abc, sizeof(sha256_abc) );

    hr = IHashAlgorithmProvider_CreateHash( provider, &hash );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    check_interface( hash, &IID_IHashComputation );
    hr = IHashComputation_Append( hash, data );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    digest2 = NULL;
    hr = IHashComputation_GetValueAndReset( hash, &digest2 );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    compare_buffers( buffer_statics, digest, digest2, TRUE );

    IHashComputation_Release( hash );
    IBuffer_Release( digest2 );
    IBuffer_Release( digest );
    IBuffer_Release( data );
    ICryptographicBufferStatics_Release( buffer_statics );
    IHashAlgorithmProvider_Release( provider );

    ref = IHashAlgorithmProviderStatics_Release( statics );
    ok( ref == 1, "got ref %ld.\n", ref );
    ref = IActivationFactory_Release( factory );
    ok( ref == 1, "got ref %ld.\n", ref );
}

static void test_KeyDerivation(void)
{
    static const BYTE key_material[16] =
    {
        0x0f, 0x0e, 0x0d, 0x0c, 0x0b, 0x0a, 0x09, 0x08,
        0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, 0x00,
    };
    static const BYTE salt[8] = {0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8};
    static const WCHAR *prov_class = RuntimeClass_Windows_Security_Cryptography_Core_KeyDerivationAlgorithmProvider;
    static const WCHAR *params_class = RuntimeClass_Windows_Security_Cryptography_Core_KeyDerivationParameters;
    IKeyDerivationAlgorithmProviderStatics *prov_statics = (void *)0xdeadbeef;
    IKeyDerivationParametersStatics *params_statics = (void *)0xdeadbeef;
    ICryptographicEngineStatics *engine_statics = (void *)0xdeadbeef;
    IKeyDerivationAlgorithmProvider *provider = (void *)0xdeadbeef;
    IKeyDerivationParameters *params = (void *)0xdeadbeef;
    ICryptographicBufferStatics *buffer_statics;
    IActivationFactory *factory = (void *)0xdeadbeef;
    ICryptographicKey *key = (void *)0xdeadbeef;
    IBuffer *material, *salt_buf, *derived;
    HSTRING str;
    UINT32 count;
    HRESULT hr;

    /* KeyDerivationAlgorithmProvider.OpenAlgorithm + CreateKey */
    hr = WindowsCreateString( prov_class, wcslen( prov_class ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&factory );
    WindowsDeleteString( str );
    ok( hr == S_OK || broken( hr == REGDB_E_CLASSNOTREG ), "got hr %#lx.\n", hr );
    if (hr == REGDB_E_CLASSNOTREG)
    {
        win_skip( "%s runtimeclass not registered, skipping tests.\n", wine_dbgstr_w( prov_class ) );
        return;
    }
    check_interface( factory, &IID_IInspectable );
    hr = IActivationFactory_QueryInterface( factory, &IID_IKeyDerivationAlgorithmProviderStatics, (void **)&prov_statics );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    IActivationFactory_Release( factory );

    hr = WindowsCreateString( L"PBKDF2_SHA256", 13, &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IKeyDerivationAlgorithmProviderStatics_OpenAlgorithm( prov_statics, str, &provider );
    WindowsDeleteString( str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    check_interface( provider, &IID_IKeyDerivationAlgorithmProvider );
    hr = IKeyDerivationAlgorithmProvider_get_AlgorithmName( provider, &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ok( !wcscmp( WindowsGetStringRawBuffer( str, NULL ), L"PBKDF2_SHA256" ), "got name %s.\n",
        debugstr_w( WindowsGetStringRawBuffer( str, NULL ) ) );
    WindowsDeleteString( str );

    buffer_statics = get_cryptobuffer_statics();
    material = create_buffer( buffer_statics, key_material, sizeof(key_material) );
    salt_buf = create_buffer( buffer_statics, salt, sizeof(salt) );

    hr = IKeyDerivationAlgorithmProvider_CreateKey( provider, material, &key );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    check_interface( key, &IID_ICryptographicKey );

    /* KeyDerivationParameters.BuildForPbkdf2 */
    hr = WindowsCreateString( params_class, wcslen( params_class ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&factory );
    WindowsDeleteString( str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IActivationFactory_QueryInterface( factory, &IID_IKeyDerivationParametersStatics, (void **)&params_statics );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    IActivationFactory_Release( factory );

    hr = IKeyDerivationParametersStatics_BuildForPbkdf2( params_statics, salt_buf, 1000, &params );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    check_interface( params, &IID_IKeyDerivationParameters );
    count = 0;
    hr = IKeyDerivationParameters_get_IterationCount( params, &count );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ok( count == 1000, "got iteration count %u.\n", count );

    /* CryptographicEngine.DeriveKeyMaterial produces the requested key size */
    hr = WindowsCreateString( RuntimeClass_Windows_Security_Cryptography_Core_CryptographicEngine,
                              wcslen( RuntimeClass_Windows_Security_Cryptography_Core_CryptographicEngine ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&factory );
    WindowsDeleteString( str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IActivationFactory_QueryInterface( factory, &IID_ICryptographicEngineStatics, (void **)&engine_statics );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    IActivationFactory_Release( factory );

    derived = NULL;
    hr = ICryptographicEngineStatics_DeriveKeyMaterial( engine_statics, key, params, 32, &derived );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    count = 0;
    hr = IBuffer_get_Length( derived, &count );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ok( count == 32, "got length %u.\n", count );

    IBuffer_Release( derived );
    ICryptographicEngineStatics_Release( engine_statics );
    IKeyDerivationParameters_Release( params );
    IKeyDerivationParametersStatics_Release( params_statics );
    ICryptographicKey_Release( key );
    IBuffer_Release( salt_buf );
    IBuffer_Release( material );
    ICryptographicBufferStatics_Release( buffer_statics );
    IKeyDerivationAlgorithmProvider_Release( provider );
    IKeyDerivationAlgorithmProviderStatics_Release( prov_statics );
}

struct buffer_async_handler
{
    __FIAsyncOperationCompletedHandler_1_Windows__CStorage__CStreams__CIBuffer __FIAsyncOperationCompletedHandler_1_Windows__CStorage__CStreams__CIBuffer_iface;
    boolean *invoked;
    AsyncStatus status;
};

static inline struct buffer_async_handler *impl_from_handler(
        __FIAsyncOperationCompletedHandler_1_Windows__CStorage__CStreams__CIBuffer *iface )
{
    return CONTAINING_RECORD( iface, struct buffer_async_handler, __FIAsyncOperationCompletedHandler_1_Windows__CStorage__CStreams__CIBuffer_iface );
}

static HRESULT WINAPI buffer_async_handler_QueryInterface( __FIAsyncOperationCompletedHandler_1_Windows__CStorage__CStreams__CIBuffer *iface,
        REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID___FIAsyncOperationCompletedHandler_1_Windows__CStorage__CStreams__CIBuffer ))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI buffer_async_handler_AddRef( __FIAsyncOperationCompletedHandler_1_Windows__CStorage__CStreams__CIBuffer *iface ) { return 2; }
static ULONG WINAPI buffer_async_handler_Release( __FIAsyncOperationCompletedHandler_1_Windows__CStorage__CStreams__CIBuffer *iface ) { return 1; }

static HRESULT WINAPI buffer_async_handler_Invoke( __FIAsyncOperationCompletedHandler_1_Windows__CStorage__CStreams__CIBuffer *iface,
        IAsyncOperation_IBuffer *async, AsyncStatus status )
{
    struct buffer_async_handler *impl = impl_from_handler( iface );

    *impl->invoked = TRUE;
    impl->status = status;
    return S_OK;
}

static const __FIAsyncOperationCompletedHandler_1_Windows__CStorage__CStreams__CIBufferVtbl buffer_async_completed_handler_vtbl =
{
    /*** IUnknown methods ***/
    buffer_async_handler_QueryInterface,
    buffer_async_handler_AddRef,
    buffer_async_handler_Release,
    /*** IAsyncOperationCompletedHandler<IBuffer> methods ***/
    buffer_async_handler_Invoke,
};

static void test_DataProtectionProvider(void)
{
    static const BYTE payload[] = "protect me";
    static const WCHAR *class_name = L"Windows.Security.Cryptography.DataProtection.DataProtectionProvider";
    IDataProtectionProvider *provider = (void *)0xdeadbeef;
    IAsyncOperation_IBuffer *async = (void *)0xdeadbeef;
    IActivationFactory *factory = (void *)0xdeadbeef;
    ICryptographicBufferStatics *buffer_statics;
    struct buffer_async_handler handler;
    IAsyncAction *action = (void *)0xdeadbeef;
    IInspectable *inspectable = (void *)0xdeadbeef;
    IAsyncInfo *info;
    IBuffer *buffer, *protected_buf, *unprotected_buf;
    HSTRING str;
    HRESULT hr;
    LONG ref;
    boolean invoked;

    hr = WindowsCreateString( class_name, wcslen( class_name ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = RoActivateInstance( str, &inspectable );
    WindowsDeleteString( str );
    ok( hr == S_OK || broken( hr == REGDB_E_CLASSNOTREG ), "got hr %#lx.\n", hr );
    if (hr != S_OK)
    {
        win_skip( "%s runtimeclass not registered, skipping tests.\n", wine_dbgstr_w( class_name ) );
        return;
    }

    check_runtimeclass( inspectable, class_name );
    check_interface( inspectable, &IID_IUnknown );
    check_interface( inspectable, &IID_IInspectable );
    check_interface( inspectable, &IID_IAgileObject );
    check_interface( inspectable, &IID_IDataProtectionProvider );
    check_nointerface( inspectable, &IID_IAsyncAction );

    hr = IInspectable_QueryInterface( inspectable, &IID_IDataProtectionProvider, (void **)&provider );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    IInspectable_Release( inspectable );

    hr = WindowsCreateString( class_name, wcslen( class_name ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&factory );
    WindowsDeleteString( str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    check_interface( factory, &IID_IDataProtectionProviderFactory );
    check_nointerface( factory, &IID_IDataProtectionProvider );

    buffer_statics = get_cryptobuffer_statics();
    buffer = create_buffer( buffer_statics, payload, sizeof(payload) );

    /* ProtectAsync is a passthrough for the local security descriptor */
    protected_buf = NULL;
    hr = IDataProtectionProvider_ProtectAsync( provider, buffer, &async );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    check_interface( async, &IID_IAsyncOperation_IBuffer );
    check_interface( async, &IID_IAsyncInfo );
    check_runtimeclass( (IInspectable *)async, L"Windows.Foundation.IAsyncOperation`1<Windows.Storage.Streams.IBuffer>" );

    hr = IAsyncOperation_IBuffer_QueryInterface( async, &IID_IAsyncInfo, (void **)&info );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IAsyncInfo_Close( info );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    IAsyncInfo_Release( info );

    hr = IAsyncOperation_IBuffer_GetResults( async, &protected_buf );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ok( protected_buf != NULL, "got buffer %p.\n", protected_buf );
    compare_buffers( buffer_statics, buffer, protected_buf, TRUE );
    IAsyncOperation_IBuffer_Release( async );

    /* Completed handler is invoked for completed operations */
    async = NULL;
    invoked = FALSE;
    handler.__FIAsyncOperationCompletedHandler_1_Windows__CStorage__CStreams__CIBuffer_iface.lpVtbl = &buffer_async_completed_handler_vtbl;
    handler.invoked = &invoked;
    handler.status = (AsyncStatus)-1;
    hr = IDataProtectionProvider_ProtectAsync( provider, buffer, &async );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IAsyncOperation_IBuffer_put_Completed( async, &handler.__FIAsyncOperationCompletedHandler_1_Windows__CStorage__CStreams__CIBuffer_iface );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ok( invoked, "handler not invoked.\n" );
    IAsyncOperation_IBuffer_Release( async );

    /* UnprotectAsync roundtrip */
    unprotected_buf = NULL;
    async = NULL;
    hr = IDataProtectionProvider_UnprotectAsync( provider, protected_buf, &async );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IAsyncOperation_IBuffer_GetResults( async, &unprotected_buf );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    compare_buffers( buffer_statics, buffer, unprotected_buf, TRUE );
    IAsyncOperation_IBuffer_Release( async );
    IBuffer_Release( unprotected_buf );

    hr = IDataProtectionProvider_ProtectAsync( provider, NULL, &async );
    ok( hr == E_INVALIDARG, "got hr %#lx.\n", hr );

    /* Stream variants return IAsyncAction */
    action = NULL;
    hr = IDataProtectionProvider_ProtectStreamAsync( provider, NULL, NULL, &action );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    check_interface( action, &IID_IAsyncAction );
    hr = IAsyncAction_GetResults( action );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    IAsyncAction_Release( action );

    action = NULL;
    hr = IDataProtectionProvider_UnprotectStreamAsync( provider, NULL, NULL, &action );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    check_interface( action, &IID_IAsyncAction );
    IAsyncAction_Release( action );

    IBuffer_Release( protected_buf );
    IBuffer_Release( buffer );
    ICryptographicBufferStatics_Release( buffer_statics );
    ref = IDataProtectionProvider_Release( provider );
    ok( ref == 0, "got ref %ld.\n", ref );
    ref = IActivationFactory_Release( factory );
    ok( ref == 1, "got ref %ld.\n", ref );
}

START_TEST(crypto)
{
    HRESULT hr;

    hr = RoInitialize( RO_INIT_MULTITHREADED );
    ok( hr == S_OK, "RoInitialize failed, hr %#lx\n", hr );

    if (!load_combase_functions())
    {
        RoUninitialize();
        return;
    }

    test_CryptobufferStatics();
    test_Credentials_Statics();
    test_Cryptobuffer_Bytes();
    test_SymmetricKeyAlgorithmProvider();
    test_CryptographicEngine();
    test_HashAlgorithmProvider();
    test_KeyDerivation();
    test_DataProtectionProvider();

    RoUninitialize();
}
