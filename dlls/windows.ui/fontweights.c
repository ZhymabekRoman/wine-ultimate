/* WinRT Microsoft.UI.Text.FontWeights activation provider
 *
 * The Windows App SDK XAML parser activates Microsoft.UI.Text.FontWeights for
 * font-weight property values (RoGetActivationFactory with the
 * IFontWeightsStatics iid cc390df6-76b0-5807-8b9d-e949a4e623ae).  No framework
 * module answers the class id - it is in none of their activation tables - and
 * the parse dies with CLASS_E_CLASSNOTAVAILABLE (0x80040111, "XAML parsing
 * failed" 0x802b000a), so windows.ui.dll provides it: the eleven static
 * FontWeight properties of the winmd projection and the boxed value type for
 * the parameterized activatable class id
 * "Windows.Foundation.IReference`1<Windows.UI.Text.FontWeight>".
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include <string.h>

#include "private.h"
#include "objbase.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(ui);

/*
 * FontWeights static property values, the documented WinUI font-weight scale
 * (CSS names: Thin 100, ExtraLight 200, Light 300, SemiLight 400, Normal 400,
 * Medium 500, SemiBold 600, Bold 700, ExtraBold 800, Black 900, ExtraBlack 950;
 * SemiLight matches the UWP FontWeights value of 400).
 */
#define FONTWEIGHT_THIN        100
#define FONTWEIGHT_EXTRA_LIGHT 200
#define FONTWEIGHT_LIGHT       300
#define FONTWEIGHT_SEMI_LIGHT  400
#define FONTWEIGHT_NORMAL      400
#define FONTWEIGHT_MEDIUM      500
#define FONTWEIGHT_SEMI_BOLD   600
#define FONTWEIGHT_BOLD        700
#define FONTWEIGHT_EXTRA_BOLD  800
#define FONTWEIGHT_BLACK       900
#define FONTWEIGHT_EXTRA_BLACK 950

struct fontweights_statics
{
    IActivationFactory IActivationFactory_iface;
    IFontWeights IFontWeights_iface;
    IFontWeightsStatics IFontWeightsStatics_iface;
    LONG ref;
};

static inline struct fontweights_statics *impl_fw_from_IActivationFactory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct fontweights_statics, IActivationFactory_iface );
}

static HRESULT WINAPI fontweights_factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct fontweights_statics *impl = impl_fw_from_IActivationFactory( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown )
            || IsEqualGUID( iid, &IID_IInspectable )
            || IsEqualGUID( iid, &IID_IAgileObject )
            || IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        IUnknown_AddRef( iface );
        *out = iface;
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IFontWeightsStatics ))
    {
        IUnknown_AddRef( iface );
        *out = &impl->IFontWeightsStatics_iface;
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IFontWeights ))
    {
        IUnknown_AddRef( iface );
        *out = &impl->IFontWeights_iface;
        return S_OK;
    }

    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI fontweights_factory_AddRef( IActivationFactory *iface )
{
    struct fontweights_statics *impl = impl_fw_from_IActivationFactory( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI fontweights_factory_Release( IActivationFactory *iface )
{
    struct fontweights_statics *impl = impl_fw_from_IActivationFactory( iface );
    return InterlockedDecrement( &impl->ref );
}

static HRESULT WINAPI fontweights_factory_GetIids( IActivationFactory *iface, ULONG *iid_count, IID **iids )
{
    static const IID iid_array[] =
    {
        IID_IActivationFactory,
        IID_IFontWeightsStatics,
        IID_IFontWeights,
    };
    IID *array = CoTaskMemAlloc( sizeof(iid_array) );

    if (!array) return E_OUTOFMEMORY;
    memcpy( array, iid_array, sizeof(iid_array) );
    *iid_count = ARRAY_SIZE(iid_array);
    *iids = array;
    return S_OK;
}

static HRESULT WINAPI fontweights_factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *class_name )
{
    return WindowsCreateString( RuntimeClass_Microsoft_UI_Text_FontWeights,
                                ARRAY_SIZE( RuntimeClass_Microsoft_UI_Text_FontWeights ) - 1, class_name );
}

static HRESULT WINAPI fontweights_factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *trust_level )
{
    if (trust_level) *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI fontweights_factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    /* FontWeights exposes only static properties; the default interface is empty. */
    FIXME( "iface %p, instance %p.\n", iface, instance );
    return E_NOINTERFACE;
}

static const struct IActivationFactoryVtbl fontweights_factory_vtbl =
{
    fontweights_factory_QueryInterface,
    fontweights_factory_AddRef,
    fontweights_factory_Release,
    /* IInspectable methods */
    fontweights_factory_GetIids,
    fontweights_factory_GetRuntimeClassName,
    fontweights_factory_GetTrustLevel,
    /* IActivationFactory methods */
    fontweights_factory_ActivateInstance,
};

DEFINE_IINSPECTABLE( fontweights_default, IFontWeights,
                     struct fontweights_statics, IActivationFactory_iface )
