/* WinRT Windows.Globalization.Calendar Implementation
 *
 * Copyright (C) 2026 Wine project
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
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(glob);

static const GUID IID_IReferenceTracker_cal =
    { 0xae927363, 0x74e4, 0x47e7, { 0xab, 0x5e, 0x6e, 0x97, 0xd6, 0xc2, 0x88, 0x5b } };
static const GUID IID_IReferenceTrackerTarget_cal =
    { 0x11d3b13a, 0x180e, 0x4789, { 0xa8, 0xbe, 0x77, 0x12, 0x88, 0x28, 0x93, 0xe6 } };

struct calendar_obj
{
    ICalendar ICalendar_iface;
    ITimeZoneOnCalendar ITimeZoneOnCalendar_iface;
    LONG ref;
    INT64 utc_ticks; /* FILETIME 100ns ticks */
    WCHAR calendar_system[64];
    WCHAR clock[32];
    WCHAR numeral_system[32];
    WCHAR time_zone[128];
};

static inline struct calendar_obj *impl_from_ICalendar( ICalendar *iface )
{
    return CONTAINING_RECORD( iface, struct calendar_obj, ICalendar_iface );
}

static inline struct calendar_obj *impl_from_ITimeZoneOnCalendar( ITimeZoneOnCalendar *iface )
{
    return CONTAINING_RECORD( iface, struct calendar_obj, ITimeZoneOnCalendar_iface );
}

static void cal_get_systime( struct calendar_obj *impl, SYSTEMTIME *st )
{
    FILETIME ft, lft;
    ft.dwLowDateTime = (DWORD)(impl->utc_ticks & 0xffffffff);
    ft.dwHighDateTime = (DWORD)((impl->utc_ticks >> 32) & 0xffffffff);
    if (!FileTimeToLocalFileTime( &ft, &lft )) lft = ft;
    if (!FileTimeToSystemTime( &lft, st )) GetLocalTime( st );
}

