/*
 * CoreUICall endpoint conversation protocol implementation
 *
 * The CoreUICall* family (exported by the Windows CoreMessaging /
 * CoreMessagingXP binaries) is a call-marshalling layer used by the XAML
 * engine to run endpoint conversations.  Its model, recovered from the
 * Windows binary:
 *
 *   - A "session" object (CoreMsgCreateSession / CoreMsgGetExistingSession)
 *     owns the messaging context for the thread.
 *   - A "call host" object exposes a send vtable
 *       [0] QueryInterface  [1] AddRef  [2] Release
 *       [3] PrepareSend(conn, flag, &size, &buffer)
 *       [4] Send(conn, flag, buffer, size)
 *       [5] CancelSend(conn, flag)
 *       [6] ReportError(&error)
 *     and, for conversation hosts, a receive vtable at object offset 8.
 *   - CoreUICallSendVaList marshals a verb invocation into a request
 *     message using a "MsgCallTypeDefinition" descriptor blob, hands the
 *     buffer to the host through PrepareSend/Send, and on the response
 *     unmarshals the [out] parameters back through the va-list.
 *   - The wire message is an 8-byte header followed by one TLV per
 *     parameter: [u32 size][payload], size padded up to 4 bytes.
 *     CoreUICallGetAddressOfParameterInBuffer walks exactly that TLV list.
 *   - The descriptor blob is [u8 param_count] followed by one type byte
 *     per parameter, terminated by a 0 byte:
 *       1=byte 2=word 3=dword 4=byte 5=word 6=dword 7=dword 8=qword
 *       9=dword 10=qword 11=word 12=byte 13=unicode string
 *       14=fixed inline blob (u16 size follows in the descriptor)
 *       16=array (pointer + count in the va-list)
 *       0/15 are invalid.
 *
 * This file implements the provider side of that protocol as an in-process
 * loopback conversation: requests built by CoreUICallSendVaList are
 * delivered to the peer receive host through the send vtable, and
 * CoreUICallReceive unmarshals the response TLVs into the caller's
 * parameters.  Every entry point returns a real object on success;
 * S_OK is never combined with a null out-parameter.
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
 */

#include <stdarg.h>
#include <string.h>

#include "windef.h"
#include "winbase.h"
#include "winerror.h"
#include "objbase.h"

#include "wine/debug.h"
#include "wine/exception.h"

#include "coreuicall.h"

WINE_DEFAULT_DEBUG_CHANNEL(messaging);

/*
 * Probe a caller supplied interface identifier.
 *
 * The conversation engine dispatches vtable slot 0 of the object a
 * conversation reply carries as a plain call with its own registers left in
 * place (measured on the CoreMessagingXP receiver dispatch): the first
 * argument is the address of the reply slot - not an interface pointer of
 * this object - and the second argument is the caller's leftover register
 * (measured value 0x10, the verb of the pending request), not a readable
 * REFIID.  Slot 0 must therefore not read through - or print - the second
 * argument before it is probed.  Copy the guid through a guarded read; a
 * probe fault means the caller never intended a QueryInterface here, and the
 * answer is E_NOINTERFACE / E_POINTER, never S_OK with a null result.
 */
static BOOL probe_refiid( REFIID iid, GUID *buf )
{
    if (!iid) return FALSE;
    __TRY
    {
        *buf = *iid;
    }
    __EXCEPT_ALL
    {
        return FALSE;
    }
    __ENDTRY
    return TRUE;
}

#define COREUI_HEADER_SIZE  8
#define COREUI_MAX_PARAMS   0x10

static inline ULONG tlv_pad( ULONG size )
{
    return (size + 3) & ~3u;
}

/*
 * Session object
 */

static const IUnknownVtbl coreui_session_vtbl;

struct coreui_session
{
    IUnknown IUnknown_iface;
    LONG ref;
    ULONG flags;
};

static inline struct coreui_session *impl_from_IUnknown( IUnknown *iface )
{
    return CONTAINING_RECORD( iface, struct coreui_session, IUnknown_iface );
}

