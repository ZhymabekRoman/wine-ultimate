/*
 * Package identity tests for the env-var layer (WINE_PACKAGE_FULL_NAME / WINE_PACKAGE_PATH)
 * and its v2/v3 extensions: GetCurrentPackageInfo2/3, GetCurrentPackagePath2,
 * the package context APIs, FindPackagesByPackageFamily and CloseState.
 *
 * Copyright 2026 the Wine project authors
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

#include "windef.h"
#include "winbase.h"
#include "winerror.h"

#include "appmodel.h"

#include "wine/test.h"

#define FULL_NAME   L"WineWpTest_1.2.3.4_x64__8wekyb3d8bbwe"
#define FAMILY_NAME L"WineWpTest_8wekyb3d8bbwe"
#define PKG_PATH    L"C:\\windows\\temp\\wp011pkg"

typedef LONG (WINAPI *context_fn)( UINT32 flags, UINT32 *length, WCHAR *buffer );
typedef LONG (WINAPI *info2_fn)( UINT32 flags, UINT64 buffer_size, BYTE *buffer, UINT32 *count );
typedef LONG (WINAPI *find_fn)( const WCHAR *family, UINT32 count, WCHAR *full_names, UINT32 *full_names_len,
                                PACKAGE_INFO *package_info, UINT32 *package_info_count, void *filter );
typedef LONG (WINAPI *close_state_fn)( void *state );
typedef void *(WINAPI *fls_get2_fn)( DWORD index );
typedef LONG (WINAPI *open_info_fn)( const WCHAR *full_name, UINT32 reserved, PACKAGE_INFO **info );
typedef LONG (WINAPI *close_info_fn)( PACKAGE_INFO *info );
/* GetPackageApplicationIds takes the package enumeration type (a GUID) by value. */
typedef LONG (WINAPI *app_ids_fn)( const WCHAR *family, GUID type, UINT32 *count, HANDLE *packages );

static void *kb_export( const char *name )
{
    HMODULE mod = GetModuleHandleA( "kernelbase.dll" );
    return mod ? (void *)GetProcAddress( mod, name ) : NULL;
}

static void set_test_env( void )
{
    SetEnvironmentVariableW( L"WINE_PACKAGE_FULL_NAME", FULL_NAME );
    SetEnvironmentVariableW( L"WINE_PACKAGE_PATH", PKG_PATH );
}

static void clear_test_env( void )
{
    SetEnvironmentVariableW( L"WINE_PACKAGE_FULL_NAME", NULL );
    SetEnvironmentVariableW( L"WINE_PACKAGE_PATH", NULL );
}

/* baseline: the v1 env layer the v2/v3 APIs sit on */
static void test_v1_baseline( void )
{
    WCHAR buf[256];
    UINT32 len = 0;
    LONG ret;

    ret = GetCurrentPackageFullName( &len, NULL );
    ok( ret == ERROR_INSUFFICIENT_BUFFER && len == lstrlenW( FULL_NAME ) + 1,
        "v1 query ret %ld len %u\n", ret, len );

    len = ARRAY_SIZE( buf );
    ret = GetCurrentPackageFullName( &len, buf );
    ok( ret == ERROR_SUCCESS && !wcscmp( buf, FULL_NAME ), "v1 fill ret %ld name %s\n", ret, debugstr_w( buf ) );
}

static void test_package_path2( void )
{
    context_fn p = (void *)kb_export( "GetCurrentPackagePath2" );
    WCHAR buf[256];
    UINT32 len;
    LONG ret;

    ok( p != NULL, "GetCurrentPackagePath2 not exported\n" );
    if (!p) return;

    len = 0;
    ret = p( 0, &len, NULL );
    ok( ret == ERROR_INSUFFICIENT_BUFFER && len == lstrlenW( PKG_PATH ) + 1,
        "Path2 query ret %ld len %u\n", ret, len );

    len = ARRAY_SIZE( buf );
    ret = p( 0, &len, buf );
    ok( ret == ERROR_SUCCESS && !wcscmp( buf, PKG_PATH ), "Path2 fill ret %ld path %s\n", ret, debugstr_w( buf ) );
}