static void cal_set_systime( struct calendar_obj *impl, const SYSTEMTIME *st )
{
    FILETIME lft, ft;
    if (SystemTimeToFileTime( st, &lft ) && LocalFileTimeToFileTime( &lft, &ft ))
        impl->utc_ticks = ((INT64)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
}

static HRESULT create_calendar( const WCHAR *cal, const WCHAR *clk, const WCHAR *tz, ICalendar **out );

static HRESULT WINAPI calendar_QueryInterface( ICalendar *iface, REFIID iid, void **out )
{
    struct calendar_obj *impl = impl_from_ICalendar( iface );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IReferenceTracker_cal ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget_cal ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_ICalendar ))
    {
        *out = &impl->ICalendar_iface;
        ICalendar_AddRef( *out );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_ITimeZoneOnCalendar ))
    {
        *out = &impl->ITimeZoneOnCalendar_iface;
        ITimeZoneOnCalendar_AddRef( *out );
        return S_OK;
    }
    FIXME( "iid %s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI calendar_AddRef( ICalendar *iface )
{
    struct calendar_obj *impl = impl_from_ICalendar( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI calendar_Release( ICalendar *iface )
{
    struct calendar_obj *impl = impl_from_ICalendar( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref) free( impl );
    return ref;
}

static HRESULT WINAPI calendar_GetIids( ICalendar *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI calendar_GetRuntimeClassName( ICalendar *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.Globalization.Calendar", 30, name );
}

static HRESULT WINAPI calendar_GetTrustLevel( ICalendar *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI calendar_Clone( ICalendar *iface, ICalendar **value )
{
    struct calendar_obj *impl = impl_from_ICalendar( iface );
    struct calendar_obj *clone;
    if (!value) return E_POINTER;
    if (!(clone = calloc( 1, sizeof(*clone) ))) return E_OUTOFMEMORY;
    *clone = *impl;
    clone->ref = 1;
    *value = &clone->ICalendar_iface;
    return S_OK;
}

static HRESULT WINAPI calendar_SetToMin( ICalendar *iface )
{
    struct calendar_obj *impl = impl_from_ICalendar( iface );
    impl->utc_ticks = 0;
    return S_OK;
}

static HRESULT WINAPI calendar_SetToMax( ICalendar *iface )
{
    struct calendar_obj *impl = impl_from_ICalendar( iface );
    impl->utc_ticks = 0x2cba000000000000LL;
    return S_OK;
}

static HRESULT WINAPI calendar_get_Languages( ICalendar *iface, IVectorView_HSTRING **value )
{
    if (value) *value = NULL;
    return S_OK;
}

static HRESULT WINAPI calendar_get_NumeralSystem( ICalendar *iface, HSTRING *value )
{
    struct calendar_obj *impl = impl_from_ICalendar( iface );
    return WindowsCreateString( impl->numeral_system, wcslen(impl->numeral_system), value );
}

static HRESULT WINAPI calendar_put_NumeralSystem( ICalendar *iface, HSTRING value )
{
    struct calendar_obj *impl = impl_from_ICalendar( iface );
    const WCHAR *s = WindowsGetStringRawBuffer( value, NULL );
    lstrcpynW( impl->numeral_system, s ? s : L"Latn", ARRAY_SIZE(impl->numeral_system) );
    return S_OK;
}

static HRESULT WINAPI calendar_GetCalendarSystem( ICalendar *iface, HSTRING *value )
{
    struct calendar_obj *impl = impl_from_ICalendar( iface );
    return WindowsCreateString( impl->calendar_system, wcslen(impl->calendar_system), value );
}

static HRESULT WINAPI calendar_ChangeCalendarSystem( ICalendar *iface, HSTRING value )
{
    struct calendar_obj *impl = impl_from_ICalendar( iface );
    const WCHAR *s = WindowsGetStringRawBuffer( value, NULL );
    lstrcpynW( impl->calendar_system, s ? s : L"GregorianCalendar", ARRAY_SIZE(impl->calendar_system) );
    return S_OK;
}

static HRESULT WINAPI calendar_GetClock( ICalendar *iface, HSTRING *value )
{
    struct calendar_obj *impl = impl_from_ICalendar( iface );
    return WindowsCreateString( impl->clock, wcslen(impl->clock), value );
}

static HRESULT WINAPI calendar_ChangeClock( ICalendar *iface, HSTRING value )
{
    struct calendar_obj *impl = impl_from_ICalendar( iface );
    const WCHAR *s = WindowsGetStringRawBuffer( value, NULL );
    lstrcpynW( impl->clock, s ? s : L"12HourClock", ARRAY_SIZE(impl->clock) );
    return S_OK;
}

static HRESULT WINAPI calendar_GetDateTime( ICalendar *iface, DateTime *result )
{
    struct calendar_obj *impl = impl_from_ICalendar( iface );
    if (!result) return E_POINTER;
    result->UniversalTime = impl->utc_ticks;
    return S_OK;
}

static HRESULT WINAPI calendar_SetDateTime( ICalendar *iface, DateTime value )
{
    struct calendar_obj *impl = impl_from_ICalendar( iface );
    impl->utc_ticks = value.UniversalTime;
    return S_OK;
}

static HRESULT WINAPI calendar_SetToNow( ICalendar *iface )
{
    struct calendar_obj *impl = impl_from_ICalendar( iface );
    FILETIME ft;
    GetSystemTimeAsFileTime( &ft );
    impl->utc_ticks = ((INT64)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    return S_OK;
}

static HRESULT WINAPI calendar_get_FirstEra( ICalendar *iface, INT32 *value ) { if (value) *value = 1; return S_OK; }
static HRESULT WINAPI calendar_get_LastEra( ICalendar *iface, INT32 *value ) { if (value) *value = 1; return S_OK; }
static HRESULT WINAPI calendar_get_NumberOfEras( ICalendar *iface, INT32 *value ) { if (value) *value = 1; return S_OK; }
static HRESULT WINAPI calendar_get_Era( ICalendar *iface, INT32 *value ) { if (value) *value = 1; return S_OK; }
static HRESULT WINAPI calendar_put_Era( ICalendar *iface, INT32 value ) { return S_OK; }
static HRESULT WINAPI calendar_AddEras( ICalendar *iface, INT32 eras ) { return S_OK; }
static HRESULT WINAPI calendar_EraAsFullString( ICalendar *iface, HSTRING *result ) { return WindowsCreateString( L"A.D.", 4, result ); }
static HRESULT WINAPI calendar_EraAsString( ICalendar *iface, INT32 ideal_length, HSTRING *result ) { return WindowsCreateString( L"AD", 2, result ); }

static HRESULT WINAPI calendar_get_FirstYearInThisEra( ICalendar *iface, INT32 *value ) { if (value) *value = 1; return S_OK; }
static HRESULT WINAPI calendar_get_LastYearInThisEra( ICalendar *iface, INT32 *value ) { if (value) *value = 9999; return S_OK; }
static HRESULT WINAPI calendar_get_NumberOfYearsInThisEra( ICalendar *iface, INT32 *value ) { if (value) *value = 9999; return S_OK; }

static HRESULT WINAPI calendar_get_Year( ICalendar *iface, INT32 *value )
{
    SYSTEMTIME st;
    if (!value) return E_POINTER;
    cal_get_systime( impl_from_ICalendar( iface ), &st );
    *value = st.wYear;
    return S_OK;
}

static HRESULT WINAPI calendar_put_Year( ICalendar *iface, INT32 value )
{
    struct calendar_obj *impl = impl_from_ICalendar( iface );
    SYSTEMTIME st;
    cal_get_systime( impl, &st );
    st.wYear = (WORD)value;
    cal_set_systime( impl, &st );
    return S_OK;
}

static HRESULT WINAPI calendar_AddYears( ICalendar *iface, INT32 years )
{
    struct calendar_obj *impl = impl_from_ICalendar( iface );
    SYSTEMTIME st;
    cal_get_systime( impl, &st );
    st.wYear = (WORD)(st.wYear + years);
    cal_set_systime( impl, &st );
    return S_OK;
}

static HRESULT int_to_hstring( int val, int pad, HSTRING *out )
{
    WCHAR buf[32];
    int len = swprintf( buf, ARRAY_SIZE(buf), L"%0*d", pad, val );
    return WindowsCreateString( buf, len, out );
}

static HRESULT WINAPI calendar_YearAsString( ICalendar *iface, HSTRING *result )
{
    INT32 y = 2026;
    calendar_get_Year( iface, &y );
    return int_to_hstring( y, 1, result );
}

static HRESULT WINAPI calendar_YearAsTruncatedString( ICalendar *iface, INT32 rem, HSTRING *result )
{
    return calendar_YearAsString( iface, result );
}

static HRESULT WINAPI calendar_YearAsPaddedString( ICalendar *iface, INT32 min_digits, HSTRING *result )
{
    INT32 y = 2026;
    calendar_get_Year( iface, &y );
    return int_to_hstring( y, min_digits, result );
}

static HRESULT WINAPI calendar_get_FirstMonthInThisYear( ICalendar *iface, INT32 *value ) { if (value) *value = 1; return S_OK; }
static HRESULT WINAPI calendar_get_LastMonthInThisYear( ICalendar *iface, INT32 *value ) { if (value) *value = 12; return S_OK; }
static HRESULT WINAPI calendar_get_NumberOfMonthsInThisYear( ICalendar *iface, INT32 *value ) { if (value) *value = 12; return S_OK; }

static HRESULT WINAPI calendar_get_Month( ICalendar *iface, INT32 *value )
{
    SYSTEMTIME st;
    if (!value) return E_POINTER;
    cal_get_systime( impl_from_ICalendar( iface ), &st );
    *value = st.wMonth;
    return S_OK;
}

static HRESULT WINAPI calendar_put_Month( ICalendar *iface, INT32 value )
{
    struct calendar_obj *impl = impl_from_ICalendar( iface );
    SYSTEMTIME st;
    cal_get_systime( impl, &st );
    st.wMonth = (WORD)value;
    cal_set_systime( impl, &st );
    return S_OK;
}

static HRESULT WINAPI calendar_AddMonths( ICalendar *iface, INT32 months )
{
    struct calendar_obj *impl = impl_from_ICalendar( iface );
    SYSTEMTIME st;
    int m;
    cal_get_systime( impl, &st );
    m = (st.wYear * 12 + (st.wMonth - 1)) + months;
    st.wYear = (WORD)(m / 12);
    st.wMonth = (WORD)((m % 12) + 1);
    cal_set_systime( impl, &st );
    return S_OK;
}

static HRESULT WINAPI calendar_MonthAsNumericString( ICalendar *iface, HSTRING *result )
{
    INT32 m = 1;
    calendar_get_Month( iface, &m );
    return int_to_hstring( m, 1, result );
}

static HRESULT WINAPI calendar_MonthAsFullString( ICalendar *iface, HSTRING *result ) { return calendar_MonthAsNumericString( iface, result ); }
static HRESULT WINAPI calendar_MonthAsString( ICalendar *iface, INT32 ideal, HSTRING *result ) { return calendar_MonthAsNumericString( iface, result ); }
static HRESULT WINAPI calendar_MonthAsFullSoloString( ICalendar *iface, HSTRING *result ) { return calendar_MonthAsNumericString( iface, result ); }
static HRESULT WINAPI calendar_MonthAsSoloString( ICalendar *iface, INT32 ideal, HSTRING *result ) { return calendar_MonthAsNumericString( iface, result ); }

static HRESULT WINAPI calendar_MonthAsPaddedNumericString( ICalendar *iface, INT32 min_digits, HSTRING *result )
{
    INT32 m = 1;
    calendar_get_Month( iface, &m );
    return int_to_hstring( m, min_digits, result );
}

static HRESULT WINAPI calendar_AddWeeks( ICalendar *iface, INT32 weeks )
{
    struct calendar_obj *impl = impl_from_ICalendar( iface );
    impl->utc_ticks += (INT64)weeks * 7 * 86400 * 10000000LL;
    return S_OK;
}

static HRESULT WINAPI calendar_get_FirstDayInThisMonth( ICalendar *iface, INT32 *value ) { if (value) *value = 1; return S_OK; }
static HRESULT WINAPI calendar_get_LastDayInThisMonth( ICalendar *iface, INT32 *value ) { if (value) *value = 30; return S_OK; }
static HRESULT WINAPI calendar_get_NumberOfDaysInThisMonth( ICalendar *iface, INT32 *value ) { if (value) *value = 30; return S_OK; }

static HRESULT WINAPI calendar_get_Day( ICalendar *iface, INT32 *value )
{
    SYSTEMTIME st;
    if (!value) return E_POINTER;
    cal_get_systime( impl_from_ICalendar( iface ), &st );
    *value = st.wDay;
    return S_OK;
}

static HRESULT WINAPI calendar_put_Day( ICalendar *iface, INT32 value )
{
    struct calendar_obj *impl = impl_from_ICalendar( iface );
    SYSTEMTIME st;
    cal_get_systime( impl, &st );
    st.wDay = (WORD)value;
    cal_set_systime( impl, &st );
    return S_OK;
}

static HRESULT WINAPI calendar_AddDays( ICalendar *iface, INT32 days )
{
    struct calendar_obj *impl = impl_from_ICalendar( iface );
    impl->utc_ticks += (INT64)days * 86400 * 10000000LL;
    return S_OK;
}

static HRESULT WINAPI calendar_DayAsString( ICalendar *iface, HSTRING *result )
{
    INT32 d = 1;
    calendar_get_Day( iface, &d );
    return int_to_hstring( d, 1, result );
}

static HRESULT WINAPI calendar_DayAsPaddedString( ICalendar *iface, INT32 min_digits, HSTRING *result )
{
    INT32 d = 1;
    calendar_get_Day( iface, &d );
    return int_to_hstring( d, min_digits, result );
}

static HRESULT WINAPI calendar_get_DayOfWeek( ICalendar *iface, DayOfWeek *value )
{
    SYSTEMTIME st;
    if (!value) return E_POINTER;
    cal_get_systime( impl_from_ICalendar( iface ), &st );
    *value = (DayOfWeek)st.wDayOfWeek;
    return S_OK;
}

static HRESULT WINAPI calendar_DayOfWeekAsFullString( ICalendar *iface, HSTRING *result )
{
    static const WCHAR *days[] = { L"Sunday", L"Monday", L"Tuesday", L"Wednesday", L"Thursday", L"Friday", L"Saturday" };
    DayOfWeek dow = 0;
    calendar_get_DayOfWeek( iface, &dow );
    return WindowsCreateString( days[dow % 7], wcslen(days[dow % 7]), result );
}
static HRESULT WINAPI calendar_DayOfWeekAsString( ICalendar *iface, INT32 ideal, HSTRING *result ) { return calendar_DayOfWeekAsFullString( iface, result ); }
static HRESULT WINAPI calendar_DayOfWeekAsFullSoloString( ICalendar *iface, HSTRING *result ) { return calendar_DayOfWeekAsFullString( iface, result ); }
static HRESULT WINAPI calendar_DayOfWeekAsSoloString( ICalendar *iface, INT32 ideal, HSTRING *result ) { return calendar_DayOfWeekAsFullString( iface, result ); }

static HRESULT WINAPI calendar_get_FirstPeriodInThisDay( ICalendar *iface, INT32 *value ) { if (value) *value = 1; return S_OK; }
static HRESULT WINAPI calendar_get_LastPeriodInThisDay( ICalendar *iface, INT32 *value ) { if (value) *value = 2; return S_OK; }
static HRESULT WINAPI calendar_get_NumberOfPeriodsInThisDay( ICalendar *iface, INT32 *value ) { if (value) *value = 2; return S_OK; }
static HRESULT WINAPI calendar_get_Period( ICalendar *iface, INT32 *value )
{
    SYSTEMTIME st;
    if (!value) return E_POINTER;
    cal_get_systime( impl_from_ICalendar( iface ), &st );
    *value = st.wHour >= 12 ? 2 : 1;
    return S_OK;
}
static HRESULT WINAPI calendar_put_Period( ICalendar *iface, INT32 value ) { return S_OK; }
static HRESULT WINAPI calendar_AddPeriods( ICalendar *iface, INT32 periods )
{
    struct calendar_obj *impl = impl_from_ICalendar( iface );
    impl->utc_ticks += (INT64)periods * 12 * 3600 * 10000000LL;
    return S_OK;
}
static HRESULT WINAPI calendar_PeriodAsFullString( ICalendar *iface, HSTRING *result )
{
    INT32 p = 1;
    calendar_get_Period( iface, &p );
    return WindowsCreateString( p == 2 ? L"PM" : L"AM", 2, result );
}
static HRESULT WINAPI calendar_PeriodAsString( ICalendar *iface, INT32 ideal, HSTRING *result )
{
    return calendar_PeriodAsFullString( iface, result );
}

static HRESULT WINAPI calendar_get_FirstHourInThisPeriod( ICalendar *iface, INT32 *value ) { if (value) *value = 0; return S_OK; }
static HRESULT WINAPI calendar_get_LastHourInThisPeriod( ICalendar *iface, INT32 *value ) { if (value) *value = 23; return S_OK; }
static HRESULT WINAPI calendar_get_NumberOfHoursInThisPeriod( ICalendar *iface, INT32 *value ) { if (value) *value = 24; return S_OK; }

static HRESULT WINAPI calendar_get_Hour( ICalendar *iface, INT32 *value )
{
    SYSTEMTIME st;
    if (!value) return E_POINTER;
    cal_get_systime( impl_from_ICalendar( iface ), &st );
    *value = st.wHour;
    return S_OK;
}

static HRESULT WINAPI calendar_put_Hour( ICalendar *iface, INT32 value )
{
    struct calendar_obj *impl = impl_from_ICalendar( iface );
    SYSTEMTIME st;
    cal_get_systime( impl, &st );
    st.wHour = (WORD)value;
    cal_set_systime( impl, &st );
    return S_OK;
}

static HRESULT WINAPI calendar_AddHours( ICalendar *iface, INT32 hours )
{
    struct calendar_obj *impl = impl_from_ICalendar( iface );
    impl->utc_ticks += (INT64)hours * 3600 * 10000000LL;
    return S_OK;
}

static HRESULT WINAPI calendar_HourAsString( ICalendar *iface, HSTRING *result )
{
    INT32 h = 0;
    calendar_get_Hour( iface, &h );
    return int_to_hstring( h, 1, result );
}

static HRESULT WINAPI calendar_HourAsPaddedString( ICalendar *iface, INT32 min_digits, HSTRING *result )
{
    INT32 h = 0;
    calendar_get_Hour( iface, &h );
    return int_to_hstring( h, min_digits, result );
}

static HRESULT WINAPI calendar_get_Minute( ICalendar *iface, INT32 *value )
{
    SYSTEMTIME st;
    if (!value) return E_POINTER;
    cal_get_systime( impl_from_ICalendar( iface ), &st );
    *value = st.wMinute;
    return S_OK;
}

static HRESULT WINAPI calendar_put_Minute( ICalendar *iface, INT32 value )
{
    struct calendar_obj *impl = impl_from_ICalendar( iface );
    SYSTEMTIME st;
    cal_get_systime( impl, &st );
    st.wMinute = (WORD)value;
    cal_set_systime( impl, &st );
    return S_OK;
}

static HRESULT WINAPI calendar_AddMinutes( ICalendar *iface, INT32 minutes )
{
    struct calendar_obj *impl = impl_from_ICalendar( iface );
    impl->utc_ticks += (INT64)minutes * 60 * 10000000LL;
    return S_OK;
}

static HRESULT WINAPI calendar_MinuteAsString( ICalendar *iface, HSTRING *result )
{
    INT32 m = 0;
    calendar_get_Minute( iface, &m );
    return int_to_hstring( m, 1, result );
}

static HRESULT WINAPI calendar_MinuteAsPaddedString( ICalendar *iface, INT32 min_digits, HSTRING *result )
{
    INT32 m = 0;
    calendar_get_Minute( iface, &m );
    return int_to_hstring( m, min_digits, result );
}

static HRESULT WINAPI calendar_get_Second( ICalendar *iface, INT32 *value )
{
    SYSTEMTIME st;
    if (!value) return E_POINTER;
    cal_get_systime( impl_from_ICalendar( iface ), &st );
    *value = st.wSecond;
    return S_OK;
}

static HRESULT WINAPI calendar_put_Second( ICalendar *iface, INT32 value )
{
    struct calendar_obj *impl = impl_from_ICalendar( iface );
    SYSTEMTIME st;
    cal_get_systime( impl, &st );
    st.wSecond = (WORD)value;
    cal_set_systime( impl, &st );
    return S_OK;
}

static HRESULT WINAPI calendar_AddSeconds( ICalendar *iface, INT32 seconds )
{
    struct calendar_obj *impl = impl_from_ICalendar( iface );
    impl->utc_ticks += (INT64)seconds * 10000000LL;
    return S_OK;
}

static HRESULT WINAPI calendar_SecondAsString( ICalendar *iface, HSTRING *result )
{
    INT32 s = 0;
    calendar_get_Second( iface, &s );
    return int_to_hstring( s, 1, result );
}

static HRESULT WINAPI calendar_SecondAsPaddedString( ICalendar *iface, INT32 min_digits, HSTRING *result )
{
    INT32 s = 0;
    calendar_get_Second( iface, &s );
    return int_to_hstring( s, min_digits, result );
}

static HRESULT WINAPI calendar_get_Nanosecond( ICalendar *iface, INT32 *value )
{
    struct calendar_obj *impl = impl_from_ICalendar( iface );
    if (!value) return E_POINTER;
    *value = (INT32)((impl->utc_ticks % 10000000LL) * 100);
    return S_OK;
}

static HRESULT WINAPI calendar_put_Nanosecond( ICalendar *iface, INT32 value ) { return S_OK; }
static HRESULT WINAPI calendar_AddNanoseconds( ICalendar *iface, INT32 ns )
{
    struct calendar_obj *impl = impl_from_ICalendar( iface );
    impl->utc_ticks += ns / 100;
    return S_OK;
}
static HRESULT WINAPI calendar_NanosecondAsString( ICalendar *iface, HSTRING *result ) { return WindowsCreateString( L"0", 1, result ); }
static HRESULT WINAPI calendar_NanosecondAsPaddedString( ICalendar *iface, INT32 min_digits, HSTRING *result ) { return int_to_hstring( 0, min_digits, result ); }

static HRESULT WINAPI calendar_Compare( ICalendar *iface, ICalendar *other, INT32 *result )
{
    struct calendar_obj *a = impl_from_ICalendar( iface );
    struct calendar_obj *b = impl_from_ICalendar( other );
    if (!result) return E_POINTER;
    if (!other) return E_INVALIDARG;
    *result = (a->utc_ticks < b->utc_ticks) ? -1 : (a->utc_ticks > b->utc_ticks) ? 1 : 0;
    return S_OK;
}

static HRESULT WINAPI calendar_CompareDateTime( ICalendar *iface, DateTime other, INT32 *result )
{
    struct calendar_obj *a = impl_from_ICalendar( iface );
    if (!result) return E_POINTER;
    *result = (a->utc_ticks < other.UniversalTime) ? -1 : (a->utc_ticks > other.UniversalTime) ? 1 : 0;
    return S_OK;
}

static HRESULT WINAPI calendar_CopyTo( ICalendar *iface, ICalendar *other )
{
    struct calendar_obj *a = impl_from_ICalendar( iface );
    struct calendar_obj *b = impl_from_ICalendar( other );
    if (!other) return E_INVALIDARG;
    b->utc_ticks = a->utc_ticks;
    return S_OK;
}

static HRESULT WINAPI calendar_get_FirstMinuteInThisHour( ICalendar *iface, INT32 *value ) { if (value) *value = 0; return S_OK; }
static HRESULT WINAPI calendar_get_LastMinuteInThisHour( ICalendar *iface, INT32 *value ) { if (value) *value = 59; return S_OK; }
static HRESULT WINAPI calendar_get_NumberOfMinutesInThisHour( ICalendar *iface, INT32 *value ) { if (value) *value = 60; return S_OK; }
static HRESULT WINAPI calendar_get_FirstSecondInThisMinute( ICalendar *iface, INT32 *value ) { if (value) *value = 0; return S_OK; }
static HRESULT WINAPI calendar_get_LastSecondInThisMinute( ICalendar *iface, INT32 *value ) { if (value) *value = 59; return S_OK; }
static HRESULT WINAPI calendar_get_NumberOfSecondsInThisMinute( ICalendar *iface, INT32 *value ) { if (value) *value = 60; return S_OK; }
static HRESULT WINAPI calendar_get_ResolvedLanguage( ICalendar *iface, HSTRING *value ) { return WindowsCreateString( L"en-US", 5, value ); }
static HRESULT WINAPI calendar_get_IsDaylightSavingTime( ICalendar *iface, boolean *value ) { if (value) *value = FALSE; return S_OK; }

static const struct ICalendarVtbl calendar_vtbl =
{
    calendar_QueryInterface,
    calendar_AddRef,
    calendar_Release,
    calendar_GetIids,
    calendar_GetRuntimeClassName,
    calendar_GetTrustLevel,
    calendar_Clone,
    calendar_SetToMin,
    calendar_SetToMax,
    calendar_get_Languages,
    calendar_get_NumeralSystem,
    calendar_put_NumeralSystem,
    calendar_GetCalendarSystem,
    calendar_ChangeCalendarSystem,
    calendar_GetClock,
    calendar_ChangeClock,
    calendar_GetDateTime,
    calendar_SetDateTime,
    calendar_SetToNow,
    calendar_get_FirstEra,
    calendar_get_LastEra,
    calendar_get_NumberOfEras,
    calendar_get_Era,
    calendar_put_Era,
    calendar_AddEras,
    calendar_EraAsFullString,
    calendar_EraAsString,
    calendar_get_FirstYearInThisEra,
    calendar_get_LastYearInThisEra,
    calendar_get_NumberOfYearsInThisEra,
    calendar_get_Year,
    calendar_put_Year,
    calendar_AddYears,
    calendar_YearAsString,
    calendar_YearAsTruncatedString,
    calendar_YearAsPaddedString,
    calendar_get_FirstMonthInThisYear,
    calendar_get_LastMonthInThisYear,
    calendar_get_NumberOfMonthsInThisYear,
    calendar_get_Month,
    calendar_put_Month,
    calendar_AddMonths,
    calendar_MonthAsFullString,
    calendar_MonthAsString,
    calendar_MonthAsFullSoloString,
    calendar_MonthAsSoloString,
    calendar_MonthAsNumericString,
    calendar_MonthAsPaddedNumericString,
    calendar_AddWeeks,
    calendar_get_FirstDayInThisMonth,
    calendar_get_LastDayInThisMonth,
    calendar_get_NumberOfDaysInThisMonth,
    calendar_get_Day,
    calendar_put_Day,
    calendar_AddDays,
    calendar_DayAsString,
    calendar_DayAsPaddedString,
    calendar_get_DayOfWeek,
    calendar_DayOfWeekAsFullString,
    calendar_DayOfWeekAsString,
    calendar_DayOfWeekAsFullSoloString,
    calendar_DayOfWeekAsSoloString,
    calendar_get_FirstPeriodInThisDay,
    calendar_get_LastPeriodInThisDay,
    calendar_get_NumberOfPeriodsInThisDay,
    calendar_get_Period,
    calendar_put_Period,
    calendar_AddPeriods,
    calendar_PeriodAsFullString,
    calendar_PeriodAsString,
    calendar_get_FirstHourInThisPeriod,
    calendar_get_LastHourInThisPeriod,
    calendar_get_NumberOfHoursInThisPeriod,
    calendar_get_Hour,
    calendar_put_Hour,
    calendar_AddHours,
    calendar_HourAsString,
    calendar_HourAsPaddedString,
    calendar_get_Minute,
    calendar_put_Minute,
    calendar_AddMinutes,
    calendar_MinuteAsString,
    calendar_MinuteAsPaddedString,
    calendar_get_Second,
    calendar_put_Second,
    calendar_AddSeconds,
    calendar_SecondAsString,
    calendar_SecondAsPaddedString,
    calendar_get_Nanosecond,
    calendar_put_Nanosecond,
    calendar_AddNanoseconds,
    calendar_NanosecondAsString,
    calendar_NanosecondAsPaddedString,
    calendar_Compare,
    calendar_CompareDateTime,
    calendar_CopyTo,
    calendar_get_FirstMinuteInThisHour,
    calendar_get_LastMinuteInThisHour,
    calendar_get_NumberOfMinutesInThisHour,
    calendar_get_FirstSecondInThisMinute,
    calendar_get_LastSecondInThisMinute,
    calendar_get_NumberOfSecondsInThisMinute,
    calendar_get_ResolvedLanguage,
    calendar_get_IsDaylightSavingTime,
};

static HRESULT WINAPI tz_cal_QueryInterface( ITimeZoneOnCalendar *iface, REFIID iid, void **out )
{
    return calendar_QueryInterface( &impl_from_ITimeZoneOnCalendar( iface )->ICalendar_iface, iid, out );
}
static ULONG WINAPI tz_cal_AddRef( ITimeZoneOnCalendar *iface )
{
    return calendar_AddRef( &impl_from_ITimeZoneOnCalendar( iface )->ICalendar_iface );
}
static ULONG WINAPI tz_cal_Release( ITimeZoneOnCalendar *iface )
{
    return calendar_Release( &impl_from_ITimeZoneOnCalendar( iface )->ICalendar_iface );
}
static HRESULT WINAPI tz_cal_GetIids( ITimeZoneOnCalendar *iface, ULONG *c, IID **i ) { return calendar_GetIids( NULL, c, i ); }
static HRESULT WINAPI tz_cal_GetRuntimeClassName( ITimeZoneOnCalendar *iface, HSTRING *n ) { return calendar_GetRuntimeClassName( NULL, n ); }
static HRESULT WINAPI tz_cal_GetTrustLevel( ITimeZoneOnCalendar *iface, TrustLevel *l ) { return calendar_GetTrustLevel( NULL, l ); }

static HRESULT WINAPI tz_cal_GetTimeZone( ITimeZoneOnCalendar *iface, HSTRING *value )
{
    struct calendar_obj *impl = impl_from_ITimeZoneOnCalendar( iface );
    return WindowsCreateString( impl->time_zone, wcslen(impl->time_zone), value );
}

static HRESULT WINAPI tz_cal_ChangeTimeZone( ITimeZoneOnCalendar *iface, HSTRING tz )
{
    struct calendar_obj *impl = impl_from_ITimeZoneOnCalendar( iface );
    const WCHAR *s = WindowsGetStringRawBuffer( tz, NULL );
    lstrcpynW( impl->time_zone, s && *s ? s : L"UTC", ARRAY_SIZE(impl->time_zone) );
    return S_OK;
}

static HRESULT WINAPI tz_cal_TimeZoneAsFullString( ITimeZoneOnCalendar *iface, HSTRING *result )
{
    return tz_cal_GetTimeZone( iface, result );
}

static HRESULT WINAPI tz_cal_TimeZoneAsString( ITimeZoneOnCalendar *iface, INT32 ideal_length, HSTRING *result )
{
    return tz_cal_GetTimeZone( iface, result );
}

static const struct ITimeZoneOnCalendarVtbl tz_cal_vtbl =
{
    tz_cal_QueryInterface,
    tz_cal_AddRef,
    tz_cal_Release,
    tz_cal_GetIids,
    tz_cal_GetRuntimeClassName,
    tz_cal_GetTrustLevel,
    tz_cal_GetTimeZone,
    tz_cal_ChangeTimeZone,
    tz_cal_TimeZoneAsFullString,
    tz_cal_TimeZoneAsString,
};

static HRESULT create_calendar( const WCHAR *cal, const WCHAR *clk, const WCHAR *tz, ICalendar **out )
{
    struct calendar_obj *obj;
    FILETIME ft;

    if (!out) return E_POINTER;
    if (!(obj = calloc( 1, sizeof(*obj) ))) return E_OUTOFMEMORY;
    obj->ICalendar_iface.lpVtbl = &calendar_vtbl;
    obj->ITimeZoneOnCalendar_iface.lpVtbl = &tz_cal_vtbl;
    obj->ref = 1;
    GetSystemTimeAsFileTime( &ft );
    obj->utc_ticks = ((INT64)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    lstrcpynW( obj->calendar_system, cal && *cal ? cal : L"GregorianCalendar", ARRAY_SIZE(obj->calendar_system) );
    lstrcpynW( obj->clock, clk && *clk ? clk : L"12HourClock", ARRAY_SIZE(obj->clock) );
    lstrcpynW( obj->numeral_system, L"Latn", ARRAY_SIZE(obj->numeral_system) );
    lstrcpynW( obj->time_zone, tz && *tz ? tz : L"UTC", ARRAY_SIZE(obj->time_zone) );
    *out = &obj->ICalendar_iface;
    return S_OK;
}

struct calendar_factory
{
    IActivationFactory IActivationFactory_iface;
    ICalendarFactory ICalendarFactory_iface;
    ICalendarFactory2 ICalendarFactory2_iface;
    LONG ref;
};

static inline struct calendar_factory *cal_fac_from_IActivationFactory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct calendar_factory, IActivationFactory_iface );
}
static inline struct calendar_factory *cal_fac_from_ICalendarFactory( ICalendarFactory *iface )
{
    return CONTAINING_RECORD( iface, struct calendar_factory, ICalendarFactory_iface );
}
static inline struct calendar_factory *cal_fac_from_ICalendarFactory2( ICalendarFactory2 *iface )
{
    return CONTAINING_RECORD( iface, struct calendar_factory, ICalendarFactory2_iface );
}

static HRESULT WINAPI cal_factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct calendar_factory *impl = cal_fac_from_IActivationFactory( iface );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IReferenceTracker_cal ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget_cal ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = &impl->IActivationFactory_iface;
        IActivationFactory_AddRef( *out );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_ICalendarFactory ))
    {
        *out = &impl->ICalendarFactory_iface;
        ICalendarFactory_AddRef( *out );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_ICalendarFactory2 ))
    {
        *out = &impl->ICalendarFactory2_iface;
        ICalendarFactory2_AddRef( *out );
        return S_OK;
    }
    FIXME( "cal_factory: iid %s not implemented\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI cal_factory_AddRef( IActivationFactory *iface ) { return 2; }
static ULONG WINAPI cal_factory_Release( IActivationFactory *iface ) { return 1; }
static HRESULT WINAPI cal_factory_GetIids( IActivationFactory *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}
static HRESULT WINAPI cal_factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.Globalization.Calendar", 30, name );
}
static HRESULT WINAPI cal_factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}
static HRESULT WINAPI cal_factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    return create_calendar( NULL, NULL, NULL, (ICalendar **)instance );
}

