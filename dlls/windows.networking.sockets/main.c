/* WinRT Windows.Networking.Sockets Implementation
 *
 * Copyright (C) 2026 Wine Developers
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
#define COBJMACROS
#include "windef.h"
#include "winbase.h"
#include "winstring.h"
#include "initguid.h"
#include "activation.h"
#include "roapi.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(winsock);

static const GUID IID_IClosable = {0x3028b027, 0x6029, 0x47e6, {0x81, 0xa1, 0x65, 0x5b, 0x3b, 0x3c, 0x52, 0x0f}};
static const GUID IID_IInputStream = {0x905a0fe2, 0xbc53, 0x11df, {0x8c, 0x49, 0x00, 0x1e, 0x4f, 0xc6, 0x86, 0xda}};
static const GUID IID_IOutputStream = {0x905a0fe6, 0xbc53, 0x11df, {0x8c, 0x49, 0x00, 0x1e, 0x4f, 0xc6, 0x86, 0xda}};
static const GUID IID_IAsyncAction = {0x5a648006, 0x843a, 0x4da9, {0x86, 0x5b, 0x9d, 0x26, 0xe5, 0xdf, 0xad, 0x7b}};
static const GUID IID_IAsyncInfo = {0x00000036, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
static const GUID IID_IReferenceTracker = {0x11d3b13a, 0x180e, 0x4789, {0xa8, 0xbe, 0x77, 0x12, 0x88, 0x28, 0x93, 0xe6}};
static const GUID IID_IReferenceTrackerTarget = {0x5c13e51c, 0x4f32, 0x4726, {0xa3, 0xfd, 0xf3, 0xed, 0xd6, 0x3d, 0xa3, 0xa0}};

static const GUID IID_IStreamSocket = {0x69A22CF3, 0xFC7B, 0x4857, {0xAF, 0x38, 0xF6, 0xE7, 0xDE, 0x6A, 0x5B, 0x49}};
static const GUID IID_IStreamSocket2 = {0x29D0E575, 0xF314, 0x4D09, {0xAD, 0xF0, 0x0F, 0xBD, 0x96, 0x7F, 0xBD, 0x9F}};
static const GUID IID_IStreamSocket3 = {0x3F430B00, 0x9D28, 0x4854, {0xBA, 0xC3, 0x23, 0x01, 0x94, 0x1E, 0xC2, 0x23}};
static const GUID IID_IStreamSocketStatics = {0xA420BC4A, 0x6E2E, 0x4AF5, {0xB5, 0x56, 0x35, 0x5A, 0xE0, 0xCD, 0x4F, 0x29}};
static const GUID IID_IStreamSocketControl = {0xFE25ADF1, 0x92AB, 0x4AF3, {0x99, 0x92, 0x0F, 0x4C, 0x85, 0xE3, 0x6C, 0xC4}};
static const GUID IID_IStreamSocketInformation = {0x3B80AE30, 0x5E68, 0x4205, {0x88, 0xF0, 0xDC, 0x85, 0xD2, 0xE2, 0x5D, 0xED}};

static const GUID IID_IMessageWebSocket = {0x33727D08, 0x34D5, 0x4746, {0xAD, 0x7B, 0x8D, 0xDE, 0x5B, 0xC2, 0xEF, 0x88}};
static const GUID IID_IMessageWebSocket2 = {0xBED0CEE7, 0xF9C8, 0x440A, {0x9A, 0xD5, 0x73, 0x72, 0x81, 0xD9, 0x74, 0x2E}};
static const GUID IID_IMessageWebSocket3 = {0x59D9DEFB, 0x71AF, 0x4349, {0x84, 0x87, 0x91, 0x1F, 0xCF, 0x68, 0x15, 0x97}};
static const GUID IID_IMessageWebSocketControl = {0x8118388A, 0xC629, 0x4F0A, {0x80, 0xFB, 0x81, 0xFC, 0x05, 0x53, 0x88, 0x62}};
static const GUID IID_IWebSocket = {0xF877396F, 0x99B1, 0x4E18, {0xBC, 0x08, 0x85, 0x0C, 0x9A, 0xDF, 0x15, 0x6E}};
static const GUID IID_IWebSocketControl = {0x2EC4BDC3, 0xD9A5, 0x455A, {0x98, 0x11, 0xDE, 0x24, 0xD4, 0x53, 0x37, 0xE9}};
static const GUID IID_IWebSocketInformation = {0x5E01E316, 0xC92A, 0x47A5, {0xB2, 0x5F, 0x07, 0x84, 0x76, 0x39, 0xD1, 0x81}};

static HRESULT create_socket_async_action( void **out );
static HRESULT create_socket_async_op( void **out );
static HRESULT create_socket_stream( BOOL is_input, void **out );
static HRESULT create_stream_socket( void **out );
static HRESULT create_message_websocket( void **out );

/* --- Socket Async Action --- */

struct socket_async
{
    const void *vtbl_async;
    const void *vtbl_info;
    LONG ref;
};

static inline struct socket_async *impl_from_socket_async( void *iface )
{
    return (struct socket_async *)iface;
}
static inline struct socket_async *impl_from_socket_async_info( void *iface )
{
    return (struct socket_async *)((BYTE *)iface - sizeof(void *));
}

static HRESULT WINAPI socket_async_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct socket_async *impl = impl_from_socket_async( iface );

    if (IsEqualGUID( iid, &IID_IReferenceTracker ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IAsyncAction ))
    {
        *out = &impl->vtbl_async;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IAsyncInfo ))
    {
        *out = &impl->vtbl_info;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    /* Accept any generic IAsyncOperation/Progress */
    *out = &impl->vtbl_async;
    InterlockedIncrement( &impl->ref );
    return S_OK;
}

