/* WinRT Windows.Globalization.ApplicationLanguages implementation
 *
 * Copyright 2021 Rémi Bernon for CodeWeavers
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

WINE_DEFAULT_DEBUG_CHANNEL(locale);

/* CoreCLR's ComWrappers shader reads IInspectable::GetIids output as an array
 * of ComInterfaceEntry pairs (vtable-first layout: struct { void** Vtable;
 * Guid* IID; }). */
struct com_interface_entry
{
    void **vtable;
    IID   *iid;
};

/* Defined further below; referenced by the cached GetIids pair array. */
static const struct IActivationFactoryVtbl application_languages_activation_factory_vtbl;
static const struct IApplicationLanguagesStaticsVtbl application_languages_statics_vtbl;
static const struct IApplicationLanguagesVtbl application_languages_instance_vtbl;

typedef struct IApplicationLanguagesVtbl IApplicationLanguages;
typedef struct IApplicationLanguagesVtbl
{
    const struct IApplicationLanguagesVtbl *lpVtbl;
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IApplicationLanguages *This, REFIID riid, void **ppvObject);
    ULONG (STDMETHODCALLTYPE *AddRef)(IApplicationLanguages *This);
    ULONG (STDMETHODCALLTYPE *Release)(IApplicationLanguages *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(IApplicationLanguages *This, ULONG *iid_count, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(IApplicationLanguages *This, HSTRING *class_name);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(IApplicationLanguages *This, TrustLevel *trust_level);
    HRESULT (STDMETHODCALLTYPE *get_ManagedLanguages)(IApplicationLanguages *This, IVectorView_HSTRING **value);
    HRESULT (STDMETHODCALLTYPE *get_LayoutLanguages)(IApplicationLanguages *This, IVectorView_HSTRING **value);
    HRESULT (STDMETHODCALLTYPE *get_StandardLanguages)(IApplicationLanguages *This, IVectorView_HSTRING **value);
    HRESULT (STDMETHODCALLTYPE *add_LanguageProfileChanged)(IApplicationLanguages *This, IUnknown *handler,
                                                           EventRegistrationToken *cookie);
    HRESULT (STDMETHODCALLTYPE *remove_LanguageProfileChanged)(IApplicationLanguages *This, EventRegistrationToken cookie);
} IApplicationLanguages;

#define IApplicationLanguages_QueryInterface(self,riid,ppv) (self)->lpVtbl->QueryInterface(self,riid,ppv)
#define IApplicationLanguages_AddRef(self) (self)->lpVtbl->AddRef(self)
#define IApplicationLanguages_Release(self) (self)->lpVtbl->Release(self)

struct application_languages
{
    IActivationFactory IActivationFactory_iface;
    IApplicationLanguagesStatics IApplicationLanguagesStatics_iface;
    LONG ref;
    struct com_interface_entry *iids_cache; /* stable GetIids pair array, see factory_GetIids */
    ULONG iids_count;
};

static inline struct application_languages *impl_application_languages_from_IActivationFactory(IActivationFactory *iface)
{
    return CONTAINING_RECORD(iface, struct application_languages, IActivationFactory_iface);
}

static HRESULT STDMETHODCALLTYPE application_languages_activation_factory_QueryInterface(
        IActivationFactory *iface, REFIID iid, void **out)
{
    struct application_languages *languages = impl_application_languages_from_IActivationFactory(iface);

    ERR("GLOBALIZATION: QueryInterface iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_IUnknown) ||
        IsEqualGUID(iid, &IID_IInspectable) ||
        IsEqualGUID(iid, &IID_IAgileObject) ||
        IsEqualGUID(iid, &IID_IActivationFactory))
    {
        IUnknown_AddRef(iface);
        *out = iface;
        return S_OK;
    }
    else if (IsEqualGUID(iid, &IID_IApplicationLanguagesStatics))
    {
        IUnknown_AddRef(&languages->IApplicationLanguagesStatics_iface);
        *out = &languages->IApplicationLanguagesStatics_iface;
        return S_OK;
    }

    FIXME("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid));
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE application_languages_activation_factory_AddRef(IActivationFactory *iface)
{
    struct application_languages *languages = impl_application_languages_from_IActivationFactory(iface);
    ULONG ref = InterlockedIncrement(&languages->ref);
    TRACE("iface %p, ref %lu.\n", iface, ref);
    return ref;
}

static ULONG STDMETHODCALLTYPE application_languages_activation_factory_Release(IActivationFactory *iface)
{
    struct application_languages *languages = impl_application_languages_from_IActivationFactory(iface);
    ULONG ref = InterlockedDecrement(&languages->ref);
    TRACE("iface %p, ref %lu.\n", iface, ref);
    return ref;
}

static HRESULT STDMETHODCALLTYPE application_languages_activation_factory_GetIids(
        IActivationFactory *iface, ULONG *iid_count, IID **iids)
{
    /* CoreCLR's ComWrappers shader reads the output as a ComInterfaceEntry
     * pair array; E_NOTIMPL makes it fall back to a single-entry IInspectable
     * vtable and the IApplicationLanguagesStatics (get_Current) RCW decodes
     * its slots off the wrong ComVirtualMethodData. One stable pair array per
     * singleton: {factory vtbl, IActivationFactory} + {statics vtbl,
     * IApplicationLanguagesStatics}. */
    struct application_languages *impl = impl_application_languages_from_IActivationFactory(iface);
    TRACE("iface %p, iid_count %p, iids %p.\n", iface, iid_count, iids);

    if (iid_count) *iid_count = 2; /* filled on the (NULL, NULL) probe itself */
    if (!impl->iids_cache)
    {
        struct com_interface_entry entries[] =
        {
            { (void **)&application_languages_activation_factory_vtbl, (IID *)&IID_IActivationFactory },
            { (void **)&application_languages_statics_vtbl, (IID *)&IID_IApplicationLanguagesStatics },
        };
        struct com_interface_entry *array = CoTaskMemAlloc(sizeof(entries));
        if (!array) return E_OUTOFMEMORY;
        memcpy(array, entries, sizeof(entries));
        impl->iids_cache = array;
        impl->iids_count = ARRAY_SIZE(entries);
    }

    if (iid_count) *iid_count = impl->iids_count;
    if (iids) *iids = (IID *)impl->iids_cache;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE application_languages_activation_factory_GetRuntimeClassName(
        IActivationFactory *iface, HSTRING *class_name)
{
    FIXME("iface %p, class_name %p stub!\n", iface, class_name);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE application_languages_activation_factory_GetTrustLevel(
        IActivationFactory *iface, TrustLevel *trust_level)
{
    FIXME("iface %p, trust_level %p stub!\n", iface, trust_level);
    return E_NOTIMPL;
}

struct language_profile_changed_entry
{
    EventRegistrationToken token;
    IUnknown *handler;
};

struct application_languages_instance
{
    IApplicationLanguages IApplicationLanguages_iface;
    LONG ref;
    ULONG64 next_token;
    struct language_profile_changed_entry entries[16];
};

static HRESULT STDMETHODCALLTYPE application_languages_activation_factory_ActivateInstance(
        IActivationFactory *iface, IInspectable **instance)
{
    struct application_languages_instance *impl;

    TRACE("iface %p, instance %p.\n", iface, instance);

    if (!(impl = LocalAlloc(LPTR, sizeof(*impl)))) return E_OUTOFMEMORY;
    impl->IApplicationLanguages_iface.lpVtbl = &application_languages_instance_vtbl;
    impl->ref = 1;
    impl->next_token = 1;
    *instance = (IInspectable *)&impl->IApplicationLanguages_iface;
    return S_OK;
}

static const struct IActivationFactoryVtbl application_languages_activation_factory_vtbl =
{
    /* IUnknown methods */
    application_languages_activation_factory_QueryInterface,
    application_languages_activation_factory_AddRef,
    application_languages_activation_factory_Release,
    /* IInspectable methods */
    application_languages_activation_factory_GetIids,
    application_languages_activation_factory_GetRuntimeClassName,
    application_languages_activation_factory_GetTrustLevel,
    /* IActivationFactory methods */
    application_languages_activation_factory_ActivateInstance,
};

DEFINE_IINSPECTABLE(application_languages_statics, IApplicationLanguagesStatics, struct application_languages, IActivationFactory_iface)

static HRESULT STDMETHODCALLTYPE application_languages_statics_get_PrimaryLanguageOverride(
        IApplicationLanguagesStatics *iface, HSTRING *value)
{
    ERR("GLOBALIZATION: get_PrimaryLanguageOverride iface %p, value %p\n", iface, value);
    if (!value) return E_POINTER;
    return WindowsCreateString(L"", 0, value);
}

static HRESULT STDMETHODCALLTYPE application_languages_statics_put_PrimaryLanguageOverride(
        IApplicationLanguagesStatics *iface, HSTRING value)
{
    ERR("GLOBALIZATION: put_PrimaryLanguageOverride iface %p, value %s\n", iface, wine_dbgstr_hstring(value));
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE application_languages_statics_get_Languages(
        IApplicationLanguagesStatics *iface, IVectorView_HSTRING **value)
{
    WCHAR locale[LOCALE_NAME_MAX_LENGTH];
    HSTRING hstring;
    HRESULT hr;

    ERR("GLOBALIZATION: get_Languages iface %p, out %p\n", iface, value);

    if (!value) return E_POINTER;

    if (!GetUserDefaultLocaleName(locale, LOCALE_NAME_MAX_LENGTH) || !locale[0])
        wcscpy(locale, L"en-US");

    if (FAILED(hr = WindowsCreateString(locale, wcslen(locale), &hstring)))
        return hr;

    hr = hstring_vector_create(&hstring, 1, value);
    ERR("GLOBALIZATION: get_Languages returning locale %s, hr %#lx, view %p\n", wine_dbgstr_w(locale), hr, *value);
    return hr;
}

static HRESULT STDMETHODCALLTYPE application_languages_statics_get_ManifestLanguages(
        IApplicationLanguagesStatics *iface, IVectorView_HSTRING **value)
{
    WCHAR locale[LOCALE_NAME_MAX_LENGTH];
    HSTRING hstring;
    HRESULT hr;

    ERR("GLOBALIZATION: get_ManifestLanguages iface %p, value %p\n", iface, value);

    if (!value) return E_POINTER;

    if (!GetUserDefaultLocaleName(locale, LOCALE_NAME_MAX_LENGTH))
        wcscpy(locale, L"en-US");

    if (FAILED(hr = WindowsCreateString(locale, wcslen(locale), &hstring)))
        return hr;

    return hstring_vector_create(&hstring, 1, value);
}

static const struct IApplicationLanguagesStaticsVtbl application_languages_statics_vtbl =
{
    /* IUnknown methods */
    application_languages_statics_QueryInterface,
    application_languages_statics_AddRef,
    application_languages_statics_Release,
    /* IInspectable methods */
    application_languages_statics_GetIids,
    application_languages_statics_GetRuntimeClassName,
    application_languages_statics_GetTrustLevel,
    /* IApplicationLanguagesStatics methods */
    application_languages_statics_get_PrimaryLanguageOverride,
    application_languages_statics_put_PrimaryLanguageOverride,
    application_languages_statics_get_Languages,
    application_languages_statics_get_ManifestLanguages
};

/*
 *
 * IApplicationLanguages (instance face, served by ActivateInstance)
 *
 */

static const GUID IID_IApplicationLanguages_glob =
    { 0x94ea2b94, 0xe9cc, 0x49e0, { 0xc0, 0xff, 0xee, 0x64, 0xca, 0x8f, 0x5b, 0x90 } };

static inline struct application_languages_instance *application_languages_instance_from_IApplicationLanguages(
        IApplicationLanguages *iface)
{
    return CONTAINING_RECORD(iface, struct application_languages_instance, IApplicationLanguages_iface);
}

static HRESULT WINAPI application_languages_instance_QueryInterface(
        IApplicationLanguages *iface, REFIID iid, void **out)
{
    struct application_languages_instance *impl = application_languages_instance_from_IApplicationLanguages(iface);

    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_IUnknown) ||
        IsEqualGUID(iid, &IID_IInspectable) ||
        IsEqualGUID(iid, &IID_IAgileObject) ||
        IsEqualGUID(iid, &IID_IApplicationLanguages_glob))
    {
        IApplicationLanguages_AddRef(iface);
        *out = impl;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI application_languages_instance_AddRef(IApplicationLanguages *iface)
{
    struct application_languages_instance *impl = application_languages_instance_from_IApplicationLanguages(iface);
    return InterlockedIncrement(&impl->ref);
}

static ULONG WINAPI application_languages_instance_Release(IApplicationLanguages *iface)
{
    struct application_languages_instance *impl = application_languages_instance_from_IApplicationLanguages(iface);
    ULONG ref = InterlockedDecrement(&impl->ref);

    if (!ref)
    {
        unsigned int i;
        for (i = 0; i < ARRAYSIZE(impl->entries); i++)
            if (impl->entries[i].handler) IUnknown_Release(impl->entries[i].handler);
        LocalFree(impl);
    }
    return ref;
}

static HRESULT WINAPI application_languages_instance_GetIids(
        IApplicationLanguages *iface, ULONG *iid_count, IID **iids)
{
    *iid_count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI application_languages_instance_GetRuntimeClassName(
        IApplicationLanguages *iface, HSTRING *class_name)
{
    return WindowsCreateString(L"Windows.Globalization.ApplicationLanguages",
                               ARRAYSIZE(L"Windows.Globalization.ApplicationLanguages"), class_name);
}

static HRESULT WINAPI application_languages_instance_GetTrustLevel(
        IApplicationLanguages *iface, TrustLevel *trust_level)
{
    *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT application_languages_instance_get_languages(IApplicationLanguages *iface, IVectorView_HSTRING **value)
{
    WCHAR locale[LOCALE_NAME_MAX_LENGTH];
    HSTRING hstring;
    HRESULT hr;

    if (!value) return E_POINTER;
    if (!GetUserDefaultLocaleName(locale, LOCALE_NAME_MAX_LENGTH) || !locale[0])
        wcscpy(locale, L"en-US");
    if (FAILED(hr = WindowsCreateString(locale, wcslen(locale), &hstring)))
        return hr;
    return hstring_vector_create(&hstring, 1, value);
}

static HRESULT WINAPI application_languages_instance_get_ManagedLanguages(
        IApplicationLanguages *iface, IVectorView_HSTRING **value)
{
    TRACE("iface %p, value %p.\n", iface, value);
    return application_languages_instance_get_languages(iface, value);
}

static HRESULT WINAPI application_languages_instance_get_LayoutLanguages(
        IApplicationLanguages *iface, IVectorView_HSTRING **value)
{
    TRACE("iface %p, value %p.\n", iface, value);
    return application_languages_instance_get_languages(iface, value);
}

static HRESULT WINAPI application_languages_instance_get_StandardLanguages(
        IApplicationLanguages *iface, IVectorView_HSTRING **value)
{
    TRACE("iface %p, value %p.\n", iface, value);
    return application_languages_instance_get_languages(iface, value);
}

static HRESULT WINAPI application_languages_instance_add_LanguageProfileChanged(
        IApplicationLanguages *iface, IUnknown *handler, EventRegistrationToken *cookie)
{
    struct application_languages_instance *impl = application_languages_instance_from_IApplicationLanguages(iface);
    unsigned int i;

    TRACE("iface %p, handler %p, cookie %p.\n", iface, handler, cookie);

    if (!cookie) return E_POINTER;
    for (i = 0; i < ARRAYSIZE(impl->entries); i++)
    {
        if (!impl->entries[i].handler)
        {
            impl->entries[i].handler = handler;
            if (handler) IUnknown_AddRef(handler);
            cookie->value = impl->next_token++;
            impl->entries[i].token.value = cookie->value;
            return S_OK;
        }
    }
    return E_OUTOFMEMORY;
}

static HRESULT WINAPI application_languages_instance_remove_LanguageProfileChanged(
        IApplicationLanguages *iface, EventRegistrationToken cookie)
{
    struct application_languages_instance *impl = application_languages_instance_from_IApplicationLanguages(iface);
    unsigned int i;

    TRACE("iface %p, cookie %s.\n", iface, wine_dbgstr_longlong(cookie.value));

    for (i = 0; i < ARRAYSIZE(impl->entries); i++)
    {
        if (impl->entries[i].handler && impl->entries[i].token.value == cookie.value)
        {
            IUnknown_Release(impl->entries[i].handler);
            impl->entries[i].handler = NULL;
            break;
        }
    }
    return S_OK;
}

static const struct IApplicationLanguagesVtbl application_languages_instance_vtbl =
{
    0, /* lpVtbl */
    /* IUnknown methods */
    application_languages_instance_QueryInterface,
    application_languages_instance_AddRef,
    application_languages_instance_Release,
    /* IInspectable methods */
    application_languages_instance_GetIids,
    application_languages_instance_GetRuntimeClassName,
    application_languages_instance_GetTrustLevel,
    /* IApplicationLanguages methods */
    application_languages_instance_get_ManagedLanguages,
    application_languages_instance_get_LayoutLanguages,
    application_languages_instance_get_StandardLanguages,
    application_languages_instance_add_LanguageProfileChanged,
    application_languages_instance_remove_LanguageProfileChanged
};

static struct application_languages application_languages =
{
    {&application_languages_activation_factory_vtbl},
    {&application_languages_statics_vtbl},
    1
};

IActivationFactory *application_languages_factory = &application_languages.IActivationFactory_iface;
