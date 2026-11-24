/* WinRT Windows.Globalization.NumberFormatting rounder implementations
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

static ULONG WINAPI rounder_factory_AddRef( void *iface ) { return 2; }
static ULONG WINAPI rounder_factory_Release( void *iface ) { return 1; }
static HRESULT WINAPI rounder_factory_GetIids( void *iface, ULONG *count, IID **iids )
{ if (count) *count = 0; if (iids) *iids = NULL; return S_OK; }
static HRESULT WINAPI rounder_factory_GetTrustLevel( void *iface, TrustLevel *level )
{ if (level) *level = BaseTrust; return S_OK; }

/* =========================================================================
 * Windows.Globalization.NumberFormatting.IncrementNumberRounder
 * ========================================================================= */
static const GUID IID_INumberRounder =
    { 0x5473c375, 0x38ed, 0x4631, { 0xb8, 0x0c, 0xef, 0x34, 0xfc, 0x48, 0xb7, 0xf5 } };
static const GUID IID_IIncrementNumberRounder =
    { 0x70a64ff8, 0x66ab, 0x4155, { 0x9d, 0xa1, 0x73, 0x9e, 0x46, 0x76, 0x45, 0x43 } };

struct increment_rounder
{
    const void *vtbl_rounder;
    const void *vtbl_incr;
    LONG ref;
    INT32 algorithm;
    double increment;
};

static inline struct increment_rounder *rounder_from_incr( void *iface )
{
    return CONTAINING_RECORD( iface, struct increment_rounder, vtbl_incr );
}

static HRESULT WINAPI increment_rounder_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct increment_rounder *impl = iface;
    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_INumberRounder ))
        *out = &impl->vtbl_rounder;
    else if (IsEqualGUID( iid, &IID_IIncrementNumberRounder ))
        *out = &impl->vtbl_incr;
    else
        return E_NOINTERFACE;
    InterlockedIncrement( &impl->ref );
    return S_OK;
}

static ULONG WINAPI increment_rounder_AddRef( void *iface )
{
    struct increment_rounder *impl = iface;
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI increment_rounder_Release( void *iface )
{
    struct increment_rounder *impl = iface;
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref) free( impl );
    return ref;
}

static HRESULT WINAPI increment_rounder_GetIids( void *iface, ULONG *count, IID **iids )
{
    if (!count || !iids) return E_POINTER;
    *count = 2;
    *iids = CoTaskMemAlloc( 2 * sizeof(IID) );
    if (!*iids) return E_OUTOFMEMORY;
    (*iids)[0] = IID_INumberRounder;
    (*iids)[1] = IID_IIncrementNumberRounder;
    return S_OK;
}

static HRESULT WINAPI increment_rounder_GetRuntimeClassName( void *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.Globalization.NumberFormatting.IncrementNumberRounder", 60, name );
}

static HRESULT WINAPI increment_rounder_GetTrustLevel( void *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}

static double round_by_increment( struct increment_rounder *impl, double value )
{
    double inc = impl->increment;
    if (inc <= 0.0) return value;
    return round( value / inc ) * inc;
}

static HRESULT WINAPI increment_rounder_RoundInt32( void *iface, INT32 value, INT32 *result )
{
    struct increment_rounder *impl = iface;
    if (!result) return E_POINTER;
    *result = (INT32)round_by_increment( impl, (double)value );
    return S_OK;
}

static HRESULT WINAPI increment_rounder_RoundUInt32( void *iface, UINT32 value, UINT32 *result )
{
    struct increment_rounder *impl = iface;
    if (!result) return E_POINTER;
    *result = (UINT32)round_by_increment( impl, (double)value );
    return S_OK;
}

static HRESULT WINAPI increment_rounder_RoundInt64( void *iface, INT64 value, INT64 *result )
{
    struct increment_rounder *impl = iface;
    if (!result) return E_POINTER;
    *result = (INT64)round_by_increment( impl, (double)value );
    return S_OK;
}

