/* WinRT Windows.Globalization.NumberFormatting formatter implementations
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

#include <math.h>
#include "private.h"
#include "objbase.h"

WINE_DEFAULT_DEBUG_CHANNEL(locale);

enum formatter_kind
{
    FORMATTER_DECIMAL = 0,
    FORMATTER_PERCENT,
    FORMATTER_PERMILLE,
    FORMATTER_CURRENCY,
};

enum currency_formatter_mode
{
    CURRENCY_MODE_USE_SYMBOL = 0,
    CURRENCY_MODE_USE_CURRENCY_CODE = 1,
};

static const GUID IID_ICurrencyFormatter =
    { 0x11730ca5, 0x4b00, 0x41b2, { 0xb3, 0x32, 0x73, 0xb1, 0x2a, 0x49, 0x7d, 0x54 } };
static const GUID IID_ICurrencyFormatter2 =
    { 0x072c2f1d, 0xe7ba, 0x4197, { 0x92, 0x0e, 0x24, 0x7c, 0x92, 0xf7, 0xde, 0xa6 } };
static const GUID IID_ICurrencyFormatterFactory =
    { 0x86c7537e, 0xb938, 0x4aa2, { 0x84, 0xb0, 0x2c, 0x33, 0xdc, 0x5b, 0x14, 0x50 } };
static const GUID IID_IDecimalFormatterFactory =
    { 0x0d018c9a, 0xe393, 0x46b8, { 0xb8, 0x30, 0x7a, 0x69, 0xc8, 0xf8, 0x9f, 0xbb } };
static const GUID IID_IPercentFormatterFactory =
    { 0xb7828aef, 0xfed4, 0x4018, { 0xa6, 0xe2, 0xe0, 0x99, 0x61, 0xe0, 0x37, 0x65 } };
static const GUID IID_IPermilleFormatterFactory =
    { 0x2b37b4ac, 0xe638, 0x4ed5, { 0xa9, 0x98, 0x62, 0xf6, 0xb0, 0x6a, 0x49, 0xae } };
static const GUID IID_INumberFormatter =
    { 0xa5007c49, 0x7676, 0x4db7, { 0x86, 0x31, 0x1b, 0x6f, 0xf2, 0x65, 0xca, 0xa9 } };
static const GUID IID_INumberFormatter2 =
    { 0xd4a8c1f0, 0x80d0, 0x4b0d, { 0xa8, 0x9e, 0x88, 0x2c, 0x1e, 0x8f, 0x83, 0x10 } };
static const GUID IID_INumberFormatterOptions =
    { 0x80332d21, 0xaee1, 0x4a39, { 0xba, 0xa2, 0x07, 0xed, 0x8c, 0x96, 0xda, 0xf6 } };
static const GUID IID_INumberParser =
    { 0xe6659412, 0x4a13, 0x4a53, { 0x83, 0xa1, 0x39, 0x2f, 0xbe, 0x4c, 0xff, 0x9f } };
static const GUID IID_ISignificantDigitsOption =
    { 0x1d4dfcdd, 0x2d43, 0x4ee8, { 0xbb, 0xf1, 0xc1, 0xb2, 0x6a, 0x71, 0x1a, 0x58 } };
static const GUID IID_ISignedZeroOption =
    { 0xfd1cdd31, 0x0a3c, 0x49c4, { 0xa6, 0x42, 0x96, 0xa1, 0x56, 0x4f, 0x4f, 0x30 } };
static const GUID IID_INumberRounderOption =
    { 0x3b088433, 0x646f, 0x4efe, { 0x8d, 0x48, 0x66, 0xeb, 0x2e, 0x49, 0xe7, 0x36 } };

struct number_formatter
{
    const void *vtbl_formatter;   /* INumberFormatter & INumberFormatter2 */
    const void *vtbl_options;     /* INumberFormatterOptions */
    const void *vtbl_parser;      /* INumberParser */
    const void *vtbl_sigdigits;   /* ISignificantDigitsOption */
    const void *vtbl_signedzero;  /* ISignedZeroOption */
    const void *vtbl_rounder_opt; /* INumberRounderOption */
    const void *vtbl_currency;    /* ICurrencyFormatter (default face for CurrencyFormatter) */
    const void *vtbl_currency2;   /* ICurrencyFormatter2 */
    LONG ref;
    enum formatter_kind kind;
    INT32 fraction_digits;
    INT32 integer_digits;
    INT32 significant_digits;
    BOOLEAN is_grouping_enabled;
    BOOLEAN is_decimal_point_always_displayed;
    BOOLEAN is_zero_signed;
    INT32 currency_mode;
    WCHAR currency_code[16];
    WCHAR language[LOCALE_NAME_MAX_LENGTH];
    WCHAR geo_region[16];
    WCHAR numeral_system[16];
    IUnknown *number_rounder;
};

static inline struct number_formatter *impl_from_formatter( void *iface )
{
    return CONTAINING_RECORD( iface, struct number_formatter, vtbl_formatter );
}
static inline struct number_formatter *impl_from_options( void *iface )
{
    return CONTAINING_RECORD( iface, struct number_formatter, vtbl_options );
}
static inline struct number_formatter *impl_from_parser( void *iface )
{
    return CONTAINING_RECORD( iface, struct number_formatter, vtbl_parser );
}
static inline struct number_formatter *impl_from_sigdigits( void *iface )
{
    return CONTAINING_RECORD( iface, struct number_formatter, vtbl_sigdigits );
}
static inline struct number_formatter *impl_from_signedzero( void *iface )
{
    return CONTAINING_RECORD( iface, struct number_formatter, vtbl_signedzero );
}
static inline struct number_formatter *impl_from_rounder_opt( void *iface )
{
    return CONTAINING_RECORD( iface, struct number_formatter, vtbl_rounder_opt );
}
static inline struct number_formatter *impl_from_currency( void *iface )
{
    return CONTAINING_RECORD( iface, struct number_formatter, vtbl_currency );
}
static inline struct number_formatter *impl_from_currency2( void *iface )
{
    return CONTAINING_RECORD( iface, struct number_formatter, vtbl_currency2 );
}

