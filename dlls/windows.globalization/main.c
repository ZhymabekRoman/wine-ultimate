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

#include "private.h"

WINE_DEFAULT_DEBUG_CHANNEL(locale);

const GUID IID_IReferenceTrackerTarget_glob =
    { 0x11d3b13a, 0x180e, 0x4789, { 0xa8, 0xbe, 0x77, 0x12, 0x88, 0x28, 0x93, 0xe6 } };
const GUID IID_IReferenceTracker_glob =
    { 0x11d3b13a, 0x180e, 0x4789, { 0xa8, 0xbe, 0x77, 0x12, 0x88, 0x28, 0x93, 0xe5 } };

HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void **out)
{
    FIXME("clsid %s, riid %s, out %p stub!\n", debugstr_guid(clsid), debugstr_guid(riid), out);
    return CLASS_E_CLASSNOTAVAILABLE;
}

HRESULT WINAPI DllGetActivationFactory(HSTRING classid, IActivationFactory **factory)
{
    const WCHAR *name = WindowsGetStringRawBuffer(classid, NULL);

    TRACE("classid %s, factory %p.\n", debugstr_hstring(classid), factory);

    *factory = NULL;

    if (!wcscmp(name, RuntimeClass_Windows_System_UserProfile_GlobalizationPreferences))
    {
        *factory = userprofile_preferences_factory;
        IUnknown_AddRef(*factory);
    }
    else if (!wcscmp(name, RuntimeClass_Windows_Globalization_Language))
    {
        *factory = language_activation_factory;
        IUnknown_AddRef(*factory);
    }
    else if (!wcscmp(name, RuntimeClass_Windows_Globalization_GeographicRegion))
    {
        *factory = geographic_region_factory;
        IUnknown_AddRef(*factory);
    }
    else if (!wcscmp(name, RuntimeClass_Windows_Globalization_ApplicationLanguages))
    {
        *factory = application_languages_factory;
        IUnknown_AddRef(*factory);
    }
    else if (!wcscmp(name, L"Windows.Globalization.Fonts.LanguageFontGroup"))
    {
        *factory = font_group_factory;
        IUnknown_AddRef(*factory);
    }
    else if (!wcscmp(name, L"Windows.Globalization.CalendarIdentifiers"))
    {
        *factory = calendar_identifiers_factory;
    }
    else if (!wcscmp(name, L"Windows.Globalization.ClockIdentifiers"))
    {
        *factory = clock_identifiers_factory;
    }
    else if (!wcscmp(name, L"Windows.Globalization.NumberFormatting.DecimalFormatter"))
    {
        *factory = decimal_formatter_factory;
        IUnknown_AddRef(*factory);
    }
    else if (!wcscmp(name, L"Windows.Globalization.NumberFormatting.CurrencyFormatter"))
    {
        *factory = currency_formatter_factory;
        IUnknown_AddRef(*factory);
    }
    else if (!wcscmp(name, L"Windows.Globalization.NumberFormatting.PercentFormatter"))
    {
        *factory = percent_formatter_factory;
        IUnknown_AddRef(*factory);
    }
    else if (!wcscmp(name, L"Windows.Globalization.NumberFormatting.PermilleFormatter"))
    {
        *factory = permille_formatter_factory;
        IUnknown_AddRef(*factory);
    }
    else if (!wcscmp(name, L"Windows.Globalization.NumberFormatting.IncrementNumberRounder"))
    {
        *factory = increment_rounder_factory;
    }
    else if (!wcscmp(name, L"Windows.Globalization.NumberFormatting.SignificantDigitsNumberRounder"))
    {
        *factory = significant_digits_rounder_factory;
    }
    else if (!wcscmp(name, L"Windows.Globalization.DateTimeFormatting.DateTimeFormatter"))
    {
        *factory = datetime_formatter_factory;
    }
    else if (!wcscmp(name, L"Windows.Globalization.Calendar"))
    {
        *factory = calendar_factory;
        IUnknown_AddRef(*factory);
    }

    if (*factory) return S_OK;
    return E_NOINTERFACE;
}
