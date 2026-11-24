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

WINE_DEFAULT_DEBUG_CHANNEL(locale);

static const GUID IID_ISignedZeroOption =
    { 0xfd1cdd31, 0x0a3c, 0x49c4, { 0xa6, 0x42, 0x96, 0xa1, 0x56, 0x4f, 0x4f, 0x30 } };

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
static const GUID IID_ISignificantDigitsOption =
    { 0x1d4dfcdd, 0x2d43, 0x4ee8, { 0xbb, 0xf1, 0xc1, 0xb2, 0x6a, 0x71, 0x1a, 0x58 } };
static const GUID IID_INumberRounderOption =
    { 0x3b088433, 0x646f, 0x4efe, { 0x8d, 0x48, 0x66, 0xeb, 0x2e, 0x49, 0xe7, 0x36 } };

struct decimal_formatter
{
    const void *vtbl_formatter;  /* 0: INumberFormatter & INumberFormatter2 */
    const void *vtbl_options;    /* +8: INumberFormatterOptions */
    const void *vtbl_parser;     /* +16: INumberParser */
    const void *vtbl_sigdigits;  /* +24: ISignificantDigitsOption */
    const void *vtbl_rounder_opt;/* +32: INumberRounderOption */
    LONG ref;
    INT32 fraction_digits;
    INT32 integer_digits;
    INT32 significant_digits;
    BOOLEAN is_grouping_enabled;
    BOOL is_percent;
    IUnknown *number_rounder;
};

static inline struct decimal_formatter *impl_from_formatter( void *iface )
{
    return CONTAINING_RECORD( iface, struct decimal_formatter, vtbl_formatter );
}
static inline struct decimal_formatter *impl_from_options( void *iface )
{
    return CONTAINING_RECORD( iface, struct decimal_formatter, vtbl_options );
}
static inline struct decimal_formatter *impl_from_parser( void *iface )
{
    return CONTAINING_RECORD( iface, struct decimal_formatter, vtbl_parser );
}
static inline struct decimal_formatter *impl_from_sigdigits( void *iface )
{
    return CONTAINING_RECORD( iface, struct decimal_formatter, vtbl_sigdigits );
}
static inline struct decimal_formatter *impl_from_rounder_opt( void *iface )
{
    return CONTAINING_RECORD( iface, struct decimal_formatter, vtbl_rounder_opt );
}

static HRESULT WINAPI decimal_formatter_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct decimal_formatter *impl = impl_from_formatter( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

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
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_INumberFormatter ) ||
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
    else if (IsEqualGUID( iid, &IID_ISignificantDigitsOption ) ||
             IsEqualGUID( iid, &IID_ISignedZeroOption ))
    {
        *out = (void *)&impl->vtbl_sigdigits;
    }
    else
    {
        FIXME( "decimal_formatter %s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
        *out = NULL;
        return E_NOINTERFACE;
    }

    InterlockedIncrement( &impl->ref );
    return S_OK;
}

