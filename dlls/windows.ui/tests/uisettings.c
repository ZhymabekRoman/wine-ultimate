/*
 * Copyright (C) 2023 Mohamad Al-Jaf
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
#define COBJMACROS
#include "initguid.h"
#include <stdarg.h>

#include "windef.h"
#include "winbase.h"
#include "winstring.h"
#include "weakreference.h"

#include "roapi.h"

#define WIDL_using_Windows_Foundation
#include "windows.foundation.h"
#define WIDL_using_Windows_UI
#include "windows.ui.h"
#define WIDL_using_Windows_UI_ViewManagement
#include "windows.ui.viewmanagement.h"

#include "wine/test.h"

static const WCHAR *subkey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";
static const WCHAR *name = L"AppsUseLightTheme";
static const HKEY root = HKEY_CURRENT_USER;

#define check_interface( obj, iid, exp ) check_interface_( __LINE__, obj, iid, exp )
static void check_interface_( unsigned int line, void *obj, const IID *iid, BOOL supported )
{
    IUnknown *iface = obj;
    IUnknown *unk;
    HRESULT hr, expected_hr;

    expected_hr = supported ? S_OK : E_NOINTERFACE;

    hr = IUnknown_QueryInterface( iface, iid, (void **)&unk );
    ok_( __FILE__, line )( hr == expected_hr, "Got hr %#lx, expected %#lx.\n", hr, expected_hr );
    if (SUCCEEDED(hr))
        IUnknown_Release( unk );
}

static DWORD get_app_theme(void)
{
    DWORD ret = 0, len = sizeof(ret), type;
    HKEY hkey;

    if (RegOpenKeyExW( root, subkey, 0, KEY_QUERY_VALUE, &hkey )) return 1;
    if (RegQueryValueExW( hkey, name, NULL, &type, (BYTE *)&ret, &len ) || type != REG_DWORD) ret = 1;
    RegCloseKey( hkey );
    return ret;
}

static DWORD set_app_theme( DWORD mode )
{
    DWORD ret = 1, len = sizeof(ret);
    HKEY hkey;

    if (RegOpenKeyExW( root, subkey, 0, KEY_SET_VALUE, &hkey )) return 0;
    if (RegSetValueExW( hkey, name, 0, REG_DWORD, (const BYTE *)&mode, len )) ret = 0;
    RegCloseKey( hkey );
    return ret;
}

static BOOL get_accent_palette( DWORD *colors, DWORD *len )
{
    BOOL ret = TRUE;
    DWORD type;
    HKEY hkey;

    if (RegOpenKeyExW( HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Accent", 0, KEY_QUERY_VALUE, &hkey )) return FALSE;
    if (RegQueryValueExW( hkey, L"AccentPalette", NULL, &type, (BYTE *)colors, len ) || type != REG_BINARY) ret = FALSE;
    RegCloseKey( hkey );
    return ret;
}

static BOOL delete_accent_palette(void)
{
    BOOL ret = TRUE;
    HRESULT res;
    HKEY hkey;

    res = RegOpenKeyExW( HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Accent", 0, KEY_SET_VALUE, &hkey );
    if (res != 0) return res == ERROR_FILE_NOT_FOUND;

    res = RegDeleteValueW( hkey, L"AccentPalette" );
    if (res != 0 && res != ERROR_FILE_NOT_FOUND) ret = FALSE;

    RegCloseKey( hkey );
    return ret;
}

static DWORD set_accent_palette( DWORD *colors, DWORD len )
{
    DWORD ret = 1;
    HKEY hkey;

    if (RegOpenKeyExW( HKEY_CURRENT_USER,  L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Accent", 0, KEY_SET_VALUE, &hkey )) return 0;
    if (RegSetValueExW( hkey, L"AccentPalette", 0, REG_BINARY, (const BYTE *)&colors, len )) ret = 0;
    RegCloseKey( hkey );
    return ret;
}

static void reset_color( Color *value )
{
    value->A = 1;
    value->R = 1;
    value->G = 1;
    value->B = 1;
}

static BOOL compare_dword_color( Color color, DWORD dword )
{
    return (color.R == (dword & 0xFF) && color.G == ((dword >> 8) & 0xFF) && color.B == ((dword >> 16) & 0xFF));
}

static void test_single_accent( IUISettings3 *uisettings3, UIColorType type, DWORD expected )
{
    Color value;
    HRESULT hr;
    BOOL result;

    reset_color( &value );
    hr = IUISettings3_GetColorValue( uisettings3, type, &value );
    ok( hr == S_OK, "GetColorValue returned %#lx\n", hr );

    result = compare_dword_color( value, expected );
    ok( result, "got unexpected value.A == %d value.R == %d value.G == %d value.B == %d (type = %d)\n", value.A, value.R, value.G, value.B, type );
}

static void test_AccentColor( IUISettings3 *uisettings3 )
{
    DWORD default_palette[8];
    DWORD default_palette_len = sizeof(default_palette);
    DWORD accent_palette[8];
    DWORD accent_palette_len = sizeof(accent_palette);
    Color value;
    HRESULT hr;

    if (!get_accent_palette( default_palette, &default_palette_len )) default_palette_len = 0;

    /* deleting AccentPalette fills a default value */
    ok( delete_accent_palette(), "failed to delete AccentPalette key.\n");
    ok( !get_accent_palette( accent_palette, &accent_palette_len ), "AccentPalette should not be available.\n" );

    hr = IUISettings3_GetColorValue( uisettings3, UIColorType_Accent, &value );
    ok( hr == S_OK, "GetColorValue returned %#lx\n", hr );

    ok( get_accent_palette( accent_palette, &accent_palette_len ), "failed to retrieve AccentPalette key.\n" );

    test_single_accent( uisettings3, UIColorType_Accent, accent_palette[3] );
    test_single_accent( uisettings3, UIColorType_AccentDark1, accent_palette[4] );
    test_single_accent( uisettings3, UIColorType_AccentDark2, accent_palette[5] );
    test_single_accent( uisettings3, UIColorType_AccentDark3, accent_palette[6] );
    test_single_accent( uisettings3, UIColorType_AccentLight1, accent_palette[2] );
    test_single_accent( uisettings3, UIColorType_AccentLight2, accent_palette[1] );
    test_single_accent( uisettings3, UIColorType_AccentLight3, accent_palette[0] );

    if (default_palette_len) set_accent_palette( default_palette, default_palette_len );
}

