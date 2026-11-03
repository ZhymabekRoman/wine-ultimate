/* WinRT asynchronous operations that are born completed
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

#include "private.h"

WINE_DEFAULT_DEBUG_CHANNEL(winsystem);

/*
 * Windows.System.User property lookups are RemoteAsync calls into the per-user property store,
 * which a prefix has no provider for, so the operations this module hands out are created already
 * completed: IAsyncInfo::Status reports Completed, put_Completed invokes the handler synchronously
 * and GetResults returns the result built at creation time (here NULL, the documented "no such
 * property" answer).  Both the C++/CX and the C++/WinRT projection accept a completed operation
 * whose handler is called on the calling thread, and no failure is ever thrown, which is what the
 * app's await path needs to make progress instead of parking.
 *
 * The object is one allocation with the operation vtable at offset 0 and the IAsyncInfo vtable
 * pointer at offset sizeof(void *), so IAsyncInfo_QueryInterface can hand back the same object;
 * one vtable pair serves every closed instantiation because the parameterized IID and the runtime
 * class name are per-object fields.  Same shape as
 * dlls/windows.networking.pushnotifications/async.c.
 */

struct async_op
{
    const void *self_vtbl;
    const void *info_vtbl;
    LONG ref;
    const IID *iid;
    const WCHAR *class_name;
    void *handler;        /* IAsyncOperationCompletedHandler`1<T> *, owned */
    IInspectable *result; /* owned */
};

static inline struct async_op *impl_from_self( const void *iface )
{
    return (struct async_op *)iface;
}

static inline struct async_op *impl_from_info( const void *iface )
{
    return (struct async_op *)((const BYTE *)iface - sizeof(void *));
}

static void invoke_completed_handler( struct async_op *impl )
{
    void **vtbl;
    HRESULT (STDMETHODCALLTYPE *invoke)( void *handler, void *async, enum AsyncStatus status );

    if (!impl->handler) return;

    /* The completed handlers are { QueryInterface, AddRef, Release, Invoke }. */
    vtbl = *(void ***)impl->handler;
    invoke = (void *)vtbl[3];
    invoke( impl->handler, (void *)impl, Completed );
}

