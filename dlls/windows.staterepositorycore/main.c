/*
 * State repository cache (ext-ms-onecore-appmodel-staterepository-cache-*)
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

/*
 * The state repository is the database describing installed packages, maintained by the
 * StateRepository service. There is no such service here: package identity comes from the
 * environment (see kernelbase/version.c), so every cache lookup reports "no repository" and
 * callers fall back to reading the package manifest directly.
 */

#include <stdarg.h>

#include <stdarg.h>

#include "windef.h"
#include "winbase.h"
#include "winerror.h"
#include "winreg.h"
#include "sddl.h"
#include "objbase.h"
#include "appmodel.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(srcache);

#define SR_E_UNAVAILABLE HRESULT_FROM_WIN32( ERROR_SERVICE_NOT_ACTIVE )

/*
 * The cache is the client side of the StateRepository service's database. The
 * service is not here; the package registration kernelbase answers identity from
 * (the AppModel key in the registry, see dlls/kernelbase/version.c) describes the
 * same set of installed packages, so a cache opens against it. Of the context's
 * fields the Store-app consumers probe, the registration records two: Roaming
 * (the package's state follows the user, and the registration plane is the
 * per-user view over the machine) and InstanceId (the instance of the context,
 * stable for the package the context was opened for). Fields the registration
 * does not record answer zero, which is what an empty cache answers.
 */
#define SRCACHE_CONTEXT_TAG 0x57696e5352436163ULL /* "WinSRcac" */

/* Context field ids as the SRPC consumers index them (the SRCACHECONTEXT_FIELDS
 * enum): the paths are what the StateCacheManager QueryState plane asks for,
 * Roaming and InstanceId are what the Store-app consumers probe first. */
#define CXT_PATH          1 /* the package's state repository path */
#define CXT_ROAMING       2
#define CXT_INSTANCE      3 /* its text form answers through GetField_String */
#define CXT_SECURE        4
#define CXT_TARGET        5
#define CXT_MACHINE_PATH  6 /* the machine copy of the state: same file here */
#define CXT_DATAPKG_PATH  7
#define CXT_DATAPKG_TEMP  8
#define CXT_DATAPKG_ROAM  9
#define CXT_DATAPKG_CACHE 10
#define CXT_DATAPKG_PERSIST 11
#define CXT_ROAM_NOTIF    12

struct srcache_context
{
    UINT64 tag;
    WCHAR *name;
    GUID instance_id;
};

HRESULT WINAPI SRCache_DuplicateString( const WCHAR *str, WCHAR **copy );

/* The instance id is derived from the context name, so the same context opened
 * twice for the same package reports the same id. */
static GUID context_instance_id( const WCHAR *name )
{
    static const WCHAR base[] = L"Windows.Internal.StateRepository.Cache";
    UINT64 h1 = 0xcbf29ce484222325ULL, h2 = 0xcbf29ce484222325ULL;
    GUID guid;
    const WCHAR *s;

    for (s = base; *s; s++) h1 = (h1 ^ *s) * 0x100000001b3ULL;
    for (s = name; name && *s; s++) h2 = (h2 ^ *s) * 0x100000001b3ULL;

    guid.Data1 = (DWORD)(h1 >> 32);
    guid.Data2 = (WORD)(h1 >> 16);
    guid.Data3 = (WORD)h1;
    guid.Data4[0] = (UCHAR)(h2 >> 56);
    guid.Data4[1] = (UCHAR)(h2 >> 48);
    guid.Data4[2] = (UCHAR)(h2 >> 40);
    guid.Data4[3] = (UCHAR)(h2 >> 32);
    guid.Data4[4] = (UCHAR)(h2 >> 24);
    guid.Data4[5] = (UCHAR)(h2 >> 16);
    guid.Data4[6] = (UCHAR)(h2 >> 8);
    guid.Data4[7] = (UCHAR)h2;
    return guid;
}

/*
 * A top-level context's name is the package full name whose state is queried
 * (subcontexts append '\'-separated parts to it); the registration the cache
 * answers from is kernelbase's: Packages\<FullName> under the AppModel key,
 * HKCU over HKLM (see dlls/kernelbase/version.c). An empty name is the running
 * package, which is the root the C++/WinRT QueryState plane opens.
 */
static const WCHAR appmodel_reg_path[] = L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppModel";

/* The part of the context name that names the package. */
static void context_package_name( const struct srcache_context *ctx, WCHAR *buf, DWORD cch )
{
    SIZE_T i = 0;

    if (!ctx || !ctx->name) { buf[0] = 0; return; }
    while (ctx->name[i] && ctx->name[i] != L'\\' && i + 1 < cch)
    {
        buf[i] = ctx->name[i];
        i++;
    }
    buf[i] = 0;
}

