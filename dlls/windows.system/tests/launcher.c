/*
 * Windows.System.Launcher tests
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

#include <stdarg.h>
#include <stdio.h>

#define COBJMACROS
#include "windef.h"
#include "winbase.h"
#include "winstring.h"
#include "winreg.h"

#include "activation.h"
#include "asyncinfo.h"
#include "roapi.h"

#define WIDL_using_Windows_Foundation
#define WIDL_using_Windows_Foundation_Collections
#include "windows.foundation.h"
#include "windows.foundation.collections.h"

#include "wine/test.h"

/* {277151c3-9e3e-42f6-91a4-5dfdeb232451} */
static const IID IID_ILauncherStatics =
    { 0x277151c3, 0x9e3e, 0x42f6, { 0x91, 0xa4, 0x5d, 0xfd, 0xeb, 0x23, 0x24, 0x51 } };
/* {59ba2fbb-24cb-4c02-a4c4-8294569d54f1} */
static const IID IID_ILauncherStatics2 =
    { 0x59ba2fbb, 0x24cb, 0x4c02, { 0xa4, 0xc4, 0x82, 0x94, 0x56, 0x9d, 0x54, 0xf1 } };
/* {234261a8-9db3-4683-aa42-dc6f51d33847} */
static const IID IID_ILauncherStatics3 =
    { 0x234261a8, 0x9db3, 0x4683, { 0xaa, 0x42, 0xdc, 0x6f, 0x51, 0xd3, 0x38, 0x47 } };
/* {cdb5efb3-5788-509d-9be1-71ccb8a3362a} __FIAsyncOperation_1_boolean */
static const IID IID_op_bool =
    { 0xcdb5efb3, 0x5788, 0x509d, { 0x9b, 0xe1, 0x71, 0xcc, 0xb8, 0xa3, 0x36, 0x2a } };
/* {a2d13cfe-9746-5caa-b01d-bbe364ad2930} __FIAsyncOperation_1_LaunchQuerySupportStatus (pids) */
static const IID IID_op_lqss =
    { 0xa2d13cfe, 0x9746, 0x5caa, { 0xb0, 0x1d, 0xbb, 0xe3, 0x64, 0xad, 0x29, 0x30 } };
/* {12345678-1234-1234-1234-1234567890ab} */
static const IID IID_random =
    { 0x12345678, 0x1234, 0x1234, { 0x12, 0x34, 0x12, 0x34, 0x56, 0x78, 0x90, 0xab } };

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

/*
 * The ILauncherStatics{,2,3} faces are not in wine's windows.system.h; call them through
 * vtable slots (prologue 0-5).  The completed operations' faces (IAsyncOperation`1<...>) are
 * QI-able opaque objects: put_Completed slot 6, GetResults slot 8.
 */
typedef struct { const void **lpVtbl; } ILauncherStatics;

#define CALL_STATICS( s, n, ... ) \
    ((HRESULT (STDMETHODCALLTYPE *)( void *, ... ))((const void ***)( s ))[0][n])( (void *)(s), __VA_ARGS__ )
#define OP_CALL( op, n, ... ) \
    ((HRESULT (STDMETHODCALLTYPE *)( void *, ... ))((const void ***)( op ))[0][n])( (void *)(op), __VA_ARGS__ )

/* ILauncherStatics: 6 LaunchFileAsync, 8 LaunchUriAsync; ILauncherStatics2: 9
 * QueryUriSupportAsync; ILauncherStatics3: 6 LaunchFolderAsync, 8 LaunchFolderPathAsync. */

static void check_interface_( unsigned int line, void *obj, const IID *iid, BOOL supported )
{
    IUnknown *iface = obj;
    IUnknown *unk;
    HRESULT hr;

    hr = IUnknown_QueryInterface( iface, iid, (void **)&unk );
    ok_(__FILE__, line)( hr == S_OK || (!supported && hr == E_NOINTERFACE), "got hr %#lx.\n", hr );
    if (SUCCEEDED(hr))
        IUnknown_Release( unk );
}
#define check_interface( obj, iid, supported ) check_interface_( __LINE__, obj, iid, supported )

