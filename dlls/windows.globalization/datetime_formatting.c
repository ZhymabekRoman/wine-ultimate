/* WinRT Windows.Globalization.DateTimeFormatting and Calendar/ClockIdentifiers implementation
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

/* Windows.Globalization.CalendarIdentifiers */
static HRESULT WINAPI cal_id_get_Gregorian( void *iface, HSTRING *value )
{
    if (!value) return E_POINTER;
    return WindowsCreateString( L"GregorianCalendar", 17, value );
}
static HRESULT WINAPI cal_id_get_Hebrew( void *iface, HSTRING *value )
{
    if (!value) return E_POINTER;
    return WindowsCreateString( L"HebrewCalendar", 14, value );
}
static HRESULT WINAPI cal_id_get_Hijri( void *iface, HSTRING *value )
{
    if (!value) return E_POINTER;
    return WindowsCreateString( L"HijriCalendar", 13, value );
}
static HRESULT WINAPI cal_id_get_Japanese( void *iface, HSTRING *value )
{
    if (!value) return E_POINTER;
    return WindowsCreateString( L"JapaneseCalendar", 16, value );
}
static HRESULT WINAPI cal_id_get_Julian( void *iface, HSTRING *value )
{
    if (!value) return E_POINTER;
    return WindowsCreateString( L"JulianCalendar", 14, value );
}
static HRESULT WINAPI cal_id_get_Korean( void *iface, HSTRING *value )
{
    if (!value) return E_POINTER;
    return WindowsCreateString( L"KoreanCalendar", 14, value );
}
static HRESULT WINAPI cal_id_get_Taiwan( void *iface, HSTRING *value )
{
    if (!value) return E_POINTER;
    return WindowsCreateString( L"TaiwanCalendar", 14, value );
}
static HRESULT WINAPI cal_id_get_Thai( void *iface, HSTRING *value )
{
    if (!value) return E_POINTER;
    return WindowsCreateString( L"ThaiCalendar", 12, value );
}
static HRESULT WINAPI cal_id_get_UmAlQura( void *iface, HSTRING *value )
{
    if (!value) return E_POINTER;
    return WindowsCreateString( L"UmAlQuraCalendar", 16, value );
}
static HRESULT WINAPI cal_id_get_Persian( void *iface, HSTRING *value )
{
    if (!value) return E_POINTER;
    return WindowsCreateString( L"PersianCalendar", 15, value );
}

static const GUID IID_ICalendarIdentifiersStatics_local =
    { 0x80653f68, 0x2cb2, 0x4c1f, { 0xb5, 0x90, 0xf0, 0xf5, 0x2b, 0xf4, 0xfd, 0x1a } };
static const GUID IID_ICalendarIdentifiersStatics2_local =
    { 0x7df4d488, 0x5fd0, 0x42a7, { 0x95, 0xb5, 0x7d, 0x98, 0xd8, 0x23, 0x07, 0x5f } };
static const GUID IID_ICalendarIdentifiersStatics3_local =
    { 0x2c225423, 0x1fad, 0x40c0, { 0x93, 0x34, 0xa8, 0xeb, 0x90, 0xdb, 0x04, 0xf5 } };

static const void *cal_id_factory_vtbl[];
static const void *cal_id2_factory_vtbl[];
static struct { const void **vtbl; const void **vtbl2; } cal_id_factory_obj = { cal_id_factory_vtbl, cal_id2_factory_vtbl };

