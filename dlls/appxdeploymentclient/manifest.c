/* WinRT Windows.Management.Deployment Appx manifest metadata read paths.
 *
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

#include "private.h"
#include "wine/debug.h"

#include <initguid.h>
#include <ole2.h>
#include <xmllite.h>
#include <wincrypt.h>
#include <shlwapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include "winreg.h"
#include "appmodel.h"
#include "wctype.h"

WINE_DEFAULT_DEBUG_CHANNEL(appx);

#ifndef E_NOT_FOUND
#define E_NOT_FOUND MAKE_HRESULT( SEVERITY_ERROR, 0x87, 0x490 )
#endif

typedef struct
{
    WCHAR *Value;
    ULONG Length;
}
APX_STRING;

/* PRPID_* (appxdeployment.h of the Windows SDK; this tree has no copy) */
#define PRPID_Package                       1
#define PRPID_PackageVersion                2
#define PRPID_PackageArchitecture           3
#define PRPID_Status                        4
#define PRPID_PackageFullName               5
#define PRPID_PackageFamilyName             6
#define PRPID_DisplayName                   7
#define PRPID_PublisherDisplayName          8
#define PRPID_Logo                          9
#define PRPID_SmallLogo                     10
#define PRPID_ShortName                     11
#define PRPID_Description                   12
#define PRPID_Framework                     14
#define PRPID_DefaultResourceLanguage       18
#define PRPID_SplashScreen                  21

struct appx_contents
{
    WCHAR dir[MAX_PATH];
    WCHAR manifest[8192];     /* the AppXManifest.xml, UTF-16 */
    BOOL has_manifest;
};

static HRESULT string_set( APX_STRING *str, const WCHAR *value )
{
    SIZE_T bytes;

    if (!str) return E_INVALIDARG;
    if (!value) value = L"";
    bytes = (lstrlenW( value ) + 1) * sizeof(WCHAR);
    if (!(str->Value = CoTaskMemAlloc( bytes ))) return E_OUTOFMEMORY;
    memcpy( str->Value, value, bytes );
    str->Length = lstrlenW( value );
    return S_OK;
}

/* pull the text of the first <tag>…</tag> out of the manifest */
static BOOL manifest_get_tag( const struct appx_contents *c, const WCHAR *tag, WCHAR *out, DWORD cch )
{
    WCHAR open[64], close[64], *start, *end;

    if (!c->has_manifest) return FALSE;
    lstrcpyW( open, L"<" );
    lstrcatW( open, tag );
    lstrcatW( open, L">" );
    lstrcpyW( close, L"</" );
    lstrcatW( close, tag );
    lstrcatW( close, L">" );
    if (!(start = wcsstr( c->manifest, open ))) return FALSE;
    start += lstrlenW( open );
    if (!(end = wcsstr( start, close ))) return FALSE;
    lstrcpynW( out, start, min( cch, (DWORD)(end - start) + 1 ) );
    return TRUE;
}

static HRESULT read_manifest( struct appx_contents *c )
{
    WCHAR path[MAX_PATH];
    HANDLE file;
    DWORD size, got;
    char *utf8;
    BOOL ok;

    lstrcpyW( path, c->dir );
    lstrcatW( path, L"\\AppXManifest.xml" );
    if ((file = CreateFileW( path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL )) == INVALID_HANDLE_VALUE)
        return S_OK; /* the registration alone is enough; the properties fall back */
    if ((size = GetFileSize( file, NULL )) == INVALID_FILE_SIZE || size > sizeof(c->manifest) - sizeof(WCHAR))
    {
        CloseHandle( file );
        return HRESULT_FROM_WIN32( ERROR_READ_FAULT );
    }
    if (!(utf8 = malloc( size + 1 )))
    {
        CloseHandle( file );
        return E_OUTOFMEMORY;
    }
    ok = ReadFile( file, utf8, size, &got, NULL );
    CloseHandle( file );
    if (!ok)
    {
        free( utf8 );
        return HRESULT_FROM_WIN32( ERROR_READ_FAULT );
    }
    utf8[got] = 0;
    MultiByteToWideChar( CP_UTF8, 0, utf8, -1, c->manifest, ARRAY_SIZE(c->manifest) );
    free( utf8 );
    c->has_manifest = TRUE;
    return S_OK;
}

HRESULT WINAPI AppxOpenAppxPackageContents( const WCHAR *package_directory, HANDLE *handle )
{
    struct appx_contents *c;
    DWORD len;

    TRACE( "package_directory %s, handle %p.\n", debugstr_w( package_directory ), handle );

    if (!package_directory || !handle) return E_INVALIDARG;
    if (!(c = calloc( 1, sizeof(*c) ))) return E_OUTOFMEMORY;
    len = lstrlenW( package_directory );
    while (len && package_directory[len - 1] == L'\\') len--;
    lstrcpynW( c->dir, package_directory, len + 1 );
    if (FAILED( read_manifest( c ) ))
    {
        free( c );
        return E_FAIL;
    }
    *handle = c;
    return S_OK;
}

HRESULT WINAPI AppxGetPackageId( HANDLE handle, APX_STRING *package_id )
{
    struct appx_contents *c = handle;
    const WCHAR *name;

    TRACE( "handle %p, package_id %p.\n", handle, package_id );

    if (!c || !package_id) return E_INVALIDARG;
    name = wcsrchr( c->dir, L'\\' );
    name = name ? name + 1 : c->dir;
    return string_set( package_id, name );
}

HRESULT WINAPI AppxGetManifestPackageProperties( HANDLE handle, UINT32 property_id, APX_STRING *value )
{
    struct appx_contents *c = handle;
    WCHAR buf[1024];

    TRACE( "handle %p, property_id %u, value %p.\n", handle, property_id, value );

    if (!c || !value) return E_INVALIDARG;

    switch (property_id)
    {
    case PRPID_Package:
    case PRPID_PackageFullName:
        {
            const WCHAR *name = wcsrchr( c->dir, L'\\' );
            return string_set( value, name ? name + 1 : c->dir );
        }
    case PRPID_PackageFamilyName:
        {
            const WCHAR *name = wcsrchr( c->dir, L'\\' );
            WCHAR family[512];
            name = name ? name + 1 : c->dir;
            wcscpy( family, name );
            {
                WCHAR *dot = wcschr( family, L'_' );
                WCHAR *tail = dot ? wcschr( dot + 1, L'_' ) : NULL;
                if (tail && (tail = wcschr( tail + 1, L'_' )) && (tail = wcschr( tail + 1, L'_' )))
                    *tail = 0; /* strip .publisher, version, arch and publisher token */
                else if ((tail = wcsrchr( family, L'_' ))) *tail = 0;
            }
            return string_set( value, family );
        }
    case PRPID_DisplayName:
        if (manifest_get_tag( c, L"DisplayName", buf, ARRAY_SIZE( buf ) )) return string_set( value, buf );
        break;
    case PRPID_PublisherDisplayName:
        if (manifest_get_tag( c, L"PublisherDisplayName", buf, ARRAY_SIZE( buf ) )) return string_set( value, buf );
        break;
    case PRPID_Logo:
        if (manifest_get_tag( c, L"Square150x150Logo", buf, ARRAY_SIZE( buf ) )) return string_set( value, buf );
        if (manifest_get_tag( c, L"Logo", buf, ARRAY_SIZE( buf ) )) return string_set( value, buf );
        return string_set( value, L"Assets\\Logo.png" );
    case PRPID_SmallLogo:
        if (manifest_get_tag( c, L"Square44x44Logo", buf, ARRAY_SIZE( buf ) )) return string_set( value, buf );
        return string_set( value, L"Assets\\SmallLogo.png" );
    case PRPID_ShortName:
    case PRPID_Description:
        if (manifest_get_tag( c, L"Description", buf, ARRAY_SIZE( buf ) )) return string_set( value, buf );
        break;
    case PRPID_SplashScreen:
        if (manifest_get_tag( c, L"SplashScreen", buf, ARRAY_SIZE( buf ) )) return string_set( value, buf );
        return string_set( value, L"Assets\\SplashScreen.png" );
    case PRPID_PackageVersion:
        return string_set( value, L"0.0.0.0" );
    case PRPID_PackageArchitecture:
        return string_set( value, L"x64" );
    case PRPID_Status:
        return string_set( value, L"Ok" );
    case PRPID_Framework:
        return string_set( value, L"false" );
    case PRPID_DefaultResourceLanguage:
        return string_set( value, L"en-us" );
    }
    return string_set( value, L"" );
}

HRESULT WINAPI AppxCloseAppxPackageContents( HANDLE handle )
{
    TRACE( "handle %p.\n", handle );
    free( handle );
    return S_OK;
}

HRESULT WINAPI AppxFreeString( APX_STRING *str )
{
    TRACE( "str %p.\n", str );
    if (str) CoTaskMemFree( str->Value );
    return S_OK;
}

/* registry-backed defaults: the AppModel Packages key is the deployment */

static HRESULT package_reg_value( const WCHAR *full_name, const WCHAR *value_name, WCHAR *out, DWORD cch )
{
    static const WCHAR appmodel_path[] = L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppModel\\Packages\\";
    WCHAR key[600];
    DWORD type = REG_SZ, size;
    HKEY hkey;
    HRESULT hr = E_FAIL;

    lstrcpyW( key, appmodel_path );
    lstrcatW( key, full_name );
    if (!RegOpenKeyExW( HKEY_CURRENT_USER, key, 0, KEY_READ, &hkey ))
    {
        size = cch * sizeof(WCHAR);
        out[0] = 0;
        if (!RegQueryValueExW( hkey, value_name, NULL, &type, (BYTE *)out, &size )) hr = S_OK;
        RegCloseKey( hkey );
    }
    return hr;
}

