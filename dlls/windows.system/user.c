/*
 * Windows.System.User runtime class
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
 * Windows.System.User, the per-user identity object the app stack resolves through
 * RoGetActivationFactory (older-era gate traces log a 'Failed to find library' line for the
 * class, and every such line is a probe that retry-loops on the REGDB_E_CLASSNOTREG answer).
 *
 * Metadata (windows-rs projection, crates/libs/windows/src/Windows/System/mod.rs):
 *   :2389 IUser {df9a26c6-e746-4bcd-b5d4-120103c4209b} - NonRoamableId, AuthenticationStatus,
 *         Type, GetPropertyAsync, GetPropertiesAsync, GetPictureAsync in that slot order
 *         (:2398-2409).  The IID is the same value the tree idl carries
 *         (include/windows.system.idl, interface IUser uuid df9a26c6-...), so the tree header is
 *         the first IID source here.
 *   :2521 IUserStatics {155eb23b-242a-45e0-a2e9-3171fc6a7fdd} - CreateWatcher, FindAllAsync,
 *         FindAllAsyncByType, FindAllAsyncByTypeAndStatus, GetFromId (:2530-2534).
 *   :2536 IUserStatics2 {74a37e11-2eb5-4487-b0d5-2c6790e013e9} - GetDefault (:2545), synchronous,
 *         contract v12.0 per the "Windows.System.User.GetDefault" page on learn.microsoft.com
 *         (device family Windows 10, version 2104, introduced in 10.0.20348.0).
 *
 * Documented contracts used for the builtin answers (learn.microsoft.com, Windows.System.User):
 *   GetDefault          "Gets an object representing the default user account."
 *   NonRoamableId       "Gets the user's non-roamable id."
 *   AuthenticationStatus"Gets the authentication status of the user."
 *   Type                "Gets the user type."
 *   GetPropertyAsync    "Gets a property for the user."  (RemoteAsync)
 *
 * A prefix has one interactive identity: the user the process is running as.  GetDefault and
 * GetFromId therefore answer that one user, its Type is LocalUser (the tree enum
 * Windows.System.UserType, include/windows.system.idl: LocalUser = 0 - the account is not a
 * remote/desktop-session redirect), its AuthenticationStatus is LocallyAuthenticated (the session
 * is signed in on the local machine: there is no remote logon redirect in a prefix), and its
 * NonRoamableId is the process token's user SID string, which is the honest per-user,
 * non-roamable, unique identifier a prefix can offer.
 *
 * Not implemented, with reason:
 *   - User.GetSystemUser and a User.NonInspectableType member: neither exists in the fetched
 *     windows-rs projection of the namespace (no 'GetSystemUser' / 'NonInspectableType' hit in
 *     Windows/System/mod.rs), so their interface membership and IIDs are unverifiable here and
 *     are left out rather than guessed (IID rule: never from memory).
 *   - IUserStatics::CreateWatcher and the three FindAllAsync overloads: they enumerate the
 *     system's user list out of the Windows UserInformation store, which a prefix has no
 *     provider for, so they answer E_NOTIMPL rather than fabricate a UserWatcher object (the
 *     tree declares no UserWatcher class at all) or an IVectorView<User> collection.  The slots
 *     exist in the vtable, which is what an app's metadata-derived call needs to land.
 *   - The property lookups return a born-completed operation with a null result: the documented
 *     outcome of asking for a property the user does not have, and the no-throw answer an
 *     app's await path needs (an error code here would originate a WinRT language exception).
 */

struct user
{
    IUser IUser_iface;
    LONG ref;

    HSTRING nonroamable_id;
};

static RTL_SRWLOCK user_lock = RTL_SRWLOCK_INIT;
static struct user *user_singleton; /* holds its own reference for the process lifetime */

