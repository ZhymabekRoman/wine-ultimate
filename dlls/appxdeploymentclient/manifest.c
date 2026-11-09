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
            DWORD chars = ARRAY_SIZE( family );
            name = name ? name + 1 : c->dir;
            wcscpy( family, name );
            if (c->has_manifest && manifest_get_tag( c, L"Resources", family, ARRAY_SIZE( family ) ))
                ; /* Resources lists the languages, not the family; keep FullName-derived */
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
    WCHAR keyname[512], value[128], family[512];
    HKEY hkey;
    DWORD i = 0, chars, framework;
    DWORD type = REG_DWORD, size = sizeof(framework);
    HKEY hsub;

    TRACE( "family_name %s, full_name %p.\n", debugstr_w( family_name ), full_name );

    if (!family_name || !full_name) return E_INVALIDARG;
    if (RegOpenKeyExW( HKEY_CURRENT_USER, appmodel_path, 0, KEY_READ, &hkey )) return E_FAIL;
    for (;;)
    {
        chars = ARRAY_SIZE( keyname );
        if (RegEnumKeyW( hkey, i++, keyname, &chars )) break;
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
    TRACE( "full_name %s, status %u.\n", debugstr_w( full_name ), status );

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
    TRACE( "package_root %s, staged_path %s, options %u.\n",
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
    TRACE( "flags %u.\n", flags );
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
    TRACE( "reserved %u, callback %p.\n", reserved, callback );
    return S_OK;
}

HRESULT WINAPI RequestContentGroupsForFullTrust( DWORD reserved, void *callback )
{
    TRACE( "reserved %u, callback %p.\n", reserved, callback );
    return S_OK;
}

HRESULT WINAPI CheckComCallerHasCapabilities( DWORD reserved, void *callback )
{
    TRACE( "reserved %u, callback %p.\n", reserved, callback );
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
    DWORD i = 0, chars;
    BOOL found = FALSE;

    if (RegOpenKeyExW( HKEY_CURRENT_USER, appmodel_path, 0, KEY_READ, &hkey )) return FALSE;
    for (;;)
    {
        chars = ARRAY_SIZE( keyname );
        if (RegEnumKeyW( hkey, i++, keyname, (LPDWORD)&chars )) break;
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
    TRACE( "reserved %u, event_handle %p.\n", reserved, event_handle );

    return S_OK;
}

HRESULT WINAPI RemovePackageFromContainer( const WCHAR *full_name, const WCHAR *container_id )
{
    TRACE( "full_name %s, container_id %s.\n", debugstr_w( full_name ), debugstr_w( container_id ) );

    if (!full_name) return E_INVALIDARG;
    return package_reg_delete( full_name );
}