static HRESULT number_formatter_qi_impl( struct number_formatter *impl, REFIID iid, void **out )
{
    TRACE( "impl %p, kind %d, iid %s, out %p.\n", impl, impl->kind, debugstr_guid( iid ), out );

    if (!out) return E_INVALIDARG;

    if (IsEqualGUID( iid, &IID_IMarshal ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget_glob ) ||
        IsEqualGUID( iid, &IID_IReferenceTracker_glob ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ))
    {
        *out = (impl->kind == FORMATTER_CURRENCY)
             ? (void *)&impl->vtbl_currency
             : (void *)&impl->vtbl_formatter;
    }
    else if (IsEqualGUID( iid, &IID_INumberFormatter ) ||
             IsEqualGUID( iid, &IID_INumberFormatter2 ))
    {
        *out = (void *)&impl->vtbl_formatter;
    }
    else if (IsEqualGUID( iid, &IID_INumberFormatterOptions ))
    {
        *out = (void *)&impl->vtbl_options;
    }
    else if (IsEqualGUID( iid, &IID_INumberRounderOption ))
    {
        *out = (void *)&impl->vtbl_rounder_opt;
    }
    else if (IsEqualGUID( iid, &IID_INumberParser ))
    {
        *out = (void *)&impl->vtbl_parser;
    }
    else if (IsEqualGUID( iid, &IID_ISignificantDigitsOption ))
    {
        *out = (void *)&impl->vtbl_sigdigits;
    }
    else if (IsEqualGUID( iid, &IID_ISignedZeroOption ))
    {
        *out = (void *)&impl->vtbl_signedzero;
    }
    else if (impl->kind == FORMATTER_CURRENCY && IsEqualGUID( iid, &IID_ICurrencyFormatter ))
    {
        *out = (void *)&impl->vtbl_currency;
    }
    else if (impl->kind == FORMATTER_CURRENCY && IsEqualGUID( iid, &IID_ICurrencyFormatter2 ))
    {
        *out = (void *)&impl->vtbl_currency2;
    }
    else
    {
        FIXME( "number_formatter (kind %d) %s not implemented, returning E_NOINTERFACE.\n",
               impl->kind, debugstr_guid( iid ) );
        *out = NULL;
        return E_NOINTERFACE;
    }

    InterlockedIncrement( &impl->ref );
    return S_OK;
}

static ULONG number_formatter_addref_impl( struct number_formatter *impl )
{
    return InterlockedIncrement( &impl->ref );
}

static ULONG number_formatter_release_impl( struct number_formatter *impl )
{
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        if (impl->number_rounder) IUnknown_Release( impl->number_rounder );
        free( impl );
    }
    return ref;
}

static HRESULT number_formatter_getiids_impl( struct number_formatter *impl, ULONG *count, IID **iids )
{
    static const IID base_iids[] =
    {
        { 0xa5007c49, 0x7676, 0x4db7, { 0x86, 0x31, 0x1b, 0x6f, 0xf2, 0x65, 0xca, 0xa9 } }, /* INumberFormatter */
        { 0xd4a8c1f0, 0x80d0, 0x4b0d, { 0xa8, 0x9e, 0x88, 0x2c, 0x1e, 0x8f, 0x83, 0x10 } }, /* INumberFormatter2 */
        { 0x80332d21, 0xaee1, 0x4a39, { 0xba, 0xa2, 0x07, 0xed, 0x8c, 0x96, 0xda, 0xf6 } }, /* INumberFormatterOptions */
        { 0xe6659412, 0x4a13, 0x4a53, { 0x83, 0xa1, 0x39, 0x2f, 0xbe, 0x4c, 0xff, 0x9f } }, /* INumberParser */
        { 0x1d4dfcdd, 0x2d43, 0x4ee8, { 0xbb, 0xf1, 0xc1, 0xb2, 0x6a, 0x71, 0x1a, 0x58 } }, /* ISignificantDigitsOption */
        { 0xfd1cdd31, 0x0a3c, 0x49c4, { 0xa6, 0x42, 0x96, 0xa1, 0x56, 0x4f, 0x4f, 0x30 } }, /* ISignedZeroOption */
        { 0x3b088433, 0x646f, 0x4efe, { 0x8d, 0x48, 0x66, 0xeb, 0x2e, 0x49, 0xe7, 0x36 } }, /* INumberRounderOption */
    };
    static const IID currency_iids[] =
    {
        { 0x11730ca5, 0x4b00, 0x41b2, { 0xb3, 0x32, 0x73, 0xb1, 0x2a, 0x49, 0x7d, 0x54 } }, /* ICurrencyFormatter */
        { 0x072c2f1d, 0xe7ba, 0x4197, { 0x92, 0x0e, 0x24, 0x7c, 0x92, 0xf7, 0xde, 0xa6 } }, /* ICurrencyFormatter2 */
        { 0xa5007c49, 0x7676, 0x4db7, { 0x86, 0x31, 0x1b, 0x6f, 0xf2, 0x65, 0xca, 0xa9 } }, /* INumberFormatter */
        { 0xd4a8c1f0, 0x80d0, 0x4b0d, { 0xa8, 0x9e, 0x88, 0x2c, 0x1e, 0x8f, 0x83, 0x10 } }, /* INumberFormatter2 */
        { 0x80332d21, 0xaee1, 0x4a39, { 0xba, 0xa2, 0x07, 0xed, 0x8c, 0x96, 0xda, 0xf6 } }, /* INumberFormatterOptions */
        { 0xe6659412, 0x4a13, 0x4a53, { 0x83, 0xa1, 0x39, 0x2f, 0xbe, 0x4c, 0xff, 0x9f } }, /* INumberParser */
        { 0x1d4dfcdd, 0x2d43, 0x4ee8, { 0xbb, 0xf1, 0xc1, 0xb2, 0x6a, 0x71, 0x1a, 0x58 } }, /* ISignificantDigitsOption */
        { 0xfd1cdd31, 0x0a3c, 0x49c4, { 0xa6, 0x42, 0x96, 0xa1, 0x56, 0x4f, 0x4f, 0x30 } }, /* ISignedZeroOption */
        { 0x3b088433, 0x646f, 0x4efe, { 0x8d, 0x48, 0x66, 0xeb, 0x2e, 0x49, 0xe7, 0x36 } }, /* INumberRounderOption */
    };
    const IID *src;
    ULONG n;
    IID *array;

    if (!count || !iids) return E_POINTER;
    if (impl->kind == FORMATTER_CURRENCY)
    {
        src = currency_iids;
        n = ARRAY_SIZE(currency_iids);
    }
    else
    {
        src = base_iids;
        n = ARRAY_SIZE(base_iids);
    }
    if (!(array = CoTaskMemAlloc( n * sizeof(IID) ))) return E_OUTOFMEMORY;
    memcpy( array, src, n * sizeof(IID) );
    *count = n;
    *iids = array;
    return S_OK;
}

