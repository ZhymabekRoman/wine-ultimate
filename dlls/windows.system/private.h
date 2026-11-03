#ifndef __WINE_WINDOWS_SYSTEM_PRIVATE_H
#define __WINE_WINDOWS_SYSTEM_PRIVATE_H

#include <stdarg.h>
#include <stddef.h>

#define COBJMACROS
#include "windef.h"
#include "winbase.h"
#include "winstring.h"
#include "sddl.h"
#include "winternl.h"

#include "activation.h"
#include "asyncinfo.h"
#include "eventtoken.h"
#include "roapi.h"

#include "wine/debug.h"

#define WIDL_using_Windows_Foundation
#define WIDL_using_Windows_Foundation_Collections
#define WIDL_using_Windows_Storage_Streams
#define WIDL_using_Windows_System
#include "windows.foundation.h"
#include "windows.foundation.collections.h"
#include "windows.storage.streams.h"
#include "windows.system.h"

extern IActivationFactory *user_factory;

/*
 * WinRT asynchronous operations that are born completed; same shape as
 * dlls/windows.networking.pushnotifications/async.c.  The User properties are a remote
 * (per-user) lookup that a prefix has no provider for, so the operations the User object hands
 * out complete at creation time: IAsyncInfo::Status reports Completed, put_Completed invokes the
 * handler synchronously and GetResults returns the creation-time result (here: none, the documented
 * answer for a property the prefix does not have).
 */
struct async_op;

extern const void *async_op_vtbl[];
extern const void *async_info_vtbl[];

struct async_op *create_async_op( const IID *iid, const WCHAR *class_name, IInspectable *result );
void *async_op_iface( struct async_op *impl );
void async_op_release( struct async_op *impl );

/*
 * IInspectable forwarder shared by the activation factory and the interfaces it hands out; same
 * shape as dlls/windows.system.profile/private.h.
 */
#define DEFINE_IINSPECTABLE_( pfx, iface_type, impl_type, impl_from, iface_mem, expr )             \
    static inline impl_type *impl_from( iface_type *iface )                                        \
    {                                                                                              \
        return CONTAINING_RECORD( iface, impl_type, iface_mem );                                   \
    }                                                                                              \
    static HRESULT WINAPI pfx##_QueryInterface( iface_type *iface, REFIID iid, void **out )        \
    {                                                                                              \
        impl_type *impl = impl_from( iface );                                                      \
        return IInspectable_QueryInterface( (IInspectable *)(expr), iid, out );                    \
    }                                                                                              \
    static ULONG WINAPI pfx##_AddRef( iface_type *iface )                                          \
    {                                                                                              \
        impl_type *impl = impl_from( iface );                                                      \
        return IInspectable_AddRef( (IInspectable *)(expr) );                                      \
    }                                                                                              \
    static ULONG WINAPI pfx##_Release( iface_type *iface )                                         \
    {                                                                                              \
        impl_type *impl = impl_from( iface );                                                      \
        return IInspectable_Release( (IInspectable *)(expr) );                                     \
    }                                                                                              \
    static HRESULT WINAPI pfx##_GetIids( iface_type *iface, ULONG *iid_count, IID **iids )         \
    {                                                                                              \
        impl_type *impl = impl_from( iface );                                                      \
        return IInspectable_GetIids( (IInspectable *)(expr), iid_count, iids );                    \
    }                                                                                              \
    static HRESULT WINAPI pfx##_GetRuntimeClassName( iface_type *iface, HSTRING *class_name )      \
    {                                                                                              \
        impl_type *impl = impl_from( iface );                                                      \
        return IInspectable_GetRuntimeClassName( (IInspectable *)(expr), class_name );             \
    }                                                                                              \
    static HRESULT WINAPI pfx##_GetTrustLevel( iface_type *iface, TrustLevel *trust_level )        \
    {                                                                                              \
        impl_type *impl = impl_from( iface );                                                      \
        return IInspectable_GetTrustLevel( (IInspectable *)(expr), trust_level );                  \
    }
#define DEFINE_IINSPECTABLE( pfx, iface_type, impl_type, base_iface )                              \
    DEFINE_IINSPECTABLE_( pfx, iface_type, impl_type, impl_from_##iface_type, iface_type##_iface,  \
                          &impl->base_iface )

HRESULT create_user( void **user );
void user_add_ref( IUser *user );
void user_release( IUser *user );

#endif /* __WINE_WINDOWS_SYSTEM_PRIVATE_H */