static ULONG WINAPI socket_async_AddRef( void *iface )
{
    struct socket_async *impl = impl_from_socket_async( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI socket_async_Release( void *iface )
{
    struct socket_async *impl = impl_from_socket_async( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref) free( impl );
    return ref;
}

static HRESULT WINAPI socket_async_GetIids( void *iface, ULONG *c, IID **i )
{
    if (!c || !i) return E_POINTER;
    *c = 0;
    *i = NULL;
    return S_OK;
}
static HRESULT WINAPI socket_async_action_GetRuntimeClassName( void *iface, HSTRING *n )
{
    static const WCHAR class_name[] = L"Windows.Foundation.IAsyncAction";
    if (!n) return E_POINTER;
    return WindowsCreateString( class_name, ARRAY_SIZE(class_name) - 1, n );
}
static HRESULT WINAPI socket_async_op_GetRuntimeClassName( void *iface, HSTRING *n )
{
    static const WCHAR class_name[] = L"Windows.Foundation.IAsyncOperationWithProgress`2<UInt32, UInt32>";
    if (!n) return E_POINTER;
    return WindowsCreateString( class_name, ARRAY_SIZE(class_name) - 1, n );
}
static HRESULT WINAPI socket_async_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }

static HRESULT WINAPI socket_async_put_Progress( void *iface, void *handler ) { return S_OK; }
static HRESULT WINAPI socket_async_get_Progress( void *iface, void **handler ) { if (handler) *handler = NULL; return S_OK; }

static HRESULT WINAPI socket_async_put_Completed( void *iface, void *handler )
{
    if (handler)
    {
        typedef HRESULT (WINAPI *invoke_fn)( void *handler, void *async, int status );
        void **vtbl = *(void ***)handler;
        invoke_fn invoke = (invoke_fn)vtbl[3];
        invoke( handler, iface, 1 /* Completed */ );
    }
    return S_OK;
}

static HRESULT WINAPI socket_async_get_Completed( void *iface, void **handler )
{
    if (handler) *handler = NULL;
    return S_OK;
}

static HRESULT WINAPI socket_async_action_GetResults( void *iface )
{
    return S_OK;
}

static HRESULT WINAPI socket_async_op_GetResults( void *iface, void *results )
{
    if (results) *(UINT32 *)results = 0;
    return S_OK;
}

static const void *socket_async_action_vtbl[] =
{
    socket_async_QueryInterface,
    socket_async_AddRef,
    socket_async_Release,
    socket_async_GetIids,
    socket_async_action_GetRuntimeClassName,
    socket_async_GetTrustLevel,
    socket_async_put_Completed,
    socket_async_get_Completed,
    socket_async_action_GetResults,
};

static const void *socket_async_op_vtbl[] =
{
    socket_async_QueryInterface,
    socket_async_AddRef,
    socket_async_Release,
    socket_async_GetIids,
    socket_async_op_GetRuntimeClassName,
    socket_async_GetTrustLevel,
    socket_async_put_Progress,
    socket_async_get_Progress,
    socket_async_put_Completed,
    socket_async_get_Completed,
    socket_async_op_GetResults,
};

static HRESULT WINAPI socket_info_QueryInterface( void *iface, REFIID iid, void **out )
{
    return socket_async_QueryInterface( &impl_from_socket_async_info( iface )->vtbl_async, iid, out );
}
static ULONG WINAPI socket_info_AddRef( void *iface )
{
    return socket_async_AddRef( &impl_from_socket_async_info( iface )->vtbl_async );
}
static ULONG WINAPI socket_info_Release( void *iface )
{
    return socket_async_Release( &impl_from_socket_async_info( iface )->vtbl_async );
}
static HRESULT WINAPI socket_info_GetIids( void *iface, ULONG *c, IID **i )
{
    if (!c || !i) return E_POINTER;
    *c = 0;
    *i = NULL;
    return S_OK;
}
static HRESULT WINAPI socket_info_GetRuntimeClassName( void *iface, HSTRING *n )
{
    static const WCHAR class_name[] = L"Windows.Foundation.IAsyncInfo";
    if (!n) return E_POINTER;
    return WindowsCreateString( class_name, ARRAY_SIZE(class_name) - 1, n );
}
static HRESULT WINAPI socket_info_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI socket_info_get_Id( void *iface, UINT32 *id )
{
    if (!id) return E_POINTER;
    *id = 1;
    return S_OK;
}
static HRESULT WINAPI socket_info_get_Status( void *iface, int *status )
{
    if (!status) return E_POINTER;
    *status = 1 /* Completed */;
    return S_OK;
}
static HRESULT WINAPI socket_info_get_ErrorCode( void *iface, HRESULT *code )
{
    if (!code) return E_POINTER;
    *code = S_OK;
    return S_OK;
}
static HRESULT WINAPI socket_info_Cancel( void *iface ) { return S_OK; }
static HRESULT WINAPI socket_info_Close( void *iface ) { return S_OK; }

static const void *socket_info_vtbl[] =
{
    socket_info_QueryInterface,
    socket_info_AddRef,
    socket_info_Release,
    socket_info_GetIids,
    socket_info_GetRuntimeClassName,
    socket_info_GetTrustLevel,
    socket_info_get_Id,
    socket_info_get_Status,
    socket_info_get_ErrorCode,
    socket_info_Cancel,
    socket_info_Close,
};

static HRESULT create_socket_async_action( void **out )
{
    struct socket_async *impl;
    if (!out) return E_POINTER;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->vtbl_async = socket_async_action_vtbl;
    impl->vtbl_info = socket_info_vtbl;
    impl->ref = 1;
    *out = &impl->vtbl_async;
    return S_OK;
}

static HRESULT create_socket_async_op( void **out )
{
    struct socket_async *impl;
    if (!out) return E_POINTER;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->vtbl_async = socket_async_op_vtbl;
    impl->vtbl_info = socket_info_vtbl;
    impl->ref = 1;
    *out = &impl->vtbl_async;
    return S_OK;
}

/* --- Socket Streams (InputStream / OutputStream) --- */

struct socket_stream
{
    const void *vtbl_stream;
    const void *vtbl_closable;
    LONG ref;
    BOOL is_input;
};

static inline struct socket_stream *impl_from_stream( void *iface )
{
    return (struct socket_stream *)iface;
}
static inline struct socket_stream *impl_from_stream_closable( void *iface )
{
    return (struct socket_stream *)((BYTE *)iface - sizeof(void *));
}

static HRESULT WINAPI stream_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct socket_stream *impl = impl_from_stream( iface );
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ))
    {
        *out = &impl->vtbl_stream;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    if (impl->is_input && IsEqualGUID( iid, &IID_IInputStream ))
    {
        *out = &impl->vtbl_stream;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    if (!impl->is_input && IsEqualGUID( iid, &IID_IOutputStream ))
    {
        *out = &impl->vtbl_stream;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IClosable ))
    {
        *out = &impl->vtbl_closable;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI stream_AddRef( void *iface )
{
    struct socket_stream *impl = impl_from_stream( iface );
    return InterlockedIncrement( &impl->ref );
}
static ULONG WINAPI stream_Release( void *iface )
{
    struct socket_stream *impl = impl_from_stream( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref) free( impl );
    return ref;
}
static HRESULT WINAPI stream_GetIids( void *iface, ULONG *c, IID **i ) { return E_NOTIMPL; }
static HRESULT WINAPI stream_GetRuntimeClassName( void *iface, HSTRING *n ) { return E_NOTIMPL; }
static HRESULT WINAPI stream_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }

static HRESULT WINAPI input_stream_ReadAsync( void *iface, void *buf, UINT32 count, int opt, void **op )
{
    return create_socket_async_op( (void **)op );
}

static const void *socket_input_stream_vtbl[] =
{
    stream_QueryInterface,
    stream_AddRef,
    stream_Release,
    stream_GetIids,
    stream_GetRuntimeClassName,
    stream_GetTrustLevel,
    input_stream_ReadAsync,
};

static HRESULT WINAPI output_stream_WriteAsync( void *iface, void *buf, void **op )
{
    return create_socket_async_op( (void **)op );
}
static HRESULT WINAPI output_stream_FlushAsync( void *iface, void **op )
{
    return create_socket_async_action( op );
}

static const void *socket_output_stream_vtbl[] =
{
    stream_QueryInterface,
    stream_AddRef,
    stream_Release,
    stream_GetIids,
    stream_GetRuntimeClassName,
    stream_GetTrustLevel,
    output_stream_WriteAsync,
    output_stream_FlushAsync,
};

static HRESULT WINAPI stream_closable_QueryInterface( void *iface, REFIID iid, void **out )
{
    return stream_QueryInterface( &impl_from_stream_closable( iface )->vtbl_stream, iid, out );
}
static ULONG WINAPI stream_closable_AddRef( void *iface )
{
    return stream_AddRef( &impl_from_stream_closable( iface )->vtbl_stream );
}
static ULONG WINAPI stream_closable_Release( void *iface )
{
    return stream_Release( &impl_from_stream_closable( iface )->vtbl_stream );
}
static HRESULT WINAPI stream_closable_GetIids( void *iface, ULONG *c, IID **i ) { return E_NOTIMPL; }
static HRESULT WINAPI stream_closable_GetRuntimeClassName( void *iface, HSTRING *n ) { return E_NOTIMPL; }
static HRESULT WINAPI stream_closable_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI stream_closable_Close( void *iface ) { return S_OK; }

static const void *socket_stream_closable_vtbl[] =
{
    stream_closable_QueryInterface,
    stream_closable_AddRef,
    stream_closable_Release,
    stream_closable_GetIids,
    stream_closable_GetRuntimeClassName,
    stream_closable_GetTrustLevel,
    stream_closable_Close,
};

static HRESULT create_socket_stream( BOOL is_input, void **out )
{
    struct socket_stream *impl;
    if (!out) return E_POINTER;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->vtbl_stream = is_input ? socket_input_stream_vtbl : socket_output_stream_vtbl;
    impl->vtbl_closable = socket_stream_closable_vtbl;
    impl->ref = 1;
    impl->is_input = is_input;
    *out = &impl->vtbl_stream;
    return S_OK;
}

/* --- StreamSocketControl / StreamSocketInformation --- */

struct dummy_control_info
{
    const void *vtbl;
    LONG ref;
    GUID iid;
};

static HRESULT WINAPI dummy_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct dummy_control_info *impl = iface;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &impl->iid ) ||
        (IsEqualGUID( &impl->iid, &IID_IMessageWebSocketControl ) && IsEqualGUID( iid, &IID_IWebSocketControl )))
    {
        *out = iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI dummy_AddRef( void *iface )
{
    struct dummy_control_info *impl = iface;
    return InterlockedIncrement( &impl->ref );
}
static ULONG WINAPI dummy_Release( void *iface )
{
    struct dummy_control_info *impl = iface;
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref) free( impl );
    return ref;
}
static HRESULT WINAPI dummy_GetIids( void *iface, ULONG *c, IID **i ) { return E_NOTIMPL; }
static HRESULT WINAPI dummy_GetRuntimeClassName( void *iface, HSTRING *n ) { return E_NOTIMPL; }
static HRESULT WINAPI dummy_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }

