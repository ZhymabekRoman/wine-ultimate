/* WinRT Windows.System.Power.PowerManager implementation
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

#include "wine/list.h"

WINE_DEFAULT_DEBUG_CHANNEL(twinapi);

static const WCHAR runtime_class_name[] = {'W','i','n','d','o','w','s','.','S','y','s','t','e','m','.','P','o','w','e','r','.','P','o','w','e','r','M','a','n','a','g','e','r'};

static SRWLOCK event_lock = SRWLOCK_INIT;
static struct list event_list = LIST_INIT( event_list );
static INT64 next_token;

struct event_registration
{
    struct list entry;
    EventRegistrationToken token;
    __FIEventHandler_1_IInspectable *handler;
};

static HRESULT add_event_handler( __FIEventHandler_1_IInspectable *handler, EventRegistrationToken *token )
{
    struct event_registration *reg;

    TRACE( "handler %p, token %p.\n", handler, token );

    if (!handler || !token) return E_POINTER;
    if (!(reg = calloc( 1, sizeof(*reg) ))) return E_OUTOFMEMORY;

    reg->handler = handler;
    __FIEventHandler_1_IInspectable_AddRef( handler );
    reg->token.value = InterlockedIncrement64( &next_token );

    AcquireSRWLockExclusive( &event_lock );
    list_add_tail( &event_list, &reg->entry );
    ReleaseSRWLockExclusive( &event_lock );

    *token = reg->token;
    return S_OK;
}

static HRESULT remove_event_handler( EventRegistrationToken token )
{
    struct event_registration *reg, *cursor;
    BOOL found = FALSE;

    TRACE( "token %s.\n", wine_dbgstr_longlong( token.value ) );

    if (!token.value) return E_INVALIDARG;

    AcquireSRWLockExclusive( &event_lock );
    LIST_FOR_EACH_ENTRY_SAFE( reg, cursor, &event_list, struct event_registration, entry )
    {
        if (reg->token.value == token.value)
        {
            list_remove( &reg->entry );
            found = TRUE;
            break;
        }
    }
    ReleaseSRWLockExclusive( &event_lock );

    if (found)
    {
        __FIEventHandler_1_IInspectable_Release( reg->handler );
        free( reg );
    }

    return S_OK;
}

struct power_manager_factory
{
    IActivationFactory IActivationFactory_iface;
    IPowerManagerStatics IPowerManagerStatics_iface;
    LONG ref;
};

static inline struct power_manager_factory *impl_from_IPowerManagerStatics( IPowerManagerStatics *iface )
{
    return CONTAINING_RECORD( iface, struct power_manager_factory, IPowerManagerStatics_iface );
}

static inline struct power_manager_factory *impl_from_IActivationFactory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct power_manager_factory, IActivationFactory_iface );
}

static HRESULT WINAPI power_manager_QueryInterface( struct power_manager_factory *impl, REFIID iid, void **out )
{
    TRACE( "iid %s, out %p.\n", debugstr_guid( iid ), out );

    if (!out) return E_POINTER;

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = &impl->IActivationFactory_iface;
        IActivationFactory_AddRef( *out );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IPowerManagerStatics ))
    {
        *out = &impl->IPowerManagerStatics_iface;
        IPowerManagerStatics_AddRef( *out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static HRESULT WINAPI activation_factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct power_manager_factory *impl = impl_from_IActivationFactory( iface );
    return power_manager_QueryInterface( impl, iid, out );
}

static HRESULT WINAPI statics_QueryInterface( IPowerManagerStatics *iface, REFIID iid, void **out )
{
    struct power_manager_factory *impl = impl_from_IPowerManagerStatics( iface );
    return power_manager_QueryInterface( impl, iid, out );
}

static ULONG WINAPI power_manager_AddRef( struct power_manager_factory *impl )
{
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "ref %lu.\n", ref );
    return ref;
}

static ULONG WINAPI power_manager_Release( struct power_manager_factory *impl )
{
    ULONG ref = InterlockedDecrement( &impl->ref );
    TRACE( "ref %lu.\n", ref );
    return ref;
}

static ULONG WINAPI activation_factory_AddRef( IActivationFactory *iface )
{
    struct power_manager_factory *impl = impl_from_IActivationFactory( iface );
    return power_manager_AddRef( impl );
}

static ULONG WINAPI activation_factory_Release( IActivationFactory *iface )
{
    struct power_manager_factory *impl = impl_from_IActivationFactory( iface );
    return power_manager_Release( impl );
}

static ULONG WINAPI statics_AddRef( IPowerManagerStatics *iface )
{
    struct power_manager_factory *impl = impl_from_IPowerManagerStatics( iface );
    return power_manager_AddRef( impl );
}

static ULONG WINAPI statics_Release( IPowerManagerStatics *iface )
{
    struct power_manager_factory *impl = impl_from_IPowerManagerStatics( iface );
    return power_manager_Release( impl );
}

static HRESULT WINAPI power_manager_GetIids( struct power_manager_factory *impl, ULONG *iid_count, IID **iids )
{
    TRACE( "iid_count %p, iids %p.\n", iid_count, iids );

    if (!iid_count || !iids) return E_POINTER;
    *iid_count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI activation_factory_GetIids( IActivationFactory *iface, ULONG *iid_count, IID **iids )
{
    struct power_manager_factory *impl = impl_from_IActivationFactory( iface );
    return power_manager_GetIids( impl, iid_count, iids );
}

static HRESULT WINAPI statics_GetIids( IPowerManagerStatics *iface, ULONG *iid_count, IID **iids )
{
    struct power_manager_factory *impl = impl_from_IPowerManagerStatics( iface );
    return power_manager_GetIids( impl, iid_count, iids );
}

static HRESULT WINAPI power_manager_GetRuntimeClassName( struct power_manager_factory *impl, HSTRING *class_name )
{
    TRACE( "class_name %p.\n", class_name );

    if (!class_name) return E_POINTER;
    return WindowsCreateString( runtime_class_name, ARRAY_SIZE( runtime_class_name ), class_name );
}

static HRESULT WINAPI activation_factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *class_name )
{
    struct power_manager_factory *impl = impl_from_IActivationFactory( iface );
    return power_manager_GetRuntimeClassName( impl, class_name );
}

static HRESULT WINAPI statics_GetRuntimeClassName( IPowerManagerStatics *iface, HSTRING *class_name )
{
    struct power_manager_factory *impl = impl_from_IPowerManagerStatics( iface );
    return power_manager_GetRuntimeClassName( impl, class_name );
}

static HRESULT WINAPI power_manager_GetTrustLevel( struct power_manager_factory *impl, TrustLevel *trust_level )
{
    TRACE( "trust_level %p.\n", trust_level );

    if (!trust_level) return E_POINTER;
    *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI activation_factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *trust_level )
{
    struct power_manager_factory *impl = impl_from_IActivationFactory( iface );
    return power_manager_GetTrustLevel( impl, trust_level );
}

static HRESULT WINAPI statics_GetTrustLevel( IPowerManagerStatics *iface, TrustLevel *trust_level )
{
    struct power_manager_factory *impl = impl_from_IPowerManagerStatics( iface );
    return power_manager_GetTrustLevel( impl, trust_level );
}

static HRESULT WINAPI activation_factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    TRACE( "instance %p.\n", instance );

    if (instance) *instance = NULL;
    return E_NOTIMPL;
}

static const struct IActivationFactoryVtbl activation_factory_vtbl =
{
    activation_factory_QueryInterface,
    activation_factory_AddRef,
    activation_factory_Release,
    /* IInspectable methods */
    activation_factory_GetIids,
    activation_factory_GetRuntimeClassName,
    activation_factory_GetTrustLevel,
    /* IActivationFactory methods */
    activation_factory_ActivateInstance,
};