static HRESULT WINAPI increment_rounder_RoundUInt64( void *iface, UINT64 value, UINT64 *result )
{
    struct increment_rounder *impl = iface;
    if (!result) return E_POINTER;
    *result = (UINT64)round_by_increment( impl, (double)value );
    return S_OK;
}

static HRESULT WINAPI increment_rounder_RoundSingle( void *iface, float value, float *result )
{
    struct increment_rounder *impl = iface;
    if (!result) return E_POINTER;
    *result = (float)round_by_increment( impl, (double)value );
    return S_OK;
}

static HRESULT WINAPI increment_rounder_RoundDouble( void *iface, double value, double *result )
{
    struct increment_rounder *impl = iface;
    if (!result) return E_POINTER;
    *result = round_by_increment( impl, value );
    return S_OK;
}

static HRESULT WINAPI increment_rounder_incr_QueryInterface( void *iface, REFIID iid, void **out )
{
    return increment_rounder_QueryInterface( rounder_from_incr( iface ), iid, out );
}

static ULONG WINAPI increment_rounder_incr_AddRef( void *iface )
{
    return increment_rounder_AddRef( rounder_from_incr( iface ) );
}

static ULONG WINAPI increment_rounder_incr_Release( void *iface )
{
    return increment_rounder_Release( rounder_from_incr( iface ) );
}

static HRESULT WINAPI increment_rounder_get_RoundingAlgorithm( void *iface, INT32 *value )
{
    struct increment_rounder *impl = rounder_from_incr( iface );
    if (!value) return E_POINTER;
    *value = impl->algorithm;
    return S_OK;
}

static HRESULT WINAPI increment_rounder_put_RoundingAlgorithm( void *iface, INT32 value )
{
    struct increment_rounder *impl = rounder_from_incr( iface );
    impl->algorithm = value;
    return S_OK;
}

static HRESULT WINAPI increment_rounder_get_Increment( void *iface, double *value )
{
    struct increment_rounder *impl = rounder_from_incr( iface );
    if (!value) return E_POINTER;
    *value = impl->increment;
    return S_OK;
}

static HRESULT WINAPI increment_rounder_put_Increment( void *iface, double value )
{
    struct increment_rounder *impl = rounder_from_incr( iface );
    if (value <= 0.0) return E_INVALIDARG;
    impl->increment = value;
    return S_OK;
}

static const void *increment_rounder_vtbl[] =
{
    increment_rounder_QueryInterface,
    increment_rounder_AddRef,
    increment_rounder_Release,
    increment_rounder_GetIids,
    increment_rounder_GetRuntimeClassName,
    increment_rounder_GetTrustLevel,
    increment_rounder_RoundInt32,
    increment_rounder_RoundUInt32,
    increment_rounder_RoundInt64,
    increment_rounder_RoundUInt64,
    increment_rounder_RoundSingle,
    increment_rounder_RoundDouble,
};

static const void *increment_rounder_incr_vtbl[] =
{
    increment_rounder_incr_QueryInterface,
    increment_rounder_incr_AddRef,
    increment_rounder_incr_Release,
    increment_rounder_GetIids,
    increment_rounder_GetRuntimeClassName,
    increment_rounder_GetTrustLevel,
    increment_rounder_get_RoundingAlgorithm,
    increment_rounder_put_RoundingAlgorithm,
    increment_rounder_get_Increment,
    increment_rounder_put_Increment,
};

static HRESULT WINAPI increment_rounder_factory_QueryInterface( void *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = iface;
        return S_OK;
    }
    return E_NOINTERFACE;
}

static HRESULT WINAPI increment_rounder_factory_ActivateInstance( void *iface, IInspectable **out )
{
    struct increment_rounder *impl;
    if (!out) return E_POINTER;
    impl = calloc( 1, sizeof(*impl) );
    if (!impl) return E_OUTOFMEMORY;
    impl->vtbl_rounder = increment_rounder_vtbl;
    impl->vtbl_incr = increment_rounder_incr_vtbl;
    impl->ref = 1;
    impl->algorithm = 4; /* RoundHalfUp */
    impl->increment = 1.0;
    *out = (IInspectable *)&impl->vtbl_rounder;
    return S_OK;
}