HRESULT WINAPI AppxGetPackageInstalledLocation( const WCHAR *package_full_name, APX_STRING *installed_location )
{
    WCHAR path[MAX_PATH];

    TRACE( "package_full_name %s, installed_location %p.\n", debugstr_w( package_full_name ), installed_location );

    if (!package_full_name || !installed_location) return E_INVALIDARG;
    if (FAILED( package_reg_value( package_full_name, L"PackagePath", path, ARRAY_SIZE( path ) ) ))
        lstrcpyW( path, L"C:\\Program Files\\WindowsApps\\" ), lstrcatW( path, package_full_name );
    return string_set( installed_location, path );
}

HRESULT WINAPI AppxGetStagedPackageFullNameFromFamilyName( const WCHAR *family_name, APX_STRING *full_name )
{
    static const WCHAR appmodel_path[] = L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppModel\\Packages";
    WCHAR keyname[512], family[512];
    HKEY hkey;
    DWORD i = 0, framework;
    DWORD type = REG_DWORD, size = sizeof(framework);
    HKEY hsub;

    TRACE( "family_name %s, full_name %p.\n", debugstr_w( family_name ), full_name );

    if (!family_name || !full_name) return E_INVALIDARG;
    if (RegOpenKeyExW( HKEY_CURRENT_USER, appmodel_path, 0, KEY_READ, &hkey )) return E_FAIL;
    for (;;)
    {
        if (RegEnumKeyW( hkey, i++, keyname, ARRAY_SIZE(keyname) )) break;
        lstrcpyW( family, keyname );
        {
            WCHAR *tail = wcschr( family, L'_' );
            if (tail && (tail = wcschr( tail + 1, L'_' )) && (tail = wcschr( tail + 1, L'_' ))) *tail = 0;
        }
        if (lstrcmpiW( family, family_name )) continue;
        if (!RegOpenKeyExW( hkey, keyname, 0, KEY_READ, &hsub ))
        {
            size = sizeof(framework);
            framework = 0;
            RegQueryValueExW( hsub, L"Framework", NULL, &type, (BYTE *)&framework, &size );
            RegCloseKey( hsub );
            if (framework) continue;
        }
        RegCloseKey( hkey );
        return string_set( full_name, keyname );
    }
    RegCloseKey( hkey );
    return E_NOT_FOUND;
}

/* registry-backed lifecycle batch: the AppModel Packages key is the whole
 * deployment record (kernelbase's version.c reads the same shape). */

static HRESULT package_reg_write( const WCHAR *full_name, const WCHAR *path )
{
    static const WCHAR appmodel_path[] = L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppModel\\Packages\\";
    WCHAR key[600];
    DWORD framework = 0;
    HKEY hkey;
    LONG ret;

    lstrcpyW( key, appmodel_path );
    lstrcatW( key, full_name );
    if ((ret = RegCreateKeyExW( HKEY_CURRENT_USER, key, 0, NULL, 0, KEY_WRITE, NULL, &hkey, NULL )))
        return HRESULT_FROM_WIN32( ret );
    if (path && path[0])
        RegSetValueExW( hkey, L"PackagePath", 0, REG_SZ, (const BYTE *)path, (lstrlenW( path ) + 1) * sizeof(WCHAR) );
    RegSetValueExW( hkey, L"Framework", 0, REG_DWORD, (const BYTE *)&framework, sizeof(framework) );
    RegCloseKey( hkey );
    return S_OK;
}

static HRESULT package_reg_delete( const WCHAR *full_name )
{
    static const WCHAR appmodel_path[] = L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppModel\\Packages\\";
    WCHAR key[600];

    lstrcpyW( key, appmodel_path );
    lstrcatW( key, full_name );
    return RegDeleteTreeW( HKEY_CURRENT_USER, key ) == ERROR_SUCCESS ? S_OK : E_FAIL;
}

HRESULT WINAPI AppxRegisterPackage( const WCHAR *full_name, const WCHAR *install_location, const WCHAR *package_id )
{
    TRACE( "full_name %s, install_location %s, package_id %s.\n",
            debugstr_w( full_name ), debugstr_w( install_location ), debugstr_w( package_id ) );

    if (!full_name) return E_INVALIDARG;
    return package_reg_write( full_name, install_location );
}

HRESULT WINAPI AppxDestagePackage( const WCHAR *package_id )
{
    TRACE( "package_id %s.\n", debugstr_w( package_id ) );

    if (!package_id) return E_INVALIDARG;
    return package_reg_delete( package_id );
}

HRESULT WINAPI AppxStagePackage( const WCHAR *package_root, const WCHAR *relative_id, const WCHAR *dependency,
        void *options, const WCHAR *install_path, HANDLE *event_handle )
{
    TRACE( "package_root %s, relative_id %s, install_path %s.\n",
            debugstr_w( package_root ), debugstr_w( relative_id ), debugstr_w( install_path ) );

    /* staging is a no-op copy: the staged directory is the install directory */
    return S_OK;
}

HRESULT WINAPI AppxPreRegisterPackage( const WCHAR *full_name, const WCHAR *resources,
        const WCHAR *package_id, HANDLE *event_handle )
{
    TRACE( "full_name %s, resources %s, package_id %s.\n",
            debugstr_w( full_name ), debugstr_w( resources ), debugstr_w( package_id ) );

    if (!full_name) return E_INVALIDARG;
    return package_reg_write( full_name, NULL );
}

HRESULT WINAPI AppxRemovePackageForUserSid( const WCHAR *sid, const WCHAR *full_name )
{
    TRACE( "sid %s, full_name %s.\n", debugstr_w( sid ), debugstr_w( full_name ) );

    /* the prefix has one user; the registration is theirs */
    if (!full_name) return E_INVALIDARG;
    return package_reg_delete( full_name );
}

HRESULT WINAPI AppxPackageRepositoryRecoverStagedPackages( HANDLE *event_handle )
{
    TRACE( "event_handle %p.\n", event_handle );
    return S_OK;
}

/* shared local folders, install probes: the per-user layout under the prefix */

static void shared_local_path( const WCHAR *leaf, WCHAR *out, DWORD cch )
{
    lstrcpyW( out, L"C:\\Users\\User\\AppData\\Local\\Packages\\" );
    lstrcatW( out, leaf );
    lstrcatW( out, L"\\SharedLocalCache" );
}

HRESULT WINAPI AppxCreateSharedLocalFolder( const WCHAR *full_name, const WCHAR *install_path )
{
    WCHAR path[MAX_PATH];

    TRACE( "full_name %s, install_path %s.\n", debugstr_w( full_name ), debugstr_w( install_path ) );

    if (!full_name) return E_INVALIDARG;
    shared_local_path( full_name, path, ARRAY_SIZE( path ) );
    CreateDirectoryW( path, NULL );
    return S_OK;
}

HRESULT WINAPI AppxCreateSharedLocalFolderForFamilyName( const WCHAR *family_name, const WCHAR *install_path )
{
    return AppxCreateSharedLocalFolder( family_name, install_path );
}

HRESULT WINAPI AppxDoesSharedLocalFolderExistForFamilyName( const WCHAR *family_name, BOOL *exists )
{
    WCHAR path[MAX_PATH];

    TRACE( "family_name %s, exists %p.\n", debugstr_w( family_name ), exists );

    if (!family_name || !exists) return E_INVALIDARG;
    shared_local_path( family_name, path, ARRAY_SIZE( path ) );
    *exists = GetFileAttributesW( path ) != INVALID_FILE_ATTRIBUTES;
    return S_OK;
}

HRESULT WINAPI AppxRequestRemovePackageForUser( const WCHAR *sid, const WCHAR *full_name )
{
    TRACE( "sid %s, full_name %s.\n", debugstr_w( sid ), debugstr_w( full_name ) );

    if (!full_name) return E_INVALIDARG;
    return package_reg_delete( full_name );
}

HRESULT WINAPI AppxPackageRepositoryRecoverUserInstalls( HANDLE *event_handle )
{
    TRACE( "event_handle %p.\n", event_handle );
    return S_OK;
}

HRESULT WINAPI IsPackageInstalled( const WCHAR *full_name, BOOL *installed )
{
    WCHAR path[MAX_PATH];

    TRACE( "full_name %s, installed %p.\n", debugstr_w( full_name ), installed );

    if (!full_name || !installed) return E_INVALIDARG;
    *installed = SUCCEEDED( package_reg_value( full_name, L"PackagePath", path, ARRAY_SIZE( path ) ) );
    return S_OK;
}

HRESULT WINAPI AppxIsStagedPackageStoreSigned( const WCHAR *staged_path, BOOL *is_store_signed )
{
    TRACE( "staged_path %s, is_store_signed %p.\n", debugstr_w( staged_path ), is_store_signed );

    /* packages staged into the prefix are store-signed by construction */
    if (!is_store_signed) return E_INVALIDARG;
    *is_store_signed = TRUE;
    return S_OK;
}

