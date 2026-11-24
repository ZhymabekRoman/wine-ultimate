/* WinRT Windows.Globalization.Language, GlobalizationPreferences, and Fonts implementation
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

struct windows_globalization
{
    IActivationFactory IActivationFactory_iface;
    IGlobalizationPreferencesStatics IGlobalizationPreferencesStatics_iface;
    LONG ref;
};

struct language_factory
{
    IActivationFactory IActivationFactory_iface;
    ILanguageFactory ILanguageFactory_iface;
    ILanguageStatics ILanguageStatics_iface;
    LONG ref;
};

struct language
{
    ILanguage ILanguage_iface;
    ILanguage2 ILanguage2_iface;
    LONG ref;
    WCHAR name[LOCALE_NAME_MAX_LENGTH];
};

static inline struct windows_globalization *impl_from_IActivationFactory(IActivationFactory *iface)
{
    return CONTAINING_RECORD(iface, struct windows_globalization, IActivationFactory_iface);
}

static inline struct language_factory *impl_language_factory_from_IActivationFactory(IActivationFactory *iface)
{
    return CONTAINING_RECORD(iface, struct language_factory, IActivationFactory_iface);
}

static inline struct windows_globalization *impl_from_IGlobalizationPreferencesStatics(IGlobalizationPreferencesStatics *iface)
{
    return CONTAINING_RECORD(iface, struct windows_globalization, IGlobalizationPreferencesStatics_iface);
}

static inline struct language_factory *impl_from_ILanguageFactory(ILanguageFactory *iface)
{
    return CONTAINING_RECORD(iface, struct language_factory, ILanguageFactory_iface);
}

static inline struct language_factory *impl_from_ILanguageStatics(ILanguageStatics *iface)
{
    return CONTAINING_RECORD(iface, struct language_factory, ILanguageStatics_iface);
}

static inline struct language *impl_from_ILanguage(ILanguage *iface)
{
    return CONTAINING_RECORD(iface, struct language, ILanguage_iface);
}

static HRESULT STDMETHODCALLTYPE language_QueryInterface(
        ILanguage *iface, REFIID iid, void **out)
{
    struct language *language = impl_from_ILanguage(iface);

    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_IUnknown) ||
        IsEqualGUID(iid, &IID_IInspectable) ||
        IsEqualGUID(iid, &IID_IAgileObject) ||
        IsEqualGUID(iid, &IID_ILanguage))
    {
        IUnknown_AddRef(iface);
        *out = iface;
        return S_OK;
    }
    else if (IsEqualGUID(iid, &IID_ILanguage2))
    {
        IUnknown_AddRef(&language->ILanguage2_iface);
        *out = &language->ILanguage2_iface;
        return S_OK;
    }

    FIXME("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid));
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE language_AddRef(
        ILanguage *iface)
{
    struct language *language = impl_from_ILanguage(iface);
    ULONG ref = InterlockedIncrement(&language->ref);
    TRACE("iface %p, ref %lu.\n", iface, ref);
    return ref;
}

static ULONG STDMETHODCALLTYPE language_Release(
        ILanguage *iface)
{
    struct language *language = impl_from_ILanguage(iface);
    ULONG ref = InterlockedDecrement(&language->ref);

    TRACE("iface %p, ref %lu.\n", iface, ref);

    if (!ref)
        free(language);

    return ref;
}

static HRESULT STDMETHODCALLTYPE language_GetIids(
        ILanguage *iface, ULONG *iid_count, IID **iids)
{
    FIXME("iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE language_GetRuntimeClassName(
        ILanguage *iface, HSTRING *class_name)
{
    FIXME("iface %p, class_name %p stub!\n", iface, class_name);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE language_GetTrustLevel(
        ILanguage *iface, TrustLevel *trust_level)
{
    FIXME("iface %p, trust_level %p stub!\n", iface, trust_level);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE language_get_LanguageTag(
        ILanguage *iface, HSTRING *value)
{
    struct language *language = impl_from_ILanguage(iface);

    TRACE("iface %p, value %p.\n", iface, value);

    return WindowsCreateString(language->name, wcslen(language->name), value);
}

static HRESULT STDMETHODCALLTYPE language_get_DisplayName(
        ILanguage *iface, HSTRING *value)
{
    struct language *language = impl_from_ILanguage(iface);
    WCHAR buffer[LOCALE_NAME_MAX_LENGTH];

    TRACE("iface %p, value %p\n", iface, value);
    if (!value) return E_POINTER;

    if (!GetLocaleInfoEx(language->name, LOCALE_SLOCALIZEDDISPLAYNAME, buffer, ARRAY_SIZE(buffer)))
        wcscpy(buffer, language->name);

    return WindowsCreateString(buffer, wcslen(buffer), value);
}

static HRESULT STDMETHODCALLTYPE language_get_NativeName(
        ILanguage *iface, HSTRING *value)
{
    struct language *language = impl_from_ILanguage(iface);
    WCHAR buffer[LOCALE_NAME_MAX_LENGTH];

    TRACE("iface %p, value %p\n", iface, value);
    if (!value) return E_POINTER;

    if (!GetLocaleInfoEx(language->name, LOCALE_SNATIVEDISPLAYNAME, buffer, ARRAY_SIZE(buffer)))
        wcscpy(buffer, language->name);

    return WindowsCreateString(buffer, wcslen(buffer), value);
}

static HRESULT STDMETHODCALLTYPE language_get_Script(
        ILanguage *iface, HSTRING *value)
{
    struct language *language = impl_from_ILanguage(iface);
    WCHAR buffer[LOCALE_NAME_MAX_LENGTH];

    TRACE("iface %p, value %p\n", iface, value);
    if (!value) return E_POINTER;

    if (!GetLocaleInfoEx(language->name, LOCALE_SSCRIPTS, buffer, ARRAY_SIZE(buffer)))
        wcscpy(buffer, L"Latn");

    return WindowsCreateString(buffer, wcslen(buffer), value);
}

static const struct ILanguageVtbl language_vtbl =
{
    language_QueryInterface,
    language_AddRef,
    language_Release,
    /* IInspectable methods */
    language_GetIids,
    language_GetRuntimeClassName,
    language_GetTrustLevel,
    /* ILanguage methods */
    language_get_LanguageTag,
    language_get_DisplayName,
    language_get_NativeName,
    language_get_Script,
};

