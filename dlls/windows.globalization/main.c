/* WinRT Windows.Globalization implementation
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

#include "initguid.h"
#include <math.h>
#include "private.h"

WINE_DEFAULT_DEBUG_CHANNEL(locale);

/*
 *
 * IIterator<HSTRING>
 *
 */

struct iterator_hstring
{
   IIterator_HSTRING IIterator_HSTRING_iface;
   LONG ref;

   IVectorView_HSTRING *view;
   UINT32 index;
   UINT32 size;
};

static inline struct iterator_hstring *impl_from_IIterator_HSTRING( IIterator_HSTRING *iface )
{
   return CONTAINING_RECORD(iface, struct iterator_hstring, IIterator_HSTRING_iface);
}

static HRESULT WINAPI iterator_hstring_QueryInterface( IIterator_HSTRING *iface, REFIID iid, void **out )
{
    struct iterator_hstring *impl = impl_from_IIterator_HSTRING(iface);

   TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

   if (IsEqualGUID(iid, &IID_IUnknown) ||
       IsEqualGUID(iid, &IID_IInspectable) ||
       IsEqualGUID(iid, &IID_IAgileObject) ||
       IsEqualGUID(iid, &IID_IIterator_HSTRING))
    {
       IInspectable_AddRef((*out = &impl->IIterator_HSTRING_iface));
       return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid));
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI iterator_hstring_AddRef( IIterator_HSTRING *iface )
{
    struct iterator_hstring *impl = impl_from_IIterator_HSTRING(iface);
    ULONG ref = InterlockedIncrement(&impl->ref);
    TRACE("iface %p increasing refcount to %lu.\n", iface, ref);
    return ref;
}

static ULONG WINAPI iterator_hstring_Release( IIterator_HSTRING *iface )
{
    struct iterator_hstring *impl = impl_from_IIterator_HSTRING(iface);
    ULONG ref = InterlockedDecrement(&impl->ref);

    TRACE("iface %p decreasing refcount to %lu.\n", iface, ref);

    if (!ref)
    {
        IVectorView_HSTRING_Release(impl->view);
        free(impl);
    }

    return ref;
}