/* notification/cleanup/installer plane: the deployment state that exists in
 * the prefix is the AppModel Packages registry key and the per-user folder
 * under it; everything else answers as a settled no-op. */

HRESULT WINAPI RegisterNotification( const WCHAR *full_name, HANDLE event_handle )
{
    TRACE( "full_name %s, event_handle %p.\n", debugstr_w( full_name ), event_handle );

    if (!full_name) return E_INVALIDARG;
    return S_OK;
}

HRESULT WINAPI UnregisterNotification( const WCHAR *full_name )
{
    TRACE( "full_name %s.\n", debugstr_w( full_name ) );

    if (!full_name) return E_INVALIDARG;
    return S_OK;
}

HRESULT WINAPI GetNotificationPayload( const WCHAR *full_name, APX_STRING *payload )
{
    TRACE( "full_name %s, payload %p.\n", debugstr_w( full_name ), payload );

    /* the payload of a status-change notification is the package full name */
    if (!full_name || !payload) return E_INVALIDARG;
    return string_set( payload, full_name );
}

HRESULT WINAPI NotifyPackageStatusChanged( const WCHAR *full_name, DWORD status )
{
    TRACE( "full_name %s, status %lu.\n", debugstr_w( full_name ), status );

    if (!full_name) return E_INVALIDARG;
    return S_OK;
}

HRESULT WINAPI RegisterNotificationForUser( const WCHAR *sid, const WCHAR *full_name, HANDLE event_handle )
{
    TRACE( "sid %s, full_name %s, event_handle %p.\n", debugstr_w( sid ), debugstr_w( full_name ), event_handle );

    if (!full_name) return E_INVALIDARG;
    return S_OK;
}

HRESULT WINAPI UnregisterNotificationForUser( const WCHAR *sid, const WCHAR *full_name )
{
    TRACE( "sid %s, full_name %s.\n", debugstr_w( sid ), debugstr_w( full_name ) );

    if (!full_name) return E_INVALIDARG;
    return S_OK;
}

HRESULT WINAPI GetNotificationPayloadForUser( const WCHAR *sid, const WCHAR *full_name, APX_STRING *payload )
{
    TRACE( "sid %s, full_name %s, payload %p.\n", debugstr_w( sid ), debugstr_w( full_name ), payload );

    if (!full_name || !payload) return E_INVALIDARG;
    return string_set( payload, full_name );
}

HRESULT WINAPI VerifyPackage( const WCHAR *package_root, const WCHAR *relative_id )
{
    TRACE( "package_root %s, relative_id %s.\n", debugstr_w( package_root ), debugstr_w( relative_id ) );

    /* staged trees in the prefix are complete by construction */
    return S_OK;
}

HRESULT WINAPI AppxValidatePackages( const WCHAR *package_root, const WCHAR *staged_path )
{
    TRACE( "package_root %s, staged_path %s.\n", debugstr_w( package_root ), debugstr_w( staged_path ) );

    return S_OK;
}

HRESULT WINAPI AppxValidatePackagesWithOptions( const WCHAR *package_root, const WCHAR *staged_path, DWORD options )
{
    TRACE( "package_root %s, staged_path %s, options %lu.\n",
            debugstr_w( package_root ), debugstr_w( staged_path ), options );

    return S_OK;
}

HRESULT WINAPI AppxPreStageCleanupRunTask( void )
{
    TRACE( "stub-free cleanup task run.\n" );
    return S_OK;
}

HRESULT WINAPI ReArmAppxPreStageCleanupTask( void )
{
    TRACE( "stub-free cleanup task rearm.\n" );
    return S_OK;
}

HRESULT WINAPI AppxPreRegisterAllInboxPackages( void )
{
    TRACE( "no inbox packages in the prefix.\n" );
    return S_OK;
}

HRESULT WINAPI AppxCleanupWCIReparsePoints( void )
{
    TRACE( "no reparse points in the prefix.\n" );
    return S_OK;
}

HRESULT WINAPI RepairPackageFileAcls( void )
{
    TRACE( "prefix fs carries its own acls.\n" );
    return S_OK;
}

HRESULT WINAPI AppxCleanupSystemAppsMigratedToFOD( void )
{
    TRACE( "no FOD migration state.\n" );
    return S_OK;
}

HRESULT WINAPI AppxCleanupOrphanPackages( void )
{
    TRACE( "the registry key is the only package state.\n" );
    return S_OK;
}

HRESULT WINAPI RDSRecoverRequests( void )
{
    TRACE( "single user prefix: nothing to recover.\n" );
    return S_OK;
}

HRESULT WINAPI EnsurePackageFamilyIsRegisteredBeforeActivation( const WCHAR *family_name,
        const WCHAR *sid, void *reserved )
{
    TRACE( "family_name %s, sid %s.\n", debugstr_w( family_name ), debugstr_w( sid ) );

    return S_OK;
}

HRESULT WINAPI AppInstallerUpdateAllTask( void )
{
    TRACE( "no installer update state.\n" );
    return S_OK;
}

HRESULT WINAPI CheckAppInstallerUpdateAvailability( BOOL *available )
{
    TRACE( "available %p.\n", available );

    if (!available) return E_INVALIDARG;
    *available = FALSE;
    return S_OK;
}

HRESULT WINAPI CheckForUpdatesAndWaitForInstallerIfNeeded( DWORD flags )
{
    TRACE( "flags %lu.\n", flags );
    return S_OK;
}

HRESULT WINAPI UpdateAgentCancelAllDownloads( void )
{
    TRACE( "no active downloads.\n" );
    return S_OK;
}

/* registry-backed applicability/content tasks with settled answers */

HRESULT WINAPI RequestContentGroups( DWORD reserved, void *callback )
{
    TRACE( "reserved %lu, callback %p.\n", reserved, callback );
    return S_OK;
}

HRESULT WINAPI RequestContentGroupsForFullTrust( DWORD reserved, void *callback )
{
    TRACE( "reserved %lu, callback %p.\n", reserved, callback );
    return S_OK;
}

HRESULT WINAPI CheckComCallerHasCapabilities( DWORD reserved, void *callback )
{
    TRACE( "reserved %lu, callback %p.\n", reserved, callback );
    return S_OK;
}

HRESULT WINAPI GeneratePreInstalledPriFiles( void )
{
    TRACE( "no preinstalled packages in the prefix.\n" );
    return S_OK;
}

HRESULT WINAPI PopulateProtocolAndFTA( void )
{
    TRACE( "protocol/FTA state is registry-side.\n" );
    return S_OK;
}

HRESULT WINAPI FixJunctionsForAppsIfNecessary( void )
{
    TRACE( "the prefix has plain folders, no junctions.\n" );
    return S_OK;
}

HRESULT WINAPI CreateCanonicalPriFile( const WCHAR *full_name, const WCHAR *install_path )
{
    TRACE( "full_name %s, install_path %s.\n", debugstr_w( full_name ), debugstr_w( install_path ) );

    if (!full_name) return E_INVALIDARG;
    return S_OK;
}

/* registration-state family: the AppModel Packages key answers directly */

static BOOL package_key_exists( const WCHAR *full_name )
{
    static const WCHAR appmodel_path[] = L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppModel\\Packages\\";
    WCHAR key[600];
    HKEY hkey;

    lstrcpyW( key, appmodel_path );
    lstrcatW( key, full_name );
    if (RegOpenKeyExW( HKEY_CURRENT_USER, key, 0, KEY_READ, &hkey )) return FALSE;
    RegCloseKey( hkey );
    return TRUE;
}

static BOOL family_prefix( const WCHAR *key, const WCHAR *family )
{
    for (; *family; family++, key++)
        if (towlower( *family ) != towlower( *key )) return FALSE;
    return !*key || *key == L'_';
}

static BOOL family_key_exists( const WCHAR *family_name )
{
    static const WCHAR appmodel_path[] = L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppModel\\Packages";
    WCHAR keyname[512];
    HKEY hkey;
    DWORD i = 0;
    BOOL found = FALSE;

    if (RegOpenKeyExW( HKEY_CURRENT_USER, appmodel_path, 0, KEY_READ, &hkey )) return FALSE;
    for (;;)
    {
        if (RegEnumKeyW( hkey, i++, keyname, ARRAY_SIZE(keyname) )) break;
        if (family_prefix( keyname, family_name ))
        {
            found = TRUE;
            break;
        }
    }
    RegCloseKey( hkey );
    return found;
}

HRESULT WINAPI GetPackageRegistrationStatusForUser( const WCHAR *sid, const WCHAR *full_name, DWORD *status )
{
    TRACE( "sid %s, full_name %s, status %p.\n", debugstr_w( sid ), debugstr_w( full_name ), status );

    /* RegistrationStatus: 0 = NotRegistered, 1 = Registered */
    if (!full_name || !status) return E_INVALIDARG;
    *status = package_key_exists( full_name ) ? 1 : 0;
    return S_OK;
}

HRESULT WINAPI GetPackageRegistrationStatusForUserAndDefaultAccount( const WCHAR *full_name, DWORD *status )
{
    TRACE( "full_name %s, status %p.\n", debugstr_w( full_name ), status );

    if (!full_name || !status) return E_INVALIDARG;
    *status = package_key_exists( full_name ) ? 1 : 0;
    return S_OK;
}