DEFINE_IINSPECTABLE( fontweights_statics, IFontWeightsStatics,
                     struct fontweights_statics, IActivationFactory_iface )

/* The default interface is empty (winmd projection): IInspectable surface only. */
static const IFontWeightsVtbl fontweights_default_vtbl =
{
    fontweights_default_QueryInterface,
    fontweights_default_AddRef,
    fontweights_default_Release,
    fontweights_default_GetIids,
    fontweights_default_GetRuntimeClassName,
    fontweights_default_GetTrustLevel,
};

#define DEFINE_FONTWEIGHT_GETTER( name, weight )                                           \
    static HRESULT WINAPI fontweights_statics_get_##name(                                 \
            IFontWeightsStatics *iface,                     \
            FontWeight *value )                               \
    {                                                                                     \
        TRACE( "iface %p, value %p.\n", iface, value );                                   \
        if (!value) return E_POINTER;                                                     \
        value->Value = (weight);                                                          \
        return S_OK;                                                                      \
    }

DEFINE_FONTWEIGHT_GETTER( Black, FONTWEIGHT_BLACK )
DEFINE_FONTWEIGHT_GETTER( Bold, FONTWEIGHT_BOLD )
DEFINE_FONTWEIGHT_GETTER( ExtraBlack, FONTWEIGHT_EXTRA_BLACK )
DEFINE_FONTWEIGHT_GETTER( ExtraBold, FONTWEIGHT_EXTRA_BOLD )
DEFINE_FONTWEIGHT_GETTER( ExtraLight, FONTWEIGHT_EXTRA_LIGHT )
DEFINE_FONTWEIGHT_GETTER( Light, FONTWEIGHT_LIGHT )
DEFINE_FONTWEIGHT_GETTER( Medium, FONTWEIGHT_MEDIUM )
DEFINE_FONTWEIGHT_GETTER( Normal, FONTWEIGHT_NORMAL )
DEFINE_FONTWEIGHT_GETTER( SemiBold, FONTWEIGHT_SEMI_BOLD )
DEFINE_FONTWEIGHT_GETTER( SemiLight, FONTWEIGHT_SEMI_LIGHT )
DEFINE_FONTWEIGHT_GETTER( Thin, FONTWEIGHT_THIN )

static const IFontWeightsStaticsVtbl fontweights_statics_vtbl =
{
    fontweights_statics_QueryInterface,
    fontweights_statics_AddRef,
    fontweights_statics_Release,
    /* IInspectable methods */
    fontweights_statics_GetIids,
    fontweights_statics_GetRuntimeClassName,
    fontweights_statics_GetTrustLevel,
    /* IFontWeightsStatics methods (winmd declaration order) */
    fontweights_statics_get_Black,
    fontweights_statics_get_Bold,
    fontweights_statics_get_ExtraBlack,
    fontweights_statics_get_ExtraBold,
    fontweights_statics_get_ExtraLight,
    fontweights_statics_get_Light,
    fontweights_statics_get_Medium,
    fontweights_statics_get_Normal,
    fontweights_statics_get_SemiBold,
    fontweights_statics_get_SemiLight,
    fontweights_statics_get_Thin,
};

static struct fontweights_statics fontweights_statics =
{
    { &fontweights_factory_vtbl },
    { &fontweights_default_vtbl },
    { &fontweights_statics_vtbl },
    1
};

IActivationFactory *fontweights_factory = &fontweights_statics.IActivationFactory_iface;

/*
 * Boxed FontWeight: the instance created for the parameterized activatable
 * class id "Windows.Foundation.IReference`1<Windows.UI.Text.FontWeight>"
 * (RoActivateInstance boxing of the value type), answering the closed
 * pinterface iid of IReference<Windows.UI.Text.FontWeight>.
 */
struct fontweight_reference
{
    IReference_FontWeight IReference_FontWeight_iface;
    FontWeight value;
    LONG ref;
};

static inline struct fontweight_reference *impl_from_IReference_FontWeight(
        IReference_FontWeight *iface )
{
    return CONTAINING_RECORD( iface, struct fontweight_reference, IReference_FontWeight_iface );
}

