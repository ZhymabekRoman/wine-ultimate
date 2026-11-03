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

static HRESULT WINAPI statics_FindAllAsync( IUserStatics *iface,
                                            __FIAsyncOperation_1___FIVectorView_1_Windows__CSystem__CUser **operation )
{
    TRACE( "iface %p, operation %p.\n", iface, operation );

    if (!operation) return E_POINTER;
    *operation = NULL;

    /* No user enumeration store in a prefix; see the header comment. */
    return E_NOTIMPL;
}

static HRESULT WINAPI statics_FindAllAsyncByType( IUserStatics *iface, UserType value,
                                                  __FIAsyncOperation_1___FIVectorView_1_Windows__CSystem__CUser **operation )
{
    TRACE( "iface %p, value %u, operation %p.\n", iface, value, operation );

    if (!operation) return E_POINTER;
    *operation = NULL;

    return E_NOTIMPL;
}

static HRESULT WINAPI statics_FindAllAsyncByTypeAndStatus( IUserStatics *iface, UserType type,
                                                           UserAuthenticationStatus status,
                                                           __FIAsyncOperation_1___FIVectorView_1_Windows__CSystem__CUser **operation )
{
    TRACE( "iface %p, type %u, status %u, operation %p.\n", iface, type, status, operation );

    if (!operation) return E_POINTER;
    *operation = NULL;

    return E_NOTIMPL;
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