static HRESULT build_nonroamable_id( HSTRING *out )
{
    WCHAR *sid = NULL, *name = NULL;
    TOKEN_USER *info = NULL;
    DWORD size = 0;
    HANDLE token;
    HRESULT hr = E_FAIL;

    *out = NULL;

    if (!OpenProcessToken( GetCurrentProcess(), TOKEN_QUERY, &token ))
        return HRESULT_FROM_WIN32( GetLastError() );

    while (!GetTokenInformation( token, TokenUser, info, size, &size ))
    {
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) goto done;
        LocalFree( info );
        if (!(info = LocalAlloc( LPTR, size ))) { hr = E_OUTOFMEMORY; goto done; }
    }

    if (!ConvertSidToStringSidW( info->User.Sid, &sid )) { hr = HRESULT_FROM_WIN32( GetLastError() ); goto done; }

    if (!(name = HeapAlloc( GetProcessHeap(), 0, (lstrlenW( sid ) + 1) * sizeof(*name) ))) { hr = E_OUTOFMEMORY; goto done; }
    lstrcpyW( name, sid );
    hr = WindowsCreateString( name, lstrlenW( name ), out );

done:
    CloseHandle( token );
    LocalFree( info );
    if (sid) LocalFree( sid );
    HeapFree( GetProcessHeap(), 0, name );
    return hr;
}

static inline struct user *impl_from_IUser( IUser *iface )
{
    return CONTAINING_RECORD( iface, struct user, IUser_iface );
}