static const void *increment_rounder_factory_vtbl[] =
{
    increment_rounder_factory_QueryInterface,
    rounder_factory_AddRef,
    rounder_factory_Release,
    rounder_factory_GetIids,
    increment_rounder_GetRuntimeClassName,
    rounder_factory_GetTrustLevel,
    increment_rounder_factory_ActivateInstance,
};

static struct { const void **vtbl; } increment_rounder_factory_obj = { increment_rounder_factory_vtbl };

IActivationFactory *increment_rounder_factory = (IActivationFactory *)&increment_rounder_factory_obj;

/* =========================================================================
 * Windows.Globalization.NumberFormatting.SignificantDigitsNumberRounder
 * ========================================================================= */
static const GUID IID_ISignificantDigitsNumberRounder =
    { 0xf5941bca, 0x6646, 0x4913, { 0x8c, 0x76, 0x1b, 0x19, 0x1f, 0xf9, 0x4d, 0xfd } };

struct significant_digits_rounder
{
    const void *vtbl_rounder;
    const void *vtbl_sig;
    LONG ref;
    INT32 algorithm;
    UINT32 significant_digits;
};

static inline struct significant_digits_rounder *rounder_from_sig( void *iface )
{
    return CONTAINING_RECORD( iface, struct significant_digits_rounder, vtbl_sig );
}

static double apply_rounding_algorithm( double value, INT32 algorithm )
{
    double f, diff;

    switch (algorithm)
    {
    case 0: /* RoundingAlgorithm_None */
        return value;
    case 1: /* RoundingAlgorithm_RoundDown */
        return floor( value );
    case 2: /* RoundingAlgorithm_RoundUp */
        return ceil( value );
    case 3: /* RoundingAlgorithm_RoundTowardsZero */
        return trunc( value );
    case 4: /* RoundingAlgorithm_RoundAwayFromZero */
        return value >= 0.0 ? ceil( value ) : floor( value );
    case 5: /* RoundingAlgorithm_RoundHalfDown */
        return ceil( value - 0.5 );
    case 6: /* RoundingAlgorithm_RoundHalfUp */
        return floor( value + 0.5 );
    case 7: /* RoundingAlgorithm_RoundHalfTowardsZero */
        return value >= 0.0 ? ceil( value - 0.5 ) : floor( value + 0.5 );
    case 8: /* RoundingAlgorithm_RoundHalfAwayFromZero */
        return round( value );
    case 9: /* RoundingAlgorithm_RoundHalfToEven */
        f = floor( value );
        diff = value - f;
        if (diff < 0.5) return f;
        if (diff > 0.5) return f + 1.0;
        return fmod( f, 2.0 ) == 0.0 ? f : f + 1.0;
    case 10: /* RoundingAlgorithm_RoundHalfToOdd */
        f = floor( value );
        diff = value - f;
        if (diff < 0.5) return f;
        if (diff > 0.5) return f + 1.0;
        return fmod( f, 2.0 ) != 0.0 ? f : f + 1.0;
    default:
        return floor( value + 0.5 );
    }
}

static double round_significant_digits( double value, UINT32 sig_digits, INT32 algorithm )
{
    int exp;
    double factor, scaled, rounded;

    if (value == 0.0 || !isfinite( value ) || sig_digits == 0)
        return value;

    exp = (int)sig_digits - 1 - (int)floor( log10( fabs( value ) ) );
    if (exp >= 0)
    {
        factor = pow( 10.0, exp );
        if (!isfinite( factor ) || factor == 0.0) return value;
        scaled = value * factor;
        if (!isfinite( scaled )) return value;
        rounded = apply_rounding_algorithm( scaled, algorithm );
        return rounded / factor;
    }
    else
    {
        factor = pow( 10.0, -exp );
        if (!isfinite( factor ) || factor == 0.0) return value;
        scaled = value / factor;
        if (!isfinite( scaled )) return value;
        rounded = apply_rounding_algorithm( scaled, algorithm );
        return rounded * factor;
    }
}