/* Read one REG_SZ from the package's registration, HKCU over HKLM. */
static BOOL package_read_str( const WCHAR *full_name, const WCHAR *value, WCHAR *buf, DWORD cch )
{
    static const HKEY roots[] = { HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE };
    WCHAR path[600];
    SIZE_T i;

    if (!full_name || !full_name[0]) return FALSE;
    for (i = 0; i < ARRAY_SIZE(roots); i++)
    {
        HKEY hkey;
        DWORD type = 0, size = cch * sizeof(WCHAR);

        lstrcpyW( path, appmodel_reg_path );
        lstrcatW( path, L"\\Packages\\" );
        lstrcatW( path, full_name );
        if (RegOpenKeyExW( roots[i], path, 0, KEY_READ, &hkey )) continue;
        buf[0] = 0;
        if (RegQueryValueExW( hkey, value, NULL, &type, (BYTE *)buf, &size ) == ERROR_SUCCESS &&
            (type == REG_SZ || type == REG_EXPAND_SZ))
        {
            RegCloseKey( hkey );
            return TRUE;
        }
        RegCloseKey( hkey );
    }
    return FALSE;
}

/* Install path of the package the context names: the running package answers
 * from its own identity (the env pin and the Exes/Identity registration that
 * GetCurrentPackagePath reads), every other package from its deployment
 * record; PackageRootPath is the root the installer saw, an acceptable answer
 * for a deployment that records only that one. */
static BOOL package_install_path( const struct srcache_context *ctx, WCHAR *buf, DWORD cch )
{
    WCHAR full[PACKAGE_FULL_NAME_MAX_LENGTH + 1];
    UINT32 len;

    context_package_name( ctx, full, ARRAY_SIZE(full) );
    if (!full[0])
    {
        len = ARRAY_SIZE(full);
        if (GetCurrentPackageFullName( &len, full )) full[0] = 0;
    }
    if (full[0])
    {
        len = cch;
        if (!GetCurrentPackagePath( &len, buf ) && buf[0]) return TRUE;
        if (package_read_str( full, L"PackagePath", buf, cch )) return TRUE;
        if (package_read_str( full, L"PackageRootPath", buf, cch )) return TRUE;
    }
    return FALSE;
}

/* Append a subpath to a directory, normalising the separator. */
static BOOL join_path( WCHAR *buf, SIZE_T cch, const WCHAR *sub )
{
    SIZE_T len = lstrlenW( buf );

    if (len && buf[len - 1] == L'\\') len--;
    if (len + lstrlenW( sub ) + 2 > cch) return FALSE;
    buf[len] = L'\\';
    lstrcpyW( buf + len + 1, sub );
    return TRUE;
}

/* Answer one of the path fields: the package's recorded StateRepositoryPath
 * for Path/MachinePath (the convention root is the package's AppData folder),
 * the AppData layout of the install path for the DataPackage family. A package
 * that is not registered answers the empty cache: S_OK and no string. */
static HRESULT answer_path_field( struct srcache_context *ctx, ULONG field, WCHAR **value )
{
    static const WCHAR datappath[] = L"AppData\\Local";
    static const WCHAR dataptemp[] = L"AppData\\Local\\Temp";
    static const WCHAR dataproam[] = L"AppData\\Roaming";
    static const WCHAR datapcache[] = L"AppData\\Local\\Microsoft\\Windows\\INetCache";
    static const WCHAR datappersist[] = L"AppData\\Local\\PersistentStorage";
    static const WCHAR roamnotif[] = L"AppData\\Roaming\\Microsoft\\Windows\\Start Menu\\Programs";
    const WCHAR *sub;
    WCHAR path[600];

    switch (field)
    {
    case CXT_PATH:
    case CXT_MACHINE_PATH:
        {
            WCHAR full[PACKAGE_FULL_NAME_MAX_LENGTH + 1];
            context_package_name( ctx, full, ARRAY_SIZE(full) );
            if (package_read_str( full, L"StateRepositoryPath", path, ARRAY_SIZE(path) ))
                return SRCache_DuplicateString( path, value );
            sub = L"AppData";
        }
        break;
    case CXT_DATAPKG_PATH:    sub = datappath; break;
    case CXT_DATAPKG_TEMP:    sub = dataptemp; break;
    case CXT_DATAPKG_ROAM:    sub = dataproam; break;
    case CXT_DATAPKG_CACHE:   sub = datapcache; break;
    case CXT_DATAPKG_PERSIST: sub = datappersist; break;
    case CXT_ROAM_NOTIF:      sub = roamnotif; break;
    default:
        return S_OK;
    }
    if (!package_install_path( ctx, path, ARRAY_SIZE(path) )) return S_OK;
    if (!join_path( path, ARRAY_SIZE(path), sub )) return E_OUTOFMEMORY;
    return SRCache_DuplicateString( path, value );
}