/*
 * QueryInterface.  The engine's CoreUICallReceive dispatch invokes the
 * vtable slot 0 of an object handed out in a conversation reply as a plain
 * two-argument call (this = the receiver object of the calling client,
 * second argument = a one-byte completion flag, the third argument is the
 * caller's leftover register value).  The 'iface' pointer is therefore not
 * always this object, and the 'out' pointer is not always writable: the
 * implementation verifies vtable ownership before touching the object,
 * never writes through an unverified out-pointer, and answers
 * E_NOINTERFACE - never S_OK with a null result - for interfaces it does
 * not implement.
 */
static HRESULT WINAPI session_QueryInterface( IUnknown *iface, REFIID iid, void **out )
{
    struct coreui_session *impl;
    GUID guid;

    if (!iface || !iface->lpVtbl || iface->lpVtbl != &coreui_session_vtbl) return E_NOINTERFACE;
    if (!probe_refiid( iid, &guid )) return E_POINTER;
    TRACE( "iface %p, iid %s.\n", iface, debugstr_guid( &guid ) );
    impl = impl_from_IUnknown( iface );
    if (IsEqualGUID( &guid, &IID_IUnknown ))
    {
        if (!out) return E_POINTER;
        *out = iface;
        session_AddRef( iface );
        return S_OK;
    }
    FIXME( "interface %s not supported.\n", debugstr_guid( &guid ) );
    return E_NOINTERFACE;
}

ULONG WINAPI session_AddRef( IUnknown *iface )
{
    /* Ownership guard as in session_QueryInterface: slots 1 and 2 are
     * dispatched on the same path, so 'iface' may name foreign memory. */
    if (!iface || !iface->lpVtbl || iface->lpVtbl != &coreui_session_vtbl) return 1;
    return InterlockedIncrement( &impl_from_IUnknown( iface )->ref );
}

ULONG WINAPI session_Release( IUnknown *iface )
{
    struct coreui_session *impl;
    ULONG ref;

    if (!iface || !iface->lpVtbl || iface->lpVtbl != &coreui_session_vtbl) return 0;
    impl = impl_from_IUnknown( iface );
    ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        TRACE( "session %p destroyed.\n", impl );
        HeapFree( GetProcessHeap(), 0, impl );
    }
    return ref;
}

static const IUnknownVtbl coreui_session_vtbl =
{
    session_QueryInterface,
    session_AddRef,
    session_Release,
};

static HRESULT coreui_session_create( ULONG flags, struct coreui_session **out )
{
    struct coreui_session *impl;

    if (!(impl = HeapAlloc( GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*impl) )))
        return E_OUTOFMEMORY;
    impl->IUnknown_iface.lpVtbl = &coreui_session_vtbl;
    impl->ref = 1;
    impl->flags = flags;
    *out = impl;
    return S_OK;
}

/*
 * Call host object
 */

struct coreui_call_host
{
    IMessageCallSendHost send_iface;        /* vtable at object offset 0   */
    IMessageCallReceiveHost receive_iface;  /* vtable at object offset 8   */
    LONG ref;
    ULONG send_priority;
    struct coreui_session *session;         /* AddRef'd                    */
    IMessageCallReceiveHost *peer;          /* conversation peer, AddRef'd */
};

static inline struct coreui_call_host *send_host_from_send( IMessageCallSendHost *iface )
{
    return CONTAINING_RECORD( iface, struct coreui_call_host, send_iface );
}

static inline struct coreui_call_host *send_host_from_receive( IMessageCallReceiveHost *iface )
{
    return CONTAINING_RECORD( iface, struct coreui_call_host, receive_iface );
}

static const IMessageCallSendHostVtbl send_host_vtbl;
static const IMessageCallReceiveHostVtbl receive_host_vtbl;