static HRESULT WINAPI significant_digits_rounder_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct significant_digits_rounder *impl = iface;
    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_INumberRounder ))
        *out = &impl->vtbl_rounder;
    else if (IsEqualGUID( iid, &IID_ISignificantDigitsNumberRounder ))
        *out = &impl->vtbl_sig;
    else
        return E_NOINTERFACE;
    InterlockedIncrement( &impl->ref );
    return S_OK;
}

static ULONG WINAPI significant_digits_rounder_AddRef( void *iface )
{
    struct significant_digits_rounder *impl = iface;
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI significant_digits_rounder_Release( void *iface )
{
    struct significant_digits_rounder *impl = iface;
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref) free( impl );
    return ref;
}

static HRESULT WINAPI significant_digits_rounder_GetIids( void *iface, ULONG *count, IID **iids )
{
    if (!count || !iids) return E_POINTER;
    *count = 2;
    *iids = CoTaskMemAlloc( 2 * sizeof(IID) );
    if (!*iids) return E_OUTOFMEMORY;
    (*iids)[0] = IID_INumberRounder;
    (*iids)[1] = IID_ISignificantDigitsNumberRounder;
    return S_OK;
}

static HRESULT WINAPI significant_digits_rounder_GetRuntimeClassName( void *iface, HSTRING *name )
{
    static const WCHAR class_name[] = L"Windows.Globalization.NumberFormatting.SignificantDigitsNumberRounder";
    return WindowsCreateString( class_name, ARRAY_SIZE(class_name) - 1, name );
}