static HRESULT context_open( const WCHAR *name, struct srcache_context **ret )
{
    struct srcache_context *ctx;

    if (!(ctx = HeapAlloc( GetProcessHeap(), 0, sizeof(*ctx) ))) return E_OUTOFMEMORY;
    ctx->tag = SRCACHE_CONTEXT_TAG;
    ctx->instance_id = context_instance_id( name );
    if (name && !SUCCEEDED( SRCache_DuplicateString( name, &ctx->name ) ))
    {
        HeapFree( GetProcessHeap(), 0, ctx );
        return E_OUTOFMEMORY;
    }
    ctx->name = name ? ctx->name : NULL;
    *ret = ctx;
    return S_OK;
}

HRESULT WINAPI SRCacheManager_Open( void *params, void **manager )
{
    static int once;

    if (!once++) FIXME( "%p %p: answering from the package registration\n", params, manager );
    if (manager) *manager = NULL;  /* the registration needs no handle state */
    return SR_E_UNAVAILABLE;
}

void WINAPI SRCacheManager_Close( void *manager )
{
    TRACE( "%p\n", manager );
}

HRESULT WINAPI SRCacheManager_GetProperty_UInt32( void *manager, ULONG property, ULONG *value )
{
    TRACE( "%p %lu %p\n", manager, property, value );
    if (value) *value = 0;
    return SR_E_UNAVAILABLE;
}

HRESULT WINAPI SRCacheManager_SetProperty_UInt32( void *manager, ULONG property, ULONG value )
{
    TRACE( "%p %lu %lu\n", manager, property, value );
    return SR_E_UNAVAILABLE;
}

HRESULT WINAPI SRCacheManager_SetProperty_UInt64( void *manager, ULONG property, ULONG64 value )
{
    TRACE( "%p %lu %s\n", manager, property, wine_dbgstr_longlong(value) );
    return SR_E_UNAVAILABLE;
}

HRESULT WINAPI SRCacheContext_Open( void *manager, const WCHAR *name, void **context )
{
    struct srcache_context *ctx;
    HRESULT hr;

    TRACE( "%p %s %p\n", manager, debugstr_w(name), context );
    if (!context) return E_INVALIDARG;
    *context = NULL;
    if ((hr = context_open( name, &ctx )) != S_OK) return hr;
    *context = ctx;
    return S_OK;
}

HRESULT WINAPI SRCacheContext_OpenSubContext( void *context, const WCHAR *name, void **sub_context )
{
    struct srcache_context *parent = context, *ctx;
    WCHAR *path;
    HRESULT hr;

    TRACE( "%p %s %p\n", context, debugstr_w(name), sub_context );
    if (!sub_context) return E_INVALIDARG;
    *sub_context = NULL;
    if ((hr = context_open( name, &ctx )) != S_OK) return hr;
    if (parent && parent->tag == SRCACHE_CONTEXT_TAG && parent->name && name)
    {
        SIZE_T len = lstrlenW( parent->name ) + lstrlenW( name ) + 2;
        if ((path = HeapAlloc( GetProcessHeap(), 0, len * sizeof(WCHAR) )))
        {
            lstrcpyW( path, parent->name );
            lstrcatW( path, L"\\" );
            lstrcatW( path, name );
            HeapFree( GetProcessHeap(), 0, parent->name );
            ctx->name = path;
            ctx->instance_id = context_instance_id( path );
        }
    }
    *sub_context = ctx;
    return S_OK;
}

void WINAPI SRCacheContext_Close( void *context )
{
    struct srcache_context *ctx = context;

    TRACE( "%p\n", context );
    if (ctx && ctx->tag == SRCACHE_CONTEXT_TAG)
    {
        HeapFree( GetProcessHeap(), 0, ctx->name );
        HeapFree( GetProcessHeap(), 0, ctx );
    }
}

HRESULT WINAPI SRCacheContext_IsEmpty( void *context, BOOL *empty )
{
    struct srcache_context *ctx = context;
    WCHAR path[600];

    TRACE( "%p %p\n", context, empty );
    if (!empty) return E_INVALIDARG;
    /* a registered package has a row in the cache the context opens against */
    *empty = !(ctx && ctx->tag == SRCACHE_CONTEXT_TAG && package_install_path( ctx, path, ARRAY_SIZE(path) ));
    return S_OK;
}

HRESULT WINAPI SRCacheContext_EnumerateData( void *context, void *callback, void *param )
{
    TRACE( "%p %p %p\n", context, callback, param );
    return SR_E_UNAVAILABLE;
}

HRESULT WINAPI SRCacheContext_AddToCache( void *context, void *data )
{
    TRACE( "%p %p\n", context, data );
    return SR_E_UNAVAILABLE;
}