static HRESULT WINAPI user_QueryInterface( IUser *iface, REFIID iid, void **out )
{
    struct user *impl = impl_from_IUser( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (!out) return E_POINTER;
    *out = NULL;

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IUser ))
    {
        *out = &impl->IUser_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    /*
     * IUser2 {98ba5628-a6e3-518e-89d9-d3b2b1991a10} carries CheckUserAgeConsentGroupAsync
     * (windows-rs Windows/System/mod.rs:2411, :2420); no age-consent service exists in a prefix,
     * so the interface is not advertised.
     */
    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG WINAPI user_AddRef( IUser *iface )
{
    struct user *impl = impl_from_IUser( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );

    TRACE( "iface %p, ref %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI user_Release( IUser *iface )
{
    struct user *impl = impl_from_IUser( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    TRACE( "iface %p, ref %lu.\n", iface, ref );

    /* The singleton keeps its own reference; a released client reference never destroys it. */
    if (!ref)
    {
        WindowsDeleteString( impl->nonroamable_id );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI user_GetIids( IUser *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI user_GetRuntimeClassName( IUser *iface, HSTRING *class_name )
{
    if (!class_name) return E_POINTER;
    return WindowsCreateString( RuntimeClass_Windows_System_User, lstrlenW( RuntimeClass_Windows_System_User ), class_name );
}

static HRESULT WINAPI user_GetTrustLevel( IUser *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI user_get_NonRoamableId( IUser *iface, HSTRING *value )
{
    struct user *impl = impl_from_IUser( iface );

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    if (!impl->nonroamable_id) return E_FAIL;

    return WindowsDuplicateString( impl->nonroamable_id, value );
}

static HRESULT WINAPI user_get_AuthenticationStatus( IUser *iface, UserAuthenticationStatus *value )
{
    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;

    /* The prefix session is signed in on the local machine; there is no remote logon redirect. */
    *value = UserAuthenticationStatus_LocallyAuthenticated;
    return S_OK;
}

static HRESULT WINAPI user_get_Type( IUser *iface, UserType *value )
{
    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;

    /* Single local interactive account; Windows.System.UserType LocalUser = 0. */
    *value = UserType_LocalUser;
    return S_OK;
}

static HRESULT WINAPI user_GetPropertyAsync( IUser *iface, HSTRING value,
                                             __FIAsyncOperation_1_IInspectable **operation )
{
    struct async_op *op;

    TRACE( "iface %p, value %s, operation %p.\n", iface, debugstr_hstring( value ), operation );

    if (!operation) return E_POINTER;
    *operation = NULL;

    if (!(op = create_async_op( &IID_IAsyncOperation_IInspectable, L"Windows.Foundation.IAsyncOperation", NULL )))
        return E_OUTOFMEMORY;

    *operation = (void *)async_op_iface( op );
    return S_OK;
}

static HRESULT WINAPI user_GetPropertiesAsync( IUser *iface,
                                               __FIVectorView_1_HSTRING *values,
                                               __FIAsyncOperation_1_Windows__CFoundation__CCollections__CIPropertySet **operation )
{
    struct async_op *op;

    TRACE( "iface %p, values %p, operation %p.\n", iface, values, operation );

    if (!operation) return E_POINTER;
    *operation = NULL;

    if (!(op = create_async_op( &IID_IAsyncOperation_IPropertySet, L"Windows.Foundation.IAsyncOperation", NULL )))
        return E_OUTOFMEMORY;

    *operation = (void *)async_op_iface( op );
    return S_OK;
}

static HRESULT WINAPI user_GetPictureAsync( IUser *iface, UserPictureSize desired_size,
                                            __FIAsyncOperation_1_Windows__CStorage__CStreams__CIRandomAccessStreamReference **operation )
{
    struct async_op *op;

    TRACE( "iface %p, desired_size %u, operation %p.\n", iface, desired_size, operation );

    if (!operation) return E_POINTER;
    *operation = NULL;

    if (!(op = create_async_op( &IID_IAsyncOperation_IRandomAccessStreamReference,
                                L"Windows.Foundation.IAsyncOperation", NULL )))
        return E_OUTOFMEMORY;

    *operation = (void *)async_op_iface( op );
    return S_OK;
}

static const struct IUserVtbl user_vtbl =
{
    user_QueryInterface,
    user_AddRef,
    user_Release,
    user_GetIids,
    user_GetRuntimeClassName,
    user_GetTrustLevel,
    user_get_NonRoamableId,
    user_get_AuthenticationStatus,
    user_get_Type,
    user_GetPropertyAsync,
    user_GetPropertiesAsync,
    user_GetPictureAsync,
};

HRESULT create_user( void **user )
{
    struct user *impl;
    HRESULT hr;

    RtlAcquireSRWLockExclusive( &user_lock );

    if (!user_singleton)
    {
        if (!(impl = calloc( 1, sizeof(*impl) ))) { RtlReleaseSRWLockExclusive( &user_lock ); return E_OUTOFMEMORY; }

        impl->IUser_iface.lpVtbl = &user_vtbl;
        impl->ref = 1;
        if (FAILED(hr = build_nonroamable_id( &impl->nonroamable_id )))
        {
            FIXME( "no user SID available, hr %#lx.\n", hr );
            hr = WindowsCreateString( L"", 0, &impl->nonroamable_id );
            if (FAILED(hr)) { free( impl ); RtlReleaseSRWLockExclusive( &user_lock ); return hr; }
        }
        /* The static reference keeps the singleton alive for the process lifetime. */
        impl->ref++;
        user_singleton = impl;
        TRACE( "created user %p, nonroamable id.\n", impl );
    }

    user_singleton->ref++;
    *user = &user_singleton->IUser_iface;
    RtlReleaseSRWLockExclusive( &user_lock );
    return S_OK;
}

void user_add_ref( IUser *user )
{
    IUser_AddRef( user );
}

void user_release( IUser *user )
{
    IUser_Release( user );
}

/***********************************************************************
 * statics: IUserStatics {155eb23b-242a-45e0-a2e9-3171fc6a7fdd} and
 *          IUserStatics2 {74a37e11-2eb5-4487-b0d5-2c6790e013e9}
 */

struct user_statics
{
    IActivationFactory IActivationFactory_iface;
    IUserStatics IUserStatics_iface;
    IUserStatics2 IUserStatics2_iface;
    LONG ref;
};

static inline struct user_statics *impl_from_IActivationFactory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct user_statics, IActivationFactory_iface );
}

static HRESULT WINAPI factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct user_statics *impl = impl_from_IActivationFactory( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (!out) return E_POINTER;
    *out = NULL;

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = &impl->IActivationFactory_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IUserStatics ))
    {
        *out = &impl->IUserStatics_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IUserStatics2 ))
    {
        *out = &impl->IUserStatics2_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG WINAPI factory_AddRef( IActivationFactory *iface )
{
    struct user_statics *impl = impl_from_IActivationFactory( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );

    TRACE( "iface %p, ref %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI factory_Release( IActivationFactory *iface )
{
    struct user_statics *impl = impl_from_IActivationFactory( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    TRACE( "iface %p, ref %lu.\n", iface, ref );
    return ref;
}

static HRESULT WINAPI factory_GetIids( IActivationFactory *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *class_name )
{
    if (!class_name) return E_POINTER;
    return WindowsCreateString( RuntimeClass_Windows_System_User, lstrlenW( RuntimeClass_Windows_System_User ), class_name );
}

static HRESULT WINAPI factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    TRACE( "iface %p, instance %p.\n", iface, instance );

    if (!instance) return E_POINTER;
    *instance = NULL;

    /* Windows.System.User carries no activatable constructor (include/windows.system.idl,
     * runtimeclass User: statics + IUser only); instances come from the statics. */
    return CLASS_E_CLASSNOTAVAILABLE;
}

static const struct IActivationFactoryVtbl factory_vtbl =
{
    factory_QueryInterface,
    factory_AddRef,
    factory_Release,
    factory_GetIids,
    factory_GetRuntimeClassName,
    factory_GetTrustLevel,
    factory_ActivateInstance,
};

DEFINE_IINSPECTABLE( statics, IUserStatics, struct user_statics, IActivationFactory_iface )
DEFINE_IINSPECTABLE( statics2, IUserStatics2, struct user_statics, IActivationFactory_iface )

static HRESULT WINAPI statics_CreateWatcher( IUserStatics *iface, IInspectable **result )
{
    TRACE( "iface %p, result %p.\n", iface, result );

    if (!result) return E_POINTER;
    *result = NULL;

    /* No user-information store in a prefix to watch; see the header comment. */
    return E_NOTIMPL;
}

static const GUID IID_IIterable_User =
    { 0xd1bacd1f, 0x0376, 0x5823, { 0x8c, 0x29, 0x1d, 0x45, 0xb9, 0xf4, 0xc1, 0x91 } };
static const GUID IID_IIterator_User =
    { 0x26d11457, 0x41cf, 0x5e4a, { 0xa5, 0xeb, 0x57, 0x2a, 0x13, 0x2a, 0x62, 0xd4 } };

struct user_iter_vtbl
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)( void *iface, REFIID iid, void **out );
    ULONG (STDMETHODCALLTYPE *AddRef)( void *iface );
    ULONG (STDMETHODCALLTYPE *Release)( void *iface );
    HRESULT (STDMETHODCALLTYPE *GetIids)( void *iface, ULONG *count, IID **iids );
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)( void *iface, HSTRING *name );
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)( void *iface, TrustLevel *level );
    HRESULT (STDMETHODCALLTYPE *get_Current)( void *iface, IUser **current );
    HRESULT (STDMETHODCALLTYPE *get_HasCurrent)( void *iface, boolean *has_current );
    HRESULT (STDMETHODCALLTYPE *MoveNext)( void *iface, boolean *has_current );
    HRESULT (STDMETHODCALLTYPE *GetMany)( void *iface, UINT32 capacity, IUser **items, UINT32 *actual );
};

struct user_iterable_vtbl
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)( void *iface, REFIID iid, void **out );
    ULONG (STDMETHODCALLTYPE *AddRef)( void *iface );
    ULONG (STDMETHODCALLTYPE *Release)( void *iface );
    HRESULT (STDMETHODCALLTYPE *GetIids)( void *iface, ULONG *count, IID **iids );
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)( void *iface, HSTRING *name );
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)( void *iface, TrustLevel *level );
    HRESULT (STDMETHODCALLTYPE *First)( void *iface, void **first );
};

