/* WinRT Windows.UI.ViewManagement.AccessibilitySettings Implementation
 *
 * Copyright 2025 Zhiyi Zhang for CodeWeavers
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

#include <string.h>

#include "private.h"
#include "objbase.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(ui);

struct accessibilitysettings
{
    IAccessibilitySettings IAccessibilitySettings_iface;
    LONG ref;
};

static inline struct accessibilitysettings *impl_from_IAccessibilitySettings(IAccessibilitySettings *iface)
{
    return CONTAINING_RECORD(iface, struct accessibilitysettings, IAccessibilitySettings_iface);
}

static HRESULT WINAPI accessibilitysettings_QueryInterface(IAccessibilitySettings *iface,
                                                           REFIID iid, void **out)
{
    struct accessibilitysettings *impl = impl_from_IAccessibilitySettings(iface);

    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_IUnknown)
        || IsEqualGUID(iid, &IID_IInspectable)
        || IsEqualGUID(iid, &IID_IAgileObject)
        || IsEqualGUID(iid, &IID_IAccessibilitySettings))
    {
        *out = &impl->IAccessibilitySettings_iface;
        IAccessibilitySettings_AddRef(&impl->IAccessibilitySettings_iface);
        return S_OK;
    }

    *out = NULL;
    FIXME("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid));
    return E_NOINTERFACE;
}

static ULONG WINAPI accessibilitysettings_AddRef(IAccessibilitySettings *iface)
{
    struct accessibilitysettings *impl = impl_from_IAccessibilitySettings(iface);
    ULONG ref = InterlockedIncrement(&impl->ref);
    TRACE("iface %p, ref %lu.\n", iface, ref);
    return ref;
}

static ULONG WINAPI accessibilitysettings_Release(IAccessibilitySettings *iface)
{
    struct accessibilitysettings *impl = impl_from_IAccessibilitySettings(iface);
    ULONG ref = InterlockedDecrement(&impl->ref);

    TRACE("iface %p, ref %lu.\n", iface, ref);

    if (!ref)
        free(impl);
    return ref;
}

static HRESULT WINAPI accessibilitysettings_GetIids(IAccessibilitySettings *iface, ULONG *iid_count,
                                                    IID **iids)
{
    static const IID iid_array[] =
    {
        IID_IAccessibilitySettings,
    };
    IID *array = CoTaskMemAlloc( sizeof(iid_array) );

    if (!array) return E_OUTOFMEMORY;
    memcpy( array, iid_array, sizeof(iid_array) );
    *iid_count = ARRAY_SIZE(iid_array);
    *iids = array;
    return S_OK;
}

static HRESULT WINAPI accessibilitysettings_GetRuntimeClassName(IAccessibilitySettings *iface,
                                                                HSTRING *class_name)
{
    FIXME("iface %p, class_name %p stub!\n", iface, class_name);
    return E_NOTIMPL;
}

static HRESULT WINAPI accessibilitysettings_GetTrustLevel(IAccessibilitySettings *iface,
                                                          TrustLevel *trust_level)
{
    FIXME("iface %p, trust_level %p stub!\n", iface, trust_level);
    return E_NOTIMPL;
}

static HRESULT WINAPI accessibilitysettings_get_HighContrast(IAccessibilitySettings *iface,
                                                             boolean *value)
{
    HIGHCONTRASTW high_contrast = {.cbSize = sizeof(high_contrast)};

    TRACE("iface %p, value %p.\n", iface, value);

    if (!SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(high_contrast), &high_contrast, 0))
        return E_FAIL;

    *value = !!(high_contrast.dwFlags & HCF_HIGHCONTRASTON);
    return S_OK;
}

static HRESULT WINAPI accessibilitysettings_get_HighContrastScheme(IAccessibilitySettings *iface,
                                                                   HSTRING *value)
{
    FIXME("iface %p, value %p stub!\n", iface, value);
    return E_NOTIMPL;
}

static HRESULT WINAPI accessibilitysettings_add_HighContrastChanged(IAccessibilitySettings *iface,
                                                                    ITypedEventHandler_AccessibilitySettings_IInspectable *handler,
                                                                    EventRegistrationToken *cookie)
{
    FIXME("iface %p, handler %p, cookie %p stub!\n", iface, handler, cookie);
    return E_NOTIMPL;
}

static HRESULT WINAPI accessibilitysettings_remove_HighContrastChanged(IAccessibilitySettings *iface,
                                                                       EventRegistrationToken cookie)
{
    FIXME("iface %p, cookie %I64x stub!\n", iface, cookie.value);
    return E_NOTIMPL;
}

static const struct IAccessibilitySettingsVtbl accessibilitysettings_vtbl =
{
    accessibilitysettings_QueryInterface,
    accessibilitysettings_AddRef,
    accessibilitysettings_Release,
    /* IInspectable methods */
    accessibilitysettings_GetIids,
    accessibilitysettings_GetRuntimeClassName,
    accessibilitysettings_GetTrustLevel,
    /* IAccessibilitySettings methods */
    accessibilitysettings_get_HighContrast,
    accessibilitysettings_get_HighContrastScheme,
    accessibilitysettings_add_HighContrastChanged,
    accessibilitysettings_remove_HighContrastChanged,
};

struct factory
{
    IActivationFactory IActivationFactory_iface;
    IAccessibilitySettings IAccessibilitySettings_iface;
    LONG ref;
};