static HRESULT WINAPI dummy_stub_ok( void *iface ) { return S_OK; }
static HRESULT WINAPI dummy_stub_bool( void *iface, boolean *val ) { if (val) *val = FALSE; return S_OK; }
static HRESULT WINAPI dummy_stub_uint32( void *iface, UINT32 *val ) { if (val) *val = 0; return S_OK; }
static HRESULT WINAPI dummy_stub_byte( void *iface, BYTE *val ) { if (val) *val = 0; return S_OK; }
static HRESULT WINAPI dummy_stub_set_bool( void *iface, boolean value ) { return S_OK; }
static HRESULT WINAPI dummy_stub_set_uint32( void *iface, UINT32 value ) { return S_OK; }
static HRESULT WINAPI dummy_stub_set_byte( void *iface, BYTE value ) { return S_OK; }
static HRESULT WINAPI dummy_stub_set_ptr( void *iface, void *value ) { return S_OK; }
static HRESULT WINAPI dummy_stub_null( void *iface, void **val ) { if (val) *val = NULL; return S_OK; }
static HRESULT WINAPI dummy_stub_hstring( void *iface, HSTRING *val )
{
    if (!val) return E_POINTER;
    return WindowsCreateString( L"", 0, val );
}

static const void *stream_socket_control_vtbl[] =
{
    dummy_QueryInterface,
    dummy_AddRef,
    dummy_Release,
    dummy_GetIids,
    dummy_GetRuntimeClassName,
    dummy_GetTrustLevel,
    dummy_stub_bool,        /* get_KeepAlive */
    dummy_stub_set_bool,    /* put_KeepAlive */
    dummy_stub_bool,        /* get_NoDelay */
    dummy_stub_set_bool,    /* put_NoDelay */
    dummy_stub_uint32,      /* get_OutboundBufferSizeInBytes */
    dummy_stub_set_uint32,  /* put_OutboundBufferSizeInBytes */
    dummy_stub_byte,        /* get_OutboundUnicastHopLimit */
    dummy_stub_set_byte,    /* put_OutboundUnicastHopLimit */
    dummy_stub_uint32,      /* get_QualityOfService */
    dummy_stub_set_uint32,  /* put_QualityOfService */
};

