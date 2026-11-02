/*
 * Copyright (C) 2026 Zhymabek Roman
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

#include <ntstatus.h>
#define WIN32_NO_STATUS
#include <windef.h>
#include <winbase.h>
#include <winerror.h>
#include <winreg.h>
#include <winnls.h>
#include <winuser.h>
#include <winternl.h>

#include "wine/test.h"

static NTSTATUS (WINAPI *pOpenGlobalizationUserSettingsKey)( ACCESS_MASK access, ULONG reserved, HKEY *ret_key );

static unsigned int enum_count;
static WCHAR enum_first[LOCALE_NAME_MAX_LENGTH];

static BOOL CALLBACK enum_proc_allW( LPWSTR value, LONG_PTR param )
{
    if (winetest_debug > 1) trace( "%s %08Ix\n", debugstr_w( value ), param );
    if (!enum_count) lstrcpyW( enum_first, value );
    enum_count++;
    return TRUE;
}

static BOOL CALLBACK enum_proc_onceW( LPWSTR value, LONG_PTR param )
{
    if (winetest_debug > 1) trace( "%s %08Ix\n", debugstr_w( value ), param );
    ok( !enum_count, "callback called again unexpectedly\n" );
    enum_count++;
    return FALSE;
}

static void test_EnumUILanguages(void)
{
    static const struct
    {
        DWORD flags;
        const char *name;
    } tests[] =
    {
        { MUI_LANGUAGE_NAME, "MUI_LANGUAGE_NAME" },
        { MUI_LANGUAGE_ID, "MUI_LANGUAGE_ID" },
        { MUI_THREAD_LANGUAGES, "MUI_THREAD_LANGUAGES" },
        { MUI_LANGUAGE_NAME | MUI_THREAD_LANGUAGES, "MUI_LANGUAGE_NAME|MUI_THREAD_LANGUAGES" },
        { MUI_LANGUAGE_ID | MUI_THREAD_LANGUAGES, "MUI_LANGUAGE_ID|MUI_THREAD_LANGUAGES" },
    };
    unsigned int i;
    BOOL ret;

    for (i = 0; i < ARRAY_SIZE( tests ); i++)
    {
        enum_count = 0;
        enum_first[0] = 0;
        SetLastError( 0xdeadbeef );
        ret = EnumUILanguagesW( enum_proc_allW, tests[i].flags, 0 );
        ok( ret, "%s: unexpected failure, error %lu\n", tests[i].name, GetLastError() );
        if (ret)
        {
            ok( enum_count >= 1, "%s: got %u languages\n", tests[i].name, enum_count );
            ok( enum_first[0], "%s: got an empty first name\n", tests[i].name );
        }
    }

    /* a callback returning FALSE stops the enumeration, the function still succeeds */
    enum_count = 0;
    SetLastError( 0xdeadbeef );
    ret = EnumUILanguagesW( enum_proc_onceW, MUI_LANGUAGE_NAME | MUI_THREAD_LANGUAGES, 0 );
    ok( ret, "MUI_LANGUAGE_NAME|MUI_THREAD_LANGUAGES: unexpected failure, error %lu\n", GetLastError() );
    ok( enum_count == 1, "got %u callbacks\n", enum_count );

    enum_count = 0;
    SetLastError( 0xdeadbeef );
    ret = EnumUILanguagesW( enum_proc_allW, 0x5a5a5a5a, 0 );
    ok( !ret, "unexpected success\n" );
    ok( GetLastError() == ERROR_INVALID_FLAGS, "got error %ld\n", GetLastError() );

    SetLastError( 0xdeadbeef );
    ret = EnumUILanguagesW( NULL, MUI_LANGUAGE_NAME | MUI_THREAD_LANGUAGES, 0 );
    ok( !ret, "unexpected success\n" );
    ok( GetLastError() == ERROR_INVALID_PARAMETER, "got error %ld\n", GetLastError() );
}

static void check_dword_value( HKEY hkey, const WCHAR *name )
{
    DWORD type, size, value = 0;
    LONG ret;

    type = 0, size = sizeof( value );
    SetLastError( 0xdeadbeef );
    ret = RegQueryValueExW( hkey, name, NULL, &type, (BYTE *)&value, &size );
    ok( !ret, "failed to read the %s value, error %ld\n", debugstr_w( name ), ret );
    if (!ret) ok( type == REG_DWORD && value == 1, "got type %ld value %ld for %s\n",
                  type, value, debugstr_w( name ) );
}

static void test_OpenGlobalizationUserSettingsKey(void)
{
    WCHAR buffer[64];
    HKEY hkey = 0, sub_key = 0;
    DWORD type, size;
    LONG ret;

    if (!pOpenGlobalizationUserSettingsKey)
    {
        skip( "OpenGlobalizationUserSettingsKey is not available\n" );
        return;
    }

    SetLastError( 0xdeadbeef );
    ret = pOpenGlobalizationUserSettingsKey( KEY_READ, 0, &hkey );
    ok( ret == ERROR_SUCCESS, "unexpected ret %08lx\n", ret );
    ok( hkey != 0, "got an empty key handle\n" );
    if (ret) return;

    /* the handle must be the globalization user settings key of the current user */
    type = 0, size = sizeof( buffer );
    SetLastError( 0xdeadbeef );
    ret = RegQueryValueExW( hkey, L"Locale", NULL, &type, (BYTE *)buffer, &size );
    ok( !ret, "the returned handle is not the Control Panel\\International key, error %ld\n", ret );

    SetLastError( 0xdeadbeef );
    ret = RegOpenKeyExW( hkey, L"User Profile", 0, KEY_READ, &sub_key );
    if (ret == ERROR_FILE_NOT_FOUND)
    {
        win_skip( "no Control Panel\\International\\User Profile key\n" );
        RegCloseKey( hkey );
        return;
    }
    ok( !ret, "failed to open the User Profile key, error %ld\n", ret );
    if (ret)
    {
        RegCloseKey( hkey );
        return;
    }

    type = 0, size = sizeof( buffer );
    SetLastError( 0xdeadbeef );
    ret = RegQueryValueExW( sub_key, L"Languages", NULL, &type, (BYTE *)buffer, &size );
    ok( !ret, "failed to read the Languages value, error %ld\n", ret );
    if (!ret)
    {
        ok( type == REG_MULTI_SZ, "got type %ld\n", type );
        ok( size >= 2 * sizeof(WCHAR) && buffer[0], "got an empty Languages value\n" );
    }

    check_dword_value( sub_key, L"ShowAutoCorrection" );
    check_dword_value( sub_key, L"ShowTextPrediction" );
    check_dword_value( sub_key, L"ShowCasing" );
    check_dword_value( sub_key, L"ShowShiftLock" );

    RegCloseKey( sub_key );
    RegCloseKey( hkey );
}

START_TEST(locale)
{
    HMODULE module;

    module = LoadLibraryA( "kernelbase.dll" );
    pOpenGlobalizationUserSettingsKey = (void *)GetProcAddress( module, "OpenGlobalizationUserSettingsKey" );

    test_EnumUILanguages();
    test_OpenGlobalizationUserSettingsKey();
}