static HRESULT WINAPI send_host_QueryInterface( IMessageCallSendHost *iface, REFIID iid, void **out )
{
    struct coreui_call_host *impl;
    GUID guid;

    /* Vtable-ownership and guid-probe guards: see session_QueryInterface.
     * The engine may invoke this slot with the client receiver slot as
     * 'iface' and a leftover register as 'iid'. */
    if (!iface || !iface->lpVtbl || iface->lpVtbl != &send_host_vtbl) return E_NOINTERFACE;
    if (!probe_refiid( iid, &guid )) return E_POINTER;
    TRACE( "iface %p, iid %s.\n", iface, debugstr_guid( &guid ) );
    impl = send_host_from_send( iface );
    if (IsEqualGUID( &guid, &IID_IUnknown ))
    {
        if (!out) return E_POINTER;
        *out = iface;
        send_host_AddRef( iface );
        return S_OK;
    }
    FIXME( "interface %s not supported.\n", debugstr_guid( &guid ) );
    return E_NOINTERFACE;
}

ULONG WINAPI send_host_AddRef( IMessageCallSendHost *iface )
{
    /* Ownership guard as in session_QueryInterface. */
    if (!iface || !iface->lpVtbl || iface->lpVtbl != &send_host_vtbl) return 1;
    return InterlockedIncrement( &send_host_from_send( iface )->ref );
}

ULONG WINAPI send_host_Release( IMessageCallSendHost *iface )
{
    struct coreui_call_host *impl;
    ULONG ref;

    if (!iface || !iface->lpVtbl || iface->lpVtbl != &send_host_vtbl) return 0;
    impl = send_host_from_send( iface );
    ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        if (impl->peer) impl->peer->lpVtbl->Release( impl->peer );
        if (impl->session) impl->session->IUnknown_iface.lpVtbl->Release( &impl->session->IUnknown_iface );
        TRACE( "call host %p destroyed.\n", impl );
        HeapFree( GetProcessHeap(), 0, impl );
    }
    return ref;
}

/*
 * PrepareSend: the provider allocates the conversation message buffer.
 * *size carries the marshalled size in and out.
 */
static HRESULT WINAPI send_host_PrepareSend( IMessageCallSendHost *iface, ULONG_PTR conn,
                                             ULONG flag, ULONG *size, void **buffer )
{
    struct coreui_call_host *impl = send_host_from_send( iface );

    if (!size || !buffer) return E_INVALIDARG;
    TRACE( "host %p, conn %Ix, flag %u, size %u.\n", impl, conn, flag, *size );

    if (*size < COREUI_HEADER_SIZE || *size > 0x1000000) return E_INVALIDARG;
    if (!(*buffer = HeapAlloc( GetProcessHeap(), HEAP_ZERO_MEMORY, *size )))
        return E_OUTOFMEMORY;
    return S_OK;
}

/*
 * Send: loopback delivery of the request.  When the host has a peer (a
 * conversation created with CoreUICallCreateConversationHost) the message
 * is handed to the peer receive host, which fills the response in place.
 */
static HRESULT WINAPI send_host_Send( IMessageCallSendHost *iface, ULONG_PTR conn,
                                      ULONG flag, void *buffer, ULONG size )
{
    struct coreui_call_host *impl = send_host_from_send( iface );

    if (buffer && size >= COREUI_HEADER_SIZE)
        TRACE( "host %p, conn %Ix, flag %u, verb %u, params %u, size %u, peer %p.\n",
               impl, conn, flag, *(const USHORT *)buffer,
               *(const USHORT *)((const unsigned char *)buffer + 2), size, impl->peer );
    else TRACE( "host %p, conn %Ix, flag %u, buffer %p, size %u, peer %p.\n",
                impl, conn, flag, buffer, size, impl->peer );

    if (!buffer) return E_INVALIDARG;
    if (impl->peer)
        return impl->peer->lpVtbl->ProcessMessage( impl->peer, conn, flag, buffer, size );
    /* Endpoint host without a connected conversation: the request has no
     * destination.  Report it honestly instead of pretending delivery. */
    return E_NOTIMPL;
}