static HRESULT WINAPI significant_digits_rounder_GetTrustLevel( void *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI significant_digits_rounder_RoundInt32( void *iface, INT32 value, INT32 *result )
{
    struct significant_digits_rounder *impl = iface;
    if (!result) return E_POINTER;
    *result = (INT32)round_significant_digits( (double)value, impl->significant_digits, impl->algorithm );
    return S_OK;
}

static HRESULT WINAPI significant_digits_rounder_RoundUInt32( void *iface, UINT32 value, UINT32 *result )
{
    struct significant_digits_rounder *impl = iface;
    if (!result) return E_POINTER;
    *result = (UINT32)round_significant_digits( (double)value, impl->significant_digits, impl->algorithm );
    return S_OK;
}

static HRESULT WINAPI significant_digits_rounder_RoundInt64( void *iface, INT64 value, INT64 *result )
{
    struct significant_digits_rounder *impl = iface;
    if (!result) return E_POINTER;
    *result = (INT64)round_significant_digits( (double)value, impl->significant_digits, impl->algorithm );
    return S_OK;
}

static HRESULT WINAPI significant_digits_rounder_RoundUInt64( void *iface, UINT64 value, UINT64 *result )
{
    struct significant_digits_rounder *impl = iface;
    if (!result) return E_POINTER;
    *result = (UINT64)round_significant_digits( (double)value, impl->significant_digits, impl->algorithm );
    return S_OK;
}

static HRESULT WINAPI significant_digits_rounder_RoundSingle( void *iface, float value, float *result )
{
    struct significant_digits_rounder *impl = iface;
    if (!result) return E_POINTER;
    *result = (float)round_significant_digits( (double)value, impl->significant_digits, impl->algorithm );
    return S_OK;
}

static HRESULT WINAPI significant_digits_rounder_RoundDouble( void *iface, double value, double *result )
{
    struct significant_digits_rounder *impl = iface;
    if (!result) return E_POINTER;
    *result = round_significant_digits( value, impl->significant_digits, impl->algorithm );
    return S_OK;
}

static HRESULT WINAPI significant_digits_rounder_sig_QueryInterface( void *iface, REFIID iid, void **out )
{
    return significant_digits_rounder_QueryInterface( rounder_from_sig( iface ), iid, out );
}

static ULONG WINAPI significant_digits_rounder_sig_AddRef( void *iface )
{
    return significant_digits_rounder_AddRef( rounder_from_sig( iface ) );
}

static ULONG WINAPI significant_digits_rounder_sig_Release( void *iface )
{
    return significant_digits_rounder_Release( rounder_from_sig( iface ) );
}

static HRESULT WINAPI significant_digits_rounder_get_RoundingAlgorithm( void *iface, INT32 *value )
{
    struct significant_digits_rounder *impl = rounder_from_sig( iface );
    if (!value) return E_POINTER;
    *value = impl->algorithm;
    return S_OK;
}

static HRESULT WINAPI significant_digits_rounder_put_RoundingAlgorithm( void *iface, INT32 value )
{
    struct significant_digits_rounder *impl = rounder_from_sig( iface );
    impl->algorithm = value;
    return S_OK;
}

static HRESULT WINAPI significant_digits_rounder_get_SignificantDigits( void *iface, UINT32 *value )
{
    struct significant_digits_rounder *impl = rounder_from_sig( iface );
    if (!value) return E_POINTER;
    *value = impl->significant_digits;
    return S_OK;
}

static HRESULT WINAPI significant_digits_rounder_put_SignificantDigits( void *iface, UINT32 value )
{
    struct significant_digits_rounder *impl = rounder_from_sig( iface );
    if (value == 0) return E_INVALIDARG;
    impl->significant_digits = value;
    return S_OK;
}

static const void *significant_digits_rounder_vtbl[] =
{
    significant_digits_rounder_QueryInterface,
    significant_digits_rounder_AddRef,
    significant_digits_rounder_Release,
    significant_digits_rounder_GetIids,
    significant_digits_rounder_GetRuntimeClassName,
    significant_digits_rounder_GetTrustLevel,
    significant_digits_rounder_RoundInt32,
    significant_digits_rounder_RoundUInt32,
    significant_digits_rounder_RoundInt64,
    significant_digits_rounder_RoundUInt64,
    significant_digits_rounder_RoundSingle,
    significant_digits_rounder_RoundDouble,
};

static const void *significant_digits_rounder_sig_vtbl[] =
{
    significant_digits_rounder_sig_QueryInterface,
    significant_digits_rounder_sig_AddRef,
    significant_digits_rounder_sig_Release,
    significant_digits_rounder_GetIids,
    significant_digits_rounder_GetRuntimeClassName,
    significant_digits_rounder_GetTrustLevel,
    significant_digits_rounder_get_RoundingAlgorithm,
    significant_digits_rounder_put_RoundingAlgorithm,
    significant_digits_rounder_get_SignificantDigits,
    significant_digits_rounder_put_SignificantDigits,
};

static HRESULT WINAPI significant_digits_rounder_factory_QueryInterface( void *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = iface;
        return S_OK;
    }
    return E_NOINTERFACE;
}

static HRESULT WINAPI significant_digits_rounder_factory_ActivateInstance( void *iface, IInspectable **out )
{
    struct significant_digits_rounder *impl;
    if (!out) return E_POINTER;
    impl = calloc( 1, sizeof(*impl) );
    if (!impl) return E_OUTOFMEMORY;
    impl->vtbl_rounder = significant_digits_rounder_vtbl;
    impl->vtbl_sig = significant_digits_rounder_sig_vtbl;
    impl->ref = 1;
    impl->algorithm = 6; /* RoundingAlgorithm_RoundHalfUp */
    impl->significant_digits = 1;
    *out = (IInspectable *)&impl->vtbl_rounder;
    return S_OK;
}

static const void *significant_digits_rounder_factory_vtbl[] =
{
    significant_digits_rounder_factory_QueryInterface,
    rounder_factory_AddRef,
    rounder_factory_Release,
    rounder_factory_GetIids,
    significant_digits_rounder_GetRuntimeClassName,
    rounder_factory_GetTrustLevel,
    significant_digits_rounder_factory_ActivateInstance,
};

static struct { const void **vtbl; } significant_digits_rounder_factory_obj = { significant_digits_rounder_factory_vtbl };

IActivationFactory *significant_digits_rounder_factory = (IActivationFactory *)&significant_digits_rounder_factory_obj;