static const WCHAR *formatter_class_name( enum formatter_kind kind )
{
    switch (kind)
    {
    case FORMATTER_CURRENCY: return L"Windows.Globalization.NumberFormatting.CurrencyFormatter";
    case FORMATTER_PERCENT:  return L"Windows.Globalization.NumberFormatting.PercentFormatter";
    case FORMATTER_PERMILLE: return L"Windows.Globalization.NumberFormatting.PermilleFormatter";
    default:                 return L"Windows.Globalization.NumberFormatting.DecimalFormatter";
    }
}

static HRESULT number_formatter_getclassname_impl( struct number_formatter *impl, HSTRING *name )
{
    const WCHAR *cls = formatter_class_name( impl->kind );
    if (!name) return E_POINTER;
    return WindowsCreateString( cls, wcslen( cls ), name );
}

static HRESULT number_formatter_gettrust_impl( struct number_formatter *impl, TrustLevel *level )
{
    if (!level) return E_POINTER;
    *level = BaseTrust;
    return S_OK;
}

#define DEFINE_FORMATTER_FACE_IINSPECTABLE( pfx, from_fn )                                          \
    static HRESULT WINAPI pfx##_QueryInterface( void *iface, REFIID iid, void **out )               \
    {                                                                                               \
        return number_formatter_qi_impl( from_fn( iface ), iid, out );                              \
    }                                                                                               \
    static ULONG WINAPI pfx##_AddRef( void *iface )                                                 \
    {                                                                                               \
        return number_formatter_addref_impl( from_fn( iface ) );                                    \
    }                                                                                               \
    static ULONG WINAPI pfx##_Release( void *iface )                                                \
    {                                                                                               \
        return number_formatter_release_impl( from_fn( iface ) );                                   \
    }                                                                                               \
    static HRESULT WINAPI pfx##_GetIids( void *iface, ULONG *count, IID **iids )                    \
    {                                                                                               \
        return number_formatter_getiids_impl( from_fn( iface ), count, iids );                      \
    }                                                                                               \
    static HRESULT WINAPI pfx##_GetRuntimeClassName( void *iface, HSTRING *name )                   \
    {                                                                                               \
        return number_formatter_getclassname_impl( from_fn( iface ), name );                        \
    }                                                                                               \
    static HRESULT WINAPI pfx##_GetTrustLevel( void *iface, TrustLevel *level )                     \
    {                                                                                               \
        return number_formatter_gettrust_impl( from_fn( iface ), level );                           \
    }

DEFINE_FORMATTER_FACE_IINSPECTABLE( decimal_formatter, impl_from_formatter )
DEFINE_FORMATTER_FACE_IINSPECTABLE( options, impl_from_options )
DEFINE_FORMATTER_FACE_IINSPECTABLE( parser, impl_from_parser )
DEFINE_FORMATTER_FACE_IINSPECTABLE( sigdigits, impl_from_sigdigits )
DEFINE_FORMATTER_FACE_IINSPECTABLE( signedzero, impl_from_signedzero )
DEFINE_FORMATTER_FACE_IINSPECTABLE( rounder_opt, impl_from_rounder_opt )
DEFINE_FORMATTER_FACE_IINSPECTABLE( currency, impl_from_currency )
DEFINE_FORMATTER_FACE_IINSPECTABLE( currency2, impl_from_currency2 )

static const WCHAR *currency_symbol_for_code( const WCHAR *code, WCHAR *buf, size_t buf_len )
{
    static const struct { const WCHAR *code; const WCHAR *symbol; } map[] =
    {
        { L"USD", L"$" },
        { L"EUR", L"\x20ac" },
        { L"GBP", L"\x00a3" },
        { L"JPY", L"\x00a5" },
        { L"CNY", L"\x00a5" },
        { L"KRW", L"\x20a9" },
        { L"INR", L"\x20b9" },
        { L"RUB", L"\x20bd" },
        { L"KZT", L"\x20b8" },
        { L"CHF", L"CHF" },
        { L"CAD", L"CA$" },
        { L"AUD", L"A$" },
    };
    WCHAR intl[16];
    unsigned int i;

    if (GetLocaleInfoEx( LOCALE_NAME_USER_DEFAULT, LOCALE_SINTLSYMBOL, intl, ARRAY_SIZE(intl) ) &&
        !wcscmp( intl, code ) &&
        GetLocaleInfoEx( LOCALE_NAME_USER_DEFAULT, LOCALE_SCURRENCY, buf, buf_len ) &&
        buf[0])
        return buf;

    for (i = 0; i < ARRAY_SIZE(map); i++)
        if (!wcscmp( map[i].code, code )) return map[i].symbol;

    return code;
}