static const void *stream_socket_info_vtbl[] =
{
    dummy_QueryInterface,
    dummy_AddRef,
    dummy_Release,
    dummy_GetIids,
    dummy_GetRuntimeClassName,
    dummy_GetTrustLevel,
    dummy_stub_null,    /* get_BandwidthStatistics */
    dummy_stub_null,    /* get_LocalAddress */
    dummy_stub_hstring, /* get_LocalPort */
    dummy_stub_uint32,  /* get_ProtectionLevel */
    dummy_stub_null,    /* get_RemoteAddress */
    dummy_stub_null,    /* get_RemoteHostName */
    dummy_stub_hstring, /* get_RemotePort */
    dummy_stub_hstring, /* get_RemoteServiceName */
    dummy_stub_null,    /* get_RoundTripTimeStatistics */
    dummy_stub_null,    /* get_SessionKey */
};

static HRESULT create_dummy_obj( const void *vtbl, const GUID *iid, void **out )
{
    struct dummy_control_info *impl;
    if (!out) return E_POINTER;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->vtbl = vtbl;
    impl->ref = 1;
    impl->iid = *iid;
    *out = impl;
    return S_OK;
}

/* --- StreamSocket --- */

struct stream_socket
{
    const void *vtbl_socket;
    const void *vtbl_socket2;
    const void *vtbl_socket3;
    const void *vtbl_closable;
    LONG ref;
    void *control;
    void *info;
    void *input_stream;
    void *output_stream;
};

static inline struct stream_socket *impl_from_socket( void *iface )
{
    return (struct stream_socket *)iface;
}
static inline struct stream_socket *impl_from_socket2( void *iface )
{
    return (struct stream_socket *)((BYTE *)iface - sizeof(void *));
}
static inline struct stream_socket *impl_from_socket3( void *iface )
{
    return (struct stream_socket *)((BYTE *)iface - 2 * sizeof(void *));
}
static inline struct stream_socket *impl_from_socket_closable( void *iface )
{
    return (struct stream_socket *)((BYTE *)iface - 3 * sizeof(void *));
}

static HRESULT WINAPI socket_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct stream_socket *impl = impl_from_socket( iface );
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IStreamSocket ))
    {
        *out = &impl->vtbl_socket;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IStreamSocket2 ))
    {
        *out = &impl->vtbl_socket2;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IStreamSocket3 ))
    {
        *out = &impl->vtbl_socket3;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IClosable ))
    {
        *out = &impl->vtbl_closable;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI socket_AddRef( void *iface )
{
    struct stream_socket *impl = impl_from_socket( iface );
    return InterlockedIncrement( &impl->ref );
}
static ULONG WINAPI socket_Release( void *iface )
{
    struct stream_socket *impl = impl_from_socket( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        if (impl->control) IUnknown_Release( (IUnknown *)impl->control );
        if (impl->info) IUnknown_Release( (IUnknown *)impl->info );
        if (impl->input_stream) IUnknown_Release( (IUnknown *)impl->input_stream );
        if (impl->output_stream) IUnknown_Release( (IUnknown *)impl->output_stream );
        free( impl );
    }
    return ref;
}
static HRESULT WINAPI socket_GetIids( void *iface, ULONG *c, IID **i ) { return E_NOTIMPL; }
static HRESULT WINAPI socket_GetRuntimeClassName( void *iface, HSTRING *n )
{
    return WindowsCreateString( L"Windows.Networking.Sockets.StreamSocket", 39, n );
}
static HRESULT WINAPI socket_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }

static HRESULT WINAPI socket_ConnectAsync( void *iface, void *endpoint, HSTRING host, HSTRING service, void **op )
{
    TRACE( "iface %p, endpoint %p, service %s\n", iface, endpoint, debugstr_hstring( service ) );
    return create_socket_async_action( op );
}
static HRESULT WINAPI socket_ConnectAsync2( void *iface, void *host, HSTRING service, void **op )
{
    TRACE( "iface %p, service %s\n", iface, debugstr_hstring( service ) );
    return create_socket_async_action( op );
}
static HRESULT WINAPI socket_ConnectAsync3( void *iface, void *endpoint, void *host, HSTRING service, int prot, void **op )
{
    TRACE( "iface %p, endpoint %p, service %s, prot %d\n", iface, endpoint, debugstr_hstring( service ), prot );
    return create_socket_async_action( op );
}
static HRESULT WINAPI socket_ConnectAsync4( void *iface, void *endpoint, int prot, void **op )
{
    TRACE( "iface %p, endpoint %p, prot %d\n", iface, endpoint, prot );
    return create_socket_async_action( op );
}
static HRESULT WINAPI socket_UpgradeToSslAsync( void *iface, int prot, void *host, void **op )
{
    TRACE( "iface %p, prot %d\n", iface, prot );
    return create_socket_async_action( op );
}
static HRESULT WINAPI socket_get_Control( void *iface, void **control )
{
    struct stream_socket *impl = impl_from_socket( iface );
    if (!control) return E_POINTER;
    if (!impl->control) create_dummy_obj( stream_socket_control_vtbl, &IID_IStreamSocketControl, &impl->control );
    IUnknown_AddRef( (IUnknown *)impl->control );
    *control = impl->control;
    return S_OK;
}
static HRESULT WINAPI socket_get_Information( void *iface, void **info )
{
    struct stream_socket *impl = impl_from_socket( iface );
    if (!info) return E_POINTER;
    if (!impl->info) create_dummy_obj( stream_socket_info_vtbl, &IID_IStreamSocketInformation, &impl->info );
    IUnknown_AddRef( (IUnknown *)impl->info );
    *info = impl->info;
    return S_OK;
}
static HRESULT WINAPI socket_get_InputStream( void *iface, void **stream )
{
    struct stream_socket *impl = impl_from_socket( iface );
    if (!stream) return E_POINTER;
    if (!impl->input_stream) create_socket_stream( TRUE, &impl->input_stream );
    IUnknown_AddRef( (IUnknown *)impl->input_stream );
    *stream = impl->input_stream;
    return S_OK;
}
static HRESULT WINAPI socket_get_OutputStream( void *iface, void **stream )
{
    struct stream_socket *impl = impl_from_socket( iface );
    if (!stream) return E_POINTER;
    if (!impl->output_stream) create_socket_stream( FALSE, &impl->output_stream );
    IUnknown_AddRef( (IUnknown *)impl->output_stream );
    *stream = impl->output_stream;
    return S_OK;
}