static BOOL get_dwm_accent_color( DWORD *color )
{
    DWORD type, len = sizeof(*color);
    HKEY hkey;
    BOOL ret = TRUE;

    if (RegOpenKeyExW( HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\DWM", 0, KEY_QUERY_VALUE, &hkey )) return FALSE;
    if (RegQueryValueExW( hkey, L"AccentColor", NULL, &type, (BYTE *)color, &len ) || type != REG_DWORD) ret = FALSE;
    RegCloseKey( hkey );
    return ret;
}

static BOOL set_dwm_accent_color( DWORD color )
{
    HKEY hkey;

    if (RegCreateKeyExW( HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\DWM", 0, NULL, 0, KEY_SET_VALUE, NULL, &hkey, NULL )) return FALSE;
    if (RegSetValueExW( hkey, L"AccentColor", 0, REG_DWORD, (const BYTE *)&color, sizeof(color) ))
    {
        RegCloseKey( hkey );
        return FALSE;
    }
    RegCloseKey( hkey );
    return TRUE;
}

static void delete_dwm_accent_color( void )
{
    HKEY hkey;

    if (RegOpenKeyExW( HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\DWM", 0, KEY_SET_VALUE, &hkey )) return;
    RegDeleteValueW( hkey, L"AccentColor" );
    RegCloseKey( hkey );
}

/* HSL lightness of the color: the accent ramp scales lightness, so order the
 * ramp by it. Perceived luminance weights would be hue-biased. */
static float color_lightness( const Color *c )
{
    float r = c->R / 255.0f, g = c->G / 255.0f, b = c->B / 255.0f;
    float max = r > g ? (r > b ? r : b) : (g > b ? g : b);
    float min = r < g ? (r < b ? r : b) : (g < b ? g : b);

    return (max + min) / 2.0f;
}

/* With no AccentPalette the ramp must be derived from the DWM AccentColor:
 * Accent is exact, Light1..3 get progressively lighter, Dark1..3 progressively darker,
 * and the derived palette is persisted to the registry. */
static void test_AccentColorDerived( IUISettings3 *uisettings3 )
{
    static const DWORD test_accent = 0x00f0820a; /* BGR of RGB(10, 130, 240) */
    DWORD default_accent = 0, palette[8] = {0}, palette_len = sizeof(palette);
    Color accent, light1, light2, light3, dark1, dark2, dark3;
    BOOL had_accent = get_dwm_accent_color( &default_accent );

    delete_accent_palette();
    ok( set_dwm_accent_color( test_accent ), "failed to set DWM AccentColor.\n" );

    memset( &accent, 0, sizeof(accent) );
    ok( IUISettings3_GetColorValue( uisettings3, UIColorType_Accent, &accent ) == S_OK, "GetColorValue failed.\n" );
    ok( compare_dword_color( accent, test_accent ),
        "Accent got %d %d %d, expected 10 130 240.\n", accent.R, accent.G, accent.B );

    memset( &light1, 0xff, sizeof(light1) );
    ok( IUISettings3_GetColorValue( uisettings3, UIColorType_AccentLight1, &light1 ) == S_OK, "GetColorValue failed.\n" );
    ok( IUISettings3_GetColorValue( uisettings3, UIColorType_AccentLight2, &light2 ) == S_OK, "GetColorValue failed.\n" );
    ok( IUISettings3_GetColorValue( uisettings3, UIColorType_AccentLight3, &light3 ) == S_OK, "GetColorValue failed.\n" );
    ok( IUISettings3_GetColorValue( uisettings3, UIColorType_AccentDark1, &dark1 ) == S_OK, "GetColorValue failed.\n" );
    ok( IUISettings3_GetColorValue( uisettings3, UIColorType_AccentDark2, &dark2 ) == S_OK, "GetColorValue failed.\n" );
    ok( IUISettings3_GetColorValue( uisettings3, UIColorType_AccentDark3, &dark3 ) == S_OK, "GetColorValue failed.\n" );

    ok( color_lightness( &light3 ) > color_lightness( &light2 ), "Light3 not lighter than Light2.\n" );
    ok( color_lightness( &light2 ) > color_lightness( &light1 ), "Light2 not lighter than Light1.\n" );
    ok( color_lightness( &light1 ) > color_lightness( &accent ), "Light1 not lighter than Accent.\n" );
    ok( color_lightness( &accent ) > color_lightness( &dark1 ), "Accent not lighter than Dark1.\n" );
    ok( color_lightness( &dark1 ) > color_lightness( &dark2 ), "Dark1 not darker than Dark2.\n" );
    ok( color_lightness( &dark2 ) > color_lightness( &dark3 ), "Dark2 not darker than Dark3.\n" );

    ok( light1.A == 0xff && light1.R < 255, "Light1 alpha/channel got %d %d.\n", light1.A, light1.R );

    /* the derived palette must be persisted for consumers reading the registry */
    ok( get_accent_palette( palette, &palette_len ), "AccentPalette was not written.\n" );
    ok( palette_len == sizeof(palette), "AccentPalette len %lu, expected %lu.\n", palette_len, (DWORD)sizeof(palette) );
    ok( compare_dword_color( light3, palette[0] ), "palette[0] does not match Light3.\n" );
    ok( compare_dword_color( light2, palette[1] ), "palette[1] does not match Light2.\n" );
    ok( compare_dword_color( light1, palette[2] ), "palette[2] does not match Light1.\n" );
    ok( compare_dword_color( accent, palette[3] ), "palette[3] does not match Accent.\n" );
    ok( compare_dword_color( dark1, palette[4] ), "palette[4] does not match Dark1.\n" );
    ok( compare_dword_color( dark2, palette[5] ), "palette[5] does not match Dark2.\n" );
    ok( compare_dword_color( dark3, palette[6] ), "palette[6] does not match Dark3.\n" );

    /* restore registry state */
    if (had_accent) set_dwm_accent_color( default_accent );
    else delete_dwm_accent_color();
    delete_accent_palette();
}

static void test_UIElementColor( IUISettings *uisettings )
{
    static const struct
    {
        enum UIElementType type;
        int syscolor_index;
    }
    tests[] =
    {
        { UIElementType_ActiveCaption, COLOR_ACTIVECAPTION },
        { UIElementType_Background, COLOR_BACKGROUND },
        { UIElementType_ButtonFace, COLOR_BTNFACE },
        { UIElementType_ButtonText, COLOR_BTNTEXT },
        { UIElementType_CaptionText, COLOR_CAPTIONTEXT },
        { UIElementType_GrayText, COLOR_GRAYTEXT },
        { UIElementType_Highlight, COLOR_HIGHLIGHT },
        { UIElementType_HighlightText, COLOR_HIGHLIGHTTEXT },
        { UIElementType_Hotlight, COLOR_HOTLIGHT },
        { UIElementType_InactiveCaption, COLOR_INACTIVECAPTION },
        { UIElementType_InactiveCaptionText, COLOR_INACTIVECAPTIONTEXT },
        { UIElementType_Window, COLOR_WINDOW },
        { UIElementType_WindowText, COLOR_WINDOWTEXT },
        { UIElementType_AccentColor, -1 },
        { UIElementType_TextHigh, -1 },
        { UIElementType_TextMedium, -1 },
        { UIElementType_TextLow, -1 },
        { UIElementType_TextContrastWithHigh, -1 },
        { UIElementType_NonTextHigh, -1 },
        { UIElementType_NonTextMediumHigh, -1 },
        { UIElementType_NonTextMedium, -1 },
        { UIElementType_NonTextMediumLow, -1 },
        { UIElementType_NonTextLow, -1 },
        { UIElementType_PageBackground, -1 },
        { UIElementType_PopupBackground, -1 },
        { UIElementType_OverlayOutsidePopup, -1 },
        { (UIElementType)12345, -1 } /* Invalid UIElementType */
    };
    Color color;
    HRESULT hr;

    for (int i = 0; i < ARRAY_SIZE( tests ); i++)
    {
        winetest_push_context( "%d", i );

        memset( &color, 0xff, sizeof(color) );
        hr = IUISettings_UIElementColor( uisettings, tests[i].type, &color );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        if (tests[i].syscolor_index == -1)
        {
            ok( color.A == 0 && color.R == 0 && color.G == 0 && color.B == 0,
                "Got unexpected color %d %d %d %d.\n", color.A, color.R, color.G, color.B );
        }
        else
        {
            COLORREF colorref = GetSysColor( tests[i].syscolor_index );
            ok( color.A == 0xff && color.R == GetRValue( colorref )
                && color.G == GetGValue( colorref ) && color.B == GetBValue( colorref ),
                "Got unexpected color %d %d %d %d.\n", color.A, color.R, color.G, color.B );
        }

        winetest_pop_context();
    }

    /* Crash on Windows */
    if (0)
    {
    hr = IUISettings_UIElementColor( uisettings, UIElementType_ActiveCaption, NULL );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    }
}

static void test_AnimationsEnabled( IUISettings *uisettings )
{
    BOOL client_area_animation, ret;
    boolean enabled;
    HRESULT hr;

    /* Crash on Windows */
    if (0)
    {
    hr = IUISettings_get_AnimationsEnabled( uisettings, NULL );
    ok( hr == E_INVALIDARG, "Got unexpected hr %#lx.\n", hr );
    }

    ret = SystemParametersInfoW( SPI_GETCLIENTAREAANIMATION, 0, &client_area_animation, 0 );
    ok( ret, "SystemParametersInfoW failed, error %ld.\n", GetLastError() );
    hr = IUISettings_get_AnimationsEnabled( uisettings, &enabled );
    ok( hr == S_OK, "Got unexpected hr %#lx.\n", hr );
    ok( enabled == client_area_animation, "Expected %d, got %d.\n", client_area_animation, enabled );

    ret = SystemParametersInfoW( SPI_SETCLIENTAREAANIMATION, 0, IntToPtr(!client_area_animation), 0 );
    ok( ret, "SystemParametersInfoW failed, error %ld.\n", GetLastError() );
    hr = IUISettings_get_AnimationsEnabled( uisettings, &enabled );
    ok( hr == S_OK, "Got unexpected hr %#lx.\n", hr );
    ok( enabled == !client_area_animation, "Expected %d, got %d.\n", !client_area_animation, enabled );

    ret = SystemParametersInfoW( SPI_SETCLIENTAREAANIMATION, 0, IntToPtr(client_area_animation), 0 );
    ok( ret, "SystemParametersInfoW failed, error %ld.\n", GetLastError() );
    hr = IUISettings_get_AnimationsEnabled( uisettings, &enabled );
    ok( hr == S_OK, "Got unexpected hr %#lx.\n", hr );
    ok( enabled == client_area_animation, "Expected %d, got %d.\n", client_area_animation, enabled );
}

static void test_UISettings(void)
{
    static const WCHAR *uisettings_name = L"Windows.UI.ViewManagement.UISettings";
    IActivationFactory *factory;
    IUISettings2 *uisettings2;
    IUISettings3 *uisettings3;
    IInspectable *inspectable;
    DOUBLE text_scale_factor;
    IUISettings *uisettings;
    DWORD default_theme;
    UIColorType type;
    Color value;
    HSTRING str;
    HRESULT hr;
    LONG ref;

    hr = WindowsCreateString( uisettings_name, wcslen( uisettings_name ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&factory );
    ok( hr == S_OK || broken( hr == REGDB_E_CLASSNOTREG ), "got hr %#lx.\n", hr );
    if (hr == REGDB_E_CLASSNOTREG)
    {
        win_skip( "%s runtimeclass not registered, skipping tests.\n", wine_dbgstr_w( uisettings_name ) );
        return;
    }

    check_interface( factory, &IID_IUnknown, TRUE );
    check_interface( factory, &IID_IInspectable, TRUE );
    /* WinAppSDK C++/WinRT resolves an agile reference on every activation factory before using it. */
    check_interface( factory, &IID_IAgileObject, TRUE );
    check_interface( factory, &IID_IUISettings3, FALSE );

    hr = RoActivateInstance( str, &inspectable );
    ok( hr == S_OK, "Got unexpected hr %#lx.\n", hr );
    WindowsDeleteString( str );

    hr = IInspectable_QueryInterface( inspectable, &IID_IUISettings3, (void **)&uisettings3 );
    ok( hr == S_OK || broken( hr == E_NOINTERFACE ), "Got unexpected hr %#lx.\n", hr );
    if (FAILED(hr))
    {
        win_skip( "IUISettings3 not supported.\n" );
        goto skip_uisettings3;
    }

    check_interface( inspectable, &IID_IInspectable, TRUE );
    check_interface( inspectable, &IID_IAgileObject, TRUE );
    check_interface( inspectable, &IID_IUISettings, TRUE );
    check_interface( inspectable, &IID_IUISettings2, TRUE );
    check_interface( inspectable, &IID_IUISettings3, TRUE );
    check_interface( inspectable, &IID_IUISettings4, TRUE );
    check_interface( inspectable, &IID_IUISettings5, TRUE );
    check_interface( inspectable, &IID_IUISettings6, TRUE );
    check_interface( inspectable, &IID_IWeakReferenceSource, TRUE );
    check_interface( inspectable, &IID_IWeakReference, FALSE );

    hr = IInspectable_QueryInterface( inspectable, &IID_IUISettings, (void **)&uisettings );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IInspectable_QueryInterface( inspectable, &IID_IUISettings2, (void **)&uisettings2 );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    text_scale_factor = 12345.6;
    hr = IUISettings2_get_TextScaleFactor( uisettings2, &text_scale_factor );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ok( text_scale_factor >= 1.0 && text_scale_factor <= 2.25, "got text_scale_factor %f.\n", text_scale_factor );

    IUISettings2_Release( uisettings2 );

    test_AnimationsEnabled( uisettings );
    test_AccentColor( uisettings3 );
    test_AccentColorDerived( uisettings3 );

    default_theme = get_app_theme();

    /* Light Theme */
    if (!set_app_theme( 1 )) goto done;

    test_UIElementColor( uisettings );

    reset_color( &value );
    type = UIColorType_Foreground;
    hr = IUISettings3_GetColorValue( uisettings3, type, &value );
    ok( hr == S_OK, "GetColorValue returned %#lx\n", hr );
    ok( value.A == 255 && value.R == 0 && value.G == 0 && value.B == 0,
        "got unexpected value.A == %d value.R == %d value.G == %d value.B == %d\n", value.A, value.R, value.G, value.B );

    reset_color( &value );
    type = UIColorType_Background;
    hr = IUISettings3_GetColorValue( uisettings3, type, &value );
    ok( hr == S_OK, "GetColorValue returned %#lx\n", hr );
    ok( value.A == 255 && value.R == 255 && value.G == 255 && value.B == 255,
        "got unexpected value.A == %d value.R == %d value.G == %d value.B == %d\n", value.A, value.R, value.G, value.B );

    /* Dark Theme */
    if (!set_app_theme( 0 )) goto done;

    test_UIElementColor( uisettings );

    reset_color( &value );
    type = UIColorType_Foreground;
    hr = IUISettings3_GetColorValue( uisettings3, type, &value );
    ok( hr == S_OK, "GetColorValue returned %#lx\n", hr );
    ok( value.A == 255 && value.R == 255 && value.G == 255 && value.B == 255,
        "got unexpected value.A == %d value.R == %d value.G == %d value.B == %d\n", value.A, value.R, value.G, value.B );

    reset_color( &value );
    type = UIColorType_Background;
    hr = IUISettings3_GetColorValue( uisettings3, type, &value );
    ok( hr == S_OK, "GetColorValue returned %#lx\n", hr );
    ok( value.A == 255 && value.R == 0 && value.G == 0 && value.B == 0,
        "got unexpected value.A == %d value.R == %d value.G == %d value.B == %d\n", value.A, value.R, value.G, value.B );

done:
    set_app_theme( default_theme );
    IUISettings_Release( uisettings );
    IUISettings3_Release( uisettings3 );

skip_uisettings3:
    IInspectable_Release( inspectable );
    ref = IActivationFactory_Release( factory );
    ok( ref == 1, "got ref %ld.\n", ref );
}

static void test_UISettings_weak_ref(void)
{
    static const WCHAR *uisettings_name = L"Windows.UI.ViewManagement.UISettings";
    IWeakReferenceSource *weak_reference_source;
    IInspectable *inspectable, *inspectable2;
    IWeakReference *weak_reference;
    IActivationFactory *factory;
    IUISettings *uisettings;
    IUnknown *unknown;
    HSTRING str;
    HRESULT hr;
    LONG ref;

    hr = WindowsCreateString( uisettings_name, wcslen( uisettings_name ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&factory );
    ok( hr == S_OK || broken( hr == REGDB_E_CLASSNOTREG ), "got hr %#lx.\n", hr );
    if (hr == REGDB_E_CLASSNOTREG)
    {
        win_skip( "%s runtimeclass not registered, skipping tests.\n", wine_dbgstr_w( uisettings_name ) );
        return;
    }

    hr = RoActivateInstance( str, &inspectable );
    ok( hr == S_OK, "Got unexpected hr %#lx.\n", hr );
    WindowsDeleteString( str );

    hr = IInspectable_QueryInterface( inspectable, &IID_IWeakReferenceSource, (void **)&weak_reference_source );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IWeakReferenceSource_GetWeakReference( weak_reference_source, &weak_reference );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    IWeakReferenceSource_Release( weak_reference_source );

    check_interface( weak_reference, &IID_IUnknown, TRUE );
    check_interface( weak_reference, &IID_IWeakReference, TRUE );
    check_interface( weak_reference, &IID_IInspectable, FALSE );
    check_interface( weak_reference, &IID_IAgileObject, FALSE );
    check_interface( weak_reference, &IID_IUISettings, FALSE );

    hr = IWeakReference_Resolve( weak_reference, &IID_IUnknown, (IInspectable **)&unknown );
    ok( hr == S_OK && unknown, "got hr %#lx.\n", hr );
    hr = IWeakReference_Resolve( weak_reference, &IID_IInspectable, &inspectable2 );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ok( (void *)inspectable2 == (void *)unknown, "Interfaces are not the same.\n" );
    IInspectable_Release( inspectable2 );
    hr = IWeakReference_Resolve( weak_reference, &IID_IUISettings, (IInspectable **)&uisettings );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ok( (void *)uisettings == (void *)unknown, "Interfaces are not the same.\n" );
    IUISettings_Release( uisettings );
    IUnknown_Release( unknown );

    /* Free inspectable, weak reference should fail to resolve now */
    IInspectable_Release( inspectable );

    inspectable2 = (void *)0xdeadbeef;
    hr = IWeakReference_Resolve( weak_reference, &IID_IInspectable, &inspectable2 );
    ok( hr == S_OK && !inspectable2, "got hr %#lx.\n", hr );

    IWeakReference_Release( weak_reference );
    ref = IActivationFactory_Release( factory );
    ok( ref == 1, "got ref %ld.\n", ref );
}

static void test_AccessibilitySettings(void)
{
    static const WCHAR *class_name = RuntimeClass_Windows_UI_ViewManagement_AccessibilitySettings;
    HIGHCONTRASTW high_contrast = {0};
    IAccessibilitySettings *settings;
    IActivationFactory *factory;
    IInspectable *inspectable;
    boolean value;
    HSTRING str;
    HRESULT hr;
    BOOL ret;
    LONG ref;

    hr = WindowsCreateString( class_name, wcslen( class_name ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&factory );
    ok( hr == S_OK || broken( hr == REGDB_E_CLASSNOTREG ), "got hr %#lx.\n", hr );
    if (FAILED( hr ))
    {
        win_skip( "%s runtimeclass not registered, skipping tests.\n", wine_dbgstr_w( class_name ) );
        WindowsDeleteString( str );
        return;
    }

    check_interface( factory, &IID_IUnknown, TRUE );
    check_interface( factory, &IID_IInspectable, TRUE );
    check_interface( factory, &IID_IActivationFactory, TRUE );
    check_interface( factory, &IID_IAgileObject, TRUE );
    check_interface( factory, &IID_IAccessibilitySettings, FALSE );

    hr = RoActivateInstance( str, &inspectable );
    ok( hr == S_OK, "Got unexpected hr %#lx.\n", hr );
    WindowsDeleteString( str );

    hr = IInspectable_QueryInterface( inspectable, &IID_IAccessibilitySettings, (void **)&settings );
    ok( hr == S_OK, "Got unexpected hr %#lx.\n", hr );

    check_interface( inspectable, &IID_IUnknown, TRUE );
    check_interface( inspectable, &IID_IInspectable, TRUE );
    check_interface( inspectable, &IID_IAgileObject, TRUE );
    check_interface( inspectable, &IID_IAccessibilitySettings, TRUE );

    hr = IAccessibilitySettings_get_HighContrast( settings, &value );
    ok( hr == S_OK, "Got unexpected hr %#lx.\n", hr );

    high_contrast.cbSize = sizeof(high_contrast);
    ret = SystemParametersInfoW( SPI_GETHIGHCONTRAST, sizeof(high_contrast), &high_contrast, 0 );
    ok( ret, "SystemParametersInfoW failed, error %lu.\n", GetLastError() );
    ok( value == !!(high_contrast.dwFlags & HCF_HIGHCONTRASTON), "Got unexpected high contrast value.\n" );

    IAccessibilitySettings_Release( settings );
    IInspectable_Release( inspectable );
    ref = IActivationFactory_Release( factory );
    ok( ref == 1, "got ref %ld.\n", ref );
}

START_TEST(uisettings)
{
    HRESULT hr;

    hr = RoInitialize( RO_INIT_MULTITHREADED );
    ok( hr == S_OK, "RoInitialize failed, hr %#lx\n", hr );

    test_UISettings();
    test_UISettings_weak_ref();
    test_AccessibilitySettings();

    RoUninitialize();
}