static void format_currency_string( struct number_formatter *impl, const WCHAR *num_str,
                                    BOOL negative, WCHAR *out, size_t out_len )
{
    WCHAR sym_buf[16];
    const WCHAR *sign = negative ? L"-" : L"";

    if (impl->currency_mode == CURRENCY_MODE_USE_CURRENCY_CODE)
        swprintf( out, out_len, L"%s%s %s", sign, impl->currency_code, num_str );
    else
    {
        const WCHAR *sym = currency_symbol_for_code( impl->currency_code, sym_buf, ARRAY_SIZE(sym_buf) );
        swprintf( out, out_len, L"%s%s%s", sign, sym, num_str );
    }
}

/* Face 1: INumberFormatter / INumberFormatter2 */
static HRESULT WINAPI decimal_formatter_FormatInt( void *iface, INT64 value, HSTRING *result )
{
    struct number_formatter *impl = impl_from_formatter( iface );
    WCHAR buf[128], num[64];

    if (!result) return E_POINTER;

    switch (impl->kind)
    {
    case FORMATTER_CURRENCY:
    {
        BOOL neg = value < 0;
        double dval = fabs( (double)value );
        INT32 frac = impl->fraction_digits > 0 ? impl->fraction_digits : 0;
        swprintf( num, ARRAY_SIZE(num), L"%.*f", frac, dval );
        format_currency_string( impl, num, neg, buf, ARRAY_SIZE(buf) );
        break;
    }
    case FORMATTER_PERCENT:
        swprintf( buf, ARRAY_SIZE(buf), L"%lld%%", (long long)(value * 100) );
        break;
    case FORMATTER_PERMILLE:
        swprintf( buf, ARRAY_SIZE(buf), L"%lld\x2030", (long long)(value * 1000) );
        break;
    default:
        if (impl->fraction_digits > 0)
            swprintf( buf, ARRAY_SIZE(buf), L"%.*f", impl->fraction_digits, (double)value );
        else
            swprintf( buf, ARRAY_SIZE(buf), L"%lld", (long long)value );
        break;
    }
    return WindowsCreateString( buf, wcslen( buf ), result );
}

static HRESULT WINAPI decimal_formatter_FormatUInt( void *iface, UINT64 value, HSTRING *result )
{
    struct number_formatter *impl = impl_from_formatter( iface );
    WCHAR buf[128], num[64];

    if (!result) return E_POINTER;

    switch (impl->kind)
    {
    case FORMATTER_CURRENCY:
    {
        INT32 frac = impl->fraction_digits > 0 ? impl->fraction_digits : 0;
        swprintf( num, ARRAY_SIZE(num), L"%.*f", frac, (double)value );
        format_currency_string( impl, num, FALSE, buf, ARRAY_SIZE(buf) );
        break;
    }
    case FORMATTER_PERCENT:
        swprintf( buf, ARRAY_SIZE(buf), L"%llu%%", (unsigned long long)(value * 100) );
        break;
    case FORMATTER_PERMILLE:
        swprintf( buf, ARRAY_SIZE(buf), L"%llu\x2030", (unsigned long long)(value * 1000) );
        break;
    default:
        if (impl->fraction_digits > 0)
            swprintf( buf, ARRAY_SIZE(buf), L"%.*f", impl->fraction_digits, (double)value );
        else
            swprintf( buf, ARRAY_SIZE(buf), L"%llu", (unsigned long long)value );
        break;
    }
    return WindowsCreateString( buf, wcslen( buf ), result );
}

static HRESULT WINAPI decimal_formatter_FormatDouble( void *iface, double value, HSTRING *result )
{
    struct number_formatter *impl = impl_from_formatter( iface );
    WCHAR buf[128], num[64];

    if (!result) return E_POINTER;

    switch (impl->kind)
    {
    case FORMATTER_CURRENCY:
    {
        BOOL neg = value < 0.0 || (value == 0.0 && impl->is_zero_signed && signbit( value ));
        double aval = fabs( value );
        INT32 frac = impl->fraction_digits >= 0 ? impl->fraction_digits : 2;
        swprintf( num, ARRAY_SIZE(num), L"%.*f", frac, aval );
        format_currency_string( impl, num, neg, buf, ARRAY_SIZE(buf) );
        break;
    }
    case FORMATTER_PERCENT:
    {
        double pct = value * 100.0;
        if (impl->fraction_digits > 0)
            swprintf( buf, ARRAY_SIZE(buf), L"%.*f%%", impl->fraction_digits, pct );
        else
            swprintf( buf, ARRAY_SIZE(buf), L"%.0f%%", pct );
        break;
    }
    case FORMATTER_PERMILLE:
    {
        double pm = value * 1000.0;
        if (impl->fraction_digits > 0)
            swprintf( buf, ARRAY_SIZE(buf), L"%.*f\x2030", impl->fraction_digits, pm );
        else
            swprintf( buf, ARRAY_SIZE(buf), L"%.0f\x2030", pm );
        break;
    }
    default:
        if (impl->fraction_digits > 0)
            swprintf( buf, ARRAY_SIZE(buf), L"%.*f", impl->fraction_digits, value );
        else if (value == floor( value ) && fabs( value ) < 1e14)
            swprintf( buf, ARRAY_SIZE(buf), L"%.0f", value );
        else
            swprintf( buf, ARRAY_SIZE(buf), L"%.10g", value );
        break;
    }
    return WindowsCreateString( buf, wcslen( buf ), result );
}

/* Face 2: INumberFormatterOptions */
static HRESULT WINAPI decimal_formatter_get_Languages( void *iface, void **value )
{
    struct number_formatter *impl = impl_from_options( iface );
    HSTRING hs;
    HRESULT hr;

    if (!value) return E_POINTER;
    if (FAILED(hr = WindowsCreateString( impl->language, wcslen( impl->language ), &hs )))
        return hr;
    return hstring_vector_create( &hs, 1, (IVectorView_HSTRING **)value );
}

static HRESULT WINAPI decimal_formatter_get_GeographicRegion( void *iface, HSTRING *value )
{
    struct number_formatter *impl = impl_from_options( iface );
    if (!value) return E_POINTER;
    return WindowsCreateString( impl->geo_region, wcslen( impl->geo_region ), value );
}