static inline struct factory *impl_from_IActivationFactory(IActivationFactory *iface)
{
    return CONTAINING_RECORD(iface, struct factory, IActivationFactory_iface);
}

static HRESULT WINAPI factory_QueryInterface(IActivationFactory *iface, REFIID iid, void **out)
{
    struct factory *impl = impl_from_IActivationFactory(iface);

    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_IUnknown)
        || IsEqualGUID(iid, &IID_IInspectable)
        || IsEqualGUID(iid, &IID_IAgileObject)
        || IsEqualGUID(iid, &IID_IActivationFactory))
    {
        *out = &impl->IActivationFactory_iface;
        IInspectable_AddRef(*out);
        return S_OK;
    }

    *out = NULL;
    /* Windows aggregates the class default interface (IAccessibilitySettings) on the factory object
     * (CsWinRT probes the factory for the class interfaces). The factory is not an
     * instance; instance method calls fail. */
    if (IsEqualGUID( iid, &IID_IAccessibilitySettings ))
    {
        *out = &impl->IAccessibilitySettings_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    FIXME("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid));
    return E_NOINTERFACE;
}

static ULONG WINAPI factory_AddRef(IActivationFactory *iface)
{
    struct factory *impl = impl_from_IActivationFactory(iface);
    ULONG ref = InterlockedIncrement(&impl->ref);
    TRACE("iface %p, ref %lu.\n", iface, ref);
    return ref;
}

static ULONG WINAPI factory_Release(IActivationFactory *iface)
{
    struct factory *impl = impl_from_IActivationFactory(iface);
    ULONG ref = InterlockedDecrement(&impl->ref);
    TRACE("iface %p, ref %lu.\n", iface, ref);
    return ref;
}

static HRESULT WINAPI factory_GetIids(IActivationFactory *iface, ULONG *iid_count, IID **iids)
{
    static const IID factory_iids[] =
    {
        IID_IActivationFactory,
        IID_IAccessibilitySettings,
    };
    IID *array = CoTaskMemAlloc( sizeof(factory_iids) );

    if (!array) return E_OUTOFMEMORY;
    memcpy( array, factory_iids, sizeof(factory_iids) );
    *iid_count = ARRAY_SIZE(factory_iids);
    *iids = array;
    return S_OK;
}

static HRESULT WINAPI factory_GetRuntimeClassName(IActivationFactory *iface, HSTRING *class_name)
{
    return WindowsCreateString( RuntimeClass_Windows_UI_ViewManagement_AccessibilitySettings, lstrlenW( RuntimeClass_Windows_UI_ViewManagement_AccessibilitySettings ), class_name );
}

static HRESULT WINAPI factory_GetTrustLevel(IActivationFactory *iface, TrustLevel *trust_level)
{
    **trust_level = FullTrust;
    return S_OK;
}

static HRESULT WINAPI factory_ActivateInstance(IActivationFactory *iface, IInspectable **instance)
{
    struct accessibilitysettings *impl;

    TRACE("iface %p, instance %p.\n", iface, instance);

    if (!(impl = calloc(1, sizeof(*impl))))
    {
        *instance = NULL;
        return E_OUTOFMEMORY;
    }

    impl->IAccessibilitySettings_iface.lpVtbl = &accessibilitysettings_vtbl;
    impl->ref = 1;

    *instance = (IInspectable *)&impl->IAccessibilitySettings_iface;
    return S_OK;
}

static const struct IActivationFactoryVtbl factory_vtbl =
{
    factory_QueryInterface,
    factory_AddRef,
    factory_Release,
    /* IInspectable methods */
    factory_GetIids,
    factory_GetRuntimeClassName,
    factory_GetTrustLevel,
    /* IActivationFactory methods */
    factory_ActivateInstance,
};


/* Windows aggregates the class default interface (IAccessibilitySettings) on the factory object;
 * the factory is not an instance: method calls fail. */
DEFINE_IINSPECTABLE_( factory_def, IAccessibilitySettings, struct factory, impl_from_IAccessibilitySettings_factory_def,
                      IAccessibilitySettings_iface, &impl->IActivationFactory_iface )

static HRESULT WINAPI factory_def_get_HighContrast( IAccessibilitySettings *iface, boolean *value ) { return E_FAIL; }
static HRESULT WINAPI factory_def_get_HighContrastScheme( IAccessibilitySettings *iface, HSTRING *value ) { return E_FAIL; }
static HRESULT WINAPI factory_def_add_HighContrastChanged( IAccessibilitySettings *iface, __FITypedEventHandler_2_Windows__CUI__CViewManagement__CAccessibilitySettings_IInspectable *handler, EventRegistrationToken *cookie ) { return E_FAIL; }
static HRESULT WINAPI factory_def_remove_HighContrastChanged( IAccessibilitySettings *iface, EventRegistrationToken cookie ) { return E_FAIL; }

static const struct IAccessibilitySettingsVtbl factory_def_vtbl =
{
    factory_def_QueryInterface,
    factory_def_AddRef,
    factory_def_Release,
    factory_def_GetIids,
    factory_def_GetRuntimeClassName,
    factory_def_GetTrustLevel,
    factory_def_get_HighContrast,
    factory_def_get_HighContrastScheme,
    factory_def_add_HighContrastChanged,
    factory_def_remove_HighContrastChanged,
};

static struct factory factory =
{
    {&factory_vtbl},
    {&factory_def_vtbl}, 1,
};

IActivationFactory *accessibilitysettings_factory = &factory.IActivationFactory_iface;