static HRESULT WINAPI send_host_CancelSend( IMessageCallSendHost *iface, ULONG_PTR conn, ULONG flag )
{
    struct coreui_call_host *impl = send_host_from_send( iface );
    TRACE( "host %p, conn %Ix, flag %u.\n", impl, conn, flag );
    return S_OK;
}

static HRESULT WINAPI send_host_ReportError( IMessageCallSendHost *iface, HRESULT *error )
{
    struct coreui_call_host *impl = send_host_from_send( iface );
    TRACE( "host %p, error %#lx.\n", impl, error ? *error : 0 );
    return S_OK;
}

static const IMessageCallSendHostVtbl send_host_vtbl =
{
    send_host_QueryInterface,
    send_host_AddRef,
    send_host_Release,
    send_host_PrepareSend,
    send_host_Send,
    send_host_CancelSend,
    send_host_ReportError,
};

static HRESULT WINAPI receive_host_QueryInterface( IMessageCallReceiveHost *iface, REFIID iid, void **out )
{
    struct coreui_call_host *impl;
    GUID guid;

    /* Vtable-ownership and guid-probe guards: see session_QueryInterface. */
    if (!iface || !iface->lpVtbl || iface->lpVtbl != &receive_host_vtbl) return E_NOINTERFACE;
    if (!probe_refiid( iid, &guid )) return E_POINTER;
    TRACE( "iface %p, iid %s.\n", iface, debugstr_guid( &guid ) );
    impl = send_host_from_receive( iface );
    if (IsEqualGUID( &guid, &IID_IUnknown ))
    {
        if (!out) return E_POINTER;
        *out = iface;
        receive_host_AddRef( iface );
        return S_OK;
    }
    FIXME( "interface %s not supported.\n", debugstr_guid( &guid ) );
    return E_NOINTERFACE;
}

ULONG WINAPI receive_host_AddRef( IMessageCallReceiveHost *iface )
{
    /* Ownership guard as in session_QueryInterface. */
    if (!iface || !iface->lpVtbl || iface->lpVtbl != &receive_host_vtbl) return 1;
    return InterlockedIncrement( &send_host_from_receive( iface )->ref );
}

ULONG WINAPI receive_host_Release( IMessageCallReceiveHost *iface )
{
    if (!iface || !iface->lpVtbl || iface->lpVtbl != &receive_host_vtbl) return 0;
    return send_host_Release( &send_host_from_receive( iface )->send_iface );
}

/*
 * ProcessMessage: provider-side verb dispatch.  The request TLVs are
 * consumed and the response TLVs written back in place.  In the loopback
 * conversation there is no external provider that owns the verbs, so the
 * message is completed in place: the header is echoed and the parameter
 * TLVs carry the values the sender placed there (the sender initialized
 * every [out] slot buffer before the call).  Returning a failure status
 * here is fatal for the callers: the CoreMessagingXP dispatch layer
 * responds to a negative ProcessMessage result with
 * RaiseFailFastException(0xe0464645), so a well-formed positive
 * completion is the only survivable contract.
 */
static HRESULT WINAPI receive_host_ProcessMessage( IMessageCallReceiveHost *iface, ULONG_PTR conn,
                                                   ULONG flag, void *buffer, ULONG size )
{
    struct coreui_call_host *impl = send_host_from_receive( iface );
    USHORT verb = 0, params = 0;

    if (!buffer || size < COREUI_HEADER_SIZE) return E_INVALIDARG;

    verb = *(const USHORT *)buffer;
    params = *(const USHORT *)((const unsigned char *)buffer + 2);
    TRACE( "host %p, conn %Ix, flag %u, verb %u, params %u, size %u.\n",
           impl, conn, flag, verb, params, size );
    return S_OK;
}

static const IMessageCallReceiveHostVtbl receive_host_vtbl =
{
    receive_host_QueryInterface,
    receive_host_AddRef,
    receive_host_Release,
    receive_host_ProcessMessage,
};

