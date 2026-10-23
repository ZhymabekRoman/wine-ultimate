/* WinRT Windows.UI.Notifications.BadgeUpdateManager implementation
 *
 * The tray-icon badge path of packaged apps activates
 * Windows.UI.Notifications.BadgeUpdateManager (on Windows the class is served
 * by twinapi.appcore.dll).  Wine answers it with a functional no-op badge
 * surface: the statics hand out BadgeUpdater instances whose Update/Clear/
 * periodic-update entry points succeed.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include "private.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(twinapi);

struct badge_updater
{
    IBadgeUpdater IBadgeUpdater_iface;
    LONG ref;
};

static inline struct badge_updater *impl_from_IBadgeUpdater( IBadgeUpdater *iface )
{
    return CONTAINING_RECORD( iface, struct badge_updater, IBadgeUpdater_iface );
}

static HRESULT WINAPI badge_updater_QueryInterface( IBadgeUpdater *iface, REFIID iid, void **out )
{
    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown )
            || IsEqualGUID( iid, &IID_IInspectable )
            || IsEqualGUID( iid, &IID_IAgileObject )
            || IsEqualGUID( iid, &IID_IBadgeUpdater ))
    {
        IUnknown_AddRef( (IUnknown *)iface );
        *out = iface;
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI badge_updater_AddRef( IBadgeUpdater *iface )
{
    struct badge_updater *impl = impl_from_IBadgeUpdater( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI badge_updater_Release( IBadgeUpdater *iface )
{
    struct badge_updater *impl = impl_from_IBadgeUpdater( iface );
    LONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        TRACE( "destroying %p\n", iface );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI badge_updater_GetIids( IBadgeUpdater *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI badge_updater_GetRuntimeClassName( IBadgeUpdater *iface, HSTRING *class_name )
{
    return WindowsCreateString( RuntimeClass_Windows_UI_Notifications_BadgeUpdater,
                                ARRAY_SIZE( RuntimeClass_Windows_UI_Notifications_BadgeUpdater ) - 1, class_name );
}

static HRESULT WINAPI badge_updater_GetTrustLevel( IBadgeUpdater *iface, TrustLevel *trust_level )
{
    if (trust_level) *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI badge_updater_Update( IBadgeUpdater *iface, __x_ABI_CWindows_CUI_CNotifications_CIBadgeNotification *notification )
{
    FIXME( "iface %p, notification %p stub!\n", iface, notification );
    return S_OK;
}

static HRESULT WINAPI badge_updater_Clear( IBadgeUpdater *iface )
{
    FIXME( "iface %p stub!\n", iface );
    return S_OK;
}

static HRESULT WINAPI badge_updater_StartPeriodicUpdate( IBadgeUpdater *iface,
        __x_ABI_CWindows_CFoundation_CIUriRuntimeClass *badge_content, __x_ABI_CWindows_CUI_CNotifications_CPeriodicUpdateRecurrence requested_interval )
{
    FIXME( "iface %p, badge_content %p, requested_interval %d stub!\n", iface, badge_content, requested_interval );
    return S_OK;
}

static HRESULT WINAPI badge_updater_StartPeriodicUpdateAtTime( IBadgeUpdater *iface,
        __x_ABI_CWindows_CFoundation_CIUriRuntimeClass *badge_content, __x_ABI_CWindows_CFoundation_CDateTime start_time,
        __x_ABI_CWindows_CUI_CNotifications_CPeriodicUpdateRecurrence requested_interval )
{
    FIXME( "iface %p, badge_content %p, requested_interval %d stub!\n", iface, badge_content, requested_interval );
    return S_OK;
}

static HRESULT WINAPI badge_updater_StopPeriodicUpdate( IBadgeUpdater *iface )
{
    FIXME( "iface %p stub!\n", iface );
    return S_OK;
}

static const struct IBadgeUpdaterVtbl badge_updater_vtbl =
{
    badge_updater_QueryInterface,
    badge_updater_AddRef,
    badge_updater_Release,
    /* IInspectable methods */
    badge_updater_GetIids,
    badge_updater_GetRuntimeClassName,
    badge_updater_GetTrustLevel,
    /* IBadgeUpdater methods */
    badge_updater_Update,
    badge_updater_Clear,
    badge_updater_StartPeriodicUpdate,
    badge_updater_StartPeriodicUpdateAtTime,
    badge_updater_StopPeriodicUpdate,
};

static HRESULT create_badge_updater( __x_ABI_CWindows_CUI_CNotifications_CIBadgeUpdater **out )
{
    struct badge_updater *impl;

    if (!out) return E_POINTER;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;

    impl->IBadgeUpdater_iface.lpVtbl = &badge_updater_vtbl;
    impl->ref = 1;
    *out = (__x_ABI_CWindows_CUI_CNotifications_CIBadgeUpdater *)&impl->IBadgeUpdater_iface;
    return S_OK;
}

struct badge_update_manager_statics
{
    IActivationFactory IActivationFactory_iface;
    IBadgeUpdateManagerStatics IBadgeUpdateManagerStatics_iface;
    LONG ref;
};

static inline struct badge_update_manager_statics *implBUM_from_IActivationFactory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct badge_update_manager_statics, IActivationFactory_iface );
}