static const struct IActivationFactoryVtbl cal_act_vtbl =
{
    cal_factory_QueryInterface,
    cal_factory_AddRef,
    cal_factory_Release,
    cal_factory_GetIids,
    cal_factory_GetRuntimeClassName,
    cal_factory_GetTrustLevel,
    cal_factory_ActivateInstance,
};

static HRESULT WINAPI cf1_QueryInterface( ICalendarFactory *iface, REFIID iid, void **out )
{
    return cal_factory_QueryInterface( &cal_fac_from_ICalendarFactory( iface )->IActivationFactory_iface, iid, out );
}
static ULONG WINAPI cf1_AddRef( ICalendarFactory *iface ) { return 2; }
static ULONG WINAPI cf1_Release( ICalendarFactory *iface ) { return 1; }
static HRESULT WINAPI cf1_GetIids( ICalendarFactory *iface, ULONG *c, IID **i ) { return cal_factory_GetIids( NULL, c, i ); }
static HRESULT WINAPI cf1_GetRuntimeClassName( ICalendarFactory *iface, HSTRING *n ) { return cal_factory_GetRuntimeClassName( NULL, n ); }
static HRESULT WINAPI cf1_GetTrustLevel( ICalendarFactory *iface, TrustLevel *l ) { return cal_factory_GetTrustLevel( NULL, l ); }