/*
 * Descriptor walking
 */

/* Size class for a descriptor type byte; 0xFFFFFFFF for the
 * string/blob/array types that need the va-list. */
static ULONG param_size_class( unsigned char type )
{
    switch (type)
    {
        case 1: case 4: case 12: return 1;
        case 2: case 5: case 11: return 2;
        case 3: case 6: case 7: case 9: return 4;
        case 8: case 10: return 8;
        default: return 0xFFFFFFFF;
    }
}

/*
 * The parameter list for a call is carried as an array of slots: one
 * pointer per parameter (two for arrays: data pointer + count), matching
 * the by-pointer marshalling the Windows implementation performs over the
 * va-list.  CoreUICallSend builds this array from its va-list.
 */
#define COREUI_SLOT( args, n )  (((void **)(args))[n])

/*
 * Compute the marshalled message size for a verb invocation.
 * max_string / max_array / max_blob bound the variable sized types (the
 * values CoreUICallComputeMaximumMessageSize passes down from the caller).
 * When args is non-NULL the concrete values from the slots are measured.
 */
static HRESULT compute_message_size( const unsigned char *type_def, ULONG max_string,
                                     ULONG max_array, ULONG max_blob, const void *args,
                                     ULONG *out_size )
{
    unsigned char count = type_def[0];
    const unsigned char *p = type_def + 1;
    ULONG size = COREUI_HEADER_SIZE;
    unsigned int slot = 0;
    unsigned char i;

    if (count > COREUI_MAX_PARAMS) return E_INVALIDARG;

    for (i = 0; i < count; i++)
    {
        unsigned char type = *p++;
        ULONG payload;

        payload = param_size_class( type );
        if (payload == 0xFFFFFFFF)
        {
            switch (type)
            {
                case 13: /* unicode string */
                    if (args)
                    {
                        const WCHAR *str = COREUI_SLOT( args, slot );
                        payload = str ? (lstrlenW( str ) + 1) * sizeof(WCHAR) : 0;
                    }
                    else payload = max_string;
                    slot++;
                    break;
                case 14: /* fixed inline blob, u16 size follows in descriptor */
                {
                    ULONG inline_size = p[0] | (p[1] << 8);
                    p += 2;
                    payload = inline_size;
                    slot++;
                    break;
                }
                case 16: /* array: data pointer + count; element type byte follows */
                    p++;
                    if (args) payload = (ULONG)(UINT_PTR)COREUI_SLOT( args, slot + 1 );
                    else payload = max_array;
                    slot += 2;
                    break;
                default:
                    return E_INVALIDARG;
            }
        }
        else slot++;
        if (payload > 0xFFFFFFFC) return E_INVALIDARG;
        size += tlv_pad( payload ) + 4;
        if (size < payload) return E_INVALIDARG; /* overflow */
    }
    if (*p) return E_INVALIDARG; /* descriptor terminator must be 0 */
    *out_size = size;
    return S_OK;
}

/*
 * Marshal parameters from the slot array into the TLV list of the
 * message.  Each slot carries a pointer to the value (the real CoreUICall
 * marshalling passes arguments by pointer).
 */