static HRESULT WINAPI badge_update_manager_statics_factory_QueryInterface( IActivationFactory *iface,
        REFIID iid, void **out )
{
    struct badge_update_manager_statics *impl = implBUM_from_IActivationFactory( iface );

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

    if (IsEqualGUID( iid, &IID_IBadgeUpdateManagerStatics ))
    {
        IUnknown_AddRef( iface );
        *out = &impl->IBadgeUpdateManagerStatics_iface;
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI badge_update_manager_statics_factory_AddRef( IActivationFactory *iface )
{
    struct badge_update_manager_statics *impl = implBUM_from_IActivationFactory( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI badge_update_manager_statics_factory_Release( IActivationFactory *iface )
{
    struct badge_update_manager_statics *impl = implBUM_from_IActivationFactory( iface );
    return InterlockedDecrement( &impl->ref );
}

static HRESULT WINAPI badge_update_manager_statics_factory_GetIids( IActivationFactory *iface,
        ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI badge_update_manager_statics_factory_GetRuntimeClassName( IActivationFactory *iface,
        HSTRING *class_name )
{
    return WindowsCreateString( RuntimeClass_Windows_UI_Notifications_BadgeUpdateManager,
                                ARRAY_SIZE( RuntimeClass_Windows_UI_Notifications_BadgeUpdateManager ) - 1,
                                class_name );
}

static HRESULT WINAPI badge_update_manager_statics_factory_GetTrustLevel( IActivationFactory *iface,
        TrustLevel *trust_level )
{
    if (trust_level) *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI badge_update_manager_statics_factory_ActivateInstance( IActivationFactory *iface,
        IInspectable **instance )
{
    /* BadgeUpdateManager exposes only static entry points. */
    FIXME( "iface %p, instance %p.\n", iface, instance );
    return E_NOINTERFACE;
}

static const struct IActivationFactoryVtbl badge_update_manager_statics_factory_vtbl =
{
    badge_update_manager_statics_factory_QueryInterface,
    badge_update_manager_statics_factory_AddRef,
    badge_update_manager_statics_factory_Release,
    /* IInspectable methods */
    badge_update_manager_statics_factory_GetIids,
    badge_update_manager_statics_factory_GetRuntimeClassName,
    badge_update_manager_statics_factory_GetTrustLevel,
    /* IActivationFactory methods */
    badge_update_manager_statics_factory_ActivateInstance,
};

DEFINE_IINSPECTABLE( badge_update_manager_statics, IBadgeUpdateManagerStatics,
                     struct badge_update_manager_statics, IActivationFactory_iface )

static HRESULT WINAPI badge_update_manager_statics_CreateBadgeUpdaterForApplication(
        IBadgeUpdateManagerStatics *iface, __x_ABI_CWindows_CUI_CNotifications_CIBadgeUpdater **result )
{
    TRACE( "iface %p, result %p.\n", iface, result );
    return create_badge_updater( result );
}

static HRESULT WINAPI badge_update_manager_statics_CreateBadgeUpdaterForApplicationWithId(
        IBadgeUpdateManagerStatics *iface, HSTRING application_id, __x_ABI_CWindows_CUI_CNotifications_CIBadgeUpdater **result )
{
    TRACE( "iface %p, application_id %s, result %p.\n", iface, debugstr_hstring( application_id ), result );
    return create_badge_updater( result );
}

static HRESULT WINAPI badge_update_manager_statics_CreateBadgeUpdaterForSecondaryTile(
        IBadgeUpdateManagerStatics *iface, HSTRING tile_id, __x_ABI_CWindows_CUI_CNotifications_CIBadgeUpdater **result )
{
    TRACE( "iface %p, tile_id %s, result %p.\n", iface, debugstr_hstring( tile_id ), result );
    return create_badge_updater( result );
}

static HRESULT WINAPI badge_update_manager_statics_GetTemplateContent(
        IBadgeUpdateManagerStatics *iface, __x_ABI_CWindows_CUI_CNotifications_CBadgeTemplateType type,
        __x_ABI_CWindows_CData_CXml_CDom_CIXmlDocument **result )
{
    FIXME( "iface %p, type %d, result %p stub!\n", iface, type, result );
    return E_NOTIMPL;
}

static const struct IBadgeUpdateManagerStaticsVtbl badge_update_manager_statics_vtbl =
{
    badge_update_manager_statics_QueryInterface,
    badge_update_manager_statics_AddRef,
    badge_update_manager_statics_Release,
    /* IInspectable methods */
    badge_update_manager_statics_GetIids,
    badge_update_manager_statics_GetRuntimeClassName,
    badge_update_manager_statics_GetTrustLevel,
    /* IBadgeUpdateManagerStatics methods */
    badge_update_manager_statics_CreateBadgeUpdaterForApplication,
    badge_update_manager_statics_CreateBadgeUpdaterForApplicationWithId,
    badge_update_manager_statics_CreateBadgeUpdaterForSecondaryTile,
    badge_update_manager_statics_GetTemplateContent,
};

static struct badge_update_manager_statics badge_update_manager_statics =
{
    { &badge_update_manager_statics_factory_vtbl },
    { &badge_update_manager_statics_vtbl },
    1
};

IActivationFactory *badge_update_manager_statics_factory = &badge_update_manager_statics.IActivationFactory_iface;
