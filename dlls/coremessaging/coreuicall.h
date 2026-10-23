/*
 * CoreUICall endpoint conversation interfaces
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
#ifndef __COREMESSAGING_COREUICALL_H
#define __COREMESSAGING_COREUICALL_H

#include "unknwn.h"

typedef struct IMessageCallSendHost IMessageCallSendHost;
typedef struct IMessageCallReceiveHost IMessageCallReceiveHost;

/*
 * Send host vtable.  Slot indices match the interface observed in the
 * Windows CoreMessaging implementation: PrepareSend at slot 3 (+24),
 * Send at slot 4 (+32), CancelSend at slot 5 (+40), ReportError at
 * slot 6 (+48).
 */
typedef struct IMessageCallSendHostVtbl
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)( IMessageCallSendHost *iface, REFIID iid, void **out );
    ULONG (STDMETHODCALLTYPE *AddRef)( IMessageCallSendHost *iface );
    ULONG (STDMETHODCALLTYPE *Release)( IMessageCallSendHost *iface );
    HRESULT (STDMETHODCALLTYPE *PrepareSend)( IMessageCallSendHost *iface, ULONG_PTR conn,
                                             ULONG flag, ULONG *size, void **buffer );
    HRESULT (STDMETHODCALLTYPE *Send)( IMessageCallSendHost *iface, ULONG_PTR conn,
                                      ULONG flag, void *buffer, ULONG size );
    HRESULT (STDMETHODCALLTYPE *CancelSend)( IMessageCallSendHost *iface, ULONG_PTR conn, ULONG flag );
    HRESULT (STDMETHODCALLTYPE *ReportError)( IMessageCallSendHost *iface, HRESULT *error );
} IMessageCallSendHostVtbl;

struct IMessageCallSendHost
{
    const IMessageCallSendHostVtbl *lpVtbl;
};

/* Receive host vtable. */
typedef struct IMessageCallReceiveHostVtbl
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)( IMessageCallReceiveHost *iface, REFIID iid, void **out );
    ULONG (STDMETHODCALLTYPE *AddRef)( IMessageCallReceiveHost *iface );
    ULONG (STDMETHODCALLTYPE *Release)( IMessageCallReceiveHost *iface );
    HRESULT (STDMETHODCALLTYPE *ProcessMessage)( IMessageCallReceiveHost *iface, ULONG_PTR conn,
                                                ULONG flag, void *buffer, ULONG size );
} IMessageCallReceiveHostVtbl;

struct IMessageCallReceiveHost
{
    const IMessageCallReceiveHostVtbl *lpVtbl;
};

ULONG WINAPI session_AddRef( IUnknown *iface );
ULONG WINAPI session_Release( IUnknown *iface );
ULONG WINAPI send_host_AddRef( IMessageCallSendHost *iface );
ULONG WINAPI receive_host_AddRef( IMessageCallReceiveHost *iface );

#endif /* __COREMESSAGING_COREUICALL_H */