static HRESULT WINAPI cal_id_QueryInterface( void *iface, REFIID iid, void **out )
{
    TRACE( "cal_id_QI: iid %s\n", debugstr_guid(iid) );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ) ||
        IsEqualGUID( iid, &IID_ICalendarIdentifiersStatics_local ) ||
        IsEqualGUID( iid, &IID_ICalendarIdentifiersStatics3_local ))
    {
        *out = &cal_id_factory_obj.vtbl;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_ICalendarIdentifiersStatics2_local ))
    {
        *out = &cal_id_factory_obj.vtbl2;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI cal_id_AddRef( void *iface ) { return 2; }
static ULONG WINAPI cal_id_Release( void *iface ) { return 1; }
static HRESULT WINAPI cal_id_GetIids( void *iface, ULONG *count, IID **iids )
{ if (count) *count = 0; if (iids) *iids = NULL; return S_OK; }
static HRESULT WINAPI cal_id_GetRuntimeClassName( void *iface, HSTRING *name )
{ return WindowsCreateString( L"Windows.Globalization.CalendarIdentifiers", 41, name ); }
static HRESULT WINAPI cal_id_GetTrustLevel( void *iface, TrustLevel *level )
{ if (level) *level = BaseTrust; return S_OK; }

static const void *cal_id_factory_vtbl[] =
{
    cal_id_QueryInterface,
    cal_id_AddRef,
    cal_id_Release,
    cal_id_GetIids,
    cal_id_GetRuntimeClassName,
    cal_id_GetTrustLevel,
    cal_id_get_Gregorian,
    cal_id_get_Hebrew,
    cal_id_get_Hijri,
    cal_id_get_Japanese,
    cal_id_get_Julian,
    cal_id_get_Korean,
    cal_id_get_Taiwan,
    cal_id_get_Thai,
    cal_id_get_UmAlQura,
    cal_id_get_Persian,
    cal_id_get_Gregorian,
    cal_id_get_Gregorian,
};
static const void *cal_id2_factory_vtbl[] =
{
    cal_id_QueryInterface,
    cal_id_AddRef,
    cal_id_Release,
    cal_id_GetIids,
    cal_id_GetRuntimeClassName,
    cal_id_GetTrustLevel,
    cal_id_get_Persian,
};

IActivationFactory *calendar_identifiers_factory = (IActivationFactory *)&cal_id_factory_obj.vtbl;

/* Windows.Globalization.ClockIdentifiers */
static HRESULT WINAPI clk_id_GetRuntimeClassName( void *iface, HSTRING *name )
{ return WindowsCreateString( L"Windows.Globalization.ClockIdentifiers", 38, name ); }
static HRESULT WINAPI clk_id_get_TwelveHour( void *iface, HSTRING *val )
{ return WindowsCreateString( L"12HourClock", 11, val ); }
static HRESULT WINAPI clk_id_get_TwentyFourHour( void *iface, HSTRING *val )
{ return WindowsCreateString( L"24HourClock", 11, val ); }

static const void *clk_id_factory_vtbl[];
static struct { const void **vtbl; } clk_id_factory_obj = { clk_id_factory_vtbl };
static const GUID IID_IClockIdentifiersStatics_local =
    { 0x523805bb, 0x12ec, 0x4f83, { 0xbc, 0x31, 0xb1, 0xb4, 0x37, 0x6b, 0x08, 0x08 } };

static HRESULT WINAPI clk_id_QueryInterface( void *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ) ||
        IsEqualGUID( iid, &IID_IClockIdentifiersStatics_local ))
    {
        *out = &clk_id_factory_obj;
        return S_OK;
    }
    if (!IsEqualGUID( iid, &IID_IReferenceTracker_glob ) &&
        !IsEqualGUID( iid, &IID_IReferenceTrackerTarget_glob ))
        FIXME( "clk_id: iid %s not implemented\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}
static const void *clk_id_factory_vtbl[] =
{
    clk_id_QueryInterface,
    cal_id_AddRef,
    cal_id_Release,
    cal_id_GetIids,
    clk_id_GetRuntimeClassName,
    cal_id_GetTrustLevel,
    clk_id_get_TwelveHour,
    clk_id_get_TwentyFourHour,
};

IActivationFactory *clock_identifiers_factory = (IActivationFactory *)&clk_id_factory_obj;

/* Windows.Globalization.DateTimeFormatting.DateTimeFormatter */
static const GUID IID_IDateTimeFormatterFactory_local =
    { 0xec8d8a53, 0x1a2e, 0x412d, { 0x88, 0x15, 0x3b, 0x74, 0x5f, 0xb1, 0xa2, 0xa0 } };
static const GUID IID_IDateTimeFormatterStatics_local =
    { 0xbfcde7c0, 0xdf4c, 0x4a2e, { 0x90, 0x12, 0xf4, 0x7d, 0xaf, 0x3f, 0x12, 0x12 } };
static const GUID IID_IDateTimeFormatter2_local =
    { 0x27c91a86, 0xbdaa, 0x4fd0, { 0x9e, 0x36, 0x67, 0x1d, 0x5a, 0xf5, 0xee, 0x03 } };

struct dt_formatter
{
    const void **vtbl;
    const void **vtbl2;
    LONG ref;
    WCHAR *tmpl;
};

static inline struct dt_formatter *dt_from_vtbl2( void *iface )
{
    return CONTAINING_RECORD( iface, struct dt_formatter, vtbl2 );
}

static const GUID IID_IDateTimeFormatter_local =
    { 0x95eeca10, 0x73e0, 0x4e4b, { 0xa1, 0x83, 0x3d, 0x6a, 0xd0, 0xba, 0x35, 0xec } };

static HRESULT WINAPI dt_fmt_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct dt_formatter *impl = iface;
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IDateTimeFormatter_local ))
    {
        InterlockedIncrement( &impl->ref );
        *out = &impl->vtbl;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IDateTimeFormatter2_local ))
    {
        InterlockedIncrement( &impl->ref );
        *out = &impl->vtbl2;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static HRESULT WINAPI dt_fmt2_QueryInterface( void *iface, REFIID iid, void **out )
{
    return dt_fmt_QueryInterface( dt_from_vtbl2( iface ), iid, out );
}

static ULONG WINAPI dt_fmt_AddRef( void *iface )
{
    struct dt_formatter *impl = iface;
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI dt_fmt2_AddRef( void *iface )
{
    return dt_fmt_AddRef( dt_from_vtbl2( iface ) );
}

static ULONG WINAPI dt_fmt_Release( void *iface )
{
    struct dt_formatter *impl = iface;
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        free( impl->tmpl );
        free( impl );
    }
    return ref;
}

static ULONG WINAPI dt_fmt2_Release( void *iface )
{
    return dt_fmt_Release( dt_from_vtbl2( iface ) );
}

static HRESULT WINAPI dt_fmt_GetRuntimeClassName( void *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.Globalization.DateTimeFormatting.DateTimeFormatter", 56, name );
}

static HRESULT format_datetime_helper( struct dt_formatter *impl, INT64 universal_time, HSTRING *result )
{
    FILETIME ft;
    SYSTEMTIME st;
    WCHAR buf[128];
    const WCHAR *t = impl->tmpl ? impl->tmpl : L"";
    int len = 0;

    if (!result) return E_POINTER;
    ft.dwLowDateTime = (DWORD)(universal_time & 0xffffffff);
    ft.dwHighDateTime = (DWORD)((universal_time >> 32) & 0xffffffff);
    if (!FileTimeToSystemTime( &ft, &st ))
        GetLocalTime( &st );

    if (wcsstr( t, L"hour" ) || wcsstr( t, L"minute" ) || wcsstr( t, L"second" ) || wcsstr( t, L"shorttime" ) || wcsstr( t, L"longtime" ))
        len = GetTimeFormatW( LOCALE_USER_DEFAULT, 0, &st, NULL, buf, ARRAY_SIZE(buf) );
    else if (wcsstr( t, L"year" ) || wcsstr( t, L"month" ) || wcsstr( t, L"day" ) || wcsstr( t, L"shortdate" ) || wcsstr( t, L"longdate" ))
        len = GetDateFormatW( LOCALE_USER_DEFAULT, 0, &st, NULL, buf, ARRAY_SIZE(buf) );

    if (len <= 1)
    {
        len = swprintf( buf, ARRAY_SIZE(buf), L"%04u-%02u-%02u %02u:%02u:%02u",
                        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond );
    }
    else
    {
        len--; /* GetTimeFormatW/GetDateFormatW includes NUL */
    }
    return WindowsCreateString( buf, len, result );
}

static HRESULT WINAPI dt_fmt_get_EmptyVec( void *iface, void **value )
{
    if (value) *value = NULL;
    return S_OK;
}

static HRESULT WINAPI dt_fmt_get_GeographicRegion( void *iface, HSTRING *value )
{
    return WindowsCreateString( L"US", 2, value );
}

static HRESULT WINAPI dt_fmt_get_Calendar( void *iface, HSTRING *value )
{
    return WindowsCreateString( L"GregorianCalendar", 17, value );
}

static HRESULT WINAPI dt_fmt_get_Clock( void *iface, HSTRING *value )
{
    return WindowsCreateString( L"12HourClock", 11, value );
}

static HRESULT WINAPI dt_fmt_get_NumeralSystem( void *iface, HSTRING *value )
{
    return WindowsCreateString( L"Latn", 4, value );
}

static HRESULT WINAPI dt_fmt_put_NumeralSystem( void *iface, HSTRING value )
{
    return S_OK;
}

static HRESULT WINAPI dt_fmt_get_Template( void *iface, HSTRING *value )
{
    struct dt_formatter *impl = iface;
    const WCHAR *t = impl->tmpl ? impl->tmpl : L"";
    return WindowsCreateString( t, wcslen(t), value );
}

static HRESULT WINAPI dt_fmt_Format( void *iface, INT64 value, HSTRING *result )
{
    return format_datetime_helper( iface, value, result );
}

static HRESULT WINAPI dt_fmt_get_IncludeZero( void *iface, int *value )
{
    if (value) *value = 0;
    return S_OK;
}

static HRESULT WINAPI dt_fmt_get_ResolvedLanguage( void *iface, HSTRING *value )
{
    return WindowsCreateString( L"en-US", 5, value );
}

static HRESULT WINAPI dt_fmt2_FormatUsingTimeZone( void *iface, INT64 value, HSTRING tz, HSTRING *result )
{
    return format_datetime_helper( dt_from_vtbl2( iface ), value, result );
}

static const void *dt_fmt_vtbl[] =
{
    dt_fmt_QueryInterface,
    dt_fmt_AddRef,
    dt_fmt_Release,
    cal_id_GetIids,
    dt_fmt_GetRuntimeClassName,
    cal_id_GetTrustLevel,
    dt_fmt_get_EmptyVec,        /* 6: get_Languages */
    dt_fmt_get_GeographicRegion,/* 7: get_GeographicRegion */
    dt_fmt_get_Calendar,        /* 8: get_Calendar */
    dt_fmt_get_Clock,           /* 9: get_Clock */
    dt_fmt_get_NumeralSystem,   /* 10: get_NumeralSystem */
    dt_fmt_put_NumeralSystem,   /* 11: put_NumeralSystem */
    dt_fmt_get_EmptyVec,        /* 12: get_Patterns */
    dt_fmt_get_Template,        /* 13: get_Template */
    dt_fmt_Format,              /* 14: Format */
    dt_fmt_get_IncludeZero,     /* 15: get_IncludeYear */
    dt_fmt_get_IncludeZero,     /* 16: get_IncludeMonth */
    dt_fmt_get_IncludeZero,     /* 17: get_IncludeDayOfWeek */
    dt_fmt_get_IncludeZero,     /* 18: get_IncludeDay */
    dt_fmt_get_IncludeZero,     /* 19: get_IncludeHour */
    dt_fmt_get_IncludeZero,     /* 20: get_IncludeMinute */
    dt_fmt_get_IncludeZero,     /* 21: get_IncludeSecond */
    dt_fmt_get_ResolvedLanguage,/* 22: get_ResolvedLanguage */
    dt_fmt_get_GeographicRegion,/* 23: get_ResolvedGeographicRegion */
};

static const void *dt_fmt2_vtbl[] =
{
    dt_fmt2_QueryInterface,
    dt_fmt2_AddRef,
    dt_fmt2_Release,
    cal_id_GetIids,
    dt_fmt_GetRuntimeClassName,
    cal_id_GetTrustLevel,
    dt_fmt2_FormatUsingTimeZone,
};

static HRESULT dt_formatter_create( const WCHAR *tmpl, void **out )
{
    struct dt_formatter *obj;
    if (!out) return E_POINTER;
    if (!(obj = calloc( 1, sizeof(*obj) ))) return E_OUTOFMEMORY;
    obj->vtbl = dt_fmt_vtbl;
    obj->vtbl2 = dt_fmt2_vtbl;
    obj->ref = 1;
    obj->tmpl = wcsdup( tmpl ? tmpl : L"" );
    *out = &obj->vtbl;
    return S_OK;
}

static HRESULT WINAPI dt_factory_Create( void *iface, HSTRING tmpl, void **out )
{
    return dt_formatter_create( WindowsGetStringRawBuffer( tmpl, NULL ), out );
}

static HRESULT WINAPI dt_factory_CreateLanguages( void *iface, HSTRING tmpl, void *langs, void **out )
{
    return dt_formatter_create( WindowsGetStringRawBuffer( tmpl, NULL ), out );
}

static HRESULT WINAPI dt_factory_CreateContext( void *iface, HSTRING tmpl, void *langs, HSTRING region, HSTRING cal, HSTRING clk, void **out )
{
    return dt_formatter_create( WindowsGetStringRawBuffer( tmpl, NULL ), out );
}

static HRESULT WINAPI dt_factory_CreateParts( void *iface, int y, int m, int d, int dow, int h, int min, int s, void *langs, void **out )
{
    return dt_formatter_create( L"hour minute", out );
}

static HRESULT WINAPI dt_factory_CreatePartsContext( void *iface, int y, int m, int d, int dow, int h, int min, int s, void *langs, HSTRING region, HSTRING cal, HSTRING clk, void **out )
{
    return dt_formatter_create( L"hour minute", out );
}

static HRESULT WINAPI dt_statics_get_LongDate( void *iface, void **out )
{
    return dt_formatter_create( L"longdate", out );
}

static HRESULT WINAPI dt_statics_get_ShortDate( void *iface, void **out )
{
    return dt_formatter_create( L"shortdate", out );
}

static HRESULT WINAPI dt_statics_get_LongTime( void *iface, void **out )
{
    return dt_formatter_create( L"longtime", out );
}

static HRESULT WINAPI dt_statics_get_ShortTime( void *iface, void **out )
{
    return dt_formatter_create( L"shorttime", out );
}

static const void *dt_factory_act_vtbl[];
static const void *dt_factory_fac_vtbl[];
static const void *dt_factory_stat_vtbl[];
static struct {
    const void **vtbl_act;
    const void **vtbl_fac;
    const void **vtbl_stat;
} dt_factory_obj = { dt_factory_act_vtbl, dt_factory_fac_vtbl, dt_factory_stat_vtbl };

static HRESULT WINAPI dt_factory_QueryInterface( void *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = &dt_factory_obj.vtbl_act;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IDateTimeFormatterFactory_local ))
    {
        *out = &dt_factory_obj.vtbl_fac;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IDateTimeFormatterStatics_local ))
    {
        *out = &dt_factory_obj.vtbl_stat;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static HRESULT WINAPI dt_factory_ActivateInstance( void *iface, void **out )
{
    return dt_formatter_create( L"shorttime", out );
}

static const void *dt_factory_act_vtbl[] =
{
    dt_factory_QueryInterface,
    cal_id_AddRef,
    cal_id_Release,
    cal_id_GetIids,
    dt_fmt_GetRuntimeClassName,
    cal_id_GetTrustLevel,
    dt_factory_ActivateInstance,
};

static const void *dt_factory_fac_vtbl[] =
{
    dt_factory_QueryInterface,
    cal_id_AddRef,
    cal_id_Release,
    cal_id_GetIids,
    dt_fmt_GetRuntimeClassName,
    cal_id_GetTrustLevel,
    dt_factory_Create,
    dt_factory_CreateLanguages,
    dt_factory_CreateContext,
    dt_factory_CreateParts,
    dt_factory_CreatePartsContext,
};

static const void *dt_factory_stat_vtbl[] =
{
    dt_factory_QueryInterface,
    cal_id_AddRef,
    cal_id_Release,
    cal_id_GetIids,
    dt_fmt_GetRuntimeClassName,
    cal_id_GetTrustLevel,
    dt_statics_get_LongDate,
    dt_statics_get_ShortDate,
    dt_statics_get_LongTime,
    dt_statics_get_ShortTime,
};

IActivationFactory *datetime_formatter_factory = (IActivationFactory *)&dt_factory_obj.vtbl_act;