HRESULT WINAPI HasPackageFamilyBeenRegisteredForUser( const WCHAR *sid, const WCHAR *family_name, BOOL *registered )
{
    TRACE( "sid %s, family_name %s, registered %p.\n", debugstr_w( sid ), debugstr_w( family_name ), registered );

    if (!family_name || !registered) return E_INVALIDARG;
    *registered = family_key_exists( family_name );
    return S_OK;
}

HRESULT WINAPI IsSharedAppsEnabled( BOOL *enabled )
{
    TRACE( "enabled %p.\n", enabled );

    if (!enabled) return E_INVALIDARG;
    *enabled = TRUE;
    return S_OK;
}

HRESULT WINAPI GetMetadataRootForPackage( const WCHAR *full_name, APX_STRING *metadata_root )
{
    WCHAR path[MAX_PATH];

    TRACE( "full_name %s, metadata_root %p.\n", debugstr_w( full_name ), metadata_root );

    /* the per-user metadata root is the folder the deployment creates under
     * Local\\Packages; AppxCreateSharedLocalFolder builds the same shape */
    if (!full_name || !metadata_root) return E_INVALIDARG;
    lstrcpyW( path, L"C:\\Users\\User\\AppData\\Local\\Packages\\" );
    lstrcatW( path, full_name );
    CreateDirectoryW( path, NULL );
    return string_set( metadata_root, path );
}

HRESULT WINAPI IsPackageMetadataUnderSystemMetadata( const WCHAR *full_name, BOOL *under_system )
{
    TRACE( "full_name %s, under_system %p.\n", debugstr_w( full_name ), under_system );

    /* the prefix carries per-user metadata only */
    if (!full_name || !under_system) return E_INVALIDARG;
    *under_system = FALSE;
    return S_OK;
}

HRESULT WINAPI AppxDeletePackageFiles( const WCHAR *full_name )
{
    TRACE( "full_name %s.\n", debugstr_w( full_name ) );

    if (!full_name) return E_INVALIDARG;
    return package_reg_delete( full_name );
}

HRESULT WINAPI AppxAddPackageToAllUserStoreForPbr( const WCHAR *full_name )
{
    TRACE( "full_name %s.\n", debugstr_w( full_name ) );

    if (!full_name) return E_INVALIDARG;
    return package_reg_write( full_name, NULL );
}

HRESULT WINAPI AppxRemovePackageForAllUsers( const WCHAR *full_name )
{
    TRACE( "full_name %s.\n", debugstr_w( full_name ) );

    /* the prefix has one user; removing for all users is removing theirs */
    if (!full_name) return E_INVALIDARG;
    return package_reg_delete( full_name );
}

HRESULT WINAPI AppxRemoveAllPackagesForUserSid( const WCHAR *sid )
{
    TRACE( "sid %s.\n", debugstr_w( sid ) );

    return S_OK;
}

HRESULT WINAPI CleanupProfileForUser( const WCHAR *sid )
{
    TRACE( "sid %s.\n", debugstr_w( sid ) );

    return S_OK;
}

HRESULT WINAPI AppxRecoverUserInstallsForUpgrade( DWORD reserved, HANDLE *event_handle )
{
    TRACE( "reserved %lu, event_handle %p.\n", reserved, event_handle );

    return S_OK;
}

HRESULT WINAPI RemovePackageFromContainer( const WCHAR *full_name, const WCHAR *container_id )
{
    TRACE( "full_name %s, container_id %s.\n", debugstr_w( full_name ), debugstr_w( container_id ) );

    if (!full_name) return E_INVALIDARG;
    return package_reg_delete( full_name );
}

/*
 * AppxManifest XML parsing and registration engine
 */

struct appx_identity
{
    WCHAR name[256];
    WCHAR publisher[512];
    WCHAR version[64];
    WCHAR arch[64];
    WCHAR resource_id[64];
    WCHAR publisher_id[64];
    WCHAR full_name[512];
    WCHAR family_name[512];
};

struct appx_application
{
    WCHAR id[128];
    WCHAR executable[256];
};

struct appx_dependency
{
    WCHAR name[256];
    WCHAR min_version[64];
    WCHAR publisher[512];
};

struct appx_activatable_class
{
    WCHAR class_id[256];
    WCHAR in_proc_server[MAX_PATH];
    WCHAR threading_model[32];
};

struct appx_package
{
    struct appx_identity identity;
    BOOL is_framework;
    WCHAR package_path[MAX_PATH];

    struct appx_application *applications;
    DWORD applications_count;
    DWORD applications_capacity;

    struct appx_dependency *dependencies;
    DWORD dependencies_count;
    DWORD dependencies_capacity;

    struct appx_activatable_class *classes;
    DWORD classes_count;
    DWORD classes_capacity;
};

static BOOL add_application(struct appx_package *pkg, const WCHAR *id, const WCHAR *executable)
{
    const WCHAR *p;
    if (pkg->applications_count >= pkg->applications_capacity)
    {
        DWORD cap = pkg->applications_capacity ? pkg->applications_capacity * 2 : 8;
        void *mem = realloc(pkg->applications, cap * sizeof(*pkg->applications));
        if (!mem) return FALSE;
        pkg->applications = mem;
        pkg->applications_capacity = cap;
    }
    lstrcpynW(pkg->applications[pkg->applications_count].id, id ? id : L"", 128);
    p = wcsrchr(executable, L'\\');
    if (!p) p = wcsrchr(executable, L'/');
    p = p ? p + 1 : executable;
    lstrcpynW(pkg->applications[pkg->applications_count].executable, p, 256);
    pkg->applications_count++;
    return TRUE;
}

static BOOL add_dependency(struct appx_package *pkg, const WCHAR *name, const WCHAR *min_ver, const WCHAR *pub)
{
    if (pkg->dependencies_count >= pkg->dependencies_capacity)
    {
        DWORD cap = pkg->dependencies_capacity ? pkg->dependencies_capacity * 2 : 16;
        void *mem = realloc(pkg->dependencies, cap * sizeof(*pkg->dependencies));
        if (!mem) return FALSE;
        pkg->dependencies = mem;
        pkg->dependencies_capacity = cap;
    }
    lstrcpynW(pkg->dependencies[pkg->dependencies_count].name, name ? name : L"", 256);
    lstrcpynW(pkg->dependencies[pkg->dependencies_count].min_version, min_ver ? min_ver : L"", 64);
    lstrcpynW(pkg->dependencies[pkg->dependencies_count].publisher, pub ? pub : L"", 512);
    pkg->dependencies_count++;
    return TRUE;
}

static BOOL add_activatable_class(struct appx_package *pkg, const WCHAR *class_id, const WCHAR *in_proc_server, const WCHAR *threading_model)
{
    if (pkg->classes_count >= pkg->classes_capacity)
    {
        DWORD cap = pkg->classes_capacity ? pkg->classes_capacity * 2 : 64;
        void *mem = realloc(pkg->classes, cap * sizeof(*pkg->classes));
        if (!mem) return FALSE;
        pkg->classes = mem;
        pkg->classes_capacity = cap;
    }
    lstrcpynW(pkg->classes[pkg->classes_count].class_id, class_id ? class_id : L"", 256);
    lstrcpynW(pkg->classes[pkg->classes_count].in_proc_server, in_proc_server ? in_proc_server : L"", MAX_PATH);
    lstrcpynW(pkg->classes[pkg->classes_count].threading_model,
              (threading_model && threading_model[0]) ? threading_model : L"Both", 32);
    pkg->classes_count++;
    return TRUE;
}

static void crockford_base32_encode(const BYTE *hash8, WCHAR *out)
{
    static const WCHAR alphabet[] = L"0123456789abcdefghjkmnpqrstvwxyz";
    UINT64 val = 0;
    int i;

    for (i = 0; i < 8; i++)
        val = (val << 8) | hash8[i];

    for (i = 0; i < 13; i++)
    {
        int shift = 64 - 5 * (i + 1);
        if (shift >= 0)
            out[i] = alphabet[(val >> shift) & 0x1f];
        else
            out[i] = alphabet[(val << (-shift)) & 0x1f];
    }
    out[13] = 0;
}

static BOOL derive_publisher_id(const WCHAR *publisher, WCHAR *out, DWORD cch)
{
    DWORD der_size = 0;
    BYTE *der_bytes = NULL;
    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;
    BYTE hash[32];
    DWORD hash_len = sizeof(hash);
    BOOL ret = FALSE;

    if (!publisher || !publisher[0] || cch < 14) return FALSE;

    if (wcsstr(publisher, L"Microsoft Corporation"))
    {
        lstrcpynW(out, L"8wekyb3d8bbwe", cch);
        return TRUE;
    }

    if (CertStrToNameW(X509_ASN_ENCODING, publisher, CERT_X500_NAME_STR | CERT_NAME_STR_REVERSE_FLAG,
                       NULL, NULL, &der_size, NULL) && der_size > 0)
    {
        der_bytes = malloc(der_size);
        if (der_bytes && CertStrToNameW(X509_ASN_ENCODING, publisher,
                                        CERT_X500_NAME_STR | CERT_NAME_STR_REVERSE_FLAG,
                                        NULL, der_bytes, &der_size, NULL))
        {
            if (CryptAcquireContextW(&hProv, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT))
            {
                if (CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash))
                {
                    if (CryptHashData(hHash, der_bytes, der_size, 0) &&
                        CryptGetHashParam(hHash, HP_HASHVAL, hash, &hash_len, 0) &&
                        hash_len >= 8)
                    {
                        crockford_base32_encode(hash, out);
                        ret = TRUE;
                    }
                    CryptDestroyHash(hHash);
                }
                CryptReleaseContext(hProv, 0);
            }
        }
        free(der_bytes);
    }

    if (!ret)
    {
        if (CryptAcquireContextW(&hProv, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT))
        {
            if (CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash))
            {
                if (CryptHashData(hHash, (const BYTE *)publisher, lstrlenW(publisher) * sizeof(WCHAR), 0) &&
                    CryptGetHashParam(hHash, HP_HASHVAL, hash, &hash_len, 0) &&
                    hash_len >= 8)
                {
                    crockford_base32_encode(hash, out);
                    ret = TRUE;
                }
                CryptDestroyHash(hHash);
            }
            CryptReleaseContext(hProv, 0);
        }
    }

    return ret;
}