static HRESULT WINAPI decimal_formatter_get_IntegerDigits( void *iface, INT32 *value )
{
    struct number_formatter *impl = impl_from_options( iface );
    if (!value) return E_POINTER;
    *value = impl->integer_digits;
    return S_OK;
}
static HRESULT WINAPI decimal_formatter_put_IntegerDigits( void *iface, INT32 value )
{
    struct number_formatter *impl = impl_from_options( iface );
    impl->integer_digits = value;
    return S_OK;
}

static HRESULT WINAPI decimal_formatter_get_FractionDigits( void *iface, INT32 *value )
{
    struct number_formatter *impl = impl_from_options( iface );
    if (!value) return E_POINTER;
    *value = impl->fraction_digits;
    return S_OK;
}
static HRESULT WINAPI decimal_formatter_put_FractionDigits( void *iface, INT32 value )
{
    struct number_formatter *impl = impl_from_options( iface );
    impl->fraction_digits = value;
    return S_OK;
}

static HRESULT WINAPI decimal_formatter_get_IsGroupingEnabled( void *iface, BOOLEAN *value )
{
    struct number_formatter *impl = impl_from_options( iface );
    if (!value) return E_POINTER;
    *value = impl->is_grouping_enabled;
    return S_OK;
}
static HRESULT WINAPI decimal_formatter_put_IsGroupingEnabled( void *iface, BOOLEAN value )
{
    struct number_formatter *impl = impl_from_options( iface );
    impl->is_grouping_enabled = value;
    return S_OK;
}

static HRESULT WINAPI decimal_formatter_get_IsDecimalPointAlwaysDisplayed( void *iface, BOOLEAN *value )
{
    struct number_formatter *impl = impl_from_options( iface );
    if (!value) return E_POINTER;
    *value = impl->is_decimal_point_always_displayed;
    return S_OK;
}
static HRESULT WINAPI decimal_formatter_put_IsDecimalPointAlwaysDisplayed( void *iface, BOOLEAN value )
{
    struct number_formatter *impl = impl_from_options( iface );
    impl->is_decimal_point_always_displayed = value;
    return S_OK;
}

static HRESULT WINAPI decimal_formatter_get_NumeralSystem( void *iface, HSTRING *value )
{
    struct number_formatter *impl = impl_from_options( iface );
    if (!value) return E_POINTER;
    return WindowsCreateString( impl->numeral_system, wcslen( impl->numeral_system ), value );
}
static HRESULT WINAPI decimal_formatter_put_NumeralSystem( void *iface, HSTRING value )
{
    struct number_formatter *impl = impl_from_options( iface );
    const WCHAR *str = WindowsGetStringRawBuffer( value, NULL );
    if (str && str[0] && wcslen( str ) < ARRAY_SIZE(impl->numeral_system))
        wcscpy( impl->numeral_system, str );
    return S_OK;
}

static HRESULT WINAPI decimal_formatter_get_ResolvedLanguage( void *iface, HSTRING *value )
{
    struct number_formatter *impl = impl_from_options( iface );
    if (!value) return E_POINTER;
    return WindowsCreateString( impl->language, wcslen( impl->language ), value );
}
static HRESULT WINAPI decimal_formatter_get_ResolvedGeographicRegion( void *iface, HSTRING *value )
{
    return decimal_formatter_get_GeographicRegion( iface, value );
}

/* Face 3: INumberParser */
static const GUID IID_IReference_double =
    { 0x2f2d6c29, 0x5473, 0x5f3e, { 0x92, 0xe7, 0x96, 0x57, 0x2b, 0xb9, 0x90, 0xe2 } };

struct iref_double
{
    const void *lpVtbl;
    LONG ref;
    double value;
};

