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

static struct toast_notification_manager_statics toast_notification_manager_statics =
{
    { &toast_notification_manager_statics_factory_vtbl },
    { &toast_notification_manager_statics_vtbl },
    1
};

IActivationFactory *toast_notification_manager_statics_factory =
    &toast_notification_manager_statics.IActivationFactory_iface;