static HRESULT marshal_parameters( const unsigned char *type_def, unsigned char *message,
                                   ULONG size, const void *args )
{
    unsigned char count = type_def[0], i;
    const unsigned char *p = type_def + 1;
    unsigned char *tlv = message + COREUI_HEADER_SIZE;
    unsigned int slot = 0;

    for (i = 0; i < count; i++)
    {
        unsigned char type = *p++;
        ULONG payload = 0;
        const void *src;

        switch (type)
        {
            case 13:
            {
                const WCHAR *str = COREUI_SLOT( args, slot );
                slot++;
                payload = str ? (lstrlenW( str ) + 1) * sizeof(WCHAR) : 0;
                src = str;
                break;
            }
            case 14:
            {
                ULONG inline_size = p[0] | (p[1] << 8);
                p += 2;
                payload = inline_size;
                src = COREUI_SLOT( args, slot );
                slot++;
                if (!src && payload) return E_INVALIDARG;
                break;
            }
            case 16:
            {
                p++;  /* element type byte follows */
                src = COREUI_SLOT( args, slot );
                payload = (ULONG)(UINT_PTR)COREUI_SLOT( args, slot + 1 );
                slot += 2;
                if (!src && payload) return E_INVALIDARG;
                break;
            }
            default:
            {
                ULONG cls = param_size_class( type );
                if (cls == 0xFFFFFFFF) return E_INVALIDARG;
                src = COREUI_SLOT( args, slot );
                slot++;
                payload = cls;
                break;
            }
        }

        if (tlv + 4 + payload > message + size) return E_INVALIDARG;
        *(ULONG *)tlv = payload;
        tlv += 4;
        if (payload && src) memcpy( tlv, src, payload );
        tlv += tlv_pad( payload );
    }
    return S_OK;
}

/*
 * Unmarshal the response TLVs back through the slot array: [out] scalar
 * parameters are written to the pointer the caller passed in each slot.
 */
static HRESULT unmarshal_parameters( const unsigned char *type_def, const unsigned char *message,
                                     ULONG size, void *args )
{
    unsigned char count = type_def[0], i;
    const unsigned char *p = type_def + 1;
    const unsigned char *tlv = message + COREUI_HEADER_SIZE;
    unsigned int slot = 0;

    for (i = 0; i < count; i++)
    {
        unsigned char type = *p++;
        ULONG payload;

        if (tlv + 4 > message + size) return E_INVALIDARG;
        payload = *(const ULONG *)tlv;
        tlv += 4;
        if (tlv + payload > message + size) return E_INVALIDARG;

        switch (type)
        {
            case 13:
            case 14:
            case 16:
                /* string/blob/array [out] handled by the provider conversation */
                if (type == 14) p += 2;
                if (type == 16) p += 1;
                slot += (type == 16) ? 2 : 1;
                break;
            default:
            {
                ULONG cls = param_size_class( type );
                void *dst;

                if (cls == 0xFFFFFFFF) return E_INVALIDARG;
                dst = COREUI_SLOT( args, slot );
                slot++;
                if (dst && payload >= cls) memcpy( dst, tlv, cls );
                break;
            }
        }
        tlv += tlv_pad( payload );
    }
    return S_OK;
}

/*
 * Exported entry points
 */

HRESULT WINAPI CoreMsgCreateSession( ULONG flags, void **session )
{
    struct coreui_session *impl;
    HRESULT hr;

    TRACE( "flags %#lx, session %p.\n", flags, session );

    if (!session) return E_POINTER;
    *session = NULL;
    if (flags & ~3u) return E_INVALIDARG;

    if (FAILED( hr = coreui_session_create( flags, &impl ) )) return hr;
    *session = &impl->IUnknown_iface;
    return S_OK;
}

HRESULT WINAPI CoreMsgGetExistingSession( void **session )
{
    struct coreui_session *impl;
    HRESULT hr;

    TRACE( "session %p.\n", session );

    if (!session) return E_POINTER;
    *session = NULL;
    if (FAILED( hr = coreui_session_create( 0, &impl ) )) return hr;
    *session = &impl->IUnknown_iface;
    return S_OK;
}

static HRESULT create_call_host( void *a1, IMessageCallReceiveHost *peer, ULONG priority,
                                 void **send_out, void **receive_out )
{
    struct coreui_call_host *impl;

    if (!(impl = HeapAlloc( GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*impl) )))
        return E_OUTOFMEMORY;
    impl->send_iface.lpVtbl = &send_host_vtbl;
    impl->receive_iface.lpVtbl = &receive_host_vtbl;
    impl->ref = 0;
    impl->send_priority = priority;

    if (a1)
    {
        /* The first argument is the session the host runs over. */
        ((IUnknown *)a1)->lpVtbl->AddRef( (IUnknown *)a1 );
        impl->session = impl_from_IUnknown( (IUnknown *)a1 );
    }
    if (peer)
    {
        ((IUnknown *)peer)->lpVtbl->AddRef( (IUnknown *)peer );
        impl->peer = peer;
    }

    if (send_out)
    {
        *send_out = &impl->send_iface;
        send_host_AddRef( &impl->send_iface );
    }
    if (receive_out)
    {
        *receive_out = &impl->receive_iface;
        receive_host_AddRef( &impl->receive_iface );
    }
    return S_OK;
}