struct user_find_list
{
    __FIAsyncOperation_1___FIVectorView_1_Windows__CSystem__CUser op;
    __FIVectorView_1_Windows__CSystem__CUser view;
    IAsyncInfo info;
    const struct user_iterable_vtbl *iterable_vtbl;
    LONG ref;
    __FIAsyncOperationCompletedHandler_1___FIVectorView_1_Windows__CSystem__CUser *handler;
    IUser *user;
    UINT32 count;
};

struct user_iterator
{
    const struct user_iter_vtbl *vtbl;
    LONG ref;
    struct user_find_list *list;
    UINT32 pos;
};

static inline struct user_find_list *find_from_op( __FIAsyncOperation_1___FIVectorView_1_Windows__CSystem__CUser *iface )
{
    return CONTAINING_RECORD( iface, struct user_find_list, op );
}

static inline struct user_find_list *find_from_view( __FIVectorView_1_Windows__CSystem__CUser *iface )
{
    return CONTAINING_RECORD( iface, struct user_find_list, view );
}

static inline struct user_find_list *find_from_info( IAsyncInfo *iface )
{
    return CONTAINING_RECORD( iface, struct user_find_list, info );
}

static inline struct user_find_list *find_from_iterable( void *iface )
{
    return CONTAINING_RECORD( iface, struct user_find_list, iterable_vtbl );
}

