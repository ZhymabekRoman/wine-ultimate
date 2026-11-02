/* WinRT Windows.UI.Notifications.ToastNotificationManager implementation
 *
 * The tray/toast path of packaged apps activates
 * Windows.UI.Notifications.ToastNotificationManager (on Windows the class is
 * served by twinapi.appcore.dll).  Wine answers it with a functional no-op
 * toast surface: the statics hand out ToastNotifier instances whose
 * Show/Hide/schedule entry points succeed; template content is not produced
 * (E_NOTIMPL), the honest stub marker for callers that need the XML.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include "private.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(twinapi);

/* IInspectable forwarder for the secondary interfaces of an object: identity,
 * refcount and the IInspectable entry points live on the object's canonical
 * interface, as the WinRT projection requires. */
#define FORWARD_IINSPECTABLE( pfx, iface_type, impl_type, impl_from, base_expr )                     \
    static HRESULT WINAPI pfx##_QueryInterface( iface_type *iface, REFIID iid, void **out )          \
    {                                                                                                \
        impl_type *impl = impl_from( iface );                                                        \
        return IInspectable_QueryInterface( (IInspectable *)(base_expr), iid, out );                 \
    }                                                                                                \
    static ULONG WINAPI pfx##_AddRef( iface_type *iface )                                            \
    {                                                                                                \
        impl_type *impl = impl_from( iface );                                                        \
        return IInspectable_AddRef( (IInspectable *)(base_expr) );                                   \
    }                                                                                                \
    static ULONG WINAPI pfx##_Release( iface_type *iface )                                           \
    {                                                                                                \
        impl_type *impl = impl_from( iface );                                                        \
        return IInspectable_Release( (IInspectable *)(base_expr) );                                  \
    }                                                                                                \
    static HRESULT WINAPI pfx##_GetIids( iface_type *iface, ULONG *iid_count, IID **iids )           \
    {                                                                                                \
        impl_type *impl = impl_from( iface );                                                        \
        return IInspectable_GetIids( (IInspectable *)(base_expr), iid_count, iids );                  \
    }                                                                                                \
    static HRESULT WINAPI pfx##_GetRuntimeClassName( iface_type *iface, HSTRING *class_name )        \
    {                                                                                                \
        impl_type *impl = impl_from( iface );                                                        \
        return IInspectable_GetRuntimeClassName( (IInspectable *)(base_expr), class_name );           \
    }                                                                                                \
    static HRESULT WINAPI pfx##_GetTrustLevel( iface_type *iface, TrustLevel *trust_level )          \
    {                                                                                                \
        impl_type *impl = impl_from( iface );                                                        \
        return IInspectable_GetTrustLevel( (IInspectable *)(base_expr), trust_level );                \
    }

struct toast_notifier
{
    IToastNotifier IToastNotifier_iface;
    LONG ref;
};

static inline struct toast_notifier *impl_from_IToastNotifier( IToastNotifier *iface )
{
    return CONTAINING_RECORD( iface, struct toast_notifier, IToastNotifier_iface );
}