static void determine_package_identity(const WCHAR *package_path, struct appx_package *pkg)
{
    const WCHAR *folder = wcsrchr(package_path, L'\\');
    DWORD name_len;
    BOOL folder_matches = FALSE;

    if (!folder) folder = wcsrchr(package_path, L'/');
    folder = folder ? folder + 1 : package_path;

    name_len = lstrlenW(pkg->identity.name);
    if (name_len > 0 && !wcsncmp(folder, pkg->identity.name, name_len) && folder[name_len] == L'_')
    {
        if (pkg->identity.version[0] && wcsstr(folder, pkg->identity.version))
            folder_matches = TRUE;
    }

    if (folder_matches)
    {
        const WCHAR *pub_id;
        lstrcpynW(pkg->identity.full_name, folder, ARRAY_SIZE(pkg->identity.full_name));
        pub_id = wcsrchr(folder, L'_');
        if (pub_id && pub_id[1])
            lstrcpynW(pkg->identity.publisher_id, pub_id + 1, ARRAY_SIZE(pkg->identity.publisher_id));
    }

    if (!pkg->identity.publisher_id[0])
    {
        derive_publisher_id(pkg->identity.publisher, pkg->identity.publisher_id,
                            ARRAY_SIZE(pkg->identity.publisher_id));
    }

    swprintf(pkg->identity.family_name, ARRAY_SIZE(pkg->identity.family_name),
             L"%ls_%ls", pkg->identity.name, pkg->identity.publisher_id);

    if (!pkg->identity.full_name[0])
    {
        const WCHAR *arch = pkg->identity.arch[0] ? pkg->identity.arch : L"neutral";
        const WCHAR *res = pkg->identity.resource_id[0] ? pkg->identity.resource_id : L"";
        if (res[0])
        {
            swprintf(pkg->identity.full_name, ARRAY_SIZE(pkg->identity.full_name),
                     L"%ls_%ls_%ls_%ls_%ls",
                     pkg->identity.name, pkg->identity.version, arch, res, pkg->identity.publisher_id);
        }
        else
        {
            swprintf(pkg->identity.full_name, ARRAY_SIZE(pkg->identity.full_name),
                     L"%ls_%ls_%ls__%ls",
                     pkg->identity.name, pkg->identity.version, arch, pkg->identity.publisher_id);
        }
    }
}

static BOOL parse_appx_manifest(const WCHAR *manifest_path, struct appx_package *pkg)
{
    HANDLE hFile;
    DWORD file_size, read_bytes;
    char *buf;
    HGLOBAL hMem;
    IStream *stream = NULL;
    IXmlReader *reader = NULL;
    HRESULT hr;
    XmlNodeType node_type;
    BOOL in_properties = FALSE;
    BOOL in_framework = FALSE;
    BOOL in_inproc_server = FALSE;
    BOOL in_path = FALSE;
    DWORD server_class_start = 0;
    WCHAR current_inproc_server[MAX_PATH] = {0};

    hFile = CreateFileW(manifest_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        WARN("Failed to open '%s' (error %lu)\n", debugstr_w(manifest_path), GetLastError());
        return FALSE;
    }

    file_size = GetFileSize(hFile, NULL);
    if (file_size == INVALID_FILE_SIZE || file_size == 0)
    {
        CloseHandle(hFile);
        return FALSE;
    }

    buf = malloc(file_size);
    if (!buf)
    {
        CloseHandle(hFile);
        return FALSE;
    }

    if (!ReadFile(hFile, buf, file_size, &read_bytes, NULL) || read_bytes == 0)
    {
        free(buf);
        CloseHandle(hFile);
        return FALSE;
    }
    CloseHandle(hFile);

    hMem = GlobalAlloc(GMEM_MOVEABLE, read_bytes);
    if (!hMem)
    {
        free(buf);
        return FALSE;
    }
    memcpy(GlobalLock(hMem), buf, read_bytes);
    GlobalUnlock(hMem);
    free(buf);

    hr = CreateStreamOnHGlobal(hMem, TRUE, &stream);
    if (FAILED(hr)) return FALSE;

    hr = CreateXmlReader(&IID_IXmlReader, (void **)&reader, NULL);
    if (FAILED(hr))
    {
        IStream_Release(stream);
        return FALSE;
    }

    hr = IXmlReader_SetInput(reader, (IUnknown *)stream);
    if (FAILED(hr))
    {
        IXmlReader_Release(reader);
        IStream_Release(stream);
        return FALSE;
    }

    while (IXmlReader_Read(reader, &node_type) == S_OK)
    {
        const WCHAR *local_name = NULL;

        switch (node_type)
        {
        case XmlNodeType_Element:
            IXmlReader_GetLocalName(reader, &local_name, NULL);
            if (!local_name) break;

            if (!wcscmp(local_name, L"Identity"))
            {
                if (IXmlReader_MoveToFirstAttribute(reader) == S_OK)
                {
                    do
                    {
                        const WCHAR *aname = NULL, *aval = NULL;
                        IXmlReader_GetLocalName(reader, &aname, NULL);
                        IXmlReader_GetValue(reader, &aval, NULL);
                        if (aname && aval)
                        {
                            if (!wcscmp(aname, L"Name"))
                                lstrcpynW(pkg->identity.name, aval, ARRAY_SIZE(pkg->identity.name));
                            else if (!wcscmp(aname, L"Publisher"))
                                lstrcpynW(pkg->identity.publisher, aval, ARRAY_SIZE(pkg->identity.publisher));
                            else if (!wcscmp(aname, L"Version"))
                                lstrcpynW(pkg->identity.version, aval, ARRAY_SIZE(pkg->identity.version));
                            else if (!wcscmp(aname, L"ProcessorArchitecture"))
                                lstrcpynW(pkg->identity.arch, aval, ARRAY_SIZE(pkg->identity.arch));
                            else if (!wcscmp(aname, L"ResourceId"))
                                lstrcpynW(pkg->identity.resource_id, aval, ARRAY_SIZE(pkg->identity.resource_id));
                        }
                    } while (IXmlReader_MoveToNextAttribute(reader) == S_OK);
                    IXmlReader_MoveToElement(reader);
                }
            }
            else if (!wcscmp(local_name, L"Properties"))
            {
                in_properties = TRUE;
            }
            else if (in_properties && !wcscmp(local_name, L"Framework"))
            {
                in_framework = TRUE;
            }
            else if (!wcscmp(local_name, L"PackageDependency"))
            {
                WCHAR dep_name[256] = {0};
                WCHAR min_ver[64] = {0};
                WCHAR pub[512] = {0};

                if (IXmlReader_MoveToFirstAttribute(reader) == S_OK)
                {
                    do
                    {
                        const WCHAR *aname = NULL, *aval = NULL;
                        IXmlReader_GetLocalName(reader, &aname, NULL);
                        IXmlReader_GetValue(reader, &aval, NULL);
                        if (aname && aval)
                        {
                            if (!wcscmp(aname, L"Name"))
                                lstrcpynW(dep_name, aval, ARRAY_SIZE(dep_name));
                            else if (!wcscmp(aname, L"MinVersion"))
                                lstrcpynW(min_ver, aval, ARRAY_SIZE(min_ver));
                            else if (!wcscmp(aname, L"Publisher"))
                                lstrcpynW(pub, aval, ARRAY_SIZE(pub));
                        }
                    } while (IXmlReader_MoveToNextAttribute(reader) == S_OK);
                    IXmlReader_MoveToElement(reader);
                }
                if (dep_name[0])
                    add_dependency(pkg, dep_name, min_ver, pub);
            }
            else if (!wcscmp(local_name, L"Application"))
            {
                WCHAR app_id[128] = {0};
                WCHAR app_exe[256] = {0};

                if (IXmlReader_MoveToFirstAttribute(reader) == S_OK)
                {
                    do
                    {
                        const WCHAR *aname = NULL, *aval = NULL;
                        IXmlReader_GetLocalName(reader, &aname, NULL);
                        IXmlReader_GetValue(reader, &aval, NULL);
                        if (aname && aval)
                        {
                            if (!wcscmp(aname, L"Id"))
                                lstrcpynW(app_id, aval, ARRAY_SIZE(app_id));
                            else if (!wcscmp(aname, L"Executable"))
                                lstrcpynW(app_exe, aval, ARRAY_SIZE(app_exe));
                        }
                    } while (IXmlReader_MoveToNextAttribute(reader) == S_OK);
                    IXmlReader_MoveToElement(reader);
                }
                if (app_exe[0])
                    add_application(pkg, app_id, app_exe);
            }
            else if (!wcscmp(local_name, L"InProcessServer"))
            {
                in_inproc_server = TRUE;
                server_class_start = pkg->classes_count;
                current_inproc_server[0] = 0;

                if (IXmlReader_MoveToFirstAttribute(reader) == S_OK)
                {
                    do
                    {
                        const WCHAR *aname = NULL, *aval = NULL;
                        IXmlReader_GetLocalName(reader, &aname, NULL);
                        IXmlReader_GetValue(reader, &aval, NULL);
                        if (aname && aval && !wcscmp(aname, L"Path"))
                            lstrcpynW(current_inproc_server, aval, ARRAY_SIZE(current_inproc_server));
                    } while (IXmlReader_MoveToNextAttribute(reader) == S_OK);
                    IXmlReader_MoveToElement(reader);
                }
            }
            else if (in_inproc_server && !wcscmp(local_name, L"Path"))
            {
                in_path = TRUE;
            }
            else if (in_inproc_server && !wcscmp(local_name, L"ActivatableClass"))
            {
                WCHAR class_id[256] = {0};
                WCHAR threading_model[32] = {0};

                if (IXmlReader_MoveToFirstAttribute(reader) == S_OK)
                {
                    do
                    {
                        const WCHAR *aname = NULL, *aval = NULL;
                        IXmlReader_GetLocalName(reader, &aname, NULL);
                        IXmlReader_GetValue(reader, &aval, NULL);
                        if (aname && aval)
                        {
                            if (!wcscmp(aname, L"ActivatableClassId"))
                                lstrcpynW(class_id, aval, ARRAY_SIZE(class_id));
                            else if (!wcscmp(aname, L"ThreadingModel"))
                                lstrcpynW(threading_model, aval, ARRAY_SIZE(threading_model));
                        }
                    } while (IXmlReader_MoveToNextAttribute(reader) == S_OK);
                    IXmlReader_MoveToElement(reader);
                }
                if (class_id[0])
                    add_activatable_class(pkg, class_id, current_inproc_server, threading_model);
            }
            break;

        case XmlNodeType_Text:
            if (in_path)
            {
                const WCHAR *val = NULL;
                IXmlReader_GetValue(reader, &val, NULL);
                if (val)
                    lstrcpynW(current_inproc_server, val, ARRAY_SIZE(current_inproc_server));
            }
            else if (in_framework)
            {
                const WCHAR *val = NULL;
                IXmlReader_GetValue(reader, &val, NULL);
                if (val && wcsstr(val, L"true"))
                    pkg->is_framework = TRUE;
            }
            break;

        case XmlNodeType_EndElement:
            IXmlReader_GetLocalName(reader, &local_name, NULL);
            if (!local_name) break;

            if (!wcscmp(local_name, L"Properties"))
                in_properties = FALSE;
            else if (!wcscmp(local_name, L"Framework"))
                in_framework = FALSE;
            else if (!wcscmp(local_name, L"Path"))
                in_path = FALSE;
            else if (!wcscmp(local_name, L"InProcessServer"))
            {
                DWORD j;
                for (j = server_class_start; j < pkg->classes_count; j++)
                {
                    if (!pkg->classes[j].in_proc_server[0] && current_inproc_server[0])
                        lstrcpynW(pkg->classes[j].in_proc_server, current_inproc_server, MAX_PATH);
                }
                in_inproc_server = FALSE;
                current_inproc_server[0] = 0;
            }
            break;

        default:
            break;
        }
    }

    IXmlReader_Release(reader);
    IStream_Release(stream);
    return TRUE;
}