static const void *stream_socket_vtbl[] =
{
    socket_QueryInterface,
    socket_AddRef,
    socket_Release,
    socket_GetIids,
    socket_GetRuntimeClassName,
    socket_GetTrustLevel,
    socket_ConnectAsync,
    socket_ConnectAsync2,
    socket_ConnectAsync3,
    socket_ConnectAsync4,
    socket_UpgradeToSslAsync,
    socket_get_Control,
    socket_get_Information,
    socket_get_InputStream,
    socket_get_OutputStream,
};

static HRESULT WINAPI socket2_QueryInterface( void *iface, REFIID iid, void **out )
{
    return socket_QueryInterface( &impl_from_socket2( iface )->vtbl_socket, iid, out );
}
static ULONG WINAPI socket2_AddRef( void *iface )
{
    return socket_AddRef( &impl_from_socket2( iface )->vtbl_socket );
}
static ULONG WINAPI socket2_Release( void *iface )
{
    return socket_Release( &impl_from_socket2( iface )->vtbl_socket );
}
static HRESULT WINAPI socket2_GetIids( void *iface, ULONG *c, IID **i ) { return E_NOTIMPL; }
static HRESULT WINAPI socket2_GetRuntimeClassName( void *iface, HSTRING *n ) { return E_NOTIMPL; }
static HRESULT WINAPI socket2_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI socket2_ConnectAsync( void *iface, void *host, HSTRING service, int prot, void *adapter, void **op )
{
    return create_socket_async_action( op );
}

static const void *stream_socket2_vtbl[] =
{
    socket2_QueryInterface,
    socket2_AddRef,
    socket2_Release,
    socket2_GetIids,
    socket2_GetRuntimeClassName,
    socket2_GetTrustLevel,
    socket2_ConnectAsync,
};

static HRESULT WINAPI socket3_QueryInterface( void *iface, REFIID iid, void **out )
{
    return socket_QueryInterface( &impl_from_socket3( iface )->vtbl_socket, iid, out );
}
static ULONG WINAPI socket3_AddRef( void *iface )
{
    return socket_AddRef( &impl_from_socket3( iface )->vtbl_socket );
}
static ULONG WINAPI socket3_Release( void *iface )
{
    return socket_Release( &impl_from_socket3( iface )->vtbl_socket );
}
static HRESULT WINAPI socket3_GetIids( void *iface, ULONG *c, IID **i ) { return E_NOTIMPL; }
static HRESULT WINAPI socket3_GetRuntimeClassName( void *iface, HSTRING *n ) { return E_NOTIMPL; }
static HRESULT WINAPI socket3_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI socket3_CancelIOAsync( void *iface, void **op ) { return create_socket_async_action( op ); }
static HRESULT WINAPI socket3_stub_ok( void *iface ) { return S_OK; }

static const void *stream_socket3_vtbl[] =
{
    socket3_QueryInterface,
    socket3_AddRef,
    socket3_Release,
    socket3_GetIids,
    socket3_GetRuntimeClassName,
    socket3_GetTrustLevel,
    socket3_CancelIOAsync,
    socket3_stub_ok,
    socket3_stub_ok,
    socket3_stub_ok,
    socket3_stub_ok,
    socket3_stub_ok,
};

static HRESULT WINAPI socket_closable_QueryInterface( void *iface, REFIID iid, void **out )
{
    return socket_QueryInterface( &impl_from_socket_closable( iface )->vtbl_socket, iid, out );
}
static ULONG WINAPI socket_closable_AddRef( void *iface )
{
    return socket_AddRef( &impl_from_socket_closable( iface )->vtbl_socket );
}
static ULONG WINAPI socket_closable_Release( void *iface )
{
    return socket_Release( &impl_from_socket_closable( iface )->vtbl_socket );
}
static HRESULT WINAPI socket_closable_GetIids( void *iface, ULONG *c, IID **i ) { return E_NOTIMPL; }
static HRESULT WINAPI socket_closable_GetRuntimeClassName( void *iface, HSTRING *n ) { return E_NOTIMPL; }
static HRESULT WINAPI socket_closable_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI socket_closable_Close( void *iface ) { return S_OK; }

static const void *stream_socket_closable_vtbl[] =
{
    socket_closable_QueryInterface,
    socket_closable_AddRef,
    socket_closable_Release,
    socket_closable_GetIids,
    socket_closable_GetRuntimeClassName,
    socket_closable_GetTrustLevel,
    socket_closable_Close,
};

static HRESULT create_stream_socket( void **out )
{
    struct stream_socket *impl;
    if (!out) return E_POINTER;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->vtbl_socket = stream_socket_vtbl;
    impl->vtbl_socket2 = stream_socket2_vtbl;
    impl->vtbl_socket3 = stream_socket3_vtbl;
    impl->vtbl_closable = stream_socket_closable_vtbl;
    impl->ref = 1;
    *out = &impl->vtbl_socket;
    return S_OK;
}

/* --- MessageWebSocket --- */

