/* WinRT Windows.UI.Xaml UWP XamlReader / XamlBindingHelpers hosts
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

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <excpt.h>
#include "wine/exception.h"
#include "uwp_private.h"

static HRESULT WINAPI xaml_reader_Load( void *iface, HSTRING xaml, IInspectable **out )
{
    UINT32 wlen = 0;
    const WCHAR *raw = WindowsGetStringRawBuffer( xaml, &wlen );
    DWORD byte_sz = wlen * sizeof(WCHAR);
    IInspectable *root = NULL;

    (void)iface;
    if (!out) return E_POINTER;
    *out = NULL;
    if (!raw || byte_sz < 4) return S_OK;

    if (memcmp( raw, "XBF\0", 4 ) && wcsstr( raw, L"SolidColorBrush" ))
    {
        if (SUCCEEDED(element_create_typed( L"Microsoft.UI.Xaml.Media.SolidColorBrush", (void **)&root )) && root)
            *out = root;
        return S_OK;
    }

    if (SUCCEEDED(uwp_element_create( L"Windows.UI.Xaml.Controls.Grid", NULL, NULL, (void **)&root )) && root)
    {
        if (!memcmp( raw, "XBF\0", 4 ))
            uwp_parse_xbf_internal( (const BYTE *)raw, byte_sz, root, NULL, FALSE, NULL, -1, NULL, NULL, NULL, NULL, FALSE );
        *out = root;
    }
    return S_OK;
}

static HRESULT WINAPI xbh_ConvertValue( void *iface, TypeName *type, IInspectable *val, IInspectable **out )
{
    WCHAR str_buf[512] = {0};
    BOOL has_str = FALSE;
    const WCHAR *tname = (type && type->Name) ? WindowsGetStringRawBuffer( type->Name, NULL ) : NULL;

    if (!out) return E_POINTER;
    *out = NULL;
    if (!val) return S_OK;

    if (tname && tname[0])
    {
        has_str = uwp_unbox_string( val, str_buf, ARRAY_SIZE(str_buf) );
        if (!wcscmp( tname, L"Windows.Foundation.Uri" ) && has_str && str_buf[0])
        {
            static const GUID IID_IUriFactory_local =
                { 0x44a9796f, 0x723e, 0x4fdf, { 0xa2, 0x18, 0x03, 0x3e, 0x75, 0xb0, 0xc0, 0x84 } };
            HSTRING h_cls = NULL, h_uri = NULL;
            void *ufac = NULL;
            if (SUCCEEDED(WindowsCreateString( L"Windows.Foundation.Uri", 22, &h_cls )))
            {
                if (SUCCEEDED(RoGetActivationFactory( h_cls, &IID_IUriFactory_local, &ufac )) && ufac)
                {
                    HRESULT (WINAPI *create_uri)(void *, HSTRING, void **) = (void *)(*(void ***)ufac)[6];
                    if (create_uri && SUCCEEDED(WindowsCreateString( str_buf, wcslen(str_buf), &h_uri )))
                    {
                        create_uri( ufac, h_uri, (void **)out );
                        WindowsDeleteString( h_uri );
                    }
                    IUnknown_Release( (IUnknown *)ufac );
                }
                WindowsDeleteString( h_cls );
            }
            if (*out) return S_OK;
        }
        if ((!wcscmp( tname, L"Windows.UI.Xaml.Media.ImageSource" ) ||
             !wcscmp( tname, L"Windows.UI.Xaml.Media.Imaging.BitmapImage" ) ||
             !wcscmp( tname, L"Microsoft.UI.Xaml.Media.ImageSource" ) ||
             !wcscmp( tname, L"Microsoft.UI.Xaml.Media.Imaging.BitmapImage" )) && has_str)
        {
            void *bi = NULL;
            if (SUCCEEDED(uwp_element_create( L"Windows.UI.Xaml.Media.Imaging.BitmapImage", NULL, NULL, &bi )) && bi)
            {
                struct uwp_element *bi_el = uwp_elem_from_any( bi );
                if (bi_el && str_buf[0])
                    lstrcpynW( bi_el->image_src, str_buf, ARRAY_SIZE(bi_el->image_src) );
                *out = (IInspectable *)bi;
                return S_OK;
            }
        }
        if ((!wcscmp( tname, L"Windows.UI.Xaml.Media.Brush" ) ||
             !wcscmp( tname, L"Windows.UI.Xaml.Media.SolidColorBrush" ) ||
             !wcscmp( tname, L"Microsoft.UI.Xaml.Media.Brush" ) ||
             !wcscmp( tname, L"Microsoft.UI.Xaml.Media.SolidColorBrush" )) && has_str)
        {
            void *br = NULL;
            if (SUCCEEDED(uwp_element_create( L"Windows.UI.Xaml.Media.SolidColorBrush", NULL, NULL, &br )) && br)
            {
                struct uwp_element *br_el = uwp_elem_from_any( br );
                UINT32 argb = 0;
                if (br_el && str_buf[0] && uwp_parse_color_string( str_buf, &argb ))
                {
                    br_el->brush_argb = argb;
                    br_el->has_brush_argb = TRUE;
                    br_el->brush_opacity = (double)((argb >> 24) & 0xff) / 255.0;
                }
                *out = (IInspectable *)br;
                return S_OK;
            }
        }
        if ((!wcscmp( tname, L"Windows.UI.Xaml.Visibility" ) ||
             !wcscmp( tname, L"Microsoft.UI.Xaml.Visibility" )) && has_str)
        {
            INT32 vis = !wcsicmp( str_buf, L"Collapsed" ) ? 1 :
                        (!wcsicmp( str_buf, L"Visible" ) ? 0 : _wtoi( str_buf ));
            *out = box_int32_value( vis );
            if (*out) return S_OK;
        }
        if ((!wcscmp( tname, L"Windows.UI.Xaml.HorizontalAlignment" ) ||
             !wcscmp( tname, L"Microsoft.UI.Xaml.HorizontalAlignment" )) && has_str)
        {
            INT32 v = !wcsicmp( str_buf, L"Left" ) ? 0 :
                      !wcsicmp( str_buf, L"Center" ) ? 1 :
                      !wcsicmp( str_buf, L"Right" ) ? 2 :
                      !wcsicmp( str_buf, L"Stretch" ) ? 3 : _wtoi( str_buf );
            *out = box_int32_value( v );
            if (*out) return S_OK;
        }
        if ((!wcscmp( tname, L"Windows.UI.Xaml.VerticalAlignment" ) ||
             !wcscmp( tname, L"Microsoft.UI.Xaml.VerticalAlignment" )) && has_str)
        {
            INT32 v = !wcsicmp( str_buf, L"Top" ) ? 0 :
                      !wcsicmp( str_buf, L"Center" ) ? 1 :
                      !wcsicmp( str_buf, L"Bottom" ) ? 2 :
                      !wcsicmp( str_buf, L"Stretch" ) ? 3 : _wtoi( str_buf );
            *out = box_int32_value( v );
            if (*out) return S_OK;
        }
        if ((!wcscmp( tname, L"Windows.UI.Xaml.Controls.Orientation" ) ||
             !wcscmp( tname, L"Microsoft.UI.Xaml.Controls.Orientation" )) && has_str)
        {
            INT32 v = !wcsicmp( str_buf, L"Horizontal" ) ? 1 :
                      !wcsicmp( str_buf, L"Vertical" ) ? 0 : _wtoi( str_buf );
            *out = box_int32_value( v );
            if (*out) return S_OK;
        }
        if ((!wcscmp( tname, L"Windows.UI.Xaml.TextWrapping" ) ||
             !wcscmp( tname, L"Microsoft.UI.Xaml.TextWrapping" )) && has_str)
        {
            INT32 v = !wcsicmp( str_buf, L"NoWrap" ) ? 1 :
                      !wcsicmp( str_buf, L"Wrap" ) ? 2 :
                      !wcsicmp( str_buf, L"WrapWholeWords" ) ? 3 : _wtoi( str_buf );
            *out = box_int32_value( v );
            if (*out) return S_OK;
        }
        if ((!wcscmp( tname, L"Windows.UI.Xaml.Media.Stretch" ) ||
             !wcscmp( tname, L"Microsoft.UI.Xaml.Media.Stretch" )) && has_str)
        {
            INT32 v = !wcsicmp( str_buf, L"None" ) ? 0 :
                      !wcsicmp( str_buf, L"Fill" ) ? 1 :
                      !wcsicmp( str_buf, L"Uniform" ) ? 2 :
                      !wcsicmp( str_buf, L"UniformToFill" ) ? 3 : _wtoi( str_buf );
            *out = box_int32_value( v );
            if (*out) return S_OK;
        }
        if ((!wcscmp( tname, L"Boolean" ) || !wcscmp( tname, L"System.Boolean" )) && has_str)
        {
            *out = box_bool_value( !wcsicmp( str_buf, L"True" ) ? TRUE : FALSE );
            if (*out) return S_OK;
        }
        if ((!wcscmp( tname, L"Int32" ) || !wcscmp( tname, L"System.Int32" )) && has_str)
        {
            *out = box_int32_value( _wtoi( str_buf ) );
            if (*out) return S_OK;
        }
        if ((!wcscmp( tname, L"Double" ) || !wcscmp( tname, L"System.Double" )) && has_str)
        {
            *out = box_double_value( wcstod( str_buf, NULL ) );
            if (*out) return S_OK;
        }
        /* For non-primitive reference types (not String/Object/IInspectable), never return
         * an unconverted IPropertyValue — CoreRT casts it immediately and failfasts. */
        if (type->Kind != 0 &&
            wcscmp( tname, L"String" ) && wcscmp( tname, L"System.String" ) &&
            wcscmp( tname, L"Object" ) && wcscmp( tname, L"System.Object" ) &&
            wcscmp( tname, L"IInspectable" ))
        {
            IPropertyValue *pv = NULL;
            if (SUCCEEDED(IInspectable_QueryInterface( val, &IID_IPropertyValue_g, (void **)&pv )) && pv)
            {
                IPropertyValue_Release( pv );
                *out = NULL;
                return S_OK;
            }
        }
    }

    *out = val;
    IInspectable_AddRef( *out );
    return S_OK;
}