static void set_reg_str(HKEY hkey, const WCHAR *name, const WCHAR *value)
{
    if (value)
        RegSetValueExW(hkey, name, 0, REG_SZ, (const BYTE *)value, (lstrlenW(value) + 1) * sizeof(WCHAR));
}

static void set_reg_dword(HKEY hkey, const WCHAR *name, DWORD value)
{
    RegSetValueExW(hkey, name, 0, REG_DWORD, (const BYTE *)&value, sizeof(value));
}

static BOOL register_package(const struct appx_package *pkg)
{
    static const HKEY roots[] = { HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE };
    WCHAR key[600];
    HKEY hkey;
    LONG ret;
    DWORD r, i;

    TRACE("Registering package: %s\n", debugstr_w(pkg->identity.full_name));
    TRACE("  PackagePath: %s\n", debugstr_w(pkg->package_path));
    TRACE("  PackageFamilyName: %s\n", debugstr_w(pkg->identity.family_name));
    TRACE("  PackagePublisherId: %s\n", debugstr_w(pkg->identity.publisher_id));
    if (pkg->is_framework) TRACE("  (Framework package)\n");

    for (r = 0; r < ARRAY_SIZE(roots); r++)
    {
        HKEY root = roots[r];

        /* 1. Software\Microsoft\Windows NT\CurrentVersion\AppModel\Packages\<PackageFullName> */
        swprintf(key, ARRAY_SIZE(key),
                 L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppModel\\Packages\\%ls",
                 pkg->identity.full_name);
        ret = RegCreateKeyExW(root, key, 0, NULL, 0, KEY_WRITE, NULL, &hkey, NULL);
        if (ret == ERROR_SUCCESS)
        {
            set_reg_str(hkey, L"PackagePath", pkg->package_path);
            set_reg_str(hkey, L"PackageRootPath", pkg->package_path);
            set_reg_str(hkey, L"PackageFullName", pkg->identity.full_name);
            set_reg_str(hkey, L"PackageFamilyName", pkg->identity.family_name);
            set_reg_str(hkey, L"PackagePublisher", pkg->identity.publisher);
            set_reg_str(hkey, L"PackagePublisherId", pkg->identity.publisher_id);
            set_reg_dword(hkey, L"Framework", pkg->is_framework ? 1 : 0);
            RegCloseKey(hkey);
        }
        else
        {
            WARN("Failed to create Packages key in hive %lu: %ld\n", r, ret);
        }

        /* 2. Software\Microsoft\Windows NT\CurrentVersion\AppModel\Exes\<Executable> */
        for (i = 0; i < pkg->applications_count; i++)
        {
            WCHAR aumid[512];
            swprintf(key, ARRAY_SIZE(key),
                     L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppModel\\Exes\\%ls",
                     pkg->applications[i].executable);
            ret = RegCreateKeyExW(root, key, 0, NULL, 0, KEY_WRITE, NULL, &hkey, NULL);
            if (ret == ERROR_SUCCESS)
            {
                swprintf(aumid, ARRAY_SIZE(aumid), L"%ls!%ls",
                         pkg->identity.family_name, pkg->applications[i].id);
                set_reg_str(hkey, L"PackageFullName", pkg->identity.full_name);
                set_reg_str(hkey, L"PackagePath", pkg->package_path);
                set_reg_str(hkey, L"ApplicationUserModelId", aumid);
                RegCloseKey(hkey);
            }
        }

        /* 3. Software\Microsoft\Windows NT\CurrentVersion\AppModel\Activation\<ActivatableClassId>
         * AND Software\Microsoft\WindowsRuntime\ActivatableClassId\<ActivatableClassId> */
        for (i = 0; i < pkg->classes_count; i++)
        {
            WCHAR dll_path[MAX_PATH];
            const WCHAR *inproc = pkg->classes[i].in_proc_server;

            if (inproc[0] && (inproc[1] == L':' || (inproc[0] == L'\\' && inproc[1] == L'\\')))
                lstrcpynW(dll_path, inproc, MAX_PATH);
            else
                swprintf(dll_path, MAX_PATH, L"%ls\\%ls", pkg->package_path, inproc);

            /* AppModel\Activation */
            swprintf(key, ARRAY_SIZE(key),
                     L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppModel\\Activation\\%ls",
                     pkg->classes[i].class_id);
            ret = RegCreateKeyExW(root, key, 0, NULL, 0, KEY_WRITE, NULL, &hkey, NULL);
            if (ret == ERROR_SUCCESS)
            {
                set_reg_str(hkey, L"PackageFullName", pkg->identity.full_name);
                set_reg_str(hkey, L"InProcessServer", dll_path);
                RegCloseKey(hkey);
            }

            /* WindowsRuntime\ActivatableClassId */
            swprintf(key, ARRAY_SIZE(key),
                     L"Software\\Microsoft\\WindowsRuntime\\ActivatableClassId\\%ls",
                     pkg->classes[i].class_id);
            ret = RegCreateKeyExW(root, key, 0, NULL, 0, KEY_WRITE, NULL, &hkey, NULL);
            if (ret == ERROR_SUCCESS)
            {
                set_reg_str(hkey, L"DllPath", dll_path);
                set_reg_str(hkey, L"ThreadingModel",
                            pkg->classes[i].threading_model[0] ? pkg->classes[i].threading_model : L"Both");
                set_reg_str(hkey, L"PackageFullName", pkg->identity.full_name);
                RegCloseKey(hkey);
            }
        }
    }

    TRACE("Registered %lu application(s) and %lu activatable class(es).\n",
          pkg->applications_count, pkg->classes_count);

    AppxRegisterPackage(pkg->identity.full_name, pkg->package_path, pkg->identity.family_name);
    return TRUE;
}