static HRESULT make_uri( const WCHAR *uri, IUriRuntimeClass **out )
{
    IUriRuntimeClassFactory *uri_factory;
    HRESULT hr;
    HSTRING str;

    hr = WindowsCreateString( L"Windows.Foundation.Uri", 22, &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = RoGetActivationFactory( str, &IID_IUriRuntimeClassFactory, (void **)&uri_factory );
    WindowsDeleteString( str );
    if (FAILED(hr)) return hr;

    hr = WindowsCreateString( uri, lstrlenW( uri ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IUriRuntimeClassFactory_CreateUri( uri_factory, str, out );
    WindowsDeleteString( str );
    IUriRuntimeClassFactory_Release( uri_factory );
    return hr;
}

static ULONG invoked_count;
static enum AsyncStatus invoked_status;

static HRESULT WINAPI handler_QueryInterface( void *iface, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI handler_AddRef( void *iface ) { return 2; }
static ULONG WINAPI handler_Release( void *iface ) { return 1; }

static HRESULT WINAPI handler_Invoke( void *iface, void *async, enum AsyncStatus status )
{
    invoked_count++;
    invoked_status = status;
    return S_OK;
}

static const void *handler_vtbl[] =
{
    handler_QueryInterface,
    handler_AddRef,
    handler_Release,
    handler_Invoke,
};

static struct { const void **vtbl; } completed_handler = { handler_vtbl };

static void check_op_completed( IAsyncInfo *info )
{
    enum AsyncStatus status;
    HRESULT hr;

    hr = IAsyncInfo_get_Status( info, &status );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ok( status == Completed, "got status %d.\n", status );
}

static void test_launcher( void )
{
    IAsyncInfo *info;
    IUriRuntimeClass *uri;
    IActivationFactory *factory;
    IInspectable *instance;
    ILauncherStatics *statics, *statics2, *statics3;
    HRESULT hr;
    HSTRING str;
    void *op;
    boolean result;
    INT32 status;
    WCHAR path[MAX_PATH], command[MAX_PATH * 2];
    HKEY key;

    hr = WindowsCreateString( L"Windows.System.Launcher", 23, &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = RoGetActivationFactory( str, &IID_ILauncherStatics, (void **)&statics );
    WindowsDeleteString( str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    if (FAILED(hr)) return;

    check_interface( statics, &IID_IInspectable, TRUE );
    check_interface( statics, &IID_IAgileObject, TRUE );
    check_interface( statics, &IID_ILauncherStatics, TRUE );
    check_interface( statics, &IID_ILauncherStatics2, TRUE );
    check_interface( statics, &IID_ILauncherStatics3, TRUE );
    check_interface( statics, &IID_random, FALSE );

    /* the factory face answers IActivationFactory, ActivateInstance is not supported */
    hr = IUnknown_QueryInterface( (IUnknown *)statics, &IID_IActivationFactory, (void **)&factory );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    if (SUCCEEDED(hr))
    {
        hr = IActivationFactory_ActivateInstance( factory, &instance );
        ok( hr == E_NOTIMPL, "got hr %#lx.\n", hr );
        IActivationFactory_Release( factory );
    }

    /* LaunchUriAsync(NULL) */
    op = NULL;
    hr = CALL_STATICS( statics, 8, NULL, &op );
    ok( hr == E_INVALIDARG, "got hr %#lx.\n", hr );

    hr = IUnknown_QueryInterface( (IUnknown *)statics, &IID_ILauncherStatics2, (void **)&statics2 );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    /* QueryUriSupportAsync: https -> Available */
    hr = make_uri( L"https://example.com", &uri );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    op = NULL;
    hr = CALL_STATICS( statics2, 9, uri, (INT32)0, &op );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    IUriRuntimeClass_Release( uri );
    check_interface( op, &IID_op_lqss, TRUE );
    status = -1;
    hr = OP_CALL( op, 8, &status );
    ok( hr == S_OK && status == LaunchQuerySupportStatus_Available,
        "got hr %#lx, status %d.\n", hr, status );
    hr = IUnknown_QueryInterface( (IUnknown *)op, &IID_IAsyncInfo, (void **)&info );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    check_op_completed( info );
    IAsyncInfo_Release( info );
    IUnknown_Release( (IUnknown *)op );

    /* QueryUriSupportAsync: unregistered scheme -> NotSupported */
    hr = make_uri( L"nonexistent-scheme-xyz://foo", &uri );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    status = -1;
    op = NULL;
    hr = CALL_STATICS( statics2, 9, uri, (INT32)0, &op );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    IUriRuntimeClass_Release( uri );
    hr = OP_CALL( op, 8, &status );
    ok( hr == S_OK && status == LaunchQuerySupportStatus_NotSupported,
        "got hr %#lx, status %d.\n", hr, status );
    IUnknown_Release( (IUnknown *)op );

    op = NULL;
    hr = CALL_STATICS( statics2, 9, NULL, (INT32)0, &op );
    ok( hr == E_INVALIDARG, "got hr %#lx.\n", hr );

    /* LaunchUriAsync through a registered custom scheme: cmd.exe writes a marker file,
     * proving the ShellExecuteW handoff end to end without popping an interactive browser. */
    GetTempPathW( ARRAYSIZE( path ), path );
    wcscat( path, L"wine_launcher_marker.txt" );
    DeleteFileW( path );

    if (!RegCreateKeyExW( HKEY_LOCAL_MACHINE, L"Software\\Classes\\https\\shell\\open\\command",
                          0, NULL, 0, KEY_WRITE, NULL, &key, NULL ))
    {
        const WCHAR cmd_fmt[] = L"\"cmd.exe\" /c echo marker > \"%s\"";
        wsprintfW( command, cmd_fmt, path );
        RegSetValueExW( key, NULL, 0, REG_SZ, (const BYTE *)command,
                        (lstrlenW( command ) + 1) * sizeof(WCHAR) );
        RegCloseKey( key );

        hr = make_uri( L"https://example.com/probe", &uri );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        op = NULL;
        hr = CALL_STATICS( statics, 8, uri, &op );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        IUriRuntimeClass_Release( uri );
        check_interface( op, &IID_op_bool, TRUE );

        invoked_count = 0;
        hr = OP_CALL( op, 6, &completed_handler );
        ok( hr == S_OK, "put_Completed: got hr %#lx.\n", hr );
        ok( invoked_count == 1, "got invoked_count %lu.\n", invoked_count );
        ok( invoked_status == Completed, "got status %d.\n", invoked_status );

        result = FALSE;
        hr = OP_CALL( op, 8, &result );
        ok( hr == S_OK && result == TRUE, "got hr %#lx, result %d.\n", hr, result );
        IUnknown_Release( (IUnknown *)op );

        for (int i = 0; i < 50; i++)
        {
            if (GetFileAttributesW( path ) != INVALID_FILE_ATTRIBUTES) break;
            Sleep( 100 );
        }
        ok( GetFileAttributesW( path ) != INVALID_FILE_ATTRIBUTES, "marker file %s missing.\n",
            wine_dbgstr_w( path ) );
        DeleteFileW( path );
        RegDeleteTreeW( HKEY_LOCAL_MACHINE, L"Software\\Classes\\https" );
    }
    else
        win_skip( "cannot register test URI scheme.\n" );

    /* LaunchFolderPathAsync on the Windows directory (opens the file manager) */
    hr = IUnknown_QueryInterface( (IUnknown *)statics, &IID_ILauncherStatics3, (void **)&statics3 );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    GetWindowsDirectoryW( path, ARRAYSIZE( path ) );
    hr = WindowsCreateString( path, lstrlenW( path ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    op = NULL;
    hr = CALL_STATICS( statics3, 8, str, &op );
    WindowsDeleteString( str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    result = FALSE;
    hr = OP_CALL( op, 8, &result );
    ok( hr == S_OK && result == TRUE, "got hr %#lx, result %d.\n", hr, result );
    IUnknown_Release( (IUnknown *)op );

    op = NULL;
    hr = CALL_STATICS( statics3, 8, NULL, &op );
    ok( hr == E_INVALIDARG, "got hr %#lx.\n", hr );

    IUnknown_Release( (IUnknown *)statics );
}

START_TEST( launcher )
{
    RoInitialize( RO_INIT_MULTITHREADED );
    test_launcher();
}
