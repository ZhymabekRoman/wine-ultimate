#ifndef __WINE_PUSHNOTIFICATIONS_PRIVATE_H
#define __WINE_PUSHNOTIFICATIONS_PRIVATE_H

#include <stdarg.h>
#include <stddef.h>

#define COBJMACROS
#include "windef.h"
#include "winbase.h"
#include "winstring.h"

#include "activation.h"
#include "asyncinfo.h"
#include "eventtoken.h"
#include "roapi.h"

#include "wine/debug.h"

#define WIDL_using_Windows_Foundation
#define WIDL_using_Windows_Foundation_Collections
#define WIDL_using_Windows_Storage_Streams
#define WIDL_using_Windows_System
#define WIDL_using_Windows_UI_Notifications
#define WIDL_using_Windows_Networking_PushNotifications
#include "windows.foundation.h"
#include "windows.foundation.collections.h"
#include "windows.storage.streams.h"
#include "windows.system.h"
#include "windows.ui.notifications.h"
#include "windows.networking.pushnotifications.h"

extern IActivationFactory *pushnotificationchannel_manager_factory;

/*
 * Born-completed IAsyncOperation`1<T> / IAsyncAction implementation (async.c);
 * one vtable pair serves every closed instantiation, the parameterized IID and
 * the runtime class name being per-object fields.
 */
struct async_op;

extern const void *async_op_vtbl[];
extern const void *async_info_vtbl[];
extern const void *async_action_vtbl[];

struct async_op *create_async_op( const IID *iid, const WCHAR *class_name, IInspectable *result );
struct async_op *create_async_action( IInspectable *result );
void *async_op_iface( struct async_op *impl );
void *async_action_iface( struct async_op *impl );

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
    DEFINE_IINSPECTABLE_( pfx, iface_type, impl_type, impl_from_##iface_type, iface_type##_iface, &impl->base_iface )

IInspectable *create_push_notification_channel( void );
IInspectable *create_push_notification_channel_manager_for_user( void );

#endif /* __WINE_PUSHNOTIFICATIONS_PRIVATE_H */