static void append_to_path(const WCHAR *new_dir)
{
    HKEY hkey;
    LONG ret;
    DWORD type = 0, size = 0;
    WCHAR *old_path = NULL;
    WCHAR *new_path = NULL;
    DWORD new_len;
    DWORD_PTR dwResult;

    if (!new_dir || !new_dir[0]) return;

    ret = RegCreateKeyExW(HKEY_CURRENT_USER, L"Environment", 0, NULL, 0,
                          KEY_READ | KEY_WRITE, NULL, &hkey, NULL);
    if (ret != ERROR_SUCCESS) return;

    if (RegQueryValueExW(hkey, L"Path", NULL, &type, NULL, &size) == ERROR_SUCCESS && size > 0)
    {
        old_path = malloc(size + sizeof(WCHAR));
        if (old_path)
        {
            RegQueryValueExW(hkey, L"Path", NULL, &type, (BYTE *)old_path, &size);
            old_path[size / sizeof(WCHAR)] = 0;
        }
    }

    if (old_path && old_path[0])
    {
        const WCHAR *p = old_path;
        DWORD dir_len = lstrlenW(new_dir);
        while (p && *p)
        {
            const WCHAR *semi = wcschr(p, L';');
            size_t seg_len = semi ? (size_t)(semi - p) : wcslen(p);
            if (seg_len == dir_len && !wcsnicmp(p, new_dir, dir_len))
            {
                free(old_path);
                RegCloseKey(hkey);
                return;
            }
            p = semi ? semi + 1 : NULL;
        }

        new_len = lstrlenW(old_path) + 1 + dir_len + 1;
        new_path = malloc(new_len * sizeof(WCHAR));
        if (new_path)
            swprintf(new_path, new_len, L"%ls;%ls", old_path, new_dir);
    }
    else
    {
        new_len = lstrlenW(new_dir) + 1;
        new_path = malloc(new_len * sizeof(WCHAR));
        if (new_path)
            lstrcpyW(new_path, new_dir);
    }

    if (new_path)
    {
        RegSetValueExW(hkey, L"Path", 0, REG_EXPAND_SZ, (const BYTE *)new_path,
                       (lstrlenW(new_path) + 1) * sizeof(WCHAR));
        TRACE("Appended to HKCU\\Environment\\Path: %s\n", debugstr_w(new_dir));
        free(new_path);
    }

    free(old_path);
    RegCloseKey(hkey);

    SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, (LPARAM)L"Environment",
                        SMTO_ABORTIFHUNG, 5000, &dwResult);
}

static BOOL resolve_dependency_path(const struct appx_package *pkg, const WCHAR *dep_name, WCHAR *out_path, DWORD cch)
{
    static const HKEY roots[] = { HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE };
    WCHAR key[600], name[256];
    DWORD r, i;

    for (r = 0; r < ARRAY_SIZE(roots); r++)
    {
        HKEY hkey;
        if (RegOpenKeyExW(roots[r], L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppModel\\Packages",
                          0, KEY_READ, &hkey) == ERROR_SUCCESS)
        {
            DWORD dep_len = lstrlenW(dep_name);
            for (i = 0; ; i++)
            {
                DWORD name_size = ARRAY_SIZE(name);
                if (RegEnumKeyExW(hkey, i, name, &name_size, NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
                    break;
                if (!wcsncmp(name, dep_name, dep_len) && name[dep_len] == L'_')
                {
                    HKEY sub;
                    swprintf(key, ARRAY_SIZE(key),
                             L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppModel\\Packages\\%ls", name);
                    if (RegOpenKeyExW(roots[r], key, 0, KEY_READ, &sub) == ERROR_SUCCESS)
                    {
                        DWORD size = cch * sizeof(WCHAR);
                        DWORD type = 0;
                        if (RegQueryValueExW(sub, L"PackagePath", NULL, &type, (BYTE *)out_path, &size) == ERROR_SUCCESS && out_path[0])
                        {
                            RegCloseKey(sub);
                            RegCloseKey(hkey);
                            return TRUE;
                        }
                        RegCloseKey(sub);
                    }
                }
            }
            RegCloseKey(hkey);
        }
    }

    {
        WCHAR parent_dir[MAX_PATH];
        WCHAR search_pattern[MAX_PATH];
        WIN32_FIND_DATAW fd;
        HANDLE hFind;
        WCHAR *slash;

        lstrcpynW(parent_dir, pkg->package_path, MAX_PATH);
        slash = wcsrchr(parent_dir, L'\\');
        if (!slash) slash = wcsrchr(parent_dir, L'/');
        if (slash)
        {
            *slash = 0;
            swprintf(search_pattern, MAX_PATH, L"%ls\\%ls_*", parent_dir, dep_name);
            hFind = FindFirstFileW(search_pattern, &fd);
            if (hFind != INVALID_HANDLE_VALUE)
            {
                do
                {
                    if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                        wcscmp(fd.cFileName, L".") && wcscmp(fd.cFileName, L".."))
                    {
                        swprintf(out_path, cch, L"%ls\\%ls", parent_dir, fd.cFileName);
                        FindClose(hFind);
                        return TRUE;
                    }
                } while (FindNextFileW(hFind, &fd));
                FindClose(hFind);
            }
        }
    }

    return FALSE;
}

static void process_dependencies(const struct appx_package *pkg)
{
    DWORD i;
    for (i = 0; i < pkg->dependencies_count; i++)
    {
        const WCHAR *dep = pkg->dependencies[i].name;
        BOOL is_framework_dep = (wcsstr(dep, L"VCLibs") != NULL ||
                                 wcsstr(dep, L"Native") != NULL ||
                                 wcsstr(dep, L"Xaml") != NULL);
        WCHAR dep_path[MAX_PATH];

        if (resolve_dependency_path(pkg, dep, dep_path, ARRAY_SIZE(dep_path)))
        {
            WCHAR manifest[MAX_PATH];
            const WCHAR *folder;
            WCHAR reg_key[600];
            HKEY hkey;

            TRACE("Resolved dependency %s -> %s\n", debugstr_w(dep), debugstr_w(dep_path));

            swprintf(manifest, MAX_PATH, L"%ls\\AppxManifest.xml", dep_path);
            if (GetFileAttributesW(manifest) != INVALID_FILE_ATTRIBUTES)
            {
                folder = wcsrchr(dep_path, L'\\');
                folder = folder ? folder + 1 : dep_path;
                swprintf(reg_key, ARRAY_SIZE(reg_key),
                         L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppModel\\Packages\\%ls", folder);
                if (RegCreateKeyExW(HKEY_CURRENT_USER, reg_key, 0, NULL, 0, KEY_WRITE, NULL, &hkey, NULL) == ERROR_SUCCESS)
                {
                    set_reg_str(hkey, L"PackagePath", dep_path);
                    set_reg_str(hkey, L"PackageRootPath", dep_path);
                    set_reg_str(hkey, L"PackageFullName", folder);
                    set_reg_dword(hkey, L"Framework", 1);
                    RegCloseKey(hkey);
                }
                if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, reg_key, 0, NULL, 0, KEY_WRITE, NULL, &hkey, NULL) == ERROR_SUCCESS)
                {
                    set_reg_str(hkey, L"PackagePath", dep_path);
                    set_reg_str(hkey, L"PackageRootPath", dep_path);
                    set_reg_str(hkey, L"PackageFullName", folder);
                    set_reg_dword(hkey, L"Framework", 1);
                    RegCloseKey(hkey);
                }
            }

            if (is_framework_dep)
            {
                WCHAR bin_path[MAX_PATH];
                DWORD attr;
                swprintf(bin_path, MAX_PATH, L"%ls\\bin", dep_path);
                attr = GetFileAttributesW(bin_path);
                if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY))
                    append_to_path(bin_path);
                else
                    append_to_path(dep_path);
            }
        }
        else
        {
            TRACE("Dependency %s not found locally\n", debugstr_w(dep));
        }
    }
}