static HRESULT WINAPI op_QueryInterface( const void *iface, REFIID iid, void **out )
{
    struct async_op *impl = impl_from_self( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (!out) return E_POINTER;
    *out = NULL;

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, impl->iid ))
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

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG WINAPI op_AddRef( const void *iface )
{
    struct async_op *impl = impl_from_self( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );

    TRACE( "iface %p, new ref %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI op_Release( const void *iface )
{
    struct async_op *impl = impl_from_self( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    TRACE( "iface %p, new ref %lu.\n", iface, ref );

    if (!ref)
    {
        if (impl->handler) IUnknown_Release( (IUnknown *)impl->handler );
        if (impl->result) IInspectable_Release( impl->result );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI op_GetIids( const void *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI op_GetRuntimeClassName( const void *iface, HSTRING *class_name )
{
    struct async_op *impl = impl_from_self( iface );

    if (!class_name) return E_POINTER;
    return WindowsCreateString( impl->class_name, lstrlenW( impl->class_name ), class_name );
}

static HRESULT WINAPI op_GetTrustLevel( const void *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI op_put_Completed( const void *iface, void *handler )
{
    struct async_op *impl = impl_from_self( iface );

    TRACE( "iface %p, handler %p.\n", iface, handler );

    if (impl->handler) IUnknown_Release( (IUnknown *)impl->handler );
    impl->handler = handler;
    if (handler) IUnknown_AddRef( (IUnknown *)handler );

    /* The operation is born completed: a handler attached later fires at once. */
    invoke_completed_handler( impl );
    return S_OK;
}

static HRESULT WINAPI op_get_Completed( const void *iface, void **handler )
{
    struct async_op *impl = impl_from_self( iface );

    TRACE( "iface %p, handler %p.\n", iface, handler );

    if (!handler) return E_POINTER;
    *handler = impl->handler;
    if (*handler) IUnknown_AddRef( (IUnknown *)*handler );
    return S_OK;
}

static HRESULT WINAPI op_GetResults( const void *iface, void **results )
{
    struct async_op *impl = impl_from_self( iface );

    TRACE( "iface %p, results %p.\n", iface, results );

    if (!results) return E_POINTER;
    *results = (void *)impl->result;
    if (*results) IInspectable_AddRef( impl->result );
    return S_OK;
}

const void *async_op_vtbl[] =
{
    op_QueryInterface,
    op_AddRef,
    op_Release,
    op_GetIids,
    op_GetRuntimeClassName,
    op_GetTrustLevel,
    op_put_Completed,
    op_get_Completed,
    op_GetResults,
};

static HRESULT WINAPI info_QueryInterface( const void *iface, REFIID iid, void **out )
{
    struct async_op *impl = impl_from_info( iface );

    return op_QueryInterface( (void *)impl, iid, out );
}

static ULONG WINAPI info_AddRef( const void *iface )
{
    struct async_op *impl = impl_from_info( iface );

    return op_AddRef( (void *)impl );
}

static ULONG WINAPI info_Release( const void *iface )
{
    struct async_op *impl = impl_from_info( iface );

    return op_Release( (void *)impl );
}

static HRESULT WINAPI info_GetIids( const void *iface, ULONG *iid_count, IID **iids )
{
    struct async_op *impl = impl_from_info( iface );

    return op_GetIids( (void *)impl, iid_count, iids );
}

static HRESULT WINAPI info_GetRuntimeClassName( const void *iface, HSTRING *class_name )
{
    if (!class_name) return E_POINTER;
    return WindowsCreateString( L"Windows.Foundation.IAsyncInfo", 29, class_name );
}

static HRESULT WINAPI info_GetTrustLevel( const void *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI info_get_Id( const void *iface, UINT32 *id )
{
    TRACE( "iface %p, id %p.\n", iface, id );
    if (!id) return E_POINTER;
    *id = 1;
    return S_OK;
}

static HRESULT WINAPI info_get_Status( const void *iface, enum AsyncStatus *status )
{
    TRACE( "iface %p, status %p.\n", iface, status );
    if (!status) return E_POINTER;
    *status = Completed;
    return S_OK;
}

static HRESULT WINAPI info_get_ErrorCode( const void *iface, HRESULT *error_code )
{
    TRACE( "iface %p, error_code %p.\n", iface, error_code );
    if (!error_code) return E_POINTER;
    *error_code = S_OK;
    return S_OK;
}

static HRESULT WINAPI info_Cancel( const void *iface )
{
    TRACE( "iface %p.\n", iface );
    return S_OK;
}

static HRESULT WINAPI info_Close( const void *iface )
{
    TRACE( "iface %p.\n", iface );
    return S_OK;
}

const void *async_info_vtbl[] =
{
    info_QueryInterface,
    info_AddRef,
    info_Release,
    info_GetIids,
    info_GetRuntimeClassName,
    info_GetTrustLevel,
    info_get_Id,
    info_get_Status,
    info_get_ErrorCode,
    info_Cancel,
    info_Close,
};

struct async_op *create_async_op( const IID *iid, const WCHAR *class_name, IInspectable *result )
{
    struct async_op *impl;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return NULL;

    impl->self_vtbl = async_op_vtbl;
    impl->info_vtbl = async_info_vtbl;
    impl->ref = 1;
    impl->iid = iid;
    impl->class_name = class_name;
    impl->result = result;
    if (result) IInspectable_AddRef( result );

    TRACE( "created op %p, iid %s, result %p.\n", impl, debugstr_guid( iid ), result );
    return impl;
}

void *async_op_iface( struct async_op *impl )
{
    return (void *)impl;
}

void async_op_release( struct async_op *impl )
{
    op_Release( (void *)impl );
}
