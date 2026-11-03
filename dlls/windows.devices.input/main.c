/*
 * Windows.Devices.Input.PenDevice activation factory
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

#include "initguid.h"
#include "private.h"

WINE_DEFAULT_DEBUG_CHANNEL(devicesinput);

/*
 * Windows.Devices.Input.PenDevice: the app input stack resolves the class on its pointer/pen
 * path (gate50: one 'Failed to find library' line from client 0468; the probe retry-loops on the
 * REGDB_E_CLASSNOTREG answer combase gives for an unregistered class).
 *
 * Metadata (windows-rs projection, crates/libs/windows/src/Windows/Devices/Input/mod.rs):
 *   :118 IPenDeviceStatics {9dfbbe01-0966-5180-bcb4-b85060e39479}, one slot after the
 *        IInspectable prologue: GetFromPointerId(uint32 pointerid, PenDevice**) (:127) - the
 *        lookup is synchronous; there is no async statics surface on this class, and the
 *        GetPointerDeviceForPen / GetPointerDeviceForHidEndpointId spellings some callers use
 *        are not in the metadata, so no IAsyncOperation is fabricated here.
 *   :93  IPenDevice {31856eba-a738-5a8c-b8f6-f97ef68d18ef}, one property: PenId -> GUID (:102).
 *   :104 IPenDevice2 {0207d327-7fb8-5566-8c34-f8342037b7f9}, SimpleHapticsController (:114).
 *   :547 runtime class name "Windows.Devices.Input.PenDevice".
 *
 * Documented contract of the implemented member (learn.microsoft.com "Pen Device. Get From
 * Pointer Id(UInt32) Method", Windows.Devices.Input): "Obtains a PenDevice object associated
 * with the specified pointer, if available."  /  "Returns null if the pointer input does not
 * correspond to the pen input, or if the pen does not support a unique ID."
 *
 * A Wine prefix has no pen HID stack to enumerate, so every pointer id is a pointer input that
 * "does not correspond to the pen input": the documented answer is a null PenDevice, returned
 * with S_OK.  Returning S_OK + null is also the no-throw answer the app stack expects from this
 * probe; an E_FAIL here would originate a WinRT language exception on the caller's thread, which
 * is exactly the failfast class of failure this work avoids.
 *
 * Not implemented, with reason:
 *   - IPenDevice (PenId) as an object: an instance only exists as the result of a successful
 *     GetFromPointerId, which always yields null here, so there is nothing to hand out.  The
 *     statics factory answers E_NOINTERFACE for the instance interface, the honest answer.
 *   - IPenDevice2 (SimpleHapticsController): its retval type is the Windows.Devices.Haptics
 *     surface, which this tree has no idl for, and the class is not probed.
 *   - Windows.Devices.Input.PenIlbrInformation: the fetched windows-rs projection of this
 *     namespace has no PenIlbr entry at all, so its interface IID cannot be verified here; the
 *     class is left unimplemented rather than guessed (IID rule: never from memory).
 */

struct pen_device_statics
{
    IActivationFactory IActivationFactory_iface;
    IPenDeviceStatics IPenDeviceStatics_iface;
    LONG ref;
};

static inline struct pen_device_statics *impl_from_IActivationFactory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct pen_device_statics, IActivationFactory_iface );
}

static HRESULT WINAPI factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct pen_device_statics *impl = impl_from_IActivationFactory( iface );

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

    if (IsEqualGUID( iid, &IID_IPenDeviceStatics ))
    {
        *out = &impl->IPenDeviceStatics_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    /* The instance interfaces (IPenDevice, IPenDevice2) are not answerable from a statics
     * factory: a PenDevice object only comes out of a successful GetFromPointerId. */
    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG WINAPI factory_AddRef( IActivationFactory *iface )
{
    struct pen_device_statics *impl = impl_from_IActivationFactory( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );

    TRACE( "iface %p, ref %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI factory_Release( IActivationFactory *iface )
{
    struct pen_device_statics *impl = impl_from_IActivationFactory( iface );
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
    return WindowsCreateString( RuntimeClass_Windows_Devices_Input_PenDevice,
                                lstrlenW( RuntimeClass_Windows_Devices_Input_PenDevice ), class_name );
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

    /* PenDevice has no activatable constructor in the metadata; instances come from
     * IPenDeviceStatics::GetFromPointerId only. */
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

DEFINE_IINSPECTABLE( statics, IPenDeviceStatics, struct pen_device_statics, IActivationFactory_iface )

static HRESULT WINAPI statics_GetFromPointerId( IPenDeviceStatics *iface, UINT32 pointer_id, IPenDevice **result )
{
    TRACE( "iface %p, pointer_id %u, result %p.\n", iface, pointer_id, result );

    if (!result) return E_POINTER;

    /* No pen HID stack in the prefix: the documented answer for a pointer that does not
     * correspond to pen input is a null PenDevice with a success code. */
    *result = NULL;
    return S_OK;
}

static const struct IPenDeviceStaticsVtbl statics_vtbl =
{
    statics_QueryInterface,
    statics_AddRef,
    statics_Release,
    statics_GetIids,
    statics_GetRuntimeClassName,
    statics_GetTrustLevel,
    statics_GetFromPointerId,
};

static struct pen_device_statics pen_device_statics =
{
    { &factory_vtbl },
    { &statics_vtbl },
    1,
};

HRESULT WINAPI DllGetActivationFactory( HSTRING classid, IActivationFactory **factory )
{
    const WCHAR *name = WindowsGetStringRawBuffer( classid, NULL );

    TRACE( "classid %s, factory %p.\n", debugstr_hstring( classid ), factory );

    if (!factory) return E_POINTER;
    *factory = NULL;

    if (!wcscmp( name, RuntimeClass_Windows_Devices_Input_PenDevice ))
        IActivationFactory_QueryInterface( &pen_device_statics.IActivationFactory_iface,
                                           &IID_IActivationFactory, (void **)factory );

    if (*factory) return S_OK;
    return E_NOINTERFACE;
}

HRESULT WINAPI DllCanUnloadNow( void )
{
    return S_FALSE;
}

HRESULT WINAPI DllGetClassObject( REFCLSID clsid, REFIID riid, void **out )
{
    FIXME( "clsid %s, riid %s, out %p stub!\n", debugstr_guid( clsid ), debugstr_guid( riid ), out );
    return CLASS_E_CLASSNOTAVAILABLE;
}