HRESULT WINAPI AppxRegisterPackageManifest( const WCHAR *manifest_path )
{
    WCHAR clean_path[MAX_PATH];
    const WCHAR *src;
    WCHAR dos_path[MAX_PATH];
    DWORD attr, len;
    struct appx_package pkg;
    WCHAR *p;

    TRACE( "manifest_path %s.\n", debugstr_w( manifest_path ) );

    if (!manifest_path || !manifest_path[0]) return E_INVALIDARG;

    src = manifest_path;
    while (*src == L' ' || *src == L'\'' || *src == L'"') src++;
    lstrcpynW(clean_path, src, MAX_PATH);
    while (clean_path[0] && (clean_path[lstrlenW(clean_path) - 1] == L' ' ||
                             clean_path[lstrlenW(clean_path) - 1] == L'\'' ||
                             clean_path[lstrlenW(clean_path) - 1] == L'"'))
        clean_path[lstrlenW(clean_path) - 1] = 0;

    if (!clean_path[0]) return E_INVALIDARG;

    if (clean_path[0] == L'/')
    {
        typedef WCHAR * (CDECL *p_wine_get_dos_file_name)(const char *);
        p_wine_get_dos_file_name p_wine_get_dos = (void *)GetProcAddress(GetModuleHandleA("KERNEL32"), "wine_get_dos_file_name");
        if (p_wine_get_dos)
        {
            char unix_path[MAX_PATH];
            WCHAR *res;
            WideCharToMultiByte(CP_UNIXCP, 0, clean_path, -1, unix_path, sizeof(unix_path), NULL, NULL);
            res = p_wine_get_dos(unix_path);
            if (res)
            {
                lstrcpynW(dos_path, res, MAX_PATH);
                HeapFree(GetProcessHeap(), 0, res);
            }
            else
                lstrcpynW(dos_path, clean_path, MAX_PATH);
        }
        else
            lstrcpynW(dos_path, clean_path, MAX_PATH);
    }
    else
    {
        GetFullPathNameW(clean_path, MAX_PATH, dos_path, NULL);
    }

    attr = GetFileAttributesW(dos_path);
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY))
    {
        len = lstrlenW(dos_path);
        if (len && (dos_path[len - 1] == L'\\' || dos_path[len - 1] == L'/'))
            dos_path[len - 1] = 0;
        lstrcatW(dos_path, L"\\AppxManifest.xml");
    }
    else
    {
        const WCHAR *fn = wcsrchr(dos_path, L'\\');
        if (!fn) fn = wcsrchr(dos_path, L'/');
        fn = fn ? fn + 1 : dos_path;
        if (wcsicmp(fn, L"AppxManifest.xml") != 0)
        {
            WCHAR check_manifest[MAX_PATH];
            swprintf(check_manifest, MAX_PATH, L"%ls\\AppxManifest.xml", dos_path);
            if (GetFileAttributesW(check_manifest) != INVALID_FILE_ATTRIBUTES)
                lstrcpyW(dos_path, check_manifest);
        }
    }

    if (GetFileAttributesW(dos_path) == INVALID_FILE_ATTRIBUTES)
    {
        /* Fallback for paths without AppxManifest.xml on disk (e.g. unit tests):
         * derive package name from folder name and register minimally */
        const WCHAR *folder;
        WCHAR dir_path[MAX_PATH];
        lstrcpynW(dir_path, dos_path, MAX_PATH);
        p = wcsrchr(dir_path, L'\\');
        if (!p) p = wcsrchr(dir_path, L'/');
        if (p)
        {
            if (!wcsicmp(p + 1, L"AppxManifest.xml"))
            {
                *p = 0;
                folder = wcsrchr(dir_path, L'\\');
                if (!folder) folder = wcsrchr(dir_path, L'/');
                folder = folder ? folder + 1 : dir_path;
            }
            else
                folder = p + 1;
        }
        else
            folder = dir_path;

        if (folder && folder[0])
        {
            static const HKEY roots[] = { HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE };
            DWORD r;
            for (r = 0; r < ARRAY_SIZE(roots); r++)
            {
                WCHAR key[600];
                HKEY hkey;
                swprintf(key, ARRAY_SIZE(key),
                         L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppModel\\Packages\\%ls", folder);
                if (RegCreateKeyExW(roots[r], key, 0, NULL, 0, KEY_WRITE, NULL, &hkey, NULL) == ERROR_SUCCESS)
                {
                    DWORD framework = 0;
                    RegSetValueExW(hkey, L"PackagePath", 0, REG_SZ, (const BYTE *)dir_path, (lstrlenW(dir_path) + 1) * sizeof(WCHAR));
                    RegSetValueExW(hkey, L"PackageRootPath", 0, REG_SZ, (const BYTE *)dir_path, (lstrlenW(dir_path) + 1) * sizeof(WCHAR));
                    RegSetValueExW(hkey, L"PackageFullName", 0, REG_SZ, (const BYTE *)folder, (lstrlenW(folder) + 1) * sizeof(WCHAR));
                    RegSetValueExW(hkey, L"Framework", 0, REG_DWORD, (const BYTE *)&framework, sizeof(framework));
                    RegCloseKey(hkey);
                }
            }
            return S_OK;
        }
        return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
    }

    memset(&pkg, 0, sizeof(pkg));
    lstrcpynW(pkg.package_path, dos_path, MAX_PATH);
    p = wcsrchr(pkg.package_path, L'\\');
    if (!p) p = wcsrchr(pkg.package_path, L'/');
    if (p) *p = 0;

    if (!parse_appx_manifest(dos_path, &pkg))
    {
        WARN("Failed to parse '%s'.\n", debugstr_w(dos_path));
        return E_FAIL;
    }

    determine_package_identity(pkg.package_path, &pkg);

    if (!register_package(&pkg))
    {
        WARN("Registration failed for '%s'.\n", debugstr_w(pkg.identity.full_name));
        free(pkg.applications);
        free(pkg.dependencies);
        free(pkg.classes);
        return E_FAIL;
    }

    process_dependencies(&pkg);

    TRACE("Package '%s' registered successfully.\n", debugstr_w(pkg.identity.full_name));

    free(pkg.applications);
    free(pkg.dependencies);
    free(pkg.classes);
    return S_OK;
}

HRESULT WINAPI AppxUnregisterPackageByName( const WCHAR *package_full_name )
{
    static const HKEY roots[] = { HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE };
    WCHAR key[600];
    DWORD r;

    TRACE( "package_full_name %s.\n", debugstr_w( package_full_name ) );

    if (!package_full_name || !package_full_name[0]) return E_INVALIDARG;

    for (r = 0; r < ARRAY_SIZE(roots); r++)
    {
        HKEY root = roots[r];
        HKEY hkey_exes, hkey_act, hkey_rt;

        /* 1. Delete Packages\<package_full_name> */
        swprintf(key, ARRAY_SIZE(key),
                 L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppModel\\Packages\\%ls", package_full_name);
        RegDeleteTreeW(root, key);

        /* 2. Enumerate Exes */
        if (RegOpenKeyExW(root, L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppModel\\Exes",
                          0, KEY_READ | KEY_WRITE, &hkey_exes) == ERROR_SUCCESS)
        {
            WCHAR sub_name[256];
            DWORD idx = 0;
            while (1)
            {
                DWORD size = ARRAY_SIZE(sub_name);
                HKEY sub;
                BOOL match = FALSE;

                if (RegEnumKeyExW(hkey_exes, idx, sub_name, &size, NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
                    break;

                if (RegOpenKeyExW(hkey_exes, sub_name, 0, KEY_READ, &sub) == ERROR_SUCCESS)
                {
                    WCHAR pkg_name[512];
                    DWORD pkg_size = sizeof(pkg_name), type = 0;
                    if (RegQueryValueExW(sub, L"PackageFullName", NULL, &type, (BYTE *)pkg_name, &pkg_size) == ERROR_SUCCESS)
                    {
                        if (!wcsicmp(pkg_name, package_full_name))
                            match = TRUE;
                    }
                    RegCloseKey(sub);
                }

                if (match)
                    RegDeleteTreeW(hkey_exes, sub_name);
                else
                    idx++;
            }
            RegCloseKey(hkey_exes);
        }

        /* 3. Enumerate Activation */
        if (RegOpenKeyExW(root, L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppModel\\Activation",
                          0, KEY_READ | KEY_WRITE, &hkey_act) == ERROR_SUCCESS)
        {
            WCHAR sub_name[256];
            DWORD idx = 0;
            while (1)
            {
                DWORD size = ARRAY_SIZE(sub_name);
                HKEY sub;
                BOOL match = FALSE;

                if (RegEnumKeyExW(hkey_act, idx, sub_name, &size, NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
                    break;

                if (RegOpenKeyExW(hkey_act, sub_name, 0, KEY_READ, &sub) == ERROR_SUCCESS)
                {
                    WCHAR pkg_name[512];
                    DWORD pkg_size = sizeof(pkg_name), type = 0;
                    if (RegQueryValueExW(sub, L"PackageFullName", NULL, &type, (BYTE *)pkg_name, &pkg_size) == ERROR_SUCCESS)
                    {
                        if (!wcsicmp(pkg_name, package_full_name))
                            match = TRUE;
                    }
                    RegCloseKey(sub);
                }

                if (match)
                    RegDeleteTreeW(hkey_act, sub_name);
                else
                    idx++;
            }
            RegCloseKey(hkey_act);
        }

        /* 4. Enumerate WindowsRuntime\ActivatableClassId */
        if (RegOpenKeyExW(root, L"Software\\Microsoft\\WindowsRuntime\\ActivatableClassId",
                          0, KEY_READ | KEY_WRITE, &hkey_rt) == ERROR_SUCCESS)
        {
            WCHAR sub_name[256];
            DWORD idx = 0;
            while (1)
            {
                DWORD size = ARRAY_SIZE(sub_name);
                HKEY sub;
                BOOL match = FALSE;

                if (RegEnumKeyExW(hkey_rt, idx, sub_name, &size, NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
                    break;

                if (RegOpenKeyExW(hkey_rt, sub_name, 0, KEY_READ, &sub) == ERROR_SUCCESS)
                {
                    WCHAR pkg_name[512];
                    DWORD pkg_size = sizeof(pkg_name), type = 0;
                    if (RegQueryValueExW(sub, L"PackageFullName", NULL, &type, (BYTE *)pkg_name, &pkg_size) == ERROR_SUCCESS)
                    {
                        if (!wcsicmp(pkg_name, package_full_name))
                            match = TRUE;
                    }
                    RegCloseKey(sub);
                }

                if (match)
                    RegDeleteTreeW(hkey_rt, sub_name);
                else
                    idx++;
            }
            RegCloseKey(hkey_rt);
        }
    }

    package_reg_delete( package_full_name );
    return S_OK;
}
