/*
 * WinRT Windows.Networking.Sockets conformance tests
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
#include "initguid.h"
#include <stdarg.h>
#include <string.h>

#include "windef.h"
#include "winbase.h"
#include "winstring.h"

#include "roapi.h"
#include "activation.h"

#define WIDL_using_Windows_Foundation
#include "windows.foundation.h"
#define WIDL_using_Windows_Storage_Streams
#include "windows.storage.streams.h"

#include "wine/test.h"

static const GUID IID_sock_IClosable = {0x3028b027, 0x6029, 0x47e6, {0x81, 0xa1, 0x65, 0x5b, 0x3b, 0x3c, 0x52, 0x0f}};
static const GUID IID_IStreamSocket = {0x69a22cf3, 0xfc7b, 0x4857, {0xaf, 0x38, 0xf6, 0xe7, 0xde, 0x6a, 0x5b, 0x49}};
static const GUID IID_IStreamSocket2 = {0x29d0e575, 0xf314, 0x4d09, {0xad, 0xf0, 0x0f, 0xbd, 0x96, 0x7f, 0xbd, 0x9f}};
static const GUID IID_IStreamSocket3 = {0x3f430b00, 0x9d28, 0x4854, {0xba, 0xc3, 0x23, 0x01, 0x94, 0x1e, 0xc2, 0x23}};
static const GUID IID_IStreamSocketStatics = {0xa420bc4a, 0x6e2e, 0x4af5, {0xb5, 0x56, 0x35, 0x5a, 0xe0, 0xcd, 0x4f, 0x29}};
static const GUID IID_IStreamSocketControl = {0xfe25adf1, 0x92ab, 0x4af3, {0x99, 0x92, 0x0f, 0x4c, 0x85, 0xe3, 0x6c, 0xc4}};
static const GUID IID_IStreamSocketInformation = {0x3b80ae30, 0x5e68, 0x4205, {0x88, 0xf0, 0xdc, 0x85, 0xd2, 0xe2, 0x5d, 0xed}};
static const GUID IID_IMessageWebSocket = {0x33727d08, 0x34d5, 0x4746, {0xad, 0x7b, 0x8d, 0xde, 0x5b, 0xc2, 0xef, 0x88}};
static const GUID IID_IMessageWebSocket2 = {0xbed0cee7, 0xf9c8, 0x440a, {0x9a, 0xd5, 0x73, 0x72, 0x81, 0xd9, 0x74, 0x2e}};
static const GUID IID_IMessageWebSocket3 = {0x59d9defb, 0x71af, 0x4349, {0x84, 0x87, 0x91, 0x1f, 0xcf, 0x68, 0x15, 0x97}};
static const GUID IID_IMessageWebSocketControl = {0x8118388a, 0xc629, 0x4f0a, {0x80, 0xfb, 0x81, 0xfc, 0x05, 0x53, 0x88, 0x62}};
static const GUID IID_IWebSocket = {0xf877396f, 0x99b1, 0x4e18, {0xbc, 0x08, 0x85, 0x0c, 0x9a, 0xdf, 0x15, 0x6e}};
static const GUID IID_IWebSocketControl = {0x2ec4bdc3, 0xd9a5, 0x455a, {0x98, 0x11, 0xde, 0x24, 0xd4, 0x53, 0x37, 0xe9}};
static const GUID IID_IWebSocketInformation = {0x5e01e316, 0xc92a, 0x47a5, {0xb2, 0x5f, 0x07, 0x84, 0x76, 0x39, 0xd1, 0x81}};

/* Local declarations of the Windows.Networking.Sockets interface surface,
 * following the widl-generated ABI of the Windows SDK metadata. */

typedef struct IStreamSocket IStreamSocket;