static const void *message_websocket_control_vtbl[] =
{
    dummy_QueryInterface,
    dummy_AddRef,
    dummy_Release,
    dummy_GetIids,
    dummy_GetRuntimeClassName,
    dummy_GetTrustLevel,
    dummy_stub_uint32,     /* get_MaxMessageSize */
    dummy_stub_set_uint32, /* put_MaxMessageSize */
    dummy_stub_uint32,     /* get_MessageType */
    dummy_stub_set_uint32, /* put_MessageType */
    dummy_stub_uint32,     /* get_OutboundBufferSizeInBytes */
    dummy_stub_set_uint32, /* put_OutboundBufferSizeInBytes */
    dummy_stub_null,       /* get_ProxyCredential */
    dummy_stub_set_ptr,    /* put_ProxyCredential */
    dummy_stub_null,       /* get_ServerCredential */
    dummy_stub_set_ptr,    /* put_ServerCredential */
    dummy_stub_null,       /* get_SupportedProtocols */
};

static const void *message_websocket_info_vtbl[] =
{
    dummy_QueryInterface,
    dummy_AddRef,
    dummy_Release,
    dummy_GetIids,
    dummy_GetRuntimeClassName,
    dummy_GetTrustLevel,
    dummy_stub_null,    /* get_BandwidthStatistics */
    dummy_stub_null,    /* get_LocalAddress */
    dummy_stub_hstring, /* get_Protocol */
};

struct message_websocket
{
    const void *vtbl_msgws;
    const void *vtbl_msgws2;
    const void *vtbl_msgws3;
    const void *vtbl_ws;
    const void *vtbl_closable;
    LONG ref;
    void *control;
    void *info;
    void *output_stream;
};

static inline struct message_websocket *impl_from_msgws( void *iface )
{
    return (struct message_websocket *)iface;
}
static inline struct message_websocket *impl_from_msgws2( void *iface )
{
    return (struct message_websocket *)((BYTE *)iface - sizeof(void *));
}
static inline struct message_websocket *impl_from_msgws3( void *iface )
{
    return (struct message_websocket *)((BYTE *)iface - 2 * sizeof(void *));
}
static inline struct message_websocket *impl_from_ws( void *iface )
{
    return (struct message_websocket *)((BYTE *)iface - 3 * sizeof(void *));
}
static inline struct message_websocket *impl_from_msgws_closable( void *iface )
{
    return (struct message_websocket *)((BYTE *)iface - 4 * sizeof(void *));
}

static HRESULT WINAPI msgws_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct message_websocket *impl = impl_from_msgws( iface );
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IMessageWebSocket ))
    {
        *out = &impl->vtbl_msgws;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IMessageWebSocket2 ))
    {
        *out = &impl->vtbl_msgws2;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IMessageWebSocket3 ))
    {
        *out = &impl->vtbl_msgws3;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IWebSocket ))
    {
        *out = &impl->vtbl_ws;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IClosable ))
    {
        *out = &impl->vtbl_closable;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI msgws_AddRef( void *iface )
{
    struct message_websocket *impl = impl_from_msgws( iface );
    return InterlockedIncrement( &impl->ref );
}
static ULONG WINAPI msgws_Release( void *iface )
{
    struct message_websocket *impl = impl_from_msgws( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        if (impl->control) IUnknown_Release( (IUnknown *)impl->control );
        if (impl->info) IUnknown_Release( (IUnknown *)impl->info );
        if (impl->output_stream) IUnknown_Release( (IUnknown *)impl->output_stream );
        free( impl );
    }
    return ref;
}
static HRESULT WINAPI msgws_GetIids( void *iface, ULONG *c, IID **i ) { return E_NOTIMPL; }
static HRESULT WINAPI msgws_GetRuntimeClassName( void *iface, HSTRING *n )
{
    static const WCHAR class_name[] = L"Windows.Networking.Sockets.MessageWebSocket";
    return WindowsCreateString( class_name, ARRAY_SIZE(class_name) - 1, n );
}
static HRESULT WINAPI msgws_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }

static HRESULT WINAPI msgws_get_Control( void *iface, void **control )
{
    struct message_websocket *impl = impl_from_msgws( iface );
    if (!control) return E_POINTER;
    if (!impl->control) create_dummy_obj( message_websocket_control_vtbl, &IID_IMessageWebSocketControl, &impl->control );
    IUnknown_AddRef( (IUnknown *)impl->control );
    *control = impl->control;
    return S_OK;
}
static HRESULT WINAPI msgws_get_Information( void *iface, void **info )
{
    struct message_websocket *impl = impl_from_msgws( iface );
    if (!info) return E_POINTER;
    if (!impl->info) create_dummy_obj( message_websocket_info_vtbl, &IID_IWebSocketInformation, &impl->info );
    IUnknown_AddRef( (IUnknown *)impl->info );
    *info = impl->info;
    return S_OK;
}
static HRESULT WINAPI msgws_add_MessageReceived( void *iface, void *handler, INT64 *token )
{
    if (token) *token = 1;
    return S_OK;
}
static HRESULT WINAPI msgws_remove_MessageReceived( void *iface, INT64 token ) { return S_OK; }

static const void *message_websocket_vtbl[] =
{
    msgws_QueryInterface,
    msgws_AddRef,
    msgws_Release,
    msgws_GetIids,
    msgws_GetRuntimeClassName,
    msgws_GetTrustLevel,
    msgws_get_Control,
    msgws_get_Information,
    msgws_add_MessageReceived,
    msgws_remove_MessageReceived,
};

static HRESULT WINAPI msgws2_QueryInterface( void *iface, REFIID iid, void **out )
{
    return msgws_QueryInterface( &impl_from_msgws2( iface )->vtbl_msgws, iid, out );
}
static ULONG WINAPI msgws2_AddRef( void *iface ) { return msgws_AddRef( &impl_from_msgws2( iface )->vtbl_msgws ); }
static ULONG WINAPI msgws2_Release( void *iface ) { return msgws_Release( &impl_from_msgws2( iface )->vtbl_msgws ); }
static HRESULT WINAPI msgws2_GetIids( void *iface, ULONG *c, IID **i ) { return E_NOTIMPL; }
static HRESULT WINAPI msgws2_GetRuntimeClassName( void *iface, HSTRING *n ) { return E_NOTIMPL; }
static HRESULT WINAPI msgws2_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI msgws2_add_ServerCustomValidationRequested( void *iface, void *handler, INT64 *token )
{
    if (token) *token = 1;
    return S_OK;
}
static HRESULT WINAPI msgws2_remove_ServerCustomValidationRequested( void *iface, INT64 token ) { return S_OK; }