static HRESULT WINAPI fontweight_reference_QueryInterface(
        IReference_FontWeight *iface, REFIID iid, void **out )
{
    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown )
            || IsEqualGUID( iid, &IID_IInspectable )
            || IsEqualGUID( iid, &IID_IAgileObject )
            || IsEqualGUID( iid, &IID_IReference_FontWeight ))
    {
        IUnknown_AddRef( (IUnknown *)iface );
        *out = iface;
        return S_OK;
    }

    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI fontweight_reference_AddRef( IReference_FontWeight *iface )
{
    struct fontweight_reference *impl = impl_from_IReference_FontWeight( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI fontweight_reference_Release( IReference_FontWeight *iface )
{
    struct fontweight_reference *impl = impl_from_IReference_FontWeight( iface );
    LONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        TRACE( "destroying %p\n", iface );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI fontweight_reference_GetIids( IReference_FontWeight *iface,
        ULONG *iid_count, IID **iids )
{
    IID *array;

    if (!iid_count || !iids) return E_INVALIDARG;
    if (!(array = CoTaskMemAlloc( sizeof(IID) * 2 ))) return E_OUTOFMEMORY;
    array[0] = IID_IInspectable;
    array[1] = IID_IReference_FontWeight;
    *iid_count = 2;
    *iids = array;
    return S_OK;
}

static HRESULT WINAPI fontweight_reference_GetRuntimeClassName(
        IReference_FontWeight *iface, HSTRING *class_name )
{
    static const WCHAR name[] = L"Windows.Foundation.IReference`1<Windows.UI.Text.FontWeight>";
    return WindowsCreateString( name, ARRAY_SIZE( name ) - 1, class_name );
}

static HRESULT WINAPI fontweight_reference_GetTrustLevel(
        IReference_FontWeight *iface, TrustLevel *trust_level )
{
    if (trust_level) *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI fontweight_reference_get_Value(
        IReference_FontWeight *iface, FontWeight *value )
{
    struct fontweight_reference *impl = impl_from_IReference_FontWeight( iface );

    TRACE( "iface %p, value %p.\n", iface, value );
    if (!value) return E_POINTER;
    *value = impl->value;
    return S_OK;
}

static const IReference_FontWeightVtbl fontweight_reference_vtbl =
{
    fontweight_reference_QueryInterface,
    fontweight_reference_AddRef,
    fontweight_reference_Release,
    /* IInspectable methods */
    fontweight_reference_GetIids,
    fontweight_reference_GetRuntimeClassName,
    fontweight_reference_GetTrustLevel,
    /* IReference<FontWeight> methods */
    fontweight_reference_get_Value,
};

struct fontweight_reference_factory
{
    IActivationFactory IActivationFactory_iface;
    LONG ref;
};

static inline struct fontweight_reference_factory *impl_fr_from_IActivationFactory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct fontweight_reference_factory, IActivationFactory_iface );
}

static HRESULT WINAPI fontweight_reference_factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown )
            || IsEqualGUID( iid, &IID_IInspectable )
            || IsEqualGUID( iid, &IID_IAgileObject )
            || IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        IUnknown_AddRef( iface );
        *out = iface;
        return S_OK;
    }

    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI fontweight_reference_factory_AddRef( IActivationFactory *iface )
{
    struct fontweight_reference_factory *impl = impl_fr_from_IActivationFactory( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI fontweight_reference_factory_Release( IActivationFactory *iface )
{
    struct fontweight_reference_factory *impl = impl_fr_from_IActivationFactory( iface );
    return InterlockedDecrement( &impl->ref );
}

static HRESULT WINAPI fontweight_reference_factory_GetIids( IActivationFactory *iface, ULONG *iid_count, IID **iids )
{
    static const IID iid_array[] =
    {
        IID_IActivationFactory,
    };
    IID *array = CoTaskMemAlloc( sizeof(iid_array) );

    if (!array) return E_OUTOFMEMORY;
    memcpy( array, iid_array, sizeof(iid_array) );
    *iid_count = ARRAY_SIZE(iid_array);
    *iids = array;
    return S_OK;
}

static HRESULT WINAPI fontweight_reference_factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *class_name )
{
    static const WCHAR name[] = L"Windows.Foundation.IReference`1<Windows.UI.Text.FontWeight>";
    return WindowsCreateString( name, ARRAY_SIZE( name ) - 1, class_name );
}

static HRESULT WINAPI fontweight_reference_factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *trust_level )
{
    if (trust_level) *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI fontweight_reference_factory_ActivateInstance( IActivationFactory *iface,
        IInspectable **instance )
{
    struct fontweight_reference *impl;

    TRACE( "iface %p, instance %p.\n", iface, instance );

    if (!instance) return E_POINTER;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;

    impl->IReference_FontWeight_iface.lpVtbl = &fontweight_reference_vtbl;
    impl->ref = 1;
    *instance = (IInspectable *)&impl->IReference_FontWeight_iface;
    return S_OK;
}

static const struct IActivationFactoryVtbl fontweight_reference_factory_vtbl =
{
    fontweight_reference_factory_QueryInterface,
    fontweight_reference_factory_AddRef,
    fontweight_reference_factory_Release,
    /* IInspectable methods */
    fontweight_reference_factory_GetIids,
    fontweight_reference_factory_GetRuntimeClassName,
    fontweight_reference_factory_GetTrustLevel,
    /* IActivationFactory methods */
    fontweight_reference_factory_ActivateInstance,
};

static struct fontweight_reference_factory fontweight_reference_factory_obj =
{
    { &fontweight_reference_factory_vtbl },
    1
};

IActivationFactory *fontweight_reference_factory = &fontweight_reference_factory_obj.IActivationFactory_iface;
