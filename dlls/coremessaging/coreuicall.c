/*
 * CoreUICall apiset forwarders
 *
 * On Windows the ext-ms-win-coreuicall-l1-1-0 apiset is implemented by
 * CoreMessagingXP.dll itself: the app's XAML ABI layer (Microsoft.ui.xaml.dll)
 * and the CoreMessagingXP engine register their call hosts with the engine's
 * own implementation, and CoreUICallCreateConversationHost /
 * CoreUICallCreateEndpointHost record the caller's MsgCallTypeDefinition verb
 * records (16 bytes: call entry pointer, parameter offset, parameter count)
 * on the native connection.  The native receive dispatch then calls the verb
 * entry straight from that record (CoreMessagingXP RVA 0x8c19a:
 * 'mov rax,[rax] ; guard_dispatch_icall', the ABI being
 * HRESULT entry(void *receiver_slot, void *param_pointer_array)).
 *
 * Measured (WhatsApp 2.2620): when this apiset is hosted by a reimplementation
 * instead of the engine's own exports, the app's native side registers its
 * hosts with the reimplementation while the native CoreMessagingXP receive
 * path dispatches from its own never-populated verb table: the verb record
 * slot reads back NULL, the guarded indirect call goes to address 0, and the
 * process dies with a c0000005 access violation inside CoreUICallReceive on
 * Application.Start (bimodally: the managed exception machinery converts the
 * fault into FailFast, "Fatal error. System.AccessViolationException").
 *
 * Host the apiset as thin forwarders to the real CoreMessagingXP exports, so
 * every registration and dispatch runs the same engine code as on Windows.
 * There is no messaging logic on the Wine side: the payload is a verbatim
 * pipe and the engine supplies both ends of the conversation.
 *
 * Copyright 2026 Zhymabek Roman
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT WARRANTY OF ANY KIND, either express or implied.  See the
 * GNU Lesser General Public License for more details.
 */

#include <stdarg.h>
#include <string.h>

#include "windef.h"
#include "winbase.h"
#include "winerror.h"
#include "objbase.h"

#include "wine/debug.h"

#include "coreuicall.h"

WINE_DEFAULT_DEBUG_CHANNEL(messaging);

#define COREUI_MAX_PARAMS   0x10

/*
 * Resolve an export of the engine binary that hosts the real implementation.
 * The engine is loaded by the app before it initialises its conversation
 * infrastructure (Microsoft.ui.xaml.dll imports it directly); resolving per
 * call covers the ordering window and keeps the forwarders stateless.
 */
static void *cmxp_entry( const char *name )
{
    static const WCHAR nameW[] = {'C','o','r','e','M','e','s','s','a','g','i','n','g','X','P','.','d','l','l',0};
    HMODULE module;

    if (!(module = GetModuleHandleW( nameW ))) module = LoadLibraryW( nameW );
    if (!module) return NULL;
    return (void *)GetProcAddress( module, name );
}

#define DEFINE_CMX_FORWARDER( ret, name, params, args )       \
    ret WINAPI name params                                     \
    {                                                          \
        typedef ret (WINAPI *fn_t) params;                     \
        fn_t fn = cmxp_entry( #name );                         \
        TRACE( #name " -> %p\n", fn );                         \
        if (!fn) return E_FAIL;                                \
        return fn args;                                        \
    }

DEFINE_CMX_FORWARDER( HRESULT, CoreMsgCreateSession, ( ULONG flags, void **session ), ( flags, session ) )
DEFINE_CMX_FORWARDER( HRESULT, CoreMsgGetExistingSession, ( void **session ), ( session ) )
DEFINE_CMX_FORWARDER( HRESULT, CoreUICallComputeMaximumMessageSize,
                      ( IMessageCallSendHost *host, const unsigned char **type_defs, ULONG count,
                        ULONG max_string, ULONG max_array, ULONG max_blob, ULONG *out_max ),
                      ( host, type_defs, count, max_string, max_array, max_blob, out_max ) )
DEFINE_CMX_FORWARDER( HRESULT, CoreUICallCreateConversationHost,
                      ( void *session, IUnknown *peer, void **send_out, void **receive_out ),
                      ( session, peer, send_out, receive_out ) )
DEFINE_CMX_FORWARDER( HRESULT, CoreUICallCreateEndpointHost,
                      ( void *session, void **send_out, void **receive_out ),
                      ( session, send_out, receive_out ) )
DEFINE_CMX_FORWARDER( HRESULT, CoreUICallCreateEndpointHostWithSendPriority,
                      ( void *session, ULONG priority, void **send_out, void **receive_out ),
                      ( session, priority, send_out, receive_out ) )
DEFINE_CMX_FORWARDER( HRESULT, CoreUICallGetAddressOfParameterInBuffer,
                      ( const void *message, ULONG offset, ULONG index, void **out ),
                      ( message, offset, index, out ) )
DEFINE_CMX_FORWARDER( HRESULT, CoreUICallReceive,
                      ( IMessageCallReceiveHost *host, const unsigned char *type_def,
                        unsigned char *buffer, ULONG size, void *receiver, void *param, ULONG flags ),
                      ( host, type_def, buffer, size, receiver, param, flags ) )
DEFINE_CMX_FORWARDER( HRESULT, CoreUICallSendVaList,
                      ( IMessageCallSendHost *host, ULONG_PTR conn, ULONG flag,
                        USHORT verb, USHORT param_count, const unsigned char *type_def, void *args ),
                      ( host, conn, flag, verb, param_count, type_def, args ) )

/*
 * CoreUICallSend is varargs; the native entry gathers the slots and enters
 * the va-list worker.  Gather here and forward the gathered array: the slot
 * layout (array parameter = data pointer + count, element type byte follows
 * in the descriptor) is the documented calling convention of the entry.
 */
HRESULT WINAPI CoreUICallSend( IMessageCallSendHost *host, ULONG_PTR conn, ULONG flag,
                               USHORT verb, USHORT param_count, const unsigned char *type_def, ... )
{
    void *slots[COREUI_MAX_PARAMS * 2];
    typedef HRESULT (WINAPI *fn_t)( IMessageCallSendHost *, ULONG_PTR, ULONG, USHORT, USHORT,
                                    const unsigned char *, void * );
    fn_t fn;
    va_list ap;
    unsigned int slot = 0;
    unsigned char i;

    TRACE( "host %p, conn %Iu, flag %u, verb %u, params %u, type_def %p.\n",
           host, conn, flag, verb, param_count, type_def );

    if (!host || !type_def) return E_INVALIDARG;
    if (param_count > COREUI_MAX_PARAMS) return E_INVALIDARG;
    if (!(fn = cmxp_entry( "CoreUICallSendVaList" ))) return E_FAIL;

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

    return fn( host, conn, flag, verb, param_count, type_def, slots );
}