static const void *message_websocket2_vtbl[] =
{
    msgws2_QueryInterface,
    msgws2_AddRef,
    msgws2_Release,
    msgws2_GetIids,
    msgws2_GetRuntimeClassName,
    msgws2_GetTrustLevel,
    msgws2_add_ServerCustomValidationRequested,
    msgws2_remove_ServerCustomValidationRequested,
};

static HRESULT WINAPI msgws3_QueryInterface( void *iface, REFIID iid, void **out )
{
    return msgws_QueryInterface( &impl_from_msgws3( iface )->vtbl_msgws, iid, out );
}
static ULONG WINAPI msgws3_AddRef( void *iface ) { return msgws_AddRef( &impl_from_msgws3( iface )->vtbl_msgws ); }
static ULONG WINAPI msgws3_Release( void *iface ) { return msgws_Release( &impl_from_msgws3( iface )->vtbl_msgws ); }
static HRESULT WINAPI msgws3_GetIids( void *iface, ULONG *c, IID **i ) { return E_NOTIMPL; }
static HRESULT WINAPI msgws3_GetRuntimeClassName( void *iface, HSTRING *n ) { return E_NOTIMPL; }
static HRESULT WINAPI msgws3_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI msgws3_SendNonfinalFrameAsync( void *iface, void *data, void **op ) { return create_socket_async_op( op ); }
static HRESULT WINAPI msgws3_SendFinalFrameAsync( void *iface, void *data, void **op ) { return create_socket_async_op( op ); }

static const void *message_websocket3_vtbl[] =
{
    msgws3_QueryInterface,
    msgws3_AddRef,
    msgws3_Release,
    msgws3_GetIids,
    msgws3_GetRuntimeClassName,
    msgws3_GetTrustLevel,
    msgws3_SendNonfinalFrameAsync,
    msgws3_SendFinalFrameAsync,
};

static HRESULT WINAPI ws_QueryInterface( void *iface, REFIID iid, void **out )
{
    return msgws_QueryInterface( &impl_from_ws( iface )->vtbl_msgws, iid, out );
}
static ULONG WINAPI ws_AddRef( void *iface ) { return msgws_AddRef( &impl_from_ws( iface )->vtbl_msgws ); }
static ULONG WINAPI ws_Release( void *iface ) { return msgws_Release( &impl_from_ws( iface )->vtbl_msgws ); }
static HRESULT WINAPI ws_GetIids( void *iface, ULONG *c, IID **i ) { return E_NOTIMPL; }
static HRESULT WINAPI ws_GetRuntimeClassName( void *iface, HSTRING *n ) { return E_NOTIMPL; }
static HRESULT WINAPI ws_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }

static HRESULT WINAPI ws_ConnectAsync( void *iface, void *uri, void **op )
{
    TRACE( "iface %p, uri %p\n", iface, uri );
    return create_socket_async_action( op );
}
static HRESULT WINAPI ws_SetRequestHeader( void *iface, HSTRING name, HSTRING val ) { return S_OK; }
static HRESULT WINAPI ws_Close( void *iface, UINT16 code, HSTRING reason ) { return S_OK; }
static HRESULT WINAPI ws_get_OutputStream( void *iface, void **stream )
{
    struct message_websocket *impl = impl_from_ws( iface );
    if (!stream) return E_POINTER;
    if (!impl->output_stream) create_socket_stream( FALSE, &impl->output_stream );
    IUnknown_AddRef( (IUnknown *)impl->output_stream );
    *stream = impl->output_stream;
    return S_OK;
}
static HRESULT WINAPI ws_add_Closed( void *iface, void *handler, INT64 *token )
{
    if (token) *token = 1;
    return S_OK;
}
static HRESULT WINAPI ws_remove_Closed( void *iface, INT64 token ) { return S_OK; }

static const void *websocket_vtbl[] =
{
    ws_QueryInterface,
    ws_AddRef,
    ws_Release,
    ws_GetIids,
    ws_GetRuntimeClassName,
    ws_GetTrustLevel,
    ws_ConnectAsync,
    ws_SetRequestHeader,
    ws_Close,
    ws_get_OutputStream,
    ws_add_Closed,
    ws_remove_Closed,
};

static HRESULT WINAPI msgws_closable_QueryInterface( void *iface, REFIID iid, void **out )
{
    return msgws_QueryInterface( &impl_from_msgws_closable( iface )->vtbl_msgws, iid, out );
}
static ULONG WINAPI msgws_closable_AddRef( void *iface ) { return msgws_AddRef( &impl_from_msgws_closable( iface )->vtbl_msgws ); }
static ULONG WINAPI msgws_closable_Release( void *iface ) { return msgws_Release( &impl_from_msgws_closable( iface )->vtbl_msgws ); }
static HRESULT WINAPI msgws_closable_GetIids( void *iface, ULONG *c, IID **i ) { return E_NOTIMPL; }
static HRESULT WINAPI msgws_closable_GetRuntimeClassName( void *iface, HSTRING *n ) { return E_NOTIMPL; }
static HRESULT WINAPI msgws_closable_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI msgws_closable_Close( void *iface ) { return S_OK; }

static const void *message_websocket_closable_vtbl[] =
{
    msgws_closable_QueryInterface,
    msgws_closable_AddRef,
    msgws_closable_Release,
    msgws_closable_GetIids,
    msgws_closable_GetRuntimeClassName,
    msgws_closable_GetTrustLevel,
    msgws_closable_Close,
};

static HRESULT create_message_websocket( void **out )
{
    struct message_websocket *impl;
    if (!out) return E_POINTER;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->vtbl_msgws = message_websocket_vtbl;
    impl->vtbl_msgws2 = message_websocket2_vtbl;
    impl->vtbl_msgws3 = message_websocket3_vtbl;
    impl->vtbl_ws = websocket_vtbl;
    impl->vtbl_closable = message_websocket_closable_vtbl;
    impl->ref = 1;
    *out = &impl->vtbl_msgws;
    return S_OK;
}

/* --- Activation Factories --- */