DEFINE_IINSPECTABLE(language2, ILanguage2, struct language, ILanguage_iface)

static HRESULT STDMETHODCALLTYPE language2_get_LayoutDirection(ILanguage2 *iface, enum LanguageLayoutDirection *value)
{
    struct language *language = impl_from_ILanguage2(iface);
    DWORD reading_layout;

    TRACE("iface %p, value %p.\n", iface, value);

    if (!GetLocaleInfoEx(language->name, LOCALE_IREADINGLAYOUT | LOCALE_RETURN_NUMBER,
            (void *)&reading_layout, sizeof(reading_layout)))
         return E_FAIL;

    *value = (enum LanguageLayoutDirection)reading_layout;
    return S_OK;
}

static const struct ILanguage2Vtbl language2_vtbl =
{
    language2_QueryInterface,
    language2_AddRef,
    language2_Release,
    /* IInspectable methods */
    language2_GetIids,
    language2_GetRuntimeClassName,
    language2_GetTrustLevel,
    /* ILanguage2 methods */
    language2_get_LayoutDirection,
};

static HRESULT STDMETHODCALLTYPE windows_globalization_QueryInterface(
        IActivationFactory *iface, REFIID iid, void **out)
{
    struct windows_globalization *impl = impl_from_IActivationFactory(iface);
    TRACE("iface %p, iid %s, out %p stub!\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_IUnknown) ||
        IsEqualGUID(iid, &IID_IInspectable) ||
        IsEqualGUID(iid, &IID_IAgileObject) ||
        IsEqualGUID(iid, &IID_IActivationFactory))
    {
        IUnknown_AddRef(iface);
        *out = &impl->IActivationFactory_iface;
        return S_OK;
    }

    if (IsEqualGUID(iid, &IID_IGlobalizationPreferencesStatics))
    {
        IUnknown_AddRef(iface);
        *out = &impl->IGlobalizationPreferencesStatics_iface;
        return S_OK;
    }

    FIXME("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid));
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE windows_globalization_AddRef(
        IActivationFactory *iface)
{
    struct windows_globalization *impl = impl_from_IActivationFactory(iface);
    ULONG ref = InterlockedIncrement(&impl->ref);
    TRACE("iface %p, ref %lu.\n", iface, ref);
    return ref;
}

static ULONG STDMETHODCALLTYPE windows_globalization_Release(
        IActivationFactory *iface)
{
    struct windows_globalization *impl = impl_from_IActivationFactory(iface);
    ULONG ref = InterlockedDecrement(&impl->ref);
    TRACE("iface %p, ref %lu.\n", iface, ref);
    return ref;
}

static HRESULT STDMETHODCALLTYPE windows_globalization_GetIids(
        IActivationFactory *iface, ULONG *iid_count, IID **iids)
{
    FIXME("iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE windows_globalization_GetRuntimeClassName(
        IActivationFactory *iface, HSTRING *class_name)
{
    FIXME("iface %p, class_name %p stub!\n", iface, class_name);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE windows_globalization_GetTrustLevel(
        IActivationFactory *iface, TrustLevel *trust_level)
{
    FIXME("iface %p, trust_level %p stub!\n", iface, trust_level);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE windows_globalization_ActivateInstance(
        IActivationFactory *iface, IInspectable **instance)
{
    FIXME("iface %p, instance %p stub!\n", iface, instance);
    return E_NOTIMPL;
}

static const struct IActivationFactoryVtbl activation_factory_vtbl =
{
    windows_globalization_QueryInterface,
    windows_globalization_AddRef,
    windows_globalization_Release,
    /* IInspectable methods */
    windows_globalization_GetIids,
    windows_globalization_GetRuntimeClassName,
    windows_globalization_GetTrustLevel,
    /* IActivationFactory methods */
    windows_globalization_ActivateInstance,
};

static HRESULT STDMETHODCALLTYPE globalization_preferences_QueryInterface(
        IGlobalizationPreferencesStatics *iface, REFIID iid, void **object)
{
    struct windows_globalization *impl = impl_from_IGlobalizationPreferencesStatics(iface);
    return windows_globalization_QueryInterface(&impl->IActivationFactory_iface, iid, object);
}

static ULONG STDMETHODCALLTYPE globalization_preferences_AddRef(
        IGlobalizationPreferencesStatics *iface)
{
    struct windows_globalization *impl = impl_from_IGlobalizationPreferencesStatics(iface);
    return windows_globalization_AddRef(&impl->IActivationFactory_iface);
}

static ULONG STDMETHODCALLTYPE globalization_preferences_Release(
        IGlobalizationPreferencesStatics *iface)
{
    struct windows_globalization *impl = impl_from_IGlobalizationPreferencesStatics(iface);
    return windows_globalization_Release(&impl->IActivationFactory_iface);
}

static HRESULT STDMETHODCALLTYPE globalization_preferences_GetIids(
        IGlobalizationPreferencesStatics *iface, ULONG *iid_count, IID **iids)
{
    FIXME("iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE globalization_preferences_GetRuntimeClassName(
        IGlobalizationPreferencesStatics *iface, HSTRING *class_name)
{
    FIXME("iface %p, class_name %p stub!\n", iface, class_name);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE globalization_preferences_GetTrustLevel(
        IGlobalizationPreferencesStatics *iface, TrustLevel *trust_level)
{
    FIXME("iface %p, trust_level %p stub!\n", iface, trust_level);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE globalization_preferences_get_Calendars(
        IGlobalizationPreferencesStatics *iface, IVectorView_HSTRING **out)
{
    FIXME("iface %p, out %p stub!\n", iface, out);
    return hstring_vector_create(NULL, 0, out);
}

static HRESULT STDMETHODCALLTYPE globalization_preferences_get_Clocks(
        IGlobalizationPreferencesStatics *iface, IVectorView_HSTRING **out)
{
    FIXME("iface %p, out %p stub!\n", iface, out);
    return hstring_vector_create(NULL, 0, out);
}

static HRESULT STDMETHODCALLTYPE globalization_preferences_get_Currencies(
        IGlobalizationPreferencesStatics *iface, IVectorView_HSTRING **out)
{
    FIXME("iface %p, out %p stub!\n", iface, out);
    return hstring_vector_create(NULL, 0, out);
}

static HRESULT STDMETHODCALLTYPE globalization_preferences_get_Languages(
        IGlobalizationPreferencesStatics *iface, IVectorView_HSTRING **out)
{
    HSTRING hstring;
    HRESULT hr;
    WCHAR locale[LOCALE_NAME_MAX_LENGTH];

    TRACE("iface %p, out %p.\n", iface, out);

    if (!GetUserDefaultLocaleName(locale, LOCALE_NAME_MAX_LENGTH))
        return E_FAIL;

    TRACE("returning language %s\n", debugstr_w(locale));

    if (FAILED(hr = WindowsCreateString(locale, wcslen(locale), &hstring)))
        return hr;

    return hstring_vector_create(&hstring, 1, out);
}

static HRESULT STDMETHODCALLTYPE globalization_preferences_get_HomeGeographicRegion(
        IGlobalizationPreferencesStatics *iface, HSTRING* out)
{
    WCHAR country[16];

    TRACE("iface %p, out %p.\n", iface, out);

    if (!GetUserDefaultGeoName(country, 16))
        return E_FAIL;

    TRACE("returning country %s\n", debugstr_w(country));

    return WindowsCreateString(country, wcslen(country), out);
}

static HRESULT STDMETHODCALLTYPE globalization_preferences_get_WeekStartsOn(
        IGlobalizationPreferencesStatics *iface, enum DayOfWeek* out)
{
    FIXME("iface %p, out %p stub!\n", iface, out);
    return E_NOTIMPL;
}

static const struct IGlobalizationPreferencesStaticsVtbl globalization_preferences_vtbl =
{
    globalization_preferences_QueryInterface,
    globalization_preferences_AddRef,
    globalization_preferences_Release,
    /* IInspectable methods */
    globalization_preferences_GetIids,
    globalization_preferences_GetRuntimeClassName,
    globalization_preferences_GetTrustLevel,
    /* IGlobalizationPreferencesStatics methods */
    globalization_preferences_get_Calendars,
    globalization_preferences_get_Clocks,
    globalization_preferences_get_Currencies,
    globalization_preferences_get_Languages,
    globalization_preferences_get_HomeGeographicRegion,
    globalization_preferences_get_WeekStartsOn,
};

static struct windows_globalization userprofile_preferences =
{
    {&activation_factory_vtbl},
    {&globalization_preferences_vtbl},
    0
};

IActivationFactory *userprofile_preferences_factory = &userprofile_preferences.IActivationFactory_iface;

static HRESULT STDMETHODCALLTYPE windows_globalization_language_factory_QueryInterface(
        IActivationFactory *iface, REFIID iid, void **out)
{
    struct language_factory *factory = impl_language_factory_from_IActivationFactory(iface);

    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_IUnknown) ||
        IsEqualGUID(iid, &IID_IInspectable) ||
        IsEqualGUID(iid, &IID_IAgileObject) ||
        IsEqualGUID(iid, &IID_IActivationFactory))
    {
        IUnknown_AddRef(iface);
        *out = iface;
        return S_OK;
    }

    if (IsEqualGUID(iid, &IID_ILanguageFactory))
    {
        IUnknown_AddRef(iface);
        *out = &factory->ILanguageFactory_iface;
        return S_OK;
    }

    if (IsEqualGUID(iid, &IID_ILanguageStatics))
    {
        IUnknown_AddRef(iface);
        *out = &factory->ILanguageStatics_iface;
        return S_OK;
    }

    FIXME("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid));
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE windows_globalization_language_factory_AddRef(IActivationFactory *iface)
{
    struct language_factory *factory = impl_language_factory_from_IActivationFactory(iface);
    return InterlockedIncrement(&factory->ref);
}

static ULONG STDMETHODCALLTYPE windows_globalization_language_factory_Release(IActivationFactory *iface)
{
    struct language_factory *factory = impl_language_factory_from_IActivationFactory(iface);
    return InterlockedDecrement(&factory->ref);
}

static const struct IActivationFactoryVtbl activation_factory_language_vtbl =
{
    windows_globalization_language_factory_QueryInterface,
    windows_globalization_language_factory_AddRef,
    windows_globalization_language_factory_Release,
    /* IInspectable methods */
    windows_globalization_GetIids,
    windows_globalization_GetRuntimeClassName,
    windows_globalization_GetTrustLevel,
    /* IActivationFactory methods */
    windows_globalization_ActivateInstance,
};

static HRESULT STDMETHODCALLTYPE language_factory_QueryInterface(
        ILanguageFactory *iface, REFIID iid, void **object)
{
    struct language_factory *factory = impl_from_ILanguageFactory(iface);
    return IActivationFactory_QueryInterface(&factory->IActivationFactory_iface, iid, object);
}

static ULONG STDMETHODCALLTYPE language_factory_AddRef(
        ILanguageFactory *iface)
{
    struct language_factory *factory = impl_from_ILanguageFactory(iface);
    return IActivationFactory_AddRef(&factory->IActivationFactory_iface);
}

static ULONG STDMETHODCALLTYPE language_factory_Release(
        ILanguageFactory *iface)
{
    struct language_factory *factory = impl_from_ILanguageFactory(iface);
    return IActivationFactory_Release(&factory->IActivationFactory_iface);
}

static HRESULT STDMETHODCALLTYPE language_factory_GetIids(
        ILanguageFactory *iface, ULONG *iid_count, IID **iids)
{
    FIXME("iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE language_factory_GetRuntimeClassName(
        ILanguageFactory *iface, HSTRING *class_name)
{
    FIXME("iface %p, class_name %p stub!\n", iface, class_name);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE language_factory_GetTrustLevel(
        ILanguageFactory *iface, TrustLevel *trust_level)
{
    FIXME("iface %p, trust_level %p stub!\n", iface, trust_level);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE language_factory_CreateLanguage(
        ILanguageFactory *iface, HSTRING tag, ILanguage **value)
{
    const WCHAR *name = WindowsGetStringRawBuffer(tag, NULL);
    WCHAR buffer[LOCALE_NAME_MAX_LENGTH];
    struct language *language;

    TRACE("iface %p, tag %p, value %p.\n", iface, tag, value);
    if (!value) return E_POINTER;

    if (!GetLocaleInfoEx(name, LOCALE_SNAME, buffer, ARRAY_SIZE(buffer)))
    {
        if (name && wcslen(name) < ARRAY_SIZE(buffer))
            wcscpy(buffer, name);
        else
            wcscpy(buffer, L"en-US");
    }

    if (!(language = calloc(1, sizeof(*language))))
        return E_OUTOFMEMORY;

    language->ILanguage_iface.lpVtbl = &language_vtbl;
    language->ILanguage2_iface.lpVtbl = &language2_vtbl;
    language->ref = 1;
    wcscpy(language->name, buffer);

    *value = &language->ILanguage_iface;

    return S_OK;
}

static const struct ILanguageFactoryVtbl language_factory_vtbl =
{
    language_factory_QueryInterface,
    language_factory_AddRef,
    language_factory_Release,
    /* IInspectable methods */
    language_factory_GetIids,
    language_factory_GetRuntimeClassName,
    language_factory_GetTrustLevel,
    /* ILanguageFactory methods */
    language_factory_CreateLanguage,
};

static HRESULT STDMETHODCALLTYPE language_statics_QueryInterface(
        ILanguageStatics *iface, REFIID iid, void **object)
{
    struct language_factory *factory = impl_from_ILanguageStatics(iface);
    return IActivationFactory_QueryInterface(&factory->IActivationFactory_iface, iid, object);
}

static ULONG STDMETHODCALLTYPE language_statics_AddRef(ILanguageStatics *iface)
{
    struct language_factory *factory = impl_from_ILanguageStatics(iface);
    return IActivationFactory_AddRef(&factory->IActivationFactory_iface);
}

static ULONG STDMETHODCALLTYPE language_statics_Release(ILanguageStatics *iface)
{
    struct language_factory *factory = impl_from_ILanguageStatics(iface);
    return IActivationFactory_Release(&factory->IActivationFactory_iface);
}

static HRESULT STDMETHODCALLTYPE language_statics_GetIids(
        ILanguageStatics *iface, ULONG *iid_count, IID **iids)
{
    FIXME("iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE language_statics_GetRuntimeClassName(
        ILanguageStatics *iface, HSTRING *class_name)
{
    return WindowsCreateString(L"Windows.Globalization.Language", 30, class_name);
}

static HRESULT STDMETHODCALLTYPE language_statics_GetTrustLevel(
        ILanguageStatics *iface, TrustLevel *trust_level)
{
    if (trust_level) *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE language_statics_IsWellFormed(
        ILanguageStatics *iface, HSTRING tag, BOOLEAN *result)
{
    TRACE("iface %p, tag %s, result %p.\n", iface, debugstr_hstring(tag), result);
    if (!result) return E_POINTER;
    *result = TRUE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE language_statics_get_CurrentInputMethodLanguageTag(
        ILanguageStatics *iface, HSTRING *value)
{
    WCHAR locale[LOCALE_NAME_MAX_LENGTH];
    TRACE("iface %p, value %p.\n", iface, value);
    if (!value) return E_POINTER;
    if (!GetUserDefaultLocaleName(locale, LOCALE_NAME_MAX_LENGTH))
        wcscpy(locale, L"en-US");
    return WindowsCreateString(locale, wcslen(locale), value);
}

static const struct ILanguageStaticsVtbl language_statics_vtbl =
{
    language_statics_QueryInterface,
    language_statics_AddRef,
    language_statics_Release,
    language_statics_GetIids,
    language_statics_GetRuntimeClassName,
    language_statics_GetTrustLevel,
    language_statics_IsWellFormed,
    language_statics_get_CurrentInputMethodLanguageTag,
};

static struct language_factory language_factory =
{
    {&activation_factory_language_vtbl},
    {&language_factory_vtbl},
    {&language_statics_vtbl},
    0
};

IActivationFactory *language_activation_factory = &language_factory.IActivationFactory_iface;

/* LanguageFont implementation */
static HRESULT WINAPI language_font_QueryInterface( void *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IMarshal ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget_glob ) ||
        IsEqualGUID( iid, &IID_IReferenceTracker_glob ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    *out = iface;
    return S_OK;
}
static ULONG WINAPI language_font_AddRef( void *iface ) { return 2; }
static ULONG WINAPI language_font_Release( void *iface ) { return 1; }
static HRESULT WINAPI language_font_GetIids( void *iface, ULONG *c, IID **i ) { return E_NOTIMPL; }
static HRESULT WINAPI language_font_GetRuntimeClassName( void *iface, HSTRING *n )
{
    return WindowsCreateString( L"Windows.Globalization.Fonts.LanguageFont", 40, n );
}
static HRESULT WINAPI language_font_GetTrustLevel( void *iface, TrustLevel *t )
{
    if (t) *t = BaseTrust;
    return S_OK;
}
static HRESULT WINAPI language_font_get_FontFamily( void *iface, HSTRING *value )
{
    if (!value) return E_POINTER;
    return WindowsCreateString( L"Segoe UI", 8, value );
}
static HRESULT WINAPI language_font_get_FontWeight( void *iface, void *weight )
{
    if (!weight) return E_POINTER;
    *(UINT16 *)weight = 400;
    return S_OK;
}
static HRESULT WINAPI language_font_get_FontStretch( void *iface, INT32 *stretch )
{
    if (!stretch) return E_POINTER;
    *stretch = 5;
    return S_OK;
}
static HRESULT WINAPI language_font_get_FontStyle( void *iface, INT32 *style )
{
    if (!style) return E_POINTER;
    *style = 0;
    return S_OK;
}
static HRESULT WINAPI language_font_get_ScaleFactor( void *iface, double *factor )
{
    if (!factor) return E_POINTER;
    *factor = 100.0;
    return S_OK;
}

static const void *language_font_vtbl[] =
{
    language_font_QueryInterface,
    language_font_AddRef,
    language_font_Release,
    language_font_GetIids,
    language_font_GetRuntimeClassName,
    language_font_GetTrustLevel,
    language_font_get_FontFamily,
    language_font_get_FontWeight,
    language_font_get_FontStretch,
    language_font_get_FontStyle,
    language_font_get_ScaleFactor,
};
static struct { const void **vtbl; } language_font_obj = { language_font_vtbl };

/* LanguageFontGroup implementation */
static HRESULT WINAPI language_font_group_QueryInterface( void *iface, REFIID iid, void **out )
{
    return language_font_QueryInterface( iface, iid, out );
}
static ULONG WINAPI language_font_group_AddRef( void *iface ) { return 2; }
static ULONG WINAPI language_font_group_Release( void *iface ) { return 1; }
static HRESULT WINAPI language_font_group_GetIids( void *iface, ULONG *c, IID **i ) { return E_NOTIMPL; }
static HRESULT WINAPI language_font_group_GetRuntimeClassName( void *iface, HSTRING *n )
{
    return WindowsCreateString( L"Windows.Globalization.Fonts.LanguageFontGroup", 45, n );
}
static HRESULT WINAPI language_font_group_GetTrustLevel( void *iface, TrustLevel *t )
{
    if (t) *t = BaseTrust;
    return S_OK;
}
static HRESULT WINAPI language_font_group_get_Font( void *iface, void **value )
{
    if (!value) return E_POINTER;
    *value = &language_font_obj;
    return S_OK;
}

static const void *language_font_group_vtbl[] =
{
    language_font_group_QueryInterface,
    language_font_group_AddRef,
    language_font_group_Release,
    language_font_group_GetIids,
    language_font_group_GetRuntimeClassName,
    language_font_group_GetTrustLevel,
    language_font_group_get_Font, /* 6: UITextFont */
    language_font_group_get_Font, /* 7: UIHeadingFont */
    language_font_group_get_Font, /* 8: UITitleFont */
    language_font_group_get_Font, /* 9: UICaptionFont */
    language_font_group_get_Font, /* 10: UINotificationHeadingFont */
    language_font_group_get_Font, /* 11: TraditionalDocumentFont */
    language_font_group_get_Font, /* 12: ModernDocumentFont */
    language_font_group_get_Font, /* 13: DocumentHeadingFont */
    language_font_group_get_Font, /* 14: FixedWidthTextFont */
    language_font_group_get_Font, /* 15: DocumentAlternate1Font */
    language_font_group_get_Font, /* 16: DocumentAlternate2Font */
};
static struct { const void **vtbl; } language_font_group_obj = { language_font_group_vtbl };

/* LanguageFontGroupFactory implementation */
static HRESULT WINAPI font_group_factory_QueryInterface( void *iface, REFIID iid, void **out )
{
    return language_font_QueryInterface( iface, iid, out );
}
static ULONG WINAPI font_group_factory_AddRef( void *iface ) { return 2; }
static ULONG WINAPI font_group_factory_Release( void *iface ) { return 1; }
static HRESULT WINAPI font_group_factory_GetIids( void *iface, ULONG *c, IID **i ) { return E_NOTIMPL; }
static HRESULT WINAPI font_group_factory_GetRuntimeClassName( void *iface, HSTRING *n )
{
    return WindowsCreateString( L"Windows.Globalization.Fonts.LanguageFontGroup", 45, n );
}
static HRESULT WINAPI font_group_factory_GetTrustLevel( void *iface, TrustLevel *t )
{
    if (t) *t = BaseTrust;
    return S_OK;
}
static HRESULT WINAPI font_group_factory_CreateLanguageFontGroup( void *iface, HSTRING languageTag, void **value )
{
    TRACE( "iface %p, languageTag %s, value %p\n", iface, debugstr_hstring(languageTag), value );
    if (!value) return E_POINTER;
    *value = &language_font_group_obj;
    return S_OK;
}
static const void *font_group_factory_vtbl[] =
{
    font_group_factory_QueryInterface,
    font_group_factory_AddRef,
    font_group_factory_Release,
    font_group_factory_GetIids,
    font_group_factory_GetRuntimeClassName,
    font_group_factory_GetTrustLevel,
    font_group_factory_CreateLanguageFontGroup, /* slot 6 */
};
static struct { const void **vtbl; } font_group_factory_obj = { font_group_factory_vtbl };

IActivationFactory *font_group_factory = (IActivationFactory *)&font_group_factory_obj;