/* IPowerManagerStatics methods, in Windows metadata vtable order */

static HRESULT WINAPI statics_get_EnergySaverStatus( IPowerManagerStatics *iface, EnergySaverStatus *value )
{
    TRACE( "value %p.\n", value );

    if (!value) return E_POINTER;
    *value = EnergySaverStatus_Off;
    return S_OK;
}

static HRESULT WINAPI statics_add_EnergySaverStatusChanged( IPowerManagerStatics *iface,
        __FIEventHandler_1_IInspectable *handler, EventRegistrationToken *token )
{
    return add_event_handler( handler, token );
}

static HRESULT WINAPI statics_remove_EnergySaverStatusChanged( IPowerManagerStatics *iface, EventRegistrationToken token )
{
    return remove_event_handler( token );
}

static HRESULT WINAPI statics_get_BatteryStatus( IPowerManagerStatics *iface, BatteryStatus *value )
{
    TRACE( "value %p.\n", value );

    if (!value) return E_POINTER;
    *value = BatteryStatus_NotPresent;
    return S_OK;
}

static HRESULT WINAPI statics_add_BatteryStatusChanged( IPowerManagerStatics *iface,
        __FIEventHandler_1_IInspectable *handler, EventRegistrationToken *token )
{
    return add_event_handler( handler, token );
}