static HRESULT WINAPI iref_double_QI( void *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IReference_double ))
    {
        *out = iface;
        InterlockedIncrement( &((struct iref_double *)iface)->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI iref_double_AddRef( void *iface )
{
    return InterlockedIncrement( &((struct iref_double *)iface)->ref );
}
static ULONG WINAPI iref_double_Release( void *iface )
{
    struct iref_double *impl = iface;
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref) free( impl );
    return ref;
}
static HRESULT WINAPI iref_double_GetIids( void *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}
static HRESULT WINAPI iref_double_GetRuntimeClassName( void *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.Foundation.IReference`1<Double>", 38, name );
}
static HRESULT WINAPI iref_double_GetTrustLevel( void *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}
static HRESULT WINAPI iref_double_get_Value( void *iface, double *value )
{
    if (!value) return E_POINTER;
    *value = ((struct iref_double *)iface)->value;
    return S_OK;
}
static const void *iref_double_vtbl[] =
{
    iref_double_QI, iref_double_AddRef, iref_double_Release,
    iref_double_GetIids, iref_double_GetRuntimeClassName, iref_double_GetTrustLevel,
    iref_double_get_Value,
};

static HRESULT WINAPI parser_ParseInt( void *iface, HSTRING text, void **result )
{
    if (!result) return E_POINTER;
    *result = NULL;
    return S_OK;
}

static HRESULT WINAPI parser_ParseUInt( void *iface, HSTRING text, void **result )
{
    if (!result) return E_POINTER;
    *result = NULL;
    return S_OK;
}

static HRESULT WINAPI parser_ParseDouble( void *iface, HSTRING text, void **result )
{
    struct number_formatter *impl = impl_from_parser( iface );
    const WCHAR *str;
    WCHAR *end = NULL;
    double val;
    struct iref_double *ref;

    if (!result) return E_POINTER;
    *result = NULL;
    str = WindowsGetStringRawBuffer( text, NULL );
    if (!str || !*str) return S_OK;

    if (impl->kind == FORMATTER_CURRENCY)
    {
        while (*str == ' ' || *str == '\t') str++;
        if (impl->currency_code[0] && !wcsncmp( str, impl->currency_code, wcslen( impl->currency_code ) ))
            str += wcslen( impl->currency_code );
        else if (*str == '$' || *str == 0x20ac || *str == 0x00a3 || *str == 0x00a5 ||
                 *str == 0x20a9 || *str == 0x20b9 || *str == 0x20bd || *str == 0x20b8)
            str++;
        while (*str == ' ' || *str == '\t') str++;
    }

    val = wcstod( str, &end );
    if (end == str) return S_OK;
    if (impl->kind == FORMATTER_PERCENT)
    {
        while (*end == ' ' || *end == '\t') end++;
        if (*end == '%') val /= 100.0;
    }
    else if (impl->kind == FORMATTER_PERMILLE)
    {
        while (*end == ' ' || *end == '\t') end++;
        if (*end == 0x2030) val /= 1000.0;
    }

    if (!(ref = malloc( sizeof(*ref) ))) return E_OUTOFMEMORY;
    ref->lpVtbl = iref_double_vtbl;
    ref->ref = 1;
    ref->value = val;
    *result = ref;
    return S_OK;
}

/* Face 4: ISignificantDigitsOption */
static HRESULT WINAPI decimal_formatter_get_SignificantDigits( void *iface, INT32 *value )
{
    struct number_formatter *impl = impl_from_sigdigits( iface );
    if (!value) return E_POINTER;
    *value = impl->significant_digits;
    return S_OK;
}
static HRESULT WINAPI decimal_formatter_put_SignificantDigits( void *iface, INT32 value )
{
    struct number_formatter *impl = impl_from_sigdigits( iface );
    impl->significant_digits = value;
    return S_OK;
}

/* Face 5: ISignedZeroOption */
static HRESULT WINAPI signedzero_get_IsZeroSigned( void *iface, BOOLEAN *value )
{
    struct number_formatter *impl = impl_from_signedzero( iface );
    if (!value) return E_POINTER;
    *value = impl->is_zero_signed;
    return S_OK;
}
static HRESULT WINAPI signedzero_put_IsZeroSigned( void *iface, BOOLEAN value )
{
    struct number_formatter *impl = impl_from_signedzero( iface );
    impl->is_zero_signed = value;
    return S_OK;
}

/* Face 6: INumberRounderOption */
static HRESULT WINAPI rounder_opt_get_NumberRounder( void *iface, IUnknown **value )
{
    struct number_formatter *impl = impl_from_rounder_opt( iface );
    if (!value) return E_POINTER;
    *value = impl->number_rounder;
    if (*value) IUnknown_AddRef( *value );
    return S_OK;
}
static HRESULT WINAPI rounder_opt_put_NumberRounder( void *iface, IUnknown *value )
{
    struct number_formatter *impl = impl_from_rounder_opt( iface );
    if (value) IUnknown_AddRef( value );
    if (impl->number_rounder) IUnknown_Release( impl->number_rounder );
    impl->number_rounder = value;
    return S_OK;
}

/* Face 7: ICurrencyFormatter */
static HRESULT WINAPI currency_get_Currency( void *iface, HSTRING *value )
{
    struct number_formatter *impl = impl_from_currency( iface );
    TRACE( "iface %p, value %p -> %s.\n", iface, value, debugstr_w( impl->currency_code ) );
    if (!value) return E_POINTER;
    return WindowsCreateString( impl->currency_code, wcslen( impl->currency_code ), value );
}

static HRESULT WINAPI currency_put_Currency( void *iface, HSTRING value )
{
    struct number_formatter *impl = impl_from_currency( iface );
    const WCHAR *code = WindowsGetStringRawBuffer( value, NULL );
    TRACE( "iface %p, value %s.\n", iface, debugstr_hstring( value ) );
    if (!code || !code[0] || wcslen( code ) >= ARRAY_SIZE(impl->currency_code))
        return E_INVALIDARG;
    wcscpy( impl->currency_code, code );
    return S_OK;
}

/* Face 8: ICurrencyFormatter2 */
static HRESULT WINAPI currency2_get_Mode( void *iface, INT32 *value )
{
    struct number_formatter *impl = impl_from_currency2( iface );
    TRACE( "iface %p, value %p -> %d.\n", iface, value, impl->currency_mode );
    if (!value) return E_POINTER;
    *value = impl->currency_mode;
    return S_OK;
}

static HRESULT WINAPI currency2_put_Mode( void *iface, INT32 value )
{
    struct number_formatter *impl = impl_from_currency2( iface );
    TRACE( "iface %p, value %d.\n", iface, value );
    impl->currency_mode = value;
    return S_OK;
}

static HRESULT WINAPI currency2_ApplyRoundingForCurrency( void *iface, INT32 rounding_algorithm )
{
    TRACE( "iface %p, rounding_algorithm %d.\n", iface, rounding_algorithm );
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
    /* INumberFormatter / INumberFormatter2 */
    decimal_formatter_FormatInt,
    decimal_formatter_FormatUInt,
    decimal_formatter_FormatDouble,
};

static const void *options_vtbl[] =
{
    options_QueryInterface,
    options_AddRef,
    options_Release,
    options_GetIids,
    options_GetRuntimeClassName,
    options_GetTrustLevel,
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
};

static const void *parser_vtbl[] =
{
    parser_QueryInterface,
    parser_AddRef,
    parser_Release,
    parser_GetIids,
    parser_GetRuntimeClassName,
    parser_GetTrustLevel,
    /* INumberParser */
    parser_ParseInt,
    parser_ParseUInt,
    parser_ParseDouble,
};

static const void *sigdigits_vtbl[] =
{
    sigdigits_QueryInterface,
    sigdigits_AddRef,
    sigdigits_Release,
    sigdigits_GetIids,
    sigdigits_GetRuntimeClassName,
    sigdigits_GetTrustLevel,
    /* ISignificantDigitsOption */
    decimal_formatter_get_SignificantDigits,
    decimal_formatter_put_SignificantDigits,
};

static const void *signedzero_vtbl[] =
{
    signedzero_QueryInterface,
    signedzero_AddRef,
    signedzero_Release,
    signedzero_GetIids,
    signedzero_GetRuntimeClassName,
    signedzero_GetTrustLevel,
    /* ISignedZeroOption */
    signedzero_get_IsZeroSigned,
    signedzero_put_IsZeroSigned,
};

static const void *rounder_opt_vtbl[] =
{
    rounder_opt_QueryInterface,
    rounder_opt_AddRef,
    rounder_opt_Release,
    rounder_opt_GetIids,
    rounder_opt_GetRuntimeClassName,
    rounder_opt_GetTrustLevel,
    /* INumberRounderOption */
    rounder_opt_get_NumberRounder,
    rounder_opt_put_NumberRounder,
};

static const void *currency_vtbl[] =
{
    currency_QueryInterface,
    currency_AddRef,
    currency_Release,
    currency_GetIids,
    currency_GetRuntimeClassName,
    currency_GetTrustLevel,
    /* ICurrencyFormatter */
    currency_get_Currency,
    currency_put_Currency,
};

static const void *currency2_vtbl[] =
{
    currency2_QueryInterface,
    currency2_AddRef,
    currency2_Release,
    currency2_GetIids,
    currency2_GetRuntimeClassName,
    currency2_GetTrustLevel,
    /* ICurrencyFormatter2 */
    currency2_get_Mode,
    currency2_put_Mode,
    currency2_ApplyRoundingForCurrency,
};

static void extract_first_language( IIterable_HSTRING *languages, WCHAR *out, size_t out_len )
{
    IIterator_HSTRING *iter = NULL;
    boolean has_current = FALSE;
    HSTRING first = NULL;

    out[0] = 0;
    if (languages && SUCCEEDED(IIterable_HSTRING_First( languages, &iter )) && iter)
    {
        if (SUCCEEDED(IIterator_HSTRING_get_HasCurrent( iter, &has_current )) && has_current &&
            SUCCEEDED(IIterator_HSTRING_get_Current( iter, &first )) && first)
        {
            const WCHAR *buf = WindowsGetStringRawBuffer( first, NULL );
            if (buf && buf[0] && wcslen( buf ) < out_len)
                wcscpy( out, buf );
            WindowsDeleteString( first );
        }
        IIterator_HSTRING_Release( iter );
    }

    if (!out[0] && (!GetUserDefaultLocaleName( out, out_len ) || !out[0]))
        wcscpy( out, L"en-US" );
}

static HRESULT number_formatter_create( enum formatter_kind kind, const WCHAR *currency_code,
                                        IIterable_HSTRING *languages, HSTRING geographic_region,
                                        void **instance )
{
    struct number_formatter *impl;
    const WCHAR *geo_str;

    if (!instance) return E_POINTER;
    *instance = NULL;

    if (kind == FORMATTER_CURRENCY)
    {
        if (!currency_code || !currency_code[0] || wcslen( currency_code ) >= ARRAY_SIZE(impl->currency_code))
            return E_INVALIDARG;
    }

    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->vtbl_formatter   = decimal_formatter_vtbl;
    impl->vtbl_options     = options_vtbl;
    impl->vtbl_parser      = parser_vtbl;
    impl->vtbl_sigdigits   = sigdigits_vtbl;
    impl->vtbl_signedzero  = signedzero_vtbl;
    impl->vtbl_rounder_opt = rounder_opt_vtbl;
    impl->vtbl_currency    = currency_vtbl;
    impl->vtbl_currency2   = currency2_vtbl;
    impl->ref = 1;
    impl->kind = kind;
    impl->integer_digits = 1;
    impl->fraction_digits = (kind == FORMATTER_CURRENCY) ? 2 : 0;
    impl->significant_digits = 0;
    impl->is_grouping_enabled = (kind == FORMATTER_CURRENCY) ? TRUE : FALSE;
    impl->is_decimal_point_always_displayed = FALSE;
    impl->is_zero_signed = FALSE;
    impl->currency_mode = CURRENCY_MODE_USE_SYMBOL;
    wcscpy( impl->numeral_system, L"Latn" );

    if (currency_code && currency_code[0])
        wcscpy( impl->currency_code, currency_code );

    extract_first_language( languages, impl->language, ARRAY_SIZE(impl->language) );

    geo_str = geographic_region ? WindowsGetStringRawBuffer( geographic_region, NULL ) : NULL;
    if (geo_str && geo_str[0] && wcslen( geo_str ) < ARRAY_SIZE(impl->geo_region))
        wcscpy( impl->geo_region, geo_str );
    else if (!GetUserDefaultGeoName( impl->geo_region, ARRAY_SIZE(impl->geo_region) ) || !impl->geo_region[0])
        wcscpy( impl->geo_region, L"US" );

    *instance = (kind == FORMATTER_CURRENCY)
              ? (void *)&impl->vtbl_currency
              : (void *)&impl->vtbl_formatter;
    return S_OK;
}

/* =========================================================================
 * Factories: DecimalFormatter, PercentFormatter, PermilleFormatter, CurrencyFormatter
 * ========================================================================= */
struct formatter_factory_impl
{
    const void *vtbl_activation;
    const void *vtbl_factory;
    enum formatter_kind kind;
    const GUID *factory_iid;
};

static inline struct formatter_factory_impl *impl_from_ff_activation( void *iface )
{
    return CONTAINING_RECORD( iface, struct formatter_factory_impl, vtbl_activation );
}
static inline struct formatter_factory_impl *impl_from_ff_factory( void *iface )
{
    return CONTAINING_RECORD( iface, struct formatter_factory_impl, vtbl_factory );
}

static HRESULT formatter_factory_qi_impl( struct formatter_factory_impl *impl, REFIID iid, void **out )
{
    TRACE( "impl %p, kind %d, iid %s, out %p.\n", impl, impl->kind, debugstr_guid( iid ), out );

    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IMarshal ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget_glob ) ||
        IsEqualGUID( iid, &IID_IReferenceTracker_glob ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = (void *)&impl->vtbl_activation;
        return S_OK;
    }
    if (IsEqualGUID( iid, impl->factory_iid ))
    {
        *out = (void *)&impl->vtbl_factory;
        return S_OK;
    }

    FIXME( "formatter_factory (kind %d) %s not implemented, returning E_NOINTERFACE.\n",
           impl->kind, debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI ff_AddRef( void *iface ) { return 2; }
static ULONG WINAPI ff_Release( void *iface ) { return 1; }
static HRESULT WINAPI ff_GetIids( void *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}
static HRESULT WINAPI ff_GetTrustLevel( void *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI ff_activation_QueryInterface( void *iface, REFIID iid, void **out )
{
    return formatter_factory_qi_impl( impl_from_ff_activation( iface ), iid, out );
}
static HRESULT WINAPI ff_activation_GetRuntimeClassName( void *iface, HSTRING *name )
{
    const WCHAR *cls = formatter_class_name( impl_from_ff_activation( iface )->kind );
    if (!name) return E_POINTER;
    return WindowsCreateString( cls, wcslen( cls ), name );
}
static HRESULT WINAPI ff_activation_ActivateInstance( void *iface, IInspectable **instance )
{
    struct formatter_factory_impl *impl = impl_from_ff_activation( iface );
    if (impl->kind == FORMATTER_CURRENCY)
        return E_NOTIMPL;
    return number_formatter_create( impl->kind, NULL, NULL, NULL, (void **)instance );
}

static const void *ff_activation_vtbl[] =
{
    ff_activation_QueryInterface,
    ff_AddRef,
    ff_Release,
    ff_GetIids,
    ff_activation_GetRuntimeClassName,
    ff_GetTrustLevel,
    ff_activation_ActivateInstance,
};

static HRESULT WINAPI ff_factory_QueryInterface( void *iface, REFIID iid, void **out )
{
    return formatter_factory_qi_impl( impl_from_ff_factory( iface ), iid, out );
}
static HRESULT WINAPI ff_factory_GetRuntimeClassName( void *iface, HSTRING *name )
{
    const WCHAR *cls = formatter_class_name( impl_from_ff_factory( iface )->kind );
    if (!name) return E_POINTER;
    return WindowsCreateString( cls, wcslen( cls ), name );
}

/* IDecimalFormatterFactory / IPercentFormatterFactory / IPermilleFormatterFactory */
static HRESULT WINAPI ff_factory_CreateWithContext( void *iface, IIterable_HSTRING *languages,
                                                    HSTRING geographic_region, void **result )
{
    struct formatter_factory_impl *impl = impl_from_ff_factory( iface );
    TRACE( "iface %p, kind %d, languages %p, region %s, out %p.\n",
           iface, impl->kind, languages, debugstr_hstring( geographic_region ), result );
    return number_formatter_create( impl->kind, NULL, languages, geographic_region, result );
}

static const void *ff_decimal_like_factory_vtbl[] =
{
    ff_factory_QueryInterface,
    ff_AddRef,
    ff_Release,
    ff_GetIids,
    ff_factory_GetRuntimeClassName,
    ff_GetTrustLevel,
    ff_factory_CreateWithContext,
};

/* ICurrencyFormatterFactory */
static HRESULT WINAPI cf_factory_CreateCurrencyFormatterCode( void *iface, HSTRING currency_code,
                                                              void **result )
{
    const WCHAR *code = currency_code ? WindowsGetStringRawBuffer( currency_code, NULL ) : NULL;
    TRACE( "iface %p, currency_code %s, out %p.\n", iface, debugstr_hstring( currency_code ), result );
    return number_formatter_create( FORMATTER_CURRENCY, code, NULL, NULL, result );
}

static HRESULT WINAPI cf_factory_CreateCurrencyFormatterCodeContext( void *iface, HSTRING currency_code,
                                                                     IIterable_HSTRING *languages,
                                                                     HSTRING geographic_region, void **result )
{
    const WCHAR *code = currency_code ? WindowsGetStringRawBuffer( currency_code, NULL ) : NULL;
    TRACE( "iface %p, currency_code %s, languages %p, region %s, out %p.\n",
           iface, debugstr_hstring( currency_code ), languages, debugstr_hstring( geographic_region ), result );
    return number_formatter_create( FORMATTER_CURRENCY, code, languages, geographic_region, result );
}

static const void *ff_currency_factory_vtbl[] =
{
    ff_factory_QueryInterface,
    ff_AddRef,
    ff_Release,
    ff_GetIids,
    ff_factory_GetRuntimeClassName,
    ff_GetTrustLevel,
    cf_factory_CreateCurrencyFormatterCode,
    cf_factory_CreateCurrencyFormatterCodeContext,
};

static struct formatter_factory_impl decimal_formatter_factory_inst =
{
    ff_activation_vtbl,
    ff_decimal_like_factory_vtbl,
    FORMATTER_DECIMAL,
    &IID_IDecimalFormatterFactory,
};

static struct formatter_factory_impl currency_formatter_factory_inst =
{
    ff_activation_vtbl,
    ff_currency_factory_vtbl,
    FORMATTER_CURRENCY,
    &IID_ICurrencyFormatterFactory,
};

static struct formatter_factory_impl percent_formatter_factory_inst =
{
    ff_activation_vtbl,
    ff_decimal_like_factory_vtbl,
    FORMATTER_PERCENT,
    &IID_IPercentFormatterFactory,
};

static struct formatter_factory_impl permille_formatter_factory_inst =
{
    ff_activation_vtbl,
    ff_decimal_like_factory_vtbl,
    FORMATTER_PERMILLE,
    &IID_IPermilleFormatterFactory,
};

IActivationFactory *decimal_formatter_factory  = (IActivationFactory *)&decimal_formatter_factory_inst.vtbl_activation;
IActivationFactory *currency_formatter_factory = (IActivationFactory *)&currency_formatter_factory_inst.vtbl_activation;
IActivationFactory *percent_formatter_factory  = (IActivationFactory *)&percent_formatter_factory_inst.vtbl_activation;
IActivationFactory *permille_formatter_factory = (IActivationFactory *)&permille_formatter_factory_inst.vtbl_activation;