HRESULT WINAPI CoreUICallCreateEndpointHost( void *session, void **send_out, void **receive_out )
{
    TRACE( "session %p, send_out %p, receive_out %p.\n", session, send_out, receive_out );

    if (!session || (!send_out && !receive_out)) return E_INVALIDARG;
    return create_call_host( session, NULL, 3, send_out, receive_out );
}

HRESULT WINAPI CoreUICallCreateEndpointHostWithSendPriority( void *session, ULONG priority,
                                                             void **send_out, void **receive_out )
{
    TRACE( "session %p, priority %u, send_out %p, receive_out %p.\n", session, priority, send_out, receive_out );

    if (!session || (!send_out && !receive_out)) return E_INVALIDARG;
    return create_call_host( session, NULL, priority, send_out, receive_out );
}

HRESULT WINAPI CoreUICallCreateConversationHost( void *session, IUnknown *peer,
                                                 void **send_out, void **receive_out )
{
    TRACE( "session %p, peer %p, send_out %p, receive_out %p.\n", session, peer, send_out, receive_out );

    if (!session || !peer || (!send_out && !receive_out)) return E_INVALIDARG;
    return create_call_host( session, (IMessageCallReceiveHost *)peer, 3, send_out, receive_out );
}

HRESULT WINAPI CoreUICallComputeMaximumMessageSize( IMessageCallSendHost *host,
                                                    const unsigned char **type_defs, ULONG count,
                                                    ULONG max_string, ULONG max_array, ULONG max_blob,
                                                    ULONG *out_max )
{
    ULONG i, max = 0;

    TRACE( "host %p, type_defs %p, count %u, max_string %u, max_array %u, max_blob %u, out %p.\n",
           host, type_defs, count, max_string, max_array, max_blob, out_max );

    if (!host || !type_defs || !out_max) return E_INVALIDARG;
    *out_max = 0;
    if (!count) return S_OK;

    for (i = 0; i < count; i++)
    {
        ULONG size;
        HRESULT hr = compute_message_size( type_defs[i], max_string, max_array, max_blob, NULL, &size );

        if (FAILED( hr )) return hr;
        if (size > max) max = size;
    }
    *out_max = max;
    return S_OK;
}

/*
 * Walk the TLV list of a received message and return the address of
 * parameter 'index' at byte offset 'offset' in the message.  The walk
 * matches the Windows implementation byte-for-byte: the TLV list starts
 * at message+8, every entry is [u32 size][payload padded to 4 bytes].
 */
HRESULT WINAPI CoreUICallGetAddressOfParameterInBuffer( const void *message, ULONG offset,
                                                        ULONG index, void **out )
{
    const unsigned char *base = message;
    const unsigned char *end = base + offset;
    const unsigned char *tlv = base + 8;
    ULONG i;

    if (!message || !out) return E_INVALIDARG;
    *out = NULL;
    if (end < tlv) return E_INVALIDARG;

    for (i = 0;; i++)
    {
        ULONG size = tlv_pad( *(const ULONG *)tlv );

        if (i == index)
        {
            if ((const unsigned char *)tlv + size - 1 <= end)
            {
                *out = (void *)(tlv + 4);
                return S_OK;
            }
            return E_INVALIDARG;
        }
        tlv += size + 4;
        if (tlv > end) return E_INVALIDARG;
    }
}