static void check_package_info( const char *name, info2_fn f )
{
    UINT32 count = 0, v1_size = 0;
    PACKAGE_INFO *info;
    BYTE *buffer;
    LONG ret;

    if (!f)
    {
        ok( 0, "%s not exported\n", name );
        return;
    }

    /* negotiate the size through v1, then widen through v2/v3 */
    ret = GetCurrentPackageInfo( 0, &v1_size, NULL, &count );
    ok( ret == ERROR_INSUFFICIENT_BUFFER && v1_size, "%s v1 negotiation ret %ld size %u\n", name, ret, v1_size );

    count = 0;
    ret = f( 0, 0, NULL, &count );
    ok( ret == ERROR_INSUFFICIENT_BUFFER, "%s query ret %ld\n", name, ret );
    ok( count == 1, "%s query count %u\n", name, count );

    buffer = HeapAlloc( GetProcessHeap(), 0, 8192 );
    count = 0;
    ret = f( 0, 8192, buffer, &count );
    ok( ret == ERROR_SUCCESS, "%s fill ret %ld\n", name, ret );
    ok( count == 1, "%s fill count %u\n", name, count );

    info = (PACKAGE_INFO *)buffer;
    if (count == 1)
    {
        ok( info->packageFullName && !wcscmp( info->packageFullName, FULL_NAME ),
            "%s full name %s\n", name, debugstr_w( info->packageFullName ) );
        ok( info->path && !wcscmp( info->path, PKG_PATH ), "%s path %s\n", name, debugstr_w( info->path ) );
        ok( info->packageFamilyName && !wcscmp( info->packageFamilyName, FAMILY_NAME ),
            "%s family %s\n", name, debugstr_w( info->packageFamilyName ) );
        ok( info->packageId.version.Major == 1 && info->packageId.version.Minor == 2
            && info->packageId.version.Build == 3 && info->packageId.version.Revision == 4,
            "%s version %u.%u.%u.%u\n", name, info->packageId.version.Major, info->packageId.version.Minor,
            info->packageId.version.Build, info->packageId.version.Revision );
    }
    HeapFree( GetProcessHeap(), 0, buffer );
}

static void test_package_info_v2_v3( void )
{
    check_package_info( "GetCurrentPackageInfo2", (void *)kb_export( "GetCurrentPackageInfo2" ) );
    check_package_info( "GetCurrentPackageInfo3", (void *)kb_export( "GetCurrentPackageInfo3" ) );
}

static void check_context( const char *name )
{
    context_fn p = (void *)kb_export( name );
    WCHAR buf[256];
    UINT32 len;
    LONG ret;

    ok( p != NULL, "%s not exported\n", name );
    if (!p) return;

    len = 0;
    ret = p( 0, &len, NULL );
    ok( ret == ERROR_INSUFFICIENT_BUFFER && len == lstrlenW( FULL_NAME ) + 1,
        "%s query ret %ld len %u\n", name, ret, len );

    len = ARRAY_SIZE( buf );
    ret = p( 0, &len, buf );
    ok( ret == ERROR_SUCCESS && !wcscmp( buf, FULL_NAME ), "%s fill ret %ld context %s\n", name, ret, debugstr_w( buf ) );
}

static void test_package_contexts( void )
{
    check_context( "GetCurrentPackageContext" );
    check_context( "GetCurrentPackageApplicationContext" );
    check_context( "GetCurrentPackageApplicationResourcesContext" );
    check_context( "GetCurrentPackageGlobalizationContext" );
}

static void test_find_packages_by_family( void )
{
    find_fn p = (void *)kb_export( "FindPackagesByPackageFamily" );
    WCHAR names[256];
    PACKAGE_INFO info[2];
    UINT32 names_len, info_count;
    LONG ret;

    ok( p != NULL, "FindPackagesByPackageFamily not exported\n" );
    if (!p) return;

    /* query mode */
    names_len = 0;
    info_count = 0;
    ret = p( FAMILY_NAME, 0, NULL, &names_len, NULL, &info_count, NULL );
    ok( ret == ERROR_SUCCESS && info_count == 1 && names_len == lstrlenW( FULL_NAME ) + 1,
        "query ret %ld count %u names_len %u\n", ret, info_count, names_len );

    /* fill mode */
    names[0] = 0;
    names_len = ARRAY_SIZE( names );
    info_count = ARRAY_SIZE( info );
    memset( info, 0, sizeof(info) );
    ret = p( FAMILY_NAME, 1, names, &names_len, info, &info_count, NULL );
    ok( ret == ERROR_SUCCESS, "fill ret %ld\n", ret );
    ok( info_count == 1, "fill count %u\n", info_count );
    ok( !wcscmp( names, FULL_NAME ), "fill names %s\n", debugstr_w( names ) );
    ok( info[0].packageFullName && !wcscmp( info[0].packageFullName, FULL_NAME ),
        "fill full name %s\n", debugstr_w( info[0].packageFullName ) );
    ok( info[0].path && !wcscmp( info[0].path, PKG_PATH ), "fill path %s\n", debugstr_w( info[0].path ) );

    /* a family that is not installed resolves to nothing, not an error */
    names_len = ARRAY_SIZE( names );
    info_count = ARRAY_SIZE( info );
    ret = p( L"NotExist_8wekyb3d8bbwe", 1, names, &names_len, info, &info_count, NULL );
    ok( ret == ERROR_SUCCESS && info_count == 0, "absent ret %ld count %u\n", ret, info_count );
}