static HRESULT find_list_query( struct user_find_list *This, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID___FIAsyncOperation_1___FIVectorView_1_Windows__CSystem__CUser ))
    {
        *out = &This->op;
    }
    else if (IsEqualGUID( iid, &IID___FIVectorView_1_Windows__CSystem__CUser ))
    {
        *out = &This->view;
    }
    else if (IsEqualGUID( iid, &IID_IIterable_User ))
    {
        *out = &This->iterable_vtbl;
    }
    else if (IsEqualGUID( iid, &IID_IAsyncInfo ))
    {
        *out = &This->info;
    }
    else
    {
        FIXME( "iid %s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
        *out = NULL;
        return E_NOINTERFACE;
    }
    InterlockedIncrement( &This->ref );
    return S_OK;
}

static void find_list_release( struct user_find_list *This )
{
    if (This->handler) IUnknown_Release( (IUnknown *)This->handler );
    if (This->user) IUser_Release( This->user );
    free( This );
}

#define USER_FIND_FWD(name, type, from) \
static HRESULT WINAPI name##_QueryInterface( type *iface, REFIID iid, void **out ) { return find_list_query( from( iface ), iid, out ); } \
static ULONG WINAPI name##_AddRef( type *iface ) { return InterlockedIncrement( &from( iface )->ref ); } \
static ULONG WINAPI name##_Release( type *iface ) { struct user_find_list *This = from( iface ); ULONG ref = InterlockedDecrement( &This->ref ); \
    if (!ref) find_list_release( This ); return ref; } \
static HRESULT WINAPI name##_GetIids( type *iface, ULONG *count, IID **iids ) { if (count) *count = 0; if (iids) *iids = NULL; return S_OK; } \
static HRESULT WINAPI name##_GetRuntimeClassName( type *iface, HSTRING *name ) { return WindowsCreateString( L"Windows.Foundation.IAsyncOperation", 34, name ); } \
static HRESULT WINAPI name##_GetTrustLevel( type *iface, TrustLevel *level ) { if (level) *level = BaseTrust; return S_OK; }

USER_FIND_FWD( find_op, __FIAsyncOperation_1___FIVectorView_1_Windows__CSystem__CUser, find_from_op )
USER_FIND_FWD( find_view, __FIVectorView_1_Windows__CSystem__CUser, find_from_view )
USER_FIND_FWD( find_info, IAsyncInfo, find_from_info )