static HRESULT WINAPI statics_remove_BatteryStatusChanged( IPowerManagerStatics *iface, EventRegistrationToken token )
{
    return remove_event_handler( token );
}

static HRESULT WINAPI statics_get_PowerSupplyStatus( IPowerManagerStatics *iface, PowerSupplyStatus *value )
{
    TRACE( "value %p.\n", value );

    if (!value) return E_POINTER;
    *value = PowerSupplyStatus_Adequate;
    return S_OK;
}

static HRESULT WINAPI statics_add_PowerSupplyStatusChanged( IPowerManagerStatics *iface,
        __FIEventHandler_1_IInspectable *handler, EventRegistrationToken *token )
{
    return add_event_handler( handler, token );
}

static HRESULT WINAPI statics_remove_PowerSupplyStatusChanged( IPowerManagerStatics *iface, EventRegistrationToken token )
{
    return remove_event_handler( token );
}

static HRESULT WINAPI statics_get_RemainingChargePercent( IPowerManagerStatics *iface, INT32 *value )
{
    TRACE( "value %p.\n", value );

    if (!value) return E_POINTER;
    *value = 100;
    return S_OK;
}

static HRESULT WINAPI statics_add_RemainingChargePercentChanged( IPowerManagerStatics *iface,
        __FIEventHandler_1_IInspectable *handler, EventRegistrationToken *token )
{
    return add_event_handler( handler, token );
}

static HRESULT WINAPI statics_remove_RemainingChargePercentChanged( IPowerManagerStatics *iface, EventRegistrationToken token )
{
    return remove_event_handler( token );
}

static HRESULT WINAPI statics_get_RemainingDischargeTime( IPowerManagerStatics *iface, TimeSpan *value )
{
    TRACE( "value %p.\n", value );

    if (!value) return E_POINTER;
    value->Duration = -1; /* unknown */
    return S_OK;
}

static HRESULT WINAPI statics_add_RemainingDischargeTimeChanged( IPowerManagerStatics *iface,
        __FIEventHandler_1_IInspectable *handler, EventRegistrationToken *token )
{
    return add_event_handler( handler, token );
}

static HRESULT WINAPI statics_remove_RemainingDischargeTimeChanged( IPowerManagerStatics *iface, EventRegistrationToken token )
{
    return remove_event_handler( token );
}

static const struct IPowerManagerStaticsVtbl statics_vtbl =
{
    statics_QueryInterface,
    statics_AddRef,
    statics_Release,
    /* IInspectable methods */
    statics_GetIids,
    statics_GetRuntimeClassName,
    statics_GetTrustLevel,
    /* IPowerManagerStatics methods */
    statics_get_EnergySaverStatus,
    statics_add_EnergySaverStatusChanged,
    statics_remove_EnergySaverStatusChanged,
    statics_get_BatteryStatus,
    statics_add_BatteryStatusChanged,
    statics_remove_BatteryStatusChanged,
    statics_get_PowerSupplyStatus,
    statics_add_PowerSupplyStatusChanged,
    statics_remove_PowerSupplyStatusChanged,
    statics_get_RemainingChargePercent,
    statics_add_RemainingChargePercentChanged,
    statics_remove_RemainingChargePercentChanged,
    statics_get_RemainingDischargeTime,
    statics_add_RemainingDischargeTimeChanged,
    statics_remove_RemainingDischargeTimeChanged,
};

static struct power_manager_factory factory =
{
    { &activation_factory_vtbl },
    { &statics_vtbl },
    1,
};

IActivationFactory *power_manager_statics_factory = &factory.IActivationFactory_iface;