static void test_close_state( void )
{
    close_state_fn p = (void *)kb_export( "CloseState" );

    ok( p != NULL, "CloseState not exported\n" );
    if (!p) return;
    ok( p( (void *)1 ) == ERROR_SUCCESS, "CloseState returned an error\n" );
}

/* FlsGetValue2 is the callback-free reader of the same FLS slot FlsGetValue
 * reads; msedge.dll 136 looks it up in kernelbase.dll directly. */
static void test_fls_get_value2( void )
{
    fls_get2_fn get2 = (void *)kb_export( "FlsGetValue2" );
    DWORD index;

    ok( get2 != NULL, "FlsGetValue2 not exported\n" );
    if (!get2) return;

    ok( get2( 0 ) == NULL, "reserved index returned %p\n", get2( 0 ) );

    index = FlsAlloc( NULL );
    ok( index != FLS_OUT_OF_INDEXES, "FlsAlloc failed\n" );
    if (index == FLS_OUT_OF_INDEXES) return;

    ok( get2( index ) == NULL, "fresh slot returned %p\n", get2( index ) );
    ok( FlsSetValue( index, (void *)0x1234 ), "FlsSetValue failed\n" );
    ok( get2( index ) == (void *)0x1234, "slot value %p\n", get2( index ) );
    ok( get2( index ) == FlsGetValue( index ), "value2 %p value %p\n", get2( index ), FlsGetValue( index ) );

    ok( FlsSetValue( index, NULL ), "clear FlsSetValue failed\n" );
    ok( get2( index ) == NULL, "cleared slot returned %p\n", get2( index ) );
    ok( FlsFree( index ), "FlsFree failed\n" );
}

/* The identity OpenPackageInfoByFullName hands back has to be the one
 * GetCurrentPackageInfo builds for the running package. */
static void check_info_against_current( const char *name, const PACKAGE_INFO *info )
{
    BYTE buffer[8192];
    PACKAGE_INFO *ref = (PACKAGE_INFO *)buffer;
    UINT32 size = sizeof( buffer ), count = 0;
    LONG ret = GetCurrentPackageInfo( 0, &size, buffer, &count );

    ok( ret == ERROR_SUCCESS && count == 1, "%s reference ret %ld count %u\n", name, ret, count );
    if (ret != ERROR_SUCCESS || count != 1) return;

    ok( info->reserved == ref->reserved, "%s reserved %u\n", name, info->reserved );
    ok( info->flags == ref->flags, "%s flags %#x\n", name, info->flags );
    ok( info->path && !wcscmp( info->path, ref->path ), "%s path %s\n", name, debugstr_w( info->path ) );
    ok( info->packageFullName && !wcscmp( info->packageFullName, ref->packageFullName ),
        "%s full name %s\n", name, debugstr_w( info->packageFullName ) );
    ok( info->packageFamilyName && !wcscmp( info->packageFamilyName, ref->packageFamilyName ),
        "%s family %s\n", name, debugstr_w( info->packageFamilyName ) );
    ok( info->packageId.processorArchitecture == ref->packageId.processorArchitecture,
        "%s arch %#x\n", name, info->packageId.processorArchitecture );
    ok( info->packageId.version.Version == ref->packageId.version.Version,
        "%s version %#llx\n", name, info->packageId.version.Version );
    ok( info->packageId.name && !wcscmp( info->packageId.name, ref->packageId.name ),
        "%s id name %s\n", name, debugstr_w( info->packageId.name ) );
    ok( info->packageId.publisherId && !wcscmp( info->packageId.publisherId, ref->packageId.publisherId ),
        "%s publisher id %s\n", name, debugstr_w( info->packageId.publisherId ) );
    ok( info->packageId.resourceId && !wcscmp( info->packageId.resourceId, ref->packageId.resourceId ),
        "%s resource id %s\n", name, debugstr_w( info->packageId.resourceId ) );
    ok( info->packageFullName && !wcscmp( info->packageFullName, FULL_NAME ),
        "%s full name is not the test package %s\n", name, debugstr_w( info->packageFullName ) );
}