static HRESULT WINAPI stream_socket_statics_QueryInterface( void *iface, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IStreamSocketStatics ))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI stream_socket_statics_AddRef( void *iface ) { return 2; }
static ULONG WINAPI stream_socket_statics_Release( void *iface ) { return 1; }
static HRESULT WINAPI stream_socket_statics_GetIids( void *iface, ULONG *c, IID **i ) { return E_NOTIMPL; }
static HRESULT WINAPI stream_socket_statics_GetRuntimeClassName( void *iface, HSTRING *n )
{
    static const WCHAR class_name[] = L"Windows.Networking.Sockets.StreamSocket";
    return WindowsCreateString( class_name, ARRAY_SIZE(class_name) - 1, n );
}
static HRESULT WINAPI stream_socket_statics_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI stream_socket_statics_GetEndpointPairsAsync( void *iface, void *host, HSTRING svc, IInspectable **op )
{
    TRACE( "svc %s\n", debugstr_hstring( svc ) );
    return create_socket_async_op( (void **)op );
}
static HRESULT WINAPI stream_socket_statics_GetEndpointPairsAsync2( void *iface, void *host, HSTRING svc, int opt, IInspectable **op )
{
    TRACE( "svc %s, opt %d\n", debugstr_hstring( svc ), opt );
    return create_socket_async_op( (void **)op );
}

static const void *stream_socket_statics_vtbl[] =
{
    stream_socket_statics_QueryInterface,
    stream_socket_statics_AddRef,
    stream_socket_statics_Release,
    stream_socket_statics_GetIids,
    stream_socket_statics_GetRuntimeClassName,
    stream_socket_statics_GetTrustLevel,
    stream_socket_statics_GetEndpointPairsAsync,
    stream_socket_statics_GetEndpointPairsAsync2,
};
static const void *stream_socket_statics_obj = stream_socket_statics_vtbl;

static HRESULT WINAPI stream_socket_factory_QueryInterface( void *iface, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = iface;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IStreamSocketStatics ))
    {
        *out = (void *)&stream_socket_statics_obj;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI stream_socket_factory_AddRef( void *iface ) { return 2; }
static ULONG WINAPI stream_socket_factory_Release( void *iface ) { return 1; }
static HRESULT WINAPI stream_socket_factory_GetIids( void *iface, ULONG *c, IID **i ) { return E_NOTIMPL; }
static HRESULT WINAPI stream_socket_factory_GetRuntimeClassName( void *iface, HSTRING *n )
{
    return WindowsCreateString( L"Windows.Networking.Sockets.StreamSocket", 39, n );
}
static HRESULT WINAPI stream_socket_factory_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI stream_socket_factory_ActivateInstance( void *iface, IInspectable **instance )
{
    return create_stream_socket( (void **)instance );
}
static const void *stream_socket_factory_vtbl[] =
{
    stream_socket_factory_QueryInterface,
    stream_socket_factory_AddRef,
    stream_socket_factory_Release,
    stream_socket_factory_GetIids,
    stream_socket_factory_GetRuntimeClassName,
    stream_socket_factory_GetTrustLevel,
    stream_socket_factory_ActivateInstance,
};
static const void *stream_socket_factory_obj = stream_socket_factory_vtbl;
static IActivationFactory *stream_socket_factory = (IActivationFactory *)&stream_socket_factory_obj;

static HRESULT WINAPI message_websocket_factory_QueryInterface( void *iface, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI message_websocket_factory_AddRef( void *iface ) { return 2; }
static ULONG WINAPI message_websocket_factory_Release( void *iface ) { return 1; }
static HRESULT WINAPI message_websocket_factory_GetIids( void *iface, ULONG *c, IID **i ) { return E_NOTIMPL; }
static HRESULT WINAPI message_websocket_factory_GetRuntimeClassName( void *iface, HSTRING *n )
{
    static const WCHAR class_name[] = L"Windows.Networking.Sockets.MessageWebSocket";
    return WindowsCreateString( class_name, ARRAY_SIZE(class_name) - 1, n );
}
static HRESULT WINAPI message_websocket_factory_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI message_websocket_factory_ActivateInstance( void *iface, IInspectable **instance )
{
    return create_message_websocket( (void **)instance );
}

static const void *message_websocket_factory_vtbl[] =
{
    message_websocket_factory_QueryInterface,
    message_websocket_factory_AddRef,
    message_websocket_factory_Release,
    message_websocket_factory_GetIids,
    message_websocket_factory_GetRuntimeClassName,
    message_websocket_factory_GetTrustLevel,
    message_websocket_factory_ActivateInstance,
};
static const void *message_websocket_factory_obj = message_websocket_factory_vtbl;
static IActivationFactory *message_websocket_factory = (IActivationFactory *)&message_websocket_factory_obj;

BOOL WINAPI DllMain( HINSTANCE instance, DWORD reason, void *reserved )
{
    return TRUE;
}

HRESULT WINAPI DllGetClassObject( REFCLSID clsid, REFIID riid, void **out )
{
    FIXME( "clsid %s, riid %s, out %p stub!\n", debugstr_guid( clsid ), debugstr_guid( riid ), out );
    return CLASS_E_CLASSNOTAVAILABLE;
}

HRESULT WINAPI DllGetActivationFactory( HSTRING classid, void **factory )
{
    const WCHAR *buffer = WindowsGetStringRawBuffer( classid, NULL );

    TRACE( "class %s, factory %p.\n", debugstr_hstring( classid ), factory );

    *factory = NULL;

    if (!wcscmp( buffer, L"Windows.Networking.Sockets.StreamSocket" ))
        IActivationFactory_QueryInterface( stream_socket_factory, &IID_IActivationFactory, factory );
    if (!wcscmp( buffer, L"Windows.Networking.Sockets.MessageWebSocket" ))
        IActivationFactory_QueryInterface( message_websocket_factory, &IID_IActivationFactory, factory );

    if (*factory) return S_OK;
    return E_NOINTERFACE;
}

HRESULT WINAPI DllCanUnloadNow( void )
{
    return S_FALSE;
}

HRESULT WINAPI DllRegisterServer( void )
{
    return S_OK;
}

HRESULT WINAPI DllUnregisterServer( void )
{
    return S_OK;
}