HRESULT WINAPI SRCacheContext_GetField_UInt32( void *context, ULONG field, ULONG *value )
{
    TRACE( "%p %lu %p\n", context, field, value );
    if (value) *value = (field == CXT_ROAMING);
    return S_OK;
}

HRESULT WINAPI SRCacheContext_GetField_UInt64( void *context, ULONG field, ULONG64 *value )
{
    TRACE( "%p %lu %p\n", context, field, value );
    if (value) *value = 0;
    return S_OK;
}

HRESULT WINAPI SRCacheContext_GetField_String( void *context, ULONG field, WCHAR **value )
{
    struct srcache_context *ctx = context;

    TRACE( "%p %lu %p\n", context, field, value );
    if (!value) return E_INVALIDARG;
    *value = NULL;
    if (!ctx || ctx->tag != SRCACHE_CONTEXT_TAG) return S_OK;
    if (field == CXT_INSTANCE)
    {
        WCHAR text[40];

        if (!StringFromGUID2( &ctx->instance_id, text, ARRAY_SIZE(text) )) return SR_E_UNAVAILABLE;
        return SRCache_DuplicateString( text, value );
    }
    return answer_path_field( ctx, field, value );
}

HRESULT WINAPI SRCacheContext_GetField_MultiString( void *context, ULONG field, WCHAR **value )
{
    TRACE( "%p %lu %p\n", context, field, value );
    if (value) *value = NULL;
    return SR_E_UNAVAILABLE;
}

HRESULT WINAPI SRCacheContext_GetField_Binary( void *context, ULONG field, void **value, ULONG *size )
{
    TRACE( "%p %lu %p %p\n", context, field, value, size );
    if (value) *value = NULL;
    if (size) *size = 0;
    return SR_E_UNAVAILABLE;
}

HRESULT WINAPI SRCacheContext_GetField_Guid( void *context, ULONG field, GUID *value )
{
    struct srcache_context *ctx = context;

    TRACE( "%p %lu %p\n", context, field, value );
    if (!value) return E_INVALIDARG;
    if (field == CXT_INSTANCE && ctx && ctx->tag == SRCACHE_CONTEXT_TAG) *value = ctx->instance_id;
    else memset( value, 0, sizeof(*value) );
    return S_OK;
}

HRESULT WINAPI SRCache_AllocStringBuffer( ULONG len, WCHAR **buffer )
{
    TRACE( "%lu %p\n", len, buffer );
    if (!buffer) return E_INVALIDARG;
    if (!(*buffer = HeapAlloc( GetProcessHeap(), HEAP_ZERO_MEMORY, (len + 1) * sizeof(WCHAR) ))) return E_OUTOFMEMORY;
    return S_OK;
}

HRESULT WINAPI SRCache_DuplicateString( const WCHAR *str, WCHAR **copy )
{
    SIZE_T size;

    TRACE( "%s %p\n", debugstr_w(str), copy );
    if (!copy) return E_INVALIDARG;
    if (!str) { *copy = NULL; return S_OK; }
    size = (lstrlenW( str ) + 1) * sizeof(WCHAR);
    if (!(*copy = HeapAlloc( GetProcessHeap(), 0, size ))) return E_OUTOFMEMORY;
    memcpy( *copy, str, size );
    return S_OK;
}

void WINAPI SRCache_Free( void *ptr )
{
    TRACE( "%p\n", ptr );
    HeapFree( GetProcessHeap(), 0, ptr );
}

HRESULT WINAPI SRCache_ExpandMacros( void *context, const WCHAR *str, WCHAR **expanded )
{
    TRACE( "%p %s %p\n", context, debugstr_w(str), expanded );
    return SRCache_DuplicateString( str, expanded );
}

/* The account the state follows: the process's user, the one user a prefix has. */
HRESULT WINAPI SRCache_GetDefaultAccountSid( WCHAR **sid )
{
    BYTE buffer[sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE];
    HANDLE token;
    WCHAR *text = NULL;
    DWORD len;
    BOOL ret;

    TRACE( "%p\n", sid );
    if (!sid) return E_INVALIDARG;
    *sid = NULL;
    if (!OpenProcessToken( GetCurrentProcess(), TOKEN_QUERY, &token )) return SR_E_UNAVAILABLE;
    ret = GetTokenInformation( token, TokenUser, buffer, sizeof(buffer), &len ) &&
          ConvertSidToStringSidW( ((TOKEN_USER *)buffer)->User.Sid, &text );
    CloseHandle( token );
    if (!ret || !text) return SR_E_UNAVAILABLE;
    ret = SUCCEEDED( SRCache_DuplicateString( text, sid ) );
    LocalFree( text );
    return ret ? S_OK : E_OUTOFMEMORY;
}