static HRESULT WINAPI iterator_hstring_GetIids( IIterator_HSTRING *iface, ULONG *iid_count, IID **iids )
{
    if (!iid_count || !iids) return E_POINTER;
    *iid_count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI iterator_hstring_GetRuntimeClassName( IIterator_HSTRING *iface, HSTRING *class_name )
{
    if (!class_name) return E_POINTER;
    return WindowsCreateString( L"Windows.Foundation.Collections.IIterator`1<String>", 50, class_name );
}

static HRESULT WINAPI iterator_hstring_GetTrustLevel( IIterator_HSTRING *iface, TrustLevel *trust_level )
{
    if (trust_level) *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI iterator_hstring_get_Current( IIterator_HSTRING *iface, HSTRING *value )
{
    struct iterator_hstring *impl = impl_from_IIterator_HSTRING(iface);
    TRACE("iface %p, value %p.\n", iface, value);
    return IVectorView_HSTRING_GetAt(impl->view, impl->index, value);
}

static HRESULT WINAPI iterator_hstring_get_HasCurrent( IIterator_HSTRING *iface, boolean *value )
{
    struct iterator_hstring *impl = impl_from_IIterator_HSTRING(iface);

    TRACE("iface %p, value %p.\n", iface, value);

    *value = impl->index < impl->size;
    return S_OK;
}

static HRESULT WINAPI iterator_hstring_MoveNext( IIterator_HSTRING *iface, boolean *value )
{
    struct iterator_hstring *impl = impl_from_IIterator_HSTRING(iface);

    TRACE("iface %p, value %p.\n", iface, value);

    if (impl->index < impl->size) impl->index++;
    return IIterator_HSTRING_get_HasCurrent(iface, value);
}

static HRESULT WINAPI iterator_hstring_GetMany( IIterator_HSTRING *iface, UINT32 items_size,
                                                HSTRING *items, UINT *count )
{
    struct iterator_hstring *impl = impl_from_IIterator_HSTRING(iface);
    TRACE("iface %p, items_size %u, items %p, count %p.\n", iface, items_size, items, count);
    return IVectorView_HSTRING_GetMany(impl->view, impl->index, items_size, items, count);
}

static const IIterator_HSTRINGVtbl iterator_hstring_vtbl =
{
    /* IUnknown methods */
    iterator_hstring_QueryInterface,
    iterator_hstring_AddRef,
    iterator_hstring_Release,
    /* IInspectable methods */
    iterator_hstring_GetIids,
    iterator_hstring_GetRuntimeClassName,
    iterator_hstring_GetTrustLevel,
    /* IIterator<HSTRING> methods */
    iterator_hstring_get_Current,
    iterator_hstring_get_HasCurrent,
    iterator_hstring_MoveNext,
    iterator_hstring_GetMany,
};

struct hstring_vector
{
    IVectorView_HSTRING IVectorView_HSTRING_iface;
    IIterable_HSTRING IIterable_HSTRING_iface;
    LONG ref;

    ULONG count;
    HSTRING values[];
};

C_ASSERT(sizeof(struct hstring_vector) == offsetof(struct hstring_vector, values[0]));

static inline struct hstring_vector *impl_from_IVectorView_HSTRING(IVectorView_HSTRING *iface)
{
    return CONTAINING_RECORD(iface, struct hstring_vector, IVectorView_HSTRING_iface);
}

static HRESULT STDMETHODCALLTYPE hstring_vector_QueryInterface(IVectorView_HSTRING *iface,
        REFIID iid, void **out)
{
    struct hstring_vector *impl = impl_from_IVectorView_HSTRING(iface);
    ERR("GLOBALIZATION: vector_QI iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_IUnknown) ||
        IsEqualGUID(iid, &IID_IInspectable) ||
        IsEqualGUID(iid, &IID_IAgileObject) ||
        IsEqualGUID(iid, &IID_IVectorView_HSTRING))
    {
        IUnknown_AddRef(iface);
        *out = &impl->IVectorView_HSTRING_iface;
        return S_OK;
    }

    if (IsEqualGUID(iid, &IID_IIterable_HSTRING))
    {
        IUnknown_AddRef(iface);
        *out = &impl->IIterable_HSTRING_iface;
        return S_OK;
    }

    FIXME("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid));
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE hstring_vector_AddRef(IVectorView_HSTRING *iface)
{
    struct hstring_vector *impl = impl_from_IVectorView_HSTRING(iface);
    ULONG ref = InterlockedIncrement(&impl->ref);
    TRACE("iface %p, ref %lu.\n", iface, ref);
    return ref;
}

static ULONG STDMETHODCALLTYPE hstring_vector_Release(IVectorView_HSTRING *iface)
{
    struct hstring_vector *impl = impl_from_IVectorView_HSTRING(iface);
    ULONG ref = InterlockedDecrement(&impl->ref);
    TRACE("iface %p, ref %lu.\n", iface, ref);
    if (ref == 0)
    {
        while (impl->count--) WindowsDeleteString(impl->values[impl->count]);
        free(impl);
    }
    return ref;
}

static HRESULT STDMETHODCALLTYPE hstring_vector_GetIids(IVectorView_HSTRING *iface,
        ULONG *iid_count, IID **iids)
{
    if (!iid_count || !iids) return E_POINTER;
    *iid_count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE hstring_vector_GetRuntimeClassName(IVectorView_HSTRING *iface,
        HSTRING *class_name)
{
    if (!class_name) return E_POINTER;
    return WindowsCreateString(L"Windows.Foundation.Collections.IVectorView`1<String>", 49, class_name);
}

static HRESULT STDMETHODCALLTYPE hstring_vector_GetTrustLevel(IVectorView_HSTRING *iface,
        TrustLevel *trust_level)
{
    if (trust_level) *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE hstring_vector_GetAt(IVectorView_HSTRING *iface,
        UINT32 index, HSTRING *value)
{
    struct hstring_vector *impl = impl_from_IVectorView_HSTRING(iface);

    ERR("GLOBALIZATION: GetAt iface %p, index %#x, value %p.\n", iface, index, value);

    *value = NULL;
    if (index >= impl->count) return E_BOUNDS;
    return WindowsDuplicateString(impl->values[index], value);
}

static HRESULT STDMETHODCALLTYPE hstring_vector_get_Size(IVectorView_HSTRING *iface,
        UINT32 *value)
{
    struct hstring_vector *impl = impl_from_IVectorView_HSTRING(iface);

    ERR("GLOBALIZATION: get_Size iface %p, value %p, returning count %lu.\n", iface, value, impl->count);

    if (!value) return E_POINTER;
    *value = impl->count;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE hstring_vector_IndexOf(IVectorView_HSTRING *iface,
        HSTRING element, UINT32 *index, BOOLEAN *found)
{
    struct hstring_vector *impl = impl_from_IVectorView_HSTRING(iface);
    INT32 i, order;

    TRACE("iface %p, element %p, index %p, found %p.\n", iface, element, index, found);

    for (i = 0; i < impl->count; ++i)
        if (SUCCEEDED(WindowsCompareStringOrdinal(impl->values[i], element, &order)) && order == 0)
            break;

    if (i < impl->count)
    {
        *found = TRUE;
        *index = i;
    }
    else
    {
        *found = FALSE;
        *index = 0;
    }

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE hstring_vector_GetMany(IVectorView_HSTRING *iface,
        UINT32 start_index, UINT32 items_size, HSTRING *items, UINT *count)
{
    struct hstring_vector *impl = impl_from_IVectorView_HSTRING(iface);
    HRESULT hr = S_OK;
    ULONG i;

    TRACE("iface %p, start_index %#x, items %p, count %p.\n", iface, start_index, items, count);

    memset(items, 0, items_size * sizeof(*items));

    for (i = start_index; i < impl->count && i < start_index + items_size; ++i)
        if (FAILED(hr = WindowsDuplicateString(impl->values[i], items + i - start_index)))
            break;

    if (FAILED(hr)) while (i-- > start_index) WindowsDeleteString(items[i - start_index]);
    *count = i - start_index;
    return hr;
}

static const struct IVectorView_HSTRINGVtbl hstring_vector_vtbl =
{
    hstring_vector_QueryInterface,
    hstring_vector_AddRef,
    hstring_vector_Release,
    /* IInspectable methods */
    hstring_vector_GetIids,
    hstring_vector_GetRuntimeClassName,
    hstring_vector_GetTrustLevel,
    /* IVectorView<HSTRING> methods */
    hstring_vector_GetAt,
    hstring_vector_get_Size,
    hstring_vector_IndexOf,
    hstring_vector_GetMany,
};

static const struct IIterable_HSTRINGVtbl iterable_view_hstring_vtbl;

HRESULT hstring_vector_create(HSTRING *values, SIZE_T count, IVectorView_HSTRING **out)
{
    struct hstring_vector *impl;

    if (!(impl = malloc(offsetof(struct hstring_vector, values[count])))) return E_OUTOFMEMORY;
    impl->ref = 1;

    impl->IVectorView_HSTRING_iface.lpVtbl = &hstring_vector_vtbl;
    impl->IIterable_HSTRING_iface.lpVtbl = &iterable_view_hstring_vtbl;
    impl->count = count;
    memcpy(impl->values, values, count * sizeof(HSTRING));

    *out = &impl->IVectorView_HSTRING_iface;
    return S_OK;
}

/*
 *
 * IIterable<HSTRING>
 *
 */

DEFINE_IINSPECTABLE_(iterable_view_hstring, IIterable_HSTRING, struct hstring_vector, view_impl_from_IIterable_HSTRING,
                     IIterable_HSTRING_iface, &impl->IVectorView_HSTRING_iface)

static HRESULT WINAPI iterable_view_hstring_First( IIterable_HSTRING *iface, IIterator_HSTRING **value )
{
    struct hstring_vector *impl = view_impl_from_IIterable_HSTRING(iface);
    struct iterator_hstring *iter;

    TRACE("iface %p, value %p.\n", iface, value);

    if (!(iter = calloc(1, sizeof(*iter)))) return E_OUTOFMEMORY;
    iter->IIterator_HSTRING_iface.lpVtbl = &iterator_hstring_vtbl;
    iter->ref = 1;

    IVectorView_HSTRING_AddRef((iter->view = &impl->IVectorView_HSTRING_iface));
    iter->size = impl->count;

    *value = &iter->IIterator_HSTRING_iface;
    return S_OK;
}

static const struct IIterable_HSTRINGVtbl iterable_view_hstring_vtbl =
{
    /* IUnknown methods */
    iterable_view_hstring_QueryInterface,
    iterable_view_hstring_AddRef,
    iterable_view_hstring_Release,
    /* IInspectable methods */
    iterable_view_hstring_GetIids,
    iterable_view_hstring_GetRuntimeClassName,
    iterable_view_hstring_GetTrustLevel,
    /* IIterable<HSTRING> methods */
    iterable_view_hstring_First,
};

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

struct application_languages
{
    IActivationFactory IActivationFactory_iface;
    IApplicationLanguagesStatics IApplicationLanguagesStatics_iface;
    LONG ref;
    struct com_interface_entry *iids_cache; /* stable GetIids pair array, see factory_GetIids */
    ULONG iids_count;
};

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

static inline struct application_languages *impl_application_languages_from_IActivationFactory(IActivationFactory *iface)
{
    return CONTAINING_RECORD(iface, struct application_languages, IActivationFactory_iface);
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

static HRESULT STDMETHODCALLTYPE application_languages_activation_factory_ActivateInstance(
        IActivationFactory *iface, IInspectable **instance)
{
    FIXME("iface %p, instance %p stub!\n", iface, instance);
    return E_NOTIMPL;
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

static struct application_languages application_languages =
{
    {&application_languages_activation_factory_vtbl},
    {&application_languages_statics_vtbl},
    1
};

HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void **out)
{
    FIXME("clsid %s, riid %s, out %p stub!\n", debugstr_guid(clsid), debugstr_guid(riid), out);
    return CLASS_E_CLASSNOTAVAILABLE;
}

/* LanguageFont implementation */
static HRESULT WINAPI language_font_QueryInterface( void *iface, REFIID iid, void **out )
{
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
    *out = iface;
    return S_OK;
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
    *out = iface;
    return S_OK;
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

/* =========================================================================
 * Windows.Globalization.NumberFormatting.DecimalFormatter
 * ========================================================================= */
static const GUID IID_IDecimalFormatterFactory =
    { 0x0d018c9a, 0xe393, 0x46b8, { 0xb8, 0x30, 0x7a, 0x69, 0xc8, 0xf8, 0x9f, 0xbb } };
static const GUID IID_INumberFormatter =
    { 0xa5007c49, 0x7676, 0x4db7, { 0x86, 0x31, 0x1b, 0x6f, 0xf2, 0x65, 0xca, 0xa9 } };
static const GUID IID_INumberFormatter2 =
    { 0xd4a8c1f0, 0x80d0, 0x4b0d, { 0xa8, 0x9e, 0x88, 0x2c, 0x1e, 0x8f, 0x83, 0x10 } };
static const GUID IID_INumberFormatterOptions =
    { 0x80332d21, 0xaee1, 0x4a39, { 0xba, 0xa2, 0x07, 0xed, 0x8c, 0x96, 0xda, 0xf6 } };
static const GUID IID_INumberParser =
    { 0xe6659412, 0x4a13, 0x4a53, { 0x83, 0xa1, 0x39, 0x2f, 0xbe, 0x4c, 0xff, 0x9f } };

struct decimal_formatter
{
    IInspectable IInspectable_iface;
    LONG ref;
    INT32 fraction_digits;
    INT32 integer_digits;
    BOOLEAN is_grouping_enabled;
};

static inline struct decimal_formatter *impl_from_decimal_formatter( void *iface )
{
    return CONTAINING_RECORD( iface, struct decimal_formatter, IInspectable_iface );
}

static HRESULT WINAPI decimal_formatter_QueryInterface( IInspectable *iface, REFIID iid, void **out )
{
    struct decimal_formatter *impl = impl_from_decimal_formatter( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_INumberFormatter ) ||
        IsEqualGUID( iid, &IID_INumberFormatter2 ) ||
        IsEqualGUID( iid, &IID_INumberFormatterOptions ) ||
        IsEqualGUID( iid, &IID_INumberParser ))
    {
        *out = &impl->IInspectable_iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI decimal_formatter_AddRef( IInspectable *iface )
{
    struct decimal_formatter *impl = impl_from_decimal_formatter( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI decimal_formatter_Release( IInspectable *iface )
{
    struct decimal_formatter *impl = impl_from_decimal_formatter( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref) free( impl );
    return ref;
}

static HRESULT WINAPI decimal_formatter_GetIids( IInspectable *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI decimal_formatter_GetRuntimeClassName( IInspectable *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.Globalization.NumberFormatting.DecimalFormatter", 55, name );
}

static HRESULT WINAPI decimal_formatter_GetTrustLevel( IInspectable *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI decimal_formatter_FormatInt( void *iface, INT64 value, HSTRING *result )
{
    WCHAR buf[64];
    if (!result) return E_POINTER;
    swprintf( buf, ARRAY_SIZE(buf), L"%lld", (long long)value );
    return WindowsCreateString( buf, wcslen(buf), result );
}

static HRESULT WINAPI decimal_formatter_FormatUInt( void *iface, UINT64 value, HSTRING *result )
{
    WCHAR buf[64];
    if (!result) return E_POINTER;
    swprintf( buf, ARRAY_SIZE(buf), L"%llu", (unsigned long long)value );
    return WindowsCreateString( buf, wcslen(buf), result );
}

static HRESULT WINAPI decimal_formatter_FormatDouble( void *iface, double value, HSTRING *result )
{
    WCHAR buf[64];
    if (!result) return E_POINTER;
    if (value == floor( value ) && fabs( value ) < 1e14)
        swprintf( buf, ARRAY_SIZE(buf), L"%.0f", value );
    else
        swprintf( buf, ARRAY_SIZE(buf), L"%.10g", value );
    return WindowsCreateString( buf, wcslen(buf), result );
}

static HRESULT WINAPI decimal_formatter_get_Languages( void *iface, void **value )
{
    WCHAR locale[LOCALE_NAME_MAX_LENGTH];
    HSTRING hs;
    if (!value) return E_POINTER;
    if (!GetUserDefaultLocaleName( locale, LOCALE_NAME_MAX_LENGTH ))
        wcscpy( locale, L"en-US" );
    WindowsCreateString( locale, wcslen(locale), &hs );
    return hstring_vector_create( &hs, 1, (IVectorView_HSTRING **)value );
}

static HRESULT WINAPI decimal_formatter_get_GeographicRegion( void *iface, HSTRING *value )
{
    WCHAR country[16];
    if (!value) return E_POINTER;
    if (!GetUserDefaultGeoName( country, 16 ))
        wcscpy( country, L"US" );
    return WindowsCreateString( country, wcslen(country), value );
}

static HRESULT WINAPI decimal_formatter_get_IntegerDigits( void *iface, INT32 *value )
{
    struct decimal_formatter *impl = impl_from_decimal_formatter( iface );
    if (!value) return E_POINTER;
    *value = impl->integer_digits;
    return S_OK;
}
static HRESULT WINAPI decimal_formatter_put_IntegerDigits( void *iface, INT32 value )
{
    struct decimal_formatter *impl = impl_from_decimal_formatter( iface );
    impl->integer_digits = value;
    return S_OK;
}

static HRESULT WINAPI decimal_formatter_get_FractionDigits( void *iface, INT32 *value )
{
    struct decimal_formatter *impl = impl_from_decimal_formatter( iface );
    if (!value) return E_POINTER;
    *value = impl->fraction_digits;
    return S_OK;
}
static HRESULT WINAPI decimal_formatter_put_FractionDigits( void *iface, INT32 value )
{
    struct decimal_formatter *impl = impl_from_decimal_formatter( iface );
    impl->fraction_digits = value;
    return S_OK;
}

static HRESULT WINAPI decimal_formatter_get_IsGroupingEnabled( void *iface, BOOLEAN *value )
{
    struct decimal_formatter *impl = impl_from_decimal_formatter( iface );
    if (!value) return E_POINTER;
    *value = impl->is_grouping_enabled;
    return S_OK;
}
static HRESULT WINAPI decimal_formatter_put_IsGroupingEnabled( void *iface, BOOLEAN value )
{
    struct decimal_formatter *impl = impl_from_decimal_formatter( iface );
    impl->is_grouping_enabled = value;
    return S_OK;
}

static HRESULT WINAPI decimal_formatter_get_IsDecimalPointAlwaysDisplayed( void *iface, BOOLEAN *value )
{
    if (value) *value = FALSE;
    return S_OK;
}
static HRESULT WINAPI decimal_formatter_put_IsDecimalPointAlwaysDisplayed( void *iface, BOOLEAN value )
{
    return S_OK;
}

static HRESULT WINAPI decimal_formatter_get_NumeralSystem( void *iface, HSTRING *value )
{
    if (!value) return E_POINTER;
    return WindowsCreateString( L"Latn", 4, value );
}
static HRESULT WINAPI decimal_formatter_put_NumeralSystem( void *iface, HSTRING value )
{
    return S_OK;
}

static HRESULT WINAPI decimal_formatter_get_ResolvedLanguage( void *iface, HSTRING *value )
{
    return decimal_formatter_get_GeographicRegion( iface, value );
}
static HRESULT WINAPI decimal_formatter_get_ResolvedGeographicRegion( void *iface, HSTRING *value )
{
    return decimal_formatter_get_GeographicRegion( iface, value );
}

static HRESULT WINAPI decimal_formatter_stub_ok( void *iface, ... )
{
    return S_OK;
}

static const void *decimal_formatter_vtbl[] =
{
    decimal_formatter_QueryInterface,
    decimal_formatter_AddRef,
    decimal_formatter_Release,
    decimal_formatter_GetIids,
    decimal_formatter_GetRuntimeClassName,
    decimal_formatter_GetTrustLevel,
    /* INumberFormatter */
    decimal_formatter_FormatInt,
    decimal_formatter_FormatUInt,
    decimal_formatter_FormatDouble,
    /* INumberFormatterOptions */
    decimal_formatter_get_Languages,
    decimal_formatter_get_GeographicRegion,
    decimal_formatter_get_IntegerDigits,
    decimal_formatter_put_IntegerDigits,
    decimal_formatter_get_FractionDigits,
    decimal_formatter_put_FractionDigits,
    decimal_formatter_get_IsGroupingEnabled,
    decimal_formatter_put_IsGroupingEnabled,
    decimal_formatter_get_IsDecimalPointAlwaysDisplayed,
    decimal_formatter_put_IsDecimalPointAlwaysDisplayed,
    decimal_formatter_get_NumeralSystem,
    decimal_formatter_put_NumeralSystem,
    decimal_formatter_get_ResolvedLanguage,
    decimal_formatter_get_ResolvedGeographicRegion,
    decimal_formatter_stub_ok, decimal_formatter_stub_ok, decimal_formatter_stub_ok,
    decimal_formatter_stub_ok, decimal_formatter_stub_ok, decimal_formatter_stub_ok,
    decimal_formatter_stub_ok, decimal_formatter_stub_ok, decimal_formatter_stub_ok,
};

static HRESULT decimal_formatter_create( void **instance )
{
    struct decimal_formatter *impl = calloc( 1, sizeof(*impl) );
    if (!impl) return E_OUTOFMEMORY;
    impl->IInspectable_iface.lpVtbl = (const struct IInspectableVtbl *)decimal_formatter_vtbl;
    impl->ref = 1;
    impl->integer_digits = 1;
    impl->fraction_digits = 0;
    impl->is_grouping_enabled = FALSE;
    *instance = &impl->IInspectable_iface;
    return S_OK;
}

/* DecimalFormatter factory */
static HRESULT WINAPI decimal_formatter_factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ) ||
        IsEqualGUID( iid, &IID_IDecimalFormatterFactory ))
    {
        *out = iface;
        IUnknown_AddRef( iface );
        return S_OK;
    }
    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI decimal_formatter_factory_AddRef( IActivationFactory *iface )
{
    return 2;
}
static ULONG WINAPI decimal_formatter_factory_Release( IActivationFactory *iface )
{
    return 1;
}
static HRESULT WINAPI decimal_formatter_factory_GetIids( IActivationFactory *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}
static HRESULT WINAPI decimal_formatter_factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.Globalization.NumberFormatting.DecimalFormatter", 55, name );
}
static HRESULT WINAPI decimal_formatter_factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}
static HRESULT WINAPI decimal_formatter_factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    return decimal_formatter_create( (void **)instance );
}
static HRESULT WINAPI decimal_formatter_factory_CreateDecimalFormatter( void *iface, void *languages, HSTRING geographicregion, void **result )
{
    return decimal_formatter_create( result );
}

static const void *decimal_formatter_factory_vtbl[] =
{
    decimal_formatter_factory_QueryInterface,
    decimal_formatter_factory_AddRef,
    decimal_formatter_factory_Release,
    decimal_formatter_factory_GetIids,
    decimal_formatter_factory_GetRuntimeClassName,
    decimal_formatter_factory_GetTrustLevel,
    decimal_formatter_factory_ActivateInstance,
    decimal_formatter_factory_CreateDecimalFormatter,
};

static struct { const void **vtbl; } decimal_formatter_factory_obj = { decimal_formatter_factory_vtbl };

HRESULT WINAPI DllGetActivationFactory(HSTRING classid, IActivationFactory **factory)
{
    const WCHAR *name = WindowsGetStringRawBuffer(classid, NULL);

    TRACE("classid %s, factory %p.\n", debugstr_hstring(classid), factory);

    *factory = NULL;

    if (!wcscmp(name, RuntimeClass_Windows_System_UserProfile_GlobalizationPreferences))
    {
        *factory = &userprofile_preferences.IActivationFactory_iface;
        IUnknown_AddRef(*factory);
    }
    else if (!wcscmp(name, RuntimeClass_Windows_Globalization_Language))
    {
        *factory = &language_factory.IActivationFactory_iface;
        IUnknown_AddRef(*factory);
    }
    else if (!wcscmp(name, RuntimeClass_Windows_Globalization_GeographicRegion))
    {
        *factory = geographic_region_factory;
        IUnknown_AddRef(*factory);
    }
    else if (!wcscmp(name, RuntimeClass_Windows_Globalization_ApplicationLanguages))
    {
        *factory = &application_languages.IActivationFactory_iface;
        IUnknown_AddRef(*factory);
    }
    else if (!wcscmp(name, L"Windows.Globalization.Fonts.LanguageFontGroup"))
    {
        *factory = (IActivationFactory *)&font_group_factory_obj;
        IUnknown_AddRef(*factory);
    }
    else if (!wcscmp(name, L"Windows.Globalization.NumberFormatting.DecimalFormatter") ||
             !wcscmp(name, L"Windows.Globalization.NumberFormatting.CurrencyFormatter") ||
             !wcscmp(name, L"Windows.Globalization.NumberFormatting.PercentFormatter"))
    {
        *factory = (IActivationFactory *)&decimal_formatter_factory_obj;
        IUnknown_AddRef(*factory);
    }

    if (*factory) return S_OK;
    return E_NOINTERFACE;
}