static ULONG WINAPI decimal_formatter_AddRef( void *iface )
{
    struct decimal_formatter *impl = impl_from_formatter( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI decimal_formatter_Release( void *iface )
{
    struct decimal_formatter *impl = impl_from_formatter( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        if (impl->number_rounder) IUnknown_Release( impl->number_rounder );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI decimal_formatter_GetIids( void *iface, ULONG *count, IID **iids )
{
    static IID *cache;
    static const IID *iids_arr[] = { &IID_INumberFormatter, &IID_INumberFormatter2,
        &IID_INumberFormatterOptions, &IID_INumberParser, &IID_ISignificantDigitsOption,
        &IID_INumberRounderOption };
    ULONG i, n = sizeof(iids_arr)/sizeof(iids_arr[0]);
    if (!count || !iids) return E_POINTER;
    if (!cache)
    {
        cache = malloc( n * sizeof(IID) );
        for (i = 0; i < n; i++) cache[i] = *iids_arr[i];
    }
    *count = n;
    *iids = cache;   /* leaked static: .NET Native never CoTaskMemFrees */
    (void)i;
    return S_OK;
}

static HRESULT WINAPI decimal_formatter_GetRuntimeClassName( void *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.Globalization.NumberFormatting.DecimalFormatter", 55, name );
}

static HRESULT WINAPI decimal_formatter_GetTrustLevel( void *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}

/* Face 1: INumberFormatter / INumberFormatter2 */
static HRESULT WINAPI decimal_formatter_FormatInt( void *iface, INT64 value, HSTRING *result )
{
    struct decimal_formatter *impl = impl_from_formatter( iface );
    WCHAR buf[64];
    if (!result) return E_POINTER;
    if (impl->is_percent)
        swprintf( buf, ARRAY_SIZE(buf), L"%lld%%", (long long)(value * 100) );
    else
        swprintf( buf, ARRAY_SIZE(buf), L"%lld", (long long)value );
    return WindowsCreateString( buf, wcslen(buf), result );
}

static HRESULT WINAPI decimal_formatter_FormatUInt( void *iface, UINT64 value, HSTRING *result )
{
    struct decimal_formatter *impl = impl_from_formatter( iface );
    WCHAR buf[64];
    if (!result) return E_POINTER;
    if (impl->is_percent)
        swprintf( buf, ARRAY_SIZE(buf), L"%llu%%", (unsigned long long)(value * 100) );
    else
        swprintf( buf, ARRAY_SIZE(buf), L"%llu", (unsigned long long)value );
    return WindowsCreateString( buf, wcslen(buf), result );
}

static HRESULT WINAPI decimal_formatter_FormatDouble( void *iface, double value, HSTRING *result )
{
    struct decimal_formatter *impl = impl_from_formatter( iface );
    WCHAR buf[64];
    if (!result) return E_POINTER;
    if (impl->is_percent)
    {
        double pct = value * 100.0;
        if (impl->fraction_digits > 0)
            swprintf( buf, ARRAY_SIZE(buf), L"%.*f%%", impl->fraction_digits, pct );
        else
            swprintf( buf, ARRAY_SIZE(buf), L"%.0f%%", pct );
    }
    else if (value == floor( value ) && fabs( value ) < 1e14)
        swprintf( buf, ARRAY_SIZE(buf), L"%.0f", value );
    else
        swprintf( buf, ARRAY_SIZE(buf), L"%.10g", value );
    return WindowsCreateString( buf, wcslen(buf), result );
}

/* Face 2: INumberFormatterOptions */
static HRESULT WINAPI options_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct decimal_formatter *impl = impl_from_options( iface );
    return decimal_formatter_QueryInterface( (void *)&impl->vtbl_formatter, iid, out );
}
static ULONG WINAPI options_AddRef( void *iface )
{
    return decimal_formatter_AddRef( (void *)&impl_from_options( iface )->vtbl_formatter );
}
static ULONG WINAPI options_Release( void *iface )
{
    return decimal_formatter_Release( (void *)&impl_from_options( iface )->vtbl_formatter );
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
    struct decimal_formatter *impl = impl_from_options( iface );
    if (!value) return E_POINTER;
    *value = impl->integer_digits;
    return S_OK;
}
static HRESULT WINAPI decimal_formatter_put_IntegerDigits( void *iface, INT32 value )
{
    struct decimal_formatter *impl = impl_from_options( iface );
    impl->integer_digits = value;
    return S_OK;
}

static HRESULT WINAPI decimal_formatter_get_FractionDigits( void *iface, INT32 *value )
{
    struct decimal_formatter *impl = impl_from_options( iface );
    if (!value) return E_POINTER;
    *value = impl->fraction_digits;
    return S_OK;
}
static HRESULT WINAPI decimal_formatter_put_FractionDigits( void *iface, INT32 value )
{
    struct decimal_formatter *impl = impl_from_options( iface );
    impl->fraction_digits = value;
    return S_OK;
}

static HRESULT WINAPI decimal_formatter_get_IsGroupingEnabled( void *iface, BOOLEAN *value )
{
    struct decimal_formatter *impl = impl_from_options( iface );
    if (!value) return E_POINTER;
    *value = impl->is_grouping_enabled;
    return S_OK;
}
static HRESULT WINAPI decimal_formatter_put_IsGroupingEnabled( void *iface, BOOLEAN value )
{
    struct decimal_formatter *impl = impl_from_options( iface );
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

/* Face 3: INumberParser */
static HRESULT WINAPI parser_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct decimal_formatter *impl = impl_from_parser( iface );
    return decimal_formatter_QueryInterface( (void *)&impl->vtbl_formatter, iid, out );
}
static ULONG WINAPI parser_AddRef( void *iface )
{
    return decimal_formatter_AddRef( (void *)&impl_from_parser( iface )->vtbl_formatter );
}
static ULONG WINAPI parser_Release( void *iface )
{
    return decimal_formatter_Release( (void *)&impl_from_parser( iface )->vtbl_formatter );
}

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

static HRESULT WINAPI parser_ParseDouble( void *iface, HSTRING text, void **result )
{
    struct decimal_formatter *impl = impl_from_parser( iface );
    const WCHAR *str;
    WCHAR *end = NULL;
    double val;
    struct iref_double *ref;

    if (!result) return E_POINTER;
    *result = NULL;
    str = WindowsGetStringRawBuffer( text, NULL );
    if (!str || !*str) return S_OK;

    val = wcstod( str, &end );
    if (end == str) return S_OK;
    if (impl->is_percent)
    {
        while (*end == ' ' || *end == '\t') end++;
        if (*end == '%') val /= 100.0;
    }

    if (!(ref = malloc( sizeof(*ref) ))) return E_OUTOFMEMORY;
    ref->lpVtbl = iref_double_vtbl;
    ref->ref = 1;
    ref->value = val;
    *result = ref;
    return S_OK;
}

static HRESULT WINAPI decimal_formatter_stub_zero( void *iface, ... )
{
    va_list args;
    void **out;
    va_start( args, iface );
    va_arg( args, void * );
    out = va_arg( args, void ** );
    va_end( args );
    if (out) *out = NULL;
    return S_OK;
}

/* Face 4: ISignificantDigitsOption */
static HRESULT WINAPI sigdigits_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct decimal_formatter *impl = impl_from_sigdigits( iface );
    return decimal_formatter_QueryInterface( (void *)&impl->vtbl_formatter, iid, out );
}
static ULONG WINAPI sigdigits_AddRef( void *iface )
{
    return decimal_formatter_AddRef( (void *)&impl_from_sigdigits( iface )->vtbl_formatter );
}
static ULONG WINAPI sigdigits_Release( void *iface )
{
    return decimal_formatter_Release( (void *)&impl_from_sigdigits( iface )->vtbl_formatter );
}
static HRESULT WINAPI decimal_formatter_get_SignificantDigits( void *iface, INT32 *value )
{
    struct decimal_formatter *impl = impl_from_sigdigits( iface );
    if (!value) return E_POINTER;
    *value = impl->significant_digits;
    return S_OK;
}
static HRESULT WINAPI decimal_formatter_put_SignificantDigits( void *iface, INT32 value )
{
    struct decimal_formatter *impl = impl_from_sigdigits( iface );
    impl->significant_digits = value;
    return S_OK;
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
    /* INumberFormatter / INumberFormatter2 */
    decimal_formatter_FormatInt,
    decimal_formatter_FormatUInt,
    decimal_formatter_FormatDouble,
    decimal_formatter_stub_ok, decimal_formatter_stub_ok, decimal_formatter_stub_ok,
    decimal_formatter_stub_ok, decimal_formatter_stub_ok, decimal_formatter_stub_ok,
    decimal_formatter_stub_ok, decimal_formatter_stub_ok, decimal_formatter_stub_ok,
    decimal_formatter_stub_ok, decimal_formatter_stub_ok, decimal_formatter_stub_ok,
    decimal_formatter_stub_ok, decimal_formatter_stub_ok, decimal_formatter_stub_ok,
    decimal_formatter_stub_ok, decimal_formatter_stub_ok, decimal_formatter_stub_ok,
    decimal_formatter_stub_ok, decimal_formatter_stub_ok, decimal_formatter_stub_ok,
};

static const void *options_vtbl[] =
{
    options_QueryInterface,
    options_AddRef,
    options_Release,
    decimal_formatter_GetIids,
    decimal_formatter_GetRuntimeClassName,
    decimal_formatter_GetTrustLevel,
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
    decimal_formatter_stub_ok, decimal_formatter_stub_ok, decimal_formatter_stub_ok,
    decimal_formatter_stub_ok, decimal_formatter_stub_ok, decimal_formatter_stub_ok,
};

static const void *parser_vtbl[] =
{
    parser_QueryInterface,
    parser_AddRef,
    parser_Release,
    decimal_formatter_GetIids,
    decimal_formatter_GetRuntimeClassName,
    decimal_formatter_GetTrustLevel,
    /* INumberParser */
    decimal_formatter_stub_zero,
    decimal_formatter_stub_zero,
    parser_ParseDouble,
    decimal_formatter_stub_ok, decimal_formatter_stub_ok, decimal_formatter_stub_ok,
    decimal_formatter_stub_ok, decimal_formatter_stub_ok, decimal_formatter_stub_ok,
    decimal_formatter_stub_ok, decimal_formatter_stub_ok, decimal_formatter_stub_ok,
    decimal_formatter_stub_ok, decimal_formatter_stub_ok, decimal_formatter_stub_ok,
};

static const void *sigdigits_vtbl[] =
{
    sigdigits_QueryInterface,
    sigdigits_AddRef,
    sigdigits_Release,
    decimal_formatter_GetIids,
    decimal_formatter_GetRuntimeClassName,
    decimal_formatter_GetTrustLevel,
    /* ISignificantDigitsOption */
    decimal_formatter_get_SignificantDigits,
    decimal_formatter_put_SignificantDigits,
    decimal_formatter_stub_ok, decimal_formatter_stub_ok, decimal_formatter_stub_ok,
    decimal_formatter_stub_ok, decimal_formatter_stub_ok, decimal_formatter_stub_ok,
    decimal_formatter_stub_ok, decimal_formatter_stub_ok, decimal_formatter_stub_ok,
    decimal_formatter_stub_ok, decimal_formatter_stub_ok, decimal_formatter_stub_ok,
};

static HRESULT WINAPI rounder_opt_QueryInterface( void *iface, REFIID iid, void **out )
{
    return decimal_formatter_QueryInterface( &impl_from_rounder_opt( iface )->vtbl_formatter, iid, out );
}
static ULONG WINAPI rounder_opt_AddRef( void *iface )
{
    return decimal_formatter_AddRef( &impl_from_rounder_opt( iface )->vtbl_formatter );
}
static ULONG WINAPI rounder_opt_Release( void *iface )
{
    return decimal_formatter_Release( &impl_from_rounder_opt( iface )->vtbl_formatter );
}
static HRESULT WINAPI rounder_opt_get_NumberRounder( void *iface, IUnknown **value )
{
    struct decimal_formatter *impl = impl_from_rounder_opt( iface );
    if (!value) return E_POINTER;
    *value = impl->number_rounder;
    if (*value) IUnknown_AddRef( *value );
    return S_OK;
}
static HRESULT WINAPI rounder_opt_put_NumberRounder( void *iface, IUnknown *value )
{
    struct decimal_formatter *impl = impl_from_rounder_opt( iface );
    if (value) IUnknown_AddRef( value );
    if (impl->number_rounder) IUnknown_Release( impl->number_rounder );
    impl->number_rounder = value;
    return S_OK;
}

static const void *rounder_opt_vtbl[] =
{
    rounder_opt_QueryInterface,
    rounder_opt_AddRef,
    rounder_opt_Release,
    decimal_formatter_GetIids,
    decimal_formatter_GetRuntimeClassName,
    decimal_formatter_GetTrustLevel,
    rounder_opt_get_NumberRounder,
    rounder_opt_put_NumberRounder,
};

static HRESULT decimal_formatter_create( BOOL is_percent, void **instance )
{
    struct decimal_formatter *impl = calloc( 1, sizeof(*impl) );
    if (!impl) return E_OUTOFMEMORY;
    impl->vtbl_formatter = decimal_formatter_vtbl;
    impl->vtbl_options = options_vtbl;
    impl->vtbl_parser = parser_vtbl;
    impl->vtbl_sigdigits = sigdigits_vtbl;
    impl->vtbl_rounder_opt = rounder_opt_vtbl;
    impl->ref = 1;
    impl->integer_digits = 1;
    impl->fraction_digits = 0;
    impl->significant_digits = 0;
    impl->is_grouping_enabled = FALSE;
    impl->is_percent = is_percent;
    *instance = (void *)&impl->vtbl_formatter;
    return S_OK;
}

/* DecimalFormatter factory */
struct decimal_formatter_factory_impl
{
    const void *vtbl_activation;
    const void *vtbl_factory;
    BOOL is_percent;
};

static inline struct decimal_formatter_factory_impl *impl_from_df_activation( void *iface )
{
    return (struct decimal_formatter_factory_impl *)iface;
}
static inline struct decimal_formatter_factory_impl *impl_from_df_factory( void *iface )
{
    return (struct decimal_formatter_factory_impl *)((char *)iface - sizeof(void *));
}

static HRESULT WINAPI df_factory_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct decimal_formatter_factory_impl *impl = impl_from_df_activation( iface );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = (void *)&impl->vtbl_activation;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IDecimalFormatterFactory ))
    {
        *out = (void *)&impl->vtbl_factory;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI df_factory_AddRef( void *iface ) { return 2; }
static ULONG WINAPI df_factory_Release( void *iface ) { return 1; }
static HRESULT WINAPI df_factory_GetIids( void *iface, ULONG *count, IID **iids )
{ if (count) *count = 0; if (iids) *iids = NULL; return S_OK; }
static HRESULT WINAPI df_factory_GetRuntimeClassName( void *iface, HSTRING *name )
{ return WindowsCreateString( L"Windows.Globalization.NumberFormatting.DecimalFormatter", 55, name ); }
static HRESULT WINAPI df_factory_GetTrustLevel( void *iface, TrustLevel *level )
{ if (level) *level = BaseTrust; return S_OK; }

/* Face 1: IActivationFactory */
static HRESULT WINAPI df_factory_ActivateInstance( void *iface, IInspectable **instance )
{
    struct decimal_formatter_factory_impl *impl = impl_from_df_activation( iface );
    return decimal_formatter_create( impl->is_percent, (void **)instance );
}
static const void *df_activation_vtbl[] =
{
    df_factory_QueryInterface,
    df_factory_AddRef,
    df_factory_Release,
    df_factory_GetIids,
    df_factory_GetRuntimeClassName,
    df_factory_GetTrustLevel,
    df_factory_ActivateInstance,
};

/* Face 2: IDecimalFormatterFactory */
static HRESULT WINAPI df_factory2_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct decimal_formatter_factory_impl *impl = impl_from_df_factory( iface );
    return df_factory_QueryInterface( (void *)&impl->vtbl_activation, iid, out );
}
static ULONG WINAPI df_factory2_AddRef( void *iface )
{
    return df_factory_AddRef( (void *)&impl_from_df_factory( iface )->vtbl_activation );
}
static ULONG WINAPI df_factory2_Release( void *iface )
{
    return df_factory_Release( (void *)&impl_from_df_factory( iface )->vtbl_activation );
}
static HRESULT WINAPI df_factory_CreateDecimalFormatter( void *iface, void *languages, HSTRING geographicregion, void **result )
{
    struct decimal_formatter_factory_impl *impl = impl_from_df_factory( iface );
    TRACE( "CreateDecimalFormatter iface %p languages %p region %s out %p\n",
           iface, languages, debugstr_hstring(geographicregion), result );
    return decimal_formatter_create( impl->is_percent, result );
}
static HRESULT WINAPI df_factory_CreateDecimalFormatterWithDigits( void *iface, void *languages,
        INT32 integer_digits, INT32 fraction_digits, INT32 significant_digits,
        HSTRING geographicregion, void **result )
{
    struct decimal_formatter_factory_impl *impl = impl_from_df_factory( iface );
    TRACE( "CreateDecimalFormatterWithDigits iface %p integer %d fraction %d significant %d region %s out %p\n",
           iface, integer_digits, fraction_digits, significant_digits, debugstr_hstring(geographicregion), result );
    return decimal_formatter_create( impl->is_percent, result );
}
static const void *df_factory_vtbl[] =
{
    df_factory2_QueryInterface,
    df_factory2_AddRef,
    df_factory2_Release,
    df_factory_GetIids,
    df_factory_GetRuntimeClassName,
    df_factory_GetTrustLevel,
    df_factory_CreateDecimalFormatter,
    df_factory_CreateDecimalFormatterWithDigits,
};

static struct decimal_formatter_factory_impl decimal_formatter_factory_inst =
{
    df_activation_vtbl,
    df_factory_vtbl,
    FALSE,
};

static struct decimal_formatter_factory_impl percent_formatter_factory_inst =
{
    df_activation_vtbl,
    df_factory_vtbl,
    TRUE,
};

IActivationFactory *decimal_formatter_factory = (IActivationFactory *)&decimal_formatter_factory_inst.vtbl_activation;
IActivationFactory *percent_formatter_factory = (IActivationFactory *)&percent_formatter_factory_inst.vtbl_activation;