static HRESULT WINAPI toast_notifier_QueryInterface( IToastNotifier *iface, REFIID iid, void **out )
{
    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown )
            || IsEqualGUID( iid, &IID_IInspectable )
            || IsEqualGUID( iid, &IID_IAgileObject )
            || IsEqualGUID( iid, &IID_IToastNotifier ))
    {
        IUnknown_AddRef( (IUnknown *)iface );
        *out = iface;
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI toast_notifier_AddRef( IToastNotifier *iface )
{
    struct toast_notifier *impl = impl_from_IToastNotifier( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI toast_notifier_Release( IToastNotifier *iface )
{
    struct toast_notifier *impl = impl_from_IToastNotifier( iface );
    LONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        TRACE( "destroying %p\n", iface );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI toast_notifier_GetIids( IToastNotifier *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI toast_notifier_GetRuntimeClassName( IToastNotifier *iface, HSTRING *class_name )
{
    return WindowsCreateString( RuntimeClass_Windows_UI_Notifications_ToastNotifier,
                                ARRAY_SIZE( RuntimeClass_Windows_UI_Notifications_ToastNotifier ) - 1, class_name );
}

static HRESULT WINAPI toast_notifier_GetTrustLevel( IToastNotifier *iface, TrustLevel *trust_level )
{
    if (trust_level) *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI toast_notifier_Show( IToastNotifier *iface,
        __x_ABI_CWindows_CUI_CNotifications_CIToastNotification *notification )
{
    FIXME( "iface %p, notification %p stub!\n", iface, notification );
    return S_OK;
}

static HRESULT WINAPI toast_notifier_Hide( IToastNotifier *iface,
        __x_ABI_CWindows_CUI_CNotifications_CIToastNotification *notification )
{
    FIXME( "iface %p, notification %p stub!\n", iface, notification );
    return S_OK;
}

static HRESULT WINAPI toast_notifier_get_Setting( IToastNotifier *iface,
        __x_ABI_CWindows_CUI_CNotifications_CNotificationSetting *value )
{
    TRACE( "iface %p, value %p.\n", iface, value );
    if (!value) return E_POINTER;
    *value = NotificationSetting_Enabled;
    return S_OK;
}

static HRESULT WINAPI toast_notifier_AddToSchedule( IToastNotifier *iface,
        __x_ABI_CWindows_CUI_CNotifications_CIScheduledToastNotification *scheduled_toast )
{
    FIXME( "iface %p, scheduled_toast %p stub!\n", iface, scheduled_toast );
    return S_OK;
}

static HRESULT WINAPI toast_notifier_RemoveFromSchedule( IToastNotifier *iface,
        __x_ABI_CWindows_CUI_CNotifications_CIScheduledToastNotification *scheduled_toast )
{
    FIXME( "iface %p, scheduled_toast %p stub!\n", iface, scheduled_toast );
    return S_OK;
}

/*
 * The app's tray path enumerates scheduled notifications (CancelScheduled-
 * Notifications) before touching the tray; the C# projection throws on
 * E_NOTIMPL and the unhandled exception fail-fasts the process, so the empty
 * scheduled set is answered as a real (empty) IVectorView.
 *
 * The C# projection of IReadOnlyList<T> enumerates the same object through
 * IEnumerable<T>: IReadOnlyListImpl`1.Make_IEnumerableObjRef() QIs the vector
 * view for the closed pinterface instantiation
 * IIterable<ScheduledToastNotification> ({7a7b2a51-c182-5846-a861-4f9c036f24ad},
 * the parameterized instance of pinterface({4947e93c-...};...)), and an
 * E_NOINTERFACE there becomes an InvalidCastException in the caller.  The
 * IIterable is answered on the same object (shared identity) and its First()
 * hands out a real (exhausted) IIterator.
 */
struct scheduled_toast_vector
{
    __FIVectorView_1_Windows__CUI__CNotifications__CScheduledToastNotification
        __FIVectorView_1_Windows__CUI__CNotifications__CScheduledToastNotification_iface;
    __FIIterable_1_Windows__CUI__CNotifications__CScheduledToastNotification
        __FIIterable_1_Windows__CUI__CNotifications__CScheduledToastNotification_iface;
    LONG ref;
};

static inline struct scheduled_toast_vector *impl_from_FIVectorView_ScheduledToastNotification(
        __FIVectorView_1_Windows__CUI__CNotifications__CScheduledToastNotification *iface )
{
    return CONTAINING_RECORD( iface, struct scheduled_toast_vector,
            __FIVectorView_1_Windows__CUI__CNotifications__CScheduledToastNotification_iface );
}

static HRESULT WINAPI sched_vector_QueryInterface(
        __FIVectorView_1_Windows__CUI__CNotifications__CScheduledToastNotification *iface, REFIID iid, void **out )
{
    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown )
            || IsEqualGUID( iid, &IID_IInspectable )
            || IsEqualGUID( iid, &IID_IAgileObject )
            || IsEqualGUID( iid, &IID___FIVectorView_1_Windows__CUI__CNotifications__CScheduledToastNotification ))
    {
        IUnknown_AddRef( (IUnknown *)iface );
        *out = iface;
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID___FIIterable_1_Windows__CUI__CNotifications__CScheduledToastNotification ))
    {
        struct scheduled_toast_vector *impl = impl_from_FIVectorView_ScheduledToastNotification( iface );

        IUnknown_AddRef( (IUnknown *)iface );
        *out = &impl->__FIIterable_1_Windows__CUI__CNotifications__CScheduledToastNotification_iface;
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI sched_vector_AddRef(
        __FIVectorView_1_Windows__CUI__CNotifications__CScheduledToastNotification *iface )
{
    struct scheduled_toast_vector *impl = impl_from_FIVectorView_ScheduledToastNotification( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI sched_vector_Release(
        __FIVectorView_1_Windows__CUI__CNotifications__CScheduledToastNotification *iface )
{
    struct scheduled_toast_vector *impl = impl_from_FIVectorView_ScheduledToastNotification( iface );
    LONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        TRACE( "destroying %p\n", iface );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI sched_vector_GetIids(
        __FIVectorView_1_Windows__CUI__CNotifications__CScheduledToastNotification *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI sched_vector_GetRuntimeClassName(
        __FIVectorView_1_Windows__CUI__CNotifications__CScheduledToastNotification *iface, HSTRING *class_name )
{
    static const WCHAR name[] = L"Windows.Foundation.Collections.IVectorView`1<Windows.UI.Notifications.ScheduledToastNotification>";
    return WindowsCreateString( name, ARRAY_SIZE( name ) - 1, class_name );
}

static HRESULT WINAPI sched_vector_GetTrustLevel(
        __FIVectorView_1_Windows__CUI__CNotifications__CScheduledToastNotification *iface, TrustLevel *trust_level )
{
    if (trust_level) *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI sched_vector_GetAt(
        __FIVectorView_1_Windows__CUI__CNotifications__CScheduledToastNotification *iface, UINT32 index,
        __x_ABI_CWindows_CUI_CNotifications_CIScheduledToastNotification **value )
{
    return E_BOUNDS;
}

static HRESULT WINAPI sched_vector_get_Size(
        __FIVectorView_1_Windows__CUI__CNotifications__CScheduledToastNotification *iface, UINT32 *value )
{
    if (!value) return E_POINTER;
    *value = 0;
    return S_OK;
}

static HRESULT WINAPI sched_vector_IndexOf(
        __FIVectorView_1_Windows__CUI__CNotifications__CScheduledToastNotification *iface,
        __x_ABI_CWindows_CUI_CNotifications_CIScheduledToastNotification *element, UINT32 *index, BOOLEAN *value )
{
    if (!index || !value) return E_POINTER;
    *index = 0;
    *value = FALSE;
    return S_OK;
}

static HRESULT WINAPI sched_vector_GetMany(
        __FIVectorView_1_Windows__CUI__CNotifications__CScheduledToastNotification *iface, UINT32 start_index,
        UINT32 items_size, __x_ABI_CWindows_CUI_CNotifications_CIScheduledToastNotification **items, UINT32 *value )
{
    if (!value) return E_POINTER;
    *value = 0;
    return S_OK;
}

static const struct __FIVectorView_1_Windows__CUI__CNotifications__CScheduledToastNotificationVtbl sched_vector_vtbl =
{
    sched_vector_QueryInterface,
    sched_vector_AddRef,
    sched_vector_Release,
    /* IInspectable methods */
    sched_vector_GetIids,
    sched_vector_GetRuntimeClassName,
    sched_vector_GetTrustLevel,
    /* IVectorView<ScheduledToastNotification> methods */
    sched_vector_GetAt,
    sched_vector_get_Size,
    sched_vector_IndexOf,
    sched_vector_GetMany,
};

/*
 * IIterator<ScheduledToastNotification> over the (empty) scheduled set.  The
 * set never has contents, so the iterator is created already exhausted:
 * HasCurrent is FALSE, MoveNext is FALSE, GetMany returns zero items and
 * get_Current is E_BOUNDS, exactly what an empty enumeration reports.
 */
struct scheduled_toast_iterator
{
    __FIIterator_1_Windows__CUI__CNotifications__CScheduledToastNotification
        __FIIterator_1_Windows__CUI__CNotifications__CScheduledToastNotification_iface;
    LONG ref;
};

static inline struct scheduled_toast_iterator *impl_from_FIIterator_ScheduledToastNotification(
        __FIIterator_1_Windows__CUI__CNotifications__CScheduledToastNotification *iface )
{
    return CONTAINING_RECORD( iface, struct scheduled_toast_iterator,
            __FIIterator_1_Windows__CUI__CNotifications__CScheduledToastNotification_iface );
}

static HRESULT WINAPI sched_iterator_QueryInterface(
        __FIIterator_1_Windows__CUI__CNotifications__CScheduledToastNotification *iface, REFIID iid, void **out )
{
    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown )
            || IsEqualGUID( iid, &IID_IInspectable )
            || IsEqualGUID( iid, &IID_IAgileObject )
            || IsEqualGUID( iid, &IID___FIIterator_1_Windows__CUI__CNotifications__CScheduledToastNotification ))
    {
        IUnknown_AddRef( (IUnknown *)iface );
        *out = iface;
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI sched_iterator_AddRef(
        __FIIterator_1_Windows__CUI__CNotifications__CScheduledToastNotification *iface )
{
    struct scheduled_toast_iterator *impl = impl_from_FIIterator_ScheduledToastNotification( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI sched_iterator_Release(
        __FIIterator_1_Windows__CUI__CNotifications__CScheduledToastNotification *iface )
{
    struct scheduled_toast_iterator *impl = impl_from_FIIterator_ScheduledToastNotification( iface );
    LONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        TRACE( "destroying %p\n", iface );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI sched_iterator_GetIids(
        __FIIterator_1_Windows__CUI__CNotifications__CScheduledToastNotification *iface, ULONG *iid_count, IID **iids )
{
    if (!iid_count || !iids) return E_POINTER;
    *iid_count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI sched_iterator_GetRuntimeClassName(
        __FIIterator_1_Windows__CUI__CNotifications__CScheduledToastNotification *iface, HSTRING *class_name )
{
    static const WCHAR name[] = L"Windows.Foundation.Collections.IIterator`1<Windows.UI.Notifications.ScheduledToastNotification>";
    return WindowsCreateString( name, ARRAY_SIZE( name ) - 1, class_name );
}

static HRESULT WINAPI sched_iterator_GetTrustLevel(
        __FIIterator_1_Windows__CUI__CNotifications__CScheduledToastNotification *iface, TrustLevel *trust_level )
{
    if (trust_level) *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI sched_iterator_get_Current(
        __FIIterator_1_Windows__CUI__CNotifications__CScheduledToastNotification *iface,
        __x_ABI_CWindows_CUI_CNotifications_CIScheduledToastNotification **value )
{
    if (!value) return E_POINTER;
    *value = NULL;
    return E_BOUNDS;
}

static HRESULT WINAPI sched_iterator_get_HasCurrent(
        __FIIterator_1_Windows__CUI__CNotifications__CScheduledToastNotification *iface, BOOLEAN *value )
{
    if (!value) return E_POINTER;
    *value = FALSE;
    return S_OK;
}

static HRESULT WINAPI sched_iterator_MoveNext(
        __FIIterator_1_Windows__CUI__CNotifications__CScheduledToastNotification *iface, BOOLEAN *value )
{
    if (!value) return E_POINTER;
    *value = FALSE;
    return S_OK;
}

static HRESULT WINAPI sched_iterator_GetMany(
        __FIIterator_1_Windows__CUI__CNotifications__CScheduledToastNotification *iface, UINT32 items_size,
        __x_ABI_CWindows_CUI_CNotifications_CIScheduledToastNotification **items, UINT32 *value )
{
    if (!value) return E_POINTER;
    if (items_size && !items) return E_POINTER;
    *value = 0;
    return S_OK;
}

static const struct __FIIterator_1_Windows__CUI__CNotifications__CScheduledToastNotificationVtbl sched_iterator_vtbl =
{
    sched_iterator_QueryInterface,
    sched_iterator_AddRef,
    sched_iterator_Release,
    /* IInspectable methods */
    sched_iterator_GetIids,
    sched_iterator_GetRuntimeClassName,
    sched_iterator_GetTrustLevel,
    /* IIterator<ScheduledToastNotification> methods */
    sched_iterator_get_Current,
    sched_iterator_get_HasCurrent,
    sched_iterator_MoveNext,
    sched_iterator_GetMany,
};

static inline struct scheduled_toast_vector *impl_from_FIIterable_ScheduledToastNotification(
        __FIIterable_1_Windows__CUI__CNotifications__CScheduledToastNotification *iface )
{
    return CONTAINING_RECORD( iface, struct scheduled_toast_vector,
            __FIIterable_1_Windows__CUI__CNotifications__CScheduledToastNotification_iface );
}

static HRESULT WINAPI sched_iterable_QueryInterface(
        __FIIterable_1_Windows__CUI__CNotifications__CScheduledToastNotification *iface, REFIID iid, void **out )
{
    struct scheduled_toast_vector *impl = impl_from_FIIterable_ScheduledToastNotification( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    /* IUnknown identity is the vector view iface, the object's canonical one. */
    if (IsEqualGUID( iid, &IID_IUnknown )
            || IsEqualGUID( iid, &IID_IInspectable )
            || IsEqualGUID( iid, &IID_IAgileObject )
            || IsEqualGUID( iid, &IID___FIVectorView_1_Windows__CUI__CNotifications__CScheduledToastNotification ))
    {
        IUnknown_AddRef( (IUnknown *)iface );
        *out = &impl->__FIVectorView_1_Windows__CUI__CNotifications__CScheduledToastNotification_iface;
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID___FIIterable_1_Windows__CUI__CNotifications__CScheduledToastNotification ))
    {
        IUnknown_AddRef( (IUnknown *)iface );
        *out = iface;
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI sched_iterable_AddRef(
        __FIIterable_1_Windows__CUI__CNotifications__CScheduledToastNotification *iface )
{
    struct scheduled_toast_vector *impl = impl_from_FIIterable_ScheduledToastNotification( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI sched_iterable_Release(
        __FIIterable_1_Windows__CUI__CNotifications__CScheduledToastNotification *iface )
{
    struct scheduled_toast_vector *impl = impl_from_FIIterable_ScheduledToastNotification( iface );
    LONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        TRACE( "destroying %p\n", iface );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI sched_iterable_GetIids(
        __FIIterable_1_Windows__CUI__CNotifications__CScheduledToastNotification *iface, ULONG *iid_count, IID **iids )
{
    if (!iid_count || !iids) return E_POINTER;
    *iid_count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI sched_iterable_GetRuntimeClassName(
        __FIIterable_1_Windows__CUI__CNotifications__CScheduledToastNotification *iface, HSTRING *class_name )
{
    static const WCHAR name[] = L"Windows.Foundation.Collections.IIterable`1<Windows.UI.Notifications.ScheduledToastNotification>";
    return WindowsCreateString( name, ARRAY_SIZE( name ) - 1, class_name );
}

static HRESULT WINAPI sched_iterable_GetTrustLevel(
        __FIIterable_1_Windows__CUI__CNotifications__CScheduledToastNotification *iface, TrustLevel *trust_level )
{
    if (trust_level) *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI sched_iterable_First(
        __FIIterable_1_Windows__CUI__CNotifications__CScheduledToastNotification *iface,
        __FIIterator_1_Windows__CUI__CNotifications__CScheduledToastNotification **result )
{
    struct scheduled_toast_iterator *impl;

    TRACE( "iface %p, result %p.\n", iface, result );

    if (!result) return E_POINTER;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;

    impl->__FIIterator_1_Windows__CUI__CNotifications__CScheduledToastNotification_iface.lpVtbl = &sched_iterator_vtbl;
    impl->ref = 1;
    *result = &impl->__FIIterator_1_Windows__CUI__CNotifications__CScheduledToastNotification_iface;
    return S_OK;
}

static const struct __FIIterable_1_Windows__CUI__CNotifications__CScheduledToastNotificationVtbl sched_iterable_vtbl =
{
    sched_iterable_QueryInterface,
    sched_iterable_AddRef,
    sched_iterable_Release,
    /* IInspectable methods */
    sched_iterable_GetIids,
    sched_iterable_GetRuntimeClassName,
    sched_iterable_GetTrustLevel,
    /* IIterable<ScheduledToastNotification> methods */
    sched_iterable_First,
};

static HRESULT toast_notifier_GetScheduled( IToastNotifier *iface,
        __FIVectorView_1_Windows__CUI__CNotifications__CScheduledToastNotification **result )
{
    struct scheduled_toast_vector *impl;

    if (!result) return E_POINTER;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;

    impl->__FIVectorView_1_Windows__CUI__CNotifications__CScheduledToastNotification_iface.lpVtbl = &sched_vector_vtbl;
    impl->__FIIterable_1_Windows__CUI__CNotifications__CScheduledToastNotification_iface.lpVtbl = &sched_iterable_vtbl;
    impl->ref = 1;
    *result = &impl->__FIVectorView_1_Windows__CUI__CNotifications__CScheduledToastNotification_iface;
    return S_OK;
}

static const struct IToastNotifierVtbl toast_notifier_vtbl =
{
    toast_notifier_QueryInterface,
    toast_notifier_AddRef,
    toast_notifier_Release,
    /* IInspectable methods */
    toast_notifier_GetIids,
    toast_notifier_GetRuntimeClassName,
    toast_notifier_GetTrustLevel,
    /* IToastNotifier methods */
    toast_notifier_Show,
    toast_notifier_Hide,
    toast_notifier_get_Setting,
    toast_notifier_AddToSchedule,
    toast_notifier_RemoveFromSchedule,
    toast_notifier_GetScheduled,
};

static HRESULT create_toast_notifier( __x_ABI_CWindows_CUI_CNotifications_CIToastNotifier **out )
{
    struct toast_notifier *impl;

    if (!out) return E_POINTER;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;

    impl->IToastNotifier_iface.lpVtbl = &toast_notifier_vtbl;
    impl->ref = 1;
    *out = (__x_ABI_CWindows_CUI_CNotifications_CIToastNotifier *)&impl->IToastNotifier_iface;
    return S_OK;
}

struct toast_notification_manager_statics
{
    IActivationFactory IActivationFactory_iface;
    IToastNotificationManagerStatics IToastNotificationManagerStatics_iface;
    IToastNotificationManagerStatics2 IToastNotificationManagerStatics2_iface;
    IToastNotificationManagerStatics4 IToastNotificationManagerStatics4_iface;
    IToastNotificationManagerStatics5 IToastNotificationManagerStatics5_iface;
    LONG ref;
};

static inline struct toast_notification_manager_statics *implTUM_from_IActivationFactory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct toast_notification_manager_statics, IActivationFactory_iface );
}

static HRESULT WINAPI toast_notification_manager_statics_factory_QueryInterface( IActivationFactory *iface,
        REFIID iid, void **out )
{
    struct toast_notification_manager_statics *impl = implTUM_from_IActivationFactory( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown )
            || IsEqualGUID( iid, &IID_IInspectable )
            || IsEqualGUID( iid, &IID_IAgileObject )
            || IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        IUnknown_AddRef( iface );
        *out = iface;
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IToastNotificationManagerStatics ))
    {
        IUnknown_AddRef( iface );
        *out = &impl->IToastNotificationManagerStatics_iface;
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IToastNotificationManagerStatics2 ))
    {
        IUnknown_AddRef( iface );
        *out = &impl->IToastNotificationManagerStatics2_iface;
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IToastNotificationManagerStatics4 ))
    {
        IUnknown_AddRef( iface );
        *out = &impl->IToastNotificationManagerStatics4_iface;
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IToastNotificationManagerStatics5 ))
    {
        IUnknown_AddRef( iface );
        *out = &impl->IToastNotificationManagerStatics5_iface;
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI toast_notification_manager_statics_factory_AddRef( IActivationFactory *iface )
{
    struct toast_notification_manager_statics *impl = implTUM_from_IActivationFactory( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI toast_notification_manager_statics_factory_Release( IActivationFactory *iface )
{
    struct toast_notification_manager_statics *impl = implTUM_from_IActivationFactory( iface );
    return InterlockedDecrement( &impl->ref );
}

static HRESULT WINAPI toast_notification_manager_statics_factory_GetIids( IActivationFactory *iface,
        ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI toast_notification_manager_statics_factory_GetRuntimeClassName( IActivationFactory *iface,
        HSTRING *class_name )
{
    return WindowsCreateString( RuntimeClass_Windows_UI_Notifications_ToastNotificationManager,
                                ARRAY_SIZE( RuntimeClass_Windows_UI_Notifications_ToastNotificationManager ) - 1,
                                class_name );
}

static HRESULT WINAPI toast_notification_manager_statics_factory_GetTrustLevel( IActivationFactory *iface,
        TrustLevel *trust_level )
{
    if (trust_level) *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI toast_notification_manager_statics_factory_ActivateInstance( IActivationFactory *iface,
        IInspectable **instance )
{
    /* ToastNotificationManager exposes only static entry points. */
    FIXME( "iface %p, instance %p.\n", iface, instance );
    return E_NOINTERFACE;
}

static const struct IActivationFactoryVtbl toast_notification_manager_statics_factory_vtbl =
{
    toast_notification_manager_statics_factory_QueryInterface,
    toast_notification_manager_statics_factory_AddRef,
    toast_notification_manager_statics_factory_Release,
    /* IInspectable methods */
    toast_notification_manager_statics_factory_GetIids,
    toast_notification_manager_statics_factory_GetRuntimeClassName,
    toast_notification_manager_statics_factory_GetTrustLevel,
    /* IActivationFactory methods */
    toast_notification_manager_statics_factory_ActivateInstance,
};

DEFINE_IINSPECTABLE( toast_notification_manager_statics, IToastNotificationManagerStatics,
                     struct toast_notification_manager_statics, IActivationFactory_iface )

static HRESULT WINAPI toast_notification_manager_statics_CreateToastNotifier(
        IToastNotificationManagerStatics *iface, __x_ABI_CWindows_CUI_CNotifications_CIToastNotifier **result )
{
    TRACE( "iface %p, result %p.\n", iface, result );
    return create_toast_notifier( result );
}

static HRESULT WINAPI toast_notification_manager_statics_CreateToastNotifierWithId(
        IToastNotificationManagerStatics *iface, HSTRING application_id,
        __x_ABI_CWindows_CUI_CNotifications_CIToastNotifier **result )
{
    TRACE( "iface %p, application_id %s, result %p.\n", iface, debugstr_hstring( application_id ), result );
    return create_toast_notifier( result );
}

static HRESULT WINAPI toast_notification_manager_statics_GetTemplateContent(
        IToastNotificationManagerStatics *iface, __x_ABI_CWindows_CUI_CNotifications_CToastTemplateType type,
        __x_ABI_CWindows_CData_CXml_CDom_CIXmlDocument **result )
{
    FIXME( "iface %p, type %d, result %p stub!\n", iface, type, result );
    return E_NOTIMPL;
}

static const struct IToastNotificationManagerStaticsVtbl toast_notification_manager_statics_vtbl =
{
    toast_notification_manager_statics_QueryInterface,
    toast_notification_manager_statics_AddRef,
    toast_notification_manager_statics_Release,
    /* IInspectable methods */
    toast_notification_manager_statics_GetIids,
    toast_notification_manager_statics_GetRuntimeClassName,
    toast_notification_manager_statics_GetTrustLevel,
    /* IToastNotificationManagerStatics methods */
    toast_notification_manager_statics_CreateToastNotifier,
    toast_notification_manager_statics_CreateToastNotifierWithId,
    toast_notification_manager_statics_GetTemplateContent,
};

/*
 * The later static interfaces of the class (windows.ui.notifications.idl:
 * IToastNotificationManagerStatics2 7ab93c52-0e48-4750-ba9d-1a4113981847:1280,
 * IToastNotificationManagerStatics4 8f993fd3-e516-45fb-8130-398e93fa52c3:1290,
 * IToastNotificationManagerStatics5 d6f5f569-d40d-407c-8989-88cab42cfd14:1301).
 * The .NET projection of the notification path reaches its notifier through
 * GetDefault()/GetForUser(), so those hand out the per-user manager; the toast
 * history is a real store on Windows and is not produced here, so its accessors
 * report E_NOTIMPL instead of answering with an object the tray would drive
 * blind.
 */
static inline struct toast_notification_manager_statics *impl_from_IToastNotificationManagerStatics2(
        IToastNotificationManagerStatics2 *iface )
{
    return CONTAINING_RECORD( iface, struct toast_notification_manager_statics,
            IToastNotificationManagerStatics2_iface );
}

static inline struct toast_notification_manager_statics *impl_from_IToastNotificationManagerStatics4(
        IToastNotificationManagerStatics4 *iface )
{
    return CONTAINING_RECORD( iface, struct toast_notification_manager_statics,
            IToastNotificationManagerStatics4_iface );
}

static inline struct toast_notification_manager_statics *impl_from_IToastNotificationManagerStatics5(
        IToastNotificationManagerStatics5 *iface )
{
    return CONTAINING_RECORD( iface, struct toast_notification_manager_statics,
            IToastNotificationManagerStatics5_iface );
}

static HRESULT create_toast_notification_manager_for_user( IToastNotificationManagerForUser **out );

FORWARD_IINSPECTABLE( toast_notification_manager_statics2, IToastNotificationManagerStatics2, struct toast_notification_manager_statics, impl_from_IToastNotificationManagerStatics2, &impl->IActivationFactory_iface )
FORWARD_IINSPECTABLE( toast_notification_manager_statics4, IToastNotificationManagerStatics4, struct toast_notification_manager_statics, impl_from_IToastNotificationManagerStatics4, &impl->IActivationFactory_iface )
FORWARD_IINSPECTABLE( toast_notification_manager_statics5, IToastNotificationManagerStatics5, struct toast_notification_manager_statics, impl_from_IToastNotificationManagerStatics5, &impl->IActivationFactory_iface )

static HRESULT WINAPI toast_notification_manager_statics2_get_History(
        IToastNotificationManagerStatics2 *iface, IToastNotificationHistory **value )
{
    FIXME( "iface %p, value %p stub!\n", iface, value );
    return E_NOTIMPL;
}

static const struct IToastNotificationManagerStatics2Vtbl toast_notification_manager_statics2_vtbl =
{
    toast_notification_manager_statics2_QueryInterface,
    toast_notification_manager_statics2_AddRef,
    toast_notification_manager_statics2_Release,
    /* IInspectable methods */
    toast_notification_manager_statics2_GetIids,
    toast_notification_manager_statics2_GetRuntimeClassName,
    toast_notification_manager_statics2_GetTrustLevel,
    /* IToastNotificationManagerStatics2 methods */
    toast_notification_manager_statics2_get_History,
};

static HRESULT WINAPI toast_notification_manager_statics4_GetForUser( IToastNotificationManagerStatics4 *iface,
        __x_ABI_CWindows_CSystem_CIUser *user, IToastNotificationManagerForUser **result )
{
    TRACE( "iface %p, user %p, result %p.\n", iface, user, result );

    /* A prefix has one interactive user and no user identity to key on, so every
     * user's manager is the same functional surface. */
    return create_toast_notification_manager_for_user( result );
}

static HRESULT WINAPI toast_notification_manager_statics4_ConfigureNotificationMirroring(
        IToastNotificationManagerStatics4 *iface, NotificationMirroring value )
{
    /* Nothing mirrors toasts from here; accepting the setting is the outcome. */
    TRACE( "iface %p, value %d.\n", iface, value );
    return S_OK;
}

static const struct IToastNotificationManagerStatics4Vtbl toast_notification_manager_statics4_vtbl =
{
    toast_notification_manager_statics4_QueryInterface,
    toast_notification_manager_statics4_AddRef,
    toast_notification_manager_statics4_Release,
    /* IInspectable methods */
    toast_notification_manager_statics4_GetIids,
    toast_notification_manager_statics4_GetRuntimeClassName,
    toast_notification_manager_statics4_GetTrustLevel,
    /* IToastNotificationManagerStatics4 methods */
    toast_notification_manager_statics4_GetForUser,
    toast_notification_manager_statics4_ConfigureNotificationMirroring,
};

static HRESULT WINAPI toast_notification_manager_statics5_GetDefault(
        IToastNotificationManagerStatics5 *iface, IToastNotificationManagerForUser **result )
{
    TRACE( "iface %p, result %p.\n", iface, result );
    return create_toast_notification_manager_for_user( result );
}

static const struct IToastNotificationManagerStatics5Vtbl toast_notification_manager_statics5_vtbl =
{
    toast_notification_manager_statics5_QueryInterface,
    toast_notification_manager_statics5_AddRef,
    toast_notification_manager_statics5_Release,
    /* IInspectable methods */
    toast_notification_manager_statics5_GetIids,
    toast_notification_manager_statics5_GetRuntimeClassName,
    toast_notification_manager_statics5_GetTrustLevel,
    /* IToastNotificationManagerStatics5 methods */
    toast_notification_manager_statics5_GetDefault,
};

/*
 * Windows.UI.Notifications.ToastNotificationManagerForUser
 * (IToastNotificationManagerForUser 79ab57f6-43fe-487b-8a7f-99567200ae94:1228):
 * the per-user notifier factory.  The history and user accessors report
 * E_NOTIMPL, the honest marker for the store and the identity this prefix has
 * no answer for.
 */
struct toast_notification_manager_for_user
{
    IToastNotificationManagerForUser IToastNotificationManagerForUser_iface;
    LONG ref;
};

static inline struct toast_notification_manager_for_user *impl_from_IToastNotificationManagerForUser(
        IToastNotificationManagerForUser *iface )
{
    return CONTAINING_RECORD( iface, struct toast_notification_manager_for_user,
            IToastNotificationManagerForUser_iface );
}

static HRESULT WINAPI for_user_QueryInterface( IToastNotificationManagerForUser *iface, REFIID iid, void **out )
{
    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown )
            || IsEqualGUID( iid, &IID_IInspectable )
            || IsEqualGUID( iid, &IID_IAgileObject )
            || IsEqualGUID( iid, &IID_IToastNotificationManagerForUser ))
    {
        IUnknown_AddRef( iface );
        *out = iface;
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI for_user_AddRef( IToastNotificationManagerForUser *iface )
{
    struct toast_notification_manager_for_user *impl = impl_from_IToastNotificationManagerForUser( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI for_user_Release( IToastNotificationManagerForUser *iface )
{
    struct toast_notification_manager_for_user *impl = impl_from_IToastNotificationManagerForUser( iface );
    LONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        TRACE( "destroying %p\n", iface );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI for_user_GetIids( IToastNotificationManagerForUser *iface, ULONG *iid_count, IID **iids )
{
    if (!iid_count || !iids) return E_POINTER;
    *iid_count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI for_user_GetRuntimeClassName( IToastNotificationManagerForUser *iface, HSTRING *class_name )
{
    return WindowsCreateString( RuntimeClass_Windows_UI_Notifications_ToastNotificationManagerForUser,
                                ARRAY_SIZE( RuntimeClass_Windows_UI_Notifications_ToastNotificationManagerForUser ) - 1,
                                class_name );
}

static HRESULT WINAPI for_user_GetTrustLevel( IToastNotificationManagerForUser *iface, TrustLevel *trust_level )
{
    if (trust_level) *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI for_user_CreateToastNotifier( IToastNotificationManagerForUser *iface,
        IToastNotifier **result )
{
    TRACE( "iface %p, result %p.\n", iface, result );
    return create_toast_notifier( result );
}

static HRESULT WINAPI for_user_CreateToastNotifierWithId( IToastNotificationManagerForUser *iface,
        HSTRING application_id, IToastNotifier **result )
{
    TRACE( "iface %p, application_id %s, result %p.\n", iface, debugstr_hstring( application_id ), result );
    return create_toast_notifier( result );
}

static HRESULT WINAPI for_user_get_History( IToastNotificationManagerForUser *iface,
        IToastNotificationHistory **value )
{
    FIXME( "iface %p, value %p stub!\n", iface, value );
    return E_NOTIMPL;
}

static HRESULT WINAPI for_user_get_User( IToastNotificationManagerForUser *iface,
        __x_ABI_CWindows_CSystem_CIUser **value )
{
    FIXME( "iface %p, value %p stub!\n", iface, value );
    return E_NOTIMPL;
}

static const struct IToastNotificationManagerForUserVtbl for_user_vtbl =
{
    for_user_QueryInterface,
    for_user_AddRef,
    for_user_Release,
    /* IInspectable methods */
    for_user_GetIids,
    for_user_GetRuntimeClassName,
    for_user_GetTrustLevel,
    /* IToastNotificationManagerForUser methods */
    for_user_CreateToastNotifier,
    for_user_CreateToastNotifierWithId,
    for_user_get_History,
    for_user_get_User,
};

static HRESULT create_toast_notification_manager_for_user( IToastNotificationManagerForUser **out )
{
    struct toast_notification_manager_for_user *impl;

    if (!out) return E_POINTER;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;

    impl->IToastNotificationManagerForUser_iface.lpVtbl = &for_user_vtbl;
    impl->ref = 1;
    *out = &impl->IToastNotificationManagerForUser_iface;
    return S_OK;
}

/*
 * Windows.UI.Notifications.ToastNotification
 * (IToastNotification 997e2675-059e-4e60-8b06-1760917c8b80:1081, IToastNotification2
 * 9dfb9fd1-143a-490e-90bf-b9fba7132de7:1105, IToastNotificationFactory
 * 04124b20-82c6-4229-b109-fd9ed4662b53:1168).
 *
 * The notification is the content document the notification path built (kept by
 * reference, so get_Content reports the same object it was handed) plus the
 * tag/group/suppression state the tray path reads back.  The dismissal,
 * activation and failure events are accepted and never fire: nothing is
 * delivered, so a handler registered here has nothing to report to.
 */
struct toast_notification
{
    IToastNotification IToastNotification_iface;
    IToastNotification2 IToastNotification2_iface;
    __x_ABI_CWindows_CData_CXml_CDom_CIXmlDocument *content;
    __FIReference_1_DateTime *expiration_time;
    HSTRING tag, group;
    boolean suppress_popup;
    LONG ref;
};

static inline struct toast_notification *impl_from_IToastNotification( IToastNotification *iface )
{
    return CONTAINING_RECORD( iface, struct toast_notification, IToastNotification_iface );
}

static inline struct toast_notification *impl_from_IToastNotification2( IToastNotification2 *iface )
{
    return CONTAINING_RECORD( iface, struct toast_notification, IToastNotification2_iface );
}

static HRESULT WINAPI toast_notification_QueryInterface( IToastNotification *iface, REFIID iid, void **out )
{
    struct toast_notification *impl = impl_from_IToastNotification( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown )
            || IsEqualGUID( iid, &IID_IInspectable )
            || IsEqualGUID( iid, &IID_IAgileObject )
            || IsEqualGUID( iid, &IID_IToastNotification ))
    {
        IUnknown_AddRef( iface );
        *out = iface;
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IToastNotification2 ))
    {
        IUnknown_AddRef( iface );
        *out = &impl->IToastNotification2_iface;
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI toast_notification_AddRef( IToastNotification *iface )
{
    struct toast_notification *impl = impl_from_IToastNotification( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI toast_notification_Release( IToastNotification *iface )
{
    struct toast_notification *impl = impl_from_IToastNotification( iface );
    LONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        TRACE( "destroying %p\n", iface );
        if (impl->content) IInspectable_Release( (IInspectable *)impl->content );
        if (impl->expiration_time) IInspectable_Release( (IInspectable *)impl->expiration_time );
        WindowsDeleteString( impl->tag );
        WindowsDeleteString( impl->group );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI toast_notification_GetIids( IToastNotification *iface, ULONG *iid_count, IID **iids )
{
    if (!iid_count || !iids) return E_POINTER;
    *iid_count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI toast_notification_GetRuntimeClassName( IToastNotification *iface, HSTRING *class_name )
{
    return WindowsCreateString( RuntimeClass_Windows_UI_Notifications_ToastNotification,
                                ARRAY_SIZE( RuntimeClass_Windows_UI_Notifications_ToastNotification ) - 1,
                                class_name );
}

static HRESULT WINAPI toast_notification_GetTrustLevel( IToastNotification *iface, TrustLevel *trust_level )
{
    if (trust_level) *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI toast_notification_get_Content( IToastNotification *iface,
        __x_ABI_CWindows_CData_CXml_CDom_CIXmlDocument **value )
{
    struct toast_notification *impl = impl_from_IToastNotification( iface );

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    if (!impl->content) return E_FAIL;
    IInspectable_AddRef( (IInspectable *)impl->content );
    *value = impl->content;
    return S_OK;
}

static HRESULT WINAPI toast_notification_put_ExpirationTime( IToastNotification *iface,
        __FIReference_1_DateTime *value )
{
    struct toast_notification *impl = impl_from_IToastNotification( iface );

    TRACE( "iface %p, value %p.\n", iface, value );

    if (value) IInspectable_AddRef( (IInspectable *)value );
    if (impl->expiration_time) IInspectable_Release( (IInspectable *)impl->expiration_time );
    impl->expiration_time = value;
    return S_OK;
}

static HRESULT WINAPI toast_notification_get_ExpirationTime( IToastNotification *iface,
        __FIReference_1_DateTime **value )
{
    struct toast_notification *impl = impl_from_IToastNotification( iface );

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    if (impl->expiration_time) IInspectable_AddRef( (IInspectable *)impl->expiration_time );
    *value = impl->expiration_time;
    return S_OK;
}

/* The events are accepted and forgotten; see the header comment above. */
static HRESULT WINAPI toast_notification_add_event( IToastNotification *iface, EventRegistrationToken *token )
{
    struct toast_notification *impl = impl_from_IToastNotification( iface );

    if (!token) return E_POINTER;
    token->value = (ULONGLONG)(size_t)impl;
    return S_OK;
}

static HRESULT WINAPI toast_notification_Dismissed_add( IToastNotification *iface,
        __FITypedEventHandler_2_Windows__CUI__CNotifications__CToastNotification_Windows__CUI__CNotifications__CToastDismissedEventArgs *handler,
        EventRegistrationToken *token )
{
    TRACE( "iface %p, handler %p, token %p.\n", iface, handler, token );
    return toast_notification_add_event( iface, token );
}

static HRESULT WINAPI toast_notification_Activated_add( IToastNotification *iface,
        __FITypedEventHandler_2_Windows__CUI__CNotifications__CToastNotification_IInspectable *handler,
        EventRegistrationToken *token )
{
    TRACE( "iface %p, handler %p, token %p.\n", iface, handler, token );
    return toast_notification_add_event( iface, token );
}

static HRESULT WINAPI toast_notification_Failed_add( IToastNotification *iface,
        __FITypedEventHandler_2_Windows__CUI__CNotifications__CToastNotification_Windows__CUI__CNotifications__CToastFailedEventArgs *handler,
        EventRegistrationToken *token )
{
    TRACE( "iface %p, handler %p, token %p.\n", iface, handler, token );
    return toast_notification_add_event( iface, token );
}

static HRESULT WINAPI toast_notification_event_remove( IToastNotification *iface,
        EventRegistrationToken token )
{
    TRACE( "iface %p, token %s.\n", iface, wine_dbgstr_longlong( token.value ) );
    return S_OK;
}

static const struct IToastNotificationVtbl toast_notification_vtbl =
{
    toast_notification_QueryInterface,
    toast_notification_AddRef,
    toast_notification_Release,
    /* IInspectable methods */
    toast_notification_GetIids,
    toast_notification_GetRuntimeClassName,
    toast_notification_GetTrustLevel,
    /* IToastNotification methods */
    toast_notification_get_Content,
    toast_notification_put_ExpirationTime,
    toast_notification_get_ExpirationTime,
    toast_notification_Dismissed_add,
    toast_notification_event_remove,
    toast_notification_Activated_add,
    toast_notification_event_remove,
    toast_notification_Failed_add,
    toast_notification_event_remove,
};

FORWARD_IINSPECTABLE( toast_notification2, IToastNotification2, struct toast_notification,
                      impl_from_IToastNotification2, &impl->IToastNotification_iface )

/* The tag, group and suppression state of the notification round-trips; an
 * unset string reads back empty, as the projection reports it. */
static HRESULT WINAPI toast_notification2_put_Tag( IToastNotification2 *iface, HSTRING value )
{
    struct toast_notification *impl = impl_from_IToastNotification2( iface );
    HSTRING dup = NULL;
    HRESULT hr;

    TRACE( "iface %p, value %s.\n", iface, debugstr_hstring( value ) );

    if (value && FAILED( hr = WindowsDuplicateString( value, &dup ) )) return hr;
    WindowsDeleteString( impl->tag );
    impl->tag = dup;
    return S_OK;
}

static HRESULT WINAPI toast_notification2_get_Tag( IToastNotification2 *iface, HSTRING *value )
{
    struct toast_notification *impl = impl_from_IToastNotification2( iface );

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    return WindowsDuplicateString( impl->tag, value );
}

static HRESULT WINAPI toast_notification2_put_Group( IToastNotification2 *iface, HSTRING value )
{
    struct toast_notification *impl = impl_from_IToastNotification2( iface );
    HSTRING dup = NULL;
    HRESULT hr;

    TRACE( "iface %p, value %s.\n", iface, debugstr_hstring( value ) );

    if (value && FAILED( hr = WindowsDuplicateString( value, &dup ) )) return hr;
    WindowsDeleteString( impl->group );
    impl->group = dup;
    return S_OK;
}

static HRESULT WINAPI toast_notification2_get_Group( IToastNotification2 *iface, HSTRING *value )
{
    struct toast_notification *impl = impl_from_IToastNotification2( iface );

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    return WindowsDuplicateString( impl->group, value );
}

static HRESULT WINAPI toast_notification2_put_SuppressPopup( IToastNotification2 *iface, boolean value )
{
    struct toast_notification *impl = impl_from_IToastNotification2( iface );

    TRACE( "iface %p, value %d.\n", iface, value );

    impl->suppress_popup = value;
    return S_OK;
}

static HRESULT WINAPI toast_notification2_get_SuppressPopup( IToastNotification2 *iface, boolean *value )
{
    struct toast_notification *impl = impl_from_IToastNotification2( iface );

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    *value = impl->suppress_popup;
    return S_OK;
}

static const struct IToastNotification2Vtbl toast_notification2_vtbl =
{
    toast_notification2_QueryInterface,
    toast_notification2_AddRef,
    toast_notification2_Release,
    /* IInspectable methods */
    toast_notification2_GetIids,
    toast_notification2_GetRuntimeClassName,
    toast_notification2_GetTrustLevel,
    /* IToastNotification2 methods */
    toast_notification2_put_Tag,
    toast_notification2_get_Tag,
    toast_notification2_put_Group,
    toast_notification2_get_Group,
    toast_notification2_put_SuppressPopup,
    toast_notification2_get_SuppressPopup,
};

static HRESULT create_toast_notification( __x_ABI_CWindows_CData_CXml_CDom_CIXmlDocument *content,
                                          IToastNotification **out )
{
    static const WCHAR emptyW[] = L"";
    struct toast_notification *impl;
    HRESULT hr;

    if (!out) return E_POINTER;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;

    impl->IToastNotification_iface.lpVtbl = &toast_notification_vtbl;
    impl->IToastNotification2_iface.lpVtbl = &toast_notification2_vtbl;
    impl->ref = 1;

    if (content)
    {
        IInspectable_AddRef( (IInspectable *)content );
        impl->content = content;
    }
    if (FAILED( hr = WindowsCreateString( emptyW, 0, &impl->tag ) ))
    {
        if (content) IInspectable_Release( (IInspectable *)content );
        free( impl );
        return hr;
    }
    if (FAILED( hr = WindowsCreateString( emptyW, 0, &impl->group ) ))
    {
        WindowsDeleteString( impl->tag );
        if (content) IInspectable_Release( (IInspectable *)content );
        free( impl );
        return hr;
    }
    *out = &impl->IToastNotification_iface;
    return S_OK;
}

/*
 * The class factory of Windows.UI.Notifications.ToastNotification: the class is
 * activatable with an interface (windows.ui.notifications.idl:1655), so its
 * IActivationFactory answers IToastNotificationFactory and has no parameterless
 * activation.
 */
struct toast_notification_factory
{
    IActivationFactory IActivationFactory_iface;
    IToastNotificationFactory IToastNotificationFactory_iface;
    LONG ref;
};

static inline struct toast_notification_factory *impl_from_toast_notification_factory(
        IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct toast_notification_factory, IActivationFactory_iface );
}

static inline struct toast_notification_factory *impl_from_IToastNotificationFactory(
        IToastNotificationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct toast_notification_factory, IToastNotificationFactory_iface );
}

static HRESULT WINAPI toast_notification_factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct toast_notification_factory *impl = impl_from_toast_notification_factory( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown )
            || IsEqualGUID( iid, &IID_IInspectable )
            || IsEqualGUID( iid, &IID_IAgileObject )
            || IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        IUnknown_AddRef( iface );
        *out = iface;
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IToastNotificationFactory ))
    {
        IUnknown_AddRef( iface );
        *out = &impl->IToastNotificationFactory_iface;
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI toast_notification_factory_AddRef( IActivationFactory *iface )
{
    struct toast_notification_factory *impl = impl_from_toast_notification_factory( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI toast_notification_factory_Release( IActivationFactory *iface )
{
    struct toast_notification_factory *impl = impl_from_toast_notification_factory( iface );
    return InterlockedDecrement( &impl->ref );
}

static HRESULT WINAPI toast_notification_factory_GetIids( IActivationFactory *iface, ULONG *iid_count, IID **iids )
{
    if (!iid_count || !iids) return E_POINTER;
    *iid_count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI toast_notification_factory_GetRuntimeClassName( IActivationFactory *iface,
        HSTRING *class_name )
{
    return WindowsCreateString( RuntimeClass_Windows_UI_Notifications_ToastNotification,
                                ARRAY_SIZE( RuntimeClass_Windows_UI_Notifications_ToastNotification ) - 1,
                                class_name );
}

static HRESULT WINAPI toast_notification_factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *trust_level )
{
    if (trust_level) *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI toast_notification_factory_ActivateInstance( IActivationFactory *iface,
        IInspectable **instance )
{
    /* ToastNotification is activatable with an interface, not without arguments. */
    FIXME( "iface %p, instance %p.\n", iface, instance );
    return E_NOINTERFACE;
}

static const struct IActivationFactoryVtbl toast_notification_factory_vtbl =
{
    toast_notification_factory_QueryInterface,
    toast_notification_factory_AddRef,
    toast_notification_factory_Release,
    /* IInspectable methods */
    toast_notification_factory_GetIids,
    toast_notification_factory_GetRuntimeClassName,
    toast_notification_factory_GetTrustLevel,
    /* IActivationFactory methods */
    toast_notification_factory_ActivateInstance,
};

FORWARD_IINSPECTABLE( toast_notification_factory_statics, IToastNotificationFactory, struct toast_notification_factory, impl_from_IToastNotificationFactory, &impl->IActivationFactory_iface )

static HRESULT WINAPI toast_notification_factory_CreateToastNotification( IToastNotificationFactory *iface,
        __x_ABI_CWindows_CData_CXml_CDom_CIXmlDocument *content, IToastNotification **value )
{
    TRACE( "iface %p, content %p, value %p.\n", iface, content, value );
    return create_toast_notification( content, value );
}

static const struct IToastNotificationFactoryVtbl toast_notification_factory_toast_vtbl =
{
    toast_notification_factory_statics_QueryInterface,
    toast_notification_factory_statics_AddRef,
    toast_notification_factory_statics_Release,
    /* IInspectable methods */
    toast_notification_factory_statics_GetIids,
    toast_notification_factory_statics_GetRuntimeClassName,
    toast_notification_factory_statics_GetTrustLevel,
    /* IToastNotificationFactory methods */
    toast_notification_factory_CreateToastNotification,
};

static struct toast_notification_manager_statics toast_notification_manager_statics =
{
    { &toast_notification_manager_statics_factory_vtbl },
    { &toast_notification_manager_statics_vtbl },
    { &toast_notification_manager_statics2_vtbl },
    { &toast_notification_manager_statics4_vtbl },
    { &toast_notification_manager_statics5_vtbl },
    1
};

static struct toast_notification_factory toast_notification_factory_obj =
{
    { &toast_notification_factory_vtbl },
    { &toast_notification_factory_toast_vtbl },
    1
};

IActivationFactory *toast_notification_manager_statics_factory =
    &toast_notification_manager_statics.IActivationFactory_iface;

IActivationFactory *toast_notification_factory =
    &toast_notification_factory_obj.IActivationFactory_iface;