typedef struct IStreamSocketVtbl
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IStreamSocket *This, REFIID iid, void **out);
    ULONG (STDMETHODCALLTYPE *AddRef)(IStreamSocket *This);
    ULONG (STDMETHODCALLTYPE *Release)(IStreamSocket *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(IStreamSocket *This, ULONG *count, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(IStreamSocket *This, HSTRING *name);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(IStreamSocket *This, TrustLevel *level);
    HRESULT (STDMETHODCALLTYPE *ConnectAsync_ep_host_svc)(IStreamSocket *This, void *endpoint_pair, HSTRING host, HSTRING service, IAsyncAction **op);
    HRESULT (STDMETHODCALLTYPE *ConnectAsync_host_svc)(IStreamSocket *This, void *host, HSTRING service, IAsyncAction **op);
    HRESULT (STDMETHODCALLTYPE *ConnectAsync_ep_host_svc_prot)(IStreamSocket *This, void *endpoint_pair, void *host, HSTRING service, INT32 prot, IAsyncAction **op);
    HRESULT (STDMETHODCALLTYPE *ConnectAsync_ep_prot)(IStreamSocket *This, void *endpoint_pair, INT32 prot, IAsyncAction **op);
    HRESULT (STDMETHODCALLTYPE *UpgradeToSslAsync)(IStreamSocket *This, INT32 prot, void *host, IAsyncAction **op);
    HRESULT (STDMETHODCALLTYPE *get_Control)(IStreamSocket *This, void **value);
    HRESULT (STDMETHODCALLTYPE *get_Information)(IStreamSocket *This, void **value);
    HRESULT (STDMETHODCALLTYPE *get_InputStream)(IStreamSocket *This, IInputStream **value);
    HRESULT (STDMETHODCALLTYPE *get_OutputStream)(IStreamSocket *This, IOutputStream **value);
} IStreamSocketVtbl;

struct IStreamSocket
{
    const IStreamSocketVtbl *lpVtbl;
};

typedef struct IStreamSocketStatics IStreamSocketStatics;

typedef struct IStreamSocketStaticsVtbl
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IStreamSocketStatics *This, REFIID iid, void **out);
    ULONG (STDMETHODCALLTYPE *AddRef)(IStreamSocketStatics *This);
    ULONG (STDMETHODCALLTYPE *Release)(IStreamSocketStatics *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(IStreamSocketStatics *This, ULONG *count, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(IStreamSocketStatics *This, HSTRING *name);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(IStreamSocketStatics *This, TrustLevel *level);
    HRESULT (STDMETHODCALLTYPE *GetEndpointPairsAsync)(IStreamSocketStatics *This, void *host, HSTRING service, IInspectable **op);
} IStreamSocketStaticsVtbl;

struct IStreamSocketStatics
{
    const IStreamSocketStaticsVtbl *lpVtbl;
};

typedef struct IStreamSocketControl IStreamSocketControl;

typedef struct IStreamSocketControlVtbl
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IStreamSocketControl *This, REFIID iid, void **out);
    ULONG (STDMETHODCALLTYPE *AddRef)(IStreamSocketControl *This);
    ULONG (STDMETHODCALLTYPE *Release)(IStreamSocketControl *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(IStreamSocketControl *This, ULONG *count, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(IStreamSocketControl *This, HSTRING *name);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(IStreamSocketControl *This, TrustLevel *level);
    HRESULT (STDMETHODCALLTYPE *get_KeepAlive)(IStreamSocketControl *This, boolean *value);
    HRESULT (STDMETHODCALLTYPE *put_KeepAlive)(IStreamSocketControl *This, boolean value);
} IStreamSocketControlVtbl;

struct IStreamSocketControl
{
    const IStreamSocketControlVtbl *lpVtbl;
};

typedef struct IStreamSocketInformation IStreamSocketInformation;

typedef struct IStreamSocketInformationVtbl
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IStreamSocketInformation *This, REFIID iid, void **out);
    ULONG (STDMETHODCALLTYPE *AddRef)(IStreamSocketInformation *This);
    ULONG (STDMETHODCALLTYPE *Release)(IStreamSocketInformation *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(IStreamSocketInformation *This, ULONG *count, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(IStreamSocketInformation *This, HSTRING *name);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(IStreamSocketInformation *This, TrustLevel *level);
    HRESULT (STDMETHODCALLTYPE *get_BandwidthStatistics)(IStreamSocketInformation *This, void **value);
    HRESULT (STDMETHODCALLTYPE *get_LocalAddress)(IStreamSocketInformation *This, void **value);
    HRESULT (STDMETHODCALLTYPE *get_LocalPort)(IStreamSocketInformation *This, HSTRING *value);
} IStreamSocketInformationVtbl;

struct IStreamSocketInformation
{
    const IStreamSocketInformationVtbl *lpVtbl;
};

typedef struct IMessageWebSocket IMessageWebSocket;

typedef struct IMessageWebSocketVtbl
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IMessageWebSocket *This, REFIID iid, void **out);
    ULONG (STDMETHODCALLTYPE *AddRef)(IMessageWebSocket *This);
    ULONG (STDMETHODCALLTYPE *Release)(IMessageWebSocket *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(IMessageWebSocket *This, ULONG *count, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(IMessageWebSocket *This, HSTRING *name);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(IMessageWebSocket *This, TrustLevel *level);
    HRESULT (STDMETHODCALLTYPE *get_Control)(IMessageWebSocket *This, void **value);
    HRESULT (STDMETHODCALLTYPE *get_Information)(IMessageWebSocket *This, void **value);
    HRESULT (STDMETHODCALLTYPE *add_MessageReceived)(IMessageWebSocket *This, void *handler, INT64 *token);
    HRESULT (STDMETHODCALLTYPE *remove_MessageReceived)(IMessageWebSocket *This, INT64 token);
} IMessageWebSocketVtbl;

struct IMessageWebSocket
{
    const IMessageWebSocketVtbl *lpVtbl;
};

typedef struct IMessageWebSocketControl IMessageWebSocketControl;

typedef struct IMessageWebSocketControlVtbl
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IMessageWebSocketControl *This, REFIID iid, void **out);
    ULONG (STDMETHODCALLTYPE *AddRef)(IMessageWebSocketControl *This);
    ULONG (STDMETHODCALLTYPE *Release)(IMessageWebSocketControl *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(IMessageWebSocketControl *This, ULONG *count, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(IMessageWebSocketControl *This, HSTRING *name);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(IMessageWebSocketControl *This, TrustLevel *level);
    HRESULT (STDMETHODCALLTYPE *get_MaxMessageSize)(IMessageWebSocketControl *This, UINT32 *value);
} IMessageWebSocketControlVtbl;

struct IMessageWebSocketControl
{
    const IMessageWebSocketControlVtbl *lpVtbl;
};

typedef struct IWebSocket IWebSocket;

typedef struct IWebSocketVtbl
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IWebSocket *This, REFIID iid, void **out);
    ULONG (STDMETHODCALLTYPE *AddRef)(IWebSocket *This);
    ULONG (STDMETHODCALLTYPE *Release)(IWebSocket *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(IWebSocket *This, ULONG *count, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(IWebSocket *This, HSTRING *name);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(IWebSocket *This, TrustLevel *level);
    HRESULT (STDMETHODCALLTYPE *ConnectAsync)(IWebSocket *This, void *uri, IAsyncAction **op);
    HRESULT (STDMETHODCALLTYPE *SetRequestHeader)(IWebSocket *This, HSTRING name, HSTRING value);
    HRESULT (STDMETHODCALLTYPE *Close)(IWebSocket *This, UINT16 status, HSTRING reason);
    HRESULT (STDMETHODCALLTYPE *get_OutputStream)(IWebSocket *This, IOutputStream **value);
} IWebSocketVtbl;

struct IWebSocket
{
    const IWebSocketVtbl *lpVtbl;
};

typedef struct ISockClosable ISockClosable;

typedef struct ISockClosableVtbl
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(ISockClosable *This, REFIID iid, void **out);
    ULONG (STDMETHODCALLTYPE *AddRef)(ISockClosable *This);
    ULONG (STDMETHODCALLTYPE *Release)(ISockClosable *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(ISockClosable *This, ULONG *count, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(ISockClosable *This, HSTRING *name);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(ISockClosable *This, TrustLevel *level);
    HRESULT (STDMETHODCALLTYPE *Close)(ISockClosable *This);
} ISockClosableVtbl;
/* COBJMACROS-style call macros for the locally declared interfaces. */

#define IStreamSocket_QueryInterface(This,...) (This)->lpVtbl->QueryInterface(This, ##__VA_ARGS__)
#define IStreamSocket_AddRef(This,...) (This)->lpVtbl->AddRef(This, ##__VA_ARGS__)
#define IStreamSocket_Release(This,...) (This)->lpVtbl->Release(This, ##__VA_ARGS__)
#define IStreamSocket_GetIids(This,...) (This)->lpVtbl->GetIids(This, ##__VA_ARGS__)
#define IStreamSocket_GetRuntimeClassName(This,...) (This)->lpVtbl->GetRuntimeClassName(This, ##__VA_ARGS__)
#define IStreamSocket_GetTrustLevel(This,...) (This)->lpVtbl->GetTrustLevel(This, ##__VA_ARGS__)
#define IStreamSocket_ConnectAsync_ep_host_svc(This,...) (This)->lpVtbl->ConnectAsync_ep_host_svc(This, ##__VA_ARGS__)
#define IStreamSocket_ConnectAsync_host_svc(This,...) (This)->lpVtbl->ConnectAsync_host_svc(This, ##__VA_ARGS__)
#define IStreamSocket_ConnectAsync_ep_host_svc_prot(This,...) (This)->lpVtbl->ConnectAsync_ep_host_svc_prot(This, ##__VA_ARGS__)
#define IStreamSocket_ConnectAsync_ep_prot(This,...) (This)->lpVtbl->ConnectAsync_ep_prot(This, ##__VA_ARGS__)
#define IStreamSocket_UpgradeToSslAsync(This,...) (This)->lpVtbl->UpgradeToSslAsync(This, ##__VA_ARGS__)
#define IStreamSocket_get_Control(This,...) (This)->lpVtbl->get_Control(This, ##__VA_ARGS__)
#define IStreamSocket_get_Information(This,...) (This)->lpVtbl->get_Information(This, ##__VA_ARGS__)
#define IStreamSocket_get_InputStream(This,...) (This)->lpVtbl->get_InputStream(This, ##__VA_ARGS__)
#define IStreamSocket_get_OutputStream(This,...) (This)->lpVtbl->get_OutputStream(This, ##__VA_ARGS__)
#define IStreamSocketStatics_QueryInterface(This,...) (This)->lpVtbl->QueryInterface(This, ##__VA_ARGS__)
#define IStreamSocketStatics_AddRef(This,...) (This)->lpVtbl->AddRef(This, ##__VA_ARGS__)
#define IStreamSocketStatics_Release(This,...) (This)->lpVtbl->Release(This, ##__VA_ARGS__)
#define IStreamSocketStatics_GetIids(This,...) (This)->lpVtbl->GetIids(This, ##__VA_ARGS__)
#define IStreamSocketStatics_GetRuntimeClassName(This,...) (This)->lpVtbl->GetRuntimeClassName(This, ##__VA_ARGS__)
#define IStreamSocketStatics_GetTrustLevel(This,...) (This)->lpVtbl->GetTrustLevel(This, ##__VA_ARGS__)
#define IStreamSocketStatics_GetEndpointPairsAsync(This,...) (This)->lpVtbl->GetEndpointPairsAsync(This, ##__VA_ARGS__)
#define IStreamSocketControl_QueryInterface(This,...) (This)->lpVtbl->QueryInterface(This, ##__VA_ARGS__)
#define IStreamSocketControl_AddRef(This,...) (This)->lpVtbl->AddRef(This, ##__VA_ARGS__)
#define IStreamSocketControl_Release(This,...) (This)->lpVtbl->Release(This, ##__VA_ARGS__)
#define IStreamSocketControl_GetIids(This,...) (This)->lpVtbl->GetIids(This, ##__VA_ARGS__)
#define IStreamSocketControl_GetRuntimeClassName(This,...) (This)->lpVtbl->GetRuntimeClassName(This, ##__VA_ARGS__)
#define IStreamSocketControl_GetTrustLevel(This,...) (This)->lpVtbl->GetTrustLevel(This, ##__VA_ARGS__)
#define IStreamSocketControl_get_KeepAlive(This,...) (This)->lpVtbl->get_KeepAlive(This, ##__VA_ARGS__)
#define IStreamSocketControl_put_KeepAlive(This,...) (This)->lpVtbl->put_KeepAlive(This, ##__VA_ARGS__)
#define IStreamSocketInformation_QueryInterface(This,...) (This)->lpVtbl->QueryInterface(This, ##__VA_ARGS__)
#define IStreamSocketInformation_AddRef(This,...) (This)->lpVtbl->AddRef(This, ##__VA_ARGS__)
#define IStreamSocketInformation_Release(This,...) (This)->lpVtbl->Release(This, ##__VA_ARGS__)
#define IStreamSocketInformation_GetIids(This,...) (This)->lpVtbl->GetIids(This, ##__VA_ARGS__)
#define IStreamSocketInformation_GetRuntimeClassName(This,...) (This)->lpVtbl->GetRuntimeClassName(This, ##__VA_ARGS__)
#define IStreamSocketInformation_GetTrustLevel(This,...) (This)->lpVtbl->GetTrustLevel(This, ##__VA_ARGS__)
#define IStreamSocketInformation_get_BandwidthStatistics(This,...) (This)->lpVtbl->get_BandwidthStatistics(This, ##__VA_ARGS__)
#define IStreamSocketInformation_get_LocalAddress(This,...) (This)->lpVtbl->get_LocalAddress(This, ##__VA_ARGS__)
#define IStreamSocketInformation_get_LocalPort(This,...) (This)->lpVtbl->get_LocalPort(This, ##__VA_ARGS__)
#define IMessageWebSocket_QueryInterface(This,...) (This)->lpVtbl->QueryInterface(This, ##__VA_ARGS__)
#define IMessageWebSocket_AddRef(This,...) (This)->lpVtbl->AddRef(This, ##__VA_ARGS__)
#define IMessageWebSocket_Release(This,...) (This)->lpVtbl->Release(This, ##__VA_ARGS__)
#define IMessageWebSocket_GetIids(This,...) (This)->lpVtbl->GetIids(This, ##__VA_ARGS__)
#define IMessageWebSocket_GetRuntimeClassName(This,...) (This)->lpVtbl->GetRuntimeClassName(This, ##__VA_ARGS__)
#define IMessageWebSocket_GetTrustLevel(This,...) (This)->lpVtbl->GetTrustLevel(This, ##__VA_ARGS__)
#define IMessageWebSocket_get_Control(This,...) (This)->lpVtbl->get_Control(This, ##__VA_ARGS__)
#define IMessageWebSocket_get_Information(This,...) (This)->lpVtbl->get_Information(This, ##__VA_ARGS__)
#define IMessageWebSocket_add_MessageReceived(This,...) (This)->lpVtbl->add_MessageReceived(This, ##__VA_ARGS__)
#define IMessageWebSocket_remove_MessageReceived(This,...) (This)->lpVtbl->remove_MessageReceived(This, ##__VA_ARGS__)
#define IMessageWebSocketControl_QueryInterface(This,...) (This)->lpVtbl->QueryInterface(This, ##__VA_ARGS__)
#define IMessageWebSocketControl_AddRef(This,...) (This)->lpVtbl->AddRef(This, ##__VA_ARGS__)
#define IMessageWebSocketControl_Release(This,...) (This)->lpVtbl->Release(This, ##__VA_ARGS__)
#define IMessageWebSocketControl_GetIids(This,...) (This)->lpVtbl->GetIids(This, ##__VA_ARGS__)
#define IMessageWebSocketControl_GetRuntimeClassName(This,...) (This)->lpVtbl->GetRuntimeClassName(This, ##__VA_ARGS__)
#define IMessageWebSocketControl_GetTrustLevel(This,...) (This)->lpVtbl->GetTrustLevel(This, ##__VA_ARGS__)
#define IMessageWebSocketControl_get_MaxMessageSize(This,...) (This)->lpVtbl->get_MaxMessageSize(This, ##__VA_ARGS__)
#define IWebSocket_QueryInterface(This,...) (This)->lpVtbl->QueryInterface(This, ##__VA_ARGS__)
#define IWebSocket_AddRef(This,...) (This)->lpVtbl->AddRef(This, ##__VA_ARGS__)
#define IWebSocket_Release(This,...) (This)->lpVtbl->Release(This, ##__VA_ARGS__)
#define IWebSocket_GetIids(This,...) (This)->lpVtbl->GetIids(This, ##__VA_ARGS__)
#define IWebSocket_GetRuntimeClassName(This,...) (This)->lpVtbl->GetRuntimeClassName(This, ##__VA_ARGS__)
#define IWebSocket_GetTrustLevel(This,...) (This)->lpVtbl->GetTrustLevel(This, ##__VA_ARGS__)
#define IWebSocket_ConnectAsync(This,...) (This)->lpVtbl->ConnectAsync(This, ##__VA_ARGS__)
#define IWebSocket_SetRequestHeader(This,...) (This)->lpVtbl->SetRequestHeader(This, ##__VA_ARGS__)
#define IWebSocket_Close(This,...) (This)->lpVtbl->Close(This, ##__VA_ARGS__)
#define IWebSocket_get_OutputStream(This,...) (This)->lpVtbl->get_OutputStream(This, ##__VA_ARGS__)
#define ISockClosable_QueryInterface(This,...) (This)->lpVtbl->QueryInterface(This, ##__VA_ARGS__)
#define ISockClosable_AddRef(This,...) (This)->lpVtbl->AddRef(This, ##__VA_ARGS__)
#define ISockClosable_Release(This,...) (This)->lpVtbl->Release(This, ##__VA_ARGS__)
#define ISockClosable_GetIids(This,...) (This)->lpVtbl->GetIids(This, ##__VA_ARGS__)
#define ISockClosable_GetRuntimeClassName(This,...) (This)->lpVtbl->GetRuntimeClassName(This, ##__VA_ARGS__)
#define ISockClosable_GetTrustLevel(This,...) (This)->lpVtbl->GetTrustLevel(This, ##__VA_ARGS__)
#define ISockClosable_Close(This,...) (This)->lpVtbl->Close(This, ##__VA_ARGS__)


struct ISockClosable
{
    const ISockClosableVtbl *lpVtbl;
};

#define check_interface( obj, iid ) check_interface_( __LINE__, obj, iid )
static void check_interface_( unsigned int line, void *obj, const GUID *iid )
{
    IInspectable *iface = obj;
    IInspectable *inspectable;
    HRESULT hr;

    hr = IInspectable_QueryInterface( iface, iid, (void **)&inspectable );
    ok_(__FILE__, line)( hr == S_OK, "got hr %#lx.\n", hr );
    if (SUCCEEDED(hr)) IInspectable_Release( inspectable );
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

static void check_runtimeclass( IInspectable *inspectable, const WCHAR *class_name )
{
    const WCHAR *buffer;
    HSTRING str;
    HRESULT hr;

    hr = IInspectable_GetRuntimeClassName( inspectable, &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    buffer = WindowsGetStringRawBuffer( str, NULL );
    ok( !wcscmp( buffer, class_name ), "got class name %s.\n", debugstr_w( buffer ) );
    WindowsDeleteString( str );
}

static void check_async_action( unsigned int line, IAsyncAction *action )
{
    AsyncStatus status;
    IAsyncInfo *info;
    HRESULT hr, error;
    UINT32 id;

    check_interface( action, &IID_IAsyncAction );
    check_interface( action, &IID_IAsyncInfo );

    hr = IAsyncAction_QueryInterface( action, &IID_IAsyncInfo, (void **)&info );
    ok_(__FILE__, line)( hr == S_OK, "got hr %#lx.\n", hr );

    status = (AsyncStatus)0xdeadbeef;
    hr = IAsyncInfo_get_Status( info, &status );
    ok_(__FILE__, line)( hr == S_OK, "got hr %#lx.\n", hr );
    ok_(__FILE__, line)( status == Started || status == Completed, "got status %u.\n", status );

    id = 0xdeadbeef;
    hr = IAsyncInfo_get_Id( info, &id );
    ok_(__FILE__, line)( hr == S_OK, "got hr %#lx.\n", hr );
    ok_(__FILE__, line)( id != 0, "got id %u.\n", id );

    error = 0xdeadbeef;
    hr = IAsyncInfo_get_ErrorCode( info, &error );
    ok_(__FILE__, line)( hr == S_OK, "got hr %#lx.\n", hr );
    ok_(__FILE__, line)( error == S_OK, "got error %#lx.\n", error );

    hr = IAsyncAction_GetResults( action );
    ok_(__FILE__, line)( hr == S_OK, "got hr %#lx.\n", hr );

    hr = IAsyncInfo_Cancel( info );
    ok_(__FILE__, line)( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IAsyncInfo_Close( info );
    ok_(__FILE__, line)( hr == S_OK, "got hr %#lx.\n", hr );

    IAsyncInfo_Release( info );
}

static void test_StreamSocket_activation(void)
{
    static const WCHAR *class_name = L"Windows.Networking.Sockets.StreamSocket";
    IStreamSocketStatics *statics = (void *)0xdeadbeef;
    IActivationFactory *factory = (void *)0xdeadbeef;
    IInspectable *inspectable = (void *)0xdeadbeef;
    IStreamSocket *socket = (void *)0xdeadbeef;
    IInspectable *op = (void *)0xdeadbeef;
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
    check_interface( factory, &IID_IActivationFactory );

    check_nointerface( factory, &IID_IStreamSocket );

    check_runtimeclass( (IInspectable *)factory, class_name );

    hr = IActivationFactory_QueryInterface( factory, &IID_IStreamSocketStatics, (void **)&statics );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    hr = IStreamSocketStatics_GetEndpointPairsAsync( statics, NULL, (HSTRING)NULL, &op );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    if (SUCCEEDED(hr) && op)
    {
        check_interface( op, &IID_IAsyncInfo );
        IInspectable_Release( op );
    }

    ref = IStreamSocketStatics_Release( statics );
    ok( ref == 1, "got ref %ld.\n", ref );

    hr = WindowsCreateString( class_name, wcslen( class_name ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = RoActivateInstance( str, &inspectable );
    WindowsDeleteString( str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    check_runtimeclass( inspectable, class_name );
    check_interface( inspectable, &IID_IAgileObject );
    check_interface( inspectable, &IID_IStreamSocket );
    check_interface( inspectable, &IID_IStreamSocket2 );
    check_interface( inspectable, &IID_IStreamSocket3 );
    check_interface( inspectable, &IID_sock_IClosable );

    hr = IInspectable_QueryInterface( inspectable, &IID_IStreamSocket, (void **)&socket );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ref = IInspectable_Release( inspectable );
    ok( ref == 1, "got ref %ld.\n", ref );

    ref = IStreamSocket_Release( socket );
    ok( ref == 0, "got ref %ld.\n", ref );
    ref = IActivationFactory_Release( factory );
    ok( ref == 1, "got ref %ld.\n", ref );
}

static void test_StreamSocket_interface(void)
{
    IStreamSocketInformation *info = (void *)0xdeadbeef;
    IStreamSocketControl *control2 = (void *)0xdeadbeef;
    IStreamSocketControl *control = (void *)0xdeadbeef;
    IInspectable *inspectable = (void *)0xdeadbeef;
    IOutputStream *stream = (void *)0xdeadbeef;
    IInputStream *input = (void *)0xdeadbeef;
    IAsyncAction *action = (void *)0xdeadbeef;
    ISockClosable *closable = (void *)0xdeadbeef;
    IStreamSocket *socket = (void *)0xdeadbeef;
    const WCHAR *buffer;
    HSTRING str;
    HRESULT hr;
    LONG ref;
    boolean value;

    hr = WindowsCreateString( L"Windows.Networking.Sockets.StreamSocket", 39, &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = RoActivateInstance( str, &inspectable );
    WindowsDeleteString( str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IInspectable_QueryInterface( inspectable, &IID_IStreamSocket, (void **)&socket );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    IInspectable_Release( inspectable );

    /* Control */
    hr = IStreamSocket_get_Control( socket, (void **)&control );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    check_interface( control, &IID_IStreamSocketControl );
    value = TRUE;
    hr = IStreamSocketControl_get_KeepAlive( control, &value );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ok( value == FALSE, "got value %u.\n", value );
    hr = IStreamSocketControl_put_KeepAlive( control, TRUE );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    IStreamSocketControl_Release( control );

    /* Information */
    hr = IStreamSocket_get_Information( socket, (void **)&info );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    check_interface( info, &IID_IStreamSocketInformation );
    hr = IStreamSocketInformation_get_LocalPort( info, &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    buffer = WindowsGetStringRawBuffer( str, NULL );
    ok( buffer && !buffer[0], "got port %s.\n", debugstr_w( buffer ) );
    WindowsDeleteString( str );
    IStreamSocketInformation_Release( info );

    /* Streams */
    hr = IStreamSocket_get_InputStream( socket, &input );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    check_interface( input, &IID_IInputStream );
    check_interface( input, &IID_sock_IClosable );
    check_nointerface( input, &IID_IOutputStream );

    hr = IStreamSocket_get_OutputStream( socket, &stream );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    check_interface( stream, &IID_IOutputStream );
    check_nointerface( stream, &IID_IInputStream );

    /* ConnectAsync returns IAsyncAction with valid IAsyncInfo */
    hr = IStreamSocket_ConnectAsync_ep_host_svc( socket, NULL, (HSTRING)NULL, (HSTRING)NULL, &action );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    check_async_action( __LINE__, action );
    check_runtimeclass( (IInspectable *)action, L"Windows.Foundation.IAsyncAction" );
    ref = IAsyncAction_Release( action );
    ok( ref == 0, "got ref %ld.\n", ref );

    hr = IStreamSocket_ConnectAsync_ep_prot( socket, NULL, 0 /* PlainSocket */, &action );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    check_async_action( __LINE__, action );
    ref = IAsyncAction_Release( action );
    ok( ref == 0, "got ref %ld.\n", ref );

    hr = IStreamSocket_UpgradeToSslAsync( socket, 1 /* Ssl */, NULL, &action );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    check_async_action( __LINE__, action );
    ref = IAsyncAction_Release( action );
    ok( ref == 0, "got ref %ld.\n", ref );

    /* The cached control object is identity-stable across calls */
    hr = IStreamSocket_get_Control( socket, (void **)&control );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IStreamSocket_get_Control( socket, (void **)&control2 );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ok( control2 == control, "got control %p, expected %p.\n", control2, control );
    IStreamSocketControl_Release( control2 );
    IStreamSocketControl_Release( control );

    hr = IStreamSocket_QueryInterface( socket, &IID_sock_IClosable, (void **)&closable );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = ISockClosable_Close( closable );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ref = ISockClosable_Release( closable );
    ok( ref == 1, "got ref %ld.\n", ref );

    /* Streams are cached by the socket, releasing our reference leaves the cached one */
    ref = IInputStream_Release( input );
    ok( ref == 1, "got ref %ld.\n", ref );
    ref = IOutputStream_Release( stream );
    ok( ref == 1, "got ref %ld.\n", ref );
    ref = IStreamSocket_Release( socket );
    ok( ref == 0, "got ref %ld.\n", ref );
}

static void test_MessageWebSocket(void)
{
    static const WCHAR *class_name = L"Windows.Networking.Sockets.MessageWebSocket";
    IMessageWebSocketControl *control = (void *)0xdeadbeef;
    IActivationFactory *factory = (void *)0xdeadbeef;
    IInspectable *inspectable = (void *)0xdeadbeef;
    IMessageWebSocket *ws = (void *)0xdeadbeef;
    IWebSocket *iface_ws = (void *)0xdeadbeef;
    IAsyncAction *action = (void *)0xdeadbeef;
    ISockClosable *closable = (void *)0xdeadbeef;
    IInspectable *info = (void *)0xdeadbeef;
    HSTRING str;
    HRESULT hr;
    UINT32 size;
    LONG ref;
    INT64 token;

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
    check_interface( factory, &IID_IActivationFactory );

    check_nointerface( factory, &IID_IMessageWebSocket );

    check_runtimeclass( (IInspectable *)factory, class_name );

    hr = WindowsCreateString( class_name, wcslen( class_name ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = RoActivateInstance( str, &inspectable );
    WindowsDeleteString( str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    check_runtimeclass( inspectable, class_name );
    check_interface( inspectable, &IID_IAgileObject );
    check_interface( inspectable, &IID_IMessageWebSocket );
    check_interface( inspectable, &IID_IMessageWebSocket2 );
    check_interface( inspectable, &IID_IMessageWebSocket3 );
    check_interface( inspectable, &IID_IWebSocket );
    check_interface( inspectable, &IID_sock_IClosable );

    hr = IInspectable_QueryInterface( inspectable, &IID_IMessageWebSocket, (void **)&ws );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ref = IInspectable_Release( inspectable );
    ok( ref == 1, "got ref %ld.\n", ref );

    hr = IMessageWebSocket_get_Control( ws, (void **)&control );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    check_interface( control, &IID_IMessageWebSocketControl );
    check_interface( control, &IID_IWebSocketControl );
    size = 0xdeadbeef;
    hr = IMessageWebSocketControl_get_MaxMessageSize( control, &size );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    IMessageWebSocketControl_Release( control );

    hr = IMessageWebSocket_get_Information( ws, (void **)&info );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    check_interface( info, &IID_IWebSocketInformation );
    IInspectable_Release( info );

    token = 0;
    hr = IMessageWebSocket_add_MessageReceived( ws, NULL, &token );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IMessageWebSocket_remove_MessageReceived( ws, token );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    hr = IMessageWebSocket_QueryInterface( ws, &IID_IWebSocket, (void **)&iface_ws );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    hr = IWebSocket_ConnectAsync( iface_ws, NULL, &action );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    check_async_action( __LINE__, action );
    check_runtimeclass( (IInspectable *)action, L"Windows.Foundation.IAsyncAction" );
    ref = IAsyncAction_Release( action );
    ok( ref == 0, "got ref %ld.\n", ref );

    hr = WindowsCreateString( L"X-Test", 6, &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IWebSocket_SetRequestHeader( iface_ws, str, str );
    WindowsDeleteString( str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    hr = WindowsCreateString( L"bye", 3, &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IWebSocket_Close( iface_ws, 1000, str );
    WindowsDeleteString( str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    hr = IWebSocket_QueryInterface( iface_ws, &IID_sock_IClosable, (void **)&closable );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = ISockClosable_Close( closable );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ref = ISockClosable_Release( closable );
    ok( ref >= 1, "got ref %ld.\n", ref );

    ref = IWebSocket_Release( iface_ws );
    ok( ref == 1, "got ref %ld.\n", ref );
    ref = IMessageWebSocket_Release( ws );
    ok( ref == 0, "got ref %ld.\n", ref );
    ref = IActivationFactory_Release( factory );
    ok( ref == 1, "got ref %ld.\n", ref );
}

START_TEST(sock)
{
    HRESULT hr;

    hr = RoInitialize( RO_INIT_MULTITHREADED );
    ok( hr == S_OK, "RoInitialize failed, hr %#lx.\n", hr );

    test_StreamSocket_activation();
    test_StreamSocket_interface();
    test_MessageWebSocket();

    RoUninitialize();
}