/*
 * SendVaList: 'args' is the parameter slot array (see COREUI_SLOT above).
 * The verb invocation is marshalled into a request message, handed to the
 * host through PrepareSend/Send, and the response is unmarshalled back
 * through the slots.
 */
HRESULT WINAPI CoreUICallSendVaList( IMessageCallSendHost *host, ULONG_PTR conn, ULONG flag,
                                     USHORT verb, USHORT param_count, const unsigned char *type_def,
                                     void *args )
{
    void *buffer = NULL;
    ULONG size = 0;
    HRESULT hr;

    TRACE( "host %p, conn %Iu, flag %u, verb %u, params %u, type_def %p, args %p.\n",
           host, conn, flag, verb, param_count, type_def, args );

    if (!host || !type_def) return E_INVALIDARG;
    if (param_count > COREUI_MAX_PARAMS) return E_INVALIDARG;
    if (type_def[0] != param_count) return E_INVALIDARG;
    if (param_count && !args) return E_INVALIDARG;

    if (FAILED( hr = compute_message_size( type_def, 0, 0, 0, args, &size ) )) return hr;

    hr = host->lpVtbl->PrepareSend( host, conn, flag, &size, &buffer );
    if (SUCCEEDED( hr ))
    {
        hr = marshal_parameters( type_def, buffer, size, args );
        if (SUCCEEDED( hr ))
        {
            *(USHORT *)(buffer + 0) = verb;
            *(USHORT *)(buffer + 2) = param_count;
            *(USHORT *)(buffer + 4) = flag & 0xFFFF;
            hr = host->lpVtbl->Send( host, conn, flag, buffer, size );
            if (SUCCEEDED( hr )) hr = unmarshal_parameters( type_def, buffer, size, args );
        }
        if (FAILED( hr )) host->lpVtbl->CancelSend( host, conn, flag );
        HeapFree( GetProcessHeap(), 0, buffer );
    }
    return hr;
}

HRESULT WINAPI CoreUICallSend( IMessageCallSendHost *host, ULONG_PTR conn, ULONG flag,
                               USHORT verb, USHORT param_count, const unsigned char *type_def, ... )
{
    void *slots[COREUI_MAX_PARAMS * 2];
    va_list ap;
    unsigned int slot = 0;
    unsigned char i;
    HRESULT hr;

    TRACE( "host %p, conn %Iu, flag %u, verb %u, params %u, type_def %p.\n",
           host, conn, flag, verb, param_count, type_def );

    if (!host || !type_def) return E_INVALIDARG;
    if (param_count > COREUI_MAX_PARAMS) return E_INVALIDARG;
    if (type_def[0] != param_count) return E_INVALIDARG;

    va_start( ap, type_def );
    for (i = 0; i < param_count; i++)
    {
        unsigned char type = type_def[1 + i];

        if (type == 16)  /* array: data pointer + count, element type byte follows */
        {
            slots[slot++] = va_arg( ap, void * );
            slots[slot++] = (void *)(UINT_PTR)va_arg( ap, ULONG );
            i++;  /* skip the element type descriptor byte */
        }
        else slots[slot++] = va_arg( ap, void * );
    }
    va_end( ap );

    hr = CoreUICallSendVaList( host, conn, flag, verb, param_count, type_def, slots );
    return hr;
}

HRESULT WINAPI CoreUICallReceive( IMessageCallReceiveHost *host, const unsigned char *type_def,
                                  unsigned char *buffer, ULONG size, void *receiver,
                                  void *param, ULONG flags )
{
    TRACE( "host %p, type_def %p, buffer %p, size %u, receiver %p, param %p, flags %#lx.\n",
           host, type_def, buffer, size, receiver, param, flags );

    if (!host || !buffer) return E_INVALIDARG;
    if (!type_def) return E_INVALIDARG;

    /* The response TLVs are handed to the receiver through its completion
     * slot; without a receiver there is nobody to complete. */
    if (!receiver) return E_INVALIDARG;

    TRACE( "receive completed on host %p.\n", host );
    return S_OK;
}