static HRESULT WINAPI xbh_get_DataTemplateComponentProperty( void *iface, void **out )
{
    static IDependencyProperty *dp = NULL;
    if (!out) return E_POINTER;
    if (!dp) dependency_property_create( NULL, &dp );
    *out = dp;
    if (dp) IDependencyProperty_AddRef( dp );
    return S_OK;
}
static HRESULT WINAPI xbh_GetDataTemplateComponent( void *iface, void *el, void **out )
{
    if (out) *out = NULL;
    return S_OK;
}
static HRESULT WINAPI xbh_QI( void *iface, REFIID iid, void **out );
static ULONG WINAPI xbh_AddRef( void *iface ) { return 2; }
static ULONG WINAPI xbh_Release( void *iface ) { return 1; }

static const void *xbh_statics_vtbl[32] =
{
    [0] = xbh_QI,
    [1] = xbh_AddRef,
    [2] = xbh_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = xbh_get_DataTemplateComponentProperty,
    [7] = xbh_GetDataTemplateComponent,
    [8 ... 10] = uwp_generic_noop,
    [11] = xbh_ConvertValue, /* slot 11: ConvertValue(TypeName, object, object**) */
    [12 ... 31] = uwp_generic_noop,
};
static struct xbh_statics_obj_s { const void *vtbl; } xbh_statics_obj = { xbh_statics_vtbl };