static HRESULT WINAPI find_op_put_Completed( __FIAsyncOperation_1___FIVectorView_1_Windows__CSystem__CUser *iface,
                                             __FIAsyncOperationCompletedHandler_1___FIVectorView_1_Windows__CSystem__CUser *handler )
{
    struct user_find_list *This = find_from_op( iface );

    if (This->handler) IUnknown_Release( (IUnknown *)This->handler );
    This->handler = handler;
    if (handler)
    {
        IUnknown_AddRef( (IUnknown *)handler );
        handler->lpVtbl->Invoke( handler, iface, (AsyncStatus)1 /* Completed */ );
    }
    return S_OK;
}

static HRESULT WINAPI find_op_get_Completed( __FIAsyncOperation_1___FIVectorView_1_Windows__CSystem__CUser *iface,
                                             __FIAsyncOperationCompletedHandler_1___FIVectorView_1_Windows__CSystem__CUser **handler )
{
    struct user_find_list *This = find_from_op( iface );

    *handler = This->handler;
    if (*handler) IUnknown_AddRef( (IUnknown *)*handler );
    return S_OK;
}

static HRESULT WINAPI find_op_GetResults( __FIAsyncOperation_1___FIVectorView_1_Windows__CSystem__CUser *iface,
                                          __FIVectorView_1_Windows__CSystem__CUser **results )
{
    struct user_find_list *This = find_from_op( iface );

    TRACE( "iface %p, results %p.\n", iface, results );
    *results = &This->view;
    InterlockedIncrement( &This->ref );
    return S_OK;
}

static const struct __FIAsyncOperation_1___FIVectorView_1_Windows__CSystem__CUserVtbl find_op_vtbl =
{
    find_op_QueryInterface,
    find_op_AddRef,
    find_op_Release,
    find_op_GetIids,
    find_op_GetRuntimeClassName,
    find_op_GetTrustLevel,
    find_op_put_Completed,
    find_op_get_Completed,
    find_op_GetResults,
};

static HRESULT WINAPI find_view_GetAt( __FIVectorView_1_Windows__CSystem__CUser *iface, UINT32 index, IUser **item )
{
    struct user_find_list *This = find_from_view( iface );

    if (index >= This->count || !This->user) return E_BOUNDS;
    *item = This->user;
    IUser_AddRef( *item );
    return S_OK;
}

static HRESULT WINAPI find_view_get_Size( __FIVectorView_1_Windows__CSystem__CUser *iface, UINT32 *size )
{
    struct user_find_list *This = find_from_view( iface );

    TRACE( "iface %p, size %u.\n", iface, This->count );
    *size = This->count;
    return S_OK;
}

static HRESULT WINAPI find_view_IndexOf( __FIVectorView_1_Windows__CSystem__CUser *iface, IUser *item, UINT32 *index, boolean *found )
{
    struct user_find_list *This = find_from_view( iface );

    if (This->count && item == This->user)
    {
        *index = 0;
        *found = TRUE;
    }
    else
    {
        *index = 0;
        *found = FALSE;
    }
    return S_OK;
}

static HRESULT WINAPI find_view_GetMany( __FIVectorView_1_Windows__CSystem__CUser *iface, UINT32 start_index,
                                         UINT32 items_size, IUser **items, UINT32 *items_read )
{
    struct user_find_list *This = find_from_view( iface );

    if (!items_read) return E_POINTER;
    if (start_index < This->count && items_size > 0 && items && This->user)
    {
        items[0] = This->user;
        IUser_AddRef( items[0] );
        *items_read = 1;
    }
    else *items_read = 0;
    return S_OK;
}

static const struct __FIVectorView_1_Windows__CSystem__CUserVtbl find_view_vtbl =
{
    find_view_QueryInterface,
    find_view_AddRef,
    find_view_Release,
    find_view_GetIids,
    find_view_GetRuntimeClassName,
    find_view_GetTrustLevel,
    find_view_GetAt,
    find_view_get_Size,
    find_view_IndexOf,
    find_view_GetMany,
};