static HRESULT WINAPI cf1_CreateCalendarDefaultCalendarAndClock( ICalendarFactory *iface, IIterable_HSTRING *languages, ICalendar **result )
{
    return create_calendar( NULL, NULL, NULL, result );
}

static HRESULT WINAPI cf1_CreateCalendar( ICalendarFactory *iface, IIterable_HSTRING *languages, HSTRING calendar, HSTRING clock, ICalendar **result )
{
    return create_calendar( WindowsGetStringRawBuffer( calendar, NULL ), WindowsGetStringRawBuffer( clock, NULL ), NULL, result );
}

static const struct ICalendarFactoryVtbl cf1_vtbl =
{
    cf1_QueryInterface,
    cf1_AddRef,
    cf1_Release,
    cf1_GetIids,
    cf1_GetRuntimeClassName,
    cf1_GetTrustLevel,
    cf1_CreateCalendarDefaultCalendarAndClock,
    cf1_CreateCalendar,
};

static HRESULT WINAPI cf2_QueryInterface( ICalendarFactory2 *iface, REFIID iid, void **out )
{
    return cal_factory_QueryInterface( &cal_fac_from_ICalendarFactory2( iface )->IActivationFactory_iface, iid, out );
}
static ULONG WINAPI cf2_AddRef( ICalendarFactory2 *iface ) { return 2; }
static ULONG WINAPI cf2_Release( ICalendarFactory2 *iface ) { return 1; }
static HRESULT WINAPI cf2_GetIids( ICalendarFactory2 *iface, ULONG *c, IID **i ) { return cal_factory_GetIids( NULL, c, i ); }
static HRESULT WINAPI cf2_GetRuntimeClassName( ICalendarFactory2 *iface, HSTRING *n ) { return cal_factory_GetRuntimeClassName( NULL, n ); }
static HRESULT WINAPI cf2_GetTrustLevel( ICalendarFactory2 *iface, TrustLevel *l ) { return cal_factory_GetTrustLevel( NULL, l ); }

static HRESULT WINAPI cf2_CreateCalendarWithTimeZone( ICalendarFactory2 *iface, IIterable_HSTRING *languages, HSTRING calendar, HSTRING clock, HSTRING tz, ICalendar **result )
{
    return create_calendar( WindowsGetStringRawBuffer( calendar, NULL ), WindowsGetStringRawBuffer( clock, NULL ), WindowsGetStringRawBuffer( tz, NULL ), result );
}

static const struct ICalendarFactory2Vtbl cf2_vtbl =
{
    cf2_QueryInterface,
    cf2_AddRef,
    cf2_Release,
    cf2_GetIids,
    cf2_GetRuntimeClassName,
    cf2_GetTrustLevel,
    cf2_CreateCalendarWithTimeZone,
};

static struct calendar_factory calendar_factory_obj =
{
    { &cal_act_vtbl },
    { &cf1_vtbl },
    { &cf2_vtbl },
    1,
};

IActivationFactory *calendar_factory = &calendar_factory_obj.IActivationFactory_iface;