/*
 * Documented return values of OpenPackageInfoByFullName for a name we have no
 * identity for, taken from the APPMODEL codes in include/winerror.h:3387-3389:
 * a name that does not parse as a package full name reports the identity-corrupt
 * code, a well-formed name of another package the no-package code.
 */
#define HR_CORRUPT_NAME   HRESULT_FROM_WIN32( APPMODEL_ERROR_PACKAGE_IDENTITY_CORRUPT )
#define HR_NO_PACKAGE     HRESULT_FROM_WIN32( APPMODEL_ERROR_NO_PACKAGE )

static void test_open_package_info_by_full_name( void )
{
    open_info_fn open = (void *)kb_export( "OpenPackageInfoByFullName" );
    close_info_fn close = (void *)kb_export( "ClosePackageInfo" );
    PACKAGE_INFO *info = (PACKAGE_INFO *)0x1;
    LONG ret;

    ok( open != NULL, "OpenPackageInfoByFullName not exported\n" );
    if (!open) return;

    info = NULL;
    ret = open( FULL_NAME, 0, &info );
    ok( ret == S_OK && info != NULL, "open ret %#lx info %p\n", ret, info );
    if (ret == S_OK && info)
    {
        check_info_against_current( "open", info );
        if (close) ok( close( info ) == S_OK, "close returned an error\n" );
    }

    info = NULL;
    ret = open( L"garbage", 0, &info );
    ok( ret == HR_CORRUPT_NAME && !info, "bogus name ret %#lx info %p\n", ret, info );

    info = NULL;
    ret = open( L"NotExist_1.2.3.4_x64__8wekyb3d8bbwe", 0, &info );
    ok( ret == HR_NO_PACKAGE && !info, "absent package ret %#lx info %p\n", ret, info );

    ok( open( NULL, 0, &info ) == E_INVALIDARG, "null name not rejected\n" );
    ok( open( FULL_NAME, 0, NULL ) == E_INVALIDARG, "null out not rejected\n" );

    if (close) ok( close( NULL ) == E_INVALIDARG, "close of null not rejected\n" );
}

static void test_get_package_application_ids( void )
{
    app_ids_fn f = (void *)kb_export( "GetPackageApplicationIds" );
    HANDLE packages = NULL;
    UINT32 count = 0xdead;
    GUID type;
    LONG ret;

    ok( f != NULL, "GetPackageApplicationIds not exported\n" );
    if (!f) return;

    memset( &type, 0, sizeof(type) );

    /* query: the count of packages in our own family, without an array */
    ret = f( FAMILY_NAME, type, &count, NULL );
    ok( ret == ERROR_SUCCESS && count >= 1, "query ret %#lx count %u\n", ret, count );

    /* fill: one HPKG per package in the family */
    count = 0;
    packages = NULL;
    ret = f( FAMILY_NAME, type, &count, &packages );
    ok( ret == ERROR_SUCCESS, "fill ret %#lx\n", ret );
    ok( count >= 1, "fill count %u\n", count );
    ok( packages != NULL, "fill array %p\n", packages );

    /* a family that is not ours resolves to nothing */
    count = 0xdead;
    ret = f( L"NotExist_8wekyb3d8bbwe", type, &count, NULL );
    ok( ret == ERROR_SUCCESS && count == 0, "absent family ret %#lx count %u\n", ret, count );

    ok( f( NULL, type, &count, NULL ) == E_INVALIDARG, "null family not rejected\n" );
    ok( f( FAMILY_NAME, type, NULL, NULL ) == E_INVALIDARG, "null count not rejected\n" );
}

START_TEST( package )
{
    set_test_env();
    test_v1_baseline();
    test_package_path2();
    test_package_info_v2_v3();
    test_package_contexts();
    test_find_packages_by_family();
    test_close_state();
    test_fls_get_value2();
    test_open_package_info_by_full_name();
    test_get_package_application_ids();
    clear_test_env();
}