static HRESULT WINAPI find_info_get_Id( IAsyncInfo *iface, UINT32 *id ) { *id = 1; return S_OK; }
static HRESULT WINAPI find_info_get_Status( IAsyncInfo *iface, AsyncStatus *status ) { *status = (AsyncStatus)1 /* Completed */; return S_OK; }
static HRESULT WINAPI find_info_get_ErrorCode( IAsyncInfo *iface, HRESULT *code ) { *code = S_OK; return S_OK; }
static HRESULT WINAPI find_info_Cancel( IAsyncInfo *iface ) { return S_OK; }
static HRESULT WINAPI find_info_Close( IAsyncInfo *iface ) { return S_OK; }

static const struct IAsyncInfoVtbl find_info_vtbl =
{
    find_info_QueryInterface,
    find_info_AddRef,
    find_info_Release,
    find_info_GetIids,
    find_info_GetRuntimeClassName,
    find_info_GetTrustLevel,
    find_info_get_Id,
    find_info_get_Status,
    find_info_get_ErrorCode,
    find_info_Cancel,
    find_info_Close,
};

static HRESULT WINAPI find_iter_QueryInterface( void *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IIterator_User ))
    {
        *out = iface;
        InterlockedIncrement( &((struct user_iterator *)iface)->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI find_iter_AddRef( void *iface )
{
    return InterlockedIncrement( &((struct user_iterator *)iface)->ref );
}

static ULONG WINAPI find_iter_Release( void *iface )
{
    struct user_iterator *impl = iface;
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        find_view_Release( &impl->list->view );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI find_iter_GetIids( void *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI find_iter_GetRuntimeClassName( void *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.Foundation.Collections.IIterator`1<Windows.System.User>", 63, name );
}

static HRESULT WINAPI find_iter_GetTrustLevel( void *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI find_iter_get_Current( void *iface, IUser **current )
{
    struct user_iterator *impl = iface;
    if (!current) return E_POINTER;
    if (impl->pos >= impl->list->count || !impl->list->user) return E_BOUNDS;
    *current = impl->list->user;
    IUser_AddRef( *current );
    return S_OK;
}

static HRESULT WINAPI find_iter_get_HasCurrent( void *iface, boolean *has_current )
{
    struct user_iterator *impl = iface;
    if (!has_current) return E_POINTER;
    *has_current = impl->pos < impl->list->count;
    return S_OK;
}

static HRESULT WINAPI find_iter_MoveNext( void *iface, boolean *has_current )
{
    struct user_iterator *impl = iface;
    if (!has_current) return E_POINTER;
    if (impl->pos < impl->list->count) impl->pos++;
    *has_current = impl->pos < impl->list->count;
    return S_OK;
}

static HRESULT WINAPI find_iter_GetMany( void *iface, UINT32 capacity, IUser **items, UINT32 *actual )
{
    struct user_iterator *impl = iface;
    if (!actual) return E_POINTER;
    if (impl->pos < impl->list->count && capacity > 0 && items && impl->list->user)
    {
        items[0] = impl->list->user;
        IUser_AddRef( items[0] );
        impl->pos++;
        *actual = 1;
    }
    else *actual = 0;
    return S_OK;
}

static const struct user_iter_vtbl find_iter_vtbl =
{
    find_iter_QueryInterface,
    find_iter_AddRef,
    find_iter_Release,
    find_iter_GetIids,
    find_iter_GetRuntimeClassName,
    find_iter_GetTrustLevel,
    find_iter_get_Current,
    find_iter_get_HasCurrent,
    find_iter_MoveNext,
    find_iter_GetMany,
};

USER_FIND_FWD( find_iterable, void, find_from_iterable )

static HRESULT WINAPI find_iterable_First( void *iface, void **first )
{
    struct user_find_list *This = find_from_iterable( iface );
    struct user_iterator *iter;

    if (!first) return E_POINTER;
    if (!(iter = calloc( 1, sizeof(*iter) ))) return E_OUTOFMEMORY;
    iter->vtbl = &find_iter_vtbl;
    iter->ref = 1;
    iter->list = This;
    InterlockedIncrement( &This->ref );
    *first = &iter->vtbl;
    return S_OK;
}

static const struct user_iterable_vtbl find_iterable_vtbl =
{
    find_iterable_QueryInterface,
    find_iterable_AddRef,
    find_iterable_Release,
    find_iterable_GetIids,
    find_iterable_GetRuntimeClassName,
    find_iterable_GetTrustLevel,
    find_iterable_First,
};

static HRESULT create_find_all_op( __FIAsyncOperation_1___FIVectorView_1_Windows__CSystem__CUser **operation )
{
    struct user_find_list *impl;
    HRESULT hr;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->op.lpVtbl = &find_op_vtbl;
    impl->view.lpVtbl = &find_view_vtbl;
    impl->info.lpVtbl = &find_info_vtbl;
    impl->iterable_vtbl = &find_iterable_vtbl;
    impl->ref = 1;
    if (SUCCEEDED((hr = create_user( (void **)&impl->user )))) impl->count = 1;
    *operation = &impl->op;
    return S_OK;
}

static HRESULT WINAPI statics_FindAllAsync( IUserStatics *iface,
                                            __FIAsyncOperation_1___FIVectorView_1_Windows__CSystem__CUser **operation )
{
    TRACE( "iface %p, operation %p.\n", iface, operation );

    if (!operation) return E_POINTER;
    return create_find_all_op( operation );
}

static HRESULT WINAPI statics_FindAllAsyncByType( IUserStatics *iface, UserType value,
                                                  __FIAsyncOperation_1___FIVectorView_1_Windows__CSystem__CUser **operation )
{
    TRACE( "iface %p, value %u, operation %p.\n", iface, value, operation );

    if (!operation) return E_POINTER;
    return create_find_all_op( operation );
}

static HRESULT WINAPI statics_FindAllAsyncByTypeAndStatus( IUserStatics *iface, UserType type,
                                                           UserAuthenticationStatus status,
                                                           __FIAsyncOperation_1___FIVectorView_1_Windows__CSystem__CUser **operation )
{
    TRACE( "iface %p, type %u, status %u, operation %p.\n", iface, type, status, operation );

    if (!operation) return E_POINTER;
    return create_find_all_op( operation );
}

static HRESULT WINAPI statics_GetFromId( IUserStatics *iface, HSTRING nonroamableid, IUser **result )
{
    TRACE( "iface %s, result %p.\n", debugstr_hstring( nonroamableid ), result );

    if (!result) return E_POINTER;
    *result = NULL;

    /* A prefix has one interactive identity, so the id resolves to the process user. */
    return create_user( (void **)result );
}

static const struct IUserStaticsVtbl statics_vtbl =
{
    statics_QueryInterface,
    statics_AddRef,
    statics_Release,
    statics_GetIids,
    statics_GetRuntimeClassName,
    statics_GetTrustLevel,
    statics_CreateWatcher,
    statics_FindAllAsync,
    statics_FindAllAsyncByType,
    statics_FindAllAsyncByTypeAndStatus,
    statics_GetFromId,
};

static HRESULT WINAPI statics2_GetDefault( IUserStatics2 *iface, IUser **result )
{
    TRACE( "iface %p, result %p.\n", iface, result );

    if (!result) return E_POINTER;
    *result = NULL;

    /* "Gets an object representing the default user account": the process user. */
    return create_user( (void **)result );
}

static const struct IUserStatics2Vtbl statics2_vtbl =
{
    statics2_QueryInterface,
    statics2_AddRef,
    statics2_Release,
    statics2_GetIids,
    statics2_GetRuntimeClassName,
    statics2_GetTrustLevel,
    statics2_GetDefault,
};

static struct user_statics user_statics =
{
    { &factory_vtbl },
    { &statics_vtbl },
    { &statics2_vtbl },
    1,
};

IActivationFactory *user_factory = &user_statics.IActivationFactory_iface;