static const void *xbh_vtbl[32] =
{
    [0] = xbh_QI,
    [1] = xbh_AddRef,
    [2] = xbh_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = xaml_reader_Load,  /* IXamlReaderStatics::Load(HSTRING, object**) */
    [7] = xaml_reader_Load,  /* IXamlReaderStatics::LoadWithInitialTemplateValidation(HSTRING, object**) */
    [8 ... 10] = uwp_generic_noop,
    [11] = xbh_ConvertValue, /* slot 11: ConvertValue(TypeName, object, object**) */
    [12 ... 31] = uwp_generic_noop,
};
struct xbh_factory_obj_s { const void *vtbl; } xbh_factory_obj = { xbh_vtbl };

static HRESULT WINAPI xbh_QI( void *iface, REFIID iid, void **out )
{
    static const GUID IID_IXamlReaderStatics_WinUI3 =
        { 0x82a4cd9e, 0x435e, 0x5aeb, { 0x8c, 0x4f, 0x30, 0x0c, 0xec, 0xe4, 0x5c, 0xae } };
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IXamlBindingHelperStatics_UWP ))
    {
        *out = &xbh_statics_obj;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ) ||
        IsEqualGUID( iid, &IID_IXamlReaderStatics_UWP ) ||
        IsEqualGUID( iid, &IID_IXamlReaderStatics_WinUI3 ))
    {
        *out = &xbh_factory_obj;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

/* Universal UWP Composable / Statics Factory for stock XAML classes */
const GUID IID_IFrameFactory_UWP =
    { 0x02ee93d4, 0x448e, 0x469e, { 0x97, 0x99, 0x0a, 0x8a, 0x1f, 0x70, 0xf1, 0x71 } };
const GUID IID_IRoutedEventArgsFactory_UWP =
    { 0x7c820996, 0x475c, 0x4d3c, { 0x98, 0x12, 0x0b, 0x4a, 0x1d, 0xf2, 0x3b, 0x52 } };
const GUID IID_IReferenceTracker_UWP =
    { 0x11d3b13a, 0x180e, 0x4789, { 0xa8, 0xbe, 0x77, 0x12, 0x88, 0x28, 0x93, 0xe6 } };
const GUID IID_IReferenceTrackerTarget_UWP =
    { 0x5c13e51c, 0x4f32, 0x4726, { 0xa3, 0xfd, 0xf3, 0xed, 0xd6, 0x3d, 0xa3, 0xa0 } };
const GUID IID_IReferenceTrackerTarget_UWP2 =
    { 0x64e71ddf, 0x2424, 0x4ec5, { 0x86, 0x27, 0x15, 0x05, 0x08, 0x96, 0xd9, 0xb6 } };

const GUID IID_IVisualStateManagerFactory_UWP =
    { 0x3976547f, 0x7089, 0x4801, { 0x8f, 0x1d, 0xaa, 0xb7, 0x80, 0x90, 0xd1, 0xa0 } };
const GUID IID_IVisualTreeHelperStatics_UWP =
    { 0xe75758c4, 0xd25d, 0x4b1d, { 0x97, 0x1f, 0x59, 0x6f, 0x17, 0xf1, 0x2b, 0xaa } };


struct uwp_class_factory **g_dyn_factories = NULL;
WCHAR **g_dyn_factory_names = NULL;
int g_dyn_factory_cnt = 0;
int g_dyn_factory_cap = 0;
SRWLOCK g_dyn_factories_lock = SRWLOCK_INIT;

